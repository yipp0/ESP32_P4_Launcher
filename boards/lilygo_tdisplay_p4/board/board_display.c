/* RM69A10, two-lane MIPI DSI, RGB565 double buffering.
 * Panel register values/timings: LILYGO RM69A10 / H0410S001AMT001-V0 reference.
 * No vendor driver library is linked; this uses esp_lcd directly. */
#include "board_internal.h"
#include "board_pins.h"
#include <string.h>
#include "esp_check.h"
#include "esp_cache.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "bsp_disp";
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_fb[2];
static SemaphoreHandle_t s_vsync;
static volatile bool s_pending;
static int s_visible;

static bool IRAM_ATTR refreshed(esp_lcd_panel_handle_t panel,
                                esp_lcd_dpi_panel_event_data_t *event, void *ctx)
{
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(s_vsync, &wake);
    return wake == pdTRUE;
}

static esp_err_t command(int reg, const uint8_t *data, size_t len)
{
    return esp_lcd_panel_io_tx_param(s_io, reg, data, len);
}

esp_err_t bsp_display_init(void)
{
    if (s_panel) return ESP_OK;
    esp_lcd_dsi_bus_handle_t bus;
    esp_lcd_dsi_bus_config_t dsi = {
        .bus_id = 0, .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT, .lane_bit_rate_mbps = 1000,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&dsi, &bus), TAG, "DSI");
    esp_lcd_dbi_io_config_t dbi = {
        .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &dbi, &s_io), TAG, "DBI");
    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_rx_param(s_io, 0xa1, &id, 1), TAG, "panel ID");
    if (id != 1) return ESP_ERR_NOT_FOUND;

    /* Physical timing is portrait; the launcher compositor rotates landscape
     * surfaces. Brightness stays zero until both framebuffers are black. */
    esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel = 0, .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 60, .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 2,
        .video_timing = {
            .h_size = BOARD_LCD_H_RES, .v_size = BOARD_LCD_V_RES,
            .hsync_pulse_width = 50, .hsync_back_porch = 150, .hsync_front_porch = 50,
            .vsync_pulse_width = 40, .vsync_back_porch = 120, .vsync_front_porch = 80,
        },
        .flags.use_dma2d = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(bus, &dpi, &s_panel), TAG, "DPI");
    static const struct { uint8_t reg, len, data[4]; } init[] = {
        {0xfe, 1, {0xfd}}, {0x80, 1, {0xfc}}, {0xfe, 1, {0}},
        {0x2a, 4, {0, 0, 0x02, 0x37}}, {0x2b, 4, {0, 0, 0x04, 0xcf}},
        {0x31, 4, {0, 3, 0x02, 0x34}}, {0x30, 4, {0, 0, 0x04, 0xcf}},
        {0x12, 1, {0}}, {0x35, 1, {0}}, {0x51, 1, {0}}, {0x11, 0, {0}},
    };
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i)
        ESP_RETURN_ON_ERROR(command(init[i].reg, init[i].data, init[i].len), TAG, "panel command");
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_RETURN_ON_ERROR(command(0x29, NULL, 0), TAG, "display on");
    void *fb0 = NULL, *fb1 = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1), TAG, "buffers");
    s_fb[0] = fb0;
    s_fb[1] = fb1;
    size_t bytes = BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t);
    for (int i = 0; i < 2; ++i) {
        memset(s_fb[i], 0, bytes);
        ESP_RETURN_ON_ERROR(esp_cache_msync(s_fb[i], bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M), TAG, "cache");
    }
    s_vsync = xSemaphoreCreateBinary();
    if (!s_vsync) return ESP_ERR_NO_MEM;
    esp_lcd_dpi_panel_event_callbacks_t cb = {.on_refresh_done = refreshed};
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cb, NULL), TAG, "vsync");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "scanout");
    bsp_display_backlight(70);
    board_set_cap(BSP_CAP_DISPLAY);
    ESP_LOGI(TAG, "RM69A10 %dx%d, two RGB565 buffers", BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}

int bsp_display_fb_count(void) { return s_panel ? 2 : 0; }
uint16_t *bsp_display_fb(int index) { return index >= 0 && index < 2 ? s_fb[index] : NULL; }
int bsp_display_visible(void) { return s_visible; }

esp_err_t bsp_display_show(int index)
{
    if (!s_panel || index < 0 || index > 1) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_vsync, 0);
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, BOARD_LCD_H_RES, BOARD_LCD_V_RES, s_fb[index]);
    if (err == ESP_OK) { s_visible = index; s_pending = true; }
    return err;
}

bool bsp_display_wait_flip(uint32_t timeout_ms)
{
    if (!s_pending) return true;
    bool done = xSemaphoreTake(s_vsync, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    if (done) s_pending = false;
    return done;
}

bool bsp_display_wait_vsync(uint32_t timeout_ms)
{
    if (!s_vsync) return false;
    xSemaphoreTake(s_vsync, 0);
    return xSemaphoreTake(s_vsync, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void bsp_display_backlight(int percent)
{
    if (!s_io) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint8_t value = percent * 255 / 100;
    command(0x51, &value, 1);
}
