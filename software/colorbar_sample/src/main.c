#include "at32f415.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Board wiring read back from the factory firmware's live register state:
 *   PB12 -> LT7680B SCS#
 *   PB13 -> LT7680B SCLK  (SPI2)
 *   PB14 <- LT7680B SDO   (SPI2)
 *   PB15 -> LT7680B SDI   (SPI2)
 *   PA8  -> LT7680B XI    (8 MHz; the LT7680B has no crystal of its own)
 *
 * SPI mode 0 reads back correctly at low SCK. Mode 3 (used by the factory
 * firmware at 12 MHz) shifts read data by one bit at this speed.
 *
 * This program deliberately uses the LT7680B built-in color-bar generator.
 * It does not require the W25Q32JV contents or SDRAM drawing functions.
 */

#define LT_SPI     SPI2
#define LT_CS_PORT GPIOB
#define LT_CS_PIN  GPIO_PINS_12

/* HXX043LB0701: 4.3-inch 480 x 272 RGB panel.
 * Timing values taken from the factory firmware's LT7680B initialisation.
 */
#define LCD_WIDTH          480u
#define LCD_HEIGHT         272u
#define LCD_H_NON_DISPLAY   39u
#define LCD_HSYNC_START      8u
#define LCD_HSYNC_WIDTH      4u
#define LCD_V_NON_DISPLAY    8u
#define LCD_VSYNC_START      8u
#define LCD_VSYNC_WIDTH      4u

/* Diagnostics readable over SWD (e.g. OpenOCD "mdw &g_diag").
 * stage: 1 = SPI init done, 2 = LT7680B status OK, 3 = color bar configured,
 *        0xE1 = LT7680B never left power-saving state,
 *        0xE2 = register write/read-back mismatch.
 */
volatile struct
{
  uint32_t stage;
  uint8_t status;
  uint8_t reg00;
  uint8_t reg01;
  uint8_t reg12;
} g_diag;

static void delay_cycles(volatile uint32_t count)
{
  while(count-- != 0u)
  {
    __NOP();
  }
}

static void delay_ms(uint32_t ms)
{
  /* Adequate for reset/start-up waits; exact timing is not required here. */
  while(ms-- != 0u)
  {
    delay_cycles(system_core_clock / 5000u);
  }
}

static uint8_t lt_spi_transfer(uint8_t value)
{
  while(spi_i2s_flag_get(LT_SPI, SPI_I2S_TDBE_FLAG) == RESET)
  {
  }
  spi_i2s_data_transmit(LT_SPI, value);
  while(spi_i2s_flag_get(LT_SPI, SPI_I2S_RDBF_FLAG) == RESET)
  {
  }
  return (uint8_t)spi_i2s_data_receive(LT_SPI);
}

static inline void lt_select(void)
{
  gpio_bits_reset(LT_CS_PORT, LT_CS_PIN);
}

static inline void lt_deselect(void)
{
  while(spi_i2s_flag_get(LT_SPI, SPI_I2S_BF_FLAG) != RESET)
  {
  }
  gpio_bits_set(LT_CS_PORT, LT_CS_PIN);
}

static void lt_cmd(uint8_t reg)
{
  lt_select();
  (void)lt_spi_transfer(0x00u);
  (void)lt_spi_transfer(reg);
  lt_deselect();
}

static void lt_data_write(uint8_t value)
{
  lt_select();
  (void)lt_spi_transfer(0x80u);
  (void)lt_spi_transfer(value);
  lt_deselect();
}

static uint8_t lt_data_read(void)
{
  uint8_t value;
  lt_select();
  (void)lt_spi_transfer(0xC0u);
  value = lt_spi_transfer(0x00u);
  lt_deselect();
  return value;
}

static uint8_t lt_status_read(void)
{
  uint8_t value;
  lt_select();
  (void)lt_spi_transfer(0x40u);
  value = lt_spi_transfer(0x00u);
  lt_deselect();
  return value;
}

static void lt_reg_write(uint8_t reg, uint8_t value)
{
  lt_cmd(reg);
  lt_data_write(value);
}

static uint8_t lt_reg_read(uint8_t reg)
{
  lt_cmd(reg);
  return lt_data_read();
}

/* Reproduce the GPIO output state the factory firmware holds while running
 * (read over SWD). The board loses power as soon as the MCU is reset, which
 * points to a power-hold latch driven by one of these pins.
 * PB3/PB4 are JTAG pins, so JTAG is released first while SWD stays enabled.
 */
