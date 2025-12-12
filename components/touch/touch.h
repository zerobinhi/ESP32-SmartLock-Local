#ifndef __TOUCH_DRIVER_H_
#define __TOUCH_DRIVER_H_

#include <driver/touch_sens.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_check.h>

/**
 * @brief 初始化触摸按键驱动（12键）
 */
esp_err_t app_touch_initialization(void);

/**
 * @brief 根据硬件触摸通道号返回键值字符
 * @return '1'~'9', '*', '0', '#', 或 0（未找到）
 */
char touch_key_from_channel(uint8_t channel);

#endif // __TOUCH_DRIVER_H_
