#include "wind_overview.h"
#include <stdlib.h>
#include "wind_renderer.h"

size_t wind_overview_last_page(size_t count) {
    return count ? (count - 1) / WIND_OVERVIEW_PAGE_SIZE : 0;
}

wind_touch_action_t wind_overview_hit_test(int x, int y, bool overview,
                                          size_t page, size_t count) {
    wind_touch_action_t action = {WIND_TOUCH_NONE, 0};
    if (x < 12 || x > 799 || y < 12 || y > 599) return action;
    if (!overview) {
        if (x > 787) return action;
        if (y <= 80) {
            if (count) action.kind = WIND_TOUCH_OPEN;
        } else {
            action.kind = WIND_TOUCH_TOGGLE_DAY;
            while (action.day_index < WIND_RENDERER_DAY_COUNT - 1 &&
                   x >= 12 + 775 * (int)(action.day_index + 1) / WIND_RENDERER_DAY_COUNT) ++action.day_index;
        }
        return action;
    }
    if (page > wind_overview_last_page(count)) return action;
    /* Two disjoint 46x46 logical targets (~12mm), larger than their artwork. */
    if (count > 3 && x >= 698 && x < 790 && y >= 538 && y < 584) {
        if (x < 744 && page < wind_overview_last_page(count)) action.kind = WIND_TOUCH_NEXT_PAGE;
        if (x >= 744 && page > 0) action.kind = WIND_TOUCH_PREVIOUS_PAGE;
        return action; /* A disabled control must not open the row behind it. */
    }
    if (x > 787 || y < WIND_OVERVIEW_TOP) return action;
    size_t row = (size_t)(y - WIND_OVERVIEW_TOP) / WIND_OVERVIEW_ROW_HEIGHT;
    size_t index = page * WIND_OVERVIEW_PAGE_SIZE + row;
    if (row < WIND_OVERVIEW_PAGE_SIZE && index < count) {
        action.kind = WIND_TOUCH_SELECT;
        action.spot_index = index;
    }
    return action;
}

wind_touch_action_t wind_touch_update(wind_touch_gesture_t *g,
    unsigned contacts, int x, int y, uint32_t now,
    bool overview, size_t page, size_t count) {
    wind_touch_action_t none = {WIND_TOUCH_NONE, 0};
    if (!g) return none;
    if (contacts) {
        if (!g->down) {
            *g = (wind_touch_gesture_t){.down=true, .start_x=x, .start_y=y,
                .last_x=x, .last_y=y, .started_ms=now};
        }
        if (contacts != 1) g->cancelled = true;
        g->last_x = x; g->last_y = y;
        int distance = abs(x-g->start_x) + abs(y-g->start_y);
        if (distance > g->max_distance) g->max_distance = distance;
        return none;
    }
    if (!g->down) return none;
    g->down = false;
    uint32_t duration = now - g->started_ms;
    if (g->cancelled || duration < 30 || duration > 2000) return none;
    int dx = g->last_x - g->start_x, dy = g->last_y - g->start_y;
    if (overview && abs(dy) >= 45 && abs(dy) > 2 * abs(dx)) {
        if (dy < 0 && page < wind_overview_last_page(count)) none.kind = WIND_TOUCH_NEXT_PAGE;
        if (dy > 0 && page > 0) none.kind = WIND_TOUCH_PREVIOUS_PAGE;
        return none;
    }
    if (g->max_distance > 12 || duration > 700) return none;
    return wind_overview_hit_test(g->start_x, g->start_y, overview, page, count);
}

/* Select which displayed forecasts own the device wake schedule. */
int64_t wind_overview_next_wake(const int64_t *deadlines, size_t count,
    size_t selected, bool overview, size_t page, int64_t fallback) {
    if (!deadlines || !count) return fallback;
    if (!overview) return selected<count ? deadlines[selected] : fallback;
    if (page>wind_overview_last_page(count)) return fallback;
    size_t first=page*WIND_OVERVIEW_PAGE_SIZE;
    int64_t next=fallback;
    for (size_t index=first; index<count && index<first+WIND_OVERVIEW_PAGE_SIZE; ++index)
        if (deadlines[index]>0 && deadlines[index]<next) next=deadlines[index];
    return next;
}
