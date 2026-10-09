#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* DM72D board (AT32F415CBT7).
 *   PA7  power-hold latch output (high = keep power on). Confirmed over SWD:
 *        driving it low turns the board off.
 *   PB2  S2 (PWR, the middle button; also fire), active low. Confirmed by sampling.
 *   PB7 (left), PB6 (right)  the other two tact switches, active low
 *   PB0/PB1  PCF8563 RTC, bit-banged I2C (rtc.c)
 *   PA5  battery voltage, ADC1 channel 5 (battery.c)
 *   PB5  low while charging (factory firmware; not used here)
 */
void board_clock_init(void);        /* 144 MHz from the 8 MHz HEXT crystal */
void board_gpio_init(void);         /* factory output state, buttons */
void board_power_hold(bool on);

uint32_t board_cycles(void);        /* DWT cycle counter */
void board_delay_us(uint32_t us);
void board_delay_ms(uint32_t ms);

bool board_s2_pressed(void);       /* S2 (PWR) = fire */
bool board_left_pressed(void);     /* PB7 */
bool board_right_pressed(void);    /* PB6 */

#endif
