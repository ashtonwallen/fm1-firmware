/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CITY POP: ashtonwallen, 2026 */
/* The instrument (citypop.h). One compilation unit, -O2. The command queue and the render's shape are AMB-1's,
 * by way of LOFI RADIO. */
#include "citypop.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

#ifdef EN_HOST
void (*en_log)(int kind, int bar, int step, int note, int vel, int dur);
#define LOG(k, n, v, d)                                                                                            \
    do {                                                                                                           \
        if (en_log)                                                                                                \
            en_log((k), sg.bar, sg.step, (n), (int)((v) * 100.0f), (d));                                           \
    } while (0)
#else
#define LOG(k, n, v, d) ((void)0)
#endif

const char *const EN_NOTE_NAME[12] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Tempo", 90, 124, 108}, {"Sparkle", 0, 100, 60}, {"Groove", 0, 100, 55}, {"Gloss", 0, 100, 55},
    {"Keys", 0, 100, 70},    {"Guitar", 0, 100, 65},  {"Bass", 0, 100, 70},   {"Drums", 0, 100, 70},
    {"Lead", 0, 100, 70},    {"Brass", 0, 100, 50},   {"Strings", 0, 100, 55},
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
    if (i == P_GROOVE)                            /* how busy the bass and the guitar are */
        put_s(buf, v < 20 ? "Laid" : v < 40 ? "Easy" : v < 60 ? "Smooth" : v < 80 ? "Busy" : "Funky");
    else
        put_u(buf, (uint32_t)(v < 0 ? 0 : v));
}

static const char *const SEC_NAME[SEC_N] = {"INTRO", "VERSE", "PRE-CHORUS", "CHORUS", "INTERLUDE",
                                            "BRIDGE", "SOLO", "KEY UP", "FADE OUT", "AUTO REVERSE"};
const char *en_section_name(int s) { return SEC_NAME[s < 0 || s >= SEC_N ? 0 : s]; }

/* ------------------------------------------------------------ harmony --- */
enum { Q_MAJ7, Q_MAJ9, Q_M7, Q_M9, Q_M11, Q_7, Q_9, Q_13, Q_7SUS, Q_7B9, Q_M7B5, Q_ADD9, Q_69, Q_NQ };
static const char *const Q_NAME[Q_NQ] = {"maj7", "maj9", "m7",  "m9",  "m11", "7",  "9",
                                         "13",   "7sus4", "7b9", "m7b5", "add9", "6/9"};
/* the keys' (and the guitar's, the strings') four notes: semitones above the chord's root. The bass has the root */
static const int8_t Q_VOICE[Q_NQ][4] = {
    {4, 7, 11, 14}, {4, 7, 11, 14}, {0, 3, 7, 10}, {3, 7, 10, 14}, {3, 10, 14, 17}, {0, 4, 7, 10}, {4, 7, 10, 14},
    {4, 10, 14, 21}, {5, 7, 10, 14}, {4, 7, 10, 13}, {0, 3, 6, 10}, {0, 4, 7, 14}, {4, 9, 14, 19},
};
#define B_(x) (1u << (x))
/* the melody's notes on each: the chord's own tones, and the tensions a singer leans on (the 9th, the 11th) */
static const uint16_t Q_CORE[Q_NQ] = {
    B_(0) | B_(4) | B_(7) | B_(11), B_(0) | B_(4) | B_(7) | B_(11), B_(0) | B_(3) | B_(7) | B_(10),
    B_(0) | B_(3) | B_(7) | B_(10), B_(0) | B_(3) | B_(7) | B_(10), B_(0) | B_(4) | B_(7) | B_(10),
    B_(0) | B_(4) | B_(7) | B_(10), B_(0) | B_(4) | B_(7) | B_(10), B_(0) | B_(5) | B_(7) | B_(10),
    B_(0) | B_(4) | B_(7) | B_(10), B_(0) | B_(3) | B_(6) | B_(10), B_(0) | B_(4) | B_(7),
    B_(0) | B_(4) | B_(7) | B_(9),
};
static const uint16_t Q_TENS[Q_NQ] = {
    B_(2), B_(2), B_(2) | B_(5), B_(2) | B_(5), B_(2) | B_(5), B_(2) | B_(9), B_(2) | B_(9), B_(2) | B_(9),
    B_(2), 0, B_(5), B_(2), B_(2),
};
const char *en_chord_name(int root, int q, int bass, char *buf)
{
    char *e;
    root = (root % 12 + 12) % 12;
    e = put_s(put_s(buf, EN_NOTE_NAME[root]), Q_NAME[q < 0 || q >= Q_NQ ? 0 : q]);
    if (bass >= 0 && (bass % 12 + 12) % 12 != root)
        put_s(put_s(e, "/"), EN_NOTE_NAME[(bass % 12 + 12) % 12]);
    return buf;
}

/* progressions, all in a major key: chords as (semitones above the tonic, quality, bass (-1: the root), beats).
 * Four bars each (16 beats). Curated from the genre's idioms; nothing is invented on the fly */
typedef struct {
    int8_t r, q, b, beats;
} pch_t;
typedef struct {
    uint8_t n;
    pch_t c[7];
} prog_t;
#define C_(r, q, n) {r, q, -1, n}
#define S_(r, q, b, n) {r, q, b, n}
/* the verses (and the intro, the interlude) */
static const prog_t VERSES[] = {
    {4, {C_(0, Q_MAJ9, 4), C_(5, Q_MAJ9, 4), C_(0, Q_MAJ9, 4), S_(5, Q_MAJ7, 7, 4)}},               /* I IV I IV/V */
    {4, {C_(0, Q_MAJ7, 4), C_(9, Q_M9, 4), C_(2, Q_M9, 4), C_(7, Q_13, 4)}},                        /* I vi ii V */
    {5, {C_(5, Q_MAJ7, 4), C_(4, Q_7, 4), C_(9, Q_M7, 4), C_(7, Q_M7, 2), C_(0, Q_9, 2)}},          /* IV III7 vi v I7 */
    {4, {C_(9, Q_M9, 4), C_(2, Q_M9, 4), C_(9, Q_M9, 4), S_(5, Q_MAJ7, 7, 4)}},                     /* vi ii vi IV/V */
    {5, {C_(5, Q_MAJ7, 4), C_(4, Q_M7, 4), C_(2, Q_M7, 4), C_(0, Q_MAJ7, 2), C_(0, Q_9, 2)}},       /* IV iii ii I I7 */
    {5, {C_(0, Q_MAJ7, 4), C_(4, Q_M7, 4), C_(5, Q_MAJ7, 4), C_(7, Q_7SUS, 2), C_(7, Q_7, 2)}},     /* I iii IV V */
    {4, {C_(2, Q_M9, 4), C_(7, Q_13, 4), C_(0, Q_MAJ9, 4), C_(9, Q_7B9, 4)}},                       /* ii V I VI7 */
};
#define NVERSES (int)(sizeof VERSES / sizeof VERSES[0])
/* the pre-choruses: three bars; the fourth is the way into the chorus (lift()) */
static const prog_t PRES[] = {
    {3, {C_(5, Q_MAJ7, 4), C_(4, Q_M7, 4), C_(2, Q_M7, 4)}},                                        /* IV iii ii */
    {3, {C_(2, Q_M7, 4), C_(4, Q_M7, 4), C_(5, Q_MAJ7, 4)}},                                        /* ii iii IV */
    {3, {C_(5, Q_MAJ7, 4), C_(5, Q_M7, 4), C_(4, Q_M7, 4)}},                                        /* IV iv iii */
    {3, {C_(9, Q_M7, 4), C_(5, Q_MAJ7, 4), C_(2, Q_M9, 4)}},                                        /* vi IV ii */
    {3, {C_(5, Q_MAJ7, 4), C_(8, Q_MAJ7, 4), C_(10, Q_ADD9, 4)}},                                   /* IV bVI bVII */
};
#define NPRES (int)(sizeof PRES / sizeof PRES[0])
/* the choruses */
static const prog_t CHORUSES[] = {
    {4, {C_(5, Q_MAJ7, 4), C_(7, Q_13, 4), C_(4, Q_M7, 4), C_(9, Q_M9, 4)}},                        /* the royal road */
    {7, {C_(5, Q_MAJ7, 2), C_(7, Q_9, 2), C_(4, Q_M7, 2), C_(9, Q_M7, 2), C_(2, Q_M9, 2), C_(7, Q_7SUS, 2),
         C_(0, Q_MAJ9, 4)}},                                                                          /* royal road, home */
    {5, {C_(5, Q_MAJ7, 4), C_(4, Q_7, 4), C_(9, Q_M9, 4), C_(7, Q_M7, 2), C_(0, Q_9, 2)}},          /* IV III7 vi v I7 */
    {4, {C_(2, Q_M9, 4), C_(7, Q_13, 4), C_(0, Q_MAJ9, 4), C_(9, Q_7B9, 4)}},                       /* ii V I VI7 */
    {5, {C_(0, Q_MAJ9, 4), C_(4, Q_M7, 2), C_(9, Q_7, 2), C_(2, Q_M9, 4), S_(5, Q_MAJ7, 7, 4)}},    /* I iii VI7 ii IV/V */
    {5, {C_(5, Q_MAJ9, 4), C_(4, Q_M7, 4), C_(2, Q_M9, 4), C_(0, Q_MAJ7, 2), C_(0, Q_9, 2)}},       /* IV iii ii I I7 */
    {4, {C_(9, Q_M9, 4), C_(5, Q_MAJ7, 4), C_(7, Q_13, 4), C_(4, Q_7, 4)}},                         /* vi IV V III7 */
    {4, {C_(5, Q_MAJ7, 4), S_(5, Q_MAJ7, 7, 4), C_(4, Q_M7, 4), C_(9, Q_M9, 4)}},                   /* IV IV/V iii vi */
};
#define NCHORUSES (int)(sizeof CHORUSES / sizeof CHORUSES[0])
/* the bridges: twice; the second time its last bar is the lift into the last chorus, a key up */
static const prog_t BRIDGES[] = {
    {4, {C_(8, Q_MAJ7, 4), C_(10, Q_ADD9, 4), C_(0, Q_MAJ7, 4), C_(0, Q_MAJ7, 4)}},                 /* bVI bVII I */
    {5, {C_(4, Q_M7, 4), C_(9, Q_7, 4), C_(2, Q_M7, 4), C_(7, Q_7SUS, 2), C_(7, Q_7, 2)}},          /* iii VI7 ii V */
    {4, {C_(5, Q_MAJ7, 4), C_(5, Q_M7, 4), C_(4, Q_M7, 4), C_(9, Q_7B9, 4)}},                       /* IV iv iii VI7 */
    {4, {C_(2, Q_M9, 4), C_(4, Q_M7, 4), C_(5, Q_MAJ7, 4), S_(5, Q_MAJ7, 7, 4)}},                   /* ii iii IV IV/V */
};
#define NBRIDGES (int)(sizeof BRIDGES / sizeof BRIDGES[0])

/* ------------------------------------------------------------- grooves --- */
/* a bar each. Kick: X, x softer. Snare: X the backbeat, g a ghost. Hats: X accent, x, s soft, O open.
 * Bass: R root (the slash chord's bass), O octave, F fifth, P a pop (slap; fingered: the octave), A the way
 * into the next chord (else the root), g a ghost; lower case only when Groove is up. The kick and the bass
 * hit together: the band locks. cut: the guitar's cutting; comp: the keys' */
enum { F_BALLAD, F_MID, F_DANCE, F_FUNK };
typedef struct {
    uint8_t feel;
    const char *k, *s, *h, *b;
    uint8_t cut, comp;
} groove_t;
static const groove_t GROOVES[] = {
    {F_MID, "X.....X...X..x..", "....X.......X...", "XsxsXsxsXsxsXsxs", "R.....R.r.R..o.A", 0, 1},
    {F_MID, "X......X..X.....", "....X.......X..g", "XsxsXsxsXsxsXsxs", "R..r...R..R.o..A", 1, 2},
    {F_MID, "X.......X.x.....", "....X.......X...", "x.xsx.xsx.xsx.xs", "R...r..oR.R...A.", 0, 1},
    {F_DANCE, "X...X...X...X...", "....X.......X...", "xsOsxsOsxsOsxsOs", "R.o.R.O.R.o.R.Oa", 2, 3},
    {F_DANCE, "X...X...X...X...", "....X.......X...", "XsxsXsxsXsxsXsxs", "R..r..O.R..r.OgA", 3, 3},
    {F_FUNK, "X..X....X.X.....", "....X..g....X...", "XsxsXsxsXsxsXsxs", "R.gR.P.gR.R.gp.a", 3, 1},
    {F_FUNK, "X.....X.X.....X.", "....X.......X.g.", "XsxsXsxsXsxsXsxs", "R.gP.gR.R.gp.gRA", 1, 1},
    {F_BALLAD, "X.......X.x.....", "....X.......X...", "x.x.x.x.x.x.x.x.", "R.......R.r...A.", 4, 0},
    {F_BALLAD, "X.........X.....", "....X.......X...", "x.x.x.x.x.x.x.xs", "R.........R...o.", 4, 4},
};
#define NGROOVES (int)(sizeof GROOVES / sizeof GROOVES[0])
/* the guitar's cutting: X an accented chop (the backbeat), x a chop, c only with Groove up, g a muted scratch */
static const char *const CUTS[] = {"..c.X.gx..c.X.gx", "x.gxX.gxg.xgX.gx", "..X...X...X..cX.", "gcgxXgcgxgcxXgcx",
                                   "....X.......X..."};
#define NCUTS (int)(sizeof CUTS / sizeof CUTS[0])
/* its arpeggios: which string (0 low .. 3 high) */
static const char *const ARPS[] = {"0.1.2.3.2.1.3.2.", "0.2.1.3.0.2.1.3.", "0123210301232103", "0...1...2...3..."};
/* the keys' comping: hits in a bar (step, length in steps, velocity %) */
typedef struct {
    int8_t n;
    int8_t st[5], len[5], vel[5];
} comp_t;
static const comp_t COMPS[] = {
    {2, {0, 8}, {8, 8}, {80, 66}},                                  /* half notes: ballads */
    {3, {0, 6, 10}, {6, 4, 6}, {80, 64, 70}},                       /* a syncopated three */
    {5, {0, 3, 6, 10, 12}, {3, 3, 4, 2, 4}, {80, 60, 70, 58, 66}}, /* sixteenths, pushed */
    {4, {2, 6, 10, 14}, {2, 2, 2, 2}, {70, 76, 70, 78}},           /* the off-beats: dance */
    {1, {0}, {16}, {78}},                                           /* a whole bar */
};
/* the brass: an answer of eight steps in the melody's breath (X a stab, L held) */
static const char *const RIFFS[] = {"X.....X.", "..X.X...", "X..X..L.", "....X.X.", "L.......", "X.X...X."};
#define NRIFFS (int)(sizeof RIFFS / sizeof RIFFS[0])

