#include "wind_app.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wind_timezone.h"

static const int64_t FAILED_REFRESH_RETRY_SECONDS = 5 * 60;

static wind_freshness_t freshness_for(const wind_forecast_t *forecast, int64_t now) {
    if (!forecast) {
        return WIND_FRESHNESS_UNAVAILABLE;
    }
    int64_t age = now - forecast->retrieved_at;
    if (age < -5 * 60) {
        return WIND_FRESHNESS_UNAVAILABLE;
    }
    if (age <= 0 || age < 6 * 60 * 60) {
        return WIND_FRESHNESS_FRESH;
    }
    return age >= 24 * 60 * 60 ? WIND_FRESHNESS_STALE : WIND_FRESHNESS_AGED;
}

static bool forecast_covers_dashboard_window(const wind_forecast_t *forecast,
                                             const char *timezone, int64_t now) {
    if (!forecast || !timezone || now <= 0 || !wind_forecast_validate(forecast)) {
        return false;
    }
    wind_local_datetime_t local;
    char today[WIND_FORECAST_DATE_LENGTH];
    if (wind_timezone_from_unix(timezone, now, &local) != ESP_OK ||
        wind_timezone_format_date(&local, today, sizeof(today)) != ESP_OK) {
        return false;
    }
    return strcmp(forecast->days[0].local_date, today) == 0;
}

