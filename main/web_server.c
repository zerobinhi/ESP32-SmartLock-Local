#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "app_config.h"

// HTTP请求处理器声明
static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t css_handler(httpd_req_t *req);
static esp_err_t ws_handler(httpd_req_t *req);

httpd_handle_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;     // 增加栈大小
    config.max_open_sockets = 5;  // 增加最大连接数
    config.recv_wait_timeout = 5; // 增加接收超时
    config.send_wait_timeout = 5; // 增加发送超时

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
        ESP_LOGI(TAG, "Web服务器启动成功");
    }
    else
    {
        ESP_LOGE(TAG, "Web服务器启动失败");
    }

    return server;
}

/**
 * 主页面请求处理器
 */
static esp_err_t root_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "收到主页面请求");
    sprintf(response_data, index_html);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, response_data, HTTPD_RESP_USE_STRLEN);
}

/**
 * CSS样式请求处理器
 */
static esp_err_t css_handler(httpd_req_t *req)
{
    struct stat st;
    if (stat(CSS_PATH, &st) != 0)
    {
        ESP_LOGE(TAG, "未找到style.css");
        return httpd_resp_send_404(req);
    }

    FILE *fp = fopen(CSS_PATH, "r");
    if (!fp)
    {
        ESP_LOGE(TAG, "打开style.css失败");
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
            ESP_LOGE(TAG, "发送CSS数据失败");
            return ESP_FAIL;
        }
    }

    fclose(fp);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/**
 * WebSocket请求处理器 - 处理按钮命令并打印提示
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    // 处理握手
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "客户端已连接WebSocket");
        return ESP_OK;
    }

    // 接收WebSocket消息
    char recv_buf[WS_RECV_BUFFER_SIZE];
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = (uint8_t *)recv_buf;

    // 首先接收帧头以获取 payload 长度
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        // 处理连接关闭错误，不显示错误日志
        // if (ret == ESP_ERR_HTTPD_CONN_ABORTED || ret == ESP_ERR_HTTPD_CONN_RESET)
        // {
        //     ESP_LOGI(TAG, "客户端已断开连接");
        //     return ESP_OK;
        // }

        ESP_LOGE(TAG, "接收WebSocket帧头失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 限制最大接收长度
    if (ws_pkt.len > WS_RECV_BUFFER_SIZE - 1)
    {
        ws_pkt.len = WS_RECV_BUFFER_SIZE - 1;
    }

    // 接收实际数据
    if (ws_pkt.len > 0)
    {
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "接收WebSocket数据失败: %s", esp_err_to_name(ret));
            return ret;
        }

        // 添加字符串终止符
        recv_buf[ws_pkt.len] = '\0';
        ESP_LOGI(TAG, "收到客户端命令: %s", recv_buf);

        // 处理命令并打印提示
        if (strcmp(recv_buf, "add_card") == 0)
        {
            ESP_LOGI(TAG, "[操作提示] 用户点击了添加卡片按钮");
        }
        else if (strcmp(recv_buf, "clear_cards") == 0)
        {
            ESP_LOGI(TAG, "[操作提示] 用户点击了清空卡片按钮");
        }
        else if (strcmp(recv_buf, "refresh_cards") == 0)
        {
            ESP_LOGI(TAG, "[操作提示] 用户点击了刷新卡片按钮");
        }
        else if (strcmp(recv_buf, "add_fingerprint") == 0)
        {
            ESP_LOGI(TAG, "[操作提示] 用户点击了添加指纹按钮");
        }
        else if (strcmp(recv_buf, "clear_fingerprints") == 0)
        {
            ESP_LOGI(TAG, "[操作提示] 用户点击了清空指纹按钮");
        }
        else if (strcmp(recv_buf, "refresh_fingerprints") == 0)
        {
            ESP_LOGI(TAG, "[操作提示] 用户点击了刷新指纹按钮");
        }
        else
        {
            ESP_LOGW(TAG, "[操作提示] 收到未知命令");
        }
    }

    return ESP_OK;
}