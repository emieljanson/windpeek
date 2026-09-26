// Compile the actual embedded navigation/rendering path. Only hardware services
// are supplied by the harness; cache, schedule and rendering remain production.
#define ESP_PLATFORM 1
#define WIFI_MANAGER_H
#include <stdbool.h>
#include <time.h>
bool wifi_manager_is_connected(void);
time_t runtime_test_time(time_t *out);
#define time runtime_test_time
#define wind_renderer_render_for_display runtime_test_render_for_display
#include "../main/wind_app.c"

void runtime_test_reset(bool preserve_rtc) {
    free(s_fetch_lock);
    s_fetch_lock = NULL;
    free(s_spots);
    s_spots = NULL;
    free(s_app_lock);
    s_app_lock = NULL;
    free(s_runtime_lock);
    s_runtime_lock = NULL;
    s_ready = false;
    s_preview_configuration = NULL;
    s_last_render_succeeded = false;
    s_force_next_display = false;
    s_selected_index = 0;
    if (!preserve_rtc) {
        atomic_store(&s_displayed_view, VIEW_UNINITIALIZED);
        s_overview_configuration = 0;
        s_focused_date[0] = 0;
    }
}
