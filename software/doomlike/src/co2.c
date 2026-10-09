#include "at32f415.h"
#include "board.h"
#include "co2.h"

#define TX_PIN     GPIO_PINS_0
#define RX_PIN     GPIO_PINS_1
#define BAUD       9600u
#define FRAME_LEN  14u
#define GAP_MS     20u     /* a pause this long starts a new frame */

static uint32_t bit_cycles;
static volatile uint8_t ring[32];
static volatile uint8_t head;
static uint8_t tail;
static uint8_t frame[FRAME_LEN];
static uint8_t fill;
static uint32_t last_byte;
volatile uint32_t co2_rx_bytes;

static void wait_until(uint32_t t)
{
  while((int32_t)(board_cycles() - t) < 0)
  {
  }
}

void co2_init(void)
{
  gpio_init_type gpio;
  exint_init_type ex;

  bit_cycles = system_core_clock / BAUD;

  /* PA0 is already a push-pull output held high (board_gpio_init) */
  gpio_default_para_init(&gpio);
  gpio.gpio_mode = GPIO_MODE_INPUT;
  gpio.gpio_pull = GPIO_PULL_UP;
  gpio.gpio_pins = RX_PIN;
  gpio_init(GPIOA, &gpio);

  gpio_exint_line_config(GPIO_PORT_SOURCE_GPIOA, GPIO_PINS_SOURCE1);
  exint_default_para_init(&ex);
  ex.line_mode = EXINT_LINE_INTERRUPT;
  ex.line_select = EXINT_LINE_1;
  ex.line_polarity = EXINT_TRIGGER_FALLING_EDGE;
  ex.line_enable = TRUE;
  exint_init(&ex);
  exint_flag_clear(EXINT_LINE_1);
  NVIC_SetPriority(EXINT1_IRQn, 0u);
  NVIC_EnableIRQ(EXINT1_IRQn);
}

/* Start bit edge: sample the byte in the middle of each bit. */
void EXINT1_IRQHandler(void)
{
  uint32_t t0 = board_cycles();
  uint8_t v = 0u;

  wait_until(t0 + bit_cycles / 2u);
  if(gpio_input_data_bit_read(GPIOA, RX_PIN) == RESET)
  {
    for(int i = 0; i < 8; i++)
    {
      wait_until(t0 + bit_cycles * (uint32_t)(3 + 2 * i) / 2u);
      v = (uint8_t)((v >> 1) | (gpio_input_data_bit_read(GPIOA, RX_PIN) == SET ? 0x80u : 0u));
    }
    wait_until(t0 + bit_cycles * 19u / 2u);   /* into the stop bit */
    ring[head % sizeof(ring)] = v;
    head++;
    co2_rx_bytes++;
  }
  exint_flag_clear(EXINT_LINE_1);
}

static void send_byte(uint8_t v)
{
  uint32_t t = board_cycles();

  gpio_bits_reset(GPIOA, TX_PIN);
  for(int i = 0; i < 8; i++, v >>= 1)
  {
    t += bit_cycles;
    wait_until(t);
    if((v & 1u) != 0u) gpio_bits_set(GPIOA, TX_PIN); else gpio_bits_reset(GPIOA, TX_PIN);
  }
  t += bit_cycles;
  wait_until(t);
  gpio_bits_set(GPIOA, TX_PIN);
  wait_until(t + bit_cycles);
}

void co2_request(void)
{
  static const uint8_t cmd[5] = {0x64u, 0x69u, 0x03u, 0x5Eu, 0x4Eu};

  for(unsigned i = 0; i < sizeof(cmd); i++)
  {
    send_byte(cmd[i]);
  }
}

bool co2_poll(int16_t *ppm)
{
  bool got = false;

  if(fill != 0u && board_cycles() - last_byte > GAP_MS * (system_core_clock / 1000u))
  {
    fill = 0u;
  }
  while(tail != head)
  {
    frame[fill++] = ring[tail % sizeof(ring)];
    tail++;
    last_byte = board_cycles();
    if(fill == FRAME_LEN)
    {
      fill = 0u;
      if(frame[0] == 0x64u && frame[1] == 0x69u && frame[2] == 0x03u && frame[3] == 0x01u)
      {
        uint16_t v = (uint16_t)(frame[4] | (frame[5] << 8));
        *ppm = (int16_t)(v > 5000u ? 5000u : v);
        got = true;
      }
    }
  }
  return got;
}
