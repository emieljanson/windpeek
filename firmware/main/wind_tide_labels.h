#pragma once

#include <stdbool.h>

typedef struct {
    int anchor;
    int half_width;
    int center;
    int minimum;
    int maximum;
    bool visible;
} wind_tide_label_position_t;

// Input is chronological. Keep movement local, then omit an interior label
// when the row cannot fit. The curve and its extrema are never changed.
static inline void wind_tide_labels_place(wind_tide_label_position_t *labels,
                                         int count, int left, int right,
                                         int max_shift, int gap) {
    for (int i = 0; i < count; ++i) {
        wind_tide_label_position_t *label = &labels[i];
        const int low = left + label->half_width;
        const int high = right - label->half_width;
        label->visible = low <= high;
        const int anchor = label->anchor < low ? low : label->anchor > high ? high : label->anchor;
        label->minimum = anchor - max_shift < low ? low : anchor - max_shift;
        label->maximum = anchor + max_shift > high ? high : anchor + max_shift;
    }

    // Find the leftmost feasible placement, restarting after each omission.
    // Prefer removing an interior time over either end of a crowded group.
    for (;;) {
        int previous = -1;
        int before_previous = -1;
        bool retry = false;
        for (int i = 0; i < count; ++i) {
            if (!labels[i].visible) continue;
            int center = labels[i].minimum;
            if (previous >= 0) {
                const int required = labels[previous].center + labels[previous].half_width +
                                     gap + labels[i].half_width;
                if (center < required) center = required;
            }
            if (center > labels[i].maximum) {
                labels[before_previous >= 0 ? previous : i].visible = false;
                retry = true;
                break;
            }
            labels[i].center = center;
            before_previous = previous;
            previous = i;
        }
        if (!retry) break;
    }

    // Move back towards the actual times without undoing the spacing.
    int next = -1;
    for (int i = count - 1; i >= 0; --i) {
        if (!labels[i].visible) continue;
        int high = labels[i].maximum;
        if (next >= 0) {
            const int limit = labels[next].center - labels[next].half_width - gap - labels[i].half_width;
            if (high > limit) high = limit;
        }
        int center = labels[i].anchor < high ? labels[i].anchor : high;
        if (center < labels[i].center) center = labels[i].center;
        labels[i].center = center;
        next = i;
    }
}
