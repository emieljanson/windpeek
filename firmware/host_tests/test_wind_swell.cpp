#include <gtest/gtest.h>
#include <cstring>
#include <string>
extern "C" {
#include "wind_swell.h"
#include "wind_swell_cache.h"
}
TEST(WindSwell, ParsesZeroAndMissingWithoutInventingValues) {
    open_meteo_marine_config_t config = {"coast", 52, 4, "Europe/Amsterdam", "meteofrance_wave"};
    const char *json = R"({"timezone":"Europe/Amsterdam","hourly_units":{"time":"unixtime","swell_wave_height":"m","swell_wave_period":"s","swell_wave_direction":"°"},"hourly":{"time":[1788883200,1788886800],"swell_wave_height":[0,null],"swell_wave_period":[9,null],"swell_wave_direction":[0,null]}})";
    wind_swell_t swell;
    ASSERT_EQ(wind_swell_parse(&config, json, strlen(json), 1788883200, &swell), ESP_OK);
    EXPECT_EQ(swell.samples[0].height_cm, 0);
    EXPECT_EQ(swell.samples[0].period_tenths, 90);
    EXPECT_EQ(swell.samples[0].destination_degrees, 180);
    EXPECT_EQ(swell.samples[1].height_cm, -1);
    EXPECT_EQ(swell.samples[1].period_tenths, -1);
    EXPECT_EQ(swell.samples[1].destination_degrees, -1);
    const std::string path = "/tmp/windpeek-swell-cache-test";
    ASSERT_EQ(wind_swell_cache_store(path.c_str(), &swell), ESP_OK);
    wind_swell_cache_identity_t identity = {"coast", "Europe/Amsterdam", "meteofrance_wave"};
    wind_swell_t restored;
    ASSERT_EQ(wind_swell_cache_load(path.c_str(), &identity, &restored), ESP_OK);
    identity.model = "best_match";
    EXPECT_NE(wind_swell_cache_load(path.c_str(), &identity, &restored), ESP_OK);
    remove((path + ".a").c_str()); remove((path + ".b").c_str());
    swell.samples[1].timestamp = swell.samples[0].timestamp;
    EXPECT_FALSE(wind_swell_validate(&swell));
}

TEST(WindSwell, PreservesSecondaryComponentAndCache) {
    open_meteo_marine_config_t config = {"coast", 52, 4, "Europe/Amsterdam", "meteofrance_wave"};
    const char *json = R"({"timezone":"Europe/Amsterdam","hourly_units":{"time":"unixtime","swell_wave_height":"m","swell_wave_period":"s","swell_wave_direction":"°","secondary_swell_wave_height":"m","secondary_swell_wave_period":"s","secondary_swell_wave_direction":"°"},"hourly":{"time":[1788883200,1788886800],"swell_wave_height":[1,1],"swell_wave_period":[9,9],"swell_wave_direction":[270,270],"secondary_swell_wave_height":[0.45,null],"secondary_swell_wave_period":[7.2,null],"secondary_swell_wave_direction":[270,null]}})";
    wind_swell_t swell;
    ASSERT_EQ(wind_swell_parse(&config, json, strlen(json), 1788883200, &swell), ESP_OK);
    EXPECT_EQ(swell.samples[0].secondary_height_cm, 45);
    EXPECT_EQ(swell.samples[0].secondary_period_tenths, 72);
    EXPECT_EQ(swell.samples[0].secondary_destination_degrees, 90);
    EXPECT_EQ(swell.samples[1].secondary_height_cm, -1);
    const char *path = "/tmp/windpeek-secondary-swell-cache-test";
    ASSERT_EQ(wind_swell_cache_store(path, &swell), ESP_OK);
    wind_swell_cache_identity_t identity = {"coast", "Europe/Amsterdam", "meteofrance_wave"};
    wind_swell_t restored;
    ASSERT_EQ(wind_swell_cache_load(path, &identity, &restored), ESP_OK);
    EXPECT_EQ(restored.samples[0].secondary_height_cm, 45);
    remove("/tmp/windpeek-secondary-swell-cache-test.a");
    remove("/tmp/windpeek-secondary-swell-cache-test.b");
}

TEST(WindSwell, ChoosesFinestAvailableGfsGrid) {
    EXPECT_STREQ(wind_swell_base_model("best_match"), "ncep_gfswave025");
    EXPECT_STREQ(wind_swell_base_model(nullptr), "ncep_gfswave025");
    EXPECT_STREQ(wind_swell_base_model("meteofrance_wave"), "ncep_gfswave025");
    EXPECT_STREQ(wind_swell_base_model("dwd_ewam"), "dwd_gwam");
    EXPECT_STREQ(wind_swell_base_model("ncep_gfswave025"), "ncep_gfswave025");
    EXPECT_STREQ(wind_swell_preferred_model("ncep_gfswave025", 51.76), "ncep_gfswave016");
    EXPECT_STREQ(wind_swell_preferred_model("ncep_gfswave025", -15), "ncep_gfswave016");
    EXPECT_STREQ(wind_swell_preferred_model("ncep_gfswave025", 52.5), "ncep_gfswave016");
    EXPECT_STREQ(wind_swell_preferred_model("ncep_gfswave025", 52.6), "ncep_gfswave025");
    EXPECT_STREQ(wind_swell_preferred_model("ncep_gfswave025", -34), "ncep_gfswave025");
    EXPECT_STREQ(wind_swell_preferred_model("meteofrance_wave", 52), "meteofrance_wave");
}

