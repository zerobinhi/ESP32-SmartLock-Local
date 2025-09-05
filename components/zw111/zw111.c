#include "zw111.h"

// ========================== 全局变量定义 ==========================
/** 帧头固定值（2字节），与模块通信的起始标识 */
const uint8_t FRAME_HEADER[2] = {0xEF, 0x01};

struct fingerprint_device zw111 = {0};

// 指纹模块的信号量，仅用于触摸之后开启模块
SemaphoreHandle_t fingerprint_semaphore = NULL;
QueueHandle_t xQueue_buzzer;

const uint8_t BUZZER_OPEN = 0;
const uint8_t BUZZER_NOOPEN = 2;
const uint8_t BUZZER_TOUCH = 4;
const uint8_t BUZZER_CARD = 6;

static QueueHandle_t uart2_queue;
static const char *TAG = "SmartLock Fingerprint";

// ========================== 通用工具函数 ==========================

/**
 * @brief 计算数据帧的校验和（累加和算法）
 * @param frameData 数据帧缓冲区（需非空）
 * @param frameLen 数据帧总长度（需大于校验和起始索引+校验和长度）
 * @return uint16_t 计算得到的16位校验和（高字节在前），参数无效返回0
 */
static uint16_t calculate_checksum(const uint8_t *frameData, uint16_t frameLen)
{
    if (frameData == NULL || frameLen <= CHECKSUM_START_INDEX + CHECKSUM_LEN)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "校验和计算失败：数据为空或长度不足（需大于%d字节，当前%d字节）", CHECKSUM_START_INDEX + CHECKSUM_LEN, frameLen);
#endif
        return 0;
    }

    uint16_t checksum = 0;
    uint8_t checksumEndIndex = frameLen - CHECKSUM_LEN - 1; // 校验和前一字节索引
    // 累加校验范围：从CHECKSUM_START_INDEX到checksumEndIndex
    for (uint8_t i = CHECKSUM_START_INDEX; i <= checksumEndIndex; i++)
    {
        checksum += frameData[i];
    }

    return (checksum & 0xFFFF);
}

/**
 * @brief 校验指纹模块接收数据的有效性（重点验证帧结构和校验和）
 * @param recvData 接收的数据包缓冲区（需非空）
 * @param dataLen 实际接收的字节数（必须显式传入，不可用strlen计算）
 * @return esp_err_t 校验结果：ESP_OK=数据是有效的，ESP_FAIL=数据是无效的
 */
static esp_err_t verify_received_data(const uint8_t *recvData, uint16_t dataLen)
{
    // 基础合法性检查
    if (recvData == NULL || dataLen < MIN_RESPONSE_LEN)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "校验失败：数据为空或长度不足（最小需%d字节，当前%d字节）", MIN_RESPONSE_LEN, dataLen);
#endif
        return ESP_FAIL;
    }

    // 验证帧头（前2字节）
    if (recvData[0] != FRAME_HEADER[0] || recvData[1] != FRAME_HEADER[1])
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "校验失败：帧头不匹配（期望%02X%02X，实际%02X%02X）", FRAME_HEADER[0], FRAME_HEADER[1], recvData[0], recvData[1]);
#endif
        return ESP_FAIL;
    }

    // 验证设备地址（2-5字节）
    for (int i = 2; i < 6; i++)
    {
        if (recvData[i] != zw111.deviceAddress[i - 2])
        {
#ifdef DEBUG
            ESP_LOGE(TAG, "校验失败：设备地址不匹配（期望%02X%02X%02X%02X，实际%02X%02X%02X%02X）",
                     zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3],
                     recvData[2], recvData[3], recvData[4], recvData[5]);
#endif
            return ESP_FAIL;
        }
    }

    // 验证包标识（第6字节）
    if (recvData[6] != PACKET_RESPONSE)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "校验失败：包标识错误（期望应答包%02X，实际%02X）", PACKET_RESPONSE, recvData[6]);
#endif
        return ESP_FAIL;
    }

    // 验证数据长度（7-8字节，高字节在前）
    uint16_t expectedDataLen = (recvData[7] << 8) | recvData[8]; // 数据区长度
    if (9 + expectedDataLen != dataLen)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "校验失败：长度不匹配（期望总长度%u，实际%u）", 9 + expectedDataLen, dataLen);
#endif
        return ESP_FAIL;
    }

    // 验证校验和（最后2字节，高字节在前）
    uint16_t receivedChecksum = (recvData[dataLen - 2] << 8) | recvData[dataLen - 1];

    if (calculate_checksum(recvData, dataLen) != receivedChecksum)
    {

#ifdef DEBUG
        ESP_LOGE(TAG, "校验失败：校验和不匹配（期望0x%04X，实际0x%04X）", calculate_checksum(recvData, dataLen), receivedChecksum);
#endif
        return ESP_FAIL;
    }

#ifdef DEBUG
    ESP_LOGI(TAG, "校验成功：数据有效");
#endif
    return ESP_OK;
}

// ========================== 功能函数 ==========================
/**
 * @brief 指纹模块自动注册函数
 * @param ID 指纹ID号（0-99，超出范围返回失败）
 * @param enrollTimes 录入次数（2-255，超出返回失败）
 * @param ledControl 采图背光灯控制：false=常亮；true=采图成功后熄灭
 * @param preprocess 采图预处理控制：false=不预处理；true=开启预处理
 * @param returnStatus 注册状态返回控制：false=返回状态；true=不返回状态
 * @param allowOverwrite ID覆盖控制：false=不允许覆盖；true=允许覆盖
 * @param allowDuplicate 重复注册控制：false=允许重复；true=禁止重复
 * @param requireRemove 手指离开要求：false=需离开；true=无需离开
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=命令发送失败
 */
static esp_err_t auto_enroll(uint16_t ID, uint8_t enrollTimes,
                             bool ledControl, bool preprocess,
                             bool returnStatus, bool allowOverwrite,
                             bool allowDuplicate, bool requireRemove)
{
    // 参数合法性检查
    if (ID >= 100)
    {
        // ID超出范围
#ifdef DEBUG
        ESP_LOGE(TAG, "注册失败：ID超出范围（需0-99，当前%d）", ID);
#endif
        return ESP_FAIL;
    }
    if (enrollTimes < 2)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "注册失败：录入次数超出范围（需2-255，当前%d）", enrollTimes);
