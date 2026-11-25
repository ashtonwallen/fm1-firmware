/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * DUB DESK: ashtonwallen, 2026 */
/* The instrument (dub.h). One compilation unit, -O2. The command queue and the render's shape are AMB-1's (by way of
 * LOFI RADIO). */
#include "dub.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

#ifdef EN_HOST
void (*en_log)(int type, int bar, int step, int note, float vel);
#define LOG(t, n, v)                                                                                               \
    do {                                                                                                           \
        if (en_log)                                                                                                \
            en_log((t), tk.bar, tk.step, (n), (v));                                                                \
    } while (0)
#else
#define LOG(t, n, v) ((void)0)
#endif

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Engineer", 0, 100, 55}, {"Echo", 0, 100, 50},  {"Time", 0, 3, 1},     {"Bass", 0, 100, 70},
    {"Drums", 0, 100, 70},    {"Skank", 0, 100, 65}, {"Organ", 0, 100, 60}, {"Lead", 0, 100, 50},
    {"Spring", 0, 100, 50},   {"Tone", 0, 100, 55},  {"Tempo", 60, 86, 72}, {"Length", 0, 4, 1},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }
static const uint8_t LEN_MIN[5] = {2, 3, 4, 6, 8};
static const char *const TIME_TXT[4] = {"1/8", "3/16", "1/4", "3/8"};
static const uint8_t TIME_STEPS[4] = {2, 3, 4, 6};

static char *put_u(char *b, uint32_t v)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static char *put_s(char *b, const char *s)
{
    while ((*b = *s++))
        b++;
    return b;
}

void en_param_text(int i, int v, char *buf)
{
    if (i == P_LENGTH)
        put_s(put_u(buf, LEN_MIN[v < 0 ? 0 : v > 4 ? 4 : v]), " min");
    else if (i == P_TIME)
        put_s(buf, TIME_TXT[v < 0 ? 0 : v > 3 ? 3 : v]);
    else if (i == P_ENGINEER)
        put_s(buf, v < 20 ? "Asleep" : v < 45 ? "Easy" : v < 70 ? "Busy" : v < 90 ? "Wild" : "Mad");
    else
        put_u(buf, (uint32_t)(v < 0 ? 0 : v));
}

/* ------------------------------------------------------------ riddims --- */
/* a riddim: its key, its chords (a bar each, a cycle of 1, 2 or 4 bars; the bass's root placed in semitones from
 * the bass's tonic), its bass figure (1 or 2 bars of sixteenths: step, semitones above the bar's chord root, length)
 * and the drum styles it suits. All made for this, in the manner of the classic Kingston riddims. */
enum { Q_MIN, Q_MAJ, Q_MIN7 };
enum { DS_ONE = 1, DS_ROCK = 2, DS_STEP = 4 };
typedef struct {
    int8_t st, semi, len;
} bn_t;
typedef struct {
    const char *name;
    uint8_t key, minor, bt;                      /* the key's pitch class, minor, the bass's tonic (MIDI) */
    uint8_t ncyc;                                /* bars in the chord cycle */
    int8_t root[4];                              /* each bar's chord root, semitones from the tonic (placed) */
    uint8_t q[4];
    uint8_t fbars;                               /* the bass figure's bars */
    uint8_t nf[2];
    bn_t f[2][8];
    int8_t tempo;                                /* its tempo, from the style's */
    uint8_t drums;                               /* the drum styles it suits */
} riddim_t;
static const riddim_t RIDDIMS[EN_NRIDDIMS] = {
    {"Gully Bank", 9, 1, 33, 1, {0}, {Q_MIN}, 2, {6, 6},
     {{{0, 0, 3}, {3, 0, 1}, {4, 3, 2}, {6, 5, 2}, {8, 7, 5}, {14, 10, 2}},
      {{0, 12, 2}, {2, 10, 2}, {4, 7, 4}, {10, 5, 2}, {12, 3, 2}, {14, -2, 2}}}, 0, DS_ONE | DS_ROCK},
    {"Iron Gate", 4, 1, 40, 2, {0, -2}, {Q_MIN, Q_MAJ}, 2, {6, 6},
     {{{2, 0, 2}, {4, 7, 2}, {6, 10, 1}, {7, 12, 3}, {10, 10, 2}, {12, 7, 4}},
      {{2, 0, 2}, {4, 4, 2}, {6, 7, 2}, {8, 12, 3}, {12, 7, 2}, {14, 2, 2}}}, 0, DS_ONE | DS_ROCK},
    {"Dread Walk", 7, 1, 31, 1, {0}, {Q_MIN}, 2, {8, 7},
     {{{0, 0, 2}, {2, 0, 2}, {4, 3, 2}, {6, 5, 2}, {8, 7, 3}, {11, 5, 1}, {12, 3, 2}, {14, 0, 2}},
      {{0, 0, 2}, {2, 0, 2}, {4, 3, 2}, {6, 5, 2}, {8, 10, 4}, {12, 7, 2}, {14, 5, 2}}}, 2, DS_STEP | DS_ROCK},
    {"Back Yard", 0, 1, 36, 2, {0, 5}, {Q_MIN, Q_MIN}, 2, {6, 7},
     {{{0, 0, 3}, {3, 0, 1}, {6, 7, 2}, {8, 10, 2}, {10, 7, 2}, {12, 3, 4}},
      {{0, 0, 3}, {3, 0, 1}, {6, 7, 2}, {8, 3, 2}, {10, 0, 2}, {12, -2, 2}, {14, -5, 2}}}, -2, DS_ONE},
    {"Mango Crate", 7, 0, 31, 4, {0, 5, 7, 5}, {Q_MAJ, Q_MAJ, Q_MAJ, Q_MAJ}, 1, {6},
     {{{0, 0, 3}, {4, 4, 2}, {6, 7, 2}, {8, 9, 3}, {12, 7, 2}, {14, 4, 2}}}, 2, DS_ONE | DS_ROCK},
    {"Hill Road", 2, 1, 38, 1, {0}, {Q_MIN7}, 2, {5, 6},
     {{{0, 0, 5}, {6, 0, 1}, {7, 3, 2}, {10, 5, 2}, {12, 7, 4}},
      {{0, 10, 3}, {4, 7, 2}, {6, 5, 2}, {8, 3, 4}, {12, 0, 2}, {14, -2, 2}}}, -3, DS_ONE},
    {"Bubbler", 9, 1, 33, 4, {0, 0, 5, 5}, {Q_MIN, Q_MIN, Q_MIN, Q_MIN}, 1, {7},
     {{{0, 0, 2}, {3, 0, 1}, {4, 3, 2}, {6, 5, 3}, {10, 7, 2}, {12, 5, 2}, {14, 3, 2}}}, 0, DS_ONE | DS_ROCK},
    {"Tin Roof Rattle", 5, 1, 29, 2, {0, -2}, {Q_MIN, Q_MAJ}, 2, {7, 7},
     {{{0, 0, 2}, {2, 0, 2}, {4, 12, 2}, {6, 10, 2}, {8, 7, 4}, {12, 3, 2}, {14, 5, 2}},
      {{0, 0, 2}, {2, 0, 2}, {4, 12, 2}, {6, 7, 2}, {8, 4, 4}, {12, 7, 2}, {14, 2, 2}}}, 2, DS_ROCK | DS_STEP},
    {"Coal Pot", 11, 1, 35, 2, {0, 5}, {Q_MIN7, Q_MIN}, 2, {6, 6},
     {{{2, 0, 2}, {4, 7, 2}, {6, 0, 2}, {8, 3, 3}, {12, 5, 2}, {14, 7, 2}},
      {{2, 0, 2}, {4, 7, 2}, {6, 0, 2}, {8, 3, 3}, {12, -2, 2}, {14, -5, 2}}}, -2, DS_ONE},
    {"Marching Feet", 4, 1, 28, 1, {0}, {Q_MIN}, 2, {8, 7},
     {{{0, 0, 2}, {3, 0, 1}, {4, 12, 2}, {6, 0, 2}, {8, 10, 2}, {10, 7, 2}, {12, 5, 2}, {14, 3, 2}},
      {{0, 0, 2}, {3, 0, 1}, {4, 12, 2}, {6, 0, 2}, {8, 7, 4}, {12, 3, 2}, {14, -2, 2}}}, 3, DS_STEP},
    {"Spring Water", 0, 0, 36, 2, {0, -7}, {Q_MAJ, Q_MAJ}, 1, {7},
     {{{0, 0, 3}, {3, 0, 1}, {4, 4, 2}, {6, 7, 4}, {10, 9, 2}, {12, 7, 2}, {14, 4, 2}}}, 0, DS_ONE},
    {"Fifth Street", 2, 0, 38, 4, {0, 5, -5, 5}, {Q_MAJ, Q_MAJ, Q_MAJ, Q_MAJ}, 1, {7},
     {{{0, 0, 2}, {2, 0, 2}, {6, 4, 2}, {8, 7, 3}, {11, 7, 1}, {12, 9, 2}, {14, 7, 2}}}, 2, DS_ROCK | DS_STEP},
    {"Yard Steps", 9, 1, 33, 2, {0, -2}, {Q_MIN, Q_MAJ}, 2, {6, 6},
     {{{0, 0, 3}, {3, 3, 1}, {4, 5, 2}, {6, 7, 2}, {8, 0, 4}, {14, -2, 2}},
      {{0, 0, 3}, {3, 2, 1}, {4, 4, 2}, {6, 7, 2}, {8, 0, 4}, {14, 2, 2}}}, 0, DS_ONE | DS_ROCK},
    {"Lead Pipe", 2, 1, 38, 1, {0}, {Q_MIN}, 2, {5, 4},
     {{{0, 0, 4}, {6, 0, 2}, {8, 12, 1}, {9, 10, 3}, {12, 7, 4}},
      {{0, 5, 4}, {6, 3, 2}, {8, 0, 6}, {14, -2, 2}}}, -3, DS_ONE | DS_STEP},
    {"Red Clay Lane", 7, 1, 31, 2, {0, 5}, {Q_MIN, Q_MIN}, 2, {7, 8},
     {{{0, 0, 2}, {3, 0, 1}, {4, 3, 1}, {5, 5, 3}, {8, 7, 2}, {10, 10, 2}, {12, 7, 4}},
      {{0, 0, 2}, {3, 0, 1}, {4, 3, 1}, {5, 5, 3}, {8, 7, 2}, {10, 3, 2}, {12, 0, 2}, {14, -5, 2}}}, 0,
     DS_ROCK | DS_STEP},
    {"Ital Stew", 4, 1, 28, 2, {0, 5}, {Q_MIN, Q_MIN}, 1, {7},
     {{{0, 0, 1}, {2, 0, 2}, {4, 3, 2}, {6, 7, 2}, {8, 10, 2}, {10, 7, 2}, {12, 0, 4}}}, -1, DS_ONE | DS_ROCK},
};
const char *en_riddim_name(int i) { return RIDDIMS[(i % EN_NRIDDIMS + EN_NRIDDIMS) % EN_NRIDDIMS].name; }

/* ------------------------------------------------------------- styles --- */
/* the user's eleven (Engineer Echo Time Bass, Drums Skank Organ Lead, Spring Tone Tempo) and the style's flavour:
 * drums: the drum styles; snare: 0 the rim, 1 a snare, 2 the rim in a one-drop and a snare otherwise; dbl: % of
 * versions with a double skank; mel, horn, siren: how often (%); wild: the engineer's temper (%, x his knob); swing:
 * the sixteenths' shuffle (% of a sixteenth); riddims: which (bit i); arcs: which plans (bit i) */
