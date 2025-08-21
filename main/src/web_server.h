#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <sys/stat.h>
#include "esp_http_server.h"
#include "app_config.h"
#include "cJSON.h"
#include "zw111.h"

#define CSS_PATH "/spiffs/style.css"
#define WS_RECV_BUFFER_SIZE 128
extern struct fingerprint_device zw111; // 指纹设备全局变量

// 卡片数据结构
typedef struct {
    int id;
    char cardNumber[9];  // 8位卡号
} CardInfo;

// 全局数据存储
#define MAX_CARDS 20
 
httpd_handle_t web_server_start(void);
void send_fingerprint_list(httpd_req_t *req);
void send_init_data(httpd_req_t *req);

#endif // WEB_SERVER_H