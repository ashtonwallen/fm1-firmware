/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * JUKE JOINT: ashtonwallen, 2026 */
/* The instrument (juke.h). One compilation unit, -O2: this file (the parameters, the nights, the voices, the room,
 * the render) and the band (juke_band.c: the forms, the licks, the arrangement and the players, bar by bar). The
 * command queue and the render's shape are AMB-1's; the piano is FELT PIANO's; the bass and the brushes began as
 * JAZZ TRIO's. */
#include "juke.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Tempo", 80, 120, 100}, {"Harp", 0, 100, 55},  {"Drive", 0, 100, 45},  {"Room", 0, 100, 40},
    {"Piano", 0, 100, 70},   {"Bass", 0, 100, 70},  {"Drums", 0, 100, 60},  {"Ambience", 0, 100, 15},
    {"Age", 0, 100, 25},     {"Width", 0, 100, 55}, {"Length", 0, 4, 1},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }
static const uint8_t LEN_MIN[5] = {2, 3, 4, 6, 8};

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
    if (i == P_LENGTH)
        put_s(put_u(buf, LEN_MIN[v < 0 ? 0 : v > 4 ? 4 : v]), " min");
    else if (i == P_TEMPO)
        put_s(put_u(buf, (uint32_t)(v < 0 ? 0 : v)), "%");
    else if (i == P_HARP && v == 0)
        put_s(buf, "Out");
    else
        put_u(buf, (uint32_t)(v < 0 ? 0 : v));
}

/* ------------------------------------------------------------- chords --- */
/* qualities: semitones above the root */
enum { T_DOM7, T_DOM9, T_M7, T_M9, T_DIM7, T_6, T_M6, T_7B9, T_MAJ, T_MIN, T_M7B5, T_N };
static const uint8_t CT_N[T_N] = {4, 5, 4, 5, 4, 4, 4, 5, 3, 3, 4};
static const int8_t CT_IV[T_N][5] = {
    {0, 4, 7, 10}, {0, 4, 7, 10, 14}, {0, 3, 7, 10}, {0, 3, 7, 10, 14}, {0, 3, 6, 9}, {0, 4, 7, 9}, {0, 3, 7, 9},
    {0, 4, 7, 10, 13}, {0, 4, 7}, {0, 3, 7}, {0, 3, 6, 10},
};
static const char *const Q_NAME[T_N] = {"7", "9", "m7", "m9", "dim", "6", "m6", "7b9", "", "m", "m7b5"};
const char *en_q_name(int q) { return Q_NAME[(unsigned)q < T_N ? q : 0]; }
void en_chord_name(int root, int q, char *b) { put_s(put_s(b, EN_NOTE_NAME[(root % 12 + 12) % 12]), en_q_name(q)); }
typedef struct {
    int8_t root;                                 /* semitones above the tonic; 99: the bar's first chord held */
    uint8_t t;
} ch_t;

/* ------------------------------------------------------------- nights --- */
/* the user's ten (Tempo Harp Drive Room, Piano Bass Drums Ambience, Age Width) and the night's own: how often each
 * kind of tune comes up (slow, shuffle, minor, ballad: weights), the piano's felt (0 bright .. 100 dark), the harp's
 * sound (0 acoustic, cupped; 1 through a small amp), the changes (0 plain, 1 some jazzier slow blues), how often a
 * solo goes to stop time, and the room's look on the screen */
typedef struct {
    const char *name;
    int8_t v[P_NSTYLE];
    uint8_t w[K_N];
    uint8_t felt, amp, jazz, stop, scene;
} style_t;
static const style_t STYLES[EN_NSTYLES] = {
    /*                   Tem Hrp Drv Roo  Pno Bas Drm Amb  Age Wid   slow shf min bal  felt amp jazz stop scene */
    {"Sawdust Floor",   {100, 55, 45, 40, 70, 70, 60, 15, 25, 55}, {4, 4, 2, 2}, 45, 0, 0, 35, 0},
    {"Cigar Box Choir", {96, 45, 35, 35, 72, 68, 55, 12, 40, 50}, {6, 2, 2, 3}, 55, 0, 0, 25, 1},
    {"Catfish Plate",   {106, 60, 60, 30, 68, 74, 66, 18, 20, 55}, {2, 7, 1, 1}, 35, 1, 0, 50, 2},
    {"Hambone Annie",   {104, 80, 55, 38, 66, 70, 62, 15, 30, 60}, {3, 5, 2, 1}, 40, 1, 0, 40, 3},
    {"Smokehouse",      {94, 40, 40, 45, 72, 68, 52, 14, 55, 50}, {3, 2, 5, 2}, 50, 0, 0, 20, 1},
    {"Tin Roof Ticks",  {98, 55, 30, 50, 70, 66, 50, 22, 35, 60}, {5, 2, 2, 4}, 60, 0, 1, 15, 0},
    {"Two Dollar Cot",  {92, 35, 25, 42, 74, 64, 48, 16, 45, 50}, {4, 1, 2, 6}, 65, 0, 0, 10, 2},
    {"Chitlin Circuit", {108, 70, 70, 32, 66, 74, 70, 20, 25, 60}, {2, 6, 2, 1}, 30, 1, 0, 55, 3},
    {"Deacon's Flask",  {100, 50, 45, 45, 70, 70, 58, 15, 30, 55}, {4, 3, 2, 2}, 45, 0, 1, 30, 1},
    {"Mule Shoes",      {102, 65, 50, 36, 68, 72, 60, 12, 50, 55}, {3, 6, 1, 1}, 50, 0, 0, 45, 2},
    {"Ida Belle",       {96, 60, 40, 48, 70, 66, 54, 18, 20, 60}, {3, 2, 6, 2}, 40, 1, 1, 30, 0},
    {"Busted Jukebox",  {104, 75, 65, 34, 66, 72, 66, 26, 65, 55}, {3, 5, 2, 1}, 40, 1, 0, 50, 3},
    {"Lamp Oil",        {90, 45, 20, 52, 74, 62, 44, 10, 40, 50}, {3, 1, 2, 7}, 70, 0, 1, 10, 1},
};
static const style_t *sty(int i) { return &STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES]; }
const char *en_style_name(int i) { return sty(i)->name; }
int en_style_scene(int i) { return sty(i)->scene; }
void en_style_recipe(int i, int16_t *par)
{
    int k;
    for (k = 0; k < P_NSTYLE; k++)
        par[k] = sty(i)->v[k];
}
static const char *const KIND_NAME[K_N] = {"Slow blues", "Shuffle", "Minor blues", "Slow ballad"};
const char *en_kind_name(int k) { return KIND_NAME[(unsigned)k < K_N ? k : 0]; }
static const char *const SECTION_NAME[S_N] = {"Count", "Intro", "Head", "Piano", "Harp", "Stop time", "Head out",
                                              "Ending", "Break"};
