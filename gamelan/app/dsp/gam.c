/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen; the command queue and Dattorro's plate from AMB-1, Copyright (C) 2026 Charles
 * Vestal */
/* The instrument (gam.h). One compilation unit, -O2. */
#include "gam.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

/* ------------------------------------------------------------- tunings --- */
/* cents above tone 1 of each laras, for the five tones a pathet uses. Slendro: near-equal steps, a little uneven, as
 * measured sets are; pelog: Javanese tones 1 2 3 4 5 6 7 at 0 120 258 539 675 785 943; selisir: a Balinese gong
 * kebyar's five. Octaves stretched to 1205 cents. */
const char *const EN_TUNING_NAME[EN_NTUNING] = {"Manyura", "Sanga", "Sl. nem", "Pl. nem", "Pl. lima", "Barang",
                                                "Selisir"};
const char *const EN_TUNING_LONG[EN_NTUNING] = {"Slendro manyura", "Slendro sanga", "Slendro nem", "Pelog nem",
                                                "Pelog lima", "Pelog barang", "Selisir"};
const char *const EN_TONE_LABEL[EN_NTUNING] = {"12356", "12356", "12356", "12356", "12456", "23567", "12356"};
const int16_t EN_CENTS[EN_NTUNING][EN_NTONE] = {
    {0, 231, 474, 717, 955}, {0, 231, 474, 717, 955}, {0, 231, 474, 717, 955},
    {0, 120, 258, 675, 785}, {0, 120, 539, 675, 785}, {120, 258, 675, 785, 943}, {0, 128, 287, 679, 792}};
/* each pathet's strong tones (indices into the five): the gong tone first, then the kenong's favourites */
static const uint8_t STRONG[EN_NTUNING][3] = {{4, 2, 1}, {3, 0, 1}, {1, 4, 3}, {4, 3, 1}, {0, 3, 1}, {3, 0, 1},
                                             {0, 3, 2}};
#define OCT_CENTS 1205.0f

