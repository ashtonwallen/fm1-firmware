/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * RIGGING: ashtonwallen, 2026 */
/* The instrument (rig.h). One compilation unit, -O2. The command queue, the room and the bus are AMB-1's shape,
 * by way of FELT PIANO. */
#include "rig.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Wind", 0, 100, 45},   {"Gusts", 0, 100, 50},  {"Swell", 0, 100, 50}, {"Ambience", 0, 100, 50},
    {"Breath", 0, 100, 35}, {"Tone", 0, 100, 50},   {"Hull", 0, 100, 50},  {"Bell", 0, 100, 50},
    {"Room", 0, 100, 30},   {"Width", 0, 100, 60},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

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
    if ((i == P_BELL || i == P_AMB || i == P_HULL) && v <= 0)
        put_s(buf, "Off");
    else
        put_u(buf, (uint32_t)(v < 0 ? 0 : v));
}

int en_beaufort(float w)
{
    int f = (int)(w * 10.0f + 0.5f);
    return f < 0 ? 0 : f > 10 ? 10 : f;
}

static const char *const SECT_NAME[EN_NSECT] = {"close-hauled", "close reach", "beam reach", "broad reach", "running"};
const char *en_sector_name(int s) { return SECT_NAME[(unsigned)s < EN_NSECT ? s : 0]; }

/* ------------------------------------------------------------- presets --- */
/* a chord: its tones in cents above the preset's tonic (the first is its root, the next two its "triad", the ones
 * the middle ropes may take; the low ropes take the root and a fifth above it only) */
typedef struct {
    const char *name;
    uint8_t n;
    int16_t c[5];
} chord_t;
/* a preset: the user's ten (Wind Gusts Swell Ambience, Breath Tone Hull Bell, Room Width), the tonic, its chain of
 * chords across the points of sail, its near keys (cents), its weather (squalls and calms an hour, minutes between
 * the helm's course changes), the swell's period, the ropes' register (semitones) */
typedef struct {
    const char *name, *mode;
    int8_t v[P_NSTYLE];
    uint8_t tonic;
    chord_t ch[EN_NSECT];
    int16_t waters[3];
    uint8_t nwaters;
    uint8_t squalls, calms;                      /* an hour, in tenths */
    uint8_t course_min;
    uint8_t period;                              /* the swell, seconds */
    int8_t reg;
    uint8_t sky;                                 /* (the screen's) */
} style_t;

