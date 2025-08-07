#ifndef __FT6336U_DRIVER_H_
#define __FT6336U_DRIVER_H_

#include "driver/i2c_master.h"
#include "esp_err.h"

#define FT6336U_I2C_ADDR 0x38 // 设备I2C地址

extern i2c_master_dev_handle_t touch_handle;

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

typedef struct
{
    uint8_t touch_num; // 触摸点数 (0-2)
    uint16_t touch0_x; // 触摸点1 X坐标
    uint16_t touch0_y; // 触摸点1 Y坐标
    uint16_t touch1_x; // 触摸点2 X坐标
    uint16_t touch1_y; // 触摸点2 Y坐标
} FT6336U_TOUCH_POS;

esp_err_t ft6336u_read_touch_pos(FT6336U_TOUCH_POS *touch_pos);

#endif // __FT6336U_DRIVER_H_