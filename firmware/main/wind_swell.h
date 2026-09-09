#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "open_meteo_marine_provider.h"
#ifdef __cplusplus
extern "C" {
#endif
#define WIND_SWELL_MAX_SAMPLES 122
typedef struct {
    int64_t timestamp;
    int16_t height_cm;
    int16_t period_tenths;
    int16_t destination_degrees;
    int16_t secondary_height_cm;
    int16_t secondary_period_tenths;
    int16_t secondary_destination_degrees;
} wind_swell_sample_t;
typedef struct {
    char spot_id[65];
    char timezone[64];
    char model[32];
    int64_t retrieved_at;
    size_t sample_count;
    wind_swell_sample_t samples[WIND_SWELL_MAX_SAMPLES];
} wind_swell_t;
const char *wind_swell_preferred_model(const char *model, double latitude);
const char *wind_swell_base_model(const char *model);
void wind_swell_overlay(wind_swell_t *base, const wind_swell_t *preferred);
bool wind_swell_validate(const wind_swell_t *swell);
esp_err_t wind_swell_parse(const open_meteo_marine_config_t *config, const char *json, size_t length, int64_t now, wind_swell_t *out);
esp_err_t wind_swell_fetch(const open_meteo_marine_config_t *config, int64_t now, wind_swell_t *out);
#ifdef __cplusplus
}
#endif
