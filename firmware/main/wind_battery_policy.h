#pragma once

#include <stdbool.h>

/* Conservative reserve for a full panel refresh; validate under load on hardware.
 * A higher recovery threshold prevents an unloaded battery rebound restarting Wi-Fi. */
#define WIND_BATTERY_EMPTY_MV 3450
#define WIND_BATTERY_RECOVER_MV 3650

typedef enum {
    WIND_BATTERY_RUN,
    WIND_BATTERY_RENDER_EMPTY,
    WIND_BATTERY_STAY_ASLEEP,
} wind_battery_action_t;

static inline wind_battery_action_t wind_battery_action(bool usb, int millivolts,
                                                        bool empty_latched)
{
    if (usb) return WIND_BATTERY_RUN;
    const bool valid = millivolts > 500 && millivolts <= 4500;
    if (empty_latched)
        return valid && millivolts >= WIND_BATTERY_RECOVER_MV
                   ? WIND_BATTERY_RUN : WIND_BATTERY_STAY_ASLEEP;
    return valid && millivolts <= WIND_BATTERY_EMPTY_MV
               ? WIND_BATTERY_RENDER_EMPTY : WIND_BATTERY_RUN;
}

/* Latch before either operation: failed persistence still has the RTC fallback,
 * and a failed panel operation must never cause repeated refresh attempts. */
static inline int wind_battery_render_once(bool *latched,
                                           int (*persist)(bool), int (*render)(void))
{
    if (*latched) return 0;
    *latched = true;
    (void)persist(true);
    return render();
}
