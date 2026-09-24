set(WINDPEEK_SOURCES
    "config_manager.c"
    "cron.c"
    "debug_log.c"
    "windpeek_display_manager.c"
    "windpeek_main.c"
    "power_manager.c"
    "storage.c"
    "wifi_manager.c"
    "wind_forecast.c"
    "wind_spots.c"
    "wind_overview.c"
    "hardware_profile.c"
    "installed_configuration.c"
    "wind_usb_protocol.c"
    "wind_installer_service.c"
    "wind_installer_device.c"
    "open_meteo_knmi_provider.c"
    "wind_tide.c"
    "open_meteo_marine_provider.c"
    "wind_swell.c"
    "wind_swell_cache.c"
    "wind_tide_cache.c"
    "wind_cache.c"
    "wind_schedule.c"
    "wind_clock.c"
    "wind_timezone.c"
    "wind_font.c"
    "fonts/berkeley_mono_bold_15.c"
    "fonts/berkeley_mono_bold_condensed_12.c"
    "fonts/berkeley_mono_bold_condensed_15.c"
    "fonts/inter_43.c"
    "fonts/berkeley_mono_bold_35.c"
    "fonts/berkeley_mono_bold_80.c"
    "fonts/berkeley_mono_bold_condensed_28.c"
    "fonts/berkeley_mono_bold_condensed_35.c"
    "fonts/inter_101.c"
    "fonts/inter_70.c"
    "wind_renderer.c"
    "wind_renderer_native.c"
    "wind_renderer_canvas.c"
    "wind_renderer_output.c"
    "wind_renderer_input.c"
    "wind_display_config.c"
    "wind_app.c"
    "wind_dashboard_data.c"
    "wind_app_status.c"
    "wind_app_cycle.c"
    "wind_analytics.c"
)

if(CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003)
    list(APPEND WINDPEEK_SOURCES "wind_quick_cache.c" "wind_app_prefetch.c")
endif()

set(WINDPEEK_PRIV_REQUIRES
    acetimec
    board_hal
    epaper
    epaper_src
    esp_app_format
    esp_driver_gpio
    esp_driver_ledc
    esp_driver_uart
    esp_event
    esp_http_client
    esp-tls
    esp_netif
    esp_timer
    esp_wifi
    espressif__cjson
    espressif__zlib
    fatfs
    mbedtls
    memfs
    nvs_flash
    sdcard
    espcoredump
    joltwallet__littlefs
)

idf_component_register(
    SRCS ${WINDPEEK_SOURCES}
    INCLUDE_DIRS "."
    PRIV_REQUIRES ${WINDPEEK_PRIV_REQUIRES}
)

target_include_directories(${COMPONENT_LIB} PRIVATE "../components/acetimec/src")

set(WINDPEEK_POSTHOG_PROJECT_TOKEN "$ENV{WINDPEEK_POSTHOG_PROJECT_TOKEN}")
if(WINDPEEK_POSTHOG_PROJECT_TOKEN AND
   NOT WINDPEEK_POSTHOG_PROJECT_TOKEN MATCHES "^[A-Za-z0-9_]+$")
    message(FATAL_ERROR "WINDPEEK_POSTHOG_PROJECT_TOKEN contains unsupported characters")
endif()
if(WINDPEEK_POSTHOG_PROJECT_TOKEN)
    target_compile_definitions(
        ${COMPONENT_LIB}
        PRIVATE WINDPEEK_POSTHOG_PROJECT_TOKEN="${WINDPEEK_POSTHOG_PROJECT_TOKEN}"
    )
endif()

set_source_files_properties(wind_installer_service.c installed_configuration.c
    PROPERTIES COMPILE_OPTIONS "-fstack-usage")
