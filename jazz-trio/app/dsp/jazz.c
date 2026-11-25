/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * JAZZ TRIO: ashtonwallen, 2026 */
/* The instrument (jazz.h). One compilation unit, -O2: this file, the harmony (jazz_harm.c), the tunes (jazz_tune.c)
 * and the players (jazz_play.c). The command queue and the render's shape are AMB-1's. */
#include "jazz.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Tempo", 0, 100, 50}, {"Intensity", 0, 100, 50}, {"Room", 0, 100, 45}, {"Swing", 0, 100, 50},
    {"Piano", 0, 100, 75}, {"Bass", 0, 100, 75},       {"Drums", 0, 100, 70}, {"Crowd", 0, 100, 50},
    {"Tone", 0, 100, 55},  {"Focus", 0, 100, 50},      {"Stretch", 0, 3, 1},
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
    if (i == P_TEMPO) {                           /* the band's feel for time: slower .. faster than the tune's */
        put_s(buf, v < 20 ? "Laid back" : v < 40 ? "Easy" : v < 61 ? "Natural" : v < 80 ? "Pushed" : "Burning");
    } else if (i == P_SWING) {
        put_s(buf, v < 20 ? "Straight" : v < 40 ? "Even" : v < 61 ? "Natural" : v < 80 ? "Deep" : "Heavy");
    } else if (i == P_FOCUS) {                    /* who gets the solos: the piano .. the bass and drums */
        put_s(buf, v < 25 ? "Piano" : v < 50 ? "Mostly piano" : v < 75 ? "Shared" : "Bass & drums");
    } else if (i == P_STRETCH) {
        static const char *const S[4] = {"Short", "Normal", "Long", "Marathon"};
        put_s(buf, S[v < 0 ? 0 : v > 3 ? 3 : v]);
    } else {
        put_u(buf, (uint32_t)(v < 0 ? 0 : v));
    }
}

/* ------------------------------------------------------------ styles --- */
/* the user's ten (Tempo Intensity Room Swing, Piano Bass Drums Crowd, Tone Focus) and the band's own flavour:
 * w: how often each kind of tune comes up (medium, up, ballad, blues, bird blues, minor blues, rhythm, modal, sus,
 * waltz); touch: the piano (0 warm grand, 1 bright grand, 2 dark and soft, 3 an old upright); brush: 0 sticks,
 * 1 brushes on ballads, 2 on heads and ballads, 3 throughout; comp: the left hand (0 sparse, 1 even, 2 busy);
 * lines: how dense the solos run; harm: reharmonization (tritone subs, split dominants, altered); shells: shells
 * under the solos (%); tempo: the set's tempos (%) */
