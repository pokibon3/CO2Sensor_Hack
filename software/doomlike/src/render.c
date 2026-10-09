#include <math.h>
#include <stdio.h>
#include "font.h"
#include "game.h"
#include "game_int.h"
#include "gfx.h"

/* Portrait layout: 3D view on top, status panel below. */
#define VIEW_H     304
#define HORIZON    (VIEW_H / 2)
#define HUD_Y      VIEW_H
#define COL_W      2
#define RAYS       (SCREEN_W / COL_W)
#define FOCAL      206.0f              /* (SCREEN_W / 2) / 0.66 */
#define FOG_Q16    (4 << 16)           /* half brightness at 4 cells */
#define MAX_SPRITES 24

#define MM_X       8
#define MM_Y       (HUD_Y + 12)
#define MM_CELL    7
#define PANEL_X    160

static const uint8_t wall_rgb[CELL_TYPES][3] = {
  {0, 0, 0},
  {150, 150, 150},   /* stone */
  {175, 75, 50},     /* brick */
  {70, 100, 180},    /* tech */
  {150, 105, 60},    /* wood */
  {40, 190, 70},     /* exit */
};
static const uint8_t floor_rgb[3] = {115, 90, 65};
static const uint8_t ceil_rgb[3] = {75, 75, 90};
static const uint8_t band_rgb[3] = {150, 230, 255};
static const uint8_t switch_rgb[3] = {250, 220, 60};

static float zbuf[RAYS];

/* Fill clipped to the 3D view. */
static void view_fill(int x, int y, int w, int h, uint16_t color)
{
  if(y < 0)
  {
    h += y;
    y = 0;
  }
  if(y + h > VIEW_H)
  {
    h = VIEW_H - y;
  }
  if(h > 0)
  {
    gfx_fill(x, y, w, h, color);
  }
}

/* ---- colours ---- */

static uint16_t shade(const uint8_t rgb[3], int b)
{
  int r = rgb[0] * b >> 8;
  int gr = rgb[1] * b >> 8;
  int bl = rgb[2] * b >> 8;

  if(g.player.hurt != 0u)
  {
    r = r / 2 + 120;
    gr /= 2;
    bl /= 2;
  }
  else if(g.player.bonus != 0u)
  {
    r = r / 2 + 100;
    gr = gr / 2 + 90;
    bl /= 2;
  }
  if(r > 255) r = 255;
  if(gr > 255) gr = 255;
  if(bl > 255) bl = 255;
  return RGB565(r, gr, bl);
}

/* Brightness 0..256 for a distance (Q16), in steps of 16 so colours stay
 * steady while moving.
 */
static int fog(int32_t d_q16)
{
  int b = (int)(((int64_t)256 * FOG_Q16) / (FOG_Q16 + d_q16));
  return (b + 8) & ~15;
}

/* ---- floor and ceiling: bands of equal brightness ---- */

static void draw_background(void)
{
  int y = HORIZON;

  while(y < VIEW_H)
  {
    int y0 = y;
    float d = 0.5f * FOCAL / ((float)(y - HORIZON) + 0.5f);
    int b = fog((int32_t)(d * 65536.0f));

    for(y++; y < VIEW_H; y++)
    {
      float d2 = 0.5f * FOCAL / ((float)(y - HORIZON) + 0.5f);
      if(fog((int32_t)(d2 * 65536.0f)) != b)
      {
        break;
      }
    }
    gfx_fill(0, y0, SCREEN_W, y - y0, shade(floor_rgb, b));
    gfx_fill(0, 2 * HORIZON - y, SCREEN_W, y - y0, shade(ceil_rgb, b * 3 / 4));
  }
}

/* ---- walls: one DDA ray per COL_W pixels ---- */

