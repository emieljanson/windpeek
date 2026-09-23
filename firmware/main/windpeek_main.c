#include <stdbool.h>
#include <sys/time.h>
#include <time.h>

#include "board_hal.h"
#include "config.h"
#include "config_manager.h"
#include "debug_log.h"
#include "display_manager.h"
#include "driver/gpio.h"
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
#include "driver/ledc.h"
#endif
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_attr.h"
#include "nvs.h"
#include "freertos/semphr.h"
#include "wind_battery_policy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "hardware_profile.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_attr.h"
#include "power_manager.h"
#include "storage.h"
#include "wifi_manager.h"
#include "wind_app.h"
#include "wind_clock.h"
#include "wind_navigation.h"
#include "wind_spots.h"
#include "wind_installer_service.h"
#include "wind_battery_policy.h"
#include "esp_timer.h"
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
#include "board_touch.h"
#include "driver/ledc.h"
#include "wind_overview.h"
#endif

static const char *TAG = "windpeek";
static volatile bool s_time_synchronized;
static TaskHandle_t s_dashboard_task;

RTC_DATA_ATTR static bool s_empty_latched;
static bool s_battery_state_loaded;
static SemaphoreHandle_t s_battery_lock;

static esp_err_t store_battery_latch(bool empty)
{
    s_empty_latched = empty;
    nvs_handle_t handle;
    esp_err_t result = nvs_open("wind_battery", NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_u8(handle, "empty", empty);
        if (result == ESP_OK) result = nvs_commit(handle);
        nvs_close(handle);
    }
    if (result != ESP_OK)
        ESP_LOGW(TAG, "Battery state persistence failed: %s", esp_err_to_name(result));
    return result;
}

/* Called only at boot or before existing user/scheduled work, never by a poller. */
static bool battery_allows_work(void)
{
    if (xSemaphoreTake(s_battery_lock, portMAX_DELAY) != pdTRUE) return false;
    if (!s_battery_state_loaded) {
        nvs_handle_t handle;
        if (nvs_open("wind_battery", NVS_READONLY, &handle) == ESP_OK) {
            uint8_t empty = 0;
            if (nvs_get_u8(handle, "empty", &empty) == ESP_OK)
                s_empty_latched = s_empty_latched || empty != 0;
            nvs_close(handle);
        }
        s_battery_state_loaded = true;
    }
    const bool usb = board_hal_is_usb_connected();
    const int millivolts = usb ? -1 : board_hal_get_battery_voltage();
    const wind_battery_action_t action = wind_battery_action(usb, millivolts, s_empty_latched);
    if (action == WIND_BATTERY_RUN) {
        if (s_empty_latched) {
            // Rebuild the forecast even if its data is identical to the old screen.
            (void)wind_app_clear_panel_confirmation();
            store_battery_latch(false);
        }
        power_manager_set_battery_empty(false);
        xSemaphoreGive(s_battery_lock);
        return true;
    }
    power_manager_set_battery_empty(true);
    // USB may have been removed during an interactive session. Stop an already
    // running radio before spending the reserve on the final panel refresh.
    (void)wifi_manager_stop();
    if (action == WIND_BATTERY_RENDER_EMPTY) {
        // Persist the attempt BEFORE the power-hungry refresh. A brownout or
        // failed panel must not cause endless refresh attempts on button wakes.
        ESP_LOGI(TAG, "Battery reserve reached (%d mV); rendering final screen", millivolts);
        const esp_err_t result = wind_battery_render_once(
            &s_empty_latched, store_battery_latch, wind_app_show_battery_empty);
        if (result != ESP_OK)
            ESP_LOGW(TAG, "Final battery screen failed: %s", esp_err_to_name(result));
    }
    power_manager_enter_sleep();
    // Sleep can be cancelled by USB arriving during the refresh. Let the caller
    // retry its existing work path; it will then clear the latch on USB power.
    xSemaphoreGive(s_battery_lock);
    return board_hal_is_usb_connected() ? battery_allows_work() : false;
}

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

