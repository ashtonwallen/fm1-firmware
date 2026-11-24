/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (AMB-1: the harmony helpers, the plate, the command queue)
 * Copyright (C) 2026 ashtonwallen (DECAY LOOPS) */
/* The instrument (decay.h). One compilation unit, -O2. */
#include "decay.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const char *const EN_MODE_NAME[EN_NMODES] = {"Ionian", "Dorian", "Phrygian", "Lydian", "Mixolyd.", "Aeolian",
                                             "Pent maj", "Pent min"};
#define B(a, b, c, d, e, f, g) (1u << (a) | 1u << (b) | 1u << (c) | 1u << (d) | 1u << (e) | 1u << (f) | 1u << (g))
const uint16_t EN_MODE_POOL[EN_NMODES] = {
    B(0, 2, 4, 5, 7, 9, 11), B(0, 2, 3, 5, 7, 9, 10), B(0, 1, 3, 5, 7, 8, 10), B(0, 2, 4, 6, 7, 9, 11),
    B(0, 2, 4, 5, 7, 9, 10), B(0, 2, 3, 5, 7, 8, 10), B(0, 2, 4, 7, 9, 9, 9),  B(0, 3, 5, 7, 10, 10, 10)};
#undef B
const char *const EN_STOCK_NAME[EN_NSTOCK] = {"Cassette", "Reel", "Wax", "Wire"};
const char *const EN_VOICE_NAME[EN_NVOICE] = {"Piano", "Choir", "Strings", "Brass", "Organ", "Glass", "Bells"};
const char *const EN_PHRASE_NAME[EN_NPHRASE] = {"Chords", "Figure", "Melody", "Mixed"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Decay", 0, 100, 40},   {"Length", 4, 12, 8},    {"Layers", 1, 4, 2},     {"Wow", 0, 100, 40},
    {"Stock", 0, EN_NSTOCK - 1, 0}, {"Tone", 0, 100, 50}, {"Hiss", 0, 100, 40}, {"Space", 0, 100, 55},
    {"Voice", 0, EN_NVOICE - 1, 0}, {"Phrase", 0, EN_NPHRASE - 1, 0}, {"Density", 0, 100, 50}, {"Octave", -2, 2, 0},
    {"Root", 0, 11, 2},      {"Mode", 0, EN_NMODES - 1, 0}, {"Tune", -50, 50, 0},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

/* the presets: a tape stock and a mood each.
 *                    Dec Len Lay Wow  Stock        Ton His Spc  Voice       Phrase      Den Oct Root Mode        Tune */
static const struct {
    const char *name;
    int8_t v[P_NPARAMS];
} PRESETS[EN_NPRESETS] = {
    {"Cassette",     {45, 6, 2, 40, ST_CASSETTE, 55, 40, 45, VX_PIANO, PH_CHORDS, 50, 0, 2, MODE_IONIAN, 0}},
    {"Reel to Reel", {30, 10, 2, 30, ST_REEL, 70, 25, 60, VX_STRINGS, PH_CHORDS, 40, 0, 5, MODE_IONIAN, 0}},
    {"Wax",          {50, 7, 2, 55, ST_WAX, 35, 65, 45, VX_GLASS, PH_MELODY, 45, 0, 9, MODE_AEOLIAN, 0}},
    {"Wire",         {65, 5, 3, 70, ST_WIRE, 50, 50, 50, VX_BELLS, PH_FIGURE, 60, 0, 4, MODE_DORIAN, 0}},
    {"Fog",          {25, 12, 3, 35, ST_REEL, 25, 30, 85, VX_CHOIR, PH_CHORDS, 25, -1, 0, MODE_AEOLIAN, 0}},
    {"Choir",        {40, 9, 2, 30, ST_CASSETTE, 50, 30, 70, VX_CHOIR, PH_CHORDS, 45, 0, 7, MODE_DORIAN, 0}},
    {"Brass Memory", {35, 8, 2, 45, ST_REEL, 50, 35, 55, VX_BRASS, PH_CHORDS, 55, 0, 10, MODE_IONIAN, 0}},
    {"Music Box",    {55, 6, 2, 50, ST_CASSETTE, 60, 35, 50, VX_BELLS, PH_FIGURE, 70, 1, 0, MODE_IONIAN, 0}},
    {"Hymn",         {30, 10, 2, 25, ST_REEL, 45, 30, 75, VX_ORGAN, PH_CHORDS, 30, 0, 5, MODE_IONIAN, 0}},
    {"Dust",         {60, 6, 3, 45, ST_WAX, 40, 70, 40, VX_PIANO, PH_FIGURE, 45, 0, 4, MODE_AEOLIAN, 0}},
    {"Snowfield",    {20, 11, 3, 20, ST_REEL, 60, 20, 80, VX_GLASS, PH_CHORDS, 30, 1, 9, MODE_LYDIAN, 0}},
    {"Lantern",      {40, 8, 2, 35, ST_CASSETTE, 55, 35, 55, VX_STRINGS, PH_MIXED, 50, 0, 2, MODE_DORIAN, 0}},
    {"Low Sun",      {35, 9, 2, 40, ST_CASSETTE, 40, 40, 60, VX_BRASS, PH_MELODY, 40, -1, 3, MODE_MIXOLYDIAN, 0}},
    {"Night Wire",   {70, 5, 4, 60, ST_WIRE, 35, 55, 65, VX_CHOIR, PH_FIGURE, 50, 0, 11, MODE_PHRYGIAN, 0}},
};
const char *en_preset_name(int i) { return PRESETS[(unsigned)i < EN_NPRESETS ? i : 0].name; }
void en_preset(int i, int16_t *p)
{
    int k;
    i = (unsigned)i < EN_NPRESETS ? i : 0;
    for (k = 0; k < P_NPARAMS; k++)
        p[k] = PRESETS[i].v[k];
}

/* the stocks: how each tape sounds and falls apart */
static const struct {
    float bw;                                   /* its top, times Tone's */
    float hp;                                   /* its bottom (Hz) fresh; it rises with wear */
    float hiss, crackle, wow, flutter, dips;
    uint8_t seeds, seeds_n;                     /* holes: at least, and up to this many more */
    float spread;                               /* how quickly they grow */
    float nlp;                                  /* the hiss's colour (Hz) */
} STOCK[EN_NSTOCK] = {
    {1.0f, 45.0f, 1.0f, 0.5f, 1.0f, 1.0f, 1.0f, 5, 5, 1.0f, 5200.0f},     /* Cassette */
    {1.25f, 30.0f, 0.6f, 0.25f, 0.8f, 0.5f, 0.6f, 3, 3, 0.7f, 3600.0f},   /* Reel: few, large patches */
    {0.62f, 120.0f, 0.9f, 2.6f, 1.4f, 0.6f, 1.4f, 8, 6, 1.3f, 2600.0f},   /* Wax: narrow, crackling */
    {0.85f, 170.0f, 1.2f, 0.9f, 0.9f, 2.6f, 2.0f, 9, 8, 1.6f, 7000.0f},   /* Wire: thin, fluttering */
};

static char *itoa_s(int v, char *b)
{
    char t[8];
    int n = 0, neg = v < 0;
    if (neg)
        v = -v;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 6);
    if (neg)
        *b++ = '-';
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

float en_life_secs(int v) { return 900.0f * fm_exp2f((float)v * (-1.0f / 22.0f)); }   /* 15 min .. 38 s */

void en_param_text(int i, int v, char *b)
{
    const char *s = 0;
    switch (i) {
    case P_STOCK: s = EN_STOCK_NAME[(unsigned)v < EN_NSTOCK ? v : 0]; break;
    case P_VOICE: s = EN_VOICE_NAME[(unsigned)v < EN_NVOICE ? v : 0]; break;
    case P_PHRASE: s = EN_PHRASE_NAME[(unsigned)v < EN_NPHRASE ? v : 0]; break;
    case P_MODE: s = EN_MODE_NAME[(unsigned)v < EN_NMODES ? v : 0]; break;
    case P_ROOT: s = EN_NOTE_NAME[(unsigned)v < 12u ? v : 0]; break;
    default: break;
    }
    if (s) {
        cat(b, s);
        return;
    }
    if (i == P_DECAY) {                         /* a loop's life */
        int sec = (int)(en_life_secs(v) + 0.5f);
        if (sec < 90)
            cat(itoa_s(sec, b), " s");
        else
            cat(itoa_s((sec + 30) / 60, b), " min");
        return;
    }
    if (i == P_LENGTH) {
        cat(itoa_s(v, b), " s");
        return;
    }
    if ((i == P_TUNE || i == P_OCT) && v > 0)
        *b++ = '+';
    itoa_s(v, b);
}

/* ------------------------------------------------------- the pure parts (AMB-1's) --- */
int en_scale(uint32_t pool, int *steps)
{
    int i, m = 0;
    pool |= 1u;
    for (i = 0; i < 12; i++)
        if (pool >> i & 1u)
            steps[m++] = i;
    return m;
}

int en_chord_tones(uint32_t pool, int root, int deg, int *pcs)
{
    static const uint8_t OFF[7] = {0, 2, 4, 6, 1, 3, 5};   /* stacked thirds, then what's left */
    int st[12], m = en_scale(pool, st), want, n = 0, i, j;
    want = m >= 6 ? 4 : 3;
    deg = ((deg % m) + m) % m;
    for (i = 0; i < 7 && n < want; i++) {
        int pc = (root + st[(deg + OFF[i]) % m]) % 12, dup = 0;
        for (j = 0; j < n; j++)
            dup |= pcs[j] == pc;
        if (!dup)
            pcs[n++] = pc;
    }
    return n;
}

static int nearest(int from, int pc, int lo, int hi)    /* the note of class pc nearest from, in lo..hi */
{
    int n = from + ((pc - from) % 12 + 12) % 12;
    if (n - from > 6)
        n -= 12;
    while (n < lo)
        n += 12;
    while (n > hi)
        n -= 12;
    return n;
}

void en_voice_lead(const int *from, const int *pcs, int nt, int *to, int lo, int hi)
{
    static const uint8_t PERM[24][4] = {
        {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
        {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
        {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
        {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0}};
    int t[4], p, v, best = 1 << 30;
    for (v = 0; v < 4; v++)
        t[v] = pcs[v < nt ? v : 0];
    for (p = 0; p < 24; p++) {
        int c = 0, n[4], a, b;
        for (v = 0; v < 4; v++) {
            n[v] = nearest(from[v], t[PERM[p][v]], lo, hi);
            c += n[v] > from[v] ? n[v] - from[v] : from[v] - n[v];
        }
        for (a = 0; a < 4; a++)
            for (b = a + 1; b < 4; b++)
                if (n[a] == n[b])
                    c += 40;
        if (c < best) {
            best = c;
            for (v = 0; v < 4; v++)
                to[v] = n[v];
        }
    }
}

const char *en_chord_name(const int *pcs, int n, char *buf)
{
    uint32_t iv = 0;
    int i, k = 0, third, fifth;
    const char *q = "", *r = EN_NOTE_NAME[pcs[0] % 12];
    for (i = 1; i < n; i++)
        iv |= 1u << ((pcs[i] - pcs[0] + 12) % 12);
    third = iv >> 4 & 1u ? 4 : iv >> 3 & 1u ? 3 : 0;
    fifth = iv >> 7 & 1u ? 7 : iv >> 6 & 1u ? 6 : iv >> 8 & 1u ? 8 : 0;
    if (third == 4) {
        q = fifth == 8 ? "aug" : iv >> 11 & 1u ? "M7" : iv >> 10 & 1u ? "7" : iv >> 9 & 1u ? "6"
            : iv >> 2 & 1u ? "add9" : "";
    } else if (third == 3) {
        if (fifth == 6)
            q = iv >> 10 & 1u ? "m7b5" : iv >> 9 & 1u ? "dim7" : "dim";
        else
            q = iv >> 10 & 1u ? "m7" : iv >> 11 & 1u ? "mM7" : iv >> 9 & 1u ? "m6" : iv >> 2 & 1u ? "madd9" : "m";
    } else {
        q = iv >> 5 & 1u ? (iv >> 10 & 1u ? "7sus4" : "sus4") : iv >> 2 & 1u ? "sus2" : "5";
    }
    while (*r)
        buf[k++] = *r++;
    while (*q)
        buf[k++] = *q++;
    buf[k] = 0;
    return buf;
}

/* mu-law (ITU-T G.711), on a 16-bit scale: the tape's bytes. Quiet passages keep their detail; the grain it adds
 * is about 38 dB under the signal, which a worn tape's hiss soon covers */
uint8_t en_mulaw(float x)
{
    int32_t s = (int32_t)(x * 32000.0f), sign = 0, e, m;
    if (s < 0) {
        sign = 0x80;
        s = -s;
    }
    if (s > 32635)
        s = 32635;
    s += 0x84;
    for (e = 7; e > 0 && !(s & (0x80 << e)); e--)
        ;
    m = (s >> (e + 3)) & 0x0F;
    return (uint8_t)~(sign | e << 4 | m);
}
float en_unmulaw(uint8_t u)
{
    int32_t t;
    u = (uint8_t)~u;
    t = ((u & 0x0F) << 3) + 0x84;
    t <<= (u & 0x70) >> 4;
    t = (u & 0x80) ? (0x84 - t) : (t - 0x84);
    return (float)t * (1.0f / 32000.0f);
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_PLAY, C_FREEZE, C_NEW, C_REGROW, C_NOTE, C_SEED, C_PANIC };
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
void en_play(int on) { post(C_PLAY, on, 0); }
void en_freeze(int on) { post(C_FREEZE, on, 0); }
void en_new(void) { post(C_NEW, 0, 0); }
void en_regrow(void) { post(C_REGROW, 0, 0); }
void en_note(int note, int vel) { post(C_NOTE, note, vel); }
void en_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
void en_panic(void) { post(C_PANIC, 0, 0); }

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_frozen;
volatile uint8_t en_lay_state[EN_NLAYER];
volatile uint16_t en_lay_serial[EN_NLAYER];
volatile uint8_t en_lay_voice[EN_NLAYER];
volatile float en_lay_wear[EN_NLAYER], en_lay_pos[EN_NLAYER], en_lay_rec[EN_NLAYER], en_lay_level[EN_NLAYER];
volatile float en_lay_gain[EN_NLAYER], en_lay_secs[EN_NLAYER];
volatile uint8_t en_lay_dmg[EN_NLAYER][EN_NCELL];
volatile uint32_t en_passes[EN_NLAYER];
volatile float en_tape_used, en_speed, en_out_level;
volatile uint32_t en_loops, en_flakes;
volatile uint8_t en_prog_n, en_prog_pcs[4][4], en_prog_nt[4];

static int16_t par[P_NPARAMS];
static uint32_t pool = 0xAB5u;
static int nsteps, steps[12];
static float tunefac = 1.0f, tone = 0.5f, wowp = 0.4f, hissp = 0.4f, space = 0.55f;
static uint32_t rng = 0x9E3779B9u;

static uint32_t rnd(void)                       /* Marsaglia's xorshift (Xorshift RNGs, 2003): shifts 13, 17, 5 */
{
    uint32_t x = rng;
    x = x ^ (x << 13);
    x = x ^ (x >> 17);
    rng = x ^ (x << 5);
    return rng;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static int irand(int n) { return n > 0 ? (int)((rnd() >> 8) % (uint32_t)n) : 0; }

static float mtof(float n) { return 440.0f * tunefac * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }

#define SN 1024
static float sint[SN + 1];
static inline float sine(float ph)              /* ph in turns, any value >= 0 */
{
    float x = (ph - (float)(int)ph) * (float)SN;
    int i = (int)x;
    float f = x - (float)i;
    return sint[i] + (sint[i + 1] - sint[i]) * f;
}
static float MU[256];                           /* the tape's bytes, decoded */

static void svf_coefs(float *c, float f, float k)   /* a TPT state-variable filter (Zavalishin) */
{
    float g = fm_tanf(FM_PI * fm_clampf(f, 30.0f, 16000.0f) * (1.0f / EN_SR));
    c[0] = 1.0f / (1.0f + g * (g + k));
    c[1] = g * c[0];
    c[2] = g * c[1];
}
static inline float blep_saw(float t, float dt)
{
    float y = 2.0f * t - 1.0f;
    if (t < dt) {
        float x = t / dt;
        y -= x + x - x * x - 1.0f;
    } else if (t > 1.0f - dt) {
        float x = (t - 1.0f) / dt;
        y -= x * x + x + x + 1.0f;
    }
    return y;
}

/* ------------------------------------------------------------- the synth --- */
/* A phrase's instruments, one shared set of voices: what they play while a loop records goes onto it. */
#define NVOX 12
typedef struct {
    float p0, p1, i0, i1;                       /* phases, increments (turns a sample) */
    float s0, s1, s2, s3;                       /* the filters' states */
    float c1[3], c2[3];                         /* their coefficients */
    float env, ak, dk, sus, rk, amp;
    float idx, idk;                             /* an FM index and its fall */
    float note, vph, age;
    int32_t off_at;                             /* the phrase clock at which its key goes up (live: never) */
    uint8_t on, stage, type, live, key;
} vox_t;
static vox_t vox[NVOX];
static const struct {
    float att, dec, sus, rel, gain;
} VENV[EN_NVOICE] = {
    {0.004f, 4.5f, 0.0f, 0.45f, 0.50f},         /* piano: an FM electric piano */
    {0.50f, 1.0f, 1.0f, 1.3f, 0.36f},           /* choir: saws through two formants */
    {0.55f, 1.0f, 1.0f, 1.0f, 0.22f},           /* strings: two saws, a low-pass */
    {0.12f, 1.2f, 0.72f, 0.45f, 0.32f},         /* brass: a saw, a low-pass that opens with the breath */
    {0.04f, 1.0f, 1.0f, 0.25f, 0.15f},          /* organ: four drawbars and a chorus */
    {0.30f, 6.0f, 0.55f, 1.6f, 0.34f},          /* glass: two sines a breath apart, beating */
    {0.001f, 3.0f, 0.0f, 0.9f, 0.70f},          /* bells: FM, a music box's tines */
};
static const float VOWEL[4][2] = {{320.0f, 800.0f}, {480.0f, 900.0f}, {730.0f, 1090.0f}, {530.0f, 1840.0f}};

static void vox_coefs(vox_t *v)                 /* every 32 samples: pitch, vibrato, the filters */
{
    float vib = 0.0f, f;
    v->age += 32.0f / EN_SR;
    v->vph += 32.0f * 5.1f / EN_SR;
    if (v->vph >= 1.0f)
        v->vph -= 1.0f;
    if (v->type == VX_CHOIR)
        vib = 0.11f;
    else if (v->type == VX_STRINGS || v->type == VX_BRASS)
        vib = 0.08f * fm_clampf(v->age - 0.5f, 0.0f, 1.0f);
    f = mtof(v->note + vib * sine(v->vph));
    switch (v->type) {
    case VX_CHOIR: {
        float vw = fm_clampf(tone * 0.9f + 0.05f, 0.0f, 1.0f) * 2.999f, f1, f2;
        int a = (int)vw;
        vw -= (float)a;
        f1 = VOWEL[a][0] + (VOWEL[a + 1][0] - VOWEL[a][0]) * vw;
        f2 = VOWEL[a][1] + (VOWEL[a + 1][1] - VOWEL[a][1]) * vw;
        svf_coefs(v->c1, f1, 0.45f);
        svf_coefs(v->c2, f2, 0.3f);
        v->i0 = f * 0.99856f * (1.0f / EN_SR);
        v->i1 = f * 1.00289f * (1.0f / EN_SR);
        break;
    }
    case VX_STRINGS:
        svf_coefs(v->c1, f * (1.4f + 3.0f * tone) + 250.0f + 600.0f * v->env, 0.9f);
        v->i0 = f * 0.99596f * (1.0f / EN_SR);
        v->i1 = f * 1.00405f * (1.0f / EN_SR);
        break;
    case VX_BRASS:
        svf_coefs(v->c1, f * (1.0f + (1.5f + 5.0f * tone) * v->env * v->env) + 120.0f, 0.85f);
        v->i0 = f * (1.0f / EN_SR);
        v->i1 = f * 1.0023f * (1.0f / EN_SR);
        break;
    case VX_ORGAN:
        v->i0 = f * (1.0f / EN_SR);
        v->i1 = f * 1.0035f * (1.0f / EN_SR);
        break;
    case VX_GLASS:
        v->i0 = f * (1.0f / EN_SR);
        v->i1 = (f + 0.9f) * (1.0f / EN_SR);
        break;
    case VX_BELLS:
        v->i0 = f * (1.0f / EN_SR);
        v->i1 = f * 3.5f * (1.0f / EN_SR);
        break;
    default:
        v->i0 = f * (1.0f / EN_SR);
        break;
    }
}

static void vox_on(int note, int vel, int type, int live, int32_t off_at)
{
    int i, best = 0;
    float lo = 9.0f, a;
    vox_t *v;
    for (i = 0; i < NVOX; i++) {                /* a free voice, else the quietest (a releasing one first) */
        a = !vox[i].on ? -1.0f : vox[i].env * (vox[i].stage == 3 ? 0.5f : 1.0f) + (vox[i].live ? 2.0f : 0.0f);
        if (a < lo) {
            lo = a;
            best = i;
        }
    }
    v = &vox[best];
    if (note < 24 || note > 108)
        return;
    if (!v->on || v->env < 0.01f)
        v->env = 0.0f;
    v->env *= 0.5f;                              /* a stolen voice: from where it was, quieter */
    v->on = 1;
    v->stage = 1;
    v->type = (uint8_t)type;
    v->live = (uint8_t)live;
    v->key = (uint8_t)note;
    v->note = (float)note;
    v->off_at = off_at;
    v->amp = fm_powf((float)vel * (1.0f / 127.0f), 1.4f) * VENV[type].gain;
    v->ak = 1.0f / (VENV[type].att * EN_SR);
    v->dk = 1.0f - fm_expf(-6.9f / (VENV[type].dec * EN_SR));
    v->sus = VENV[type].sus;
    v->rk = fm_expf(-6.9f / (VENV[type].rel * EN_SR));
    v->p0 = 0.0f;
    v->p1 = 0.31f;
    v->s0 = v->s1 = v->s2 = v->s3 = 0.0f;
    v->age = 0.0f;
    v->vph = frand();
    if (type == VX_PIANO) {                     /* the tine's bloom: a bright strike falling to a round tone */
        v->idx = (0.12f + 0.22f * tone) * (0.5f + 0.6f * (float)vel / 127.0f);
        v->idk = fm_expf(-6.9f / (0.9f * EN_SR));
    } else if (type == VX_BELLS) {
        v->idx = (0.25f + 0.5f * tone) * (0.6f + 0.4f * (float)vel / 127.0f);
        v->idk = fm_expf(-6.9f / (0.5f * EN_SR));
    } else {
        v->idx = 0.0f;
        v->idk = 1.0f;
    }
    vox_coefs(v);
}

static void vox_off_key(int note)               /* a key up: its live voices let go */
{
    int i;
    for (i = 0; i < NVOX; i++)
        if (vox[i].on && vox[i].live && vox[i].key == note && vox[i].stage < 3)
            vox[i].stage = 3;
}

/* the voices into syn (the phrase's) and liv (the keys'): n samples */
#define BLK 256
static float syn[BLK], liv[BLK];
static void synth_render(uint32_t n, int32_t clock)
{
    int i;
    uint32_t j;
    for (j = 0; j < n; j++)
        syn[j] = liv[j] = 0.0f;
    for (i = 0; i < NVOX; i++) {
        vox_t *v = &vox[i];
        float *o = v->live ? liv : syn, p0 = v->p0, p1 = v->p1, s0 = v->s0, s1 = v->s1, s2 = v->s2, s3 = v->s3;
        float env = v->env, idx = v->idx;
        if (!v->on)
            continue;
        if (!v->live && v->stage < 3 && clock >= v->off_at)
            v->stage = 3;
        for (j = 0; j < n; j++) {
            float x, v1, v2, v3;
            if (!((j + (uint32_t)i) & 31u)) {
                v->env = env;
                vox_coefs(v);
            }
            switch (v->type) {
            case VX_PIANO:
                x = sine(p0 + sine(p0) * idx + 1.0f) + 0.1f * sine(2.0f * p0);
                idx *= v->idk;
                break;
            case VX_BELLS:
                x = sine(p0 + sine(p1) * idx + 4.0f);
                idx *= v->idk;
                break;
            case VX_CHOIR: {
                float b1, b2, src = (p0 < 0.5f ? 4.0f * p0 - 1.0f : 3.0f - 4.0f * p0) * 1.3f +
                                    0.4f * blep_saw(p0, v->i0) + 0.35f * blep_saw(p1, v->i1);
                v3 = src - s1;
                v1 = v->c1[0] * s0 + v->c1[1] * v3;
                v2 = s1 + v->c1[1] * s0 + v->c1[2] * v3;
                s0 = 2.0f * v1 - s0;
                s1 = 2.0f * v2 - s1;
                b1 = v1 * 0.45f;
                x = v2 * 0.45f;
                v3 = src - s3;
                v1 = v->c2[0] * s2 + v->c2[1] * v3;
                v2 = s3 + v->c2[1] * s2 + v->c2[2] * v3;
                s2 = 2.0f * v1 - s2;
                s3 = 2.0f * v2 - s3;
                b2 = v1 * 0.3f;
                x += b1 + 0.5f * b2;
                break;
            }
            case VX_STRINGS:
            case VX_BRASS: {
                float src = blep_saw(p0, v->i0) + (v->type == VX_STRINGS ? 1.0f : 0.5f) * blep_saw(p1, v->i1);
                v3 = src - s1;
                v1 = v->c1[0] * s0 + v->c1[1] * v3;
                v2 = s1 + v->c1[1] * s0 + v->c1[2] * v3;
                s0 = 2.0f * v1 - s0;
                s1 = 2.0f * v2 - s1;
                x = v2;
                break;
            }
            case VX_ORGAN:
                x = sine(p0) + 0.55f * sine(2.0f * p0) + 0.3f * sine(3.0f * p0) + 0.12f * sine(4.0f * p0) +
                    0.4f * sine(p1);
                break;
            default:                             /* glass */
                x = sine(p0) + 0.5f * sine(p1) + 0.12f * sine(2.0f * p0);
                break;
            }
            p0 += v->i0;
            if (p0 >= 1.0f)
                p0 -= 1.0f;
            p1 += v->i1;
            if (p1 >= 1.0f)
                p1 -= 1.0f;
            if (v->stage == 1) {
                env += v->ak;
                if (env >= 1.0f) {
                    env = 1.0f;
                    v->stage = 2;
                }
            } else if (v->stage == 2) {
                env += (v->sus - env) * v->dk;
            } else {
                env *= v->rk;
            }
            o[j] += x * env * v->amp;
        }
        v->p0 = p0;
        v->p1 = p1;
        v->s0 = fm_flush(s0);
        v->s1 = fm_flush(s1);
        v->s2 = fm_flush(s2);
        v->s3 = fm_flush(s3);
        v->idx = idx;
        v->env = env;
        if ((v->stage == 3 || v->sus <= 0.0f) && env < 1e-4f)
            v->on = 0;
    }
}

/* ------------------------------------------------------------ the tape --- */
static uint8_t tape[EN_TAPE_N] EN_POOL;
typedef struct {
    uint8_t state, voice;
    uint32_t serial;
    uint32_t start, len;                        /* on the tape, samples */
    float pos, rate;                            /* the playhead (tape samples), its speed (tape samples a sample) */
    float wear;
    float gain, gtgt, gstep;                    /* its fade */
    float l1, l2, hp, cl, ch;                   /* the worn tape's tone: two low-passes and a high-pass */
    float wph, wf, fph, ff, drift, drift_t;     /* wow, flutter, a slow drift */
    uint32_t drift_left, passes;
    float pl, pr;                               /* its place */
    float dip, dip_t;                           /* a moment off the head */
    uint32_t dip_left;
    float cellk;                                /* wear cells a tape sample */
    float g_wear;                               /* the wear the cells' gains are for */
    float lvl;
    float G[EN_NCELL + 2];                      /* each cell's gain (the first again at the end) */
    uint8_t th[EN_NCELL];                       /* the wear at which each cell starts to go (x 255) */
} layer_t;
static layer_t lay[EN_NLAYER];
static uint32_t serial_next = 1;
#define LAY_G 0.5f                              /* a loop's level */
#define REC_DRIVE 2.6f                          /* the synth onto the tape: near full, softly saturated */
#define OUT_K 1.1f                              /* the mix to the output (then MASTER, then a soft clip) */
#define XF 3000u                                /* the splice: the end crossfaded into the start (tape samples) */
#define PRE_OUT ((int32_t)(1.6f * EN_SR))       /* a phrase starts this early, unheard, so its loop joins up */

static int rec = -1;                            /* the layer being recorded */
static int32_t pcl;                             /* the phrase clock: output samples since its start (< 0: the run-in) */
static uint32_t rec_n;                          /* tape samples written */
static uint32_t rec_ph;
static float aa[4], aa_c1[3], aa_c2[3];         /* the anti-alias filter: 4th-order Butterworth at 4.4 kHz */
static uint8_t want_new, force_new;
static float spawn_wait;                        /* output samples waited for the newest loop's downbeat */
static float mixl[BLK], mixr[BLK], rsend[BLK];
static float hiss_g, nz_c = 0.5f, nz_l, nz_r, nz_hl, nz_hr, crk_s0, crk_s1, crk_c[3], crk_pl, crk_g;
static uint32_t crk_thr;                         /* a crackle when the noise's top bits fall under it */
static uint32_t nz = 22222u;
static float rv_acc, rv_l0, rv_r0, rv_l1, rv_r1;
static int rv_ph;
static float out_env;

static int lay_live(int i) { return lay[i].state == LS_REC || lay[i].state == LS_PLAY; }
static int lay_pick(int newest, int live_only)
{
    int i, b = -1;
    for (i = 0; i < EN_NLAYER; i++) {
        if (lay[i].state == LS_FREE || (live_only && !lay_live(i)))
            continue;
        if (b < 0 || (newest ? lay[i].serial > lay[b].serial : lay[i].serial < lay[b].serial))
            b = i;
    }
    return b;
}
static uint32_t tape_free(uint32_t *head)       /* room after the newest loop, up to the oldest */
{
    int o = lay_pick(0, 0), y = lay_pick(1, 0);
    if (o < 0) {
        *head = 0;
        return EN_TAPE_N;
    }
    *head = (lay[y].start + lay[y].len) % EN_TAPE_N;
    return (lay[o].start + EN_TAPE_N - *head) % EN_TAPE_N;
}

static void retire(int i, float secs)           /* fade it out, then free it */
{
    layer_t *l = &lay[i];
    if (l->state == LS_FREE)
        return;
    l->state = LS_FADE;
    l->gtgt = 0.0f;
    l->gstep = 1.0f / (secs * EN_SR);
}

/* ------------------------------------------------------------ the phrase --- */
typedef struct {
    uint32_t t, dur;                            /* tape samples */
    uint8_t note, vel;
} ev_t;
#define MAXEV 80
static ev_t ev[MAXEV];
static int nev, ev_i;
static int32_t ev_cyc;
static uint8_t ev_type;
static int prog[4], prog_ok, vlead[4];
static const int8_t PROG_MAJ[8][4] = {{0, 5, 3, 4}, {0, 4, 5, 3}, {3, 0, 4, 5}, {0, 3, 0, 4},
                                      {5, 3, 0, 4}, {0, 5, 1, 4}, {0, 3, 5, 4}, {0, 2, 3, 3}};
static const int8_t PROG_MIN[8][4] = {{0, 5, 2, 6}, {0, 3, 5, 4}, {0, 6, 5, 6}, {0, 5, 3, 4},
                                      {5, 6, 0, 0}, {0, 2, 6, 3}, {0, 3, 6, 2}, {0, 5, 6, 4}};
#define NMEM 16                                 /* the keys played between loops, for the next */
static struct {
    uint32_t t, dur;                            /* output samples */
    uint8_t note, vel, open;
} mem[NMEM];
static int nmem;
static uint32_t clk, mem_t0;

static int minorish(void)
{
    int m = par[P_MODE];
    return m == MODE_DORIAN || m == MODE_PHRYGIAN || m == MODE_AEOLIAN || m == MODE_PENT_MIN;
}
static void prog_next(void)                     /* the chords: most of the last loop's, now and then one moved */
{
    int i;
    float r = frand();
    if (!prog_ok || r < 0.12f) {
        const int8_t *p = (minorish() ? PROG_MIN : PROG_MAJ)[irand(8)];
        for (i = 0; i < 4; i++)
            prog[i] = p[i];
        prog_ok = 1;
    } else if (r < 0.55f) {
        int k = 1 + irand(3), d = 1 + irand(6);
        if (d == prog[k])
            d = (d + 2) % 7;
        prog[k] = d;
    }
}
static void ev_add(uint32_t t, uint32_t dur, int note, int vel)
{
    int i;
    if (nev >= MAXEV || note < 24 || note > 108 || vel <= 0)
        return;
    for (i = nev; i > 0 && ev[i - 1].t > t; i--)   /* kept in time order */
        ev[i] = ev[i - 1];
    ev[i].t = t;
    ev[i].dur = dur < 200u ? 200u : dur;
    ev[i].note = (uint8_t)note;
    ev[i].vel = (uint8_t)(vel > 127 ? 127 : vel);
    nev++;
}
static int deg_note(int lo, int deg)            /* scale degree deg (any) at or above the root nearest lo */
{
    int d = deg + 8 * nsteps, root = par[P_ROOT], n;
    n = lo + ((root - lo) % 12 + 12) % 12 - 12 + steps[d % nsteps] + 12 * (d / nsteps - 8);
    return n;
}
static int clampn(int n, int lo, int hi)
{
    while (n < lo)
        n += 12;
    while (n > hi)
        n -= 12;
    return n;
}

static void phrase_make(layer_t *l, int kind)
{
    const uint32_t L = l->len;
    const int dens = par[P_DENSITY], oct = 12 * par[P_OCT], type = l->voice;
    const int decaying = type == VX_PIANO || type == VX_BELLS, ring = decaying || type == VX_GLASS;
    int k, i, nc, pcs[4], nt, to[4];
    nev = 0;
    prog_next();
    nc = kind == PH_CHORDS ? (dens < 34 ? 2 : dens < 67 ? 3 : 4) : 2;
    if (kind == PH_MELODY)
        nc = dens < 50 ? 1 : 2;
    en_prog_n = (uint8_t)nc;
    for (k = 0; k < nc; k++) {
        uint32_t span = L / (uint32_t)nc, t0 = (uint32_t)k * span;
        int lo = 50 + oct + (type == VX_BELLS ? 12 : 0), bass;
        nt = en_chord_tones(pool, par[P_ROOT], prog[k], pcs);
        for (i = 0; i < 4; i++)
            en_prog_pcs[k][i] = (uint8_t)pcs[i < nt ? i : 0];
        en_prog_nt[k] = (uint8_t)nt;
        en_voice_lead(vlead, pcs, nt, to, lo, lo + 26);
        for (i = 0; i < 4; i++)
            vlead[i] = to[i];
        bass = clampn(36 + oct + pcs[0], 31, 58);
        if (kind == PH_CHORDS) {
            uint32_t strum = ring ? 120u + (uint32_t)irand(160) : 40u, dur = decaying ? span : span + 600u;
            for (i = 0; i < 4; i++)
                ev_add(t0 + strum * (uint32_t)i + (uint32_t)irand(120), dur - strum * (uint32_t)i,
                       to[i], 66 + irand(22) - (i == 3 ? 0 : 6));
            if (!decaying)                       /* the root below, for warmth */
                ev_add(t0, dur, bass, type == VX_ORGAN ? 70 : 78);
            else if (dens > 35)
                ev_add(t0, span, bass + 12, 70);
            if (decaying && span > 26000u && dens > 40)   /* a long chord struck again, softer */
                for (i = 0; i < 3; i++)
                    ev_add(t0 + span / 2u + (uint32_t)irand(400), span / 2u, to[i + 1], 52 + irand(14));
            if (dens > 55 && frand() < 0.65f) {  /* a note above, on top of the chord */
                int top = clampn(to[3] + (frand() < 0.5f ? 0 : 2 + (frand() < 0.5f ? 0 : 1)), lo + 12, 96), ok = 0, s;
                for (s = 0; s < nsteps; s++)
                    ok |= (top - par[P_ROOT] + 120) % 12 == steps[s];
                if (!ok)
                    top = to[3];
                ev_add(t0 + span / 2u + (uint32_t)irand(800), span * 2u / 5u, top + 12 * (top < to[3] + 3), 74);
            }
        } else if (kind == PH_FIGURE) {
            int S = dens < 34 ? 8 : dens < 67 ? 12 : 16, per = S / nc, s, idx = irand(3), dir = 1;
            uint32_t step = L / (uint32_t)S;
            int tones[8], m = 0, b = lo + 10;
            for (i = 0; i < 2 * nt && m < 8; i++)
                tones[m++] = clampn(b + ((pcs[i % nt] - b) % 12 + 12) % 12 + 12 * (i / nt), b, b + 26);
            for (i = 1; i < m; i++) {           /* in order, low to high */
                int x = tones[i], j = i;
                while (j > 0 && tones[j - 1] > x) {
                    tones[j] = tones[j - 1];
                    j--;
                }
                tones[j] = x;
            }
            for (s = 0; s < per; s++) {
                uint32_t t = t0 + (uint32_t)s * step;
                int accent = s % (per > 4 ? 4 : 2) == 0;
                if (s && frand() < 0.12f + 0.2f * (float)(100 - dens) * 0.01f)
                    continue;                    /* a rest */
                ev_add(t + (uint32_t)irand(90), ring ? step * 3u : step * 7u / 4u, tones[idx], accent ? 92 : 64 + irand(16));
                idx += dir;
                if (idx >= m - 1 || idx <= 0)
                    dir = -dir;
                if (frand() < 0.18f)
                    idx = irand(m);
                idx = idx < 0 ? 0 : idx >= m ? m - 1 : idx;
            }
            if (decaying)                        /* under it: the root, struck */
                ev_add(t0, span, bass + 12, 62);
            else                                 /* or the chord, soft */
                for (i = 0; i < 3; i++)
                    ev_add(t0 + 40u * (uint32_t)i, span + 600u, to[i] - 12 * (to[i] > lo + 14), 38);
        } else {                                 /* melody: a drone, a slow line over it */
            ev_add(t0, span + (decaying ? 0u : 600u), bass + 12, 52);
            ev_add(t0 + 300u, span + (decaying ? 0u : 300u), clampn(bass + 19, lo - 2, lo + 14), 44);
            if (decaying && span > 26000u)
                ev_add(t0 + span / 2u, span / 2u, bass + 12, 42);
        }
    }
    if (kind == PH_MELODY) {                     /* stepwise mostly, from a chord tone, home at the end */
        int n = 3 + dens / 25, d = 2 * irand(2) + 7 * (1 + par[P_OCT] / 2), lo = 60 + oct, s;
        uint32_t gap = L / (uint32_t)n;
        (void)lo;
        for (s = 0; s < n; s++) {
            int note = clampn(deg_note(62 + oct, d), 55 + oct, 86 + oct);
            uint32_t t = (uint32_t)s * gap + (uint32_t)irand((int)(gap / 5u));
            ev_add(t, gap * (ring ? 13u : 9u) / 10u, note, 80 + irand(16));
            d += frand() < 0.7f ? (frand() < 0.5f ? 1 : -1) : (frand() < 0.5f ? 2 : -2);
            if (s == n - 2)
                d = 2 * (d / 2);                 /* to a chord tone before the end */
        }
    }
    for (i = 0; i < nmem; i++) {                 /* the keys played since the last loop */
        uint32_t t = (mem[i].t / 4u + 1600u) % L, dur = mem[i].open ? 13000u : mem[i].dur / 4u;
        if (dur > L * 3u / 5u)
            dur = L * 3u / 5u;
        ev_add(t, dur, mem[i].note, mem[i].vel);
    }
    nmem = 0;
    ev_type = (uint8_t)type;
}

static void phrase_cue(int32_t from)            /* the next event at or after phrase time `from` (output samples) */
{
    int32_t L4 = (int32_t)lay[rec].len * 4;
    ev_cyc = -1;
    ev_i = 0;
    while (ev_i < nev && (int32_t)ev[ev_i].t * 4 + ev_cyc * L4 < from)
        ev_i++;
    if (ev_i >= nev) {
        ev_i = 0;
        ev_cyc = 0;
    }
}
static void phrase_tick(void)                   /* the events that are due: the phrase, round and round */
{
    int32_t L4 = (int32_t)lay[rec].len * 4;
    int guard = MAXEV + 1;
    while (nev && guard--) {
        int32_t at = (int32_t)ev[ev_i].t * 4 + ev_cyc * L4;
        if (at > pcl)
            break;
        vox_on(ev[ev_i].note, ev[ev_i].vel, ev_type, 0, at + (int32_t)ev[ev_i].dur * 4);
        if (++ev_i >= nev) {
            ev_i = 0;
            ev_cyc++;
        }
    }
}
static void phrase_voices_off(void)
{
    int i;
    for (i = 0; i < NVOX; i++)
        if (!vox[i].live)
            vox[i].on = 0;
}

/* ---------------------------------------------------------------- wear --- */
static void wear_map(layer_t *l)                /* where the holes open: a few seeds, each its own time, growing */
{
    const int st = par[P_STOCK];
    int ns = STOCK[st].seeds + irand(STOCK[st].seeds_n + 1), s, c;
    float t[EN_NCELL];
    for (c = 0; c < EN_NCELL; c++)
        t[c] = 0.78f + 0.2f * frand();          /* at the end, all of it goes */
    for (s = 0; s < ns; s++) {
        int p = irand(EN_NCELL);
        float b = 0.05f + 0.55f * frand() * frand() + 0.2f * frand(), sp = (0.045f + 0.09f * frand()) / STOCK[st].spread;
        for (c = 0; c < EN_NCELL; c++) {
            int d = c - p;
            float v;
            d = d < 0 ? -d : d;
            if (d > EN_NCELL / 2)
                d = EN_NCELL - d;
            v = b + sp * (float)d;
            if (v < t[c])
                t[c] = v;
        }
    }
    for (c = 0; c < EN_NCELL; c++) {
        float v = (t[c] + 0.035f * (frand() - 0.5f)) * 255.0f;
        l->th[c] = (uint8_t)fm_clampf(v, 6.0f, 255.0f);
    }
}

static void wear_gains(int i)                   /* the cells' gains for the wear now; the screen's map */
{
    layer_t *l = &lay[i];
    const float w = l->wear * 255.0f, inv = 1.0f / (0.07f * 255.0f);
    int c;
    for (c = 0; c < EN_NCELL; c++) {
        float d = fm_clampf((w - (float)l->th[c]) * inv, 0.0f, 1.0f);
        uint8_t m = (uint8_t)(d * 255.0f);
        if (m && !en_lay_dmg[i][c])
            en_flakes++;
        en_lay_dmg[i][c] = m;
        d = d * d * (3.0f - 2.0f * d);
        l->G[c] = 1.0f - 0.97f * d;
    }
    l->G[EN_NCELL] = l->G[0];
    l->G[EN_NCELL + 1] = l->G[0];
    l->g_wear = l->wear;
}

/* --------------------------------------------------------------- spawning --- */
static void free_layer(int i)
{
    int c;
    lay[i].state = LS_FREE;
    en_lay_state[i] = LS_FREE;
    en_lay_level[i] = 0.0f;
    for (c = 0; c < EN_NCELL; c++)
        en_lay_dmg[i][c] = 0;
}

static void start_layer(int s, uint32_t head, uint32_t L, int32_t preroll)
{
    static const float PAN[6] = {-0.42f, 0.42f, -0.18f, 0.18f, -0.6f, 0.6f};
    layer_t *l = &lay[s];
    int c, kind = par[P_PHRASE];
    float p;
    l->state = LS_REC;
    l->voice = (uint8_t)par[P_VOICE];
    l->serial = serial_next++;
    l->start = head;
    l->len = L;
    l->pos = -3.0f - 0.25f * (float)preroll;   /* the play head waits out the run-in, behind the record head */
    l->rate = 0.25f;
    l->wear = 0.0f;
    l->gain = 0.0f;
    l->gtgt = 1.0f;
    l->gstep = 1.0f / (1.2f * EN_SR);
    l->l1 = l->l2 = l->hp = 0.0f;
    l->wph = frand();
    l->wf = (0.32f + 0.3f * frand()) / EN_SR;
    l->fph = frand();
    l->ff = (5.0f + 2.5f * frand()) / EN_SR;
    l->drift = l->drift_t = 0.0f;
    l->drift_left = 0;
    l->passes = 0;
    p = (PAN[l->serial % 6u] + 1.0f) * (FM_PI / 4.0f);
    l->pl = fm_cosf(p);
    l->pr = fm_sinf(p);
    l->dip = l->dip_t = 1.0f;
    l->dip_left = 0;
    l->cellk = (float)EN_NCELL / (float)L;
    l->lvl = 0.0f;
    wear_map(l);
    for (c = 0; c < EN_NCELL; c++)
        en_lay_dmg[s][c] = 0;
    wear_gains(s);
    if (kind == PH_MIXED) {
        static const uint8_t MIX[4] = {PH_CHORDS, PH_FIGURE, PH_CHORDS, PH_MELODY};
        kind = MIX[l->serial % 4u];
    }
    phrase_make(l, kind);
    rec = s;
    pcl = -preroll;
    rec_n = 0;
    rec_ph = 0;
    aa[0] = aa[1] = aa[2] = aa[3] = 0.0f;
    phrase_voices_off();
    phrase_cue(pcl);
    en_loops++;
    en_lay_serial[s] = (uint16_t)l->serial;
    en_lay_voice[s] = l->voice;
    en_lay_secs[s] = (float)L * (1.0f / EN_TSR);
    en_passes[s] = 0;
}

static void finish_rec(void)
{
    layer_t *l = &lay[rec];
    l->state = LS_PLAY;
    rec = -1;
    phrase_voices_off();
}

static void abort_rec(void)                     /* stopped, or a new preset: what was recorded, looped, fades */
{
    layer_t *l;
    if (rec < 0)
        return;
    l = &lay[rec];
    phrase_voices_off();
    if (rec_n < 2048u) {
        free_layer(rec);
    } else {
        l->len = rec_n - 4u < l->len ? rec_n - 4u : l->len;
        l->cellk = (float)EN_NCELL / (float)l->len;
        if (l->pos >= (float)l->len)
            l->pos = 0.0f;
        l->state = LS_PLAY;
        retire(rec, 0.8f);
    }
    rec = -1;
}

static void spawn_check(uint32_t k)
{
    int y, s, i, n = par[P_LAYERS];
    uint32_t L, head, room;
    int32_t preroll = PRE_OUT;
    if (!en_playing || en_speed < 0.98f || rec >= 0)
        return;
    y = lay_pick(1, 1);
    if (!want_new) {
        float at = fm_minf(1.0f / (float)(n < 1 ? 1 : n), 0.85f);
        if (y < 0 || (lay[y].state == LS_PLAY && lay[y].wear >= at))
            want_new = 1;
    }
    if (!want_new)
        return;
    L = (uint32_t)par[P_LENGTH] * (uint32_t)EN_TSR;
    room = tape_free(&head);
    for (s = -1, i = 0; i < EN_NLAYER; i++)
        if (lay[i].state == LS_FREE) {
            s = i;
            break;
        }
    if (room < L + 64u || s < 0) {               /* no room: the oldest goes (and the new one waits for it) */
        int o = -1;
        for (i = 0; i < EN_NLAYER; i++)
            if (lay[i].state == LS_PLAY && (o < 0 || lay[i].serial < lay[o].serial))
                o = i;
        if (o >= 0)
            retire(o, 2.5f);
        return;
    }
    if (!force_new && y >= 0 && lay[y].state == LS_PLAY && lay[y].len == L && lay[y].rate > 0.05f &&
        spawn_wait < (float)L * 4.4f) {         /* on the newest loop's downbeat (waiting a pass at most) */
        float togo = ((float)L - lay[y].pos) / lay[y].rate;
        if (togo > (float)PRE_OUT + 2.0f * (float)k || togo < 0.4f * (float)PRE_OUT) {
            spawn_wait += (float)k;
            return;
        }
        preroll = (int32_t)togo;
    }
    start_layer(s, head, L, preroll);
    want_new = force_new = 0;
    spawn_wait = 0.0f;
}

/* ---------------------------------------------------- reverb: Dattorro's plate --- */
/* J. Dattorro, "Effect Design, Part 1", JAES 1997, as in AMB-1: a predelay, four input diffusers, a figure-eight
 * tank. Here at half the rate (22.05 kHz: it only has to carry what a worn tape leaves) and in 16 bits, in RAM:
 * 35 KB, which leaves the pool to the tape. DS() scales the paper's lengths (29761 Hz) to 22.05 kHz. */
#define DS(n) (((n) * 7409 + 5000) / 10000)
#define PRE_N 662
#define EXC DS(16)
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define DL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static int16_t pl_buf[DL_TOTAL];
typedef struct {
    int16_t *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, pl_decay = 0.7f, pl_dd2 = 0.5f, lfo_s, lfo_c = 1.0f;
#define PQ 8192.0f
static inline float tap(const dl_t *d, int k)
{
    int j = d->i - k;
    return (float)d->b[j < 0 ? j + d->n : j] * (1.0f / PQ);
}
static inline void push(dl_t *d, float x)
{
    float v = x * PQ;
    v += v >= 0.0f ? 0.5f : -0.5f;
    d->b[d->i] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, v);
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    push(d, v);
    return z + g * v;
}
static inline void reverb(float in, float *ol, float *or_)
{
    float x, a, b, fb_l = tap(&dl[L_D2R], DS(3163)), fb_r = tap(&dl[L_D2L], DS(3720));
    push(&dl[L_PRE], in);
    x = tap(&dl[L_PRE], PRE_N);
    pl_bw += 0.7f * (x - pl_bw);
    x = allpass(&dl[L_IN1], DS(142), pl_bw, 0.75f);
    x = allpass(&dl[L_IN2], DS(107), x, 0.75f);
    x = allpass(&dl[L_IN3], DS(379), x, 0.625f);
    x = allpass(&dl[L_IN4], DS(277), x, 0.625f);
    {
        float s = lfo_s + 0.00014f * lfo_c, c = lfo_c - 0.00014f * s;
        lfo_s = s;
        lfo_c = c;
    }
    a = allpass_mod(&dl[L_APL], (float)DS(672) + (float)EXC * lfo_s, x + pl_decay * fb_l, -0.7f);
    push(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], DS(4453));
    pl_damp_l += 0.5f * (b - pl_damp_l);
    b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * pl_decay, pl_dd2);
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * lfo_c, x + pl_decay * fb_r, -0.7f);
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], DS(4217));
    pl_damp_r += 0.5f * (b - pl_damp_r);
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
    int16_t *p = pl_buf;
    int k, i;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    for (i = 0; i < DL_TOTAL; i++)
        pl_buf[i] = 0;
    pl_bw = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
}

