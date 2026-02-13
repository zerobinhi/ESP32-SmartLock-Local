#include "web_server.h"

char *index_html;

static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t css_handler(httpd_req_t *req);
static esp_err_t ws_handler(httpd_req_t *req);
static esp_err_t favicon_handler(httpd_req_t *req);

// Flag bits
bool g_ready_add_fingerprint = false;
bool g_cancel_add_fingerprint = false;
bool g_ready_delete_fingerprint = false;
bool g_ready_delete_all_fingerprint = false;
bool g_ready_add_card = false;
bool g_ready_delete_card = false;
char g_add_card_number[9] = {0};
char g_delete_card_number[9] = {0};
uint8_t g_deleteFingerprintID = 0;

httpd_handle_t server = NULL;

// Save file descriptors of connected clients
static int ws_clients[MAX_WS_CLIENTS] = {0};
static int ws_client_count = 0;

static const char *TAG = "web_server";

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirect to captive portal", HTTPD_RESP_USE_STRLEN);
    ESP_LOGI("CAPTIVE_PORTAL", "HTTP redirect to root");
    return ESP_OK;
}

/**
 * @brief Start the Web server
 */
httpd_handle_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 32768; // Increase stack size
    config.max_open_sockets = 13;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    // -------------------------------
    // URI handler definitions
    // -------------------------------
    static const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL};

    static const httpd_uri_t css_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = css_handler,
        .user_ctx = NULL};

    static const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true};

    static const httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_handler,
        .user_ctx = NULL};

    // -------------------------------
    // Start server
    // -------------------------------
    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Web server startup failed");
        return NULL;
    }

    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler); // Register 404 redirect
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &css_uri);
    httpd_register_uri_handler(server, &ws_uri);
    httpd_register_uri_handler(server, &favicon_uri);

    ESP_LOGI(TAG, "Web server started successfully");
    return server;
}

/**
 * Root page request handler
 */
static esp_err_t root_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Received root page request");
    if (!index_html)
    {
        ESP_LOGE(TAG, "index_html not loaded");
        return httpd_resp_send_500(req);
    }

    esp_err_t res = httpd_resp_set_type(req, "text/html");
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set content type");
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Connection", "close"); // Prevent Keep-Alive issues

    res = httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send root page (%s)", esp_err_to_name(res));
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * CSS style request handler
 */
static esp_err_t css_handler(httpd_req_t *req)
{
    struct stat st;
    if (stat(CSS_PATH, &st) != 0)
    {
        ESP_LOGE(TAG, "style.css not found");
        return httpd_resp_send_404(req);
    }
    FILE *fp = fopen(CSS_PATH, "rb");
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
            ESP_LOGE(TAG, "Failed to send CSS data");
            return ESP_FAIL;
        }
    }
    fclose(fp);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/**
 * favicon.ico request handler
 */
