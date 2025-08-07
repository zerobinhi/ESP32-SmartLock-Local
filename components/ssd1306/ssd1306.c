/*
 * SPDX-FileCopyrightText: 2015-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <driver/i2c_master.h>
#include "ssd1306.h"
#include <string.h> // for memset

#define SSD1306_WRITE_CMD (0x00)
#define SSD1306_WRITE_DAT (0x40)

#define COORDINATE_SWAP(x1, x2, y1, y2) \
    {                                   \
        int16_t temp = x1;              \
        x1 = x2, x2 = temp;             \
        temp = y1;                      \
        y1 = y2;                        \
        y2 = temp;                      \
    }

uint8_t s_chDisplayBuffer[128][8];

static uint32_t _pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;
    while (n--)
    {
        result *= m;
    }
    return result;
}

static esp_err_t ssd1306_write_data(i2c_master_dev_handle_t oled_handle, const uint8_t *const data, const uint16_t data_len)
{
    // 使用局部变量，减少动态内存的分配开销
    uint8_t data_wr[data_len + 1];
    data_wr[0] = SSD1306_WRITE_DAT; // 控制字节，代表数据

    // 将 data 的内容复制到 data_wr[1] 开始的位置
    memcpy(data_wr + 1, data, data_len);

    // 发送组合后的 data_wr 数组
    ESP_ERROR_CHECK(i2c_master_transmit(oled_handle, data_wr, data_len + 1, -1));

    return ESP_OK;
}
static esp_err_t ssd1306_write_cmd(i2c_master_dev_handle_t oled_handle, const uint8_t *const data, const uint16_t data_len)
{
    // 使用局部变量，减少动态内存的分配开销
    uint8_t data_wr[data_len + 1];
    data_wr[0] = SSD1306_WRITE_CMD; // 控制字节，代表命令

    // 将 data 的内容复制到 data_wr[1] 开始的位置
    memcpy(data_wr + 1, data, data_len);

    // 发送组合后的 data_wr 数组
    ESP_ERROR_CHECK(i2c_master_transmit(oled_handle, data_wr, data_len + 1, -1));

    return ESP_OK;
}

static inline esp_err_t ssd1306_write_cmd_byte(i2c_master_dev_handle_t oled_handle, const uint8_t cmd)
{
    return ssd1306_write_cmd(oled_handle, &cmd, 1);
}

void ssd1306_fill_rectangle(i2c_master_dev_handle_t oled_handle, uint8_t chXpos1,
                            uint8_t chYpos1, uint8_t chXpos2, uint8_t chYpos2, uint8_t chDot)
{
    uint8_t chXpos, chYpos;

    for (chXpos = chXpos1; chXpos <= chXpos2; chXpos++)
    {
        for (chYpos = chYpos1; chYpos <= chYpos2; chYpos++)
        {
            ssd1306_fill_point(oled_handle, chXpos, chYpos, chDot);
        }
    }
}

void ssd1306_draw_num(i2c_master_dev_handle_t oled_handle, uint8_t chXpos, uint8_t chYpos,
                      uint32_t chNum, uint8_t chLen, uint8_t chSize)
{
    uint8_t i;
    uint8_t chTemp, chShow = 0;

    for (i = 0; i < chLen; i++)
    {
        chTemp = (chNum / _pow(10, chLen - i - 1)) % 10;
        if (chShow == 0 && i < (chLen - 1))
        {
            if (chTemp == 0)
            {
                ssd1306_draw_char(oled_handle, chXpos + (chSize / 2) * i, chYpos,
                                  ' ', chSize, 1);
                continue;
            }
            else
            {
                chShow = 1;
            }
        }
        ssd1306_draw_char(oled_handle, chXpos + (chSize / 2) * i, chYpos,
                          chTemp + '0', chSize, 1);
    }
}

void ssd1306_draw_char(i2c_master_dev_handle_t oled_handle, uint8_t chXpos, uint8_t chYpos,
                       uint8_t chChr, uint8_t chSize, uint8_t chMode)
{
    uint8_t i, j;
    uint8_t chTemp, chYpos0 = chYpos;

    chChr = chChr - ' ';
    for (i = 0; i < chSize; i++)
    {
        if (chSize == 12)
        {
            if (chMode)
            {
                chTemp = c_chFont1206[chChr][i];
            }
            else
            {
                chTemp = ~c_chFont1206[chChr][i];
            }
        }
        else
        {
            if (chMode)
            {
                chTemp = c_chFont1608[chChr][i];
            }
            else
            {
                chTemp = ~c_chFont1608[chChr][i];
            }
        }

        for (j = 0; j < 8; j++)
        {
            if (chTemp & 0x80)
            {
                ssd1306_fill_point(oled_handle, chXpos, chYpos, 1);
            }
            else
            {
                ssd1306_fill_point(oled_handle, chXpos, chYpos, 0);
            }
            chTemp <<= 1;
            chYpos++;

            if ((chYpos - chYpos0) == chSize)
            {
                chYpos = chYpos0;
                chXpos++;
                break;
            }
        }
    }
}

void ssd1306_draw_string(i2c_master_dev_handle_t oled_handle, uint8_t chXpos, uint8_t chYpos,
                         const uint8_t *pchString, uint8_t chSize, uint8_t chMode)
{
    while (*pchString != '\0')
    {
        if (chXpos > (SSD1306_WIDTH - chSize / 2))
        {
            chXpos = 0;
            chYpos += chSize;
            if (chYpos > (SSD1306_HEIGHT - chSize))
            {
                chYpos = chXpos = 0;
                ssd1306_clear_screen(oled_handle, 0x00);
            }
        }
        ssd1306_draw_char(oled_handle, chXpos, chYpos, *pchString, chSize, chMode);
        chXpos += chSize / 2;
        pchString++;
    }
}

void ssd1306_fill_point(i2c_master_dev_handle_t oled_handle, uint8_t chXpos, uint8_t chYpos, uint8_t chPoint)
{
    uint8_t chPos, chBx, chTemp = 0;

    if (chXpos > 127 || chYpos > 63)
    {
        return;
    }
    chPos = 7 - chYpos / 8;
    chBx = chYpos % 8;
    chTemp = 1 << (7 - chBx);

    if (chPoint)
    {
        s_chDisplayBuffer[chXpos][chPos] |= chTemp;
    }
    else
    {
        s_chDisplayBuffer[chXpos][chPos] &= ~chTemp;
    }
}

void ssd1306_draw_1616char(i2c_master_dev_handle_t oled_handle, uint8_t chXpos, uint8_t chYpos, uint8_t chChar)
{
    uint8_t i, j;
    uint8_t chTemp = 0, chYpos0 = chYpos, chMode = 0;

    for (i = 0; i < 32; i++)
    {
        chTemp = c_chFont1612[chChar - 0x30][i];
        for (j = 0; j < 8; j++)
        {
            chMode = chTemp & 0x80 ? 1 : 0;
            ssd1306_fill_point(oled_handle, chXpos, chYpos, chMode);
            chTemp <<= 1;
            chYpos++;
            if ((chYpos - chYpos0) == 16)
            {
                chYpos = chYpos0;
                chXpos++;
                break;
            }
        }
    }
}

void ssd1306_draw_3216char(i2c_master_dev_handle_t oled_handle, uint8_t chXpos, uint8_t chYpos, uint8_t chChar)
{
    uint8_t i, j;
    uint8_t chTemp = 0, chYpos0 = chYpos, chMode = 0;

    for (i = 0; i < 64; i++)
    {
        chTemp = c_chFont3216[chChar - 0x30][i];
        for (j = 0; j < 8; j++)
        {
            chMode = chTemp & 0x80 ? 1 : 0;
            ssd1306_fill_point(oled_handle, chXpos, chYpos, chMode);
            chTemp <<= 1;
            chYpos++;
            if ((chYpos - chYpos0) == 32)
            {
                chYpos = chYpos0;
                chXpos++;
                break;
            }
        }
    }
}

void ssd1306_draw_bitmap(i2c_master_dev_handle_t oled_handle, uint8_t chXpos, uint8_t chYpos,
                         const uint8_t *pchBmp, uint8_t chWidth, uint8_t chHeight)
{
    uint16_t i, j, byteWidth = (chWidth + 7) / 8;

    for (j = 0; j < chHeight; j++)
    {
        for (i = 0; i < chWidth; i++)
        {
            if (*(pchBmp + j * byteWidth + i / 8) & (128 >> (i & 7)))
            {
                ssd1306_fill_point(oled_handle, chXpos + i, chYpos + j, 1);
            }
        }
    }
}

void ssd1306_draw_line(i2c_master_dev_handle_t oled_handle, int16_t chXpos1, int16_t chYpos1, int16_t chXpos2, int16_t chYpos2)
{
    // 16-bit variables allowing a display overflow effect
    int16_t x_len = abs(chXpos1 - chXpos2);
    int16_t y_len = abs(chYpos1 - chYpos2);

    if (y_len < x_len)
    {
        if (chXpos1 > chXpos2)
        {
            COORDINATE_SWAP(chXpos1, chXpos2, chYpos1, chYpos2);
        }
        int16_t len = x_len;
        int16_t diff = y_len;

        do
        {
            if (diff >= x_len)
            {
                diff -= x_len;
                if (chYpos1 < chYpos2)
                {
                    chYpos1++;
                }
                else
                {
                    chYpos1--;
                }
            }

            diff += y_len;
            ssd1306_fill_point(oled_handle, chXpos1++, chYpos1, 1);
        } while (len--);
    }

    else
    {
        if (chYpos1 > chYpos2)
        {
            COORDINATE_SWAP(chXpos1, chXpos2, chYpos1, chYpos2);
        }
        int16_t len = y_len;
        int16_t diff = x_len;

        do
        {
            if (diff >= y_len)
            {
                diff -= y_len;
                if (chXpos1 < chXpos2)
                {
                    chXpos1++;
                }
                else
                {
                    chXpos1--;
                }
            }

            diff += x_len;
            ssd1306_fill_point(oled_handle, chXpos1, chYpos1++, 1);
        } while (len--);
    }
}

esp_err_t ssd1306_init(i2c_master_dev_handle_t oled_handle)
{
    esp_err_t ret;
    ssd1306_write_cmd_byte(oled_handle, 0xAE); //--turn off oled panel
    ssd1306_write_cmd_byte(oled_handle, 0x40); //--set start line address  Set Mapping RAM Display Start Line (0x00~0x3F)
    ssd1306_write_cmd_byte(oled_handle, 0x81); //--set contrast control register
    ssd1306_write_cmd_byte(oled_handle, 0xCF); // Set SEG Output Current Brightness
    ssd1306_write_cmd_byte(oled_handle, 0xA1); //--Set SEG/Column Mapping
    ssd1306_write_cmd_byte(oled_handle, 0xC0); // Set COM/Row Scan Direction
    ssd1306_write_cmd_byte(oled_handle, 0xA6); //--set normal display
    ssd1306_write_cmd_byte(oled_handle, 0xA8); //--set multiplex ratio(1 to 64)
    ssd1306_write_cmd_byte(oled_handle, 0x3f); //--1/64 duty
    ssd1306_write_cmd_byte(oled_handle, 0xd5); //--set display clock divide ratio/oscillator frequency
    ssd1306_write_cmd_byte(oled_handle, 0x80); //--set divide ratio, Set Clock as 100 Frames/Sec
    ssd1306_write_cmd_byte(oled_handle, 0xD9); //--set pre-charge period
    ssd1306_write_cmd_byte(oled_handle, 0xF1); // Set Pre-Charge as 15 Clocks & Discharge as 1 Clock
    ssd1306_write_cmd_byte(oled_handle, 0xDA); //--set com pins hardware configuration
    ssd1306_write_cmd_byte(oled_handle, 0xDB); //--set vcomh
    ssd1306_write_cmd_byte(oled_handle, 0x40); // Set VCOM Deselect Level
    ssd1306_write_cmd_byte(oled_handle, 0x8D); //--set Charge Pump enable/disable
    ssd1306_write_cmd_byte(oled_handle, 0x14); //--set(0x10) disable
    ssd1306_write_cmd_byte(oled_handle, 0xA4); // Disable Entire Display On (0xa4/0xa5)
    ssd1306_write_cmd_byte(oled_handle, 0xA6); // Disable Inverse Display On (0xa6/a7)

    uint8_t cmd[2] = {0x20, 1}; //-- set vertical adressing mode
    ssd1306_write_cmd(oled_handle, cmd, sizeof(cmd));
    
    uint8_t col_cmd[] = {0x21, 0, 127}; // Set column address to zero
    uint8_t row_cmd[] = {0x22, 0, 7};   // Set row address to zero
    ssd1306_write_cmd(oled_handle, col_cmd, sizeof(col_cmd));
    ssd1306_write_cmd(oled_handle, row_cmd, sizeof(row_cmd));

    ret = ssd1306_write_cmd_byte(oled_handle, 0xAF); //--turn on oled panel

    ssd1306_clear_screen(oled_handle, 0x00);
    return ret;
}

esp_err_t ssd1306_refresh_gram(i2c_master_dev_handle_t oled_handle)
{
    return ssd1306_write_data(oled_handle, &s_chDisplayBuffer[0][0], sizeof(s_chDisplayBuffer));
}

void ssd1306_clear_screen(i2c_master_dev_handle_t oled_handle, uint8_t chFill)
{
    memset(s_chDisplayBuffer, chFill, sizeof(s_chDisplayBuffer));
}
