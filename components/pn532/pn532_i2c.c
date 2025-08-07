#include "pn532_i2c.h"

/**
 * PN532发生命令和接收响应的函数
 * @param cmd 命令数据
 * @param cmd_len 命令长度
 * @param response 响应数据
 * @param response_len 响应长度
 * @note 该函数会先发送命令，然后延迟20ms，再接收响应数据
 */
void pn532_send_command_and_receive(const uint8_t *cmd, size_t cmd_len, uint8_t *response, size_t response_len)
{
    i2c_master_transmit(pn532_handle, cmd, cmd_len, 100);
    vTaskDelay(pdMS_TO_TICKS(20)); // 延迟等待模块处理
    i2c_master_receive(pn532_handle, response, response_len, 100);
}
