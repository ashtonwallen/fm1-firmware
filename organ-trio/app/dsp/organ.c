/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * ORGAN TRIO: ashtonwallen, 2026 */
/* The instrument (organ.h). One compilation unit, -O2: this file, the harmony (organ_harm.c), the tunes
 * (organ_tune.c) and the players (organ_play.c). The command queue and the render's shape are AMB-1's. */
#include "organ.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Tempo", 0, 100, 50},  {"Intensity", 0, 100, 50}, {"Room", 0, 100, 40},  {"Swing", 0, 100, 50},
    {"Organ", 0, 100, 75},  {"Guitar", 0, 100, 70},    {"Drums", 0, 100, 70}, {"Crowd", 0, 100, 35},
    {"Drawbars", 0, EN_NREG - 1, 0}, {"Speaker", 0, 2, 1}, {"Drive", 0, 100, 35}, {"Perc", 0, 3, 2},
    {"Tone", 0, 100, 55},   {"Focus", 0, 100, 50},     {"Stretch", 0, 3, 1},
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

#include "organ_harm.c"

void en_param_text(int i, int v, char *buf)
{
    if (i == P_TEMPO) {                           /* the band's feel for time: slower .. faster than the tune's */
        put_s(buf, v < 20 ? "Laid back" : v < 40 ? "Easy" : v < 61 ? "Natural" : v < 80 ? "Pushed" : "Burning");
    } else if (i == P_SWING) {
        put_s(buf, v < 20 ? "Straight" : v < 40 ? "Even" : v < 61 ? "Natural" : v < 80 ? "Deep" : "Heavy");
    } else if (i == P_FOCUS) {                    /* who gets the solos: the organ .. the guitar */
        put_s(buf, v < 25 ? "Organ" : v < 50 ? "Mostly organ" : v < 75 ? "Shared" : "Guitar");
    } else if (i == P_STRETCH) {
        static const char *const S[4] = {"Short", "Normal", "Long", "Marathon"};
        put_s(buf, S[v < 0 ? 0 : v > 3 ? 3 : v]);
    } else if (i == P_DRAW) {                     /* the registrations by name (the screen shows the bars) */
        static const char *const R[EN_NREG] = {"Grease", "Bright", "Hollow", "Smoke", "Mellow", "Church", "Flute", "Ballad"};
        put_s(buf, R[v < 0 ? 0 : v >= EN_NREG ? EN_NREG - 1 : v]);
    } else if (i == P_SPEAKER) {
        static const char *const S[3] = {"Slow", "Auto", "Fast"};
        put_s(buf, S[v < 0 ? 0 : v > 2 ? 2 : v]);
    } else if (i == P_PERC) {
        static const char *const S[4] = {"Off", "2nd", "3rd", "3rd slow"};
        put_s(buf, S[v < 0 ? 0 : v > 3 ? 3 : v]);
    } else {
        put_u(buf, (uint32_t)(v < 0 ? 0 : v));
    }
}

/* ------------------------------------------------------------ styles --- */
/* the user's fourteen (Tempo Intensity Room Swing, Organ Guitar Drums Crowd, Drawbars Speaker Drive Perc, Tone
 * Focus) and the band's own flavour: w: how often each kind of tune comes up (greasy blues, slow minor blues,
 * groove, gospel ballad, shuffle, swinging standard); pick: the guitarist (0 thumb, dark; 1 a pick, brighter);
 * brush: 0 sticks, 1 brushes on the ballads and slow blues; comp: the guitar's comping (0 sparse, 1 even, 2 four to
 * the bar); lines: how dense the solos run; vib: the scanner (0 off, 1 chorus, 2 vibrato); click: the key click;
 * harm: reharmonization of the standards; tempo: the set's tempos (%) */
typedef struct {
    const char *name;
    int8_t v[P_NSTYLE];
    uint8_t w[K_NKINDS];
    uint8_t pick, brush, comp, lines, vib, click, harm, tempo;
} style_t;
static const style_t STYLES[EN_NSTYLES] = {
    /*                  Tem Int Roo Swi  Org Gtr Drm Crw  Drw Spk Drv Prc  Ton Foc     Bl Mi Gr Go Sh Sw  pk br cp lin vb clk har tmp */
    {"Gravy Boat",      {50, 55, 40, 55, 75, 70, 70, 35, 0, 1, 35, 2, 55, 40}, {9, 4, 4, 3, 5, 3}, 0, 0, 1, 55, 1, 45, 20, 100},
    {"Sixteen Feet",    {48, 60, 45, 52, 78, 66, 70, 35, 3, 1, 45, 2, 52, 30}, {6, 3, 3, 6, 5, 2}, 0, 0, 2, 55, 1, 40, 15, 98},
    {"Hot Comb",        {58, 65, 35, 58, 74, 74, 74, 35, 1, 1, 50, 2, 62, 55}, {5, 1, 3, 1, 9, 3}, 1, 0, 2, 65, 1, 55, 20, 104},
    {"Fish Fry",        {54, 62, 38, 30, 74, 74, 74, 40, 5, 1, 45, 1, 58, 50}, {3, 2, 9, 1, 3, 1}, 1, 0, 1, 55, 1, 50, 10, 102},
    {"Amen Corner",     {42, 50, 55, 50, 78, 64, 62, 35, 4, 1, 30, 0, 48, 35}, {3, 6, 1, 9, 1, 1}, 0, 1, 0, 40, 1, 30, 10, 94},
    {"Third Harmonic",  {60, 62, 38, 50, 75, 70, 72, 30, 0, 1, 25, 2, 60, 45}, {4, 1, 1, 1, 3, 9}, 0, 0, 1, 80, 1, 60, 60, 106},
    {"Choir Robe",      {40, 45, 60, 50, 80, 62, 60, 35, 7, 1, 20, 0, 46, 30}, {2, 5, 1, 9, 1, 2}, 0, 1, 0, 35, 2, 25, 20, 92},
    {"Percolator",      {66, 78, 32, 55, 74, 72, 78, 40, 2, 1, 60, 3, 64, 55}, {5, 1, 3, 1, 6, 6}, 1, 0, 2, 85, 1, 60, 40, 108},
    {"Velvet Booth",    {36, 35, 58, 52, 74, 66, 55, 35, 7, 0, 15, 0, 44, 50}, {3, 8, 1, 6, 1, 3}, 0, 1, 0, 30, 1, 25, 30, 90},
    {"Collard Greens",  {52, 58, 42, 45, 75, 72, 72, 35, 5, 1, 40, 2, 56, 50}, {6, 3, 7, 2, 4, 2}, 0, 0, 1, 55, 1, 45, 15, 100},
    {"Two-Tone Shoes",  {56, 60, 38, 56, 70, 80, 72, 35, 1, 1, 35, 2, 60, 85}, {5, 2, 4, 1, 7, 4}, 1, 0, 1, 65, 1, 45, 25, 102},
    {"Holy Roller",     {54, 72, 40, 55, 80, 66, 74, 40, 6, 1, 70, 2, 58, 30}, {6, 3, 4, 3, 5, 2}, 0, 0, 1, 70, 1, 50, 15, 100},
    {"Hush Puppies",    {46, 45, 48, 54, 74, 72, 64, 40, 4, 1, 25, 3, 50, 60}, {5, 4, 2, 4, 4, 4}, 0, 1, 1, 45, 1, 35, 25, 96},
};
const char *en_style_name(int i) { return STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES].name; }
void en_style_recipe(int i, int16_t *par)
{
    int k;
    const style_t *s = &STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES];
    for (k = 0; k < P_NSTYLE; k++)
        par[k] = s->v[k];
}

