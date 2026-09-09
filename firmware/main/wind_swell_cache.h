#pragma once

#include "esp_err.h"
#include "wind_swell.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIND_SWELL_CACHE_SCHEMA_VERSION 2u

typedef struct {
    const char *spot_id;
    const char *timezone;
    const char *model;
} wind_swell_cache_identity_t;

esp_err_t wind_swell_cache_store(const char *path, const wind_swell_t *tide);
esp_err_t wind_swell_cache_load(const char *path, const wind_swell_cache_identity_t *identity,
                               wind_swell_t *out_tide);

#ifdef __cplusplus
}
#endif