typedef struct {
    const char *name;
    int8_t v[P_NSTYLE];
    uint8_t drums, snare, dbl, mel, horn, siren, wild, swing;
    uint16_t riddims;
    uint8_t arcs;
} style_t;
#define RB(i) (1u << (i))
static const style_t STYLES[EN_NSTYLES] = {
    /*                 Eng Ech Tim Bas  Drm Skn Org Led  Spr Ton Tem    drums  sn dbl mel hrn sir wild swing */
    {"Sixpence Bass",    {55, 50, 1, 70, 70, 65, 60, 50, 50, 55, 72},   DS_ONE, 0, 0, 55, 30, 10, 100, 22,
     RB(0) | RB(1) | RB(3) | RB(5) | RB(6) | RB(8) | RB(12), 0x1F},
    {"Jukebox Lion",    {50, 45, 1, 75, 75, 70, 55, 45, 40, 62, 76},   DS_ROCK, 1, 70, 25, 45, 10, 90, 15,
     RB(0) | RB(1) | RB(6) | RB(7) | RB(11) | RB(12) | RB(14), 0x1F},
    {"Four Boots Down",       {55, 50, 2, 80, 80, 60, 50, 45, 40, 58, 78},   DS_STEP, 1, 50, 20, 50, 30, 100, 10,
     RB(2) | RB(7) | RB(9) | RB(11) | RB(13) | RB(14), 0x1B},
    {"Sufferah Smile",     {35, 40, 1, 65, 70, 70, 65, 60, 40, 65, 74},   DS_ONE | DS_ROCK, 0, 20, 45, 60, 0, 70, 22,
     RB(4) | RB(10) | RB(11) | RB(12) | RB(6), 0x15},
    {"Blue Mouthpiece",   {40, 45, 2, 65, 65, 55, 55, 75, 50, 52, 68},   DS_ONE, 0, 0, 90, 10, 0, 75, 24,
     RB(0) | RB(3) | RB(5) | RB(8) | RB(12) | RB(15), 0x07},
    {"Kerosene Lamp",  {45, 60, 3, 75, 60, 45, 40, 45, 60, 35, 65},   DS_ONE, 0, 0, 45, 0, 5, 80, 25,
     RB(5) | RB(8) | RB(13) | RB(15) | RB(3), 0x0B},
    {"Burnt Altar",      {80, 65, 1, 70, 65, 60, 55, 45, 75, 40, 68},   DS_ONE | DS_ROCK, 0, 30, 40, 25, 40, 150, 26,
     RB(0) | RB(3) | RB(5) | RB(6) | RB(8) | RB(12) | RB(15), 0x0A},
    {"Bass Bin Prayer",   {50, 45, 1, 100, 75, 50, 35, 35, 40, 50, 72},  DS_STEP | DS_ROCK, 1, 30, 20, 20, 20, 90, 14,
     RB(2) | RB(5) | RB(9) | RB(13) | RB(14) | RB(7), 0x0B},
    {"Ricochet Room",   {95, 75, 1, 70, 70, 60, 50, 45, 60, 55, 70},   DS_ROCK | DS_ONE, 0, 20, 40, 35, 35, 160, 20,
     RB(0) | RB(1) | RB(3) | RB(5) | RB(6) | RB(7) | RB(8) | RB(12) | RB(13) | RB(14) | RB(15), 0x0A},
    {"Generator Lawn",   {65, 50, 1, 85, 85, 70, 60, 55, 45, 65, 80},   DS_STEP | DS_ROCK, 1, 60, 30, 70, 60, 110, 12,
     RB(1) | RB(2) | RB(7) | RB(9) | RB(11) | RB(14), 0x1F},
    {"Biscuit Tin Mic",      {35, 40, 0, 70, 75, 65, 80, 45, 35, 62, 76},   DS_ROCK, 2, 40, 30, 40, 0, 70, 18,
     RB(1) | RB(4) | RB(6) | RB(7) | RB(11) | RB(12), 0x15},
    {"Acetate Only",      {70, 60, 2, 85, 75, 45, 40, 40, 55, 48, 70},   DS_ONE | DS_STEP, 0, 0, 25, 15, 25, 120, 20,
     RB(0) | RB(2) | RB(5) | RB(9) | RB(13) | RB(14), 0x0A},
    {"Church Shoes", {25, 35, 2, 60, 60, 60, 70, 65, 35, 60, 70},   DS_ONE, 0, 0, 70, 50, 0, 55, 24,
     RB(4) | RB(6) | RB(10) | RB(11) | RB(12), 0x15},
};
const char *en_style_name(int i) { return STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES].name; }
void en_style_recipe(int i, int16_t *par)
{
    int k;
    const style_t *s = &STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES];
    for (k = 0; k < P_NSTYLE; k++)
        par[k] = s->v[k];
}

const char *en_chord_name(int root, int q, char *buf)
{
    put_s(put_s(buf, EN_NOTE_NAME[(root % 12 + 12) % 12]), q == Q_MIN ? "m" : q == Q_MIN7 ? "m7" : "");
    return buf;
}

/* ---------------------------------------------------- the engineer's plan --- */
/* a block: four bars of the desk (which channels are up, a bar each) and what he does in it. HO: the horns may
 * stab; ME: the melodica's phrase, when this version has one */
#define DR 1
#define BA 2
#define GT 4
#define OR 8
#define ME 16
#define HO 32
#define FB (DR | BA | GT | OR | HO)
enum { FL_FILL = 1, FL_SWEEP = 2, FL_DROP = 4, FL_SWELL = 8, FL_SIREN = 16, FL_CRASH = 32, FL_THROW = 64 };
typedef struct {
    uint8_t m[4], fl;
} block_t;
enum {
    B_INTRO_A, B_INTRO_B, B_INTRO_C, B_FULL, B_FULLM, B_RIDDIM, B_KEYS, B_DNB, B_DNB_SK, B_DRUMS2, B_NOBASS, B_BUILD,
    B_STRIP, B_TEASE, B_MELDNB, B_OUTRO, NBLOCKS
};
static const block_t BLOCKS[NBLOCKS] = {
    {{DR, DR | BA, DR | BA, DR | BA | GT}, FL_FILL},                   /* intro: drums, the bass, the skank */
    {{FB, FB, FB, FB}, FL_FILL},                                       /* intro: the band from the top */
    {{DR | BA, DR | BA, DR | BA | OR, DR | BA | OR | GT}, FL_FILL},    /* intro: drum and bass, the band piles in */
    {{FB, FB, FB, FB}, 0},                                             /* the full band */
    {{FB | ME, FB | ME, FB | ME, FB | ME}, 0},                         /* and the melodica */
    {{DR | BA | GT, DR | BA | GT, DR | BA | GT, DR | BA | GT}, 0},     /* the organ out */
    {{DR | BA | OR, DR | BA | OR, DR | BA | OR, DR | BA | OR}, 0},     /* the skank out, the bubble alone */
    {{DR | BA, DR | BA, DR | BA, DR | BA}, FL_THROW},                  /* drum and bass */
    {{DR | BA, DR | BA, DR | BA | GT, DR | BA}, FL_THROW},             /* drum and bass, a skank peeks in */
    {{DR, DR, DR | BA, DR | BA}, FL_CRASH | FL_THROW},                 /* the drums alone, the bass back */
    {{DR | GT | OR, DR | GT | OR, FB, FB}, 0},                         /* the bass pulled, then slammed back */
    {{DR | BA, DR | BA, DR | BA | OR, DR | BA | OR | GT}, FL_SWEEP | FL_DROP},   /* a build: the high-pass, a drop */
    {{FB, DR | BA | GT, DR | BA, DR | BA}, FL_SWELL},                  /* stripped back, bar by bar */
    {{DR | BA, FB, DR | BA, DR | BA}, FL_THROW},                       /* the band peeks in */
    {{DR | BA | ME, DR | BA | ME, DR | BA | ME, DR | BA | ME}, 0},     /* the melodica over drum and bass */
    {{DR | BA, DR | BA, DR, DR}, FL_SWELL | FL_SIREN},                 /* the end: down to the drums, a last throw */
};
/* plans: the blocks after the intro, in order (looped for a long version) */
#define ARC_N 16
static const uint8_t ARCS[5][ARC_N] = {
    {B_FULL, B_FULLM, B_FULLM, B_STRIP, B_DNB, B_DNB_SK, B_BUILD, B_FULL, B_FULLM, B_KEYS, B_NOBASS, B_FULL, B_FULLM,
     B_STRIP, B_DNB, B_BUILD},
    {B_FULL, B_STRIP, B_DNB, B_DRUMS2, B_TEASE, B_BUILD, B_FULLM, B_MELDNB, B_DNB, B_NOBASS, B_FULL, B_STRIP,
     B_DNB_SK, B_DRUMS2, B_BUILD, B_FULLM},
    {B_FULLM, B_FULL, B_KEYS, B_FULLM, B_STRIP, B_MELDNB, B_DNB, B_BUILD, B_FULLM, B_FULL, B_RIDDIM, B_FULLM,
     B_STRIP, B_DNB, B_BUILD, B_FULL},
    {B_DNB, B_DNB_SK, B_DRUMS2, B_TEASE, B_DNB, B_BUILD, B_FULL, B_STRIP, B_MELDNB, B_DRUMS2, B_NOBASS, B_BUILD,
     B_FULL, B_STRIP, B_DNB, B_TEASE},
    {B_FULL, B_FULLM, B_RIDDIM, B_KEYS, B_FULL, B_STRIP, B_DNB, B_BUILD, B_FULLM, B_FULL, B_KEYS, B_NOBASS, B_FULLM,
     B_FULL, B_STRIP, B_BUILD},
};
/* a calm engineer strips less */
static int tame(int b, float a)
{
    if (a < 0.3f && (b == B_DRUMS2 || b == B_TEASE))
        b = B_DNB;
    if (a < 0.15f) {
        if (b == B_DNB || b == B_DNB_SK || b == B_BUILD)
            b = B_RIDDIM;
        else if (b == B_STRIP || b == B_NOBASS)
            b = B_FULL;
        else if (b == B_MELDNB)
            b = B_FULLM;
    }
    return b;
}

/* the melodica's rhythms: a call (two bars: start, length in sixteenths) and its answer's second bar */
typedef struct {
    uint8_t nc, ne;
    uint8_t c[8][2], e[5][2];
} mrh_t;
static const mrh_t MRH[] = {
    {8, 4, {{0, 4}, {4, 2}, {6, 2}, {8, 8}, {16, 2}, {18, 2}, {20, 4}, {24, 8}}, {{0, 2}, {2, 2}, {4, 4}, {8, 8}}},
    {7, 3, {{2, 2}, {4, 4}, {8, 2}, {10, 6}, {20, 2}, {22, 2}, {24, 8}}, {{4, 2}, {6, 2}, {8, 8}}},
    {6, 3, {{0, 6}, {6, 2}, {8, 4}, {12, 4}, {16, 4}, {20, 12}}, {{0, 3}, {3, 1}, {4, 12}}},
    {7, 2, {{4, 4}, {8, 2}, {10, 2}, {12, 4}, {16, 8}, {24, 4}, {28, 4}}, {{0, 8}, {8, 8}}},
    {7, 3, {{0, 3}, {3, 3}, {6, 2}, {8, 8}, {16, 4}, {20, 4}, {24, 8}}, {{0, 4}, {4, 4}, {8, 8}}},
    {8, 4, {{2, 1}, {3, 3}, {6, 2}, {8, 8}, {18, 2}, {20, 2}, {22, 2}, {24, 8}}, {{2, 2}, {4, 2}, {6, 2}, {8, 8}}},
};
#define NMRH (sizeof MRH / sizeof MRH[0])

/* horn stabs: a bar's hits (start, length) */
typedef struct {
    uint8_t n, h[3][2];
} hpat_t;
static const hpat_t HPATS[] = {
    {2, {{2, 2}, {6, 6}}},
    {3, {{0, 2}, {3, 1}, {4, 6}}},
    {2, {{10, 2}, {12, 4}}},
    {1, {{0, 10}}},
    {3, {{6, 1}, {7, 1}, {8, 6}}},
};
#define NHPATS (sizeof HPATS / sizeof HPATS[0])

/* the drums: a bar each. Kick X; rim/snare X, g a ghost; hats x closed, s soft, o open */
typedef struct {
    const char *k, *s, *h;
} groove_t;
static const groove_t GROOVES[3][2] = {
    {{"........X.......", "........X.......", "x.x.x.x.x.x.x.x."},       /* one-drop */
     {"........X.......", "........X......g", "x.xsx.x.x.xsx.x."}},
    {{"X.......X.......", "........X.......", "x.o.x.o.x.o.x.o."},       /* rockers */
     {"X.......X.....x.", "........X.......", "x.o.x.xsx.o.x.os"}},
    {{"X...X...X...X...", "........X.......", "x.o.x.o.x.o.x.o."},       /* steppers */
     {"X...X...X...X...", "....g...X.......", "xso.xso.xso.xso."}},
};
/* fills over a bar's end: (step x4, so quarter steps: rolls, triplets; what; velocity %). What: 0 rim/snare, 1..3
 * toms high to low */
