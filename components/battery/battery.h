#ifndef BATTERY_H
#define BATTERY_H

#include "app_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include "ssd1306.h"
#include "ssd1306_fonts.h"

// 分压电阻（kΩ）
#define R_UPPER 680.0f
#define R_LOWER 100.0f

// 电池电压等级阈值（mV）
#define BATTERY_FULL_MV        8400.0f   // 100%
#define BATTERY_TWO_THIRD_MV   7900.0f   // ~60–70%
#define BATTERY_ONE_THIRD_MV   7400.0f   // ~30%

// ADC 硬件配置
#define ADC_UNIT ADC_UNIT_1
#define ADC_CHANNEL ADC_CHANNEL_0
#define ADC_ATTEN ADC_ATTEN_DB_6
#define ADC_BITWIDTH ADC_BITWIDTH_DEFAULT

esp_err_t battery_init(void);

#endif 
