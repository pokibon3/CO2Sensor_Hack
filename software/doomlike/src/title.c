#include "font.h"
#include "game_int.h"
#include "gfx.h"
#include "setup.h"
#include "title.h"

/* DOOM title page: a bevelled orange logo with a perspective flare over a
 * burning sky, black mountains and a glowing gate with an imp in it. It is
 * about 2000 rectangles, so it is drawn once per buffer and afterwards only
 * the blinking prompt is redrawn.
 */

#define GROUND_Y   330
#define PROMPT_Y   356
#define PROMPT_H   28
#define GROUND_COL RGB565(14, 4, 4)

#define GLYPH_W    9
#define GLYPH_H    11

typedef struct
{
  char c;
  const char *rows[GLYPH_H];
} glyph_t;

static const glyph_t glyphs[] = {
  {'D', {"########.", "#########", "###...###", "###...###", "###...###", "###...###",
         "###...###", "###...###", "###...###", "#########", "########."}},
  {'E', {"#########", "#########", "###......", "###......", "#######..", "#######..",
         "###......", "###......", "###......", "#########", "#########"}},
  {'M', {"###...###", "####.####", "#########", "#########", "###.#.###", "###...###",
         "###...###", "###...###", "###...###", "###...###", "###...###"}},
  {'O', {"#########", "#########", "###...###", "###...###", "###...###", "###...###",
         "###...###", "###...###", "###...###", "#########", "#########"}},
  {'N', {"###...###", "####..###", "####..###", "#####.###", "#########", "###.#####",
         "###..####", "###..####", "###...###", "###...###", "###...###"}},
  {'G', {".########", "#########", "###......", "###......", "###......", "###..####",
         "###..####", "###...###", "###...###", "#########", ".#######."}},
  {'A', {"..#####..", ".#######.", "###...###", "###...###", "###...###", "#########",
         "#########", "###...###", "###...###", "###...###", "###...###"}},
  {'T', {"#########", "#########", "...###...", "...###...", "...###...", "...###...",
         "...###...", "...###...", "...###...", "...###...", "...###..."}},
};

static const glyph_t *glyph(char c)
{
  for(unsigned i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++)
  {
    if(glyphs[i].c == c)
    {
      return &glyphs[i];
    }
  }
  return 0;
}

static int lerp(int a, int b, int t, int n)
{
  return a + (b - a) * t / n;
}

/* Colour from a list of (position, r, g, b) keys. */
static uint16_t ramp(const int16_t (*keys)[4], int count, int pos)
{
  for(int i = 1; i < count; i++)
  {
    if(pos <= keys[i][0] || i == count - 1)
    {
      int n = keys[i][0] - keys[i - 1][0];
      int t = pos - keys[i - 1][0];
      if(t < 0) t = 0;
      if(t > n) t = n;
      return RGB565(lerp(keys[i - 1][1], keys[i][1], t, n), lerp(keys[i - 1][2], keys[i][2], t, n),
                    lerp(keys[i - 1][3], keys[i][3], t, n));
    }
  }
  return 0;
}

static const int16_t sky_keys[][4] = {
  {0, 8, 0, 0}, {110, 50, 4, 0}, {210, 140, 26, 4}, {GROUND_Y, 245, 120, 24},
};

static uint32_t rng = 0x2545F491u;

static int rnd(int n)
{
  rng = rng * 1664525u + 1013904223u;
  return (int)((rng >> 16) % (uint32_t)n);
}

static void draw_sky(void)
{
  for(int y = 0; y < GROUND_Y; y += 3)
  {
    gfx_fill(0, y, SCREEN_W, 3, ramp(sky_keys, 4, y));
  }
  /* streaks of cloud, darker high up and glowing near the horizon */
  rng = 0x2545F491u;
  for(int i = 0; i < 36; i++)
  {
    int y = 30 + rnd(240);
    int w = 24 + rnd(80);
    int x = rnd(SCREEN_W + 40) - 40;
    int h = 2 + rnd(3);
    int pos = y + (i % 3 == 0 ? 40 : -30);
    gfx_fill(x, y, w, h, ramp(sky_keys, 4, pos));
    gfx_fill(x + w / 4, y + h, w / 2, 2, ramp(sky_keys, 4, pos - 20));
  }
}