typedef struct {
    uint8_t from, n;
    uint8_t e[8][3];
} fill_t;
static const fill_t FILLS[] = {
    {12, 4, {{48, 0, 45}, {52, 0, 55}, {56, 0, 70}, {60, 0, 85}}},
    {8, 4, {{32, 1, 70}, {40, 1, 70}, {48, 2, 75}, {56, 3, 85}}},
    {10, 4, {{40, 0, 50}, {44, 0, 60}, {48, 2, 75}, {56, 3, 85}}},
    {12, 8, {{48, 0, 30}, {50, 0, 36}, {52, 0, 42}, {54, 0, 50}, {56, 0, 58}, {58, 0, 66}, {60, 0, 76}, {62, 0, 88}}},
    {12, 3, {{48, 1, 70}, {53, 2, 75}, {59, 3, 85}}},
    {8, 5, {{32, 0, 80}, {44, 1, 60}, {48, 1, 70}, {52, 2, 75}, {58, 3, 90}}},
};
#define NFILLS (sizeof FILLS / sizeof FILLS[0])

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_STYLE, C_PLAY, C_SKIP, C_REPEAT, C_TRACK_NO, C_TRACK_SEED_HI, C_TRACK_SEED_LO, C_NOTE, C_PANIC, C_THROW, C_CRASH };
#define QN 64u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void en_set(int p, int v) { post(C_SET, p, v); }
void en_style(int i) { post(C_STYLE, i, 0); }
void en_play(int on) { post(C_PLAY, on, 0); }
void en_skip(void) { post(C_SKIP, 0, 0); }
void en_repeat(int on) { post(C_REPEAT, on, 0); }
void en_note(int note, int vel) { post(C_NOTE, note, vel); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_throw(void) { post(C_THROW, 0, 0); }
void en_crash(void) { post(C_CRASH, 0, 0); }
void en_track(uint32_t no, uint32_t seed)
{
    post(C_TRACK_SEED_HI, 0, (int)(seed >> 16));
    post(C_TRACK_SEED_LO, 0, (int)(seed & 0xFFFFu));
    post(C_TRACK_NO, (int)(no >> 16 & 0xFFu), (int)(no & 0xFFFFu));
}

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_phase, en_repeat_on;
volatile uint32_t en_track_no, en_track_seed, en_seed_base = 0x44554231u, en_track_ms, en_track_len_ms;
volatile uint8_t en_riddim, en_key, en_minor, en_bpm = 72, en_chord_root, en_chord_q, en_bar_in_block, en_step;
volatile uint16_t en_chord_pcs;
volatile float en_fader[EN_NCH], en_meter[EN_NCH], en_send[EN_NCH];
volatile float en_echo_fb, en_echo_out, en_echo_s = 0.5f, en_spring, en_hp, en_siren;
volatile uint8_t en_move;
volatile uint32_t en_move_no;
volatile float en_vu_l, en_vu_r, en_kick_glow;
volatile uint32_t en_beats, en_tracks_played;

static int16_t par[P_NPARAMS];
static int style_i;
static uint32_t rng = 0x9E3779B9u, nrng = 0x2545F491u, trng = 1u;

static inline uint32_t xs(uint32_t *s)          /* Marsaglia's xorshift (13, 17, 5) */
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
static float frand(void) { return (float)(xs(&rng) >> 8) * (1.0f / 16777216.0f); }
static float trand(void) { return (float)(xs(&trng) >> 8) * (1.0f / 16777216.0f); }   /* the version's own */
static int tirand(int n) { return n > 0 ? (int)((xs(&trng) >> 8) % (uint32_t)n) : 0; }
static inline float noise(void) { return (float)(int32_t)xs(&nrng) * (1.0f / 2147483648.0f); }

uint32_t en_track_rand(uint32_t base, uint32_t no)
{
    uint32_t h = base ^ (no * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h ? h : 1u;
}

/* the sine, from a table: phase in turns (>= -8) */
#define SN 1024
static float SINT[SN + 1];
static inline __attribute__((always_inline)) float sn(float ph)
{
    float x = (ph + 8.0f) * (float)SN;
    int32_t i = (int32_t)x;
    float f = x - (float)i;
    i &= SN - 1;
    return SINT[i] + (SINT[i + 1] - SINT[i]) * f;
}
static inline float wrap1(float p) { return p >= 1.0f ? p - 1.0f : p; }
static inline float mtof(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }
static inline float tau(float sec) { return fm_expf(-1.0f / (sec * EN_SR)); }   /* a per-sample decay */
static inline float onepole(float fc) { return 1.0f - fm_expf(-FM_TWO_PI * fc / EN_SR); }

/* -------------------------------------------------------------- drums --- */
static struct {
    float ph, f, fend, pdec, amp, adec, click;
} kick;
static struct {
    float ph, inc, tamp, tdec, namp, ndec, lo, bd, lp;
} snare;
static struct {
    float ph, inc, amp, dec, namp, ndec, lo, bd;
} rim;
static struct {
    float amp, dec, h1, h2, lp, lp2;
} hat;
static struct {
    float ph, f, fend, pdec, amp, adec;
} tom;

static void kick_hit(float v)
{
    kick.ph = 0.0f;
    kick.f = 118.0f;
    kick.fend = 54.0f + 3.0f * frand();
    kick.pdec = tau(0.035f);
    kick.amp = v;
    kick.adec = tau(0.30f);
    kick.click = v * 0.12f;
    en_kick_glow = 1.0f;
}
static void snare_hit(float v)
{
    snare.ph = 0.0f;
    snare.inc = (228.0f + 8.0f * frand()) / EN_SR;
    snare.tamp = v * 0.6f;
    snare.tdec = tau(0.045f);
    snare.namp = v;
    snare.ndec = tau(0.085f + 0.02f * frand());
}
static void rim_hit(float v)
{
    rim.ph = 0.0f;
    rim.inc = (505.0f + 10.0f * frand()) / EN_SR;
    rim.amp = v * 0.8f;
    rim.dec = tau(0.028f);
    rim.namp = v * 0.9f;
    rim.ndec = tau(0.012f);
}
static void hat_hit(float v, int open)
{
    hat.amp = v;
    hat.dec = tau(open ? 0.20f : 0.028f + 0.01f * frand());
}
static void tom_hit(int which, float v)              /* 1 high .. 3 low */
{
    static const float F[4] = {0.0f, 196.0f, 150.0f, 112.0f};
    tom.ph = 0.0f;
    tom.fend = F[which & 3];
    tom.f = tom.fend * 1.5f;
    tom.pdec = tau(0.05f);
    tom.amp = v;
    tom.adec = tau(0.22f);
}

/* --------------------------------------------------------------- bass --- */
static struct {
    float ph, inc, tinc, amp, peak, sus, idx, lp, lp2;
    int32_t gate;
    uint8_t on, atk;
} bs;
static void bass_on(int note, float vel, int32_t gate)
{
    float f = mtof((float)note);
    if (!bs.on || bs.amp < 0.05f || bs.gate <= 0)  /* from silence, or a new note: there; legato: a quick glide */
        bs.inc = f / EN_SR;
    bs.tinc = f / EN_SR;
    bs.on = 1;
    bs.atk = 1;
    bs.gate = gate;
    bs.peak = vel;
    bs.sus = vel * 0.8f;
    bs.idx = 0.35f + 0.25f * vel;
}

/* -------------------------------------------------------------- skank --- */
#define NGT 6
static struct {
    float ph, inc, idx, amp, dec;
    int32_t gate;
    uint8_t on;
} gt[NGT];
static float gt_chk, gt_hp, gt_lp, gt_nhp;
static void gtr_on(int note, float vel, int32_t gate)
{
    int i, k = 0;
    float lo = 9.0f;
    for (i = 0; i < NGT; i++) {                  /* a free voice, else the quietest */
        if (!gt[i].on) {
            k = i;
            break;
        }
        if (gt[i].amp < lo)
            lo = gt[i].amp, k = i;
    }
    gt[k].ph = 0.0f;
    gt[k].inc = mtof((float)note) / EN_SR;
    gt[k].idx = 1.3f + 1.2f * vel;
    gt[k].amp = vel;
    gt[k].dec = tau(0.16f);
    gt[k].gate = gate;
    gt[k].on = 1;
    gt_chk = fm_maxf(gt_chk, vel * 0.5f);
}

/* -------------------------------------------------------------- organ --- */
#define NOR 10
static struct {
    float ph, inc, amp, tgt, att;
    int32_t gate;                                /* samples to the release; < 0 held by the player */
    uint8_t on, note, player;
} org[NOR];
static void org_on(int note, float vel, int32_t gate, int player)
{
    int i, k = -1;
    float lo = 9.0f;
    while (note < 36)
        note += 12;
    while (note > 96)
        note -= 12;
    for (i = 0; i < NOR; i++)
        if (!org[i].on) {
            k = i;
            break;
        }
    if (k < 0)
        for (i = 0; i < NOR; i++)
            if (!org[i].player && org[i].amp < lo)
                lo = org[i].amp, k = i;
    if (k < 0)
        k = 0;
    org[k].ph = 0.0f;
    org[k].inc = mtof((float)note) / EN_SR;
    org[k].amp = 0.0f;
    org[k].tgt = vel;
    org[k].att = vel / (0.004f * EN_SR);
    org[k].gate = gate;
    org[k].on = 1;
    org[k].note = (uint8_t)note;
    org[k].player = (uint8_t)player;
}
static void org_off_player(int note)
{
    int i;
    for (i = 0; i < NOR; i++)
        if (org[i].on && org[i].player && org[i].note == note && org[i].gate < 0)
            org[i].gate = 0;
}

/* ------------------------------------------------------------ melodica --- */
static struct {
    float ph, inc, tinc, amp, peak, vib, vph, lp;
    int32_t gate;
    uint8_t on;
} mel;
static void mel_on(int note, float vel, int32_t gate)
{
    float f = mtof((float)note) / EN_SR;
    if (!mel.on || mel.amp < 0.02f) {
        mel.inc = f;
        mel.amp = 0.0f;
        mel.vib = 0.0f;
    }
    mel.tinc = f;
    mel.peak = vel;
    mel.gate = gate;
    mel.on = 1;
}

/* -------------------------------------------------------------- horns --- */
#define NHN 3
static struct {
    float ph, inc, scoop, idx, amp, peak, t;
    int32_t gate;
    uint8_t on;
} hn[NHN];
static void horn_on(int i, int note, float vel, int32_t gate)
{
    hn[i].ph = 0.0f;
    hn[i].inc = mtof((float)note) / EN_SR;
    hn[i].scoop = 0.965f;
    hn[i].idx = 0.4f;
    hn[i].amp = 0.0f;
    hn[i].peak = vel;
    hn[i].t = 0.0f;
    hn[i].gate = gate;
    hn[i].on = 1;
}

/* -------------------------------------------------------------- siren --- */
static struct {
    float ph, lfo, f, amp;
    int32_t left;
    uint8_t mode, on;
} sir;

/* ------------------------------------------------------------- spaces --- */
/* the tape echo: one long loop of 16-bit samples (1.48 s), its head moving with the time */
#define EC_N 65536
static int16_t ec_buf[EC_N] EN_POOL;
static int ec_w;
static float ec_d = 20000.0f, ec_t = 20000.0f, ec_hp, ec_lp, ec_lp2, ec_wow, ec_pan;
static float fb_boost, fb_boost_t;
static int32_t swell_left;

/* the spring tank: two springs, each a delay with a chain of one-sample all-passes (the chirp) in its loop */
#define SP0 1187
#define SP1 1543
#define NAP 6
static float sp_buf[SP0 + SP1] EN_POOL;
static float *sp_d[2];
static const int SP_N[2] = {SP0, SP1};
static int sp_p[2];
static float sp_ax[2][NAP], sp_ay[2][NAP], sp_lp[2], sp_hp;
static float crash_amp, crash_fb;

/* -------------------------------------------------------- the sequence --- */
#define BLK 256
#define NEV 64
enum { E_KICK, E_RIM, E_SNARE, E_HAT, E_OHAT, E_TOM, E_BASS, E_GTR, E_ORG, E_MEL, E_HORN, E_THROW, E_CRASH, E_SWELL };
typedef struct {
    int32_t wait;
    uint8_t type, note, used, aux;
    float vel;
    int32_t gate;
} ev_t;
static ev_t evq[NEV];

static void sched(int type, int32_t wait, int note, float vel, int32_t gate, int aux)
{
    int i;
    for (i = 0; i < NEV; i++)
        if (!evq[i].used) {
            evq[i].used = 1;
            evq[i].type = (uint8_t)type;
            evq[i].wait = wait < 0 ? 0 : wait;
            evq[i].note = (uint8_t)note;
            evq[i].vel = vel;
            evq[i].gate = gate;
            evq[i].aux = (uint8_t)aux;
            return;
        }
}

/* the version */
#define MAXBLK 48
typedef struct {
    int8_t st, len, note;
} mn_t;
static struct {
    const riddim_t *r;
    int ri;
    int drums;                                   /* 0 one-drop, 1 rockers, 2 steppers */
    int gvar, dbl, bubble, rimsn, hpat, mel_on;
    int nblk, blk[MAXBLK];
    int bar, step;                               /* where the sequence is */
    float step_left;
    int on, prev_on;                             /* the channels up this bar, the last */
    int fill, fill_from, drop_from, horn_bar;
    int mel_n[4];
    mn_t mel[4][8];                              /* the melodica's phrase: four bars */
    int vs[3], vo[3], vh[3];                     /* the skank's, the organ's and the horns' voicings */
    int skipping, skipped;
} tk;

static float step_len = 9187.0f;                 /* samples a sixteenth */
static uint32_t trk_samples;
static float hp_amt, hp_t, hp_rate;              /* the high-pass sweep */
static float ch_g[EN_NCH], ch_gt[EN_NCH];        /* each channel's gain, its target (the mutes) */
static float ch_s[EN_NCH];                       /* each channel's echo send */
static int32_t thr_left[EN_NCH];                 /* a throw's samples left */
static float fade = 0.0f, fade_t = 1.0f, fade_rate = 1.0f / 44100.0f;

static float actv(void)                          /* the engineer: his knob, his temper */
{
    return fm_clampf((float)par[P_ENGINEER] * 0.01f * (float)STYLES[style_i].wild * 0.01f, 0.0f, 1.6f);
}
static void move(int m)
{
    en_move = (uint8_t)m;
    en_move_no++;
    LOG(L_MOVE, m, 0.0f);
}

static int cur_bpm(void)
{
    int b = par[P_TEMPO] + (tk.r ? tk.r->tempo : 0);
    return b < 58 ? 58 : b > 88 ? 88 : b;
}
static void tempo_set(void)
{
    int b = cur_bpm();
    en_bpm = (uint8_t)b;
    step_len = EN_SR * 15.0f / (float)b;
    en_track_len_ms = (uint32_t)((float)(tk.nblk * 64) * step_len * (1000.0f / EN_SR));
    ec_t = fm_minf(step_len * (float)TIME_STEPS[par[P_TIME] & 3], (float)(EC_N - 600));
    en_echo_s = ec_t / EN_SR;
}

/* the chord at a bar of the version: its root's pitch class, quality, the bass's root (MIDI) */
static int chord_q(int bar) { return tk.r->q[bar % tk.r->ncyc]; }
static int chord_pc(int bar) { return (tk.r->key + tk.r->root[bar % tk.r->ncyc] + 24) % 12; }
static int chord_pcs(int bar, int *pc)
{
    int r = chord_pc(bar), q3 = chord_q(bar);
    pc[0] = r;
    pc[1] = (r + (q3 == Q_MAJ ? 4 : 3)) % 12;
    pc[2] = (r + 7) % 12;
    pc[3] = (r + 10) % 12;
    return q3 == Q_MIN7 ? 4 : 3;
}

/* a close triad (three of the chord's notes) round a centre, moving as little as it can from the last */
static void voice3(int bar, int centre, int *v)
{
    int pc[4], n = chord_pcs(bar, pc), inv, best[3] = {0, 0, 0}, bd = 1 << 30, prev = (v[0] + v[1] + v[2]) / 3, i;
    int use[3];
    use[0] = pc[0], use[1] = pc[1], use[2] = n == 4 ? pc[3] : pc[2];   /* a seventh replaces the fifth */
    if (prev < centre - 7 || prev > centre + 7)
        prev = centre;
    for (inv = 0; inv < 3; inv++) {
        int x[3], j, mean, d = 0;
        x[0] = centre - 6 + ((use[inv] - (centre - 6)) % 12 + 12) % 12;
        for (j = 1; j < 3; j++) {
            int pcj = use[(inv + j) % 3];
            x[j] = x[j - 1] + ((pcj - x[j - 1]) % 12 + 12) % 12;
            if (x[j] == x[j - 1])
                x[j] += 12;
        }
        mean = (x[0] + x[1] + x[2]) / 3;
        while (mean > centre + 6) {
            for (j = 0; j < 3; j++)
                x[j] -= 12;
            mean -= 12;
        }
        for (j = 0; j < 3; j++) {
            int e = x[j] - v[j];
            d += e < 0 ? -e : e;
        }
        d += 2 * (mean > prev ? mean - prev : prev - mean) + (mean > centre ? mean - centre : centre - mean);
        if (d < bd) {
            bd = d;
            for (j = 0; j < 3; j++)
                best[j] = x[j];
        }
    }
    for (i = 0; i < 3; i++)
        v[i] = best[i];
}

/* the melodica's phrase: a call over bars 0-1, its answer over 2-3 (the call's first bar again, then home). Chord
 * tones on the strong beats, scale steps between, the call ending open (third or fifth), the answer on the root */
static int near_pc(int from, int pcm, int lo, int hi, int dir)   /* the nearest note in the set pcm (bit i) */
{
    int d, best = from, bd = 99;
    for (d = -7; d <= 7; d++) {
        int n = from + d, w = d < 0 ? -d : d;
        if (n < lo || n > hi || !(pcm >> (n % 12) & 1))
            continue;
        if (dir && d * dir < 0)
            w += 3;                              /* against the line's way: a little further */
        if (w < bd)
            bd = w, best = n;
    }
    return best;
}
static void make_melody(void)
{
    const mrh_t *m = &MRH[tirand((int)NMRH)];
    int key = tk.r->key, scale = 0, i, b, prev, dir = 1, k, last = 0, reps = 0;
    static const int8_t MIN_SC[6] = {0, 2, 3, 5, 7, 10}, MAJ_SC[5] = {0, 2, 4, 7, 9};
    if (tk.r->minor)
        for (i = 0; i < 6; i++)
            scale |= 1 << ((key + MIN_SC[i]) % 12);
    else
        for (i = 0; i < 5; i++)
            scale |= 1 << ((key + MAJ_SC[i]) % 12);
    for (b = 0; b < 4; b++)
        tk.mel_n[b] = 0;
    {
        int pc[4], n = chord_pcs(0, pc), cm = 0;
        for (i = 0; i < n; i++)
            cm |= 1 << pc[i];
        prev = near_pc(67 + tirand(4), cm, 62, 76, 0);
    }
    for (k = 0; k < m->nc; k++) {                /* the call */
        int st = m->c[k][0], len = m->c[k][1], bar = st / 16, pc[4], n = chord_pcs(bar, pc), cm = 0, note;
        for (i = 0; i < n; i++)
            cm |= 1 << pc[i];
        if (st >= 12)
            dir = -1;                            /* up, then down */
        if (k == 0) {
            note = prev;
        } else if (k == m->nc - 1) {             /* open: the third or the fifth */
            int open = (1 << pc[1]) | (1 << pc[2]);
            note = near_pc(prev, open, 62, 79, dir);
        } else if (st % 8 == 0 || len >= 4) {
            note = near_pc(prev + dir * 2, cm, 62, 79, dir);
        } else {
            note = near_pc(prev + dir, scale & ~(1 << (prev % 12)), 62, 79, dir);
        }
        if (note == last && ++reps >= 2) {
            note = near_pc(note + dir * 2, cm, 62, 79, dir);
            reps = 0;
        } else if (note != last) {
            reps = 0;
        }
        last = prev = note;
        tk.mel[bar][tk.mel_n[bar]].st = (int8_t)(st % 16);
        tk.mel[bar][tk.mel_n[bar]].len = (int8_t)len;
        tk.mel[bar][tk.mel_n[bar]++].note = (int8_t)note;
    }
    /* the answer: bar 2 the call's first bar (moved to its chord if that differs), bar 3 down to the root */
    for (k = 0; k < tk.mel_n[0]; k++) {
        int note = tk.mel[0][k].note;
        if (chord_pc(2) != chord_pc(0) || chord_q(2) != chord_q(0)) {
            int pc[4], n = chord_pcs(2, pc), cm = 0;
            for (i = 0; i < n; i++)
                cm |= 1 << pc[i];
            note = near_pc(note, (tk.mel[0][k].st % 8 == 0 || tk.mel[0][k].len >= 4) ? cm : scale, 62, 79, 0);
        }
        tk.mel[2][k] = tk.mel[0][k];
        tk.mel[2][k].note = (int8_t)note;
    }
    tk.mel_n[2] = tk.mel_n[0];
    prev = tk.mel_n[2] ? tk.mel[2][tk.mel_n[2] - 1].note : prev;
    dir = -1;
    for (k = 0; k < m->ne; k++) {
        int st = m->e[k][0], len = m->e[k][1], pc[4], n = chord_pcs(3, pc), cm = 0, note;
        for (i = 0; i < n; i++)
            cm |= 1 << pc[i];
        if (k == m->ne - 1)
            note = near_pc(prev, 1 << pc[0], 60, 76, -1);    /* home: the root */
        else if (st % 8 == 0 || len >= 4)
            note = near_pc(prev - 1, cm, 62, 79, dir);
        else
            note = near_pc(prev - 1, scale & ~(1 << (prev % 12)), 62, 79, dir);
        prev = note;
        tk.mel[3][tk.mel_n[3]].st = (int8_t)st;
        tk.mel[3][tk.mel_n[3]].len = (int8_t)len;
        tk.mel[3][tk.mel_n[3]++].note = (int8_t)note;
    }
}

static void plan(void)                           /* the blocks: an intro, a plan, the end */
{
    float bar_s = step_len * 16.0f / EN_SR, a = actv();
    int want, i, arc, n, w[5], tot = 0, r;
    want = (int)((float)LEN_MIN[par[P_LENGTH] < 0 ? 0 : par[P_LENGTH] > 4 ? 4 : par[P_LENGTH]] * 60.0f / (bar_s * 4.0f) + 0.5f);
    want += tirand(3) - 1;
    tk.nblk = want < 6 ? 6 : want > MAXBLK ? MAXBLK : want;
    for (i = 0; i < 5; i++) {                    /* a plan the style knows; a wild engineer likes the dubbier ones */
        w[i] = (STYLES[style_i].arcs >> i & 1) ? (i == 1 || i == 3 ? (int)(2.0f + 6.0f * a) : (int)(8.0f - 4.0f * a)) : 0;
        if (w[i] < 0)
            w[i] = 0;
        tot += w[i];
    }
    r = tirand(tot > 0 ? tot : 1);
    for (arc = 0; arc < 4 && r >= w[arc]; arc++)
        r -= w[arc];
    r = tirand(10);
    tk.blk[0] = a > 0.5f ? (r < 5 ? B_INTRO_A : r < 8 ? B_INTRO_C : B_INTRO_B) : (r < 3 ? B_INTRO_A : r < 6 ? B_INTRO_C : B_INTRO_B);
    n = 0;
    for (i = 1; i < tk.nblk - 1; i++)
        tk.blk[i] = tame(ARCS[arc][(n + i - 1) % ARC_N], a);
    tk.blk[tk.nblk - 1] = B_OUTRO;
}

static void new_track(void)
{
    uint32_t seed;
    const style_t *st = &STYLES[style_i];
    int i, n = 0, pick[EN_NRIDDIMS], dm, ds[3], nd = 0;
    en_track_no++;
    seed = en_track_rand(en_seed_base, en_track_no);
    en_track_seed = seed;
    trng = seed;
    for (i = 0; i < EN_NRIDDIMS; i++)            /* a riddim of the style's in another key than the last one */
        if ((st->riddims >> i & 1u) && (!tk.r || RIDDIMS[i].key != tk.r->key))
            pick[n++] = i;
    if (!n)
        for (i = 0; i < EN_NRIDDIMS; i++)        /* (or at least not the last one) */
            if ((st->riddims >> i & 1u) && (!tk.r || i != tk.ri))
                pick[n++] = i;
    tk.ri = n ? pick[tirand(n)] : 0;
    tk.r = &RIDDIMS[tk.ri];
    en_riddim = (uint8_t)tk.ri;
    en_key = tk.r->key;
    en_minor = tk.r->minor;
    dm = st->drums & tk.r->drums;
    if (!dm)
        dm = st->drums;
    for (i = 0; i < 3; i++)
        if (dm >> i & 1)
            ds[nd++] = i;
    tk.drums = nd ? ds[tirand(nd)] : 0;
    tk.gvar = tirand(2);
    tk.rimsn = st->snare == 2 ? tk.drums != 0 : st->snare;
    tk.dbl = trand() * 100.0f < (float)st->dbl;
    tk.bubble = tirand(3) == 0;                  /* the half bubble, now and then */
    tk.hpat = tirand((int)NHPATS);
    tk.mel_on = trand() * 100.0f < (float)st->mel;
    tempo_set();
    make_melody();
    plan();
    tempo_set();
    for (i = 0; i < 3; i++) {
        tk.vs[i] = 64 + 3 * i;
        tk.vo[i] = 60 + 3 * i;
        tk.vh[i] = 64 + 3 * i;
    }
    tk.bar = 0;
    tk.step = 0;
    tk.step_left = 0.0f;
    tk.skipping = 0;
    tk.skipped = 0;
    tk.prev_on = 0;
    trk_samples = 0;
    en_track_ms = 0;
    en_phase = PH_INTRO;
    hp_t = 0.0f;
    en_tracks_played++;
}

/* --------------------------------------------------- the bar, the step --- */
static int32_t swing_at(int s) { return (s & 1) ? (int32_t)(step_len * (float)STYLES[style_i].swing * 0.01f) : 0; }
static int32_t hum(float ms) { return (int32_t)((frand() * 2.0f - 1.0f) * ms * 44.1f); }
static void throw_ch(int ch, int32_t at, int32_t len)
{
    sched(E_THROW, at - 40, ch, 1.0f, len, 0);
}
static int lose(int from, int to) { return from & ~to & (BA | GT | OR); }

static void bar_start(void)
{
    int bib = tk.bar & 3, bi = tk.bar >> 2, b, m, nm, fl, nb;
    float a = actv(), r;
    if (tk.skipping) {                           /* skipped: straight to the end's drums */
        tk.skipping = 0;
        tk.skipped = 1;
        if (bi < tk.nblk - 1) {
            bi = tk.nblk - 1;
            tk.bar = bi * 4 + 2;
            bib = 2;
        }
    }
    if (bi >= tk.nblk) {                         /* the version is done (repeat: the plan again) */
        if (en_repeat_on && !tk.skipped) {
            tk.bar = 4;
            bi = 1;
            bib = 0;
        } else {
            new_track();
            bi = 0;
            bib = 0;
        }
    }
    if (en_repeat_on && !tk.skipped && tk.blk[bi] == B_OUTRO && bib == 0) {
        tk.bar = 4;
        bi = 1;
    }
    b = tk.blk[bi];
    fl = BLOCKS[b].fl;
    m = BLOCKS[b].m[bib];
    nb = bib < 3 ? b : (bi + 1 < tk.nblk ? tk.blk[bi + 1] : B_INTRO_A);
    nm = BLOCKS[nb].m[bib < 3 ? bib + 1 : 0];
    if (!tk.mel_on || par[P_LEAD] == 0)
        m &= ~ME, nm &= ~ME;
    if (par[P_LEAD] == 0)
        m &= ~HO;
    en_phase = (uint8_t)(bi == 0 ? PH_INTRO : bi == tk.nblk - 1 ? PH_OUTRO : PH_BODY);
    en_bar_in_block = (uint8_t)bib;
    /* the desk: faders to the plan, on the bar line */
    tk.prev_on = tk.on;
    tk.on = m;
    ch_gt[CH_DRUMS] = (m & DR) ? 1.0f : 0.0f;
    ch_gt[CH_BASS] = (m & BA) ? 1.0f : 0.0f;
    ch_gt[CH_GTR] = (m & GT) ? 1.0f : 0.0f;
    ch_gt[CH_ORGAN] = (m & OR) ? 1.0f : 0.0f;
    ch_gt[CH_MEL] = (m & ME) ? 1.0f : 0.0f;
    ch_gt[CH_HORN] = ((m & HO) || sir.on) ? 1.0f : 0.0f;
    if (tk.bar > 0 && lose(tk.prev_on, m))
        move(MV_STRIP);
    else if (tk.bar > 0 && lose(m, tk.prev_on))
        move(MV_BACK);
    /* the high-pass: a build's last two bars rise; the next bar line lets go */
    if ((fl & FL_SWEEP) && bib == 2 && frand() < 0.4f + 0.6f * a) {
        hp_t = 1.0f;
        hp_rate = 1.0f / (step_len * 30.0f);
        move(MV_FILTER);
    } else if (bib == 0 && hp_t > 0.0f) {
        hp_t = 0.0f;
    } else if (bib == 0 && (m & BA) == 0 && frand() < 0.1f * a) {   /* a small one over the drums */
        hp_t = 0.55f;
        hp_rate = 1.0f / (step_len * 14.0f);
        move(MV_FILTER);
    }
    if (hp_t <= 0.0f)
        hp_rate = 1.0f / (step_len * 0.25f);
    /* the spring: kicked as the band drops away, or by a block that asks */
    r = frand();
    if (((fl & FL_CRASH) && bib == 0 && r < 0.3f + 0.6f * a) || (tk.bar > 0 && lose(tk.prev_on, m) && r < 0.28f * a))
        sched(E_CRASH, 60, 0, 0.6f + 0.4f * frand(), 0, 0);
    /* the siren: over drum and bass, at the end */
    if (!sir.on && bib == 0 && par[P_LEAD] > 0 && (m & (GT | OR)) == 0 &&
        frand() < (float)STYLES[style_i].siren * 0.01f * a * ((fl & FL_SIREN) ? 1.6f : 0.6f)) {
        sir.on = 1;
        sir.mode = (uint8_t)(frand() < 0.5f);
        sir.left = (int32_t)(step_len * 32.0f);
        sir.f = sir.mode ? 520.0f : 880.0f;
        sir.lfo = 0.0f;
        ch_gt[CH_HORN] = 1.0f;
        move(MV_SIREN);
        LOG(L_SIREN, sir.mode, 1.0f);
    }
    /* a fill into a fuller block, at a phrase's end, or where the block asks */
    tk.fill = -1;
    if (m & DR) {
        r = frand();
        if (bib == 3 && (lose(nm, m) || (fl & FL_FILL)) && r < 0.75f)
            tk.fill = (int)(frand() * (float)NFILLS);
        else if (bib == 3 && r < 0.18f + 0.12f * a)
            tk.fill = (int)(frand() * (float)NFILLS);
        else if (bib == 1 && r < 0.06f)
            tk.fill = 0;
    }
    tk.fill_from = tk.fill >= 0 ? FILLS[tk.fill].from : 99;
    /* a drop: the last beat silent before the band comes back */
    tk.drop_from = 99;
    if (bib == 3 && (((fl & FL_DROP) && frand() < 0.5f + 0.5f * a) || (lose(nm, m) && frand() < 0.25f * a)))
        tk.drop_from = 12;
    if (b == B_OUTRO && bib == 3)
        tk.drop_from = 10;                       /* the end: the last rim into the echo, then nothing */
    if (tk.drop_from < 99)
        tk.fill = -1, tk.fill_from = 99;
    /* a horn stab: the band back, or now and then */
    tk.horn_bar = (m & HO) && ((bib == 0 && (lose(m, tk.prev_on) || tk.bar < 4) && frand() < (float)STYLES[style_i].horn * 0.012f) ||
                               ((bib == 0 || bib == 2) && frand() < (float)STYLES[style_i].horn * 0.0035f));
    /* the chords this bar */
    en_chord_root = (uint8_t)chord_pc(tk.bar);
    en_chord_q = (uint8_t)chord_q(tk.bar);
    {
        int pc[4], n = chord_pcs(tk.bar, pc), i;
        uint16_t pm = 0;
        for (i = 0; i < n; i++)
            pm |= (uint16_t)(1u << pc[i]);
        en_chord_pcs = pm;
    }
    voice3(tk.bar, 66, tk.vs);
    voice3(tk.bar, 63, tk.vo);
    voice3(tk.bar, 67, tk.vh);
    LOG(L_BAR, b, (float)m);
}

static void seq_step(void)
{
    int s = tk.step, bib, dropped, i;
    int32_t sw;
    float dv = 0.55f + 0.45f * (float)par[P_DRUMS] * 0.01f, a = actv();
    const groove_t *g;
    if (s == 0)
        bar_start();
    bib = tk.bar & 3;
    sw = swing_at(s);
    g = &GROOVES[tk.drums][tk.gvar];
    en_step = (uint8_t)s;
    dropped = s >= tk.drop_from;
    if (s == tk.drop_from) {                     /* everything out for the last beat: the echo carries it */
        for (i = 0; i < EN_NCH; i++)
            ch_gt[i] = 0.0f;
        move(MV_DROP);
    }
    /* drums */
    if (!dropped) {
        char kc = g->k[s], sc = g->s[s], hc = g->h[s];
        if (s >= tk.fill_from) {                 /* the fill takes the bar's end */
            if (s != 8)
                kc = '.';
            sc = '.';
            hc = s == 15 && FILLS[tk.fill].e[FILLS[tk.fill].n - 1][1] == 0 ? 'o' : '.';
        }
        if (s == tk.fill_from) {
            const fill_t *f = &FILLS[tk.fill];
            for (i = 0; i < f->n; i++) {
                int32_t at = (int32_t)((float)(f->e[i][0] - 4 * s) * 0.25f * step_len) + hum(2.0f);
                float v = dv * (float)f->e[i][2] * 0.01f;
                if (f->e[i][1] == 0)
                    sched(tk.rimsn ? E_SNARE : E_RIM, at, 0, v, 0, 0);
                else
                    sched(E_TOM, at, f->e[i][1], v, 0, 0);
                LOG(f->e[i][1] ? L_TOM : L_SNARE, f->e[i][1], v);
            }
        }
        if (kc != '.') {
            float v = dv * (s == 8 || tk.drums == 2 ? 0.95f : 0.8f);
            sched(E_KICK, sw + hum(1.5f) + 40, 0, v, 0, 0);
            LOG(L_KICK, 0, v);
        }
        if (sc != '.') {
            float v = dv * (sc == 'X' ? 0.85f : 0.22f);
            int32_t at = sw + hum(2.0f) + 180;   /* the backbeat, a hair late */
            sched(tk.rimsn ? E_SNARE : E_RIM, at, 0, v, 0, 0);
            LOG(tk.rimsn ? L_SNARE : L_RIM, 0, v);
            if (sc == 'X') {                     /* the engineer throws the snare */
                float p = 0.2f * a * ((tk.on & (GT | OR)) ? 1.0f : 2.0f) * ((BLOCKS[tk.blk[tk.bar >> 2]].fl & FL_THROW) ? 1.6f : 1.0f);
                int big = (BLOCKS[tk.blk[tk.bar >> 2]].fl & FL_SWELL) && bib == 3;
                if (big || frand() < p) {
                    throw_ch(CH_DRUMS, at, (int32_t)(step_len * 1.5f));
                    move(MV_THROW);
                    if (big || frand() < 0.25f * a)
                        sched(E_SWELL, at, 0, big ? 1.0f : 0.6f, (int32_t)(step_len * (big ? 24.0f : 12.0f)), 0);
                }
            }
        } else if (s == 15 && tk.drums == 0 && frand() < 0.08f) {
            sched(tk.rimsn ? E_SNARE : E_RIM, sw + 180, 0, dv * 0.2f, 0, 0);
        }
        if (hc != '.') {
            float v = hc == 's' ? 0.28f : hc == 'o' ? 0.42f : (s & 2) ? 0.5f : 0.4f;
            sched(hc == 'o' ? E_OHAT : E_HAT, sw + hum(3.0f), 0, dv * v * (0.85f + 0.25f * frand()), 0, 0);
            LOG(hc == 'o' ? L_OHAT : L_HAT, 0, v);
        }
    }
    /* bass: the figure, laid back a little */
    {
        int fb = tk.bar % tk.r->fbars, n = tk.r->nf[fb];
        for (i = 0; i < n; i++)
            if (tk.r->f[fb][i].st == s) {
                int note = tk.r->bt + tk.r->root[tk.bar % tk.r->ncyc] + tk.r->f[fb][i].semi;
                float v = 0.82f + 0.18f * frand() + (s == 0 ? 0.05f : 0.0f);
                while (note > 57)
                    note -= 12;
                while (note < 26)
                    note += 12;
                sched(E_BASS, sw + hum(2.0f) + 260, note, v, (int32_t)((float)tk.r->f[fb][i].len * step_len * 0.86f), 0);
                LOG(L_BASS, note, v);
            }
    }
    /* the skank: two and four (doubled: and the sixteenth after) */
    if (s == 4 || s == 12 || (tk.dbl && (s == 5 || s == 13))) {
        int second = s & 1;
        float v = (second ? 0.55f : 0.85f) * (0.9f + 0.15f * frand());
        int32_t at = sw + hum(2.5f), gate = (int32_t)(step_len * (second ? 0.45f : 0.6f));
        for (i = 0; i < 3; i++) {
            sched(E_GTR, at + i * 90, tk.vs[i], v * (i == 2 ? 1.0f : 0.85f), gate, 0);
            LOG(L_GTR, tk.vs[i], v);
        }
        if (s == (tk.dbl ? 13 : 12) && (tk.on & GT)) {   /* the last chop of the bar: into the echo */
            int nm = bib < 3 ? BLOCKS[tk.blk[tk.bar >> 2]].m[bib + 1] : (tk.bar >> 2) + 1 < tk.nblk ? BLOCKS[tk.blk[(tk.bar >> 2) + 1]].m[0] : 0;
            float p = (nm & GT) ? 0.12f * a : 0.45f + 0.4f * a;
            if (bib == 3)
                p += 0.1f * a;
            if (frand() < p) {
                throw_ch(CH_GTR, at, (int32_t)(step_len * 1.2f));
                move(MV_THROW);
            }
        }
    }
    /* the organ's bubble: the low half on the off-beat eighths, the high half on the sixteenth after */
    if ((s & 3) == 2) {
        float v = 0.75f * (0.92f + 0.12f * frand());
        int lo = 43 + ((chord_pc(tk.bar) - 43) % 12 + 12) % 12, gate = (int32_t)(step_len * 0.8f);
        sched(E_ORG, sw + hum(2.0f), lo, v, gate, 0);
        sched(E_ORG, sw + hum(2.0f), lo + 7, v * 0.8f, gate, 0);
        LOG(L_ORG, lo, v);
    }
    if ((s & 3) == 3 && (!tk.bubble || s == 7 || s == 15)) {
        float v = 0.5f * (0.9f + 0.15f * frand());
        int32_t gate = (int32_t)(step_len * 0.7f);
        for (i = 0; i < 3; i++) {
            sched(E_ORG, sw + hum(2.0f), tk.vo[i], v, gate, 0);
            LOG(L_ORG, tk.vo[i], v);
        }
        if (s == 15 && (tk.on & OR) && frand() < 0.04f * a) {
            throw_ch(CH_ORGAN, sw, (int32_t)step_len);
            move(MV_THROW);
        }
    }
    /* the melodica: its phrase, bar by bar of the block */
    if (tk.on & ME) {
        for (i = 0; i < tk.mel_n[bib]; i++)
            if (tk.mel[bib][i].st == s) {
                const mn_t *mn = &tk.mel[bib][i];
                float v = 0.75f + 0.2f * frand();
                int32_t at = sw + hum(6.0f) + 300, gate = (int32_t)((float)mn->len * step_len * 0.92f);
                int lastn = i == tk.mel_n[bib] - 1 && (bib == 1 || bib == 3);
                sched(E_MEL, at, mn->note, v, gate, 0);
                LOG(L_MEL, mn->note, v);
                if (lastn && frand() < (bib == 3 ? 0.4f + 0.5f * a : 0.25f * a)) {
                    throw_ch(CH_MEL, at, gate);
                    move(MV_THROW);
                    if (bib == 3 && frand() < 0.4f * a)
                        sched(E_SWELL, at, 0, 0.6f, (int32_t)(step_len * 12.0f), 0);
                }
            }
    }
    /* the horns: a stab */
    if (tk.horn_bar && !dropped) {
        const hpat_t *h = &HPATS[tk.hpat];
        for (i = 0; i < h->n; i++)
            if (h->h[i][0] == s) {
                int k;
                float v = 0.8f + 0.15f * frand();
                int32_t at = sw + hum(3.0f), gate = (int32_t)((float)h->h[i][1] * step_len * 0.85f);
                for (k = 0; k < 3; k++)
                    sched(E_HORN, at + k * 60, tk.vh[k], v, gate, k);
                LOG(L_HORN, tk.vh[2], v);
                if (i == h->n - 1 && frand() < 0.5f + 0.4f * a) {
                    throw_ch(CH_HORN, at, gate);
                    move(MV_THROW);
                }
            }
    }
    if (s % 4 == 0)
        en_beats++;
    if (++tk.step >= 16) {
        tk.step = 0;
        tk.bar++;
    }
}

/* ------------------------------------------------------------- control --- */
static void quiet(void)
{
    int i;
    for (i = 0; i < NGT; i++)
        gt[i].on = 0;
    for (i = 0; i < NOR; i++)
        org[i].on = 0;
    for (i = 0; i < NHN; i++)
        hn[i].on = 0;
    for (i = 0; i < NEV; i++)
        evq[i].used = 0;
    mel.on = 0;
    mel.amp = 0.0f;
    bs.on = 0;
    bs.amp = 0.0f;
    sir.on = 0;
    sir.amp = 0.0f;
    kick.amp = kick.click = snare.tamp = snare.namp = rim.amp = rim.namp = hat.amp = tom.amp = 0.0f;
    gt_chk = 0.0f;
}

static void play_on(int on)
{
    int i;
    if (on && !en_playing) {
        en_playing = 1;
        fade = 0.0f;
        fade_t = 1.0f;
        fade_rate = 1.0f / (0.3f * EN_SR);
    } else if (!on && en_playing) {
        en_playing = 0;
        for (i = 0; i < NEV; i++)
            evq[i].used = 0;
        for (i = 0; i < NOR; i++)
            if (org[i].on && !org[i].player)
                org[i].gate = 0;
        for (i = 0; i < NHN; i++)
            hn[i].gate = 0;
        mel.gate = 0;
        bs.gate = 0;
        sir.left = 0;
    }
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(int16_t)(c & 0xFFFFu);
        uint32_t ub = c & 0xFFFFu;
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_SET:
            if (a < P_NPARAMS) {
                const en_param_t *p = &PARAMS[a];
                par[a] = (int16_t)(b < p->lo ? p->lo : b > p->hi ? p->hi : b);
                if (a == P_TEMPO || a == P_TIME)
                    tempo_set();
            }
            break;
        case C_STYLE: style_i = a % EN_NSTYLES; break;
        case C_PLAY: play_on(a); break;
        case C_SKIP: tk.skipping = 1; break;
        case C_REPEAT: en_repeat_on = (uint8_t)(a != 0); break;
        case C_TRACK_SEED_HI: en_seed_base = (en_seed_base & 0xFFFFu) | ub << 16; break;
        case C_TRACK_SEED_LO: en_seed_base = (en_seed_base & 0xFFFF0000u) | ub; break;
        case C_TRACK_NO:
            en_track_no = (uint32_t)a << 16 | ub;   /* the next is no + 1 */
            quiet();
            new_track();
            break;
        case C_NOTE:
            if (b > 0)
                org_on(a, (float)b * (1.0f / 127.0f), -1, 1);
            else
                org_off_player(a);
            break;
        case C_PANIC: quiet(); break;
        case C_THROW: {
            int c;
            for (c = 0; c < EN_NCH; c++)
                if (ch_gt[c] > 0.0f)
                    thr_left[c] = (int32_t)(step_len * 1.5f);
            fb_boost_t = 0.26f;
            swell_left = (int32_t)(step_len * 8.0f);
            move(MV_THROW);
            break;
        }
        case C_CRASH:
            crash_amp = 0.9f;
            crash_fb = 0.12f;
            move(MV_SPRING);
            break;
        }
    }
}

