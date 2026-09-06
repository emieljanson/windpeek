#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIND_ANALYTICS_DASHBOARD_ID_LENGTH 32
#define WIND_ANALYTICS_HEARTBEAT_SECONDS INT64_C(604800)
#define WIND_ANALYTICS_RETRY_SECONDS INT64_C(86400)

typedef struct {
    uint32_t version;
    char dashboard_id[WIND_ANALYTICS_DASHBOARD_ID_LENGTH + 1];
    int64_t last_success_unix;
    int64_t last_attempt_unix;
} wind_analytics_state_t;

typedef esp_err_t (*wind_analytics_load_fn)(void *context, wind_analytics_state_t *state);
typedef esp_err_t (*wind_analytics_store_fn)(void *context,
                                             const wind_analytics_state_t *state);
typedef uint32_t (*wind_analytics_random_fn)(void *context);
typedef esp_err_t (*wind_analytics_send_fn)(void *context, const char *dashboard_id);

typedef struct {
    void *context;
    wind_analytics_load_fn load;
    wind_analytics_store_fn store;
    wind_analytics_random_fn random_u32;
    wind_analytics_send_fn send;
} wind_analytics_dependencies_t;

bool wind_analytics_dashboard_id_valid(const char *dashboard_id);
esp_err_t wind_analytics_build_payload(const char *project_token, const char *dashboard_id,
                                       const char *firmware_version, const char *device_type,
                                       char *payload, size_t payload_size);
bool wind_analytics_http_status_accepted(int status_code);
esp_err_t wind_analytics_run(const wind_analytics_dependencies_t *dependencies, time_t now);
esp_err_t wind_analytics_maybe_send(time_t now);

#ifdef __cplusplus
}
#endif
