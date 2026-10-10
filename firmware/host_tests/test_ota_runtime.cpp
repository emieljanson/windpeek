#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
extern "C" {
#include "ota_platform.h"
#include "wind_ota.h"
#include "wind_device_errors.h"
#include "cJSON.h"
}
struct Http { std::string body; size_t position = 0; int status = 200; bool complete = true; };
static std::map<std::string, Http> responses;
static std::vector<std::string> requests;
static std::string written;
static int writes, switches, restarts, aborts, reports, confirms, rollbacks;
static bool installer, pending_boot, storage_fails, sequence_store_fails, usb_power;
static int battery_reads;
static int64_t last_check;
static uint32_t sequence;
static const int64_t now = 1787544000;
static esp_err_t write_result, end_result, select_result;
static esp_app_desc_t current = {"dev-old", "windpeek"};
static esp_app_desc_t candidate;
static const esp_partition_t running = {0x20000, 0x380000}, inactive = {0x3a0000, 0x380000};
extern "C" {
time_t ota_test_time(time_t *out) { if(out) *out=now; return now; }
int64_t esp_timer_get_time(void) { return now*1000000; }
const char *esp_err_to_name(esp_err_t) { return "test-error"; }
bool wifi_manager_is_connected(void) { return true; }
bool board_hal_is_usb_connected(void) { return usb_power; }
int board_hal_get_battery_voltage(void) { return ++battery_reads > 1 ? 3700 : 4000; }
bool power_manager_is_installer_active(void) { return installer; }
int esp_crt_bundle_attach(void *) { return 0; }
void wind_device_errors_capture(wind_device_error_stage_t, esp_err_t) { ++reports; }
void wind_device_errors_flush(void) {}
esp_err_t nvs_open(const char *, int, nvs_handle_t *out) { *out=1; return ESP_OK; }
esp_err_t nvs_get_i64(nvs_handle_t, const char *, int64_t *out) { *out=last_check; return ESP_OK; }
esp_err_t nvs_get_u32(nvs_handle_t, const char *, uint32_t *out) { *out=sequence; return ESP_OK; }
esp_err_t nvs_set_i64(nvs_handle_t, const char *, int64_t value) { last_check=value; return ESP_OK; }
esp_err_t nvs_set_u32(nvs_handle_t, const char *, uint32_t value) { if(sequence_store_fails) return ESP_FAIL; sequence=value; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t) { return storage_fails ? ESP_FAIL : ESP_OK; }
void nvs_close(nvs_handle_t) {}
const esp_partition_t *esp_ota_get_running_partition(void) { return &running; }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *) { return &inactive; }
esp_err_t esp_ota_begin(const esp_partition_t *part, size_t, esp_ota_handle_t *out) {
    EXPECT_EQ(part->address, inactive.address); *out=1; ++writes; return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t, const void *data, size_t size) {
    written.append(static_cast<const char *>(data), size); return write_result;
}
esp_err_t esp_ota_end(esp_ota_handle_t) { return end_result; }
esp_err_t esp_ota_abort(esp_ota_handle_t) { ++aborts; return ESP_OK; }
esp_err_t esp_ota_get_partition_description(const esp_partition_t *, esp_app_desc_t *out) { *out=candidate; return ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *part) {
    EXPECT_EQ(part->address, inactive.address); ++switches; return select_result;
}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *, esp_ota_img_states_t *out) {
    *out=pending_boot ? ESP_OTA_IMG_PENDING_VERIFY : ESP_OTA_IMG_UNDEFINED; return ESP_OK;
}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) { ++confirms; return ESP_OK; }
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void) { ++rollbacks; return ESP_OK; }
const esp_app_desc_t *esp_app_get_description(void) { return &current; }
void esp_restart(void) { ++restarts; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    EXPECT_TRUE(config->disable_auto_redirect); EXPECT_NE(config->crt_bundle_attach, nullptr);
    requests.emplace_back(config->url);
    if (!responses.count(config->url)) return nullptr;
    return new Http(responses[config->url]);
}
esp_err_t esp_http_client_open(esp_http_client_handle_t, int) { return ESP_OK; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h) { return h->body.size(); }
int esp_http_client_get_status_code(esp_http_client_handle_t h) { return h->status; }
int64_t esp_http_client_get_content_length(esp_http_client_handle_t h) { return h->body.size(); }
int esp_http_client_read(esp_http_client_handle_t h, char *out, int capacity) {
    size_t count=std::min(h->body.size()-h->position, static_cast<size_t>(capacity));
    memcpy(out, h->body.data()+h->position, count); h->position+=count; return count;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h) { return h->complete && h->position==h->body.size(); }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h) { delete h; return ESP_OK; }
