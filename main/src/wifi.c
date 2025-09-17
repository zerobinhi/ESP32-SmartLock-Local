#include "wifi.h"

static const char *TAG = "SmartLock WiFi";

char AP_SSID[32];
char AP_PASS[64];

/**
 * @brief WiFi事件处理器
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "设备 " MACSTR " 已连接, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "设备 " MACSTR " 已断开, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

/**
 * @brief 初始化WiFi为SoftAP模式
 */
void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    size_t ssid_len = sizeof(AP_SSID);
    size_t pass_len = sizeof(AP_PASS);
    nvs_custom_get_str(NULL, "wifi", "wifi_ssid", AP_SSID, &ssid_len);
    nvs_custom_get_str(NULL, "wifi", "wifi_pass", AP_PASS, &pass_len);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK},
    };
    strncpy((char *)wifi_config.ap.ssid, AP_SSID, sizeof(wifi_config.ap.ssid));
    strncpy((char *)wifi_config.ap.password, AP_PASS, sizeof(wifi_config.ap.password));
    if (strlen(AP_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi AP初始化完成. SSID:%s 密码:%s 信道:%d",
             AP_SSID, AP_PASS, AP_CHANNEL);
}
