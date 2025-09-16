#ifndef __PN532_I2C_H__
#define __PN532_I2C_H__

#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_log.h>
#include "nvs_custom.h"
#include "app_config.h"

extern i2c_master_dev_handle_t pn532_handle; // I2C从设备句柄
extern bool g_ready_add_card;
extern bool g_ready_delete_card;
extern char g_add_card_number[9];
extern char g_delete_card_number[9];

extern void send_card_list();                                         // 发送当前卡片列表到前端
extern void send_operation_result(const char *message, bool success); // 发送操作结果到前端

esp_err_t pn532_initialization();
esp_err_t pn532_send_command_and_receive(const uint8_t *cmd, size_t cmd_len, uint8_t *response, size_t response_len);
uint8_t find_card_id(uint64_t card_id);
void pn532_task(void *arg);

#endif
