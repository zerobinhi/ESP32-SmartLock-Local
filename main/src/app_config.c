#include "app_config.h"

// 全局变量定义
httpd_handle_t server = NULL;
const char *TAG = "SmartLock Server";

// 缓冲区定义
char index_html[INDEX_HTML_BUFFER_SIZE];
char response_data[RESPONSE_DATA_BUFFER_SIZE];