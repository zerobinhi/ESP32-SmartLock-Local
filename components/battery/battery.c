#include "battery.h"

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle;

static esp_err_t adc_init(void)
{
    // initialize ADC unit
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    // configure ADC channel
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg));

    // initialize ADC calibration
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle));

    ESP_LOGI(TAG, "ADC initialized");
    return ESP_OK;
}
void battery_task(void *arg)
{
    int raw;
    int voltage_mv;

    while (1)
    {
        // read raw ADC value
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw));

        // convert raw to voltage in mV
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &voltage_mv));

        // calculate battery voltage (considering voltage divider)
        float battery_voltage = voltage_mv * (R_UPPER + R_LOWER) / R_LOWER;

        ESP_LOGI(TAG, "Battery Voltage: %.2f mV", battery_voltage);
        if (battery_voltage >= BATTERY_FULL_MV)
        {
            ssd1306_draw_bitmap(112, 2, &c_chBat816_Full[0], 16, 8, 0);
        }
        else if (battery_voltage >= BATTERY_TWO_THIRD_MV)
        {
            ssd1306_draw_bitmap(112, 2, &c_chBat816_TwoThird[0], 16, 8, 0);
        }
        else if (battery_voltage >= BATTERY_ONE_THIRD_MV)
        {
            ssd1306_draw_bitmap(112, 2, &c_chBat816_OneThird[0], 16, 8, 0);
        }
        else
        {
            ssd1306_draw_bitmap(112, 2, &c_chBat816_Empty[0], 16, 8, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(6000)); // delay 6 seconds
    }
}

esp_err_t battery_init(void)
{
    adc_init();
    xTaskCreate(battery_task, "battery_task", 4096, NULL, 10, NULL);
    ESP_LOGI(TAG, "Battery task created");
    return ESP_OK;
}