#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <unistd.h>

extern "C" {
#include "wind_app.h"
#include "wind_overview.h"
#include "wind_app_runtime.h"
#include "wind_quick_cache.h"
#include "wind_timezone.h"
#include "wind_display_config.h"
#include "wind_renderer.h"
#include "wind_config.h"
#include "wind_tide_cache.h"
#include "wind_swell_cache.h"
void runtime_test_reset(bool preserve_rtc);
}

namespace {
time_t clock_now = 1787544000;
bool online = true;
bool fail_display = false;
int battery = 75;
int displays = 0;
int renders = 0;
int quick_displays = 0;
wind_renderer_dashboard_t last_dashboard{};
std::string last_spot;
std::map<std::string, int> fetches;
std::string failed_spot;
std::vector<uint8_t> panel;
std::vector<uint8_t> pending_panel;
wind_display_config_t display_config{};
std::string device_timezone = "Europe/Amsterdam";
std::function<void()> during_render;
std::function<void()> during_display;
std::function<void()> during_fetch;
}

extern "C" {
const char *esp_err_to_name(esp_err_t) { return "simulated error"; }
int board_hal_get_battery_percent(void) { return battery; }
bool wifi_manager_is_connected(void) { return online; }
bool power_manager_is_installer_active(void) { return false; }
esp_err_t wind_analytics_maybe_send(time_t) { return ESP_OK; }
int64_t esp_timer_get_time(void) { return (int64_t)clock_now * 1000000; }
time_t runtime_test_time(time_t *out) { if (out) *out = clock_now; return clock_now; }
bool config_manager_set_timezone_transient(const char *value) { device_timezone = value; return true; }
const char *config_manager_get_timezone(void) { return device_timezone.c_str(); }
bool config_manager_set_wind_display_config_transient(const wind_display_config_t *value) {
    display_config = *value; return true;
}
bool config_manager_set_wind_display_config(const wind_display_config_t *value) {
    return config_manager_set_wind_display_config_transient(value);
}
wind_display_config_t config_manager_get_wind_display_config(void) { return display_config; }
esp_err_t display_manager_begin_rgb_stream(void) {
    pending_panel.assign(WIND_RENDERER_E1003_COMPOSITION_BYTES, 0xff);
    return ESP_OK;
}
esp_err_t display_manager_push_palette_row(int y, const uint8_t *row, int width) {
    std::copy(row, row + width, pending_panel.begin() + y * width); return ESP_OK;
}
esp_err_t display_manager_end_rgb_stream(bool show) {
    if (fail_display) return ESP_FAIL;
    if (show) { panel = pending_panel; ++displays; }
    if (during_display) {
        auto callback = std::move(during_display);
        during_display = nullptr;
        callback();
    }
    return ESP_OK;
}
esp_err_t epaper_display(const uint8_t *packed) {
    if (fail_display) return ESP_FAIL;
    panel.resize(WIND_RENDERER_E1003_COMPOSITION_BYTES);
    for (size_t i = 0; i < panel.size() / 2; ++i) {
        panel[i * 2] = packed[i] >> 4;
        panel[i * 2 + 1] = packed[i] & 15;
    }
    ++displays; ++quick_displays;
    return ESP_OK;
}
int runtime_test_render_for_display(const wind_renderer_dashboard_t *dashboard,
    wind_renderer_display_t display, uint8_t *bitmap, size_t size, wind_renderer_stats_t *stats) {
    ++renders;
    last_dashboard = *dashboard;
    last_spot = dashboard->spot_name;
    if (during_render) {
        auto callback = std::move(during_render);
        during_render = nullptr;
        callback();
    }
    return wind_renderer_render_for_display(dashboard, display, bitmap, size, stats);
}
bool open_meteo_knmi_config_valid(const open_meteo_knmi_config_t *) { return true; }
bool open_meteo_marine_config_valid(const open_meteo_marine_config_t *) { return true; }
void open_meteo_knmi_get_diagnostics(wind_provider_diagnostics_t *out) { if (out) *out = {}; }
static esp_err_t fetch_wind(void *context, int64_t now, wind_forecast_t *out) {
    const auto *config = static_cast<open_meteo_knmi_config_t *>(context);
    ++fetches[config->spot_id];
    if (!online || failed_spot == config->spot_id) return ESP_ERR_TIMEOUT;
    if (during_fetch) {
        auto callback = std::move(during_fetch);
        during_fetch = nullptr;
        callback();
    }
    wind_forecast_clear(out);
    snprintf(out->spot_id, sizeof(out->spot_id), "%s", config->spot_id);
    snprintf(out->spot_name, sizeof(out->spot_name), "%s", config->spot_name);
    snprintf(out->timezone, sizeof(out->timezone), "%s", config->timezone);
    snprintf(out->model, sizeof(out->model), "%s", config->model);
    strcpy(out->provider, "open-meteo");
    out->latitude = config->latitude; out->longitude = config->longitude;
    out->retrieved_at = now;
    wind_local_datetime_t date{};
    if (wind_timezone_from_unix(config->timezone, now, &date) != ESP_OK) return ESP_FAIL;
    for (int day = 0; day < 5; ++day) {
        wind_timezone_format_date(&date, out->days[day].local_date, sizeof(out->days[day].local_date));
        for (int hour = 8; hour <= 20; ++hour) {
            date.hour = hour; date.minute = date.second = 0;
            int64_t timestamp;
            wind_timezone_to_unix(config->timezone, &date, &timestamp);
            auto &sample = out->hourly[day][hour - 8];
            sample.timestamp = timestamp; sample.local_hour = hour;
            sample.wind_knots = 10 + day + hour % 5;
            sample.gust_knots = sample.wind_knots + 5;
            sample.destination_degrees = 90;
            if ((hour - 8) % 3 == 0) out->days[day].samples[(hour - 8) / 3] = sample;
        }
        wind_timezone_shift_date(&date, 1);
    }
    return wind_forecast_validate(out) ? ESP_OK : ESP_FAIL;
}
void open_meteo_knmi_provider_init(wind_provider_t *provider, open_meteo_knmi_config_t *config) {
    *provider = {fetch_wind, config};
}
static esp_err_t fetch_tide(void *context, int64_t now, wind_tide_t *out) {
    if (!online) return ESP_ERR_TIMEOUT;
    const auto *config = static_cast<open_meteo_marine_config_t *>(context);
    wind_tide_clear(out);
    strcpy(out->spot_id, config->spot_id); strcpy(out->timezone, config->timezone);
    strcpy(out->provider, "open-meteo"); out->retrieved_at = now;
    out->capability = WIND_TIDE_UNSUPPORTED;
    return ESP_OK;
}
void open_meteo_marine_provider_init(wind_tide_provider_t *provider, open_meteo_marine_config_t *config) {
    *provider = {fetch_tide, config};
}
esp_err_t wind_swell_fetch(const open_meteo_marine_config_t *config, int64_t now, wind_swell_t *out) {
    if (!online) return ESP_ERR_TIMEOUT;
    *out = {};
    strcpy(out->spot_id, config->spot_id); strcpy(out->timezone, config->timezone);
    strcpy(out->model, config->swell_model); out->retrieved_at = now;
    out->sample_count = WIND_SWELL_MAX_SAMPLES;
    const int64_t start = now - now % 86400;
    for (size_t i = 0; i < out->sample_count; ++i)
        out->samples[i] = {start + (int64_t)i * 3600, 150, 85, 90, 70, 60, 180};
    return wind_swell_validate(out) ? ESP_OK : ESP_FAIL;
}
}