void en_init(void)
{
    int i;
    float *p = sp_buf;
    for (i = 0; i <= SN; i++)
        SINT[i] = fm_sinf(FM_TWO_PI * (float)i / (float)SN);
    for (i = 0; i < P_NPARAMS; i++)
        par[i] = PARAMS[i].def;
    for (i = 0; i < 2; i++) {
        sp_d[i] = p;
        p += SP_N[i];
    }
    for (i = 0; i < SP0 + SP1; i++)
        sp_buf[i] = 0.0f;
    for (i = 0; i < EC_N; i++)
        ec_buf[i] = 0;
    for (i = 0; i < EN_NCH; i++)
        ch_g[i] = ch_gt[i] = ch_s[i] = 0.0f, thr_left[i] = 0;
    fb_boost = fb_boost_t = 0.0f;
    swell_left = 0;
    hp_amt = hp_t = 0.0f;
    quiet();
    tk.r = 0;
    en_track_no = 0;
    new_track();
}

/* -------------------------------------------------------------- render --- */
enum { X_DK, X_DS, X_BASS, X_GTR, X_ORG, X_MEL, X_HORN, X_SIREN, X_KEYS, NX };
static float xb[NX][BLK];
static float tone_a = 0.3f, t1l, t2l, t1r, t2r;  /* the bus's low-pass */
static float hpf_l[2], hpf_r[2];                 /* the sweep's high-pass (two-pole, state-variable) */
static float dc_l, dc_r;
static float lev = 0.0f;
static float vu_acc_l, vu_acc_r;
static uint32_t vu_n;