static void draw_walls(void)
{
  const player_t *p = &g.player;
  float cs = cosf(p->angle), sn = sinf(p->angle);
  int32_t dir_x = (int32_t)(cs * 65536.0f), dir_y = (int32_t)(sn * 65536.0f);
  int32_t pl_x = (int32_t)(-sn * 0.66f * 65536.0f), pl_y = (int32_t)(cs * 0.66f * 65536.0f);
  int32_t px = (int32_t)(p->x * 65536.0f), py = (int32_t)(p->y * 65536.0f);

  for(int c = 0; c < RAYS; c++)
  {
    int32_t cam = (int32_t)(((int64_t)(2 * c + 1) << 16) / RAYS) - 65536;
    int32_t rx = dir_x + (int32_t)(((int64_t)pl_x * cam) >> 16);
    int32_t ry = dir_y + (int32_t)(((int64_t)pl_y * cam) >> 16);
    int32_t ddx = rx == 0 ? 0x10000000 : (int32_t)((1LL << 32) / (rx < 0 ? -rx : rx));
    int32_t ddy = ry == 0 ? 0x10000000 : (int32_t)((1LL << 32) / (ry < 0 ? -ry : ry));
    int mx = px >> 16, my = py >> 16;
    int sx, sy, side = 0;
    int32_t dist_x, dist_y, perp, frac;
    uint8_t cell = CELL_STONE;

    if(ddx > 0x10000000) ddx = 0x10000000;
    if(ddy > 0x10000000) ddy = 0x10000000;
    if(rx < 0)
    {
      sx = -1;
      dist_x = (int32_t)(((int64_t)(px & 0xFFFF) * ddx) >> 16);
    }
    else
    {
      sx = 1;
      dist_x = (int32_t)(((int64_t)(0x10000 - (px & 0xFFFF)) * ddx) >> 16);
    }
    if(ry < 0)
    {
      sy = -1;
      dist_y = (int32_t)(((int64_t)(py & 0xFFFF) * ddy) >> 16);
    }
    else
    {
      sy = 1;
      dist_y = (int32_t)(((int64_t)(0x10000 - (py & 0xFFFF)) * ddy) >> 16);
    }

    for(int n = 0; n < 64; n++)
    {
      if(dist_x < dist_y)
      {
        dist_x += ddx;
        mx += sx;
        side = 0;
      }
      else
      {
        dist_y += ddy;
        my += sy;
        side = 1;
      }
      if(map_solid(mx, my))
      {
        cell = (mx < 0 || my < 0 || mx >= MAP_W || my >= MAP_H) ? CELL_STONE : g.map[my][mx];
        break;
      }
    }

    perp = side == 0 ? dist_x - ddx : dist_y - ddy;
    if(perp < 0x400)
    {
      perp = 0x400;
    }
    if(side == 0)
    {
      frac = (py + (int32_t)(((int64_t)perp * ry) >> 16)) & 0xFFFF;
    }
    else
    {
      frac = (px + (int32_t)(((int64_t)perp * rx) >> 16)) & 0xFFFF;
    }
    zbuf[c] = (float)perp / 65536.0f;

    {
      int h = (int)(((int64_t)FOCAL * 65536) / perp);
      int top = HORIZON - h / 2;
      int b = fog(perp);
      int x = c * COL_W;

      if(side == 1)
      {
        b = b * 3 / 4;
      }
      if(frac < 0x0C00 || frac > 0xF400)
      {
        b = b * 5 / 8;              /* block seams */
      }
      view_fill(x, top, COL_W, h, shade(wall_rgb[cell], b));
      if(cell == CELL_TECH)
      {
        view_fill(x, top + h * 7 / 16, COL_W, h / 8 + 1, shade(band_rgb, b));
      }
      else if(cell == CELL_EXIT && frac > 0x5000 && frac < 0xB000)
      {
        view_fill(x, top + h * 3 / 8, COL_W, h / 4, shade(switch_rgb, b));
      }
    }
  }
}

/* ---- sprites ---- */

typedef struct
{
  const actor_t *a;
  float tx, ty;
} vis_t;

static uint8_t actor_art(const actor_t *a)
{
  bool step = ((a->anim >> 3) & 1u) != 0u;

  switch(a->type)
  {
    case A_IMP:
      if(a->state == S_DEAD) return ART_IMP_DEAD;
      if(a->state == S_ATTACK) return ART_IMP_ATK;
      return step ? ART_IMP_B : ART_IMP_A;
    case A_BRUTE:
      if(a->state == S_DEAD) return ART_BRUTE_DEAD;
      if(a->state == S_ATTACK) return ART_BRUTE_ATK;
      return step ? ART_BRUTE_B : ART_BRUTE_A;
    case A_FIREBALL: return ART_FIREBALL;
    case A_HEALTH: return ART_HEALTH;
    default: return ART_AMMO;
  }
}

