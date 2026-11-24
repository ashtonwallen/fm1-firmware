/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen; built on AMB-1 by Charles Vestal (its command queue and Dattorro's plate, from
 * FoMni-1, are AMB-1's) */
/* PATCH BAY's instrument (patch.h). One compilation unit, -O2. */
#include "patch.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define PB_POOL
#else
#define PB_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

#define CB 32u                                   /* control block: the control modules run once a block */
#define CR (PB_SR / (float)CB)                    /* their rate: 1378 Hz */

/* ------------------------------------------------------------ the jacks --- */
const pb_jack_t PB_JACK[PB_NJACK] = {
    {M_SEQ, T_GATE, 0, "CLK"},   {M_RAND, T_GATE, 0, "CLK"},  {M_QUANT, T_CV, 0, "IN"},  {M_ENV1, T_GATE, 0, "TRIG"},
    {M_ENV2, T_GATE, 0, "TRIG"}, {M_VCO, T_CV, 0, "PITCH"},   {M_FM, T_CV, 0, "PITCH"},  {M_FM, T_CV, 0, "INDEX"},
    {M_VCF, T_AUDIO, 0, "IN"},   {M_VCF, T_CV, 0, "CUT"},     {M_VCA, T_AUDIO, 0, "IN"}, {M_VCA, T_CV, 0, "CV"},
    {M_DLY, T_AUDIO, 0, "IN"},   {M_MIX, T_AUDIO, 0, "A"},    {M_MIX, T_AUDIO, 0, "B"},  {M_MIX, T_AUDIO, 0, "C"},
    {M_VERB, T_AUDIO, 0, "IN"},
    {M_CLK, T_GATE, 1, "BEAT"},  {M_CLK, T_GATE, 1, "DIV"},   {M_SEQ, T_CV, 1, "CV"},    {M_SEQ, T_GATE, 1, "GATE"},
    {M_RAND, T_CV, 1, "S&H"},    {M_RAND, T_CV, 1, "SMOOTH"}, {M_QUANT, T_CV, 1, "NOTE"}, {M_QUANT, T_GATE, 1, "CHANGE"},
    {M_LFO1, T_CV, 1, "SINE"},   {M_LFO1, T_GATE, 1, "SQUARE"}, {M_LFO2, T_CV, 1, "SINE"}, {M_LFO2, T_GATE, 1, "SQUARE"},
    {M_ENV1, T_CV, 1, "OUT"},    {M_ENV2, T_CV, 1, "OUT"},    {M_VCO, T_AUDIO, 1, "OUT"}, {M_FM, T_AUDIO, 1, "OUT"},
    {M_NOISE, T_AUDIO, 1, "NOISE"}, {M_NOISE, T_GATE, 1, "DUST"}, {M_VCF, T_AUDIO, 1, "OUT"}, {M_VCA, T_AUDIO, 1, "OUT"},
    {M_DLY, T_AUDIO, 1, "OUT"},
};
const char *const PB_MOD_NAME[PB_NMOD] = {"CLOCK", "SEQ", "RAND", "QUANT", "LFO 1", "LFO 2", "ENV 1", "ENV 2",
                                          "VCO", "FM", "NOISE", "VCF", "VCA", "DELAY", "MIX", "REVERB"};
const char *const PB_MOD_WORDS[PB_NMOD] = {"Clock", "Sequencer", "Random", "Quantizer", "LFO 1", "LFO 2",
                                           "Envelope 1", "Envelope 2", "Oscillator", "FM pair", "Noise", "Filter",
                                           "Amplifier", "Delay", "Mixer", "Reverb"};
const char *const PB_PARAM_NAME[PB_NMOD] = {"Divide", "Length", "Rate", "Range", "Rate", "Rate", "Decay", "Decay",
                                            "Wave", "Ratio", "Dust", "Cutoff", "Level", "Repeats", "Level", "Size"};
const char *const PB_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const char *const PB_SCALE_NAME[PB_NSCALES] = {"Major", "Dorian", "Phrygian", "Lydian", "Mixolyd.", "Minor",
                                               "Pent maj", "Pent min"};
static const uint8_t SCALES[PB_NSCALES][8] = {
    {7, 0, 2, 4, 5, 7, 9, 11}, {7, 0, 2, 3, 5, 7, 9, 10}, {7, 0, 1, 3, 5, 7, 8, 10}, {7, 0, 2, 4, 6, 7, 9, 11},
    {7, 0, 2, 4, 5, 7, 9, 10}, {7, 0, 2, 3, 5, 7, 8, 10}, {5, 0, 2, 4, 7, 9},        {5, 0, 3, 5, 7, 10},
};
static const float RATIOS[10] = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f, 7.0f};
static const float DLY_Q[5] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};   /* the echo, in quarter notes */

int pb_scale(int s, int *steps)
{
    int i, n = SCALES[s % PB_NSCALES][0];
    for (i = 0; i < n; i++)
        steps[i] = SCALES[s % PB_NSCALES][i + 1];
    return n;
}

int pb_mod_jacks(int m, int *j)
{
    int k, n = 0;
    for (k = 0; k < PB_NJACK; k++)
        if (PB_JACK[k].mod == m)
            j[n++] = k;
    return n;
}

/* the knob's mappings (also the render's) */
static int clk_div(int v) { return 2 + v * 6 / 100; }
static int seq_len(int v) { return 2 + v * 6 / 100; }
static int quant_range(int v) { return 3 + v * 9 / 100; }
static float rand_hz(int v) { return 0.05f * fm_exp2f((float)v * 0.053f); }        /* 0.05 .. 2 Hz */
static float lfo1_hz(int v) { return 0.03f * fm_exp2f((float)v * 0.08f); }         /* 0.03 .. 7.7 Hz */
static float lfo2_hz(int v) { return 0.01f * fm_exp2f((float)v * 0.0665f); }       /* 0.01 .. 1 Hz */
static float env_dec(int v) { return 0.04f * fm_exp2f((float)v * 0.073f); }        /* 40 ms .. 6.3 s */
static float dust_hz(int v) { return v < 3 ? 0.0f : 0.1f * fm_exp2f((float)v * 0.0665f); }   /* .. 10 a second */
static float vcf_hz(int v) { return 60.0f * fm_exp2f((float)v * 0.075f); }         /* 60 Hz .. 11 kHz */

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
static void put_f(char *b, float x, const char *unit)   /* 0.25 -> ".25", 3.5 -> "3.5", 12 -> "12" */
{
    uint32_t c = (uint32_t)(x * 100.0f + 0.5f);
    if (c >= 1000u) {
        b = put_u(b, (c + 50u) / 100u);
    } else if (c >= 100u) {
        b = put_u(b, c / 100u);
        if (c % 100u >= 5u) {
            *b++ = '.';
            b = put_u(b, (c % 100u + 5u) / 10u > 9u ? 9u : (c % 100u + 5u) / 10u);
        }
    } else {
        *b++ = '.';
        if (c < 10u)
            *b++ = '0';
        b = put_u(b, c);
    }
    put_s(b, unit);
}

void pb_param_text(int m, int v, char *b)
{
    switch (m) {
    case M_CLK: b = put_s(b, "/"); put_u(b, (uint32_t)clk_div(v)); break;
    case M_SEQ: b = put_u(b, (uint32_t)seq_len(v)); put_s(b, " steps"); break;
    case M_RAND: put_f(b, rand_hz(v), " Hz"); break;
    case M_QUANT: b = put_u(b, (uint32_t)quant_range(v)); put_s(b, " notes"); break;
    case M_LFO1: put_f(b, lfo1_hz(v), " Hz"); break;
    case M_LFO2: put_f(b, lfo2_hz(v), " Hz"); break;
    case M_ENV1: case M_ENV2: put_f(b, env_dec(v), " s"); break;
    case M_VCO: put_s(b, v < 15 ? "Sine" : v < 40 ? "Round" : v < 60 ? "Triangle" : v < 85 ? "Bright" : "Saw"); break;
    case M_FM: b = put_s(b, "x"); put_f(b, RATIOS[v * 9 / 100], ""); break;
    case M_NOISE: if (v < 3) put_s(b, "Off"); else put_f(b, dust_hz(v), " /s"); break;
    case M_VCF: {
        float hz = vcf_hz(v);
        if (hz >= 1000.0f)
            put_f(b, hz * 0.001f, " kHz");
        else
            put_s(put_u(b, (uint32_t)(hz + 0.5f)), " Hz");
        break;
    }
    default: put_s(put_u(b, (uint32_t)v), "%"); break;
    }
}

