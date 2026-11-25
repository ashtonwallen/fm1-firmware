/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * FELT PIANO: ashtonwallen, 2026 */
/* The instrument (felt.h). One compilation unit, -O2. The command queue and the render's shape are AMB-1's.
 */
#include "felt.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Tempo", 40, 120, 66},  {"Density", 0, 100, 35}, {"Felt", 0, 100, 55},  {"Room", 0, 100, 45},
    {"Rubato", 0, 100, 50},  {"Dynamics", 0, 100, 45}, {"Pedal", 0, 100, 60}, {"Noise", 0, 100, 35},
    {"Age", 0, 100, 10},     {"Width", 0, 100, 55},   {"Length", 0, 4, 1},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }
static const uint8_t LEN_MIN[5] = {2, 3, 4, 6, 8};

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
    else if (i == P_PEDAL && v < 15)
        put_s(buf, "Off");
    else
        put_u(buf, (uint32_t)(v < 0 ? 0 : v));
}

/* -------------------------------------------------------------- modes --- */
enum { M_ION, M_DOR, M_LYD, M_AEO, M_MIX, M_BLU, M_NM };
static const int8_t MODE_ST[M_NM][7] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 6, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 10},
    {0, 3, 4, 5, 6, 7, 10},                     /* the blues: both thirds, the flat fifth */
};
static const char *const MODE_NAME[M_NM] = {"major", "Dorian", "Lydian", "minor", "Mixolydian", "blues"};
const char *en_mode_name(int m) { return MODE_NAME[(unsigned)m < M_NM ? m : 0]; }
/* the mode's colour note (Dorian's sixth, Lydian's fourth): the improviser leans on it */
static const int8_t MODE_CHAR[M_NM] = {-1, 9, 6, -1, 10, -1};
static const char *const IDIOM_NAME[ID_N] = {"Gymnopedie", "Airport", "Ostinato", "Nocturne", "Modal",
                                             "Minimalist", "Chorale", "Stride", "Planing", "Lullaby", "Blues"};
const char *en_idiom_name(int id) { return IDIOM_NAME[(unsigned)id < ID_N ? id : 0]; }

/* chord types: semitones above the root */
enum { T_MAJ, T_MIN, T_MAJ7, T_M7, T_DOM7, T_ADD9, T_MADD9, T_MAJ9, T_M9, T_DOM9, T_SUS2, T_6, T_M7B5, T_DIM7, T_M6, T_DIM,
       T_N };
static const uint8_t CT_N[T_N] = {3, 3, 4, 4, 4, 4, 4, 5, 5, 5, 3, 4, 4, 4, 4, 3};
static const int8_t CT_IV[T_N][5] = {
    {0, 4, 7}, {0, 3, 7}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10}, {0, 4, 7, 14}, {0, 3, 7, 14},
    {0, 4, 7, 11, 14}, {0, 3, 7, 10, 14}, {0, 4, 7, 10, 14}, {0, 2, 7}, {0, 4, 7, 9}, {0, 3, 6, 10}, {0, 3, 6, 9}, {0, 3, 7, 9},
    {0, 3, 6},
};
typedef struct {
    int8_t root;                                 /* semitones above the tonic */
    uint8_t t;
} ch_t;

/* ------------------------------------------------------------- styles --- */
/* the user's ten (Tempo Density Felt Room, Rubato Dynamics Pedal Noise, Age Width) and the style's own: its idiom,
 * a variant (Gymnopedie: which chord pairs, bit i; Airport: its pentatonic; Ostinato: its figure; Nocturne: its
 * metre), its register (0 middle, 1 high, 2 low) and its modes (bit M_*) */
typedef struct {
    const char *name;
    int8_t v[P_NSTYLE];
    uint8_t idiom, var, reg;
    uint8_t modes;
} style_t;
#define MB(m) (1u << (m))
static const style_t STYLES[EN_NSTYLES] = {
    /*                  Tem Den Fel Roo  Rub Dyn Ped Noi  Age Wid   idiom     var reg modes */
    {"Erik's Bowler",  {66, 35, 55, 45, 50, 45, 60, 35, 10, 55}, ID_GYM, 0x03, 0, 0},
    {"Slow Carousel",  {60, 55, 65, 70, 20, 30, 100, 25, 0, 65}, ID_AIR, 1, 0, 0},
    {"Velvet Lid",     {52, 55, 40, 50, 65, 65, 55, 25, 0, 60}, ID_NOCT, 0, 0, MB(M_ION) | MB(M_AEO)},
    {"Paper Lanterns", {48, 25, 50, 80, 25, 25, 100, 15, 0, 75}, ID_AIR, 0, 1, 0},
    {"Hammer Rail",    {76, 60, 70, 40, 25, 40, 70, 55, 5, 60}, ID_OST, 1, 0, MB(M_AEO) | MB(M_DOR)},
    {"Una Corda",      {92, 50, 85, 35, 20, 55, 65, 70, 0, 55}, ID_OST, 0, 0, MB(M_ION) | MB(M_AEO)},
    {"Lemon Peel",     {64, 45, 35, 55, 55, 50, 70, 20, 0, 65}, ID_MODAL, 0, 0, MB(M_LYD)},
    {"Upright in 4B",  {72, 40, 60, 25, 45, 40, 55, 60, 60, 45}, ID_GYM, 0x18, 0, 0},
    {"Milk Teeth",     {60, 35, 75, 45, 50, 30, 60, 25, 0, 55}, ID_NOCT, 1, 1, MB(M_ION)},
    {"Row Q, Seat 9",  {60, 30, 45, 95, 50, 40, 75, 10, 0, 80}, ID_GYM, 0x14, 0, 0},
    {"Blue Pencil",    {56, 40, 55, 50, 60, 45, 65, 35, 10, 55}, ID_MODAL, 0, 2, MB(M_DOR)},
    {"Cold Radiator",  {50, 30, 70, 45, 65, 40, 70, 40, 15, 50}, ID_MODAL, 0, 0, MB(M_AEO)},
    {"Iron Pier",      {54, 45, 60, 75, 20, 35, 100, 30, 10, 70}, ID_AIR, 2, 2, 0},
    {"Coat Check",     {66, 50, 50, 55, 60, 60, 55, 30, 0, 60}, ID_NOCT, 2, 0, MB(M_AEO)},
    /* 1.1 (Minimalist: var 0 triplets over eighths, 1 eighths over held octaves, 2 rocking eighths over triplets;
     * Chorale: modes major and/or minor; Stride: var 0 octave bass, 1 tenths, 2 single bass; Planing: var 0
     * pentatonic stacks, 1 open fifths, 2 diatonic sevenths and a whole-tone passage; Lullaby: var 0 rocking
     * arpeggio, 1 bass and chords; Blues: var 0 rocking bass, 1 walking bass and the jazz changes, 2 sparse) */
    {"Laundromat",     {66, 50, 50, 55, 10, 30, 60, 30, 0, 65}, ID_ARP, 0, 0, MB(M_AEO) | MB(M_ION)},
    {"Bicycle Spokes", {84, 55, 40, 40, 5, 35, 55, 40, 5, 60}, ID_ARP, 2, 0, MB(M_ION)},
    {"Revolving Door", {60, 40, 65, 65, 15, 40, 70, 25, 10, 60}, ID_ARP, 1, 2, MB(M_AEO)},
    {"Pew Cushion",    {72, 50, 60, 70, 35, 40, 50, 30, 15, 50}, ID_HYMN, 0, 0, MB(M_ION)},
    {"Sexton's Keys",  {60, 50, 70, 80, 40, 45, 45, 25, 25, 55}, ID_HYMN, 0, 0, MB(M_AEO) | MB(M_ION)},
    {"Tip Jar",        {66, 45, 45, 40, 30, 50, 45, 45, 15, 55}, ID_STRIDE, 0, 0, MB(M_ION)},
    {"Brass Spittoon", {58, 35, 55, 50, 45, 55, 50, 50, 45, 50}, ID_STRIDE, 1, 0, MB(M_ION)},
    {"Sleeve Garter",  {74, 60, 40, 35, 25, 45, 40, 40, 25, 60}, ID_STRIDE, 2, 1, MB(M_ION)},
    {"Sunken Steeple", {46, 30, 60, 90, 30, 55, 95, 15, 0, 80}, ID_PLANE, 1, 2, MB(M_ION) | MB(M_MIX)},
    {"Flaxen Braid",   {52, 45, 55, 65, 50, 40, 75, 20, 0, 70}, ID_PLANE, 0, 0, MB(M_ION) | MB(M_LYD)},
    {"Linen Sails",    {50, 40, 50, 75, 45, 45, 85, 20, 5, 75}, ID_PLANE, 2, 1, MB(M_DOR) | MB(M_AEO)},
    {"Crib Mobile",    {50, 35, 45, 60, 30, 30, 60, 20, 0, 60}, ID_LULL, 0, 1, MB(M_ION)},
    {"Rocking Horse",  {56, 45, 65, 50, 40, 40, 55, 30, 10, 55}, ID_LULL, 1, 0, MB(M_ION)},
    {"Wet Ring",       {46, 35, 50, 45, 45, 60, 40, 45, 20, 50}, ID_BLUES, 2, 0, MB(M_BLU)},
    {"Mop Bucket",     {52, 50, 45, 40, 30, 55, 35, 50, 30, 55}, ID_BLUES, 0, 0, MB(M_BLU)},
    {"Bent Nickel",    {58, 55, 40, 35, 25, 50, 30, 45, 10, 60}, ID_BLUES, 1, 0, MB(M_BLU)},
};
static const style_t *sty(int i) { return &STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES]; }
const char *en_style_name(int i) { return sty(i)->name; }
int en_style_idiom(int i) { return sty(i)->idiom; }
void en_style_recipe(int i, int16_t *par)
{
    int k;
    for (k = 0; k < P_NSTYLE; k++)
        par[k] = sty(i)->v[k];
}

/* ---------------------------------------------------- curated material --- */
/* Gymnopedie: two chords rocking a bar each (A, then B), and two more for the middle (C, D); the tonic to end on */
typedef struct {
    uint8_t mode;
    ch_t a, b, c, d, home;
} gpair_t;
static const gpair_t GPAIRS[] = {
    {M_ION, {5, T_MAJ7}, {0, T_MAJ7}, {9, T_M7}, {2, T_M7}, {0, T_MAJ7}},       /* IVmaj7 Imaj7 (Satie's) */
    {M_ION, {2, T_M7}, {0, T_MAJ7}, {5, T_MAJ7}, {9, T_M7}, {0, T_MAJ7}},       /* ii7 Imaj7 */
    {M_LYD, {0, T_MAJ7}, {2, T_MAJ}, {4, T_M7}, {9, T_M7}, {0, T_MAJ7}},        /* Imaj7 II: Lydian's float */
    {M_DOR, {0, T_M7}, {5, T_DOM7}, {3, T_MAJ7}, {10, T_MAJ}, {0, T_M9}},       /* i7 IV7 */
    {M_AEO, {0, T_MADD9}, {8, T_MAJ7}, {5, T_M7}, {3, T_MAJ7}, {0, T_MADD9}},   /* i bVImaj7 */
};
#define NGPAIRS (sizeof GPAIRS / sizeof GPAIRS[0])

/* Nocturne: four-bar phrases, a chord a bar (a second in a bar: after the bar's half). The antecedent ends on V, the
 * consequent goes home; B is the contrasting period */
typedef struct {
    ch_t c[4][2];                                /* [bar][half]: half 1 root 99 = the bar's chord held */
} nphr_t;
#define NX {99, 0}
static const nphr_t N_ANT_MAJ[] = {
    {{{{0, T_MAJ}, NX}, {{9, T_MIN}, NX}, {{2, T_M7}, NX}, {{7, T_DOM7}, NX}}},
    {{{{0, T_MAJ}, NX}, {{5, T_MAJ}, NX}, {{0, T_MAJ}, NX}, {{7, T_MAJ}, NX}}},
    {{{{0, T_MAJ7}, NX}, {{4, T_MIN}, NX}, {{5, T_MAJ}, NX}, {{7, T_DOM7}, NX}}},
    {{{{0, T_MAJ}, NX}, {{9, T_M7}, NX}, {{5, T_MAJ7}, NX}, {{7, T_MAJ}, NX}}},
};
static const nphr_t N_CON_MAJ[] = {
    {{{{0, T_MAJ}, NX}, {{9, T_MIN}, NX}, {{2, T_M7}, {7, T_DOM7}}, {{0, T_MAJ}, NX}}},
    {{{{5, T_MAJ}, NX}, {{4, T_MIN}, NX}, {{2, T_M7}, {7, T_DOM7}}, {{0, T_MAJ}, NX}}},
    {{{{0, T_MAJ}, NX}, {{5, T_MAJ}, NX}, {{7, T_DOM7}, NX}, {{0, T_MAJ}, NX}}},
};
static const nphr_t N_B_MAJ[] = {
    {{{{9, T_MIN}, NX}, {{4, T_MIN}, NX}, {{5, T_MAJ}, NX}, {{7, T_DOM7}, NX}}},
    {{{{5, T_MAJ7}, NX}, {{7, T_MAJ}, NX}, {{4, T_M7}, NX}, {{9, T_MIN}, NX}}},
    {{{{9, T_M7}, NX}, {{5, T_MAJ}, NX}, {{2, T_M7}, NX}, {{7, T_DOM7}, NX}}},
};
static const nphr_t N_ANT_MIN[] = {
    {{{{0, T_MIN}, NX}, {{5, T_MIN}, NX}, {{10, T_MAJ}, NX}, {{3, T_MAJ}, NX}}},
    {{{{0, T_MIN}, NX}, {{8, T_MAJ}, NX}, {{5, T_MIN}, NX}, {{7, T_MAJ}, NX}}},
    {{{{0, T_MIN}, NX}, {{5, T_M7}, NX}, {{0, T_MIN}, NX}, {{7, T_DOM7}, NX}}},
};
static const nphr_t N_CON_MIN[] = {
    {{{{0, T_MIN}, NX}, {{5, T_MIN}, NX}, {{7, T_DOM7}, NX}, {{0, T_MIN}, NX}}},
    {{{{8, T_MAJ}, NX}, {{5, T_MIN}, NX}, {{2, T_M7B5}, {7, T_DOM7}}, {{0, T_MIN}, NX}}},
    {{{{5, T_MIN}, NX}, {{0, T_MIN}, NX}, {{7, T_DOM7}, NX}, {{0, T_MIN}, NX}}},
};
static const nphr_t N_B_MIN[] = {
    {{{{3, T_MAJ}, NX}, {{10, T_MAJ}, NX}, {{5, T_MIN}, NX}, {{7, T_DOM7}, NX}}},
    {{{{8, T_MAJ7}, NX}, {{3, T_MAJ}, NX}, {{5, T_M7}, NX}, {{7, T_MAJ}, NX}}},
    {{{{5, T_MIN}, NX}, {{10, T_DOM7}, NX}, {{3, T_MAJ7}, NX}, {{7, T_DOM7}, NX}}},
};
#define NA(a) (int)(sizeof a / sizeof a[0])

/* the left hand's figures: steps through a ladder (root, fifth, octave, tenth, twelfth; a seventh for the octave
 * when the chord has one) */
static const int8_t LH_128[12] = {0, 1, 3, 2, 3, 1, 0, 1, 3, 4, 3, 1};
static const int8_t LH_34[6] = {0, 1, 3, 4, 3, 1};
static const int8_t LH_44[8] = {0, 1, 2, 3, 4, 3, 2, 1};

/* Modal: the two chords of the drone, and two for the middle */
typedef struct {
    ch_t a, b, c, d;
} mpair_t;
static const mpair_t MPAIRS[M_NM] = {
    {{0, T_MAJ9}, {5, T_MAJ7}, {9, T_M7}, {2, T_M7}},
    {{0, T_M9}, {5, T_DOM9}, {3, T_MAJ7}, {10, T_ADD9}},
    {{0, T_MAJ9}, {2, T_ADD9}, {4, T_M7}, {9, T_M7}},
    {{0, T_MADD9}, {8, T_MAJ7}, {10, T_ADD9}, {5, T_M7}},
    {{0, T_ADD9}, {10, T_ADD9}, {5, T_MAJ7}, {7, T_M7}},
};

/* Airport: pentatonic sets, and phrases in their steps (5 an octave; negative below) with onsets in ticks */
static const int8_t PENT[3][5] = {{0, 2, 4, 7, 9}, {0, 2, 4, 7, 11}, {0, 2, 3, 7, 9}};
typedef struct {
    uint8_t n;
    int8_t st[4];
    uint8_t at[4];
} amot_t;
static const amot_t AMOT[] = {
    {3, {2, 3, 5}, {0, 18, 36}},  {2, {4, 3}, {0, 15}},           {1, {0}, {0}},
    {4, {5, 4, 2, 3}, {0, 15, 30, 54}}, {3, {1, 2, 1}, {0, 9, 24}}, {2, {3, 5}, {0, 12}},
    {3, {6, 5, 4}, {0, 18, 27}},  {3, {2, 1, -1}, {0, 12, 24}},   {2, {0, 4}, {0, 6}},
    {3, {4, 6, 7}, {0, 9, 18}},   {2, {7, 6}, {0, 24}},           {3, {3, 2, 0}, {0, 21, 33}},
    {2, {0, 3}, {0, 30}},                                         /* (the low loop's) */
};
#define NAMOT NA(AMOT)

/* the melody's bars: notes as (onset, length) in ticks, 12 a beat */
typedef struct {
    uint8_t n;
    uint8_t on[5], du[5];
} rhy_t;
static const rhy_t R34[] = {
    {1, {0}, {36}}, {2, {0, 24}, {24, 12}}, {3, {0, 12, 24}, {12, 12, 12}}, {2, {12, 24}, {12, 12}},
    {1, {12}, {24}}, {2, {0, 12}, {12, 24}}, {3, {0, 18, 24}, {18, 6, 12}}, {4, {0, 12, 18, 24}, {12, 6, 6, 12}},
};
static const rhy_t R44[] = {
    {1, {0}, {48}}, {2, {0, 24}, {24, 24}}, {2, {0, 36}, {36, 12}}, {3, {0, 12, 24}, {12, 12, 24}},
    {3, {0, 18, 24}, {18, 6, 24}}, {3, {0, 24, 36}, {24, 12, 12}}, {2, {12, 24}, {12, 24}},
    {4, {0, 12, 24, 36}, {12, 12, 12, 12}}, {4, {0, 6, 12, 24}, {6, 6, 12, 24}}, {2, {0, 12}, {12, 36}},
};
static const rhy_t R128[] = {
    {2, {0, 24}, {24, 24}}, {1, {0}, {48}}, {3, {0, 12, 24}, {12, 12, 24}}, {4, {0, 8, 12, 24}, {8, 4, 12, 24}},
    {5, {0, 12, 20, 24, 36}, {12, 8, 4, 12, 12}}, {5, {0, 4, 8, 12, 24}, {4, 4, 4, 12, 24}}, {2, {12, 24}, {12, 24}},
    {4, {0, 24, 32, 36}, {24, 8, 4, 12}},
};


/* ---------------------------------------------- curated material (1.1) --- */
/* Minimalist: short chord cycles (each chord two or four bars), and the right hand's figures, stage by stage: a
 * note more each stage (indices into the chord's ladder: its three notes close, then the lower two an octave up) */
typedef struct {
    uint8_t mode, n;
    ch_t c[4];
} acyc_t;
static const acyc_t ACYC[] = {
    {M_AEO, 4, {{0, T_MIN}, {8, T_MAJ}, {3, T_MAJ}, {10, T_MAJ}}},   /* i bVI bIII bVII */
    {M_AEO, 4, {{0, T_MIN}, {8, T_MAJ}, {5, T_MIN}, {7, T_MAJ}}},    /* i bVI iv V */
    {M_AEO, 4, {{0, T_MIN}, {10, T_MAJ}, {8, T_MAJ}, {7, T_MAJ}}},   /* i bVII bVI V, the Andalusian fall */
    {M_AEO, 2, {{0, T_MIN}, {8, T_MAJ}}},                            /* i bVI, rocking */
    {M_ION, 4, {{0, T_MAJ}, {4, T_MIN}, {5, T_MAJ}, {5, T_MIN}}},    /* I iii IV iv */
    {M_ION, 4, {{9, T_MIN}, {5, T_MAJ}, {0, T_MAJ}, {7, T_MAJ}}},    /* vi IV I V */
    {M_ION, 2, {{0, T_MAJ}, {5, T_MAJ}}},                            /* I IV */
};
#define NACYC NA(ACYC)
static const int8_t AF8[4][8] = {{0, 2, 0, 2, 0, 2, 0, 2}, {0, 1, 2, 1, 0, 1, 2, 1}, {0, 1, 2, 3, 0, 1, 2, 3},
                                 {0, 1, 2, 3, 4, 3, 2, 1}};
static const int8_t AF12[4][12] = {{0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2}, {0, 1, 2, 1, 0, 1, 2, 1, 0, 1, 2, 1},
                                   {0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1}, {0, 1, 2, 3, 4, 3, 0, 1, 2, 3, 4, 3}};

/* Chorale: phrases of four bars written as soprano and bass over the chords (in C; eighths, chord, soprano, bass),
 * checked for parallels in the outer voices; the alto and tenor are found as it plays. A tune: an opening phrase
 * (to a half close), a closing one, a middle one (away), a closing one; verses of it, then an amen */
