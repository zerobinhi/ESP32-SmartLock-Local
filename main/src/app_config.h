#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>
#include <esp_log.h>

#define DEFAULT_AP_SSID "ESP32-SmartLock"
#define DEFAULT_AP_PASS "12345678"

#define INDEX_HTML_BUFFER_SIZE 32768
#define RESPONSE_DATA_BUFFER_SIZE 32768

#define FINGERPRINT_TX_PIN 26
#define FINGERPRINT_RX_PIN 25
#define FINGERPRINT_CTL_PIN 33
#define FINGERPRINT_INT_PIN 27

#define I2C_MASTER_SCL_IO 16      /*!< GPIO for I2C master clock */
#define I2C_MASTER_SDA_IO 17      /*!< GPIO for I2C master data */
#define PN532_RST_PIN 18          /*!< GPIO for PN532 reset */
#define PN532_INT_PIN 4           /*!< GPIO for PN532 interrupt */
#define I2C_MASTER_NUM I2C_NUM_0  /*!< I2C port number */
#define I2C_MASTER_FREQ_HZ 400000 /*!< I2C master clock frequency */
#define PN532_I2C_ADDRESS ((uint8_t)0x24)
#define OLED_I2C_ADDRESS ((uint8_t)0x3C)

#define FT6336U_RST_PIN 19 /*!< GPIO for FT6336U reset */
#define FT6336U_INT_PIN 21 /*!< GPIO for FT6336U interrupt */

#define LOCK_CTL_PIN 2
#define BUZZER_CTL_PIN 14

#define FINGERPRINT_LED_PIN 13
#define PASSWORD_LED_PIN 32
#define CARD_LED_PIN 22
#define APP_LED_PIN 23

#define BATTERY_PIN 35

#define MAX_CARDS 20

#define true 1
#define false 0

#endif