/* the kinds of tune: their tempo range (BPM, before the band's and the knob's scaling) */
typedef struct {
    const char *name;
    uint16_t lo, hi;
} kind_t;
static const kind_t KINDS[K_NKINDS] = {
    {"Blues", 112, 150}, {"Slow minor blues", 54, 70}, {"Groove", 98, 122}, {"Gospel ballad", 58, 72},
    {"Shuffle", 118, 156}, {"Swing", 132, 196},
};
const char *en_kind_name(int k) { return KINDS[k < 0 || k >= K_NKINDS ? 0 : k].name; }

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_STYLE, C_PLAY, C_SKIP, C_REPEAT, C_TUNE_NO, C_SEED_HI, C_SEED_LO, C_NOTE, C_PANIC };
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
void en_tune(uint32_t no, uint32_t seed)
{
    post(C_SEED_HI, 0, (int)(seed >> 16));
    post(C_SEED_LO, 0, (int)(seed & 0xFFFFu));
    post(C_TUNE_NO, (int)(no >> 16 & 0xFFu), (int)(no & 0xFFFFu));
}

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_phase, en_repeat_on, en_role, en_soloist, en_climax;
volatile uint32_t en_tune_no, en_tune_seed, en_seed_base = 0x4F524731u, en_tunes_played, en_beats;
volatile uint8_t en_kind, en_key, en_minor, en_chord_root, en_chord_q, en_bar, en_form_bars, en_chorus, en_nchorus;
volatile uint8_t en_beat, en_bpb = 4;
volatile uint16_t en_bpm = 120, en_chord_pcs;
volatile uint8_t en_hits[NHITS];
volatile uint8_t en_gtr_note = 64, en_bass_note = 40;
volatile uint32_t en_keys_up[2], en_keys_lo[2];
volatile float en_draw[9];
volatile uint8_t en_perc_on;
volatile float en_horn_ph, en_drum_ph, en_horn_hz = 0.8f, en_expr = 0.8f;
volatile float en_applause, en_vu_l, en_vu_r;
#ifdef EN_HOST
void (*en_trace)(int inst, int rec, int role, int fbar, int tick, int dur, int note, int vel);
void (*en_trace_bar)(int rec, int role, int fbar, int chorus, const char *chords);
#endif

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
static float trand(void) { return (float)(xs(&trng) >> 8) * (1.0f / 16777216.0f); }   /* the tune's own */
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
static float pf(int p) { return (float)par[p] * 0.01f; }

/* --------------------------------------------------------------- organ --- */
/* tonewheels: every drawbar is a sine at a whole multiple of the 16' pitch (1 3 2 4 6 8 10 12 16), so a manual's
 * registration is one wavetable over the 16' cycle, rebuilt a quarter at a time as the drawbars move. A voice
 * reads it; a key's contacts make the click; percussion (the second or third harmonic, decaying) sounds on the
 * upper manual when no other key there is held (single trigger) */
#define OT_N 1024
static float otab[2][OT_N + 1] EN_POOL;
static const uint8_t HARM[9] = {1, 3, 2, 4, 6, 8, 10, 12, 16};
static float reg_lv[2][9], reg_tg[2][9];          /* the drawbars as they stand, where they're going */
static float reg_gn[2];                           /* each manual's level (the registration's loudness evened) */
static int reg_chunk[2];                          /* quarters of the table still to rebuild */
static int reg_base;                              /* the tune's registration (P_DRAW) */
static int reg_mode;                              /* 0 the tune's, 1 pulled out (a climax), 2 everything out */

#define NOV 12
typedef struct {
    float ph, inc, env, tgt, perc, pdec;
    int32_t gate;                                /* samples until the key comes up; < 0: held by the player */
    uint8_t note, on, man, player;
    uint32_t age;
} ov_t;
static ov_t ov[NOV];
static uint32_t ov_clock;
static float click_e, click_lo, click_bd;          /* the key click: an envelope on a band of noise */
static float expr = 0.8f, expr_tg = 0.8f, expr_rate = 0.0f;   /* the swell pedal */

static float dbamp(float d)                       /* a drawbar's setting (0..8) as an amplitude: 3 dB a step */
{
    return d < 1.0f ? d * 0.0884f : fm_exp2f((d - 8.0f) * 0.5f);
}
static void reg_set(int man, const uint8_t *r)
{
    int k;
    for (k = 0; k < 9; k++)
        reg_tg[man][k] = (float)r[k];
}
static void reg_target(void)                     /* the upper manual: the tune's, or pulled out */
{
    uint8_t r[9];
    int k;
    for (k = 0; k < 9; k++) {
        r[k] = REG_UP[reg_base][k];
        if (reg_mode == 1 && REG_FULL[k] > r[k] && k >= 3)
            r[k] = (uint8_t)(r[k] + (REG_FULL[k] - r[k]) * (k < 6 ? 3 : 2) / 4);
        if (reg_mode == 2)
            r[k] = 8;
    }
    reg_set(0, r);
    reg_set(1, REG_LO[reg_base]);
}
static void reg_move(float dt)                   /* the drawbars pulled, at a hand's speed; the tables follow */
{
    int m, k, i;
    for (m = 0; m < 2; m++) {
        int moved = 0;
        float s2 = 0.0f;
        for (k = 0; k < 9; k++) {
            float d = reg_tg[m][k] - reg_lv[m][k], st = 7.0f * dt;
            if (d > st)
                d = st;
            else if (d < -st)
                d = -st;
            if (d != 0.0f) {
                reg_lv[m][k] += d;
                moved = 1;
            }
            s2 += dbamp(reg_lv[m][k]) * dbamp(reg_lv[m][k]);
        }
        if (moved)
            reg_chunk[m] = 4;
        reg_gn[m] = 0.62f / (0.45f + fm_sqrtf(s2 * 0.5f));
        if (reg_chunk[m] > 0) {
            int c = --reg_chunk[m], i0 = c * (OT_N / 4), i1 = i0 + OT_N / 4;
            float a[9];
            for (k = 0; k < 9; k++)
                a[k] = dbamp(reg_lv[m][k]);
            for (i = i0; i < i1; i++) {
                float s = 0.0f;
                for (k = 0; k < 9; k++)
                    s += a[k] * SINT[(i * HARM[k]) & (SN - 1)];
                otab[m][i] = s;
            }
            if (c == 0)
                otab[m][OT_N] = otab[m][0];
        }
    }
    for (k = 0; k < 9; k++)
        en_draw[k] = reg_lv[0][k];
}

