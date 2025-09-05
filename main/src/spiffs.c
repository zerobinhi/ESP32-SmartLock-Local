#include "spiffs.h"

static const char *TAG = "SmartLock SPIFFS";

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
#ifdef DEBUG
        ESP_LOGE(TAG, "SPIFFS 注册失败 (%s)", esp_err_to_name(ret));
#endif
        return;
    }

    // 动态分配缓冲区
    if (!index_html)
    {
        index_html = malloc(INDEX_HTML_BUFFER_SIZE);
        if (!index_html)
        {
#ifdef DEBUG
            ESP_LOGE(TAG, "index_html 缓冲区分配失败");
#endif
            return;
        }
    }

    // 检查并加载 index.html

    struct stat st;
    if (stat(INDEX_HTML_PATH, &st) != 0)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "SPIFFS 中未找到 index.html");
#endif
        return;
    }

    if (st.st_size >= INDEX_HTML_BUFFER_SIZE)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "index.html 文件过大 (大小: %ld, 缓冲区: %d)",
                 st.st_size, INDEX_HTML_BUFFER_SIZE);
#endif
        return;
    }

    FILE *fp = fopen(INDEX_HTML_PATH, "r");
    if (!fp)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "打开 index.html 失败");
#endif
        return;
    }

    size_t bytes_read = fread(index_html, 1, st.st_size, fp);
    fclose(fp);

    if (bytes_read != st.st_size)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "读取 index.html 失败 (已读: %ld, 预期: %ld)",
                 bytes_read, st.st_size);
#endif
        free(index_html);
        index_html = NULL;
    }
    else
    {
        index_html[bytes_read] = '\0';
#ifdef DEBUG
        ESP_LOGI(TAG, "index.html 加载成功 (大小: %ld)", st.st_size);
#endif
    }
}
