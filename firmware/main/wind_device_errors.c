#include "wind_device_errors.h"

#include <stdio.h>
#include <string.h>
#include "wind_clock.h"

#define REPORT_INTERVAL_SECONDS 3600
#define REPORT_STATE_VERSION 1u
#define SENTRY_DSN "https://8318e5c198b76f64d99701b0bab4a216@o4511992329535488.ingest.de.sentry.io/4511992357584976"
#define SENTRY_ENVELOPE_URL "https://o4511992329535488.ingest.de.sentry.io/api/4511992357584976/envelope/"

static const char *const stages[] = {"forecast", "render", "display", "runtime", "ota"};

static bool safe_label(const char *value, size_t capacity)
{
    size_t length = strnlen(value, capacity);
    if (!length || length == capacity) return false;
    for (size_t i = 0; i < length; ++i) {
        char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '+'))
            return false;
    }
    return true;
}

esp_err_t wind_device_error_envelope(wind_device_error_stage_t stage,
    const wind_device_error_state_t *state, char *output, size_t size)
{
    if (stage < 0 || stage >= WIND_DEVICE_ERROR_COUNT || !state || !output || !size ||
        strnlen(state->event_id, sizeof(state->event_id)) != 32 ||
        strspn(state->event_id, "0123456789abcdef") != 32 ||
        !safe_label(state->error.firmware, sizeof(state->error.firmware)) ||
        !safe_label(state->error.board, sizeof(state->error.board))) return ESP_ERR_INVALID_ARG;
    const wind_device_error_t *error = &state->error;
    char timestamp[64] = "";
    if (wind_clock_is_valid_unix(state->occurred_at))
        snprintf(timestamp, sizeof(timestamp), "\"timestamp\":%lld,", (long long)state->occurred_at);
    // One JSON event per envelope; newline-delimited per Sentry's envelope protocol.
    int written = snprintf(output, size,
        "{\"event_id\":\"%s\",\"dsn\":\"" SENTRY_DSN "\"}\n"
        "{\"type\":\"event\"}\n"
        "{\"event_id\":\"%s\",%s\"platform\":\"native\","
        "\"level\":\"error\",\"logger\":\"windpeek.device\",\"release\":\"%s\","
        "\"message\":\"Windpeek device %s failed\","
        "\"fingerprint\":[\"windpeek-device\",\"%s\",\"%s\",\"%ld\",\"%lu\"],"
        "\"tags\":{\"source\":\"device\",\"stage\":\"%s\",\"board\":\"%s\",\"power\":\"%s\"},"
        "\"extra\":{\"error_code\":%ld,\"panel_phase\":%lu,\"panel_wait_ms\":%lu,"
        "\"panel_busy_level\":%lu,\"battery_mv\":%ld,\"reset_reason\":%lu}}\n",
        state->event_id, state->event_id, timestamp,
        error->firmware, stages[stage], stages[stage], error->board,
        (long)error->error, (unsigned long)error->panel_phase,
        stages[stage], error->board, error->usb ? "usb" : "battery",
        (long)error->error, (unsigned long)error->panel_phase,
        (unsigned long)error->panel_wait_ms, (unsigned long)error->panel_busy_level,
        (long)error->battery_mv, (unsigned long)error->reset_reason);
    return written > 0 && (size_t)written < size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t wind_device_error_run(const wind_device_error_dependencies_t *deps,
    wind_device_error_stage_t stage, const wind_device_error_t *error,
    int64_t now, bool connected)
{
    if (!deps || !deps->load || !deps->store || !deps->random_u32 || !deps->send ||
        stage < 0 || stage >= WIND_DEVICE_ERROR_COUNT || now < 0)
        return ESP_ERR_INVALID_ARG;
    wind_device_error_state_t state = {0};
    esp_err_t result = deps->load(deps->context, stage, &state);
    if (result != ESP_OK && result != ESP_ERR_NOT_FOUND &&
        result != ESP_ERR_INVALID_SIZE) return result;
    const bool reset_record = result != ESP_OK || state.version != REPORT_STATE_VERSION || state.last_attempt < 0;
    if (reset_record) {
        memset(&state, 0, sizeof(state));
        state.version = REPORT_STATE_VERSION;
    }
    // A backwards clock does not permanently disable reporting.
    bool quiet = state.last_attempt > 0 && now >= state.last_attempt &&
                 now - state.last_attempt < REPORT_INTERVAL_SECONDS;
    if (error && error->error != ESP_OK && !state.pending && !quiet) {
        state.pending = true;
        state.occurred_at = wind_clock_is_valid_unix(now) ? now : 0;
        state.error = *error;
        uint32_t words[4];
        for (size_t i = 0; i < 4; ++i) words[i] = deps->random_u32(deps->context);
        snprintf(state.event_id, sizeof(state.event_id), "%08lx%08lx%08lx%08lx",
            (unsigned long)words[0], (unsigned long)words[1],
            (unsigned long)words[2], (unsigned long)words[3]);
        result = deps->store(deps->context, stage, &state);
        if (result != ESP_OK) return result;
    }
    if (reset_record && !state.pending) {
        result = deps->store(deps->context, stage, &state);
        if (result != ESP_OK) return result;
    }
    if (!state.pending || !connected || quiet || !wind_clock_is_valid_unix(now)) return ESP_OK;
    state.last_attempt = now;
    result = deps->store(deps->context, stage, &state);
    if (result != ESP_OK) return result;
    result = deps->send(deps->context, stage, &state);
    if (result != ESP_OK) return result; // Retain the same event ID for retry.
    state.pending = false;
    return deps->store(deps->context, stage, &state);
}

#ifdef ESP_PLATFORM
#include <time.h>
#include "board_hal.h"
#include "epaper.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "wifi_manager.h"
#include "installed_configuration.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev-unknown"
#endif

static SemaphoreHandle_t report_mutex;
static bool flushing;
static bool stage_loaded[WIND_DEVICE_ERROR_COUNT];
static bool stage_pending[WIND_DEVICE_ERROR_COUNT];
static portMUX_TYPE init_lock = portMUX_INITIALIZER_UNLOCKED;

static bool lock_reports(void)
{
    portENTER_CRITICAL(&init_lock);
    SemaphoreHandle_t mutex = report_mutex;
    portEXIT_CRITICAL(&init_lock);
    if (!mutex) {
        SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
        if (!candidate) return false;
        portENTER_CRITICAL(&init_lock);
        if (!report_mutex) {
            report_mutex = candidate;
            candidate = NULL;
        }
        mutex = report_mutex;
        portEXIT_CRITICAL(&init_lock);
        if (candidate) vSemaphoreDelete(candidate);
    }
    return xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE;
}

static esp_err_t load_report(void *context, wind_device_error_stage_t stage,
                            wind_device_error_state_t *state)
{
    (void)context;
    nvs_handle_t handle;
    esp_err_t result = nvs_open("device_errors", NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (result != ESP_OK) return result;
    size_t size = sizeof(*state);
    result = nvs_get_blob(handle, stages[stage], state, &size);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (result == ESP_ERR_NVS_INVALID_LENGTH || result == ESP_ERR_NVS_TYPE_MISMATCH)
        return ESP_ERR_INVALID_SIZE;
    if (result != ESP_OK) return result;
    if (size != sizeof(*state)) return ESP_ERR_INVALID_SIZE;
    stage_loaded[stage] = state->version == REPORT_STATE_VERSION && state->last_attempt >= 0;
    stage_pending[stage] = state->pending;
    return ESP_OK;
}

static esp_err_t store_report(void *context, wind_device_error_stage_t stage,
                             const wind_device_error_state_t *state)
{
    (void)context;
    nvs_handle_t handle;
    esp_err_t result = nvs_open("device_errors", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_blob(handle, stages[stage], state, sizeof(*state));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result == ESP_OK) {
        stage_loaded[stage] = true;
        stage_pending[stage] = state->pending;
    }
    return result;
}

static uint32_t random_word(void *context) { (void)context; return esp_random(); }

static esp_err_t send_report(void *context, wind_device_error_stage_t stage,
                            const wind_device_error_state_t *state)
{
    (void)context;
    char envelope[1536];
    esp_err_t result = wind_device_error_envelope(stage, state, envelope, sizeof(envelope));
    if (result != ESP_OK) return result;
    esp_http_client_config_t config = {
        .url = SENTRY_ENVELOPE_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 4000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;
    result = esp_http_client_set_header(client, "Content-Type", "application/x-sentry-envelope");
    if (result == ESP_OK) result = esp_http_client_set_post_field(client, envelope, strlen(envelope));
    if (result == ESP_OK) result = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (result != ESP_OK) return result;
    return status >= 200 && status < 300 ? ESP_OK : ESP_FAIL;
}

static esp_err_t send_without_lock(void *context, wind_device_error_stage_t stage,
                                   const wind_device_error_state_t *state)
{
    // State and the delivery deadline are already persisted. Captures can queue
    // other stages while the network is slow; only one flush may send at a time.
    xSemaphoreGive(report_mutex);
    esp_err_t result = send_report(context, stage, state);
    xSemaphoreTake(report_mutex, portMAX_DELAY);
    return result;
}

static const wind_device_error_dependencies_t dependencies = {
    .load = load_report, .store = store_report, .random_u32 = random_word, .send = send_without_lock,
};

void wind_device_errors_capture(wind_device_error_stage_t stage, esp_err_t error)
{
    if (error == ESP_OK || !lock_reports()) return;
    wind_device_error_t report = {
        .error = error,
        .battery_mv = board_hal_get_battery_voltage(),
        .usb = board_hal_is_usb_connected(),
        .reset_reason = esp_reset_reason(),
        .firmware = FIRMWARE_VERSION,
        .board = WINDPEEK_BOARD_ID,
    };
    if (stage == WIND_DEVICE_ERROR_DISPLAY) {
        const epaper_panel_diagnostics_t panel = epaper_panel_diagnostics_get();
        report.panel_phase = panel.phase;
        report.panel_wait_ms = panel.wait_ms;
        report.panel_busy_level = panel.busy_level;
    }
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E100X
    snprintf(report.board, sizeof(report.board), "seeedstudio_reterminal_e100%d",
        epaper_active_hardware() == EPAPER_HARDWARE_E1001 ? 1 : 2);
#endif
    (void)wind_device_error_run(&dependencies, stage, &report, time(NULL), false);
    xSemaphoreGive(report_mutex);
}

void wind_device_errors_flush(void)
{
    if (!wifi_manager_is_connected() || !lock_reports()) return;
    if (flushing) {
        xSemaphoreGive(report_mutex);
        return;
    }
    flushing = true;
    for (int stage = 0; stage < WIND_DEVICE_ERROR_COUNT; ++stage)
        if (!stage_loaded[stage] || stage_pending[stage])
            (void)wind_device_error_run(&dependencies, stage, NULL, time(NULL), true);
    flushing = false;
    xSemaphoreGive(report_mutex);
}
#else
void wind_device_errors_capture(wind_device_error_stage_t stage, esp_err_t error)
{ (void)stage; (void)error; }
void wind_device_errors_flush(void) {}
#endif
