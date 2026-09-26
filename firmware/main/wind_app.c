#include "wind_app.h"
#include "wind_app_internal.h"
#include "wind_app_status.h"
#include "wind_dashboard_data.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "wind_timezone.h"

#ifdef ESP_PLATFORM
#include "wind_app_runtime.h"
#include "wind_quick_cache.h"
#include "wind_app_prefetch.h"
#include <stdatomic.h>
#include "board_hal.h"
#include "config.h"
#include "config_manager.h"
#include "display_manager.h"
#include "power_manager.h"
#include "epaper.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "installed_configuration.h"
#include "open_meteo_knmi_provider.h"
#include "open_meteo_marine_provider.h"
#include "wind_config.h"
#include "wind_analytics.h"
#include "wind_renderer.h"
#include "wind_renderer_internal.h"
#include "wind_overview.h"
#include "wifi_manager.h"
#include "esp_attr.h"
#include "wind_spots.h"
#include "wind_tide_cache.h"
#include "wind_swell_cache.h"

// Bump this whenever layout, typography, palette encoding, or final bitmap semantics
// change.
#define WIND_DASHBOARD_RENDER_SIGNATURE UINT64_C(0x57494E4400000018)

static const char *TAG = "wind_app";
static wind_spot_runtime_t *s_spots;
static installed_configuration_t s_installed_configuration;
static const installed_configuration_t *s_preview_configuration;
static size_t s_selected_index;
static SemaphoreHandle_t s_app_lock;
// Serializes runtime reconfiguration with scheduled refreshes and button
// actions. The installer temporarily swaps the active spot/display settings;
// no other task may observe that preview state.
static SemaphoreHandle_t s_runtime_lock;
static bool s_ready;
// One retained, atomic value is shared by navigation and touch hit testing.
// Background fetches may hold the runtime mutex without hiding the shown view.
enum { VIEW_UNINITIALIZED, VIEW_DETAIL, VIEW_OVERVIEW_FIRST };
RTC_DATA_ATTR static atomic_uint s_displayed_view;

static atomic_bool s_last_render_succeeded;
static bool s_force_next_display;
RTC_DATA_ATTR static char s_focused_date[WIND_FORECAST_DATE_LENGTH];
RTC_DATA_ATTR static uint64_t s_overview_configuration;
typedef enum {
    OVERVIEW_INTERACTIVE,
    OVERVIEW_REFRESH,
    OVERVIEW_PREPARE,
} overview_render_mode_t;
static esp_err_t render_overview_unlocked(size_t page,
    overview_render_mode_t mode, bool force);

static bool overview_open(void) {
    return atomic_load(&s_displayed_view) >= VIEW_OVERVIEW_FIRST;
}

static size_t overview_page(void) {
    const unsigned view = atomic_load(&s_displayed_view);
    return view >= VIEW_OVERVIEW_FIRST ? view - VIEW_OVERVIEW_FIRST : 0;
}

static void set_displayed_view(bool overview, size_t page) {
    if (!s_preview_configuration)
        atomic_store(&s_displayed_view, overview ? VIEW_OVERVIEW_FIRST + (unsigned)page : VIEW_DETAIL);
}

static esp_err_t wind_app_refresh_unlocked(bool force_refresh, wind_app_outcome_t *outcome);
static void apply_spot_display(size_t index);
static esp_err_t clear_panel_confirmation_unlocked(void);
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
static esp_err_t prefetch_spots(bool all, bool force);
static SemaphoreHandle_t s_fetch_lock;
#endif

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
        s_spots[index].app.config.render_signature = signature ^
            (s_spots[index].have_swell ? (uint64_t)s_spots[index].swell.retrieved_at : 0) ^
            ((uint64_t)s_spots[index].swell_failed << 63);
    }
}

#define WIND_TIDE_REFRESH_INTERVAL_SECONDS (6 * 60 * 60)