/* ------------------------------------------------------------ the patch --- */
int pb_fits(int in, int out)
{
    int ti, to;
    if (in < 0 || in >= PB_NIN || out < 0 || out >= PB_NOUT)
        return 0;
    ti = PB_JACK[in].type;
    to = PB_JACK[PB_NIN + out].type;
    return ti == T_AUDIO ? to == T_AUDIO : to != T_AUDIO;
}

int pb_ncables(const int8_t *cab)
{
    int i, n = 0;
    for (i = 0; i < PB_NIN; i++)
        n += cab[i] >= 0;
    return n;
}

/* the three processors that pass audio along (VCF, VCA, DELAY): a circle among them is a loop */
static int proc_in(int o)                        /* the processor output o feeds the input of, as its audio: -1 none */
{
    return o == O_VCF ? I_VCF_IN : o == O_VCA ? I_VCA_IN : o == O_DLY ? I_DLY_IN : -1;
}
int pb_loops(const int8_t *cab)
{
    static const uint8_t P[3] = {O_VCF, O_VCA, O_DLY};
    int s, k;
    for (s = 0; s < 3; s++) {                     /* walk back from each processor's input, three steps at most */
        int o = P[s];
        for (k = 0; k < 3; k++) {
            int i = proc_in(o);
            if (i < 0 || cab[i] < 0)
                break;
            o = cab[i];
            if (o == P[s])
                return 1;
        }
    }
    return 0;
}

int pb_legal(const int8_t *cab)
{
    int i;
    for (i = 0; i < PB_NIN; i++)
        if (cab[i] >= 0 && !pb_fits(i, cab[i]))
            return 0;
    return !pb_loops(cab);
}

static int alive_d(const int8_t *cab, const uint8_t *par, int out, int d);
static int trig_ok(const int8_t *cab, const uint8_t *par, int in, int d)   /* a gate comes into in */
{
    int o = cab[in];
    if (o < 0)
        return 0;
    if (PB_JACK[PB_NIN + o].type == T_CV && o != O_LFO1_SIN && o != O_LFO2_SIN && o != O_RAND_SH && o != O_RAND_SM &&
        o != O_ENV1 && o != O_ENV2)
        return 0;                                 /* a pitch CV may never cross the middle */
    return alive_d(cab, par, o, d + 1);
}
static int alive_d(const int8_t *cab, const uint8_t *par, int out, int d)
{
    if (d > 6)
        return 0;
    switch (out) {
    case O_SEQ_CV: case O_SEQ_GATE: return trig_ok(cab, par, I_SEQ_CLK, d);
    case O_QUANT_NOTE: case O_QUANT_CHG: return cab[I_QUANT_IN] >= 0 && alive_d(cab, par, cab[I_QUANT_IN], d + 1);
    case O_ENV1: return trig_ok(cab, par, I_ENV1_TRIG, d);
    case O_ENV2: return trig_ok(cab, par, I_ENV2_TRIG, d);
    case O_DUST: return par[M_NOISE] >= 3;
    default: return 1;                            /* the clock, the LFOs, RAND (its own clock), audio */
    }
}
int pb_alive(const int8_t *cab, const uint8_t *par, int out) { return alive_d(cab, par, out, 0); }

static int heard_d(const int8_t *cab, int out, int d, int gated_only, const uint8_t *par)
{
    int i;
    if (d > 4)
        return 0;
    for (i = 0; i < PB_NIN; i++) {
        if (cab[i] != out)
            continue;
        if (i == I_MIX_A || i == I_MIX_B || i == I_MIX_C || i == I_VERB_IN)
            return 1;
        if (i == I_VCF_IN && heard_d(cab, O_VCF, d + 1, gated_only, par))
            return 1;
        if (i == I_VCA_IN) {                      /* a VCA its CV opens and closes: not a raw path */
            if (gated_only && cab[I_VCA_CV] >= 0 && alive_d(cab, par, cab[I_VCA_CV], 0))
                continue;
            if (heard_d(cab, O_VCA, d + 1, gated_only, par))
                return 1;
        }
        if (i == I_DLY_IN && heard_d(cab, O_DLY, d + 1, gated_only, par))
            return 1;
    }
    return 0;
}
int pb_heard(const int8_t *cab, int out) { return heard_d(cab, out, 0, 0, 0); }

/* The auto-patcher's rules: legal; 5 to 15 cables; an oscillator heard whose pitch moves; the noise heard only
 * through a VCA that opens and closes; a VCA whose CV is patched gets a CV that moves (never a silent ENV) */
int pb_musical(const int8_t *cab, const uint8_t *par)
{
    int n = pb_ncables(cab), tune = 0;
    if (!pb_legal(cab) || n < 5 || n > 15)
        return 0;
    if (pb_heard(cab, O_VCO) && cab[I_VCO_PITCH] >= 0 && pb_alive(cab, par, cab[I_VCO_PITCH]))
        tune = 1;
    if (pb_heard(cab, O_FM) && cab[I_FM_PITCH] >= 0 && pb_alive(cab, par, cab[I_FM_PITCH]))
        tune = 1;
    if (!tune)
        return 0;
    if (heard_d(cab, O_NOISE, 0, 1, par))
        return 0;
    if (pb_heard(cab, O_VCA) && cab[I_VCA_CV] >= 0 && !pb_alive(cab, par, cab[I_VCA_CV]))
        return 0;
    if (cab[I_VCA_CV] >= 0 && (cab[I_VCA_CV] == O_SEQ_CV || cab[I_VCA_CV] == O_QUANT_NOTE))
        return 0;                                 /* a pitch as a level: the low notes would vanish */
    return 1;
}

/* ------------------------------------------------------------ commands --- */
enum { C_CONNECT, C_PARAM, C_GLOBAL, C_SEQ, C_PLAY, C_PANIC, C_SEED, C_RESTART };
#define QN 128u
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
void pb_connect(int in, int out) { post(C_CONNECT, in, out + 1); }
void pb_param(int m, int v) { post(C_PARAM, m, v); }
void pb_global(int g, int v) { post(C_GLOBAL, g, v); }
void pb_seq(int step, int v) { post(C_SEQ, step, v); }
void pb_play(int on) { post(C_PLAY, on, 0); }
void pb_panic(void) { post(C_PANIC, 0, 0); }
void pb_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
void pb_restart(void) { post(C_RESTART, 0, 0); }

/* -------------------------------------------------------------- state --- */
volatile uint8_t pb_playing;
volatile float pb_val[PB_NOUT];
volatile uint8_t pb_seq_pos;
volatile uint8_t pb_note[3];
volatile float pb_fm_index, pb_vcf_hz, pb_vca_gain, pb_level, pb_peak;
volatile uint32_t pb_eighths;
volatile uint8_t pb_harm;
volatile float pb_dly_echo[4];
volatile uint32_t pb_moves;

static uint8_t par[PB_NMOD];
static int16_t glo[PB_NG];
static int8_t seqv[8];
static int8_t cab[PB_NIN];                       /* the cable each input plays now */
static int8_t want[PB_NIN];                      /* the one it is told (a change fades the old out first) */
static float fade[PB_NIN];                       /* the input's level: 0 .. 1 */
static int nsteps = 7, steps[8];
static uint32_t rng = 0x9E3779B9u;

