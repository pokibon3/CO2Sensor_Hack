# DEMON GATE on the DM72D board

A DOOM-style raycaster for the AT32F415 + LT7680B board. The screen is portrait (272 x 480): the 3D view on top, a status panel with a minimap below. Everything is drawn with the LT7680B's hardware rectangle fill, so a frame is a few hundred register writes instead of a full-screen pixel transfer.

## Controls

| Button | Function |
|---|---|
| PB7 | Turn left |
| S2 (PB2) | Turn right. Hold 1 s to power on, hold 5 s alone to power off |
| PB7 + S2 | Walk forward |
| PB6 | Fire / start |

Three levels. Imps throw fireballs, brutes bite at close range. Health and ammo boxes lie around. Walk into the green exit switch to finish a level.

## Rendering

- Walls: one fixed-point DDA ray per 2-pixel column, one filled rectangle per column. Brightness falls off with distance in steps of 1/16 so colours stay steady; side walls are darker and block edges are shaded as seams. Tech walls get a light band, the exit switch a yellow plate.
- Floor and ceiling: horizontal bands of equal brightness (about 30 rectangles).
- Sprites: pixel art from `src/art.c`, scaled per texel column and clipped against the wall depth of each column. Each vertical run of one colour is one rectangle.
- Double buffering: two canvases in the LT7680B SDRAM (0x000000 and 0x080000). The finished canvas becomes the main window (REG20-23); the next frame is drawn into the other (REG50-53).
- The status panel is redrawn per buffer only when a value changes.

LT7680B geometry engine: REG68-6F start/end point, REGD2-D4 colour, REG76 = 0xE0 (start, fill, rectangle). The driver keeps a copy of these registers and skips writes that would not change them, and waits for STSR bit 3 (core busy) before each fill.

## Structure

| File | Role |
|---|---|
| `src/game.c` | Game logic at 30 Hz: player movement and wall sliding, hitscan, monster AI, fireballs, pickups, level flow |
| `src/render.c` | Raycasting, floor/ceiling bands, sprites, weapon, status panel, title and win pages |
| `src/levels.c` | The three 20 x 20 maps |
| `src/art.c` | Sprite art and palette |
| `src/font.c` | 5 x 7 font drawn as rectangles |
| `src/gfx.h`, `src/gfx_lt.c` | Portrait rectangle API and double buffering on the LT7680B |
| `src/lt7680.c` | Driver from `../invaders`, plus hardware rectangle fill and buffer switching |
| `src/sound.c` | Buzzer effects on PA4 (TMR3 48 kHz, as in ALIEN RAID) |
| `src/board.c` | 144 MHz clock, PA7 power latch, buttons (from `../invaders`) |
| `src/main.c` | Power on/off, main loop, diagnostics (`g_diag`) |

## Host simulator

`tools/host_sim.c` builds the game with a software framebuffer, plays a scripted input sequence and writes frames as PPM. It also checks the map and art row lengths.

```sh
cc -O2 -Isrc -o host_sim tools/host_sim.c src/game.c src/render.c src/font.c src/levels.c src/art.c -lm
./host_sim OUTDIR        # walk through level 1
./host_sim OUTDIR 1      # stand in front of an imp and shoot it
python3 tools/ppm2png.py OUTDIR/*.ppm
```

A frame needs about 220-320 rectangles in play and up to about 630 on the title page.

## Flashing

Build with `pio run`, then flash while halted (see `../invaders/README.md`):

```sh
python3 ../../tools/openocd.py \
  -f interface/cmsis-dap.cfg -f target/at32f415xx.cfg -c "adapter speed 1000" \
  -c "init; halt; cortex_m maskisr on; mww 0xE0042004 0x300; mww 0xE000E010 0; mww 0xE000E180 0xFFFFFFFF; mww 0xE000E280 0xFFFFFFFF; flash write_image erase .pio/build/at32f415cbt7/firmware.elf; verify_image .pio/build/at32f415cbt7/firmware.elf; cortex_m maskisr auto; reset run; exit"
```

## Status (2026-10-09)

On hardware: title page, 3D view, status panel, controls and sound work, and movement looked smooth. The frame rate has not been measured: while the game runs, SWD memory reads fail (the CPU can be halted, but `mdw &g_diag` returns nothing), as with the sflash firmware when S2 is not held. See `../../tools/README.md`.