static void load_or_refresh_tide(wind_spot_runtime_t *runtime, bool force_refresh,
                                 bool allow_fetch, int64_t now) {
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
    // Cached navigation must leave every download to the shared refresh round.
    if (!allow_fetch ||
        (tide_is_fresh && !force_refresh)) {
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

static void load_or_refresh_swell(wind_spot_runtime_t *runtime, bool force,
                                  bool allow_fetch, int64_t now) {
    const wind_display_config_t display = config_manager_get_wind_display_config();
    runtime->have_swell = false;
    runtime->swell_failed = false;
    if (!display.swell_size) return;
    char path[128];
    snprintf(path, sizeof(path), "%s.swell", runtime->forecast_path);
    const wind_swell_cache_identity_t identity = { runtime->spot->id, runtime->spot->timezone, runtime->marine_config.swell_model };
    runtime->have_swell = wind_swell_cache_load(path, &identity, &runtime->swell) == ESP_OK;
    if (!allow_fetch) return;
    if (runtime->have_swell && runtime->swell.retrieved_at <= now &&
        now - runtime->swell.retrieved_at < 6 * 3600 && !force) return;
    wind_swell_t *fresh = malloc(sizeof(*fresh));
    if (!fresh) { runtime->swell_failed = true; return; }
    if (wind_swell_fetch(&runtime->marine_config, now, fresh) == ESP_OK && wind_swell_validate(fresh)) {
        runtime->swell = *fresh;
        runtime->have_swell = true;
        if (wind_swell_cache_store(path, fresh) != ESP_OK) ESP_LOGW(TAG, "Could not cache swell");
    } else runtime->swell_failed = true;
    free(fresh);
}

static esp_err_t render_dashboard_with_workspace(void *context, const wind_forecast_t *forecast,
                                  wind_freshness_t freshness, bool refresh_failed,
                                  int64_t now, uint8_t *bitmap, size_t bitmap_size,
                                  wind_renderer_dashboard_t *dashboard, wind_forecast_t *calendar) {
    const wind_spot_runtime_t *runtime = (const wind_spot_runtime_t *)context;
    const wind_spot_t *spot = runtime->spot;
    const wind_display_config_t display = config_manager_get_wind_display_config();
    char updated[32] = "";
    char dates[WIND_RENDERER_DAY_COUNT][16] = {{0}};
    char times[WIND_RENDERER_DAY_COUNT][WIND_RENDERER_MAX_SAMPLES_PER_DAY][8] = {{{0}}};
    wind_local_datetime_t local = {0};

    const wind_forecast_t *hourly_forecast = forecast;
    const bool weather_required = display.wind_size || display.show_weather || display.show_temperature;
    const bool weather_missing = !forecast;
    bool calendar_used = false;
    bool calendar_weather[WIND_FORECAST_DAY_COUNT][WIND_FORECAST_SAMPLES_PER_DAY] = {{false}};
    const bool show_swell = display.swell_size && runtime->have_swell;
    if (show_swell || (forecast &&
        !wind_app_forecast_covers_window(forecast, spot->timezone, now))) {
        calendar_used = true;
        const esp_err_t calendar_result = wind_dashboard_build_calendar(
            forecast, spot->timezone, now, calendar, calendar_weather);
        if (calendar_result != ESP_OK) return calendar_result;
        snprintf(calendar->spot_name, sizeof(calendar->spot_name), "%s", spot->display_name);
        if (show_swell)
            calendar->retrieved_at = weather_required && forecast && forecast->retrieved_at < runtime->swell.retrieved_at
                ? forecast->retrieved_at : runtime->swell.retrieved_at;
        forecast = calendar;
    }
    dashboard->spot_name = spot->display_name;
    dashboard->provider =
        wind_forecast_model_screen_name(forecast ? forecast->model : WIND_MODEL);
    dashboard->updated_time = updated;
    dashboard->state = display.swell_size && runtime->have_swell ? renderer_state(wind_app_forecast_freshness(forecast, now)) : renderer_state(freshness);
    dashboard->refresh_failed = (weather_required && refresh_failed) || (display.swell_size && runtime->swell_failed);
    if (display.swell_size) dashboard->provider = "OPEN-METEO";
    dashboard->age_hours = forecast && now > forecast->retrieved_at
                              ? (int)((now - forecast->retrieved_at) / 3600)
                              : 0;
    dashboard->battery_percent = board_hal_get_battery_percent();
    dashboard->display_mode = (wind_renderer_display_mode_t)display.display_mode;
    dashboard->custom_modules = 1;
    dashboard->ordered_modules = 1;
    dashboard->wind_size = display.wind_size;
    dashboard->swell_size = display.swell_size;
    for (int i = 0; i < 5; ++i) dashboard->module_order[i] = display.module_order[i];
    for (int day = 0; day < 5; ++day) {
        for (int hour = 0; hour < 24; ++hour) {
            dashboard->swell_hourly[day][hour] = -1;
            dashboard->secondary_swell_hourly[day][hour] = -1;
        }
        for (int i = 0; i < 5; ++i) dashboard->swell[day][i] = (wind_renderer_swell_sample_t){-1,-1,-1};
    }
    if (display.wind_size != 2) dashboard->display_mode = WIND_RENDERER_MODE_SOLID;
    dashboard->threshold_kt = display.threshold_kt;
    dashboard->show_weather = display.show_weather;
    dashboard->show_temperature = display.show_temperature;
    dashboard->show_tide = display.show_tide;
    dashboard->show_dedicated_footer = display.show_dedicated_footer;
    dashboard->use_24_hour = display.use_24_hour;
    dashboard->temperature_fahrenheit = display.temperature_fahrenheit;
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
            dashboard->days[day].day =
                day == 0 ? "TODAY" : wind_dashboard_day_name(wind_timezone_weekday(&local));
            snprintf(dates[day], sizeof(dates[day]), "%02u %s", local.day,
                     month_name(local.month));
            dashboard->days[day].date = dates[day];
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
                dashboard->days[day].samples[sample] = wind_dashboard_forecast_sample(
                    source, times[day][sample], !weather_missing && source->timestamp > 0 &&
                    (!calendar_used || calendar_weather[day][sample]));
            }
        }

        if (display.show_tide && runtime->have_tide &&
            runtime->tide.capability == WIND_TIDE_AVAILABLE) {
            for (size_t tide_index = 0;
                 tide_index < runtime->tide.sample_count &&
                 dashboard->tide_sample_count < WIND_RENDERER_MAX_TIDE_SAMPLES;
                 ++tide_index) {
                const wind_tide_sample_t *source = &runtime->tide.samples[tide_index];
                for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
                    if (strcmp(source->local_date, forecast->days[day].local_date) !=
                        0) {
                        continue;
                    }
                    dashboard->tide_samples[dashboard->tide_sample_count++] =
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
                 dashboard->tide_extremum_count < WIND_RENDERER_MAX_TIDE_EXTREMA;
                 ++extremum_index) {
                const wind_tide_extremum_t *source =
                    &runtime->tide.extrema[extremum_index];
                for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
                    if (strcmp(source->local_date, forecast->days[day].local_date) != 0)
                        continue;
                    dashboard->tide_extrema[dashboard->tide_extremum_count++] =
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
            dashboard->tide_available = dashboard->tide_sample_count >= 2;
        }
    }

    if (display.swell_size && runtime->have_swell) {
        for (size_t i = 0; i < runtime->swell.sample_count; ++i) {
            const wind_swell_sample_t *source = &runtime->swell.samples[i];
            wind_local_datetime_t date;
            char local_date[11];
            if (wind_timezone_from_unix(spot->timezone, source->timestamp, &date) != ESP_OK) continue;
            wind_timezone_format_date(&date, local_date, sizeof(local_date));
            for (int day = 0; day < 5; ++day) {
                if (!forecast || strcmp(local_date, forecast->days[day].local_date)) continue;
                dashboard->swell_hourly[day][date.hour] = source->height_cm;
                dashboard->secondary_swell_hourly[day][date.hour] = source->secondary_height_cm;
                for (int slot = 0; slot < 5; ++slot) {
                    if (forecast->days[day].samples[slot].local_hour == date.hour) {
                        dashboard->swell[day][slot] = wind_dashboard_swell_sample(source, false);
                        dashboard->secondary_swell[day][slot] = wind_dashboard_swell_sample(source, true);
                    }
                }
            }
        }
    }
    if (active_renderer_display() == WIND_RENDERER_DISPLAY_E1003_GC16 && !s_preview_configuration) {
        dashboard->visible_day_count = WIND_RENDERER_DAY_COUNT;
        int focused = -1;
        if (forecast && s_focused_date[0]) {
            for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day)
                if (!strcmp(s_focused_date, forecast->days[day].local_date)) focused = day;
        }
        if (focused < 0 && forecast) s_focused_date[0] = 0;
        if (focused >= 0) {
            dashboard->visible_day_count = 1;
            dashboard->visible_sample_count = WIND_RENDERER_MAX_SAMPLES_PER_DAY;
            dashboard->days[0] = dashboard->days[focused];
            memmove(dashboard->swell_hourly[0], dashboard->swell_hourly[focused], sizeof(dashboard->swell_hourly[0]));
            memmove(dashboard->secondary_swell_hourly[0], dashboard->secondary_swell_hourly[focused], sizeof(dashboard->secondary_swell_hourly[0]));
            for (int i = 0; i < WIND_RENDERER_MAX_SAMPLES_PER_DAY; ++i) {
                const int hour = 8 + i;
                const wind_forecast_sample_t *source = NULL;
                if (hourly_forecast) for (int day = 0; day < WIND_FORECAST_DAY_COUNT; ++day) {
                    if (strcmp(forecast->days[focused].local_date, hourly_forecast->days[day].local_date)) continue;
                    if (hourly_forecast->hourly[day][i].timestamp) source = &hourly_forecast->hourly[day][i];
                    /* A migrated cache still has its five original observations. */
                    if (!source) for (int j = 0; j < WIND_FORECAST_SAMPLES_PER_DAY; ++j)
                        if (hourly_forecast->days[day].samples[j].local_hour == hour)
                            source = &hourly_forecast->days[day].samples[j];
                }
                if (display.use_24_hour) snprintf(times[0][i], sizeof(times[0][i]), "%02d", hour);
                else snprintf(times[0][i], sizeof(times[0][i]), "%d%s", hour%12 ? hour%12 : 12, hour<12 ? "AM" : "PM");
                dashboard->days[0].samples[i] = (wind_renderer_sample_t){.time=times[0][i]};
                if (source) dashboard->days[0].samples[i] = wind_dashboard_forecast_sample(source, times[0][i], true);
                dashboard->swell[0][i] = dashboard->secondary_swell[0][i] = (wind_renderer_swell_sample_t){-1,-1,-1};
                if (runtime->have_swell) for (size_t j = 0; j < runtime->swell.sample_count; ++j) {
                    const wind_swell_sample_t *swell = &runtime->swell.samples[j];
                    wind_local_datetime_t date;
                    char local_date[WIND_FORECAST_DATE_LENGTH];
                    if (wind_timezone_from_unix(spot->timezone, swell->timestamp, &date) != ESP_OK || date.hour != hour) continue;
                    wind_timezone_format_date(&date, local_date, sizeof(local_date));
                    if (strcmp(local_date, forecast->days[focused].local_date)) continue;
                    dashboard->swell[0][i] = wind_dashboard_swell_sample(swell, false);
                    dashboard->secondary_swell[0][i] = wind_dashboard_swell_sample(swell, true);
                    break;
                }
            }
            int count = 0;
            for (int i = 0; i < dashboard->tide_sample_count; ++i) if (dashboard->tide_samples[i].day_index == focused) {
                dashboard->tide_samples[count] = dashboard->tide_samples[i];
                dashboard->tide_samples[count++].day_index = 0;
            }
            dashboard->tide_sample_count = count;
            dashboard->tide_available = count >= 2;
            count = 0;
            for (int i = 0; i < dashboard->tide_extremum_count; ++i) if (dashboard->tide_extrema[i].day_index == focused) {
                dashboard->tide_extrema[count] = dashboard->tide_extrema[i];
                dashboard->tide_extrema[count++].day_index = 0;
            }
            dashboard->tide_extremum_count = count;
        }
    }
    if (!s_preview_configuration && !wind_renderer_dashboard_valid(dashboard)) {
        ESP_LOGE(TAG, "Invalid dashboard for %s (tide=%d/%d, wind=%d, swell=%d, threshold=%d)",
                 spot->id, dashboard->tide_available, dashboard->tide_sample_count,
                 dashboard->wind_size, dashboard->swell_size, dashboard->threshold_kt);
        // Invalid render input is a programming error, not absent forecast
        // data. Preserve the panel and never cache a fabricated empty screen.
        return ESP_ERR_INVALID_STATE;
    }
    wind_renderer_stats_t stats;
    int render_result = wind_renderer_render_for_display(
        dashboard, active_renderer_display(), bitmap, bitmap_size, &stats);
    if (render_result != 0) {
        ESP_LOGE(TAG, "Dashboard render failed: %d", render_result);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t render_dashboard(void *context, const wind_forecast_t *forecast,
                                  wind_freshness_t freshness, bool refresh_failed,
                                  int64_t now, uint8_t *bitmap, size_t bitmap_size) {
    /* Hourly detail expands the workspace beyond the task stack budget. */
    struct {
        wind_renderer_dashboard_t dashboard;
        wind_forecast_t calendar;
    } *workspace = calloc(1, sizeof(*workspace));
    if (!workspace) return ESP_ERR_NO_MEM;
    esp_err_t result = render_dashboard_with_workspace(context, forecast, freshness,
        refresh_failed, now, bitmap, bitmap_size, &workspace->dashboard, &workspace->calendar);
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    // Prepared images are independent of panel confirmation. Capture the exact
    // render input, never a cache file a background fetch may have replaced.
    wind_quick_spot_header_t header;
    if (result == ESP_OK && !refresh_failed && !s_preview_configuration &&
        wind_quick_spot_capture(context, forecast, s_focused_date, s_overview_configuration,
                                now, workspace->dashboard.battery_percent, &header))
        wind_quick_spot_save(context, &header, bitmap, bitmap_size);
#endif
    free(workspace);
    return result;
}

static esp_err_t display_dashboard(void *context, const uint8_t *bitmap,
                                   size_t bitmap_size) {
    const wind_renderer_display_t display = active_renderer_display();
    if (!bitmap || bitmap_size != active_renderer_bitmap_size()) {
        return ESP_ERR_INVALID_SIZE;
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
    esp_err_t end_result = display_manager_end_rgb_stream(result == ESP_OK);
    free(row);
    if (result != ESP_OK || end_result != ESP_OK)
        return result != ESP_OK ? result : end_result;
    (void)context;
    // The preview is useful over USB, but its SD write must not delay the panel.
    if (write_dashboard_preview(bitmap, bitmap_size, display) != ESP_OK)
        ESP_LOGW(TAG, "Could not publish dashboard preview");
    return ESP_OK;
}

esp_err_t wind_app_show_battery_empty(void) {
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    const size_t size = active_renderer_bitmap_size();
    uint8_t *bitmap = malloc(size);
    esp_err_t result = bitmap ? ESP_OK : ESP_ERR_NO_MEM;
    if (bitmap && clear_panel_confirmation_unlocked() != ESP_OK)
        ESP_LOGW(TAG, "Could not invalidate panel cache before battery screen");
    if (result == ESP_OK) {
        result = wind_renderer_render_battery_empty_for_display(
            active_renderer_display(), bitmap, size) == 0 ? ESP_OK : ESP_FAIL;
        if (result == ESP_OK) result = display_dashboard(NULL, bitmap, size);
    }
    free(bitmap);
    xSemaphoreGive(s_runtime_lock);
    return result;
}

static esp_err_t show_setup_unlocked(void) {
    const size_t size = active_renderer_bitmap_size();
    uint8_t *bitmap = malloc(size);
    esp_err_t result = bitmap ? ESP_OK : ESP_ERR_NO_MEM;
    (void)clear_panel_confirmation_unlocked();
    if (result == ESP_OK) {
        result = wind_renderer_render_setup(active_renderer_display(), bitmap, size)
            == 0 ? ESP_OK : ESP_FAIL;
        if (result == ESP_OK) result = display_dashboard(NULL, bitmap, size);
    }
    free(bitmap);
    return result;
}

esp_err_t wind_app_show_setup(void) {
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    // The installer may finish while this task waits for the panel lock.
    if (installed_configuration_has_setup()) {
        xSemaphoreGive(s_runtime_lock);
        return ESP_OK;
    }
    if (power_manager_is_installer_active()) {
        xSemaphoreGive(s_runtime_lock);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = show_setup_unlocked();
    xSemaphoreGive(s_runtime_lock);
    return result;
}

esp_err_t wind_app_show_failed_setup(void) {
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    // A failed candidate can already be visible on e-paper. Replace it even
    // while the USB session is active or an older setup is saved.
    esp_err_t result = show_setup_unlocked();
    xSemaphoreGive(s_runtime_lock);
    return result;
}

static esp_err_t ensure_ready(void) {
    if (s_ready) {
        return ESP_OK;
    }
    if (wind_spots_count() > INSTALLED_CONFIGURATION_MAX_SPOTS) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!s_spots) {
        s_spots = heap_caps_calloc(INSTALLED_CONFIGURATION_MAX_SPOTS, sizeof(*s_spots), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_spots) return ESP_ERR_NO_MEM;
    }
    if (!s_app_lock) {
        s_app_lock = xSemaphoreCreateMutex();
        if (!s_app_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_preview_configuration) s_installed_configuration = *s_preview_configuration;
    else if (installed_configuration_load(&s_installed_configuration) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t digest = installed_configuration_digest(&s_installed_configuration);
    if (!s_preview_configuration && s_overview_configuration != digest) {
        set_displayed_view(false, 0); s_overview_configuration=digest;
        s_focused_date[0]=0;
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
                           FS_MOUNT_POINT "/wind-%s.cache", runtime->spot->id);
        int schedule_length =
            index == 0
                ? snprintf(runtime->schedule_path, sizeof(runtime->schedule_path), "%s",
                           WIND_SCHEDULE_CACHE_PATH)
                : snprintf(runtime->schedule_path, sizeof(runtime->schedule_path),
                           FS_MOUNT_POINT "/wind-%s.schedule", runtime->spot->id);
        int tide_length = index == 0
                              ? snprintf(runtime->tide_path, sizeof(runtime->tide_path),
                                         "%s", WIND_TIDE_CACHE_PATH)
                              : snprintf(runtime->tide_path, sizeof(runtime->tide_path),
                                         FS_MOUNT_POINT "/wind-%s.tide", runtime->spot->id);
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
            .model = index == 0 ? s_installed_configuration.forecast_model : s_installed_configuration.additional_spots[index - 1].forecast_model,
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
        runtime->marine_config.swell_model = index == 0 ? s_installed_configuration.display.swell_model : s_installed_configuration.additional_spots[index - 1].display.swell_model;
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
                         .model = index == 0 ? s_installed_configuration.forecast_model : s_installed_configuration.additional_spots[index - 1].forecast_model},
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
        runtime->app.force_display = s_force_next_display;
    }
    apply_spot_display(s_selected_index);
    s_ready = true;
    if (atomic_load(&s_displayed_view) == VIEW_UNINITIALIZED) set_displayed_view(false, 0);
    return ESP_OK;
}

esp_err_t wind_app_configure_runtime(void) {
    if (!s_runtime_lock) {
        s_runtime_lock = xSemaphoreCreateMutex();
        if (!s_runtime_lock) return ESP_ERR_NO_MEM;
    }
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    if (!s_fetch_lock) {
        s_fetch_lock = xSemaphoreCreateMutex();
        if (!s_fetch_lock) return ESP_ERR_NO_MEM;
    }
#endif
    installed_configuration_t installed;
    if (installed_configuration_load(&installed) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!config_manager_set_timezone_transient(installed.spot.timezone)) {
        return ESP_ERR_INVALID_ARG;
    }
#ifndef CONFIG_BOARD_CAP_WINDPEEK
    // Other photo-frame boards still use the legacy cron-backed power manager.
    static const char *rules[] = {"5 0 *", "0 7 *", "0 11 *", "0 15 *", "0 19 *"};
    config_manager_set_cron_rules(rules, 5);
    config_manager_set_auto_rotate(true);
#endif
    return ESP_OK;
}

static wind_display_config_t
display_from_installed(const installed_display_configuration_t *installed) {
    wind_display_config_t display;
    wind_display_config_default(&display);
    display.display_mode = installed->show_threshold
                               ? WIND_RENDERER_MODE_THRESHOLD
                               : WIND_RENDERER_MODE_SOLID;
    display.wind_size = installed->wind_size;
    display.swell_size = installed->swell_size;
    memcpy(display.module_order, installed->module_order, sizeof(display.module_order));
    display.threshold_kt = installed->threshold_kt >= WIND_RENDERER_MIN_THRESHOLD_KT &&
        installed->threshold_kt <= WIND_RENDERER_MAX_THRESHOLD_KT
        ? installed->threshold_kt : WIND_RENDERER_DEFAULT_THRESHOLD_KT;
    display.show_weather = installed->show_weather;
    display.show_temperature = installed->show_temperature;
    display.show_tide = installed->show_tide;
    display.show_dedicated_footer = installed->show_dedicated_footer;
    display.use_24_hour = installed->use_24_hour;
    display.temperature_fahrenheit = installed->temperature_fahrenheit;
    return display;
}

static void apply_spot_display(size_t index) {
    const installed_display_configuration_t *settings = index == 0
        ? &s_installed_configuration.display : &s_installed_configuration.additional_spots[index - 1].display;
    const wind_display_config_t display = display_from_installed(settings);
    (void)config_manager_set_wind_display_config_transient(&display);
    (void)config_manager_set_timezone_transient(s_spots[index].spot->timezone);
}

#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
static uint64_t quick_overview_configuration(void) {
    return s_overview_configuration ^ WIND_DASHBOARD_RENDER_SIGNATURE;
}

static bool show_quick_spot(wind_spot_runtime_t *runtime) {
    if (!wind_quick_spot_show(runtime, s_focused_date, s_overview_configuration))
        return false;
    (void)clear_panel_confirmation_unlocked();
    s_last_render_succeeded = true;
    return true;
}

static bool show_quick_overview(size_t page) {
    time_t now;
    time(&now);
    if (!wind_quick_overview_show(s_spots, wind_spots_count(),
                                  quick_overview_configuration(), page, now))
        return false;
    (void)clear_panel_confirmation_unlocked();
    s_last_render_succeeded = true;
    set_displayed_view(true, page);
    s_focused_date[0] = 0;
    return true;
}
#endif

/* All overview work runs under s_runtime_lock. No intermediate row is shown. */
static esp_err_t render_overview_unlocked(size_t page,
    overview_render_mode_t mode, bool force) {
    const bool show = mode != OVERVIEW_PREPARE;
    const bool fetch = mode == OVERVIEW_REFRESH;
    if (active_renderer_display() != WIND_RENDERER_DISPLAY_E1003_GC16) {
        if (show) wind_app_status_finish(ESP_ERR_NOT_SUPPORTED, ESP_OK, false, false, NULL);
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t result = ensure_ready();
    if (result != ESP_OK) {
        if (show) wind_app_status_finish(result, ESP_OK, false, false, NULL);
        return result;
    }
    size_t total = wind_spots_count();
    if (page > wind_overview_last_page(total)) {
        if (show) wind_app_status_finish(ESP_ERR_INVALID_ARG, ESP_OK, false, false, NULL);
        return ESP_ERR_INVALID_ARG;
    }
    size_t first = page * WIND_OVERVIEW_PAGE_SIZE;
    size_t count = total-first < WIND_OVERVIEW_PAGE_SIZE ? total-first : WIND_OVERVIEW_PAGE_SIZE;
    wind_renderer_dashboard_t *rows = calloc(count, sizeof(*rows));
    wind_forecast_t *cached = malloc(sizeof(*cached));
    uint8_t *bitmap = malloc(WIND_RENDERER_E1003_COMPOSITION_BYTES);
    if (!rows || !cached || !bitmap) {
        free(rows); free(cached); free(bitmap);
        if (show) wind_app_status_finish(ESP_ERR_NO_MEM, ESP_OK, false, false, NULL);
        return ESP_ERR_NO_MEM;
    }
    bool reported_failure = false;
    time_t now; time(&now);
    bool use_swell[WIND_OVERVIEW_PAGE_SIZE] = {0};
    for (size_t row = 0; row < count; ++row) {
        size_t index = first+row;
        wind_spot_runtime_t *runtime = &s_spots[index];
        apply_spot_display(index);
        const wind_display_config_t display = config_manager_get_wind_display_config();
        /* Prefer the large graph; when both are large, use their configured order. */
        bool swell = display.swell_size == 2 || (!display.wind_size && display.swell_size);
        if (display.wind_size == 2 && display.swell_size == 2)
            for (int m=0; m<5; ++m) {
                if (display.module_order[m] == 0) { swell=false; break; }
                if (display.module_order[m] == 1) { swell=true; break; }
            }
        use_swell[row] = swell;
        /* Advance attempts even when offline or displaying swell. Otherwise an
           overdue wind retry can keep waking the overview every second. This
           also prepares the wind cache for opening the full spot dashboard. */
        wind_app_outcome_t outcome = {0};
        const esp_err_t prefetch_result = fetch
            ? wind_app_prefetch(&runtime->app, force, now, &outcome) : ESP_OK;
        if (fetch && !reported_failure && (prefetch_result != ESP_OK || outcome.fetch_result != ESP_OK ||
                                  outcome.freshness == WIND_FRESHNESS_UNAVAILABLE)) {
            wind_provider_diagnostics_t forecast = {0};
            if (outcome.attempted_fetch) open_meteo_knmi_get_diagnostics(&forecast);
            wind_app_status_finish(prefetch_result, outcome.fetch_result,
                                   outcome.attempted_fetch, false, &forecast);
            reported_failure = true;
        }
        if (fetch && wifi_manager_is_connected()) {
            if (swell) load_or_refresh_swell(runtime, force, true, now);
        } else if (swell) {
            char path[128]; snprintf(path,sizeof(path),"%s.swell",runtime->forecast_path);
            const wind_swell_cache_identity_t identity = {runtime->spot->id,runtime->spot->timezone,runtime->marine_config.swell_model};
            runtime->have_swell = wind_swell_cache_load(path,&identity,&runtime->swell) == ESP_OK;
        }
    }
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    // Snapshot after our downloads, before reading the pixels' source data.
    // A later concurrent publication must invalidate this image, not relabel it.
    wind_quick_overview_header_t quick_header = wind_quick_overview_identity(
        s_spots, total, quick_overview_configuration(), page, now);
#endif
    for (size_t row = 0; row < count; ++row) {
        wind_spot_runtime_t *runtime = &s_spots[first + row];
        const bool swell = use_swell[row];
        bool have_wind = wind_cache_load(runtime->forecast_path,&runtime->app.config.identity,cached) == ESP_OK;
        result = wind_dashboard_build_overview_row(
            runtime->spot->display_name, runtime->spot->timezone,
            have_wind ? cached : NULL,
            swell && runtime->have_swell ? &runtime->swell : NULL,
            swell, now, &rows[row]);
        if (result != ESP_OK) break;
    }
    apply_spot_display(s_selected_index);
    refresh_render_signatures();
    if (result == ESP_OK) {
        wind_renderer_stats_t stats;
        result = wind_renderer_render_overview(rows,count,first,total,bitmap,
            WIND_RENDERER_E1003_COMPOSITION_BYTES,&stats) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (result == ESP_OK && show) {
        /* The regular dashboard hash must never suppress returning from this page. */
        (void)clear_panel_confirmation_unlocked();
        result = display_dashboard(NULL,bitmap,WIND_RENDERER_E1003_COMPOSITION_BYTES);
        if (result == ESP_OK) {
            set_displayed_view(true, page);
            s_focused_date[0]=0;
        }
    }
    if (result == ESP_OK) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
        wind_quick_overview_save(&quick_header, bitmap);
#endif
    }
    free(rows); free(cached); free(bitmap);
    if (show && result != ESP_OK && !reported_failure)
        wind_app_status_finish(result, ESP_OK, false, false, NULL);
    // Overview rows do not save a dashboard panel confirmation. Drawing them
    // cannot clear an earlier forecast failure, even when cached rows look valid.
    return result;
}

esp_err_t wind_app_show_overview(void) {
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock,portMAX_DELAY)!=pdTRUE) return ESP_ERR_INVALID_STATE;
    esp_err_t result=ensure_ready();
    if (result==ESP_OK) {
        const size_t page = overview_open() ? overview_page() : s_selected_index/3;
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
        if (!show_quick_overview(page))
#endif
            result=render_overview_unlocked(page, OVERVIEW_INTERACTIVE, false);
    }
    xSemaphoreGive(s_runtime_lock);
    return result;
}

esp_err_t wind_app_overview_page(int direction) {
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock,portMAX_DELAY)!=pdTRUE) return ESP_ERR_INVALID_STATE;
    int page=(int)overview_page()+direction;
    esp_err_t result=ESP_OK;
    if (overview_open() && page>=0 && (size_t)page<=wind_overview_last_page(wind_spots_count())) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
        if (!show_quick_overview((size_t)page))
