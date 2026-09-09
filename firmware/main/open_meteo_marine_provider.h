#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wind_tide.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OPEN_METEO_MARINE_ENDPOINT "https://marine-api.open-meteo.com/v1/marine"
#define OPEN_METEO_MARINE_RESPONSE_LIMIT (16u * 1024u)
#define OPEN_METEO_MARINE_TIMEOUT_MS 15000

typedef struct {
    const char *spot_id;
    double latitude;
    double longitude;
    const char *timezone;
    const char *swell_model;
} open_meteo_marine_config_t;

bool open_meteo_marine_config_valid(const open_meteo_marine_config_t *config);
esp_err_t open_meteo_marine_parse_json(const open_meteo_marine_config_t *config, const char *json,
                                       size_t length, int64_t retrieved_at,
                                       wind_tide_t *out_tide);
void open_meteo_marine_provider_init(wind_tide_provider_t *provider,
                                     open_meteo_marine_config_t *config);

#ifdef __cplusplus
}
#endif