static void render_drums(uint32_t n)
{
    uint32_t j;
    float fs = 2.0f * fm_sinf(FM_PI * 2400.0f / EN_SR), fr = 2.0f * fm_sinf(FM_PI * 2000.0f / EN_SR);
    float hha = onepole(7000.0f), hlp = onepole(11000.0f), slp = onepole(7000.0f);
    float *dk = xb[X_DK], *ds = xb[X_DS];
    if (kick.amp < 1e-3f && snare.tamp < 1e-3f && snare.namp < 1e-3f && rim.amp < 1e-3f && rim.namp < 1e-3f &&
        hat.amp < 1e-3f && tom.amp < 1e-3f)
        return;
    for (j = 0; j < n; j++) {
        float k = 0.0f, s = 0.0f, h = 0.0f, x;
        if (kick.amp > 1e-3f) {
            kick.f = kick.fend + (kick.f - kick.fend) * kick.pdec;
            kick.ph = wrap1(kick.ph + kick.f / EN_SR);
            k = sn(kick.ph) * kick.amp + noise() * kick.click;
            kick.amp *= kick.adec;
            kick.click *= 0.9f;
        }
        if (tom.amp > 1e-3f) {
            tom.f = tom.fend + (tom.f - tom.fend) * tom.pdec;
            tom.ph = wrap1(tom.ph + tom.f / EN_SR);
            k += (sn(tom.ph) + 0.08f * noise()) * tom.amp * 0.8f;
            tom.amp *= tom.adec;
        }
        if (snare.tamp > 1e-3f || snare.namp > 1e-3f) {
            float hp;
            x = noise() * snare.namp;
            snare.lo += fs * snare.bd;           /* Chamberlin's state-variable filter: its band, a little high */
            hp = x - snare.lo - 0.8f * snare.bd;
            snare.bd += fs * hp;
            snare.lp += (snare.bd + 0.4f * hp - snare.lp) * slp;
            s = snare.lp * 0.9f + sn(snare.ph) * snare.tamp;
            snare.ph = wrap1(snare.ph + snare.inc);
            snare.tamp *= snare.tdec;
            snare.namp *= snare.ndec;
        }
        if (rim.amp > 1e-3f || rim.namp > 1e-3f) {   /* the cross-stick: a woody knock, a click */
            x = noise() * rim.namp;
            rim.lo += fr * rim.bd;
            rim.bd += fr * (x - rim.lo - 0.3f * rim.bd);
            s += rim.bd * 0.6f + (sn(rim.ph) + 0.35f * sn(rim.ph * 2.3f)) * rim.amp;
            rim.ph = wrap1(rim.ph + rim.inc);
            rim.amp *= rim.dec;
            rim.namp *= rim.ndec;
        }
        if (hat.amp > 1e-3f) {
            x = noise();
            hat.h1 += (x - hat.h1) * hha;
            x -= hat.h1;
            hat.h2 += (x - hat.h2) * hha;
            x -= hat.h2;
            hat.lp += (x - hat.lp) * hlp;
            hat.lp2 += (hat.lp - hat.lp2) * hlp;
            h = hat.lp2 * hat.amp * 0.7f;
            hat.amp *= hat.dec;
        }
        dk[j] = k * 0.85f + h;
        ds[j] = s * 0.7f;
    }
}

