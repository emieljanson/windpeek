#pragma once

#include <stdbool.h>
#include <limits.h>
#include <stdint.h>

#include "wind_renderer.h"

typedef struct {
    int anchor;
    int half_width;
    int center;
    int minimum;
    int maximum;
    bool visible;
} wind_tide_label_position_t;

// Input is chronological, at most WIND_RENDERER_MAX_TIDE_EXTREMA labels.
// Keep the largest subset that fits with local movement; never change the curve.
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

    // For each possible count, retain the selection with the earliest right
    // edge. A later label only needs that edge to decide whether it fits.
    // This avoids greedy omissions that discard more times than necessary.
    int best_right[WIND_RENDERER_MAX_TIDE_EXTREMA + 1];
    uint32_t selections[WIND_RENDERER_MAX_TIDE_EXTREMA + 1] = {0};
    for (int kept = 0; kept <= count; ++kept) best_right[kept] = INT_MAX;
    best_right[0] = left - gap;
    int shown = 0;
    uint32_t selected = 0;
    for (int i = 0; i < count; ++i) {
        if (!labels[i].visible) continue;
        for (int kept = i + 1; kept >= 1; --kept) {
            if (best_right[kept - 1] == INT_MAX) continue;
            int center = best_right[kept - 1] + gap + labels[i].half_width;
            if (center < labels[i].minimum) center = labels[i].minimum;
            if (center > labels[i].maximum) continue;
            const uint32_t selection = selections[kept - 1] | (UINT32_C(1) << i);
            // At equal counts, keep a later endpoint where possible, so a
            // crowded triple prefers its outer times over the middle one.
            if (kept > shown || (kept == shown && kept > 1)) {
                shown = kept;
                selected = selection;
            }
            const int edge = center + labels[i].half_width;
            if (edge < best_right[kept]) {
                best_right[kept] = edge;
                selections[kept] = selection;
            }
        }
    }

    int previous_right = left - gap;
    for (int i = 0; i < count; ++i) {
        labels[i].visible = (selected & (UINT32_C(1) << i)) != 0;
        if (!labels[i].visible) continue;
        int center = previous_right + gap + labels[i].half_width;
        if (center < labels[i].minimum) center = labels[i].minimum;
        labels[i].center = center;
        previous_right = center + labels[i].half_width;
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
