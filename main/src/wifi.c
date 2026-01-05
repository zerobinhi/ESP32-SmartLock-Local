#include "wifi.h"

static const char *TAG = "wifi";

char g_ap_ssid[32] = {0};
char g_ap_pass[64] = {0};

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Device " MACSTR " connected, AID=%d", MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Device " MACSTR " disconnected, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    size_t ssid_len = sizeof(g_ap_ssid);
    size_t pass_len = sizeof(g_ap_pass);

    // Read WiFi configuration from NVS (use default values if not exist)
    if (nvs_custom_get_str(NULL, "wifi", "wifi_ssid", g_ap_ssid, &ssid_len) != ESP_OK)
    {
        strncpy(g_ap_ssid, DEFAULT_AP_SSID, sizeof(g_ap_ssid));
        nvs_custom_set_str(NULL, "wifi", "wifi_ssid", g_ap_ssid);
    }

    if (nvs_custom_get_str(NULL, "wifi", "wifi_pass", g_ap_pass, &pass_len) != ESP_OK)
    {
        strncpy(g_ap_pass, DEFAULT_AP_PASS, sizeof(g_ap_pass));
        nvs_custom_set_str(NULL, "wifi", "wifi_pass", g_ap_pass);
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(g_ap_ssid),
            .channel = AP_CHANNEL,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        }};

    strncpy((char *)wifi_config.ap.ssid, g_ap_ssid, sizeof(wifi_config.ap.ssid));
    strncpy((char *)wifi_config.ap.password, g_ap_pass, sizeof(wifi_config.ap.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP initialization completed. SSID:%s Password:%s Channel:%d", g_ap_ssid, g_ap_pass, AP_CHANNEL);

    // Get the IP of the access point to redirect to
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
    ESP_LOGI(TAG, "Set up softAP with IP: %s", ip_addr);

    // Turn the IP into a URI
    char *captiveportal_uri = (char *)malloc(32 * sizeof(char));
    assert(captiveportal_uri && "Failed to allocate captiveportal_uri");
    strcpy(captiveportal_uri, "http://");
    strcat(captiveportal_uri, ip_addr);

    // Get a handle to configure DHCP with
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

    // Set the DHCP option 114
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(netif));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, captiveportal_uri, strlen(captiveportal_uri)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(netif));

    ESP_LOGI(TAG, "DHCP Captive Portal URI set to %s", captiveportal_uri);

    // ---------- Start DNS intercept service ----------
    dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    start_dns_server(&dns_config);
    ESP_LOGI(TAG, "DNS Captive Portal server started (redirect all domains)");
}
