#include <gtest/gtest.h>
#include <algorithm>
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
TEST(WindOverview, FinalPageKeepsAllThreeSpotRowsFilled) {
    EXPECT_EQ(wind_overview_last_page(10), 3u);
    EXPECT_EQ(wind_overview_first_spot(1, 4), 1u);
    EXPECT_EQ(wind_overview_first_spot(1, 5), 2u);
    EXPECT_EQ(wind_overview_first_spot(3, 10), 7u);
    EXPECT_EQ(wind_overview_hit_test(100, 430, true, 1, 4).spot_index, 3u);
    EXPECT_EQ(wind_overview_hit_test(100, 430, true, 1, 5).spot_index, 4u);
    for (size_t count = 1; count <= 10; ++count) {
        size_t previous = 0;
        for (size_t page = 0; page <= wind_overview_last_page(count); ++page) {
            size_t first = wind_overview_first_spot(page, count);
            EXPECT_LE(first - previous, WIND_OVERVIEW_PAGE_SIZE);
            EXPECT_EQ(std::min(count - first, (size_t)WIND_OVERVIEW_PAGE_SIZE),
                      std::min(count, (size_t)WIND_OVERVIEW_PAGE_SIZE));
            previous = first;
        }
        EXPECT_EQ(previous + std::min(count, (size_t)WIND_OVERVIEW_PAGE_SIZE), count);
    }
    EXPECT_EQ(wind_overview_hit_test(100, 100, true, 3, 10).spot_index, 7u);
    EXPECT_EQ(wind_overview_hit_test(100, 250, true, 3, 10).spot_index, 8u);
    EXPECT_EQ(wind_overview_hit_test(100, 430, true, 3, 10).spot_index, 9u);
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
                    ASSERT_EQ(action.spot_index,wind_overview_first_spot(page,count)+(size_t)((y-WIND_OVERVIEW_TOP)/WIND_OVERVIEW_ROW_HEIGHT));
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
    EXPECT_EQ(wind_overview_next_wake(deadlines,7,0,true,2,1000),400);
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
    EXPECT_EQ(wind_overview_hit_test(166, 200, false, 0, 3).day_index, 0u);
    EXPECT_EQ(wind_overview_hit_test(167, 200, false, 0, 3).day_index, 1u);
    EXPECT_EQ(wind_overview_hit_test(321, 420, false, 0, 3).day_index, 1u);
    EXPECT_EQ(wind_overview_hit_test(322, 420, false, 0, 3).day_index, 2u);
    EXPECT_EQ(wind_overview_hit_test(632, 420, false, 0, 3).day_index, 4u);
    EXPECT_EQ(wind_overview_hit_test(800, 200, false, 0, 3).kind, WIND_TOUCH_NONE);
}
