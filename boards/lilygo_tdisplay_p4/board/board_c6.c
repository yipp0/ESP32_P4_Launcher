/* ESP-Hosted 2.12.13's GPIO abstraction supports port+pin identities.
 * Use a tagged XL9535 port for C6 EN instead of driving a P4 GPIO.
 * Installed by the BSP before the deferred ESP-Hosted initialization. */
#include "board_internal.h"
#include "board_pins.h"
#if CONFIG_LX_HOSTED_DEFERRED_INIT
#include "esp_hosted_transport_config.h"
/* The pinned 2.12.13 ESP port always defines H_USE_MEMPOOL (to 0 or 1).
 * Its public OSI header uses #ifdef, so preserve those vtable entries here. */
#define H_USE_MEMPOOL CONFIG_ESP_HOSTED_USE_MEMPOOL
#include "esp_hosted_os_abstraction.h"
#include "esp_log.h"

static char s_expander_port;
static int (*s_config)(void *, uint32_t, uint32_t);
static int (*s_write)(void *, uint32_t, uint32_t);

static int config_gpio(void *port, uint32_t pin, uint32_t mode)
{
    if (port == &s_expander_port)
        return pin == 12 ? ESP_OK : ESP_ERR_INVALID_ARG; /* already configured by BSP */
    return s_config(port, pin, mode);
}
static int write_gpio(void *port, uint32_t pin, uint32_t value)
{
    if (port == &s_expander_port) {
        if (pin != 12) return ESP_ERR_INVALID_ARG;
        esp_err_t err = board_iox_update(BOARD_IOX_C6_ENABLE, value ? BOARD_IOX_C6_ENABLE : 0);
        if (err != ESP_OK) ESP_LOGE("bsp_c6", "C6 reset write failed: %s", esp_err_to_name(err));
        return err;
    }
    return s_write(port, pin, value);
}

esp_err_t board_c6_configure(void)
{
    if (s_config) return ESP_OK;
    struct esp_hosted_sdio_config cfg = INIT_DEFAULT_HOST_SDIO_CONFIG();
    cfg.slot = 1;
    cfg.clock_freq_khz = 20000;
    cfg.bus_width = 4;
    cfg.iomux_enable = false;
    cfg.pin_clk.pin = 18; cfg.pin_cmd.pin = 19;
    cfg.pin_d0.pin = 14; cfg.pin_d1.pin = 15;
    cfg.pin_d2.pin = 16; cfg.pin_d3.pin = 17;
    cfg.pin_reset = (gpio_pin_t){.port = &s_expander_port, .pin = 12};
    esp_err_t err = esp_hosted_sdio_set_config(&cfg);
    if (err != ESP_OK) return err;
    s_config = g_hosted_osi_funcs._h_config_gpio;
    s_write = g_hosted_osi_funcs._h_write_gpio;
    g_hosted_osi_funcs._h_config_gpio = config_gpio;
    g_hosted_osi_funcs._h_write_gpio = write_gpio;
    ESP_LOGI("bsp_c6", "C6 SDIO slot 1 at 20 MHz; reset on XL9535 IO14");
    return ESP_OK;
}
#else
esp_err_t board_c6_configure(void) { return ESP_OK; }
#endif
