/* PCF8563 at 0x51. Device time is UTC; the VL flag marks lost/invalid time. */
#include "board_internal.h"
#include <time.h>

static i2c_master_dev_handle_t s_rtc;
static int dec(uint8_t b) { return (b >> 4) * 10 + (b & 15); }
static uint8_t bcd(int n) { return (n / 10) << 4 | n % 10; }

esp_err_t bsp_rtc_init(void)
{
    if (s_rtc) return ESP_OK;
    esp_err_t err = board_i2c_device(board_i2c_bus(), 0x51, &s_rtc);
    if (err != ESP_OK) return err;
    uint8_t reg = 0, status[2];
    err = i2c_master_transmit_receive(s_rtc, &reg, 1, status, 2, 100);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(s_rtc); s_rtc = NULL;
        return err;
    }
    board_set_cap(BSP_CAP_RTC);
    return ESP_OK;
}

esp_err_t bsp_rtc_get(int64_t *utc)
{
    if (!s_rtc) return ESP_ERR_NOT_SUPPORTED;
    if (!utc) return ESP_ERR_INVALID_ARG;
    uint8_t reg = 0, r[9];
    esp_err_t err = i2c_master_transmit_receive(s_rtc, &reg, 1, r, sizeof(r), 100);
    if (err != ESP_OK) return err;
    struct tm t = {
        .tm_sec = dec(r[2] & 0x7f), .tm_min = dec(r[3] & 0x7f),
        .tm_hour = dec(r[4] & 0x3f), .tm_mday = dec(r[5] & 0x3f),
        .tm_mon = dec(r[7] & 0x1f) - 1, .tm_year = 100 + dec(r[8]),
    };
    if ((r[0] & 0x20) || (r[2] & 0x80) || t.tm_sec > 59 || t.tm_min > 59 ||
        t.tm_hour > 23 || t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31)
        return ESP_ERR_INVALID_STATE;
    /* Gregorian civil date -> UTC days since epoch, independent of the timezone. */
    int y = t.tm_year + 1900, m = t.tm_mon + 1;
    y -= m <= 2;
    int era = y / 400, yo = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t.tm_mday - 1;
    int64_t days = (int64_t)era * 146097 + yo * 365 + yo / 4 - yo / 100 + doy - 719468;
    *utc = days * 86400 + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    return ESP_OK;
}

esp_err_t bsp_rtc_set(int64_t utc)
{
    if (!s_rtc) return ESP_ERR_NOT_SUPPORTED;
    if (utc < 946684800 || utc >= 4102444800LL) return ESP_ERR_INVALID_ARG;
    time_t seconds = utc;
    struct tm t;
    gmtime_r(&seconds, &t);
    uint8_t r[] = {2, bcd(t.tm_sec), bcd(t.tm_min), bcd(t.tm_hour), bcd(t.tm_mday),
                   bcd(t.tm_wday), bcd(t.tm_mon + 1), bcd(t.tm_year - 100)};
    esp_err_t err = i2c_master_transmit(s_rtc, r, sizeof(r), 100);
    if (err == ESP_OK) {
        uint8_t run[] = {0, 0};
        err = i2c_master_transmit(s_rtc, run, sizeof(run), 100);
    }
    return err;
}
