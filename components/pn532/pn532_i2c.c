#include "pn532_i2c.h"

SemaphoreHandle_t pn532_semaphore = NULL; // PN532模块的信号量，仅用于识别到卡靠近后读取卡号

i2c_master_bus_handle_t bus_handle;   // I2C主设备句柄
i2c_master_dev_handle_t pn532_handle; // I2C从设备句柄

bool g_gpio_isr_service_installed = false; // 是否安装了GPIO中断服务
bool g_i2c_service_installed = false;      // 是否安装了I2C服务

uint64_t g_card_id_value[MAX_CARDS] = {0};                                                        // 卡号的数值形式
uint8_t g_card_count = 0;                                                                         // 卡的数量
uint8_t g_card_uid[8] = {0};                                                                      // 卡号
uint8_t g_cmd_detect_card[] = {0x00, 0x00, 0xff, 0x04, 0xfc, 0xd4, 0x4a, 0x02, 0x00, 0xe0, 0x00}; // 读取卡片命令

static const char *TAG = "SmartLock PN532";

/**
 * @brief 触摸中断服务程序
 * @param arg 中断参数（传入GPIO编号）
 * @return void
 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    if (gpio_num == PN532_INT_PIN && gpio_get_level(PN532_INT_PIN) == 0)
    {
        ESP_EARLY_LOGI(TAG, "pn532模块中断触发, gpio_num=%u", gpio_num);
        xSemaphoreGiveFromISR(pn532_semaphore, NULL);
    }
}

/**
 * @brief 初始化PN532模块I2C通信
 * @param 无
 * @return esp_err_t ESP_OK=初始化成功，ESP_FAIL=初始化失败
 */
