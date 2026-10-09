/* Host check of video.c: incremental updates must equal a full redraw.
 *   host_video FRAMES out.ppm   (portrait view of the emulated panel)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/lt7680.h"
#include "../src/machine.h"
#include "../src/video.h"

lt_info_t lt_info;
static uint16_t panel[LT_PANEL_H][LT_PANEL_W];
static int lowbyte = -1;
static uint32_t wx, wy, ww, wh, pos;

void lt_write_begin(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  if(x + w > LT_PANEL_W || y + h > LT_PANEL_H) { printf("window out of range %u %u %u %u\n", x, y, w, h); exit(1); }
  wx = x; wy = y; ww = w; wh = h; pos = 0;
}
void lt_write_pixels(const uint8_t *px, uint32_t n)
{
  while(n--)
  {
    if(lowbyte < 0) { lowbyte = *px++; continue; }
    panel[wy + pos / ww][wx + pos % ww] = (uint16_t)(lowbyte | (*px++ << 8)); pos++; lowbyte = -1;
  }
}
void lt_write_end(void) { if(pos != ww * wh) { printf("short write %u/%u\n", pos, ww * wh); exit(1); } }

static uint8_t input_script(int f)
{
  uint8_t in = IN1_ALWAYS;
  if((f >= 10 && f <= 12) || (f > 2000 && f % 600 < 5)) in |= IN1_START;
  if((f / 7) % 3 == 0) in |= IN1_FIRE;
  if((f / 90) % 2 == 0) in |= IN1_LEFT; else in |= IN1_RIGHT;
  return in;
}

int main(int argc, char **argv)
{
  static uint16_t inc[LT_PANEL_H][LT_PANEL_W];
  int frames = atoi(argv[1]);
  uint32_t maxpx = 0, tot = 0;
  video_init();
  machine_reset();
  video_full_redraw();
  for(int f = 1; f <= frames; f++)
  {
    machine_in1 = input_script(f);
    machine_run_frame();
    uint32_t px = video_update();
    tot += px; if(px > maxpx) maxpx = px;
  }
  memcpy(inc, panel, sizeof(panel));
  memset(panel, 0, sizeof(panel));
  video_full_redraw();
  int diff = 0;
  for(int y = 0; y < (int)LT_PANEL_H; y++) for(int x = 0; x < (int)LT_PANEL_W; x++) diff += inc[y][x] != panel[y][x];
  printf("incremental vs full redraw: %d differing pixels; max %u px/frame, avg %u\n", diff, maxpx, tot / frames);
  FILE *o = fopen(argv[2], "wb");
  fprintf(o, "P6 %d %d 255\n", LT_PANEL_H, LT_PANEL_W);
  for(int v = 0; v < (int)LT_PANEL_W; v++)            /* portrait: u across, v down */
    for(int u = 0; u < (int)LT_PANEL_H; u++)
    {
      uint16_t c = inc[LT_PANEL_H - 1 - u][v];         /* rotation 0 */
      uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
      fwrite(rgb, 1, 3, o);
    }
  fclose(o);
  return 0;
}
