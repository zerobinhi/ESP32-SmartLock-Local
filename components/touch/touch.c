#include "touch.h"
#include <stdio.h>
#include <inttypes.h>
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "TOUCH_DRIVER";

// ====================================================================
// 内部宏和变量 (从例程中提取)
// ====================================================================

// 采样配置，V1/V2/V3 芯片的默认配置不同，此处使用例程的V1默认配置
#define EXAMPLE_TOUCH_SAMPLE_CFG_DEFAULT() {TOUCH_SENSOR_V1_DEFAULT_SAMPLE_CONFIG(5.0, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_1V7)}

// 通道配置，此处提取例程中的初始配置
#define EXAMPLE_TOUCH_CHAN_CFG_DEFAULT() {              \
    .abs_active_thresh = {1000},                        \
    .charge_speed = TOUCH_CHARGE_SPEED_7,               \
    .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT, \
    .group = TOUCH_CHAN_TRIG_GROUP_BOTH,                \
}

#define EXAMPLE_TOUCH_SAMPLE_CFG_NUM 1       // 单个配置组
#define EXAMPLE_TOUCH_CHANNEL_NUM 1          // 单个通道
#define EXAMPLE_TOUCH_CHAN_INIT_SCAN_TIMES 3 // 初始扫描次数

// Active threshold to benchmark ratio (来自touch.h的配置)
static const float s_thresh2bm_ratio[EXAMPLE_TOUCH_CHANNEL_NUM] = {
    [0 ... EXAMPLE_TOUCH_CHANNEL_NUM - 1] = APP_TOUCH_THRESH2BM_RATIO,
};

// ====================================================================
// 内部回调函数
// ====================================================================

bool example_touch_on_active_callback(touch_sensor_handle_t sens_handle, const touch_active_event_data_t *event, void *user_ctx)
{
    // 按下事件触发，此处可以发送信号量或队列到任务
    ESP_LOGI(TAG, "[CH %d] ACTIVE (Press)", (int)event->chan_id);
    // return false 表示不阻止更低优先级的 ISR 运行
    return false;
}

bool example_touch_on_inactive_callback(touch_sensor_handle_t sens_handle, const touch_inactive_event_data_t *event, void *user_ctx)
{
    // 释放事件触发
    ESP_LOGI(TAG, "[CH %d] INACTIVE (Release)", (int)event->chan_id);
    return false;
}

// ====================================================================
// 初始扫描和动态阈值调整逻辑 (从例程中提取)
// ====================================================================

