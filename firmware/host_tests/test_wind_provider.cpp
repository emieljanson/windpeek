#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <cstring>

#include "cJSON.h"

extern "C" {
#include "open_meteo_knmi_provider.h"
#include "wind_timezone.h"
}

static open_meteo_knmi_config_t config()
{
    return {"edam", "Edam", 52.5126, 5.0486, "Europe/Amsterdam", "knmi_harmonie"};
}

static std::string fixture()
{
    std::ifstream file(WIND_PROVIDER_FIXTURE);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

static std::string regional_fixture_with_three_days()
{
    cJSON *root = cJSON_Parse(fixture().c_str());
    cJSON *units = cJSON_GetObjectItemCaseSensitive(root, "hourly_units");
    cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    const char *fields[] = {"wind_speed_10m", "wind_gusts_10m", "wind_direction_10m",
                            "cloud_cover", "precipitation", "is_day", "temperature_2m"};
    for (const char *field : fields) {
        cJSON *source_array = cJSON_GetObjectItemCaseSensitive(hourly, field);
        cJSON *source_unit = cJSON_GetObjectItemCaseSensitive(units, field);
        if (!source_array || !source_unit) {
            cJSON_Delete(root);
            return {};
        }
        cJSON *regional_array = cJSON_Duplicate(source_array, true);
        for (int index = 15; index < cJSON_GetArraySize(regional_array); ++index) {
            cJSON_ReplaceItemInArray(regional_array, index, cJSON_CreateNull());
        }
        std::string regional_name = std::string(field) + "_knmi_harmonie_arome_netherlands";
        std::string fallback_name = std::string(field) + "_best_match";
        cJSON_AddItemToObject(hourly, regional_name.c_str(), regional_array);
        cJSON_AddItemToObject(hourly, fallback_name.c_str(), cJSON_Duplicate(source_array, true));
        cJSON_AddItemToObject(units, regional_name.c_str(), cJSON_Duplicate(source_unit, true));
        cJSON_AddItemToObject(units, fallback_name.c_str(), cJSON_Duplicate(source_unit, true));
        cJSON_DeleteItemFromObject(hourly, field);
        cJSON_DeleteItemFromObject(units, field);
    }
    char *printed = cJSON_PrintUnformatted(root);
    std::string result = printed;
    cJSON_free(printed);
    cJSON_Delete(root);
    return result;
}

TEST(WindProvider, ParsesCompleteLocalFiveDayForecast)
{
    auto config = ::config();
    auto json = fixture();
    wind_forecast_t forecast;
    ASSERT_EQ(open_meteo_knmi_parse_json(&config, json.data(), json.size(), 1787544000,
                                         "2026-08-24", &forecast),
              ESP_OK);
    EXPECT_STREQ(forecast.model, "knmi_harmonie");
    EXPECT_STREQ(forecast.days[4].local_date, "2026-08-28");
    EXPECT_EQ(forecast.days[0].samples[0].wind_knots, 10);
    EXPECT_EQ(forecast.days[0].samples[1].wind_knots, 12);
    EXPECT_EQ(forecast.days[0].samples[3].destination_degrees, 90);
    EXPECT_EQ(forecast.days[4].samples[4].wind_knots, 44);
    EXPECT_TRUE(forecast.days[0].samples[0].weather_available);
    EXPECT_EQ(forecast.days[0].samples[1].precipitation_hundredths_mm, 9);
    EXPECT_EQ(wind_forecast_weather_state(&forecast.days[0].samples[2]),
              WIND_WEATHER_LIGHT_RAIN);
    EXPECT_EQ(wind_forecast_weather_state(&forecast.days[0].samples[4]), WIND_WEATHER_RAIN);
    EXPECT_TRUE(forecast.days[0].samples[0].temperature_available);
    EXPECT_EQ(forecast.days[0].samples[0].temperature_tenths_c, -24);
}

TEST(WindProvider, KeepsTemperatureIndependentWhenOneValueIsNull)
{
    auto config = ::config();
    auto json = fixture();
    const auto value = json.find("-2.35");
    ASSERT_NE(value, std::string::npos);
    json.replace(value, 5, "null ");
    wind_forecast_t forecast;
    ASSERT_EQ(open_meteo_knmi_parse_json(&config, json.data(), json.size(), 1787544000,
                                         "2026-08-24", &forecast), ESP_OK);
    EXPECT_FALSE(forecast.days[0].samples[0].temperature_available);
    EXPECT_TRUE(forecast.days[0].samples[1].temperature_available);
}

TEST(WindProvider, FillsTheFiveDaysWithBestMatchAfterARegionalModelEnds)
{
    auto config = ::config();
    auto json = regional_fixture_with_three_days();
    wind_forecast_t forecast;
    ASSERT_EQ(open_meteo_knmi_parse_json(&config, json.data(), json.size(), 1787544000,
                                         "2026-08-24", &forecast), ESP_OK);
    EXPECT_EQ(forecast.days[2].samples[4].wind_knots, 24);
    EXPECT_EQ(forecast.days[3].samples[0].wind_knots, 25);
    EXPECT_EQ(forecast.days[4].samples[4].wind_knots, 44);
    EXPECT_STREQ(forecast.model, "knmi_harmonie");
}

TEST(WindProvider, RejectsTimezoneUnitsAndArrayLengthMismatch)
{
    auto config = ::config();
    auto json = fixture();
    wind_forecast_t forecast;
    auto timezone = json;
    timezone.replace(timezone.find("Europe/Amsterdam"), 16, "UTC             ");
    EXPECT_NE(open_meteo_knmi_parse_json(&config, timezone.data(), timezone.size(), 1787544000,
                                         "2026-08-24", &forecast),
              ESP_OK);
    auto bad_unit = json;
    bad_unit.replace(bad_unit.find("\"kn\""), 4, "\"km\"");
    EXPECT_NE(open_meteo_knmi_parse_json(&config, bad_unit.data(), bad_unit.size(), 1787544000,
                                         "2026-08-24", &forecast),
              ESP_OK);
    auto short_array = json;
    short_array.erase(short_array.find(", 49"), 4);
    EXPECT_NE(open_meteo_knmi_parse_json(&config, short_array.data(), short_array.size(), 1787544000,
                                         "2026-08-24", &forecast),
              ESP_OK);
}

TEST(WindProvider, RejectsPartialAndOversizedResponsesWithoutOutputMutation)
{
    auto config = ::config();
    wind_forecast_t forecast;
    std::memset(&forecast, 0x5a, sizeof(forecast));
    auto before = forecast;
    const char *partial = "{\"timezone\":\"Europe/Amsterdam\"}";
    EXPECT_NE(open_meteo_knmi_parse_json(&config, partial, std::strlen(partial), 1787544000,
                                         "2026-08-24", &forecast),
              ESP_OK);
    EXPECT_EQ(std::memcmp(&forecast, &before, sizeof(forecast)), 0);
    std::string oversized(OPEN_METEO_RESPONSE_LIMIT + 1, 'x');
    EXPECT_EQ(open_meteo_knmi_parse_json(&config, oversized.data(), oversized.size(), 1787544000,
                                         "2026-08-24", &forecast),
              ESP_ERR_INVALID_ARG);
}

TEST(WindProvider, UsesTheWindpeekOpenMeteoService)
{
    auto config = ::config();
    EXPECT_TRUE(open_meteo_knmi_config_valid(&config));
    EXPECT_STREQ(open_meteo_knmi_endpoint(), OPEN_METEO_ENDPOINT);
}

TEST(WindProvider, AcceptsEveryConfiguratorModelAndSpotTimezone)
{
    auto config = ::config();
    const char *models[] = {
        "best_match", "ecmwf_ifs", "icon_seamless", "ncep_gfs_seamless",
        "knmi_harmonie", "dmi_harmonie", "met_nordic", "meteofrance_arome", "ukmo_ukv",
        "meteoswiss_icon", "geosphere_arome", "italiameteo_icon", "chmi_aladin", "noaa_hrrr",
        "canada_hrdps", "jma_msm", "kma_ldps",
    };
    config.timezone = "America/New_York";
    for (const char *model : models) {
        config.model = model;
        EXPECT_TRUE(open_meteo_knmi_config_valid(&config)) << model;
    }
    config.model = "unknown_model";
    EXPECT_FALSE(open_meteo_knmi_config_valid(&config));
}
