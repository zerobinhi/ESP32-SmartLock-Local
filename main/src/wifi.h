#ifndef WIFI_H
#define WIFI_H

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include "app_config.h"
#include <esp_mac.h>
#include "nvs_custom.h"

#define AP_CHANNEL 6
#define MAX_STA_CONN 5

void wifi_init_softap(void);

#endif