class E1003Runtime : public testing::Test {
protected:
    installed_configuration_t config{};
    std::filesystem::path original, root;
    void SetUp() override {
        original = std::filesystem::current_path();
        root = std::filesystem::temp_directory_path() / ("windpeek-runtime-" + std::to_string(getpid()));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        std::filesystem::current_path(root);
        runtime_test_reset(false);
        installed_configuration_reset_host_storage();
        clock_now = 1787544000; online = true; fail_display = false; battery = 75;
        displays = renders = quick_displays = 0; fetches.clear(); failed_spot.clear(); panel.clear();
        during_render = nullptr;
        during_display = nullptr;
        during_fetch = nullptr;
        installed_configuration_default(&config);
        config.version = INSTALLED_CONFIGURATION_MULTI_VERSION;
        config.additional_spot_count = 9;
        config.display.show_tide = false;
        config.display.swell_size = 0;
        for (int i = 0; i < 9; ++i) {
            auto &extra = config.additional_spots[i];
            installed_configuration_get_spot(&config, 0, &extra);
            extra.version = INSTALLED_CONFIGURATION_VERSION;
            extra.spot = config.spot;
            snprintf(extra.spot.id, sizeof(extra.spot.id), "spot-%d", i + 2);
            snprintf(extra.spot.display_name, sizeof(extra.spot.display_name), "Spot %d", i + 2);
            strcpy(extra.forecast_model, config.forecast_model);
            extra.display = config.display;
        }
        ASSERT_TRUE(installed_configuration_validate(&config));
        ASSERT_EQ(installed_configuration_promote_setup(&config, "test", "test-only"), ESP_OK);
        ASSERT_EQ(wind_spots_reload_installed(), ESP_OK);
        ASSERT_EQ(wind_spots_store_selected(0), ESP_OK);
        ASSERT_EQ(wind_app_configure_runtime(), ESP_OK);
    }
    void TearDown() override {
        runtime_test_reset(false);
        std::filesystem::current_path(original);
        std::filesystem::remove_all(root);
    }
    void reboot(bool rtc = true) {
        runtime_test_reset(rtc);
        ASSERT_EQ(wind_spots_reload_installed(), ESP_OK);
        ASSERT_EQ(wind_app_configure_runtime(), ESP_OK);
    }
    void install() {
        ASSERT_EQ(wind_app_preview_configuration(&config, nullptr), ESP_OK);
        ASSERT_EQ(wind_app_activate_configuration(&config), ESP_OK);
        ASSERT_EQ(wind_app_start(), ESP_OK);
        ASSERT_EQ(wind_app_prefetch_other_spots(), ESP_OK);
    }
    void publish_stronger_forecast() {
        wind_forecast_t newer;
        const wind_cache_identity_t identity{"spot-2", "Europe/Amsterdam", "best_match"};
        ASSERT_EQ(wind_cache_load("./wind-spot-2.cache", &identity, &newer), ESP_OK);
        newer.retrieved_at = clock_now + 1;
        for (auto &day : newer.days) for (auto &sample : day.samples) {
            sample.wind_knots = 36;
            sample.gust_knots = 40;
        }
        for (auto &day : newer.hourly) for (auto &sample : day) {
            sample.wind_knots = 36;
            sample.gust_knots = 40;
        }
        ASSERT_EQ(wind_cache_store("./wind-spot-2.cache", &newer), ESP_OK);
    }
};

