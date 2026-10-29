#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIND_SCHEDULE_SCHEMA_VERSION 5u
#define WIND_SCHEDULE_BOUNDARY_COUNT 5
#define WIND_SCHEDULE_SPOT_ID_MAX 65
#define WIND_SCHEDULE_TIMEZONE_MAX 40

typedef struct {
    uint32_t schema_version;
    char spot_id[WIND_SCHEDULE_SPOT_ID_MAX];
    char timezone[WIND_SCHEDULE_TIMEZONE_MAX];
    int64_t last_attempted_boundary;
    int64_t last_satisfied_boundary;
    int64_t retry_boundary;
    int64_t retry_at;
    int64_t final_retry_at;
} wind_schedule_state_t;

esp_err_t wind_schedule_state_set_scope(wind_schedule_state_t *state, const char *spot_id,
                                        const char *timezone);
bool wind_schedule_state_matches_scope(const wind_schedule_state_t *state, const char *spot_id,
                                       const char *timezone);
int64_t wind_schedule_latest_boundary(const char *timezone, time_t now);
int64_t wind_schedule_next_boundary(const char *timezone, time_t now);
int64_t wind_schedule_next_attempt(const wind_schedule_state_t *state, time_t now);
bool wind_schedule_is_due(const wind_schedule_state_t *state, time_t now,
                          int64_t *out_boundary);
bool wind_schedule_retry_is_due(const wind_schedule_state_t *state, time_t now,
                                int64_t *out_boundary);
void wind_schedule_mark_attempted(wind_schedule_state_t *state, int64_t boundary);
void wind_schedule_mark_satisfied(wind_schedule_state_t *state, int64_t boundary);
void wind_schedule_schedule_retry(wind_schedule_state_t *state, int64_t boundary,
                                  int64_t retry_at);
void wind_schedule_consume_retry(wind_schedule_state_t *state);
esp_err_t wind_schedule_state_load_scoped(const char *path, const char *spot_id,
                                          const char *timezone,
                                          wind_schedule_state_t *out_state);
esp_err_t wind_schedule_state_store(const char *path, const wind_schedule_state_t *state);

#ifdef __cplusplus
}
#endif
