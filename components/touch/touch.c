#include "touch.h"

static const char *TAG = "TOUCH_DRIVER";

// =======================================================================
// 12 键布局 (四行三列)
//    1   2   3
//    4   5   6
//    7   8   9
//    *   0   #
// =======================================================================

// 你的硬件通道映射（可按实际 PCB 修改）
static const uint8_t touch_channels[] = {
    TOUCH_MIN_CHAN_ID + 7, // 1
    TOUCH_MIN_CHAN_ID + 9, // 2
    TOUCH_MIN_CHAN_ID + 1, // 3

    TOUCH_MIN_CHAN_ID + 6, // 4
    TOUCH_MIN_CHAN_ID + 8, // 5
    TOUCH_MIN_CHAN_ID + 3, // 6

    TOUCH_MIN_CHAN_ID + 5,  // 7
    TOUCH_MIN_CHAN_ID + 2,  // 8
    TOUCH_MIN_CHAN_ID + 10, // 9

    TOUCH_MIN_CHAN_ID + 4,  // *
    TOUCH_MIN_CHAN_ID + 11, // 0
    TOUCH_MIN_CHAN_ID + 12, // #
};

// 键值字符（顺序必须与上面通道一致）
static const char touch_keys[] = {
    '1',
    '2',
    '3',
    '4',
    '5',
    '6',
    '7',
    '8',
    '9',
    '*',
    '0',
    '#',
};

#define TOUCH_KEY_COUNT (sizeof(touch_keys))

#define APP_TOUCH_THRESH2BM_RATIO 0.3f

// =======================================================================
// 根据通道号查找键值
// =======================================================================

char touch_key_from_channel(uint8_t ch)
{
    for (int i = 0; i < TOUCH_KEY_COUNT; i++)
    {
        if (touch_channels[i] == ch)
            return touch_keys[i];
    }
    return 0;
}

// =======================================================================
// 事件回调
// =======================================================================

static bool on_touch_active(touch_sensor_handle_t sens,
                            const touch_active_event_data_t *event,
                            void *arg)
{
    char key = touch_key_from_channel(event->chan_id);
    if (key)
        ESP_EARLY_LOGI(TAG, "Key '%c' Press", key);
    else
        ESP_EARLY_LOGI(TAG, "Unknown CH %d Press", event->chan_id);

    return false;
}

static bool on_touch_inactive(touch_sensor_handle_t sens,
                              const touch_inactive_event_data_t *event,
                              void *arg)
{
    char key = touch_key_from_channel(event->chan_id);
    if (key)
        ESP_EARLY_LOGI(TAG, "Key '%c' Release", key);
    else
        ESP_EARLY_LOGI(TAG, "Unknown CH %d Release", event->chan_id);

    return false;
}

// =======================================================================
// 初始标定：扫描基线，计算动态阈值
// =======================================================================

static void do_initial_scanning(touch_sensor_handle_t sens,
                                touch_channel_handle_t *handles)
{
    touch_sensor_enable(sens);

    for (int i = 0; i < 3; i++)
        touch_sensor_trigger_oneshot_scanning(sens, 2000);

    touch_sensor_disable(sens);

    for (int i = 0; i < TOUCH_KEY_COUNT; i++)
    {

        uint32_t bm[1] = {};
        touch_channel_config_t cfg = {
            .active_thresh = {0},
            .charge_speed = TOUCH_CHARGE_SPEED_7,
            .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
        };

#if SOC_TOUCH_SUPPORT_BENCHMARK
        touch_channel_read_data(handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, bm);
#else
        touch_channel_read_data(handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, bm);
#endif

        cfg.active_thresh[0] = (uint32_t)(bm[0] * APP_TOUCH_THRESH2BM_RATIO);

        ESP_LOGI(TAG,
                 "CH %d BM:%" PRIu32 " TH:%" PRIu32,
                 touch_channels[i], bm[0], cfg.active_thresh[0]);

        touch_sensor_reconfig_channel(handles[i], &cfg);
    }
}

// =======================================================================
// 初始化入口
// =======================================================================

esp_err_t app_touch_initialization(void)
{
    touch_sensor_handle_t sens = NULL;
    touch_channel_handle_t ch[TOUCH_KEY_COUNT];

    // 控制器创建
    touch_sensor_sample_config_t sample_cfg[1] =
        {TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2)};

    touch_sensor_config_t sens_cfg =
        TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample_cfg);

    ESP_RETURN_ON_ERROR(touch_sensor_new_controller(&sens_cfg, &sens), TAG,
                        "Create controller failed");

    // 创建通道
    for (int i = 0; i < TOUCH_KEY_COUNT; i++)
    {

        touch_channel_config_t cfg = {
            .active_thresh = {2000},
            .charge_speed = TOUCH_CHARGE_SPEED_7,
            .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
        };

        ESP_RETURN_ON_ERROR(
            touch_sensor_new_channel(sens, touch_channels[i], &cfg, &ch[i]),
            TAG, "Create channel failed");

        touch_chan_info_t info;
        touch_sensor_get_channel_info(ch[i], &info);

        ESP_LOGI(TAG, "Key '%c': CH %d -> GPIO%d",
                 touch_keys[i], touch_channels[i], info.chan_gpio);
    }

    // 滤波器设置
    touch_sensor_filter_config_t filter = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    touch_sensor_config_filter(sens, &filter);

    // 初始扫描
    do_initial_scanning(sens, ch);

    // 注册回调
    touch_event_callbacks_t cb = {
        .on_active = on_touch_active,
        .on_inactive = on_touch_inactive,
    };
    touch_sensor_register_callbacks(sens, &cb, NULL);

    // 启动扫描
    touch_sensor_enable(sens);
    touch_sensor_start_continuous_scanning(sens);

    ESP_LOGI(TAG, "Touch driver initialized with 12 keys.");
    return ESP_OK;
}
