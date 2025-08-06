#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_http_server.h"

httpd_handle_t web_server_start(void);
esp_err_t trigger_ws_send(httpd_handle_t handle, httpd_req_t *req);

#endif // WEB_SERVER_H