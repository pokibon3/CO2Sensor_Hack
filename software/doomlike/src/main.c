#include "at32f415.h"
#include "board.h"
#include "game.h"
#include "gfx.h"
#include "lt7680.h"
#include "sound.h"

/* DEMON GATE: a DOOM-style raycaster drawn with LT7680B rectangle fills.
 * Controls: PB7 = turn left, S2 (PB2) = turn right, both = forward,
 * PB6 = fire. S2 long press = power on (1 s) / off (5 s, alone).
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

static volatile uint32_t tick_count;

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
    in.right = board_s2_pressed();
    in.fire = board_fire_pressed();
    if(s2_locked)
    {
      s2_locked = in.right;
      in.right = false;
    }

    s2_alone = (in.right && !in.left && !in.fire) ? s2_alone + pending : 0u;
    if(s2_alone >= POWER_OFF_TICKS)
    {
      power_off(true);
    }

    while(pending-- != 0u)
    {
      game_tick(&in);
      sound_tick();
      g_diag.ticks++;
    }

    t0 = board_cycles();
    game_render();
    g_diag.render_us = (board_cycles() - t0) / (system_core_clock / 1000000u);
    if(g_diag.render_us > g_diag.render_us_max)
    {
      g_diag.render_us_max = g_diag.render_us;
    }
    g_diag.rects = gfx_rects;
    g_diag.busy_polls = lt_busy_polls;
    g_diag.frames++;
  }
}
