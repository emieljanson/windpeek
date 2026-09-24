#ifndef WIND_DASHBOARD_DATA_H
#define WIND_DASHBOARD_DATA_H

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "wind_forecast.h"
#include "wind_renderer.h"
#include "wind_swell.h"

const char *wind_dashboard_day_name(int weekday);
wind_renderer_sample_t wind_dashboard_forecast_sample(const wind_forecast_sample_t *source,
                                                       const char *time, bool available);
wind_renderer_swell_sample_t wind_dashboard_swell_sample(const wind_swell_sample_t *source,
                                                         bool secondary);
esp_err_t wind_dashboard_build_overview_row(const char *spot_name, const char *timezone,
                                            const wind_forecast_t *forecast,
                                            const wind_swell_t *swell, bool swell_primary,
                                            time_t now, wind_renderer_dashboard_t *out);

#endif
