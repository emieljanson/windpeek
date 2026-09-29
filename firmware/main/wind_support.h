#pragma once
#include "installed_configuration.h"
#include "wind_provider.h"
#include "wind_app_status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIND_SUPPORT_HISTORY_LIMIT 32u
typedef enum {
    WIND_SUPPORT_BOOT,
    WIND_SUPPORT_SETUP_STARTED,
    WIND_SUPPORT_SETUP_COMPLETE,
    WIND_SUPPORT_SETUP_FAILED,
    WIND_SUPPORT_REFRESH_COMPLETE,
    WIND_SUPPORT_REFRESH_FAILED,
} wind_support_kind_t;

// Call after storage is mounted. Failure never blocks normal device operation.
esp_err_t wind_support_init(const char *directory, const char *new_device_id);
void wind_support_record(wind_support_kind_t kind, esp_err_t result, unsigned stage,
                         const installed_configuration_t *configuration,
                         const wind_provider_diagnostics_t *forecast);
void wind_support_record_refresh(const wind_app_status_t *status, unsigned last_stage);
// sequence=0 returns identity, installed settings and the bounded history range.
// Other requests return one immutable event, keeping USB frames below 16 KiB.
esp_err_t wind_support_read(uint32_t sequence, char *response, size_t size);

#ifdef __cplusplus
}
#endif
