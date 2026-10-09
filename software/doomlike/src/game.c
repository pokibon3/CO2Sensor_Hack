#include <math.h>
#include "game.h"
#include "game_int.h"
#include "sound.h"

game_t g;

#define DT            (1.0f / GAME_HZ)
#define MOVE_SPEED    (3.0f * DT)
#define TURN_SPEED    (2.4f * DT)
#define PLAYER_R      0.25f
#define ACTOR_R       0.3f
#define START_HP      100
#define START_AMMO    40
#define MAX_AMMO      99
#define FIRE_TICKS    10
#define WAKE_DIST     12.0f

static uint32_t rng;

static uint32_t rnd(uint32_t n)
{
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng % n;
}

bool map_solid(int x, int y)
{
  if(x < 0 || y < 0 || x >= MAP_W || y >= MAP_H)
  {
    return true;
  }
  return g.map[y][x] != CELL_FLOOR;
}

static bool blocked(float x, float y, float r)
{
  return map_solid((int)(x - r), (int)(y - r)) || map_solid((int)(x + r), (int)(y - r)) ||
         map_solid((int)(x - r), (int)(y + r)) || map_solid((int)(x + r), (int)(y + r));
}

/* Move with wall sliding. Returns true if the full move was possible. */
static bool move(float *x, float *y, float dx, float dy, float r)
{
  bool ok = true;
  if(!blocked(*x + dx, *y, r))
  {
    *x += dx;
  }
  else
  {
    ok = false;
  }
  if(!blocked(*x, *y + dy, r))
  {
    *y += dy;
  }
  else
  {
    ok = false;
  }
  return ok;
}

/* Line of sight between two points (steps of 0.1 cell). */
static bool sight(float x0, float y0, float x1, float y1)
{
  float dx = x1 - x0, dy = y1 - y0;
  float d = sqrtf(dx * dx + dy * dy);
  int n = (int)(d * 10.0f);

  for(int i = 1; i < n; i++)
  {
    float t = (float)i / (float)n;
    if(map_solid((int)(x0 + dx * t), (int)(y0 + dy * t)))
    {
      return false;
    }
  }
  return true;
}

static actor_t *spawn(uint8_t type, float x, float y)
{
  for(int i = 0; i < MAX_ACTORS; i++)
  {
    actor_t *a = &g.actors[i];
    if(a->type == A_NONE)
    {
      *a = (actor_t){0};
      a->type = type;
      a->x = x;
      a->y = y;
      a->hp = type == A_IMP ? 40 : type == A_BRUTE ? 110 : 1;
      a->state = S_IDLE;
      return a;
    }
  }
  return 0;
}

static void load_level(uint8_t level)
{
  g.level = level;
  g.kills = 0u;
  g.total = 0u;
  for(int i = 0; i < MAX_ACTORS; i++)
  {
    g.actors[i].type = A_NONE;
  }
  for(int y = 0; y < MAP_H; y++)
  {
    for(int x = 0; x < MAP_W; x++)
    {
      char c = levels[level][y][x];
      uint8_t cell = CELL_FLOOR;
      float cx = (float)x + 0.5f, cy = (float)y + 0.5f;

      switch(c)
      {
        case '#': cell = CELL_STONE; break;
        case 'B': cell = CELL_BRICK; break;
        case 'T': cell = CELL_TECH; break;
        case 'W': cell = CELL_WOOD; break;
        case 'X': cell = CELL_EXIT; break;
        case 'P': g.player.x = cx; g.player.y = cy; g.player.angle = 0.0f; break;
        case 'i': spawn(A_IMP, cx, cy); g.total++; break;
        case 'z': spawn(A_BRUTE, cx, cy); g.total++; break;
        case 'h': spawn(A_HEALTH, cx, cy); break;
        case 'a': spawn(A_AMMO, cx, cy); break;
        default: break;
      }
      g.map[y][x] = cell;
    }
  }
  g.player.cooldown = 0u;
  g.player.muzzle = 0u;
  g.player.hurt = 0u;
  g.player.bonus = 0u;
}

static void new_game(void)
{
  g.player.hp = START_HP;
  g.player.ammo = START_AMMO;
  load_level(0u);
  g.player.cooldown = 15u;          /* the start press is not a shot */
  g.state = ST_PLAY;
  g.timer = 0u;
}

void game_init(uint32_t seed)
{
  rng = seed != 0u ? seed : 0x12345678u;
  g.state = ST_TITLE;
  g.timer = 0u;
  load_level(0u);
}

