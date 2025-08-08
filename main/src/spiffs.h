#ifndef SPIFFS_H
#define SPIFFS_H

#include <sys/stat.h>
#include "esp_spiffs.h"
#include "app_config.h"

#define INDEX_HTML_PATH "/spiffs/index.html"
extern char index_html[INDEX_HTML_BUFFER_SIZE];
extern char response_data[RESPONSE_DATA_BUFFER_SIZE];

void spiffs_init_and_load_webpage(void);

#endif // SPIFFS_H