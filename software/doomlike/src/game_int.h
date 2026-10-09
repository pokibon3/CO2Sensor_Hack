#ifndef GAME_INT_H
#define GAME_INT_H

#include <stdbool.h>
#include <stdint.h>

/* State shared by game.c (logic) and render.c (drawing). */

#define MAP_W 20
#define MAP_H 20
#define LEVEL_COUNT 3
#define MAX_ACTORS 40

enum { CELL_FLOOR, CELL_STONE, CELL_BRICK, CELL_TECH, CELL_WOOD, CELL_EXIT, CELL_TYPES };
enum { ST_TITLE, ST_PLAY, ST_DEAD, ST_CLEAR, ST_WIN };
enum { A_NONE, A_IMP, A_BRUTE, A_FIREBALL, A_HEALTH, A_AMMO };
enum { S_IDLE, S_CHASE, S_ATTACK, S_DEAD };

typedef struct
{
  float x, y;
  float vx, vy;       /* fireballs */
  int16_t hp;
  uint8_t type;
  uint8_t state;
  uint8_t timer;      /* ticks left in the current state / attack cooldown */
  uint8_t anim;       /* walk animation counter */
  uint8_t flash;      /* ticks of pain flash */
} actor_t;

typedef struct
{
  float x, y, angle;
  int16_t hp;
  int16_t ammo;
  uint8_t cooldown;   /* ticks until the next shot */
  uint8_t muzzle;     /* ticks of muzzle flash */
  uint8_t hurt;       /* ticks of red tint */
  uint8_t bonus;      /* ticks of yellow tint */
  float bob;          /* walk bob phase */
  bool moving;
} player_t;

typedef struct
{
  uint8_t state;
  uint8_t level;      /* 0-based */
  uint16_t timer;     /* ticks in the current state */
  uint8_t kills, total;
  uint8_t map[MAP_H][MAP_W];
  actor_t actors[MAX_ACTORS];
  player_t player;
} game_t;

extern game_t g;

extern const char *const levels[LEVEL_COUNT][MAP_H];

/* Sprite art: rows of palette characters, '.' = transparent. */
typedef struct
{
  uint8_t w, h;
  const char *const *rows;
} art_t;

enum { ART_IMP_A, ART_IMP_B, ART_IMP_ATK, ART_IMP_DEAD,
       ART_BRUTE_A, ART_BRUTE_B, ART_BRUTE_ATK, ART_BRUTE_DEAD,
       ART_FIREBALL, ART_HEALTH, ART_AMMO, ART_GUN, ART_FLASH, ART_COUNT };

extern const art_t arts[ART_COUNT];
const uint8_t *palette_rgb(char c);   /* NULL for transparent/unknown */

bool map_solid(int x, int y);

#endif
