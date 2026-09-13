/* GT911 register protocol. Bus is shared with the RTC, sensor and charger.
 * https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino_peripherals_2/
 */
static i2c_master_dev_handle_t touch_device;
static uint16_t touch_width, touch_height;
static int touch_wake_level;
static SemaphoreHandle_t touch_lock;
static bool touch_sleeping, touch_boot_gesture, touch_boot_double_tap;
static bool touch_boot_has_point;
static board_touch_sample_t touch_boot_point;

static esp_err_t touch_write_byte(uint16_t reg, uint8_t value) {
    const uint8_t data[] = {(uint8_t)(reg >> 8), (uint8_t)reg, value};
    return i2c_master_transmit(touch_device,data,sizeof(data),50);
}

static esp_err_t touch_read_register(uint16_t reg, uint8_t *data, size_t size) {
    uint8_t address[] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 255)};
    return i2c_master_transmit_receive(touch_device,address,2,data,size,50);
}

static esp_err_t touch_ack(void) {
    const uint8_t clear[] = {0x81,0x4e,0};
    return i2c_master_transmit(touch_device,clear,sizeof(clear),50);
}

static bool touch_probe(void) {
    const uint8_t addresses[] = {0x5d,0x14};
    for (size_t i=0;i<2;++i) {
        if (i2c_master_probe(i2c_bus,addresses[i],50)!=ESP_OK) continue;
        i2c_device_config_t config={.dev_addr_length=I2C_ADDR_BIT_LEN_7,
            .device_address=addresses[i],.scl_speed_hz=100000};
        if (i2c_master_bus_add_device(i2c_bus,&config,&touch_device)!=ESP_OK) continue;
        uint8_t product[4], limits[4], mode;
        if (touch_read_register(0x8140,product,4)==ESP_OK) {
            /* Gesture firmware replaces the normal product/coordinate map. */
            if (product[0]=='G' && product[1]=='E' && product[2]=='S' && product[3]=='T') {
                uint8_t gesture=0;
                /* A failed status read must still reset GEST to coordinate mode. */
                if (touch_read_register(0x814b,&gesture,1)!=ESP_OK) {
                    i2c_master_bus_rm_device(touch_device); touch_device=NULL;
                    return false;
                }
                touch_boot_gesture=true;
                touch_boot_double_tap=gesture==0xcc;
                uint8_t size[4], point[4];
                if (touch_boot_double_tap && touch_read_register(0x8146,size,4)==ESP_OK &&
                    touch_read_register(0x8151,point,4)==ESP_OK) {
                    unsigned width=size[0]|(size[1]<<8), height=size[2]|(size[3]<<8);
                    unsigned x=point[0]|(point[1]<<8), y=point[2]|(point[3]<<8);
                    if (width && height && x<width && y<height) {
                        touch_boot_point=(board_touch_sample_t){.contacts=1,
                            .x=(uint16_t)(x*1872/width),.y=(uint16_t)(y*1404/height)};
                        touch_boot_has_point=true;
                    }
                }
                return true;
            }
            if (product[0]=='9' && product[1]=='1' && product[2]=='1' &&
                touch_read_register(0x8048,limits,4)==ESP_OK &&
                touch_read_register(0x804d,&mode,1)==ESP_OK) {
                touch_width=limits[0]|(limits[1]<<8);
                touch_height=limits[2]|(limits[3]<<8);
                touch_wake_level=(mode&3)==0 || (mode&3)==3;
                if (touch_width && touch_height) return true;
            }
        }
        i2c_master_bus_rm_device(touch_device); touch_device=NULL;
    }
    return false;
}

