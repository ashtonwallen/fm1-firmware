/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * LOFI RADIO: ashtonwallen, 2026 */
/* The instrument (lofi.h). One compilation unit, -O2. The command queue and the render's shape are AMB-1's. */
#include "lofi.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Swing", 0, 100, 45}, {"Dust", 0, 100, 40},  {"Tempo", 60, 100, 80}, {"Mood", 0, 100, 40},
    {"Keys", 0, 100, 70},  {"Drums", 0, 100, 70}, {"Bass", 0, 100, 70},   {"Melody", 0, 100, 50},
    {"Tone", 0, 100, 50},  {"Rain", 0, 100, 0},   {"Length", 0, 4, 1},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }
static const uint8_t LEN_MIN[5] = {2, 3, 4, 5, 6};

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
    if (i == P_TEMPO) {
        put_u(buf, (uint32_t)v);
    } else if (i == P_LENGTH) {
        put_s(put_u(buf, LEN_MIN[v < 0 ? 0 : v > 4 ? 4 : v]), " min");
    } else if (i == P_MOOD) {                     /* jazzy .. melancholy */
        put_s(buf, v < 20 ? "Jazzy" : v < 40 ? "Warm" : v < 60 ? "Mellow" : v < 80 ? "Blue" : "Melancholy");
    } else {
        put_u(buf, (uint32_t)(v < 0 ? 0 : v));
    }
}

/* ------------------------------------------------------------ styles --- */
/* the user's ten (Swing Dust Tempo Mood, Keys Drums Bass Melody, Tone Rain) and the style's own flavour:
 * keys 0 Rhodes, 1 Wurli, 2 felt piano; snare 0 snare, 1 side-stick, 2 both; hats how busy; mel 0 muted lead,
 * 1 vibraphone, 2 either; back: the snare's lay-back (ms); grooves: which (bit i); verb: the room's send */
typedef struct {
    const char *name;
    int8_t v[P_NSTYLE];
    uint8_t keys, snare, hats, mel, back, verb, label;
    uint16_t grooves;
} style_t;
static const style_t STYLES[EN_NSTYLES] = {
    /*                 Swi Dus Tem Moo  Key Drm Bas Mel  Ton Rai    keys sn hats mel back verb label grooves */
    {"Study",          {45, 35, 80, 40, 70, 70, 70, 50, 55, 0},    0, 0, 50, 2, 12, 40, 0, 0x00FF},
    {"Rainy Window",   {40, 45, 72, 70, 75, 55, 65, 45, 40, 60},   0, 1, 35, 1, 16, 55, 1, 0x00B5},
    {"Jazz Cafe",      {62, 30, 88, 10, 75, 70, 75, 60, 66, 0},    0, 0, 70, 2, 8, 45, 2, 0x005B},
    {"Late Bus",       {50, 40, 76, 75, 70, 65, 70, 40, 45, 15},   1, 1, 40, 0, 14, 45, 3, 0x00D6},
    {"Sunday",         {38, 25, 84, 25, 70, 65, 65, 55, 62, 0},    2, 0, 55, 1, 10, 40, 4, 0x003F},
    {"Vinyl Dust",     {48, 85, 78, 50, 70, 70, 70, 45, 35, 0},    0, 0, 50, 0, 12, 40, 5, 0x00FF},
    {"Tokyo Night",    {44, 35, 82, 45, 70, 70, 75, 55, 55, 10},   0, 1, 60, 0, 10, 50, 6, 0x00AD},
    {"Snow Day",       {30, 30, 70, 60, 75, 50, 60, 50, 40, 0},    2, 1, 30, 1, 14, 60, 7, 0x0095},
    {"Library",        {40, 20, 74, 50, 70, 45, 60, 35, 45, 0},    2, 1, 25, 1, 12, 45, 8, 0x0085},
    {"Midnight Tea",   {46, 40, 72, 85, 70, 60, 70, 45, 38, 20},   1, 0, 40, 1, 16, 55, 9, 0x00F4},
    {"Old Tape",       {50, 75, 80, 55, 70, 70, 70, 45, 30, 0},    1, 0, 50, 0, 12, 40, 10, 0x00FF},
    {"Sleepy Cat",     {55, 35, 70, 55, 72, 50, 65, 40, 35, 0},    0, 1, 30, 1, 18, 50, 11, 0x0093},
    {"Golden Hour",    {42, 30, 86, 20, 70, 70, 70, 60, 70, 0},    0, 0, 65, 0, 8, 40, 0, 0x007B},
    {"Rooftop",        {52, 35, 90, 35, 70, 75, 75, 55, 62, 0},    1, 2, 75, 2, 10, 40, 2, 0x005F},
};
const char *en_style_name(int i) { return STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES].name; }
uint8_t en_style_label(int i) { return STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES].label; }
void en_style_recipe(int i, int16_t *par)
{
    int k;
    const style_t *s = &STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES];
    for (k = 0; k < P_NSTYLE; k++)
        par[k] = s->v[k];
}

/* ------------------------------------------------------------ harmony --- */
enum { Q_MAJ7, Q_MAJ9, Q_M7, Q_M9, Q_M11, Q_DOM9, Q_DOM13, Q_7B9, Q_M7B5, Q_M6, Q_69, Q_9SUS, Q_NQ };
static const char *const Q_NAME[Q_NQ] = {"maj7", "maj9", "m7", "m9", "m11", "9", "13", "7b9", "m7b5", "m6", "6/9", "9sus"};
/* rootless voicings (the bass has the root): four notes, semitones above the chord's root */
static const int8_t Q_VOICE[Q_NQ][4] = {
    {4, 7, 11, 12}, {4, 7, 11, 14}, {3, 7, 10, 12}, {3, 7, 10, 14}, {3, 10, 14, 17}, {4, 10, 14, 19},
    {4, 10, 14, 21}, {4, 10, 13, 19}, {3, 6, 10, 12}, {3, 7, 9, 14}, {4, 9, 14, 19}, {5, 10, 14, 19},
};
const char *en_chord_name(int root, int q, char *buf)
{
    put_s(put_s(buf, EN_NOTE_NAME[(root % 12 + 12) % 12]), Q_NAME[q < 0 || q >= Q_NQ ? 0 : q]);
    return buf;
}

/* progressions: chords as (semitones above the key's tonic, quality, beats); mood 0 jazzy .. 100 melancholy;
 * minor: the key is the minor tonic (the melody's scale too) */
typedef struct {
    int8_t root, q, beats;
} pchord_t;
typedef struct {
    uint8_t mood, minor, n;
    pchord_t c[4];
} prog_t;
static const prog_t PROGS[] = {
    {8, 0, 3, {{2, Q_M9, 4}, {7, Q_DOM13, 4}, {0, Q_MAJ9, 8}}},                       /* ii V I */
    {18, 0, 4, {{0, Q_MAJ7, 4}, {9, Q_M9, 4}, {2, Q_M9, 4}, {7, Q_DOM13, 4}}},         /* I vi ii V */
    {12, 0, 4, {{0, Q_MAJ9, 4}, {9, Q_7B9, 4}, {2, Q_M9, 4}, {7, Q_DOM13, 4}}},        /* the turnaround */
    {22, 0, 4, {{2, Q_M9, 4}, {7, Q_9SUS, 4}, {0, Q_MAJ9, 4}, {5, Q_MAJ9, 4}}},        /* ii V9sus I IV */
    {28, 0, 2, {{0, Q_69, 8}, {5, Q_MAJ9, 8}}},                                         /* a floating I IV */
    {34, 0, 4, {{5, Q_MAJ9, 4}, {4, Q_M7, 4}, {2, Q_M9, 4}, {0, Q_MAJ7, 4}}},          /* IV iii ii I */
    {44, 0, 4, {{0, Q_MAJ7, 4}, {4, Q_M7, 4}, {5, Q_MAJ9, 4}, {5, Q_M6, 4}}},          /* I iii IV iv */
    {50, 0, 4, {{5, Q_MAJ7, 4}, {7, Q_DOM13, 4}, {4, Q_M7, 4}, {9, Q_M9, 4}}},         /* the royal road */
    {56, 0, 4, {{5, Q_MAJ9, 4}, {5, Q_M6, 4}, {4, Q_M7, 4}, {9, Q_7B9, 4}}},           /* IV iv iii VI */
    {64, 1, 2, {{0, Q_M9, 8}, {5, Q_DOM9, 8}}},                                         /* dorian i IV */
    {70, 1, 4, {{8, Q_MAJ7, 4}, {7, Q_M7, 4}, {5, Q_M9, 4}, {0, Q_M9, 4}}},            /* VI v iv i */
    {74, 1, 4, {{2, Q_M7B5, 4}, {7, Q_7B9, 4}, {0, Q_M9, 4}, {8, Q_MAJ9, 4}}},         /* ii V i VI */
    {78, 1, 3, {{2, Q_M7B5, 4}, {7, Q_7B9, 4}, {0, Q_M9, 8}}},                         /* minor ii V i */
    {84, 1, 2, {{0, Q_M9, 8}, {5, Q_M9, 8}}},                                           /* i iv */
    {88, 1, 4, {{0, Q_M9, 4}, {8, Q_MAJ9, 4}, {5, Q_M9, 4}, {7, Q_7B9, 4}}},           /* i VI iv V */
    {95, 1, 4, {{0, Q_M11, 4}, {10, Q_69, 4}, {8, Q_MAJ7, 4}, {7, Q_7B9, 4}}},         /* i bVII bVI V */
};
#define NPROGS (sizeof PROGS / sizeof PROGS[0])

