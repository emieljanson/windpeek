#include <stdbool.h>
#include <stdatomic.h>
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
#include "freertos/queue.h"
#include "wind_overview.h"
#include <stdatomic.h>
#endif

static const char *TAG = "windpeek";
static volatile bool s_time_synchronized;
static TaskHandle_t s_dashboard_task;

RTC_DATA_ATTR static bool s_empty_latched;
static bool s_battery_state_loaded;
static SemaphoreHandle_t s_battery_lock;
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
typedef struct {
    wind_touch_action_t touch;
    int direction;
    bool white_button;
} input_command_t;
static QueueHandle_t s_input_commands;
static atomic_uint s_pending_input_actions;
static atomic_bool s_interactive_refresh_requested;
static atomic_int_fast64_t s_last_input_action_us;
static bool s_input_initial_overview_open;
static size_t s_input_initial_overview_page;
static _Atomic(TaskHandle_t) s_setup_forecasts_task;
static atomic_bool s_setup_forecasts_pending;
static atomic_uint s_quick_frames_state;
#endif

static void configuration_installed(void) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    atomic_store(&s_setup_forecasts_pending, true);
    TaskHandle_t task = atomic_load(&s_setup_forecasts_task);
    if (task) xTaskNotifyGive(task);
#endif
}

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

static void run_touch_action(wind_touch_action_t action, bool click) {
    if (action.kind==WIND_TOUCH_NONE || power_manager_is_installer_active()) return;
    if (click) play_navigation_click();
    if (!battery_allows_work()) return;
    power_manager_work_begin();
    power_manager_reset_sleep_timer();
    /* Fetching is lazy, and overview rendering reads only its visible three spots. */
    bool need_network = action.kind==WIND_TOUCH_TOGGLE_DAY ? false : action.kind==WIND_TOUCH_SELECT
        ? wind_app_spot_requires_network(action.spot_index)
        : wind_app_overview_requires_network(action.kind==WIND_TOUCH_NEXT_PAGE ? 1 :
            action.kind==WIND_TOUCH_PREVIOUS_PAGE ? -1 : 0);
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
    else {
        if (need_network) atomic_store(&s_interactive_refresh_requested, true);
        atomic_store(&s_last_input_action_us, esp_timer_get_time());
        if (s_dashboard_task) xTaskNotifyGive(s_dashboard_task);
    }
    power_manager_reset_sleep_timer();
    power_manager_work_end();
}

static void navigate_spot_button(int direction) {
    if (!battery_allows_work()) return;
    power_manager_work_begin();
    power_manager_reset_sleep_timer();
    const bool need_network = wind_app_navigation_requires_network(direction);
    esp_err_t result=direction<0?wind_app_select_previous():wind_app_select_next();
    if (result!=ESP_OK) ESP_LOGW(TAG,"Spot navigation failed: %s",esp_err_to_name(result));
    else {
        if (need_network) atomic_store(&s_interactive_refresh_requested, true);
        atomic_store(&s_last_input_action_us, esp_timer_get_time());
        if (s_dashboard_task) xTaskNotifyGive(s_dashboard_task);
    }
    power_manager_reset_sleep_timer();
    power_manager_work_end();
}

static bool queue_input_command(input_command_t command) {
    if (!s_input_commands) return false;
    atomic_fetch_add(&s_pending_input_actions, 1);
    if (xQueueSend(s_input_commands, &command, 0) == pdTRUE) {
        atomic_store(&s_last_input_action_us, esp_timer_get_time());
        if (s_dashboard_task) xTaskNotifyGive(s_dashboard_task);
        return true;
    }
    atomic_fetch_sub(&s_pending_input_actions, 1);
    return false;
}

static void e1003_action_task(void *argument) {
    (void)argument;
    input_command_t command;
    while (true) {
        if (xQueueReceive(s_input_commands, &command, portMAX_DELAY) != pdTRUE) continue;
        if (command.white_button) {
            bool overview_open=false;
            size_t page=0;
            wind_app_overview_state(&overview_open,&page);
            if (overview_open && wind_spots_count()>1) navigate_spot_button(1);
            else run_touch_action((wind_touch_action_t){WIND_TOUCH_OPEN,0}, false);
        } else if (command.direction) navigate_spot_button(command.direction);
        else run_touch_action(command.touch, false);
        atomic_fetch_sub(&s_pending_input_actions, 1);
    }
}

