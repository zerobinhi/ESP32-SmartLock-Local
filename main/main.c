#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "app_config.h"
#include "wifi.h"
#include "spiffs.h"
#include "web_server.h"

static const char *TAG = "SmartLock Main";

void app_main(void)
{
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化系统组件
    ESP_LOGI(TAG, "Initializing system components...");
    wifi_init_softap();
    spiffs_init_and_load_webpage();

    // 启动Web服务器
    web_server_start();

    ESP_LOGI(TAG, "SmartLock system ready.");
}