static uint32_t rnd(void)                        /* Marsaglia's xorshift */
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    rng = x ^ (x << 5);
    return rng;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }

#define SN 1024
static float sine_t[SN + 1];
static inline float sin_turn(float ph)          /* sin(2 pi ph), ph in turns (any value) */
{
    float x = (ph - fm_floorf(ph)) * (float)SN;
    int i = (int)x;
    float f = x - (float)i;
    return sine_t[i] + (sine_t[i + 1] - sine_t[i]) * f;
}

/* -------------------------------------------------------- the control part --- */
static float cv[PB_NOUT];                        /* the control outputs now (audio outputs: their levels) */
static float prev_in[PB_NIN];                    /* a trigger input's last value: its edges */
static float clk_s;                              /* samples into the eighth */
static uint32_t eighth;
static int seq_pos;
static float rand_sh, rand_sm, rand_tgt, rand_t, rand_own;
static float lfo_ph[2];
static int q_step = -1000;
static int q_chg, dust_left;
static float env[2];
static uint8_t env_att[2];
static int harm_deg;
static float run_gain;                           /* pause: the sound into the room fades */

/* an input's value unplugged: VCA CV open, FM's index at 1, the rest 0 */
static float in_dflt(int i) { return i == I_VCA_CV ? 1.0f : i == I_FM_INDEX ? 0.25f : 0.0f; }
static float cv_hold[PB_NIN];                    /* a CV input's value as its cable changed: it glides from there */
static float in_cv(int i)                        /* a control input's value: its cable's, or its default */
{
    float v = cab[i] >= 0 ? cv[cab[i]] : in_dflt(i);
    return fade[i] >= 1.0f ? v : v * fade[i] + cv_hold[i] * (1.0f - fade[i]);
}
static int rising(int i, float v)
{
    int r = v > 0.5f && prev_in[i] <= 0.5f;
    prev_in[i] = v;
    return r;
}

static int pspan(void) { return 3 * nsteps; }    /* a pitch CV's range: three octaves of the scale */
static float step_cv(int s) { return (float)s / (float)pspan(); }
static int cv_step(float c)
{
    int s = (int)(fm_clampf(c, 0.0f, 1.0f) * (float)pspan() + 0.5f);
    return s;
}
static int step_note(int s, int oct)             /* scale step s above the root, from octave oct: MIDI */
{
    return 12 * (oct + 1) + glo[G_ROOT] + 12 * (s / nsteps) + steps[s % nsteps];
}
static const uint8_t CHORD7[3] = {0, 2, 4}, CHORDPM[3] = {0, 2, 3}, CHORDPN[3] = {0, 1, 3};
static int chord_step(int k)                     /* the harmony's chord tone k (0 its root, 3 an octave up) */
{
    const uint8_t *c = nsteps == 7 ? CHORD7 : glo[G_SCALE] == 6 ? CHORDPM : CHORDPN;
    return harm_deg + c[k % 3] + nsteps * (k / 3);
}

static void harmony_next(void)                   /* the next chord: I, IV, V or vi of the scale */
{
    static const uint8_t D7[6] = {0, 3, 4, 5, 3, 0}, D5[4] = {0, 1, 3, 4};
    int d;
    if (glo[G_HARM] == 0) {
        harm_deg = 0;
        return;
    }
    do
        d = nsteps == 7 ? D7[rnd() % 6u] : D5[rnd() % 4u];
    while (d == harm_deg && (rnd() & 3u));
    harm_deg = d;
}

static void control(uint32_t n)                  /* once a block of n samples (n <= CB) */
{
    float len8 = PB_SR * 30.0f / (float)glo[G_TEMPO], half = 0.5f * len8, v;
    int i;
    /* CLOCK: eighth notes, and every DIV-th of them */
    if (pb_playing) {
        clk_s += (float)n;
        if (clk_s >= len8) {
            clk_s -= len8;
            if (clk_s >= len8)
                clk_s = 0.0f;
            eighth++;
            pb_eighths = eighth;
            if (glo[G_HARM] && eighth % (glo[G_HARM] == 2 ? 64u : 128u) == 0u)
                harmony_next();
        }
        cv[O_CLK_BEAT] = clk_s < half ? 1.0f : 0.0f;
        cv[O_CLK_DIV] = (clk_s < half && eighth % (uint32_t)clk_div(par[M_CLK]) == 0u) ? 1.0f : 0.0f;
    } else {
        cv[O_CLK_BEAT] = cv[O_CLK_DIV] = 0.0f;
    }
    /* LFOs */
    lfo_ph[0] += lfo1_hz(par[M_LFO1]) * (float)n * (1.0f / PB_SR);
    lfo_ph[1] += lfo2_hz(par[M_LFO2]) * (float)n * (1.0f / PB_SR);
    for (i = 0; i < 2; i++) {
        if (lfo_ph[i] >= 1.0f)
            lfo_ph[i] -= 1.0f;
        cv[O_LFO1_SIN + 2 * i] = 0.5f + 0.5f * sin_turn(lfo_ph[i]);
        cv[O_LFO1_SQR + 2 * i] = lfo_ph[i] < 0.5f ? 1.0f : 0.0f;
    }
    /* RAND: sampled on its clock (its own, at its rate, when nothing is patched); a smooth wander */
    {
        float hz = rand_hz(par[M_RAND]);
        rand_t -= (float)n * (1.0f / PB_SR);
        if (rand_t <= 0.0f) {
            rand_t += 1.0f / hz;
            rand_tgt = frand();
            if (cab[I_RAND_CLK] < 0)
                rand_own = 1.0f;
        }
        rand_sm += (rand_tgt - rand_sm) * fm_clampf(hz * 3.0f * (float)n / PB_SR, 0.0f, 1.0f);
        v = in_cv(I_RAND_CLK);
        if (rising(I_RAND_CLK, v) || rand_own > 0.0f) {
            rand_sh = 0.15f + 0.7f * frand();
            rand_own = 0.0f;
        }
        cv[O_RAND_SH] = rand_sh;
        cv[O_RAND_SM] = rand_sm;
    }
    /* NOISE's dust: a short gate now and then */
    {
        float hz = dust_hz(par[M_NOISE]);
        if (dust_left > 0)
            dust_left--;
        else if (hz > 0.0f && pb_playing && frand() < hz * (float)n / PB_SR)
            dust_left = (int)(0.02f * CR);
        cv[O_DUST] = dust_left > 0 ? 1.0f : 0.0f;
    }
    /* SEQ: a step on each clock */
    v = in_cv(I_SEQ_CLK);
    if (rising(I_SEQ_CLK, v)) {
        seq_pos = (seq_pos + 1) % seq_len(par[M_SEQ]);
        pb_seq_pos = (uint8_t)seq_pos;
    }
    if (seq_pos >= seq_len(par[M_SEQ]))
        seq_pos = 0;
    {
        int s = seqv[seq_pos];
        if (s >= 0)
            cv[O_SEQ_CV] = step_cv(s + harm_deg);
        cv[O_SEQ_GATE] = (s >= 0 && v > 0.5f) ? 1.0f : 0.0f;
    }
    /* QUANT: a CV onto the harmony's chord tones; a gate when the note changes */
    {
        int r = quant_range(par[M_QUANT]), k = cab[I_QUANT_IN] >= 0 ? (int)(fm_clampf(in_cv(I_QUANT_IN), 0.0f, 1.0f) * (float)(r - 1) + 0.5f) : 0;
        int s = chord_step(k);
        if (s != q_step) {
            if (q_step != -1000)
                q_chg = (int)(0.015f * CR) + 1;
            q_step = s;
            pb_note[0] = (uint8_t)step_note(s, 3);
        }
        cv[O_QUANT_NOTE] = step_cv(s);
        if (q_chg > 0)
            q_chg--;
        cv[O_QUANT_CHG] = q_chg > 0 ? 1.0f : 0.0f;
    }
    /* ENV 1, 2: attack, then decay; again at each trigger */
    for (i = 0; i < 2; i++) {
        int in = i ? I_ENV2_TRIG : I_ENV1_TRIG;
        float a = 0.002f * fm_exp2f((float)glo[i ? G_ATT2 : G_ATT1] * 0.1f);   /* 2 ms .. 2 s */
        v = in_cv(in);
        if (rising(in, v))
            env_att[i] = 1;
        if (env_att[i]) {
            env[i] += (float)n / (a * PB_SR);
            if (env[i] >= 1.0f) {
                env[i] = 1.0f;
                env_att[i] = 0;
            }
        } else {
            env[i] *= fm_expf(-(float)n / (env_dec(par[i ? M_ENV2 : M_ENV1]) * PB_SR) * 1.6f);
            env[i] = fm_flush(env[i]);
        }
        cv[O_ENV1 + i] = env[i];
    }
    /* a cable changing: audio fades the old one out, then the new in (12 ms each); a CV glides from where it was;
     * a pitch or a gate goes straight over */
    for (i = 0; i < PB_NIN; i++) {
        if (want[i] != cab[i]) {
            int t = PB_JACK[i].type;
            if (t == T_AUDIO) {
                fade[i] -= 0.06f;
                if (fade[i] <= 0.0f || cab[i] < 0) {
                    fade[i] = 0.0f;
                    cab[i] = want[i];
                    pb_moves++;
                }
            } else if (t == T_CV && i != I_VCO_PITCH && i != I_FM_PITCH) {
                cv_hold[i] = in_cv(i);
                cab[i] = want[i];
                fade[i] = 0.0f;
                pb_moves++;
            } else {
                cab[i] = want[i];
                fade[i] = 1.0f;
                prev_in[i] = 1.0f;               /* no edge from the swap itself */
                pb_moves++;
            }
        } else if (fade[i] < 1.0f) {
            fade[i] = fm_minf(1.0f, fade[i] + 0.06f);
        }
    }
    run_gain += ((pb_playing ? 1.0f : 0.0f) - run_gain) * 0.004f;
}

