#ifndef CO2_H
#define CO2_H

#include <stdbool.h>
#include <stdint.h>

/* The factory CO2 module on a software UART, 9600 baud 8N1: PA0 = TX,
 * PA1 = RX. The request is 64 69 03 5E 4E (Modbus style, CRC16 at the
 * end); the reply is 14 bytes starting 64 69 03 01 with the ppm in bytes
 * 4-5, little endian. Bytes are received in the PA1 falling-edge
 * interrupt.
 */
void co2_init(void);
void co2_request(void);
bool co2_poll(int16_t *ppm);    /* true when a complete reply arrived */

extern volatile uint32_t co2_rx_bytes;   /* diagnostics */

#endif
