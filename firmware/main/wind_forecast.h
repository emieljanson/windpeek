#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIND_FORECAST_SCHEMA_VERSION 4u
#define WIND_FORECAST_DAY_COUNT 5
#define WIND_FORECAST_SAMPLES_PER_DAY 5
#define WIND_FORECAST_SAMPLE_COUNT 25
#define WIND_FORECAST_HOURLY_COUNT 13
#define WIND_FORECAST_SPOT_ID_MAX 32
#define WIND_FORECAST_SPOT_NAME_MAX 64
#define WIND_FORECAST_TIMEZONE_MAX 40
#define WIND_FORECAST_PROVIDER_MAX 24
#define WIND_FORECAST_MODEL_MAX 32
#define WIND_FORECAST_DATE_LENGTH 11

typedef struct {
    int64_t timestamp;
    uint8_t local_hour;
    int16_t wind_knots;
    int16_t gust_knots;
    uint16_t destination_degrees;
    uint8_t cloud_cover_percent;
    uint16_t precipitation_hundredths_mm;
    uint8_t is_day;
    uint8_t weather_available;
    int16_t temperature_tenths_c;
    uint8_t temperature_available;
} wind_forecast_sample_t;

typedef enum {
    WIND_WEATHER_UNAVAILABLE = 0,
    WIND_WEATHER_CLEAR_DAY,
    WIND_WEATHER_CLEAR_NIGHT,
    WIND_WEATHER_PARTLY_CLOUDY_DAY,
    WIND_WEATHER_PARTLY_CLOUDY_NIGHT,
    WIND_WEATHER_CLOUDY,
    WIND_WEATHER_LIGHT_RAIN,
    WIND_WEATHER_RAIN,
    WIND_WEATHER_HEAVY_RAIN,
} wind_weather_state_t;

typedef struct {
    char local_date[WIND_FORECAST_DATE_LENGTH];
    wind_forecast_sample_t samples[WIND_FORECAST_SAMPLES_PER_DAY];
} wind_forecast_day_t;

typedef struct {
    uint32_t schema_version;
    char spot_id[WIND_FORECAST_SPOT_ID_MAX];
    char spot_name[WIND_FORECAST_SPOT_NAME_MAX];
    double latitude;
    double longitude;
    char timezone[WIND_FORECAST_TIMEZONE_MAX];
    char provider[WIND_FORECAST_PROVIDER_MAX];
    char model[WIND_FORECAST_MODEL_MAX];
    int64_t retrieved_at;
    wind_forecast_day_t days[WIND_FORECAST_DAY_COUNT];
    /* Optional 08–20 hourly observations; zero timestamp means unavailable. */
    wind_forecast_sample_t hourly[WIND_FORECAST_DAY_COUNT][WIND_FORECAST_HOURLY_COUNT];
} wind_forecast_t;

void wind_forecast_clear(wind_forecast_t *forecast);
bool wind_forecast_validate(const wind_forecast_t *forecast);
int wind_forecast_round_knots(double knots);
uint16_t wind_forecast_destination_degrees(double source_degrees);
bool wind_forecast_is_overflow(int knots);
const char *wind_forecast_model_screen_name(const char *model_id);
const char *wind_forecast_model_api_id(const char *model_id);
wind_weather_state_t wind_forecast_weather_state(const wind_forecast_sample_t *sample);
const wind_forecast_sample_t *wind_forecast_sample(const wind_forecast_t *forecast, size_t day,
                                                   size_t sample);

#ifdef __cplusplus
}
#endif
