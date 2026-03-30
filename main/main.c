#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include "wifi.h"
#include "spiffs.h"
#include "web_server.h"
#include "zw111.h"
#include "pn7160_i2c.h"
#include "oled.h"
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

    if (touch_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "capacitive touch button initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "capacitive touch button initialization successful");
    }

    // initializing OLED display
    if (oled_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "OLED display initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "OLED display initialization successful");
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

    // initializing buzzer
    if (smart_lock_buzzer_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "buzzer module initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "buzzer module initialization successful");
    }

    // initializing fingerprint module
    if (fingerprint_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "fingerprint module initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "fingerprint module initialization successful");
    }

    // initializing PN7160 NFC module
    if (pn7160_initialization() != ESP_OK)
    {
        ESP_LOGE(TAG, "PN7160 module initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "PN7160 module initialization successful");
    }

    // spiffs_init_and_load_webpage();
    // wifi_init_softap();
    // web_server_start();

    ESP_LOGI(TAG, "Function: %s, File: %s, Line: %d", __func__, __FILE__, __LINE__);

    ESP_LOGI(TAG, "smart lock system initialization complete.");
}
