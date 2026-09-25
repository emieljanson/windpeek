#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "wind_app_runtime.h"
#include "wind_renderer.h"

enum { WIND_QUICK_FRAME_BYTES = WIND_RENDERER_E1003_COMPOSITION_BYTES / 2 };

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t render_signature;
    int64_t forecast_at;
    int64_t swell_at;
    int64_t tide_at;
    uint64_t forecast_hash;
    uint64_t swell_hash;
    uint64_t tide_hash;
    uint64_t configuration_digest;
    int64_t age_hours;
    int64_t swell_age_hours;
    int32_t freshness;
    int32_t battery_percent;
    int32_t swell_from_future;
    char spot_id[48];
    char focused_date[WIND_FORECAST_DATE_LENGTH];
    char first_date[WIND_FORECAST_DATE_LENGTH];
    char local_date[WIND_FORECAST_DATE_LENGTH];
} wind_quick_spot_header_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t source_hash;
    uint32_t page;
} wind_quick_overview_header_t;

bool wind_quick_spot_identity(const wind_spot_runtime_t *runtime,
                              const char *focused_date, uint64_t configuration_digest,
                              wind_quick_spot_header_t *header);
bool wind_quick_spot_capture(const wind_spot_runtime_t *runtime,
                             const wind_forecast_t *forecast, const char *focused_date,
                             uint64_t configuration_digest, time_t now, int battery_percent,
                             wind_quick_spot_header_t *header);
bool wind_quick_spot_path(const wind_spot_runtime_t *runtime,
                          const char *focused_date, char *path, size_t capacity);
bool wind_quick_spot_cached(const wind_spot_runtime_t *runtime,
                            const char *focused_date, uint64_t configuration_digest);
bool wind_quick_spot_show(const wind_spot_runtime_t *runtime,
                          const char *focused_date, uint64_t configuration_digest);
void wind_quick_spot_save(const wind_spot_runtime_t *runtime,
                          const wind_quick_spot_header_t *header,
                          const uint8_t *bitmap, size_t bitmap_size);
bool wind_quick_spot_stage(const char *path, const wind_quick_spot_header_t *header,
                           const uint8_t *bitmap, size_t bitmap_size,
                           char *temporary, size_t temporary_capacity);

wind_quick_overview_header_t wind_quick_overview_identity(
    const wind_spot_runtime_t *spots, size_t spot_count,
    uint64_t configuration_digest, size_t page, time_t now);
bool wind_quick_overview_cached(const wind_spot_runtime_t *spots, size_t spot_count,
                                uint64_t configuration_digest, size_t page, time_t now);
bool wind_quick_overview_show(const wind_spot_runtime_t *spots, size_t spot_count,
                              uint64_t configuration_digest, size_t page, time_t now);
void wind_quick_overview_save(const wind_quick_overview_header_t *header,
                              const uint8_t *bitmap);
