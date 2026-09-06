#include "installed_configuration.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "wind_renderer.h"
#include "wind_timezone.h"

#define CONFIG_RECORD_MAGIC UINT32_C(0x57434647)

typedef struct {
    uint32_t magic;
    uint8_t committed;
    uint8_t reserved[3];
    uint64_t digest;
    installed_configuration_t config;
    uint8_t has_credentials;
    char ssid[33];
    char password[65];
} configuration_record_t;

typedef struct {
    bool show_threshold;
    uint8_t threshold_kt;
    bool show_weather;
    bool show_temperature;
    bool show_tide;
    bool use_24_hour;
    bool temperature_fahrenheit;
} installed_display_configuration_v2_t;

typedef struct {
    uint32_t version;
    uint32_t generation;
    char board_id[40];
    installed_spot_t spot;
    char forecast_model[32];
    installed_display_configuration_v2_t display;
} installed_configuration_v2_t;

typedef struct {
    uint32_t version;
    uint32_t generation;
    char board_id[40];
    installed_spot_t spot;
    char forecast_model[32];
    installed_display_configuration_t display;
} installed_configuration_v3_t;

typedef struct {
    uint32_t magic;
    uint8_t committed;
    uint8_t reserved[3];
    uint64_t digest;
    installed_configuration_v2_t config;
    uint8_t has_credentials;
    char ssid[33];
    char password[65];
} configuration_record_v2_t;

typedef struct {
    uint32_t magic;
    uint8_t committed;
    uint8_t reserved[3];
    uint64_t digest;
    installed_configuration_v3_t config;
    uint8_t has_credentials;
    char ssid[33];
    char password[65];
} configuration_record_v3_t;

typedef union {
    configuration_record_t current;
    configuration_record_v3_t v3;
    configuration_record_v2_t v2;
} configuration_record_storage_t;

_Static_assert(sizeof(configuration_record_v3_t) == sizeof(configuration_record_v2_t),
               "v2 and v3 configuration records must retain their NVS footprint");

static bool terminated(const char *value, size_t size)
{
    return value && memchr(value, '\0', size) != NULL;
}

