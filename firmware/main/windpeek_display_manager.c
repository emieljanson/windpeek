#include <string.h>

#include "GUI_ColorMap.h"
#include "GUI_Paint.h"
#include "board_hal.h"
#include "display_manager.h"
#include "epaper.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "wind_display";
static SemaphoreHandle_t s_display_mutex;
static uint8_t *s_image_buffer;
static uint16_t s_display_width;
static uint16_t s_display_height;
static size_t s_frame_size;
static bool s_stream_active;

#define DISPLAY_LOCK_TIMEOUT_MS (120 * 1000)

static bool display_is_grayscale(void) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E100X
    return epaper_active_hardware() == EPAPER_HARDWARE_E1001;
#else
    return strncmp(BOARD_HAL_DISPLAY_TYPE, "gc", 2) == 0;
#endif
}

static bool display_uses_packed_gc16(void) {
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
    return true;
#else
    return false;
#endif
}

static void set_logical_pixel(int x, int y, uint8_t value) {
    if (display_uses_packed_gc16()) {
        uint8_t *packed = &s_image_buffer[((size_t)y * s_display_width + x) / 2];
        if ((x & 1) == 0) *packed = (uint8_t)((*packed & 0x0Fu) | (value << 4));
        else *packed = (uint8_t)((*packed & 0xF0u) | value);
        return;
    }
    s_image_buffer[(size_t)y * s_display_width + x] = value;
}

static UWORD display_white(void) {
    return display_is_grayscale() ? 3 : EPD_7IN3E_WHITE;
}

void display_manager_initialize_paint(void) {
    /* Windpeek keeps one byte per logical pixel until the selected backend. */
}

esp_err_t display_manager_init(void) {
    if (s_display_mutex || s_image_buffer) return ESP_ERR_INVALID_STATE;
    s_display_mutex = xSemaphoreCreateMutex();
    if (!s_display_mutex) return ESP_ERR_NO_MEM;

    s_display_width = epaper_get_width();
    s_display_height = epaper_get_height();
    const size_t logical_pixels = (size_t)s_display_width * s_display_height;
    s_frame_size = display_uses_packed_gc16() ? logical_pixels / 2 : logical_pixels;
    if (s_display_width == 0 || s_display_height == 0 || s_frame_size == 0 ||
        (display_uses_packed_gc16() && (s_display_width & 1u) != 0)) {
        vSemaphoreDelete(s_display_mutex);
        s_display_mutex = NULL;
        return ESP_ERR_INVALID_SIZE;
    }
    s_image_buffer = heap_caps_malloc(s_frame_size, MALLOC_CAP_SPIRAM);
    if (!s_image_buffer) {
        vSemaphoreDelete(s_display_mutex);
        s_display_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    display_manager_initialize_paint();
    ESP_LOGI(TAG, "Wind dashboard display initialized");
    return ESP_OK;
}

esp_err_t display_manager_begin_rgb_stream(void) {
    if (!s_display_mutex ||
        xSemaphoreTake(s_display_mutex, pdMS_TO_TICKS(DISPLAY_LOCK_TIMEOUT_MS)) !=
            pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(s_image_buffer, display_uses_packed_gc16() ? 0xFF : display_white(),
           s_frame_size);
    s_stream_active = true;
    return ESP_OK;
}

esp_err_t display_manager_push_rgb_row(int y, const uint8_t *rgb_row, int width) {
    if (!rgb_row || y < 0 || width < 0) return ESP_ERR_INVALID_ARG;
    if (!s_stream_active) return ESP_ERR_INVALID_STATE;
    if (y >= s_display_height) return ESP_OK;
    GUI_RGBMapFn map_rgb = display_uses_packed_gc16()
                               ? GUI_RGBToGray16
                               : display_is_grayscale() ? GUI_RGBToGray4
                                                        : GUI_RGBToSpectra6Logical;
    for (int x = 0; x < width && x < s_display_width; ++x) {
        const uint8_t *pixel = &rgb_row[x * 3];
        set_logical_pixel(x, y, map_rgb(pixel[0], pixel[1], pixel[2]));
    }
    return ESP_OK;
}

esp_err_t display_manager_push_palette_row(int y, const uint8_t *palette_row,
                                           int width) {
    if (!palette_row || y < 0 || width < 0) return ESP_ERR_INVALID_ARG;
    if (!s_stream_active) return ESP_ERR_INVALID_STATE;
    if (y >= s_display_height) return ESP_OK;
    const int copy_width = width < s_display_width ? width : s_display_width;
    if (display_uses_packed_gc16()) {
        for (int x = 0; x < copy_width; ++x) {
            if (palette_row[x] > 15) return ESP_ERR_INVALID_ARG;
            set_logical_pixel(x, y, palette_row[x]);
        }
    } else {
        memcpy(s_image_buffer + (size_t)y * s_display_width, palette_row,
               (size_t)copy_width);
    }
    return ESP_OK;
}

esp_err_t display_manager_end_rgb_stream(bool show, const display_publish_t *publish) {
    (void)publish;
    if (!s_display_mutex || !s_stream_active) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ESP_OK;
    if (show) {
        result = display_uses_packed_gc16()
                     ? epaper_display(s_image_buffer)
                     : epaper_display_logical(s_image_buffer, s_frame_size);
    }
    s_stream_active = false;
    xSemaphoreGive(s_display_mutex);
    return result;
}
