/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen
 * (the command queue and the render's shape after AMB-1's engine, Copyright (C) 2026 Charles Vestal) */
/* MINIMAL MACHINE's engine (mm.h). One compilation unit, -O2. */
#include "mm.h"
#include "fastmath.h"

#if defined MM_HOST || defined PLAT_HOST
#define MM_POOL
#else
#define MM_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const MM_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const char *const MM_PART_NAME[MM_NT] = {"Kick", "Hats", "Perc", "Stab", "Bass", "Bleep"};
const char *const MM_MUT_NAME[MU_N] = {"moves", "new note", "home", "leans", "rests", "returns", "longer",
                                       "shorter", "new chord"};

static const mm_param_t PARAMS[P_NPARAMS] = {
    {"Density", 0, 100, 50}, {"Drift", 0, 100, 50}, {"Filter", 0, 100, 50}, {"Echo", 0, 100, 50},
    {"Drums", 0, 100, 70},   {"Stab", 0, 100, 70},  {"Bass", 0, 100, 70},   {"Bleep", 0, 100, 70},
    {"Swing", 0, 60, 0},     {"Echo time", 0, 3, 1}, {"Reverb", 0, 100, 35}, {"Duck", 0, 100, 40},
    {"Tempo", 90, 140, 120}, {"Key", 0, 11, 5},
};
const mm_param_t *mm_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

static const uint8_t ETIME_SIX[4] = {2, 3, 4, 6};          /* the echo's time in sixteenths */
static const char *const ETIME_NAME[4] = {"1/8", "3/16", "1/4", "3/8"};

static int mut_every(int v)                                /* Drift: bars between mutations (0: never) */
{
    float b;
    if (v <= 0)
        return 0;
    b = fm_exp2f(5.0f - (float)v * 0.05f);                 /* 1 .. 32 bars */
    return b < 1.0f ? 1 : (int)(b + 0.5f);
}

