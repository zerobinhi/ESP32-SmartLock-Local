#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include "esp_http_server.h"

// 全局变量声明
extern httpd_handle_t server;
extern const char *TAG;

// 配置定义
#define INDEX_HTML_PATH "/spiffs/index.html"
#define CSS_PATH "/spiffs/style.css"
#define AP_SSID "ESP32-SmartLock"
#define AP_PASS "12345678"
#define AP_CHANNEL 6
#define MAX_STA_CONN 3

// 缓冲区定义
#define INDEX_HTML_BUFFER_SIZE 32768
#define RESPONSE_DATA_BUFFER_SIZE 32768
#define WS_RECV_BUFFER_SIZE 128  // WebSocket接收缓冲区

extern char index_html[INDEX_HTML_BUFFER_SIZE];
extern char response_data[RESPONSE_DATA_BUFFER_SIZE];

#endif // APP_CONFIG_H