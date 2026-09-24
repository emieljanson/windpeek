#include "config_manager.h"

#include <string.h>

#include "config.h"
#include "esp_log.h"
#include "nvs.h"
#include "wind_timezone.h"

static const char *TAG = "config_manager";
static char s_timezone[TIMEZONE_MAX_LEN] = "UTC";
static wind_display_config_t s_display;
static bool s_debug_log_enabled;

static void load_display(nvs_handle_t handle)
{
    uint32_t version = 0;
    if (nvs_get_u32(handle, NVS_WIND_CONFIG_VERSION_KEY, &version) != ESP_OK ||
        !wind_display_config_stored_version_supported(version)) return;

    wind_display_config_t stored;
    wind_display_config_default(&stored);
    nvs_get_u8(handle, NVS_WIND_DISPLAY_MODE_KEY, &stored.display_mode);
    nvs_get_u8(handle, NVS_WIND_THRESHOLD_KEY, &stored.threshold_kt);
    uint8_t value = 0;
    if (nvs_get_u8(handle, NVS_WIND_WEATHER_KEY, &value) == ESP_OK)
        stored.show_weather = value != 0;
    if (nvs_get_u8(handle, NVS_WIND_TEMPERATURE_KEY, &value) == ESP_OK)
        stored.show_temperature = value != 0;
    if (nvs_get_u8(handle, NVS_WIND_TIDE_KEY, &value) == ESP_OK)
        stored.show_tide = value != 0;
    if (version >= 2u) {
        value = 1;
        if (nvs_get_u8(handle, NVS_WIND_TIME_24_KEY, &value) == ESP_OK)
            stored.use_24_hour = value != 0;
        if (nvs_get_u8(handle, NVS_WIND_TEMP_F_KEY, &value) == ESP_OK)
            stored.temperature_fahrenheit = value != 0;
    }
    if (version >= 3u) {
        value = 1;
        if (nvs_get_u8(handle, NVS_WIND_FOOTER_KEY, &value) == ESP_OK)
            stored.show_dedicated_footer = value != 0;
    }
    if (version >= 4u) {
        nvs_get_u8(handle, "wind_size", &stored.wind_size);
        nvs_get_u8(handle, "swell_size", &stored.swell_size);
        size_t size = sizeof(stored.module_order);
        nvs_get_blob(handle, "module_order", stored.module_order, &size);
    }
    if (wind_display_config_validate(&stored)) s_display = stored;
    else ESP_LOGW(TAG, "Ignoring invalid display configuration");
}

esp_err_t config_manager_init(void)
{
    wind_display_config_default(&s_display);
    esp_err_t result = wind_timezone_init();
    if (result != ESP_OK) return result;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        load_display(handle);
        uint8_t enabled = 0;
        if (nvs_get_u8(handle, NVS_DEBUG_LOG_KEY, &enabled) == ESP_OK)
            s_debug_log_enabled = enabled != 0;
        nvs_close(handle);
    }
    return ESP_OK;
}

bool config_manager_set_timezone_transient(const char *timezone)
{
    if (!timezone || strlen(timezone) >= sizeof(s_timezone) ||
        !wind_timezone_is_supported(timezone)) return false;
    strcpy(s_timezone, timezone);
    return true;
}

const char *config_manager_get_timezone(void)
{
    return s_timezone;
}

bool config_manager_set_wind_display_config(const wind_display_config_t *config)
{
    if (!wind_display_config_validate(config)) return false;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t result = nvs_set_u8(handle, NVS_WIND_DISPLAY_MODE_KEY, config->display_mode);
    if (result == ESP_OK) result = nvs_set_u8(handle, NVS_WIND_THRESHOLD_KEY, config->threshold_kt);
    if (result == ESP_OK) result = nvs_set_u8(handle, NVS_WIND_WEATHER_KEY, config->show_weather ? 1 : 0);
    if (result == ESP_OK) result = nvs_set_u8(handle, NVS_WIND_TEMPERATURE_KEY, config->show_temperature ? 1 : 0);
    if (result == ESP_OK) result = nvs_set_u8(handle, NVS_WIND_TIDE_KEY, config->show_tide ? 1 : 0);
    if (result == ESP_OK) result = nvs_set_u8(handle, NVS_WIND_FOOTER_KEY, config->show_dedicated_footer ? 1 : 0);
    if (result == ESP_OK) result = nvs_set_u8(handle, NVS_WIND_TIME_24_KEY, config->use_24_hour ? 1 : 0);
    if (result == ESP_OK) result = nvs_set_u8(handle, NVS_WIND_TEMP_F_KEY, config->temperature_fahrenheit ? 1 : 0);
    if (result == ESP_OK) result = nvs_set_u8(handle, "wind_size", config->wind_size);
    if (result == ESP_OK) result = nvs_set_u8(handle, "swell_size", config->swell_size);
    if (result == ESP_OK) result = nvs_set_blob(handle, "module_order", config->module_order, sizeof(config->module_order));
    if (result == ESP_OK) result = nvs_set_u32(handle, NVS_WIND_CONFIG_VERSION_KEY, WIND_DISPLAY_CONFIG_VERSION);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK) return false;
    s_display = *config;
    return true;
}

bool config_manager_set_wind_display_config_transient(const wind_display_config_t *config)
{
    if (!wind_display_config_validate(config)) return false;
    s_display = *config;
    return true;
}

wind_display_config_t config_manager_get_wind_display_config(void)
{
    return s_display;
}

void config_manager_set_debug_log_enabled(bool enabled)
{
    s_debug_log_enabled = enabled;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        if (nvs_set_u8(handle, NVS_DEBUG_LOG_KEY, enabled ? 1 : 0) == ESP_OK)
            nvs_commit(handle);
        nvs_close(handle);
    }
}

bool config_manager_get_debug_log_enabled(void)
{
    return s_debug_log_enabled;
}
