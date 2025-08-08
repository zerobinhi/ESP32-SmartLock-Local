#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <sys/stat.h>
#include "esp_http_server.h"
#include "app_config.h"

#define CSS_PATH "/spiffs/style.css"
#define WS_RECV_BUFFER_SIZE 128

httpd_handle_t web_server_start(void);

#endif // WEB_SERVER_H