typedef struct {
    uint8_t d;                                   /* eighths */
    int8_t r;
    uint8_t t, s, b;                             /* chord type; soprano, bass (MIDI, in C) */
} hst_t;
typedef struct {
    uint8_t n;
    hst_t st[14];
} hphr_t;
enum { H_O1, H_O2, H_C1, H_C2, H_M1, H_M2, H_AMEN, H_MO1, H_MC1, H_MM1, H_MAMEN };
static const hphr_t HPHR[] = {
    {13, {{2, 0, T_MAJ, 67, 48}, {2, 0, T_MAJ, 67, 52}, {2, 5, T_MAJ, 69, 53}, {2, 7, T_MAJ, 67, 55},
          {2, 0, T_MAJ, 72, 52}, {2, 7, T_MAJ, 71, 55}, {2, 9, T_MIN, 69, 57}, {2, 0, T_MAJ, 67, 52},
          {2, 5, T_MAJ, 69, 53}, {2, 0, T_MAJ, 67, 48}, {2, 7, T_DOM7, 65, 50}, {2, 0, T_MAJ, 64, 48},
          {8, 7, T_MAJ, 62, 43}}},
    {13, {{2, 0, T_MAJ, 64, 48}, {2, 7, T_DOM7, 65, 50}, {2, 0, T_MAJ, 67, 52}, {2, 9, T_MIN, 72, 45},
          {2, 5, T_MAJ, 69, 53}, {2, 0, T_MAJ, 67, 55}, {2, 5, T_MAJ, 65, 57}, {2, 0, T_MAJ, 64, 48},
          {2, 7, T_MAJ, 62, 43}, {2, 0, T_MAJ, 64, 48}, {2, 2, T_MIN, 65, 53}, {2, 2, T_MIN, 69, 53},
          {8, 7, T_MAJ, 67, 43}}},
    {12, {{2, 0, T_MAJ, 72, 52}, {2, 7, T_MAJ, 71, 43}, {2, 9, T_MIN, 69, 45}, {2, 0, T_MAJ, 67, 52},
          {2, 5, T_MAJ, 65, 53}, {2, 2, T_MIN, 69, 50}, {2, 7, T_MAJ, 67, 43}, {2, 7, T_DOM7, 65, 43},
          {2, 0, T_MAJ, 64, 48}, {2, 5, T_MAJ, 65, 41}, {4, 7, T_MAJ, 62, 43}, {8, 0, T_MAJ, 60, 48}}},
    {13, {{2, 0, T_MAJ, 67, 52}, {2, 5, T_MAJ, 69, 53}, {2, 11, T_DIM, 71, 50}, {2, 0, T_MAJ, 72, 48},
          {2, 7, T_MAJ, 74, 47}, {2, 0, T_MAJ, 72, 48}, {2, 7, T_MAJ, 71, 43}, {2, 9, T_MIN, 69, 45},
          {2, 0, T_MAJ, 67, 52}, {2, 2, T_MIN, 65, 53}, {2, 0, T_MAJ, 64, 43}, {2, 7, T_DOM7, 62, 43},
          {8, 0, T_MAJ, 60, 48}}},
    {13, {{2, 9, T_MIN, 69, 45}, {2, 5, T_MAJ, 69, 53}, {2, 0, T_MAJ, 67, 52}, {2, 0, T_MAJ, 64, 48},
          {2, 2, T_MIN, 65, 50}, {2, 0, T_MAJ, 67, 52}, {2, 5, T_MAJ, 69, 53}, {2, 7, T_MAJ, 71, 55},
          {2, 9, T_MIN, 72, 57}, {2, 4, T_MAJ, 71, 52}, {2, 9, T_MIN, 69, 45}, {2, 2, T_MIN, 69, 53},
          {8, 4, T_MAJ, 68, 52}}},
    {14, {{2, 0, T_MAJ, 64, 48}, {2, 7, T_DOM7, 65, 50}, {2, 0, T_MAJ, 67, 52}, {2, 5, T_MAJ, 69, 53},
          {2, 0, T_MAJ, 67, 48}, {2, 9, T_MIN, 72, 45}, {2, 4, T_MIN, 71, 52}, {2, 5, T_MAJ, 69, 53},
          {2, 0, T_MAJ, 67, 52}, {2, 2, T_MAJ, 66, 50}, {2, 7, T_MAJ, 67, 43}, {2, 2, T_DOM7, 69, 48},
          {4, 7, T_MAJ, 67, 47}, {4, 7, T_MAJ, 67, 43}}},
    {2, {{8, 5, T_MAJ, 69, 53}, {8, 0, T_MAJ, 67, 48}}},
    {13, {{2, 0, T_MIN, 63, 48}, {2, 7, T_DOM7, 65, 50}, {2, 0, T_MIN, 67, 51}, {2, 0, T_MIN, 67, 48},
          {2, 5, T_MIN, 68, 53}, {2, 0, T_MIN, 67, 55}, {2, 5, T_MIN, 65, 56}, {2, 0, T_MIN, 63, 48},
          {2, 5, T_MIN, 65, 53}, {2, 0, T_MIN, 67, 51}, {2, 8, T_MAJ, 68, 44}, {2, 2, T_DIM, 65, 53},
          {8, 7, T_MAJ, 67, 43}}},
    {12, {{2, 0, T_MIN, 72, 51}, {2, 3, T_MAJ, 70, 51}, {2, 5, T_MIN, 68, 53}, {2, 0, T_MIN, 67, 51},
          {2, 2, T_DIM, 65, 53}, {2, 0, T_MIN, 67, 48}, {2, 5, T_MIN, 68, 53}, {2, 0, T_MIN, 67, 51},
          {2, 2, T_DIM, 65, 53}, {2, 0, T_MIN, 63, 43}, {4, 7, T_MAJ, 62, 43}, {8, 0, T_MIN, 60, 48}}},
    {13, {{2, 0, T_MIN, 63, 48}, {2, 0, T_MIN, 67, 51}, {2, 3, T_MAJ, 70, 51}, {2, 10, T_MAJ, 70, 46},
          {2, 10, T_DOM7, 68, 46}, {2, 3, T_MAJ, 67, 51}, {2, 5, T_MIN, 65, 53}, {2, 3, T_MAJ, 67, 51},
          {2, 5, T_MIN, 68, 53}, {2, 10, T_MAJ, 65, 50}, {2, 3, T_MAJ, 67, 46}, {2, 10, T_DOM7, 65, 46},
          {8, 3, T_MAJ, 63, 51}}},
    {2, {{8, 5, T_MIN, 68, 53}, {8, 0, T_MAJ, 67, 48}}},   /* iv, then the major tonic: a Picardy amen */
};

/* Stride: eight-bar sections, a chord a bar or two (NX: the bar's chord held); the song's A and its bridge */
#define S_I6 {0, T_6}
#define S_I7 {0, T_DOM7}
#define S_IM7 {0, T_MAJ7}
#define S_ID7 {1, T_DIM7}
#define S_II7 {2, T_DOM7}
#define S_ii7 {2, T_M7}
#define S_III7 {4, T_DOM7}
#define S_iii7 {4, T_M7}
#define S_IV6 {5, T_6}
#define S_iv6 {5, T_M6}
#define S_IVD7 {6, T_DIM7}
#define S_V7 {7, T_DOM7}
#define S_v7 {7, T_M7}
#define S_VI7 {9, T_DOM7}
typedef struct {
    ch_t c[8][2];
} ssec_t;
static const ssec_t S_A[] = {
    {{{S_I6, S_VI7}, {S_ii7, S_V7}, {S_I6, S_VI7}, {S_ii7, S_V7}, {S_I6, S_I7}, {S_IV6, S_iv6}, {S_I6, S_VI7}, {S_ii7, S_V7}}},
    {{{S_IM7, S_ID7}, {S_ii7, S_V7}, {S_iii7, S_VI7}, {S_ii7, S_V7}, {S_I6, S_I7}, {S_IV6, S_IVD7}, {S_I6, S_VI7}, {S_ii7, S_V7}}},
    {{{S_I6, NX}, {S_VI7, NX}, {S_ii7, NX}, {S_V7, NX}, {S_I6, S_I7}, {S_IV6, S_iv6}, {S_iii7, S_VI7}, {S_ii7, S_V7}}},
};
static const ssec_t S_B[] = {
    {{{S_III7, NX}, {S_III7, NX}, {S_VI7, NX}, {S_VI7, NX}, {S_II7, NX}, {S_II7, NX}, {S_ii7, NX}, {S_V7, NX}}},
    {{{S_v7, S_I7}, {S_IV6, NX}, {S_IV6, NX}, {S_iv6, NX}, {S_iii7, NX}, {S_VI7, NX}, {S_II7, NX}, {S_ii7, S_V7}}},
};
/* swung bars (4/4, 12 ticks a beat: the off-beat at 8) */
static const rhy_t R_SW[] = {
    {1, {0}, {40}}, {2, {0, 24}, {20, 22}}, {3, {0, 12, 24}, {12, 12, 22}}, {3, {0, 8, 12}, {8, 4, 32}},
    {4, {0, 8, 12, 24}, {8, 4, 12, 22}}, {3, {8, 12, 24}, {4, 12, 22}}, {4, {0, 12, 20, 24}, {12, 8, 4, 22}},
    {3, {0, 20, 24}, {20, 4, 22}}, {2, {12, 24}, {12, 22}}, {4, {0, 12, 24, 36}, {12, 12, 12, 10}},
    {5, {0, 8, 12, 20, 24}, {8, 4, 8, 4, 22}},
};

/* Planing: the pentatonics, the whole-tone row; the rhythms of a planed phrase (two bars, ticks), the top line's
 * contours (steps in the collection), the arabesques above (pentatonic steps, onsets, lengths) */
static const int8_t PENT_MAJ[5] = {0, 2, 4, 7, 9}, PENT_MIN[5] = {0, 3, 5, 7, 10}, WT6[6] = {0, 2, 4, 6, 8, 10};
typedef struct {
    uint8_t n, du[6];
} plr_t;
static const plr_t PL_R[] = {
    {4, {24, 24, 24, 24}}, {4, {12, 12, 24, 48}}, {3, {36, 12, 48}}, {4, {18, 6, 24, 48}}, {5, {12, 12, 12, 12, 48}},
    {4, {24, 12, 12, 48}}, {3, {24, 24, 48}},
};
typedef struct {
    uint8_t n;
    int8_t s[5];
} plc_t;
static const plc_t PL_C[] = {
    {3, {0, 1, 2}}, {3, {0, -1, -2}}, {3, {0, 1, 0}}, {3, {0, -1, 0}}, {3, {0, 2, 1}},
    {4, {0, 1, 2, 1}}, {4, {0, -1, -2, -1}}, {4, {0, 1, 0, -1}}, {4, {0, -1, -2, -3}}, {4, {0, 1, 2, 3}}, {4, {2, 1, 0, 1}},
    {5, {0, 1, 2, 1, 0}}, {5, {0, -1, -2, -1, 0}}, {5, {0, 1, 0, -1, -2}}, {5, {0, -1, 0, 1, 2}}, {5, {0, 1, 2, 3, 2}},
};
typedef struct {
    uint8_t n;
    int8_t s[6];
    uint8_t at[6], du[6];
} plar_t;
static const plar_t PL_A[] = {
    {5, {0, 1, 2, 3, 4}, {0, 3, 6, 9, 12}, {3, 3, 3, 3, 30}},
    {4, {2, 1, 0, 1}, {0, 6, 12, 24}, {6, 6, 12, 30}},
    {5, {0, 2, 1, 3, 2}, {0, 4, 8, 12, 16}, {4, 4, 4, 4, 30}},
    {5, {4, 3, 2, 1, 0}, {0, 3, 6, 9, 12}, {3, 3, 3, 3, 30}},
    {5, {0, 1, 0, -1, 0}, {0, 9, 12, 18, 24}, {9, 3, 6, 6, 30}},
    {3, {0, 2, 4}, {0, 12, 24}, {12, 12, 36}},
    {4, {0, -1, 1, 0}, {0, 6, 9, 12}, {6, 3, 3, 30}},
};

/* Lullaby (6/8: two beats of three eighths, 12 ticks each): four-bar phrases, antecedent to V, consequent home, and
 * a middle; the tune's bars */
static const nphr_t L_ANT[] = {
    {{{{0, T_MAJ}, NX}, {{5, T_MAJ}, NX}, {{0, T_MAJ}, NX}, {{7, T_MAJ}, NX}}},
    {{{{0, T_MAJ}, NX}, {{9, T_MIN}, NX}, {{5, T_MAJ}, NX}, {{7, T_MAJ}, NX}}},
    {{{{0, T_MAJ}, NX}, {{7, T_DOM7}, NX}, {{0, T_MAJ}, NX}, {{7, T_MAJ}, NX}}},
    {{{{0, T_MAJ}, NX}, {{5, T_MAJ}, NX}, {{2, T_MIN}, NX}, {{7, T_MAJ}, NX}}},
};
static const nphr_t L_CON[] = {
    {{{{0, T_MAJ}, NX}, {{5, T_MAJ}, NX}, {{7, T_DOM7}, NX}, {{0, T_MAJ}, NX}}},
    {{{{0, T_MAJ}, NX}, {{9, T_MIN}, NX}, {{2, T_MIN}, {7, T_DOM7}}, {{0, T_MAJ}, NX}}},
    {{{{5, T_MAJ}, NX}, {{0, T_MAJ}, NX}, {{7, T_DOM7}, NX}, {{0, T_MAJ}, NX}}},
    {{{{0, T_MAJ}, NX}, {{5, T_MAJ}, NX}, {{0, T_MAJ}, {7, T_DOM7}}, {{0, T_MAJ}, NX}}},
};
static const nphr_t L_B[] = {
    {{{{5, T_MAJ}, NX}, {{0, T_MAJ}, NX}, {{5, T_MAJ}, NX}, {{7, T_MAJ}, NX}}},
    {{{{9, T_MIN}, NX}, {{4, T_MIN}, NX}, {{5, T_MAJ}, NX}, {{7, T_MAJ}, NX}}},
    {{{{5, T_MAJ}, NX}, {{5, T_MAJ}, NX}, {{0, T_MAJ}, NX}, {{7, T_DOM7}, NX}}},
};
static const rhy_t R68[] = {
    {1, {0}, {24}}, {2, {0, 12}, {12, 12}}, {4, {0, 8, 12, 20}, {8, 4, 8, 4}}, {3, {0, 8, 12}, {8, 4, 12}},
    {4, {0, 4, 8, 12}, {4, 4, 4, 12}}, {3, {0, 12, 20}, {12, 8, 4}}, {5, {0, 8, 12, 16, 20}, {8, 4, 4, 4, 4}},
    {2, {0, 8}, {8, 16}},
};
static const int8_t LH_68[6] = {0, 1, 2, 3, 2, 1};

/* Blues (12/8: four beats of three eighths): the twelve bars (a bar's second chord from its half), and the right
 * hand's licks: (tick, semitones from the tonic, length, 1 a grace from below, 2 the fourth under it, 4 the sixth) */
typedef struct {
    ch_t c[12][2];
} bform_t;
#define B_I7 {0, T_DOM7}
#define B_IV7 {5, T_DOM7}
#define B_V7 {7, T_DOM7}
static const bform_t BFORM[] = {
    {{{B_I7, NX}, {B_IV7, NX}, {B_I7, NX}, {B_I7, NX}, {B_IV7, NX}, {B_IV7, NX}, {B_I7, NX}, {B_I7, NX}, {B_V7, NX},
      {B_IV7, NX}, {B_I7, NX}, {B_V7, NX}}},
    {{{B_I7, NX}, {B_IV7, NX}, {B_I7, NX}, {B_I7, NX}, {B_IV7, NX}, {{6, T_DIM7}, NX}, {B_I7, NX}, {{9, T_DOM7}, NX},
      {{2, T_M7}, NX}, {B_V7, NX}, {B_I7, {9, T_DOM7}}, {{2, T_M7}, B_V7}}},
    {{{B_I7, NX}, {B_I7, NX}, {B_I7, NX}, {B_I7, NX}, {B_IV7, NX}, {B_IV7, NX}, {B_I7, NX}, {B_I7, NX}, {B_V7, NX},
      {B_IV7, NX}, {B_I7, B_IV7}, {B_I7, B_V7}}},
};
typedef struct {
    uint8_t n, at[7];
    int8_t o[7];
    uint8_t du[7], fl[7];
} lick_t;
static const lick_t LICK[] = {
    {5, {0, 8, 12, 20, 24}, {4, 0, -2, -5, 0}, {8, 4, 8, 4, 20}, {1, 0, 0, 0, 0}},
    {6, {0, 8, 12, 16, 20, 24}, {12, 12, 10, 7, 3, 0}, {8, 4, 4, 4, 4, 20}, {2, 2, 0, 0, 0, 0}},
    {6, {0, 4, 8, 12, 16, 20}, {10, 7, 6, 5, 3, 0}, {4, 4, 4, 4, 4, 24}, {0}},
    {4, {0, 4, 8, 12}, {-5, -2, 0, 4}, {4, 4, 4, 22}, {0, 0, 0, 1}},
    {5, {0, 4, 8, 20, 24}, {5, 6, 7, 3, 0}, {4, 4, 12, 4, 20}, {0}},
    {2, {0, 24}, {4, 0}, {22, 20}, {1, 0}},
    {7, {0, 4, 8, 12, 20, 24, 32}, {0, -2, 0, 3, 0, -2, -5}, {4, 4, 4, 8, 4, 8, 14}, {0}},
    {3, {0, 12, 24}, {9, 7, 4}, {12, 12, 20}, {4, 4, 5}},
    {5, {12, 16, 20, 24, 32}, {7, 9, 7, 4, 0}, {4, 4, 4, 8, 14}, {0, 0, 0, 1, 0}},
};
#define NLICK NA(LICK)
/* the calls (lines one and two) and the answers that come home (line three) */
static const uint8_t LICK_CALL[] = {0, 1, 3, 4, 5, 7, 8}, LICK_HOME[] = {2, 6, 0, 7};

/* -------------------------------------------------------------- titles --- */
static const char *const T_ADJ[] = {
    "quiet", "slow", "small", "pale", "distant", "rented", "soft", "grey", "blue", "still", "winter", "paper", "early",
    "faint", "white", "empty", "folded", "sleeping", "patient", "gentle", "old", "last", "far", "low", "open"};
static const char *const T_NOUN[] = {
    "rooms", "stairs", "windows", "letters", "teacups", "mittens", "lamps", "hours", "kilns", "rivers", "clouds", "fields",
    "lofts", "attics", "hallways", "shadows", "candles", "bells", "birds", "trains", "mornings", "evenings",
    "curtains", "stones", "shores", "pages", "streets", "boats", "orchards", "ferries"};

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

void en_title(uint32_t seed, int idiom, char *b)
{
    uint32_t h = en_track_rand(seed, 77u);
    const char *a = T_ADJ[h % (uint32_t)NA(T_ADJ)], *n = T_NOUN[(h >> 8) % (uint32_t)NA(T_NOUN)];
    int f = (int)((h >> 20) % 4u);
    char *e = b;
    switch (idiom) {
    case ID_GYM:
        e = f == 0 ? put_s(put_s(put_s(b, "waltz for "), a), " ") : f == 1 ? put_s(b, "slow dance for ") : b;
        if (f > 1)
            e = put_s(put_s(b, a), " ");
        put_s(e, n);
        break;
    case ID_AIR:
        put_s(put_s(put_s(put_s(b, f & 1 ? "music for " : "for "), a), " "), n);
        break;
    case ID_NOCT:
        if (f == 0)
            put_s(put_s(put_s(b, a), " "), "nocturne");
        else
            put_s(put_s(b, "nocturne for "), n);
        break;
    case ID_OST:
        if (f == 0)
            put_s(put_s(b, n), ", again");
        else
            put_s(put_s(put_s(b, a), " "), n);
        break;
    case ID_ARP:
        if (f == 0)
            put_s(put_s(b, n), " in rotation");
        else if (f == 1)
            put_s(put_s(b, "metamorphosis of "), n);
        else
            put_s(put_s(put_s(put_s(b, a), " "), n), ", turning");
        break;
    case ID_HYMN:
        if (f == 0)
            put_s(put_s(put_s(put_s(b, "hymn for "), a), " "), n);
        else if (f == 1)
            put_s(put_s(b, "evensong for "), n);
        else
            put_s(put_s(put_s(put_s(b, a), " "), n), " hymn");
        break;
    case ID_STRIDE:
        if (f == 0)
            put_s(put_s(b, "ballad for "), n);
        else if (f == 1)
            put_s(put_s(put_s(b, "if "), n), " could talk");
        else
            put_s(put_s(put_s(put_s(b, a), " "), n), f == 2 ? " ballad" : " serenade");
        break;
    case ID_PLANE:
        if (f == 0)
            put_s(put_s(put_s(put_s(b, "prelude: "), a), " "), n);
        else if (f == 1)
            put_s(put_s(b, n), " in the water");
        else if (f == 2)
            put_s(put_s(b, "reflections in "), n);
        else
            put_s(put_s(put_s(put_s(b, "the "), a), " "), n);
        break;
    case ID_LULL:
        if (f == 0)
            put_s(put_s(b, "lullaby for "), n);
        else if (f == 1)
            put_s(put_s(b, "cradle song for "), n);
        else if (f == 2)
            put_s(put_s(put_s(put_s(b, "hush, "), a), " "), n);
        else
            put_s(put_s(put_s(put_s(b, a), " "), n), " lullaby");
        break;
    case ID_BLUES:
        if (f == 0)
            put_s(put_s(put_s(put_s(b, a), " "), n), " blues");
        else if (f == 1)
            put_s(put_s(b, "blues for "), n);
        else if (f == 2)
            put_s(put_s(b, n), " blues");
        else
            put_s(put_s(put_s(b, "low down "), n), " blues");
        break;
    default:
        if (f == 0)
            put_s(put_s(put_s(b, n), " at "), (h >> 24) & 1 ? "3am" : "the bakery");
        else
            put_s(put_s(put_s(b, a), " "), n);
        break;
    }
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_STYLE, C_PLAY, C_SKIP, C_REPEAT, C_TRACK_NO, C_TRACK_SEED_HI, C_TRACK_SEED_LO, C_NOTE, C_PANIC };
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
void en_track(uint32_t no, uint32_t seed)
{
    post(C_TRACK_SEED_HI, 0, (int)(seed >> 16));
    post(C_TRACK_SEED_LO, 0, (int)(seed & 0xFFFFu));
    post(C_TRACK_NO, (int)(no >> 16 & 0xFFu), (int)(no & 0xFFFFu));
}

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_phase, en_repeat_on, en_key, en_mode, en_idiom, en_meter = 3, en_compound, en_bpm = 66;
volatile uint32_t en_track_no, en_track_seed, en_seed_base = 0x46454C54u, en_track_ms, en_track_len_ms;
volatile uint16_t en_chord_pcs;
volatile uint8_t en_chord_root, en_pedal, en_voices;
volatile float en_vu_l, en_vu_r;
volatile uint32_t en_now_ms, en_notes_played, en_tracks_played;
volatile en_logn_t EN_LOG[EN_NLOG];
volatile uint32_t en_log_w;
volatile uint32_t EN_PED[EN_NPED];
volatile uint32_t en_ped_w;
#ifdef EN_HOST
void (*en_dbg_note)(int bar, int tick, int note, float vel, int role, int dur_ticks);
void (*en_dbg_chord)(int bar, int tick, int root_pc, uint16_t pcs);
void (*en_dbg_satb)(int bar, int tick, const int *satb);
#endif

static int16_t par[P_NPARAMS];
static int style_i;
static uint32_t rng = 0x9E3779B9u, nrng = 0x2545F491u, trng = 1u;
static uint32_t ms_rem, pstart;                  /* pstart: the piece's start on the clock */

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
static float trand(void) { return (float)(xs(&trng) >> 8) * (1.0f / 16777216.0f); }   /* the piece's own */
static int tirand(int n) { return n > 0 ? (int)((xs(&trng) >> 8) % (uint32_t)n) : 0; }
static inline float noise(void) { return (float)(int32_t)xs(&nrng) * (1.0f / 2147483648.0f); }
static float pf(int p) { return (float)par[p] * 0.01f; }

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
static inline float tau(float sec) { return fm_expf(-1.0f / (sec * EN_SR)); }
static inline float onepole(float fc) { return 1.0f - fm_expf(-FM_TWO_PI * fc / EN_SR); }

