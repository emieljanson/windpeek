#include "wind_installer_service.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "wind_clock.h"
#include "wind_usb_protocol.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "development"
#endif

static void clear_credentials(wind_installer_service_t *service)
{
    volatile unsigned char *cursor = (volatile unsigned char *) service->password;
    for (size_t index = 0; index < sizeof(service->password); ++index) cursor[index] = 0;
    memset(service->ssid, 0, sizeof(service->ssid));
    service->credentials_cleared = true;
    service->wifi_ready = false;
}

static void set_wake_lock(wind_installer_service_t *service, bool held)
{
    if (service->wake_lock_held == held) return;
    service->wake_lock_held = held;
    if (service->dependencies.set_wake_lock) {
        service->dependencies.set_wake_lock(service->dependencies.context, held);
    }
}

static void finish_session(wind_installer_service_t *service)
{
    clear_credentials(service);
    set_wake_lock(service, false);
}

static void abort_session(wind_installer_service_t *service)
{
    if (service->dependencies.abort) {
        service->dependencies.abort(service->dependencies.context);
    }
    finish_session(service);
}

static void rollback_candidate(wind_installer_service_t *service)
{
    if (service->dependencies.abort) {
        service->dependencies.abort(service->dependencies.context);
    }
    clear_credentials(service);
}

static bool apply_in_progress(const wind_installer_service_t *service)
{
    if (!service || !service->dependencies.apply_state) return false;
    const char *state = service->dependencies.apply_state(service->dependencies.context);
    return state && strcmp(state, "applying") == 0;
}

void wind_installer_service_init(wind_installer_service_t *service,
                                 const wind_installer_dependencies_t *dependencies)
{
    if (!service) return;
    memset(service, 0, sizeof(*service));
    if (dependencies) service->dependencies = *dependencies;
    service->credentials_cleared = true;
}

static bool copy_json_string(const cJSON *object, const char *key, char *output, size_t size)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(value) || !value->valuestring || strlen(value->valuestring) >= size) {
        return false;
    }
    memcpy(output, value->valuestring, strlen(value->valuestring) + 1);
    return true;
}

static bool json_bool(const cJSON *object, const char *key, bool *output)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsBool(value)) return false;
    *output = cJSON_IsTrue(value);
    return true;
}

static bool object_has_only_keys(const cJSON *object, const char *const *keys, size_t key_count)
{
    if (!cJSON_IsObject(object)) return false;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, object)
    {
        bool known = false;
        for (size_t index = 0; index < key_count; ++index) {
            if (item->string && strcmp(item->string, keys[index]) == 0) {
                known = true;
                break;
            }
        }
        if (!known) return false;
    }
    return true;
}

static bool valid_spot_id(const char *value)
{
    const size_t length = value ? strlen(value) : 0;
    if (length == 0 || length > 64) return false;
    for (size_t index = 0; index < length; ++index) {
        const unsigned char character = (unsigned char) value[index];
        const bool lower_alphanumeric = (character >= 'a' && character <= 'z') ||
                                        (character >= '0' && character <= '9');
        if (!lower_alphanumeric && character != '-') return false;
        if ((index == 0 || index == length - 1) && !lower_alphanumeric) return false;
    }
    return true;
}

static bool valid_digest(const char *value)
{
    if (!value || strlen(value) != 16) return false;
    for (size_t index = 0; index < 16; ++index) {
        if (!(value[index] >= '0' && value[index] <= '9') &&
            !(value[index] >= 'a' && value[index] <= 'f')) return false;
    }
    return true;
}