/* grooves: a bar each. Kick: X hard, x soft. Snare: X, g a ghost. Hats: x, s soft, o open */
typedef struct {
    const char *k, *s, *h;
} groove_t;
static const groove_t GROOVES[] = {
    {"X.........X.....", "....X.......X...", "x.x.x.x.x.x.x.x."},   /* the classic */
    {"X......x..X.....", "....X.......X..g", "x.x.x.xsx.x.x.xs"},   /* a kick pushed */
    {"X.......X..x....", "....X.......X...", "x.xsx.x.x.xsx.o."},   /* lazy */
    {"X..x......X.....", "....X..g....X...", "xsxsxsxsxsxsxsxs"},   /* bounce, sixteenths */
    {"X.........x..x..", "....X.......X...", "x.x.x.x.x.x.x.x."},   /* a late kick */
    {"X.....x...X.....", "....X.......X.g.", "x.x.x.x.x.x.o.x."},   /* dilla-ish */
    {"X.......x.x.....", "....X.......X...", "x...x...x...x..."},   /* sparse quarters */
    {"X..x...x..X.....", "....X.......X...", "x.xsx.xsx.xsx.xs"},   /* busy */
};
#define NGROOVES (sizeof GROOVES / sizeof GROOVES[0])

/* comping: hits in a chord's first bar (step, length in steps, velocity in %); push: the next chord comes an
 * eighth early */
typedef struct {
    int8_t n, push;
    int8_t st[4], len[4], vel[4];
} comp_t;
static const comp_t COMPS[] = {
    {1, 0, {0}, {15}, {80}},
    {2, 0, {0, 6}, {6, 9}, {80, 62}},
    {2, 1, {0, 10}, {9, 4}, {80, 60}},
    {3, 0, {0, 3, 10}, {3, 6, 5}, {78, 60, 66}},
    {2, 0, {0, 8}, {8, 7}, {80, 66}},
    {2, 1, {0, 7}, {7, 6}, {80, 58}},
};
#define NCOMPS (sizeof COMPS / sizeof COMPS[0])

/* the bass: notes in a chord's first bar: R the root, F the fifth, O the octave, A the way to the next root */
typedef struct {
    const char *n;
    int8_t len[16];
} bpat_t;
static const bpat_t BPATS[] = {
    {"R.........R...A.", {7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 2}},
    {"R.............A.", {13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2}},
    {"R.......F.....A.", {7, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 2}},
    {"R......R..F.....", {6, 0, 0, 0, 0, 0, 0, 2, 0, 0, 5, 0, 0, 0, 0}},
    {"R.........O.R...", {8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 4, 0, 0}},
};
#define NBPATS (sizeof BPATS / sizeof BPATS[0])

/* ------------------------------------------------------------- titles --- */
static const char *const T_ADJ[] = {
    "sleepy", "quiet", "dusty", "warm", "hazy", "slow", "soft", "faded", "lazy", "rainy", "cozy", "sunday",
    "midnight", "golden", "blue", "velvet", "paper", "distant", "little", "lonely", "gentle", "misty", "mellow",
    "amber", "cloudy", "silver", "honey", "late", "early", "old", "green", "small"};
static const char *const T_NOUN[] = {
    "window", "coffee", "letters", "streets", "rooftop", "cassette", "notebook", "train", "sweater", "moon",
    "garden", "kettle", "lamplight", "umbrella", "bicycle", "polaroid", "candle", "bookshop", "alley", "river",
    "cat", "clouds", "records", "tea", "autumn", "snow", "porch", "radio", "pillow", "morning", "sunset",
    "station", "ferry", "attic", "postcard", "balcony", "laundry", "noodles", "puddles", "headphones"};
static const char *const T_TIME[] = {"dawn", "dusk", "2am", "4am", "noon", "midnight", "6pm", "sunrise"};
#define NA(a) (sizeof a / sizeof a[0])

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

void en_title(uint32_t seed, char *b)
{
    uint32_t h = en_track_rand(seed, 77u);
    const char *a = T_ADJ[h % NA(T_ADJ)], *n = T_NOUN[(h >> 8) % NA(T_NOUN)];
    const char *n2 = T_NOUN[(h >> 16) % NA(T_NOUN)];
    switch ((h >> 26) % 8u) {
    case 0: case 1: case 2: case 3:                  /* "sleepy window" */
        put_s(put_s(put_s(b, a), " "), n);
        break;
    case 4:                                          /* "tea at 2am" */
        put_s(put_s(put_s(b, n), " at "), T_TIME[(h >> 20) % NA(T_TIME)]);
        break;
    case 5:                                          /* "coffee & clouds" */
        if (n2 == n)
            n2 = T_NOUN[((h >> 16) + 1u) % NA(T_NOUN)];
        put_s(put_s(put_s(b, n), " & "), n2);
        break;
    case 6:                                          /* "the sleepy kettle" */
        put_s(put_s(put_s(put_s(b, "the "), a), " "), n);
        break;
    default:                                         /* "rooftop (dusk)" */
        put_s(put_s(put_s(put_s(b, n), " ("), T_TIME[(h >> 20) % NA(T_TIME)]), ")");
        break;
    }
}
uint8_t en_label_colour(uint32_t seed) { return (uint8_t)(en_track_rand(seed, 5u) % 12u); }

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
volatile uint8_t en_playing, en_phase, en_repeat_on;
volatile uint32_t en_track_no, en_track_seed, en_seed_base = 0x4C4F4649u, en_track_ms, en_track_len_ms;
volatile uint8_t en_key, en_minor, en_bpm = 80, en_chord_root, en_chord_q, en_section;
volatile uint16_t en_chord_pcs;
volatile float en_vu_l, en_vu_r, en_kick_glow, en_needle;
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
static float trand(void) { return (float)(xs(&trng) >> 8) * (1.0f / 16777216.0f); }   /* the track's own */
static int tirand(int n) { return n > 0 ? (int)((xs(&trng) >> 8) % (uint32_t)n) : 0; }
static inline float noise(void) { return (float)(int32_t)xs(&nrng) * (1.0f / 2147483648.0f); }

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

/* ---------------------------------------------------------------- keys --- */
#define NKV 12
typedef struct {
    float pc, pt, ic, it, ratio;
    float amp, dec, rel, att, att_inc;
    float idx, idx_dec, idx_floor, tine, tine_dec;
    float gl, gr;
    int32_t gate;                                /* samples until the damper; < 0: held by the player */
    uint8_t note, on, player;
    uint32_t age;
} kv_t;
static kv_t kv[NKV];
static uint32_t kv_clock;

