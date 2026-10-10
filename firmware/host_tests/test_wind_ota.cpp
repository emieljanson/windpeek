#include <gtest/gtest.h>
#include <cstring>
#include <string>
extern "C" {
#include "wind_ota.h"
#include "cJSON.h"
}
static const char *hash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static const char *board = "seeedstudio_reterminal_e1002";
static std::string pointer_json(const char *path) {
    return std::string("{\"version\":\"dev-new\",\"manifest\":\"") + path + "\",\"sha256\":\"" + hash + "\"}";
}
static std::string manifest_json() {
    return std::string("{\"version\":\"dev-new\",\"schemaVersion\":1,\"firmwareLayoutVersion\":1,") +
        "\"flashSize\":33554432,\"otaProtocol\":1,\"otaSequence\":42,\"chipFamily\":\"ESP32-S3\","
        "\"configuration\":{\"minimum\":5,\"maximum\":6},\"boardId\":\"" + board +
        "\",\"otaApplication\":\"windpeek.bin\",\"parts\":[{\"kind\":\"application\","
        "\"file\":\"windpeek.bin\",\"size\":1000000,\"sha256\":\"" + hash + "\"}]}";
}
TEST(Ota, RejectsRedirectsTraversalAndMalformedPointers) {
    wind_ota_pointer_t pointer{};
    for (const char *path : {"https://evil.test/a", "//evil/a", "/firmware/a", "../a", "v1/../a", "a%2fb", "a?b", "a\\\\b"})
        EXPECT_NE(wind_ota_parse_pointer(pointer_json(path).c_str(), &pointer), ESP_OK) << path;
    EXPECT_EQ(wind_ota_parse_pointer(pointer_json("dev-new/installer-manifest.json").c_str(), &pointer), ESP_OK);
    EXPECT_NE(wind_ota_parse_pointer((pointer_json("a") + "garbage").c_str(), &pointer), ESP_OK);
}
TEST(Ota, AcceptsOnlyCompatibleBoardVersionLayoutAndBoundedApplication) {
    wind_ota_pointer_t pointer{};
    ASSERT_EQ(wind_ota_parse_pointer(pointer_json("dev-new/installer-manifest.json").c_str(), &pointer), ESP_OK);
    wind_ota_release_t release{};
    auto json = manifest_json();
    EXPECT_EQ(wind_ota_parse_manifest(json.c_str(), board, &pointer, 0x380000, &release), ESP_OK);
    EXPECT_EQ(release.sequence, 42u);
    EXPECT_EQ(release.size, 1000000u);
    EXPECT_NE(wind_ota_parse_manifest(json.c_str(), "seeedstudio_reterminal_e1003", &pointer, 0x380000, &release), ESP_OK);
    EXPECT_NE(wind_ota_parse_manifest(json.c_str(), board, &pointer, 1000, &release), ESP_OK);
    std::strcpy(pointer.version, "other");
    EXPECT_NE(wind_ota_parse_manifest(json.c_str(), board, &pointer, 0x380000, &release), ESP_OK);
}
TEST(Ota, RejectsLegacyUnsequencedAndIncompatibleReleases) {
    wind_ota_pointer_t pointer{};
    ASSERT_EQ(wind_ota_parse_pointer(pointer_json("manifest.json").c_str(), &pointer), ESP_OK);
    wind_ota_release_t release{};
    for (const char *key : {"otaProtocol", "otaSequence", "schemaVersion", "firmwareLayoutVersion"}) {
        cJSON *root = cJSON_Parse(manifest_json().c_str());
        cJSON_ReplaceItemInObject(root, key, cJSON_CreateNumber(0));
        char *json = cJSON_PrintUnformatted(root);
        EXPECT_NE(wind_ota_parse_manifest(json, board, &pointer, 0x380000, &release), ESP_OK) << key;
        cJSON_free(json); cJSON_Delete(root);
    }
    cJSON *root = cJSON_Parse(manifest_json().c_str());
    auto *parts = cJSON_GetObjectItem(root, "parts");
    cJSON_AddItemToArray(parts, cJSON_Duplicate(cJSON_GetArrayItem(parts, 0), true));
    char *json = cJSON_PrintUnformatted(root);
    EXPECT_NE(wind_ota_parse_manifest(json, board, &pointer, 0x380000, &release), ESP_OK);
    cJSON_free(json); cJSON_Delete(root);
}
TEST(Ota, ChecksDailyOnExistingWorkOnlyWithEnoughPower) {
    const int64_t now = 1787544000;
    EXPECT_TRUE(wind_ota_should_check(now, 0, true, false, false, 3900));
    EXPECT_FALSE(wind_ota_should_check(now, now-300, true, false, true, 3900));
    EXPECT_TRUE(wind_ota_should_check(now, now-86400, true, false, true, 0));
    EXPECT_FALSE(wind_ota_should_check(now, 0, false, false, true, 4000));
    EXPECT_FALSE(wind_ota_should_check(now, 0, true, true, true, 4000));
    EXPECT_FALSE(wind_ota_should_check(now, 0, true, false, false, 3700));
    EXPECT_FALSE(wind_ota_should_check(0, 0, true, false, true, 4000));
}

TEST(Ota, RejectsAnObjectMasqueradingAsThePartsArray) {
    wind_ota_pointer_t pointer{};
    ASSERT_EQ(wind_ota_parse_pointer(pointer_json("manifest.json").c_str(), &pointer), ESP_OK);
    cJSON *root = cJSON_Parse(manifest_json().c_str());
    cJSON *parts = cJSON_GetObjectItem(root, "parts");
    cJSON *object = cJSON_CreateObject();
    cJSON_AddItemToObject(object, "app", cJSON_Duplicate(cJSON_GetArrayItem(parts, 0), true));
    cJSON_ReplaceItemInObject(root, "parts", object);
    char *json = cJSON_PrintUnformatted(root);
    wind_ota_release_t release{};
    EXPECT_NE(wind_ota_parse_manifest(json, board, &pointer, 0x380000, &release), ESP_OK);
    cJSON_free(json);
    cJSON_Delete(root);
}