TEST(WindSwell, FillsShortForecastWithoutMixingSampleComponents) {
    wind_swell_t base = {}, fine = {};
    base.sample_count = 4; fine.sample_count = 3;
    for (int i = 0; i < 4; ++i) base.samples[i] = {100+i, 100, 90, 180, 30, 60, 0};
    for (int i = 0; i < 3; ++i) fine.samples[i] = {100+i, 200, 110, 90, 40, 70, 20};
    fine.samples[1].period_tenths = -1;
    fine.samples[2].height_cm = 0;
    wind_swell_overlay(&base, &fine);
    EXPECT_EQ(base.samples[0].height_cm, 200);
    EXPECT_EQ(base.samples[0].secondary_height_cm, 40);
    EXPECT_EQ(base.samples[1].height_cm, 100);
    EXPECT_EQ(base.samples[1].period_tenths, 90);
    EXPECT_EQ(base.samples[1].secondary_height_cm, 30);
    EXPECT_EQ(base.samples[2].height_cm, 0);
    EXPECT_EQ(base.samples[3].height_cm, 100);
}

TEST(WindSwell, PreservesPreferredHoursBeyondShortFallback) {
    wind_swell_t base = {}, preferred = {};
    base.sample_count = 1;
    base.samples[0] = {100, 100, 90, 180, -1, -1, -1};
    preferred.sample_count = 3;
    for (int i = 0; i < 3; ++i) preferred.samples[i] = {100+i, 200, 100, 90, -1, -1, -1};
    wind_swell_overlay(&base, &preferred);
    ASSERT_EQ(base.sample_count, 3u);
    EXPECT_EQ(base.samples[2].height_cm, 200);
}

TEST(WindSwell, RejectsEmptyForecastInsteadOfReplacingGoodCache) {
    open_meteo_marine_config_t config = {"coast", 52, 4, "Europe/Amsterdam", "best_match"};
    const char *json = R"({"timezone":"Europe/Amsterdam","hourly_units":{"time":"unixtime","swell_wave_height":"m","swell_wave_period":"s","swell_wave_direction":"°"},"hourly":{"time":[1788883200],"swell_wave_height":[null],"swell_wave_period":[null],"swell_wave_direction":[null]}})";
    wind_swell_t swell;
    EXPECT_NE(wind_swell_parse(&config, json, strlen(json), 1788883200, &swell), ESP_OK);
}

TEST(WindSwell, MergesEarlierHoursAndRetainsPartialHeights) {
    wind_swell_t base = {}, preferred = {};
    base.sample_count = 2;
    base.samples[0] = {101, -1, -1, -1, -1, -1, -1};
    base.samples[1] = {103, 100, 90, 180, 20, 70, 0};
    preferred.sample_count = 3;
    preferred.samples[0] = {100, 200, 100, 90, -1, -1, -1};
    preferred.samples[1] = {101, 200, -1, -1, -1, -1, -1};
    preferred.samples[2] = {103, 200, -1, -1, -1, -1, -1};
    ASSERT_EQ(wind_swell_overlay(&base, &preferred), ESP_OK);
    ASSERT_EQ(base.sample_count, 3u);
    EXPECT_EQ(base.samples[0].timestamp, 100);
    EXPECT_EQ(base.samples[1].height_cm, 200);
    EXPECT_EQ(base.samples[1].period_tenths, -1);
    EXPECT_EQ(base.samples[2].height_cm, 100);
    EXPECT_EQ(base.samples[2].secondary_height_cm, 20);
}

TEST(WindSwell, OversizedMergeLeavesBaseUntouched) {
    wind_swell_t base = {}, preferred = {};
    base.sample_count = WIND_SWELL_MAX_SAMPLES;
    for (size_t i = 0; i < base.sample_count; ++i) base.samples[i] = {100+(int64_t)i, 100, 90, 180, -1, -1, -1};
    preferred.sample_count = 1;
    preferred.samples[0] = {99, 200, 100, 90, -1, -1, -1};
    const wind_swell_t before = base;
    EXPECT_EQ(wind_swell_overlay(&base, &preferred), ESP_ERR_INVALID_SIZE);
    EXPECT_EQ(memcmp(&base, &before, sizeof(base)), 0);
}
