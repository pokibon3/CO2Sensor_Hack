#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* DM72C board (AT32F415CBT7).
 *   PA7  power-hold latch output (high = keep power on). Confirmed over SWD:
 *        driving it low turns the board off.
 *   PB2  PWR (power button, also "right"), active low. Confirmed by sampling.
 *   PB6, PB7  the other two tact switches, active low
 */
void board_clock_init(void);        /* 144 MHz from the 8 MHz HEXT crystal */
void board_gpio_init(void);         /* factory output state, buttons */
void board_power_hold(bool on);

uint32_t board_cycles(void);        /* DWT cycle counter */
void board_delay_us(uint32_t us);
void board_delay_ms(uint32_t ms);

bool board_pwr_pressed(void);      /* PWR = right */
bool board_left_pressed(void);
bool board_fire_pressed(void);

#endif
