#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "app_config.h"

void spiffs_init_and_load_webpage(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    { // 增加SPIFFS注册失败处理
        ESP_LOGE(TAG, "Failed to register SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    // 检查并加载index.html
    memset(index_html, 0, sizeof(index_html));
    struct stat st;
    if (stat(INDEX_HTML_PATH, &st) != 0)
    {
        ESP_LOGE(TAG, "index.html not found in SPIFFS");
        return;
    }

    // 检查文件大小是否超过缓冲区
    if (st.st_size >= sizeof(index_html))
    {
        ESP_LOGE(TAG, "index.html too large (size: %ld, buffer: %ld)",
                 st.st_size, sizeof(index_html));
        return;
    }

    FILE *fp = fopen(INDEX_HTML_PATH, "r");
    if (!fp)
    {
        ESP_LOGE(TAG, "Failed to open index.html");
        return;
    }

    // 正确读取文件（使用实际大小而非1个块）
    size_t bytes_read = fread(index_html, 1, st.st_size, fp);
    fclose(fp);

    if (bytes_read != st.st_size)
    {
        ESP_LOGE(TAG, "Failed to read index.html (read: %ld, expected: %ld)",
                 bytes_read, st.st_size);
    }
    else
    {
        ESP_LOGI(TAG, "index.html loaded successfully (size: %ld)", st.st_size);
    }
}