#endif
        return ESP_FAIL;
    }

    // 组装控制参数（bit0-bit5）
    uint16_t param = 0;
    param |= (ledControl ? 1 << 0 : 0);     // bit0: 背光灯控制
    param |= (preprocess ? 1 << 1 : 0);     // bit1: 预处理控制
    param |= (returnStatus ? 1 << 2 : 0);   // bit2: 状态返回控制
    param |= (allowOverwrite ? 1 << 3 : 0); // bit3: ID覆盖控制
    param |= (allowDuplicate ? 1 << 4 : 0); // bit4: 重复注册控制
    param |= (requireRemove ? 1 << 5 : 0);  // bit5: 手指离开控制

    // 构建数据帧（共17字节）
    uint8_t frame[17] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x08,                                                                                     // 数据长度(2字节，固定8字节)
        CMD_AUTO_ENROLL,                                                                                // 指令码(1字节)
        (uint8_t)(ID >> 8), (uint8_t)ID,                                                                // 指纹ID(2字节，高字节在前)
        enrollTimes,                                                                                    // 录入次数(1字节)
        (uint8_t)(param >> 8), (uint8_t)param,                                                          // 控制参数(2字节，高字节在前)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };

    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[15] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[16] = (uint8_t)(checksum & 0xFF); // 校验和低字节

// 调试输出帧信息
#ifdef DEBUG
    ESP_LOGI(TAG, "发送自动注册指令: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
#endif

    // UART发送命令
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // 发送成功（所有字节都被推入FIFO）
#ifdef DEBUG
        ESP_LOGI(TAG, "自动注册指令发送成功");
#endif
        return ESP_OK;
    }
    else
    {
        // 发送失败（参数错误或未全部发送）
#ifdef DEBUG
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", len);
#endif
        return ESP_FAIL;
    }
}

/**
 * @brief 指纹模块自动识别函数
 * @param ID 指纹ID号：具体数值(0-99)=验证指定ID；0xFFFF=验证所有已注册指纹
 * @param scoreLevel 比对分数等级（1-5，等级越高严格度越高，默认建议2）
 * @param ledControl 采图背光灯控制：false=常亮；true=采图成功后熄灭
 * @param preprocess 采图预处理控制：false=不预处理；true=开启预处理
 * @param returnStatus 识别状态返回控制：false=返回状态；true=不返回状态
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=参数无效或命令发送失败
 */
static esp_err_t auto_identify(uint16_t ID, uint8_t scoreLevel, bool ledControl, bool preprocess, bool returnStatus)
{

    if (scoreLevel < 1 || scoreLevel > 5)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "自动识别失败：分数等级无效（需1-5，当前%d）", scoreLevel);
#endif
        return ESP_FAIL;
    }

    // 组装控制参数（bit0-bit2）
    uint16_t param = 0;
    param |= (ledControl ? 1 << 0 : 0);   // bit0: 背光灯控制
    param |= (preprocess ? 1 << 1 : 0);   // bit1: 预处理控制
    param |= (returnStatus ? 1 << 2 : 0); // bit2: 状态返回控制

    // 构建数据帧（共17字节）
    uint8_t frame[17] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x08,                                                                                     // 数据长度(2字节，固定8字节)
        CMD_AUTO_IDENTIFY,                                                                              // 指令码(1字节)
        scoreLevel,                                                                                     // 分数等级(1字节)
        (uint8_t)(ID >> 8), (uint8_t)ID,                                                                // 指纹ID(2字节，高字节在前)
        (uint8_t)(param >> 8), (uint8_t)param,                                                          // 控制参数(2字节，高字节在前)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };

    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[15] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[16] = (uint8_t)(checksum & 0xFF); // 校验和低字节

#ifdef DEBUG
    ESP_LOGI(TAG, "发送自动识别指令: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
#endif
    // UART发送命令
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // 发送成功（所有字节都被推入FIFO）
#ifdef DEBUG
        ESP_LOGI(TAG, "自动识别指令发送成功");
#endif
        return ESP_OK;
    }
    else
    {
        // 发送失败（参数错误或未全部发送）
#ifdef DEBUG
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", len);
#endif
        return ESP_FAIL;
    }
}

/**
 * @brief 指纹模块LED控制函数（支持呼吸、闪烁、开关等模式）
 * @param functionCode 功能码（1-6，参考BLN_xxx宏定义，如BLN_BREATH=呼吸灯）
 * @param startColor 起始颜色（bit0-蓝,bit1-绿,bit2-红，参考LED_xxx宏定义）
 * @param endColor 结束颜色（仅功能码1-呼吸灯有效，其他模式忽略）
 * @param cycleTimes 循环次数（仅功能码1-呼吸灯/2-闪烁灯有效，0=无限循环）
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=参数无效或命令发送失败
 */
static esp_err_t control_led(uint8_t functionCode, uint8_t startColor,
                             uint8_t endColor, uint8_t cycleTimes)
{
    // 参数合法性检查
    if (functionCode < BLN_BREATH || functionCode > BLN_FADE_OUT)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "LED控制失败：功能码无效（需1-6，当前%d）", functionCode);
#endif
        return ESP_FAIL;
    }

    // 过滤颜色参数无效位（仅保留低3位）
    if ((startColor & 0xF8) != 0)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "LED控制警告：起始颜色仅低3位有效，已过滤为0x%02X\n", startColor & 0x07);
#endif
        startColor &= 0x07;
    }
    if ((endColor & 0xF8) != 0)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "LED控制警告：结束颜色仅低3位有效，已过滤为0x%02X\n", endColor & 0x07);
#endif
        endColor &= 0x07;
    }

    // 构建数据帧（共16字节）
    uint8_t frame[16] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x07,                                                                                     // 数据长度(2字节，固定7字节)
        CMD_CONTROL_BLN,                                                                                // 指令码(1字节)
        functionCode,                                                                                   // 功能码(1字节)
        startColor,                                                                                     // 起始颜色(1字节)
        endColor,                                                                                       // 结束颜色(1字节)
        cycleTimes,                                                                                     // 循环次数(1字节)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };

    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[14] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[15] = (uint8_t)(checksum & 0xFF); // 校验和低字节

// 调试输出帧信息
#ifdef DEBUG
    ESP_LOGI(TAG, "发送LED控制帧: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
#endif

    // UART发送命令
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // 发送成功（所有字节都被推入FIFO）
#ifdef DEBUG
        ESP_LOGI(TAG, "LED控制指令发送成功");
#endif
        return ESP_OK;
    }
    else
    {
        // 发送失败（参数错误或未全部发送）
#ifdef DEBUG
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", len);
#endif
        return ESP_FAIL;
    }
}

/**
 * @brief 指纹模块LED跑马灯控制函数（七彩循环模式）
 * @param startColor 起始颜色配置（参考LED_xxx宏定义，仅低3位有效）
 * @param timeBit 呼吸周期时间参数（1-100，对应0.1秒-10秒）
 * @param cycleTimes 循环次数（0=无限循环）
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=参数无效或命令发送失败
 */
static esp_err_t control_colorful_led(uint8_t startColor, uint8_t timeBit, uint8_t cycleTimes)
{
    // 参数合法性检查
    if (timeBit < 1 || timeBit > 100)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "跑马灯控制失败：时间参数无效（需1-100，当前%d）", timeBit);
