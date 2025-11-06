#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include "wifi.h"
#include "spiffs.h"
#include "web_server.h"
#include "zw111.h"
#include "pn532_i2c.h"
#include "ssd1306.h"
#include "ft6336u.h"
#include "nvs_custom.h"

static const char *TAG = "SmartLock Main";

void app_main(void)
{
    // 初始化NVS
    if (nvs_custom_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS 初始化失败");
    }
    else
    {
        ESP_LOGI(TAG, "NVS 初始化成功");
    }
    // 初始化系统组件
    ESP_LOGI(TAG, "初始化系统组件...");

    spiffs_init_and_load_webpage();
    wifi_init_softap();
    web_server_start(); // 启动Web服务器

    // 初始化蜂鸣器模块
    if (smart_lock_buzzer_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "蜂鸣器模块初始化失败");
    }
    else
    {
        ESP_LOGI(TAG, "蜂鸣器模块初始化成功");
    }

    // 初始化指纹模块
    if (fingerprint_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "指纹模块初始化失败");
    }
    else
    {
        ESP_LOGI(TAG, "指纹模块初始化成功");
    }

    // 初始化PN532模块
    if (pn532_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "PN532模块初始化失败");
    }
    else
    {
        ESP_LOGI(TAG, "PN532模块初始化成功");
    }

    // 初始化FT6336U模块
    if (ft6336u_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "FT6336U模块初始化失败");
    }
    else
    {
        ESP_LOGI(TAG, "FT6336U模块初始化成功");
    }

    // 初始化SSD1306 OLED显示屏
    if (ssd1306_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "SSD1306 OLED显示屏初始化失败");
    }
    else
    {
        ESP_LOGI(TAG, "SSD1306 OLED显示屏初始化成功");
    }

    ESP_LOGI(TAG, "智能门锁系统就绪");
}
