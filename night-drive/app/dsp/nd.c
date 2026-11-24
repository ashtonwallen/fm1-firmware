/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen
 * NIGHT DRIVE's engine (nd.h). One compilation unit, -O2. The command queue, the sine table and Dattorro's plate are
 * AMB-1's (Charles Vestal); the rest is NIGHT DRIVE's own. */
#include "nd.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

/* notes spelled as the key spells them: the sharp minor keys (A E B F# C# G#) with sharps, the others with flats */
static const char *const NOTE_SHARP[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const NOTE_FLAT[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
static int mod12(int v) { return ((v % 12) + 12) % 12; }
int en_sharp_key(int key)
{
    key = mod12(key);
    return key == 9 || key == 4 || key == 11 || key == 6 || key == 1 || key == 8;
}
const char *en_note(int pc, int key) { return (en_sharp_key(key) ? NOTE_SHARP : NOTE_FLAT)[mod12(pc)]; }
const char *const EN_SEC_NAME[EN_NSEC] = {"Intro", "Drive", "Break", "Build"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Speed", 85, 130, 108}, {"Bright", 0, 100, 55}, {"Arp", 0, 100, 60},  {"Drift", 0, 100, 40},
    {"Drums", 0, 100, 70},   {"Lead", 0, 100, 60},   {"Tune", -50, 50, 0}, {"Key", 0, 11, 9},
    {"Progression", 0, EN_NPROG - 1, 0}, {"Sound", 0, EN_NPRESETS - 1, 0},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

/* ---------------------------------------------------------- progressions --- */
/* the chord's root above the key, and 1 minor / 0 major */
static const int8_t PROG[EN_NPROG][4][2] = {
    {{0, 1}, {8, 0}, {3, 0}, {10, 0}},   /* i VI III VII */
    {{0, 1}, {10, 0}, {8, 0}, {10, 0}},  /* i VII VI VII */
    {{8, 0}, {10, 0}, {0, 1}, {0, 1}},   /* VI VII i i */
    {{0, 1}, {8, 0}, {5, 1}, {10, 0}},   /* i VI iv VII */
    {{0, 1}, {5, 1}, {8, 0}, {7, 0}},    /* i iv VI V */
    {{8, 0}, {5, 1}, {0, 1}, {10, 0}},   /* VI iv i VII */
    {{0, 1}, {7, 1}, {8, 0}, {10, 0}},   /* i v VI VII */
    {{5, 1}, {8, 0}, {10, 0}, {0, 1}},   /* iv VI VII i */
};
static const char *const PROG_NAME[EN_NPROG] = {"i VI III VII", "i VII VI VII", "VI VII i i", "i VI iv VII",
                                                "i iv VI V", "VI iv i VII", "i v VI VII", "iv VI VII i"};
const char *en_prog_name(int i) { return PROG_NAME[(unsigned)i < EN_NPROG ? i : 0]; }
void en_prog_chord(int p, int k, int *off, int *minor)
{
    p = (unsigned)p < EN_NPROG ? p : 0;
    *off = PROG[p][k & 3][0];
    *minor = PROG[p][k & 3][1];
}

/* ------------------------------------------------------------- presets --- */
/* the sounds of a preset; pattern and colour choices by number */
enum { PAD_SAW, PAD_HOLLOW, PAD_STRINGS };
enum { AW_SAW, AW_PULSE, AW_BELL };
enum { AP_UP, AP_UPDOWN, AP_PEDAL, AP_ROLL, AP_FIFTHS, NARP };
enum { BS_ROLL, BS_OCTAVE, BS_GALLOP, BS_EIGHTHS };
enum { LD_SAW, LD_BRASS, LD_SQUARE };
typedef struct {
    const char *name;
    uint8_t tempo, root, prog, bright, arp, drift;
    uint8_t pad, arpw, arpp, bass, kick, hats, lead;
    uint8_t cbars;                               /* bars a chord */
    uint8_t dly, rev, gate, detune;              /* 0..100: the delay's level, the plate's, the snare's gated room, the pads' width */
} snd_t;
static const snd_t SND[EN_NPRESETS] = {
    {"Midnight", 100, 5, 0, 50, 60, 40, PAD_SAW, AW_SAW, AP_UP, BS_ROLL, 0, 0, LD_SAW, 1, 45, 55, 70, 50},
    {"Sunset Strip", 96, 4, 2, 60, 50, 35, PAD_SAW, AW_PULSE, AP_UPDOWN, BS_OCTAVE, 1, 1, LD_BRASS, 1, 40, 50, 80, 60},
    {"Chrome", 112, 6, 1, 65, 70, 45, PAD_HOLLOW, AW_PULSE, AP_FIFTHS, BS_GALLOP, 0, 2, LD_SQUARE, 1, 35, 40, 60, 30},
    {"Turbo", 124, 2, 4, 70, 85, 55, PAD_SAW, AW_SAW, AP_ROLL, BS_ROLL, 0, 2, LD_SAW, 1, 30, 35, 65, 45},
    {"Neon Rain", 88, 0, 3, 40, 45, 30, PAD_STRINGS, AW_BELL, AP_UPDOWN, BS_EIGHTHS, 2, 0, LD_BRASS, 2, 55, 70, 85, 70},
    {"Coastline", 92, 7, 5, 50, 40, 30, PAD_STRINGS, AW_SAW, AP_PEDAL, BS_OCTAVE, 1, 1, LD_SAW, 2, 50, 65, 75, 65},
    {"Arcade", 118, 11, 6, 75, 80, 50, PAD_HOLLOW, AW_PULSE, AP_UP, BS_GALLOP, 0, 2, LD_SQUARE, 1, 30, 30, 50, 25},
    {"Outrun", 110, 9, 1, 60, 65, 45, PAD_SAW, AW_SAW, AP_FIFTHS, BS_ROLL, 0, 0, LD_SAW, 1, 40, 45, 75, 55},
    {"Afterglow", 98, 3, 0, 45, 55, 35, PAD_STRINGS, AW_BELL, AP_PEDAL, BS_OCTAVE, 1, 0, LD_BRASS, 1, 50, 60, 80, 60},
    {"Palm Haze", 90, 8, 7, 45, 35, 25, PAD_STRINGS, AW_PULSE, AP_UPDOWN, BS_EIGHTHS, 2, 0, LD_SAW, 2, 55, 70, 70, 75},
    {"Overdrive", 128, 1, 4, 75, 90, 60, PAD_SAW, AW_SAW, AP_ROLL, BS_ROLL, 0, 2, LD_BRASS, 1, 30, 35, 60, 40},
    {"Starfield", 86, 2, 2, 35, 30, 70, PAD_STRINGS, AW_BELL, AP_UP, BS_EIGHTHS, 1, 0, LD_SAW, 2, 60, 80, 80, 80},
    {"Interstate", 104, 10, 3, 55, 60, 40, PAD_SAW, AW_PULSE, AP_PEDAL, BS_ROLL, 0, 1, LD_SAW, 1, 40, 50, 70, 50},
    {"Laserdisc", 108, 0, 6, 70, 75, 50, PAD_HOLLOW, AW_SAW, AP_UPDOWN, BS_GALLOP, 1, 2, LD_SQUARE, 1, 35, 40, 65, 35},
};
const char *en_preset_name(int i) { return SND[(unsigned)i < EN_NPRESETS ? i : 0].name; }
void en_preset(int i, int16_t *p)
{
    const snd_t *s = &SND[(unsigned)i < EN_NPRESETS ? i : 0];
    int k;
    for (k = 0; k < P_NPARAMS; k++)
        p[k] = PARAMS[k].def;
    p[P_TEMPO] = s->tempo;
    p[P_BRIGHT] = s->bright;
    p[P_ARP] = s->arp;
    p[P_DRIFT] = s->drift;
    p[P_ROOT] = s->root;
    p[P_PROG] = s->prog;
    p[P_SOUND] = (int16_t)((unsigned)i < EN_NPRESETS ? i : 0);
}

static char *cat(char *b, const char *s)
{
    while ((*b = *s++))
        b++;
    return b;
}
static char *itoa_s(int v, char *b)
{
    char t[12];
    int n = 0;
    unsigned u;
    if (v < 0) {
        *b++ = '-';
        u = (unsigned)-v;
    } else {
        u = (unsigned)v;
    }
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}

const char *en_chord_name(int root, int minor, int key, char *buf)
{
    char *b = cat(buf, en_note(root, key));
    if (minor)
        cat(b, "m");
    return buf;
}

void en_param_text(int i, int v, char *b)
{
    b[0] = 0;
    switch (i) {
    case P_TEMPO: cat(itoa_s(v, b), " bpm"); break;
    case P_ARP: case P_DRUMS: case P_LEAD:
        if (v == 0)
            cat(b, "Off");
        else
            itoa_s(v, b);
        break;
    case P_TUNE:
        if (v > 0)
            *b++ = '+';
        cat(itoa_s(v, b), " c");
        break;
    case P_ROOT: cat(cat(b, en_note(v, v)), "m"); break;
    case P_PROG: cat(b, en_prog_name(v)); break;
    case P_SOUND: cat(b, en_preset_name(v)); break;
    default: itoa_s(v, b); break;
    }
}

/* natural minor: degree deg of the key, in the octave whose root is the highest at or below base */
static const int8_t MINOR[7] = {0, 2, 3, 5, 7, 8, 10};
int en_scale_note(int key, int deg, int base)
{
    int oct = deg >= 0 ? deg / 7 : -((6 - deg) / 7), d = deg - 7 * oct;
    int root = base - (((base - key) % 12) + 12) % 12;
    return root + MINOR[d] + 12 * oct;
}
static int in_key(int key, int pc)
{
    int r = ((pc - key) % 12 + 12) % 12;
    return r == 0 || r == 2 || r == 3 || r == 5 || r == 7 || r == 8 || r == 10;
}
static int degree_of(int off)                   /* a chord root above the key -> its scale degree */
{
    static const int8_t D[12] = {0, 0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6};
    return D[((off % 12) + 12) % 12];
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_PLAY, C_NEXT, C_NEWKEY, C_NEWARP, C_SEED, C_PANIC, C_RESTART };
#define QN 64u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                                   /* full: dropped (never, at the UI's rate) */
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void en_set(int p, int v) { post(C_SET, p, v); }
void en_play(int on) { post(C_PLAY, on, 0); }
void en_next(void) { post(C_NEXT, 0, 0); }
void en_newkey(void) { post(C_NEWKEY, 0, 0); }
void en_newarp(void) { post(C_NEWARP, 0, 0); }
void en_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_restart(void) { post(C_RESTART, 0, 0); }

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_key, en_chord_root, en_chord_minor, en_chord_idx, en_prog;
volatile uint8_t en_section, en_sec_bar, en_sec_len, en_step, en_arp_pat, en_lead_on;
volatile uint32_t en_sixteenths, en_keychanges;
volatile float en_beats, en_kick, en_snare, en_hat, en_arp_glow, en_lead_glow, en_bass_glow, en_pad_level, en_energy;
volatile float en_gr = 1.0f;

static int16_t par[P_NPARAMS];
static const snd_t *snd = &SND[0];
static float tunefac = 1.0f;
static uint32_t rng = 0x9E3779B9u, nz = 0x2545F491u;

static uint32_t rnd(void)                       /* Marsaglia's xorshift: shifts 13, 17, 5 */
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    rng = x ^ (x << 5);
    return rng;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static int irand(int n) { return n > 0 ? (int)((rnd() >> 8) % (uint32_t)n) : 0; }
static inline float noise(void)
{
    uint32_t x = nz;
    x ^= x << 13;
    x ^= x >> 17;
    nz = x ^ (x << 5);
    return (float)(int32_t)nz * (1.0f / 2147483648.0f);
}

static float mtof(float n) { return 440.0f * tunefac * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }

/* a sine table: 1024 points a turn, plus one (AMB-1's) */
#define SN 1024
static float sint[SN + 1];
static inline float sine(float ph)              /* ph in turns, >= 0 */
{
    float x = (ph - (float)(int)ph) * (float)SN;
    int i = (int)x;
    float f = x - (float)i;
    return sint[i] + (sint[i + 1] - sint[i]) * f;
}

/* ---------------------------------------------------------- building blocks --- */
/* Zavalishin's trapezoidal state-variable filter: stable at any setting, coefficients at the control rate */
typedef struct {
    float a1, a2, a3, k;
} svfc_t;
typedef struct {
    float ic1, ic2;
} svf_t;
static void svf_set(svfc_t *c, float fc, float q)
{
    float g = fm_tanf(FM_PI * fm_clampf(fc, 20.0f, 15000.0f) * (1.0f / EN_SR));
    c->k = 1.0f / q;
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}
static inline float svf_lp(svf_t *s, const svfc_t *c, float x)
{
    float v3 = x - s->ic2, v1 = c->a1 * s->ic1 + c->a2 * v3, v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    return v2;
}
static inline float svf_bp(svf_t *s, const svfc_t *c, float x)
{
    float v3 = x - s->ic2, v1 = c->a1 * s->ic1 + c->a2 * v3, v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    return v1;
}
static void svf_flush(svf_t *s)
{
    s->ic1 = fm_flush(s->ic1);
    s->ic2 = fm_flush(s->ic2);
}

/* band-limited saw and pulse (polyBLEP) */
static inline float blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline float saw(float *ph, float dt)
{
    float t = *ph + dt;
    if (t >= 1.0f)
        t -= 1.0f;
    *ph = t;
    return 2.0f * t - 1.0f - blep(t, dt);
}
static inline float pulse(float *ph, float dt)
{
    float t = *ph + dt, t2;
    if (t >= 1.0f)
        t -= 1.0f;
    *ph = t;
    t2 = t + 0.5f;
    if (t2 >= 1.0f)
        t2 -= 1.0f;
    return (2.0f * t - blep(t, dt)) - (2.0f * t2 - blep(t2, dt));
}

/* a time constant as a one-pole's coefficient per sample */
static float tc(float secs) { return 1.0f - fm_expf(-1.0f / (secs * EN_SR)); }
static float decay_k(float secs) { return fm_expf(-1.0f / (secs * EN_SR)); }   /* multiply per sample */

/* --------------------------------------------------------------- the song --- */
static float step_len;                          /* samples a sixteenth */
static float step_left;                         /* until the next */
static int key = 9, home = 9, key_pending = -1, prog;
static int section, sec_bar, sec_len, step16 = -1, drives, breaks_since;
static int next_req, newkey_req;
static int chord_off, chord_minor, chord_k = -1;
static uint32_t sixteenths;

/* the layers' levels, as the sections set them (targets, then slid per chunk) */
enum { LY_PAD, LY_ARP, LY_BASS, LY_DRUMS, NLY };
static float ly[NLY], ly_t[NLY];

/* ---------------------------------------------------------------- pads --- */
#define NPAD 4
typedef struct {
    float ph[2], f, f_t, dt[2];
    float amp;
    int note;
} pad_t;
static pad_t pad[NPAD];
static svf_t pad_fl, pad_fr;
static svfc_t pad_c;
static float pad_det = 1.004f, pad_lfo;
static const float PAD_PAN[NPAD] = {-0.55f, -0.18f, 0.18f, 0.55f};

static void pad_voice(const int *pcs)           /* the chord's four tones to the four voices, each moving least */
{
    int used[4] = {0, 0, 0, 0}, v, j, o;
    for (v = 0; v < NPAD; v++) {
        int best = -1, bn = 0, bd = 99;
        for (j = 0; j < 4; j++) {
            if (used[j])
                continue;
            for (o = 48; o <= 74; o += 12) {
                int n = o + ((pcs[j] - o) % 12 + 12) % 12, d = n - pad[v].note;
                if (n > 74)
                    continue;
                d = d < 0 ? -d : d;
                if (d < bd) {
                    bd = d;
                    bn = n;
                    best = j;
                }
            }
        }
        if (best < 0)
            best = 0, bn = pad[v].note;
        used[best] = 1;
        pad[v].note = bn;
        pad[v].f_t = mtof((float)bn);
    }
}

/* ---------------------------------------------------------------- bass --- */
static struct {
    float ph, sph, f, dt, amp, env, gate_left, vel;
    int note;
    svf_t fl;
    svfc_t c;
} bass;
static void bass_note(int n, float vel, float len_steps)
{
    bass.note = n;
    bass.f = mtof((float)n);
    bass.dt = bass.f * (1.0f / EN_SR);
    bass.env = 1.0f;
    bass.vel = vel;
    bass.gate_left = len_steps * step_len;
    en_bass_glow = vel;
}

/* ----------------------------------------------------------------- arp --- */
static struct {
    float ph[2], dt[2], amp, env, vel;
    int n;                                      /* notes since the chord came */
    uint16_t mask;                              /* the steps it plays (ARP) */
    uint8_t pat, mask_pending;
    svf_t fl;
    svfc_t c;
} arp;
static const int8_t ARP_SEQ[NARP][8] = {
    {0, 1, 2, 3, 0, 1, 2, 3}, {0, 1, 2, 3, 4, 3, 2, 1}, {0, 3, 0, 2, 0, 3, 0, 4}, {0, 2, 3, 4, 5, 4, 3, 2},
    {0, 2, 3, 2, 0, 2, 3, 2}};
static int arp_tones[6];                        /* root, third, fifth, octave, tenth, twelfth */

static void arp_mask(void)                      /* ARP's steps for the bars to come */
{
    float a = (float)par[P_ARP] * 0.01f;
    uint16_t m = 0;
    int s;
    if (par[P_ARP] > 0) {
        for (s = 0; s < 16; s++) {
            float p = (s & 3) == 0 ? a * 4.0f : (s & 1) == 0 ? (a - 0.15f) * 2.5f : (a - 0.5f) * 2.4f;
            if (section == SEC_BREAK && (s & 1))
                p *= 0.3f;
            if (frand() < p)
                m |= (uint16_t)(1u << s);
        }
        m |= 1u;
    }
    arp.mask = m;
}

/* ---------------------------------------------------------------- lead --- */
#define MOTIF_MAX 24
static struct {
    float ph[2], dt[2], f, f_t, amp, gate, ienv, vib, age;
    int note;
    svf_t fl;
    svfc_t c;
    /* the phrase: a motif of two bars played twice; on, the bar in it */
    uint8_t on, bar, nm, rest;
    int8_t m_step[MOTIF_MAX], m_len[MOTIF_MAX], m_deg[MOTIF_MAX];
    float note_left;
} lead;

static void motif_make(int slow)
{
    int s = 0, d = (int)irand(3) + 2, n = 0;
    while (s < 32 && n < MOTIF_MAX) {
        static const int8_t LEN[6] = {2, 2, 4, 4, 6, 8}, SLOW[3] = {8, 12, 16};
        int len = slow ? SLOW[irand(3)] : LEN[irand(6)];
        if (s + len > 32)
            len = 32 - s;
        if (s > 0 && frand() < 0.15f) {         /* a rest */
            s += len;
            continue;
        }
        if ((s & 7) == 0) {                     /* strong beats: a chord tone */
            static const int8_t CT[4] = {0, 2, 4, 7};
            d = CT[irand(4)];
        } else {
            d += irand(5) - 2;
        }
        d = d < -1 ? -1 : d > 9 ? 9 : d;
        lead.m_step[n] = (int8_t)s;
        lead.m_len[n] = (int8_t)len;
        lead.m_deg[n] = (int8_t)d;
        n++;
        s += len;
    }
    lead.nm = (uint8_t)n;
}

/* --------------------------------------------------------------- drums --- */
static struct {
    float ph, amp, f;
} kick;
static struct {
    float ph, body, nse, hp, lp;
} snare;
static struct {
    float amp, dk, hp1, hp2, x1;
} hat, crash;
static struct {
    float ph, amp, f, f0;
} tom;
static float riser, riser_t;
static svf_t riser_f;
static svfc_t riser_c;
static float duck;                              /* the kick's pump: 1 at a hit, falling */
static float drum_lvl;

static const uint16_t KICK_PAT[3] = {0x1111u, 0x0141u, 0x0501u};
static const uint16_t HAT_PAT[3] = {0x4444u, 0x5555u, 0xFFFFu};

static void kick_hit(float vel)
{
    kick.ph = 0.0f;
    kick.amp = vel;
    kick.f = 1.0f;
    duck = 1.0f;
    en_kick = 1.0f;
}
static float gate_env, gate_hold;
static void snare_hit(float vel)
{
    snare.body = vel;
    snare.nse = vel;
    snare.ph = 0.0f;
    gate_env = 1.0f;
    gate_hold = (0.12f + 0.0018f * (float)snd->gate) * EN_SR;
    if (vel > 0.5f)
        en_snare = vel;
}
static void hat_hit(float vel, int open)
{
    hat.amp = vel;
    hat.dk = decay_k(open ? 0.22f : 0.035f);
    en_hat = vel;
}
static void crash_hit(float vel)
{
    crash.amp = vel;
    crash.dk = decay_k(1.6f);
}
static void tom_hit(float f, float vel)
{
    tom.ph = 0.0f;
    tom.amp = vel;
    tom.f0 = f;
    tom.f = 1.0f;
}

/* ------------------------------------------------------- the structure --- */
static int sec_length(int s)                    /* in bars: Drift shortens them */
{
    int d = par[P_DRIFT];
    switch (s) {
    case SEC_INTRO: return 8;
    case SEC_DRIVE: return d < 30 ? 32 : d < 70 ? (frand() < 0.5f ? 16 : 32) : (frand() < 0.5f ? 8 : 16);
    case SEC_BREAK: return d < 30 ? 16 : d < 75 ? 8 : 4;
    default: return d > 85 ? 2 : 4;
    }
}

static void key_change(int big)                 /* the engine's own: mostly up, never far from home */
{
    static const int8_t UP[6] = {1, 2, 1, 2, -2, 5};
    int dist, k;
    k = big ? key + UP[irand(6)] : key + (irand(2) ? 3 : -3);
    k = ((k % 12) + 12) % 12;
    dist = ((k - home) % 12 + 12) % 12;
    if (dist > 4 && dist < 8)                   /* (a tritone or so away: home instead) */
        k = home;
    key_pending = k;
}

static void section_start(int s)
{
    int d = par[P_DRIFT];
    if (s == SEC_DRIVE && section == SEC_BUILD && frand() < 0.007f * (float)d)
        key_change(1);
    if (s == SEC_BREAK && frand() < 0.25f * (float)d * 0.01f)
        key_change(0);
    if ((s == SEC_DRIVE || s == SEC_BREAK) && section != s && frand() < 0.35f * (float)d * 0.01f) {
        int p = irand(EN_NPROG - 1);
        prog = p >= prog ? p + 1 : p;
    }
    if (s == SEC_DRIVE && frand() < 0.25f * (float)d * 0.01f)
        arp.pat = (uint8_t)irand(NARP);
    if (s == SEC_DRIVE)
        drives++;
    breaks_since = s == SEC_BREAK ? 0 : breaks_since + (s == SEC_DRIVE);
    section = s;
    sec_bar = 0;
    sec_len = sec_length(s);
    arp.mask_pending = 1;
    lead.on = 0;
    lead.rest = s == SEC_DRIVE ? (uint8_t)(drives == 1 ? 8 : 4) : 2;
    if (s == SEC_DRIVE)
        crash_hit(0.55f);
}

static void section_next(void)
{
    switch (section) {
    case SEC_INTRO: section_start(SEC_DRIVE); break;
    case SEC_DRIVE: section_start(breaks_since >= 1 && frand() < 0.75f ? SEC_BREAK : SEC_DRIVE); break;
    case SEC_BREAK: section_start(SEC_BUILD); break;
    default: section_start(SEC_DRIVE); break;
    }
}

static void layers_for_section(void)
{
    float f = sec_len > 0 ? (float)sec_bar / (float)sec_len : 0.0f;
    switch (section) {
    case SEC_INTRO:
        ly_t[LY_PAD] = 1.0f;
        ly_t[LY_ARP] = 0.5f + 0.5f * f;
        ly_t[LY_BASS] = f >= 0.5f ? 1.0f : 0.0f;
        ly_t[LY_DRUMS] = f >= 0.5f ? 0.7f : 0.0f;
        break;
    case SEC_DRIVE:
        ly_t[LY_PAD] = 0.8f;
        ly_t[LY_ARP] = 1.0f;
        ly_t[LY_BASS] = 1.0f;
        ly_t[LY_DRUMS] = 1.0f;
        break;
    case SEC_BREAK:
        ly_t[LY_PAD] = 1.15f;
        ly_t[LY_ARP] = 0.8f;
        ly_t[LY_BASS] = 0.6f;
        ly_t[LY_DRUMS] = 0.0f;
        break;
    default:
        ly_t[LY_PAD] = 0.9f;
        ly_t[LY_ARP] = 1.0f;
        ly_t[LY_BASS] = 0.9f;
        ly_t[LY_DRUMS] = 1.0f;
        break;
    }
}

static void chord_set(int k)
{
    int pcs[4], root, cands[2], i;
    en_prog_chord(prog, k, &chord_off, &chord_minor);
    chord_k = k;
    root = (key + chord_off) % 12;
    pcs[0] = root;
    pcs[1] = (root + (chord_minor ? 3 : 4)) % 12;
    pcs[2] = (root + 7) % 12;
    cands[0] = (root + (chord_minor ? 10 : 11)) % 12;   /* the colour: a seventh, else a ninth, in the key */
    cands[1] = (root + 2) % 12;
    pcs[3] = root;
    for (i = 0; i < 2; i++)
        if (in_key(key, cands[i])) {
            pcs[3] = cands[i];
            break;
        }
    pad_voice(pcs);
    {
        int r = 57 + ((root - 57) % 12 + 12) % 12;    /* the arp: from the root at or above A3 */
        if (r > 63)
            r -= 12;
        arp_tones[0] = r;
        arp_tones[1] = r + (chord_minor ? 3 : 4);
        arp_tones[2] = r + 7;
        arp_tones[3] = r + 12;
        arp_tones[4] = r + 12 + (chord_minor ? 3 : 4);
        arp_tones[5] = r + 19;
    }
    arp.n = 0;
    en_chord_root = (uint8_t)root;
    en_chord_minor = (uint8_t)chord_minor;
    en_chord_idx = (uint8_t)k;
}

static void bar_start(void)
{
    int cb, k;
    if (sixteenths > 0) {
        sec_bar++;
        if (sec_bar >= sec_len || next_req) {
            next_req = 0;
            section_next();
        }
    }
    if (newkey_req) {
        newkey_req = 0;
        key_change(1);
    }
    if (key_pending >= 0) {
        if (key_pending != key) {
            en_keychanges++;
            chord_k = -1;                        /* (the chord again, in the new key) */
        }
        key = key_pending;
        key_pending = -1;
    }
    if (arp.mask_pending) {
        arp.mask_pending = 0;
        arp_mask();
    }
    layers_for_section();
    cb = snd->cbars * (section == SEC_BREAK ? 2 : 1);
    k = (sec_bar / cb) & 3;
    if (k != chord_k || sec_bar == 0 || (sec_bar % cb) == 0)
        chord_set(k);
    /* the lead: in a drive, a phrase of four bars now and then, then a rest; in a break, slow notes */
    if (lead.on) {
        if (++lead.bar >= 4) {
            lead.on = 0;
            lead.rest = (uint8_t)(4 + 4 * irand(2));
        }
    } else if (lead.rest) {
        lead.rest--;
    } else if (par[P_LEAD] > 0 && (sec_bar & 3) == 0 && sec_bar + 4 <= sec_len) {
        float p = (float)par[P_LEAD] * 0.01f;
        if ((section == SEC_DRIVE && frand() < 0.25f + 0.65f * p) ||
            (section == SEC_BREAK && par[P_LEAD] > 30 && frand() < 0.4f * p)) {
            motif_make(section == SEC_BREAK);
            lead.on = 1;
            lead.bar = 0;
        } else {
            lead.rest = 4;
        }
    }
    en_lead_on = lead.on;
    en_section = (uint8_t)section;
    en_sec_bar = (uint8_t)sec_bar;
    en_sec_len = (uint8_t)sec_len;
    en_key = (uint8_t)key;
    en_prog = (uint8_t)prog;
    en_arp_pat = arp.pat;
}

static int bass_root(void)                      /* the chord's root in the bass's octave: E1 .. D#2 */
{
    int r = (key + chord_off) % 12;
    return 28 + ((r - 28) % 12 + 12) % 12;
}

static void step(void)
{
    int s, last_bar;
    float dv = (float)par[P_DRUMS] * 0.01f;
    step16 = (step16 + 1) & 15;
    s = step16;
    if (s == 0)
        bar_start();
    last_bar = sec_bar == sec_len - 1;
    en_step = (uint8_t)s;
    /* drums */
    if (dv > 0.0f && ly_t[LY_DRUMS] > 0.0f) {
        uint16_t km = KICK_PAT[snd->kick], hm = HAT_PAT[snd->hats];
        int fill = last_bar && (section == SEC_DRIVE || section == SEC_INTRO) && s >= 12;
        if (section == SEC_BUILD) {
            int stage = sec_len > 1 ? sec_bar * 3 / sec_len : 2, every = stage == 0 ? 4 : stage == 1 ? 2 : 1;
            float v = 0.35f + 0.6f * ((float)sec_bar + (float)s / 16.0f) / (float)sec_len;
            if (!(last_bar && s >= 14)) {        /* (the last half beat: the breath before the drop) */
                if ((s & 3) == 0)
                    kick_hit(0.85f);
                if (s % every == 0)
                    snare_hit(v);
                if ((s & 1) == 0)
                    hat_hit(0.25f, 0);
            }
        } else if (section == SEC_INTRO) {
            if (fill)
                snare_hit(0.4f + 0.12f * (float)(s - 12));
            else if (hm >> s & 1u)
                hat_hit((s & 3) == 2 ? 0.45f : 0.25f, 0);
        } else if (section == SEC_DRIVE) {
            if (km >> s & 1u)
                kick_hit(s == 0 ? 1.0f : 0.9f);
            if (fill && drives & 1) {             /* toms down */
                static const float TF[4] = {196.0f, 165.0f, 131.0f, 98.0f};
                if ((s & 1) == 0 || s == 15)
                    tom_hit(TF[(s - 12) & 3], 0.8f);
            } else if (fill) {
                snare_hit(0.5f + 0.15f * (float)(s - 12));
            } else if (s == 4 || s == 12) {
                snare_hit(1.0f);
            }
            if (hm >> s & 1u)
                hat_hit((s & 3) == 2 ? 0.55f : (s & 1) ? 0.22f : 0.35f, s == 14 && (sec_bar & 1));
        }
    }
    /* bass */
    if (ly_t[LY_BASS] > 0.0f) {
        int r = bass_root();
        if (section == SEC_BREAK) {
            if (s == 0)
                bass_note(r, 0.7f, 16.0f * (float)snd->cbars);
        } else if (section == SEC_BUILD) {
            if ((s & 1) == 0)
                bass_note(r, (s & 3) ? 0.7f : 0.9f, 1.6f);
        } else {
            switch (snd->bass) {
            case BS_ROLL: bass_note(r, (s & 3) == 0 ? 1.0f : (s & 1) ? 0.62f : 0.75f, 0.85f); break;
            case BS_OCTAVE:
                if ((s & 1) == 0)
                    bass_note((s & 2) ? r + 12 : r, (s & 3) == 0 ? 1.0f : 0.8f, 1.5f);
                break;
            case BS_GALLOP:
                if ((s & 3) != 1)
                    bass_note(r, (s & 3) == 0 ? 1.0f : 0.72f, 0.85f);
                break;
            default:
                if ((s & 1) == 0)
                    bass_note(s == 14 ? r + 7 : r, (s & 3) == 0 ? 1.0f : 0.8f, 1.7f);
                break;
            }
        }
    }
    /* arp */
    if ((arp.mask >> s & 1u) && ly_t[LY_ARP] > 0.0f) {
        int t = ARP_SEQ[arp.pat][arp.n & 7];
        float f = mtof((float)arp_tones[t]);
        arp.dt[0] = f * (1.0f / EN_SR);
        arp.dt[1] = f * 1.0035f * (1.0f / EN_SR);
        arp.env = 1.0f;
        arp.vel = (s & 3) == 0 ? 1.0f : 0.78f;
        arp.n++;
        en_arp_glow = 1.0f;
    }
    /* lead */
    if (lead.on) {
        int pos = (lead.bar & 1) * 16 + s, i;
        for (i = 0; i < lead.nm; i++)
            if (lead.m_step[i] == pos) {
                int deg = lead.m_deg[i], last = lead.bar == 3 && i == lead.nm - 1;
                if (last)
                    deg = deg > 3 ? 4 : 0;       /* the answer comes home */
                lead.note = en_scale_note(key, degree_of(chord_off) + deg, 74);
                lead.f_t = mtof((float)lead.note);
                if (lead.gate < 0.5f) {
                    lead.f = lead.f_t;
                    lead.age = 0.0f;
                }
                lead.gate = 1.0f;
                lead.ienv = 1.0f;
                lead.note_left = ((float)lead.m_len[i] - 0.4f) * step_len;
                en_lead_glow = 1.0f;
            }
    }
    sixteenths++;
    en_sixteenths = sixteenths;
}

/* ------------------------------------------------- reverb: Dattorro's plate --- */
/* (AMB-1's, after J. Dattorro, "Effect Design, Part 1", JAES 1997: a predelay, four input diffusers, then a
 * figure-eight tank of two halves; the paper's lengths for 29761 Hz, DS() scales them to 44.1 kHz) */
#define DS(n) (((n) * 14818 + 5000) / 10000)
#define PRE_N 662                                    /* 15 ms */
#define EXC DS(16)
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define DL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static float pl_buf[DL_TOTAL] EN_POOL;
typedef struct {
    float *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, pl_decay = 0.5f, pl_dd2 = 0.5f, lfo_s, lfo_c = 1.0f;

static inline float tap(const dl_t *d, int k)
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void push(dl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline void reverb(float in, float *ol, float *or_)
{
    float x, a, b, fb_l = tap(&dl[L_D2R], DS(3163)), fb_r = tap(&dl[L_D2L], DS(3720));
    push(&dl[L_PRE], in);
    x = tap(&dl[L_PRE], PRE_N);
    pl_bw += 0.6f * (x - pl_bw);
    x = allpass(&dl[L_IN1], DS(142), pl_bw, 0.75f);
    x = allpass(&dl[L_IN2], DS(107), x, 0.75f);
    x = allpass(&dl[L_IN3], DS(379), x, 0.625f);
    x = allpass(&dl[L_IN4], DS(277), x, 0.625f);
    {
        float s = lfo_s + 0.0001f * lfo_c, c = lfo_c - 0.0001f * s;
        lfo_s = s;
        lfo_c = c;
    }
    a = allpass_mod(&dl[L_APL], (float)DS(672) + (float)EXC * lfo_s, x + pl_decay * fb_l, -0.7f);
    push(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], DS(4453));
    pl_damp_l += 0.4f * (b - pl_damp_l);
    b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * pl_decay, pl_dd2);
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * lfo_c, x + pl_decay * fb_r, -0.7f);
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], DS(4217));
    pl_damp_r += 0.4f * (b - pl_damp_r);
    b = allpass(&dl[L_AP2R], DS(2656), pl_damp_r * pl_decay, pl_dd2);
    push(&dl[L_D2R], b);
    *ol = 0.6f * (tap(&dl[L_D1R], DS(266)) + tap(&dl[L_D1R], DS(2974)) - tap(&dl[L_AP2R], DS(1913)) +
                  tap(&dl[L_D2R], DS(1996)) - tap(&dl[L_D1L], DS(1990)) - tap(&dl[L_AP2L], DS(187)) -
                  tap(&dl[L_D2L], DS(1066)));
    *or_ = 0.6f * (tap(&dl[L_D1L], DS(353)) + tap(&dl[L_D1L], DS(3627)) - tap(&dl[L_AP2L], DS(1228)) +
                   tap(&dl[L_D2L], DS(2673)) - tap(&dl[L_D1R], DS(2111)) - tap(&dl[L_AP2R], DS(335)) -
                   tap(&dl[L_D2R], DS(121)));
}
static void plate_init(void)
{
    float *p = pl_buf;
    int k, i;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    for (i = 0; i < DL_TOTAL; i++)
        pl_buf[i] = 0.0f;
    pl_bw = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
}

