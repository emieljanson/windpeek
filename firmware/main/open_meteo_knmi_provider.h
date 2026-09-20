#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wind_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OPEN_METEO_ENDPOINT "https://api.open-meteo.com/v1/forecast"
#define OPEN_METEO_RESPONSE_LIMIT (16u * 1024u)



void open_meteo_knmi_get_diagnostics(wind_provider_diagnostics_t *out_diagnostics);
#define OPEN_METEO_REQUEST_TIMEOUT_MS 15000

typedef struct {
    const char *spot_id;
    const char *spot_name;
    double latitude;
    double longitude;
    const char *timezone;
    const char *model;
} open_meteo_knmi_config_t;

const char *open_meteo_knmi_endpoint(void);
bool open_meteo_knmi_config_valid(const open_meteo_knmi_config_t *config);
esp_err_t open_meteo_knmi_parse_json(const open_meteo_knmi_config_t *config, const char *json,
                                     size_t length, int64_t retrieved_at, const char *first_date,
                                     wind_forecast_t *out_forecast);
void open_meteo_knmi_provider_init(wind_provider_t *provider, open_meteo_knmi_config_t *config);

#ifdef __cplusplus
}
#endif
