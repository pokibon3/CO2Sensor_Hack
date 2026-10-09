#ifndef RTC_H
#define RTC_H

#include <stdbool.h>
#include "game.h"

/* PCF8563 (7-bit address 0x51) on a bit-banged I2C bus: PB0 = SCL,
 * PB1 = SDA. Years are 2000 + the 2-digit BCD year.
 */
bool rtc_read(datetime_t *t);         /* false: no answer or VL (time lost) */
bool rtc_write(const datetime_t *t);  /* also clears VL */

#endif