/* ------------------------------------------------- the snare's gated room --- */
/* four damped combs and two allpasses (Schroeder's, Freeverb's lengths), long and dense, cut by a gate a moment
 * after each hit: the eighties' big snare */
#define GC0 1116
#define GC1 1188
#define GC2 1277
#define GC3 1356
#define GA0 556
#define GA1 441
static float gr_buf[GC0 + GC1 + GC2 + GC3 + GA0 + GA1] EN_POOL;
static float *gc[4], *ga[2], gcl[4];
static int gci[4], gai[2];
static const int GC_N[4] = {GC0, GC1, GC2, GC3}, GA_N[2] = {GA0, GA1};

static void gate_init(void)
{
    float *p = gr_buf;
    int k;
    for (k = 0; k < (int)(sizeof gr_buf / sizeof gr_buf[0]); k++)
        gr_buf[k] = 0.0f;
    for (k = 0; k < 4; k++) {
        gc[k] = p;
        p += GC_N[k];
        gci[k] = 0;
        gcl[k] = 0.0f;
    }
    for (k = 0; k < 2; k++) {
        ga[k] = p;
        p += GA_N[k];
        gai[k] = 0;
    }
}
static inline void gated(float in, float *ol, float *or_)
{
    float o[4], x;
    int k;
    for (k = 0; k < 4; k++) {
        float y = gc[k][gci[k]];
        gcl[k] += 0.35f * (y - gcl[k]);
        gc[k][gci[k]] = fm_flush(in + gcl[k] * 0.86f);
        if (++gci[k] >= GC_N[k])
            gci[k] = 0;
        o[k] = y;
    }
    x = (o[0] + o[1] + o[2] + o[3]) * 0.25f;
    for (k = 0; k < 2; k++) {
        float z = ga[k][gai[k]], v = x + 0.5f * z;
        ga[k][gai[k]] = fm_flush(v);
        if (++gai[k] >= GA_N[k])
            gai[k] = 0;
        x = z - 0.5f * v;
    }
    *ol = x + 0.3f * (o[0] - o[2]);
    *or_ = x + 0.3f * (o[1] - o[3]);
}

