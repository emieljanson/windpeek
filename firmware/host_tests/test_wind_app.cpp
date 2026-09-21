#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

extern "C" {
#include "wind_app.h"
#include "wind_app_status.h"
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

    EXPECT_NE(source.find("wind_app_refresh_unlocked(true, &outcome)"), std::string::npos);
    EXPECT_NE(source.find("wind_app_refresh_unlocked(bool force_refresh, wind_app_outcome_t *outcome)"),
              std::string::npos);

    const size_t public_refresh = source.find("esp_err_t wind_app_refresh(bool force_refresh)");
    ASSERT_NE(public_refresh, std::string::npos);
    const size_t refresh_call = source.find(
        "wind_app_refresh_unlocked(force_refresh, &outcome)", public_refresh);
    const size_t unlock = source.find("xSemaphoreGive(s_runtime_lock)", refresh_call);
    const size_t heartbeat = source.find("wind_analytics_maybe_send", unlock);

    ASSERT_NE(refresh_call, std::string::npos);
    ASSERT_NE(unlock, std::string::npos);
    ASSERT_NE(heartbeat, std::string::npos);
    EXPECT_LT(refresh_call, unlock);
    EXPECT_LT(unlock, heartbeat);
    EXPECT_NE(source.find("if (outcome.published_forecast)", unlock), std::string::npos);
}

TEST_F(WindAppTest, ForcedDisplayIgnoresStaleDiskConfirmationAndThenResumesDeduplication)
{
    wind_app_outcome_t outcome;
    ASSERT_EQ(wind_app_run(&app, true, 1787544000, &outcome), ESP_OK);
    uint64_t hash = 0;
    ASSERT_EQ(wind_cache_panel_load(panel_storage.c_str(), 1, &hash), ESP_OK);
    // Simulate an out-of-band screen whose disk confirmation could not be removed.
    app.force_display = true;
    ASSERT_EQ(wind_app_run(&app, false, 1787544060, &outcome), ESP_OK);
    EXPECT_TRUE(outcome.displayed);
    EXPECT_FALSE(outcome.display_unchanged);
    EXPECT_FALSE(app.force_display);
    ASSERT_EQ(wind_app_run(&app, false, 1787544120, &outcome), ESP_OK);
    EXPECT_TRUE(outcome.display_unchanged);
    EXPECT_EQ(fake.displays, 2);
}

TEST_F(WindAppTest, FailedForcedDisplayKeepsTheOverride)
{
    wind_app_outcome_t outcome;
    ASSERT_EQ(wind_app_run(&app, true, 1787544000, &outcome), ESP_OK);
    app.force_display = true;
    fake.display_result = ESP_FAIL;
    EXPECT_EQ(wind_app_run(&app, false, 1787544060, &outcome), ESP_FAIL);
    EXPECT_TRUE(app.force_display);
}

TEST(WindAppProductionContractTest, NavigationHeartbeatRequiresPublishedForecastAndReleasedLocks)
{
    const auto source = read_wind_app_source();
    const auto start = source.find("static esp_err_t navigate(int direction, bool absolute)");
    const auto end = source.find("esp_err_t wind_app_select_previous", start);
    ASSERT_NE(start, std::string::npos);
    ASSERT_NE(end, std::string::npos);
    const auto body = source.substr(start, end - start);
    const auto run = body.find("wind_app_run(&runtime->app, !have_cache, now, &outcome)");
    const auto app_unlock = body.find("xSemaphoreGive(s_app_lock)", run);
    const auto runtime_unlock = body.find("xSemaphoreGive(s_runtime_lock)", app_unlock);
    const auto published = body.find("if (outcome.published_forecast)", runtime_unlock);
    const auto heartbeat = body.find("wind_analytics_maybe_send(now)", published);
    EXPECT_NE(body.find("wind_app_outcome_t outcome = {0}"), std::string::npos);
    ASSERT_NE(run, std::string::npos);
    ASSERT_NE(app_unlock, std::string::npos);
    ASSERT_NE(runtime_unlock, std::string::npos);
    ASSERT_NE(published, std::string::npos);
    ASSERT_NE(heartbeat, std::string::npos);
    EXPECT_LT(run, app_unlock);
    EXPECT_LT(app_unlock, runtime_unlock);
    EXPECT_LT(runtime_unlock, published);
    EXPECT_LT(published, heartbeat);
    EXPECT_NE(body.find("return result;", heartbeat), std::string::npos);
}