#endif
            result=render_overview_unlocked((size_t)page, OVERVIEW_INTERACTIVE, false);
    }
    xSemaphoreGive(s_runtime_lock);
    return result;
}

void wind_app_overview_state(bool *open,size_t *page) {
    if (!open || !page) return;
    *open=false; *page=0;
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock,portMAX_DELAY)!=pdTRUE) return;
    if (ensure_ready()==ESP_OK) { *open=overview_open(); *page=overview_page(); }
    xSemaphoreGive(s_runtime_lock);
}

bool wind_app_overview_state_if_ready(bool *open,size_t *page) {
    if (!open || !page) return false;
    const unsigned view = atomic_load(&s_displayed_view);
    if (view == VIEW_UNINITIALIZED) return false;
    *open = view >= VIEW_OVERVIEW_FIRST;
    *page = *open ? view - VIEW_OVERVIEW_FIRST : 0;
    return true;
}

static esp_err_t preview_configuration(const installed_configuration_t *candidate,
                                      wind_provider_diagnostics_t *diagnostics) {
    if (diagnostics) memset(diagnostics, 0, sizeof(*diagnostics));
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
    const wind_display_config_t preview_display = display_from_installed(&candidate->display);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (config_manager_set_wind_display_config_transient(&preview_display) &&
        config_manager_set_timezone_transient(candidate->spot.timezone) &&
        wind_spots_use_configuration(candidate) == ESP_OK) {
        s_preview_configuration = candidate;
        s_ready = false;
        wind_app_outcome_t outcome;
        result = wind_app_refresh_unlocked(true, &outcome);
        // A preparation failure must not report an earlier fetch's HTTP result.
        if (outcome.attempted_fetch) open_meteo_knmi_get_diagnostics(diagnostics);
        s_preview_configuration = NULL;
    }
    (void)wind_spots_use_configuration(&active);
    (void)config_manager_set_timezone_transient(active.spot.timezone);
    (void)config_manager_set_wind_display_config_transient(&old_display);
    s_ready = false;
    xSemaphoreGive(s_runtime_lock);
    return result;
}

