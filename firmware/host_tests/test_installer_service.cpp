#include <gtest/gtest.h>

#include <cstring>
#include <string>

extern "C" {
#include "wind_installer_service.h"
}

namespace {
struct FakeDevice {
    bool wifi_ok = true;
    bool render_ok = true;
    bool commit_ok = true;
    int commits = 0;
    int wake_acquires = 0;
    int wake_releases = 0;
    std::string password;
    bool async_apply = false;
    bool apply_prepared = false;
    bool apply_started = false;
    bool clock_ok = true;
    int64_t clock = 0;
    hardware_profile_state_t profile = {};
    esp_err_t profile_update_result = ESP_OK;
    int wifi_tests = 0;
    int renders = 0;
    int apply_prepares = 0;
    int scans = 0;
};

esp_err_t get_hardware_profile(void *context, hardware_profile_state_t *state)
{
    *state = static_cast<FakeDevice *>(context)->profile;
    return ESP_OK;
}

esp_err_t select_hardware_profile(void *context, hardware_model_t model,
                                  uint32_t expected_revision,
                                  hardware_profile_update_result_t *result)
{
    auto *fake = static_cast<FakeDevice *>(context);
    if (expected_revision != fake->profile.revision) return ESP_ERR_INVALID_STATE;
    if (fake->profile_update_result != ESP_OK) return fake->profile_update_result;
    fake->profile.stored_model = model;
    fake->profile.source = HARDWARE_PROFILE_SOURCE_OWNER_CONFIRMATION;
    fake->profile.revision++;
    result->committed_revision = fake->profile.revision;
    result->reboot_required = true;
    return ESP_OK;
}

esp_err_t test_wifi(void *context, const char *, const char *password)
{
    auto *fake = static_cast<FakeDevice *>(context);
    fake->wifi_tests++;
    fake->password = password;
    return fake->wifi_ok ? ESP_OK : ESP_FAIL;
}

esp_err_t render(void *context, const installed_configuration_t *)
{
    auto *fake = static_cast<FakeDevice *>(context);
    fake->renders++;
    return fake->render_ok ? ESP_OK : ESP_FAIL;
}

esp_err_t commit(void *context, const installed_configuration_t *, const char *, const char *)
{
    auto *fake = static_cast<FakeDevice *>(context);
    if (!fake->commit_ok) return ESP_FAIL;
    fake->commits++;
    return ESP_OK;
}

esp_err_t begin_apply(void *context, const installed_configuration_t *, const char *, const char *)
{
    auto *fake = static_cast<FakeDevice *>(context);
    fake->apply_prepares++;
    fake->apply_prepared = true;
    return ESP_OK;
}

esp_err_t scan_wifi(void *context, char *response, size_t response_size)
{
    auto *fake = static_cast<FakeDevice *>(context);
    fake->scans++;
    return snprintf(response, response_size, R"({"networks":[]})") >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t start_apply(void *context)
{
    auto *fake = static_cast<FakeDevice *>(context);
    fake->apply_started = true;
    fake->async_apply = true;
    return ESP_OK;
}

const char *apply_state(void *context)
{
    return static_cast<FakeDevice *>(context)->async_apply ? "applying" : "idle";
}

void wake(void *context, bool held)
{
    auto *fake = static_cast<FakeDevice *>(context);
    held ? fake->wake_acquires++ : fake->wake_releases++;
}

esp_err_t set_clock(void *context, int64_t unix_seconds)
{
    auto *fake = static_cast<FakeDevice *>(context);
    if (!fake->clock_ok) return ESP_FAIL;
    fake->clock = unix_seconds;
    return ESP_OK;
}

wind_installer_service_t make_service(FakeDevice *fake)
{
    wind_installer_service_t service;
    wind_installer_dependencies_t dependencies = {
        .context = fake,
        .test_wifi = test_wifi,
        .render_candidate = render,
        .commit = commit,
        .set_wake_lock = wake,
        .set_clock = set_clock,
    };
    wind_installer_service_init(&service, &dependencies);
    return service;
}

std::string request(wind_installer_service_t *service, const char *json)
{
    char response[2048] = {0};
    EXPECT_EQ(wind_installer_service_handle_json(service, json, strlen(json), response,
                                                  sizeof(response)), ESP_OK);
    return response;
}
}

TEST(InstallerServiceTest, HelloAndStateAreRedacted)
{
    FakeDevice fake;
    auto service = make_service(&fake);
    const std::string hello = request(&service, R"({"command":"hello"})");
    EXPECT_NE(hello.find(WINDSCOUT_BOARD_ID), std::string::npos);
    EXPECT_EQ(hello.find("password"), std::string::npos);
    EXPECT_NE(hello.find("clock-sync"), std::string::npos);
    EXPECT_TRUE(service.wake_lock_held);
    const std::string state = request(&service, R"({"command":"get_state"})");
    EXPECT_EQ(state.find("password"), std::string::npos);
    EXPECT_NE(state.find("\"wifiConfigured\":false"), std::string::npos);
}

TEST(InstallerServiceTest, UniversalFirmwarePublishesAndPersistsHardwareProfile)
{
    FakeDevice fake;
    fake.profile.revision = 4;
    auto service = make_service(&fake);
    service.dependencies.get_hardware_profile = get_hardware_profile;
    service.dependencies.select_hardware_profile = select_hardware_profile;

    const std::string hello = request(&service, R"({"command":"hello"})");
    EXPECT_NE(hello.find("hardware-profile"), std::string::npos);
    EXPECT_NE(hello.find("\"hardwareModel\":\"unknown\""), std::string::npos);
    EXPECT_NE(hello.find("\"hardwareProfileRevision\":4"), std::string::npos);

    const std::string selected = request(
        &service,
        R"({"command":"set_hardware_profile","hardwareModel":"e1001","expectedRevision":4})");
    EXPECT_NE(selected.find("reboot_required"), std::string::npos);
    EXPECT_NE(selected.find("\"hardwareProfileRevision\":5"), std::string::npos);
    EXPECT_EQ(fake.profile.stored_model, HARDWARE_MODEL_E1001);

    const std::string stale = request(
        &service,
        R"({"command":"set_hardware_profile","hardwareModel":"e1002","expectedRevision":4})");
    EXPECT_NE(stale.find("hardware_profile_conflict"), std::string::npos);

    fake.profile_update_result = ESP_FAIL;
    const std::string failed = request(
        &service,
        R"({"command":"set_hardware_profile","hardwareModel":"e1001","expectedRevision":5})");
    EXPECT_NE(failed.find("hardware_profile_save_failed"), std::string::npos);

    const std::string out_of_range = request(
        &service,
        R"({"command":"set_hardware_profile","hardwareModel":"e1001","expectedRevision":4294967296})");
    EXPECT_NE(out_of_range.find("hardware_profile_rejected"), std::string::npos);
}

TEST(InstallerServiceTest, UnknownAndRecoveryProfilesBlockSetupMutations)
{
    const hardware_profile_state_t blocked_profiles[] = {
        {
            .stored_model = HARDWARE_MODEL_UNKNOWN,
            .effective_model = HARDWARE_MODEL_UNKNOWN,
            .revision = 1,
        },
        {
            .stored_model = HARDWARE_MODEL_E1001,
            .effective_model = HARDWARE_MODEL_UNKNOWN,
            .revision = 2,
            .safe_boot_override = true,
        },
        {
            .stored_model = HARDWARE_MODEL_E1002,
            .effective_model = HARDWARE_MODEL_UNKNOWN,
            .revision = 3,
            .driver_failure_latched = true,
        },
    };

    for (const auto &profile : blocked_profiles) {
        FakeDevice fake;
        fake.profile = profile;
        auto service = make_service(&fake);
        service.dependencies.get_hardware_profile = get_hardware_profile;
        service.dependencies.scan_wifi = scan_wifi;
        service.dependencies.begin_apply = begin_apply;

        EXPECT_NE(request(&service, R"({"command":"scan_networks"})")
                      .find("hardware_profile_required"), std::string::npos);
        EXPECT_NE(request(&service, R"({"command":"stage_configuration"})")
                      .find("hardware_profile_required"), std::string::npos);
        EXPECT_NE(request(&service, R"({"command":"test_wifi","ssid":"Home","password":"secret"})")
                      .find("hardware_profile_required"), std::string::npos);
        service.candidate_staged = true;
        EXPECT_NE(request(&service, R"({"command":"apply_configuration"})")
                      .find("hardware_profile_required"), std::string::npos);

        EXPECT_EQ(fake.scans, 0);
        EXPECT_EQ(fake.wifi_tests, 0);
        EXPECT_EQ(fake.renders, 0);
        EXPECT_EQ(fake.apply_prepares, 0);
        EXPECT_EQ(fake.commits, 0);
        EXPECT_TRUE(service.candidate_staged);
        EXPECT_EQ(service.candidate.version, 0u);
    }
}

TEST(InstallerServiceTest, RecoveryHelloReportsStoredModelWithoutClaimingItIsActive)
{
    const hardware_profile_state_t recovery_profiles[] = {
        {
            .stored_model = HARDWARE_MODEL_E1001,
            .effective_model = HARDWARE_MODEL_UNKNOWN,
            .revision = 7,
            .safe_boot_override = true,
        },
        {
            .stored_model = HARDWARE_MODEL_E1002,
            .effective_model = HARDWARE_MODEL_UNKNOWN,
            .revision = 8,
            .driver_failure_latched = true,
        },
    };

    for (const auto &profile : recovery_profiles) {
        FakeDevice fake;
        fake.profile = profile;
        auto service = make_service(&fake);
        service.dependencies.get_hardware_profile = get_hardware_profile;

        const std::string hello = request(&service, R"({"command":"hello"})");
        EXPECT_NE(hello.find("\"hardwareModel\":\"unknown\""), std::string::npos);
        const char *stored_model = profile.stored_model == HARDWARE_MODEL_E1001
                                       ? "e1001" : "e1002";
        EXPECT_NE(hello.find(std::string("\"storedHardwareModel\":\"") +
                             stored_model + "\""),
                  std::string::npos);
        EXPECT_NE(hello.find(std::string("\"safeBootOverride\":") +
                             (profile.safe_boot_override ? "true" : "false")),
                  std::string::npos);
        EXPECT_NE(hello.find(std::string("\"driverFailureLatched\":") +
                             (profile.driver_failure_latched ? "true" : "false")),
                  std::string::npos);
    }
}

TEST(InstallerServiceTest, BeginSetsClockFromBrowserBeforeHoldingWakeLock)
{
    FakeDevice fake;
    auto service = make_service(&fake);
    const std::string ready = request(&service, R"({"command":"begin","unixTime":1787932800})");
    EXPECT_NE(ready.find("ready"), std::string::npos);
    EXPECT_EQ(fake.clock, 1787932800);
    EXPECT_TRUE(service.wake_lock_held);

    auto invalid_service = make_service(&fake);
    const std::string invalid = request(&invalid_service,
                                        R"({"command":"begin","unixTime":123})");
    EXPECT_NE(invalid.find("clock_rejected"), std::string::npos);
    EXPECT_FALSE(invalid_service.wake_lock_held);
}

TEST(InstallerServiceTest, BeginRejectsMissingBrowserClock)
{
    FakeDevice fake;
    auto service = make_service(&fake);

    const std::string response = request(&service, R"({"command":"begin"})");

    EXPECT_NE(response.find("clock_rejected"), std::string::npos);
    EXPECT_FALSE(service.wake_lock_held);
    EXPECT_EQ(fake.clock, 0);
}

TEST(InstallerServiceTest, BeginValidatesClockBoundsAndPersistence)
{
    FakeDevice fake;
    const char *invalid_requests[] = {
        R"({"command":"begin","unixTime":4102444800})",
        R"({"command":"begin","unixTime":1787932800.5})",
        R"({"command":"begin","unixTime":"1787932800"})",
    };
    for (const char *request_json : invalid_requests) {
        auto service = make_service(&fake);
        EXPECT_NE(request(&service, request_json).find("clock_rejected"), std::string::npos);
        EXPECT_FALSE(service.wake_lock_held);
    }

    auto lower_bound_service = make_service(&fake);
    EXPECT_NE(request(&lower_bound_service,
                      R"({"command":"begin","unixTime":1735689600})").find("ready"),
              std::string::npos);

    fake.clock_ok = false;
    auto failed_persistence_service = make_service(&fake);
    EXPECT_NE(request(&failed_persistence_service,
                      R"({"command":"begin","unixTime":1787932800})").find("clock_rejected"),
              std::string::npos);
    EXPECT_FALSE(failed_persistence_service.wake_lock_held);
}

TEST(InstallerServiceTest, CommitsOnlyAfterWifiAndRenderSucceed)
{
    FakeDevice fake;
    auto service = make_service(&fake);
    request(&service, R"({"command":"begin","unixTime":1787932800})");
    request(&service, R"({"command":"stage_configuration","configuration":{"version":4,"boardId":"seeedstudio_reterminal_e1002","deviceTimezone":"Europe/Amsterdam","spot":{"id":"edam","name":"Edam","latitude":52.5126,"longitude":5.0486,"timezone":"Europe/Amsterdam"},"forecastModel":"best_match","display":{"showThreshold":false,"threshold":17,"showWeather":true,"showTemperature":false,"showTide":false,"showDedicatedFooter":true,"timeFormat":"24-hour","temperatureUnit":"celsius"},"digest":"949c6be684df723d"}})");
    const std::string wifi = request(&service, R"({"command":"test_wifi","ssid":"Home","password":"secret-value"})");
    EXPECT_NE(wifi.find("wifi_ready"), std::string::npos);
    EXPECT_EQ(wifi.find("secret-value"), std::string::npos);
    const std::string applied = request(&service, R"({"command":"apply_configuration"})");
    EXPECT_NE(applied.find("complete"), std::string::npos);
    EXPECT_EQ(fake.commits, 1);
    EXPECT_TRUE(service.credentials_cleared);
    EXPECT_EQ(fake.wake_acquires, 1);
    EXPECT_EQ(fake.wake_releases, 1);
}

TEST(InstallerServiceTest, PreservesDeviceAndSpotTimezonesSeparately)
{
    FakeDevice fake;
    auto service = make_service(&fake);
    request(&service, R"({"command":"begin","unixTime":1787932800})");

    const std::string staged = request(
        &service,
        R"({"command":"stage_configuration","configuration":{"version":4,"boardId":"seeedstudio_reterminal_e1002","deviceTimezone":"America/New_York","spot":{"id":"edam","name":"Edam","latitude":52.5126,"longitude":5.0486,"timezone":"Europe/Amsterdam"},"forecastModel":"best_match","display":{"showThreshold":false,"threshold":17,"showWeather":true,"showTemperature":false,"showTide":false,"showDedicatedFooter":true,"timeFormat":"24-hour","temperatureUnit":"celsius"},"digest":"b10310d53df55979"}})");

    EXPECT_NE(staged.find("configuration_staged"), std::string::npos);
    EXPECT_STREQ(service.candidate.device_timezone, "America/New_York");
    EXPECT_STREQ(service.candidate.spot.timezone, "Europe/Amsterdam");
}

TEST(InstallerServiceTest, WrongWifiAndRenderFailureRemainRetryable)
{
    FakeDevice fake;
    auto service = make_service(&fake);
    request(&service, R"({"command":"begin","unixTime":1787932800})");
    request(&service, R"({"command":"stage_configuration","configuration":{"version":4,"boardId":"seeedstudio_reterminal_e1002","deviceTimezone":"Europe/Amsterdam","spot":{"id":"edam","name":"Edam","latitude":52.5126,"longitude":5.0486,"timezone":"Europe/Amsterdam"},"forecastModel":"best_match","display":{"showThreshold":false,"threshold":17,"showWeather":true,"showTemperature":false,"showTide":false,"showDedicatedFooter":true,"timeFormat":"24-hour","temperatureUnit":"celsius"},"digest":"949c6be684df723d"}})");

    fake.wifi_ok = false;
    const std::string rejected = request(&service, R"({"command":"test_wifi","ssid":"Home","password":"wrong"})");
    EXPECT_NE(rejected.find("wifi_rejected"), std::string::npos);
    EXPECT_EQ(fake.commits, 0);

    fake.wifi_ok = true;
    fake.render_ok = false;
    request(&service, R"({"command":"test_wifi","ssid":"Home","password":"right"})");
    const std::string failed = request(&service, R"({"command":"apply_configuration"})");
    EXPECT_NE(failed.find("render_failed"), std::string::npos);
    EXPECT_EQ(fake.commits, 0);
    EXPECT_TRUE(service.wake_lock_held);

    request(&service, R"({"command":"cancel"})");
    EXPECT_FALSE(service.wake_lock_held);
    EXPECT_TRUE(service.credentials_cleared);
}

TEST(InstallerServiceTest, AsyncApplyReturnsBeforeRenderAndReportsProgress)
{
    FakeDevice fake;
    auto service = make_service(&fake);
    service.dependencies.begin_apply = begin_apply;
    service.dependencies.start_apply = start_apply;
    service.dependencies.apply_state = apply_state;
    request(&service, R"({"command":"begin","unixTime":1787932800})");
    request(&service, R"({"command":"stage_configuration","configuration":{"version":4,"boardId":"seeedstudio_reterminal_e1002","deviceTimezone":"Europe/Amsterdam","spot":{"id":"edam","name":"Edam","latitude":52.5126,"longitude":5.0486,"timezone":"Europe/Amsterdam"},"forecastModel":"best_match","display":{"showThreshold":false,"threshold":17,"showWeather":true,"showTemperature":false,"showTide":false,"showDedicatedFooter":true,"timeFormat":"24-hour","temperatureUnit":"celsius"},"digest":"949c6be684df723d"}})");

    const std::string applying = request(&service, R"({"command":"apply_configuration"})");
    EXPECT_NE(applying.find("applying"), std::string::npos);
    EXPECT_TRUE(fake.apply_prepared);
    EXPECT_FALSE(fake.apply_started);
    EXPECT_FALSE(fake.async_apply);
    EXPECT_TRUE(service.wake_lock_held);

    EXPECT_EQ(wind_installer_service_start_pending_apply(&service), ESP_OK);
    EXPECT_TRUE(fake.apply_started);
    EXPECT_TRUE(fake.async_apply);

    const std::string state = request(&service, R"({"command":"get_state"})");
    EXPECT_NE(state.find("\"apply\":\"applying\""), std::string::npos);

    const std::string cancel = request(&service, R"({"command":"cancel"})");
    EXPECT_NE(cancel.find("apply_busy"), std::string::npos);
    char malformed_response[256] = {0};
    EXPECT_EQ(wind_installer_service_handle_json(&service, "{", 1, malformed_response,
                                                 sizeof(malformed_response)),
              ESP_ERR_INVALID_ARG);
    wind_installer_service_timeout(&service);
    EXPECT_TRUE(service.wake_lock_held);
    EXPECT_TRUE(service.candidate_staged);

    fake.async_apply = false;
    wind_installer_service_complete_apply(&service, true);
    EXPECT_FALSE(service.wake_lock_held);
    EXPECT_TRUE(service.credentials_cleared);
}

TEST(InstallerServiceTest, AsyncApplyDoesNotStartWhenAcknowledgementWasNotTransmitted)
{
    FakeDevice fake;
    auto service = make_service(&fake);
    service.dependencies.begin_apply = begin_apply;
    service.dependencies.start_apply = start_apply;
    service.dependencies.apply_state = apply_state;
    request(&service, R"({"command":"begin","unixTime":1787932800})");
    request(&service, R"({"command":"stage_configuration","configuration":{"version":4,"boardId":"seeedstudio_reterminal_e1002","deviceTimezone":"Europe/Amsterdam","spot":{"id":"edam","name":"Edam","latitude":52.5126,"longitude":5.0486,"timezone":"Europe/Amsterdam"},"forecastModel":"best_match","display":{"showThreshold":false,"threshold":17,"showWeather":true,"showTemperature":false,"showTide":false,"showDedicatedFooter":true,"timeFormat":"24-hour","temperatureUnit":"celsius"},"digest":"949c6be684df723d"}})");
    request(&service, R"({"command":"apply_configuration"})");

    EXPECT_EQ(wind_installer_service_confirm_pending_apply_response(&service, false), ESP_FAIL);
    EXPECT_FALSE(fake.apply_started);
    EXPECT_FALSE(service.apply_start_pending);
    EXPECT_TRUE(service.credentials_cleared);
}

TEST(InstallerServiceTest, TimeoutAndMalformedRequestsReleaseOrPreserveStateSafely)
{
    FakeDevice fake;
    auto service = make_service(&fake);
    request(&service, R"({"command":"begin","unixTime":1787932800})");
    EXPECT_TRUE(service.wake_lock_held);
    wind_installer_service_timeout(&service);
    EXPECT_FALSE(service.wake_lock_held);
    EXPECT_TRUE(service.credentials_cleared);

    char response[128];
    EXPECT_EQ(wind_installer_service_handle_json(&service, "{", 1, response, sizeof(response)),
              ESP_ERR_INVALID_ARG);
    EXPECT_EQ(fake.commits, 0);
}

TEST(InstallerServiceTest, RejectsConfigurationValuesOutsideThePublishedSchema)
{
    FakeDevice fake;
    auto service = make_service(&fake);
    request(&service, R"({"command":"begin","unixTime":1787932800})");

    const char *invalid_configurations[] = {
        R"({"command":"stage_configuration","configuration":{"version":4,"boardId":"seeedstudio_reterminal_e1002","spot":{"id":"edam","name":"Edam","latitude":52.5126,"longitude":5.0486,"timezone":"Europe/Amsterdam"},"forecastModel":"best_match","display":{"showThreshold":false,"threshold":17,"showWeather":true,"showTemperature":false,"showTide":false,"showDedicatedFooter":true,"timeFormat":"24-hour","temperatureUnit":"celsius"},"digest":"949c6be684df723d"}})",
        R"({"command":"stage_configuration","configuration":{"version":4,"boardId":"seeedstudio_reterminal_e1002","deviceTimezone":"Europe/Definitely-Not-A-Zone","spot":{"id":"edam","name":"Edam","latitude":52.5126,"longitude":5.0486,"timezone":"Europe/Amsterdam"},"forecastModel":"best_match","display":{"showThreshold":false,"threshold":17,"showWeather":true,"showTemperature":false,"showTide":false,"showDedicatedFooter":true,"timeFormat":"24-hour","temperatureUnit":"celsius"},"digest":"949c6be684df723d"}})",
        R"({"command":"stage_configuration","configuration":{"version":2,"boardId":"seeedstudio_reterminal_e1002","spot":{"id":"Edam","name":"Edam","latitude":52.5126,"longitude":5.0486,"timezone":"Europe/Amsterdam"},"forecastModel":"best_match","display":{"showThreshold":false,"threshold":17,"showWeather":true,"showTemperature":false,"showTide":false,"timeFormat":"24-hour","temperatureUnit":"celsius"},"digest":"bde996ae21f5ca31"}})",
        R"({"command":"stage_configuration","configuration":{"version":2,"boardId":"seeedstudio_reterminal_e1002","spot":{"id":"edam","name":"Edam","latitude":52.5126,"longitude":5.0486,"timezone":"Europe/Amsterdam"},"forecastModel":"best_match","display":{"showThreshold":false,"threshold":17,"showWeather":true,"showTemperature":false,"showTide":false,"timeFormat":"local","temperatureUnit":"celsius"},"digest":"bde996ae21f5ca31"}})",
        R"({"command":"stage_configuration","configuration":{"version":2,"boardId":"seeedstudio_reterminal_e1002","spot":{"id":"edam","name":"Edam","latitude":52.5126,"longitude":5.0486,"timezone":"Europe/Amsterdam"},"forecastModel":"best_match","display":{"showThreshold":false,"threshold":17,"showWeather":true,"showTemperature":false,"showTide":false,"timeFormat":"24-hour","temperatureUnit":"kelvin","extra":true},"digest":"bde996ae21f5ca31"}})",
        R"({"command":"stage_configuration","configuration":{"version":2,"boardId":"seeedstudio_reterminal_e1002","spot":{"id":"edam","name":"Edam","latitude":52.5126,"longitude":5.0486,"timezone":"Europe/Amsterdam"},"forecastModel":"best_match","display":{"showThreshold":false,"threshold":17,"showWeather":true,"showTemperature":false,"showTide":false,"timeFormat":"24-hour","temperatureUnit":"celsius"},"digest":"BDE996AE21F5CA31","extra":true}})",
    };
    for (const char *invalid : invalid_configurations) {
        EXPECT_NE(request(&service, invalid).find("configuration_rejected"), std::string::npos);
        EXPECT_FALSE(service.candidate_staged);
    }
}

TEST(InstallerServiceTest, RejectsUnknownCredentialFieldsBeforeUsingWifi)
{
    FakeDevice fake;
    auto service = make_service(&fake);
    request(&service, R"({"command":"begin","unixTime":1787932800})");

    const std::string response = request(
        &service,
        R"({"command":"test_wifi","ssid":"Home","password":"secret","persist":true})");

    EXPECT_NE(response.find("wifi_rejected"), std::string::npos);
    EXPECT_TRUE(fake.password.empty());
    EXPECT_TRUE(service.credentials_cleared);
}
