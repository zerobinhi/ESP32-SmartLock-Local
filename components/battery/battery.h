#ifndef BATTERY_H
#define BATTERY_H

#include "app_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include "oled.h"
#include "oled_fonts.h"

// Voltage divider resistors (in kOhms)
#define R_UPPER 680.0f
#define R_LOWER 100.0f

// battery voltage thresholds (mV)
#define BATTERY_FULL_MV        8400.0f   // 100%
#define BATTERY_TWO_THIRD_MV   7900.0f   // ~60–70%
#define BATTERY_ONE_THIRD_MV   7400.0f   // ~30%

// ADC configuration
#define ADC_UNIT ADC_UNIT_1
#define ADC_CHANNEL ADC_CHANNEL_0
#define ADC_ATTEN ADC_ATTEN_DB_6
#define ADC_BITWIDTH ADC_BITWIDTH_DEFAULT

esp_err_t battery_init(void);

#endif 
