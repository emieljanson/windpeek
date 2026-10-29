#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <cstring>
#include <string>

extern "C" {
#include "wind_cache.h"
}

static wind_forecast_t cache_forecast(int64_t retrieved_at = 1787544000)
{
    wind_forecast_t value;
    wind_forecast_clear(&value);
    std::strcpy(value.spot_id, "edam");
    std::strcpy(value.spot_name, "Edam");
    std::strcpy(value.timezone, "Europe/Amsterdam");
    std::strcpy(value.provider, "open-meteo");
    std::strcpy(value.model, "knmi_seamless");
    value.latitude = 52.5;
    value.longitude = 5.0;
    value.retrieved_at = retrieved_at;
    const int hours[] = {8, 11, 14, 17, 20};
    for (int d = 0; d < 5; ++d) {
        std::snprintf(value.days[d].local_date, sizeof(value.days[d].local_date), "2026-08-%02d",
                      24 + d);
        for (int s = 0; s < 5; ++s) {
            value.days[d].samples[s] = {retrieved_at + d * 86400 + s * 10800,
                                        static_cast<uint8_t>(hours[s]), 12, 18, 90,
                                        35, 25, 1, 1, 125, 1};
        }
    }
    return value;
}

class WindCacheTest : public testing::Test {
  protected:
    void SetUp() override
    {
        root = std::filesystem::temp_directory_path() /
            (std::string("einkwind-cache-test-") +
             testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    void TearDown() override { std::filesystem::remove_all(root); }
    std::filesystem::path root;
};

TEST_F(WindCacheTest, StoresAndLoadsCompatibleForecastAtomically)
{
    auto path = (root / "forecast.cache").string();
    auto forecast = cache_forecast();
    ASSERT_EQ(wind_cache_store(path.c_str(), &forecast), ESP_OK);
    wind_cache_identity_t identity = {"edam", "Europe/Amsterdam", "knmi_seamless"};
    wind_forecast_t loaded;
    ASSERT_EQ(wind_cache_load(path.c_str(), &identity, &loaded), ESP_OK);
    EXPECT_EQ(loaded.retrieved_at, forecast.retrieved_at);
    EXPECT_EQ(loaded.days[0].samples[0].cloud_cover_percent, 35);
    EXPECT_EQ(loaded.days[0].samples[0].precipitation_hundredths_mm, 25);
    EXPECT_EQ(loaded.days[0].samples[0].temperature_tenths_c, 125);
    EXPECT_FALSE(std::filesystem::exists(path + ".a.tmp"));
    EXPECT_FALSE(std::filesystem::exists(path + ".b.tmp"));
}

TEST_F(WindCacheTest, RecoversPreviousGenerationWhenNewestIsCorrupt)
{
    auto path = (root / "forecast.cache").string();
    auto first = cache_forecast(1787544000);
    auto second = cache_forecast(1787547600);
    ASSERT_EQ(wind_cache_store(path.c_str(), &first), ESP_OK);
    ASSERT_EQ(wind_cache_store(path.c_str(), &second), ESP_OK);

    std::fstream file(path + ".b", std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file.is_open());
    file.seekp(24);
    file.put('\x55');
    file.close();

    wind_cache_identity_t identity = {"edam", "Europe/Amsterdam", "knmi_seamless"};
    wind_forecast_t loaded;
    ASSERT_EQ(wind_cache_load(path.c_str(), &identity, &loaded), ESP_OK);
    EXPECT_EQ(loaded.retrieved_at, first.retrieved_at);

    auto third = cache_forecast(1787551200);
    ASSERT_EQ(wind_cache_store(path.c_str(), &third), ESP_OK);
    ASSERT_EQ(wind_cache_load(path.c_str(), &identity, &loaded), ESP_OK);
    EXPECT_EQ(loaded.retrieved_at, third.retrieved_at);
}

TEST_F(WindCacheTest, RejectsIdentityMismatch)
{
    auto path = (root / "forecast.cache").string();
    auto forecast = cache_forecast();
    ASSERT_EQ(wind_cache_store(path.c_str(), &forecast), ESP_OK);
    wind_cache_identity_t other = {"brouwersdam", "Europe/Amsterdam", "knmi_seamless"};
    wind_forecast_t loaded;
    EXPECT_EQ(wind_cache_load(path.c_str(), &other, &loaded), ESP_ERR_INVALID_STATE);
}

TEST_F(WindCacheTest, KeepsPanelConfirmationSeparateAndInvalidatable)
{
    auto path = (root / "panel.cache").string();
    const uint8_t bitmap[] = {0, 1, 1, 0};
    uint64_t hash = wind_cache_bitmap_hash(bitmap, sizeof(bitmap));
    ASSERT_EQ(wind_cache_panel_confirm(path.c_str(), 1, hash), ESP_OK);
    uint64_t loaded = 0;
    ASSERT_EQ(wind_cache_panel_load(path.c_str(), 1, &loaded), ESP_OK);
    EXPECT_EQ(loaded, hash);
    EXPECT_EQ(wind_cache_panel_load(path.c_str(), 2, &loaded), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(wind_cache_panel_invalidate(path.c_str()), ESP_OK);
    EXPECT_EQ(wind_cache_panel_load(path.c_str(), 1, &loaded), ESP_ERR_NOT_FOUND);
}

TEST_F(WindCacheTest, RetainsHourlyDataAcrossReload) {
    auto forecast = cache_forecast();
    forecast.hourly[1][1] = forecast.days[1].samples[0];
    forecast.hourly[1][1].local_hour = 9;
    forecast.hourly[1][1].timestamp += 3600;
    forecast.hourly[1][1].wind_knots = 27;
    const auto path = (root / "hourly.cache").string();
    ASSERT_EQ(wind_cache_store(path.c_str(), &forecast), ESP_OK);
    wind_cache_identity_t identity = {"edam", "Europe/Amsterdam", "knmi_seamless"};
    wind_forecast_t loaded{};
    ASSERT_EQ(wind_cache_load(path.c_str(), &identity, &loaded), ESP_OK);
    EXPECT_EQ(loaded.hourly[1][1].wind_knots, 27);
    EXPECT_EQ(loaded.hourly[1][1].timestamp, forecast.hourly[1][1].timestamp);
    EXPECT_EQ(loaded.hourly[1][2].timestamp, 0);
}

TEST_F(WindCacheTest, MigratesPreviousCacheWithoutDiscardingOfflineForecast) {
    struct PreviousForecast {
        uint32_t schema_version;
        char spot_id[32], spot_name[64];
        double latitude, longitude;
        char timezone[40], provider[24], model[32];
        int64_t retrieved_at;
        wind_forecast_day_t days[WIND_FORECAST_DAY_COUNT];
        wind_forecast_sample_t hourly[WIND_FORECAST_DAY_COUNT][WIND_FORECAST_HOURLY_COUNT];
    };
    struct OldRecord {
        uint32_t magic, schema, render, payload;
        uint64_t generation;
        uint32_t checksum;
        PreviousForecast forecast;
    };
    for (bool hourly : {false, true}) {
        SCOPED_TRACE(hourly ? "schema 5 with hourly data" : "schema 4 without hourly data");
        OldRecord old{};
        const auto forecast = cache_forecast();
        old.forecast.schema_version = hourly ? 4 : 3;
        strcpy(old.forecast.spot_id, forecast.spot_id);
        strcpy(old.forecast.spot_name, forecast.spot_name);
        old.forecast.latitude = forecast.latitude;
        old.forecast.longitude = forecast.longitude;
        strcpy(old.forecast.timezone, forecast.timezone);
        strcpy(old.forecast.provider, forecast.provider);
        strcpy(old.forecast.model, forecast.model);
        old.forecast.retrieved_at = forecast.retrieved_at;
        memcpy(old.forecast.days, forecast.days, sizeof(forecast.days));
        if (hourly) old.forecast.hourly[0][0] = forecast.days[0].samples[0];
        old.magic=0x574E4446u; old.schema=hourly ? 5 : 4; old.render=WIND_RENDER_COMPAT_VERSION;
        old.payload=hourly ? sizeof(old.forecast) : offsetof(PreviousForecast, hourly);
        old.generation=3;
        const size_t old_size = offsetof(OldRecord, forecast) + old.payload;
        uint32_t crc=0xffffffffu;
        const auto *bytes = reinterpret_cast<const uint8_t *>(&old);
        for (size_t i=0;i<old_size;++i) {
            crc ^= bytes[i];
            for (int bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u & (uint32_t)-(int32_t)(crc&1));
        }
        old.checksum=~crc;
        const auto path=(root/(hourly ? "old-hourly.cache" : "old.cache")).string();
        std::ofstream file(path+".a",std::ios::binary);
        file.write(reinterpret_cast<const char *>(&old),old_size); file.close();
        wind_cache_identity_t identity={"edam","Europe/Amsterdam","knmi_seamless"};
        wind_forecast_t loaded{};
        ASSERT_EQ(wind_cache_load(path.c_str(),&identity,&loaded),ESP_OK);
        EXPECT_EQ(loaded.schema_version,WIND_FORECAST_SCHEMA_VERSION);
        EXPECT_EQ(loaded.days[0].samples[0].wind_knots,12);
        EXPECT_EQ(loaded.hourly[0][0].timestamp, hourly ? forecast.days[0].samples[0].timestamp : 0);
        EXPECT_EQ(loaded.hourly[0][1].timestamp,0);
        ASSERT_EQ(wind_cache_store(path.c_str(),&loaded),ESP_OK);
        loaded = {};
        ASSERT_EQ(wind_cache_load(path.c_str(),&identity,&loaded),ESP_OK);
        EXPECT_EQ(loaded.days[0].samples[0].wind_knots,12);
        EXPECT_EQ(loaded.hourly[0][1].timestamp,0);
    }
}
