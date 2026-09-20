#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "wind_cache.h"
#include "wind_provider.h"
#include "wind_schedule.h"
#include "installed_configuration.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wind_app_show_battery_empty(void);
esp_err_t wind_app_show_setup(void);

typedef enum {
    WIND_FRESHNESS_UNAVAILABLE = 0,
    WIND_FRESHNESS_FRESH,
    WIND_FRESHNESS_AGED,
    WIND_FRESHNESS_STALE,
} wind_freshness_t;

typedef esp_err_t (*wind_app_render_fn)(void *context, const wind_forecast_t *forecast,
                                        wind_freshness_t freshness, bool refresh_failed, int64_t now,
                                        uint8_t *bitmap, size_t bitmap_size);
typedef esp_err_t (*wind_app_display_fn)(void *context, const uint8_t *bitmap, size_t bitmap_size);

typedef struct {
    wind_provider_t provider;
    wind_cache_identity_t identity;
    const char *forecast_cache_path;
    const char *panel_cache_path;
    const char *schedule_path;
    uint64_t render_signature;
    size_t bitmap_size;
    wind_app_render_fn render;
    wind_app_display_fn display;
    void *io_context;
} wind_app_config_t;

typedef struct {
    wind_app_config_t config;
    wind_schedule_state_t schedule;
    int64_t coverage_refresh_cache_retrieved_at;
    bool coverage_refresh_attempted;
    bool initialized;
    bool force_display;
} wind_app_t;

typedef struct {
    bool attempted_fetch;
    bool published_forecast;
    bool used_cache;
    bool displayed;
    bool display_unchanged;
    wind_freshness_t freshness;
    esp_err_t fetch_result;
} wind_app_outcome_t;

esp_err_t wind_app_init(wind_app_t *app, const wind_app_config_t *config);
esp_err_t wind_app_run(wind_app_t *app, bool force_refresh, int64_t now,
                       wind_app_outcome_t *outcome);
// Requires a newly fetched forecast and confirmed panel output.
esp_err_t wind_app_run_setup(wind_app_t *app, int64_t now, wind_app_outcome_t *outcome);
esp_err_t wind_app_prefetch(wind_app_t *app, bool force_refresh, int64_t now,
                            wind_app_outcome_t *outcome);
esp_err_t wind_app_show_cached(wind_app_t *app, int64_t now, wind_app_outcome_t *outcome);
esp_err_t wind_app_configure_runtime(void);
esp_err_t wind_app_start(void);
esp_err_t wind_app_refresh(bool force_refresh);
esp_err_t wind_app_show_overview(void);
esp_err_t wind_app_toggle_day(size_t day_index);
esp_err_t wind_app_overview_page(int direction);
esp_err_t wind_app_select_spot(size_t index);
void wind_app_overview_state(bool *open, size_t *page);
esp_err_t wind_app_select_previous(void);
esp_err_t wind_app_select_next(void);
esp_err_t wind_app_select_next_display_mode(void);
bool wind_app_navigation_requires_network(int direction);
bool wind_app_spot_requires_network(size_t index);
bool wind_app_overview_requires_network(int page_direction);
esp_err_t wind_app_clear_panel_confirmation(void);
int wind_app_seconds_until_next_wake(void);
esp_err_t wind_app_preview_configuration(const installed_configuration_t *candidate,
                                         wind_provider_diagnostics_t *diagnostics);
esp_err_t wind_app_activate_configuration(const installed_configuration_t *configuration);
bool wind_app_last_render_succeeded(void);

#ifdef __cplusplus
}
#endif
