#include "wind_installer_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

#ifdef ESP_PLATFORM
#include <stdatomic.h>
#include <sys/time.h>

#include "board_hal.h"
#include "epaper.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_log_level.h"
#include "esp_rom_serial_output.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_manager.h"
#include "wifi_manager.h"
#include "wind_app.h"
#include "wind_clock.h"
#include "wind_spots.h"
#include "wind_usb_protocol.h"

#if defined(CONFIG_BOARD_CAP_WINDPEEK) && !defined(CONFIG_UART_ISR_IN_IRAM)
#error "Windpeek USB reception must remain available during flash writes"
#endif

#define WIND_INSTALLER_APPLY_STACK_SIZE 32768

typedef enum {
    PHYSICAL_APPLY_IDLE,
    PHYSICAL_APPLY_RUNNING,
    PHYSICAL_APPLY_COMPLETE,
    PHYSICAL_APPLY_RENDER_FAILED,
    PHYSICAL_APPLY_COMMIT_FAILED,
} physical_apply_state_t;

typedef struct {
    wind_installer_service_t service;
    wind_usb_parser_t parser;
    char response[WIND_USB_MAX_PAYLOAD];
    uint8_t output[WIND_USB_MAX_FRAME_SIZE];
    char previous_ssid[WIND_INSTALLER_SSID_MAX + 1];
    char previous_password[WIND_INSTALLER_PASSWORD_MAX + 1];
    bool had_previous_wifi;
    bool candidate_wifi_active;
    atomic_int apply_state;
    atomic_int apply_error;
    atomic_bool apply_render_verified;
    atomic_uint diagnostic_stage;
    wind_provider_diagnostics_t forecast_diagnostics;
    installed_configuration_t apply_candidate;
    char apply_ssid[WIND_INSTALLER_SSID_MAX + 1];
    char apply_password[WIND_INSTALLER_PASSWORD_MAX + 1];
    bool apply_has_wifi;
    void (*on_configuration_installed)(void);
    int64_t last_activity_us;
} physical_installer_t;

static physical_installer_t *s_physical_installer;
// Reserve internal RAM before Wi-Fi/HTTPS can fragment the heap. The apply
// pipeline writes flash, so its stack must not live in external PSRAM.
static StackType_t s_apply_stack[WIND_INSTALLER_APPLY_STACK_SIZE];
static StaticTask_t s_apply_task_buffer;
static TaskHandle_t s_apply_task;

// Stable diagnostic IDs: retain these values across firmware releases.
enum {
    DIAG_READY = 0, DIAG_WIFI_BEGIN = 1, DIAG_WIFI_DONE = 2,
    DIAG_PREVIEW_BEGIN = 3, DIAG_PREVIEW_DONE = 4, DIAG_ACTIVATE = 5,
    DIAG_PERSIST = 6, DIAG_RELOAD = 7, DIAG_COMPLETE = 8,
    DIAG_IDLE_TIMEOUT = 9,
};

static void physical_health(void *context, wind_installer_health_t *health)
{
    physical_installer_t *installer = context;
    *health = (wind_installer_health_t) {
        .stage = atomic_load(&installer->diagnostic_stage),
        .heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .minimum_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .stack = uxTaskGetStackHighWaterMark(NULL),
        .reset_reason = esp_reset_reason(),
        .uptime_ms = (uint32_t) (esp_timer_get_time() / 1000),
        .device_time = (int64_t)time(NULL),
        .internal_largest_bytes = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
    };
    wind_app_status_get(&health->refresh);
    const epaper_panel_diagnostics_t panel = epaper_panel_diagnostics_get();
    health->panel_phase = panel.phase;
    health->panel_wait_ms = panel.wait_ms;
    health->panel_busy_level = panel.busy_level;
    // The worker owns this snapshot until it publishes a terminal state.
    const int state = atomic_load(&installer->apply_state);
    if (state != PHYSICAL_APPLY_IDLE && state != PHYSICAL_APPLY_RUNNING)
        health->forecast = installer->forecast_diagnostics;
}

