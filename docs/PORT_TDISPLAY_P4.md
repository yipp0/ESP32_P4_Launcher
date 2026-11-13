# LILYGO T-Display P4 V1 AMOLED

Board id: `lilygo_tdisplay_p4`. This package targets the H753 4.1-inch AMOLED
assembly on the original ESP32-P4 V1 board (chip revision 1.x), with 16 MB flash
and 32 MB hex PSRAM. The display is RM69A10, 568×1232, used in landscape as
1232×568. TFT/HI8561, V2, and P4 Air need separate board packages.

## Initial port

The board package implements the launcher BSP using ESP-IDF peripheral APIs.
It provides the AMOLED with two RGB565 buffers, GT9895 capacitive touch,
microSD (SDMMC slot 0), ES8311 audio output, PCF8563 RTC, BQ27220 battery voltage,
and the BOOT button as HOME. Touch reports are transformed from the controller's
1060×2400 coordinate range into the launcher's landscape coordinates.

The on-board LoRa radio is held in reset. GPS, camera, IMU, vibration and the
keyboard expansion are not exposed by this port. A USB gamepad/keyboard can use
the P4 USB host port; the V1 board needs an external 5 V source for VBUS.

P4-only resets leave the peripheral power rails and I2C chips running. The BSP
clears both I2C buses before its first transaction and retries power-expander
writes after bus recovery, including after a reset during an active touch read.

### Factory C6 firmware and offline mode

The factory C6 runs ESP-AT. ESP32_P4_Launcher normally expects ESP-Hosted; these
are different protocols. This initial board package sets `CONFIG_LX_NET_DISABLED=y`
and disables Bluetooth. The C6 is held in reset, network requests report no radio,
and ESP-Hosted's automatic startup constructor is excluded by the network
component's CMake configuration. Thus it creates no SDIO/RPC tasks and cannot
drive a guessed reset GPIO. Display, sound,
USB DevLink and SD apps work without changing the C6 firmware. The Store,
Wi-Fi updates, web file manager and network/BLE apps are unavailable offline.

The P4 communicates with the C6 on SDIO slot 1 (CLK18, CMD19, D0..D3=14..17),
and C6 EN is on XL9535 IO14 (bit 12). A future ESP-Hosted port must implement
that expander-controlled reset and migrate/verify the C6 firmware separately;
changing a GPIO number in sdkconfig is insufficient.

The tested board's **P4U** port enumerates as a WCH USB-UART bridge
(VID:PID `1a86:55d3`) connected to P4 UART0, not native USB Serial/JTAG.
The other USB-C port shares the P4's native USB data lines with USB-A for OTG.
This package therefore selects UART DevLink at **921600 baud**, leaving native
USB available to the input driver. Once DevLink starts, console logs on UART0
are framed so they cannot corrupt uploads. Bootloader/early logs are plain text
at 115200 baud. C6 UART0 is on its own four-pin programming connector.
LILYGO's coprocessor-download helper prepares
the rails and releases reset; its V1 implementation is not a UART data bridge.

## Build and flash

Use an ESP-IDF 5.5 environment (this port is built with 5.5.4; upstream uses 5.5.6).
DevLink's host tool requires Python 3.12 or later:

```sh
cd launcher
idf.py -B build_lilygo_tdisplay_p4 -DLX_BOARD=lilygo_tdisplay_p4 build
```

Connect a data cable to **P4U**. Save a full flash backup before replacing another
firmware, and keep it outside the repository. Flash the build using its own
offsets, then power-cycle the board:

```sh
idf.py -B build_lilygo_tdisplay_p4 -DLX_BOARD=lilygo_tdisplay_p4 -p PORT flash
```

For a first-install merged image at address 0:

```sh
cd ..
python tools/make_release.py
# build/release/launcher_lilygo_tdisplay_p4_full.bin
```