TEST_F(E1003Runtime, InstallationOverviewEverySpotAndEveryDayRemainAvailableOffline) {
    install();
    ASSERT_EQ(fetches.size(), 10u);
    online = false;
    for (size_t index = 0; index < 10; ++index) {
        SCOPED_TRACE(index);
        ASSERT_EQ(wind_app_select_spot(index), ESP_OK);
        EXPECT_EQ(last_spot, wind_spots_at(index)->display_name);
        EXPECT_EQ(last_dashboard.visible_day_count, 5);
        EXPECT_EQ(last_dashboard.state, WIND_RENDERER_FRESH);
        EXPECT_EQ(last_dashboard.battery_percent, 75);
        const auto overview_spot = panel;
        for (size_t day = 0; day < 5; ++day) {
            ASSERT_EQ(wind_app_toggle_day(day), ESP_OK);
            EXPECT_EQ(last_dashboard.visible_day_count, 1);
            EXPECT_EQ(last_dashboard.visible_sample_count, 13);
            EXPECT_TRUE(last_dashboard.days[0].samples[12].available);
            ASSERT_EQ(wind_app_toggle_day(day), ESP_OK);
            EXPECT_EQ(panel, overview_spot);
        }
        ASSERT_EQ(wind_app_show_overview(), ESP_OK);
        ASSERT_EQ(wind_app_select_spot(index), ESP_OK);
        EXPECT_EQ(panel, overview_spot);
    }
}