static void board_power_hold_init(void)
{
  gpio_init_type gpio;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_IOMUX_PERIPH_CLOCK, TRUE);
  gpio_pin_remap_config(SWJTAG_GMUX_010, TRUE);

  gpio_bits_set(GPIOA, GPIO_PINS_0 | GPIO_PINS_7 | GPIO_PINS_11 | GPIO_PINS_15);
  gpio_bits_reset(GPIOA, GPIO_PINS_4);
  gpio_bits_set(GPIOB, GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_3 | GPIO_PINS_4);
  gpio_bits_reset(GPIOC, GPIO_PINS_15);

  gpio_default_para_init(&gpio);
  gpio.gpio_mode = GPIO_MODE_OUTPUT;
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio.gpio_pull = GPIO_PULL_NONE;

  gpio.gpio_pins = GPIO_PINS_0 | GPIO_PINS_4 | GPIO_PINS_7 | GPIO_PINS_11 | GPIO_PINS_15;
  gpio_init(GPIOA, &gpio);
  gpio.gpio_pins = GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_3 | GPIO_PINS_4;
  gpio_init(GPIOB, &gpio);
  gpio.gpio_pins = GPIO_PINS_15;
  gpio_init(GPIOC, &gpio);
}

/* The LT7680B XI input is fed from the MCU. The factory firmware generates
 * 8 MHz with TMR1 CH1; CLKOUT of the 8 MHz HEXT crystal gives the same clock.
 */
static void board_lt_clock_init(void)
{
  gpio_init_type gpio;

  crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT, TRUE);
  (void)crm_hext_stable_wait();
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

static void board_spi2_init(void)
{
  gpio_init_type gpio;
  spi_init_type spi;

  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_SPI2_PERIPH_CLOCK, TRUE);

  gpio_default_para_init(&gpio);
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio.gpio_pull = GPIO_PULL_UP;

  gpio.gpio_mode = GPIO_MODE_OUTPUT;
  gpio.gpio_pins = LT_CS_PIN;
  gpio_init(LT_CS_PORT, &gpio);
  lt_deselect();

  gpio.gpio_mode = GPIO_MODE_MUX;
  gpio.gpio_pull = GPIO_PULL_UP;
  gpio.gpio_pins = GPIO_PINS_13;
  gpio_init(GPIOB, &gpio);

  gpio.gpio_mode = GPIO_MODE_INPUT;
  gpio.gpio_pull = GPIO_PULL_UP;
  gpio.gpio_pins = GPIO_PINS_14;
  gpio_init(GPIOB, &gpio);

  gpio.gpio_mode = GPIO_MODE_MUX;
  gpio.gpio_pull = GPIO_PULL_UP;
  gpio.gpio_pins = GPIO_PINS_15;
  gpio_init(GPIOB, &gpio);

  spi_default_para_init(&spi);
  spi.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spi.master_slave_mode = SPI_MODE_MASTER;
  spi.mclk_freq_division = SPI_MCLK_DIV_16;
  spi.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spi.frame_bit_num = SPI_FRAME_8BIT;
  spi.clock_polarity = SPI_CLOCK_POLARITY_LOW;
  spi.clock_phase = SPI_CLOCK_PHASE_1EDGE;
  spi.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
  spi_init(LT_SPI, &spi);
  spi_enable(LT_SPI, TRUE);
}

static bool lt_wait_normal(uint32_t attempts)
{
  while(attempts-- != 0u)
  {
    /* Status bit 1 = power-saving state; normal operation reads as zero. */
    if((lt_status_read() & 0x02u) == 0u)
    {
      return true;
    }
    delay_ms(2u);
  }
  return false;
}

/* A stuck MISO line also reads as "normal" status, so confirm with a
 * write/read-back of a plain data register (REGD2, foreground red).
 */
static bool lt_readback_ok(void)
{
  lt_reg_write(0xD2u, 0x5Au);
  if(lt_reg_read(0xD2u) != 0x5Au)
  {
    return false;
  }
  lt_reg_write(0xD2u, 0xA5u);
  return lt_reg_read(0xD2u) == 0xA5u;
}

static void lt_program_pll(void)
{
  /* XI = 8 MHz from PA8. Values copied from the factory firmware:
   * Fout = XI * N / (R * 2^OD) -> PCLK = 4.5 MHz, MCLK = CCLK = 50 MHz.
   */
  const uint16_t od = 3u;
  const uint16_t r = 2u;
  const uint16_t pclk_n = 9u;
  const uint16_t mclk_n = 100u;
  const uint16_t cclk_n = 100u;

  lt_reg_write(0x05u, (uint8_t)((od << 6) | (r << 1) | ((pclk_n >> 8) & 1u)));
  lt_reg_write(0x07u, (uint8_t)((od << 6) | (r << 1) | ((mclk_n >> 8) & 1u)));
  lt_reg_write(0x09u, (uint8_t)((od << 6) | (r << 1) | ((cclk_n >> 8) & 1u)));
  lt_reg_write(0x06u, (uint8_t)pclk_n);
  lt_reg_write(0x08u, (uint8_t)mclk_n);
  lt_reg_write(0x0Au, (uint8_t)cclk_n);

  /* Enable PLL. */
  lt_reg_write(0x00u, 0x80u);
  delay_ms(2u);
}

