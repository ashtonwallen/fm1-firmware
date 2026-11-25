/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * DEEP HOUSE DECKS: ashtonwallen, 2026 */
/* The instrument (house.h). One compilation unit, -O2. The command queue and the render's shape are AMB-1's (by way
 * of LOFI RADIO); the voice leading and the electric piano are LOFI RADIO's. */
#include "house.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

#ifdef EN_HOST
void (*en_log)(int deck, uint32_t step, char kind, int note, int len, float vel);
#define LOG(d, k, n, l, v)                                                                                         \
    do {                                                                                                           \
        if (en_log)                                                                                                \
            en_log((d), gstep, (k), (n), (l), (v));                                                                \
    } while (0)
#else
#define LOG(d, k, n, l, v) ((void)0)
#endif

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const char *const EN_SECT_NAME[S_NSECT] = {"", "INTRO", "GROOVE", "FULL", "BREAK", "BUILD", "OUTRO", "OUTRO"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Energy", 0, 100, 60}, {"Warmth", 0, 100, 50}, {"Tempo", 116, 126, 121}, {"Swing", 0, 100, 40},
    {"Mood", 0, 100, 50},   {"Space", 0, 100, 45},  {"Chords", 0, 100, 70},   {"Drums", 0, 100, 70},
    {"Bass", 0, 100, 70},   {"Lead", 0, 100, 50},   {"Filter", -50, 50, 0},   {"Mix", 0, 2, 1},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }
static const uint8_t MIX_BARS[3] = {16, 32, 64};

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
    if (i == P_SWING) {                              /* as a drum machine's: 50 % straight .. 66 % a triplet's */
        put_s(put_u(buf, (uint32_t)(50 + (v * 16 + 50) / 100)), "%");
    } else if (i == P_MOOD) {
        put_s(buf, v < 20 ? "Soulful" : v < 40 ? "Warm" : v < 60 ? "Deep" : v < 80 ? "Dark" : "Inky");
    } else if (i == P_FILTER) {
        if (v == 0)
            put_s(buf, "Open");
        else
            put_u(put_s(buf, v < 0 ? "LP " : "HP "), (uint32_t)(v < 0 ? -v : v) * 2u);
    } else if (i == P_MIX) {
        put_s(put_u(buf, MIX_BARS[v < 0 ? 0 : v > 2 ? 2 : v]), " bars");
    } else {
        put_u(buf, (uint32_t)(v < 0 ? 0 : v));
    }
}

/* ------------------------------------------------------------ harmony --- */
enum { Q_M7, Q_M9, Q_M11, Q_MAJ7, Q_MAJ9, Q_DOM9, Q_SUS9, Q_69, Q_NQ };
static const char *const Q_NAME[Q_NQ] = {"m7", "m9", "m11", "maj7", "maj9", "9", "9sus", "6/9"};
/* the stab's four notes above the chord's root (the bass has the root): m9 and maj9 rootless, m11 the b3 b7 9 11
 * stack, 9sus the 11 b7 9 5 */
static const int8_t Q_VOICE[Q_NQ][4] = {
    {0, 3, 7, 10}, {3, 7, 10, 14}, {3, 10, 14, 17}, {0, 4, 7, 11},
    {4, 7, 11, 14}, {4, 10, 14, 19}, {5, 10, 14, 19}, {4, 9, 14, 19},
};
/* the chord's tones for a melody: root, third, fifth, seventh, ninth (the sus's fourth for its third, the 6/9's
 * sixth for its seventh) */
static const int8_t Q_DEG[Q_NQ][5] = {
    {0, 3, 7, 10, 14}, {0, 3, 7, 10, 14}, {0, 3, 7, 10, 14}, {0, 4, 7, 11, 14},
    {0, 4, 7, 11, 14}, {0, 4, 7, 10, 14}, {0, 5, 7, 10, 14}, {0, 4, 7, 9, 14},
};
const char *en_chord_name(int root, int q, char *buf)
{
    put_s(put_s(buf, EN_NOTE_NAME[(root % 12 + 12) % 12]), Q_NAME[q < 0 || q >= Q_NQ ? 0 : q]);
    return buf;
}

/* the vamps: deep house's two- and four-chord loops, curated. Roots in semitones above the key's minor tonic (the
 * set's keys are all minor: the wheel's A side; a vamp in the relative major is written from the minor's tonic),
 * each chord one or two bars. mood: 0 soulful .. 100 dark */
typedef struct {
    int8_t root, q, bars;
} vch_t;
typedef struct {
    uint8_t mood, n;
    vch_t c[4];
} vamp_t;
static const vamp_t VAMPS[] = {
    {50, 2, {{0, Q_M9, 2}, {5, Q_M9, 2}}},                                  /*  0 i9 iv9 */
    {58, 2, {{0, Q_M11, 1}, {5, Q_M9, 1}}},                                 /*  1 i11 iv9, a bar each */
    {32, 2, {{0, Q_M9, 2}, {5, Q_DOM9, 2}}},                                /*  2 i9 IV9: the dorian lift */
    {45, 2, {{0, Q_M9, 2}, {8, Q_MAJ9, 2}}},                                /*  3 i9 bVImaj9 */
    {28, 2, {{0, Q_M9, 2}, {3, Q_MAJ9, 2}}},                                /*  4 i9 bIIImaj9 */
    {66, 2, {{0, Q_M11, 2}, {10, Q_M11, 2}}},                               /*  5 i11 bvii11: Kerri's planing */
    {50, 3, {{0, Q_M11, 1}, {10, Q_SUS9, 1}, {8, Q_MAJ9, 2}}},              /*  6 i11 bVII9sus bVImaj9: Kerri's sus */
    {22, 2, {{3, Q_MAJ9, 2}, {8, Q_M9, 2}}},                                /*  7 Imaj9 ivm9: Larry Heard's borrowed iv */
    {35, 4, {{8, Q_MAJ9, 1}, {7, Q_M7, 1}, {5, Q_M9, 1}, {3, Q_MAJ9, 1}}},  /*  8 the Heard descent */
    {12, 4, {{5, Q_M9, 1}, {10, Q_DOM9, 1}, {3, Q_MAJ9, 1}, {0, Q_M9, 1}}}, /*  9 the soul ii V I vi */
    {55, 4, {{0, Q_M9, 1}, {8, Q_MAJ9, 1}, {10, Q_SUS9, 1}, {7, Q_M7, 1}}}, /* 10 i9 bVImaj9 bVII9sus v7 */
    {92, 2, {{0, Q_M11, 2}, {0, Q_M9, 2}}},                                 /* 11 the dub: one chord breathing */
    {75, 2, {{0, Q_M9, 2}, {7, Q_M7, 2}}},                                  /* 12 i9 v7 */
    {82, 3, {{0, Q_M11, 2}, {8, Q_MAJ7, 1}, {7, Q_M7, 1}}},                 /* 13 i11 bVImaj7 v7: after hours */
    {8, 2, {{3, Q_MAJ9, 2}, {8, Q_MAJ9, 2}}},                               /* 14 Imaj9 IVmaj9: bright */
    {62, 2, {{0, Q_M7, 1}, {5, Q_M7, 1}}},                                  /* 15 i7 iv7: the organ's */
    {42, 2, {{0, Q_M9, 2}, {10, Q_69, 2}}},                                 /* 16 i9 bVII6/9 */
};
#define NVAMPS (int)(sizeof VAMPS / sizeof VAMPS[0])

/* the chords' rhythm: two bars of sixteenths. X a stab, x a soft one, P a push (the next bar's chord an eighth
 * early), - held, . rest */
static const char *const COMPS[] = {
    "X-----------..P---------------..",   /* 0 held, pushed into the next */
    "X-.X-.X-......x-X-.X-.X-........",   /* 1 the 3-3-2 stab */
    "..x-..x-..x-..x-..x-..x-..x-..x-",   /* 2 offbeat (organ) */
    "..X-......X-......X-.......x-...",   /* 3 dub: offbeat stabs into the echo */
    "X---------------x---------------",   /* 4 a chord a bar (pads) */
    "X--.x-....x-....X--.x-......P---",   /* 5 bounce, pushed */
};
#define NCOMPS (int)(sizeof COMPS / sizeof COMPS[0])
static const char BREAK_COMP[] = "X-------------..x-------------..";   /* the breakdown: long chords */

/* the bass: a bar. R root, O its octave, F its fifth, A a step into the next bar's root, - held, . rest; the kick
 * is on the beats */
static const char *const BPATS[] = {
    "..R-..R-..R-..R-",   /* 0 offbeat */
    "R-.R-.R-..O-.R-.",   /* 1 rolling 3-3-2 */
    "..R-..O-..R-..FA",   /* 2 offbeat, octave, and in */
    "R--.......R-.R-.",   /* 3 deep and sparse */
    "..RR..R-..RR..A-",   /* 4 garage doubles */
};
#define NBPATS (int)(sizeof BPATS / sizeof BPATS[0])

/* closed hats (the open ones are on the offbeats): x, s soft */
static const char *const HATS[] = {
    ".s.s.s.s.s.s.s.s",   /* 0 the shuffled sixteenths between */
    "xs.sxs.sxs.sxs.s",   /* 1 busier */
    "x..sx..sx..sx..s",   /* 2 the beat and its "a" */
    "...s...s...s.s.s",   /* 3 sparse */
};
#define NHATS (int)(sizeof HATS / sizeof HATS[0])

/* percussion: two bars each. Shaker x, s soft; conga h high, l low, m muted; rim x; clave x */
enum { PC_NONE, PC_SHAKER, PC_CONGA, PC_RIM, PC_CLAVE };
static const char *const PERC[5][2] = {
    {0, 0},
    {"ssxsssxsssxsssxsssxsssxsssxsssxs", "s.x.s.x.s.x.s.x.s.x.s.x.s.x.s.xs"},
    {"..h...h.l..h.l....h...h.l.h..l..", "h..l..h...h.m...h..l..h...h.ll.."},
    {"...x..x....x..x....x..x.....x...", "...x.....x..x......x.....x..x..."},
    {"x.....x.....x.......x...x.......", "....x...x.......x.....x.....x..."},
};

/* the lead's hooks: four bars, the rhythm of two (x a note, - held) twice: first with contour A, then B, the
 * answer, ending home. Contours: the chord's tones (0 root, 1 third, 2 fifth, 3 seventh, 4 ninth) */
typedef struct {
    const char *r;
    int8_t a[8], b[8];
} hook_t;
static const hook_t HOOKS[] = {
    {"x-.x-.x---......................", {2, 1, 2}, {2, 1, 0}},
    {"..x-x-..x-----....x-.x-.x-------", {1, 2, 4, 2, 1, 2}, {1, 2, 4, 2, 1, 0}},
    {"x---..x-x---....x---..x-x---....", {2, 3, 2, 1, 2, 1}, {2, 3, 2, 1, 1, 0}},
    {"......x-x-.x----................", {0, 1, 2}, {2, 1, 0}},
    {"x-..x-..x-x-----......x-x-x-----", {4, 2, 1, 2, 1, 2, 1}, {4, 2, 1, 2, 2, 1, 0}},
};
#define NHOOKS (int)(sizeof HOOKS / sizeof HOOKS[0])
/* the vocal chops: two bars, a vowel each (a o e i u) */
typedef struct {
    const char *r, *vow;
} vox_t;
static const vox_t VOXES[] = {
    {"..x-......x-......x-......x-....", "aoao"},
    {"......x-x-..x---................", "ooa"},
    {"x-------....x-..............x-x-", "aaoa"},
    {"..x.x...x-........x.x...x-....x.", "eeaeeao"},
};
#define NVOXES (int)(sizeof VOXES / sizeof VOXES[0])

