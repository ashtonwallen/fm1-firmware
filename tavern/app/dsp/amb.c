/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Tavern session: ashtonwallen, 2026 */
/* The instrument (amb.h). One compilation unit, -O2. */
#include "amb.h"
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
const char *const EN_INST_NAME[EN_NINST] = {"fiddle", "whistle", "voice", "harp", "lute", "box", "bass", "bodhran"};
const char *const EN_KIND_NAME[EN_NKIND] = {"reel", "jig", "waltz", "air"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Lead", 0, 100, 70},  {"Backing", 0, 100, 65}, {"Bodhran", 0, 100, 55},
    {"Echo", 0, 100, 15},  {"Reverb", 0, 100, 55},  {"Room", 0, 100, 55},   {"Tone", 0, 100, 50},
    {"Tune", -50, 50, 0},  {"MIDI out", 0, 1, 1},
    {"Tempo", 30, 140, 72}, {"Root", 0, 11, 2},     {"Mode", 0, EN_NMODES - 1, 1},
    {"Texture", 0, 3, 0},   {"Air", 0, 100, 0},     {"Sway", 0, 100, 50},  {"Shimmer", 0, 100, 0},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

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

void en_param_text(int i, int v, char *b)
{
    const char *s = 0;
    if (i == P_MIDI)
        s = v ? "On" : "Off";
    else if (i == P_TEXTURE) {
        static const char *const TEX[4] = {"Rain", "Wind", "Sea", "Fire"};
        s = TEX[(unsigned)v < 4u ? v : 0];
    } else if (i == P_MODE)
        s = EN_MODE_NAME[(unsigned)v < EN_NMODES ? v : 0];
    else if (i == P_ROOT)
        s = EN_NOTE_NAME[(unsigned)v < 12u ? v : 0];
    if (s) {
        int k = 0;
        while (s[k] && k < 9) {
            b[k] = s[k];
            k++;
        }
        b[k] = 0;
        return;
    }
    if (i == P_TUNE && v > 0)
        *b++ = '+';
    itoa_s(v, b);
}

/* ------------------------------------------------------- the pure parts --- */
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
    int n = from + ((pc - from) % 12 + 12) % 12;          /* at or above */
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
        t[v] = pcs[v < nt ? v : 0];                        /* three tones: the root doubled */
    for (p = 0; p < 24; p++) {
        int c = 0, n[4], a, b;
        for (v = 0; v < 4; v++) {
            n[v] = nearest(from[v], t[PERM[p][v]], lo, hi);
            c += n[v] > from[v] ? n[v] - from[v] : from[v] - n[v];
        }
        for (a = 0; a < 4; a++)                            /* no two voices on one note */
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

/* ------------------------------------------------------------- the tunes --- */
/* A tune as a session would know it: two parts of eight bars, each in two four-bar phrases, the second phrase
 * the first again but for its last bar (first ending: open, back to the top; second ending: home). Chords from
 * a few progressions a mode, the melody a walk that lands on the chord's tones on the beats, in the rhythms of
 * its kind. Pure, from the seed: the same seed and kind make the same tune (a regular's favourite comes back). */
static uint32_t tg;
static uint32_t tg_rnd(void)
{
    uint32_t x = tg;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return tg = x;
}
static int tg_irand(int n) { return n > 0 ? (int)((tg_rnd() >> 8) % (uint32_t)n) : 0; }

/* four-bar progressions (scale degrees): Ionian, Dorian, Mixolydian, Aeolian; the last bar open */
static const int8_t PROG_A[4][4][4] = {
    {{0, 3, 0, 4}, {0, 0, 3, 4}, {0, 4, 0, 4}, {0, 3, 4, 4}},
    {{0, 6, 0, 6}, {0, 0, 6, 6}, {0, 3, 0, 6}, {0, 6, 3, 6}},
    {{0, 6, 0, 4}, {0, 0, 6, 6}, {0, 3, 0, 6}, {0, 6, 3, 4}},
    {{0, 6, 5, 6}, {0, 3, 0, 6}, {0, 5, 6, 6}, {0, 6, 0, 4}},
};
static const int8_t PROG_B[4][3][4] = {
    {{3, 0, 3, 4}, {0, 4, 3, 4}, {4, 0, 4, 4}},
    {{6, 6, 0, 6}, {3, 0, 3, 6}, {6, 0, 3, 6}},
    {{6, 0, 6, 4}, {3, 0, 6, 6}, {0, 6, 3, 6}},
    {{5, 6, 0, 6}, {3, 0, 5, 6}, {6, 6, 0, 4}},
};
/* rhythms in eighths, a pattern a bar */
static const char *const RHY_REEL[] = {"11111111", "11111111", "11111111", "2111111", "1111211", "211211", "1121111"};
static const char *const RHY_JIG[] = {"111111", "111111", "111111", "21111", "2121", "11121"};
static const char *const RHY_WALTZ[] = {"222", "2112", "42", "312", "2211", "1122"};
static const char *const RHY_AIR[] = {"2222", "3122", "422", "224", "62", "314", "44", "2231"};
static const char *const RHY_HOME[EN_NKIND] = {"116", "114", "24", "26"};       /* the second ending */
static const char *const RHY_OPEN[EN_NKIND] = {"1111112", "1113", "222", "224"}; /* the first */

static int prog_family(int mode)
{
    return mode == MODE_DORIAN ? 1 : mode == MODE_MIXOLYDIAN ? 2 : (mode == MODE_AEOLIAN || mode == MODE_PHRYGIAN) ? 3 : 0;
}

static int chord_tone(int d, int c) { return ((d - c) % 7 + 7) % 7 % 2 == 0 && ((d - c) % 7 + 7) % 7 != 6; }

/* the next note: on a beat, a tone of the chord near the last; between, a step (mostly on the way it was going) */
static int tg_next(int cur, int c, int strong, int lo, int hi, int *dir)
{
    int n, k;
    if (strong) {
        int best[8], nb = 0;
        for (k = -3; k <= 3; k++)
            if (cur + k >= lo && cur + k <= hi && chord_tone(cur + k, c) && nb < 8)
                best[nb++] = cur + k;
        if (nb) {
            int a = best[tg_irand(nb)], b = best[tg_irand(nb)];   /* the nearer of two picks */
            n = (a - cur) * (a - cur) <= (b - cur) * (b - cur) ? a : b;
            *dir = n > cur ? 1 : n < cur ? -1 : *dir;
            return n;
        }
    }
    k = tg_irand(100);
    n = k < 55 ? cur + *dir : k < 75 ? cur - *dir : k < 88 ? cur + 2 * *dir : k < 95 ? cur : cur - 2 * *dir;
    if (n < lo || n > hi) {
        n = 2 * cur - n;
        *dir = -*dir;
    } else if (n != cur) {
        *dir = n > cur ? 1 : -1;
    }
    if (tg_irand(100) < 12)
        *dir = -*dir;                            /* the contour turns now and then */
    return n < lo ? lo : n > hi ? hi : n;
}

static int pick_rhy(int kind)
{
    switch (kind) {
    case K_REEL: return tg_irand(7);
    case K_JIG: return tg_irand(6);
    case K_WALTZ: return tg_irand(6);
    default: return tg_irand(8);
    }
}
static const char *rhy(int kind, int i)
{
    return kind == K_REEL ? RHY_REEL[i] : kind == K_JIG ? RHY_JIG[i] : kind == K_WALTZ ? RHY_WALTZ[i] : RHY_AIR[i];
}

/* a bar of melody into m: the rhythm's onsets, each a note (the last one forced to `last` if >= -50) */
static int tg_bar(int8_t *m, const char *r, int cur, int c, int beat, int lo, int hi, int *dir, int last)
{
    int e = 0, i, k;
    for (i = 0; r[i]; i++) {
        int d = r[i] - '0';
        if (!r[i + 1] && last > -50)
            cur = last;
        else
            cur = tg_next(cur, c, e % beat == 0, lo, hi, dir);
        m[e] = (int8_t)cur;
        for (k = 1; k < d; k++)
            m[e + k] = EN_TIE;
        e += d;
    }
    return cur;
}

void en_tune_make(uint16_t seed, int kind, int mode, en_tune_t *t)
{
    int fam = prog_family(mode), p, b, e, dir = 1, cur;
    kind = (unsigned)kind < EN_NKIND ? kind : 0;
    tg = (uint32_t)seed * 2654435761u + 0x9E3779B9u * (uint32_t)(kind + 1);
    if (!tg)
        tg = 1u;
    tg_rnd();
    t->kind = (uint8_t)kind;
    t->steps = kind == K_JIG || kind == K_WALTZ ? 12 : 16;
    t->eighths = (uint8_t)(t->steps / 2);
    t->beat = kind == K_JIG ? 3 : 2;
    for (p = 0; p < 2; p++) {
        const int8_t *pr = p ? PROG_B[fam][tg_irand(3)] : PROG_A[fam][tg_irand(4)];
        int lo = p ? 2 : -3, hi = p ? 11 : (kind == K_AIR ? 9 : 8), home = p ? 7 : 0, open, cc;
        static const int8_t START[2][4] = {{0, 4, -3, 2}, {7, 4, 9, 7}};
        for (b = 0; b < 9; b++)
            for (e = 0; e < 8; e++)
                t->mel[p][b][e] = EN_REST;
        for (b = 0; b < 4; b++)
            t->chord[p][b] = t->chord[p][b + 4] = pr[b];
        t->chord[p][8] = 0;
        cur = START[p][tg_irand(4)];
        dir = tg_irand(2) ? 1 : -1;
        for (b = 0; b < 4; b++) {
            if (b == 2 && tg_irand(100) < 40) {       /* the phrase's third bar answers its first */
                for (e = 0; e < 8; e++)
                    t->mel[p][2][e] = t->mel[p][0][e];
                continue;
            }
            cur = tg_bar(t->mel[p][b], rhy(kind, pick_rhy(kind)), cur, pr[b], t->beat, lo, hi, &dir, -100);
        }
        for (b = 4; b < 7; b++)                   /* the second phrase: the first again */
            for (e = 0; e < 8; e++)
                t->mel[p][b][e] = t->mel[p][b - 4][e];
        if (tg_irand(100) < 50) {                 /* with its third bar's end a little changed */
            int h = t->eighths / 2, last = t->mel[p][6][h - 1];
            for (e = h - 1; e >= 0 && last == EN_TIE; e--)
                last = t->mel[p][6][e];
            if (t->mel[p][6][h] != EN_TIE) {
                int8_t tmp[8];
                for (e = 0; e < 8; e++)
                    tmp[e] = EN_REST;
                tg_bar(tmp, rhy(kind, 0), last > -50 ? last : home, pr[2], t->beat, lo, hi, &dir, -100);
                for (e = h; e < t->eighths; e++)
                    t->mel[p][6][e] = tmp[e];
            }
        }
        cur = home + 2;                           /* the first ending: open, on a tone of its chord */
        cc = pr[3];
        open = cc;
        while (open < lo + 1)
            open += 7;
        while (open > hi - 1)
            open -= 7;
        if (chord_tone(open + 2, cc) && open + 2 <= hi && tg_irand(2))
            open += 2;
        tg_bar(t->mel[p][7], RHY_OPEN[kind], cur, cc, t->beat, lo, hi, &dir, open);
        {                                         /* the second: home, from just above */
            const char *r = RHY_HOME[kind];
            int8_t *m = t->mel[p][8];
            int i, k, at = 0, n = 0;
            for (i = 0; r[i]; i++)
                n++;
            for (i = 0; r[i]; i++) {
                int d = r[i] - '0';
                m[at] = (int8_t)(home + (n - 1 - i));
                for (k = 1; k < d; k++)
                    m[at + k] = EN_TIE;
                at += d;
            }
            if (kind == K_AIR)                    /* an air breathes before it goes round */
                m[t->eighths - 1] = EN_REST;
        }
    }
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_PLAY, C_SEED, C_PANIC, C_CLOCK, C_TUNE, C_END, C_INST, C_TWANG };
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
void en_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_clock(int msg) { post(C_CLOCK, msg, 0); }
void en_tune(uint16_t seed, int kind, int passes, int harp)
{
    passes = passes < 1 ? 1 : passes > 4 ? 4 : passes;
    post(C_TUNE, (kind & 3) | (passes - 1) << 2 | (harp ? 16 : 0), (int)seed);
}
void en_end(void) { post(C_END, 0, 0); }
void en_inst(int inst, int level, int skill)
{
    level = level < 0 ? 0 : level > 100 ? 100 : level;
    skill = skill < 0 ? 0 : skill > 100 ? 100 : skill;
    post(C_INST, inst, level | skill << 8);
}
void en_twang(int inst) { post(C_TWANG, inst, 0); }

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_ext;
volatile uint8_t en_sess, en_part, en_rep, en_bar, en_pass, en_passes, en_kind;
volatile uint32_t en_tunes_done;
volatile uint8_t en_live;
volatile float en_glow[EN_NINST];
volatile uint8_t en_chord_root, en_chord[EN_NPAD], en_mel;
volatile uint32_t en_bars;
volatile float en_level;

static int16_t par[P_NPARAMS];
static uint32_t pool = 0x6ADu;                  /* Dorian until told */
static int nsteps = 7, steps[12] = {0, 2, 3, 5, 7, 9, 10};
static int chord_pcs[4], chord_nt;
static float tunefac = 1.0f;
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

static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (par[P_MIDI])
        en_midi_out(st, d1, d2);
}

static float mtof(float n) { return 440.0f * tunefac * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }

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

static void svf_coefs(float *c, float f, float k)
{
    float g = fm_tanf(FM_PI * fm_clampf(f, 40.0f, 16000.0f) * (1.0f / EN_SR));
    c[0] = 1.0f / (1.0f + g * (g + k));
    c[1] = g * c[0];
    c[2] = g * c[1];
    c[3] = k;
}

/* ------------------------------------------------- slow random motion --- */
#define NMOD 9                                  /* 0..5 the reeds', 6..8 the echo's */
static struct {
    float v, tgt, rate;
    uint32_t left;                              /* samples until a new target */
} mod[NMOD];

static void mods_step(uint32_t n)
{
    int i;
    for (i = 0; i < NMOD; i++) {
        if (mod[i].left <= n) {
            float secs = 7.0f + 33.0f * frand();
            mod[i].tgt = frand() * 2.0f - 1.0f;
            mod[i].left = (uint32_t)(secs * EN_SR);
            mod[i].rate = (float)n / (secs * EN_SR * 0.5f);
        } else {
            mod[i].left -= n;
        }
        mod[i].v += (mod[i].tgt - mod[i].v) * fm_minf(mod[i].rate, 1.0f);
    }
}
static const float motion = 0.3f;               /* AMB-1's Motion, fixed: the echo's heads wander a little */
static const uint8_t en_frozen = 0;

/* -------------------------------------------------------------- the band --- */
static uint8_t lvl_req[EN_NINST], lvl[EN_NINST], skill[EN_NINST];   /* asked for; playing (from the bar) */
static float ivol[EN_NINST];                     /* each one's gain: its level, times the mix's */
static float step_len, pulse_left;               /* a sixteenth, in samples; to the next MIDI clock pulse */
static uint32_t noise = 22222u;

static float nz(void)                            /* white noise, -1..1 */
{
    noise = noise * 1664525u + 1013904223u;
    return (float)(int32_t)noise * (1.0f / 2147483648.0f);
}
static int32_t slop(int i)                       /* a human hand: later by up to ~15 ms, less with skill */
{
    return irand(40 + (100 - skill[i]) * 6);
}

/* the melody's voices: fiddle (a bowed saw, darkened, vibrato that grows on a held note), whistle (a sine and
 * breath), voice (a saw through two formants: a singer's "ah" and "oh") */
#define NLEAD 3
typedef struct {
    float ph, inc, tinc, amp, gate, dip, dipt, vib, hold, lp1, lp2, s[4], c1[4], c2[4], cut_inc;
    int32_t wait, cut, dipn;
    float pend_inc;
    int8_t pend;                                 /* waiting: 1 a note, 0 a release, -1 nothing */
    int8_t pend_cut;
    uint8_t vowel;
} lead_t;
static lead_t lead[NLEAD];

/* the plucks: harp and lute (and a string's twang), two-operator FM with a quick index fall */
#define NPLUCK 10
typedef struct {
    float cph, mph, cinc, minc, amp, adec, idx, idec, att, gl, gr, bend;
    int32_t wait;
    uint8_t on, note, inst;
} pluck_t;
static pluck_t pk[NPLUCK];

/* the box: four reeds, each two saws a few cents apart (the musette) through two formants */
typedef struct {
    float ph[2], inc[2], note, target, amp, ic[4], c1[4], c2[4], pan, hf;
    uint8_t midi_note, hand;
} reed_t;
static reed_t reed[EN_NPAD];
static float box_gate, box_gain;
static const float REED_PAN[EN_NPAD] = {-0.35f, -0.1f, 0.15f, 0.4f};

/* the bass: a plucked string, its overtones dying first */
static struct {
    float ph, inc, amp, br, dec, bdec, att;
    int32_t wait;
    float pinc, pvel;
    uint8_t midi_note;
} bass;

/* the bodhran: a low boom and a higher tap, each a falling sine and a breath of skin */
typedef struct {
    float ph, inc, inc_end, amp, adec, att, nlp, pend;
    int32_t wait;
    uint8_t on;
} drum_t;
static drum_t drum[2];

/* ------------------------------------------------------------ the voices --- */
static const float LEAD_PAN[NLEAD] = {-0.25f, 0.3f, 0.0f};
static const float VOWELS[2][2] = {{730.0f, 1090.0f}, {500.0f, 850.0f}};   /* "ah", "oh" */

static void lead_note(int i, int n, int cut, int32_t wait)   /* n < 0: release */
{
    lead_t *v = &lead[i];
    v->pend = n >= 0 ? 1 : 0;
    if (n >= 0) {
        v->pend_inc = mtof((float)n) * (1.0f / EN_SR);
        v->pend_cut = (int8_t)cut;
        v->cut_inc = mtof((float)(n + (n % 12 == 4 || n % 12 == 11 ? 1 : 2))) * (1.0f / EN_SR);
    }
    v->wait = wait > 0 ? wait : 1;
}

static void lead_apply(lead_t *v, int i)
{
    if (v->pend == 1) {
        if (v->amp > 0.05f) {                    /* a new bow, a new breath: a little dip */
            v->dipt = i == I_VOICE ? 0.75f : 0.45f;
            v->dipn = i == I_FIDDLE ? 700 : 400;
        }
        v->tinc = v->pend_inc;
        if (v->amp < 0.02f)
            v->inc = v->tinc;
        v->cut = v->pend_cut ? (int32_t)(0.028f * EN_SR) : 0;
        v->gate = 1.0f;
        v->hold = 0.0f;
        v->vowel ^= (uint8_t)(irand(3) == 0);
        if (i == I_VOICE) {
            svf_coefs(v->c1, VOWELS[v->vowel][0], 0.4f);
            svf_coefs(v->c2, VOWELS[v->vowel][1], 0.35f);
        }
    } else if (v->pend == 0) {
        v->gate = 0.0f;
    }
    v->pend = -1;
}

static pluck_t *pluck(int inst, int n, float vel, int32_t wait, float pan)
{
    int i, best = 0;
    float lo = 9.0f, t60, p, ix;
    pluck_t *v;
    for (i = 0; i < NPLUCK; i++) {               /* a free voice, or the quietest */
        float a = pk[i].on ? pk[i].amp : -1.0f;
        if (a < lo) {
            lo = a;
            best = i;
        }
    }
    while (n > 96)
        n -= 12;
    while (n < 36)
        n += 12;
    v = &pk[best];
    if (v->on)
        midi(0x81, v->note, 0);
    v->on = 1;
    v->note = (uint8_t)n;
    v->inst = (uint8_t)inst;
    v->cph = v->mph = 0.0f;
    v->cinc = mtof((float)n) * (1.0f / EN_SR);
    v->bend = 1.0f;
    if (inst == I_LUTE) {                        /* gut strings: shorter, a little more bite */
        v->minc = v->cinc * 2.0f;
        t60 = 1.3f - (float)(n - 50) * 0.012f;
        ix = 0.9f;
        v->idec = fm_expf(-6.9f / (0.12f * EN_SR));
    } else {                                     /* the harp: round, long */
        v->minc = v->cinc;
        t60 = 3.2f - (float)(n - 60) * 0.035f;
        ix = 0.55f;
        v->idec = fm_expf(-6.9f / (0.25f * EN_SR));
    }
    t60 = fm_clampf(t60, 0.6f, 4.5f);
    v->adec = fm_expf(-6.9f / (t60 * EN_SR));
    v->amp = vel;
    v->idx = ix * (0.4f + 0.6f * vel) * (0.6f + 0.008f * (float)par[P_TONE]) * (1.0f / FM_TWO_PI);
    v->att = 0.0f;
    p = (fm_clampf(pan, -1.0f, 1.0f) + 1.0f) * (FM_PI / 4.0f);
    v->gl = fm_cosf(p);
    v->gr = fm_sinf(p);
    v->wait = wait;
    en_glow[inst] = 1.0f;
    midi(0x91, (uint32_t)n, (uint32_t)(vel * 110.0f));
    return v;
}

static void bass_pluck(int n, float vel, int32_t wait)
{
    bass.pinc = mtof((float)n) * (1.0f / EN_SR);
    bass.pvel = vel;
    bass.wait = wait > 0 ? wait : 1;
    if (bass.midi_note)
        midi(0x82, bass.midi_note, 0);
    bass.midi_note = (uint8_t)n;
    midi(0x92, (uint32_t)n, (uint32_t)(vel * 100.0f));
    en_glow[I_BASS] = 1.0f;
}

static void drum_fire(int d, float vel)
{
    drum_t *v = &drum[d];
    float f = d ? 165.0f : 92.0f;
    v->on = 1;
    v->amp = vel;
    v->att = 0.0f;
    v->inc = f * 1.35f * (1.0f / EN_SR);
    v->inc_end = f * (d ? 0.9f : 0.68f) * (1.0f / EN_SR);
    v->adec = fm_expf(-6.9f / ((d ? 0.14f : 0.42f) * EN_SR));
    en_glow[I_DRUM] = 1.0f;
    midi(0x99, d ? 37u : 41u, (uint32_t)(vel * 100.0f));
    midi(0x89, d ? 37u : 41u, 0);
}

static void drum_hit(int d, float vel, int32_t wait)
{
    if (wait > 0) {
        drum[d].wait = wait;
        drum[d].pend = vel;
    } else {
        drum_fire(d, vel);
    }
}

static int32_t drums_wait(void)                 /* samples to the next hit that waits; 0: none */
{
    int32_t m = 0;
    int d;
    for (d = 0; d < 2; d++)
        if (drum[d].wait > 0 && (!m || drum[d].wait < m))
            m = drum[d].wait;
    return m;
}

static void drums_wait_step(int32_t k)          /* k samples went by: hits whose wait is over */
{
    int d;
    for (d = 0; d < 2; d++)
        if (drum[d].wait > 0 && (drum[d].wait -= k) <= 0) {
            drum[d].wait = 0;
            drum_fire(d, drum[d].pend);
        }
}

/* ------------------------------------------------------------ the session --- */
static en_tune_t tune;
static int tonic = 62, sb, harp_leads, end_bars, mel_note = -1;
static uint8_t box_midi[EN_NPAD];

static int deg_midi(int d)                      /* a scale degree from the tonic: its note */
{
    int o = d >= 0 ? d / nsteps : -((-d + nsteps - 1) / nsteps), s = d - o * nsteps;
    return tonic + 12 * o + steps[s];
}

static int in_range(int pc, int lo)             /* pitch class pc in lo..lo+11 */
{
    return lo + ((pc - lo) % 12 + 12) % 12;
}

static void chord_set(int dg)                   /* the bar's chord: the box's reeds lead to it */
{
    int from[4], to[4], i;
    chord_nt = en_chord_tones(pool, par[P_ROOT], dg, chord_pcs);
    if (chord_nt > 3)
        chord_nt = 3;                            /* triads: a session's chords */
    for (i = 0; i < EN_NPAD; i++)
        from[i] = (int)(reed[i].target + 0.5f);
    en_voice_lead(from, chord_pcs, chord_nt, to, 53, 72);
    for (i = 0; i < EN_NPAD; i++) {
        reed_t *v = &reed[i];
        if ((float)to[i] != v->target) {
            v->target = (float)to[i];
            if (v->amp > 0.01f && !v->hand)
                v->hand = 1;                     /* out of the old chord, quickly, into the new */
            else if (!v->hand)
                v->note = v->target;
        }
        en_chord[i] = (uint8_t)to[i];
        if (en_playing && lvl[I_BOX] && box_midi[i] != to[i]) {
            if (box_midi[i])
                midi(0x83, box_midi[i], 0);
            midi(0x93, (uint32_t)to[i], 60);
            box_midi[i] = (uint8_t)to[i];
        }
    }
    en_chord_root = (uint8_t)chord_pcs[0];
}

/* the backing's patterns, an eighth a slot: the chord's tone (0 root, 1 third, 2 fifth, 3 the octave; +8 low;
 * 9 a strum; -1 nothing) */
static const int8_t HARP_PAT[EN_NKIND][8] = {{8, -1, 1, -1, 2, -1, 3, -1}, {8, -1, 2, 1, -1, 2, -1, -1},
                                              {8, -1, 1, 2, 3, -1, -1, -1}, {8, -1, 2, -1, 3, -1, 1, -1}};
static const int8_t LUTE_PAT[EN_NKIND][8] = {{8, -1, 9, -1, 10, -1, 9, -1}, {8, -1, -1, 9, -1, -1, -1, -1},
                                              {8, -1, 9, -1, 9, -1, -1, -1}, {9, -1, -1, -1, -1, -1, -1, -1}};
/* the bodhran, a sixteenth a slot: 3 an accented boom, 2 a boom, 1 a tap */
static const uint8_t DRUM_PAT[3][16] = {{3, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 1},
                                        {3, 0, 1, 0, 1, 0, 2, 0, 1, 0, 1, 0},
                                        {3, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}};

static int tone_note(int t, int lo)             /* a chord tone (HARP_PAT's) in a register */
{
    int k = t & 7, pc = chord_pcs[k == 3 ? 0 : k < chord_nt ? k : 0], n = in_range(pc, lo);
    if (k == 3)
        n += 12;
    return n;
}

static void strum(int inst, int lo, float vel, int spread)
{
    int k;
    for (k = 0; k < chord_nt; k++) {
        int n = in_range(chord_pcs[k], lo);
        pluck(inst, n, vel * (k ? 0.8f : 1.0f), slop(inst) + k * spread, 0.45f);
    }
}

static void melody(int e, int v)                /* an eighth of the tune: to the melody's players */
{
    int i, n = v > -50 ? deg_midi(v) : -1, held = 0;
    int32_t lilt = 0;
    if (v == EN_TIE)
        return;
    if (tune.kind == K_REEL || tune.kind == K_AIR)  /* the lilt: the off eighths a little late */
        lilt = (e & 1) ? (int32_t)(step_len * 0.22f) : 0;
    else if (tune.kind == K_JIG)
        lilt = e % 3 == 1 ? (int32_t)(step_len * 0.25f) : e % 3 == 2 ? (int32_t)(step_len * 0.1f) : 0;
    if (n >= 0) {
        const int8_t *bar = tune.mel[en_part][en_bar == 7 && en_rep ? 8 : en_bar];
        held = e + 1 < tune.eighths && bar[e + 1] == EN_TIE;
    }
    if (mel_note >= 0)
        midi(0x80, (uint32_t)mel_note, 0);
    mel_note = n;
    en_mel = (uint8_t)(n >= 0 ? n : 0);
    if (n >= 0)
        midi(0x90, (uint32_t)n, 90);
    for (i = 0; i < NLEAD; i++) {
        int m = n;
        if (!lvl[i])
            continue;
        if (n >= 0 && i == I_WHISTLE && tonic <= 62)
            m = n + 12;                          /* the whistle an octave up, while that stays sweet */
        if (n >= 0)
            en_glow[i] = 1.0f;
        lead_note(i, m, n >= 0 && held && (i != I_VOICE) && irand(100) < skill[i] / 3, slop(i) + lilt);
    }
    if (harp_leads && lvl[I_HARP] && n >= 0)
        pluck(I_HARP, n + 12 <= 84 ? n + 12 : n, 0.75f, slop(I_HARP) + lilt, 0.1f);
}

static void backing(int e)                      /* an eighth: harp, lute, bass */
{
    int k = tune.kind, t, b, fifth;
    if (lvl[I_HARP] && !harp_leads && (t = HARP_PAT[k][e]) >= 0)
        pluck(I_HARP, tone_note(t, t & 8 ? 45 : 57), t & 8 ? 0.7f : 0.5f, slop(I_HARP), -0.45f);
    else if (lvl[I_HARP] && harp_leads && e == 0)
        pluck(I_HARP, tone_note(0, 43), 0.55f, slop(I_HARP), -0.3f);
    if (lvl[I_LUTE] && (t = LUTE_PAT[k][e]) >= 0) {
        if (t == 9)
            strum(I_LUTE, 55, 0.42f, k == K_AIR ? 2600 : 500);
        else
            pluck(I_LUTE, tone_note(t == 10 ? 2 : 0, 43), 0.6f, slop(I_LUTE), 0.5f);
    }
    b = in_range(chord_pcs[0], 36);
    fifth = b + 7 > 50 ? b - 5 : b + 7;
    if (lvl[I_BASS]) {
        if (e == 0)
            bass_pluck(b, 0.9f, slop(I_BASS));
        else if ((k == K_REEL && e == 4) || (k == K_JIG && e == 3))
            bass_pluck(irand(3) ? fifth : b, 0.7f, slop(I_BASS));
    }
}

static void drums(int s)                        /* a sixteenth: the bodhran */
{
    int k = tune.kind, h;
    float v;
    if (!lvl[I_DRUM] || k == K_AIR)
        return;
    h = DRUM_PAT[k == K_REEL ? 0 : k == K_JIG ? 1 : 2][s];
    if (!h || (h == 1 && irand(100) < 25 - skill[I_DRUM] / 5))
        return;                                  /* a tap or two let go */
    v = (h == 3 ? 0.9f : h == 2 ? 0.65f : 0.32f) * (0.85f + 0.3f * frand());
    drum_hit(h == 1 ? 1 : 0, v, slop(I_DRUM) + (s & 2 ? (int32_t)(step_len * 0.15f) : 0));
}

static void vols(void)                          /* each one's gain: its level, times the mix's */
{
    float l = (float)par[P_LEAD] * 0.01f, b = (float)par[P_BACK] * 0.01f, d = (float)par[P_DRUM] * 0.01f;
    int i;
    for (i = 0; i < EN_NINST; i++)
        ivol[i] = (float)lvl[i] * 0.01f * (i < NLEAD ? l * l : i == I_DRUM ? d * d : b * b);
}

static void bar_start(void)                     /* a new bar: who plays, the chord */
{
    int i;
    uint8_t live = 0;
    for (i = 0; i < EN_NINST; i++) {
        lvl[i] = lvl_req[i];
        live |= (uint8_t)((lvl[i] != 0) << i);
    }
    en_live = live;
    vols();
    for (i = 0; i < NLEAD; i++)
        if (!lvl[i])
            lead[i].gate = 0.0f;
    if (en_sess == 2)
        chord_set(0);
    else
        chord_set(tune.chord[en_part][en_bar == 7 && en_rep ? 8 : en_bar]);
}

static void sess_end(void)                      /* the last pass done: home, held, from the next bar */
{
    en_sess = 2;
    end_bars = 0;
}

static void final_notes(void)
{
    int i;
    bar_start();
    for (i = 0; i < NLEAD; i++)
        if (lvl[i])
            lead_note(i, i == I_WHISTLE && tonic <= 62 ? tonic + 12 : tonic, 0, slop(i));
    if (lvl[I_HARP])
        strum(I_HARP, 50, 0.6f, 2200);
    if (lvl[I_LUTE])
        strum(I_LUTE, 55, 0.5f, 1800);
    if (lvl[I_BASS])
        bass_pluck(in_range(chord_pcs[0], 36), 0.9f, 0);
    if (lvl[I_DRUM])
        drum_hit(0, 0.8f, 0);
    en_mel = (uint8_t)tonic;
}

static void sess_tick(void)                     /* a sixteenth of the session */
{
    if (en_sess == 0)
        return;
    if (en_sess == 2) {                          /* the last note: a bar and a half, then the room */
        if (sb == 0 && end_bars == 0)
            final_notes();
        if (sb == 0 && ++end_bars >= 3) {
            int i;
            en_sess = 0;
            for (i = 0; i < NLEAD; i++)
                lead[i].gate = 0.0f;
            if (mel_note >= 0)
                midi(0x80, (uint32_t)mel_note, 0);
            mel_note = -1;
            en_mel = 0;
            en_live = 0;
            en_tunes_done++;
            return;
        }
        if (sb == tune.steps / 2 && end_bars == 2) {
            int i;
            for (i = 0; i < NLEAD; i++)
                lead[i].gate = 0.0f;             /* let go, together */
        }
    } else {
        const int8_t *bar = tune.mel[en_part][en_bar == 7 && en_rep ? 8 : en_bar];
        if (sb == 0)
            bar_start();
        if (!(sb & 1)) {
            melody(sb >> 1, bar[sb >> 1]);
            backing(sb >> 1);
        }
        drums(sb);
    }
    if (++sb >= tune.steps) {
        sb = 0;
        if (en_sess == 1 && ++en_bar >= 8) {
            en_bar = 0;
            if (++en_rep >= 2) {
                en_rep = 0;
                if (++en_part >= 2) {
                    en_part = 0;
                    if (++en_pass >= en_passes) {
                        en_pass = (uint8_t)(en_passes - 1);
                        en_part = 1;
                        en_rep = 1;
                        en_bar = 7;
                        sess_end();
                    }
                }
            }
        }
    }
}

static void sess_start(uint16_t seed, int kind, int passes, int harp)
{
    en_tune_make(seed, kind, par[P_MODE], &tune);
    harp_leads = harp;
    en_kind = (uint8_t)kind;
    en_passes = (uint8_t)passes;
    en_pass = en_part = en_rep = en_bar = 0;
    sb = 0;
    en_sess = 1;
}

static void twang(int inst)                     /* a string gives: a bright note falling away, the player out */
{
    lvl_req[inst] = lvl[inst] = 0;
    en_live &= (uint8_t)~(1u << inst);
    if (inst < NLEAD)
        lead[inst].gate = 0.0f;
    {
        pluck_t *v = pluck(I_LUTE, 81 + irand(6), 0.8f, 0, inst == I_HARP ? -0.4f : 0.3f);
        v->bend = 0.99998f;                      /* sagging as it goes */
        v->adec = fm_expf(-6.9f / (0.5f * EN_SR));
        v->idx *= 3.0f;
    }
}

/* ------------------------------------------------------------------ space --- */
static float echo_send, rev_send, echo_len, echo_len_t, echo_fb = 0.55f, tone = 0.5f;
static float rev_in_gain = 1.0f;

/* ------------------------------------------------- reverb: Dattorro's plate --- */
/* J. Dattorro, "Effect Design, Part 1", JAES 1997 (as in FoMni): a predelay, four input diffusers,
 * then a figure-eight tank of two halves, each a modulated allpass, a delay, a damping lowpass, an
 * allpass and a delay, feeding the other half. Stereo out from seven taps a side. The paper's lengths
 * are for 29761 Hz; DS() scales them to 44.1 kHz. */
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
static float pl_bw, pl_damp_l, pl_damp_r, pl_decay = 0.5f, pl_decay_t = 0.5f, pl_dd2 = 0.5f, lfo_s, lfo_c = 1.0f;

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
    pl_damp_l += 0.45f * (b - pl_damp_l);
    b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * pl_decay, pl_dd2);
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * lfo_c, x + pl_decay * fb_r, -0.7f);
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], DS(4217));
    pl_damp_r += 0.45f * (b - pl_damp_r);
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