static void draw_mountains(void)
{
  int h = 40;

  rng = 0x9E3779B9u;
  for(int x = 0; x < SCREEN_W; x += 4)
  {
    int top;
    h += rnd(13) - 6;
    /* higher at the sides, low behind the gate */
    int side = x < SCREEN_W / 2 ? SCREEN_W / 2 - x : x - SCREEN_W / 2;
    if(h < 10) h = 10;
    if(h > 60) h = 60;
    top = GROUND_Y - h - side / 3;
    gfx_fill(x, top, 4, GROUND_Y - top, RGB565(28, 6, 6));
    gfx_fill(x, top, 4, 2, RGB565(200, 70, 20));
  }
}

static void draw_gate(void)
{
  static const int16_t glow_keys[][4] = {
    {0, 120, 10, 0}, {5, 230, 90, 10}, {10, 255, 220, 120},
  };
  const art_t *imp = &arts[ART_IMP_ATK];
  const int x0 = 84, x1 = 188, top = 180, inner_x0 = 106, inner_x1 = 166, inner_top = 210;
  const int scale = 4;
  int ix = (inner_x0 + inner_x1 - imp->w * scale) / 2;
  int iy = GROUND_Y - imp->h * scale;

  /* light thrown on the ground in front of the gate */
  for(int i = 0; i < 6; i++)
  {
    int w = (inner_x1 - inner_x0) + i * 24;
    gfx_fill((SCREEN_W - w) / 2, GROUND_Y + i * 4, w, 4, RGB565(150 - i * 22, 40 - i * 6, 6));
  }

  /* stone frame with block joints and horns on the lintel */
  gfx_fill(x0 - 2, top - 2, x1 - x0 + 4, GROUND_Y - top + 2, RGB565(20, 10, 10));
  gfx_fill(x0, top, x1 - x0, GROUND_Y - top, RGB565(90, 70, 62));
  for(int y = top + 14; y < GROUND_Y; y += 16)
  {
    gfx_fill(x0, y, inner_x0 - x0, 2, RGB565(50, 38, 34));
    gfx_fill(inner_x1, y, x1 - inner_x1, 2, RGB565(50, 38, 34));
  }
  gfx_fill(x0, top, x1 - x0, 3, RGB565(150, 120, 100));
  for(int i = 0; i < 4; i++)
  {
    gfx_fill(x0 + 4 + i * 3, top - 6 - i * 6, 6, 6, RGB565(70, 55, 48));
    gfx_fill(x1 - 10 - i * 3, top - 6 - i * 6, 6, 6, RGB565(70, 55, 48));
  }

  /* portal glowing from the edge to the centre */
  for(int i = 0; i <= 10; i++)
  {
    int w = inner_x1 - inner_x0 - i * 4;
    int y = inner_top + i * 4;
    gfx_fill(inner_x0 + i * 2, y, w, GROUND_Y - y, ramp(glow_keys, 3, i));
  }

  /* the imp as a black shape, eyes and fire still lit */
  for(int j = 0; j < imp->h; j++)
  {
    for(int i = 0; i < imp->w;)
    {
      char ch = imp->rows[j][i];
      int i0 = i;
      while(i < imp->w && imp->rows[j][i] == ch)
      {
        i++;
      }
      if(ch != '.')
      {
        uint16_t col = ch == 'y' ? RGB565(255, 60, 20) : ch == 'o' ? RGB565(255, 200, 60) : RGB565(16, 4, 4);
        gfx_fill(ix + i0 * scale, iy + j * scale, (i - i0) * scale, scale, col);
      }
    }
  }
}