static void hurt_player(int dmg)
{
  if(g.player.hp <= 0)
  {
    return;
  }
  g.player.hp = (int16_t)(g.player.hp - dmg);
  g.player.hurt = 8u;
  sound_play(SFX_HURT);
  if(g.player.hp <= 0)
  {
    g.player.hp = 0;
    g.state = ST_DEAD;
    g.timer = 0u;
    sound_play(SFX_DEATH);
  }
}

/* Hitscan: the nearest living monster whose body covers the view centre,
 * in front of the wall hit by the centre ray.
 */
static void fire(void)
{
  player_t *p = &g.player;
  float dx = cosf(p->angle), dy = sinf(p->angle);
  float wall = 0.0f;
  actor_t *best = 0;
  float best_d = 1e9f;

  if(p->ammo <= 0)
  {
    sound_play(SFX_EMPTY);
    p->cooldown = FIRE_TICKS;
    return;
  }
  p->ammo--;
  p->cooldown = FIRE_TICKS;
  p->muzzle = 4u;
  sound_play(SFX_SHOT);

  while(wall < 30.0f && !map_solid((int)(p->x + dx * wall), (int)(p->y + dy * wall)))
  {
    wall += 0.05f;
  }

  for(int i = 0; i < MAX_ACTORS; i++)
  {
    actor_t *a = &g.actors[i];
    float ax, ay, along, side;

    if((a->type != A_IMP && a->type != A_BRUTE) || a->state == S_DEAD)
    {
      continue;
    }
    ax = a->x - p->x;
    ay = a->y - p->y;
    along = ax * dx + ay * dy;
    side = ax * dy - ay * dx;
    if(along > 0.2f && along < wall && fabsf(side) < 0.35f && along < best_d)
    {
      best = a;
      best_d = along;
    }
  }

  /* The noise wakes monsters nearby. */
  for(int i = 0; i < MAX_ACTORS; i++)
  {
    actor_t *a = &g.actors[i];
    if((a->type == A_IMP || a->type == A_BRUTE) && a->state == S_IDLE &&
       fabsf(a->x - p->x) + fabsf(a->y - p->y) < 8.0f)
    {
      a->state = S_CHASE;
    }
  }

  if(best != 0)
  {
    best->hp = (int16_t)(best->hp - (int)(18u + rnd(14u)));
    best->flash = 4u;
    if(best->state == S_IDLE)
    {
      best->state = S_CHASE;
    }
    if(best->hp <= 0)
    {
      best->state = S_DEAD;
      g.kills++;
      sound_play(SFX_KILL);
    }
    else
    {
      sound_play(SFX_PAIN);
    }
  }
}

static void update_monster(actor_t *a)
{
  player_t *p = &g.player;
  float dx = p->x - a->x, dy = p->y - a->y;
  float d = sqrtf(dx * dx + dy * dy);
  bool imp = a->type == A_IMP;
  float speed = (imp ? 1.5f : 2.3f) * DT;

  if(a->flash != 0u)
  {
    a->flash--;
  }
  if(a->state == S_DEAD)
  {
    return;
  }
  if(a->state == S_IDLE)
  {
    if(d < WAKE_DIST && sight(a->x, a->y, p->x, p->y))
    {
      a->state = S_CHASE;
      a->timer = (uint8_t)(20u + rnd(30u));
    }
    return;
  }
  if(a->state == S_ATTACK)
  {
    if(--a->timer == 0u)
    {
      if(imp)
      {
        actor_t *f = spawn(A_FIREBALL, a->x, a->y);
        if(f != 0 && d > 0.01f)
        {
          f->vx = dx / d * 5.0f * DT;
          f->vy = dy / d * 5.0f * DT;
          sound_play(SFX_FIREBALL);
        }
        a->timer = (uint8_t)(40u + rnd(40u));
      }
      else
      {
        if(d < 1.2f)
        {
          hurt_player((int)(10u + rnd(8u)));
        }
        a->timer = 25u;
      }
      a->state = S_CHASE;
    }
    return;
  }

  /* S_CHASE */
  if(a->timer != 0u)
  {
    a->timer--;
  }
  if(imp)
  {
    if(a->timer == 0u && d < 10.0f && sight(a->x, a->y, p->x, p->y))
    {
      a->state = S_ATTACK;
      a->timer = 8u;
      return;
    }
    if(d > 2.5f)
    {
      move(&a->x, &a->y, dx / d * speed, dy / d * speed, ACTOR_R);
      a->anim++;
    }
  }
  else
  {
    if(d < 1.1f)
    {
      if(a->timer == 0u)
      {
        a->state = S_ATTACK;
        a->timer = 6u;
        sound_play(SFX_BITE);
      }
      return;
    }
    move(&a->x, &a->y, dx / d * speed, dy / d * speed, ACTOR_R);
    a->anim++;
  }
}