TEST_F(E1003Runtime, FailedSpotRecoversWithoutBlockingOtherSpotsAndSurvivesRestart) {
    ASSERT_EQ(wind_app_start(), ESP_OK);
    failed_spot = "spot-3";
    EXPECT_EQ(wind_app_prefetch_other_spots(), ESP_ERR_NOT_FOUND);
    EXPECT_EQ(fetches.size(), 10u);
    EXPECT_TRUE(wind_app_spot_requires_network(2));
    EXPECT_FALSE(wind_app_spot_requires_network(9));
    clock_now += 300;
    EXPECT_EQ(wind_app_prefetch_other_spots(), ESP_ERR_NOT_FOUND);
    reboot();
    failed_spot.clear(); clock_now += 300;
    ASSERT_EQ(wind_app_prefetch_other_spots(), ESP_OK);
    EXPECT_FALSE(wind_app_spot_requires_network(2));
    ASSERT_EQ(wind_app_select_spot(2), ESP_OK);
    EXPECT_EQ(last_dashboard.state, WIND_RENDERER_FRESH);
}

TEST_F(E1003Runtime, SleepWakeRestoresSelectedSpotAndOverviewPage) {
    install();
    ASSERT_EQ(wind_app_select_spot(9), ESP_OK);
    ASSERT_EQ(wind_app_show_overview(), ESP_OK);
    const auto before_sleep = panel;
    online = false;
    reboot();
    ASSERT_EQ(wind_app_start(), ESP_OK);
    bool open = false; size_t page = 0;
    wind_app_overview_state(&open, &page);
    EXPECT_TRUE(open); EXPECT_EQ(page, 3u);
    EXPECT_EQ(panel, before_sleep);
    ASSERT_EQ(wind_app_select_previous(), ESP_OK);
    size_t selected = 0;
    ASSERT_EQ(wind_spots_load_selected(&selected), ESP_OK);
    EXPECT_EQ(selected, 8u);
}

TEST_F(E1003Runtime, DisplayFailureDoesNotCommitNavigationOrPoisonQuickFrame) {
    install();
    const auto previous = panel;
    fail_display = true;
    EXPECT_NE(wind_app_select_spot(4), ESP_OK);
    size_t selected = 99;
    ASSERT_EQ(wind_spots_load_selected(&selected), ESP_OK);
    EXPECT_EQ(selected, 0u); EXPECT_EQ(panel, previous);
    fail_display = false;
    ASSERT_EQ(wind_app_select_spot(4), ESP_OK);
    EXPECT_EQ(last_spot, "Spot 5");
    EXPECT_EQ(last_dashboard.state, WIND_RENDERER_FRESH);
}

TEST_F(E1003Runtime, DayRolloverRequestsFreshDataAndOfflineCacheRemainsUsable) {
    install();
    clock_now += 86400; online = false;
    EXPECT_TRUE(wind_app_spot_requires_network(1));
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    // Re-render, bypassing any prepared frame, to exercise age/failure handling.
    ASSERT_EQ(wind_app_refresh(true), ESP_OK);
    EXPECT_EQ(last_dashboard.state, WIND_RENDERER_STALE);
    EXPECT_TRUE(last_dashboard.refresh_failed);
    online = true;
    ASSERT_EQ(wind_app_refresh(true), ESP_OK);
    EXPECT_EQ(last_dashboard.state, WIND_RENDERER_FRESH);
    EXPECT_FALSE(last_dashboard.refresh_failed);
}