/* the melody's rhythms: two bars of sixteenths. X a strong note, x a note, - held, . a rest (a breath).
 * A note is strong too on beats 1 and 3, or held a beat or more */
static const char *const CELL_VERSE[] = {
    "..x-x-x-X---x-x-X-------........", "x-x-x.x-X-------..x-x.x-X-----..", "....x-x-x-x-X---x-x-X-x-X---....",
    "..x.x-x.X---..x-x.x-x-X-------..", "x-x-X---x-x-X-----..x-x-X-----..", "..x-x-X-x-x-X-----..x.x-X-----..",
};
static const char *const CELL_PRE[] = {
    "..x-x-x-X-----..x-x-x-X-------..", "x-x-x-x-X---x-x-X-------x-x-x-..", "..x-x-x-x-x-X---..x-x-x-X-----..",
};
static const char *const CELL_HOOK[] = {
    "X-----x-x-X---x-X-------....x-x-", "..x-x-X-----x-X-----x-x-X-----..", "X---x-x-X-----x-X-------..x-x-x-",
    "x-x-X---x-x-X---x-x-X-x-X-----..", "X-----X-----x-x-X-------..x.x-x-", "..x-X-----x-X---x-X---x-X-----..",
};
static const char *const CELL_BRIDGE[] = {
    "X-------x-x-X---X-----------....", "..x-x-X-------x-x-X-----------..", "x-x-x-X-------..x-x-x-X-------..",
};
static const char *const CELL_SOLO[] = {
    "x-xxx-x-X---x.xxX-x-x-xxX-----..", "..xxx-x-xxx-X---x-x-xxx-X-----..", "x-x-xxx-X-xxx-x-X---x-x-xxX---..",
};
#define NA(a) (int)(sizeof a / sizeof a[0])

/* ------------------------------------------------------------ styles --- */
/* the user's eleven (Tempo Sparkle Groove Gloss, Keys Guitar Bass Drums, Lead Brass Strings) and the style's own
 * flavour: keys 0 Rhodes, 1 DX, 2 either; lead 0 synth, 1 sax, 2 mostly synth; bass 0 fingered, 1 slap, 2 now and
 * then slap; feel (grooves); gtr 0 arpeggios, 1 cutting, 2 arpeggios in the verses, cutting in the choruses;
 * rim: the verses on the cross-stick; lush: strings in the bridge and the second verse too; verses, choruses:
 * which progressions (bit i); sky: the screen's colours */
typedef struct {
    const char *name;
    int8_t v[P_NSTYLE];
    uint8_t keys, lead, bass, feel, gtr, rim, lush;
    uint8_t verses, choruses;
    uint8_t sky;
} style_t;
static const style_t STYLES[EN_NSTYLES] = {
    /*                    Tem Spk Grv Gls  Key Gtr Bas Drm  Led Brs Str   keys lead bass feel gtr rim lush  verses choruses sky */
    {"Sunroof",          {108, 60, 55, 55, 70, 65, 70, 70, 70, 50, 55},   0, 0, 0, F_MID, 2, 0, 0, 0x7F, 0xFF, 0},
    {"E.Piano 1",        {114, 72, 55, 60, 80, 45, 70, 70, 70, 45, 45},   1, 0, 0, F_MID, 1, 0, 0, 0x37, 0x9B, 1},
    {"Thumb and Pop",    {116, 65, 80, 40, 60, 75, 80, 75, 65, 70, 35},   2, 0, 1, F_FUNK, 1, 0, 0, 0x6E, 0x5D, 2},
    {"Curly Cord",       {96, 45, 30, 70, 75, 55, 65, 55, 70, 15, 75},    0, 1, 0, F_BALLAD, 0, 1, 1, 0x1B, 0xA3, 3},
    {"Bubble Economy",   {120, 70, 65, 70, 70, 55, 75, 80, 70, 75, 70},   1, 0, 0, F_DANCE, 1, 0, 1, 0x67, 0x87, 4},
    {"Chorus Pedal",     {110, 60, 60, 70, 45, 85, 70, 70, 65, 30, 40},   0, 0, 0, F_MID, 2, 0, 0, 0x3D, 0xB5, 5},
    {"Highball",         {104, 50, 50, 55, 75, 50, 70, 65, 70, 40, 45},   0, 1, 0, F_MID, 0, 1, 0, 0x52, 0x1A, 0},
    {"Pocket Bell",      {118, 85, 60, 55, 75, 55, 70, 75, 70, 55, 50},   1, 0, 0, F_MID, 1, 0, 0, 0x27, 0xA7, 1},
    {"Melon Soda",       {112, 65, 55, 50, 65, 70, 70, 70, 70, 55, 40},   0, 1, 0, F_MID, 1, 0, 0, 0x2F, 0x37, 2},
    {"Tinted Windows",   {100, 45, 55, 65, 70, 55, 75, 70, 70, 30, 60},   0, 0, 2, F_MID, 2, 1, 1, 0x4A, 0xC5, 3},
    {"Rental Yacht",     {98, 55, 40, 75, 70, 55, 65, 60, 70, 25, 80},    2, 1, 0, F_BALLAD, 0, 1, 1, 0x1D, 0xB3, 4},
    {"Mirrorball Debt",  {120, 70, 75, 55, 60, 65, 80, 80, 65, 70, 60},   1, 0, 1, F_DANCE, 1, 0, 0, 0x26, 0x4D, 5},
    {"Escalator Kiss",   {106, 65, 50, 60, 70, 60, 70, 70, 70, 45, 55},   1, 2, 0, F_MID, 2, 0, 0, 0x77, 0xEF, 0},
};
static int sidx(int i) { return (i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES; }
const char *en_style_name(int i) { return STYLES[sidx(i)].name; }
uint8_t en_style_sky(int i) { return STYLES[sidx(i)].sky; }
void en_style_recipe(int i, int16_t *par)
{
    int k;
    const style_t *s = &STYLES[sidx(i)];
    for (k = 0; k < P_NSTYLE; k++)
        par[k] = s->v[k];
}

/* ------------------------------------------------------------- titles --- */
static const char *const T_ADJ[] = {
    "Ultramarine", "Lime", "Pastel", "Lacquer", "Magenta", "Vanilla", "Lilac", "Apricot", "Marine", "Mint", "Menthol",
    "Topaz", "Peach", "Platinum", "Papaya", "Swimming", "Grapefruit", "Weekend", "Sparkling", "Second", "Turquoise",
    "Holographic", "Ivory", "Aqua", "Champagne", "Saffron", "Indigo", "Saturday", "Mirror", "Rosewater"};
static const char *const T_NOUN[] = {
    "Telephone", "Escalator", "Coupe", "Freeway", "Convertible", "Marina", "Boulevard", "Cocktail", "Resort",
    "Poolside", "Sunglasses", "Earring", "Overpass", "Hydrofoil", "Heatwave", "Lobby", "Monorail", "Airmail",
    "Snapshot", "Daydream", "Sundress", "Skylight", "Romance", "Runway", "Ribbon", "Holiday", "Weekender", "Promise",
    "Hairpin", "Gondola", "Terrace", "Rendezvous", "Sherbet", "Atrium", "Hatchback", "Bikini"};
static const char *const T_TAIL[] = {"for Two", "Again", "Forever", "in Blue", "Deluxe", "Dancer", "Line",
                                     "Hotel", "Magic", "Shower"};

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
    const char *a = T_ADJ[h % (uint32_t)NA(T_ADJ)], *n = T_NOUN[(h >> 8) % (uint32_t)NA(T_NOUN)];
    const char *n2 = T_NOUN[(h >> 16) % (uint32_t)NA(T_NOUN)], *t = T_TAIL[(h >> 20) % (uint32_t)NA(T_TAIL)];
    if (n2 == n)
        n2 = T_NOUN[((h >> 16) + 1u) % (uint32_t)NA(T_NOUN)];
    switch ((h >> 26) % 8u) {
    case 0: case 1: case 2: case 3:                  /* "Lime Escalator" */
        put_s(put_s(put_s(b, a), " "), n);
        break;
    case 4: case 5:                                  /* "Marina Deluxe" */
        put_s(put_s(put_s(b, n), " "), t);
        break;
    default:                                         /* "Telephone Heatwave" */
        put_s(put_s(put_s(b, n), " "), n2);
        break;
    }
}
uint8_t en_sky_colour(uint32_t seed) { return (uint8_t)(en_track_rand(seed, 5u) % 6u); }

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
volatile uint8_t en_playing, en_phase, en_repeat_on;
volatile uint32_t en_track_no, en_track_seed, en_seed_base = 0x43495459u, en_track_ms, en_track_len_ms;
volatile uint8_t en_key, en_key_up, en_bpm = 108, en_chord_root, en_chord_q, en_chord_bass, en_section;
volatile uint8_t en_section_no, en_section_bar, en_section_bars, en_lead_sax;
volatile uint16_t en_chord_pcs;
volatile float en_vu_l, en_vu_r, en_kick_glow, en_lead_glow, en_band[5];
volatile uint32_t en_beats, en_tracks_played;

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
static float trand(void) { return (float)(xs(&trng) >> 8) * (1.0f / 16777216.0f); }   /* the song's own */
static int tirand(int n) { return n > 0 ? (int)((xs(&trng) >> 8) % (uint32_t)n) : 0; }
static inline float noise(void) { return (float)(int32_t)xs(&nrng) * (1.0f / 2147483648.0f); }

#define SN 1024
static float SINT[SN + 1];
static inline __attribute__((always_inline)) float sn(float ph)   /* the sine, from a table: phase in turns */
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
static inline float blep(float t, float dt)       /* a band-limited step's correction (PolyBLEP) */
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
static float spark(void) { return (float)par[P_SPARKLE] * 0.01f; }

/* ---------------------------------------------------------------- keys --- */
#define NKV 8
typedef struct {
    float pc, pt, ic, it;
    float amp, dec, rel, att;
    float idx, idx_dec, idx_floor, tine, tine_dec;
    float gl, gr;
    int32_t gate;                                /* samples until the damper; < 0: held by the player */
    uint8_t note, on, player;
    uint32_t age;
} kv_t;
static kv_t kv[NKV];
static uint32_t kv_clock;
static int keys_t;                               /* this song's: 0 Rhodes, 1 DX */

static void key_on(int note, float vel, int32_t gate, int player)
{
    kv_t *v = 0;
    int i;
    float f, tdec, sp = spark();
    uint32_t oldest = 0;
    while (note < 36)
        note += 12;
    while (note > 96)
        note -= 12;
    for (i = 0; i < NKV; i++)                    /* the same note again: its own voice; else a free one */
        if (kv[i].on && kv[i].note == note && kv[i].player == (uint8_t)player) {
            v = &kv[i];
            break;
        }
    if (!v)
        for (i = 0; i < NKV; i++)
            if (!kv[i].on) {
                v = &kv[i];
                break;
            }
    if (!v)
        for (i = 0; i < NKV; i++) {              /* else the quietest-oldest */
            uint32_t a = kv_clock - kv[i].age + (kv[i].gate == 0 ? 0x10000000u : 0u);
            if (!kv[i].player && a >= oldest) {
                oldest = a;
                v = &kv[i];
            }
        }
    if (!v)
        v = &kv[0];
    f = mtof((float)note);
    v->ic = f / EN_SR;
    if (!v->on)
        v->pc = v->pt = 0.0f;
    vel = fm_clampf(vel, 0.05f, 1.0f);
    tdec = fm_clampf(2.4f * fm_sqrtf(261.6f / f), 0.8f, 4.0f);
    if (keys_t == 0) {                           /* Rhodes: the bark with the velocity, the tine's ping */
        v->idx = (0.10f + 0.30f * vel) * (0.65f + 0.7f * sp);
        v->idx_floor = 0.035f;
        v->idx_dec = tau(0.28f);
        v->tine = 0.06f * vel * (0.5f + sp);
        v->tine_dec = tau(0.035f);
        v->it = fm_minf(f * 7.0f, 7000.0f) / EN_SR;
        v->dec = tau(tdec);
    } else {                                     /* DX: a glassy body, a bright metal tine */
        v->idx = (0.05f + 0.12f * vel) * (0.65f + 0.7f * sp);
        v->idx_floor = 0.02f;
        v->idx_dec = tau(0.5f);
        v->tine = 0.13f * vel * (0.45f + sp);
        v->tine_dec = tau(0.06f);
        v->it = fm_minf(f * 14.0f, 9000.0f) / EN_SR;
        v->dec = tau(tdec * 1.15f);
    }
    v->rel = tau(0.12f);
    v->amp = vel * (0.55f + 0.45f * vel);
    v->att = 0.0f;
    {
        float p = fm_clampf((float)(note - 62) * 0.03f, -0.4f, 0.4f);   /* low left, high right */
        v->gl = 0.75f - 0.9f * p;
        v->gr = 0.75f + 0.9f * p;
    }
    v->gate = gate;
    v->note = (uint8_t)note;
    v->on = 1;
    v->player = (uint8_t)player;
    v->age = kv_clock++;
}
static void key_off_player(int note)
{
    int i;
    for (i = 0; i < NKV; i++)
        if (kv[i].on && kv[i].player && kv[i].note == note && kv[i].gate < 0)
            kv[i].gate = 0;
}

/* -------------------------------------------------------------- guitar --- */
/* four strings, plucked: a delay line a period long, its loop low-passed and tuned with an all-pass */
#define NGV 4
#define KSN 512
static float ks_buf[NGV][KSN] EN_POOL;
static struct {
    int n, w;
    float apc, apx, apy, lp, a, fb, amp, gl, gr;
    int32_t gate;
    uint8_t on;
    float mute_a, mute_fb;
} gv[NGV];

static void gtr_on(int s, int note, float vel, int32_t gate, int muted)
{
    float f, p, dl, d, a, *b;
    int n, i;
    float x = 0.0f, br;
    if (s < 0 || s >= NGV)
        return;
    while (note < 48)
        note += 12;
    while (note > 84)
        note -= 12;
    f = mtof((float)note);
    a = 0.55f + 0.25f * spark();                 /* the loop's brightness */
    dl = (1.0f - a) / a;                         /* the low-pass's delay, near the fundamental */
    p = EN_SR / f;
    n = (int)(p - dl - 0.5f);
    if (n < 8)
        n = 8;
    if (n > KSN - 4)
        n = KSN - 4;
    d = p - dl - (float)n;                       /* the rest: the all-pass's fraction */
    d = fm_clampf(d, 0.2f, 1.8f);
    gv[s].apc = (1.0f - d) / (1.0f + d);
    gv[s].n = n;
    gv[s].a = a;
    gv[s].fb = 0.9975f;
    gv[s].mute_a = 0.25f;
    gv[s].mute_fb = 0.90f;
    b = ks_buf[s];
    br = muted ? 0.25f : 0.35f + 0.4f * vel;     /* the pick: brighter harder */
    for (i = 0; i < KSN; i++) {                  /* the line filled with a filtered burst */
        x += (noise() - x) * br;
        b[i] = i <= n ? x * vel * 1.6f : 0.0f;
    }
    gv[s].w = n + 1;
    gv[s].apx = gv[s].apy = 0.0f;
    gv[s].lp = 0.0f;
    gv[s].amp = 1.0f;
    gv[s].gate = muted ? 0 : gate;
    gv[s].on = 1;
    {
        float pn = (float)(s - 1.5f) * 0.18f;
        gv[s].gl = 0.8f - pn;
        gv[s].gr = 0.8f + pn;
    }
}

