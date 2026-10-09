#ifndef GFX_H
#define GFX_H

#include <stdint.h>

/* Portrait screen, 272 x 480, drawn only with filled rectangles.
 * Frames are double buffered: draw between gfx_frame_begin() and
 * gfx_frame_end(); gfx_back() tells which buffer (0/1) is being drawn, for
 * parts that are redrawn only when they change.
 */
#define SCREEN_W 272
#define SCREEN_H 480

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

void gfx_init(void);
void gfx_frame_begin(void);
void gfx_frame_end(void);
uint8_t gfx_back(void);
void gfx_fill(int x, int y, int w, int h, uint16_t color);

extern uint32_t gfx_rects;      /* rectangles drawn in the last frame */

#endif
