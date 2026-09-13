#include "wind_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    uint32_t magic;
    uint32_t cache_schema;
    uint32_t render_version;
    uint32_t payload_size;
    uint64_t generation;
    uint32_t checksum;
    wind_forecast_t forecast;
} forecast_record_t;

typedef struct {
    int64_t timestamp;
    uint8_t local_hour;
    int16_t wind_knots;
    int16_t gust_knots;
    uint16_t destination_degrees;
} legacy_sample_t;

typedef struct {
    char local_date[WIND_FORECAST_DATE_LENGTH];
    legacy_sample_t samples[WIND_FORECAST_SAMPLES_PER_DAY];
} legacy_day_t;

typedef struct {
    uint32_t schema_version;
    char spot_id[WIND_FORECAST_SPOT_ID_MAX];
    char spot_name[WIND_FORECAST_SPOT_NAME_MAX];
    double latitude;
    double longitude;
    char timezone[WIND_FORECAST_TIMEZONE_MAX];
    char provider[WIND_FORECAST_PROVIDER_MAX];
    char model[WIND_FORECAST_MODEL_MAX];
    int64_t retrieved_at;
    legacy_day_t days[WIND_FORECAST_DAY_COUNT];
} legacy_forecast_t;

typedef struct {
    uint32_t magic;
    uint32_t cache_schema;
    uint32_t render_version;
    uint32_t payload_size;
    uint64_t generation;
    uint32_t checksum;
    legacy_forecast_t forecast;
} legacy_record_t;

typedef struct {
    uint32_t magic;
    uint32_t schema;
    uint64_t render_signature;
    uint64_t hash;
    uint32_t checksum;
} panel_record_t;

static uint32_t checksum_bytes(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *) data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t) -(int32_t) (crc & 1u));
        }
    }
    return ~crc;
}

static uint32_t forecast_record_checksum(const forecast_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    const size_t start = offsetof(forecast_record_t, checksum);
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < sizeof(*record); ++i) {
        crc ^= i >= start && i < start + sizeof(record->checksum) ? 0 : bytes[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1));
    }
    return ~crc;
}

static uint32_t legacy_record_checksum(const legacy_record_t *record)
{
    legacy_record_t copy = *record;
    copy.checksum = 0;
    return checksum_bytes(&copy, sizeof(copy));
}

static bool identity_matches(const wind_forecast_t *forecast,
                             const wind_cache_identity_t *identity)
{
    return !identity || (strcmp(forecast->spot_id, identity->spot_id) == 0 &&
                         strcmp(forecast->timezone, identity->timezone) == 0 &&
                         strcmp(forecast->model, identity->model) == 0);
}

static void migrate_legacy_forecast(const legacy_forecast_t *legacy,
                                    wind_forecast_t *forecast)
{
    wind_forecast_clear(forecast);
    memcpy(forecast->spot_id, legacy->spot_id, sizeof(forecast->spot_id));
    memcpy(forecast->spot_name, legacy->spot_name, sizeof(forecast->spot_name));
    forecast->latitude = legacy->latitude;
    forecast->longitude = legacy->longitude;
    memcpy(forecast->timezone, legacy->timezone, sizeof(forecast->timezone));
    memcpy(forecast->provider, legacy->provider, sizeof(forecast->provider));
    memcpy(forecast->model, legacy->model, sizeof(forecast->model));
    forecast->retrieved_at = legacy->retrieved_at;
    for (size_t day = 0; day < WIND_FORECAST_DAY_COUNT; ++day) {
        memcpy(forecast->days[day].local_date, legacy->days[day].local_date,
               sizeof(forecast->days[day].local_date));
        for (size_t sample = 0; sample < WIND_FORECAST_SAMPLES_PER_DAY; ++sample) {
            const legacy_sample_t *source = &legacy->days[day].samples[sample];
            forecast->days[day].samples[sample] = (wind_forecast_sample_t) {
                .timestamp = source->timestamp,
                .local_hour = source->local_hour,
                .wind_knots = source->wind_knots,
                .gust_knots = source->gust_knots,
                .destination_degrees = source->destination_degrees,
            };
        }
    }
}

