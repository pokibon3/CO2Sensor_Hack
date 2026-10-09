#include "at32f415.h"
#include "sound.h"

/* PA4 passive buzzer, driven as in ALIEN RAID: a 48 kHz TMR3 interrupt ORs
 * square-wave pulse trains (width = loudness) and gated LFSR noise onto the
 * pin. Effects are short lists of linear frequency sweeps, advanced at
 * GAME_HZ by sound_tick().
 */
#define BUZZER_PIN    GPIO_PINS_4
#define SAMPLE_RATE   48000u
#define FREQ_INC(hz)  ((uint32_t)((hz) * (4294967296.0f / SAMPLE_RATE)))
#define VOICES        3u

typedef struct
{
  uint16_t f0, f1;      /* Hz at start and end of the segment */
  uint8_t amp;          /* 0..255 */
  uint8_t noise;
  uint8_t ticks;
} seg_t;

typedef struct
{
  uint8_t voice;
  uint8_t count;
  seg_t seg[4];
} sfx_t;

static const sfx_t sfx_table[SFX_COUNT] = {
  [SFX_SHOT]     = {0, 2, {{1800, 500, 220, 1, 4}, {130, 60, 140, 0, 3}}},
  [SFX_EMPTY]    = {0, 1, {{900, 900, 70, 0, 2}}},
  [SFX_PAIN]     = {1, 1, {{700, 380, 120, 0, 5}}},
  [SFX_KILL]     = {1, 2, {{520, 90, 160, 0, 9}, {300, 100, 120, 1, 6}}},
  [SFX_HURT]     = {2, 1, {{170, 80, 220, 0, 6}}},
  [SFX_DEATH]    = {2, 2, {{320, 40, 220, 0, 30}, {200, 50, 150, 1, 15}}},
  [SFX_PICKUP]   = {2, 2, {{880, 880, 110, 0, 3}, {1320, 1320, 110, 0, 4}}},
  [SFX_FIREBALL] = {1, 1, {{400, 1300, 100, 1, 8}}},
  [SFX_BITE]     = {1, 1, {{350, 200, 170, 1, 4}}},
  [SFX_CLEAR]    = {2, 4, {{523, 523, 110, 0, 5}, {659, 659, 110, 0, 5},
                           {784, 784, 110, 0, 5}, {1047, 1047, 110, 0, 10}}},
};

typedef struct
{
  volatile uint32_t inc;
  volatile uint8_t amp;
  volatile uint8_t noise;
  uint32_t phase;
  uint8_t level;
} voice_t;

typedef struct
{
  const sfx_t *fx;
  uint8_t seg;
  uint8_t t;
} player_t;

static voice_t voice[VOICES];
static player_t play[VOICES];
static uint16_t lfsr = 0xACE1u;
static uint16_t dither = 0x1234u;
static bool enabled;

void TMR3_GLOBAL_IRQHandler(void)
{
  bool on = false;

  TMR3->ists = ~TMR_OVF_FLAG;
  dither = (uint16_t)((dither >> 1) ^ (-(dither & 1u) & 0xB400u));

  for(uint32_t i = 0; i < VOICES; i++)
  {
    voice_t *v = &voice[i];
    uint32_t prev = v->phase;

    if(v->amp == 0u)
    {
      continue;
    }
    v->phase += v->inc;
    if(v->noise)
    {
      if(v->phase < prev)
      {
        lfsr = (uint16_t)((lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u));
        v->level = (uint8_t)(lfsr & 1u);
      }
      on |= v->level && (uint8_t)dither < v->amp;
    }
    else
    {
      on |= v->phase < ((uint32_t)v->amp << 23);
    }
  }

  if(on)
  {
    GPIOA->scr = BUZZER_PIN;
  }
  else
  {
    GPIOA->clr = BUZZER_PIN;
  }
}

void sound_init(void)
{
  gpio_init_type gpio;
  tmr_type *t = TMR3;

  crm_periph_clock_enable(CRM_TMR3_PERIPH_CLOCK, TRUE);
  gpio_bits_reset(GPIOA, BUZZER_PIN);
  gpio_default_para_init(&gpio);
  gpio.gpio_mode = GPIO_MODE_OUTPUT;
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio.gpio_pull = GPIO_PULL_NONE;
  gpio.gpio_pins = BUZZER_PIN;
  gpio_init(GPIOA, &gpio);

  /* TMR3 clock = 2 x APB1 = 144 MHz */
  tmr_base_init(t, (uint32_t)(system_core_clock / SAMPLE_RATE) - 1u, 0u);
  tmr_cnt_dir_set(t, TMR_COUNT_UP);
  tmr_interrupt_enable(t, TMR_OVF_INT, TRUE);
  nvic_irq_enable(TMR3_GLOBAL_IRQn, 1, 0);
  tmr_counter_enable(t, TRUE);
}

void sound_enable(bool on)
{
  enabled = on;
  if(!on)
  {
    for(uint32_t i = 0; i < VOICES; i++)
    {
      play[i].fx = 0;
      voice[i].amp = 0u;
    }
  }
}

void sound_play(int sfx)
{
  const sfx_t *fx;

  if(!enabled || sfx < 0 || sfx >= SFX_COUNT)
  {
    return;
  }
  fx = &sfx_table[sfx];
  play[fx->voice].fx = fx;
  play[fx->voice].seg = 0u;
  play[fx->voice].t = 0u;
}

void sound_tick(void)
{
  for(uint32_t i = 0; i < VOICES; i++)
  {
    player_t *p = &play[i];
    const seg_t *s;
    float f;

    if(p->fx == 0)
    {
      voice[i].amp = 0u;
      continue;
    }
    if(p->t >= p->fx->seg[p->seg].ticks)
    {
      p->seg++;
      p->t = 0u;
      if(p->seg >= p->fx->count)
      {
        p->fx = 0;
        voice[i].amp = 0u;
        continue;
      }
    }
    s = &p->fx->seg[p->seg];
    f = (float)s->f0 + ((float)s->f1 - (float)s->f0) * (float)p->t / (float)s->ticks;
    voice[i].noise = s->noise;
    voice[i].inc = FREQ_INC(f);
    voice[i].amp = s->amp;
    p->t++;
  }
}
