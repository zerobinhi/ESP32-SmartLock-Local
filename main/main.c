#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include "app_config.h"
#include "wifi.h"
#include "spiffs.h"
#include "web_server.h"
#include "zw111.h"
#include "pn532_i2c.h"

static const char *MAIN_TAG = "SmartLock Main";

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
#ifdef DEBUG
    ESP_LOGI(MAIN_TAG, "初始化系统组件...");
#endif
    // wifi_init_softap();
    // spiffs_init_and_load_webpage();

    // // 启动Web服务器
    // web_server_start();
    // 初始化指纹模块
    if (fingerprint_initialization() != ESP_OK)
    {
#ifdef DEBUG
        ESP_LOGE(MAIN_TAG, "指纹模块初始化失败");
#endif
    }
    else
    {
#ifdef DEBUG
        ESP_LOGI(MAIN_TAG, "指纹模块初始化成功");
#endif
    }
    // 初始化PN532模块
    if (pn532_initialization() != ESP_OK)
    {
#ifdef DEBUG
        ESP_LOGE(MAIN_TAG, "PN532模块初始化失败");
#endif
    }
    else
    {
#ifdef DEBUG
        ESP_LOGI(MAIN_TAG, "PN532模块初始化成功");
#endif
    }

#ifdef DEBUG
    ESP_LOGI(MAIN_TAG, "智能门锁系统就绪");
#endif
}