#endif
        return ESP_FAIL;
    }

    // 过滤颜色参数无效位
    if ((startColor & 0xF8) != 0)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "跑马灯控制失败：起始颜色仅低3位有效，已过滤为0x%02X\n", startColor & 0x07);
#endif
        startColor &= 0x07;
    }

    // 构建数据帧（共17字节）
    uint8_t frame[17] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x08,                                                                                     // 数据长度(2字节，固定8字节)
        CMD_CONTROL_BLN,                                                                                // 指令码(1字节)
        BLN_COLORFUL,                                                                                   // 功能码(1字节，七彩模式)
        startColor,                                                                                     // 起始颜色(1字节)
        0x11,                                                                                           // 占空比固定值
        cycleTimes,                                                                                     // 循环次数(1字节)
        timeBit,                                                                                        // 周期时间参数(1字节)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };

    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[15] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[16] = (uint8_t)(checksum & 0xFF); // 校验和低字节

// 调试输出帧信息
#ifdef DEBUG
    ESP_LOGI(TAG, "发送跑马灯控制帧: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
#endif

    // UART发送命令
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // 发送成功（所有字节都被推入FIFO）
#ifdef DEBUG
        ESP_LOGI(TAG, "跑马灯控制指令发送成功");
#endif
        return ESP_OK;
    }
    else
    {
        // 发送失败（参数错误或未全部发送）
#ifdef DEBUG
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", len);
#endif
        return ESP_FAIL;
    }
}

/**
 * @brief 删除指定数量的指纹（从指定ID开始连续删除）
 * @param ID 起始指纹ID（0-99，超出范围返回失败）
 * @param count 删除数量（1-100，需确保不超出ID范围）
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=参数无效或命令发送失败
 */
esp_err_t delete_char(uint16_t ID, uint16_t count)
{
    // 参数合法性检查
    if (ID >= 100)
    {
        // ID超出范围
#ifdef DEBUG
        ESP_LOGE(TAG, "删除失败：起始ID超出范围（需0-99，当前%d）", ID);
#endif
        return ESP_FAIL;
    }
    if (count == 0 || count > 100 || (ID + count) > 100)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "删除失败：数量无效（需1-100且不超出ID范围，当前数量%d）", count);
#endif
        // 数量无效或超出ID范围
        return ESP_FAIL;
    }

    // 构建数据帧（共16字节）
    uint8_t frame[16] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x07,                                                                                     // 数据长度(2字节，固定7字节)
        CMD_DELET_CHAR,                                                                                 // 指令码(1字节)
        (uint8_t)(ID >> 8), (uint8_t)ID,                                                                // 起始ID(2字节，高字节在前)
        (uint8_t)(count >> 8), (uint8_t)count,                                                          // 删除数量(2字节，高字节在前)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };

    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[14] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[15] = (uint8_t)(checksum & 0xFF); // 校验和低字节

#ifdef DEBUG
    ESP_LOGI(TAG, "发送删除指纹帧: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
#endif

    // UART发送命令
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // 发送成功（所有字节都被推入FIFO）
#ifdef DEBUG
        ESP_LOGI(TAG, "删除指纹指令发送成功");
#endif
        return ESP_OK;
    }
    else
    {
        // 发送失败（参数错误或未全部发送）
#ifdef DEBUG
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", len);
#endif
        return ESP_FAIL;
    }
}

/**
 * @brief 清空模块中所有已注册的指纹
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=命令发送失败
 */
static esp_err_t empty()
{
    // 构建数据帧（共12字节）
    uint8_t frame[12] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x03,                                                                                     // 数据长度(2字节，固定3字节)
        CMD_EMPTY,                                                                                      // 指令码(1字节)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };

    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[10] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[11] = (uint8_t)(checksum & 0xFF); // 校验和低字节

    // 调试输出帧信息
#ifdef DEBUG
    ESP_LOGI(TAG, "发送清空指纹帧: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
#endif

    // UART发送命令
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // 发送成功（所有字节都被推入FIFO）
#ifdef DEBUG
        ESP_LOGI(TAG, "清空指纹指令发送成功");
#endif
        return ESP_OK;
    }
    else
    {
        // 发送失败（参数错误或未全部发送）
#ifdef DEBUG
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", len);
#endif
        return ESP_FAIL;
    }
}

/**
 * @brief 取消模块当前正在执行的操作（如注册、识别等）
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=命令发送失败
 */
static esp_err_t cancel()
{
    // 构建数据帧（共12字节）
    uint8_t frame[12] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x03,                                                                                     // 数据长度(2字节，固定3字节)
        CMD_CANCEL,                                                                                     // 指令码(1字节)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };

    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[10] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[11] = (uint8_t)(checksum & 0xFF); // 校验和低字节

// 调试输出帧信息
#ifdef DEBUG
    ESP_LOGI(TAG, "发送取消操作帧: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
#endif

    // UART发送命令
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // 发送成功（所有字节都被推入FIFO）
#ifdef DEBUG
        ESP_LOGI(TAG, "取消操作指令发送成功");
#endif
        return ESP_OK;
    }
    else
    {
        // 发送失败（参数错误或未全部发送）
#ifdef DEBUG
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", len);
#endif
        return ESP_FAIL;
    }
}

/**
 * @brief 控制模块进入休眠模式（降低功耗，需外部唤醒）
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=命令发送失败
 */
static esp_err_t sleep()
{
    // 构建数据帧（共12字节）
    uint8_t frame[12] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x03,                                                                                     // 数据长度(2字节，固定3字节)
        CMD_SLEEP,                                                                                      // 指令码(1字节)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };

    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[10] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[11] = (uint8_t)(checksum & 0xFF); // 校验和低字节

// 调试输出帧信息
#ifdef DEBUG
    ESP_LOGI(TAG, "发送休眠指令帧: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
#endif

    // UART发送命令
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // 发送成功（所有字节都被推入FIFO）
#ifdef DEBUG
        ESP_LOGI(TAG, "休眠指令发送成功");
#endif
        return ESP_OK;
    }
    else
    {
        // 发送失败（参数错误或未全部发送）
#ifdef DEBUG
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", len);
#endif
        return ESP_FAIL;
    }
}

/**
 * @brief 读取模块中的指纹索引表（获取已注册指纹ID）
 * @param page 页码（0-4，每页对应20枚指纹，共100枚）
 * @return esp_err_t 操作结果：ESP_OK=命令发送成功，ESP_FAIL=参数无效或命令发送失败
 */
static esp_err_t read_index_table(uint8_t page)
{
    if (page > 4)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "页码无效（需0-4，当前%d）", page);
