#include "buzzer.h"

QueueHandle_t buzzer_queue;      // 蜂鸣器任务接收队列（统一接收所有模块的触发信号）
QueueHandle_t fingerprint_queue; // 指纹模块→蜂鸣器的消息队列
QueueHandle_t password_queue;    // 密码模块→蜂鸣器的消息队列
QueueHandle_t app_queue;         // 远程APP→蜂鸣器的消息队列
QueueHandle_t card_queue;        // 刷卡模块→蜂鸣器的消息队列

static const char *TAG = "SmartLock Buzzer";

esp_err_t gpio_initialization(void)
{
    // 指纹指示灯GPIO配置
    gpio_config_t fingerprint_led_cfg = {
        .pin_bit_mask = (1ULL << FINGERPRINT_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&fingerprint_led_cfg);

    // APP指示灯GPIO配置
    gpio_config_t app_led_cfg = {
        .pin_bit_mask = (1ULL << APP_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&app_led_cfg);

    // 密码指示灯GPIO配置
    gpio_config_t password_led_cfg = {
        .pin_bit_mask = (1ULL << PASSWORD_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&password_led_cfg);

    // 刷卡指示灯GPIO配置
    gpio_config_t card_led_cfg = {
        .pin_bit_mask = (1ULL << CARD_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&card_led_cfg);

    // 电磁锁GPIO配置
    gpio_config_t lock_ctl_cfg = {
        .pin_bit_mask = (1ULL << LOCK_CTL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&lock_ctl_cfg);

    // 蜂鸣器GPIO配置
    gpio_config_t buzzer_ctl_cfg = {
        .pin_bit_mask = (1ULL << BUZZER_CTL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&buzzer_ctl_cfg);

    // 初始化引脚电平（默认状态）
    gpio_set_level(FINGERPRINT_LED_PIN, 1); // 熄灭指纹灯（假设高电平熄灭、低电平点亮）
    gpio_set_level(APP_LED_PIN, 1);         // 熄灭APP灯
    gpio_set_level(PASSWORD_LED_PIN, 1);    // 熄灭密码灯
    gpio_set_level(CARD_LED_PIN, 1);        // 熄灭刷卡灯
    gpio_set_level(LOCK_CTL_PIN, 0);        // 锁默认关闭（低电平断电）
    gpio_set_level(BUZZER_CTL_PIN, 1);      // 蜂鸣器默认关闭（高电平静音）

    ESP_LOGI(TAG, "GPIO initialized successfully");
    return ESP_OK;
}

void fingerprint_send_buzzer_message(void *pvParameters)
{
    uint8_t message; // 0=失败，1=成功
    while (1)
    {
        // 修复：从指纹专属队列接收消息（原代码无问题，补充超时日志）
        BaseType_t xRecvRet = xQueueReceive(
            fingerprint_queue,
            &message,
            portMAX_DELAY // 永久阻塞，直到有消息（符合等待指纹触发的逻辑）
        );

        if (xRecvRet == pdTRUE)
        {
            if (message == 1)
            {                                           // 指纹验证成功
                gpio_set_level(FINGERPRINT_LED_PIN, 0); // 点亮指纹灯
                ESP_LOGI(TAG, "Fingerprint verified successfully");

                // 向蜂鸣器队列发送“成功”信号
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send success message to buzzer queue (fingerprint)");
                }
            }
            else // 指纹验证失败（message=0）
            {
                gpio_set_level(FINGERPRINT_LED_PIN, 1); // 确保灯熄灭
                ESP_LOGW(TAG, "Fingerprint verification failed");

                // 向蜂鸣器队列发送“失败”信号
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send fail message to buzzer queue (fingerprint)");
                }
            }

            vTaskDelay(pdMS_TO_TICKS(600));         // 延迟800ms之后关闭指纹模块
            gpio_set_level(FINGERPRINT_LED_PIN, 1); // 熄灭指纹灯
            prepare_turn_off_fingerprint();         // 关闭指纹模块
        }
    }
}

void password_send_buzzer_message(void *pvParameters)
{
    uint8_t message; // 0=失败，1=成功
    while (1)
    {
        // 修复：从密码专属队列接收消息
        BaseType_t xRecvRet = xQueueReceive(password_queue, &message, portMAX_DELAY);

        if (xRecvRet == pdTRUE)
        {
            if (message == 1)
            {                                        // 密码验证成功
                gpio_set_level(PASSWORD_LED_PIN, 0); // 点亮密码灯
                ESP_LOGI(TAG, "Password verified successfully");

                // 向蜂鸣器队列发送“成功”信号
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send success message to buzzer queue (password)");
                }

                vTaskDelay(pdMS_TO_TICKS(800));      // 保持灯亮800ms
                gpio_set_level(PASSWORD_LED_PIN, 1); // 熄灭密码灯
            }
            else
            {                                        // 密码验证失败
                gpio_set_level(PASSWORD_LED_PIN, 1); // 确保灯熄灭
                ESP_LOGW(TAG, "Password verification failed");

                // 向蜂鸣器队列发送“失败”信号
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send fail message to buzzer queue (password)");
                }
            }
        }
    }
}

void card_send_buzzer_message(void *pvParameters)
{
    uint8_t message; // 0=失败，1=成功
    while (1)
    {
        // 修复：从刷卡专属队列接收消息
        BaseType_t xRecvRet = xQueueReceive(card_queue, &message, portMAX_DELAY);

        if (xRecvRet == pdTRUE)
        {
            if (message == 1)
            {                                    // 刷卡验证成功
                gpio_set_level(CARD_LED_PIN, 0); // 点亮刷卡灯
                ESP_LOGI(TAG, "Card verified successfully");

                // 向蜂鸣器队列发送“成功”信号
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send success message to buzzer queue (card)");
                }

                vTaskDelay(pdMS_TO_TICKS(800));  // 保持灯亮800ms
                gpio_set_level(CARD_LED_PIN, 1); // 熄灭刷卡灯
            }
            else
            {                                    // 刷卡验证失败
                gpio_set_level(CARD_LED_PIN, 1); // 确保灯熄灭
                ESP_LOGW(TAG, "Card verification failed");

                // 向蜂鸣器队列发送“失败”信号
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send fail message to buzzer queue (card)");
                }
            }
        }
    } // 原代码缺少此括号，导致语法错误
}