/* ------------------------------------------------------------ parameters --- */
static void apply(int p, int v)
{
    const en_param_t *pi = &PARAMS[p];
    if (v < pi->lo)
        v = pi->lo;
    if (v > pi->hi)
        v = pi->hi;
    par[p] = (int16_t)v;
    switch (p) {
    case P_TONE: tone = (float)v * 0.01f; break;
    case P_WOW: wowp = (float)v * 0.01f; break;
    case P_HISS: hissp = (float)v * 0.01f; break;
    case P_SPACE: space = (float)v * 0.01f; break;
    case P_TUNE: tunefac = fm_exp2f((float)v * (1.0f / 1200.0f)); break;
    case P_MODE:
        pool = EN_MODE_POOL[v];
        nsteps = en_scale(pool, steps);
        prog_ok = 0;                             /* another mode: chords from its own book */
        break;
    case P_ROOT: prog_ok = 0; break;
    default: break;
    }
}

static void quiet(void)
{
    int i;
    for (i = 0; i < NVOX; i++)
        vox[i].on = 0;
    abort_rec();
    for (i = 0; i < EN_NLAYER; i++)
        if (lay[i].state != LS_FREE)
            free_layer(i);
}

void en_init(void)
{
    int i;
    for (i = 0; i <= SN; i++)
        sint[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SN));
    for (i = 0; i < 256; i++)
        MU[i] = en_unmulaw((uint8_t)i);
    plate_init();
    for (i = 0; i < (int)EN_TAPE_N; i++)
        tape[i] = 0xFF;                          /* mu-law silence */
    q_r = q_w = 0;
    for (i = 0; i < NVOX; i++)
        vox[i].on = 0;
    for (i = 0; i < EN_NLAYER; i++)
        free_layer(i);
    rec = -1;
    nmem = 0;
    want_new = force_new = 0;
    prog_ok = 0;
    en_loops = 0;
    serial_next = 1;
    for (i = 0; i < 4; i++)
        vlead[i] = 55 + 4 * i;
    svf_coefs(aa_c1, 4400.0f, 1.8478f);
    svf_coefs(aa_c2, 4400.0f, 0.7654f);
    svf_coefs(crk_c, 2300.0f, 0.3f);
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, PARAMS[i].def);
    en_speed = 0.0f;
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
            } else if (!a && en_playing) {
                en_playing = 0;
                abort_rec();
                want_new = 0;
            }
            break;
        case C_FREEZE: en_frozen = (uint8_t)(a != 0); break;
        case C_NEW:
            if (rec < 0) {
                want_new = force_new = 1;
                spawn_wait = 0.0f;
            }
            break;
        case C_REGROW: {
            int i;
            abort_rec();
            for (i = 0; i < EN_NLAYER; i++)
                if (lay_live(i))
                    retire(i, 3.0f);
            prog_ok = 0;
            nmem = 0;
            want_new = force_new = 1;
            spawn_wait = 0.0f;
            break;
        }
        case C_NOTE:
            if (b > 0) {
                vox_on(a, b, par[P_VOICE], 1, 0);
                if ((rec < 0 || pcl < 0) && nmem < NMEM) {   /* between loops: kept for the next */
                    if (!nmem)
                        mem_t0 = clk;
                    mem[nmem].t = clk - mem_t0;
                    mem[nmem].dur = 0;
                    mem[nmem].note = (uint8_t)a;
                    mem[nmem].vel = (uint8_t)b;
                    mem[nmem].open = 1;
                    nmem++;
                }
            } else {
                int i;
                vox_off_key(a);
                for (i = 0; i < nmem; i++)
                    if (mem[i].open && mem[i].note == a) {
                        mem[i].open = 0;
                        mem[i].dur = clk - mem_t0 - mem[i].t;
                    }
            }
            break;
        case C_SEED:
            rng ^= (uint32_t)(b & 0xFFFF) * 2654435761u;
            if (!rng)
                rng = 1u;
            break;
        case C_PANIC: quiet(); break;
        }
    }
}

