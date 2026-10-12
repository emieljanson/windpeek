#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "wind_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIND_DEVICE_ERROR_ENVELOPE_SIZE 1536

typedef enum {
    WIND_DEVICE_ERROR_FORECAST,
    WIND_DEVICE_ERROR_RENDER,
    WIND_DEVICE_ERROR_DISPLAY,
    WIND_DEVICE_ERROR_RUNTIME,
    WIND_DEVICE_ERROR_OTA,
    WIND_DEVICE_ERROR_COUNT,
} wind_device_error_stage_t;

// Numeric diagnostics and validated firmware/board labels only: never credentials, location, or log text.
typedef struct {
    int32_t error;
    uint32_t panel_phase;
    uint32_t panel_wait_ms;
    uint32_t panel_busy_level;
    int32_t battery_mv;
    bool usb;
    uint32_t reset_reason;
    char firmware[64];
    char board[48];
    wind_provider_diagnostics_t forecast;
    bool has_forecast_diagnostics;
} wind_device_error_t;

typedef struct {
    uint32_t version;
    bool pending;
    int64_t occurred_at;
    int64_t last_attempt;
    char event_id[33];
    wind_device_error_t error;
} wind_device_error_state_t;

typedef struct {
    void *context;
    esp_err_t (*load)(void *, wind_device_error_stage_t, wind_device_error_state_t *);
    esp_err_t (*store)(void *, wind_device_error_stage_t, const wind_device_error_state_t *);
    uint32_t (*random_u32)(void *);
    esp_err_t (*send)(void *, wind_device_error_stage_t, const wind_device_error_state_t *);
} wind_device_error_dependencies_t;

// At most one pending report per stage; at most one send per stage per hour.
// Backward clock corrections reset the rate-limit window.
// Persist before sending so reset/offline/retry cannot cause a report storm.
esp_err_t wind_device_error_run(const wind_device_error_dependencies_t *dependencies,
    wind_device_error_stage_t stage, const wind_device_error_t *error,
    int64_t now, bool connected);
esp_err_t wind_device_error_envelope(wind_device_error_stage_t stage,
    const wind_device_error_state_t *state, char *output, size_t size);
void wind_device_errors_capture(wind_device_error_stage_t stage, esp_err_t error);
void wind_device_errors_flush(void);

#ifdef __cplusplus
}
#endif
