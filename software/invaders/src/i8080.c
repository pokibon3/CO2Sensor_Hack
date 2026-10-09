#include "i8080.h"

#define RA 7
#define RH 4
#define RL 5

static const uint8_t cycles_table[256] = {
  4, 10, 7, 5, 5, 5, 7, 4, 4, 10, 7, 5, 5, 5, 7, 4,
  4, 10, 7, 5, 5, 5, 7, 4, 4, 10, 7, 5, 5, 5, 7, 4,
  4, 10, 16, 5, 5, 5, 7, 4, 4, 10, 16, 5, 5, 5, 7, 4,
  4, 10, 13, 5, 10, 10, 10, 4, 4, 10, 13, 5, 5, 5, 7, 4,
  5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
  5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
  5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
  7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 5, 5, 5, 5, 7, 5,
  4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
  4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
  4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
  4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
  5, 10, 10, 10, 11, 11, 7, 11, 5, 10, 10, 10, 11, 17, 7, 11,
  5, 10, 10, 10, 11, 11, 7, 11, 5, 10, 10, 10, 11, 17, 7, 11,
  5, 10, 10, 18, 11, 11, 7, 11, 5, 5, 10, 4, 11, 17, 7, 11,
  5, 10, 10, 4, 11, 11, 7, 11, 5, 5, 10, 4, 11, 17, 7, 11,
};

static uint8_t parity_even(uint8_t v)
{
  v ^= (uint8_t)(v >> 4);
  v ^= (uint8_t)(v >> 2);
  v ^= (uint8_t)(v >> 1);
  return (uint8_t)((v & 1u) == 0u);
}

static uint16_t hl(const i8080_t *c)
{
  return (uint16_t)((c->r[RH] << 8) | c->r[RL]);
}

static void set_hl(i8080_t *c, uint16_t v)
{
  c->r[RH] = (uint8_t)(v >> 8);
  c->r[RL] = (uint8_t)v;
}

static uint16_t rp(const i8080_t *c, int i)
{
  if(i == 3)
  {
    return c->sp;
  }
  return (uint16_t)((c->r[i * 2] << 8) | c->r[i * 2 + 1]);
}

static void set_rp(i8080_t *c, int i, uint16_t v)
{
  if(i == 3)
  {
    c->sp = v;
  }
  else
  {
    c->r[i * 2] = (uint8_t)(v >> 8);
    c->r[i * 2 + 1] = (uint8_t)v;
  }
}

static uint8_t get8(const i8080_t *c, int i)
{
  return i == 6 ? i8080_mem_read(hl(c)) : c->r[i];
}

static void set8(i8080_t *c, int i, uint8_t v)
{
  if(i == 6)
  {
    i8080_mem_write(hl(c), v);
  }
  else
  {
    c->r[i] = v;
  }
}

static void szp(i8080_t *c, uint8_t v)
{
  c->s = (uint8_t)(v >> 7);
  c->z = (uint8_t)(v == 0u);
  c->p = parity_even(v);
}

uint8_t i8080_psw(const i8080_t *c)
{
  return (uint8_t)((c->s << 7) | (c->z << 6) | (c->ac << 4) | (c->p << 2) | 2u | c->cy);
}

static void set_psw(i8080_t *c, uint8_t f)
{
  c->s = (uint8_t)((f >> 7) & 1u);
  c->z = (uint8_t)((f >> 6) & 1u);
  c->ac = (uint8_t)((f >> 4) & 1u);
  c->p = (uint8_t)((f >> 2) & 1u);
  c->cy = (uint8_t)(f & 1u);
}

static uint8_t fetch8(i8080_t *c)
{
  uint8_t v = i8080_mem_read(c->pc);
  c->pc++;
  return v;
}

static uint16_t fetch16(i8080_t *c)
{
  uint8_t lo = fetch8(c);
  return (uint16_t)((fetch8(c) << 8) | lo);
}

static void push(i8080_t *c, uint16_t v)
{
  c->sp--;
  i8080_mem_write(c->sp, (uint8_t)(v >> 8));
  c->sp--;
  i8080_mem_write(c->sp, (uint8_t)v);
}

static uint16_t pop(i8080_t *c)
{
  uint8_t lo = i8080_mem_read(c->sp);
  uint8_t hi = i8080_mem_read((uint16_t)(c->sp + 1u));
  c->sp = (uint16_t)(c->sp + 2u);
  return (uint16_t)((hi << 8) | lo);
}

static uint8_t add(i8080_t *c, uint8_t v, uint8_t carry)
{
  uint8_t a = c->r[RA];
  uint16_t res = (uint16_t)(a + v + carry);
  c->ac = (uint8_t)(((a & 0xFu) + (v & 0xFu) + carry) > 0xFu);
  c->cy = (uint8_t)(res > 0xFFu);
  szp(c, (uint8_t)res);
  return (uint8_t)res;
}