/* --------------------------------------------------------------- render --- */

static void layer_control(int i, uint32_t k)    /* once a chunk: wear, tone, wow, the fade */
{
    layer_t *l = &lay[i];
    const int st = par[P_STOCK];
    const float w = l->wear, sp = en_speed;
    float fc0, fc, hpf, wd, fd, sag;
    if (l->state == LS_PLAY && en_playing && !en_frozen && sp > 0.5f) {
        l->wear += (float)k / (en_life_secs(par[P_DECAY]) * EN_SR);
        if (l->wear >= 1.0f) {
            free_layer(i);
            return;
        }
    }
    if (l->state != LS_REC && (l->wear - l->g_wear > 0.0025f || l->wear < l->g_wear))
        wear_gains(i);
    /* the top rolls off as it wears: from Tone's (times the stock's) down to ~300 Hz */
    fc0 = fm_clampf((1700.0f + 3500.0f * tone) * STOCK[st].bw, 500.0f, 5200.0f);
    fc = fc0 * fm_exp2f(fm_log2f(300.0f / fc0) * fm_powf(w, 1.3f));
    hpf = STOCK[st].hp * (1.0f + 2.0f * w * w);
    l->cl = 1.0f - fm_expf(-FM_TWO_PI * fc * (1.0f / EN_SR));
    l->ch = 1.0f - fm_expf(-FM_TWO_PI * hpf * (1.0f / EN_SR));
    /* the speed: wow (slow, and a drift), flutter (quick), and a sag as the tape stretches */
    l->wph += (float)k * l->wf;
    if (l->wph >= 1.0f)
        l->wph -= 1.0f;
    l->fph += (float)k * l->ff;
    if (l->fph >= 1.0f)
        l->fph -= 1.0f;
    if (l->drift_left <= k) {
        l->drift_t = frand() * 2.0f - 1.0f;
        l->drift_left = (uint32_t)((1.0f + 2.5f * frand()) * EN_SR);
    } else {
        l->drift_left -= k;
    }
    l->drift += (l->drift_t - l->drift) * fm_minf((float)k * (1.0f / (1.2f * EN_SR)), 1.0f);
    wd = wowp * STOCK[st].wow * (0.12f + 0.88f * w) * 0.0055f;
    fd = wowp * STOCK[st].flutter * (0.1f + 0.9f * w) * 0.0011f;
    sag = wowp * w * 0.0035f;
    if (l->state == LS_REC)
        l->rate = 0.25f;                         /* recording: locked to the record head */
    else
        l->rate = 0.25f * sp * (1.0f - sag + wd * (0.65f * sine(l->wph) + 0.35f * l->drift) + fd * sine(l->fph));
    /* a moment off the head now and then, more as it wears */
    if (l->dip_left > k) {
        l->dip_left -= k;
    } else {
        l->dip_left = 0;
        l->dip_t = 1.0f;
        if (l->state == LS_PLAY && frand() < (float)k * w * w * STOCK[st].dips * (0.9f / EN_SR)) {
            l->dip_t = 0.12f + 0.6f * frand();
            l->dip_left = (uint32_t)((0.04f + 0.18f * frand()) * EN_SR);
        }
    }
    /* the fade: in as it records, out at the end of its life (the last tenth) or when let go */
    {
        float g = l->gain, d = (float)k * l->gstep;
        g = g < l->gtgt ? fm_minf(g + d, l->gtgt) : fm_maxf(g - d, l->gtgt);
        l->gain = g;
        if (l->state == LS_FADE && g <= 0.0f) {
            free_layer(i);
            return;
        }
    }
}