static void render_bass(uint32_t n)
{
    uint32_t j;
    float w = (float)par[P_BASS] * 0.01f, a = onepole(520.0f - 220.0f * w), glide = 1.0f - tau(0.012f);
    float h2 = 0.32f - 0.14f * w, idec = tau(0.05f), dec = tau(0.6f), rel = tau(0.035f), att = 1.0f / (0.004f * EN_SR);
    float *o = xb[X_BASS];
    if (!bs.on)
        return;
    for (j = 0; j < n; j++) {
        float s;
        bs.inc += (bs.tinc - bs.inc) * glide;
        s = sn(bs.ph + sn(bs.ph) * bs.idx) + h2 * sn(bs.ph * 2.0f);
        bs.ph = wrap1(bs.ph + bs.inc);
        if (bs.gate > 0) {
            bs.gate--;
            if (bs.atk) {
                bs.amp += att * bs.peak;
                if (bs.amp >= bs.peak)
                    bs.amp = bs.peak, bs.atk = 0;
            } else {
                bs.amp = bs.sus + (bs.amp - bs.sus) * dec;
            }
        } else {
            bs.amp *= rel;
        }
        bs.idx *= idec;
        s *= bs.amp;
        s = s * (1.0f - 0.1f * s * s);           /* a little warmth */
        bs.lp += (s - bs.lp) * a;
        bs.lp2 += (bs.lp - bs.lp2) * a;
        o[j] = bs.lp2;
    }
    if (bs.gate <= 0 && bs.amp < 0.0005f)
        bs.on = 0;
}