/* ---------------------------------------------------------------- bass --- */
enum { BA_FINGER, BA_THUMB, BA_POP, BA_GHOST };
static struct {
    float ph, inc, tinc, amp, peak, sus, idx, idx_floor, idec, dec, lp, lp2, lpa, click;
    int32_t gate;
    uint8_t on, atk;
} bs;
static int bass_t;                               /* this song's: 0 fingered, 1 slapped */
static void bass_on(int note, float vel, int32_t gate, int art)
{
    float f = mtof((float)note), sp = spark();
    if (!bs.on || bs.amp < 0.05f)
        bs.inc = f / EN_SR;
    bs.tinc = f / EN_SR;
    bs.on = 1;
    bs.atk = 1;
    bs.gate = gate;
    switch (art) {
    case BA_THUMB:                               /* the thumb: a bright smack, a quick fall */
        bs.peak = vel;
        bs.idx = 1.0f + 0.4f * vel;
        bs.idx_floor = 0.14f;
        bs.idec = tau(0.05f);
        bs.lpa = onepole(2600.0f + 1800.0f * sp);
        bs.click = 0.25f * vel;
        bs.dec = tau(0.5f);
        break;
    case BA_POP:                                 /* the pop: the octave snapped */
        bs.peak = vel * 0.85f;
        bs.idx = 1.5f + 0.5f * vel;
        bs.idx_floor = 0.2f;
        bs.idec = tau(0.04f);
        bs.lpa = onepole(4200.0f + 2000.0f * sp);
        bs.click = 0.15f * vel;
        bs.dec = tau(0.3f);
        break;
    case BA_GHOST:                               /* a dead note: a thump with no pitch to speak of */
        bs.peak = vel * 0.45f;
        bs.idx = 0.9f;
        bs.idx_floor = 0.3f;
        bs.idec = tau(0.02f);
        bs.lpa = onepole(1200.0f);
        bs.click = 0.1f * vel;
        bs.dec = tau(0.05f);
        break;
    default:                                     /* fingered: round, a little growl on the attack */
        bs.peak = vel;
        bs.idx = 0.30f + 0.25f * vel;
        bs.idx_floor = 0.07f;
        bs.idec = tau(0.12f);
        bs.lpa = onepole(900.0f + 900.0f * sp);
        bs.click = 0.0f;
        bs.dec = tau(0.8f);
        break;
    }
    bs.sus = bs.peak * 0.7f;
}

/* --------------------------------------------------------------- drums --- */
static struct {
    float ph, f, fend, pdec, amp, adec, click;
} kick;
static struct {
    float ph, inc, tamp, tdec, namp, ndec, lo, bd, lp, gamp, glen, glp;
} snare;
static struct {
    float ph, inc, amp, dec, namp, ndec, lo, bd;
} rim;
static struct {
    float amp, dec, h1, h2, lp, lp2;
} hat;
static struct {
    float ph, f, fend, pdec, amp, adec;
} tom;
static struct {
    float amp, dec, h1, h2, h3, h4;
} crash;
static float duck;                               /* the kick's push on the rest (a soft side-chain) */

static void kick_hit(float v)
{
    kick.ph = 0.0f;
    kick.f = 170.0f;
    kick.fend = 50.0f + 4.0f * frand();
    kick.pdec = tau(0.022f);
    kick.amp = v;
    kick.adec = tau(0.16f);
    kick.click = v * 0.35f;
    duck = 1.0f;
    en_kick_glow = 1.0f;
}
static void snare_hit(float v, int gated)
{
    snare.ph = 0.0f;
    snare.inc = (195.0f + 8.0f * frand()) / EN_SR;
    snare.tamp = v * 0.6f;
    snare.tdec = tau(0.045f);
    snare.namp = v;
    snare.ndec = tau(0.07f);
    snare.gamp = gated ? v * 0.38f : 0.0f;       /* the gated room: a flat burst, then cut */
    snare.glen = gated ? EN_SR * 0.16f : 0.0f;
}
static void rim_hit(float v)
{
    rim.ph = 0.0f;
    rim.inc = 460.0f / EN_SR;
    rim.amp = v * 0.7f;
    rim.dec = tau(0.022f);
    rim.namp = v;
    rim.ndec = tau(0.010f);
}
static void hat_hit(float v, int open)
{
    hat.amp = v;
    hat.dec = tau(open ? 0.24f : 0.028f + 0.01f * frand());
}
static void tom_hit(int which, float v)          /* 0 high .. 2 low */
{
    static const float F[3] = {210.0f, 155.0f, 110.0f};
    tom.ph = 0.0f;
    tom.fend = F[which < 0 ? 0 : which > 2 ? 2 : which];
    tom.f = tom.fend * 1.6f;
    tom.pdec = tau(0.04f);
    tom.amp = v;
    tom.adec = tau(0.24f);
}
static void crash_hit(float v)
{
    crash.amp = v;
    crash.dec = tau(1.3f);
}

/* ---------------------------------------------------------------- lead --- */
static struct {
    float ph1, ph2, inc, tinc, amp, peak, lp1, lp2, bright, vph, vib, scoop, att, idx;
    int32_t gate;
    uint8_t on;
} ld;
static int lead_t;                               /* this song's: 0 the synth, 1 the soprano sax */
static void lead_on(int note, float vel, int32_t gate, int legato)
{
    float f = mtof((float)note);
    ld.tinc = f / EN_SR;
    if (!legato || !ld.on || ld.amp < 0.05f) {
        ld.inc = ld.tinc;
        ld.att = 0.0f;
        ld.vib = 0.0f;
        ld.scoop = lead_t ? 0.035f : 0.0f;       /* the sax: each new breath scooped up into the note */
    }
    ld.peak = vel;
    ld.gate = gate;
    ld.bright = 1.0f;
    ld.on = 1;
}

/* --------------------------------------------------------------- brass --- */
#define NBV 4
static struct {
    float ph, inc, amp, peak, idx, ienv, att, scoop;
    int32_t gate;
    uint8_t on;
    float pan;
} bv[NBV];
static int bv_next;
static void brass_on(int note, float vel, int32_t gate, int voice)
{
    int i = voice >= 0 && voice < NBV ? voice : (bv_next++ & (NBV - 1));
    bv[i].inc = mtof((float)note) / EN_SR;
    bv[i].ph = 0.0f;
    bv[i].peak = vel;
    bv[i].amp = 0.0f;
    bv[i].att = 0.0f;
    bv[i].ienv = 0.0f;
    bv[i].scoop = 0.012f;
    bv[i].gate = gate;
    bv[i].on = 1;
    bv[i].pan = ((float)i - 1.5f) * 0.25f;
}

/* ------------------------------------------------------------- strings --- */
#define NSV 4
static struct {
    float p1, p2, inc, tinc, amp;
    uint8_t on;
} sv[NSV];
static float str_level, str_target;              /* the section's pad, swelling in and out */
static float str_lp1l, str_lp2l, str_lp1r, str_lp2r;

/* -------------------------------------------------------------- spaces --- */
/* the plate: four delay lines, a Householder mix, damped; two diffusers before */
#define RV0 1693
#define RV1 2111
#define RV2 2539
#define RV3 2903
#define AP0 331
#define AP1 467
static float rv_buf[RV0 + RV1 + RV2 + RV3 + AP0 + AP1] EN_POOL;
static float *rv_d[4];
static const int RV_N[4] = {RV0, RV1, RV2, RV3};
static int rv_p[4], ap_p[2];
static float *ap_d[2];
static const int AP_N[2] = {AP0, AP1};
static float rv_lp[4];

/* the lead's echo: a dotted eighth */
#define EC_N 32768
static int16_t ec_buf[EC_N] EN_POOL;
static int ec_w;
static float ec_lp, ec_len = 20000.0f;

/* the chorus: one line, two moving taps (left, right) */
#define CH_N 2048
static float ch_buf[CH_N] EN_POOL;
static int ch_w;
static float ch_ph;

/* -------------------------------------------------------- the sequence --- */
#define BLK 256
#define NEV 96
enum { E_KEY, E_BASS, E_KICK, E_SNARE, E_SNAREG, E_RIM, E_HAT, E_OHAT, E_TOM, E_CRASH, E_GTR, E_LEAD, E_BRASS,
       E_STR };
typedef struct {
    int32_t wait;
    uint8_t type, note, used, x;
    float vel;
    int32_t gate;
} ev_t;
static ev_t evq[NEV];

static void sched(int type, int32_t wait, int note, float vel, int32_t gate, int x)
{
    int i;
    for (i = 0; i < NEV; i++)
        if (!evq[i].used) {
            evq[i].used = 1;
            evq[i].type = (uint8_t)type;
            evq[i].wait = wait < 0 ? 0 : wait;
            evq[i].note = (uint8_t)note;
            evq[i].vel = vel;
            evq[i].gate = gate;
            evq[i].x = (uint8_t)x;
            return;
        }
}

/* the song: its plan (sections, a chord each half bar, the pushes, the key), its melody, where it is */
#define MAXBARS 96
#define MAXSEC 12
#define NMEL 400
typedef struct {
    uint8_t type, no, bars, bar0;
} sec_t;
typedef struct {
    int8_t r, q, b;                              /* root and bass: pitch classes; the quality */
} chd_t;
typedef struct {
    uint16_t at;                                 /* sixteenths into the song */
    uint8_t dur, note, vel, leg;
} mnote_t;
static struct {
    sec_t sec[MAXSEC];
    int nsec, bars;
    chd_t ch[MAXBARS * 2];
    uint8_t push[MAXBARS];                       /* the next bar's chord comes an eighth early (step 14) */
    uint8_t keyoff[MAXBARS];                     /* semitones up: the last chorus */
    uint8_t secof[MAXBARS];
    mnote_t mel[NMEL];
    int nmel, mel_i;
    int key, up, t0;                             /* the key; the key change; the melody's tonic (MIDI) */
    int verse, pre, chorus, bridge, groove, cut, arp, comp, riff, solo;
    int tempo_off;
    float swing;
    int bar, step;
    float step_left;
    int kvo[4], gvo[4], svo[4];                  /* the voicings now: keys, guitar, strings */
    int cur;                                     /* the chord sounding (its slot), -1 none */
    int bass_last;
} sg;

static float step_len = 6125.0f;                 /* samples a sixteenth */
static uint32_t trk_samples;
static int gap_left;                             /* between songs: samples to the next */
static float fade = 0.0f, fade_t = 1.0f, fade_rate = 0.0f;
static int skipping;

static int cur_bpm(void)
{
    int b = par[P_TEMPO] + sg.tempo_off, lo = par[P_TEMPO] < 95 ? par[P_TEMPO] : 95,
        hi = par[P_TEMPO] > 120 ? par[P_TEMPO] : 120;      /* the genre's 95..120, unless the knob is outside */
    return b < lo ? lo : b > hi ? hi : b;
}
static void tempo_set(void)
{
    int b = cur_bpm();
    en_bpm = (uint8_t)b;
    step_len = EN_SR * 15.0f / (float)b;
    en_track_len_ms = (uint32_t)((float)(sg.bars * 16) * step_len * (1000.0f / EN_SR));
    ec_len = fm_minf(step_len * 3.0f, (float)(EC_N - 4));
}

/* ---- the plan */
static uint16_t rot12(uint16_t m, int k)
{
    k = (k % 12 + 12) % 12;
    return (uint16_t)(((m << k) | (m >> (12 - k))) & 0xFFFu);
}
static const uint16_t MAJOR = B_(0) | B_(2) | B_(4) | B_(5) | B_(7) | B_(9) | B_(11);

static int chd_eq(const chd_t *a, const chd_t *b) { return a->r == b->r && a->q == b->q && a->b == b->b; }

/* the chords of a four-bar progression into bars b0.. (n bars of it, from its bar from), in key k */
static void put_prog(const prog_t *p, int b0, int n, int from, int k)
{
    int slot = 0, i, j, s0 = from * 2, s1 = (from + n) * 2;
    for (i = 0; i < p->n; i++)
        for (j = 0; j < p->c[i].beats / 2; j++, slot++)
            if (slot >= s0 && slot < s1) {
                chd_t *c = &sg.ch[b0 * 2 + slot - s0];
                c->r = (int8_t)((k + p->c[i].r) % 12);
                c->q = p->c[i].q;
                c->b = (int8_t)(p->c[i].b < 0 ? c->r : (k + p->c[i].b) % 12);
            }
}
/* the lift: a bar of the dominant (sus4, then the 7th) of the chord that comes next, at pitch class t */
static void put_lift(int bar, int t, int minor_target)
{
    int d = (t + 7) % 12;
    sg.ch[bar * 2].r = sg.ch[bar * 2].b = (int8_t)d;
    sg.ch[bar * 2].q = Q_7SUS;
    sg.ch[bar * 2 + 1].r = sg.ch[bar * 2 + 1].b = (int8_t)d;
    sg.ch[bar * 2 + 1].q = (int8_t)(minor_target ? Q_7B9 : Q_7);
}
static int add_sec(int type, int no, int bars)
{
    sec_t *s = &sg.sec[sg.nsec];
    if (sg.nsec >= MAXSEC || sg.bars + bars > MAXBARS)
        return -1;
    s->type = (uint8_t)type;
    s->no = (uint8_t)no;
    s->bars = (uint8_t)bars;
    s->bar0 = (uint8_t)sg.bars;
    sg.bars += bars;
    return sg.nsec++;
}
static int pick_mask(uint8_t mask, int n, int avoid)
{
    int i, c = 0, k;
    for (i = 0; i < n; i++)
        if ((mask >> i & 1u) && i != avoid)
            c++;
    if (!c)
        return avoid >= 0 ? avoid : 0;
    k = tirand(c);
    for (i = 0; i < n; i++)
        if ((mask >> i & 1u) && i != avoid && k-- == 0)
            return i;
    return 0;
}
static int is_minor_q(int q) { return q == Q_M7 || q == Q_M9 || q == Q_M11 || q == Q_M7B5; }

static const chd_t *chord_at(int abs)            /* the chord at a sixteenth of the song (the pushes too) */
{
    int bar = abs >> 4, st = abs & 15;
    if (bar >= sg.bars)
        bar = sg.bars - 1;
    if (bar < 0)
        bar = 0;
    if (st >= 14 && sg.push[bar] && bar + 1 < sg.bars)
        return &sg.ch[(bar + 1) * 2];
    return &sg.ch[bar * 2 + (st >> 3)];
}

