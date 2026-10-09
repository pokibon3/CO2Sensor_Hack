#include "machine.h"

#define FRAME_CYCLES 33333u     /* 2 MHz / 60 Hz */

uint8_t machine_ram[MACHINE_RAM];
i8080_t machine_cpu;
uint8_t machine_in1 = IN1_ALWAYS;

static uint16_t shift_reg;
static uint8_t shift_amount;
static int pending_irq = -1;

#ifdef MACHINE_TRACE
void machine_trace_hook(const i8080_t *cpu);
#endif

uint8_t i8080_mem_read(uint16_t addr)
{
  addr &= 0x3FFFu;
  if(addr < GAME_ROM_SIZE)
  {
    return game_rom[addr];
  }
  return machine_ram[addr - GAME_ROM_SIZE];
}

void i8080_mem_write(uint16_t addr, uint8_t value)
{
  addr &= 0x3FFFu;
  if(addr >= GAME_ROM_SIZE)
  {
    machine_ram[addr - GAME_ROM_SIZE] = value;
  }
}

uint8_t i8080_io_in(uint8_t port)
{
  switch(port)
  {
    case 1: return machine_in1;
    case 3: return (uint8_t)(shift_reg >> (8u - shift_amount));
    default: return 0u;
  }
}

void i8080_io_out(uint8_t port, uint8_t value)
{
  if(port == 2u)
  {
    shift_amount = value & 7u;
  }
  else if(port == 4u)
  {
    shift_reg = (uint16_t)((value << 8) | (shift_reg >> 8));
  }
}

void machine_reset(void)
{
  for(uint32_t i = 0; i < MACHINE_RAM; i++)
  {
    machine_ram[i] = 0u;
  }
  i8080_reset(&machine_cpu);
  shift_reg = 0u;
  shift_amount = 0u;
  pending_irq = -1;
}

/* The interrupt request is held until accepted, like the FPGA top. */
static void run_until(uint32_t cycles)
{
  i8080_t *cpu = &machine_cpu;

  while(cpu->cycles < cycles)
  {
    if(pending_irq >= 0 && i8080_interrupt(cpu, (uint8_t)pending_irq))
    {
      pending_irq = -1;
      continue;
    }
#ifdef MACHINE_TRACE
    if(!cpu->halted)
    {
      machine_trace_hook(cpu);
    }
#endif
    i8080_step(cpu);
  }
}

/* The cycle counter is rebased every frame (the overshoot carries over),
 * which is equivalent to the reference model's absolute frame boundaries.
 */
void machine_run_frame(void)
{
  run_until(FRAME_CYCLES / 2u);
  pending_irq = 0xCF;
  run_until(FRAME_CYCLES);
  pending_irq = 0xD7;
  machine_cpu.cycles -= FRAME_CYCLES;
}
