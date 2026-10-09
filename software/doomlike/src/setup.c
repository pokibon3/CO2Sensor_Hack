#include <stdio.h>
#include "digits.h"
#include "font.h"
#include "gfx.h"
#include "setup.h"

datetime_t clock_now = {2026u, 1u, 1u, 0u, 0u, 0u};
bool clock_valid;
int8_t batt_percent = -1;
int16_t co2_ppm = SENSOR_NONE;
int16_t temp_c10 = SENSOR_NONE;
int16_t humi_pc10 = SENSOR_NONE;

#define BG        RGB565(0, 116, 210)
#define WHITE     RGB565(255, 255, 255)
#define SELECT    RGB565(255, 230, 60)
#define REPEAT_DELAY  15u   /* ticks before a held button repeats */
#define REPEAT_EVERY  3u

enum { F_YEAR, F_MON, F_DAY, F_HOUR, F_MIN, F_COUNT };

static datetime_t edit;
static uint8_t field;
static uint8_t held;
static bool changed;
static bool left_prev, right_prev;

static uint8_t days_in(uint16_t year, uint8_t mon)
{
  static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if(mon == 2u && year % 4u == 0u)
  {
    return 29u;
  }
  return days[mon - 1u];
}

static int wrap(int v, int lo, int hi)
{
  if(v < lo) return hi;
  if(v > hi) return lo;
  return v;
}

static void step(int d)
{
  switch(field)
  {
    case F_YEAR: edit.year = (uint16_t)wrap(edit.year + d, 2000, 2099); break;
    case F_MON:  edit.mon = (uint8_t)wrap(edit.mon + d, 1, 12); break;
    case F_DAY:  edit.day = (uint8_t)wrap(edit.day + d, 1, days_in(edit.year, edit.mon)); break;
    case F_HOUR: edit.hour = (uint8_t)wrap(edit.hour + d, 0, 23); break;
    default:     edit.min = (uint8_t)wrap(edit.min + d, 0, 59); break;
  }
  if(edit.day > days_in(edit.year, edit.mon))
  {
    edit.day = days_in(edit.year, edit.mon);
  }
}

void setup_enter(void)
{
  edit = clock_now;
  if(!clock_valid)
  {
    edit = (datetime_t){2026u, 1u, 1u, 0u, 0u, 0u};
  }
  field = F_YEAR;
  held = 0u;
  changed = false;
  /* the button that opened the page must be released first */
  left_prev = right_prev = true;
}

bool setup_tick(const input_t *in, bool fire_edge)
{
  if(in->left != in->right)
  {
    bool go;
    if(!(in->left ? left_prev : right_prev))
    {
      go = true;
      held = 0u;
    }
    else
    {
      if(held < 0xFFu) held++;
      go = held >= REPEAT_DELAY && (held - REPEAT_DELAY) % REPEAT_EVERY == 0u;
    }
    if(go)
    {
      step(in->left ? -1 : 1);
      changed = true;
    }
  }
  left_prev = in->left;
  right_prev = in->right;

  if(fire_edge && ++field == F_COUNT)
  {
    if(changed)
    {
      edit.sec = 0u;
      platform_set_clock(&edit);
      clock_now = edit;
      clock_valid = true;
    }
    return true;
  }
  return false;
}

/* A field value; the selected one is yellow with a marker above it. */
static int draw_field(int x, int y, const char *s, int scale, bool selected)
{
  int w = text_width(s, scale);
  if(selected)
  {
    int cx = x + w / 2;
    for(int i = 0; i < 6; i++)
    {
      gfx_fill(cx - 6 + i, y - 14 + i, 12 - 2 * i, 1, SELECT);
    }
  }
  text_draw(x, y, s, scale, selected ? SELECT : WHITE);
  return x + w + scale;
}

void setup_draw(void)
{
  char s[8];
  int x;

  gfx_fill(0, 0, SCREEN_W, SCREEN_H, BG);
  text_center(40, "SET UP", 4, WHITE);
  gfx_fill(16, 84, SCREEN_W - 32, 2, WHITE);

  text_draw(12, 150, "DATE:", 2, WHITE);
  snprintf(s, sizeof(s), "%04u", edit.year);
  x = draw_field(78, 146, s, 3, field == F_YEAR);
  x = draw_field(x, 146, "/", 3, false);
  snprintf(s, sizeof(s), "%02u", edit.mon);
  x = draw_field(x, 146, s, 3, field == F_MON);
  x = draw_field(x, 146, "/", 3, false);
  snprintf(s, sizeof(s), "%02u", edit.day);
  draw_field(x, 146, s, 3, field == F_DAY);

  text_draw(12, 220, "TIME:", 2, WHITE);
  snprintf(s, sizeof(s), "%02u", edit.hour);
  x = draw_field(78, 216, s, 3, field == F_HOUR);
  x = draw_field(x, 216, ":", 3, false);
  snprintf(s, sizeof(s), "%02u", edit.min);
  draw_field(x, 216, s, 3, field == F_MIN);

  text_draw(12, 290, "BATT:", 2, WHITE);
  if(batt_percent >= 0)
  {
    snprintf(s, sizeof(s), "%d%%", batt_percent);
  }
  else
  {
    snprintf(s, sizeof(s), "--");
  }
  text_draw(78, 286, s, 3, WHITE);

  gfx_fill(16, 380, SCREEN_W - 32, 2, WHITE);
  text_center(400, "LEFT: -1   RIGHT: +1", 2, WHITE);
  text_center(424, field == F_MIN ? "PWR: SAVE AND EXIT" : "PWR: NEXT", 2, WHITE);
}

