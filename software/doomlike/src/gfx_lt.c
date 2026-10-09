#include "gfx.h"
#include "lt7680.h"

/* 0: screen top at panel x = 0 (as ALIEN RAID), 1: rotated by 180 degrees. */
#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 0
#endif

/* Two 480 x 272 x 16 bpp canvases in the LT7680B SDRAM. */
static const uint32_t buf_addr[2] = {0x000000u, 0x080000u};
static uint8_t back;
uint32_t gfx_rects;
static uint32_t rects;

void gfx_init(void)
{
  lt_gfx_init();
  for(uint32_t i = 0; i < 2u; i++)
  {
    lt_set_canvas(buf_addr[i]);
    lt_rect(0u, 0u, LT_PANEL_W - 1u, LT_PANEL_H - 1u, 0u);
  }
  lt_set_display(buf_addr[0]);
  back = 1u;
  lt_set_canvas(buf_addr[back]);
}

void gfx_frame_begin(void)
{
  rects = 0u;
}

void gfx_frame_end(void)
{
  lt_set_display(buf_addr[back]);          /* waits for the last fill */
  back ^= 1u;
  lt_set_canvas(buf_addr[back]);
  gfx_rects = rects;
}

uint8_t gfx_back(void)
{
  return back;
}

void gfx_fill(int x, int y, int w, int h, uint16_t color)
{
  int x1 = x + w - 1;
  int y1 = y + h - 1;

  if(x < 0) x = 0;
  if(y < 0) y = 0;
  if(x1 >= SCREEN_W) x1 = SCREEN_W - 1;
  if(y1 >= SCREEN_H) y1 = SCREEN_H - 1;
  if(x > x1 || y > y1)
  {
    return;
  }
  rects++;
  /* portrait (x, y) -> panel (y, 271 - x) */
#if DISPLAY_ROTATION == 0
  lt_rect((uint16_t)y, (uint16_t)(SCREEN_W - 1 - x1), (uint16_t)y1, (uint16_t)(SCREEN_W - 1 - x), color);
#else
  lt_rect((uint16_t)(SCREEN_H - 1 - y1), (uint16_t)x, (uint16_t)(SCREEN_H - 1 - y), (uint16_t)x1, color);
#endif
}
