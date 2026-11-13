idf_component_register(
    SRCS "board.c" "board_display.c" "board_touch.c" "board_audio.c" "board_buttons.c" "board_rtc.c" "board_c6.c"
    INCLUDE_DIRS "."
    REQUIRES bsp fatfs vfs sdmmc esp_driver_sdmmc esp_driver_sdspi esp_driver_spi esp_driver_gpio esp_lcd
             esp_hw_support esp_mm esp_driver_i2c esp_driver_i2s esp_timer espressif__esp_hosted
    WHOLE_ARCHIVE
)