const char *const EN_FORM_NAME[EN_NFORM] = {"Auto", "Lancaran", "Ketawang", "Ladrang", "Gilak"};
const char *const EN_IRAMA_NAME[4] = {"Auto", "I", "II", "III"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Tempo", 40, 120, 72},    {"Density", 0, 100, 50},  {"Tuning", 0, EN_NTUNING - 1, T_PLNEM}, {"Ombak", 0, 100, 50},
    {"Gongs", 0, 100, 70},     {"Balungan", 0, 100, 60}, {"Kotekan", 0, 100, 55},  {"Bonang", 0, 100, 50},
    {"Kendang", 0, 100, 45},   {"Suling", 0, 100, 0},    {"Reverb", 0, 100, 50},   {"Mallet", 0, 100, 35},
    {"Pitch", -6, 6, 0},       {"Irama", 0, 3, 0},       {"Form", 0, EN_NFORM - 1, 0},
    {"Style", 0, 1, 0},        {"Flow", 0, 100, 35},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

/* the presets: tempo, density, tuning, ombak; gongs, balungan, kotekan, bonang; kendang, suling, reverb, mallet;
 * pitch, irama, form; style (0 Jawa, 1 Bali), flow */
static const struct {
    const char *name;
    int16_t par[P_NPARAMS];
} PRESETS[EN_NPRESETS] = {
    {"Pelog Dawn",    {66, 45, T_PLNEM,   40, 70, 60, 45, 50, 35, 30, 50, 30, 0, 0, F_LADRANG, 0, 35}},
    {"Slendro Night", {58, 40, T_MANYURA, 35, 70, 55, 40, 40, 30, 30, 60, 20, -2, 0, F_KETAWANG, 0, 30}},
    {"Temple",        {52, 30, T_SANGA,   30, 85, 60, 25, 45, 30, 0, 70, 25, -3, 0, F_LADRANG, 0, 25}},
    {"Rain Court",    {84, 70, T_PLLIMA,  45, 55, 50, 60, 55, 30, 0, 45, 40, 0, 0, F_LANCARAN, 0, 30}},
    {"Kotekan",       {96, 80, T_SELISIR, 75, 60, 50, 70, 30, 45, 0, 40, 45, 1, 1, F_GILAK, 1, 60}},
    {"Gong Ageng",    {44, 20, T_SLNEM,   30, 95, 50, 20, 35, 25, 0, 65, 15, -4, 0, F_LADRANG, 0, 20}},
    {"Water Palace",  {70, 55, T_BARANG,  55, 60, 50, 55, 45, 25, 25, 65, 25, 0, 0, F_KETAWANG, 0, 35}},
    {"Moon Pavilion", {62, 40, T_MANYURA, 40, 65, 55, 40, 40, 30, 45, 60, 20, 0, 0, F_LADRANG, 0, 30}},
    {"Rice Terraces", {88, 65, T_SELISIR, 65, 55, 50, 65, 25, 40, 25, 40, 35, 0, 1, F_GILAK, 1, 45}},
    {"Shadow Play",   {92, 75, T_SANGA,   70, 45, 45, 70, 20, 45, 0, 45, 40, 2, 1, F_LANCARAN, 1, 55}},
    {"Bronze Garden", {80, 60, T_PLNEM,   50, 65, 60, 50, 55, 40, 0, 45, 40, 0, 0, F_LANCARAN, 0, 40}},
    {"Monsoon",       {76, 70, T_BARANG,  55, 70, 60, 60, 50, 60, 0, 50, 45, -1, 0, F_LADRANG, 0, 70}},
    {"Incense",       {46, 15, T_SLNEM,   25, 75, 50, 20, 35, 20, 30, 70, 15, -2, 0, F_KETAWANG, 0, 15}},
    {"Palm Shade",    {64, 45, T_PLLIMA,  45, 60, 55, 50, 40, 30, 20, 55, 25, 0, 0, F_KETAWANG, 0, 30}},
};
const char *en_preset_name(int i) { return PRESETS[(unsigned)i < EN_NPRESETS ? i : 0].name; }
void en_preset(int i, int16_t *par)
{
    int k;
    if ((unsigned)i >= EN_NPRESETS)
        i = 0;
    for (k = 0; k < P_NPARAMS; k++)
        par[k] = PRESETS[i].par[k];
}

static char *itoa_s(int v, char *b)
{
    char t[12];
    int n = 0;
    unsigned u = (unsigned)(v < 0 ? -v : v);
    if (v < 0)
        *b++ = '-';
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static void cpy(char *d, const char *s)
{
    while ((*d++ = *s++))
        ;
}

void en_param_text(int i, int v, char *b)
{
    switch (i) {
    case P_TUNING: cpy(b, EN_TUNING_NAME[(unsigned)v < EN_NTUNING ? v : 0]); break;
    case P_OMBAK: {                                    /* the pair's beat, Hz: 0 .. 8 */
        int t = (v * 8 + 5) / 10;                      /* tenths */
        char *e = itoa_s(t / 10, b);
        *e++ = '.';
        *e++ = (char)('0' + t % 10);
        cpy(e, " Hz");
        break;
    }
    case P_PITCH:
        if (v > 0)
            *b++ = '+';
        itoa_s(v, b);
        break;
    case P_IRAMA: cpy(b, EN_IRAMA_NAME[v & 3]); break;
    case P_FORM: cpy(b, EN_FORM_NAME[(unsigned)v < EN_NFORM ? v : 0]); break;
    case P_STYLE: cpy(b, v ? "Bali" : "Jawa"); break;
    default: itoa_s(v, b); break;
    }
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_PLAY, C_NEWPIECE, C_RESTART, C_HOLD, C_IRAMA, C_KEY, C_SEED, C_PANIC };
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
void en_new_piece(void) { post(C_NEWPIECE, 0, 0); }
void en_restart(void) { post(C_RESTART, 0, 0); }
void en_hold(int on) { post(C_HOLD, on, 0); }
void en_irama(int dir) { post(C_IRAMA, dir & 0xFF, 0); }
void en_key(int kind, int deg) { post(C_KEY, kind, deg); }
void en_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
void en_panic(void) { post(C_PANIC, 0, 0); }

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_state, en_hold_on, en_form, en_nbeats, en_beat, en_level, en_sub, en_sirep;
volatile uint8_t en_tuning;
volatile int8_t en_bal[EN_MAXBEATS];
volatile uint32_t en_mk_gong, en_mk_kempul, en_mk_kenong, en_mk_ketuk, en_mk_kempyang;
volatile float en_pos;
volatile float en_glow[EN_NKEYKIND];
volatile float en_tone_glow[EN_NTONE];
volatile int8_t en_trend, en_cur, en_kenong_at;
volatile uint8_t en_gongan;
volatile uint32_t en_ticks, en_gongs, en_pieces, en_strokes, en_irama_changes;
volatile float en_out_level;

static int16_t par[P_NPARAMS];
static uint32_t rng = 0x9E3779B9u, nzs = 0x2545F491u;

static uint32_t rnd(void)                       /* Marsaglia's xorshift (Xorshift RNGs, 2003): shifts 13, 17, 5 */
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    rng = x ^ (x << 5);
    return rng;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static int irand(int n) { return n > 0 ? (int)((rnd() >> 8) % (uint32_t)n) : 0; }
static inline float noise(void)                 /* white, -1..1, for the drum and the flute's breath */
{
    uint32_t x = nzs;
    x ^= x << 13;
    x ^= x >> 17;
    nzs = x ^ (x << 5);
    return (float)(int32_t)nzs * (1.0f / 2147483648.0f);
}

/* a sine table: 1024 points a turn, plus one */
#define SN 1024
static float sint[SN + 1];
static inline float sine(float ph)              /* ph in turns, any value >= 0 */
{
    float x = (ph - (float)(int)ph) * (float)SN;
    int i = (int)x;
    float f = x - (float)i;
    return sint[i] + (sint[i + 1] - sint[i]) * f;
}

static float lvl(int v) { return (float)(v * v) * 0.0001f; }   /* a level knob: 0..100 -> 0..1, squared */

/* -------------------------------------------------------------- pitch --- */
static float base_hz = 130.81f;                 /* degree 0: tone 1, an octave under the balungan */
static int tuning = T_PLNEM;

static int deg_oct(int deg) { return deg >= 0 ? deg / EN_NTONE : -((-deg + EN_NTONE - 1) / EN_NTONE); }
float en_deg_hz(int t, int pitch, int deg)
{
    int o = deg_oct(deg), pc = deg - o * EN_NTONE;
    float cents = (float)o * OCT_CENTS + (float)EN_CENTS[(unsigned)t < EN_NTUNING ? t : 0][pc];
    return 130.81f * fm_exp2f(((float)pitch * 100.0f + cents) * (1.0f / 1200.0f));
}
static float deg_hz(int deg)
{
    int o = deg_oct(deg), pc = deg - o * EN_NTONE;
    return base_hz * fm_exp2f(((float)o * OCT_CENTS + (float)EN_CENTS[tuning][pc]) * (1.0f / 1200.0f));
}
static int fold(int d, int lo, int hi)          /* d by octaves into lo..hi */
{
    while (d < lo)
        d += EN_NTONE;
    while (d > hi)
        d -= EN_NTONE;
    return d;
}
static int pc_of(int d) { return d - deg_oct(d) * EN_NTONE; }

/* ------------------------------------------------------- modal voices --- */
/* Each stroke a handful of partials, each a decaying sinusoid: a complex phasor turned and shrunk every sample by
 * one complex multiply (4 multiplies, 2 adds), its output the imaginary part, so it starts from zero at the stroke.
 * A partial below -100 dB is dropped; a voice with none is free. A player damps the last note when striking the
 * next: the voice's phasors shrink faster from then on. */
enum { I_GONG, I_KEMPUL, I_KENONG, I_KETUK, I_KEMPYANG, I_SARON, I_SLENTHEM, I_PEKING, I_POLOS, I_SANGSIH, I_BONANG,
       I_KEY, NINST };
#define NPART 5
#define NMV 30
typedef struct {
    float c[NPART], s[NPART], rc[NPART], rs[NPART];
    float gl, gr, send;
    uint8_t np, inst;
    uint16_t serial;
} mv_t;
static mv_t mv[NMV];
static uint16_t mv_serial;
static int8_t inst_last[NINST];                 /* the instrument's last voice (to damp), -1 none */
static uint16_t inst_last_serial[NINST];

#define CH 64                                   /* samples a chunk, at most */
static float mixl[CH], mixr[CH], rsend[CH], vb[CH];

static void part_set(mv_t *v, float f, float amp, float t60)
{
    float w, r;
    int k = v->np;
    if (k >= NPART || f <= 10.0f || f > 15000.0f || amp == 0.0f)
        return;
    w = f * (FM_TWO_PI / EN_SR);
    r = fm_expf(-6.9078f / (t60 * EN_SR));
    v->rc[k] = r * fm_cosf(w);
    v->rs[k] = r * fm_sinf(w);
    v->c[k] = amp;
    v->s[k] = 0.0f;
    v->np = (uint8_t)(k + 1);
}

static void mv_damp(mv_t *v, float t60)          /* from now on, it dies away in t60 seconds at most */
{
    float d = fm_expf(-6.9078f / (t60 * EN_SR));
    int k;
    for (k = 0; k < v->np; k++) {
        v->rc[k] *= d;
        v->rs[k] *= d;
    }
}

static mv_t *mv_alloc(int inst)
{
    int i, best = -1;
    float be = 1e30f;
    for (i = 0; i < NMV; i++)
        if (!mv[i].np) {
            best = i;
            break;
        }
    if (best < 0)                               /* all busy: the quietest goes */
        for (i = 0; i < NMV; i++) {
            float e = 0.0f;
            int k;
            for (k = 0; k < mv[i].np; k++)
                e += mv[i].c[k] * mv[i].c[k] + mv[i].s[k] * mv[i].s[k];
            if (e < be) {
                be = e;
                best = i;
            }
        }
    mv[best].np = 0;
    mv[best].inst = (uint8_t)inst;
    mv[best].serial = ++mv_serial;
    return &mv[best];
}

static void pan_set(mv_t *v, float p, float send)  /* p -1 left .. +1 right */
{
    float a = (p + 1.0f) * (FM_PI / 4.0f);
    v->gl = fm_cosf(a) * 1.4142f;
    v->gr = fm_sinf(a) * 1.4142f;
    v->send = send;
}

static void mv_run(mv_t *v, uint32_t n)
{
    uint32_t j;
    int k;
    for (j = 0; j < n; j++)
        vb[j] = 0.0f;
    for (k = 0; k < v->np; k++) {
        float c = v->c[k], s = v->s[k];
        const float rc = v->rc[k], rs = v->rs[k];
        for (j = 0; j < n; j++) {
            float t = c * rc - s * rs;
            s = c * rs + s * rc;
            c = t;
            vb[j] += s;
        }
        v->c[k] = fm_flush(c);
        v->s[k] = fm_flush(s);
    }
    for (k = 0; k < v->np;) {                   /* the quiet partials dropped */
        if (v->c[k] * v->c[k] + v->s[k] * v->s[k] < 1e-10f) {
            int z = --v->np;
            v->c[k] = v->c[z];
            v->s[k] = v->s[z];
            v->rc[k] = v->rc[z];
            v->rs[k] = v->rs[z];
        } else {
            k++;
        }
    }
    {
        const float gl = v->gl, gr = v->gr, sd = v->send;
        for (j = 0; j < n; j++) {
            mixl[j] += vb[j] * gl;
            mixr[j] += vb[j] * gr;
            rsend[j] += vb[j] * sd;
        }
    }
}

/* ---------------------------------------------------- the instruments --- */
static float g_gongs, g_bal, g_kot, g_bon, g_kend, g_sul;   /* the mix (levels, squared) */
static float ombak_hz = 4.0f, mallet = 0.35f;
static int style;                               /* 0 Jawa, 1 Bali */
static uint8_t sirep;                           /* a soft gongan: the loud instruments down */

/* a bar (saron, slenthem, peking, the kotekan's pair, a key): two fundamentals ombak apart (the pair), a second
 * partial near 2.76 and a short strike partial, both shaded by Mallet */
static void bar(mv_t *v, float f, float amp, float t60, float up, float beat)
{
    float b = beat * 0.5f;
    part_set(v, f - b, amp * 0.5f, t60);
    part_set(v, f + b, amp * 0.5f, t60 * 0.96f);
    part_set(v, f * 2.76f, amp * up * (0.25f + 0.75f * mallet), t60 * 0.3f);
    part_set(v, f * 5.40f, amp * up * mallet * 0.35f, 0.06f);
}

static void damp_last(int inst, float t60)
{
    int i = inst_last[inst];
    if (i >= 0 && mv[i].np && mv[i].inst == inst && mv[i].serial == inst_last_serial[inst])
        mv_damp(&mv[i], t60);
}
static void set_last(int inst, mv_t *v)
{
    inst_last[inst] = (int8_t)(v - mv);
    inst_last_serial[inst] = v->serial;
}

static void strike(int inst, int deg, float vel)
{
    mv_t *v;
    float f, a, sr = sirep ? 0.45f : 1.0f;
    if (vel <= 0.0f)
        return;
    en_strokes++;
    switch (inst) {
    case I_GONG: {                              /* low, long, blooming: a pair 2.6 Hz apart beats; a near partial in
                                                 * antiphase cancels the start and fades, so the tone swells */
        float fg = deg_hz(deg);
        while (fg > 62.0f)
            fg *= 0.5f;
        while (fg < 38.0f)
            fg *= 2.0f;
        a = 0.50f * vel * g_gongs;
        v = mv_alloc(inst);
        part_set(v, fg, a, 14.0f);
        part_set(v, fg + 2.6f, a * 0.8f, 12.0f);
        part_set(v, fg * 1.003f, -a * 0.75f, 0.9f);
        part_set(v, fg * 2.01f, a * 0.42f, 7.0f);
        part_set(v, fg * 2.93f, a * 0.10f, 3.0f);
        pan_set(v, 0.0f, 0.45f);
        en_gongs++;
        en_glow[K_GONG] = 1.0f;
        return;
    }
    case I_KEMPUL:
        f = deg_hz(fold(deg, -5, -1));
        a = 0.34f * vel * g_gongs;
        v = mv_alloc(inst);
        part_set(v, f, a, 5.0f);
        part_set(v, f + 2.2f, a * 0.6f, 4.2f);
        part_set(v, f * 1.004f, -a * 0.5f, 0.5f);
        part_set(v, f * 2.02f, a * 0.25f, 2.2f);
        part_set(v, f * 3.1f, a * 0.06f, 0.8f);
        pan_set(v, 0.25f, 0.5f);
        en_glow[K_KEMPUL] = 1.0f;
        return;
    case I_KENONG:
        f = deg_hz(fold(deg, 3, 7));
        a = 0.26f * vel * g_gongs;
        damp_last(inst, 1.2f);
        v = mv_alloc(inst);
        part_set(v, f - 1.2f, a * 0.55f, 3.2f);
        part_set(v, f + 1.2f, a * 0.45f, 3.0f);
        part_set(v, f * 1.502f, a * 0.10f, 1.0f);
        part_set(v, f * 2.03f, a * 0.12f, 1.2f);
        part_set(v, f * 4.1f, a * 0.04f * (0.4f + mallet), 0.15f);
        pan_set(v, -0.3f, 0.5f);
        set_last(inst, v);
        en_glow[K_KENONG] = 1.0f;
        return;
    case I_KETUK:                               /* a small kettle, damped at once: "tuk" */
        f = deg_hz(1);
        a = 0.30f * vel * g_gongs;
        v = mv_alloc(inst);
        part_set(v, f, a, 0.22f);
        part_set(v, f * 1.53f, a * 0.35f, 0.12f);
        part_set(v, f * 2.9f, a * 0.15f, 0.05f);
        pan_set(v, 0.45f, 0.3f);
        en_glow[K_KETUK] = 1.0f;
        return;
    case I_KEMPYANG:
        f = deg_hz(10);
        a = 0.10f * vel * g_gongs;
        v = mv_alloc(inst);
        part_set(v, f, a, 0.18f);
        part_set(v, f * 2.71f, a * 0.2f, 0.06f);
        pan_set(v, 0.6f, 0.3f);
        en_glow[K_KEMPYANG] = 1.0f;
        return;
    case I_SARON:
        f = deg_hz(deg);
        damp_last(inst, 0.18f);
        v = mv_alloc(inst);
        bar(v, f, 0.40f * vel * g_bal * sr, 2.4f, 0.4f, ombak_hz);
        pan_set(v, 0.15f, 0.55f);
        set_last(inst, v);
        en_glow[K_BAR] = 1.0f;
        return;
    case I_SLENTHEM:                            /* over tubes: round, long, few overtones */
        f = deg_hz(deg - EN_NTONE);
        damp_last(inst, 0.35f);
        v = mv_alloc(inst);
        a = 0.21f * vel * g_bal * (sirep ? 0.8f : 1.0f);
        part_set(v, f - ombak_hz * 0.25f, a * 0.5f, 4.5f);
        part_set(v, f + ombak_hz * 0.25f, a * 0.5f, 4.3f);
        part_set(v, f * 3.0f, a * 0.04f * (0.5f + mallet), 0.8f);
        pan_set(v, -0.1f, 0.6f);
        set_last(inst, v);
        return;
    case I_PEKING:
        f = deg_hz(fold(deg + EN_NTONE, 8, 14));
        damp_last(inst, 0.12f);
        v = mv_alloc(inst);
        bar(v, f, 0.15f * vel * g_bal * sr, 1.4f, 0.3f, ombak_hz);
        pan_set(v, 0.5f, 0.55f);
        set_last(inst, v);
        break;
    case I_POLOS: case I_SANGSIH: {             /* the pair: Bali damps hard, Jawa (gender-like) lets it ring */
        int d = fold(deg + EN_NTONE, 7, 14);
        f = deg_hz(d);
        damp_last(inst, style ? 0.09f : 0.45f);
        v = mv_alloc(inst);
        bar(v, f, (style ? 0.20f : 0.17f) * vel * g_kot, style ? 1.3f : 2.6f, style ? 0.35f : 0.15f, ombak_hz);
        pan_set(v, inst == I_POLOS ? -0.55f : 0.55f, 0.6f);
        set_last(inst, v);
        en_tone_glow[pc_of(d)] = 1.0f;
        return;
    }
    case I_BONANG: {                            /* a kettle: the pair, a soft upper partial, a little ring */
        int d = fold(deg, 4, 12);
        f = deg_hz(d);
        damp_last(inst, 0.4f);
        v = mv_alloc(inst);
        a = 0.25f * vel * g_bon * sr;
        part_set(v, f - ombak_hz * 0.5f, a * 0.5f, 1.6f);
        part_set(v, f + ombak_hz * 0.5f, a * 0.5f, 1.5f);
        part_set(v, f * 2.02f, a * 0.10f * (0.4f + mallet), 0.5f);
        part_set(v, f * 3.9f, a * 0.04f * mallet, 0.08f);
        pan_set(v, -0.35f, 0.6f);
        set_last(inst, v);
        en_tone_glow[pc_of(d)] = 1.0f;
        return;
    }
    case I_KEY:                                 /* the player's: a bar, never damped by the music */
        f = deg_hz(deg);
        damp_last(inst, 0.3f);
        v = mv_alloc(inst);
        bar(v, f, 0.30f * vel, 2.0f, 0.4f, ombak_hz);
        pan_set(v, 0.0f, 0.55f);
        set_last(inst, v);
        en_tone_glow[pc_of(deg)] = 1.0f;
        return;
    default: return;
    }
    en_tone_glow[pc_of(fold(deg + EN_NTONE, 8, 14))] = 0.7f;   /* (the peking) */
}

/* ---------------------------------------------------------------- kendang --- */
/* a pitched membrane (a sine falling to its pitch) and a slap (noise through a band-pass): dhe low, tak a slap, tung
 * open, ket a muted tap */
#define NKD 3
typedef struct {
    float ph, f, fe, fk, a, ak, n, nk, lo, bd, f1, q, gl, gr;
} kd_t;
static kd_t kd[NKD];
static int kd_next;
static void kendang(int stroke, float vel)
{
    kd_t *k = &kd[kd_next];
    float a = vel * g_kend;
    kd_next = (kd_next + 1) % NKD;
    if (a <= 0.0f)
        return;
    k->ph = 0.0f;
    k->lo = k->bd = 0.0f;
    switch (stroke) {
    case 'd': k->f = 118.0f; k->fe = 74.0f; k->fk = 0.9993f; k->a = 0.55f * a; k->ak = 0.99955f;
        k->n = 0.06f * a; k->nk = 0.996f; k->f1 = 0.06f; k->q = 1.2f; break;
    case 't': k->f = 340.0f; k->fe = 300.0f; k->fk = 0.998f; k->a = 0.16f * a; k->ak = 0.9975f;
        k->n = 0.30f * a; k->nk = 0.9978f; k->f1 = 0.25f; k->q = 0.7f; break;
    case 'u': k->f = 205.0f; k->fe = 185.0f; k->fk = 0.999f; k->a = 0.32f * a; k->ak = 0.9992f;
        k->n = 0.05f * a; k->nk = 0.996f; k->f1 = 0.1f; k->q = 1.0f; break;
    default:  k->f = 270.0f; k->fe = 260.0f; k->fk = 0.998f; k->a = 0.10f * a; k->ak = 0.9965f;
        k->n = 0.10f * a; k->nk = 0.996f; k->f1 = 0.17f; k->q = 0.8f; break;
    }
    k->gl = 0.8f;
    k->gr = 0.6f;
    en_glow[stroke == 'd' ? K_DHE : stroke == 't' || stroke == 'k' ? K_TAK : K_TUNG] = 1.0f;
}
static void kendang_run(uint32_t n)
{
    int i;
    uint32_t j;
    for (i = 0; i < NKD; i++) {
        kd_t *k = &kd[i];
        if (k->a < 1e-5f && k->n < 1e-5f)
            continue;
        for (j = 0; j < n; j++) {
            float x = sine(k->ph) * k->a, h;
            k->ph += k->f * (1.0f / EN_SR);
            if (k->ph >= 1.0f)
                k->ph -= 1.0f;
            k->f = k->fe + (k->f - k->fe) * k->fk;
            k->a *= k->ak;
            h = noise() * k->n - k->lo - k->q * k->bd;   /* Chamberlin's state-variable band-pass */
            k->bd += k->f1 * h;
            k->lo += k->f1 * k->bd;
            k->n *= k->nk;
            x += k->bd;
            mixl[j] += x * k->gl;
            mixr[j] += x * k->gr;
            rsend[j] += x * 0.3f;
        }
        k->bd = fm_flush(k->bd);
        k->lo = fm_flush(k->lo);
    }
}

/* ------------------------------------------------------------------ suling --- */
/* a bamboo flute: a sine with a little of its second and third, a slow vibrato, breath; phrases that float over the
 * piece and settle on the next kenong's tone */
static struct {
    float ph, f, ft, amp, at, vph, br;
    float left;                                 /* seconds left on this note */
    int notes;                                  /* left in the phrase; 0 resting */
    int deg;
    float rest;                                 /* seconds until the next phrase */
} sl;
static int sul_target(void);
static void suling_control(float dt)
{
    if (g_sul <= 0.0f || (en_state != S_PLAY && en_state != S_SUWUK)) {
        sl.at = 0.0f;
        return;
    }
    if (sl.notes <= 0) {
        sl.at = 0.0f;
        sl.rest -= dt;
        if (sl.rest <= 0.0f) {
            sl.notes = 3 + irand(4);
            sl.left = 0.0f;
            sl.deg = fold(sul_target() + irand(5) - 2 + EN_NTONE, 9, 15);
        }
        return;
    }
    sl.left -= dt;
    if (sl.left <= 0.0f) {
        int d;
        if (--sl.notes <= 0) {                  /* the phrase over: a breath */
            sl.rest = 2.0f + 7.0f * frand();
            sl.at = 0.0f;
            return;
        }
        d = sl.notes == 1 ? fold(sul_target() + EN_NTONE, 9, 15) : sl.deg + irand(5) - 2;
        sl.deg = d < 9 ? d + 2 : d > 15 ? d - 2 : d;
        sl.ft = deg_hz(sl.deg);
        if (sl.f <= 0.0f)
            sl.f = sl.ft;
        sl.left = sl.notes == 1 ? 2.5f + 2.0f * frand() : 0.5f + 1.6f * frand();
        sl.at = 0.075f * g_sul * (0.75f + 0.25f * frand());
    }
}
static void suling_run(uint32_t n)
{
    uint32_t j;
    if (sl.amp < 1e-5f && sl.at <= 0.0f)
        return;
    for (j = 0; j < n; j++) {
        float x, v;
        sl.f += (sl.ft - sl.f) * 0.0012f;
        sl.amp += (sl.at - sl.amp) * (sl.at > sl.amp ? 0.00025f : 0.00012f);
        sl.vph += 5.2f / EN_SR;
        if (sl.vph >= 1.0f)
            sl.vph -= 1.0f;
        v = 1.0f + 0.0035f * sine(sl.vph) * fm_minf(sl.amp * 40.0f, 1.0f);
        sl.ph += sl.f * v * (1.0f / EN_SR);
        if (sl.ph >= 1.0f)
            sl.ph -= 1.0f;
        sl.br += 0.08f * (noise() - sl.br);
        x = (sine(sl.ph) + 0.16f * sine(sl.ph * 2.0f) + 0.05f * sine(sl.ph * 3.0f) + 0.35f * sl.br) * sl.amp;
        mixl[j] += x * 0.75f;
        mixr[j] += x * 0.95f;
        rsend[j] += x * 0.9f;
    }
}

/* ------------------------------------------------------------- the cycle --- */
/* the forms: beats, and for each beat what marks it (bit b: beat b; the gong on the last) */
typedef struct {
    uint8_t nb;
    uint32_t gong, kempul, kenong, ketuk, kempyang;
} form_t;
#define BIT(b) (1u << (b))
static const form_t FORMS[EN_NFORM] = {
    {0, 0, 0, 0, 0, 0},
    /* Lancaran: . t . N  . t P N  . t P N  . t P G */
    {16, BIT(15), BIT(6) | BIT(10) | BIT(14), BIT(3) | BIT(7) | BIT(11) | BIT(15), BIT(1) | BIT(5) | BIT(9) | BIT(13),
     0},
    /* Ketawang: p t p .  p t p N  p t p P  p t p G */
    {16, BIT(15), BIT(11), BIT(7) | BIT(15), BIT(1) | BIT(5) | BIT(9) | BIT(13), 0x5555u},
    /* Ladrang: four kenongan of 8, kempul in the second half of the last three */
    {32, BIT(31), BIT(11) | BIT(19) | BIT(27), BIT(7) | BIT(15) | BIT(23) | BIT(31),
     BIT(1) | BIT(5) | BIT(9) | BIT(13) | BIT(17) | BIT(21) | BIT(25) | BIT(29), 0x55555555u},
    /* Gilak (Bali): gong on 8, kempur on 4, klentong on 6, the kempli on the beats between */
    {8, BIT(7), BIT(3), BIT(5), BIT(0) | BIT(1) | BIT(2) | BIT(4) | BIT(6), 0},
};

#define BLO 3                                    /* the balungan's range: degrees */
#define BHI 11
static int form, nb;
static int8_t bal[2][EN_MAXBEATS];               /* the sections, A and B */
static int nsec, sec, snext;
static int gong_pc;
static int beat, gt;                             /* where: the beat, the grid tick in it */
static int level, sub;                           /* irama: 0..2; grid ticks a beat, 4 << level */
static float laya = 1.0f, laya_t = 1.0f, laya_base = 1.0f;   /* the tick's length, times the nominal */
static float tick_left, lp;                      /* samples to the next tick; the nominal tick */
static int trans, ramp_pending, signal_gatra;    /* an irama change: under way (+1 slower, -1 faster), next gatra */
static int ending, end_beat;                     /* the last gongan: it ends at end_beat */
static float elapsed, target_secs, gap_left;
static int8_t cur;                               /* the balungan's last note */
static int8_t kfig[16];                          /* this beat's kotekan figure (grid ticks) */
static int8_t bfig[16];                          /* the bonang's, over two beats (every other tick) */
static int8_t pfig[8];                           /* the peking's, this beat (every other tick) */
static const char *kpat = "........";            /* the kendang's pattern this gatra (8 slots) */
static uint8_t on_kot, on_bon, on_pek, kot_style;

static int bal_at(int b)                         /* beat b of this gongan, or (b >= nb) of the next */
{
    return b < nb ? bal[sec][b] : bal[snext][b - nb];
}
static int next_sounded(int b)                   /* the first sounded note after beat b */
{
    int i;
    for (i = b + 1; i < b + 1 + nb; i++) {
        int d = bal_at(i);
        if (d >= 0)
            return d;
    }
    return cur >= 0 ? cur : 7;
}
static int sounded_at(int b)                     /* beat b's note, or the next sounded one */
{
    int d = bal_at(b);
    return d >= 0 ? d : next_sounded(b);
}
static int sul_target(void)                      /* the next kenong's tone */
{
    int i;
    for (i = beat + 1; i < beat + 1 + nb; i++)
        if ((FORMS[form].kenong >> (i % nb)) & 1u)
            return sounded_at(i);
    return cur >= 0 ? cur : 7;
}

/* ---------------------------------------------------- making balungan --- */
static int pick_strong(void)
{
    float r = frand();
    return STRONG[tuning][r < 0.55f ? 0 : r < 0.85f ? 1 : 2];
}
static int near_pc(int pc, int want, int lo, int hi)   /* the degree of tone pc nearest want, in lo..hi */
{
    int d = fold(pc + EN_NTONE * deg_oct(want), lo, hi), best = d, k;
    for (k = -2; k <= 2; k++) {
        int c = d + k * EN_NTONE;
        if (c >= lo && c <= hi && (c - want) * (c - want) < (best - want) * (best - want))
            best = c;
    }
    return best;
}
/* a gatra's first three notes, relative to its last */
static const int8_t SHAPES[][3] = {{1, 2, 1}, {-1, -2, -1}, {2, 1, -1}, {-2, -1, 1}, {1, -1, 1}, {-1, 1, -1},
                                   {3, 2, 1}, {-3, -2, -1}, {2, 3, 1}, {-2, -3, -1}, {2, 0, 1}, {-1, 0, -2},
                                   {1, 2, -1}, {-1, -2, 1}, {0, 2, 1}, {0, -1, 1}};
#define NSHAPES (int)(sizeof SHAPES / sizeof SHAPES[0])
static void fill_gatra(int8_t *b, int from, int e, float nibani)
{
    int s, best = 0;
    float bs = 1e9f;
    for (s = 0; s < NSHAPES; s++) {
        int k, ok = 1;
        float sc;
        for (k = 0; k < 3; k++)
            if (e + SHAPES[s][k] < BLO || e + SHAPES[s][k] > BHI)
                ok = 0;
        if (!ok)
            continue;
        sc = (float)((e + SHAPES[s][0] - from) * (e + SHAPES[s][0] - from)) * 0.6f + 3.0f * frand();
        if (e + SHAPES[s][0] == from)
            sc += 1.5f;                          /* not the same note again at once */
        if (sc < bs) {
            bs = sc;
            best = s;
        }
    }
    b[0] = (int8_t)(e + SHAPES[best][0]);
    b[1] = (int8_t)(e + SHAPES[best][1]);
    b[2] = (int8_t)(e + SHAPES[best][2]);
    b[3] = (int8_t)e;
    if (frand() < nibani) {                      /* nibani: every other beat */
        b[0] = -1;
        b[2] = -1;
    }
}
static float nibani_p(void) { return (1.0f - (float)par[P_DENSITY] * 0.01f) * (form == F_LANCARAN ? 0.75f : 0.4f); }
static void gen_section(int8_t *b)
{
    int ng = nb / 4, gi, prev = near_pc(gong_pc, 7, BLO + 1, BHI - 1), prevk = -1;
    for (gi = 0; gi < ng; gi++) {
        int pos = gi * 4 + 3, pc, e;
        if (pos == nb - 1) {
            pc = gong_pc;
        } else if ((FORMS[form].kenong >> pos) & 1u) {
            do
                pc = frand() < 0.7f ? pick_strong() : irand(EN_NTONE);
            while (pc == prevk && frand() < 0.8f);
            prevk = pc;
        } else {
            pc = (pc_of(prev) + irand(5) - 2 + EN_NTONE) % EN_NTONE;
        }
        e = near_pc(pc, prev + irand(3) - 1, BLO + 1, BHI - 1);
        fill_gatra(b + gi * 4, prev, e, nibani_p());
        prev = e;
    }
}
static void vary_section(int8_t *b)              /* garap: one gatra (not its last note) made anew */
{
    int gi = irand(nb / 4), from;
    from = gi ? b[gi * 4 - 1] : b[nb - 1];
    if (from < 0)
        from = 7;
    fill_gatra(b + gi * 4, from, b[gi * 4 + 3], nibani_p());
}
static void set_form(int f)
{
    form = f;
    nb = FORMS[f].nb;
    en_form = (uint8_t)f;
    en_nbeats = (uint8_t)nb;
    en_mk_gong = FORMS[f].gong;
    en_mk_kempul = FORMS[f].kempul;
    en_mk_kenong = FORMS[f].kenong;
    en_mk_ketuk = FORMS[f].ketuk;
    en_mk_kempyang = FORMS[f].kempyang;
}
static void publish_bal(void)
{
    int i;
    for (i = 0; i < EN_MAXBEATS; i++)
        en_bal[i] = (int8_t)(i < nb ? bal[sec][i] : -1);
}
void en_gen_test(uint32_t seed, int f, int t, int8_t *b, int *n)
{
    uint32_t keep = rng;
    int kt = tuning;
    rng = seed ? seed : 1u;
    tuning = t;
    set_form(f);
    gong_pc = pick_strong();
    gen_section(b);
    *n = nb;
    rng = keep;
    tuning = kt;
}

/* ------------------------------------------------------- elaboration --- */
/* the kotekan's figure for a beat: n grid ticks from a (landed now) toward b (the next note, landed at the next
 * beat). Norot: the target and the tone above, back and forth; nyog cag: a walk to it, turning on the way; telu:
 * three tones around it. Polos takes the even ticks, sangsih the odd: each part alone is half the figure. */
static void kot_figure(int a, int b, int n, int8_t *f)
{
    int k, dir = b > a ? 1 : b < a ? -1 : (irand(2) ? 1 : -1);
    f[0] = (int8_t)a;
    switch (kot_style) {
    case 0:                                       /* norot */
        for (k = 1; k < n; k++) {
            int to = k * 2 < n ? a : b;
            f[k] = (int8_t)(((n - k) & 1) ? to + 1 : to);
        }
        break;
    case 1: {                                     /* nyog cag: back from the target, then turning at the start */
        int x = b;
        for (k = n - 1; k >= 1; k--) {
            int want = x - dir;
            x = (want - a) * dir < 0 ? (((n - k) & 1) ? a + dir : a) : want;
            f[k] = (int8_t)x;
        }
        break;
    }
    default: {                                    /* telu */
        static const int8_t C[3] = {0, -1, 1};
        for (k = 1; k < n; k++)
            f[k] = (int8_t)(b + C[(k + 3 - n % 3) % 3]);
        f[n - 1] = (int8_t)(b + (b > a ? -1 : 1));
        break;
    }
    }
}

/* the kendang's patterns, a gatra each (8 slots: half beats): . rest, d dhe, t tak, u tung, k ket */
static const char *const KP_CALM[] = {"....t..d", "..t...td", ".t.k.t.d", "...t..dd", ".k.t.k.d"};
static const char *const KP_BUSY[] = {"t.dt.tkd", ".tdt.dtd", "tkt.dt.d", "k.tdk.td"};
static const char *const KP_SIGNAL[] = {"tdtdkt.d", "ttdt.ttd", "dtkd.tdd"};
static const char *const KP_END[] = {"d..t..dd", "t.d.t..d"};
static const char *const KP_BUKA[] = {"....tt.d", ".....tdd"};
#define PICK(a) (a[irand((int)(sizeof a / sizeof a[0]))])

static void gatra_start(void)
{
    float d = (float)par[P_DENSITY] * 0.01f;
    if (ramp_pending) {                          /* the signal was given: the beat moves now */
        trans = ramp_pending;
        ramp_pending = 0;
        laya_t = trans > 0 ? 2.0f : 0.5f;
        en_trend = (int8_t)(trans > 0 ? 1 : -1);
    }
    if (en_state == S_BUKA)
        kpat = PICK(KP_BUKA);
    else if (signal_gatra) {
        kpat = PICK(KP_SIGNAL);
        ramp_pending = signal_gatra;
        signal_gatra = 0;
    } else if (ending && beat >= end_beat - 3)
        kpat = PICK(KP_END);
    else
        kpat = frand() < 0.25f + 0.55f * d ? PICK(KP_BUSY) : PICK(KP_CALM);
    if (ending && beat >= end_beat - 7 && beat <= end_beat) {   /* the ending: slowing to the gong */
        laya_t = 2.3f;
        trans = 0;
        en_trend = 1;
        en_state = S_SUWUK;
    }
}

static int max_level(void)
{
    if (par[P_IRAMA])
        return par[P_IRAMA] - 1;
    if (style)
        return 0;
    return form == F_LADRANG ? 2 : form == F_KETAWANG ? (par[P_DENSITY] >= 35 ? 2 : 1) : form == F_LANCARAN ? 1 : 0;
}
static void irama_request(int dir)               /* the kendang signals this gatra; the beat moves in the next */
{
    int to = level + dir;
    if (trans || ramp_pending || signal_gatra || to < 0 || to > 2 || ending)
        return;
    signal_gatra = dir;
}

static void choose_layers(void)
{
    float d = (float)par[P_DENSITY] * 0.01f;
    on_kot = (uint8_t)(frand() < (style ? 0.75f + 0.25f * d : 0.35f + 0.5f * d + 0.15f * (float)level));
    on_bon = (uint8_t)(frand() < 0.6f + 0.4f * d);
    on_pek = (uint8_t)(frand() < 0.3f + 0.6f * d);
    if (frand() < (style ? 0.5f : 0.35f))
        kot_style = (uint8_t)(style ? irand(3) : irand(2));
    sirep = (uint8_t)(!style && level >= 1 && frand() < 0.3f);
    en_sirep = sirep;
}

static float beat_secs(void) { return lp * laya * (float)sub * (1.0f / EN_SR); }

/* at the last gatra's start: what the next gongan is */
static void decide_next(void)
{
    float phase = target_secs > 0.0f ? elapsed / target_secs : 0.0f, next = beat_secs() * (float)nb;
    snext = nsec == 2 ? ((en_gongan + 1) / 2) % 2 : 0;
    if (frand() < 0.25f)
        vary_section(bal[snext]);
    if (ending)
        return;
    if (!en_hold_on && elapsed + next > target_secs) {   /* time to end: a faster irama first (Jawa), then the end */
        if (!par[P_IRAMA] && level > 0) {
            irama_request(-1);
        } else {
            ending = 1;
            end_beat = nb - 1 + nb;              /* the next gongan's gong */
        }
        return;
    }
    if (!par[P_IRAMA] && !style) {               /* irama: deeper early on, back later */
        if (phase < 0.6f && level < max_level() && frand() < 0.55f)
            irama_request(1);
        else if (phase >= 0.6f && level > 0 && frand() < 0.45f)
            irama_request(-1);
    } else if (par[P_IRAMA] && level != par[P_IRAMA] - 1) {
        irama_request(par[P_IRAMA] - 1 > level ? 1 : -1);
    }
    {                                            /* the beat drifts (Bali: surges) */
        float fl = (float)par[P_FLOW] * 0.01f;
        laya_base = style ? 1.0f + fl * (frand() < 0.3f ? -0.28f : 0.3f * (frand() - 0.45f))
                          : 1.0f + fl * 0.16f * (frand() - 0.5f);
        if (!trans && !ramp_pending)
            laya_t = laya_base;
    }
}

static void finish(void)                         /* the last gong: all land, then the ring */
{
    en_state = S_GAP;
    gap_left = 5.0f + 4.0f * frand();
    ending = 0;
    trans = 0;
    en_trend = 0;
}

static void new_piece(void)
{
    int f = par[P_FORM];
    float r = frand();
    tuning = par[P_TUNING];
    en_tuning = (uint8_t)tuning;
    style = par[P_STYLE];
    if (!f)
        f = style ? (r < 0.7f ? F_GILAK : F_LANCARAN)
                  : (r < 0.25f ? F_LANCARAN : r < 0.6f ? F_KETAWANG : F_LADRANG);
    set_form(f);
    gong_pc = pick_strong();
    nsec = (frand() < 0.5f) ? 2 : 1;
    gen_section(bal[0]);
    gen_section(bal[1]);
    if (nsec == 2) {                             /* B ends on A's gong tone, and starts elsewhere */
        bal[1][nb - 1] = bal[0][nb - 1];
    }
    sec = snext = 0;
    publish_bal();
    level = par[P_IRAMA] ? par[P_IRAMA] - 1 : 0;
    sub = 4 << level;
    en_level = (uint8_t)level;
    en_sub = (uint8_t)sub;
    laya = 1.15f;
    laya_base = laya_t = 1.0f;
    trans = ramp_pending = signal_gatra = 0;
    ending = 0;
    elapsed = 0.0f;
    target_secs = 200.0f + 220.0f * frand() + (float)nb * 2.0f;
    en_gongan = 0;
    beat = nb - 4;                               /* the buka: the last gatra, bonang alone, into the gong */
    gt = 0;
    tick_left = 0.0f;
    cur = (int8_t)bal_at(nb - 5 >= 0 ? nb - 5 : 0);
    if (cur < 0)
        cur = (int8_t)sounded_at(beat);
    en_cur = -1;
    kot_style = (uint8_t)(style ? irand(3) : irand(2));
    on_bon = 1;
    on_kot = on_pek = 0;
    sirep = 0;
    en_sirep = 0;
    en_state = S_BUKA;
    en_trend = 0;
    sl.notes = 0;
    sl.rest = 6.0f + 6.0f * frand();
    en_pieces++;
}

/* a beat starts: the colotomy, the balungan, the figures for the beat ahead */
static void beat_start(void)
{
    int b = beat, d = bal_at(b), nxt;
    float acc = (b % 4 == 3) ? 1.0f : 0.82f;
    int buka = en_state == S_BUKA, last = ending && b == end_beat;
    if (buka && b == nb - 1)
        en_state = S_PLAY, buka = 0;             /* the gong: everyone in */
    if (b % 4 == 0)
        gatra_start();
    if (b == nb - 4 && !buka)
        decide_next();
    if (trans > 0 && laya >= 1.41f) {            /* the beat has slowed enough: the elaboration doubles */
        level++;
        sub <<= 1;
        laya *= 0.5f;
        laya_t = laya_base;
        trans = 0;
        en_trend = 1;
        en_irama_changes++;
    } else if (trans < 0 && laya <= 0.71f) {     /* or quickened enough: it halves */
        level--;
        sub >>= 1;
        laya *= 2.0f;
        laya_t = laya_base;
        trans = 0;
        en_trend = -1;
        en_irama_changes++;
    } else if (!trans && !ramp_pending && en_state != S_SUWUK && fm_fabsf(laya - laya_t) < 0.03f) {
        en_trend = 0;
    }
    en_level = (uint8_t)level;
    en_sub = (uint8_t)sub;
    if (!buka) {                                 /* the colotomy */
        uint32_t m = 1u << b;
        int kd_b = sounded_at(b);
        if ((FORMS[form].gong & m) || last)
            strike(I_GONG, gong_pc, 1.0f);
        if ((FORMS[form].kenong & m) || last) {
            strike(I_KENONG, kd_b, 0.9f + 0.1f * frand());
            en_kenong_at = (int8_t)b;
        }
        if (FORMS[form].kempul & m)
            strike(I_KEMPUL, kd_b, 0.85f + 0.15f * frand());
        if ((FORMS[form].ketuk & m) && !last)
            strike(I_KETUK, 0, (style ? 0.55f : 0.8f) + 0.15f * frand());
        if ((FORMS[form].kempyang & m) && !last)
            strike(I_KEMPYANG, 0, 0.7f + 0.2f * frand());
        if (d >= 0) {                            /* the balungan */
            strike(I_SARON, d, acc * (0.85f + 0.15f * frand()));
            strike(I_SLENTHEM, d, acc * (0.8f + 0.2f * frand()));
        }
    }
    if (d >= 0) {
        cur = (int8_t)d;
        en_cur = (int8_t)d;
    }
    if (last) {
        if (on_kot) {                            /* the pair and the bonang land with it */
            strike(I_POLOS, cur, 0.8f);
            strike(I_SANGSIH, cur + 2, 0.6f);
        }
        if (on_bon)
            strike(I_BONANG, cur, 0.8f);
        kendang('d', 1.0f);
        finish();
        return;
    }
    nxt = next_sounded(b);
    kot_figure(cur, nxt, sub, kfig);
    {                                            /* the peking: the note ahead, between this one (each twice) */
        int h, hn = sub / 2;
        for (h = 0; h < hn && h < 8; h++)
            pfig[h] = (int8_t)(h & 1 ? nxt : cur);
    }
    if ((b & 1) == 1 || b == nb - 4) {           /* the bonang: a pair of beats, mipil: p q p q, landing on q */
        int p = sounded_at(b + 1), qn = sounded_at(b + 2), hn = sub, h;
        if (hn > 16)
            hn = 16;
        for (h = 0; h < hn; h++)
            bfig[h] = (int8_t)(((hn - h) & 1) ? p : qn);
        bfig[0] = cur;
    }
}

static void tick(void)
{
    int buka = en_state == S_BUKA, h;
    en_ticks++;
    if (gt == 0) {
        beat_start();
        if (en_state == S_GAP)
            return;
        buka = en_state == S_BUKA;
    }
    {
        float v = (gt == 0 ? 0.9f : (gt & 1) ? 0.62f : 0.75f) + 0.12f * frand();
        if (on_kot && !buka) {                   /* the kotekan */
            int n = kfig[gt];
            strike((gt & 1) ? I_SANGSIH : I_POLOS, n, v);
            if (style && gt == 0 && frand() < 0.4f)   /* a landing doubled a fourth up by sangsih */
                strike(I_SANGSIH, n + 2, v * 0.6f);
        }
        if (!(gt & 1)) {
            h = gt >> 1;
            if (on_pek && !buka)
                strike(I_PEKING, pfig[h < 8 ? h : 7], 0.7f + 0.15f * frand());
            if (on_bon || buka) {                /* the bonang: every other tick, over a pair of beats */
                int idx = ((beat & 1) ? 0 : sub / 2) + h;
                if (beat == nb - 4 && buka)
                    idx = h;
                if (idx < 16 && (idx & 1 || frand() < 0.85f))
                    strike(I_BONANG, bfig[idx], 0.75f + 0.2f * frand());
            }
        }
    }
    {                                            /* the kendang: 8 slots a gatra */
        int slot_len = sub / 2, pos = (beat % 4) * sub + gt;
        if (pos % slot_len == 0) {
            char c = kpat[(pos / slot_len) & 7];
            if (c != '.' && (!buka || (beat % 4) >= 2))
                kendang(c, 0.7f + 0.3f * frand());
        }
    }
    if (++gt >= sub) {
        gt = 0;
        if (++beat >= nb) {                      /* the gongan turns */
            beat = 0;
            if (ending)
                end_beat -= nb;
            sec = snext;
            publish_bal();
            en_gongan++;
            choose_layers();
        }
    }
    laya += (laya_t - laya) * (trans ? 0.6f : en_state == S_SUWUK ? 0.8f : 0.35f) / (float)(sub * 4);
}

/* ------------------------------------------------- reverb: Dattorro's plate --- */
/* J. Dattorro, "Effect Design, Part 1", JAES 1997 (as in AMB-1 and FoMni): a predelay, four input diffusers, then a
 * figure-eight tank of two halves, each a modulated allpass, a delay, a damping lowpass, an allpass and a delay,
 * feeding the other half. Stereo out from seven taps a side. The paper's lengths are for 29761 Hz; DS() scales them
 * to 44.1 kHz. */
#define DS(n) (((n) * 14818 + 5000) / 10000)          /* 44100 / 29761 */
#define PRE_N 1323                                   /* 30 ms */
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
static float pl_bw, pl_damp_l, pl_damp_r, pl_decay = 0.6f, pl_decay_t = 0.6f, pl_dd2 = 0.5f, lfo_s, lfo_c = 1.0f;

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
    pl_bw += 0.55f * (x - pl_bw);
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

/* ------------------------------------------------------------ parameters --- */
static float rev_send = 0.5f, lp_k = 0.5f, lp_l, lp_r;
static void apply(int p, int v)
{
    const en_param_t *pi = en_param_info(p);
    v = v < pi->lo ? pi->lo : v > pi->hi ? pi->hi : v;
    par[p] = (int16_t)v;
    switch (p) {
    case P_TEMPO: lp = EN_SR * 60.0f / ((float)v * 4.0f); break;
    case P_TUNING: tuning = v; en_tuning = (uint8_t)v; break;
    case P_OMBAK: ombak_hz = (float)v * 0.08f; break;
    case P_GONGS: g_gongs = lvl(v) * 1.6f; break;
    case P_BALUNGAN: g_bal = lvl(v) * 1.8f; break;
    case P_KOTEKAN: g_kot = lvl(v) * 1.8f; break;
    case P_BONANG: g_bon = lvl(v) * 2.0f; break;
    case P_KENDANG: g_kend = lvl(v) * 1.8f; break;
    case P_SULING: g_sul = lvl(v) * 2.2f; break;
    case P_REVERB:
        rev_send = (float)v * 0.012f;
        pl_decay_t = 0.45f + 0.4f * (float)v * 0.01f;
        break;
    case P_MALLET:
        mallet = (float)v * 0.01f;
        lp_k = 1.0f - fm_expf(-FM_TWO_PI * (2600.0f + 70.0f * (float)v) / EN_SR);
        break;
    case P_PITCH: base_hz = 130.81f * fm_exp2f((float)v * (1.0f / 12.0f)); break;
    case P_STYLE: style = v; break;
    default: break;
    }
}

static void quiet(void)
{
    int i;
    for (i = 0; i < NMV; i++)
        mv[i].np = 0;
    for (i = 0; i < NKD; i++)
        kd[i].a = kd[i].n = 0.0f;
    sl.amp = sl.at = 0.0f;
    sl.notes = 0;
}

void en_init(void)
{
    int i;
    for (i = 0; i <= SN; i++)
        sint[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SN));
    plate_init();
    q_r = q_w = 0;
    quiet();
    for (i = 0; i < NINST; i++)
        inst_last[i] = -1;
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, PARAMS[i].def);
    pl_decay = pl_decay_t;
    en_playing = 0;
    en_state = S_REST;
    en_kenong_at = -1;
    en_cur = -1;
    new_piece();
    en_state = S_REST;
}