/* --------------------------------------------------------------- piano --- */
/* a note: up to NM partials, each a two-pole resonator ringing down from a sine's start (y = c y1 - r2 y2). The
 * lowest two partials are doubled, a hair apart and the second slower: the unison strings' beat and the long
 * "aftersound". The voice's envelope is the hammer's felt (a soft rise) and the damper (a fall) */
#define NV 16
#define NM 8
#define BLK 256
typedef struct {
    float y1[NM], y2[NM], c[NM], r2[NM], lvl[NM], rb[NM];
    float env, env_t, env_k, gl, gr;
    float damp_k;                                /* the damper's rate (per sample, toward 0) */
    int32_t gate;                                /* samples the key is held; < 0 held by the player; 0 up */
    uint32_t log, age, cnt;
    uint8_t nm, note, on, player, nodamp, damped;
} pv_t;
static pv_t pv[NV];
static uint32_t pv_clock;
static int ped;                                  /* the sustain pedal down */
static float vb[BLK];

static void log_off(pv_t *v)
{
    if (en_log_w - v->log <= EN_NLOG && EN_LOG[v->log % EN_NLOG].off == 0u)
        EN_LOG[v->log % EN_NLOG].off = en_now_ms ? en_now_ms : 1u;
}
static void damp(pv_t *v, float k)
{
    v->env_t = 0.0f;
    v->env_k = k;
    if (!v->damped)
        log_off(v);
    v->damped = 1;
}

/* the mechanics: the hammer's knock, the key's and the dampers' noise, the pedal's thump */
static struct {
    float kamp, kph, kinc, kdec, namp, ndec, nlp, na, pan;
} mech;
static void knock(float amp, float hz, float dec_s, float namp, float ndec_s, float nfc)
{
    float n = pf(P_NOISE);
    mech.kamp += amp * n;
    mech.kinc = hz / EN_SR;
    mech.kdec = tau(dec_s);
    mech.namp += namp * n;
    mech.ndec = tau(ndec_s);
    mech.na = onepole(nfc);
}

static void key_on(int note, float vel, int32_t gate, int player)
{
    pv_t *v = 0;
    int i, m, nm;
    float f0, b, t1, fc, a, felt = pf(P_FELT), age = pf(P_AGE), lowest = 1e9f, det, uni;
    static const uint8_t PN[NM] = {1, 1, 2, 2, 3, 4, 5, 6};
    while (note < 21)
        note += 12;
    while (note > 103)
        note -= 12;
    vel = fm_clampf(vel, 0.02f, 1.0f);
    for (i = 0; i < NV; i++)                     /* the same string struck again: the old sound let go, quickly */
        if (pv[i].on && pv[i].note == note && !pv[i].damped)
            damp(&pv[i], 1.0f - tau(0.04f));
    for (i = 0; i < NV; i++)                     /* a free voice, else the quietest (released ones first) */
        if (!pv[i].on) {
            v = &pv[i];
            break;
        }
    if (!v)
        for (i = 0; i < NV; i++) {
            float l = pv[i].lvl[0] * pv[i].env * (pv[i].gate == 0 ? 0.5f : 1.0f) * (pv[i].player ? 4.0f : 1.0f);
            if (l < lowest) {
                lowest = l;
                v = &pv[i];
            }
        }
    if (v->on && !v->damped)
        log_off(v);
    /* the string: its pitch (an old upright's a little off), its stretch, how long it rings */
    det = (frand() - 0.5f) * age * 0.16f;        /* semitones: +-8 cents at Age 100 */
    uni = (0.006f + 0.012f * frand()) * (1.0f + 4.0f * age);   /* the unison's spread, semitones */
    f0 = mtof((float)note + det);
    b = fm_clampf(2.5e-4f * fm_exp2f(((float)note - 60.0f) * (1.0f / 14.0f)), 6e-5f, 5e-3f);
    t1 = fm_clampf(15.0f * fm_exp2f(-((float)note - 33.0f) * (1.0f / 17.0f)), 0.9f, 16.0f) * (1.0f - 0.2f * felt);
    fc = (850.0f + 3400.0f * (1.0f - felt)) * (0.4f + 0.6f * vel);
    fc = fm_maxf(fc, f0 * 2.3f);
    a = 0.03f + 0.97f * fm_powf(vel, 1.7f);
    nm = note < 60 ? 8 : note < 76 ? 7 : note < 88 ? 5 : 3;
    for (m = 0; m < nm; m++) {
        int n = PN[m], second = (m == 1 || m == 3);
        float fn = (float)n * f0 * fm_sqrtf(1.0f + b * (float)(n * n)), amp, t60, r, w, s;
        if (second)
            fn *= fm_exp2f(uni * (1.0f / 12.0f));
        if (fn > 9000.0f) {
            nm = m;
            break;
        }
        amp = fm_powf((float)n, -0.7f) / (1.0f + (fn / fc) * (fn / fc));
        if (n == 1)
            amp *= fm_clampf(f0 * (1.0f / 120.0f), 0.35f, 1.0f);   /* a bass string's fundamental is weak */
        if (n <= 2)
            amp *= second ? 0.4f : 0.6f;
        t60 = t1 / (1.0f + 0.55f * (float)(n - 1)) * (second ? 2.3f : 1.0f);
        r = fm_expf(-6.9078f / (t60 * EN_SR));
        w = FM_TWO_PI * fn / EN_SR;
        s = fm_sinf(w * 0.5f);
        v->c[m] = 2.0f * r * (1.0f - 2.0f * s * s);
        v->r2[m] = r * r;
        v->y1[m] = a * amp * fm_sinf(w);
        v->y2[m] = 0.0f;
        v->lvl[m] = a * amp;
        v->rb[m] = fm_expf(-6.9078f * (float)BLK / (t60 * EN_SR));
    }
    v->nm = (uint8_t)nm;
    v->env = 0.0f;
    v->env_t = 1.0f;
    v->env_k = 1.0f - tau(0.0012f + 0.0055f * felt + 0.003f * (1.0f - vel));
    v->damp_k = 1.0f - tau(fm_clampf(0.06f + 0.3f * (72.0f - (float)note) / 36.0f, 0.06f, 0.4f) * (1.0f + age));
    {
        float p = fm_clampf(((float)note - 62.0f) * (1.0f / 36.0f), -0.75f, 0.75f) * pf(P_WIDTH);
        v->gl = 0.72f * (1.0f - p);
        v->gr = 0.72f * (1.0f + p);
    }
    v->gate = gate;
    v->note = (uint8_t)note;
    v->on = 1;
    v->player = (uint8_t)player;
    v->nodamp = note >= 89;                      /* the top strings have no dampers */
    v->damped = 0;
    v->age = pv_clock++;
    v->cnt = 0;
    {                                            /* the note into the screen's log */
        volatile en_logn_t *l = &EN_LOG[en_log_w % EN_NLOG];
        l->on = en_now_ms ? en_now_ms : 1u;
        l->off = 0u;
        l->tau = (uint16_t)fm_minf(t1 * (1000.0f / 6.9078f) * 2.3f, 60000.0f);
        l->note = (uint8_t)note;
        l->vel = (uint8_t)(vel * 127.0f);
        v->log = en_log_w;
        BARRIER();
        en_log_w = en_log_w + 1u;
    }
    knock(vel * (0.10f + 0.35f * felt), 70.0f + (float)(note - 21) * 0.6f, 0.035f, vel * (0.05f + 0.10f * felt),
          0.007f, 900.0f + 1200.0f * (1.0f - felt));
    mech.pan = fm_clampf(((float)note - 62.0f) * (1.0f / 40.0f), -0.6f, 0.6f);
    en_notes_played++;
}
static void key_up(pv_t *v)
{
    v->gate = 0;
    if (!ped && !v->nodamp) {
        damp(v, v->damp_k);
        knock(0.0f, 60.0f, 0.02f, 0.012f, 0.004f, 1800.0f);   /* the damper settling */
    }
}
static void key_off_player(int note)
{
    int i;
    for (i = 0; i < NV; i++)
        if (pv[i].on && pv[i].player && pv[i].note == note && pv[i].gate < 0)
            key_up(&pv[i]);
}
static void pedal(int down)
{
    int i;
    if (down == ped)
        return;
    ped = down;
    en_pedal = (uint8_t)down;
    {                                            /* the log: down at even entries, up at odd */
        uint32_t w = en_ped_w;
        if ((w & 1u) != (down ? 0u : 1u))
            w++;
        EN_PED[w % EN_NPED] = en_now_ms ? en_now_ms : 1u;
        BARRIER();
        en_ped_w = w + 1u;
    }
    if (down) {
        knock(0.05f, 48.0f, 0.12f, 0.025f, 0.08f, 700.0f);   /* the dampers lift: a soft thump, a breath */
        for (i = 0; i < NV; i++)                 /* those still falling are caught */
            if (pv[i].on && pv[i].damped && pv[i].env > 0.05f) {
                pv[i].env_t = pv[i].env;
                pv[i].env_k = 0.0f;
            }
    } else {
        knock(0.035f, 62.0f, 0.06f, 0.018f, 0.03f, 900.0f);
        for (i = 0; i < NV; i++)
            if (pv[i].on && pv[i].gate == 0 && !pv[i].nodamp)
                damp(&pv[i], pv[i].damp_k);
    }
}

/* ------------------------------------------------- sympathetic strings --- */
/* resonators tuned to the harmony's notes, driven by the piano: the pedal's halo (long with it down) */
#define NS 6
static struct {
    float y1, y2, x1, x2, c, r2, g;
} ss[NS];
static float ss_dec = 0.3f;
static void ss_tune(uint16_t pcs)
{
    int i = 0, n;
    float t60 = ped ? 3.2f : 0.35f;
    for (n = 43; n < 75 && i < NS; n++)
        if (pcs >> (n % 12) & 1u) {
            float w = FM_TWO_PI * mtof((float)n) / EN_SR, r = fm_expf(-6.9078f / (t60 * EN_SR)), s = fm_sinf(w * 0.5f);
            ss[i].c = 2.0f * r * (1.0f - 2.0f * s * s);
            ss[i].r2 = r * r;
            ss[i].g = (1.0f - r * r) * 0.5f;
            i++;
        }
    for (; i < NS; i++)
        ss[i].c = ss[i].r2 = ss[i].g = 0.0f;
    ss_dec = t60;
}

/* -------------------------------------------------------------- spaces --- */
/* the room: four delay lines, a Householder mix, damped; three diffusers before */
#define RV0 1361
#define RV1 1693
#define RV2 2087
#define RV3 2423
#define AP0 223
#define AP1 337
#define AP2 479
static float rv_buf[RV0 + RV1 + RV2 + RV3 + AP0 + AP1 + AP2] EN_POOL;
static float *rv_d[4], *ap_d[3];
static const int RV_N[4] = {RV0, RV1, RV2, RV3}, AP_N[3] = {AP0, AP1, AP2};
static int rv_p[4], ap_p[3];
static float rv_lp[4];
/* an old upright's tape: the bus through a slowly moving delay */
#define WB_N 1024
static float wb_l[WB_N] EN_POOL, wb_r[WB_N] EN_POOL;
static int wb_w;
static float wow_ph, flut_ph;

/* -------------------------------------------------------- the sequence --- */
#define NEV 96
enum { E_KEY, E_PEDDN, E_PEDUP, E_CHORD };
typedef struct {
    int32_t wait, gate;
    float vel;
    uint16_t pcs;
    uint8_t type, note, used, root;
} ev_t;
static ev_t evq[NEV];
static int ev_n;

#define TPB 12                                   /* ticks a beat */
static int cur_bar;                              /* (the tests' ear) */
static void sched(int type, int32_t wait, int note, float vel, int32_t gate, uint16_t pcs, int root)
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
            evq[i].pcs = pcs;
            evq[i].root = (uint8_t)root;
            ev_n++;
            return;
        }
}

/* ------------------------------------------------------------ the piece --- */
static struct {
    int idiom, var, reg, mode, gp;               /* gp: the Gymnopedie's pair, the Nocturne's major/minor */
    uint16_t scale;                              /* pitch classes (absolute) */
    int meter, tbar;                             /* beats a bar, ticks a bar */
    float spt;                                   /* samples a tick (at the piece's tempo) */
    int bar, nbars, intro, coda;                 /* bars: the coda starts at coda; the piece ends at nbars */
    int cyc;                                     /* bars a cycle (Gymnopedie 16, Nocturne 24, ...) */
    float drift;                                 /* the tempo's slow wander */
    /* the harmony of this bar: up to two chords (the second from tick at2) */
    ch_t ch[2];
    int at2;
    uint16_t last_pcs;
    int lh[4], nlh;                              /* the left hand's last chord (voice leading) */
    /* the melody */
    int mlo, mhi, mprev, mprev2, mleap, mres;
    int theme_r[4], theme_n[4][5];               /* a remembered phrase: its rhythms, its notes */
    int theme_ok;
    int shape;
    float centre;
    /* Nocturne: the phrases chosen */
    const nphr_t *ant, *con, *bph;
    /* Ostinato: the cell (four notes, low to high), its figure, the bass */
    int cell[4], cell_prev[4], fig, every, broot, homing;
    /* Airport: the loops */
    int nloop;
    float lp_next[5], lp_per[5];
    int lp_mot[5], lp_oct[5];
    float lp_vel[5], beat_at;
    int lp_stop[5];
    /* Modal: the motif */
    int motif_r, motif_n[5], motif_ok;
    int ended;
    float last_rit;
    /* 1.1 */
    int blue;                                    /* Stride: blue graces in the melody */
    int rh[3];                                   /* Minimalist: the right hand's chord */
    int hp[5], hv[14][4], satb[4], has_satb, htr, nverse;   /* Chorale: phrases, parts, transposition, verses */
    ch_t hlast;
    int sai, sbi, flen, form[4], proot;          /* Stride: the A and B, the song's length and letters */
    const int8_t *pl_pent;                       /* Planing: the pentatonic; the shape, the B's bass; units */
    int pl_base, pl_lo, pl_main, pl_alt, pl_nu, pl_a1, pl_nb, pl_wt, pl_bass;
    int pl_n, pl_on[12], pl_du[12], pl_top[12], pl_sh[12];
    int pl_an, pl_aat[6], pl_adu[6], pl_anote[6];
    const nphr_t *lb;                            /* Lullaby: its middle phrase */
    int bl_form, bl_T, bl_nch, bl_la, bl_lb, bl_up, bl_prev, bl_st;   /* Blues: (the next call, chosen early; a stab) */
} pc;

static int scale_has(int n) { return (pc.scale >> ((n % 12 + 12) % 12)) & 1; }
static uint16_t ch_pcs(ch_t c)
{
    uint16_t m = 0;
    int i;
    for (i = 0; i < CT_N[c.t]; i++)
        m |= (uint16_t)(1u << ((en_key + c.root + CT_IV[c.t][i]) % 12));
    return m;
}
static int ch_rootpc(ch_t c) { return (en_key + c.root) % 12; }
/* the scale with the chord's own notes in it (V's raised seventh in minor, and so on) */
static uint16_t eff_scale(uint16_t chord)
{
    uint16_t s = pc.scale;
    int p;
    for (p = 0; p < 12; p++)
        if ((chord >> p & 1u) && !(s >> p & 1u)) {
            if (s >> ((p + 11) % 12) & 1u)       /* a raised note (V's leading note): its natural out */
                s &= (uint16_t)~(1u << ((p + 11) % 12));
            else
                s &= (uint16_t)~(1u << ((p + 1) % 12));
            s |= (uint16_t)(1u << p);
        }
    return s;
}

/* the bar's time: samples from its start to a tick, the bar slowing through (rit) */
static float bar_spt, bar_rit;
static int32_t at_tick(float t) { return (int32_t)((t + bar_rit * t * t / (2.0f * (float)pc.tbar)) * bar_spt); }
static int32_t dur(float from, float len) { return at_tick(from + len) - at_tick(from); }
static int32_t hum(float ms) { return (int32_t)((frand() * 2.0f - 1.0f) * ms * 44.1f * (0.3f + pf(P_RUBATO))); }

/* loudness: lo .. hi by Dynamics, x 0..1 the music's level here */
static float loud(float x)
{
    float d = pf(P_DYN), lo = 0.16f + 0.08f * (1.0f - d), hi = lo + 0.12f + 0.55f * d;
    return fm_clampf(lo + (hi - lo) * x, 0.04f, 1.0f) * (0.94f + 0.12f * frand());
}

static void note_at(float tick, int note, float vel, float len_ticks, int role, int32_t extra)
{
    int32_t a = at_tick(tick) + extra, g = dur(tick, len_ticks);
#ifdef EN_HOST
    if (en_dbg_note)
        en_dbg_note(cur_bar, (int)tick, note, vel, role, (int)len_ticks);
#endif
    (void)role;
    if (par[P_PEDAL] < 15)
        g += (int32_t)(0.06f * EN_SR);           /* no pedal: a fuller legato */
    sched(E_KEY, a, note, vel, g < 400 ? 400 : g, 0, 0);
}
static void chord_at(float tick, ch_t c)
{
    uint16_t m = ch_pcs(c);
#ifdef EN_HOST
    if (en_dbg_chord)
        en_dbg_chord(cur_bar, (int)tick, ch_rootpc(c), m);
#endif
    sched(E_CHORD, at_tick(tick), 0, 0.0f, 0, m, ch_rootpc(c));
}
static void chord_set_at(float tick, uint16_t m, int root)
{
#ifdef EN_HOST
    if (en_dbg_chord)
        en_dbg_chord(cur_bar, (int)tick, root, m);
#endif
    sched(E_CHORD, at_tick(tick), 0, 0.0f, 0, m, root);
}
/* the pedal changed with the harmony: up as the new bass sounds, down again a moment later (legato pedalling) */
static int ped_skip;
static void ped_change(float tick, int force)
{
    int32_t a = at_tick(tick);
    if (par[P_PEDAL] < 15)
        return;
    if (!force && par[P_PEDAL] > 85 && (ped_skip++ & 1))   /* a heavy foot: every other change */
        return;
    sched(E_PEDUP, a + (int32_t)(0.012f * EN_SR), 0, 0.0f, 0, 0, 0);
    sched(E_PEDDN, a + (int32_t)((0.09f + 0.06f * frand()) * EN_SR), 0, 0.0f, 0, 0, 0);
}

/* voice leading: these pitch classes as a close chord in lo..hi, moving least from the last */
static void voice_lead(const int *pcs, int n, int lo, int hi, int *out)
{
    int r, j, best = 1 << 30, prevm = 0, tmp[5];
    for (j = 0; j < pc.nlh; j++)
        prevm += pc.lh[j];
    prevm = pc.nlh ? prevm / pc.nlh : (lo + hi) / 2;
    for (r = 0; r < n; r++) {
        int base = lo + ((pcs[r] - lo) % 12 + 12) % 12, oct;
        for (oct = 0; oct < 2; oct++) {
            int d = 0, m = 0;
            tmp[0] = base + 12 * oct;
            for (j = 1; j < n; j++) {
                int x = tmp[j - 1] + ((pcs[(r + j) % n] - tmp[j - 1]) % 12 + 12) % 12;
                if (x == tmp[j - 1])
                    x += 12;
                tmp[j] = x;
            }
            for (j = 0; j < n; j++) {
                int k, e = 99;
                m += tmp[j];
                for (k = 0; k < pc.nlh; k++) {
                    int dd = tmp[j] - pc.lh[k];
                    dd = dd < 0 ? -dd : dd;
                    e = dd < e ? dd : e;
                }
                d += pc.nlh ? e : 0;
            }
            m /= n;
            d += 2 * (m > prevm ? m - prevm : prevm - m) / 3;
            if (tmp[n - 1] > hi)
                d += 30 + 4 * (tmp[n - 1] - hi);
            for (j = 1; j < n; j++)
                if (tmp[j] - tmp[j - 1] == 1)    /* no seconds rubbing in the middle */
                    d += 12;
            if (d < best) {
                best = d;
                for (j = 0; j < n; j++)
                    out[j] = tmp[j];
            }
        }
    }
    for (j = 0; j < n; j++)
        pc.lh[j] = out[j];
    pc.nlh = n;
}
static int bass_note(ch_t c, int lo)            /* the chord's root in lo .. lo+11 */
{
    return lo + ((ch_rootpc(c) - lo) % 12 + 12) % 12;
}

/* --------------------------------------------------------- the melody --- */
/* a note for this moment: near the last (steps above all, a leap turned back), on the chord's notes where it
 * leans (strong beats, long notes, phrase ends), passing notes between, never a semitone above a chord note held
 * long; toward the phrase's contour. end: 1 a chord note, 2 home (the root, else the third) */
static int mel_pick(uint16_t chord, int root, int strong, int longn, float target, int end)
{
    float w[40], tot = 0.0f, r;
    int n, k = 0, cand[40], ch_char = MODE_CHAR[pc.mode];
    uint16_t sc = eff_scale(chord);
    for (n = pc.mlo; n <= pc.mhi && k < 40; n++) {
        int pcl = n % 12, iv = n - pc.mprev, a = iv < 0 ? -iv : iv, in = chord >> pcl & 1u;
        int above = !in && (chord >> ((pcl + 11) % 12) & 1u);   /* a semitone over a chord note */
        float x;
        if (!(sc >> pcl & 1u))
            continue;
        x = a == 0 ? 0.35f : a <= 2 ? 3.0f : a <= 4 ? 1.5f : a == 5 ? 0.7f : a == 7 ? 0.5f : a <= 9 && a != 6 ? 0.12f
            : a == 12 ? 0.08f : 0.0f;
        if (pc.mleap >= 5 || pc.mleap <= -5) {   /* after a leap: back by step */
            if (iv != 0 && (iv > 0) == (pc.mleap > 0))
                x *= 0.08f;
            else if (a <= 2 && a > 0)
                x *= 2.5f;
        }
        if (pc.mres && (a == 0 || a > 2))        /* a passing note goes on by step */
            x *= 0.06f;
        if (in)
            x *= strong ? 3.0f : 1.3f;
        else if (pcl == (root + 2) % 12 && !above)
            x *= strong ? 1.0f : 1.1f;           /* the ninth: a colour */
        else if (above)
            x *= (strong || longn) ? 0.015f : 0.35f;
        else
            x *= strong ? 0.08f : longn ? 0.2f : 1.0f;
        if (ch_char >= 0 && pcl == (en_key + ch_char) % 12 && !above)
            x *= strong ? 1.2f : 1.8f;           /* the mode's colour note */
        if (end == 1 && !in)
            x = 0.0f;
        if (end == 2)
            x *= pcl == root ? 4.0f : in && (pcl == (root + 3) % 12 || pcl == (root + 4) % 12) ? 1.5f : in ? 0.4f : 0.0f;
        if (n == pc.mprev && pc.mprev == pc.mprev2)
            x *= 0.05f;
        else if (n == pc.mprev2 && n != pc.mprev)
            x *= 0.4f;                           /* not back and forth: somewhere */
        {
            float d = ((float)n - target) * 0.25f;
            x *= 1.0f / (1.0f + d * d);
        }
        if (x <= 0.0f)
            continue;
        x *= x;
        cand[k] = n;
        w[k++] = x;
        tot += x;
    }
    if (!k || tot <= 0.0f) {                     /* nothing fits: the nearest chord note */
        int best = pc.mprev, bd = 99;
        for (n = pc.mlo; n <= pc.mhi; n++)
            if ((chord >> (n % 12) & 1u) && (n - pc.mprev < 0 ? pc.mprev - n : n - pc.mprev) < bd) {
                bd = n - pc.mprev < 0 ? pc.mprev - n : n - pc.mprev;
                best = n;
            }
        n = best;
    } else {
        r = trand() * tot;
        n = cand[k - 1];
        for (k = 0; k < 40; k++) {
            r -= w[k];
            if (r <= 0.0f) {
                n = cand[k];
                break;
            }
        }
    }
    pc.mleap = n - pc.mprev;
    pc.mprev2 = pc.mprev;
    pc.mprev = n;
    {
        int pcl = n % 12;
        pc.mres = !(chord >> pcl & 1u) && pcl != (root + 2) % 12;
    }
    return n;
}