/* ------------------------------------------------- echo: tape and Echoplex --- */
/* After Discreet Music's chain: an Echoplex (a short, smearing echo, ~160 ms, fed back) and a tape loop
 * (long: up to ~5.6 s, synced to the tempo), each repeat a little darker and thinner, with a little
 * wow. The tape runs at a quarter rate (11 kHz, 16-bit, mono): it loses its highs anyway. Two heads:
 * the first plays the loop's length, the second a part of it (3/4 at first). With Motion the echo
 * evolves: each head wanders across the field, the feedback leans now on one head, now on the other
 * (the repeats fall in changing patterns, left and right), and now and then the second head slides to
 * another part of the loop, as a head moved by hand (the repeats bend a little in pitch on the way).
 * The Echoplex has a second tap, for the right. */
#define TAPE_N 62000                             /* 5.6 s at 11.025 kHz */
#define TAPE_SR 11025.0f
static int16_t tape[TAPE_N] EN_POOL;
static int tape_w, tape_ph;
static float tape_acc, tape_lp, tape_hp, tape_p1, tape_p2, tape_c1, tape_c2;
static const float HEAD2[5] = {0.75f, 0.6667f, 0.625f, 0.5f, 0.375f};   /* the second head's parts */
static float head2 = 0.75f, head2_t = 0.75f, head2_v;   /* its place (of the loop), target, speed */
static float fb_lean;                            /* 0..1: how much of the feedback is the second head's */
static float hg[4] = {1.0f, 0.0f, 0.0f, 1.0f};   /* the heads' pan gains: 1 left, 1 right, 2 left, 2 right */
static uint32_t head_left;                       /* samples until the second head may move */
#define PLEX_N 7100                              /* 160 ms at 44.1 kHz */
#define PLEX_TAP 2700                            /* the right's tap: ~100 ms */
static int16_t plex[PLEX_N];
static int plex_w;
static float plex_lp, plex_lp2;