TEST_F(WindAppTest, OverviewPrefetchConsumesFailedRetryWithoutWritingIndividualScreen)
{
    const int64_t now=1787544000;
    fake.fetch_result=ESP_ERR_TIMEOUT;
    wind_app_outcome_t outcome{};
    ASSERT_EQ(wind_app_prefetch(&app,false,now,&outcome),ESP_OK);
    ASSERT_GT(app.schedule.retry_at,now);
    const int64_t retry=app.schedule.retry_at;
    ASSERT_EQ(wind_app_prefetch(&app,false,retry,&outcome),ESP_OK);
    EXPECT_TRUE(outcome.attempted_fetch);
    EXPECT_EQ(outcome.fetch_result,ESP_ERR_TIMEOUT);
    EXPECT_EQ(app.schedule.retry_at,0);
    EXPECT_GT(wind_schedule_next_attempt(&app.schedule,retry),retry);
    EXPECT_EQ(fake.displays,0);EXPECT_EQ(fake.renders,0);
}

TEST_F(WindAppTest, SetupRejectsUnavailableEvenWhenThePanelRefreshSucceeds)
{
    fake.fetch_result = ESP_ERR_TIMEOUT;
    wind_app_outcome_t outcome;
    EXPECT_EQ(wind_app_run_setup(&app, 1787544000, &outcome), ESP_ERR_TIMEOUT);
    EXPECT_FALSE(outcome.displayed);
    EXPECT_EQ(fake.renders, 0);
    EXPECT_EQ(fake.displays, 0);
    EXPECT_EQ(outcome.freshness, WIND_FRESHNESS_UNAVAILABLE);
}


TEST_F(WindAppTest, SetupAfterFiveOfflineHoursFetchesImmediatelyAndCanRetry)
{
    const int64_t boot = 1787544000;
    fake.fetch_result = ESP_ERR_TIMEOUT;
    wind_app_outcome_t outcome;
    ASSERT_EQ(wind_app_run(&app, false, boot, &outcome), ESP_OK);
    ASSERT_EQ(wind_app_run(&app, false, boot + 300, &outcome), ESP_OK);
    EXPECT_EQ(wind_app_run_setup(&app, boot + 5 * 3600, &outcome), ESP_ERR_TIMEOUT);
    fake.fetch_result = ESP_OK;
    EXPECT_EQ(wind_app_run_setup(&app, boot + 5 * 3600 + 60, &outcome), ESP_OK);
    EXPECT_TRUE(outcome.attempted_fetch);
    EXPECT_TRUE(outcome.published_forecast);
    EXPECT_EQ(outcome.freshness, WIND_FRESHNESS_FRESH);
    EXPECT_TRUE(outcome.displayed);
}


TEST_F(WindAppTest, SetupRejectsFetchFailureEvenWithAnExistingForecast)
{
    wind_app_outcome_t outcome;
    ASSERT_EQ(wind_app_run_setup(&app, 1787544000, &outcome), ESP_OK);
    fake.fetch_result = ESP_ERR_TIMEOUT;
    EXPECT_EQ(wind_app_run_setup(&app, 1787544060, &outcome), ESP_ERR_TIMEOUT);
    EXPECT_TRUE(outcome.used_cache);
}


