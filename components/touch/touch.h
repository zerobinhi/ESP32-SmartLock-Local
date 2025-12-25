#ifndef __TOUCH_DRIVER_H_
#define __TOUCH_DRIVER_H_

#include <driver/touch_sens.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "nvs_custom.h"
#include "app_config.h"

#define TOUCH_THRESH2BM_RATIO 0.3f

/**
 * @brief 初始化触摸按键驱动（12键）
 */
esp_err_t touch_initialization(void);

#endif // __TOUCH_DRIVER_H_
