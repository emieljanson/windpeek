#pragma once

#include "wind_app.h"
#include "open_meteo_marine_provider.h"
#include "wind_spots.h"
#include "wind_swell.h"
#include "wind_tide.h"

typedef struct {
    wind_app_t app;
    open_meteo_knmi_config_t provider_config;
    open_meteo_marine_config_t marine_config;
    wind_tide_provider_t tide_provider;
    wind_tide_t tide;
    bool have_tide;
    wind_swell_t swell;
    bool have_swell;
    bool swell_failed;
    const wind_spot_t *spot;
    const char *device_timezone;
    char forecast_path[96];
    char schedule_path[96];
    char tide_path[96];
} wind_spot_runtime_t;