/* ---- the melody: written for each song, phrase by phrase, before it plays */
static int cell_parse(const char *c, int8_t *on, int8_t *dur, uint8_t *strong)
{
    int i, n = 0;
    for (i = 0; i < 32 && c[i]; i++)
        if (c[i] == 'x' || c[i] == 'X') {
            int d = 1;
            while (i + d < 32 && c[i + d] == '-')
                d++;
            on[n] = (int8_t)i;
            dur[n] = (int8_t)d;
            strong[n] = (uint8_t)(c[i] == 'X' || (i & 7) == 0 || d >= 4 || ((i & 3) == 0 && d >= 2));
            n++;
        }
    return n;
}
/* the notes a melody may use against a chord: its tones, its tensions, and the key's scale (bent to the chord's
 * own notes where they leave the key: E7 in C takes G#, and the G next to it goes) */
static void chord_sets(const chd_t *c, int key, uint16_t *core, uint16_t *tens, uint16_t *scale)
{
    uint16_t co = rot12(Q_CORE[c->q], c->r), te = rot12(Q_TENS[c->q], c->r), s = rot12(MAJOR, key), ch;
    int pc;
    te |= (uint16_t)B_(c->b);
    ch = co & (uint16_t)~s;
    for (pc = 0; pc < 12; pc++)
        if (ch >> pc & 1u)
            s &= (uint16_t)~(B_((pc + 1) % 12) | B_((pc + 11) % 12));
    s |= co;
    te &= (uint16_t)~co;
    te &= s;                                     /* tensions only where the key has them */
    *core = co;
    *tens = te;
    *scale = s;
}
static int contour(int kind, int u16)             /* -16..16 along the phrase (u16: 0..32) */
{
    int u = u16;
    switch (kind) {
    case 0: return (u * (32 - u)) / 8 - 16;       /* an arch */
    case 1: return u - 16;                        /* rising */
    case 2: return 16 - u;                        /* falling */
    default: return (u < 16 ? 16 - 2 * u : 2 * u - 48) / 2;   /* down and up */
    }
}
enum { END_NONE, END_STABLE, END_HOME, END_OPEN };
/* the allowed pitch nearest a target (ties: up when up, else down) */
static int snap(int tg, uint16_t set, int lo, int hi, int up)
{
    int d;
    for (d = 0; d <= 12; d++) {
        int a = tg + (up ? d : -d), b = tg - (up ? d : -d);
        if (a >= lo && a <= hi && (set >> (a % 12) & 1u))
            return a;
        if (b >= lo && b <= hi && (set >> (b % 12) & 1u))
            return b;
    }
    return tg < lo ? lo : tg > hi ? hi : tg;
}
static int is_home(int pc, int key) { return pc == key % 12 || pc == (key + 4) % 12 || pc == (key + 7) % 12; }
/* one phrase (two bars, a cell) at song bar b0, in [lo, hi] around center. src: an earlier phrase's notes to
 * follow (a repeat, a sequence) shifted by shift; else written fresh: first its skeleton (the strong notes, chord
 * tones along the contour, moving by steps and small leaps), then the notes between (passing and neighbour notes
 * from the scale, pickups leaning into the next strong note). Notes may not cross the section's end (sec_end, a
 * sixteenth). Returns the notes written; pitches into outp */
static int phrase(const char *cell, int b0, const uint8_t *src, int nsrc, int shift, int lo, int hi, int center,
                  int amp, int kind, int endk, int sec_end, int key, uint8_t *outp, int last_phrase)
{
    int8_t on[32], dur[32];
    uint8_t strong[32], st[32];
    uint16_t sset[32], wset[32], coreset[32];
    int at_[32], d_[32], p[32];
    int n = cell_parse(cell, on, dur, strong), i, m = 0, endi;
    for (i = 0; i < n; i++) {                    /* the notes: where, how long, what the chords allow */
        int at = b0 * 16 + on[i], d = dur[i], x;
        const chd_t *c, *c2 = 0;
        uint16_t core, tens, scale;
        if (last_phrase && on[i] >= 26)          /* the section's last phrase: no pickups into the next */
            break;
        if (last_phrase && on[i] + d > 28)
            d = 28 - on[i];
        if (at + d > sec_end)
            d = sec_end - at;
        if (d < 1)
            break;
        c = chord_at(at);
        for (x = at + 1; x < at + d && x < sec_end; x++) {   /* a change just after it starts: anticipated */
            const chd_t *cx = chord_at(x);
            if (!chd_eq(cx, c)) {
                if (x - at <= 2)
                    c = cx;
                else
                    c2 = cx;                     /* held through a change: it must suit both */
                break;
            }
        }
        chord_sets(c, key, &core, &tens, &scale);
        sset[m] = core | tens;
        wset[m] = scale;
        coreset[m] = core;
        if (c2) {
            uint16_t c2c, c2t, c2s;
            chord_sets(c2, key, &c2c, &c2t, &c2s);
            if (sset[m] & (c2c | c2t))
                sset[m] &= c2c | c2t;
            if (wset[m] & (c2c | c2t))
                wset[m] &= c2c | c2t;
        }
        at_[m] = at;
        d_[m] = d;
        st[m] = strong[i];
        m++;
    }
    n = m;
    if (!n)
        return 0;
    for (endi = n - 1; endi > 0 && on[endi] >= 26 && !last_phrase; endi--)
        ;                                        /* the phrase's last word (after it: pickups) */
    st[endi] = 1;
    if (endk != END_NONE) {                      /* it ends on the chord's own, the key's notes */
        uint16_t e = coreset[endi] & rot12(MAJOR, key);
        sset[endi] = e ? e : coreset[endi];
    }
    for (i = endi + 1; i < n; i++)
        st[i] = 0;
    if (src) {                                   /* follow the earlier phrase: as near as the chords allow */
        int tg = center;
        for (i = 0; i < n; i++) {
            tg = i < nsrc ? src[i] + shift : tg;
            p[i] = snap(tg, st[i] ? sset[i] : wset[i], lo, hi, shift > 0);
            if (i == endi && endk == END_HOME && !is_home(p[i] % 12, key)) {
                int k;
                for (k = 1; k <= 4; k++) {       /* home within a third, if the chord has it */
                    if (p[i] - k >= lo && (sset[i] >> ((p[i] - k) % 12) & 1u) && is_home((p[i] - k) % 12, key)) {
                        p[i] -= k;
                        break;
                    }
                    if (p[i] + k <= hi && (sset[i] >> ((p[i] + k) % 12) & 1u) && is_home((p[i] + k) % 12, key)) {
                        p[i] += k;
                        break;
                    }
                }
            }
        }
    } else {
        int ps = -1, a, b;
        for (i = 0; i < n; i++) {                /* the skeleton */
            int cand, best = center, tg;
            float bs = 1e9f;
            if (!st[i])
                continue;
            tg = center + (contour(kind, on[i] + d_[i] / 2) * amp) / 16;
            for (cand = lo; cand <= hi; cand++) {
                float sc;
                int pc = cand % 12, e = cand - tg, dd;
                if (!(sset[i] >> pc & 1u))
                    continue;
                sc = (float)(e < 0 ? -e : e) * 0.8f;
                if (ps >= 0) {
                    dd = cand - ps;
                    dd = dd < 0 ? -dd : dd;
                    sc += dd == 0 ? 2.4f : dd <= 4 ? 0.0f : dd <= 7 ? (float)(dd - 4) * 0.7f : 2.0f + (float)(dd - 7) * 2.0f;
                }
                if (!(coreset[i] >> pc & 1u))
                    sc += 0.5f;
                if (i == endi && endk == END_HOME && is_home(pc, key))
                    sc -= 2.5f;
                if (i == endi && endk == END_OPEN && pc == (key + 2) % 12)
                    sc -= 1.0f;
                sc += trand() * 1.3f;
                if (sc < bs) {
                    bs = sc;
                    best = cand;
                }
            }
            p[i] = best;
            ps = best;
        }
        for (a = 0; a < n; a = b + 1) {          /* the notes between */
            int L, R, k, j, nb = trand() < 0.6f ? 2 : -2, how = tirand(10);
            if (st[a]) {
                b = a;
                continue;
            }
            for (b = a; b + 1 < n && !st[b + 1]; b++)
                ;
            L = a > 0 ? p[a - 1] : -1;
            R = b + 1 < n ? p[b + 1] : -1;
            k = b - a + 1;
            for (j = 0; j < k; j++) {
                int ideal, up;
                if (L >= 0 && R >= 0) {
                    int span = R - L;
                    if (span >= 2 || span <= -2) {
                        ideal = L + span * (j + 1) / (k + 1);   /* passing */
                        up = span > 0;
                    } else {
                        ideal = L + ((j & 1) == 0 ? nb : 0);    /* a neighbour, and back */
                        up = nb > 0;
                    }
                } else if (R >= 0) {             /* pickups: repeated, from below, from above */
                    ideal = how < 3 && k <= 2 ? R : how < 8 ? R - 2 * (k - j) : R + 2 * (k - j);
                    up = how < 8;
                } else {                         /* after the last word: back towards the middle */
                    ideal = L + (center > L ? 2 : -2) * (j + 1);
                    up = center > L;
                }
                p[a + j] = snap(ideal, wset[a + j], lo, hi, up);
            }
        }
    }
    for (i = 0; i < n; i++) {
        outp[i] = (uint8_t)p[i];
        if (sg.nmel < NMEL) {
            mnote_t *mn = &sg.mel[sg.nmel++];
            int v = st[i] ? 92 : 76;
            mn->at = (uint16_t)at_[i];
            mn->dur = (uint8_t)d_[i];
            mn->note = (uint8_t)p[i];
            mn->vel = (uint8_t)(i == 0 ? v - 4 : v);
            mn->leg = (uint8_t)(i + 1 < n && at_[i + 1] == at_[i] + d_[i]);
        }
    }
    return n;
}

/* the melody of one section instance (fresh), its phrases following the section's plan */
static void write_section(int si, uint8_t *mem /* 4 x 32 */)
{
    const sec_t *s = &sg.sec[si];
    int b0 = s->bar0, end = (s->bar0 + s->bars) * 16, t0 = sg.t0, k = sg.key, n0;
    uint8_t *A = mem, *B = mem + 32, *Cc = mem + 64;
    int na, kind;
    const char *ca, *cb;
    switch (s->type) {
    case SEC_VERSE:
        ca = CELL_VERSE[tirand(NA(CELL_VERSE))];
        do
            cb = CELL_VERSE[tirand(NA(CELL_VERSE))];
        while (cb == ca);
        kind = (int)(trand() < 0.5f ? 0 : trand() < 0.5f ? 2 : 3);
        na = phrase(ca, b0, 0, 0, 0, t0 + 2, t0 + 16, t0 + 9, 8, kind, END_NONE, end, k, A, 0);
        phrase(ca, b0 + 2, A, na, 0, t0 + 2, t0 + 16, t0 + 9, 8, kind, END_NONE, end, k, B, 0);
        phrase(ca, b0 + 4, A, na, 0, t0 + 2, t0 + 16, t0 + 9, 8, kind, END_NONE, end, k, Cc, 0);
        phrase(cb, b0 + 6, 0, 0, 0, t0 + 2, t0 + 16, t0 + 8, 4, 2, END_STABLE, end, k, Cc, 1);
        break;
    case SEC_PRE:
        ca = CELL_PRE[tirand(NA(CELL_PRE))];
        na = phrase(ca, b0, 0, 0, 0, t0 + 4, t0 + 17, t0 + 10, 5, 1, END_NONE, end, k, A, 0);
        n0 = sg.nmel;
        phrase(ca, b0 + 2, A, na, 3, t0 + 4, t0 + 18, t0 + 13, 5, 1, END_OPEN, end, k, B, 0);
        (void)n0;
        break;
    case SEC_CHORUS:
        ca = CELL_HOOK[tirand(NA(CELL_HOOK))];
        kind = trand() < 0.6f ? 0 : 2;
        na = phrase(ca, b0, 0, 0, 0, t0 + 5, t0 + 18, t0 + 12, 7, kind, END_NONE, end, k, A, 0);
        phrase(ca, b0 + 2, A, na, 0, t0 + 5, t0 + 18, t0 + 12, 7, kind, END_NONE, end, k, B, 0);
        phrase(ca, b0 + 4, A, na, 0, t0 + 5, t0 + 18, t0 + 12, 7, kind, END_NONE, end, k, Cc, 0);
        phrase(ca, b0 + 6, B, na, 0, t0 + 5, t0 + 18, t0 + 12, 7, kind, END_HOME, end, k, Cc, 1);
        break;
    case SEC_BRIDGE:
    case SEC_SOLO: {
        int solo = s->type == SEC_SOLO, lo = solo ? t0 + 7 : t0 + 4, hi = solo ? t0 + 21 : t0 + 17;
        int c0 = solo ? t0 + 14 : t0 + 11;
        if (solo) {
            ca = CELL_SOLO[tirand(NA(CELL_SOLO))];
            do
                cb = CELL_SOLO[tirand(NA(CELL_SOLO))];
            while (cb == ca);
        } else {
            ca = CELL_BRIDGE[tirand(NA(CELL_BRIDGE))];
            do
                cb = CELL_BRIDGE[tirand(NA(CELL_BRIDGE))];
            while (cb == ca);
        }
        kind = trand() < 0.5f ? 0 : 1;
        na = phrase(ca, b0, 0, 0, 0, lo, hi, c0, 5, kind, END_NONE, end, k, A, 0);
        phrase(ca, b0 + 2, A, na, 0, lo, hi, c0, 5, kind, END_NONE, end, k, B, 0);
        phrase(ca, b0 + 4, A, na, 2, lo, hi, c0 + 2, 5, kind, END_NONE, end, k, Cc, 0);
        phrase(cb, b0 + 6, 0, 0, 0, lo, hi, c0 + 1, 4, 1, END_OPEN, end, k, Cc, 1);
        break;
    }
    default: break;
    }
}
/* an earlier section's melody again (verse two, chorus two, the last chorus up a key) */
static void copy_section(int from, int to, int up)
{
    int i, n = sg.nmel, d = (sg.sec[to].bar0 - sg.sec[from].bar0) * 16;
    int a0 = sg.sec[from].bar0 * 16, a1 = (sg.sec[from].bar0 + sg.sec[from].bars) * 16;
    for (i = 0; i < n && sg.nmel < NMEL; i++)
        if (sg.mel[i].at >= a0 && sg.mel[i].at < a1) {
            mnote_t m = sg.mel[i];
            m.at = (uint16_t)(m.at + d);
            m.note = (uint8_t)(m.note + up);
            sg.mel[sg.nmel++] = m;
        }
}
static void mel_sort(void)                       /* by time (the copies went on the end) */
{
    int i, j;
    for (i = 1; i < sg.nmel; i++) {
        mnote_t m = sg.mel[i];
        for (j = i - 1; j >= 0 && sg.mel[j].at > m.at; j--)
            sg.mel[j + 1] = sg.mel[j];
        sg.mel[j + 1] = m;
    }
}

