#include <stddef.h>
#include "game_int.h"

static const char *const imp_a[] = {
  "..B........B..",
  "..bB.bbbb.Bb..",
  "...bbbbbbbb...",
  "...brrbbrrb...",
  "...bbbbbbbb...",
  "....bwwwwb....",
  "..bbbbbbbbbb..",
  ".bbbbBbbBbbbb.",
  "bb.bbbbbbbb.bb",
  "b..bbBbbBbb..b",
  "k..bbbbbbbb..k",
  "...bbb..bbb...",
  "...bb....bb...",
  "...bb....bb...",
  "..bbb....bbb..",
  "..kk......kk..",
};

static const char *const imp_b[] = {
  "..B........B..",
  "..bB.bbbb.Bb..",
  "...bbbbbbbb...",
  "...brrbbrrb...",
  "...bbbbbbbb...",
  "....bwwwwb....",
  "..bbbbbbbbbb..",
  ".bbbbBbbBbbbb.",
  "bb.bbbbbbbb.bb",
  "b..bbBbbBbb..b",
  "k..bbbbbbbb..k",
  "...bbb..bbb...",
  "..bb......bb..",
  ".bb........bb.",
  ".bb........bb.",
  ".kk........kk.",
};

static const char *const imp_atk[] = {
  "..B........B..",
  "..bB.bbbb.Bb..",
  "...bbbbbbbb...",
  "...byybbyyb...",
  "...bbbbbbbb...",
  "o...bwwwwb...o",
  "bb.bbbbbbbb.bb",
  ".bbbbBbbBbbbb.",
  "...bbbbbbbb...",
  "...bbBbbBbb...",
  "...bbbbbbbb...",
  "...bbb..bbb...",
  "...bb....bb...",
  "...bb....bb...",
  "..bbb....bbb..",
  "..kk......kk..",
};

static const char *const imp_dead[] = {
  "...R.rr..R....",
  "..RbbrbbRrr...",
  ".rbbbbBbbbbrR.",
  "RRrrbbrrbRRrrR",
};

static const char *const brute_a[] = {
  "................",
  "....PPPPPPPP....",
  "...PppppppppP...",
  "..PpprrpprrppP..",
  "..PppppppppppP..",
  "..PpkwkwkwkwpP..",
  "..PpkkkkkkkkpP..",
  ".PPpwkwkwkwkpPP.",
  "PppPppppppppPppP",
  "Ppp.PppppppP.ppP",
  "PP..PppppppP..PP",
  "....PppPPppP....",
  "...PppP..PppP...",
  "...Ppp....ppP...",
  "..PPpP....PpPP..",
  "..kkk......kkk..",
};

static const char *const brute_b[] = {
  "................",
  "....PPPPPPPP....",
  "...PppppppppP...",
  "..PpprrpprrppP..",
  "..PppppppppppP..",
  "..PpkwkwkwkwpP..",
  "..PpkkkkkkkkpP..",
  ".PPpwkwkwkwkpPP.",
  "PppPppppppppPppP",
  "Ppp.PppppppP.ppP",
  "PP..PppppppP..PP",
  "....PppPPppP....",
  "..PppP....PppP..",
  "..Ppp......ppP..",
  ".PPpP......PpPP.",
  ".kkk........kkk.",
};

static const char *const brute_atk[] = {
  "................",
  "....PPPPPPPP....",
  "...PppppppppP...",
  "..PppyyppyyppP..",
  "..PpwkwkwkwkpP..",
  "..PpkkkkkkkkpP..",
  "..PprrrrrrrrpP..",
  ".PPpwkwkwkwkpPP.",
  "PppPppppppppPppP",
  "Ppp.PppppppP.ppP",
  "PP..PppppppP..PP",
  "....PppPPppP....",
  "...PppP..PppP...",
  "...Ppp....ppP...",
  "..PPpP....PpPP..",
  "..kkk......kkk..",
};

static const char *const brute_dead[] = {
  "....PPpp.rr.....",
  "..PppprrppPPp...",
  "rRPPpppprrppPPRr",
};

static const char *const fireball[] = {
  "..oo..",
  ".oyyo.",
  "oyywyo",
  "oywyyo",
  ".oyyo.",
  "..oo..",
};

static const char *const health[] = {
  "wwwwwwww",
  "wwwrrwww",
  "wwwrrwww",
  "wrrrrrrw",
  "wrrrrrrw",
  "wwwrrwww",
  "wwwrrwww",
  "xxxxxxxx",
};

static const char *const ammo[] = {
  "..y..y..",
  "..y..y..",
  "GGGGGGGG",
  "GggggggG",
  "GgwwwwgG",
  "GggggggG",
  "GGGGGGGG",
  "GGGGGGGG",
};

static const char *const gun[] = {
  "........XSSX........",
  "........SkkS........",
  "........SssS........",
  "........SssS........",
  "........SssS........",
  "........SssS........",
  ".......XSssSX.......",
  ".......SSSSSS.......",
  ".......SbbbbS.......",
  "......hSbBBbSh......",
  ".....hhSbbbbShh.....",
  "....hhhSbBBbShhh....",
  "...hhhhhbbbbhhhhh...",
  "...HhhhhbBBbhhhhH...",
  "..HhhhhhbbbbhhhhhH..",
  "..HHhhhhbbbbhhhhHH..",
};

static const char *const flash[] = {
  "..y..y..",
  ".yoyyoy.",
  "yyowwoyy",
  ".yowwoy.",
  "..yyyy..",
  "...yy...",
};

#define ART(a, w) {(w), (uint8_t)(sizeof(a) / sizeof(a[0])), a}

const art_t arts[ART_COUNT] = {
  ART(imp_a, 14), ART(imp_b, 14), ART(imp_atk, 14), ART(imp_dead, 14),
  ART(brute_a, 16), ART(brute_b, 16), ART(brute_atk, 16), ART(brute_dead, 16),
  ART(fireball, 6), ART(health, 8), ART(ammo, 8), ART(gun, 20), ART(flash, 8),
};

typedef struct
{
  char c;
  uint8_t rgb[3];
} pal_t;

static const pal_t palette[] = {
  {'k', {20, 20, 20}},    {'r', {210, 30, 20}},   {'R', {120, 10, 10}},
  {'o', {240, 130, 20}},  {'y', {250, 230, 60}},  {'w', {240, 240, 240}},
  {'b', {140, 85, 40}},   {'B', {80, 45, 20}},    {'p', {230, 130, 150}},
  {'P', {160, 70, 90}},   {'g', {60, 180, 60}},   {'G', {30, 100, 30}},
  {'x', {150, 150, 150}}, {'X', {80, 80, 90}},    {'s', {60, 60, 70}},    {'S', {120, 120, 135}},
  {'h', {220, 170, 130}}, {'H', {160, 115, 85}},
};

const uint8_t *palette_rgb(char c)
{
  for(size_t i = 0; i < sizeof(palette) / sizeof(palette[0]); i++)
  {
    if(palette[i].c == c)
    {
      return palette[i].rgb;
    }
  }
  return NULL;
}