static void draw_ground(void)
{
  gfx_fill(0, GROUND_Y, SCREEN_W, SCREEN_H - GROUND_Y, GROUND_COL);
  gfx_fill(0, GROUND_Y, SCREEN_W, 2, RGB565(90, 30, 10));
  /* lava cracks between the horizon and the prompt */
  rng = 0x6C8E9CF5u;
  for(int i = 0; i < 14; i++)
  {
    int x = rnd(SCREEN_W);
    int y = GROUND_Y + 4 + rnd(PROMPT_Y - GROUND_Y - 10);
    for(int k = 0; k < 4; k++)
    {
      int w = 6 + rnd(14);
      gfx_fill(x, y, w, 2, RGB565(200, 60 + rnd(60), 10));
      x += w;
      y += rnd(3) - 1;
    }
  }
}

/* Logo letters drawn cell by cell. Each column is stretched by its distance
 * from the centre, so the bottom edge curves down towards the ends (the
 * perspective of the DOOM logo).
 */
static void draw_logo(const char *s, int y, int cell, int flare)
{
  static const int16_t face_keys[][4] = {
    {0, 255, 235, 110}, {4, 245, 150, 30}, {8, 200, 60, 10}, {10, 140, 25, 5},
  };
  const int half = SCREEN_W / 2;
  int n = 0;
  int gap = cell;
  int x0;

  while(s[n] != 0)
  {
    n++;
  }
  x0 = (SCREEN_W - (n * GLYPH_W * cell + (n - 1) * gap)) / 2;

  for(int pass = 0; pass < 4; pass++)
  {
    for(int l = 0; l < n; l++)
    {
      const glyph_t *gl = glyph(s[l]);
      int lx = x0 + l * (GLYPH_W * cell + gap);

      if(gl == 0)
      {
        continue;
      }
      for(int i = 0; i < GLYPH_W; i++)
      {
        int x = lx + i * cell;
        int dx = x + cell / 2 - half;
        int h = GLYPH_H * cell + flare * dx * dx / (half * half);

        for(int j = 0; j < GLYPH_H; j++)
        {
          int y0 = y + j * h / GLYPH_H;
          int y1 = y + (j + 1) * h / GLYPH_H;

          if(gl->rows[j][i] != '#')
          {
            continue;
          }
          switch(pass)
          {
          case 0:   /* drop shadow */
            gfx_fill(x + 3, y0 + 4, cell + 3, y1 - y0 + 3, RGB565(10, 0, 0));
            break;
          case 1:   /* dark outline */
            gfx_fill(x - 2, y0 - 2, cell + 4, y1 - y0 + 4, RGB565(60, 12, 0));
            break;
          case 2:   /* face, yellow at the top to red at the bottom */
            gfx_fill(x, y0, cell, y1 - y0, ramp(face_keys, 4, j));
            break;
          default:  /* bevel: light on open top/left edges, dark on bottom */
            if(j == 0 || gl->rows[j - 1][i] != '#')
            {
              gfx_fill(x, y0, cell, 2, RGB565(255, 250, 200));
            }
            if(i == 0 || gl->rows[j][i - 1] != '#')
            {
              gfx_fill(x, y0, 2, y1 - y0, RGB565(255, 210, 120));
            }
            if(j == GLYPH_H - 1 || gl->rows[j + 1][i] != '#')
            {
              gfx_fill(x, y1 - 2, cell, 2, RGB565(110, 20, 0));
            }
            break;
          }
        }
      }
    }
  }
}

static void draw_prompt(bool on)
{
  gfx_fill(0, PROMPT_Y, SCREEN_W, PROMPT_H, GROUND_COL);
  if(on)
  {
    text_center(PROMPT_Y + 3, "PRESS SHOOT", 3, RGB565(230, 40, 30));
  }
}

void title_draw(bool full, bool prompt)
{
  if(full)
  {
    draw_sky();
    draw_mountains();
    draw_ground();
    draw_gate();
    draw_logo("DOOM", 44, 6, 48);
    text_center(386, "LEFT/RIGHT: TURN", 1, RGB565(190, 170, 160));
    text_center(398, "BOTH: FORWARD", 1, RGB565(190, 170, 160));
    text_center(410, "PWR: SHOOT", 1, RGB565(190, 170, 160));
    text_center(422, "PWR TWICE: SET UP", 1, RGB565(190, 170, 160));
  }
  draw_prompt(prompt);
  status_bar_draw();
  sensor_bar_draw();
}
