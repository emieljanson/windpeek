#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "wind_forecast.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIND_TIDE_SCHEMA_VERSION 3u
#define WIND_TIDE_MIN_SAMPLES 119u
#define WIND_TIDE_MAX_SAMPLES 121u
#define WIND_TIDE_MAX_EXTREMA 32u
#define WIND_TIDE_PROVIDER_MAX 32

typedef enum {
    WIND_TIDE_UNSUPPORTED = 0,
    WIND_TIDE_AVAILABLE = 1,
} wind_tide_capability_t;

typedef struct {
    int64_t timestamp;
    char local_date[WIND_FORECAST_DATE_LENGTH];
    uint8_t local_hour;
    int32_t sea_level_mm;
} wind_tide_sample_t;

typedef struct {
    int64_t timestamp;
    char local_date[WIND_FORECAST_DATE_LENGTH];
    uint8_t local_hour;
    uint8_t local_minute;
    uint8_t is_high;
    int32_t sea_level_mm;
} wind_tide_extremum_t;

typedef struct {
    uint32_t schema_version;
    char spot_id[WIND_FORECAST_SPOT_ID_MAX];
    char timezone[WIND_FORECAST_TIMEZONE_MAX];
    char provider[WIND_TIDE_PROVIDER_MAX];
    int64_t retrieved_at;
    uint8_t capability;
    uint16_t sample_count;
    wind_tide_sample_t samples[WIND_TIDE_MAX_SAMPLES];
    uint8_t extremum_count;
    wind_tide_extremum_t extrema[WIND_TIDE_MAX_EXTREMA];
} wind_tide_t;

typedef esp_err_t (*wind_tide_fetch_fn)(void *context, int64_t retrieved_at,
                                        wind_tide_t *out_tide);

typedef struct {
    wind_tide_fetch_fn fetch;
    void *context;
} wind_tide_provider_t;

void wind_tide_clear(wind_tide_t *tide);
bool wind_tide_validate(const wind_tide_t *tide);

#ifdef __cplusplus
}
#endif
