#include "ft6336u.h"

static const char *TAG = "SmartLock FT6336U";

SemaphoreHandle_t touch_semaphore;
i2c_master_dev_handle_t touch_handle;

// 解锁密码
char g_touch_password[7] = {0};

// 当前输入的密码
char g_input_password[7] = {0};

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    // ESP_EARLY_LOGI(TAG, "ft6336u模块中断触发, gpio_num=%u", gpio_num);
    // if (gpio_num == FT6336U_INT_PIN && gpio_get_level(FT6336U_INT_PIN) == 0)
    // {
    //     xSemaphoreGiveFromISR(touch_semaphore, NULL);
    // }
}
// 键盘矩阵
const uint8_t g_matrix_keyboard[4][3] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}};

// I2C读取单个寄存器
static esp_err_t i2c_read_register(uint8_t reg_addr, uint8_t *data)
{
    return i2c_master_transmit_receive(touch_handle, &reg_addr, 1, data, 1, portMAX_DELAY);
}

// 读取触摸数据
esp_err_t ft6336u_read_touch_pos(ft6336u_touch_pos *touch_pos)
{
    if (touch_pos == NULL || touch_handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data;

    // 读取触摸点数
    esp_err_t err = i2c_read_register(FT6336U_TD_STATUS, &data);
    if (err != ESP_OK)
    {
        return err;
    }
    touch_pos->touch_num = data & 0x0F;

    // 读取触摸点1坐标
    err = i2c_read_register(FT6336U_P1_XH, &data);
    if (err != ESP_OK)
    {
        return err;
    }
    uint8_t xh = data & 0x0F;

    err = i2c_read_register(FT6336U_P1_XL, &data);
    if (err != ESP_OK)
    {
        return err;
    }
    touch_pos->touch0_x = (xh << 8) | data;

    err = i2c_read_register(FT6336U_P1_YH, &data);
    if (err != ESP_OK)
    {
        return err;
    }
    uint8_t yh = data & 0x0F;

    err = i2c_read_register(FT6336U_P1_YL, &data);
    if (err != ESP_OK)
    {
        return err;
    }
    touch_pos->touch0_y = (yh << 8) | data;

    // 如果有第二个触摸点，读取其坐标
    if (touch_pos->touch_num >= 2)
    {
        err = i2c_read_register(FT6336U_P2_XH, &data);
        if (err != ESP_OK)
        {
            return err;
        }
        xh = data & 0x0F;

        err = i2c_read_register(FT6336U_P2_XL, &data);
        if (err != ESP_OK)
        {
            return err;
        }
        touch_pos->touch1_x = (xh << 8) | data;

        err = i2c_read_register(FT6336U_P2_YH, &data);
        if (err != ESP_OK)
        {
            return err;
        }
        yh = data & 0x0F;

        err = i2c_read_register(FT6336U_P2_YL, &data);
        if (err != ESP_OK)
        {
            return err;
        }
        touch_pos->touch1_y = (yh << 8) | data;
    }
    else
    {
        touch_pos->touch1_x = 0;
        touch_pos->touch1_y = 0;
    }

    return ESP_OK;
}

/**
 * @brief 输入密码任务
 * @param arg 任务参数（未使用，传入NULL）
 * @return void
 */
void touch_task(void *arg)
{
    uint8_t row, col;
    ft6336u_touch_pos touch;
    uint8_t password_index = 0;
    while (1)
    {
        // 等待信号量被释放
        if (xSemaphoreTake(touch_semaphore, portMAX_DELAY) == pdTRUE)
        {
            ft6336u_read_touch_pos(&touch);
            row = touch.touch0_y / 80;
            col = touch.touch0_x / 80;
            if (touch.touch_num == 0)
            {
                // ESP_LOGI("触摸按键: ", "%c", g_matrix_keyboard[row][col]);

                if (g_matrix_keyboard[row][col] == '*')
                {
                    if (password_index > 0)
                    {
                        password_index--;
                        g_input_password[password_index] = '\0';
                        // ssd1306_draw_3216char(oled_handle, password_index * 16, 32, ';');
                        // ssd1306_refresh_gram(oled_handle);
                    }
                }
                else if (g_matrix_keyboard[row][col] == '#')
                {
                    if (password_index == 6)
                    {
                        password_index = 0;
                        ESP_LOGI(TAG, "提交: %s", g_input_password);
                        if (strcmp(g_touch_password, g_input_password) == 0)
                        {
                            ESP_LOGI(TAG, "密码正确，开门！");
                            uint8_t message = 0x01;
                            xQueueSend(password_queue, &message, portMAX_DELAY);
                            // ssd1306_fill_rectangle(oled_handle, 0, 32, 128, 64, 0);
                            // ssd1306_refresh_gram(oled_handle);
                        }
                        else
                        {
                            ESP_LOGI(TAG, "密码错误！");
                            // ssd1306_fill_rectangle(oled_handle, 0, 32, 128, 64, 0);
                            // ssd1306_refresh_gram(oled_handle);
                            uint8_t message = 0x00;
                            xQueueSend(password_queue, &message, portMAX_DELAY);
                        }
                        memset(g_input_password, 0, sizeof(g_input_password));
                    }
                }
                else
                {
                    if (password_index < 6)
                    {
                        // ssd1306_draw_3216char(oled_handle, password_index * 16, 32, g_matrix_keyboard[row][col]);
                        // ssd1306_refresh_gram(oled_handle);
                        g_input_password[password_index] = g_matrix_keyboard[row][col];
                        password_index++;
                    }
                }

                ESP_LOGI(TAG, "当前的索引: %d", password_index);
                ESP_LOGI(TAG, "输入的密码: %s", g_input_password);

                // xQueueSend(xQueue_buzzer, &BUZZER_TOUCH, pdMS_TO_TICKS(10));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief 初始化FT6336U模块I2C通信
 * @param 无
 * @return esp_err_t ESP_OK=初始化成功，ESP_FAIL=初始化失败
 */
esp_err_t ft6336u_initialization()
{
    // 创建信号量
    touch_semaphore = xSemaphoreCreateBinary();
    if (touch_semaphore == NULL)
    {
        ESP_LOGE(TAG, "Failed to create semaphore");
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
        .device_address = FT6336U_I2C_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &touch_handle));
    ESP_LOGI(TAG, "ft6336u device created");

    // gpio_config_t ft6336u_reset_gpio_config = {
    //     .pin_bit_mask = (1ULL << FT6336U_RST_PIN),
    //     .mode = GPIO_MODE_OUTPUT,
    //     .pull_up_en = GPIO_PULLUP_DISABLE,
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .intr_type = GPIO_INTR_DISABLE};
    // gpio_config(&ft6336u_reset_gpio_config);

    // gpio_config_t ft6336u_int_gpio_config = {
    //     .pin_bit_mask = (1ULL << FT6336U_INT_PIN),
    //     .mode = GPIO_MODE_INPUT,
    //     .pull_up_en = GPIO_PULLUP_ENABLE,
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .intr_type = GPIO_INTR_NEGEDGE};
    // gpio_config(&ft6336u_int_gpio_config);

    if (g_gpio_isr_service_installed == false)
    {
        gpio_install_isr_service(0);
        g_gpio_isr_service_installed = true;
    }
    // gpio_isr_handler_add(FT6336U_INT_PIN, gpio_isr_handler, (void *)FT6336U_INT_PIN);

    // gpio_set_level(FT6336U_RST_PIN, 0);
    // vTaskDelay(pdMS_TO_TICKS(100));
    // gpio_set_level(FT6336U_RST_PIN, 1);
    // vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "ft6336u interrupt gpio configured");

    size_t buf_len = sizeof(g_touch_password);

    // 读取存在nvs的密码信息
    if (nvs_custom_get_str(NULL, "pwd", "password", g_touch_password, &buf_len) != ESP_OK)
    {
        ESP_LOGI(TAG, "还未设置密码，初始密码为：000000");
        strcpy(g_touch_password, "000000");
        nvs_custom_set_str(NULL, "pwd", "password", g_touch_password);
    }

    // 打印当前的密码
    ESP_LOGI(TAG, "Current password is: %s", g_touch_password);

    // 创建任务
    xTaskCreate(touch_task, "touch_task", 8192, NULL, 10, NULL);
    return ESP_OK;
}