static const style_t STYLES[EN_NSTYLES] = {
    /*                     Win Gus Swe Amb  Bre Ton Hul Bel  Roo Wid */
    {"Box the Compass", "Dorian", {45, 50, 50, 50, 35, 50, 50, 50, 30, 60}, 2,
     {{"m9", 5, {0, 300, 700, 1000, 200}}, {"maj9", 5, {300, 700, 1000, 200, 500}},
      {"m11", 5, {700, 1000, 200, 500, 0}}, {"maj9", 5, {1000, 200, 500, 900, 0}},
      {"m11", 5, {200, 500, 900, 0, 700}}},
     {0, 700, -500}, 3, 6, 4, 14, 9, 0, 0},
    {"Cat's Paw", "major pentatonic", {24, 40, 30, 55, 50, 60, 35, 45, 25, 65}, 9,
     {{"6/9", 5, {0, 400, 700, 900, 200}}, {"m11", 5, {900, 0, 400, 700, 200}},
      {"9sus4", 5, {200, 700, 900, 0, 400}}, {"6sus4", 5, {700, 0, 200, 400, 900}},
      {"add9", 4, {0, 400, 700, 200, 0}}},
     {0, 700, 500}, 3, 0, 12, 12, 7, 5, 1},
    {"Roaring Forties", "minor", {66, 70, 85, 55, 45, 45, 70, 40, 20, 65}, 4,
     {{"m9", 5, {0, 300, 700, 1000, 200}}, {"maj9", 5, {800, 0, 300, 700, 1000}},
      {"m11", 5, {500, 800, 0, 300, 1000}}, {"6/9", 5, {300, 700, 1000, 0, 500}},
      {"add9", 4, {1000, 200, 500, 0, 0}}},
     {0, 500, 700}, 3, 22, 2, 18, 11, -3, 2},
    {"Doldrum Choir", "just tuning", {22, 35, 40, 50, 55, 35, 75, 60, 40, 60}, 0,
     {{"4:5:6:9", 4, {0, 386, 702, 204, 0}}, {"6:7:9", 3, {702, 969, 204, 0, 0}},
      {"4:6:7:9", 4, {0, 702, 969, 204, 0}}, {"8:10:12:15", 4, {0, 386, 702, 1088, 0}},
      {"4:5:6", 3, {498, 884, 0, 0, 0}}},
     {0, 702, 498}, 3, 0, 14, 20, 12, 0, 3},
    {"St. Elmo's Fire", "Lydian", {50, 60, 55, 50, 30, 75, 40, 40, 35, 65}, 5,
     {{"maj7#11", 5, {0, 400, 700, 1100, 600}}, {"m9", 5, {400, 700, 1100, 200, 600}},
      {"maj9", 5, {700, 1100, 200, 600, 900}}, {"m11", 5, {1100, 200, 600, 900, 400}},
      {"6/9", 5, {200, 600, 900, 1100, 400}}},
     {0, 700, 500}, 3, 26, 3, 12, 9, 3, 4},
    {"Ratline Hymn", "fifths", {45, 40, 50, 45, 75, 35, 60, 50, 55, 60}, 7,
     {{"5ths", 4, {0, 700, 200, 900, 0}}, {"5ths", 4, {700, 200, 900, 400, 0}},
      {"m7sus4", 5, {900, 200, 400, 700, 0}}, {"9sus4", 4, {200, 700, 900, 400, 0}},
      {"6/9", 5, {0, 400, 700, 900, 200}}},
     {0, 700, 500}, 3, 5, 5, 16, 10, 0, 5},
    {"Plimsoll Mark", "Mixolydian", {50, 45, 70, 50, 30, 35, 85, 60, 25, 55}, 10,
     {{"add9", 4, {0, 400, 700, 200, 0}}, {"m11", 5, {900, 0, 400, 700, 200}},
      {"6/9", 5, {500, 900, 0, 200, 700}}, {"m7", 5, {200, 500, 900, 0, 700}},
      {"maj9", 5, {1000, 200, 500, 900, 0}}},
     {0, 500, 700}, 3, 8, 4, 15, 10, -5, 6},
    {"Growler Watch", "Phrygian", {55, 50, 60, 55, 60, 30, 60, 70, 50, 60}, 4,
     {{"m7", 4, {0, 300, 700, 1000, 0}}, {"maj9", 5, {800, 0, 300, 700, 1000}},
      {"m11", 5, {500, 800, 0, 300, 1000}}, {"maj9", 5, {100, 500, 800, 0, 300}},
      {"m9", 5, {1000, 100, 500, 800, 0}}},
     {0, 500, 700}, 3, 10, 4, 16, 10, -2, 7},
    {"Spindrift", "minor pentatonic", {70, 85, 75, 55, 40, 60, 50, 35, 15, 70}, 11,
     {{"m11", 5, {0, 300, 700, 1000, 500}}, {"6/9", 5, {300, 700, 1000, 0, 500}},
      {"9sus4", 5, {500, 1000, 0, 300, 700}}, {"6sus4", 5, {1000, 300, 500, 700, 0}},
      {"m7sus4", 4, {700, 0, 500, 1000, 0}}},
     {0, 700, 500}, 3, 14, 1, 10, 8, 0, 8},
    {"Sargasso Slack", "major", {22, 30, 35, 50, 45, 55, 50, 50, 45, 60}, 0,
     {{"6/9", 5, {0, 400, 700, 900, 200}}, {"m11", 5, {900, 0, 400, 700, 200}},
      {"maj9", 5, {500, 900, 0, 400, 700}}, {"m11", 5, {200, 500, 900, 0, 700}},
      {"6/9", 5, {700, 1100, 200, 400, 900}}},
     {0, 700, 500}, 3, 0, 24, 22, 13, 2, 9},
    {"Tea Clipper", "major", {60, 55, 50, 45, 25, 70, 45, 40, 25, 70}, 9,
     {{"maj9", 5, {0, 400, 700, 1100, 200}}, {"m9", 5, {900, 0, 400, 700, 1100}},
      {"maj9", 5, {500, 900, 0, 400, 700}}, {"m11", 5, {200, 500, 900, 0, 700}},
      {"6/9", 5, {700, 1100, 200, 400, 900}}},
     {0, 700, 500}, 3, 6, 2, 9, 8, 2, 10},
    {"Hove To", "Dorian", {74, 35, 80, 55, 70, 40, 75, 50, 30, 60}, 5,
     {{"m9", 5, {0, 300, 700, 1000, 200}}, {"maj9", 5, {300, 700, 1000, 200, 500}},
      {"m11", 5, {700, 1000, 200, 500, 0}}, {"6/9", 5, {1000, 200, 500, 700, 0}},
      {"9", 5, {500, 900, 0, 300, 700}}},
     {0, 700, 500}, 3, 5, 1, 45, 12, -4, 11},
};
static const style_t *sty(int i) { return &STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES]; }
const char *en_style_name(int i) { return sty(i)->name; }
const char *en_style_mode(int i) { return sty(i)->mode; }
void en_style_recipe(int i, int16_t *par)
{
    int k;
    for (k = 0; k < P_NSTYLE; k++)
        par[k] = sty(i)->v[k];
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_STYLE, C_PLAY, C_TRIM, C_ABOUT, C_HOLD, C_WORLD, C_PANIC };
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
static en_world_t w_in;                          /* a saved world, waiting for the render */
void en_set(int p, int v) { post(C_SET, p, v); }
void en_style(int i) { post(C_STYLE, i, 0); }
void en_play(int on) { post(C_PLAY, on, 0); }
void en_trim(int deg) { post(C_TRIM, 0, deg); }
void en_about(void) { post(C_ABOUT, 0, 0); }
void en_hold(int on) { post(C_HOLD, on, 0); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_world_put(const en_world_t *w)
{
    w_in = *w;
    BARRIER();
    post(C_WORLD, 0, 0);
}

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_hold_on, en_sector, en_squall, en_calm, en_key, en_waters, en_bells, en_sounding;
volatile float en_rope_amp[EN_NR], en_rope_harm[EN_NR];
volatile uint8_t en_rope_note[EN_NR];
volatile float en_heading, en_wind_dir, en_rel, en_wind, en_gust, en_swell, en_swell_ph, en_fill, en_luff;
volatile uint32_t en_watch_s, en_bell_no;
volatile float en_vu_l, en_vu_r;
volatile uint32_t en_now_ms;
volatile uint32_t en_n_gusts, en_n_squalls, en_n_chords, en_n_courses, en_n_onsets;
#ifdef EN_HOST
void (*en_dbg_onset)(uint32_t ms, int rope, float midi, float amp);
void (*en_dbg_chord)(uint32_t ms, int sector, const float *midi, int n);
int en_dbg_fast, en_dbg_mute;
#endif

static int16_t par[P_NPARAMS];
static int style_i;
static uint32_t wrng = 0x52494731u, nrng = 0x2545F491u;
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
static float wrand(void) { return (float)(xs(&wrng) >> 8) * (1.0f / 16777216.0f); }   /* the world's own */
static inline float noise(void) { return (float)(int32_t)xs(&nrng) * (1.0f / 2147483648.0f); }
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
static inline float onepole(float fc) { return 1.0f - fm_expf(-FM_TWO_PI * fc / EN_SR); }
static float wrap180(float a)
{
    while (a > 180.0f)
        a -= 360.0f;
    while (a <= -180.0f)
        a += 360.0f;
    return a;
}
static float wrap360(float a)
{
    while (a >= 360.0f)
        a -= 360.0f;
    while (a < 0.0f)
        a += 360.0f;
    return a;
}
static float smooth01(float x)
{
    x = fm_clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

/* -------------------------------------------------------------- ropes --- */
/* the rig, bow to stern: each rope's natural pitch, where it is (-1 the stern .. 1 the bow), the wind's angle off
 * the bow that catches it best, and how hard the wind must blow to start it */
static const struct {
    float base, pos, peak, thr;
} RIG[EN_NR] = {
    {74, 0.95f, 50, 0.30f},  {67, 0.75f, 60, 0.22f},  {50, 0.55f, 95, 0.34f},   {71, 0.45f, 130, 0.46f},
    {62, 0.20f, 75, 0.20f},  {45, 0.00f, 100, 0.40f}, {77, 0.05f, 65, 0.52f},   {57, -0.20f, 140, 0.28f},
    {69, -0.40f, 85, 0.38f}, {53, -0.55f, 110, 0.44f}, {64, -0.75f, 160, 0.33f}, {81, -0.90f, 170, 0.58f},
};
typedef struct {
    float midi, next;                            /* its pitch, the one it takes once quiet */
    float ph[4], inc[4];                         /* the strands (two), the octave, the twelfth */
    float by1, by2, bc, br2, bg;                 /* the breath: a resonator on the noise */
    float amp, harm, turb, drift, gl, gr;
    float a_prev;
    uint8_t pending, armed, mute, mute_next;
} rope_t;
static rope_t rp[EN_NR];
static uint8_t order_lo[EN_NR];                  /* the ropes by register, low first (who chooses first) */
static float rope_mk = 1.0f;                    /* a light-air preset's few ropes a little up */

static void rope_tune(rope_t *r, float midi, int i)
{
    float f = mtof(midi), w = FM_TWO_PI * f / EN_SR, bw = 5.0f + f * 0.008f, rr = fm_expf(-FM_PI * bw / EN_SR);
    float det = 0.0009f + 0.0005f * (float)(i % 3);
    r->midi = midi;
    r->inc[0] = f / EN_SR;
    r->inc[1] = f * (1.0f + det) / EN_SR;
    r->inc[2] = 2.0f * f / EN_SR;
    r->inc[3] = 3.0f * f / EN_SR;
    r->bc = 2.0f * rr * fm_cosf(w);
    r->br2 = rr * rr;
    r->bg = (1.0f - rr) * 2.0f * fm_sinf(w) * fm_sqrtf(EN_SR / (FM_PI * bw)) * 0.35f;
    r->by1 = r->by2 = 0.0f;
    en_rope_note[i] = (uint8_t)(midi + 0.5f);
}

/* ------------------------------------------------------------ harmony --- */
static int sect, sect_shown;                     /* the point of sail; the chord the ropes are tuned to */
static int waters;
static int trans;                                /* a change under way: the ropes leaving are fading */
static float trans_t;
static int chord_now[6], chord_prev[6], n_now, n_prev;   /* pitch classes in cents (the checks) */
static int key_cents(void) { return (int)sty(style_i)->tonic * 100 + sty(style_i)->waters[waters]; }
static int cmod(int c)
{
    c %= 1200;
    return c < 0 ? c + 1200 : c;
}

static void chord_pcs(int s, int *out, int *n)
{
    const chord_t *c = &sty(style_i)->ch[s];
    int i, k = key_cents();
    for (i = 0; i < c->n; i++)
        out[i] = cmod(k + c->c[i]);
    *n = c->n;
}

/* each rope the chord tone nearest its own pitch: the low ropes the root or a fifth, the middle ones the triad;
 * never a semitone or a minor ninth against a rope already tuned, nor the same note twice */
static void retarget(float *nx, uint8_t *mute)
{
    const chord_t *c = &sty(style_i)->ch[sect];
    int k = key_cents(), oi, j;
    int fifth = -1;
    float reg = (float)sty(style_i)->reg;
    for (j = 1; j < c->n; j++) {
        int iv = cmod(c->c[j] - c->c[0]);
        if (iv >= 680 && iv <= 720)
            fifth = j;
    }
    for (oi = 0; oi < EN_NR; oi++) {
        int i = order_lo[oi], t, best = -1, m, nallow;
        float b = RIG[i].base + reg, cand[16], d[16];
        int nc = 0;
        nallow = b < 50.0f ? 1 : b < 57.0f ? 3 : c->n;
        for (t = 0; t < c->n; t++) {
            float pc, x;
            if (b < 50.0f ? (t != 0 && t != fifth) : t >= nallow)
                continue;
            pc = (float)cmod(k + c->c[t]) * 0.01f;
            x = pc + 12.0f * fm_floorf((b - pc) / 12.0f + 0.5f);   /* the octave nearest the rope */
            cand[nc] = x;
            d[nc++] = fm_fabsf(x - b);
            if (fm_fabsf(x + 12.0f - b) < 9.0f) {
                cand[nc] = x + 12.0f;
                d[nc++] = fm_fabsf(x + 12.0f - b);
            }
            if (fm_fabsf(x - 12.0f - b) < 9.0f) {
                cand[nc] = x - 12.0f;
                d[nc++] = fm_fabsf(x - 12.0f - b);
            }
        }
        for (m = 0; m < nc; m++) {              /* nearest first, if it sits well with those tuned */
            int a, ok = 1;
            if (best >= 0 && d[m] >= d[best])
                continue;
            for (a = 0; a < oi; a++) {
                float iv = fm_fabsf(cand[m] - nx[order_lo[a]]);
                if (mute[order_lo[a]])
                    continue;
                if (iv < 0.1f || (iv > 0.7f && iv < 1.3f) || (iv > 12.7f && iv < 13.3f))
                    ok = 0;
            }
            if (ok)
                best = m;
        }
        mute[i] = best < 0;                      /* none sits well: this rope is still for this chord */
        if (best < 0) {
            best = 0;
            for (m = 1; m < nc; m++)
                if (d[m] < d[best])
                    best = m;
        }
        nx[i] = nc ? cand[best] : b;
    }
}

static void harmony(int force)
{
    float nx[EN_NR];
    uint8_t mu[EN_NR];
    int i;
    retarget(nx, mu);
    for (i = 0; i < EN_NR; i++) {
        rp[i].next = nx[i];
        rp[i].mute_next = mu[i];
        rp[i].pending = force || fm_fabsf(nx[i] - rp[i].midi) > 0.04f || (mu[i] && !rp[i].mute);
    }
    trans = 1;
    trans_t = 0.0f;
}
/* the leaving ropes are quiet: the new tuning in (the common tones never stopped) */
static void harmony_apply(void)
{
    int i;
    for (i = 0; i < EN_NR; i++) {
        if (rp[i].pending) {
            rope_tune(&rp[i], rp[i].next, i);
            rp[i].pending = 0;
        }
        rp[i].mute = rp[i].mute_next;
    }
    for (i = 0; i < n_now; i++)
        chord_prev[i] = chord_now[i];
    n_prev = n_now;
    chord_pcs(sect, chord_now, &n_now);
    sect_shown = sect;
    en_key = (uint8_t)(cmod(key_cents() + 50) / 100 % 12);
    trans = 0;
    en_n_chords++;
#ifdef EN_HOST
    if (en_dbg_chord) {
        float m[EN_NR];
        for (i = 0; i < EN_NR; i++)
            m[i] = rp[i].midi;
        en_dbg_chord(en_now_ms, sect, m, EN_NR);
    }
#endif
}

void en_chord_text(char *b)
{
    const chord_t *c = &sty(style_i)->ch[sect_shown];
    int r = cmod(key_cents() + c->c[0] + 50) / 100 % 12, i, colon = 0;
    char *e;
    for (i = 0; c->name[i]; i++)
        colon |= c->name[i] == ':';
    e = put_s(b, EN_NOTE_NAME[r]);
    if (colon)
        e = put_s(e, " ");
    put_s(e, c->name);
}

/* -------------------------------------------------------------- world --- */
static struct {
    float hdg, hdg_t, wdir, wdir_t, dev, rate;   /* the course, its target; the wind's way; its mean's offset */
    float wind, wnow, turb;                      /* the wind (smoothed), with gusts and turbulence */
    float sw_ph, sw_amp;                         /* the swell */
    float cnt_course, cnt_weather, cnt_waters, cnt_squall, cnt_calm;   /* seconds to go */
    int sq;                                      /* squall: 0 none, 1 building, 2 blowing, 3 easing */
    float sqf;                                   /* its hold on the rig, 0..1 */
    float sq_t, sq_len;
    float calm_t;                                /* calm: seconds left */
    float watch, watch_frac;
    float speed;
    int about;
} wd;
#define NG 6
static struct {
    float t, s, off, sweep;
    uint32_t pick;                               /* which ropes it favours (two bits a rope) */
    int fore;                                    /* from the bow aft, or the stern forward */
} gu[NG];
static float gust_env(float x) { return x < 0.0f ? 0.0f : x < 1.1f ? x * (1.0f / 1.1f) : fm_expf(-(x - 1.1f) * 0.4f); }

static float rnd_exp(float mean_s) { return mean_s * (0.3f + 1.4f * wrand()); }
static float per_hour(int tenths) { return tenths ? 36000.0f / (float)tenths : 1e9f; }

static void world_defaults(void)
{
    wd.hdg = wd.hdg_t = 250.0f;
    wd.wdir = wd.wdir_t = 140.0f;              /* the wind 110 degrees off the bow, to starboard: a broad reach */
    wd.dev = 0.0f;
    wd.wind = pf(P_WIND);
    wd.watch = 1790.0f;                          /* the first bell soon */
    wd.cnt_course = 600.0f;
    wd.cnt_weather = 300.0f;
    wd.cnt_waters = 3000.0f;
    wd.cnt_squall = 900.0f;
    wd.cnt_calm = 1500.0f;
    wd.sq = 0;
    wd.calm_t = 0.0f;
    waters = 0;
}

void en_world_get(en_world_t *w)
{
    w->heading = wd.hdg_t;
    w->wind_dir = wd.wdir_t;
    w->wind_mean = wd.dev;
    w->watch_s = (uint32_t)wd.watch;
    w->rng = wrng;
    w->next_course_s = (uint32_t)fm_clampf(wd.cnt_course, 0.0f, 1e6f);
    w->next_weather_s = (uint32_t)fm_clampf(wd.cnt_weather, 0.0f, 1e6f);
    w->next_waters_s = (uint32_t)fm_clampf(wd.cnt_waters, 0.0f, 1e6f);
    w->next_squall_s = (uint32_t)fm_clampf(wd.cnt_squall, 0.0f, 1e6f);
    w->next_calm_s = (uint32_t)fm_clampf(wd.cnt_calm, 0.0f, 1e6f);
    w->waters = (uint8_t)waters;
    w->rsv[0] = w->rsv[1] = w->rsv[2] = 0;
}
static void world_take(const en_world_t *w)
{
    wd.hdg = wd.hdg_t = wrap360(w->heading);
    wd.wdir = wd.wdir_t = wrap360(w->wind_dir);
    wd.dev = fm_clampf(w->wind_mean, -0.25f, 0.25f);
    wd.watch = (float)(w->watch_s % 14400u);
    wrng = w->rng ? w->rng : 0x52494731u;
    wd.cnt_course = (float)w->next_course_s;
    wd.cnt_weather = (float)w->next_weather_s;
    wd.cnt_waters = (float)w->next_waters_s;
    wd.cnt_squall = (float)w->next_squall_s;
    wd.cnt_calm = (float)w->next_calm_s;
    waters = w->waters < sty(style_i)->nwaters ? w->waters : 0;
    {
        float rel = wrap180(wd.wdir - wd.hdg);   /* never kept in irons */
        if (fm_fabsf(rel) < 45.0f)
            wd.hdg = wd.hdg_t = wrap360(wd.wdir - (rel < 0.0f ? -60.0f : 60.0f));
    }
}

/* the points of sail by the wind's angle off the bow (abs), with a little stickiness at the edges */
static const float SECT_EDGE[EN_NSECT - 1] = {62.0f, 86.0f, 114.0f, 146.0f};
static int sector_of(float a, int cur)
{
    int s = 0;
    while (s < EN_NSECT - 1 && a >= SECT_EDGE[s])
        s++;
    if (s != cur) {
        int e = s > cur ? cur : s;               /* the edge between */
        if (s - cur == 1 || cur - s == 1) {
            if (fm_fabsf(a - SECT_EDGE[e]) < 3.5f)
                return cur;
        }
    }
    return s;
}

/* a new course: a point of sail or two away (now and then over the wind or before it) */
static void new_course(int fast)
{
    float rel = wrap180(wd.wdir - wd.hdg_t), a = fm_fabsf(rel), side = rel < 0.0f ? -1.0f : 1.0f, na;
    int s = sector_of(a, sect), ns, step = wrand() < 0.7f ? 1 : 2;
    static const float LO[EN_NSECT] = {48, 66, 90, 118, 150}, HI[EN_NSECT] = {58, 82, 108, 140, 172};
    if (fast && step < 2 && wrand() < 0.5f)
        step = 2;
    ns = wrand() < 0.5f ? s - step : s + step;
    if (ns < 0)
        ns = s + step;
    if (ns > EN_NSECT - 1)
        ns = s - step;
    ns = ns < 0 ? 0 : ns > EN_NSECT - 1 ? EN_NSECT - 1 : ns;
    if ((ns == 0 || ns == EN_NSECT - 1) && wrand() < 0.4f)
        side = -side;                            /* a tack, or a gybe */
    na = LO[ns] + (HI[ns] - LO[ns]) * wrand();
    wd.hdg_t = wrap360(wd.wdir - side * na);
    wd.rate = fast ? 1.2f : 0.35f;
    en_n_courses++;
}

static int playing_target(void) { return en_playing; }
static float bell_t[8];
static int bell_n, bell_i;
static float bell_clock;
static void bells_start(int count)
{
    int k;
    for (k = 0; k < count; k++)
        bell_t[k] = (float)(k / 2) * 1.35f + (float)(k % 2) * 0.48f + 0.2f;
    bell_n = count;
    bell_i = 0;
    bell_clock = 0.0f;
    en_bells = (uint8_t)count;
    en_bell_no++;
}

static void gust_new(float s)
{
    int i, best = 0;
    float rel = fm_fabsf(wrap180(wd.wdir - wd.hdg));
    for (i = 0; i < NG; i++)
        if (gu[i].t > gu[best].t)
            best = i;
    gu[best].t = 0.0f;
    gu[best].s = s;
    gu[best].fore = rel < 90.0f;
    gu[best].off = (wrand() - 0.5f) * 80.0f;
    gu[best].sweep = 0.35f + 0.6f * wrand();
    gu[best].pick = xs(&wrng);
    en_n_gusts++;
}

/* the world's step (a block's time, dt seconds) */
static void world_step(float dt)
{
    const style_t *st = sty(style_i);
    float rel, a, tgt, k, prev_ph;
    int i;
    /* the helm */
    if (!en_hold_on || wd.about) {
        wd.cnt_course -= dt;
        if (wd.cnt_course <= 0.0f || wd.about) {
            if (!en_hold_on || wd.about)
                new_course(wd.about);
            wd.about = 0;
            wd.cnt_course = rnd_exp((float)st->course_min * 60.0f);
        }
    }
    {
        float d = wrap180(wd.hdg_t - wd.hdg), mv = (wd.rate > 0.0f ? wd.rate : 0.35f) * dt;
        if (fm_fabsf(d) <= mv)
            wd.hdg = wd.hdg_t;
        else
            wd.hdg = wrap360(wd.hdg + (d > 0.0f ? mv : -mv));
    }
    /* the wind's way: slowly to its target, which moves with the weather */
    {
        float d = wrap180(wd.wdir_t - wd.wdir), mv = 0.04f * dt;
        wd.wdir = fm_fabsf(d) <= mv ? wd.wdir_t : wrap360(wd.wdir + (d > 0.0f ? mv : -mv));
    }
    rel = wrap180(wd.wdir - wd.hdg);
    a = fm_fabsf(rel);
    if (a < 44.0f && fm_fabsf(wrap180(wd.wdir - wd.hdg_t)) < 44.0f)   /* in irons, and not turning through: bear away */
        wd.hdg_t = wrap360(wd.wdir - (rel < 0.0f ? -52.0f : 52.0f));
    /* the weather */
    wd.cnt_weather -= dt;
    if (wd.cnt_weather <= 0.0f) {
        wd.dev = (wrand() - 0.5f) * 0.3f * fm_clampf(0.4f + pf(P_WIND), 0.5f, 1.0f);
        wd.wdir_t = wrap360(wd.wdir_t + (wrand() - 0.5f) * 40.0f);
        wd.cnt_weather = 240.0f + 480.0f * wrand();
    }
    wd.cnt_squall -= dt;
    if (wd.cnt_squall <= 0.0f) {
        if (wd.sq == 0 && wd.calm_t <= 0.0f && st->squalls) {
            wd.sq = 1;
            wd.sq_t = 0.0f;
            wd.sq_len = 60.0f + 100.0f * wrand();
            en_n_squalls++;
        }
        wd.cnt_squall = rnd_exp(per_hour(st->squalls));
    }
    wd.cnt_calm -= dt;
    if (wd.cnt_calm <= 0.0f) {
        if (wd.sq == 0 && st->calms)
            wd.calm_t = 120.0f + 200.0f * wrand();
        wd.cnt_calm = rnd_exp(per_hour(st->calms));
    }
    tgt = fm_clampf(pf(P_WIND) + wd.dev, 0.04f, 0.95f);
    k = dt / 30.0f;
    if (wd.calm_t > 0.0f) {
        wd.calm_t -= dt;
        tgt = 0.03f;
        k = dt / 25.0f;
    }
    if (wd.sq) {
        wd.sq_t += dt;
        if (wd.sq == 1) {
            tgt = fm_maxf(tgt, 0.92f);
            k = dt / 9.0f;
            if (wd.sq_t > 25.0f)
                wd.sq = 2, wd.sq_t = 0.0f;
        } else if (wd.sq == 2) {
            tgt = fm_maxf(tgt, 0.95f);
            k = dt / 6.0f;
            if (wd.sq_t > wd.sq_len)
                wd.sq = 3, wd.sq_t = 0.0f;
        } else {
            k = dt / 14.0f;
            if (wd.sq_t > 45.0f)
                wd.sq = 0;
        }
    }
    wd.wind += (tgt - wd.wind) * k;
    wd.turb += ((wrand() - 0.5f) * 0.4f - wd.turb) * (dt / 2.5f);
    en_squall = (uint8_t)(wd.sq == 1 || wd.sq == 2);
    en_calm = (uint8_t)(wd.calm_t > 0.0f && wd.wind < 0.12f);
    /* the swell: its height by Swell and the wind; a gust now and then on its crest */
    wd.sw_amp += (fm_clampf(pf(P_SWELL) * (0.6f + 0.6f * wd.wind), 0.0f, 1.0f) - wd.sw_amp) * (dt / 40.0f);
    prev_ph = wd.sw_ph;
    wd.sw_ph += dt / ((float)st->period * (1.0f + 0.1f * wd.turb));
    if (wd.sw_ph >= 1.0f)
        wd.sw_ph -= 1.0f;
    if (prev_ph < 0.25f && wd.sw_ph >= 0.25f) {
        float p = pf(P_GUSTS) * (0.15f + 0.85f * wd.wind);
        if (wd.wind > 0.1f && wrand() < p)
            gust_new(0.35f + 0.65f * wrand() * (0.4f + 0.6f * pf(P_GUSTS)));
    }
    if (en_squall && wrand() < dt / 4.0f)
        gust_new(0.6f + 0.4f * wrand());
    en_gust = 0.0f;
    for (i = 0; i < NG; i++)
        if (gu[i].t < 30.0f) {
            gu[i].t += dt;
            en_gust = fm_maxf(en_gust, gu[i].s * gust_env(gu[i].t));
        }
    wd.wnow = fm_clampf(wd.wind * (1.0f + 0.5f * en_gust) + 0.06f * wd.turb, 0.0f, 1.2f);
    /* the sails: full in a breeze, flapping in a lull or turning through the wind */
    en_luff = fm_clampf(1.0f - (a - 22.0f) / 22.0f, 0.0f, 1.0f);
    en_fill = fm_clampf(wd.wnow * 1.8f, 0.0f, 1.0f) * (1.0f - en_luff);
    wd.speed += (en_fill * (0.4f + 0.6f * fm_minf(wd.wnow * 1.5f, 1.0f)) - wd.speed) * (dt / 20.0f);
    /* near keys, now and then, at a quiet moment */
    wd.cnt_waters -= dt;
    if (wd.cnt_waters <= 0.0f && !en_squall && !trans) {
        int nw = st->nwaters > 1 ? (waters ? 0 : 1 + (int)(wrand() * (float)(st->nwaters - 1))) : 0;
        wd.cnt_waters = 2700.0f + 3300.0f * wrand();
        if (nw != waters) {
            waters = nw;
            en_waters = (uint8_t)waters;
            harmony(0);
        }
    }
    /* the point of sail: the chord */
    {
        int s = sector_of(a, sect);
        if (a >= 40.0f && s != sect) {
            sect = s;
            harmony(0);
        }
    }
    /* the watch's bells, every half hour */
    {
        float old = wd.watch;
        wd.watch += dt;
        if (wd.watch >= 14400.0f)
            wd.watch -= 14400.0f;
        if ((int)(old / 1800.0f) != (int)(wd.watch / 1800.0f)) {
            int n = (int)(wd.watch / 1800.0f) % 8;
            if (en_playing && pf(P_BELL) > 0.0f)
                bells_start(n == 0 ? 8 : n);
        }
    }
    en_heading = wd.hdg;
    en_wind_dir = wd.wdir;
    en_rel = rel;
    en_wind = wd.wnow;
    en_swell = wd.sw_amp;
    en_swell_ph = wd.sw_ph;
    en_sector = (uint8_t)sect_shown;
    en_watch_s = (uint32_t)wd.watch;
}

/* the ropes' levels: what the wind gives each, the strongest few (more as it blows harder) */
static void ropes_step(float dt)
{
    float rel = fm_fabsf(en_rel), exc[EN_NR], tg[EN_NR];
    int i, j, cap, play = playing_target(), loud = 0;
    float ka = 1.0f - fm_expf(-dt / 0.9f), kr = 1.0f - fm_expf(-dt / 2.6f), kp = 1.0f - fm_expf(-dt / 1.1f);
    float sens = fm_clampf(fm_powf(0.45f / fm_maxf((float)sty(style_i)->v[P_WIND] * 0.01f, 0.1f), 0.7f), 0.6f, 2.4f);
    rope_mk = sens > 1.0f ? 1.0f + 2.0f * (sens - 1.0f) : 1.0f;
    /* a squall swells in: the rig wakes rope by rope as it builds */
    wd.sqf += ((float)(wd.sq == 1 || wd.sq == 2) - wd.sqf) * (dt / (wd.sq == 1 || wd.sq == 2 ? 14.0f : 8.0f));
    cap = 1 + (int)(wd.wind * 4.6f + wd.sqf * 7.5f);
    for (i = 0; i < EN_NR; i++) {
        /* the steady wind holds the low and middle ropes; the gusts reach the high ones (a gust has its own angle,
         * so each lights a different few, in turn along the ship) */
        float d = (rel - RIG[i].peak) * (1.0f / 45.0f), expo = 0.3f + 0.7f / (1.0f + d * d), g = 0.0f;
        float up = fm_clampf((RIG[i].base - 45.0f) * (1.0f / 36.0f), 0.0f, 1.0f), w = fm_minf(wd.wind * sens, 1.0f);
        for (j = 0; j < NG; j++)
            if (gu[j].t < 30.0f) {
                float order = gu[j].fore ? (1.0f - RIG[i].pos) : (1.0f + RIG[i].pos);
                float dg = (rel + gu[j].off - RIG[i].peak) * (1.0f / 32.0f);
                g += gu[j].s * gust_env(gu[j].t - order * gu[j].sweep) * (0.25f + 0.75f / (1.0f + dg * dg)) *
                     (0.35f + 0.33f * (float)(gu[j].pick >> (2 * i) & 3u));
            }
        rp[i].turb += ((wrand() - 0.5f) * 2.0f - rp[i].turb) * (dt / 3.0f);
        rp[i].drift += (rp[i].turb - rp[i].drift) * (dt / 14.0f);
        exc[i] = (w * expo * (1.12f - 0.4f * up) * (1.0f + 0.16f * rp[i].drift) + w * g * (0.5f + 1.1f * up)) *
                 (1.0f - en_luff) + 0.03f * rp[i].turb;
        tg[i] = rp[i].mute ? -1.0f : exc[i] - RIG[i].thr * (1.0f - 0.4f * wd.sqf);
    }
    for (i = 0; i < EN_NR; i++) {                /* the strongest cap of them */
        int rank = 0;
        for (j = 0; j < EN_NR; j++)
            if (tg[j] > tg[i] || (tg[j] == tg[i] && j < i))
                rank++;
        if (rank >= cap)
            tg[i] = -1.0f;
    }
    if (trans) {                                 /* a change: the leaving ropes fade; then the new tuning */
        int all = 1;
        trans_t += dt;
        for (i = 0; i < EN_NR; i++)
            if (rp[i].pending && rp[i].amp > 0.006f)
                all = 0;
        if (all || trans_t > 9.0f)
            harmony_apply();
    }
    for (i = 0; i < EN_NR; i++) {
        rope_t *r = &rp[i];
        float t = play && tg[i] > 0.0f ? smooth01(tg[i] * (1.0f / 0.32f)) : 0.0f, h;
        r->a_prev = r->amp;
        if (r->pending)
            r->amp += (0.0f - r->amp) * (trans_t > 6.0f ? 1.0f - fm_expf(-dt / 0.25f) : kp);
        else
            r->amp += (t - r->amp) * (t > r->amp ? ka : kr);
        if (r->amp < 1e-5f)
            r->amp = 0.0f;
        h = fm_clampf((exc[i] - RIG[i].thr - 0.28f) * (1.0f / 0.45f), 0.0f, 1.0f);
        r->harm += (h - r->harm) * (dt / 4.0f);
        en_rope_amp[i] = r->amp;
        en_rope_harm[i] = r->harm;
        if (r->amp > 0.02f)
            loud++;
        if (r->armed && r->amp > 0.15f) {        /* (an onset: the tests' ear) */
            r->armed = 0;
            en_n_onsets++;
#ifdef EN_HOST
            if (en_dbg_onset)
                en_dbg_onset(en_now_ms, i, r->midi, t);
#endif
        } else if (r->amp < 0.05f) {
            r->armed = 1;
        }
    }
    en_sounding = (uint8_t)loud;
}

/* -------------------------------------------------------- hull and bell --- */
static struct {
    float ph[3], inc[3], amp, tgt, f5;           /* the root, its octave, its fifth */
} hull[2];
static int hull_cur;
static float hull_root = -1.0f;
static void hull_tune(void)
{
    const chord_t *c = &sty(style_i)->ch[sect_shown];
    float r = (float)cmod(key_cents() + c->c[0]) * 0.01f, f;
    int j, fifth = 0;
    while (r < 36.0f)
        r += 12.0f;
    while (r >= 48.0f)
        r -= 12.0f;
    for (j = 1; j < c->n; j++) {
        int iv = cmod(c->c[j] - c->c[0]);
        if (iv >= 680 && iv <= 720)
            fifth = 1;
    }
    if (fm_fabsf(r - hull_root) < 0.04f)
        return;
    hull_root = r;
    hull_cur ^= 1;
    f = mtof(r);
    hull[hull_cur].inc[0] = f / EN_SR;
    hull[hull_cur].inc[1] = 2.0f * f / EN_SR;
    hull[hull_cur].inc[2] = 3.0f * f / EN_SR;
    hull[hull_cur].f5 = fifth ? 0.22f : 0.0f;
    hull[hull_cur].amp = 0.0f;
}

#define NBP 5
static struct {
    float y1[NBP], y2[NBP], c[NBP], r2[NBP];
} bell;
static float bell_click;
static void bell_strike(void)
{
    static const float RAT[NBP] = {0.5f, 1.0f, 2.0f, 3.0f, 4.07f}, AMP[NBP] = {0.25f, 1.0f, 0.45f, 0.22f, 0.1f},
                       T60[NBP] = {7.0f, 5.5f, 3.2f, 2.0f, 1.1f};
    const chord_t *c = &sty(style_i)->ch[sect_shown];
    float r = (float)cmod(key_cents() + c->c[0]) * 0.01f, f;
    int m;
    while (r < 79.0f)
        r += 12.0f;
    f = mtof(r);
    for (m = 0; m < NBP; m++) {
        float fn = f * RAT[m], w = FM_TWO_PI * fn / EN_SR, rr = fm_expf(-6.9078f / (T60[m] * EN_SR));
        float s = fm_sinf(w * 0.5f);
        bell.c[m] = 2.0f * rr * (1.0f - 2.0f * s * s);
        bell.r2[m] = rr * rr;
        bell.y1[m] += AMP[m] * fm_sinf(w) * 0.13f;
    }
    bell_click = 0.02f;
}

/* -------------------------------------------------------------- spaces --- */
/* the air around the ship: four delay lines, a Householder mix, damped; three diffusers before */
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

/* -------------------------------------------------------------- render --- */
#define BLK 256
static float bufl[BLK], bufr[BLK], sendb[BLK];
static float hp_l, hp_r, lev, tone_l, tone_r;
static float vu_acc_l, vu_acc_r;
static uint32_t vu_n;
static float play_g;                             /* everything but the ropes, fading with PLAY */

static void apply_style(int i)
{
    style_i = (i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES;
    if (waters >= sty(style_i)->nwaters)
        waters = 0;
    en_waters = (uint8_t)waters;
    en_key = (uint8_t)(cmod(key_cents() + 50) / 100 % 12);
    harmony(1);
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(int16_t)(c & 0xFFFFu);
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_SET:
            if (a < P_NPARAMS) {
                const en_param_t *p = &PARAMS[a];
                par[a] = (int16_t)(b < p->lo ? p->lo : b > p->hi ? p->hi : b);
                if (a == P_WIND && en_now_ms < 1500u)   /* (at power-on: the preset's wind at once) */
                    wd.wind = pf(P_WIND);
                if (a == P_SWELL && en_now_ms < 1500u)
                    wd.sw_amp = pf(P_SWELL) * (0.6f + 0.6f * wd.wind);
            }
            break;
        case C_STYLE:
            if (a % EN_NSTYLES != style_i)
                apply_style(a);
            break;
        case C_PLAY: en_playing = (uint8_t)(a != 0); break;
        case C_TRIM: {
            float t = wrap360(wd.hdg_t + (float)b), rel = wrap180(wd.wdir - t);
            if (fm_fabsf(rel) < 45.0f)           /* not into the wind */
                t = wrap360(wd.wdir - (rel < 0.0f ? -45.0f : 45.0f));
            wd.hdg_t = t;
            wd.rate = 0.8f;
            break;
        }
        case C_ABOUT: wd.about = 1; break;
        case C_HOLD: en_hold_on = (uint8_t)(a != 0); break;
        case C_WORLD:
            world_take(&w_in);
            en_waters = (uint8_t)waters;
            harmony(1);
            break;
        case C_PANIC: {
            int i;
            for (i = 0; i < EN_NR; i++)
                rp[i].amp = 0.0f;
            bell_n = 0;
            break;
        }
        }
    }
}

void en_init(void)
{
    int i, j;
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
    for (i = 0; i < EN_NR; i++)                  /* who chooses first: low to high */
        order_lo[i] = (uint8_t)i;
    for (i = 1; i < EN_NR; i++)
        for (j = i; j > 0 && RIG[order_lo[j]].base < RIG[order_lo[j - 1]].base; j--) {
            uint8_t t = order_lo[j];
            order_lo[j] = order_lo[j - 1];
            order_lo[j - 1] = t;
        }
    for (i = 0; i < EN_NR; i++) {
        rp[i].amp = rp[i].harm = 0.0f;
        rp[i].armed = 1;
        rope_tune(&rp[i], RIG[i].base, i);
    }
    for (i = 0; i < NG; i++)
        gu[i].t = 100.0f;
    style_i = 0;
    world_defaults();
    sect = sector_of(fm_fabsf(wrap180(wd.wdir - wd.hdg)), 0);
    n_now = n_prev = 0;
    harmony(1);
    harmony_apply();
    hull_root = -1.0f;
    hull_tune();
    en_n_chords = en_n_gusts = en_n_squalls = en_n_courses = en_n_onsets = 0;
    en_hold_on = 0;
    en_now_ms = 0;
    ms_rem = 0;
    bell_n = 0;
    play_g = 0.0f;
}

/* the ropes: two strands, an octave, a twelfth, and the breath; each fading in and out over the block */
static void render_ropes(uint32_t n)
{
    int i;
    uint32_t j;
    float tone = pf(P_TONE), breath = pf(P_BREATH), width = pf(P_WIDTH) * 0.75f;
    float lv = 0.105f, inv = 1.0f / (float)n;
    for (i = 0; i < EN_NR; i++) {
        rope_t *r = &rp[i];
        float a0 = r->a_prev, a1 = r->amp, da, w1, w2, w3, wb, p0, p1, p2, p3, y1, y2, gl, gr, pan;
        float i0, i1, i2, i3, bc, br2, bg, flut;
        if (a0 < 1e-4f && a1 < 1e-4f)
            continue;
        a0 *= a0 * lv * rope_mk * (0.62f + 0.38f * fm_clampf((r->midi - 45.0f) * (1.0f / 24.0f), 0.0f, 1.0f));
                                                 /* (by its square: a soft swell; the low ropes softer) */
        a1 *= a1 * lv * rope_mk * (0.62f + 0.38f * fm_clampf((r->midi - 45.0f) * (1.0f / 24.0f), 0.0f, 1.0f));
        da = (a1 - a0) * inv;
        w1 = (1.0f - 0.55f * r->harm) * (1.0f - 0.45f * breath);
        w2 = (0.16f + 0.5f * r->harm) * (0.4f + 0.8f * tone) * (1.0f - 0.3f * breath);
        w3 = (0.04f + 0.3f * r->harm * tone) * (0.3f + 0.9f * tone);
        wb = breath * (0.7f + 0.5f * r->harm);
        if (r->midi > 76.0f) {                   /* the high ropes: thinner */
            w2 *= 0.6f;
            w3 *= 0.4f;
        }
        pan = RIG[i].pos * width;
        gl = 1.0f - pan;
        gr = 1.0f + pan;
        flut = 1.0f + 0.0005f * r->turb;
        p0 = r->ph[0], p1 = r->ph[1], p2 = r->ph[2], p3 = r->ph[3];
        i0 = r->inc[0] * flut, i1 = r->inc[1] * flut, i2 = r->inc[2] * flut, i3 = r->inc[3] * flut;
        y1 = r->by1, y2 = r->by2, bc = r->bc, br2 = r->br2, bg = r->bg;
        for (j = 0; j < n; j++) {
            float s, y = bc * y1 - br2 * y2 + bg * noise();
            y2 = y1;
            y1 = y;
            s = w1 * (sn(p0) + 0.8f * sn(p1)) * 0.56f + w2 * sn(p2) + w3 * sn(p3) + wb * y;
            p0 = wrap1(p0 + i0);
            p1 = wrap1(p1 + i1);
            p2 = wrap1(p2 + i2);
            p3 = wrap1(p3 + i3);
            s *= a0;
            a0 += da;
            bufl[j] += s * gl;
            bufr[j] += s * gr;
        }
        r->ph[0] = p0, r->ph[1] = p1, r->ph[2] = p2, r->ph[3] = p3;
        r->by1 = fm_flush(y1), r->by2 = fm_flush(y2);
    }
    {                                            /* Tone: the ropes' brightness */
        float k = onepole(1500.0f + 7000.0f * tone * tone);
        for (j = 0; j < n; j++) {
            tone_l += (bufl[j] - tone_l) * k;
            tone_r += (bufr[j] - tone_r) * k;
            bufl[j] = tone_l;
            bufr[j] = tone_r;
        }
    }
}

/* the hull's hum, the bell */
static void render_hull(uint32_t n, float dt)
{
    uint32_t j;
    int h, m;
    float tgt = pf(P_HULL) * 0.07f * (0.15f + 0.85f * fm_minf(wd.speed * 1.4f, 1.0f)) * play_g;
    float sw = 0.8f + 0.2f * sn(wrap1(wd.sw_ph + 0.75f));
    if (!trans)
        hull_tune();
    for (h = 0; h < 2; h++) {
        float a0 = hull[h].amp, a1, da, p0 = hull[h].ph[0], p1 = hull[h].ph[1], p2 = hull[h].ph[2], f5 = hull[h].f5;
        hull[h].tgt = h == hull_cur ? tgt * sw : 0.0f;
        a1 = a0 + (hull[h].tgt - a0) * fm_minf(dt / (h == hull_cur && hull[h].tgt > a0 ? 3.0f : 1.0f), 1.0f);
        hull[h].amp = a1;
        if (a0 < 1e-5f && a1 < 1e-5f)
            continue;
        da = (a1 - a0) / (float)n;
        for (j = 0; j < n; j++) {
            float s = (sn(p0) + 0.35f * sn(p1) + f5 * sn(p2)) * a0;
            p0 = wrap1(p0 + hull[h].inc[0]);
            p1 = wrap1(p1 + hull[h].inc[1]);
            p2 = wrap1(p2 + hull[h].inc[2]);
            a0 += da;
            bufl[j] += s;
            bufr[j] += s;
        }
        hull[h].ph[0] = p0, hull[h].ph[1] = p1, hull[h].ph[2] = p2;
    }
    if (bell_n) {                                /* the bell's strokes due */
        bell_clock += dt;
        while (bell_i < bell_n && bell_clock >= bell_t[bell_i]) {
            bell_strike();
            bell_i++;
        }
        if (bell_i >= bell_n && bell_clock > bell_t[bell_n - 1] + 8.0f)
            bell_n = 0;
    }
    if (bell_n || bell.y1[1] != 0.0f) {
        float g = pf(P_BELL) * 1.4f;
        for (m = 0; m < NBP; m++) {
            float y1 = bell.y1[m], y2 = bell.y2[m], c = bell.c[m], r2 = bell.r2[m];
            for (j = 0; j < n; j++) {
                float y = c * y1 - r2 * y2;
                y2 = y1;
                y1 = y;
                bufl[j] += y * g * 0.9f;
                bufr[j] += y * g;
            }
            bell.y1[m] = fm_flush(y1);
            bell.y2[m] = fm_flush(y2);
        }
        if (bell.y1[1] < 1e-7f && bell.y1[1] > -1e-7f && !bell_n)
            for (m = 0; m < NBP; m++)
                bell.y1[m] = bell.y2[m] = 0.0f;
    }
}

/* the Ambience: wind, the sea's wash, the sails, the hull's creaks (the music's bed, its own level) */
static struct {
    float wl1, wl2, wr1, wr2, sea1, sea2, hiss1, hiss2, sail1, whoosh, flap, flap_t, flap_lp;
    float cr_amp, cr_ph, cr_inc, cr_dinc, cr_y1, cr_y2, cr_c, cr_r2, cr_t;
    float prev_gust;
    float lvl;
} amb;
static void render_amb(uint32_t n, float dt)
{
    uint32_t j;
    float g, w = wd.wnow, fc, kw, ks, kh, sw, wash, wa, sa, ha;
    amb.lvl += (pf(P_AMB) * play_g - amb.lvl) * fm_minf(dt / 0.3f, 1.0f);
    g = amb.lvl;
    if (g < 1e-4f)
        return;
    fc = 180.0f + 700.0f * w;
    kw = onepole(fc);
    ks = onepole(380.0f);
    kh = onepole(2600.0f);
    sw = sn(wrap1(wd.sw_ph));                    /* the wave's rise and its break */
    wash = 0.35f + 0.65f * fm_maxf(0.0f, sw) * wd.sw_amp;
    wa = g * 0.2f * fm_powf(w, 1.4f);
    sa = g * 0.17f * wash * (0.5f + 0.6f * wd.sw_amp);
    ha = g * 0.03f * wd.speed * (0.5f + 0.5f * fm_maxf(0.0f, -sw));
    /* the sails: a whoosh as a gust fills them, a flap in a lull or turning through the wind */
    if (en_gust > amb.prev_gust + 0.02f)
        amb.whoosh = fm_maxf(amb.whoosh, en_gust * 0.12f);
    amb.prev_gust = en_gust;
    {
        float lull = fm_maxf(en_luff, fm_clampf((0.2f - w) * 6.0f, 0.0f, 1.0f) * (w > 0.05f ? 1.0f : 0.25f));
        amb.flap_t -= dt;
        if (lull > 0.05f && amb.flap_t <= 0.0f) {
            amb.flap = 0.1f * lull;
            amb.flap_t = 0.18f + 0.5f * (1.0f - lull) + 0.4f * (float)(xs(&nrng) >> 24) * (1.0f / 256.0f);
        }
    }
    /* a creak at the swell's turn, now and then */
    amb.cr_t -= dt;
    if (amb.cr_t <= 0.0f) {
        float f = 260.0f + 300.0f * (float)(xs(&nrng) >> 24) * (1.0f / 256.0f), rr = fm_expf(-FM_PI * 30.0f / EN_SR);
        amb.cr_amp = 0.6f * (0.4f + 0.6f * wd.sw_amp);
        amb.cr_inc = (35.0f + 30.0f * (float)(xs(&nrng) >> 24) * (1.0f / 256.0f)) / EN_SR;
        amb.cr_dinc = -amb.cr_inc * 0.5f / (0.6f * EN_SR);
        amb.cr_c = 2.0f * rr * fm_cosf(FM_TWO_PI * f / EN_SR);
        amb.cr_r2 = rr * rr;
        amb.cr_t = 3.0f + 9.0f * (float)(xs(&nrng) >> 24) * (1.0f / 256.0f) * (1.2f - wd.sw_amp);
    }
    for (j = 0; j < n; j++) {
        float nl = noise(), nr = noise(), s, cr = 0.0f, fl;
        amb.wl1 += (nl - amb.wl1) * kw;
        amb.wl2 += (amb.wl1 - amb.wl2) * kw;
        amb.wr1 += (nr - amb.wr1) * kw;
        amb.wr2 += (amb.wr1 - amb.wr2) * kw;
        amb.sea1 += ((nl + nr) * 0.5f - amb.sea1) * ks;
        amb.sea2 += (amb.sea1 - amb.sea2) * ks;
        amb.hiss1 += (nr - amb.hiss1) * kh;
        amb.hiss2 += (amb.hiss1 - amb.hiss2) * 0.02f;
        amb.sail1 += (nl - amb.sail1) * 0.012f;
        s = amb.sail1 * (amb.whoosh * 6.0f) * g;
        fl = amb.flap > 1e-4f ? (amb.sail1 * 8.0f) * amb.flap * g : 0.0f;
        amb.flap *= 0.9993f;
        amb.whoosh *= 0.99996f;
        if (amb.cr_amp > 1e-4f) {
            float x = 0.0f, y;
            amb.cr_ph += amb.cr_inc;
            amb.cr_inc += amb.cr_dinc;
            if (amb.cr_ph >= 1.0f) {
                amb.cr_ph -= 1.0f;
                x = amb.cr_amp;
            }
            y = amb.cr_c * amb.cr_y1 - amb.cr_r2 * amb.cr_y2 + x * 0.012f;
            amb.cr_y2 = amb.cr_y1;
            amb.cr_y1 = y;
            cr = y;
            amb.cr_amp *= 0.99992f;
            if (amb.cr_inc < 8.0f / EN_SR)
                amb.cr_amp = 0.0f;
        }
        {
            float common = amb.sea2 * sa * 4.0f + (amb.hiss1 - amb.hiss2) * ha + s + fl;
            bufl[j] += amb.wl2 * wa * 2.0f + common + cr * g * 0.9f;
            bufr[j] += amb.wr2 * wa * 2.0f + common * 0.95f + cr * g * 0.75f;
        }
    }
}

static void render_room(uint32_t n)
{
    uint32_t j;
    float room = pf(P_ROOM), send = 0.1f + 0.5f * room, g = 0.5f + 0.42f * room, damp = 0.35f;
    float dry = 1.0f - 0.4f * room * room, wet = 0.3f + 0.6f * room;
    for (j = 0; j < n; j++) {
        float in = sendb[j] * send, x[4], sum, yl, yr;
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
    float hp_a = onepole(28.0f), lv_rel = fm_expf(-1.0f / (0.6f * EN_SR)), lv_att = 1.0f - fm_expf(-1.0f / (0.004f * EN_SR));
    for (j = 0; j < n; j++) {
        float l = bufl[j], r = bufr[j], x, vol;
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

static void clock_ms(uint32_t k)
{
    ms_rem += k * 1000u;
    en_now_ms += ms_rem / 44100u;
    ms_rem %= 44100u;
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    drain();
    while (n) {
        uint32_t k = n > BLK ? BLK : n, j;
        float dt = (float)k / EN_SR;
        play_g += ((en_playing ? 1.0f : 0.0f) - play_g) * fm_minf(dt / 0.8f, 1.0f);
        world_step(dt);
        ropes_step(dt);
        clock_ms(k);
#ifdef EN_HOST
        if (en_dbg_fast) {
            for (j = 0; j < 2 * k; j++)
                out[j] = 0;
            out += 2 * k;
            n -= k;
            continue;
        }
#endif
        for (j = 0; j < k; j++)
            bufl[j] = bufr[j] = 0.0f;
        render_ropes(k);
#ifdef EN_HOST
        if (en_dbg_mute & 1)
            for (j = 0; j < k; j++)
                bufl[j] = bufr[j] = 0.0f;
#endif
#ifdef EN_HOST
        if (!(en_dbg_mute & 4))
#endif
            render_hull(k, dt);
        for (j = 0; j < k; j++)                  /* the music into the air; the bed mostly dry */
            sendb[j] = (bufl[j] + bufr[j]) * 0.5f;
#ifdef EN_HOST
        if (!(en_dbg_mute & 2))
#endif
            render_amb(k, dt);
        for (j = 0; j < k; j++)
            sendb[j] += (bufl[j] + bufr[j]) * 0.1f;
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

#ifdef EN_HOST
int en_dbg_sounding(float *midi, float *amp)
{
    int i, n = 0;
    for (i = 0; i < EN_NR; i++)
        if (rp[i].amp > 0.02f) {
            midi[n] = rp[i].midi;
            amp[n++] = rp[i].amp;
        }
    return n;
}
int en_dbg_chord_cents(int which, int *cents)
{
    int i, n = which ? n_prev : n_now;
    for (i = 0; i < n; i++)
        cents[i] = which ? chord_prev[i] : chord_now[i];
    return n;
}
#endif
#if defined EN_HOST || defined PLAT_HOST
void en_dbg_squall(void)
{
    wd.sq = 1;
    wd.sq_t = 0.0f;
    wd.sq_len = 120.0f;
    wd.calm_t = 0.0f;
}
#endif
