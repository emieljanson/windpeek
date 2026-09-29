#include "wind_support.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "cJSON.h"

#ifdef ESP_PLATFORM
#include "board_hal.h"
#include "config.h"
#include "epaper.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "wifi_manager.h"
static SemaphoreHandle_t s_lock;
#define LOCK() do { if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) != pdTRUE) return ESP_ERR_TIMEOUT; } while (0)
#define UNLOCK() xSemaphoreGive(s_lock)
#else
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#endif
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "host-test"
#endif

#define MAX_RECORD_BYTES 14000u
static char s_directory[192];
static char s_device_id[33];
static uint32_t s_newest, s_storage_errors;
static bool s_ready;
static const char *const KINDS[] = {"boot", "setup-started", "setup-complete", "setup-failed", "refresh-complete", "refresh-failed"};

static void path_for(char *path, size_t size, uint32_t sequence) {
    snprintf(path, size, "%s/support-%02u.json", s_directory, (unsigned)(sequence % WIND_SUPPORT_HISTORY_LIMIT));
}

static cJSON *read_json(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char *data = malloc(MAX_RECORD_BYTES + 1);
    if (!data) { fclose(file); return NULL; }
    size_t size = fread(data, 1, MAX_RECORD_BYTES, file);
    bool valid_size = size < MAX_RECORD_BYTES && !ferror(file);
    fclose(file);
    data[size] = 0;
    cJSON *json = valid_size ? cJSON_ParseWithLengthOpts(data, size + 1, NULL, true) : NULL;
    free(data);
    if (!json) s_storage_errors++;
    return json;
}

static bool write_json(const char *path, cJSON *json) {
    char temporary[256];
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    char *data = cJSON_PrintUnformatted(json);
    if (!data) return false;
    size_t size = strlen(data);
    FILE *file = size < MAX_RECORD_BYTES ? fopen(temporary, "wb") : NULL;
    bool ok = false;
    if (file) {
        ok = fwrite(data, 1, size, file) == size && fflush(file) == 0 && fsync(fileno(file)) == 0;
        if (fclose(file) != 0) ok = false;
        if (ok) ok = rename(temporary, path) == 0;
        if (!ok) unlink(temporary);
    }
    free(data);
    return ok;
}

static bool valid_id(const char *id) {
    if (!id || strlen(id) != 32) return false;
    for (unsigned i = 0; i < 32; ++i)
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
    return true;
}

// Settings only: installed_configuration_t deliberately does not contain Wi-Fi.
static cJSON *configuration_json(const installed_configuration_t *config) {
    if (!config || !installed_configuration_validate(config)) return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON *additional = NULL;
    for (size_t index = 0; index <= config->additional_spot_count; ++index) {
        installed_configuration_single_t spot;
        installed_configuration_get_spot(config, index, &spot);
        cJSON *item = index ? cJSON_CreateObject() : root;
        if (!item) { cJSON_Delete(root); return NULL; }
        if (index) {
            if (!additional) additional = cJSON_AddArrayToObject(root, "additionalSpots");
            if (!additional) { cJSON_Delete(item); cJSON_Delete(root); return NULL; }
            cJSON_AddItemToArray(additional, item);
        }
        cJSON_AddNumberToObject(item, "version", spot.version);
        cJSON_AddNumberToObject(item, "generation", spot.generation);
        cJSON_AddStringToObject(item, "boardId", spot.board_id);
        cJSON_AddStringToObject(item, "deviceTimezone", spot.device_timezone);
        cJSON_AddStringToObject(item, "forecastModel", spot.forecast_model);
        cJSON *location = cJSON_AddObjectToObject(item, "spot");
        cJSON_AddStringToObject(location, "id", spot.spot.id);
        cJSON_AddStringToObject(location, "name", spot.spot.display_name);
        cJSON_AddStringToObject(location, "timezone", spot.spot.timezone);
        cJSON_AddNumberToObject(location, "latitude", spot.spot.latitude);
        cJSON_AddNumberToObject(location, "longitude", spot.spot.longitude);
        const installed_display_configuration_t *d = &spot.display;
        cJSON *display = cJSON_AddObjectToObject(item, "display");
        cJSON_AddBoolToObject(display, "showThreshold", d->show_threshold);
        cJSON_AddNumberToObject(display, "threshold", d->threshold_kt);
        cJSON_AddBoolToObject(display, "showWeather", d->show_weather);
        cJSON_AddBoolToObject(display, "showTemperature", d->show_temperature);
        cJSON_AddBoolToObject(display, "showTide", d->show_tide);
        cJSON_AddBoolToObject(display, "showDedicatedFooter", d->show_dedicated_footer);
        cJSON_AddStringToObject(display, "timeFormat", d->use_24_hour ? "24-hour" : "12-hour");
        cJSON_AddStringToObject(display, "temperatureUnit", d->temperature_fahrenheit ? "fahrenheit" : "celsius");
        const char *sizes[] = {"off", "small", "large"};
        cJSON_AddStringToObject(display, "windSize", sizes[d->wind_size]);
        cJSON_AddStringToObject(display, "swellSize", sizes[d->swell_size]);
        cJSON_AddStringToObject(display, "swellModel", d->swell_model);
        cJSON *order = cJSON_AddArrayToObject(display, "moduleOrder");
        const char *modules[] = {"wind", "swell", "weather", "temperature", "tide"};
        for (unsigned m = 0; m < 5; ++m) cJSON_AddItemToArray(order, cJSON_CreateString(modules[d->module_order[m]]));
    }
    char digest[17];
    snprintf(digest, sizeof(digest), "%016" PRIx64, installed_configuration_digest(config));
    cJSON_AddStringToObject(root, "digest", digest);
    return root;
}

