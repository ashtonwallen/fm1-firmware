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
enum { M_ION, M_DOR, M_LYD, M_AEO, M_MIX, M_NM };
static const int8_t MODE_ST[M_NM][7] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 6, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 10},
};
static const char *const MODE_NAME[M_NM] = {"major", "Dorian", "Lydian", "minor", "Mixolydian"};
const char *en_mode_name(int m) { return MODE_NAME[(unsigned)m < M_NM ? m : 0]; }
/* the mode's colour note (Dorian's sixth, Lydian's fourth): the improviser leans on it */
static const int8_t MODE_CHAR[M_NM] = {-1, 9, 6, -1, 10};
static const char *const IDIOM_NAME[ID_N] = {"Gymnopedie", "Airport", "Ostinato", "Nocturne", "Modal"};
const char *en_idiom_name(int id) { return IDIOM_NAME[(unsigned)id < ID_N ? id : 0]; }

/* chord types: semitones above the root */
enum { T_MAJ, T_MIN, T_MAJ7, T_M7, T_DOM7, T_ADD9, T_MADD9, T_MAJ9, T_M9, T_DOM9, T_SUS2, T_6, T_M7B5, T_N };
static const uint8_t CT_N[T_N] = {3, 3, 4, 4, 4, 4, 4, 5, 5, 5, 3, 4, 4};
static const int8_t CT_IV[T_N][5] = {
    {0, 4, 7}, {0, 3, 7}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10}, {0, 4, 7, 14}, {0, 3, 7, 14},
    {0, 4, 7, 11, 14}, {0, 3, 7, 10, 14}, {0, 4, 7, 10, 14}, {0, 2, 7}, {0, 4, 7, 9}, {0, 3, 6, 10},
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
        int on = rh->on[i], du = rh->du[i], strong = on % (pc.tbar == 36 ? 36 : 24) == 0 || du >= 24;
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
            note_at((float)on, n, v, (float)du * 1.02f, R_MEL, lag + hum(12.0f));
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
    en_compound = (uint8_t)(pc.idiom == ID_NOCT && pc.var == 0);
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
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f * (pc.idiom == ID_AIR ? 1.7f : pc.idiom == ID_MODAL ? 1.2f : 1.0f);
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
