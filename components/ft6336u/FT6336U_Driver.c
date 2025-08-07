#include "FT6336U_Driver.h"
#include "esp_log.h"

#define TAG "FT6336U"

// I2C读取单个寄存器
static esp_err_t i2c_read_register(uint8_t reg_addr, uint8_t *data)
{
    return i2c_master_transmit_receive(touch_handle, &reg_addr, 1, data, 1, 10);
}

// 读取触摸数据
esp_err_t ft6336u_read_touch_pos(FT6336U_TOUCH_POS *touch_pos)
{
    if (touch_pos == NULL || touch_handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data;

    // 读取触摸点数
    esp_err_t err = i2c_read_register(FT6336U_TD_STATUS, &data);
    if (err != ESP_OK)
    {
        return err;
    }
    touch_pos->touch_num = data & 0x0F;

    // 读取触摸点1坐标
    err = i2c_read_register(FT6336U_P1_XH, &data);
    if (err != ESP_OK)
    {
        return err;
    }
    uint8_t xh = data & 0x0F;

    err = i2c_read_register(FT6336U_P1_XL, &data);
    if (err != ESP_OK)
    {
        return err;
    }
    touch_pos->touch0_x = (xh << 8) | data;

    err = i2c_read_register(FT6336U_P1_YH, &data);
    if (err != ESP_OK)
    {
        return err;
    }
    uint8_t yh = data & 0x0F;

    err = i2c_read_register(FT6336U_P1_YL, &data);
    if (err != ESP_OK)
    {
        return err;
    }
    touch_pos->touch0_y = (yh << 8) | data;

    // 如果有第二个触摸点，读取其坐标
    if (touch_pos->touch_num >= 2)
    {
        err = i2c_read_register(FT6336U_P2_XH, &data);
        if (err != ESP_OK)
        {
            return err;
        }
        xh = data & 0x0F;

        err = i2c_read_register(FT6336U_P2_XL, &data);
        if (err != ESP_OK)
        {
            return err;
        }
        touch_pos->touch1_x = (xh << 8) | data;

        err = i2c_read_register(FT6336U_P2_YH, &data);
        if (err != ESP_OK)
        {
            return err;
        }
        yh = data & 0x0F;

        err = i2c_read_register(FT6336U_P2_YL, &data);
        if (err != ESP_OK)
        {
            return err;
        }
        touch_pos->touch1_y = (yh << 8) | data;
    }
    else
    {
        touch_pos->touch1_x = 0;
        touch_pos->touch1_y = 0;
    }

    return ESP_OK;
}