static float contour(float pos)                  /* -1 .. 1 over a phrase */
{
    switch (pc.shape) {
    case 0: return -0.5f + 1.3f * fm_sinf(FM_PI * pos);      /* an arch */
    case 1: return 0.8f - 1.5f * pos;                        /* falling */
    default: return -0.6f + 1.4f * fm_sinf(FM_PI * fm_minf(pos * 1.3f, 1.0f)) - 0.3f * pos;   /* rising late */
    }
}

/* a bar's rhythm, by Density: a few notes or more, an ending long */
static int pick_rhy(const rhy_t *R, int nr, int last, int sparse)
{
    float w[12], tot = 0.0f, want = 1.2f + 2.6f * pf(P_DENSITY) - (float)sparse, r;
    int i;
    for (i = 0; i < nr; i++) {
        float d = (float)R[i].n - want;
        int ln = R[i].du[R[i].n - 1];
        w[i] = fm_expf(-d * d * 0.5f);
        if (last && (ln < 24 || R[i].n > 3))
            w[i] = 0.0f;
        if (last && R[i].n == 1)
            w[i] += 0.5f;
        tot += w[i];
    }
    r = trand() * tot;
    for (i = 0; i < nr; i++) {
        r -= w[i];
        if (r <= 0.0f)
            return i;
    }
    return 0;
}
static ch_t bar_ch(int tick) { return (pc.at2 > 0 && tick >= pc.at2) ? pc.ch[1] : pc.ch[0]; }

/* one bar of melody: new (theme kept in slot bip), or the theme again (refitted), or the theme with a new end.
 * mode 0 new (kept as the theme), 1 replay, 2 replay but a new cadence, 3 new (not kept); end 0, 1 half close,
 * 2 home */
static void melody_bar(const rhy_t *R, int nr, int bip, int plen, int mode, int end, float x, int32_t lag)
{
    int ri, i;
    const rhy_t *rh;
    int replay = (mode == 1 || (mode == 2 && bip < plen / 2)) && pc.theme_ok;
    ri = replay ? pc.theme_r[bip] : pick_rhy(R, nr, end != 0, 0);
    if (end == 2 && mode == 3 && bip == plen - 1)   /* the piece's last word: one long note */
        for (i = 0; i < nr; i++)
            if (R[i].n == 1)
                ri = i;
    rh = &R[ri];
    if (!replay && mode == 0 && bip < 4)
        pc.theme_r[bip] = ri;
    for (i = 0; i < rh->n; i++) {
        /* (6/8, a bar of 24 ticks: both its beats lean) */
        int on = rh->on[i], du = rh->du[i], strong = on % (pc.tbar == 36 ? 36 : pc.tbar == 24 ? 12 : 24) == 0 || du >= 24;
        ch_t c = bar_ch(on);
        int last = end && i == rh->n - 1, n;
        float pos = ((float)(bip * pc.tbar + on)) / (float)(plen * pc.tbar);
        float target = pc.centre + 6.0f * contour(pos);
        if (replay) {
            n = pc.theme_n[bip][i];
            if ((strong || du >= 18) && !(ch_pcs(c) >> (n % 12) & 1u) && n % 12 != (ch_rootpc(c) + 2) % 12) {
                pc.mprev = n;                    /* refit: the nearest note that fits, near the old */
                pc.mleap = 0;
                pc.mres = 0;
                n = mel_pick(ch_pcs(c), ch_rootpc(c), 1, 1, (float)n, last ? end : 0);
            } else {
                pc.mleap = n - pc.mprev;
                pc.mprev2 = pc.mprev;
                pc.mprev = n;
                pc.mres = 0;
            }
        } else {
            n = mel_pick(ch_pcs(c), ch_rootpc(c), strong, du >= 18, target, last ? end : 0);
            if (mode == 0 && bip < 4)
                pc.theme_n[bip][i] = n;
        }
        {
            float acc = (on == 0 ? 1.06f : 1.0f) * (du >= 24 ? 1.04f : 0.97f);
            float v = loud(x) * acc;
            if (pc.blue) {                       /* Stride: a blue note crushed into a long major third */
                int32_t h = lag + hum(10.0f);
                if (du >= 12 && CT_IV[c.t][1] == 4 && n % 12 == (ch_rootpc(c) + 4) % 12 && trand() < 0.35f)
                    note_at((float)on - 0.6f, n - 1, v * 0.7f, 0.6f, R_MEL, h);
                note_at((float)on, n, v, (float)du * 0.98f, R_MEL, h);
            } else {
                note_at((float)on, n, v, (float)du * 1.02f, R_MEL, lag + hum(12.0f));
            }
        }
    }
    if (mode == 0 && bip == plen - 1)
        pc.theme_ok = 1;
}

/* the phrase's push and pull: a bar's tempo (x the base), and the slowing through its last bar */
static float bar_tempo(int bip, int plen, float *rit)
{
    float r = pf(P_RUBATO), f;
    static const float PUSH[4] = {0.01f, 0.05f, 0.02f, -0.06f};
    f = 1.0f + r * PUSH[(bip * 4 / (plen > 0 ? plen : 4)) & 3] + pc.drift;
    *rit = bip == plen - 1 ? r * 0.16f : 0.0f;
    return f;
}

/* ------------------------------------------------------- the idioms --- */
static int phrase_x_tbl(int bip) { static const int X[4] = {50, 62, 72, 55}; return X[bip & 3]; }

/* Gymnopedie: bars of 3/4; a phrase of four. Cycle: A A' B A'' (16 bars); the theme anew each other cycle */
static void gym_bar(int b)
{
    const gpair_t *g = &GPAIRS[pc.gp];
    int body = b - pc.intro, pin = (body % 16) / 4, bip = body % 4, i, up[4], pcs[5], n = 0;
    float x, rit;
    ch_t c;
    int coda = b >= pc.coda, last = b == pc.nbars - 1;
    if (b < pc.intro)
        c = (b & 1) ? g->b : g->a;
    else if (coda)                               /* the close: to the tonic from whichever is not it */
        c = last ? g->home : (g->a.root == g->home.root ? g->b : g->a);
    else if (pin == 2)                           /* the middle: two others, then back */
        c = bip == 0 ? g->c : bip == 1 ? g->d : bip == 2 ? g->a : g->b;
    else
        c = (bip & 1) ? g->b : g->a;
    pc.ch[0] = c;
    pc.at2 = -1;
    bar_spt = pc.spt / bar_tempo(b < pc.intro ? 0 : coda ? 3 : bip, 4, &rit);
    if (coda)
        rit = 0.15f + 0.25f * pf(P_RUBATO) * (last ? 1.4f : 0.5f);
    bar_rit = rit;
    x = b < pc.intro ? 0.35f : coda ? 0.4f : (pin == 2 ? 0.8f : 0.55f) * (float)phrase_x_tbl(bip) * 0.016f;
    chord_at(0.0f, c);
    ped_change(0.0f, last);
    /* the bass on one, the chord on two held (Satie's) */
    note_at(0.0f, bass_note(c, 38), loud(x) * 0.82f, 36.0f, R_BASS, hum(6.0f));
    for (i = 1; i < CT_N[c.t] && n < 3; i++)
        pcs[n++] = (en_key + c.root + CT_IV[c.t][i]) % 12;
    if (n < 3)
        pcs[n++] = ch_rootpc(c);
    voice_lead(pcs, n, 52, 69, up);
    for (i = 0; i < n; i++)
        note_at(12.0f, up[i], loud(x) * (0.52f + 0.04f * (float)i), last ? 36.0f : 24.0f, R_CHORD,
                hum(5.0f) + (int32_t)(i * 0.008f * EN_SR * pf(P_RUBATO)));
    if (last) {                                  /* the end: the low tonic, an octave under */
        if (bass_note(c, 38) - 12 >= 28)
            note_at(0.0f, bass_note(c, 38) - 12, loud(0.3f) * 0.6f, 36.0f, R_BASS, hum(6.0f) + 400);
    }
    /* the melody: from the intro's end; A, A' (theme, new end), B (new, higher), A'' (theme home) */
    if (b >= pc.intro) {
        int mode, end;
        int32_t lag = (int32_t)(0.012f * EN_SR);
        if (coda) {                              /* the close: two bars falling home */
            if (b == pc.coda) {
                pc.centre = (float)(pc.mlo + pc.mhi) * 0.5f - 2.0f;
                pc.shape = 1;
            }
            melody_bar(R34, NA(R34), last ? 3 : 2, 4, 3, last ? 2 : 0, 0.42f, lag);
            return;
        }
        if (bip == 0) {
            if (pin == 0)
                pc.theme_ok = 0;                 /* a new theme each cycle */
            pc.shape = pin == 2 ? 0 : tirand(3);
            pc.centre = (float)(pc.mlo + pc.mhi) * 0.5f + (pin == 2 ? 3.0f : -1.0f);
        }
        mode = pin == 2 ? 3 : pin == 1 ? 2 : 1;
        if (!pc.theme_ok && mode != 3)
            mode = 0;
        end = bip == 3 ? (pin == 3 ? 2 : 1) : 0;
        melody_bar(R34, NA(R34), bip, 4, mode, end, x, lag);
    }
}

/* Nocturne: periods of eight bars (antecedent to V, consequent home); A, B, A again; a coda */
static const nphr_t *nph_for(int pin)
{
    return pin == 0 || pin == 4 ? pc.ant : pin == 1 || pin == 5 ? pc.con : pc.bph;
}
static void noct_bar(int b)
{
    int body = b - pc.intro, pin = (body % 24) / 4, bip = body % 4, i, j, nf;
    int coda = b >= pc.coda, last = b == pc.nbars - 1, minor = pc.gp;
    const int8_t *fig = pc.var == 0 ? LH_128 : pc.var == 1 ? LH_34 : LH_44;
    float x, rit, step;
    const nphr_t *ph;
    if (b < pc.intro) {
        pc.ch[0] = (ch_t){0, (uint8_t)(minor ? T_MIN : T_MAJ)};
        pc.at2 = -1;
    } else if (coda) {
        int k = b - pc.coda;
        static const ch_t CM[3] = {{5, T_MAJ}, {7, T_DOM7}, {0, T_MAJ}}, Cm[3] = {{5, T_MIN}, {7, T_DOM7}, {0, T_MIN}};
        pc.ch[0] = minor ? Cm[k > 2 ? 2 : k] : CM[k > 2 ? 2 : k];
        if (last)
            pc.ch[0] = minor ? Cm[2] : CM[2];
        pc.at2 = -1;
    } else {
        ph = nph_for(pin);
        pc.ch[0] = ph->c[bip][0];
        pc.at2 = ph->c[bip][1].root == 99 ? -1 : pc.tbar / 2;
        if (pc.at2 > 0)
            pc.ch[1] = ph->c[bip][1];
    }
    bar_spt = pc.spt / bar_tempo(b < pc.intro ? 0 : coda ? 3 : bip, 4, &rit);
    if (coda)
        rit = last ? 0.25f + 0.3f * pf(P_RUBATO) : 0.06f;
    bar_rit = rit;
    x = b < pc.intro ? 0.3f : coda ? 0.38f : (pin == 2 || pin == 3 ? 0.78f : 0.58f) * (float)phrase_x_tbl(bip) * 0.016f;
    /* the left hand's figure, per chord */
    nf = pc.var == 0 ? 12 : pc.var == 1 ? 6 : 8;
    step = (float)pc.tbar / (float)nf;
    for (j = 0; j < (pc.at2 > 0 ? 2 : 1); j++) {
        ch_t c = pc.ch[j];
        int lad[5], r0 = bass_note(c, 36), has7 = CT_N[c.t] >= 4 && CT_IV[c.t][3] < 12, from = j ? nf / 2 : 0;
        int to = pc.at2 > 0 && !j ? nf / 2 : nf;
        int third = CT_IV[c.t][1] == 2 ? 2 : CT_IV[c.t][1];
        lad[0] = r0;
        lad[1] = r0 + CT_IV[c.t][2];
        lad[2] = has7 ? r0 + CT_IV[c.t][3] : r0 + 12;
        lad[3] = r0 + 12 + third;
        lad[4] = r0 + 12 + CT_IV[c.t][2];
        chord_at((float)from * step, c);
        ped_change((float)from * step, last);
        if (last) {                              /* the end: the figure once, up into a held chord */
            for (i = 0; i < 4; i++)
                note_at((float)i * step * 0.75f, lad[i == 3 ? 4 : i], loud(0.32f) * (i ? 0.55f : 0.7f),
                        (float)pc.tbar * 1.5f, R_ARP, hum(5.0f));
            break;
        }
        for (i = from; i < to; i++) {
            int k = fig[i], grp = pc.var == 0 ? 3 : 2;
            float v = loud(x) * (i % grp == 0 ? 0.62f : 0.5f) * (k == 0 ? 1.15f : 1.0f);
            note_at((float)i * step, lad[k], v, step * (k == 0 ? (float)(to - i) : 1.6f), k == 0 ? R_BASS : R_ARP,
                    hum(6.0f));
        }
    }
    /* the right hand: the theme in A, its own in B; the coda's last word */
    if (b >= pc.intro) {
        const rhy_t *R = pc.var == 0 ? R128 : pc.var == 1 ? R34 : R44;
        int nr = pc.var == 0 ? NA(R128) : pc.var == 1 ? NA(R34) : NA(R44), mode, end;
        int32_t lag = (int32_t)(0.015f * EN_SR);
        if (coda) {                              /* IV, V, I: the last words */
            int k = b - pc.coda;
            if (k == 0) {
                pc.shape = 1;
                pc.centre = (float)(pc.mlo + pc.mhi) * 0.5f;
            }
            melody_bar(R, nr, k + 1 > 3 ? 3 : k + 1, 4, 3, last ? 2 : k == 1 ? 1 : 0, 0.4f, lag);
            return;
        }
        if (bip == 0) {
            pc.shape = (pin & 1) ? 1 : pin >= 2 && pin < 4 ? 0 : 2;
            pc.centre = (float)(pc.mlo + pc.mhi) * 0.5f + (pin == 2 || pin == 3 ? 2.0f : -1.0f);
        }
        end = bip == 3 ? ((pin & 1) ? 2 : 1) : 0;
        if (pin == 2 || pin == 3) {              /* B: new each time, the theme kept */
            int keep_ok = pc.theme_ok;
            pc.theme_ok = 0;
            melody_bar(R, nr, bip, 4, 3, end, x, lag);
            pc.theme_ok = keep_ok;
        } else {
            mode = pin == 0 || pin == 4 ? 1 : 2;
            if (!pc.theme_ok)
                mode = 0;
            melody_bar(R, nr, bip, 4, mode, end, x, lag);
        }
    }
}

/* Modal: two-bar chords (a pair, now and then the other two), an open left hand rolled; the right hand in short
 * motifs, answered, then a breath */
static void modal_bar(int b)
{
    const mpair_t *mp = &MPAIRS[pc.mode];
    int body = b - pc.intro, unit = body / 4, bin = body % 4, last = b == pc.nbars - 1, coda = b >= pc.coda, i;
    int mid = (unit % 3) == 2 && !coda;
    float x, rit;
    ch_t c;
    if (b < pc.intro || coda)
        c = (last || b < pc.intro || ((b - pc.coda) / 2 & 1)) ? mp->a : mp->b;
    else
        c = bin < 2 ? (mid ? mp->c : mp->a) : (mid ? mp->d : mp->b);
    if (coda && b == pc.nbars - 2)
        c = mp->b;
    pc.ch[0] = c;
    pc.at2 = -1;
    bar_spt = pc.spt / bar_tempo(b < pc.intro ? 0 : bin, 4, &rit);
    if (coda)
        rit = last ? 0.3f + 0.3f * pf(P_RUBATO) : 0.08f;
    bar_rit = rit;
    x = 0.5f + 0.15f * fm_sinf(FM_PI * (float)((body % 12 + 12) % 12) / 12.0f);
    if ((body & 1) == 0 || b < pc.intro || last) {   /* a chord each two bars: root, fifth, ninth or tenth; rolled */
        int r0 = bass_note(c, pc.reg == 2 ? 33 : 36), lad[4], k;
        int third = CT_IV[c.t][1], sev = CT_N[c.t] >= 4 && CT_IV[c.t][3] < 12 ? CT_IV[c.t][3] : 0;
        int nine = CT_IV[c.t][CT_N[c.t] - 1] == 14;
        lad[0] = r0;                             /* open: 1 5 7 10; major 1 5 9 10; minor 1 5 10 (9 up high) */
        lad[1] = r0 + 7;
        lad[2] = sev ? r0 + sev : third == 4 ? r0 + 14 : r0 + 12 + third;
        lad[3] = sev ? r0 + 12 + third : third == 4 ? r0 + 16 : nine ? r0 + 26 : r0 + 19;
        chord_at(0.0f, c);
        ped_change(0.0f, last);
        for (k = 0; k < 4; k++)
            note_at((float)k * (1.0f + 2.0f * pf(P_RUBATO)), lad[k], loud(x * 0.8f) * (k ? 0.5f : 0.66f), (float)pc.tbar * 2.0f,
                    k ? R_CHORD : R_BASS, hum(4.0f));
    } else if (pf(P_DENSITY) > 0.55f && frand() < 0.5f) {   /* busier: the fifth again, on three */
        note_at(24.0f, bass_note(c, pc.reg == 2 ? 33 : 36) + 7, loud(x * 0.6f) * 0.45f, 24.0f, R_CHORD, hum(6.0f));
    }
    if (b < pc.intro)
        return;
    if (coda) {
        if (last || b == pc.coda) {
            pc.shape = 1;
            melody_bar(R44, NA(R44), 3, 4, 0, last ? 2 : 1, 0.38f, 0);
        }
        return;
    }
    /* the right hand: bar 0 the motif (the piece's, or a new one), bar 1 its answer (the motif a step or two off),
     * bar 2 sometimes more, bar 3 a breath */
    {
        float rest = 0.55f - 0.45f * pf(P_DENSITY);
        if (bin == 0) {
            pc.shape = tirand(3);
            pc.centre = (float)(pc.mlo + pc.mhi) * 0.5f + (float)(tirand(5) - 2);
            if (!pc.motif_ok || trand() < 0.45f) {          /* a new motif */
                int keep = pc.theme_ok;
                pc.theme_ok = 0;
                melody_bar(R44, NA(R44), 0, 2, 0, 0, x, 0);
                pc.motif_r = pc.theme_r[0];
                for (i = 0; i < 5; i++)
                    pc.motif_n[i] = pc.theme_n[0][i];
                pc.motif_ok = 1;
                pc.theme_ok = keep;
            } else {                             /* the motif again, a step or two off (a sequence) */
                pc.theme_r[0] = pc.motif_r;
                for (i = 0; i < 5; i++) {
                    int n = pc.motif_n[i], s = tirand(3) - 1, k;
                    for (k = 0; k < 4 && s; k++) {   /* move it s scale steps */
                        n += s;
                        while (!scale_has(n))
                            n += s;
                        s = 0;
                    }
                    pc.theme_n[0][i] = n < pc.mlo ? n + 12 : n > pc.mhi ? n - 12 : n;
                }
                pc.theme_ok = 1;
                melody_bar(R44, NA(R44), 0, 2, 1, 0, x, 0);
            }
        } else if (bin == 1) {
            melody_bar(R44, NA(R44), 1, 2, 0, 1, x * 0.95f, 0);
        } else if (bin == 2 && trand() > rest) {
            pc.shape = 1;
            melody_bar(R44, NA(R44), 1, 2, 0, 1, x * 0.85f, 0);
        }
    }
}

/* Ostinato: the cell's figure every bar; every few bars one of its notes moves a step, to the next good chord */
static const int8_t FIG8[4][8] = {{0, 2, 1, 3, 0, 2, 1, 3}, {0, 1, 2, 3, 4, 3, 2, 1}, {0, 2, 3, 2, 1, 2, 3, 2}, {0, 1, 3, 1, 2, 1, 3, 1}};
static const int8_t FIG6[3][6] = {{0, 1, 2, 3, 2, 1}, {0, 2, 3, 4, 3, 2}, {0, 2, 1, 3, 1, 2}};
static const float ROOTW[2][12] = {
    {1.0f, 0, 0.5f, 0, 0.3f, 0.9f, 0, 0.45f, 0, 0.85f, 0, 0},   /* major */
    {1.0f, 0, 0, 0.8f, 0, 0.7f, 0, 0.3f, 0.9f, 0, 0.75f, 0},   /* minor */
};
/* the chord a set of notes makes: its root (absolute), or -1. Major (3 4 7 9 11 2 over it), minor (3 7 10 2 5),
 * or sus (2 5 7 10: no third) */