/* ---------------------------------------------------------- the sound part --- */
enum { A_VCO, A_FM, A_NOISE, A_VCF, A_VCA, A_DLY, NA };
static float abuf[NA][CB];
static float zeros[CB];
static float lvl[NA];
static int a_of(int out)                         /* an audio output's buffer */
{
    return out == O_VCO ? A_VCO : out == O_FM ? A_FM : out == O_NOISE ? A_NOISE : out == O_VCF ? A_VCF :
           out == O_VCA ? A_VCA : A_DLY;
}
static float tmp_in[CB];
static float fade_prev[PB_NIN];
static const float *ain(int i, uint32_t n)      /* an audio input over the block, its fade ramped */
{
    uint32_t j;
    float g0 = fade_prev[i], g1 = fade[i], dg;
    const float *s;
    fade_prev[i] = g1;
    if (cab[i] < 0 || (g0 <= 0.0f && g1 <= 0.0f))
        return zeros;
    s = abuf[a_of(cab[i])];
    if (g0 >= 1.0f && g1 >= 1.0f)
        return s;
    dg = (g1 - g0) / (float)n;
    for (j = 0; j < n; j++)
        tmp_in[j] = s[j] * (g0 + dg * (float)j);
    return tmp_in;
}

static float mtof(float m) { return 440.0f * fm_exp2f((m - 69.0f) * (1.0f / 12.0f)); }

/* VCO: sine, triangle and a band-limited saw, morphed by WAVE; a short glide between notes */
static float vco_ph, vco_inc, vco_lg;
static void do_vco(uint32_t n)
{
    float *o = abuf[A_VCO], w = (float)par[M_VCO] * 0.01f, ws, wt, wsw, lg, inc0, dinc;
    int note = step_note(cv_step(in_cv(I_VCO_PITCH)), glo[G_VCO_OCT]);
    uint32_t j;
    pb_note[1] = (uint8_t)note;
    lg = fm_log2f(mtof((float)note) / PB_SR);
    vco_lg = vco_lg == 0.0f ? lg : vco_lg + (lg - vco_lg) * 0.12f;
    inc0 = vco_inc;
    vco_inc = fm_exp2f(vco_lg);
    dinc = (vco_inc - inc0) / (float)n;
    ws = w < 0.5f ? 1.0f - 2.0f * w : 0.0f;
    wt = w < 0.5f ? 2.0f * w : 2.0f - 2.0f * w;
    wsw = w < 0.5f ? 0.0f : 2.0f * w - 1.0f;
    for (j = 0; j < n; j++) {
        float inc = inc0 + dinc * (float)j, p, saw, tri, t;
        vco_ph += inc;
        if (vco_ph >= 1.0f)
            vco_ph -= 1.0f;
        p = vco_ph;
        saw = 2.0f * p - 1.0f;
        if (p < inc) {                           /* polyBLEP */
            t = p / inc;
            saw -= t + t - t * t - 1.0f;
        } else if (p > 1.0f - inc) {
            t = (p - 1.0f) / inc;
            saw -= t * t + t + t + 1.0f;
        }
        tri = 1.0f - 4.0f * fm_fabsf(p - 0.5f);
        o[j] = 0.5f * (ws * sin_turn(p) + wt * tri + 0.8f * wsw * saw);
    }
}

/* FM: a carrier and its modulator at RATIO; INDEX (0..1) takes the index to 4, less as the note climbs */
static float fm_cp, fm_mp, fm_inc, fm_lg, fm_ix;
static void do_fm(uint32_t n)
{
    float *o = abuf[A_FM], r = RATIOS[par[M_FM] * 9 / 100], lg, inc0, dinc, ix0, dix, ix, f;
    int note = step_note(cv_step(in_cv(I_FM_PITCH)), glo[G_FM_OCT]);
    uint32_t j;
    pb_note[2] = (uint8_t)note;
    lg = fm_log2f(mtof((float)note) / PB_SR);
    fm_lg = fm_lg == 0.0f ? lg : fm_lg + (lg - fm_lg) * 0.25f;
    inc0 = fm_inc;
    fm_inc = fm_exp2f(fm_lg);
    dinc = (fm_inc - inc0) / (float)n;
    f = fm_inc * PB_SR;
    ix = in_cv(I_FM_INDEX) * 4.0f * fm_minf(1.0f, 900.0f / (f * fm_maxf(r, 1.0f)));
    ix0 = fm_ix;
    fm_ix += (ix - fm_ix) * 0.3f;
    pb_fm_index = fm_ix;
    dix = (fm_ix - ix0) / (float)n;
    for (j = 0; j < n; j++) {
        float inc = inc0 + dinc * (float)j;
        fm_cp += inc;
        fm_mp += inc * r;
        if (fm_cp >= 1.0f)
            fm_cp -= 1.0f;
        if (fm_mp >= 1.0f)
            fm_mp -= fm_floorf(fm_mp);
        o[j] = 0.42f * sin_turn(fm_cp + (ix0 + dix * (float)j) * 0.159155f * sin_turn(fm_mp));
    }
}

static float nz_lp, nz_hp;
static void do_noise(uint32_t n)
{
    float *o = abuf[A_NOISE];
    uint32_t j;
    for (j = 0; j < n; j++) {
        float x = (float)(int32_t)rnd() * (1.0f / 2147483648.0f);
        nz_lp += 0.45f * (x - nz_lp);            /* a little darker than white */
        nz_hp += 0.01f * (nz_lp - nz_hp);
        o[j] = 0.32f * (nz_lp - nz_hp);
    }
}

