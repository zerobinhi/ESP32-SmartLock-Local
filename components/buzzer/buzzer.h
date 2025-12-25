#ifndef BUZZER_H
#define BUZZER_H

#include <driver/gpio.h>
#include "app_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "zw111.h"

esp_err_t gpio_initialization();
void buzzer_task(void *pvParameters);
void app_send_buzzer_message(void *pvParameters);
void fingerprint_send_buzzer_message(void *pvParameters);
void password_send_buzzer_message(void *pvParameters);
void card_send_buzzer_message(void *pvParameters);
esp_err_t smart_lock_buzzer_init(void);

#endif
