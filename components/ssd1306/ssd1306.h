#ifndef SSD1306_H_
#define SSD1306_H_

#include "ssd1306_fonts.h"
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_log.h>
#include "app_config.h"

#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 64

extern i2c_master_dev_handle_t oled_handle;
extern bool g_i2c_service_installed; // 是否安装了I2C服务
extern i2c_master_bus_handle_t bus_handle;

esp_err_t ssd1306_initialization(void);
esp_err_t ssd1306_init(void);
esp_err_t ssd1306_refresh(void);
void ssd1306_clear(uint8_t color);
esp_err_t ssd1306_set_contrast(uint8_t contrast);
esp_err_t ssd1306_invert(bool invert);

void ssd1306_draw_point(uint8_t x, uint8_t y, uint8_t color);
void ssd1306_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color);
void ssd1306_draw_rect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t color);
void ssd1306_fill_rect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t color);

void ssd1306_show_char(uint8_t x, uint8_t y, char chr, uint8_t size, uint8_t color);
void ssd1306_show_string(uint8_t x, uint8_t y, const char *str, uint8_t size, uint8_t color);
void ssd1306_show_num(uint8_t x, uint8_t y, int32_t num, uint8_t len, uint8_t size, uint8_t color);
void ssd1306_show_float(uint8_t x, uint8_t y, float num, uint8_t int_len, uint8_t dec_len,
                        uint8_t size, uint8_t color);
void ssd1306_draw_bitmap(uint8_t x, uint8_t y, const uint8_t *bmp,
                         uint8_t w, uint8_t h, uint8_t color);

#endif /* SSD1306_H_ */