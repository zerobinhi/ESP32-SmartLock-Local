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
    xTaskCreate(dns_server_task, "dns_server_task", 4096, NULL, 5, NULL);
}

void dns_server_task(void *pv)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DNS_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    uint8_t buf[512];
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    ESP_LOGI("DNS", "DNS 拦截服务已启动");

    while (1)
    {
        int len = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&source_addr, &socklen);
        if (len > 0)
        {
            buf[2] |= 0x80; // 响应标志
            buf[3] |= 0x80; // recursion available
            buf[7] = 1;     // Answer count = 1
            // 回应一个 A 记录
            buf[len++] = 0xC0;
            buf[len++] = 0x0C; // 指针
            buf[len++] = 0x00;
            buf[len++] = 0x01; // Type A
            buf[len++] = 0x00;
            buf[len++] = 0x01; // Class IN
            buf[len++] = 0x00;
            buf[len++] = 0x00;
            buf[len++] = 0x00;
            buf[len++] = 0x3C; // TTL
            buf[len++] = 0x00;
            buf[len++] = 0x04; // Data length
            buf[len++] = 192;
            buf[len++] = 168;
            buf[len++] = 4;
            buf[len++] = 1; // 192.168.4.1
            sendto(sock, buf, len, 0, (struct sockaddr *)&source_addr, socklen);
        }
    }
}