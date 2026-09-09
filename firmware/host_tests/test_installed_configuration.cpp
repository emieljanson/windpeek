#include <gtest/gtest.h>

extern "C" {
#include "installed_configuration.h"
}

TEST(InstalledConfigurationTest, DefaultIsValidAndStable)
{
    installed_configuration_t config;
    installed_configuration_default(&config);

    EXPECT_TRUE(installed_configuration_validate(&config));
    EXPECT_STREQ(config.board_id, WINDPEEK_BOARD_ID);
    EXPECT_STREQ(config.device_timezone, "Europe/Amsterdam");
    EXPECT_STREQ(config.spot.id, "brouwersdam");
    EXPECT_EQ(installed_configuration_digest(&config), UINT64_C(0xf0d2b5b76c69406e));
}

TEST(InstalledConfigurationTest, RejectsUnsupportedAndOutOfBoundsValues)
{
    installed_configuration_t config;
    installed_configuration_default(&config);
    config.spot.latitude = 90.0001;
    EXPECT_FALSE(installed_configuration_validate(&config));

    installed_configuration_default(&config);
    config.version++;
    EXPECT_FALSE(installed_configuration_validate(&config));

    installed_configuration_default(&config);
    config.board_id[0] = 'x';
    EXPECT_FALSE(installed_configuration_validate(&config));
}

TEST(InstalledConfigurationTest, AcceptsA64CharacterSpotId)
{
    installed_configuration_reset_host_storage();
    installed_configuration_t config;
    installed_configuration_default(&config);
    memset(config.spot.id, 'a', 64);
    config.spot.id[64] = '\0';

    ASSERT_TRUE(installed_configuration_validate(&config));
    ASSERT_EQ(installed_configuration_promote(&config), ESP_OK);

    installed_configuration_t loaded;
    ASSERT_EQ(installed_configuration_load(&loaded), ESP_OK);
    EXPECT_STREQ(loaded.spot.id, config.spot.id);
}

TEST(InstalledConfigurationTest, MigratesV2SettingsAndWifiCredentialsToCurrentVersion)
{
    installed_configuration_reset_host_storage();
    installed_configuration_t legacy;
    installed_configuration_default(&legacy);
    legacy.version = 2;
    snprintf(legacy.spot.id, sizeof(legacy.spot.id), "edam");
    snprintf(legacy.spot.display_name, sizeof(legacy.spot.display_name), "Edam");
    legacy.spot.latitude = 52.5126;
    legacy.spot.longitude = 5.0486;
    legacy.display.show_threshold = true;
    legacy.display.threshold_kt = 23;
    legacy.display.show_temperature = true;
    legacy.display.use_24_hour = false;
    legacy.display.temperature_fahrenheit = true;
    installed_configuration_seed_v2_host_storage(&legacy, "Home WiFi", "secret-value");

    installed_configuration_t loaded;
    ASSERT_EQ(installed_configuration_load(&loaded), ESP_OK);
    EXPECT_EQ(loaded.version, INSTALLED_CONFIGURATION_VERSION);
    EXPECT_STREQ(loaded.spot.id, "edam");
    EXPECT_TRUE(loaded.display.show_threshold);
    EXPECT_EQ(loaded.display.threshold_kt, 23);
    EXPECT_TRUE(loaded.display.show_temperature);
    EXPECT_FALSE(loaded.display.show_dedicated_footer);
    EXPECT_FALSE(loaded.display.use_24_hour);
    EXPECT_TRUE(loaded.display.temperature_fahrenheit);

    char ssid[33] = {0};
    char password[65] = {0};
    ASSERT_EQ(installed_configuration_load_credentials(
                  ssid, sizeof(ssid), password, sizeof(password)), ESP_OK);
    EXPECT_STREQ(ssid, "Home WiFi");
    EXPECT_STREQ(password, "secret-value");

    installed_configuration_t loaded_again;
    ASSERT_EQ(installed_configuration_load(&loaded_again), ESP_OK);
    EXPECT_EQ(installed_configuration_digest(&loaded_again),
              installed_configuration_digest(&loaded));
}

TEST(InstalledConfigurationTest, MigratesV3UsingTheSpotTimezoneAsDeviceFallback)
{
    installed_configuration_reset_host_storage();
    installed_configuration_t legacy;
    installed_configuration_default(&legacy);
    snprintf(legacy.spot.timezone, sizeof(legacy.spot.timezone), "America/New_York");
    snprintf(legacy.device_timezone, sizeof(legacy.device_timezone), "Europe/Amsterdam");
    installed_configuration_seed_v3_host_storage(&legacy, "Home WiFi", "secret-value");

    installed_configuration_t loaded;
    ASSERT_EQ(installed_configuration_load(&loaded), ESP_OK);
    EXPECT_EQ(loaded.version, INSTALLED_CONFIGURATION_VERSION);
    EXPECT_STREQ(loaded.spot.timezone, "America/New_York");
    EXPECT_STREQ(loaded.device_timezone, "America/New_York");
}