#endif
        return ESP_FAIL;
    }
    // 参数合法性检查（隐含，页码超出范围模块会返回错误）
    uint8_t frame[13] = {
        FRAME_HEADER[0], FRAME_HEADER[1],                                                               // 帧头(2字节)
        zw111.deviceAddress[0], zw111.deviceAddress[1], zw111.deviceAddress[2], zw111.deviceAddress[3], // 设备地址(4字节)
        PACKET_CMD,                                                                                     // 包标识(1字节)
        0x00, 0x04,                                                                                     // 数据长度(2字节，固定4字节)
        CMD_READ_INDEX_TABLE,                                                                           // 指令码(1字节)
        page,                                                                                           // 页码(1字节)
        0x00, 0x00                                                                                      // 校验和(2字节，待计算)
    };

    // 计算并填充校验和
    uint16_t checksum = calculate_checksum(frame, sizeof(frame));
    frame[11] = (uint8_t)(checksum >> 8);   // 校验和高字节
    frame[12] = (uint8_t)(checksum & 0xFF); // 校验和低字节

#ifdef DEBUG
    ESP_LOGI(TAG, "读取索引表: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
#endif
    // UART发送命令
    int len = uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame));
    if (len == sizeof(frame))
    {
        // 发送成功（所有字节都被推入FIFO）
#ifdef DEBUG
        ESP_LOGI(TAG, "读取索引表指令发送成功");
#endif
        return ESP_OK;
    }
    else
    {
        // 发送失败（参数错误或未全部发送）
#ifdef DEBUG
        ESP_LOGE(TAG, "发送失败，实际发送字节数: %d", len);
#endif
        return ESP_FAIL;
    }
}

/**
 * @brief 解析读索引表命令的返回数据，提取已注册指纹ID
 * @param recvData 接收的数据包缓冲区
 * @param dataLen 实际接收的字节数（需显式传入）
 * @return esp_err_t 解析结果：ESP_OK=解析成功，ESP_FAIL=数据无效或解析失败
 */
static esp_err_t fingerprint_parse_frame(const uint8_t *recvData, uint16_t dataLen)
{
    // 初始化指纹ID数组（0xFF表示未使用）
    memset(zw111.fingerIDArray, 0xFF, sizeof(zw111.fingerIDArray));
    zw111.fingerNumber = 0;

    // 掩码数组（用于检测每个bit是否置位）
    uint8_t mask[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
    uint8_t tempCount = 0; // 临时计数变量

    // 解析数据区（索引表数据从第10字节开始，共13字节，对应104个bit，实际有效100个）
    for (uint8_t i = 10; i <= 22; i++) // i=10到22共13字节
    {
        uint8_t byteData = recvData[i];
        if (byteData == 0)
            continue; // 跳过无指纹的字节

        // 检测当前字节的每个bit（对应8个指纹ID）
        for (uint8_t j = 0; j < 8; j++)
        {
            if (byteData & mask[j])
            {
                // 计算指纹ID：(字节偏移)×8 + bit偏移
                uint8_t fingerID = (i - 10) * 8 + j;
                if (fingerID < 100) // 仅保留有效ID（0-99）
                {
                    zw111.fingerIDArray[tempCount] = fingerID;
                    tempCount++;
                    if (tempCount >= 100)
                        break; // 达到最大容量则停止
                }
            }
        }
        if (tempCount >= 100)
            break; // 达到最大容量则停止
    }

    // 更新有效指纹数量
    zw111.fingerNumber = tempCount;

#ifdef DEBUG

    if (zw111.fingerNumber > 0)
    {
        ESP_LOGI(TAG, "检测到%d个已注册指纹ID: ", zw111.fingerNumber);
        for (size_t i = 0; i < zw111.fingerNumber; i++)
        {
            ESP_LOGI(TAG, "%d ", zw111.fingerIDArray[i]);
        }
    }
    else
    {
        ESP_LOGI(TAG, "未检测到任何已注册指纹");
    }

#endif

    return ESP_OK;
}
/**
 * 在指纹列表中查找最小未使用序号并返回其序号
 * @return 最小未使用的指纹ID，无可用ID时返回-1
 */
uint8_t get_mini_unused_id()
{
    // 特殊情况：没有任何指纹，直接返回0
    if (zw111.fingerNumber == 0)
    {
        return 0;
    }

    // 检查0是否被使用（数组从0开始，但ID可能不从0开始）
    if (zw111.fingerIDArray[0] > 0)
    {
        return 0;
    }

    // 遍历已排序的ID数组，查找连续序列中的空缺
    for (uint8_t i = 0; i < zw111.fingerNumber - 1; i++)
    {
        // 当前ID和下一个ID之间存在空缺
        if (zw111.fingerIDArray[i + 1] > zw111.fingerIDArray[i] + 1)
        {
            return zw111.fingerIDArray[i] + 1;
        }
    }

    // 所有已有ID是连续的，返回最后一个ID的下一个值
    uint8_t last_id = zw111.fingerIDArray[zw111.fingerNumber - 1];
    if (last_id + 1 < 100) // 确保不超过最大支持的ID范围
    {
        return last_id + 1;
    }

    // 所有可能的ID都已使用
    return 100;
}

/**
 * 插入新注册的指纹ID到数组中，保持数组有序性
 * @param new_id 要插入的新指纹ID（应通过get_mini_unused_id()获取）
 * @return 成功插入返回ESP_OK，失败返回ESP_FAIL（数组已满）
 */
esp_err_t insert_fingerprint_id(uint8_t new_id)
{
    // 检查数组是否已满
    if (zw111.fingerNumber >= 100)
    {
        return ESP_FAIL; // 达到最大容量，无法插入
    }

    // 找到插入位置
    uint8_t insert_pos = 0;
    while (insert_pos < zw111.fingerNumber &&
           zw111.fingerIDArray[insert_pos] < new_id)
    {
        insert_pos++;
    }

    // 移动元素为新ID腾出位置
    for (uint8_t i = zw111.fingerNumber; i > insert_pos; i--)
    {
        zw111.fingerIDArray[i] = zw111.fingerIDArray[i - 1];
    }

    // 插入新ID
    zw111.fingerIDArray[insert_pos] = new_id;

    // 更新指纹数量
    zw111.fingerNumber++;

#ifdef DEBUG
    ESP_LOGI(TAG, "插入指纹ID成功: %d", zw111.fingerIDArray[insert_pos]);
#endif

    return ESP_OK; // 插入成功
}

/**
 * @brief 模块取消当前的操作并执行某条指令
 * @note 该函数会取消当前正在进行的指纹操作（如注册、识别等），并将状态设置为取消状态
 * @return void
 */

void cancel_current_operation_and_execute_command()
{
    zw111.state = 0x0A; // 切换为取消状态
    // 发送取消命令
    if (cancel() == ESP_OK)
    {
#ifdef DEBUG
        ESP_LOGI(TAG, "准备取消当前操作，模块状态已切换为取消状态");
#endif
    }
    else
    {
        // 取消操作失败
#ifdef DEBUG
        ESP_LOGE(TAG, "取消当前操作失败");
#endif
    }
}

/**
 * @brief 打开指纹模块
 * @note 该函数会给指纹模块供电，并设置状态为已供电
 * @return void
 */
void turn_on_fingerprint()
{
    gpio_set_level(FINGERPRINT_CTL_PIN, 0); // 给指纹模块供电
    fingerprint_initialization_uart();      // 初始化UART通信
    xTaskCreate(uart_task, "uart_task", 8192, NULL, 10, NULL);
    zw111.power = true;
#ifdef DEBUG
    ESP_LOGI(TAG, "指纹模块已供电");
#endif
}

/**
 * @brief 模块准备关闭
 * @note 该函数会准备关闭指纹模块，发送休眠命令并设置状态为休眠
 * @return void
 */
void prepare_turn_off_fingerprint()
{
    zw111.state = 0x0B; // 切换为休眠状态
    // 发送休眠命令
    if (sleep() == ESP_OK)
    {
#ifdef DEBUG
        ESP_LOGI(TAG, "准备休眠，模块状态已切换为休眠状态");
#endif
    }
    else
    {
        // 休眠操作失败
#ifdef DEBUG
        ESP_LOGE(TAG, "休眠当前操作失败");
#endif
    }
}
/**
 * @brief 触摸中断服务程序
 * @param arg 中断参数（传入GPIO编号）
 * @return void
 */
void IRAM_ATTR fingerprint_gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    if (gpio_num == FINGERPRINT_INT_PIN && gpio_get_level(FINGERPRINT_INT_PIN) == 1)
    {
        xSemaphoreGiveFromISR(fingerprint_semaphore, NULL);
    }
}
/**
 * @brief 初始化指纹模块UART通信
 * @return esp_err_t ESP_OK=初始化成功，其他=失败
 */