static inline float tape_read(float d)           /* d quarter-rate samples ago */
{
    int k = (int)d, i0, i1;
    float f = d - (float)k, a, c;
    i0 = tape_w - k;
    i1 = i0 - 1;
    if (i0 < 0)
        i0 += TAPE_N;
    if (i1 < 0)
        i1 += TAPE_N;
    a = (float)tape[i0];
    c = (float)tape[i1];
    return (a + (c - a) * f) * (1.0f / 12000.0f);
}
static inline int16_t s16(float x)
{
    x *= 12000.0f;
    return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
}



/* once a chunk: where the heads sit in the field, how the feedback leans, the second head's slide */
static void heads_step(uint32_t n)
{
    float p, want, vmax;
    int i;
    for (i = 0; i < 2; i++) {
        p = fm_clampf((i ? 0.7f : -0.7f) + 0.9f * motion * mod[6 + i].v, -1.0f, 1.0f);
        p = (p + 1.0f) * (FM_PI / 4.0f);
        hg[2 * i] = fm_cosf(p);
        hg[2 * i + 1] = fm_sinf(p);
    }
    fb_lean = 0.25f * motion * (1.0f + mod[8].v);
    if (head_left <= n) {
        head_left = (uint32_t)((20.0f + 40.0f * frand()) * EN_SR);
        if (en_playing && !en_frozen && frand() < motion)
            head2_t = HEAD2[(int)(frand() * 4.999f)];
    } else {
        head_left -= n;
    }
    /* its speed (a part of the loop per tape sample): at most a bend of ~14 cents, easing in and out */
    vmax = 0.008f / fm_maxf(echo_len, 100.0f);
    want = fm_clampf((head2_t - head2) * (1.0f / (2.0f * TAPE_SR)), -vmax, vmax);
    head2_v += (want - head2_v) * fm_minf((float)n * (1.0f / EN_SR), 1.0f);
}

