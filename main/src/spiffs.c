#include <stdio.h>
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
    {
        ESP_LOGE(TAG, "SPIFFS注册失败 (%s)", esp_err_to_name(ret));
        return;
    }

    // 检查并加载index.html
    memset(index_html, 0, sizeof(index_html));
    struct stat st;
    if (stat(INDEX_HTML_PATH, &st) != 0)
    {
        ESP_LOGE(TAG, "SPIFFS中未找到index.html");
        return;
    }

    if (st.st_size >= sizeof(index_html))
    {
        ESP_LOGE(TAG, "index.html文件过大 (大小: %ld, 缓冲区: %ld)",
                 st.st_size, sizeof(index_html));
        return;
    }

    FILE *fp = fopen(INDEX_HTML_PATH, "r");
    if (!fp)
    {
        ESP_LOGE(TAG, "打开index.html失败");
        return;
    }

    size_t bytes_read = fread(index_html, 1, st.st_size, fp);
    fclose(fp);

    if (bytes_read != st.st_size)
    {
        ESP_LOGE(TAG, "读取index.html失败 (已读: %ld, 预期: %ld)",
                 bytes_read, st.st_size);
    }
    else
    {
        ESP_LOGI(TAG, "index.html加载成功 (大小: %ld)", st.st_size);
    }
}