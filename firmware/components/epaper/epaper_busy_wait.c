#include "epaper_backend.h"
#include "epaper.h"
#include <stdatomic.h>

static atomic_uint s_panel_phase;
static atomic_uint s_panel_wait_ms;
static atomic_uint s_panel_busy_level;

void epaper_panel_diagnostics_reset(void)
{
    atomic_store(&s_panel_phase, EPAPER_PANEL_NONE);
    atomic_store(&s_panel_wait_ms, 0);
    atomic_store(&s_panel_busy_level, 0);
}

void epaper_panel_diagnostics_timeout(epaper_panel_phase_t phase, uint32_t wait_ms,
                                      uint32_t busy_level)
{
    // Cleanup can time out too; keep the first panel failure from this attempt.
    if (atomic_load(&s_panel_phase) != EPAPER_PANEL_NONE) return;
    atomic_store(&s_panel_wait_ms, wait_ms);
    atomic_store(&s_panel_busy_level, busy_level);
    atomic_store(&s_panel_phase, (unsigned)phase);
}

epaper_panel_diagnostics_t epaper_panel_diagnostics_get(void)
{
    return (epaper_panel_diagnostics_t) {
        .phase = atomic_load(&s_panel_phase),
        .wait_ms = atomic_load(&s_panel_wait_ms),
        .busy_level = atomic_load(&s_panel_busy_level),
    };
}

esp_err_t epaper_wait_busy_bounded(epaper_busy_reader_t is_busy, epaper_delay_t delay,
                                   void *context, uint32_t poll_interval_ms,
                                   uint32_t timeout_ms)
{
    if (!is_busy || !delay || poll_interval_ms == 0) return ESP_ERR_INVALID_ARG;
    uint32_t elapsed_ms = 0;
    while (is_busy(context)) {
        if (elapsed_ms >= timeout_ms) return ESP_ERR_TIMEOUT;
        delay(context, poll_interval_ms);
        elapsed_ms += poll_interval_ms;
    }
    return ESP_OK;
}
