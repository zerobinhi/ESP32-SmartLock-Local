#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_http_server.h"

httpd_handle_t web_server_start(void);

#endif // WEB_SERVER_H