static void e1003_input_task(void *argument) {
    (void)argument;
    const gpio_num_t pins[]={BOARD_HAL_ROTATE_KEY,BOARD_HAL_CLEAR_KEY,BOARD_HAL_WAKEUP_KEY};
    bool down[3]={false}, armed[3]={false};
    bool click_played[3]={false};
    TickType_t pressed_at[3]={0};
    wind_touch_gesture_t gesture={0};
    /* The second wake tap may still be held after the controller resets. */
    bool discard_until_release=board_hal_touch_gesture_wake();
    uint32_t last_frame=(uint32_t)(esp_timer_get_time()/1000);
    bool last_overview_open=s_input_initial_overview_open;
    size_t last_overview_page=s_input_initial_overview_page;
    while (true) {
        uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
        bool held[3];
        for (int i=0;i<3;++i) held[i]=gpio_get_level(pins[i])==0;
        bool blocked=power_manager_is_installer_active() || (held[0]&&held[1]);
        if (blocked) { gesture=(wind_touch_gesture_t){0}; discard_until_release=true; }
        for (int i=0;i<3;++i) {
            if (blocked) armed[i]=false;
            if (held[i]&&!down[i]) {
                pressed_at[i]=xTaskGetTickCount();
                click_played[i]=false;
            }
            if (held[i] && armed[i] && !blocked && !click_played[i] &&
                xTaskGetTickCount()-pressed_at[i]>=pdMS_TO_TICKS(50) &&
                (i==2 || wind_spots_count()>1)) {
                if (i==2 || queue_input_command((input_command_t){.direction=i==0?-1:1})) {
                    play_navigation_click();
                    click_played[i]=true;
                }
            }
            if (!held[i]&&down[i]&&armed[i]&&!blocked) {
                TickType_t duration=xTaskGetTickCount()-pressed_at[i];
                if (duration>=pdMS_TO_TICKS(50)&&duration<pdMS_TO_TICKS(3000)) {
                    if (i==2) {
                        if (queue_input_command((input_command_t){.white_button=true}) &&
                            !click_played[i]) play_navigation_click();
                    } else if (!click_played[i] && wind_spots_count()>1 &&
                               queue_input_command((input_command_t){.direction=i==0?-1:1})) {
                        play_navigation_click();
                    }
                    gesture=(wind_touch_gesture_t){0}; discard_until_release=true;
                }
            }
            if (!held[i]&&!blocked) armed[i]=true;
            down[i]=held[i];
        }
        board_touch_sample_t sample;
        esp_err_t result=board_hal_touch_read(&sample);
        now=(uint32_t)(esp_timer_get_time()/1000);
        if (result==ESP_OK) {
            last_frame=now;
            if (sample.contacts) power_manager_reset_sleep_timer();
            if (atomic_load(&s_pending_input_actions)>0) {
                gesture=(wind_touch_gesture_t){0};
                discard_until_release=true;
            } else if (!sample.contacts && discard_until_release) discard_until_release=false;
            else if (!blocked&&!discard_until_release) {
                (void)wind_app_overview_state_if_ready(&last_overview_open,&last_overview_page);
                wind_touch_action_t action=wind_touch_update(&gesture,sample.contacts,
                    (uint32_t)sample.x*800/1872,(uint32_t)sample.y*600/1404,now,
                    last_overview_open,last_overview_page,wind_spots_count());
                if (action.kind!=WIND_TOUCH_NONE) {
                    if (queue_input_command((input_command_t){.touch=action})) play_navigation_click();
                    discard_until_release=true;
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

#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
static void start_quick_frames_task(void);

static void setup_forecasts_task(void *argument) {
    (void)argument;
    TickType_t retry_delay = portMAX_DELAY;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, retry_delay);
        const bool newly_installed = atomic_exchange(&s_setup_forecasts_pending, false);
        if (!newly_installed && retry_delay == portMAX_DELAY) continue;
        while (power_manager_is_installer_active()) vTaskDelay(pdMS_TO_TICKS(100));
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (battery_allows_work()) {
            power_manager_work_begin();
            if (wifi_manager_is_connected() || connect_installed_wifi()) {
                result = wind_app_prefetch_other_spots();
                start_quick_frames_task();
            }
            power_manager_work_end();
        }
        retry_delay = result == ESP_OK ? portMAX_DELAY : pdMS_TO_TICKS(5 * 60 * 1000);
        if (result != ESP_OK)
            ESP_LOGW(TAG, "Setup forecasts incomplete: %s; retrying in five minutes",
                     esp_err_to_name(result));
    }
}

static bool quick_frames_may_continue(void) {
    while (atomic_load(&s_pending_input_actions) > 0) {
        if (power_manager_is_installer_active()) return false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return !power_manager_is_installer_active() && battery_allows_work();
}

static void quick_frames_sweep(void) {
    const size_t count = wind_spots_count();
    if (!count) return;
    size_t selected = 0;
    if (wind_spots_load_selected(&selected) != ESP_OK || selected >= count) selected = 0;
    for (size_t rank = 0; rank < count; ++rank) {
        const int offset = rank == 0 ? 0 : (rank & 1u)
            ? (int)((rank + 1) / 2) : -(int)(rank / 2);
        if (!quick_frames_may_continue()) return;
        power_manager_work_begin();
        (void)wind_app_prepare_quick_frame(wind_spots_offset(selected, offset), 0);
        power_manager_work_end();
        vTaskDelay(1);
    }
    const size_t last_page = wind_overview_last_page(count);
    for (size_t rank = 0; rank <= last_page; ++rank) {
        if (!quick_frames_may_continue()) return;
        const size_t page = (selected / WIND_OVERVIEW_PAGE_SIZE + rank) % (last_page + 1);
        power_manager_work_begin();
        (void)wind_app_prepare_quick_overview(page);
        power_manager_work_end();
        vTaskDelay(1);
    }
    for (int variant = 1; variant <= WIND_FORECAST_DAY_COUNT; ++variant) {
        for (size_t rank = 0; rank < count; ++rank) {
            const int offset = rank == 0 ? 0 : (rank & 1u)
                ? (int)((rank + 1) / 2) : -(int)(rank / 2);
            if (!quick_frames_may_continue()) return;
            power_manager_work_begin();
            (void)wind_app_prepare_quick_frame(wind_spots_offset(selected, offset), variant);
            power_manager_work_end();
            vTaskDelay(1);
        }
    }
}

static void quick_frames_task(void *argument) {
    (void)argument;
    for (;;) {
        quick_frames_sweep();
        unsigned expected = 1;
        if (atomic_compare_exchange_strong(&s_quick_frames_state, &expected, 0)) break;
        expected = 2;
        if (!atomic_compare_exchange_strong(&s_quick_frames_state, &expected, 1)) break;
    }
    vTaskDelete(NULL);
}

static void start_quick_frames_task(void) {
    for (;;) {
        unsigned expected = 0;
        if (atomic_compare_exchange_strong(&s_quick_frames_state, &expected, 1)) break;
        if (expected == 2) return;
        expected = 1;
        if (atomic_compare_exchange_strong(&s_quick_frames_state, &expected, 2)) return;
    }
    if (xTaskCreate(quick_frames_task, "wind_quick", 16384, NULL, 2, NULL) != pdPASS) {
        atomic_store(&s_quick_frames_state, 0);
        ESP_LOGW(TAG, "Could not prepare quick screen images");
    }
}
#endif

static void dashboard_task(void *argument)
{
    (void) argument;
    while (true) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
        bool requested = atomic_exchange(&s_interactive_refresh_requested, false);
        if (!requested) {
            const int seconds = dashboard_seconds_until_wake(NULL);
            const bool notified = dashboard_wait_notified(NULL, seconds > 0 ? seconds : 1);
            requested = atomic_exchange(&s_interactive_refresh_requested, false);
            if (notified && !requested) continue;
        }
        // Leave the panel and radio available throughout a short navigation session.
        while (atomic_load(&s_pending_input_actions) > 0 ||
               (atomic_load(&s_last_input_action_us) > 0 &&
                esp_timer_get_time() - atomic_load(&s_last_input_action_us) < INT64_C(30000000))) {
            (void)dashboard_wait_notified(NULL, 1);
            requested |= atomic_exchange(&s_interactive_refresh_requested, false);
        }
#else
        wind_navigation_wait_for_refresh(NULL, dashboard_seconds_until_wake, dashboard_wait_notified);
#endif
        if (!power_manager_is_installer_active()) {
            if (!battery_allows_work()) continue;
            power_manager_work_begin();
            if (!wifi_manager_is_connected() && !connect_installed_wifi()) {
                ESP_LOGW(TAG, "Scheduled refresh is offline");
            }
            esp_err_t result = wind_app_refresh(
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
                requested
#else
                false
#endif
            );
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "Scheduled forecast refresh failed: %s", esp_err_to_name(result));
            }
            power_manager_work_end();
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
            if (result == ESP_OK) start_quick_frames_task();
#endif
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
        ESP_ERROR_CHECK(wind_installer_service_start(configuration_installed));
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
        ESP_ERROR_CHECK(wind_installer_service_start(configuration_installed));
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
    ESP_ERROR_CHECK(wind_installer_service_start(configuration_installed));

    power_manager_work_begin();
    const bool first_setup = !installed_configuration_has_setup();
    if (first_setup) {
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
    bool cached_start = false;
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    cached_start = wake == WAKEUP_SOURCE_NONE && wind_app_has_cached_start();
#endif
    bool interactive_wake = touch_wake;
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    interactive_wake = interactive_wake ||
        wake == WAKEUP_SOURCE_BOOT_BUTTON ||
        wake == WAKEUP_SOURCE_ROTATE_BUTTON ||
        wake == WAKEUP_SOURCE_CLEAR_BUTTON || cached_start;
#endif
    const bool connected = !interactive_wake &&
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
        run_touch_action(action, true);
        result=ESP_OK;
    } else result = previous_spot ? wind_app_select_previous() : next_spot ? wind_app_select_next() :
        first_setup && wind_app_last_render_succeeded() ? ESP_OK :
        cached_start ? wind_app_show_cached_start() : wind_app_start();
#else
    result = previous_spot ? wind_app_select_previous() : next_spot ? wind_app_select_next() : wind_app_start();
#endif
    power_manager_reset_sleep_timer();
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    if (result == ESP_OK && wake == WAKEUP_SOURCE_TIMER)
        (void)wind_app_prepare_quick_frames();
    const bool boot_navigation = previous_spot || next_spot ||
        wake == WAKEUP_SOURCE_BOOT_BUTTON || cached_start;
    if (result == ESP_OK && boot_navigation) {
        const bool needs_network = wake == WAKEUP_SOURCE_BOOT_BUTTON
            ? wind_app_overview_requires_network(0)
            : wind_app_navigation_requires_network(0);
        if (needs_network || cached_start) {
            atomic_store(&s_interactive_refresh_requested, true);
            atomic_store(&s_last_input_action_us, esp_timer_get_time());
        }
    }
#endif
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

#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    wind_app_overview_state(&s_input_initial_overview_open,&s_input_initial_overview_page);
    s_input_commands=xQueueCreate(8,sizeof(input_command_t));
    if (!s_input_commands) ESP_LOGE(TAG,"Failed to allocate input queue");
    else if (xTaskCreate(e1003_action_task,"wind_actions",24576,NULL,5,NULL)!=pdPASS) {
        vQueueDelete(s_input_commands);
        s_input_commands=NULL;
        ESP_LOGE(TAG,"Failed to start input actions");
    }
    xTaskCreate(dashboard_task, "wind_dashboard", 16384, NULL, 5, &s_dashboard_task);
    if (s_input_commands &&
        xTaskCreate(e1003_input_task,"wind_input",24576,NULL,6,NULL)!=pdPASS)
        ESP_LOGE(TAG,"Failed to start input sampling");
    if (s_input_commands && !first_setup) start_quick_frames_task();
    TaskHandle_t setup_task = NULL;
    if (xTaskCreate(setup_forecasts_task, "wind_setup_forecasts", 16384,
                    NULL, 2, &setup_task) != pdPASS)
        ESP_LOGE(TAG, "Failed to start setup forecast loading");
    else {
        atomic_store(&s_setup_forecasts_task, setup_task);
        // A reset or deep sleep can interrupt the initial background sweep.
        // Resume missing spots instead of depending on a volatile setup flag.
        if (!atomic_load(&s_setup_forecasts_pending)) {
            size_t selected = 0;
            if (wind_spots_load_selected(&selected) != ESP_OK || selected >= wind_spots_count())
                selected = 0;
            for (size_t index = 0; index < wind_spots_count(); ++index) {
                // The dashboard task owns refreshes for the selected spot.
                if (index != selected && wind_app_spot_requires_network(index)) {
                    atomic_store(&s_setup_forecasts_pending, true);
                    break;
                }
            }
        }
        if (atomic_load(&s_setup_forecasts_pending)) xTaskNotifyGive(setup_task);
    }
#else
    xTaskCreate(dashboard_task, "wind_dashboard", 16384, NULL, 5, &s_dashboard_task);
    xTaskCreate(spot_buttons_task, "wind_buttons", 16384, NULL, 5, NULL);
#endif
    ESP_LOGI(TAG, "Windpeek ready%s", connected ? "" : " (offline)");
    while (true) vTaskDelay(pdMS_TO_TICKS(60000));
}
