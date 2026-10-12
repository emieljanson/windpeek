#include <gtest/gtest.h>
#include <cstring>
#include <string>
extern "C" {
#include "wind_device_errors.h"
#include "cJSON.h"
}

struct Reports {
    wind_device_error_state_t saved[WIND_DEVICE_ERROR_COUNT]{};
    bool exists[WIND_DEVICE_ERROR_COUNT]{};
    int sends = 0;
    int stores = 0;
    bool fail_store = false;
    esp_err_t send_result = ESP_OK;
    std::string sent_id;
};
static esp_err_t load(void *context, wind_device_error_stage_t stage, wind_device_error_state_t *state) {
    auto &reports = *static_cast<Reports *>(context);
    *state = reports.saved[stage];
    return reports.exists[stage] ? ESP_OK : ESP_ERR_NOT_FOUND;
}
static esp_err_t store(void *context, wind_device_error_stage_t stage, const wind_device_error_state_t *state) {
    auto &reports = *static_cast<Reports *>(context);
    if (reports.fail_store) return ESP_FAIL;
    ++reports.stores;
    reports.saved[stage] = *state;
    reports.exists[stage] = true;
    return ESP_OK;
}
static uint32_t random_word(void *) { return 0x12345678; }
static esp_err_t send(void *context, wind_device_error_stage_t stage, const wind_device_error_state_t *state) {
    auto &reports = *static_cast<Reports *>(context);
    EXPECT_TRUE(reports.saved[stage].pending);
    EXPECT_GT(reports.saved[stage].last_attempt, 0);
    ++reports.sends;
    reports.sent_id = state->event_id;
    return reports.send_result;
}
class DeviceErrors : public testing::Test {
protected:
    Reports reports;
    wind_device_error_dependencies_t deps{&reports, load, store, random_word, send};
    const int64_t now = 1787544000;
    wind_device_error_t error{ESP_ERR_TIMEOUT, 5, 40000, 0, 3720, false, 8, "dev-test", "seeedstudio_reterminal_e1002"};
};
TEST_F(DeviceErrors, OfflineFailureSurvivesRestartAndSendsWithoutAnotherError) {
    ASSERT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, now, false), ESP_OK);
    EXPECT_EQ(reports.sends, 0);
    EXPECT_TRUE(reports.saved[WIND_DEVICE_ERROR_DISPLAY].pending);
    // Dependencies reload persisted state on every call; no process RAM is needed.
    ASSERT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, nullptr, now+300, true), ESP_OK);
    EXPECT_EQ(reports.sends, 1);
    EXPECT_FALSE(reports.saved[WIND_DEVICE_ERROR_DISPLAY].pending);
    EXPECT_EQ(reports.saved[WIND_DEVICE_ERROR_DISPLAY].occurred_at, now);
}
TEST_F(DeviceErrors, FailedDeliveryKeepsOriginalIdAndIsRateLimitedAcrossRestarts) {
    reports.send_result = ESP_FAIL;
    EXPECT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, now, true), ESP_FAIL);
    auto first_id = reports.sent_id;
    EXPECT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, now+300, true), ESP_OK);
    EXPECT_EQ(reports.sends, 1);
    reports.send_result = ESP_OK;
    EXPECT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, nullptr, now+3600, true), ESP_OK);
    EXPECT_EQ(reports.sends, 2);
    EXPECT_EQ(first_id, reports.sent_id);
}
TEST_F(DeviceErrors, RepeatedErrorsAreBoundedButDifferentStagesStillReport) {
    ASSERT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, now, true), ESP_OK);
    EXPECT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, now+300, true), ESP_OK);
    EXPECT_EQ(reports.sends, 1);
    EXPECT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_FORECAST, &error, now+300, true), ESP_OK);
    EXPECT_EQ(reports.sends, 2);
    EXPECT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, now+3600, true), ESP_OK);
    EXPECT_EQ(reports.sends, 3);
}
TEST_F(DeviceErrors, FailedPersistenceNeverSendsAnUnboundedReport) {
    reports.fail_store = true;
    EXPECT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, now, true), ESP_FAIL);
    EXPECT_EQ(reports.sends, 0);
}
TEST_F(DeviceErrors, SuccessIsIgnoredButEarlyBootFailureWaitsForValidClock) {
    error.error = ESP_OK;
    EXPECT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, now, true), ESP_OK);
    error.error = ESP_FAIL;
    EXPECT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, 0, true), ESP_OK);
    EXPECT_EQ(reports.sends, 0);
    EXPECT_TRUE(reports.saved[WIND_DEVICE_ERROR_DISPLAY].pending);
    EXPECT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, nullptr, now, true), ESP_OK);
    EXPECT_EQ(reports.sends, 1);
}
TEST_F(DeviceErrors, EnvelopeIsValidJsonWithOnlyApprovedDiagnostics) {
    ASSERT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, now, false), ESP_OK);
    char payload[WIND_DEVICE_ERROR_ENVELOPE_SIZE];
    ASSERT_EQ(wind_device_error_envelope(WIND_DEVICE_ERROR_DISPLAY, &reports.saved[2], payload, sizeof(payload)), ESP_OK);
    std::string envelope(payload);
    auto first = envelope.find('\n');
    auto second = envelope.find('\n', first+1);
    cJSON *header = cJSON_Parse(envelope.substr(0, first).c_str());
    cJSON *item = cJSON_Parse(envelope.substr(first+1, second-first-1).c_str());
    cJSON *event = cJSON_Parse(envelope.substr(second+1).c_str());
    ASSERT_NE(header, nullptr); ASSERT_NE(item, nullptr); ASSERT_NE(event, nullptr);
    EXPECT_STREQ(cJSON_GetObjectItem(header, "event_id")->valuestring, cJSON_GetObjectItem(event, "event_id")->valuestring);
    EXPECT_STREQ(cJSON_GetObjectItem(item, "type")->valuestring, "event");
    auto *extra = cJSON_GetObjectItem(event, "extra");
    EXPECT_EQ(cJSON_GetArraySize(extra), 6);
    EXPECT_EQ(cJSON_GetObjectItem(extra, "panel_wait_ms")->valueint, 40000);
    EXPECT_EQ(cJSON_GetObjectItem(extra, "battery_mv")->valueint, 3720);
    EXPECT_EQ(cJSON_GetObjectItem(event, "user"), nullptr);
    EXPECT_EQ(cJSON_GetObjectItem(event, "request"), nullptr);
    EXPECT_EQ(cJSON_GetObjectItem(event, "breadcrumbs"), nullptr);
    cJSON_Delete(header); cJSON_Delete(item); cJSON_Delete(event);
    EXPECT_EQ(wind_device_error_envelope(WIND_DEVICE_ERROR_DISPLAY, &reports.saved[2], payload, 5), ESP_ERR_INVALID_SIZE);
    std::strcpy(reports.saved[2].error.firmware, "bad\"release");
    EXPECT_EQ(wind_device_error_envelope(WIND_DEVICE_ERROR_DISPLAY, &reports.saved[2], payload, sizeof(payload)), ESP_ERR_INVALID_ARG);
}