#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
static wind_touch_gesture_t s_boot_gesture;
static bool s_have_boot_gesture;
static uint32_t s_boot_release_ms;
static esp_timer_handle_t s_navigation_click_timer;

static void stop_navigation_click(void *argument) {
    (void)argument;
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
}

static void init_navigation_click(void) {
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 1800,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel = {
        .gpio_num = GPIO_NUM_45,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 128,
        .hpoint = 0,
    };
    const esp_timer_create_args_t click_timer = {
        .callback = stop_navigation_click,
        .name = "navigation_click",
    };
    if (ledc_timer_config(&timer) != ESP_OK ||
        ledc_channel_config(&channel) != ESP_OK ||
        esp_timer_create(&click_timer, &s_navigation_click_timer) != ESP_OK) {
        ESP_LOGW(TAG, "Navigation click unavailable");
        s_navigation_click_timer = NULL;
        return;
    }
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
}

static void play_navigation_click(void) {
    if (!s_navigation_click_timer) return;
    (void)esp_timer_stop(s_navigation_click_timer);
    if (ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 128) != ESP_OK ||
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) != ESP_OK) return;
    (void)esp_timer_start_once(s_navigation_click_timer, 45000);
}

static void capture_boot_touch(void) {
    if (board_hal_touch_gesture_wake()) return;
    board_touch_sample_t sample;
    uint32_t started=(uint32_t)(esp_timer_get_time()/1000);
    if (board_hal_touch_read(&sample)!=ESP_OK || sample.contacts!=1) return;
    (void)wind_touch_update(&s_boot_gesture,sample.contacts,
        (uint32_t)sample.x*800/1872,(uint32_t)sample.y*600/1404,started,false,0,1);
    while ((uint32_t)(esp_timer_get_time()/1000)-started<1500) {
        vTaskDelay(pdMS_TO_TICKS(20));
        esp_err_t result=board_hal_touch_read(&sample);
        uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
        if (result==ESP_ERR_NOT_FINISHED) continue;
        if (result!=ESP_OK) return;
        if (!sample.contacts) { s_have_boot_gesture=true; s_boot_release_ms=now; return; }
        (void)wind_touch_update(&s_boot_gesture,sample.contacts,
            (uint32_t)sample.x*800/1872,(uint32_t)sample.y*600/1404,now,false,0,1);
    }
}

static void run_touch_action(wind_touch_action_t action) {
    if (action.kind==WIND_TOUCH_NONE || power_manager_is_installer_active()) return;
    if (!battery_allows_work()) return;
    play_navigation_click();
    power_manager_work_begin();
    power_manager_reset_sleep_timer();
    /* Fetching is lazy, and overview rendering reads only its visible three spots. */
    bool need_network = action.kind==WIND_TOUCH_TOGGLE_DAY ? false : action.kind==WIND_TOUCH_SELECT
        ? wind_app_spot_requires_network(action.spot_index)
        : wind_app_overview_requires_network(action.kind==WIND_TOUCH_NEXT_PAGE ? 1 :
            action.kind==WIND_TOUCH_PREVIOUS_PAGE ? -1 : 0);
    if (need_network && !wifi_manager_is_connected()) (void)connect_installed_wifi();
    esp_err_t result=ESP_OK;
    switch (action.kind) {
        case WIND_TOUCH_OPEN: result=wind_app_show_overview(); break;
        case WIND_TOUCH_TOGGLE_DAY: result=wind_app_toggle_day(action.day_index); break;
        case WIND_TOUCH_SELECT: result=wind_app_select_spot(action.spot_index); break;
        case WIND_TOUCH_NEXT_PAGE: result=wind_app_overview_page(1); break;
        case WIND_TOUCH_PREVIOUS_PAGE: result=wind_app_overview_page(-1); break;
        default: break;
    }
    if (result!=ESP_OK) ESP_LOGW(TAG,"Touch action failed: %s",esp_err_to_name(result));
    else if (s_dashboard_task) xTaskNotifyGive(s_dashboard_task);
    power_manager_reset_sleep_timer();
    power_manager_work_end();
}