static bool parse_single_configuration(const cJSON *json, installed_configuration_t *configuration,
                                char digest_text[17])
{
    if (!cJSON_IsObject(json)) return false;
    static const char *const root_keys[] = {
        "version", "boardId", "deviceTimezone", "spot", "forecastModel", "display", "digest", "additionalSpots",
    };
    static const char *const spot_keys[] = {
        "id", "name", "latitude", "longitude", "timezone",
    };
    static const char *const display_keys[] = {
        "showThreshold", "threshold", "showWeather", "showTemperature", "showTide",
        "showDedicatedFooter", "timeFormat", "temperatureUnit", "windSize", "swellSize", "moduleOrder", "swellModel",
    };
    memset(configuration, 0, sizeof(*configuration));
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
    const cJSON *spot = cJSON_GetObjectItemCaseSensitive(json, "spot");
    const cJSON *display = cJSON_GetObjectItemCaseSensitive(json, "display");
    if (!cJSON_IsNumber(version) || !object_has_only_keys(json, root_keys, 8) ||
        !object_has_only_keys(spot, spot_keys, 5) ||
        !object_has_only_keys(display, display_keys, 12) ||
        !copy_json_string(json, "boardId", configuration->board_id,
                          sizeof(configuration->board_id)) ||
        !copy_json_string(json, "deviceTimezone", configuration->device_timezone,
                          sizeof(configuration->device_timezone)) ||
        !copy_json_string(json, "forecastModel", configuration->forecast_model,
                          sizeof(configuration->forecast_model)) ||
        !copy_json_string(json, "digest", digest_text, 17) ||
        !copy_json_string(spot, "id", configuration->spot.id, sizeof(configuration->spot.id)) ||
        !copy_json_string(spot, "name", configuration->spot.display_name,
                          sizeof(configuration->spot.display_name)) ||
        !copy_json_string(spot, "timezone", configuration->spot.timezone,
                          sizeof(configuration->spot.timezone))) return false;
    const cJSON *latitude = cJSON_GetObjectItemCaseSensitive(spot, "latitude");
    const cJSON *longitude = cJSON_GetObjectItemCaseSensitive(spot, "longitude");
    const cJSON *threshold = cJSON_GetObjectItemCaseSensitive(display, "threshold");
    char time_format[16];
    char temperature_unit[16];
    if (!cJSON_IsNumber(latitude) || !cJSON_IsNumber(longitude) || !cJSON_IsNumber(threshold) ||
        !copy_json_string(display, "timeFormat", time_format, sizeof(time_format)) ||
        !copy_json_string(display, "temperatureUnit", temperature_unit,
                          sizeof(temperature_unit)) ||
        !json_bool(display, "showThreshold", &configuration->display.show_threshold) ||
        !json_bool(display, "showWeather", &configuration->display.show_weather) ||
        !json_bool(display, "showTemperature", &configuration->display.show_temperature) ||
        !json_bool(display, "showTide", &configuration->display.show_tide) ||
        !json_bool(display, "showDedicatedFooter",
                   &configuration->display.show_dedicated_footer)) return false;
    if ((version->valuedouble != INSTALLED_CONFIGURATION_VERSION &&
         version->valuedouble != INSTALLED_CONFIGURATION_MULTI_VERSION) ||
        !valid_spot_id(configuration->spot.id) || strlen(configuration->spot.timezone) < 3 ||
        !valid_digest(digest_text) ||
        (strcmp(time_format, "24-hour") != 0 && strcmp(time_format, "12-hour") != 0) ||
        (strcmp(temperature_unit, "celsius") != 0 &&
         strcmp(temperature_unit, "fahrenheit") != 0) ||
        threshold->valuedouble < 0 || threshold->valuedouble > 99 ||
        threshold->valuedouble != (double) (uint8_t) threshold->valuedouble) return false;
    configuration->version = (uint32_t) version->valuedouble;
    configuration->generation = 1;
    configuration->spot.latitude = latitude->valuedouble;
    configuration->spot.longitude = longitude->valuedouble;
    configuration->display.threshold_kt = (uint8_t) threshold->valuedouble;
    configuration->display.use_24_hour = strcmp(time_format, "24-hour") == 0;
    configuration->display.temperature_fahrenheit = strcmp(temperature_unit, "fahrenheit") == 0;
    if (!copy_json_string(display, "swellModel", configuration->display.swell_model, sizeof(configuration->display.swell_model))) return false;
    const char *sizes[] = { "off", "small", "large" };
    const char *modules[] = { "wind", "swell", "weather", "temperature", "tide" };
    char wind_size[8], swell_size[8];
    if (!copy_json_string(display, "windSize", wind_size, sizeof(wind_size)) ||
        !copy_json_string(display, "swellSize", swell_size, sizeof(swell_size))) return false;
    configuration->display.wind_size = configuration->display.swell_size = 255;
    for (int i = 0; i < 3; ++i) {
        if (!strcmp(wind_size, sizes[i])) configuration->display.wind_size = i;
        if (!strcmp(swell_size, sizes[i])) configuration->display.swell_size = i;
    }
    const cJSON *order = cJSON_GetObjectItemCaseSensitive(display, "moduleOrder");
    if (!cJSON_IsArray(order) || cJSON_GetArraySize(order) != 5) return false;
    for (int i = 0; i < 5; ++i) {
        const cJSON *item = cJSON_GetArrayItem(order, i);
        if (!cJSON_IsString(item)) return false;
        configuration->display.module_order[i] = 255;
        for (int j = 0; j < 5; ++j) if (!strcmp(item->valuestring, modules[j])) configuration->display.module_order[i] = j;
    }
    // Validate the single entry before the outer parser attaches the remaining spots.
    const uint32_t parsed_version = configuration->version;
    configuration->version = INSTALLED_CONFIGURATION_VERSION;
    bool valid = installed_configuration_validate(configuration);
    configuration->version = parsed_version;
    return valid;
}

