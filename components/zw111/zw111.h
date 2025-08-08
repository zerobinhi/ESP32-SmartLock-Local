#ifndef ZW111_H_
#define ZW111_H_

#include "driver/uart.h"
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

#endif // ZW111_H_