// The platform owns SHA-256. A deterministic digest double tests that production
// code hashes the received bytes and gates boot selection on the result.
int psa_hash_setup(psa_hash_operation_t *op, int) { *op={}; return 0; }
int psa_hash_update(psa_hash_operation_t *op, const uint8_t *bytes, size_t size) {
    for(size_t i=0;i<size;++i) op->hash[op->count++%32]^=bytes[i]; return 0;
}
int psa_hash_finish(psa_hash_operation_t *op, uint8_t *out, size_t, size_t *length) { memcpy(out,op->hash,32); *length=32; return 0; }
int psa_hash_abort(psa_hash_operation_t *) { return 0; }
int psa_hash_compute(int, const uint8_t *bytes, size_t size, uint8_t *out, size_t capacity, size_t *length) {
    psa_hash_operation_t op{}; psa_hash_update(&op,bytes,size); return psa_hash_finish(&op,out,capacity,length);
}
}
static std::string digest(const std::string &bytes) {
    uint8_t out[32]; size_t length; psa_hash_compute(1, reinterpret_cast<const uint8_t *>(bytes.data()),bytes.size(),out,32,&length);
    char hex[65]; for(int i=0;i<32;++i) snprintf(hex+i*2,3,"%02x",out[i]); return hex;
}
class OtaRuntime : public testing::Test {
protected:
    const std::string root="https://windpeek.com/firmware/";
    void SetUp() override {
        responses.clear(); requests.clear(); written.clear();
        writes=switches=restarts=aborts=reports=confirms=rollbacks=0;
        installer=pending_boot=storage_fails=sequence_store_fails=false; usb_power=true; battery_reads=0; last_check=0; sequence=0;
        write_result=end_result=select_result=ESP_OK;
        candidate={}; strcpy(candidate.version,"dev-new"); strcpy(candidate.project_name,"windpeek");
        std::string image(10037, 0);
        for (size_t i=0; i<image.size(); ++i) image[i]=static_cast<char>((i*37+i/251)%256);
        responses[root+"new/app.bin"]={image};
        std::string manifest=std::string("{\"version\":\"dev-new\",\"schemaVersion\":1,\"firmwareLayoutVersion\":1,\"flashSize\":33554432,")+
            "\"otaProtocol\":1,\"otaSequence\":42,\"chipFamily\":\"ESP32-S3\",\"boardId\":\"seeedstudio_reterminal_e1002\","
            "\"configuration\":{\"minimum\":5,\"maximum\":6},\"otaApplication\":\"app.bin\",\"parts\":[{\"kind\":\"application\","
            "\"file\":\"app.bin\",\"size\":10037,\"sha256\":\""+digest(responses[root+"new/app.bin"].body)+"\"}]}";
        responses[root+"new/manifest.json"]={manifest};
        refresh_pointer();
    }
    void refresh_pointer() {
        responses[root+"latest.json"]={"{\"version\":\"dev-new\",\"manifest\":\"new/manifest.json\",\"sha256\":\""+
            digest(responses[root+"new/manifest.json"].body)+"\"}"};
    }
};
TEST_F(OtaRuntime, VerifiesDownloadsWritesInactiveSlotAndRestartsOnlyAfterSuccess) {
    wind_ota_maybe_update(); EXPECT_EQ(writes,1); EXPECT_EQ(written,responses[root+"new/app.bin"].body);
    EXPECT_EQ(switches,1); EXPECT_EQ(restarts,1); EXPECT_EQ(sequence,42u); EXPECT_EQ(reports,0);
}
TEST_F(OtaRuntime, CorruptManifestNeverTouchesFlash) {
    responses[root+"new/manifest.json"].body+=" "; wind_ota_maybe_update();
    EXPECT_EQ(writes,0); EXPECT_EQ(switches,0); EXPECT_EQ(restarts,0); EXPECT_EQ(reports,1);
}
TEST_F(OtaRuntime, CorruptBinaryAbortsAndKeepsCurrentBootSelection) {
    responses[root+"new/app.bin"].body[0]='z'; wind_ota_maybe_update();
    EXPECT_EQ(writes,1); EXPECT_EQ(aborts,1); EXPECT_EQ(switches,0); EXPECT_EQ(restarts,0);
}
TEST_F(OtaRuntime, TruncatedDownloadAndHttpRedirectNeverSwitchBoot) {
    responses[root+"new/app.bin"].complete=false; wind_ota_maybe_update(); EXPECT_EQ(switches,0);
    last_check=0; responses[root+"new/app.bin"].status=302; wind_ota_maybe_update(); EXPECT_EQ(switches,0);
}
TEST_F(OtaRuntime, FlashFailureAbortsAndDoesNotRetryUntilNextDay) {
    write_result=ESP_FAIL; wind_ota_maybe_update(); EXPECT_EQ(aborts,1); EXPECT_EQ(switches,0);
    auto count=requests.size(); wind_ota_maybe_update(); EXPECT_EQ(requests.size(),count);
}
TEST_F(OtaRuntime, RejectsWrongEmbeddedVersionAndImageValidationFailure) {
    strcpy(candidate.version,"wrong"); wind_ota_maybe_update(); EXPECT_EQ(switches,0);
    last_check=0; strcpy(candidate.version,"dev-new"); end_result=ESP_FAIL;
    wind_ota_maybe_update(); EXPECT_EQ(switches,0);
}
TEST_F(OtaRuntime, OlderSequenceAndInstallerActivityNeverWriteFlash) {
    sequence=43; wind_ota_maybe_update(); EXPECT_EQ(writes,0);
    last_check=0; sequence=0; installer=true; wind_ota_maybe_update(); EXPECT_EQ(writes,0);
}
TEST_F(OtaRuntime, FailedPersistenceNeverStartsDownload) {
    storage_fails=true; wind_ota_maybe_update(); EXPECT_TRUE(requests.empty()); EXPECT_EQ(writes,0);
}
TEST_F(OtaRuntime, FirstBootHealthControlsConfirmationOrRollback) {
    wind_ota_finish_boot(false); EXPECT_EQ(rollbacks,0);
    pending_boot=true; wind_ota_finish_boot(true); EXPECT_EQ(confirms,1);
    wind_ota_finish_boot(false); EXPECT_EQ(rollbacks,1); EXPECT_EQ(reports,1);
}

TEST_F(OtaRuntime, SequencePersistenceFailureKeepsCurrentBootSelection) {
    sequence_store_fails=true;
    wind_ota_maybe_update();
    EXPECT_EQ(writes,1); EXPECT_EQ(switches,0); EXPECT_EQ(restarts,0); EXPECT_EQ(reports,1);
}
TEST_F(OtaRuntime, FallingBatteryAbortsBeforeApplicationWrite) {
    usb_power=false;
    wind_ota_maybe_update();
    EXPECT_EQ(writes,1); EXPECT_TRUE(written.empty()); EXPECT_EQ(aborts,1);
    EXPECT_EQ(switches,0); EXPECT_EQ(restarts,0);
}

TEST_F(OtaRuntime, BootSelectionFailureRestoresReleaseEligibility) {
    select_result=ESP_FAIL;
    wind_ota_maybe_update();
    EXPECT_EQ(switches,1); EXPECT_EQ(restarts,0); EXPECT_EQ(sequence,0u);
    select_result=ESP_OK; last_check=0;
    wind_ota_maybe_update();
    EXPECT_EQ(restarts,1); EXPECT_EQ(sequence,42u);
}