esp_err_t wind_app_preview_configuration(const installed_configuration_t *candidate,
                                         wind_provider_diagnostics_t *diagnostics) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    // Setup verification writes the same forecast files as a refresh round.
    if (!s_fetch_lock || xSemaphoreTake(s_fetch_lock, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
#endif
    const esp_err_t result = preview_configuration(candidate, diagnostics);
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    xSemaphoreGive(s_fetch_lock);
#endif
    return result;
}

esp_err_t
wind_app_activate_configuration(const installed_configuration_t *configuration) {
    if (!installed_configuration_validate(configuration)) return ESP_ERR_INVALID_ARG;
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    const wind_display_config_t display = display_from_installed(&configuration->display);
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
    atomic_store(&s_displayed_view, VIEW_UNINITIALIZED);
    xSemaphoreGive(s_runtime_lock);
    return ESP_OK;
}

static esp_err_t wind_app_refresh_unlocked(bool force_refresh, wind_app_outcome_t *outcome) {
    memset(outcome, 0, sizeof(*outcome));
    const bool report_status = s_preview_configuration == NULL;
    esp_err_t result = ensure_ready();
    if (result != ESP_OK) {
        s_last_render_succeeded = false;
        if (report_status) wind_app_status_finish(result, ESP_OK, false, false, NULL);
        return result;
    }
    if (overview_open() && !s_preview_configuration) {
        return render_overview_unlocked(overview_page(), OVERVIEW_REFRESH, force_refresh);
    }
    if (report_status) wind_app_status_begin();
    xSemaphoreTake(s_app_lock, portMAX_DELAY);
    time_t now;
    time(&now);
    if (report_status) wind_app_status_stage(WIND_REFRESH_SWELL);
    load_or_refresh_swell(&s_spots[s_selected_index], force_refresh, true, now);
    refresh_render_signatures();
    if (report_status) wind_app_status_stage(WIND_REFRESH_TIDE);
    load_or_refresh_tide(&s_spots[s_selected_index], force_refresh, true, now);
    // Fetch other spots when selected. A slow/offline location must not delay
    // installing or refreshing the spot currently shown on the panel.
    if (report_status) wind_app_status_stage(WIND_REFRESH_FORECAST);
    result = s_preview_configuration
        ? wind_app_run_setup(&s_spots[s_selected_index].app, now, outcome)
        : wind_app_run(&s_spots[s_selected_index].app, force_refresh, now, outcome);
    if (outcome->displayed) s_force_next_display = false;
    s_last_render_succeeded = result == ESP_OK &&
        outcome->freshness != WIND_FRESHNESS_UNAVAILABLE &&
        (outcome->displayed || outcome->display_unchanged);
    if (report_status) {
        wind_provider_diagnostics_t forecast = {0};
        if (outcome->attempted_fetch) open_meteo_knmi_get_diagnostics(&forecast);
        wind_app_status_finish(result, outcome->fetch_result, outcome->attempted_fetch,
                               s_last_render_succeeded, &forecast);
    }
    xSemaphoreGive(s_app_lock);
    return result;
}

esp_err_t wind_app_refresh(bool force_refresh) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    // Every refresh is a device-wide round, including manual refreshes and wakes.
    (void)force_refresh;
    return prefetch_spots(true, true);
#else
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    wind_app_outcome_t outcome = {0};
    esp_err_t result = wind_app_refresh_unlocked(force_refresh, &outcome);
    xSemaphoreGive(s_runtime_lock);
    if (outcome.published_forecast) {
        time_t now;
        time(&now);
        const esp_err_t analytics_result = wind_analytics_maybe_send(now);
        if (analytics_result != ESP_OK) {
            ESP_LOGW(TAG, "Dashboard activity heartbeat failed: %s",
                     esp_err_to_name(analytics_result));
        }
    }
    return result;
#endif
}

