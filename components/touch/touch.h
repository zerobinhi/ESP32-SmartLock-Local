#ifndef __TOUCH_DRIVER_H_
#define __TOUCH_DRIVER_H_

#include <driver/touch_sens.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_check.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "nvs_custom.h"

#define TOUCH_PASSWORD_LEN 6
#define DEFAULT_PASSWORD "123456"
#define TOUCH_THRESH2BM_RATIO 0.3f

/**
 * @brief 初始化触摸按键驱动（12键）
 */
esp_err_t touch_initialization(void);

#endif // __TOUCH_DRIVER_H_