/* ------------------------------------------------- the ping-pong delay --- */
#define DLY_N 23400                              /* 0.53 s: a dotted eighth at 85 bpm */
static int16_t dly_l[DLY_N] EN_POOL, dly_r[DLY_N] EN_POOL;
static int dly_w;
static float dly_len, dly_len_t, dly_lp_l, dly_lp_r;
static inline int16_t s16(float x)
{
    x = fm_clampf(x * 8192.0f, -32767.0f, 32767.0f);
    return (int16_t)x;
}
static inline float dly_read(const int16_t *b, float d)
{
    float p = (float)dly_w - d;
    int i, j;
    float f;
    if (p < 0.0f)
        p += (float)DLY_N;
    i = (int)p;
    f = p - (float)i;
    j = i + 1 >= DLY_N ? 0 : i + 1;
    return ((float)b[i] + ((float)b[j] - (float)b[i]) * f) * (1.0f / 8192.0f);
}

/* ------------------------------------------------------------ parameters --- */
static float bright;                            /* 0..1 */
static float pad_g, arp_g, bass_g, lead_g, drums_g;

static void tempo_set(void)
{
    step_len = EN_SR * 60.0f / ((float)par[P_TEMPO] * 4.0f);
    dly_len_t = fm_minf(3.0f * step_len, (float)(DLY_N - 4));
}

