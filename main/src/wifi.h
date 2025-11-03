#ifndef WIFI_H
#define WIFI_H

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include "app_config.h"
#include <esp_mac.h>
#include "nvs_custom.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#define AP_CHANNEL 6
#define MAX_STA_CONN 5
#define DNS_PORT 53

void wifi_init_softap(void);
void dns_server_task(void *pv);

#endif