static bool parse_configuration(const cJSON *json, installed_configuration_t *configuration,
                                char digest_text[17])
{
    if (!parse_single_configuration(json, configuration, digest_text)) return false;
    const cJSON *entries = cJSON_GetObjectItemCaseSensitive(json, "additionalSpots");
    if (configuration->version == INSTALLED_CONFIGURATION_VERSION) return entries == NULL;
    if (!cJSON_IsArray(entries) || cJSON_GetArraySize(entries) < 1 ||
        cJSON_GetArraySize(entries) >= INSTALLED_CONFIGURATION_MAX_SPOTS) return false;
    installed_configuration_t *entry = calloc(1, sizeof(*entry));
    if (!entry) return false;
    bool valid = true;
    configuration->additional_spot_count = cJSON_GetArraySize(entries);
    for (size_t i = 0; i < configuration->additional_spot_count; ++i) {
        const cJSON *json_entry = cJSON_GetArrayItem(entries, i);
        char entry_digest[17], expected[17];
        if (!parse_single_configuration(json_entry, entry, entry_digest) ||
            entry->version != INSTALLED_CONFIGURATION_VERSION ||
            cJSON_GetObjectItemCaseSensitive(json_entry, "additionalSpots")) { valid = false; break; }
        snprintf(expected, sizeof(expected), "%016" PRIx64, installed_configuration_digest(entry));
        if (strcmp(expected, entry_digest)) { valid = false; break; }
        installed_configuration_get_spot(entry, 0, &configuration->additional_spots[i]);
    }
    free(entry);
    return valid && installed_configuration_validate(configuration);
}