static void key_on(int note, float vel, int32_t gate, int player)
{
    kv_t *v = 0;
    int i, timbre = STYLES[style_i].keys;
    float f, ratio, tdec;
    uint32_t oldest = 0;
    for (i = 0; i < NKV; i++)                    /* a free voice, else the one quietest-oldest */
        if (!kv[i].on) {
            v = &kv[i];
            break;
        }
    if (!v)
        for (i = 0; i < NKV; i++) {
            uint32_t a = kv_clock - kv[i].age + (kv[i].gate == 0 ? 0x10000000u : 0u);
            if (!kv[i].player && a >= oldest) {
                oldest = a;
                v = &kv[i];
            }
        }
    if (!v)
        v = &kv[0];
    while (note < 36)
        note += 12;
    while (note > 96)
        note -= 12;
    f = mtof((float)note);
    ratio = timbre == 2 ? 2.0f : 1.0f;
    v->ic = f / EN_SR;
    v->ratio = ratio;
    v->it = fm_minf(f * (timbre == 2 ? 3.0f : 7.0f), 7000.0f) / EN_SR;
    v->pc = v->pt = 0.0f;
    vel = fm_clampf(vel, 0.05f, 1.0f);
    tdec = fm_clampf(2.6f * fm_sqrtf(261.6f / f), 0.9f, 4.5f);
    if (timbre == 0) {                           /* Rhodes: the bark with the velocity, a tine's ping */
        v->idx = 0.10f + 0.28f * vel;
        v->idx_floor = 0.035f;
        v->idx_dec = tau(0.25f);
        v->tine = 0.06f * vel;
        v->tine_dec = tau(0.03f);
        v->dec = tau(tdec);
    } else if (timbre == 1) {                    /* Wurli: reedier, held */
        v->idx = 0.18f + 0.30f * vel;
        v->idx_floor = 0.08f;
        v->idx_dec = tau(0.35f);
        v->tine = 0.0f;
        v->tine_dec = 0.0f;
        v->dec = tau(tdec * 0.8f);
    } else {                                     /* a felt piano: soft, a hammer's overtone */
        v->idx = 0.05f + 0.10f * vel;
        v->idx_floor = 0.01f;
        v->idx_dec = tau(0.2f);
        v->tine = 0.10f * vel;
        v->tine_dec = tau(0.25f);
        v->dec = tau(tdec * 0.65f);
    }
    v->rel = tau(timbre == 2 ? 0.28f : 0.16f);
    v->amp = vel * (0.55f + 0.45f * vel);
    v->att = 0.0f;
    v->att_inc = 1.0f / (0.003f * EN_SR);
    {
        float p = fm_clampf((float)(note - 60) * 0.025f, -0.35f, 0.35f);   /* low left, high right, a little */
        v->gl = 0.75f - 0.9f * p;
        v->gr = 0.75f + 0.9f * p;
    }
    v->gate = gate;
    v->note = (uint8_t)note;
    v->on = 1;
    v->player = (uint8_t)player;
    v->age = kv_clock++;
}
static void key_off_player(int note)
{
    int i;
    for (i = 0; i < NKV; i++)
        if (kv[i].on && kv[i].player && kv[i].note == note && kv[i].gate < 0)
            kv[i].gate = 0;
}

/* ---------------------------------------------------------------- bass --- */
static struct {
    float ph, inc, tinc, amp, peak, sus, idx;
    float lp;
    int32_t gate;
    uint8_t on, atk;
    int note;
} bs;
static void bass_on(int note, float vel, int32_t gate)
{
    float f = mtof((float)note);
    if (!bs.on || bs.amp < 0.05f)                /* from silence: start there; legato: glide */
        bs.inc = f / EN_SR;
    bs.tinc = f / EN_SR;
    bs.on = 1;
    bs.atk = 1;
    bs.gate = gate;
    bs.peak = vel;
    bs.sus = vel * 0.62f;
    bs.idx = 0.10f + 0.10f * vel;
    bs.note = note;
}

/* --------------------------------------------------------------- drums --- */
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
static float duck;                               /* the kick's push on the rest (a soft side-chain) */

static void kick_hit(float v)
{
    kick.ph = 0.0f;
    kick.f = 150.0f;
    kick.fend = 52.0f + 6.0f * frand();
    kick.pdec = tau(0.028f);
    kick.amp = v;
    kick.adec = tau(0.15f + 0.04f * frand());
    kick.click = v * 0.25f;
    duck = 1.0f;
    en_kick_glow = 1.0f;
}
static void snare_hit(float v)
{
    snare.ph = 0.0f;
    snare.inc = (180.0f + 10.0f * frand()) / EN_SR;
    snare.tamp = v * 0.55f;
    snare.tdec = tau(0.05f);
    snare.namp = v;
    snare.ndec = tau(0.10f + 0.04f * frand());
}
static void rim_hit(float v)
{
    rim.ph = 0.0f;
    rim.inc = 430.0f / EN_SR;
    rim.amp = v * 0.7f;
    rim.dec = tau(0.025f);
    rim.namp = v;
    rim.ndec = tau(0.010f);
}
static void hat_hit(float v, int open)
{
    hat.amp = v;
    hat.dec = tau(open ? 0.22f : 0.03f + 0.015f * frand());
}

/* -------------------------------------------------------------- melody --- */
#define NMV 2
static struct {
    float ph, inc, amp, att, att_inc, dec, rel, bell, bell_dec, vib, vph;
    int32_t gate;
    uint8_t on;
} mv[NMV];
static float mel_lp;
static int mel_type;                             /* this track's: 0 muted lead, 1 vibraphone */
static void mel_on(int note, float vel, int32_t gate)
{
    int i = mv[0].on && (!mv[1].on || mv[1].amp < mv[0].amp) ? 1 : 0;
    float f = mtof((float)note);
    mv[i].inc = f / EN_SR;
    mv[i].ph = 0.0f;
    mv[i].on = 1;
    mv[i].gate = gate;
    mv[i].vib = 0.0f;
    if (mel_type == 0) {
        mv[i].amp = vel;
        mv[i].att = 0.0f;
        mv[i].att_inc = 1.0f / (0.025f * EN_SR);
        mv[i].dec = tau(1.6f);
        mv[i].rel = tau(0.09f);
        mv[i].bell = 0.0f;
    } else {
        mv[i].amp = vel;
        mv[i].att = 0.0f;
        mv[i].att_inc = 1.0f / (0.002f * EN_SR);
        mv[i].dec = tau(1.4f);
        mv[i].rel = tau(0.35f);
        mv[i].bell = vel * 0.35f;
        mv[i].bell_dec = tau(0.12f);
    }
}

/* -------------------------------------------------------------- spaces --- */
/* the room: four delay lines, a Householder mix, damped; two diffusers before */
#define RV0 1123
#define RV1 1361
#define RV2 1597
#define RV3 1801
#define AP0 225
#define AP1 341
static float rv_buf[RV0 + RV1 + RV2 + RV3 + AP0 + AP1] EN_POOL;
static float *rv_d[4];
static const int RV_N[4] = {RV0, RV1, RV2, RV3};
static int rv_p[4], ap_p[2];
static float *ap_d[2];
static const int AP_N[2] = {AP0, AP1};
static float rv_lp[4];

/* the melody's echo: a dotted eighth, darkening */
#define EC_N 32768
static int16_t ec_buf[EC_N] EN_POOL;
static int ec_w;
static float ec_lp, ec_len = 20000.0f;

/* tape wow and flutter: the bus through a moving delay */
#define WB_N 2048
static float wb_l[WB_N] EN_POOL, wb_r[WB_N] EN_POOL;
static int wb_w;
static float wow_ph, wow2_ph, flut_ph, wow_rnd, wow_rnd_t;

/* -------------------------------------------------------- the sequence --- */
#define BLK 256
#define NEV 48
enum { E_KEY, E_BASS, E_KICK, E_SNARE, E_RIM, E_HAT, E_OHAT, E_MEL };
typedef struct {
    int32_t wait;
    uint8_t type, note, used;
    float vel;
    int32_t gate;
} ev_t;
static ev_t evq[NEV];

static void sched(int type, int32_t wait, int note, float vel, int32_t gate)
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
            return;
        }
}

/* the track */
static struct {
    const prog_t *pg;
    int prog;
    int cyc_beats;                               /* the progression's length */
    int groove, comp, bpat, push;
    float push_p, ghost_p;
    int tempo_off;
    int bars, intro, outro;                      /* in bars */
    uint8_t sect[40];                            /* a bar each 4: A 1, B 2, breakdown 3 */
    int8_t mel_deg[32], mel_len[32];             /* the motif: two bars of sixteenths (-1 rest) */
    uint8_t mel_vel[32];
    uint8_t scratch;                             /* this record has a scratch: a tick once a turn */
    uint8_t pick_mood;                           /* the mood it was picked at */
    int bar, step;                               /* where the sequence is */
    float step_left;                             /* samples to the next sixteenth */
    int voicing[4];                              /* the keys' notes now */
    int chord_i;
    int pushed;
} tk;

static float step_len = 5512.0f;                 /* samples a sixteenth */
static uint32_t trk_samples;                     /* into the track */
static int runout_left;                          /* the run-out: samples to the next track */
static float runout_tick;                        /* to the next turn's thump */
static float fade = 0.0f, fade_t = 1.0f, fade_rate = 0.0f;   /* the music's level (the transitions) */
static float sweep = 0.15f, sweep_t = 1.0f;      /* the bus filter's opening (the intro's, the outro's) */
static int skipping;
static float needle_t;                           /* 1 in the groove, 0 lifted */
static float drop_thump;

static int cur_bpm(void)
{
    int b = par[P_TEMPO] + tk.tempo_off;
    return b < 60 ? 60 : b > 100 ? 100 : b;
}
static void tempo_set(void)
{
    int b = cur_bpm();
    en_bpm = (uint8_t)b;
    step_len = EN_SR * 15.0f / (float)b;
    en_track_len_ms = (uint32_t)((float)(tk.bars * 16) * step_len * (1000.0f / EN_SR));
    ec_len = fm_minf(step_len * 3.0f, (float)(EC_N - 4));
}