/* VCF: Zavalishin's TPT state-variable low-pass; CUT opens it up to five octaves */
static float f_ic1, f_ic2, f_hz = 500.0f;
static void do_vcf(uint32_t n)
{
    const float *x = ain(I_VCF_IN, n);
    float *o = abuf[A_VCF], hz, g, k, a1, a2, a3;
    uint32_t j;
    hz = vcf_hz(par[M_VCF]);
    hz *= fm_exp2f(5.0f * in_cv(I_VCF_CUT));
    hz = fm_clampf(hz, 30.0f, 12000.0f);
    f_hz += (hz - f_hz) * 0.35f;
    pb_vcf_hz = f_hz;
    g = fm_tanf(FM_PI * f_hz / PB_SR);
    k = 1.7f - 1.3f * (float)glo[G_RES] * 0.01f;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
    for (j = 0; j < n; j++) {
        float in = x[j], v3, v1, v2;
        in = in / (1.0f + fm_fabsf(in));         /* soft into the filter */
        v3 = in - f_ic2;
        v1 = a1 * f_ic1 + a2 * v3;
        v2 = f_ic2 + a2 * f_ic1 + a3 * v3;
        f_ic1 = fm_flush(2.0f * v1 - f_ic1);
        f_ic2 = fm_flush(2.0f * v2 - f_ic2);
        o[j] = v2 * 1.1f;
    }
}

/* VCA: LEVEL, times CV when one is patched */
static float a_g;
static void do_vca(uint32_t n)
{
    const float *x = ain(I_VCA_IN, n);
    float *o = abuf[A_VCA], t = (float)par[M_VCA] * 0.01f, g0 = a_g, dg;
    uint32_t j;
    t *= fm_clampf(in_cv(I_VCA_CV), 0.0f, 1.0f);
    a_g += (t - a_g) * 0.5f;                     /* (a gate's step softened over a millisecond) */
    pb_vca_gain = a_g;
    dg = (a_g - g0) / (float)n;
    for (j = 0; j < n; j++)
        o[j] = x[j] * (g0 + dg * (float)j);
}

/* DELAY: one line at half rate (22 kHz, 16-bit), two taps: T to the left, 2T to the right, 2T fed back, darker
 * each time: echoes that bounce from side to side. T follows the tempo (a note value per preset) */
#define DL_N 32768                                /* 1.49 s */
static int16_t dl_buf[DL_N] PB_POOL;
static uint32_t dl_w;
static float dl_t = 8000.0f, dl_acc, dl_lp, dl_a0, dl_b0, dl_a1, dl_b1;
static float dly_side[CB];
static inline float dl_read(float d)
{
    float p = (float)dl_w - d;
    int i;
    float f;
    if (p < 0.0f)
        p += (float)DL_N;
    i = (int)p;
    f = p - (float)i;
    return ((float)dl_buf[i & (DL_N - 1)] + ((float)dl_buf[(i + 1) & (DL_N - 1)] - (float)dl_buf[i & (DL_N - 1)]) * f) *
           (1.0f / 16384.0f);
}
static void do_dly(uint32_t n)
{
    const float *x = ain(I_DLY_IN, n);
    float *o = abuf[A_DLY], fb = (float)par[M_DLY] * 0.0075f, t;
    uint32_t j;
    t = DLY_Q[glo[G_DLY] % 5] * 60.0f / (float)glo[G_TEMPO] * (PB_SR * 0.5f);
    while (t > 15800.0f)
        t *= 0.5f;
    dl_t += (t - dl_t) * 0.01f;
    for (j = 0; j < n; j++) {
        float l, r;
        dl_acc += x[j];
        if (j & 1u) {                            /* every other sample: the line moves on */
            float in = dl_acc * 0.5f, w;
            dl_acc = 0.0f;
            dl_a0 = dl_a1;
            dl_b0 = dl_b1;
            dl_a1 = dl_read(dl_t);
            dl_b1 = dl_read(2.0f * dl_t);
            dl_lp += 0.5f * (dl_b1 - dl_lp);
            w = in + fb * dl_lp;
            w = w / (1.0f + 0.25f * fm_fabsf(w));
            dl_buf[dl_w & (DL_N - 1)] = (int16_t)(fm_clampf(w, -1.99f, 1.99f) * 16384.0f);
            dl_w++;
            l = dl_a1;
            r = dl_b1;
        } else {
            l = 0.5f * (dl_a0 + dl_a1);
            r = 0.5f * (dl_b0 + dl_b1);
        }
        o[j] = x[j] + 0.3f * (l + r);
        dly_side[j] = 0.3f * (l - r);
    }
    pb_dly_echo[0] = pb_dly_echo[0] * 0.9f + 0.1f * fm_fabsf(dl_a1) * 4.0f;
    pb_dly_echo[1] = pb_dly_echo[1] * 0.9f + 0.1f * fm_fabsf(dl_b1) * 4.0f;
    pb_dly_echo[2] = pb_dly_echo[1] * fb;
    pb_dly_echo[3] = pb_dly_echo[2] * fb;
}

/* ------------------------------------------------- reverb: Dattorro's plate --- */
/* J. Dattorro, "Effect Design, Part 1", JAES 1997 (as in FoMni and AMB-1): a predelay, four input diffusers, then a
 * figure-eight tank of two halves, each a modulated allpass, a delay, a damping lowpass, an allpass and a delay,
 * feeding the other half. Stereo out from seven taps a side. The paper's lengths are for 29761 Hz; DS() scales them
 * to 44.1 kHz. */
#define DS(n) (((n) * 14818 + 5000) / 10000)
#define PRE_N 882                                 /* 20 ms */
#define EXC DS(16)
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define PL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static float pl_buf[PL_TOTAL] PB_POOL;
typedef struct {
    float *b;
    int n, i;
} dl_t_;
static dl_t_ dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, pl_decay = 0.5f, pl_dd2 = 0.5f, lfo_s, lfo_c = 1.0f;

static inline float tap(const dl_t_ *d, int k)
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void push(dl_t_ *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t_ *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline float allpass_mod(dl_t_ *d, float len, float x, float g)
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
    for (i = 0; i < PL_TOTAL; i++)
        pl_buf[i] = 0.0f;
    pl_bw = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
}

/* ---------------------------------------------------- the mix and the room --- */
/* MIX: three inputs, left, right and middle, each kept in its place by its own slow level: a drone that never stops
 * sits lower than a part that comes and goes. Then the plate (the mix at SPACE, plus REVERB's input), a slow level
 * keeper (the patch changes by itself: the room stays the same loudness), a gentle top cut and a soft limiter. */