static esp_err_t write_response(char *response, size_t response_size, const char *status,
                                const char *detail)
{
    if (!response || response_size == 0) return ESP_ERR_INVALID_ARG;
    int written = detail ? snprintf(response, response_size,
                                    "{\"status\":\"%s\",\"detail\":\"%s\"}", status, detail)
                         : snprintf(response, response_size, "{\"status\":\"%s\"}", status);
    return written >= 0 && (size_t) written < response_size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static const char *hardware_model_json_name(hardware_model_t model)
{
    if (model == HARDWARE_MODEL_E1001) return "e1001";
    if (model == HARDWARE_MODEL_E1002) return "e1002";
    return "unknown";
}

static esp_err_t handle_hello(wind_installer_service_t *service, char *response,
                              size_t response_size)
{
    hardware_profile_state_t profile = {0};
    const bool has_hardware_profile = service->dependencies.get_hardware_profile &&
        service->dependencies.get_hardware_profile(service->dependencies.context, &profile) == ESP_OK;
    const char *hardware_model = hardware_model_json_name(profile.effective_model);
    const char *stored_hardware_model = hardware_model_json_name(profile.stored_model);
    const int written = has_hardware_profile
        ? snprintf(
              response, response_size,
              "{\"status\":\"ok\",\"boardId\":\"%s\",\"firmwareVersion\":\"%s\","
              "\"protocolVersion\":1,\"firmwareLayoutVersion\":1,"
              "\"configurationVersion\":%u,\"capabilities\":[\"state\",\"wifi\","
              "\"configuration\",\"render-verification\",\"clock-sync\","
              "\"hardware-profile\"],\"hardwareModel\":\"%s\","
              "\"storedHardwareModel\":\"%s\",\"hardwareProfileRevision\":%" PRIu32 ","
              "\"safeBootOverride\":%s,\"driverFailureLatched\":%s}",
              WINDPEEK_BOARD_ID, FIRMWARE_VERSION, INSTALLED_CONFIGURATION_VERSION,
              hardware_model, stored_hardware_model, profile.revision,
              profile.safe_boot_override ? "true" : "false",
              profile.driver_failure_latched ? "true" : "false")
        : snprintf(
              response, response_size,
              "{\"status\":\"ok\",\"boardId\":\"%s\",\"firmwareVersion\":\"%s\","
              "\"protocolVersion\":1,\"firmwareLayoutVersion\":1,"
              "\"configurationVersion\":%u,\"capabilities\":[\"state\",\"wifi\","
              "\"configuration\",\"render-verification\",\"clock-sync\"]}",
              WINDPEEK_BOARD_ID, FIRMWARE_VERSION, INSTALLED_CONFIGURATION_VERSION);
    return written >= 0 && (size_t) written < response_size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static bool hardware_profile_allows_setup(wind_installer_service_t *service)
{
    if (!service->dependencies.get_hardware_profile) return true;
    hardware_profile_state_t profile = {0};
    if (service->dependencies.get_hardware_profile(service->dependencies.context, &profile) !=
        ESP_OK) return false;
    return (profile.effective_model == HARDWARE_MODEL_E1001 ||
            profile.effective_model == HARDWARE_MODEL_E1002) &&
           !profile.safe_boot_override && !profile.driver_failure_latched;
}

static bool command_requires_hardware_profile(const char *command)
{
    return strcmp(command, "scan_networks") == 0 ||
           strcmp(command, "stage_configuration") == 0 ||
           strcmp(command, "test_wifi") == 0 ||
           strcmp(command, "apply_configuration") == 0;
}

static esp_err_t handle_set_hardware_profile(wind_installer_service_t *service,
                                             const cJSON *request, char *response,
                                             size_t response_size)
{
    if (!service->dependencies.select_hardware_profile) {
        return write_response(response, response_size, "hardware_profile_unsupported", NULL);
    }
    static const char *const keys[] = {
        "command", "hardwareModel", "expectedRevision",
    };
    const cJSON *model = cJSON_GetObjectItemCaseSensitive(request, "hardwareModel");
    const cJSON *revision = cJSON_GetObjectItemCaseSensitive(request, "expectedRevision");
    if (!object_has_only_keys(request, keys, 3) || !cJSON_IsString(model) ||
        !cJSON_IsNumber(revision) || !isfinite(revision->valuedouble) ||
        revision->valuedouble < 0 || revision->valuedouble > UINT32_MAX ||
        revision->valuedouble != (double) (uint32_t) revision->valuedouble) {
        return write_response(response, response_size, "hardware_profile_rejected", NULL);
    }
    hardware_model_t selected = HARDWARE_MODEL_UNKNOWN;
    if (strcmp(model->valuestring, "e1001") == 0) selected = HARDWARE_MODEL_E1001;
    else if (strcmp(model->valuestring, "e1002") == 0) selected = HARDWARE_MODEL_E1002;
    else return write_response(response, response_size, "hardware_profile_rejected", NULL);

    hardware_profile_update_result_t update = {0};
    const esp_err_t result = service->dependencies.select_hardware_profile(
        service->dependencies.context, selected, (uint32_t) revision->valuedouble, &update);
    if (result == ESP_ERR_INVALID_STATE) {
        return write_response(response, response_size, "hardware_profile_conflict", NULL);
    }
    if (result != ESP_OK) {
        return write_response(response, response_size, "hardware_profile_save_failed", NULL);
    }
    const int written = snprintf(
        response, response_size,
        "{\"status\":\"%s\",\"hardwareProfileRevision\":%" PRIu32 "}",
        update.reboot_required ? "reboot_required" : "hardware_profile_saved",
        update.committed_revision);
    return written >= 0 && (size_t) written < response_size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t handle_state(wind_installer_service_t *service, char *response,
                              size_t response_size)
{
    // Configurations contain up to ten spots. Keep them off the 16 KiB UART
    // stack, including while loading/migrating the persisted record.
    installed_configuration_t *active = malloc(sizeof(*active));
    if (!active) return write_response(response, response_size, "state_unavailable", NULL);
    esp_err_t result = installed_configuration_load(active);
    const uint64_t digest = result == ESP_OK ? installed_configuration_digest(active) : 0;
    free(active);
    if (result != ESP_OK) return write_response(response, response_size, "state_unavailable", NULL);
    char configured_ssid[WIND_INSTALLER_SSID_MAX + 1] = {0};
    char configured_password[WIND_INSTALLER_PASSWORD_MAX + 1] = {0};
    const bool wifi_configured = installed_configuration_load_credentials(
        configured_ssid, sizeof(configured_ssid), configured_password,
        sizeof(configured_password)) == ESP_OK;
    memset(configured_ssid, 0, sizeof(configured_ssid));
    memset(configured_password, 0, sizeof(configured_password));
    const char *apply = service->dependencies.apply_state
                            ? service->dependencies.apply_state(service->dependencies.context)
                            : "idle";
    int written = snprintf(
        response, response_size,
        "{\"status\":\"ok\",\"boardId\":\"%s\",\"configurationDigest\":"
        "\"%016" PRIx64 "\",\"wifi\":\"%s\",\"wifiConfigured\":%s,"
        "\"render\":\"%s\",\"apply\":\"%s\",\"applyError\":%d}",
        WINDPEEK_BOARD_ID, digest,
        service->dependencies.wifi_connected &&
                service->dependencies.wifi_connected(service->dependencies.context)
            ? "connected" : "disconnected",
        wifi_configured ? "true" : "false",
        service->dependencies.render_succeeded &&
                service->dependencies.render_succeeded(service->dependencies.context)
            ? "valid" : "pending",
        apply ? apply : "idle",
        service->dependencies.apply_error
            ? service->dependencies.apply_error(service->dependencies.context) : ESP_OK);
    if (written < 1 || (size_t) written >= response_size) return ESP_ERR_INVALID_SIZE;
    if (service->dependencies.health) {
        wind_installer_health_t health = {0};
        service->dependencies.health(service->dependencies.context, &health);
        const size_t offset = (size_t) written - 1;
        written = snprintf(response + offset, response_size - offset,
            ",\"deviceStage\":%" PRIu32 ",\"freeHeap\":%" PRIu32
            ",\"minimumHeap\":%" PRIu32 ",\"taskStackFree\":%" PRIu32
            ",\"resetReason\":%" PRIu32 ",\"uptimeMs\":%" PRIu32 "}",
            health.stage, health.heap, health.minimum_heap, health.stack,
            health.reset_reason, health.uptime_ms);
        return written >= 0 && (size_t) written < response_size - offset
            ? ESP_OK : ESP_ERR_INVALID_SIZE;
    }
    return written >= 0 && (size_t) written < response_size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t set_clock_from_request(wind_installer_service_t *service,
                                        const cJSON *request)
{
    const cJSON *unix_time = cJSON_GetObjectItemCaseSensitive(request, "unixTime");
    if (!cJSON_IsNumber(unix_time) || !service->dependencies.set_clock ||
        unix_time->valuedouble != (double) (int64_t) unix_time->valuedouble ||
        !wind_clock_is_valid_unix((int64_t) unix_time->valuedouble)) {
        return ESP_ERR_INVALID_ARG;
    }
    return service->dependencies.set_clock(service->dependencies.context,
                                           (int64_t) unix_time->valuedouble);
}

static esp_err_t handle_stage_configuration(wind_installer_service_t *service,
                                             const cJSON *request, char *response,
                                             size_t response_size)
{
    // Stack locals in the command dispatcher consume space even for hello and
    // get_state. Allocate staging's two full configurations only on this path.
    installed_configuration_t *candidate = malloc(sizeof(*candidate));
    installed_configuration_t *active = malloc(sizeof(*active));
    if (!candidate || !active) {
        free(candidate);
        free(active);
        return ESP_ERR_NO_MEM;
    }
    const cJSON *json = cJSON_GetObjectItemCaseSensitive(request, "configuration");
    char supplied_digest[17] = {0};
    esp_err_t result;
    if (!parse_configuration(json, candidate, supplied_digest)) {
        result = write_response(response, response_size, "configuration_rejected", NULL);
    } else {
        char calculated_digest[17];
        snprintf(calculated_digest, sizeof(calculated_digest), "%016" PRIx64,
                 installed_configuration_digest(candidate));
        if (strcmp(calculated_digest, supplied_digest) != 0) {
            result = write_response(response, response_size, "digest_mismatch", NULL);
        } else {
            result = installed_configuration_load(active);
            if (result == ESP_OK) {
                candidate->generation = active->generation + 1;
                service->candidate = *candidate;
                service->candidate_staged = true;
                result = write_response(response, response_size, "configuration_staged",
                                        calculated_digest);
            }
        }
    }
    free(active);
    free(candidate);
    return result;
}

esp_err_t wind_installer_service_handle_json(wind_installer_service_t *service,
                                             const char *payload, size_t payload_length,
                                             char *response, size_t response_size)
{
    if (!service || !payload || payload_length == 0 || payload_length > WIND_USB_MAX_PAYLOAD || !response) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *request = cJSON_ParseWithLength(payload, payload_length);
    if (!request) {
        // The asynchronous render/commit task still owns the candidate and
        // credentials. Serial noise must not clear them underneath that task.
        if (!apply_in_progress(service)) abort_session(service);
        return ESP_ERR_INVALID_ARG;
    }
    const cJSON *command = cJSON_GetObjectItemCaseSensitive(request, "command");
    esp_err_t result = ESP_OK;
    if (!cJSON_IsString(command)) {
        result = ESP_ERR_INVALID_ARG;
    } else if (command_requires_hardware_profile(command->valuestring) &&
               !hardware_profile_allows_setup(service)) {
        result = write_response(response, response_size, "hardware_profile_required", NULL);
    } else if (apply_in_progress(service) && strcmp(command->valuestring, "hello") != 0 &&
               strcmp(command->valuestring, "get_state") != 0) {
        // The render/commit transaction owns the staged configuration and
        // credential buffers until it finishes. A second mutation cannot
        // safely cancel or replace those values mid-apply.
        result = write_response(response, response_size, "apply_busy", NULL);
    } else if (strcmp(command->valuestring, "hello") == 0) {
        // The UART pins do not wake the E1002 reliably from automatic light
        // sleep. Hold the installer wake lock from the first successful
        // handshake, not only after `begin`, so a user can pause on the review
        // or Wi-Fi screen without losing the next command.
        set_wake_lock(service, true);
        result = handle_hello(service, response, response_size);
    } else if (strcmp(command->valuestring, "get_state") == 0) {
        result = handle_state(service, response, response_size);
    } else if (strcmp(command->valuestring, "set_hardware_profile") == 0) {
        result = handle_set_hardware_profile(service, request, response, response_size);
    } else if (strcmp(command->valuestring, "scan_networks") == 0) {
        result = service->dependencies.scan_wifi
                     ? service->dependencies.scan_wifi(service->dependencies.context, response,
                                                       response_size)
                     : write_response(response, response_size, "scan_unavailable", NULL);
    } else if (strcmp(command->valuestring, "begin") == 0) {
        if (set_clock_from_request(service, request) != ESP_OK) {
            result = write_response(response, response_size, "clock_rejected", NULL);
        } else {
            set_wake_lock(service, true);
            result = write_response(response, response_size, "ready", NULL);
        }
    } else if (strcmp(command->valuestring, "stage_configuration") == 0) {
        result = handle_stage_configuration(service, request, response, response_size);
    } else if (strcmp(command->valuestring, "test_wifi") == 0) {
        static const char *const credential_keys[] = {"command", "ssid", "password"};
        if (service->dependencies.abort) {
            service->dependencies.abort(service->dependencies.context);
        }
        clear_credentials(service);
        if (!object_has_only_keys(request, credential_keys, 3) ||
            !copy_json_string(request, "ssid", service->ssid, sizeof(service->ssid)) ||
            !copy_json_string(request, "password", service->password, sizeof(service->password)) ||
            !service->dependencies.test_wifi) {
            clear_credentials(service);
            result = write_response(response, response_size, "wifi_rejected", NULL);
        } else {
            service->credentials_cleared = false;
            esp_err_t wifi_result = service->dependencies.test_wifi(
                service->dependencies.context, service->ssid, service->password);
            service->wifi_ready = wifi_result == ESP_OK;
            if (!service->wifi_ready) clear_credentials(service);
            result = write_response(response, response_size,
                                    service->wifi_ready ? "wifi_ready" : "wifi_rejected", NULL);
        }
    } else if (strcmp(command->valuestring, "apply_configuration") == 0) {
        if (!service->candidate_staged) {
            result = write_response(response, response_size, "configuration_required", NULL);
        } else if (service->dependencies.begin_apply) {
            result = service->dependencies.begin_apply(
                service->dependencies.context, &service->candidate,
                service->wifi_ready ? service->ssid : NULL,
                service->wifi_ready ? service->password : NULL);
            if (result == ESP_OK) {
                service->apply_start_pending = service->dependencies.start_apply != NULL;
                result = write_response(response, response_size, "applying", NULL);
            } else {
                result = write_response(response, response_size, "apply_busy", NULL);
            }
        } else if (service->dependencies.render_candidate &&
                   service->dependencies.render_candidate(service->dependencies.context,
                                                          &service->candidate) != ESP_OK) {
            result = write_response(response, response_size, "render_failed", NULL);
            rollback_candidate(service);
        } else if (!service->dependencies.commit ||
                   service->dependencies.commit(service->dependencies.context, &service->candidate,
                                                service->wifi_ready ? service->ssid : NULL,
                                                service->wifi_ready ? service->password : NULL) != ESP_OK) {
            result = write_response(response, response_size, "commit_failed", NULL);
            rollback_candidate(service);
        } else {
            service->candidate_staged = false;
            result = write_response(response, response_size, "complete", NULL);
            finish_session(service);
        }
    } else if (strcmp(command->valuestring, "cancel") == 0) {
        service->candidate_staged = false;
        abort_session(service);
        result = write_response(response, response_size, "cancelled", NULL);
    } else {
        result = write_response(response, response_size, "unknown_command", NULL);
    }
    cJSON_Delete(request);
    return result;
}

void wind_installer_service_timeout(wind_installer_service_t *service)
{
    if (!service) return;
    if (apply_in_progress(service)) return;
    service->candidate_staged = false;
    abort_session(service);
}

void wind_installer_service_disconnect(wind_installer_service_t *service)
{
    wind_installer_service_timeout(service);
}

void wind_installer_service_complete_apply(wind_installer_service_t *service, bool succeeded)
{
    if (!service) return;
    service->apply_start_pending = false;
    if (succeeded) {
        service->candidate_staged = false;
        finish_session(service);
    } else {
        rollback_candidate(service);
    }
}

esp_err_t wind_installer_service_start_pending_apply(wind_installer_service_t *service)
{
    if (!service || !service->apply_start_pending || !service->dependencies.start_apply) {
        return ESP_ERR_INVALID_STATE;
    }
    service->apply_start_pending = false;
    return service->dependencies.start_apply(service->dependencies.context);
}

esp_err_t wind_installer_service_confirm_pending_apply_response(
    wind_installer_service_t *service, bool response_transmitted)
{
    if (!service || !service->apply_start_pending) return ESP_ERR_INVALID_STATE;
    if (!response_transmitted) {
        wind_installer_service_complete_apply(service, false);
        return ESP_FAIL;
    }
    esp_err_t result = wind_installer_service_start_pending_apply(service);
    if (result != ESP_OK) wind_installer_service_complete_apply(service, false);
    return result;
}

#ifdef ESP_PLATFORM
#include <stdatomic.h>
#include <sys/time.h>

#include "board_hal.h"
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
    atomic_uint diagnostic_stage;
    installed_configuration_t apply_candidate;
    char apply_ssid[WIND_INSTALLER_SSID_MAX + 1];
    char apply_password[WIND_INSTALLER_PASSWORD_MAX + 1];
    bool apply_has_wifi;
    int64_t last_activity_us;
} physical_installer_t;

static physical_installer_t s_physical_installer;
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
    };
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
    if (result == ESP_OK) {
        installer->candidate_wifi_active = true;
        return ESP_OK;
    }
    if (installer->had_previous_wifi) {
        (void) wifi_manager_connect(installer->previous_ssid, installer->previous_password);
    }
    physical_clear_previous_wifi(installer);
    return result;
}

static esp_err_t physical_render(void *context, const installed_configuration_t *candidate)
{
    physical_checkpoint(context, DIAG_PREVIEW_BEGIN);
    esp_err_t result = wind_app_preview_configuration(candidate);
    physical_checkpoint(context, DIAG_PREVIEW_DONE);
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
        physical_clear_apply(installer);
        atomic_store(&installer->apply_error, result);
        if (result == ESP_OK) physical_checkpoint(installer, DIAG_COMPLETE);
        // Publish completion only after cleanup, so the next attempt cannot have
        // its configuration or credentials erased by the previous worker.
        atomic_store(&installer->apply_state, final_state);
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
    atomic_store(&installer->apply_error, ESP_OK);
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
    physical_installer_t *installer = (physical_installer_t *) context;
    if (!s_apply_task) {
        atomic_store(&installer->apply_error, ESP_ERR_NO_MEM);
        atomic_store(&installer->apply_state, PHYSICAL_APPLY_COMMIT_FAILED);
        physical_clear_apply(installer);
        return ESP_ERR_NO_MEM;
    }
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
    (void) context;
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
        if (installer->service.wake_lock_held &&
            atomic_load(&installer->apply_state) != PHYSICAL_APPLY_RUNNING &&
            esp_timer_get_time() - installer->last_activity_us > INT64_C(120000000)) {
            physical_checkpoint(installer, DIAG_IDLE_TIMEOUT);
            wind_installer_service_timeout(&installer->service);
        }
    }
}