static uint8_t sub(i8080_t *c, uint8_t v, uint8_t borrow)
{
  uint8_t a = c->r[RA];
  uint8_t nv = (uint8_t)~v;
  uint16_t res = (uint16_t)(a + nv + (1u - borrow));
  c->ac = (uint8_t)(((a & 0xFu) + (nv & 0xFu) + (1u - borrow)) > 0xFu);
  c->cy = (uint8_t)(res <= 0xFFu);
  szp(c, (uint8_t)res);
  return (uint8_t)res;
}

static void alu(i8080_t *c, int op, uint8_t v)
{
  uint8_t a = c->r[RA];
  uint8_t res;

  switch(op)
  {
    case 0: c->r[RA] = add(c, v, 0u); break;
    case 1: c->r[RA] = add(c, v, c->cy); break;
    case 2: c->r[RA] = sub(c, v, 0u); break;
    case 3: c->r[RA] = sub(c, v, c->cy); break;
    case 4:
      res = a & v;
      c->cy = 0u;
      c->ac = (uint8_t)(((a | v) >> 3) & 1u);
      szp(c, res);
      c->r[RA] = res;
      break;
    case 5:
      res = a ^ v;
      c->cy = 0u;
      c->ac = 0u;
      szp(c, res);
      c->r[RA] = res;
      break;
    case 6:
      res = a | v;
      c->cy = 0u;
      c->ac = 0u;
      szp(c, res);
      c->r[RA] = res;
      break;
    default:
      (void)sub(c, v, 0u);
      break;
  }
}

static int cond(const i8080_t *c, int cc)
{
  switch(cc)
  {
    case 0: return !c->z;
    case 1: return c->z;
    case 2: return !c->cy;
    case 3: return c->cy;
    case 4: return !c->p;
    case 5: return c->p;
    case 6: return !c->s;
    default: return c->s;
  }
}