esp_err_t wind_app_init(wind_app_t *app, const wind_app_config_t *config) {
    if (!app || !config || !config->provider.fetch || !config->identity.spot_id ||
        !config->identity.timezone || !config->identity.model ||
        !config->forecast_cache_path || !config->panel_cache_path ||
        !config->schedule_path || !config->render || !config->display ||
        config->render_signature == 0 || config->bitmap_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(app, 0, sizeof(*app));
    app->config = *config;
    if (wind_schedule_state_load_scoped(config->schedule_path, config->identity.spot_id,
                                        config->identity.timezone,
                                        &app->schedule) != ESP_OK) {
        if (wind_schedule_state_set_scope(&app->schedule, config->identity.spot_id,
                                          config->identity.timezone) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    app->initialized = true;
    return ESP_OK;
}

static esp_err_t run_internal(wind_app_t *app, bool force_refresh, bool allow_fetch,
                              bool publish_display, int64_t now,
                              wind_app_outcome_t *outcome) {
    if (!app || !app->initialized || now <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    wind_app_outcome_t local = {.fetch_result = ESP_OK};
    wind_forecast_t active;
    bool have_active = wind_cache_load(app->config.forecast_cache_path,
                                       &app->config.identity, &active) == ESP_OK;
    local.used_cache = have_active;

    bool cache_has_coverage =
        have_active &&
        forecast_covers_dashboard_window(&active, app->config.identity.timezone, now);
    bool coverage_refresh_due =
        have_active && !cache_has_coverage &&
        (!app->coverage_refresh_attempted ||
         app->coverage_refresh_cache_retrieved_at != active.retrieved_at);
    int64_t boundary = 0;
    bool due = wind_schedule_is_due(&app->schedule, (time_t)now, &boundary);
    int64_t retry_boundary = 0;
    bool pending_retry_due =
        wind_schedule_retry_is_due(&app->schedule, (time_t)now, &retry_boundary);
    // A regular boundary supersedes a missed retry. Treat this as a fresh
    // scheduled attempt so a transient failure still earns its own one retry.
    bool retry_due = pending_retry_due && !due;
    bool initial_fetch_due = !have_active && app->schedule.last_attempted_boundary == 0;
    if (allow_fetch && (force_refresh || due || coverage_refresh_due || retry_due ||
                        initial_fetch_due)) {
        local.attempted_fetch = true;
        if (coverage_refresh_due) {
            app->coverage_refresh_attempted = true;
            app->coverage_refresh_cache_retrieved_at = active.retrieved_at;
        }
        if (due) {
            wind_schedule_mark_attempted(&app->schedule, boundary);
            if (pending_retry_due) {
                wind_schedule_consume_retry(&app->schedule);
            }
            if (wind_schedule_state_store(app->config.schedule_path, &app->schedule) !=
                ESP_OK) {
                if (outcome) {
                    *outcome = local;
                }
                return ESP_FAIL;
            }
        }
        if (retry_due) {
            boundary = retry_boundary;
            wind_schedule_consume_retry(&app->schedule);
            if (wind_schedule_state_store(app->config.schedule_path, &app->schedule) !=
                ESP_OK) {
                if (outcome) {
                    *outcome = local;
                }
                return ESP_FAIL;
            }
        }
        wind_forecast_t fetched;
        wind_forecast_clear(&fetched);
        local.fetch_result = wind_provider_fetch(&app->config.provider, now, &fetched);
        if (local.fetch_result == ESP_OK && wind_forecast_validate(&fetched) &&
            forecast_covers_dashboard_window(&fetched, app->config.identity.timezone,
                                             now)) {
            local.fetch_result =
                wind_cache_store(app->config.forecast_cache_path, &fetched);
            if (local.fetch_result == ESP_OK) {
                active = fetched;
                have_active = true;
                local.used_cache = false;
                local.published_forecast = true;
                app->coverage_refresh_attempted = false;
                app->coverage_refresh_cache_retrieved_at = 0;
                wind_schedule_consume_retry(&app->schedule);
                wind_schedule_mark_satisfied(&app->schedule, boundary);
                wind_schedule_state_store(app->config.schedule_path, &app->schedule);
            }
        } else if (local.fetch_result == ESP_OK) {
            local.fetch_result = ESP_ERR_INVALID_RESPONSE;
        }
        const bool automatic_attempt = due || coverage_refresh_due || initial_fetch_due;
        if (local.fetch_result != ESP_OK && automatic_attempt && !retry_due) {
            wind_schedule_schedule_retry(&app->schedule, boundary,
                                         now + FAILED_REFRESH_RETRY_SECONDS);
            wind_schedule_state_store(app->config.schedule_path, &app->schedule);
        }
    }

    local.freshness = freshness_for(have_active ? &active : NULL, now);
    if (!publish_display) {
        if (outcome) {
            *outcome = local;
        }
        return ESP_OK;
    }
    uint8_t *bitmap = (uint8_t *)malloc(app->config.bitmap_size);
    if (!bitmap) {
        return ESP_ERR_NO_MEM;
    }
    const bool refresh_failed = local.attempted_fetch && local.fetch_result != ESP_OK;
    esp_err_t result = app->config.render(
        app->config.io_context, have_active ? &active : NULL, local.freshness,
        refresh_failed, now, bitmap, app->config.bitmap_size);
    if (result != ESP_OK) {
        free(bitmap);
        if (outcome) {
            *outcome = local;
        }
        return result;
    }

    uint64_t hash = wind_cache_bitmap_hash(bitmap, app->config.bitmap_size);
    uint64_t confirmed_hash = 0;
    if (hash != 0 &&
        wind_cache_panel_load(app->config.panel_cache_path,
                              app->config.render_signature,
                              &confirmed_hash) == ESP_OK &&
        hash == confirmed_hash) {
        local.display_unchanged = true;
        free(bitmap);
        if (outcome) {
            *outcome = local;
        }
        return ESP_OK;
    }

    result =
        app->config.display(app->config.io_context, bitmap, app->config.bitmap_size);
    free(bitmap);
    if (result != ESP_OK) {
        wind_cache_panel_invalidate(app->config.panel_cache_path);
        if (outcome) {
            *outcome = local;
        }
        return result;
    }
    result = wind_cache_panel_confirm(app->config.panel_cache_path,
                                      app->config.render_signature, hash);
    if (result == ESP_OK) {
        local.displayed = true;
    }
    if (outcome) {
        *outcome = local;
    }
    return result;
}

esp_err_t wind_app_run(wind_app_t *app, bool force_refresh, int64_t now,
                       wind_app_outcome_t *outcome) {
    return run_internal(app, force_refresh, true, true, now, outcome);
}

esp_err_t wind_app_prefetch(wind_app_t *app, bool force_refresh, int64_t now,
                            wind_app_outcome_t *outcome) {
    return run_internal(app, force_refresh, true, false, now, outcome);
}

esp_err_t wind_app_show_cached(wind_app_t *app, int64_t now,
                               wind_app_outcome_t *outcome) {
    return run_internal(app, false, false, true, now, outcome);
}

#ifdef ESP_PLATFORM
#include "board_hal.h"
#include "config.h"
#include "config_manager.h"
#include "display_manager.h"
#include "epaper.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "installed_configuration.h"
#include "open_meteo_knmi_provider.h"
#include "open_meteo_marine_provider.h"
#include "wind_config.h"
#include "wind_analytics.h"
#include "wind_renderer.h"
#include "wind_spots.h"
#include "wind_tide_cache.h"

// Bump this whenever layout, typography, palette encoding, or final bitmap semantics
// change.
#define WIND_DASHBOARD_RENDER_SIGNATURE UINT64_C(0x57494E440000000F)

static const char *TAG = "wind_app";
typedef struct {
    wind_app_t app;
    open_meteo_knmi_config_t provider_config;
    open_meteo_marine_config_t marine_config;
    wind_tide_provider_t tide_provider;
    wind_tide_t tide;
    bool have_tide;
    const wind_spot_t *spot;
    const char *device_timezone;
    char forecast_path[96];
    char schedule_path[96];
    char tide_path[96];
} wind_spot_runtime_t;

static wind_spot_runtime_t s_spots[1];
static installed_configuration_t s_installed_configuration;
static size_t s_selected_index;
static SemaphoreHandle_t s_app_lock;
// Serializes runtime reconfiguration with scheduled refreshes and button
// actions. The installer temporarily swaps the active spot/display settings;
// no other task may observe that preview state.
static SemaphoreHandle_t s_runtime_lock;
static bool s_ready;
static bool s_last_render_succeeded;

static esp_err_t wind_app_refresh_unlocked(bool force_refresh, bool *published_forecast);

static wind_renderer_display_t active_renderer_display(void) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E100X
    if (epaper_active_hardware() == EPAPER_HARDWARE_E1001) {
        return WIND_RENDERER_DISPLAY_E1001_GRAY4;
    }
#elif defined(CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003)
    return WIND_RENDERER_DISPLAY_E1003_GC16;
#endif
    return WIND_RENDERER_DISPLAY_E1002_SPECTRA6;
}

static size_t active_renderer_bitmap_size(void) {
    return active_renderer_display() == WIND_RENDERER_DISPLAY_E1003_GC16
               ? WIND_RENDERER_E1003_COMPOSITION_BYTES
               : WIND_RENDERER_PALETTE_BYTES;
}

static uint64_t current_render_signature(void) {
    const wind_display_config_t config = config_manager_get_wind_display_config();
    return wind_renderer_display_signature(WIND_DASHBOARD_RENDER_SIGNATURE ^
                                               wind_display_config_signature(&config),
                                           active_renderer_display());
}

static void refresh_render_signatures(void) {
    const uint64_t signature = current_render_signature();
    for (size_t index = 0; index < wind_spots_count(); ++index) {
        s_spots[index].app.config.render_signature = signature;
    }
}

#define WIND_TIDE_REFRESH_INTERVAL_SECONDS (6 * 60 * 60)

static void load_or_refresh_tide(wind_spot_runtime_t *runtime, bool force_refresh,
                                 int64_t now) {
    const wind_display_config_t display = config_manager_get_wind_display_config();
    runtime->have_tide = false;
    wind_tide_clear(&runtime->tide);
    if (!display.show_tide) {
        return;
    }

    const wind_tide_cache_identity_t identity = {
        .spot_id = runtime->spot->id,
        .timezone = runtime->spot->timezone,
    };
    if (wind_tide_cache_load(runtime->tide_path, &identity, &runtime->tide) == ESP_OK) {
        runtime->have_tide = true;
    }

    const bool tide_is_fresh =
        runtime->have_tide && runtime->tide.retrieved_at <= now &&
        now - runtime->tide.retrieved_at < WIND_TIDE_REFRESH_INTERVAL_SECONDS;
    if (tide_is_fresh && !force_refresh) {
        return;
    }
    if (!runtime->tide_provider.fetch) {
        ESP_LOGW(TAG, "Tide provider is not configured for %s", runtime->spot->id);
        return;
    }
    wind_tide_t *fetched = malloc(sizeof(*fetched));
    if (!fetched) {
        ESP_LOGW(TAG, "No memory available to refresh tide for %s", runtime->spot->id);
        return;
    }
    wind_tide_clear(fetched);
    const esp_err_t result =
        runtime->tide_provider.fetch(runtime->tide_provider.context, now, fetched);
    if (result == ESP_OK && wind_tide_validate(fetched)) {
        runtime->tide = *fetched;
        runtime->have_tide = true;
        if (wind_tide_cache_store(runtime->tide_path, fetched) != ESP_OK) {
            ESP_LOGW(TAG, "Could not persist tide data for %s", runtime->spot->id);
        }
    } else if (runtime->have_tide) {
        ESP_LOGW(TAG, "Tide refresh failed for %s; using cached data",
                 runtime->spot->id);
    } else {
        ESP_LOGW(TAG, "Tide unavailable for %s", runtime->spot->id);
    }
    free(fetched);
}

static const char *day_name(int weekday) {
    static const char *names[] = {"SUNDAY",   "MONDAY", "TUESDAY", "WEDNESDAY",
                                  "THURSDAY", "FRIDAY", "SATURDAY"};
    return weekday >= 0 && weekday < 7 ? names[weekday] : "";
}

static const char *month_name(unsigned month) {
    static const char *names[] = {"",    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return month <= 12 ? names[month] : "";
}

static wind_renderer_state_t renderer_state(wind_freshness_t freshness) {
    switch (freshness) {
        case WIND_FRESHNESS_FRESH:
            return WIND_RENDERER_FRESH;
        case WIND_FRESHNESS_AGED:
            return WIND_RENDERER_AGED;
        case WIND_FRESHNESS_STALE:
            return WIND_RENDERER_STALE;
        default:
            return WIND_RENDERER_UNAVAILABLE;
    }
}

static bool preview_pixel_is_dark(wind_renderer_display_t display, uint8_t pixel) {
    if (display == WIND_RENDERER_DISPLAY_E1003_GC16) return pixel < 8;
    if (display == WIND_RENDERER_DISPLAY_E1001_GRAY4) return pixel < 2;
    return pixel == 0;
}

static esp_err_t write_dashboard_preview(const uint8_t *bitmap, size_t bitmap_size,
                                         wind_renderer_display_t display) {
    if (!bitmap || bitmap_size != active_renderer_bitmap_size()) {
        return ESP_ERR_INVALID_SIZE;
    }
    int width = 0;
    int height = 0;
    if (wind_renderer_display_dimensions(display, &width, &height) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *temporary_path = WIND_DASHBOARD_PREVIEW_PATH ".tmp";
    FILE *file = fopen(temporary_path, "wb");
    if (!file) {
        return ESP_FAIL;
    }
    if (fprintf(file, "P4\n%d %d\n", width, height) < 0) {
        fclose(file);
        return ESP_FAIL;
    }
    uint8_t *logical_row = malloc((size_t)width);
    uint8_t *packed_row = malloc(((size_t)width + 7u) / 8u);
    if (!logical_row || !packed_row) {
        free(logical_row);
        free(packed_row);
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    const size_t packed_size = ((size_t)width + 7u) / 8u;
    esp_err_t result = ESP_OK;
    for (int y = 0; y < height && result == ESP_OK; ++y) {
        if (wind_renderer_project_display_row(display, bitmap, bitmap_size, y,
                                              logical_row, (size_t)width) != 0) {
            result = ESP_FAIL;
            break;
        }
        memset(packed_row, 0, packed_size);
        for (int x = 0; x < width; ++x) {
            if (preview_pixel_is_dark(display, logical_row[x])) {
                packed_row[x / 8] |= (uint8_t)(0x80u >> (x % 8));
            }
        }
        if (fwrite(packed_row, 1, packed_size, file) != packed_size) {
            result = ESP_FAIL;
        }
    }
    free(logical_row);
    free(packed_row);
    if (fclose(file) != 0) {
        result = ESP_FAIL;
    }
    if (result == ESP_OK && rename(temporary_path, WIND_DASHBOARD_PREVIEW_PATH) != 0) {
        result = ESP_FAIL;
    }
    return result;
}

static esp_err_t render_dashboard(void *context, const wind_forecast_t *forecast,
                                  wind_freshness_t freshness, bool refresh_failed,
                                  int64_t now, uint8_t *bitmap, size_t bitmap_size) {
    const wind_spot_runtime_t *runtime = (const wind_spot_runtime_t *)context;
    const wind_spot_t *spot = runtime->spot;
    const wind_display_config_t display = config_manager_get_wind_display_config();
    wind_renderer_dashboard_t dashboard = {0};
    char updated[32] = "";
    char dates[WIND_RENDERER_DAY_COUNT][16] = {{0}};
    char times[WIND_RENDERER_DAY_COUNT][WIND_RENDERER_SAMPLES_PER_DAY][8] = {{{0}}};
    wind_local_datetime_t local = {0};

    dashboard.spot_name = forecast ? forecast->spot_name : spot->display_name;
    dashboard.provider =
        wind_forecast_model_screen_name(forecast ? forecast->model : WIND_MODEL);
    dashboard.updated_time = updated;
    dashboard.state = renderer_state(freshness);
    dashboard.refresh_failed = refresh_failed ? 1 : 0;
    dashboard.age_hours = forecast && now > forecast->retrieved_at
                              ? (int)((now - forecast->retrieved_at) / 3600)
                              : 0;
    dashboard.battery_percent = board_hal_get_battery_percent();
    dashboard.display_mode = (wind_renderer_display_mode_t)display.display_mode;
    dashboard.threshold_kt = display.threshold_kt;
    dashboard.show_weather = display.show_weather;
    dashboard.show_temperature = display.show_temperature;
    dashboard.show_tide = display.show_tide;
    dashboard.show_dedicated_footer = display.show_dedicated_footer;
    dashboard.use_24_hour = display.use_24_hour;
    dashboard.temperature_fahrenheit = display.temperature_fahrenheit;
    if (forecast) {
        char update_date[16] = "";
        if (wind_timezone_from_unix(runtime->device_timezone, forecast->retrieved_at, &local) !=
            ESP_OK) {
            return ESP_ERR_INVALID_STATE;
        }
        snprintf(update_date, sizeof(update_date), "%02u %s", local.day,
                 month_name(local.month));
        if (display.use_24_hour) {
            snprintf(updated, sizeof(updated), "%s %02d:%02d", update_date, local.hour,
                     local.minute);
        } else {
            const int hour_12 = local.hour % 12 == 0 ? 12 : local.hour % 12;
            snprintf(updated, sizeof(updated), "%s %d%s", update_date, hour_12,
                     local.hour < 12 ? "AM" : "PM");
        }
        for (char *cursor = updated; *cursor; ++cursor) {
            *cursor = (char)toupper((unsigned char)*cursor);
        }
        for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
            const wind_forecast_day_t *source_day = &forecast->days[day];
            if (wind_timezone_from_unix(spot->timezone,
                                        source_day->samples[0].timestamp,
                                        &local) != ESP_OK) {
                return ESP_ERR_INVALID_STATE;
            }
            dashboard.days[day].day =
                day == 0 ? "TODAY" : day_name(wind_timezone_weekday(&local));
            snprintf(dates[day], sizeof(dates[day]), "%02u %s", local.day,
                     month_name(local.month));
            dashboard.days[day].date = dates[day];
            for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
                const wind_forecast_sample_t *source = &source_day->samples[sample];
                const unsigned hour = source->local_hour <= 23 ? source->local_hour : 0;
                if (display.use_24_hour) {
                    snprintf(times[day][sample], sizeof(times[day][sample]), "%02u",
                             hour);
                } else {
                    const unsigned hour_12 = hour % 12 == 0 ? 12 : hour % 12;
                    snprintf(times[day][sample], sizeof(times[day][sample]), "%u%s",
                             hour_12, hour < 12 ? "AM" : "PM");
                }
                dashboard.days[day].samples[sample] = (wind_renderer_sample_t){
                    .time = times[day][sample],
                    .sustained_kt = source->wind_knots,
                    .gust_kt = source->gust_knots,
                    .destination_degrees = source->destination_degrees,
                    .available = source->timestamp > 0,
                    .weather =
                        (wind_renderer_weather_t)wind_forecast_weather_state(source),
                    .temperature_tenths_c = source->temperature_tenths_c,
                    .temperature_available = source->temperature_available,
                };
            }
        }

        if (display.show_tide && runtime->have_tide &&
            runtime->tide.capability == WIND_TIDE_AVAILABLE) {
            dashboard.tide_available = 1;
            for (size_t tide_index = 0;
                 tide_index < runtime->tide.sample_count &&
                 dashboard.tide_sample_count < WIND_RENDERER_MAX_TIDE_SAMPLES;
                 ++tide_index) {
                const wind_tide_sample_t *source = &runtime->tide.samples[tide_index];
                for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
                    if (strcmp(source->local_date, forecast->days[day].local_date) !=
                        0) {
                        continue;
                    }
                    dashboard.tide_samples[dashboard.tide_sample_count++] =
                        (wind_renderer_tide_sample_t){
                            .day_index = day,
                            .local_hour = source->local_hour,
                            .sea_level_mm = source->sea_level_mm,
                            .available = 1,
                        };
                    break;
                }
            }
            for (size_t extremum_index = 0;
                 extremum_index < runtime->tide.extremum_count &&
                 dashboard.tide_extremum_count < WIND_RENDERER_MAX_TIDE_EXTREMA;
                 ++extremum_index) {
                const wind_tide_extremum_t *source =
                    &runtime->tide.extrema[extremum_index];
                for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
                    if (strcmp(source->local_date, forecast->days[day].local_date) != 0)
                        continue;
                    dashboard.tide_extrema[dashboard.tide_extremum_count++] =
                        (wind_renderer_tide_extremum_t){
                            .day_index = day,
                            .local_hour = source->local_hour,
                            .local_minute = source->local_minute,
                            .sea_level_mm = source->sea_level_mm,
                            .is_high = source->is_high,
                            .available = 1,
                        };
                    break;
                }
            }
        }
    }

    wind_renderer_stats_t stats;
    int render_result = wind_renderer_render_for_display(
        &dashboard, active_renderer_display(), bitmap, bitmap_size, &stats);
    if (render_result != 0) {
        ESP_LOGE(TAG, "Dashboard render failed: %d", render_result);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t display_dashboard(void *context, const uint8_t *bitmap,
                                   size_t bitmap_size) {
    (void)context;
    const wind_renderer_display_t display = active_renderer_display();
    if (!bitmap || bitmap_size != active_renderer_bitmap_size()) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (write_dashboard_preview(bitmap, bitmap_size, display) != ESP_OK) {
        ESP_LOGW(TAG, "Could not publish dashboard preview");
    }
    int width = 0;
    int height = 0;
    if (wind_renderer_display_dimensions(display, &width, &height) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t *row = malloc((size_t)width);
    if (!row) return ESP_ERR_NO_MEM;
    esp_err_t result = display_manager_begin_rgb_stream();
    if (result != ESP_OK) {
        free(row);
        return result;
    }
    for (int y = 0; y < height && result == ESP_OK; ++y) {
        if (wind_renderer_project_display_row(display, bitmap, bitmap_size, y, row,
                                              (size_t)width) != 0) {
            result = ESP_FAIL;
            break;
        }
        result = display_manager_push_palette_row(y, row, width);
    }
    esp_err_t end_result = display_manager_end_rgb_stream(result == ESP_OK, NULL);
    free(row);
    return result != ESP_OK ? result : end_result;
}

static esp_err_t ensure_ready(void) {
    if (s_ready) {
        return ESP_OK;
    }
    if (wind_spots_count() != sizeof(s_spots) / sizeof(s_spots[0])) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!s_app_lock) {
        s_app_lock = xSemaphoreCreateMutex();
        if (!s_app_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (installed_configuration_load(&s_installed_configuration) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (wind_spots_load_selected(&s_selected_index) != ESP_OK ||
        !wind_spots_at(s_selected_index)) {
        s_selected_index = 0;
    }
    for (size_t index = 0; index < wind_spots_count(); ++index) {
        wind_spot_runtime_t *runtime = &s_spots[index];
        runtime->spot = wind_spots_at(index);
        runtime->device_timezone = wind_spots_device_timezone();
        int forecast_length =
            index == 0
                ? snprintf(runtime->forecast_path, sizeof(runtime->forecast_path), "%s",
                           WIND_FORECAST_CACHE_PATH)
                : snprintf(runtime->forecast_path, sizeof(runtime->forecast_path),
                           "/storage/wind-%s.cache", runtime->spot->id);
        int schedule_length =
            index == 0
                ? snprintf(runtime->schedule_path, sizeof(runtime->schedule_path), "%s",
                           WIND_SCHEDULE_CACHE_PATH)
                : snprintf(runtime->schedule_path, sizeof(runtime->schedule_path),
                           "/storage/wind-%s.schedule", runtime->spot->id);
        int tide_length = index == 0
                              ? snprintf(runtime->tide_path, sizeof(runtime->tide_path),
                                         "%s", WIND_TIDE_CACHE_PATH)
                              : snprintf(runtime->tide_path, sizeof(runtime->tide_path),
                                         "/storage/wind-%s.tide", runtime->spot->id);
        if (forecast_length < 0 ||
            forecast_length >= (int)sizeof(runtime->forecast_path) ||
            schedule_length < 0 ||
            schedule_length >= (int)sizeof(runtime->schedule_path) || tide_length < 0 ||
            tide_length >= (int)sizeof(runtime->tide_path)) {
            return ESP_ERR_INVALID_SIZE;
        }
        runtime->provider_config = (open_meteo_knmi_config_t){
            .spot_id = runtime->spot->id,
            .spot_name = runtime->spot->display_name,
            .latitude = runtime->spot->latitude,
            .longitude = runtime->spot->longitude,
            .timezone = runtime->spot->timezone,
            .model = s_installed_configuration.forecast_model,
        };
        if (!open_meteo_knmi_config_valid(&runtime->provider_config)) {
            ESP_LOGE(TAG, "Provider configuration rejected for %s", runtime->spot->id);
            return ESP_ERR_INVALID_STATE;
        }
        runtime->marine_config = (open_meteo_marine_config_t){
            .spot_id = runtime->spot->id,
            .latitude = runtime->spot->latitude,
            .longitude = runtime->spot->longitude,
            .timezone = runtime->spot->timezone,
        };
        if (!open_meteo_marine_config_valid(&runtime->marine_config)) {
            // Tide is an optional row. A missing licensed marine endpoint must
            // not take the core wind forecast offline; the renderer will show
            // tide as unavailable if the user enables it.
            memset(&runtime->tide_provider, 0, sizeof(runtime->tide_provider));
            ESP_LOGW(TAG, "Marine provider unavailable for %s", runtime->spot->id);
        } else {
            open_meteo_marine_provider_init(&runtime->tide_provider,
                                            &runtime->marine_config);
        }
        wind_provider_t provider;
        open_meteo_knmi_provider_init(&provider, &runtime->provider_config);
        wind_app_config_t config = {
            .provider = provider,
            .identity = {.spot_id = runtime->spot->id,
                         .timezone = runtime->spot->timezone,
                         .model = s_installed_configuration.forecast_model},
            .forecast_cache_path = runtime->forecast_path,
            .panel_cache_path = WIND_PANEL_CACHE_PATH,
            .schedule_path = runtime->schedule_path,
            .render_signature = current_render_signature(),
            .bitmap_size = active_renderer_bitmap_size(),
            .render = render_dashboard,
            .display = display_dashboard,
            .io_context = runtime,
        };
        esp_err_t result = wind_app_init(&runtime->app, &config);
        if (result != ESP_OK) {
            return result;
        }
    }
    s_ready = true;
    return ESP_OK;
}

esp_err_t wind_app_configure_runtime(void) {
    if (!s_runtime_lock) {
        s_runtime_lock = xSemaphoreCreateMutex();
        if (!s_runtime_lock) return ESP_ERR_NO_MEM;
    }
    installed_configuration_t installed;
    if (installed_configuration_load(&installed) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!config_manager_set_timezone_transient(installed.spot.timezone)) {
        return ESP_ERR_INVALID_ARG;
    }
#ifndef CONFIG_BOARD_CAP_WINDSCOUT
    // Other photo-frame boards still use the legacy cron-backed power manager.
    static const char *rules[] = {"5 0 *", "0 7 *", "0 11 *", "0 15 *", "0 19 *"};
    config_manager_set_cron_rules(rules, 5);
    config_manager_set_auto_rotate(true);
#endif
    return ESP_OK;
}

static wind_display_config_t
display_from_installed(const installed_configuration_t *installed) {
    wind_display_config_t display;
    wind_display_config_default(&display);
    display.display_mode = installed->display.show_threshold
                               ? WIND_RENDERER_MODE_THRESHOLD
                               : WIND_RENDERER_MODE_SOLID;
    display.threshold_kt = installed->display.threshold_kt;
    display.show_weather = installed->display.show_weather;
    display.show_temperature = installed->display.show_temperature;
    display.show_tide = installed->display.show_tide;
    display.show_dedicated_footer = installed->display.show_dedicated_footer;
    display.use_24_hour = installed->display.use_24_hour;
    display.temperature_fahrenheit = installed->display.temperature_fahrenheit;
    return display;
}

esp_err_t wind_app_preview_configuration(const installed_configuration_t *candidate) {
    if (!installed_configuration_validate(candidate)) return ESP_ERR_INVALID_ARG;
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    installed_configuration_t active;
    if (installed_configuration_load(&active) != ESP_OK) {
        xSemaphoreGive(s_runtime_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const wind_display_config_t old_display = config_manager_get_wind_display_config();
    const wind_display_config_t preview_display = display_from_installed(candidate);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (config_manager_set_wind_display_config_transient(&preview_display) &&
        config_manager_set_timezone_transient(candidate->spot.timezone) &&
        wind_spots_use_configuration(candidate) == ESP_OK) {
        s_ready = false;
        result = wind_app_refresh_unlocked(true, NULL);
    }
    (void)wind_spots_use_configuration(&active);
    (void)config_manager_set_timezone_transient(active.spot.timezone);
    (void)config_manager_set_wind_display_config_transient(&old_display);
    s_ready = false;
    xSemaphoreGive(s_runtime_lock);
    return result;
}

esp_err_t
wind_app_activate_configuration(const installed_configuration_t *configuration) {
    if (!installed_configuration_validate(configuration)) return ESP_ERR_INVALID_ARG;
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    const wind_display_config_t display = display_from_installed(configuration);
    if (!config_manager_set_wind_display_config(&display) ||
        wind_spots_use_configuration(configuration) != ESP_OK) {
        xSemaphoreGive(s_runtime_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (!config_manager_set_timezone_transient(configuration->spot.timezone)) {
        xSemaphoreGive(s_runtime_lock);
        return ESP_ERR_INVALID_ARG;
    }
    s_ready = false;
    xSemaphoreGive(s_runtime_lock);
    return ESP_OK;
}

static esp_err_t wind_app_refresh_unlocked(bool force_refresh, bool *published_forecast) {
    if (published_forecast) {
        *published_forecast = false;
    }
    esp_err_t result = ensure_ready();
    if (result != ESP_OK) {
        return result;
    }
    xSemaphoreTake(s_app_lock, portMAX_DELAY);
    time_t now;
    time(&now);
    refresh_render_signatures();
    load_or_refresh_tide(&s_spots[s_selected_index], force_refresh, now);
    bool any_forecast_published = false;
    for (size_t index = 0; index < wind_spots_count(); ++index) {
        wind_app_outcome_t outcome;
        esp_err_t spot_result =
            index == s_selected_index
                ? wind_app_run(&s_spots[index].app, force_refresh, now, &outcome)
                : wind_app_prefetch(&s_spots[index].app, force_refresh, now, &outcome);
        ESP_LOGI(TAG, "%s: fetch=%d published=%d displayed=%d unchanged=%d",
                 s_spots[index].spot->id, outcome.attempted_fetch,
                 outcome.published_forecast, outcome.displayed,
                 outcome.display_unchanged);
        any_forecast_published = any_forecast_published || outcome.published_forecast;
        if (index == s_selected_index || result == ESP_OK) {
            result = spot_result;
        }
        if (index == s_selected_index) {
            s_last_render_succeeded = spot_result == ESP_OK &&
                                      (outcome.displayed || outcome.display_unchanged);
        }
    }
    if (published_forecast) {
        *published_forecast = any_forecast_published;
    }
    xSemaphoreGive(s_app_lock);
    return result;
}

esp_err_t wind_app_refresh(bool force_refresh) {
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    bool published_forecast = false;
    esp_err_t result = wind_app_refresh_unlocked(force_refresh, &published_forecast);
    xSemaphoreGive(s_runtime_lock);
    if (published_forecast) {
        time_t now;
        time(&now);
        const esp_err_t analytics_result = wind_analytics_maybe_send(now);
        if (analytics_result != ESP_OK) {
            ESP_LOGW(TAG, "Dashboard activity heartbeat failed: %s",
                     esp_err_to_name(analytics_result));
        }
    }
    return result;
}

static esp_err_t navigate(int direction) {
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ensure_ready();
    if (result != ESP_OK) {
        xSemaphoreGive(s_runtime_lock);
        return result;
    }
    xSemaphoreTake(s_app_lock, portMAX_DELAY);
    const size_t target = wind_spots_offset(s_selected_index, direction);
    wind_spot_runtime_t *runtime = &s_spots[target];
    wind_forecast_t cached;
    const bool have_cache =
        wind_cache_load(runtime->forecast_path, &runtime->app.config.identity,
                        &cached) == ESP_OK;
    time_t now;
    time(&now);
    refresh_render_signatures();
    load_or_refresh_tide(runtime, false, now);
    wind_app_outcome_t outcome;
    result = have_cache ? wind_app_show_cached(&runtime->app, now, &outcome)
                        : wind_app_run(&runtime->app, true, now, &outcome);
    if (result == ESP_OK) {
        s_selected_index = target;
        esp_err_t store_result = wind_spots_store_selected(target);
        if (store_result != ESP_OK) {
            ESP_LOGW(TAG, "Could not persist selected spot: %s",
                     esp_err_to_name(store_result));
        }
        ESP_LOGI(TAG, "Selected spot %s (cached=%d)", runtime->spot->id, have_cache);
    }
    xSemaphoreGive(s_app_lock);
    xSemaphoreGive(s_runtime_lock);
    return result;
}

esp_err_t wind_app_select_previous(void) {
    return navigate(-1);
}
esp_err_t wind_app_select_next(void) {
    return navigate(1);
}
esp_err_t wind_app_select_next_display_mode(void) {
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ready = ensure_ready();
    if (ready != ESP_OK) {
        xSemaphoreGive(s_runtime_lock);
        return ready;
    }
    if (xSemaphoreTake(s_app_lock, portMAX_DELAY) != pdTRUE) {
        xSemaphoreGive(s_runtime_lock);
        return ESP_ERR_TIMEOUT;
    }

    wind_display_config_t display = config_manager_get_wind_display_config();
    display.display_mode =
        (uint8_t)((display.display_mode + 1) % WIND_RENDERER_MODE_COUNT);
    if (!config_manager_set_wind_display_config(&display)) {
        xSemaphoreGive(s_app_lock);
        xSemaphoreGive(s_runtime_lock);
        return ESP_FAIL;
    }
    refresh_render_signatures();
    wind_cache_panel_invalidate(WIND_PANEL_CACHE_PATH);
    ESP_LOGI(TAG, "Selected display mode %d", (int)display.display_mode);

    time_t now;
    time(&now);
    wind_app_outcome_t outcome = {0};
    esp_err_t result =
        wind_app_show_cached(&s_spots[s_selected_index].app, now, &outcome);
    xSemaphoreGive(s_app_lock);
    xSemaphoreGive(s_runtime_lock);
    return result;
}

bool wind_app_navigation_requires_network(int direction) {
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE) {
        return true;
    }
    if (ensure_ready() != ESP_OK) {
        xSemaphoreGive(s_runtime_lock);
        return true;
    }
    const size_t target = wind_spots_offset(s_selected_index, direction);
    wind_forecast_t cached;
    const bool requires_network =
        wind_cache_load(s_spots[target].forecast_path,
                        &s_spots[target].app.config.identity, &cached) != ESP_OK;
    xSemaphoreGive(s_runtime_lock);
    return requires_network;
}

esp_err_t wind_app_start(void) {
    return wind_app_refresh(false);
}

esp_err_t wind_app_clear_panel_confirmation(void) {
    // A splash or another out-of-band display write replaces the forecast
    // even when the panel cache still describes the previously rendered frame.
    // Keep installer verification tied to what is actually visible.
    s_last_render_succeeded = false;
    return wind_cache_panel_invalidate(WIND_PANEL_CACHE_PATH);
}

int wind_app_seconds_until_next_wake(void) {
    const bool locked =
        s_runtime_lock && xSemaphoreTake(s_runtime_lock, portMAX_DELAY) == pdTRUE;
    time_t now;
    time(&now);
    int64_t next = wind_schedule_next_boundary(config_manager_get_timezone(), now);
    if (s_ready && s_selected_index < wind_spots_count()) {
        next = wind_schedule_next_attempt(&s_spots[s_selected_index].app.schedule, now);
    }
    if (locked) xSemaphoreGive(s_runtime_lock);
    return next > now ? (int)(next - now) : 1;
}

bool wind_app_last_render_succeeded(void) {
    return s_last_render_succeeded;
}
#else
esp_err_t wind_app_configure_runtime(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t wind_app_start(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t wind_app_refresh(bool force_refresh) {
    (void)force_refresh;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t wind_app_select_previous(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t wind_app_select_next(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t wind_app_select_next_display_mode(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
bool wind_app_navigation_requires_network(int direction) {
    (void)direction;
    return true;
}
esp_err_t wind_app_clear_panel_confirmation(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
int wind_app_seconds_until_next_wake(void) {
    return 0;
}
bool wind_app_last_render_succeeded(void) {
    return false;
}
esp_err_t wind_app_preview_configuration(const installed_configuration_t *candidate) {
    (void)candidate;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t
wind_app_activate_configuration(const installed_configuration_t *configuration) {
    (void)configuration;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
