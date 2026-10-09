#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

/* Portrait presentation of the 224 x 256 game screen on the 480 x 272 panel.
 * The game is scaled to 272 x 358 (arcade-like 7:6 pixel aspect) and
 * centred vertically. Only VRAM bytes that changed since the last update are
 * sent to the LT7680B.
 */
void video_init(void);
void video_full_redraw(void);
/* Returns the number of pixels sent. */
uint32_t video_update(void);

#endif