static void sound_set(int i)
{
    snd = &SND[(unsigned)i < EN_NPRESETS ? i : 0];
    pad_det = fm_exp2f((4.0f + 0.14f * (float)snd->detune) * (1.0f / 1200.0f));
    pl_decay = 0.45f + 0.0035f * (float)snd->rev;
    pl_dd2 = fm_clampf(pl_decay + 0.15f, 0.25f, 0.5f);
    arp.pat = snd->arpp;
}

static void apply(int p, int v)
{
    const en_param_t *pi = en_param_info(p);
    v = v < pi->lo ? pi->lo : v > pi->hi ? pi->hi : v;
    par[p] = (int16_t)v;
    switch (p) {
    case P_TEMPO: tempo_set(); break;
    case P_BRIGHT: bright = (float)v * 0.01f; break;
    case P_ARP: arp.mask_pending = 1; break;
    case P_TUNE: tunefac = fm_exp2f((float)v * (1.0f / 1200.0f)); break;
    case P_ROOT:
        home = v;
        key_pending = v;
        break;
    case P_PROG: prog = v; break;
    case P_SOUND: sound_set(v); break;
    default: break;
    }
}

static void notes_off(void)
{
    lead.on = 0;
    lead.gate = 0.0f;
    bass.gate_left = 0.0f;
}