static esp_err_t navigate(int direction, bool absolute) {
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ensure_ready();
    if (result != ESP_OK) {
        xSemaphoreGive(s_runtime_lock);
        return result;
    }
    xSemaphoreTake(s_app_lock, portMAX_DELAY);
    const size_t target = absolute ? (size_t)direction : wind_spots_offset(s_selected_index, direction);
    if (target >= wind_spots_count()) {
        xSemaphoreGive(s_app_lock); xSemaphoreGive(s_runtime_lock); return ESP_ERR_INVALID_ARG;
    }
    wind_spot_runtime_t *runtime = &s_spots[target];
    char previous_focus[sizeof(s_focused_date)];
    memcpy(previous_focus, s_focused_date, sizeof(previous_focus));
    s_focused_date[0] = 0;
    apply_spot_display(target);
    wind_forecast_t cached;
    const bool have_cache =
        wind_cache_load(runtime->forecast_path, &runtime->app.config.identity,
                        &cached) == ESP_OK;
    bool fetch_before_display = true;
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    fetch_before_display = false;
#endif
    time_t now;
    time(&now);
    load_or_refresh_swell(runtime, false, fetch_before_display, now);
    refresh_render_signatures();
    load_or_refresh_tide(runtime, false, fetch_before_display, now);
    wind_app_outcome_t outcome = {0};
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    result = have_cache && show_quick_spot(runtime)
        ? ESP_OK : wind_app_show_cached(&runtime->app, now, &outcome);
#else
    result = wind_app_run(&runtime->app, !have_cache, now, &outcome);
#endif
    if (outcome.displayed) s_force_next_display = false;
    if (result == ESP_OK) {
        set_displayed_view(false, 0);
        s_selected_index = target;
        esp_err_t store_result = wind_spots_store_selected(target);
        if (store_result != ESP_OK) {
            ESP_LOGW(TAG, "Could not persist selected spot: %s",
                     esp_err_to_name(store_result));
        }
        ESP_LOGI(TAG, "Selected spot %s (cached=%d)", runtime->spot->id, have_cache);
    }
    if (result != ESP_OK) {
        memcpy(s_focused_date, previous_focus, sizeof(s_focused_date));
        apply_spot_display(s_selected_index);
        refresh_render_signatures();
    }
    xSemaphoreGive(s_app_lock);
    xSemaphoreGive(s_runtime_lock);
    if (outcome.published_forecast) {
        const esp_err_t analytics_result = wind_analytics_maybe_send(now);
        if (analytics_result != ESP_OK) {
            ESP_LOGW(TAG, "Dashboard activity heartbeat failed: %s",
                     esp_err_to_name(analytics_result));
        }
    }
    return result;
}

