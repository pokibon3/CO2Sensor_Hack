#include "at32f415.h"
#include "board.h"
#include "lt7680.h"

#define LT_SPI      SPI2
#define LT_CS_PIN   GPIO_PINS_12

/* Panel timing (HXX043LB0701), from the factory firmware. */
#define LCD_H_NON_DISPLAY  39u
#define LCD_HSYNC_START     8u
#define LCD_HSYNC_WIDTH     4u
#define LCD_V_NON_DISPLAY   8u
#define LCD_VSYNC_START     8u
#define LCD_VSYNC_WIDTH     4u

/* Off-screen canvas row used for the SPI write self-test. */
#define TEST_ROW 400u
#define TEST_LEN 32u

lt_info_t lt_info;

static inline void cs_low(void)
{
  GPIOB->clr = LT_CS_PIN;
}

static inline void cs_high(void)
{
  GPIOB->scr = LT_CS_PIN;
}

static uint8_t spi_xfer(uint8_t value)
{
  while(LT_SPI->sts_bit.tdbe == 0u)
  {
  }
  LT_SPI->dt = value;
  while(LT_SPI->sts_bit.rdbf == 0u)
  {
  }
  return (uint8_t)LT_SPI->dt;
}

static inline void spi_tx(uint8_t value)
{
  while(LT_SPI->sts_bit.tdbe == 0u)
  {
  }
  LT_SPI->dt = value;
}

/* Wait for the end of a transmit-only burst and drop the received bytes
 * (clears the overrun flag), so the next spi_xfer() reads fresh data.
 */
static void spi_tx_finish(void)
{
  volatile uint32_t dummy;

  while(LT_SPI->sts_bit.tdbe == 0u)
  {
  }
  while(LT_SPI->sts_bit.bf != 0u)
  {
  }
  dummy = LT_SPI->dt;
  dummy = LT_SPI->sts;
  (void)dummy;
}

static void spi_config(uint8_t mode, spi_mclk_freq_div_type div)
{
  spi_tx_finish();
  LT_SPI->ctrl1_bit.spien = FALSE;
  LT_SPI->ctrl1_bit.clkpol = (mode == 3u) ? 1u : 0u;
  LT_SPI->ctrl1_bit.clkpha = (mode == 3u) ? 1u : 0u;
  LT_SPI->ctrl2_bit.mdiv_h = 0u;
  LT_SPI->ctrl1_bit.mdiv_l = (uint32_t)div & 7u;
  LT_SPI->ctrl1_bit.spien = TRUE;
}

static void lt_cmd(uint8_t reg)
{
  cs_low();
  (void)spi_xfer(0x00u);
  (void)spi_xfer(reg);
  cs_high();
}

static void lt_data(uint8_t value)
{
  cs_low();
  (void)spi_xfer(0x80u);
  (void)spi_xfer(value);
  cs_high();
}

static uint8_t lt_data_read(void)
{
  uint8_t value;
  cs_low();
  (void)spi_xfer(0xC0u);
  value = spi_xfer(0x00u);
  cs_high();
  return value;
}

static uint8_t lt_status(void)
{
  uint8_t value;
  cs_low();
  (void)spi_xfer(0x40u);
  value = spi_xfer(0x00u);
  cs_high();
  return value;
}

static void lt_reg(uint8_t reg, uint8_t value)
{
  lt_cmd(reg);
  lt_data(value);
}

static uint8_t lt_reg_read(uint8_t reg)
{
  lt_cmd(reg);
  return lt_data_read();
}

static void lt_reg16(uint8_t reg, uint16_t value)
{
  lt_reg(reg, (uint8_t)value);
  lt_reg((uint8_t)(reg + 1u), (uint8_t)(value >> 8));
}

static void lt_reg32(uint8_t reg, uint32_t value)
{
  lt_reg(reg, (uint8_t)value);
  lt_reg((uint8_t)(reg + 1u), (uint8_t)(value >> 8));
  lt_reg((uint8_t)(reg + 2u), (uint8_t)(value >> 16));
  lt_reg((uint8_t)(reg + 3u), (uint8_t)(value >> 24));
}