const char *en_section_name(int s) { return SECTION_NAME[(unsigned)s < S_N ? s : 0]; }

/* -------------------------------------------------------------- titles --- */
static const char *const T_WHO[] = {"Big Odessa", "Tommy Lee", "Cora Lee", "Deacon Bill", "Skinny", "Roosevelt",
                                    "Little Royal", "Sugar Boy", "Hattie Mae", "Peewee", "Mister Ellis",
                                    "Sister Pearl", "Fat Clyde", "Boo", "Bessie Ray", "Cleotha"};
static const char *const T_THING[] = {"hock shop", "cotton sack", "coal stove", "seed store", "boll weevil",
                                      "mojo hand", "wash tub", "tin cup", "porch light", "railroad watch",
                                      "two-room shack", "back door", "feather bed", "chicken wire", "skillet",
                                      "levee", "molasses", "snuff tin", "crawfish", "corn liquor"};
static const char *const T_PLACE[] = {"Clarksdale", "Helena", "Tupelo", "Itta Bena", "Yazoo City", "Shreveport",
                                      "Greenwood", "Natchez", "Vicksburg", "Gary", "Leland", "Rosedale"};
static const char *const T_GO[] = {"walkin'", "goin'", "leavin'", "rollin'", "headed", "crawlin'"};

uint32_t en_track_rand(uint32_t base, uint32_t no)
{
    uint32_t h = base ^ (no * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h ? h : 1u;
}

#define NA(a) (int)(sizeof a / sizeof a[0])
static int slen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}
void en_title(uint32_t seed, int kind, char *b)
{
    uint32_t h = en_track_rand(seed, 77u);
    const char *w = T_WHO[h % (uint32_t)NA(T_WHO)], *t = T_THING[(h >> 8) % (uint32_t)NA(T_THING)];
    const char *p = T_PLACE[(h >> 13) % (uint32_t)NA(T_PLACE)];
    int f = (int)((h >> 20) % 5u);
    if (kind == K_MINOR && f == 4)
        f = 1;
    switch (f) {
    case 0:
        if (slen(w) + slen(t) > 22) {            /* a short name: the title fits */
            static const char *const SHORT[] = {"Peewee", "Skinny", "Boo", "Roosevelt"};
            w = SHORT[(h >> 4) % 4u];
        }
        put_s(put_s(put_s(b, w), "'s "), t);
        break;
    case 1: put_s(put_s(b, t), " blues"); break;
    case 2: put_s(put_s(put_s(put_s(b, T_GO[(h >> 24) % (uint32_t)NA(T_GO)]), " to "), p), ""); break;
    case 3: put_s(put_s(b, w), kind == K_SHUF ? " shuffle" : kind == K_BALLAD ? "'s lament" : " blues"); break;
    default: put_s(put_s(b, kind == K_SHUF ? "back to " : "low in "), p); break;
    }
    if (b[0] >= 'A' && b[0] <= 'Z' && f != 0 && f != 3)
        b[0] = (char)(b[0] + 32);
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_STYLE, C_PLAY, C_SKIP, C_REPEAT, C_TRACK_NO, C_TRACK_SEED_HI, C_TRACK_SEED_LO, C_NOTE, C_PANIC };
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
void en_track(uint32_t no, uint32_t seed)
{
    post(C_TRACK_SEED_HI, 0, (int)(seed >> 16));
    post(C_TRACK_SEED_LO, 0, (int)(seed & 0xFFFFu));
    post(C_TRACK_NO, (int)(no >> 16 & 0xFFu), (int)(no & 0xFFFFu));
}

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_phase, en_repeat_on, en_key, en_kind, en_bpm = 54, en_section, en_bar, en_form_len = 12;
volatile uint8_t en_chorus, en_nchorus, en_form_q[12], en_chord_root, en_chord_q, en_harp_in, en_harp_on;
volatile int8_t en_form_root[12];
volatile uint8_t en_harp_note, en_rh_note = 70, en_bass_note = 36, en_hits[NHITS], en_beat_no, en_voices;
volatile uint32_t en_track_no, en_track_seed, en_seed_base = 0x4A4B4A54u, en_track_ms, en_track_len_ms;
volatile uint16_t en_chord_pcs;
volatile float en_applause, en_beat, en_vu_l, en_vu_r;
volatile uint32_t en_now_ms, en_notes_played, en_tracks_played;
#ifdef EN_HOST
void (*en_dbg_note)(int bar, int tick, int note, float vel, int role, int dur_ticks);
void (*en_dbg_chord)(int bar, int tick, int root_pc, uint16_t pcs);
void (*en_dbg_mark)(int bar, int section, int chorus, int bar12);
#endif

static int16_t par[P_NPARAMS];
static int style_i;
static uint32_t rng = 0x9E3779B9u, nrng = 0x2545F491u, trng = 1u;
static uint32_t ms_rem, pstart;                  /* pstart: the tune's start on the clock */

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
static float pf(int p) { return (float)par[p] * 0.01f; }

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
static inline float tau(float sec) { return fm_expf(-1.0f / (sec * EN_SR)); }
static inline float onepole(float fc) { return 1.0f - fm_expf(-FM_TWO_PI * fc / EN_SR); }
static float lvl(int p) { return pf(p) * (1.0f / 0.7f); }   /* a mix level: 1 at 70 */
static float fade = 1.0f, fade_t = 1.0f, fade_rate;

#define BLK 256
static float bufl[BLK], bufr[BLK], rsend[BLK];

/* --------------------------------------------------------------- piano --- */
/* FELT PIANO's: a note is up to NM partials, each a two-pole resonator ringing down from a sine's start. The lowest
 * two are doubled, a hair apart (the unison strings' beat); the felt is a low-pass on the partials' levels and a
 * soft rise; dampers, and a little pedal on the slow tunes */
#define NV 12
#define NM 7
typedef struct {
    float y1[NM], y2[NM], c[NM], r2[NM], lvl[NM], rb[NM];
    float env, env_t, env_k, gl, gr;
    float damp_k;
    int32_t gate;                                /* samples the key is held; < 0 held by the player; 0 up */
    uint32_t cnt;
    uint8_t nm, note, on, player, nodamp, damped;
} pv_t;
static pv_t pv[NV];
static int ped;
static float vb[BLK], felt = 0.45f;

static void damp(pv_t *v, float k)
{
    v->env_t = 0.0f;
    v->env_k = k;
    v->damped = 1;
}

static struct {
    float kamp, kph, kinc, kdec, namp, ndec, nlp, na;
} mech;
static void knock(float amp, float hz, float dec_s, float namp, float ndec_s, float nfc)
{
    mech.kamp += amp * 0.3f;
    mech.kinc = hz / EN_SR;
    mech.kdec = tau(dec_s);
    mech.namp += namp * 0.3f;
    mech.ndec = tau(ndec_s);
    mech.na = onepole(nfc);
}

