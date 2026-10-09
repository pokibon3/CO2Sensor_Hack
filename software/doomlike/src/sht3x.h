#ifndef SHT3X_H
#define SHT3X_H

#include <stdbool.h>
#include <stdint.h>

/* SHT3x temperature/humidity sensor (7-bit address 0x44) on a bit-banged
 * I2C bus: PB3 = SCL, PB4 = SDA. Runs in periodic mode, 1 measurement/s.
 */
bool sht3x_start(void);                        /* false: no answer */
bool sht3x_read(int16_t *t_c10, int16_t *rh_pc10);  /* latest measurement */

#endif
