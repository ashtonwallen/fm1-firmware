/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CAVE: ashtonwallen, 2026 */
/* The instrument (cave.h). One compilation unit, -O2. The command queue and the render's shape are AMB-1's (by way
 * of FELT PIANO). */
#include "cave.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Drips", 0, 100, 50},  {"Ring", 0, 100, 60},  {"River", 0, 100, 45}, {"Ambience", 0, 100, 40},
    {"Cavern", 0, 100, 70}, {"Echo", 0, 100, 30},  {"Tone", 0, 100, 60},  {"Width", 0, 100, 70},
    {"Growth", 0, 100, 50}, {"Events", 0, 100, 50}, {"Pebbles", 0, 100, 70},
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

/* Growth: x0.25 .. x4 the natural pace (a stalagmite's next note in about an hour at x1) */
static float growth_rate(int v) { return fm_exp2f((float)(v - 50) * (1.0f / 25.0f)); }
void en_param_text(int i, int v, char *buf)
{
    if (i == P_GROWTH) {                         /* the pace, in percent of nature's */
        uint32_t x = (uint32_t)(growth_rate(v) * 100.0f + 0.5f);
        put_s(put_u(buf, x > 100u ? (x + 5u) / 10u * 10u : x), "%");
    } else if (i == P_EVENTS && v == 0) {
        put_s(buf, "None");
    } else if ((i == P_AMB || i == P_ECHO || i == P_RIVER || i == P_PEBBLE) && v == 0) {
        put_s(buf, "Off");
    } else {
        put_u(buf, (uint32_t)(v < 0 ? 0 : v));
    }
}

/* -------------------------------------------------------------- caves --- */
/* Tuning sets: cents above the tonic, an octave's worth. None has a semitone in it: any two notes ring together
 * without rubbing, whatever the drips do. The river's path: its roots (cents above the tonic), chosen so every note
 * of the set is a consonance or a gentle colour over each (tests/host/cave_test.c checks: no minor ninth over a
 * root, just sets pure against their drones) */
enum { TB_GLASS, TB_STONE, TB_BOSS, TB_CRYSTAL, TB_N };
typedef struct {
    const char *name;
    int8_t v[P_NSTYLE];
    uint8_t tonic, n;                            /* degree 0 (MIDI); notes an octave */
    int16_t cents[6];
    int16_t path[6];
    uint8_t npath;
    int16_t dfifth;                              /* the river's second note above its root */
    uint8_t timbre;
    uint8_t nstal, npool, slo, shi, plo, phi;    /* the layout: counts, degree ranges */
    uint8_t pmin, pmax;                          /* drip periods, tenths of a second */
    uint8_t harm;                                /* tens of seconds between the river's moves */
    uint8_t bats;                                /* bats' rate, 16 = 1 */
    uint8_t echo;                                /* the echo's time, 10 ms */
    const char *scale;
} cave_t;

#define PENT_MAJ {0, 200, 400, 700, 900}
#define PENT_MIN {0, 300, 500, 700, 1000}
static const cave_t CAVES[EN_NSTYLES] = {
    /*                    Dri Rin Riv Amb  Cav Ech Ton Wid  Gro Evt */
    {"Soda Straws",      {50, 60, 45, 40, 70, 30, 60, 70, 50, 50}, 50, 5, PENT_MAJ,
     {0, -300, 500, 0, 700}, 5, 700, TB_GLASS, 5, 3, 8, 15, 3, 9, 22, 60, 36, 16, 42, "major pentatonic"},
    {"Rimstone Pools",   {45, 55, 40, 45, 60, 20, 45, 60, 50, 40}, 53, 5, {0, 204, 386, 702, 884},
     {0, 702}, 2, 702, TB_STONE, 2, 6, 8, 13, 2, 10, 30, 70, 48, 16, 36, "just pentatonic"},
    {"Bacon Rind",       {55, 75, 35, 30, 75, 35, 75, 75, 50, 50}, 48, 5, {0, 200, 400, 600, 900},
     {0, 200, 0, -300}, 4, 700, TB_GLASS, 6, 2, 9, 17, 4, 9, 26, 60, 30, 16, 48, "Lydian pentatonic"},
    {"Fat Man's Misery", {40, 30, 50, 40, 30, 55, 35, 40, 50, 45}, 45, 5, PENT_MIN,
     {0, -400, 500, 0, 300}, 5, 700, TB_STONE, 4, 2, 4, 11, 2, 7, 25, 65, 36, 16, 17, "minor pentatonic"},
    {"Sump Two",         {40, 50, 75, 50, 55, 25, 20, 55, 50, 50}, 50, 5, PENT_MIN,
     {0, -400, 500, 0}, 4, 700, TB_STONE, 2, 5, 6, 11, 1, 8, 30, 80, 42, 16, 52, "minor pentatonic"},
    {"Pearl Nest",       {55, 45, 25, 35, 65, 40, 70, 80, 50, 50}, 52, 5, {0, 240, 480, 720, 960},
     {0, 720, 0, 480}, 4, 720, TB_CRYSTAL, 3, 5, 10, 17, 8, 15, 22, 50, 30, 16, 38, "slendro"},
    {"Lamp Off",         {30, 90, 55, 30, 95, 30, 40, 85, 50, 30}, 48, 5, {0, 204, 386, 702, 969},
     {0, 702}, 2, 702, TB_BOSS, 4, 3, 5, 13, 2, 9, 45, 110, 60, 16, 60, "harmonic series"},
    {"Stal Boss",        {45, 70, 50, 35, 80, 35, 45, 65, 50, 50}, 46, 5, PENT_MAJ,
     {0, -300, 500, 0, 700}, 5, 700, TB_BOSS, 6, 1, 4, 12, 3, 7, 30, 75, 40, 16, 55, "major pentatonic"},
    {"Guano Choir",      {55, 50, 40, 45, 65, 40, 60, 75, 50, 70}, 47, 5, PENT_MIN,
     {0, -400, 300, 500}, 4, 700, TB_GLASS, 4, 3, 7, 15, 3, 10, 20, 55, 33, 64, 44, "minor pentatonic"},
    {"Karst Window",     {60, 45, 30, 50, 45, 30, 80, 70, 50, 50}, 57, 5, PENT_MAJ,
     {0, -300, 500, 700}, 4, 700, TB_CRYSTAL, 5, 3, 6, 14, 2, 9, 22, 50, 27, 16, 33, "major pentatonic"},
    {"Helictite",        {45, 70, 35, 35, 80, 45, 65, 80, 50, 50}, 48, 6, {0, 200, 400, 600, 800, 1000},
     {0, 200, 0, -200}, 4, 400, TB_GLASS, 6, 2, 8, 18, 4, 10, 26, 60, 36, 16, 46, "whole tone"},
    {"Ox Bow Lake",      {35, 80, 60, 45, 85, 50, 35, 70, 50, 50}, 43, 5, {0, 316, 498, 702, 996},
     {0, 498}, 2, 702, TB_STONE, 2, 6, 7, 12, 3, 11, 40, 95, 54, 16, 58, "just minor"},
};
static const cave_t *cv(int i) { return &CAVES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES]; }
const char *en_style_name(int i) { return cv(i)->name; }
const char *en_style_scale(int i) { return cv(i)->scale; }
int en_style_tonic(int i) { return cv(i)->tonic; }
int en_style_nset(int i) { return cv(i)->n; }
void en_style_recipe(int i, int16_t *par)
{
    int k;
    for (k = 0; k < P_NSTYLE; k++)
        par[k] = cv(i)->v[k];
}
int en_deg_cents(int style, int deg)
{
    const cave_t *c = cv(style);
    if (deg < 0)
        deg = 0;
    return c->tonic * 100 + 1200 * (deg / c->n) + c->cents[deg % c->n];
}
int en_cents_pc(int cents) { return ((cents + 50) / 100) % 12; }

