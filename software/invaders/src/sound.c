#include "at32f415.h"
#include "machine.h"
#include "sound.h"

/* PA4 drives a passive buzzer (magnetic transducer; a steady level only
 * clicks). TMR3 runs a 48 kHz sample interrupt. Each tone voice is a plain
 * square-wave pulse train whose width sets the loudness; noise voices gate
 * an LFSR. The voices are OR-ed onto PA4, which stays low in silence.
 * (A delta-sigma bit stream sounded harsh on this transducer.)
 */
#define BUZZER_PIN    GPIO_PINS_4
#define SAMPLE_RATE   48000u
#define FREQ_INC(hz)  ((uint32_t)((hz) * 4294967296.0 / SAMPLE_RATE))

/* Game RAM (see rom/game.asm) */
#define RAM(a)        machine_ram[(a) - 0x2000u]
#define G_STATE       0x2004u
#define G_PB_ACT      0x200Cu
#define G_AL_ANIM     0x2021u
#define G_AL_CNT      0x2022u
#define G_UFO_ACT     0x2023u
#define G_UFO_STMR    0x202Cu

typedef struct
{
  volatile uint32_t inc;      /* phase increment per sample */
  volatile uint8_t amp;       /* 0..255: pulse width 0..50 %, noise density */
  volatile uint8_t noise;     /* 1: LFSR noise clocked at the phase rate */
  uint32_t phase;
  uint8_t level;              /* current output (noise) */
} voice_t;

enum { V_FLEET, V_UFO, V_SHOT, V_HIT, V_COUNT };

static voice_t voice[V_COUNT];
static uint16_t lfsr = 0xACE1u;
static uint16_t dither = 0x1234u;
static bool enabled;

/* Effect timers (frames since start, 0xFF = idle) */
static uint8_t t_fleet = 0xFF, t_shot = 0xFF, t_hit = 0xFF, t_ufo_hit = 0xFF, t_death = 0xFF;
static uint8_t fleet_note;
static uint32_t ufo_t;
static uint8_t last_state, last_pb, last_anim, last_cnt, last_ufo_stmr;

void TMR3_GLOBAL_IRQHandler(void)
{
  bool on = false;

  TMR3->ists = ~TMR_OVF_FLAG;
  dither = (uint16_t)((dither >> 1) ^ (-(dither & 1u) & 0xB400u));

  for(uint32_t i = 0; i < V_COUNT; i++)
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
      /* pulse width = amp / 512 of the period */
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

static void set_voice(uint32_t i, float hz, uint8_t amp, uint8_t noise)
{
  voice[i].noise = noise;
  voice[i].inc = FREQ_INC(hz);
  voice[i].amp = amp;
}

static uint8_t decay(uint32_t t, uint32_t length, uint32_t peak)
{
  return t >= length ? 0u : (uint8_t)(peak * (length - t) / length);
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
    for(uint32_t i = 0; i < V_COUNT; i++)
    {
      voice[i].amp = 0u;
    }
  }
}

void sound_frame(void)
{
  /* Fleet march: four descending bass notes, one per fleet step. */
  static const float fleet_hz[4] = {98.0f, 87.3f, 77.8f, 73.4f};
  uint8_t state = RAM(G_STATE);
  uint8_t pb = RAM(G_PB_ACT);
  uint8_t anim = RAM(G_AL_ANIM);
  uint8_t cnt = RAM(G_AL_CNT);
  uint8_t ufo_stmr = RAM(G_UFO_STMR);

  if(!enabled)
  {
    return;
  }

  /* Events from game RAM */
  if(state == 1u && anim != last_anim)
  {
    t_fleet = 0u;
    fleet_note = (uint8_t)((fleet_note + 1u) & 3u);
  }
  if(pb != 0u && last_pb == 0u)
  {
    t_shot = 0u;
  }
  if(state == 1u && cnt < last_cnt)
  {
    t_hit = 0u;
  }
  if(ufo_stmr != 0u && last_ufo_stmr == 0u)
  {
    t_ufo_hit = 0u;
  }
  if(state == 2u && last_state != 2u)
  {
    t_death = 0u;
    t_shot = 0xFFu;
  }
  last_state = state;
  last_pb = pb;
  last_anim = anim;
  last_cnt = cnt;
  last_ufo_stmr = ufo_stmr;

  /* Fleet: low, narrow pulse train -> a short "thump" on the buzzer */
  if(t_fleet != 0xFFu)
  {
    set_voice(V_FLEET, fleet_hz[fleet_note], decay(t_fleet, 7u, 60u), 0u);
    t_fleet = (t_fleet >= 7u) ? 0xFFu : (uint8_t)(t_fleet + 1u);
  }

  /* UFO: siren warbling about 6 times a second */
  if(RAM(G_UFO_ACT) != 0u && state == 1u)
  {
    uint32_t p = ufo_t++ % 10u;
    float tri = (p < 5u) ? (float)p / 5.0f : (float)(10u - p) / 5.0f;
    set_voice(V_UFO, 650.0f + 550.0f * tri, 90u, 0u);
  }
  else
  {
    voice[V_UFO].amp = 0u;
    ufo_t = 0u;
  }

  /* Player shot: falling "pew" */
  if(t_shot != 0xFFu)
  {
    set_voice(V_SHOT, 1500.0f - 70.0f * t_shot, decay(t_shot, 16u, 200u), 0u);
    t_shot = (t_shot >= 16u) ? 0xFFu : (uint8_t)(t_shot + 1u);
  }

  /* Hit channel: player explosion > UFO hit > invader hit */
  if(t_death != 0xFFu)
  {
    set_voice(V_HIT, 6000.0f - 80.0f * t_death, decay(t_death, 60u, 220u), 1u);
    t_death = (t_death >= 60u) ? 0xFFu : (uint8_t)(t_death + 1u);
  }
  else if(t_ufo_hit != 0xFFu)
  {
    set_voice(V_HIT, ((t_ufo_hit / 3u) & 1u) ? 600.0f : 1200.0f, decay(t_ufo_hit, 45u, 220u), 0u);
    t_ufo_hit = (t_ufo_hit >= 45u) ? 0xFFu : (uint8_t)(t_ufo_hit + 1u);
  }
  else if(t_hit != 0xFFu)
  {
    set_voice(V_HIT, 4000.0f - 300.0f * t_hit, decay(t_hit, 10u, 200u), 1u);
    t_hit = (t_hit >= 10u) ? 0xFFu : (uint8_t)(t_hit + 1u);
  }
  else
  {
    voice[V_HIT].amp = 0u;
  }
}