esp_err_t wind_app_toggle_day(size_t day_index) {
    if (day_index >= WIND_RENDERER_DAY_COUNT || active_renderer_display() != WIND_RENDERER_DISPLAY_E1003_GC16)
        return ESP_ERR_INVALID_ARG;
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    esp_err_t result = ensure_ready();
    if (result != ESP_OK || overview_open() || s_preview_configuration) {
        xSemaphoreGive(s_runtime_lock);
        return result != ESP_OK ? result : ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_app_lock, portMAX_DELAY);
    char previous[sizeof(s_focused_date)];
    memcpy(previous, s_focused_date, sizeof(previous));
    time_t now; time(&now);
    wind_spot_runtime_t *runtime = &s_spots[s_selected_index];
    if (s_focused_date[0]) s_focused_date[0] = 0;
    else {
        wind_local_datetime_t date;
        result = wind_timezone_from_unix(runtime->spot->timezone, now, &date);
        if (result == ESP_OK) {
            wind_timezone_shift_date(&date, (int)day_index);
            result = wind_timezone_format_date(&date, s_focused_date, sizeof(s_focused_date));
        }
    }
    if (result == ESP_OK) {
        runtime->app.force_display = true;
        wind_app_outcome_t outcome;
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
        if (show_quick_spot(runtime)) {
            xSemaphoreGive(s_app_lock);
            xSemaphoreGive(s_runtime_lock);
            return ESP_OK;
        }
#endif
        result = wind_app_show_cached(&runtime->app, now, &outcome);
        s_last_render_succeeded = result == ESP_OK &&
            outcome.freshness != WIND_FRESHNESS_UNAVAILABLE &&
            (outcome.displayed || outcome.display_unchanged);
    }
    if (result != ESP_OK) memcpy(s_focused_date, previous, sizeof(s_focused_date));
    xSemaphoreGive(s_app_lock);
    xSemaphoreGive(s_runtime_lock);
    return result;
}

esp_err_t wind_app_select_spot(size_t index) {
    if (index >= wind_spots_count()) return ESP_ERR_INVALID_ARG;
    return navigate((int)index, true);
}

esp_err_t wind_app_select_previous(void) {
    return navigate(-1, false);
}
esp_err_t wind_app_select_next(void) {
    return navigate(1, false);
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
    if (outcome.displayed) s_force_next_display = false;
    xSemaphoreGive(s_app_lock);
    xSemaphoreGive(s_runtime_lock);
    return result;
}

static bool navigation_requires_network(int direction, bool absolute) {
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE) {
        return true;
    }
    if (ensure_ready() != ESP_OK) {
        xSemaphoreGive(s_runtime_lock);
        return true;
    }
    const size_t target = absolute ? (size_t)direction : wind_spots_offset(s_selected_index, direction);
    if (target>=wind_spots_count()) { xSemaphoreGive(s_runtime_lock); return false; }
    wind_forecast_t cached;
    bool requires_network =
        wind_cache_load(s_spots[target].forecast_path,
                        &s_spots[target].app.config.identity, &cached) != ESP_OK;
    // E1003 navigation displays its cache first, but must still request a
    // background refresh for expired wind data or missing marine modules.
    time_t now;
    time(&now);
    const wind_display_config_t display = display_from_installed(target == 0
        ? &s_installed_configuration.display : &s_installed_configuration.additional_spots[target - 1].display);
    const wind_spot_runtime_t *runtime = &s_spots[target];
    int64_t boundary = 0;
    if (!requires_network) requires_network =
        !wind_app_forecast_covers_window(&cached, runtime->spot->timezone, now) ||
        wind_schedule_is_due(&runtime->app.schedule, now, &boundary) ||
        wind_schedule_retry_is_due(&runtime->app.schedule, now, &boundary);
    if (!requires_network && display.swell_size) {
        char path[128];
        snprintf(path, sizeof(path), "%s.swell", runtime->forecast_path);
        const wind_swell_cache_identity_t identity = {
            runtime->spot->id, runtime->spot->timezone, runtime->marine_config.swell_model
        };
        wind_swell_t *swell = malloc(sizeof(*swell));
        requires_network = !swell || wind_swell_cache_load(path, &identity, swell) != ESP_OK ||
            swell->retrieved_at > now || now - swell->retrieved_at >= 6 * 3600;
        free(swell);
    }
    if (!requires_network && display.show_tide) {
        const wind_tide_cache_identity_t identity = { runtime->spot->id, runtime->spot->timezone };
        wind_tide_t *tide = malloc(sizeof(*tide));
        requires_network = !tide || wind_tide_cache_load(runtime->tide_path, &identity, tide) != ESP_OK ||
            tide->retrieved_at > now || now - tide->retrieved_at >= WIND_TIDE_REFRESH_INTERVAL_SECONDS;
        free(tide);
    }
    xSemaphoreGive(s_runtime_lock);
    return requires_network;
}

bool wind_app_navigation_requires_network(int direction) { return navigation_requires_network(direction,false); }
bool wind_app_spot_requires_network(size_t index) { return navigation_requires_network((int)index,true); }
bool wind_app_overview_requires_network(int direction) {
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock,portMAX_DELAY)!=pdTRUE) return false;
    if (ensure_ready()!=ESP_OK) { xSemaphoreGive(s_runtime_lock); return false; }
    int page=(int)(overview_open() ? overview_page() : s_selected_index/3)+direction;
    size_t count=wind_spots_count();
    xSemaphoreGive(s_runtime_lock);
    if (page<0 || (size_t)page>wind_overview_last_page(count)) return false;
    for (size_t i=(size_t)page*3;i<count && i<(size_t)page*3+3;++i)
        if (wind_app_spot_requires_network(i)) return true;
    return false;
}

