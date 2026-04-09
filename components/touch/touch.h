#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include <driver/touch_sens.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "nvs_custom.h"
#include "app_config.h"

#define TOUCH_THRESH2BM_RATIO 0.4f
extern QueueHandle_t password_queue;

esp_err_t touch_initialization(void);

#endif // __TOUCH_DRIVER_H_
