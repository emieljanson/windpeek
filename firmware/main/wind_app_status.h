#pragma once

#include "wind_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

// Stable numeric stages for diagnostics, independent of the USB apply worker.
typedef enum {
    WIND_REFRESH_IDLE = 0,
    WIND_REFRESH_PREPARING = 1,
    WIND_REFRESH_SWELL = 2,
    WIND_REFRESH_TIDE = 3,
    WIND_REFRESH_FORECAST = 4,
    WIND_REFRESH_COMPLETE = 5,
    WIND_REFRESH_FAILED = 6,
} wind_refresh_stage_t;

typedef struct {
    wind_refresh_stage_t stage;
    esp_err_t result;
    esp_err_t fetch_result;
    bool attempted_fetch;
    wind_provider_diagnostics_t forecast;
} wind_app_status_t;

void wind_app_status_begin(void);
void wind_app_status_stage(wind_refresh_stage_t stage);
void wind_app_status_finish(esp_err_t result, esp_err_t fetch_result,
                            bool attempted_fetch, bool render_valid,
                            const wind_provider_diagnostics_t *forecast);
void wind_app_status_get(wind_app_status_t *out);

#ifdef __cplusplus
}
#endif