/* one sample of both: the input (a mono send) to the tape's heads (el, er) and the Echoplex (xl, xr) */
static inline void echoes(float in, float *el, float *er, float *xl, float *xr)
{
    float y = (float)plex[plex_w] * (1.0f / 12000.0f);         /* the Echoplex: 160 ms, fed back, darker */
    int t = plex_w - PLEX_TAP;
    plex_lp += 0.35f * (y - plex_lp);
    plex_lp2 += 0.35f * ((float)plex[t < 0 ? t + PLEX_N : t] * (1.0f / 12000.0f) - plex_lp2);
    plex[plex_w] = s16(in + 0.55f * plex_lp);
    if (++plex_w >= PLEX_N)
        plex_w = 0;
    *xl = plex_lp;
    *xr = plex_lp2;
    tape_acc += in + 0.3f * plex_lp;
    if (++tape_ph >= 4) {                        /* the tape, a quarter of the time */
        float d, h1, h2, fb;
        tape_ph = 0;
        echo_len += (echo_len_t - echo_len) * 0.001f;
        head2 += head2_v;
        d = echo_len * (1.0f + 0.003f * motion * mod[5].v);   /* wow */
        if (d > (float)(TAPE_N - 4))
            d = (float)(TAPE_N - 4);
        h1 = tape_read(d);
        h2 = tape_read(d * head2);
        tape_lp += 0.55f * (h1 + fb_lean * (h2 - h1) - tape_lp);   /* each time round: darker, thinner */
        tape_hp += 0.03f * (tape_lp - tape_hp);
        fb = tape_lp - tape_hp;
        if (++tape_w >= TAPE_N)
            tape_w = 0;
        tape[tape_w] = s16(tape_acc * 0.25f + echo_fb * fb);
        tape_acc = 0.0f;
        tape_p1 = tape_c1;
        tape_p2 = tape_c2;
        tape_c1 = h1;
        tape_c2 = h2;
    }
    {   /* between the tape's samples: a line from the last to the next; then to the field */
        float f = (float)tape_ph * 0.25f;
        float h1 = tape_p1 + (tape_c1 - tape_p1) * f, h2 = tape_p2 + (tape_c2 - tape_p2) * f;
        *el = hg[0] * h1 + hg[2] * h2;
        *er = hg[1] * h1 + hg[3] * h2;
    }
}

