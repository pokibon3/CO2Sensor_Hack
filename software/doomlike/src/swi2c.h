#ifndef SWI2C_H
#define SWI2C_H

#include <stdbool.h>
#include <stdint.h>
#include "at32f415.h"

/* Bit-banged I2C as the factory firmware does it: both lines driven
 * push-pull, SDA switched to an input with pull-up for ACKs and reads.
 */
typedef struct
{
  gpio_type *port;
  uint16_t scl, sda;
  uint32_t half_us;     /* half an SCL period */
} swi2c_t;

void swi2c_start(const swi2c_t *b);
void swi2c_stop(const swi2c_t *b);
bool swi2c_send(const swi2c_t *b, uint8_t v);     /* true = ACK */
uint8_t swi2c_recv(const swi2c_t *b, bool last);  /* NACK after the last */

#endif
