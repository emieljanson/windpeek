/* GT911 register protocol. Bus is shared with the RTC, sensor and charger.
 * https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino_peripherals_2/
 */
static i2c_master_dev_handle_t touch_device;
static uint16_t touch_width, touch_height;
static SemaphoreHandle_t touch_lock;

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
        uint8_t product[4], limits[4];
        if (touch_read_register(0x8140,product,4)==ESP_OK) {
            if (product[0]=='9' && product[1]=='1' && product[2]=='1' &&
                touch_read_register(0x8048,limits,4)==ESP_OK) {
                touch_width=limits[0]|(limits[1]<<8);
                touch_height=limits[2]|(limits[3]<<8);
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

bool board_hal_touch_available(void) { return touch_device != NULL; }

esp_err_t board_hal_touch_enable(void) {
    if (!i2c_bus) return ESP_ERR_INVALID_STATE;
    if (!touch_lock) touch_lock=xSemaphoreCreateMutex();
    if (!touch_lock) return ESP_ERR_NO_MEM;
    xSemaphoreTake(touch_lock,portMAX_DELAY);
    if (!touch_device) {
        gpio_hold_dis(BOARD_HAL_TOUCH_RESET);
        gpio_hold_dis(BOARD_HAL_TOUCH_INT);
        touch_reset();
        if (!touch_probe()) ESP_LOGW(TAG,"GT911 unavailable; physical buttons remain usable");
    }
    esp_err_t result=touch_device ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
    xSemaphoreGive(touch_lock);
    return result;
}

esp_err_t board_hal_touch_prepare_sleep(void) {
    if (touch_lock) xSemaphoreTake(touch_lock,portMAX_DELAY);
    if (touch_device) { i2c_master_bus_rm_device(touch_device); touch_device=NULL; }
    gpio_hold_dis(BOARD_HAL_TOUCH_RESET);
    gpio_config_t reset={.pin_bit_mask=1ULL<<BOARD_HAL_TOUCH_RESET,.mode=GPIO_MODE_OUTPUT};
    gpio_config(&reset);
    gpio_set_level(BOARD_HAL_TOUCH_RESET,0);
    gpio_hold_en(BOARD_HAL_TOUCH_RESET);
    if (touch_lock) xSemaphoreGive(touch_lock);
    ESP_LOGI(TAG,"GT911 held in reset; button wake only");
    return ESP_OK;
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
    if (!touch_lock) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(touch_lock,portMAX_DELAY);
    esp_err_t result=!touch_device ? ESP_ERR_NOT_SUPPORTED : touch_read_sample(sample);
    xSemaphoreGive(touch_lock);
    return result;
}