TEST(InstalledConfigurationTest, InterruptedCandidateNeverReplacesActive)
{
    installed_configuration_reset_host_storage();
    installed_configuration_t original;
    installed_configuration_default(&original);
    ASSERT_EQ(installed_configuration_promote(&original), ESP_OK);

    installed_configuration_t candidate = original;
    snprintf(candidate.spot.id, sizeof(candidate.spot.id), "edam");
    snprintf(candidate.spot.display_name, sizeof(candidate.spot.display_name), "EDAM");
    candidate.spot.latitude = 52.5126;
    candidate.spot.longitude = 5.0486;
    candidate.generation++;

    for (int boundary = 0; boundary < INSTALLED_CONFIGURATION_WRITE_BOUNDARY_COUNT; ++boundary) {
        installed_configuration_set_host_failure_boundary(boundary);
        EXPECT_NE(installed_configuration_promote(&candidate), ESP_OK);
        installed_configuration_t loaded;
        ASSERT_EQ(installed_configuration_load(&loaded), ESP_OK);
        EXPECT_STREQ(loaded.spot.id, original.spot.id);
    }

    installed_configuration_set_host_failure_boundary(-1);
    ASSERT_EQ(installed_configuration_promote(&candidate), ESP_OK);
    installed_configuration_t loaded;
    ASSERT_EQ(installed_configuration_load(&loaded), ESP_OK);
    EXPECT_STREQ(loaded.spot.id, "edam");
}

TEST(InstalledConfigurationTest, MigratesV4AndPreservesWifiAndTimezone) {
    installed_configuration_reset_host_storage();
    installed_configuration_t old;
    installed_configuration_default(&old);
    strcpy(old.device_timezone, "America/New_York");
    old.display.show_dedicated_footer = true;
    installed_configuration_seed_v4_host_storage(&old, "home", "secret");
    installed_configuration_t restored;
    ASSERT_EQ(installed_configuration_load(&restored), ESP_OK);
    EXPECT_EQ(restored.version, 5u);
    EXPECT_STREQ(restored.device_timezone, "America/New_York");
    EXPECT_TRUE(restored.display.show_dedicated_footer);
    EXPECT_EQ(restored.display.wind_size, 2);
    EXPECT_EQ(restored.display.swell_size, 0);
    EXPECT_STREQ(restored.display.swell_model, "best_match");
    char ssid[33], password[65];
    ASSERT_EQ(installed_configuration_load_credentials(ssid, sizeof(ssid), password, sizeof(password)), ESP_OK);
    EXPECT_STREQ(ssid, "home");
    EXPECT_STREQ(password, "secret");
}

TEST(InstalledConfigurationTest, RoundTripsSwellAndOrderAndRejectsDuplicates) {
    installed_configuration_reset_host_storage();
    installed_configuration_t config, restored;
    installed_configuration_default(&config);
    config.display.wind_size = 1;
    config.display.swell_size = 2;
    config.display.module_order[0] = 1;
    config.display.module_order[1] = 0;
    strcpy(config.display.swell_model, "meteofrance_wave");
    ASSERT_EQ(installed_configuration_promote(&config), ESP_OK);
    ASSERT_EQ(installed_configuration_load(&restored), ESP_OK);
    EXPECT_EQ(installed_configuration_digest(&config), installed_configuration_digest(&restored));
    EXPECT_EQ(restored.display.module_order[0], 1);
    EXPECT_STREQ(restored.display.swell_model, "meteofrance_wave");
    restored.display.module_order[1] = 1;
    EXPECT_FALSE(installed_configuration_validate(&restored));
}

TEST(InstalledConfigurationTest, PersistsEwamModel) {
    installed_configuration_reset_host_storage();
    installed_configuration_t config, restored;
    installed_configuration_default(&config);
    strcpy(config.display.swell_model, "dwd_ewam");
    ASSERT_EQ(installed_configuration_promote(&config), ESP_OK);
    ASSERT_EQ(installed_configuration_load(&restored), ESP_OK);
    EXPECT_STREQ(restored.display.swell_model, "dwd_ewam");
    EXPECT_EQ(installed_configuration_digest(&config), installed_configuration_digest(&restored));
}
