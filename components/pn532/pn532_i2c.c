#include "pn532_i2c.h"

/* Semaphore used to notify card detection interrupt */
SemaphoreHandle_t pn532_semaphore = NULL;

/* I2C handles */
i2c_master_bus_handle_t bus_handle;   // I2C master bus handle
i2c_master_dev_handle_t pn532_handle; // PN532 I2C device handle

/* Service installation flags */
bool g_gpio_isr_service_installed = false; // GPIO ISR service installation status
bool g_i2c_service_installed = false;      // I2C service installation status

/* Card storage */
uint64_t g_card_id_value[MAX_CARDS] = {0}; // Stored card IDs (uint64 format)
uint8_t g_card_count = 0;                  // Number of stored cards

/* PN532 commands */
uint8_t g_cmd_detect_card[] = {0x00, 0x00, 0xff, 0x04, 0xfc, 0xd4, 0x4a, 0x02, 0x00, 0xe0, 0x00}; // InListPassiveTarget command

static const char *TAG = "PN532";

/**
 * @brief GPIO interrupt service routine for PN532 INT pin
 *
 * Triggered when PN532 detects a card.
 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;

    if (gpio_num == PN532_INT_PIN && gpio_get_level(PN532_INT_PIN) == 0)
    {
        ESP_EARLY_LOGI(TAG, "Card detection interrupt triggered");
        xSemaphoreGiveFromISR(pn532_semaphore, NULL);
    }
}

/**
 * @brief Initialize PN532 module (I2C + GPIO + NVS)
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t pn532_initialization(void)
{
    /* Create binary semaphore */
    pn532_semaphore = xSemaphoreCreateBinary();
    if (pn532_semaphore == NULL)
    {
        ESP_LOGE(TAG, "Semaphore creation failed");
        return ESP_FAIL;
    }

    /* Initialize I2C bus if not installed */
    if (!g_i2c_service_installed)
    {
        i2c_master_bus_config_t i2c_cfg = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = I2C_MASTER_NUM,
            .scl_io_num = I2C_MASTER_SCL_IO,
            .sda_io_num = I2C_MASTER_SDA_IO,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };

        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &bus_handle));
        g_i2c_service_installed = true;
        ESP_LOGI(TAG, "I2C bus initialized");
    }

    /* Add PN532 device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PN532_I2C_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &pn532_handle));
    ESP_LOGI(TAG, "PN532 device added");

    /* Configure reset pin */
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << PN532_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };

    gpio_config(&rst_cfg);

    /* Install GPIO ISR service if needed */
    if (!g_gpio_isr_service_installed)

    {
        gpio_install_isr_service(0);
        g_gpio_isr_service_installed = true;
    }

    gpio_isr_handler_add(PN532_INT_PIN, gpio_isr_handler, (void *)PN532_INT_PIN);

    /* Hardware reset PN532 */
    gpio_set_level(PN532_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PN532_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "PN532 reset completed");

    /* PN532 initialization sequence */
    uint8_t ack[7];
    uint8_t CMD_WAKEUP[] = {0x00, 0x00, 0xff, 0x02, 0xfe, 0xd4, 0x55, 0xd7, 0x00};
    uint8_t CMD_SAMCONF[] = {0x00, 0x00, 0xff, 0x04, 0xfc, 0xd4, 0x14, 0x01, 0x00, 0x17, 0x00};

    pn532_send_command_and_receive(CMD_WAKEUP, sizeof(CMD_WAKEUP), ack, sizeof(ack));
    pn532_send_command_and_receive(CMD_WAKEUP, sizeof(CMD_WAKEUP), ack, sizeof(ack));
    pn532_send_command_and_receive(CMD_SAMCONF, sizeof(CMD_SAMCONF), ack, sizeof(ack));
    pn532_send_command_and_receive(g_cmd_detect_card, sizeof(g_cmd_detect_card), ack, sizeof(ack));

    ESP_LOGI(TAG, "PN532 configured for card detection");

    /* Configure INT pin */
    gpio_config_t int_cfg = {
        .pin_bit_mask = (1ULL << PN532_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&int_cfg);

    /* Load card data from NVS */
    esp_err_t err = nvs_custom_get_u8(NULL, "card", "count", &g_card_count);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "No card data found in NVS");
        g_card_count = 0;
    }
    else
    {

        size_t size = sizeof(g_card_id_value);
        nvs_custom_get_blob(NULL, "card", "card_ids", g_card_id_value, &size);

        ESP_LOGI(TAG, "Loaded %d cards from NVS", g_card_count);
    }

    /* Create PN532 task */
    xTaskCreate(pn532_task, "pn532_task", 8192, NULL, 10, NULL);
    ESP_LOGI(TAG, "PN532 task started");

    return ESP_OK;
}