esp_err_t wind_app_start(void) {
    return wind_app_refresh(false);
}

bool wind_app_has_cached_start(void) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE)
        return false;
    bool cached = false;
    if (ensure_ready() == ESP_OK) {
        time_t now;
        time(&now);
        wind_spot_runtime_t *runtime = &s_spots[s_selected_index];
        apply_spot_display(s_selected_index);
        load_or_refresh_swell(runtime, false, false, now);
        load_or_refresh_tide(runtime, false, false, now);
        refresh_render_signatures();
        cached = wind_quick_spot_cached(runtime, "", s_overview_configuration);
    }
    xSemaphoreGive(s_runtime_lock);
    return cached;
#else
    return false;
#endif
}

esp_err_t wind_app_show_cached_start(void) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    if (wind_app_has_cached_start()) {
        size_t selected = 0;
        if (wind_spots_load_selected(&selected) == ESP_OK)
            return wind_app_select_spot(selected);
    }
#endif
    return wind_app_start();
}

#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
static esp_err_t prepare_quick_frame_unlocked(size_t index, int variant,
    wind_forecast_t *forecast, uint8_t *bitmap, time_t now) {
    wind_spot_runtime_t *runtime = &s_spots[index];
    if (wind_cache_load(runtime->forecast_path, &runtime->app.config.identity,
                        forecast) != ESP_OK) return ESP_OK;
    s_focused_date[0] = 0;
    if (variant) {
        wind_local_datetime_t date;
        if (wind_timezone_from_unix(runtime->spot->timezone, now, &date) != ESP_OK)
            return ESP_ERR_INVALID_STATE;
        wind_timezone_shift_date(&date, variant - 1);
        if (wind_timezone_format_date(&date, s_focused_date, sizeof(s_focused_date)) != ESP_OK)
            return ESP_ERR_INVALID_STATE;
    }
    if (wind_quick_spot_cached(runtime, s_focused_date,
                               s_overview_configuration)) return ESP_OK;
    esp_err_t result = render_dashboard(runtime, forecast,
        wind_app_forecast_freshness(forecast, now), false, now,
        bitmap, WIND_RENDERER_E1003_COMPOSITION_BYTES);
    return result;
}
#endif

esp_err_t wind_app_prepare_quick_frame(size_t index, int variant) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    if (variant < 0 || variant > WIND_RENDERER_DAY_COUNT) return ESP_ERR_INVALID_ARG;
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    esp_err_t result = ensure_ready();
    if (result == ESP_OK && (s_preview_configuration || index >= wind_spots_count()))
        result = ESP_ERR_INVALID_STATE;
    wind_forecast_t *forecast = NULL;
    uint8_t *bitmap = NULL;
    char previous_focus[sizeof(s_focused_date)];
    memcpy(previous_focus, s_focused_date, sizeof(previous_focus));
    if (result == ESP_OK) {
        forecast = malloc(sizeof(*forecast));
        bitmap = heap_caps_malloc(WIND_RENDERER_E1003_COMPOSITION_BYTES,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!forecast || !bitmap) result = ESP_ERR_NO_MEM;
        else {
            time_t now;
            time(&now);
            wind_spot_runtime_t *runtime = &s_spots[index];
            apply_spot_display(index);
            load_or_refresh_swell(runtime, false, false, now);
            load_or_refresh_tide(runtime, false, false, now);
            refresh_render_signatures();
            result = prepare_quick_frame_unlocked(index, variant, forecast, bitmap, now);
        }
    }
    memcpy(s_focused_date, previous_focus, sizeof(s_focused_date));
    if (s_ready) {
        apply_spot_display(s_selected_index);
        refresh_render_signatures();
    }
    free(bitmap);
    free(forecast);
    xSemaphoreGive(s_runtime_lock);
    return result;
#else
    (void)index; (void)variant;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t wind_app_prepare_quick_overview(size_t page) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    esp_err_t result = ensure_ready();
    if (result == ESP_OK && s_preview_configuration)
        result = ESP_ERR_INVALID_STATE;
    if (result == ESP_OK && page > wind_overview_last_page(wind_spots_count()))
        result = ESP_ERR_INVALID_ARG;
    if (result == ESP_OK) {
        time_t now;
        time(&now);
        if (!wind_quick_overview_cached(s_spots, wind_spots_count(),
                                        quick_overview_configuration(), page, now))
            result = render_overview_unlocked(page, OVERVIEW_PREPARE, false);
    }
    xSemaphoreGive(s_runtime_lock);
    return result;