TEST_F(E1003Runtime, CachedScreenDoesNotFreezeBatteryOrForecastAge) {
    install();
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    const auto charged = panel;
    ASSERT_EQ(wind_app_show_overview(), ESP_OK);
    battery = 5;
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    EXPECT_EQ(last_dashboard.battery_percent, 5);
    EXPECT_NE(panel, charged);
    ASSERT_EQ(wind_app_show_overview(), ESP_OK);
    clock_now += 86400;
    online = false;
    const int previous_renders = renders;
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    EXPECT_GT(renders, previous_renders);
    EXPECT_EQ(last_dashboard.state, WIND_RENDERER_STALE);
}

TEST_F(E1003Runtime, AllModuleSizesAndOptionalRowsRenderThroughProductionBuilder) {
    config.additional_spot_count = 0;
    config.version = INSTALLED_CONFIGURATION_VERSION;
    for (int wind = 0; wind <= 2; ++wind) {
        for (int swell = 0; swell <= 2; ++swell) {
            for (unsigned rows = 0; rows < 8; ++rows) {
                SCOPED_TRACE(std::to_string(wind) + "/" + std::to_string(swell) + "/" + std::to_string(rows));
                config.display.wind_size = wind;
                config.display.swell_size = swell;
                config.display.show_weather = rows & 1;
                config.display.show_temperature = rows & 2;
                config.display.show_tide = rows & 4;
                ASSERT_EQ(installed_configuration_promote_setup(&config, "test", "test-only"), ESP_OK);
                ASSERT_EQ(wind_app_activate_configuration(&config), ESP_OK);
                ASSERT_EQ(wind_app_start(), ESP_OK);
                EXPECT_NE(last_dashboard.state, WIND_RENDERER_UNAVAILABLE);
                EXPECT_EQ(last_dashboard.battery_percent, 75);
                ASSERT_EQ(wind_app_toggle_day(2), ESP_OK);
                EXPECT_EQ(last_dashboard.visible_sample_count, 13);
                EXPECT_NE(last_dashboard.state, WIND_RENDERER_UNAVAILABLE);
            }
        }
    }
}

TEST_F(E1003Runtime, OfflineMidnightKeepsTodayAlignedBetweenOverviewAndDetail) {
    install();
    clock_now += 86400;
    online = false;
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    // Yesterday's second day is today's first day: 10 + day(1) + hour(8)%5.
    EXPECT_EQ(last_dashboard.days[0].samples[0].sustained_kt, 14);
    ASSERT_EQ(wind_app_toggle_day(0), ESP_OK);
    EXPECT_EQ(last_dashboard.visible_day_count, 1);
    EXPECT_EQ(last_dashboard.days[0].samples[0].sustained_kt, 14);
    ASSERT_EQ(wind_app_toggle_day(0), ESP_OK);
    ASSERT_EQ(wind_app_show_overview(), ESP_OK);
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    EXPECT_EQ(last_dashboard.days[0].samples[0].sustained_kt, 14);
}

TEST_F(E1003Runtime, CorruptQuickFrameFallsBackToForecastWithoutWifi) {
    install();
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    const auto expected = panel;
    bool corrupted = false;
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        const auto name = entry.path().filename().string();
        if (name.find("wind-spot-2.cache.quick.") == 0) {
            std::filesystem::resize_file(entry.path(), 10);
            corrupted = true;
        }
    }
    ASSERT_TRUE(corrupted);
    online = false;
    ASSERT_EQ(wind_app_show_overview(), ESP_OK);
    const int previous_renders = renders;
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    EXPECT_GT(renders, previous_renders);
    EXPECT_EQ(panel, expected);
}

