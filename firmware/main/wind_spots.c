#include "wind_spots.h"

#include <string.h>
#ifdef ESP_PLATFORM
#include "nvs.h"
#endif

static installed_configuration_t s_configuration;
static wind_spot_t s_spots[INSTALLED_CONFIGURATION_MAX_SPOTS];
static bool s_loaded;
typedef struct { uint64_t digest; uint32_t index; } selected_spot_t;
#ifndef ESP_PLATFORM
static selected_spot_t s_selected;
#endif

esp_err_t wind_spots_use_configuration(const installed_configuration_t *configuration)
{
    if (!installed_configuration_validate(configuration)) return ESP_ERR_INVALID_ARG;
    s_configuration = *configuration;
    for (size_t i = 0; i <= s_configuration.additional_spot_count; ++i) {
        const installed_spot_t *spot = i == 0 ? &s_configuration.spot : &s_configuration.additional_spots[i - 1].spot;
        s_spots[i] = (wind_spot_t) {
            .id = spot->id, .display_name = spot->display_name,
            .latitude = spot->latitude, .longitude = spot->longitude, .timezone = spot->timezone,
        };
    }
    s_loaded = true;
    return ESP_OK;
}

esp_err_t wind_spots_reload_installed(void)
{
    esp_err_t result = installed_configuration_load(&s_configuration);
    if (result != ESP_OK) return result;
    return wind_spots_use_configuration(&s_configuration);
}

static void ensure_loaded(void)
{
    if (!s_loaded) (void) wind_spots_reload_installed();
}

const char *wind_spots_device_timezone(void)
{
    ensure_loaded();
    return s_configuration.device_timezone;
}

size_t wind_spots_count(void)
{
    ensure_loaded();
    return 1 + s_configuration.additional_spot_count;
}

const wind_spot_t *wind_spots_at(size_t index)
{
    return index < wind_spots_count() ? &s_spots[index] : NULL;
}

size_t wind_spots_offset(size_t current, int direction)
{
    const int64_t count = (int64_t) wind_spots_count();
    return (size_t) (((int64_t)(current % count) + direction % count + count) % count);
}

esp_err_t wind_spots_load_selected(size_t *out_index)
{
    if (!out_index) return ESP_ERR_INVALID_ARG;
    ensure_loaded();
    selected_spot_t selected = {0};
#ifdef ESP_PLATFORM
    nvs_handle_t handle;
    if (nvs_open("wind", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(selected);
        if (nvs_get_blob(handle, "selection", &selected, &size) != ESP_OK || size != sizeof(selected))
            memset(&selected, 0, sizeof(selected));
        nvs_close(handle);
    }
#else
    selected = s_selected;
#endif
    *out_index = selected.digest == installed_configuration_digest(&s_configuration) &&
        selected.index < wind_spots_count() ? selected.index : 0;
    return ESP_OK;
}

esp_err_t wind_spots_store_selected(size_t index)
{
    if (index >= wind_spots_count()) return ESP_ERR_INVALID_ARG;
    const selected_spot_t selected = {.digest = installed_configuration_digest(&s_configuration), .index = index};
#ifdef ESP_PLATFORM
    nvs_handle_t handle;
    esp_err_t result = nvs_open("wind", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_blob(handle, "selection", &selected, sizeof(selected));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
#else
    s_selected = selected;
    return ESP_OK;
#endif
}
