#ifndef SPIFFS_H
#define SPIFFS_H

#include <sys/stat.h>
#include "esp_spiffs.h"
#include "app_config.h"

#define INDEX_HTML_PATH "/spiffs/index.html"

extern char *index_html;       // 动态分配缓冲区
extern char *response_data;

void spiffs_init_and_load_webpage(void);

#endif // SPIFFS_H
