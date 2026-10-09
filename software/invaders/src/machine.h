#ifndef MACHINE_H
#define MACHINE_H

#include <stdint.h>
#include "i8080.h"

/* Space-Invaders-compatible machine, same as invaders9k (FPGA top and
 * tools/emu8080.py):
 *   0000-1FFF ROM, 2000-23FF work RAM, 2400-3FFF video RAM (mirrors every 4000h)
 *   IN 1 controls, IN 3 + OUT 2/4 barrel shifter
 *   RST 1 (CFh) at mid-frame, RST 2 (D7h) at end of frame, 2 MHz / 60 Hz
 */
#define GAME_ROM_SIZE   0x2000u
#define MACHINE_RAM     0x2000u
#define VRAM_OFFSET     0x0400u   /* VRAM inside machine_ram */
#define VRAM_SIZE       0x1C00u   /* 224 columns x 32 bytes */
#define SCREEN_W        224u
#define SCREEN_H        256u

/* IN 1 bits */
#define IN1_ALWAYS      0x08u
#define IN1_START       0x04u
#define IN1_FIRE        0x10u
#define IN1_LEFT        0x20u
#define IN1_RIGHT       0x40u

extern const uint8_t game_rom[GAME_ROM_SIZE];
extern uint8_t machine_ram[MACHINE_RAM];
extern i8080_t machine_cpu;
extern uint8_t machine_in1;

void machine_reset(void);
void machine_run_frame(void);

static inline const uint8_t *machine_vram(void)
{
  return &machine_ram[VRAM_OFFSET];
}

#endif
