/* SPDX-License-Identifier: GPL-3.0-only
 *
 * Experimental UC8179 Gray4 backend for reTerminal E1001.
 *
 * Lifecycle, LUTs, polarity, and plane ordering are derived from Seeed's
 * pinned GPL-3.0 reference documented in firmware/UPSTREAM.md. This port is
 * modified for ESP-IDF, bounded BUSY waits, and Windpeek's logical surface.
 * Hardware correctness remains gated on U7 physical acceptance.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "epaper.h"
#include "epaper_frame.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    UC8179_SPI_HZ = 2000000,
    UC8179_BUSY_POLL_MS = 10,
    UC8179_BUSY_TIMEOUT_MS = 40000,
    UC8179_CHUNK_BYTES = 128,
};

static const char *TAG = "epaper_uc8179";
static epaper_config_t s_config;
static spi_device_handle_t s_spi;
static bool s_panel_sleeping;

/* Verbatim 42-byte external Gray4 LUTs from the pinned Seeed example. */
static const uint8_t LUT_VCOM_GRAY[] = {
    0x00,0x00,0x06,0x08,0x07,0x01, 0x00,0x06,0x0A,0x0B,0x0A,0x01,
    0x00,0x03,0x03,0x00,0x00,0x03, 0x00,0x05,0x09,0x06,0x06,0x01,
    0x00,0x02,0x02,0x0A,0x0A,0x01, 0x00,0x0A,0x11,0x06,0x07,0x01,
    0x00,0x02,0x01,0x02,0x01,0x01,
};
static const uint8_t LUT_WW_GRAY[] = {
    0x15,0x00,0x06,0x08,0x07,0x01, 0x54,0x06,0x0A,0x0B,0x0A,0x01,
    0x90,0x03,0x03,0x00,0x00,0x03, 0x2A,0x05,0x09,0x06,0x06,0x01,
    0xAA,0x02,0x02,0x0A,0x0A,0x01, 0x00,0x0A,0x11,0x06,0x07,0x01,
    0x28,0x02,0x01,0x02,0x01,0x01,
};
static const uint8_t LUT_KW_GRAY[] = {
    0x2A,0x00,0x06,0x08,0x07,0x01, 0x59,0x06,0x0A,0x0B,0x0A,0x01,
    0x90,0x03,0x03,0x00,0x00,0x03, 0x5A,0x05,0x09,0x06,0x06,0x01,
    0xA8,0x02,0x02,0x0A,0x0A,0x01, 0x45,0x0A,0x11,0x06,0x07,0x01,
    0xA8,0x02,0x01,0x02,0x01,0x01,
};
static const uint8_t LUT_WK_GRAY[] = {
    0x16,0x00,0x06,0x08,0x07,0x01, 0xA0,0x06,0x0A,0x0B,0x0A,0x01,
    0x90,0x03,0x03,0x00,0x00,0x03, 0x99,0x05,0x09,0x06,0x06,0x01,
    0xA0,0x02,0x02,0x0A,0x0A,0x01, 0x40,0x0A,0x11,0x06,0x07,0x01,
    0x20,0x02,0x01,0x02,0x01,0x01,
};
static const uint8_t LUT_KK_GRAY[] = {
    0x26,0x00,0x06,0x08,0x07,0x01, 0x6A,0x06,0x0A,0x0B,0x0A,0x01,
    0x90,0x03,0x03,0x00,0x00,0x03, 0x65,0x05,0x09,0x06,0x06,0x01,
    0x50,0x02,0x02,0x0A,0x0A,0x01, 0x10,0x0A,0x11,0x06,0x07,0x01,
    0x10,0x02,0x01,0x02,0x01,0x01,
};

static esp_err_t spi_write(const uint8_t *data, size_t length)
{
    uint8_t local[UC8179_CHUNK_BYTES];
    while (length > 0) {
        const size_t chunk = length > sizeof(local) ? sizeof(local) : length;
        memcpy(local, data, chunk);
        spi_transaction_t transaction = {
            .length = chunk * 8,
            .tx_buffer = local,
        };
        esp_err_t result = spi_device_polling_transmit(s_spi, &transaction);
        if (result != ESP_OK) return result;
        data += chunk;
        length -= chunk;
    }
    return ESP_OK;
}