static bool lt_readback_ok(void)
{
  lt_reg(0xD2u, 0x5Au);
  if(lt_reg_read(0xD2u) != 0x5Au)
  {
    return false;
  }
  lt_reg(0xD2u, 0xA5u);
  return lt_reg_read(0xD2u) == 0xA5u;
}

static void lt_clock_init(void)
{
  gpio_init_type gpio;

  /* HEXT is already running as the PLL source. */
  crm_clkout_div_set(CRM_CLKOUT_DIV_1);
  crm_clock_out_set(CRM_CLKOUT_HEXT);

  gpio_default_para_init(&gpio);
  gpio.gpio_mode = GPIO_MODE_MUX;
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio.gpio_pull = GPIO_PULL_NONE;
  gpio.gpio_pins = GPIO_PINS_8;
  gpio_init(GPIOA, &gpio);
}

static void lt_spi_init(void)
{
  gpio_init_type gpio;
  spi_init_type spi;

  crm_periph_clock_enable(CRM_SPI2_PERIPH_CLOCK, TRUE);

  gpio_default_para_init(&gpio);
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;

  gpio.gpio_mode = GPIO_MODE_OUTPUT;
  gpio.gpio_pull = GPIO_PULL_UP;
  gpio.gpio_pins = LT_CS_PIN;
  cs_high();
  gpio_init(GPIOB, &gpio);

  gpio.gpio_mode = GPIO_MODE_MUX;
  gpio.gpio_pins = GPIO_PINS_13 | GPIO_PINS_15;
  gpio_init(GPIOB, &gpio);

  gpio.gpio_mode = GPIO_MODE_INPUT;
  gpio.gpio_pins = GPIO_PINS_14;
  gpio_init(GPIOB, &gpio);

  /* APB1 = 72 MHz. Start slowly: before the LT7680B PLL is up its core
   * runs from the 8 MHz XI.
   */
  spi_default_para_init(&spi);
  spi.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spi.master_slave_mode = SPI_MODE_MASTER;
  spi.mclk_freq_division = SPI_MCLK_DIV_64;
  spi.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spi.frame_bit_num = SPI_FRAME_8BIT;
  spi.clock_polarity = SPI_CLOCK_POLARITY_LOW;
  spi.clock_phase = SPI_CLOCK_PHASE_1EDGE;
  spi.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
  spi_init(LT_SPI, &spi);
  spi_enable(LT_SPI, TRUE);
}

static bool lt_wait_normal(void)
{
  for(uint32_t i = 0; i < 200u; i++)
  {
    if((lt_status() & 0x02u) == 0u)
    {
      return true;
    }
    board_delay_ms(2u);
  }
  return false;
}

static void lt_pll_init(void)
{
  /* XI = 8 MHz, values from the factory firmware:
   * Fout = XI * N / (R * 2^OD) -> PCLK = 4.5 MHz, MCLK = CCLK = 50 MHz.
   */
  const uint8_t od = 3u;
  const uint8_t r = 2u;
  const uint16_t pclk_n = 9u;
  const uint16_t mclk_n = 100u;
  const uint16_t cclk_n = 100u;

  lt_reg(0x05u, (uint8_t)((od << 6) | (r << 1) | ((pclk_n >> 8) & 1u)));
  lt_reg(0x07u, (uint8_t)((od << 6) | (r << 1) | ((mclk_n >> 8) & 1u)));
  lt_reg(0x09u, (uint8_t)((od << 6) | (r << 1) | ((cclk_n >> 8) & 1u)));
  lt_reg(0x06u, (uint8_t)pclk_n);
  lt_reg(0x08u, (uint8_t)mclk_n);
  lt_reg(0x0Au, (uint8_t)cclk_n);
  lt_reg(0x00u, 0x80u);
  board_delay_ms(10u);
}

