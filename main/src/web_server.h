#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <sys/stat.h>
#include <esp_http_server.h>
#include "app_config.h"
#include <cJSON.h>
#include "zw111.h"
#include "nvs_custom.h"

#define CSS_PATH "/spiffs/style.css"
#define FAVICON_PATH "/spiffs/favicon.ico"
#define WS_RECV_BUFFER_SIZE 128
#define MAX_WS_CLIENTS 5

extern struct fingerprint_device zw111; // Fingerprint device instance
extern char g_touch_password[TOUCH_PASSWORD_LEN + 1];             // Current password
extern uint64_t g_card_id_value[MAX_CARDS];
extern int g_card_count;

httpd_handle_t web_server_start(void);

void send_card_list();
void send_fingerprint_list();
void send_status_msg(const char *message);
void send_init_data();
void send_operation_result(const char *message, bool success);

#endif
