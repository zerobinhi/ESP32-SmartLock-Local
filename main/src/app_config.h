#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>
#include <esp_log.h>

#define DEBUG

#define INDEX_HTML_BUFFER_SIZE 32768
#define RESPONSE_DATA_BUFFER_SIZE 32768

#define FINGERPRINT_TX_PIN 26
#define FINGERPRINT_RX_PIN 25
#define FINGERPRINT_CTL_PIN 33
#define FINGERPRINT_INT_PIN 27

#endif // APP_CONFIG_H