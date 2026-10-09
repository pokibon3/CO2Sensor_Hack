#ifndef LT7680_H
#define LT7680_H

#include <stdbool.h>
#include <stdint.h>

/* LT7680B on SPI2 (PB12 CS, PB13 SCK, PB14 MISO, PB15 MOSI), XI = 8 MHz on PA8.
 * The panel is driven as a 480 x 272, 16 bpp (RGB565) canvas. Pixel data is
 * sent low byte first.
 */
#define LT_PANEL_W 480u
#define LT_PANEL_H 272u

#define LT_RGB565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))
#define LT_BLACK 0x0000u

typedef struct
{
  uint8_t status;       /* status after init */
  uint8_t spi_mode;     /* 0 or 3: mode that passed register read-back */
  uint8_t write_div;    /* SPI_MCLK_DIV_x used for memory writes */
  uint8_t burst_ok;     /* 1: multi-byte data bursts verified by read-back */
} lt_info_t;

extern lt_info_t lt_info;

/* Clock (PA8), SPI and LT7680B initialisation. Returns false if the
 * controller does not respond.
 */
bool lt_init(void);
void lt_display_on(bool on);
void lt_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/* Rectangle write: lt_write_begin(), then w*h pixels (2 bytes each) in row
 * order via lt_write_pixels(), then lt_write_end().
 */
void lt_write_begin(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void lt_write_pixels(const uint8_t *bytes, uint32_t n);
void lt_write_end(void);

/* W25Q32 serial flash on the LT7680B SPI master, chip select nSS1.
 * lt_sf_begin() asserts CS, lt_sf_xfer() exchanges one byte, lt_sf_end()
 * releases CS. Requires lt_init().
 */
void lt_sf_init(void);
void lt_sf_begin(void);
uint8_t lt_sf_xfer(uint8_t value);
void lt_sf_end(void);

#endif
