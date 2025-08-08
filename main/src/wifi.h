#ifndef WIFI_H
#define WIFI_H

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "app_config.h"
#include "esp_mac.h"

#define AP_SSID "ESP32-SmartLock"
#define AP_PASS "12345678"
#define AP_CHANNEL 6
#define MAX_STA_CONN 3

void wifi_init_softap(void);

#endif // WIFI_H