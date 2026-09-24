#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <cstring>
extern "C" {
#include "cJSON.h"
#include "wind_installer_service.h"
#include "wind_spots.h"
}

static std::string fixture() {
    std::ifstream input(MULTI_SPOT_FIXTURE);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

static std::string stage(wind_installer_service_t *service, const std::string &configuration) {
    const std::string request = "{\"command\":\"stage_configuration\",\"configuration\":" + configuration + "}";
    char response[1024] = {};
    EXPECT_EQ(wind_installer_service_handle_json(service, request.c_str(), request.size(), response, sizeof(response)), ESP_OK);
    return response;
}

TEST(E1003Spots, TransfersTenSettingsPersistsAndNavigatesBothDirections) {
    installed_configuration_reset_host_storage();
    wind_installer_service_t service;
    wind_installer_dependencies_t dependencies = {};
    wind_installer_service_init(&service, &dependencies);
    ASSERT_NE(stage(&service, fixture()).find("configuration_staged"), std::string::npos);
    ASSERT_EQ(service.candidate.additional_spot_count, 9u);
    EXPECT_EQ(service.candidate.display.threshold_kt, 17u);
    EXPECT_EQ(service.candidate.additional_spots[8].display.threshold_kt, 26u);
    ASSERT_EQ(installed_configuration_promote_setup(&service.candidate, "test-wifi", "test-password"), ESP_OK);
    ASSERT_EQ(wind_spots_reload_installed(), ESP_OK);
    ASSERT_EQ(wind_spots_count(), 10u);
    EXPECT_EQ(wind_spots_offset(0, -1), 9u);
    EXPECT_EQ(wind_spots_offset(9, 1), 0u);
    for (size_t i = 0; i < 10; ++i) {
        EXPECT_EQ(wind_spots_offset(i, 1), (i + 1) % 10);
        EXPECT_EQ(wind_spots_offset(i, -1), (i + 9) % 10);
    }
    EXPECT_EQ(wind_spots_store_selected(10), ESP_ERR_INVALID_ARG);
    ASSERT_EQ(wind_spots_store_selected(7), ESP_OK);
    ASSERT_EQ(wind_spots_reload_installed(), ESP_OK);
    size_t selected = 0;
    ASSERT_EQ(wind_spots_load_selected(&selected), ESP_OK);
    EXPECT_EQ(selected, 7u);
    EXPECT_STREQ(wind_spots_at(selected)->id, "spot-8");
    service.candidate.display.threshold_kt++;
    ASSERT_EQ(wind_spots_use_configuration(&service.candidate), ESP_OK);
    ASSERT_EQ(wind_spots_load_selected(&selected), ESP_OK);
    EXPECT_EQ(selected, 0u);
}

TEST(E1003Spots, RejectsTamperedNestedDuplicateAndEleventhSpots) {
    for (int scenario = 0; scenario < 10; ++scenario) {
        wind_installer_service_t service;
        wind_installer_dependencies_t dependencies = {};
        wind_installer_service_init(&service, &dependencies);
        cJSON *root = cJSON_Parse(fixture().c_str());
        ASSERT_NE(root, nullptr);
        cJSON *extras = cJSON_GetObjectItem(root, "additionalSpots");
        cJSON *first = cJSON_GetArrayItem(extras, 0);
        if (scenario == 0) cJSON_SetNumberValue(cJSON_GetObjectItem(cJSON_GetObjectItem(first, "display"), "threshold"), 99);
        if (scenario == 1) cJSON_AddArrayToObject(first, "additionalSpots");
        if (scenario == 2) cJSON_ReplaceItemInArray(extras, 1, cJSON_Duplicate(first, true));
        if (scenario == 3) cJSON_AddItemToArray(extras, cJSON_Duplicate(first, true));
        if (scenario == 4) cJSON_AddBoolToObject(cJSON_GetObjectItem(first, "display"), "extra", true);
        if (scenario == 5) cJSON_AddBoolToObject(first, "extra", true);
        if (scenario == 6) cJSON_DeleteItemFromObjectCaseSensitive(first, "digest");
        if (scenario == 7) cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetObjectItem(first, "display"), "timeFormat");
        if (scenario == 8) cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetObjectItem(first, "display"), "timeFormat", cJSON_CreateString("local"));
        if (scenario == 9) cJSON_ReplaceItemInArray(cJSON_GetObjectItem(cJSON_GetObjectItem(first, "display"), "moduleOrder"), 1, cJSON_CreateString("wind"));
        char *json = cJSON_PrintUnformatted(root);
        EXPECT_EQ(stage(&service, json).find("configuration_staged"), std::string::npos);
        EXPECT_FALSE(service.candidate_staged);
        cJSON_free(json);
        cJSON_Delete(root);
    }
}

