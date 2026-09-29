#include <gtest/gtest.h>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>
extern "C" {
#include "wind_support.h"
#include "cJSON.h"
}

class SupportHistory : public testing::Test {
protected:
    std::string directory;
    void SetUp() override {
        char path[] = "/tmp/wind-support-XXXXXX";
        directory = mkdtemp(path);
        installed_configuration_reset_host_storage();
        ASSERT_EQ(wind_support_init(directory.c_str(), "0123456789abcdef0123456789abcdef"), ESP_OK);
    }
    void TearDown() override { std::filesystem::remove_all(directory); }
    std::string read(unsigned sequence) {
        char response[16384];
        EXPECT_EQ(wind_support_read(sequence, response, sizeof(response)), ESP_OK);
        return response;
    }
};

TEST_F(SupportHistory, RetainsIdentitySettingsAndFailureAcrossReinitialization) {
    installed_configuration_t config;
    installed_configuration_default(&config);
    ASSERT_EQ(installed_configuration_promote_setup(&config, "private-network", "private-password"), ESP_OK);
    wind_provider_diagnostics_t forecast = {};
    forecast.http_status = 503;
    wind_support_record(WIND_SUPPORT_SETUP_FAILED, ESP_ERR_INVALID_ARG, 3, &config, &forecast);
    ASSERT_EQ(wind_support_init(directory.c_str(), "ffffffffffffffffffffffffffffffff"), ESP_OK);
    const auto meta = read(0);
    EXPECT_NE(meta.find("0123456789abcdef0123456789abcdef"), std::string::npos);
    EXPECT_NE(meta.find("\"configuration\""), std::string::npos);
    EXPECT_EQ(meta.find("private-"), std::string::npos);
    const auto event = read(1);
    EXPECT_NE(event.find("setup-failed"), std::string::npos);
    EXPECT_NE(event.find("\"result\":258"), std::string::npos);
    EXPECT_NE(event.find("\"httpStatus\":503"), std::string::npos);
    EXPECT_EQ(event.find("private-"), std::string::npos);
}

TEST_F(SupportHistory, BoundsHistoryAndIgnoresInterruptedWrites) {
    for (unsigned i = 0; i < 40; ++i) wind_support_record(WIND_SUPPORT_REFRESH_COMPLETE, ESP_OK, 4, nullptr, nullptr);
    std::ofstream(directory + "/support-08.json.tmp") << "broken";
    ASSERT_EQ(wind_support_init(directory.c_str(), "ffffffffffffffffffffffffffffffff"), ESP_OK);
    EXPECT_NE(read(0).find("\"oldestSequence\":9"), std::string::npos);
    EXPECT_NE(read(0).find("\"newestSequence\":40"), std::string::npos);
    EXPECT_NE(read(1).find("missing"), std::string::npos);
    EXPECT_NE(read(40).find("refresh-complete"), std::string::npos);
}

TEST_F(SupportHistory, RejectsOversizedOutputWithoutTruncatedJson) {
    char output[8];
    EXPECT_EQ(wind_support_read(0, output, sizeof(output)), ESP_ERR_INVALID_SIZE);
}

TEST_F(SupportHistory, DoesNotPresentFactoryDefaultsAsInstalledSettings) {
    const auto metadata = read(0);
    EXPECT_NE(metadata.find("\"configurationInstalled\":false"), std::string::npos);
    EXPECT_EQ(metadata.find("\"configuration\":"), std::string::npos);
}

TEST_F(SupportHistory, RecordsFailedFetchEvenWhenCachedRenderSucceeded) {
    wind_app_status_t status = {};
    status.stage = WIND_REFRESH_COMPLETE;
    status.result = ESP_OK;
    status.fetch_result = ESP_ERR_TIMEOUT;
    status.attempted_fetch = true;
    status.forecast.http_status = 503;
    wind_support_record_refresh(&status, WIND_REFRESH_FORECAST);
    const auto event = read(1);
    EXPECT_NE(event.find("refresh-failed"), std::string::npos);
    EXPECT_NE(event.find("\"result\":0"), std::string::npos);
    EXPECT_NE(event.find("\"fetchResult\":263"), std::string::npos);
    EXPECT_NE(event.find("\"attemptedFetch\":true"), std::string::npos);
}

TEST_F(SupportHistory, ReportsAllTenSpotsWithinOneUsbFrame) {
    installed_configuration_t config;
    installed_configuration_default(&config);
    config.version = INSTALLED_CONFIGURATION_MULTI_VERSION;
    memset(config.spot.display_name, '\\', sizeof(config.spot.display_name) - 1);
    config.additional_spot_count = 9;
    for (unsigned i = 0; i < 9; ++i) {
        installed_configuration_get_spot(&config, 0, &config.additional_spots[i]);
        config.additional_spots[i].version = INSTALLED_CONFIGURATION_VERSION;
        snprintf(config.additional_spots[i].spot.id, sizeof(config.additional_spots[i].spot.id), "spot-%u", i);
    }
    ASSERT_TRUE(installed_configuration_validate(&config));
    wind_support_record(WIND_SUPPORT_SETUP_COMPLETE, ESP_OK, 8, &config, nullptr);
    const auto event = read(1);
    cJSON *json = cJSON_Parse(event.c_str());
    ASSERT_NE(json, nullptr);
    const cJSON *settings = cJSON_GetObjectItem(cJSON_GetObjectItem(json, "entry"), "configuration");
    EXPECT_EQ(cJSON_GetArraySize(cJSON_GetObjectItem(settings, "additionalSpots")), 9);
    cJSON_Delete(json);
    EXPECT_LT(event.size(), 14000u);
}
