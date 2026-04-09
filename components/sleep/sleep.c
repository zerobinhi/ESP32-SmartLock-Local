#include "sleep.h"

static const char *TAG = "sleep";

uint8_t g_sleep_time = 0;
static int64_t g_last_activity_time = 0;

static void light_sleep_task(void *args)
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(200));

        int64_t now = esp_timer_get_time();

        if ((now - g_last_activity_time) < (g_sleep_time * 1000000LL))
        {
            continue;
        }

        ESP_LOGI(TAG, "Idle timeout, entering light sleep");

        if (zw111.power == true)
        {
            cancel_current_operation_and_execute_command();
            prepare_turn_off_fingerprint();
        }

        if (g_input_len != 0)
        {
            g_input_len = 0;
            memset(g_input_password, 0, sizeof(g_input_password));
        }

        if (pn7160_task_handle != NULL)
        {
            vTaskDelete(pn7160_task_handle);
            pn7160_task_handle = NULL;
        }

        vTaskDelay(pdMS_TO_TICKS(100));

        gpio_wakeup_enable(FINGERPRINT_INT_PIN, GPIO_INTR_HIGH_LEVEL);

        esp_sleep_enable_gpio_wakeup();

        g_touch_wakeup_flag = true;

        esp_light_sleep_start();

        g_last_activity_time = esp_timer_get_time();

        ESP_LOGI(TAG, "Wake up from sleep");

        gpio_set_level(PN7160_RST_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100)); // Wait for reset to complete
        /* pn7160 initialization sequence */
        uint8_t CORE_RESET_CMD[4] = {0x20, 0x00, 0x01, 0x01}; // Core reset command, reset configuration
        i2c_master_transmit(pn7160_handle, CORE_RESET_CMD, sizeof(CORE_RESET_CMD), portMAX_DELAY);
        xSemaphoreTake(pn7160_semaphore, portMAX_DELAY); // Wait for reset to complete
        uint8_t CORE_RESET_RSP[4] = {0};
        i2c_master_receive(pn7160_handle, CORE_RESET_RSP, sizeof(CORE_RESET_RSP), portMAX_DELAY);
        ESP_LOGI(TAG, "pn7160 core reset response: %02x %02x %02x %02x", CORE_RESET_RSP[0], CORE_RESET_RSP[1], CORE_RESET_RSP[2], CORE_RESET_RSP[3]);
        uint8_t CORE_RESET_NTF[12] = {0};
        xSemaphoreTake(pn7160_semaphore, portMAX_DELAY); // Wait for reset to complete
        i2c_master_receive(pn7160_handle, CORE_RESET_NTF, sizeof(CORE_RESET_NTF), portMAX_DELAY);
        ESP_LOGI(TAG, "pn7160 core reset notification: ");
        ESP_LOG_BUFFER_HEX(TAG, CORE_RESET_NTF, sizeof(CORE_RESET_NTF));
        uint8_t CORE_INIT_CMD[5] = {0x20, 0x01, 0x02, 0x00, 0x00}; // Core init command
        i2c_master_transmit(pn7160_handle, CORE_INIT_CMD, sizeof(CORE_INIT_CMD), portMAX_DELAY);
        xSemaphoreTake(pn7160_semaphore, portMAX_DELAY); // Wait for init to complete
        uint8_t CORE_INIT_RSP[33] = {0};
        i2c_master_receive(pn7160_handle, CORE_INIT_RSP, sizeof(CORE_INIT_RSP), portMAX_DELAY);
        ESP_LOGI(TAG, "pn7160 core init response: ");
        ESP_LOG_BUFFER_HEX(TAG, CORE_INIT_RSP, sizeof(CORE_INIT_RSP));
        uint8_t CORE_SET_POWER_MODE_CMD[4] = {0x2F, 0x00, 0x01, 0x00}; // NCI proprietary activation command
        i2c_master_transmit(pn7160_handle, CORE_SET_POWER_MODE_CMD, sizeof(CORE_SET_POWER_MODE_CMD), portMAX_DELAY);
        xSemaphoreTake(pn7160_semaphore, portMAX_DELAY); // Wait for activation
        uint8_t CORE_SET_POWER_MODE_RSP[4] = {0};
        i2c_master_receive(pn7160_handle, CORE_SET_POWER_MODE_RSP, sizeof(CORE_SET_POWER_MODE_RSP), portMAX_DELAY);
        ESP_LOGI(TAG, "pn7160 core set power mode response: %02x %02x %02x %02x", CORE_SET_POWER_MODE_RSP[0], CORE_SET_POWER_MODE_RSP[1], CORE_SET_POWER_MODE_RSP[2], CORE_SET_POWER_MODE_RSP[3]);
        uint8_t CORE_SET_CONFIG_CMD[8] = {0x20, 0x02, 0x05, 0x01, 0x00, 0x02, 0xFE, 0X01}; // Core set config command to enable extended length
        i2c_master_transmit(pn7160_handle, CORE_SET_CONFIG_CMD, sizeof(CORE_SET_CONFIG_CMD), portMAX_DELAY);
        xSemaphoreTake(pn7160_semaphore, portMAX_DELAY); // Wait for config
        uint8_t CORE_SET_CONFIG_RSP[5] = {0};
        i2c_master_receive(pn7160_handle, CORE_SET_CONFIG_RSP, sizeof(CORE_SET_CONFIG_RSP), portMAX_DELAY);
        ESP_LOGI(TAG, "pn7160 core set config response: %02x %02x %02x %02x", CORE_SET_CONFIG_RSP[0], CORE_SET_CONFIG_RSP[1], CORE_SET_CONFIG_RSP[2], CORE_SET_CONFIG_RSP[3]);
        uint8_t CORE_RESET_CMD_KEEP[4] = {0x20, 0x00, 0x01, 0x00}; // Core reset command
        i2c_master_transmit(pn7160_handle, CORE_RESET_CMD_KEEP, sizeof(CORE_RESET_CMD_KEEP), portMAX_DELAY);
        xSemaphoreTake(pn7160_semaphore, portMAX_DELAY); // Wait for reset to complete
        i2c_master_receive(pn7160_handle, CORE_RESET_RSP, sizeof(CORE_RESET_RSP), portMAX_DELAY);
        ESP_LOGI(TAG, "pn7160 core reset response: %02x %02x %02x %02x", CORE_RESET_RSP[0], CORE_RESET_RSP[1], CORE_RESET_RSP[2], CORE_RESET_RSP[3]);
        xSemaphoreTake(pn7160_semaphore, portMAX_DELAY); // Wait for reset to complete
        i2c_master_receive(pn7160_handle, CORE_RESET_NTF, sizeof(CORE_RESET_NTF), portMAX_DELAY);
        ESP_LOGI(TAG, "pn7160 core reset notification: ");
        ESP_LOG_BUFFER_HEX(TAG, CORE_RESET_NTF, sizeof(CORE_RESET_NTF));
        i2c_master_transmit(pn7160_handle, CORE_INIT_CMD, sizeof(CORE_INIT_CMD), portMAX_DELAY);
        xSemaphoreTake(pn7160_semaphore, portMAX_DELAY); // Wait for init to complete
        i2c_master_receive(pn7160_handle, CORE_INIT_RSP, sizeof(CORE_INIT_RSP), portMAX_DELAY);
        ESP_LOGI(TAG, "pn7160 core init response: ");
        ESP_LOG_BUFFER_HEX(TAG, CORE_INIT_RSP, sizeof(CORE_INIT_RSP));
        uint8_t NCI_PROPRIETARY_ACT_CMD[3] = {0x2F, 0x02, 0x00}; // NCI proprietary activation command
        i2c_master_transmit(pn7160_handle, NCI_PROPRIETARY_ACT_CMD, sizeof(NCI_PROPRIETARY_ACT_CMD), portMAX_DELAY);
        xSemaphoreTake(pn7160_semaphore, portMAX_DELAY); // Wait for activation
        uint8_t NCI_PROPRIETARY_ACT_RSP[8] = {0};
        i2c_master_receive(pn7160_handle, NCI_PROPRIETARY_ACT_RSP, sizeof(NCI_PROPRIETARY_ACT_RSP), portMAX_DELAY);
        ESP_LOGI(TAG, "pn7160 NCI proprietary activation response: ");
        ESP_LOG_BUFFER_HEX(TAG, NCI_PROPRIETARY_ACT_RSP, sizeof(NCI_PROPRIETARY_ACT_RSP));
        uint8_t RF_DISCOVER_MAP_CMD[19] = {0x21, 0x00, 0x10, 0x05, 0x01, 0x01, 0x01, 0x02, 0x01, 0x01, 0x03, 0x01, 0x01, 0x04, 0x01, 0x02, 0x80, 0x01, 0x80}; // RF discover map command
        i2c_master_transmit(pn7160_handle, RF_DISCOVER_MAP_CMD, sizeof(RF_DISCOVER_MAP_CMD), portMAX_DELAY);
        xSemaphoreTake(pn7160_semaphore, portMAX_DELAY);
        uint8_t RF_DISCOVER_MAP_RSP[4] = {0};
        i2c_master_receive(pn7160_handle, RF_DISCOVER_MAP_RSP, sizeof(RF_DISCOVER_MAP_RSP), portMAX_DELAY);
        ESP_LOGI(TAG, "pn7160 RF discover map response: %02x %02x %02x %02x", RF_DISCOVER_MAP_RSP[0], RF_DISCOVER_MAP_RSP[1], RF_DISCOVER_MAP_RSP[2], RF_DISCOVER_MAP_RSP[3]);
        uint8_t RF_DISCOVER_CMD[10] = {0x21, 0x03, 0x07, 0x03, 0x00, 0x01, 0x01, 0x01, 0x06, 0x01}; // RF discover command
        i2c_master_transmit(pn7160_handle, RF_DISCOVER_CMD, sizeof(RF_DISCOVER_CMD), portMAX_DELAY);
        xSemaphoreTake(pn7160_semaphore, portMAX_DELAY);
        uint8_t RF_DISCOVER_RSP[4] = {0};
        i2c_master_receive(pn7160_handle, RF_DISCOVER_RSP, sizeof(RF_DISCOVER_RSP), portMAX_DELAY);
        ESP_LOGI(TAG, "pn7160 RF discover response: %02x %02x %02x %02x", RF_DISCOVER_RSP[0], RF_DISCOVER_RSP[1], RF_DISCOVER_RSP[2], RF_DISCOVER_RSP[3]);

        xTaskCreate(pn7160_task, "pn7160_task", 8192, NULL, 10, &pn7160_task_handle);
        ESP_LOGI(TAG, "pn7160 task started");
    }
    vTaskDelete(NULL);
}

void notify_user_activity(void)
{
    g_last_activity_time = esp_timer_get_time();
}

esp_err_t sleep_initialization(void)
{
    esp_err_t err = nvs_custom_get_u8(NULL, "sleep", "sleep_time", &g_sleep_time);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "sleep time not found, using default");

        g_sleep_time = DEFAULT_SLEEP_TIME;

        nvs_custom_set_u8(NULL, "sleep", "sleep_time", g_sleep_time);
    }
    else
    {
        ESP_LOGI(TAG, "sleep time loaded from NVS");
    }

    g_last_activity_time = esp_timer_get_time();

    xTaskCreate(light_sleep_task, "light_sleep_task", 4096, NULL, 6, NULL);
    return ESP_OK;
}