/* Draw art column by column, clipped against the wall depth buffer. */
static void draw_sprite(const vis_t *v)
{
  const actor_t *a = v->a;
  const art_t *art = &arts[actor_art(a)];
  float texel = a->type == A_BRUTE ? 0.055f : a->type == A_IMP ? 0.05f : 0.04f;
  float ts = FOCAL * texel / v->ty;
  float cx = (float)SCREEN_W * 0.5f * (1.0f + v->tx / v->ty);
  float lift = a->type == A_FIREBALL ? 0.3f : 0.0f;
  float bottom = (float)HORIZON + FOCAL * (0.5f - lift) / v->ty;
  float left = cx - ts * (float)art->w * 0.5f;
  float top = bottom - ts * (float)art->h;
  int b = fog((int32_t)(v->ty * 65536.0f));

  if(a->type == A_FIREBALL)
  {
    b = 256;                          /* glows */
  }

  for(int i = 0; i < art->w; i++)
  {
    int x0 = (int)(left + ts * (float)i);
    int x1 = (int)(left + ts * (float)(i + 1));
    int x = x0 < 0 ? 0 : x0;

    if(x1 > SCREEN_W) x1 = SCREEN_W;
    while(x < x1)
    {
      /* find a span of columns in front of the walls */
      int r = x / COL_W;
      if(zbuf[r] <= v->ty)
      {
        x = (r + 1) * COL_W;
        continue;
      }
      int s = x;
      while(x < x1 && zbuf[x / COL_W] > v->ty)
      {
        x++;
      }
      for(int j = 0; j < art->h;)
      {
        char ch = art->rows[j][i];
        int j0 = j;
        const uint8_t *rgb;

        while(j < art->h && art->rows[j][i] == ch)
        {
          j++;
        }
        rgb = palette_rgb(ch);
        if(rgb != 0)
        {
          int y0 = (int)(top + ts * (float)j0);
          int y1 = (int)(top + ts * (float)j);
          if(y1 > y0)
          {
            uint16_t col = a->flash != 0u ? RGB565(255, 255, 255) : shade(rgb, b);
            view_fill(s, y0, x - s, y1 - y0, col);
          }
        }
      }
    }
  }
}

static void draw_sprites(void)
{
  const player_t *p = &g.player;
  float dx = cosf(p->angle), dy = sinf(p->angle);
  float plx = -dy * 0.66f, ply = dx * 0.66f;
  float inv = 1.0f / (plx * dy - dx * ply);
  vis_t vis[MAX_SPRITES];
  int n = 0;

  for(int i = 0; i < MAX_ACTORS && n < MAX_SPRITES; i++)
  {
    const actor_t *a = &g.actors[i];
    float sx, sy, tx, ty;

    if(a->type == A_NONE)
    {
      continue;
    }
    sx = a->x - p->x;
    sy = a->y - p->y;
    tx = inv * (dy * sx - dx * sy);
    ty = inv * (-ply * sx + plx * sy);
    if(ty < 0.25f || fabsf(tx) > ty + 1.0f)
    {
      continue;
    }
    /* insertion sort, far to near */
    int k = n++;
    while(k > 0 && vis[k - 1].ty < ty)
    {
      vis[k] = vis[k - 1];
      k--;
    }
    vis[k] = (vis_t){a, tx, ty};
  }
  for(int i = 0; i < n; i++)
  {
    draw_sprite(&vis[i]);
  }
}

/* ---- weapon and overlays ---- */

