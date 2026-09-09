#include "wind_swell_cache.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    uint32_t magic;
    uint32_t schema;
    uint32_t payload_size;
    uint64_t generation;
    uint32_t checksum;
    wind_swell_t swell;
} swell_record_t;

static uint32_t checksum(const swell_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *) record;
    const size_t checksum_offset = offsetof(swell_record_t, checksum);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t index = 0; index < sizeof(*record); ++index) {
        const bool checksum_byte = index >= checksum_offset &&
                                   index < checksum_offset + sizeof(record->checksum);
        crc ^= checksum_byte ? 0 : bytes[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t) -(int32_t) (crc & 1u));
        }
    }
    return ~crc;
}

static bool slot_path(char *output, size_t size, const char *path, char slot)
{
    if (!output || !path) return false;
    int written = snprintf(output, size, "%s.%c", path, slot);
    return written > 0 && (size_t) written < size;
}

static esp_err_t read_record(const char *path, const wind_swell_cache_identity_t *identity,
                             swell_record_t *out)
{
    FILE *file = fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    bool ok = fread(out, 1, sizeof(*out), file) == sizeof(*out) && fgetc(file) == EOF;
    fclose(file);
    if (!ok || out->magic != 0x574E4453u || out->schema != WIND_SWELL_CACHE_SCHEMA_VERSION ||
        out->payload_size != sizeof(out->swell) || out->generation == 0 ||
        out->checksum != checksum(out) || !wind_swell_validate(&out->swell)) {
        return ESP_ERR_INVALID_CRC;
    }
    if (identity && (strcmp(out->swell.spot_id, identity->spot_id) != 0 ||
                     strcmp(out->swell.timezone, identity->timezone) != 0 ||
                     !identity->model || strcmp(out->swell.model, identity->model) != 0)) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t write_slot(const char *path, const swell_record_t *record)
{
    char temporary[260];
    int written = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (written <= 0 || (size_t) written >= sizeof(temporary)) return ESP_ERR_INVALID_SIZE;
    FILE *file = fopen(temporary, "wb");
    if (!file) return ESP_FAIL;
    bool ok = fwrite(record, 1, sizeof(*record), file) == sizeof(*record) && fflush(file) == 0 &&
              fsync(fileno(file)) == 0;
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        unlink(temporary);
        return ESP_FAIL;
    }
    swell_record_t *verified = malloc(sizeof(*verified));
    if (!verified) {
        unlink(temporary);
        return ESP_ERR_NO_MEM;
    }
    const bool verified_ok = read_record(temporary, NULL, verified) == ESP_OK &&
                             verified->generation == record->generation;
    free(verified);
    if (!verified_ok) {
        unlink(temporary);
        return ESP_FAIL;
    }
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

esp_err_t wind_swell_cache_store(const char *path, const wind_swell_t *tide)
{
    if (!path || !wind_swell_validate(tide)) return ESP_ERR_INVALID_ARG;
    char a[256], b[256];
    if (!slot_path(a, sizeof(a), path, 'a') || !slot_path(b, sizeof(b), path, 'b')) {
        return ESP_ERR_INVALID_SIZE;
    }
    swell_record_t *record = calloc(1, sizeof(*record));
    if (!record) return ESP_ERR_NO_MEM;
    const bool valid_a = read_record(a, NULL, record) == ESP_OK;
    const uint64_t generation_a = valid_a ? record->generation : 0;
    const bool valid_b = read_record(b, NULL, record) == ESP_OK;
    const uint64_t generation_b = valid_b ? record->generation : 0;
    uint64_t generation = generation_a > generation_b ? generation_a : generation_b;
    if (generation == UINT64_MAX) {
        free(record);
        return ESP_ERR_INVALID_STATE;
    }
    const char *inactive = !valid_a ? a : !valid_b ? b :
        generation_a <= generation_b ? a : b;
    *record = (swell_record_t) {.magic = 0x574E4453u,
                              .schema = WIND_SWELL_CACHE_SCHEMA_VERSION,
                              .payload_size = sizeof(*tide),
                              .generation = generation + 1,
                              .swell = *tide};
    record->checksum = checksum(record);
    const esp_err_t result = write_slot(inactive, record);
    free(record);
    return result;
}

esp_err_t wind_swell_cache_load(const char *path, const wind_swell_cache_identity_t *identity,
                               wind_swell_t *out_tide)
{
    if (!path || !identity || !identity->spot_id || !identity->timezone || !out_tide) {
        return ESP_ERR_INVALID_ARG;
    }
    char a[256], b[256];
    if (!slot_path(a, sizeof(a), path, 'a') || !slot_path(b, sizeof(b), path, 'b')) {
        return ESP_ERR_INVALID_SIZE;
    }
    swell_record_t *record = malloc(sizeof(*record));
    if (!record) return ESP_ERR_NO_MEM;
    const esp_err_t result_a = read_record(a, identity, record);
    const bool valid_a = result_a == ESP_OK;
    const uint64_t generation_a = valid_a ? record->generation : 0;
    if (valid_a) *out_tide = record->swell;
    const esp_err_t result_b = read_record(b, identity, record);
    const bool valid_b = result_b == ESP_OK;
    const uint64_t generation_b = valid_b ? record->generation : 0;
    if (valid_b && (!valid_a || generation_b > generation_a)) *out_tide = record->swell;
    free(record);
    if (!valid_a && !valid_b) {
        if (result_a == ESP_ERR_INVALID_STATE || result_b == ESP_ERR_INVALID_STATE) {
            return ESP_ERR_INVALID_STATE;
        }
        if (result_a == ESP_ERR_NOT_FOUND && result_b == ESP_ERR_NOT_FOUND) {
            return ESP_ERR_NOT_FOUND;
        }
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}
