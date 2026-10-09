#include "at32f415.h"
#include "board.h"

#define BTN_LEFT_PIN   GPIO_PINS_7
#define BTN_FIRE_PIN   GPIO_PINS_6
#define BTN_S2_PIN     GPIO_PINS_2    /* power button, also "right" */
#define POWER_HOLD_PIN GPIO_PINS_7    /* on GPIOA */

void board_clock_init(void)
{
  crm_reset();
  flash_psr_set(FLASH_WAIT_CYCLE_4);

  crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT, TRUE);
  while(crm_hext_stable_wait() == ERROR)
  {
  }

  /* 8 MHz / 2 * 36 = 144 MHz */
  crm_pll_config(CRM_PLL_SOURCE_HEXT_DIV, CRM_PLL_MULT_36);
  crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);
  while(crm_flag_get(CRM_PLL_STABLE_FLAG) != SET)
  {
  }

  crm_ahb_div_set(CRM_AHB_DIV_1);
  crm_apb2_div_set(CRM_APB2_DIV_2);
  crm_apb1_div_set(CRM_APB1_DIV_2);

  crm_auto_step_mode_enable(TRUE);
  crm_sysclk_switch(CRM_SCLK_PLL);
  while(crm_sysclk_switch_status_get() != CRM_SCLK_PLL)
  {
  }
  crm_auto_step_mode_enable(FALSE);
  system_core_clock_update();

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* Reproduce the GPIO output state the factory firmware holds while running
 * (read over SWD). PB3/PB4 are JTAG pins, so JTAG is released first while
 * SWD stays enabled. PA7 (power hold) is driven separately.
 */
void board_gpio_init(void)
{
  gpio_init_type gpio;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_IOMUX_PERIPH_CLOCK, TRUE);
  gpio_pin_remap_config(SWJTAG_GMUX_010, TRUE);

  gpio_bits_set(GPIOA, GPIO_PINS_0 | GPIO_PINS_11 | GPIO_PINS_15);
  gpio_bits_reset(GPIOA, GPIO_PINS_4);
  gpio_bits_set(GPIOB, GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_3 | GPIO_PINS_4);
  gpio_bits_reset(GPIOC, GPIO_PINS_15);

  gpio_default_para_init(&gpio);
  gpio.gpio_mode = GPIO_MODE_OUTPUT;
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio.gpio_pull = GPIO_PULL_NONE;

  gpio.gpio_pins = GPIO_PINS_0 | GPIO_PINS_4 | GPIO_PINS_11 | GPIO_PINS_15;
  gpio_init(GPIOA, &gpio);
  gpio.gpio_pins = GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_3 | GPIO_PINS_4;
  gpio_init(GPIOB, &gpio);
  gpio.gpio_pins = GPIO_PINS_15;
  gpio_init(GPIOC, &gpio);

  gpio.gpio_mode = GPIO_MODE_INPUT;
  gpio.gpio_pull = GPIO_PULL_UP;
  gpio.gpio_pins = BTN_LEFT_PIN | BTN_FIRE_PIN | BTN_S2_PIN;
  gpio_init(GPIOB, &gpio);
}

void board_power_hold(bool on)
{
  gpio_init_type gpio;

  if(on)
  {
    gpio_bits_set(GPIOA, POWER_HOLD_PIN);
  }
  else
  {
    gpio_bits_reset(GPIOA, POWER_HOLD_PIN);
  }

  gpio_default_para_init(&gpio);
  gpio.gpio_mode = GPIO_MODE_OUTPUT;
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio.gpio_pull = GPIO_PULL_NONE;
  gpio.gpio_pins = POWER_HOLD_PIN;
  gpio_init(GPIOA, &gpio);
}

uint32_t board_cycles(void)
{
  return DWT->CYCCNT;
}

void board_delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * (system_core_clock / 1000000u);
  while((DWT->CYCCNT - start) < ticks)
  {
  }
}

void board_delay_ms(uint32_t ms)
{
  while(ms-- != 0u)
  {
    board_delay_us(1000u);
  }
}

bool board_s2_pressed(void)
{
  return gpio_input_data_bit_read(GPIOB, BTN_S2_PIN) == RESET;
}

bool board_left_pressed(void)
{
  return gpio_input_data_bit_read(GPIOB, BTN_LEFT_PIN) == RESET;
}

bool board_fire_pressed(void)
{
  return gpio_input_data_bit_read(GPIOB, BTN_FIRE_PIN) == RESET;
}
