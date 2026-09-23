#include <gtest/gtest.h>

extern "C" {
#include "wind_display_config.h"
}

TEST(WindDisplayConfig, UsesSettledDefaults)
{
    wind_display_config_t config{};
    wind_display_config_default(&config);
    EXPECT_TRUE(wind_display_config_validate(&config));
    EXPECT_EQ(config.version, WIND_DISPLAY_CONFIG_VERSION);
    EXPECT_EQ(config.display_mode, WIND_RENDERER_MODE_SOLID);
    EXPECT_EQ(config.threshold_kt, WIND_RENDERER_DEFAULT_THRESHOLD_KT);
    EXPECT_TRUE(config.show_weather);
    EXPECT_FALSE(config.show_temperature);
    EXPECT_FALSE(config.show_tide);
    EXPECT_TRUE(config.show_dedicated_footer);
    EXPECT_TRUE(config.use_24_hour);
    EXPECT_FALSE(config.temperature_fahrenheit);
}

TEST(WindDisplayConfig, AcceptsEveryPreviouslyStoredVersionForLoading)
{
    EXPECT_FALSE(wind_display_config_stored_version_supported(0));
    EXPECT_TRUE(wind_display_config_stored_version_supported(1));
    EXPECT_TRUE(wind_display_config_stored_version_supported(2));
    EXPECT_TRUE(wind_display_config_stored_version_supported(WIND_DISPLAY_CONFIG_VERSION));
    EXPECT_FALSE(wind_display_config_stored_version_supported(WIND_DISPLAY_CONFIG_VERSION + 1));
}

TEST(WindDisplayConfig, AcceptsEveryVisibilityCombinationAndRejectsBadBounds)
{
    wind_display_config_t config{};
    wind_display_config_default(&config);
    for (int mask = 0; mask < 8; ++mask) {
        config.show_weather = mask & 1;
        config.show_temperature = mask & 2;
        config.show_tide = mask & 4;
        EXPECT_TRUE(wind_display_config_validate(&config)) << mask;
        EXPECT_NE(wind_display_config_signature(&config), 0u) << mask;
    }
    config.threshold_kt = WIND_RENDERER_MIN_THRESHOLD_KT - 1;
    EXPECT_FALSE(wind_display_config_validate(&config));
    config.threshold_kt = WIND_RENDERER_MAX_THRESHOLD_KT + 1;
    EXPECT_FALSE(wind_display_config_validate(&config));
    config.threshold_kt = WIND_RENDERER_DEFAULT_THRESHOLD_KT;
    config.display_mode = 0;
    EXPECT_FALSE(wind_display_config_validate(&config));
    config.display_mode = WIND_RENDERER_MODE_SOLID;
    config.version += 1;
    EXPECT_FALSE(wind_display_config_validate(&config));
}

TEST(WindDisplayConfig, DisplayOnlyChangesProduceDifferentPanelSignatures)
{
    wind_display_config_t first{};
    wind_display_config_default(&first);
    auto second = first;
    second.show_temperature = true;
    EXPECT_NE(wind_display_config_signature(&first),
              wind_display_config_signature(&second));

    second = first;
    second.use_24_hour = false;
    EXPECT_NE(wind_display_config_signature(&first),
              wind_display_config_signature(&second));

    second = first;
    second.temperature_fahrenheit = true;
    EXPECT_NE(wind_display_config_signature(&first),
              wind_display_config_signature(&second));
}