static void lt_sdram_init(void)
{
  /* Factory values: REGE0 = 0x29, REGE1 = 0x03, refresh interval 779. */
  const uint16_t refresh = 779u;

  lt_reg(0xE0u, 0x29u);
  lt_reg(0xE1u, 0x03u);
  lt_reg16(0xE2u, refresh);
  lt_reg(0xE4u, 0x01u);
  for(uint32_t i = 0; i < 100u && (lt_status() & 0x04u) == 0u; i++)
  {
    board_delay_ms(1u);
  }
}

static void lt_panel_init(void)
{
  uint8_t v;

  /* REG01: 16-bit TFT output, as the factory firmware. */
  v = lt_reg_read(0x01u);
  lt_reg(0x01u, (uint8_t)((v | 0x10u) & (uint8_t)~0x08u));

  /* REG02: direct data format, write left->right then top->down.
   * REG03: graphic mode, memory port = SDRAM.
   */
  lt_reg(0x02u, 0x00u);
  v = lt_reg_read(0x03u);
  lt_reg(0x03u, (uint8_t)(v & (uint8_t)~0x07u));

  /* REG12: PCLK falling edge, display off for now, no color bar.
   * REG13: HSYNC/VSYNC active low, DE active high.
   */
  lt_reg(0x12u, 0x80u);
  lt_reg(0x13u, 0x00u);

  lt_reg(0x14u, (uint8_t)((LT_PANEL_W / 8u) - 1u));
  lt_reg(0x15u, (uint8_t)(LT_PANEL_W % 8u));
  lt_reg(0x16u, (uint8_t)((LCD_H_NON_DISPLAY / 8u) - 1u));
  lt_reg(0x17u, (uint8_t)(LCD_H_NON_DISPLAY % 8u));
  lt_reg(0x18u, (uint8_t)((LCD_HSYNC_START / 8u) - 1u));
  lt_reg(0x19u, LCD_HSYNC_WIDTH < 8u ? 0u : (uint8_t)((LCD_HSYNC_WIDTH / 8u) - 1u));
  lt_reg16(0x1Au, (uint16_t)(LT_PANEL_H - 1u));
  lt_reg16(0x1Cu, (uint16_t)(LCD_V_NON_DISPLAY - 1u));
  lt_reg(0x1Eu, (uint8_t)(LCD_VSYNC_START - 1u));
  lt_reg(0x1Fu, (uint8_t)(LCD_VSYNC_WIDTH - 1u));

  /* REG10: main window 16 bpp, sync mode, no PIP. */
  lt_reg(0x10u, 0x04u);
  lt_reg32(0x20u, 0u);                     /* main image start address */
  lt_reg16(0x24u, LT_PANEL_W);             /* main image width */
  lt_reg16(0x26u, 0u);                     /* main window upper-left */
  lt_reg16(0x28u, 0u);

  /* Canvas = main image, block mode, 16 bpp. */
  lt_reg32(0x50u, 0u);
  lt_reg16(0x54u, LT_PANEL_W);
  lt_reg(0x5Eu, 0x01u);
}

static void set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  lt_reg16(0x56u, x);
  lt_reg16(0x58u, y);
  lt_reg16(0x5Au, w);
  lt_reg16(0x5Cu, h);
  lt_reg16(0x5Fu, x);
  lt_reg16(0x61u, y);
}

void lt_write_begin(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  set_window(x, y, w, h);
  lt_cmd(0x04u);
  if(lt_info.burst_ok)
  {
    cs_low();
    spi_tx(0x80u);
  }
}

/* n = number of bytes */
void lt_write_pixels(const uint8_t *bytes, uint32_t n)
{
  if(lt_info.burst_ok)
  {
    while(n-- != 0u)
    {
      spi_tx(*bytes++);
    }
    return;
  }
  while(n-- != 0u)
  {
    cs_low();
    spi_tx(0x80u);
    spi_tx(*bytes++);
    spi_tx_finish();
    cs_high();
  }
}

void lt_write_end(void)
{
  if(lt_info.burst_ok)
  {
    spi_tx_finish();
    cs_high();
  }
}