static uint64_t fnv1a(const char *value)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (const unsigned char *cursor = (const unsigned char *) value; *cursor; ++cursor) {
        hash ^= *cursor;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

static bool credentials_valid(uint8_t has_credentials, const char *ssid, size_t ssid_size,
                              const char *password, size_t password_size)
{
    return !has_credentials ||
           (terminated(ssid, ssid_size) && ssid[0] != '\0' &&
            terminated(password, password_size));
}

static bool legacy_configuration_core_valid(uint32_t version, uint32_t expected_version,
                                            uint32_t generation, const char *board_id,
                                            const installed_spot_t *spot,
                                            const char *forecast_model, uint8_t threshold_kt)
{
    return version == expected_version && generation > 0 && board_id && spot && forecast_model &&
           terminated(board_id, 40) && strcmp(board_id, WINDSCOUT_BOARD_ID) == 0 &&
           terminated(spot->id, sizeof(spot->id)) && spot->id[0] != '\0' &&
           terminated(spot->display_name, sizeof(spot->display_name)) &&
           spot->display_name[0] != '\0' && spot->latitude >= -90.0 && spot->latitude <= 90.0 &&
           spot->longitude >= -180.0 && spot->longitude <= 180.0 &&
           terminated(spot->timezone, sizeof(spot->timezone)) && spot->timezone[0] != '\0' &&
           terminated(forecast_model, 32) && forecast_model[0] != '\0' && threshold_kt <= 99;
}

static bool configuration_v2_validate(const installed_configuration_v2_t *config)
{
    return config && legacy_configuration_core_valid(
        config->version, 2u, config->generation, config->board_id, &config->spot,
        config->forecast_model, config->display.threshold_kt);
}

static uint64_t configuration_v2_digest(const installed_configuration_v2_t *config)
{
    if (!configuration_v2_validate(config)) return 0;
    char canonical[512];
    int length = snprintf(
        canonical, sizeof(canonical),
        "%" PRIu32 "|%s|%s|%s|%.6f|%.6f|%s|%s|%u|%u|%u|%u|%u|%s|%s",
        config->version, config->board_id, config->spot.id, config->spot.display_name,
        config->spot.latitude, config->spot.longitude, config->spot.timezone,
        config->forecast_model, config->display.show_threshold ? 1u : 0u,
        config->display.threshold_kt, config->display.show_weather ? 1u : 0u,
        config->display.show_temperature ? 1u : 0u, config->display.show_tide ? 1u : 0u,
        config->display.use_24_hour ? "24-hour" : "12-hour",
        config->display.temperature_fahrenheit ? "fahrenheit" : "celsius");
    return length > 0 && (size_t) length < sizeof(canonical) ? fnv1a(canonical) : 0;
}

static bool configuration_v3_validate(const installed_configuration_v3_t *config)
{
    return config && legacy_configuration_core_valid(
        config->version, 3u, config->generation, config->board_id, &config->spot,
        config->forecast_model, config->display.threshold_kt);
}

static uint64_t configuration_v3_digest(const installed_configuration_v3_t *config)
{
    if (!configuration_v3_validate(config)) return 0;
    char canonical[512];
    int length = snprintf(
        canonical, sizeof(canonical),
        "%" PRIu32 "|%s|%s|%s|%.6f|%.6f|%s|%s|%u|%u|%u|%u|%u|%u|%s|%s",
        config->version, config->board_id, config->spot.id, config->spot.display_name,
        config->spot.latitude, config->spot.longitude, config->spot.timezone,
        config->forecast_model, config->display.show_threshold ? 1u : 0u,
        config->display.threshold_kt, config->display.show_weather ? 1u : 0u,
        config->display.show_temperature ? 1u : 0u, config->display.show_tide ? 1u : 0u,
        config->display.show_dedicated_footer ? 1u : 0u,
        config->display.use_24_hour ? "24-hour" : "12-hour",
        config->display.temperature_fahrenheit ? "fahrenheit" : "celsius");
    return length > 0 && (size_t) length < sizeof(canonical) ? fnv1a(canonical) : 0;
}

void installed_configuration_default(installed_configuration_t *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->version = INSTALLED_CONFIGURATION_VERSION;
    config->generation = 1;
    snprintf(config->board_id, sizeof(config->board_id), "%s", WINDSCOUT_BOARD_ID);
    snprintf(config->device_timezone, sizeof(config->device_timezone), "Europe/Amsterdam");
    snprintf(config->spot.id, sizeof(config->spot.id), "brouwersdam");
    snprintf(config->spot.display_name, sizeof(config->spot.display_name), "Brouwersdam");
    config->spot.latitude = 51.7506;
    config->spot.longitude = 3.8577;
    snprintf(config->spot.timezone, sizeof(config->spot.timezone), "Europe/Amsterdam");
    snprintf(config->forecast_model, sizeof(config->forecast_model), "best_match");
    config->display.show_threshold = false;
    config->display.threshold_kt = WIND_RENDERER_DEFAULT_THRESHOLD_KT;
    config->display.show_weather = true;
    config->display.show_temperature = false;
    config->display.show_tide = false;
    config->display.show_dedicated_footer = false;
    config->display.use_24_hour = true;
    config->display.temperature_fahrenheit = false;
}

bool installed_configuration_validate(const installed_configuration_t *config)
{
    if (!config || config->version != INSTALLED_CONFIGURATION_VERSION || config->generation == 0 ||
        !terminated(config->board_id, sizeof(config->board_id)) ||
        strcmp(config->board_id, WINDSCOUT_BOARD_ID) != 0 ||
        !terminated(config->device_timezone, sizeof(config->device_timezone)) ||
        config->device_timezone[0] == '\0' || !wind_timezone_is_supported(config->device_timezone) ||
        !terminated(config->spot.id, sizeof(config->spot.id)) || config->spot.id[0] == '\0' ||
        !terminated(config->spot.display_name, sizeof(config->spot.display_name)) ||
        config->spot.display_name[0] == '\0' ||
        config->spot.latitude < -90.0 || config->spot.latitude > 90.0 ||
        config->spot.longitude < -180.0 || config->spot.longitude > 180.0 ||
        !terminated(config->spot.timezone, sizeof(config->spot.timezone)) ||
        config->spot.timezone[0] == '\0' || !wind_timezone_is_supported(config->spot.timezone) ||
        !terminated(config->forecast_model, sizeof(config->forecast_model)) ||
        config->forecast_model[0] == '\0' || config->display.threshold_kt > 99) {
        return false;
    }
    return true;
}

static uint64_t configuration_digest_unchecked(const installed_configuration_t *config)
{
    char canonical[512];
    int length = snprintf(
        canonical, sizeof(canonical), "%" PRIu32 "|%s|%s|%s|%s|%.6f|%.6f|%s|%s|%u|%u|%u|%u|%u|%u|%s|%s",
        config->version, config->board_id, config->device_timezone, config->spot.id,
        config->spot.display_name,
        config->spot.latitude, config->spot.longitude, config->spot.timezone,
        config->forecast_model, config->display.show_threshold ? 1u : 0u,
        config->display.threshold_kt, config->display.show_weather ? 1u : 0u,
        config->display.show_temperature ? 1u : 0u, config->display.show_tide ? 1u : 0u,
        config->display.show_dedicated_footer ? 1u : 0u,
        config->display.use_24_hour ? "24-hour" : "12-hour",
        config->display.temperature_fahrenheit ? "fahrenheit" : "celsius");
    return length > 0 && (size_t) length < sizeof(canonical) ? fnv1a(canonical) : 0;
}

uint64_t installed_configuration_digest(const installed_configuration_t *config)
{
    return installed_configuration_validate(config) ? configuration_digest_unchecked(config) : 0;
}

static bool record_valid(const configuration_record_t *record)
{
    return record && record->magic == CONFIG_RECORD_MAGIC && record->committed == 1 &&
           credentials_valid(record->has_credentials, record->ssid, sizeof(record->ssid),
                             record->password, sizeof(record->password)) &&
           installed_configuration_validate(&record->config) &&
           record->digest == configuration_digest_unchecked(&record->config);
}

static void initialize_migrated_record(configuration_record_t *migrated, uint32_t generation,
                                       const char *board_id, const installed_spot_t *spot,
                                       const char *forecast_model, uint8_t has_credentials,
                                       const char *ssid, const char *password)
{
    memset(migrated, 0, sizeof(*migrated));
    migrated->magic = CONFIG_RECORD_MAGIC;
    migrated->committed = 1;
    installed_configuration_default(&migrated->config);
    migrated->config.generation = generation;
    memcpy(migrated->config.board_id, board_id, sizeof(migrated->config.board_id));
    migrated->config.spot = *spot;
    memcpy(migrated->config.device_timezone, spot->timezone,
           sizeof(migrated->config.device_timezone));
    memcpy(migrated->config.forecast_model, forecast_model,
           sizeof(migrated->config.forecast_model));
    migrated->has_credentials = has_credentials;
    memcpy(migrated->ssid, ssid, sizeof(migrated->ssid));
    memcpy(migrated->password, password, sizeof(migrated->password));
}

static bool migrate_v2_record(const configuration_record_v2_t *legacy,
                              configuration_record_t *migrated)
{
    if (!legacy || !migrated || legacy->magic != CONFIG_RECORD_MAGIC || legacy->committed != 1 ||
        !credentials_valid(legacy->has_credentials, legacy->ssid, sizeof(legacy->ssid),
                           legacy->password, sizeof(legacy->password)) ||
        !configuration_v2_validate(&legacy->config) ||
        legacy->digest != configuration_v2_digest(&legacy->config)) return false;

    initialize_migrated_record(migrated, legacy->config.generation, legacy->config.board_id,
                               &legacy->config.spot, legacy->config.forecast_model,
                               legacy->has_credentials, legacy->ssid, legacy->password);
    migrated->config.display.show_threshold = legacy->config.display.show_threshold;
    migrated->config.display.threshold_kt = legacy->config.display.threshold_kt;
    migrated->config.display.show_weather = legacy->config.display.show_weather;
    migrated->config.display.show_temperature = legacy->config.display.show_temperature;
    migrated->config.display.show_tide = legacy->config.display.show_tide;
    migrated->config.display.use_24_hour = legacy->config.display.use_24_hour;
    migrated->config.display.temperature_fahrenheit =
        legacy->config.display.temperature_fahrenheit;
    migrated->digest = configuration_digest_unchecked(&migrated->config);
    return record_valid(migrated);
}

static bool migrate_v3_record(const configuration_record_v3_t *legacy,
                              configuration_record_t *migrated)
{
    if (!legacy || !migrated || legacy->magic != CONFIG_RECORD_MAGIC || legacy->committed != 1 ||
        !credentials_valid(legacy->has_credentials, legacy->ssid, sizeof(legacy->ssid),
                           legacy->password, sizeof(legacy->password)) ||
        !configuration_v3_validate(&legacy->config) ||
        legacy->digest != configuration_v3_digest(&legacy->config)) return false;

    initialize_migrated_record(migrated, legacy->config.generation, legacy->config.board_id,
                               &legacy->config.spot, legacy->config.forecast_model,
                               legacy->has_credentials, legacy->ssid, legacy->password);
    migrated->config.display = legacy->config.display;
    migrated->digest = configuration_digest_unchecked(&migrated->config);
    return record_valid(migrated);
}

static bool decode_record(const configuration_record_storage_t *stored, size_t stored_size,
                          configuration_record_t *decoded, bool *was_migrated)
{
    if (!stored || !decoded) return false;
    if (stored_size == sizeof(stored->current) && record_valid(&stored->current)) {
        *decoded = stored->current;
        if (was_migrated) *was_migrated = false;
        return true;
    }
    if (stored_size != sizeof(stored->v3) ||
        (!migrate_v3_record(&stored->v3, decoded) &&
         !migrate_v2_record(&stored->v2, decoded))) return false;
    if (was_migrated) *was_migrated = true;
    return true;
}

#ifdef ESP_PLATFORM
#include "nvs.h"

#define CONFIG_NAMESPACE "wind_install"
#define ACTIVE_KEY "active"
#define CANDIDATE_KEY "candidate"

static esp_err_t read_record(nvs_handle_t handle, const char *key, configuration_record_t *record,
                             bool *was_migrated)
{
    configuration_record_storage_t stored;
    memset(&stored, 0, sizeof(stored));
    size_t size = sizeof(stored);
    esp_err_t result = nvs_get_blob(handle, key, &stored, &size);
    return result == ESP_OK && decode_record(&stored, size, record, was_migrated)
               ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t installed_configuration_load(installed_configuration_t *out_config)
{
    if (!out_config) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    configuration_record_t record;
    bool was_migrated = false;
    esp_err_t result = nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (result == ESP_OK) {
        result = read_record(handle, ACTIVE_KEY, &record, &was_migrated);
        nvs_close(handle);
    }
    if (result == ESP_OK) {
        *out_config = record.config;
        if (was_migrated) {
            result = installed_configuration_promote_setup(
                &record.config, record.has_credentials ? record.ssid : NULL,
                record.has_credentials ? record.password : NULL);
            if (result != ESP_OK) return result;
        }
        return ESP_OK;
    }
    installed_configuration_default(out_config);
    nvs_handle_t legacy_handle;
    if (nvs_open("wind", NVS_READONLY, &legacy_handle) == ESP_OK) {
        char legacy_spot[64] = {0};
        size_t legacy_size = sizeof(legacy_spot);
        if (nvs_get_str(legacy_handle, "spot", legacy_spot, &legacy_size) == ESP_OK) {
            if (strcmp(legacy_spot, "edam") == 0) {
                snprintf(out_config->spot.id, sizeof(out_config->spot.id), "edam");
                snprintf(out_config->spot.display_name, sizeof(out_config->spot.display_name), "Edam");
                out_config->spot.latitude = 52.5126;
                out_config->spot.longitude = 5.0486;
            } else if (strcmp(legacy_spot, "castricum-aan-zee") == 0) {
                snprintf(out_config->spot.id, sizeof(out_config->spot.id), "castricum-aan-zee");
                snprintf(out_config->spot.display_name, sizeof(out_config->spot.display_name),
                         "Castricum aan Zee");
                out_config->spot.latitude = 52.5550;
                out_config->spot.longitude = 4.6090;
            }
        }
        nvs_close(legacy_handle);
    }
    return ESP_OK;
}

esp_err_t installed_configuration_promote_setup(const installed_configuration_t *candidate,
                                                 const char *ssid, const char *password)
{
    if (!installed_configuration_validate(candidate) || (ssid && strlen(ssid) > 32) ||
        (password && strlen(password) > 64)) return ESP_ERR_INVALID_ARG;
    configuration_record_t record = {
        .magic = CONFIG_RECORD_MAGIC,
        .committed = 0,
        .digest = configuration_digest_unchecked(candidate),
        .config = *candidate,
    };
    nvs_handle_t handle;
    esp_err_t result = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    configuration_record_t previous;
    if (ssid) {
        record.has_credentials = 1;
        snprintf(record.ssid, sizeof(record.ssid), "%s", ssid);
        snprintf(record.password, sizeof(record.password), "%s", password ? password : "");
    } else if (read_record(handle, ACTIVE_KEY, &previous, NULL) == ESP_OK &&
               previous.has_credentials) {
        record.has_credentials = 1;
        memcpy(record.ssid, previous.ssid, sizeof(record.ssid));
        memcpy(record.password, previous.password, sizeof(record.password));
    }
    result = nvs_set_blob(handle, CANDIDATE_KEY, &record, sizeof(record));
    if (result == ESP_OK) result = nvs_commit(handle);
    record.committed = 1;
    if (result == ESP_OK) result = nvs_set_blob(handle, CANDIDATE_KEY, &record, sizeof(record));
    if (result == ESP_OK) result = nvs_commit(handle);
    if (result == ESP_OK) result = nvs_set_blob(handle, ACTIVE_KEY, &record, sizeof(record));
    if (result == ESP_OK) result = nvs_commit(handle);
    if (result == ESP_OK) {
        nvs_erase_key(handle, CANDIDATE_KEY);
        nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

esp_err_t installed_configuration_promote(const installed_configuration_t *candidate)
{
    return installed_configuration_promote_setup(candidate, NULL, NULL);
}

esp_err_t installed_configuration_load_credentials(char *ssid, size_t ssid_size,
                                                    char *password, size_t password_size)
{
    if (!ssid || ssid_size == 0 || !password || password_size == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    configuration_record_t record;
    esp_err_t result = nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (result != ESP_OK) return result;
    result = read_record(handle, ACTIVE_KEY, &record, NULL);
    nvs_close(handle);
    if (result != ESP_OK || !record.has_credentials || strlen(record.ssid) >= ssid_size ||
        strlen(record.password) >= password_size) return ESP_ERR_NOT_FOUND;
    snprintf(ssid, ssid_size, "%s", record.ssid);
    snprintf(password, password_size, "%s", record.password);
    return ESP_OK;
}
#else
static configuration_record_storage_t s_active;
static size_t s_active_size;
static int s_failure_boundary = -1;

void installed_configuration_reset_host_storage(void)
{
    memset(&s_active, 0, sizeof(s_active));
    s_active_size = 0;
    s_failure_boundary = -1;
}

void installed_configuration_set_host_failure_boundary(int boundary)
{
    s_failure_boundary = boundary;
}

esp_err_t installed_configuration_load(installed_configuration_t *out_config)
{
    if (!out_config) return ESP_ERR_INVALID_ARG;
    configuration_record_t decoded;
    bool was_migrated = false;
    if (decode_record(&s_active, s_active_size, &decoded, &was_migrated)) {
        *out_config = decoded.config;
        if (was_migrated) {
            s_active.current = decoded;
            s_active_size = sizeof(s_active.current);
        }
    } else {
        installed_configuration_default(out_config);
    }
    return ESP_OK;
}

esp_err_t installed_configuration_promote_setup(const installed_configuration_t *candidate,
                                                 const char *ssid, const char *password)
{
    if (!installed_configuration_validate(candidate) || (ssid && strlen(ssid) > 32) ||
        (password && strlen(password) > 64)) return ESP_ERR_INVALID_ARG;
    configuration_record_t staged = {
        .magic = CONFIG_RECORD_MAGIC,
        .committed = 0,
        .digest = configuration_digest_unchecked(candidate),
        .config = *candidate,
    };
    if (ssid) {
        staged.has_credentials = 1;
        snprintf(staged.ssid, sizeof(staged.ssid), "%s", ssid);
        snprintf(staged.password, sizeof(staged.password), "%s", password ? password : "");
    } else {
        configuration_record_t previous;
        const bool have_previous = decode_record(&s_active, s_active_size, &previous, NULL);
        if (have_previous && previous.has_credentials) {
            staged.has_credentials = 1;
            memcpy(staged.ssid, previous.ssid, sizeof(staged.ssid));
            memcpy(staged.password, previous.password, sizeof(staged.password));
        }
    }
    if (s_failure_boundary == 0) return ESP_FAIL;
    if (s_failure_boundary == 1) return ESP_FAIL;
    staged.committed = 1;
    if (s_failure_boundary == 2) return ESP_FAIL;
    s_active.current = staged;
    s_active_size = sizeof(s_active.current);
    return ESP_OK;
}

esp_err_t installed_configuration_promote(const installed_configuration_t *candidate)
{
    return installed_configuration_promote_setup(candidate, NULL, NULL);
}

esp_err_t installed_configuration_load_credentials(char *ssid, size_t ssid_size,
                                                    char *password, size_t password_size)
{
    configuration_record_t record;
    if (!ssid || ssid_size == 0 || !password || password_size == 0 ||
        !decode_record(&s_active, s_active_size, &record, NULL) || !record.has_credentials ||
        strlen(record.ssid) >= ssid_size || strlen(record.password) >= password_size) {
        return ESP_ERR_NOT_FOUND;
    }
    snprintf(ssid, ssid_size, "%s", record.ssid);
    snprintf(password, password_size, "%s", record.password);
    return ESP_OK;
}

void installed_configuration_seed_v2_host_storage(const installed_configuration_t *config,
                                                   const char *ssid, const char *password)
{
    if (!config) return;
    memset(&s_active, 0, sizeof(s_active));
    s_active_size = sizeof(s_active.v2);
    configuration_record_v2_t *record = &s_active.v2;
    record->magic = CONFIG_RECORD_MAGIC;
    record->committed = 1;
    record->config.version = 2;
    record->config.generation = config->generation;
    memcpy(record->config.board_id, config->board_id, sizeof(record->config.board_id));
    record->config.spot = config->spot;
    memcpy(record->config.forecast_model, config->forecast_model,
           sizeof(record->config.forecast_model));
    record->config.display.show_threshold = config->display.show_threshold;
    record->config.display.threshold_kt = config->display.threshold_kt;
    record->config.display.show_weather = config->display.show_weather;
    record->config.display.show_temperature = config->display.show_temperature;
    record->config.display.show_tide = config->display.show_tide;
    record->config.display.use_24_hour = config->display.use_24_hour;
    record->config.display.temperature_fahrenheit = config->display.temperature_fahrenheit;
    if (ssid) {
        record->has_credentials = 1;
        snprintf(record->ssid, sizeof(record->ssid), "%s", ssid);
        snprintf(record->password, sizeof(record->password), "%s", password ? password : "");
    }
    record->digest = configuration_v2_digest(&record->config);
}

void installed_configuration_seed_v3_host_storage(const installed_configuration_t *config,
                                                   const char *ssid, const char *password)
{
    if (!config) return;
    memset(&s_active, 0, sizeof(s_active));
    s_active_size = sizeof(s_active.v3);
    configuration_record_v3_t *record = &s_active.v3;
    record->magic = CONFIG_RECORD_MAGIC;
    record->committed = 1;
    record->config.version = 3;
    record->config.generation = config->generation;
    memcpy(record->config.board_id, config->board_id, sizeof(record->config.board_id));
    record->config.spot = config->spot;
    memcpy(record->config.forecast_model, config->forecast_model,
           sizeof(record->config.forecast_model));
    record->config.display = config->display;
    if (ssid) {
        record->has_credentials = 1;
        snprintf(record->ssid, sizeof(record->ssid), "%s", ssid);
        snprintf(record->password, sizeof(record->password), "%s", password ? password : "");
    }
    record->digest = configuration_v3_digest(&record->config);
}
#endif
