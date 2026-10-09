#ifndef SETUP_H
#define SETUP_H

#include <stdbool.h>
#include "game.h"

/* SET UP page for the clock, laid out like the factory firmware's.
 * LEFT = -1, RIGHT = +1 (both repeat while held), FIRE (PWR) = next
 * field; after the last field it returns (writing the clock if anything was
 * changed).
 */
void setup_enter(void);
bool setup_tick(const input_t *in, bool fire_edge);  /* true when done */
void setup_draw(void);

/* The date, time and battery strip at the top of the title page. */
#define STATUS_H 28
void status_bar_draw(void);

/* CO2, health, temperature and humidity at the bottom of the title page:
 * two 16-line rows with 4 lines above, between and below them.
 */
#define SENSOR_H 44
void sensor_bar_draw(void);

#endif