void lt_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  uint8_t line[64];
  uint32_t n = (uint32_t)w * h * 2u;

  for(uint32_t i = 0; i < sizeof(line); i += 2u)
  {
    line[i] = (uint8_t)color;
    line[i + 1u] = (uint8_t)(color >> 8);
  }
  lt_write_begin(x, y, w, h);
  while(n != 0u)
  {
    uint32_t chunk = n > sizeof(line) ? sizeof(line) : n;
    lt_write_pixels(line, chunk);
    n -= chunk;
  }
  lt_write_end();
}

/* SPI master registers: REGB8 data, REGB9 control (bit5 = nSS1 select,
 * bit4 = nSS active, bits1:0 = SPI mode), REGBA status (bit5 = RX FIFO
 * empty), REGBB clock divisor (SCK = CCLK / (2 * (div + 1))).
 */
#define SF_CTRL_IDLE    0x20u
#define SF_CTRL_ACTIVE  0x30u

/* Register reads are only verified at the slow register clock, and the SPI
 * master needs many of them, so this drops the host SPI back to 4.5 MHz.
 */
void lt_sf_init(void)
{
  spi_config(lt_info.spi_mode, SPI_MCLK_DIV_16);
  lt_reg(0xBBu, 0x01u);                    /* 50 MHz / 4 = 12.5 MHz */
  lt_reg(0xB9u, SF_CTRL_IDLE);
}

void lt_sf_begin(void)
{
  lt_reg(0xB9u, SF_CTRL_IDLE);
  while((lt_reg_read(0xBAu) & 0x20u) == 0u)
  {
    (void)lt_reg_read(0xB8u);
  }
  lt_reg(0xB9u, SF_CTRL_ACTIVE);
}

uint8_t lt_sf_xfer(uint8_t value)
{
  lt_reg(0xB8u, value);
  while((lt_reg_read(0xBAu) & 0x20u) != 0u)
  {
  }
  return lt_reg_read(0xB8u);
}

void lt_sf_end(void)
{
  lt_reg(0xB9u, SF_CTRL_IDLE);
}

void lt_display_on(bool on)
{
  lt_reg(0x12u, on ? 0xC0u : 0x80u);
}

/* Write TEST_LEN bytes (TEST_LEN / 2 pixels) off-screen with the current
 * write settings, then read them back at the slow, verified register speed.
 */
static bool write_selftest(uint8_t mode, spi_mclk_freq_div_type div, uint8_t seed)
{
  uint8_t pattern[TEST_LEN];
  bool ok = true;

  for(uint32_t i = 0; i < TEST_LEN; i++)
  {
    pattern[i] = (uint8_t)(i * 37u + seed);
  }

  spi_config(mode, div);
  lt_write_begin(0u, TEST_ROW, TEST_LEN / 2u, 1u);
  lt_write_pixels(pattern, TEST_LEN);
  lt_write_end();

  spi_config(mode, SPI_MCLK_DIV_16);
  set_window(0u, TEST_ROW, TEST_LEN / 2u, 1u);
  lt_cmd(0x04u);
  (void)lt_data_read();                    /* first memory read is a dummy */
  for(uint32_t i = 0; i < TEST_LEN; i++)
  {
    if(lt_data_read() != pattern[i])
    {
      ok = false;
    }
  }
  return ok;
}

