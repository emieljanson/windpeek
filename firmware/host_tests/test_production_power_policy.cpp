#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "config.h"

#ifdef WINDPEEK_DEVELOPMENT_MODE
#error "Windpeek production firmware must not expose an always-on development mode"
#endif

TEST(ProductionPowerPolicy, AlwaysOnDevelopmentModeIsUnavailable)
{
    SUCCEED();
}

static std::string read_source(const char *path)
{
    std::ifstream input(path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

TEST(ProductionPowerPolicy, UsbPowerNeverDisablesFutureBatteryWakes)
{
    const std::string main_source = read_source(WIND_MAIN_SOURCE);
    EXPECT_EQ(main_source.find("config_manager_set_auto_rotate(false)"), std::string::npos);

    const std::string power_source = read_source(WIND_POWER_SOURCE);
    EXPECT_NE(power_source.find("return true;"), std::string::npos);
    EXPECT_NE(power_source.find("wind_app_seconds_until_next_wake()"), std::string::npos);
    EXPECT_NE(power_source.find("esp_sleep_get_wakeup_causes()"), std::string::npos);
    EXPECT_EQ(power_source.find("esp_sleep_get_wakeup_cause()"), std::string::npos);
    EXPECT_NE(power_source.find("board_hal_is_usb_connected() || installer_active"),
              std::string::npos);
}

TEST(ProductionPowerPolicy, E100xCapabilityBuildContainsOnlyTheWindpeekRuntime)
{
    const std::string cmake = read_source(WIND_CMAKE_SOURCE);
    EXPECT_NE(cmake.find("if(CONFIG_BOARD_CAP_WINDPEEK)"), std::string::npos);
    EXPECT_NE(cmake.find("windpeek_main.c"), std::string::npos);
    EXPECT_NE(cmake.find("windpeek_display_manager.c"), std::string::npos);

    const std::string main_source = read_source(WIND_MAIN_SOURCE);
    for (const char *legacy : {"album_manager", "ha_integration", "http_server",
                               "ota_manager", "wifi_provisioning", "image_processor"}) {
        EXPECT_EQ(main_source.find(legacy), std::string::npos) << legacy;
    }
}

TEST(ProductionPowerPolicy, RuntimePanelSelectionIsLimitedToTheUniversalTarget)
{
    const std::string main_source = read_source(WIND_MAIN_SOURCE);
    const auto guard = main_source.find("#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E100X");
    const auto selection = main_source.find("epaper_select_backend(panel_hardware)");
    ASSERT_NE(guard, std::string::npos);
    ASSERT_NE(selection, std::string::npos);
    EXPECT_LT(guard, selection);
}

TEST(ProductionPowerPolicy, PanelInitializationFailureKeepsUsbRecoveryAlive)
{
    const std::string main_source = read_source(WIND_MAIN_SOURCE);
    const auto initialization = main_source.find("result = board_hal_init()");
    const auto recovery = main_source.find("wind_installer_service_start()", initialization);
    ASSERT_NE(initialization, std::string::npos);
    ASSERT_NE(recovery, std::string::npos);
    EXPECT_LT(initialization, recovery);
}

TEST(ProductionPowerPolicy, UniversalCarrierSkipsPanelSleepWithoutAnActiveBackend)
{
    const std::string board_source = read_source(E100X_BOARD_SOURCE);
    EXPECT_NE(board_source.find("if (epaper_has_active_backend())"), std::string::npos);
}

TEST(ProductionPowerPolicy, ScheduledWifiRefreshHasABoundedRadioBudget)
{
    const std::string wifi_source = read_source(WIFI_MANAGER_SOURCE);
    const std::string main_source = read_source(WIND_MAIN_SOURCE);

    EXPECT_NE(wifi_source.find("#define WIFI_REFRESH_CONNECT_TIMEOUT_MS 12000"),
              std::string::npos);
    EXPECT_NE(wifi_source.find("#define WIFI_REFRESH_MAX_RETRIES 1"), std::string::npos);
    EXPECT_NE(main_source.find("wifi_manager_connect_for_refresh(ssid, password)"),
              std::string::npos);
}

TEST(ProductionPowerPolicy, OpenWifiUsesAnOpenAuthenticationThreshold)
{
    const std::string wifi_source = read_source(WIFI_MANAGER_SOURCE);

    EXPECT_NE(wifi_source.find(
                  "password && password[0] != '\\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN"),
              std::string::npos);
}
