#ifndef SSD1306_H_
#define SSD1306_H_

#include "driver/i2c_master.h"
#include "ssd1306_fonts.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>


/* ================================
 * SSD1306 OLED 参数定义
 * ================================ */
#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64

/* ================================
 * 初始化与刷新函数
 * ================================ */

/**
 * @brief 初始化 SSD1306 OLED 显示屏
 * 
 * @param handle 已创建的 i2c_master_dev_handle_t
 * @return esp_err_t 
 */
esp_err_t ssd1306_init(i2c_master_dev_handle_t handle);

/**
 * @brief 刷新显示缓存到 OLED
 */
esp_err_t ssd1306_refresh(i2c_master_dev_handle_t handle);

/**
 * @brief 清空屏幕
 * @param fill 0 = 黑，1 = 白
 */
void ssd1306_clear_screen(uint8_t fill);

/**
 * @brief 设置对比度
 * @param contrast 0x00 ~ 0xFF
 */
void ssd1306_set_contrast(i2c_master_dev_handle_t handle, uint8_t contrast);

/**
 * @brief 反转显示（黑白互换）
 */
void ssd1306_invert_display(i2c_master_dev_handle_t handle, bool invert);

/* ================================
 * 基础绘图函数
 * ================================ */
void ssd1306_draw_point(uint8_t x, uint8_t y, uint8_t color);
void ssd1306_draw_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t color);
void ssd1306_draw_rect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t color);
void ssd1306_fill_rect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t color);

/* ================================
 * 字符与字符串显示
 * ================================ */

/**
 * @brief 显示单个 ASCII 字符
 * 
 * @param x 起始x坐标
 * @param y 起始y坐标
 * @param chr 字符
 * @param size 字号（12、16、24、32）
 * @param color 0=正常，1=反色
 */
void ssd1306_show_char(uint8_t x, uint8_t y, char chr, uint8_t size, uint8_t color);

/**
 * @brief 显示字符串
 */
void ssd1306_show_string(uint8_t x, uint8_t y, const char *str, uint8_t size, uint8_t color);

/**
 * @brief 显示整数
 */
void ssd1306_show_num(i2c_master_dev_handle_t oled_handle, uint8_t x, uint8_t y,
                      int32_t num, uint8_t len, uint8_t size, uint8_t color);

/**
 * @brief 显示浮点数
 */
void ssd1306_show_decimal(i2c_master_dev_handle_t oled_handle, uint8_t x, uint8_t y,
                          float num, uint8_t int_len, uint8_t dec_len,
                          uint8_t size, uint8_t color);

/* ================================
 * 图像显示
 * ================================ */
void ssd1306_draw_bitmap(uint8_t x, uint8_t y,
                         const uint8_t *bmp, uint8_t w, uint8_t h, uint8_t color);

#endif /* SSD1306_H_ */