`make_release.py` discovers the board automatically. Its generated update URLs
still target upstream; upstream does not publish this board's firmware.

## Install and test apps over USB

Use an existing FAT32 or exFAT microSD card; the board never formats a card on
mount failure. Preserve existing files when copying the starter `SDcard/` content.
No Wi-Fi configuration is needed.

```sh
export PAPP_BAUD=921600
python tools/papp_dev.py --port PORT info
python tools/papp_dev.py --port PORT bootlog
python papp-sdk/tools/papp_build.py --name hello --src apps/hello/main.c
python tools/papp_dev.py --port PORT deploy build/apps/hello/hello.papp
python tools/papp_dev.py --port PORT shot tdisplay-p4.png
python tests/board/smoke.py --port PORT
```

`--baud 921600` on `papp_dev.py` can be used instead of `PAPP_BAUD`; setting the
environment variable also propagates to the smoke suite's subprocesses.

Build any additional test apps before running the smoke suite: unbuilt apps are
skipped. In particular, exercise app load/exit, libc, threads, display and audio.
Press BOOT only while already running to open the app menu; holding it while
resetting instead enters the ESP32-P4 ROM downloader.

## Validation

Tested on 2026-11-13 with a V1 H753 AMOLED board (P4 revision 1.0), ESP-IDF
5.5.4, an existing exFAT microSD card and the factory C6 firmware unchanged:

- The board image builds; the original `guition_p4_43` board also builds with
  the shared DevLink/network changes and its default native USB transport.
- Eight reset/reconnect cycles with the launcher UI active completed without
  a new crash record; SD mounted on every boot.
- A continuous DevLink session uploaded and ran `hello`, `libc_test`,
  `hid_test`, `thread_test`, `gfx_demo` and `audio_test`. All exited with code 0;
  the board remained running and reported no crash afterward.
- The final `tests/board/smoke.py` run passed the host relocation checks, all
  four smoke apps and the crash-record check across separate UART connections.
- The double-buffered 320×240 graphics demo, scaled to fit the landscape
  display, produced 2242 frames in 45 seconds (49.8 fps). Screenshots verified
  the home screen and the scaled application surface.
- Four simultaneous audio streams (11.025/22.05/32/44.1 kHz, U8/S16,
  mono/stereo) completed with zero underruns; five open/write/close cycles
  also passed. This validates the audio pipeline; speaker audibility still
  requires a listening check.
- DevLink reported working RTC time and the battery-voltage capability.
  Clock and Files were built and installed under the Tools folder.
- The board owner confirmed that physical touch opens Tools and Clock, and
  that Clock displays correctly.

USB controller hardware, touch edge accuracy/multi-touch, battery-only
runtime and RTC retention across a loss of power still need manual testing.
The HID parser test exercises descriptors without a physical USB controller.

## Hardware references

- [LILYGO V1 pin definitions](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/main/src/device/t_display_p4/v1/config.h)
- [LILYGO display timings and touch scaling](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/main/src/device/t_display_p4/config.h)
- [LILYGO board power control](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/main/src/device/t_display_p4/v1/driver.cpp)
- [RM69A10 register initialization reference](https://github.com/Llgok/cpp_bus_driver/blob/main/src/chip/mipi/rm69a10.h)
- [GT9895 register protocol reference](https://github.com/Llgok/cpp_bus_driver/blob/main/src/chip/i2c/touch/gt9895.cpp)
- [V1 AMOLED schematic, 2026-01-06](https://github.com/Xinyuan-LilyGO/T-Display-P4/blob/main/project/T-Display-P4_V1.0%28H0410S001AMT001-V0%29_202601061148.pdf)
- [LILYGO coprocessor programming instructions](https://github.com/Xinyuan-LilyGO/T-Display-P4#flashing-the-coprocessor-network-adapter-firmware)

The pin numbers, register values and protocols above were used as hardware
references; this package does not link LILYGO's vendor driver libraries.