static float mx_rms[3] = {0.05f, 0.05f, 0.05f};
static float agc_env = 0.1f, agc_g = 1.0f, out_lp_l, out_lp_r, lvl_slow;
static void do_out(int32_t *out, uint32_t n, float gain)
{
    static const float PAN_L[3] = {0.85f, 0.45f, 0.67f}, PAN_R[3] = {0.45f, 0.85f, 0.67f};
    const float *mi[3], *vi;
    float mg[3], mlvl = (float)par[M_MIX] * 0.012f, space = (float)glo[G_SPACE] * 0.01f, pk = 0.0f;
    uint32_t j;
    int k, side = -1;
    for (k = 0; k < 3; k++) {
        float e = 0.0f;
        mi[k] = ain(I_MIX_A + k, n);
        for (j = 0; j < n; j += 4)
            e += mi[k][j] * mi[k][j];
        e *= 4.0f / (float)n;
        mx_rms[k] += (e - mx_rms[k]) * 0.0008f;
        mg[k] = mlvl * fm_clampf(0.11f * fm_rsqrtf(mx_rms[k] + 1e-6f), 0.35f, 1.0f) * run_gain;
        if (cab[I_MIX_A + k] == O_DLY && side < 0)
            side = k;
    }
    vi = ain(I_VERB_IN, n);
    pl_decay = 0.3f + 0.006f * (float)par[M_VERB];
    pl_dd2 = fm_clampf(pl_decay + 0.15f, 0.25f, 0.5f);
    for (j = 0; j < n; j++) {
        float l = 0.0f, r = 0.0f, wl, wr, sd;
        for (k = 0; k < 3; k++) {
            l += mi[k][j] * mg[k] * PAN_L[k];
            r += mi[k][j] * mg[k] * PAN_R[k];
        }
        if (side >= 0) {
            sd = dly_side[j] * mg[side];
            l += sd;
            r -= sd;
        }
        reverb((l + r) * 0.5f * space + vi[j] * 0.45f * run_gain, &wl, &wr);
        l += 0.9f * wl;
        r += 0.9f * wr;
        l *= agc_g;
        r *= agc_g;
        out_lp_l += 0.62f * (l - out_lp_l);      /* (about 8 kHz down a little: never sharp) */
        out_lp_r += 0.62f * (r - out_lp_r);
        l = fm_tanhf(out_lp_l * gain) * 0.8f;
        r = fm_tanhf(out_lp_r * gain) * 0.8f;
        pk = fm_maxf(pk, fm_maxf(fm_fabsf(l), fm_fabsf(r)));
        out[2 * j] = (int32_t)(l * 8300000.0f);
        out[2 * j + 1] = (int32_t)(r * 8300000.0f);
    }
    /* the level keeper: on the mix before MASTER, a quick fall, a slow rise */
    {
        float lv = pk / fm_maxf(gain, 0.05f) / fm_maxf(agc_g, 0.05f), tg;
        if (lv > agc_env)
            agc_env += (lv - agc_env) * 0.25f;
        else
            agc_env += (lv - agc_env) * 0.0004f;
        tg = fm_clampf(0.32f / fm_maxf(agc_env, 0.02f), 0.4f, 2.5f);
        agc_g += (tg - agc_g) * (tg < agc_g ? 0.02f : 0.0012f);
    }
    if (pk > pb_peak)
        pb_peak = pk;
    lvl_slow += (pk - lvl_slow) * 0.01f;
    pb_level = lvl_slow;
}

/* the audio processors' order: each after what feeds it (no circles: pb_legal) */
static void process(uint32_t n)
{
    int done[3] = {0, 0, 0}, pass, k;
    static const uint8_t PO[3] = {O_VCF, O_VCA, O_DLY}, PI[3] = {I_VCF_IN, I_VCA_IN, I_DLY_IN};
    do_noise(n);
    do_vco(n);
    do_fm(n);
    for (pass = 0; pass < 3; pass++)
        for (k = 0; k < 3; k++) {
            int s = cab[PI[k]], ready = 1, m;
            if (done[k])
                continue;
            for (m = 0; m < 3; m++)
                if (!done[m] && m != k && s == PO[m])
                    ready = 0;
            if (!ready && pass < 2)
                continue;
            if (k == 0)
                do_vcf(n);
            else if (k == 1)
                do_vca(n);
            else
                do_dly(n);
            done[k] = 1;
        }
    for (k = 0; k < NA; k++) {                    /* the audio outputs' levels, for the panels */
        float e = 0.0f;
        uint32_t j;
        for (j = 0; j < n; j += 2)
            e = fm_maxf(e, fm_fabsf(abuf[k][j]));
        lvl[k] = e > lvl[k] ? e : lvl[k] * 0.985f;
    }
    cv[O_VCO] = lvl[A_VCO];
    cv[O_FM] = lvl[A_FM];
    cv[O_NOISE] = lvl[A_NOISE];
    cv[O_VCF] = lvl[A_VCF];
    cv[O_VCA] = lvl[A_VCA];
    cv[O_DLY] = lvl[A_DLY];
}

/* --------------------------------------------------------- the render --- */
static void reset_all(void)
{
    int i;
    seq_pos = 0;
    pb_seq_pos = 0;
    harm_deg = 0;
    pb_harm = 0;
    clk_s = 0.0f;
    for (i = 0; i < 2; i++)
        env[i] = 0.0f, env_att[i] = 0;
    q_step = -1000;
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t m = q[q_r % QN], c = m >> 24, a = (m >> 16) & 0xFFu;
        int b = (int)(int16_t)(m & 0xFFFFu);
        BARRIER();
        q_r++;
        switch (c) {
        case C_CONNECT:
            if (a < PB_NIN)
                want[a] = (int8_t)(b - 1 >= 0 && pb_fits((int)a, b - 1) ? b - 1 : -1);
            break;
        case C_PARAM:
            if (a < PB_NMOD)
                par[a] = (uint8_t)(b < 0 ? 0 : b > 100 ? 100 : b);
            break;
        case C_GLOBAL:
            if (a < PB_NG) {
                glo[a] = (int16_t)b;
                if (a == G_SCALE)
                    nsteps = pb_scale(b, steps);
                if (a == G_TEMPO && glo[G_TEMPO] < 20)
                    glo[G_TEMPO] = 20;
            }
            break;
        case C_SEQ:
            if (a < 8u)
                seqv[a] = (int8_t)(b >= 0 && b <= 30 ? b : -1);
            break;
        case C_PLAY: pb_playing = (uint8_t)(a != 0); break;
        case C_PANIC:
            run_gain = 0.0f;
            env[0] = env[1] = 0.0f;
            break;
        case C_SEED: rng ^= (uint32_t)b * 2654435761u; if (!rng) rng = 1u; break;
        case C_RESTART: reset_all(); break;
        default: break;
        }
    }
}

void pb_init(void)
{
    int i;
    for (i = 0; i <= SN; i++)
        sine_t[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SN));
    for (i = 0; i < PB_NIN; i++)
        cab[i] = want[i] = -1;
    for (i = 0; i < DL_N; i++)
        dl_buf[i] = 0;
    plate_init();
    glo[G_TEMPO] = 96;
    glo[G_SPACE] = 40;
    glo[G_VCO_OCT] = 2;
    glo[G_FM_OCT] = 3;
    nsteps = pb_scale(0, steps);
    for (i = 0; i < PB_NMOD; i++)
        par[i] = 50;
    reset_all();
}

void pb_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 1.6f;
    drain();
    while (n) {
        uint32_t k = n > CB ? CB : n;
        uint32_t i;
        control(k);
        process(k);
        do_out(out, k, gain);
        for (i = 0; i < PB_NOUT; i++)
            pb_val[i] = cv[i];
        pb_harm = (uint8_t)harm_deg;
        out += 2 * k;
        n -= k;
    }
}

/* ------------------------------------------------------------ presets --- */
/* g: tempo, space, root, scale, VCO octave, FM octave, ENV 1 attack, ENV 2 attack, resonance, echo (1/8, 3/16, 1/4,
 * 3/8, 1/2), harmony (0 none, 1 every 16 bars, 2 every 8); auto: the auto-patcher's rate (ui.c AUTO_S);
 * par: CLOCK DIV, SEQ LEN, RAND RATE, QUANT RANGE, LFO 1, LFO 2, ENV 1, ENV 2, VCO WAVE, FM RATIO, NOISE DUST, VCF,
 * VCA, DELAY, MIX, REVERB */