static uint32_t panel_record_checksum(const panel_record_t *record)
{
    panel_record_t copy = *record;
    copy.checksum = 0;
    return checksum_bytes(&copy, sizeof(copy));
}

static bool slot_path(char *output, size_t size, const char *path, char slot)
{
    if (!output || !path) {
        return false;
    }
    int written = snprintf(output, size, "%s.%c", path, slot);
    return written > 0 && (size_t) written < size;
}

static esp_err_t read_forecast_record(const char *path, const wind_cache_identity_t *identity,
                                      forecast_record_t *out_record)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return ESP_ERR_NOT_FOUND;
    }
    forecast_record_t record;
    bool ok = fread(&record, 1, sizeof(record), file) == sizeof(record) && fgetc(file) == EOF;
    fclose(file);
    if (ok && record.magic == 0x574E4446u &&
        record.cache_schema == WIND_CACHE_SCHEMA_VERSION &&
        record.render_version == WIND_RENDER_COMPAT_VERSION &&
        record.payload_size == sizeof(record.forecast) && record.generation > 0 &&
        record.checksum == forecast_record_checksum(&record) &&
        wind_forecast_validate(&record.forecast)) {
        if (!identity_matches(&record.forecast, identity)) return ESP_ERR_INVALID_STATE;
        *out_record = record;
        return ESP_OK;
    }

    /* Schema 4 stored the same prefix, before hourly observations were added.
       Validate its original byte length and CRC before migrating in memory. */
    const size_t old_payload = offsetof(wind_forecast_t, hourly);
    const size_t old_size = offsetof(forecast_record_t, forecast) + old_payload;
    memset(&record, 0, sizeof(record));
    file = fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    bool old_ok = fread(&record, 1, old_size, file) == old_size && fgetc(file) == EOF;
    fclose(file);
    uint32_t old_checksum = record.checksum;
    record.checksum = 0;
    if (old_ok && record.magic == 0x574E4446u && record.cache_schema == 4u &&
        record.render_version == WIND_RENDER_COMPAT_VERSION && record.generation > 0 &&
        record.payload_size == old_payload && record.forecast.schema_version == 3u &&
        old_checksum == checksum_bytes(&record, old_size)) {
        record.forecast.schema_version = WIND_FORECAST_SCHEMA_VERSION;
        record.cache_schema = WIND_CACHE_SCHEMA_VERSION;
        record.payload_size = sizeof(record.forecast);
        if (!wind_forecast_validate(&record.forecast)) return ESP_ERR_INVALID_CRC;
        if (!identity_matches(&record.forecast, identity)) return ESP_ERR_INVALID_STATE;
        *out_record = record;
        return ESP_OK;
    }

    file = fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    legacy_record_t legacy;
    bool legacy_ok = fread(&legacy, 1, sizeof(legacy), file) == sizeof(legacy) &&
                     fgetc(file) == EOF;
    fclose(file);
    if (!legacy_ok || legacy.magic != 0x574E4446u || legacy.cache_schema != 1u ||
        legacy.render_version != WIND_RENDER_COMPAT_VERSION ||
        legacy.payload_size != sizeof(legacy.forecast) || legacy.generation == 0 ||
        legacy.checksum != legacy_record_checksum(&legacy) || legacy.forecast.schema_version != 1u)
        return ESP_ERR_INVALID_CRC;
    memset(out_record, 0, sizeof(*out_record));
    out_record->magic = legacy.magic;
    out_record->cache_schema = WIND_CACHE_SCHEMA_VERSION;
    out_record->render_version = legacy.render_version;
    out_record->payload_size = sizeof(out_record->forecast);
    out_record->generation = legacy.generation;
    migrate_legacy_forecast(&legacy.forecast, &out_record->forecast);
    if (!wind_forecast_validate(&out_record->forecast)) return ESP_ERR_INVALID_CRC;
    if (!identity_matches(&out_record->forecast, identity)) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