static void set_horizontal_size(uint16_t width, uint16_t non_display,
                                uint16_t sync_start, uint16_t sync_width)
{
  lt_reg_write(0x14u, (uint8_t)((width / 8u) - 1u));
  lt_reg_write(0x15u, (uint8_t)(width % 8u));
  lt_reg_write(0x16u, (uint8_t)((non_display / 8u) - 1u));
  lt_reg_write(0x17u, (uint8_t)(non_display % 8u));
  lt_reg_write(0x18u, sync_start < 8u ? 0u : (uint8_t)((sync_start / 8u) - 1u));
  lt_reg_write(0x19u, sync_width < 8u ? 0u : (uint8_t)((sync_width / 8u) - 1u));
}

static void set_vertical_size(uint16_t height, uint16_t non_display,
                              uint16_t sync_start, uint16_t sync_width)
{
  const uint16_t h = height - 1u;
  const uint16_t nd = non_display - 1u;
  lt_reg_write(0x1Au, (uint8_t)h);
  lt_reg_write(0x1Bu, (uint8_t)(h >> 8));
  lt_reg_write(0x1Cu, (uint8_t)nd);
  lt_reg_write(0x1Du, (uint8_t)(nd >> 8));
  lt_reg_write(0x1Eu, (uint8_t)(sync_start - 1u));
  lt_reg_write(0x1Fu, (uint8_t)(sync_width - 1u));
}

static void lt_sdram_init(void)
{
  /* Factory values: REGE0 = 0x29, REGE1 = 0x03, refresh interval 779. */
  const uint16_t refresh = 779u;
  uint32_t attempts = 100u;

  lt_reg_write(0xE0u, 0x29u);
  lt_reg_write(0xE1u, 0x03u);
  lt_reg_write(0xE2u, (uint8_t)refresh);
  lt_reg_write(0xE3u, (uint8_t)(refresh >> 8));
  lt_reg_write(0xE4u, 0x01u);

  /* Status bit 2 = SDRAM ready. */
  while(((lt_status_read() & 0x04u) == 0u) && (attempts-- != 0u))
  {
    delay_ms(1u);
  }
}

static void lt_color_bar_init(void)
{
  uint8_t value;

  lt_program_pll();
  lt_sdram_init();

  /* REG01: 16-bit TFT output. LT7680B host remains serial SPI. */
  value = lt_reg_read(0x01u);
  value = (uint8_t)((value | 0x10u) & (uint8_t)~0x08u);
  lt_reg_write(0x01u, value);

  /* REG12: PCLK falling edge, display ON, color bar ON,
   * left-to-right/top-to-bottom scan, RGB order.
   */
  lt_reg_write(0x12u, 0xE0u);

  /* REG13: HSYNC low, VSYNC low, DE high. */
  lt_reg_write(0x13u, 0x00u);

  set_horizontal_size(LCD_WIDTH, LCD_H_NON_DISPLAY,
                      LCD_HSYNC_START, LCD_HSYNC_WIDTH);
  set_vertical_size(LCD_HEIGHT, LCD_V_NON_DISPLAY,
                    LCD_VSYNC_START, LCD_VSYNC_WIDTH);

  /* Re-assert display and test pattern after timing registers are set. */
  value = lt_reg_read(0x12u);
  lt_reg_write(0x12u, (uint8_t)(value | 0x60u));
}

int main(void)
{
  board_power_hold_init();
  system_core_clock_update();
  board_lt_clock_init();
  board_spi2_init();
  g_diag.stage = 1u;
  delay_ms(100u);

  g_diag.status = lt_status_read();
  if(!lt_wait_normal(200u))
  {
    /* No response: most likely the inferred SPI pin assignment is wrong. */
    g_diag.stage = 0xE1u;
    for(;;)
    {
    }
  }

  if(!lt_readback_ok())
  {
    g_diag.stage = 0xE2u;
    for(;;)
    {
    }
  }

  g_diag.stage = 2u;
  lt_color_bar_init();
  g_diag.status = lt_status_read();
  g_diag.reg00 = lt_reg_read(0x00u);
  g_diag.reg01 = lt_reg_read(0x01u);
  g_diag.reg12 = lt_reg_read(0x12u);
  g_diag.stage = 3u;

  for(;;)
  {
    /* LT7680B continuously generates the color bars. */
  }
}