static void render_layer(int i, uint32_t n)
{
    layer_t *l = &lay[i];
    const float lenf = (float)l->len, cellk = l->cellk, rate = l->rate, cl = l->cl, ch = l->ch;
    const float *G = l->G;
    const uint8_t *tp = tape;
    const uint32_t len = l->len, start = l->start;
    const float life = fm_clampf((1.0f - l->wear) * 10.0f, 0.0f, 1.0f) * (1.0f - 0.35f * l->wear);
    float pos = l->pos, l1 = l->l1, l2 = l->l2, hp = l->hp, pk = 0.0f;
    float a = l->lvl, a1 = l->gain * life * LAY_G * fm_sqrtf(en_speed), da;
    float dip = l->dip;
    const float dipk = fm_minf(1.0f / (0.01f * EN_SR), 1.0f), dipt = l->dip_t;
    const float send = 0.55f + 0.7f * l->wear;
    uint32_t j;
    da = (a1 - a) / (float)n;
    for (j = 0; j < n; j++) {
        float x = 0.0f, y;
        if (pos >= 0.0f) {
            uint32_t i0 = (uint32_t)pos, i1 = i0 + 1u, a0, b0;
            float f = pos - (float)i0, s0, cp, gc;
            int c;
            if (i1 >= len)
                i1 = 0;
            a0 = start + i0;
            b0 = start + i1;
            if (a0 >= EN_TAPE_N)
                a0 -= EN_TAPE_N;
            if (b0 >= EN_TAPE_N)
                b0 -= EN_TAPE_N;
            s0 = MU[tp[a0]];
            x = s0 + (MU[tp[b0]] - s0) * f;
            cp = pos * cellk;
            c = (int)cp;
            if (c >= EN_NCELL)
                c = EN_NCELL - 1;
            gc = G[c] + (G[c + 1] - G[c]) * (cp - (float)c);
            x *= gc;
        }
        pos += rate;
        if (pos >= lenf) {
            pos -= lenf;
            l->passes++;
        }
        dip += (dipt - dip) * dipk;
        l1 += cl * (x - l1);
        l2 += cl * (l1 - l2);
        hp += ch * (l2 - hp);
        a += da;
        y = (l2 - hp) * a * dip;
        mixl[j] += y * l->pl;
        mixr[j] += y * l->pr;
        rsend[j] += y * send;
        y = fm_fabsf(y);
        if (y > pk)
            pk = y;
    }
    l->pos = pos;
    l->l1 = fm_flush(l1);
    l->l2 = fm_flush(l2);
    l->hp = fm_flush(hp);
    l->lvl = a1;
    l->dip = dip;
    en_lay_level[i] = fm_maxf(pk * 2.5f, en_lay_level[i] * 0.97f);
}

