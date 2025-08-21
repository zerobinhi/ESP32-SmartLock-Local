#include "web_server.h"

char index_html[INDEX_HTML_BUFFER_SIZE];
char response_data[RESPONSE_DATA_BUFFER_SIZE];

static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t css_handler(httpd_req_t *req);
static esp_err_t ws_handler(httpd_req_t *req);
static void send_card_list(httpd_req_t *req);
static void send_status_msg(httpd_req_t *req, const char *message);

CardInfo card_list[MAX_CARDS] = {0};
int card_count = 0;
// 标志位，进行操作的时候指纹模块可能处在关机状态，也可能在验证指纹状态
bool g_readyAddFingerprint = false;
bool g_readyDeleteFingerprint = false;
bool g_readyDeleteAllFingerprint = false;

httpd_handle_t server = NULL;

static const char *TAG = "SmartLock Web Server";

httpd_handle_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;    // 增加栈大小
    config.max_open_sockets = 5; // 增加最大连接数
    config.uri_match_fn = httpd_uri_match_wildcard;

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
    // 对于旧版本ESP-IDF，不需要显式调用httpd_ws_upgrade
    // 只需验证是否为WebSocket连接
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "客户端尝试建立WebSocket连接");
        send_init_data(req);
        return ESP_OK;
    }

    // 接收WebSocket数据
    httpd_ws_frame_t ws_pkt;
    char recv_buf[WS_RECV_BUFFER_SIZE] = {0};
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.payload = (uint8_t *)recv_buf;

    // 先接收帧头
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "接收帧头失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 限制最大长度
    if (ws_pkt.len > WS_RECV_BUFFER_SIZE - 1)
    {
        ws_pkt.len = WS_RECV_BUFFER_SIZE - 1;
        ESP_LOGW(TAG, "数据过长，截断为%d字节", ws_pkt.len);
    }

    // 接收实际数据
    if (ws_pkt.len > 0)
    {
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "接收数据失败: %s", esp_err_to_name(ret));
            return ret;
        }
        recv_buf[ws_pkt.len] = '\0';
        ESP_LOGI(TAG, "收到数据 [长度:%d]: %s", ws_pkt.len, recv_buf);
    }

    // 命令处理逻辑
    if (strcmp(recv_buf, "add_card") == 0)
    {
        ESP_LOGI(TAG, "处理添加卡片命令");
        // 检查是否还有空间
        if (card_count < MAX_CARDS)
        {
            // 模拟添加卡片
            CardInfo new_card = {
                .id = card_count + 1,
                .cardNumber = "1A2B3C4D" // 示例卡号
            };
            card_list[card_count++] = new_card;
            send_card_list(req); // 发送更新后的卡片列表
            send_status_msg(req, "卡片添加成功");
        }
        else
        {
            send_status_msg(req, "卡片数量已达上限");
        }
    }
    else if (strcmp(recv_buf, "add_fingerprint") == 0)
    {
#ifdef DEBUG
        ESP_LOGI(TAG, "处理添加指纹命令");
#endif

        // 检查是否还有空间
        if (zw111.fingerNumber < 100)
        {
            zw111.state = 0x02;    // 设置状态为注册指纹状态
            turn_on_fingerprint(); // 开机！
        }
        else
        {
            send_status_msg(req, "指纹数量已达上限");
        }
    }
    else if (strcmp(recv_buf, "clear_cards") == 0)
    {
        ESP_LOGI(TAG, "处理清空卡片命令");
        card_count = 0;
        memset(card_list, 0, sizeof(card_list));
        send_card_list(req);
        send_status_msg(req, "卡片已清空");
    }
    else if (strcmp(recv_buf, "clear_fingerprints") == 0)
    {
        ESP_LOGI(TAG, "处理清空指纹命令");
    }
    else if (strcmp(recv_buf, "refresh_cards") == 0)
    {
        ESP_LOGI(TAG, "处理刷新卡片命令");
        send_card_list(req);
    }
    else if (strcmp(recv_buf, "refresh_fingerprints") == 0)
    {
        ESP_LOGI(TAG, "处理刷新指纹命令");
        send_fingerprint_list(req);
    }
    else if (strstr(recv_buf, "delete_fingerprint:") != NULL)
    {
        char *prefix = "delete_fingerprint:";
        int fingerprintId = atoi(recv_buf + strlen(prefix));

        ESP_LOGI(TAG, "处理删除指定指纹命令，ID: %d", fingerprintId);

        // delete_char(fingerprintId, 1);

        // send_status_msg(req, "删除指定指纹命令");
    }
    else if (ws_pkt.len > 0)
    {
        ESP_LOGI(TAG, "收到未知命令: %s", recv_buf);
        send_status_msg(req, "未知命令");
    }

    return ESP_OK;
}
/**
 * 向WebSocket客户端发送JSON消息
 */
static esp_err_t ws_send_json(httpd_req_t *req, cJSON *json)
{
    if (!json)
        return ESP_FAIL;

    char *json_str = cJSON_PrintUnformatted(json);
    if (!json_str)
        return ESP_FAIL;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = (uint8_t *)json_str;
    ws_pkt.len = strlen(json_str);
    ESP_LOGI(TAG, "发送的消息为: %s", json_str);
    esp_err_t ret = httpd_ws_send_frame(req, &ws_pkt);
    free(json_str); // 释放JSON字符串内存
    return ret;
}
/**
 * 发送卡片列表
 */
static void send_card_list(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *data_array = cJSON_CreateArray();

    for (int i = 0; i < card_count; i++)
    {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", card_list[i].id);
        cJSON_AddStringToObject(item, "cardNumber", card_list[i].cardNumber);
        cJSON_AddItemToArray(data_array, item);
    }

    cJSON_AddStringToObject(root, "type", "card_list");
    cJSON_AddItemToObject(root, "data", data_array);
    ws_send_json(req, root);
    cJSON_Delete(root);
}

/**
 * 发送指纹列表
 */
void send_fingerprint_list(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *data_array = cJSON_CreateArray();

    for (int i = 0; i < zw111.fingerNumber; i++)
    {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "templateId", (int)zw111.fingerIDArray[i]);
        cJSON_AddItemToArray(data_array, item);
    }

    cJSON_AddStringToObject(root, "type", "fingerprint_list");
    cJSON_AddItemToObject(root, "data", data_array);
    ws_send_json(req, root);
    cJSON_Delete(root);
}

/**
 * 发送状态消息
 */
static void send_status_msg(httpd_req_t *req, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "status");
    cJSON_AddStringToObject(root, "message", message);
    ws_send_json(req, root);
    cJSON_Delete(root);
}

/**
 * 发送初始化数据
 */
void send_init_data(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *cards_array = cJSON_CreateArray();
    cJSON *fingers_array = cJSON_CreateArray();

    // 添加卡片数据
    for (int i = 0; i < card_count; i++)
    {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", card_list[i].id);
        cJSON_AddStringToObject(item, "cardNumber", card_list[i].cardNumber);
        cJSON_AddItemToArray(cards_array, item);
    }

    // 添加指纹数据
    for (int i = 0; i < zw111.fingerNumber; i++)
    {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "templateId", zw111.fingerIDArray[i]);
        cJSON_AddItemToArray(fingers_array, item);
    }

    cJSON_AddStringToObject(root, "type", "init_data");
    cJSON_AddItemToObject(root, "cards", cards_array);
    cJSON_AddItemToObject(root, "fingers", fingers_array);
    ws_send_json(req, root);
    cJSON_Delete(root);
}
