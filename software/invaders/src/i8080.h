#ifndef I8080_H
#define I8080_H

#include <stdint.h>

/* Intel 8080 core. Semantics (flags, cycle counts, EI delay, interrupt
 * acceptance) follow invaders9k/tools/emu8080.py, the reference model the
 * game was developed and verified against.
 */
typedef struct
{
  uint8_t r[8];               /* B C D E H L (unused) A */
  uint16_t sp;
  uint16_t pc;
  uint8_t s, z, ac, p, cy;
  uint8_t inte;
  uint8_t ei_pending;
  uint8_t halted;
  uint32_t cycles;
} i8080_t;

/* Machine callbacks, provided by the user of the core. */
uint8_t i8080_mem_read(uint16_t addr);
void i8080_mem_write(uint16_t addr, uint8_t value);
uint8_t i8080_io_in(uint8_t port);
void i8080_io_out(uint8_t port, uint8_t value);

void i8080_reset(i8080_t *cpu);
void i8080_step(i8080_t *cpu);
/* Returns 1 if the interrupt (an RST opcode) was accepted. */
int i8080_interrupt(i8080_t *cpu, uint8_t opcode);
uint8_t i8080_psw(const i8080_t *cpu);

#endif
