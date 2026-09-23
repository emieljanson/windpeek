#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "epaper.h"
#include "epaper_frame.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E100X
#define epaper_get_width ed2208_gca_get_width
#define epaper_get_height ed2208_gca_get_height
#define epaper_init ed2208_gca_init
#define epaper_display ed2208_gca_display
#define epaper_display_logical ed2208_gca_display_logical
#define epaper_clear ed2208_gca_clear
#define epaper_enter_deepsleep ed2208_gca_enter_deepsleep
#endif

#ifdef CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

static const char *TAG = "epaper_ed2208_gca";

static epaper_config_t g_cfg;
static spi_device_handle_t spi;

#ifdef CONFIG_PM_ENABLE
static esp_pm_lock_handle_t pm_lock = NULL;
#endif

#define EPD_WIDTH 800
#define EPD_HEIGHT 480
// Packed pixel buffer size: 2 pixels per byte (4-bit color depth)
#define EPD_BUF_SIZE (EPD_WIDTH / 2 * EPD_HEIGHT)

// SPI max transfer size per transaction
#define SPI_MAX_CHUNK 4092
// Data transfer chunk size (per CS window)
#define DATA_CHUNK_SIZE 128

// --- Low-level SPI helpers ---

static void spi_begin(void)
{
    esp_err_t ret = spi_device_acquire_bus(spi, portMAX_DELAY);
    assert(ret == ESP_OK);
}

static void spi_end(void)
{
    spi_device_release_bus(spi);
}

static void spi_write(const uint8_t *data, size_t len)
{
    spi_transaction_t t = {};
    t.rxlength = 0;
    while (len > 0) {
        size_t chunk = (len > SPI_MAX_CHUNK) ? SPI_MAX_CHUNK : len;
        t.length = chunk * 8;
        t.tx_buffer = data;
        esp_err_t ret = spi_device_polling_start(spi, &t, portMAX_DELAY);
        if (ret == ESP_OK) {
            ret = spi_device_polling_end(spi, portMAX_DELAY);
        }
        assert(ret == ESP_OK);
        data += chunk;
        len -= chunk;
    }
}

// --- Display protocol helpers ---

// Send a command with optional data bytes in a single CS window.
// CS stays LOW for the entire command+data sequence.
static void cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    gpio_set_level(g_cfg.pin_dc, 0);  // DC low = command
    spi_begin();
    gpio_set_level(g_cfg.pin_cs, 0);  // CS low

    // Send command byte via SPI command register
    spi_transaction_ext_t cmd_t = {
        .command_bits = 8,
        .base =
            {
                .flags = SPI_TRANS_VARIABLE_CMD,
                .cmd = cmd,
            },
    };
    esp_err_t ret = spi_device_polling_start(spi, &cmd_t.base, portMAX_DELAY);
    if (ret == ESP_OK) {
        spi_device_polling_end(spi, portMAX_DELAY);
    }
    assert(ret == ESP_OK);

    if (len > 0) {
        gpio_set_level(g_cfg.pin_dc, 1);  // DC high = data
        // Copy to stack buffer to avoid PSRAM DMA issues
        uint8_t buf[16];
        assert(len <= sizeof(buf));
        memcpy(buf, data, len);
        spi_write(buf, len);
    }

    gpio_set_level(g_cfg.pin_cs, 1);  // CS high
    spi_end();
}

// Send a standalone command (no data bytes)
static void send_command(uint8_t cmd)
{
    cmd_data(cmd, NULL, 0);
}

