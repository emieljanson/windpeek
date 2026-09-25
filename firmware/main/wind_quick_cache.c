#include "wind_quick_cache.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "epaper.h"
#include "config.h"
#include "board_hal.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "wind_cache.h"
#include "wind_app_internal.h"
#include "wind_overview.h"
#include "wind_timezone.h"

static const char *TAG = "wind_quick_cache";
static atomic_uint s_temporary_sequence;

static gzFile open_packed(const char *path, const char *mode) {
    gzFile file = gzopen(path, mode);
    // zlib allocates three I/O buffers' worth of memory. Its 8 KiB default
    // consumes 24 KiB of preferred internal RAM alongside Wi-Fi/TLS.
    if (file && gzbuffer(file, 1024) != 0) {
        gzclose(file);
        return NULL;
    }
    return file;
}

static bool write_packed(const char *path, const void *header, size_t header_size,
                         const uint8_t *bitmap, size_t bitmap_size,
                         char *temporary, size_t temporary_capacity) {
    if (!path || !header || !bitmap || !temporary ||
        bitmap_size != WIND_RENDERER_E1003_COMPOSITION_BYTES ||
        snprintf(temporary, temporary_capacity, "%s.tmp.%08x", path,
                 atomic_fetch_add(&s_temporary_sequence, 1)) >= (int)temporary_capacity)
        return false;
    gzFile file = open_packed(temporary, "wb1");
    bool saved = file && gzwrite(file, header, (unsigned)header_size) == (int)header_size;
    uint8_t row[WIND_RENDERER_E1003_WIDTH / 2];
    for (int y = 0; saved && y < WIND_RENDERER_E1003_HEIGHT; ++y) {
        const uint8_t *source = bitmap + (size_t)y * WIND_RENDERER_E1003_WIDTH;
        for (size_t x = 0; x < sizeof(row); ++x) {
            const uint8_t left = source[x * 2];
            const uint8_t right = source[x * 2 + 1];
            if (left > 15 || right > 15) { saved = false; break; }
            row[x] = (uint8_t)((left << 4) | right);
        }
        if (saved && gzwrite(file, row, sizeof(row)) != (int)sizeof(row)) saved = false;
    }
    if (file && gzclose(file) != Z_OK) saved = false;
    if (!saved) remove(temporary);
    return saved;
}

static bool save_packed(const char *path, const void *header, size_t header_size,
                        const uint8_t *bitmap, size_t bitmap_size) {
    char temporary[160] = {0};
    if (!write_packed(path, header, header_size, bitmap, bitmap_size,
                      temporary, sizeof(temporary))) return false;
    if (rename(temporary, path) == 0) return true;
    remove(temporary);
    return false;
}