static int cell_root(const int *cell, float *wout)
{
    uint16_t m = 0;
    int r, i, best = -1, minor = pc.mode == M_AEO || pc.mode == M_DOR;
    float bw = 0.0f;
    for (i = 0; i < 4; i++)
        m |= (uint16_t)(1u << (cell[i] % 12));
    for (r = 0; r < 12; r++) {
        int rel = (r - en_key + 12) % 12;
        uint16_t rm = (uint16_t)(((m >> r) | (m << (12 - r))) & 0xFFFu);   /* relative to r */
        float w = ROOTW[minor][rel];
        int ok = 0;
        if (w <= 0.0f || !scale_has(r) || !scale_has(r + 7))
            continue;
        if ((rm & (1u << 4)) && !(rm & ~(uint16_t)(1u | 1u << 2 | 1u << 4 | 1u << 7 | 1u << 9 | 1u << 11)))
            ok = 1;
        else if ((rm & (1u << 3)) && !(rm & ~(uint16_t)(1u | 1u << 2 | 1u << 3 | 1u << 5 | 1u << 7 | 1u << 10)))
            ok = 1;
        else if (!(rm & (1u << 3 | 1u << 4)) && (rm & (1u << 2 | 1u << 5)) &&
                 !(rm & ~(uint16_t)(1u | 1u << 2 | 1u << 5 | 1u << 7 | 1u << 10)) && !((rm & 1u << 2) && (rm & 1u << 5)))
            ok = 1, w *= 0.6f;
        if (!ok)
            continue;
        if (m & (1u << r))
            w *= 1.3f;
        if (w > bw) {
            bw = w;
            best = r;
        }
    }
    *wout = bw;
    return best;
}
static void ost_move(void)
{
    int cand[16][4], i, d, k = 0, j;
    float w[16], tot = 0.0f, r;
    static const int8_t DS[4] = {-2, -1, 1, 2};
    for (i = 0; i < 4; i++)
        for (d = 0; d < 4; d++) {
            int c[4], ok = 1, rt;
            float rw;
            for (j = 0; j < 4; j++)
                c[j] = pc.cell[j];
            c[i] += DS[d];
            if (!scale_has(c[i]) || c[i] < 50 || c[i] > 77)
                continue;
            for (j = 1; j < 4; j++)
                if (c[j] - c[j - 1] < 2)
                    ok = 0;
            if (!ok)
                continue;
            rt = cell_root(c, &rw);
            if (rt < 0)
                continue;
            {
                int same = 1;
                for (j = 0; j < 4; j++)
                    same &= c[j] == pc.cell_prev[j];
                if (same)
                    rw *= 0.15f;                 /* not straight back */
            }
            if (rt == pc.broot)
                rw *= 0.45f;
            if (pc.homing)
                rw *= rt == en_key ? 5.0f : 0.6f;
            for (j = 0; j < 4; j++)
                cand[k][j] = c[j];
            w[k] = rw * rw;
            tot += w[k++];
        }
    if (!k)
        return;
    r = trand() * tot;
    for (i = 0; i < k; i++) {
        r -= w[i];
        if (r <= 0.0f || i == k - 1) {
            for (j = 0; j < 4; j++) {
                pc.cell_prev[j] = pc.cell[j];
                pc.cell[j] = cand[i][j];
            }
            return;
        }
    }
}
static void ost_bar(int b)
{
    int body = b - pc.intro, i, last = b == pc.nbars - 1, coda = b >= pc.coda, n, rt, sixteen = pc.var == 1;
    float rw, x, arc, step, len = (float)(pc.coda - pc.intro);
    int lad[5];
    uint16_t m = 0;
    /* the arc: up to about two thirds, then down */
    arc = b < pc.intro ? 0.0f : fm_clampf((float)body / (len > 1.0f ? len : 1.0f), 0.0f, 1.0f);
    arc = arc < 0.62f ? arc / 0.62f : 1.0f - (arc - 0.62f) / 0.38f;
    x = 0.3f + 0.55f * arc;
    if (coda)
        x = 0.3f;
    pc.homing = b >= pc.coda - 6;
    if (body > 0 && body % pc.every == 0 && !coda)
        ost_move();
    rt = cell_root(pc.cell, &rw);
    if (rt < 0)
        rt = pc.broot;
    for (i = 0; i < 4; i++)
        m |= (uint16_t)(1u << (pc.cell[i] % 12));
    m |= (uint16_t)(1u << rt);
    pc.ch[0].root = (int8_t)((rt - en_key + 12) % 12);
    pc.ch[0].t = T_MAJ;
    pc.at2 = -1;
    bar_spt = pc.spt / (1.0f + pc.drift + pf(P_RUBATO) * 0.03f * fm_sinf(FM_PI * (float)(body % 8) / 8.0f));
    bar_rit = coda ? (last ? 0.25f + 0.3f * pf(P_RUBATO) : 0.04f * (float)(b - pc.coda + 1)) : 0.0f;
    if (rt != pc.broot || body % 4 == 0 || b < 1 || last) {   /* the bass: a new root, or each four bars */
        int bn = 36 + ((rt - 36) % 12 + 12) % 12;
        if (rt != pc.broot || b < 1 || last)
            ped_change(0.0f, last);
        chord_set_at(0.0f, m, rt);
        note_at(0.0f, bn, loud(x) * 0.7f, (float)pc.tbar * 2.0f, R_BASS, hum(4.0f));
        if (arc > 0.6f && pf(P_DENSITY) > 0.35f && !coda)
            note_at(0.0f, bn - 12 >= 28 ? bn - 12 : bn + 12, loud(x) * 0.45f, (float)pc.tbar * 2.0f, R_BASS, hum(4.0f) + 300);
        pc.broot = rt;
    }
    for (i = 0; i < 4; i++)
        lad[i] = pc.cell[i];
    lad[4] = pc.cell[1] + 12;
    if (last) {                                  /* the close: the cell rolled, held */
        for (i = 0; i < 5; i++)
            note_at((float)i * 2.0f, lad[i], loud(0.3f) * 0.55f, (float)pc.tbar * 2.0f, R_CHORD, hum(4.0f));
        return;
    }
    n = pc.var == 2 ? 6 : sixteen ? 16 : 8;
    step = (float)pc.tbar / (float)n;
    for (i = 0; i < n; i++) {
        int k = pc.var == 2 ? FIG6[pc.fig % 3][i] : FIG8[pc.fig & 3][i & 7];
        float v = loud(x) * (i % (sixteen ? 4 : 2) == 0 ? 0.6f : 0.48f);
        if (b < pc.intro && i >= n / 2 && b == 0)
            v *= 0.8f;
        if (coda && (i & 1) && b >= pc.nbars - 3)
            continue;                            /* thinning out */
        if (sixteen && (i & 1) && pf(P_DENSITY) < 0.4f && arc < 0.5f)
            continue;
        note_at((float)i * step, lad[k], v, step * 1.5f, R_ARP, hum(3.0f));
    }
    /* a voice above at the swell: long notes, a step at a time */
    if (!coda && arc > 0.45f && pf(P_DENSITY) > 0.3f && body % 2 == 0) {
        uint16_t cm = m;
        pc.mlo = pc.cell[3] + 3;
        pc.mhi = pc.cell[3] + 15;
        if (pc.mprev < pc.mlo || pc.mprev > pc.mhi)
            pc.mprev = pc.mprev2 = pc.cell[3] + 7;
        n = mel_pick(cm, rt, 1, 1, (float)(pc.cell[3] + 8), 1);
        note_at(0.0f, n, loud(x) * 0.95f, (float)pc.tbar * 2.0f, R_MEL, (int32_t)(0.015f * EN_SR) + hum(10.0f));
    }
}

/* Airport: the loops come in one by one, each at its own length, then leave one by one */
static void air_bar(int b)
{
    int i, k, all_stopped = 1;
    float b0 = (float)b * (float)pc.meter, b1 = b0 + (float)pc.meter;
    const int8_t *ps = PENT[pc.var % 3];
    int base = 60 + (pc.reg == 1 ? 12 : pc.reg == 2 ? -7 : 0);
    base += ((en_key - base) % 12 + 12) % 12;    /* the tonic nearest */
    if (base > 66 + (pc.reg == 1 ? 12 : pc.reg == 2 ? -7 : 0))
        base -= 12;
    bar_spt = pc.spt;
    bar_rit = 0.0f;
    if (b == 0) {
        uint16_t m = 0;
        for (i = 0; i < 5; i++)
            m |= (uint16_t)(1u << ((en_key + ps[i]) % 12));
        chord_set_at(0.0f, m, en_key);
        if (par[P_PEDAL] >= 15)
            sched(E_PEDDN, 400, 0, 0.0f, 0, 0, 0);
    } else if (par[P_PEDAL] >= 15 && par[P_PEDAL] < 60 && b % (par[P_PEDAL] < 35 ? 2 : 4) == 0 && b < pc.nbars) {
        ped_change(0.0f, 1);                     /* a lighter foot: the haze cleared now and then */
    }
    for (i = 0; i < pc.nloop; i++) {
        const amot_t *mo = &AMOT[pc.lp_mot[i]];
        while (pc.lp_next[i] < b1) {
            float at = pc.lp_next[i];
            if (b >= pc.nbars) {
                pc.lp_stop[i] = 1;
                break;
            }
            for (k = 0; k < mo->n; k++) {
                int st = mo->st[k], o = st < 0 ? -1 : st / 5, idx = ((st % 5) + 5) % 5;
                int note = base + pc.lp_oct[i] * 12 + o * 12 + ps[idx];
                float tick = (at - b0) * (float)TPB + (float)mo->at[k];
                if (note > 98)
                    note -= 12;
                note_at(tick, note, pc.lp_vel[i] * (0.9f + 0.15f * frand()), 4.0f * (float)TPB, R_LOOP, hum(8.0f));
            }
            pc.lp_next[i] += pc.lp_per[i];
        }
        all_stopped &= pc.lp_stop[i];
    }
    if (b >= pc.nbars && all_stopped)
        pc.ended = 1;
}

/* ------------------------------------------------- the idioms of 1.1 --- */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int iabs(int v) { return v < 0 ? -v : v; }
static int in_rng(int pcl, int lo) { return lo + ((pcl - lo) % 12 + 12) % 12; }   /* a pitch class in lo .. lo+11 */
static float body_arc(int b)                     /* 0 .. 1 .. 0 over the body: up to two thirds, then down */
{
    float len = (float)(pc.coda - pc.intro), a;
    a = b < pc.intro ? 0.0f : fm_clampf((float)(b - pc.intro) / (len > 1.0f ? len : 1.0f), 0.0f, 1.0f);
    return a < 0.62f ? a / 0.62f : 1.0f - (a - 0.62f) / 0.38f;
}

/* Minimalist: the figure every pulse, the chord every two or four bars through the cycle; a cycle a section, the
 * figure a note longer each section to the middle, then shorter. Var 0: triplets over the left hand's eighths;
 * 1: eighths over held octaves; 2: rocking eighths over the left hand's triplets */
static void arp_bar(int b)
{
    const acyc_t *cy = &ACYC[pc.gp];
    int body = b - pc.intro, last = b == pc.nbars - 1, coda = b >= pc.coda, i, npul = pc.var == 0 ? 12 : 8;
    int clen = cy->n * pc.every, sec = body < 0 ? 0 : body / clen, chg, stage, lad[5], top = 0;
    float arc = body_arc(b), x, step;
    ch_t c;
    if (b < pc.intro)
        c = cy->c[0];
    else if (coda)
        c = (ch_t){0, (uint8_t)(cy->mode == M_AEO ? T_MIN : T_MAJ)};
    else
        c = cy->c[(body % clen) / pc.every];
    chg = b == 0 || b == pc.intro || b == pc.coda || (!coda && body > 0 && body % pc.every == 0);
    pc.ch[0] = c;
    pc.at2 = -1;
    bar_spt = pc.spt / (1.0f + 0.3f * pc.drift);
    bar_rit = coda ? (last ? 0.3f + 0.3f * pf(P_RUBATO) : 0.05f * (float)(b - pc.coda + 1)) : 0.0f;
    {
        int s = sec < pc.cyc - 1 - sec ? sec : pc.cyc - 1 - sec, base = pc.var == 1 ? 1 : 0;
        stage = clampi((coda || b < pc.intro) ? base : base + s, 0, 3);
    }
    x = coda ? 0.32f : b < pc.intro ? 0.36f : 0.38f + 0.32f * arc;
    if (chg) {
        int pcs[3], ro = pc.reg == 1 ? 7 : pc.reg == 2 ? -5 : 0;
        for (i = 0; i < 3; i++)
            pcs[i] = (en_key + c.root + CT_IV[c.t][i]) % 12;
        voice_lead(pcs, 3, 58 + ro, 74 + ro, pc.rh);
        chord_at(0.0f, c);
    }
    ped_change(0.0f, last);
    lad[0] = pc.rh[0];
    lad[1] = pc.rh[1];
    lad[2] = pc.rh[2];
    lad[3] = pc.rh[0] + 12;
    lad[4] = pc.rh[1] + 12;
    {
        int r = bass_note(c, pc.reg == 2 ? 33 : 36);
        if (last) {                              /* the close: a low octave, the chord rolled up, held */
            note_at(0.0f, r, loud(0.3f) * 0.62f, (float)pc.tbar * 2.0f, R_BASS, hum(4.0f));
            note_at(0.0f, r + 12, loud(0.3f) * 0.42f, (float)pc.tbar * 2.0f, R_BASS, hum(4.0f) + 200);
            for (i = 0; i < 4; i++)
                note_at(2.0f + (float)i * 2.0f, lad[i], loud(0.3f) * 0.48f, (float)pc.tbar * 2.0f, R_CHORD, hum(4.0f));
            return;
        }
        if (b >= pc.intro) {                     /* the left hand: in from the body */
            if (pc.var == 1) {                   /* octaves held, struck again softer each bar */
                float v = loud(x) * (chg ? 0.6f : 0.46f);
                note_at(0.0f, r, v, (float)pc.tbar, R_BASS, hum(3.0f));
                note_at(0.0f, r + 12, v * 0.78f, (float)pc.tbar, R_BASS, hum(3.0f));
            } else {
                static const int8_t L8[4] = {0, 7, 12, 7}, L12[3] = {0, 7, 12};
                int nl = pc.var == 0 ? 8 : 12, grp = pc.var == 0 ? 4 : 3;
                float st = (float)pc.tbar / (float)nl;
                for (i = 0; i < nl; i++) {
                    int o = pc.var == 0 ? L8[i & 3] : L12[i % 3];
                    if (coda && (i & 1) && b == pc.nbars - 2)
                        continue;
                    note_at((float)i * st, r + o, loud(x) * (i % grp == 0 ? 0.5f : 0.38f), st * 1.3f,
                            i % grp == 0 ? R_BASS : R_ARP, hum(2.5f));
                }
            }
        }
    }
    /* the right hand's figure; in the held-octave kind its top note sings a little */
    step = (float)pc.tbar / (float)npul;
    for (i = 0; i < npul; i++)
        top = (pc.var == 0 ? AF12[stage][i] : AF8[stage][i]) > top ? (pc.var == 0 ? AF12[stage][i] : AF8[stage][i]) : top;
    for (i = 0; i < npul; i++) {
        int k = pc.var == 0 ? AF12[stage][i] : AF8[stage][i];
        float v = loud(x) * (i % (pc.var == 0 ? 3 : 2) == 0 ? 0.54f : 0.44f);
        if (pc.var == 1 && stage >= 2 && k == top)
            v *= 1.15f;
        if (coda && (i & 1) && b == pc.nbars - 2)
            continue;                            /* the figure thinning to the end */
        note_at((float)i * step, lad[k], v, step * 1.4f, R_ARP, hum(2.5f));
    }
}

/* Chorale: the alto and tenor for a phrase, chord by chord: chord notes in range, the chord complete, the right
 * doubling, the least motion, no parallel fifths or octaves between any two parts, sevenths down, the leading
 * note up */
static int hy_pen(const int *v, const int *p, ch_t c, ch_t pch, int has_prev)
{
    int i, j, pen = 0, cnt[12] = {0}, n = CT_N[c.t], root = ch_rootpc(c), lt = (en_key + 11) % 12;
    for (i = 0; i < 4; i++)
        cnt[v[i] % 12]++;
    for (i = 0; i < n; i++)
        if (!cnt[(root + CT_IV[c.t][i]) % 12])
            pen += i < 2 ? 60 : i == 3 ? 40 : 4;
    {
        int third = (root + CT_IV[c.t][1]) % 12, fifth = (root + CT_IV[c.t][2]) % 12;
        if (cnt[third] > 1)
            pen += (c.t == T_MIN || c.t == T_DIM) && third != lt ? 3 : 30;
        if (n == 4 && cnt[(root + CT_IV[c.t][3]) % 12] > 1)
            pen += 50;
        if (cnt[lt] > 1 && (c.root == 7 || c.root == 11 || c.root == 4))
            pen += 50;
        if (cnt[fifth] > 1)
            pen += 2;
    }
    if (!has_prev)
        return pen + iabs(v[1] - (v[0] - 4)) + iabs(v[2] - (v[1] + v[3]) / 2) / 2;
    for (i = 1; i < 3; i++) {
        int d = iabs(v[i] - p[i]);
        pen += d > 7 ? 2 * d + 6 : d;
    }
    for (i = 0; i < 4; i++)
        for (j = i + 1; j < 4; j++) {
            int a = v[i] - v[j], q = p[i] - p[j], mi = v[i] - p[i], mj = v[j] - p[j];
            if (a % 12 == q % 12 && (a % 12 == 0 || a % 12 == 7) && mi && mj)
                pen += (mi > 0) == (mj > 0) ? 200 : 60;
        }
    if (v[1] < p[2] || v[2] > p[1])
        pen += 15;
    if (pch.root != c.root || pch.t != c.t) {
        if (CT_N[pch.t] == 4 && CT_IV[pch.t][3] < 12) {   /* the last chord's seventh: down a step */
            int sev = (en_key + pch.root + CT_IV[pch.t][3]) % 12;
            for (i = 1; i < 3; i++)
                if (p[i] % 12 == sev && !(v[i] == p[i] - 1 || v[i] == p[i] - 2))
                    pen += 40;
        }
        if (pch.root == 7 && (c.root == 0 || c.root == 9 || c.root == 8))   /* the leading note: up */
            for (i = 1; i < 3; i++)
                if (p[i] % 12 == lt && v[i] != p[i] + 1)
                    pen += 12;
    }
    return pen;
}
/* the alto and tenor for a whole phrase: every chord's possible pairs, and the cheapest path through them */
#define HY_NC 24
static uint8_t hy_a[14][HY_NC], hy_t[14][HY_NC], hy_n[14], hy_back[14][HY_NC];
static int hy_cands(int k, const hst_t *st)
{
    int a, t, n = 0, s = st->s + pc.htr, bs = st->b + pc.htr, wide;
    uint16_t m = ch_pcs((ch_t){st->r, st->t});
    for (wide = 0; wide < 2 && !n; wide++)       /* (none in range: a wider range, the bass's unison) */
        for (a = s - 1; a >= s - 12 - 4 * wide && a > bs + 1 - wide; a--) {
            if (!(m >> (a % 12) & 1u) || a < 50 - 4 * wide)
                continue;
            for (t = a - 1; t >= a - 12 - 4 * wide && t > bs - wide; t--)
                if (m >> (t % 12) & 1u && t >= 43 - 3 * wide && t <= 69 && n < HY_NC) {
                    hy_a[k][n] = (uint8_t)a;
                    hy_t[k][n] = (uint8_t)t;
                    n++;
                }
        }
    if (!n) {
        hy_a[k][0] = (uint8_t)(s - 3);
        hy_t[k][0] = (uint8_t)((s + bs) / 2);
        n = 1;
    }
    return hy_n[k] = (uint8_t)n;
}
static void hy_solve(const hphr_t *ph)
{
    int k, i, j, cost[HY_NC], nc[HY_NC], v[4], p[4];
    for (k = 0; k < ph->n; k++) {
        const hst_t *st = &ph->st[k];
        ch_t c = {st->r, st->t}, pch = k ? (ch_t){ph->st[k - 1].r, ph->st[k - 1].t} : pc.hlast;
        int n = hy_cands(k, st);
        v[0] = st->s + pc.htr;
        v[3] = st->b + pc.htr;
        for (i = 0; i < n; i++) {
            v[1] = hy_a[k][i];
            v[2] = hy_t[k][i];
            if (!k) {
                nc[i] = hy_pen(v, pc.satb, c, pch, pc.has_satb);
                continue;
            }
            p[0] = ph->st[k - 1].s + pc.htr;
            p[3] = ph->st[k - 1].b + pc.htr;
            nc[i] = 1 << 30;
            for (j = 0; j < hy_n[k - 1]; j++) {
                int q;
                p[1] = hy_a[k - 1][j];
                p[2] = hy_t[k - 1][j];
                q = cost[j] + hy_pen(v, p, c, pch, 1);
                if (q < nc[i])
                    nc[i] = q, hy_back[k][i] = (uint8_t)j;
            }
        }
        for (i = 0; i < n; i++)
            cost[i] = nc[i];
    }
    for (j = 0, i = 1; i < hy_n[ph->n - 1]; i++)
        if (cost[i] < cost[j])
            j = i;
    for (k = ph->n - 1; k >= 0; k--) {
        pc.hv[k][0] = ph->st[k].s + pc.htr;
        pc.hv[k][1] = hy_a[k][j];
        pc.hv[k][2] = hy_t[k][j];
        pc.hv[k][3] = ph->st[k].b + pc.htr;
        if (k)
            j = hy_back[k][j];
    }
    for (i = 0; i < 4; i++)
        pc.satb[i] = pc.hv[ph->n - 1][i];
    pc.has_satb = 1;
    pc.hlast = (ch_t){ph->st[ph->n - 1].r, ph->st[ph->n - 1].t};
}
/* a bar of the hymn: the intro gives out the last line; verses (the last fuller, the bass doubled), an amen */
static void hymn_bar(int b)
{
    int pos = b - pc.intro, ph, bip, verse, k, e = 0, i, last = b == pc.nbars - 1;
    const hphr_t *p;
    float x, rit;
    if (b < pc.intro)
        ph = 3, bip = b, verse = -1;
    else if (b >= pc.coda)
        ph = 4, bip = b - pc.coda, verse = pc.nverse;
    else
        verse = pos / 16, ph = (pos % 16) / 4, bip = pos % 4;
    p = &HPHR[pc.hp[ph]];
    if (bip == 0)
        hy_solve(p);
    pc.at2 = -1;
    bar_spt = pc.spt / bar_tempo(ph == 4 ? 2 + bip : bip, 4, &rit);
    rit = (ph < 4 && bip == 3) ? 0.18f + 0.3f * pf(P_RUBATO) : last ? 0.4f + 0.3f * pf(P_RUBATO) : 0.0f;
    bar_rit = rit;
    {
        static const float VX[4] = {0.55f, 0.45f, 0.6f, 0.5f};
        int full = verse == pc.nverse - 1 && pc.nverse > 1;
        x = verse < 0 ? 0.42f : ph == 4 ? 0.42f : full ? 0.68f : VX[verse & 3];
        x *= 0.6f + 0.45f * (float)phrase_x_tbl(bip) * 0.016f;
        for (k = 0; k < p->n; k++) {
            const hst_t *st = &p->st[k];
            int fin = k == p->n - 1;
            if (e / 8 == bip) {
                ch_t c = {st->r, st->t};
                float on = (float)((e % 8) * 6), len = (float)(st->d * 6);
                chord_at(on, c);
                ped_change(on, last);
                for (i = 0; i < 4; i++) {
                    int nn = pc.hv[k][i], j, ends_fin = fin;
                    float l = len;
                    if ((i == 1 || i == 2) && k > 0 && pc.hv[k - 1][i] == nn)
                        continue;                /* a common tone held over */
                    if (i == 1 || i == 2)
                        for (j = k + 1; j < p->n && pc.hv[j][i] == nn; j++) {
                            l += (float)(p->st[j].d * 6);
                            ends_fin = j == p->n - 1;
                        }
                    if (ends_fin)
                        l -= ph == 4 ? -24.0f : 6.0f;   /* a breath before the next line; the amen rings */
                    note_at(on, nn, loud(x) * (i == 0 ? 0.6f : i == 3 ? 0.54f : 0.42f), l * 0.97f,
                            i == 0 ? R_MEL : i == 3 ? R_BASS : R_CHORD, hum(4.0f));
                }
                if (full && ph < 4 && pc.hv[k][3] - 12 >= 28)
                    note_at(on, pc.hv[k][3] - 12, loud(x) * 0.36f, (fin ? len - 6.0f : len) * 0.97f, R_BASS, hum(4.0f));
#ifdef EN_HOST
                if (en_dbg_satb)
                    en_dbg_satb(cur_bar, (int)on, pc.hv[k]);
#endif
            }
            e += st->d;
        }
    }
    if (ph != 4 && bip == 3 && par[P_PEDAL] >= 15)   /* the breath: the pedal up with the hands */
        sched(E_PEDUP, at_tick(42.0f), 0, 0.0f, 0, 0, 0);
}

