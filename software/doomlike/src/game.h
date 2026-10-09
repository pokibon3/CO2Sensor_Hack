#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>

#define GAME_HZ 30

typedef struct
{
  bool left;      /* PB7 */
  bool right;     /* PB6 */
  bool fire;      /* S2 (PWR) */
} input_t;

void game_init(uint32_t seed);
void game_tick(const input_t *in);   /* one 1/GAME_HZ step */
void game_render(void);
bool game_playing(void);

/* Shown in the status panel; set by the platform once per second.
 * perf_fps = frames drawn in the last second, perf_ms = slowest frame (ms).
 */
extern uint16_t perf_fps;
extern uint16_t perf_ms;

typedef struct
{
  uint16_t year;  /* 2000-2099 */
  uint8_t mon, day, hour, min, sec;
} datetime_t;

/* Shown at the top of the title page; set by the platform once per second.
 * clock_valid is false while the RTC has no valid time; batt_percent < 0
 * means unknown.
 */
extern datetime_t clock_now;
extern bool clock_valid;
extern int8_t batt_percent;

void platform_set_clock(const datetime_t *t);   /* from SET UP */

/* Room sensors, set by the platform when a reading arrives.
 * SENSOR_NONE = no reading yet.
 */
#define SENSOR_NONE (-32768)
extern int16_t co2_ppm;
extern int16_t temp_c10;      /* 0.1 degC */
extern int16_t humi_pc10;     /* 0.1 %RH */

/* Room comfort 0-100 from the sensors that have a reading (-1: none);
 * the game starts with 50 + comfort / 2 health (100 without sensors).
 */
int comfort_score(void);
int start_health(void);

#endif