static void piano_on(int note, float vel, int32_t gate, int player)
{
    pv_t *v = 0;
    int i, m, nm;
    float f0, b, t1, fc, a, age = pf(P_AGE), lowest = 1e9f, det, uni;
    static const uint8_t PN[NM] = {1, 1, 2, 2, 3, 4, 5};
    while (note < 24)
        note += 12;
    while (note > 100)
        note -= 12;
    vel = fm_clampf(vel, 0.02f, 1.0f);
    for (i = 0; i < NV; i++)                     /* the same string struck again: the old sound let go, quickly */
        if (pv[i].on && pv[i].note == note && !pv[i].damped)
            damp(&pv[i], 1.0f - tau(0.03f));
    for (i = 0; i < NV; i++)
        if (!pv[i].on) {
            v = &pv[i];
            break;
        }
    if (!v)                                      /* else the quietest (released ones first) */
        for (i = 0; i < NV; i++) {
            float l = pv[i].lvl[0] * pv[i].env * (pv[i].gate == 0 ? 0.5f : 1.0f) * (pv[i].player ? 4.0f : 1.0f);
            if (l < lowest) {
                lowest = l;
                v = &pv[i];
            }
        }
    det = (frand() - 0.5f) * age * 0.22f;        /* an old upright: a little out, the unisons wider */
    uni = (0.008f + 0.014f * frand()) * (1.0f + 4.5f * age);
    f0 = mtof((float)note + det);
    b = fm_clampf(2.5e-4f * fm_exp2f(((float)note - 60.0f) * (1.0f / 14.0f)), 6e-5f, 5e-3f);
    t1 = fm_clampf(13.0f * fm_exp2f(-((float)note - 33.0f) * (1.0f / 17.0f)), 0.8f, 14.0f) * (1.0f - 0.2f * felt);
    fc = (950.0f + 3600.0f * (1.0f - felt)) * (0.4f + 0.6f * vel);
    fc = fm_maxf(fc, f0 * 2.3f);
    a = 0.03f + 0.97f * fm_powf(vel, 1.6f);
    nm = note < 60 ? 7 : note < 76 ? 6 : note < 88 ? 5 : 3;
    for (m = 0; m < nm; m++) {
        int n = PN[m], second = (m == 1 || m == 3);
        float fn = (float)n * f0 * fm_sqrtf(1.0f + b * (float)(n * n)), amp, t60, r, w, s;
        if (second)
            fn *= fm_exp2f(uni * (1.0f / 12.0f));
        if (fn > 9000.0f) {
            nm = m;
            break;
        }
        amp = fm_powf((float)n, -0.7f) / (1.0f + (fn / fc) * (fn / fc));
        if (n == 1)
            amp *= fm_clampf(f0 * (1.0f / 120.0f), 0.35f, 1.0f);
        if (n <= 2)
            amp *= second ? 0.4f : 0.6f;
        t60 = t1 / (1.0f + 0.55f * (float)(n - 1)) * (second ? 2.3f : 1.0f);
        r = fm_expf(-6.9078f / (t60 * EN_SR));
        w = FM_TWO_PI * fn / EN_SR;
        s = fm_sinf(w * 0.5f);
        v->c[m] = 2.0f * r * (1.0f - 2.0f * s * s);
        v->r2[m] = r * r;
        v->y1[m] = a * amp * fm_sinf(w);
        v->y2[m] = 0.0f;
        v->lvl[m] = a * amp;
        v->rb[m] = fm_expf(-6.9078f * (float)BLK / (t60 * EN_SR));
    }
    v->nm = (uint8_t)nm;
    v->env = 0.0f;
    v->env_t = 1.0f;
    v->env_k = 1.0f - tau(0.0012f + 0.005f * felt + 0.003f * (1.0f - vel));
    v->damp_k = 1.0f - tau(fm_clampf(0.05f + 0.25f * (72.0f - (float)note) / 36.0f, 0.05f, 0.3f) * (1.0f + age));
    {                                            /* the piano on the stage's left; low to the left, high right */
        float p = -0.3f * pf(P_WIDTH) + fm_clampf(((float)note - 62.0f) * (1.0f / 40.0f), -0.5f, 0.5f) * pf(P_WIDTH) * 0.6f;
        v->gl = 0.72f * (1.0f - p);
        v->gr = 0.72f * (1.0f + p);
    }
    v->gate = gate;
    v->note = (uint8_t)note;
    v->on = 1;
    v->player = (uint8_t)player;
    v->nodamp = note >= 89;
    v->damped = 0;
    v->cnt = 0;
    knock(vel * (0.10f + 0.3f * felt), 70.0f + (float)(note - 21) * 0.6f, 0.035f, vel * (0.05f + 0.08f * felt), 0.007f,
          900.0f + 1200.0f * (1.0f - felt));
    en_notes_played++;
}
static void key_up(pv_t *v)
{
    v->gate = 0;
    if (!ped && !v->nodamp)
        damp(v, v->damp_k);
}
static void piano_off_player(int note)
{
    int i;
    for (i = 0; i < NV; i++)
        if (pv[i].on && pv[i].player && pv[i].note == note && pv[i].gate < 0)
            key_up(&pv[i]);
}
static void pedal(int down)
{
    int i;
    if (down == ped)
        return;
    ped = down;
    if (!down)
        for (i = 0; i < NV; i++)
            if (pv[i].on && pv[i].gate == 0 && !pv[i].nodamp)
                damp(&pv[i], pv[i].damp_k);
}

/* ---------------------------------------------------------------- bass --- */
/* a plucked string (Karplus-Strong): two strings in turn, the last one damped as the next is plucked */
#define KS_N 2048
static float ks_buf[2][KS_N] EN_POOL;
static struct {
    float d, fb, c, lp, amp, f;
    int32_t gate;
    int w;
    uint8_t on;
} ks[2];
static int ks_cur;
static float bass_lp, bass_hp, bass_sum[BLK];

