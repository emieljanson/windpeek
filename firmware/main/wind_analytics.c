#include "wind_analytics.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "wind_clock.h"

#define WIND_ANALYTICS_STATE_VERSION UINT32_C(1)
#define WIND_ANALYTICS_POSTHOG_URL "https://us.i.posthog.com/i/v0/e/"
#define WIND_ANALYTICS_PAYLOAD_SIZE 512

#ifndef WINDSCOUT_POSTHOG_PROJECT_TOKEN
#define WINDSCOUT_POSTHOG_PROJECT_TOKEN ""
#endif

#ifdef ESP_PLATFORM
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "nvs.h"

#include "installed_configuration.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev-unknown"
#endif

#define WIND_ANALYTICS_NVS_NAMESPACE "wind_analytics"
#define WIND_ANALYTICS_NVS_STATE_KEY "state_v1"

static esp_err_t load_state(void *context, wind_analytics_state_t *state);
static esp_err_t store_state(void *context, const wind_analytics_state_t *state);
static uint32_t random_u32(void *context);
static esp_err_t send_event(void *context, const char *dashboard_id);
#endif

static bool safe_value(const char *value, bool token)
{
    if (!value || value[0] == '\0') return false;
    for (const unsigned char *cursor = (const unsigned char *) value; *cursor; ++cursor) {
        if (isalnum(*cursor) || *cursor == '_' ||
            (!token && (*cursor == '-' || *cursor == '.'))) {
            continue;
        }
        return false;
    }
    return true;
}

bool wind_analytics_dashboard_id_valid(const char *dashboard_id)
{
    if (!dashboard_id || strlen(dashboard_id) != WIND_ANALYTICS_DASHBOARD_ID_LENGTH) {
        return false;
    }
    for (size_t index = 0; index < WIND_ANALYTICS_DASHBOARD_ID_LENGTH; ++index) {
        if (!isdigit((unsigned char) dashboard_id[index]) &&
            !(dashboard_id[index] >= 'a' && dashboard_id[index] <= 'f')) {
            return false;
        }
    }
    return true;
}