static void e1003_input_task(void *argument) {
    (void)argument;
    const gpio_num_t pins[]={BOARD_HAL_ROTATE_KEY,BOARD_HAL_CLEAR_KEY,BOARD_HAL_WAKEUP_KEY};
    bool down[3]={false}, armed[3]={false};
    TickType_t pressed_at[3]={0};
    wind_touch_gesture_t gesture={0};
    /* The second wake tap may still be held after the controller resets. */
    bool discard_until_release=board_hal_touch_gesture_wake();
    uint32_t last_frame=(uint32_t)(esp_timer_get_time()/1000);
    while (true) {
        uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
        bool navigation_ran=false;
        bool held[3];
        for (int i=0;i<3;++i) held[i]=gpio_get_level(pins[i])==0;
        bool blocked=power_manager_is_installer_active() || power_manager_work_active() || (held[0]&&held[1]);
        if (blocked) { gesture=(wind_touch_gesture_t){0}; discard_until_release=true; }
        for (int i=0;i<3;++i) {
            if (blocked) armed[i]=false;
            if (held[i]&&!down[i]) pressed_at[i]=xTaskGetTickCount();
            if (!held[i]&&down[i]&&armed[i]&&!blocked) {
                TickType_t duration=xTaskGetTickCount()-pressed_at[i];
                if (duration>=pdMS_TO_TICKS(50)&&duration<pdMS_TO_TICKS(3000)) {
                    navigation_ran=true;
                    if (i==2) run_touch_action((wind_touch_action_t){WIND_TOUCH_OPEN,0});
                    else if (wind_spots_count()>1 && battery_allows_work()) {
                        play_navigation_click();
                        power_manager_work_begin();
                        int direction=i==0?-1:1;
                        if (wind_app_navigation_requires_network(direction)&&!wifi_manager_is_connected())
                            (void)connect_installed_wifi();
                        esp_err_t result=direction<0?wind_app_select_previous():wind_app_select_next();
                        if (result!=ESP_OK) ESP_LOGW(TAG,"Spot navigation failed: %s",esp_err_to_name(result));
                        else if (s_dashboard_task) xTaskNotifyGive(s_dashboard_task);
                        power_manager_reset_sleep_timer();
                        power_manager_work_end();
                    }
                    gesture=(wind_touch_gesture_t){0}; discard_until_release=true;
                }
            }
            if (!held[i]&&!blocked) armed[i]=true;
            down[i]=held[i];
        }
        if (navigation_ran) {
            /* Ignore presses made while the e-ink refresh was blocking this task. */
            for (int i=0;i<3;++i) {
                down[i]=gpio_get_level(pins[i])==0;
                armed[i]=!down[i];
            }
        }
        board_touch_sample_t sample;
        esp_err_t result=board_hal_touch_read(&sample);
        now=(uint32_t)(esp_timer_get_time()/1000);
        if (result==ESP_OK) {
            last_frame=now;
            if (sample.contacts) power_manager_reset_sleep_timer();
            if (!sample.contacts && discard_until_release) discard_until_release=false;
            else if (!blocked&&!discard_until_release) {
                bool open; size_t page; wind_app_overview_state(&open,&page);
                wind_touch_action_t action=wind_touch_update(&gesture,sample.contacts,
                    (uint32_t)sample.x*800/1872,(uint32_t)sample.y*600/1404,now,open,page,wind_spots_count());
                if (action.kind!=WIND_TOUCH_NONE) {
                    run_touch_action(action);
                    discard_until_release=true;
                    for (int i=0;i<3;++i) {
                        down[i]=gpio_get_level(pins[i])==0;
                        armed[i]=!down[i];
                    }
                }
            }
        } else if (result!=ESP_ERR_NOT_FINISHED && result!=ESP_ERR_NOT_SUPPORTED) {
            /* Lost I2C frames must never turn a drag into an accidental selection. */
            gesture=(wind_touch_gesture_t){0}; discard_until_release=true;
        } else if (now-last_frame>250) {
            /* Some controller revisions omit a final zero-contact frame. Only
               re-arm here; never synthesize a tap from a timeout. */
            gesture=(wind_touch_gesture_t){0}; discard_until_release=false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#endif

#ifndef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
// Short releases navigate. Holding both buttons remains the boot recovery chord.
static void spot_buttons_task(void *argument)
{
    (void) argument;
    wind_navigation_buttons_t buttons = {0};
    while (true) {
        const int direction = wind_navigation_poll(
            &buttons, gpio_get_level(BOARD_HAL_ROTATE_KEY) == 0,
            gpio_get_level(BOARD_HAL_CLEAR_KEY) == 0,
            power_manager_is_installer_active(), wind_spots_count(),
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
        if (direction != 0) {
            if (!battery_allows_work()) continue;
            power_manager_reset_sleep_timer();
            if (wind_app_navigation_requires_network(direction) && !wifi_manager_is_connected())
                (void)connect_installed_wifi();
            const esp_err_t result = direction < 0 ? wind_app_select_previous() : wind_app_select_next();
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "Spot navigation failed: %s", esp_err_to_name(result));
            } else if (s_dashboard_task) {
                xTaskNotifyGive(s_dashboard_task);
            }
            power_manager_enter_sleep();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

#endif
static int dashboard_seconds_until_wake(void *context)
{
    (void)context;
    return wind_app_seconds_until_next_wake();
}

static bool dashboard_wait_notified(void *context, int seconds)
{
    (void)context;
    return ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(seconds * 1000)) != 0;
}

static void dashboard_task(void *argument)
{
    (void) argument;
    while (true) {
        wind_navigation_wait_for_refresh(NULL, dashboard_seconds_until_wake, dashboard_wait_notified);
        if (!power_manager_is_installer_active()) {
            if (!battery_allows_work()) continue;
            power_manager_work_begin();
            if (!wifi_manager_is_connected() && !connect_installed_wifi()) {
                ESP_LOGW(TAG, "Scheduled refresh is offline");
            }
            esp_err_t result = wind_app_refresh(false);
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "Scheduled forecast refresh failed: %s", esp_err_to_name(result));
            }
            power_manager_work_end();
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
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    init_navigation_click();
    /* Capture the latched wake coordinate before Wi-Fi or a screen refresh. */
    capture_boot_touch();
#endif
    ESP_ERROR_CHECK(storage_init());

    result = board_hal_rtc_init();
    if (result != ESP_OK && result != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "RTC initialization failed: %s", esp_err_to_name(result));
    }
    (void) restore_clock_from_rtc();

    ESP_ERROR_CHECK(display_manager_init());
    ESP_ERROR_CHECK(power_manager_init());
    s_battery_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_battery_lock ? ESP_OK : ESP_ERR_NO_MEM);
    while (!battery_allows_work()) vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(wind_installer_service_start());

    power_manager_work_begin();
    if (!installed_configuration_has_setup()) {
        ESP_LOGI(TAG, "Waiting for USB setup before starting the forecast");
        power_manager_work_end();
        bool setup_drawn = false;
        int64_t next_hint_attempt = 0;
        while (!installed_configuration_has_setup() || power_manager_is_installer_active()) {
            if (power_manager_is_installer_active()) {
                setup_drawn = false;
            } else if (!setup_drawn && esp_timer_get_time() >= next_hint_attempt) {
                power_manager_work_begin();
                result = wind_app_show_setup();
                power_manager_work_end();
                setup_drawn = result == ESP_OK;
                if (!setup_drawn)
                    ESP_LOGW(TAG, "Setup screen failed: %s", esp_err_to_name(result));
                next_hint_attempt = esp_timer_get_time() + INT64_C(30000000);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        power_manager_work_begin();
    }
    const wakeup_source_t wake = power_manager_get_wakeup_source();
    bool touch_wake=false;
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    touch_wake=wake==WAKEUP_SOURCE_TOUCH;
    if (wake==WAKEUP_SOURCE_BOOT_BUTTON ||
        ((wake==WAKEUP_SOURCE_ROTATE_BUTTON || wake==WAKEUP_SOURCE_CLEAR_BUTTON) &&
         wind_spots_count()>1)) play_navigation_click();
    /* Reject a stray gesture interrupt without starting Wi-Fi or refreshing. */
    if (touch_wake && board_hal_touch_gesture_wake() && !board_hal_touch_double_tap()) {
        power_manager_work_end();
        power_manager_enter_sleep();
        power_manager_work_begin(); // USB may have arrived during boot.
    }
#endif
    const bool connected = !touch_wake &&
                           (wifi_manager_is_connected() || connect_installed_wifi());
    if (connected && synchronize_clock() != ESP_OK) {
        ESP_LOGW(TAG, "Clock sync timed out; using the retained RTC clock");
    }

    const int early_seconds = power_manager_get_seconds_until_wake_target();
    if (early_seconds > EARLY_WAKE_TOLERANCE_SEC) {
        power_manager_work_end();
        power_manager_enter_sleep_with_timer((uint32_t) early_seconds);
        power_manager_work_begin();
    }

    const bool previous_spot = wake == WAKEUP_SOURCE_ROTATE_BUTTON && wind_spots_count() > 1;
    const bool next_spot = wake == WAKEUP_SOURCE_CLEAR_BUTTON && wind_spots_count() > 1;
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    if (wake==WAKEUP_SOURCE_BOOT_BUTTON) result=wind_app_show_overview();
    else if (touch_wake) {
        bool open; size_t page; wind_app_overview_state(&open,&page);
        wind_touch_action_t action={WIND_TOUCH_NONE,0};
        board_touch_sample_t point;
        if (board_hal_touch_wake_sample(&point))
            action=wind_overview_hit_test((uint32_t)point.x*800/1872,
                (uint32_t)point.y*600/1404,open,page,wind_spots_count());
        else if (s_have_boot_gesture)
            action=wind_touch_update(&s_boot_gesture,0,0,0,
                s_boot_release_ms,open,page,wind_spots_count());
        run_touch_action(action);
        result=ESP_OK;
    } else result = previous_spot ? wind_app_select_previous() : next_spot ? wind_app_select_next() : wind_app_start();
#else
    result = previous_spot ? wind_app_select_previous() : next_spot ? wind_app_select_next() : wind_app_start();
#endif
    power_manager_reset_sleep_timer();
    power_manager_work_end();
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Dashboard refresh completed with error: %s", esp_err_to_name(result));
    }

    if (wake == WAKEUP_SOURCE_TIMER
#ifndef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
        || previous_spot || next_spot
#endif
    ) {
        while (gpio_get_level(BOARD_HAL_ROTATE_KEY) == 0 || gpio_get_level(BOARD_HAL_CLEAR_KEY) == 0)
            vTaskDelay(pdMS_TO_TICKS(20));
        power_manager_enter_sleep();
    }

    xTaskCreate(dashboard_task, "wind_dashboard", 16384, NULL, 5, &s_dashboard_task);
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    xTaskCreate(e1003_input_task, "wind_input", 24576, NULL, 5, NULL);
#else
    xTaskCreate(spot_buttons_task, "wind_buttons", 16384, NULL, 5, NULL);
#endif
    ESP_LOGI(TAG, "Windpeek ready%s", connected ? "" : " (offline)");
    while (true) vTaskDelay(pdMS_TO_TICKS(60000));
}
