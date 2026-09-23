#ifndef EPAPER_H
#define EPAPER_H

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

#include "epaper_backend.h"

// Resolution APIs
uint16_t epaper_get_width(void);
uint16_t epaper_get_height(void);

/**********************************
Color Index
**********************************/
#define EPD_7IN3E_BLACK 0x0   /// 000
#define EPD_7IN3E_WHITE 0x1   /// 001
#define EPD_7IN3E_YELLOW 0x2  /// 010
#define EPD_7IN3E_RED 0x3     /// 011
#define EPD_7IN3E_BLUE 0x5    /// 101
#define EPD_7IN3E_GREEN 0x6   /// 110

#ifdef __cplusplus
extern "C" {
#endif

// Configuration Struct
typedef struct epaper_config {
    int spi_host;
    int pin_cs;
    int pin_dc;
    int pin_rst;
    int pin_busy;
    int pin_cs1;     // For 13.3" Dual CS
    int pin_enable;  // Optional power enable
} epaper_config_t;

/* Stable E1002 panel step IDs, reported after a BUSY timeout. */
typedef enum {
    EPAPER_PANEL_NONE = 0,
    EPAPER_PANEL_RESET = 1,
    EPAPER_PANEL_INIT = 2,
    EPAPER_PANEL_POWER_ON = 3,
    EPAPER_PANEL_DATA = 4,
    EPAPER_PANEL_REFRESH = 5,
    EPAPER_PANEL_POWER_OFF = 6,
    EPAPER_PANEL_SLEEP_POWER_OFF = 7,
} epaper_panel_phase_t;

typedef struct {
    uint32_t phase;
    uint32_t wait_ms;
    uint32_t busy_level;
} epaper_panel_diagnostics_t;

void epaper_panel_diagnostics_reset(void);
void epaper_panel_diagnostics_timeout(epaper_panel_phase_t phase, uint32_t wait_ms,
                                      uint32_t busy_level);
epaper_panel_diagnostics_t epaper_panel_diagnostics_get(void);

/**
 * @brief Initialize the E-Paper display
 * @param cfg Configuration structure
 */
esp_err_t epaper_init(const epaper_config_t *cfg);

/**
 * @brief Send image buffer to display
 * @param image Pointer to image buffer
 */
esp_err_t epaper_display(uint8_t *image);

/** Encode and display one byte per logical 800x480 pixel. */
esp_err_t epaper_display_logical(const uint8_t *image, size_t image_size);

/**
 * @brief Clear display with specific color
 * @param image Buffer to use for clearing (size must match display)
 * @param color Color to clear with
 */
esp_err_t epaper_clear(uint8_t *image, uint8_t color);

/**
 * @brief Set the panel temperature in degrees Celsius.
 *
 * Controllers that select their display waveform by temperature (e.g. IT8951)
 * use this to pick the right waveform; feeding a live reading improves grayscale
 * quality and reduces ghosting. Only implemented by drivers that need it.
 *
 * @param celsius Panel temperature in degrees Celsius
 */
void epaper_set_temperature(int8_t celsius);

/**
 * @brief Enter deep sleep mode
 */
esp_err_t epaper_enter_deepsleep(void);

/** Select the only panel backend that may be used during this boot. */
esp_err_t epaper_select_backend(epaper_hardware_t hardware);

/** True only after a concrete backend has been selected. */
bool epaper_has_active_backend(void);

/** Diagnostic name for the active backend, or "none" while unselected. */
const char *epaper_active_backend_name(void);

/** Runtime model selected for this boot, or UNKNOWN before selection. */
epaper_hardware_t epaper_active_hardware(void);

/**
 * @brief Get display width
 * @return width in pixels
 */
uint16_t epaper_get_width(void);

/**
 * @brief Get display height
 * @return height in pixels
 */
uint16_t epaper_get_height(void);

#ifdef __cplusplus
}
#endif

#endif  // !EPAPER_H