static void draw_art_rows(const art_t *art, int x, int y, int scale, bool tint, int clip_y)
{
  for(int j = 0; j < art->h; j++)
  {
    for(int i = 0; i < art->w;)
    {
      char ch = art->rows[j][i];
      int i0 = i;
      const uint8_t *rgb;

      while(i < art->w && art->rows[j][i] == ch)
      {
        i++;
      }
      rgb = palette_rgb(ch);
      if(rgb != 0)
      {
        uint16_t col = tint ? shade(rgb, 256) : RGB565(rgb[0], rgb[1], rgb[2]);
        int y0 = y + j * scale;
        int h = y0 + scale > clip_y ? clip_y - y0 : scale;
        if(h > 0)
        {
          gfx_fill(x + i0 * scale, y0, (i - i0) * scale, h, col);
        }
      }
    }
  }
}

static void draw_weapon(void)
{
  const player_t *p = &g.player;
  const art_t *gun = &arts[ART_GUN];
  const int scale = 5;
  int bx = (int)(sinf(p->bob) * 7.0f);
  int by = (int)(fabsf(cosf(p->bob)) * 6.0f);
  int recoil = p->muzzle != 0u ? 8 : 0;
  int x = SCREEN_W / 2 - gun->w * scale / 2 + bx;
  int y = VIEW_H - gun->h * scale + 10 + by + recoil;

  if(p->muzzle != 0u)
  {
    const art_t *fl = &arts[ART_FLASH];
    draw_art_rows(fl, SCREEN_W / 2 - fl->w * scale / 2 + bx, y - fl->h * scale + scale, scale, false, VIEW_H);
  }
  draw_art_rows(gun, x, y, scale, true, VIEW_H);

  /* crosshair */
  gfx_fill(SCREEN_W / 2 - 5, HORIZON, 4, 2, RGB565(255, 255, 255));
  gfx_fill(SCREEN_W / 2 + 3, HORIZON, 4, 2, RGB565(255, 255, 255));
}

/* ---- status panel (redrawn per buffer only when something changed) ---- */

typedef struct
{
  bool valid;
  uint8_t level;
  int16_t hp, ammo, kills;
  int8_t mx, my;
} hud_cache_t;

static hud_cache_t hud[2];

static uint16_t cell_color(int x, int y)
{
  uint8_t c = g.map[y][x];
  if(c == CELL_FLOOR) return RGB565(40, 36, 32);
  if(c == CELL_EXIT) return RGB565(40, 200, 70);
  return RGB565(wall_rgb[c][0] * 3 / 4, wall_rgb[c][1] * 3 / 4, wall_rgb[c][2] * 3 / 4);
}

static void draw_minimap(void)
{
  for(int y = 0; y < MAP_H; y++)
  {
    for(int x = 0; x < MAP_W;)
    {
      uint16_t c = cell_color(x, y);
      int x0 = x;
      while(x < MAP_W && cell_color(x, y) == c)
      {
        x++;
      }
      gfx_fill(MM_X + x0 * MM_CELL, MM_Y + y * MM_CELL, (x - x0) * MM_CELL, MM_CELL, c);
    }
  }
}

static void draw_number(int x, int y, int w, int value, int scale, uint16_t color)
{
  char s[8];
  snprintf(s, sizeof(s), "%d", value);
  gfx_fill(x, y, w, 7 * scale, RGB565(30, 30, 36));
  text_draw(x, y, s, scale, color);
}