static void record(uint32_t n)                  /* the synth onto the tape: filtered, a quarter of the samples */
{
    layer_t *l = &lay[rec];
    uint32_t j;
    float a0 = aa[0], a1 = aa[1], a2 = aa[2], a3 = aa[3];
    for (j = 0; j < n; j++) {
        float x, v1, v2, v3;
        if (pcl + (int32_t)j < 0) {              /* the run-in: unheard, but the keys are */
            mixl[j] += liv[j] * 0.45f;
            mixr[j] += liv[j] * 0.45f;
            rsend[j] += liv[j] * 0.5f;
            continue;
        }
        x = syn[j] + liv[j];
        v3 = x - a1;
        v1 = aa_c1[0] * a0 + aa_c1[1] * v3;
        v2 = a1 + aa_c1[1] * a0 + aa_c1[2] * v3;
        a0 = 2.0f * v1 - a0;
        a1 = 2.0f * v2 - a1;
        v3 = v2 - a3;
        v1 = aa_c2[0] * a2 + aa_c2[1] * v3;
        v2 = a3 + aa_c2[1] * a2 + aa_c2[2] * v3;
        a2 = 2.0f * v1 - a2;
        a3 = 2.0f * v2 - a3;
        if (++rec_ph >= 4u) {
            uint32_t at;
            float y = fm_tanhf(v2 * REC_DRIVE);       /* the record level: the tape saturates softly */
            rec_ph = 0;
            if (rec_n < l->len) {
                at = l->start + rec_n;
                if (at >= EN_TAPE_N)
                    at -= EN_TAPE_N;
                tape[at] = en_mulaw(y);
            } else if (rec_n < l->len + XF) {    /* the splice: the end into the start */
                uint32_t i = rec_n - l->len;
                float w = (float)i * (1.0f / (float)XF);
                at = l->start + i;
                if (at >= EN_TAPE_N)
                    at -= EN_TAPE_N;
                tape[at] = en_mulaw(MU[tape[at]] * w + y * (1.0f - w));
            }
            rec_n++;
        }
    }
    aa[0] = fm_flush(a0);
    aa[1] = fm_flush(a1);
    aa[2] = fm_flush(a2);
    aa[3] = fm_flush(a3);
}

