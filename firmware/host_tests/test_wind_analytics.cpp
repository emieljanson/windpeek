#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "cJSON.h"
#include "wind_analytics.h"
}

namespace {

constexpr time_t kNow = 1788436800;

struct FakeAnalytics {
    esp_err_t load_result = ESP_ERR_NOT_FOUND;
    esp_err_t store_result = ESP_OK;
    esp_err_t send_result = ESP_OK;
    wind_analytics_state_t stored{};
    std::vector<wind_analytics_state_t> writes;
    int sends = 0;
    std::string sent_id;
    uint32_t random_value = 0x01234567;
};

esp_err_t load_state(void *context, wind_analytics_state_t *state)
{
    auto *fake = static_cast<FakeAnalytics *>(context);
    if (fake->load_result == ESP_OK) *state = fake->stored;
    return fake->load_result;
}

esp_err_t store_state(void *context, const wind_analytics_state_t *state)
{
    auto *fake = static_cast<FakeAnalytics *>(context);
    if (fake->store_result != ESP_OK) return fake->store_result;
    fake->stored = *state;
    fake->load_result = ESP_OK;
    fake->writes.push_back(*state);
    return ESP_OK;
}

uint32_t random_word(void *context)
{
    auto *fake = static_cast<FakeAnalytics *>(context);
    return fake->random_value++;
}

esp_err_t send_heartbeat(void *context, const char *dashboard_id)
{
    auto *fake = static_cast<FakeAnalytics *>(context);
    ++fake->sends;
    fake->sent_id = dashboard_id;
    return fake->send_result;
}

wind_analytics_dependencies_t dependencies(FakeAnalytics *fake)
{
    return {.context = fake,
            .load = load_state,
            .store = store_state,
            .random_u32 = random_word,
            .send = send_heartbeat};
}

wind_analytics_state_t existing_state(int64_t success, int64_t attempt = 0)
{
    wind_analytics_state_t state{};
    state.version = 1;
    std::strcpy(state.dashboard_id, "00112233445566778899aabbccddeeff");
    state.last_success_unix = success;
    state.last_attempt_unix = attempt;
    return state;
}

TEST(WindAnalyticsTest, CreatesAndPersistsRandomIdBeforeFirstHeartbeat)
{
    FakeAnalytics fake;
    const auto deps = dependencies(&fake);

    ASSERT_EQ(wind_analytics_run(&deps, kNow), ESP_OK);

    ASSERT_EQ(fake.sends, 1);
    EXPECT_EQ(fake.sent_id, "0123456701234568012345690123456a");
    ASSERT_EQ(fake.writes.size(), 3u);
    EXPECT_EQ(fake.writes[0].last_attempt_unix, 0);
    EXPECT_EQ(fake.writes[1].last_attempt_unix, kNow);
    EXPECT_EQ(fake.writes[2].last_success_unix, kNow);
    EXPECT_TRUE(wind_analytics_dashboard_id_valid(fake.stored.dashboard_id));
}

TEST(WindAnalyticsTest, SuppressesHeartbeatUntilSevenDaysAfterSuccess)
{
    FakeAnalytics fake;
    fake.load_result = ESP_OK;
    fake.stored = existing_state(kNow);
    const auto deps = dependencies(&fake);

    EXPECT_EQ(wind_analytics_run(&deps, kNow + WIND_ANALYTICS_HEARTBEAT_SECONDS - 1), ESP_OK);
    EXPECT_EQ(fake.sends, 0);
    EXPECT_EQ(wind_analytics_run(&deps, kNow + WIND_ANALYTICS_HEARTBEAT_SECONDS), ESP_OK);
    EXPECT_EQ(fake.sends, 1);
}

TEST(WindAnalyticsTest, FailedSendRetriesOnlyAfterOneDay)
{
    FakeAnalytics fake;
    fake.load_result = ESP_OK;
    fake.stored = existing_state(0, kNow);
    fake.send_result = ESP_FAIL;
    const auto deps = dependencies(&fake);

    EXPECT_EQ(wind_analytics_run(&deps, kNow + WIND_ANALYTICS_RETRY_SECONDS - 1), ESP_OK);
    EXPECT_EQ(fake.sends, 0);
    EXPECT_EQ(wind_analytics_run(&deps, kNow + WIND_ANALYTICS_RETRY_SECONDS), ESP_FAIL);
    EXPECT_EQ(fake.sends, 1);
    EXPECT_EQ(fake.stored.last_success_unix, 0);
}

TEST(WindAnalyticsTest, InvalidOrBackwardTimeFailsClosed)
{
    FakeAnalytics invalid;
    auto invalid_deps = dependencies(&invalid);
    EXPECT_EQ(wind_analytics_run(&invalid_deps, 1), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(invalid.sends, 0);

    FakeAnalytics backward;
    backward.load_result = ESP_OK;
    backward.stored = existing_state(kNow + 60);
    auto backward_deps = dependencies(&backward);
    EXPECT_EQ(wind_analytics_run(&backward_deps, kNow), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(backward.sends, 0);
}

TEST(WindAnalyticsTest, StateWriteFailurePreventsNetworkRequest)
{
    FakeAnalytics fake;
    fake.store_result = ESP_FAIL;
    const auto deps = dependencies(&fake);

    EXPECT_EQ(wind_analytics_run(&deps, kNow), ESP_FAIL);
    EXPECT_EQ(fake.sends, 0);
}

TEST(WindAnalyticsTest, RejectsMalformedDashboardIds)
{
    EXPECT_FALSE(wind_analytics_dashboard_id_valid(nullptr));
    EXPECT_FALSE(wind_analytics_dashboard_id_valid(""));
    EXPECT_FALSE(wind_analytics_dashboard_id_valid("00112233445566778899AABBCCDDEEFF"));
    EXPECT_FALSE(wind_analytics_dashboard_id_valid("00112233445566778899aabbccddeefg"));
    EXPECT_TRUE(wind_analytics_dashboard_id_valid("00112233445566778899aabbccddeeff"));
}

TEST(WindAnalyticsTest, BuildsAnExactPersonlessPostHogPayload)
{
    char payload[512] = {0};
    ASSERT_EQ(wind_analytics_build_payload(
                  "phc_test", "00112233445566778899aabbccddeeff", "v1.2.3",
                  "seeedstudio_reterminal_e1003", payload, sizeof(payload)),
              ESP_OK);

    cJSON *root = cJSON_Parse(payload);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(cJSON_GetArraySize(root), 4);
    EXPECT_STREQ(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "api_key")),
                 "phc_test");
    EXPECT_STREQ(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "event")),
                 "windscout_dashboard_heartbeat");
    EXPECT_STREQ(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "distinct_id")),
                 "00112233445566778899aabbccddeeff");

    cJSON *properties = cJSON_GetObjectItemCaseSensitive(root, "properties");
    ASSERT_TRUE(cJSON_IsObject(properties));
    EXPECT_EQ(cJSON_GetArraySize(properties), 3);
    EXPECT_TRUE(cJSON_IsFalse(
        cJSON_GetObjectItemCaseSensitive(properties, "$process_person_profile")));
    EXPECT_STREQ(cJSON_GetStringValue(
                     cJSON_GetObjectItemCaseSensitive(properties, "firmware_version")),
                 "v1.2.3");
    EXPECT_STREQ(
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(properties, "device_type")),
        "seeedstudio_reterminal_e1003");
    EXPECT_EQ(std::string(payload).find("location"), std::string::npos);
    EXPECT_EQ(std::string(payload).find("wifi"), std::string::npos);
    EXPECT_EQ(std::string(payload).find("forecast"), std::string::npos);
    cJSON_Delete(root);
}

TEST(WindAnalyticsTest, RejectsUnsafeOrOversizedPayloadValues)
{
    char payload[128] = {0};
    EXPECT_EQ(wind_analytics_build_payload(
                  "", "00112233445566778899aabbccddeeff", "v1", "e1003", payload,
                  sizeof(payload)),
              ESP_ERR_INVALID_ARG);
    EXPECT_EQ(wind_analytics_build_payload(
                  "phc_test", "00112233445566778899aabbccddeeff", "bad\"version", "e1003",
                  payload, sizeof(payload)),
              ESP_ERR_INVALID_ARG);
    EXPECT_EQ(wind_analytics_build_payload(
                  "phc_test", "00112233445566778899aabbccddeeff", "v1", "e1003", payload,
                  8),
              ESP_ERR_INVALID_SIZE);
}

TEST(WindAnalyticsTest, AcceptsOnlySuccessfulHttpStatuses)
{
    EXPECT_FALSE(wind_analytics_http_status_accepted(199));
    EXPECT_TRUE(wind_analytics_http_status_accepted(200));
    EXPECT_TRUE(wind_analytics_http_status_accepted(299));
    EXPECT_FALSE(wind_analytics_http_status_accepted(300));
}

}  // namespace
