#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <sys/stat.h>
#include <esp_http_server.h>
#include "app_config.h"
#include <cJSON.h>
#include "zw111.h"

#define CSS_PATH "/spiffs/style.css"
#define FAVICON_PATH "/spiffs/favicon.ico"
#define WS_RECV_BUFFER_SIZE 128
#define MAX_CARDS 20
#define MAX_WS_CLIENTS 5

extern struct fingerprint_device zw111; // 指纹设备全局变量

// 卡片数据结构
typedef struct
{
    int id;
    char cardNumber[9]; // 8位卡号
} CardInfo;

httpd_handle_t web_server_start(void);

// 对外可调用接口
void send_card_list();
void send_fingerprint_list();
void send_status_msg(const char *message);
void send_init_data();
void send_operation_result(const char *message, bool success);

#endif
