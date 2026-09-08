#include "wifi_manager.h"

#include <string.h>

#include "config.h"
#include "config_manager.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "installed_configuration.h"
#include "storage.h"
#ifndef CONFIG_BOARD_CAP_WINDPEEK
#include "utils.h"
#endif

static const char *TAG = "wifi_manager";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_INTERACTIVE_CONNECT_TIMEOUT_MS 45000
#define WIFI_REFRESH_CONNECT_TIMEOUT_MS 12000
#define WIFI_INTERACTIVE_MAX_RETRIES 5
#define WIFI_REFRESH_MAX_RETRIES 1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static int s_max_retries = WIFI_INTERACTIVE_MAX_RETRIES;
static bool s_is_connected = false;
static bool s_wifi_started = false;
static bool s_connect_on_start = false;
static esp_netif_t *s_sta_netif = NULL;

static void apply_dns_override(void);

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                          void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_connect_on_start) esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        // Bring up an IPv6 link-local address so mDNS can answer AAAA queries.
        // Without one the responder stays silent on AAAA, and clients resolving
        // <name>.local wait out their full resolver timeout (~5s per request)
        // before falling back to the A record.
        esp_netif_create_ip6_linklocal(s_sta_netif);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < s_max_retries) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        s_is_connected = false;
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        // Applied after the address is up so it overrides DHCP-provided DNS
        // servers too (#43).
        apply_dns_override();
        s_retry_num = 0;
        s_is_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_GOT_IP6) {
        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *) event_data;
        ESP_LOGI(TAG, "got ip6:" IPV6STR, IPV62STR(event->ip6_info.ip));
    }
}

esp_err_t wifi_manager_set_performance_mode(bool enable)
{
    // Modem power save adds ~100ms+ of latency to every round trip, which
    // throttles the web UI hard: bulk transfer speed is roughly one TCP send
    // buffer (~5.7KB) per round trip, i.e. ~45KB/s at 130ms RTT. Full RX
    // (WIFI_PS_NONE) costs ~60-70mA extra while the radio is up, so it is only
    // enabled when someone may actually be using the UI — the policy lives in
    // power_manager's sleep_timer_task.
    static bool applied = false;
    static bool current = false;
    if (applied && current == enable) {
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_set_ps(enable ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
    if (err == ESP_OK) {
        applied = true;
        current = enable;
        ESP_LOGI(TAG, "WiFi power save %s (%s mode)", enable ? "disabled" : "enabled",
                 enable ? "performance" : "power-save");
    }
    return err;
}

esp_err_t wifi_manager_update_hostname(void)
{
    if (!s_sta_netif) {
        return ESP_ERR_INVALID_STATE;
    }

    // DHCP hostname from the device name (CamelCase, shown in router device
    // lists). The router picks it up at the next DHCP negotiation (reconnect).
    char hostname[64];
#ifdef CONFIG_BOARD_CAP_WINDPEEK
    strncpy(hostname, "windpeek", sizeof(hostname));
#else
    sanitize_dhcp_hostname(config_manager_get_device_name(), hostname, sizeof(hostname));
#endif
    esp_err_t err = esp_netif_set_hostname(s_sta_netif, hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set DHCP hostname: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "DHCP hostname set to: %s", hostname);
    return ESP_OK;
}

esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Windpeek is configured over USB and only needs a station interface.
    s_sta_netif = esp_netif_create_default_wifi_sta();
#ifndef CONFIG_BOARD_CAP_WINDPEEK
    esp_netif_create_default_wifi_ap();
#endif

    wifi_manager_update_hostname();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // Candidate credentials from the USB installer must never be persisted by
    // the Wi-Fi driver. Windpeek commits credentials through its own
    // transactional configuration record only after setup succeeds.
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_t instance_got_ip6;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &event_handler, NULL, &instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_GOT_IP6, &event_handler,
                                                        NULL, &instance_got_ip6));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // Don't start WiFi here - let wifi_manager_connect() or wifi_provisioning_start_ap() start it
    // ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_manager_init finished.");

    return ESP_OK;
}

