#ifndef __PN532_I2C_H__
#define __PN532_I2C_H__

#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_log.h>
#include "app_config.h"

extern i2c_master_dev_handle_t pn532_handle;

esp_err_t pn532_initialization();
void pn532_send_command_and_receive(const uint8_t *cmd, size_t cmd_len, uint8_t *response, size_t response_len);
uint8_t find_card_id(const char *card_id, int number);
void pn532_task(void *arg);

#endif
