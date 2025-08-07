#ifndef __PN532_I2C_H__
#define __PN532_I2C_H__

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"

extern i2c_master_dev_handle_t pn532_handle;

void pn532_send_command_and_receive(const uint8_t *cmd, size_t cmd_len, uint8_t *response, size_t response_len);

#endif
