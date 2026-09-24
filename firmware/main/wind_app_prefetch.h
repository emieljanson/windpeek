#pragma once

#include <time.h>

#include "wind_app_runtime.h"

/* A self-contained copy of one spot's fetch configuration. Capture while the
 * runtime lock is held; network requests can then run without blocking input. */
typedef struct {
    wind_app_t app;
    wind_schedule_state_t original_schedule;
    open_meteo_knmi_config_t provider;
    open_meteo_marine_config_t marine;
    char spot_id[65];
    char spot_name[65];
    char timezone[64];
    char model[32];
    char swell_model[32];
    char forecast_path[96];
    char schedule_path[96];
    char tide_path[96];
    bool show_swell;
    bool show_tide;
} wind_app_prefetch_spot_t;

void wind_app_prefetch_spot_capture(wind_app_prefetch_spot_t *snapshot,
                                    const wind_spot_runtime_t *runtime,
                                    const installed_display_configuration_t *display);
bool wind_app_prefetch_spot_fetch(wind_app_prefetch_spot_t *snapshot, time_t now);