TEST_F(E1003Runtime, ReinstallInvalidatesOldScreensAndPreservesPerSpotPreferences) {
    install();
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    const auto old_frame = panel;
    strcpy(config.additional_spots[0].spot.display_name, "Renamed spot");
    config.additional_spots[0].display.wind_size = 1;
    config.additional_spots[0].display.show_temperature = true;
    config.additional_spots[0].display.use_24_hour = false;
    ASSERT_EQ(wind_app_preview_configuration(&config, nullptr), ESP_OK);
    ASSERT_EQ(installed_configuration_promote_setup(&config, "test", "test-only"), ESP_OK);
    ASSERT_EQ(wind_app_activate_configuration(&config), ESP_OK);
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    EXPECT_EQ(last_spot, "Renamed spot");
    EXPECT_EQ(last_dashboard.wind_size, 1);
    EXPECT_TRUE(last_dashboard.show_temperature);
    EXPECT_FALSE(last_dashboard.use_24_hour);
    EXPECT_NE(panel, old_frame);
}

TEST_F(E1003Runtime, FailedInstallKeepsPreviousConfigurationUsable) {
    install();
    const auto digest = installed_configuration_digest(&config);
    auto candidate = config;
    strcpy(candidate.spot.id, "replacement");
    online = false;
    EXPECT_NE(wind_app_preview_configuration(&candidate, nullptr), ESP_OK);
    installed_configuration_t installed;
    ASSERT_EQ(installed_configuration_load(&installed), ESP_OK);
    EXPECT_EQ(installed_configuration_digest(&installed), digest);
    ASSERT_EQ(wind_app_select_spot(0), ESP_OK);
    EXPECT_EQ(last_dashboard.state, WIND_RENDERER_FRESH);
}

TEST_F(E1003Runtime, SwellAgeInvalidatesQuickFrameEvenWhenWindIsStillFresh) {
    config.display.wind_size = 0;
    config.display.swell_size = 2;
    config.display.show_weather = false;
    ASSERT_EQ(installed_configuration_promote_setup(&config, "test", "test-only"), ESP_OK);
    ASSERT_EQ(wind_app_activate_configuration(&config), ESP_OK);
    open_meteo_marine_config_t marine{config.spot.id, config.spot.latitude,
        config.spot.longitude, config.spot.timezone, config.display.swell_model};
    wind_swell_t swell;
    ASSERT_EQ(wind_swell_fetch(&marine, clock_now - 6 * 3600 + 60, &swell), ESP_OK);
    ASSERT_EQ(wind_swell_cache_store(WIND_FORECAST_CACHE_PATH ".swell", &swell), ESP_OK);
    ASSERT_EQ(wind_app_start(), ESP_OK);
    EXPECT_EQ(last_dashboard.state, WIND_RENDERER_FRESH);
    ASSERT_EQ(wind_app_show_overview(), ESP_OK);
    online = false;
    clock_now += 120;
    const int previous_renders = renders;
    ASSERT_EQ(wind_app_select_spot(0), ESP_OK);
    EXPECT_GT(renders, previous_renders);
    EXPECT_EQ(last_dashboard.state, WIND_RENDERER_AGED);
}

TEST_F(E1003Runtime, ForecastPublishedDuringRenderingCannotLabelAnOldFrameAsNew) {
    install();
    during_render = [this] { publish_stronger_forecast(); };
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    const auto previous = panel;
    ASSERT_EQ(wind_app_show_overview(), ESP_OK);
    const int previous_renders = renders;
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    EXPECT_GT(renders, previous_renders);
    EXPECT_EQ(last_dashboard.days[0].samples[0].sustained_kt, 36);
    EXPECT_NE(panel, previous);
}

TEST_F(E1003Runtime, ForecastPublishedDuringOverviewDisplayInvalidatesItsPreparedImage) {
    install();
    during_display = [this] { publish_stronger_forecast(); };
    ASSERT_EQ(wind_app_show_overview(), ESP_OK);
    const auto previous = panel;
    const int previous_quick = quick_displays;
    ASSERT_EQ(wind_app_show_overview(), ESP_OK);
    EXPECT_EQ(quick_displays, previous_quick);
    EXPECT_NE(panel, previous);
}

