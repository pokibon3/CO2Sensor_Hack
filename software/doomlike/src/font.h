#ifndef FONT_H
#define FONT_H

#include <stdint.h>

/* 5 x 7 font, 6 x 8 cell, scaled by an integer. Upper case, digits and
 * " :/+-!.%". Unknown characters draw as blanks.
 */
void text_draw(int x, int y, const char *s, int scale, uint16_t color);
int text_width(const char *s, int scale);
void text_center(int y, const char *s, int scale, uint16_t color);

#endif
