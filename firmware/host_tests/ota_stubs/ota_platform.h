#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define WIFI_MANAGER_H
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define NVS_READWRITE 1
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
#define PSA_HASH_OPERATION_INIT {0}
typedef struct { uint8_t hash[32]; size_t count; } psa_hash_operation_t;
int psa_hash_setup(psa_hash_operation_t *, int);
int psa_hash_update(psa_hash_operation_t *, const uint8_t *, size_t);
int psa_hash_finish(psa_hash_operation_t *, uint8_t *, size_t, size_t *);
int psa_hash_abort(psa_hash_operation_t *);
int psa_hash_compute(int, const uint8_t *, size_t, uint8_t *, size_t, size_t *);
typedef int nvs_handle_t;
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_i64(nvs_handle_t, const char *, int64_t *);
esp_err_t nvs_get_u32(nvs_handle_t, const char *, uint32_t *);
esp_err_t nvs_set_i64(nvs_handle_t, const char *, int64_t);
esp_err_t nvs_set_u32(nvs_handle_t, const char *, uint32_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
typedef struct { uint32_t address; uint32_t size; } esp_partition_t;
typedef struct { char version[32]; char project_name[32]; } esp_app_desc_t;
typedef int esp_ota_handle_t;
typedef enum { ESP_OTA_IMG_UNDEFINED, ESP_OTA_IMG_PENDING_VERIFY } esp_ota_img_states_t;
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *);
esp_err_t esp_ota_begin(const esp_partition_t *, size_t, esp_ota_handle_t *);
esp_err_t esp_ota_write(esp_ota_handle_t, const void *, size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *, esp_app_desc_t *);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *, esp_ota_img_states_t *);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
const esp_app_desc_t *esp_app_get_description(void);
void esp_restart(void);
bool wifi_manager_is_connected(void);
bool board_hal_is_usb_connected(void);
int board_hal_get_battery_voltage(void);
int esp_crt_bundle_attach(void *);
typedef struct Http *esp_http_client_handle_t;
typedef struct {
    const char *url;
    int timeout_ms;
    int (*crt_bundle_attach)(void *);
    bool disable_auto_redirect;
    int buffer_size;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_open(esp_http_client_handle_t, int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int64_t esp_http_client_get_content_length(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
time_t ota_test_time(time_t *);
#ifdef __cplusplus
}
#endif