static int pick_prog(int mood, int minor_only)
{
    float w[NPROGS], tot = 0.0f, r;
    int i;
    for (i = 0; i < (int)NPROGS; i++) {
        float d = ((float)PROGS[i].mood - (float)mood) * (1.0f / 18.0f);
        w[i] = (minor_only >= 0 && PROGS[i].minor != minor_only) ? 0.0f : 1.0f / (1.0f + d * d * d * d);
        if (i == tk.prog && tk.pg)               /* not the same one again */
            w[i] *= 0.15f;
        tot += w[i];
    }
    r = trand() * tot;
    for (i = 0; i < (int)NPROGS; i++) {
        r -= w[i];
        if (r <= 0.0f)
            return i;
    }
    return (int)NPROGS - 1;
}
static void use_prog(int i)
{
    int k;
    tk.prog = i;
    tk.pg = &PROGS[i];
    tk.cyc_beats = 0;
    for (k = 0; k < tk.pg->n; k++)
        tk.cyc_beats += tk.pg->c[k].beats;
    en_minor = tk.pg->minor;
    tk.pick_mood = (uint8_t)par[P_MOOD];
}

static void make_motif(void)
{
    int s, d = 2 + tirand(4), left = 0;
    float dens = 0.35f + 0.25f * trand();
    for (s = 0; s < 32; s++) {
        tk.mel_deg[s] = -1;
        tk.mel_len[s] = 0;
    }
    for (s = 0; s < 28; s += 2) {                /* eighths; the last half bar breathes */
        if (left > 0) {
            left -= 2;
            continue;
        }
        if (trand() < dens || s == 0) {
            static const int8_t MOVE[8] = {-2, -1, -1, 0, 1, 1, 2, -3};
            int at = s + (trand() < 0.15f ? 1 : 0);
            d += MOVE[tirand(8)];
            d = d < 0 ? 1 : d > 8 ? 7 : d;
            tk.mel_deg[at] = (int8_t)d;
            tk.mel_len[at] = (int8_t)(trand() < 0.6f ? 2 : trand() < 0.6f ? 4 : 6);
            tk.mel_vel[at] = (uint8_t)(55 + tirand(40));
            left = tk.mel_len[at] - 2;
        }
    }
}

static void plan(void)                           /* the sections: intro, A and B (and a breakdown), outro */
{
    float bar_s = step_len * 16.0f / EN_SR, want;
    int body, i, n4, br;
    want = (float)LEN_MIN[par[P_LENGTH] < 0 ? 0 : par[P_LENGTH] > 4 ? 4 : par[P_LENGTH]] * 60.0f * (0.85f + 0.3f * trand());
    tk.bars = ((int)(want / bar_s) + 2) / 4 * 4;
    if (tk.bars < 16)
        tk.bars = 16;
    if (tk.bars > 160)
        tk.bars = 160;
    tk.intro = tk.bars >= 32 ? 4 : 2;
    tk.outro = 4;
    body = tk.bars - tk.intro - tk.outro;
    n4 = body / 4;
    br = n4 >= 8 ? 2 + tirand(n4 - 5) : -1;      /* a breakdown, a 4-bar block somewhere in the middle */
    for (i = 0; i < n4 && i < 40; i++) {
        int phrase = i / 2;
        tk.sect[i] = (uint8_t)(i == br ? 3 : (phrase & 1) ? 2 : 1);
        if (i == n4 - 1 || i == n4 - 2)
            tk.sect[i] = 2;                      /* the last eight bars with the melody */
    }
}

static void new_track(void)
{
    uint32_t seed;
    en_track_no++;
    seed = en_track_rand(en_seed_base, en_track_no);
    en_track_seed = seed;
    trng = seed;
    en_key = (uint8_t)tirand(12);
    use_prog(pick_prog(par[P_MOOD], -1));
    {
        uint16_t gm = STYLES[style_i].grooves;
        int tries = 0;
        do
            tk.groove = tirand((int)NGROOVES);
        while (!(gm >> tk.groove & 1u) && ++tries < 32);
    }
    tk.comp = tirand((int)NCOMPS);
    tk.bpat = tirand((int)NBPATS);
    tk.push = COMPS[tk.comp].push;
    tk.push_p = 0.4f + 0.5f * trand();
    tk.ghost_p = 0.1f + 0.25f * trand();
    tk.tempo_off = tirand(7) - 3;
    mel_type = STYLES[style_i].mel == 2 ? tirand(2) : STYLES[style_i].mel;
    tk.scratch = (uint8_t)(trand() < 0.45f);
    make_motif();
    tempo_set();
    plan();
    tempo_set();
    tk.bar = 0;
    tk.step = 0;
    tk.step_left = 0.0f;
    tk.chord_i = -1;
    tk.pushed = 0;
    trk_samples = 0;
    en_track_ms = 0;
    en_phase = PH_INTRO;
    en_section = 0;
    sweep = 0.35f;
    sweep_t = 0.4f;
    fade = 0.0f;
    fade_t = 1.0f;
    fade_rate = 1.0f / (2.5f * EN_SR);
    skipping = 0;
    en_tracks_played++;
}

/* the keys' notes for a chord: its four, voice-led from the last (each moving as little as it can), in 52..76 */
static void voice(int root_pc, int q)
{
    int pcs[4], best[4] = {60, 64, 67, 71}, i, inv, bestd = 1 << 30, prev = 0;
    for (i = 0; i < 4; i++) {
        pcs[i] = (root_pc + Q_VOICE[q][i]) % 12;
        prev += tk.voicing[i];
    }
    prev /= 4;
    if (prev < 56 || prev > 70)
        prev = 63;
    for (inv = 0; inv < 4; inv++) {              /* each tone at the bottom in turn, the rest stacked closely */
        int n[4], j, base = pcs[inv], lo, d = 0, mean = 0;
        lo = 52 + ((base - 52) % 12 + 12) % 12;
        for (j = 0; j < 4; j++) {
            int pc = pcs[(inv + j) % 4];
            int x = j ? n[j - 1] + ((pc - n[j - 1]) % 12 + 12) % 12 : lo;
            if (j && x == n[j - 1])
                x += 12;
            n[j] = x;
        }
        for (j = 0; j < 4; j++)
            mean += n[j];
        mean /= 4;
        while (mean > prev + 6) {
            for (j = 0; j < 4; j++)
                n[j] -= 12;
            mean -= 12;
        }
        while (mean < prev - 6) {
            for (j = 0; j < 4; j++)
                n[j] += 12;
            mean += 12;
        }
        for (j = 0; j < 4; j++) {
            int k, m = 99;
            for (k = 0; k < 4; k++) {            /* each new note to its nearest old one */
                int e = n[j] - tk.voicing[k];
                e = e < 0 ? -e : e;
                m = e < m ? e : m;
            }
            d += m;
        }
        d += (mean - 63 > 0 ? mean - 63 : 63 - mean);   /* and home towards the middle */
        if (n[0] < 48 || n[3] > 80)
            d += 40;
        if (d < bestd) {
            bestd = d;
            for (j = 0; j < 4; j++)
                best[j] = n[j];
        }
    }
    for (i = 0; i < 4; i++)
        tk.voicing[i] = best[i];
}

static int chord_at(int beat, int *start, int *len)   /* the progression's chord at this beat of the track */
{
    int b = beat % tk.cyc_beats, k, at = 0;
    for (k = 0; k < tk.pg->n; k++) {
        if (b < at + tk.pg->c[k].beats) {
            *start = beat - (b - at);
            *len = tk.pg->c[k].beats;
            return k;
        }
        at += tk.pg->c[k].beats;
    }
    *start = beat;
    *len = 4;
    return 0;
}

/* samples the step is late: an eighth-note swing (the off-beat eighth up to 0.6 of a sixteenth late, two thirds
 * of the way to a triplet's), the sixteenths around it half that */
static float swing_off(int step)
{
    static const float W[4] = {0.0f, 0.3f, 0.6f, 0.3f};
    return W[step & 3] * step_len * (float)par[P_SWING] * 0.01f;
}
static int32_t hum(float ms) { return (int32_t)((frand() * 2.0f - 1.0f) * ms * 44.1f); }

static void play_chord(int k, int32_t at, int len_steps, float vel)
{
    int root = (en_key + tk.pg->c[k].root) % 12, i;
    float roll = (8.0f + 14.0f * frand()) * 44.1f;
    if (k != tk.chord_i) {
        tk.chord_i = k;
        voice(root, tk.pg->c[k].q);
        en_chord_root = (uint8_t)root;
        en_chord_q = (uint8_t)tk.pg->c[k].q;
        {
            uint16_t m = 0;
            for (i = 0; i < 4; i++)
                m |= (uint16_t)(1u << (tk.voicing[i] % 12));
            en_chord_pcs = (uint16_t)(m | 1u << root);
        }
    }
    for (i = 0; i < 4; i++) {
        float v = vel * (0.85f + 0.2f * frand()) * (i == 3 ? 1.08f : 1.0f);
        int32_t g = (int32_t)((float)len_steps * step_len) - (int32_t)(roll * (float)i) + 400;
        sched(E_KEY, at + (int32_t)(roll * (float)i), tk.voicing[i], v, g);
    }
}

