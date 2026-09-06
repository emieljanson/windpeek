#include <gtest/gtest.h>

#include <cstring>

extern "C" {
#include "wind_spots.h"
}

TEST(WindSpotsTest, ExposesOneInstalledSpot)
{
    ASSERT_EQ(wind_spots_count(), 1u);
    EXPECT_STREQ(wind_spots_at(0)->id, "brouwersdam");
    EXPECT_STREQ(wind_spots_at(0)->timezone, "Europe/Amsterdam");
    EXPECT_STREQ(wind_spots_device_timezone(), "Europe/Amsterdam");
    EXPECT_EQ(wind_spots_at(1), nullptr);
}

TEST(WindSpotsTest, NavigationStaysOnInstalledSpot)
{
    EXPECT_EQ(wind_spots_offset(0, 1), 0u);
    EXPECT_EQ(wind_spots_offset(0, -1), 0u);
}

TEST(WindSpotsTest, OnlyAcceptsTheInstalledIndex)
{
    ASSERT_EQ(wind_spots_store_selected(0), ESP_OK);
    size_t selected = 0;
    ASSERT_EQ(wind_spots_load_selected(&selected), ESP_OK);
    EXPECT_EQ(selected, 0u);
    EXPECT_EQ(wind_spots_store_selected(1), ESP_ERR_INVALID_ARG);
}
