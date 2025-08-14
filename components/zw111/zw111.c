#include "zw111.h"

// ========================== 全局变量定义 ==========================
/** 帧头固定值（2字节），与模块通信的起始标识 */
const uint8_t FRAME_HEADER[2] = {0xEF, 0x01};

struct fingerprint_device zw111 = {0};

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
        printf("校验和计算失败：无效参数（帧长%d，最小需%d）\n",
               frameLen, CHECKSUM_START_INDEX + CHECKSUM_LEN + 1);
        return 0;
    }

    uint16_t checksum = 0;
    uint8_t checksumEndIndex = frameLen - CHECKSUM_LEN - 1; // 校验和前一字节索引
    // 累加校验范围：从CHECKSUM_START_INDEX到checksumEndIndex
    for (uint8_t i = CHECKSUM_START_INDEX; i <= checksumEndIndex; i++)
    {
        checksum += frameData[i];
    }

    return checksum;
}

/**
 * @brief 校验指纹模块接收数据的有效性（重点验证帧结构和校验和）
 * @param recvData 接收的数据包缓冲区（需非空）
 * @param dataLen 实际接收的字节数（必须显式传入，不可用strlen计算）
 * @return esp_err_t 校验结果：ESP_OK=有效数据，ESP_FAIL=无效数据
 */