static void quiet(void)
{
    int i;
    notes_off();
    for (i = 0; i < NPAD; i++)
        pad[i].amp = 0.0f;
    bass.amp = arp.env = lead.amp = 0.0f;
    kick.amp = snare.body = snare.nse = hat.amp = crash.amp = tom.amp = riser = 0.0f;
    plate_init();
    gate_init();
    for (i = 0; i < DLY_N; i++)
        dly_l[i] = dly_r[i] = 0;
    gate_env = 0.0f;
}

static void restart(void)                       /* a new drive: the intro from the top, in the home key */
{
    sixteenths = 0;
    step16 = -1;
    step_left = 0.0f;
    key = home;
    key_pending = -1;
    drives = 0;
    breaks_since = 0;
    chord_k = -1;
    section = SEC_INTRO;
    sec_bar = 0;
    sec_len = sec_length(SEC_INTRO);
    lead.on = 0;
    lead.rest = 0;
    arp.mask_pending = 1;
    en_beats = 0.0f;
    en_key = (uint8_t)key;
    en_prog = (uint8_t)prog;
    en_section = SEC_INTRO;
    en_sec_bar = 0;
    en_sec_len = (uint8_t)sec_len;
}

void en_init(void)
{
    int i;
    for (i = 0; i <= SN; i++)
        sint[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SN));
    plate_init();
    gate_init();
    for (i = 0; i < DLY_N; i++)
        dly_l[i] = dly_r[i] = 0;
    dly_w = 0;
    q_r = q_w = 0;
    for (i = 0; i < NPAD; i++) {
        pad[i].note = 52 + 5 * i;
        pad[i].f = pad[i].f_t = mtof((float)pad[i].note);
        pad[i].ph[0] = 0.13f * (float)i;
        pad[i].ph[1] = 0.61f + 0.07f * (float)i;
        pad[i].amp = 0.0f;
    }
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, PARAMS[i].def);
    restart();
    dly_len = dly_len_t;
    for (i = 0; i < NLY; i++)
        ly[i] = ly_t[i] = 0.0f;
    en_playing = 0;
}