/* Stride: half a bar of the left hand, the bass on its beat and the chord on the next */
static void stride_half(float t0, ch_t c, int alt, float x, int last)
{
    int r = bass_note(c, pc.var == 0 ? 33 : 36), i, pcs[3], up[3], n = CT_N[c.t];
    float v = loud(x);
    if (alt)                                     /* beat three of a one-chord bar: the fifth */
        r = r - 5 >= 31 ? r - 5 : r + 7;
    else if (c.root == 0 && pc.proot == 6)       /* from the raised fourth's dim7: the tonic over its fifth */
        r = in_rng((en_key + 7) % 12, pc.var == 0 ? 31 : 36);
    pc.proot = c.root;
    ped_change(t0, last);
    if (pc.var == 0) {                           /* octaves */
        note_at(t0, r, v * 0.56f, 20.0f, R_BASS, hum(5.0f));
        note_at(t0, r + 12, v * 0.44f, 20.0f, R_BASS, hum(5.0f));
    } else if (pc.var == 1 && !alt) {            /* a tenth, rolled */
        note_at(t0, r, v * 0.56f, 22.0f, R_BASS, hum(5.0f));
        note_at(t0 + 0.9f, r + 12 + CT_IV[c.t][1], v * 0.4f, 21.0f, R_BASS, hum(5.0f));
    } else {
        note_at(t0, r, v * 0.56f, 20.0f, R_BASS, hum(5.0f));
    }
    for (i = 0; i < 3; i++)                      /* the chord: its notes but the root (a triad: the root too) */
        pcs[i] = (en_key + c.root + CT_IV[c.t][n == 3 ? i : i + 1]) % 12;
    voice_lead(pcs, 3, 53, 66, up);
    for (i = 0; i < 3; i++)
        note_at(t0 + 12.0f, up[i], v * 0.36f, 9.0f, R_CHORD, hum(4.0f));
}
static void stride_bar(int b)
{
    int body = b - pc.intro, last = b == pc.nbars - 1, coda = b >= pc.coda, sec = 0, bis = 0, two, lb = 0;
    float x, rit;
    ch_t c0, c1;
    static const ch_t SI[2][2] = {{S_I6, S_VI7}, {S_ii7, S_V7}};
    if (b < pc.intro) {
        c0 = SI[b & 1][0];
        c1 = SI[b & 1][1];
        bis = 6 + (b & 1);
    } else if (coda) {
        c0 = (ch_t)S_I6;
        c1 = b == pc.coda ? (ch_t){1, T_DOM7} : (ch_t){99, 0};
        bis = 7;
    } else {
        const ssec_t *s;
        sec = (body % pc.flen) / 8;
        bis = body % 8;
        lb = pc.form[sec];
        s = lb ? &S_B[pc.sbi] : &S_A[pc.sai];
        c0 = s->c[bis][0];
        c1 = s->c[bis][1];
    }
    two = c1.root != 99;
    pc.ch[0] = c0;
    pc.at2 = two ? 24 : -1;
    if (two)
        pc.ch[1] = c1;
    bar_spt = pc.spt / bar_tempo(bis & 3, 4, &rit);
    if (coda)
        rit = last ? 0.35f + 0.3f * pf(P_RUBATO) : 0.1f;
    bar_rit = rit;
    x = b < pc.intro || coda ? 0.4f : (lb ? 0.62f : 0.52f) * (float)phrase_x_tbl(bis & 3) * 0.016f;
    chord_at(0.0f, c0);
    if (two)
        chord_at(24.0f, c1);
    if (last) {                                  /* the end: a low tonic, a 6/9 in fourths rolled up and held */
        int r = bass_note(c0, 33), e = in_rng((en_key + 4) % 12, 58), i;
        ped_change(0.0f, 1);
        note_at(0.0f, r, loud(0.35f) * 0.6f, 96.0f, R_BASS, hum(4.0f));
        note_at(0.0f, r + 12, loud(0.35f) * 0.45f, 96.0f, R_BASS, hum(4.0f) + 300);
        for (i = 0; i < 4; i++)
            note_at(1.5f + 1.5f * (float)i, e + 5 * i, loud(0.35f) * (i == 3 ? 0.55f : 0.45f), 96.0f, R_CHORD, hum(3.0f));
        return;
    }
    stride_half(0.0f, c0, 0, x, last);
    stride_half(24.0f, two ? c1 : c0, !two, x, last);
    if (b < pc.intro)
        return;
    {
        int32_t lag = (int32_t)(0.02f * EN_SR);
        if (coda) {                              /* the last word, home */
            pc.shape = 1;
            melody_bar(R_SW, NA(R_SW), 3, 4, 3, 2, 0.42f, lag);
            return;
        }
        if (body % pc.flen == 0)
            pc.theme_ok = 0;                     /* each chorus its own tune: the second a variation */
        if ((bis & 3) == 0) {
            pc.shape = lb ? 0 : tirand(3);
            pc.centre = (float)(pc.mlo + pc.mhi) * 0.5f + (lb ? 2.0f : -1.0f);
        }
        {
            int mode, end = (bis & 3) == 3 ? 1 : 0;
            if (lb)
                mode = 3;
            else
                mode = bis < 4 ? (pc.theme_ok ? 1 : 0) : 2;
            if (!pc.theme_ok && mode != 3)
                mode = 0;
            melody_bar(R_SW, NA(R_SW), bis & 3, 4, mode, end, x, lag);
        }
    }
}

/* Planing: the collection's note i (0 the pentatonic, 1 the mode, 2 the whole tones), from the tonic up */
static int pl_note(int col, int i)
{
    const int8_t *c = col == 0 ? pc.pl_pent : col == 1 ? MODE_ST[pc.mode] : WT6;
    int n = col == 0 ? 5 : col == 1 ? 7 : 6, o = (i >= 0 ? i : i - n + 1) / n;
    return pc.pl_base + 12 * o + c[i - o * n];
}
enum { SH_PENT, SH_FIFTH, SH_SEV, SH_WT };
static int sh_col(int sh) { return sh == SH_SEV ? 1 : sh == SH_WT ? 2 : 0; }
static int pl_chord(int sh, int i, int *o)       /* the shape under top note i: its notes, top first */
{
    int t;
    switch (sh) {
    case SH_PENT:                                /* every other note of the pentatonic: fourths and thirds */
        o[0] = pl_note(0, i), o[1] = pl_note(0, i - 2), o[2] = pl_note(0, i - 4);
        return 3;
    case SH_FIFTH:                               /* open fifths and the octave, moved exactly */
        t = pl_note(0, i);
        o[0] = t, o[1] = t - 5, o[2] = t - 12;
        return 3;
    case SH_SEV:                                 /* sevenths in the mode */
        o[0] = pl_note(1, i), o[1] = pl_note(1, i - 2), o[2] = pl_note(1, i - 4), o[3] = pl_note(1, i - 6);
        return 4;
    default:                                     /* a ninth chord on the whole tones, moved exactly */
        t = pl_note(2, i);
        o[0] = t, o[1] = t - 4, o[2] = t - 10, o[3] = t - 14;
        return 4;
    }
}
/* an index to end a line on, its note in the register: the tonic, or (open) the third or fifth */
static int pl_end(int col, int open)
{
    int d, o;
    if (col == 2)
        d = tirand(6);
    else if (col == 1)
        d = open ? (tirand(2) ? 2 : 4) : 0;
    else
        d = open ? (pc.pl_pent == PENT_MAJ ? (tirand(2) ? 2 : 3) : (tirand(2) ? 1 : 3)) : 0;
    for (o = -3; o < 3; o++)
        if (pl_note(col, d + o * (col == 0 ? 5 : col == 1 ? 7 : 6)) >= pc.pl_lo)
            return d + o * (col == 0 ? 5 : col == 1 ? 7 : 6);
    return d;
}
/* a planed phrase of two bars from tick t0 of the unit, its top line ending on end */
static void pl_phrase(int sh, int t0, int end)
{
    const plr_t *r = &PL_R[tirand(NA(PL_R))];
    int k, nc = 0, cand[16], ci, start, t = t0;
    for (k = 0; k < NA(PL_C); k++)
        if (PL_C[k].n == r->n)
            cand[nc++] = k;
    ci = cand[tirand(nc)];
    start = end - PL_C[ci].s[r->n - 1];
    for (k = 0; k < r->n && pc.pl_n < 12; k++) {
        pc.pl_on[pc.pl_n] = t;
        pc.pl_du[pc.pl_n] = r->du[k];
        pc.pl_top[pc.pl_n] = start + PL_C[ci].s[k];
        pc.pl_sh[pc.pl_n] = sh;
        pc.pl_n++;
        t += r->du[k];
    }
}
/* a unit (four bars): a planed call, then an arabesque over its held chord or a planed answer home */
static void pl_unit(int u)
{
    int sec = u < pc.pl_a1 ? 0 : u < pc.pl_a1 + pc.pl_nb ? 1 : 2, sh = pc.pl_main;
    int closing = u == pc.pl_a1 - 1 || u == pc.pl_a1 + pc.pl_nb - 1 || u == pc.pl_nu - 1;
    pc.pl_n = pc.pl_an = 0;
    pc.pl_bass = sec == 1 ? pc.pl_alt : 0;
    if (sec == 1 && sh == SH_SEV)
        sh = SH_PENT;                            /* over the other bass: the pentatonic's */
    if (sec == 1 && pc.var == 2 && !pc.pl_wt && (u == pc.pl_a1 + pc.pl_nb - 1 || trand() < 0.5f)) {
        sh = SH_WT;                              /* once a piece: the whole tones, over the tonic */
        pc.pl_wt = 1;
        pc.pl_bass = 0;
    }
    pl_phrase(sh, 0, pl_end(sh_col(sh), 1));
    if (!closing && sh != SH_WT && trand() < 0.3f + 0.45f * pf(P_DENSITY)) {
        const plar_t *a = &PL_A[tirand(NA(PL_A))];
        int top[4], i, idx, lo;
        pl_chord(pc.pl_sh[pc.pl_n - 1], pc.pl_top[pc.pl_n - 1], top);
        pc.pl_du[pc.pl_n - 1] += 96;             /* the last chord held under it */
        lo = top[0] + 3;
        for (idx = -12; pl_note(0, idx) < lo; idx++)
            ;
        for (i = 0; i < a->n; i++) {
            pc.pl_aat[i] = 96 + 6 + a->at[i];
            pc.pl_adu[i] = a->du[i];
            pc.pl_anote[i] = pl_note(0, idx + a->s[i]);
        }
        pc.pl_an = a->n;
    } else {
        int s2 = sh == SH_WT ? (pc.pl_main == SH_SEV ? SH_PENT : pc.pl_main) : sh;
        pl_phrase(s2, 96, pl_end(sh_col(s2), !closing && sec != 2 && trand() < 0.4f));
    }
}
static void plane_bar(int b)
{
    int body = b - pc.intro, ub = body % 4, last = b == pc.nbars - 1, coda = b >= pc.coda, k, i, o[4], n;
    float x, rit, arc = body_arc(b);
    pc.at2 = -1;
    bar_spt = pc.spt / bar_tempo(b < pc.intro || coda ? 0 : ub, 4, &rit);
    bar_rit = coda ? (last ? 0.3f + 0.3f * pf(P_RUBATO) : 0.1f) : ub == 3 ? rit : 0.0f;
    x = b < pc.intro || coda ? 0.36f : 0.42f + 0.25f * arc;
    if (b < pc.intro || coda) {
        int bass = in_rng(en_key, 31);
        if (coda && b > pc.coda) {               /* the close: a figure up high, fading; then the ring */
            if (b == pc.coda + 1) {
                const plar_t *a = &PL_A[0];
                int idx;
                for (idx = -12; pl_note(0, idx) < pc.pl_lo + 7; idx++)
                    ;
                for (i = 0; i < a->n; i++)
                    note_at(6.0f + (float)a->at[i], pl_note(0, idx + a->s[i]), loud(0.3f) * (0.42f - 0.03f * (float)i),
                            (float)a->du[i] + 24.0f, R_MEL, hum(6.0f));
            }
            return;
        }
        /* the opening, the last chord: the tonic deep, the shape over it on the tonic's sonority */
        ped_change(0.0f, 1);
        chord_set_at(0.0f, (uint16_t)(1u << en_key | 1u << ((en_key + 7) % 12) | 1u << ((en_key + (pc.pl_pent == PENT_MAJ ? 4 : 3)) % 12)), en_key);
        note_at(0.0f, bass, loud(x) * 0.6f, (float)pc.tbar * (coda ? 3.0f : 1.0f), R_BASS, hum(4.0f));
        note_at(0.0f, bass + 12, loud(x) * 0.42f, (float)pc.tbar * (coda ? 3.0f : 1.0f), R_BASS, hum(4.0f) + 250);
        n = pl_chord(pc.pl_main == SH_SEV || pc.pl_main == SH_WT ? SH_PENT : pc.pl_main,
                     pl_end(0, 0) + (pc.pl_pent == PENT_MAJ ? 4 : 1), o);
        for (i = n - 1; i >= 0; i--)             /* rolled up from the bottom */
            note_at((float)(n - 1 - i) * (coda ? 3.0f : 1.0f), o[i], loud(x) * (i ? 0.42f : 0.5f),
                    (float)pc.tbar * (coda ? 3.0f : 1.0f), R_CHORD, hum(4.0f));
        return;
    }
    if (ub == 0) {                               /* a unit: its plan, the bass pedal (held by hand), the pedal down */
        int bass;
        pl_unit(body / 4);
        bass = in_rng((en_key + pc.pl_bass) % 12, pc.reg == 2 ? 29 : 31);
        ped_change(0.0f, 1);
        note_at(0.0f, bass, loud(x) * 0.58f, (float)pc.tbar * 4.0f, R_BASS, hum(4.0f));
        note_at(0.0f, bass + 12, loud(x) * 0.4f, (float)pc.tbar * 4.0f, R_BASS, hum(4.0f) + 300);
    } else if (ub == 2 && par[P_PEDAL] < 80) {
        ped_change(0.0f, 1);                     /* a lighter foot: the haze cleared halfway */
    }
    for (k = 0; k < pc.pl_n; k++)
        if (pc.pl_on[k] / 48 == ub) {
            float on = (float)(pc.pl_on[k] % 48);
            uint16_t m = 0;
            n = pl_chord(pc.pl_sh[k], pc.pl_top[k], o);
            for (i = 0; i < n; i++) {
                m |= (uint16_t)(1u << (o[i] % 12));
                note_at(on, o[i], loud(x) * (i ? 0.4f : 0.5f), (float)pc.pl_du[k] * 0.98f, i ? R_CHORD : R_MEL,
                        hum(4.0f) + (int32_t)(i * 0.006f * EN_SR * pf(P_RUBATO)));
            }
            chord_set_at(on, m, o[n - 1] % 12);
        }
    for (k = 0; k < pc.pl_an; k++)
        if (pc.pl_aat[k] / 48 == ub)
            note_at((float)(pc.pl_aat[k] % 48), pc.pl_anote[k], loud(x) * (0.4f - 0.02f * (float)k), (float)pc.pl_adu[k],
                    R_MEL, hum(6.0f));
}

/* Lullaby: a song of four-bar phrases (A: antecedent, consequent; A again; B; A), the left hand rocking (var 0) or
 * bass and chords (var 1, a barcarolle's), a soft close: I, IV, I */
static void lull_half(float t0, ch_t c, float x, int half, int last)
{
    int r = bass_note(c, 38), i, lad[4], third = CT_IV[c.t][1];
    float v = loud(x);
    chord_at(t0, c);
    ped_change(t0, last);
    lad[0] = r, lad[1] = r + 7, lad[2] = r + 12, lad[3] = r + 12 + third;
    if (pc.var == 0) {
        int from = half > 0 ? 3 : 0, to = half ? 6 : 3;
        for (i = from; i < to; i++) {
            int k = LH_68[i];
            note_at((float)i * 4.0f, lad[k], v * (k == 0 ? 0.5f : i == 3 ? 0.4f : 0.34f),
                    k == 0 ? (float)(to - i) * 4.0f : 6.0f, k == 0 ? R_BASS : R_ARP, hum(5.0f));
        }
    } else {
        int pcs[2], up[2], k;
        pcs[0] = (en_key + c.root + third) % 12;
        pcs[1] = (en_key + c.root + CT_IV[c.t][CT_N[c.t] >= 4 ? 3 : 2]) % 12;
        voice_lead(pcs, 2, 55, 66, up);
        for (k = (half > 0 ? 1 : 0); k < (half == 0 ? 1 : 2); k++) {
            float tb = (float)k * 12.0f;
            note_at(tb, k ? (r + 7 > 52 ? r - 5 : r + 7) : r, v * (k ? 0.42f : 0.5f), 11.0f, R_BASS, hum(5.0f));
            for (i = 0; i < 2; i++) {
                note_at(tb + 4.0f, up[i], v * 0.3f, 3.5f, R_CHORD, hum(4.0f));
                note_at(tb + 8.0f, up[i], v * 0.28f, 3.5f, R_CHORD, hum(4.0f));
            }
        }
    }
}
static void lull_bar(int b)
{
    int body = b - pc.intro, pin = (body % pc.flen) / 4, bip = body % 4, last = b == pc.nbars - 1;
    int coda = b >= pc.coda, k = b - pc.coda;
    float x, rit;
    static const ch_t CL[4] = {{0, T_MAJ}, {5, T_MAJ}, {0, T_MAJ}, {0, T_MAJ}};
    if (b < pc.intro) {
        pc.ch[0] = CL[0];
        pc.at2 = -1;
    } else if (coda) {
        pc.ch[0] = CL[k > 3 ? 3 : k];
        pc.at2 = -1;
    } else {
        const nphr_t *ph = pin == 4 ? pc.bph : (pin & 1) ? pc.con : pc.ant;
        pc.ch[0] = ph->c[bip][0];
        pc.at2 = ph->c[bip][1].root == 99 ? -1 : 12;
        if (pc.at2 > 0)
            pc.ch[1] = ph->c[bip][1];
    }
    bar_spt = pc.spt / bar_tempo(b < pc.intro ? 0 : coda ? 3 : bip, 4, &rit);
    if (coda)
        rit = last ? 0.35f + 0.3f * pf(P_RUBATO) : k >= 2 ? 0.15f : 0.04f;
    bar_rit = rit;
    x = b < pc.intro ? 0.3f : coda ? 0.32f : (pin == 4 || pin == 5 ? 0.6f : 0.48f) * (float)phrase_x_tbl(bip) * 0.016f;
    if (last) {                                  /* the close: the tonic rolled slowly, held */
        int r = bass_note(pc.ch[0], 38), i;
        static const int8_t UP[5] = {0, 7, 12, 16, 19};
        chord_at(0.0f, pc.ch[0]);
        ped_change(0.0f, 1);
        for (i = 0; i < 5; i++)
            note_at((float)i * 2.5f, r + UP[i], loud(0.28f) * (i ? 0.38f : 0.5f), 72.0f, i ? R_ARP : R_BASS, hum(4.0f));
        return;
    }
    if (pc.at2 > 0) {
        lull_half(0.0f, pc.ch[0], x, 0, last);
        lull_half(12.0f, pc.ch[1], x, 1, last);
    } else {
        lull_half(0.0f, pc.ch[0], x, -1, last);
    }
    if (b < pc.intro)
        return;
    {
        int32_t lag = (int32_t)(0.012f * EN_SR);
        int mode, end;
        if (coda) {                              /* the last words, falling home */
            if (k == 0) {
                pc.shape = 1;
                pc.centre = (float)(pc.mlo + pc.mhi) * 0.5f - 1.0f;
            }
            if (k <= 2)
                melody_bar(R68, NA(R68), k + 1, 4, 3, k == 2 ? 2 : 0, 0.34f, lag);
            return;
        }
        if (bip == 0) {
            pc.shape = pin == 4 ? 0 : (pin & 1) ? 1 : 2;
            pc.centre = (float)(pc.mlo + pc.mhi) * 0.5f + (pin == 4 ? 2.0f : -1.0f);
        }
        end = bip == 3 ? ((pin & 1) ? 2 : 1) : 0;
        mode = pin == 0 ? 0 : pin == 4 ? 3 : (pin & 1) ? 2 : 1;
        if (!pc.theme_ok && mode != 3)
            mode = 0;
        if (mode == 3) {                         /* the middle: new, the tune kept */
            int keep = pc.theme_ok;
            pc.theme_ok = 0;
            melody_bar(R68, NA(R68), bip, 4, 3, end, x, lag);
            pc.theme_ok = keep;
        } else {
            melody_bar(R68, NA(R68), bip, 4, mode, end, x, lag);
        }
    }
}

/* Blues: a lick's notes fitted to the chord under them: chord notes, the ninth and thirteenth, the sharp nine on a
 * dominant (the blue third); short ones may pass on the fourth or the flat five; anything else to the chord note
 * nearest (down first) */