static uint32_t push_hash(int type, int bin)
{
    int t = type == SEC_LAST || type == SEC_FADE ? SEC_CHORUS : type == SEC_SOLO ? SEC_BRIDGE : type;
    return en_track_rand(en_track_seed, 1000u + (uint32_t)t * 64u + (uint32_t)bin);
}

static void plan(void)
{
    const style_t *st = &STYLES[style_i];
    int i, b, si_v1 = -1, si_p1 = -1, si_c1 = -1, si_b = -1, inter, solo;
    uint8_t mem[128];
    float push_p = st->feel == F_FUNK ? 0.45f : st->feel == F_MID ? 0.35f : st->feel == F_DANCE ? 0.22f : 0.12f;
    sg.nsec = sg.bars = 0;
    sg.nmel = sg.mel_i = 0;
    /* the progressions: from the style's own; the verse not the chorus's twin */
    sg.verse = pick_mask(st->verses, NVERSES, -1);
    sg.chorus = pick_mask(st->choruses, NCHORUSES, sg.verse == 2 ? 2 : -1);
    sg.pre = tirand(NPRES);
    sg.bridge = tirand(NBRIDGES);
    sg.up = trand() < 0.5f ? 1 : 2;
    inter = trand() < 0.5f;
    solo = trand() < (st->lead == 1 ? 0.5f : 0.3f);
    sg.solo = solo;
    /* the form */
    add_sec(SEC_INTRO, 1, 4);
    si_v1 = add_sec(SEC_VERSE, 1, 8);
    si_p1 = add_sec(SEC_PRE, 1, 4);
    si_c1 = add_sec(SEC_CHORUS, 1, 8);
    if (inter)
        add_sec(SEC_INTER, 1, 4);
    add_sec(SEC_VERSE, 2, 8);
    add_sec(SEC_PRE, 2, 4);
    add_sec(SEC_CHORUS, 2, 8);
    si_b = add_sec(solo ? SEC_SOLO : SEC_BRIDGE, 1, 8);
    add_sec(SEC_LAST, 1, 8);
    add_sec(SEC_FADE, 1, 8);
    /* the chords */
    for (i = 0; i < sg.nsec; i++) {
        sec_t *s = &sg.sec[i];
        int up = s->type == SEC_LAST || s->type == SEC_FADE ? sg.up : 0, k = (sg.key + up) % 12;
        for (b = 0; b < s->bars; b++) {
            sg.secof[s->bar0 + b] = (uint8_t)i;
            sg.keyoff[s->bar0 + b] = (uint8_t)up;
        }
        switch (s->type) {
        case SEC_INTRO: case SEC_VERSE: case SEC_INTER:
            for (b = 0; b < s->bars; b += 4)
                put_prog(&VERSES[sg.verse], s->bar0 + b, 4, 0, k);
            break;
        case SEC_PRE: {
            const pch_t *t = &CHORUSES[sg.chorus].c[0];
            put_prog(&PRES[sg.pre], s->bar0, 3, 0, k);
            if (t->r == 0 && trand() < 0.5f)     /* into a chorus on I: the IV/V, or the V7 */
                put_prog(&VERSES[0], s->bar0 + 3, 1, 3, k);
            else
                put_lift(s->bar0 + 3, (k + t->r) % 12, is_minor_q(t->q));
            break;
        }
        case SEC_CHORUS: case SEC_LAST: case SEC_FADE:
            for (b = 0; b < s->bars; b += 4)
                put_prog(&CHORUSES[sg.chorus], s->bar0 + b, 4, 0, k);
            break;
        case SEC_BRIDGE: case SEC_SOLO: {
            const pch_t *t = &CHORUSES[sg.chorus].c[0];
            put_prog(&BRIDGES[sg.bridge], s->bar0, 4, 0, k);
            put_prog(&BRIDGES[sg.bridge], s->bar0 + 4, 3, 0, k);
            put_lift(s->bar0 + 7, (sg.key + sg.up + t->r) % 12, is_minor_q(t->q));   /* the new key's way in */
            break;
        }
        default: break;
        }
    }
    /* the pushes: where the chord changes on the bar line, now and then the band anticipates it together;
     * the same in each instance of a section */
    for (b = 0; b + 1 < sg.bars; b++) {
        const sec_t *s = &sg.sec[sg.secof[b]];
        sg.push[b] = 0;
        if (!chd_eq(&sg.ch[b * 2 + 1], &sg.ch[(b + 1) * 2]) && b + 2 < sg.bars &&
            (float)(push_hash(s->type, b - s->bar0) & 0xFFFFu) < push_p * 65536.0f)
            sg.push[b] = 1;
    }
    sg.push[sg.bars - 1] = 0;
    /* the melody: each kind of section written once, then repeated */
    for (i = 0; i < sg.nsec; i++) {
        sec_t *s = &sg.sec[i];
        switch (s->type) {
        case SEC_VERSE:
            if (i == si_v1)
                write_section(i, mem);
            else
                copy_section(si_v1, i, 0);
            break;
        case SEC_PRE:
            if (i == si_p1)
                write_section(i, mem);
            else
                copy_section(si_p1, i, 0);
            break;
        case SEC_CHORUS:
            if (i == si_c1)
                write_section(i, mem);
            else
                copy_section(si_c1, i, 0);
            break;
        case SEC_LAST: case SEC_FADE: copy_section(si_c1, i, sg.up); break;
        case SEC_BRIDGE: case SEC_SOLO:
            if (i == si_b)
                write_section(i, mem);
            break;
        default: break;
        }
    }
    mel_sort();
}

/* the voicings: four notes, voice-led from the last (each moving as little as it can), around center */
static void voice4(const chd_t *c, int *cur, int center, int lo, int hi)
{
    int pcs[4], best[4], i, inv, bestd = 1 << 30, prev = 0;
    for (i = 0; i < 4; i++) {
        pcs[i] = (c->r + Q_VOICE[(int)c->q][i]) % 12;
        prev += cur[i];
        best[i] = center - 6 + 4 * i;
    }
    prev /= 4;
    if (prev < center - 7 || prev > center + 7)
        prev = center;
    for (inv = 0; inv < 4; inv++) {              /* each tone at the bottom in turn, the rest stacked closely */
        int n[4], j, base = pcs[inv], d = 0, mean = 0;
        n[0] = lo + ((base - lo) % 12 + 12) % 12;
        for (j = 1; j < 4; j++) {
            int pc = pcs[(inv + j) % 4];
            int x = n[j - 1] + ((pc - n[j - 1]) % 12 + 12) % 12;
            if (x == n[j - 1])
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
                int e = n[j] - cur[k];
                e = e < 0 ? -e : e;
                m = e < m ? e : m;
            }
            d += m;
        }
        d += (mean > center ? mean - center : center - mean);
        for (j = 0; j < 3; j++)                  /* no semitone rubs inside the voicing */
            if (n[j + 1] - n[j] == 1)
                d += j == 2 ? 26 : 20;
        if (n[0] < lo || n[3] > hi)
            d += 40;
        if (d < bestd) {
            bestd = d;
            for (j = 0; j < 4; j++)
                best[j] = n[j];
        }
    }
    for (i = 0; i < 4; i++)
        cur[i] = best[i];
}

static void new_track(void)
{
    uint32_t seed;
    const style_t *st = &STYLES[style_i];
    int tries = 0;
    en_track_no++;
    seed = en_track_rand(en_seed_base, en_track_no);
    en_track_seed = seed;
    trng = seed;
    sg.key = tirand(12);
    sg.t0 = 52 + ((sg.key - 4) % 12 + 12) % 12;  /* the melody's tonic: E3 .. Eb4 */
    keys_t = st->keys == 2 ? tirand(2) : st->keys;
    lead_t = st->lead == 2 ? (trand() < 0.25f) : st->lead;
    bass_t = st->bass == 2 ? (trand() < 0.34f) : st->bass;
    en_lead_sax = (uint8_t)lead_t;
    do
        sg.groove = tirand(NGROOVES);
    while (GROOVES[sg.groove].feel != st->feel && ++tries < 64);
    sg.cut = GROOVES[sg.groove].cut;
    sg.comp = GROOVES[sg.groove].comp;
    sg.arp = st->feel == F_BALLAD ? (trand() < 0.5f ? 3 : 0) : tirand(3);
    sg.riff = tirand(NRIFFS);
    sg.swing = st->feel == F_FUNK ? 0.10f : st->feel == F_MID ? 0.06f : st->feel == F_BALLAD ? 0.05f : 0.0f;
    sg.tempo_off = tirand(7) - 3;
    plan();
    tempo_set();
    sg.bar = 0;
    sg.step = 0;
    sg.step_left = 0.0f;
    sg.cur = -1;
    sg.bass_last = 40;
    trk_samples = 0;
    en_track_ms = 0;
    en_phase = PH_PLAY;
    en_section = SEC_INTRO;
    en_section_no = 1;
    en_section_bar = 0;
    en_section_bars = (uint8_t)sg.sec[0].bars;
    en_key = (uint8_t)sg.key;
    en_key_up = 0;
    fade = 0.0f;
    fade_t = 1.0f;
    fade_rate = 1.0f / (0.05f * EN_SR);
    str_target = 0.0f;
    skipping = 0;
    en_tracks_played++;
}

/* ---- the arrangement: what plays in each section (levels 0..1) */
typedef struct {
    float keys, bass, drums, brass, strings;
    uint8_t gtr;                                 /* 0 out, 1 arpeggios, 2 cutting, 3 the style's way */
    int8_t groove;                               /* added to Groove */
} arr_t;
static const arr_t ARR[SEC_N] = {
    {1.0f, 1.0f, 0.95f, 1.0f, 0.0f, 3, 0},       /* intro */
    {0.9f, 0.95f, 0.85f, 0.0f, 0.0f, 1, -15},    /* verse */
    {1.0f, 1.0f, 0.9f, 0.0f, 0.6f, 1, -5},       /* pre-chorus */
    {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2, 10},       /* chorus */
    {1.0f, 1.0f, 0.95f, 1.0f, 0.0f, 3, 5},       /* interlude */
    {0.95f, 0.95f, 0.85f, 0.4f, 0.8f, 1, -10},   /* bridge */
    {1.0f, 1.0f, 0.95f, 0.6f, 0.7f, 2, 5},       /* solo */
    {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2, 15},       /* the last chorus */
    {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2, 15},       /* the fade */
    {0, 0, 0, 0, 0, 0, 0},
};

static int32_t hum(float ms) { return (int32_t)((frand() * 2.0f - 1.0f) * ms * 44.1f); }
static int32_t swing_off(int step) { return (step & 1) ? (int32_t)(sg.swing * step_len) : 0; }

static void play_keys(const chd_t *c, int32_t at, int len, float vel)
{
    int i;
    float roll = (3.0f + 7.0f * frand()) * 44.1f;
    for (i = 0; i < 4; i++) {
        float v = vel * (0.86f + 0.18f * frand()) * (i == 3 ? 1.08f : 1.0f);
        int32_t g = (int32_t)((float)len * step_len) - (int32_t)(roll * (float)i) - 300;
        sched(E_KEY, at + (int32_t)(roll * (float)i), sg.kvo[i], v, g < 800 ? 800 : g, 0);
        LOG('K', sg.kvo[i], v, len);
    }
    (void)c;
}
static void chop(int32_t at, int down, float vel, int strings, int ghost)
{
    int i;
    float gap = (down ? 5.0f : 3.5f) * 44.1f;
    for (i = 0; i < strings; i++) {
        int s = down ? 4 - strings + i : 3 - i;  /* down: low to high; up: high to low */
        sched(E_GTR, at + (int32_t)(gap * (float)i), sg.gvo[s], vel * (0.85f + 0.2f * frand()),
              ghost ? 0 : (int32_t)(step_len * 0.55f), s | (ghost ? 0x80 : 0));
        LOG('G', sg.gvo[s], vel, ghost ? 0 : 1);
    }
}