#define END {0xFF, 0}
const pb_preset_t PB_PRESET[PB_NPRESETS] = {
    {"Berlin School", {112, 35, 9, 5, 1, 3, 0, 20, 60, 1, 1}, 6,
     {17, 100, 40, 60, 30, 18, 30, 45, 92, 34, 0, 30, 85, 55, 80, 45}, {0, 7, 2, 7, 4, 7, 3, 9},
     {{I_SEQ_CLK, O_CLK_BEAT}, {I_VCO_PITCH, O_SEQ_CV}, {I_ENV1_TRIG, O_SEQ_GATE}, {I_VCF_IN, O_VCO},
      {I_VCF_CUT, O_LFO2_SIN}, {I_VCA_IN, O_VCF}, {I_VCA_CV, O_ENV1}, {I_DLY_IN, O_VCA}, {I_MIX_A, O_DLY},
      {I_ENV2_TRIG, O_CLK_DIV}, {I_RAND_CLK, O_CLK_DIV}, {I_QUANT_IN, O_RAND_SH}, {I_FM_PITCH, O_QUANT_NOTE},
      {I_FM_INDEX, O_ENV2}, {I_MIX_B, O_FM}, END}},
    {"Bleeps", {120, 40, 0, 6, 3, 4, 0, 0, 30, 1, 2}, 5,
     {0, 50, 60, 80, 55, 25, 12, 25, 20, 34, 0, 45, 90, 60, 80, 40}, {0, 2, 4, 5, 7, 4, 2, 9},
     {{I_RAND_CLK, O_CLK_BEAT}, {I_QUANT_IN, O_RAND_SH}, {I_FM_PITCH, O_QUANT_NOTE}, {I_ENV1_TRIG, O_QUANT_CHG},
      {I_VCA_CV, O_ENV1}, {I_VCA_IN, O_FM}, {I_DLY_IN, O_VCA}, {I_MIX_A, O_DLY}, {I_FM_INDEX, O_LFO1_SIN},
      {I_SEQ_CLK, O_CLK_DIV}, {I_VCO_PITCH, O_SEQ_CV}, {I_ENV2_TRIG, O_SEQ_GATE}, {I_VCF_CUT, O_ENV2},
      {I_VCF_IN, O_VCO}, {I_MIX_B, O_VCF}, END}},
    {"West Coast", {84, 45, 2, 1, 2, 3, 5, 30, 20, 2, 1}, 6,
     {34, 67, 30, 40, 30, 15, 40, 50, 0, 25, 0, 30, 90, 45, 80, 55}, {0, 4, 2, 7, -1, 5, 3, -1},
     {{I_SEQ_CLK, O_CLK_BEAT}, {I_FM_PITCH, O_SEQ_CV}, {I_ENV1_TRIG, O_SEQ_GATE}, {I_FM_INDEX, O_ENV1},
      {I_VCF_IN, O_FM}, {I_VCF_CUT, O_ENV1}, {I_VCA_IN, O_VCF}, {I_VCA_CV, O_ENV1}, {I_MIX_A, O_VCA},
      {I_DLY_IN, O_VCA}, {I_MIX_B, O_DLY}, {I_QUANT_IN, O_LFO2_SIN}, {I_VCO_PITCH, O_QUANT_NOTE},
      {I_VERB_IN, O_VCO}, END}},
    {"Drone Cathedral", {50, 80, 4, 5, 2, 3, 80, 90, 40, 4, 1}, 7,
     {100, 50, 10, 30, 8, 10, 70, 92, 55, 12, 0, 35, 75, 60, 75, 90}, {0, 4, 2, 5, 0, 3, 1, 4},
     {{I_QUANT_IN, O_LFO2_SIN}, {I_VCO_PITCH, O_QUANT_NOTE}, {I_VCF_IN, O_VCO}, {I_VCF_CUT, O_LFO1_SIN},
      {I_MIX_A, O_VCF}, {I_ENV2_TRIG, O_CLK_DIV}, {I_VCA_CV, O_ENV2}, {I_FM_INDEX, O_RAND_SM},
      {I_FM_PITCH, O_QUANT_NOTE}, {I_VCA_IN, O_FM}, {I_DLY_IN, O_VCA}, {I_MIX_B, O_DLY}, {I_VERB_IN, O_VCF}, END}},
    {"Random Garden", {90, 55, 5, 3, 3, 4, 10, 0, 45, 2, 2}, 5,
     {34, 84, 45, 70, 40, 20, 45, 35, 30, 56, 35, 30, 85, 55, 80, 60}, {7, 9, 11, 8, 10, 12, 9, 14},
     {{I_QUANT_IN, O_RAND_SM}, {I_VCO_PITCH, O_QUANT_NOTE}, {I_ENV1_TRIG, O_QUANT_CHG}, {I_VCA_CV, O_ENV1},
      {I_VCA_IN, O_VCO}, {I_DLY_IN, O_VCA}, {I_MIX_A, O_DLY}, {I_ENV2_TRIG, O_DUST}, {I_FM_INDEX, O_ENV2},
      {I_SEQ_CLK, O_DUST}, {I_FM_PITCH, O_SEQ_CV}, {I_VCF_IN, O_FM}, {I_VCF_CUT, O_ENV2}, {I_MIX_B, O_VCF}, END}},
    {"Krell", {66, 75, 10, 2, 3, 3, 40, 60, 60, 3, 0}, 7,
     {50, 50, 30, 50, 48, 30, 55, 70, 70, 30, 0, 30, 90, 60, 80, 80}, {0, 3, 1, 5, 2, 6, 4, 7},
     {{I_ENV1_TRIG, O_LFO1_SQR}, {I_RAND_CLK, O_LFO1_SQR}, {I_VCO_PITCH, O_RAND_SH}, {I_VCF_CUT, O_ENV1},
      {I_VCF_IN, O_VCO}, {I_VCA_IN, O_VCF}, {I_VCA_CV, O_ENV1}, {I_DLY_IN, O_VCA}, {I_MIX_A, O_DLY},
      {I_FM_PITCH, O_RAND_SM}, {I_FM_INDEX, O_LFO2_SIN}, {I_VERB_IN, O_FM}, END}},
    {"Plinky", {132, 35, 7, 0, 3, 4, 0, 0, 25, 0, 2}, 5,
     {34, 84, 50, 60, 35, 20, 14, 22, 40, 67, 0, 22, 90, 45, 80, 35}, {7, 9, 11, 14, 11, 9, 12, -1},
     {{I_SEQ_CLK, O_CLK_BEAT}, {I_FM_PITCH, O_SEQ_CV}, {I_ENV1_TRIG, O_SEQ_GATE}, {I_VCA_CV, O_ENV1},
      {I_VCA_IN, O_FM}, {I_DLY_IN, O_VCA}, {I_MIX_A, O_DLY}, {I_FM_INDEX, O_ENV1}, {I_ENV2_TRIG, O_CLK_DIV},
      {I_QUANT_IN, O_LFO2_SIN}, {I_VCO_PITCH, O_QUANT_NOTE}, {I_VCF_IN, O_VCO}, {I_VCF_CUT, O_ENV2},
      {I_MIX_B, O_VCF}, END}},
    {"Tape Choir", {60, 70, 3, 0, 3, 3, 75, 70, 20, 3, 1}, 7,
     {50, 34, 20, 35, 15, 12, 75, 70, 50, 12, 0, 40, 85, 65, 80, 75}, {0, 2, 4, 3, 5, 4, 2, 1},
     {{I_SEQ_CLK, O_CLK_DIV}, {I_VCO_PITCH, O_SEQ_CV}, {I_ENV1_TRIG, O_SEQ_GATE}, {I_VCA_CV, O_ENV1},
      {I_VCF_IN, O_VCO}, {I_VCF_CUT, O_LFO1_SIN}, {I_VCA_IN, O_VCF}, {I_DLY_IN, O_VCA}, {I_MIX_A, O_DLY},
      {I_ENV2_TRIG, O_CLK_DIV}, {I_FM_INDEX, O_ENV2}, {I_QUANT_IN, O_LFO2_SIN}, {I_FM_PITCH, O_QUANT_NOTE},
      {I_VERB_IN, O_FM}, END}},
    {"Night Rain", {70, 70, 6, 7, 2, 5, 0, 0, 70, 2, 1}, 6,
     {50, 50, 40, 60, 40, 15, 30, 18, 20, 45, 40, 20, 85, 55, 80, 70}, {0, 2, 4, 1, 3, 5, 2, 6},
     {{I_VCF_IN, O_NOISE}, {I_ENV2_TRIG, O_DUST}, {I_VCF_CUT, O_ENV2}, {I_VCA_IN, O_VCF}, {I_VCA_CV, O_LFO2_SIN},
      {I_MIX_A, O_VCA}, {I_RAND_CLK, O_DUST}, {I_QUANT_IN, O_RAND_SH}, {I_FM_PITCH, O_QUANT_NOTE},
      {I_ENV1_TRIG, O_QUANT_CHG}, {I_FM_INDEX, O_ENV1}, {I_DLY_IN, O_FM}, {I_MIX_B, O_DLY}, END}},
    {"Sine Tides", {56, 65, 7, 6, 3, 4, 50, 50, 10, 3, 1}, 8,
     {50, 50, 20, 50, 20, 12, 50, 50, 0, 34, 0, 45, 85, 50, 80, 65}, {0, 2, 4, 6, 4, 2, 0, 1},
     {{I_QUANT_IN, O_LFO2_SIN}, {I_VCO_PITCH, O_QUANT_NOTE}, {I_VCA_IN, O_VCO}, {I_VCA_CV, O_LFO1_SIN},
      {I_DLY_IN, O_VCA}, {I_MIX_A, O_DLY}, {I_FM_PITCH, O_RAND_SM}, {I_FM_INDEX, O_LFO1_SIN}, {I_VCF_IN, O_FM},
      {I_VCF_CUT, O_LFO2_SIN}, {I_MIX_B, O_VCF}, END}},
    {"Clockwork", {126, 30, 0, 1, 1, 4, 0, 0, 35, 1, 2}, 5,
     {34, 100, 40, 50, 50, 20, 0, 20, 85, 45, 0, 25, 70, 50, 80, 40}, {0, 0, 4, 0, 2, 0, 5, 3},
     {{I_ENV1_TRIG, O_CLK_BEAT}, {I_VCA_CV, O_ENV1}, {I_VCA_IN, O_NOISE}, {I_MIX_C, O_VCA}, {I_SEQ_CLK, O_CLK_DIV},
      {I_VCO_PITCH, O_SEQ_CV}, {I_ENV2_TRIG, O_SEQ_GATE}, {I_VCF_CUT, O_ENV2}, {I_VCF_IN, O_VCO},
      {I_DLY_IN, O_VCF}, {I_MIX_B, O_DLY}, {I_RAND_CLK, O_CLK_DIV}, {I_QUANT_IN, O_RAND_SH},
      {I_FM_PITCH, O_QUANT_NOTE}, {I_FM_INDEX, O_LFO1_SQR}, END}},
    {"Polymeter", {104, 35, 9, 4, 2, 4, 0, 0, 55, 2, 2}, 6,
     {84, 50, 40, 60, 45, 20, 25, 35, 75, 56, 0, 20, 90, 50, 80, 45}, {0, 4, 7, 2, 5, -1, 3, 6},
     {{I_SEQ_CLK, O_CLK_BEAT}, {I_VCO_PITCH, O_SEQ_CV}, {I_ENV1_TRIG, O_SEQ_GATE}, {I_VCF_CUT, O_ENV1},
      {I_VCF_IN, O_VCO}, {I_MIX_A, O_VCF}, {I_ENV2_TRIG, O_CLK_DIV}, {I_RAND_CLK, O_CLK_DIV},
      {I_QUANT_IN, O_RAND_SH}, {I_FM_PITCH, O_QUANT_NOTE}, {I_VCA_IN, O_FM}, {I_VCA_CV, O_ENV2},
      {I_DLY_IN, O_VCA}, {I_MIX_B, O_DLY}, {I_FM_INDEX, O_LFO1_SQR}, END}},
};