static int bl_fit(int n, ch_t c, int shortn)
{
    uint16_t m = ch_pcs(c);
    int rel = ((n - ch_rootpc(c)) % 12 + 12) % 12, dom = c.t == T_DOM7;
    if (m >> (n % 12) & 1u)
        return n;
    if (rel == 2 || rel == 9)
        return n;
    if (dom && rel == 3)
        return n;
    if (shortn && c.t != T_DIM7 && (rel == 5 || rel == 6 || rel == 8))
        return n;
    if (m >> ((n + 11) % 12) & 1u)
        return n - 1;
    if (m >> ((n + 10) % 12) & 1u)
        return n - 2;
    if (m >> ((n + 1) % 12) & 1u)
        return n + 1;
    return n + 2;
}
static void bl_lick(const lick_t *L, float t0, float x, int oct)
{
    int i;
    for (i = 0; i < L->n; i++) {
        float t = t0 + (float)L->at[i];
        ch_t c = bar_ch((int)t);
        int raw = pc.bl_T + oct + L->o[i], n = bl_fit(raw, c, L->du[i] <= 8), fl = L->fl[i];
        float v = loud(x) * (i == 0 ? 0.92f : L->du[i] >= 12 ? 0.88f : 0.74f);
        int32_t h = (int32_t)(0.01f * EN_SR) + hum(10.0f);
        if ((fl & 1) && n == raw && (ch_pcs(c) >> ((n + 11) % 12) & 1u) == 0)
            note_at(t - 0.7f, n - 1, v * 0.7f, 0.8f, R_MEL, h);   /* a grace from below: the blue third, bent up */
        note_at(t, n, v, (float)L->du[i] * 0.95f, R_MEL, h);
        if (fl & 2)
            note_at(t, bl_fit(n - 5, c, 0), v * 0.7f, (float)L->du[i] * 0.95f, R_MEL, h);
        if (fl & 4)
            note_at(t, bl_fit(n - 9, c, 0), v * 0.66f, (float)L->du[i] * 0.95f, R_MEL, h);
    }
}
/* the left hand over a chord for so many beats from t0: rocking 1 5 6 5 with a shell on the back beats (var 0),
 * walking (1), or a held root, seventh and tenth (2) */
static void bl_lh(ch_t c, float t0, int beats, ch_t next, float x, int last)
{
    int r = bass_note(c, 36), i, third = CT_IV[c.t][1], sev = CT_N[c.t] >= 4 ? CT_IV[c.t][3] : 10;
    int pcs[2], sh[2];
    float v = loud(x);
    chord_at(t0, c);
    ped_change(t0, last);
    pcs[0] = (en_key + c.root + third) % 12;
    pcs[1] = (en_key + c.root + sev) % 12;
    voice_lead(pcs, 2, 52, 64, sh);
    switch (pc.var) {
    case 0: {
        static const int8_t RK[4] = {0, 7, 9, 7};
        for (i = 0; i < beats; i++) {
            int o = c.t == T_DIM7 ? (i & 1) * 6 : RK[i];
            note_at(t0 + 12.0f * (float)i, r + o, v * (i == 0 ? 0.52f : 0.42f), 11.0f, R_BASS, hum(4.0f));
            if (i & 1) {
                note_at(t0 + 12.0f * (float)i + 8.0f, sh[0], v * 0.28f, 3.5f, R_CHORD, hum(3.0f));
                note_at(t0 + 12.0f * (float)i + 8.0f, sh[1], v * 0.28f, 3.5f, R_CHORD, hum(3.0f));
            }
        }
        break;
    }
    case 1: {
        int up[4], dn[4], *w;
        up[0] = r, up[1] = r + third, up[2] = r + (c.t == T_DIM7 ? 6 : 7), up[3] = r + 9;
        dn[0] = r + sev, dn[1] = r + 9, dn[2] = r + (c.t == T_DIM7 ? 6 : 7), dn[3] = r + third;
        w = pc.bl_up ? up : dn;
        if (beats == 2)
            w = up;
        for (i = 0; i < beats; i++) {
            int n = w[i];
            if (i == beats - 1 && next.root != 99 && next.root != c.root) {   /* the last beat leans into the next */
                int nr = bass_note(next, 36), pv = i ? w[i - 1] : r;
                n = iabs(nr + 1 - pv) < iabs(nr - 1 - pv) ? nr + 1 : nr - 1;
            }
            note_at(t0 + 12.0f * (float)i, n, v * (i == 0 ? 0.5f : 0.42f), 11.0f, R_BASS, hum(4.0f));
        }
        note_at(t0, sh[0], v * 0.3f, 12.0f * (float)beats - 2.0f, R_CHORD, hum(3.0f));
        note_at(t0, sh[1], v * 0.3f, 12.0f * (float)beats - 2.0f, R_CHORD, hum(3.0f));
        if (beats == 4 && (next.root == c.root || next.root == 99))
            pc.bl_up ^= 1;
        else
            pc.bl_up = 1;
        break;
    }
    default:
        note_at(t0, r, v * 0.52f, 12.0f * (float)beats, R_BASS, hum(4.0f));
        note_at(t0 + 0.8f, r + sev, v * 0.32f, 12.0f * (float)beats - 1.0f, R_CHORD, hum(3.0f));
        note_at(t0 + 1.6f, r + 12 + third, v * 0.32f, 12.0f * (float)beats - 2.0f, R_CHORD, hum(3.0f));
        if (beats == 4)
            note_at(t0 + 24.0f, r + 7, v * 0.3f, 22.0f, R_BASS, hum(4.0f));
        break;
    }
}
/* a short answer that isn't the call again */
static int bl_tail(void)
{
    return pc.bl_la == 5 ? 3 : pc.bl_la == 3 ? 5 : tirand(2) ? 5 : 3;
}
static void blues_bar(int b)
{
    const bform_t *F = &BFORM[pc.bl_form];
    int body = b - pc.intro, last = b == pc.nbars - 1, coda = b >= pc.coda, bar12, chorus = 0, i, two;
    float x, rit;
    ch_t c0, c1, next;
    if (b < pc.intro)
        bar12 = 10 + b;
    else if (coda)
        bar12 = 10 + (b - pc.coda);
    else
        chorus = body / 12, bar12 = body % 12;
    c0 = F->c[bar12][0];
    c1 = F->c[bar12][1];
    if (coda) {
        c0 = (ch_t)B_I7;
        c1 = (ch_t){99, 0};
    }
    two = c1.root != 99;
    next = F->c[(bar12 + 1) % 12][0];
    pc.ch[0] = c0;
    pc.at2 = two ? 24 : -1;
    if (two)
        pc.ch[1] = c1;
    bar_spt = pc.spt / bar_tempo(bar12 & 3, 4, &rit);
    if (coda)
        rit = last ? 0.45f + 0.3f * pf(P_RUBATO) : 0.12f;
    bar_rit = rit;
    {
        static const float CX[5] = {0.5f, 0.62f, 0.7f, 0.56f, 0.64f};
        float lx = bar12 >= 8 ? 1.06f : bar12 >= 4 ? 1.0f : 0.94f;
        x = b < pc.intro || coda ? 0.42f : (chorus == pc.bl_nch - 1 && pc.bl_nch > 1 ? 0.48f : CX[chorus % 5]) * lx;
    }
    if (bar12 == 0 && b >= pc.intro) {           /* a chorus: its call and its answer */
        pc.bl_la = pc.bl_prev ? pc.bl_prev - 1 : LICK_CALL[tirand(NA(LICK_CALL))];
        pc.bl_prev = 0;
        do
            pc.bl_lb = LICK_HOME[tirand(NA(LICK_HOME))];
        while (pc.bl_lb == pc.bl_la);
    }
    if (last) {                                  /* the end: bII9 sliding onto I9, held */
        ch_t b2 = {1, T_DOM7}, i9 = {0, T_DOM9};
        int r2 = bass_note(b2, 36), r1 = bass_note(i9, 36), T = pc.bl_T;
        chord_at(0.0f, b2);
        ped_change(0.0f, 0);
        note_at(0.0f, r2, loud(0.45f) * 0.5f, 11.0f, R_BASS, hum(4.0f));
        note_at(0.0f, T - 7, loud(0.45f) * 0.42f, 11.0f, R_CHORD, hum(3.0f));
        note_at(0.0f, T - 1, loud(0.45f) * 0.42f, 11.0f, R_CHORD, hum(3.0f));
        note_at(0.0f, T + 3, loud(0.45f) * 0.46f, 11.0f, R_CHORD, hum(3.0f));
        chord_at(12.0f, i9);
        ped_change(12.0f, 1);
        note_at(12.0f, r1, loud(0.45f) * 0.55f, 84.0f, R_BASS, hum(4.0f));
        note_at(12.0f, r1 + 10, loud(0.45f) * 0.36f, 84.0f, R_CHORD, hum(3.0f));
        note_at(12.0f, T - 8, loud(0.45f) * 0.42f, 84.0f, R_CHORD, hum(3.0f));
        note_at(12.0f, T - 2, loud(0.45f) * 0.42f, 84.0f, R_CHORD, hum(3.0f));
        note_at(12.0f, T + 2, loud(0.45f) * 0.46f, 84.0f, R_CHORD, hum(3.0f));
        note_at(30.0f, T + 12, loud(0.4f) * 0.4f, 60.0f, R_MEL, hum(6.0f));
        return;
    }
    if (two) {
        bl_lh(c0, 0.0f, 2, c1, x, last);
        bl_lh(c1, 24.0f, 2, next, x, last);
    } else {
        bl_lh(c0, 0.0f, 4, next, x, last);
    }
    /* the right hand */
    if (bar12 == 10 && !two) {                   /* the turnaround: the tonic on top, a line falling under it */
        for (i = 0; i < 4; i++) {
            float t = 12.0f * (float)i;
            note_at(t, pc.bl_T - 2 - i, loud(x) * 0.6f, 8.0f, R_MEL, hum(4.0f));
            note_at(t, pc.bl_T, loud(x) * 0.62f, 8.0f, R_MEL, hum(4.0f));
            note_at(t + 8.0f, pc.bl_T, loud(x) * 0.45f, 3.5f, R_MEL, hum(4.0f));
        }
    } else if (bar12 == 11 && !coda && b < pc.coda - 1 && trand() < 0.6f) {   /* a pickup into the next chorus */
        int nx = LICK_CALL[tirand(NA(LICK_CALL))], tg = pc.bl_T + LICK[nx].o[0];
        int n1 = bl_fit(tg - 5, bar_ch(36), 1), n2 = bl_fit(tg - 3, bar_ch(40), 1);   /* climbing into the next call */
        pc.bl_prev = nx + 1;
        note_at(36.0f, n1, loud(x) * 0.6f, 3.5f, R_MEL, hum(5.0f));
        note_at(40.0f, n2 > n1 && n2 < tg - 1 ? n2 : tg - 2, loud(x) * 0.6f, 3.5f, R_MEL, hum(5.0f));
        note_at(44.0f, tg - 1, loud(x) * 0.62f, 3.5f, R_MEL, hum(5.0f));
    } else if (b >= pc.intro && !coda) {
        int line = bar12 / 4, lb = bar12 % 4;
        if (lb == 0)                             /* the call (the same twice), then the line that comes home */
            bl_lick(&LICK[line < 2 ? pc.bl_la : pc.bl_lb], 0.0f, x, 0);
        else if (lb == 1 && trand() < 0.25f + 0.5f * pf(P_DENSITY))
            bl_lick(&LICK[bl_tail()], 0.0f, x * 0.9f, 0);   /* its tail */
        else if (lb == 3 && bar12 < 11 && !pc.bl_st && trand() < 0.6f)
            bl_lick(&LICK[bl_tail()], 0.0f, x * 0.8f, 0);   /* no answer yet: a word before the next line */
        else if (lb == 2 && (pc.bl_st = trand() < 0.45f + 0.4f * pf(P_DENSITY)) != 0) {   /* the response: a soft stab */
            ch_t c = bar_ch(20);
            int t3 = in_rng((ch_rootpc(c) + CT_IV[c.t][1]) % 12, 64), s7 = CT_N[c.t] >= 4 ? CT_IV[c.t][3] : 10;
            int t7 = in_rng((ch_rootpc(c) + s7) % 12, 64);
            note_at(20.0f, t3, loud(x) * 0.45f, 3.5f, R_CHORD, hum(4.0f));
            note_at(20.0f, t7, loud(x) * 0.45f, 3.5f, R_CHORD, hum(4.0f));
            note_at(32.0f, t3, loud(x) * 0.4f, 10.0f, R_CHORD, hum(4.0f));
            note_at(32.0f, t7, loud(x) * 0.4f, 10.0f, R_CHORD, hum(4.0f));
        }
    }
}

/* ----------------------------------------------------------- planning --- */
static void plan(void)
{
    float bar_s = pc.spt * (float)pc.tbar / EN_SR, want;
    int body, cyc;
    want = (float)LEN_MIN[par[P_LENGTH] < 0 ? 0 : par[P_LENGTH] > 4 ? 4 : par[P_LENGTH]] * 60.0f * (0.88f + 0.24f * trand());
    body = (int)(want / bar_s);
    switch (pc.idiom) {
    case ID_GYM:
        pc.intro = 2;
        pc.cyc = 16;
        cyc = (body - 5) / 16;
        cyc = cyc < 1 ? 1 : cyc > 8 ? 8 : cyc;
        pc.coda = pc.intro + cyc * 16;
        pc.nbars = pc.coda + 2;
        break;
    case ID_NOCT:
        pc.intro = 1 + (pc.var != 0);
        cyc = (body - 4) / 24;
        if (cyc < 1) {                           /* a short one: A and A' (16 bars) */
            pc.coda = pc.intro + 16;
        } else {
            cyc = cyc > 6 ? 6 : cyc;
            pc.coda = pc.intro + cyc * 24;
        }
        pc.nbars = pc.coda + 3;
        break;
    case ID_MODAL:
        pc.intro = 1;
        cyc = (body - 3) / 4;
        cyc = cyc < 4 ? 4 : cyc > 60 ? 60 : cyc;
        pc.coda = pc.intro + cyc * 4;
        pc.nbars = pc.coda + 4;
        break;
    case ID_OST:
        pc.intro = 2;
        cyc = (body - 4) / 4;
        cyc = cyc < 6 ? 6 : cyc > 90 ? 90 : cyc;
        pc.coda = pc.intro + cyc * 4;
        pc.nbars = pc.coda + 3;
        break;
    case ID_ARP:                                 /* sections of a cycle each */
        pc.intro = 1;
        cyc = (body - 4) / (ACYC[pc.gp].n * pc.every);
        pc.cyc = clampi(cyc, 3, 14);
        pc.coda = pc.intro + pc.cyc * ACYC[pc.gp].n * pc.every;
        pc.nbars = pc.coda + 3;
        break;
    case ID_HYMN:                                /* the last line as the intro; verses of sixteen bars; the amen */
        pc.intro = 4;
        pc.nverse = clampi((body - 6) / 16, 1, 4);
        pc.coda = pc.intro + 16 * pc.nverse;
        pc.nbars = pc.coda + 2;
        break;
    case ID_STRIDE:                              /* choruses of AABA (or AA, short) */
        pc.intro = 2;
        pc.flen = body >= 30 ? 32 : 16;
        pc.coda = pc.intro + pc.flen * clampi((body - 4 + pc.flen / 3) / pc.flen, 1, 4);
        pc.nbars = pc.coda + 2;
        break;
    case ID_PLANE:                               /* units of four bars: A, B, A */
        pc.intro = 1;
        pc.pl_nu = clampi((body - 4) / 4, 4, 30);
        pc.pl_nb = pc.pl_nu / 3;
        pc.pl_a1 = (pc.pl_nu - pc.pl_nb + 1) / 2;
        pc.coda = pc.intro + pc.pl_nu * 4;
        pc.nbars = pc.coda + 3;
        break;
    case ID_LULL:                                /* the song (32 bars, or 16), sung through again; a soft close */
        pc.intro = 2;
        pc.flen = body >= 40 ? 32 : 16;
        pc.coda = pc.intro + pc.flen * clampi((body - 6 + pc.flen / 3) / pc.flen, 1, 4);
        pc.nbars = pc.coda + 4;
        break;
    case ID_BLUES:                               /* a turnaround in; choruses; the last one's turnaround the ending */
        pc.intro = 2;
        pc.bl_nch = clampi(body / 12, 1, 6);
        pc.coda = pc.intro + 12 * pc.bl_nch - 2;
        pc.nbars = pc.coda + 2;
        break;
    default:                                     /* Airport: loops until about then */
        pc.intro = 0;
        pc.nbars = body < 12 ? 12 : body;
        pc.coda = pc.nbars;
        break;
    }
    en_track_len_ms = (uint32_t)((float)pc.nbars * bar_s * 1000.0f + 9000.0f);
}

static void tempo_set(void)
{
    int bpm = par[P_TEMPO];
    en_bpm = (uint8_t)bpm;
    pc.spt = EN_SR * 60.0f / ((float)bpm * (float)TPB);
}

static void new_piece(void)
{
    uint32_t seed;
    const style_t *s = sty(style_i);
    int i;
    en_track_no++;
    seed = en_track_rand(en_seed_base, en_track_no);
    en_track_seed = seed;
    trng = seed;
    __builtin_memset(&pc, 0, sizeof pc);
    pc.idiom = s->idiom;
    pc.var = s->var;
    pc.reg = s->reg;
    /* the key: any, the register follows it */
    en_key = (uint8_t)tirand(12);
    switch (pc.idiom) {
    case ID_GYM: {
        int k, n = 0, pick;
        for (k = 0; k < (int)NGPAIRS; k++)
            n += (s->var >> k) & 1;
        pick = tirand(n);
        for (k = 0; k < (int)NGPAIRS; k++)
            if ((s->var >> k) & 1) {
                if (!pick--) {
                    pc.gp = k;
                    break;
                }
            }
        pc.mode = GPAIRS[pc.gp].mode;
        pc.meter = 3;
        break;
    }
    case ID_NOCT: {
        int minor = (s->modes & MB(M_AEO)) && (!(s->modes & MB(M_ION)) || trand() < 0.45f);
        pc.mode = minor ? M_AEO : M_ION;
        pc.gp = minor;
        pc.meter = pc.var == 1 ? 3 : 4;
        pc.ant = minor ? &N_ANT_MIN[tirand(NA(N_ANT_MIN))] : &N_ANT_MAJ[tirand(NA(N_ANT_MAJ))];
        pc.con = minor ? &N_CON_MIN[tirand(NA(N_CON_MIN))] : &N_CON_MAJ[tirand(NA(N_CON_MAJ))];
        pc.bph = minor ? &N_B_MIN[tirand(NA(N_B_MIN))] : &N_B_MAJ[tirand(NA(N_B_MAJ))];
        break;
    }
    case ID_AIR:
        pc.mode = pc.var == 2 ? M_DOR : M_ION;
        pc.meter = 4;
        break;
    case ID_ARP: {                               /* a cycle of the style's modes; two bars a chord (four for two) */
        int k, n = 0, ok[NACYC];
        for (k = 0; k < (int)NACYC; k++)
            if (s->modes >> ACYC[k].mode & 1u)
                ok[n++] = k;
        pc.gp = n ? ok[tirand(n)] : 0;
        pc.mode = ACYC[pc.gp].mode;
        pc.every = ACYC[pc.gp].n == 2 ? 4 : 2;
        pc.meter = 4;
        break;
    }
    case ID_HYMN: {                              /* a tune: opening, closing, middle, closing; minor or major */
        int minor = (s->modes & MB(M_AEO)) && (!(s->modes & MB(M_ION)) || trand() < 0.55f);
        pc.mode = minor ? M_AEO : M_ION;
        pc.meter = 4;
        if (minor) {
            pc.hp[0] = H_MO1, pc.hp[1] = H_MC1, pc.hp[2] = H_MM1, pc.hp[3] = H_MC1, pc.hp[4] = H_MAMEN;
        } else {
            pc.hp[0] = tirand(2) ? H_O1 : H_O2;
            pc.hp[1] = tirand(2) ? H_C1 : H_C2;
            pc.hp[2] = tirand(2) ? H_M1 : H_M2;
            pc.hp[3] = pc.hp[2] == H_M1 ? H_C2 : (pc.hp[1] == H_C1 ? H_C2 : H_C1);
            pc.hp[4] = H_AMEN;
        }
        {                                        /* a hymnal's keys: the tunes sit C4-D5 in C, so F# to A move */
            static const uint8_t HK[12] = {0, 1, 2, 3, 4, 5, 5, 2, 3, 10, 10, 11};   /* (B and Bb: down) */
            en_key = HK[en_key % 12];
        }
        pc.htr = en_key <= 5 ? en_key : en_key - 12;
        break;
    }
    case ID_STRIDE:
        pc.mode = M_ION;
        pc.meter = 4;
        pc.sai = tirand(NA(S_A));
        pc.sbi = tirand(NA(S_B));
        pc.form[0] = pc.form[1] = pc.form[3] = 0;
        pc.form[2] = 1;
        pc.blue = 1;
        break;
    case ID_LULL:
        pc.mode = M_ION;
        pc.meter = 2;
        pc.ant = &L_ANT[tirand(NA(L_ANT))];
        pc.con = &L_CON[tirand(NA(L_CON))];
        pc.bph = &L_B[tirand(NA(L_B))];
        break;
    case ID_BLUES:
        pc.mode = M_BLU;
        pc.meter = 4;
        pc.bl_form = pc.var == 1 ? 1 : tirand(2) ? 2 : 0;
        break;
    default: {
        int k, n = 0, pick;
        for (k = 0; k < M_NM; k++)
            n += (s->modes >> k) & 1;
        pick = tirand(n ? n : 1);
        pc.mode = M_ION;
        for (k = 0; k < M_NM; k++)
            if ((s->modes >> k) & 1)
                if (!pick--) {
                    pc.mode = k;
                    break;
                }
        pc.meter = pc.idiom == ID_OST && pc.var == 2 ? 3 : 4;
        break;
    }
    }
    pc.scale = 0;
    for (i = 0; i < 7; i++)
        pc.scale |= (uint16_t)(1u << ((en_key + MODE_ST[pc.mode][i]) % 12));
    en_mode = (uint8_t)pc.mode;
    en_idiom = (uint8_t)pc.idiom;
    en_meter = (uint8_t)pc.meter;
    en_compound = (uint8_t)((pc.idiom == ID_NOCT && pc.var == 0) || pc.idiom == ID_LULL || pc.idiom == ID_BLUES);
    pc.tbar = pc.meter * TPB;
    pc.drift = 0.0f;
    tempo_set();
    plan();
    /* the melody's register */
    pc.mlo = pc.idiom == ID_NOCT ? 65 : pc.idiom == ID_MODAL ? 62 : 64;
    pc.mhi = pc.mlo + 18;
    if (pc.reg == 1)
        pc.mlo += 5, pc.mhi += 5;
    else if (pc.reg == 2)
        pc.mlo -= 4, pc.mhi -= 4;
    if (pc.idiom == ID_STRIDE)                   /* the song's voice: a little higher, an octave and a half */
        pc.mlo = 65 + (pc.reg == 1 ? 3 : pc.reg == 2 ? -4 : 0), pc.mhi = pc.mlo + 19;
    if (pc.idiom == ID_LULL)                     /* singable: middle, an octave and a third (music box: high) */
        pc.mlo = pc.reg == 1 ? 72 : pc.reg == 2 ? 59 : 62, pc.mhi = pc.mlo + 15;
    pc.mprev = pc.mprev2 = (pc.mlo + pc.mhi) / 2;
    pc.centre = (float)pc.mprev;
    pc.nlh = 0;
    if (pc.idiom == ID_OST) {                    /* the cell: root, fifth, ninth, tenth (or eleventh) */
        int r = 50 + ((en_key - 50) % 12 + 12) % 12, third = pc.mode == M_AEO || pc.mode == M_DOR ? 3 : 4;
        pc.cell[0] = r;                          /* major 1 5 9 10, minor 1 5 7 10 */
        pc.cell[1] = r + 7;
        pc.cell[2] = third == 4 ? r + 14 : r + 10;
        pc.cell[3] = r + 12 + third;
        for (i = 0; i < 4; i++)
            pc.cell_prev[i] = pc.cell[i];
        pc.fig = tirand(pc.var == 2 ? 3 : 4);
        pc.every = pf(P_DENSITY) > 0.5f ? 2 : 4;
        pc.broot = -1;
    }
    if (pc.idiom == ID_PLANE) {                  /* the collections, the register, the shape, the B's bass */
        static const int8_t ALT[M_NM][2] = {{5, 9}, {5, 3}, {2, 7}, {8, 3}, {10, 5}, {5, 5}};
        pc.pl_pent = pc.mode == M_AEO || pc.mode == M_DOR ? PENT_MIN : PENT_MAJ;
        pc.pl_base = 60 + en_key;
        pc.pl_lo = 69 + (pc.reg == 1 ? 5 : pc.reg == 2 ? -5 : 0);
        pc.pl_main = pc.var == 1 ? SH_FIFTH : pc.var == 2 ? SH_SEV : SH_PENT;
        pc.pl_alt = ALT[pc.mode][tirand(2)];
    }
    if (pc.idiom == ID_BLUES)                    /* the right hand's tonic: D4 .. C#5 */
        pc.bl_T = in_rng(en_key, 62);
    if (pc.idiom == ID_AIR) {                    /* the loops: three to five, lengths that never line up */
        static const float PER[9] = {15.0f, 17.0f, 19.0f, 21.0f, 23.0f, 25.0f, 27.0f, 29.0f, 31.0f};
        int used = 0, mot = 0;
        pc.nloop = 3 + (int)(pf(P_DENSITY) * 2.4f);
        pc.nloop = pc.nloop > 5 ? 5 : pc.nloop;
        for (i = 0; i < pc.nloop; i++) {
            int p, m;
            do
                p = tirand(9);
            while (used >> p & 1);
            used |= 1 << p;
            do
                m = tirand(NAMOT - 1);
            while (mot >> m & 1);
            mot |= 1 << m;
            pc.lp_per[i] = PER[p] * (1.0f + 0.04f * trand());
            pc.lp_mot[i] = m;
            pc.lp_oct[i] = (i == 0 && pc.reg != 1) ? -1 : (pc.reg != 2 && trand() < 0.2f) ? 1 : 0;
            if (i == 0 && pc.reg != 1)
                pc.lp_mot[i] = tirand(2) ? 2 : NAMOT - 1;   /* the low loop: the tonic, or tonic and fifth */
            pc.lp_vel[i] = loud(0.55f + 0.3f * trand()) * (pc.lp_oct[i] < 0 ? 0.85f : 1.0f);
            pc.lp_next[i] = (float)i * (3.0f + 4.0f * trand()) + 0.5f;
        }
    }
    cur_bar = 0;
    en_track_ms = 0;
    pstart = en_now_ms;
    en_phase = PH_INTRO;
    en_tracks_played++;
}