static void render_gtr(uint32_t n)
{
    int i;
    uint32_t j;
    float *o = xb[X_GTR], idec = tau(0.02f), rel = tau(0.012f), ha = onepole(380.0f), la = onepole(3600.0f), na = onepole(2500.0f);
    int any = gt_chk > 1e-3f;
    for (i = 0; i < NGT; i++) {
        float ph, inc, idx, amp, d;
        if (!gt[i].on)
            continue;
        any = 1;
        ph = gt[i].ph, inc = gt[i].inc, idx = gt[i].idx, amp = gt[i].amp;
        for (j = 0; j < n; j++) {
            d = gt[i].gate > 0 ? gt[i].dec : rel;
            if (gt[i].gate > 0)
                gt[i].gate--;
            o[j] += sn(ph + idx * sn(ph * 2.0f)) * amp;
            ph = wrap1(ph + inc);
            idx *= idec;
            amp *= d;
        }
        gt[i].ph = ph, gt[i].idx = idx, gt[i].amp = amp;
        if (amp < 0.0008f)
            gt[i].on = 0;
    }
    if (!any)
        return;
    for (j = 0; j < n; j++) {                    /* the pick's scrape; the amp: thin and bright */
        float x = o[j] + (noise() - gt_nhp) * gt_chk, y;
        gt_nhp += (noise() - gt_nhp) * na;
        gt_chk *= 0.9975f;
        gt_hp += (x - gt_hp) * ha;
        y = x - gt_hp;
        gt_lp += (y - gt_lp) * la;
        o[j] = gt_lp;
    }
}

static void render_org(uint32_t n)
{
    int i;
    uint32_t j;
    float *o, rel = tau(0.02f);
    for (i = 0; i < NOR; i++) {
        float ph, inc, amp, tgt, att;
        if (!org[i].on)
            continue;
        o = xb[org[i].player ? X_KEYS : X_ORG];   /* the player's own: not on the desk's fader */
        ph = org[i].ph, inc = org[i].inc, amp = org[i].amp, tgt = org[i].tgt, att = org[i].att;
        for (j = 0; j < n; j++) {
            if (org[i].gate != 0) {
                if (amp < tgt)
                    amp = fm_minf(amp + att, tgt);
                if (org[i].gate > 0)
                    org[i].gate--;
            } else {
                amp *= rel;
            }
            o[j] += (sn(ph) + 0.55f * sn(ph * 2.0f) + 0.3f * sn(ph * 4.0f)) * amp;   /* drawbars 8', 4', 2' */
            ph = wrap1(ph + inc);
        }
        org[i].ph = ph, org[i].amp = amp;
        if (org[i].gate == 0 && amp < 0.0005f)
            org[i].on = 0;
    }
}

static void render_mel(uint32_t n)
{
    uint32_t j;
    float *o = xb[X_MEL], att = 1.0f / (0.03f * EN_SR), rel = tau(0.06f), la = onepole(2800.0f), gl = 1.0f - tau(0.008f);
    if (!mel.on)
        return;
    for (j = 0; j < n; j++) {
        float s, vib = sn(mel.vph) * fm_minf(mel.vib, 1.0f) * 0.0045f;
        mel.vph = wrap1(mel.vph + 5.4f / EN_SR);
        mel.vib += 1.0f / (0.5f * EN_SR);
        mel.inc += (mel.tinc - mel.inc) * gl;
        if (mel.gate > 0) {
            mel.gate--;
            if (mel.amp < mel.peak)
                mel.amp = fm_minf(mel.amp + att * mel.peak, mel.peak);
        } else {
            mel.amp *= rel;
        }
        s = sn(mel.ph + 0.95f * sn(mel.ph)) + 0.05f * noise();   /* a reed, a breath */
        mel.ph = wrap1(mel.ph + mel.inc * (1.0f + vib));
        mel.lp += (s * mel.amp - mel.lp) * la;
        o[j] = mel.lp;
    }
    if (mel.gate <= 0 && mel.amp < 0.0005f)
        mel.on = 0, mel.vib = 0.0f;
}

static void render_horns(uint32_t n)
{
    int i;
    uint32_t j;
    float *o = xb[X_HORN], att = 1.0f / (0.014f * EN_SR), rel = tau(0.06f), dt = 1.0f / EN_SR;
    for (i = 0; i < NHN; i++) {
        float ph, inc, amp;
        if (!hn[i].on)
            continue;
        ph = hn[i].ph, inc = hn[i].inc, amp = hn[i].amp;
        for (j = 0; j < n; j++) {
            float t = hn[i].t, idx;
            hn[i].t = t + dt;
            hn[i].scoop += (1.0f - hn[i].scoop) * 0.0012f;          /* a scoop up into the note */
            idx = t < 0.025f ? 0.4f + t * 70.0f : 1.15f + 0.6f * fm_expf(-(t - 0.025f) * 12.0f);   /* the blare */
            if (hn[i].gate > 0) {
                hn[i].gate--;
                if (amp < hn[i].peak)
                    amp = fm_minf(amp + att * hn[i].peak, hn[i].peak);
            } else {
                amp *= rel;
            }
            o[j] += sn(ph + idx * sn(ph)) * amp;
            ph = wrap1(ph + inc * hn[i].scoop);
        }
        hn[i].ph = ph, hn[i].amp = amp;
        if (hn[i].gate <= 0 && amp < 0.0005f)
            hn[i].on = 0;
    }
}

static void render_siren(uint32_t n)
{
    uint32_t j;
    float *o = xb[X_SIREN], rate = sir.mode ? 1.1f : 3.2f;
    if (!sir.on)
        return;
    for (j = 0; j < n; j++) {
        float tgt, s;
        sir.lfo = wrap1(sir.lfo + rate / EN_SR);
        if (sir.mode)                            /* a rising sweep, over and over */
            tgt = 420.0f * fm_exp2f(1.7f * sir.lfo);
        else                                     /* the two-tone wail */
            tgt = sir.lfo < 0.5f ? 760.0f : 1040.0f;
        sir.f += (tgt - sir.f) * (sir.mode ? 0.02f : 0.0016f);
        if (sir.left > 0) {
            sir.left--;
            sir.amp += (0.32f - sir.amp) * 0.0004f;
        } else {
            sir.amp *= 0.99985f;
        }
        s = sn(sir.ph) + 0.25f * sn(sir.ph * 3.0f);
        sir.ph = wrap1(sir.ph + sir.f / EN_SR);
        o[j] = s * sir.amp;
    }
    if (sir.left <= 0 && sir.amp < 0.0005f)
        sir.on = 0, ch_gt[CH_HORN] = (tk.on & HO) ? 1.0f : 0.0f;
}