esp_err_t wind_support_init(const char *directory, const char *new_device_id) {
    if (!directory || strlen(directory) >= sizeof(s_directory) || !valid_id(new_device_id)) return ESP_ERR_INVALID_ARG;
#ifdef ESP_PLATFORM
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
#endif
    LOCK();
    s_ready = false; s_newest = 0; s_storage_errors = 0;
    snprintf(s_directory, sizeof(s_directory), "%s", directory);
    char path[256];
    snprintf(path, sizeof(path), "%s/support-id.json", s_directory);
    cJSON *identity = read_json(path);
    const char *saved = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(identity, "deviceId"));
    snprintf(s_device_id, sizeof(s_device_id), "%s", valid_id(saved) ? saved : new_device_id);
    if (!valid_id(saved)) {
        cJSON_Delete(identity);
        identity = cJSON_CreateObject();
        cJSON_AddStringToObject(identity, "deviceId", s_device_id);
        if (!write_json(path, identity)) { cJSON_Delete(identity); UNLOCK(); return ESP_FAIL; }
    }
    cJSON_Delete(identity);
    for (unsigned slot = 0; slot < WIND_SUPPORT_HISTORY_LIMIT; ++slot) {
        path_for(path, sizeof(path), slot);
        cJSON *entry = read_json(path);
        const cJSON *seq = cJSON_GetObjectItemCaseSensitive(entry, "sequence");
        if (cJSON_IsNumber(seq) && seq->valuedouble > 0 && seq->valuedouble <= UINT32_MAX &&
            seq->valuedouble == (uint32_t)seq->valuedouble && (uint32_t)seq->valuedouble % WIND_SUPPORT_HISTORY_LIMIT == slot &&
            seq->valuedouble > s_newest) s_newest = (uint32_t)seq->valuedouble;
        cJSON_Delete(entry);
    }
    s_ready = true;
    UNLOCK();
    return ESP_OK;
}

static esp_err_t record_event(wind_support_kind_t kind, esp_err_t result, unsigned stage,
                              const installed_configuration_t *configuration,
                              const wind_provider_diagnostics_t *forecast,
                              const wind_app_status_t *refresh, bool hardware_ready) {
    if (!s_ready || kind < WIND_SUPPORT_BOOT || kind > WIND_SUPPORT_REFRESH_FAILED) return ESP_ERR_INVALID_STATE;
    LOCK();
    if (s_newest == UINT32_MAX) { UNLOCK(); return ESP_ERR_INVALID_STATE; }
    cJSON *entry = cJSON_CreateObject();
    uint32_t sequence = s_newest + 1;
    cJSON_AddNumberToObject(entry, "sequence", sequence);
    cJSON_AddStringToObject(entry, "kind", KINDS[kind]);
    cJSON_AddStringToObject(entry, "firmwareVersion", FIRMWARE_VERSION);
    cJSON_AddNumberToObject(entry, "timestamp", (double)time(NULL));
    cJSON_AddNumberToObject(entry, "result", result);
    cJSON_AddNumberToObject(entry, "stage", stage);
#ifdef ESP_PLATFORM
    cJSON_AddNumberToObject(entry, "uptimeMs", (double)(esp_timer_get_time() / 1000));
    cJSON_AddNumberToObject(entry, "resetReason", esp_reset_reason());
    cJSON_AddNumberToObject(entry, "wakeReasons", (double)esp_sleep_get_wakeup_causes());
    cJSON_AddBoolToObject(entry, "wifiConnected", wifi_manager_is_connected());
    if (hardware_ready) {
        cJSON_AddNumberToObject(entry, "batteryPercent", board_hal_get_battery_percent());
        cJSON_AddBoolToObject(entry, "usbPowered", board_hal_is_usb_connected());
        const epaper_panel_diagnostics_t panel = epaper_panel_diagnostics_get();
        cJSON_AddNumberToObject(entry, "panelPhase", panel.phase);
        cJSON_AddNumberToObject(entry, "panelWaitMs", panel.wait_ms);
    }
#else
    (void)hardware_ready;
#endif
    if (forecast) {
        cJSON_AddNumberToObject(entry, "httpStatus", forecast->http_status);
        cJSON_AddNumberToObject(entry, "transportError", forecast->perform_result);
        cJSON_AddNumberToObject(entry, "parseError", forecast->parse_result);
    }
    if (refresh) {
        cJSON_AddNumberToObject(entry, "fetchResult", refresh->fetch_result);
        cJSON_AddBoolToObject(entry, "attemptedFetch", refresh->attempted_fetch);
    }
    cJSON *settings = configuration_json(configuration);
    if (settings) cJSON_AddItemToObject(entry, "configuration", settings);
    char path[256]; path_for(path, sizeof(path), sequence);
    bool ok = entry && write_json(path, entry);
    cJSON_Delete(entry);
    if (ok) s_newest = sequence;
    else s_storage_errors++;
    UNLOCK();
    return ok ? ESP_OK : ESP_FAIL;
}