/* Battery outline with the charge level inside and the percentage on it. */
static void draw_battery(int x, int y)
{
  const int w = 40, h = 16;
  char s[8];
  uint16_t col = RGB565(40, 150, 50);

  gfx_fill(x, y, w, h, WHITE);
  gfx_fill(x + w, y + 5, 2, h - 10, WHITE);
  gfx_fill(x + 2, y + 2, w - 4, h - 4, RGB565(0, 0, 0));
  if(batt_percent < 0)
  {
    digits_draw(x + (w - digits_width("--", DIGITS_SMALL)) / 2, y + 4, "--", DIGITS_SMALL, WHITE);
    return;
  }
  if(batt_percent <= 20)
  {
    col = RGB565(200, 30, 30);
  }
  else if(batt_percent <= 50)
  {
    col = RGB565(170, 130, 0);
  }
  gfx_fill(x + 3, y + 3, (w - 6) * batt_percent / 100, h - 6, col);
  snprintf(s, sizeof(s), "%d%%", batt_percent);
  digits_draw(x + (w - digits_width(s, DIGITS_SMALL)) / 2 + 1, y + 4, s, DIGITS_SMALL, WHITE);
}

void status_bar_draw(void)
{
  char s[32];

  gfx_fill(0, 0, SCREEN_W, STATUS_H, RGB565(0, 0, 0));
  if(clock_valid)
  {
    snprintf(s, sizeof(s), "%04u/%02u/%02u %02u:%02u:%02u", clock_now.year, clock_now.mon,
             clock_now.day, clock_now.hour, clock_now.min, clock_now.sec);
  }
  else
  {
    snprintf(s, sizeof(s), "----/--/-- --:--:--");
  }
  digits_draw(6, 7, s, DIGITS_CLOCK, WHITE);
  draw_battery(SCREEN_W - 48, 5);

}

/* One 16-line row of the sensor strip: a label at x and the value, whose
 * integer part (everything before a '.') ends at `right`, so decimal
 * points line up.
 */
static void sensor_row(int x, int right, int y, const char *name, const char *value, uint16_t col)
{
  char whole[12];
  int n = 0;

  while(value[n] != '\0' && value[n] != '.' && n < (int)sizeof(whole) - 1)
  {
    whole[n] = value[n];
    n++;
  }
  whole[n] = '\0';
  text_draw(x, y + 5, name, 1, RGB565(160, 160, 160));
  digits_draw(right - digits_width(whole, DIGITS_16), y, whole, DIGITS_16, col);
  if(value[n] == '.')
  {
    digits_draw(right + 1, y, &value[n], DIGITS_16, col);
  }
}

static uint16_t level_color(int v, int warn, int bad)
{
  return v >= bad ? RGB565(240, 60, 50) : v >= warn ? RGB565(240, 210, 50) : RGB565(80, 220, 90);
}

void sensor_bar_draw(void)
{
  const int y = SCREEN_H - SENSOR_H;
  const uint16_t dim = RGB565(160, 160, 160);
  char s[12];
  int hp = start_health();

  gfx_fill(0, y, SCREEN_W, SENSOR_H, RGB565(0, 0, 0));

  /* left: CO2 above, the health the next game starts with below */
  if(co2_ppm == SENSOR_NONE)
  {
    sensor_row(6, 104, y + 4, "CO2", "----", dim);
  }
  else
  {
    snprintf(s, sizeof(s), "%d", co2_ppm);
    sensor_row(6, 104, y + 4, "CO2", s, level_color(co2_ppm, 1000, 1500));
  }
  text_draw(108, y + 9, "PPM", 1, dim);
  snprintf(s, sizeof(s), "%d", hp);
  sensor_row(6, 104, y + 24, "HEALTH", s, level_color(100 - hp, 25, 50));

  /* right: temperature above, humidity below, decimal points aligned */
  const int dot = SCREEN_W - 6 - digits_width(".0'C", DIGITS_16) - 1;
  if(temp_c10 == SENSOR_NONE)
  {
    snprintf(s, sizeof(s), "----");
  }
  else
  {
    int t = temp_c10 < 0 ? -temp_c10 : temp_c10;
    snprintf(s, sizeof(s), "%s%d.%d'C", temp_c10 < 0 ? "-" : "", t / 10, t % 10);
  }
  sensor_row(150, dot, y + 4, "TEMP", s, temp_c10 == SENSOR_NONE ? dim : WHITE);

  if(humi_pc10 == SENSOR_NONE)
  {
    snprintf(s, sizeof(s), "----");
  }
  else
  {
    snprintf(s, sizeof(s), "%d.%d%%", humi_pc10 / 10, humi_pc10 % 10);
  }
  sensor_row(150, dot, y + 24, "HUMI", s, humi_pc10 == SENSOR_NONE ? dim : WHITE);
}