static void organ_on(int note, float vel, int32_t gate, int man, int player)
{
    ov_t *v = 0;
    int i, held = 0;
    uint32_t oldest = 0;
    while (note < 36)
        note += 12;
    while (note > 96)
        note -= 12;
    for (i = 0; i < NOV; i++) {                  /* the same key again, a free voice, the oldest */
        if (ov[i].on && ov[i].man == man && ov[i].tgt > 0.5f && ov[i].note != note)
            held = 1;
        if (!v && ov[i].on && ov[i].note == note && ov[i].man == man && ov[i].player == player)
            v = &ov[i];
    }
    if (!v)
        for (i = 0; i < NOV; i++)
            if (!ov[i].on) {
                v = &ov[i];
                break;
            }
    if (!v)
        for (i = 0; i < NOV; i++) {
            uint32_t a = ov_clock - ov[i].age + (ov[i].tgt < 0.5f ? 0x10000000u : 0u);
            if (!ov[i].player && a >= oldest) {
                oldest = a;
                v = &ov[i];
            }
        }
    if (!v)
        v = &ov[0];
    if (!(v->on && v->note == note))
        v->ph = 0.0f, v->env = 0.0f;
    v->inc = mtof((float)note) * 0.5f / EN_SR;   /* the 16' pitch: the table's cycle */
    v->tgt = 0.82f + 0.18f * fm_clampf(vel, 0.0f, 1.0f);
    v->gate = gate;
    v->note = (uint8_t)note;
    v->on = 1;
    v->man = (uint8_t)man;
    v->player = (uint8_t)player;
    v->age = ov_clock++;
    v->perc = 0.0f;
    if (man == 0 && par[P_PERC] && !held) {      /* percussion: single trigger */
        v->perc = 0.85f;
        v->pdec = tau(par[P_PERC] == 3 ? 1.0f : 0.22f);
    }
    click_e = fm_minf(click_e + 0.55f * (0.4f + 0.6f * (float)STYLES[style_i].click * 0.01f), 1.0f);
}
static void organ_off_player(int note)
{
    int i;
    for (i = 0; i < NOV; i++)
        if (ov[i].on && ov[i].player && ov[i].note == note && ov[i].gate < 0)
            ov[i].gate = 0;
}

/* the bass: the left hand on the lower manual's 16' and 8', the pedal tapped for its thump */
static struct {
    float ph, inc, env, tgt, thump;
    int32_t gate;
    uint8_t on;
} bv[2];
static int bv_cur;
static void bass_on(int note, float vel, int32_t gate)
{
    int s = bv_cur ^ 1;
    if (bv[bv_cur].on && bv[bv_cur].gate != 0)
        bv[bv_cur].gate = 0;                     /* the last note lets go as the next sounds */
    bv_cur = s;
    while (note < 28)
        note += 12;
    while (note > 55)
        note -= 12;
    bv[s].ph = 0.0f;
    bv[s].inc = mtof((float)note) / EN_SR;
    bv[s].env = bv[s].on ? bv[s].env : 0.0f;
    bv[s].tgt = 0.7f + 0.3f * fm_clampf(vel, 0.0f, 1.0f);
    bv[s].thump = 0.5f + 0.5f * vel;
    bv[s].gate = gate;
    bv[s].on = 1;
    en_bass_note = (uint8_t)note;
    en_hits[HIT_BASS]++;
}

/* -------------------------------------------------------------- guitar --- */
/* an archtop: plucked strings (Karplus-Strong), two for the lines in turn, four for the chords */
#define GS 6
#define GN 1024
static float g_buf[GS][GN] EN_POOL;
static struct {
    float d, fb, c, lp, f;
    int32_t gate;
    int w;
    uint8_t on, note;
} gs[GS];
static int g_line, g_chord;
static void gtr_on(int note, float vel, int32_t gate, int chord)
{
    int s, i, L;
    float f, c, e = 0.0f, d0, t60;
    while (note < 40)
        note += 12;
    while (note > 88)
        note -= 12;
    if (chord) {
        s = -1;
        for (i = 2; i < GS; i++)
            if (gs[i].on && gs[i].note == note)
                s = i;
        if (s < 0) {
            g_chord = g_chord >= GS - 1 ? 2 : g_chord + 1;
            s = g_chord < 2 ? 2 : g_chord;
        }
    } else {
        if (gs[g_line].on)                       /* the last note: lifted off */
            gs[g_line].fb = fm_minf(gs[g_line].fb, fm_expf(-6.9f / (0.05f * gs[g_line].f)));
        g_line ^= 1;
        s = g_line;
    }
    f = mtof((float)note);
    vel = fm_clampf(vel, 0.05f, 1.0f);
    c = (STYLES[style_i].pick ? 0.42f : 0.30f) + 0.2f * vel;   /* the loop's low-pass: thumb dark, pick brighter */
    gs[s].f = f;
    gs[s].c = c;
    gs[s].d = EN_SR / f - (1.0f - c) / c;
    if (gs[s].d > (float)(GN - 4))
        gs[s].d = (float)(GN - 4);
    t60 = fm_clampf(2.4f - (float)(note - 52) * 0.035f, 0.8f, 2.6f);
    gs[s].fb = fm_expf(-6.9f / (t60 * f));
    gs[s].gate = gate;
    gs[s].lp = 0.0f;
    gs[s].on = 1;
    gs[s].note = (uint8_t)note;
    L = (int)gs[s].d + 2;
    d0 = 1.0f / (float)L;
    for (i = 0; i < L; i++) {                    /* the excitation: the pick's (or thumb's) round push, some grit */
        float bump = sn((float)i * d0 * 0.5f) * 0.8f;
        e += (noise() - e) * (STYLES[style_i].pick ? 0.35f : 0.2f);
        g_buf[s][(gs[s].w - 1 - i) & (GN - 1)] = (bump + e * 0.6f) * vel;
    }
    en_gtr_note = (uint8_t)note;
    en_hits[HIT_GTR]++;
}

/* --------------------------------------------------------------- drums --- */
static struct {
    float ph[6], inc[6];
    float wash, ping, bell, bph1, bph2, h1, h2, lp;
} ride;
static struct {
    float amp, env, lo, bd, dec;
} hat;
static struct {
    float amp;
} crash;
static struct {
    float ph, inc, tamp, tdec, namp, ndec, lo, bd, lp;
} snare;
static struct {
    float ph, amp, bph, bamp;
} rim;
static struct {
    float ph, f, fend, pdec, amp, adec, click;
} kick;
static struct {
    float ph, f, fend, amp, adec, namp;
} tom[2];
static struct {
    float amp, att, lp, lo, bd;
} brush;
static struct {
    float amp, ph;
} stick;
static float sweep_lvl;                           /* the brushes' circling (0: sticks) */

static void ride_hit(float v, int bell)
{
    ride.wash = fm_minf(ride.wash * 0.7f + v * 0.55f, 1.1f);
    ride.ping = v;
    if (bell)
        ride.bell = v * 0.6f;
    en_hits[HIT_RIDE]++;
}
static void hat_hit(float v, int open)
{
    hat.amp = v;
    hat.env = 0.0f;
    hat.dec = tau(open ? 0.22f : 0.028f);
    en_hits[HIT_HAT]++;
}
static void crash_hit(float v)
{
    crash.amp = fm_minf(crash.amp + v, 1.2f);
    en_hits[HIT_CRASH]++;
}
static void snare_hit(float v)
{
    snare.ph = 0.0f;
    snare.inc = (205.0f + 12.0f * frand()) / EN_SR;
    snare.tamp = v * 0.5f;
    snare.tdec = tau(0.045f);
    snare.namp = v;
    snare.ndec = tau(0.09f + 0.03f * frand());
    en_hits[HIT_SNARE]++;
}
static void rim_hit(float v)                      /* the cross-stick: the stick's wood on the rim, the head's knock */
{
    rim.ph = rim.bph = 0.0f;
    rim.amp = v;
    rim.bamp = v * 0.7f;
    en_hits[HIT_SNARE]++;
}
static void kick_hit(float v)
{
    kick.ph = 0.0f;
    kick.f = 105.0f;
    kick.fend = 58.0f + 4.0f * frand();
    kick.pdec = tau(0.022f);
    kick.amp = v;
    kick.adec = tau(0.22f);
    kick.click = v * 0.12f;
    en_hits[HIT_KICK]++;
}
static void tom_hit(int t, float v)
{
    tom[t].ph = 0.0f;
    tom[t].f = t ? 104.0f : 172.0f;
    tom[t].fend = t ? 86.0f : 146.0f;
    tom[t].amp = v;
    tom[t].adec = tau(t ? 0.42f : 0.30f);
    tom[t].namp = v * 0.5f;
    en_hits[HIT_TOM]++;
}
static void brush_hit(float v)
{
    brush.amp = fm_maxf(brush.amp, v);
    brush.att = 0.0f;
    en_hits[HIT_SNARE]++;
}
static void stick_hit(float v)
{
    stick.amp = v;
    stick.ph = 0.0f;
}

