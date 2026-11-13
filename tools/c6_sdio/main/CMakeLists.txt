idf_component_register(SRCS "main.c" "hash.c" "../../../boards/lilygo_tdisplay_p4/board/board_c6.c" "../../../boards/lilygo_tdisplay_p4/board/board.c"
    INCLUDE_DIRS "../../../boards/lilygo_tdisplay_p4/board"
    REQUIRES bsp fatfs sdmmc esp_driver_sdmmc esp_driver_uart esp_driver_i2c
             esp_hw_support esp_timer esp_serial_flasher mbedtls)