esp_err_t pn532_initialization()
{
    // 创建信号量
    pn532_semaphore = xSemaphoreCreateBinary();
    if (pn532_semaphore == NULL)
    {
        ESP_LOGE("PN532", "Failed to create semaphore");
        return ESP_FAIL;
    }
    if (g_i2c_service_installed == false)
    {
        // 初始化I2C
        i2c_master_bus_config_t i2c_mst_config = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = I2C_MASTER_NUM,
            .scl_io_num = I2C_MASTER_SCL_IO,
            .sda_io_num = I2C_MASTER_SDA_IO,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));
        g_i2c_service_installed = true;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PN532_I2C_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &pn532_handle));
    ESP_LOGI(TAG, "pn532 device created");
    gpio_config_t pn532_reset_gpio_config = {
        .pin_bit_mask = (1ULL << PN532_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&pn532_reset_gpio_config);
    if (g_gpio_isr_service_installed == false)
    {
        gpio_install_isr_service(0);
        g_gpio_isr_service_installed = true;
    }
    gpio_isr_handler_add(PN532_INT_PIN, gpio_isr_handler, (void *)PN532_INT_PIN);

    gpio_set_level(PN532_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PN532_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "pn532 interrupt gpio configured");

    // 让pn532准备读取卡片
    uint8_t ack[7];
    uint8_t CMD_WAKEUP[] = {0x00, 0x00, 0xff, 0x02, 0xfe, 0xd4, 0x55, 0xd7, 0x00};              // 唤醒命令
    uint8_t CMD_SAMCONF[] = {0x00, 0x00, 0xff, 0x04, 0xfc, 0xd4, 0x14, 0x01, 0x00, 0x17, 0x00}; // SAM配置命令
    pn532_send_command_and_receive(CMD_WAKEUP, sizeof(CMD_WAKEUP), ack, sizeof(ack));
    pn532_send_command_and_receive(CMD_WAKEUP, sizeof(CMD_WAKEUP), ack, sizeof(ack));
    pn532_send_command_and_receive(CMD_SAMCONF, sizeof(CMD_SAMCONF), ack, sizeof(ack));
    pn532_send_command_and_receive(g_cmd_detect_card, sizeof(g_cmd_detect_card), ack, sizeof(ack));

    gpio_config_t pn532_int_gpio_config = {
        .pin_bit_mask = (1ULL << PN532_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE};
    gpio_config(&pn532_int_gpio_config);

    // 读取存在nvs的卡片信息
    nvs_custom_get_u8(NULL, "card", "count", &g_card_count);

    // 读取所有卡片ID
    size_t size = sizeof(g_card_id_value); // 读取前告诉 NVS 缓冲区大小
    nvs_custom_get_blob(NULL, "card", "card_ids", g_card_id_value, &size);

    // 打印卡片数量
    ESP_LOGI(TAG, "Total cards loaded: %d", g_card_count);
    // 打印所有卡片ID
    for (uint8_t i = 0; i < g_card_count; i++)
    {
        ESP_LOGI(TAG, "Card %d ID (uint64): 0x%llX", i + 1, g_card_id_value[i]);
    }

    // 创建任务
    xTaskCreate(pn532_task, "pn532_task", 8192, NULL, 10, NULL);
    return ESP_OK;
}

/**
 * 该函数会先发送命令，然后延迟20ms，再接收响应数据
 * @param cmd 命令数据
 * @param cmd_len 命令长度
 * @param response 响应数据
 * @param response_len 响应长度
 * @return esp_err_t ESP_OK=操作成功，ESP_FAIL=操作失败
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
        vTaskDelay(pdMS_TO_TICKS(30)); // 延迟等待模块处理
        i2c_master_receive(pn532_handle, response, response_len, portMAX_DELAY);
    }
    else
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * 在卡号列表中查找指定卡号并返回其序号
 * @param card_id 要查找的卡号
 * @return 若找到则返回卡号在列表中的序号(1~20)，未找到则返回0
 */
uint8_t find_card_id(uint64_t card_id)
{
    // 检查是否有卡片
    if (g_card_count == 0)
    {
        return 0;
    }
    // 遍历卡号列表查找匹配项
    for (uint8_t i = 0; i < g_card_count; i++)
    {

        if (g_card_id_value[i] == card_id)
        {
            return i + 1; // 找到匹配项，返回序号
        }
    }
    return 0; // 未找到匹配项
}

/**
 * @brief 识别卡片任务
 * @param arg 任务参数（未使用，传入NULL）
 * @return void
 */
void pn532_task(void *arg)
{
    uint8_t res[19] = {0};
    while (1)
    {
        if (xSemaphoreTake(pn532_semaphore, portMAX_DELAY) == pdTRUE)
        {
            if (i2c_master_receive(pn532_handle, res, sizeof(res), portMAX_DELAY) == ESP_OK && res[0] == 0x01)
            {
                uint8_t card_id_len = res[13]; // 卡号长度

                if (card_id_len < 1 || card_id_len > 8)
                {
                    ESP_LOGE(TAG, "卡号长度不合法: %hhu", card_id_len);
                    return; // 跳过无效卡号
                }

                memcpy(g_card_uid, &res[14], card_id_len); // 复制卡号

                ESP_LOG_BUFFER_HEX("g_card_uid", g_card_uid, card_id_len); // 打印卡号

                // ssd1306_draw_string(oled_handle, 0, 16, g_card_uid, 16, 1);
                // ssd1306_refresh_gram(oled_handle);

                uint64_t card_id_value = 0;

                for (uint8_t i = 0; i < card_id_len; i++)
                {
                    card_id_value |= ((uint64_t)g_card_uid[i]) << ((card_id_len - i - 1) * 8);
                }
                ESP_LOGI(TAG, "Card ID: 0x%llX", card_id_value);

                if (g_ready_add_card == true) // 添加卡操作
                {
                    if (find_card_id(card_id_value) == 0) // 是新卡
                    {
                        g_ready_add_card = false; // 重置添加卡标志

                        g_card_id_value[g_card_count] = card_id_value;
                        nvs_custom_set_blob(NULL, "card", "card_ids", g_card_id_value, sizeof(g_card_id_value)); // 保存所有卡号
                        g_card_count++;                                                                          // 增加卡片数量
                        send_operation_result("card_added", true);                                               // 发送操作结果
                        nvs_custom_set_u8(NULL, "card", "count", g_card_count);
                        ESP_LOGI(TAG, "Add card ID (uint64): 0x%llX", card_id_value);
                        send_card_list(); // 发送更新后的卡片列表
                    }
                    else // 卡已存在
                    {
                        g_ready_add_card = false; // 重置添加卡标志
                        send_operation_result("card_added", false);
                        ESP_LOGI(TAG, "Card already exists: 0x%llX", card_id_value);
                    }
                }
                else // 非添加卡操作，也就是识别卡操作
                {
                    if (find_card_id(card_id_value) == 0) // 未找到此卡
                    {
                        ESP_LOGI(TAG, "Unknown card: 0x%llX", card_id_value);
                        uint8_t message = 0x00;
                        // xQueueSend(card_queue, &message, portMAX_DELAY);
                    }
                    else // 卡在库中
                    {
                        ESP_LOGI(TAG, "Recognized card: 0x%llX", card_id_value);
                        uint8_t message = 0x01;
                        // xQueueSend(card_queue, &message, portMAX_DELAY);
                    }
                }

                uint8_t ack[7] = {0};
                pn532_send_command_and_receive(g_cmd_detect_card, sizeof(g_cmd_detect_card), ack, sizeof(ack)); // 让pn532准备读取下一张卡
                res[0] = 0x00;
            }
        }
    }
}
