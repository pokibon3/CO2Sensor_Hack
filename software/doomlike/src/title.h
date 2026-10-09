#ifndef TITLE_H
#define TITLE_H

#include <stdbool.h>

/* full: redraw the whole page (else only the blinking prompt). */
void title_draw(bool full, bool prompt);

#endif
