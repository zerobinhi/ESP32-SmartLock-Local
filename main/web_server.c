#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "app_config.h"

// HTTP请求处理器声明
static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t css_handler(httpd_req_t *req);
static esp_err_t ws_handler(httpd_req_t *req);

// WebSocket辅助函数
static void ws_send_state(void *arg);

httpd_handle_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192; // 增加栈大小以处理较大的HTML文件

    // 注册URI处理器
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL};

    httpd_uri_t css_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = css_handler,
        .user_ctx = NULL};

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true};

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &css_uri);
        httpd_register_uri_handler(server, &ws_uri);
    }

    return server;
}

/**
 * HTTP GET请求处理器 - 处理主页面请求
 */
static esp_err_t root_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Received GET request for main page");
    sprintf(response_data, index_html);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, response_data, HTTPD_RESP_USE_STRLEN);
}

/**
 * CSS请求处理器 - 处理样式表请求
 */
static esp_err_t css_handler(httpd_req_t *req)
{
    struct stat st;
    if (stat(CSS_PATH, &st) != 0)
    {
        ESP_LOGE(TAG, "style.css not found in SPIFFS");
        return httpd_resp_send_404(req);
    }

    FILE *fp = fopen(CSS_PATH, "r");
    if (!fp)
    {
        ESP_LOGE(TAG, "Failed to open style.css");
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "text/css");
    char buffer[1024];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        if (httpd_resp_send_chunk(req, buffer, bytes_read) != ESP_OK)
        {
            fclose(fp);
            ESP_LOGE(TAG, "Failed to send CSS chunk");
            return ESP_FAIL;
        }
    }

    fclose(fp);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/**
 * WebSocket消息发送函数
 */
static void ws_send_state(void *arg)
{
    struct async_resp_arg *resp_arg = arg;
    if (!resp_arg)
        return;

    // 准备发送的锁状态数据
    char buff[32];
    snprintf(buff, sizeof(buff), "{\"lock_state\": %d}", lock_state);

    httpd_ws_frame_t ws_pkt = {
        .payload = (uint8_t *)buff,
        .len = strlen(buff),
        .type = HTTPD_WS_TYPE_TEXT};

    // 向所有连接的WebSocket客户端发送状态
    size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    size_t fds = max_clients;
    int client_fds[max_clients];

    if (httpd_get_client_list(server, &fds, client_fds) == ESP_OK)
    {
        for (int i = 0; i < fds; i++)
        {
            if (httpd_ws_get_fd_info(server, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET)
            {
                httpd_ws_send_frame_async(resp_arg->hd, client_fds[i], &ws_pkt);
            }
        }
    }

    free(resp_arg);
}

/**
 * 触发WebSocket状态发送
 */
esp_err_t trigger_ws_send(httpd_handle_t handle, httpd_req_t *req)
{
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    if (!resp_arg)
        return ESP_ERR_NO_MEM;

    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    return httpd_queue_work(handle, ws_send_state, resp_arg);
}

/**
 * WebSocket请求处理器
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "WebSocket handshake completed");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = {.type = HTTPD_WS_TYPE_TEXT};
    uint8_t *buf = NULL;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to receive WS frame: %d", ret);
        return ret;
    }

    if (ws_pkt.len)
    {
        buf = calloc(1, ws_pkt.len + 1);
        if (!buf)
        {
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Received WS data: %s", (char *)buf);

            // 处理锁状态控制命令
            if (strcmp((char *)buf, "unlock") == 0)
            {
                lock_state = 1;
                trigger_ws_send(req->handle, req);
            }
            else if (strcmp((char *)buf, "lock") == 0)
            {
                lock_state = 0;
                trigger_ws_send(req->handle, req);
            }
        }
        free(buf);
    }

    return ESP_OK;
}