esp_err_t wind_installer_service_start(void)
{
    if (s_apply_task) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ESP_OK;
    if (!uart_is_driver_installed(UART_NUM_0)) {
        result = uart_driver_install(UART_NUM_0, 4096, 4096, 0, NULL, 0);
        if (result != ESP_OK) return result;
    }
    memset(&s_physical_installer, 0, sizeof(s_physical_installer));
    atomic_init(&s_physical_installer.apply_state, PHYSICAL_APPLY_IDLE);
    atomic_init(&s_physical_installer.apply_error, ESP_OK);
    atomic_init(&s_physical_installer.diagnostic_stage, DIAG_READY);
    wind_usb_parser_init(&s_physical_installer.parser);
    const wind_installer_dependencies_t dependencies = {
        .context = &s_physical_installer,
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
    wind_installer_service_init(&s_physical_installer.service, &dependencies);
    s_physical_installer.last_activity_us = esp_timer_get_time();
    s_apply_task = xTaskCreateStatic(physical_apply_task, "wind_apply",
        sizeof(s_apply_stack), &s_physical_installer, 6, s_apply_stack, &s_apply_task_buffer);
    if (!s_apply_task) return ESP_ERR_NO_MEM;
    if (xTaskCreate(installer_usb_task, "wind_usb", 16384, &s_physical_installer, 6, NULL) != pdPASS) {
        vTaskDelete(s_apply_task);
        s_apply_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
#endif