/* ------------------------------------------------------------ transport --- */
static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(int16_t)(c & 0xFFFFu);
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_SET:
            if (a < P_NPARAMS)
                apply(a, b);
            break;
        case C_PLAY:
            if (a && !en_playing) {
                en_playing = 1;
                if (sixteenths == 0)
                    step_left = 0.0f;
            } else if (!a && en_playing) {
                en_playing = 0;
                notes_off();
            }
            break;
        case C_NEXT: next_req = 1; break;
        case C_NEWKEY: newkey_req = 1; break;
        case C_NEWARP:
            arp.pat = (uint8_t)((arp.pat + 1) % NARP);
            en_arp_pat = arp.pat;
            break;
        case C_SEED:
            rng ^= (uint32_t)(b & 0xFFFF) * 2654435761u;
            if (!rng)
                rng = 1u;
            break;
        case C_PANIC: quiet(); break;
        case C_RESTART: restart(); break;
        }
    }
}

/* --------------------------------------------------------------- render --- */
#define CH 32                                    /* the control rate: a chunk */
static float dry_l[CH], dry_r[CH], s_dly[CH], s_rev[CH], s_gate[CH];

static void control(uint32_t n)
{
    int i;
    float k = (float)n * (1.0f / (0.6f * EN_SR)), sf, fc;
    float st = en_playing ? 1.0f : 0.0f;
    for (i = 0; i < NLY; i++)
        ly[i] += (ly_t[i] * st - ly[i]) * fm_minf(1.0f, k * (i == LY_DRUMS ? 4.0f : 1.0f));
    en_energy = 0.15f * ly[LY_PAD] + 0.35f * ly[LY_BASS] + 0.5f * ly[LY_DRUMS];
    sf = section == SEC_BREAK ? -0.9f : section == SEC_INTRO ? -0.6f + 0.6f * (float)sec_bar / (float)(sec_len ? sec_len : 1) :
         section == SEC_BUILD ? -0.4f + 0.8f * (float)sec_bar / (float)(sec_len ? sec_len : 1) : 0.0f;
    pad_lfo += (float)n * (FM_TWO_PI / (11.0f * EN_SR));
    if (pad_lfo > FM_TWO_PI)
        pad_lfo -= FM_TWO_PI;
    fc = 500.0f * fm_exp2f(2.6f * bright + 0.8f * sf + 0.35f * fm_sinf(pad_lfo) - (snd->pad == PAD_STRINGS ? 0.4f : 0.0f));
    svf_set(&pad_c, fc, 0.8f);
    for (i = 0; i < NPAD; i++) {
        pad_t *v = &pad[i];
        v->f += (v->f_t - v->f) * fm_minf(1.0f, (float)n * (1.0f / (0.03f * EN_SR)));
        v->dt[0] = v->f * (1.0f / EN_SR);
        v->dt[1] = v->f * pad_det * (1.0f / EN_SR);
    }
    /* bass: a pluck's filter */
    fc = 110.0f * fm_exp2f(1.6f * bright + 2.6f * bass.env * bass.vel + 0.3f * sf);
    svf_set(&bass.c, fc, 1.4f);
    /* arp */
    fc = 400.0f * fm_exp2f(3.0f * bright + 2.4f * arp.env + 0.7f * sf);
    svf_set(&arp.c, fc, snd->arpw == AW_BELL ? 0.7f : 1.6f);
    /* lead */
    if (lead.amp > 1e-5f || lead.gate > 0.0f) {
        float vib, f;
        lead.f += (lead.f_t - lead.f) * fm_minf(1.0f, (float)n * (1.0f / (0.05f * EN_SR)));
        lead.age += (float)n * (1.0f / EN_SR);
        lead.vib += (float)n * (FM_TWO_PI * 5.4f / EN_SR);
        if (lead.vib > FM_TWO_PI)
            lead.vib -= FM_TWO_PI;
        vib = 1.0f + 0.006f * fm_clampf((lead.age - 0.3f) * 2.0f, 0.0f, 1.0f) * fm_sinf(lead.vib);
        f = lead.f * vib;
        lead.dt[0] = f * (1.0f / EN_SR);
        lead.dt[1] = f * 1.005f * (1.0f / EN_SR);
        fc = 900.0f * fm_exp2f(2.2f * bright + 1.2f * lead.ienv);
        svf_set(&lead.c, fc, 0.9f);
        if (lead.note_left > 0.0f) {
            lead.note_left -= (float)n;
            if (lead.note_left <= 0.0f)
                lead.gate = 0.0f;
        }
    }
    /* riser: in a build, noise rising to the drop */
    if (section == SEC_BUILD && en_playing && par[P_DRUMS] > 0) {
        float f = ((float)sec_bar + (float)(step16 < 0 ? 0 : step16) / 16.0f) / (float)(sec_len ? sec_len : 1);
        riser_t = f < 0.95f ? 0.08f + 0.35f * f * f : 0.0f;
        svf_set(&riser_c, 300.0f * fm_exp2f(4.5f * f), 2.5f);
    } else {
        riser_t = 0.0f;
    }
    riser += (riser_t - riser) * fm_minf(1.0f, (float)n * (1.0f / (0.05f * EN_SR)));
    dly_len += (dly_len_t - dly_len) * fm_minf(1.0f, (float)n * (1.0f / (0.3f * EN_SR)));
    drum_lvl = (float)par[P_DRUMS] * 0.01f * ly[LY_DRUMS];
    pad_g = ly[LY_PAD];
    arp_g = ly[LY_ARP];
    bass_g = ly[LY_BASS];
    lead_g = 0.35f + 0.65f * (float)par[P_LEAD] * 0.01f;
    drums_g = drum_lvl;
    /* the glows */
    {
        float g = fm_expf(-(float)n * (1.0f / (0.12f * EN_SR)));
        en_kick *= g;
        en_snare *= g;
        en_hat *= g * g;
        en_arp_glow *= g;
        en_lead_glow *= fm_expf(-(float)n * (1.0f / (0.5f * EN_SR)));
        en_bass_glow *= g;
    }
    en_pad_level = pad[0].amp * pad_g;
}

