#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>

#define GAME_HZ 30

typedef struct
{
  bool left;      /* PB7 */
  bool right;     /* S2 */
  bool fire;      /* PB6 */
} input_t;

void game_init(uint32_t seed);
void game_tick(const input_t *in);   /* one 1/GAME_HZ step */
void game_render(void);

#endif
