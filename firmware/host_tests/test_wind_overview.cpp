#include <gtest/gtest.h>
extern "C" {
#include "wind_overview.h"
}

TEST(WindOverview, HiddenControlsTakePriorityAndNeverOverlap) {
    EXPECT_EQ(wind_overview_hit_test(700, 540, true, 0, 10).kind, WIND_TOUCH_NEXT_PAGE);
    EXPECT_EQ(wind_overview_hit_test(743, 580, true, 1, 10).kind, WIND_TOUCH_NEXT_PAGE);
    EXPECT_EQ(wind_overview_hit_test(744, 580, true, 1, 10).kind, WIND_TOUCH_PREVIOUS_PAGE);
    EXPECT_EQ(wind_overview_hit_test(789, 580, true, 1, 10).kind, WIND_TOUCH_PREVIOUS_PAGE);
    EXPECT_EQ(wind_overview_hit_test(750, 560, true, 0, 10).kind, WIND_TOUCH_NONE);
    EXPECT_EQ(wind_overview_hit_test(720, 560, true, 3, 10).kind, WIND_TOUCH_NONE);
}
TEST(WindOverview, PartialPageDoesNotSelectNonexistentSpots) {
    EXPECT_EQ(wind_overview_last_page(10), 3u);
    EXPECT_EQ(wind_overview_hit_test(100, 100, true, 3, 10).spot_index, 9u);
    EXPECT_EQ(wind_overview_hit_test(100, 250, true, 3, 10).kind, WIND_TOUCH_NONE);
    EXPECT_EQ(wind_overview_hit_test(100, 35, true, 0, 10).kind, WIND_TOUCH_NONE);
    EXPECT_EQ(wind_overview_hit_test(100, 35, false, 0, 10).kind, WIND_TOUCH_OPEN);
}
TEST(WindOverview, SwipeIsOnePageAndNeverASelection) {
    wind_touch_gesture_t g{};
    EXPECT_EQ(wind_touch_update(&g,1,200,400,0,true,0,10).kind,WIND_TOUCH_NONE);
    EXPECT_EQ(wind_touch_update(&g,1,210,280,300,true,0,10).kind,WIND_TOUCH_NONE);
    EXPECT_EQ(wind_touch_update(&g,0,0,0,350,true,0,10).kind,WIND_TOUCH_NEXT_PAGE);
    EXPECT_EQ(wind_touch_update(&g,0,0,0,400,true,0,10).kind,WIND_TOUCH_NONE);
}
TEST(WindOverview, MovingAwayAndBackAndMultitouchCancelTap) {
    wind_touch_gesture_t g{};
    wind_touch_update(&g,1,200,100,0,true,0,10);
    wind_touch_update(&g,1,260,100,100,true,0,10);
    wind_touch_update(&g,1,200,100,200,true,0,10);
    EXPECT_EQ(wind_touch_update(&g,0,0,0,300,true,0,10).kind,WIND_TOUCH_NONE);
    wind_touch_update(&g,1,200,100,400,true,0,10);
    wind_touch_update(&g,2,200,100,450,true,0,10);
    EXPECT_EQ(wind_touch_update(&g,0,0,0,500,true,0,10).kind,WIND_TOUCH_NONE);
}

TEST(WindOverview, EverySelectablePixelResolvesToAnExistingSpot) {
    for(size_t count=0;count<=10;++count) {
        for(size_t page=0;page<=wind_overview_last_page(count);++page) {
            for(int y=0;y<600;++y)for(int x=0;x<800;++x) {
                const auto action=wind_overview_hit_test(x,y,true,page,count);
                if(action.kind==WIND_TOUCH_SELECT) {
                    ASSERT_LT(action.spot_index,count);
                    ASSERT_EQ(action.spot_index/3,page);
                }
                if(action.kind==WIND_TOUCH_NEXT_PAGE)ASSERT_LT(page,wind_overview_last_page(count));
                if(action.kind==WIND_TOUCH_PREVIOUS_PAGE)ASSERT_GT(page,0u);
            }
        }
    }
}
TEST(WindOverview, GestureLimitsAndClockWrapDoNotTriggerAccidentalSelection) {
    wind_touch_gesture_t g{};
    wind_touch_update(&g,1,200,100,UINT32_MAX-50,true,0,10);
    EXPECT_EQ(wind_touch_update(&g,0,0,0,49,true,0,10).kind,WIND_TOUCH_SELECT);
    wind_touch_update(&g,1,200,100,100,true,0,10);
    EXPECT_EQ(wind_touch_update(&g,0,0,0,120,true,0,10).kind,WIND_TOUCH_NONE);
    wind_touch_update(&g,1,200,100,200,true,0,10);
    EXPECT_EQ(wind_touch_update(&g,0,0,0,950,true,0,10).kind,WIND_TOUCH_NONE);
    wind_touch_update(&g,1,200,100,1000,true,0,10);
    wind_touch_update(&g,1,300,200,1100,true,0,10);
    EXPECT_EQ(wind_touch_update(&g,0,0,0,1200,true,0,10).kind,WIND_TOUCH_NONE);
    wind_touch_update(&g,1,200,100,1300,true,0,10);
    wind_touch_update(&g,1,200,220,1400,true,0,10);
    EXPECT_EQ(wind_touch_update(&g,0,0,0,1500,true,0,10).kind,WIND_TOUCH_NONE);
}

TEST(WindOverview, HiddenSpotRetryCannotKeepOverviewRefreshing) {
    const int64_t deadlines[]={100,500,600,700,400,900,800};
    EXPECT_EQ(wind_overview_next_wake(deadlines,7,0,true,1,1000),400);
    EXPECT_EQ(wind_overview_next_wake(deadlines,7,0,true,2,1000),800);
    EXPECT_EQ(wind_overview_next_wake(deadlines,7,0,false,1,1000),100);
    EXPECT_EQ(wind_overview_next_wake(nullptr,0,0,true,0,1000),1000);
}

TEST(WindOverview, EntireTitleOpensSpotsAndForecastTapsIdentifyDay) {
    EXPECT_EQ(wind_overview_hit_test(780, 40, false, 0, 3).kind, WIND_TOUCH_OPEN);
    for (int y : {81, 110, 240, 420, 599}) {
        for (int x : {20, 400, 780}) {
            EXPECT_NE(wind_overview_hit_test(x, y, false, 0, 3).kind, WIND_TOUCH_NONE);
        }
    }
    const int edges[][2] = {{12, 166}, {167, 321}, {322, 476}, {477, 631}, {632, 786}};
    for (size_t day = 0; day < 5; ++day) {
        const int left = edges[day][0];
        const int right = edges[day][1];
        EXPECT_EQ(wind_overview_hit_test(left, 200, false, 0, 3).day_index, day);
        EXPECT_EQ(wind_overview_hit_test(right, 420, false, 0, 3).day_index, day);
    }
    EXPECT_EQ(wind_overview_hit_test(800, 200, false, 0, 3).kind, WIND_TOUCH_NONE);
}