/* the melody's ladder for this chord: its tones and its ninth, from G4 up */
static int mel_note(int deg)
{
    int k = tk.chord_i < 0 ? 0 : tk.chord_i, root = (en_key + tk.pg->c[k].root) % 12, lad[8], i, n = 0, oct;
    lad[n++] = root;
    for (i = 0; i < 4; i++) {
        int pc = (root + Q_VOICE[tk.pg->c[k].q][i]) % 12, j, dup = 0;
        for (j = 0; j < n; j++)
            dup |= lad[j] == pc;
        if (!dup)
            lad[n++] = pc;
    }
    for (i = 1; i < n; i++) {                    /* ascending from G */
        int x = lad[i], j = i - 1;
        while (j >= 0 && (lad[j] - 7 + 12) % 12 > (x - 7 + 12) % 12) {
            lad[j + 1] = lad[j];
            j--;
        }
        lad[j + 1] = x;
    }
    oct = deg / n;
    i = 67 + (lad[deg % n] - 7 + 12) % 12 + 12 * oct;
    return i > 86 ? i - 12 : i;
}

static void seq_step(void)
{
    int bar = tk.bar, s = tk.step, beat = bar * 4 + s / 4, start, len, k, pos, sect, in_body;
    int32_t sw = (int32_t)swing_off(s);
    float dv = 0.6f + 0.4f * (float)par[P_DRUMS] * 0.01f;
    const groove_t *g = &GROOVES[tk.groove];
    const style_t *st = &STYLES[style_i];
    in_body = bar >= tk.intro && bar < tk.bars - tk.outro;
    sect = bar < tk.intro ? 0 : in_body ? tk.sect[(bar - tk.intro) / 4 < 40 ? (bar - tk.intro) / 4 : 39] : 4;
    en_section = (uint8_t)sect;
    if (s == 0) {
        en_phase = (uint8_t)(bar < tk.intro ? PH_INTRO : in_body ? PH_BODY : PH_OUTRO);
        if (bar == tk.intro)                     /* the intro opens: half way, then all of it with the drums */
            sweep_t = 1.0f;
        else if (bar == tk.intro / 2 && bar)
            sweep_t = 0.6f;
        if (bar == tk.bars - 2 && !en_repeat_on) {   /* the outro's last two bars: the filter closes, a fade */
            sweep_t = 0.18f;
            fade_t = 0.0f;
            fade_rate = 1.0f / (step_len * 32.0f);
        }
        if (in_body && (bar - tk.intro) % 4 == 0 && bar > tk.intro) {   /* the mood moved a long way: re-picked */
            int dm = par[P_MOOD] - tk.pick_mood;
            if (dm > 30 || dm < -30) {
                int was = tk.pg->minor, np = pick_prog(par[P_MOOD], -1);
                if (PROGS[np].minor != was)      /* to the relative key: the same notes */
                    en_key = (uint8_t)((en_key + (was ? 3 : 9)) % 12);
                use_prog(np);
                tk.chord_i = -1;
            }
            if (trand() < 0.3f) {                /* the motif changes a little */
                int at = tirand(14) * 2;
                if (tk.mel_deg[at] >= 0)
                    tk.mel_deg[at] = (int8_t)(1 + tirand(7));
            }
        }
    }
    /* keys */
    k = chord_at(beat, &start, &len);
    pos = (beat - start) * 4 + s % 4;            /* steps into the chord */
    if (sect != 4 || bar < tk.bars - 1) {
        const comp_t *c = &COMPS[tk.comp];
        int i, lsteps = len * 4, p16 = pos % 16, second = pos >= 16;
        float kv0 = 0.45f + 0.35f * (float)par[P_KEYS] * 0.01f;
        if (tk.pushed && pos == 0) {
            tk.pushed = 0;                       /* it came early */
        } else {
            for (i = 0; i < c->n; i++)
                if (c->st[i] == p16 && pos < lsteps && (!second || c->st[i] == 0 || frand() < 0.5f)) {
                    int l = c->len[i];
                    if (pos + l > lsteps)
                        l = lsteps - pos;
                    play_chord(k, sw + hum(4.0f), l, kv0 * (float)c->vel[i] * 0.01f * (second ? 0.85f : 1.0f));
                }
        }
        if (tk.push && pos == lsteps - 2 && frand() < tk.push_p && sect != 0) {   /* the next one an eighth early */
            int st2, l2, nk = chord_at(beat + 1, &st2, &l2);
            if (nk != k) {
                play_chord(nk, sw + hum(4.0f), 2 + 14, kv0 * 0.8f);
                tk.pushed = 1;
            }
        }
    }
    /* bass */
    if (sect != 0 && bar < tk.bars - 1 && par[P_BASS] > 0) {
        const bpat_t *b = &BPATS[tk.bpat];
        int p16 = pos % 16;
        char c = pos < len * 4 ? b->n[p16] : '.';
        if (sect == 3 && bar % 4 < 2)
            c = p16 == 0 ? 'R' : '.';            /* the breakdown: roots only */
        if (c != '.') {
            int root = (en_key + tk.pg->c[k].root) % 12, n = 33 + (root - 9 + 12) % 12, l = b->len[p16];
            if (c == 'A' && pos != len * 4 - 2)
                c = frand() < 0.5f ? 'R' : 'F';      /* not the chord's last bar: no next root to lead to */
            if (c == 'F')
                n += 7;
            else if (c == 'O')
                n += 12;
            else if (c == 'A') {
                int st2, l2, nk = chord_at(beat + 1, &st2, &l2);
                int nr = 33 + ((en_key + tk.pg->c[nk].root) % 12 - 9 + 12) % 12;
                if (nk == k)
                    nr = n + 7;
                n = nr + (frand() < 0.5f ? -1 : 1);   /* a semitone off the next root */
            }
            if (n > 50)
                n -= 12;
            if (pos + l <= len * 4 + 1)
                sched(E_BASS, sw + hum(3.0f) + 120, n, 0.75f + 0.25f * frand(), (int32_t)((float)l * step_len) - 300);
        }
    }
    /* drums: the body and the outro's first half; a fill at the end of a phrase; out for the breakdown */
    if ((sect == 1 || sect == 2 || (sect == 4 && bar < tk.bars - 2)) && par[P_DRUMS] > 0) {
        int phr_end = (bar - tk.intro) % 8 == 7 && sect != 4, stop = 0;
        char kc = g->k[s], sc = g->s[s], hc = g->h[s];
        uint32_t hb = en_track_rand(en_track_seed, (uint32_t)bar);
        if (phr_end && (hb & 7u) == 0)
            stop = s >= 8;                       /* the drums stop for the bar's second half */
        if (phr_end && (hb >> 3 & 3u) == 1 && s >= 12) {   /* a fill: no kick, a ghost, an open hat */
            kc = '.';
            sc = s == 14 ? 'g' : sc;
            hc = s == 14 ? 'o' : hc;
        }
        if (!stop) {
            if (kc != '.')
                sched(E_KICK, sw + hum(2.0f) + 60, 0, dv * (kc == 'X' ? 0.95f : 0.6f) * (0.9f + 0.1f * frand()), 0);
            else if (s == 14 && frand() < tk.ghost_p * 0.5f)
                sched(E_KICK, sw + hum(2.0f), 0, dv * 0.45f, 0);
            if (sc != '.' || ((s & 1) && frand() < tk.ghost_p * 0.25f)) {
                float v = sc == 'X' ? 0.85f : 0.28f;
                int32_t back = (int32_t)((float)st->back * 44.1f);
                int rimq = st->snare == 1 || (st->snare == 2 && (bar & 2));
                sched(rimq ? E_RIM : E_SNARE, sw + back + hum(5.0f), 0, dv * v * (0.88f + 0.12f * frand()), 0);
            }
            if (hc != '.') {
                float v = hc == 's' ? 0.35f : 0.6f;
                if (s % 4 == 2)
                    v *= 1.12f;              /* the off-beat eighths lean */
                sched(hc == 'o' ? E_OHAT : E_HAT, sw + hum(4.0f), 0, dv * v * (0.75f + 0.35f * frand()), 0);
            } else if ((s & 1) && frand() < (float)st->hats * 0.004f) {
                sched(E_HAT, sw + hum(4.0f), 0, dv * 0.22f * (0.7f + 0.6f * frand()), 0);
            }
        }
    }
    /* the intro's last bar: soft hats lead in */
    if (sect == 0 && bar == tk.intro - 1 && s % 2 == 0 && par[P_DRUMS] > 0)
        sched(E_HAT, sw + hum(4.0f), 0, dv * (0.15f + 0.03f * (float)s), 0);
    /* melody: B sections and the breakdown; with Melody high, the second half of A too */
    if (par[P_MELODY] > 0 && (sect == 2 || sect == 3 || (sect == 1 && par[P_MELODY] > 60 && (bar - tk.intro) % 8 >= 4))) {
        int m = (bar & 1) * 16 + s;
        if (tk.mel_deg[m] >= 0) {
            float v = (float)tk.mel_vel[m] * 0.01f * (0.3f + 0.7f * (float)par[P_MELODY] * 0.01f);
            sched(E_MEL, sw + hum(6.0f) + 200, mel_note(tk.mel_deg[m]), v,
                  (int32_t)((float)tk.mel_len[m] * step_len * 0.9f));
        }
    }
    if (s % 4 == 0)
        en_beats++;
    /* on */
    if (++tk.step >= 16) {
        tk.step = 0;
        tk.bar++;
        if (tk.bar >= tk.bars - tk.outro && en_repeat_on)
            tk.bar = tk.intro;                   /* repeat: the body again */
        if (tk.bar >= tk.bars) {                 /* the needle in the run-out */
            en_phase = PH_RUNOUT;
            en_section = 5;
            runout_left = (int)(EN_SR * 3.6f);
            runout_tick = EN_SR * 0.3f;
        }
    }
    tempo_set();
}

