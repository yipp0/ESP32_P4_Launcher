# T-Display P4 V1: C6 migration over SDIO

A temporary ESP32-P4 application that programs the board's ESP32-C6 using the
existing SDIO wiring. It needs a microSD card, the P4U USB-UART connection and
access to the **C6 BOOT** button. It does not need an external UART adapter.
The board's other USB-C port is the P4 OTG connection, not C6 UART.

This utility replaces the P4 application temporarily. Restore the launcher when
finished. These instructions assume this port's factory application at 0x20000;
check the active application/partition layout before using them on another setup.
No eFuses are written. Preserve the full C6 backup outside Git.

## Dependencies and image

Use ESP-IDF 5.5.4 and Python with pyserial. Fetch the exact tested flasher revision
into a directory named `esp_serial_flasher`:

```sh
git clone https://github.com/espressif/esp-serial-flasher.git /tmp/esp_serial_flasher
git -C /tmp/esp_serial_flasher checkout ddf3cdd8fa87f3d26b42f1b8ad94bbbb67745f50
```

From the repository root, first configure/build the T-Display launcher, then build
its matching C6 image:

```sh
python tools/build_c6_fw.py --board lilygo_tdisplay_p4
cd build/c6_fw/slave_2.12.13/build
python -m esptool --chip esp32c6 merge_bin -o ../c6-hosted-2.12.13-full.bin \
  --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin \
  0xd000 ota_data_initial.bin 0x10000 network_adapter.bin
```

Before replacing the running P4 application, copy that merged image to
`/sd/update/c6-hosted-2.12.13-full.bin` using DevLink or an SD-card reader.
The merged image contains the C6 bootloader, partition table, initial OTA data
and ESP-Hosted application. An app-only OTA image cannot replace factory ESP-AT's
partition layout by itself.

## Build and run

```sh
cd tools/c6_sdio
idf.py -B ../../build/c6_sdio -DESP_SERIAL_FLASHER_DIR=/tmp/esp_serial_flasher build
python -m esptool --chip esp32p4 --port PORT --baud 921600 write_flash \
  0x20000 ../../build/c6_sdio/tdisplay_c6_sdio.bin
cd ../..
```

The utility loads the staged image into PSRAM, then shuts down the SD-card
controller. No SD-card reads or writes occur during C6 programming. SDIO slot 1
uses CLK18/CMD19/D0..D3=14..17 at 5 MHz. C6 EN uses XL9535 bit 12.
The screen is blank during this temporary application. Early logs use 115200;
the utility console switches to 921600 baud.

Hold C6 BOOT and start the console, keeping it held until `CONNECT OK` appears:

```sh
python tools/c6_sdio/console.py --port PORT --backup /path/outside/repo/c6-before-hosted-4mb.bin
```

Release BOOT after connection. The console requests a full 4 MB backup. The P4
checks it against the C6's MD5, and the host checks the received and saved data
before acknowledging it. Backups use private file permissions and existing files
are never overwritten. Wait for `HOST BACKUP ACKNOWLEDGED`, then enter:

```text
flash
```

Continue only after `FLASH VERIFIED error 0`. Enter `reset` with C6 BOOT released,
close the console, and restore the P4 launcher application at 0x20000. Finally
power-cycle the whole board. If a backup or flash fails, preserve the log and
backup files; do not acknowledge a failed backup.

The `backup-ok` command is intended for the validating host console. The utility
accepts it only after reading, checking and sending a complete backup in the same
session. The library's SDIO support is experimental; its pinned source is the
[Espressif SDIO flashing example](https://github.com/espressif/esp-serial-flasher/tree/ddf3cdd8fa87f3d26b42f1b8ad94bbbb67745f50/examples/esp32_sdio_example).