static void bass_on(int note, float vel, int32_t gate)
{
    int s = ks_cur ^ 1, i, L;
    float f, c, e = 0.0f, d0;
    if (ks[ks_cur].on)                           /* the last note: damped by the fingers */
        ks[ks_cur].fb = fm_expf(-6.9f / (0.05f * ks[ks_cur].f));
    ks_cur = s;
    while (note < 28)
        note += 12;
    while (note > 60)
        note -= 12;
    f = mtof((float)note);
    vel = fm_clampf(vel, 0.05f, 1.0f);
    c = 0.30f + 0.22f * vel;
    ks[s].f = f;
    ks[s].c = c;
    ks[s].d = EN_SR / f - (1.0f - c) / c;
    if (ks[s].d > (float)(KS_N - 4))
        ks[s].d = (float)(KS_N - 4);
    ks[s].fb = fm_expf(-6.9f / (fm_clampf(2.4f - (float)(note - 40) * 0.03f, 1.1f, 3.0f) * f));
    ks[s].gate = gate;
    ks[s].amp = vel;
    ks[s].lp = 0.0f;
    ks[s].on = 1;
    L = (int)ks[s].d + 2;
    d0 = 0.5f / (float)L;
    for (i = 0; i < L; i++) {                    /* the pluck: the finger's flesh, a little grit */
        float bump = sn((float)i * d0) * 0.85f;
        e += (noise() - e) * 0.18f;
        ks_buf[s][(ks[s].w - 1 - i) & (KS_N - 1)] = (bump + e * 0.5f) * vel;
    }
    en_bass_note = (uint8_t)note;
    en_hits[HIT_BASS]++;
}

/* --------------------------------------------------------------- drums --- */
static struct {
    float amp, dec, att, lo, bd, lp;
} brush;
static struct {
    float amp, env, lo, bd, ph[3];
} hat;
static const float HAT_INC[3] = {3270.0f / EN_SR, 4610.0f / EN_SR, 5830.0f / EN_SR};   /* its metal: three squares */
static struct {
    float ph, f, fend, pdec, amp, adec, click;
} kick;
static struct {
    float amp, ph;
} stick;
static float sweep_lvl, sweep_t;                 /* the brushes' circling, and where it is going */
static float beat_len = 1.0f, beat_pos;          /* samples a beat, samples into it (the sweep's circle) */

static void slap(float v, float dec_s)
{
    brush.amp = fm_maxf(brush.amp * 0.5f, v);
    brush.dec = tau(dec_s);
    brush.att = 0.0f;
    en_hits[HIT_SNARE]++;
}
static void hat_foot(float v)
{
    hat.amp = v;
    hat.env = 0.0f;
    en_hits[HIT_HAT]++;
}
static void kick_hit(float v)
{
    kick.ph = 0.0f;
    kick.f = 98.0f;
    kick.fend = 55.0f + 4.0f * frand();
    kick.pdec = tau(0.025f);
    kick.amp = v;
    kick.adec = tau(0.24f);
    kick.click = v * 0.08f;
    en_hits[HIT_KICK]++;
}
static void stick_hit(float v)
{
    stick.amp = v;
    stick.ph = 0.0f;
    en_hits[HIT_SNARE]++;
}

/* --------------------------------------------------------------- harp --- */
/* the harmonica: one reed (or a chord of two or three, tongue-blocked), a pulse and a saw from the same breath,
 * band-limited; the cupped hands a resonant low-pass that opens with the breath and with the hand's flutter; the
 * breath's own noise. A note may bend up into its pitch, shake to the hole above, fall off at its end; a held one
 * gets the throat's vibrato */
enum { HF_SHAKE = 1, HF_FALL = 2, HF_LEG = 4, HF_CHORD = 8, HF_TREM = 16 };
static struct {
    float ph[3], iv[3];
    int nv;
    float cur, tgt, gk;                          /* the pitch now, where it goes, the glide (per sub-block) */
    float env, vel, att, rel;
    int32_t gate, held, len;
    uint8_t on, fl;
    float sh_ph, sh_iv, sh_x, tr_ph;
    float s1, s2, b1, b2, hpz, dc;
} hv;
static float harp_amp;                           /* the night's: the small amp (1) or the cupped hands (0) */

static void harp_on(int note, float vel, int32_t gate, int bend, int fl, int c1, int c2)
{
    int leg = (fl & HF_LEG) && hv.on && hv.gate > 0;
    hv.tgt = (float)note;
    if (!leg) {
        hv.cur = (float)(note - bend);
        hv.env = hv.on ? hv.env * 0.5f : 0.0f;
        hv.held = 0;
        hv.ph[1] = hv.ph[0] + 0.31f;
        hv.ph[2] = hv.ph[0] + 0.57f;
    } else if (bend) {
        hv.cur = (float)(note - bend);
    }
    hv.gk = bend ? (bend >= 2 ? 0.085f : 0.12f) : leg ? 0.35f : 1.0f;   /* a bend: up over ~70-110 ms */
    hv.vel = fm_clampf(vel, 0.05f, 1.0f);
    hv.att = 1.0f - tau(leg ? 0.01f : 0.018f);
    hv.rel = 1.0f - tau(0.045f);
    hv.gate = gate;
    hv.len = gate;
    hv.on = 1;
    hv.fl = (uint8_t)fl;
    hv.nv = 1;
    if (fl & HF_CHORD) {
        hv.iv[1] = (float)c1;
        hv.iv[2] = (float)c2;
        hv.nv = c2 ? 3 : 2;
    }
    hv.iv[0] = 0.0f;
    hv.sh_iv = (float)(c1 ? c1 : 3);
    hv.sh_ph = 0.75f;
    en_harp_note = (uint8_t)note;
    en_harp_on = 1;
    en_hits[HIT_HARP]++;
}

/* --------------------------------------------------------------- room --- */
/* the room's murmur, a glass now and then, applause between the tunes (Ambience) */
#define NCLAP 8
static struct {
    float amp, dec, lo, bd, f, gl, gr;
} clap[NCLAP];
static struct {
    float amp, ph1, ph2, i1, i2, gl, gr;
} clink;
static float appl_lvl, appl_t, murm_lp, murm_hp, murm_ph, clap_next, clink_next, murm_lp2;

/* a small room: four delay lines, a Householder mix, damped; two diffusers before */
#define RV0 1361
#define RV1 1693
#define RV2 2087
#define RV3 2423
#define AP0 223
#define AP1 379
static float rv_buf[RV0 + RV1 + RV2 + RV3 + AP0 + AP1] EN_POOL;
static float *rv_d[4], *ap_d[2];
static const int RV_N[4] = {RV0, RV1, RV2, RV3}, AP_N[2] = {AP0, AP1};
static int rv_p[4], ap_p[2];
static float rv_lp[4];
/* an old upright's (and an old room's) wobble with Age: the bus through a slowly moving delay */
#define WB_N 512
static float wb_l[WB_N] EN_POOL, wb_r[WB_N] EN_POOL;
static int wb_w;
static float wow_ph, flut_ph;

/* -------------------------------------------------------- the schedule --- */
#define NEV 176
enum { E_PIANO, E_BASS, E_HARP, E_KICK, E_SLAP, E_HAT, E_STICK, E_SWEEP, E_CHORD, E_PEDDN, E_PEDUP, E_MARK, E_CLAP,
       E_HARPIN };
