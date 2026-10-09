#ifndef SOUND_H
#define SOUND_H

#include <stdbool.h>

/* Space-Invaders-style sound on the PA4 buzzer. The game has no sound
 * ports, so effects are triggered from game RAM changes once per frame.
 */
void sound_init(void);
void sound_frame(void);        /* call once per 60 Hz frame, after the CPU */
void sound_enable(bool on);

#endif