static void update_actors(void)
{
  player_t *p = &g.player;

  for(int i = 0; i < MAX_ACTORS; i++)
  {
    actor_t *a = &g.actors[i];
    float dx = p->x - a->x, dy = p->y - a->y;

    switch(a->type)
    {
      case A_IMP:
      case A_BRUTE:
        update_monster(a);
        break;
      case A_FIREBALL:
        a->x += a->vx;
        a->y += a->vy;
        a->anim++;
        if(map_solid((int)a->x, (int)a->y))
        {
          a->type = A_NONE;
        }
        else if(dx * dx + dy * dy < 0.16f)
        {
          a->type = A_NONE;
          hurt_player((int)(6u + rnd(8u)));
        }
        break;
      case A_HEALTH:
        if(dx * dx + dy * dy < 0.3f && p->hp < START_HP)
        {
          p->hp = (int16_t)(p->hp + 25 > START_HP ? START_HP : p->hp + 25);
          p->bonus = 5u;
          a->type = A_NONE;
          sound_play(SFX_PICKUP);
        }
        break;
      case A_AMMO:
        if(dx * dx + dy * dy < 0.3f && p->ammo < MAX_AMMO)
        {
          p->ammo = (int16_t)(p->ammo + 12 > MAX_AMMO ? MAX_AMMO : p->ammo + 12);
          p->bonus = 5u;
          a->type = A_NONE;
          sound_play(SFX_PICKUP);
        }
        break;
      default:
        break;
    }
  }
}

static void update_player(const input_t *in)
{
  player_t *p = &g.player;
  float dx = cosf(p->angle), dy = sinf(p->angle);

  p->moving = false;
  if(in->left && in->right)
  {
    float nx = p->x, ny = p->y;
    move(&nx, &ny, dx * MOVE_SPEED, dy * MOVE_SPEED, PLAYER_R);
    p->moving = nx != p->x || ny != p->y;
    p->x = nx;
    p->y = ny;
    /* Walking into the exit switch ends the level. */
    if(g.map[(int)(p->y + dy * (PLAYER_R + 0.1f))][(int)(p->x + dx * (PLAYER_R + 0.1f))] == CELL_EXIT)
    {
      g.state = ST_CLEAR;
      g.timer = 0u;
      sound_play(SFX_CLEAR);
    }
  }
  else if(in->left)
  {
    p->angle -= TURN_SPEED;
  }
  else if(in->right)
  {
    p->angle += TURN_SPEED;
  }
  if(p->angle < 0.0f)
  {
    p->angle += 6.2831853f;
  }
  else if(p->angle >= 6.2831853f)
  {
    p->angle -= 6.2831853f;
  }
  if(p->moving)
  {
    p->bob += 0.45f;
  }

  if(p->cooldown != 0u)
  {
    p->cooldown--;
  }
  else if(in->fire)
  {
    fire();
  }
  if(p->muzzle != 0u) p->muzzle--;
  if(p->hurt != 0u) p->hurt--;
  if(p->bonus != 0u) p->bonus--;
}

void game_tick(const input_t *in)
{
  static bool fire_prev = true;
  bool fire_edge = in->fire && !fire_prev;

  fire_prev = in->fire;
  if(g.timer < 0xFFFFu)
  {
    g.timer++;
  }

  switch(g.state)
  {
    case ST_TITLE:
      if(fire_edge)
      {
        new_game();
      }
      break;
    case ST_PLAY:
      update_player(in);
      if(g.state == ST_PLAY)
      {
        update_actors();
      }
      break;
    case ST_DEAD:
      update_actors();
      if(g.player.hurt != 0u) g.player.hurt--;
      if(g.timer > GAME_HZ && fire_edge)
      {
        g.state = ST_TITLE;
        g.timer = 0u;
        load_level(0u);
      }
      break;
    case ST_CLEAR:
      if(g.timer > 2u * GAME_HZ)
      {
        if(g.level + 1u < LEVEL_COUNT)
        {
          load_level((uint8_t)(g.level + 1u));
          g.state = ST_PLAY;
        }
        else
        {
          g.state = ST_WIN;
        }
        g.timer = 0u;
      }
      break;
    case ST_WIN:
      if(g.timer > GAME_HZ && fire_edge)
      {
        g.state = ST_TITLE;
        g.timer = 0u;
        load_level(0u);
      }
      break;
    default:
      break;
  }
}
