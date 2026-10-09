#include "at32f415.h"
#include "battery.h"

#define RAW_EMPTY 2100
#define RAW_FULL  2400
#define SAMPLES   16

void battery_init(void)
{
  gpio_init_type gpio;
  adc_base_config_type cfg;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);
  crm_adc_clock_div_set(CRM_ADC_DIV_6);        /* 72 MHz APB2 / 6 = 12 MHz */

  gpio_default_para_init(&gpio);
  gpio.gpio_mode = GPIO_MODE_ANALOG;
  gpio.gpio_pins = GPIO_PINS_5;
  gpio_init(GPIOA, &gpio);

  adc_base_default_para_init(&cfg);
  cfg.sequence_mode = FALSE;
  cfg.repeat_mode = FALSE;
  cfg.data_align = ADC_RIGHT_ALIGNMENT;
  cfg.ordinary_channel_length = 1u;
  adc_base_config(ADC1, &cfg);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_5, 1u, ADC_SAMPLETIME_239_5);
  adc_ordinary_conversion_trigger_set(ADC1, ADC12_ORDINARY_TRIG_SOFTWARE, TRUE);
  adc_enable(ADC1, TRUE);

  adc_calibration_init(ADC1);
  while(adc_calibration_init_status_get(ADC1) == SET)
  {
  }
  adc_calibration_start(ADC1);
  while(adc_calibration_status_get(ADC1) == SET)
  {
  }
}

uint16_t battery_raw(void)
{
  uint32_t sum = 0u;

  for(int i = 0; i < SAMPLES; i++)
  {
    adc_ordinary_software_trigger_enable(ADC1, TRUE);
    while(adc_flag_get(ADC1, ADC_CCE_FLAG) == RESET)
    {
    }
    sum += adc_ordinary_conversion_data_get(ADC1);   /* clears CCE */
  }
  return (uint16_t)(sum / SAMPLES);
}

int8_t battery_percent(uint16_t raw)
{
  static int32_t avg = -1;     /* x16 */
  int32_t v;

  /* slow moving average so the number does not flicker */
  avg = avg < 0 ? (int32_t)raw * 16 : avg + (int32_t)raw - avg / 16;
  v = (avg / 16 - RAW_EMPTY) * 100 / (RAW_FULL - RAW_EMPTY);
  if(v < 0) v = 0;
  if(v > 100) v = 100;
  return (int8_t)v;
}