TEST_F(WindAppTest, SetupStillRequiresAConfirmedPanelAndAcceptsAnUnchangedForecast)
{
    wind_app_outcome_t outcome;
    fake.display_result = ESP_FAIL;
    EXPECT_EQ(wind_app_run_setup(&app, 1787544000, &outcome), ESP_FAIL);
    fake.display_result = ESP_OK;
    ASSERT_EQ(wind_app_run_setup(&app, 1787544000, &outcome), ESP_OK);
    ASSERT_TRUE(outcome.displayed);
    EXPECT_EQ(wind_app_run_setup(&app, 1787544000, &outcome), ESP_OK);
    EXPECT_TRUE(outcome.published_forecast);
    EXPECT_TRUE(outcome.display_unchanged);
}

TEST_F(WindAppTest, SetupDueScheduleWriteFailureDoesNotClaimAForecastRequest)
{
    int64_t boundary = 0;
    ASSERT_TRUE(wind_schedule_is_due(&app.schedule, 1787544000, &boundary));
    std::filesystem::remove_all(root);
    wind_app_outcome_t outcome{};
    EXPECT_NE(wind_app_run_setup(&app, 1787544000, &outcome), ESP_OK);
    EXPECT_EQ(fake.fetches, 0);
    EXPECT_FALSE(outcome.attempted_fetch);
    EXPECT_EQ(fake.displays, 0);
}

TEST_F(WindAppTest, SetupCacheWriteFailureAfterSatisfiedBoundaryRetainsFetchEvidence)
{
    const int64_t now = 1787544000;
    const int64_t boundary = wind_schedule_latest_boundary(fake.identity.timezone, now);
    wind_schedule_mark_attempted(&app.schedule, boundary);
    wind_schedule_mark_satisfied(&app.schedule, boundary);
    ASSERT_FALSE(wind_schedule_is_due(&app.schedule, now, nullptr));
    std::filesystem::remove_all(root);
    wind_app_outcome_t outcome{};
    EXPECT_NE(wind_app_run_setup(&app, now, &outcome), ESP_OK);
    EXPECT_EQ(fake.fetches, 1);
    EXPECT_TRUE(outcome.attempted_fetch);
    EXPECT_FALSE(outcome.published_forecast);
    EXPECT_EQ(fake.displays, 0);
}


TEST(WindAppDiagnostics, ExposesBootFailureIndependentlyOfInstallerApply)
{
    wind_app_status_begin();
    wind_app_status_stage(WIND_REFRESH_FORECAST);
    wind_app_status_t status;
    wind_app_status_get(&status);
    EXPECT_EQ(status.stage, WIND_REFRESH_FORECAST);
    wind_provider_diagnostics_t forecast = {};
    forecast.perform_result = ESP_FAIL;
    forecast.http_status = 503;
    // Normal refresh can return OK after drawing an unavailable screen.
    wind_app_status_finish(ESP_OK, ESP_FAIL, true, false, &forecast);
    wind_app_status_get(&status);
    EXPECT_EQ(status.stage, WIND_REFRESH_FAILED);
    EXPECT_EQ(status.result, ESP_OK);
    EXPECT_EQ(status.fetch_result, ESP_FAIL);
    EXPECT_EQ(status.forecast.http_status, 503);
    EXPECT_TRUE(status.attempted_fetch);
    // A later preparation failure must not inherit the previous HTTP result.
    wind_app_status_begin();
    wind_app_status_finish(ESP_ERR_NO_MEM, ESP_OK, false, false, &forecast);
    wind_app_status_get(&status);
    EXPECT_EQ(status.result, ESP_ERR_NO_MEM);
    EXPECT_FALSE(status.attempted_fetch);
    EXPECT_EQ(status.forecast.http_status, 0);
    wind_app_status_finish(ESP_OK, ESP_OK, true, true, nullptr);
    wind_app_status_get(&status);
    EXPECT_EQ(status.stage, WIND_REFRESH_COMPLETE);
}