/* ------------------------------------------------------------ the render --- */
static void key(int kind, int deg)
{
    static const uint8_t KI[EN_NKEYKIND] = {I_KEY, I_GONG, I_KEMPUL, I_KENONG, I_KETUK, I_KEMPYANG, 0, 0, 0};
    float gg = g_gongs;
    if (kind == K_BAR) {
        strike(I_KEY, deg, 0.9f);
        return;
    }
    if (kind >= K_DHE) {
        float gk = g_kend;
        g_kend = fm_maxf(g_kend, 0.4f);
        kendang(kind == K_DHE ? 'd' : kind == K_TAK ? 't' : 'u', 0.9f);
        g_kend = gk;
        return;
    }
    g_gongs = fm_maxf(g_gongs, 0.5f);           /* the keys sound even with the gongs turned down */
    strike(KI[kind], kind == K_GONG ? gong_pc : deg, 0.9f);
    g_gongs = gg;
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
            if (a < P_NPARAMS)
                apply(a, b);
            break;
        case C_PLAY:
            if (a && !en_playing) {
                en_playing = 1;
                new_piece();
            } else if (!a && en_playing) {
                en_playing = 0;
                en_state = S_REST;
                en_trend = 0;
            }
            break;
        case C_NEWPIECE:
            if (en_state == S_PLAY && !ending) { /* at the next kenong at least a gatra ahead (or the gong) */
                int i;
                for (i = beat + 4; i < beat + 4 + nb; i++)
                    if ((FORMS[form].kenong >> (i % nb)) & 1u)
                        break;
                ending = 1;
                end_beat = i;
                laya_t = 1.6f;
                en_state = S_SUWUK;
                en_trend = 1;
            } else if (en_state == S_BUKA || en_state == S_GAP) {
                gap_left = 0.0f;
                if (en_state == S_BUKA)
                    new_piece();
            }
            break;
        case C_RESTART:
            if (en_playing) {
                en_state = S_GAP;
                gap_left = 1.2f;
                ending = 0;
                en_trend = 0;
            }
            break;
        case C_HOLD: en_hold_on = (uint8_t)(a != 0); break;
        case C_IRAMA:
            if (en_state == S_PLAY)
                irama_request((int8_t)a);
            break;
        case C_KEY: key(a, b); break;
        case C_SEED:
            rng ^= (uint32_t)(b & 0xFFFF) * 2654435761u;
            if (!rng)
                rng = 1u;
            break;
        case C_PANIC: quiet(); break;
        default: break;
        }
    }
}

