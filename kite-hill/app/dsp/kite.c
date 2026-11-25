/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * KITE HILL: ashtonwallen, 2026 */
/* The instrument (kite.h). One compilation unit, -O2. The command queue, the room and the bus are FELT PIANO's
 * (AMB-1's shape). */
#include "kite.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Wind", 0, 100, 50},   {"Kites", 1, EN_NK, 4}, {"Space", 0, 100, 45}, {"Tone", 0, 100, 50},
    {"Breath", 0, 100, 35}, {"Drone", 0, 100, 40},  {"Swell", 0, 100, 50}, {"Width", 0, 100, 60},
    {"Ambience", 0, 100, 50}, {"Day", 0, 4, 2},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }
static const uint8_t DAY_MIN[5] = {20, 40, 60, 120, 240};

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
    if (i == P_DAYLEN) {
        int m = DAY_MIN[v < 0 ? 0 : v > 4 ? 4 : v];
        if (m >= 60)
            put_s(put_u(buf, (uint32_t)m / 60u), " h");
        else
            put_s(put_u(buf, (uint32_t)m), " min");
    } else if ((i == P_DRONE || i == P_AMBIENCE) && v == 0)
        put_s(buf, "Off");
    else
        put_u(buf, (uint32_t)(v < 0 ? 0 : v));
}

/* ------------------------------------------------------------- the hills --- */
/* each hill: its own settings (Wind Kites Space Tone, Breath Drone Swell Width), its drone's tonic (MIDI), the
 * kites' range (MIDI), the lines' sound, its weather (wind bias in hundredths, gustiness in tenths), its tuning set's
 * name, its ring of chords. A chord: its bass (semitones above the tonic) and its notes (bit i: i semitones above
 * the tonic). A just hill (ji) counts in harmonics of the tonic instead: bass and notes are harmonic numbers */
enum { TB_HUM, TB_WHISTLE, TB_REED, TB_GLASS, TB_N };
typedef struct {
    int8_t bass;
    uint32_t notes;
} chd_t;
typedef struct {
    const char *name, *mode;
    int8_t v[P_NSTYLE];
    uint8_t tonic, lo, hi, timbre, ji, nch, sky;
    int8_t bias, gusty;
    chd_t ch[4];
} style_t;
#define B(n) (1u << (n))
static const style_t STYLES[EN_NSTYLES] = {
    /* D major pentatonic over a moving bass: D6/9, Bm7, G (maj9, 13), A sus */
    {"Chalk Horse", "major pentatonic", {50, 4, 45, 50, 35, 40, 50, 60}, 38, 62, 86, TB_HUM, 0, 4, 0, 0, 10,
     {{0, B(0) | B(2) | B(4) | B(7) | B(9)}, {9, B(9) | B(0) | B(4) | B(7)}, {5, B(9) | B(0) | B(2) | B(4)},
      {7, B(7) | B(2) | B(9) | B(0)}}},
    /* E Lydian: Imaj9, II add9 (the raised fourth), Imaj7#11, vii m7 */
    {"Patang", "Lydian", {55, 5, 50, 45, 50, 30, 45, 70}, 40, 64, 90, TB_WHISTLE, 0, 4, 1, 5, 10,
     {{0, B(0) | B(4) | B(7) | B(11) | B(2)}, {2, B(2) | B(6) | B(9) | B(4)}, {0, B(0) | B(4) | B(7) | B(11) | B(6)},
      {11, B(11) | B(2) | B(6) | B(9)}}},
    /* the harmonic series on A: the major chord in its partials, with the seventh partial, open with the ninth,
     * then the fifth's own series */
    {"Seventh Partial", "harmonic series", {50, 4, 55, 60, 40, 50, 55, 55}, 33, 56, 86, TB_REED, 1, 4, 2, 0, 10,
     {{1, B(4) | B(5) | B(6) | B(8) | B(10) | B(12) | B(16) | B(20)}, {1, B(4) | B(6) | B(7) | B(8) | B(12) | B(14) | B(16)},
      {1, B(6) | B(8) | B(9) | B(12) | B(16) | B(18)}, {3, B(6) | B(9) | B(12) | B(15) | B(18)}}},
    /* D yo scale (D E G A B), open fifths and fourths */
    {"Rokkaku", "yo scale", {45, 3, 60, 40, 30, 35, 60, 65}, 38, 62, 88, TB_GLASS, 0, 4, 3, -5, 8,
     {{0, B(0) | B(7) | B(2) | B(5)}, {5, B(5) | B(9) | B(2) | B(7)}, {2, B(2) | B(9) | B(0) | B(5)},
      {0, B(0) | B(7) | B(9) | B(2)}}},
    /* G Dorian in a strong wind: i m9, IV add9, i m7, bVII add9 */
    {"Beaufort Six", "Dorian", {65, 6, 40, 65, 45, 45, 35, 65}, 43, 60, 84, TB_REED, 0, 4, 0, 15, 18,
     {{0, B(0) | B(3) | B(7) | B(10) | B(2)}, {5, B(5) | B(9) | B(0) | B(7)}, {0, B(0) | B(3) | B(7) | B(10)},
      {10, B(10) | B(2) | B(5) | B(0)}}},
    /* A Mixolydian: I add9, bVII add9, IV add9, v m7 */
    {"Linen Delta", "Mixolydian", {50, 4, 45, 55, 35, 45, 50, 55}, 45, 57, 81, TB_HUM, 0, 4, 1, 0, 10,
     {{0, B(0) | B(4) | B(7) | B(2)}, {10, B(10) | B(2) | B(5) | B(0)}, {5, B(5) | B(9) | B(0) | B(7)},
      {7, B(7) | B(10) | B(2) | B(5)}}},
    /* B Aeolian, low: i add9, bVImaj7, bIII add9, bVII add9 */
    {"Barrow Top", "minor", {45, 4, 55, 40, 40, 65, 60, 50}, 35, 54, 78, TB_HUM, 0, 4, 2, -2, 10,
     {{0, B(0) | B(3) | B(7) | B(2)}, {8, B(8) | B(0) | B(3) | B(7)}, {3, B(3) | B(7) | B(10) | B(5)},
      {10, B(10) | B(2) | B(5) | B(0)}}},
    /* C major, high and light: I add9, vi m7, IV add9, V7 sus */
    {"Sixpence Spool", "major", {48, 4, 40, 45, 55, 25, 40, 70}, 36, 67, 93, TB_WHISTLE, 0, 4, 3, -8, 6,
     {{0, B(0) | B(4) | B(7) | B(2)}, {9, B(9) | B(0) | B(4) | B(7)}, {5, B(5) | B(9) | B(0) | B(7)},
      {7, B(7) | B(0) | B(2) | B(5)}}},
    /* E Phrygian: i m7, bII maj7, iv m7, bvii m7 */
    {"Paper Hawk", "Phrygian", {55, 4, 50, 55, 45, 45, 45, 60}, 40, 59, 83, TB_REED, 0, 4, 2, 3, 16,
     {{0, B(0) | B(3) | B(7) | B(10)}, {1, B(1) | B(5) | B(8) | B(0)}, {5, B(5) | B(8) | B(0) | B(3)},
      {10, B(10) | B(1) | B(5) | B(8)}}},
    /* F suspended pentatonic (F G Bb C Eb): stacked fourths */
    {"Thermal Chimney", "suspended pentatonic", {55, 5, 55, 45, 35, 40, 55, 65}, 41, 60, 86, TB_GLASS, 0, 4, 1, 5,
     12, {{0, B(0) | B(7) | B(2) | B(5)}, {10, B(10) | B(5) | B(0) | B(2)}, {5, B(5) | B(0) | B(7) | B(10)},
          {7, B(7) | B(0) | B(2) | B(5)}}},
    /* C# minor pentatonic, high, no drone */
    {"Ninety Feet Up", "minor pentatonic", {60, 5, 50, 40, 60, 0, 45, 75}, 37, 66, 92, TB_WHISTLE, 0, 4, 0, 8, 12,
     {{0, B(0) | B(3) | B(7) | B(10)}, {3, B(3) | B(7) | B(10) | B(0)}, {10, B(10) | B(0) | B(3) | B(5)},
      {5, B(5) | B(10) | B(0) | B(3)}}},
    /* Bb major, low and lush, a gentle wind: Imaj9, iii m7, vi m9, IVmaj9 */
    {"Uncle's Diamond", "major sevenths", {42, 4, 60, 45, 30, 50, 75, 55}, 34, 55, 79, TB_GLASS, 0, 4, 3, -5, 6,
     {{0, B(0) | B(4) | B(7) | B(11) | B(2)}, {4, B(4) | B(7) | B(11) | B(2)}, {9, B(9) | B(0) | B(4) | B(7) | B(11)},
      {5, B(5) | B(9) | B(0) | B(4) | B(7)}}},
};
static const style_t *sty(int i) { return &STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES]; }
const char *en_style_name(int i) { return sty(i)->name; }
const char *en_style_mode(int i) { return sty(i)->mode; }
int en_style_sky(int i) { return sty(i)->sky; }
void en_style_recipe(int i, int16_t *par)
{
    int k;
    for (k = 0; k < P_NSTYLE; k++)
        par[k] = sty(i)->v[k];
}

