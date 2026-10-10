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