typedef struct {
    int32_t wait, gate;
    float vel;
    uint16_t pcs;
    uint8_t type, used, n, fl;
    uint8_t note[3];
    int8_t a;
} ev_t;
static ev_t evq[NEV];
static int ev_n;
static ev_t *ev_new(int type, int32_t wait)
{
    int i;
    for (i = 0; i < NEV; i++)
        if (!evq[i].used) {
            ev_t *e = &evq[i];
            e->used = 1;
            e->type = (uint8_t)type;
            e->wait = wait < 0 ? 0 : wait;
            e->gate = 0;
            e->vel = 0.0f;
            e->pcs = 0;
            e->n = 0;
            e->fl = 0;
            e->a = 0;
            e->note[0] = e->note[1] = e->note[2] = 0;
            ev_n++;
            return e;
        }
    return 0;
}

static void fire(ev_t *e)
{
    int i;
    switch (e->type) {
    case E_PIANO:
        for (i = 0; i < e->n; i++)
            piano_on(e->note[i], e->vel * (i ? 0.92f : 1.0f), e->gate, 0);
        en_rh_note = e->note[e->n - 1];
        en_hits[e->note[0] >= 60 || e->n == 1 ? HIT_RH : HIT_LH]++;
        break;
    case E_BASS: bass_on(e->note[0], e->vel, e->gate); break;
    case E_HARP: harp_on(e->note[0], e->vel, e->gate, e->a, e->fl, e->note[1] ? e->note[1] - 64 : 0,
                         e->note[2] ? e->note[2] - 64 : 0); break;
    case E_KICK: kick_hit(e->vel); break;
    case E_SLAP: slap(e->vel, e->a ? 0.03f : 0.11f); break;   /* a: a tap (short) */
    case E_HAT: hat_foot(e->vel); break;
    case E_STICK: stick_hit(e->vel); break;
    case E_SWEEP: sweep_t = e->vel; break;
    case E_CHORD:
        en_chord_pcs = e->pcs;
        en_chord_root = e->note[0];
        en_chord_q = e->note[1];
        break;
    case E_PEDDN: pedal(1); break;
    case E_PEDUP: pedal(0); break;
    case E_MARK:
        en_section = e->note[0];
        en_bar = e->note[1];
        en_chorus = e->note[2];
        en_beat_no = 0;
        beat_pos = 0.0f;
        break;
    case E_CLAP: appl_t = e->vel; break;
    case E_HARPIN: en_harp_in = (uint8_t)e->a; break;
    default: break;
    }
}

static float bar_left;                           /* samples to the next bar */
static int gap_left, ring_left;                  /* the last chord's ringing, the break after: samples */
static int skipping;

#include "juke_band.c"

/* ------------------------------------------------------------ control --- */

static void quiet(void)
{
    int i;
    for (i = 0; i < NV; i++)
        pv[i].on = 0;
    for (i = 0; i < NEV; i++)
        evq[i].used = 0;
    ev_n = 0;
    pedal(0);
    mech.kamp = mech.namp = 0.0f;
    ks[0].on = ks[1].on = 0;
    hv.on = 0;
    hv.env = 0.0f;
    en_harp_on = 0;
    brush.amp = kick.amp = hat.amp = stick.amp = 0.0f;
    sweep_lvl = sweep_t = 0.0f;
    appl_t = 0.0f;
}

static void play_on(int on)
{
    int i;
    if (on && !en_playing) {
        en_playing = 1;
    } else if (!on && en_playing) {              /* resting: nothing new; the keys up, the band's hands off */
        en_playing = 0;
        for (i = 0; i < NEV; i++)
            evq[i].used = 0;
        ev_n = 0;
        for (i = 0; i < NV; i++)
            if (pv[i].on && !pv[i].player) {
                pv[i].gate = 0;
                damp(&pv[i], 1.0f - tau(0.25f));
            }
        pedal(0);
        for (i = 0; i < 2; i++)
            if (ks[i].on)
                ks[i].fb = fm_minf(ks[i].fb, fm_expf(-6.9f / (0.15f * ks[i].f)));
        hv.gate = 0;
        sweep_t = 0.0f;
        appl_t = 0.0f;
        bar_left = 0.0f;
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
                if (a == P_TEMPO)
                    tempo_set();
            }
            break;
        case C_STYLE: style_i = a % EN_NSTYLES; break;
        case C_PLAY: play_on(a); break;
        case C_SKIP:
            if (en_phase != PH_GAP && !skipping) {
                skipping = 1;
                fade_t = 0.0f;
                fade_rate = 1.0f / (1.5f * EN_SR);
            }
            break;
        case C_REPEAT: en_repeat_on = (uint8_t)(a != 0); break;
        case C_TRACK_SEED_HI: en_seed_base = (en_seed_base & 0xFFFFu) | ub << 16; break;
        case C_TRACK_SEED_LO: en_seed_base = (en_seed_base & 0xFFFF0000u) | ub; break;
        case C_TRACK_NO:
            en_track_no = (uint32_t)a << 16 | ub;
            quiet();
            last_kind = -1;                      /* a set begins: its first tune the night's own kind */
            new_tune();
            bar_left = EN_SR * 0.5f;
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
    for (i = 0; i < 2; i++) {
        ap_d[i] = p;
        p += AP_N[i];
        ap_p[i] = 0;
    }
    for (i = 0; i < (int)(sizeof rv_buf / sizeof rv_buf[0]); i++)
        rv_buf[i] = 0.0f;
    for (i = 0; i < WB_N; i++)
        wb_l[i] = wb_r[i] = 0.0f;
    for (i = 0; i < KS_N; i++)
        ks_buf[0][i] = ks_buf[1][i] = 0.0f;
    ped = 0;
    quiet();
    en_harp_in = 0;
    appl_lvl = 0.0f;
    style_i = 0;
    skipping = 0;
    fade = fade_t = 1.0f;
    gap_left = ring_left = 0;
    en_track_no = 0;
    en_tracks_played = 0;
    en_notes_played = 0;
    new_tune();
    bar_left = EN_SR * 0.3f;
}

/* -------------------------------------------------------------- render --- */
static float hp_l, hp_r, lev, hiss_lp, hiss_hp;
static float vu_acc_l, vu_acc_r;
static uint32_t vu_n;