static void render_pads(uint32_t n)
{
    const float atk = (snd->pad == PAD_STRINGS ? tc(0.9f) : tc(0.35f));
    const float tgt = en_playing ? 1.0f : 0.0f, rel = tc(1.2f);
    const float g = pad_g * (snd->pad == PAD_HOLLOW ? 0.10f : 0.12f);
    const float dk = decay_k(0.18f);
    uint32_t j;
    int i;
    if (pad[0].amp < 1e-5f && tgt == 0.0f && pad_g < 1e-4f)
        return;
    for (j = 0; j < n; j++) {
        float l = 0.0f, r = 0.0f, pump = 1.0f - 0.32f * duck * (drums_g > 0.0f ? 1.0f : 0.0f);
        duck *= dk;
        for (i = 0; i < NPAD; i++) {
            pad_t *v = &pad[i];
            float a, b;
            v->amp += (tgt - v->amp) * (tgt > v->amp ? atk : rel);
            a = saw(&v->ph[0], v->dt[0]);
            b = snd->pad == PAD_HOLLOW ? 0.7f * pulse(&v->ph[1], v->dt[1]) : saw(&v->ph[1], v->dt[1]);
            a *= v->amp;
            b *= v->amp;
            l += a * (0.5f - 0.5f * PAD_PAN[i]) + b * (0.5f + 0.3f * PAD_PAN[i]);
            r += a * (0.5f + 0.5f * PAD_PAN[i]) + b * (0.5f - 0.3f * PAD_PAN[i]);
        }
        l = svf_lp(&pad_fl, &pad_c, l) * g * pump;
        r = svf_lp(&pad_fr, &pad_c, r) * g * pump;
        dry_l[j] += l;
        dry_r[j] += r;
        s_rev[j] += (l + r) * 0.35f;
        s_dly[j] += (l + r) * 0.05f;
    }
    svf_flush(&pad_fl);
    svf_flush(&pad_fr);
}

static void render_bass(uint32_t n)
{
    const float dk = decay_k(0.16f), atk = tc(0.002f), rel = tc(0.025f), g = 0.30f * bass_g;
    uint32_t j;
    if (bass.amp < 1e-5f && bass.gate_left <= 0.0f)
        return;
    for (j = 0; j < n; j++) {
        float x, tgt = bass.gate_left > 0.0f ? bass.vel * (0.55f + 0.45f * bass.env) : 0.0f;
        bass.gate_left -= 1.0f;
        bass.env *= dk;
        bass.amp += (tgt - bass.amp) * (tgt > bass.amp ? atk : rel);
        x = saw(&bass.ph, bass.dt);
        bass.sph += bass.dt * 0.5f;
        if (bass.sph >= 1.0f)
            bass.sph -= 1.0f;
        x = svf_lp(&bass.fl, &bass.c, x * 0.7f) + 0.55f * sine(bass.sph);
        x *= bass.amp * g;
        dry_l[j] += x;
        dry_r[j] += x;
    }
    svf_flush(&bass.fl);
}

static void render_arp(uint32_t n)
{
    const float dk = decay_k(par[P_ARP] > 70 ? 0.11f : 0.18f), g = 0.26f * arp_g;
    uint32_t j;
    if (arp.env < 1e-4f)
        return;
    for (j = 0; j < n; j++) {
        float x;
        arp.env *= dk;
        if (snd->arpw == AW_BELL) {             /* two sines a fifth and an octave apart, struck */
            arp.ph[0] += arp.dt[0];
            if (arp.ph[0] >= 1.0f)
                arp.ph[0] -= 1.0f;
            arp.ph[1] += arp.dt[0] * 2.0f;
            if (arp.ph[1] >= 1.0f)
                arp.ph[1] -= 1.0f;
            x = sine(arp.ph[0] + 0.6f * arp.env * sine(arp.ph[1]) + 1.0f) * 0.9f;
        } else if (snd->arpw == AW_PULSE) {
            x = pulse(&arp.ph[0], arp.dt[0]) * 0.6f;
        } else {
            x = saw(&arp.ph[0], arp.dt[0]) + saw(&arp.ph[1], arp.dt[1]);
            x *= 0.75f;
        }
        x = svf_lp(&arp.fl, &arp.c, x) * arp.env * arp.vel * g;
        dry_l[j] += x * 0.8f;
        dry_r[j] += x * 0.8f;
        s_dly[j] += x * (0.4f + 0.012f * (float)snd->dly);
        s_rev[j] += x * 0.4f;
    }
    svf_flush(&arp.fl);
}

