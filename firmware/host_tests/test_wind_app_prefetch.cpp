#include <gtest/gtest.h>

extern "C" {
#include "wind_app_prefetch.h"
#include "wind_swell_cache.h"
#include "wind_tide_cache.h"
}

namespace {
struct FetchState {
    bool wind_cached = true;
    bool wind_attempted = false;
    bool forced = false;
    int wind_calls = 0;
    bool marine_cached = false;
    bool marine_valid = true;
    esp_err_t marine_fetch = ESP_OK;
    esp_err_t marine_store = ESP_OK;
    int swell_calls = 0;
    int tide_calls = 0;
} state;
constexpr time_t now = 1787544000;
}

extern "C" {
const char *esp_err_to_name(esp_err_t) { return "test error"; }
esp_err_t wind_app_prefetch(wind_app_t *, bool force, int64_t, wind_app_outcome_t *out) {
    ++state.wind_calls;
    state.forced |= force;
    *out = {};
    out->used_cache = state.wind_cached;
    out->attempted_fetch = force || state.wind_attempted;
    out->published_forecast = force;
    return ESP_OK;
}
void open_meteo_knmi_provider_init(wind_provider_t *, open_meteo_knmi_config_t *) {}
esp_err_t wind_swell_cache_load(const char *, const wind_swell_cache_identity_t *, wind_swell_t *out) {
    out->retrieved_at = now;
    return state.marine_cached ? ESP_OK : ESP_ERR_NOT_FOUND;
}
esp_err_t wind_tide_cache_load(const char *, const wind_tide_cache_identity_t *, wind_tide_t *out) {
    out->retrieved_at = now;
    return state.marine_cached ? ESP_OK : ESP_ERR_NOT_FOUND;
}
esp_err_t wind_swell_fetch(const open_meteo_marine_config_t *, int64_t, wind_swell_t *) {
    ++state.swell_calls;
    return state.marine_fetch;
}
static esp_err_t fetch_tide(void *, int64_t, wind_tide_t *) {
    ++state.tide_calls;
    return state.marine_fetch;
}
void open_meteo_marine_provider_init(wind_tide_provider_t *provider, open_meteo_marine_config_t *) {
    *provider = {fetch_tide, nullptr};
}
bool wind_swell_validate(const wind_swell_t *) { return state.marine_valid; }
bool wind_tide_validate(const wind_tide_t *) { return state.marine_valid; }
esp_err_t wind_swell_cache_store(const char *, const wind_swell_t *) { return state.marine_store; }
esp_err_t wind_tide_cache_store(const char *, const wind_tide_t *) { return state.marine_store; }
}

class BackgroundForecast : public testing::Test {
protected:
    void SetUp() override { state = {}; }
    wind_app_prefetch_spot_t spot{};
};

TEST_F(BackgroundForecast, MissingCacheRecoversAfterScheduledRetriesAreExhausted) {
    state.wind_cached = false;
    EXPECT_TRUE(wind_app_prefetch_spot_fetch(&spot, now));
    EXPECT_TRUE(state.forced);
    EXPECT_EQ(state.wind_calls, 2);
}

TEST_F(BackgroundForecast, FailedAttemptIsNotImmediatelyRepeated) {
    state.wind_cached = false;
    state.wind_attempted = true;
    EXPECT_FALSE(wind_app_prefetch_spot_fetch(&spot, now));
    EXPECT_FALSE(state.forced);
    EXPECT_EQ(state.wind_calls, 1);
}

TEST_F(BackgroundForecast, MarineFailuresKeepSetupIncompleteUntilTheyRecover) {
    for (bool swell : {false, true}) {
        spot.show_swell = swell;
        spot.show_tide = !swell;
        state.marine_fetch = ESP_ERR_TIMEOUT;
        EXPECT_FALSE(wind_app_prefetch_spot_fetch(&spot, now));
        state.marine_fetch = ESP_OK;
        state.marine_store = ESP_FAIL;
        EXPECT_FALSE(wind_app_prefetch_spot_fetch(&spot, now));
        state.marine_store = ESP_OK;
        state.marine_valid = false;
        EXPECT_FALSE(wind_app_prefetch_spot_fetch(&spot, now));
        state.marine_valid = true;
        EXPECT_TRUE(wind_app_prefetch_spot_fetch(&spot, now));
    }
}

TEST_F(BackgroundForecast, FreshMarineCachesDoNotFetchAgain) {
    spot.show_swell = spot.show_tide = true;
    state.marine_cached = true;
    state.marine_fetch = ESP_ERR_TIMEOUT;
    EXPECT_TRUE(wind_app_prefetch_spot_fetch(&spot, now));
    EXPECT_EQ(state.swell_calls, 0);
    EXPECT_EQ(state.tide_calls, 0);
    EXPECT_FALSE(state.forced);
}

TEST_F(BackgroundForecast, WindOnlySpotsDoNotRequestMarineData) {
    state.marine_fetch = ESP_ERR_TIMEOUT;
    EXPECT_TRUE(wind_app_prefetch_spot_fetch(&spot, now));
    EXPECT_EQ(state.swell_calls, 0);
    EXPECT_EQ(state.tide_calls, 0);
}

TEST_F(BackgroundForecast, SwellFailureDoesNotPreventTideFetch) {
    spot.show_swell = spot.show_tide = true;
    state.marine_fetch = ESP_ERR_TIMEOUT;
    EXPECT_FALSE(wind_app_prefetch_spot_fetch(&spot, now));
    EXPECT_EQ(state.swell_calls, 1);
    EXPECT_EQ(state.tide_calls, 1);
}
