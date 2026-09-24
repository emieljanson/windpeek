#ifndef CONFIG_H
#define CONFIG_H

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 64
#define TIMEZONE_MAX_LEN 64

#ifndef FS_MOUNT_POINT
#define FS_MOUNT_POINT "/storage"
#endif

#define WIND_DASHBOARD_PREVIEW_PATH FS_MOUNT_POINT "/.wind-dashboard.pbm"

#define AUTO_SLEEP_TIMEOUT_SEC 120
#define EARLY_WAKE_TOLERANCE_SEC 5

/* Keep the original NVS namespace so existing Windpeek devices retain settings. */
#define NVS_NAMESPACE "photoframe"
#define NVS_WIND_CONFIG_VERSION_KEY "wind_cfg_ver"
#define NVS_WIND_DISPLAY_MODE_KEY "wind_mode"
#define NVS_WIND_THRESHOLD_KEY "wind_thresh"
#define NVS_WIND_WEATHER_KEY "wind_weather"
#define NVS_WIND_TEMPERATURE_KEY "wind_temp"
#define NVS_WIND_TIDE_KEY "wind_tide"
#define NVS_WIND_FOOTER_KEY "wind_footer"
#define NVS_WIND_TIME_24_KEY "wind_time24"
#define NVS_WIND_TEMP_F_KEY "wind_temp_f"
#define NVS_DEBUG_LOG_KEY "debug_log"

#endif
