#include "wind_ota.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "wind_clock.h"

static bool copy_string(const cJSON *object, const char *key, char *out, size_t size)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(value) || !value->valuestring || !value->valuestring[0] ||
        strlen(value->valuestring) >= size) return false;
    strcpy(out, value->valuestring);
    return true;
}
static bool relative_path(const char *path)
{
    if (!path[0] || path[0] == '/' || strstr(path, "..") || strstr(path, "//")) return false;
    for (const char *p = path; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '/'))
            return false;
    return true;
}
static bool digest_valid(const char *digest)
{ return strlen(digest) == 64 && strspn(digest, "0123456789abcdef") == 64; }
static bool number(const cJSON *object, const char *key, uint32_t *out)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(value) || !(value->valuedouble >= 0 && value->valuedouble <= UINT32_MAX) ||
        value->valuedouble != (uint32_t)value->valuedouble) return false;
    *out = (uint32_t)value->valuedouble;
    return true;
}
static bool equals(const cJSON *object, const char *key, const char *expected)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) && strcmp(value->valuestring, expected) == 0;
}

esp_err_t wind_ota_parse_pointer(const char *json, wind_ota_pointer_t *out)
{
    if (!json || !out) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    wind_ota_pointer_t pointer = {0};
    bool valid = copy_string(root, "version", pointer.version, sizeof(pointer.version)) &&
        copy_string(root, "manifest", pointer.manifest, sizeof(pointer.manifest)) &&
        copy_string(root, "sha256", pointer.sha256, sizeof(pointer.sha256)) &&
        relative_path(pointer.manifest) && digest_valid(pointer.sha256);
    cJSON_Delete(root);
    if (!valid) return ESP_ERR_INVALID_RESPONSE;
    *out = pointer;
    return ESP_OK;
}

esp_err_t wind_ota_parse_manifest(const char *json, const char *board,
    const wind_ota_pointer_t *pointer, size_t partition_size, wind_ota_release_t *out)
{
    if (!json || !board || !pointer || !out) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    uint32_t schema = 0, layout = 0, protocol = 0, sequence = 0, flash = 0;
    const cJSON *configuration = cJSON_GetObjectItemCaseSensitive(root, "configuration");
    uint32_t minimum = 0, maximum = 0;
    bool valid = number(root, "schemaVersion", &schema) && schema == 1 &&
        number(root, "firmwareLayoutVersion", &layout) && layout == 1 &&
        number(root, "flashSize", &flash) && flash == 32 * 1024 * 1024 &&
        number(root, "otaProtocol", &protocol) && protocol == 1 &&
        number(root, "otaSequence", &sequence) && sequence > 0 &&
        number(configuration, "minimum", &minimum) && minimum <= 5 &&
        number(configuration, "maximum", &maximum) && maximum >= 6 &&
        equals(root, "boardId", board) && equals(root, "chipFamily", "ESP32-S3") &&
        equals(root, "version", pointer->version);
    wind_ota_release_t release = {.sequence = sequence};
    int applications = 0;
    const cJSON *part;
    const cJSON *parts = cJSON_GetObjectItemCaseSensitive(root, "parts");
    valid = valid && cJSON_IsArray(parts);
    cJSON_ArrayForEach(part, parts) {
        if (!equals(part, "kind", "application")) continue;
        ++applications;
        valid = valid && copy_string(part, "file", release.file, sizeof(release.file)) &&
            relative_path(release.file) &&
            copy_string(part, "sha256", release.sha256, sizeof(release.sha256)) &&
            digest_valid(release.sha256) && number(part, "size", &release.size) &&
            release.size >= 256 && release.size <= partition_size &&
            equals(root, "otaApplication", release.file);
    }
    cJSON_Delete(root);
    if (!valid || applications != 1) return ESP_ERR_INVALID_RESPONSE;
    *out = release;
    return ESP_OK;
}

bool wind_ota_should_check(int64_t now, int64_t last_check, bool connected,
    bool installer_active, bool usb, int battery_mv)
{
    return wind_clock_is_valid_unix(now) && connected && !installer_active &&
        (usb || battery_mv >= 3800) &&
        (last_check <= 0 || now < last_check || now - last_check >= 86400);
}

#ifdef ESP_PLATFORM
#include <time.h>
#include "board_hal.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "installed_configuration.h"
#include "psa/crypto.h"
#include "nvs.h"
#include "power_manager.h"
#include "wifi_manager.h"
#include "wind_device_errors.h"