TEST(E1003Spots, MigratesV5WithoutLosingWifiOrDisplaySettings) {
    installed_configuration_t original, loaded;
    installed_configuration_default(&original);
    original.display.threshold_kt = 29;
    installed_configuration_seed_v5_host_storage(&original, "old-wifi", "old-password");
    ASSERT_EQ(installed_configuration_load(&loaded), ESP_OK);
    EXPECT_EQ(loaded.additional_spot_count, 0u);
    EXPECT_EQ(loaded.display.threshold_kt, 29u);
    EXPECT_EQ(installed_configuration_digest(&loaded), installed_configuration_digest(&original));
    char ssid[33], password[65];
    ASSERT_EQ(installed_configuration_load_credentials(ssid, sizeof(ssid), password, sizeof(password)), ESP_OK);
    EXPECT_STREQ(ssid, "old-wifi");
    EXPECT_STREQ(password, "old-password");
}

TEST(E1003Spots, SetupSurvivesFiveHoursAndRetriesWithoutCommittingAFailedPreview) {
    installed_configuration_reset_host_storage();
    struct State { bool wifi_ok = false; bool forecast_ok = false; } state;
    wind_installer_dependencies_t dependencies = {};
    dependencies.context = &state;
    dependencies.set_clock = [](void *, int64_t) { return ESP_OK; };
    dependencies.test_wifi = [](void *context, const char *, const char *) {
        return static_cast<State *>(context)->wifi_ok ? ESP_OK : ESP_FAIL;
    };
    dependencies.render_candidate = [](void *context, const installed_configuration_t *) {
        return static_cast<State *>(context)->forecast_ok ? ESP_OK : ESP_ERR_TIMEOUT;
    };
    dependencies.commit = [](void *, const installed_configuration_t *candidate,
                              const char *ssid, const char *password) {
        return installed_configuration_promote_setup(candidate, ssid, password);
    };
    wind_installer_service_t service;
    wind_installer_service_init(&service, &dependencies);
    auto request = [&](const char *json) {
        char response[2048] = {};
        EXPECT_EQ(wind_installer_service_handle_json(&service, json, strlen(json),
            response, sizeof(response)), ESP_OK);
        return std::string(response);
    };
    request(R"({"command":"hello"})");
    wind_installer_service_check_idle(&service, true, INT64_C(5) * 60 * 60 * 1000000);
    EXPECT_TRUE(service.wake_lock_held);
    request(R"({"command":"begin","unixTime":1787932800})");
    ASSERT_NE(stage(&service, fixture()).find("configuration_staged"), std::string::npos);
    const char *wifi = R"({"command":"test_wifi","ssid":"Test network","password":"test-value"})";
    EXPECT_NE(request(wifi).find("wifi_rejected"), std::string::npos);
    EXPECT_TRUE(service.credentials_cleared);
    state.wifi_ok = true;
    EXPECT_NE(request(wifi).find("wifi_ready"), std::string::npos);
    EXPECT_NE(request(R"({"command":"apply_configuration"})").find("render_failed"), std::string::npos);
    EXPECT_FALSE(installed_configuration_has_setup());
    EXPECT_TRUE(service.credentials_cleared);
    state.forecast_ok = true;
    request(R"({"command":"begin","unixTime":1787932860})");
    EXPECT_NE(request(wifi).find("wifi_ready"), std::string::npos);
    EXPECT_NE(request(R"({"command":"apply_configuration"})").find("complete"), std::string::npos);
    EXPECT_TRUE(installed_configuration_has_setup());
    installed_configuration_t installed;
    ASSERT_EQ(installed_configuration_load(&installed), ESP_OK);
    EXPECT_EQ(installed.additional_spot_count, 9u);
    EXPECT_TRUE(service.credentials_cleared);
}