static void physical_checkpoint(physical_installer_t *installer, unsigned stage)
{
    atomic_store(&installer->diagnostic_stage, stage);
    wind_installer_health_t health;
    physical_health(installer, &health);
    // Deliberately fixed numeric output: never include credentials or settings.
    printf("WINDDIAG stage=%u heap=%" PRIu32 " min=%" PRIu32
           " stack=%" PRIu32 " reset=%" PRIu32 " uptime=%" PRIu32 "\n",
           stage, health.heap, health.minimum_heap, health.stack, health.reset_reason,
           health.uptime_ms);
}

static void physical_clear_previous_wifi(physical_installer_t *installer)
{
    memset(installer->previous_ssid, 0, sizeof(installer->previous_ssid));
    memset(installer->previous_password, 0, sizeof(installer->previous_password));
    installer->had_previous_wifi = false;
    installer->candidate_wifi_active = false;
}

static void physical_abort(void *context)
{
    physical_installer_t *installer = (physical_installer_t *) context;
    if (!installer->candidate_wifi_active) return;
    if (installer->had_previous_wifi) {
        (void) wifi_manager_connect(installer->previous_ssid, installer->previous_password);
    } else {
        (void) wifi_manager_disconnect();
    }
    physical_clear_previous_wifi(installer);
}

static esp_err_t physical_test_wifi(void *context, const char *ssid, const char *password)
{
    physical_installer_t *installer = (physical_installer_t *) context;
    physical_checkpoint(installer, DIAG_WIFI_BEGIN);
    physical_clear_previous_wifi(installer);
    installer->had_previous_wifi = wifi_manager_load_credentials(
        installer->previous_ssid, installer->previous_password) == ESP_OK;
    const esp_err_t result = wifi_manager_connect(ssid, password);
    physical_checkpoint(installer, DIAG_WIFI_DONE);
    installer->candidate_wifi_active = true;
    if (result != ESP_OK) physical_abort(installer);
    return result;
}

static esp_err_t physical_render(void *context, const installed_configuration_t *candidate)
{
    physical_installer_t *installer = context;
    physical_checkpoint(installer, DIAG_PREVIEW_BEGIN);
    esp_err_t result = wind_app_preview_configuration(candidate, &installer->forecast_diagnostics);
    if (result == ESP_OK) physical_checkpoint(context, DIAG_PREVIEW_DONE);
    return result;
}

static esp_err_t physical_commit(void *context, const installed_configuration_t *candidate,
                                 const char *ssid, const char *password)
{
    physical_installer_t *installer = (physical_installer_t *) context;
    installed_configuration_t previous;
    if (installed_configuration_load(&previous) != ESP_OK) return ESP_ERR_INVALID_STATE;
    physical_checkpoint(installer, DIAG_ACTIVATE);
    esp_err_t result = wind_app_activate_configuration(candidate);
    if (result != ESP_OK) return result;
    physical_checkpoint(installer, DIAG_PERSIST);
    result = installed_configuration_promote_setup(candidate, ssid, password);
    if (result != ESP_OK) {
        (void) wind_app_activate_configuration(&previous);
        physical_abort(installer);
        return result;
    }
    physical_checkpoint(installer, DIAG_RELOAD);
    result = wind_spots_reload_installed();
    if (result != ESP_OK) {
        (void) installed_configuration_promote_setup(
            &previous, installer->had_previous_wifi ? installer->previous_ssid : NULL,
            installer->had_previous_wifi ? installer->previous_password : NULL);
        (void) wind_app_activate_configuration(&previous);
        physical_abort(installer);
        return result;
    }
    physical_clear_previous_wifi(installer);
    return ESP_OK;
}

static void physical_clear_apply(physical_installer_t *installer)
{
    volatile unsigned char *password =
        (volatile unsigned char *) installer->apply_password;
    for (size_t index = 0; index < sizeof(installer->apply_password); ++index) {
        password[index] = 0;
    }
    memset(installer->apply_ssid, 0, sizeof(installer->apply_ssid));
    memset(&installer->apply_candidate, 0, sizeof(installer->apply_candidate));
    installer->apply_has_wifi = false;
}

