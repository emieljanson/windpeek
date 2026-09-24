#include "storage.h"

#include "config.h"
#include "esp_err.h"
#include "esp_log.h"

#ifdef CONFIG_HAS_SDCARD
#include "sdcard.h"
#endif

#ifdef CONFIG_USE_INTERNAL_FLASH_STORAGE
#include "esp_littlefs.h"
#endif

#include "memfs.h"

static const char *TAG = "storage";
static storage_type_t current_storage_type = STORAGE_TYPE_NONE;

#ifdef CONFIG_USE_INTERNAL_FLASH_STORAGE
static esp_err_t mount_littlefs(void)
{
    ESP_LOGI(TAG, "Initializing LittleFS");

    esp_vfs_littlefs_conf_t conf = {
        .base_path = FS_MOUNT_POINT,
        .partition_label = LITTLEFS_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LittleFS Partition size: total: %d, used: %d", total, used);
    }

    return ESP_OK;
}
#endif

esp_err_t storage_init(void)
{
    esp_err_t ret = ESP_OK;

#ifdef CONFIG_HAS_SDCARD
    // For devices with SD card configured, SD card handles its own init in board_hal_init.
    // We just check if it was successfully mounted.
    if (sdcard_is_mounted()) {
        ESP_LOGI(TAG, "SD Card storage is active");
        current_storage_type = STORAGE_TYPE_SDCARD;
        return ESP_OK;
    }

    ESP_LOGW(TAG, "SD Card not mounted. Attempting fallback...");
#endif

#ifdef CONFIG_USE_INTERNAL_FLASH_STORAGE
    // Fallback or explicit flash storage
    if (mount_littlefs() == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS storage is active");
        current_storage_type = STORAGE_TYPE_LITTLEFS;
        return ESP_OK;
    }
#endif

    // Final fallback to MemFS
    ESP_LOGW(TAG, "No persistent storage available, mounting MemFS at %s", FS_MOUNT_POINT);
    ret = memfs_mount(FS_MOUNT_POINT, 10);
    if (ret == ESP_OK) {
        current_storage_type = STORAGE_TYPE_MEMFS;
    } else {
        ESP_LOGE(TAG, "Failed to mount MemFS fallback!");
    }

    return ret;
}

storage_type_t storage_get_type(void)
{
    return current_storage_type;
}

bool storage_has_persistent_storage(void)
{
    return current_storage_type == STORAGE_TYPE_SDCARD ||
           current_storage_type == STORAGE_TYPE_LITTLEFS;
}

void storage_unmount(void)
{
#ifdef CONFIG_USE_INTERNAL_FLASH_STORAGE
    if (current_storage_type == STORAGE_TYPE_LITTLEFS) {
        ESP_LOGI(TAG, "Unmounting LittleFS before deep sleep");
        esp_vfs_littlefs_unregister(LITTLEFS_PARTITION_LABEL);
    }
#endif
}