static void render_piano(uint32_t n)
{
    int i, m;
    uint32_t j;
    float lv = 0.30f * lvl(P_PIANO) * fade;
    int voices = 0;
    for (i = 0; i < NV; i++) {
        pv_t *v = &pv[i];
        float env, et, ek, gl, gr, pl = v->player ? 0.30f / fm_maxf(lv, 1e-4f) * fade : 1.0f;
        if (!v->on)
            continue;
        voices++;
        for (j = 0; j < n; j++)
            vb[j] = 0.0f;
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
        env = v->env, et = v->env_t, ek = v->env_k, gl = v->gl * lv * pl, gr = v->gr * lv * pl;
        for (j = 0; j < n; j++) {
            float s;
            env += (et - env) * ek;
            s = vb[j] * env;
            bufl[j] += s * gl;
            bufr[j] += s * gr;
            rsend[j] += s * (gl + gr) * 0.35f;
        }
        v->env = env;
        if (v->gate > 0) {
            v->gate -= (int32_t)n;
            if (v->gate <= 0)
                key_up(v);
        }
        v->cnt += n;
        while (v->cnt >= BLK) {
            v->cnt -= BLK;
            for (m = 0; m < v->nm; m++)
                v->lvl[m] *= v->rb[m];
        }
        while (v->nm && v->lvl[v->nm - 1] * fm_maxf(env, et) < 5e-5f)
            v->nm--;
        if (!v->nm || (et == 0.0f && env < 5e-5f))
            v->on = 0;
    }
    en_voices = (uint8_t)voices;
    if (mech.kamp > 1e-5f || mech.namp > 1e-5f) {   /* the hammers' knock, the keys */
        float g = 0.5f * lv;
        for (j = 0; j < n; j++) {
            float k = sn(mech.kph) * mech.kamp;
            mech.kph = wrap1(mech.kph + mech.kinc);
            mech.kamp *= mech.kdec;
            mech.nlp += (noise() * mech.namp - mech.nlp) * mech.na;
            mech.namp *= mech.ndec;
            k = (k + mech.nlp) * g;
            bufl[j] += k * 1.2f;
            bufr[j] += k * 0.8f;
        }
    }
}

