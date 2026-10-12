// Compile the production coordinator with synchronous, re-entrant I/O probes.
#define ESP_PLATFORM 1
#include "ota_platform.h"
#include "freertos/semphr.h"
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static void vSemaphoreDelete(SemaphoreHandle_t lock) { free(lock); }
#define time ota_test_time
#include "../main/wind_device_errors.c"
void reports_test_reset(void) {
    if (report_mutex) vSemaphoreDelete(report_mutex);
    report_mutex = NULL;
    flushing = false;
    memset(stage_loaded, 0, sizeof(stage_loaded));
    memset(stage_pending, 0, sizeof(stage_pending));
}

size_t reports_test_legacy_state_size(void) { return sizeof(wind_device_error_state_v1_t); }
void reports_test_make_legacy(void *out, const wind_device_error_state_t *state) {
    wind_device_error_state_v1_t legacy = {
        .version = 1, .pending = state->pending,
        .occurred_at = state->occurred_at, .last_attempt = state->last_attempt,
    };
    memcpy(legacy.event_id, state->event_id, sizeof(legacy.event_id));
    memcpy(&legacy.error, &state->error, sizeof(legacy.error));
    memcpy(out, &legacy, sizeof(legacy));
}
