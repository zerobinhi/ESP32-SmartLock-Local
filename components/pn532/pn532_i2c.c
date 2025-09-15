#include "pn532_i2c.h"

SemaphoreHandle_t pn532_semaphore = NULL; // PN532模块的信号量，仅用于识别到卡靠近后读取卡号
i2c_master_bus_handle_t bus_handle;       // I2C主设备句柄
i2c_master_dev_handle_t pn532_handle;     // I2C从设备句柄

bool g_gpio_isr_service_installed = false; // 是否安装了GPIO中断服务

uint8_t PN532_CARDUID[4] = {0};
char PN532_UID[9] = {0};
uint8_t g_cmd_detect_card[] = {0x00, 0x00, 0xff, 0x04, 0xfc, 0xd4, 0x4a, 0x02, 0x00, 0xe0, 0x00};

int32_t CARD_NUMBER = 0; // 卡的数量

bool READY_ADD_CARD = 0; // 准备添加卡

char CARD_ID[20][11] = {0}; // 卡号，最多添加19个卡

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
#ifdef DEBUG
        ESP_EARLY_LOGI(TAG, "pn532模块中断触发, gpio_num=%u", gpio_num);
#endif
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
#ifdef DEBUG
    ESP_LOGI(TAG, "pn532 interrupt gpio configured");
#endif

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

    xTaskCreate(pn532_task, "pn532_task", 8192, NULL, 10, NULL);
    return ESP_OK;
}

/**
 * PN532发生命令和接收响应的函数
 * @param cmd 命令数据
 * @param cmd_len 命令长度
 * @param response 响应数据
 * @param response_len 响应长度
 * @note 该函数会先发送命令，然后延迟20ms，再接收响应数据
 */
void pn532_send_command_and_receive(const uint8_t *cmd, size_t cmd_len, uint8_t *response, size_t response_len)
{
    i2c_master_transmit(pn532_handle, cmd, cmd_len, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(20)); // 延迟等待模块处理
    i2c_master_receive(pn532_handle, response, response_len, portMAX_DELAY);
}
/**
 * 在卡号列表中查找指定卡号并返回其序号
 * @param card_id 要查找的卡号字符串
 * @param number 卡号数量
 * @return 若找到则返回卡号在列表中的序号(1~20)，未找到则返回0
 */
uint8_t find_card_id(const char *card_id, int number)
{
    // 检查参数有效性
    if (card_id == NULL || number == 0)
    {
        return 0;
    }

    // 遍历卡号列表查找匹配项
    for (uint8_t i = 1; i <= number; i++)
    {
        if (strcmp(CARD_ID[i], card_id) == 0)
        {
            return i; // 找到匹配项，返回序号
        }
    }

    return 0; // 未找到匹配项
}
// 刷卡任务
void pn532_task(void *arg)
{
    uint8_t res[19] = {0};
    uint8_t ack[7] = {0};

    while (1)
    {
        if (xSemaphoreTake(pn532_semaphore, portMAX_DELAY) == pdTRUE)
        {
            if (i2c_master_receive(pn532_handle, res, sizeof(res), 100) == ESP_OK && res[0] == 0x01)
            {
                uint8_t id_len = res[13];
                memcpy(PN532_CARDUID, &res[14], id_len);
                ESP_LOG_BUFFER_HEX("PN532_CARDUID", PN532_CARDUID, id_len);

                sprintf(PN532_UID, "%02X%02X%02X%02X", PN532_CARDUID[0], PN532_CARDUID[1], PN532_CARDUID[2], PN532_CARDUID[3]);

                ESP_LOGI(TAG, "识别到卡片，UID: %s", PN532_UID);
                // ssd1306_draw_string(oled_handle, 0, 16, PN532_UID, 16, 1);
                // ssd1306_refresh_gram(oled_handle);
                ESP_LOGI(TAG, "PN532_UID %s", PN532_UID);
                if (READY_ADD_CARD == 1) // 添加卡操作
                {
                    if (find_card_id(PN532_UID, CARD_NUMBER) == 0) // 没有此卡
                    {

                        uint8_t storage_index = CARD_NUMBER + 1; // 实际存储位置

                        // 复制卡号到数组
                        strncpy(CARD_ID[storage_index], PN532_UID, sizeof(CARD_ID[storage_index]) - 1);
                        CARD_ID[storage_index][sizeof(CARD_ID[storage_index]) - 1] = '\0';

                        ESP_LOGI(TAG, "添加卡片: %s 到位置: %u", CARD_ID[storage_index], storage_index);
                        CARD_NUMBER++; // 增加卡片数量
                        // write_nvs_i32("CARD_NUMBER", CARD_NUMBER);

                        char temp[20];
                        snprintf(temp, sizeof(temp), "CUID_ID%u", storage_index);
                        // write_nvs_string(temp, CARD_ID[storage_index]);
                        // aliot_post_property_int("theNumberOfCard", CARD_NUMBER);    // 上报卡片数量
                        // aliot_post_property_card("CardList", CARD_NUMBER, CARD_ID); // 上报卡片ID列表
                        READY_ADD_CARD = 0;
                    }
                    else // 存在此卡
                    {
                        ESP_LOGI(TAG, "卡片已存在！");
                        // xQueueSend(buzzer_queue, &BUZZER_NOOPEN, pdMS_TO_TICKS(10));
                    }
                }
                else
                {
                    ESP_LOGI(TAG, "CARD_NUMBER: %u", CARD_NUMBER);
                    ESP_LOGI(TAG, "PN532_UID: %s", PN532_UID);

                    if (find_card_id(PN532_UID, CARD_NUMBER) == 0)
                    {
                        ESP_LOGI(TAG, "卡片不存在！");
                        // xQueueSend(buzzer_queue, &BUZZER_NOOPEN, pdMS_TO_TICKS(10));
                    }
                    else
                    {
                        ESP_LOGI(TAG, "卡片存在！");
                        // xQueueSend(buzzer_queue, &BUZZER_OPEN, pdMS_TO_TICKS(10));
                    }
                }

                i2c_master_transmit(pn532_handle, g_cmd_detect_card, sizeof(g_cmd_detect_card), 500);
                vTaskDelay(pdMS_TO_TICKS(20));
                i2c_master_receive(pn532_handle, ack, sizeof(ack), 500);
                res[0] = 0x00;
                // ESP_LOG_BUFFER_HEX(TAG, ack, sizeof(ack));
            }

            xSemaphoreTake(pn532_semaphore, portMAX_DELAY);
        }
    }
}