/* the echo: a dotted quarter at the tune's tempo (the tape loop, kept short: a room's slap, not a canyon) */
static void echo_time(void)
{
    float t;
    if (par[P_TEMPO] <= 0)
        return;                                  /* not yet (en_init applies in order) */
    t = 1.5f * 60.0f / (float)par[P_TEMPO];
    if (t * TAPE_SR > (float)(TAPE_N - 8))
        t = (float)(TAPE_N - 8) / TAPE_SR;
    echo_len_t = t * TAPE_SR;
    if (echo_len <= 0.0f)
        echo_len = echo_len_t;
}

/* ------------------------------------------------------------ parameters --- */
static int pulse;
static uint32_t ext_age, sixteenth;

static void tempo_set(void) { step_len = EN_SR * 60.0f / ((float)par[P_TEMPO] * 4.0f); }

static void notes_off(void)
{
    int i;
    for (i = 0; i < 4; i++)
        en_midi_out(0xB0 | (uint32_t)i, 123, 0);
    en_midi_out(0xB9, 123, 0);
    for (i = 0; i < EN_NPAD; i++)
        box_midi[i] = 0;
    bass.midi_note = 0;
    mel_note = -1;
}

/* ---------------------------------------------- AMB-1 Drift 1.1's air and shimmer --- */
/* Ported from AMB-1 Drift 1.1 (the strings and the lead are not: the band has its own). Their own random numbers;
 * nothing at all runs while they are at 0. Vinyl's crackle is the fire here. */
static uint32_t rng2 = 0x2545F491u;
static uint32_t rnd2(void)
{
    uint32_t x = rng2;
    x = x ^ (x << 13);
    x = x ^ (x >> 17);
    rng2 = x ^ (x << 5);
    return rng2;
}
static float frand2(void) { return (float)(rnd2() >> 8) * (1.0f / 16777216.0f); }

/* AIR: rain, wind, sea or vinyl, from noise. Two noises (left, right) through one state-variable filter each
 * (its band-pass and low-pass mixed by type), a slow sway (Sway: how far), and for rain and vinyl, drops: an
 * impulse now and then into a ringing band-pass (a drop's "plip", a record's tick).
 *   rain   a soft hiss around 3 kHz and drops (1.8 .. 5 kHz), thicker and thinner
 *   wind   a band of noise (300 Hz .. 1.4 kHz) that gusts: louder and higher together
 *   sea    low-passed noise in waves (about 9 and 10 s, left and right): it swells, brightens, falls back
 *   vinyl  a faint high hiss and crackle */
#define NPING 8
static float ping_c[2][NPING][4];               /* the drops' filters: rain, vinyl */
static struct {
    float s[2][2], c[2][4], g, gi, mb, ml, ph[2], gust, gust_t, d0, d1, dg, pl, pr, fade;
    uint32_t nz, rate, acc, gust_left;
    uint8_t type, dset, pc, ping;
} air;
static float air_vol, air_sway;

/* SHIMMER: the plate's output an octave up (a delay read at twice the speed by two heads half a window apart,
 * each faded in and out by a triangle so the jumps are never heard), darkened and thinned, back into the plate:
 * each time round a further octave, quieter, and heard beside the plate. The feedback is kept low (a plate this
 * long has a large gain of its own: higher, and the loop runs away). 8 KB; nothing while it is at 0 */
#define SH_N 2048
#define SH_MIX 0.35f                             /* the octave beside the plate, at Shimmer 100 */
#define SH_FB 0.6f                               /* back into it: times (1 - the plate's decay) */
#define SH_W 1600.0f                             /* the window: 36 ms */
static float sh_buf[SH_N];
static int sh_w;
static float sh_d = SH_W, sh_lp, sh_hp, sh_out, sh_g;

static void air_init(void)
{
    int i;
    static const float RAIN_F[NPING] = {1800.0f, 2200.0f, 2600.0f, 3000.0f, 3400.0f, 3900.0f, 4400.0f, 5000.0f};
    air.g = 0.0f;
    air.fade = 0.0f;
    air.nz = 12345u;
    for (i = 0; i < NPING; i++) {
        svf_coefs(ping_c[0][i], RAIN_F[i], 0.035f);
        svf_coefs(ping_c[1][i], 2500.0f + 450.0f * (float)i, 0.7f);
    }
    for (i = 0; i < SH_N; i++)
        sh_buf[i] = 0.0f;
    sh_lp = sh_hp = sh_out = 0.0f;
}

static void apply(int p, int v)
{
    const en_param_t *pi = &PARAMS[p];
    if (v < pi->lo)
        v = pi->lo;
    if (v > pi->hi)
        v = pi->hi;
    par[p] = (int16_t)v;
    switch (p) {
    case P_LEAD: case P_BACK: case P_DRUM: vols(); break;
    case P_ECHO: echo_send = (float)(v * v) * 0.00006f; break;
    case P_REVERB: rev_send = (float)(v * v) * 0.0001f; break;
    case P_ROOM:
        pl_decay_t = 0.45f + 0.47f * (float)v * 0.01f;   /* a snug corner .. a stone hall */
        echo_fb = 0.3f + 0.3f * (float)v * 0.01f;
        break;
    case P_TONE: tone = (float)v * 0.01f; break;
    case P_TEXTURE:
        if (air.type != (uint8_t)v) {            /* another texture: it comes in anew, over 2 s */
            air.type = (uint8_t)v;
            air.fade = 0.0f;
            air.acc = 64u;
        }
        break;
    case P_TEXLVL: air_vol = (float)(v * v) * 0.0001f * 0.5f; break;
    case P_TEXMOD: air_sway = (float)v * 0.01f; break;
    case P_SHIMMER: sh_g = (float)v * 0.01f; break;
    case P_TUNE: tunefac = fm_exp2f((float)v * (1.0f / 1200.0f)); break;
    case P_TEMPO:
        tempo_set();
        echo_time();
        break;
    case P_ROOT:
        tonic = 60 + v;
        if (tonic > 66)
            tonic -= 12;                         /* the tonic in G3..F#4: a fiddle's home */
        break;
    case P_MODE:
        pool = EN_MODE_POOL[v];
        nsteps = en_scale(pool, steps);
        break;
    case P_MIDI:
        if (!v)
            notes_off();
        break;
    default: break;
    }
}

