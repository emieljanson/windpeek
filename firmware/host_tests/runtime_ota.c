// Exercise the production HTTPS/download/partition code, replacing hardware I/O.
#define ESP_PLATFORM 1
#include "ota_platform.h"
#define time ota_test_time
#include "../main/wind_ota.c"
