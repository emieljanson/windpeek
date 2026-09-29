#include "wind_app_status.h"

#ifdef ESP_PLATFORM
#include "wind_support.h"
#include "freertos/FreeRTOS.h"
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
#define LOCK() portENTER_CRITICAL(&s_status_lock)
#define UNLOCK() portEXIT_CRITICAL(&s_status_lock)
#else
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#endif

// The runtime lock serializes writers. A tiny critical section lets the UART
// read a consistent snapshot without waiting for networking or panel output.
static wind_app_status_t s_status;

void wind_app_status_begin(void)
{
    LOCK();
    s_status = (wind_app_status_t){.stage = WIND_REFRESH_PREPARING};
    UNLOCK();
}

void wind_app_status_stage(wind_refresh_stage_t stage)
{
    LOCK();
    s_status.stage = stage;
    UNLOCK();
}

void wind_app_status_finish(esp_err_t result, esp_err_t fetch_result,
                            bool attempted_fetch, bool render_valid,
                            const wind_provider_diagnostics_t *forecast)
{
    wind_app_status_t completed = {
        .stage = result == ESP_OK && render_valid ? WIND_REFRESH_COMPLETE : WIND_REFRESH_FAILED,
        .result = result, .fetch_result = fetch_result, .attempted_fetch = attempted_fetch,
    };
    if (attempted_fetch && forecast) completed.forecast = *forecast;
    LOCK();
#ifdef ESP_PLATFORM
    const unsigned last_stage = s_status.stage;
#endif
    s_status = completed;
    UNLOCK();
#ifdef ESP_PLATFORM
    // File writes must stay outside the status critical section. Logging is
    // best-effort and never changes the refresh result.
    wind_support_record_refresh(&completed, last_stage);
#endif
}

void wind_app_status_get(wind_app_status_t *out)
{
    if (!out) return;
    LOCK();
    *out = s_status;
    UNLOCK();
}