esp_err_t fingerprint_initialization_uart()
{
    esp_err_t ret = ESP_OK;

    // 检查是否已安装
    if (uart_is_driver_installed(EX_UART_NUM))
    {
#ifdef DEBUG
        ESP_LOGW(TAG, "UART驱动已安装，无需重复安装");
#endif
        return ESP_OK;
    }

    // 安装UART驱动
    ret = uart_driver_install(EX_UART_NUM, 1024, 1024, 5, &uart2_queue, 0);
    if (ret != ESP_OK)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "UART驱动安装失败: 0x%x", ret);
#endif
        return ret;
    }

    // 配置UART参数
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ret = uart_param_config(EX_UART_NUM, &uart_config);
    if (ret != ESP_OK)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "UART参数配置失败: 0x%x", ret);
#endif
        uart_driver_delete(EX_UART_NUM); // 回滚操作
        return ret;
    }

    // 设置UART引脚
    ret = uart_set_pin(EX_UART_NUM, FINGERPRINT_RX_PIN, FINGERPRINT_TX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "UART引脚配置失败: 0x%x", ret);
#endif
        uart_driver_delete(EX_UART_NUM); // 回滚操作
        return ret;
    }

    // 配置模式检测（原有功能保留）
    ret = uart_enable_pattern_det_baud_intr(EX_UART_NUM, 0x55, 1, 9, 20, 0);
    if (ret != ESP_OK)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "模式检测配置失败: 0x%x", ret);
#endif
        uart_driver_delete(EX_UART_NUM); // 回滚操作
        return ret;
    }

    // 重置模式队列（原有功能保留）
    ret = uart_pattern_queue_reset(EX_UART_NUM, 5);
    if (ret != ESP_OK)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "模式队列重置失败: 0x%x", ret);
#endif
        uart_driver_delete(EX_UART_NUM); // 回滚操作
        return ret;
    }

#ifdef DEBUG
    ESP_LOGI(TAG, "UART初始化成功");
#endif
    return ESP_OK;
}

/**
 * @brief 删除指纹模块UART通信
 * @return esp_err_t ESP_OK=删除成功，ESP_FAIL=删除失败
 */
esp_err_t fingerprint_deinitialization_uart()
{
    if (!uart_is_driver_installed(EX_UART_NUM))
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "UART驱动未安装，无法删除");
#endif
        return ESP_FAIL;
    }

    // 等待TX数据发送完成
    esp_err_t ret = uart_wait_tx_done(EX_UART_NUM, 100); // 超时100ticks
    if (ret == ESP_ERR_TIMEOUT)
    {
#ifdef DEBUG
        ESP_LOGW(TAG, "TX缓冲区数据未完全发送，强制删除");
#endif
    }

    // 清空RX缓冲区
    uart_flush_input(EX_UART_NUM);

    // 删除事件队列
    if (uart2_queue != NULL)
    {
        vQueueDelete(uart2_queue);
        uart2_queue = NULL;
    }

    // 删除UART驱动
    ret = uart_driver_delete(EX_UART_NUM);
    if (ret != ESP_OK)
    {
#ifdef DEBUG
        ESP_LOGE(TAG, "UART驱动删除失败: 0x%x", ret);
#endif
        return ret;
    }

#ifdef DEBUG
    ESP_LOGI(TAG, "UART驱动已删除");
#endif
    return ESP_OK;
}
/**
 * @brief 初始化指纹模块UART通信
 * @return esp_err_t ESP_OK=初始化成功，ESP_FAIL=数据无效或初始化失败
 */