static esp_err_t favicon_handler(httpd_req_t *req)
{
    struct stat st;
    if (stat(FAVICON_PATH, &st) != 0)
    {
        ESP_LOGE(TAG, "favicon.ico not found");
        return httpd_resp_send_404(req);
    }
    FILE *fp = fopen(FAVICON_PATH, "rb");
    if (!fp)
    {
        ESP_LOGE(TAG, "Failed to open favicon.ico");
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "image/x-icon");
    char buffer[512];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        if (httpd_resp_send_chunk(req, buffer, bytes_read) != ESP_OK)
        {
            fclose(fp);
            ESP_LOGE(TAG, "Failed to send favicon.ico data");
            return ESP_FAIL;
        }
    }
    fclose(fp);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/**
 * WebSocket request handler - Process button commands and print prompts
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WebSocket request handler called, method:%d", req->method);

    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Client attempts to establish WebSocket connection");
        int fd = httpd_req_to_sockfd(req);
        bool found = false;

        for (size_t i = 0; i < ws_client_count; i++)
        {
            if (ws_clients[i] == fd)
            {
                ESP_LOGW(TAG, "Client fd=%d already exists", fd);
                found = true;
                break;
            }
        }

        if (!found)
        {
            if (ws_client_count < MAX_WS_CLIENTS)
            {
                ws_clients[ws_client_count++] = fd;
                ESP_LOGI(TAG, "New client joined, fd=%d, total=%d", fd, ws_client_count);
            }
            else
            {
                ESP_LOGW(TAG, "Client limit reached, rejecting fd=%d", fd);
                return ESP_FAIL;
            }
        }

        send_init_data();
        return ESP_OK;
    }

    // Receive data
    httpd_ws_frame_t ws_pkt = {0};
    char recv_buf[WS_RECV_BUFFER_SIZE] = {0};
    ws_pkt.payload = (uint8_t *)recv_buf;

    // Parse frame header to get frame length
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to receive frame header: %s", esp_err_to_name(ret));
        return ret;
    }

    // Limit maximum length to prevent buffer overflow
    if (ws_pkt.len >= WS_RECV_BUFFER_SIZE)
    {
        ESP_LOGW(TAG, "Data too long, truncated to %u bytes", WS_RECV_BUFFER_SIZE - 1);
        ws_pkt.len = WS_RECV_BUFFER_SIZE - 1;
    }

    // Receive actual data
    if (ws_pkt.len > 0)
    {
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to receive data: %s", esp_err_to_name(ret));
            return ret;
        }

        // Ensure string ends with '\0'
        recv_buf[ws_pkt.len] = '\0';
        ESP_LOGI(TAG, "Received data [length:%u]: %s", ws_pkt.len, recv_buf);
    }
    else
    {
        // Return directly for empty data
        return ESP_OK;
    }

    // Command processing logic
    if (strcmp(recv_buf, "add_card") == 0)
    {
        ESP_LOGI(TAG, "Processing add card command");
        // Check if there is remaining space
        if (g_card_count < MAX_CARDS)
        {
            g_ready_add_card = true;
        }
        else
        {
            send_status_msg("Card limit reached");
        }
    }
    else if (strcmp(recv_buf, "cancel_add_card") == 0)
    {
        ESP_LOGI(TAG, "Processing cancel add card command");
        g_ready_add_card = false;
    }
    else if (strstr(recv_buf, "delete_card:") != NULL)
    {
        char *prefix = "delete_card:";
        strncpy(g_delete_card_number, recv_buf + strlen(prefix), sizeof(g_delete_card_number) - 1);
        g_delete_card_number[sizeof(g_delete_card_number) - 1] = '\0';
        ESP_LOGI(TAG, "Processing delete specified card command, card number: %s", g_delete_card_number);

        g_ready_delete_card = true; // todo
        // for (uint8_t i = 0; i < g_card_count; i++)
        // {
        //     if (strcmp(g_delete_card_number, g_card_id_value[i]) == 0)
        //     {
        //         // Find matching item, delete card
        //         for (uint8_t j = i; j < g_card_count - 1; j++)
        //         {
        //             g_card_id_value[j] = g_card_id_value[j + 1];
        //             strcpy(g_card_id_value[j], g_card_id_value[j + 1]);
        //         }
        //         g_card_count--;
        //         nvs_custom_set_u8(NULL, "card", "count", g_card_count);
        //         send_operation_result("card_deleted", true); // Send operation result
        //         ESP_LOGI(TAG, "Card %s deleted successfully", g_delete_card_number);
        //         break;
        //     }
        // }
    }
    else if (strcmp(recv_buf, "add_fingerprint") == 0)
    {
        ESP_LOGI(TAG, "Processing add fingerprint command, current module state: %u", zw111.state);
        // Check if there is remaining space
        if (zw111.fingerNumber < 100)
        {
            if (zw111.power == true)
            {
                cancel_current_operation_and_execute_command();
                g_ready_add_fingerprint = true;
            }
            else
            {
                zw111.state = 0x02;    // Set state to enroll fingerprint state
                turn_on_fingerprint(); // Power on!
            }
        }
        else
        {
            send_status_msg("Fingerprint limit reached");
        }
    }
    else if (strcmp(recv_buf, "cancel_add_fingerprint") == 0)
    {
        ESP_LOGI(TAG, "Processing cancel add fingerprint command");
        g_cancel_add_fingerprint = true;
        cancel_current_operation_and_execute_command();
    }
    else if (strcmp(recv_buf, "clear_cards") == 0)
    {
        ESP_LOGI(TAG, "Processing clear all cards command");
        g_card_count = 0;
        nvs_custom_set_u8(NULL, "card", "count", g_card_count);
        send_operation_result("card_cleared", true); // Send operation result
    }
    else if (strcmp(recv_buf, "clear_fingerprints") == 0)
    {
        ESP_LOGI(TAG, "Processing clear all fingerprints command, current module state: %u", zw111.state);
        g_ready_delete_all_fingerprint = true;
        if (zw111.power == true)
        {
            cancel_current_operation_and_execute_command();
        }
        else
        {
            zw111.state = 0x03;    // Set state to delete fingerprint state
            turn_on_fingerprint(); // Power on!
        }
    }
    else if (strcmp(recv_buf, "refresh_cards") == 0)
    {
        ESP_LOGI(TAG, "Processing refresh card list command");
        send_card_list();
    }
    else if (strcmp(recv_buf, "refresh_fingerprints") == 0)
    {
        ESP_LOGI(TAG, "Processing refresh fingerprint list command");
        send_fingerprint_list();
    }
    else if (strstr(recv_buf, "delete_fingerprint:") != NULL)
    {
        char *prefix = "delete_fingerprint:";
        g_deleteFingerprintID = atoi(recv_buf + strlen(prefix));
        ESP_LOGI(TAG, "Processing delete specified fingerprint command, ID: %u, current module state: %u", g_deleteFingerprintID, zw111.state);
        g_ready_delete_fingerprint = true;
        if (zw111.power == true)
        {
            cancel_current_operation_and_execute_command();
        }
        else
        {
            zw111.state = 0x03;    // Set state to delete fingerprint state
            turn_on_fingerprint(); // Power on!
        }
    }
    else if (ws_pkt.len > 0)
    {
        ESP_LOGI(TAG, "Received unknown command: %s", recv_buf);
        send_status_msg("Unknown command");
    }
    return ESP_OK;
}

/**
 * WebSocket broadcast JSON data
 */