TEST_F(E1003Runtime, NavigationDuringBackgroundFetchKeepsTheNewSelection) {
    ASSERT_EQ(wind_app_start(), ESP_OK);
    int navigated_displays = 0;
    during_fetch = [&] {
        ASSERT_EQ(wind_app_select_spot(4), ESP_OK);
        navigated_displays = displays;
    };
    ASSERT_EQ(wind_app_prefetch_other_spots(), ESP_OK);
    EXPECT_EQ(fetches.size(), 10u);
    size_t selected;
    ASSERT_EQ(wind_spots_load_selected(&selected), ESP_OK);
    EXPECT_EQ(selected, 4u);
    EXPECT_EQ(displays, navigated_displays);
    EXPECT_EQ(last_spot, "Spot 5");
}

TEST_F(E1003Runtime, TruncatedQuickFrameIsRepairedForTheNextVisit) {
    install();
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    const auto expected = panel;
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        if (entry.path().filename().string().find("wind-spot-2.cache.quick.") == 0)
            std::filesystem::resize_file(entry.path(), std::filesystem::file_size(entry.path()) - 8);
    }
    online = false;
    ASSERT_EQ(wind_app_show_overview(), ESP_OK);
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    EXPECT_EQ(panel, expected);
    ASSERT_EQ(wind_app_show_overview(), ESP_OK);
    const int previous_quick = quick_displays;
    ASSERT_EQ(wind_app_select_spot(1), ESP_OK);
    EXPECT_GT(quick_displays, previous_quick);
    EXPECT_EQ(panel, expected);
}