void app_send_buzzer_message(void *pvParameters)
{
    uint8_t message; // 0=失败，1=成功
    while (1)
    {
        // 从APP专属队列接收消息（如远程开锁指令）
        BaseType_t xRecvRet = xQueueReceive(app_queue, &message, portMAX_DELAY);

        if (xRecvRet == pdTRUE)
        {
            if (message == 1)
            {                                   // APP远程开锁成功
                gpio_set_level(APP_LED_PIN, 0); // 点亮APP灯
                ESP_LOGI(TAG, "APP remote unlock succeeded");

                // 向蜂鸣器队列发送“成功”信号
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send success message to buzzer queue (APP)");
                }

                vTaskDelay(pdMS_TO_TICKS(800)); // 保持灯亮800ms
                gpio_set_level(APP_LED_PIN, 1); // 熄灭APP灯
            }
            else
            {                                   // APP远程开锁失败
                gpio_set_level(APP_LED_PIN, 1); // 确保灯熄灭
                ESP_LOGW(TAG, "APP remote unlock failed");

                // 向蜂鸣器队列发送“失败”信号
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send fail message to buzzer queue (APP)");
                }
            }
        }
    }
}

void buzzer_task(void *pvParameters)
{
    uint8_t message; // 接收各模块的信号（0=失败，1=成功）
    while (1)
    {
        // 从蜂鸣器队列接收信号（统一处理所有模块的触发）
        BaseType_t xRecvRet = xQueueReceive(buzzer_queue, &message, portMAX_DELAY);

        if (xRecvRet == pdTRUE)
        {
            ESP_LOGI(TAG, "Buzzer received message: %u (1=success, 0=failure)", message);
            if (message == 1)
            { // 开门成功：长鸣1秒+开锁
                // gpio_set_level(BUZZER_CTL_PIN, 0); // 打开蜂鸣器（低电平响）
                gpio_set_level(LOCK_CTL_PIN, 1); // 电磁锁通电开锁
                ESP_LOGI(TAG, "Buzzer beeping (success) + Lock unlocked");

                vTaskDelay(pdMS_TO_TICKS(1000)); // 保持开锁1秒
                // gpio_set_level(BUZZER_CTL_PIN, 1); // 关闭蜂鸣器
                gpio_set_level(LOCK_CTL_PIN, 0); // 电磁锁断电关锁
                ESP_LOGI(TAG, "Buzzer stopped + Lock locked");
            }
            else if (message == 0)
            {
                // 开门失败：短鸣2次（200ms响+100ms停）
                ESP_LOGI(TAG, "Buzzer beeping (failure)");
                // 第一次鸣叫
                // gpio_set_level(BUZZER_CTL_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                // gpio_set_level(BUZZER_CTL_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                // 第二次鸣叫
                // gpio_set_level(BUZZER_CTL_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                // gpio_set_level(BUZZER_CTL_PIN, 1);
                ESP_LOGI(TAG, "Buzzer stopped (failure)");
            }
        }
    }
}

esp_err_t smart_lock_buzzer_init(void)
{
    esp_err_t gpio_ret = gpio_initialization();
    if (gpio_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO initialization failed");
        return gpio_ret;
    }

    buzzer_queue = xQueueCreate(8, sizeof(uint8_t));
    if (buzzer_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create buzzer_queue");
        return ESP_FAIL;
    }

    fingerprint_queue = xQueueCreate(4, sizeof(uint8_t));
    if (fingerprint_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create fingerprint_queue");
        return ESP_FAIL;
    }

    password_queue = xQueueCreate(4, sizeof(uint8_t));
    if (password_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create password_queue");
        return ESP_FAIL;
    }

    app_queue = xQueueCreate(4, sizeof(uint8_t));
    if (app_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create app_queue");
        return ESP_FAIL;
    }

    card_queue = xQueueCreate(4, sizeof(uint8_t));
    if (card_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create card_queue");
        return ESP_FAIL;
    }

    BaseType_t task_ret;

    task_ret = xTaskCreate(
        fingerprint_send_buzzer_message, // 任务函数
        "fingerprint_task",              // 任务名称
        4096,                            // 栈大小
        NULL,                            // 任务参数
        10,                              // 优先级
        NULL                             // 任务句柄（不保存）
    );
    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create fingerprint_task");
        return ESP_FAIL;
    }

    task_ret = xTaskCreate(
        password_send_buzzer_message,
        "password_task",
        4096,
        NULL,
        10,
        NULL);
    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create password_task");
        return ESP_FAIL;
    }

    task_ret = xTaskCreate(
        card_send_buzzer_message,
        "card_task",
        4096,
        NULL,
        10,
        NULL);
    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create card_task");
        return ESP_FAIL;
    }

    task_ret = xTaskCreate(
        app_send_buzzer_message,
        "app_task",
        4096,
        NULL,
        10,
        NULL);
    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create app_task");
        return ESP_FAIL;
    }

    task_ret = xTaskCreate(
        buzzer_task,
        "buzzer_task",
        4096,
        NULL,
        11, // 蜂鸣器任务优先级略高，确保及时响应
        NULL);
    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create buzzer_task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "All tasks and queues initialized successfully");
    return ESP_OK;
}
