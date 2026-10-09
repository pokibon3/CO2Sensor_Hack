#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

/* Battery voltage on PA5 (ADC1 channel 5). The factory firmware compares
 * the raw 12-bit value only: below 2200 is its lowest icon level, 2400 and
 * above is full, and below 2100 it switches off.
 */
void battery_init(void);
uint16_t battery_raw(void);            /* average of several conversions */
int8_t battery_percent(uint16_t raw);  /* smoothed, 0-100 */

#endif
