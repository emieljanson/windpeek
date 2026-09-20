#pragma once

#include "wind_app.h"

// Shared by the portable refresh cycle and device runtime's cache selection.
wind_freshness_t wind_app_forecast_freshness(const wind_forecast_t *forecast, int64_t now);
bool wind_app_forecast_covers_window(const wind_forecast_t *forecast,
                                    const char *timezone, int64_t now);
