#include "pn7160_i2c.h"

/* Semaphore used to notify card detection interrupt */
SemaphoreHandle_t pn7160_semaphore = NULL;

/* I2C handles */
i2c_master_bus_handle_t bus_handle;    // I2C master bus handle
i2c_master_dev_handle_t pn7160_handle; // PN7160 I2C device handle

/* Service installation flags */
bool g_gpio_isr_service_installed = false; // GPIO ISR service installation status
bool g_i2c_service_installed = false;      // I2C service installation status

/* Card storage */
uint64_t g_card_id_value[MAX_CARDS] = {0}; // Stored card IDs (uint64 format)
uint8_t g_card_count = 0;                  // Number of stored cards

// uint16_t BytesRead;           // records number of bytes read from PN7160
// uint8_t pResponseBuffer[512]; // storage for response from PN7160

static const char *TAG = "pn7160";

/**
 * @brief GPIO interrupt service routine for PN7160 INT pin
 *
 * Triggered when PN7160 detects a card.
 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (gpio_num == PN7160_INT_PIN)
    {
        xSemaphoreGiveFromISR(pn7160_semaphore, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken)
    {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Initialize PN7160 module (I2C + GPIO + NVS)
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t pn7160_initialization(void)
{
    /* Create binary semaphore */
    pn7160_semaphore = xSemaphoreCreateBinary();
    if (pn7160_semaphore == NULL)
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

    /* Add PN7160 device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PN7160_I2C_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &pn7160_handle));
    ESP_LOGI(TAG, "PN7160 device added");

    /* Configure reset pin */
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << PN7160_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&rst_cfg);

    /* Configure interrupt pin*/
    gpio_config_t pn7160_irq_cfg = {
        .pin_bit_mask = (1ULL << PN7160_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE};
    gpio_config(&pn7160_irq_cfg);

    /* Install GPIO ISR service if needed */
    if (!g_gpio_isr_service_installed)
    {
        gpio_install_isr_service(0);
        g_gpio_isr_service_installed = true;
        ESP_LOGI(TAG, "GPIO ISR service installed");
    }

    gpio_isr_handler_add(PN7160_INT_PIN, gpio_isr_handler, (void *)PN7160_INT_PIN);
    ESP_LOGI(TAG, "PN7160 INT pin ISR handler added");

    /* Load card data from NVS */
    if (nvs_custom_get_u8(NULL, "card", "count", &g_card_count) == ESP_OK)
    {
        size_t size = sizeof(g_card_id_value);
        nvs_custom_get_blob(NULL, "card", "card_ids", g_card_id_value, &size);
        ESP_LOGI(TAG, "Loaded %d cards from NVS", g_card_count);
    }
    else
    {
        ESP_LOGW(TAG, "No card data found in NVS");
        g_card_count = 0;
    }

    /* Hardware reset PN7160 */
    gpio_set_level(PN7160_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PN7160_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(30));

    ESP_LOGI(TAG, "PN7160 reset completed");

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

    /* Create pn7160 task */
    xTaskCreate(pn7160_task, "pn7160_task", 8192, NULL, 10, NULL);
    ESP_LOGI(TAG, "pn7160 task started");

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

void pn7160_task(void *arg)
{
    uint8_t RF_DISCOVER_NTF[24] = {0}; // Buffer for RF discover notification
    uint64_t card_id_value[20] = {0};  // Array to store card IDs (max 20 cards)
    uint8_t card_count = 0;
    while (1)
    {
        if (xSemaphoreTake(pn7160_semaphore, portMAX_DELAY) == pdTRUE)
        {
            // failed frame:60 07 01 a1
            // one card successful frame:61 05 15 01 01 02 00 ff 01 0a 04 00 04 98 8c b3 a2 01 08 00 00 00 00 00
            // two cards successful frame:
            // 61 03 0f 01 80 00 0a 04 00 04 78 ec 86 a2 01 08 00 02
            // 61 03 0f 02 80 00 0a 04 00 04 98 8c b3 a2 01 08 00 01
            if (i2c_master_receive(pn7160_handle, RF_DISCOVER_NTF, sizeof(RF_DISCOVER_NTF), pdMS_TO_TICKS(1000)) == ESP_OK)
            {
                card_count = 1; // Reset card count to 1 by default, will set to 2 if we find two cards in notification
                ESP_LOGI(TAG, "Card detected");
                ESP_LOG_BUFFER_HEX(TAG, RF_DISCOVER_NTF, sizeof(RF_DISCOVER_NTF));
                if (RF_DISCOVER_NTF[0] == 0x60 && RF_DISCOVER_NTF[1] == 0x07 && RF_DISCOVER_NTF[2] == 0x01 && RF_DISCOVER_NTF[3] == 0xa1)
                {
                    ESP_LOGW(TAG, "Card detection failed");
                    continue;
                }
                if (RF_DISCOVER_NTF[0] == 0x61 && RF_DISCOVER_NTF[1] == 0x03 && RF_DISCOVER_NTF[2] == 0x0f)
                {
                    card_count = 2; // Two cards detected
                    xSemaphoreTake(pn7160_semaphore, pdMS_TO_TICKS(1000));
                    i2c_master_receive(pn7160_handle, RF_DISCOVER_NTF, sizeof(RF_DISCOVER_NTF), pdMS_TO_TICKS(1000));
                    ESP_LOGI(TAG, "RF discover notification:");
                    ESP_LOG_BUFFER_HEX(TAG, RF_DISCOVER_NTF, sizeof(RF_DISCOVER_NTF));
                    uint8_t RF_DISCOVER_SELECT_CMD_1[6] = {0x21, 0x04, 0x03, 0x01, 0x80, 0x01}; // RF discover select command for card type 1
                    i2c_master_transmit(pn7160_handle, RF_DISCOVER_SELECT_CMD_1, sizeof(RF_DISCOVER_SELECT_CMD_1), pdMS_TO_TICKS(1000));
                    xSemaphoreTake(pn7160_semaphore, pdMS_TO_TICKS(1000));
                    uint8_t RF_DISCOVER_SELECT_RSP[4] = {0};
                    i2c_master_receive(pn7160_handle, RF_DISCOVER_SELECT_RSP, sizeof(RF_DISCOVER_SELECT_RSP), pdMS_TO_TICKS(1000));
                    ESP_LOGI(TAG, "RF discover select response: %02x %02x %02x %02x", RF_DISCOVER_SELECT_RSP[0], RF_DISCOVER_SELECT_RSP[1], RF_DISCOVER_SELECT_RSP[2], RF_DISCOVER_SELECT_RSP[3]);
                    xSemaphoreTake(pn7160_semaphore, pdMS_TO_TICKS(1000));
                    i2c_master_receive(pn7160_handle, RF_DISCOVER_NTF, sizeof(RF_DISCOVER_NTF), pdMS_TO_TICKS(1000));
                    ESP_LOGI(TAG, "RF discover notification:");
                    ESP_LOG_BUFFER_HEX(TAG, RF_DISCOVER_NTF, sizeof(RF_DISCOVER_NTF));
                    card_id_value[1] = 0;
                    for (uint8_t i = 0; i < 4; i++)
                    {
                        card_id_value[1] = (card_id_value[1] << 8) | RF_DISCOVER_NTF[13 + i];
                    }
                    uint8_t RF_DEACTIVATE_CMD_SLEEP[4] = {0x21, 0x06, 0x01, 0x01}; // RF deactivate command, sleep mode
                    i2c_master_transmit(pn7160_handle, RF_DEACTIVATE_CMD_SLEEP, sizeof(RF_DEACTIVATE_CMD_SLEEP), pdMS_TO_TICKS(1000));
                    xSemaphoreTake(pn7160_semaphore, pdMS_TO_TICKS(1000));
                    uint8_t RF_DEACTIVATE_RSP[4] = {0};
                    i2c_master_receive(pn7160_handle, RF_DEACTIVATE_RSP, sizeof(RF_DEACTIVATE_RSP), pdMS_TO_TICKS(1000));
                    ESP_LOGI(TAG, "RF deactivate response: %02x %02x %02x %02x", RF_DEACTIVATE_RSP[0], RF_DEACTIVATE_RSP[1], RF_DEACTIVATE_RSP[2], RF_DEACTIVATE_RSP[3]);
                    xSemaphoreTake(pn7160_semaphore, pdMS_TO_TICKS(1000));
                    uint8_t RF_DEACTIVATE_NTF[5] = {0};
                    i2c_master_receive(pn7160_handle, RF_DEACTIVATE_NTF, sizeof(RF_DEACTIVATE_NTF), pdMS_TO_TICKS(1000));
                    ESP_LOGI(TAG, "RF deactivate notification:");
                    ESP_LOG_BUFFER_HEX(TAG, RF_DEACTIVATE_NTF, sizeof(RF_DEACTIVATE_NTF));
                    uint8_t RF_DISCOVER_SELECT_CMD_2[6] = {0x21, 0x04, 0x03, 0x02, 0x80, 0x01}; // RF discover select command for card type 2
                    i2c_master_transmit(pn7160_handle, RF_DISCOVER_SELECT_CMD_2, sizeof(RF_DISCOVER_SELECT_CMD_2), pdMS_TO_TICKS(1000));
                    xSemaphoreTake(pn7160_semaphore, pdMS_TO_TICKS(1000));
                    uint8_t RF_DISCOVER_SELECT_RSP_2[4] = {0};
                    i2c_master_receive(pn7160_handle, RF_DISCOVER_SELECT_RSP_2, sizeof(RF_DISCOVER_SELECT_RSP_2), pdMS_TO_TICKS(1000));
                    ESP_LOGI(TAG, "RF discover select response: %02x %02x %02x %02x", RF_DISCOVER_SELECT_RSP_2[0], RF_DISCOVER_SELECT_RSP_2[1], RF_DISCOVER_SELECT_RSP_2[2], RF_DISCOVER_SELECT_RSP_2[3]);
                    xSemaphoreTake(pn7160_semaphore, pdMS_TO_TICKS(1000));
                    i2c_master_receive(pn7160_handle, RF_DISCOVER_NTF, sizeof(RF_DISCOVER_NTF), pdMS_TO_TICKS(1000));
                    ESP_LOGI(TAG, "RF discover notification:");
                    ESP_LOG_BUFFER_HEX(TAG, RF_DISCOVER_NTF, sizeof(RF_DISCOVER_NTF));
                }

                card_id_value[0] = 0;
                for (uint8_t i = 0; i < 4; i++)
                {
                    card_id_value[0] = (card_id_value[0] << 8) | RF_DISCOVER_NTF[13 + i];
                }

                for (uint8_t i = 0; i < card_count; i++)
                {
                    ESP_LOGI(TAG, "Card %d ID (uint64): 0x%llX", i + 1, card_id_value[i]);
                }

                for (uint8_t i = 0; i < card_count; i++)
                {
                    if (g_ready_add_card == true) // Add card operation
                    {

                        if (find_card_id(card_id_value[i]) == 0) // Card not found, can be added
                        {
                            g_card_id_value[g_card_count] = card_id_value[i];                                        // Store new card ID
                            nvs_custom_set_blob(NULL, "card", "card_ids", g_card_id_value, sizeof(g_card_id_value)); // Save all card IDs
                            g_card_count++;                                                                          // Increment card count
                            send_operation_result("card_added", true);                                               // Send operation result
                            nvs_custom_set_u8(NULL, "card", "count", g_card_count);
                            ESP_LOGI(TAG, "Add card ID (uint64): 0x%llX", card_id_value[i]);
                            send_card_list(); // Send updated card list
                        }
                        else // Card already exists
                        {
                            send_operation_result("card_added", false);
                            ESP_LOGI(TAG, "Card already exists: 0x%llX", card_id_value[i]);
                        }
                    }
                    else // Card recognition operation
                    {
                        if (find_card_id(card_id_value[i]) == 0) // Unknown card
                        {
                            ESP_LOGW(TAG, "Unknown Card ID (uint64): 0x%llX", card_id_value[i]);
                            uint8_t message = 0x00;
                            xQueueSend(card_queue, &message, pdMS_TO_TICKS(1000));
                        }
                        else // Recognized card
                        {
                            ESP_LOGI(TAG, "Recognized card: 0x%llX", card_id_value[i]);
                            uint8_t message = 0x01;
                            xQueueSend(card_queue, &message, pdMS_TO_TICKS(1000));
                        }
                    }
                    g_ready_add_card = false; // Reset add card flag
                }
            }
            else
            {
                ESP_LOGE(TAG, "Failed to receive RF discover notification");
            }
            ESP_LOGI(TAG, "Cleared pending notifications");
            i2c_master_receive(pn7160_handle, RF_DISCOVER_NTF, sizeof(RF_DISCOVER_NTF), pdMS_TO_TICKS(100)); // Clear any pending notifications
            ESP_LOG_BUFFER_HEX(TAG, RF_DISCOVER_NTF, sizeof(RF_DISCOVER_NTF));
            uint8_t RF_DEACTIVATE_CMD[4] = {0x21, 0x06, 0x01, 0x00}; // RF deactivate command, idle mode
            i2c_master_transmit(pn7160_handle, RF_DEACTIVATE_CMD, sizeof(RF_DEACTIVATE_CMD), pdMS_TO_TICKS(1000));
            xSemaphoreTake(pn7160_semaphore, pdMS_TO_TICKS(1000));
            uint8_t RF_DEACTIVATE_RSP[4] = {0};
            i2c_master_receive(pn7160_handle, RF_DEACTIVATE_RSP, sizeof(RF_DEACTIVATE_RSP), pdMS_TO_TICKS(1000));
            ESP_LOGI(TAG, "RF deactivate response: %02x %02x %02x %02x", RF_DEACTIVATE_RSP[0], RF_DEACTIVATE_RSP[1], RF_DEACTIVATE_RSP[2], RF_DEACTIVATE_RSP[3]);
            xSemaphoreTake(pn7160_semaphore, pdMS_TO_TICKS(1000));
            uint8_t RF_DEACTIVATE_NTF[5] = {0};
            i2c_master_receive(pn7160_handle, RF_DEACTIVATE_NTF, sizeof(RF_DEACTIVATE_NTF), pdMS_TO_TICKS(1000));
            ESP_LOGI(TAG, "RF deactivate notification:");
            ESP_LOG_BUFFER_HEX(TAG, RF_DEACTIVATE_NTF, sizeof(RF_DEACTIVATE_NTF));
            vTaskDelay(pdMS_TO_TICKS(100));                                                             // Delay before next discovery
            uint8_t RF_DISCOVER_CMD[10] = {0x21, 0x03, 0x07, 0x03, 0x00, 0x01, 0x01, 0x01, 0x06, 0x01}; // RF discover command
            i2c_master_transmit(pn7160_handle, RF_DISCOVER_CMD, sizeof(RF_DISCOVER_CMD), pdMS_TO_TICKS(1000));
            uint8_t RF_DISCOVER_RSP[4] = {0};
            xSemaphoreTake(pn7160_semaphore, pdMS_TO_TICKS(1000));
            i2c_master_receive(pn7160_handle, RF_DISCOVER_RSP, sizeof(RF_DISCOVER_RSP), pdMS_TO_TICKS(1000));
            ESP_LOGI(TAG, "pn7160 RF discover response: %02x %02x %02x %02x", RF_DISCOVER_RSP[0], RF_DISCOVER_RSP[1], RF_DISCOVER_RSP[2], RF_DISCOVER_RSP[3]);
        }
    }
}