static void render_lead(uint32_t n)
{
    const float atk = tc(0.02f), rel = tc(0.18f), idk = decay_k(0.25f), g = 0.27f * lead_g;
    uint32_t j;
    if (lead.amp < 1e-5f && lead.gate <= 0.0f)
        return;
    for (j = 0; j < n; j++) {
        float x;
        lead.amp += (lead.gate - lead.amp) * (lead.gate > lead.amp ? atk : rel);
        lead.ienv *= idk;
        if (snd->lead == LD_BRASS) {            /* two operators, the brightness swelling then settling */
            lead.ph[0] += lead.dt[0];
            if (lead.ph[0] >= 1.0f)
                lead.ph[0] -= 1.0f;
            lead.ph[1] += lead.dt[1];
            if (lead.ph[1] >= 1.0f)
                lead.ph[1] -= 1.0f;
            float b = 4.0f * lead.ienv * (1.0f - lead.ienv);   /* 0 at the start, 1 a moment in, then settling */
            x = sine(lead.ph[0] + (0.13f + 0.3f * b) * sine(lead.ph[1]) + 1.0f) * 0.6f;
        } else if (snd->lead == LD_SQUARE) {
            x = pulse(&lead.ph[0], lead.dt[0]) * 0.42f;
        } else {
            x = (saw(&lead.ph[0], lead.dt[0]) + saw(&lead.ph[1], lead.dt[1])) * 0.55f;
        }
        x = svf_lp(&lead.fl, &lead.c, x) * lead.amp * g;
        dry_l[j] += x * 0.75f;
        dry_r[j] += x * 0.75f;
        s_dly[j] += x * (0.3f + 0.008f * (float)snd->dly);
        s_rev[j] += x * 0.7f;
    }
    svf_flush(&lead.fl);
}

static void render_drums(uint32_t n)
{
    const float g = drums_g * 0.46f;
    const float kdk = decay_k(0.32f), kfk = decay_k(0.028f), bdk = decay_k(0.09f), ndk = decay_k(0.16f);
    const float tdk = decay_k(0.28f), tfk = decay_k(0.12f);
    uint32_t j;
    if (kick.amp < 1e-4f && snare.body < 1e-4f && snare.nse < 1e-4f && hat.amp < 1e-4f && crash.amp < 1e-4f &&
        tom.amp < 1e-4f && riser < 1e-4f)
        return;
    for (j = 0; j < n; j++) {
        float x = 0.0f, y, sn = 0.0f, h;
        if (kick.amp > 1e-4f) {
            kick.ph += (44.0f + 120.0f * kick.f) * (1.0f / EN_SR);
            if (kick.ph >= 1.0f)
                kick.ph -= 1.0f;
            kick.f *= kfk;
            kick.amp *= kdk;
            y = sine(kick.ph) * kick.amp;
            x += y * 1.1f;
        }
        if (snare.body > 1e-4f || snare.nse > 1e-4f) {
            float w = noise();
            snare.ph += 190.0f * (1.0f / EN_SR);
            if (snare.ph >= 1.0f)
                snare.ph -= 1.0f;
            snare.body *= bdk;
            snare.nse *= ndk;
            snare.hp += 0.25f * (w - snare.hp);       /* (the noise above ~2 kHz, a little dulled) */
            snare.lp += 0.6f * ((w - snare.hp) - snare.lp);
            sn = sine(snare.ph) * snare.body * 0.5f + snare.lp * snare.nse * 0.75f;
            x += sn * 0.6f;
        }
        if (hat.amp > 1e-4f || crash.amp > 1e-4f) {
            float w = noise(), a = hat.amp + crash.amp * 0.6f;
            hat.amp *= hat.dk;
            crash.amp *= crash.dk;
            hat.hp1 += 0.55f * (w - hat.hp1);
            h = w - hat.hp1;
            hat.hp2 += 0.4f * (h - hat.hp2);
            h = (h - hat.hp2) * a * 0.32f;
            dry_l[j] += h * g * 0.9f;
            dry_r[j] += h * g * 1.1f;
            s_rev[j] += h * g * 0.25f;
        }
        if (tom.amp > 1e-4f) {
            tom.ph += tom.f0 * (0.7f + 0.3f * tom.f) * (1.0f / EN_SR);
            if (tom.ph >= 1.0f)
                tom.ph -= 1.0f;
            tom.f *= tfk;
            tom.amp *= tdk;
            y = sine(tom.ph) * tom.amp * 0.7f;
            x += y;
            s_gate[j] += y * 0.5f * g;
        }
        if (riser > 1e-4f)
            x += svf_bp(&riser_f, &riser_c, noise()) * riser;
        dry_l[j] += x * g;
        dry_r[j] += x * g;
        s_gate[j] += sn * g * (0.4f + 0.01f * (float)snd->gate);
        s_rev[j] += sn * g * 0.2f;
        s_dly[j] += sn * g * 0.06f;
    }
    hat.hp1 = fm_flush(hat.hp1);
    hat.hp2 = fm_flush(hat.hp2);
    snare.hp = fm_flush(snare.hp);
    snare.lp = fm_flush(snare.lp);
    svf_flush(&riser_f);
}

static float lim_env;
static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    const float fb = 0.32f + 0.002f * (float)snd->dly, dmix = 0.35f + 0.006f * (float)snd->dly;
    const float rmix = 0.25f + 0.007f * (float)snd->rev, gclose = decay_k(0.012f), lrel = decay_k(0.25f);
    const float T = 0.62f;
    for (j = 0; j < n; j++)
        dry_l[j] = dry_r[j] = s_dly[j] = s_rev[j] = s_gate[j] = 0.0f;
    render_pads(n);
    render_bass(n);
    render_arp(n);
    render_lead(n);
    render_drums(n);
    for (j = 0; j < n; j++) {
        float dl_, dr_, rl, rr, gl, gr, l, r, m, g;
        dl_ = dly_read(dly_l, dly_len);
        dr_ = dly_read(dly_r, dly_len);
        dly_lp_l += 0.35f * (dl_ - dly_lp_l);
        dly_lp_r += 0.35f * (dr_ - dly_lp_r);
        dly_l[dly_w] = s16(s_dly[j] + dly_lp_r * fb);   /* ping-pong: each side feeds the other */
        dly_r[dly_w] = s16(dly_lp_l * fb);
        if (++dly_w >= DLY_N)
            dly_w = 0;
        reverb((s_rev[j] + (dl_ + dr_) * 0.15f) * 0.5f, &rl, &rr);
        gated(s_gate[j], &gl, &gr);
        if (gate_hold > 0.0f)
            gate_hold -= 1.0f;
        else
            gate_env *= gclose;
        l = dry_l[j] + dl_ * dmix + rl * rmix + gl * gate_env * 0.55f;
        r = dry_r[j] + dr_ * dmix + rr * rmix + gr * gate_env * 0.55f;
        l *= gain;
        r *= gain;
        /* the limiter: no attack time (the envelope jumps to a peak), a quarter second's release */
        m = fm_maxf(fm_fabsf(l), fm_fabsf(r));
        lim_env = m > lim_env ? m : lim_env * lrel + m * (1.0f - lrel);
        g = lim_env > T ? T / lim_env : 1.0f;
        out[2 * j] = (int32_t)(fm_tanhf(l * g) * 7900000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r * g) * 7900000.0f);
    }
    en_gr = lim_env > T ? T / lim_env : 1.0f;
    dly_lp_l = fm_flush(dly_lp_l);
    dly_lp_r = fm_flush(dly_lp_r);
    pl_damp_l = fm_flush(pl_damp_l);
    pl_damp_r = fm_flush(pl_damp_r);
    pl_bw = fm_flush(pl_bw);
    lim_env = fm_flush(lim_env);
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    drain();
    while (n) {
        uint32_t k = n > CH ? CH : n;
        if (en_playing) {
            if (step_left <= 0.0f) {
                step();
                step_left += step_len;
            }
            if ((float)k > step_left)
                k = (uint32_t)step_left + 1u;
            if (k > n)
                k = n;
            step_left -= (float)k;
            en_beats = ((float)((sixteenths + 63u) % 64u) + fm_clampf(1.0f - step_left / step_len, 0.0f, 1.0f)) * 0.25f;
        }
        control(k);
        render_chunk(out, k, gain);
        out += 2 * k;
        n -= k;
    }
}
