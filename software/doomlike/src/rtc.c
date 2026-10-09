#include "rtc.h"
#include "swi2c.h"

#define ADDR_W    0xA2u
#define ADDR_R    0xA3u

static const swi2c_t bus = {GPIOB, GPIO_PINS_0, GPIO_PINS_1, 5u};   /* about 100 kHz */

static uint8_t from_bcd(uint8_t v)
{
  return (uint8_t)((v >> 4) * 10u + (v & 0x0Fu));
}

static uint8_t to_bcd(uint8_t v)
{
  return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}

bool rtc_read(datetime_t *t)
{
  uint8_t r[7];

  swi2c_start(&bus);
  if(!swi2c_send(&bus, ADDR_W) || !swi2c_send(&bus, 0x02u))
  {
    swi2c_stop(&bus);
    return false;
  }
  swi2c_start(&bus);
  if(!swi2c_send(&bus, ADDR_R))
  {
    swi2c_stop(&bus);
    return false;
  }
  for(int i = 0; i < 7; i++)
  {
    r[i] = swi2c_recv(&bus, i == 6);
  }
  swi2c_stop(&bus);

  t->sec = from_bcd(r[0] & 0x7Fu);
  t->min = from_bcd(r[1] & 0x7Fu);
  t->hour = from_bcd(r[2] & 0x3Fu);
  t->day = from_bcd(r[3] & 0x3Fu);
  t->mon = from_bcd(r[5] & 0x1Fu);
  t->year = (uint16_t)(2000u + from_bcd(r[6]));
  return (r[0] & 0x80u) == 0u && t->mon >= 1u && t->mon <= 12u && t->day >= 1u;
}

/* 0 = Sunday (Sakamoto) */
static uint8_t weekday(uint16_t y, uint8_t m, uint8_t d)
{
  static const uint8_t k[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if(m < 3u)
  {
    y--;
  }
  return (uint8_t)((y + y / 4u - y / 100u + y / 400u + k[m - 1u] + d) % 7u);
}

bool rtc_write(const datetime_t *t)
{
  const uint8_t r[7] = {
    to_bcd(t->sec), to_bcd(t->min), to_bcd(t->hour), to_bcd(t->day),
    weekday(t->year, t->mon, t->day), to_bcd(t->mon), to_bcd((uint8_t)(t->year % 100u)),
  };
  bool ok;

  swi2c_start(&bus);
  ok = swi2c_send(&bus, ADDR_W) && swi2c_send(&bus, 0x02u);
  for(int i = 0; ok && i < 7; i++)
  {
    ok = swi2c_send(&bus, r[i]);
  }
  swi2c_stop(&bus);
  return ok;
}