/* one sixteenth of the song */
static void seq_step(void)
{
    int bar = sg.bar, s = sg.step, abs = bar * 16 + s, si = sg.secof[bar], st, bin, i;
    const sec_t *sec = &sg.sec[si];
    const style_t *sty = &STYLES[style_i];
    const groove_t *g = &GROOVES[sg.groove];
    const arr_t *a;
    const chd_t *c = chord_at(abs);
    int pushed_in = s == 0 && bar > 0 && sg.push[bar - 1];
    int push_now = s == 14 && sg.push[bar];
    int last_bar, next_type = -1, grv, dens, change = 0;
    int32_t sw = swing_off(s);
    float dv = 0.55f + 0.45f * (float)par[P_DRUMS] * 0.01f;
    st = sec->type;
    a = &ARR[st];
    bin = bar - sec->bar0;
    last_bar = bin == sec->bars - 1;
    if (last_bar && si + 1 < sg.nsec)
        next_type = sg.sec[si + 1].type;
    grv = par[P_GROOVE] + a->groove;
    dens = grv < 25 ? 0 : grv < 45 ? 1 : grv < 65 ? 2 : 3;
    /* the screen's view of it */
    if (s == 0) {
        en_section = (uint8_t)st;
        en_section_no = sec->no;
        en_section_bar = (uint8_t)bin;
        en_section_bars = sec->bars;
        en_key = (uint8_t)((sg.key + sg.keyoff[bar]) % 12);
        en_key_up = sg.keyoff[bar];
        if (st == SEC_FADE && bin == 0 && !en_repeat_on) {   /* the fade: eight bars down to nothing */
            fade_t = 0.0f;
            fade_rate = 1.0f / (step_len * 16.0f * 8.0f);
        }
        if (bin == 0) {                          /* the strings: in, out */
            float sl = a->strings;
            if ((st == SEC_BRIDGE || (st == SEC_VERSE && sec->no == 2)) && sty->lush)
                sl = st == SEC_VERSE ? 0.45f : 0.9f;
            if (st == SEC_PRE && par[P_STRINGS] < 25)
                sl = 0.0f;
            str_target = sl;
        }
    }
    {                                            /* a new chord: its voicings */
        int slot = (int)(c - sg.ch);
        if (slot != sg.cur && (s == 0 || s == 8 || s == 14)) {
            if (sg.cur < 0 || !chd_eq(c, &sg.ch[sg.cur])) {
                change = 1;
                voice4(c, sg.kvo, 62, 50, 76);
                voice4(c, sg.gvo, 67, 55, 79);
                voice4(c, sg.svo, 69, 57, 81);
                en_chord_root = (uint8_t)c->r;
                en_chord_q = (uint8_t)c->q;
                en_chord_bass = (uint8_t)c->b;
                {
                    uint16_t m = 0;
                    for (i = 0; i < 4; i++)
                        m |= (uint16_t)(1u << (sg.kvo[i] % 12));
                    en_chord_pcs = (uint16_t)(m | 1u << c->b);
                }
                for (i = 0; i < NSV; i++)        /* the strings glide to the new chord */
                    sched(E_STR, sw, sg.svo[i], 1.0f, 1, i);
            }
            sg.cur = slot;
        }
    }
    /* keys */
    if (par[P_KEYS] > 0 && a->keys > 0.0f) {
        const comp_t *cp = &COMPS[sty->feel == F_BALLAD && st == SEC_VERSE ? 0 : sg.comp];
        float kv0 = (0.45f + 0.35f * (float)par[P_KEYS] * 0.01f) * a->keys;
        int hit = -1;
        for (i = 0; i < cp->n; i++)
            if (cp->st[i] == s)
                hit = i;
        if (pushed_in && s == 0)
            hit = -1;
        if (sg.push[bar] && s >= 13 && !push_now)
            hit = -1;
        if (push_now)
            play_keys(c, sw + hum(3.0f), 6, kv0 * 0.85f);
        else if (hit >= 0)
            play_keys(c, sw + hum(3.0f), cp->len[hit], kv0 * (float)cp->vel[hit] * 0.01f);
        else if (change && s == 8)
            play_keys(c, sw + hum(3.0f), 6, kv0 * 0.7f);
    }
    /* guitar */
    if (par[P_GUITAR] > 0 && a->gtr) {
        int mode = a->gtr == 3 ? (sty->gtr == 0 ? 1 : 2) : a->gtr;
        float gl = 0.5f + 0.5f * (float)par[P_GUITAR] * 0.01f;
        if (sty->gtr == 1 && mode == 1)
            mode = st == SEC_VERSE && sec->no == 1 ? 1 : 2;   /* a cutting style cuts from verse two */
        if (sty->gtr == 0 && mode == 2 && st != SEC_LAST && st != SEC_FADE)
            mode = 1;                            /* an arpeggio style: arpeggios until the last choruses */
        if (mode == 2) {
            char ch = CUTS[sg.cut][s];
            if (sg.push[bar] && s >= 13 && !push_now)
                ch = '.';
            if (pushed_in)
                ch = '.';
            if (push_now)
                ch = 'X';
            if (ch == 'X')
                chop(sw + hum(2.0f), !(s & 1), gl * 0.9f, 4, 0);
            else if (ch == 'x' || (ch == 'c' && dens >= 1))
                chop(sw + hum(2.0f), !(s & 1), gl * 0.7f, 3, 0);
            else if (ch == 'g' && dens >= 2)
                chop(sw + hum(2.0f), !(s & 1), gl * 0.35f, 3, 1);
        } else {
            const char *ap = ARPS[dens >= 3 && sty->feel != F_BALLAD ? 2 : sg.arp];
            char ch = ap[s];
            if (ch >= '0' && ch <= '3') {
                int k = ch - '0';
                int ring = sg.arp == 3 ? 12 : 6;
                sched(E_GTR, sw + hum(3.0f), sg.gvo[k], gl * (0.55f + 0.1f * frand()), (int32_t)(step_len * (float)ring),
                      k);
                LOG('G', sg.gvo[k], gl * 0.6f, ring);
            }
        }
    }
    /* bass */
    if (par[P_BASS] > 0 && a->bass > 0.0f) {
        char ch = g->b[s];
        int n = -1, art = bass_t ? BA_THUMB : BA_FINGER, j, gapn = 16 - s;
        float v = (0.78f + 0.2f * frand()) * a->bass;
        int root = 28 + ((c->b - 4) % 12 + 12) % 12;
        if (ch >= 'a' && ch <= 'z' && ch != 'g' && dens < 2)
            ch = '.';
        if (ch == 'g' && dens < 3)
            ch = '.';
        if (dens == 0 && (s & 1))
            ch = '.';
        if (sg.push[bar] && s >= 13)
            ch = push_now ? 'N' : '.';
        if (pushed_in)
            ch = '.';
        for (j = s + 1; j < 16; j++) {           /* how long: to the next note of the pattern */
            char x = g->b[j];
            if (x != '.' && !((x >= 'a' && x <= 'z' && x != 'g' && dens < 2) || (x == 'g' && dens < 3))) {
                gapn = j - s;
                break;
            }
        }
        switch (ch) {
        case 'R': case 'r': n = root; break;
        case 'O': case 'o':
            n = root + 12;
            if (bass_t)
                art = BA_POP;                    /* slapped: the octaves pop */
            break;
        case 'F': case 'f': n = root + ((c->q == Q_M7B5) ? 6 : 7); break;
        case 'P': case 'p':
            n = root + 12;
            if (bass_t)
                art = BA_POP;
            break;
        case 'A': case 'a': {                    /* a semitone into the next chord, when one comes */
            const chd_t *nx = chord_at(abs + 1 + (s == 15 ? 0 : 1));
            if (!chd_eq(nx, c)) {
                int nr = 28 + ((nx->b - 4) % 12 + 12) % 12;
                n = nr + (nr > root ? -1 : 1);
            } else {
                n = (ch == 'A') ? root + 12 : root + 7;
            }
            break;
        }
        case 'g': n = sg.bass_last; art = BA_GHOST; v *= 0.6f; break;
        case 'N': {
            const chd_t *nx = chord_at(abs);
            n = 28 + ((nx->b - 4) % 12 + 12) % 12;
            gapn = 4;
            v = 0.95f;
            break;
        }
        default: break;
        }
        if (n >= 0) {
            float hold = art == BA_GHOST ? 0.25f : bass_t ? (art == BA_POP ? 0.4f : 0.55f) : 0.88f;
            if (n > 52)
                n -= 12;
            sched(E_BASS, sw + hum(1.5f) + 60, n, v, (int32_t)((float)gapn * step_len * hold), art);
            LOG('B', n, v, gapn);
            if (art != BA_GHOST)
                sg.bass_last = n;
        }
    }
    /* drums */
    if (par[P_DRUMS] > 0 && a->drums > 0.0f) {
        char kc = g->k[s], sc = g->s[s], hc = g->h[s];
        int fill_tom = last_bar && (next_type == SEC_CHORUS || next_type == SEC_LAST) && s >= 8 && s < 14;
        int fill_sn = last_bar && (next_type == SEC_VERSE || next_type == SEC_PRE || next_type == SEC_BRIDGE ||
                                   next_type == SEC_SOLO) && s >= 12 && dens >= 1 &&
                      (en_track_rand(en_track_seed, (uint32_t)bar) & 1u);
        int crash_now = (s == 0 && bin == 0 && !pushed_in &&
                         (st == SEC_INTRO || st == SEC_CHORUS || st == SEC_LAST || st == SEC_INTER || st == SEC_SOLO)) ||
                        (push_now && next_type >= 0 &&
                         (next_type == SEC_CHORUS || next_type == SEC_LAST || next_type == SEC_INTER ||
                          next_type == SEC_SOLO));
        float da = dv * a->drums;
        if (sg.push[bar] && s >= 13)
            kc = push_now ? 'X' : '.';
        if (pushed_in)
            kc = '.';
        if (fill_tom) {
            static const int8_t T[6] = {0, 0, 1, 1, 2, 2};
            sched(E_TOM, sw + hum(1.5f), T[s - 8], da * (0.7f + 0.05f * (float)(s - 8)), 0, 0);
            LOG('D', 45, da, 1);
            if (s == 8 || s == 12)
                sched(E_KICK, sw, 0, da * 0.7f, 0, 0);
        } else if (fill_sn) {
            sched(E_SNARE, sw + hum(1.5f), 0, da * (0.45f + 0.12f * (float)(s - 12)), 0, 0);
            LOG('D', 38, da, 1);
        } else {
            if (kc != '.') {
                sched(E_KICK, sw + hum(1.0f) + 40, 0, da * (kc == 'X' ? 0.95f : 0.7f), 0, 0);
                LOG('D', 36, da, 1);
            }
            if (sc == 'X') {
                int rimv = sty->rim && (st == SEC_VERSE || st == SEC_INTRO) && sty->feel != F_DANCE;
                sched(rimv ? E_RIM : E_SNAREG, sw + hum(1.5f), 0, da * (0.85f + 0.1f * frand()), 0, 0);
                LOG('D', rimv ? 37 : 38, da, 1);
            } else if (sc == 'g' && dens >= 2) {
                sched(E_SNARE, sw + hum(2.0f), 0, da * 0.22f, 0, 0);
            }
            if (hc != '.') {
                float v = hc == 'X' ? 0.62f : hc == 'x' ? 0.48f : hc == 'O' ? 0.45f : 0.3f;
                int open = hc == 'O' || (s == 14 && (st == SEC_CHORUS || st == SEC_LAST || st == SEC_FADE) &&
                                         (en_track_seed & 4u) && hc == 'x');
                if (hc == 's' && (st == SEC_VERSE || st == SEC_BRIDGE) && dens < 2)
                    v = 0.0f;                    /* the verses: eighths */
                v *= 0.75f + 0.5f * spark();
                if (v > 0.0f) {
                    sched(open ? E_OHAT : E_HAT, sw + hum(2.0f), 0, da * v, 0, 0);
                    LOG('D', open ? 46 : 42, da * v, 1);
                }
            }
        }
        if (crash_now) {
            sched(E_CRASH, sw, 0, da * 0.6f, 0, 0);
            LOG('D', 49, da, 1);
        }
    }
    /* brass: the section's first hit, and answers in the melody's breaths */
    if (par[P_BRASS] > 0) {
        int sect_hit = (s == 0 && bin == 0 && !pushed_in && st != SEC_FADE && a->brass > 0.0f) ||
                       (push_now && next_type >= 0 && ARR[next_type].brass > 0.0f && next_type != SEC_FADE);
        float ab = sect_hit && push_now ? ARR[next_type].brass : a->brass;
        float bl = (0.45f + 0.55f * (float)par[P_BRASS] * 0.01f) * ab;
        int hit = 0, len = 2;
        if (ab <= 0.0f) {
        } else if (sect_hit) {
            hit = 1;
            len = 3;
        } else if (s >= 8 && ((st == SEC_INTRO || st == SEC_INTER) ? (bin & 1) : (bin & 1) && st != SEC_BRIDGE)) {
            /* the phrase's second bar, its back half, if the melody rests there */
            int j, busy = 0;
            for (j = sg.mel_i; j < sg.nmel && sg.mel[j].at < (bar + 1) * 16; j++)
                if (sg.mel[j].at + sg.mel[j].dur > bar * 16 + 8)
                    busy = 1;
            if (sg.mel_i > 0 && sg.mel[sg.mel_i - 1].at + sg.mel[sg.mel_i - 1].dur > bar * 16 + 8)
                busy = 1;
            if (!busy) {
                char rc = RIFFS[sg.riff][s - 8];
                if (rc == 'X')
                    hit = 1;
                else if (rc == 'L')
                    hit = 1, len = 4;
            }
            if (sg.push[bar] && s >= 13)
                hit = 0;
        }
        if (hit) {
            const chd_t *bc = chord_at(abs);
            int top[3], k;
            (void)bc;
            for (k = 0; k < 3; k++) {            /* the section's three: the strings' upper notes */
                top[k] = sg.svo[k + 1];
                while (top[k] < 62)
                    top[k] += 12;
                while (top[k] > 79)
                    top[k] -= 12;
                sched(E_BRASS, sw + hum(2.0f) + 80, top[k], bl * (0.9f + 0.1f * frand()),
                      (int32_t)((float)len * step_len * 0.8f), k);
                LOG('H', top[k], bl, len);
            }
        }
    }
    /* the lead: the melody */
    while (sg.mel_i < sg.nmel && sg.mel[sg.mel_i].at < abs)
        sg.mel_i++;
    while (sg.mel_i < sg.nmel && sg.mel[sg.mel_i].at == abs) {
        const mnote_t *m = &sg.mel[sg.mel_i];
        if (par[P_LEAD] > 0) {
            float v = (float)m->vel * 0.01f;
            int32_t gate = (int32_t)((float)m->dur * step_len * (m->leg ? 1.02f : 0.9f));
            sched(E_LEAD, 150 + hum(2.0f), m->note, v, gate, m->leg);
            LOG('L', m->note, v, m->dur);
        }
        sg.mel_i++;
    }
    if (s % 4 == 0)
        en_beats++;
    /* on */
    if (++sg.step >= 16) {
        sg.step = 0;
        sg.bar++;
        if (sg.bar >= sg.bars - 8 && en_repeat_on && sg.sec[sg.secof[sg.bars - 1]].type == SEC_FADE) {
            int k;                               /* repeat: the last chorus again, not the fade */
            sg.bar = sg.sec[sg.secof[sg.bars - 9]].bar0;
            for (k = 0; k < sg.nmel; k++)
                if (sg.mel[k].at >= sg.bar * 16)
                    break;
            sg.mel_i = k;
        }
        if (sg.bar >= sg.bars) {                 /* the tape's end: a pause, it turns over */
            en_phase = PH_GAP;
            en_section = SEC_GAP;
            gap_left = (int)(EN_SR * 2.4f);
        }
    }
    tempo_set();
}

/* ------------------------------------------------------------- control --- */
static void quiet(void)
{
    int i;
    for (i = 0; i < NKV; i++)
        kv[i].on = 0;
    for (i = 0; i < NGV; i++)
        gv[i].on = 0;
    for (i = 0; i < NBV; i++)
        bv[i].on = 0;
    for (i = 0; i < NSV; i++)
        sv[i].on = 0, sv[i].amp = 0.0f;
    for (i = 0; i < NEV; i++)
        evq[i].used = 0;
    bs.on = 0;
    bs.amp = 0.0f;
    ld.on = 0;
    ld.amp = 0.0f;
    str_level = str_target = 0.0f;
    kick.amp = kick.click = snare.tamp = snare.namp = snare.gamp = rim.amp = rim.namp = hat.amp = 0.0f;
    tom.amp = crash.amp = 0.0f;
}

