#ifndef __PN7160_I2C_H__
#define __PN7160_I2C_H__

#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "nvs_custom.h"
#include "app_config.h"

#define DL_CMD 0x00		   // Download command
#define DL_RESET 0xF0	   // Reset command
#define DL_GETVERSION 0xF1 // Get version command

#define MAX_FRAME_SIZE 1000				// Maximum frame size for PN7160
#define CHUNK_SIZE (MAX_FRAME_SIZE - 4) // Chunk size for data transfer

extern i2c_master_dev_handle_t pn7160_handle;
extern bool g_ready_add_card;
extern bool g_ready_delete_card;
extern char g_delete_card_number;
extern QueueHandle_t card_queue; 
extern void send_card_list();                                         // send updated card list to front end
extern void send_operation_result(const char *message, bool success); // send operation result to front end

esp_err_t pn7160_initialization();
uint8_t find_card_id(uint64_t card_id);
void pn7160_task(void *arg);

#endif
