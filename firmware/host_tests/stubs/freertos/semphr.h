#pragma once
#include <assert.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#define portMAX_DELAY (-1)
typedef int *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return calloc(1, sizeof(int));
}
static inline int xSemaphoreTake(SemaphoreHandle_t lock, TickType_t timeout) {
    if (!lock) return pdFALSE;
    if (*lock && timeout == 0) return pdFALSE;
    assert(!*lock); // Detect recursive locking in the sequential runtime harness.
    *lock = 1;
    return pdTRUE;
}
static inline void xSemaphoreGive(SemaphoreHandle_t lock) {
    assert(lock && *lock);
    *lock = 0;
}