static void quiet(void)
{
    int i;
    for (i = 0; i < NPLUCK; i++)
        pk[i].on = 0;
    for (i = 0; i < 2; i++)
        drum[i].on = 0, drum[i].wait = 0;
    for (i = 0; i < EN_NPAD; i++)
        reed[i].amp = 0.0f;
    for (i = 0; i < NLEAD; i++)
        lead[i].amp = lead[i].gate = 0.0f, lead[i].pend = -1;
    bass.amp = 0.0f;
    bass.wait = 0;
    air.g = 0.0f;
    notes_off();
}

void en_init(void)
{
    int i;
    for (i = 0; i <= SN; i++)
        sint[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SN));
    plate_init();
    air_init();
    for (i = 0; i < TAPE_N; i++)
        tape[i] = 0;
    for (i = 0; i < PLEX_N; i++)
        plex[i] = 0;
    tape_w = plex_w = tape_ph = 0;
    head2 = head2_t = 0.75f;
    head2_v = 0.0f;
    echo_len = 0.0f;
    q_r = q_w = 0;
    for (i = 0; i < NMOD; i++) {
        mod[i].v = mod[i].tgt = 0.0f;
        mod[i].left = 0;
    }
    for (i = 0; i < EN_NPAD; i++) {
        reed_t *v = &reed[i];
        v->note = v->target = (float)(55 + 5 * i);
        v->pan = REED_PAN[i];
        v->amp = 0.0f;
        v->ph[0] = 0.0f;
        v->ph[1] = 0.37f;
        v->hf = 1.0f;
        v->hand = 0;
        v->midi_note = 0;
    }
    for (i = 0; i < NLEAD; i++) {
        lead_t *v = &lead[i];
        v->amp = v->gate = 0.0f;
        v->dip = v->dipt = 1.0f;
        v->pend = -1;
        v->wait = v->cut = v->dipn = 0;
        v->inc = v->tinc = 440.0f / EN_SR;
        v->vib = 0.2f * (float)i;
        svf_coefs(v->c1, VOWELS[0][0], 0.4f);
        svf_coefs(v->c2, VOWELS[0][1], 0.35f);
    }
    for (i = 0; i < EN_NINST; i++) {
        lvl[i] = lvl_req[i] = 0;
        skill[i] = 60;
        en_glow[i] = 0.0f;
    }
    en_sess = 0;
    en_live = 0;
    box_gain = box_gate = 0.0f;
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, PARAMS[i].def);
    chord_set(0);
    for (i = 0; i < EN_NPAD; i++)
        reed[i].note = reed[i].target;
    pl_decay = pl_decay_t;
    quiet();
}

/* ------------------------------------------------------------ transport --- */
static void clock_pulse(void)
{
    if (pulse == 0) {
        sess_tick();
        en_bars = ++sixteenth;
    }
    if (!en_ext)
        midi(0xF8, 0, 0);
    if (++pulse >= 6)
        pulse = 0;
}

static void play_start(int from_top)
{
    if (from_top)
        pulse = 0;
    pulse_left = 0.0f;
    en_playing = 1;
    if (!en_ext)
        midi(from_top ? 0xFA : 0xFB, 0, 0);
}

static void play_stop(void)
{
    int d;
    for (d = 0; d < 2; d++)
        drum[d].wait = 0;                        /* a late hit still waiting stays unplayed */
    for (d = 0; d < NLEAD; d++) {
        lead[d].gate = 0.0f;
        lead[d].pend = -1;
    }
    en_playing = 0;
    if (!en_ext)
        midi(0xFC, 0, 0);
    notes_off();
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
            if (a && !en_playing)
                play_start(0);
            else if (!a && en_playing)
                play_stop();
            break;
        case C_SEED:
            rng ^= (uint32_t)(b & 0xFFFF) * 2654435761u;
            if (!rng)
                rng = 1u;
            rng2 ^= (uint32_t)(b & 0xFFFF) * 2246822519u;
            if (!rng2)
                rng2 = 1u;
            break;
        case C_CLOCK:
            en_ext = 1;
            ext_age = 0;
            if (a == EN_CLK_TICK) {
                if (en_playing)
                    clock_pulse();
            } else if (a == EN_CLK_START) {
                play_start(1);
            } else if (a == EN_CLK_CONTINUE) {
                if (!en_playing)
                    play_start(0);
            } else if (en_playing) {
                play_stop();
            }
            break;
        case C_PANIC: quiet(); break;
        case C_TUNE: sess_start((uint16_t)(c & 0xFFFFu), a & 3, (a >> 2 & 3) + 1, a >> 4 & 1); break;
        case C_END:
            if (en_sess == 1)
                en_passes = (uint8_t)(en_pass + 1);
            break;
        case C_INST:
            if (a < EN_NINST) {
                lvl_req[a] = (uint8_t)(b & 0xFF);
                skill[a] = (uint8_t)(b >> 8 & 0x7F);
                if (en_sess == 0) {              /* between tunes: at once */
                    lvl[a] = lvl_req[a];
                    vols();
                }
            }
            break;
        case C_TWANG:
            if (a < EN_NINST)
                twang(a);
            break;
        }
    }
}

/* --------------------------------------------------------------- render --- */
#define BLK 256
static float mixl[BLK], mixr[BLK], rsend[BLK], esend[BLK];

static void render_leads(uint32_t n)
{
    static const float GAIN[NLEAD] = {0.34f, 0.42f, 0.36f}, ATT[NLEAD] = {0.0006f, 0.0012f, 0.0004f};
    static const float VDEP[NLEAD] = {0.009f, 0.004f, 0.013f}, VRATE[NLEAD] = {5.6f, 4.8f, 5.1f};
    static const float GLIDE[NLEAD] = {0.004f, 0.008f, 0.0016f};
    int i;
    for (i = 0; i < NLEAD; i++) {
        lead_t *v = &lead[i];
        float g = ivol[i] * GAIN[i], p = (LEAD_PAN[i] + 1.0f) * (FM_PI / 4.0f), gl = fm_cosf(p) * g,
              gr = fm_sinf(p) * g, c = 0.0f, vm = 1.0f;
        uint32_t j;
        if (v->amp < 1e-5f && v->gate == 0.0f && v->pend < 0) {
            v->amp = 0.0f;
            continue;
        }
        for (j = 0; j < n; j++) {
            float x, inc;
            if (v->wait > 0 && --v->wait == 0)
                lead_apply(v, i);
            if (!(j & 31u)) {                    /* the vibrato, grown in on a held note; the tone */
                float d = fm_clampf((v->hold - 0.35f) * 1.6f, 0.0f, 1.0f) * VDEP[i];
                v->hold += 32.0f / EN_SR;
                v->vib += VRATE[i] * 32.0f / EN_SR;
                if (v->vib >= 1.0f)
                    v->vib -= 1.0f;
                vm = 1.0f + d * (sine(v->vib) );
                if (v->dipn > 0 && (v->dipn -= 32) <= 0)
                    v->dipt = 1.0f;
                c = 1.0f - fm_expf(-FM_TWO_PI * (1300.0f + 2200.0f * tone) * (1.0f / EN_SR));
            }
            v->inc += (v->tinc - v->inc) * GLIDE[i];
            inc = v->cut > 0 ? v->cut_inc : v->inc * vm;
            if (v->cut > 0)
                v->cut--;
            v->dip += (v->dipt - v->dip) * 0.004f;
            v->amp += (v->gate - v->amp) * (v->gate > v->amp ? ATT[i] : 0.00025f);
            if (i == I_FIDDLE) {                 /* a bowed string: a saw, darkened twice, a little hair */
                x = blep_saw(v->ph, inc) + 0.04f * nz();
                v->lp1 += c * (x - v->lp1);
                v->lp2 += c * (v->lp1 - v->lp2);
                x = v->lp2 * 1.3f;
            } else if (i == I_WHISTLE) {         /* a wooden whistle: nearly pure, and breath */
                v->lp1 += 0.2f * (nz() - v->lp1);
                x = sine(v->ph) + 0.09f * sine(2.0f * v->ph) + 0.05f * sine(3.0f * v->ph) + 0.12f * v->lp1;
            } else {                             /* a voice: a saw through two formants (TPT SVF, Zavalishin) */
                float s = blep_saw(v->ph, inc), v1, v2, v3, b1, b2;
                v3 = s - v->s[1];
                v1 = v->c1[0] * v->s[0] + v->c1[1] * v3;
                v2 = v->s[1] + v->c1[1] * v->s[0] + v->c1[2] * v3;
                v->s[0] = 2.0f * v1 - v->s[0];
                v->s[1] = 2.0f * v2 - v->s[1];
                b1 = v1 * v->c1[3];
                v3 = s - v->s[3];
                v1 = v->c2[0] * v->s[2] + v->c2[1] * v3;
                v2 = v->s[3] + v->c2[1] * v->s[2] + v->c2[2] * v3;
                v->s[2] = 2.0f * v1 - v->s[2];
                v->s[3] = 2.0f * v2 - v->s[3];
                b2 = v1 * v->c2[3];
                x = (b1 + 0.6f * b2 + 0.25f * v2) * 1.6f;
            }
            v->ph += inc;
            if (v->ph >= 1.0f)
                v->ph -= 1.0f;
            x *= v->amp * v->dip;
            mixl[j] += x * gl;
            mixr[j] += x * gr;
            rsend[j] += x * g * 0.55f;
            esend[j] += x * g * 0.3f;
        }
        v->lp1 = fm_flush(v->lp1);
        v->lp2 = fm_flush(v->lp2);
        for (j = 0; j < 4; j++)
            v->s[j] = fm_flush(v->s[j]);
    }
}

