#include "at32f415.h"
#include "board.h"
#include "lt7680.h"
#include "machine.h"
#include "sound.h"
#include "video.h"

/* ALIEN RAID (invaders9k/sw/game.bin) running on an 8080 emulator.
 * Controls: left = PB7, right = PWR (PB2), fire/start = PB6.
 * PWR long press = power on (1 s) / off (5 s).
 */

#define FRAME_HZ          60u
#define POWER_ON_HOLD_MS  1000u
#define POWER_OFF_FRAMES  (FRAME_HZ * 5u)          /* 5 s */

/* Readable over SWD: mdw &g_diag 12 */
volatile struct
{
  uint32_t stage;           /* 1 clock, 2 LT ok, 3 running, 0xE1 LT failed */
  uint32_t frames;
  uint32_t emu_us_max;
  uint32_t video_us_max;
  uint32_t video_px_max;
  uint32_t late_frames;     /* frames that missed the 60 Hz deadline */
  lt_info_t lt;
  uint32_t in1;
} g_diag;

static volatile uint32_t frame_tick;

void SysTick_Handler(void)
{
  frame_tick++;
}

static uint32_t cycles_to_us(uint32_t cycles)
{
  return cycles / (system_core_clock / 1000000u);
}

/* Release the power latch. While PWR is held the button itself keeps the
 * supply up, so blank the display and silence the buzzer at once; the board
 * loses power when PWR is released. If it stays powered (external supply),
 * wait for another long press and restart.
 */
static void power_off(bool display_ready)
{
  uint32_t held = 0u;

  sound_enable(false);
  if(display_ready)
  {
    lt_display_on(false);
  }
  board_power_hold(false);
  while(board_pwr_pressed())
  {
  }
  board_delay_ms(500u);

  for(;;)
  {
    held = board_pwr_pressed() ? held + 10u : 0u;
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
  if(!board_pwr_pressed())
  {
    /* Not started by the button (debugger reset, external supply). */
    board_power_hold(true);
    board_clock_init();
    return;
  }

  board_clock_init();
  while(board_pwr_pressed() && held < POWER_ON_HOLD_MS)
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
  uint32_t last_tick;
  uint32_t pwr_frames = 0u;
  bool pwr_locked;

  power_on();
  g_diag.stage = 1u;

  if(!lt_init())
  {
    g_diag.stage = 0xE1u;
    for(;;)
    {
      if(board_pwr_pressed())
      {
        power_off(false);
      }
    }
  }
  g_diag.lt = lt_info;
  g_diag.stage = 2u;

  video_init();
  machine_reset();
  video_full_redraw();
  lt_display_on(true);
  sound_init();
  sound_enable(true);

  /* Ignore the power-on press until PWR is released. */
  pwr_locked = board_pwr_pressed();

  SysTick_Config(system_core_clock / FRAME_HZ);
  last_tick = frame_tick;
  g_diag.stage = 3u;

  for(;;)
  {
    uint32_t t0, t1, t2, px;
    uint8_t in1 = IN1_ALWAYS;
    bool pwr;

    while(frame_tick == last_tick)
    {
      __WFI();
    }
    if(frame_tick - last_tick > 1u)
    {
      g_diag.late_frames++;
    }
    last_tick = frame_tick;

    pwr = board_pwr_pressed();
    if(!pwr)
    {
      pwr_locked = false;
    }
    pwr_frames = pwr ? pwr_frames + 1u : 0u;
    if(pwr_frames >= POWER_OFF_FRAMES && !pwr_locked)
    {
      power_off(true);
    }

    if(pwr && !pwr_locked)
    {
      in1 |= IN1_RIGHT;
    }
    if(board_left_pressed())
    {
      in1 |= IN1_LEFT;
    }
    if(board_fire_pressed())
    {
      in1 |= IN1_FIRE | IN1_START;
    }
    machine_in1 = in1;
    g_diag.in1 = in1;

    t0 = board_cycles();
    machine_run_frame();
    sound_frame();
    t1 = board_cycles();
    px = video_update();
    t2 = board_cycles();

    g_diag.frames++;
    if(cycles_to_us(t1 - t0) > g_diag.emu_us_max)
    {
      g_diag.emu_us_max = cycles_to_us(t1 - t0);
    }
    if(cycles_to_us(t2 - t1) > g_diag.video_us_max)
    {
      g_diag.video_us_max = cycles_to_us(t2 - t1);
    }
    if(px > g_diag.video_px_max)
    {
      g_diag.video_px_max = px;
    }
  }
}
