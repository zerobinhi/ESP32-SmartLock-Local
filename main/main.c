#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include "wifi.h"
#include "spiffs.h"
#include "web_server.h"
#include "zw111.h"
#include "pn532_i2c.h"
#include "ssd1306.h"
#include "touch.h"
#include "battery.h"
#include "nvs_custom.h"

static const char *TAG = "main";

void app_main(void)
{
    // initialize NVS
    if (nvs_custom_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "NVS initialization successful");
    }
    // initialize system components
    ESP_LOGI(TAG, "Initializing system components...");

    // initializing OLED display
    if (ssd1306_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "OLED display initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "SSD1306 OLED display initialization successful");
    }

    // initializing battery monitoring
    if (battery_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "battery monitoring initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "battery monitoring initialization successful");
    }

    if (smart_lock_buzzer_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "buzzer module initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "buzzer module initialization successful");
    }

    if (fingerprint_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "fingerprint module initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "fingerprint module initialization successful");
    }

    if (pn532_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "PN532 module initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "PN532 module initialization successful");
    }

    if (touch_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "capacitive touch button initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "capacitive touch button initialization successful");
    }

    // spiffs_init_and_load_webpage();
    // wifi_init_softap();
    // web_server_start(); // 启动Web服务器

    ESP_LOGI(TAG, "Function: %s, File: %s, Line: %d\n", __func__, __FILE__, __LINE__);

    ESP_LOGI(TAG, "smart lock system initialization complete.");
}
