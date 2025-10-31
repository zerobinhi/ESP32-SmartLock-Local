/**
 * @file ssd1306.c
 * @brief SSD1306 OLED 驱动实现（I2C）
 *
 * 适用于 ESP32 / ESP-IDF
 * 支持：
 * - 点、字符、字符串显示
 * - 整数显示（带前导零，可显示负数）
 * - 浮点数显示（带符号与小数位控制）
 * - 清屏、刷新屏幕
 *
 * @author 
 * @date 2025-10-31
 */

#include "ssd1306.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"

static const char *TAG = "SSD1306";

/* OLED 显存缓存 */
static uint8_t oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8] = {0};

/* 私有函数声明 */
static void ssd1306_write_cmd(i2c_master_dev_handle_t i2c_handle, uint8_t cmd);
static void ssd1306_write_data(i2c_master_dev_handle_t i2c_handle, uint8_t data);
static uint32_t oled_pow(uint8_t x, uint8_t y);

/**
 * @brief 初始化 SSD1306 OLED
 * 
 * @param i2c_handle I2C 设备句柄
 */
void ssd1306_init(i2c_master_dev_handle_t i2c_handle)
{
    ESP_LOGI(TAG, "Initializing SSD1306...");

    ssd1306_write_cmd(i2c_handle, 0xAE); // 关闭显示
    ssd1306_write_cmd(i2c_handle, 0xD5); // 设置显示时钟分频/震荡频率
    ssd1306_write_cmd(i2c_handle, 0x80); // 默认分频因子
    ssd1306_write_cmd(i2c_handle, 0xA8); // 设置多路复用率
    ssd1306_write_cmd(i2c_handle, OLED_HEIGHT - 1);
    ssd1306_write_cmd(i2c_handle, 0xD3); // 设置显示偏移
    ssd1306_write_cmd(i2c_handle, 0x00);
    ssd1306_write_cmd(i2c_handle, 0x40); // 设置起始行
    ssd1306_write_cmd(i2c_handle, 0x8D); // 电荷泵设置
    ssd1306_write_cmd(i2c_handle, 0x14);
    ssd1306_write_cmd(i2c_handle, 0x20); // 内存地址模式
    ssd1306_write_cmd(i2c_handle, 0x00); // 水平模式
    ssd1306_write_cmd(i2c_handle, 0xA1); // 段重映射
    ssd1306_write_cmd(i2c_handle, 0xC8); // COM扫描方向
    ssd1306_write_cmd(i2c_handle, 0xDA); // COM引脚配置
    ssd1306_write_cmd(i2c_handle, 0x12);
    ssd1306_write_cmd(i2c_handle, 0x81); // 对比度
    ssd1306_write_cmd(i2c_handle, 0xCF);
    ssd1306_write_cmd(i2c_handle, 0xD9); // 预充电周期
    ssd1306_write_cmd(i2c_handle, 0xF1);
    ssd1306_write_cmd(i2c_handle, 0xDB); // VCOMH 电压倍率
    ssd1306_write_cmd(i2c_handle, 0x40);
    ssd1306_write_cmd(i2c_handle, 0xA4); // 全局显示开启
    ssd1306_write_cmd(i2c_handle, 0xA6); // 非反相显示
    ssd1306_write_cmd(i2c_handle, 0xAF); // 打开显示

    memset(oled_buffer, 0, sizeof(oled_buffer));
}

/**
 * @brief 写入命令到 OLED
 */
static void ssd1306_write_cmd(i2c_master_dev_handle_t i2c_handle, uint8_t cmd)
{
    i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
    i2c_master_start(cmd_handle);
    i2c_master_write_byte(cmd_handle, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd_handle, 0x00, true); // Co=0, D/C#=0
    i2c_master_write_byte(cmd_handle, cmd, true);
    i2c_master_stop(cmd_handle);
    i2c_master_cmd_begin(i2c_handle, cmd_handle, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd_handle);
}

/**
 * @brief 写入数据到 OLED
 */
static void ssd1306_write_data(i2c_master_dev_handle_t i2c_handle, uint8_t data)
{
    i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
    i2c_master_start(cmd_handle);
    i2c_master_write_byte(cmd_handle, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd_handle, 0x40, true); // Co=0, D/C#=1
    i2c_master_write_byte(cmd_handle, data, true);
    i2c_master_stop(cmd_handle);
    i2c_master_cmd_begin(i2c_handle, cmd_handle, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd_handle);
}

/**
 * @brief 刷新 OLED 显存缓存到屏幕
 */
void ssd1306_refresh(i2c_master_dev_handle_t i2c_handle)
{
    for (uint8_t page = 0; page < OLED_HEIGHT / 8; page++)
    {
        ssd1306_write_cmd(i2c_handle, 0xB0 + page);
        ssd1306_write_cmd(i2c_handle, 0x00);
        ssd1306_write_cmd(i2c_handle, 0x10);

        for (uint8_t col = 0; col < OLED_WIDTH; col++)
        {
            ssd1306_write_data(i2c_handle, oled_buffer[OLED_WIDTH * page + col]);
        }
    }
}