static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    int i;
    float pk = 0.0f;
    for (j = 0; j < n; j++)
        mixl[j] = mixr[j] = rsend[j] = 0.0f;
    for (i = 0; i < NMV; i++)
        if (mv[i].np)
            mv_run(&mv[i], n);
    kendang_run(n);
    suling_run(n);
    for (j = 0; j < n; j++) {
        float wl, wr, l, r;
        reverb(rsend[j] * rev_send * 0.5f, &wl, &wr);
        lp_l += lp_k * (mixl[j] - lp_l);         /* Mallet: a soft one is darker */
        lp_r += lp_k * (mixr[j] - lp_r);
        l = (lp_l + wl) * 0.68f * gain;
        r = (lp_r + wr) * 0.68f * gain;
        pk = fm_maxf(pk, fm_maxf(fm_fabsf(l), fm_fabsf(r)));
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
    }
    lp_l = fm_flush(lp_l);
    lp_r = fm_flush(lp_r);
    en_out_level += (pk - en_out_level) * (pk > en_out_level ? 0.2f : 0.002f);
}

static void control(uint32_t n)                 /* once a chunk: the slow things */
{
    const float dt = (float)n * (1.0f / EN_SR);
    float dk = 1.0f - dt * 2.2f, i;
    int k;
    for (k = 0; k < EN_NKEYKIND; k++)
        en_glow[k] *= k == K_GONG ? 1.0f - dt * 0.45f : dk;
    for (k = 0; k < EN_NTONE; k++)
        en_tone_glow[k] *= 1.0f - dt * 3.0f;
    pl_decay += (pl_decay_t - pl_decay) * 0.002f;
    pl_dd2 = fm_clampf(pl_decay + 0.15f, 0.25f, 0.5f);
    if (!en_playing)
        return;
    suling_control(dt);
    if (en_state == S_GAP) {
        gap_left -= dt;
        if (gap_left <= 0.0f)
            new_piece();
        return;
    }
    elapsed += dt;
    i = (float)beat + ((float)gt + 1.0f - fm_clampf(tick_left / fm_maxf(lp * laya, 1.0f), 0.0f, 1.0f)) / (float)sub;
    en_pos = i;
    en_beat = (uint8_t)beat;
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    drain();
    while (n) {
        uint32_t k = n > CH ? CH : n;
        int timed = en_playing && (en_state == S_BUKA || en_state == S_PLAY || en_state == S_SUWUK);
        if (timed) {
            if (tick_left <= 0.0f) {
                tick();
                tick_left += lp * laya;
                if (tick_left < 1.0f)
                    tick_left = 1.0f;
            }
            if (en_state != S_GAP && (float)k > tick_left)
                k = (uint32_t)tick_left + 1u;
        }
        if (k > n)
            k = n;
        if (timed && en_state != S_GAP)
            tick_left -= (float)k;
        control(k);
        render_chunk(out, k, gain);
        out += 2 * k;
        n -= k;
    }
}
