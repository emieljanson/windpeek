#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char version[32];
    char manifest[160];
    char sha256[65];
} wind_ota_pointer_t;
typedef struct {
    char file[160];
    char sha256[65];
    uint32_t size;
    uint32_t sequence;
} wind_ota_release_t;

esp_err_t wind_ota_parse_pointer(const char *json, wind_ota_pointer_t *pointer);
esp_err_t wind_ota_parse_manifest(const char *json, const char *board,
    const wind_ota_pointer_t *pointer, size_t partition_size, wind_ota_release_t *release);
bool wind_ota_should_check(int64_t now, int64_t last_check, bool connected,
    bool installer_active, bool usb, int battery_mv);
// Called only during existing device work, with the power work lock held.
void wind_ota_maybe_update(void);
void wind_ota_finish_boot(bool healthy);
#ifdef __cplusplus
}
#endif
