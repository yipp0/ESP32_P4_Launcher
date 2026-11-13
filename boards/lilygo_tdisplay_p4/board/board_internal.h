#pragma once

#include "bsp.h"
#include "driver/i2c_master.h"

esp_err_t board_c6_configure(void);
i2c_master_bus_handle_t board_i2c_bus(void);
i2c_master_bus_handle_t board_audio_i2c_bus(void);
void board_set_cap(uint32_t cap);
esp_err_t board_iox_update(uint16_t mask, uint16_t value);
esp_err_t board_i2c_device(i2c_master_bus_handle_t bus, uint8_t address,
                           i2c_master_dev_handle_t *device);
