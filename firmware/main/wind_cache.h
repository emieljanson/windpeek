#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "wind_forecast.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIND_CACHE_SCHEMA_VERSION 5u
#define WIND_RENDER_COMPAT_VERSION 2u
#define WIND_PANEL_CACHE_SCHEMA_VERSION 2u

typedef struct {
    const char *spot_id;
    const char *timezone;
    const char *model;
} wind_cache_identity_t;

esp_err_t wind_cache_load(const char *path, const wind_cache_identity_t *identity,
                          wind_forecast_t *out_forecast);
esp_err_t wind_cache_store(const char *path, const wind_forecast_t *forecast);
uint64_t wind_cache_bitmap_hash(const uint8_t *bitmap, size_t length);
esp_err_t wind_cache_panel_load(const char *path, uint64_t render_signature, uint64_t *out_hash);
esp_err_t wind_cache_panel_confirm(const char *path, uint64_t render_signature, uint64_t hash);
esp_err_t wind_cache_panel_invalidate(const char *path);

#ifdef __cplusplus
}
#endif