static esp_err_t write_command(uint8_t command)
{
    gpio_set_level(s_config.pin_dc, 0);
    gpio_set_level(s_config.pin_cs, 0);
    esp_err_t result = spi_write(&command, 1);
    gpio_set_level(s_config.pin_cs, 1);
    gpio_set_level(s_config.pin_dc, 1);
    return result;
}

static esp_err_t write_data(const uint8_t *data, size_t length)
{
    gpio_set_level(s_config.pin_dc, 1);
    gpio_set_level(s_config.pin_cs, 0);
    esp_err_t result = spi_write(data, length);
    gpio_set_level(s_config.pin_cs, 1);
    return result;
}

static esp_err_t command_data(uint8_t command, const uint8_t *data,
                              size_t length)
{
    esp_err_t result = write_command(command);
    return result == ESP_OK && length ? write_data(data, length) : result;
}

static bool busy_active(void *context)
{
    (void) context;
    return gpio_get_level(s_config.pin_busy) == 0;
}

static void busy_delay(void *context, uint32_t milliseconds)
{
    (void) context;
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

static esp_err_t wait_busy(const char *stage)
{
    busy_delay(NULL, UC8179_BUSY_POLL_MS);
    esp_err_t result = epaper_wait_busy_bounded(
        busy_active, busy_delay, NULL, UC8179_BUSY_POLL_MS,
        UC8179_BUSY_TIMEOUT_MS);
    if (result != ESP_OK) ESP_LOGW(TAG, "%s BUSY timeout", stage);
    return result;
}

static esp_err_t hardware_reset(void)
{
    s_panel_sleeping = false;
    gpio_set_level(s_config.pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(s_config.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    return wait_busy("reset");
}

static esp_err_t write_lut(uint8_t command, const uint8_t *lut)
{
    esp_err_t result = command_data(command, lut, 42);
    return result == ESP_OK ? wait_busy("lut") : result;
}

static esp_err_t initialize_external_gray4_waveform(void)
{
    static const uint8_t power[] = {0x07, 0x17, 0x3F, 0x3F, 0x07};
    static const uint8_t booster[] = {0x27, 0x27, 0x28, 0x17};
    static const uint8_t resolution[] = {0x03, 0x20, 0x01, 0xE0};
    esp_err_t result;
    if ((result = command_data(0x01, power, sizeof(power))) != ESP_OK ||
        (result = command_data(0x30, (uint8_t[]){0x06}, 1)) != ESP_OK ||
        (result = command_data(0x82, (uint8_t[]){0x12}, 1)) != ESP_OK ||
        (result = command_data(0x06, booster, sizeof(booster))) != ESP_OK ||
        (result = write_command(0x04)) != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(100));
    if ((result = wait_busy("power_on")) != ESP_OK ||
        (result = command_data(0x00, (uint8_t[]){0x3F}, 1)) != ESP_OK ||
        (result = command_data(0xE3, (uint8_t[]){0x88}, 1)) != ESP_OK ||
        (result = command_data(0x50, (uint8_t[]){0x10, 0x07}, 2)) != ESP_OK ||
        (result = command_data(0x52, (uint8_t[]){0x00}, 1)) != ESP_OK ||
        (result = command_data(0x61, resolution, sizeof(resolution))) != ESP_OK ||
        (result = write_lut(0x20, LUT_VCOM_GRAY)) != ESP_OK ||
        (result = write_lut(0x21, LUT_WW_GRAY)) != ESP_OK ||
        (result = write_lut(0x22, LUT_KW_GRAY)) != ESP_OK ||
        (result = write_lut(0x23, LUT_WK_GRAY)) != ESP_OK ||
        (result = write_lut(0x24, LUT_KK_GRAY)) != ESP_OK) return result;
    return ESP_OK;
}

static esp_err_t upload_and_refresh(const uint8_t *transport)
{
    esp_err_t result = write_command(0x10);
    if (result == ESP_OK)
        result = write_data(transport, EPAPER_GRAY4_PLANE_BYTES);
    if (result == ESP_OK) result = write_command(0x13);
    if (result == ESP_OK)
        result = write_data(transport + EPAPER_GRAY4_PLANE_BYTES,
                            EPAPER_GRAY4_PLANE_BYTES);
    if (result == ESP_OK) result = write_command(0x12);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(100));
    return wait_busy("refresh");
}

static esp_err_t power_off_and_sleep(void)
{
    esp_err_t result = write_command(0x02);
    if (result == ESP_OK) result = wait_busy("power_off");
    if (result == ESP_OK) {
        result = command_data(0x07, (uint8_t[]){0xA5}, 1);
        if (result == ESP_OK) s_panel_sleeping = true;
    }
    return result;
}

static esp_err_t display_transport(const uint8_t *transport)
{
    esp_err_t result = hardware_reset();
    if (result == ESP_OK) result = initialize_external_gray4_waveform();
    if (result == ESP_OK) result = upload_and_refresh(transport);
    if (result == ESP_OK) result = power_off_and_sleep();
    return result;
}

static esp_err_t uc8179_init(const epaper_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    s_panel_sleeping = false;
    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << config->pin_cs) | (1ULL << config->pin_dc) |
                        (1ULL << config->pin_rst),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_set_level(config->pin_cs, 1);
    gpio_set_level(config->pin_dc, 1);
    gpio_set_level(config->pin_rst, 1);
    esp_err_t result = gpio_config(&outputs);
    gpio_config_t busy = {
        .pin_bit_mask = 1ULL << config->pin_busy,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    if (result == ESP_OK) result = gpio_config(&busy);
    spi_device_interface_config_t device = {
        .clock_speed_hz = UC8179_SPI_HZ,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY,
    };
    if (result == ESP_OK)
        result = spi_bus_add_device(config->spi_host, &device, &s_spi);
    return result;
}

static esp_err_t uc8179_display_logical(const uint8_t *image, size_t image_size)
{
    if (!image) return ESP_ERR_INVALID_ARG;
    uint8_t *transport = heap_caps_malloc(EPAPER_E1001_TRANSPORT_BYTES,
                                          MALLOC_CAP_SPIRAM);
    if (!transport) return ESP_ERR_NO_MEM;
    esp_err_t result = epaper_encode_e1001_gray4(
        image, image_size, transport, EPAPER_E1001_TRANSPORT_BYTES);
    if (result == ESP_OK) result = display_transport(transport);
    free(transport);
    return result;
}

static esp_err_t uc8179_display(uint8_t *packed_gray4)
{
    if (!packed_gray4) return ESP_ERR_INVALID_ARG;
    uint8_t *logical = heap_caps_malloc(EPAPER_LOGICAL_FRAME_BYTES,
                                        MALLOC_CAP_SPIRAM);
    if (!logical) return ESP_ERR_NO_MEM;
    for (size_t pixel = 0; pixel < EPAPER_LOGICAL_FRAME_BYTES; ++pixel) {
        const uint8_t packed = packed_gray4[pixel / 4];
        logical[pixel] = (packed >> ((3u - (pixel & 3u)) * 2u)) & 0x03u;
    }
    esp_err_t result = uc8179_display_logical(logical, EPAPER_LOGICAL_FRAME_BYTES);
    free(logical);
    return result;
}

static esp_err_t uc8179_clear(uint8_t *packed_gray4, uint8_t color)
{
    if (!packed_gray4 || color > 3) return ESP_ERR_INVALID_ARG;
    const uint8_t fill = (uint8_t) (color * 0x55u);
    memset(packed_gray4, fill, EPAPER_E1001_TRANSPORT_BYTES);
    return uc8179_display(packed_gray4);
}

static esp_err_t uc8179_enter_deepsleep(void)
{
    return s_panel_sleeping ? ESP_OK : power_off_and_sleep();
}

static const epaper_backend_t BACKEND = {
    .name = "uc8179-gray4-experimental",
    .width = EPAPER_FRAME_WIDTH,
    .height = EPAPER_FRAME_HEIGHT,
    .init = uc8179_init,
    .display = uc8179_display,
    .display_logical = uc8179_display_logical,
    .clear = uc8179_clear,
    .set_temperature = NULL,
    .enter_deepsleep = uc8179_enter_deepsleep,
};

const epaper_backend_t *epaper_backend_uc8179_gray4(void)
{
    return &BACKEND;
}
