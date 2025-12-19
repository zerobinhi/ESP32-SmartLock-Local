#include "touch.h"

static const char *TAG = "touch";

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

QueueHandle_t touch_key_queue = NULL; // 触摸按键事件队列

char g_touch_password[TOUCH_PASSWORD_LEN + 1]; // 存储的密码
char g_input_password[TOUCH_PASSWORD_LEN + 1]; // 当前输入
uint8_t g_input_len = 0;

// 根据通道号查找键值
static char touch_key_from_channel(uint8_t ch)
{
    for (int i = 0; i < sizeof(touch_keys); i++)
    {
        if (touch_channels[i] == ch)
        {
            return touch_keys[i];
        }
    }
    return 0;
}

static bool on_touch_active(touch_sensor_handle_t sens, const touch_active_event_data_t *event, void *arg)
{
    char key = touch_key_from_channel(event->chan_id);
    if (!key)
        return false;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(touch_key_queue, &key, &xHigherPriorityTaskWoken);

    return xHigherPriorityTaskWoken == pdTRUE;
}

static bool on_touch_inactive(touch_sensor_handle_t sens, const touch_inactive_event_data_t *event, void *arg)
{
    return false;
}

// 初始标定：扫描基线，计算动态阈值
static void do_initial_scanning(touch_sensor_handle_t sens, touch_channel_handle_t *handles)
{
    touch_sensor_enable(sens);

    for (int i = 0; i < 3; i++)
        touch_sensor_trigger_oneshot_scanning(sens, 2000);

    touch_sensor_disable(sens);

    for (int i = 0; i < sizeof(touch_keys); i++)
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

        cfg.active_thresh[0] = (uint32_t)(bm[0] * TOUCH_THRESH2BM_RATIO);

        ESP_LOGI(TAG,
                 "CH %d BM:%" PRIu32 " TH:%" PRIu32,
                 touch_channels[i], bm[0], cfg.active_thresh[0]);

        touch_sensor_reconfig_channel(handles[i], &cfg);
    }
}

// 触摸按键任务
static void touch_key_task(void *arg)
{
    char key;

    while (1)
    {
        if (xQueueReceive(touch_key_queue, &key, portMAX_DELAY))
        {
            ESP_LOGI(TAG, "Key: %c", key);

            if (key >= '0' && key <= '9')
            {
                if (g_input_len < TOUCH_PASSWORD_LEN)
                {
                    g_input_password[g_input_len++] = key;
                    g_input_password[g_input_len] = '\0';
                }
            }
            else if (key == '*')
            {
                g_input_len = 0;
                memset(g_input_password, 0, sizeof(g_input_password));
            }
            else if (key == '#')
            {
                if (g_input_len == TOUCH_PASSWORD_LEN)
                {
                    if (strcmp(g_input_password, g_touch_password) == 0)
                    {
                        ESP_LOGI(TAG, "Password OK");
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Password ERROR");
                    }
                }
                else
                {
                    ESP_LOGW(TAG, "Password length error (%d)", g_input_len);
                }

                g_input_len = 0;
                memset(g_input_password, 0, sizeof(g_input_password));
            }
        }
    }
}

// 读取或初始化密码
static void touch_password_init(void)
{
    size_t len = sizeof(g_touch_password);

    esp_err_t err = nvs_custom_get_str(NULL, "NVS_TOUCH", "touch_password", g_touch_password, &len);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Password not found, set default");
        strcpy(g_touch_password, DEFAULT_PASSWORD);

        nvs_custom_set_str(NULL, "NVS_TOUCH", "touch_password", g_touch_password);
    }
    else
    {
        ESP_LOGI(TAG, "Password loaded: %s", g_touch_password);
    }
    xTaskCreate(touch_key_task, "touch_key_task", 4096, NULL, 10, NULL);
}

// 初始化入口
esp_err_t touch_initialization(void)
{
    touch_key_queue = xQueueCreate(8, sizeof(char));
    if (touch_key_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create touch key queue");
        return ESP_FAIL;
    }

    touch_sensor_handle_t sens = NULL;
    touch_channel_handle_t ch[sizeof(touch_keys)];

    // 控制器创建
    touch_sensor_sample_config_t sample_cfg[1] =
        {TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2)};

    touch_sensor_config_t sens_cfg =
        TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample_cfg);

    ESP_RETURN_ON_ERROR(touch_sensor_new_controller(&sens_cfg, &sens), TAG,
                        "Create controller failed");

    // 创建通道
    for (int i = 0; i < sizeof(touch_keys); i++)
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

    touch_password_init();

    return ESP_OK;
}
