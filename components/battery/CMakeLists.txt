idf_component_register(SRCS "battery.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver main esp_adc oled
                       )
