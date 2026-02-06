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

#define FINGERPRINT_TX_PIN 16
#define FINGERPRINT_RX_PIN 15
#define FINGERPRINT_CTL_PIN 17
#define FINGERPRINT_INT_PIN 18

#define I2C_MASTER_SCL_IO 21      /*!< GPIO for I2C master clock */
#define I2C_MASTER_SDA_IO 47      /*!< GPIO for I2C master data */
#define PN7160_RST_PIN 48         /*!< GPIO for PN7160 reset */
#define PN7160_INT_PIN 36         /*!< GPIO for PN7160 interrupt */
#define I2C_MASTER_NUM I2C_NUM_0  /*!< I2C port number */
#define I2C_MASTER_FREQ_HZ 100000 /*!< I2C master clock frequency */
#define PN7160_I2C_ADDRESS ((uint8_t)0x28)
#define OLED_I2C_ADDRESS ((uint8_t)0x3C)

#define LOCK_CTL_PIN 35
#define BUZZER_CTL_PIN 20

#define APP_LED_PIN 19
#define CARD_LED_PIN 37
#define FINGERPRINT_LED_PIN 40
#define PASSWORD_LED_PIN 39

#define BATTERY_PIN 1

#define MAX_CARDS 20

#define TOUCH_PASSWORD_LEN 6
#define DEFAULT_PASSWORD "123456"

#define true 1
#define false 0

#endif
