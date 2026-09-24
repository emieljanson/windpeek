#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <vector>
extern "C" {
#include "wind_navigation.h"
}

TEST(WindNavigation, WakeDirectionAndSleepPreserveSingleSpotStartup)
{
    for (size_t spots : {0u, 1u, 2u, 10u}) {
        EXPECT_EQ(wind_navigation_wake_direction(WAKEUP_SOURCE_ROTATE_BUTTON, spots), spots > 1 ? -1 : 0);
        EXPECT_EQ(wind_navigation_wake_direction(WAKEUP_SOURCE_CLEAR_BUTTON, spots), spots > 1 ? 1 : 0);
        EXPECT_EQ(wind_navigation_sleep_after_wake(WAKEUP_SOURCE_ROTATE_BUTTON, spots), spots > 1);
        EXPECT_EQ(wind_navigation_sleep_after_wake(WAKEUP_SOURCE_CLEAR_BUTTON, spots), spots > 1);
        EXPECT_TRUE(wind_navigation_sleep_after_wake(WAKEUP_SOURCE_TIMER, spots));
        for (auto wake : {WAKEUP_SOURCE_NONE, WAKEUP_SOURCE_BOOT_BUTTON, WAKEUP_SOURCE_EXT1_UNKNOWN}) {
            EXPECT_EQ(wind_navigation_wake_direction(wake, spots), 0);
            EXPECT_FALSE(wind_navigation_sleep_after_wake(wake, spots));
        }
    }
}

TEST(WindNavigation, ShortReleaseNavigatesOnceAndHeldBootButtonDoesNot)
{
    wind_navigation_buttons_t state{};
    EXPECT_EQ(wind_navigation_poll(&state, true, false, false, 2, 0), 0);
    EXPECT_EQ(wind_navigation_poll(&state, false, false, false, 2, 100), 0);
    EXPECT_EQ(wind_navigation_poll(&state, true, false, false, 2, 200), 0);
    EXPECT_EQ(wind_navigation_poll(&state, false, false, false, 2, 300), -1);
    EXPECT_EQ(wind_navigation_poll(&state, false, true, false, 2, 400), 0);
    EXPECT_EQ(wind_navigation_poll(&state, false, false, false, 2, 500), 1);
    EXPECT_EQ(wind_navigation_poll(&state, false, false, false, 2, 600), 0);
}

TEST(WindNavigation, DebounceHoldAndSingleSpotBoundaries)
{
    for (uint32_t duration : {49u, 50u, 2999u, 3000u}) {
        for (size_t spots : {1u, 2u}) {
            wind_navigation_buttons_t state{};
            wind_navigation_poll(&state, false, false, false, spots, 0);
            wind_navigation_poll(&state, true, false, false, spots, 100);
            EXPECT_EQ(wind_navigation_poll(&state, false, false, false, spots, 100 + duration),
                      spots > 1 && duration >= 50 && duration < 3000 ? -1 : 0);
        }
    }
}

TEST(WindNavigation, ChordAndInstallerCancelPressUntilReleased)
{
    for (bool installer : {false, true}) {
        wind_navigation_buttons_t state{};
        wind_navigation_poll(&state, false, false, false, 2, 0);
        wind_navigation_poll(&state, true, false, false, 2, 100);
        EXPECT_EQ(wind_navigation_poll(&state, true, !installer, installer, 2, 120), 0);
        EXPECT_EQ(wind_navigation_poll(&state, true, false, false, 2, 140), 0);
        EXPECT_EQ(wind_navigation_poll(&state, false, false, false, 2, 200), 0);
        wind_navigation_poll(&state, false, true, false, 2, 300);
        EXPECT_EQ(wind_navigation_poll(&state, false, false, false, 2, 400), 1);
    }
}

TEST(WindNavigation, MillisecondCounterWrapStillRecognizesRelease)
{
    wind_navigation_buttons_t state{};
    wind_navigation_poll(&state, false, false, false, 2, UINT32_MAX - 150);
    wind_navigation_poll(&state, true, false, false, 2, UINT32_MAX - 100);
    EXPECT_EQ(wind_navigation_poll(&state, false, false, false, 2, 20), -1);
}

struct FakeDashboard {
    std::vector<int> deadlines;
    std::vector<int> waits;
    size_t selected = 0;
};
static int deadline(void *context)
{
    auto &fake = *static_cast<FakeDashboard *>(context);
    return fake.deadlines.at(fake.selected);
}
static bool wait_and_select(void *context, int seconds)
{
    auto &fake = *static_cast<FakeDashboard *>(context);
    fake.waits.push_back(seconds);
    return ++fake.selected < fake.deadlines.size();
}

TEST(WindNavigation, UsbSelectionReplacesOldDeadlineWithFiveMinuteRetry)
{
    FakeDashboard fake{{4 * 3600, 5 * 60}};
    wind_navigation_wait_for_refresh(&fake, deadline, wait_and_select);
    EXPECT_EQ(fake.waits, (std::vector<int>{14400, 300}));
}

TEST(WindNavigation, ConsecutiveSelectionsRecomputeTimezoneBoundaryAndDueDeadline)
{
    FakeDashboard fake{{4 * 3600, 30 * 60, 0}};
    wind_navigation_wait_for_refresh(&fake, deadline, wait_and_select);
    EXPECT_EQ(fake.waits, (std::vector<int>{14400, 1800, 1}));
}

TEST(WindNavigation, DeviceUsesInterruptibleWaitAndNotifiesSuccessfulSelection)
{
    std::ifstream input(WIND_MAIN_SOURCE);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const auto source = buffer.str();
    const auto navigation = source.find("const esp_err_t result = direction < 0");
    const auto notification = source.find("xTaskNotifyGive(s_dashboard_task)", navigation);
    ASSERT_NE(navigation, std::string::npos);
    ASSERT_NE(notification, std::string::npos);
    EXPECT_NE(source.find("ulTaskNotifyTake(pdTRUE"), std::string::npos);
    EXPECT_NE(source.find("wind_navigation_wait_for_refresh(NULL, dashboard_seconds_until_wake, dashboard_wait_notified)"), std::string::npos);
    EXPECT_NE(source.find("&s_dashboard_task"), std::string::npos);
}
