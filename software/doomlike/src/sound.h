#ifndef SOUND_H
#define SOUND_H

#include <stdbool.h>

/* Sound effects on the PA4 passive buzzer. */
enum { SFX_SHOT, SFX_EMPTY, SFX_PAIN, SFX_KILL, SFX_HURT, SFX_DEATH,
       SFX_PICKUP, SFX_FIREBALL, SFX_BITE, SFX_CLEAR, SFX_COUNT };

void sound_init(void);
void sound_enable(bool on);
void sound_play(int sfx);
void sound_tick(void);          /* call at GAME_HZ */

#endif