/* --------------------------------------------------------------- crowd --- */
#define NCLAP 8
static struct {
    float amp, dec, lo, bd, f, gl, gr;
} clap[NCLAP];
static struct {
    float amp, ph1, ph2, i1, i2, gl, gr;
} clink;
static float appl_lvl, appl_t, murm_lp, murm_hp, murm_ph, clap_next, clink_next;

/* -------------------------------------------------------------- spaces --- */
/* the club: four delay lines, a Householder mix, damped; two diffusers before */
#define RV0 1307
#define RV1 1559
#define RV2 1823
#define RV3 2087
#define AP0 241
#define AP1 367
static float rv_buf[RV0 + RV1 + RV2 + RV3 + AP0 + AP1] EN_POOL;
static float *rv_d[4];
static const int RV_N[4] = {RV0, RV1, RV2, RV3};
static int rv_p[4], ap_p[2];
static float *ap_d[2];
static const int AP_N[2] = {AP0, AP1};
static float rv_lp[4];

/* the rotating speaker: the horn (above the crossover) and the drum (below), each turning at its own speed */
#define HN 128
static float horn_buf[HN];
static int horn_w;
static float lsl_lp, horn_hz = 0.8f, drum_hz = 0.7f, horn_ph, drum_ph;
static uint8_t lsl_fast;                          /* the band's call (Speaker on Auto) */
#define VN 64
static float vib_buf[VN];                         /* the scanner chorus */
static int vib_w;
static float vib_ph;

/* -------------------------------------------------------- the schedule --- */
#define BLK 256
#define NEV 192
enum {
    I_RH, I_CMP, I_BASS, I_GTR, I_GCH, I_RIDE, I_HAT, I_OHAT, I_SNARE, I_KICK, I_TOM, I_CRASH, I_BRUSH, I_STICK,
    I_BELL, I_RIM, I_SWELL, I_SPEED, I_DRAW, I_NI
};
typedef struct {
    int32_t wait, gate;
    uint8_t inst, note, used;
    float vel;
} ev_t;
static ev_t evq[NEV];

static void sched(int inst, int32_t wait, int note, float vel, int32_t gate)
{
    int i;
    for (i = 0; i < NEV; i++)
        if (!evq[i].used) {
            evq[i].used = 1;
            evq[i].inst = (uint8_t)inst;
            evq[i].wait = wait < 0 ? 0 : wait;
            evq[i].note = (uint8_t)note;
            evq[i].vel = vel;
            evq[i].gate = gate;
            return;
        }
}

static void fire(ev_t *e)
{
    float v = e->vel;
    switch (e->inst) {
    case I_RH:
        organ_on(e->note, v, e->gate, 0, 0);
        en_hits[HIT_ORG]++;
        break;
    case I_CMP:
        organ_on(e->note, v, e->gate, 1, 0);
        en_hits[HIT_CMP]++;
        break;
    case I_BASS: bass_on(e->note, v, e->gate); break;
    case I_GTR: gtr_on(e->note, v, e->gate, 0); break;
    case I_GCH: gtr_on(e->note, v, e->gate, 1); break;
    case I_RIDE: ride_hit(v, 0); break;
    case I_BELL: ride_hit(v, 1); break;
    case I_HAT: hat_hit(v, 0); break;
    case I_OHAT: hat_hit(v, 1); break;
    case I_SNARE: snare_hit(v); break;
    case I_RIM: rim_hit(v); break;
    case I_KICK: kick_hit(v); break;
    case I_TOM: tom_hit(e->note & 1, v); break;
    case I_CRASH: crash_hit(v); break;
    case I_BRUSH: brush_hit(v); break;
    case I_STICK: stick_hit(v); break;
    case I_SWELL:                                /* the swell pedal: to note/100 over gate samples */
        expr_tg = (float)e->note * 0.01f;
        expr_rate = 1.0f / (float)(e->gate > 64 ? e->gate : 64);
        break;
    case I_SPEED: lsl_fast = e->note != 0; break;
    case I_DRAW:
        reg_mode = e->note;
        reg_target();
        break;
    default: break;
    }
}

static void memset_l(void *p, unsigned n)       /* zeroed, without the C library */
{
    uint8_t *b = (uint8_t *)p;
    while (n--)
        *b++ = 0;
}
static void plan_note(int inst, int tick, int dur, int note, int vel, int fl);
static void play_reset(void);
static float sweep_shape(void);

#include "organ_tune.c"
#include "organ_play.c"

/* ------------------------------------------------------------- control --- */
static void quiet(void)
{
    int i;
    for (i = 0; i < NOV; i++)
        ov[i].on = 0;
    for (i = 0; i < NEV; i++)
        evq[i].used = 0;
    for (i = 0; i < GS; i++)
        gs[i].on = 0;
    for (i = 0; i < 2; i++) {
        bv[i].on = 0;
        tom[i].amp = 0.0f;
    }
    for (i = 0; i < NCLAP; i++)
        clap[i].amp = 0.0f;
    ride.wash = ride.ping = ride.bell = hat.amp = crash.amp = 0.0f;
    snare.tamp = snare.namp = kick.amp = kick.click = brush.amp = stick.amp = clink.amp = rim.amp = rim.bamp = 0.0f;
    sweep_lvl = 0.0f;
    click_e = 0.0f;
}

static void play_on(int on)
{
    int i;
    if (on && !en_playing) {
        en_playing = 1;
        fade = 0.0f;
        fade_t = 1.0f;
        fade_rate = 1.0f / (0.4f * EN_SR);
    } else if (!on && en_playing) {
        en_playing = 0;
        fade_t = 0.0f;                           /* the band stops: cymbals caught, keys up, strings damped */
        fade_rate = 1.0f / (0.35f * EN_SR);
        for (i = 0; i < NEV; i++)
            evq[i].used = 0;
        for (i = 0; i < NOV; i++)
            if (ov[i].on && !ov[i].player)
                ov[i].gate = 0;
        for (i = 0; i < 2; i++)
            bv[i].gate = 0;
        for (i = 0; i < GS; i++)
            if (gs[i].on)
                gs[i].fb = fm_expf(-6.9f / (0.08f * gs[i].f));
        sweep_lvl = 0.0f;
        appl_lvl = 0.0f;
        lsl_fast = 0;
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
                if (a == P_DRAW && reg_base != par[P_DRAW]) {   /* the registration: the drawbars move now */
                    reg_base = par[P_DRAW];
                    reg_target();
                }
            }
            break;
        case C_STYLE: style_i = a % EN_NSTYLES; break;
        case C_PLAY: play_on(a); break;
        case C_SKIP:
            if (en_phase == PH_TUNE && !skipping) {
                skipping = 1;
                fade_t = 0.0f;
                fade_rate = 1.0f / (0.9f * EN_SR);
            } else if (en_phase != PH_TUNE) {
                gap_left = 0.0f;                 /* between tunes: the next one now */
                en_phase = PH_GAP;
            }
            break;
        case C_REPEAT: en_repeat_on = (uint8_t)(a != 0); break;
        case C_SEED_HI: en_seed_base = (en_seed_base & 0xFFFFu) | ub << 16; break;
        case C_SEED_LO: en_seed_base = (en_seed_base & 0xFFFF0000u) | ub; break;
        case C_TUNE_NO:
            en_tune_no = (uint32_t)a << 16 | ub; /* the next is no + 1 */
            quiet();
            new_tune();
            break;
        case C_NOTE:
            if (b > 0)
                organ_on(a, (float)b * (1.0f / 127.0f), -1, 0, 1);
            else
                organ_off_player(a);
            break;
        case C_PANIC: quiet(); break;
        }
    }
}