bool lt_init(void)
{
  static const spi_mclk_freq_div_type divs[] = {SPI_MCLK_DIV_4, SPI_MCLK_DIV_8, SPI_MCLK_DIV_16};
  bool found = false;

  lt_clock_init();
  lt_spi_init();
  board_delay_ms(100u);

  if(!lt_wait_normal())
  {
    return false;
  }

  lt_pll_init();
  spi_config(0u, SPI_MCLK_DIV_16);         /* 4.5 MHz for register access */
  lt_info.spi_mode = 0u;
  if(!lt_readback_ok())
  {
    spi_config(3u, SPI_MCLK_DIV_16);
    lt_info.spi_mode = 3u;
    if(!lt_readback_ok())
    {
      return false;
    }
  }

  lt_sdram_init();
  lt_panel_init();

  /* Fastest write clock that passes, with multi-byte bursts if possible. */
  for(uint32_t pass = 0; !found && pass < 2u; pass++)
  {
    lt_info.burst_ok = (uint8_t)(pass == 0u);
    for(uint32_t i = 0; !found && i < sizeof(divs) / sizeof(divs[0]); i++)
    {
      if(write_selftest(lt_info.spi_mode, divs[i], (uint8_t)(pass * 100u + i * 50u)))
      {
        lt_info.write_div = (uint8_t)divs[i];
        found = true;
      }
    }
  }
  if(!found)
  {
    lt_info.burst_ok = 0u;
    lt_info.write_div = (uint8_t)SPI_MCLK_DIV_16;
  }

  spi_config(lt_info.spi_mode, (spi_mclk_freq_div_type)lt_info.write_div);
  lt_fill(0u, 0u, LT_PANEL_W, LT_PANEL_H, LT_BLACK);
  lt_info.status = lt_status();
  return true;
}

/* ---- Geometry engine and double buffering ---- */

/* Register write without waiting for each received byte. */
static void lt_wr(uint8_t reg, uint8_t value)
{
  cs_low();
  spi_tx(0x00u);
  spi_tx(reg);
  spi_tx_finish();
  cs_high();
  cs_low();
  spi_tx(0x80u);
  spi_tx(value);
  spi_tx_finish();
  cs_high();
}

/* Last values written to REG68-6F (draw start/end) and REGD2-D4 (colour);
 * -1 = unknown. Unchanged registers are skipped.
 */
static int16_t shadow[11];

static void lt_wr_cached(uint32_t i, uint8_t reg, uint8_t value)
{
  if(shadow[i] != (int16_t)value)
  {
    shadow[i] = (int16_t)value;
    lt_wr(reg, value);
  }
}

uint32_t lt_busy_polls;

void lt_wait_idle(void)
{
  /* STSR bit 3 = core task busy */
  while((lt_status() & 0x08u) != 0u)
  {
    lt_busy_polls++;
  }
}

void lt_gfx_init(void)
{
  for(uint32_t i = 0; i < sizeof(shadow) / sizeof(shadow[0]); i++)
  {
    shadow[i] = -1;
  }
  lt_reg16(0x56u, 0u);                     /* active window = whole canvas */
  lt_reg16(0x58u, 0u);
  lt_reg16(0x5Au, LT_PANEL_W);
  lt_reg16(0x5Cu, LT_PANEL_H);
}

void lt_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
  uint8_t r = (uint8_t)((color >> 8) & 0xF8u);
  uint8_t g = (uint8_t)((color >> 3) & 0xFCu);
  uint8_t b = (uint8_t)(color << 3);

  lt_wait_idle();
  lt_wr_cached(0u, 0x68u, (uint8_t)x0);
  lt_wr_cached(1u, 0x69u, (uint8_t)(x0 >> 8));
  lt_wr_cached(2u, 0x6Au, (uint8_t)y0);
  lt_wr_cached(3u, 0x6Bu, (uint8_t)(y0 >> 8));
  lt_wr_cached(4u, 0x6Cu, (uint8_t)x1);
  lt_wr_cached(5u, 0x6Du, (uint8_t)(x1 >> 8));
  lt_wr_cached(6u, 0x6Eu, (uint8_t)y1);
  lt_wr_cached(7u, 0x6Fu, (uint8_t)(y1 >> 8));
  lt_wr_cached(8u, 0xD2u, r);
  lt_wr_cached(9u, 0xD3u, g);
  lt_wr_cached(10u, 0xD4u, b);
  lt_wr(0x76u, 0xE0u);                     /* start, fill, rectangle */
}

void lt_set_canvas(uint32_t addr)
{
  lt_wait_idle();
  lt_reg32(0x50u, addr);
}

void lt_set_display(uint32_t addr)
{
  lt_wait_idle();
  lt_reg32(0x20u, addr);
}
