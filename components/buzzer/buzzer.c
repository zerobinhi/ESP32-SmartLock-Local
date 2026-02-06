#include "buzzer.h"

QueueHandle_t buzzer_queue;      // Buzzer task receiving queue (collects trigger signals from all modules)
QueueHandle_t fingerprint_queue; // Fingerprint module → Buzzer message queue
QueueHandle_t password_queue;    // Password module → Buzzer message queue
QueueHandle_t app_queue;         // Remote APP → Buzzer message queue
QueueHandle_t card_queue;        // Card module → Buzzer message queue

static const char *TAG = "buzzer";

esp_err_t gpio_initialization(void)
{
    // Fingerprint LED GPIO
    gpio_config_t fingerprint_led_cfg = {
        .pin_bit_mask = (1ULL << FINGERPRINT_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&fingerprint_led_cfg);

    // Password LED GPIO
    gpio_config_t password_led_cfg = {
        .pin_bit_mask = (1ULL << PASSWORD_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&password_led_cfg);

    // Card LED GPIO
    gpio_config_t card_led_cfg = {
        .pin_bit_mask = (1ULL << CARD_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&card_led_cfg);

    // APP LED GPIO
    gpio_config_t app_led_cfg = {
        .pin_bit_mask = (1ULL << APP_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&app_led_cfg);

    // Lock control GPIO
    gpio_config_t lock_ctl_cfg = {
        .pin_bit_mask = (1ULL << LOCK_CTL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&lock_ctl_cfg);

    // Buzzer control GPIO
    gpio_config_t buzzer_ctl_cfg = {
        .pin_bit_mask = (1ULL << BUZZER_CTL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&buzzer_ctl_cfg);

    // Default states
    gpio_set_level(FINGERPRINT_LED_PIN, 1); // Turn off fingerprint LED (assuming HIGH=off, LOW=on)
    gpio_set_level(APP_LED_PIN, 1);         // Turn off APP LED
    gpio_set_level(PASSWORD_LED_PIN, 1);    // Turn off password LED
    gpio_set_level(CARD_LED_PIN, 1);        // Turn off card LED
    gpio_set_level(LOCK_CTL_PIN, 0);        // Lock default closed (LOW=power off)
    gpio_set_level(BUZZER_CTL_PIN, 1);      // Buzzer default off (HIGH=silent)

    ESP_LOGI(TAG, "GPIO initialized successfully");
    return ESP_OK;
}

void fingerprint_send_buzzer_message(void *pvParameters)
{
    uint8_t message; // 0=failure, 1=success
    while (1)
    {
        // Fix: receive message from fingerprint-specific queue (original code fine, adding timeout log)
        BaseType_t xRecvRet = xQueueReceive(
            fingerprint_queue,
            &message,
            portMAX_DELAY // Block indefinitely until message arrives (matches waiting for fingerprint trigger)
        );

        if (xRecvRet == pdTRUE)
        {
            if (message == 1)
            {                                           // Fingerprint verification success
                gpio_set_level(FINGERPRINT_LED_PIN, 0); // Turn on fingerprint LED
                ESP_LOGI(TAG, "Fingerprint verified successfully");

                // Send "success" signal to buzzer queue
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send success message to buzzer queue (fingerprint)");
                }
            }
            else // Fingerprint verification failed (message=0)
            {
                gpio_set_level(FINGERPRINT_LED_PIN, 1); // Ensure LED off
                ESP_LOGW(TAG, "Fingerprint verification failed");

                // Send "failure" signal to buzzer queue
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send fail message to buzzer queue (fingerprint)");
                }
            }

            vTaskDelay(pdMS_TO_TICKS(600));         // Delay 600ms then turn off fingerprint module
            gpio_set_level(FINGERPRINT_LED_PIN, 1); // Turn off fingerprint LED
            prepare_turn_off_fingerprint();         // Power down fingerprint module
        }
    }
}

void password_send_buzzer_message(void *pvParameters)
{
    uint8_t message; // 0=failure, 1=success
    while (1)
    {
        // Fix: receive message from password-specific queue
        BaseType_t xRecvRet = xQueueReceive(password_queue, &message, portMAX_DELAY);

        if (xRecvRet == pdTRUE)
        {
            if (message == 1)
            {                                        // Password verification success
                gpio_set_level(PASSWORD_LED_PIN, 0); // Turn on password LED
                ESP_LOGI(TAG, "Password verified successfully");

                // Send "success" signal to buzzer queue
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send success message to buzzer queue (password)");
                }

                vTaskDelay(pdMS_TO_TICKS(800));      // Keep LED on for 800ms
                gpio_set_level(PASSWORD_LED_PIN, 1); // Turn off password LED
            }
            else
            {                                        // Password verification failed
                gpio_set_level(PASSWORD_LED_PIN, 1); // Ensure LED off
                ESP_LOGW(TAG, "Password verification failed");

                // Send "failure" signal to buzzer queue
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
    uint8_t message; // 0=failure, 1=success
    while (1)
    {
        // Fix: receive message from card-specific queue
        BaseType_t xRecvRet = xQueueReceive(card_queue, &message, portMAX_DELAY);

        if (xRecvRet == pdTRUE)
        {
            if (message == 1)
            {                                    // Card verification success
                gpio_set_level(CARD_LED_PIN, 0); // Turn on card LED
                ESP_LOGI(TAG, "Card verified successfully");

                // Send "success" signal to buzzer queue
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send success message to buzzer queue (card)");
                }

                vTaskDelay(pdMS_TO_TICKS(800));  // Keep LED on for 800ms
                gpio_set_level(CARD_LED_PIN, 1); // Turn off card LED
            }
            else
            {                                    // Card verification failed
                gpio_set_level(CARD_LED_PIN, 1); // Ensure LED off
                ESP_LOGW(TAG, "Card verification failed");

                // Send "failure" signal to buzzer queue
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send fail message to buzzer queue (card)");
                }
            }
        }
    } // Fixed missing closing bracket from original code
}

void app_send_buzzer_message(void *pvParameters)
{
    uint8_t message; // 0=failure, 1=success
    while (1)
    {
        // Receive message from APP-specific queue (e.g., remote unlock command)
        BaseType_t xRecvRet = xQueueReceive(app_queue, &message, portMAX_DELAY);

        if (xRecvRet == pdTRUE)
        {
            if (message == 1)
            {                                   // APP remote unlock succeeded
                gpio_set_level(APP_LED_PIN, 0); // Turn on APP LED
                ESP_LOGI(TAG, "APP remote unlock succeeded");

                // Send "success" signal to buzzer queue
                if (xQueueSend(buzzer_queue, &message, portMAX_DELAY) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to send success message to buzzer queue (APP)");
                }

                vTaskDelay(pdMS_TO_TICKS(800)); // Keep LED on for 800ms
                gpio_set_level(APP_LED_PIN, 1); // Turn off APP LED
            }
            else
            {                                   // APP remote unlock failed
                gpio_set_level(APP_LED_PIN, 1); // Ensure LED off
                ESP_LOGW(TAG, "APP remote unlock failed");

                // Send "failure" signal to buzzer queue
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
    uint8_t message; // Receive signals from all modules (0=failure, 1=success)
    while (1)
    {
        // Receive signal from buzzer queue (unified processing of all module triggers)
        BaseType_t xRecvRet = xQueueReceive(buzzer_queue, &message, portMAX_DELAY);

        if (xRecvRet == pdTRUE)
        {
            ESP_LOGI(TAG, "Buzzer received message: %u (1=success, 0=failure)", message);
            if (message == 1)
            { // Unlock success: long beep 1s + unlock
                // gpio_set_level(BUZZER_CTL_PIN, 0); // Turn on buzzer (LOW=active)
                gpio_set_level(LOCK_CTL_PIN, 1); // Power ON electromagnetic lock
                ESP_LOGI(TAG, "Buzzer beeping (success) + Lock unlocked");

                vTaskDelay(pdMS_TO_TICKS(1000)); // Keep lock powered 1s
                // gpio_set_level(BUZZER_CTL_PIN, 1); // Turn off buzzer
                gpio_set_level(LOCK_CTL_PIN, 0); // Power OFF lock
                ESP_LOGI(TAG, "Buzzer stopped + Lock locked");
            }
            else if (message == 0)
            {
                // Unlock failure: short beep twice (200ms beep + 100ms pause)
                ESP_LOGI(TAG, "Buzzer beeping (failure)");
                // First beep
                // gpio_set_level(BUZZER_CTL_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                // gpio_set_level(BUZZER_CTL_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                // Second beep
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

    task_ret = xTaskCreate(fingerprint_send_buzzer_message, "fingerprint_task", 4096, NULL, 10, NULL);

    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create fingerprint_task");
        return ESP_FAIL;
    }

    task_ret = xTaskCreate(password_send_buzzer_message, "password_task", 4096, NULL, 10, NULL);

    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create password_task");
        return ESP_FAIL;
    }

    task_ret = xTaskCreate(card_send_buzzer_message, "card_task", 4096, NULL, 10, NULL);

    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create card_task");
        return ESP_FAIL;
    }

    task_ret = xTaskCreate(app_send_buzzer_message, "app_task", 4096, NULL, 10, NULL);

    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create app_task");
        return ESP_FAIL;
    }

    task_ret = xTaskCreate(buzzer_task, "buzzer_task", 4096, NULL, 11, NULL);

    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create buzzer_task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "All tasks and queues initialized successfully");
    return ESP_OK;
}
