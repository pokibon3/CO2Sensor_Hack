#include <string.h>
#include "lt7680.h"
#include "machine.h"
#include "video.h"

/* 0: game top at panel x = 0 side, 1: rotated by 180 degrees. */
#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 0
#endif

#define PORTRAIT_W  LT_PANEL_H                      /* 272 */
#define PORTRAIT_H  LT_PANEL_W                      /* 480 */
#define SCALED_H    358u                            /* 256 * 7 / 5 */
#define TOP_MARGIN  ((PORTRAIT_H - SCALED_H) / 2u)  /* 61 */

/* Game RAM (see rom/game.asm) */
#define RAM(a)      machine_ram[(a) - 0x2000u]
#define G_STATE     0x2004u
#define G_PLX       0x200Bu
#define G_PB_ACT    0x200Cu
#define G_PB_COL    0x200Du
#define G_PB_X      0x200Eu
#define G_BOMBS     0x2010u
#define G_UFO_ACT   0x2023u
#define G_UFO_COL   0x2024u
#define G_EXP_TMR   0x2028u
#define G_EXP_COL   0x2029u
#define G_EXP_X     0x202Au
#define G_UFO_STMR  0x202Cu
#define G_UFO_SCOL  0x202Du
#define G_ALIENS    0x2040u
#define UFO_X       208u

/* Color of every VRAM byte cell (column x 8 display rows), rebuilt each
 * frame from the game state. 4 bits per cell; cell i is in byte i / 2.
 * A cell whose color changed is redrawn even if its pixels did not change.
 */
enum
{
  C_WHITE, C_CYAN, C_YELLOW, C_RED, C_PINK, C_SKY, C_LIME,
  C_ORANGE, C_GREEN, C_SHOT, C_BOMB, C_COUNT
};

static const uint8_t palette[C_COUNT][3] = {
  [C_WHITE]  = {240, 240, 240},
  [C_CYAN]   = { 80, 220, 255},
  [C_YELLOW] = {255, 220,  60},
  [C_RED]    = {255,  60,  60},
  [C_PINK]   = {255,  90, 220},
  [C_SKY]    = { 90, 170, 255},
  [C_LIME]   = {170, 255,  80},
  [C_ORANGE] = {255, 160,  40},
  [C_GREEN]  = { 60, 230,  90},
  [C_SHOT]   = {255, 255, 150},
  [C_BOMB]   = {255, 110,  70},
};

/* Alien type 0/1/2 (30/20/10 points) */
static const uint8_t alien_color[3] = {C_PINK, C_SKY, C_LIME};

static uint8_t shown[VRAM_SIZE];
static uint8_t attr[VRAM_SIZE / 2u];
static uint8_t shown_attr[VRAM_SIZE / 2u];

static inline uint32_t attr_get(const uint8_t *map, uint32_t i)
{
  return (map[i >> 1] >> ((i & 1u) * 4u)) & 0xFu;
}

static inline void attr_set(uint32_t i, uint8_t color)
{
  uint32_t shift = (i & 1u) * 4u;
  attr[i >> 1] = (uint8_t)((attr[i >> 1] & ~(0xFu << shift)) | (color << shift));
}

/* Area-weighted (box filter) scaling: each output pixel covers at most two
 * source pixels per axis. a = first source index, w = its weight (0..256).
 */
static uint8_t u_sa[PORTRAIT_W], u_w[PORTRAIT_W];
static uint8_t v_sa[SCALED_H];
static uint16_t v_w[SCALED_H];
static uint16_t sx_u0[SCREEN_W], sx_u1[SCREEN_W];
static uint16_t sy_v0[SCREEN_H], sy_v1[SCREEN_H];
static uint8_t row[PORTRAIT_H * 2u];

static void paint(uint32_t col, uint32_t width, uint32_t x, uint32_t height, uint8_t color)
{
  uint32_t b0 = x >> 3;
  uint32_t b1 = (x + height - 1u) >> 3;

  for(uint32_t c = col; c < col + width && c < SCREEN_W; c++)
  {
    for(uint32_t b = b0; b <= b1 && b < 32u; b++)
    {
      attr_set(c * 32u + b, color);
    }
  }
}

