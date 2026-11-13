/* Board wiring and safe power states, implemented against the ESP-IDF API.
 * Register/pin reference: LILYGO's T-Display P4 V1 hardware configuration. */
#include "board_internal.h"
#include "board_pins.h"

#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if !CONFIG_LX_NET_DISABLED
#error "T-Display P4 V1 currently requires LX_NET_DISABLED (factory C6 is ESP-AT)"
#endif

static const char *TAG = "bsp_p4";
static bsp_info_t s_info = {
    .id = "lilygo_tdisplay_p4",
    .name = "LILYGO T-Display P4 V1 AMOLED",
    .display_w = BOARD_LCD_H_RES,
    .display_h = BOARD_LCD_V_RES,
    .display_rot = 270,
};
static i2c_master_bus_handle_t s_i2c, s_audio_i2c;
static i2c_master_dev_handle_t s_iox;
static SemaphoreHandle_t s_iox_lock;
static uint16_t s_output;
static bool s_sd_mounted;
static sd_pwr_ctrl_handle_t s_sd_power;
static esp_ldo_channel_handle_t s_phy_power;

const bsp_info_t *bsp_info(void) { return &s_info; }
void board_set_cap(uint32_t cap) { s_info.caps |= cap; }
i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c; }
i2c_master_bus_handle_t board_audio_i2c_bus(void) { return s_audio_i2c; }

esp_err_t board_i2c_device(i2c_master_bus_handle_t bus, uint8_t address,
                           i2c_master_dev_handle_t *device)
{
    if (!bus || !device) return ESP_ERR_INVALID_ARG;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address, .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &cfg, device);
}

static esp_err_t iox_write(uint8_t reg, uint16_t value)
{
    uint8_t data[] = {reg, value & 0xff, value >> 8};
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 3; ++attempt) {
        err = i2c_master_transmit(s_iox, data, sizeof(data), 100);
        if (err == ESP_OK) break;
        /* RST resets the P4, not the I2C peripherals. A reset during a read
         * can leave a slave holding SDA; clear it before retrying the same
         * idempotent output/direction write. Never continue with unknown rails. */
        i2c_master_bus_reset(s_i2c);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return err;
}

esp_err_t board_iox_update(uint16_t mask, uint16_t value)
{
    if (!s_iox || !s_iox_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_iox_lock, portMAX_DELAY);
    uint16_t next = (s_output & ~mask) | (value & mask);
    esp_err_t err = iox_write(0x02, next);
    if (err == ESP_OK) s_output = next;
    xSemaphoreGive(s_iox_lock);
    return err;
}

static esp_err_t new_bus(int port, int sda, int scl, i2c_master_bus_handle_t *out)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = port, .sda_io_num = sda, .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, out);
    if (err == ESP_OK) err = i2c_master_bus_reset(*out);
    return err;
}

esp_err_t bsp_init(void)
{
    if (s_iox) return ESP_OK;
    esp_ldo_channel_config_t phy = {
        .chan_id = BOARD_DSI_PHY_LDO_CHAN, .voltage_mv = BOARD_DSI_PHY_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&phy, &s_phy_power), TAG, "DSI supply");
    sd_pwr_ctrl_ldo_config_t sd = {.ldo_chan_id = BOARD_SD_LDO_CHAN};
    ESP_RETURN_ON_ERROR(sd_pwr_ctrl_new_on_chip_ldo(&sd, &s_sd_power), TAG, "SD supply");
    ESP_RETURN_ON_ERROR(new_bus(0, BOARD_I2C_SDA, BOARD_I2C_SCL, &s_i2c), TAG, "I2C");
    ESP_RETURN_ON_ERROR(new_bus(1, BOARD_AUDIO_I2C_SDA, BOARD_AUDIO_I2C_SCL, &s_audio_i2c), TAG, "audio I2C");
    ESP_RETURN_ON_ERROR(board_i2c_device(s_i2c, BOARD_IOX_ADDR, &s_iox), TAG, "XL9535");
    s_iox_lock = xSemaphoreCreateMutex();
    if (!s_iox_lock) return ESP_ERR_NO_MEM;
    /* Preload outputs before enabling them: main 3V3 is active LOW and must
     * stay enabled. Hold C6/radio/Ethernet reset; SD power is active LOW.
     * IO1 selects the internal antenna; IO6 supplies audio; IO10 supplies PHY.
     * Inputs: IO4, IO7, IO12, IO13, IO17 (bits 4,7,10,11,15). */
    s_output = 0x0142;
    ESP_RETURN_ON_ERROR(iox_write(0x02, s_output), TAG, "XL9535 safe outputs");
    ESP_RETURN_ON_ERROR(iox_write(0x06, 0x8c90), TAG, "XL9535 directions");
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_RETURN_ON_ERROR(board_iox_update(BOARD_IOX_LCD_RESET | BOARD_IOX_TOUCH_RESET,
                                       BOARD_IOX_LCD_RESET | BOARD_IOX_TOUCH_RESET), TAG, "panel reset");
    vTaskDelay(pdMS_TO_TICKS(120));
    board_set_cap(BSP_CAP_USB_HOST);
    ESP_LOGI(TAG, "V1 power ready; C6 held in reset (offline build)");
    return ESP_OK;
}

esp_err_t bsp_sdcard_mount(const char *mount_point)
{
    if (s_sd_mounted) return ESP_OK;
    ESP_RETURN_ON_ERROR(board_iox_update(BOARD_IOX_SD_DISABLE, 0), TAG, "SD power");
    vTaskDelay(pdMS_TO_TICKS(20));
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    host.pwr_ctrl_handle = s_sd_power;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = BOARD_SD_CLK; slot.cmd = BOARD_SD_CMD;
    slot.d0 = BOARD_SD_D0; slot.d1 = BOARD_SD_D1;
    slot.d2 = BOARD_SD_D2; slot.d3 = BOARD_SD_D3;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t cfg = {
        .format_if_mount_failed = false, .max_files = 16,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot, &cfg, &card);
    if (err == ESP_OK) {
        s_sd_mounted = true;
        board_set_cap(BSP_CAP_SDCARD);
        ESP_LOGI(TAG, "SD mounted at %s (%s)", mount_point, card->cid.name);
    }
    return err;
}

bool bsp_sdcard_mounted(void) { return s_sd_mounted; }