typedef struct {
    const char *name;
    int8_t v[P_NSTYLE];
    uint8_t w[K_NKINDS];
    uint8_t touch, brush, comp, lines, harm, shells, tempo;
} style_t;
static const style_t STYLES[EN_NSTYLES] = {
    /*                     Tem Int Roo Swi  Pia Bas Drm Crw  Ton Foc    Me Up Ba Bl Bi Mi Rh Mo Su Wa   tch br cmp lin har sh tmp */
    {"Cover Charge",      {50, 50, 45, 50, 75, 75, 70, 50, 55, 50}, {7, 3, 3, 5, 2, 3, 2, 2, 1, 3}, 0, 1, 1, 55, 30, 10, 100},
    {"Spang-a-Lang",      {56, 62, 40, 52, 75, 75, 75, 45, 60, 50}, {5, 6, 1, 5, 3, 3, 5, 1, 0, 1}, 1, 0, 1, 70, 35, 30, 104},
    {"Brush Fire",        {58, 55, 45, 48, 72, 75, 68, 45, 55, 55}, {5, 6, 2, 4, 2, 3, 4, 2, 1, 2}, 0, 3, 1, 62, 30, 10, 104},
    {"Whisper Pete",      {35, 30, 62, 55, 75, 70, 55, 40, 40, 35}, {3, 0, 9, 2, 0, 2, 0, 1, 1, 5}, 2, 2, 0, 30, 40, 0, 90},
    {"Dorian Gray",       {52, 58, 55, 45, 72, 78, 72, 40, 50, 55}, {2, 1, 2, 1, 0, 6, 0, 9, 7, 2}, 1, 0, 1, 60, 20, 0, 100},
    {"Bird Feeder",       {64, 66, 38, 45, 75, 72, 75, 45, 62, 55}, {2, 5, 1, 4, 9, 2, 7, 0, 0, 0}, 1, 0, 0, 85, 40, 70, 108},
    {"Teacup Ride",       {48, 45, 55, 55, 75, 72, 62, 45, 50, 50}, {3, 1, 4, 1, 0, 1, 0, 1, 1, 9}, 0, 2, 1, 45, 30, 0, 100},
    {"Walking Papers",    {48, 52, 45, 52, 66, 90, 68, 50, 50, 85}, {7, 2, 2, 5, 1, 3, 1, 2, 1, 2}, 0, 1, 1, 45, 25, 0, 96},
    {"Tritone Taxi",      {52, 55, 48, 50, 78, 72, 68, 45, 65, 50}, {8, 3, 3, 3, 3, 2, 2, 0, 0, 2}, 1, 1, 2, 60, 90, 0, 100},
    {"Elbow Grease",      {64, 80, 40, 52, 76, 78, 80, 55, 62, 80}, {4, 7, 1, 5, 3, 6, 4, 2, 0, 0}, 1, 0, 2, 80, 35, 20, 106},
    {"Stools Up",         {28, 25, 64, 58, 72, 68, 50, 25, 35, 40}, {2, 0, 9, 4, 0, 2, 0, 1, 2, 3}, 2, 3, 0, 25, 35, 0, 84},
    {"Napkin Chart",      {50, 55, 25, 55, 75, 75, 70, 55, 58, 55}, {5, 2, 2, 6, 1, 4, 2, 1, 1, 2}, 3, 0, 0, 45, 25, 40, 98},
    {"Rootless Folk",     {45, 45, 52, 52, 75, 75, 62, 45, 52, 60}, {8, 1, 4, 2, 0, 2, 0, 2, 1, 5}, 0, 2, 1, 45, 50, 0, 96},
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
    {"Medium swing", 120, 168}, {"Up tempo", 184, 240}, {"Ballad", 52, 72},     {"Blues", 104, 150},
    {"Bird blues", 168, 224},   {"Minor blues", 126, 186}, {"Rhythm changes", 200, 260}, {"Modal", 146, 208},
    {"Sus vamp", 108, 148},     {"Jazz waltz", 132, 180},
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
volatile uint8_t en_playing, en_phase, en_repeat_on, en_role, en_soloist, en_brushes;
volatile uint32_t en_tune_no, en_tune_seed, en_seed_base = 0x4A415A5Au, en_tunes_played, en_beats;
volatile uint8_t en_kind, en_key, en_minor, en_chord_root, en_chord_q, en_bar, en_form_bars, en_chorus, en_nchorus;
volatile uint8_t en_beat, en_bpb = 4;
volatile uint16_t en_bpm = 120, en_chord_pcs;
volatile uint8_t en_hits[NHITS];
volatile uint8_t en_rh_note = 72, en_lh_note = 60, en_bass_note = 40;
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

#include "jazz_harm.c"

/* --------------------------------------------------------------- piano --- */
/* FM: a carrier modulated by itself (ratio 1) with a falling index (the hammer's brightness mellowing), its
 * octave partial a little stretched, a hammer's knock; two decays (the prompt sound, the long aftersound) */
#define NPV 14
typedef struct {
    float pc, p2, ic, i2;
    float a1, a2, d1, d2, idx, idx_dec, idx_floor, part, part_dec, ham, rel;
    float gl, gr;
    int32_t gate;                                /* samples until the damper; < 0: held by the player */
    uint8_t note, on, player;
    uint32_t age;
} pv_t;
static pv_t pv[NPV];
static uint32_t pv_clock;

static void piano_on(int note, float vel, int32_t gate, int player)
{
    pv_t *v = 0;
    int i, touch = STYLES[style_i].touch;
    float f, kt, td;
    uint32_t oldest = 0;
    for (i = 0; i < NPV; i++)                    /* a voice on the same key (re-struck), a free one, the oldest */
        if (pv[i].on && pv[i].note == note && !pv[i].player) {
            v = &pv[i];
            break;
        }
    if (!v)
        for (i = 0; i < NPV; i++)
            if (!pv[i].on) {
                v = &pv[i];
                break;
            }
    if (!v)
        for (i = 0; i < NPV; i++) {
            uint32_t a = pv_clock - pv[i].age + (pv[i].gate == 0 ? 0x10000000u : 0u);
            if (!pv[i].player && a >= oldest) {
                oldest = a;
                v = &pv[i];
            }
        }
    if (!v)
        v = &pv[0];
    while (note < 28)
        note += 12;
    while (note > 100)
        note -= 12;
    f = mtof((float)note);
    vel = fm_clampf(vel, 0.05f, 1.0f);
    kt = fm_clampf(1.0f - (float)(note - 60) * 0.012f, 0.45f, 1.35f);   /* low notes brighter in FM terms */
    td = fm_clampf(fm_sqrtf(261.6f / f), 0.45f, 2.2f);
    v->ic = f / EN_SR;
    v->i2 = f * (touch == 3 ? 2.0f * 1.0042f : 2.0f * 1.0011f) / EN_SR;
    v->pc = v->p2 = 0.0f;
    if (touch == 1) {                            /* bright */
        v->idx = (0.55f + 1.15f * vel) * kt;
        v->idx_floor = 0.20f * kt;
        v->part = 0.22f * vel;
    } else if (touch == 2) {                     /* dark, soft */
        v->idx = (0.25f + 0.55f * vel) * kt;
        v->idx_floor = 0.06f * kt;
        v->part = 0.10f * vel;
    } else {                                     /* warm (and the old upright) */
        v->idx = (0.40f + 0.90f * vel) * kt;
        v->idx_floor = 0.12f * kt;
        v->part = (touch == 3 ? 0.26f : 0.16f) * vel;
    }
    v->idx_dec = tau(0.42f * td);
    v->part_dec = tau(0.5f * td);
    v->ham = 0.05f * vel * vel;
    vel = vel * fm_sqrtf(vel);
    v->a1 = vel * 0.55f;
    v->a2 = vel * 0.45f;
    v->d1 = tau(0.28f * td);
    v->d2 = tau(fm_clampf(3.4f * td, 0.9f, 7.0f));
    v->rel = tau(0.085f);
    {
        float p = fm_clampf((float)(note - 64) * 0.018f, -0.35f, 0.35f) - 0.22f;   /* the piano: stage left */
        v->gl = 0.75f - 0.9f * p;
        v->gr = 0.75f + 0.9f * p;
    }
    v->gate = gate;
    v->note = (uint8_t)note;
    v->on = 1;
    v->player = (uint8_t)player;
    v->age = pv_clock++;
}
static void piano_off_player(int note)
{
    int i;
    for (i = 0; i < NPV; i++)
        if (pv[i].on && pv[i].player && pv[i].note == note && pv[i].gate < 0)
            pv[i].gate = 0;
}

/* ---------------------------------------------------------------- bass --- */
/* a plucked string (Karplus-Strong): two strings in turn, the last one damped as the next is plucked */
#define KS_N 2048
static float ks_buf[2][KS_N] EN_POOL;
static struct {
    float d, fb, c, lp, amp;
    int32_t gate;
    int w;
    uint8_t on;
    float f;
} ks[2];
static int ks_cur;
static float bass_lp, bass_hp;

static void bass_on(int note, float vel, int32_t gate)
{
    int s = ks_cur ^ 1, i, L;
    float f, c, e = 0.0f, d0;
    if (ks[ks_cur].on)                           /* the last note: damped by the fingers */
        ks[ks_cur].fb = fm_expf(-6.9f / (0.06f * ks[ks_cur].f));
    ks_cur = s;
    while (note < 28)
        note += 12;
    while (note > 67)
        note -= 12;
    f = mtof((float)note);
    vel = fm_clampf(vel, 0.05f, 1.0f);
    c = 0.30f + 0.22f * vel;                     /* the loop's low-pass: a harder pluck rings brighter */
    ks[s].f = f;
    ks[s].c = c;
    ks[s].d = EN_SR / f - (1.0f - c) / c;
    if (ks[s].d > (float)(KS_N - 4))
        ks[s].d = (float)(KS_N - 4);
    ks[s].fb = fm_expf(-6.9f / (fm_clampf(2.6f - (float)(note - 40) * 0.03f, 1.2f, 3.2f) * f));
    ks[s].gate = gate;
    ks[s].amp = vel;
    ks[s].lp = 0.0f;
    ks[s].on = 1;
    L = (int)ks[s].d + 2;
    d0 = 3.14159265f / (float)L;
    for (i = 0; i < L; i++) {                    /* the excitation: a soft bump (the finger's flesh), a little grit */
        float bump = sn((float)i * d0 * (1.0f / FM_TWO_PI)) * 0.85f;
        e += (noise() - e) * 0.18f;
        ks_buf[s][(ks[s].w - 1 - i) & (KS_N - 1)] = (bump + e * 0.5f) * vel;
    }
    en_bass_note = (uint8_t)note;
    en_hits[HIT_BASS]++;
}

/* --------------------------------------------------------------- drums --- */
static struct {
    float ph[6], inc[6];
    float wash, ping, bell, bph1, bph2, h1, h2, lp, roll;
} ride;
static struct {
    float amp, env, lo, bd;
} hat;
static struct {
    float amp, h1, lp;
} crash;
static struct {
    float ph, inc, tamp, tdec, namp, ndec, lo, bd, lp;
} snare;
static struct {
    float ph, f, fend, pdec, amp, adec, click;
} kick;
static struct {
    float ph, f, fend, amp, adec, namp;
} tom[2];
static struct {
    float amp, att, lp, lo, bd, sweep, sw_lp;
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
static void hat_hit(float v)
{
    hat.amp = v;
    hat.env = 0.0f;
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
static void kick_hit(float v)
{
    kick.ph = 0.0f;
    kick.f = 105.0f;
    kick.fend = 60.0f + 4.0f * frand();
    kick.pdec = tau(0.022f);
    kick.amp = v;
    kick.adec = tau(0.20f);
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

/* -------------------------------------------------------- the schedule --- */
#define BLK 256
#define NEV 128
enum { I_RH, I_LH, I_BASS, I_RIDE, I_HAT, I_SNARE, I_KICK, I_TOM, I_CRASH, I_BRUSH, I_STICK, I_BELL, I_NI };
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
        piano_on(e->note, v, e->gate, 0);
        en_rh_note = e->note;
        en_hits[HIT_RH]++;
        break;
    case I_LH:
        piano_on(e->note, v, e->gate, 0);
        en_lh_note = e->note;
        en_hits[HIT_LH]++;
        break;
    case I_BASS: bass_on(e->note, v, e->gate); break;
    case I_RIDE: ride_hit(v, 0); break;
    case I_BELL: ride_hit(v, 1); break;
    case I_HAT: hat_hit(v); break;
    case I_SNARE: snare_hit(v); break;
    case I_KICK: kick_hit(v); break;
    case I_TOM: tom_hit(e->note & 1, v); break;
    case I_CRASH: crash_hit(v); break;
    case I_BRUSH: brush_hit(v); break;
    case I_STICK: stick_hit(v); break;
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

#include "jazz_tune.c"
#include "jazz_play.c"

/* ------------------------------------------------------------- control --- */
static void quiet(void)
{
    int i;
    for (i = 0; i < NPV; i++)
        pv[i].on = 0;
    for (i = 0; i < NEV; i++)
        evq[i].used = 0;
    for (i = 0; i < 2; i++) {
        ks[i].on = 0;
        tom[i].amp = 0.0f;
    }
    for (i = 0; i < NCLAP; i++)
        clap[i].amp = 0.0f;
    ride.wash = ride.ping = ride.bell = ride.roll = hat.amp = crash.amp = 0.0f;
    snare.tamp = snare.namp = kick.amp = kick.click = brush.amp = stick.amp = clink.amp = 0.0f;
    sweep_lvl = 0.0f;
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
        fade_t = 0.0f;                           /* the band stops: cymbals caught, strings damped */
        fade_rate = 1.0f / (0.35f * EN_SR);
        for (i = 0; i < NEV; i++)
            evq[i].used = 0;
        for (i = 0; i < NPV; i++)
            if (pv[i].on && !pv[i].player)
                pv[i].gate = 0;
        for (i = 0; i < 2; i++)
            if (ks[i].on)
                ks[i].fb = fm_expf(-6.9f / (0.08f * ks[i].f));
        sweep_lvl = 0.0f;
        appl_lvl = 0.0f;
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
                piano_on(a, (float)b * (1.0f / 127.0f), -1, 1);
            else
                piano_off_player(a);
            break;
        case C_PANIC: quiet(); break;
        }
    }
}

void en_init(void)
{
    int i;
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
    for (i = 0; i < KS_N; i++)
        ks_buf[0][i] = ks_buf[1][i] = 0.0f;
    for (i = 0; i < 6; i++)
        ride.inc[i] = RIDE_F[i] / EN_SR;
    quiet();
    en_playing = 0;
    en_tune_no = 0;
    new_tune();
}

/* -------------------------------------------------------------- render --- */
static float bufl[BLK], bufr[BLK], rsend[BLK];
static float tone_a = 0.3f, t1l, t2l, t1r, t2r;  /* the bus's low-pass */
static float hp_l, hp_r;                         /* a DC / rumble cut */
static float lev = 0.0f;                         /* the leveller's envelope */
static float vu_acc_l, vu_acc_r;
static uint32_t vu_n;

static void render_piano(uint32_t n)
{
    int i;
    uint32_t j;
    float lv = pf(P_PIANO) * 0.30f, rs = 0.0f;
    static float pl[BLK], pr[BLK];
    for (j = 0; j < n; j++)
        pl[j] = pr[j] = 0.0f;
    for (i = 0; i < NPV; i++) {
        pv_t *v = &pv[i];
        float pc, p2, a1, a2, idx, part, ham, fl = v->idx_floor, id = v->idx_dec, gl = v->gl, gr = v->gr, ic = v->ic,
                                                i2 = v->i2;
        uint32_t seg, j0 = 0;
        if (!v->on)
            continue;
        if (!v->player)                          /* the band's notes fade with it; the keys played along don't */
            gl *= fade, gr *= fade;
        pc = v->pc, p2 = v->p2, a1 = v->a1, a2 = v->a2, idx = v->idx, part = v->part, ham = v->ham;
        while (j0 < n) {                         /* in two runs: held (decaying), then damped */
            float d1 = v->gate == 0 ? v->rel : v->d1, d2 = v->gate == 0 ? v->rel : v->d2;
            seg = n - j0;
            if (v->gate > 0 && (uint32_t)v->gate < seg)
                seg = (uint32_t)v->gate;
            if (part > 0.0005f || ham > 0.0005f) {
                for (j = j0; j < j0 + seg; j++) {
                    float s = sn(pc + sn(pc) * idx) * (a1 + a2) + sn(p2) * part + noise() * ham;
                    pl[j] += s * gl;
                    pr[j] += s * gr;
                    pc = wrap1(pc + ic);
                    p2 = wrap1(p2 + i2);
                    a1 *= d1;
                    a2 *= d2;
                    idx = fl + (idx - fl) * id;
                    part *= v->part_dec;
                    ham *= 0.93f;
                }
            } else {
                for (j = j0; j < j0 + seg; j++) {
                    float s = sn(pc + sn(pc) * idx) * (a1 + a2);
                    pl[j] += s * gl;
                    pr[j] += s * gr;
                    pc = wrap1(pc + ic);
                    a1 *= d1;
                    a2 *= d2;
                    idx = fl + (idx - fl) * id;
                }
                part = ham = 0.0f;
            }
            if (v->gate > 0)
                v->gate -= (int32_t)seg;
            j0 += seg;
        }
        v->pc = pc, v->p2 = p2, v->a1 = a1, v->a2 = a2, v->idx = idx, v->part = part, v->ham = ham;
        if (a1 + a2 < 0.0006f)
            v->on = 0;
    }
    (void)rs;
    for (j = 0; j < n; j++) {
        float l = pl[j] * lv, r = pr[j] * lv;
        bufl[j] += l;
        bufr[j] += r;
        rsend[j] += (l + r) * 0.5f;
    }
}

static float bass_sum[BLK];
static void render_bass(uint32_t n)
{
    uint32_t j;
    int s;
    float lv = pf(P_BASS) * 0.62f * fade, a = onepole(1100.0f), hpa = onepole(38.0f);
    for (s = 0; s < 2; s++) {
        float c, fb, d, lp;
        int w;
        if (!ks[s].on)
            continue;
        if (ks[s].gate > 0) {
            ks[s].gate -= (int32_t)n;
            if (ks[s].gate <= 0)                 /* the note's end: the finger lifts */
                ks[s].fb = fm_minf(ks[s].fb, fm_expf(-6.9f / (0.12f * ks[s].f)));
        }
        c = ks[s].c, fb = ks[s].fb, d = ks[s].d, lp = ks[s].lp, w = ks[s].w;
        for (j = 0; j < n; j++) {
            float rd = (float)w - d, fr, y;
            int ri;
            if (rd < 0.0f)
                rd += (float)KS_N;
            ri = (int)rd;
            fr = rd - (float)ri;
            y = ks_buf[s][ri & (KS_N - 1)] + (ks_buf[s][(ri + 1) & (KS_N - 1)] - ks_buf[s][ri & (KS_N - 1)]) * fr;
            lp += (y - lp) * c;
            ks_buf[s][w] = lp * fb;
            w = (w + 1) & (KS_N - 1);
            bass_sum[j] += y;
        }
        ks[s].lp = lp, ks[s].w = w;
        if (fb < 0.999f && fm_fabsf(lp) < 1e-4f && ks[s].gate <= 0) {
            ks[s].amp *= 0.5f;
            if (ks[s].amp < 0.01f)
                ks[s].on = 0;
        }
    }
    for (j = 0; j < n; j++) {                    /* the body: rounded off, the rumble cut */
        float x = bass_sum[j], y;
        bass_lp += (x - bass_lp) * a;
        bass_hp += (bass_lp - bass_hp) * hpa;
        y = (bass_lp - bass_hp) * lv;
        bufl[j] += y;
        bufr[j] += y;
        rsend[j] += y * 0.12f;
        bass_sum[j] = 0.0f;
    }
}

static void render_drums(uint32_t n)
{
    uint32_t j;
    float lv = pf(P_DRUMS) * 0.42f * fade;
    float fs = 2.0f * fm_sinf(FM_PI * 2100.0f / EN_SR), fh = 2.0f * fm_sinf(FM_PI * 3600.0f / EN_SR);
    float fb = 2.0f * fm_sinf(FM_PI * 3000.0f / EN_SR);
    float hpa = onepole(3200.0f), lpa = onepole(10500.0f), slp = onepole(6000.0f), blp = onepole(5200.0f);
    float wdec = tau(1.7f), pdec = tau(0.05f), bdec = tau(0.55f), cdec = tau(1.5f), hdec = tau(0.028f);
    float bi1 = 1210.0f / EN_SR, bi2 = 2870.0f / EN_SR, bpd = tau(0.10f);
    float sw = sweep_lvl * (0.35f + 0.65f * sweep_shape());
    if (ride.wash < 1e-4f && ride.ping < 1e-4f && ride.bell < 1e-4f && hat.amp < 1e-4f && crash.amp < 1e-4f &&
        snare.tamp < 1e-4f && snare.namp < 1e-4f && kick.amp < 1e-4f && tom[0].amp < 1e-4f &&
        tom[1].amp < 1e-4f && brush.amp < 1e-4f && sw < 1e-4f && stick.amp < 1e-4f)
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
            s += 0.0f;
            h += cym * (ride.wash * 0.30f + ride.ping * 0.85f);
            ride.wash *= wdec;
            ride.ping *= pdec;
        }
        if (ride.bell > 1e-4f) {                 /* the bell: two sines */
            h += (sn(ride.bph1) + 0.6f * sn(ride.bph2)) * ride.bell * 0.16f;
            ride.bph1 = wrap1(ride.bph1 + bi1);
            ride.bph2 = wrap1(ride.bph2 + bi2);
            ride.bell *= bdec;
            (void)bpd;
        }
        if (crash.amp > 1e-4f) {
            t += cym * crash.amp * 0.55f;
            crash.amp *= cdec;
        }
        if (hat.amp > 1e-4f) {                   /* the foot: a chick, its band of noise and the metal */
            float y = noise() * 0.6f + cym * 1.2f, hp;
            hat.env = fm_minf(hat.env + 1.0f / (0.0015f * EN_SR), 1.0f);
            hat.lo += fh * hat.bd;
            hp = y - hat.lo - 0.8f * hat.bd;
            hat.bd += fh * hp;
            t += (hat.bd * 0.5f + hp * 0.3f) * hat.amp * hat.env * 0.8f;
            hat.amp *= hdec;
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
        /* the kit, from the audience: on the right of the stage; the ride further right, the hat nearer centre */
        bufl[j] += (k * 0.75f + s * 0.62f + br * 0.7f + h * 0.55f + t * 0.8f) * lv;
        bufr[j] += (k * 0.75f + s * 0.80f + br * 0.85f + h * 1.05f + t * 0.95f) * lv;
        rsend[j] += (s * 0.35f + h * 0.35f + t * 0.4f + br * 0.3f + k * 0.08f) * lv;
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

static void control(uint32_t n)
{
    float k = (float)n / EN_SR;
    if (++vu_n >= 8u) {                          /* the VU: about 46 ms of RMS, then ballistics */
        float l = fm_sqrtf(vu_acc_l / (float)(n * 8u)), r = fm_sqrtf(vu_acc_r / (float)(n * 8u));
        en_vu_l = en_vu_l + (fm_minf(l * 4.0f, 1.2f) - en_vu_l) * 0.45f;
        en_vu_r = en_vu_r + (fm_minf(r * 4.0f, 1.2f) - en_vu_r) * 0.45f;
        vu_acc_l = vu_acc_r = 0.0f;
        vu_n = 0;
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
        render_piano(k);
        render_bass(k);
        render_drums(k);
        render_crowd(k);
        render_space(k);
        render_bus(out, k, gain);
        out += 2 * k;
        n -= k;
    }
}