static void build_attr(void)
{
  uint8_t state = RAM(G_STATE);

  /* Background bands (VRAM byte = x / 8, x counted from the bottom). */
  memset(attr, C_WHITE * 0x11u, sizeof(attr));
  for(uint32_t c = 0; c < SCREEN_W; c++)
  {
    uint32_t a = c * 32u;
    attr_set(a + 31u, C_CYAN);                      /* SCORE / HI-SCORE */
    attr_set(a + 29u, c >= 144u ? C_YELLOW : C_WHITE); /* score digits */
    attr_set(a + 26u, C_RED);                       /* UFO row */
    attr_set(a + 0u, c < 16u ? C_WHITE : C_GREEN);  /* lives */
    attr_set(a + 1u, C_GREEN);                      /* ground line */
    attr_set(a + 4u, C_GREEN);                      /* player row */
    attr_set(a + 6u, C_GREEN);                      /* shields */
    attr_set(a + 7u, C_GREEN);
  }

  if(state == 0u)
  {
    /* Attract screen: title, and the score table rows in alien colors. */
    paint(0u, SCREEN_W, 200u, 8u, C_YELLOW);
    paint(0u, 64u, 128u, 8u, C_RED);
    paint(0u, 64u, 112u, 8u, alien_color[0]);
    paint(0u, 64u, 96u, 8u, alien_color[1]);
    paint(0u, 64u, 80u, 8u, alien_color[2]);
    return;
  }

  for(uint32_t i = 0; i < 55u; i++)
  {
    uint32_t e = G_ALIENS + i * 4u;
    if(RAM(e) != 0u && RAM(e + 3u) < 3u)
    {
      paint(RAM(e + 1u), 16u, RAM(e + 2u), 8u, alien_color[RAM(e + 3u)]);
    }
  }
  if(RAM(G_EXP_TMR) != 0u)
  {
    paint(RAM(G_EXP_COL), 16u, RAM(G_EXP_X), 8u, C_ORANGE);
  }
  if(RAM(G_UFO_STMR) != 0u)
  {
    paint(RAM(G_UFO_SCOL), 24u, UFO_X, 8u, C_ORANGE);
  }
  for(uint32_t b = 0; b < 3u; b++)
  {
    uint32_t s = G_BOMBS + b * 4u;
    if(RAM(s) != 0u)
    {
      paint(RAM(s + 1u), 1u, RAM(s + 2u), 4u, C_BOMB);
    }
  }
  if(RAM(G_PB_ACT) != 0u)
  {
    paint(RAM(G_PB_COL), 1u, RAM(G_PB_X), 4u, C_SHOT);
  }
  if(state == 3u)
  {
    paint(0u, SCREEN_W, 192u, 8u, C_RED);           /* GAME OVER */
  }
}

void video_init(void)
{
  memset(sx_u0, 0xFF, sizeof(sx_u0));
  memset(sx_u1, 0, sizeof(sx_u1));
  memset(sy_v0, 0xFF, sizeof(sy_v0));
  memset(sy_v1, 0, sizeof(sy_v1));

  for(uint32_t u = 0; u < PORTRAIT_W; u++)
  {
    /* Output pixel u covers source [u*224, (u+1)*224) / 272. */
    uint32_t start = u * SCREEN_W;
    uint32_t end = start + SCREEN_W;
    uint32_t a = start / PORTRAIT_W;
    uint32_t edge = (a + 1u) * PORTRAIT_W;
    uint32_t cover = (end < edge ? end : edge) - start;

    u_sa[u] = (uint8_t)a;
    u_w[u] = (uint8_t)((cover * 256u / SCREEN_W) > 255u ? 255u : cover * 256u / SCREEN_W);
    for(uint32_t s = a; s <= a + 1u && s < SCREEN_W; s++)
    {
      if(s == a + 1u && end <= edge)
      {
        break;
      }
      if(u < sx_u0[s]) sx_u0[s] = (uint16_t)u;
      if(u > sx_u1[s]) sx_u1[s] = (uint16_t)u;
    }
  }
  for(uint32_t v = 0; v < SCALED_H; v++)
  {
    /* Output row v covers source [5v, 5v+5) / 7. */
    uint32_t start = v * 5u;
    uint32_t end = start + 5u;
    uint32_t a = start / 7u;
    uint32_t edge = (a + 1u) * 7u;
    uint32_t cover = (end < edge ? end : edge) - start;

    v_sa[v] = (uint8_t)a;
    v_w[v] = (uint16_t)(cover * 256u / 5u);
    for(uint32_t s = a; s <= a + 1u && s < SCREEN_H; s++)
    {
      if(s == a + 1u && end <= edge)
      {
        break;
      }
      if(v + TOP_MARGIN < sy_v0[s]) sy_v0[s] = (uint16_t)(v + TOP_MARGIN);
      if(v + TOP_MARGIN > sy_v1[s]) sy_v1[s] = (uint16_t)(v + TOP_MARGIN);
    }
  }
}

/* Lit source pixel -> palette entry, unlit or off-screen -> NULL. */
static inline const uint8_t *src(const uint8_t *vram, uint32_t sx, uint32_t sy)
{
  uint32_t x, i;

  if(sx >= SCREEN_W || sy >= SCREEN_H)
  {
    return 0;
  }
  x = 255u - sy;
  i = sx * 32u + (x >> 3);
  if(((vram[i] >> (x & 7u)) & 1u) == 0u)
  {
    return 0;
  }
  return palette[attr_get(attr, i)];
}

static inline void accumulate(uint32_t *rgb, const uint8_t *c, uint32_t w)
{
  if(c != 0 && w != 0u)
  {
    rgb[0] += c[0] * w;
    rgb[1] += c[1] * w;
    rgb[2] += c[2] * w;
  }
}