void en_init(void)
{
    int i, m;
    float *p = rv_buf;
    static const float RIDE_F[6] = {339.0f, 502.0f, 610.0f, 862.0f, 891.0f, 1320.0f};
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
    for (m = 0; m < GS; m++)
        for (i = 0; i < GN; i++)
            g_buf[m][i] = 0.0f;
    for (i = 0; i < HN; i++)
        horn_buf[i] = 0.0f;
    for (i = 0; i < VN; i++)
        vib_buf[i] = 0.0f;
    for (i = 0; i < 6; i++)
        ride.inc[i] = RIDE_F[i] / EN_SR;
    reg_base = par[P_DRAW];
    reg_mode = 0;
    reg_target();
    for (m = 0; m < 2; m++) {                    /* the drawbars where they're set: the tables whole */
        for (i = 0; i < 9; i++)
            reg_lv[m][i] = reg_tg[m][i];
        reg_chunk[m] = 4;
    }
    for (i = 0; i < 4; i++)
        reg_move(0.0f);
    quiet();
    en_playing = 0;
    en_tune_no = 0;
    new_tune();
}

/* -------------------------------------------------------------- render --- */
static float bufl[BLK], bufr[BLK], rsend[BLK], obuf[BLK], bbuf[BLK];
static float tone_a = 0.3f, t1l, t2l, t1r, t2r;  /* the bus's low-pass */
static float hp_l, hp_r;                         /* a DC / rumble cut */
static float lev = 0.0f;                         /* the leveller's envelope */
static float vu_acc_l, vu_acc_r;
static uint32_t vu_n;

static void render_organ(uint32_t n)
{
    int i;
    uint32_t j;
    float pm = par[P_PERC] == 1 ? 4.0f : 6.0f, att = 0.016f, rel = 0.0035f;
    uint32_t keys[2][2] = {{0, 0}, {0, 0}};
    for (j = 0; j < n; j++)
        obuf[j] = 0.0f;
    for (i = 0; i < NOV; i++) {
        ov_t *v = &ov[i];
        const float *T;
        float ph, inc, env, tgt, perc, pd, g;
        uint32_t seg, j0 = 0;
        if (!v->on)
            continue;
        T = otab[v->man];
        g = reg_gn[v->man] * (v->man ? 0.8f : 1.0f);
        if (!v->player)
            g *= fade;
        ph = v->ph, inc = v->inc, env = v->env, perc = v->perc, pd = v->pdec;
        while (j0 < n) {                         /* in two runs: the key down, then up */
            tgt = v->gate == 0 ? 0.0f : v->tgt;
            seg = n - j0;
            if (v->gate > 0 && (uint32_t)v->gate < seg)
                seg = (uint32_t)v->gate;
            if (perc > 0.002f) {
                for (j = j0; j < j0 + seg; j++) {
                    float x = ph * (float)OT_N, f, s;
                    int32_t k = (int32_t)x;
                    f = x - (float)k;
                    s = T[k] + (T[k + 1] - T[k]) * f;
                    env += (tgt - env) * (tgt > env ? att : rel);
                    obuf[j] += (s + perc * sn(ph * pm)) * env * g;
                    perc *= pd;
                    ph = wrap1(ph + inc);
                }
            } else {
                for (j = j0; j < j0 + seg; j++) {
                    float x = ph * (float)OT_N, f, s;
                    int32_t k = (int32_t)x;
                    f = x - (float)k;
                    s = T[k] + (T[k + 1] - T[k]) * f;
                    env += (tgt - env) * (tgt > env ? att : rel);
                    obuf[j] += s * env * g;
                    ph = wrap1(ph + inc);
                }
                perc = 0.0f;
            }
            if (v->gate > 0)
                v->gate -= (int32_t)seg;
            j0 += seg;
        }
        v->ph = ph, v->env = env, v->perc = perc;
        if (v->gate == 0 && env < 0.0005f)
            v->on = 0;
        else if (v->gate != 0 && v->note >= 36 && v->note < 100)
            keys[v->man][(v->note - 36) >> 5] |= 1u << ((v->note - 36) & 31u);
    }
    en_keys_up[0] = keys[0][0];
    en_keys_up[1] = keys[0][1];
    en_keys_lo[0] = keys[1][0];
    en_keys_lo[1] = keys[1][1];
    /* the key click: a tick of noise, bright */
    if (click_e > 0.001f) {
        float fc = 2.0f * fm_sinf(FM_PI * 2400.0f / EN_SR), cd = 0.9935f, lv = 0.16f * fade;
        for (j = 0; j < n; j++) {
            float x = noise() * click_e, hp = x - click_lo - 0.6f * click_bd;
            click_lo += fc * click_bd;
            click_bd += fc * hp;
            obuf[j] += click_bd * lv;
            click_e *= cd;
        }
    }
    /* the bass: three harmonics of the 8' and the pedal's thump (heard on a small speaker too) */
    for (j = 0; j < n; j++)
        bbuf[j] = 0.0f;
    for (i = 0; i < 2; i++) {
        float ph = bv[i].ph, inc = bv[i].inc, env = bv[i].env, th = bv[i].thump, tgt;
        uint32_t seg, j0 = 0;
        if (!bv[i].on)
            continue;
        while (j0 < n) {
            tgt = bv[i].gate == 0 ? 0.0f : bv[i].tgt;
            seg = n - j0;
            if (bv[i].gate > 0 && (uint32_t)bv[i].gate < seg)
                seg = (uint32_t)bv[i].gate;
            for (j = j0; j < j0 + seg; j++) {
                float s = sn(ph) * (1.0f + 0.7f * th) + 0.5f * sn(ph * 2.0f) + 0.22f * sn(ph * 3.0f);
                env += (tgt - env) * (tgt > env ? 0.012f : 0.0022f);
                bbuf[j] += s * env;
                th *= 0.9993f;
                ph = wrap1(ph + inc);
            }
            if (bv[i].gate > 0)
                bv[i].gate -= (int32_t)seg;
            j0 += seg;
        }
        bv[i].ph = ph, bv[i].env = env, bv[i].thump = th;
        if (bv[i].gate == 0 && env < 0.0005f)
            bv[i].on = 0;
    }
    /* the swell pedal */
    if (expr != expr_tg) {
        float st = expr_rate * (float)n;
        expr = expr < expr_tg ? fm_minf(expr + st, expr_tg) : fm_maxf(expr - st, expr_tg);
    }
    en_expr = expr;
    /* the scanner chorus, the preamp's drive, then the speaker */
    {
        int vib = STYLES[style_i].vib;
        float d = pf(P_DRIVE), pre = 0.9f + 4.0f * d * d * (0.7f + 0.3f * (float)en_climax), post, lvo, lvb;
        float a = onepole(800.0f), hs, hc, ds, dc, ht, dt;
        float hinc = horn_hz / EN_SR, dinc = drum_hz / EN_SR, vinc = 6.9f / EN_SR;
        post = 1.0f / (0.55f + 0.45f * pre);
        lvo = pf(P_ORGAN) * 0.19f * expr;
        lvb = pf(P_ORGAN) * 0.19f * fade;
        for (j = 0; j < n; j++) {
            float x = obuf[j] * lvo, y, lo, hi, rl, rr, dl;
            int32_t k;
            if (vib) {                           /* a short line swept at 6.9 Hz: chorus (half dry) or vibrato */
                float dd = 14.0f + 9.0f * sn(vib_ph), rd = (float)vib_w - dd, fr;
                vib_buf[vib_w] = x;
                vib_w = (vib_w + 1) & (VN - 1);
                if (rd < 0.0f)
                    rd += (float)VN;
                k = (int32_t)rd;
                fr = rd - (float)k;
                y = vib_buf[k & (VN - 1)] + (vib_buf[(k + 1) & (VN - 1)] - vib_buf[k & (VN - 1)]) * fr;
                x = vib == 1 ? 0.5f * (x + y) : y;
                vib_ph = wrap1(vib_ph + vinc);
            }
            x *= pre;                            /* the preamp: a soft clip (a rational tanh) */
            x = x > 3.0f ? 3.0f : x < -3.0f ? -3.0f : x;
            x = x * (27.0f + x * x) / (27.0f + 9.0f * x * x) * post;
            /* the crossover: the drum takes the lows (and the bass), the horn the rest */
            lsl_lp += (x - lsl_lp) * a;
            hi = x - lsl_lp;
            lo = lsl_lp + bbuf[j] * lvb;
            horn_buf[horn_w] = hi;
            hs = sn(horn_ph);
            hc = sn(horn_ph + 0.25f);
            ds = sn(drum_ph);
            dc = sn(drum_ph + 0.25f);
            /* the horn heard by two mics a quarter turn apart: its distance (pitch) and its facing (level) */
            ht = 22.0f - 13.0f * hc;
            dl = (float)horn_w - ht;
            if (dl < 0.0f)
                dl += (float)HN;
            k = (int32_t)dl;
            rl = horn_buf[k & (HN - 1)] + (horn_buf[(k + 1) & (HN - 1)] - horn_buf[k & (HN - 1)]) * (dl - (float)k);
            dt = 22.0f - 13.0f * hs;
            dl = (float)horn_w - dt;
            if (dl < 0.0f)
                dl += (float)HN;
            k = (int32_t)dl;
            rr = horn_buf[k & (HN - 1)] + (horn_buf[(k + 1) & (HN - 1)] - horn_buf[k & (HN - 1)]) * (dl - (float)k);
            horn_w = (horn_w + 1) & (HN - 1);
            rl *= 0.62f + 0.38f * hs;
            rr *= 0.62f - 0.38f * hc;
            bufl[j] += rl + lo * (0.86f + 0.14f * ds);
            bufr[j] += rr + lo * (0.86f - 0.14f * dc);
            rsend[j] += (rl + rr) * 0.3f + lo * 0.12f;
            horn_ph = wrap1(horn_ph + hinc);
            drum_ph = wrap1(drum_ph + dinc);
        }
    }
    en_horn_ph = horn_ph;
    en_drum_ph = drum_ph;
}