static void physical_apply_task(void *argument)
{
    physical_installer_t *installer = (physical_installer_t *) argument;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI("wind_installer", "Applying configuration (stack free: %u bytes)",
                 (unsigned) uxTaskGetStackHighWaterMark(NULL));
        esp_err_t result = physical_render(installer, &installer->apply_candidate);
        atomic_store(&installer->apply_render_verified, result == ESP_OK);
        physical_apply_state_t final_state = PHYSICAL_APPLY_RENDER_FAILED;
        if (result != ESP_OK) {
            wind_installer_service_complete_apply(&installer->service, false);
        } else {
            ESP_LOGI("wind_installer", "Preview rendered (stack free: %u bytes)",
                     (unsigned) uxTaskGetStackHighWaterMark(NULL));
            result = physical_commit(installer, &installer->apply_candidate,
                                     installer->apply_has_wifi ? installer->apply_ssid : NULL,
                                     installer->apply_has_wifi ? installer->apply_password : NULL);
            wind_installer_service_complete_apply(&installer->service, result == ESP_OK);
            final_state = result == ESP_OK ? PHYSICAL_APPLY_COMPLETE
                                          : PHYSICAL_APPLY_COMMIT_FAILED;
        }
        if (result != ESP_OK && wind_app_show_failed_setup() != ESP_OK) {
            ESP_LOGW("wind_installer", "Could not replace failed setup preview");
        }
        if (result != ESP_OK) atomic_store(&installer->apply_render_verified, false);
        physical_clear_apply(installer);
        atomic_store(&installer->apply_error, result);
        if (result == ESP_OK) physical_checkpoint(installer, DIAG_COMPLETE);
        // Publish completion only after cleanup, so the next attempt cannot have
        // its configuration or credentials erased by the previous worker.
        atomic_store(&installer->apply_state, final_state);
        if (final_state == PHYSICAL_APPLY_COMPLETE && installer->on_configuration_installed)
            installer->on_configuration_installed();
    }
}

