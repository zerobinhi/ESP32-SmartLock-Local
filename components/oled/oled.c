#include "oled.h"
#include <string.h>
#include <math.h>

#define TAG "oled"

/* 显存缓存（128x64 -> 128x8页） */
static uint8_t oled_buffer[OLED_WIDTH][8];

i2c_master_dev_handle_t oled_handle;

// I2C 底层通信
static esp_err_t _oled_write_cmd(uint8_t *cmd, size_t len)
{
    uint8_t ctrl = OLED_CTRL_CMD;

    i2c_master_transmit_multi_buffer_info_t buffers[2] = {
        {.write_buffer = &ctrl, .buffer_size = 1},
        {.write_buffer = cmd, .buffer_size = len},
    };

    esp_err_t err = i2c_master_multi_buffer_transmit(oled_handle, buffers, 2, portMAX_DELAY);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Write cmd failed: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t _oled_write_page(uint8_t *data128)
{
    uint8_t ctrl = OLED_CTRL_DAT;

    i2c_master_transmit_multi_buffer_info_t buffers[2] = {
        {.write_buffer = &ctrl, .buffer_size = 1},
        {.write_buffer = data128, .buffer_size = 128},
    };

    esp_err_t err = i2c_master_multi_buffer_transmit(oled_handle, buffers, 2, portMAX_DELAY);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Write page failed: %s", esp_err_to_name(err));
    }
    return err;
}

// 基础功能
esp_err_t oled_initialization(void)
{
    if (g_i2c_service_installed == false)
    {
        // 初始化I2C
        i2c_master_bus_config_t i2c_mst_config = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = I2C_MASTER_NUM,
            .scl_io_num = I2C_MASTER_SCL_IO,
            .sda_io_num = I2C_MASTER_SDA_IO,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));
        g_i2c_service_installed = true;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &oled_handle));
    ESP_LOGI(TAG, "oled device created");
    return oled_init();
}

esp_err_t oled_init(void)
{
    uint8_t init_cmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00,
        0x40, 0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8,
        0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB,
        0x40, 0xA4, 0xA6, 0xAF};

    esp_err_t ret = _oled_write_cmd(init_cmds, sizeof(init_cmds));
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    oled_clear(0);

    oled_draw_bitmap(0, 2, &c_chSingal816[0], 16, 8, 0);
    oled_draw_bitmap(24, 2, &c_chBluetooth88[0], 8, 8, 0);
    oled_draw_bitmap(40, 2, &c_chMsg816[0], 16, 8, 0);
    oled_draw_bitmap(64, 2, &c_chGPRS88[0], 8, 8, 0);
    oled_draw_bitmap(90, 2, &c_chAlarm88[0], 8, 8, 0);
    oled_draw_bitmap(0, 21, BMP2, 128, 32, 0);

    return oled_refresh();
}

esp_err_t oled_refresh(void)
{
    esp_err_t ret;
    uint8_t page_buf[128];

    for (uint8_t page = 0; page < 8; page++)
    {
        uint8_t cmd[3] = {0xB0 | page, 0x00, 0x10};
        ret = _oled_write_cmd(cmd, 3);
        if (ret != ESP_OK)
            return ret;

        for (uint8_t col = 0; col < 128; col++)
            page_buf[col] = oled_buffer[col][page];

        ret = _oled_write_page(page_buf);
        if (ret != ESP_OK)
            return ret;
    }
    return ESP_OK;
}

void oled_clear(uint8_t color)
{
    memset(oled_buffer, color ? 0xFF : 0x00, sizeof(oled_buffer));
}

esp_err_t oled_set_contrast(uint8_t contrast)
{
    uint8_t cmd[2] = {0x81, contrast};
    return _oled_write_cmd(cmd, 2);
}

esp_err_t oled_invert(bool invert)
{
    uint8_t cmd = invert ? 0xA7 : 0xA6;
    return _oled_write_cmd(&cmd, 1);
}

// 绘图操作
void oled_draw_point(uint8_t x, uint8_t y, uint8_t color)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT)
        return;
    uint8_t page = y >> 3;
    uint8_t bit = y & 0x07;
    if (color)
        oled_buffer[x][page] |= (1 << bit);
    else
        oled_buffer[x][page] &= ~(1 << bit);
}

void oled_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color)
{
    int16_t dx = abs(x2 - x1);
    int16_t dy = abs(y2 - y1);
    int16_t sx = (x1 < x2) ? 1 : -1;
    int16_t sy = (y1 < y2) ? 1 : -1;
    int16_t err = dx - dy;

    while (1)
    {
        oled_draw_point(x1, y1, color);
        if (x1 == x2 && y1 == y2)
            break;
        int16_t e2 = err << 1;
        if (e2 > -dy)
        {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y1 += sy;
        }
        if (x1 >= OLED_WIDTH || y1 >= OLED_HEIGHT || x1 < 0 || y1 < 0)
            break;
    }
}

void oled_draw_rect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t color)
{
    oled_draw_line(x1, y1, x2, y1, color);
    oled_draw_line(x1, y2, x2, y2, color);
    oled_draw_line(x1, y1, x1, y2, color);
    oled_draw_line(x2, y1, x2, y2, color);
}

void oled_fill_rect(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t color)
{
    for (uint8_t y = y1; y <= y2; y++)
        for (uint8_t x = x1; x <= x2; x++)
            oled_draw_point(x, y, color);
}