/* -------------------------------------------------------------- render --- */
static float bar_left;                           /* samples to the next bar */
static int gap_left, ring_left;                  /* the close's ringing, the quiet after: samples */
static float fade = 1.0f, fade_t = 1.0f, fade_rate;
static int skipping;

static void compose(void)
{
    int b = pc.bar;
    if (en_repeat_on && pc.idiom != ID_AIR && b >= pc.coda && b < pc.nbars)
        b = pc.intro;                            /* repeat: the body again, no close */
    if (en_repeat_on && pc.idiom == ID_AIR && b >= pc.nbars - 2 && !pc.ended)
        pc.nbars = b + 8;
    if (b >= pc.nbars && !(pc.idiom == ID_AIR && !pc.ended)) {
        en_phase = PH_END;
        ring_left = (int)(EN_SR * (5.0f + 4.0f * pf(P_ROOM)));
        pc.bar = b;
        return;
    }
    cur_bar = b;
    if (b % 8 == 0)                              /* the tempo wanders a little */
        pc.drift = pf(P_RUBATO) * 0.04f * (trand() * 2.0f - 1.0f);
    en_phase = (uint8_t)(b < pc.intro ? PH_INTRO : b < pc.coda ? PH_BODY : PH_END);
    switch (pc.idiom) {
    case ID_GYM: gym_bar(b); break;
    case ID_NOCT: noct_bar(b); break;
    case ID_MODAL: modal_bar(b); break;
    case ID_OST: ost_bar(b); break;
    case ID_ARP: arp_bar(b); break;
    case ID_HYMN: hymn_bar(b); break;
    case ID_STRIDE: stride_bar(b); break;
    case ID_PLANE: plane_bar(b); break;
    case ID_LULL: lull_bar(b); break;
    case ID_BLUES: blues_bar(b); break;
    default: air_bar(b); break;
    }
    bar_left += (float)at_tick((float)pc.tbar);
    pc.bar = b + 1;
}

static void quiet(void)
{
    int i;
    for (i = 0; i < NV; i++)
        pv[i].on = 0;
    for (i = 0; i < NEV; i++)
        evq[i].used = 0;
    ev_n = 0;
    pedal(0);
    mech.kamp = mech.namp = 0.0f;
}

static void play_on(int on)
{
    int i;
    if (on && !en_playing) {
        en_playing = 1;
    } else if (!on && en_playing) {              /* resting: nothing new; the keys up, the pedal up */
        en_playing = 0;
        for (i = 0; i < NEV; i++)
            evq[i].used = 0;
        ev_n = 0;
        for (i = 0; i < NV; i++)
            if (pv[i].on && !pv[i].player && pv[i].gate > 0)
                pv[i].gate = 0;
        pedal(0);
        for (i = 0; i < NV; i++)                 /* (and the top strings let go too) */
            if (pv[i].on && !pv[i].player)
                damp(&pv[i], 1.0f - tau(0.25f));
        bar_left = 0.0f;
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
                if (a == P_TEMPO)
                    tempo_set();
            }
            break;
        case C_STYLE: style_i = a % EN_NSTYLES; break;
        case C_PLAY: play_on(a); break;
        case C_SKIP:
            if (en_phase != PH_GAP && !skipping) {
                skipping = 1;
                fade_t = 0.0f;
                fade_rate = 1.0f / (1.5f * EN_SR);
            }
            break;
        case C_REPEAT: en_repeat_on = (uint8_t)(a != 0); break;
        case C_TRACK_SEED_HI: en_seed_base = (en_seed_base & 0xFFFFu) | ub << 16; break;
        case C_TRACK_SEED_LO: en_seed_base = (en_seed_base & 0xFFFF0000u) | ub; break;
        case C_TRACK_NO:
            en_track_no = (uint32_t)a << 16 | ub;
            quiet();
            new_piece();
            bar_left = EN_SR * 0.5f;
            break;
        case C_NOTE:
            if (b > 0)
                key_on(a, (float)b * (1.0f / 127.0f), -1, 1);
            else
                key_off_player(a);
            break;
        case C_PANIC: quiet(); break;
        }
    }
}

void en_init(void)
{
    int i;
    float *p = rv_buf;
    for (i = 0; i <= SN; i++)
        SINT[i] = fm_sinf(FM_TWO_PI * (float)i / (float)SN);
    for (i = 0; i < P_NPARAMS; i++)
        par[i] = PARAMS[i].def;
    for (i = 0; i < 4; i++) {
        rv_d[i] = p;
        p += RV_N[i];
        rv_p[i] = 0;
        rv_lp[i] = 0.0f;
    }
    for (i = 0; i < 3; i++) {
        ap_d[i] = p;
        p += AP_N[i];
        ap_p[i] = 0;
    }
    for (i = 0; i < (int)(sizeof rv_buf / sizeof rv_buf[0]); i++)
        rv_buf[i] = 0.0f;
    for (i = 0; i < WB_N; i++)
        wb_l[i] = wb_r[i] = 0.0f;
    for (i = 0; i < NS; i++)
        ss[i].y1 = ss[i].y2 = ss[i].x1 = ss[i].x2 = 0.0f;
    ped = 0;
    en_pedal = 0;
    quiet();
    style_i = 0;
    skipping = 0;
    fade = fade_t = 1.0f;
    gap_left = ring_left = 0;
    en_track_no = 0;
    en_tracks_played = 0;
    en_notes_played = 0;
    new_piece();
    bar_left = EN_SR * 0.3f;
    ss_tune(0);
}

static float bufl[BLK], bufr[BLK];
static float hp_l, hp_r, lev, air_lp, air_hp, hiss_lp, hiss_hp;
static float vu_acc_l, vu_acc_r;
static uint32_t vu_n;

static void render_piano(uint32_t n)
{
    int i, m;
    uint32_t j;
    float lv = 0.36f;
    int voices = 0;
    for (i = 0; i < NV; i++) {
        pv_t *v = &pv[i];
        float env, et, ek, gl, gr;
        if (!v->on)
            continue;
        voices++;
        for (j = 0; j < n; j++)
            vb[j] = 0.0f;
        for (m = 0; m + 1 < v->nm; m += 2) {     /* two partials at a time */
            float a1 = v->y1[m], a2 = v->y2[m], b1 = v->y1[m + 1], b2 = v->y2[m + 1];
            float ca = v->c[m], ra = v->r2[m], cb = v->c[m + 1], rb = v->r2[m + 1];
            for (j = 0; j < n; j++) {
                float ya = ca * a1 - ra * a2, yb = cb * b1 - rb * b2;
                a2 = a1;
                a1 = ya;
                b2 = b1;
                b1 = yb;
                vb[j] += ya + yb;
            }
            v->y1[m] = a1, v->y2[m] = a2, v->y1[m + 1] = b1, v->y2[m + 1] = b2;
        }
        if (m < v->nm) {
            float a1 = v->y1[m], a2 = v->y2[m], ca = v->c[m], ra = v->r2[m];
            for (j = 0; j < n; j++) {
                float ya = ca * a1 - ra * a2;
                a2 = a1;
                a1 = ya;
                vb[j] += ya;
            }
            v->y1[m] = a1, v->y2[m] = a2;
        }
        env = v->env, et = v->env_t, ek = v->env_k, gl = v->gl * lv, gr = v->gr * lv;
        for (j = 0; j < n; j++) {
            float s;
            env += (et - env) * ek;
            s = vb[j] * env;
            bufl[j] += s * gl;
            bufr[j] += s * gr;
        }
        v->env = env;
        /* the key's time; the partials' levels (approximately, a block's worth); the ones gone, dropped */
        if (v->gate > 0) {
            v->gate -= (int32_t)n;
            if (v->gate <= 0)
                key_up(v);
        }
        v->cnt += n;
        while (v->cnt >= BLK) {
            v->cnt -= BLK;
            for (m = 0; m < v->nm; m++)
                v->lvl[m] *= v->rb[m];
        }
        while (v->nm && v->lvl[v->nm - 1] * fm_maxf(env, et) < 5e-5f)
            v->nm--;
        if (!v->nm || (et == 0.0f && env < 5e-5f)) {
            v->on = 0;
            if (!v->damped)
                log_off(v);
        }
    }
    en_voices = (uint8_t)voices;
}

/* the mechanics, the sympathetic strings, the room tone; then the room and the bus */
static void render_body(uint32_t n)
{
    uint32_t j;
    int i;
    float pl = 0.5f - 0.5f * mech.pan, pr = 0.5f + 0.5f * mech.pan, sg = 0.30f * (0.25f + 0.75f * (float)ped);
    float air = pf(P_NOISE) * pf(P_NOISE) * 0.0035f + pf(P_AGE) * pf(P_NOISE) * 0.004f;
    float aa = onepole(2500.0f);
    static float airg;                           /* the room's air: there while playing */
    airg += ((en_playing ? 1.0f : 0.0f) - airg) * fm_minf((float)n / (EN_SR * 0.8f), 1.0f);
    air *= airg;
    if ((ped ? 3.2f : 0.35f) != ss_dec)
        ss_tune(en_chord_pcs);
    for (j = 0; j < n; j++) {
        float in = (bufl[j] + bufr[j]) * 0.5f, y = 0.0f, k = 0.0f, nz;
        for (i = 0; i < NS; i++) {
            float o = ss[i].c * ss[i].y1 - ss[i].r2 * ss[i].y2 + ss[i].g * (in - ss[i].x2) * sg;
            ss[i].y2 = ss[i].y1;
            ss[i].y1 = o;
            ss[i].x2 = ss[i].x1;
            ss[i].x1 = in;
            y += o;
        }
        if (mech.kamp > 1e-5f || mech.namp > 1e-5f) {
            k = sn(mech.kph) * mech.kamp;
            mech.kph = wrap1(mech.kph + mech.kinc);
            mech.kamp *= mech.kdec;
            mech.nlp += (noise() * mech.namp - mech.nlp) * mech.na;
            mech.namp *= mech.ndec;
            k += mech.nlp;
        }
        nz = noise();                            /* the room's air: a soft hiss, darker */
        air_lp += (nz - air_lp) * aa;
        air_hp += (air_lp - air_hp) * 0.02f;
        nz = (air_lp - air_hp) * air;
        bufl[j] += y * 0.8f + k * pl * 0.7f + nz;
        bufr[j] += y * 0.8f + k * pr * 0.7f + nz * 0.9f;
    }
}

static void render_room(uint32_t n)
{
    uint32_t j;
    float room = pf(P_ROOM), send = 0.12f + 0.5f * room, g = 0.55f + 0.38f * room, damp = 0.25f + 0.35f * pf(P_FELT);
    float dry = 1.0f - 0.45f * room * room, wet = 0.35f + 0.55f * room;
    for (j = 0; j < n; j++) {
        float in = (bufl[j] + bufr[j]) * 0.5f * send, x[4], sum, yl, yr;
        int i;
        for (i = 0; i < 3; i++) {
            float *d = ap_d[i], v = d[ap_p[i]], w = in + 0.62f * v;
            d[ap_p[i]] = w;
            in = v - 0.62f * w;
            if (++ap_p[i] >= AP_N[i])
                ap_p[i] = 0;
        }
        for (i = 0; i < 4; i++)
            x[i] = rv_d[i][rv_p[i]];
        sum = (x[0] + x[1] + x[2] + x[3]) * 0.5f;
        for (i = 0; i < 4; i++) {
            float y = (x[i] - sum) * g;
            rv_lp[i] += (y - rv_lp[i]) * (1.0f - damp);
            rv_d[i][rv_p[i]] = rv_lp[i] + in * (i & 1 ? 0.5f : -0.5f);
            if (++rv_p[i] >= RV_N[i])
                rv_p[i] = 0;
        }
        yl = x[0] + x[2] * 0.6f;
        yr = x[1] + x[3] * 0.6f;
        bufl[j] = bufl[j] * dry + yl * wet;
        bufr[j] = bufr[j] * dry + yr * wet;
    }
}

static void render_bus(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    float age = pf(P_AGE), wow = age * age * 9.0f, flut = age * 0.8f, hp_a = onepole(30.0f);
    float lv_rel = tau(0.5f), lv_att = 1.0f - tau(0.003f), hiss = age * pf(P_NOISE) * 0.004f;
    for (j = 0; j < n; j++) {
        float l = bufl[j] * fade, r = bufr[j] * fade, x, vol;
        if (wow > 0.01f) {                       /* an old upright's recording: a slow wow */
            float d, rd, fr, wl, wr;
            int ri;
            wb_l[wb_w] = l;
            wb_r[wb_w] = r;
            d = 40.0f + wow * (1.0f + sn(wow_ph)) + flut * sn(flut_ph);
            wow_ph = wrap1(wow_ph + 0.45f / EN_SR);
            flut_ph = wrap1(flut_ph + 5.7f / EN_SR);
            rd = (float)wb_w - d;
            if (rd < 0.0f)
                rd += (float)WB_N;
            ri = (int)rd;
            fr = rd - (float)ri;
            wl = wb_l[ri & (WB_N - 1)] + (wb_l[(ri + 1) & (WB_N - 1)] - wb_l[ri & (WB_N - 1)]) * fr;
            wr = wb_r[ri & (WB_N - 1)] + (wb_r[(ri + 1) & (WB_N - 1)] - wb_r[ri & (WB_N - 1)]) * fr;
            wb_w = (wb_w + 1) & (WB_N - 1);
            l = wl;
            r = wr;
            x = noise();
            hiss_lp += (x - hiss_lp) * 0.3f;
            hiss_hp += (hiss_lp - hiss_hp) * 0.05f;
            l += (hiss_lp - hiss_hp) * hiss;
            r += (hiss_lp - hiss_hp) * hiss * 0.95f;
        }
        fade = fade < fade_t ? fm_minf(fade + fade_rate, fade_t) : fm_maxf(fade - fade_rate, fade_t);
        hp_l += (l - hp_l) * hp_a;
        hp_r += (r - hp_r) * hp_a;
        l -= hp_l;
        r -= hp_r;
        vu_acc_l += l * l;
        vu_acc_r += r * r;
        x = fm_maxf(fm_fabsf(l), fm_fabsf(r)) * gain;   /* the leveller: above -6 dB only */
        lev = x > lev ? lev + (x - lev) * lv_att : lev * lv_rel;
        vol = lev > 0.5f ? 0.5f / lev : 1.0f;
        l *= gain * vol;
        r *= gain * vol;
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
    }
}

static void fire(ev_t *e)
{
    switch (e->type) {
    case E_KEY: key_on(e->note, e->vel, e->gate, 0); break;
    case E_PEDDN: pedal(1); break;
    case E_PEDUP: pedal(0); break;
    case E_CHORD:
        en_chord_pcs = e->pcs;
        en_chord_root = e->root;
        pc.last_pcs = e->pcs;
        ss_tune(e->pcs);
        break;
    default: break;
    }
}

static void clock_ms(uint32_t k)
{
    ms_rem += k * 1000u;
    en_now_ms += ms_rem / 44100u;
    ms_rem %= 44100u;
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    /* MASTER; sparse idioms a little up (a few notes at a time read quieter) */
    static const float IG[ID_N] = {1.0f, 1.7f, 1.0f, 1.0f, 1.2f, 1.1f, 1.3f, 1.1f, 1.9f, 1.1f, 1.25f};
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f * IG[(unsigned)pc.idiom < ID_N ? pc.idiom : 0];
    drain();
    while (n) {
        uint32_t k = n > BLK ? BLK : n, j;
        int i, seq = 0;
        if (en_playing) {
            if (en_phase == PH_GAP) {
                if (gap_left <= 0) {
                    fade = fade_t = 1.0f;
                    new_piece();
                    bar_left = EN_SR * 0.2f;
                } else if ((int)k > gap_left) {
                    k = (uint32_t)gap_left;
                }
            } else if (skipping && fade < 0.002f) {   /* skipped: faded out, a short quiet */
                quiet();
                skipping = 0;
                en_phase = PH_GAP;
                gap_left = (int)(EN_SR * 1.2f);
            } else if (en_phase == PH_END && ring_left > 0 && pc.bar >= pc.nbars && !(pc.idiom == ID_AIR && !pc.ended)) {
                if (ring_left <= (int)k) {       /* the last chord has rung: the pedal up, the quiet */
                    for (i = 0; i < NV; i++)
                        if (pv[i].on && !pv[i].player) {
                            pv[i].gate = 0;
                            damp(&pv[i], 1.0f - tau(0.5f));
                        }
                    pedal(0);
                    ring_left = 0;
                    en_phase = PH_GAP;
                    gap_left = (int)(EN_SR * 3.0f);
                } else {
                    ring_left -= (int)k;
                }
            } else {
                if (bar_left <= 0.0f)
                    compose();
                if (en_phase != PH_GAP && !(en_phase == PH_END && ring_left > 0)) {
                    seq = 1;
                    if ((float)k > bar_left)
                        k = (uint32_t)bar_left + 1u;
                }
            }
        }
        for (i = 0; i < NEV; i++)                /* events due now fire; the chunk ends at the next one */
            if (evq[i].used) {
                if (evq[i].wait <= 0) {
                    fire(&evq[i]);
                    evq[i].used = 0;
                    ev_n--;
                } else if ((uint32_t)evq[i].wait < k) {
                    k = (uint32_t)evq[i].wait;
                }
            }
        if (k > n)
            k = n;
        if (k == 0)
            k = 1;
        for (i = 0; i < NEV; i++)
            if (evq[i].used)
                evq[i].wait -= (int32_t)k;
        if (seq)
            bar_left -= (float)k;
        if (en_playing && en_phase == PH_GAP)
            gap_left -= (int)k;
        clock_ms(k);
        if (en_playing && en_phase != PH_GAP)    /* the piece's time: held while resting */
            en_track_ms = en_now_ms - pstart;
        else
            pstart = en_now_ms - en_track_ms;
        for (j = 0; j < k; j++)
            bufl[j] = bufr[j] = 0.0f;
        render_piano(k);
        render_body(k);
        render_room(k);
        render_bus(out, k, gain);
        if (++vu_n >= 16u) {
            float l = fm_sqrtf(vu_acc_l / (float)(k * 16u)), r = fm_sqrtf(vu_acc_r / (float)(k * 16u));
            en_vu_l = en_vu_l + (fm_minf(l * 4.0f, 1.2f) - en_vu_l) * 0.45f;
            en_vu_r = en_vu_r + (fm_minf(r * 4.0f, 1.2f) - en_vu_r) * 0.45f;
            vu_acc_l = vu_acc_r = 0.0f;
            vu_n = 0;
        }
        out += 2 * k;
        n -= k;
    }
}