/**
 * @brief Card recognition task
 * @param cmd Command data
 * @param cmd_len Command length
 * @param response Response buffer
 * @param response_len Response buffer length
 * @return esp_err_t ESP_OK = success, ESP_FAIL = failure
 */
esp_err_t pn532_send_command_and_receive(const uint8_t *cmd, size_t cmd_len, uint8_t *response, size_t response_len)
{
    if (response == NULL || response_len == 0)
    {
        return ESP_FAIL;
    }
    if (cmd != NULL && cmd_len > 0)
    {
        i2c_master_transmit(pn532_handle, cmd, cmd_len, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(30)); // Wait for module to process
        i2c_master_receive(pn532_handle, response, response_len, portMAX_DELAY);
    }
    else
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Find a card ID in the list and return its index
 * @param card_id Card ID to search
 * @return Index in list (1~20) if found, 0 if not found
 */
uint8_t find_card_id(uint64_t card_id)
{
    // Check if any cards exist
    if (g_card_count == 0)
    {
        return 0;
    }
    // Traverse card list to find match
    for (uint8_t i = 0; i < g_card_count; i++)
    {

        if (g_card_id_value[i] == card_id)
        {
            return i + 1; // Return 1-based index if found
        }
    }
    return 0; // Not found
}

/**
 * @brief Card recognition task
 * @param arg Task parameter (unused, pass NULL)
 * @return void
 */
void pn532_task(void *arg)
{
    uint64_t card_id_value = 0;
    uint8_t res[19] = {0};
    uint8_t g_card_uid[8] = {0};
    while (1)
    {
        if (xSemaphoreTake(pn532_semaphore, portMAX_DELAY) == pdTRUE)
        {
            if (i2c_master_receive(pn532_handle, res, sizeof(res), portMAX_DELAY) == ESP_OK && res[0] == 0x01)
            {

                uint8_t card_id_len = res[13]; // Card ID length

                if (card_id_len < 1 || card_id_len > 8)
                {
                    ESP_LOGE(TAG, "Invalid card ID length: %hhu", card_id_len);
                    return; // Skip invalid card
                }

                memccpy(g_card_uid, &res[14], 0, card_id_len); // Copy card UID

                for (uint8_t i = 0; i < card_id_len; i++)
                {
                    card_id_value |= ((uint64_t)g_card_uid[i]) << ((card_id_len - i - 1) * 8);
                }
                ESP_LOGI(TAG, "Card ID: 0x%llX", card_id_value);


                if (g_ready_add_card == true) // Add card operation
                {
                    if (find_card_id(card_id_value) == 0) // New card
                    {
                        g_ready_add_card = false; // Reset add card flag

                        g_card_id_value[g_card_count] = card_id_value;
                        nvs_custom_set_blob(NULL, "card", "card_ids", g_card_id_value, sizeof(g_card_id_value)); // Save all card IDs
                        g_card_count++;                                                                          // Increment card count
                        send_operation_result("card_added", true);                                               // Send operation result
                        nvs_custom_set_u8(NULL, "card", "count", g_card_count);
                        ESP_LOGI(TAG, "Add card ID (uint64): 0x%llX", card_id_value);
                        send_card_list(); // Send updated card list
                    }
                    else // Card already exists
                    {
                        g_ready_add_card = false; // Reset add card flag
                        send_operation_result("card_added", false);
                        ESP_LOGI(TAG, "Card already exists: 0x%llX", card_id_value);
                    }
                }
                else // Card recognition operation
                {
                    if (find_card_id(card_id_value) == 0) // Unknown card
                    {
                        ESP_LOGI(TAG, "Unknown card: 0x%llX", card_id_value);
                        uint8_t message = 0x00;
                        // xQueueSend(card_queue, &message, portMAX_DELAY);
                    }
                    else // Recognized card
                    {
                        ESP_LOGI(TAG, "Recognized card: 0x%llX", card_id_value);
                        uint8_t message = 0x01;
                        // xQueueSend(card_queue, &message, portMAX_DELAY);
                    }
                }

                uint8_t ack[7] = {0};
                pn532_send_command_and_receive(g_cmd_detect_card, sizeof(g_cmd_detect_card), ack, sizeof(ack)); // Prepare PN532 for next card
                res[0] = 0x00;
            }
        }
    }
}