static float gl_lp, gl_lp2, gl_bp_lo, gl_bp_bd, gl_hp;
static void render_guitar(uint32_t n)
{
    uint32_t j;
    int s, any = 0;
    float lv = pf(P_GUITAR) * 0.85f * fade, a = onepole(STYLES[style_i].pick ? 3200.0f : 2300.0f), hpa = onepole(70.0f);
    float fb_body = 2.0f * fm_sinf(FM_PI * 190.0f / EN_SR);
    for (j = 0; j < n; j++)
        bbuf[j] = 0.0f;
    for (s = 0; s < GS; s++) {
        float c, fb, d, lp;
        int w;
        if (!gs[s].on)
            continue;
        any = 1;
        if (gs[s].gate > 0) {
            gs[s].gate -= (int32_t)n;
            if (gs[s].gate <= 0)                 /* the note's end: the finger eases off */
                gs[s].fb = fm_minf(gs[s].fb, fm_expf(-6.9f / (0.09f * gs[s].f)));
        }
        c = gs[s].c, fb = gs[s].fb, d = gs[s].d, lp = gs[s].lp, w = gs[s].w;
        for (j = 0; j < n; j++) {
            float rd = (float)w - d, fr, y;
            int ri;
            if (rd < 0.0f)
                rd += (float)GN;
            ri = (int)rd;
            fr = rd - (float)ri;
            y = g_buf[s][ri & (GN - 1)] + (g_buf[s][(ri + 1) & (GN - 1)] - g_buf[s][ri & (GN - 1)]) * fr;
            lp += (y - lp) * c;
            g_buf[s][w] = lp * fb;
            w = (w + 1) & (GN - 1);
            bbuf[j] += y;
        }
        gs[s].lp = lp, gs[s].w = w;
        if (fb < 0.9995f && fm_fabsf(lp) < 2e-5f && gs[s].gate <= 0)
            gs[s].on = 0;
    }
    if (!any && fm_fabsf(gl_lp2) < 1e-6f)
        return;
    for (j = 0; j < n; j++) {                    /* the archtop's body (a low hollow resonance) and the amp's warmth */
        float x = bbuf[j], hp, y;
        hp = x - gl_bp_lo - 1.2f * gl_bp_bd;
        gl_bp_lo += fb_body * gl_bp_bd;
        gl_bp_bd += fb_body * hp;
        x += gl_bp_bd * 0.35f;
        gl_lp += (x - gl_lp) * a;
        gl_lp2 += (gl_lp - gl_lp2) * a;
        gl_hp += (gl_lp2 - gl_hp) * hpa;
        y = (gl_lp2 - gl_hp) * lv;
        y = y * (27.0f + y * y) / (27.0f + 9.0f * y * y);
        bufl[j] += y * 0.95f;                    /* the guitar: stage left */
        bufr[j] += y * 0.5f;
        rsend[j] += y * 0.3f;
    }
}

