#ifndef __TOUCH_DRIVER_H_
#define __TOUCH_DRIVER_H_

#include "driver/touch_sens.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ====================================================================
// 用户配置宏 (根据您的硬件连接和需求调整)
// ====================================================================

// 触摸通道 ID。请根据您连接触摸引脚的GPIO号，查找对应的TOUCH_PAD_NUMx
// 这里的 TOUCH_MIN_CHAN_ID + 4 对应您例程中的通道选择
#define APP_TOUCH_CHANNEL TOUCH_MIN_CHAN_ID + 4 

// 触摸灵敏度参数
// 0.515f (51.5%) 是一个非常高的阈值，通常用于非常不敏感的按键
// 如果需要更敏感的按键，请将此值调小，例如 0.05f (5%)
#define APP_TOUCH_THRESH2BM_RATIO 0.515f

// ====================================================================
// 外部接口
// ====================================================================

/**
 * @brief 触摸按键初始化函数
 * * 配置 ESP 原生触摸传感器驱动，执行初始扫描和动态阈值设置。
 * * @return esp_err_t 
 */
esp_err_t app_touch_initialization(void);

#endif // __TOUCH_DRIVER_H_