static esp_err_t write_forecast_slot(const char *path, const forecast_record_t *record)
{
    char temporary[260];
    int written = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (written <= 0 || (size_t) written >= sizeof(temporary)) {
        return ESP_ERR_INVALID_SIZE;
    }
    FILE *file = fopen(temporary, "wb");
    if (!file) {
        return ESP_FAIL;
    }
    bool ok = fwrite(record, 1, sizeof(*record), file) == sizeof(*record) && fflush(file) == 0 &&
              fsync(fileno(file)) == 0;
    if (fclose(file) != 0) {
        ok = false;
    }
    forecast_record_t *verified = malloc(sizeof(*verified));
    if (!verified) { unlink(temporary); return ESP_ERR_NO_MEM; }
    bool valid = ok && read_forecast_record(temporary, NULL, verified) == ESP_OK &&
                 verified->generation == record->generation;
    free(verified);
    if (!valid) {
        unlink(temporary);
        return ESP_FAIL;
    }

    // The other slot remains authoritative while this inactive slot is replaced.
    if (unlink(path) != 0 && access(path, F_OK) == 0) {
        unlink(temporary);
        return ESP_FAIL;
    }
    if (rename(temporary, path) != 0) {
        unlink(temporary);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t atomic_write(const char *path, const void *data, size_t length)
{
    char temporary[256];
    if (!path || snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int) sizeof(temporary)) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *file = fopen(temporary, "wb");
    if (!file) {
        return ESP_FAIL;
    }
    bool ok = fwrite(data, 1, length, file) == length && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok || rename(temporary, path) != 0) {
        unlink(temporary);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t wind_cache_store_with_records(const char *path, const wind_forecast_t *forecast, forecast_record_t *records)
{
    if (!path || !wind_forecast_validate(forecast)) {
        return ESP_ERR_INVALID_ARG;
    }
    char path_a[256];
    char path_b[256];
    if (!slot_path(path_a, sizeof(path_a), path, 'a') ||
        !slot_path(path_b, sizeof(path_b), path, 'b')) {
        return ESP_ERR_INVALID_SIZE;
    }
    forecast_record_t *record_a = &records[0];
    forecast_record_t *record_b = &records[1];
    bool valid_a = read_forecast_record(path_a, NULL, record_a) == ESP_OK;
    bool valid_b = read_forecast_record(path_b, NULL, record_b) == ESP_OK;
    uint64_t latest_generation = 0;
    if (valid_a && record_a->generation > latest_generation) {
        latest_generation = record_a->generation;
    }
    if (valid_b && record_b->generation > latest_generation) {
        latest_generation = record_b->generation;
    }
    if (latest_generation == UINT64_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    const char *inactive = !valid_a ? path_a
                           : !valid_b ? path_b
                           : record_a->generation <= record_b->generation ? path_a
                                                                         : path_b;
    forecast_record_t *record = &records[2];
    record->magic = 0x574E4446u;
    record->cache_schema = WIND_CACHE_SCHEMA_VERSION;
    record->render_version = WIND_RENDER_COMPAT_VERSION;
    record->payload_size = sizeof(*forecast);
    record->generation = latest_generation + 1;
    record->forecast = *forecast;
    record->checksum = forecast_record_checksum(record);
    return write_forecast_slot(inactive, record);
}

esp_err_t wind_cache_store(const char *path, const wind_forecast_t *forecast) {
    forecast_record_t *records = calloc(3, sizeof(*records));
    if (!records) return ESP_ERR_NO_MEM;
    esp_err_t result = wind_cache_store_with_records(path, forecast, records);
    free(records);
    return result;
}

static esp_err_t wind_cache_load_with_records(const char *path, const wind_cache_identity_t *identity,
                          wind_forecast_t *out_forecast, forecast_record_t *records)
{
    if (!path || !identity || !identity->spot_id || !identity->timezone || !identity->model ||
        !out_forecast) {
        return ESP_ERR_INVALID_ARG;
    }
    char path_a[256];
    char path_b[256];
    if (!slot_path(path_a, sizeof(path_a), path, 'a') ||
        !slot_path(path_b, sizeof(path_b), path, 'b')) {
        return ESP_ERR_INVALID_SIZE;
    }
    forecast_record_t *record_a = &records[0];
    forecast_record_t *record_b = &records[1];
    esp_err_t result_a = read_forecast_record(path_a, identity, record_a);
    esp_err_t result_b = read_forecast_record(path_b, identity, record_b);
    bool valid_a = result_a == ESP_OK;
    bool valid_b = result_b == ESP_OK;
    if (!valid_a && !valid_b) {
        if (result_a == ESP_ERR_INVALID_STATE || result_b == ESP_ERR_INVALID_STATE) {
            return ESP_ERR_INVALID_STATE;
        }
        if (result_a == ESP_ERR_NOT_FOUND && result_b == ESP_ERR_NOT_FOUND) {
            return ESP_ERR_NOT_FOUND;
        }
        return ESP_ERR_INVALID_CRC;
    }
    const forecast_record_t *newest = valid_a && valid_b
                                          ? (record_a->generation >= record_b->generation ? record_a
                                                                                       : record_b)
                                          : (valid_a ? record_a : record_b);
    *out_forecast = newest->forecast;
    return ESP_OK;
}

esp_err_t wind_cache_load(const char *path, const wind_cache_identity_t *identity, wind_forecast_t *out_forecast) {
    forecast_record_t *records = calloc(2, sizeof(*records));
    if (!records) return ESP_ERR_NO_MEM;
    esp_err_t result = wind_cache_load_with_records(path, identity, out_forecast, records);
    free(records);
    return result;
}

uint64_t wind_cache_bitmap_hash(const uint8_t *bitmap, size_t length)
{
    if (!bitmap) {
        return 0;
    }
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < length; ++i) {
        hash = (hash ^ bitmap[i]) * 1099511628211ULL;
    }
    return hash;
}

esp_err_t wind_cache_panel_load(const char *path, uint64_t render_signature, uint64_t *out_hash)
{
    if (!path || render_signature == 0 || !out_hash) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return ESP_ERR_NOT_FOUND;
    }
    panel_record_t record;
    bool ok = fread(&record, 1, sizeof(record), file) == sizeof(record) && fgetc(file) == EOF;
    fclose(file);
    if (!ok || record.magic != 0x574E4450u ||
        record.schema != WIND_PANEL_CACHE_SCHEMA_VERSION ||
        record.checksum != panel_record_checksum(&record)) {
        return ESP_ERR_INVALID_CRC;
    }
    if (record.render_signature != render_signature) {
        return ESP_ERR_INVALID_STATE;
    }
    *out_hash = record.hash;
    return ESP_OK;
}

esp_err_t wind_cache_panel_confirm(const char *path, uint64_t render_signature, uint64_t hash)
{
    if (!path || render_signature == 0 || hash == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    panel_record_t record = {.magic = 0x574E4450u,
                             .schema = WIND_PANEL_CACHE_SCHEMA_VERSION,
                             .render_signature = render_signature,
                             .hash = hash};
    record.checksum = panel_record_checksum(&record);
    return atomic_write(path, &record, sizeof(record));
}

esp_err_t wind_cache_panel_invalidate(const char *path)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    if (unlink(path) == 0 || access(path, F_OK) != 0) {
        return ESP_OK;
    }
    return ESP_FAIL;
}