static void render_drums(uint32_t n)
{
    uint32_t j;
    float lv = pf(P_DRUMS) * 0.65f * fade;
    float fs = 2.0f * fm_sinf(FM_PI * 2100.0f / EN_SR), fh = 2.0f * fm_sinf(FM_PI * 3600.0f / EN_SR);
    float fb = 2.0f * fm_sinf(FM_PI * 3000.0f / EN_SR);
    float hpa = onepole(3200.0f), lpa = onepole(10500.0f), slp = onepole(6000.0f), blp = onepole(5200.0f);
    float wdec = tau(1.7f), pdec = tau(0.05f), bdec = tau(0.55f), cdec = tau(1.5f);
    float bi1 = 1210.0f / EN_SR, bi2 = 2870.0f / EN_SR, ri = 1650.0f / EN_SR, rbi = 420.0f / EN_SR;
    float sw = sweep_lvl * (0.35f + 0.65f * sweep_shape());
    if (ride.wash < 1e-4f && ride.ping < 1e-4f && ride.bell < 1e-4f && hat.amp < 1e-4f && crash.amp < 1e-4f &&
        snare.tamp < 1e-4f && snare.namp < 1e-4f && kick.amp < 1e-4f && tom[0].amp < 1e-4f &&
        tom[1].amp < 1e-4f && brush.amp < 1e-4f && sw < 1e-4f && stick.amp < 1e-4f && rim.amp < 1e-4f)
        return;
    for (j = 0; j < n; j++) {
        float metal = 0.0f, x, cym = 0.0f, k = 0.0f, s = 0.0f, h = 0.0f, t = 0.0f, br = 0.0f;
        int i;
        if (ride.wash > 1e-4f || hat.amp > 1e-4f || crash.amp > 1e-4f) {
            for (i = 0; i < 6; i++) {            /* six squares, out of tune with each other: metal */
                ride.ph[i] += ride.inc[i];
                if (ride.ph[i] >= 1.0f)
                    ride.ph[i] -= 1.0f;
                metal += ride.ph[i] < 0.5f ? 1.0f : -1.0f;
            }
            metal *= 1.0f / 6.0f;
            x = metal * 0.55f + noise() * 0.45f;
            ride.h1 += (x - ride.h1) * hpa;      /* two high-passes, a low-pass */
            x -= ride.h1;
            ride.h2 += (x - ride.h2) * hpa;
            x -= ride.h2;
            ride.lp += (x - ride.lp) * lpa;
            cym = ride.lp;
        }
        if (ride.wash > 1e-4f) {
            h += cym * (ride.wash * 0.30f + ride.ping * 0.85f);
            ride.wash *= wdec;
            ride.ping *= pdec;
        }
        if (ride.bell > 1e-4f) {                 /* the bell: two sines */
            h += (sn(ride.bph1) + 0.6f * sn(ride.bph2)) * ride.bell * 0.16f;
            ride.bph1 = wrap1(ride.bph1 + bi1);
            ride.bph2 = wrap1(ride.bph2 + bi2);
            ride.bell *= bdec;
        }
        if (crash.amp > 1e-4f) {
            t += cym * crash.amp * 0.55f;
            crash.amp *= cdec;
        }
        if (hat.amp > 1e-4f) {                   /* the hat: a chick, its band of noise and the metal */
            float y = noise() * 0.6f + cym * 1.2f, hp;
            hat.env = fm_minf(hat.env + 1.0f / (0.0015f * EN_SR), 1.0f);
            hat.lo += fh * hat.bd;
            hp = y - hat.lo - 0.8f * hat.bd;
            hat.bd += fh * hp;
            t += (hat.bd * 0.5f + hp * 0.3f) * hat.amp * hat.env * 0.8f;
            hat.amp *= hat.dec;
        }
        if (kick.amp > 1e-4f) {
            kick.f = kick.fend + (kick.f - kick.fend) * kick.pdec;
            kick.ph = wrap1(kick.ph + kick.f / EN_SR);
            k = sn(kick.ph) * kick.amp + noise() * kick.click;
            kick.amp *= kick.adec;
            kick.click *= 0.9f;
        }
        if (snare.tamp > 1e-4f || snare.namp > 1e-4f) {
            float hp;
            x = noise() * snare.namp;
            snare.lo += fs * snare.bd;           /* Chamberlin's state-variable filter: its band, a little high */
            hp = x - snare.lo - 0.9f * snare.bd;
            snare.bd += fs * hp;
            snare.lp += (snare.bd + 0.4f * hp - snare.lp) * slp;
            s += snare.lp * 0.85f + sn(snare.ph) * snare.tamp;
            snare.ph = wrap1(snare.ph + snare.inc);
            snare.tamp *= snare.tdec;
            snare.namp *= snare.ndec;
        }
        if (rim.amp > 1e-4f) {                   /* the cross-stick: a woody click, the head's short knock */
            s += (sn(rim.ph) * 0.5f + noise() * 0.25f) * rim.amp + sn(rim.bph) * rim.bamp * 0.5f;
            rim.ph = wrap1(rim.ph + ri);
            rim.bph = wrap1(rim.bph + rbi);
            rim.amp *= 0.9925f;
            rim.bamp *= 0.9975f;
        }
        for (i = 0; i < 2; i++)
            if (tom[i].amp > 1e-4f) {
                tom[i].f = tom[i].fend + (tom[i].f - tom[i].fend) * 0.9996f;
                tom[i].ph = wrap1(tom[i].ph + tom[i].f / EN_SR);
                s += sn(tom[i].ph) * tom[i].amp * 0.8f + noise() * tom[i].namp * 0.15f;
                tom[i].amp *= tom[i].adec;
                tom[i].namp *= 0.995f;
            }
        if (brush.amp > 1e-4f || sw > 1e-4f) {   /* brushes: a slap, and the circling sweep */
            float y = noise(), hp, a;
            brush.att = fm_minf(brush.att + 1.0f / (0.004f * EN_SR), 1.0f);
            a = brush.amp * brush.att;
            brush.lo += fb * brush.bd;
            hp = y - brush.lo - 1.1f * brush.bd;
            brush.bd += fb * hp;
            brush.lp += (hp * a + brush.bd * sw * 0.8f - brush.lp) * blp;
            br = brush.lp * 0.9f;
            brush.amp *= 0.99965f;
        }
        if (stick.amp > 1e-4f) {                 /* sticks clicked together: the count */
            t += (sn(stick.ph) * 0.6f + noise() * 0.4f) * stick.amp * 0.5f;
            stick.ph = wrap1(stick.ph + 2350.0f / EN_SR);
            stick.amp *= 0.9965f;
        }
        /* the kit, from the floor: in the middle, a little right; the ride further right, the hat to the left */
        bufl[j] += (k * 0.8f + s * 0.72f + br * 0.75f + h * 0.55f + t * 0.95f) * lv;
        bufr[j] += (k * 0.8f + s * 0.78f + br * 0.8f + h * 1.0f + t * 0.7f) * lv;
        rsend[j] += (s * 0.35f + h * 0.3f + t * 0.35f + br * 0.3f + k * 0.08f) * lv;
    }
}

