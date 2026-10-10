# AT32F415 + LT7680B LCD color-bar sample

Hardware bring-up program for the DM72C board. It configures the LT7680B and enables its built-in color-bar generator, so it does not depend on the W25Q32JV image contents. **Confirmed working on hardware: color bars are displayed.**

## MCU-to-LT7680B wiring (confirmed)

Read back over SWD from the factory firmware's live register state and confirmed by the working color bar:

| AT32F415 | Physical MCU pin | LT7680B / function |
|---|---:|---|
| PB12 | 25 | SCS# (GPIO, software CS) |
| PB13 | 26 | SCLK (SPI2) |
| PB14 | 27 | SDO → MCU MISO (SPI2) |
| PB15 | 28 | SDI ← MCU MOSI (SPI2) |
| PA8 | 29 | **XI clock, 8 MHz** — the LT7680B has no crystal of its own |

Without the PA8 clock the LT7680B does not respond at all (MISO stays low and every read returns 0x00).

SPI mode 0 reads back correctly at 500 kHz. The factory firmware uses mode 3 at 12 MHz; at low SCK, mode 3 shifts read data right by one bit.

## Board quirks

- **Power-hold latch.** PWR is the power button. The MCU must keep its outputs driven to keep the board powered. Any MCU reset turns the board off. `board_power_hold_init()` reproduces the factory firmware's output state (PA0/PA7/PA11/PA15/PB0/PB1/PB3/PB4 high, PA4/PC15 low) as the first action in `main()`. The exact hold pin has not been isolated.
- **Watchdog in factory firmware.** The factory firmware starts the WDT. A halted CPU therefore resets after a short time, which drops power and makes SWD disappear. Set `DEBUG_CTRL.WDT_PAUSE` (`mww 0xE0042004 0x300`) right after halting. This sample does not use the WDT.

## LCD / LT7680B settings (from factory firmware)

- HXX043LB0701, 480 x 272, RGB
- PLL with XI = 8 MHz, R = 2, OD = 3: PCLK N = 9 (4.5 MHz), MCLK N = CCLK N = 100 (50 MHz)
- Horizontal: non-display 39, sync start 8, sync width 4
- Vertical: non-display 8, sync start 8, sync width 4
- REG12: PCLK falling edge. REG13: HSYNC/VSYNC active low, DE active high
- SDRAM: REGE0 = 0x29, REGE1 = 0x03, refresh 779

## Build

```sh
pio run
```

## Upload

Flash `.pio/build/at32f415cbt7/firmware.elf` while halted, as in [`../../doc/flashing.md`](../../doc/flashing.md) (power, OpenOCD command; J1 wiring in [`../../doc/debugger_connection.md`](../../doc/debugger_connection.md)). Do not use `pio run -t upload`: the reset drops board power.

## Diagnostics

`g_diag` in RAM can be read over SWD (`mdw &g_diag 2`):

| stage | Meaning |
|---|---|
| 1 | SPI initialised |
| 2 | LT7680B status OK and register read-back OK |
| 3 | Color bar configured. Status/REG00/REG01/REG12 are captured in the next word |
| 0xE1 | LT7680B never left power-saving state |
| 0xE2 | Register write/read-back mismatch (check the PA8 clock and the SPI mode) |

Known-good capture: `00000003 e0908054`, i.e. status 0x54, REG00 0x80, REG01 0x90, REG12 0xE0.

## Factory firmware

Back up the factory firmware with `../../tools/backup_flash.py mcu` before flashing (128 KB; FAP was disabled on the author's unit). The dump is not included in the repository. To restore it, write `factory_firmware.bin` at 0x08000000 using the same halted-flash procedure ([`../../doc/flash_backup.md`](../../doc/flash_backup.md) restores it automatically with `all`).

## Sources

- Levetop LT768x datasheet V4.2
- Levetop LT768x Arduino reference library (official download)
- ArteryTek AT32F415 firmware library
- Disassembly of the factory firmware backup
