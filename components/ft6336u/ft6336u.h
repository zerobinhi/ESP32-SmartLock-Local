#ifndef __FT6336U_DRIVER_H_
#define __FT6336U_DRIVER_H_

#include "driver/i2c_master.h"
#include "app_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/gpio.h>
#include "nvs_custom.h"

#define FT6336U_I2C_ADDRESS 0x38 // 设备I2C地址

// 寄存器地址定义
#define FT6336U_TD_STATUS 0x02 // 触摸状态寄存器
#define FT6336U_P1_XH 0x03     // 触摸点1 X坐标高8位
#define FT6336U_P1_XL 0x04     // 触摸点1 X坐标低8位
#define FT6336U_P1_YH 0x05     // 触摸点1 Y坐标高8位
#define FT6336U_P1_YL 0x06     // 触摸点1 Y坐标低8位
#define FT6336U_P2_XH 0x09     // 触摸点2 X坐标高8位
#define FT6336U_P2_XL 0x0A     // 触摸点2 X坐标低8位
#define FT6336U_P2_YH 0x0B     // 触摸点2 Y坐标高8位
#define FT6336U_P2_YL 0x0C     // 触摸点2 Y坐标低8位

extern bool g_gpio_isr_service_installed; // 是否安装了GPIO中断服务
extern i2c_master_bus_handle_t bus_handle;
extern i2c_master_dev_handle_t touch_handle;
extern QueueHandle_t password_queue; // 密码模块→蜂鸣器的消息队列
extern bool g_i2c_service_installed; // 是否安装了I2C服务

typedef struct
{
    uint8_t touch_num; // 触摸点数 (0-2)
    uint16_t touch0_x; // 触摸点1 X坐标
    uint16_t touch0_y; // 触摸点1 Y坐标
    uint16_t touch1_x; // 触摸点2 X坐标
    uint16_t touch1_y; // 触摸点2 Y坐标
} ft6336u_touch_pos;

esp_err_t ft6336u_read_touch_pos(ft6336u_touch_pos *touch_pos);
void touch_task(void *arg);
esp_err_t ft6336u_initialization();

#endif
