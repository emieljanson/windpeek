#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared logical 800x600 layout; the E1003 projects this to 1872x1404. */
enum {
    WIND_OVERVIEW_PAGE_SIZE = 3,
    WIND_OVERVIEW_TOP = 47,
    WIND_OVERVIEW_ROW_HEIGHT = 180,
    WIND_OVERVIEW_BUTTON_X = 710,
    WIND_OVERVIEW_BUTTON_Y = 544,
    WIND_OVERVIEW_BUTTON_SIZE = 34,
};

typedef enum {
    WIND_TOUCH_NONE, WIND_TOUCH_OPEN, WIND_TOUCH_SELECT,
    WIND_TOUCH_NEXT_PAGE, WIND_TOUCH_PREVIOUS_PAGE, WIND_TOUCH_TOGGLE_DAY,
} wind_touch_action_kind_t;

typedef struct {
    wind_touch_action_kind_t kind;
    size_t spot_index;
    size_t day_index;
} wind_touch_action_t;

typedef struct {
    bool down;
    bool cancelled;
    int start_x, start_y, last_x, last_y;
    int max_distance;
    uint32_t started_ms;
} wind_touch_gesture_t;

/* Release-only actions. Multitouch, long holds and diagonal drags never tap. */
wind_touch_action_t wind_touch_update(wind_touch_gesture_t *gesture,
    unsigned contacts, int x, int y, uint32_t now_ms,
    bool overview, size_t page, size_t spot_count);
wind_touch_action_t wind_overview_hit_test(int x, int y, bool overview,
                                          size_t page, size_t spot_count);
size_t wind_overview_last_page(size_t spot_count);
size_t wind_overview_first_spot(size_t page, size_t spot_count);
int64_t wind_overview_next_wake(const int64_t *deadlines, size_t count,
    size_t selected, bool overview, size_t page, int64_t fallback);

#ifdef __cplusplus
}
#endif
