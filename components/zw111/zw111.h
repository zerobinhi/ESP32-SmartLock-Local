#ifndef ZW111_H_
#define ZW111_H_

#include "driver/uart.h"
#include "driver/gpio.h"

#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"

// ========================== 硬件配置宏 ==========================
#define EX_UART_NUM UART_NUM_2        // 指纹模块使用的UART端口
#define ZW111_DEFAULT_ADDR 0xFFFFFFFF // 默认设备地址（4字节）
#define ZW111_DEFAULT_HEADER 0xEF01   // 默认帧头（2字节）

// ========================== 帧结构常量 ==========================
#define CHECKSUM_LEN 2         // 校验和长度（字节，固定为2）
#define CHECKSUM_START_INDEX 6 // 校验和计算起始索引（包标识位置）
#define MIN_RESPONSE_LEN 12    // 最小应答帧长度（字节）

// ========================== 包标识定义 ==========================
#define PACKET_CMD 0x01       // 命令包（主机发送指令）
#define PACKET_DATA_MORE 0x02 // 数据包（有后续包）
#define PACKET_DATA_LAST 0x08 // 最后一个数据包（无后续）
#define PACKET_RESPONSE 0x07  // 应答包（模块返回结果）

// ========================== 指令码定义 ==========================
#define CMD_AUTO_ENROLL 0x31      // 自动注册指纹指令
#define CMD_AUTO_IDENTIFY 0x32    // 自动识别指纹指令
#define CMD_CONTROL_BLN 0x3C      // 背光灯（LED）控制指令
#define CMD_DELET_CHAR 0x0C       // 删除指定指纹指令
#define CMD_EMPTY 0x0D            // 清空所有指纹指令
#define CMD_CANCEL 0x30           // 取消当前操作指令
#define CMD_READ_INDEX_TABLE 0x1F // 读取指纹索引表指令
#define CMD_SLEEP 0x33            // 模块休眠指令

// ========================== LED控制宏定义 ==========================
// LED功能码
#define BLN_BREATH 1   // 普通呼吸灯模式
#define BLN_FLASH 2    // 闪烁灯模式
#define BLN_ON 3       // 常亮模式
#define BLN_OFF 4      // 常闭模式
#define BLN_FADE_IN 5  // 渐亮模式
#define BLN_FADE_OUT 6 // 渐暗模式
#define BLN_COLORFUL 7 // 七彩循环模式

// LED颜色定义（bit0-蓝, bit1-绿, bit2-红）
#define LED_OFF 0x00   // 全灭
#define LED_BLUE 0x01  // 蓝灯
#define LED_GREEN 0x02 // 绿灯
#define LED_RED 0x04   // 红灯
#define LED_BG 0x03    // 蓝+绿灯
#define LED_BR 0x05    // 蓝+红灯
#define LED_GR 0x06    // 绿+红灯
#define LED_ALL 0x07   // 红+绿+蓝全亮

// ========================== 数据结构定义 ==========================
struct fingerprint_device
{
    /**
     * 0X00 刚开机的状态
     * 0X01 读索引表状态
     * 0X02 注册指纹状态
     * 0X03 删除指纹状态
     * 0X04 验证指纹状态
     * 0X0A 取消命令状态
     * 0X0B 准备关机状态
     */
    uint8_t state;

    /**
     * false 断电状态
     * true 上电状态
     */
    bool power;

    // 设备地址（4字节），默认地址0xFFFFFFFF，可修改
    uint8_t deviceAddress[4];

    // 已注册指纹ID数组，最大支持100枚（0-99），未使用位置为0xFF
    uint8_t fingerIDArray[100];

    // 当前有效指纹数量
    uint8_t fingerNumber;
};

extern bool g_readyAddFingerprint;
extern bool g_readyDeleteFingerprint;
extern bool g_readyDeleteAllFingerprint;
extern uint8_t g_deleteFingerprintID;
extern void send_fingerprint_list();

void fingerprint_task(void *pvParameters);
void uart_task(void *pvParameters);
void buzzer_task(void *pvParameters);
esp_err_t fingerprint_initialization();
esp_err_t delete_char(uint16_t ID, uint16_t count);
void turn_on_fingerprint();
void prepare_turn_off_fingerprint();
void cancel_current_operation_and_execute_command();


#endif // ZW111_H_