set(LEGACY_SOURCES
    "album_manager.c"
    "cert_pin.c"
    "color_palette.c"
    "config_manager.c"
    "debug_log.c"
    "display_flow.c"
    "display_manager.c"
    "dns_server.c"
    "ha_integration.c"
    "http_server.c"
    "image_processor.c"
    "main.c"
    "mdns_service.c"
    "ota_manager.c"
    "periodic_tasks.c"
    "power_manager.c"
    "processing_settings.c"
    "splash_screen.c"
    "storage.c"
    "cron.c"
    "utils.c"
    "wifi_manager.c"
    "wifi_provisioning.c"
    "wind_forecast.c"
    "wind_spots.c"
    "installed_configuration.c"
    "wind_usb_protocol.c"
    "wind_installer_service.c"
    "open_meteo_knmi_provider.c"
    "wind_tide.c"
    "open_meteo_marine_provider.c"
    "wind_tide_cache.c"
    "wind_cache.c"
    "wind_schedule.c"
    "wind_font.c"
    "fonts/berkeley_mono_bold_15.c"
    "fonts/berkeley_mono_bold_condensed_12.c"
    "fonts/berkeley_mono_bold_condensed_15.c"
    "fonts/inter_43.c"
    "wind_renderer.c"
    "wind_display_config.c"
    "wind_app.c"
    "wind_analytics.c"
)

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
    "hardware_profile.c"
    "installed_configuration.c"
    "wind_usb_protocol.c"
    "wind_installer_service.c"
    "open_meteo_knmi_provider.c"
    "wind_tide.c"
    "open_meteo_marine_provider.c"
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
    "wind_renderer.c"
    "wind_display_config.c"
    "wind_app.c"
    "wind_analytics.c"
)

set(LEGACY_PRIV_REQUIRES
    app_update
    board_hal
    espressif__qrcode
    epaper
    epaper_src
    esp_app_format
    esp_driver_gpio
    esp_driver_ledc
    esp_driver_uart
    esp_event
    esp_http_client
    esp_http_server
    esp_https_ota
    esp-tls
    esp_netif
    esp_timer
    esp_wifi
    espressif__cjson
    espressif__esp_jpeg
    espressif__libpng
    espressif__zlib
    espressif__mdns
    fatfs
    mbedtls
    memfs
    nvs_flash
    sdcard
    espcoredump
)

set(WINDPEEK_PRIV_REQUIRES
    acetimec
    board_hal
    epaper
    epaper_src
    esp_app_format
    esp_driver_gpio
    esp_driver_uart
    esp_event
    esp_http_client
    esp-tls
    esp_netif
    esp_timer
    esp_wifi
    espressif__cjson
    fatfs
    mbedtls
    memfs
    nvs_flash
    sdcard
    espcoredump
    joltwallet__littlefs
)

if(CONFIG_BOARD_CAP_WINDPEEK)
    set(SOURCES ${WINDPEEK_SOURCES})
    set(PRIV_REQUIRES ${WINDPEEK_PRIV_REQUIRES})
    set(EMBED_FILES)
else()
    set(SOURCES ${LEGACY_SOURCES})
    set(PRIV_REQUIRES ${LEGACY_PRIV_REQUIRES})
    set(EMBED_FILES
        "webapp/index.html" "webapp/icon.svg" "webapp/assets/index.css"
        "webapp/assets/index.js" "webapp/assets/index2.js"
        "webapp/assets/exif-reader.js" "webapp/assets/browser.js"
        "webapp/assets/__vite-browser-external.js" "resources/measurement_sample.jpg"
    )
endif()

set(INCLUDE_DIRS ".")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES ""
    PRIV_REQUIRES ${PRIV_REQUIRES}
    EMBED_FILES ${EMBED_FILES}
)

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

if(CONFIG_BOARD_CAP_WINDPEEK)
    target_include_directories(${COMPONENT_LIB} PRIVATE "../components/acetimec/src")
endif()

if(NOT CONFIG_BOARD_CAP_WINDPEEK)
    # Legacy photo-frame boards retain their generated setup screens.
    target_add_binary_data(${COMPONENT_LIB} "splash_data/splash.epdgz" BINARY)
    target_add_binary_data(${COMPONENT_LIB} "splash_data/setup_complete.epdgz" BINARY)
endif()
