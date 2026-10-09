#include "sht3x.h"
#include "swi2c.h"

#define ADDR_W 0x88u
#define ADDR_R 0x89u

static const swi2c_t bus = {GPIOB, GPIO_PINS_3, GPIO_PINS_4, 5u};

static bool command(uint16_t cmd)
{
  bool ok;

  swi2c_start(&bus);
  ok = swi2c_send(&bus, ADDR_W) && swi2c_send(&bus, (uint8_t)(cmd >> 8)) && swi2c_send(&bus, (uint8_t)cmd);
  swi2c_stop(&bus);
  return ok;
}

/* CRC-8, polynomial 0x31, initial value 0xFF */
static uint8_t crc8(const uint8_t *d)
{
  uint8_t c = 0xFFu;

  for(int i = 0; i < 2; i++)
  {
    c ^= d[i];
    for(int b = 0; b < 8; b++)
    {
      c = (uint8_t)((c & 0x80u) != 0u ? (c << 1) ^ 0x31u : c << 1);
    }
  }
  return c;
}

bool sht3x_start(void)
{
  return command(0x2130u);      /* periodic, 1 mps, high repeatability */
}

bool sht3x_read(int16_t *t_c10, int16_t *rh_pc10)
{
  uint8_t d[6];

  if(!command(0xE000u))         /* fetch data */
  {
    return false;
  }
  swi2c_start(&bus);
  if(!swi2c_send(&bus, ADDR_R))   /* NACK: no new measurement yet */
  {
    swi2c_stop(&bus);
    return false;
  }
  for(int i = 0; i < 6; i++)
  {
    d[i] = swi2c_recv(&bus, i == 5);
  }
  swi2c_stop(&bus);
  if(crc8(&d[0]) != d[2] || crc8(&d[3]) != d[5])
  {
    return false;
  }
  *t_c10 = (int16_t)((int32_t)((d[0] << 8) | d[1]) * 1750 / 65535 - 450);
  *rh_pc10 = (int16_t)((int32_t)((d[3] << 8) | d[4]) * 1000 / 65535);
  return true;
}