static uint8_t *read_packed(const char *path, void *header, size_t header_size) {
    gzFile file = open_packed(path, "rb");
    if (!file) return NULL;
    uint8_t *packed = heap_caps_malloc(WIND_QUICK_FRAME_BYTES,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const bool valid = packed &&
        gzread(file, header, (unsigned)header_size) == (int)header_size &&
        gzread(file, packed, WIND_QUICK_FRAME_BYTES) == WIND_QUICK_FRAME_BYTES &&
        gzgetc(file) == -1 && gzeof(file);
    const int closed = gzclose(file);
    if (!valid || closed != Z_OK) { free(packed); return NULL; }
    return packed;
}

static bool read_header(const char *path, void *header, size_t header_size) {
    gzFile file = open_packed(path, "rb");
    if (!file) return false;
    const bool valid = gzread(file, header, (unsigned)header_size) == (int)header_size;
    gzclose(file);
    return valid;
}

bool wind_quick_spot_capture(const wind_spot_runtime_t *runtime,
                             const wind_forecast_t *forecast, const char *focused_date,
                             uint64_t configuration_digest, time_t now, int battery_percent,
                             wind_quick_spot_header_t *header) {
    if (!runtime || !forecast || !focused_date || !header) return false;
    memset(header, 0, sizeof(*header));
    header->magic = UINT32_C(0x57514631);
    header->version = 9;
    header->render_signature = runtime->app.config.render_signature;
    header->configuration_digest = configuration_digest;
    header->forecast_at = forecast->retrieved_at;
    header->swell_at = runtime->have_swell ? runtime->swell.retrieved_at : 0;
    header->tide_at = runtime->have_tide ? runtime->tide.retrieved_at : 0;
    header->forecast_hash = wind_cache_bitmap_hash((const uint8_t *)forecast,
                                                    sizeof(*forecast));
    header->swell_hash = runtime->have_swell
        ? wind_cache_bitmap_hash((const uint8_t *)&runtime->swell,
                                 sizeof(runtime->swell)) : 0;
    header->tide_hash = runtime->have_tide
        ? wind_cache_bitmap_hash((const uint8_t *)&runtime->tide,
                                 sizeof(runtime->tide)) : 0;
    snprintf(header->spot_id, sizeof(header->spot_id), "%s", runtime->spot->id);
    snprintf(header->focused_date, sizeof(header->focused_date), "%s", focused_date);
    snprintf(header->first_date, sizeof(header->first_date), "%s",
             forecast->days[0].local_date);
    header->freshness = wind_app_forecast_freshness(forecast, now);
    header->age_hours = now > forecast->retrieved_at ? (now - forecast->retrieved_at) / 3600 : 0;
    header->swell_age_hours = runtime->have_swell && now > runtime->swell.retrieved_at
        ? (now - runtime->swell.retrieved_at) / 3600 : 0;
    header->swell_from_future = runtime->have_swell && runtime->swell.retrieved_at > now + 300;
    header->battery_percent = battery_percent;
    wind_local_datetime_t local;
    if (wind_timezone_from_unix(runtime->spot->timezone, now, &local) != ESP_OK ||
        wind_timezone_format_date(&local, header->local_date, sizeof(header->local_date)) != ESP_OK) {
        return false;
    }
    return true;
}

bool wind_quick_spot_identity(const wind_spot_runtime_t *runtime,
                              const char *focused_date, uint64_t configuration_digest,
                              wind_quick_spot_header_t *header) {
    if (!runtime || !focused_date || !header) return false;
    wind_forecast_t *forecast = malloc(sizeof(*forecast));
    if (!forecast) return false;
    time_t now;
    time(&now);
    const bool available = wind_cache_load(runtime->forecast_path,
        &runtime->app.config.identity, forecast) == ESP_OK &&
        wind_quick_spot_capture(runtime, forecast, focused_date, configuration_digest,
                                now, board_hal_get_battery_percent(), header);
    free(forecast);
    return available;
}

bool wind_quick_spot_path(const wind_spot_runtime_t *runtime,
                          const char *focused_date, char *path, size_t capacity) {
    if (!runtime || !focused_date || !path || !capacity) return false;
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char *p = (const unsigned char *)focused_date; *p; ++p)
        hash = (hash ^ *p) * UINT32_C(16777619);
    const int length = snprintf(path, capacity, "%s.quick.%08lx",
                                runtime->forecast_path, (unsigned long)hash);
    return length >= 0 && length < (int)capacity;
}

bool wind_quick_spot_cached(const wind_spot_runtime_t *runtime,
                            const char *focused_date, uint64_t configuration_digest) {
    wind_quick_spot_header_t expected, stored;
    char path[128];
    if (!wind_quick_spot_identity(runtime, focused_date, configuration_digest, &expected) ||
        !wind_quick_spot_path(runtime, focused_date, path, sizeof(path)) ||
        !read_header(path, &stored, sizeof(stored))) return false;
    return memcmp(&stored, &expected, sizeof(stored)) == 0;
}

bool wind_quick_spot_show(const wind_spot_runtime_t *runtime,
                          const char *focused_date, uint64_t configuration_digest) {
    const int64_t started = esp_timer_get_time();
    wind_quick_spot_header_t expected, stored = {0};
    char path[128];
    if (!wind_quick_spot_identity(runtime, focused_date, configuration_digest, &expected) ||
        !wind_quick_spot_path(runtime, focused_date, path, sizeof(path))) return false;
    uint8_t *packed = read_packed(path, &stored, sizeof(stored));
    if (!packed) return false;
    const bool exact = memcmp(&stored, &expected, sizeof(stored)) == 0;
    const bool shown = exact && epaper_display(packed) == ESP_OK;
    free(packed);
    if (shown) {
        ESP_LOGI(TAG, "Quick frame for %s including cache read: %lld ms", runtime->spot->id,
                 (long long)((esp_timer_get_time() - started) / 1000));
    }
    return shown;
}

void wind_quick_spot_save(const wind_spot_runtime_t *runtime,
                          const wind_quick_spot_header_t *header,
                          const uint8_t *bitmap, size_t bitmap_size) {
    if (!header) return;
    char path[128];
    if (!wind_quick_spot_path(runtime, header->focused_date, path, sizeof(path))) return;
    // The caller already rendered. Replace even a matching header: the old
    // compressed payload may be truncated or may contain a previous failure.
    (void)save_packed(path, header, sizeof(*header), bitmap, bitmap_size);
}

bool wind_quick_spot_stage(const char *path, const wind_quick_spot_header_t *header,
                           const uint8_t *bitmap, size_t bitmap_size,
                           char *temporary, size_t temporary_capacity) {
    return write_packed(path, header, sizeof(*header), bitmap, bitmap_size,
                        temporary, temporary_capacity);
}