static void render_crowd(uint32_t n)
{
    uint32_t j;
    int i;
    float cr = pf(P_CROWD), lv = cr * 0.20f, ml, fc;
    if (cr <= 0.0f || !en_playing)
        return;
    /* the room murmurs a little: louder between tunes */
    ml = cr * (en_phase == PH_TUNE ? 0.0045f : 0.010f) * (0.75f + 0.25f * sn(murm_ph));
    murm_ph = wrap1(murm_ph + 0.13f * (float)n / EN_SR);
    fc = onepole(520.0f);
    for (j = 0; j < n; j++) {
        float x = noise(), y;
        murm_lp += (x - murm_lp) * fc;
        murm_hp += (murm_lp - murm_hp) * 0.012f;
        y = (murm_lp - murm_hp) * ml;
        bufl[j] += y;
        bufr[j] += y * 0.9f;
    }
    /* applause: claps, each a burst of noise in its own band, at the room's rate */
    clap_next -= (float)n;
    while (appl_lvl > 0.01f && clap_next <= 0.0f) {
        for (i = 0; i < NCLAP; i++)
            if (clap[i].amp < 0.02f)
                break;
        if (i < NCLAP) {
            float p = frand() * 1.6f - 0.8f;
            clap[i].amp = (0.45f + 0.55f * frand()) * appl_lvl;
            clap[i].dec = tau(0.010f + 0.010f * frand());
            clap[i].f = 2.0f * fm_sinf(FM_PI * (900.0f + 1500.0f * frand()) / EN_SR);
            clap[i].gl = 0.75f - 0.5f * p;
            clap[i].gr = 0.75f + 0.5f * p;
        }
        clap_next += EN_SR / (8.0f + 34.0f * appl_lvl) * (0.3f + 1.4f * frand());
    }
    for (i = 0; i < NCLAP; i++) {
        float a = clap[i].amp, d = clap[i].dec, f = clap[i].f, lo = clap[i].lo, bd = clap[i].bd;
        if (a < 1e-4f)
            continue;
        for (j = 0; j < n; j++) {
            float x = noise() * a, hp = x - lo - 0.7f * bd, y;
            lo += f * bd;
            bd += f * hp;
            y = bd * lv;
            bufl[j] += y * clap[i].gl;
            bufr[j] += y * clap[i].gr;
            rsend[j] += y * 0.5f;
            a *= d;
        }
        clap[i].amp = a, clap[i].lo = lo, clap[i].bd = bd;
    }
    /* a glass now and then */
    clink_next -= (float)n;
    if (clink_next <= 0.0f) {
        float p = frand() * 1.4f - 0.7f;
        clink.amp = (0.3f + 0.4f * frand()) * (en_phase == PH_TUNE ? 0.4f : 1.0f);
        clink.i1 = (2600.0f + 900.0f * frand()) / EN_SR;
        clink.i2 = clink.i1 * (1.52f + 0.1f * frand());
        clink.gl = 0.75f - 0.5f * p;
        clink.gr = 0.75f + 0.5f * p;
        clink_next = EN_SR * (en_phase == PH_TUNE ? 22.0f + 30.0f * frand() : 3.0f + 6.0f * frand());
    }
    if (clink.amp > 1e-4f) {
        float d = tau(0.22f);
        for (j = 0; j < n; j++) {
            float y = (sn(clink.ph1) + 0.5f * sn(clink.ph2)) * clink.amp * lv * 0.06f;
            clink.ph1 = wrap1(clink.ph1 + clink.i1);
            clink.ph2 = wrap1(clink.ph2 + clink.i2);
            clink.amp *= d;
            bufl[j] += y * clink.gl;
            bufr[j] += y * clink.gr;
            rsend[j] += y * 0.6f;
        }
    }
}

static void render_space(uint32_t n)
{
    uint32_t j;
    float room = pf(P_ROOM), vs = 0.16f + 0.42f * room, damp = 0.30f + 0.30f * (1.0f - room), g = 0.60f + 0.27f * room;
    for (j = 0; j < n; j++) {
        float in = rsend[j] * vs, x[4], sum, yl, yr;
        int i;
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
        bufl[j] += yl * 0.9f;
        bufr[j] += yr * 0.9f;
    }
}

/* the bus: toned (Tone), the rumble cut, levelled; MASTER */
static void render_bus(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    float fc, ta, lv_rel = tau(0.5f), lv_att = 1.0f - tau(0.002f), hp_a = onepole(24.0f), vol, x;
    fc = 2600.0f * fm_exp2f(pf(P_TONE) * 2.6f);  /* 2.6 .. ~16 kHz */
    fc = fm_clampf(fc, 800.0f, 16000.0f);
    ta = onepole(fc);
    tone_a += (ta - tone_a) * 0.2f;
    for (j = 0; j < n; j++) {
        float l = bufl[j], r = bufr[j];
        t1l += (l - t1l) * tone_a;
        t2l += (t1l - t2l) * tone_a;
        t1r += (r - t1r) * tone_a;
        t2r += (t1r - t2r) * tone_a;
        l = t2l, r = t2r;
        hp_l += (l - hp_l) * hp_a;
        hp_r += (r - hp_r) * hp_a;
        l -= hp_l;
        r -= hp_r;
        vu_acc_l += l * l;                       /* the VU reads the music, not MASTER */
        vu_acc_r += r * r;
        x = fm_maxf(fm_fabsf(l), fm_fabsf(r)) * gain;
        lev = x > lev ? lev + (x - lev) * lv_att : lev * lv_rel;
        vol = lev > 0.55f ? 0.55f / lev : 1.0f;  /* the leveller: a quick attack, a slow release */
        l *= gain * vol;
        r *= gain * vol;
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
    }
}

static uint32_t reg_acc;
static void control(uint32_t n)
{
    float k = (float)n / EN_SR, ht, dt;
    int sp = par[P_SPEAKER], fast = sp == 2 || (sp == 1 && lsl_fast && en_playing);
    if (++vu_n >= 8u) {                          /* the VU: about 46 ms of RMS, then ballistics */
        float l = fm_sqrtf(vu_acc_l / (float)(n * 8u)), r = fm_sqrtf(vu_acc_r / (float)(n * 8u));
        en_vu_l = en_vu_l + (fm_minf(l * 4.0f, 1.2f) - en_vu_l) * 0.45f;
        en_vu_r = en_vu_r + (fm_minf(r * 4.0f, 1.2f) - en_vu_r) * 0.45f;
        vu_acc_l = vu_acc_r = 0.0f;
        vu_n = 0;
    }
    /* the rotors: the light horn gets there in about a second, the heavy drum takes four or five */
    ht = fast ? 6.8f : 0.8f;
    dt = fast ? 5.9f : 0.68f;
    horn_hz += (ht - horn_hz) * fm_minf(k * (fast ? 1.5f : 1.1f), 1.0f);
    drum_hz += (dt - drum_hz) * fm_minf(k * (fast ? 0.32f : 0.24f), 1.0f);
    en_horn_hz = horn_hz;
    en_perc_on = (uint8_t)(par[P_PERC] == 1 ? 2 : par[P_PERC] ? 3 : 0);
    reg_acc += n;
    if (reg_acc >= 256u) {                       /* the drawbars: a step every block or so */
        reg_move((float)reg_acc / EN_SR);
        reg_acc = 0;
    }
    applause_tick(k);
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 1.5f;
    drain();
    while (n) {
        uint32_t k = n > BLK ? BLK : n, j;
        int i, run = 0;
        if (en_playing) {
            if (en_phase == PH_TUNE) {
                if (skipping && fade < 0.002f) {  /* stopped: on to the gap */
                    skipping = 0;
                    for (i = 0; i < NEV; i++)
                        evq[i].used = 0;
                    quiet();
                    en_phase = PH_GAP;
                    gap_left = EN_SR * 1.4f;
                    fade = 1.0f;
                    fade_t = 1.0f;
                    lsl_fast = 0;
                } else {
                    if (beat_left <= 0.0f) {
                        seq_beat();
                        beat_left += beat_len;
                    }
                    if (en_phase == PH_TUNE) {
                        run = 1;
                        if ((float)k > beat_left)
                            k = (uint32_t)beat_left + 1u;
                    }
                }
            } else {
                between(&k);
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
        if (k < 1)
            k = 1;
        for (i = 0; i < NEV; i++)
            if (evq[i].used)
                evq[i].wait -= (int32_t)k;
        if (run)
            beat_left -= (float)k;
        control(k);
        for (j = 0; j < k; j++)
            bufl[j] = bufr[j] = rsend[j] = 0.0f;
        fade = fade < fade_t ? fm_minf(fade + fade_rate * (float)k, fade_t) : fm_maxf(fade - fade_rate * (float)k, fade_t);
        render_organ(k);
        render_guitar(k);
        render_drums(k);
        render_crowd(k);
        render_space(k);
        render_bus(out, k, gain);
        out += 2 * k;
        n -= k;
    }
}