static esp_err_t verify_received_data(const uint8_t *recvData, uint16_t dataLen)
{
    // 基础合法性检查
    if (recvData == NULL || dataLen < MIN_RESPONSE_LEN)
    {
#if DEBUG
        ESP_LOGE(TAG, "校验失败：数据为空或长度不足（最小需%d字节，当前%d字节）", MIN_RESPONSE_LEN, dataLen);
#endif
        return ESP_FAIL;
    }

    // 验证帧头（前2字节）
    if (recvData[0] != FRAME_HEADER[0] || recvData[1] != FRAME_HEADER[1])
    {
#if DEBUG
        ESP_LOGE(TAG, "校验失败：帧头不匹配（期望%02X%02X，实际%02X%02X）", FRAME_HEADER[0], FRAME_HEADER[1], recvData[0], recvData[1]);
#endif
        return ESP_FAIL;
    }

    // 验证设备地址（2-5字节）
    for (int i = 2; i < 6; i++)
    {
        if (recvData[i] != zw111.deviceAddress[i - 2])
        {
#if DEBUG
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
#if DEBUG
        ESP_LOGE(TAG, "校验失败：包标识错误（期望应答包%02X，实际%02X）", PACKET_RESPONSE, recvData[6]);
#endif
        return ESP_FAIL;
    }

    // 验证数据长度（7-8字节，高字节在前）
    uint16_t expectedDataLen = (recvData[7] << 8) | recvData[8]; // 数据区长度
    if (9 + expectedDataLen != dataLen)
    {
#if DEBUG
        ESP_LOGE(TAG, "校验失败：长度不匹配（期望总长度%u，实际%u）", 9 + expectedDataLen, dataLen);
#endif
        return ESP_FAIL;
    }

    // 验证校验和（最后2字节，高字节在前）
    uint16_t receivedChecksum = (recvData[dataLen - 2] << 8) | recvData[dataLen - 1];

    if (calculate_checksum(recvData, dataLen) != receivedChecksum)
    {

#if DEBUG
        ESP_LOGE(TAG, "校验失败：校验和不匹配（期望0x%04X，实际0x%04X）", calculate_checksum(recvData, dataLen), receivedChecksum);
#endif
        return ESP_FAIL;
    }

#if DEBUG
    ESP_LOGI(TAG, "校验成功：数据有效");
#endif
    return ESP_OK;
}

// ========================== 功能函数 ==========================
/**
 * @brief 指纹模块自动注册函数
 * @param ID 指纹ID号（0-99，超出范围返回失败）
 * @param enrollTimes 录入次数（0-5，0和1效果相同，超出返回失败）
 * @param ledControl 采图背光灯控制：false=常亮；true=采图成功后熄灭
 * @param preprocess 采图预处理控制：false=不预处理；true=开启预处理
 * @param returnStatus 注册状态返回控制：false=返回状态；true=不返回状态
 * @param allowOverwrite ID覆盖控制：false=不允许覆盖；true=允许覆盖
 * @param allowDuplicate 重复注册控制：false=允许重复；true=禁止重复
 * @param requireRemove 手指离开要求：false=需离开；true=无需离开
 * @return esp_err_t 操作结果：ESP_OK=帧组装成功，ESP_FAIL=参数无效或组装失败
 */
static esp_err_t auto_enroll(uint16_t ID, uint8_t enrollTimes,
                             bool ledControl, bool preprocess,
                             bool returnStatus, bool allowOverwrite,
                             bool allowDuplicate, bool requireRemove)
{
    // 参数合法性检查
    if (ID >= 100)
    {
        printf("注册失败：ID超出范围（需0-99，当前%d）\n", ID);
        return ESP_FAIL;
    }
    if (enrollTimes > 5)
    {
        printf("注册失败：录入次数超出范围（需0-5，当前%d）\n", enrollTimes);
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
    printf("发送自动注册帧: ");
    for (uint8_t i = 0; i < sizeof(frame); i++)
    {
        printf("%02X ", frame[i]);
    }
    printf("\n");

    // 实际应用中需添加UART发送逻辑（示例）
    // return uart_write_bytes(EX_UART_NUM, (const char*)frame, sizeof(frame));
    return ESP_OK;
}

/**
 * @brief 指纹模块自动识别函数
 * @param ID 指纹ID号：具体数值(0-99)=验证指定ID；0xFFFF=验证所有已注册指纹
 * @param scoreLevel 比对分数等级（1-5，等级越高严格度越高，默认建议2）
 * @param ledControl 采图背光灯控制：false=常亮；true=采图成功后熄灭
 * @param preprocess 采图预处理控制：false=不预处理；true=开启预处理
 * @param returnStatus 识别状态返回控制：false=返回状态；true=不返回状态
 * @return esp_err_t 操作结果：ESP_OK=帧组装成功，ESP_FAIL=参数无效或组装失败
 */
static esp_err_t auto_identify(uint16_t ID, uint8_t scoreLevel, bool ledControl, bool preprocess, bool returnStatus)
{

    if (scoreLevel < 1 || scoreLevel > 5)
    {
#if DEBUG
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

#if DEBUG
    ESP_LOGI(TAG, "发送自动识别指令: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
#endif
    if (uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame)) == -1)
    {
#if DEBUG
        ESP_LOGE(TAG, "指令发送失败");
#endif
        return ESP_FAIL;
    }
    else
    {
#if DEBUG
        ESP_LOGI(TAG, "指令发送成功");
#endif
        return ESP_OK;
    }
}

/**
 * @brief 指纹模块LED控制函数（支持呼吸、闪烁、开关等模式）
 * @param functionCode 功能码（1-6，参考BLN_xxx宏定义，如BLN_BREATH=呼吸灯）
 * @param startColor 起始颜色（bit0-蓝,bit1-绿,bit2-红，参考LED_xxx宏定义）
 * @param endColor 结束颜色（仅功能码1-呼吸灯有效，其他模式忽略）
 * @param cycleTimes 循环次数（仅功能码1-呼吸灯/2-闪烁灯有效，0=无限循环）
 * @return esp_err_t 操作结果：ESP_OK=帧组装成功，ESP_FAIL=参数无效或组装失败
 */
static esp_err_t control_led(uint8_t functionCode, uint8_t startColor,
                             uint8_t endColor, uint8_t cycleTimes)
{
    // 参数合法性检查
    if (functionCode < BLN_BREATH || functionCode > BLN_FADE_OUT)
    {
        printf("LED控制失败：功能码无效（需1-6，当前%d）\n", functionCode);
        return ESP_FAIL;
    }

    // 过滤颜色参数无效位（仅保留低3位）
    if ((startColor & 0xF8) != 0)
    {
        printf("LED控制警告：起始颜色仅低3位有效，已过滤为0x%02X\n", startColor & 0x07);
        startColor &= 0x07;
    }
    if ((endColor & 0xF8) != 0)
    {
        printf("LED控制警告：结束颜色仅低3位有效，已过滤为0x%02X\n", endColor & 0x07);
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
    printf("发送LED控制帧: ");
    for (uint8_t i = 0; i < sizeof(frame); i++)
    {
        printf("%02X ", frame[i]);
    }
    printf("\n");

    // 实际应用中需添加UART发送逻辑
    // return uart_write_bytes(EX_UART_NUM, (const char*)frame, sizeof(frame));
    return ESP_OK;
}

/**
 * @brief 指纹模块LED跑马灯控制函数（七彩循环模式）
 * @param startColor 起始颜色配置（参考LED_xxx宏定义，仅低3位有效）
 * @param timeBit 呼吸周期时间参数（1-100，对应0.1秒-10秒）
 * @param cycleTimes 循环次数（0=无限循环）
 * @return esp_err_t 操作结果：ESP_OK=帧组装成功，ESP_FAIL=参数无效或组装失败
 */
static esp_err_t control_colorful_led(uint8_t startColor, uint8_t timeBit, uint8_t cycleTimes)
{
    // 参数合法性检查
    if (timeBit < 1 || timeBit > 100)
    {
        printf("跑马灯控制失败：时间参数无效（需1-100，当前%d）\n", timeBit);
        return ESP_FAIL;
    }

    // 过滤颜色参数无效位
    if ((startColor & 0xF8) != 0)
    {
        printf("跑马灯控制警告：起始颜色仅低3位有效，已过滤为0x%02X\n", startColor & 0x07);
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
    printf("发送跑马灯控制帧: ");
    for (uint8_t i = 0; i < sizeof(frame); i++)
    {
        printf("%02X ", frame[i]);
    }
    printf("\n");

    // 实际应用中需添加UART发送逻辑
    // return uart_write_bytes(EX_UART_NUM, (const char*)frame, sizeof(frame));
    return ESP_OK;
}

/**
 * @brief 删除指定数量的指纹（从指定ID开始连续删除）
 * @param ID 起始指纹ID（0-99，超出范围返回失败）
 * @param count 删除数量（1-100，需确保不超出ID范围）
 * @return esp_err_t 操作结果：ESP_OK=帧组装成功，ESP_FAIL=参数无效或组装失败
 */
esp_err_t delete_char(uint16_t ID, uint16_t count)
{
    // 参数合法性检查
    if (ID >= 100)
    {
        printf("删除失败：起始ID超出范围（需0-99，当前%d）\n", ID);
        return ESP_FAIL;
    }
    if (count == 0 || count > 100 || (ID + count) > 100)
    {
        printf("删除失败：数量无效（需1-100且不超出ID范围，当前数量%d）\n", count);
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

#if DEBUG
    ESP_LOGI(TAG, "发送删除指纹帧: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
#endif
    if (uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame)) == -1)
    {
#if DEBUG
        ESP_LOGE(TAG, "删除指纹指令发送失败");
#endif
        return ESP_FAIL;
    }
    else
    {
#if DEBUG
        ESP_LOGI(TAG, "删除指纹指令发送成功");
#endif
        return ESP_OK;
    }
}

/**
 * @brief 清空模块中所有已注册的指纹
 * @return esp_err_t 操作结果：ESP_OK=帧组装成功，ESP_FAIL=组装失败
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
    printf("发送清空所有指纹帧: ");
    for (uint8_t i = 0; i < sizeof(frame); i++)
    {
        printf("%02X ", frame[i]);
    }
    printf("\n");

    // 实际应用中需添加UART发送逻辑
    // return uart_write_bytes(EX_UART_NUM, (const char*)frame, sizeof(frame));
    return ESP_OK;
}

/**
 * @brief 取消模块当前正在执行的操作（如注册、识别等）
 * @return esp_err_t 操作结果：ESP_OK=帧组装成功，ESP_FAIL=组装失败
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
    printf("发送取消操作帧: ");
    for (uint8_t i = 0; i < sizeof(frame); i++)
    {
        printf("%02X ", frame[i]);
    }
    printf("\n");

    // 实际应用中需添加UART发送逻辑
    // return uart_write_bytes(EX_UART_NUM, (const char*)frame, sizeof(frame));
    return ESP_OK;
}

/**
 * @brief 控制模块进入休眠模式（降低功耗，需外部唤醒）
 * @return esp_err_t 操作结果：ESP_OK=帧组装成功，ESP_FAIL=组装失败
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
    printf("发送休眠指令帧: ");
    for (uint8_t i = 0; i < sizeof(frame); i++)
    {
        printf("%02X ", frame[i]);
    }
    printf("\n");

    // 实际应用中需添加UART发送逻辑
    // return uart_write_bytes(EX_UART_NUM, (const char*)frame, sizeof(frame));
    return ESP_OK;
}

/**
 * @brief 读取模块中的指纹索引表（获取已注册指纹ID）
 * @param page 页码（0-4，每页对应20枚指纹，共100枚）
 * @return esp_err_t 操作结果：ESP_OK=帧组装成功，ESP_FAIL=参数无效或组装失败
 */
static esp_err_t read_index_table(uint8_t page)
{
    if (page > 4)
    {
#if DEBUG
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

#if DEBUG
    ESP_LOGI(TAG, "读取索引表: ");
    ESP_LOG_BUFFER_HEX(TAG, frame, sizeof(frame));
#endif
    if (uart_write_bytes(EX_UART_NUM, (const char *)frame, sizeof(frame)) == -1)
    {
#if DEBUG
        ESP_LOGE(TAG, "指令发送失败");
#endif
        return ESP_FAIL;
    }
    else
    {
#if DEBUG
        ESP_LOGI(TAG, "指令发送成功");
#endif
        return ESP_OK;
    }
}

/**
 * @brief 解析读索引表命令的返回数据，提取已注册指纹ID
 * @param recvData 接收的数据包缓冲区（需通过verify_received_data校验）
 * @param dataLen 实际接收的字节数（需显式传入）
 * @return esp_err_t 解析结果：ESP_OK=解析成功，ESP_FAIL=数据无效或解析失败
 */
static esp_err_t fingerprint_parse_frame(const uint8_t *recvData, uint16_t dataLen)
{
    // 先校验数据有效性
    if (verify_received_data(recvData, dataLen) != ESP_OK)
    {
        return ESP_FAIL;
    }

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

#if DEBUG

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

void turn_on_fingerprint()
{
    gpio_set_level(FINGERPRINT_CTL_PIN, 0); // 给ZW101供电
    zw111.power = true;
#if DEBUG
    ESP_LOGI(TAG, "指纹模块已供电");
#endif
}

void turn_off_fingerprint()
{
    gpio_set_level(FINGERPRINT_CTL_PIN, 1);
    zw111.power = false; // 关闭指纹模块供电
#if DEBUG
    ESP_LOGI(TAG, "指纹模块已断电");
#endif
}
/**
 * @brief 初始化指纹模块UART通信
 * @return esp_err_t ESP_OK=初始化成功，ESP_FAIL=数据无效或初始化失败
 */
esp_err_t fingerprint_initialization()
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // Install UART driver, and get the queue.
    uart_driver_install(EX_UART_NUM, 1024 * 2, 1024 * 2, 20, &uart2_queue, 0);
    uart_param_config(EX_UART_NUM, &uart_config);

    // Set UART pins (using UART0 default pins ie no changes.)
    uart_set_pin(EX_UART_NUM, FINGERPRINT_RX_PIN, FINGERPRINT_TX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Set uart pattern detect function.
    uart_enable_pattern_det_baud_intr(EX_UART_NUM, 0x55, 1, 1, 100, 100);

    // Reset the pattern queue length to record at most 20 pattern positions.
    uart_pattern_queue_reset(EX_UART_NUM, 20);

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

    // Create a task to handler UART event from ISR
    xTaskCreate(uart_task, "uart_task", 8192, NULL, 10, NULL);
    xTaskCreate(fingerprint_task, "fingerprint_task", 8192, NULL, 10, NULL);

#if DEBUG
    ESP_LOGI(TAG, "fingerprint device created");
#endif
    return ESP_OK;
}

// 指纹任务
void fingerprint_task(void *pvParameters)
{
    turn_on_fingerprint(); // 打开指纹模块供电

    while (1)
    {
        // gpio_set_level(FINGERPRINT_CTL_PIN, 0); // 给ZW101供电
        // vTaskDelay(pdMS_TO_TICKS(1000));        // 等待1秒
        // read_index_table(0);                    // 读取第0页索引表
        // vTaskDelay(pdMS_TO_TICKS(2000));
        // gpio_set_level(FINGERPRINT_CTL_PIN, 1); // 给ZW101断电
        // zw111.state = 0X00;                    // 切换为初始状态
        vTaskDelay(pdMS_TO_TICKS(10000000)); // 等待1秒
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

                if (zw111.state == 0X01 && event.size > 0) // 读索引表状态
                {
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
#if DEBUG
                    ESP_LOG_BUFFER_HEX(TAG, dtmp, event.size); // 打印接收到的数据46个字节
#endif
                    ESP_LOGI(TAG, "接收到索引表数据，长度: %d", event.size); // 这里显示44个字节
                    fingerprint_parse_frame(dtmp, event.size);               // 解析指纹索引表数据
                }
                break;
            case UART_PATTERN_DET:
                uart_get_buffered_data_len(EX_UART_NUM, &buffered_size);
                int pos = uart_pattern_pop_pos(EX_UART_NUM);
                ESP_LOGI(TAG, "[UART PATTERN DETECTED] pos: %d, buffered size: %d", pos, buffered_size);
                if (pos == -1)
                {
                    // There used to be a UART_PATTERN_DET event, but the pattern position queue is full so that it can not
                    // record the position. We should set a larger queue size.
                    // As an example, we directly flush the rx buffer here.
                    uart_flush_input(EX_UART_NUM);
                }
                else
                {
                    uart_read_bytes(EX_UART_NUM, dtmp, pos, 100 / portTICK_PERIOD_MS);
                    uint8_t pat[2];
                    memset(pat, 0, sizeof(pat));
                    uart_read_bytes(EX_UART_NUM, pat, 1, 100 / portTICK_PERIOD_MS);
                    if (pat[0] == 0X55)
                    {
#if DEBUG
                        ESP_LOGI(TAG, "pat[0] == 0X55 ZW101就绪,state: %d", zw111.state);
#endif
                        if (zw111.state == 0X00) // 刚开机的状态
                        {
                            zw111.state = 0X01; // 切换为读索引表状态
                            read_index_table(0);
                        }
                    }
                }

            default:
                break;
            }
        }
    }
}
