#include <stdbool.h>
#include <sys/time.h>
#include <time.h>

#include "board_hal.h"
#include "config.h"
#include "config_manager.h"
#include "debug_log.h"
#include "display_manager.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hardware_profile.h"
#include "nvs_flash.h"
#include "power_manager.h"
#include "storage.h"
#include "wifi_manager.h"
#include "wind_app.h"
#include "wind_clock.h"
#include "wind_installer_service.h"

static const char *TAG = "windpeek";
static volatile bool s_time_synchronized;

static bool hardware_profile_allows_panel(void)
{
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1002
    return hardware_profile_can_use_panel_for_fixed_model(HARDWARE_MODEL_E1002);
#elif defined(CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003)
    return hardware_profile_can_use_panel_for_fixed_model(HARDWARE_MODEL_E1003);
#else
    return hardware_profile_can_use_panel();
#endif
}

static bool side_button_safe_boot_requested(void)
{
    const uint64_t side_button_mask = (UINT64_C(1) << BOARD_HAL_ROTATE_KEY) |
                                      (UINT64_C(1) << BOARD_HAL_CLEAR_KEY);
    gpio_hold_dis(BOARD_HAL_ROTATE_KEY);
    gpio_hold_dis(BOARD_HAL_CLEAR_KEY);
    const gpio_config_t configuration = {
        .pin_bit_mask = side_button_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&configuration) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    return gpio_get_level(BOARD_HAL_ROTATE_KEY) == 0 &&
           gpio_get_level(BOARD_HAL_CLEAR_KEY) == 0;
}

static void time_sync_notification(struct timeval *time_value)
{
    (void) time_value;
    s_time_synchronized = true;
}

static esp_err_t read_rtc_clock(void *context, time_t *value)
{
    (void) context;
    return board_hal_rtc_get_time(value);
}

static esp_err_t write_system_clock(void *context, time_t seconds)
{
    (void) context;
    const struct timeval value = {.tv_sec = seconds};
    return settimeofday(&value, NULL) == 0 ? ESP_OK : ESP_FAIL;
}

static bool restore_clock_from_rtc(void)
{
    return board_hal_rtc_is_available() &&
           wind_clock_restore_from_rtc(NULL, read_rtc_clock, write_system_clock) == ESP_OK;
}

static esp_err_t synchronize_clock(void)
{
    s_time_synchronized = false;
    esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_time_sync_notification_cb(time_sync_notification);
    esp_sntp_init();
    for (int second = 0; second < 10 && !s_time_synchronized; ++second) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!s_time_synchronized) return ESP_ERR_TIMEOUT;
    time_t now;
    time(&now);
    if (board_hal_rtc_is_available()) {
        (void) board_hal_rtc_set_time(now);
    }
    return ESP_OK;
}

static bool connect_installed_wifi(void)
{
    char ssid[WIFI_SSID_MAX_LEN] = {0};
    char password[WIFI_PASS_MAX_LEN] = {0};
    if (wifi_manager_load_credentials(ssid, password) != ESP_OK || ssid[0] == '\0') {
        ESP_LOGI(TAG, "No installed Wi-Fi configuration; waiting for USB installer");
        return false;
    }
    return wifi_manager_connect_for_refresh(ssid, password) == ESP_OK;
}

static void dashboard_task(void *argument)
{
    (void) argument;
    while (true) {
        const int seconds = wind_app_seconds_until_next_wake();
        vTaskDelay(pdMS_TO_TICKS((seconds > 0 ? seconds : 1) * 1000));
        if (!power_manager_is_installer_active()) {
            if (!wifi_manager_is_connected() && !connect_installed_wifi()) {
                ESP_LOGW(TAG, "Scheduled refresh is offline");
            }
            esp_err_t result = wind_app_refresh(false);
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "Scheduled forecast refresh failed: %s", esp_err_to_name(result));
            }
        }
    }
}

void app_main(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);

    hardware_profile_state_t profile;
    ESP_ERROR_CHECK(hardware_profile_boot(side_button_safe_boot_requested(), &profile));
    ESP_LOGI(TAG, "Hardware profile: stored=%s effective=%s source=%s revision=%lu",
             hardware_model_name(profile.stored_model),
             hardware_model_name(profile.effective_model),
             hardware_profile_source_name(profile.source), (unsigned long) profile.revision);

    ESP_ERROR_CHECK(config_manager_init());
    ESP_ERROR_CHECK(wind_app_configure_runtime());
    debug_log_init();
    ESP_ERROR_CHECK(wifi_manager_init());

    if (!hardware_profile_allows_panel()) {
        ESP_ERROR_CHECK(wind_installer_service_start());
        if (profile.safe_boot_override) {
            ESP_LOGW(TAG, "Side-button recovery active; display remains untouched");
        } else if (profile.driver_failure_latched) {
            ESP_LOGW(TAG, "Display recovery required: model=%s stage=%s error=%s",
                     hardware_model_name(profile.failed_model),
                     hardware_driver_stage_name(profile.failure_stage),
                     esp_err_to_name(profile.failure_error));
        } else {
            ESP_LOGI(TAG, "Hardware profile required; USB installer is available");
        }
        while (true) vTaskDelay(pdMS_TO_TICKS(60000));
    }

#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E100X
    const epaper_hardware_t panel_hardware =
        profile.effective_model == HARDWARE_MODEL_E1001 ? EPAPER_HARDWARE_E1001
                                                       : EPAPER_HARDWARE_E1002;
    ESP_ERROR_CHECK(epaper_select_backend(panel_hardware));
#endif
    result = board_hal_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Display hardware initialization failed: %s",
                 esp_err_to_name(result));
        if (profile.stored_model != HARDWARE_MODEL_UNKNOWN) {
            (void) hardware_profile_record_driver_failure(
                profile.stored_model, HARDWARE_DRIVER_STAGE_INITIALIZE, result);
        }
        // USB recovery must remain available even when the panel or its
        // controller is missing. A fatal check here would reboot forever
        // before the browser installer can reconnect.
        ESP_ERROR_CHECK(wind_installer_service_start());
        while (true) vTaskDelay(pdMS_TO_TICKS(60000));
    }
    ESP_ERROR_CHECK(storage_init());

    result = board_hal_rtc_init();
    if (result != ESP_OK && result != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "RTC initialization failed: %s", esp_err_to_name(result));
    }
    (void) restore_clock_from_rtc();

    ESP_ERROR_CHECK(display_manager_init());
    ESP_ERROR_CHECK(power_manager_init());
    ESP_ERROR_CHECK(wind_installer_service_start());

    const bool connected = connect_installed_wifi();
    if (connected && synchronize_clock() != ESP_OK) {
        ESP_LOGW(TAG, "Clock sync timed out; using the retained RTC clock");
    }

    const int early_seconds = power_manager_get_seconds_until_wake_target();
    if (early_seconds > EARLY_WAKE_TOLERANCE_SEC) {
        power_manager_enter_sleep_with_timer((uint32_t) early_seconds);
    }

    result = wind_app_start();
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Dashboard refresh completed with error: %s", esp_err_to_name(result));
    }

    if (power_manager_get_wakeup_source() == WAKEUP_SOURCE_TIMER) {
        power_manager_enter_sleep();
    }

    xTaskCreate(dashboard_task, "wind_dashboard", 8192, NULL, 5, NULL);
    ESP_LOGI(TAG, "Windpeek ready%s", connected ? "" : " (offline)");
    while (true) vTaskDelay(pdMS_TO_TICKS(60000));
}