static void example_touch_do_initial_scanning(touch_sensor_handle_t sens_handle, touch_channel_handle_t chan_handle[])
{
    /* 启用传感器并执行多次单次扫描以获取稳定的基线值 */
    ESP_ERROR_CHECK(touch_sensor_enable(sens_handle));
    for (int i = 0; i < EXAMPLE_TOUCH_CHAN_INIT_SCAN_TIMES; i++)
    {
        ESP_ERROR_CHECK(touch_sensor_trigger_oneshot_scanning(sens_handle, 2000));
    }
    ESP_ERROR_CHECK(touch_sensor_disable(sens_handle)); // 扫描完成，先禁用

    printf("Initial benchmark and new threshold are:\n");
    for (int i = 0; i < EXAMPLE_TOUCH_CHANNEL_NUM; i++)
    {
        uint32_t benchmark[EXAMPLE_TOUCH_SAMPLE_CFG_NUM] = {};

        // 读取基线值或平滑数据（取决于芯片版本）
#if SOC_TOUCH_SUPPORT_BENCHMARK
        ESP_ERROR_CHECK(touch_channel_read_data(chan_handle[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark));
#else
        ESP_ERROR_CHECK(touch_channel_read_data(chan_handle[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, benchmark));
#endif

        printf("Touch [CH %d]", APP_TOUCH_CHANNEL);
        touch_channel_config_t chan_cfg = EXAMPLE_TOUCH_CHAN_CFG_DEFAULT();

        // 根据芯片版本计算并更新动态阈值
        for (int j = 0; j < EXAMPLE_TOUCH_SAMPLE_CFG_NUM; j++)
        {
#if SOC_TOUCH_SENSOR_VERSION == 1
            // Touch V1 (ESP32) 使用绝对阈值 (信号下降触发)
            chan_cfg.abs_active_thresh[j] = (uint32_t)(benchmark[j] * (1.0f - s_thresh2bm_ratio[i]));
            printf(" %d: BM:%" PRIu32 ", TH:%" PRIu32 "\t", j, benchmark[j], chan_cfg.abs_active_thresh[j]);
#else
            // Touch V2/V3 使用相对阈值 (信号上升触发)
            chan_cfg.active_thresh[j] = (uint32_t)(benchmark[j] * s_thresh2bm_ratio[i]);
            printf(" %d: BM:%" PRIu32 ", TH:%" PRIu32 "\t", j, benchmark[j], chan_cfg.active_thresh[j]);
#endif
        }
        printf("\n");
        /* 更新通道配置 */
        ESP_ERROR_CHECK(touch_sensor_reconfig_channel(chan_handle[i], &chan_cfg));
    }
}

// ====================================================================
// 外部接口实现
// ====================================================================

esp_err_t app_touch_initialization(void)
{
    touch_sensor_handle_t sens_handle = NULL;
    touch_channel_handle_t chan_handle[EXAMPLE_TOUCH_CHANNEL_NUM];

    // Step 1: 创建传感器控制器
    touch_sensor_sample_config_t sample_cfg[TOUCH_SAMPLE_CFG_NUM] = EXAMPLE_TOUCH_SAMPLE_CFG_DEFAULT();
    touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(EXAMPLE_TOUCH_SAMPLE_CFG_NUM, sample_cfg);
    ESP_RETURN_ON_ERROR(touch_sensor_new_controller(&sens_cfg, &sens_handle), TAG, "New touch sensor controller failed");

    // Step 2: 创建并启用触摸通道
    touch_channel_config_t chan_cfg = EXAMPLE_TOUCH_CHAN_CFG_DEFAULT();
    ESP_RETURN_ON_ERROR(touch_sensor_new_channel(sens_handle, APP_TOUCH_CHANNEL, &chan_cfg, &chan_handle[0]), TAG, "New touch channel failed");

    touch_chan_info_t chan_info = {};
    ESP_ERROR_CHECK(touch_sensor_get_channel_info(chan_handle[0], &chan_info));
    ESP_LOGI(TAG, "Touch [CH %d] enabled on GPIO%d", APP_TOUCH_CHANNEL, chan_info.chan_gpio);

    // Step 3: 配置默认滤波器
    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_RETURN_ON_ERROR(touch_sensor_config_filter(sens_handle, &filter_cfg), TAG, "Config touch filter failed");

    // Step 4: 执行初始扫描和动态阈值设置
    example_touch_do_initial_scanning(sens_handle, chan_handle);

    // Step 5: 注册回调函数 (使用 Active/Inactive 事件)
    touch_event_callbacks_t callbacks = {
        .on_active = example_touch_on_active_callback,
        .on_inactive = example_touch_on_inactive_callback,
    };
    ESP_RETURN_ON_ERROR(touch_sensor_register_callbacks(sens_handle, &callbacks, NULL), TAG, "Register touch callbacks failed");

    // Step 6: 启用触摸传感器
    ESP_RETURN_ON_ERROR(touch_sensor_enable(sens_handle), TAG, "Enable touch sensor failed");

    // Step 7: 开始连续扫描 (传感器开始工作)
    ESP_RETURN_ON_ERROR(touch_sensor_start_continuous_scanning(sens_handle), TAG, "Start continuous scanning failed");

    ESP_LOGI(TAG, "Touch Sensor Initialization Complete.");
    return ESP_OK;
}