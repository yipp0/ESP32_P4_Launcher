/* T-Display P4 V1 / H753 AMOLED. See docs/PORT_TDISPLAY_P4.md for references.
 * XL9535 bank 1 labels IO10..IO17 correspond to bits 8..15, not 10..17. */
#pragma once

#define BOARD_LCD_H_RES 568
#define BOARD_LCD_V_RES 1232
#define BOARD_DSI_PHY_LDO_CHAN 3
#define BOARD_DSI_PHY_LDO_MV 2500
#define BOARD_SD_LDO_CHAN 4

#define BOARD_I2C_SDA 7
#define BOARD_I2C_SCL 8
#define BOARD_AUDIO_I2C_SDA 20
#define BOARD_AUDIO_I2C_SCL 21
#define BOARD_IOX_ADDR 0x20
#define BOARD_IOX_LCD_RESET (1u << 2)
#define BOARD_IOX_TOUCH_RESET (1u << 3)
#define BOARD_IOX_AUDIO_ENABLE (1u << 6)
#define BOARD_IOX_C6_ENABLE (1u << 12)
#define BOARD_IOX_SD_DISABLE (1u << 13)

#define BOARD_I2S_MCLK 13
#define BOARD_I2S_BCLK 12
#define BOARD_I2S_WS 9
#define BOARD_I2S_DOUT 10
#define BOARD_AUDIO_PA (-1) /* supply/amp enable is XL9535 IO6 */
#define BOARD_AUDIO_RATE 48000

#define BOARD_SD_CLK 43
#define BOARD_SD_CMD 44
#define BOARD_SD_D0 39
#define BOARD_SD_D1 40
#define BOARD_SD_D2 41
#define BOARD_SD_D3 42
#define BOARD_BOOT_GPIO 35