static void play_on(int on)
{
    int i;
    if (on && !en_playing) {
        en_playing = 1;
        fade = 0.0f;
        if (fade_t > 0.5f)
            fade_rate = 1.0f / (0.4f * EN_SR);
    } else if (!on && en_playing) {
        en_playing = 0;
        for (i = 0; i < NEV; i++)
            evq[i].used = 0;
        for (i = 0; i < NKV; i++)
            if (kv[i].on && !kv[i].player)
                kv[i].gate = 0;
        for (i = 0; i < NGV; i++)
            gv[i].gate = 0;
        for (i = 0; i < NBV; i++)
            bv[i].gate = 0;
        ld.gate = 0;
        bs.gate = 0;
        str_target = 0.0f;
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
        case C_STYLE: style_i = sidx(a); break;
        case C_PLAY: play_on(a); break;
        case C_SKIP:
            if (en_phase != PH_GAP && !skipping) {
                skipping = 1;
                fade_t = 0.0f;
                fade_rate = 1.0f / (1.0f * EN_SR);
            }
            break;
        case C_REPEAT: en_repeat_on = (uint8_t)(a != 0); break;
        case C_TRACK_SEED_HI: en_seed_base = (en_seed_base & 0xFFFFu) | ub << 16; break;
        case C_TRACK_SEED_LO: en_seed_base = (en_seed_base & 0xFFFF0000u) | ub; break;
        case C_TRACK_NO:
            en_track_no = (uint32_t)a << 16 | ub;   /* the next is no + 1 */
            quiet();
            new_track();
            break;
        case C_NOTE:
            if (b > 0)
                key_on(a, (float)b * (1.0f / 127.0f), -1, 1);
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
    for (i = 0; i < CH_N; i++)
        ch_buf[i] = 0.0f;
    for (i = 0; i < 4; i++) {
        sg.kvo[i] = 56 + 4 * i;
        sg.gvo[i] = 61 + 4 * i;
        sg.svo[i] = 63 + 4 * i;
    }
    q_r = q_w;
    style_i = 0;
    en_repeat_on = 0;
    quiet();
    en_track_no = 0;
    new_track();
}

/* -------------------------------------------------------------- render --- */
static float bufl[BLK], bufr[BLK], rsend[BLK], esend[BLK], csend[BLK];
static float lv_env;                             /* the leveller's envelope */
static float hp_l, hp_r, sh_l, sh_r;
static float vu_acc_l, vu_acc_r;
static uint32_t vu_n;
static float band_pk[5];

static void render_keys(uint32_t n)
{
    int i;
    uint32_t j;
    float lv = (float)par[P_KEYS] * 0.01f * 0.30f, pk = 0.0f;
    static float kl[BLK], kr[BLK];
    for (j = 0; j < n; j++)
        kl[j] = kr[j] = 0.0f;
    for (i = 0; i < NKV; i++) {
        kv_t *v = &kv[i];
        float pc, pt, amp, idx, tine, att, fl = v->idx_floor, id = v->idx_dec, gl = v->gl, gr = v->gr, ic = v->ic,
                                         it = v->it;
        uint32_t seg, j0 = 0;
        if (!v->on)
            continue;
        pc = v->pc, pt = v->pt, amp = v->amp, idx = v->idx, tine = v->tine, att = v->att;
        while (j0 < n) {                         /* in runs: held (decaying), then damped (released) */
            float m = v->gate == 0 ? v->rel : v->dec;
            seg = n - j0;
            if (v->gate > 0 && (uint32_t)v->gate < seg)
                seg = (uint32_t)v->gate;
            if (tine > 0.0003f || att < 1.0f) {
                for (j = j0; j < j0 + seg; j++) {
                    float s = sn(pc + sn(pc) * idx) * amp * att + sn(pt) * tine;
                    kl[j] += s * gl;
                    kr[j] += s * gr;
                    pc = wrap1(pc + ic);
                    pt = wrap1(pt + it);
                    att = fm_minf(att + 0.0075f, 1.0f);
                    amp *= m;
                    idx = fl + (idx - fl) * id;
                    tine *= v->tine_dec;
                }
            } else {                             /* the rest: one modulator, one carrier */
                for (j = j0; j < j0 + seg; j++) {
                    float s = sn(pc + sn(pc) * idx) * amp;
                    kl[j] += s * gl;
                    kr[j] += s * gr;
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
        if (amp < 0.0008f && tine < 0.0008f)
            v->on = 0;
    }
    lv *= 1.0f - 0.18f * duck;
    for (j = 0; j < n; j++) {
        float l = kl[j] * lv, r = kr[j] * lv;
        bufl[j] += l;
        bufr[j] += r;
        csend[j] += (l + r) * 0.5f;
        rsend[j] += (l + r) * 0.35f;
        pk = fm_maxf(pk, fm_fabsf(l));
    }
    band_pk[1] = fm_maxf(band_pk[1], pk * 3.0f);
}

static void render_guitar(uint32_t n)
{
    int i;
    uint32_t j;
    float lv = (float)par[P_GUITAR] * 0.01f * 0.34f, pk = 0.0f;
    static float gl_[BLK], gr_[BLK], ghp;
    int any = 0;
    for (i = 0; i < NGV; i++)
        any |= gv[i].on;
    if (!any)
        return;
    for (j = 0; j < n; j++)
        gl_[j] = gr_[j] = 0.0f;
    for (i = 0; i < NGV; i++) {
        float *b = ks_buf[i], apc = gv[i].apc, apx = gv[i].apx, apy = gv[i].apy, lp = gv[i].lp, a, fb;
        float amp = gv[i].amp, pl = gv[i].gl, pr = gv[i].gr, e = 0.0f;
        int w = gv[i].w, nn = gv[i].n;
        if (!gv[i].on)
            continue;
        for (j = 0; j < n; j++) {
            float x, y;
            int r = w - nn;
            if (gv[i].gate > 0) {
                gv[i].gate--;
                a = gv[i].a;
                fb = gv[i].fb;
            } else {                             /* the palm: damped, darker */
                a = gv[i].mute_a;
                fb = gv[i].mute_fb;
            }
            if (r < 0)
                r += KSN;
            x = b[r];
            y = apc * (x - apy) + apx;           /* the all-pass: the fraction of the period */
            apx = x;
            apy = y;
            lp += (y - lp) * a;
            b[w] = lp * fb;
            if (++w >= KSN)
                w = 0;
            y *= amp;
            gl_[j] += y * pl;
            gr_[j] += y * pr;
            e = fm_maxf(e, fm_fabsf(y));
        }
        gv[i].w = w, gv[i].apx = apx, gv[i].apy = apy, gv[i].lp = lp;
        if (e < 0.0004f && gv[i].gate <= 0)
            gv[i].on = 0;
    }
    for (j = 0; j < n; j++) {                    /* a pickup's thin top: the lows taken off, a touch of edge */
        float l = gl_[j], r = gr_[j], m;
        ghp += ((l + r) * 0.5f - ghp) * 0.045f;
        l = (l - ghp) * lv;
        r = (r - ghp) * lv;
        m = (l + r) * 0.5f;
        bufl[j] += l;
        bufr[j] += r;
        csend[j] += m * 1.3f;
        rsend[j] += m * 0.3f;
        pk = fm_maxf(pk, fm_fabsf(m));
    }
    band_pk[2] = fm_maxf(band_pk[2], pk * 3.0f);
}

static void render_bass(uint32_t n)
{
    uint32_t j;
    float lv = (float)par[P_BASS] * 0.01f * 0.40f, glide = 1.0f - tau(0.012f), rel = tau(0.035f), pk = 0.0f;
    float att = 1.0f / (0.004f * EN_SR);
    if (!bs.on)
        return;
    lv *= 1.0f - 0.2f * duck;
    for (j = 0; j < n; j++) {
        float s;
        bs.inc += (bs.tinc - bs.inc) * glide;
        s = sn(bs.ph + sn(bs.ph) * bs.idx) + 0.15f * sn(bs.ph * 2.0f);
        bs.ph = wrap1(bs.ph + bs.inc);
        if (bs.gate > 0) {
            bs.gate--;
            if (bs.atk) {
                bs.amp += att * bs.peak;
                if (bs.amp >= bs.peak)
                    bs.amp = bs.peak, bs.atk = 0;
            } else {
                bs.amp = bs.sus + (bs.amp - bs.sus) * bs.dec;
            }
        } else {
            bs.amp *= rel;
        }
        bs.idx = bs.idx_floor + (bs.idx - bs.idx_floor) * bs.idec;
        s = s * bs.amp + noise() * bs.click;
        bs.click *= 0.9f;
        s = s * (1.0f - 0.1f * s * s);
        bs.lp += (s - bs.lp) * bs.lpa;
        bs.lp2 += (bs.lp - bs.lp2) * bs.lpa;
        s = bs.lp2 * lv;
        bufl[j] += s;
        bufr[j] += s;
        pk = fm_maxf(pk, fm_fabsf(s));
    }
    if (bs.gate <= 0 && bs.amp < 0.0005f)
        bs.on = 0;
    band_pk[0] = fm_maxf(band_pk[0], pk * 2.5f);
}

static void render_drums(uint32_t n)
{
    uint32_t j;
    float lv = (float)par[P_DRUMS] * 0.01f * 0.62f, pk = 0.0f;
    float fs = 2.0f * fm_sinf(FM_PI * 1900.0f / EN_SR), fr = 2.0f * fm_sinf(FM_PI * 1700.0f / EN_SR);
    float hha = onepole(6500.0f + 2000.0f * spark()), hlp = onepole(11000.0f), slp = onepole(6000.0f);
    float gate_rel = tau(0.006f), clp = onepole(9000.0f), cha = onepole(4000.0f);
    if (kick.amp < 1e-3f && snare.tamp < 1e-3f && snare.namp < 1e-3f && snare.gamp < 1e-3f && rim.amp < 1e-3f &&
        rim.namp < 1e-3f && hat.amp < 1e-3f && tom.amp < 1e-3f && crash.amp < 1e-3f)
        return;
    for (j = 0; j < n; j++) {
        float k = 0.0f, s = 0.0f, h = 0.0f, x, cl = 0.0f, cr = 0.0f;
        if (kick.amp > 1e-3f) {
            kick.f = kick.fend + (kick.f - kick.fend) * kick.pdec;
            kick.ph = wrap1(kick.ph + kick.f / EN_SR);
            k = sn(kick.ph) * kick.amp + noise() * kick.click;
            kick.amp *= kick.adec;
            kick.click *= 0.9f;
        }
        if (tom.amp > 1e-3f) {
            tom.f = tom.fend + (tom.f - tom.fend) * tom.pdec;
            tom.ph = wrap1(tom.ph + tom.f / EN_SR);
            k += sn(tom.ph) * tom.amp * 0.8f;
            tom.amp *= tom.adec;
        }
        if (snare.tamp > 1e-3f || snare.namp > 1e-3f || snare.gamp > 1e-3f) {
            float hp, nz = noise();
            x = nz * snare.namp;
            snare.lo += fs * snare.bd;           /* Chamberlin's state-variable filter: its band, a little high */
            hp = x - snare.lo - 0.9f * snare.bd;
            snare.bd += fs * hp;
            s = snare.bd + 0.4f * hp + sn(snare.ph) * snare.tamp;
            snare.ph = wrap1(snare.ph + snare.inc);
            snare.tamp *= snare.tdec;
            snare.namp *= snare.ndec;
            snare.glp += (nz * snare.gamp - snare.glp) * slp;   /* the gated room's burst */
            if (snare.glen > 0.0f)
                snare.glen -= 1.0f;
            else
                snare.gamp *= gate_rel;
            s += snare.glp * 1.4f;
        }
        if (rim.amp > 1e-3f || rim.namp > 1e-3f) {
            x = noise() * rim.namp;
            rim.lo += fr * rim.bd;
            rim.bd += fr * (x - rim.lo - 0.35f * rim.bd);
            s += rim.bd * 0.5f + sn(rim.ph) * rim.amp;
            rim.ph = wrap1(rim.ph + rim.inc);
            rim.amp *= rim.dec;
            rim.namp *= rim.ndec;
        }
        if (hat.amp > 1e-3f) {
            x = noise();
            hat.h1 += (x - hat.h1) * hha;
            x -= hat.h1;
            hat.h2 += (x - hat.h2) * hha;
            x -= hat.h2;
            hat.lp += (x - hat.lp) * hlp;
            hat.lp2 += (hat.lp - hat.lp2) * hlp;
            h = hat.lp2 * hat.amp * 0.9f;
            hat.amp *= hat.dec;
        }
        if (crash.amp > 1e-3f) {                 /* the crash: two noises (a little wide), high-passed */
            float a = noise(), b2 = noise();
            crash.h1 += (a - crash.h1) * cha;
            crash.h2 += (b2 - crash.h2) * cha;
            a -= crash.h1;
            b2 -= crash.h2;
            crash.h3 += (a - crash.h3) * clp;
            crash.h4 += (b2 - crash.h4) * clp;
            cl = crash.h3 * crash.amp * 0.5f;
            cr = crash.h4 * crash.amp * 0.5f;
            crash.amp *= crash.dec;
        }
        x = k * 0.8f + s * 0.62f;
        bufl[j] += (x + h * 0.8f + cl) * lv;
        bufr[j] += (x + h * 1.05f + cr) * lv;
        rsend[j] += (s * 0.55f + (cl + cr) * 0.2f) * lv;
        pk = fm_maxf(pk, fm_fabsf(x * lv));
    }
    band_pk[4] = fm_maxf(band_pk[4], pk * 2.0f);
}

static void render_lead(uint32_t n)
{
    uint32_t j;
    float lv = (float)par[P_LEAD] * 0.01f * 0.26f, sp = spark(), pk = 0.0f;
    float glide = 1.0f - tau(0.035f), rel = tau(lead_t ? 0.07f : 0.09f), att = 1.0f / ((lead_t ? 0.03f : 0.012f) * EN_SR);
    float base_fc = lead_t ? 2600.0f + 1800.0f * sp : 2200.0f + 3000.0f * sp;
    if (!ld.on)
        return;
    for (j = 0; j < n; j++) {
        float s, inc, vib, fc, a;
        ld.inc += (ld.tinc - ld.inc) * glide;
        ld.vib = fm_minf(ld.vib + 1.0f / (0.35f * EN_SR), 1.0f);
        vib = sn(ld.vph) * ld.vib * ld.vib * (lead_t ? 0.0055f : 0.0035f);
        ld.vph = wrap1(ld.vph + (lead_t ? 5.0f : 5.6f) / EN_SR);
        inc = ld.inc * (1.0f + vib - ld.scoop);
        ld.scoop *= 0.9993f;
        if (ld.gate > 0) {
            ld.gate--;
            if (ld.att < 1.0f)
                ld.att = fm_minf(ld.att + att, 1.0f);
            ld.amp = ld.peak * ld.att;
        } else {
            ld.amp *= rel;
        }
        if (lead_t) {                            /* the soprano: FM, its brightness the breath; a little air */
            float br = 0.7f + 1.5f * ld.amp;
            s = sn(ld.ph1 + sn(ld.ph1 * 2.0f) * 0.25f * br + sn(ld.ph1) * br);
            ld.ph1 = wrap1(ld.ph1 + inc);
            s += noise() * 0.06f * ld.amp;
        } else {                                 /* the synth: two saws a little apart */
            float i2 = inc * 1.0045f;
            s = (2.0f * ld.ph1 - 1.0f - blep(ld.ph1, inc)) + 0.8f * (2.0f * ld.ph2 - 1.0f - blep(ld.ph2, i2));
            s *= 0.55f;
            ld.ph1 = wrap1(ld.ph1 + inc);
            ld.ph2 = wrap1(ld.ph2 + i2);
        }
        ld.bright *= 0.9997f;
        fc = base_fc * (0.75f + 0.5f * ld.bright);
        a = fc * (FM_TWO_PI / EN_SR);
        a = a > 0.9f ? 0.9f : a;
        ld.lp1 += (s - ld.lp1) * a;
        ld.lp2 += (ld.lp1 - ld.lp2) * a;
        s = ld.lp2 * ld.amp * lv;
        bufl[j] += s * 0.9f;
        bufr[j] += s * 0.9f;
        esend[j] += s;
        rsend[j] += s * 0.45f;
        pk = fm_maxf(pk, fm_fabsf(s));
    }
    if (ld.gate <= 0 && ld.amp < 0.0005f)
        ld.on = 0;
    band_pk[3] = fm_maxf(band_pk[3], pk * 3.0f);
    en_lead_glow = fm_maxf(en_lead_glow, fm_minf(pk * 6.0f, 1.0f));
}

static void render_brass(uint32_t n)
{
    int i;
    uint32_t j;
    float lv = (float)par[P_BRASS] * 0.01f * 0.16f, ia = 1.0f / (0.018f * EN_SR), aa = 1.0f / (0.010f * EN_SR);
    float idec = tau(0.15f), adec = tau(0.25f), rel = tau(0.05f), sp = spark();
    for (i = 0; i < NBV; i++) {
        float ph = bv[i].ph, gl = 0.8f - bv[i].pan, gr = 0.8f + bv[i].pan;
        if (!bv[i].on)
            continue;
        for (j = 0; j < n; j++) {
            float s, ix;
            if (bv[i].gate > 0) {
                bv[i].gate--;
                if (bv[i].att < 1.0f) {
                    bv[i].att += aa;
                    bv[i].amp = bv[i].peak * fm_minf(bv[i].att, 1.0f);
                } else {
                    bv[i].amp = bv[i].peak * 0.6f + (bv[i].amp - bv[i].peak * 0.6f) * adec;
                }
                bv[i].ienv = bv[i].ienv < 1.0f ? bv[i].ienv + ia : 0.55f + (bv[i].ienv - 0.55f) * idec;
            } else {
                bv[i].amp *= rel;
                bv[i].ienv *= rel;
            }
            ix = bv[i].ienv * (1.6f + 1.0f * sp);
            s = sn(ph + sn(ph) * ix) * bv[i].amp;
            ph = wrap1(ph + bv[i].inc * (1.0f - bv[i].scoop));
            bv[i].scoop *= 0.9985f;
            s *= lv;
            bufl[j] += s * gl;
            bufr[j] += s * gr;
            rsend[j] += s * 0.5f;
        }
        bv[i].ph = ph;
        if (bv[i].gate <= 0 && bv[i].amp < 0.0005f)
            bv[i].on = 0;
    }
}

static void render_strings(uint32_t n)
{
    int i;
    uint32_t j;
    float lv = (float)par[P_STRINGS] * 0.01f * 0.075f, glide = 1.0f - tau(0.06f), a, sp = spark();
    float rate = str_target > str_level ? 1.0f / (0.9f * EN_SR) : 1.0f / (1.4f * EN_SR);
    static float sl[BLK], sr[BLK];
    if (str_level < 1e-4f && str_target <= 0.0f)
        return;
    a = onepole(1800.0f + 2200.0f * sp);
    for (j = 0; j < n; j++)
        sl[j] = sr[j] = 0.0f;
    for (i = 0; i < NSV; i++) {
        float p1 = sv[i].p1, p2 = sv[i].p2;
        if (!sv[i].on)
            continue;
        for (j = 0; j < n; j++) {
            float i1, i2;
            sv[i].inc += (sv[i].tinc - sv[i].inc) * glide;
            i1 = sv[i].inc * 0.9972f;
            i2 = sv[i].inc * 1.0031f;
            sl[j] += 2.0f * p1 - 1.0f - blep(p1, i1);
            sr[j] += 2.0f * p2 - 1.0f - blep(p2, i2);
            p1 = wrap1(p1 + i1);
            p2 = wrap1(p2 + i2);
        }
        sv[i].p1 = p1, sv[i].p2 = p2;
    }
    for (j = 0; j < n; j++) {
        float l, r;
        str_level = str_level < str_target ? fm_minf(str_level + rate, str_target) : fm_maxf(str_level - rate, str_target);
        str_lp1l += (sl[j] - str_lp1l) * a;
        str_lp2l += (str_lp1l - str_lp2l) * a;
        str_lp1r += (sr[j] - str_lp1r) * a;
        str_lp2r += (str_lp1r - str_lp2r) * a;
        l = str_lp2l * str_level * lv;
        r = str_lp2r * str_level * lv;
        bufl[j] += l;
        bufr[j] += r;
        csend[j] += (l + r) * 0.4f;
        rsend[j] += (l + r) * 0.6f;
    }
}

/* the chorus (Gloss), the plate, the lead's echo */
static void render_space(uint32_t n)
{
    uint32_t j;
    float gloss = (float)par[P_GLOSS] * 0.01f;
    float vs = 0.25f + 0.55f * gloss, damp = 0.38f, g = 0.80f, ea = onepole(3200.0f), efb = 0.22f + 0.2f * gloss;
    float cw = 0.25f + 0.55f * gloss, dep = 70.0f + 60.0f * gloss;
    float lfo1 = sn(ch_ph), lfo2 = sn(ch_ph + 0.31f);
    float d1 = 300.0f + dep * lfo1, d2 = 360.0f + dep * lfo2, dd1, dd2;
    float l1 = 300.0f + dep * sn(ch_ph + (float)n * (0.55f / EN_SR)), l2 = 360.0f + dep * sn(ch_ph + 0.31f + (float)n * (0.55f / EN_SR));
    dd1 = (l1 - d1) / (float)n;
    dd2 = (l2 - d2) / (float)n;
    ch_ph = wrap1(ch_ph + (float)n * (0.55f / EN_SR));
    for (j = 0; j < n; j++) {
        float in = rsend[j] * vs, x[4], sum, yl, yr, e, rd, cl, cr, fr;
        int i, ri;
        /* the chorus: one line, two taps moving apart */
        ch_buf[ch_w] = csend[j];
        rd = (float)ch_w - d1;
        if (rd < 0.0f)
            rd += (float)CH_N;
        ri = (int)rd;
        fr = rd - (float)ri;
        cl = ch_buf[ri & (CH_N - 1)] + (ch_buf[(ri + 1) & (CH_N - 1)] - ch_buf[ri & (CH_N - 1)]) * fr;
        rd = (float)ch_w - d2;
        if (rd < 0.0f)
            rd += (float)CH_N;
        ri = (int)rd;
        fr = rd - (float)ri;
        cr = ch_buf[ri & (CH_N - 1)] + (ch_buf[(ri + 1) & (CH_N - 1)] - ch_buf[ri & (CH_N - 1)]) * fr;
        ch_w = (ch_w + 1) & (CH_N - 1);
        d1 += dd1;
        d2 += dd2;
        /* the plate */
        for (i = 0; i < 2; i++) {
            float *d = ap_d[i], v = d[ap_p[i]], w = in + 0.62f * v;
            d[ap_p[i]] = w;
            in = v - 0.62f * w;
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
        /* the echo: a dotted eighth */
        rd = (float)ec_w - ec_len;
        if (rd < 0.0f)
            rd += (float)EC_N;
        ri = (int)rd;
        e = (float)ec_buf[ri & (EC_N - 1)] * (1.0f / 16384.0f);
        ec_lp += (e - ec_lp) * ea;
        {
            float w = esend[j] + ec_lp * efb;
            w = w > 1.9f ? 1.9f : w < -1.9f ? -1.9f : w;
            ec_buf[ec_w] = (int16_t)(w * 16384.0f);
            ec_w = (ec_w + 1) & (EC_N - 1);
        }
        bufl[j] += yl * 0.8f + ec_lp * (0.18f + 0.3f * gloss) + cl * cw * 0.6f;
        bufr[j] += yr * 0.8f + ec_lp * (0.28f + 0.3f * gloss) + cr * cw * 0.6f;
    }
}

/* the bus: Sparkle's shelf, a soft knee, the fades, the leveller, MASTER */
static void render_bus(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    float shk = ((float)par[P_SPARKLE] - 50.0f) * 0.012f, sha = onepole(2800.0f);
    float lv_rel = tau(0.5f), lv_att = 1.0f - tau(0.002f), hp_a = onepole(25.0f), vol, x;
    for (j = 0; j < n; j++) {
        float l = bufl[j], r = bufr[j];
        sh_l += (l - sh_l) * sha;
        sh_r += (r - sh_r) * sha;
        l += (l - sh_l) * shk;
        r += (r - sh_r) * shk;
        l = l * (27.0f + l * l) / (27.0f + 9.0f * l * l);
        r = r * (27.0f + r * r) / (27.0f + 9.0f * r * r);
        fade = fade < fade_t ? fm_minf(fade + fade_rate, fade_t) : fm_maxf(fade - fade_rate, fade_t);
        l *= fade;
        r *= fade;
        hp_l += (l - hp_l) * hp_a;
        hp_r += (r - hp_r) * hp_a;
        l -= hp_l;
        r -= hp_r;
        vu_acc_l += l * l;
        vu_acc_r += r * r;
        x = fm_maxf(fm_fabsf(l), fm_fabsf(r)) * gain;
        lv_env = x > lv_env ? lv_env + (x - lv_env) * lv_att : lv_env * lv_rel;
        vol = lv_env > 0.5f ? 0.5f / lv_env : 1.0f;
        l *= gain * vol;
        r *= gain * vol;
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
    }
}

static void control(uint32_t n)
{
    float k = (float)n / EN_SR;
    int i;
    en_kick_glow *= fm_expf(-k * 9.0f);
    en_lead_glow *= fm_expf(-k * 4.0f);
    duck *= fm_expf(-k * 8.0f);
    if (++vu_n >= 8u) {                          /* the VU: about 46 ms of RMS, then ballistics */
        float l = fm_sqrtf(vu_acc_l / (float)(n * 8u)), r = fm_sqrtf(vu_acc_r / (float)(n * 8u));
        en_vu_l = en_vu_l + (fm_minf(l * 4.0f, 1.2f) - en_vu_l) * 0.45f;
        en_vu_r = en_vu_r + (fm_minf(r * 4.0f, 1.2f) - en_vu_r) * 0.45f;
        vu_acc_l = vu_acc_r = 0.0f;
        vu_n = 0;
        for (i = 0; i < 5; i++) {
            float t = fm_minf(band_pk[i], 1.0f);
            en_band[i] = t > en_band[i] ? t : en_band[i] * 0.82f;
            band_pk[i] = 0.0f;
        }
    }
}

static void fire(ev_t *e)
{
    switch (e->type) {
    case E_KEY: key_on(e->note, e->vel, e->gate, 0); break;
    case E_BASS: bass_on(e->note, e->vel, e->gate, e->x); break;
    case E_KICK: kick_hit(e->vel); break;
    case E_SNARE: snare_hit(e->vel, 0); break;
    case E_SNAREG: snare_hit(e->vel, 1); break;
    case E_RIM: rim_hit(e->vel); break;
    case E_HAT: hat_hit(e->vel, 0); break;
    case E_OHAT: hat_hit(e->vel, 1); break;
    case E_TOM: tom_hit(e->note, e->vel); break;
    case E_CRASH: crash_hit(e->vel); break;
    case E_GTR: gtr_on(e->x & 3, e->note, e->vel, e->gate, e->x >> 7); break;
    case E_LEAD: lead_on(e->note, e->vel, e->gate, e->x); break;
    case E_BRASS: brass_on(e->note, e->vel, e->gate, e->x); break;
    case E_STR: {
        int i = e->x & 3;
        sv[i].tinc = mtof((float)e->note) / EN_SR;
        if (!sv[i].on || sv[i].amp <= 0.0f) {
            sv[i].inc = sv[i].tinc;
            sv[i].p1 = 0.25f * (float)i;
            sv[i].p2 = 0.6f + 0.1f * (float)i;
        }
        sv[i].on = 1;
        sv[i].amp = 1.0f;
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
        int i, seq = 0;
        if (en_playing) {
            if (en_phase == PH_GAP) {
                if (gap_left <= 0) {
                    if (en_repeat_on)
                        en_track_no--;           /* the same song again */
                    new_track();
                } else if ((int)k > gap_left) {
                    k = (uint32_t)gap_left;
                }
            } else if (skipping && fade < 0.002f) {   /* skipped: faded out, a short pause */
                en_phase = PH_GAP;
                en_section = SEC_GAP;
                gap_left = (int)(EN_SR * 1.2f);
                skipping = 0;
                for (i = 0; i < NEV; i++)
                    evq[i].used = 0;
            } else {
                if (sg.step_left <= 0.0f) {
                    seq_step();
                    sg.step_left += step_len;
                }
                if (en_phase != PH_GAP) {
                    seq = 1;
                    if ((float)k > sg.step_left)
                        k = (uint32_t)sg.step_left + 1u;
                }
            }
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
        if (seq) {
            sg.step_left -= (float)k;
            trk_samples += k;
            en_track_ms = (uint32_t)((float)trk_samples * (1000.0f / EN_SR));
        } else if (en_playing && en_phase == PH_GAP) {
            gap_left -= (int)k;
        }
        control(k);
        for (j = 0; j < k; j++)
            bufl[j] = bufr[j] = rsend[j] = esend[j] = csend[j] = 0.0f;
        render_keys(k);
        render_guitar(k);
        render_bass(k);
        render_drums(k);
        render_lead(k);
        render_brass(k);
        render_strings(k);
        render_space(k);
        render_bus(out, k, gain);
        out += 2 * k;
        n -= k;
    }
}

#ifdef EN_HOST
void en_song_dump(void (*line)(const char *s))
{
    int b, i;
    char t[160], c[16], *e;
    e = put_s(t, "key ");
    e = put_s(e, EN_NOTE_NAME[sg.key]);
    e = put_s(e, "  up ");
    e = put_u(e, (uint32_t)sg.up);
    e = put_s(e, "  bpm ");
    e = put_u(e, (uint32_t)cur_bpm());
    e = put_s(e, "  verse ");
    e = put_u(e, (uint32_t)sg.verse);
    e = put_s(e, " pre ");
    e = put_u(e, (uint32_t)sg.pre);
    e = put_s(e, " chorus ");
    e = put_u(e, (uint32_t)sg.chorus);
    e = put_s(e, " bridge ");
    e = put_u(e, (uint32_t)sg.bridge);
    e = put_s(e, " groove ");
    e = put_u(e, (uint32_t)sg.groove);
    e = put_s(e, keys_t ? " DX" : " Rhodes");
    e = put_s(e, lead_t ? " sax" : " synth");
    put_s(e, bass_t ? " slap" : " fingered");
    line(t);
    for (b = 0; b < sg.bars; b++) {
        const sec_t *s = &sg.sec[sg.secof[b]];
        e = put_u(t, (uint32_t)b);
        e = put_s(e, b < 10 ? "   " : "  ");
        e = put_s(e, SEC_NAME[s->type]);
        e = put_s(e, " ");
        for (i = (int)(e - t); i < 16; i++)
            *e++ = ' ';
        *e = 0;
        for (i = 0; i < 2; i++) {
            const chd_t *ch = &sg.ch[b * 2 + i];
            if (i == 1 && chd_eq(ch, &sg.ch[b * 2]))
                continue;
            e = put_s(e, "| ");
            e = put_s(e, en_chord_name(ch->r, ch->q, ch->b, c));
            e = put_s(e, " ");
        }
        if (sg.push[b])
            e = put_s(e, " >push");
        line(t);
    }
}
#endif