esp_err_t wifi_manager_apply_ip_config(void)
{
    if (!s_sta_netif) {
        return ESP_ERR_INVALID_STATE;
    }

#ifdef CONFIG_BOARD_CAP_WINDPEEK
    esp_netif_dhcpc_start(s_sta_netif);
    return ESP_OK;
#else
    if (config_manager_get_ip_mode() == IP_MODE_STATIC) {
        esp_netif_ip_info_t ip_info = {0};
        if (esp_netif_str_to_ip4(config_manager_get_static_ip(), &ip_info.ip) != ESP_OK ||
            esp_netif_str_to_ip4(config_manager_get_static_netmask(), &ip_info.netmask) != ESP_OK ||
            esp_netif_str_to_ip4(config_manager_get_static_gateway(), &ip_info.gw) != ESP_OK) {
            // Never brick the connection on a malformed config — fall back to
            // DHCP so the frame stays reachable and the user can fix it.
            ESP_LOGE(TAG, "Invalid static IP config, falling back to DHCP");
            esp_netif_dhcpc_start(s_sta_netif);
            return ESP_ERR_INVALID_ARG;
        }

        esp_netif_dhcpc_stop(s_sta_netif);
        esp_netif_set_ip_info(s_sta_netif, &ip_info);
        ESP_LOGI(TAG, "Static IP applied: %s/%s gw %s", config_manager_get_static_ip(),
                 config_manager_get_static_netmask(), config_manager_get_static_gateway());
    } else {
        // Make sure DHCP runs when switching back from a static config.
        // Returns ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED in the normal case.
        esp_netif_dhcpc_start(s_sta_netif);
    }
    return ESP_OK;
#endif
}

// Apply the DNS override (if configured). Called after GOT_IP so it takes
// precedence over DHCP-provided servers in DHCP mode; in static mode it is the
// only DNS source (defaults to the gateway when unset).
static void apply_dns_override(void)
{
#ifdef CONFIG_BOARD_CAP_WINDPEEK
    return;
#else
    const char *dns = config_manager_get_dns_server();
    if ((dns == NULL || dns[0] == '\0') && config_manager_get_ip_mode() == IP_MODE_STATIC) {
        dns = config_manager_get_static_gateway();
    }
    if (dns == NULL || dns[0] == '\0') {
        return;
    }

    esp_netif_dns_info_t dns_info = {0};
    if (esp_netif_str_to_ip4(dns, &dns_info.ip.u_addr.ip4) != ESP_OK) {
        ESP_LOGE(TAG, "Invalid DNS server: %s", dns);
        return;
    }
    dns_info.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns_info);
    ESP_LOGI(TAG, "DNS server set to: %s", dns);
#endif
}

static esp_err_t connect_with_policy(const char *ssid, const char *password,
                                     uint32_t timeout_ms, int max_retries)
{
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "SSID is empty");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_manager_apply_ip_config();

    wifi_config_t wifi_config = {0};
    strncpy((char *) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *) wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode =
        password && password[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    // Provisioning leaves the radio in AP mode. Stop it, explicitly switch to
    // station mode, and return recoverable errors instead of rebooting when a
    // candidate network cannot be applied.
    esp_err_t result = esp_wifi_stop();
    if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGE(TAG, "Failed to stop WiFi: %s", esp_err_to_name(result));
        return result;
    }
    s_wifi_started = false;
    s_connect_on_start = false;
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enter station mode: %s", esp_err_to_name(result));
        return result;
    }
    result = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure station: %s", esp_err_to_name(result));
        return result;
    }

    // Clear stale events before starting: a fast connection must not set the
    // success bit only for this function to erase it immediately afterwards.
    s_retry_num = 0;
    s_max_retries = max_retries;
    s_is_connected = false;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_connect_on_start = true;
    result = esp_wifi_start();
    if (result != ESP_OK) {
        s_connect_on_start = false;
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(result));
        return result;
    }
    s_wifi_started = true;
    result = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // Enable power save at boot/connect
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable WiFi power save: %s", esp_err_to_name(result));
        return result;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        return ESP_FAIL;
    }
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    return connect_with_policy(ssid, password, WIFI_INTERACTIVE_CONNECT_TIMEOUT_MS,
                               WIFI_INTERACTIVE_MAX_RETRIES);
}

esp_err_t wifi_manager_connect_for_refresh(const char *ssid, const char *password)
{
    return connect_with_policy(ssid, password, WIFI_REFRESH_CONNECT_TIMEOUT_MS,
                               WIFI_REFRESH_MAX_RETRIES);
}

esp_err_t wifi_manager_disconnect(void)
{
    s_is_connected = false;
    return esp_wifi_disconnect();
}

bool wifi_manager_is_connected(void)
{
    return s_is_connected;
}