esp_err_t fingerprint_initialization()
{
    // 初始化UART通信
    if (fingerprint_initialization_uart() != ESP_OK)
    {
        return ESP_FAIL;
    }

    // 初始化指纹模块数据结构
    zw111.deviceAddress[0] = 0xFF;
    zw111.deviceAddress[1] = 0xFF;
    zw111.deviceAddress[2] = 0xFF;
    zw111.deviceAddress[3] = 0xFF;

    gpio_config_t zw101_int_gpio_config = {
        .pin_bit_mask = (1ULL << FINGERPRINT_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE};
    gpio_config(&zw101_int_gpio_config);

    gpio_config_t fingerprint_ctl_gpio_config = {
        .pin_bit_mask = (1ULL << FINGERPRINT_CTL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&fingerprint_ctl_gpio_config);

    xQueue_buzzer = xQueueCreate(1, sizeof(uint8_t)); // 用于存储蜂鸣器鸣叫的方式
    fingerprint_semaphore = xSemaphoreCreateBinary(); // 仅用于触摸之后开启模块

    gpio_install_isr_service(0);
    gpio_isr_handler_add(FINGERPRINT_INT_PIN, fingerprint_gpio_isr_handler, (void *)FINGERPRINT_INT_PIN);

#ifdef DEBUG
    ESP_LOGI(TAG, "zw101 interrupt gpio configured");
#endif

    // Create a task to handler UART event from ISR
    xTaskCreate(uart_task, "uart_task", 8192, NULL, 10, NULL);
    xTaskCreate(fingerprint_task, "fingerprint_task", 8192, NULL, 10, NULL);
    xTaskCreate(buzzer_task, "buzzer_task", 2048, NULL, 10, NULL);

#ifdef DEBUG
    ESP_LOGI(TAG, "fingerprint device created");
#endif
    return ESP_OK;
}
// 蜂鸣器任务
void buzzer_task(void *pvParameters)
{
    uint8_t receivedMessage = 0;
    while (1)
    {
        if (xQueueReceive(xQueue_buzzer, &receivedMessage, (TickType_t)portMAX_DELAY) == pdPASS)
        {
            // ESP_LOGI(TAG, "receivedMessage: %d", receivedMessage);

            if (receivedMessage == BUZZER_OPEN) // 开门成功音
            {
                // gpio_set_level(LOCK_CTL_PIN, 1); // 给 电磁锁 通电
                // gpio_set_level(BUZZER_CTL_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(800));
                // gpio_set_level(BUZZER_CTL_PIN, 1);
                // gpio_set_level(LOCK_CTL_PIN, 0); // 给 电磁锁 断电
                prepare_turn_off_fingerprint(); // 准备关闭指纹模块
            }
            else if (receivedMessage == BUZZER_NOOPEN) // 不开门音
            {
                // gpio_set_level(BUZZER_CTL_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(150));
                // gpio_set_level(BUZZER_CTL_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(50));
                // gpio_set_level(BUZZER_CTL_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(150));
                // gpio_set_level(BUZZER_CTL_PIN, 1);
                prepare_turn_off_fingerprint(); // 准备关闭指纹模块
            }
            else if (receivedMessage == BUZZER_TOUCH) // 触摸屏按键音
            {
                // gpio_set_level(BUZZER_CTL_PIN, 0);
                // vTaskDelay(pdMS_TO_TICKS(50));
                // gpio_set_level(BUZZER_CTL_PIN, 1);
            }
            else if (receivedMessage == BUZZER_CARD) // 刷卡音
            {
                // gpio_set_level(BUZZER_CTL_PIN, 0);
                // vTaskDelay(pdMS_TO_TICKS(150));
                // gpio_set_level(BUZZER_CTL_PIN, 1);
            }
        }
    }
}
// 指纹任务
void fingerprint_task(void *pvParameters)
{
    while (1)
    {
        // 等待信号量被释放
        if (xSemaphoreTake(fingerprint_semaphore, portMAX_DELAY) == pdTRUE)
        {
            // 信号量被释放，表示指纹模块已准备就绪
#ifdef DEBUG
            ESP_LOGI(TAG, "指纹模块已准备就绪，开始处理任务");
            // 打印模块当前状态
            ESP_LOGI(TAG, "指纹模块状态: %s", zw111.power ? "已供电" : "未供电");
            ESP_LOGI(TAG, "指纹模块状态: %s",
                     zw111.state == 0x00   ? "初始状态"
                     : zw111.state == 0x01 ? "读索引表状态"
                     : zw111.state == 0x02 ? "注册指纹状态"
                     : zw111.state == 0x03 ? "删除指纹状态"
                     : zw111.state == 0x04 ? "验证指纹状态"
                     : zw111.state == 0x0A ? "取消状态"
                     : zw111.state == 0x0B ? "休眠状态"
                                           : "未知状态");
            ESP_LOGI(TAG, "指纹模块设备地址: %02X:%02X:%02X:%02X",
                     zw111.deviceAddress[0], zw111.deviceAddress[1],
                     zw111.deviceAddress[2], zw111.deviceAddress[3]);
            ESP_LOGI(TAG, "指纹模块已注册指纹数量: %d", zw111.fingerNumber);
            ESP_LOGI(TAG, "指纹模块已注册指纹ID: ");
            for (size_t i = 0; i < zw111.fingerNumber; i++)
            {
                ESP_LOGI(TAG, "%d ", zw111.fingerIDArray[i]);
            }
#endif
            // 处理指纹模块状态
            if (zw111.power == false) // 断电状态
            {
#ifdef DEBUG
                ESP_LOGI(TAG, "当前状态为断电状态，准备验证指纹");
#endif
                zw111.state = 0x04;    // 切换为验证指纹状态
                turn_on_fingerprint(); // 打开指纹模块供电
            }
        }
    }
}
// 串口任务
void uart_task(void *pvParameters)
{
    uart_event_t event;
    static uint8_t dtmp[1024];
    while (1)
    {
        if (xQueueReceive(uart2_queue, (void *)&event, (TickType_t)portMAX_DELAY))
        {
            bzero(dtmp, 1024);
            size_t buffered_size;
            switch (event.type)
            {
            case UART_DATA:
                if (zw111.state == 0X0B && event.size == 12) // 休眠状态
                {
                    // 先接受数据
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    // 再验证收到的数据是否有效
                    if (verify_received_data(dtmp, event.size) != ESP_OK)
                    {
#ifdef DEBUG
                        ESP_LOGE(TAG, "接收到无效数据，丢弃");
#endif
                        break; // 丢弃无效数据
                    }
                    if (dtmp[9] == 0x00) // 确认码=00H 表示休眠设置成功
                    {
                        gpio_set_level(FINGERPRINT_CTL_PIN, 1); // 给指纹模块断电
                        zw111.power = false;                    // 设置供电状态为false
                        zw111.state = 0X00;                     // 切换为初始状态
#ifdef DEBUG
                        ESP_LOGI(TAG, "指纹模块已断电，状态已重置为初始状态");
#endif
                        fingerprint_deinitialization_uart(); // 删除UART驱动
                        vTaskDelete(NULL);                   // 删除当前任务
                    }
                }
                else if (zw111.state == 0X0A && event.size == 12) // 取消状态
                {
                    // 先接受数据
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    // 再验证收到的数据是否有效
                    if (verify_received_data(dtmp, event.size) != ESP_OK)
                    {
#ifdef DEBUG
                        ESP_LOGE(TAG, "接收到无效数据，丢弃");
#endif
                        break; // 丢弃无效数据
                    }

                    if (dtmp[9] == 0x00) // 确认码=00H 表示取消操作成功
                    {
#ifdef DEBUG
                        ESP_LOGI(TAG, "取消操作成功，准备执行其他命令");
#endif

                        if (g_readyAddFingerprint == true)
                        {
                            zw111.state = 0x02;            // 设置状态为注册指纹状态
                            g_readyAddFingerprint = false; // 重置添加指纹标志
                            // 发送注册指纹命令
                            if (auto_enroll(get_mini_unused_id(), 5, false, false, false, false, true, false) != ESP_OK)
                            {
#ifdef DEBUG
                                ESP_LOGE(TAG, "注册指纹命令发送失败");
#endif
                                prepare_turn_off_fingerprint();
                            }
                        }
                        else if (g_cancelAddFingerprint == true)
                        {
                            g_cancelAddFingerprint = false; // 重置取消添加指纹标志
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                        else if (g_readyDeleteFingerprint == true && g_readyDeleteAllFingerprint == false)
                        {
                            zw111.state = 0x03; // 设置状态为删除指纹状态
                            // 发送删除指纹命令
                            if (delete_char(g_deleteFingerprintID, 1) != ESP_OK)
                            {
#ifdef DEBUG
                                ESP_LOGE(TAG, "删除指纹命令发送失败");
#endif
                                prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                            }
                        }
                        else if (g_readyDeleteAllFingerprint == true && g_readyDeleteFingerprint == false)
                        {
                            zw111.state = 0x03; // 设置状态为删除指纹状态
                            // 发送删除所有指纹命令
                            if (empty() != ESP_OK)
                            {
#ifdef DEBUG
                                ESP_LOGE(TAG, "删除所有指纹命令发送失败");
#endif
                                prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                            }
                        }
                        else
                        {
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                    }
                }
                else if (zw111.state == 0X04 && event.size == 17) // 验证指纹状态
                {
                    // 先接受数据
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    // 再验证收到的数据是否有效
                    if (verify_received_data(dtmp, event.size) != ESP_OK)
                    {
#ifdef DEBUG
                        ESP_LOGE(TAG, "接收到无效数据，丢弃");
#endif
                        break; // 丢弃无效数据
                    }
                    if (dtmp[10] == 0x00 && dtmp[9] == 0x00)
                    {
#ifdef DEBUG
                        ESP_LOGI(TAG, "验证指纹-命令执行成功，等待图像采集");
#endif
                    }
                    else if (dtmp[10] == 0x01)
                    {
                        if (dtmp[9] == 0x00)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "验证指纹-获取图像成功");
#endif
                        }
                        else if (dtmp[9] == 0x26)
                        {
#ifdef DEBUG
                            ESP_LOGW(TAG, "验证指纹-获取图像超时");
#endif
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                    }
                    else if (dtmp[10] == 0x05)
                    {
                        if (dtmp[9] == 0x00)
                        {
                            uint16_t fingerID = (dtmp[11] << 8) | dtmp[12]; // 指纹ID
                            uint16_t score = (dtmp[13] << 8) | dtmp[14];    // 得分

#ifdef DEBUG
                            ESP_LOGI(TAG, "验证指纹-搜到了指纹, 指纹ID: %d, 得分: %d", fingerID, score);
#endif
                            xQueueSend(xQueue_buzzer, &BUZZER_OPEN, pdMS_TO_TICKS(10));
                        }
                        else if (dtmp[9] == 0x09)
                        {

#ifdef DEBUG
                            ESP_LOGI(TAG, "验证指纹-没搜索到指纹");
#endif
                            xQueueSend(xQueue_buzzer, &BUZZER_NOOPEN, pdMS_TO_TICKS(10));
                        }
                        else if (dtmp[9] == 0x24)
                        {
#ifdef DEBUG
                            ESP_LOGW(TAG, "验证指纹-指纹库为空");
#endif
                            xQueueSend(xQueue_buzzer, &BUZZER_NOOPEN, pdMS_TO_TICKS(10));
                        }
                    }
                    else if (dtmp[10] == 0x02 && dtmp[9] == 0x09)
                    {
#ifdef DEBUG
                        ESP_LOGW(TAG, "验证指纹-传感器上没有手指");
#endif
                        xQueueSend(xQueue_buzzer, &BUZZER_NOOPEN, pdMS_TO_TICKS(10));
                    }
                    else
                    {
#ifdef DEBUG
                        ESP_LOGE(TAG, "验证指纹-未知数据，丢弃");
                        // 打印接收到的未知数据
                        ESP_LOG_BUFFER_HEX(TAG, dtmp, event.size);

#endif
                        prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                    }
                }
                else if (zw111.state == 0X01 && event.size == 44) // 读索引表状态
                {
                    // 先接受数据
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    // 再验证收到的数据是否有效
                    if (verify_received_data(dtmp, event.size) != ESP_OK)
                    {
#ifdef DEBUG
                        ESP_LOGE(TAG, "接收到无效数据，丢弃");
#endif
                        break; // 丢弃无效数据
                    }
#ifdef DEBUG
                    ESP_LOGI(TAG, "接收到索引表数据，长度: %d", event.size);
#endif
                    fingerprint_parse_frame(dtmp, event.size); // 解析指纹索引表数据
                    prepare_turn_off_fingerprint();            // 准备关闭指纹模块
                }
                else if (zw111.state == 0X02 && event.size == 14) // 注册指纹状态
                {
                    // 先接受数据
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    // 再验证收到的数据是否有效
                    if (verify_received_data(dtmp, event.size) != ESP_OK)
                    {
#ifdef DEBUG
                        ESP_LOGE(TAG, "接收到无效数据，丢弃");
#endif
                        break; // 丢弃无效数据
                    }
                    if (dtmp[10] == 0x00 && dtmp[11] == 0x00)
                    {
                        if (dtmp[9] == 0x00)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-命令执行成功，等待图像采集");
#endif
                        }
                        else if (dtmp[9] == 0x22)
                        {
#ifdef DEBUG
                            ESP_LOGE(TAG, "注册指纹-当前ID已被使用，请选择其他ID");
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                        else
                        {
#ifdef DEBUG
                            ESP_LOGE(TAG, "注册指纹-未知数据，丢弃");
                            // 打印接收到的未知数据
                            ESP_LOG_BUFFER_HEX(TAG, dtmp, event.size);
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                    }
                    else if (dtmp[10] == 0x01)
                    {
                        if (dtmp[9] == 0x00)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-第%d次采图成功", dtmp[11]);
#endif
                        }
                        else if (dtmp[9] == 0x26)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-第%d次采图超时", dtmp[11]);
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                        else
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-第%d次采图失败", dtmp[11]);
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                    }
                    else if (dtmp[10] == 0x02)
                    {

                        if (dtmp[9] == 0x00)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-第%d次生成特征成功", dtmp[11]);
#endif
                        }
                        else if (dtmp[9] == 0x26)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-第%d次生成特征超时", dtmp[11]);
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                        else
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-第%d次采图失败", dtmp[11]);
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                    }
                    else if (dtmp[10] == 0x03)
                    {

                        if (dtmp[9] == 0x00)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-手指第%d次离开，录入成功", dtmp[11]);
#endif
                        }
                        else if (dtmp[9] == 0x26)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-手指第%d次离开，录入超时", dtmp[11]);
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }

                        else
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-手指第%d次离开，录入失败", dtmp[11]);
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                    }
                    else if (dtmp[10] == 0x04 && dtmp[11] == 0xF0)
                    {

                        if (dtmp[9] == 0x00)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-合并模板成功", dtmp[11]);
#endif
                        }
                        else if (dtmp[9] == 0x26)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-合并模板超时", dtmp[11]);
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }

                        else
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-合并模板失败", dtmp[11]);
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                    }
                    else if (dtmp[10] == 0x05 && dtmp[11] == 0xF1)
                    {

                        if (dtmp[9] == 0x00)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-已注册检测通过");
#endif
                        }
                        else if (dtmp[9] == 0x26)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-已注册检测超时");
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                        else
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-已注册检测失败", dtmp[11]);
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                    }
                    else if (dtmp[10] == 0x06 && dtmp[11] == 0xF2)
                    {

                        if (dtmp[9] == 0x00)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-模板存储成功，id:%d", get_mini_unused_id());
#endif
                            insert_fingerprint_id(get_mini_unused_id());
                            send_fingerprint_list();
                            send_operation_result("fingerprint_added", true);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                        else if (dtmp[9] == 0x26)
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-模板存储超时");
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                        else
                        {
#ifdef DEBUG
                            ESP_LOGI(TAG, "注册指纹-模板存储失败");
#endif
                            send_operation_result("fingerprint_added", false);
                            prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                        }
                    }
                }
                else if (zw111.state == 0X03 && event.size == 12) // 删除指纹状态
                {
                    // 先接受数据
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    // 再验证收到的数据是否有效
                    if (verify_received_data(dtmp, event.size) != ESP_OK)
                    {
#ifdef DEBUG
                        ESP_LOGE(TAG, "接收到无效数据，丢弃");
#endif
                        break; // 丢弃无效数据
                    }
                    if (g_readyDeleteFingerprint == false && g_readyDeleteAllFingerprint == true)
                    {
                        for (size_t i = 0; i <= zw111.fingerNumber; i++)
                        {
                            zw111.fingerIDArray[i] = 0xFF; // 清空指纹ID
                        }

                        zw111.fingerNumber = 0; // 清空指纹数量

                        g_readyDeleteAllFingerprint = false; // 重置删除所有指纹标志
                        send_operation_result("fingerprint_cleared", true);
#ifdef DEBUG
                        ESP_LOGI(TAG, "删除指纹-清空所有指纹成功");
#endif
                    }
                    else if (g_readyDeleteFingerprint == true && g_readyDeleteAllFingerprint == false)
                    {
                        // 查找目标ID的位置
                        size_t i;
                        for (i = 0; i < zw111.fingerNumber; i++)
                        {
                            if (zw111.fingerIDArray[i] == g_deleteFingerprintID)
                            {

                                break; // 找到目标，跳出循环准备删除
                            }
                        }

                        // 前移元素填补空缺
                        for (size_t j = i; j < zw111.fingerNumber - 1; j++)
                        {
                            zw111.fingerIDArray[j] = zw111.fingerIDArray[j + 1];
                        }

                        zw111.fingerIDArray[zw111.fingerNumber - 1] = 0xFF; // 将最后一个位置重置为0xFF

                        zw111.fingerNumber--; // 减少指纹数量

                        g_readyDeleteFingerprint = false; // 重置删除单个指纹标志

                        send_fingerprint_list();

                        send_operation_result("fingerprint_deleted", true);

#ifdef DEBUG
                        ESP_LOGI(TAG, "删除指纹-删除ID:%d成功", g_deleteFingerprintID);
#endif
                    }

                    prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                }
                break;
            case UART_PATTERN_DET:
                uart_get_buffered_data_len(EX_UART_NUM, &buffered_size);
                int pos = uart_pattern_pop_pos(EX_UART_NUM);
#ifdef DEBUG
                ESP_LOGI(TAG, "[UART PATTERN DETECTED] pos: %d, buffered size: %d", pos, buffered_size);
#endif
                if (pos == -1)
                {
                    // There used to be a UART_PATTERN_DET event, but the pattern position queue is full so that it can not
                    // record the position. We should set a larger queue size.
                    // As an example, we directly flush the rx buffer here.
                    uart_flush_input(EX_UART_NUM);
                }
                else
                {
                    uart_read_bytes(EX_UART_NUM, dtmp, pos, pdMS_TO_TICKS(100));
                    uint8_t pat[2];
                    memset(pat, 0, sizeof(pat));
                    uart_read_bytes(EX_UART_NUM, pat, 1, pdMS_TO_TICKS(100));
                    if (pat[0] == 0X55)
                    {
#ifdef DEBUG
                        ESP_LOGI(TAG, "指纹模块刚上电，状态: %s",
                                 zw111.state == 0x00   ? "初始状态"
                                 : zw111.state == 0x01 ? "读索引表状态"
                                 : zw111.state == 0x02 ? "注册指纹状态"
                                 : zw111.state == 0x03 ? "删除指纹状态"
                                 : zw111.state == 0x04 ? "验证指纹状态"
                                 : zw111.state == 0x0A ? "取消状态"
                                 : zw111.state == 0x0B ? "休眠状态"
                                                       : "未知状态");
#endif

                        if (zw111.state == 0X04) // 验证指纹状态
                        {
                            // 发送验证指纹命令
                            if (auto_identify(0xFFFF, 2, false, false, false) != ESP_OK)
                            {
#ifdef DEBUG
                                ESP_LOGE(TAG, "验证指纹命令发送失败");
#endif
                                prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                            }
                        }
                        else if (zw111.state == 0X00) // 刚开机的状态
                        {
                            zw111.state = 0X01; // 切换为读索引表状态
                            read_index_table(0);
                        }
                        else if (zw111.state == 0X02) // 注册指纹状态
                        {

#ifdef DEBUG
                            ESP_LOGI(TAG, "指纹模块处于注册状态，准备注册指纹");
#endif
                            // 发送注册指纹命令
                            if (auto_enroll(get_mini_unused_id(), 5, false, false, false, false, true, false) != ESP_OK)
                            {
#ifdef DEBUG
                                ESP_LOGE(TAG, "注册指纹命令发送失败");
#endif
                                prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                            }
                        }
                        else if (zw111.state == 0X03) // 删除指纹状态
                        {
                            if (g_readyDeleteFingerprint == true && g_readyDeleteAllFingerprint == false)
                            {
                                // 删除一个指纹
                                if (delete_char(g_deleteFingerprintID, 1) != ESP_OK)
                                {
#ifdef DEBUG
                                    ESP_LOGE(TAG, "删除指纹命令发送失败");
#endif
                                    prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                                }
                            }
                            else if (g_readyDeleteFingerprint == false && g_readyDeleteAllFingerprint == true)
                            {
                                // 删除所有指纹
                                if (empty() != ESP_OK)
                                {
#ifdef DEBUG
                                    ESP_LOGE(TAG, "删除所有指纹命令发送失败");
#endif
                                    prepare_turn_off_fingerprint(); // 准备关闭指纹模块
                                }
                            }
                        }
                    }
                }
                break;

            default:
                break;
            }
        }
    }
}