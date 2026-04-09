#ifndef SLEEP_H
#define SLEEP_H

#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include "nvs_custom.h"
#include "app_config.h"
#include "zw111.h"

extern struct fingerprint_device zw111;
extern char g_input_password[TOUCH_PASSWORD_LEN + 1];
extern uint8_t g_input_len;
extern SemaphoreHandle_t si14tp_semaphore;
extern SemaphoreHandle_t fingerprint_semaphore;
extern SemaphoreHandle_t si523_semaphore;
extern void cancel_current_operation_and_execute_command();
extern void prepare_turn_off_fingerprint();
extern bool g_touch_wakeup_flag;
extern i2c_master_dev_handle_t pn7160_handle;
extern SemaphoreHandle_t pn7160_semaphore;
extern TaskHandle_t pn7160_task_handle;

void notify_user_activity(void);
esp_err_t sleep_initialization(void);
extern void pn7160_task(void *arg);

#endif // SLEEP_H