static void execute(i8080_t *c, uint8_t op)
{
  int x = op >> 6;
  int y = (op >> 3) & 7;
  int z = op & 7;
  uint8_t a;
  uint8_t v8;
  uint16_t v16;
  uint32_t res;

  c->cycles += cycles_table[op];

  if(x == 1)
  {
    if(op == 0x76u)
    {
      c->halted = 1u;
    }
    else
    {
      set8(c, y, get8(c, z));
    }
    return;
  }
  if(x == 2)
  {
    alu(c, y, get8(c, z));
    return;
  }

  if(x == 0)
  {
    switch(z)
    {
      case 0:
        break;                                    /* NOP (+aliases) */
      case 1:
        if(y & 1)
        {                                         /* DAD */
          res = (uint32_t)hl(c) + rp(c, y >> 1);
          c->cy = (uint8_t)(res > 0xFFFFu);
          set_hl(c, (uint16_t)res);
        }
        else
        {
          set_rp(c, y >> 1, fetch16(c));          /* LXI */
        }
        break;
      case 2:
        switch(y)
        {
          case 0: i8080_mem_write(rp(c, 0), c->r[RA]); break;
          case 1: c->r[RA] = i8080_mem_read(rp(c, 0)); break;
          case 2: i8080_mem_write(rp(c, 1), c->r[RA]); break;
          case 3: c->r[RA] = i8080_mem_read(rp(c, 1)); break;
          case 4:                                 /* SHLD */
            v16 = fetch16(c);
            i8080_mem_write(v16, c->r[RL]);
            i8080_mem_write((uint16_t)(v16 + 1u), c->r[RH]);
            break;
          case 5:                                 /* LHLD */
            v16 = fetch16(c);
            c->r[RL] = i8080_mem_read(v16);
            c->r[RH] = i8080_mem_read((uint16_t)(v16 + 1u));
            break;
          case 6: i8080_mem_write(fetch16(c), c->r[RA]); break;
          default: c->r[RA] = i8080_mem_read(fetch16(c)); break;
        }
        break;
      case 3:                                     /* INX / DCX */
        set_rp(c, y >> 1, (uint16_t)(rp(c, y >> 1) + ((y & 1) ? 0xFFFFu : 1u)));
        break;
      case 4:                                     /* INR */
        v8 = (uint8_t)(get8(c, y) + 1u);
        c->ac = (uint8_t)((v8 & 0xFu) == 0u);
        szp(c, v8);
        set8(c, y, v8);
        break;
      case 5:                                     /* DCR */
        v8 = (uint8_t)(get8(c, y) - 1u);
        c->ac = (uint8_t)((v8 & 0xFu) != 0xFu);
        szp(c, v8);
        set8(c, y, v8);
        break;
      case 6:
        set8(c, y, fetch8(c));                    /* MVI */
        break;
      default:
        a = c->r[RA];
        switch(y)
        {
          case 0:                                 /* RLC */
            c->cy = (uint8_t)(a >> 7);
            c->r[RA] = (uint8_t)((a << 1) | c->cy);
            break;
          case 1:                                 /* RRC */
            c->cy = (uint8_t)(a & 1u);
            c->r[RA] = (uint8_t)((a >> 1) | (c->cy << 7));
            break;
          case 2:                                 /* RAL */
            v8 = c->cy;
            c->cy = (uint8_t)(a >> 7);
            c->r[RA] = (uint8_t)((a << 1) | v8);
            break;
          case 3:                                 /* RAR */
            v8 = c->cy;
            c->cy = (uint8_t)(a & 1u);
            c->r[RA] = (uint8_t)((a >> 1) | (v8 << 7));
            break;
          case 4:                                 /* DAA */
          {
            uint8_t lsb = a & 0xFu;
            uint8_t msb = a >> 4;
            uint8_t carry = c->cy;
            uint8_t corr = 0u;
            if(c->ac || lsb > 9u)
            {
              corr = (uint8_t)(corr + 0x06u);
            }
            if(c->cy || msb > 9u || (msb >= 9u && lsb > 9u))
            {
              corr = (uint8_t)(corr + 0x60u);
              carry = 1u;
            }
            c->r[RA] = add(c, corr, 0u);
            c->cy = carry;
            break;
          }
          case 5: c->r[RA] = (uint8_t)~a; break;  /* CMA */
          case 6: c->cy = 1u; break;              /* STC */
          default: c->cy ^= 1u; break;            /* CMC */
        }
        break;
    }
    return;
  }

  /* x == 3 */
  switch(z)
  {
    case 0:                                       /* Rcc */
      if(cond(c, y))
      {
        c->pc = pop(c);
        c->cycles += 6u;
      }
      break;
    case 1:
      if((y & 1) == 0)
      {
        v16 = pop(c);
        if((y >> 1) == 3)
        {
          c->r[RA] = (uint8_t)(v16 >> 8);
          set_psw(c, (uint8_t)v16);
        }
        else
        {
          set_rp(c, y >> 1, v16);
        }
      }
      else if(y == 1 || y == 3)
      {
        c->pc = pop(c);                           /* RET (D9 alias) */
      }
      else if(y == 5)
      {
        c->pc = hl(c);                            /* PCHL */
      }
      else
      {
        c->sp = hl(c);                            /* SPHL */
      }
      break;
    case 2:                                       /* Jcc */
      v16 = fetch16(c);
      if(cond(c, y))
      {
        c->pc = v16;
      }
      break;
    case 3:
      switch(y)
      {
        case 0:
        case 1: c->pc = fetch16(c); break;        /* JMP (CB alias) */
        case 2:
          v8 = fetch8(c);
          i8080_io_out(v8, c->r[RA]);
          break;
        case 3:
          v8 = fetch8(c);
          c->r[RA] = i8080_io_in(v8);
          break;
        case 4:                                   /* XTHL */
          v16 = (uint16_t)(i8080_mem_read(c->sp) | (i8080_mem_read((uint16_t)(c->sp + 1u)) << 8));
          i8080_mem_write(c->sp, c->r[RL]);
          i8080_mem_write((uint16_t)(c->sp + 1u), c->r[RH]);
          set_hl(c, v16);
          break;
        case 5:                                   /* XCHG */
          v8 = c->r[2]; c->r[2] = c->r[RH]; c->r[RH] = v8;
          v8 = c->r[3]; c->r[3] = c->r[RL]; c->r[RL] = v8;
          break;
        case 6: c->inte = 0u; break;              /* DI */
        default:                                  /* EI */
          c->inte = 1u;
          c->ei_pending = 1u;
          break;
      }
      break;
    case 4:                                       /* Ccc */
      v16 = fetch16(c);
      if(cond(c, y))
      {
        push(c, c->pc);
        c->pc = v16;
        c->cycles += 6u;
      }
      break;
    case 5:
      if((y & 1) == 0)
      {
        if((y >> 1) == 3)
        {
          push(c, (uint16_t)((c->r[RA] << 8) | i8080_psw(c)));
        }
        else
        {
          push(c, rp(c, y >> 1));
        }
      }
      else
      {                                           /* CALL (+DD/ED/FD) */
        v16 = fetch16(c);
        push(c, c->pc);
        c->pc = v16;
      }
      break;
    case 6:
      alu(c, y, fetch8(c));
      break;
    default:                                      /* RST */
      push(c, c->pc);
      c->pc = (uint16_t)(y * 8);
      break;
  }
}

void i8080_reset(i8080_t *cpu)
{
  uint8_t *p = (uint8_t *)cpu;
  for(unsigned i = 0; i < sizeof(*cpu); i++)
  {
    p[i] = 0u;
  }
}

void i8080_step(i8080_t *cpu)
{
  if(cpu->halted)
  {
    cpu->cycles += 4u;
    return;
  }
  cpu->ei_pending = 0u;
  execute(cpu, fetch8(cpu));
}

int i8080_interrupt(i8080_t *cpu, uint8_t opcode)
{
  if(!cpu->inte || cpu->ei_pending)
  {
    return 0;
  }
  cpu->inte = 0u;
  cpu->halted = 0u;
  execute(cpu, opcode);
  return 1;
}