static void render_plucks(uint32_t n)
{
    int i;
    const float gb = 0.55f;
    for (i = 0; i < NPLUCK; i++) {
        pluck_t *v = &pk[i];
        float cph = v->cph, mph = v->mph, amp = v->amp, idx = v->idx, att = v->att, g = ivol[v->inst] * gb;
        uint32_t j = 0;
        if (!v->on)
            continue;
        if (v->inst == I_LUTE && !lvl[I_LUTE] && v->note >= 81)
            g = gb * 0.5f;                       /* a twang: heard whoever it was */
        if (v->wait > 0) {
            if ((uint32_t)v->wait >= n) {
                v->wait -= (int32_t)n;
                continue;
            }
            j = (uint32_t)v->wait;
            v->wait = 0;
        }
        for (; j < n; j++) {
            float m = sine(mph) * idx, x;
            x = sine(cph + m + 4.0f) * amp * att * g;   /* + 4 turns: positive for the table */
            mixl[j] += x * v->gl;
            mixr[j] += x * v->gr;
            rsend[j] += x * 0.5f;
            esend[j] += x * 0.3f;
            cph += v->cinc;
            mph += v->minc;
            if (cph >= 1.0f)
                cph -= 1.0f;
            if (mph >= 1.0f)
                mph -= 1.0f;
            amp *= v->adec;
            idx *= v->idec;
            att += (1.0f - att) * 0.08f;         /* the pluck: a couple of milliseconds */
        }
        if (v->bend != 1.0f) {
            float b = fm_exp2f((float)n * fm_log2f(v->bend));
            v->cinc *= b;
            v->minc *= b;
        }
        v->cph = cph;
        v->mph = mph;
        v->amp = amp;
        v->idx = idx;
        v->att = att;
        if (amp < 0.0008f) {
            v->on = 0;
            midi(0x81, v->note, 0);
        }
    }
}

#define HAND_OUT (32.0f / (0.045f * EN_SR))
#define HAND_IN (32.0f / (0.07f * EN_SR))
static void render_box(uint32_t n)
{
    int i;
    const float g = ivol[I_BOX] * 0.42f;
    float f1 = 520.0f + 260.0f * tone, f2 = 1350.0f + 700.0f * tone;
    box_gate = en_playing && en_sess && lvl[I_BOX] ? 1.0f : 0.0f;
    box_gain += (box_gate - box_gain) * fm_minf((float)n / ((box_gate > 0.0f ? 0.25f : 0.6f) * EN_SR), 1.0f);
    for (i = 0; i < EN_NPAD; i++) {
        reed_t *v = &reed[i];
        float p0 = v->ph[0], p1 = v->ph[1], amp = v->amp, s0 = v->ic[0], s1 = v->ic[1], s2 = v->ic[2], s3 = v->ic[3],
              pn = (v->pan + 1.0f) * (FM_PI / 4.0f), gl = fm_cosf(pn) * g, gr = fm_sinf(pn) * g, tgt = 0.0f;
        uint32_t j;
        if (amp < 1e-5f && box_gain < 1e-4f) {
            v->amp = 0.0f;
            continue;
        }
        for (j = 0; j < n; j++) {
            float x, v1, v2, v3, b1, l1, b2;
            if (!(j & 31u)) {                    /* every 32 samples: a chord change's crossing, pitch */
                float f;
                if (v->hand == 1) {
                    v->hf -= HAND_OUT;
                    if (v->hf <= 0.0f) {
                        v->hf = 0.0f;
                        v->hand = 2;
                        v->note = v->target;
                    }
                } else if (v->hand == 2) {
                    v->hf += HAND_IN;
                    if (v->hf >= 1.0f) {
                        v->hf = 1.0f;
                        v->hand = 0;
                    }
                }
                f = mtof(v->note + 0.03f * mod[i].v);
                v->inc[0] = f * (1.0f / EN_SR);
                v->inc[1] = f * 1.0026f * (1.0f / EN_SR);   /* +4.5 cents: the musette's slow beat */
                svf_coefs(v->c1, f1 * (1.0f + 0.03f * (float)i), 0.5f);
                svf_coefs(v->c2, f2 * (1.0f - 0.02f * (float)i), 0.4f);
                tgt = box_gain * v->hf * (i == 3 ? 0.7f : 1.0f);
            }
            x = blep_saw(p0, v->inc[0]) + 0.7f * blep_saw(p1, v->inc[1]);
            p0 += v->inc[0];
            if (p0 >= 1.0f)
                p0 -= 1.0f;
            p1 += v->inc[1];
            if (p1 >= 1.0f)
                p1 -= 1.0f;
            v3 = x - s1;
            v1 = v->c1[0] * s0 + v->c1[1] * v3;
            v2 = s1 + v->c1[1] * s0 + v->c1[2] * v3;
            s0 = 2.0f * v1 - s0;
            s1 = 2.0f * v2 - s1;
            b1 = v1 * v->c1[3];
            l1 = v2;
            v3 = x - s3;
            v1 = v->c2[0] * s2 + v->c2[1] * v3;
            v2 = s3 + v->c2[1] * s2 + v->c2[2] * v3;
            s2 = 2.0f * v1 - s2;
            s3 = 2.0f * v2 - s3;
            b2 = v1 * v->c2[3];
            amp += (tgt - amp) * 0.004f;
            x = (b1 + 0.55f * b2 + 0.3f * l1) * amp;
            mixl[j] += x * gl;
            mixr[j] += x * gr;
            rsend[j] += x * g * 0.6f;
            esend[j] += x * g * 0.1f;
        }
        v->ic[0] = fm_flush(s0);
        v->ic[1] = fm_flush(s1);
        v->ic[2] = fm_flush(s2);
        v->ic[3] = fm_flush(s3);
        v->amp = amp;
        v->ph[0] = p0;
        v->ph[1] = p1;
    }
}

static void render_bass(uint32_t n)
{
    uint32_t j;
    const float g = ivol[I_BASS] * 0.55f;
    float ph = bass.ph, amp = bass.amp, br = bass.br, att = bass.att;
    if (amp < 1e-5f && bass.wait <= 0) {
        bass.amp = 0.0f;
        return;
    }
    for (j = 0; j < n; j++) {
        float x;
        if (bass.wait > 0 && --bass.wait == 0) {   /* the pluck lands */
            bass.inc = bass.pinc;
            amp = bass.pvel;
            br = 1.0f;
            att = 0.0f;
            bass.dec = fm_expf(-6.9f / ((en_kind == K_AIR ? 2.6f : 1.4f) * EN_SR));
            bass.bdec = fm_expf(-6.9f / (0.35f * EN_SR));
        }
        x = sine(ph) + br * (0.55f * sine(2.0f * ph) + 0.3f * sine(3.0f * ph));
        ph += bass.inc;
        if (ph >= 1.0f)
            ph -= 1.0f;
        att += (1.0f - att) * 0.02f;
        amp *= bass.dec;
        br *= bass.bdec;
        x *= amp * att * g;
        mixl[j] += x;
        mixr[j] += x;
        rsend[j] += x * 0.12f;
    }
    bass.ph = ph;
    bass.amp = amp;
    bass.br = br;
    bass.att = att;
}

static void render_drums(uint32_t n)
{
    int d;
    const float g = ivol[I_DRUM] * 0.9f;
    for (d = 0; d < 2; d++) {
        drum_t *v = &drum[d];
        float ph = v->ph, inc = v->inc, amp = v->amp, att = v->att, nl = v->nlp, gd = g * (d ? 0.5f : 1.0f);
        uint32_t j;
        if (!v->on)
            continue;
        for (j = 0; j < n; j++) {
            float x;
            nl += (d ? 0.35f : 0.12f) * (nz() - nl);   /* the skin's breath */
            x = (sine(ph) + (d ? 0.9f : 0.35f) * nl * amp) * amp * att * gd;
            mixl[j] += x * (d ? 0.8f : 1.0f);
            mixr[j] += x * (d ? 1.0f : 0.9f);
            rsend[j] += x * 0.18f;
            ph += inc;
            if (ph >= 1.0f)
                ph -= 1.0f;
            inc += (v->inc_end - inc) * 0.0015f;
            amp *= v->adec;
            att += (1.0f - att) * 0.05f;
        }
        v->ph = ph;
        v->inc = inc;
        v->amp = amp;
        v->att = att;
        v->nlp = fm_flush(nl);
        v->on = amp > 0.0005f;
    }
}

