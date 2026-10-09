/* Host-side check of the 8080 core + machine model against the reference
 * Python model (invaders9k/tools/emu8080.py).
 *   host_test FRAMES TRACE_LINES trace.txt ram.bin
 * Inputs follow tools/input_script(): same schedule as tools/ref_run.py.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/machine.h"

static FILE *trace_file;
static long trace_left;

void machine_trace_hook(const i8080_t *c)
{
  if(trace_left <= 0)
  {
    return;
  }
  trace_left--;
  fprintf(trace_file, "PC=%04X A=%02X B=%02X C=%02X D=%02X E=%02X H=%02X L=%02X SP=%04X F=%02X\n",
          c->pc, c->r[7], c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->sp, i8080_psw(c));
}

static uint8_t input_script(int f)
{
  uint8_t in = IN1_ALWAYS;
  if(f >= 10 && f <= 12) in |= IN1_START;
  if((f / 7) % 3 == 0) in |= IN1_FIRE;
  if((f / 90) % 2 == 0) in |= IN1_LEFT; else in |= IN1_RIGHT;
  if((f / 1500) % 2 == 1 && f % 600 < 20) in |= IN1_START;
  return in;
}

int main(int argc, char **argv)
{
  int frames = atoi(argv[1]);
  trace_left = atol(argv[2]);
  trace_file = fopen(argv[3], "w");
  machine_reset();
  for(int f = 1; f <= frames; f++)
  {
    machine_in1 = input_script(f);
    machine_run_frame();
  }
  fclose(trace_file);
  FILE *o = fopen(argv[4], "wb");
  fwrite(machine_ram, 1, MACHINE_RAM, o);
  fclose(o);
  printf("pc=%04X\n", machine_cpu.pc);
  return 0;
}