/* ------------------------------------------------------------- sets --- */
/* the user's ten (Energy Warmth Tempo Swing Mood Space, Chords Drums Bass Lead) and the set's own sound: keys
 * 0 Rhodes, 1 organ, 2 pad stab; pad 0 none, 1 in breakdowns, 2 also in the full sections; kit 0 clap, 1 snare
 * and clap, 2 rim; p1, p2 the percussion (PC_*); bass 0 sub, 1 round, 2 organ; leads bit 0 a hook, bit 1 a vocal;
 * lead_p: how many of its records have one (%); echo: the chords' dub echo; vamps, comps, bpats, hats: which */
typedef struct {
    const char *name;
    int8_t v[P_NSTYLE];
    uint8_t keys, pad, kit, p1, p2, bass, leads, lead_p, echo;
    uint32_t vamps;
    uint8_t comps, bpats, hats;
} style_t;
#define B(n) (1u << (n))
static const style_t STYLES[EN_NSTYLES] = {
    /*                  Ene War Tem Swi Moo Spa  Chd Drm Bas Led   keys pad kit p1 p2 bass leads lp echo */
    {"Loading Bay",    {70, 40, 122, 35, 70, 40, 65, 75, 75, 45},  2, 1, 0, PC_SHAKER, PC_CONGA, 0, 2, 60, 25,
     B(0) | B(1) | B(5) | B(12) | B(13) | B(15), B(1) | B(3) | B(5), B(1) | B(3) | B(0), B(0) | B(1)},
    {"Fresh Linen",    {55, 60, 120, 45, 15, 60, 75, 60, 65, 60},  0, 2, 0, PC_SHAKER, PC_CONGA, 1, 1, 70, 20,
     B(2) | B(4) | B(7) | B(9) | B(14) | B(16), B(0) | B(4) | B(5), B(0) | B(2), B(0) | B(3)},
    {"Cicero Ave",     {80, 35, 124, 55, 50, 30, 70, 80, 75, 45},  1, 0, 1, PC_RIM, PC_CLAVE, 1, 2, 50, 10,
     B(0) | B(1) | B(3) | B(6) | B(15), B(1) | B(2), B(1) | B(4), B(1) | B(2)},
    {"Paradise Ticket",{65, 55, 122, 60, 25, 45, 75, 70, 70, 60},  1, 1, 0, PC_SHAKER, PC_CONGA, 1, 3, 75, 15,
     B(2) | B(4) | B(7) | B(9) | B(16), B(5) | B(1) | B(2), B(4) | B(2) | B(0), B(1) | B(2)},
    {"Sangria",        {45, 65, 118, 40, 20, 70, 70, 55, 65, 65},  0, 2, 0, PC_CONGA, PC_SHAKER, 1, 1, 80, 35,
     B(2) | B(4) | B(8) | B(14) | B(16), B(0) | B(4), B(0) | B(2) | B(3), B(3) | B(0)},
    {"Velvet Rope",    {35, 75, 118, 50, 40, 55, 75, 50, 65, 45},  0, 1, 2, PC_SHAKER, PC_NONE, 0, 1, 45, 25,
     B(3) | B(4) | B(7) | B(8) | B(16), B(0) | B(5), B(3) | B(0), B(3)},
    {"Ghost Stabs",    {40, 60, 120, 35, 85, 70, 70, 65, 80, 30},  2, 0, 2, PC_SHAKER, PC_NONE, 0, 2, 35, 65,
     B(5) | B(11) | B(12) | B(13), B(3), B(3) | B(0), B(0) | B(3)},
    {"Hammond Sweat",  {75, 45, 123, 50, 45, 35, 75, 75, 75, 40},  1, 0, 0, PC_SHAKER, PC_RIM, 2, 2, 50, 10,
     B(0) | B(1) | B(6) | B(10) | B(15), B(2) | B(1), B(0) | B(2) | B(4), B(1) | B(2)},
    {"Last Elevator",  {50, 45, 121, 30, 80, 55, 65, 70, 75, 35},  0, 1, 0, PC_RIM, PC_SHAKER, 0, 2, 40, 35,
     B(0) | B(5) | B(11) | B(12) | B(13), B(3) | B(1) | B(0), B(3) | B(1), B(0) | B(3)},
    {"Assembly Line",  {65, 40, 123, 35, 60, 50, 70, 75, 70, 50},  2, 2, 1, PC_SHAKER, PC_CLAVE, 1, 1, 60, 25,
     B(0) | B(3) | B(5) | B(6) | B(10), B(0) | B(1) | B(5), B(1) | B(0), B(1) | B(0)},
    {"Sticky Floor",   {70, 30, 124, 45, 65, 35, 65, 80, 80, 40},  0, 0, 0, PC_CONGA, PC_RIM, 1, 2, 55, 15,
     B(0) | B(1) | B(10) | B(12) | B(15), B(1) | B(5), B(1) | B(4), B(1) | B(2)},
    {"Sunday Best",    {55, 70, 119, 55, 10, 50, 75, 60, 70, 60},  0, 2, 0, PC_CONGA, PC_SHAKER, 1, 3, 70, 15,
     B(2) | B(4) | B(7) | B(9) | B(14), B(5) | B(0) | B(1), B(2) | B(4) | B(0), B(1) | B(3)},
    {"Pool Lights",    {45, 55, 120, 40, 70, 75, 70, 60, 70, 50},  2, 2, 2, PC_SHAKER, PC_CLAVE, 0, 1, 55, 45,
     B(3) | B(5) | B(6) | B(13) | B(16), B(0) | B(3) | B(4), B(3) | B(0), B(0) | B(3)},
};
static int sty(int i) { return (i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES; }
const char *en_style_name(int i) { return STYLES[sty(i)].name; }
void en_style_recipe(int i, int16_t *par)
{
    int k;
    for (k = 0; k < P_NSTYLE; k++)
        par[k] = STYLES[sty(i)].v[k];
}

/* ------------------------------------------------------------- titles --- */
/* (words no other firmware here uses: adjective 7 letters at most, noun 8, so a title fits in 16) */
static const char *const T_ADJ[] = {"Sacred", "Tender", "Liquid", "Humid", "Silk", "Smoky", "Loose", "Sweaty",
                                    "Holy", "Unsung", "Velour", "Satin", "Tinted", "Frayed", "Plush", "Woozy",
                                    "Lucid", "Cherry", "Rapt", "Devoted", "Pious", "Glossy", "Ripe", "Sultry"};
static const char *const T_NOUN[] = {"Sermon", "Payphone", "Overcoat", "Witness", "Desire", "Feeling", "Hymnal",
                                     "Lipstick", "Perfume", "Wardrobe", "Gospel", "Deacon", "Pager", "Taxicab",
                                     "Doorman", "Seltzer", "Locket", "Pulpit", "Rapture", "Devotion", "Fever",
                                     "Yearning", "Embrace", "Patience", "Healing", "Mercy", "Grace", "Loyalty"};
static const char *const T_PHRASE[] = {"Feel It", "Say It Twice", "Let Me In", "Keep Moving", "Rise Up",
                                       "Come Closer", "Free Your Mind", "Move Me", "Bring It Home", "Don't Stop",
                                       "Take Me There", "Carry Me", "So Real", "Right Here", "Lift Me", "Testify"};
#define NA(a) (sizeof a / sizeof a[0])

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

void en_title(uint32_t seed, char *b)
{
    uint32_t h = en_track_rand(seed, 77u);
    const char *a = T_ADJ[h % NA(T_ADJ)], *n = T_NOUN[(h >> 8) % NA(T_NOUN)];
    switch ((h >> 24) % 8u) {
    case 0: case 1: case 2:                          /* "Velour Sermon" */
        put_s(put_s(put_s(b, a), " "), n);
        break;
    case 3: case 4:                                  /* "Feel It" */
        put_s(b, T_PHRASE[(h >> 16) % NA(T_PHRASE)]);
        break;
    case 5:                                          /* "The Payphone" */
        put_s(put_s(b, "The "), n);
        break;
    case 6:                                          /* "Mercy" */
        put_s(b, n);
        break;
    default:                                         /* "Smoky Love" */
        put_s(put_s(b, a), (h >> 16) & 1u ? " Lovin'" : " Love");
        break;
    }
}
const char *en_mix_tag(uint32_t seed, int lead)
{
    uint32_t h = en_track_rand(seed, 91u) % 4u;
    static const char *const NONE[4] = {"Bonus Beats", "Deep Mix", "Instrumental", "Reprise"};
    static const char *const HOOK[4] = {"Original Mix", "Club Mix", "Extended Mix", "Organ Mix"};
    static const char *const VOX[4] = {"Vocal Mix", "Club Mix", "Vocal Mix", "Original Mix"};
    return lead == 1 ? HOOK[h] : lead == 2 ? VOX[h] : NONE[h];
}
uint8_t en_label_colour(uint32_t seed) { return (uint8_t)(en_track_rand(seed, 5u) % 12u); }

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_STYLE, C_PLAY, C_MIX, C_HOLD, C_TRACK_NO, C_TRACK_SEED_HI, C_TRACK_SEED_LO, C_NOTE, C_PANIC };
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
void en_mix(int quick) { post(C_MIX, quick, 0); }
void en_hold(int on) { post(C_HOLD, on, 0); }
void en_note(int note, int vel) { post(C_NOTE, note, vel); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_track(uint32_t no, uint32_t seed)
{
    post(C_TRACK_SEED_HI, 0, (int)(seed >> 16));
    post(C_TRACK_SEED_LO, 0, (int)(seed & 0xFFFFu));
    post(C_TRACK_NO, (int)(no >> 16 & 0xFFu), (int)(no & 0xFFFFu));
}

/* -------------------------------------------------------------- state --- */
en_deck_t en_deck[2];
volatile float en_xfade;
volatile uint8_t en_master, en_mixing, en_playing, en_hold_on, en_bpm = 121;
volatile uint32_t en_track_no, en_seed_base = 0x44485331u, en_beats, en_tracks_played, en_mixes;
volatile uint8_t en_chord_root, en_chord_q;
volatile uint16_t en_chord_pcs;
volatile float en_vu_l, en_vu_r;

static int16_t par[P_NPARAMS];
static int style_i;
static uint32_t rng = 0x9E3779B9u, nrng = 0x2545F491u;

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

/* ------------------------------------------------------------- tracks --- */
/* a record's plan: [0, lin/2) the drums; [lin/2, lin) the groove (the bass and the chords come in on the blend's
 * swap); then the body, 16-bar phrases (G groove, F full, B a breakdown with its build), looping while held; from
 * out_at the outro: the groove without its lead for lout/2 bars, then the drums alone, then it stops */
enum { PH_G, PH_F, PH_B };
typedef struct {
    uint32_t seed, no;
    uint32_t trng;
    int key, vamp, cyc;                          /* the key (minor tonic), the vamp, its length in bars */
    int comp, bpat, hats, p1, p2, p1v, p2v;      /* the patterns; the percussion and which of its patterns */
    int keys, pad, kit, bass, echo;              /* the set's sound, as it was when the record was cued */
    int lead, hook, vox;                         /* 0 none, 1 hook, 2 vocal; which */
    int fill;                                    /* phrase ends: the kick drops for the last beat */
    int lin, lout;                               /* the blends in and out (bars) */
    int nph, body;                               /* body phrases, bars */
    uint8_t ph[8];
    int out_at;                                  /* the outro's first bar */
    int32_t start;                               /* the set's bar of its bar 0 */
    int voicing[4];
    int chord;                                   /* the chord sounding (index into the vamp), -1 none yet */
    int pushed;
    int lead_last, vox_last;                     /* the last notes: the next placed near */
    int pad_k, pad_until;                        /* the pad's chord, the bar it holds to */
} track_t;
static track_t tk[2];

static int ttrand(track_t *t, int n) { return n > 0 ? (int)((xs(&t->trng) >> 8) % (uint32_t)n) : 0; }
static float tfrand(track_t *t) { return (float)(xs(&t->trng) >> 8) * (1.0f / 16777216.0f); }
static int pick_bit(track_t *t, uint32_t mask, int n)    /* one of mask's bits below n, evenly */
{
    int i, c = 0, k;
    for (i = 0; i < n; i++)
        c += (int)(mask >> i & 1u);
    if (!c)
        return 0;
    k = ttrand(t, c);
    for (i = 0; i < n; i++)
        if (mask >> i & 1u && !k--)
            return i;
    return 0;
}

static int vamp_chord(const track_t *t, int vb)       /* the vamp's chord at its bar vb */
{
    const vamp_t *v = &VAMPS[t->vamp];
    int b = vb % t->cyc, k, at = 0;
    for (k = 0; k < v->n; k++) {
        at += v->c[k].bars;
        if (b < at)
            return k;
    }
    return 0;
}

/* the body's templates (16-bar phrases): calm, steady, long */
static const char *const BODIES[] = {"GGBFF", "GFBFF", "GFFBFF", "GFBFFBF", "FFBFF"};

static void plan_body(track_t *t, float energy)
{
    int i, b;
    float r = tfrand(t);
    b = energy < 0.35f ? (r < 0.6f ? 0 : 1) : energy < 0.7f ? (r < 0.4f ? 1 : r < 0.8f ? 2 : 4) : (r < 0.5f ? 2 : 3);
    t->nph = 0;
    for (i = 0; BODIES[b][i] && i < 8; i++)
        t->ph[t->nph++] = (uint8_t)(BODIES[b][i] == 'G' ? PH_G : BODIES[b][i] == 'F' ? PH_F : PH_B);
    t->body = t->nph * 16;
}

/* the records' keys: always a neighbour on the wheel (the same, a fifth up, a fifth down) */
static void new_record(int d, int prev_key, int prev_vamp)
{
    track_t *t = &tk[d];
    const style_t *s = &STYLES[style_i];
    en_deck_t *dk = &en_deck[d];
    float w[NVAMPS], tot = 0.0f, r;
    int i, mood = par[P_MOOD];
    en_track_no++;
    t->no = en_track_no;
    t->seed = en_track_rand(en_seed_base, en_track_no);
    t->trng = t->seed;
    if (prev_key < 0) {
        t->key = ttrand(t, 12);
    } else {
        static const int8_t STEP[3] = {0, 7, 5};
        t->key = (prev_key + STEP[ttrand(t, 3)]) % 12;
    }
    for (i = 0; i < NVAMPS; i++) {               /* the vamp: the set's, near the mood */
        float dm = ((float)VAMPS[i].mood - (float)mood) * (1.0f / 22.0f);
        w[i] = (s->vamps >> i & 1u) ? 1.0f / (1.0f + dm * dm * dm * dm) : 0.0f;
        if (i == prev_vamp)
            w[i] *= 0.1f;
        tot += w[i];
    }
    r = tfrand(t) * tot;
    t->vamp = 0;
    for (i = 0; i < NVAMPS; i++) {
        if (w[i] <= 0.0f)
            continue;
        t->vamp = i;
        r -= w[i];
        if (r <= 0.0f)
            break;
    }
    t->cyc = 0;
    for (i = 0; i < VAMPS[t->vamp].n; i++)
        t->cyc += VAMPS[t->vamp].c[i].bars;
    t->comp = pick_bit(t, s->comps, NCOMPS);
    t->bpat = pick_bit(t, s->bpats, NBPATS);
    t->hats = pick_bit(t, s->hats, NHATS);
    t->p1 = s->p1;
    t->p2 = s->p2;
    t->p1v = ttrand(t, 2);
    t->p2v = ttrand(t, 2);
    t->keys = s->keys;
    t->pad = s->pad;
    t->kit = s->kit;
    t->bass = s->bass;
    t->echo = s->echo;
    t->lead = 0;
    if (s->leads && ttrand(t, 100) < s->lead_p) {
        int l = s->leads == 3 ? 1 + ttrand(t, 2) : s->leads;
        t->lead = l;
    }
    t->hook = ttrand(t, NHOOKS);
    t->vox = ttrand(t, NVOXES);
    t->fill = ttrand(t, 3) == 0;
    plan_body(t, (float)par[P_ENERGY] * 0.01f);
    t->lin = 16;
    t->lout = MIX_BARS[par[P_MIX] < 0 ? 0 : par[P_MIX] > 2 ? 2 : par[P_MIX]];
    t->out_at = t->lin + t->body;
    t->chord = -1;
    t->pushed = 0;
    t->lead_last = 69;
    t->vox_last = 69;
    t->pad_k = -1;
    t->pad_until = 0;
    for (i = 0; i < 4; i++)
        t->voicing[i] = 58 + 3 * i;
    dk->loaded = 1;
    dk->on = 0;
    dk->no = t->no;
    dk->seed = t->seed;
    dk->key = (uint8_t)t->key;
    dk->lead = (uint8_t)t->lead;
    dk->bar = 0;
    dk->bars = (uint16_t)(t->out_at + t->lout);
    dk->sect = S_IDLE;
    en_tracks_played++;
}

/* where a record is at its bar b: the section, and its parts */
enum {
    EL_KICK = 1, EL_CLAP = 2, EL_OHAT = 4, EL_CHAT = 8, EL_P1 = 16, EL_P2 = 32, EL_BASS = 64, EL_CHORDS = 128,
    EL_PAD = 256, EL_LEAD = 512, EL_ROLL = 1024, EL_FILL = 2048
};
static uint32_t parts(const track_t *t, int b, int *sect, int *pbar)
{
    float e = (float)par[P_ENERGY] * 0.01f;
    int h = t->lin / 2;
    uint32_t m;
    *pbar = 0;
    if (b < h) {                                 /* the drums: the hats, then the offbeats and the claps */
        *sect = S_INTRO;
        *pbar = b;
        m = EL_KICK | (e > 0.25f ? EL_CHAT : 0);
        if (b >= h / 2)
            m |= EL_OHAT | EL_CLAP | (e > 0.4f ? EL_P1 : 0);
        return m;
    }
    if (b < t->out_at) {
        int pb, ph, pt;
        if (b < t->lin) {
            *sect = S_GROOVE;
            *pbar = b - h;
            return EL_KICK | EL_CLAP | EL_OHAT | EL_BASS | EL_CHORDS | (e > 0.3f ? EL_CHAT : 0) | (e > 0.5f ? EL_P1 : 0);
        }
        pb = (b - t->lin) % t->body;
        ph = pb / 16;
        pt = t->ph[ph];
        *pbar = pb % 16;
        if (pt == PH_B) {                        /* the kick and the bass out: the chords long, a pad, the lead */
            m = EL_CHORDS | (t->pad ? EL_PAD : 0) | (t->lead ? EL_LEAD : 0);
            if (*pbar >= 4 && *pbar < 12 && e > 0.6f)
                m |= EL_P1;
            if (*pbar >= 12) {                   /* the build: the hats back, the claps rolling */
                *sect = S_BUILD;
                return m | EL_CHAT | (*pbar >= 14 ? EL_ROLL : 0);
            }
            *sect = S_BREAK;
            return m;
        }
        m = EL_KICK | EL_CLAP | EL_OHAT | EL_BASS | EL_CHORDS;
        if (*pbar == 15 && t->fill && ph + 1 < t->nph)
            m |= EL_FILL;
        if (pt == PH_G) {
            *sect = S_GROOVE;
            return m | (e > 0.3f ? EL_CHAT : 0) | (e > 0.5f ? EL_P1 : 0);
        }
        *sect = S_FULL;
        m |= EL_CHAT | EL_P1 | (e > 0.6f ? EL_P2 : 0);
        if (t->pad == 2 || (t->pad && e > 0.75f))
            m |= EL_PAD;
        if (t->lead) {                           /* the lead: the drop after a breakdown, and with energy, the
                                                  * second of two full phrases */
            int after_b = ph > 0 && t->ph[ph - 1] == PH_B, second = ph > 0 && t->ph[ph - 1] == PH_F;
            if (after_b || (second && e > 0.55f && *pbar >= 8))
                m |= EL_LEAD;
        }
        return m;
    }
    if (b < t->out_at + t->lout / 2) {           /* the outro: the groove without its lead */
        *sect = S_OUTRO;
        *pbar = b - t->out_at;
        return EL_KICK | EL_CLAP | EL_OHAT | EL_CHAT | EL_BASS | EL_CHORDS | (e > 0.4f ? EL_P1 : 0);
    }
    *sect = S_TAIL;                              /* the drums alone, going */
    *pbar = b - t->out_at - t->lout / 2;
    return EL_KICK | EL_CLAP | EL_OHAT | EL_CHAT | EL_P1 | (e > 0.6f ? EL_P2 : 0);
}

/* -------------------------------------------------------------- voices --- */
/* the chords: FM electric piano (LOFI RADIO's), organ (odd partials, a percussion click), pad stab; and the pad */
enum { K_RHODES, K_ORGAN, K_STAB, K_PAD };
#define NCV 14
typedef struct {
    float pc, pt, ic, it, mr;
    float amp, dec, rel, att, att_inc, sus;
    float idx, idx_dec, idx_floor, tine, tine_dec;
    float gl, gr;
    int32_t gate;                                /* samples until the release; < 0: held by the player */
    uint8_t note, on, player, deck, pad;
    uint32_t age;
} cv_t;
static cv_t cv[NCV];
static uint32_t cv_clock;

static cv_t *cv_alloc(void)
{
    cv_t *v = 0;
    int i;
    uint32_t oldest = 0;
    for (i = 0; i < NCV; i++)
        if (!cv[i].on)
            return &cv[i];
    for (i = 0; i < NCV; i++) {                  /* the oldest released one, else the oldest */
        uint32_t a = cv_clock - cv[i].age + (cv[i].gate == 0 ? 0x10000000u : 0u);
        if (!cv[i].player && a >= oldest) {
            oldest = a;
            v = &cv[i];
        }
    }
    return v ? v : &cv[0];
}

static void key_on(int deck, int timbre, int note, float vel, int32_t gate, int player, float pan)
{
    cv_t *v = cv_alloc();
    float f, tdec;
    while (note < 36)
        note += 12;
    while (note > 96)
        note -= 12;
    f = mtof((float)note);
    v->ic = f / EN_SR;
    v->pc = v->pt = 0.0f;
    vel = fm_clampf(vel, 0.05f, 1.0f);
    v->att = 0.0f;
    v->att_inc = 1.0f / (0.003f * EN_SR);
    v->sus = 0.0f;
    v->pad = 0;
    tdec = fm_clampf(2.6f * fm_sqrtf(261.6f / f), 0.9f, 4.5f);
    if (timbre == K_RHODES) {                    /* the bark with the velocity, a tine's ping */
        v->mr = 1.0f;
        v->it = fm_minf(f * 7.0f, 7000.0f) / EN_SR;
        v->idx = 0.10f + 0.30f * vel;
        v->idx_floor = 0.04f;
        v->idx_dec = tau(0.25f);
        v->tine = 0.06f * vel;
        v->tine_dec = tau(0.03f);
        v->dec = tau(tdec);
        v->rel = tau(0.16f);
        v->amp = vel * (0.55f + 0.45f * vel);
    } else if (timbre == K_ORGAN) {              /* odd partials, steady; the percussion a third partial's click */
        v->mr = 2.0f;
        v->it = fm_minf(f * 3.0f, 9000.0f) / EN_SR;
        v->idx = 0.62f;
        v->idx_floor = 0.55f;
        v->idx_dec = tau(0.1f);
        v->tine = 0.22f * vel;
        v->tine_dec = tau(0.07f);
        v->dec = tau(6.0f);
        v->rel = tau(0.045f);
        v->amp = 0.62f * vel;
        v->att_inc = 1.0f / (0.002f * EN_SR);
    } else if (timbre == K_STAB) {               /* a pad stab: bright, closing, a short tail */
        v->mr = 1.0f;
        v->it = 0.0f;
        v->idx = 0.95f;
        v->idx_floor = 0.32f;
        v->idx_dec = tau(0.22f);
        v->tine = 0.0f;
        v->tine_dec = 0.0f;
        v->dec = tau(1.4f);
        v->rel = tau(0.22f);
        v->amp = 0.62f * vel;
        v->att_inc = 1.0f / (0.006f * EN_SR);
    } else {                                     /* the pad: slow, soft, a little detuned */
        v->mr = 1.0f;
        v->ic *= 1.0f + (pan > 0.0f ? 0.0021f : -0.0021f);
        v->it = 0.0f;
        v->idx = 0.18f;
        v->idx_floor = 0.34f;
        v->idx_dec = tau(1.6f);
        v->tine = 0.0f;
        v->tine_dec = 0.0f;
        v->dec = tau(9.0f);
        v->rel = tau(0.7f);
        v->amp = 0.30f * vel;
        v->att_inc = 1.0f / (0.55f * EN_SR);
        v->pad = 1;
    }
    v->gl = 0.75f - 0.6f * pan;
    v->gr = 0.75f + 0.6f * pan;
    v->gate = gate;
    v->note = (uint8_t)note;
    v->on = 1;
    v->player = (uint8_t)player;
    v->deck = (uint8_t)deck;
    v->age = cv_clock++;
}
static void key_off_player(int note)
{
    int i;
    for (i = 0; i < NCV; i++)
        if (cv[i].on && cv[i].player && cv[i].note == note && cv[i].gate < 0)
            cv[i].gate = 0;
}

/* the bass, a deck each: a round FM sine (a sub, or an organ's bass) */
typedef struct {
    float ph, inc, tinc, amp, peak, sus, idx, idx0, h2, dec;
    float lp;
    int32_t gate;
    uint8_t on, atk;
} bass_t;
static bass_t bs[2];
static void bass_on(int d, int type, int note, float vel, int32_t gate)
{
    bass_t *b = &bs[d];
    float f = mtof((float)note);
    if (!b->on || b->amp < 0.05f)
        b->inc = f / EN_SR;
    b->tinc = f / EN_SR;
    b->on = 1;
    b->atk = 1;
    b->gate = gate;
    b->peak = vel;
    b->sus = vel * (type == 2 ? 0.45f : 0.7f);
    b->idx0 = type == 0 ? 0.03f : type == 1 ? 0.10f : 0.32f;
    b->idx = b->idx0 + (type == 0 ? 0.08f : 0.22f) * vel;
    b->h2 = type == 0 ? 0.06f : type == 1 ? 0.18f : 0.32f;
    b->dec = tau(type == 2 ? 0.18f : 0.4f);
}

/* the drums, a deck each */
typedef struct {
    float kph, kf, kfend, kpdec, kamp, kadec, kclick;
    float cl_amp, cl_b, cl_tail, cl_lo, cl_bd;
    int32_t cl_t;
    float sn_ph, sn_inc, sn_tamp, sn_tdec, sn_namp, sn_ndec, sn_lo, sn_bd;
    float rm_ph, rm_amp, rm_namp, rm_lo, rm_bd;
    float ch_amp, ch_dec, oh_amp, oh_dec, h1, h2, hlp;
    float sh_amp, sh_env, sh_up, sh_h, sh_lp;
    float cg_ph, cg_inc, cg_tinc, cg_amp, cg_dec, cg_slap;
    float cv_ph, cv_amp;
} kit_t;
static kit_t kt[2];
static float duck[2];                            /* the kick's push on the chords (a soft side-chain) */

/* the lead (a soft FM hook) and the vocal (a formant stab): one each, the master deck's */
static struct {
    float ph, inc, tinc, amp, att, peak, dec, rel, vib, vph, idx;
    int32_t gate;
    uint8_t on, deck;
} ld;
static struct {
    float ph, inc, tinc, amp, att, rel, f1, f2, g2, s1a, s1b, s2a, s2b;
    int32_t gate;
    uint8_t on, deck;
} vx;
static const float VOWEL[5][3] = {               /* F1, F2, F2's level: a o e i u */
    {780.0f, 1180.0f, 0.55f}, {480.0f, 860.0f, 0.5f}, {520.0f, 1800.0f, 0.4f}, {330.0f, 2350.0f, 0.3f},
    {360.0f, 720.0f, 0.45f},
};

/* -------------------------------------------------------------- spaces --- */
/* the hall: four delay lines, a Householder mix, damped; two diffusers before */
#define RV0 1777
#define RV1 2137
#define RV2 2557
#define RV3 2971
#define AP0 347
#define AP1 531
static float rv_buf[RV0 + RV1 + RV2 + RV3 + AP0 + AP1] EN_POOL;
static float *rv_d[4];
static const int RV_N[4] = {RV0, RV1, RV2, RV3};
static int rv_p[4], ap_p[2];
static float *ap_d[2];
static const int AP_N[2] = {AP0, AP1};
static float rv_lp[4];

/* the echo: a dotted eighth, darkening; a second tap a beat and a half on, on the right */
#define EC_N 36864
static int16_t ec_buf[EC_N] EN_POOL;
static int ec_w;
static float ec_lp, ec_len = 16000.0f;

/* -------------------------------------------------------- the sequence --- */
#define BLK 256
#define NEV 64
enum {
    E_KEY, E_PAD, E_BASS, E_KICK, E_CLAP, E_SNARE, E_RIM, E_CHAT, E_OHAT, E_SHAKER, E_CONGA, E_CLAVE, E_LEAD, E_VOX
};
typedef struct {
    int32_t wait;
    uint8_t type, note, used, deck, x;
    float vel, pan;
    int32_t gate;
} ev_t;
static ev_t evq[NEV];

static void sched(int deck, int type, int32_t wait, int note, float vel, int32_t gate, int x, float pan)
{
    int i;
    for (i = 0; i < NEV; i++)
        if (!evq[i].used) {
            ev_t *e = &evq[i];
            e->used = 1;
            e->type = (uint8_t)type;
            e->deck = (uint8_t)deck;
            e->wait = wait < 0 ? 0 : wait;
            e->note = (uint8_t)note;
            e->vel = vel;
            e->gate = gate;
            e->x = (uint8_t)x;
            e->pan = pan;
            return;
        }
}

static float step_len = 5467.0f;                 /* samples a sixteenth */
static float step_left;                          /* samples to the next */
static uint32_t gstep;                           /* the next sixteenth of the set */
static float fade = 1.0f;                        /* the decks' power (stopped: the tails ring out) */
static int32_t mix_start = -1, mix_len = 32;     /* the blend: its first bar (the set's), its length */
static int mix_from, mix_to;
static int mix_quick;                            /* the next blend 16 bars, whatever Mix says */
static float dj_build;                           /* the DJ's filter ride over a build */

static void tempo_set(void)
{
    int b = par[P_TEMPO] < 116 ? 116 : par[P_TEMPO] > 126 ? 126 : par[P_TEMPO];
    en_bpm = (uint8_t)b;
    step_len = EN_SR * 15.0f / (float)b;
    ec_len = fm_minf(step_len * 3.0f, (float)(EC_N / 2 - 4));
}

/* the chord's notes: its four, voice-led from the last (each moving as little as it can), around the middle */
static void voice(track_t *t, int root_pc, int q)
{
    int pcs[4], best[4] = {60, 64, 67, 71}, i, inv, bestd = 1 << 30, prev = 0, home = t->keys == K_ORGAN ? 66 : 64;
    for (i = 0; i < 4; i++) {
        pcs[i] = (root_pc + Q_VOICE[q][i]) % 12;
        prev += t->voicing[i];
    }
    prev /= 4;
    if (prev < 57 || prev > 72)
        prev = home;
    for (inv = 0; inv < 4; inv++) {
        int n[4], j, base = pcs[inv], lo, d = 0, mean = 0;
        lo = 52 + ((base - 52) % 12 + 12) % 12;
        for (j = 0; j < 4; j++) {
            int pc = pcs[(inv + j) % 4];
            int x = j ? n[j - 1] + ((pc - n[j - 1]) % 12 + 12) % 12 : lo;
            if (j && x == n[j - 1])
                x += 12;
            n[j] = x;
        }
        for (j = 0; j < 4; j++)
            mean += n[j];
        mean /= 4;
        while (mean > prev + 6) {
            for (j = 0; j < 4; j++)
                n[j] -= 12;
            mean -= 12;
        }
        while (mean < prev - 6) {
            for (j = 0; j < 4; j++)
                n[j] += 12;
            mean += 12;
        }
        for (j = 0; j < 4; j++) {
            int k, m = 99;
            for (k = 0; k < 4; k++) {
                int e = n[j] - t->voicing[k];
                e = e < 0 ? -e : e;
                m = e < m ? e : m;
            }
            d += m;
        }
        d += (mean - home > 0 ? mean - home : home - mean);
        if (n[0] < 52 || n[3] > 79)
            d += 40;
        if (d < bestd) {
            bestd = d;
            for (j = 0; j < 4; j++)
                best[j] = n[j];
        }
    }
    for (i = 0; i < 4; i++)
        t->voicing[i] = best[i];
}

static int chord_root(const track_t *t, int k) { return (t->key + VAMPS[t->vamp].c[k].root) % 12; }

static void set_chord(int d, int k)
{
    track_t *t = &tk[d];
    int root = chord_root(t, k), i;
    if (k == t->chord)
        return;
    t->chord = k;
    voice(t, root, VAMPS[t->vamp].c[k].q);
    {                                            /* (one deck plays chords at a time: the screen's) */
        uint16_t m = 0;
        en_chord_root = (uint8_t)root;
        en_chord_q = (uint8_t)VAMPS[t->vamp].c[k].q;
        for (i = 0; i < 4; i++)
            m |= (uint16_t)(1u << (t->voicing[i] % 12));
        en_chord_pcs = (uint16_t)(m | 1u << root);
    }
}

static void play_chord(int d, int k, int32_t at, int len_steps, float vel)
{
    track_t *t = &tk[d];
    int i;
    float roll = (3.0f + 6.0f * frand()) * 44.1f;
    set_chord(d, k);
    for (i = 0; i < 4; i++) {
        float v = vel * (0.88f + 0.16f * frand()) * (i == 3 ? 1.06f : 1.0f);
        int32_t g = (int32_t)((float)len_steps * step_len) - (int32_t)(roll * (float)i) - 200;
        sched(d, E_KEY, at + (int32_t)(roll * (float)i), t->voicing[i], v, g < 400 ? 400 : g, t->keys,
              (float)(i - 1) * 0.22f - 0.11f);
        LOG(d, 'C', t->voicing[i], len_steps, v);
    }
}

static void play_pad(int d, int k, int len_steps)
{
    track_t *t = &tk[d];
    int i, root = chord_root(t, k), q = VAMPS[t->vamp].c[k].q;
    for (i = 0; i < 4; i++) {                    /* the chord's tones an octave over the stabs' middle */
        int n = 60 + (root + Q_VOICE[q][i]) % 12;
        if (n < 62)
            n += 12;
        sched(d, E_PAD, 0, n, 0.8f, (int32_t)((float)len_steps * step_len), 0, i & 1 ? 0.4f : -0.4f);
        LOG(d, 'P', n, len_steps, 0.8f);
    }
}

/* the melody's note for a chord tone (deg: 0 root .. 4 ninth), the octave nearest the last, in range */
static int tone_near(const track_t *t, int k, int deg, int last, int lo, int hi)
{
    int pc = (chord_root(t, k) + Q_DEG[VAMPS[t->vamp].c[k].q][deg]) % 12, n, best = 69, bd = 99, o;
    for (o = 3; o <= 7; o++) {
        n = o * 12 + pc;
        if (n < lo || n > hi)
            continue;
        if ((n > last ? n - last : last - n) < bd) {
            bd = n > last ? n - last : last - n;
            best = n;
        }
    }
    return best;
}

/* the swing: the off sixteenths late, up to a third of a sixteenth (a triplet's feel) */
static int32_t swing_at(int s) { return (s & 1) ? (int32_t)(step_len * 0.32f * (float)par[P_SWING] * 0.01f) : 0; }
static int32_t hum(float ms) { return (int32_t)((frand() * 2.0f - 1.0f) * ms * 44.1f); }

static int pat_len(const char *p, int at, int n)  /* a hit's length: itself and the holds after it */
{
    int l = 1;
    while (at + l < n && p[at + l] == '-')
        l++;
    return l;
}

static void deck_step(int d, int b, int s)
{
    track_t *t = &tk[d];
    en_deck_t *dk = &en_deck[d];
    int sect, pbar, vb, k, k1;
    uint32_t m = parts(t, b, &sect, &pbar);
    int32_t sw = swing_at(s);
    float dv = 1.0f;
    if (s == 0) {
        dk->bar = (uint16_t)b;
        dk->sect = (uint8_t)sect;
        dk->bars = (uint16_t)(t->out_at + t->lout);
        LOG(d, '|', sect, t->chord >= 0 ? chord_root(t, t->chord) * 16 + VAMPS[t->vamp].c[t->chord].q : 0,
            (float)pbar);
    }
    vb = b - t->lin / 2;                         /* the vamp's bar: from the moment the harmony comes in */
    if (vb < 0)
        vb = 0;
    k = vamp_chord(t, vb);
    k1 = vamp_chord(t, vb + 1);
    /* chords */
    if (m & EL_CHORDS) {
        int brk = sect == S_BREAK || sect == S_BUILD;
        const char *p = brk ? BREAK_COMP : COMPS[t->comp];
        int at = (vb & 1) * 16 + s;
        char c = p[at];
        float kv0 = 0.85f;
        if (s == 0)
            set_chord(d, k);                     /* (the voicing, and the screen's chord, follow the bars) */
        if (t->pushed && s == 0 && c != '.') {
            t->pushed = 0;                       /* it came early */
        } else if (c == 'X' || c == 'x' || c == 'P') {
            int len = pat_len(p, at, 32), kk = k;
            if (c == 'P') {
                kk = k1;
                t->pushed = 1;
            } else if (s + len > 16 && k1 != k) {
                len = 16 - s;                    /* not over the change */
            }
            play_chord(d, kk, sw + hum(3.0f), len,
                       kv0 * (c == 'X' ? 0.9f : c == 'P' ? 0.8f : 0.66f) * (brk ? 0.85f : 1.0f));
        } else if (s == 0) {
            t->pushed = 0;
        }
        if ((m & EL_PAD) && s == 0 && (k != t->pad_k || b >= t->pad_until)) {
            int n = 0, j = vb;                   /* the pad holds the chord for as long as it lasts */
            while (vamp_chord(t, j) == k && n < 4) {
                n++;
                j++;
            }
            play_pad(d, k, n * 16);
            t->pad_k = k;
            t->pad_until = b + n;
        }
    } else {
        t->pushed = 0;
    }
    if (!(m & EL_PAD))
        t->pad_k = -1;
    /* bass */
    if (m & EL_BASS) {
        const char *p = BPATS[t->bpat];
        char c = p[s];
        if (c != '.' && c != '-') {
            int root = chord_root(t, k), n = 31 + (root - 7 + 12) % 12, len = pat_len(p, s, 16);
            if (c == 'A') {
                if (k1 != k) {                   /* a scale step into the next root, from below */
                    static const int8_t SC[7] = {0, 2, 3, 5, 7, 8, 10};
                    int nr = 31 + (chord_root(t, k1) - 7 + 12) % 12, x, bestn = nr - 1;
                    for (x = 1; x <= 2; x++) {
                        int pc = ((nr - x) % 12 - t->key + 24) % 12, j;
                        for (j = 0; j < 7; j++)
                            if (SC[j] == pc) {
                                bestn = nr - x;
                                x = 3;
                                break;
                            }
                    }
                    n = bestn;
                } else {
                    n += 7;
                }
            } else if (c == 'O') {
                n += 12;
            } else if (c == 'F') {
                n += 7;
            }
            if (n > 54)
                n -= 12;
            sched(d, E_BASS, sw + 40, n, s == 0 ? 0.95f : (s & 3) == 2 ? 0.85f : 0.78f,
                  (int32_t)((float)len * step_len * 0.92f) - 200, t->bass, 0.0f);
            LOG(d, 'B', n, len, 0.85f);
        }
    }
    /* drums */
    if ((m & EL_KICK) && (s & 3) == 0 && !((m & EL_FILL) && s == 12)) {
        sched(d, E_KICK, 0, 0, dv * 0.95f, 0, 0, 0.0f);
        LOG(d, 'K', 0, 1, 0.95f);
    }
    if ((m & EL_CLAP) && (s == 4 || s == 12)) {
        int ty = t->kit == 2 ? E_RIM : E_CLAP;
        sched(d, ty, hum(2.0f), 0, dv * 0.85f, 0, 0, 0.0f);
        if (t->kit == 1)
            sched(d, E_SNARE, 30, 0, dv * 0.7f, 0, 0, 0.0f);
        else if (t->kit == 2)
            sched(d, E_CLAP, 90, 0, dv * 0.35f, 0, 0, 0.0f);
        LOG(d, 'S', 0, 1, 0.85f);
    }
    if ((m & EL_ROLL) && !(s & 1)) {             /* the build's last two bars: quarters, then eighths, growing */
        int on = pbar == 14 ? (s & 3) == 0 : 1;
        if (on) {
            float g = (float)((pbar - 14) * 16 + s) / 32.0f;
            sched(d, E_CLAP, hum(1.5f), 0, dv * (0.25f + 0.45f * g), 0, 0, 0.0f);
            LOG(d, 'S', 1, 1, 0.25f + 0.45f * g);
        }
    }
    if ((m & EL_OHAT) && (s & 3) == 2 && !((m & EL_FILL) && s == 14)) {
        sched(d, E_OHAT, sw + hum(2.0f), 0, dv * (0.55f + 0.1f * frand()), 0, 0, 0.0f);
        LOG(d, 'O', 0, 1, 0.6f);
    }
    if (m & EL_CHAT) {
        char c = HATS[t->hats][s];
        if (c != '.' && !((m & EL_OHAT) && (s & 3) == 2)) {
            float v = c == 'x' ? 0.5f : 0.3f;
            sched(d, E_CHAT, sw + hum(2.5f), 0, dv * v * (0.8f + 0.3f * frand()), 0, 0, 0.0f);
            LOG(d, 'H', 0, 1, v);
        }
    }
    {                                            /* the percussion */
        int i;
        for (i = 0; i < 2; i++) {
            int ty = i ? t->p2 : t->p1;
            const char *p;
            char c;
            if (!ty || !(m & (i ? EL_P2 : EL_P1)))
                continue;
            p = PERC[ty][i ? t->p2v : t->p1v];
            c = p[(b & 1) * 16 + s];
            if (c == '.')
                continue;
            if (ty == PC_SHAKER)
                sched(d, E_SHAKER, sw + hum(3.0f), 0, dv * (c == 'x' ? 0.75f : 0.42f), 0, 0, 0.0f);
            else if (ty == PC_CONGA)
                sched(d, E_CONGA, sw + hum(4.0f), c == 'h' ? 1 : c == 'l' ? 0 : 2, dv * (c == 'm' ? 0.5f : 0.75f), 0,
                      0, 0.0f);
            else if (ty == PC_RIM)
                sched(d, E_RIM, sw + hum(3.0f), 0, dv * 0.55f, 0, 0, 0.0f);
            else
                sched(d, E_CLAVE, sw + hum(2.0f), 0, dv * 0.6f, 0, 0, 0.0f);
            LOG(d, 'R', ty * 10 + (c == 'h' ? 1 : c == 'm' ? 2 : 0), 1, 0.6f);
        }
    }
    /* the lead: a hook over four bars, or the vocal's chops over two */
    if ((m & EL_LEAD) && par[P_LEAD] > 0) {
        float lv = 0.9f;
        if (t->lead == 1) {
            const hook_t *h = &HOOKS[t->hook];
            int lb = pbar % 4, at = (lb & 1) * 16 + s, ni = 0, i;
            if (h->r[at] == 'x') {
                int len = pat_len(h->r, at, 32), deg, n;
                for (i = 0; i < at; i++)
                    ni += h->r[i] == 'x';
                deg = lb < 2 ? h->a[ni] : h->b[ni];
                n = tone_near(t, k, deg, t->lead_last, 62, 81);
                t->lead_last = n;
                sched(d, E_LEAD, sw + hum(4.0f) + 120, n, lv * (s & 3 ? 0.8f : 0.92f),
                      (int32_t)((float)len * step_len * 0.88f), 0, 0.0f);
                LOG(d, 'L', n, len, lv);
            }
        } else if (t->lead == 2 && (par[P_ENERGY] >= 50 || pbar % 4 < 2)) {
            const vox_t *v = &VOXES[t->vox];
            int at = (pbar & 1) * 16 + s, ni = 0, i;
            if (v->r[at] == 'x') {
                int len = pat_len(v->r, at, 32), n, vw;
                char cw;
                for (i = 0; i < at; i++)
                    ni += v->r[i] == 'x';
                cw = v->vow[ni];
                vw = cw == 'a' ? 0 : cw == 'o' ? 1 : cw == 'e' ? 2 : cw == 'i' ? 3 : 4;
                /* the chop sits on a chord tone: the fifth to start, then the nearest one to the last */
                if (pbar % 4 == 0 && at == 0)
                    t->vox_last = tone_near(t, k, 2, 69, 64, 76);
                {
                    int best = t->vox_last, bd = 99, g;
                    for (g = 0; g < 4; g++) {
                        int c2 = tone_near(t, k, g, t->vox_last, 62, 77), e2 = c2 > t->vox_last ? c2 - t->vox_last : t->vox_last - c2;
                        if (e2 < bd) {
                            bd = e2;
                            best = c2;
                        }
                    }
                    n = best;
                }
                t->vox_last = n;
                sched(d, E_VOX, sw + hum(3.0f) + 80, n, lv * (0.85f + 0.15f * frand()),
                      (int32_t)((float)len * step_len * 0.8f), vw, 0.0f);
                LOG(d, 'V', n, len, lv);
            }
        }
    }
}

/* ------------------------------------------------------------ the DJ --- */
static float ramp(float t, float t0, float t1)   /* 0 before t0, 1 after t1, eased between */
{
    float x = t1 > t0 ? (t - t0) / (t1 - t0) : (t >= t1 ? 1.0f : 0.0f);
    x = fm_clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

static void start_deck(int d, int32_t gbar)
{
    tk[d].start = gbar;
    en_deck[d].on = 1;
}

/* a bar begins (the set's bar g): records cued, blends begun and ended */
static void dj_bar(int32_t g)
{
    int M = en_master, N = M ^ 1, bm;
    track_t *t = &tk[M];
    if (!en_deck[M].on)
        return;
    bm = g - t->start;
    if (!en_mixing) {
        if (en_hold_on && bm >= t->out_at - 1 && bm < t->out_at + 1)
            t->out_at += t->body;                /* held: the body again */
        if (!en_deck[N].loaded && bm >= t->out_at - 24 && bm >= 8) {
            new_record(N, t->key, t->vamp);     /* the next record on the other deck: cued, its bass cut */
            en_deck[N].low = 0.0f;
        }
        if (bm == t->out_at && en_deck[N].loaded) {   /* the blend */
            mix_len = mix_quick ? 16 : MIX_BARS[par[P_MIX] < 0 ? 0 : par[P_MIX] > 2 ? 2 : par[P_MIX]];
            mix_quick = 0;
            t->lout = mix_len;
            tk[N].lin = mix_len;
            tk[N].out_at = tk[N].lin + tk[N].body;
            mix_start = g;
            mix_from = M;
            mix_to = N;
            en_mixing = 1;
            start_deck(N, g);
        }
    } else if (g - mix_start >= mix_len) {       /* the blend done: the old record off, the new one the set's */
        en_deck[mix_from].on = 0;
        en_deck[mix_from].loaded = 0;
        en_deck[mix_from].sect = S_IDLE;
        en_master = (uint8_t)mix_to;
        en_mixing = 0;
        en_mixes++;
        mix_start = -1;
        tk[mix_to].chord = -1;
    }
}

/* the DJ's hands, every block: where the faders, the EQs and the filters should be, and the knobs on their way */
static void dj_hands(uint32_t n)
{
    float pos = ((float)gstep - step_left / step_len) * (1.0f / 16.0f), k = fm_minf((float)n / (0.04f * EN_SR), 1.0f);
    float tf[2] = {0.0f, 0.0f}, tl[2] = {1.0f, 1.0f}, tq[2] = {0.0f, 0.0f}, x;
    int M = en_master, N = M ^ 1, d;
    if (en_mixing) {
        float t = pos - (float)mix_start, L = (float)mix_len, H = L * 0.5f;
        int O = mix_from;
        N = mix_to;
        tf[N] = 0.9f * ramp(t, 0.0f, H * 0.5f);
        tf[O] = 0.9f * (1.0f - ramp(t, L * 0.75f, L));
        tl[N] = ramp(t, H - 0.02f, H + 0.12f);   /* the swap, on the downbeat */
        tl[O] = 1.0f - ramp(t, H - 0.02f, H + 0.12f);
        tq[N] = 0.35f * (1.0f - ramp(t, H * 0.5f, H));
        tq[O] = 0.55f * ramp(t, H, L);
        x = 0.5f * ramp(t, 0.0f, H) + 0.5f * ramp(t, H, L);
        en_xfade += ((O == 0 ? x : 1.0f - x) - en_xfade) * k;
    } else {
        int s = en_deck[M].sect;
        tf[M] = en_deck[M].on || en_deck[M].loaded ? 0.9f : 0.0f;
        if (s == S_BUILD) {                      /* the build: the high-pass up as the claps roll */
            dj_build = fm_minf(dj_build + (float)n / (step_len * 64.0f) * 0.5f, 0.5f);
        } else {
            dj_build = 0.0f;
        }
        tq[M] = dj_build;
        tf[N] = 0.0f;
        tl[N] = en_deck[N].loaded ? 0.0f : 1.0f;
        tq[N] = en_deck[N].loaded ? 0.35f : 0.0f;
        en_xfade += ((M == 0 ? 0.0f : 1.0f) - en_xfade) * k * 0.25f;
    }
    for (d = 0; d < 2; d++) {
        en_deck_t *dk = &en_deck[d];
        float kq = tq[d] < dk->filter - 0.1f ? fm_minf(k * 4.0f, 1.0f) : k;   /* a drop: the filter snaps open */
        dk->fader += (tf[d] - dk->fader) * k;
        dk->low += (tl[d] - dk->low) * fm_minf(k * 3.0f, 1.0f);
        dk->filter += (tq[d] - dk->filter) * kq;
        dk->spin += ((dk->on && en_playing ? 1.0f : 0.0f) - dk->spin) * fm_minf((float)n / (dk->on && en_playing ? 0.35f * EN_SR : 1.4f * EN_SR), 1.0f);
        dk->rot += dk->spin * 0.5556f * (float)n / EN_SR;
        if (dk->rot >= 1.0f)
            dk->rot -= 1.0f;
    }
}

static void seq_step(void)
{
    int32_t g = (int32_t)(gstep / 16u);
    int s = (int)(gstep % 16u), d;
    if (s == 0)
        dj_bar(g);
    for (d = 0; d < 2; d++)
        if (en_deck[d].on) {
            int b = g - tk[d].start;
            if (b >= 0)
                deck_step(d, b, s);
            if (b >= tk[d].out_at + tk[d].lout && s == 15 && !(en_mixing && d == mix_to)) {
                en_deck[d].on = 0;               /* ran out with no blend (none cued): the next one, now */
                en_deck[d].loaded = 0;
            }
        }
    if (!en_deck[0].on && !en_deck[1].on && s == 15) {   /* nothing playing: a record on deck A, from the top */
        int prev = tk[en_master].key, pv = tk[en_master].vamp;
        en_master = 0;
        if (!en_deck[0].loaded)
            new_record(0, en_track_no ? prev : -1, en_track_no ? pv : -1);
        en_deck[0].low = 1.0f;
        en_deck[0].filter = 0.0f;
        start_deck(0, g + 1);
    }
    if ((s & 3) == 0)
        en_beats++;
    gstep++;
}

/* ------------------------------------------------------------- control --- */
static void quiet(void)
{
    int i;
    for (i = 0; i < NCV; i++)
        cv[i].on = 0;
    for (i = 0; i < NEV; i++)
        evq[i].used = 0;
    for (i = 0; i < 2; i++) {
        bs[i].on = 0;
        bs[i].amp = 0.0f;
        kt[i].kamp = kt[i].kclick = kt[i].cl_amp = kt[i].cl_b = kt[i].cl_tail = kt[i].sn_tamp = kt[i].sn_namp = 0.0f;
        kt[i].rm_amp = kt[i].rm_namp = kt[i].ch_amp = kt[i].oh_amp = kt[i].sh_amp = kt[i].sh_env = 0.0f;
        kt[i].cg_amp = kt[i].cg_slap = kt[i].cv_amp = 0.0f;
    }
    ld.on = 0;
    vx.on = 0;
}

static void begin_set(void)                      /* a record on deck A from its top; the other empty */
{
    int d;
    quiet();
    for (d = 0; d < 2; d++) {
        en_deck_t *dk = &en_deck[d];
        dk->on = dk->loaded = 0;
        dk->fader = 0.0f;
        dk->low = 1.0f;
        dk->filter = 0.0f;
        dk->sect = S_IDLE;
    }
    en_master = 0;
    en_mixing = 0;
    mix_start = -1;
    en_xfade = 0.0f;
    gstep = 0;
    step_left = 0.0f;
    new_record(0, -1, -1);
    en_deck[0].fader = 0.9f;
    start_deck(0, 0);
}

static void user_mix(int quick)
{
    int M = en_master, N = M ^ 1, bm;
    track_t *t = &tk[M];
    if (en_mixing || !en_deck[M].on)
        return;
    mix_quick = quick;
    bm = (int)(gstep / 16u) - t->start;
    if (bm < 0)
        bm = 0;
    en_hold_on = 0;
    if (!en_deck[N].loaded) {
        new_record(N, t->key, t->vamp);
        en_deck[N].low = 0.0f;
    }
    t->out_at = (bm / 4 + 1) * 4;                /* the next four-bar line */
    if (gstep % 16u == 0u && bm % 4 == 0)
        t->out_at = bm;
}

static void play_on(int on)
{
    int i;
    if (on && !en_playing) {
        en_playing = 1;
        fade = 1.0f;
    } else if (!on && en_playing) {
        en_playing = 0;
        for (i = 0; i < NEV; i++)
            evq[i].used = 0;
        for (i = 0; i < NCV; i++)
            if (cv[i].on && !cv[i].player)
                cv[i].gate = 0;
        bs[0].gate = bs[1].gate = 0;
        ld.gate = vx.gate = 0;
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
        case C_STYLE:
            if (sty(a) != style_i) {
                int N = en_master ^ 1;
                style_i = sty(a);
                if (en_deck[N].loaded && !en_deck[N].on) {   /* a record cued from the old set: another one */
                    en_track_no--;
                    en_tracks_played--;
                    new_record(N, tk[en_master].key, tk[en_master].vamp);
                    en_deck[N].low = 0.0f;
                }
            }
            break;
        case C_PLAY: play_on(a); break;
        case C_MIX: user_mix(a); break;
        case C_HOLD: en_hold_on = (uint8_t)(a != 0); break;
        case C_TRACK_SEED_HI: en_seed_base = (en_seed_base & 0xFFFFu) | ub << 16; break;
        case C_TRACK_SEED_LO: en_seed_base = (en_seed_base & 0xFFFF0000u) | ub; break;
        case C_TRACK_NO:
            en_track_no = (uint32_t)a << 16 | ub;   /* the next is no + 1 */
            en_tracks_played = 0;
            begin_set();
            break;
        case C_NOTE:
            if (b > 0)
                key_on(en_master, tk[en_master].keys == K_ORGAN ? K_ORGAN : K_RHODES, a, (float)b * (1.0f / 127.0f),
                       -1, 1, 0.0f);
            else
                key_off_player(a);
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
    }
    for (i = 0; i < 2; i++) {
        ap_d[i] = p;
        p += AP_N[i];
    }
    for (i = 0; i < (int)(sizeof rv_buf / sizeof rv_buf[0]); i++)
        rv_buf[i] = 0.0f;
    for (i = 0; i < EC_N; i++)
        ec_buf[i] = 0;
    for (i = 0; i < 2; i++) {
        kt[i] = (kit_t){0};
        bs[i] = (bass_t){0};
    }
    q_r = q_w;
    style_i = 0;
    en_playing = 0;
    en_hold_on = 0;
    en_beats = en_tracks_played = en_mixes = 0;
    tempo_set();
    en_track_no = 0;
    begin_set();
}

/* -------------------------------------------------------------- render --- */
static float dl[2][BLK], dr[2][BLK], cl[2][BLK], cr[2][BLK], lbuf[BLK], rsend[BLK], esend[BLK];
static float df[2][4];                           /* the decks' filters (two state-variable filters each) */
static float mf[4];                              /* the master's */
static float warm_l, warm_r, hp_l, hp_r, lev;
static float vu_acc_l, vu_acc_r, mt_acc[2];
static uint32_t vu_n;

static void render_keys(uint32_t n)
{
    int i;
    uint32_t j;
    for (i = 0; i < NCV; i++) {
        cv_t *v = &cv[i];
        float pc, pt, amp, idx, tine, att, mr = v->mr, fl = v->idx_floor, id = v->idx_dec;
        float gl = v->gl, gr = v->gr, ic = v->ic, *bl = cl[v->deck], *br = cr[v->deck];
        uint32_t seg, j0 = 0;
        if (!v->on)
            continue;
        pc = v->pc, pt = v->pt, amp = v->amp, idx = v->idx, tine = v->tine, att = v->att;
        while (j0 < n) {                         /* in two runs: held (decaying), then released */
            float m = v->gate == 0 ? v->rel : v->dec;
            seg = n - j0;
            if (v->gate > 0 && (uint32_t)v->gate < seg)
                seg = (uint32_t)v->gate;
            if (att < 1.0f || tine > 0.0002f) {
                for (j = j0; j < j0 + seg; j++) {
                    float s = sn(pc + sn(pc * mr) * idx) * amp * att + sn(pt) * tine * att;
                    bl[j] += s * gl;
                    br[j] += s * gr;
                    pc = wrap1(pc + ic);
                    pt = wrap1(pt + v->it);
                    att = fm_minf(att + v->att_inc, 1.0f);
                    amp *= m;
                    idx = fl + (idx - fl) * id;
                    tine *= v->tine_dec;
                }
            } else {
                for (j = j0; j < j0 + seg; j++) {
                    float s = sn(pc + sn(pc * mr) * idx) * amp;
                    bl[j] += s * gl;
                    br[j] += s * gr;
                    pc = wrap1(pc + ic);
                    amp *= m;
                    idx = fl + (idx - fl) * id;
                }
                tine = 0.0f;
            }
            if (v->gate > 0)
                v->gate -= (int32_t)seg;
            j0 += seg;
        }
        v->pc = pc, v->pt = pt, v->amp = amp, v->idx = idx, v->tine = tine, v->att = att;
        if (amp < 0.0006f && tine < 0.0006f && att >= 1.0f)
            v->on = 0;
    }
}

static void render_bass(int d, uint32_t n, float lv)
{
    bass_t *b = &bs[d];
    uint32_t j;
    float a = onepole(520.0f), glide = 1.0f - tau(0.02f), idec = tau(0.07f), rel = tau(0.05f);
    float att = 1.0f / (0.004f * EN_SR), *L = dl[d], *R = dr[d];
    if (!b->on)
        return;
    for (j = 0; j < n; j++) {
        float s;
        b->inc += (b->tinc - b->inc) * glide;
        s = sn(b->ph + sn(b->ph) * b->idx) + b->h2 * sn(b->ph * 2.0f);
        b->ph = wrap1(b->ph + b->inc);
        if (b->gate > 0) {
            b->gate--;
            if (b->atk) {
                b->amp += att * b->peak;
                if (b->amp >= b->peak)
                    b->amp = b->peak, b->atk = 0;
            } else {
                b->amp = b->sus + (b->amp - b->sus) * b->dec;
            }
        } else {
            b->amp *= rel;
        }
        b->idx = b->idx0 + (b->idx - b->idx0) * idec;
        s *= b->amp;
        s = s * (1.0f - 0.1f * s * s);           /* a little warmth */
        b->lp += (s - b->lp) * a;
        L[j] += b->lp * lv;
        R[j] += b->lp * lv;
    }
    if (b->gate <= 0 && b->amp < 0.0005f)
        b->on = 0;
}

static void render_drums(int d, uint32_t n, float lv, float low, float sendg)
{
    kit_t *k = &kt[d];
    uint32_t j;
    float *L = dl[d], *R = dr[d];
    float fc = 2.0f * fm_sinf(FM_PI * 1150.0f / EN_SR), fs = 2.0f * fm_sinf(FM_PI * 1800.0f / EN_SR);
    float fr = 2.0f * fm_sinf(FM_PI * 1700.0f / EN_SR), fsh = 2.0f * fm_sinf(FM_PI * 7000.0f / EN_SR);
    float hha = onepole(7500.0f), hlp = onepole(12000.0f);
    int kon = k->kamp > 1e-3f, con = k->cl_amp > 1e-3f, son = k->sn_tamp > 1e-3f || k->sn_namp > 1e-3f;
    int ron = k->rm_amp > 1e-3f || k->rm_namp > 1e-3f, hon = k->ch_amp > 1e-3f || k->oh_amp > 1e-3f;
    int shon = k->sh_amp > 1e-3f, cgon = k->cg_amp > 1e-3f || k->cg_slap > 1e-3f, cvon = k->cv_amp > 1e-3f;
    float rmdec = tau(0.025f), rmndec = tau(0.010f), cldec = tau(0.004f), cltail = tau(0.13f), sndec = tau(0.05f);
    float snndec = tau(0.12f), shdec = tau(0.045f), cvdec = tau(0.035f), cgslap = tau(0.012f);
    if (!(kon | con | son | ron | hon | shon | cgon | cvon))
        return;
    for (j = 0; j < n; j++) {
        float kk = 0.0f, x = 0.0f, h = 0.0f, rv = 0.0f, nz;
        if (kon) {                               /* the kick: a sine falling in pitch; the low EQ takes its body */
            k->kf = k->kfend + (k->kf - k->kfend) * k->kpdec;
            k->kph = wrap1(k->kph + k->kf * (1.0f / EN_SR));
            kk = sn(k->kph) * k->kamp * low + noise() * k->kclick;
            k->kamp *= k->kadec;
            k->kclick *= 0.92f;
        }
        if (con) {                               /* the clap: three quick bursts, then the tail; band-passed */
            float e;
            if (k->cl_t == 380 || k->cl_t == 760)
                k->cl_b = 1.0f;
            if (k->cl_t == 1140)
                k->cl_tail = 0.75f;
            k->cl_t++;
            e = (k->cl_b + k->cl_tail) * k->cl_amp;
            k->cl_b *= cldec;
            k->cl_tail *= cltail;
            nz = noise() * e;
            k->cl_lo += fc * k->cl_bd;
            k->cl_bd += fc * (nz - k->cl_lo - 0.55f * k->cl_bd);
            x += k->cl_bd * 1.3f;
            rv += k->cl_bd;
            if (e < 1e-4f && k->cl_t > 1200)
                k->cl_amp = 0.0f;
        }
        if (son) {
            float hp;
            nz = noise() * k->sn_namp;
            k->sn_lo += fs * k->sn_bd;
            hp = nz - k->sn_lo - 0.9f * k->sn_bd;
            k->sn_bd += fs * hp;
            x += (k->sn_bd + 0.3f * hp) * 0.8f + sn(k->sn_ph) * k->sn_tamp;
            k->sn_ph = wrap1(k->sn_ph + k->sn_inc);
            k->sn_tamp *= sndec;
            k->sn_namp *= snndec;
            rv += k->sn_bd * 0.5f;
        }
        if (ron) {
            nz = noise() * k->rm_namp;
            k->rm_lo += fr * k->rm_bd;
            k->rm_bd += fr * (nz - k->rm_lo - 0.35f * k->rm_bd);
            x += (k->rm_bd * 0.55f + sn(k->rm_ph) * k->rm_amp) * 0.8f;
            k->rm_ph = wrap1(k->rm_ph + 430.0f / EN_SR);
            k->rm_amp *= rmdec;
            k->rm_namp *= rmndec;
            rv += k->rm_bd * 0.3f;
        }
        if (hon) {                               /* the hats: noise, two high-passes, a low-pass; closed and open */
            nz = noise();
            k->h1 += (nz - k->h1) * hha;
            nz -= k->h1;
            k->h2 += (nz - k->h2) * hha;
            nz -= k->h2;
            k->hlp += (nz - k->hlp) * hlp;
            h = k->hlp * (k->ch_amp + k->oh_amp);
            k->ch_amp *= k->ch_dec;
            k->oh_amp *= k->oh_dec;
        }
        if (shon) {                              /* the shaker: a band of high noise, swelling then gone */
            k->sh_env += (k->sh_up - k->sh_env) * 0.02f;
            k->sh_up *= shdec;
            nz = noise() * k->sh_env * k->sh_amp;
            k->sh_lp += fsh * k->sh_h;
            k->sh_h += fsh * (nz - k->sh_lp - 0.8f * k->sh_h);
            h += k->sh_h * 0.6f;
            if (k->sh_up < 1e-3f && k->sh_env < 1e-3f)
                k->sh_amp = 0.0f;
        }
        if (cgon) {                              /* the conga: a sine, its pitch settling; a slap */
            k->cg_inc += (k->cg_tinc - k->cg_inc) * 0.004f;
            k->cg_ph = wrap1(k->cg_ph + k->cg_inc);
            x += (sn(k->cg_ph) * k->cg_amp * (0.6f + 0.4f * low) + noise() * k->cg_slap * 0.4f) * 0.9f;
            k->cg_amp *= k->cg_dec;
            k->cg_slap *= cgslap;
        }
        if (cvon) {                              /* the clave: a hollow knock */
            k->cv_ph = wrap1(k->cv_ph + 2300.0f / EN_SR);
            x += sn(k->cv_ph) * k->cv_amp * 0.5f;
            k->cv_amp *= cvdec;
        }
        kk *= 0.9f;
        L[j] += (kk + x * 0.62f + h * 0.52f) * lv;
        R[j] += (kk + x * 0.58f + h * 0.66f) * lv;
        rsend[j] += rv * lv * sendg * 0.35f;
    }
    if (k->kamp < 1e-3f)
        k->kamp = 0.0f;
}

static void render_lead(uint32_t n, float lv)
{
    uint32_t j;
    if (ld.on) {                                 /* the hook: a soft FM voice, a little vibrato as it holds */
        for (j = 0; j < n; j++) {
            float s, vib = sn(ld.vph) * fm_minf(ld.vib, 1.0f) * 0.003f;
            ld.vph = wrap1(ld.vph + 5.0f / EN_SR);
            ld.vib += 1.0f / (0.4f * EN_SR);
            ld.inc += (ld.tinc - ld.inc) * 0.002f;
            s = sn(ld.ph + sn(ld.ph) * ld.idx);
            ld.ph = wrap1(ld.ph + ld.inc * (1.0f + vib));
            if (ld.att < 1.0f)
                ld.att = fm_minf(ld.att + 1.0f / (0.012f * EN_SR), 1.0f);
            if (ld.gate > 0) {
                ld.gate--;
                ld.amp = 0.7f * ld.peak + (ld.amp - 0.7f * ld.peak) * ld.dec;
            } else {
                ld.amp *= ld.rel;
            }
            ld.idx = 0.12f + (ld.idx - 0.12f) * 0.9997f;
            s *= ld.amp * ld.att * lv;
            lbuf[j] += s;
        }
        if (ld.gate <= 0 && ld.amp < 0.0005f)
            ld.on = 0;
    }
    if (vx.on) {                                 /* the vocal: a saw through two formants, scooped up to pitch */
        float g1 = fm_tanf(FM_PI * vx.f1 / EN_SR), g2 = fm_tanf(FM_PI * vx.f2 / EN_SR), k1 = 0.12f, k2 = 0.09f;
        float a1 = 1.0f / (1.0f + g1 * (g1 + k1)), a2 = 1.0f / (1.0f + g2 * (g2 + k2));
        for (j = 0; j < n; j++) {
            float saw, v1, v2, v3, bp1, bp2, t;
            vx.inc += (vx.tinc - vx.inc) * 0.0015f;
            vx.ph += vx.inc;
            if (vx.ph >= 1.0f)
                vx.ph -= 1.0f;
            saw = 2.0f * vx.ph - 1.0f;           /* (a polyBLEP takes the edge's alias off) */
            if (vx.ph < vx.inc) {
                t = vx.ph / vx.inc;
                saw -= t + t - t * t - 1.0f;
            } else if (vx.ph > 1.0f - vx.inc) {
                t = (vx.ph - 1.0f) / vx.inc;
                saw -= t * t + t + t + 1.0f;
            }
            v3 = saw - vx.s1b;
            v1 = a1 * (vx.s1a + g1 * v3);
            v2 = vx.s1b + g1 * v1;
            vx.s1a = 2.0f * v1 - vx.s1a;
            vx.s1b = 2.0f * v2 - vx.s1b;
            bp1 = v1;
            v3 = saw - vx.s2b;
            v1 = a2 * (vx.s2a + g2 * v3);
            v2 = vx.s2b + g2 * v1;
            vx.s2a = 2.0f * v1 - vx.s2a;
            vx.s2b = 2.0f * v2 - vx.s2b;
            bp2 = v1;
            if (vx.att < 1.0f)
                vx.att = fm_minf(vx.att + 1.0f / (0.006f * EN_SR), 1.0f);
            if (vx.gate > 0)
                vx.gate--;
            else
                vx.amp *= vx.rel;
            lbuf[j] += (bp1 * k1 + bp2 * k2 * vx.g2 * 2.0f) * vx.amp * vx.att * lv * 1.6f;
        }
        if (vx.gate <= 0 && vx.amp < 0.0005f)
            vx.on = 0;
    }
}

static void svf_coef(float fc, float k, float *g, float *a1)
{
    fc = fm_clampf(fc, 20.0f, 19000.0f);
    *g = fm_tanf(FM_PI * fc / EN_SR);
    *a1 = 1.0f / (1.0f + *g * (*g + k));
}
/* a DJ filter on a stereo pair: f < 0 low-pass (down to 140 Hz), f > 0 high-pass (up to 6 kHz), about 0 open */
static void djf(float *L, float *R, uint32_t n, float f, float *st)
{
    uint32_t j;
    float fc, g, a1, k = 1.1f;
    int hp = f > 0.0f;
    if (fm_fabsf(f) < 0.01f) {
        st[0] = st[1] = st[2] = st[3] = 0.0f;
        return;
    }
    fc = hp ? 25.0f * fm_exp2f(8.0f * f) : 18000.0f * fm_exp2f(7.0f * f);
    svf_coef(fc, k, &g, &a1);
    for (j = 0; j < n; j++) {
        float x = L[j], v3 = x - st[1], v1 = a1 * (st[0] + g * v3), v2 = st[1] + g * v1;
        st[0] = 2.0f * v1 - st[0];
        st[1] = 2.0f * v2 - st[1];
        L[j] = hp ? x - k * v1 - v2 : v2;
        x = R[j];
        v3 = x - st[3];
        v1 = a1 * (st[2] + g * v3);
        v2 = st[3] + g * v1;
        st[2] = 2.0f * v1 - st[2];
        st[3] = 2.0f * v2 - st[3];
        R[j] = hp ? x - k * v1 - v2 : v2;
    }
}

static void render_space(uint32_t n, float *L, float *R)
{
    uint32_t j;
    float sp = (float)par[P_SPACE] * 0.01f, vs = 0.25f + 0.6f * sp, damp = 0.5f - 0.15f * sp, g = 0.70f + 0.14f * sp;
    float ea = onepole(2400.0f), fb = 0.36f + 0.24f * (float)tk[en_master].echo * 0.01f;
    float elen2 = fm_minf(ec_len * 2.0f, (float)(EC_N - 4));
    for (j = 0; j < n; j++) {
        float in = rsend[j] * vs, x[4], sum, yl, yr, e, e2, rd;
        int i, ri;
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
        rd = (float)ec_w - ec_len;
        if (rd < 0.0f)
            rd += (float)EC_N;
        ri = (int)rd;
        e = (float)ec_buf[ri] * (1.0f / 16384.0f);
        rd = (float)ec_w - elen2;
        if (rd < 0.0f)
            rd += (float)EC_N;
        ri = (int)rd;
        e2 = (float)ec_buf[ri] * (1.0f / 16384.0f);
        ec_lp += (e - ec_lp) * ea;
        {
            float w = esend[j] + ec_lp * fb;
            w = w > 1.9f ? 1.9f : w < -1.9f ? -1.9f : w;
            ec_buf[ec_w] = (int16_t)(w * 16384.0f);
            if (++ec_w >= EC_N)
                ec_w = 0;
        }
        L[j] += yl * 0.8f + ec_lp * 0.55f + e2 * 0.12f;
        R[j] += yr * 0.8f + ec_lp * 0.30f + e2 * 0.36f;
    }
}

static void control(uint32_t n)
{
    float k = (float)n / EN_SR;
    duck[0] *= fm_expf(-k * 7.0f);
    duck[1] *= fm_expf(-k * 7.0f);
    if (++vu_n >= 8u) {                          /* the meters: about 46 ms of RMS, then ballistics */
        float l = fm_sqrtf(vu_acc_l / (float)(n * 8u)), r = fm_sqrtf(vu_acc_r / (float)(n * 8u));
        int d;
        en_vu_l = en_vu_l + (fm_minf(l * 4.0f, 1.2f) - en_vu_l) * 0.45f;
        en_vu_r = en_vu_r + (fm_minf(r * 4.0f, 1.2f) - en_vu_r) * 0.45f;
        for (d = 0; d < 2; d++) {
            float m = fm_minf(fm_sqrtf(mt_acc[d] / (float)(n * 8u)) * 4.0f, 1.2f);
            en_deck[d].meter += (m - en_deck[d].meter) * (m > en_deck[d].meter ? 0.6f : 0.3f);
            mt_acc[d] = 0.0f;
        }
        vu_acc_l = vu_acc_r = 0.0f;
        vu_n = 0;
    }
}

static void fire(ev_t *e)
{
    int d = e->deck;
    kit_t *k = &kt[d];
    float v = e->vel;
    switch (e->type) {
    case E_KEY: key_on(d, e->x, e->note, v, e->gate, 0, e->pan); break;
    case E_PAD: key_on(d, K_PAD, e->note, v, e->gate, 0, e->pan); break;
    case E_BASS: bass_on(d, e->x, e->note, v, e->gate); break;
    case E_KICK:
        k->kph = 0.0f;
        k->kf = 165.0f;
        k->kfend = 49.0f;
        k->kpdec = tau(0.032f);
        k->kamp = v;
        k->kadec = tau(0.28f);
        k->kclick = v * 0.18f;
        duck[d] = 1.0f;
        break;
    case E_CLAP:
        k->cl_amp = v;
        k->cl_b = 1.0f;
        k->cl_tail = 0.0f;
        k->cl_t = 0;
        break;
    case E_SNARE:
        k->sn_ph = 0.0f;
        k->sn_inc = (190.0f + 10.0f * frand()) / EN_SR;
        k->sn_tamp = v * 0.5f;
        k->sn_namp = v;
        break;
    case E_RIM:
        k->rm_ph = 0.0f;
        k->rm_amp = v * 0.7f;
        k->rm_namp = v;
        break;
    case E_CHAT:
        k->ch_amp = v;
        k->ch_dec = tau(0.028f + 0.012f * frand());
        break;
    case E_OHAT:
        k->oh_amp = v * 0.85f;
        k->oh_dec = tau(0.16f);
        k->ch_amp *= 0.3f;
        break;
    case E_SHAKER:
        k->sh_amp = v;
        k->sh_up = 1.0f;
        break;
    case E_CONGA: {
        static const float F[3] = {262.0f, 370.0f, 340.0f};
        float f = F[e->note < 3 ? e->note : 0];
        k->cg_tinc = f / EN_SR;
        k->cg_inc = f * 1.12f / EN_SR;
        k->cg_ph = 0.0f;
        k->cg_amp = e->note == 2 ? v * 0.35f : v;
        k->cg_dec = tau(e->note == 2 ? 0.04f : 0.17f);
        k->cg_slap = e->note == 2 ? v * 0.8f : v * 0.15f;
        break;
    }
    case E_CLAVE:
        k->cv_ph = 0.0f;
        k->cv_amp = v;
        break;
    case E_LEAD: {
        float f = mtof((float)e->note);
        if (!ld.on || ld.amp < 0.02f)
            ld.inc = f / EN_SR;
        ld.tinc = f / EN_SR;
        ld.on = 1;
        ld.deck = (uint8_t)d;
        ld.peak = v;
        ld.amp = v;
        ld.att = 0.0f;
        ld.gate = e->gate;
        ld.dec = tau(0.25f);
        ld.rel = tau(0.12f);
        ld.vib = 0.0f;
        ld.idx = 0.45f;
        break;
    }
    case E_VOX: {
        float f = mtof((float)e->note);
        const float *w = VOWEL[e->x < 5 ? e->x : 0];
        vx.tinc = f / EN_SR;
        vx.inc = f * 0.955f / EN_SR;             /* a little scoop up to the note */
        vx.on = 1;
        vx.deck = (uint8_t)d;
        vx.amp = v;
        vx.att = 0.0f;
        vx.gate = e->gate;
        vx.rel = tau(0.06f);
        vx.f1 = w[0];
        vx.f2 = w[1];
        vx.g2 = w[2];
        break;
    }
    default: break;
    }
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 1.6f;
    drain();
    while (n) {
        uint32_t k = n > BLK ? BLK : n, j;
        int i, d;
        float g[2], lvc = (float)par[P_CHORDS] * 0.01f * 0.27f, lvb = (float)par[P_BASS] * 0.01f * 0.52f;
        float lvd = (float)par[P_DRUMS] * 0.01f * 0.62f, lvl = (float)par[P_LEAD] * 0.01f * 0.22f;
        float warm = (float)par[P_WARMTH] * 0.01f, wa = onepole(16000.0f * fm_exp2f(-2.2f * warm));
        float drive = 1.0f + 0.9f * warm, hp_a = onepole(25.0f), lv_rel = tau(0.4f), lv_att = 1.0f - tau(0.002f);
        if (en_playing) {
            if (step_left <= 0.0f) {
                seq_step();
                step_left += step_len;
            }
            if ((float)k > step_left)
                k = (uint32_t)step_left + 1u;
        }
        for (i = 0; i < NEV; i++)                /* events due now fire; the chunk ends at the next one */
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
        for (i = 0; i < NEV; i++)
            if (evq[i].used)
                evq[i].wait -= (int32_t)k;
        if (en_playing)
            step_left -= (float)k;
        dj_hands(k);
        control(k);
        for (d = 0; d < 2; d++) {
            en_deck_t *dk = &en_deck[d];
            float xg = d == 0 ? fm_minf(1.0f, 2.0f * (1.0f - en_xfade)) : fm_minf(1.0f, 2.0f * en_xfade);
            g[d] = dk->fader * xg;
        }
        for (j = 0; j < k; j++) {
            dl[0][j] = dr[0][j] = dl[1][j] = dr[1][j] = 0.0f;
            cl[0][j] = cr[0][j] = cl[1][j] = cr[1][j] = 0.0f;
            lbuf[j] = rsend[j] = esend[j] = 0.0f;
        }
        render_keys(k);
        for (d = 0; d < 2; d++) {                /* the chords into their deck: ducked by its kick, to the hall */
            float lv = lvc * (1.0f - 0.3f * duck[d]), es = g[d] * (float)tk[d].echo * 0.01f * 0.5f, rs = g[d] * 0.5f;
            for (j = 0; j < k; j++) {
                float l = cl[d][j] * lv, r = cr[d][j] * lv;
                dl[d][j] += l;
                dr[d][j] += r;
                rsend[j] += (l + r) * rs;
                esend[j] += (l + r) * es;
            }
            render_bass(d, k, lvb * en_deck[d].low * (1.0f - 0.15f * duck[d]));
            render_drums(d, k, lvd, en_deck[d].low, g[d]);
        }
        render_lead(k, lvl);
        if (ld.on || vx.on) {
            int ldk = ld.on ? ld.deck : vx.deck;
            for (j = 0; j < k; j++) {
                float x = lbuf[j];
                dl[ldk][j] += x * 0.95f;
                dr[ldk][j] += x * 0.8f;
                esend[j] += x * g[ldk] * 0.55f;
                rsend[j] += x * g[ldk] * 0.6f;
            }
        }
        {                                        /* the mixer: each deck through its filter and fader */
            static float ml[BLK], mr[BLK];
            for (j = 0; j < k; j++)
                ml[j] = mr[j] = 0.0f;
            for (d = 0; d < 2; d++) {
                float gg = g[d] * fade, acc = 0.0f;
                if (!en_deck[d].on && gg < 1e-4f && !bs[d].on)
                    continue;
                djf(dl[d], dr[d], k, en_deck[d].filter, df[d]);
                for (j = 0; j < k; j++) {
                    float l = dl[d][j] * gg, r = dr[d][j] * gg;
                    ml[j] += l;
                    mr[j] += r;
                    acc += l * l + r * r;
                }
                mt_acc[d] += acc * 0.5f;
            }
            render_space(k, ml, mr);
            djf(ml, mr, k, (float)par[P_FILTER] * 0.02f, mf);
            for (j = 0; j < k; j++) {            /* warmth, a DC cut, the leveller, MASTER */
                float l = ml[j], r = mr[j], x, vol;
                warm_l += (l - warm_l) * wa;
                warm_r += (r - warm_r) * wa;
                l = warm_l * drive;
                r = warm_r * drive;
                l = l * (27.0f + l * l) / (27.0f + 9.0f * l * l) / drive;
                r = r * (27.0f + r * r) / (27.0f + 9.0f * r * r) / drive;
                hp_l += (l - hp_l) * hp_a;
                hp_r += (r - hp_r) * hp_a;
                l -= hp_l;
                r -= hp_r;
                vu_acc_l += l * l;
                vu_acc_r += r * r;
                x = fm_maxf(fm_fabsf(l), fm_fabsf(r)) * gain;
                lev = x > lev ? lev + (x - lev) * lv_att : lev * lv_rel;
                vol = lev > 0.5f ? 0.5f / lev : 1.0f;
                l *= gain * vol;
                r *= gain * vol;
                out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
                out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
            }
        }
        out += 2 * k;
        n -= k;
    }
}