esp_err_t wifi_manager_get_ip(char *ip_str, size_t len)
{
    if (!ip_str || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
#ifdef CONFIG_BOARD_CAP_WINDPEEK
    installed_configuration_t active;
    esp_err_t installed_result = installed_configuration_load(&active);
    if (installed_result != ESP_OK) return installed_result;
    installed_result = installed_configuration_promote_setup(&active, ssid, password);
    if (installed_result != ESP_OK) return installed_result;
    return ESP_OK;
#else
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_WIFI_SSID_KEY, ssid);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_WIFI_PASS_KEY, password);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    return err;
#endif
}

esp_err_t wifi_manager_load_credentials(char *ssid, char *password)
{
    if (installed_configuration_load_credentials(ssid, WIFI_SSID_MAX_LEN,
                                                 password, WIFI_PASS_MAX_LEN) == ESP_OK) {
        return ESP_OK;
    }
#ifdef CONFIG_BOARD_CAP_WINDPEEK
    return ESP_ERR_NOT_FOUND;
#else
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t ssid_len = WIFI_SSID_MAX_LEN;
    err = nvs_get_str(nvs_handle, NVS_WIFI_SSID_KEY, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    size_t pass_len = WIFI_PASS_MAX_LEN;
    err = nvs_get_str(nvs_handle, NVS_WIFI_PASS_KEY, password, &pass_len);
    nvs_close(nvs_handle);

    return err;
#endif
}

EventGroupHandle_t wifi_manager_get_event_group(void)
{
    return s_wifi_event_group;
}

int wifi_manager_scan(wifi_ap_record_t *results, int max_results)
{
    if (!results || max_results <= 0) {
        return 0;
    }

    // A clean E1002 deliberately leaves the station stopped until setup. A
    // blocking scan against that stopped driver can occupy the installer UART
    // until the browser times out, so bring the radio up only for this scan.
    const bool started_for_scan = !s_wifi_started;
    if (started_for_scan) {
        // WIFI_EVENT_STA_START normally begins a requested connection. A scan
        // on an unconfigured device must not race that connection state
        // machine for the radio.
        s_connect_on_start = false;
        esp_err_t start_result = esp_wifi_start();
        if (start_result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start WiFi for scan: %s", esp_err_to_name(start_result));
            return 0;
        }
        s_wifi_started = true;
    }

    // Save current WiFi mode
    wifi_mode_t original_mode;
    esp_err_t err = esp_wifi_get_mode(&original_mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi mode: %s", esp_err_to_name(err));
        if (started_for_scan) {
            (void) esp_wifi_stop();
            s_wifi_started = false;
        }
        return 0;
    }

    // Switch to APSTA mode if currently in AP-only mode
    if (original_mode == WIFI_MODE_AP) {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set APSTA mode: %s", esp_err_to_name(err));
            if (started_for_scan) {
                (void) esp_wifi_stop();
                s_wifi_started = false;
            }
            return 0;
        }
    }

    // Start blocking scan on all channels
    wifi_scan_config_t scan_config = {0};
    err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        if (original_mode == WIFI_MODE_AP) {
            esp_wifi_set_mode(original_mode);
        }
        if (started_for_scan) {
            (void) esp_wifi_stop();
            s_wifi_started = false;
        }
        return 0;
    }

    // Get number of APs found
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        ESP_LOGI(TAG, "No APs found");
        if (original_mode == WIFI_MODE_AP) {
            esp_wifi_set_mode(original_mode);
        }
        if (started_for_scan) {
            (void) esp_wifi_stop();
            s_wifi_started = false;
        }
        return 0;
    }

    // Limit to max_results
    uint16_t fetch_count = (ap_count > (uint16_t) max_results) ? (uint16_t) max_results : ap_count;
    err = esp_wifi_scan_get_ap_records(&fetch_count, results);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(err));
        if (original_mode == WIFI_MODE_AP) {
            esp_wifi_set_mode(original_mode);
        }
        if (started_for_scan) {
            (void) esp_wifi_stop();
            s_wifi_started = false;
        }
        return 0;
    }

    // Restore original WiFi mode
    if (original_mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(original_mode);
    }
    if (started_for_scan) {
        (void) esp_wifi_stop();
        s_wifi_started = false;
    }

    ESP_LOGI(TAG, "WiFi scan found %d APs (returning %d)", ap_count, fetch_count);
    return (int) fetch_count;
}
