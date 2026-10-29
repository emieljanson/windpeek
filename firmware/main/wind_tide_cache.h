#pragma once

#include "esp_err.h"
#include "wind_tide.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIND_TIDE_CACHE_SCHEMA_VERSION 3u

typedef struct {
    const char *spot_id;
    const char *timezone;
} wind_tide_cache_identity_t;

esp_err_t wind_tide_cache_store(const char *path, const wind_tide_t *tide);
esp_err_t wind_tide_cache_load(const char *path, const wind_tide_cache_identity_t *identity,
                               wind_tide_t *out_tide);

#ifdef __cplusplus
}
#endif