static esp_err_t physical_begin_apply(void *context,
                                      const installed_configuration_t *candidate,
                                      const char *ssid, const char *password)
{
    physical_installer_t *installer = (physical_installer_t *) context;
    if (atomic_load(&installer->apply_state) == PHYSICAL_APPLY_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    physical_clear_apply(installer);
    memset(&installer->forecast_diagnostics, 0, sizeof(installer->forecast_diagnostics));
    epaper_panel_diagnostics_reset();
    atomic_store(&installer->apply_error, ESP_OK);
    atomic_store(&installer->apply_render_verified, false);
    installer->apply_candidate = *candidate;
    if (ssid && password) {
        snprintf(installer->apply_ssid, sizeof(installer->apply_ssid), "%s", ssid);
        snprintf(installer->apply_password, sizeof(installer->apply_password), "%s", password);
        installer->apply_has_wifi = true;
    }
    atomic_store(&installer->apply_state, PHYSICAL_APPLY_RUNNING);
    return ESP_OK;
}

static esp_err_t physical_start_apply(void *context)
{
    (void)context;
    xTaskNotifyGive(s_apply_task);
    return ESP_OK;
}

static esp_err_t physical_apply_error(void *context)
{
    return atomic_load(&((physical_installer_t *)context)->apply_error);
}

static const char *physical_apply_state(void *context)
{
    const physical_installer_t *installer = (const physical_installer_t *) context;
    switch (atomic_load(&installer->apply_state)) {
    case PHYSICAL_APPLY_RUNNING: return "applying";
    case PHYSICAL_APPLY_COMPLETE: return "complete";
    case PHYSICAL_APPLY_RENDER_FAILED: return "render_failed";
    case PHYSICAL_APPLY_COMMIT_FAILED: return "commit_failed";
    default: return "idle";
    }
}

static void physical_wake_lock(void *context, bool held)
{
    (void) context;
    power_manager_set_installer_active(held);
}

static bool physical_wifi_connected(void *context)
{
    (void) context;
    return wifi_manager_is_connected();
}

static bool physical_render_succeeded(void *context)
{
    physical_installer_t *installer = context;
    if (atomic_load(&installer->apply_state) == PHYSICAL_APPLY_COMPLETE)
        return atomic_load(&installer->apply_render_verified);
    return wind_app_last_render_succeeded();
}

static esp_err_t physical_write_rtc(void *context, time_t value)
{
    (void) context;
    return board_hal_rtc_set_time(value);
}

static esp_err_t physical_write_system_clock(void *context, time_t seconds)
{
    (void) context;
    const struct timeval value = {.tv_sec = seconds, .tv_usec = 0};
    return settimeofday(&value, NULL) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t physical_set_clock(void *context, int64_t unix_seconds)
{
    return wind_clock_set_unix(unix_seconds, context, physical_write_rtc,
                               physical_write_system_clock);
}

#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E100X
static esp_err_t physical_get_hardware_profile(void *context,
                                               hardware_profile_state_t *state)
{
    (void) context;
    return hardware_profile_get_state(state);
}

static esp_err_t physical_select_hardware_profile(
    void *context, hardware_model_t model, uint32_t expected_revision,
    hardware_profile_update_result_t *result)
{
    (void) context;
    return hardware_profile_select(model, HARDWARE_PROFILE_SOURCE_OWNER_CONFIRMATION,
                                   expected_revision, result);
}
#endif

static esp_err_t physical_scan_wifi(void *context, char *response, size_t response_size)
{
    (void) context;
    wifi_ap_record_t records[20];
    int count = wifi_manager_scan(records, 20);
    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_AddArrayToObject(root, "networks");
    for (int index = 0; index < count; ++index) {
        if (records[index].ssid[0] == '\0') continue;
        bool duplicate = false;
        for (int previous = 0; previous < index; ++previous) {
            if (strcmp((const char *) records[previous].ssid,
                       (const char *) records[index].ssid) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        cJSON *network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", (const char *) records[index].ssid);
        cJSON_AddNumberToObject(network, "rssi", records[index].rssi);
        cJSON_AddBoolToObject(network, "secured", records[index].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(networks, network);
    }
    char *serialized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!serialized) return ESP_ERR_NO_MEM;
    size_t length = strlen(serialized);
    if (length >= response_size) {
        cJSON_free(serialized);
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(response, serialized, length + 1);
    cJSON_free(serialized);
    return ESP_OK;
}

static void physical_frame(const wind_usb_frame_t *frame, void *context)
{
    physical_installer_t *installer = (physical_installer_t *) context;
    memset(installer->response, 0, sizeof(installer->response));
    esp_err_t result = ESP_ERR_NOT_SUPPORTED;
    if (frame->message_type == WIND_USB_MESSAGE_REQUEST) {
        result = wind_installer_service_handle_json(
            &installer->service, (const char *) frame->payload, frame->payload_length,
            installer->response, sizeof(installer->response));
    }
    if (result != ESP_OK) {
        snprintf(installer->response, sizeof(installer->response),
                 "{\"status\":\"invalid_request\",\"code\":%d}", result);
    }
    size_t output_size = wind_usb_encode_frame(
        frame->request_id, result == ESP_OK ? WIND_USB_MESSAGE_RESULT : WIND_USB_MESSAGE_ERROR,
        (const uint8_t *) installer->response, strlen(installer->response), installer->output,
        sizeof(installer->output));

    // The E1002 USB-C connector is exposed through its UART bridge. Serialize
    // this short binary response with the regular ESP-IDF console so a log line
    // can never be inserted inside a CRC-protected frame.
    flockfile(stdout);
    const esp_log_level_t previous_log_level = esp_log_get_level_master();
    esp_log_set_level_master(ESP_LOG_NONE);
    // `esp_rom_output_tx_one_char` is non-blocking and can stop as soon as the
    // small hardware FIFO fills. That silently truncated larger responses such
    // as Wi-Fi scan results. The installed UART driver's TX buffer accepts the
    // complete frame and drains it while the console lock prevents log bytes
    // from being interleaved with the CRC-protected payload.
    const int written = uart_write_bytes(UART_NUM_0, installer->output, output_size);
    // The apply task can spend tens of seconds rendering without servicing the
    // installer UART. Make sure the small `applying` acknowledgement has
    // physically left the UART before that work is allowed to start.
    const esp_err_t transmit_result = uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(100));
    esp_log_set_level_master(previous_log_level);
    funlockfile(stdout);
    if (installer->service.apply_start_pending) {
        const bool response_transmitted =
            written == (int) output_size && transmit_result == ESP_OK;
        if (wind_installer_service_confirm_pending_apply_response(
                &installer->service, response_transmitted) != ESP_OK) {
            if (!response_transmitted)
                atomic_store(&installer->apply_error,
                             transmit_result != ESP_OK ? transmit_result : ESP_FAIL);
            physical_clear_apply(installer);
            atomic_store(&installer->apply_state, PHYSICAL_APPLY_COMMIT_FAILED);
        }
    }
    installer->last_activity_us = esp_timer_get_time();
}

static void installer_usb_task(void *argument)
{
    physical_installer_t *installer = (physical_installer_t *) argument;
    uint8_t input[256];
    ESP_LOGI("wind_installer", "Installer UART task ready");
    while (true) {
        int read = uart_read_bytes(UART_NUM_0, input, sizeof(input), pdMS_TO_TICKS(250));
        if (read > 0) {
            esp_err_t result = wind_usb_parser_feed(&installer->parser, input, (size_t) read,
                                                    physical_frame, installer);
            if (result != ESP_OK && installer->service.wake_lock_held) {
                wind_installer_service_disconnect(&installer->service);
            }
        }
        if (wind_installer_service_check_idle(&installer->service,
                board_hal_is_usb_connected(), esp_timer_get_time() - installer->last_activity_us))
            physical_checkpoint(installer, DIAG_IDLE_TIMEOUT);
    }
}

esp_err_t wind_installer_service_start(void (*on_configuration_installed)(void))
{
    if (s_apply_task) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ESP_OK;
    if (!uart_is_driver_installed(UART_NUM_0)) {
        result = uart_driver_install(UART_NUM_0, 4096, 4096, 0, NULL, 0);
        if (result != ESP_OK) return result;
    }
    // Parser, response and candidate storage is task-owned, never DMA/ISR memory.
    // Keep ~60 KiB out of internal RAM so Wi-Fi/TLS and stacks can coexist.
    s_physical_installer = heap_caps_calloc(1, sizeof(*s_physical_installer),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_physical_installer) return ESP_ERR_NO_MEM;
    s_physical_installer->on_configuration_installed = on_configuration_installed;
    atomic_init(&s_physical_installer->apply_state, PHYSICAL_APPLY_IDLE);
    atomic_init(&s_physical_installer->apply_error, ESP_OK);
    atomic_init(&s_physical_installer->diagnostic_stage, DIAG_READY);
    wind_usb_parser_init(&s_physical_installer->parser);
    const wind_installer_dependencies_t dependencies = {
        .context = s_physical_installer,
        .test_wifi = physical_test_wifi,
        .render_candidate = physical_render,
        .commit = physical_commit,
        .begin_apply = physical_begin_apply,
        .start_apply = physical_start_apply,
        .apply_state = physical_apply_state,
        .apply_error = physical_apply_error,
        .health = physical_health,
        .set_wake_lock = physical_wake_lock,
        .abort = physical_abort,
        .scan_wifi = physical_scan_wifi,
        .wifi_connected = physical_wifi_connected,
        .render_succeeded = physical_render_succeeded,
        .set_clock = physical_set_clock,
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E100X
        .get_hardware_profile = physical_get_hardware_profile,
        .select_hardware_profile = physical_select_hardware_profile,
#endif
    };
    wind_installer_service_init(&s_physical_installer->service, &dependencies);
    s_physical_installer->last_activity_us = esp_timer_get_time();
    s_apply_task = xTaskCreateStatic(physical_apply_task, "wind_apply",
        sizeof(s_apply_stack), s_physical_installer, 6, s_apply_stack, &s_apply_task_buffer);
    if (!s_apply_task) {
        free(s_physical_installer);
        s_physical_installer = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(installer_usb_task, "wind_usb", 16384, s_physical_installer, 6, NULL) != pdPASS) {
        vTaskDelete(s_apply_task);
        s_apply_task = NULL;
        free(s_physical_installer);
        s_physical_installer = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
#endif