TEST_F(DeviceErrors, InvalidPersistedVersionDoesNotDisableFutureReports) {
    reports.exists[WIND_DEVICE_ERROR_DISPLAY] = true;
    reports.saved[WIND_DEVICE_ERROR_DISPLAY].version = 99;
    ASSERT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, now, true), ESP_OK);
    EXPECT_EQ(reports.sends, 1);
    EXPECT_FALSE(reports.saved[WIND_DEVICE_ERROR_DISPLAY].pending);
}
TEST_F(DeviceErrors, InvalidPersistedTimestampDoesNotDisableFutureReports) {
    ASSERT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, now, false), ESP_OK);
    reports.saved[WIND_DEVICE_ERROR_DISPLAY].last_attempt = -1;
    ASSERT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, now, true), ESP_OK);
    EXPECT_EQ(reports.sends, 1);
}

TEST_F(DeviceErrors, FirstOfflineCapturePersistsThePendingErrorInOneCommit) {
    ASSERT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_DISPLAY, &error, now, false), ESP_OK);
    EXPECT_EQ(reports.stores, 1);
    EXPECT_TRUE(reports.saved[WIND_DEVICE_ERROR_DISPLAY].pending);
}

TEST_F(DeviceErrors, ForecastEnvelopeIsValidJsonAndFitsTheDeliveryBuffer) {
    error.has_forecast_diagnostics=true;
    error.forecast={ESP_ERR_TIMEOUT, 503, ESP_ERR_NOT_FINISHED, 16384, true, false};
    std::memset(error.firmware, 'a', sizeof(error.firmware)-1);
    std::memset(error.board, 'b', sizeof(error.board)-1);
    ASSERT_EQ(wind_device_error_run(&deps, WIND_DEVICE_ERROR_FORECAST, &error, now, false), ESP_OK);
    char payload[WIND_DEVICE_ERROR_ENVELOPE_SIZE];
    ASSERT_EQ(wind_device_error_envelope(WIND_DEVICE_ERROR_FORECAST,
        &reports.saved[WIND_DEVICE_ERROR_FORECAST], payload, sizeof(payload)), ESP_OK);
    std::string envelope(payload);
    auto second=envelope.find('\n',envelope.find('\n')+1);
    cJSON *event=cJSON_Parse(envelope.substr(second+1).c_str());
    ASSERT_NE(event,nullptr);
    auto *extra=cJSON_GetObjectItem(event,"extra");
    ASSERT_NE(extra,nullptr);
    EXPECT_EQ(cJSON_GetArraySize(extra),12);
    auto *status=cJSON_GetObjectItem(extra,"forecast_http_status");
    ASSERT_TRUE(cJSON_IsNumber(status));
    EXPECT_EQ(status->valueint,503);
    auto *parse=cJSON_GetObjectItem(extra,"forecast_parse_error");
    ASSERT_TRUE(cJSON_IsNumber(parse));
    EXPECT_EQ(parse->valueint,ESP_ERR_NOT_FINISHED);
    EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(extra,"forecast_response_too_large")));
    cJSON_Delete(event);
}