static void render_bass(uint32_t n)
{
    uint32_t j;
    int s;
    float lv = 0.62f * lvl(P_BASS) * fade, a = onepole(1000.0f), hpa = onepole(36.0f);
    for (s = 0; s < 2; s++) {
        float c, fb, d, lp;
        int w;
        if (!ks[s].on)
            continue;
        if (ks[s].gate > 0) {
            ks[s].gate -= (int32_t)n;
            if (ks[s].gate <= 0)                 /* the note's end: the finger lifts */
                ks[s].fb = fm_minf(ks[s].fb, fm_expf(-6.9f / (0.10f * ks[s].f)));
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
        bufl[j] += y * 0.97f;
        bufr[j] += y * 1.03f;
        rsend[j] += y * 0.1f;
        bass_sum[j] = 0.0f;
    }
}

static void render_drums(uint32_t n)
{
    uint32_t j;
    float lv = 0.42f * lvl(P_DRUMS) * fade, w = pf(P_WIDTH);
    float fh = 2.0f * fm_sinf(FM_PI * 3600.0f / EN_SR), fb = 2.0f * fm_sinf(FM_PI * 3000.0f / EN_SR);
    float hpa = onepole(3000.0f), blp = onepole(5000.0f), hdec = tau(0.03f);
    float ph = beat_len > 1.0f ? fm_clampf(beat_pos / beat_len, 0.0f, 1.0f) : 0.0f, x = sn(ph * 0.5f);
    float sw;
    sweep_lvl += (sweep_t - sweep_lvl) * fm_minf((float)n / (EN_SR * 0.3f), 1.0f);
    sw = sweep_lvl * (0.3f + 0.7f * x * x) * fade;   /* the circle: louder as the brush comes round */
    if (brush.amp < 1e-4f && sw < 1e-4f && hat.amp < 1e-4f && kick.amp < 1e-4f && stick.amp < 1e-4f)
        return;
    for (j = 0; j < n; j++) {
        float k = 0.0f, br = 0.0f, t = 0.0f;
        if (brush.amp > 1e-4f || sw > 1e-4f) {   /* brushes: a slap, and the sweep */
            float y = noise(), hp, a;
            brush.att = fm_minf(brush.att + 1.0f / (0.003f * EN_SR), 1.0f);
            a = brush.amp * brush.att;
            brush.lo += fb * brush.bd;
            hp = y - brush.lo - 1.1f * brush.bd;
            brush.bd += fb * hp;
            brush.lp += (hp * a + brush.bd * sw * 0.7f - brush.lp) * blp;
            br = brush.lp;
            brush.amp *= brush.dec;
        }
        if (hat.amp > 1e-4f) {                   /* the foot: a chick, its band of noise and a little metal */
            float m = 0.0f, y, hp;
            int i;
            for (i = 0; i < 3; i++) {
                hat.ph[i] = wrap1(hat.ph[i] + HAT_INC[i]);
                m += hat.ph[i] < 0.5f ? 0.07f : -0.07f;
            }
            y = noise() * 0.7f + m;
            hat.env = fm_minf(hat.env + 1.0f / (0.002f * EN_SR), 1.0f);
            hat.lo += fh * hat.bd;
            hp = y - hat.lo - 0.8f * hat.bd;
            hat.bd += fh * hp;
            t += (hat.bd * 0.5f + hp * 0.25f) * hat.amp * hat.env * 0.6f;
            hat.amp *= hdec;
            (void)hpa;
        }
        if (kick.amp > 1e-4f) {
            kick.f = kick.fend + (kick.f - kick.fend) * kick.pdec;
            kick.ph = wrap1(kick.ph + kick.f / EN_SR);
            k = sn(kick.ph) * kick.amp + noise() * kick.click;
            kick.amp *= kick.adec;
            kick.click *= 0.9f;
        }
        if (stick.amp > 1e-4f) {                 /* the brushes' handles clicked together: the count */
            t += (sn(stick.ph) * 0.6f + noise() * 0.4f) * stick.amp * 0.5f;
            stick.ph = wrap1(stick.ph + 2250.0f / EN_SR);
            stick.amp *= 0.9962f;
        }
        /* the kit, on the stage's right */
        bufl[j] += (k * 0.8f + br * (0.9f - 0.3f * w) + t * (0.8f - 0.3f * w)) * lv;
        bufr[j] += (k * 0.8f + br * (0.9f + 0.3f * w) + t * (0.8f + 0.4f * w)) * lv;
        rsend[j] += (br * 0.35f + t * 0.3f + k * 0.06f) * lv;
    }
}

/* the harmonica, a sub-block at a time for the pitch (32 samples) */
static void render_harp(uint32_t n)
{
    uint32_t j0, j;
    float lv = 0.26f * fade * (0.8f + 0.4f * harp_amp);
    if (!hv.on)
        return;
    for (j0 = 0; j0 < n; j0 += 32) {
        uint32_t m = n - j0 < 32 ? n - j0 : 32;
        float p, inc[3], fc, f, et, vib, held_s = (float)hv.held * (1.0f / EN_SR), wah;
        int k;
        hv.cur += (hv.tgt - hv.cur) * hv.gk;
        p = hv.cur;
        vib = fm_clampf((held_s - 0.28f) * 2.2f, 0.0f, 1.0f);   /* a held note: the throat's vibrato */
        hv.tr_ph = wrap1(hv.tr_ph + 5.3f * (float)m / EN_SR);
        p += vib * 0.13f * sn(hv.tr_ph);
        if (hv.fl & HF_SHAKE) {                  /* the shake: the head rocking across two holes */
            float s;
            hv.sh_ph = wrap1(hv.sh_ph + 6.6f * (float)m / EN_SR);
            s = fm_clampf(sn(hv.sh_ph) * 2.5f, -1.0f, 1.0f) * 0.5f + 0.5f;
            hv.sh_x += (s - hv.sh_x) * 0.5f;
            p += hv.sh_x * hv.sh_iv;
        }
        if ((hv.fl & HF_FALL) && hv.gate > 0 && hv.gate < (int32_t)(0.14f * EN_SR)) {
            float r = 1.0f - (float)hv.gate / (0.14f * EN_SR);
            p -= 2.6f * r * r;                   /* a fall off the end */
        }
        for (k = 0; k < hv.nv; k++)
            inc[k] = mtof(p + hv.iv[k]) * (1.0f / EN_SR);
        et = hv.gate > 0 ? hv.vel : 0.0f;
        wah = 1.0f + vib * 0.22f * sn(hv.tr_ph + 0.25f) + ((hv.fl & HF_TREM) ? 0.45f * sn(hv.tr_ph * 1.0f) : 0.0f);
        fc = (harp_amp > 0.5f ? 1500.0f : 2100.0f) * (0.45f + 0.75f * hv.env) * wah;
        f = 2.0f * fm_sinf(FM_PI * fm_clampf(fc, 200.0f, 7000.0f) / EN_SR);
        for (j = j0; j < j0 + m; j++) {
            float x = 0.0f, y, br;
            for (k = 0; k < hv.nv; k++) {        /* a pulse (the reed's slot) and a saw, PolyBLEP'd */
                float ph = hv.ph[k], dt = inc[k], q2, s1, s2, t;
                ph += dt;
                if (ph >= 1.0f)
                    ph -= 1.0f;
                hv.ph[k] = ph;
                s1 = 2.0f * ph - 1.0f;
                if (ph < dt) {
                    t = ph / dt;
                    s1 -= t + t - t * t - 1.0f;
                } else if (ph > 1.0f - dt) {
                    t = (ph - 1.0f) / dt;
                    s1 -= t * t + t + t + 1.0f;
                }
                q2 = ph + 0.3f;
                if (q2 >= 1.0f)
                    q2 -= 1.0f;
                s2 = 2.0f * q2 - 1.0f;
                if (q2 < dt) {
                    t = q2 / dt;
                    s2 -= t + t - t * t - 1.0f;
                } else if (q2 > 1.0f - dt) {
                    t = (q2 - 1.0f) / dt;
                    s2 -= t * t + t + t + 1.0f;
                }
                x += (s1 - s2) * 0.55f + s1 * 0.25f;
            }
            if (hv.nv > 1)
                x *= hv.nv == 3 ? 0.55f : 0.7f;
            hv.env += (et - hv.env) * (et > hv.env ? hv.att : hv.rel);
            br = noise() * 0.10f;                /* the breath */
            hv.b1 += (br - hv.b1) * 0.25f;
            x = x * hv.env + (br - hv.b1) * hv.env;
            hv.s1 += f * hv.s2;                  /* the hands: a resonant low-pass, a little of its band */
            y = x - hv.s1 - 0.75f * hv.s2;
            hv.s2 += f * y;
            y = hv.s1 + 0.55f * hv.s2;
            if (harp_amp > 0.5f) {               /* a bullet mic into a small amp: thin, pushed */
                hv.hpz += (y - hv.hpz) * 0.035f;
                y = fm_tanhf((y - hv.hpz) * 2.6f) * 0.55f;
            }
            hv.dc += (y - hv.dc) * 0.002f;
            y = (y - hv.dc) * lv;
            bufl[j] += y * 1.02f;
            bufr[j] += y * 0.98f;
            rsend[j] += y * 0.45f;
        }
        hv.held += (int32_t)m;
        if (hv.gate > 0) {
            hv.gate -= (int32_t)m;
            if (hv.gate <= 0) {
                hv.gate = 0;
                en_harp_on = 0;
            }
        } else if (hv.env < 1e-4f) {
            hv.on = 0;
            en_harp_on = 0;
        }
    }
}

static void render_crowd(uint32_t n)
{
    uint32_t j;
    int i;
    float am = pf(P_AMBI), lv = am * 0.22f, ml, fc;
    appl_lvl += (appl_t - appl_lvl) * fm_minf((float)n / (EN_SR * (appl_t > appl_lvl ? 0.25f : 1.6f)), 1.0f);
    en_applause = appl_lvl * (am > 0.0f ? 1.0f : 0.0f);
    if (am <= 0.0f || !en_playing)
        return;
    /* the room talks low under the music, louder between tunes */
    ml = am * (en_phase == PH_GAP ? 0.020f : 0.009f) * (0.7f + 0.3f * sn(murm_ph));
    murm_ph = wrap1(murm_ph + 0.11f * (float)n / EN_SR);
    fc = onepole(480.0f);
    for (j = 0; j < n; j++) {
        float x = noise(), y;
        murm_lp += (x - murm_lp) * fc;
        murm_lp2 += (murm_lp - murm_lp2) * fc;
        murm_hp += (murm_lp2 - murm_hp) * 0.015f;
        y = (murm_lp2 - murm_hp) * ml;
        bufl[j] += y;
        bufr[j] += y * 0.85f;
        rsend[j] += y * 0.5f;
    }
    /* applause: claps, each a burst of noise in its own band, at the room's rate (a small room: not many) */
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
        clap_next += EN_SR / (6.0f + 22.0f * appl_lvl) * (0.3f + 1.4f * frand());
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
    /* a glass set down, a bottle against one */
    clink_next -= (float)n;
    if (clink_next <= 0.0f) {
        float p = frand() * 1.4f - 0.7f;
        clink.amp = (0.3f + 0.4f * frand()) * (en_phase == PH_GAP ? 1.0f : 0.5f);
        clink.i1 = (2400.0f + 1100.0f * frand()) / EN_SR;
        clink.i2 = clink.i1 * (1.48f + 0.14f * frand());
        clink.gl = 0.75f - 0.5f * p;
        clink.gr = 0.75f + 0.5f * p;
        clink_next = EN_SR * (en_phase == PH_GAP ? 2.0f + 4.0f * frand() : 9.0f + 16.0f * frand());
    }
    if (clink.amp > 1e-4f) {
        float d = tau(0.2f);
        for (j = 0; j < n; j++) {
            float y = (sn(clink.ph1) + 0.5f * sn(clink.ph2)) * clink.amp * lv * 0.07f;
            clink.ph1 = wrap1(clink.ph1 + clink.i1);
            clink.ph2 = wrap1(clink.ph2 + clink.i2);
            clink.amp *= d;
            bufl[j] += y * clink.gl;
            bufr[j] += y * clink.gr;
            rsend[j] += y * 0.6f;
        }
    }
}

static void render_room(uint32_t n)
{
    uint32_t j;
    float room = pf(P_ROOM), send = 0.18f + 0.55f * room, g = 0.52f + 0.38f * room, damp = 0.32f;
    float wet = 0.3f + 0.5f * room;
    for (j = 0; j < n; j++) {
        float in = rsend[j] * send, x[4], sum, yl, yr;
        int i;
        for (i = 0; i < 2; i++) {
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
        bufl[j] += yl * wet;
        bufr[j] += yr * wet;
        rsend[j] = 0.0f;
    }
}

static void render_bus(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    float age = pf(P_AGE), wow = age * age * 5.0f, flut = age * 0.5f, hp_a = onepole(28.0f);
    float lv_rel = tau(0.5f), lv_att = 1.0f - tau(0.003f), hiss = age * 0.0016f;
    for (j = 0; j < n; j++) {
        float l = bufl[j], r = bufr[j], x, vol;
        if (wow > 0.02f) {                       /* an old room's sound: a slow wobble, a little hiss */
            float d, rd, fr, wl, wr;
            int ri;
            wb_l[wb_w] = l;
            wb_r[wb_w] = r;
            d = 24.0f + wow * (1.0f + sn(wow_ph)) + flut * sn(flut_ph);
            wow_ph = wrap1(wow_ph + 0.4f / EN_SR);
            flut_ph = wrap1(flut_ph + 5.1f / EN_SR);
            rd = (float)wb_w - d;
            if (rd < 0.0f)
                rd += (float)WB_N;
            ri = (int)rd;
            fr = rd - (float)ri;
            wl = wb_l[ri & (WB_N - 1)] + (wb_l[(ri + 1) & (WB_N - 1)] - wb_l[ri & (WB_N - 1)]) * fr;
            wr = wb_r[ri & (WB_N - 1)] + (wb_r[(ri + 1) & (WB_N - 1)] - wb_r[ri & (WB_N - 1)]) * fr;
            wb_w = (wb_w + 1) & (WB_N - 1);
            l = wl;
            r = wr;
            x = noise();
            hiss_lp += (x - hiss_lp) * 0.3f;
            hiss_hp += (hiss_lp - hiss_hp) * 0.05f;
            l += (hiss_lp - hiss_hp) * hiss * (en_playing ? 1.0f : 0.0f);
            r += (hiss_lp - hiss_hp) * hiss * 0.95f * (en_playing ? 1.0f : 0.0f);
        }
        fade = fade < fade_t ? fm_minf(fade + fade_rate, fade_t) : fm_maxf(fade - fade_rate, fade_t);
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
        out[2 * j] = (int32_t)(fm_tanhf(l) * 7400000.0f);   /* tanh: never past -1.1 dBFS */
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 7400000.0f);
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
    drain();
    while (n) {
        uint32_t k = n > BLK ? BLK : n, j;
        int i, seq = 0;
        if (en_playing) {
            if (en_phase == PH_GAP) {
                if (gap_left <= 0) {
                    fade = fade_t = 1.0f;
                    new_tune();
                    bar_left = EN_SR * 0.2f;
                } else if ((int)k > gap_left) {
                    k = (uint32_t)gap_left;
                }
            } else if (skipping && fade < 0.002f) {   /* skipped: faded out, a short break */
                quiet();
                skipping = 0;
                en_phase = PH_GAP;
                en_section = S_BREAK;
                en_harp_in = 0;
                gap_left = (int)(EN_SR * 1.5f);
            } else if (en_phase == PH_END && ring_left > 0 && pc.bar >= pc.nbars) {
                if (ring_left <= (int)k) {       /* the last chord has rung: hands off, applause, the break */
                    for (i = 0; i < NV; i++)
                        if (pv[i].on && !pv[i].player) {
                            pv[i].gate = 0;
                            damp(&pv[i], 1.0f - tau(0.4f));
                        }
                    pedal(0);
                    for (i = 0; i < 2; i++)
                        if (ks[i].on)
                            ks[i].fb = fm_minf(ks[i].fb, fm_expf(-6.9f / (0.3f * ks[i].f)));
                    hv.gate = 0;
                    sweep_t = 0.0f;
                    ring_left = 0;
                    en_phase = PH_GAP;
                    en_section = S_BREAK;
                    appl_t = 0.55f + 0.35f * pf(P_DRIVE);
                    gap_left = (int)(EN_SR * (5.5f + 2.0f * frand()));
                } else {
                    ring_left -= (int)k;
                }
            } else {
                if (bar_left <= 0.0f)
                    compose();
                if (en_phase != PH_GAP && !(en_phase == PH_END && ring_left > 0)) {
                    seq = 1;
                    if ((float)k > bar_left)
                        k = (uint32_t)bar_left + 1u;
                }
            }
            if (en_phase == PH_GAP && gap_left < (int)(EN_SR * 3.5f))
                appl_t = 0.0f;                   /* the clapping dies down; the harp player wanders off */
            if (en_phase == PH_GAP && gap_left < (int)(EN_SR * 3.0f))
                en_harp_in = 0;
        }
        for (i = 0; i < NEV; i++)                /* events due now fire; the chunk ends at the next one */
            if (evq[i].used) {
                if (evq[i].wait <= 0) {
                    fire(&evq[i]);
                    evq[i].used = 0;
                    ev_n--;
                } else if ((uint32_t)evq[i].wait < k) {
                    k = (uint32_t)evq[i].wait;
                }
            }
        if (k > n)
            k = n;
        if (k == 0)
            k = 1;
        for (i = 0; i < NEV; i++)
            if (evq[i].used)
                evq[i].wait -= (int32_t)k;
        if (seq)
            bar_left -= (float)k;
        if (en_playing && en_phase == PH_GAP)
            gap_left -= (int)k;
        beat_pos += (float)k;                    /* the beat: for the sweep and the screen */
        if (beat_pos >= beat_len && en_beat_no < 3) {
            beat_pos -= beat_len;
            en_beat_no++;
        }
        en_beat = fm_clampf(beat_pos / beat_len, 0.0f, 1.0f);
        clock_ms(k);
        if (en_playing && en_phase != PH_GAP)    /* the tune's time: held while resting */
            en_track_ms = en_now_ms - pstart;
        else
            pstart = en_now_ms - en_track_ms;
        for (j = 0; j < k; j++)
            bufl[j] = bufr[j] = rsend[j] = 0.0f;
        render_piano(k);
        render_bass(k);
        render_drums(k);
        render_harp(k);
        render_crowd(k);
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