/* the desk: each channel's fader and send; the echo, the spring, the sweep, the tone, the leveller */
static void render_desk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    int c;
    float lvl[EN_NCH], gs = 1.0f - tau(0.004f), ss = 1.0f - tau(0.002f);
    float base_fb = 0.2f + 0.55f * (float)par[P_ECHO] * 0.01f, fb, sp = (float)par[P_SPRING] * 0.01f;
    float ehp = onepole(140.0f), elp = onepole(2600.0f), sp_fb, sp_damp = onepole(3200.0f), sphp = onepole(260.0f);
    float hfc, hf, tone_fc, dca = onepole(22.0f), lv_rel = tau(0.5f), lv_att = 1.0f - tau(0.0004f);
    float sendk[EN_NCH] = {0.0f, 0.0f, 0.04f, 0.02f, 0.22f, 0.12f};      /* the sends' resting levels */
    float sprk[NX] = {0.0f, 0.55f, 0.0f, 0.4f, 0.12f, 0.14f, 0.18f, 0.25f, 0.1f};   /* each into the spring */
    float pan[NX] = {0.0f, 0.08f, 0.0f, 0.38f, -0.3f, -0.15f, 0.22f, 0.0f, 0.0f};
    float gl[NX], gr[NX];
    float ein_m = 0.0f, eout_m = 0.0f, sp_m = 0.0f;
    lvl[CH_DRUMS] = 0.9f + 0.6f * ((float)par[P_DRUMS] * 0.01f - 0.7f);
    lvl[CH_BASS] = 0.55f + 0.55f * (float)par[P_BASS] * 0.01f;
    lvl[CH_GTR] = 1.4f * (float)par[P_SKANK] * 0.01f;
    lvl[CH_ORGAN] = 1.4f * (float)par[P_ORGAN] * 0.01f;
    lvl[CH_MEL] = 1.5f * (float)par[P_LEAD] * 0.01f;
    lvl[CH_HORN] = 1.5f * (float)par[P_LEAD] * 0.01f;
    if (par[P_DRUMS] == 0)
        lvl[CH_DRUMS] = 0.0f;
    for (c = 0; c < NX; c++) {
        gl[c] = 1.0f - fm_maxf(pan[c], 0.0f);
        gr[c] = 1.0f + fm_minf(pan[c], 0.0f);
    }
    /* the echo's feedback: the knob, a swell on top; the tape's time glides (it warbles as it moves) */
    if (swell_left > 0)
        swell_left -= (int32_t)n;
    else
        fb_boost_t = 0.0f;
    fb_boost += (fb_boost_t - fb_boost) * fm_minf((float)n / (EN_SR * (fb_boost_t > fb_boost ? 0.4f : 1.6f)), 1.0f);
    fb = fm_minf(base_fb + fb_boost, 0.97f);
    en_echo_fb = fb;
    crash_fb *= fm_expf(-(float)n / (EN_SR * 0.5f));
    sp_fb = 0.72f + 0.16f * sp + crash_fb;
    if (sp_fb > 0.93f)
        sp_fb = 0.93f;
    /* the sweep: up slowly, back at once */
    hp_amt += fm_clampf(hp_t - hp_amt, -hp_rate * (float)n, hp_rate * (float)n);
    en_hp = hp_amt;
    hfc = 22.0f * fm_exp2f(hp_amt * 5.8f);       /* 22 Hz .. 1.2 kHz */
    hf = 2.0f * fm_sinf(FM_PI * hfc / EN_SR);
    tone_fc = 1700.0f * fm_exp2f((float)par[P_TONE] * 0.03f);   /* 1.7 .. 14 kHz */
    tone_a += (onepole(fm_minf(tone_fc, 16000.0f)) - tone_a) * 0.2f;
    for (c = 0; c < EN_NCH; c++)
        if (thr_left[c] > 0)
            thr_left[c] -= (int32_t)n;
    for (j = 0; j < n; j++) {
        float dl, dr, ein, spin, e, rd, fr, w, y, l, r, x;
        int ri, i;
        float gD, gB, gG, gO, gM, gH;
        for (c = 0; c < EN_NCH; c++) {
            ch_g[c] += (ch_gt[c] * lvl[c] - ch_g[c]) * gs;
            ch_s[c] += ((thr_left[c] > 0 ? 1.0f : sendk[c]) - ch_s[c]) * ss;
        }
        gD = ch_g[CH_DRUMS], gB = ch_g[CH_BASS], gG = ch_g[CH_GTR], gO = ch_g[CH_ORGAN], gM = ch_g[CH_MEL], gH = ch_g[CH_HORN];
        {
            float dk = xb[X_DK][j] * gD, ds = xb[X_DS][j] * gD, b = xb[X_BASS][j] * gB, g = xb[X_GTR][j] * gG * 0.36f;
            float o = xb[X_ORG][j] * gO * 0.042f, m = xb[X_MEL][j] * gM * 0.24f, h = xb[X_HORN][j] * gH * 0.12f;
            float si = xb[X_SIREN][j], ky = xb[X_KEYS][j] * 0.09f;
            dl = ky + dk + ds * gl[X_DS] + b + g * gl[X_GTR] + o * gl[X_ORG] + m * gl[X_MEL] + h * gl[X_HORN] + si * 0.4f;
            dr = ky + dk + ds * gr[X_DS] + b + g * gr[X_GTR] + o * gr[X_ORG] + m * gr[X_MEL] + h * gr[X_HORN] + si * 0.4f;
            ein = ds * ch_s[CH_DRUMS] + g * ch_s[CH_GTR] + o * ch_s[CH_ORGAN] + m * ch_s[CH_MEL] + h * ch_s[CH_HORN] + si * 0.9f;
            spin = (ds * sprk[X_DS] + g * sprk[X_GTR] + o * sprk[X_ORG] + m * sprk[X_MEL] + h * sprk[X_HORN] + si * sprk[X_SIREN]) * sp;
            spin += (dk + ds) * crash_amp * 1.6f + noise() * crash_amp * crash_amp * 0.5f;
            crash_amp *= 0.99965f;
        }
        /* the sweep's high-pass on the band */
        for (i = 0; i < 1; i++) {
            float hp;
            hpf_l[0] += hf * hpf_l[1];
            hp = dl - hpf_l[0] - 1.2f * hpf_l[1];
            hpf_l[1] += hf * hp;
            dl = hp;
            hpf_r[0] += hf * hpf_r[1];
            hp = dr - hpf_r[0] - 1.2f * hpf_r[1];
            hpf_r[1] += hf * hp;
            dr = hp;
        }
        /* the tape echo: its head wavers; each repeat thinner and darker, saturating as it swells */
        ec_d += (ec_t - ec_d) * 0.00005f;
        ec_wow = wrap1(ec_wow + 0.45f / EN_SR);
        rd = (float)ec_w - ec_d - 9.0f * sn(ec_wow);
        if (rd < 0.0f)
            rd += (float)EC_N;
        ri = (int)rd;
        fr = rd - (float)ri;
        e = ((float)ec_buf[ri & (EC_N - 1)] + ((float)ec_buf[(ri + 1) & (EC_N - 1)] - (float)ec_buf[ri & (EC_N - 1)]) * fr) * (1.0f / 16384.0f);
        ec_hp += (e - ec_hp) * ehp;
        y = e - ec_hp;
        ec_lp += (y - ec_lp) * elp;
        ec_lp2 += (ec_lp - ec_lp2) * elp;
        y = ec_lp2;
        w = ein + y * fb;
        w = w * (27.0f + w * w) / (27.0f + 9.0f * w * w);
        w = w > 1.9f ? 1.9f : w < -1.9f ? -1.9f : w;
        ec_buf[ec_w] = (int16_t)(w * 16384.0f);
        ec_w = (ec_w + 1) & (EC_N - 1);
        /* the springs */
        sp_hp += (spin - sp_hp) * sphp;
        x = spin - sp_hp;
        for (i = 0; i < 2; i++) {
            float v = sp_d[i][sp_p[i]];
            int k;
            for (k = 0; k < NAP; k++) {          /* one-sample all-passes: the chirp */
                float yv = -0.62f * v + sp_ax[i][k] + 0.62f * sp_ay[i][k];
                sp_ax[i][k] = v;
                sp_ay[i][k] = yv;
                v = yv;
            }
            sp_lp[i] += (v - sp_lp[i]) * sp_damp;
            sp_d[i][sp_p[i]] = (i ? -x : x) * 0.6f + sp_lp[i] * sp_fb;
            if (++sp_p[i] >= SP_N[i])
                sp_p[i] = 0;
        }
        l = dl + y * (0.85f - 0.35f * ec_pan) * 1.1f + sp_lp[0] * 0.8f;
        r = dr + y * (0.85f + 0.35f * ec_pan) * 1.1f + sp_lp[1] * 0.8f;
        eout_m = fm_maxf(eout_m, fm_fabsf(y));
        ein_m = fm_maxf(ein_m, fm_fabsf(ein));
        sp_m = fm_maxf(sp_m, fm_fabsf(sp_lp[0]) + fm_fabsf(sp_lp[1]));
        /* tone, the fade, DC */
        t1l += (l - t1l) * tone_a;
        t2l += (t1l - t2l) * tone_a;
        t1r += (r - t1r) * tone_a;
        t2r += (t1r - t2r) * tone_a;
        l = t2l * fade;
        r = t2r * fade;
        fade = fade < fade_t ? fm_minf(fade + fade_rate, fade_t) : fm_maxf(fade - fade_rate, fade_t);
        dc_l += (l - dc_l) * dca;
        dc_r += (r - dc_r) * dca;
        l -= dc_l;
        r -= dc_r;
        /* the leveller: a quick attack, a slow release, above -6 dB; then MASTER, a soft clip */
        vu_acc_l += l * l;
        vu_acc_r += r * r;
        x = fm_maxf(fm_fabsf(l), fm_fabsf(r)) * gain;
        lev = x > lev ? lev + (x - lev) * lv_att : lev * lv_rel;
        {
            float vol = lev > 0.5f ? 0.5f / lev : 1.0f;
            l *= gain * vol;
            r *= gain * vol;
        }
        out[2 * j] = (int32_t)(fm_tanhf(l) * 7300000.0f);     /* -1.2 dBFS at most */
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 7300000.0f);
    }
    ec_pan = sn(ec_wow * 0.25f);
    en_echo_out += (fm_minf(eout_m * 3.0f, 1.0f) - en_echo_out) * (eout_m * 3.0f > en_echo_out ? 0.6f : 0.08f);
    en_spring += (fm_minf(sp_m * 2.5f, 1.0f) - en_spring) * (sp_m * 2.5f > en_spring ? 0.7f : 0.1f);
    en_siren = sir.on ? sir.amp * 3.0f : 0.0f;
    (void)ein_m;
    /* the screen's faders, sends and meters */
    {
        static const int MX[EN_NCH][2] = {{X_DK, X_DS}, {X_BASS, X_BASS}, {X_GTR, X_GTR}, {X_ORG, X_ORG}, {X_MEL, X_MEL}, {X_HORN, X_SIREN}};
        float mk[EN_NCH] = {4.0f, 1.4f, 2.4f, 0.25f, 1.2f, 0.6f};
        for (c = 0; c < EN_NCH; c++) {
            float m = 0.0f;
            uint32_t k;
            const float *p0 = xb[MX[c][0]], *p1 = xb[MX[c][1]];
            for (k = 0; k < n; k += 4)
                m = fm_maxf(m, fm_maxf(fm_fabsf(p0[k]), fm_fabsf(p1[k])));
            m = fm_minf(m * mk[c] * ch_g[c], 1.0f);
            en_meter[c] = m > en_meter[c] ? m : en_meter[c] * 0.93f;
            en_fader[c] = ch_gt[c] * fm_minf(lvl[c] * (c == CH_DRUMS ? 0.8f : c == CH_BASS ? 0.9f : 0.71f), 1.0f);
            en_send[c] = thr_left[c] > 0 ? 1.0f : en_send[c] * 0.9f;
        }
    }
}

static void control(uint32_t n)
{
    float k = (float)n / EN_SR;
    en_kick_glow *= fm_expf(-k * 9.0f);
    if (++vu_n >= 8u) {
        float l = fm_sqrtf(vu_acc_l / (float)(n * 8u)), r = fm_sqrtf(vu_acc_r / (float)(n * 8u));
        en_vu_l = en_vu_l + (fm_minf(l * 4.0f, 1.2f) - en_vu_l) * 0.45f;
        en_vu_r = en_vu_r + (fm_minf(r * 4.0f, 1.2f) - en_vu_r) * 0.45f;
        vu_acc_l = vu_acc_r = 0.0f;
        vu_n = 0;
    }
}

static void fire(ev_t *e)
{
    switch (e->type) {
    case E_KICK: kick_hit(e->vel); break;
    case E_RIM: rim_hit(e->vel); break;
    case E_SNARE: snare_hit(e->vel); break;
    case E_HAT: hat_hit(e->vel, 0); break;
    case E_OHAT: hat_hit(e->vel, 1); break;
    case E_TOM: tom_hit(e->note, e->vel); break;
    case E_BASS: bass_on(e->note, e->vel, e->gate); break;
    case E_GTR: gtr_on(e->note, e->vel, e->gate); break;
    case E_ORG: org_on(e->note, e->vel, e->gate, 0); break;
    case E_MEL: mel_on(e->note, e->vel, e->gate); break;
    case E_HORN: horn_on(e->aux % NHN, e->note, e->vel, e->gate); break;
    case E_THROW: thr_left[e->note % EN_NCH] = e->gate; break;
    case E_CRASH:
        crash_amp = e->vel;
        crash_fb = 0.12f;
        move(MV_SPRING);
        break;
    case E_SWELL:
        fb_boost_t = 0.22f + 0.1f * e->vel;
        swell_left = e->gate;
        move(MV_SWELL);
        break;
    default: break;
    }
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 1.6f;
    drain();
    while (n) {
        uint32_t k = n > BLK ? BLK : n, j;
        int i, seq = 0, c;
        if (en_playing) {
            if (tk.step_left <= 0.0f) {
                seq_step();
                tempo_set();
                tk.step_left += step_len;
            }
            seq = 1;
            if ((float)k > tk.step_left)
                k = (uint32_t)tk.step_left + 1u;
        }
        for (i = 0; i < NEV; i++)
            if (evq[i].used) {
                if (evq[i].wait <= 0) {
                    fire(&evq[i]);
                    evq[i].used = 0;
                } else if ((uint32_t)evq[i].wait < k) {
                    k = (uint32_t)evq[i].wait;
                }
            }
        if (k > n)
            k = n;
        for (i = 0; i < NEV; i++)
            if (evq[i].used)
                evq[i].wait -= (int32_t)k;
        if (seq) {
            tk.step_left -= (float)k;
            trk_samples += k;
            en_track_ms = (uint32_t)((float)trk_samples * (1000.0f / EN_SR));
        }
        control(k);
        for (c = 0; c < NX; c++)
            for (j = 0; j < k; j++)
                xb[c][j] = 0.0f;
        render_drums(k);
        render_bass(k);
        render_gtr(k);
        render_org(k);
        render_mel(k);
        render_horns(k);
        render_siren(k);
        render_desk(out, k, gain);
        out += 2 * k;
        n -= k;
    }
}
