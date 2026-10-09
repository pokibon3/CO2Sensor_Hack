# ALIEN RAID on the DM72D board

Runs the invaders9k game (8080 code) on the AT32F415 with an 8080 emulator. The game is shown in portrait on the LT7680B-driven 480 x 272 panel, with per-object colors, anti-aliased scaling and Space-Invaders-style sound.

## Game ROM changes (`rom/`)

`rom/game.asm`, `rom/gen_gfx.py` and `rom/asm8080.py` are copies from `~/src/Gowin/invaders9k`. The original binary is kept as `rom/game_orig.bin`. Changes:

- Bold 7x7 arcade-style font (`FONT7` in `gen_gfx.py`).
- Nagoya attack: aliens may sit on the row just above the player. The game ends only when they reach the player row. That row does not drop bombs, as on the arcade original.

Rebuild:

```sh
cd rom && python3 gen_gfx.py && python3 asm8080.py game.asm -o game.bin --sym game.sym --lst game.lst
cd .. && python3 tools/bin2c.py rom/game.bin src/game_rom.c
```

## Controls

| Button | Function |
|---|---|
| PB7 | Left |
| S2 (PB2) | Right. Hold 1 s to power on, hold 5 s to power off |
| PB6 | Fire / start |

At 5 s the display blanks and the sound stops, and the PA7 latch is released. The board loses power when S2 is released: while S2 is held, the button itself keeps the supply on. The backlight cannot be switched off from the LT7680B. PWM1 had no effect on it. After the power-on hold, S2 is ignored as "right" until it is released.

## Structure

| File | Role |
|---|---|
| `src/i8080.c` | 8080 core, ported from `invaders9k/tools/emu8080.py` (same flags, cycles, EI delay, interrupt acceptance) |
| `src/machine.c` | Machine model: ROM/RAM/VRAM map, barrel shifter, RST 1/RST 2 at mid/end of each 33333-cycle frame |
| `src/game_rom.c` | Generated from `rom/game.bin` by `tools/bin2c.py` |
| `src/video.c` | Portrait rendering (272 x 358, arcade-like pixel aspect) with box-filter anti-aliasing in RGB565. Colors come from the game state (alien type, UFO, shots, bombs, explosions, text rows). Redraws VRAM cells whose pixels or color changed |
| `src/sound.c` | Passive buzzer on PA4: 48 kHz TMR3 interrupt, 4 voices OR-ed as pulse-width-controlled squares and gated LFSR noise. Fleet march (4 bass notes), UFO siren, shot, invader hit, UFO hit, player explosion, triggered from game RAM |
| `src/lt7680.c` | LT7680B driver: PA8 8 MHz XI clock, PLL/SDRAM/panel init, 16 bpp canvas. SPI mode and write speed are chosen by a self-test |
| `src/board.c` | 144 MHz clock, factory GPIO state, PA7 power latch, buttons |

`DISPLAY_ROTATION` in `video.c` rotates the picture by 180 degrees.

## Verification

- `tools/host_test.c` vs `tools/ref_run.py` (reference Python model): the first 300,000 instruction traces are identical, and RAM+VRAM are identical after 1,200, 4,000 and 12,000 frames with a scripted input.
- `tools/host_video.c`: after 400, 3,000 and 6,000 frames, incremental updates give the same panel image as a full redraw.

```sh
cc -O2 -DMACHINE_TRACE -o host_test tools/host_test.c src/machine.c src/i8080.c src/game_rom.c
cc -O2 -o host_video tools/host_video.c src/video.c src/machine.c src/i8080.c src/game_rom.c
```

- `tools/host_nagoya.c`: aliens stay on the row above the player for 632 frames before landing. With the original ROM, the game ends when they reach that row.

On hardware: SPI mode 0 at 18 MHz with burst writes. Emulation takes at most 6 ms per frame. Screen updates take a few ms, up to about 140 ms on full-screen clears.

## Serial flash (W25Q32) tool

The W25Q32 (JEDEC ID EF4016, 4 MB) sits on the LT7680B SPI master, chip select nSS1. It has no write protection (SR1 = 00, SR2 = 02 with QE = 1). The `sflash` environment is a small firmware that drives it through a RAM mailbox over SWD:

```sh
pio run -e sflash          # then flash .pio/build/sflash/firmware.elf as below
python3 tools/sflash.py id
python3 tools/sflash.py read  out.bin [ADDR] [LEN]   # about 27 KB/s; 4 MB in 151 s
python3 tools/sflash.py write in.bin ADDR            # erase 4 KB sectors, program, verify
python3 tools/sflash.py erase4k ADDR
```

Back up the factory contents with `../../tools/backup_flash.py` first. The dump is not included in the repository (SHA256 of the author's unit: `5b130150…d705`). Only 0x000000-0x10FFFF is used, and the rest (about 2.9 MB) is blank. Write/erase was tested on the last sector (0x3FF000), which was then erased back to blank.

## Flashing

Flash while halted (an MCU reset drops the power latch). See `../colorbar_sample/README.md` for wiring. Flash this project's ELF with the OpenOCD wrapper `../../tools/openocd.py` (see `../../tools/openocd_setup.md`):

```sh
python3 ../../tools/openocd.py \
  -f interface/cmsis-dap.cfg -f target/at32f415xx.cfg -c "adapter speed 1000" \
  -c "init; halt; cortex_m maskisr on; mww 0xE0042004 0x300; mww 0xE000E010 0; mww 0xE000E180 0xFFFFFFFF; mww 0xE000E280 0xFFFFFFFF; flash write_image erase .pio/build/at32f415cbt7/firmware.elf; verify_image .pio/build/at32f415cbt7/firmware.elf; cortex_m maskisr auto; reset run; exit"
```

Interrupts (SysTick, the 48 kHz TMR3 sound interrupt) must be disabled before flashing. Otherwise an interrupt during the flash algorithm jumps into the erased vector table and the write times out.

`g_diag` (`mdw &g_diag 9`) reports stage, frame count, worst-case emulation/video time, late frames and the LT7680B SPI settings.