esp_err_t wind_analytics_build_payload(const char *project_token, const char *dashboard_id,
                                       const char *firmware_version, const char *device_type,
                                       char *payload, size_t payload_size)
{
    if (!safe_value(project_token, true) ||
        !wind_analytics_dashboard_id_valid(dashboard_id) ||
        !safe_value(firmware_version, false) || !safe_value(device_type, false) || !payload ||
        payload_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    int written = snprintf(
        payload, payload_size,
        "{\"api_key\":\"%s\",\"event\":\"windscout_dashboard_heartbeat\","
        "\"distinct_id\":\"%s\",\"properties\":{\"$process_person_profile\":false,"
        "\"firmware_version\":\"%s\",\"device_type\":\"%s\"}}",
        project_token, dashboard_id, firmware_version, device_type);
    return written > 0 && (size_t) written < payload_size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

bool wind_analytics_http_status_accepted(int status_code)
{
    return status_code >= 200 && status_code < 300;
}

esp_err_t wind_analytics_run(const wind_analytics_dependencies_t *dependencies, time_t now)
{
    if (!dependencies || !dependencies->load || !dependencies->store ||
        !dependencies->random_u32 || !dependencies->send ||
        !wind_clock_is_valid_unix((int64_t) now)) {
        return ESP_ERR_INVALID_STATE;
    }

    wind_analytics_state_t state = {0};
    esp_err_t result = dependencies->load(dependencies->context, &state);
    if (result == ESP_ERR_NOT_FOUND) {
        state.version = WIND_ANALYTICS_STATE_VERSION;
        uint32_t random_words[4];
        for (size_t index = 0; index < 4; ++index) {
            random_words[index] = dependencies->random_u32(dependencies->context);
        }
        int written = snprintf(
            state.dashboard_id, sizeof(state.dashboard_id), "%08lx%08lx%08lx%08lx",
            (unsigned long) random_words[0], (unsigned long) random_words[1],
            (unsigned long) random_words[2], (unsigned long) random_words[3]);
        if (written != WIND_ANALYTICS_DASHBOARD_ID_LENGTH ||
            !wind_analytics_dashboard_id_valid(state.dashboard_id)) {
            return ESP_FAIL;
        }
        result = dependencies->store(dependencies->context, &state);
        if (result != ESP_OK) return result;
    } else if (result != ESP_OK) {
        return result;
    } else if (state.version != WIND_ANALYTICS_STATE_VERSION ||
               !wind_analytics_dashboard_id_valid(state.dashboard_id) ||
               state.last_success_unix < 0 || state.last_attempt_unix < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t current = (int64_t) now;
    if (state.last_success_unix > current || state.last_attempt_unix > current) {
        return ESP_ERR_INVALID_STATE;
    }
    if (state.last_success_unix > 0 &&
        current - state.last_success_unix < WIND_ANALYTICS_HEARTBEAT_SECONDS) {
        return ESP_OK;
    }
    if (state.last_attempt_unix > state.last_success_unix &&
        current - state.last_attempt_unix < WIND_ANALYTICS_RETRY_SECONDS) {
        return ESP_OK;
    }

    state.last_attempt_unix = current;
    result = dependencies->store(dependencies->context, &state);
    if (result != ESP_OK) return result;

    result = dependencies->send(dependencies->context, state.dashboard_id);
    if (result != ESP_OK) return result;

    state.last_success_unix = current;
    return dependencies->store(dependencies->context, &state);
}

esp_err_t wind_analytics_maybe_send(time_t now)
{
#ifdef ESP_PLATFORM
    if (WINDSCOUT_POSTHOG_PROJECT_TOKEN[0] == '\0') return ESP_OK;

    const wind_analytics_dependencies_t dependencies = {
        .context = NULL,
        .load = load_state,
        .store = store_state,
        .random_u32 = random_u32,
        .send = send_event,
    };
    return wind_analytics_run(&dependencies, now);
#else
    (void) now;
    return ESP_OK;
#endif
}

#ifdef ESP_PLATFORM
static esp_err_t load_state(void *context, wind_analytics_state_t *state)
{
    (void) context;
    nvs_handle_t handle;
    esp_err_t result = nvs_open(WIND_ANALYTICS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (result != ESP_OK) return result;
    size_t size = sizeof(*state);
    result = nvs_get_blob(handle, WIND_ANALYTICS_NVS_STATE_KEY, state, &size);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (result != ESP_OK) return result;
    return size == sizeof(*state) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t store_state(void *context, const wind_analytics_state_t *state)
{
    (void) context;
    nvs_handle_t handle;
    esp_err_t result = nvs_open(WIND_ANALYTICS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_blob(handle, WIND_ANALYTICS_NVS_STATE_KEY, state, sizeof(*state));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}

static uint32_t random_u32(void *context)
{
    (void) context;
    return esp_random();
}

static esp_err_t send_event(void *context, const char *dashboard_id)
{
    (void) context;
    char payload[WIND_ANALYTICS_PAYLOAD_SIZE];
    esp_err_t result = wind_analytics_build_payload(
        WINDSCOUT_POSTHOG_PROJECT_TOKEN, dashboard_id, FIRMWARE_VERSION, WINDSCOUT_BOARD_ID,
        payload, sizeof(payload));
    if (result != ESP_OK) return result;

    esp_http_client_config_t config = {
        .url = WIND_ANALYTICS_POSTHOG_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;
    result = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (result == ESP_OK) {
        result = esp_http_client_set_post_field(client, payload, strlen(payload));
    }
    if (result == ESP_OK) result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (result != ESP_OK) return result;
    return wind_analytics_http_status_accepted(status_code) ? ESP_OK : ESP_FAIL;
}
#endif
