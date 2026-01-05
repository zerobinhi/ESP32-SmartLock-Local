#include "spiffs.h"

static const char *TAG = "spiffs";

/**
 * @brief Initialize SPIFFS and load index.html into memory
 */
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
        ESP_LOGE(TAG, "SPIFFS registration failed (%s)", esp_err_to_name(ret));
        return;
    }
    // Dynamically allocate buffer
    if (!index_html)
    {
        index_html = malloc(INDEX_HTML_BUFFER_SIZE);
        if (!index_html)
        {
            ESP_LOGE(TAG, "index_html buffer allocation failed");
            free(index_html);
            return;
        }
    }
    // Check and load index.html
    struct stat st;
    if (stat(INDEX_HTML_PATH, &st) != 0)
    {
        ESP_LOGE(TAG, "index.html not found in SPIFFS");
        free(index_html);
        return;
    }
    if (st.st_size >= INDEX_HTML_BUFFER_SIZE)
    {
        ESP_LOGE(TAG, "index.html file is too large (size: %ld, buffer: %d)",
                 st.st_size, INDEX_HTML_BUFFER_SIZE);
        free(index_html);
        return;
    }
    FILE *fp = fopen(INDEX_HTML_PATH, "r");
    if (!fp)
    {
        ESP_LOGE(TAG, "Failed to open index.html");
        free(index_html);
        return;
    }
    size_t bytes_read = fread(index_html, 1, st.st_size, fp);
    fclose(fp);
    if (bytes_read != st.st_size)
    {
        ESP_LOGE(TAG, "Failed to read index.html (read: %ld, expected: %ld)",
                 bytes_read, st.st_size);
        free(index_html);
        index_html = NULL;
    }
    else
    {
        index_html[bytes_read] = '\0';
        ESP_LOGI(TAG, "index.html loaded successfully (size: %ld)", st.st_size);
    }
}