// Send image buffer in DATA_CHUNK_SIZE-byte chunks, each in its own CS window,
// copied to a stack-local buffer to avoid PSRAM DMA issues.
static void send_buffer(uint8_t *data, int len)
{
    uint8_t buf[DATA_CHUNK_SIZE];
    uint8_t *ptr = data;
    int remaining = len;

    ESP_LOGI(TAG, "Sending %d bytes in %d-byte chunks", len, DATA_CHUNK_SIZE);

    while (remaining > 0) {
        int chunk = (remaining > DATA_CHUNK_SIZE) ? DATA_CHUNK_SIZE : remaining;

        // Convert the public Spectra-6 palette to the ED2208 transport palette,
        // matching Seeed_GFX's E1002 driver.
        for (int i = 0; i < chunk; i++) {
            uint8_t hi = (ptr[i] >> 4) & 0x0F;
            uint8_t lo = ptr[i] & 0x0F;
            uint8_t native_hi = hi == 0x0F ? 0x01 : hi == 0x00 ? 0x00 : hi == 0x0D ? 0x05
                                  : hi == 0x02 ? 0x06 : hi == 0x0B ? 0x02 : hi == 0x06 ? 0x03 : 0x00;
            uint8_t native_lo = lo == 0x0F ? 0x01 : lo == 0x00 ? 0x00 : lo == 0x0D ? 0x05
                                  : lo == 0x02 ? 0x06 : lo == 0x0B ? 0x02 : lo == 0x06 ? 0x03 : 0x00;
            buf[i] = (native_hi << 4) | native_lo;
        }

        gpio_set_level(g_cfg.pin_dc, 1);  // DC high = data
        spi_begin();
        gpio_set_level(g_cfg.pin_cs, 0);  // CS low
        spi_write(buf, chunk);
        gpio_set_level(g_cfg.pin_cs, 1);  // CS high
        spi_end();

        ptr += chunk;
        remaining -= chunk;
    }

    ESP_LOGI(TAG, "Buffer send complete");
}

static void send_native_buffer(const uint8_t *data, int len)
{
    uint8_t buf[DATA_CHUNK_SIZE];
    while (len > 0) {
        const int chunk = len > DATA_CHUNK_SIZE ? DATA_CHUNK_SIZE : len;
        memcpy(buf, data, (size_t) chunk);
        gpio_set_level(g_cfg.pin_dc, 1);
        spi_begin();
        gpio_set_level(g_cfg.pin_cs, 0);
        spi_write(buf, (size_t) chunk);
        gpio_set_level(g_cfg.pin_cs, 1);
        spi_end();
        data += chunk;
        len -= chunk;
    }
}

static bool is_busy(void *context)
{
    (void) context;
    int level = gpio_get_level(g_cfg.pin_busy);
    return level == 0;
}

static void busy_delay(void *context, uint32_t milliseconds)
{
    (void) context;
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

static esp_err_t wait_busy(epaper_panel_phase_t phase, const char *label)
{
    busy_delay(NULL, 10);
    esp_err_t result = epaper_wait_busy_bounded(is_busy, busy_delay, NULL, 10, 40000);
    if (result == ESP_ERR_TIMEOUT) {
        epaper_panel_diagnostics_timeout(phase, 40000, (uint32_t)gpio_get_level(g_cfg.pin_busy));
        ESP_LOGW(TAG, "[%s] BUSY timeout after 40s", label);
    }
    return result;
}

// --- Hardware setup ---

static void gpio_init(void)
{
    // Release any pad holds latched by a previous deep-sleep cycle
    // (see epaper_enter_deepsleep) so gpio_config + gpio_set_level
    // below can re-drive these pins.
    gpio_hold_dis(g_cfg.pin_cs);
    gpio_hold_dis(g_cfg.pin_dc);
    gpio_hold_dis(g_cfg.pin_rst);

    // Set desired output levels BEFORE enabling output drivers to avoid glitches
    gpio_set_level(g_cfg.pin_cs, 1);   // CS HIGH = deselected
    gpio_set_level(g_cfg.pin_dc, 0);   // DC LOW = command mode
    gpio_set_level(g_cfg.pin_rst, 1);  // RST HIGH = not in reset

    gpio_config_t out_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << g_cfg.pin_rst) | (1ULL << g_cfg.pin_dc) | (1ULL << g_cfg.pin_cs),
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&out_conf));

    gpio_config_t in_conf = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << g_cfg.pin_busy),
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&in_conf));

    if (g_cfg.pin_enable >= 0) {
        // Release any pad hold latched by a previous deep-sleep cycle
        // (see epaper_enter_deepsleep) so gpio_config + gpio_set_level
        // below can re-drive the enable pin.
        gpio_hold_dis(g_cfg.pin_enable);
        gpio_config_t en_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << g_cfg.pin_enable),
        };
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&en_conf));
        gpio_set_level(g_cfg.pin_enable, 1);
        vTaskDelay(pdMS_TO_TICKS(100));  // allow display power to stabilize
    }
}

static void spi_add_device(void)
{
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = -1,  // CS is manually controlled
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(g_cfg.spi_host, &devcfg, &spi));
}