TEST_F(E1003Runtime, PreparedFifthDaySurvivesOfflineNavigationAndReboot) {
    install();
    for (size_t spot : {0u, 4u, 9u}) {
        for (int variant = 0; variant <= 5; ++variant)
            ASSERT_EQ(wind_app_prepare_quick_frame(spot, variant), ESP_OK);
        ASSERT_EQ(wind_app_select_spot(spot), ESP_OK);
        const auto five_days = panel;
        const int before_focus = quick_displays;
        ASSERT_EQ(wind_app_toggle_day(4), ESP_OK);
        EXPECT_GT(quick_displays, before_focus);
        const auto fifth_day = panel;
        EXPECT_NE(fifth_day, five_days);
        online = false;
        reboot();
        ASSERT_EQ(wind_app_start(), ESP_OK);
        EXPECT_EQ(panel, fifth_day);
        ASSERT_EQ(wind_app_toggle_day(4), ESP_OK);
        EXPECT_EQ(panel, five_days);
    }
    const auto unchanged = panel;
    EXPECT_EQ(wind_app_toggle_day(5), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(wind_app_prepare_quick_frame(0, 6), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(panel, unchanged);
}

TEST_F(E1003Runtime, FailedDayTransitionKeepsFocusConsistentWithThePanel) {
    install();
    const auto five_days = panel;
    fail_display = true;
    EXPECT_NE(wind_app_toggle_day(4), ESP_OK);
    EXPECT_EQ(panel, five_days);
    fail_display = false;
    ASSERT_EQ(wind_app_toggle_day(4), ESP_OK);
    const auto fifth_day = panel;
    EXPECT_NE(fifth_day, five_days);
    fail_display = true;
    EXPECT_NE(wind_app_toggle_day(4), ESP_OK);
    EXPECT_EQ(panel, fifth_day);
    fail_display = false;
    ASSERT_EQ(wind_app_toggle_day(4), ESP_OK);
    EXPECT_EQ(panel, five_days);
}

TEST_F(E1003Runtime, OverviewDisplayFailureKeepsThePreviousScreenAndSelection) {
    install();
    ASSERT_EQ(wind_app_select_spot(9), ESP_OK);
    const auto previous = panel;
    fail_display = true;
    EXPECT_NE(wind_app_show_overview(), ESP_OK);
    EXPECT_EQ(panel, previous);
    bool open = true; size_t page = 0;
    wind_app_overview_state(&open, &page);
    EXPECT_FALSE(open);
    size_t selected = 0;
    ASSERT_EQ(wind_spots_load_selected(&selected), ESP_OK);
    EXPECT_EQ(selected, 9u);
    fail_display = false;
    ASSERT_EQ(wind_app_show_overview(), ESP_OK);
    wind_app_overview_state(&open, &page);
    EXPECT_TRUE(open);
    EXPECT_EQ(page, 3u);
}

TEST_F(E1003Runtime, RepeatedOfflineNavigationPreservesEveryPreparedScreen) {
    install();
    std::vector<uint64_t> expected;
    for (size_t spot = 0; spot < 10; ++spot) {
        ASSERT_EQ(wind_app_select_spot(spot), ESP_OK);
        expected.push_back(wind_cache_bitmap_hash(panel.data(), panel.size()));
        ASSERT_EQ(wind_app_toggle_day(4), ESP_OK);
        expected.push_back(wind_cache_bitmap_hash(panel.data(), panel.size()));
        ASSERT_EQ(wind_app_toggle_day(4), ESP_OK);
    }
    online = false;
    const auto initial_fetches = fetches;
    for (size_t step = 0; step < 100; ++step) {
        const size_t spot = (step * 7) % 10;
        SCOPED_TRACE(step);
        ASSERT_EQ(wind_app_show_overview(), ESP_OK);
        ASSERT_EQ(wind_app_select_spot(spot), ESP_OK);
        EXPECT_EQ(wind_cache_bitmap_hash(panel.data(), panel.size()), expected[spot * 2]);
        ASSERT_EQ(wind_app_toggle_day(4), ESP_OK);
        EXPECT_EQ(wind_cache_bitmap_hash(panel.data(), panel.size()), expected[spot * 2 + 1]);
        ASSERT_EQ(wind_app_toggle_day(4), ESP_OK);
        size_t selected = 99;
        ASSERT_EQ(wind_spots_load_selected(&selected), ESP_OK);
        EXPECT_EQ(selected, spot);
    }
    EXPECT_EQ(fetches, initial_fetches);
}

TEST_F(E1003Runtime, TouchSeesDisplayedOverviewDuringBackgroundFetch) {
    install();
    ASSERT_EQ(wind_app_show_overview(), ESP_OK);
    ASSERT_EQ(wind_app_overview_page(1), ESP_OK);
    bool observed = false;
    during_fetch = [&] {
        observed = true;
        // The input task may still remember the detail view from before the
        // navigation task opened overview. A refresh must not hide new state.
        bool open = false;
        size_t page = 0;
        ASSERT_TRUE(wind_app_overview_state_if_ready(&open, &page));
        EXPECT_TRUE(open);
        EXPECT_EQ(page, 1u);
        const auto action = wind_overview_hit_test(100, WIND_OVERVIEW_TOP + 20,
                                                  open, page, 10);
        EXPECT_EQ(action.kind, WIND_TOUCH_SELECT);
        EXPECT_EQ(action.spot_index, 3u);
    };
    ASSERT_EQ(wind_app_refresh(true), ESP_OK);
    EXPECT_TRUE(observed);
    ASSERT_EQ(wind_app_select_spot(3), ESP_OK);
    observed = false;
    during_fetch = [&] {
        observed = true;
        bool open = true;
        size_t page = 99;
        ASSERT_TRUE(wind_app_overview_state_if_ready(&open, &page));
        EXPECT_FALSE(open);
        EXPECT_EQ(wind_overview_hit_test(100, 40, open, page, 10).kind,
                  WIND_TOUCH_OPEN);
    };
    ASSERT_EQ(wind_app_refresh(true), ESP_OK);
    EXPECT_TRUE(observed);
    fail_display = true;
    EXPECT_NE(wind_app_show_overview(), ESP_OK);
    bool open = true;
    size_t page = 99;
    ASSERT_TRUE(wind_app_overview_state_if_ready(&open, &page));
    EXPECT_FALSE(open);
}