static void draw_hud(void)
{
  hud_cache_t *h = &hud[gfx_back()];
  const player_t *p = &g.player;
  int mx = (int)p->x, my = (int)p->y;

  if(!h->valid || h->level != g.level)
  {
    char s[16];
    gfx_fill(0, HUD_Y, SCREEN_W, SCREEN_H - HUD_Y, RGB565(30, 30, 36));
    gfx_fill(0, HUD_Y, SCREEN_W, 3, RGB565(140, 30, 30));
    draw_minimap();
    text_draw(PANEL_X, HUD_Y + 12, "HEALTH", 2, RGB565(160, 160, 160));
    text_draw(PANEL_X, HUD_Y + 70, "AMMO", 2, RGB565(160, 160, 160));
    snprintf(s, sizeof(s), "LEVEL %d", g.level + 1);
    text_draw(PANEL_X, HUD_Y + 128, s, 2, RGB565(160, 160, 160));
    text_draw(PANEL_X, HUD_Y + 152, "KILLS", 2, RGB565(160, 160, 160));
    h->valid = true;
    h->level = g.level;
    h->hp = h->ammo = h->kills = -1;
    h->mx = h->my = -1;
  }
  if(h->hp != p->hp)
  {
    uint16_t c = p->hp > 50 ? RGB565(80, 220, 80) : p->hp > 25 ? RGB565(240, 200, 40) : RGB565(240, 50, 40);
    draw_number(PANEL_X, HUD_Y + 30, 106, p->hp, 4, c);
    h->hp = p->hp;
  }
  if(h->ammo != p->ammo)
  {
    draw_number(PANEL_X, HUD_Y + 88, 106, p->ammo, 4, RGB565(240, 200, 40));
    h->ammo = p->ammo;
  }
  if(h->kills != g.kills)
  {
    char s[12];
    snprintf(s, sizeof(s), "%d/%d", g.kills, g.total);
    gfx_fill(PANEL_X + 66, HUD_Y + 152, SCREEN_W - PANEL_X - 66, 14, RGB565(30, 30, 36));
    text_draw(PANEL_X + 66, HUD_Y + 152, s, 2, RGB565(220, 220, 220));
    h->kills = g.kills;
  }
  if(h->mx != mx || h->my != my)
  {
    if(h->mx >= 0)
    {
      gfx_fill(MM_X + h->mx * MM_CELL, MM_Y + h->my * MM_CELL, MM_CELL, MM_CELL, cell_color(h->mx, h->my));
    }
    gfx_fill(MM_X + mx * MM_CELL + 1, MM_Y + my * MM_CELL + 1, MM_CELL - 2, MM_CELL - 2, RGB565(255, 255, 80));
    h->mx = (int8_t)mx;
    h->my = (int8_t)my;
  }
}

/* ---- full-screen pages ---- */

static void draw_title(void)
{
  const art_t *imp = &arts[ART_IMP_ATK];

  gfx_fill(0, 0, SCREEN_W, SCREEN_H, RGB565(12, 6, 6));
  text_center(40, "DEMON", 7, RGB565(220, 40, 30));
  text_center(104, "GATE", 7, RGB565(220, 40, 30));
  draw_art_rows(imp, (SCREEN_W - imp->w * 8) / 2, 176, 8, false, SCREEN_H);
  text_center(320, "LEFT/RIGHT: TURN", 2, RGB565(200, 200, 200));
  text_center(344, "BOTH: FORWARD", 2, RGB565(200, 200, 200));
  text_center(368, "FIRE: SHOOT", 2, RGB565(200, 200, 200));
  if((g.timer / 15u) % 2u == 0u)
  {
    text_center(416, "PRESS FIRE", 3, RGB565(250, 220, 60));
  }
}

static void draw_win(void)
{
  char s[24];
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, RGB565(6, 12, 6));
  text_center(120, "YOU WIN!", 5, RGB565(80, 230, 90));
  text_center(200, "THE GATE IS SEALED", 2, RGB565(200, 200, 200));
  snprintf(s, sizeof(s), "HEALTH %d", g.player.hp);
  text_center(250, s, 2, RGB565(200, 200, 200));
  if(g.timer > GAME_HZ && (g.timer / 15u) % 2u == 0u)
  {
    text_center(400, "PRESS FIRE", 3, RGB565(250, 220, 60));
  }
}

void game_render(void)
{
  gfx_frame_begin();
  if(g.state == ST_TITLE || g.state == ST_WIN)
  {
    hud[0].valid = hud[1].valid = false;
    if(g.state == ST_TITLE)
    {
      draw_title();
    }
    else
    {
      draw_win();
    }
  }
  else
  {
    draw_background();
    draw_walls();
    draw_sprites();
    if(g.state != ST_DEAD)
    {
      draw_weapon();
    }
    if(g.state == ST_CLEAR)
    {
      text_center(110, "LEVEL CLEAR", 3, RGB565(80, 230, 90));
    }
    else if(g.state == ST_DEAD)
    {
      text_center(110, "YOU DIED", 4, RGB565(230, 40, 30));
      if(g.timer > GAME_HZ)
      {
        text_center(170, "PRESS FIRE", 2, RGB565(250, 220, 60));
      }
    }
    draw_hud();
  }
  gfx_frame_end();
}
