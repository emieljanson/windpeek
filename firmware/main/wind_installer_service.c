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
    service->completion_ack_required = false;
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
              "\"configuration\",\"render-verification\",\"clock-sync\",\"completion-ack\","
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
              "\"configuration\",\"render-verification\",\"clock-sync\",\"completion-ack\"]}",
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
    const bool wifi_configured = installed_configuration_has_setup();
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
            ",\"resetReason\":%" PRIu32 ",\"uptimeMs\":%" PRIu32
            ",\"transportError\":%d,\"httpStatus\":%d,\"parseError\":%d"
            ",\"responseBytes\":%u,\"responseTooLarge\":%u,\"allocationFailed\":%u"
            ",\"deviceTime\":%" PRId64 ",\"internalLargestBytes\":%u"
            ",\"refreshStage\":%u,\"refreshError\":%d,\"refreshFetchError\":%d"
            ",\"refreshAttemptedFetch\":%u,\"refreshHttpStatus\":%d"
            ",\"refreshTransportError\":%d,\"refreshParseError\":%d}",
            health.stage, health.heap, health.minimum_heap, health.stack,
            health.reset_reason, health.uptime_ms,
            health.forecast.perform_result, health.forecast.http_status, health.forecast.parse_result,
            (unsigned)health.forecast.response_length, (unsigned)health.forecast.too_large,
            (unsigned)health.forecast.allocation_failed, health.device_time, (unsigned)health.internal_largest_bytes,
            (unsigned)health.refresh.stage, health.refresh.result, health.refresh.fetch_result,
            (unsigned)health.refresh.attempted_fetch, health.refresh.forecast.http_status,
            health.refresh.forecast.perform_result, health.refresh.forecast.parse_result);
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
            service->completion_ack_required = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(request, "completionAck"));
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
            result = write_response(response, response_size, "complete", NULL);
            wind_installer_service_complete_apply(service, true);
        }
    } else if (strcmp(command->valuestring, "finish_setup") == 0) {
        if (service->candidate_staged) {
            result = write_response(response, response_size, "setup_incomplete", NULL);
        } else {
            finish_session(service);
            result = write_response(response, response_size, "finished", NULL);
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

bool wind_installer_service_check_idle(wind_installer_service_t *service,
                                        bool usb_connected, int64_t idle_us)
{
    if (!service || !service->wake_lock_held || idle_us <= INT64_C(120000000) ||
        apply_in_progress(service)) return false;
    // Human input has no USB deadline. Once the setup is committed, however,
    // a vanished browser must not suppress scheduled refreshes forever.
    const char *state = service->dependencies.apply_state
        ? service->dependencies.apply_state(service->dependencies.context) : NULL;
    if (service->completion_ack_required && !service->candidate_staged && state &&
        strcmp(state, "complete") == 0) {
        finish_session(service);
        return true;
    }
    if (!usb_connected) {
        wind_installer_service_timeout(service);
        return true;
    }
    return false;
}


void wind_installer_service_complete_apply(wind_installer_service_t *service, bool succeeded)
{
    if (!service) return;
    service->apply_start_pending = false;
    if (succeeded) {
        service->candidate_staged = false;
        // A capable browser acknowledges only after reading the completed
        // digest/Wi-Fi/panel state. Keep UART and the boot wait alive until then.
        if (service->completion_ack_required) clear_credentials(service);
        else finish_session(service);
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