static void touch_reset(void) {
    /* Select 0x5d deterministically while RESET is low, then release INT. */
    gpio_config_t reset={.pin_bit_mask=1ULL<<BOARD_HAL_TOUCH_RESET,.mode=GPIO_MODE_OUTPUT};
    gpio_config(&reset);
    gpio_set_level(BOARD_HAL_TOUCH_RESET,0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_config_t interrupt={.pin_bit_mask=1ULL<<BOARD_HAL_TOUCH_INT,.mode=GPIO_MODE_OUTPUT};
    gpio_config(&interrupt);
    gpio_set_level(BOARD_HAL_TOUCH_INT,0); vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(BOARD_HAL_TOUCH_RESET,1); vTaskDelay(pdMS_TO_TICKS(6));
    gpio_set_level(BOARD_HAL_TOUCH_INT,0); vTaskDelay(pdMS_TO_TICKS(50));
    interrupt.mode=GPIO_MODE_INPUT;
    gpio_config(&interrupt);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (touch_device) { i2c_master_bus_rm_device(touch_device); touch_device=NULL; }
}

static void touch_init(void) {
    if (!touch_lock) touch_lock=xSemaphoreCreateMutex();
    if (!touch_lock) return;
    touch_sleeping=false; touch_boot_gesture=false; touch_boot_double_tap=false;
    touch_boot_has_point=false;
    if (touch_device) { i2c_master_bus_rm_device(touch_device); touch_device=NULL; }
    gpio_hold_dis(BOARD_HAL_TOUCH_RESET);
    gpio_hold_dis(BOARD_HAL_TOUCH_INT);
    gpio_config_t interrupt={.pin_bit_mask=1ULL<<BOARD_HAL_TOUCH_INT,
        .mode=GPIO_MODE_INPUT};
    gpio_config(&interrupt);
    /* Probe first: resetting after deep sleep would discard the wake touch. */
    if (touch_probe() && !touch_boot_gesture) return;
    touch_reset();
    if (!touch_probe()) ESP_LOGW(TAG,"GT911 unavailable; physical buttons remain usable");
}

bool board_hal_touch_available(void) { return touch_device != NULL; }
int board_hal_touch_wake_level(void) { return touch_wake_level; }
bool board_hal_touch_gesture_wake(void) { return touch_boot_gesture; }
bool board_hal_touch_double_tap(void) { return touch_boot_double_tap; }
bool board_hal_touch_wake_sample(board_touch_sample_t *sample) {
    if (!sample || !touch_boot_has_point) return false;
    *sample=touch_boot_point;
    touch_boot_has_point=false;
    return true;
}

/* Goodix GT911 Programming Guide Rev.10, sections 3.2, 4.4 and 7:
 * https://www.lcd-module.de/fileadmin/eng/pdf/zubehoer/GT911_Programming_Guide_Rev.10.pdf
 * Keep factory calibration; change gesture masks, wake pulse and checksum.
 * Do not flash a generic panel configuration or replacement controller firmware. */
static esp_err_t touch_enter_doze(void) {
    uint8_t config[2+186] = {0x80,0x47};
    esp_err_t result=touch_read_register(0x8047,config+2,186);
    if (result!=ESP_OK) return result;
    if (config[2+0x8075-0x8047]!=0 || config[2+0x8076-0x8047]!=2 ||
        (config[2+0x8056-0x8047]&0xf0)!=0xf0) {
        config[2+0x8075-0x8047]=0;    // No swipe/letter wake-ups.
        config[2+0x8076-0x8047]=2;    // Double-tap only (bit 1).
        /* Latch INT high until the gesture is read, including during sleep entry. */
        config[2+0x8056-0x8047]|=0xf0;
        uint8_t sum=0;
        for (size_t i=2;i<2+184;++i) sum+=config[i];
        config[186]=(uint8_t)(0-sum);
        config[187]=1;
        result=i2c_master_transmit(touch_device,config,sizeof(config),50);
        if (result!=ESP_OK) return result;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    uint8_t masks[2];
    result=touch_read_register(0x8075,masks,2);
    if (result!=ESP_OK) return result;
    if (masks[0]!=0 || masks[1]!=2) return ESP_ERR_INVALID_RESPONSE;
    uint8_t pulse;
    result=touch_read_register(0x8056,&pulse,1);
    if (result!=ESP_OK) return result;
    if ((pulse&0xf0)!=0xf0) return ESP_ERR_INVALID_RESPONSE;
    result=touch_write_byte(0x814b,0);
    if (result==ESP_OK) result=touch_ack();
    if (result==ESP_OK) result=touch_write_byte(0x8046,8);
    if (result==ESP_OK) result=touch_write_byte(0x8040,8);
    if (result!=ESP_OK) return result;
    for (int i=0;i<10;++i) {
        uint8_t product[4];
        vTaskDelay(pdMS_TO_TICKS(10));
        result=touch_read_register(0x8140,product,4);
        if (result==ESP_OK && product[0]=='G' && product[1]=='E' && product[2]=='S' && product[3]=='T')
            return ESP_OK;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t board_hal_touch_prepare_sleep(void) {
    if (!touch_device || !touch_lock) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(touch_lock,portMAX_DELAY);
    esp_err_t result=touch_enter_doze();
    if (result==ESP_OK) {
        /* Gesture INT is active HIGH, independent of coordinate INT polarity. */
        touch_wake_level=1;
        ESP_LOGI(TAG,"GT911 double-tap doze enabled");
    } else {
        ESP_LOGW(TAG,"GT911 doze unavailable; retaining normal touch wake");
        touch_reset();
        (void)touch_probe();
    }
    /* No coordinate polling after changing the controller's register map. */
    touch_sleeping=true;
    xSemaphoreGive(touch_lock);
    return result;
}

static esp_err_t touch_read_sample(board_touch_sample_t *sample) {
    if (!sample) return ESP_ERR_INVALID_ARG;
    if (!touch_device) return ESP_ERR_NOT_SUPPORTED;
    uint8_t status;
    esp_err_t result=touch_read_register(0x814e,&status,1);
    if (result!=ESP_OK) return result;
    if (!(status&0x80)) return ESP_ERR_NOT_FINISHED;
    *sample=(board_touch_sample_t){.contacts=(unsigned)(status&15)};
    if (sample->contacts>5) { (void)touch_ack(); return ESP_ERR_INVALID_RESPONSE; }
    if (sample->contacts) {
        uint8_t point[8];
        result=touch_read_register(0x814f,point,8);
        if (result!=ESP_OK) { (void)touch_ack(); return result; }
        unsigned x=point[1]|(point[2]<<8), y=point[3]|(point[4]<<8);
        if (x>=touch_width || y>=touch_height) { (void)touch_ack(); return ESP_ERR_INVALID_RESPONSE; }
        /* Firmware and the GT911 configuration both use native landscape axes. */
        sample->x=(uint32_t)x*1872/touch_width;
        sample->y=(uint32_t)y*1404/touch_height;
    }
    return touch_ack();
}

esp_err_t board_hal_touch_read(board_touch_sample_t *sample) {
    if (!sample) return ESP_ERR_INVALID_ARG;
    if (!touch_device || !touch_lock) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(touch_lock,portMAX_DELAY);
    esp_err_t result=touch_sleeping ? ESP_ERR_NOT_FINISHED : touch_read_sample(sample);
    xSemaphoreGive(touch_lock);
    return result;
}
