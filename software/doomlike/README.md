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

- Walls: one fixed-point DDA ray per 2-pixel column. Each wall type has a 16 x 16 texture (`src/textures.c`); a column is drawn as one rectangle per vertical run of equal texels. The patterns are made of horizontal joints, and shades change only where a joint already splits a column, so a column needs about 3-7 rectangles. Walls lower than 32 px (texels under 2 px) are drawn as one flat rectangle. Brightness falls off with distance in steps of 1/16 so colours stay steady; side walls are darker.
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
| `src/textures.c` | 16 x 16 wall textures (stone, brick, tech, wood, exit switch) |
| `src/font.c` | 5 x 7 font drawn as rectangles |
| `src/gfx.h`, `src/gfx_lt.c` | Portrait rectangle API and double buffering on the LT7680B |
| `src/lt7680.c` | Driver from `../invaders`, plus hardware rectangle fill and buffer switching |
| `src/sound.c` | Buzzer effects on PA4 (TMR3 48 kHz, as in ALIEN RAID) |
| `src/board.c` | 144 MHz clock, PA7 power latch, buttons (from `../invaders`) |
| `src/main.c` | Power on/off, main loop, diagnostics (`g_diag`) |

## Host simulator

`tools/host_sim.c` builds the game with a software framebuffer, plays a scripted input sequence and writes frames as PPM. It also checks the map, texture and art row lengths.

```sh
cc -O2 -Isrc -o host_sim tools/host_sim.c src/game.c src/render.c src/font.c src/levels.c src/art.c src/textures.c -lm
./host_sim OUTDIR        # walk through level 1
./host_sim OUTDIR 1      # stand in front of an imp and shoot it
python3 tools/ppm2png.py OUTDIR/*.ppm
```

A frame needs about 220-700 rectangles in play (most when a near wall fills the view) and about 570 on the title page.

## Flashing

Build with `pio run`, then flash while halted (see `../invaders/README.md`):

```sh
python3 ../../tools/openocd.py \
  -f interface/cmsis-dap.cfg -f target/at32f415xx.cfg -c "adapter speed 1000" \
  -c "init; halt; cortex_m maskisr on; mww 0xE0042004 0x300; mww 0xE000E010 0; mww 0xE000E180 0xFFFFFFFF; mww 0xE000E280 0xFFFFFFFF; flash write_image erase .pio/build/at32f415cbt7/firmware.elf; verify_image .pio/build/at32f415cbt7/firmware.elf; cortex_m maskisr auto; reset run; exit"
```

## Frame rate

The status panel shows `nnFPS nnMS` under the minimap, updated once per second: frames drawn in the last second (at most 30, one per game tick) and the slowest frame's draw time in ms. SWD memory reads fail while the game runs (see below), so this display is the way to measure it.

## Status (2026-10-09)

On hardware: title page, 3D view, textured walls, status panel, controls and sound work. With flat walls a frame took at most 15 ms; with textured walls at most 20 ms (close to a wall). The game keeps 30 fps.

While the game runs, SWD fails to reach the core (the probe sees the DP, but memory access fails), as with the sflash firmware. Holding S2 makes it work: flash while S2 is held. The firmware stops counting the 5 s power-off press once it is halted.