void wind_support_record(wind_support_kind_t kind, esp_err_t result, unsigned stage,
                         const installed_configuration_t *configuration,
                         const wind_provider_diagnostics_t *forecast) {
    (void)record_event(kind, result, stage, configuration, forecast, NULL, true);
}

void wind_support_record_recovery_boot(esp_err_t result, unsigned stage) {
    (void)record_event(WIND_SUPPORT_BOOT, result, stage, NULL, NULL, NULL, false);
}

void wind_support_record_refresh(const wind_app_status_t *status, unsigned last_stage) {
    if (!status || !s_ready) return;
    installed_configuration_t *config = malloc(sizeof(*config));
    const bool have_config = config && installed_configuration_has_setup() &&
        installed_configuration_load(config) == ESP_OK;
    const bool failed = status->stage == WIND_REFRESH_FAILED ||
        (status->attempted_fetch && status->fetch_result != ESP_OK);
    (void)record_event(failed ? WIND_SUPPORT_REFRESH_FAILED : WIND_SUPPORT_REFRESH_COMPLETE,
        status->result, last_stage, have_config ? config : NULL,
        status->attempted_fetch ? &status->forecast : NULL, status, true);
    free(config);
}

esp_err_t wind_support_read(uint32_t sequence, char *response, size_t size) {
    if (!response || !size) return ESP_ERR_INVALID_ARG;
    if (!s_ready) {
        const int written = snprintf(response, size, "{\"status\":\"diagnostics_unavailable\"}");
        return written >= 0 && (size_t)written < size ? ESP_OK : ESP_ERR_INVALID_SIZE;
    }
    LOCK();
    cJSON *root = cJSON_CreateObject();
    if (!sequence) {
        cJSON_AddStringToObject(root, "status", "ok");
        cJSON_AddStringToObject(root, "deviceId", s_device_id);
        cJSON_AddStringToObject(root, "firmwareVersion", FIRMWARE_VERSION);
        cJSON_AddNumberToObject(root, "newestSequence", s_newest);
        cJSON_AddNumberToObject(root, "oldestSequence", s_newest > WIND_SUPPORT_HISTORY_LIMIT ? s_newest - WIND_SUPPORT_HISTORY_LIMIT + 1 : s_newest ? 1 : 0);
        cJSON_AddNumberToObject(root, "storageErrors", s_storage_errors);
        const bool installed = installed_configuration_has_setup();
        cJSON_AddBoolToObject(root, "configurationInstalled", installed);
        installed_configuration_t *config = malloc(sizeof(*config));
        if (installed && config && installed_configuration_load(config) == ESP_OK) {
            cJSON *settings = configuration_json(config);
            if (settings) cJSON_AddItemToObject(root, "configuration", settings);
        }
        free(config);
    } else {
        char path[256]; path_for(path, sizeof(path), sequence);
        cJSON *entry = read_json(path);
        const cJSON *seq = cJSON_GetObjectItemCaseSensitive(entry, "sequence");
        bool found = cJSON_IsNumber(seq) && seq->valuedouble == sequence;
        cJSON_AddStringToObject(root, "status", found ? "ok" : "missing");
        if (found) cJSON_AddItemToObject(root, "entry", entry);
        else cJSON_Delete(entry);
    }
    bool printed = root && size <= INT32_MAX && cJSON_PrintPreallocated(root, response, (int)size, false);
    cJSON_Delete(root);
    UNLOCK();
    if (!printed) { response[0] = 0; return ESP_ERR_INVALID_SIZE; }
    return ESP_OK;
}