static void control(uint32_t k)
{
    int i;
    float hs = 0.0f, cr = 0.0f;
    const int st = par[P_STOCK];
    uint32_t head;
    en_speed += ((en_playing ? 1.0f : 0.0f) - en_speed) * fm_minf((float)k * (en_playing ? 1.0f / (0.25f * EN_SR) : 1.0f / (0.45f * EN_SR)), 1.0f);
    if (en_speed < 0.001f)
        en_speed = 0.0f;
    spawn_check(k);
    for (i = 0; i < EN_NLAYER; i++) {
        layer_t *l = &lay[i];
        if (l->state == LS_FREE)
            continue;
        layer_control(i, k);
        if (l->state == LS_FREE)
            continue;
        hs += l->gain * (0.08f + 0.92f * l->wear * fm_sqrtf(l->wear));
        cr += l->gain * l->wear;
    }
    hiss_g = hissp * STOCK[st].hiss * hs * 0.028f * en_speed;
    crk_thr = (uint32_t)(fm_minf(hissp * STOCK[st].crackle * cr * (9.0f / EN_SR) * en_speed, 0.01f) * 16777216.0f);
    crk_g = 0.12f * fm_minf(hissp * 2.0f, 1.0f);
    nz_c = 1.0f - fm_expf(-FM_TWO_PI * STOCK[st].nlp * (1.0f / EN_SR));
    for (i = 0; i < EN_NLAYER; i++) {
        layer_t *l = &lay[i];
        en_lay_state[i] = l->state;
        if (l->state == LS_FREE)
            continue;
        en_lay_wear[i] = l->wear;
        en_lay_pos[i] = l->pos > 0.0f ? l->pos / (float)l->len : 0.0f;
        en_lay_rec[i] = l->state == LS_REC ? fm_minf((float)rec_n / (float)l->len, 1.0f) : 1.0f;
        en_lay_gain[i] = l->gain;
        en_passes[i] = l->passes;
    }
    en_tape_used = 1.0f - (float)tape_free(&head) * (1.0f / (float)EN_TAPE_N);
    pl_decay = 0.55f + 0.38f * space;
    pl_dd2 = fm_clampf(pl_decay + 0.15f, 0.25f, 0.5f);
}