static uint16_t output_pixel(const uint8_t *vram, uint32_t u, uint32_t v)
{
  uint32_t rgb[3] = {0u, 0u, 0u};
  uint32_t sxa = u_sa[u], wxa = u_w[u], wxb = 256u - wxa;
  uint32_t sya = v_sa[v], wya = v_w[v], wyb = 256u - wya;

  if(wxb < 2u)
  {
    wxa = 256u;
    wxb = 0u;
  }
  accumulate(rgb, src(vram, sxa, sya), wxa * wya);
  accumulate(rgb, src(vram, sxa + 1u, sya), wxb * wya);
  accumulate(rgb, src(vram, sxa, sya + 1u), wxa * wyb);
  accumulate(rgb, src(vram, sxa + 1u, sya + 1u), wxb * wyb);
  return LT_RGB565(rgb[0] >> 16, rgb[1] >> 16, rgb[2] >> 16);
}

/* Redraw all output pixels influenced by game columns sa..sb, rows ya..yb
 * (display rows, 0 = top).
 */
static uint32_t draw_rect(uint32_t sa, uint32_t sb, uint32_t ya, uint32_t yb)
{
  const uint8_t *vram = machine_vram();
  uint32_t u0 = sx_u0[sa], u1 = sx_u1[sb];
  uint32_t v0 = sy_v0[ya], v1 = sy_v1[yb];
  uint32_t w = v1 - v0 + 1u;
  uint32_t h = u1 - u0 + 1u;

#if DISPLAY_ROTATION == 0
  lt_write_begin((uint16_t)v0, (uint16_t)(PORTRAIT_W - 1u - u1), (uint16_t)w, (uint16_t)h);
#else
  lt_write_begin((uint16_t)(PORTRAIT_H - 1u - v1), (uint16_t)u0, (uint16_t)w, (uint16_t)h);
#endif

  for(uint32_t i = 0; i < h; i++)
  {
#if DISPLAY_ROTATION == 0
    uint32_t u = u1 - i;
#else
    uint32_t u = u0 + i;
#endif
    for(uint32_t j = 0; j < w; j++)
    {
#if DISPLAY_ROTATION == 0
      uint32_t v = v0 + j - TOP_MARGIN;
#else
      uint32_t v = v1 - j - TOP_MARGIN;
#endif
      uint16_t c = output_pixel(vram, u, v);
      row[j * 2u] = (uint8_t)c;
      row[j * 2u + 1u] = (uint8_t)(c >> 8);
    }
    lt_write_pixels(row, w * 2u);
  }
  lt_write_end();
  return w * h;
}

/* Columns sa..sb changed within VRAM bytes ba..bb (byte = x / 8). */
static uint32_t flush(uint32_t sa, uint32_t sb, uint32_t ba, uint32_t bb)
{
  uint32_t ya = 255u - (bb * 8u + 7u);
  uint32_t yb = 255u - ba * 8u;
  uint32_t n = draw_rect(sa, sb, ya, yb);

  memcpy(&shown[sa * 32u], &machine_vram()[sa * 32u], (sb - sa + 1u) * 32u);
  memcpy(&shown_attr[sa * 16u], &attr[sa * 16u], (sb - sa + 1u) * 16u);
  return n;
}

void video_full_redraw(void)
{
  build_attr();
  (void)draw_rect(0u, SCREEN_W - 1u, 0u, SCREEN_H - 1u);
  memcpy(shown, machine_vram(), VRAM_SIZE);
  memcpy(shown_attr, attr, sizeof(attr));
}

uint32_t video_update(void)
{
  const uint8_t *vram = machine_vram();
  uint32_t pixels = 0u;
  int32_t run = -1;
  uint32_t ba = 0u, bb = 0u;

  build_attr();
  for(uint32_t sx = 0; sx <= SCREEN_W; sx++)
  {
    int32_t lo = -1, hi = -1;

    if(sx < SCREEN_W)
    {
      const uint8_t *a = &vram[sx * 32u];
      const uint8_t *b = &shown[sx * 32u];
      for(int32_t i = 0; i < 32; i++)
      {
        uint32_t cell = sx * 32u + (uint32_t)i;
        if(a[i] != b[i] || attr_get(attr, cell) != attr_get(shown_attr, cell))
        {
          if(lo < 0)
          {
            lo = i;
          }
          hi = i;
        }
      }
    }

    /* Extend the current run only with overlapping or nearby byte ranges. */
    if(run >= 0 && (lo < 0 || lo > (int32_t)bb + 2 || hi + 2 < (int32_t)ba))
    {
      pixels += flush((uint32_t)run, sx - 1u, ba, bb);
      run = -1;
    }
    if(lo >= 0)
    {
      if(run < 0)
      {
        run = (int32_t)sx;
        ba = (uint32_t)lo;
        bb = (uint32_t)hi;
      }
      else
      {
        ba = (uint32_t)lo < ba ? (uint32_t)lo : ba;
        bb = (uint32_t)hi > bb ? (uint32_t)hi : bb;
      }
    }
  }
  return pixels;
}