#ifndef WINDPEEK_OTA_SEQUENCE
#define WINDPEEK_OTA_SEQUENCE 0
#endif
#ifndef WIND_OTA_BASE_URL
#define WIND_OTA_BASE_URL "https://windpeek.com/firmware/"
#endif
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
#define OTA_ROOT WIND_OTA_BASE_URL "e1003/"
#else
#define OTA_ROOT WIND_OTA_BASE_URL
#endif

static const char *TAG = "wind_ota";

void wind_ota_finish_boot(bool healthy)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) return;
    if (healthy) {
        (void)esp_ota_mark_app_valid_cancel_rollback();
    } else {
        wind_device_errors_capture(WIND_DEVICE_ERROR_OTA, ESP_FAIL);
        wind_device_errors_flush();
        ESP_LOGE(TAG, "New firmware failed its first dashboard refresh; rolling back");
        (void)esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

static esp_err_t open_download(const char *url, esp_http_client_handle_t *out)
{
    esp_http_client_config_t config = {
        .url = url, .timeout_ms = 5000, .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true, .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t result = esp_http_client_open(client, 0);
    if (result == ESP_OK) {
        if (esp_http_client_fetch_headers(client) < 0 ||
            esp_http_client_get_status_code(client) != 200) result = ESP_ERR_INVALID_RESPONSE;
    }
    if (result != ESP_OK) { esp_http_client_cleanup(client); return result; }
    *out = client;
    return ESP_OK;
}

static esp_err_t get_json(const char *url, char *buffer, size_t capacity, size_t *length)
{
    esp_http_client_handle_t client;
    esp_err_t result = open_download(url, &client);
    if (result != ESP_OK) return result;
    size_t used = 0;
    int64_t deadline = esp_timer_get_time() + INT64_C(15000000);
    while (used < capacity - 1 && esp_timer_get_time() < deadline) {
        int read = esp_http_client_read(client, buffer + used, capacity - 1 - used);
        if (read < 0) { result = ESP_FAIL; break; }
        if (read == 0) break;
        used += read;
    }
    if (!esp_http_client_is_complete_data_received(client) || used == capacity - 1)
        result = ESP_ERR_INVALID_SIZE;
    esp_http_client_cleanup(client);
    buffer[used] = '\0';
    *length = used;
    return result;
}

static bool hash_matches(const uint8_t hash[32], const char *expected)
{
    char hex[65];
    for (size_t i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", hash[i]);
    return strcmp(hex, expected) == 0;
}

static esp_err_t install_application(const char *url, const wind_ota_pointer_t *pointer,
    const wind_ota_release_t *release, const esp_partition_t *partition)
{
    esp_http_client_handle_t client;
    esp_err_t result = open_download(url, &client);
    if (result != ESP_OK) return result;
    int64_t content_length = esp_http_client_get_content_length(client);
    if (content_length >= 0 && content_length != release->size) {
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *buffer = malloc(4096);
    if (!buffer) { esp_http_client_cleanup(client); return ESP_ERR_NO_MEM; }
    esp_ota_handle_t handle = 0;
    result = power_manager_is_installer_active() ? ESP_ERR_INVALID_STATE
        : esp_ota_begin(partition, release->size, &handle);
    bool open = result == ESP_OK;
    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    if (result == ESP_OK && psa_hash_setup(&sha, PSA_ALG_SHA_256) != PSA_SUCCESS) result = ESP_FAIL;
    size_t received = 0;
    int64_t deadline = esp_timer_get_time() + INT64_C(120000000);
    while (result == ESP_OK && received < release->size) {
        if (power_manager_is_installer_active() || esp_timer_get_time() >= deadline) {
            result = ESP_ERR_TIMEOUT; break;
        }
        int count = esp_http_client_read(client, (char *)buffer, 4096);
        if (count <= 0 || received + count > release->size) {
            result = ESP_ERR_INVALID_SIZE; break;
        }
        if (psa_hash_update(&sha, buffer, count) != PSA_SUCCESS) { result = ESP_FAIL; break; }
        result = esp_ota_write(handle, buffer, count);
        received += count;
    }
    uint8_t digest[32];
    size_t digest_size = 0;
    if (result == ESP_OK && (psa_hash_finish(&sha, digest, sizeof(digest), &digest_size) != PSA_SUCCESS ||
        digest_size != sizeof(digest) ||
        !hash_matches(digest, release->sha256))) result = ESP_ERR_INVALID_CRC;
    // The exact hash-verified file must end here, even with chunked transfer.
    if (result == ESP_OK && (esp_http_client_read(client, (char *)buffer, 1) != 0 ||
        !esp_http_client_is_complete_data_received(client))) result = ESP_ERR_INVALID_SIZE;
    psa_hash_abort(&sha);
    free(buffer);
    esp_http_client_cleanup(client);
    if (result == ESP_OK) { result = esp_ota_end(handle); open = false; }
    if (open) esp_ota_abort(handle);
    if (result != ESP_OK) return result;
    esp_app_desc_t description;
    result = esp_ota_get_partition_description(partition, &description);
    if (result != ESP_OK) return result;
    if (strncmp(description.version, pointer->version, sizeof(description.version)) != 0 ||
        strncmp(description.project_name, "windpeek", sizeof(description.project_name)) != 0)
        return ESP_ERR_INVALID_RESPONSE;
    if (power_manager_is_installer_active()) return ESP_ERR_INVALID_STATE;
    return esp_ota_set_boot_partition(partition);
}

void wind_ota_maybe_update(void)
{
    const int64_t now = time(NULL);
    if (!wind_ota_should_check(now, 0, wifi_manager_is_connected(),
        power_manager_is_installer_active(), board_hal_is_usb_connected(),
        board_hal_get_battery_voltage())) return;
    nvs_handle_t nvs;
    if (nvs_open("wind_ota", NVS_READWRITE, &nvs) != ESP_OK) return;
    int64_t last_check = 0;
    uint32_t sequence = 0;
    esp_err_t result = nvs_get_i64(nvs, "last_check", &last_check);
    if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) goto done;
    if (!wind_ota_should_check(now, last_check, true, false, true, 0)) goto done;
    result = nvs_get_u32(nvs, "sequence", &sequence);
    if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) goto done;
#if WINDPEEK_OTA_SEQUENCE > 0
    if (sequence < WINDPEEK_OTA_SEQUENCE) sequence = WINDPEEK_OTA_SEQUENCE;
#endif
    // Persist before network work: a reset or failed download must not loop.
    result = nvs_set_i64(nvs, "last_check", now);
    if (result == ESP_OK) result = nvs_commit(nvs);
    if (result != ESP_OK) goto done;
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (!partition || partition->address == esp_ota_get_running_partition()->address) goto done;
    char *json = malloc(16384);
    if (!json) { result = ESP_ERR_NO_MEM; goto done; }
    size_t length = 0;
    wind_ota_pointer_t pointer;
    wind_ota_release_t release;
    result = get_json(OTA_ROOT "latest.json", json, 16384, &length);
    if (result == ESP_OK) result = wind_ota_parse_pointer(json, &pointer);
    if (result == ESP_OK && strcmp(pointer.version, esp_app_get_description()->version) == 0) {
        free(json); goto done;
    }
    char url[512];
    if (result == ESP_OK) {
        snprintf(url, sizeof(url), OTA_ROOT "%s", pointer.manifest);
        result = get_json(url, json, 16384, &length);
    }
    uint8_t hash[32];
    size_t hash_size = 0;
    if (result == ESP_OK && (psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)json, length,
        hash, sizeof(hash), &hash_size) != PSA_SUCCESS || hash_size != sizeof(hash) ||
        !hash_matches(hash, pointer.sha256))) result = ESP_ERR_INVALID_CRC;
    if (result == ESP_OK)
        result = wind_ota_parse_manifest(json, WINDPEEK_BOARD_ID, &pointer, partition->size, &release);
    free(json);
    if (result == ESP_OK && release.sequence > sequence) {
        char directory[sizeof(pointer.manifest)];
        strcpy(directory, pointer.manifest);
        char *slash = strrchr(directory, '/');
        if (slash) slash[1] = '\0'; else directory[0] = '\0';
        snprintf(url, sizeof(url), OTA_ROOT "%s%s", directory, release.file);
        ESP_LOGI(TAG, "Installing verified release %s in inactive partition", pointer.version);
        result = install_application(url, &pointer, &release, partition);
        if (result == ESP_OK) {
            // Even if this write fails, the new binary's embedded sequence prevents downgrade.
            (void)nvs_set_u32(nvs, "sequence", release.sequence);
            (void)nvs_commit(nvs);
            nvs_close(nvs);
            ESP_LOGI(TAG, "OTA verified; restarting");
            esp_restart();
            return;
        }
    }
done:
    nvs_close(nvs);
    if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "OTA check failed: %s", esp_err_to_name(result));
        wind_device_errors_capture(WIND_DEVICE_ERROR_OTA, result);
        wind_device_errors_flush();
    }
}
#else
void wind_ota_maybe_update(void) {}
void wind_ota_finish_boot(bool healthy) { (void)healthy; }
#endif
