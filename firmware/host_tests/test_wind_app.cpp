#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

extern "C" {
#include "wind_app.h"
#include "wind_timezone.h"
}

static std::string read_wind_app_source()
{
    std::ifstream input(WIND_APP_SOURCE);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

struct FakeIo {
    std::string forecast_path;
    wind_cache_identity_t identity{"edam", "Europe/Amsterdam", "knmi_seamless"};
    esp_err_t fetch_result = ESP_OK;
    esp_err_t display_result = ESP_OK;
    int fetches = 0;
    int renders = 0;
    int displays = 0;
    bool cache_was_published_before_render = false;
    wind_freshness_t rendered_freshness = WIND_FRESHNESS_UNAVAILABLE;
    bool rendered_refresh_failed = false;
};

static wind_forecast_t app_forecast(int64_t retrieved_at)
{
    wind_forecast_t value;
    wind_forecast_clear(&value);
    std::strcpy(value.spot_id, "edam");
    std::strcpy(value.spot_name, "Edam");
    std::strcpy(value.timezone, "Europe/Amsterdam");
    std::strcpy(value.provider, "open-meteo");
    std::strcpy(value.model, "knmi_seamless");
    value.latitude = 52.5;
    value.longitude = 5.0;
    value.retrieved_at = retrieved_at;
    const int hours[] = {8, 11, 14, 17, 20};
    for (int d = 0; d < 5; ++d) {
        std::snprintf(value.days[d].local_date, sizeof(value.days[d].local_date), "2026-08-%02d",
                      24 + d);
        for (int s = 0; s < 5; ++s) {
            value.days[d].samples[s] = {retrieved_at + d * 86400 + s * 10800,
                                        static_cast<uint8_t>(hours[s]), 12, 18, 90};
        }
    }
    return value;
}

static wind_forecast_t old_window_forecast(int64_t now)
{
    wind_forecast_t value = app_forecast(now - 5 * 86400);
    for (int d = 0; d < 5; ++d) {
        std::snprintf(value.days[d].local_date, sizeof(value.days[d].local_date), "2026-08-%02d",
                      19 + d);
    }
    return value;
}

static esp_err_t fake_fetch(void *context, int64_t now, wind_forecast_t *out)
{
    auto *fake = static_cast<FakeIo *>(context);
    ++fake->fetches;
    if (fake->fetch_result == ESP_OK) {
        *out = app_forecast(now);
    }
    return fake->fetch_result;
}

static esp_err_t fake_render(void *context, const wind_forecast_t *forecast,
                             wind_freshness_t freshness, bool refresh_failed, int64_t now,
                             uint8_t *bitmap, size_t bitmap_size)
{
    (void) now;
    auto *fake = static_cast<FakeIo *>(context);
    ++fake->renders;
    fake->rendered_freshness = freshness;
    fake->rendered_refresh_failed = refresh_failed;
    wind_forecast_t persisted;
    fake->cache_was_published_before_render =
        forecast && wind_cache_load(fake->forecast_path.c_str(), &fake->identity, &persisted) == ESP_OK;
    std::memset(bitmap, static_cast<int>(freshness) + 1 + (refresh_failed ? 8 : 0), bitmap_size);
    return ESP_OK;
}

static esp_err_t fake_display(void *context, const uint8_t *bitmap, size_t bitmap_size)
{
    auto *fake = static_cast<FakeIo *>(context);
    ++fake->displays;
    EXPECT_NE(bitmap, nullptr);
    EXPECT_GT(bitmap_size, 0u);
    return fake->display_result;
}

class WindAppTest : public testing::Test {
  protected:
    void SetUp() override
    {
        root = std::filesystem::temp_directory_path() / "einkwind-app-test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        fake.forecast_path = (root / "forecast").string();
        panel_storage = panel_path();
        schedule_storage = schedule_path();
        initialize_app(1);
    }
    void initialize_app(uint64_t render_signature)
    {
        wind_app_config_t config = {
            .provider = {.fetch = fake_fetch, .context = &fake},
            .identity = fake.identity,
            .forecast_cache_path = fake.forecast_path.c_str(),
            .panel_cache_path = panel_storage.c_str(),
            .schedule_path = schedule_storage.c_str(),
            .render_signature = render_signature,
            .bitmap_size = 32,
            .render = fake_render,
            .display = fake_display,
            .io_context = &fake,
        };
        ASSERT_EQ(wind_app_init(&app, &config), ESP_OK);
    }
    void TearDown() override { std::filesystem::remove_all(root); }
    std::string panel_path() const { return (root / "panel").string(); }
    std::string schedule_path() const { return (root / "schedule").string(); }
    std::filesystem::path root;
    std::string panel_storage;
    std::string schedule_storage;
    FakeIo fake;
    wind_app_t app{};
};

TEST_F(WindAppTest, PublishesForecastBeforeRenderAndConfirmsOnlyAfterDisplay)
{
    wind_app_outcome_t outcome;
    ASSERT_EQ(wind_app_run(&app, true, 1787544000, &outcome), ESP_OK);
    EXPECT_TRUE(outcome.published_forecast);
    EXPECT_TRUE(fake.cache_was_published_before_render);
    EXPECT_TRUE(outcome.displayed);
    uint64_t hash = 0;
    EXPECT_EQ(wind_cache_panel_load(panel_storage.c_str(), 1, &hash), ESP_OK);
}

TEST_F(WindAppTest, SuppressesIdenticalConfirmedBitmap)
{
    wind_app_outcome_t first;
    ASSERT_EQ(wind_app_run(&app, true, 1787544000, &first), ESP_OK);
    wind_app_outcome_t second;
    ASSERT_EQ(wind_app_run(&app, false, 1787544060, &second), ESP_OK);
    EXPECT_TRUE(second.display_unchanged);
    EXPECT_EQ(fake.displays, 1);
}

TEST_F(WindAppTest, PrefetchUpdatesCacheWithoutRenderingOrDisplaying)
{
    wind_app_outcome_t outcome;
    ASSERT_EQ(wind_app_prefetch(&app, true, 1787544000, &outcome), ESP_OK);
    EXPECT_TRUE(outcome.published_forecast);
    EXPECT_EQ(fake.fetches, 1);
    EXPECT_EQ(fake.renders, 0);
    EXPECT_EQ(fake.displays, 0);
}

TEST_F(WindAppTest, ShowCachedNeverFetches)
{
    auto forecast = app_forecast(1787544000);
    ASSERT_EQ(wind_cache_store(fake.forecast_path.c_str(), &forecast), ESP_OK);
    wind_app_outcome_t outcome;
    ASSERT_EQ(wind_app_show_cached(&app, 1787544060, &outcome), ESP_OK);
    EXPECT_EQ(fake.fetches, 0);
    EXPECT_EQ(fake.renders, 1);
    EXPECT_EQ(fake.displays, 1);
    EXPECT_TRUE(outcome.used_cache);
}

TEST_F(WindAppTest, RendererSignatureBumpDisplaysOnceThenSkipsUnchangedBitmap)
{
    wind_app_outcome_t first;
    ASSERT_EQ(wind_app_run(&app, true, 1787544000, &first), ESP_OK);
    ASSERT_EQ(fake.displays, 1);

    initialize_app(2);
    wind_app_outcome_t bumped;
    ASSERT_EQ(wind_app_run(&app, false, 1787544060, &bumped), ESP_OK);
    EXPECT_TRUE(bumped.displayed);
    EXPECT_FALSE(bumped.display_unchanged);
    EXPECT_EQ(fake.displays, 2);

    wind_app_outcome_t unchanged;
    ASSERT_EQ(wind_app_run(&app, false, 1787544120, &unchanged), ESP_OK);
    EXPECT_TRUE(unchanged.display_unchanged);
    EXPECT_EQ(fake.displays, 2);
}

TEST_F(WindAppTest, FailedDisplayInvalidatesConfirmationForRetry)
{
    fake.display_result = ESP_FAIL;
    wind_app_outcome_t failed;
    EXPECT_EQ(wind_app_run(&app, true, 1787544000, &failed), ESP_FAIL);
    uint64_t hash = 0;
    EXPECT_EQ(wind_cache_panel_load(panel_storage.c_str(), 1, &hash), ESP_ERR_NOT_FOUND);
    fake.display_result = ESP_OK;
    wind_app_outcome_t retry;
    EXPECT_EQ(wind_app_run(&app, false, 1787544060, &retry), ESP_OK);
    EXPECT_TRUE(retry.displayed);
}

TEST_F(WindAppTest, FirstBootFailureRendersUnavailableWithoutFabricatedForecast)
{
    fake.fetch_result = ESP_ERR_TIMEOUT;
    wind_app_outcome_t outcome;
    ASSERT_EQ(wind_app_run(&app, true, 1787544000, &outcome), ESP_OK);
    EXPECT_EQ(fake.rendered_freshness, WIND_FRESHNESS_UNAVAILABLE);
    EXPECT_FALSE(fake.cache_was_published_before_render);
    EXPECT_FALSE(outcome.published_forecast);
}

TEST_F(WindAppTest, FirstBootFailureRetriesOnceAfterFiveMinutes)
{
    const int64_t now = 1787544000;
    fake.fetch_result = ESP_ERR_TIMEOUT;

    wind_app_outcome_t first;
    ASSERT_EQ(wind_app_run(&app, true, now, &first), ESP_OK);
    ASSERT_EQ(fake.fetches, 1);

    wind_app_outcome_t too_soon;
    ASSERT_EQ(wind_app_run(&app, false, now + 4 * 60, &too_soon), ESP_OK);
    EXPECT_FALSE(too_soon.attempted_fetch);
    EXPECT_EQ(fake.fetches, 1);

    wind_app_outcome_t retry;
    ASSERT_EQ(wind_app_run(&app, false, now + 5 * 60, &retry), ESP_OK);
    EXPECT_TRUE(retry.attempted_fetch);
    EXPECT_EQ(fake.fetches, 2);

    wind_app_outcome_t no_loop;
    ASSERT_EQ(wind_app_run(&app, false, now + 10 * 60, &no_loop), ESP_OK);
    EXPECT_FALSE(no_loop.attempted_fetch);
    EXPECT_EQ(fake.fetches, 2);
}

TEST_F(WindAppTest, FutureDatedCacheRendersUnavailable)
{
    const int64_t now = 1787544000;
    auto forecast = app_forecast(now + 3600);
    ASSERT_EQ(wind_cache_store(fake.forecast_path.c_str(), &forecast), ESP_OK);
    fake.fetch_result = ESP_ERR_TIMEOUT;

    wind_app_outcome_t outcome;
    ASSERT_EQ(wind_app_run(&app, false, now, &outcome), ESP_OK);

    EXPECT_TRUE(outcome.used_cache);
    EXPECT_EQ(fake.rendered_freshness, WIND_FRESHNESS_UNAVAILABLE);
}

TEST_F(WindAppTest, OutOfWindowCacheFetchesOnceThenReturnsToNormalSchedule)
{
    const int64_t now = 1787544000;
    wind_app_outcome_t initial;
    ASSERT_EQ(wind_app_run(&app, true, now, &initial), ESP_OK);
    ASSERT_EQ(fake.fetches, 1);

    auto old = old_window_forecast(now);
    ASSERT_EQ(wind_cache_store(fake.forecast_path.c_str(), &old), ESP_OK);

    wind_app_outcome_t recovered;
    ASSERT_EQ(wind_app_run(&app, false, now + 60, &recovered), ESP_OK);
    EXPECT_TRUE(recovered.attempted_fetch);
    EXPECT_TRUE(recovered.published_forecast);
    EXPECT_EQ(fake.fetches, 2);

    wind_app_outcome_t same_boundary;
    ASSERT_EQ(wind_app_run(&app, false, now + 120, &same_boundary), ESP_OK);
    EXPECT_FALSE(same_boundary.attempted_fetch);
    EXPECT_EQ(fake.fetches, 2);

    int64_t next_boundary = wind_schedule_next_boundary(fake.identity.timezone,
                                                         (time_t) (now + 120));
    wind_app_outcome_t scheduled;
    ASSERT_EQ(wind_app_run(&app, false, next_boundary, &scheduled), ESP_OK);
    EXPECT_TRUE(scheduled.attempted_fetch);
    EXPECT_EQ(fake.fetches, 3);
}

TEST_F(WindAppTest, FailedOutOfWindowRecoveryRetriesOnceAfterFiveMinutes)
{
    const int64_t now = 1787544000;
    wind_app_outcome_t initial;
    ASSERT_EQ(wind_app_run(&app, true, now, &initial), ESP_OK);

    auto old = old_window_forecast(now);
    ASSERT_EQ(wind_cache_store(fake.forecast_path.c_str(), &old), ESP_OK);
    fake.fetch_result = ESP_ERR_TIMEOUT;

    wind_app_outcome_t failed;
    ASSERT_EQ(wind_app_run(&app, false, now + 60, &failed), ESP_OK);
    EXPECT_TRUE(failed.attempted_fetch);
    EXPECT_TRUE(failed.used_cache);
    ASSERT_EQ(fake.fetches, 2);

    wind_app_outcome_t too_soon;
    ASSERT_EQ(wind_app_run(&app, false, now + 5 * 60, &too_soon), ESP_OK);
    EXPECT_FALSE(too_soon.attempted_fetch);
    EXPECT_TRUE(too_soon.used_cache);

    wind_app_outcome_t retry;
    ASSERT_EQ(wind_app_run(&app, false, now + 6 * 60, &retry), ESP_OK);
    EXPECT_TRUE(retry.attempted_fetch);
    EXPECT_TRUE(retry.used_cache);
    EXPECT_EQ(fake.fetches, 3);

    wind_app_outcome_t no_loop;
    ASSERT_EQ(wind_app_run(&app, false, now + 11 * 60, &no_loop), ESP_OK);
    EXPECT_FALSE(no_loop.attempted_fetch);
    EXPECT_EQ(fake.fetches, 3);
}

TEST_F(WindAppTest, FailedRefreshKeepsValidCacheAndMarksDashboardOffline)
{
    const int64_t now = 1787544000;
    wind_app_outcome_t first;
    ASSERT_EQ(wind_app_run(&app, true, now, &first), ESP_OK);
    ASSERT_TRUE(first.displayed);
    ASSERT_FALSE(fake.rendered_refresh_failed);

    fake.fetch_result = ESP_ERR_TIMEOUT;
    wind_app_outcome_t second;
    ASSERT_EQ(wind_app_run(&app, true, now + 60, &second), ESP_OK);

    EXPECT_TRUE(second.attempted_fetch);
    EXPECT_TRUE(second.used_cache);
    EXPECT_EQ(second.fetch_result, ESP_ERR_TIMEOUT);
    EXPECT_EQ(fake.rendered_freshness, WIND_FRESHNESS_FRESH);
    EXPECT_TRUE(fake.rendered_refresh_failed);
    EXPECT_TRUE(second.displayed);
}

TEST_F(WindAppTest, MissedRetryDoesNotConsumeTheNextBoundaryRetry)
{
    const int64_t now = 1787544000;
    wind_app_outcome_t initial;
    ASSERT_EQ(wind_app_run(&app, true, now, &initial), ESP_OK);

    fake.fetch_result = ESP_ERR_TIMEOUT;
    const int64_t first_boundary = wind_schedule_next_boundary(fake.identity.timezone,
                                                                (time_t) now);
    wind_app_outcome_t first_failure;
    ASSERT_EQ(wind_app_run(&app, false, first_boundary, &first_failure), ESP_OK);
    ASSERT_TRUE(first_failure.attempted_fetch);

    const int64_t second_boundary = wind_schedule_next_boundary(fake.identity.timezone,
                                                                 (time_t) first_boundary);
    wind_app_outcome_t second_failure;
    ASSERT_EQ(wind_app_run(&app, false, second_boundary, &second_failure), ESP_OK);
    ASSERT_TRUE(second_failure.attempted_fetch);

    wind_app_outcome_t retry;
    ASSERT_EQ(wind_app_run(&app, false, second_boundary + 5 * 60, &retry), ESP_OK);
    EXPECT_TRUE(retry.attempted_fetch);
    EXPECT_EQ(fake.fetches, 4);
}

TEST(WindAppProductionContractTest, SendsHeartbeatOnlyAfterRefreshLocksAreReleased)
{
    const std::string source = read_wind_app_source();
    ASSERT_FALSE(source.empty());

    EXPECT_NE(source.find("wind_app_refresh_unlocked(true, NULL)"), std::string::npos);
    EXPECT_NE(source.find("wind_app_refresh_unlocked(bool force_refresh, bool *published_forecast)"),
              std::string::npos);

    const size_t public_refresh = source.find("esp_err_t wind_app_refresh(bool force_refresh)");
    ASSERT_NE(public_refresh, std::string::npos);
    const size_t refresh_call = source.find(
        "wind_app_refresh_unlocked(force_refresh, &published_forecast)", public_refresh);
    const size_t unlock = source.find("xSemaphoreGive(s_runtime_lock)", refresh_call);
    const size_t heartbeat = source.find("wind_analytics_maybe_send", unlock);

    ASSERT_NE(refresh_call, std::string::npos);
    ASSERT_NE(unlock, std::string::npos);
    ASSERT_NE(heartbeat, std::string::npos);
    EXPECT_LT(refresh_call, unlock);
    EXPECT_LT(unlock, heartbeat);
    EXPECT_NE(source.find("if (published_forecast)", unlock), std::string::npos);
}
