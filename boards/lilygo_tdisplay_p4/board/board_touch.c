/* GT9895: 32-bit register addresses, runtime-discovered event buffer and
 * checksummed contact reports. This is not the GT911 register protocol. */
#include "board_internal.h"
#include "board_pins.h"
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "touch";
static i2c_master_dev_handle_t s_touch;
static uint32_t s_events;
static bsp_touch_point_t s_last[BSP_TOUCH_MAX_POINTS];
static int s_count;

static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
static bool checksum(const uint8_t *p, size_t len)
{
    if (len < 2) return false;
    uint16_t sum = 0;
    for (size_t i = 0; i < len - 2; ++i) sum += p[i];
    return sum == le16(p + len - 2);
}

static esp_err_t read_reg(uint32_t address, uint8_t *data, size_t len)
{
    uint8_t reg[] = {address >> 24, address >> 16, address >> 8, address};
    return i2c_master_transmit_receive(s_touch, reg, sizeof(reg), data, len, 100);
}

static esp_err_t acknowledge(void)
{
    uint8_t data[] = {s_events >> 24, s_events >> 16, s_events >> 8, s_events, 0};
    return i2c_master_transmit(s_touch, data, sizeof(data), 100);
}

esp_err_t bsp_touch_init(void)
{
    if (s_events) return ESP_OK;
    ESP_RETURN_ON_ERROR(board_i2c_device(board_i2c_bus(), 0x5d, &s_touch), TAG, "device");
    uint8_t firmware[28], info[1024];
    esp_err_t err = read_reg(0x10014, firmware, sizeof(firmware));
    if (err != ESP_OK || !checksum(firmware, sizeof(firmware)) || memcmp(firmware + 10, "9895", 4)) {
        err = ESP_ERR_NOT_FOUND;
        goto fail;
    }
    if ((err = read_reg(0x10070, info, 2)) != ESP_OK) goto fail;
    size_t len = le16(info);
    if (len < 87 || len > sizeof(info)) { err = ESP_ERR_INVALID_SIZE; goto fail; }
    if ((err = read_reg(0x10070, info, len)) != ESP_OK) goto fail;
    if (!checksum(info, len)) { err = ESP_ERR_INVALID_CRC; goto fail; }
    size_t offset = 32; /* length, version, features, fixed parameters */
    for (int i = 0; i < 5; ++i) {
        if (offset >= len - 2) { err = ESP_ERR_INVALID_SIZE; goto fail; }
        size_t bytes = (size_t)info[offset++] * 2;
        if (bytes > len - 2 - offset) { err = ESP_ERR_INVALID_SIZE; goto fail; }
        offset += bytes;
    }
    if (offset + 48 > len - 2) { err = ESP_ERR_INVALID_SIZE; goto fail; }
    s_events = le32(info + offset + 44);
    if (!s_events) { err = ESP_ERR_INVALID_RESPONSE; goto fail; }
    acknowledge();
    board_set_cap(BSP_CAP_TOUCH);
    ESP_LOGI(TAG, "GT9895 event buffer 0x%08lx, raw 1060x2400", (unsigned long)s_events);
    return ESP_OK;
fail:
    i2c_master_bus_rm_device(s_touch);
    s_touch = NULL;
    s_events = 0;
    return err;
}

int bsp_touch_read(bsp_touch_point_t *points, int max)
{
    if (!s_events || !points || max < 0) return -1;
    uint8_t report[8 + 10 * 8 + 2];
    if (read_reg(s_events, report, sizeof(report)) != ESP_OK) return -1;
    if (!report[0]) goto out;
    if (!checksum(report, 8)) return -1;
    unsigned count = report[2] & 15;
    if (count > 10) { acknowledge(); return -1; }
    if (!(report[0] & 0x80)) { acknowledge(); goto out; }
    if (count && !checksum(report + 8, count * 8 + 2)) return -1;
    s_count = count < BSP_TOUCH_MAX_POINTS ? count : BSP_TOUCH_MAX_POINTS;
    for (int i = 0; i < s_count; ++i) {
        const uint8_t *p = report + 8 + i * 8;
        int x = (uint32_t)le16(p + 2) * BOARD_LCD_H_RES / 1060;
        int y = (uint32_t)le16(p + 4) * BOARD_LCD_V_RES / 2400;
        if (x >= BOARD_LCD_H_RES) x = BOARD_LCD_H_RES - 1;
        if (y >= BOARD_LCD_V_RES) y = BOARD_LCD_V_RES - 1;
        /* Inverse of the compositor's 270-degree rotation. */
        s_last[i] = (bsp_touch_point_t){
            .x = y, .y = BOARD_LCD_H_RES - 1 - x,
            .id = p[0] >> 4, .size = le16(p + 6),
        };
    }
    if (acknowledge() != ESP_OK) return -1;
out:
    int n = s_count < max ? s_count : max;
    memcpy(points, s_last, n * sizeof(*points));
    return n;
}