/* ------------------------------------------------------------- control --- */
static void quiet(void)
{
    int i;
    for (i = 0; i < NKV; i++)
        kv[i].on = 0;
    for (i = 0; i < NMV; i++)
        mv[i].on = 0;
    for (i = 0; i < NEV; i++)
        evq[i].used = 0;
    bs.on = 0;
    bs.amp = 0.0f;
    kick.amp = kick.click = snare.tamp = snare.namp = rim.amp = rim.namp = hat.amp = 0.0f;
}

static void play_on(int on)
{
    int i;
    if (on && !en_playing) {
        en_playing = 1;
        drop_thump = 1.0f;                       /* the needle lands */
        if (fade_t > 0.5f || en_phase == PH_INTRO) {
            fade = 0.0f;
            fade_rate = 1.0f / (0.6f * EN_SR);
        }
    } else if (!on && en_playing) {
        en_playing = 0;
        for (i = 0; i < NEV; i++)
            evq[i].used = 0;
        for (i = 0; i < NKV; i++)
            if (kv[i].on && !kv[i].player)
                kv[i].gate = 0;
        for (i = 0; i < NMV; i++)
            mv[i].gate = 0;
        bs.gate = 0;
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
            if (en_phase != PH_RUNOUT && !skipping) {
                skipping = 1;
                fade_t = 0.0f;
                fade_rate = 1.0f / (1.2f * EN_SR);
                sweep_t = 0.15f;
            }
            break;
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
    }
    for (i = 0; i < 2; i++) {
        ap_d[i] = p;
        p += AP_N[i];
    }
    for (i = 0; i < (int)(sizeof rv_buf / sizeof rv_buf[0]); i++)
        rv_buf[i] = 0.0f;
    for (i = 0; i < EC_N; i++)
        ec_buf[i] = 0;
    for (i = 0; i < WB_N; i++)
        wb_l[i] = wb_r[i] = 0.0f;
    for (i = 0; i < 4; i++)
        tk.voicing[i] = 58 + 3 * i;
    quiet();
    en_track_no = 0;
    new_track();
}

/* -------------------------------------------------------------- render --- */
static float bufl[BLK], bufr[BLK], rsend[BLK], esend[BLK];
static float tone_a = 0.3f, t1l, t2l, t1r, t2r;  /* the bus's low-pass */
static float hp_l, hp_r;                         /* a DC / rumble cut */
static float lev = 0.0f;                         /* the leveller's envelope */
static float trem_ph;
static float hiss_lp, hiss_hp, crk_l, crk_r, crk_lp_l, crk_lp_r, rain_lp, rain_bp, rain_mod, rain_drop, rain_ph;
static float thump, thump_ph;
static int32_t crk_next, pop_next, drop_next;
static int32_t gap(float p)                       /* samples to the next of a random event that comes p a sample */
{
    return p <= 0.0f ? 441000 : (int32_t)fm_minf(-fm_logf(frand() + 1e-7f) / p, 441000.0f) + 1;   /* (10 s at most: a new Dust is heard) */
}
static float vu_acc_l, vu_acc_r;
static uint32_t vu_n;

static void render_keys(uint32_t n)
{
    int i;
    uint32_t j;
    float lv = (float)par[P_KEYS] * 0.01f * 0.36f, tr, tl;
    for (i = 0; i < NKV; i++) {
        kv_t *v = &kv[i];
        float pc, pt, amp, idx, tine, att, mr = v->ratio, fl = v->idx_floor, id = v->idx_dec;
        float gl = v->gl, gr = v->gr, ic = v->ic;
        uint32_t seg, j0 = 0;
        if (!v->on)
            continue;
        pc = v->pc, pt = v->pt, amp = v->amp, idx = v->idx, tine = v->tine, att = v->att;
        while (j0 < n) {                         /* in two runs: held (decaying), then damped (released) */
            float m = v->gate == 0 ? v->rel : v->dec;
            seg = n - j0;
            if (v->gate > 0 && (uint32_t)v->gate < seg)
                seg = (uint32_t)v->gate;
            if (att < 1.0f) {                    /* the first 3 ms: the attack */
                for (j = j0; j < j0 + seg; j++) {
                    float s = sn(pc + sn(pc * mr) * idx) * amp * att + sn(pt) * tine;
                    bufl[j] += s * gl;
                    bufr[j] += s * gr;
                    pc = wrap1(pc + ic);
                    pt = wrap1(pt + v->it);
                    att = fm_minf(att + v->att_inc, 1.0f);
                    amp *= m;
                    idx = fl + (idx - fl) * id;
                    tine *= v->tine_dec;
                }
            } else if (tine > 0.0002f) {         /* the tine still ringing */
                for (j = j0; j < j0 + seg; j++) {
                    float s = sn(pc + sn(pc * mr) * idx) * amp + sn(pt) * tine;
                    bufl[j] += s * gl;
                    bufr[j] += s * gr;
                    pc = wrap1(pc + ic);
                    pt = wrap1(pt + v->it);
                    amp *= m;
                    idx = fl + (idx - fl) * id;
                    tine *= v->tine_dec;
                }
            } else {                             /* the rest: one modulator, one carrier */
                for (j = j0; j < j0 + seg; j++) {
                    float s = sn(pc + sn(pc * mr) * idx) * amp;
                    bufl[j] += s * gl;
                    bufr[j] += s * gr;
                    pc = wrap1(pc + ic);
                    amp *= m;
                    idx = fl + (idx - fl) * id;
                }
                tine = 0.0f;
            }
            if (v->gate > 0)
                v->gate -= (int32_t)seg;
            j0 += seg;
        }
        v->pc = pc, v->pt = pt, v->amp = amp, v->idx = idx, v->tine = tine, v->att = att;
        if (amp < 0.0008f && tine < 0.0008f)
            v->on = 0;
    }
    /* the autopan (a soft tremolo), the room's send, the level */
    tr = sn(trem_ph) * 0.2f;
    trem_ph = wrap1(trem_ph + 3.6f * (float)n / EN_SR);
    tl = lv * (1.0f - tr), tr = lv * (1.0f + tr);
    tl *= 1.0f - 0.22f * duck;
    tr *= 1.0f - 0.22f * duck;
    for (j = 0; j < n; j++) {
        bufl[j] *= tl;
        bufr[j] *= tr;
        rsend[j] += (bufl[j] + bufr[j]) * 0.5f;
    }
}

static void render_bass(uint32_t n)
{
    uint32_t j;
    float lv = (float)par[P_BASS] * 0.01f * 0.30f, a = onepole(700.0f), glide = 1.0f - tau(0.025f);
    float idec = tau(0.08f), dec = tau(0.35f), rel = tau(0.07f), att = 1.0f / (0.006f * EN_SR);
    if (!bs.on)
        return;
    lv *= 1.0f - 0.25f * duck;
    for (j = 0; j < n; j++) {
        float s;
        bs.inc += (bs.tinc - bs.inc) * glide;
        s = sn(bs.ph + sn(bs.ph) * bs.idx) + 0.18f * sn(bs.ph * 2.0f);
        bs.ph = wrap1(bs.ph + bs.inc);
        if (bs.gate > 0) {
            bs.gate--;
            if (bs.atk) {                        /* a quick rise to the peak, then it settles */
                bs.amp += att * bs.peak;
                if (bs.amp >= bs.peak)
                    bs.amp = bs.peak, bs.atk = 0;
            } else {
                bs.amp = bs.sus + (bs.amp - bs.sus) * dec;
            }
        } else {
            bs.amp *= rel;
        }
        bs.idx = 0.02f + (bs.idx - 0.02f) * idec;
        s = s * bs.amp;
        s = s * (1.0f - 0.12f * s * s);          /* a little warmth */
        bs.lp += (s - bs.lp) * a;
        bufl[j] += bs.lp * lv;
        bufr[j] += bs.lp * lv;
    }
    if (bs.gate <= 0 && bs.amp < 0.0005f)
        bs.on = 0;
}

