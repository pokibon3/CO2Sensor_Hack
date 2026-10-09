#include "at32f415.h"
#include "battery.h"
#include "board.h"
#include "co2.h"
#include "game.h"
#include "gfx.h"
#include "lt7680.h"
#include "rtc.h"
#include "sht3x.h"
#include "sound.h"

/* DEMON GATE: a DOOM-style raycaster drawn with LT7680B rectangle fills.
 * Controls: PB7 = turn left, PB6 = turn right, both = forward,
 * S2 (PB2, PWR) = fire. S2 long press = power on (1 s) / off (5 s, alone,
 * not while playing, where fire is held).
 */

#define POWER_ON_HOLD_MS  1000u
#define POWER_OFF_TICKS   (GAME_HZ * 5u)

/* Readable over SWD: mdw &g_diag 10 */
volatile struct
{
  uint32_t stage;           /* 1 clock, 2 LT ok, 3 running, 0xE1 LT failed */
  uint32_t frames;
  uint32_t render_us;       /* last frame */
  uint32_t render_us_max;
  uint32_t rects;           /* rectangles in the last frame */
  uint32_t ticks;           /* game ticks */
  uint32_t dropped;         /* ticks skipped because rendering was late */
  uint32_t busy_polls;
  lt_info_t lt;
} g_diag;
/* co2_rx_bytes (co2.c) counts bytes from the CO2 module. */

static volatile uint32_t tick_count;

void platform_set_clock(const datetime_t *t)
{
  rtc_write(t);
}

#define CO2_WARMUP_S  20u   /* the factory firmware ignores the first 20 s */
#define SENSOR_STALE  5u    /* seconds without a reading -> shown as ---- */

static uint32_t uptime_s;
static uint32_t co2_age = SENSOR_STALE, sht_age = SENSOR_STALE;

/* Once a second: read the SHT3x and ask the CO2 module for a reading.
 * The board warms the SHT3x up; the factory firmware subtracts 1-4 degC and
 * adds 2-8 %RH depending on the time since power on, and so do we.
 */
static void read_sensors(void)
{
  int16_t t, h;
  int16_t heat = uptime_s < 120u ? 10 : uptime_s < 320u ? 20 : uptime_s < 600u ? 30 : 40;

  uptime_s++;
  if(sht3x_read(&t, &h))
  {
    temp_c10 = (int16_t)(t - heat);
    h = (int16_t)(h + 2 * heat);
    humi_pc10 = h > 999 ? 999 : h;
    sht_age = 0u;
  }
  else if(++sht_age >= SENSOR_STALE)
  {
    temp_c10 = humi_pc10 = SENSOR_NONE;
    sht_age = SENSOR_STALE;
    sht3x_start();     /* in case it was plugged in or reset */
  }

  if(++co2_age >= SENSOR_STALE)
  {
    co2_ppm = SENSOR_NONE;
    co2_age = SENSOR_STALE;
  }
  co2_request();
}

static void poll_co2(void)
{
  int16_t ppm;

  if(co2_poll(&ppm) && uptime_s >= CO2_WARMUP_S)
  {
    co2_ppm = ppm;
    co2_age = 0u;
  }
}

/* Clock for the title page, several times a second so the seconds step
 * evenly.
 */
static void read_clock(void)
{
  datetime_t t;

  clock_valid = rtc_read(&t);
  if(clock_valid)
  {
    clock_now = t;
  }
}

void SysTick_Handler(void)
{
  tick_count++;
}

static void power_off(bool display_ready)
{
  uint32_t held = 0u;

  sound_enable(false);
  if(display_ready)
  {
    lt_display_on(false);
  }
  board_power_hold(false);
  while(board_s2_pressed())
  {
  }
  board_delay_ms(500u);

  for(;;)
  {
    held = board_s2_pressed() ? held + 10u : 0u;
    if(held >= POWER_ON_HOLD_MS)
    {
      NVIC_SystemReset();
    }
    board_delay_ms(10u);
  }
}

static void power_on(void)
{
  uint32_t held = 0u;

  board_gpio_init();
  if(!board_s2_pressed())
  {
    /* Not started by the button (debugger reset, external supply). */
    board_power_hold(true);
    board_clock_init();
    return;
  }

  board_clock_init();
  while(board_s2_pressed() && held < POWER_ON_HOLD_MS)
  {
    board_delay_ms(10u);
    held += 10u;
  }
  if(held < POWER_ON_HOLD_MS)
  {
    power_off(false);
  }
  board_power_hold(true);
}

int main(void)
{
  uint32_t done = 0u;
  uint32_t s2_alone = 0u;
  uint32_t sec_ticks = 0u, sec_frames = 0u, sec_ms = 0u;
  uint32_t clock_ticks = 0u;
  bool s2_locked;

  power_on();
  g_diag.stage = 1u;

  if(!lt_init())
  {
    g_diag.stage = 0xE1u;
    for(;;)
    {
      if(board_s2_pressed())
      {
        power_off(false);
      }
    }
  }
  g_diag.lt = lt_info;
  g_diag.stage = 2u;

  battery_init();
  read_clock();
  batt_percent = battery_percent(battery_raw());
  sht3x_start();
  co2_init();
  gfx_init();
  game_init(board_cycles());
  game_render();
  lt_display_on(true);
  sound_init();
  sound_enable(true);

  /* Ignore the power-on press until S2 is released. */
  s2_locked = board_s2_pressed();

  SysTick_Config(system_core_clock / GAME_HZ);
  done = tick_count;
  g_diag.stage = 3u;

  for(;;)
  {
    uint32_t now = tick_count;
    uint32_t pending = now - done;
    uint32_t t0;
    input_t in;

    if(pending == 0u)
    {
      continue;
    }
    if(pending > 2u)
    {
      g_diag.dropped += pending - 2u;
      pending = 2u;
    }
    done = now;

    in.left = board_left_pressed();
    in.right = board_right_pressed();
    in.fire = board_s2_pressed();
    if(s2_locked)
    {
      s2_locked = in.fire;
      in.fire = false;
    }

    s2_alone = (in.fire && !in.left && !in.right && !game_playing()) ? s2_alone + pending : 0u;
    if(s2_alone >= POWER_OFF_TICKS)
    {
      power_off(true);
    }

    sec_ticks += pending;
    clock_ticks += pending;
    while(pending-- != 0u)
    {
      game_tick(&in);
      sound_tick();
      g_diag.ticks++;
    }
    if(sec_ticks >= GAME_HZ)
    {
      perf_fps = (uint16_t)sec_frames;
      perf_ms = (uint16_t)sec_ms;
      sec_ticks -= GAME_HZ;
      sec_frames = 0u;
      sec_ms = 0u;
      batt_percent = battery_percent(battery_raw());
      read_sensors();
    }
    poll_co2();
    if(clock_ticks >= GAME_HZ / 5u)
    {
      clock_ticks = 0u;
      read_clock();
    }

    t0 = board_cycles();
    game_render();
    g_diag.render_us = (board_cycles() - t0) / (system_core_clock / 1000000u);
    if(g_diag.render_us > g_diag.render_us_max)
    {
      g_diag.render_us_max = g_diag.render_us;
    }
    sec_frames++;
    if((g_diag.render_us + 999u) / 1000u > sec_ms)
    {
      sec_ms = (g_diag.render_us + 999u) / 1000u;
    }
    g_diag.rects = gfx_rects;
    g_diag.busy_polls = lt_busy_polls;
    g_diag.frames++;
  }
}
