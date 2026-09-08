#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>

extern "C" {
#include "wind_tide_cache.h"
}

static wind_tide_t cached_tide(int64_t retrieved_at)
{
    wind_tide_t tide;
    wind_tide_clear(&tide);
    std::strcpy(tide.spot_id, "brouwersdam");
    std::strcpy(tide.timezone, "Europe/Amsterdam");
    std::strcpy(tide.provider, "open-meteo-marine");
    tide.retrieved_at = retrieved_at;
    tide.capability = WIND_TIDE_AVAILABLE;
    tide.sample_count = 120;
    for (int index = 0; index < 120; ++index) {
        tide.samples[index].timestamp = 1787695200 + index * 3600;
        std::snprintf(tide.samples[index].local_date, sizeof(tide.samples[index].local_date),
                      "2026-08-%02d", 26 + index / 24);
        tide.samples[index].local_hour = static_cast<uint8_t>(index % 24);
        tide.samples[index].sea_level_mm = index - 60;
    }
    tide.extremum_count = 1;
    tide.extrema[0].timestamp = 1787716800;
    std::strcpy(tide.extrema[0].local_date, "2026-08-26");
    tide.extrema[0].local_hour = 6;
    tide.extrema[0].local_minute = 15;
    tide.extrema[0].is_high = 1;
    tide.extrema[0].sea_level_mm = 800;
    return tide;
}

class WindTideCacheTest : public testing::Test {
  protected:
    void SetUp() override
    {
        root = std::filesystem::temp_directory_path() / "windpeek-tide-cache-test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    void TearDown() override { std::filesystem::remove_all(root); }
    std::filesystem::path root;
};

TEST_F(WindTideCacheTest, RoundTripsBySpotAndTimezone)
{
    auto path = (root / "tide.cache").string();
    auto tide = cached_tide(1787698800);
    ASSERT_EQ(wind_tide_cache_store(path.c_str(), &tide), ESP_OK);
    wind_tide_cache_identity_t identity = {"brouwersdam", "Europe/Amsterdam"};
    wind_tide_t loaded;
    ASSERT_EQ(wind_tide_cache_load(path.c_str(), &identity, &loaded), ESP_OK);
    EXPECT_EQ(loaded.retrieved_at, tide.retrieved_at);
    EXPECT_EQ(loaded.samples[10].sea_level_mm, -50);
}

TEST_F(WindTideCacheTest, RecoversPreviousGenerationWhenNewestIsCorrupt)
{
    auto path = (root / "tide.cache").string();
    auto first = cached_tide(1787698800);
    auto second = cached_tide(1787702400);
    ASSERT_EQ(wind_tide_cache_store(path.c_str(), &first), ESP_OK);
    ASSERT_EQ(wind_tide_cache_store(path.c_str(), &second), ESP_OK);
    std::fstream file(path + ".b", std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file.is_open());
    file.seekp(24);
    file.put('\x55');
    file.close();
    wind_tide_cache_identity_t identity = {"brouwersdam", "Europe/Amsterdam"};
    wind_tide_t loaded;
    ASSERT_EQ(wind_tide_cache_load(path.c_str(), &identity, &loaded), ESP_OK);
    EXPECT_EQ(loaded.retrieved_at, first.retrieved_at);
}