static void hw_reset(void)
{
    gpio_set_level(g_cfg.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(g_cfg.pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(g_cfg.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

// --- Display operations ---

static void send_init_sequence(void)
{
    cmd_data(0xAA, (uint8_t[]){0x49, 0x55, 0x20, 0x08, 0x09, 0x18}, 6);  // CMDH
    cmd_data(0x01, (uint8_t[]){0x3F, 0x00, 0x32, 0x2A, 0x0E, 0x2A}, 6);  // PWRR
    cmd_data(0x00, (uint8_t[]){0x5F, 0x69}, 2);                          // PSR
    cmd_data(0x03, (uint8_t[]){0x00, 0x54, 0x00, 0x44}, 4);              // POFS
    cmd_data(0x05, (uint8_t[]){0x40, 0x1F, 0x1F, 0x2C}, 4);              // BTST1
    cmd_data(0x06, (uint8_t[]){0x6F, 0x1F, 0x16, 0x25}, 4);              // BTST2
    cmd_data(0x08, (uint8_t[]){0x6F, 0x1F, 0x1F, 0x22}, 4);              // BTST3
    cmd_data(0x13, (uint8_t[]){0x00, 0x04}, 2);                          // IPC
    cmd_data(0x30, (uint8_t[]){0x02}, 1);                                // PLL
    cmd_data(0x41, (uint8_t[]){0x00}, 1);                                // TSE
    cmd_data(0x50, (uint8_t[]){0x3F}, 1);                                // CDI
    cmd_data(0x60, (uint8_t[]){0x02, 0x00}, 2);                          // TCON
    cmd_data(0x61, (uint8_t[]){0x03, 0x20, 0x01, 0xE0}, 4);              // TRES
    cmd_data(0x82, (uint8_t[]){0x1E}, 1);                                // VDCS
    cmd_data(0x84, (uint8_t[]){0x01}, 1);                                // T_VDCS
    cmd_data(0x86, (uint8_t[]){0x00}, 1);                                // AGID
    cmd_data(0xE3, (uint8_t[]){0x2F}, 1);                                // PWS
    cmd_data(0xE0, (uint8_t[]){0x00}, 1);                                // CCSET
    cmd_data(0xE6, (uint8_t[]){0x00}, 1);                                // TSSET
}

// Full display update cycle:
// RESET -> INIT -> wait -> DTM -> DATA -> PON -> wait -> DRF -> wait -> POF -> wait -> DSLP
static esp_err_t display_update_cycle(uint8_t *image, bool native_transport)
{
    esp_err_t result = ESP_OK;
#ifdef CONFIG_PM_ENABLE
    if (pm_lock) {
        esp_pm_lock_acquire(pm_lock);
    }
#endif

    hw_reset();
    if ((result = wait_busy(EPAPER_PANEL_RESET, "reset")) != ESP_OK) goto done;

    send_init_sequence();
    if ((result = wait_busy(EPAPER_PANEL_INIT, "init")) != ESP_OK) goto done;

    send_command(0x04);  // POWER_ON
    vTaskDelay(pdMS_TO_TICKS(10));  // allow BUSY to assert before polling it
    if ((result = wait_busy(EPAPER_PANEL_POWER_ON, "power_on")) != ESP_OK) goto done;
    send_command(0x10);  // DATA_START_TRANSMISSION
    if (native_transport) {
        send_native_buffer(image, EPD_BUF_SIZE);
    } else {
        send_buffer(image, EPD_BUF_SIZE);
    }
    if ((result = wait_busy(EPAPER_PANEL_DATA, "data")) != ESP_OK) goto done;

    cmd_data(0x12, (uint8_t[]){0x00}, 1);  // DISPLAY_REFRESH
    vTaskDelay(pdMS_TO_TICKS(10));  // ED2208 asserts BUSY asynchronously
    if ((result = wait_busy(EPAPER_PANEL_REFRESH, "refresh")) != ESP_OK) goto done;

    cmd_data(0x02, (uint8_t[]){0x00}, 1);  // POWER_OFF
    vTaskDelay(pdMS_TO_TICKS(10));
    if ((result = wait_busy(EPAPER_PANEL_POWER_OFF, "power_off")) != ESP_OK) goto done;

    cmd_data(0x07, (uint8_t[]){0xA5}, 1);  // DEEP_SLEEP

done:
#ifdef CONFIG_PM_ENABLE
    if (pm_lock) {
        esp_pm_lock_release(pm_lock);
    }
#endif
    return result;
}

// --- Public API ---

uint16_t epaper_get_width(void)
{
    return EPD_WIDTH;
}

uint16_t epaper_get_height(void)
{
    return EPD_HEIGHT;
}

esp_err_t epaper_init(const epaper_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    g_cfg = *cfg;

    ESP_LOGI(TAG, "Initializing ED2208-GCA (Spectra 6) E-Paper Driver");

    spi_add_device();
    gpio_init();

#ifdef CONFIG_PM_ENABLE
    esp_err_t ret = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "epd_update", &pm_lock);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PM lock: %s", esp_err_to_name(ret));
    }
#endif
    return ESP_OK;
}

esp_err_t epaper_clear(uint8_t *image, uint8_t color)
{
    if (!image) return ESP_ERR_INVALID_ARG;
    uint8_t packed = (color << 4) | color;
    memset(image, packed, EPD_BUF_SIZE);

    ESP_LOGI(TAG, "Clearing display with color 0x%02x", color);
    esp_err_t result = display_update_cycle(image, false);
    ESP_LOGI(TAG, "Clear complete");
    return result;
}

esp_err_t epaper_display(uint8_t *image)
{
    if (!image) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Starting display update: %d bytes", EPD_BUF_SIZE);
    esp_err_t result = display_update_cycle(image, false);
    ESP_LOGI(TAG, "Display update complete");
    return result;
}

esp_err_t epaper_display_logical(const uint8_t *image, size_t image_size)
{
    if (!image) return ESP_ERR_INVALID_ARG;
    uint8_t *transport = heap_caps_malloc(EPAPER_E1002_TRANSPORT_BYTES,
                                          MALLOC_CAP_SPIRAM);
    if (!transport) return ESP_ERR_NO_MEM;
    esp_err_t result = epaper_encode_e1002_spectra6(
        image, image_size, transport, EPAPER_E1002_TRANSPORT_BYTES);
    if (result == ESP_OK) result = display_update_cycle(transport, true);
    free(transport);
    return result;
}

esp_err_t epaper_enter_deepsleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep");

#ifdef CONFIG_PM_ENABLE
    if (pm_lock) {
        esp_pm_lock_acquire(pm_lock);
    }
#endif

    // display_update_cycle() already sends POF + DSLP after each update,
    // so the display should already be in deep sleep. Send again to be safe.
    cmd_data(0x02, (uint8_t[]){0x00}, 1);  // POWER_OFF
    esp_err_t result = wait_busy(EPAPER_PANEL_SLEEP_POWER_OFF, "deepsleep_power_off");
    if (result != ESP_OK) goto done;
    cmd_data(0x07, (uint8_t[]){0xA5}, 1);  // DEEP_SLEEP

    if (g_cfg.pin_enable >= 0) {
        // Drive panel-facing GPIOs LOW before cutting VDD so they don't
        // back-feed through the panel's ESD diodes once its rail drops to
        // 0V. SPI peripheral pads (MOSI/SCLK) become Hi-Z in deep sleep
        // and don't need handling.
        gpio_set_level(g_cfg.pin_cs, 0);
        gpio_set_level(g_cfg.pin_dc, 0);
        gpio_set_level(g_cfg.pin_rst, 0);
        if (g_cfg.pin_cs1 >= 0) {
            gpio_set_level(g_cfg.pin_cs1, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(100));       // Ensure display enters sleep before cutting power
        gpio_set_level(g_cfg.pin_enable, 0);  // Cut power
        // Latch the pads low so the rail stays cut and panel-facing
        // signals stay grounded once the digital IO domain powers down
        // during deep sleep, and arm deep-sleep hold so the latch
        // survives into deep sleep. All calls are idempotent.
        gpio_hold_en(g_cfg.pin_enable);
        gpio_hold_en(g_cfg.pin_cs);
        gpio_hold_en(g_cfg.pin_dc);
        gpio_hold_en(g_cfg.pin_rst);
        if (g_cfg.pin_cs1 >= 0) {
            gpio_hold_en(g_cfg.pin_cs1);
        }
        gpio_deep_sleep_hold_en();
    }

done:
#ifdef CONFIG_PM_ENABLE
    if (pm_lock) {
        esp_pm_lock_release(pm_lock);
    }
#endif
    return result;
}

#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E100X
const epaper_backend_t epaper_backend_ed2208_gca = {
    .name = "ed2208-gca",
    .width = EPD_WIDTH,
    .height = EPD_HEIGHT,
    .init = ed2208_gca_init,
    .display = ed2208_gca_display,
    .display_logical = ed2208_gca_display_logical,
    .clear = ed2208_gca_clear,
    .set_temperature = NULL,
    .enter_deepsleep = ed2208_gca_enter_deepsleep,
};
#endif