// 字符显示
void oled_show_char(uint8_t x, uint8_t y, char chr, uint8_t size, uint8_t color)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT)
        return;
    if (chr < ' ' || chr > '~')
        return;

    const uint8_t *font = NULL;
    uint8_t width = 0, height = 0;

    switch (size)
    {
    case 12:
        font = c_chFont1206[chr - ' '];
        width = 6;
        height = 12;
        break;
    case 16:
        font = c_chFont1608[chr - ' '];
        width = 8;
        height = 16;
        break;
    case 24:
        font = c_chFont1612[chr - ' '];
        width = 12;
        height = 16;
        break;
    case 32:
        font = c_chFont3216[chr - ' '];
        width = 16;
        height = 32;
        break;
    default:
        return;
    }

    uint8_t pages = (height + 7) / 8;
    for (uint8_t i = 0; i < width; i++)
    {
        for (uint8_t p = 0; p < pages; p++)
        {
            uint8_t data = font[p * width + i];
            for (uint8_t bit = 0; bit < 8; bit++)
            {
                uint8_t py = y + p * 8 + bit;
                if (py >= OLED_HEIGHT)
                    break;
                uint8_t pixel = (data & (1 << bit)) ? 1 : 0;
                if (color)
                    pixel = !pixel;
                oled_draw_point(x + i, py, pixel);
            }
        }
    }
}

void oled_show_string(uint8_t x, uint8_t y, const char *str, uint8_t size, uint8_t color)
{
    if (!str)
        return;

    uint8_t cx = x;
    uint8_t char_width = (size == 12) ? 6 : (size == 16) ? 8
                                        : (size == 24)   ? 12
                                                         : 16;

    while (*str)
    {
        if (cx + char_width > OLED_WIDTH)
        {
            cx = 0;
            y += size;
            if (y + size > OLED_HEIGHT)
                break;
        }
        oled_show_char(cx, y, *str++, size, color);
        cx += char_width;
    }
}

// 数字与浮点数显示
void oled_show_num(uint8_t x, uint8_t y, int32_t num, uint8_t len, uint8_t size, uint8_t color)
{
    if (len == 0 || len > 10)
        return;

    char buf[18];
    uint8_t offset = 0;

    if (num < 0)
    {
        buf[0] = '-';
        offset = 1;
        num = -num;
    }

    char fmt[8];
    snprintf(fmt, sizeof(fmt), "%%0%ulu", len);
    snprintf(&buf[offset], sizeof(buf) - offset, fmt, (uint32_t)num);

    oled_show_string(x, y, buf, size, color);
}

void oled_show_float(uint8_t x, uint8_t y, float num, uint8_t int_len, uint8_t dec_len, uint8_t size, uint8_t color)
{
    if (int_len == 0 || dec_len == 0)
        return;

    char buf[24];
    uint8_t idx = 0;
    bool neg = false;

    if (num < 0.0f)
    {
        neg = true;
        num = -num;
    }

    uint32_t int_part = (uint32_t)num;
    uint32_t frac_part = (uint32_t)((num - int_part) * powf(10, dec_len) + 0.5f);

    char int_buf[12], frac_buf[12];
    snprintf(int_buf, sizeof(int_buf), "%0*lu", int_len, (unsigned long)int_part);
    snprintf(frac_buf, sizeof(frac_buf), "%0*lu", dec_len, (unsigned long)frac_part);

    if (neg)
        idx += snprintf(buf + idx, sizeof(buf) - idx, "-");
    snprintf(buf + idx, sizeof(buf) - idx, "%s.%s", int_buf, frac_buf);

    oled_show_string(x, y, buf, size, color);
}

// 图形显示
void oled_draw_bitmap(uint8_t x, uint8_t y, const uint8_t *bmp, uint8_t w, uint8_t h, uint8_t color)
{
    if (!bmp)
        return;
    uint8_t blockCnt = (h + 7) / 8;
    for (uint8_t block = 0; block < blockCnt; block++)
    {
        for (uint8_t col = 0; col < w; col++)
        {
            uint16_t idx = block * w + col;
            uint8_t data = bmp[idx];
            for (uint8_t bit = 0; bit < 8; bit++)
            {
                uint8_t py = y + block * 8 + bit;
                if (py >= OLED_HEIGHT)
                    break;
                uint8_t pixel = (data & (1 << bit)) ? 1 : 0;
                if (color)
                    pixel = !pixel;
                oled_draw_point(x + col, py, pixel);
            }
        }
    }
}

void oled_show_chinese(uint8_t x, uint8_t y, uint8_t no, uint8_t color)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT)
        return;
    const uint8_t *hz_up = Hzk[no * 2];
    const uint8_t *hz_down = Hzk[no * 2 + 1];

    for (uint8_t block = 0; block < 2; block++)
    {
        const uint8_t *dataPtr = (block == 0) ? hz_up : hz_down;
        for (uint8_t col = 0; col < 16; col++)
        {
            uint8_t data = dataPtr[col];
            for (uint8_t bit = 0; bit < 8; bit++)
            {
                uint8_t py = y + block * 8 + bit;
                uint8_t pixel = (data & (1 << bit)) ? 1 : 0;
                if (color)
                    pixel = !pixel;
                oled_draw_point(x + col, py, pixel);
            }
        }
    }
}