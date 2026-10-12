#include <gtest/gtest.h>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
extern "C" {
#include "ota_platform.h"
#include "wind_device_errors.h"
#include "epaper.h"
#include "open_meteo_knmi_provider.h"
void reports_test_reset(void);
size_t reports_test_legacy_state_size(void);
void reports_test_make_legacy(void *, const wind_device_error_state_t *);
}
namespace {
std::map<std::string, std::vector<uint8_t>> records;
int reads, sends;
wind_provider_diagnostics_t forecast;
std::string payload;
std::function<void()> during_send;
}
extern "C" {
void open_meteo_knmi_get_diagnostics(wind_provider_diagnostics_t *out) { *out=forecast; }
time_t ota_test_time(time_t *out) { const time_t now=1787544000; if(out) *out=now; return now; }
bool wifi_manager_is_connected(void) { return true; }
bool board_hal_is_usb_connected(void) { return true; }
int board_hal_get_battery_voltage(void) { return 4000; }
uint32_t esp_random(void) { return 123; }
int esp_reset_reason(void) { return 0; }
epaper_panel_diagnostics_t epaper_panel_diagnostics_get(void) { return {}; }
esp_err_t nvs_open(const char *, int, nvs_handle_t *out) { *out=1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t, const char *key, void *out, size_t *size) {
    ++reads;
    auto it=records.find(key);
    if(it==records.end()) return ESP_ERR_NVS_NOT_FOUND;
    if(*size<it->second.size()) return ESP_ERR_NVS_INVALID_LENGTH;
    *size=it->second.size(); std::memcpy(out,it->second.data(),*size); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char *key, const void *value, size_t size) {
    auto p=static_cast<const uint8_t *>(value); records[key]={p,p+size}; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
void nvs_close(nvs_handle_t) {}
int esp_crt_bundle_attach(void *) { return 0; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *) {
    return reinterpret_cast<esp_http_client_handle_t>(1);
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *) { return ESP_OK; }
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t, const char *data, int length) { payload.assign(data,length); return ESP_OK; }
esp_err_t esp_http_client_perform(esp_http_client_handle_t) {
    ++sends;
    if(during_send) { auto callback=std::move(during_send); during_send=nullptr; callback(); }
    return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t) { return 200; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t) { return ESP_OK; }
}
class DeviceErrorsRuntime : public testing::Test {
protected:
 void SetUp() override { reports_test_reset(); records.clear(); reads=sends=0; during_send=nullptr; forecast={}; payload.clear(); }
 void TearDown() override { reports_test_reset(); }
};
TEST_F(DeviceErrorsRuntime, EmptyStagesAreLoadedOnlyOnceAndCaptureReactivatesThem) {
    wind_device_errors_flush(); EXPECT_EQ(reads,WIND_DEVICE_ERROR_COUNT);
    wind_device_errors_flush(); EXPECT_EQ(reads,WIND_DEVICE_ERROR_COUNT);
    wind_device_errors_capture(WIND_DEVICE_ERROR_DISPLAY,ESP_FAIL);
    wind_device_errors_flush(); EXPECT_EQ(sends,1);
    int loaded=reads; wind_device_errors_flush(); EXPECT_EQ(reads,loaded);
}
TEST_F(DeviceErrorsRuntime, CaptureDuringDeliveryDoesNotBlockOrLoseEarlierStage) {
    wind_device_errors_capture(WIND_DEVICE_ERROR_OTA,ESP_FAIL);
    during_send=[] {
        // The semaphore stub asserts if send still holds the mutex.
        wind_device_errors_capture(WIND_DEVICE_ERROR_FORECAST,ESP_FAIL);
        wind_device_errors_flush(); // Reentrant flush must not start a second sender.
    };
    wind_device_errors_flush(); EXPECT_EQ(sends,1);
    wind_device_errors_flush(); EXPECT_EQ(sends,2);
    wind_device_errors_flush(); EXPECT_EQ(sends,2);
}
TEST_F(DeviceErrorsRuntime, OversizedPersistedRecordRecoversOnCapture) {
    records["display"]=std::vector<uint8_t>(sizeof(wind_device_error_state_t)+1);
    wind_device_errors_capture(WIND_DEVICE_ERROR_DISPLAY,ESP_FAIL);
    wind_device_errors_flush(); EXPECT_EQ(sends,1);
}

TEST_F(DeviceErrorsRuntime, ForecastFailureRetainsNetworkDetailsUntilDelivery) {
    forecast={ESP_OK, 429, ESP_ERR_NOT_FINISHED, 123, false, false};
    wind_device_errors_capture(WIND_DEVICE_ERROR_FORECAST, ESP_FAIL);
    // A later request and restart must not overwrite the original failure details.
    forecast={ESP_ERR_TIMEOUT, 0, ESP_ERR_NOT_FINISHED, 0, false, false};
    reports_test_reset();
    wind_device_errors_flush();
    EXPECT_EQ(sends,1);
    EXPECT_NE(payload.find("\"forecast_http_status\":429"),std::string::npos);
    EXPECT_NE(payload.find("\"forecast_transport_error\":0"),std::string::npos);
    EXPECT_NE(payload.find("\"forecast_response_bytes\":123"),std::string::npos);
}

TEST_F(DeviceErrorsRuntime, ForecastTransportAndSizeFailuresKeepTheirDetails) {
    forecast={ESP_ERR_TIMEOUT, 0, ESP_ERR_NOT_FINISHED, 16384, true, false};
    wind_device_errors_capture(WIND_DEVICE_ERROR_FORECAST, ESP_FAIL);
    wind_device_errors_flush();
    EXPECT_NE(payload.find("\"forecast_transport_error\":"+std::to_string(ESP_ERR_TIMEOUT)),std::string::npos);
    EXPECT_NE(payload.find("\"forecast_response_too_large\":true"),std::string::npos);
    EXPECT_NE(payload.find("\"forecast_allocation_failed\":false"),std::string::npos);
}
TEST_F(DeviceErrorsRuntime, DisplayFailuresDoNotIncludeStaleForecastDetails) {
    forecast={ESP_OK, 429, ESP_ERR_NOT_FINISHED, 123, false, false};
    wind_device_errors_capture(WIND_DEVICE_ERROR_DISPLAY, ESP_FAIL);
    wind_device_errors_flush();
    EXPECT_EQ(payload.find("forecast_http_status"),std::string::npos);
}

TEST_F(DeviceErrorsRuntime, FirmwareUpgradePreservesQueuedLegacyEventWithoutInventingDiagnostics) {
    wind_device_errors_capture(WIND_DEVICE_ERROR_FORECAST, ESP_FAIL);
    auto &record=records["forecast"];
    wind_device_error_state_t current;
    std::memcpy(&current, record.data(), sizeof(current));
    record.resize(reports_test_legacy_state_size());
    reports_test_make_legacy(record.data(), &current);
    reports_test_reset();
    wind_device_errors_flush();
    EXPECT_EQ(sends,1);
    EXPECT_EQ(payload.find("forecast_http_status"),std::string::npos);
    wind_device_errors_capture(WIND_DEVICE_ERROR_FORECAST, ESP_FAIL);
    wind_device_errors_flush();
    EXPECT_EQ(sends,1); // The migrated hourly rate limit is retained.
}
