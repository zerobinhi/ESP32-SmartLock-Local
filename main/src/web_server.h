#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <sys/stat.h>
#include "esp_http_server.h"
#include "app_config.h"
#include "cJSON.h"

#define CSS_PATH "/spiffs/style.css"
#define WS_RECV_BUFFER_SIZE 128


// 卡片数据结构
typedef struct {
    int id;
    char cardNumber[9];  // 8位卡号
} CardInfo;

// 指纹数据结构
typedef struct {
    int id;
    int templateId;      // 指纹模板ID
} FingerprintInfo;

// 全局数据存储
#define MAX_CARDS 20
#define MAX_FINGERPRINTS 20

httpd_handle_t web_server_start(void);

#endif // WEB_SERVER_H