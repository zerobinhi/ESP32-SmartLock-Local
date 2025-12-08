idf_component_register(
    SRCS "touch.c"
    INCLUDE_DIRS "."
    REQUIRES driver main esp_driver_touch_sens
)