uint32_t en_hash(uint32_t base, uint32_t no)
{
    uint32_t h = base ^ (no * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h ? h : 1u;
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_STYLE, C_WORLD, C_PLAY, C_HARM, C_FF, C_EVENT, C_NOTE, C_PANIC };
#define QN 64u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static const en_world_t *volatile world_in;
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
void en_world_put(const en_world_t *w)
{
    world_in = w;
    BARRIER();
    post(C_WORLD, 0, 0);
}
void en_play(int on) { post(C_PLAY, on, 0); }
void en_next_harm(void) { post(C_HARM, 0, 0); }
void en_ff(int minutes) { post(C_FF, 0, minutes); }
void en_event(int kind) { post(C_EVENT, kind, 0); }
void en_note(int note, int vel) { post(C_NOTE, note, vel); }
void en_panic(void) { post(C_PANIC, 0, 0); }

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_style_i, en_stone_x, en_bats_dir, en_harm_pc, en_drips_on, en_voices;
volatile float en_flood, en_wet, en_vu_l, en_vu_r;
volatile uint32_t en_bats_ms, en_stone_ms, en_run_ms, en_now_ms, en_last_strike_ms, en_notes_played, en_retunes;
volatile int16_t en_harm_cents;
volatile uint32_t en_drip_next[EN_NDRIP];
volatile en_logn_t EN_LOG[EN_NLOG];
volatile uint32_t en_log_w;
#ifdef EN_HOST
void (*en_dbg_note)(uint32_t ms, int cents, float vel, int role, int obj);
void (*en_dbg_harm)(uint32_t ms, int cents);
void (*en_dbg_event)(uint32_t ms, int kind, const char *what);
#define DBG_EVENT(k, s)                                                                                           \
    do {                                                                                                           \
        if (en_dbg_event)                                                                                          \
            en_dbg_event(en_now_ms, (k), (s));                                                                     \
    } while (0)
#else
#define DBG_EVENT(k, s) ((void)0)
#endif

static int16_t par[P_NPARAMS];
static int style_i;
static en_world_t W;                             /* the world playing (the render's) */
static en_world_t WP;                            /* its copy for the UI */
static volatile uint32_t wp_seq;
static uint32_t rng = 0x9E3779B9u, nrng = 0x2545F491u, nrng2 = 0x1B873593u;
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
static int irand(int n) { return n > 0 ? (int)((xs(&rng) >> 8) % (uint32_t)n) : 0; }
static int wrand(en_world_t *w, int n) { return n > 0 ? (int)((xs(&w->rng) >> 8) % (uint32_t)n) : 0; }
static inline float noise(uint32_t *s) { return (float)(int32_t)xs(s) * (1.0f / 2147483648.0f); }
static float pf(int p) { return (float)par[p] * 0.01f; }

#define SN 1024
static float SINT[SN + 1];
static inline __attribute__((always_inline)) float sn(float ph)    /* ph in turns, 0..1 */
{
    float x = ph * (float)SN;
    int32_t i = (int32_t)x;
    float f = x - (float)i;
    i &= SN - 1;
    return SINT[i] + (SINT[i + 1] - SINT[i]) * f;
}
static inline float wrap1(float p) { return p >= 1.0f ? p - 1.0f : p; }
static inline float ctof(float cents) { return 440.0f * fm_exp2f((cents - 6900.0f) * (1.0f / 1200.0f)); }
static inline float tau(float sec) { return fm_expf(-1.0f / (sec * EN_SR)); }
static inline float onepole(float fc) { return 1.0f - fm_expf(-FM_TWO_PI * fc / EN_SR); }

/* ------------------------------------------------------------ the voices --- */
/* a stone: up to four partials, each a two-pole resonator ringing down from a sine's start, a click of grit on top.
 * Water: a sine that rises into its pitch (a drop's bubble), a soft second harmonic, a plop of noise. */
#define NV 16
#define NM 4
#define BLK 64
typedef struct {
    float y1[NM], y2[NM], c[NM], r2[NM];
    float lvl, rb;                               /* the loudest partial's level, its fall a block (culling) */
    float ph, inc, inc_t, gk, amp, adec, att, h2;
    float nz, nzk, nzlp, nza;
    float gl, gr;
    uint8_t on, kind, nm;
} vo_t;
static vo_t vo[NV];
static const float TB_RATIO[TB_N][NM] = {
    {1.0f, 2.01f, 3.02f, 5.1f},                  /* glass: soda straws, draperies */
    {1.0f, 2.0f, 3.99f, 9.2f},                   /* stone: a tuned bar's overtones */
    {1.0f, 2.0f, 2.98f, 4.1f},                   /* the boss: a big stone, gong-like but in tune */
    {1.0f, 3.0f, 2.0f, 5.0f},                    /* crystal */
};
static const float TB_AMP[TB_N][NM] = {
    {1.0f, 0.30f, 0.14f, 0.06f}, {1.0f, 0.08f, 0.22f, 0.05f}, {1.0f, 0.45f, 0.25f, 0.10f}, {1.0f, 0.20f, 0.10f, 0.08f},
};
static const float TB_DEC[TB_N][NM] = {
    {1.0f, 0.55f, 0.35f, 0.2f}, {1.0f, 0.5f, 0.35f, 0.15f}, {1.0f, 0.8f, 0.5f, 0.3f}, {1.0f, 0.5f, 0.7f, 0.3f},
};

static vo_t *voice_get(void)
{
    int i;
    vo_t *v = 0;
    float lo = 1e9f;
    for (i = 0; i < NV; i++)
        if (!vo[i].on)
            return &vo[i];
    for (i = 0; i < NV; i++)
        if (vo[i].lvl < lo) {
            lo = vo[i].lvl;
            v = &vo[i];
        }
    return v;
}

static void log_strike(int x, int role, float vel, int kind, int obj, int cents, float t60)
{
    volatile en_logn_t *l = &EN_LOG[en_log_w % EN_NLOG];
    l->on = en_now_ms ? en_now_ms : 1u;
    l->tau = (uint16_t)fm_clampf(t60 * (1000.0f / 6.9078f), 30.0f, 60000.0f);
    l->obj = (uint8_t)obj;
    l->x = (uint8_t)x;
    l->role = (uint8_t)role;
    l->vel = (uint8_t)fm_clampf(vel * 127.0f, 1.0f, 127.0f);
    l->kind = (uint8_t)kind;
    l->pc = (int8_t)en_cents_pc(cents);
    BARRIER();
    en_log_w = en_log_w + 1u;
}

/* a note: cents above MIDI 0; kind OB_*; x where it is (0..239, the pan); glide: water's start below (<1) or above */
static void strike(int cents, float vel, int kind, int x, int role, int obj, float ringx, float glide)
{
    vo_t *v = voice_get();
    float f = ctof((float)cents), tone = pf(P_TONE), ring = pf(P_RING), t60, p, a;
    int m;
    vel = fm_clampf(vel, 0.02f, 1.0f);
    if (f > 9000.0f)
        return;
#ifdef EN_HOST
    if (en_dbg_note)
        en_dbg_note(en_now_ms, cents, vel, role, obj);
#endif
    a = 0.20f * fm_powf(vel, 1.3f);
    p = fm_clampf(((float)x - 120.0f) * (1.0f / 120.0f), -1.0f, 1.0f) * (0.2f + 0.75f * pf(P_WIDTH));
    v->gl = 0.75f * (1.0f - 0.8f * p);
    v->gr = 0.75f * (1.0f + 0.8f * p);
    v->kind = (uint8_t)kind;
    if (kind == OB_STAL) {
        int tb = cv(style_i)->timbre;
        float bright = (0.25f + 1.3f * tone) * (0.55f + 0.6f * vel);
        t60 = (1.2f + 7.5f * fm_powf(ring, 1.4f)) * fm_clampf(fm_exp2f((7000.0f - (float)cents) / 2400.0f), 0.45f, 1.7f) *
              ringx;
        v->nm = 0;
        for (m = 0; m < NM; m++) {
            float fn = f * TB_RATIO[tb][m], amp = a * TB_AMP[tb][m] * (m ? bright : 1.0f), t = t60 * TB_DEC[tb][m], r, w,
                  s;
            if (fn > 12000.0f)
                break;
            r = fm_expf(-6.9078f / (t * EN_SR));
            w = FM_TWO_PI * fn / EN_SR;
            s = fm_sinf(w * 0.5f);
            v->c[m] = 2.0f * r * (1.0f - 2.0f * s * s);
            v->r2[m] = r * r;
            v->y1[m] = amp * fm_sinf(w);
            v->y2[m] = 0.0f;
            v->nm = (uint8_t)(m + 1);
        }
        v->lvl = a;
        v->rb = fm_expf(-6.9078f * (float)BLK / (t60 * EN_SR));
        v->amp = 0.0f;
        v->nz = a * (0.10f + 0.25f * tone) * vel;    /* the grit of the drop's landing */
        v->nzk = tau(0.0025f);
        v->nza = onepole(2500.0f + 4000.0f * tone);
    } else {
        t60 = (0.35f + 2.2f * ring) * fm_clampf(fm_exp2f((6600.0f - (float)cents) / 2400.0f), 0.5f, 1.6f) * ringx;
        v->nm = 0;
        v->ph = 0.0f;
        v->inc_t = f / EN_SR;
        v->inc = v->inc_t * glide;
        v->gk = 1.0f - tau(glide > 1.0f ? 0.008f : 0.012f);
        v->amp = a * 1.15f;
        v->adec = fm_expf(-6.9078f / (t60 * EN_SR));
        v->att = 0.0f;
        v->h2 = 0.08f + 0.22f * tone;
        v->lvl = v->amp;
        v->rb = fm_expf(-6.9078f * (float)BLK / (t60 * EN_SR));
        v->nz = a * (0.06f + 0.12f * tone) * vel;    /* the plop */
        v->nzk = tau(0.006f);
        v->nza = onepole(900.0f + 1500.0f * tone);
    }
    v->nzlp = 0.0f;
    v->on = 1;
    en_notes_played++;
    en_last_strike_ms = en_now_ms;
    log_strike(x, role, vel, kind, obj, cents, t60);
}

/* ------------------------------------------------------------ the river --- */
/* two layers, crossfaded as the root moves: the root, the root a hair sharp (a slow beat), its fifth, its octave */
static struct {
    float ph[4], inc[4], g, gt;
} dl[2];
static int dl_cur;
static float cur_ph1, cur_ph2;

static int harm_abs(void)                        /* the river's root now: cents above MIDI 0, low in the bass */
{
    const cave_t *c = cv(style_i);
    int r = (c->tonic - 24) * 100 + c->path[W.harm % c->npath];
    while (r > 4500)
        r -= 1200;
    while (r < 3300)
        r += 1200;
    return r;
}
static void river_tune(int now)
{
    const cave_t *c = cv(style_i);
    int r = harm_abs(), n = dl_cur ^ 1;
    float f = ctof((float)r);
    en_harm_cents = (int16_t)c->path[W.harm % c->npath];
    en_harm_pc = (uint8_t)en_cents_pc(c->tonic * 100 + c->path[W.harm % c->npath]);
    dl[n].inc[0] = f / EN_SR;
    dl[n].inc[1] = f * 1.0016f / EN_SR;
    dl[n].inc[2] = ctof((float)(r + c->dfifth)) / EN_SR;
    dl[n].inc[3] = f * 2.0f / EN_SR;
    dl[n].gt = 1.0f;
    dl[dl_cur].gt = 0.0f;
    if (now) {                                   /* at once (a new cave, the start) */
        dl[n].g = 1.0f;
        dl[dl_cur].g = 0.0f;
    }
    dl_cur = n;
#ifdef EN_HOST
    if (en_dbg_harm)
        en_dbg_harm(en_now_ms, c->tonic * 100 + c->path[W.harm % c->npath]);
#endif
}

/* is this pitch a chord tone of the river now? (its root, third, fifth) */
static int chord_tone(int cents)
{
    const cave_t *c = cv(style_i);
    int iv = ((cents - c->tonic * 100 - c->path[W.harm % c->npath]) % 1200 + 2400) % 1200;
    return iv < 30 || iv > 1170 || (iv > 255 && iv < 425) || (iv > c->dfifth - 30 && iv < c->dfifth + 30);
}

/* ------------------------------------------------------------- the world --- */
static int obj_cents(const en_obj_t *o) { return en_deg_cents(style_i, o->deg); }

/* the drip points onto the objects: the first (the one that drips even when the cave is driest) on a steady low note
 * (the tonic's or the fifth's pitch class), slow; then the rest spread over the registers, high and low in turn, so
 * the texture always has a top and a bottom. rot: which steady note (floods move it) */
static void assign_drips(en_world_t *w, int style, int rot)
{
    const cave_t *c = cv(style);
    static const uint8_t QF[EN_NOBJ] = {80, 15, 55, 100, 35, 90, 25, 65, 0};
    int s[EN_NOBJ] = {0}, n = w->nobj, i, j, pick[EN_NOBJ], np = 0, cand[EN_NOBJ], nc = 0;
    uint32_t mark = 0;
    for (i = 0; i < n; i++)
        s[i] = i;
    for (i = 1; i < n; i++)                      /* by pitch, low to high */
        for (j = i; j > 0 && en_deg_cents(style, w->obj[s[j]].deg) < en_deg_cents(style, w->obj[s[j - 1]].deg); j--) {
            int t = s[j];
            s[j] = s[j - 1];
            s[j - 1] = t;
        }
    for (i = 0; i < (n + 1) / 2; i++) {
        int iv = ((en_deg_cents(style, w->obj[s[i]].deg) - c->tonic * 100) % 1200 + 1200) % 1200;
        if (iv < 30 || iv > 1170 || (iv > 672 && iv < 732))
            cand[nc++] = s[i];
    }
    pick[np++] = nc ? cand[rot % nc] : s[n / 4];
    mark |= 1u << pick[0];
    for (i = 0; np < n && i < 4 * EN_NOBJ; i++) {
        int k = (QF[i % EN_NOBJ] * (n - 1) + 50) / 100, d;
        for (d = 0; d < n; d++) {                /* the nearest not yet taken */
            if (k + d < n && !(mark >> s[k + d] & 1u)) {
                k = k + d;
                break;
            }
            if (k - d >= 0 && !(mark >> s[k - d] & 1u)) {
                k = k - d;
                break;
            }
        }
        if (mark >> s[k] & 1u)
            continue;
        pick[np++] = s[k];
        mark |= 1u << s[k];
    }
    w->ndrip = (uint8_t)(np < EN_NDRIP ? np : EN_NDRIP);
    for (i = 0; i < w->ndrip; i++) {
        en_drip_t *d = &w->drip[i];
        int x = w->obj[pick[i]].x + wrand(w, 5) - 2;
        d->target = (uint8_t)pick[i];
        d->x = (uint8_t)(x < 4 ? 4 : x > 236 ? 236 : x);
        d->thresh = (uint8_t)(i * 220 / w->ndrip);
        d->period = (uint16_t)((i ? c->pmin + wrand(w, c->pmax - c->pmin + 1) : c->pmax - wrand(w, 8)) * 100);
    }
}

void en_world_fresh(int style, en_world_t *w)
{
    const cave_t *c = cv(style);
    int i, n, order[EN_NOBJ], used[32];
    for (i = 0; i < (int)sizeof *w; i++)
        ((uint8_t *)w)[i] = 0;
    w->magic = EN_WORLD_MAGIC;
    w->style = (uint8_t)style;
    w->rng = en_hash(0x43415645u, (uint32_t)style);
    w->wet = 450;
    w->wet_to = (uint16_t)(300 + wrand(w, 500));
    w->wet_left = (uint16_t)(20 + wrand(w, 40));
    w->harm = 0;
    w->harm_left = (uint16_t)(c->harm * 10);
    w->flood_left = (uint16_t)((150 + wrand(w, 150)) * 16);
    w->stone_left = (uint16_t)((45 + wrand(w, 60)) * 16);
    w->bats_left = (uint16_t)((12 + wrand(w, 25)) * 16);
    n = c->nstal + c->npool;
    if (n > EN_NOBJ)
        n = EN_NOBJ;
    w->nobj = (uint8_t)n;
    for (i = 0; i < n; i++)                      /* stalagmites and pools, shuffled across the floor */
        order[i] = i < c->nstal ? OB_STAL : OB_POOL;
    for (i = n - 1; i > 0; i--) {
        int j = wrand(w, i + 1), t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    for (i = 0; i < 32; i++)
        used[i] = 0;
    for (i = 0; i < n; i++) {
        en_obj_t *o = &w->obj[i];
        o->kind = (uint8_t)order[i];
        o->x = (uint8_t)(16 + i * 208 / (n > 1 ? n - 1 : 1) + wrand(w, 7) - 3);
        if (o->kind == OB_STAL) {
            o->lo = c->slo;
            o->hi = c->shi;
            int d, k;
            for (k = 0; k < 8; k++) {            /* each on a note of its own where it can */
                d = c->slo + 2 + wrand(w, c->shi - c->slo - 1);
                if (!used[d])
                    break;
            }
            used[d] = 1;
            o->deg = (uint8_t)d;
            o->need = (uint16_t)(4000 + wrand(w, 4000));
            o->grow = (uint16_t)wrand(w, o->need);
        } else {
            int d, k;
            o->lo = c->plo;
            o->hi = c->phi;
            for (k = 0; k < 8; k++) {            /* pools on notes of their own where they can */
                d = c->plo + wrand(w, c->phi - c->plo + 1);
                if (!used[d])
                    break;
            }
            used[d] = 1;
            o->deg = (uint8_t)d;
        }
    }
    assign_drips(w, style, 0);
}
int en_world_valid(const en_world_t *w, int style)
{
    int i;
    if (w->magic != EN_WORLD_MAGIC || w->style != style || w->nobj == 0 || w->nobj > EN_NOBJ || w->ndrip == 0 ||
        w->ndrip > EN_NDRIP || w->wet > 1000 || w->wet_to > 1000)
        return 0;
    for (i = 0; i < w->nobj; i++)
        if (w->obj[i].kind > OB_POOL || w->obj[i].deg > 30 || (w->obj[i].kind == OB_STAL && w->obj[i].need == 0))
            return 0;
    for (i = 0; i < w->ndrip; i++)
        if (w->drip[i].target >= w->nobj || w->drip[i].period < 500)
            return 0;
    return 1;
}

/* notes to come (runs, splashes, bats, shards): a short queue, in samples */
#define NPEND 32
static struct {
    int32_t wait;
    int16_t cents;
    uint8_t kind, role, x, obj, used;
    float vel, ringx, glide;
} pend[NPEND];
static void later(float sec, int cents, float vel, int kind, int role, int x, int obj, float ringx, float glide)
{
    int i;
    for (i = 0; i < NPEND; i++)
        if (!pend[i].used) {
            pend[i].used = 1;
            pend[i].wait = (int32_t)(sec * EN_SR);
            pend[i].cents = (int16_t)cents;
            pend[i].vel = vel;
            pend[i].kind = (uint8_t)kind;
            pend[i].role = (uint8_t)role;
            pend[i].x = (uint8_t)(x < 0 ? 0 : x > 239 ? 239 : x);
            pend[i].obj = (uint8_t)obj;
            pend[i].ringx = ringx;
            pend[i].glide = glide;
            return;
        }
}

static int ff_on;                                /* the world runs silently (en_ff) */
static float wetf;                               /* the wetness, finely */
static int32_t drip_left[EN_NDRIP];              /* samples to each drip */
static uint8_t drip_was;
static int32_t sec_left;                         /* samples to the world's next second */
static int sec_n;                                /* seconds into the minute */
static int run_left;                             /* seconds to the next trickle */
static int flood_t = -1;                         /* seconds into a flood (-1: none) */

static float wet_eff(void) { return fm_clampf(wetf * (0.35f + 1.3f * pf(P_DRIPS)), 0.0f, 1.0f); }
static float period_now(const en_drip_t *d) { return (float)d->period * (1.45f - 0.7f * wet_eff()); }

static int cap_deg(int d)                       /* nothing above C7: glints, not whistles */
{
    while (d > 0 && en_deg_cents(style_i, d) > 9600)
        d--;
    return d;
}
/* the degree in lo..hi closest under or at top that is a chord tone (else top) */
static int chord_deg_under(int top, int lo)
{
    int d;
    for (d = top; d >= lo; d--)
        if (chord_tone(en_deg_cents(style_i, d)))
            return d;
    return top;
}

/* a trickle down the flowstone: a short falling phrase ending on a chord tone, slowing as the water runs out */
static void trickle(void)
{
    const cave_t *c = cv(style_i);
    int shape = irand(3), e, d[6], n = 0, i, x0 = 20 + irand(60);
    float t = 0.0f, gap = 0.14f + 0.05f * frand(), ratio = 1.13f + 0.1f * frand(), v0 = 0.42f + 0.12f * frand();
    e = chord_deg_under(cap_deg(c->shi - 3 - irand(3)), c->slo);
    if (shape == 0) {                            /* steps down */
        int len = 3 + irand(3);
        for (i = len - 1; i >= 0; i--)
            d[n++] = e + i;
    } else if (shape == 1) {                     /* down the chord */
        int k = cap_deg(e + 5);
        while (k > e && n < 3) {                 /* (from the top: descending) */
            if (chord_tone(en_deg_cents(style_i, k)))
                d[n++] = k;
            k--;
        }
        d[n++] = e;
    } else {                                     /* a turn, then down */
        d[n++] = e + 2;
        d[n++] = e + 3;
        d[n++] = e + 2;
        d[n++] = e + 1;
        d[n++] = e;
    }
    for (i = 0; i < n; i++) {
        later(t, en_deg_cents(style_i, d[i]), v0 * (1.0f - 0.08f * (float)i) * (i == n - 1 ? 1.1f : 1.0f), OB_STAL,
              R_RUN, x0 + i * 28, 255, i == n - 1 ? 1.2f : 0.7f, 1.0f);
        t += gap;
        gap *= ratio;
    }
    en_run_ms = en_now_ms ? en_now_ms : 1u;
    DBG_EVENT(EV_RUN, "trickle");
}

static void bats(void)
{
    const cave_t *c = cv(style_i);
    int n = 8 + irand(7), i, top = cap_deg(c->shi + 2), dir = irand(2);
    float t = 0.0f;
    for (i = 0; i < n; i++) {
        int dg = top - irand(6), k;
        for (k = 0; k < 3 && !chord_tone(en_deg_cents(style_i, dg)); k++)   /* mostly the chord's notes */
            dg--;
        later(t, en_deg_cents(style_i, dg), 0.12f + 0.14f * frand(), OB_POOL, R_BATS,
              dir ? 230 - i * 220 / n : 10 + i * 220 / n, 255, 0.35f, 1.3f);
        t += 0.09f + 0.17f * frand();
    }
    en_bats_dir = (uint8_t)dir;
    en_bats_ms = en_now_ms ? en_now_ms : 1u;
    DBG_EVENT(EV_BATS, "bats");
}

/* a falling stone: it lands with a low tuned thud, shards bounce down the chord; the tallest stalagmite loses its
 * tip (its note jumps back up the set) */
static void stone(void)
{
    const cave_t *c = cv(style_i);
    int i, best = -1, bh = -1, top, k = 0;
    static const float GAP[5] = {0.5f, 0.33f, 0.24f, 0.175f, 0.125f};
    float t = GAP[0];
    for (i = 0; i < W.nobj; i++)
        if (W.obj[i].kind == OB_STAL && W.obj[i].hi - W.obj[i].deg > bh) {
            bh = W.obj[i].hi - W.obj[i].deg;
            best = i;
        }
    en_stone_x = best >= 0 ? W.obj[best].x : 120;
    later(0.5f, harm_abs() + 1200, 0.75f, OB_STAL, R_STONE, en_stone_x, best < 0 ? 255 : best, 0.35f, 1.0f);   /* (it falls half a second) */
    top = chord_deg_under(cap_deg(c->shi + 1), c->slo);
    for (i = 1; i < 5; i++) {
        t += GAP[i];
        while (k < 6 && !chord_tone(en_deg_cents(style_i, top - k)))
            k++;
        later(t, en_deg_cents(style_i, top - k), 0.5f - 0.08f * (float)i, OB_STAL, R_STONE, en_stone_x + i * 9, 255,
              0.8f, 1.0f);
        k++;
    }
    if (best >= 0) {
        en_obj_t *o = &W.obj[best];
        o->deg = (uint8_t)(o->deg + 3 > o->hi ? o->hi : o->deg + 3);
        o->grow = 0;
    }
    W.stones++;
    en_stone_ms = en_now_ms ? en_now_ms : 1u;
    DBG_EVENT(EV_STONE, "stone");
}

static void harm_next(void)
{
    const cave_t *c = cv(style_i);
    W.harm = (uint8_t)((W.harm + 1) % c->npath);
    W.harm_left = (uint16_t)(c->harm * 10 * (8 + wrand(&W, 5)) / 10);
    if (!ff_on)
        river_tune(0);
}

/* the flood's height: up over 90 s, held a minute, down over three */
static float flood_at(int t)
{
    if (t < 0)
        return 0.0f;
    if (t < 90)
        return (float)t / 90.0f;
    if (t < 150)
        return 1.0f;
    if (t < 330)
        return 1.0f - (float)(t - 150) / 180.0f;
    return 0.0f;
}
/* at the flood's height: the pools on new notes, the drip points moved, new periods */
static void rearrange(void)
{
    const cave_t *c = cv(style_i);
    int i, n = W.nobj, used[32];
    for (i = 0; i < 32; i++)
        used[i] = 0;
    for (i = 0; i < n; i++) {
        en_obj_t *o = &W.obj[i];
        if (o->kind == OB_POOL) {
            int d = o->deg, k;
            for (k = 0; k < 10; k++) {
                d = c->plo + wrand(&W, c->phi - c->plo + 1);
                if (!used[d] && d != o->deg)
                    break;
            }
            used[d] = 1;
            o->deg = (uint8_t)d;
        }
    }
    assign_drips(&W, style_i, W.floods + wrand(&W, 3));
    DBG_EVENT(EV_FLOOD, "rearranged");
}

static void world_minute(void)
{
    int i;
    float rate = growth_rate(par[P_GROWTH]), ev = par[P_EVENTS] ? fm_exp2f((float)(par[P_EVENTS] - 50) / 25.0f) : 0.0f;
    uint16_t step = (uint16_t)(ev * 16.0f + 0.5f), bstep = (uint16_t)(ev * (float)cv(style_i)->bats + 0.5f);
    W.minutes++;
    if (W.wet_left)
        W.wet_left--;
    if (!W.wet_left) {                           /* the weather above: the seep heads somewhere new */
        W.wet_to = (uint16_t)(120 + wrand(&W, 860));
        W.wet_left = (uint16_t)(20 + wrand(&W, 50));
    }
    W.wet = (uint16_t)(wetf * 1000.0f);
    for (i = 0; i < W.nobj; i++) {               /* the stalagmites grow; a step's growth: the next note down */
        en_obj_t *o = &W.obj[i];
        if (o->kind != OB_STAL)
            continue;
        o->grow = (uint16_t)fm_minf((float)o->grow + 100.0f * rate * (0.6f + 0.8f * wetf), 65000.0f);
        if (o->grow >= o->need) {
            if (o->deg > o->lo) {
                o->deg--;
                o->grow = 0;
                o->need = (uint16_t)(4000 + wrand(&W, 4000));
                en_retunes++;
                DBG_EVENT(EV_NONE, "retune");
            } else {
                o->grow = o->need;               /* full grown: it waits for a stone */
            }
        }
    }
    if (step && flood_t < 0) {
        if (W.flood_left > step)
            W.flood_left = (uint16_t)(W.flood_left - step);
        else {
            W.flood_left = (uint16_t)((150 + wrand(&W, 150)) * 16);
            flood_t = 0;
            W.floods++;
            DBG_EVENT(EV_FLOOD, "flood");
        }
    }
    if (step) {
        if (W.stone_left > step)
            W.stone_left = (uint16_t)(W.stone_left - step);
        else {
            W.stone_left = (uint16_t)((45 + wrand(&W, 60)) * 16);
            if (!ff_on)
                stone();
            else {
                for (i = 0; i < W.nobj; i++)     /* (silently: the tip still breaks) */
                    if (W.obj[i].kind == OB_STAL && W.obj[i].deg == W.obj[i].lo) {
                        W.obj[i].deg = (uint8_t)(W.obj[i].deg + 3 > W.obj[i].hi ? W.obj[i].hi : W.obj[i].deg + 3);
                        break;
                    }
                W.stones++;
            }
        }
    }
    if (bstep) {
        if (W.bats_left > bstep)
            W.bats_left = (uint16_t)(W.bats_left - bstep);
        else {
            W.bats_left = (uint16_t)((15 + wrand(&W, 30)) * 16);
            if (!ff_on)
                bats();
        }
    }
}

static void world_second(void)
{
    float target = (float)W.wet_to * 0.001f;
    wetf += (target - wetf) * (1.0f / 600.0f);   /* the seep follows the weather slowly */
    if (flood_t >= 0) {
        float fl = flood_at(flood_t);
        if (flood_t == 120) {
            rearrange();
            harm_next();
        }
        if (++flood_t > 330)
            flood_t = -1;
        en_flood = fl;
        if (wetf < fl)
            wetf = fl;
    } else {
        en_flood = 0.0f;
    }
    en_wet = wetf;
    if (W.harm_left)
        W.harm_left--;
    if (!W.harm_left)
        harm_next();
    if (!ff_on && --run_left <= 0) {
        trickle();
        run_left = (int)((70.0f + 140.0f * frand()) / (0.5f + wet_eff()));
    }
    if (++sec_n >= 60) {
        sec_n = 0;
        world_minute();
    }
}

static void world_publish(void)
{
    uint32_t s = wp_seq;
    wp_seq = s + 1u;
    BARRIER();
    WP = W;
    BARRIER();
    wp_seq = s + 2u;
}
int en_world_get(en_world_t *w)
{
    int tries;
    for (tries = 0; tries < 50; tries++) {
        uint32_t s = wp_seq;
        BARRIER();
        if (s & 1u)
            continue;
        *w = *(const en_world_t *)&WP;
        BARRIER();
        if (wp_seq == s)
            return 0;
    }
    return -1;
}

static void world_take(void)                     /* after a new world: its derived state */
{
    int i;
    wetf = (float)W.wet * 0.001f;
    en_wet = wetf;
    flood_t = -1;
    en_flood = 0.0f;
    for (i = 0; i < EN_NDRIP; i++)
        drip_left[i] = (int32_t)((0.4f + 2.0f * frand()) * EN_SR);
    drip_was = 0;
    run_left = 40 + irand(60);
    river_tune(1);
    world_publish();
}

/* the drips: each point on its own period while the cave is wet enough for it */
static void drips(int32_t k)
{
    int i;
    float we = wet_eff() * 1000.0f;
    uint8_t on = 0;
    for (i = 0; i < W.ndrip; i++) {
        en_drip_t *d = &W.drip[i];
        if (we < (float)d->thresh * 4.0f)
            continue;
        on |= (uint8_t)(1u << i);
        if (!(drip_was >> i & 1u))               /* it starts: somewhere in its period */
            drip_left[i] = (int32_t)(period_now(d) * (0.3f + 0.7f * frand()) * 44.1f);
        drip_left[i] -= k;
        if (drip_left[i] <= 0) {
            const en_obj_t *o = &W.obj[d->target];
            int c = obj_cents(o);
            float v = (0.5f + 0.3f * frand()) * (0.85f + 0.3f * wetf) * (chord_tone(c) ? 1.0f : 0.62f);
            strike(c, v, o->kind, o->x, R_DRIP, d->target, 1.0f, 0.72f);
            if (frand() < 0.12f) {               /* a splash onto its neighbour */
                int j, nb = -1, bd = 999;
                for (j = 0; j < W.nobj; j++) {
                    int dx = W.obj[j].x - o->x;
                    dx = dx < 0 ? -dx : dx;
                    if (j != d->target && dx < bd) {
                        bd = dx;
                        nb = j;
                    }
                }
                if (nb >= 0)
                    later(0.07f + 0.05f * frand(), obj_cents(&W.obj[nb]), v * 0.35f, W.obj[nb].kind, R_SPLASH,
                          W.obj[nb].x, nb, 0.8f, 0.8f);
            }
            drip_left[i] += (int32_t)(period_now(d) * (0.97f + 0.06f * frand()) * 44.1f);
            if (drip_left[i] < 2000)
                drip_left[i] = 2000;
        }
        en_drip_next[i] = en_now_ms + (uint32_t)(drip_left[i] / 44);
    }
    drip_was = on;
    en_drips_on = on;
}

/* a key: a pebble into the pool nearest its note, the note snapped to the set */
static void pebble(int note)
{
    int d, best = 0, bd = 1 << 30, i, pool = 255, pd = 1 << 30, c;
    for (d = 0; d < 36; d++) {
        int e = en_deg_cents(style_i, d) - note * 100;
        e = e < 0 ? -e : e;
        if (e < bd) {
            bd = e;
            best = d;
        }
    }
    c = en_deg_cents(style_i, best);
    for (i = 0; i < W.nobj; i++)
        if (W.obj[i].kind == OB_POOL) {
            int e = obj_cents(&W.obj[i]) - c;
            e = e < 0 ? -e : e;
            if (e < pd) {
                pd = e;
                pool = i;
            }
        }
    strike(c, 0.35f + 0.6f * pf(P_PEBBLE), OB_POOL, pool < 255 ? W.obj[pool].x : 120, R_PEBBLE, pool, 1.1f, 0.6f);
}

/* -------------------------------------------------------------- spaces --- */
/* the cavern: four long delay lines, a Householder mix, damped; three diffusers before. The echo: one slap off the
 * far wall, darker each time */
#define RV0 3089
#define RV1 3739
#define RV2 4421
#define RV3 5107
#define AP0 431
#define AP1 613
#define AP2 887
static float rv_buf[RV0 + RV1 + RV2 + RV3 + AP0 + AP1 + AP2] EN_POOL;
static float *rv_d[4], *ap_d[3];
static const int RV_N[4] = {RV0, RV1, RV2, RV3}, AP_N[3] = {AP0, AP1, AP2};
static int rv_p[4], ap_p[3];
static float rv_lp[4], rv_g[4];
static float rv_t60 = -1.0f;
#define EC_N 28000
static float ec_buf[EC_N] EN_POOL;
static int ec_w;
static float ec_lp;

/* -------------------------------------------------------------- render --- */
static void quiet(void)
{
    int i;
    for (i = 0; i < NV; i++)
        vo[i].on = 0;
    for (i = 0; i < NPEND; i++)
        pend[i].used = 0;
}

static void play_on(int on)
{
    int i;
    if (on && !en_playing) {
        en_playing = 1;
        for (i = 0; i < EN_NDRIP; i++)
            drip_left[i] = (int32_t)((0.3f + 1.5f * frand()) * EN_SR);
        drip_was = 0;
    } else if (!on && en_playing) {              /* resting: nothing new; what rings, rings on */
        en_playing = 0;
        for (i = 0; i < NPEND; i++)
            pend[i].used = 0;
        en_drips_on = 0;
    }
}

static void ff(int minutes)
{
    int m, s;
    ff_on = 1;
    for (m = 0; m < minutes; m++)
        for (s = 0; s < 60; s++)
            world_second();
    ff_on = 0;
    river_tune(0);
    world_publish();
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
            }
            break;
        case C_STYLE:
            style_i = a % EN_NSTYLES;
            en_style_i = (uint8_t)style_i;
            for (b = 0; b < NPEND; b++)
                pend[b].used = 0;
            en_world_fresh(style_i, &W);
            world_take();
            break;
        case C_WORLD: {
            const en_world_t *w = world_in;
            if (w && en_world_valid(w, style_i)) {
                W = *w;
                world_take();
            }
            break;
        }
        case C_PLAY: play_on(a); break;
        case C_HARM: harm_next(); break;
        case C_FF: ff(b); break;
        case C_EVENT:
            if (a == EV_RUN)
                trickle();
            else if (a == EV_BATS)
                bats();
            else if (a == EV_STONE)
                stone();
            else if (a == EV_FLOOD && flood_t < 0) {
                flood_t = 0;
                W.floods++;
                DBG_EVENT(EV_FLOOD, "flood");
            }
            break;
        case C_NOTE:
            if (b > 0)
                pebble(a);
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
    for (i = 0; i < EC_N; i++)
        ec_buf[i] = 0.0f;
    ec_lp = 0.0f;
    rv_t60 = -1.0f;
    quiet();
    for (i = 0; i < 2; i++)
        dl[i].g = dl[i].gt = 0.0f;
    style_i = 0;
    en_style_i = 0;
    en_playing = 0;
    en_notes_played = 0;
    en_retunes = 0;
    en_bats_ms = en_stone_ms = en_run_ms = 0;
    sec_left = (int32_t)EN_SR;
    sec_n = 0;
    en_world_fresh(0, &W);
    world_take();
}

static float bufl[BLK], bufr[BLK], rvin[BLK], vb[BLK];
static float hp_l, hp_r, lev, rush_l, rush_r, rush_l2, rush_r2, tick_a, tick_lp, tick_hp, amb_g, riv_g;
static float vu_acc_l, vu_acc_r, lfo1;
static uint32_t vu_n;

static void render_voices(uint32_t n)
{
    int i, m, voices = 0;
    uint32_t j;
    for (i = 0; i < NV; i++) {
        vo_t *v = &vo[i];
        float gl, gr, nz = v->nz, nzk = v->nzk, nza = v->nza, nlp = v->nzlp;
        if (!v->on)
            continue;
        voices++;
        for (j = 0; j < n; j++)
            vb[j] = 0.0f;
        if (v->kind == OB_STAL) {
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
            v->lvl *= v->rb;
            if (v->nm > 1 && v->lvl < 2e-3f)
                v->nm = 1;                       /* the overtones gone first */
        } else {
            float ph = v->ph, inc = v->inc, it = v->inc_t, gk = v->gk, amp = v->amp, ad = v->adec, at = v->att,
                  h2 = v->h2;
            for (j = 0; j < n; j++) {
                float s;
                inc += (it - inc) * gk;
                ph = wrap1(ph + inc);
                at += (1.0f - at) * 0.03f;
                s = sn(ph) + h2 * sn(ph >= 0.5f ? ph * 2.0f - 1.0f : ph * 2.0f);
                vb[j] += s * amp * at;
                amp *= ad;
            }
            v->ph = ph, v->inc = inc, v->amp = amp, v->att = at;
            v->lvl = amp;
        }
        if (nz > 1e-5f) {                        /* the landing's grit, the plop */
            for (j = 0; j < n; j++) {
                nlp += (noise(&nrng) * nz - nlp) * nza;
                nz *= nzk;
                vb[j] += nlp;
            }
            v->nz = nz;
            v->nzlp = nlp;
        }
        gl = v->gl, gr = v->gr;
        for (j = 0; j < n; j++) {
            bufl[j] += vb[j] * gl;
            bufr[j] += vb[j] * gr;
        }
        if (v->lvl < 6e-5f && v->nz < 1e-5f)
            v->on = 0;
    }
    en_voices = (uint8_t)voices;
}

/* the river (music) and the ambience (its rush, far drips: their own level) */
static void render_river(uint32_t n)
{
    uint32_t j;
    int L, k;
    float riv = pf(P_RIVER), amb = pf(P_AMB), fl = en_flood;
    float rlev = riv * riv * 0.075f * (1.0f + 0.9f * fl), xf = (float)n / (EN_SR * 12.0f);
    float alev = amb * amb * 0.11f * (1.0f + 2.2f * fl), ra = onepole(260.0f + 500.0f * fl + 300.0f * amb);
    float mod;
    riv_g += ((en_playing ? 1.0f : 0.0f) - riv_g) * fm_minf((float)n / (EN_SR * 3.0f), 1.0f);
    amb_g += ((en_playing ? 1.0f : 0.0f) - amb_g) * fm_minf((float)n / (EN_SR * 3.0f), 1.0f);
    rlev *= riv_g;
    alev *= amb_g;
    cur_ph1 = wrap1(cur_ph1 + (float)n * 0.071f / EN_SR);
    cur_ph2 = wrap1(cur_ph2 + (float)n * 0.031f / EN_SR);
    mod = 0.78f + 0.12f * sn(cur_ph1) + 0.10f * sn(cur_ph2);   /* the current's swell */
    lfo1 = mod;
    for (L = 0; L < 2; L++) {
        float g0 = dl[L].g, g1;
        dl[L].g += (dl[L].gt - dl[L].g) * fm_minf(xf * 3.0f, 1.0f);
        g1 = dl[L].g;
        if (g0 < 1e-4f && g1 < 1e-4f)
            continue;
        if (rlev > 1e-6f) {
            float p[4], in[4], a0 = g0 * rlev * mod, da = (g1 - g0) * rlev * mod / (float)n;
            for (k = 0; k < 4; k++) {
                p[k] = dl[L].ph[k];
                in[k] = dl[L].inc[k];
            }
            for (j = 0; j < n; j++) {
                float s;
                p[0] = wrap1(p[0] + in[0]);
                p[1] = wrap1(p[1] + in[1]);
                p[2] = wrap1(p[2] + in[2]);
                p[3] = wrap1(p[3] + in[3]);
                s = (sn(p[0]) + 0.6f * sn(p[1]) + 0.42f * sn(p[2]) + (0.18f + 0.25f * fl) * sn(p[3])) * a0;
                a0 += da;
                bufl[j] += s;
                bufr[j] += s;
                rvin[j] += s * 0.5f;
            }
            for (k = 0; k < 4; k++)
                dl[L].ph[k] = p[k];
        }
    }
    if (alev > 1e-6f) {
        float am = alev * (0.7f + 0.3f * mod), ha = 0.012f;
        for (j = 0; j < n; j++) {
            float a = noise(&nrng), b = noise(&nrng2);
            rush_l += (a - rush_l) * ra;
            rush_r += (b - rush_r) * ra;
            rush_l2 += (rush_l - rush_l2) * ha;
            rush_r2 += (rush_r - rush_r2) * ha;
            bufl[j] += (rush_l - rush_l2) * am;
            bufr[j] += (rush_r - rush_r2) * am;
            rvin[j] += (rush_l - rush_l2) * am * 0.3f;
        }
        /* far drips: untuned ticks, deep in the reverb */
        if (en_playing && frand() < (float)n * (1.2f + 3.0f * wet_eff()) / EN_SR)
            tick_a = amb * amb_g * (0.04f + 0.05f * frand());
        if (tick_a > 1e-5f) {
            float d = tau(0.0015f);
            for (j = 0; j < n; j++) {
                float x = noise(&nrng) * tick_a;
                tick_lp += (x - tick_lp) * 0.5f;
                tick_hp += (tick_lp - tick_hp) * 0.15f;
                rvin[j] += (tick_lp - tick_hp) * 2.0f;
                tick_a *= d;
            }
        }
    }
}

static void render_space(uint32_t n)
{
    uint32_t j;
    float room = pf(P_CAVERN), send = 0.18f + 0.55f * room, tone = pf(P_TONE), damp = 0.55f - 0.35f * tone;
    float dry = 1.0f - 0.35f * room, wet = 0.30f + 0.62f * room, echo = pf(P_ECHO);
    float efb = 0.22f + 0.33f * echo, emix = echo * 0.55f;
    int ed = (int)cv(style_i)->echo * 441, i;
    float t60 = 2.5f + 8.5f * room;
    if (t60 != rv_t60) {
        rv_t60 = t60;
        for (i = 0; i < 4; i++)
            rv_g[i] = fm_exp2f(-9.966f * (float)RV_N[i] / (EN_SR * t60));
    }
    if (ed > EC_N - 1)
        ed = EC_N - 1;
    for (j = 0; j < n; j++) {
        float mono = (bufl[j] + bufr[j]) * 0.5f, in, x[4], sum, yl, yr, e;
        int r = ec_w - ed;
        if (r < 0)
            r += EC_N;
        e = ec_buf[r];                           /* the slap off the far wall */
        ec_lp += (e - ec_lp) * (0.35f + 0.3f * tone);
        ec_buf[ec_w] = mono * (echo > 0.0f ? 1.0f : 0.0f) + ec_lp * efb;
        if (++ec_w >= EC_N)
            ec_w = 0;
        in = (mono + ec_lp * emix) * send + rvin[j];
        for (i = 0; i < 3; i++) {
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
            float y = (x[i] - sum) * rv_g[i];
            rv_lp[i] += (y - rv_lp[i]) * (1.0f - damp);
            rv_d[i][rv_p[i]] = rv_lp[i] + in * (i & 1 ? 0.5f : -0.5f);
            if (++rv_p[i] >= RV_N[i])
                rv_p[i] = 0;
        }
        yl = x[0] + x[2] * 0.6f;
        yr = x[1] + x[3] * 0.6f;
        bufl[j] = bufl[j] * dry + ec_lp * emix * 0.6f + yl * wet;
        bufr[j] = bufr[j] * dry + ec_lp * emix * 0.6f + yr * wet;
    }
}

static void render_bus(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    float hp_a = onepole(28.0f), lv_rel = tau(0.6f), lv_att = 1.0f - tau(0.003f);
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
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.2f;
    static uint32_t pub;
    drain();
    while (n) {
        uint32_t k = n > BLK ? BLK : n, j;
        int i;
        if (en_playing) {
            sec_left -= (int32_t)k;
            while (sec_left <= 0) {
                sec_left += (int32_t)EN_SR;
                world_second();
            }
            drips((int32_t)k);
            for (i = 0; i < NPEND; i++)
                if (pend[i].used) {
                    pend[i].wait -= (int32_t)k;
                    if (pend[i].wait <= 0) {
                        pend[i].used = 0;
                        strike(pend[i].cents, pend[i].vel, pend[i].kind, pend[i].x, pend[i].role, pend[i].obj,
                               pend[i].ringx, pend[i].glide);
                    }
                }
        }
        clock_ms(k);
        for (j = 0; j < k; j++)
            bufl[j] = bufr[j] = rvin[j] = 0.0f;
        render_voices(k);
        render_river(k);
        render_space(k);
        render_bus(out, k, gain);
        if (++vu_n >= 64u) {
            float l = fm_sqrtf(vu_acc_l / (float)(k * 64u)), r = fm_sqrtf(vu_acc_r / (float)(k * 64u));
            en_vu_l = en_vu_l + (fm_minf(l * 4.0f, 1.2f) - en_vu_l) * 0.45f;
            en_vu_r = en_vu_r + (fm_minf(r * 4.0f, 1.2f) - en_vu_r) * 0.45f;
            vu_acc_l = vu_acc_r = 0.0f;
            vu_n = 0;
        }
        if (++pub >= 128u) {                     /* the world for the UI, every 0.2 s */
            pub = 0;
            world_publish();
        }
        out += 2 * k;
        n -= k;
    }
}
