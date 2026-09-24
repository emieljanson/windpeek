# Shared battery-voltage ADC helper used by the ADC-based boards.
set(SRCS "src/battery_adc.c")

set(PRIV_REQS
    charger_driver_sy6974b
    epaper_driver_ed2208_gca
    epaper_driver_it8951
    esp_driver_i2c
    esp_driver_spi
    rtc_driver_pcf8563
    sdcard
    sensor_driver_sht40
)

if(CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1002)
    list(APPEND SRCS "src/driver_seeedstudio_reterminal_e1002.c")
elseif(CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E100X)
    list(APPEND SRCS "src/driver_seeedstudio_reterminal_e100x.c")
elseif(CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003)
    list(APPEND SRCS "src/driver_seeedstudio_reterminal_e1003.c")
endif()

idf_component_register(
    SRCS "${SRCS}"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer driver esp_adc esp_driver_gpio esp_driver_usb_serial_jtag epaper sensor
    PRIV_REQUIRES ${PRIV_REQS}
)
