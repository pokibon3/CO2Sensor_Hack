#include "board.h"
#include "swi2c.h"

static void pins_mode(const swi2c_t *b, uint16_t pins, gpio_mode_type mode)
{
  gpio_init_type gpio;

  gpio_default_para_init(&gpio);
  gpio.gpio_mode = mode;
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio.gpio_pull = mode == GPIO_MODE_INPUT ? GPIO_PULL_UP : GPIO_PULL_NONE;
  gpio.gpio_pins = pins;
  gpio_init(b->port, &gpio);
}

static void scl(const swi2c_t *b, bool high)
{
  if(high) gpio_bits_set(b->port, b->scl); else gpio_bits_reset(b->port, b->scl);
  board_delay_us(b->half_us);
}

static void sda(const swi2c_t *b, bool high)
{
  if(high) gpio_bits_set(b->port, b->sda); else gpio_bits_reset(b->port, b->sda);
}

static bool sda_read(const swi2c_t *b)
{
  return gpio_input_data_bit_read(b->port, b->sda) == SET;
}

void swi2c_start(const swi2c_t *b)
{
  sda(b, true);
  scl(b, true);
  pins_mode(b, b->scl | b->sda, GPIO_MODE_OUTPUT);
  sda(b, false);
  board_delay_us(b->half_us);
  scl(b, false);
}

void swi2c_stop(const swi2c_t *b)
{
  pins_mode(b, b->sda, GPIO_MODE_OUTPUT);
  sda(b, false);
  scl(b, true);
  sda(b, true);
  board_delay_us(b->half_us);
}

bool swi2c_send(const swi2c_t *b, uint8_t v)
{
  bool ack;

  pins_mode(b, b->sda, GPIO_MODE_OUTPUT);
  for(int i = 0; i < 8; i++, v <<= 1)
  {
    sda(b, (v & 0x80u) != 0u);
    scl(b, true);
    scl(b, false);
  }
  pins_mode(b, b->sda, GPIO_MODE_INPUT);
  scl(b, true);
  ack = !sda_read(b);
  scl(b, false);
  return ack;
}

uint8_t swi2c_recv(const swi2c_t *b, bool last)
{
  uint8_t v = 0u;

  pins_mode(b, b->sda, GPIO_MODE_INPUT);
  for(int i = 0; i < 8; i++)
  {
    scl(b, true);
    v = (uint8_t)((v << 1) | (sda_read(b) ? 1u : 0u));
    scl(b, false);
  }
  pins_mode(b, b->sda, GPIO_MODE_OUTPUT);
  sda(b, last);
  scl(b, true);
  scl(b, false);
  return v;
}