static uint64_t hash_file(uint64_t hash, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return (hash ^ UINT64_C(0x9E3779B97F4A7C15)) * UINT64_C(1099511628211);
    uint8_t stamp[32];
    const size_t size = fread(stamp, 1, sizeof(stamp), file);
    fclose(file);
    hash = (hash ^ size) * UINT64_C(1099511628211);
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ stamp[i]) * UINT64_C(1099511628211);
    return hash;
}

static uint64_t hash_slots(uint64_t hash, const char *base) {
    char path[160];
    for (char slot = 'a'; slot <= 'b'; ++slot) {
        if (snprintf(path, sizeof(path), "%s.%c", base, slot) >= (int)sizeof(path))
            return 0;
        hash = hash_file(hash, path);
    }
    return hash;
}

wind_quick_overview_header_t wind_quick_overview_identity(
    const wind_spot_runtime_t *spots, size_t spot_count,
    uint64_t configuration_digest, size_t page, time_t now) {
    wind_quick_overview_header_t header = {
        .magic = UINT32_C(0x57514F31), .version = 7, .page = (uint32_t)page,
    };
    uint64_t hash = configuration_digest;
    const size_t first = page * WIND_OVERVIEW_PAGE_SIZE;
    for (size_t index = first; index < spot_count &&
                                 index < first + WIND_OVERVIEW_PAGE_SIZE; ++index) {
        const wind_spot_runtime_t *runtime = &spots[index];
        wind_local_datetime_t local;
        if (wind_timezone_from_unix(runtime->spot->timezone, now, &local) == ESP_OK) {
            hash = (hash ^ (uint64_t)local.year) * UINT64_C(1099511628211);
            hash = (hash ^ (uint64_t)local.month) * UINT64_C(1099511628211);
            hash = (hash ^ (uint64_t)local.day) * UINT64_C(1099511628211);
        }
    }
    for (size_t index = first; index < spot_count &&
                                 index < first + WIND_OVERVIEW_PAGE_SIZE; ++index) {
        const wind_spot_runtime_t *runtime = &spots[index];
        hash = hash_slots(hash, runtime->forecast_path);
        char swell_path[128];
        snprintf(swell_path, sizeof(swell_path), "%s.swell", runtime->forecast_path);
        hash = hash_slots(hash, swell_path);
        hash = hash_slots(hash, runtime->tide_path);
    }
    header.source_hash = hash;
    return header;
}

static bool overview_path(size_t page, char *path, size_t capacity) {
    const int length = snprintf(path, capacity, FS_MOUNT_POINT "/overview-%u.quick", (unsigned)page);
    return length >= 0 && length < (int)capacity;
}

static bool overview_matches(const wind_quick_overview_header_t *actual,
                             const wind_quick_overview_header_t *expected) {
    return actual->magic == expected->magic && actual->version == expected->version &&
        actual->page == expected->page && actual->source_hash == expected->source_hash;
}

bool wind_quick_overview_cached(const wind_spot_runtime_t *spots, size_t spot_count,
                                uint64_t configuration_digest, size_t page, time_t now) {
    char path[64];
    wind_quick_overview_header_t actual;
    if (!overview_path(page, path, sizeof(path)) ||
        !read_header(path, &actual, sizeof(actual))) return false;
    const wind_quick_overview_header_t expected = wind_quick_overview_identity(
        spots, spot_count, configuration_digest, page, now);
    return overview_matches(&actual, &expected);
}

bool wind_quick_overview_show(const wind_spot_runtime_t *spots, size_t spot_count,
                              uint64_t configuration_digest, size_t page, time_t now) {
    const int64_t started = esp_timer_get_time();
    char path[64];
    if (!overview_path(page, path, sizeof(path))) return false;
    wind_quick_overview_header_t actual = {0};
    uint8_t *packed = read_packed(path, &actual, sizeof(actual));
    if (!packed) return false;
    const wind_quick_overview_header_t expected = wind_quick_overview_identity(
        spots, spot_count, configuration_digest, page, now);
    const bool shown = overview_matches(&actual, &expected) && epaper_display(packed) == ESP_OK;
    free(packed);
    if (shown) {
        ESP_LOGI(TAG, "Quick overview %u including cache read: %lld ms",
                 (unsigned)page, (long long)((esp_timer_get_time() - started) / 1000));
    }
    return shown;
}

void wind_quick_overview_save(const wind_quick_overview_header_t *header,
                              const uint8_t *bitmap) {
    char path[64];
    if (!header || !overview_path(header->page, path, sizeof(path))) return;
    (void)save_packed(path, header, sizeof(*header), bitmap,
                      WIND_RENDERER_E1003_COMPOSITION_BYTES);
}