static void render_drums(uint32_t n)
{
    uint32_t j;
    float lv = (float)par[P_DRUMS] * 0.01f * 0.55f;
    float fs = 2.0f * fm_sinf(FM_PI * 1800.0f / EN_SR), fr = 2.0f * fm_sinf(FM_PI * 1700.0f / EN_SR);
    float hha = onepole(7000.0f), hlp = onepole(10000.0f), slp = onepole(5500.0f);
    if (kick.amp < 1e-3f && snare.tamp < 1e-3f && snare.namp < 1e-3f && rim.amp < 1e-3f && rim.namp < 1e-3f &&
        hat.amp < 1e-3f)
        return;
    for (j = 0; j < n; j++) {
        float k = 0.0f, s = 0.0f, h = 0.0f, x;
        if (kick.amp > 1e-3f) {
            kick.f = kick.fend + (kick.f - kick.fend) * kick.pdec;
            kick.ph = wrap1(kick.ph + kick.f / EN_SR);
            k = sn(kick.ph) * kick.amp + noise() * kick.click;
            kick.amp *= kick.adec;
            kick.click *= 0.93f;
        }
        if (snare.tamp > 1e-3f || snare.namp > 1e-3f) {
            float hp;
            x = noise() * snare.namp;
            snare.lo += fs * snare.bd;           /* Chamberlin's state-variable filter: its band, a little high */
            hp = x - snare.lo - 0.9f * snare.bd;
            snare.bd += fs * hp;
            snare.lp += (snare.bd + 0.35f * hp - snare.lp) * slp;
            s = snare.lp * 0.9f + sn(snare.ph) * snare.tamp;
            snare.ph = wrap1(snare.ph + snare.inc);
            snare.tamp *= snare.tdec;
            snare.namp *= snare.ndec;
        }
        if (rim.amp > 1e-3f || rim.namp > 1e-3f) {
            x = noise() * rim.namp;
            rim.lo += fr * rim.bd;
            rim.bd += fr * (x - rim.lo - 0.35f * rim.bd);
            s += rim.bd * 0.55f + sn(rim.ph) * rim.amp;
            rim.ph = wrap1(rim.ph + rim.inc);
            rim.amp *= rim.dec;
            rim.namp *= rim.ndec;
        }
        if (hat.amp > 1e-3f) {
            x = noise();
            hat.h1 += (x - hat.h1) * hha;        /* two high-passes (a one-pole's low taken off), a low-pass */
            x -= hat.h1;
            hat.h2 += (x - hat.h2) * hha;
            x -= hat.h2;
            hat.lp += (x - hat.lp) * hlp;
            hat.lp2 += (hat.lp - hat.lp2) * hlp;
            h = hat.lp2 * hat.amp * 0.8f;
            hat.amp *= hat.dec;
        }
        x = k * 0.7f + s * 0.75f;
        bufl[j] += (x + h * 0.85f) * lv;
        bufr[j] += (x + h * 1.1f) * lv;
        rsend[j] += s * lv * 0.35f;
    }
}

static void render_mel(uint32_t n)
{
    int i;
    uint32_t j;
    float lv = 0.20f, a = onepole(mel_type ? 4200.0f : 1900.0f);
    for (i = 0; i < NMV; i++) {
        if (!mv[i].on)
            continue;
        for (j = 0; j < n; j++) {
            float s, ph = mv[i].ph, vib = mel_type ? 0.0f : sn(mv[i].vph) * fm_minf(mv[i].vib, 1.0f) * 0.004f;
            mv[i].vph = wrap1(mv[i].vph + 5.2f / EN_SR);
            if (mel_type == 0)
                s = sn(ph) + 0.16f * sn(ph * 3.0f) + 0.06f * sn(ph * 2.0f);
            else
                s = sn(ph) + mv[i].bell * 2.0f * sn(ph * 4.0f);
            mv[i].ph = wrap1(ph + mv[i].inc * (1.0f + vib));
            if (mv[i].att < 1.0f)
                mv[i].att = fm_minf(mv[i].att + mv[i].att_inc, 1.0f);
            s *= mv[i].amp * mv[i].att;
            if (mv[i].gate > 0) {
                mv[i].gate--;
                mv[i].amp *= mv[i].dec;
            } else {
                mv[i].amp *= mv[i].rel;
            }
            mv[i].bell *= mv[i].bell_dec;
            mv[i].vib += 1.0f / (0.3f * EN_SR);
            esend[j] += s;
        }
        if (mv[i].amp < 0.001f)
            mv[i].on = 0;
    }
    for (j = 0; j < n; j++) {                    /* the melody bus: darkened, left of centre, to the echo and the room */
        mel_lp += (esend[j] - mel_lp) * a;
        esend[j] = mel_lp * lv;
        bufl[j] += esend[j] * 1.1f;
        bufr[j] += esend[j] * 0.75f;
        rsend[j] += esend[j] * 0.6f;
    }
}