static char *itoa_s(int v, char *b)
{
    char t[12];
    int n = 0;
    if (v < 0) {
        *b++ = '-';
        v = -v;
    }
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static char *cat(char *b, const char *s)
{
    while ((*b = *s++))
        b++;
    return b;
}

void mm_param_text(int i, int v, char *b)
{
    b[0] = 0;
    switch (i) {
    case P_DRIFT: {
        int e = mut_every(v);
        if (!e) {
            cat(b, "Held");
        } else {
            b = itoa_s(e, b);
            cat(b, e == 1 ? " bar" : " bars");
        }
        break;
    }
    case P_SWING: cat(itoa_s(v, b), "%"); break;
    case P_ETIME: cat(b, ETIME_NAME[v & 3]); break;
    case P_KEY: cat(b, MM_NOTE_NAME[(v % 12 + 12) % 12]); break;
    default: itoa_s(v, b); break;
    }
}

/* ------------------------------------------------------------- presets --- */
/* The loops: one string a part, its length the loop's. '.' a rest, 'x' a hit, 'X' an accented one, 'O' an open
 * hat, 'o' a hit that sounds once Density is up; '1'..'8' a hit on the part's note 1..8 (bass, bleep), 'a'..'h' the
 * same, sounding with Density up. Rests get notes too: Density and the mutations bring them in. */
typedef struct {
    const char *name;
    int16_t par[P_NPARAMS];       /* density drift filter echo | drums stab bass bleep | swing etime reverb duck |
                                   * tempo key */
    const char *pat[MM_NT];
    int8_t chord[4];              /* the stab: semitones over its root (127: none) */
    int8_t chord_b;               /* its second chord: the root moved by this (0: none) */
    uint8_t stab_oct;             /* MIDI of the stab's root in C */
    uint8_t stab_dec;             /* its decay, 10 ms */
    uint8_t stab_res;             /* the filter's resonance 0..100 */
    uint8_t stab_env;             /* the filter's sweep, tenths of an octave */
    int8_t bass_n[4];             /* the bass's notes, semitones over the key */
    uint8_t bass_oct, bass_dec, bass_warm, bass_bend;
    int8_t bleep_n[6];
    uint8_t bleep_oct, bleep_type;   /* 0 an FM ping, 1 a wooden knock, 2 glass */
    uint8_t kick_hz, kick_dec, kick_punch;
    uint8_t perc_type;            /* 0 rim, 1 clap, 2 shaker */
    uint8_t hat_tone;             /* 0 dark .. 100 bright */
    uint8_t dust;                 /* vinyl and hiss, 0..100 */
} preset_t;

#define N 127
static const preset_t PRESETS[MM_NPRESETS] = {
    {"Dub Chamber", {50, 50, 46, 60, 70, 70, 70, 60, 8, 1, 40, 45, 118, 5},
     {"x...x...x...x...", "..x...x...xO", "...x..o", "..X....o..", "..1...2...1..4..", "o.3.."},
     {0, 3, 7, 10}, 5, 48, 30, 55, 22, {0, 12, 7, 10}, 36, 22, 25, 0, {0, 3, 5, 7, 10, 12}, 72, 0,
     48, 45, 60, 0, 45, 30},
    {"Warehouse", {55, 40, 55, 35, 80, 60, 70, 55, 0, 0, 25, 35, 126, 9},
     {"x...x...x...x..o", "..X..o", "....x.......x...", "..x.....X..o", "...1..1....1..2.", "1..o.2...o."},
     {0, 7, 12, 15}, -2, 48, 14, 40, 18, {0, 0, 12, 7}, 36, 16, 40, 0, {0, 3, 7, 10, 12, 15}, 60, 1,
     52, 38, 80, 1, 55, 10},
    {"Glass", {50, 55, 62, 55, 65, 55, 60, 75, 5, 2, 45, 35, 120, 4},
     {"x...x...x...x...", "..x.o.x.", "o..x.o..x", "..X.......o.", "..1.....1..3", "1...4..2.o..6"},
     {0, 3, 10, 14}, 2, 48, 24, 45, 20, {0, 12, 10, 7}, 36, 20, 15, 0, {0, 3, 7, 10, 14, 15}, 79, 2,
     46, 40, 55, 2, 70, 10},
    {"Rubber", {50, 45, 50, 45, 70, 55, 85, 55, 12, 1, 30, 50, 122, 7},
     {"x...x...x...x...", "..x...xo..x.", ".x....x", "....X...o.", "..1.2.1", "o..2.o.1..."},
     {0, 3, 7, 10}, -5, 48, 18, 50, 18, {0, 12, 3, 10}, 36, 24, 70, 70, {0, 3, 5, 7, 10, 12}, 60, 1,
     50, 42, 70, 0, 50, 15},
    {"Fog Lights", {45, 50, 30, 75, 55, 75, 70, 50, 6, 3, 65, 45, 112, 2},
     {"x...x...x...x...", "..x.....x...o.", "........x..o.", "..X.........o.", "..1....1..2.....", "3...o..1."},
     {0, 5, 10, 15}, -2, 48, 42, 60, 16, {0, 12, 7, 5}, 36, 30, 20, 0, {0, 3, 5, 7, 10, 12}, 72, 2,
     46, 50, 50, 2, 30, 45},
    {"Night Bus", {50, 50, 50, 55, 70, 65, 70, 65, 15, 1, 40, 40, 116, 0},
     {"x...x...x...x...", "..x.x..O..x.", ".....x.o", "..X...o...", "..1..1.2.", "1.o.3...5"},
     {0, 3, 7, 10}, 3, 48, 26, 50, 20, {0, 12, 7, 3}, 36, 22, 30, 10, {0, 3, 5, 7, 10, 12}, 72, 0,
     48, 45, 60, 1, 45, 25},
    {"Tape Delay", {45, 50, 40, 82, 65, 80, 65, 50, 8, 2, 35, 50, 120, 10},
     {"x...x...x...x...", "..x...x...x...x...", "...x...", "..X.......", "..1.....1..2", "2..o.1."},
     {0, 3, 7, 10}, 5, 48, 45, 60, 24, {0, 12, 7, 10}, 36, 24, 25, 0, {0, 3, 5, 7, 10, 12}, 72, 0,
     47, 48, 60, 0, 40, 55},
    {"Concrete", {60, 70, 60, 25, 85, 50, 70, 60, 0, 0, 20, 30, 128, 1},
     {"x...x...x...x.o.", "X.x.X.x.X.xO", ".......x..o.x...", "...x.....o", ".1..1..2.1..", "1.o2.1o"},
     {0, 7, 10, 15}, 0, 48, 10, 35, 14, {0, 0, 12, 10}, 36, 14, 50, 20, {0, 3, 7, 10, 12, 15}, 60, 1,
     54, 34, 90, 1, 70, 5},
    {"Shallow Water", {45, 45, 56, 60, 60, 70, 65, 75, 10, 1, 50, 35, 114, 3},
     {"x...x...x...x...", "..x...x.o.x.", "...x..o..", "..X.....o..", "..1.....2...1...", "1..3..5.o.2..6"},
     {0, 4, 11, 14}, 5, 48, 30, 45, 20, {0, 12, 7, 11}, 36, 22, 20, 0, {0, 2, 4, 7, 9, 12}, 79, 2,
     46, 44, 50, 2, 60, 20},
    {"Signal Box", {55, 60, 55, 50, 70, 55, 65, 80, 4, 0, 35, 40, 124, 8},
     {"x...x...x...x...", "..x...O.", "...x..o..x.", "....X.....", "..1...1..2...", "1.32.o4"},
     {0, 3, 7, 14}, -2, 48, 16, 45, 18, {0, 12, 7, 10}, 36, 18, 35, 0, {0, 3, 5, 7, 10, 12}, 84, 0,
     50, 40, 70, 2, 60, 10},
    {"Low Orbit", {40, 40, 35, 65, 65, 70, 80, 45, 5, 3, 55, 55, 110, 11},
     {"x...x...x...x...", "..x...o...x...o...", ".......x", "..X..........", "..1.....1.....2.", "1....o..3"},
     {0, 3, 7, 10}, -5, 48, 60, 70, 12, {0, 12, 7, 10}, 36, 40, 20, 0, {0, 3, 7, 10, 12, 15}, 67, 2,
     44, 55, 50, 0, 35, 35},
    {"Afterhours", {55, 50, 50, 50, 75, 65, 70, 60, 20, 1, 35, 45, 121, 6},
     {"x...x...x...x...", "..x...xo..x.O.", "....x..o..", "..X......o", "..1...2.1..", "o.2..4.1"},
     {0, 3, 7, 10}, -5, 48, 22, 50, 20, {0, 12, 7, 3}, 36, 20, 35, 10, {0, 3, 5, 7, 10, 12}, 72, 0,
     49, 45, 65, 1, 50, 20},
};

const char *mm_preset_name(int i) { return PRESETS[(unsigned)i < MM_NPRESETS ? i : 0].name; }
void mm_preset_pars(int i, int16_t *par)
{
    int p;
    const preset_t *r = &PRESETS[(unsigned)i < MM_NPRESETS ? i : 0];
    for (p = 0; p < P_NPARAMS; p++)
        par[p] = r->par[p];
}

static const uint8_t NOTE_N[MM_NT] = {2, 3, 2, 2, 4, 6};   /* each part's notes (colours) */

/* a small deterministic generator for the presets' loops (each preset always starts the same) */
static uint32_t hrnd(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

void mm_preset_home(int i, mm_gen_t *g)
{
    const preset_t *r = &PRESETS[(unsigned)i < MM_NPRESETS ? i : 0];
    uint32_t s = 0x2545F491u ^ (uint32_t)(i + 1) * 0x9E3779B9u;
    int t, k;
    for (t = 0; t < MM_NT; t++) {
        const char *p = r->pat[t];
        int len = 0;
        while (p[len] && len < MM_MAXLEN)
            len++;
        g->len[t] = (uint8_t)(len < 3 ? 3 : len);
        g->tone[t] = 0;
        for (k = 0; k < MM_MAXLEN; k++) {
            char c = k < len ? p[k] : '.';
            uint32_t h = hrnd(&s);
            uint8_t note = (uint8_t)((h >> 8) % NOTE_N[t]), prio;
            if (t == T_HATS && note == 2 && (h >> 20) % 4u)
                note = 0;                                  /* (open hats mostly where they are written) */
            if (c == 'x' || c == 'X' || c == 'O' || (c >= '1' && c <= '8')) {
                prio = (uint8_t)(t == T_KICK ? 0 : 5 + h % 40u);
                note = c == 'x' ? 0 : c == 'X' ? 1 : c == 'O' ? 2 : (uint8_t)(c - '1');
            } else if (c == 'o' || (c >= 'a' && c <= 'h')) {
                prio = (uint8_t)(80 + h % 60u);
                note = c == 'o' ? (t == T_KICK ? 1 : 0) : (uint8_t)(c - 'a');
            } else {
                prio = (uint8_t)(150 + h % 100u);
                if (t == T_KICK)
                    note = 1;
            }
            if (note >= NOTE_N[t])
                note = (uint8_t)(NOTE_N[t] - 1);
            g->prio[t][k] = prio;
            g->note[t][k] = note;
        }
    }
    g->rest = 0;
    g->chord = 0;
}

int mm_gen_ok(const mm_gen_t *g)
{
    int t, k;
    if (g->rest & ~0x3Eu || g->chord > 1 || (g->len[T_KICK] != 16))
        return 0;
    for (t = 0; t < MM_NT; t++) {
        if (g->len[t] < 3 || g->len[t] > MM_MAXLEN || g->tone[t] < -100 || g->tone[t] > 100)
            return 0;
        for (k = 0; k < MM_MAXLEN; k++)
            if (g->note[t][k] >= NOTE_N[t])
                return 0;
    }
    return 1;
}

static const char *const QUAL[][2] = {           /* chord names, by their intervals */
    {"\x00\x03\x07\x0A", "m7"}, {"\x00\x07\x0C\x0F", "m"}, {"\x00\x03\x0A\x0E", "m9"}, {"\x00\x05\x0A\x0F", "m11"},
    {"\x00\x07\x0A\x0F", "m7"}, {"\x00\x04\x0B\x0E", "maj9"}, {"\x00\x03\x07\x0E", "madd9"},
};
const char *mm_chord_name(int preset, int which, int key, char *buf)
{
    const preset_t *r = &PRESETS[(unsigned)preset < MM_NPRESETS ? preset : 0];
    int root = (key + (which ? r->chord_b : 0) + 24) % 12;
    uint32_t i;
    char *b = cat(buf, MM_NOTE_NAME[root]);
    for (i = 0; i < sizeof QUAL / sizeof QUAL[0]; i++)
        if (QUAL[i][0][1] == r->chord[1] && QUAL[i][0][2] == r->chord[2] && QUAL[i][0][3] == r->chord[3]) {
            cat(b, QUAL[i][1]);
            break;
        }
    return buf;
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_PRESET, C_PLAY, C_HOLD, C_MUTE, C_MUTATE, C_SEED, C_PANIC, C_HUSH, C_GEN };
#define QN 64u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static const mm_gen_t *volatile gen_in;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                                   /* full: dropped (never, at the UI's rate) */
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void mm_set(int p, int v) { post(C_SET, p, v); }
void mm_preset(int i) { post(C_PRESET, i, 0); }
void mm_play(int on) { post(C_PLAY, on, 0); }
void mm_hold(int on) { post(C_HOLD, on, 0); }
void mm_mute(uint32_t m) { post(C_MUTE, (int)m, 0); }
void mm_mutate(void) { post(C_MUTATE, 0, 0); }
void mm_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
void mm_panic(void) { post(C_PANIC, 0, 0); }
void mm_hush(void) { post(C_HUSH, 0, 0); }
void mm_gen_put(const mm_gen_t *g)
{
    gen_in = g;
    BARRIER();
    post(C_GEN, 0, 0);
}

/* -------------------------------------------------------------- state --- */
volatile uint8_t mm_playing, mm_held, mm_hushed;
volatile uint32_t mm_six;
volatile float mm_phase;
volatile uint8_t mm_pos[MM_NT], mm_len[MM_NT];
volatile uint32_t mm_pat[MM_NT];
volatile float mm_glow[MM_NT], mm_level[MM_NT];
volatile int8_t mm_mut_part = -1, mm_mut_step = -1, mm_mut_kind = -1;
volatile float mm_mut_glow;
volatile uint32_t mm_muts;
volatile uint8_t mm_chord;
volatile float mm_kick_env;

static int16_t par[P_NPARAMS];
static const preset_t *pr = &PRESETS[0];
static mm_gen_t gen, home;                       /* the loops now, and as the preset starts them */
static uint8_t pos[MM_NT];
static uint32_t mute, hand;                      /* muted by hand; parts fading for a hand (quickly) */
static float tone_now[MM_NT];                    /* the leans, sliding to gen.tone */
static float lvl[MM_NT];                         /* the parts' levels, sliding to their targets */
static uint32_t rng = 0x9E3779B9u, nz = 22222u;
static int bar_six = 15, bars_left = 4, mut_now;
static float step_len = 5512.5f, tick_left;
static uint8_t hush_req, hush_on;
static float hush_g = 1.0f;
static int32_t hush_wait;                        /* stopped: samples the quiet lasts */

static uint32_t rnd(void)                       /* Marsaglia's xorshift */
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    rng = x ^ (x << 5);
    return rng;
}
static int irand(int n) { return n > 0 ? (int)((rnd() >> 8) % (uint32_t)n) : 0; }
static inline float noise(void)                 /* white, -1..1 (an LCG: the cheap one) */
{
    nz = nz * 1664525u + 1013904223u;
    return (float)(int32_t)nz * (1.0f / 2147483648.0f);
}
static float mtof(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }
static int keyed(int oct)                       /* the key's root near oct (oct - 5 .. oct + 6) */
{
    return oct + (par[P_KEY] + 5) % 12 - 5;
}
static float tau(float secs) { return fm_expf(-1.0f / (secs * MM_SR)); }   /* a decay's factor a sample */

/* ------------------------------------------------------------ the loops --- */
static int thr(int t)                           /* steps with a priority below this sound */
{
    int v = 75 + (int)((float)(par[P_DENSITY] - 50) * 2.2f);
    return t == T_KICK && v < 1 ? 1 : v;
}
static void publish(int t)
{
    uint32_t m = 0;
    int k, th = thr(t);
    for (k = 0; k < gen.len[t]; k++)
        if (gen.prio[t][k] < th)
            m |= 1u << k;
    mm_pat[t] = m;
    mm_len[t] = gen.len[t];
}
static void publish_all(void)
{
    int t;
    for (t = 0; t < MM_NT; t++)
        publish(t);
    mm_chord = gen.chord;
}

static void flash(int t, int step, int kind)
{
    mm_mut_part = (int8_t)t;
    mm_mut_step = (int8_t)step;
    mm_mut_kind = (int8_t)kind;
    mm_mut_glow = 1.0f;
    mm_muts++;
}

static int resting(void)                        /* how many parts rest */
{
    int t, n = 0;
    for (t = 0; t < MM_NT; t++)
        n += (int)(gen.rest >> t & 1u);
    return n;
}

static int pick_part(void)                      /* any but the kick, mostly */
{
    return irand(10) == 0 ? T_KICK : 1 + irand(MM_NT - 1);
}

static int mu_move(void)                         /* a hit slides a step or two */
{
    int t = 1 + irand(MM_NT - 1), len = gen.len[t], th = thr(t), a, b, k, tries;
    for (tries = 0; tries < 8; tries++) {
        a = irand(len);
        if (gen.prio[t][a] >= th)
            continue;
        for (k = 0; k < 4; k++) {
            int d = (k & 1 ? -1 : 1) * (1 + (k >> 1));
            b = ((a + d) % len + len) % len;
            if (irand(2) && k < 2)
                continue;
            if (gen.prio[t][b] >= th) {
                uint8_t p = gen.prio[t][a];
                gen.prio[t][a] = gen.prio[t][b];
                gen.prio[t][b] = p;
                publish(t);
                flash(t, b, MU_MOVE);
                return 1;
            }
        }
    }
    return 0;
}

static int mu_note(void)                         /* a step's note or colour changes */
{
    int t = pick_part(), len = gen.len[t], th = thr(t), tries, s;
    if (t == T_KICK)
        return 0;
    for (tries = 0; tries < 8; tries++) {
        s = irand(len);
        if (gen.prio[t][s] < th) {
            int n = NOTE_N[t], v = gen.note[t][s];
            if (t == T_BASS || t == T_BLEEP)    /* a neighbour, not a leap */
                v = v + (irand(2) ? 1 : -1);
            else
                v = v + 1 + irand(n - 1);
            v = (v % n + n) % n;
            if (t == T_HATS && v == 2 && irand(2))
                v = 0;
            gen.note[t][s] = (uint8_t)v;
            flash(t, s, MU_NOTE);
            return 1;
        }
    }
    return 0;
}

static int mu_home(void)                         /* something goes back as the preset had it */
{
    int t = pick_part(), k, n = 0, s = -1;
    if (gen.len[t] != home.len[t]) {
        gen.len[t] = home.len[t];
        pos[t] = (uint8_t)(pos[t] % gen.len[t]);
        publish(t);
        flash(t, -1, MU_HOME);
        return 1;
    }
    for (k = 0; k < gen.len[t]; k++)
        if (gen.prio[t][k] != home.prio[t][k] || gen.note[t][k] != home.note[t][k])
            if (irand(++n) == 0)
                s = k;
    if (s < 0)
        return 0;
    gen.prio[t][s] = home.prio[t][s];
    gen.note[t][s] = home.note[t][s];
    publish(t);
    flash(t, s, MU_HOME);
    return 1;
}

static int mu_tone(void)                         /* a part leans darker or brighter, longer or shorter */
{
    int t = pick_part(), v;
    do
        v = irand(13) * 10 - 60;
    while (v == gen.tone[t]);
    gen.tone[t] = (int8_t)v;
    flash(t, -1, MU_TONE);
    return 1;
}

static int mu_rest(void)                         /* a part sits out a while (never the kick, one at a time) */
{
    int t = 1 + irand(MM_NT - 1);
    if (resting() || (mute >> t & 1u))
        return 0;
    gen.rest |= (uint8_t)(1u << t);
    flash(t, -1, MU_REST);
    return 1;
}

static int mu_back(void)
{
    int t;
    for (t = 0; t < MM_NT; t++)
        if (gen.rest >> t & 1u) {
            gen.rest &= (uint8_t)~(1u << t);
            flash(t, -1, MU_BACK);
            return 1;
        }
    return 0;
}

static int mu_len(int d)                         /* a loop a step longer or shorter: the phasing turns */
{
    int t = 1 + irand(MM_NT - 1), l = gen.len[t] + d, h = home.len[t];
    if (l < 3 || l > MM_MAXLEN || l < h - 2 || l > h + 2)
        return 0;
    gen.len[t] = (uint8_t)l;
    pos[t] = (uint8_t)(pos[t] % l);
    publish(t);
    flash(t, d > 0 ? l - 1 : -1, d > 0 ? MU_LONGER : MU_SHORTER);
    return 1;
}

static int mu_chord(void)
{
    if (!pr->chord_b)
        return 0;
    gen.chord ^= 1u;
    mm_chord = gen.chord;
    flash(T_STAB, -1, MU_CHORD);
    return 1;
}

static void mutate(void)                         /* one thing changes */
{
    int tries;
    for (tries = 0; tries < 12; tries++) {
        int r = irand(100), ok;
        if (resting() && r < 22)
            ok = mu_back();
        else if (r < 34)
            ok = mu_move();
        else if (r < 56)
            ok = mu_note();
        else if (r < 70)
            ok = mu_home();
        else if (r < 82)
            ok = mu_tone();
        else if (r < 87)
            ok = mu_rest();
        else if (r < 90)
            ok = mu_len(1);
        else if (r < 94)
            ok = mu_len(-1);
        else
            ok = mu_chord();
        if (ok)
            return;
    }
}

/* ------------------------------------------------------------- voices --- */
/* kick */
static float k_ph, k_amp, k_pe, k_dec, k_pdec, k_click, k_f, k_punch;
/* hats */
static float h_amp, h_dec, h_ic1, h_ic2, h_lp, h_a1, h_a2, h_a3, h_lpk;
/* perc */
static float p_amp, p_vel, p_dec, p_ph1, p_ph2, p_ic1, p_ic2, p_lp, p_lp2;
static int32_t p_t;
/* stab */
#define NSTAB 4
static float s_ph[NSTAB][2], s_dt[NSTAB][2], s_idt[NSTAB][2];
static int s_n;
static float s_amp, s_att, s_dec, s_fe, s_fdec, s_ic1, s_ic2, s_vel, s_k;
/* bass */
static float b_ph, b_f, b_amp, b_att, b_dec, b_pe, b_vel;
/* bleep */
static float l_ph, l_mph, l_f, l_amp, l_dec, l_idx, l_idec, l_pe, l_ratio;
/* dust */
static float d_lp, d_hiss;
/* the parts' levels (knobs) and sends */
static float g_drums = 1.0f, g_stab = 1.0f, g_bass = 1.0f, g_bleep = 1.0f, fk;
static float duck_d = 0.4f, duck_e;

static float knob_lvl(int v) { return (float)v * (1.0f / 70.0f); }

static void kick_hit(float vel)
{
    float tn = tone_now[T_KICK];
    k_f = (float)pr->kick_hz * (1.0f + 0.04f * tn);
    k_punch = (float)pr->kick_punch * 2.6f;           /* Hz of the pitch's fall */
    k_amp = vel;
    k_pe = 1.0f;
    k_pdec = tau(0.028f);
    k_dec = tau((float)pr->kick_dec * 0.004f * fm_exp2f(0.5f * tn));
    k_click = 0.12f * vel;
    k_ph = 0.0f;
}

static void hats_hit(int note)
{
    float tn = tone_now[T_HATS], secs = note == 2 ? 0.12f : 0.022f;
    h_amp = note == 1 ? 1.0f : note == 2 ? 0.8f : 0.62f;
    h_dec = tau(secs * fm_exp2f(0.6f * tn));
}

static void perc_hit(int note)
{
    float tn = tone_now[T_PERC];
    p_amp = p_vel = note ? 1.0f : 0.75f;
    p_t = 0;
    switch (pr->perc_type) {
    case 0: p_dec = tau(0.009f * fm_exp2f(0.5f * tn)); break;           /* rim */
    case 1: p_dec = tau(0.003f); break;                                   /* clap: its bursts */
    default: p_dec = tau(0.028f * fm_exp2f(0.6f * tn)); break;           /* shaker */
    }
}

static void stab_hit(int note)
{
    int i, root = keyed(pr->stab_oct) + (gen.chord ? pr->chord_b : 0);
    float tn = tone_now[T_STAB];
    if (root > 57)                                   /* keep it in the dub register */
        root -= 12;
    if (root < 45)
        root += 12;
    s_n = 0;
    for (i = 0; i < 4; i++) {
        float f, det;
        if (pr->chord[i] == N)
            continue;
        f = mtof((float)(root + pr->chord[i]));
        det = 0.0035f + 0.001f * (float)i;
        s_dt[s_n][0] = f * (1.0f - det) / MM_SR;
        s_dt[s_n][1] = f * (1.0f + det) / MM_SR;
        s_idt[s_n][0] = 1.0f / s_dt[s_n][0];
        s_idt[s_n][1] = 1.0f / s_dt[s_n][1];
        s_n++;
    }
    s_vel = note ? 1.0f : 0.8f;
    s_amp = 0.0f;
    s_att = 1.0f;
    s_dec = tau((float)pr->stab_dec * 0.01f * fm_exp2f(0.7f * tn) * 0.45f);
    s_fe = note ? 1.15f : 1.0f;
    s_fdec = tau((float)pr->stab_dec * 0.01f * fm_exp2f(0.7f * tn) * 0.25f);
}

static void bass_hit(int note)
{
    float tn = tone_now[T_BASS];
    int n = keyed(pr->bass_oct) + pr->bass_n[note & 3];
    if (n > 45)
        n -= 12;
    b_f = mtof((float)n);
    b_vel = 1.0f;
    b_att = 1.0f;
    b_pe = (float)pr->bass_bend * 0.0006f;           /* a little fall into the note: 0 .. 6 % */
    b_dec = tau((float)pr->bass_dec * 0.01f * fm_exp2f(0.6f * tn) * 0.5f);
}

static void bleep_hit(int note)
{
    float tn = tone_now[T_BLEEP];
    int n = keyed(pr->bleep_oct) + pr->bleep_n[note % 6];
    l_f = mtof((float)n);
    l_amp = 1.0f;
    l_pe = 0.0f;
    switch (pr->bleep_type) {
    case 0:                                            /* a ping: FM at the octave */
        l_ratio = 2.0f;
        l_idx = 2.2f * fm_exp2f(0.5f * tn);
        l_idec = tau(0.03f);
        l_dec = tau(0.11f * fm_exp2f(0.5f * tn));
        break;
    case 1:                                            /* wood: a low knock, its pitch falling */
        l_f *= 0.5f;
        l_ratio = 1.41f;
        l_idx = 1.2f;
        l_idec = tau(0.012f);
        l_dec = tau(0.05f * fm_exp2f(0.5f * tn));
        l_pe = 0.6f;
        break;
    default:                                           /* glass: an inharmonic bell */
        l_ratio = 3.5f;
        l_idx = 0.9f * fm_exp2f(0.4f * tn);
        l_idec = tau(0.25f);
        l_dec = tau(0.42f * fm_exp2f(0.5f * tn));
        break;
    }
    l_ph = l_mph = 0.0f;
}

static void trigger(int t, int note)
{
    mm_glow[t] = 1.0f;
    switch (t) {
    case T_KICK: kick_hit(note ? 0.45f : 1.0f); break;
    case T_HATS: hats_hit(note); break;
    case T_PERC: perc_hit(note); break;
    case T_STAB: stab_hit(note); break;
    case T_BASS: bass_hit(note); break;
    default: bleep_hit(note); break;
    }
}

/* ---------------------------------------------------------- sequencer --- */
static void tempo_set(void) { step_len = MM_SR * 60.0f / ((float)par[P_TEMPO] * 4.0f); }

static void tick(void)                           /* a sixteenth */
{
    int t;
    bar_six = (bar_six + 1) & 15;
    if (bar_six == 0) {                          /* a bar: a mutation when it is due */
        if (!mm_held && mm_playing && par[P_DRIFT] > 0 && --bars_left <= 0) {
            mutate();
            bars_left = mut_every(par[P_DRIFT]);
        }
        if (hush_on && mm_playing) {             /* the quiet moment is over: the downbeat */
            hush_on = 0;
            hush_req = 0;
        }
    }
    if (mut_now) {
        mut_now = 0;
        mutate();
        bars_left = mut_every(par[P_DRIFT]);
    }
    if (hush_req && mm_playing && bar_six == 15)
        hush_on = 1;
    for (t = 0; t < MM_NT; t++) {
        pos[t] = (uint8_t)((pos[t] + 1) % gen.len[t]);
        mm_pos[t] = pos[t];
        if (mm_playing && lvl[t] > 0.02f && gen.prio[t][pos[t]] < thr(t))
            trigger(t, gen.note[t][pos[t]]);
    }
    mm_six++;
}

/* ------------------------------------------------------------- effects --- */
/* the echo: a ping-pong, synced (Echo time sixteenths), each repeat darker and a little driven, with a slow wow */
#define ECHO_N 44200                             /* a second: 3/8 at 90 bpm */
static int16_t e_l[ECHO_N] MM_POOL, e_r[ECHO_N] MM_POOL;
static uint32_t e_w;
static float e_d = 11025.0f, e_dt = 11025.0f, e_send, e_fb, e_lpl, e_lpr, e_hpl, e_hpr, e_lpk, e_wow, e_wph;

static inline float e_read(const int16_t *b, float d)
{
    float rp = (float)e_w - d, f;
    int32_t i;
    if (rp < 0.0f)
        rp += (float)ECHO_N;
    i = (int32_t)rp;
    f = rp - (float)i;
    {
        int32_t j = i + 1 >= ECHO_N ? 0 : i + 1;
        return ((float)b[i] + ((float)b[j] - (float)b[i]) * f) * (1.0f / 8192.0f);
    }
}
static inline int16_t s16(float x)
{
    x *= 8192.0f;
    return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
}
static inline float drive(float x)               /* tanh's Pade: soft, bounded for |x| < 3 */
{
    x = fm_clampf(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

/* the room: four lines through a Hadamard matrix, damped, behind two diffusers */
static const int RV_LEN[4] = {1931, 2399, 2851, 3307};
#define RV_TOTAL (1931 + 2399 + 2851 + 3307)
#define AP1_N 223
#define AP2_N 347
static float rv_buf[RV_TOTAL] MM_POOL;
static float ap1[AP1_N], ap2[AP2_N];
static float *rv_line[4];
static int rv_i[4], ap1_i, ap2_i;
static float rv_lp[4], rv_g = 0.7f, rv_send;

static inline float rv_ap(float *b, int n, int *i, float x, float g)
{
    float z = b[*i], v = x - g * z;
    b[*i] = v;
    if (++*i >= n)
        *i = 0;
    return z + g * v;
}
static inline void room(float in, float *ol, float *or_)
{
    float o[4], a, b, c, d;
    int k;
    in = rv_ap(ap1, AP1_N, &ap1_i, in, 0.6f);
    in = rv_ap(ap2, AP2_N, &ap2_i, in, 0.6f);
    for (k = 0; k < 4; k++) {
        o[k] = rv_line[k][rv_i[k]];
        rv_lp[k] += 0.42f * (o[k] - rv_lp[k]);
    }
    a = rv_lp[0] + rv_lp[1];
    b = rv_lp[0] - rv_lp[1];
    c = rv_lp[2] + rv_lp[3];
    d = rv_lp[2] - rv_lp[3];
    rv_line[0][rv_i[0]] = fm_flush(in + rv_g * 0.5f * (a + c));
    rv_line[1][rv_i[1]] = fm_flush(in + rv_g * 0.5f * (b + d));
    rv_line[2][rv_i[2]] = fm_flush(rv_g * 0.5f * (a - c));
    rv_line[3][rv_i[3]] = fm_flush(rv_g * 0.5f * (b - d));
    for (k = 0; k < 4; k++)
        if (++rv_i[k] >= RV_LEN[k])
            rv_i[k] = 0;
    *ol = o[0] - 0.6f * o[2];
    *or_ = o[1] - 0.6f * o[3];
}

static void fx_init(void)
{
    int i;
    float *p = rv_buf;
    for (i = 0; i < ECHO_N; i++)
        e_l[i] = e_r[i] = 0;
    for (i = 0; i < RV_TOTAL; i++)
        rv_buf[i] = 0.0f;
    for (i = 0; i < 4; i++) {
        rv_line[i] = p;
        p += RV_LEN[i];
        rv_i[i] = 0;
        rv_lp[i] = 0.0f;
    }
    for (i = 0; i < AP1_N; i++)
        ap1[i] = 0.0f;
    for (i = 0; i < AP2_N; i++)
        ap2[i] = 0.0f;
    e_lpl = e_lpr = e_hpl = e_hpr = 0.0f;
}

/* ------------------------------------------------------------- params --- */
static void echo_target(void) { e_dt = fm_clampf(step_len * (float)ETIME_SIX[par[P_ETIME] & 3], 64.0f, (float)ECHO_N - 60.0f); }

static void apply(int p, int v)
{
    const mm_param_t *i = mm_param_info(p);
    if (p < 0 || p >= P_NPARAMS)
        return;
    v = v < i->lo ? i->lo : v > i->hi ? i->hi : v;
    par[p] = (int16_t)v;
    switch (p) {
    case P_DENSITY: publish_all(); break;
    case P_DRIFT:
        if (bars_left > mut_every(v))
            bars_left = mut_every(v);
        break;
    case P_FILTER: fk = (float)(v - 50) * 0.02f; break;
    case P_ECHO:
        e_send = (float)v * 0.0085f;
        e_fb = 0.22f + (float)v * 0.0054f;           /* to 0.76: the drive and the darkening keep it */
        break;
    case P_DRUMS: g_drums = knob_lvl(v); break;
    case P_STAB: g_stab = knob_lvl(v); break;
    case P_BASS: g_bass = knob_lvl(v); break;
    case P_BLEEP: g_bleep = knob_lvl(v); break;
    case P_ETIME: echo_target(); break;
    case P_REVERB:
        rv_send = (float)v * 0.006f;
        rv_g = 0.62f + (float)v * 0.0026f;
        break;
    case P_DUCK: duck_d = (float)v * 0.006f; break;
    case P_TEMPO:
        tempo_set();
        echo_target();
        break;
    default: break;
    }
}

static void load_preset(int i)
{
    int t;
    pr = &PRESETS[(unsigned)i < MM_NPRESETS ? i : 0];
    mm_preset_home(i, &home);
    gen = home;
    for (t = 0; t < MM_NT; t++) {
        pos[t] = (uint8_t)(gen.len[t] - 1);         /* the next sixteenth plays step 0 */
        tone_now[t] = 0.0f;
    }
    bar_six = 15;
    bars_left = mut_every(par[P_DRIFT]);
    mm_mut_part = mm_mut_step = mm_mut_kind = -1;
    mm_mut_glow = 0.0f;
    publish_all();
}

static void panic(void)
{
    k_amp = h_amp = p_amp = s_amp = s_att = b_amp = b_att = l_amp = 0.0f;
    fx_init();
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN], cmd = c >> 24, a = (c >> 16) & 0xFFu, b = c & 0xFFFFu;
        BARRIER();
        q_r++;
        switch (cmd) {
        case C_SET: apply((int)a, (int)(int16_t)b); break;
        case C_PRESET: load_preset((int)a); break;
        case C_PLAY:
            if (a && !mm_playing)
                tick_left = 0.0f;
            mm_playing = (uint8_t)(a != 0);
            break;
        case C_HOLD: mm_held = (uint8_t)(a != 0); break;
        case C_MUTE: hand |= (a & 0x3Fu) ^ mute; mute = a & 0x3Fu; break;
        case C_MUTATE: mut_now = 1; break;
        case C_SEED: rng ^= b * 2654435761u; if (!rng) rng = 1u; break;
        case C_PANIC: panic(); break;
        case C_HUSH:
            hush_req = 1;
            if (!mm_playing) {
                hush_on = 1;
                hush_wait = 4410;
            }
            break;
        case C_GEN:
            if (gen_in && mm_gen_ok(gen_in)) {
                int t;
                gen = *gen_in;
                for (t = 0; t < MM_NT; t++)
                    pos[t] = (uint8_t)(gen.len[t] - 1);
                bar_six = 15;
                publish_all();
            }
            gen_in = 0;
            break;
        default: break;
        }
    }
}

void mm_gen_get(mm_gen_t *g) { *g = gen; }

void mm_init(void)
{
    int p;
    q_w = q_r = 0;
    for (p = 0; p < P_NPARAMS; p++)
        par[p] = PARAMS[p].def;
    for (p = 0; p < P_NPARAMS; p++)
        apply(p, par[p]);
    panic();
    load_preset(0);
    for (p = 0; p < MM_NT; p++)
        lvl[p] = 1.0f;
    mm_playing = 0;
    mm_held = 0;
    mm_hushed = 0;
    hush_req = hush_on = 0;
    hush_g = 1.0f;
}

/* -------------------------------------------------------------- render --- */
#define SUB 16                                   /* the control rate: every 16 samples */
static float lim_env, run_g;
volatile float mm_lim_min = 1.0f;

static void control(uint32_t n)                  /* once a chunk: the slow things */
{
    int t;
    float fn = (float)n;
    for (t = 0; t < MM_NT; t++) {
        float tgt = ((mute >> t & 1u) || (gen.rest >> t & 1u)) ? 0.0f : 1.0f;
        float rate = (hand >> t & 1u) ? fn * (1.0f / (0.25f * MM_SR)) : fn / (step_len * 32.0f);   /* a hand's mute
                                                  * in a quarter second, a rest over two bars */
        lvl[t] += fm_clampf(tgt - lvl[t], -rate, rate);
        if (lvl[t] == tgt)
            hand &= ~(1u << t);                  /* there: a rest's fades are slow again */
        mm_level[t] = lvl[t];
        tone_now[t] += ((float)gen.tone[t] * 0.01f - tone_now[t]) * fm_minf(1.0f, fn / (step_len * 128.0f));
        mm_glow[t] *= fm_expf(-fn * (1.0f / (0.12f * MM_SR)));
    }
    run_g += fm_clampf((mm_playing ? 1.0f : 0.0f) - run_g, -fn / (2.0f * MM_SR), fn / (2.0f * MM_SR));   /* the dust */
    mm_mut_glow *= fm_expf(-fn * (1.0f / (0.9f * MM_SR)));
    if (mm_mut_glow < 0.002f)
        mm_mut_glow = 0.0f;
    /* the quiet moment for a save (mm_hush): down in 6 ms, up in 3 */
    if (hush_on) {
        hush_g -= fn * (1.0f / 256.0f);
        if (hush_g < 0.0f)
            hush_g = 0.0f;
        if (!mm_playing) {
            hush_wait -= (int32_t)n;
            if (hush_wait <= 0) {
                hush_on = 0;
                hush_req = 0;
            }
        }
    } else {
        hush_g += fn * (1.0f / 128.0f);
        if (hush_g > 1.0f)
            hush_g = 1.0f;
    }
    mm_hushed = (uint8_t)(hush_on && hush_g <= 0.0f);
    {   /* the echo's wow: a slow wander of its time */
        e_wph += fn * (0.31f / MM_SR);
        if (e_wph >= 1.0f)
            e_wph -= 1.0f;
        e_wow = 6.0f * fm_sin_turns(e_wph);
    }
    e_lpk = fm_clampf(2.0f * FM_PI * 2200.0f * fm_exp2f(1.2f * fk) / MM_SR, 0.05f, 0.9f);
    h_lpk = fm_clampf(2.0f * FM_PI * (4000.0f + 60.0f * (float)pr->hat_tone) * fm_exp2f(0.8f * fk + 0.4f * tone_now[T_HATS]) / MM_SR,
                      0.1f, 0.95f);
    {   /* the hats' high-pass: an SVF at 7 kHz */
        float g = FM_PI * 7000.0f / MM_SR, k = 1.3f;
        g = g + g * g * g * (1.0f / 3.0f);
        h_a1 = 1.0f / (1.0f + g * (g + k));
        h_a2 = g * h_a1;
        h_a3 = g * h_a2;
    }
}

static inline float blep(float t, float dt, float idt)
{
    if (t < dt) {
        float x = t * idt;
        return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - dt) {
        float x = (t - 1.0f) * idt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j0, j;
    const float lv_k = lvl[T_KICK] * g_drums, lv_h = lvl[T_HATS] * g_drums, lv_p = lvl[T_PERC] * g_drums;
    const float lv_s = lvl[T_STAB] * g_stab, lv_b = lvl[T_BASS] * g_bass, lv_l = lvl[T_BLEEP] * g_bleep;
    const float dust = (float)pr->dust * 0.01f * run_g, bwarm = (float)pr->bass_warm * 0.01f;
    const float s_res = (float)pr->stab_res * 0.01f;
    const float s_envo = (float)pr->stab_env * 0.1f * (1.0f + 0.3f * tone_now[T_STAB]);
    const float s_base = 210.0f * fm_exp2f(1.8f * fk + 0.3f * tone_now[T_STAB]);
    const int ptype = pr->perc_type;
    for (j0 = 0; j0 < n; j0 += SUB) {
        uint32_t m = n - j0 < SUB ? n - j0 : SUB;
        float sa1, sa2, sa3, dl = e_d + e_wow;
        {   /* the stab's filter, at the control rate */
            float fc = fm_clampf(s_base * fm_exp2f(s_envo * s_fe), 60.0f, 9000.0f);
            float g = FM_PI * fc / MM_SR;
            g = g + g * g * g * (1.0f / 3.0f) + g * g * g * g * g * (2.0f / 15.0f);
            s_k = 2.0f - 1.75f * s_res;
            sa1 = 1.0f / (1.0f + g * (g + s_k));
            sa2 = g * sa1;
            sa3 = g * sa2;
            s_fe *= fm_powf(s_fdec, (float)m);
        }
        e_d += (e_dt - e_d) * 0.0015f * (float)m;  /* a new echo time: the tape slides there */
        for (j = j0; j < j0 + m; j++) {
            float dry = 0.0f, l, r, kick = 0.0f, hat = 0.0f, perc = 0.0f, stab = 0.0f, bass = 0.0f, bl = 0.0f;
            float esend, rsend, el, er, wl, wr, duck;
            /* kick */
            if (k_amp > 1e-4f) {
                float f = k_f + k_punch * k_pe * k_pe;
                k_ph += f * (1.0f / MM_SR);
                if (k_ph >= 1.0f)
                    k_ph -= 1.0f;
                kick = fm_sin_turns(k_ph) * k_amp;
                if (k_click > 1e-4f) {
                    kick += noise() * k_click;
                    k_click *= 0.93f;
                }
                kick = kick * (1.25f - 0.25f * kick * kick);   /* a little round */
                k_amp *= k_dec;
                k_pe *= k_pdec;
            }
            duck_e = k_amp > duck_e ? k_amp : duck_e * 0.99985f;   /* the kick's swell, let go over 150 ms */
            /* hats */
            if (h_amp > 1e-4f) {
                float x = noise(), v3 = x - h_ic2, v1 = h_a1 * h_ic1 + h_a2 * v3, v2 = h_ic2 + h_a2 * h_ic1 + h_a3 * v3;
                h_ic1 = 2.0f * v1 - h_ic1;
                h_ic2 = 2.0f * v2 - h_ic2;
                x = x - 1.3f * v1 - v2;                  /* the high-pass */
                h_lp += h_lpk * (x - h_lp);
                hat = h_lp * h_amp;
                h_amp *= h_dec;
            }
            /* perc */
            if (p_amp > 1e-4f) {
                if (ptype == 0) {                        /* rim: two short tones and a click */
                    p_ph1 += 1720.0f * (1.0f + 0.15f * tone_now[T_PERC]) / MM_SR;
                    p_ph2 += 470.0f / MM_SR;
                    if (p_ph1 >= 1.0f)
                        p_ph1 -= 1.0f;
                    if (p_ph2 >= 1.0f)
                        p_ph2 -= 1.0f;
                    perc = (fm_sin_turns(p_ph1) + 0.6f * fm_sin_turns(p_ph2) + (p_t < 40 ? noise() : 0.0f)) * p_amp;
                    p_amp *= p_dec;
                } else if (ptype == 1) {                 /* clap: three bursts, then a tail, band-passed */
                    float x = noise(), g = 0.08f, k = 1.1f, a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                    float v3 = x - p_ic2, v1 = a1 * p_ic1 + a2 * v3, v2 = p_ic2 + a2 * p_ic1 + a3 * v3;
                    p_ic1 = 2.0f * v1 - p_ic1;
                    p_ic2 = 2.0f * v2 - p_ic2;
                    perc = v1 * 2.2f * p_amp;
                    if (p_t == 400 || p_t == 800)
                        p_amp = p_vel * 0.85f;
                    if (p_t == 1200) {
                        p_amp = p_vel * 0.6f;
                        p_dec = tau(0.07f * fm_exp2f(0.6f * tone_now[T_PERC]));
                    }
                    p_amp *= p_dec;
                } else {                                 /* shaker: high noise, a soft rise */
                    float x = noise(), env = p_t < 260 ? (float)p_t * (1.0f / 260.0f) : 1.0f;
                    p_lp += 0.5f * (x - p_lp);
                    p_lp2 += 0.25f * (x - p_lp - p_lp2);
                    perc = (x - p_lp - p_lp2) * p_amp * env * 0.7f;
                    if (p_t >= 260)
                        p_amp *= p_dec;
                }
                p_t++;
            }
            /* stab */
            if (s_att > 0.0f || s_amp > 1e-4f) {
                float x = 0.0f, v1, v2, v3;
                int i;
                for (i = 0; i < s_n; i++) {
                    int o;
                    for (o = 0; o < 2; o++) {
                        float t = s_ph[i][o];
                        x += t + t - 1.0f - blep(t, s_dt[i][o], s_idt[i][o]);
                        t += s_dt[i][o];
                        s_ph[i][o] = t >= 1.0f ? t - 1.0f : t;
                    }
                }
                x *= 0.2f;
                v3 = x - s_ic2;
                v1 = sa1 * s_ic1 + sa2 * v3;
                v2 = s_ic2 + sa2 * s_ic1 + sa3 * v3;
                s_ic1 = 2.0f * v1 - s_ic1;
                s_ic2 = 2.0f * v2 - s_ic2;
                if (s_att > 0.0f) {                      /* a 2 ms attack */
                    s_amp += 1.0f / 88.0f;
                    if (s_amp >= 1.0f) {
                        s_amp = 1.0f;
                        s_att = 0.0f;
                    }
                } else {
                    s_amp *= s_dec;
                }
                stab = drive(v2 * 1.6f) * s_amp * s_vel;
            }
            /* bass */
            if (b_att > 0.0f || b_amp > 1e-4f) {
                float s;
                b_ph += b_f * (1.0f + b_pe) * (1.0f / MM_SR);
                if (b_ph >= 1.0f)
                    b_ph -= 1.0f;
                b_pe *= 0.9985f;
                if (b_att > 0.0f) {
                    b_amp += 1.0f / 160.0f;
                    if (b_amp >= 1.0f) {
                        b_amp = 1.0f;
                        b_att = 0.0f;
                    }
                } else {
                    b_amp *= b_dec;
                }
                s = fm_sin_turns(b_ph);
                s = s + bwarm * (0.22f * fm_sin_turns(b_ph * 2.0f) - 0.12f * s * s * s);
                bass = s * b_amp * b_vel;
            }
            /* bleep */
            if (l_amp > 1e-4f) {
                float f = l_f * (1.0f + l_pe);
                l_ph += f * (1.0f / MM_SR);
                l_mph += f * l_ratio * (1.0f / MM_SR);
                if (l_ph >= 1.0f)
                    l_ph -= 1.0f;
                if (l_mph >= 1.0f)
                    l_mph -= 1.0f;
                bl = fm_sin_turns(l_ph + l_idx * fm_sin_turns(l_mph) * (1.0f / FM_TWO_PI)) * l_amp;
                l_amp *= l_dec;
                l_idx *= l_idec;
                l_pe *= 0.996f;
            }
            /* the mix */
            duck = 1.0f - duck_d * fm_minf(duck_e, 1.0f);
            kick *= 0.5f * lv_k;
            hat *= 0.36f * lv_h;
            perc *= (ptype == 0 ? 0.3f : ptype == 1 ? 0.35f : 0.4f) * lv_p;
            stab *= 0.6f * lv_s;
            bass *= 0.30f * lv_b * (1.0f - 0.5f * (1.0f - duck));
            bl *= 0.17f * lv_l;
            dry = kick + bass + stab * duck * 0.8f;
            esend = (stab * 1.1f + bl * 0.9f + perc * 0.25f) * e_send;
            rsend = (perc * 0.6f + hat * 0.25f + stab * 0.3f + bl * 0.5f) * rv_send;
            /* the echo */
            {
                float rl = e_read(e_l, dl), rr = e_read(e_r, dl + 37.0f);
                e_lpl += e_lpk * (rl - e_lpl);
                e_lpr += e_lpk * (rr - e_lpr);
                e_hpl += 0.012f * (e_lpl - e_hpl);
                e_hpr += 0.012f * (e_lpr - e_hpr);
                el = e_lpl - e_hpl;
                er = e_lpr - e_hpr;
                e_l[e_w] = s16(drive(esend + e_fb * er));
                e_r[e_w] = s16(drive(e_fb * el));
                if (++e_w >= ECHO_N)
                    e_w = 0;
            }
            room(rsend + (el + er) * 0.12f * rv_send, &wl, &wr);
            /* dust: a crackle now and then, a breath of hiss */
            if (dust > 0.0f) {
                float c = 0.0f;
                if ((rnd() & 0xFFFFu) < (uint32_t)(dust * 18.0f))
                    c = noise() * 0.5f;
                d_lp += 0.35f * (c - d_lp);
                d_hiss += 0.08f * (noise() - d_hiss);
                dry += (d_lp * 0.05f + d_hiss * 0.006f) * dust;
            }
            l = 0.74f * (dry + hat * 0.8f + perc * 0.7f + bl * 0.55f + (el * 1.2f) * duck + wl * 0.7f);
            r = 0.74f * (dry + hat * 1.0f + perc * 1.0f + bl * 0.85f + (er * 1.2f) * duck + wr * 0.7f);
            /* the limiter: never over 0.7, the gain back over a quarter second */
            {
                float a = fm_maxf(fm_fabsf(l), fm_fabsf(r)), g;
                lim_env = a > lim_env ? a : lim_env * 0.99991f;
                g = lim_env > 0.7f ? 0.7f / lim_env : 1.0f;
                if (g < mm_lim_min)
                    mm_lim_min = g;
                g *= hush_g * gain;
                l *= g;
                r *= g;
            }
            out[2 * j] = (int32_t)(fm_clampf(l, -0.98f, 0.98f) * 8388607.0f);
            out[2 * j + 1] = (int32_t)(fm_clampf(r, -0.98f, 0.98f) * 8388607.0f);
        }
    }
    mm_kick_env = fm_minf(duck_e, 1.0f);
}

void mm_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f);
    drain();
    while (n) {
        uint32_t k = n;
        if (mm_playing) {
            if (tick_left <= 0.0f) {
                tick();
                tick_left += step_len * (1.0f + ((bar_six & 1) ? -1.0f : 1.0f) * (float)par[P_SWING] * 0.005f);
            }
            if ((float)k > tick_left)
                k = (uint32_t)tick_left + 1u;
            if (k > n)
                k = n;
        }
        control(k);
        render_chunk(out, k, gain);
        if (mm_playing) {
            float len = step_len * (1.0f + ((bar_six & 1) ? -1.0f : 1.0f) * (float)par[P_SWING] * 0.005f);
            tick_left -= (float)k;
            mm_phase = fm_clampf(1.0f - tick_left / len, 0.0f, 1.0f);
        }
        out += 2 * k;
        n -= k;
    }
}