const uint8_t PB_SAFE[PB_NMOD][2] = {
    {0, 84}, {34, 100}, {10, 70}, {20, 80}, {10, 70}, {5, 60}, {5, 70}, {10, 85},
    {0, 85}, {0, 78}, {0, 50}, {12, 60}, {60, 95}, {20, 70}, {65, 90}, {30, 90},
};

/* ------------------------------------------------------ the auto-patcher --- */
/* One move: a cable's output end moves to another output (SRC), its input end to another input (DST), a new cable
 * (ADD) or one taken out (DEL). Candidates are drawn at random, the kinds weighted to keep 8 to 12 cables, and the
 * first whose patch passes pb_musical is the move. Gates go to gate inputs and CVs to CV inputs; a CV as a gate
 * (or a gate as a CV) only now and then. */
static uint32_t mv_rnd(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x2545F491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
static int mv_pick_ok(int in, int out, uint32_t *s)
{
    if (!pb_fits(in, out))
        return 0;
    if (PB_JACK[in].type != T_AUDIO && PB_JACK[in].type != PB_JACK[PB_NIN + out].type)
        return mv_rnd(s) % 3u == 0u;
    return 1;
}
int pb_auto_move(const int8_t *cab, const uint8_t *par, uint32_t *seed, const pb_move_t *last, pb_move_t *mv)
{
    int8_t t[PB_NIN];
    int tries, n = pb_ncables(cab), i;
    for (tries = 0; tries < 200; tries++) {
        uint32_t r = mv_rnd(seed) % 100u;
        int kind, in, out;
        int w_add = n < 8 ? 35 : n < 11 ? 15 : 4, w_del = n > 12 ? 35 : n > 9 ? 15 : 4;
        kind = r < (uint32_t)w_add ? MV_ADD : r < (uint32_t)(w_add + w_del) ? MV_DEL :
               r < (uint32_t)(w_add + w_del + (100 - w_add - w_del) * 6 / 10) ? MV_SRC : MV_DST;
        for (i = 0; i < PB_NIN; i++)
            t[i] = cab[i];
        mv->kind = (uint8_t)kind;
        if (kind == MV_ADD) {
            in = (int)(mv_rnd(seed) % PB_NIN);
            out = (int)(mv_rnd(seed) % PB_NOUT);
            if (cab[in] >= 0 || !mv_pick_ok(in, out, seed))
                continue;
            t[in] = (int8_t)out;
            mv->in = (int8_t)in;
            mv->in_old = -1;
            mv->out = (int8_t)out;
            mv->out_old = -1;
        } else {
            in = (int)(mv_rnd(seed) % PB_NIN);
            if (cab[in] < 0)
                continue;
            mv->in_old = (int8_t)in;
            mv->out_old = cab[in];
            if (kind == MV_DEL) {
                t[in] = -1;
                mv->in = -1;
                mv->out = cab[in];
            } else if (kind == MV_SRC) {
                out = (int)(mv_rnd(seed) % PB_NOUT);
                if (out == cab[in] || !mv_pick_ok(in, out, seed))
                    continue;
                t[in] = (int8_t)out;
                mv->in = (int8_t)in;
                mv->out = (int8_t)out;
            } else {
                int to = (int)(mv_rnd(seed) % PB_NIN);
                if (to == in || cab[to] >= 0 || !mv_pick_ok(to, cab[in], seed))
                    continue;
                t[to] = cab[in];
                t[in] = -1;
                mv->in = (int8_t)to;
                mv->out = cab[in];
            }
        }
        if (last && last->kind == MV_SRC && mv->kind == MV_SRC && mv->in == last->in && mv->out == last->out_old)
            continue;                             /* (not straight back) */
        if (last && mv->kind == MV_ADD && last->kind == MV_DEL && mv->in == last->in_old && mv->out == last->out)
            continue;
        if (!pb_musical(t, par))
            continue;
        return 1;
    }
    return 0;
}

void pb_apply_move(int8_t *cab, const pb_move_t *mv)
{
    if (mv->in_old >= 0 && mv->kind != MV_SRC)
        cab[mv->in_old] = -1;
    if (mv->in >= 0)
        cab[mv->in] = mv->out;
}