static esp_err_t ws_broadcast_json(cJSON *json)
{
    if (!json)
        return ESP_FAIL;
    char *json_str = cJSON_PrintUnformatted(json);
    if (!json_str)
        return ESP_FAIL;
    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json_str,
        .len = strlen(json_str)};
    ESP_LOGI(TAG, "Broadcasting message: %s", json_str);
    for (int i = 0; i < ws_client_count; i++)
    {
        if (httpd_ws_send_frame_async(server, ws_clients[i], &ws_pkt) != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to send to client fd=%d, removing, current client count:%d", ws_clients[i], ws_client_count);
            ws_clients[i] = ws_clients[--ws_client_count];
            i--;
        }
    }
    free(json_str);
    return ESP_OK;
}

/**
 * Send card list
 */
void send_card_list()
{
    cJSON *root = cJSON_CreateObject();
    cJSON *data_array = cJSON_CreateArray();
    for (int i = 0; i < g_card_count; i++)
    {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "cardNumber", g_card_id_value[i]);
        cJSON_AddItemToArray(data_array, item);
    }
    cJSON_AddStringToObject(root, "type", "card_list");
    cJSON_AddItemToObject(root, "data", data_array);
    ws_broadcast_json(root);
    cJSON_Delete(root);
}

/**
 * Send fingerprint list
 */
void send_fingerprint_list()
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
    ws_broadcast_json(root);
    cJSON_Delete(root);
}

/**
 * Send status message
 */
void send_status_msg(const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "status");
    cJSON_AddStringToObject(root, "message", message);
    ws_broadcast_json(root);
    cJSON_Delete(root);
}

/**
 * Send initialization data
 */
void send_init_data()
{
    cJSON *root = cJSON_CreateObject();
    cJSON *cards_array = cJSON_CreateArray();
    cJSON *fingers_array = cJSON_CreateArray();
    // Add card data
    for (int i = 0; i < g_card_count; i++)
    {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "cardNumber", g_card_id_value[i]);
        cJSON_AddItemToArray(cards_array, item);
    }

    // Add fingerprint data
    for (int i = 0; i < zw111.fingerNumber; i++)
    {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "templateId", zw111.fingerIDArray[i]);
        cJSON_AddItemToArray(fingers_array, item);
    }

    cJSON_AddStringToObject(root, "type", "init_data");

    // Add version number
    cJSON_AddStringToObject(root, "version", CONFIG_APP_PROJECT_VER);
    cJSON_AddStringToObject(root, "password", g_touch_password);
    cJSON_AddItemToObject(root, "fingers", fingers_array);
    cJSON_AddItemToObject(root, "cards", cards_array);
    ws_broadcast_json(root);
    cJSON_Delete(root);
}

/**
 * Send operation result
 */
void send_operation_result(const char *message, bool result)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "operation_result");
    cJSON_AddStringToObject(root, "message", message);
    cJSON_AddBoolToObject(root, "result", result);
    ws_broadcast_json(root);
    cJSON_Delete(root);
}
