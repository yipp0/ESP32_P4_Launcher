/* One-time T-Display P4 V1 C6 migration over its existing SDIO wiring.
 * No eFuse writes. A verified full backup is required before flashing.
 * Uses the ESP Serial Flasher protocol; see the pinned source in README.md. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "bsp.h"
#include "board_internal.h"
#include "board_pins.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_loader.h"
#include "driver/uart.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

bool bytes_md5(const uint8_t *data, size_t len, uint8_t digest[16]);

#define FLASH_SIZE (4 * 1024 * 1024)
#define IMAGE "/sd/update/c6-hosted-2.12.13-full.bin"
static esp_loader_t loader;
static sdmmc_card_t card;
static sdmmc_host_t host;
static int64_t deadline;
static bool connected, backed_up, backup_sent;
static uint8_t block[4096] __attribute__((aligned(64)));
static uint8_t *image, *backup_data;
static size_t image_size;

static void reset_target(esp_loader_port_t *p)
{
    ESP_ERROR_CHECK(board_iox_update(BOARD_IOX_C6_ENABLE, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(board_iox_update(BOARD_IOX_C6_ENABLE, BOARD_IOX_C6_ENABLE));
    vTaskDelay(pdMS_TO_TICKS(150));
}
static esp_loader_error_t init(esp_loader_port_t *p)
{
    host = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    host.slot = 1;
    host.max_freq_khz = 5000;
    host.flags = SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = 18; slot.cmd = 19;
    slot.d0 = 14; slot.d1 = 15; slot.d2 = 16; slot.d3 = 17;
    slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    if (sdmmc_host_init() != ESP_OK) return ESP_LOADER_ERROR_FAIL;
    return sdmmc_host_init_slot(1, &slot) == ESP_OK ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_FAIL;
}
static esp_loader_error_t card_init(esp_loader_port_t *p)
{
    return sdmmc_card_init(&host, &card) == ESP_OK ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_FAIL;
}
static esp_loader_error_t read_sdio(esp_loader_port_t *p, uint32_t fn, uint32_t addr,
                                   uint8_t *data, uint16_t len, uint32_t timeout)
{
    return sdmmc_io_read_bytes(&card, fn, addr, data, len) == ESP_OK ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_FAIL;
}
static esp_loader_error_t write_sdio(esp_loader_port_t *p, uint32_t fn, uint32_t addr,
                                    const uint8_t *data, uint16_t len, uint32_t timeout)
{
    return sdmmc_io_write_bytes(&card, fn, addr, data, len) == ESP_OK ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_FAIL;
}
static void start_timer(esp_loader_port_t *p, uint32_t ms) { deadline = esp_timer_get_time() + (int64_t)ms * 1000; }
static uint32_t remaining(esp_loader_port_t *p) { int64_t ms = (deadline - esp_timer_get_time()) / 1000; return ms > 0 ? ms : 0; }
static void delay(esp_loader_port_t *p, uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
static const esp_loader_port_ops_t ops = {
    .init = init, .enter_bootloader = reset_target, .reset_target = reset_target,
    .start_timer = start_timer, .remaining_time = remaining, .delay_ms = delay,
    .sdio_read = read_sdio, .sdio_write = write_sdio, .sdio_card_init = card_init,
};
static esp_loader_port_t port = {.ops = &ops};

static bool backup(void)
{
    if (!connected || !backup_data) return false;
    for (uint32_t pos = 0; pos < FLASH_SIZE; pos += sizeof(block)) {
        esp_loader_error_t e = esp_loader_flash_read(&loader, block, pos, sizeof(block));
        if (e != ESP_LOADER_SUCCESS) {
            printf("BACKUP FAILED at 0x%lx error %d\n", (unsigned long)pos, e);
            return false;
        }
        memcpy(backup_data + pos, block, sizeof(block));
        if (!(pos % (256 * 1024))) printf("BACKUP %lu/%u\n", (unsigned long)pos, FLASH_SIZE);
    }
    uint8_t md5[16];
    if (!bytes_md5(backup_data, FLASH_SIZE, md5)) return false;
    char hex[33];
    for (int i = 0; i < 16; ++i) snprintf(hex + i * 2, 3, "%02x", md5[i]);
    if (esp_loader_flash_verify_known_md5(&loader, 0, FLASH_SIZE, (uint8_t *)hex) != ESP_LOADER_SUCCESS) {
        puts("BACKUP FAILED: target MD5 mismatch"); return false;
    }
    printf("BACKUP DATA %u ", FLASH_SIZE);
    for (int i = 0; i < 16; ++i) printf("%02x", md5[i]);
    puts(""); fflush(stdout);
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(1000));
    for (size_t pos = 0; pos < FLASH_SIZE; pos += sizeof(block))
        uart_write_bytes(UART_NUM_0, backup_data + pos, sizeof(block));
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(1000));
    backup_sent = true;
    puts("\nBACKUP SENT; send backup-ok only after the host verifies its saved file");
    return true;
}

static bool flash(void)
{
    if (!connected || !backed_up) { puts("FLASH REFUSED: verified host backup required"); return false; }
    esp_loader_flash_cfg_t cfg = {.offset = 0, .image_size = image_size, .block_size = sizeof(block)};
    esp_loader_error_t e = esp_loader_flash_start(&loader, &cfg);
    for (size_t pos = 0; e == ESP_LOADER_SUCCESS && pos < image_size; pos += sizeof(block)) {
        size_t n = image_size - pos < sizeof(block) ? image_size - pos : sizeof(block);
        memcpy(block, image + pos, n);
        e = esp_loader_flash_write(&loader, &cfg, block, n);
    }
    if (e == ESP_LOADER_SUCCESS) e = esp_loader_flash_finish(&loader, &cfg);
    printf("FLASH %s error %d\n", e == ESP_LOADER_SUCCESS ? "VERIFIED" : "FAILED", e);
    backed_up = false;
    return e == ESP_LOADER_SUCCESS;
}

void app_main(void)
{
    ESP_ERROR_CHECK(bsp_init());
    ESP_ERROR_CHECK(bsp_sdcard_mount("/sd"));
    /* Read the image before initializing SDIO. Do not access the SD card while
     * the C6 is connected: this IDF/board combination cannot reliably mix both. */
    FILE *f = fopen(IMAGE, "rb");
    assert(f);
    fseek(f, 0, SEEK_END); image_size = ftell(f); rewind(f);
    assert(image_size > 0x10000 && image_size <= FLASH_SIZE && !(image_size & 3));
    image = heap_caps_malloc(image_size, MALLOC_CAP_SPIRAM);
    backup_data = heap_caps_malloc(FLASH_SIZE, MALLOC_CAP_SPIRAM);
    assert(image && backup_data);
    assert(fread(image, 1, image_size, f) == image_size);
    fclose(f);
    assert(image[0] == 0xe9 && image[12] == 13 && image[13] == 0);
    /* The utility never accesses the mounted card again. Drop both host-slot
     * contexts before initializing the dedicated C6 programming bus. */
    ESP_ERROR_CHECK(sdmmc_host_deinit());
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 16384, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_set_baudrate(UART_NUM_0, 921600));
    uart_flush_input(UART_NUM_0);
    ESP_ERROR_CHECK(esp_loader_init_sdio(&loader, &port) == ESP_LOADER_SUCCESS ? ESP_OK : ESP_FAIL);
    puts("C6 SDIO TOOL READY: hold C6 BOOT, send connect; then backup, flash, reset");
    char cmd[32]; size_t used = 0;
    for (;;) {
        char c;
        if (uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(100)) != 1) continue;
        if (c == '\n' || c == '\r') {
            cmd[used] = 0; used = 0;
            if (!strcmp(cmd, "connect")) {
                backed_up = backup_sent = false;
                esp_loader_connect_args_t args = ESP_LOADER_CONNECT_DEFAULT();
                args.sync_timeout = 500;
                esp_loader_error_t e = esp_loader_connect(&loader, &args);
                uint32_t size = 0;
                connected = e == ESP_LOADER_SUCCESS && esp_loader_get_target(&loader) == ESP32C6_CHIP &&
                    esp_loader_flash_detect_size(&loader, &size) == ESP_LOADER_SUCCESS && size == FLASH_SIZE;
                printf("CONNECT %s error %d size %lu\n", connected ? "OK" : "FAILED", e, (unsigned long)size);
            } else if (!strcmp(cmd, "backup")) backup();
            else if (!strcmp(cmd, "backup-ok") && connected && backup_sent) { backed_up = true; puts("HOST BACKUP ACKNOWLEDGED"); }
            else if (!strcmp(cmd, "flash")) flash();
            else if (!strcmp(cmd, "reset")) { reset_target(&port); connected = backed_up = false; puts("C6 RESET"); }
            else if (cmd[0]) puts("UNKNOWN COMMAND");
        } else if (used < sizeof(cmd) - 1) cmd[used++] = c;
    }
}
