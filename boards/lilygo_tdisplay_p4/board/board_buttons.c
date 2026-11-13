#include "board_internal.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static bool s_buttons;
static i2c_master_dev_handle_t s_gauge;
static int s_mv = -1;
static int64_t s_sample_us;

esp_err_t bsp_buttons_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_BOOT_GPIO,
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    s_buttons = err == ESP_OK;
    if (s_buttons) board_set_cap(BSP_CAP_GPIO_PAD);
    if (board_i2c_device(board_i2c_bus(), 0x55, &s_gauge) == ESP_OK && bsp_battery_mv() > 0)
        board_set_cap(BSP_CAP_BATTERY);
    return err;
}

uint32_t bsp_buttons_read(void)
{
    return s_buttons && !gpio_get_level(BOARD_BOOT_GPIO) ? BSP_BTN_HOME : 0;
}

int bsp_battery_mv(void)
{
    if (!s_gauge) return -1;
    int64_t now = esp_timer_get_time();
    if (s_sample_us && now - s_sample_us < 1000000) return s_mv;
    s_sample_us = now;
    uint8_t reg = 0x08, raw[2];
    s_mv = i2c_master_transmit_receive(s_gauge, &reg, 1, raw, 2, 50) == ESP_OK
           ? raw[0] | raw[1] << 8 : -1;
    return s_mv;
}

int bsp_analog_read(int channel) { return -1; }