static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    int i;
    const float rg = space * space * 0.9f + 0.05f;
    float pk = 0.0f;
    for (j = 0; j < n; j++)
        mixl[j] = mixr[j] = rsend[j] = 0.0f;
    synth_render(n, pcl);
    if (rec >= 0) {
        phrase_tick();
        record(n);
        pcl += (int32_t)n;
    } else {
        for (j = 0; j < n; j++) {               /* the keys, heard at once */
            mixl[j] += liv[j] * 0.45f;
            mixr[j] += liv[j] * 0.45f;
            rsend[j] += liv[j] * 0.5f;
        }
    }
    for (i = 0; i < EN_NLAYER; i++)
        if (lay[i].state != LS_FREE)
            render_layer(i, n);
    if (rec >= 0 && rec_n >= lay[rec].len + XF)
        finish_rec();
    for (j = 0; j < n; j++) {
        float l, r, nl, nr;
        nz = nz * 1664525u + 1013904223u;       /* the hiss: two noises, coloured by the stock */
        nl = (float)(int32_t)nz * (1.0f / 2147483648.0f);
        nz = nz * 1664525u + 1013904223u;
        nr = (float)(int32_t)nz * (1.0f / 2147483648.0f);
        nz_hl += nz_c * (nl - nz_hl);           /* low-passed, then the rumble taken out */
        nz_hr += nz_c * (nr - nz_hr);
        nz_l += 0.03f * (nz_hl - nz_l);
        nz_r += 0.03f * (nz_hr - nz_r);
        {
            float imp = 0.0f, v1, v2, v3;
            if ((nz >> 8) < crk_thr) {           /* a crackle: an impulse into a ringing band-pass */
                imp = (nl > 0.0f ? 1.0f : -1.0f) * (0.3f + 0.7f * fm_fabsf(nr));
                crk_pl = 0.2f + 0.6f * (float)(nz & 255u) * (1.0f / 255.0f);
            }
            v3 = imp - crk_s1;
            v1 = crk_c[0] * crk_s0 + crk_c[1] * v3;
            v2 = crk_s1 + crk_c[1] * crk_s0 + crk_c[2] * v3;
            crk_s0 = 2.0f * v1 - crk_s0;
            crk_s1 = 2.0f * v2 - crk_s1;
            v1 *= crk_g;
            l = mixl[j] + (nz_hl - nz_l) * hiss_g + v1 * crk_pl;
            r = mixr[j] + (nz_hr - nz_r) * hiss_g + v1 * (1.0f - crk_pl);
        }
        /* the plate, at half the rate: the input pair averaged, the output interpolated */
        rv_acc += rsend[j];
        if (rv_ph) {
            float wl, wr;
            reverb(rv_acc * 0.5f * rg, &wl, &wr);
            rv_acc = 0.0f;
            rv_l0 = rv_l1;
            rv_r0 = rv_r1;
            rv_l1 = wl;
            rv_r1 = wr;
            l += 0.5f * (rv_l0 + rv_l1);
            r += 0.5f * (rv_r0 + rv_r1);
        } else {
            l += rv_l1;
            r += rv_r1;
        }
        rv_ph ^= 1;
        l *= OUT_K * gain;
        r *= OUT_K * gain;
        if (fm_fabsf(l) > pk)
            pk = fm_fabsf(l);
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
    }
    out_env = fm_maxf(pk, out_env * fm_expf(-(float)n / (0.4f * EN_SR)));
    en_out_level = out_env;
    clk += n;
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    drain();
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        control(k);
        render_chunk(out, k, gain);
        out += 2 * k;
        n -= k;
    }
}