/**
 * @brief 清屏
 */
void ssd1306_clear(i2c_master_dev_handle_t i2c_handle)
{
    memset(oled_buffer, 0, sizeof(oled_buffer));
    ssd1306_refresh(i2c_handle);
}

/**
 * @brief 在缓存中设置一个点
 */
void ssd1306_draw_point(uint8_t x, uint8_t y, uint8_t color)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;

    if (color)
        oled_buffer[x + (y / 8) * OLED_WIDTH] |= 1 << (y % 8);
    else
        oled_buffer[x + (y / 8) * OLED_WIDTH] &= ~(1 << (y % 8));
}

/**
 * @brief 显示单个字符
 */
void ssd1306_draw_char(i2c_master_dev_handle_t oled_handle, uint8_t x, uint8_t y,
                       char chr, uint8_t size, uint8_t color)
{
    // 这里调用字库函数，略，假设已有 get_font_data()
    uint8_t i, j;
    const uint8_t *font = get_font_data(chr, size); // 用户需实现
    uint8_t bytes_per_char = (size / 8 + ((size % 8) ? 1 : 0)) * size;

    for (i = 0; i < size; i++)
    {
        uint8_t temp = font[i];
        for (j = 0; j < 8; j++)
        {
            if (temp & (1 << j))
                ssd1306_draw_point(x + i, y + j, color);
            else
                ssd1306_draw_point(x + i, y + j, !color);
        }
    }
}

/**
 * @brief 显示字符串
 */
void ssd1306_draw_string(i2c_master_dev_handle_t oled_handle, uint8_t x, uint8_t y,
                         const uint8_t *str, uint8_t size, uint8_t color)
{
    while (*str)
    {
        ssd1306_draw_char(oled_handle, x, y, *str, size, color);
        x += size / 2;
        str++;
    }
}

/**
 * @brief 显示整数（带前导零，可显示负数）
 */
void ssd1306_show_num(i2c_master_dev_handle_t oled_handle, uint8_t x, uint8_t y,
                      int32_t num, uint8_t len, uint8_t size, uint8_t color)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
    if (len == 0 || len > 10) return;

    char buf[16];
    int neg = 0;
    uint32_t abs_num;

    if (num < 0)
    {
        neg = 1;
        abs_num = (uint32_t)(-num);
    }
    else
    {
        abs_num = (uint32_t)num;
    }

    snprintf(buf, sizeof(buf), "%0*u", len, abs_num);

    if (neg)
    {
        for (int i = len; i >= 1; i--) buf[i] = buf[i - 1];
        buf[0] = '-';
        buf[len + 1] = '\0';
    }

    ssd1306_draw_string(oled_handle, x, y, (const uint8_t *)buf, size, color);
}

/**
 * @brief 显示浮点数（带符号与小数位控制）
 */
void ssd1306_show_decimal(i2c_master_dev_handle_t oled_handle, uint8_t x, uint8_t y,
                          float num, uint8_t int_len, uint8_t dec_len,
                          uint8_t size, uint8_t color)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;

    uint8_t t, temp, enshow = 0;
    uint8_t neg_flag = 0;
    int z_temp, f_temp;
    uint8_t start_x = x;

    if (num < 0)
    {
        neg_flag = 1;
        num = -num;
    }

    z_temp = (int)num;

    for (t = 0; t < int_len; t++)
    {
        temp = (z_temp / oled_pow(10, int_len - t - 1)) % 10;
        if (!enshow && t < (int_len - 1))
        {
            if (temp == 0)
            {
                ssd1306_draw_char(oled_handle, x + (size / 2) * t, y, ' ', size, color);
                continue;
            }
            else
                enshow = 1;
        }
        ssd1306_draw_char(oled_handle, x + (size / 2) * t, y, temp + '0', size, color);
    }

    ssd1306_draw_char(oled_handle, x + (size / 2) * int_len, y, '.', size, color);

    f_temp = (int)((num - z_temp) * oled_pow(10, dec_len) + 0.5f);
    for (t = 0; t < dec_len; t++)
    {
        temp = (f_temp / oled_pow(10, dec_len - t - 1)) % 10;
        ssd1306_draw_char(oled_handle, x + (size / 2) * (int_len + 1 + t), y, temp + '0', size, color);
    }

    if (neg_flag)
        ssd1306_draw_char(oled_handle, start_x, y, '-', size, color);
}

/* 私有函数：整数次幂 */
static uint32_t oled_pow(uint8_t x, uint8_t y)
{
    uint32_t result = 1;
    for (uint8_t i = 0; i < y; i++) result *= x;
    return result;
}
