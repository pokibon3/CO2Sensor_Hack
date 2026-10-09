/* Let the aliens descend without shooting and check the Nagoya-attack rules:
 * aliens may sit on the row just above the player (x = 40) without ending the
 * game, and no bomb is spawned below x = 40.
 */
#include <stdio.h>
#include "../src/machine.h"

#define RAM(a) machine_ram[(a) - 0x2000]

int main(void)
{
  int min_alien_x = 255, min_bomb_x = 255, frames_at_40 = 0, over_frame = -1;
  machine_reset();
  for(int f = 1; f < 60 * 600; f++)
  {
    machine_in1 = IN1_ALWAYS | ((f >= 10 && f <= 12) ? IN1_START : 0);
    RAM(0x2009) = 3;                         /* keep lives so the run continues */
    machine_run_frame();
    if(RAM(0x2004) == 3 && over_frame < 0) { over_frame = f; break; }
    if(RAM(0x2004) != 1) continue;
    int lowest = 255;
    for(int i = 0; i < 55; i++)
      if(RAM(0x2040 + i * 4) && RAM(0x2042 + i * 4) < lowest) lowest = RAM(0x2042 + i * 4);
    if(lowest < min_alien_x) min_alien_x = lowest;
    if(lowest == 40) frames_at_40++;
    for(int b = 0; b < 3; b++)
      if(RAM(0x2010 + b * 4) && RAM(0x2012 + b * 4) < min_bomb_x) min_bomb_x = RAM(0x2012 + b * 4);
  }
  printf("lowest alien x=%d, frames with aliens at x=40: %d, game over at frame %d, lowest bomb x=%d, invaded=%d\n",
         min_alien_x, frames_at_40, over_frame, min_bomb_x, RAM(0x202F));
  return 0;
}
