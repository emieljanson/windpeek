#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hardware_profile.h"
#include "installed_configuration.h"
#include "wind_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIND_INSTALLER_SSID_MAX 32
#define WIND_INSTALLER_PASSWORD_MAX 64

/* Fixed numeric health fields; no console text or configuration contents. */
typedef struct {
    uint32_t stage, heap, minimum_heap, stack, reset_reason, uptime_ms;
    uint32_t internal_largest_bytes;
    int64_t device_time;
    wind_provider_diagnostics_t forecast;
} wind_installer_health_t;

typedef esp_err_t (*wind_installer_test_wifi_fn)(void *context, const char *ssid,
                                                  const char *password);
typedef esp_err_t (*wind_installer_render_fn)(void *context,
                                              const installed_configuration_t *candidate);
typedef esp_err_t (*wind_installer_commit_fn)(void *context,
                                              const installed_configuration_t *candidate,
                                              const char *ssid, const char *password);
typedef esp_err_t (*wind_installer_begin_apply_fn)(void *context,
                                                   const installed_configuration_t *candidate,
                                                   const char *ssid, const char *password);
typedef esp_err_t (*wind_installer_start_apply_fn)(void *context);
typedef const char *(*wind_installer_apply_state_fn)(void *context);
typedef void (*wind_installer_wake_lock_fn)(void *context, bool held);
typedef void (*wind_installer_abort_fn)(void *context);
typedef esp_err_t (*wind_installer_scan_wifi_fn)(void *context, char *response,
                                                 size_t response_size);
typedef bool (*wind_installer_state_fn)(void *context);
typedef esp_err_t (*wind_installer_set_clock_fn)(void *context, int64_t unix_seconds);
typedef esp_err_t (*wind_installer_get_hardware_profile_fn)(
    void *context, hardware_profile_state_t *state);
typedef esp_err_t (*wind_installer_select_hardware_profile_fn)(
    void *context, hardware_model_t model, uint32_t expected_revision,
    hardware_profile_update_result_t *result);

typedef struct {
    void *context;
    wind_installer_test_wifi_fn test_wifi;
    wind_installer_render_fn render_candidate;
    wind_installer_commit_fn commit;
    wind_installer_begin_apply_fn begin_apply;
    wind_installer_start_apply_fn start_apply;
    wind_installer_apply_state_fn apply_state;
    wind_installer_wake_lock_fn set_wake_lock;
    wind_installer_abort_fn abort;
    wind_installer_scan_wifi_fn scan_wifi;
    wind_installer_state_fn wifi_connected;
    wind_installer_state_fn render_succeeded;
    wind_installer_set_clock_fn set_clock;
    wind_installer_get_hardware_profile_fn get_hardware_profile;
    wind_installer_select_hardware_profile_fn select_hardware_profile;
    /* Numeric ESP-IDF failure code only; never diagnostic text or credentials. */
    esp_err_t (*apply_error)(void *context);
    void (*health)(void *context, wind_installer_health_t *health);
} wind_installer_dependencies_t;

typedef struct {
    wind_installer_dependencies_t dependencies;
    installed_configuration_t candidate;
    bool candidate_staged;
    bool wifi_ready;
    bool wake_lock_held;
    bool credentials_cleared;
    bool apply_start_pending;
    char ssid[WIND_INSTALLER_SSID_MAX + 1];
    char password[WIND_INSTALLER_PASSWORD_MAX + 1];
} wind_installer_service_t;

void wind_installer_service_init(wind_installer_service_t *service,
                                 const wind_installer_dependencies_t *dependencies);
esp_err_t wind_installer_service_handle_json(wind_installer_service_t *service,
                                             const char *payload, size_t payload_length,
                                             char *response, size_t response_size);
void wind_installer_service_timeout(wind_installer_service_t *service);
bool wind_installer_service_check_idle(wind_installer_service_t *service,
                                       bool usb_connected, int64_t idle_us);
void wind_installer_service_disconnect(wind_installer_service_t *service);
void wind_installer_service_complete_apply(wind_installer_service_t *service, bool succeeded);
esp_err_t wind_installer_service_start_pending_apply(wind_installer_service_t *service);
esp_err_t wind_installer_service_confirm_pending_apply_response(
    wind_installer_service_t *service, bool response_transmitted);

#ifdef ESP_PLATFORM
esp_err_t wind_installer_service_start(void);
#endif

#ifdef __cplusplus
}
#endif
