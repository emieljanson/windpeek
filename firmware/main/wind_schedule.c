#include "wind_schedule.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "wind_timezone.h"

#ifdef ESP_PLATFORM
#include "wind_config.h"
#endif

static const int BOUNDARY_MINUTES[WIND_SCHEDULE_BOUNDARY_COUNT] = {5, 7 * 60, 11 * 60, 15 * 60,
                                                                  19 * 60};

typedef struct {
    uint32_t magic;
    wind_schedule_state_t state;
    uint32_t checksum;
} schedule_record_t;

typedef struct {
    uint32_t magic;
    struct {
        uint32_t schema_version;
        char spot_id[32];
        char timezone[40];
        int64_t last_attempted_boundary;
        int64_t last_satisfied_boundary;
        int64_t retry_boundary;
        int64_t retry_at;
        int64_t final_retry_at;
    } state;
    uint32_t checksum;
} previous_schedule_record_t;

static uint32_t checksum_bytes(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *) data;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

static esp_err_t initialize_scope(wind_schedule_state_t *state, const char *spot_id,
                                  const char *timezone)
{
    if (!state || !spot_id || !timezone || spot_id[0] == '\0' || timezone[0] == '\0' ||
        strlen(spot_id) >= sizeof(state->spot_id) || strlen(timezone) >= sizeof(state->timezone)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(state, 0, sizeof(*state));
    state->schema_version = WIND_SCHEDULE_SCHEMA_VERSION;
    memcpy(state->spot_id, spot_id, strlen(spot_id) + 1);
    memcpy(state->timezone, timezone, strlen(timezone) + 1);
    return ESP_OK;
}

esp_err_t wind_schedule_state_set_scope(wind_schedule_state_t *state, const char *spot_id,
                                        const char *timezone)
{
    return initialize_scope(state, spot_id, timezone);
}

bool wind_schedule_state_matches_scope(const wind_schedule_state_t *state, const char *spot_id,
                                       const char *timezone)
{
    return state && spot_id && timezone &&
           state->schema_version == WIND_SCHEDULE_SCHEMA_VERSION &&
           strcmp(state->spot_id, spot_id) == 0 && strcmp(state->timezone, timezone) == 0;
}

static int64_t boundary_for_day(const char *timezone, const wind_local_datetime_t *day,
                                int minutes)
{
    wind_local_datetime_t value = *day;
    value.hour = (uint8_t) (minutes / 60);
    value.minute = (uint8_t) (minutes % 60);
    value.second = 0;
    int64_t unix_seconds = 0;
    return wind_timezone_to_unix(timezone, &value, &unix_seconds) == ESP_OK ? unix_seconds : 0;
}

int64_t wind_schedule_latest_boundary(const char *timezone, time_t now)
{
    wind_local_datetime_t local;
    if (wind_timezone_from_unix(timezone, now, &local) != ESP_OK) return 0;
    int64_t latest = 0;
    for (int i = 0; i < WIND_SCHEDULE_BOUNDARY_COUNT; ++i) {
        int64_t candidate = boundary_for_day(timezone, &local, BOUNDARY_MINUTES[i]);
        if (candidate <= now && candidate > latest) {
            latest = candidate;
        }
    }
    if (latest == 0) {
        if (wind_timezone_shift_date(&local, -1) != ESP_OK) return 0;
        latest = boundary_for_day(timezone, &local,
                                  BOUNDARY_MINUTES[WIND_SCHEDULE_BOUNDARY_COUNT - 1]);
    }
    return latest;
}

int64_t wind_schedule_next_boundary(const char *timezone, time_t now)
{
    wind_local_datetime_t local;
    if (wind_timezone_from_unix(timezone, now, &local) != ESP_OK) return 0;
    for (int i = 0; i < WIND_SCHEDULE_BOUNDARY_COUNT; ++i) {
        int64_t candidate = boundary_for_day(timezone, &local, BOUNDARY_MINUTES[i]);
        if (candidate > now) {
            return candidate;
        }
    }
    if (wind_timezone_shift_date(&local, 1) != ESP_OK) return 0;
    return boundary_for_day(timezone, &local, BOUNDARY_MINUTES[0]);
}

int64_t wind_schedule_next_attempt(const wind_schedule_state_t *state, time_t now)
{
    if (!state || state->schema_version != WIND_SCHEDULE_SCHEMA_VERSION ||
        state->timezone[0] == '\0') {
        return 0;
    }
    int64_t next = wind_schedule_next_boundary(state->timezone, now);
    if (state && state->retry_at > 0 && state->retry_at < next) {
        return state->retry_at > now ? state->retry_at : now;
    }
    return next;
}

bool wind_schedule_is_due(const wind_schedule_state_t *state, time_t now, int64_t *out_boundary)
{
    if (!state || state->schema_version != WIND_SCHEDULE_SCHEMA_VERSION) {
        return false;
    }
    int64_t boundary = wind_schedule_latest_boundary(state->timezone, now);
    if (out_boundary) {
        *out_boundary = boundary;
    }
    return boundary > state->last_attempted_boundary && boundary > state->last_satisfied_boundary;
}

bool wind_schedule_retry_is_due(const wind_schedule_state_t *state, time_t now,
                                int64_t *out_boundary)
{
    if (!state || state->schema_version != WIND_SCHEDULE_SCHEMA_VERSION ||
        state->retry_at <= 0 || state->retry_at > now) {
        return false;
    }
    if (out_boundary) {
        *out_boundary = state->retry_boundary;
    }
    return true;
}

void wind_schedule_mark_attempted(wind_schedule_state_t *state, int64_t boundary)
{
    if (state && boundary > state->last_attempted_boundary) {
        state->last_attempted_boundary = boundary;
    }
}

void wind_schedule_mark_satisfied(wind_schedule_state_t *state, int64_t boundary)
{
    if (state && boundary > state->last_satisfied_boundary) {
        state->last_satisfied_boundary = boundary;
    }
}

void wind_schedule_schedule_retry(wind_schedule_state_t *state, int64_t boundary,
                                  int64_t retry_at)
{
    if (!state || boundary <= 0 || retry_at <= 0) {
        return;
    }
    state->retry_boundary = boundary;
    state->retry_at = retry_at;
    state->final_retry_at = 0;
}

void wind_schedule_consume_retry(wind_schedule_state_t *state)
{
    if (!state) {
        return;
    }
    state->retry_boundary = 0;
    state->retry_at = 0;
    state->final_retry_at = 0;
}

esp_err_t wind_schedule_state_load_scoped(const char *path, const char *spot_id,
                                          const char *timezone,
                                          wind_schedule_state_t *out_state)
{
    if (!path || !spot_id || !timezone || !out_state) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        initialize_scope(out_state, spot_id, timezone);
        return ESP_ERR_NOT_FOUND;
    }
    schedule_record_t record = {0};
    bool ok = fread(&record, 1, sizeof(record), file) == sizeof(record) && fgetc(file) == EOF;
    fclose(file);
    uint32_t expected = checksum_bytes(&record.state, sizeof(record.state));
    if (!ok || record.magic != 0x574E4453u || record.checksum != expected ||
        record.state.schema_version != WIND_SCHEDULE_SCHEMA_VERSION) {
        previous_schedule_record_t previous = {0};
        file = fopen(path, "rb");
        if (!file) return ESP_ERR_NOT_FOUND;
        ok = fread(&previous, 1, sizeof(previous), file) == sizeof(previous) && fgetc(file) == EOF;
        fclose(file);
        if (!ok || previous.magic != 0x574E4453u || previous.state.schema_version != 4u ||
            previous.checksum != checksum_bytes(&previous.state, sizeof(previous.state)) ||
            previous.state.spot_id[0] == '\0' || previous.state.timezone[0] == '\0' ||
            !memchr(previous.state.spot_id, '\0', sizeof(previous.state.spot_id)) ||
            !memchr(previous.state.timezone, '\0', sizeof(previous.state.timezone))) {
            initialize_scope(out_state, spot_id, timezone);
            return ESP_ERR_INVALID_CRC;
        }
        initialize_scope(&record.state, previous.state.spot_id, previous.state.timezone);
        record.state.last_attempted_boundary = previous.state.last_attempted_boundary;
        record.state.last_satisfied_boundary = previous.state.last_satisfied_boundary;
        record.state.retry_boundary = previous.state.retry_boundary;
        record.state.retry_at = previous.state.retry_at;
        record.state.final_retry_at = previous.state.final_retry_at;
    }
    if (!wind_schedule_state_matches_scope(&record.state, spot_id, timezone)) {
        initialize_scope(out_state, spot_id, timezone);
        return ESP_ERR_INVALID_STATE;
    }
    *out_state = record.state;
    return ESP_OK;
}

esp_err_t wind_schedule_state_store(const char *path, const wind_schedule_state_t *state)
{
    if (!path || !state || state->schema_version != WIND_SCHEDULE_SCHEMA_VERSION ||
        state->spot_id[0] == '\0' || state->timezone[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char temporary[256];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int) sizeof(temporary)) {
        return ESP_ERR_INVALID_SIZE;
    }
    schedule_record_t record = {.magic = 0x574E4453u, .state = *state};
    record.checksum = checksum_bytes(&record.state, sizeof(record.state));
    FILE *file = fopen(temporary, "wb");
    if (!file) {
        return ESP_FAIL;
    }
    bool ok = fwrite(&record, 1, sizeof(record), file) == sizeof(record) && fflush(file) == 0 &&
              fsync(fileno(file)) == 0;
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok || rename(temporary, path) != 0) {
        unlink(temporary);
        return ESP_FAIL;
    }
    return ESP_OK;
}
