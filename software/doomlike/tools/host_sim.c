/* Host build of the game with a software framebuffer. Plays a scripted
 * input sequence and writes selected frames as PPM.
 *
 *   cc -O2 -Isrc -o host_sim tools/host_sim.c src/game.c src/render.c \
 *      src/font.c src/levels.c src/art.c src/textures.c -lm
 *   ./host_sim OUTDIR
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "game_int.h"
#include "gfx.h"
#include "sound.h"

static uint16_t fb[2][SCREEN_H][SCREEN_W];
static uint8_t back = 1u;
uint32_t gfx_rects;
static uint32_t rects, max_rects;

void gfx_init(void) {}
void gfx_frame_begin(void) { rects = 0u; }
void gfx_frame_end(void)
{
  back ^= 1u;
  gfx_rects = rects;
  if(rects > max_rects) max_rects = rects;
}
uint8_t gfx_back(void) { return back; }

void gfx_fill(int x, int y, int w, int h, uint16_t c)
{
  int x1 = x + w, y1 = y + h;
  if(x < 0) x = 0;
  if(y < 0) y = 0;
  if(x1 > SCREEN_W) x1 = SCREEN_W;
  if(y1 > SCREEN_H) y1 = SCREEN_H;
  if(x >= x1 || y >= y1) return;
  rects++;
  for(int j = y; j < y1; j++)
    for(int i = x; i < x1; i++)
      fb[back][j][i] = c;
}

static int sfx_count[SFX_COUNT];
void sound_play(int s) { sfx_count[s]++; }

static void dump(const char *dir, int frame)
{
  char path[256];
  FILE *f;
  snprintf(path, sizeof(path), "%s/frame%05d.ppm", dir, frame);
  f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  /* the shown buffer is the one not being drawn */
  for(int j = 0; j < SCREEN_H; j++)
    for(int i = 0; i < SCREEN_W; i++)
    {
      uint16_t c = fb[back ^ 1u][j][i];
      uint8_t px[3] = {(uint8_t)((c >> 8) & 0xF8), (uint8_t)((c >> 3) & 0xFC), (uint8_t)(c << 3)};
      fwrite(px, 1, 3, f);
    }
  fclose(f);
}

static void check_levels(void)
{
  for(int l = 0; l < LEVEL_COUNT; l++)
    for(int y = 0; y < MAP_H; y++)
    {
      const char *r = levels[l][y];
      if((int)strlen(r) != MAP_W) { printf("level %d row %d: length %zu\n", l, y, strlen(r)); exit(1); }
      if((y == 0 || y == MAP_H - 1 || r[0] == '.' || r[MAP_W - 1] == '.') &&
         strpbrk(y == 0 || y == MAP_H - 1 ? r : (char[]){r[0], r[MAP_W - 1], 0}, ".Pizha") != NULL)
      { printf("level %d row %d: open border\n", l, y); exit(1); }
    }
  for(int t = CELL_STONE; t < CELL_TYPES; t++)
    for(int j = 0; j < TEX_SIZE; j++)
      if((int)strlen(textures[t].rows[j]) != TEX_SIZE || strspn(textures[t].rows[j], "0123") != TEX_SIZE)
      { printf("texture %d row %d bad\n", t, j); exit(1); }
  for(int a = 0; a < ART_COUNT; a++)
    for(int j = 0; j < arts[a].h; j++)
      if((int)strlen(arts[a].rows[j]) != arts[a].w) { printf("art %d row %d: length %zu (w %d)\n", a, j, strlen(arts[a].rows[j]), arts[a].w); exit(1); }
}

int main(int argc, char **argv)
{
  const char *dir = argc > 1 ? argv[1] : ".";
  /* script: frame ranges with inputs (L R F) */
  struct { int until; int l, r, f; } script[] = {
    {20, 0, 0, 0},     /* title */
    {22, 0, 0, 1},     /* start */
    {40, 0, 0, 0},
    {90, 1, 1, 0},     /* walk forward */
    {110, 0, 1, 0},    /* turn right */
    {150, 1, 1, 0},
    {170, 0, 0, 1},    /* fire */
    {230, 1, 0, 0},    /* turn left */
    {300, 1, 1, 1},
    {600, 1, 1, 0},
  };
  int dumps_walk[] = {10, 41, 90, 110, 151, 168, 200, 260, 330, 450, 599};
  int dumps_fight[] = {40, 60, 61, 64, 71, 80, 100, 130, 160, 260, 599};
  int *dumps = argc > 2 && atoi(argv[2]) == 1 ? dumps_fight : dumps_walk;
  int si = 0, di = 0;

  int scene = argc > 2 ? atoi(argv[2]) : 0;

  check_levels();
  game_init(1);
  perf_fps = 30;
  perf_ms = 18;
  for(int frame = 0; frame < 600; frame++)
  {
    input_t in;
    while(frame >= script[si].until) si++;
    in.left = script[si].l; in.right = script[si].r; in.fire = script[si].f;
    if(scene == 1 && frame >= 30)
    {
      /* face the imp at (10.5, 3.5) from the west; fire from frame 60 */
      if(frame == 30) { g.player.x = 7.5f; g.player.y = 3.5f; g.player.angle = 0.0f; }
      in.left = in.right = 0;
      in.fire = frame >= 60;
    }
    game_tick(&in);
    game_render();
    if(di < 11 && frame == dumps[di])
    {
      dump(dir, frame);
      printf("frame %d: state %d hp %d ammo %d kills %d/%d pos %.2f,%.2f rects %u\n", frame, g.state,
             g.player.hp, g.player.ammo, g.kills, g.total, g.player.x, g.player.y, gfx_rects);
      di++;
    }
  }
  printf("max rects %u\n", max_rects);
  for(int s = 0; s < SFX_COUNT; s++) printf("sfx %d: %d\n", s, sfx_count[s]);
  return 0;
}