/* twelve times log2 of the harmonics 1..24: their distance above the fundamental in semitones */
static const float HSEMI[25] = {0.0f,    0.0f,    12.0f,   19.020f, 24.0f,   27.863f, 31.020f, 33.688f, 36.0f,
                                38.039f, 39.863f, 41.513f, 43.020f, 44.405f, 45.688f, 46.884f, 48.0f,   49.050f,
                                50.039f, 50.976f, 51.863f, 52.708f, 53.513f, 54.282f, 55.020f};

/* the days' keys: each a fifth from the one before (back home every second day) */
static const int8_t KEY_WALK[8] = {0, 7, 0, 5, 0, 7, 2, 7};

static const char *const PHASE_NAME[PH_N] = {"still", "rising", "steady", "gusty", "easing"};
const char *en_phase_name(int ph) { return PHASE_NAME[(unsigned)ph < PH_N ? ph : 0]; }
static const float PH_START[PH_N] = {0.0f, 0.22f, 0.45f, 0.68f, 0.88f};
static int phase_of(float dp)
{
    int p = PH_N - 1;
    while (p > 0 && dp < PH_START[p])
        p--;
    return p;
}
void en_clock(float dp, char *b)                 /* 6:00 .. 21:00 */
{
    uint32_t m = (uint32_t)(fm_clampf(dp, 0.0f, 0.9999f) * 900.0f) + 360u;
    b = put_u(b, m / 60u);
    *b++ = ':';
    *b++ = (char)('0' + m % 60u / 10u);
    *b++ = (char)('0' + m % 10u);
    *b = 0;
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_STYLE, C_PLAY, C_SKIP, C_HOLD, C_DAY, C_POS, C_CHORD, C_NOTE, C_PANIC };
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
void en_hold(int on) { post(C_HOLD, on, 0); }
void en_note(int note, int vel) { post(C_NOTE, note, vel); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_world(uint32_t day, uint32_t pos, int chord)
{
    post(C_DAY, 0, (int)(day & 0xFFFFu));
    post(C_POS, 0, (int)(pos > 9999u ? 9999u : pos));
    post(C_CHORD, chord, 0);
}

/* -------------------------------------------------------------- state --- */
volatile en_kite_t EN_KITE[EN_NK];
volatile uint8_t en_playing, en_phase, en_hold_on, en_key, en_chord_i, en_quiet = 1;
volatile uint16_t en_chord_pcs;
volatile uint8_t en_chord_root;
volatile float en_wind, en_gust, en_daypos;
volatile uint32_t en_day = 1;
volatile float en_vu_l, en_vu_r;
volatile uint32_t en_now_ms, en_launches, en_breaks, en_gusts, en_changes;
volatile uint8_t en_kites;
#ifdef EN_HOST
void (*en_dbg)(int what, float t, int kite, float pitch, float x);
static float dbg_t;
#define DBG(w, k, p, x)                                                                                            \
    do {                                                                                                           \
        if (en_dbg)                                                                                                \
            en_dbg(w, dbg_t, k, p, x);                                                                             \
    } while (0)
#else
#define DBG(w, k, p, x) ((void)0)
#endif

static int16_t par[P_NPARAMS];
static int style_i;
static uint32_t rng = 0x9E3779B9u, nrng = 0x2545F491u, nrng2 = 0x6C8E9CF5u;
static uint32_t ms_rem;

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
static float pf(int p) { return (float)par[p] * 0.01f; }

#define SN 1024
static float SINT[SN + 1];
static inline __attribute__((always_inline)) float sn(float ph)
{
    float x = ph * (float)SN;
    int32_t i = (int32_t)x;
    float f = x - (float)i;
    i &= SN - 1;
    return SINT[i] + (SINT[i + 1] - SINT[i]) * f;
}
static inline float wrap1(float p) { return p >= 1.0f ? p - 1.0f : p; }
static inline float mtof(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }
static inline float tau(float sec) { return fm_expf(-1.0f / (sec * EN_SR)); }
static inline float onepole(float fc) { return 1.0f - fm_expf(-FM_TWO_PI * fc / EN_SR); }
static float smooth01(float x)
{
    x = fm_clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

/* ----------------------------------------------------------- the lines --- */
/* a kite line's hum (an aeolian tone): three partials (the timbre's), brighter as the wind rises, and a breath:
 * the wind's noise through a narrow resonator at the line's pitch */
static const float TB_RATIO[TB_N][3] = {{1.0f, 2.0f, 3.0f}, {1.0f, 2.0f, 3.0f}, {1.0f, 2.0f, 3.0f}, {1.0f, 1.0025f, 2.0f}};
static const float TB_AMP[TB_N][3] = {{1.0f, 0.26f, 0.08f}, {1.0f, 0.06f, 0.02f}, {0.8f, 0.42f, 0.30f}, {0.62f, 0.48f, 0.10f}};
static const float TB_BREATH[TB_N] = {1.0f, 1.9f, 0.8f, 0.6f};
static const float TB_GAIN[TB_N] = {1.0f, 1.45f, 0.9f, 1.35f};   /* the timbres level with each other */

#define BLK 256
typedef struct {
    uint8_t st, col, shape;
    float pitch, from, to, gl, gl_s;             /* the glide: from -> to, gl 0..1 over gl_s seconds */
    float ph[3];
    float amp, amp_t, lvl;                       /* lvl: the level the last chunk ended on */
    float alt, th, ax, sway, sway_r, turb, turb_t, turb_wait, low_t, t, loose_t, x, y, gain;
    float len;                                   /* its line: a long one flies high and hums low */
    float ry1, ry2;
} kite_t;
static kite_t kt[EN_NK];
static int col_next;

/* the drone: two slots, crossfading as the root moves */
static struct {
    float f, ph[3], lvl, tgt;
} dr[2];
static int dr_cur;

/* plucks: a line's ping as it goes taut, and the lines plucked by a big gust */
#define NPL 6
static struct {
    float y1[3], y2[3], c[3], r2[3];
    float g, gl, gr;
    int on;
} pl[NPL];
#define NPQ 12
static struct {
    int32_t wait;
    float pitch, vel, pan;
} plq[NPQ];

/* -------------------------------------------------------------- spaces --- */
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

/* -------------------------------------------------------------- the world --- */
static struct {
    float dp;                                    /* the day: 0..1 */
    int key;                                     /* semitones from the hill's tonic (KEY_WALK) */
    int ci;                                      /* the chord in the ring */
    float ch_left, ch_dur;                       /* seconds to the next chord */
    int homing;                                  /* dusk: home, then stay */
    float s1, s1t, s1w, s2, s2t, s2w;            /* the wind's slow wander */
    int g_st;                                    /* the gust: 0 none, 1 rising, 2 holding, 3 falling */
    float g_amt, g_peak, g_t, g_hold, g_rise, g_fall;
    float wind, wavg;
    float launch_wait, land_wait, twang_wait;
    float flap;                                  /* a kite's sail flapping (the bed) */
    float pend;                                  /* a key's kite waiting for a free line (MIDI; < 0 none) */
} w;

static void plq_add(float wait_s, float pitch, float vel, float pan)
{
    int i;
    for (i = 0; i < NPQ; i++)
        if (plq[i].wait < 0) {
            plq[i].wait = (int32_t)(wait_s * EN_SR);
            plq[i].pitch = pitch;
            plq[i].vel = vel;
            plq[i].pan = pan;
            return;
        }
}
static void pluck(float pitch, float vel, float pan)
{
    int i, m, best = 0;
    float lo = 1e9f;
    static const float PR[3] = {1.0f, 2.003f, 3.009f}, PA[3] = {1.0f, 0.32f, 0.12f}, PT[3] = {2.6f, 1.5f, 0.8f};
    for (i = 0; i < NPL; i++) {
        float l = pl[i].on ? fm_fabsf(pl[i].y1[0]) + fm_fabsf(pl[i].y2[0]) : -1.0f;
        if (l < lo) {
            lo = l;
            best = i;
        }
    }
    for (m = 0; m < 3; m++) {
        float f = mtof(pitch) * PR[m], wv = FM_TWO_PI * f / EN_SR, r, s;
        if (f > 12000.0f) {
            pl[best].c[m] = pl[best].r2[m] = pl[best].y1[m] = pl[best].y2[m] = 0.0f;
            continue;
        }
        r = fm_expf(-6.9078f / (PT[m] * EN_SR));
        s = fm_sinf(wv * 0.5f);
        pl[best].c[m] = 2.0f * r * (1.0f - 2.0f * s * s);
        pl[best].r2[m] = r * r;
        pl[best].y1[m] = vel * PA[m] * fm_sinf(wv);
        pl[best].y2[m] = 0.0f;
    }
    pl[best].g = 0.10f;
    pan *= pf(P_WIDTH);
    pl[best].gl = 1.0f - pan;
    pl[best].gr = 1.0f + pan;
    pl[best].on = 1;
    DBG(DB_PLUCK, best, pitch, vel);
}

/* the chord's notes in a range, as MIDI pitches (just hills: the harmonics, true) */
static int cands(int ci, float lo, float hi, float *out)
{
    const style_t *s = sty(style_i);
    const chd_t *c = &s->ch[ci % s->nch];
    int n = 0, m, h;
    if (s->ji) {
        float f0 = (float)s->tonic + (float)w.key;
        for (h = 1; h <= 24; h++)
            if (c->notes >> h & 1u) {
                float p = f0 + HSEMI[h];
                while (p > hi)
                    p -= 12.0f;
                if (p >= lo && n < 32)
                    out[n++] = p;
            }
    } else {
        int key = (s->tonic + w.key) % 12;
        for (m = (int)lo; m <= (int)hi && n < 32; m++)
            if (c->notes >> ((m - key + 120) % 12) & 1u)
                out[n++] = (float)m;
    }
    return n;
}
static float bass_pitch(int ci)                  /* the drone's root, near the tonic */
{
    const style_t *s = sty(style_i);
    const chd_t *c = &s->ch[ci % s->nch];
    float p = (float)s->tonic + (float)w.key + (s->ji ? HSEMI[c->bass] : (float)c->bass);
    float t = (float)s->tonic;
    while (p > t + 7.5f)
        p -= 12.0f;
    while (p < t - 4.5f)
        p += 12.0f;
    return p;
}
static void chord_out(void)                      /* the harmony, for the screen and the keys' lights */
{
    float c[32];
    int n = cands(w.ci, 24.0f, 108.0f, c), i;
    uint16_t m = 0;
    for (i = 0; i < n; i++)
        m |= (uint16_t)(1u << ((int)(c[i] + 0.5f) % 12));
    en_chord_pcs = m;
    en_chord_root = (uint8_t)((int)(bass_pitch(w.ci) + 0.5f) % 12);
    en_key = (uint8_t)((sty(style_i)->tonic + w.key) % 12);
    en_chord_i = (uint8_t)w.ci;
}

/* does a pitch sit badly with the other lines (a second, a minor ninth, a unison)? */
static int clash(float p, int self)
{
    int i;
    for (i = 0; i < EN_NK; i++) {
        float d;
        if (i == self || kt[i].st == KS_OFF || (kt[i].st != KS_FLY && kt[i].amp < 0.05f))
            continue;
        d = fm_fabsf(p - kt[i].to);
        if (d < 2.5f || (d > 12.5f && d < 13.5f))
            return 1;
    }
    return 0;
}
static int pc_used(float p, int self)
{
    int i, pc = (int)(p + 0.5f) % 12;
    for (i = 0; i < EN_NK; i++)
        if (i != self && kt[i].st == KS_FLY && (int)(kt[i].to + 0.5f) % 12 == pc)
            return 1;
    return 0;
}
static int flying(void)
{
    int i, n = 0;
    for (i = 0; i < EN_NK; i++)
        n += kt[i].st == KS_FLY;
    return n;
}

/* a new kite's note: from the chord, spread out, near the middle of the hill's range; the first of a sky on the
 * chord's root or fifth. A key's: the nearest that fits */
static float pick(float want)
{
    const style_t *s = sty(style_i);
    float c[32], best = -1.0f, bs = 1e9f, centre = ((float)s->lo + (float)s->hi) * 0.5f, root = bass_pitch(w.ci);
    int n = cands(w.ci, (float)s->lo, (float)s->hi, c), i, none = flying() == 0;
    for (i = 0; i < n; i++) {
        float sc;
        int bad = clash(c[i], -1);
        if (want >= 0.0f) {
            sc = fm_fabsf(c[i] - want) * 2.0f + (bad ? 40.0f : 0.0f);
        } else {
            int iv = ((int)(c[i] - root + 120.5f)) % 12;
            if (bad)
                continue;
            sc = fm_fabsf(c[i] - centre) * 0.22f + frand() * 2.5f + (pc_used(c[i], -1) ? 3.0f : 0.0f);
            if (none && (iv == 0 || iv == 7))
                sc -= 2.5f;
        }
        if (sc < bs) {
            bs = sc;
            best = c[i];
        }
    }
    return best;
}

static float anchor(void)                        /* a free place on the hill */
{
    float best = 0.5f, bd = -1.0f;
    int t, i;
    for (t = 0; t < 8; t++) {
        float a = 0.08f + 0.62f * frand(), d = 1.0f;
        for (i = 0; i < EN_NK; i++)
            if (kt[i].st != KS_OFF && kt[i].st != KS_LOOSE)
                d = fm_minf(d, fm_fabsf(a - kt[i].ax));
        if (d > bd) {
            bd = d;
            best = a;
        }
    }
    return best;
}

static int launch(float pitch, int keyed)
{
    int i;
    kite_t *k = 0;
    for (i = 0; i < EN_NK; i++)
        if (kt[i].st == KS_OFF) {
            k = &kt[i];
            break;
        }
    if (!k)
        return -1;
    k->st = KS_FLY;
    k->pitch = k->from = k->to = pitch;
    k->gl = 1.0f;
    k->ph[0] = k->ph[1] = k->ph[2] = 0.0f;
    k->amp = k->amp_t = k->lvl = 0.0f;
    k->alt = 0.0f;
    k->th = keyed ? 0.04f : 0.05f + 0.22f * frand();
    k->ax = anchor();
    k->sway = frand();
    k->sway_r = 0.07f + 0.08f * frand();
    k->turb = k->turb_t = 0.0f;
    k->turb_wait = 0.0f;
    k->low_t = k->t = k->loose_t = 0.0f;
    k->ry1 = k->ry2 = 0.0f;
    k->gain = 0.85f + 0.25f * frand();
    k->len = 1.0f - 0.45f * fm_clampf((pitch - (float)sty(style_i)->lo) / (float)(sty(style_i)->hi - sty(style_i)->lo), 0.0f, 1.0f);
    k->col = (uint8_t)(col_next++ % 8);
    k->shape = (uint8_t)(xs(&rng) % 3u);
    k->x = k->ax;
    k->y = 0.0f;
    en_launches++;
    plq_add(0.6f + 0.4f * frand(), pitch, keyed ? 0.55f : 0.32f, (k->ax - 0.4f) * 1.5f);   /* the line goes taut */
    DBG(DB_LAUNCH, (int)(k - kt), pitch, (float)keyed);
    return (int)(k - kt);
}

static void land(kite_t *k)
{
    if (k->st == KS_FLY) {
        k->st = KS_LAND;
        DBG(DB_LAND, (int)(k - kt), k->to, 0.0f);
    }
}

static void loose(kite_t *k)                      /* the line breaks: the kite drifts off, its tone falling */
{
    const style_t *s = sty(style_i);
    float c[32], best = k->pitch - 5.0f, bs = 1e9f;
    int n = cands(w.ci, (float)s->lo - 12.0f, k->pitch - 2.5f, c), i;
    for (i = 0; i < n; i++) {
        float d = k->pitch - c[i], sc = fm_fabsf(d - 5.5f);
        if (sc < bs) {
            bs = sc;
            best = c[i];
        }
    }
    k->st = KS_LOOSE;
    k->from = k->pitch;
    k->to = best;
    k->gl = 0.0f;
    k->gl_s = 11.0f;
    k->loose_t = 0.0f;
    en_breaks++;
    w.flap = 1.0f;
    DBG(DB_LOOSE, (int)(k - kt), k->pitch, best);
}

/* a new chord: the drone to its root; each line to the nearest note of it (held when it is in it), a step or two
 * at most, gliding; a line with nowhere near to go is reeled in */
static void harm_to(int ci)
{
    float c[32], took[EN_NK];
    const style_t *s = sty(style_i);
    int n, i, j, nt = 0, order[EN_NK], no = 0;
    w.ci = ci % s->nch;
    n = cands(w.ci, (float)s->lo - 3.0f, (float)s->hi + 3.0f, c);
    for (i = 0; i < EN_NK; i++)                  /* the loudest first: they keep their notes best */
        if (kt[i].st == KS_FLY)
            order[no++] = i;
    for (i = 1; i < no; i++)
        for (j = i; j > 0 && kt[order[j]].amp > kt[order[j - 1]].amp; j--) {
            int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    for (i = 0; i < no; i++) {
        kite_t *k = &kt[order[i]];
        float best = -1.0f, bs = 1e9f;
        for (j = 0; j < n; j++) {
            float d = c[j] - k->to, sc = fm_fabsf(d) + (d > 0.0f ? 0.1f : 0.0f);
            int t, bad = 0;
            for (t = 0; t < nt; t++) {
                float e = fm_fabsf(c[j] - took[t]);
                if (e < 2.5f || (e > 12.5f && e < 13.5f))
                    bad = 1;
                if (fm_fabsf(e - 12.0f) < 0.1f || fm_fabsf(e - 24.0f) < 0.1f)
                    sc += 1.6f;                  /* an octave of a line already placed: rather another note */
            }
            if (!bad && sc < bs) {
                bs = sc;
                best = c[j];
            }
        }
        if (best < 0.0f || bs > 5.2f) {
            land(k);
            continue;
        }
        took[nt++] = best;
        if (fm_fabsf(best - k->to) > 0.01f) {
            k->from = k->pitch;
            k->to = best;
            k->gl = 0.0f;
            k->gl_s = 1.6f + 1.2f * frand();
            DBG(DB_RETUNE, order[i], k->from, best);
        }
    }
    {                                            /* the drone */
        float p = bass_pitch(w.ci), f = mtof(p);
        if (fm_fabsf(f - dr[dr_cur].f) > 0.5f) {
            dr_cur ^= 1;
            dr[dr_cur].f = f;
            dr[dr_cur].tgt = 1.0f;
            dr[dr_cur ^ 1].tgt = 0.0f;
        }
    }
    chord_out();
    en_changes++;
    DBG(DB_CHORD, w.ci, bass_pitch(w.ci), (float)en_chord_pcs);
}

static float chord_dur(int ph)
{
    static const float D[PH_N] = {330.0f, 230.0f, 170.0f, 120.0f, 200.0f};
    float day = (float)DAY_MIN[par[P_DAYLEN]];
    return D[ph] * (0.8f + 0.4f * frand()) * fm_clampf(day / 60.0f, 0.6f, 1.6f);
}

/* the day's wind: still morning, rising at midday, steady, a gusty evening, easing */
static float day_wind(float dp)
{
    static const float X[8] = {0.0f, 0.20f, 0.42f, 0.55f, 0.68f, 0.86f, 0.95f, 1.0f};
    static const float Y[8] = {0.19f, 0.25f, 0.52f, 0.62f, 0.56f, 0.52f, 0.26f, 0.19f};
    int i;
    for (i = 1; i < 8; i++)
        if (dp <= X[i])
            return Y[i - 1] + (Y[i] - Y[i - 1]) * smooth01((dp - X[i - 1]) / (X[i] - X[i - 1]));
    return Y[7];
}
static const float GUST_PER_MIN[PH_N] = {0.15f, 0.6f, 0.8f, 2.2f, 0.35f};

static void new_day(void)
{
    en_day++;
    w.key = KEY_WALK[(en_day - 1u) % 8u];
    w.homing = 0;
    harm_to(0);
    w.ch_left = chord_dur(PH_MORNING);
    DBG(DB_DAY, (int)en_day, (float)w.key, 0.0f);
}

static void gust_start(void)
{
    int i, n = 0, ord[EN_NK];
    const style_t *s = sty(style_i);
    w.g_st = 1;
    w.g_t = 0.0f;
    w.g_peak = (0.14f + 0.24f * frand()) * (1.0f + 0.04f * (float)(s->gusty - 10));
    w.g_rise = 1.0f + 1.5f * frand();
    w.g_hold = 1.5f + 4.0f * frand();
    w.g_fall = 3.0f + 4.0f * frand();
    en_gusts++;
    DBG(DB_GUST, 0, w.g_peak, w.wind);
    if (w.g_peak > 0.24f) {
        w.flap = fm_maxf(w.flap, 0.6f);
        /* a big one plucks the singing lines, low to high, as it arrives */
        if (w.twang_wait <= 0.0f && en_phase >= PH_MIDDAY) {
            for (i = 0; i < EN_NK; i++)
                if (kt[i].st == KS_FLY && kt[i].amp > 0.2f)
                    ord[n++] = i;
            if (n >= 2) {
                int a, b;
                for (a = 1; a < n; a++)
                    for (b = a; b > 0 && kt[ord[b]].to < kt[ord[b - 1]].to; b--) {
                        int t = ord[b];
                        ord[b] = ord[b - 1];
                        ord[b - 1] = t;
                    }
                for (a = 0; a < n; a++)
                    plq_add(w.g_rise * 0.7f + 0.2f * (float)a + 0.05f * frand(), kt[ord[a]].to,
                            0.18f + 0.22f * kt[ord[a]].amp, (kt[ord[a]].ax - 0.4f) * 1.5f);
                w.twang_wait = 45.0f;
            }
        }
        /* the evening's gusts move the harmony on */
        if (en_phase == PH_EVENING && w.ch_left < w.ch_dur * 0.55f)
            w.ch_left = w.g_rise + 0.5f;
    }
}

static void world(float dt)
{
    const style_t *s = sty(style_i);
    int i, nfly, target, kmax = par[P_KITES];
    float swell = pf(P_SWELL), day = (float)DAY_MIN[par[P_DAYLEN]] * 60.0f, sum = 0.0f;
#ifdef EN_HOST
    dbg_t += dt;
#endif
    /* the day */
    if (!en_hold_on && en_playing) {
        w.dp += dt / day;
        if (w.dp >= 1.0f) {
            w.dp -= 1.0f;
            new_day();
        }
    }
    en_daypos = w.dp;
    en_phase = (uint8_t)phase_of(w.dp);
    /* the wind */
    w.s1w -= dt;
    if (w.s1w <= 0.0f) {
        w.s1t = (frand() - 0.5f) * 0.16f;
        w.s1w = 20.0f + 40.0f * frand();
    }
    w.s1 += (w.s1t - w.s1) * dt / 15.0f;
    w.s2w -= dt;
    if (w.s2w <= 0.0f) {
        w.s2t = (frand() - 0.5f) * 0.14f;
        w.s2w = 120.0f + 180.0f * frand();
    }
    w.s2 += (w.s2t - w.s2) * dt / 60.0f;
    if (w.g_st == 0) {
        float rate = GUST_PER_MIN[en_phase] / 60.0f * (float)s->gusty * 0.1f * (0.5f + pf(P_WIND));
        if (frand() < rate * dt)
            gust_start();
    } else {
        w.g_t += dt;
        if (w.g_st == 1) {
            w.g_amt = w.g_peak * smooth01(w.g_t / w.g_rise);
            if (w.g_t >= w.g_rise)
                w.g_st = 2, w.g_t = 0.0f;
        } else if (w.g_st == 2) {
            w.g_amt = w.g_peak;
            if (w.g_t >= w.g_hold)
                w.g_st = 3, w.g_t = 0.0f;
        } else {
            w.g_amt = w.g_peak * (1.0f - smooth01(w.g_t / w.g_fall));
            if (w.g_t >= w.g_fall)
                w.g_st = 0, w.g_amt = 0.0f;
        }
    }
    w.wind = fm_clampf(day_wind(w.dp) + (float)s->bias * 0.01f + (pf(P_WIND) - 0.5f) * 0.5f + w.s1 + w.s2 + w.g_amt,
                       0.12f, 1.0f);
    w.wavg += (w.wind - w.wavg) * fm_minf(dt / 30.0f, 1.0f);
    en_wind = w.wind;
    en_gust = w.g_amt;
    w.flap *= fm_expf(-dt / 0.7f);
    if (w.twang_wait > 0.0f)
        w.twang_wait -= dt;
    /* the harmony */
    w.ch_left -= dt;
    if (w.ch_left <= 0.0f && en_playing) {
        if (en_phase == PH_DUSK) {
            if (w.ci != 0)
                harm_to(0);
            w.homing = 1;
        } else {
            harm_to(w.ci + 1);
        }
        w.ch_dur = w.ch_left = chord_dur(en_phase);
    }
    /* the kites */
    nfly = flying();
    target = (int)((float)kmax * fm_clampf((w.wavg - 0.08f) / 0.45f, 0.4f, 1.0f) + 0.5f);
    if (target < 1)
        target = 1;
    w.launch_wait -= dt;
    w.land_wait -= dt;
    if (w.pend >= 0.0f) {
        float p = pick(w.pend);
        if (p >= 0.0f && launch(p, 1) >= 0) {
            w.pend = -1.0f;
        } else {                                 /* no free line: the faintest coming down makes room, or one is
                                                  * reeled in for it */
            kite_t *lo = 0;
            for (i = 0; i < EN_NK; i++)
                if (kt[i].st == KS_LAND && (!lo || kt[i].amp < lo->amp))
                    lo = &kt[i];
            if (lo && lo->amp < 0.03f) {
                lo->st = KS_OFF;
                lo->lvl = lo->amp = 0.0f;
            } else if (!lo && nfly == EN_NK) {
                for (i = 0; i < EN_NK; i++)
                    if (kt[i].st == KS_FLY && (!lo || kt[i].amp < lo->amp))
                        lo = &kt[i];
                if (lo)
                    land(lo);
            }
        }
    } else if (en_playing && nfly < target && w.launch_wait <= 0.0f && w.wind > 0.1f) {
        float p = pick(-1.0f);
        if (p >= 0.0f)
            launch(p, 0);
        w.launch_wait = (nfly == 0 ? 3.0f : 14.0f + 26.0f * frand()) * (en_phase == PH_MORNING ? 1.6f : 1.0f);
    } else if (nfly > target && w.land_wait <= 0.0f) {
        kite_t *lo = 0;
        for (i = 0; i < EN_NK; i++)
            if (kt[i].st == KS_FLY && kt[i].t > 30.0f && (!lo || kt[i].amp < lo->amp))
                lo = &kt[i];
        if (lo)
            land(lo);
        w.land_wait = 35.0f;
    }
    if (en_playing && w.g_st == 2 && w.wind > 0.72f && nfly >= 3 && frand() < dt / 150.0f) {
        kite_t *k = &kt[xs(&rng) % EN_NK];
        if (k->st == KS_FLY && k->t > 60.0f)
            loose(k);
    }
    for (i = 0; i < EN_NK; i++) {
        kite_t *k = &kt[i];
        float lift, a_tau;
        if (k->st == KS_OFF)
            continue;
        k->t += dt;
        k->turb_wait -= dt;
        if (k->turb_wait <= 0.0f) {
            k->turb_t = (frand() - 0.5f) * 0.5f;
            k->turb_wait = 1.5f + 3.5f * frand();
        }
        k->turb += (k->turb_t - k->turb) * fm_minf(dt / 1.5f, 1.0f);
        k->sway = wrap1(k->sway + dt * k->sway_r);
        lift = w.wind * (1.0f + k->turb);
        if (k->st == KS_FLY) {
            float at = fm_clampf((lift - k->th) / (0.75f - k->th * 0.5f), 0.0f, 1.0f), r = at > k->alt ? 0.05f : 0.035f,
                  sing;
            if (!en_playing)
                at = 0.0f, r = 0.1f;
            k->alt += fm_clampf(at - k->alt, -r * dt, r * dt);
            if (k->alt < 0.04f && w.wavg < k->th + 0.05f)
                k->low_t += dt;
            else
                k->low_t = 0.0f;
            if (k->low_t > 25.0f || (!en_playing && k->alt < 0.01f))
                land(k);
            sing = fm_clampf((lift * (0.55f + 0.45f * k->alt) - 0.02f) / 0.8f, 0.0f, 1.0f);
            sing = sing * (2.0f - sing);
            k->amp_t = en_playing ? sing : 0.0f;
            a_tau = 0.6f + 5.4f * swell;
            if (k->gl < 1.0f)                    /* retuning: a little softer through the glide */
                k->amp_t *= 0.75f;
        } else if (k->st == KS_LAND) {
            k->alt = fm_maxf(k->alt - 0.07f * dt, 0.0f);
            k->amp_t = 0.0f;
            a_tau = 1.8f;
            if (k->alt <= 0.0f && k->amp < 0.002f)
                k->st = KS_OFF;
        } else {                                 /* loose: up and away downwind */
            k->loose_t += dt;
            k->alt = fm_minf(k->alt + 0.025f * dt, 1.3f);
            k->len = fm_minf(k->len + 0.02f * dt, 1.3f);
            k->ax += (0.012f + 0.02f * w.wind) * dt;
            k->amp_t = 0.0f;
            a_tau = 4.5f;
            if (k->loose_t > 8.0f && k->amp < 0.002f)
                k->st = KS_OFF;
        }
        k->amp += (k->amp_t - k->amp) * fm_minf(dt / a_tau, 1.0f);
        if (k->gl < 1.0f) {
            k->gl = fm_minf(k->gl + dt / k->gl_s, 1.0f);
            k->pitch = k->from + (k->to - k->from) * (k->st == KS_LOOSE ? 1.0f - (1.0f - k->gl) * (1.0f - k->gl) : smooth01(k->gl));
        }
        sum += k->amp;
        /* the screen's kite: up from its anchor, leaning downwind */
        k->y = (0.1f + 0.82f * k->alt) * k->len;
        k->x = k->ax + (0.06f + 0.22f * k->alt) * (0.5f + w.wind) + 0.025f * sn(k->sway);
    }
    for (i = 0; i < EN_NK; i++) {
        volatile en_kite_t *e = &EN_KITE[i];
        e->x = kt[i].x;
        e->y = kt[i].y;
        e->ax = kt[i].ax;
        e->amp = kt[i].amp;
        e->pitch = kt[i].pitch;
        e->to = kt[i].to;
        e->note = (uint8_t)(kt[i].pitch + 0.5f);
        e->col = kt[i].col;
        e->shape = kt[i].shape;
        e->state = kt[i].st;
    }
    en_kites = (uint8_t)flying();
    en_quiet = (uint8_t)(w.g_st == 0 && w.flap < 0.05f);   /* a lull between gusts: a flash write here goes unheard */
    for (i = 0; i < NPQ; i++)                    /* the plucks due */
        if (plq[i].wait >= 0) {
            plq[i].wait -= (int32_t)(dt * EN_SR);
            if (plq[i].wait < 0) {
                if (en_playing)
                    pluck(plq[i].pitch, plq[i].vel, plq[i].pan);
                plq[i].wait = -1;
            }
        }
}

/* ------------------------------------------------------------- the sound --- */
static float bufl[BLK], bufr[BLK], nz1[BLK], nz2[BLK];
static float fade = 1.0f, fade_t = 1.0f;
static float hp_l, hp_r, lev;
static float vu_acc_l, vu_acc_r;
static uint32_t vu_n;
/* the wind bed */
static float rl1, rl2, rr1, rr2, wlow, wband, glp_l, glp_r, rust, rust_t, flp_low, flp_band, flap_ph, bed_g;

static void render_kites(uint32_t n)
{
    const style_t *s = sty(style_i);
    int i, m;
    uint32_t j;
    float tone = 0.3f + 1.4f * pf(P_TONE), br = pf(P_BREATH) * pf(P_BREATH) * 9.0f * TB_BREATH[s->timbre];
    float width = pf(P_WIDTH), lv = 0.15f;
    for (i = 0; i < EN_NK; i++) {
        kite_t *k = &kt[i];
        float f, inc[3], a[3], a0, a1, da, gl, gr, p, rc, rr2, rg, y1, y2, ph0, ph1, ph2, bright, reg, wv, r;
        if (k->st == KS_OFF)
            continue;
        a1 = k->amp * fm_sqrtf(k->amp) * lv * k->gain * TB_GAIN[s->timbre];
        a0 = k->lvl;
        k->lvl = a1;
        if (a0 < 1e-5f && a1 < 1e-5f)
            continue;
        f = mtof(k->pitch + 0.03f * sn(k->sway) + 0.05f * k->turb);   /* a line's tension: a few cents */
        reg = fm_clampf(1.0f - (k->pitch - 72.0f) * 0.012f, 0.7f, 1.15f);
        bright = (0.55f + 0.9f * w.wind * k->amp) * tone;
        for (m = 0; m < 3; m++) {
            inc[m] = TB_RATIO[s->timbre][m] * f / EN_SR;
            a[m] = TB_AMP[s->timbre][m] * (m ? bright : 1.0f) * reg;
            if (inc[m] > 0.3f)
                a[m] = 0.0f;
        }
        da = (a1 - a0) / (float)n;
        p = fm_clampf((k->x - 0.45f) * 1.4f, -0.8f, 0.8f) * width;
        gl = 0.72f * (1.0f - p);
        gr = 0.72f * (1.0f + p);
        wv = FM_TWO_PI * f / EN_SR;
        r = fm_expf(-FM_PI * (4.0f + f * 0.012f) / EN_SR);
        rc = 2.0f * r * fm_cosf(wv);
        rr2 = r * r;
        rg = (1.0f - r) * 2.0f * fm_sinf(wv) * br * reg;
        y1 = k->ry1, y2 = k->ry2, ph0 = k->ph[0], ph1 = k->ph[1], ph2 = k->ph[2];
        for (j = 0; j < n; j++) {
            float v = a[0] * sn(ph0) + a[1] * sn(ph1) + a[2] * sn(ph2), y = rc * y1 - rr2 * y2 + rg * nz1[j];
            y2 = y1;
            y1 = y;
            ph0 = wrap1(ph0 + inc[0]);
            ph1 = wrap1(ph1 + inc[1]);
            ph2 = wrap1(ph2 + inc[2]);
            a0 += da;
            v = (v + y) * a0;
            bufl[j] += v * gl;
            bufr[j] += v * gr;
        }
        k->ry1 = fm_flush(y1), k->ry2 = fm_flush(y2), k->ph[0] = ph0, k->ph[1] = ph1, k->ph[2] = ph2;
    }
}

static void render_drone(uint32_t n)
{
    int d;
    uint32_t j;
    float lvl = pf(P_DRONE) * fm_sqrtf(pf(P_DRONE)) * 0.13f * (0.55f + 0.6f * w.wavg) * (en_playing ? 1.0f : 0.0f);
    float k = 1.0f - fm_expf(-(float)n / (4.0f * EN_SR));
    for (d = 0; d < 2; d++) {
        float a0 = dr[d].lvl, a1, da, i0, i1, i2, p0 = dr[d].ph[0], p1 = dr[d].ph[1], p2 = dr[d].ph[2];
        dr[d].lvl += (dr[d].tgt - dr[d].lvl) * k;
        a1 = dr[d].lvl * lvl;
        a0 *= lvl;
        if (a0 < 1e-5f && a1 < 1e-5f)
            continue;
        da = (a1 - a0) / (float)n;
        i0 = dr[d].f / EN_SR;
        i1 = i0 * 2.0f;
        i2 = i0 * 1.5f;
        for (j = 0; j < n; j++) {
            float v = (sn(p0) + 0.30f * sn(p1) + 0.26f * sn(p2)) * a0;
            p0 = wrap1(p0 + i0);
            p1 = wrap1(p1 + i1);
            p2 = wrap1(p2 + i2);
            a0 += da;
            bufl[j] += v;
            bufr[j] += v;
        }
        dr[d].ph[0] = p0, dr[d].ph[1] = p1, dr[d].ph[2] = p2;
    }
}

static void render_plucks(uint32_t n)
{
    int i, m;
    uint32_t j;
    for (i = 0; i < NPL; i++) {
        float acc = 0.0f;
        if (!pl[i].on)
            continue;
        for (m = 0; m < 3; m++) {
            float y1 = pl[i].y1[m], y2 = pl[i].y2[m], c = pl[i].c[m], r2 = pl[i].r2[m], g = pl[i].g;
            if (c == 0.0f)
                continue;
            for (j = 0; j < n; j++) {
                float y = c * y1 - r2 * y2;
                y2 = y1;
                y1 = y;
                bufl[j] += y * g * pl[i].gl;
                bufr[j] += y * g * pl[i].gr;
            }
            pl[i].y1[m] = y1, pl[i].y2[m] = y2;
            acc += fm_fabsf(y1) + fm_fabsf(y2);
        }
        if (acc < 1e-5f)
            pl[i].on = 0;
    }
}

static void render_room(uint32_t n)
{
    uint32_t j;
    float room = pf(P_SPACE), send = 0.15f + 0.5f * room, g = 0.6f + 0.34f * room, damp = 0.45f;
    float dry = 1.0f - 0.4f * room * room, wet = 0.35f + 0.6f * room;
    for (j = 0; j < n; j++) {
        float in = (bufl[j] + bufr[j]) * 0.5f * send, x[4], sum, yl, yr;
        int i;
        for (i = 0; i < 3; i++) {
            float *d = ap_d[i], v = d[ap_p[i]], ww = in + 0.62f * v;
            d[ap_p[i]] = ww;
            in = v - 0.62f * ww;
            if (++ap_p[i] >= AP_N[i])
                ap_p[i] = 0;
        }
        for (i = 0; i < 4; i++)
            x[i] = rv_d[i][rv_p[i]];
        sum = (x[0] + x[1] + x[2] + x[3]) * 0.5f;
        for (i = 0; i < 4; i++) {
            float y = (x[i] - sum) * g;
            rv_lp[i] += (y - rv_lp[i]) * (1.0f - damp);
            rv_d[i][rv_p[i]] = fm_flush(rv_lp[i]) + in * (i & 1 ? 0.5f : -0.5f);
            if (++rv_p[i] >= RV_N[i])
                rv_p[i] = 0;
        }
        yl = x[0] + x[2] * 0.6f;
        yr = x[1] + x[3] * 0.6f;
        bufl[j] = bufl[j] * dry + yl * wet;
        bufr[j] = bufr[j] * dry + yr * wet;
    }
}

/* the wind bed, after the room (it is the open air itself): a low roar, a whoosh that rises with the gusts, the
 * grass, a sail flapping. Ambience is its level alone */
static void render_bed(uint32_t n)
{
    uint32_t j;
    float amb = pf(P_AMBIENCE), wd = w.wind, g;
    float r_a = onepole(110.0f + 420.0f * wd), roar = wd * fm_sqrtf(wd) * 1.5f;
    float wf = 2.0f * fm_sin_pi(fm_clampf((380.0f + 900.0f * w.g_amt + 500.0f * wd) / EN_SR, 0.0f, 0.15f));
    float whoosh = (0.04f + 0.5f * w.g_amt + 0.1f * wd) * 0.6f, grass = wd * wd * 0.10f, ga = onepole(2600.0f);
    float ff = 2.0f * fm_sin_pi(900.0f / EN_SR), flap = w.flap * 0.22f, fi = (13.0f + 7.0f * wd) / EN_SR;
    bed_g += ((en_playing ? amb * amb * 4.0f : 0.0f) - bed_g) * fm_minf((float)n / (EN_SR * 0.5f), 1.0f);
    g = bed_g;
    if (g < 1e-5f)
        return;
    rust_t = 0.4f + 0.6f * frand() * (0.5f + w.g_amt * 2.0f);
    for (j = 0; j < n; j++) {
        float a = nz1[j], b = nz2[j], l, r, hl, hr, fl;
        rl1 += (a - rl1) * r_a;
        rl2 += (rl1 - rl2) * r_a;
        rr1 += (b - rr1) * r_a;
        rr2 += (rr1 - rr2) * r_a;
        wlow += wf * wband;                      /* the whoosh: a state-variable band-pass */
        wband += wf * ((a + b) * 0.5f - wlow - 0.7f * wband);
        glp_l += (a - glp_l) * ga;
        glp_r += (b - glp_r) * ga;
        hl = a - glp_l;
        hr = b - glp_r;
        rust += (rust_t - rust) * 0.0005f;
        l = rl2 * roar + wband * whoosh + hl * grass * rust;
        r = rr2 * roar + wband * whoosh * 0.9f + hr * grass * rust;
        if (flap > 1e-4f) {                      /* fabric snapping in the wind: a band of noise, chopped */
            flp_low += ff * flp_band;
            flp_band += ff * (a - flp_low - 0.5f * flp_band);
            flap_ph = wrap1(flap_ph + fi);
            fl = flp_band * flap * fm_maxf(sn(flap_ph), 0.0f);
            l += fl * 0.8f;
            r += fl * 1.1f;
        }
        bufl[j] += l * g;
        bufr[j] += r * g;
    }
    wlow = fm_flush(wlow), wband = fm_flush(wband), flp_low = fm_flush(flp_low), flp_band = fm_flush(flp_band);
}

static void render_bus(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    float hp_a = onepole(28.0f), lv_rel = tau(0.5f), lv_att = 1.0f - tau(0.003f), fr = 1.0f / (1.2f * EN_SR);
    for (j = 0; j < n; j++) {
        float l = bufl[j] * fade, r = bufr[j] * fade, x, vol;
        fade = fade < fade_t ? fm_minf(fade + fr, fade_t) : fm_maxf(fade - fr, fade_t);
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

/* ------------------------------------------------------------ the control --- */
static void quiet(void)
{
    int i;
    for (i = 0; i < EN_NK; i++) {
        kt[i].st = KS_OFF;
        kt[i].amp = kt[i].lvl = 0.0f;
    }
    for (i = 0; i < NPL; i++)
        pl[i].on = 0;
    for (i = 0; i < NPQ; i++)
        plq[i].wait = -1;
    w.pend = -1.0f;
}

static void hill_start(void)                     /* a hill taken up: its harmony, its first kite soon */
{
    int i;
    for (i = 0; i < EN_NK; i++)
        if (kt[i].st == KS_FLY)
            land(&kt[i]);
    w.launch_wait = 1.5f;
    w.land_wait = 0.0f;
    w.homing = 0;
    for (i = 0; i < EN_NK; i++)                  /* the lines left in the air fade fast */
        if (kt[i].st != KS_OFF)
            kt[i].st = KS_LAND;
    harm_to(w.ci);
    w.ch_dur = w.ch_left = chord_dur((int)phase_of(w.dp));
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
            }
            break;
        case C_STYLE:
            if (a % EN_NSTYLES != style_i) {
                style_i = a % EN_NSTYLES;
                hill_start();
            }
            break;
        case C_PLAY:
            if (a && !en_playing) {
                en_playing = 1;
                fade_t = 1.0f;
                w.launch_wait = 1.0f;
            } else if (!a && en_playing) {
                en_playing = 0;
                fade_t = 0.0f;
            }
            break;
        case C_SKIP: {
            int ph = phase_of(w.dp);
            if (ph == PH_N - 1) {
                w.dp = 0.0f;
                new_day();
            } else {
                w.dp = PH_START[ph + 1];
            }
            w.ch_left = fm_minf(w.ch_left, 4.0f);
            break;
        }
        case C_HOLD: en_hold_on = (uint8_t)(a != 0); break;
        case C_DAY:
            en_day = ub ? ub : 1u;
            w.key = KEY_WALK[(en_day - 1u) % 8u];
            break;
        case C_POS: w.dp = (float)ub * 0.0001f; break;
        case C_CHORD:
            w.ci = a % sty(style_i)->nch;
            hill_start();
            break;
        case C_NOTE:
            if (b > 0)
                w.pend = (float)a;
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
    quiet();
    style_i = 0;
    en_day = 1;
    en_launches = en_breaks = en_gusts = en_changes = 0;
    w.key = KEY_WALK[0];
    w.dp = 0.30f;                                /* a fresh hill starts at late morning, the wind rising */
    w.ci = 0;
    w.wavg = w.wind = day_wind(w.dp);
    dr[0].f = dr[1].f = 0.0f;
    dr[0].lvl = dr[1].lvl = dr[0].tgt = dr[1].tgt = 0.0f;
    dr_cur = 0;
    en_playing = 0;
    fade = fade_t = 1.0f;
    hill_start();
}

static void clock_ms(uint32_t k)
{
    ms_rem += k * 1000u;
    en_now_ms += ms_rem / 44100u;
    ms_rem %= 44100u;
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.2f;
    drain();
    while (n) {
        uint32_t k = n > BLK ? BLK : n, j;
        world((float)k * (1.0f / EN_SR));
        for (j = 0; j < k; j++) {
            bufl[j] = bufr[j] = 0.0f;
            nz1[j] = (float)(int32_t)xs(&nrng) * (1.0f / 2147483648.0f);
            nz2[j] = (float)(int32_t)xs(&nrng2) * (1.0f / 2147483648.0f);
        }
        render_kites(k);
        render_drone(k);
        render_plucks(k);
        render_room(k);
        render_bed(k);
        render_bus(out, k, gain);
        clock_ms(k);
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