/* ---------------------------------------------- Drift 1.1: air, shimmer --- */
static void air_control(uint32_t n)
{
    float sw = air_sway, g = 0.0f, w0, w1;
    if (air_vol <= 0.0f && air.g < 1e-6f)
        return;                                  /* (nothing at all while the air is at 0) */
    air.fade = fm_minf(air.fade + (float)n * (1.0f / (2.0f * EN_SR)), 1.0f);   /* a new texture: in over 2 s */
    if (air.gust_left <= n) {                    /* the wind's next gust (or lull) */
        air.gust_t = frand2();
        air.gust_left = (uint32_t)((2.0f + 6.0f * frand2()) * EN_SR);
    } else {
        air.gust_left -= n;
    }
    air.gust += (air.gust_t - air.gust) * fm_minf((float)n * (1.0f / (2.5f * EN_SR)), 1.0f);
    air.ph[0] += (float)n * (1.0f / (9.0f * EN_SR));     /* the sea's waves */
    air.ph[1] += (float)n * (1.0f / (10.3f * EN_SR));
    if (air.ph[0] >= 1.0f)
        air.ph[0] -= 1.0f;
    if (air.ph[1] >= 1.0f)
        air.ph[1] -= 1.0f;
    air.acc += n;
    if (air.acc < 64u)
        return;
    {
        float gu = 0.5f + sw * (air.gust - 0.5f);    /* the sway: how far the gusts and waves go */
        air.acc = 0;
        switch (air.type) {
        case 0:                                  /* rain */
            svf_coefs(air.c[0], 3000.0f + 800.0f * sw * (air.gust - 0.5f), 1.4f);
            svf_coefs(air.c[1], 3400.0f - 800.0f * sw * (air.gust - 0.5f), 1.4f);
            air.mb = 1.0f;
            air.ml = 0.12f;
            g = 0.32f;
            air.rate = (uint32_t)((14.0f + 40.0f * gu) * (16777216.0f / EN_SR));   /* drops a second */
            air.dset = 0;
            air.dg = 2.5f;
            break;
        case 1:                                  /* wind */
            svf_coefs(air.c[0], 300.0f * fm_exp2f(2.2f * gu), 0.4f);
            svf_coefs(air.c[1], 340.0f * fm_exp2f(2.0f * gu), 0.45f);
            air.mb = 1.0f;
            air.ml = 0.25f;
            g = 0.25f + 0.75f * gu;
            air.rate = 0;
            break;
        case 2:                                  /* sea */
            w0 = 0.5f - 0.5f * sine(air.ph[0] + 0.25f);
            w1 = 0.5f - 0.5f * sine(air.ph[1] + 0.25f);
            w0 = 1.0f - (0.3f + 0.6f * sw) * (1.0f - w0 * w0);
            w1 = 1.0f - (0.3f + 0.6f * sw) * (1.0f - w1 * w1);
            svf_coefs(air.c[0], 220.0f * fm_exp2f(3.4f * w0), 0.9f);
            svf_coefs(air.c[1], 220.0f * fm_exp2f(3.4f * w1), 0.9f);
            air.mb = 0.3f;
            air.ml = 1.0f;
            g = 0.6f * (w0 + w1);
            air.rate = 0;
            break;
        default:                                 /* vinyl */
            svf_coefs(air.c[0], 6000.0f, 1.6f);
            svf_coefs(air.c[1], 6200.0f, 1.6f);
            air.mb = 0.25f;
            air.ml = 0.0f;
            g = 0.3f;
            air.rate = (uint32_t)((5.0f + 25.0f * gu) * (16777216.0f / EN_SR));
            air.dset = 1;
            air.dg = 5.0f;
            break;
        }
        air.gi = (g * air.fade * air_vol - air.g) * (1.0f / 64.0f);   /* to its new gain over 64 samples */
        air.pc = 64;
    }
}

static void render_air(uint32_t n)
{
    uint32_t j;
    float a0 = air.s[0][0], a1 = air.s[0][1], b0 = air.s[1][0], b1 = air.s[1][1], d0 = air.d0, d1 = air.d1, g = air.g;
    const float *cl = air.c[0], *cr = air.c[1], *pc = ping_c[air.dset][air.ping];
    const float dg = air.dg * air_vol * air.fade;
    if (air_vol <= 0.0f && g < 1e-6f)
        return;
    for (j = 0; j < n; j++) {
        float nl, nr, v1, v2, v3, l, r, imp = 0.0f, p;
        air.nz = air.nz * 1664525u + 1013904223u;
        nl = (float)(int32_t)air.nz * (1.0f / 2147483648.0f);
        if ((air.nz >> 8) < air.rate) {          /* a drop, or a tick: its pitch and place from the noise */
            air.ping = (uint8_t)(air.nz & 7u);
            pc = ping_c[air.dset][air.ping];
            imp = 0.4f + 0.6f * (float)(air.nz >> 3 & 15u) * (1.0f / 15.0f);
            air.pl = (float)(air.nz >> 7 & 7u) * (1.0f / 7.0f);
            air.pr = 1.0f - air.pl;
        }
        air.nz = air.nz * 1664525u + 1013904223u;
        nr = (float)(int32_t)air.nz * (1.0f / 2147483648.0f);
        v3 = nl - a1;                            /* left */
        v1 = cl[0] * a0 + cl[1] * v3;
        v2 = a1 + cl[1] * a0 + cl[2] * v3;
        a0 = 2.0f * v1 - a0;
        a1 = 2.0f * v2 - a1;
        l = air.mb * v1 + air.ml * v2;
        v3 = nr - b1;                            /* right */
        v1 = cr[0] * b0 + cr[1] * v3;
        v2 = b1 + cr[1] * b0 + cr[2] * v3;
        b0 = 2.0f * v1 - b0;
        b1 = 2.0f * v2 - b1;
        r = air.mb * v1 + air.ml * v2;
        v3 = imp - d1;                           /* the drop's ring */
        v1 = pc[0] * d0 + pc[1] * v3;
        v2 = d1 + pc[1] * d0 + pc[2] * v3;
        d0 = 2.0f * v1 - d0;
        d1 = 2.0f * v2 - d1;
        p = v1 * dg;
        if (air.pc) {
            g += air.gi;
            air.pc--;
        }
        l = l * g + p * air.pl;
        r = r * g + p * air.pr;
        mixl[j] += l;
        mixr[j] += r;
        rsend[j] += (l + r) * 0.12f;
    }
    air.s[0][0] = fm_flush(a0);
    air.s[0][1] = fm_flush(a1);
    air.s[1][0] = fm_flush(b0);
    air.s[1][1] = fm_flush(b1);
    air.d0 = fm_flush(d0);
    air.d1 = fm_flush(d1);
    air.g = g < 1e-6f ? 0.0f : g;
}

static inline float sh_tap(float d)
{
    int k = (int)d, i0 = sh_w - k, i1 = i0 - 1;
    float f = d - (float)k, a, b;
    if (i0 < 0)
        i0 += SH_N;
    if (i1 < 0)
        i1 += SH_N;
    a = sh_buf[i0];
    b = sh_buf[i1];
    return a + (b - a) * f;
}

static inline void shimmer(float x)             /* a sample of the plate in; sh_out: the octave up, for the next */
{
    float d2, w1, y;
    sh_buf[sh_w] = x;
    sh_d -= 1.0f;                                /* the heads gain a sample a sample: twice the speed */
    if (sh_d < 1.0f)
        sh_d += SH_W;
    d2 = sh_d > 0.5f * SH_W ? sh_d - 0.5f * SH_W : sh_d + 0.5f * SH_W;
    w1 = sh_d * (2.0f / SH_W) - 1.0f;            /* the first head's triangle: 0 at its jump, 1 halfway */
    w1 = 1.0f - (w1 < 0.0f ? -w1 : w1);
    y = w1 * sh_tap(sh_d) + (1.0f - w1) * sh_tap(d2);
    sh_lp += 0.45f * (y - sh_lp);                /* darker, thinner */
    sh_hp += 0.03f * (sh_lp - sh_hp);
    sh_out = fm_clampf(sh_lp - sh_hp, -2.0f, 2.0f);
    if (++sh_w >= SH_N)
        sh_w = 0;
}

static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    const float rg = rev_send * rev_in_gain, eg = echo_send;
    const float shg = sh_g * rev_in_gain;       /* shimmer (Drift 1.1) */
    const float shf = shg * SH_FB * (1.0f - pl_decay);   /* less fed back as the plate grows long: never runs away */
    float pk2 = 0.0f;
    for (j = 0; j < n; j++)
        mixl[j] = mixr[j] = rsend[j] = esend[j] = 0.0f;
    render_box(n);
    render_leads(n);
    render_plucks(n);
    render_bass(n);
    render_drums(n);
    render_air(n);                               /* (nothing while at 0) */
    for (j = 0; j < n; j++) {
        float wl, wr, l, r, el, er, xl, xr, rin;
        echoes(esend[j] * eg, &el, &er, &xl, &xr);
        rin = (rsend[j] * rg + (el + er + xl + xr) * 0.3f * rev_send) * 0.8f;
        if (shg > 0.0f)                          /* a little back into the plate: each time round an octave up */
            rin += sh_out * shf;
        reverb(rin, &wl, &wr);
        if (shg > 0.0f) {                        /* and the octave heard with the plate */
            shimmer(wl + wr);
            wl += sh_out * shg * SH_MIX;
            wr += sh_out * shg * SH_MIX;
        }
        l = (mixl[j] + 0.6f * el + 0.25f * xl + 1.2f * wl) * 0.4f * gain;
        r = (mixr[j] + 0.6f * er + 0.25f * xr + 1.2f * wr) * 0.4f * gain;
        l = fm_tanhf(l);
        r = fm_tanhf(r);
        pk2 = fm_maxf(pk2, fm_maxf(fm_fabsf(l), fm_fabsf(r)));
        out[2 * j] = (int32_t)(l * 8300000.0f);
        out[2 * j + 1] = (int32_t)(r * 8300000.0f);
    }
    en_level += (pk2 - en_level) * fm_minf((float)n * (1.0f / (0.3f * EN_SR)), 1.0f);
}

static void control(uint32_t n)                 /* once a chunk: the slow things */
{
    int i;
    float k = 1.0f - fm_minf((float)n * (1.0f / (0.25f * EN_SR)), 1.0f);
    mods_step(n);
    heads_step(n);
    air_control(n);                              /* Drift 1.1's (nothing while at 0) */
    pl_decay += (pl_decay_t - pl_decay) * 0.002f;
    pl_dd2 = fm_clampf(pl_decay + 0.15f, 0.25f, 0.5f);
    for (i = 0; i < EN_NINST; i++)
        en_glow[i] *= k;
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    drain();
    if (en_ext && ++ext_age > 100u)
        en_ext = 0;
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        int timed = en_playing && !en_ext;
        if (timed) {
            if (pulse_left <= 0.0f) {
                clock_pulse();
                pulse_left += step_len * (1.0f / 6.0f);
            }
            if ((float)k > pulse_left)
                k = (uint32_t)pulse_left + 1u;
        }
        if (k > n)
            k = n;
        {
            int32_t w = drums_wait();            /* a late hit lands on its sample */
            if (w > 0 && (uint32_t)w < k)
                k = (uint32_t)w;
        }
        if (timed)
            pulse_left -= (float)k;
        control(k);
        render_chunk(out, k, gain);
        drums_wait_step((int32_t)k);
        out += 2 * k;
        n -= k;
    }
}