#else
    (void)page;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t wind_app_prepare_quick_frames(void) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    esp_err_t result = ensure_ready();
    wind_forecast_t *forecast = malloc(sizeof(*forecast));
    uint8_t *bitmap = heap_caps_malloc(WIND_RENDERER_E1003_COMPOSITION_BYTES,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (result == ESP_OK && (!forecast || !bitmap)) result = ESP_ERR_NO_MEM;
    char previous_focus[sizeof(s_focused_date)];
    memcpy(previous_focus, s_focused_date, sizeof(previous_focus));
    time_t now;
    time(&now);
    if (result == ESP_OK && !s_preview_configuration) {
        const size_t count = wind_spots_count();
        for (size_t rank = 0; rank < count; ++rank) {
            const int offset = rank == 0 ? 0 : (rank & 1u)
                ? (int)((rank + 1) / 2) : -(int)(rank / 2);
            const size_t index = wind_spots_offset(s_selected_index, offset);
            wind_spot_runtime_t *runtime = &s_spots[index];
            apply_spot_display(index);
            load_or_refresh_swell(runtime, false, false, now);
            load_or_refresh_tide(runtime, false, false, now);
            refresh_render_signatures();
            for (int variant = 0; variant <= WIND_RENDERER_DAY_COUNT; ++variant) {
                result = prepare_quick_frame_unlocked(index, variant,
                    forecast, bitmap, now);
                if (result != ESP_OK) break;
            }
            if (result != ESP_OK) break;
        }
        free(bitmap);
        bitmap = NULL;
        if (result == ESP_OK) {
            for (size_t page = 0; page <= wind_overview_last_page(count); ++page) {
                if (wind_quick_overview_cached(s_spots, count,
                        quick_overview_configuration(), page, now)) continue;
                result = render_overview_unlocked(page, OVERVIEW_PREPARE, false);
                if (result != ESP_OK) break;
            }
        }
    }
    memcpy(s_focused_date, previous_focus, sizeof(s_focused_date));
    if (s_ready) {
        apply_spot_display(s_selected_index);
        refresh_render_signatures();
    }
    free(bitmap);
    free(forecast);
    xSemaphoreGive(s_runtime_lock);
    return result;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
static esp_err_t prefetch_spots_unlocked(bool all, bool force) {
    wind_app_outcome_t outcomes[INSTALLED_CONFIGURATION_MAX_SPOTS] = {0};
    if (!s_runtime_lock || xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    esp_err_t result = ensure_ready();
    const size_t count = result == ESP_OK ? wind_spots_count() : 0;
    const size_t selected = s_selected_index;
    const uint64_t configuration = s_overview_configuration;
    if (all && result == ESP_OK) wind_app_status_begin();
    xSemaphoreGive(s_runtime_lock);
    if (result != ESP_OK) return result;

    bool incomplete = false;
    bool attempted = false;
    esp_err_t fetch_result = ESP_OK;
    wind_provider_diagnostics_t diagnostics = {0};
    for (size_t rank = all ? 0 : 1; rank < count; ++rank) {
        if (xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE)
            return ESP_ERR_INVALID_STATE;
        result = ensure_ready();
        if (result != ESP_OK || s_preview_configuration ||
            power_manager_is_installer_active() ||
            wind_spots_count() != count || s_overview_configuration != configuration) {
            xSemaphoreGive(s_runtime_lock);
            return result != ESP_OK ? result : ESP_ERR_INVALID_STATE;
        }
        const int offset = (rank & 1u) ? (int)((rank + 1) / 2) : -(int)(rank / 2);
        const size_t index = wind_spots_offset(selected, offset);
        wind_app_prefetch_spot_t spot;
        wind_app_prefetch_spot_capture(&spot, &s_spots[index],
            index == 0 ? &s_installed_configuration.display
                       : &s_installed_configuration.additional_spots[index - 1].display);
        time_t now;
        time(&now);
        xSemaphoreGive(s_runtime_lock);

        if (!wind_app_prefetch_spot_fetch(&spot, now, force)) incomplete = true;
        if (spot.outcome.attempted_fetch) {
            attempted = true;
            // Keep the first failure instead of overwriting it with a later success.
            if (fetch_result == ESP_OK) {
                fetch_result = spot.outcome.fetch_result;
                open_meteo_knmi_get_diagnostics(&diagnostics);
            }
        }
        outcomes[index] = spot.outcome;

        if (xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE)
            return ESP_ERR_INVALID_STATE;
        result = ensure_ready();
        if (result != ESP_OK || wind_spots_count() != count ||
            s_overview_configuration != configuration) {
            xSemaphoreGive(s_runtime_lock);
            return result != ESP_OK ? result : ESP_ERR_INVALID_STATE;
        }
        wind_spot_runtime_t *runtime = &s_spots[index];
        if (memcmp(&runtime->app.schedule, &spot.original_schedule,
                   sizeof(spot.original_schedule)) == 0) {
            runtime->app.schedule = spot.app.schedule;
            runtime->app.coverage_refresh_attempted = spot.app.coverage_refresh_attempted;
            runtime->app.coverage_refresh_cache_retrieved_at =
                spot.app.coverage_refresh_cache_retrieved_at;
        }
        apply_spot_display(index);
        load_or_refresh_swell(runtime, false, false, now);
        load_or_refresh_tide(runtime, false, false, now);
        apply_spot_display(s_selected_index);
        refresh_render_signatures();
        xSemaphoreGive(s_runtime_lock);
    }
    if (xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    if (s_preview_configuration || s_overview_configuration != configuration ||
        wind_spots_count() != count) {
        xSemaphoreGive(s_runtime_lock);
        return ESP_ERR_INVALID_STATE;
    }
    bool published = false;
    if (all) {
        time_t now;
        time(&now);
        wind_app_outcome_t *outcome = &outcomes[s_selected_index];
        if (overview_open())
            result = render_overview_unlocked(overview_page(), OVERVIEW_INTERACTIVE, false);
        else {
            apply_spot_display(s_selected_index);
            result = wind_app_show_prefetched(&s_spots[s_selected_index].app, now, outcome);
        }
        s_last_render_succeeded = result == ESP_OK &&
            (overview_open() || outcome->freshness != WIND_FRESHNESS_UNAVAILABLE);
        wind_app_status_finish(result, incomplete && fetch_result == ESP_OK ? ESP_FAIL : fetch_result,
                               attempted, s_last_render_succeeded && !incomplete, &diagnostics);
        for (size_t index = 0; index < count; ++index)
            published |= outcomes[index].published_forecast;
    } else if (overview_open()) {
        (void)render_overview_unlocked(overview_page(), OVERVIEW_INTERACTIVE, false);
    }
    xSemaphoreGive(s_runtime_lock);
    if (published) {
        time_t now;
        time(&now);
        (void)wind_analytics_maybe_send(now);
    }
    if (all) return result;
    return incomplete ? ESP_ERR_NOT_FOUND : ESP_OK;
}

static esp_err_t prefetch_spots(bool all, bool force) {
    // Setup recovery and scheduled refreshes share cache files. Serialize their
    // downloads, while leaving the runtime lock available to navigation.
    if (!s_fetch_lock || xSemaphoreTake(s_fetch_lock, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    const esp_err_t result = prefetch_spots_unlocked(all, force);
    xSemaphoreGive(s_fetch_lock);
    return result;
}
#endif

esp_err_t wind_app_prefetch_other_spots(void) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    return prefetch_spots(false, false);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t clear_panel_confirmation_unlocked(void) {
    // A splash or another out-of-band display write replaces the forecast
    // even when the panel cache still describes the previously rendered frame.
    // Keep installer verification tied to what is actually visible.
    s_last_render_succeeded = false;
    // A storage failure must not leave a stale confirmation suppressing the
    // forecast after an out-of-band screen. Keep an in-memory override too.
    // Retain this across runtime reconfiguration until a forecast is displayed.
    s_force_next_display = true;
    if (s_ready)
        for (size_t index = 0; index < wind_spots_count(); ++index)
            s_spots[index].app.force_display = true;
    return wind_cache_panel_invalidate(WIND_PANEL_CACHE_PATH);
}

esp_err_t wind_app_clear_panel_confirmation(void) {
    // Before runtime initialization only the boot task can draw a splash.
    // Once initialized, serialize invalidation with refresh and reconfiguration.
    if (!s_runtime_lock) return clear_panel_confirmation_unlocked();
    if (xSemaphoreTake(s_runtime_lock, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    esp_err_t result = clear_panel_confirmation_unlocked();
    xSemaphoreGive(s_runtime_lock);
    return result;
}

int wind_app_seconds_until_next_wake(void) {
    const bool locked =
        s_runtime_lock && xSemaphoreTake(s_runtime_lock, portMAX_DELAY) == pdTRUE;
    time_t now;
    time(&now);
    int64_t next = wind_schedule_next_boundary(config_manager_get_timezone(), now);
    if (s_ready && s_selected_index < wind_spots_count()) {
        int64_t deadlines[INSTALLED_CONFIGURATION_MAX_SPOTS];
        size_t count=wind_spots_count();
        for (size_t index=0; index<count; ++index)
            deadlines[index]=wind_schedule_next_attempt(&s_spots[index].app.schedule, now);
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
        for (size_t index = 0; index < count; ++index)
            if (deadlines[index] > 0 && deadlines[index] < next) next = deadlines[index];
#else
        next=wind_overview_next_wake(deadlines,count,s_selected_index,
                                     overview_open(),overview_page(),next);
#endif
    }
    if (locked) xSemaphoreGive(s_runtime_lock);
    return next > now ? (int)(next - now) : 1;
}

bool wind_app_last_render_succeeded(void) {
    return s_last_render_succeeded;
}
#else
esp_err_t wind_app_show_setup(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t wind_app_show_failed_setup(void) { return ESP_ERR_NOT_SUPPORTED; }
bool wind_app_spot_requires_network(size_t index) { (void)index; return false; }
bool wind_app_overview_requires_network(int direction) { (void)direction; return false; }
esp_err_t wind_app_toggle_day(size_t day_index) { (void)day_index; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t wind_app_show_overview(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t wind_app_overview_page(int direction) { (void)direction; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t wind_app_select_spot(size_t index) { (void)index; return ESP_ERR_NOT_SUPPORTED; }
void wind_app_overview_state(bool *open,size_t *page) { if (open) *open=false; if (page) *page=0; }
bool wind_app_overview_state_if_ready(bool *open,size_t *page) {
    if (open) *open=false;
    if (page) *page=0;
    return false;
}
esp_err_t wind_app_configure_runtime(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t wind_app_show_battery_empty(void) {
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
esp_err_t wind_app_preview_configuration(const installed_configuration_t *candidate,
                                         wind_provider_diagnostics_t *diagnostics) {
    (void)candidate;
    (void)diagnostics;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t
wind_app_activate_configuration(const installed_configuration_t *configuration) {
    (void)configuration;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