static void render_space(uint32_t n)
{
    uint32_t j;
    float vs = (float)STYLES[style_i].verb * 0.01f * 0.55f, damp = 0.42f, g = 0.77f;
    float ea = onepole(2600.0f);
    for (j = 0; j < n; j++) {
        float in = rsend[j] * vs, x[4], sum, yl, yr, e, rd;
        int i, ri;
        for (i = 0; i < 2; i++) {                /* the diffusers */
            float *d = ap_d[i], v = d[ap_p[i]], w = in + 0.6f * v;
            d[ap_p[i]] = w;
            in = v - 0.6f * w;
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
        /* the echo: a dotted eighth, each repeat darker */
        rd = (float)ec_w - ec_len;
        if (rd < 0.0f)
            rd += (float)EC_N;
        ri = (int)rd;
        e = (float)ec_buf[ri & (EC_N - 1)] * (1.0f / 16384.0f);
        ec_lp += (e - ec_lp) * ea;
        {
            float w = esend[j] + ec_lp * 0.38f;
            w = w > 1.9f ? 1.9f : w < -1.9f ? -1.9f : w;
            ec_buf[ec_w] = (int16_t)(w * 16384.0f);
            ec_w = (ec_w + 1) & (EC_N - 1);
        }
        bufl[j] += yl * 0.9f + ec_lp * 0.45f;
        bufr[j] += yr * 0.9f + ec_lp * 0.7f;
    }
}

/* the bus: low-passed (Tone, and the transitions' sweep), through the tape's wobble, warmed; then the record's
 * dust and the rain on top, the leveller, MASTER */
static void render_bus(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    float dust = (float)par[P_DUST] * 0.01f, rain = (float)par[P_RAIN] * 0.01f;
    float fc, ta, wow_d, flut_d, base = 260.0f, crk_p, pop_p, hiss = dust * 0.0045f + 0.0006f;
    float mood_dark = 1.0f - 0.25f * (float)par[P_MOOD] * 0.01f;
    float rain_a = onepole(5200.0f), rain_ha = onepole(700.0f), crk_a = onepole(5000.0f);
    float rl = rain * 0.035f, lv_rel = tau(0.4f), lv_att = 1.0f - tau(0.002f);
    float hp_a = onepole(28.0f), vol;
    int32_t needle_on = en_playing;
    sweep += (sweep_t - sweep) * fm_minf((float)n / (EN_SR * 1.2f), 1.0f);
    fc = 2200.0f * fm_exp2f((float)par[P_TONE] * 0.024f) * mood_dark;   /* 2.2 .. ~11 kHz */
    fc = fc * sweep * sweep;
    fc = fm_clampf(fc, 250.0f, 15000.0f);
    ta = onepole(fc);
    tone_a += (ta - tone_a) * 0.2f;
    wow_d = (6.0f + 70.0f * dust) * (1.0f + 0.25f * wow_rnd);   /* samples: 0.15 .. 1 ms of wow */
    flut_d = 0.6f + 1.4f * dust;
    crk_p = needle_on ? 0.00002f + dust * dust * 0.0016f : 0.0f;
    pop_p = needle_on ? dust * 0.000025f : 0.0f;
    if (needle_on && en_phase == PH_RUNOUT)
        crk_p *= 1.6f;
    wow_rnd_t -= (float)n;
    if (wow_rnd_t <= 0.0f) {                     /* the wow wanders */
        wow_rnd = frand() * 2.0f - 1.0f;
        wow_rnd_t = EN_SR * (1.0f + 2.0f * frand());
    }
    for (j = 0; j < n; j++) {
        float l = bufl[j], r = bufr[j], d, rd, fr, wl, wr, cl, cr, x;
        int ri;
        /* tone */
        t1l += (l - t1l) * tone_a;
        t2l += (t1l - t2l) * tone_a;
        t1r += (r - t1r) * tone_a;
        t2r += (t1r - t2r) * tone_a;
        /* wobble */
        wb_l[wb_w] = t2l * fade;
        wb_r[wb_w] = t2r * fade;
        d = base + wow_d * (sn(wow_ph) + 0.4f * sn(wow2_ph)) + flut_d * sn(flut_ph);
        wow_ph = wrap1(wow_ph + 0.55f / EN_SR);
        wow2_ph = wrap1(wow2_ph + 0.21f / EN_SR);
        flut_ph = wrap1(flut_ph + 6.3f / EN_SR);
        rd = (float)wb_w - d;
        if (rd < 0.0f)
            rd += (float)WB_N;
        ri = (int)rd;
        fr = rd - (float)ri;
        wl = wb_l[ri & (WB_N - 1)] + (wb_l[(ri + 1) & (WB_N - 1)] - wb_l[ri & (WB_N - 1)]) * fr;
        wr = wb_r[ri & (WB_N - 1)] + (wb_r[(ri + 1) & (WB_N - 1)] - wb_r[ri & (WB_N - 1)]) * fr;
        wb_w = (wb_w + 1) & (WB_N - 1);
        /* tape warmth: a soft knee */
        wl = wl * (27.0f + wl * wl) / (27.0f + 9.0f * wl * wl);
        wr = wr * (27.0f + wr * wr) / (27.0f + 9.0f * wr * wr);
        fade = fade < fade_t ? fm_minf(fade + fade_rate, fade_t) : fm_maxf(fade - fade_rate, fade_t);
        /* the record: hiss, crackle, a pop now and then, the scratch's tick, the needle landing */
        cl = cr = 0.0f;
        if (needle_t > 0.001f) {
            x = noise();
            hiss_lp += (x - hiss_lp) * 0.35f;
            hiss_hp += (hiss_lp - hiss_hp) * 0.03f;
            cl = cr = (hiss_lp - hiss_hp) * hiss;
            if (--crk_next <= 0) {             /* a tick: the next after a random wait (Poisson) */
                crk_next = gap(crk_p);
                float a = frand();
                a = a * a * a * (0.05f + 0.25f * dust);
                if (frand() < 0.5f)
                    crk_l += a;
                else
                    crk_r -= a;
                if (frand() < 0.3f)
                    crk_l -= a * 0.6f, crk_r += a * 0.6f;
            }
            if (--pop_next <= 0 && (pop_next = gap(pop_p)) > 0)
                thump += 0.06f + 0.08f * frand();
            crk_lp_l += (crk_l - crk_lp_l) * crk_a;   /* each tick a short impulse, softened */
            crk_lp_r += (crk_r - crk_lp_r) * crk_a;
            cl += crk_lp_l + 0.25f * crk_l;
            cr += crk_lp_r + 0.25f * crk_r;
            crk_l *= 0.6f;
            crk_r *= 0.6f;
            if (thump > 1e-3f) {
                float t = sn(thump_ph) * thump;
                thump_ph = wrap1(thump_ph + 55.0f / EN_SR);
                cl += t;
                cr += t;
                thump *= 0.9985f;
            }
            cl *= needle_t;
            cr *= needle_t;
        }
        /* rain: filtered noise that sways, and drops */
        if (rl > 0.0f) {
            x = noise();
            rain_lp += (x - rain_lp) * rain_a;
            rain_bp += (rain_lp - rain_bp) * rain_ha;
            x = (rain_lp - rain_bp) * rl * (0.8f + 0.2f * sn(rain_ph));
            if (--drop_next <= 0) {              /* a drop on the glass */
                drop_next = gap(rain * 0.0009f);
                rain_drop += (frand() * 2.0f - 1.0f) * rl * 1.6f;
            }
            rain_mod += (rain_drop - rain_mod) * 0.3f;
            rain_drop *= 0.8f;
            cl += x + rain_mod;
            cr += x * 0.9f - rain_mod * 0.7f;
        }
        l = wl + cl;
        r = wr + cr;
        /* a DC and rumble cut */
        hp_l += (l - hp_l) * hp_a;
        hp_r += (r - hp_r) * hp_a;
        l -= hp_l;
        r -= hp_r;
        /* the leveller: a quick attack, a slow release, above -6 dB */
        vu_acc_l += l * l;                       /* the VU reads the music, not MASTER */
        vu_acc_r += r * r;
        x = fm_maxf(fm_fabsf(l), fm_fabsf(r)) * gain;
        lev = x > lev ? lev + (x - lev) * lv_att : lev * lv_rel;
        vol = lev > 0.5f ? 0.5f / lev : 1.0f;
        l *= gain * vol;
        r *= gain * vol;
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
    }
    rain_ph = wrap1(rain_ph + 0.07f * (float)n / EN_SR);
}

static void control(uint32_t n)
{
    float k = (float)n / EN_SR;
    en_kick_glow *= fm_expf(-k * 9.0f);
    duck *= fm_expf(-k * 8.0f);
    needle_t += ((en_playing ? 1.0f : 0.0f) - needle_t) * fm_minf(k * 6.0f, 1.0f);
    en_needle = needle_t;
    if (drop_thump > 0.0f) {
        thump += 0.05f;
        drop_thump = 0.0f;
    }
    if (++vu_n >= 8u) {                          /* the VU: about 46 ms of RMS, then ballistics */
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
    case E_KEY: key_on(e->note, e->vel, e->gate, 0); break;
    case E_BASS: bass_on(e->note, e->vel, e->gate); break;
    case E_KICK: kick_hit(e->vel); break;
    case E_SNARE: snare_hit(e->vel); break;
    case E_RIM: rim_hit(e->vel); break;
    case E_HAT: hat_hit(e->vel, 0); break;
    case E_OHAT: hat_hit(e->vel, 1); break;
    case E_MEL: mel_on(e->note, e->vel, e->gate); break;
    default: break;
    }
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 1.6f;
    drain();
    while (n) {
        uint32_t k = n > BLK ? BLK : n, j;
        int i, seq = 0;
        if (en_playing) {
            if (en_phase == PH_RUNOUT) {
                if (runout_left <= 0) {
                    new_track();
                } else {
                    if (runout_tick <= 0.0f) {   /* the run-out groove: a soft thump once a turn */
                        thump += 0.02f + 0.04f * (float)par[P_DUST] * 0.01f;
                        runout_tick += EN_SR * 1.8f;
                    }
                    if ((int)k > runout_left)
                        k = (uint32_t)runout_left;
                }
            } else if (skipping && fade < 0.002f) {   /* skipped: faded out, a short run-out */
                en_phase = PH_RUNOUT;
                en_section = 5;
                runout_left = (int)(EN_SR * 1.8f);
                runout_tick = EN_SR * 0.4f;
                skipping = 0;
                for (i = 0; i < NEV; i++)
                    evq[i].used = 0;
            } else {
                if (tk.step_left <= 0.0f) {
                    seq_step();
                    tk.step_left += step_len;
                }
                if (en_phase != PH_RUNOUT) {
                    seq = 1;
                    if ((float)k > tk.step_left)
                        k = (uint32_t)tk.step_left + 1u;
                }
            }
        }
        /* events due now fire; the chunk ends at the next one */
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
            if (tk.scratch && par[P_DUST] > 25) {   /* a scratch on this record: a tick once a turn */
                uint32_t turn = (uint32_t)(EN_SR * 1.8f), at = trk_samples % turn;
                if (at < k)
                    crk_l += 0.03f + 0.05f * (float)par[P_DUST] * 0.01f, crk_r += 0.02f;
            }
            trk_samples += k;
            en_track_ms = (uint32_t)((float)trk_samples * (1000.0f / EN_SR));
        } else if (en_playing && en_phase == PH_RUNOUT) {
            runout_left -= (int)k;
            runout_tick -= (float)k;
        }
        control(k);
        for (j = 0; j < k; j++)
            bufl[j] = bufr[j] = rsend[j] = esend[j] = 0.0f;
        render_keys(k);
        render_bass(k);
        render_drums(k);
        render_mel(k);
        render_space(k);
        render_bus(out, k, gain);
        out += 2 * k;
        n -= k;
    }
}
