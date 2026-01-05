#ifndef SPIFFS_H
#define SPIFFS_H

#include <sys/stat.h>
#include <esp_spiffs.h>
#include "app_config.h"

#define INDEX_HTML_PATH "/spiffs/index.html"

extern char *index_html; // dynamically allocated buffer for index.html

void spiffs_init_and_load_webpage(void);

#endif
