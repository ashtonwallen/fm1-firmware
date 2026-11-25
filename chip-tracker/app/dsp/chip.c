/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CHIP TRACKER: ashtonwallen, 2026 */
/* The instrument (chip.h). One compilation unit, -O2. The command queue and the render's shape are AMB-1's. */
#include "chip.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")
#define NA(a) ((int)(sizeof a / sizeof a[0]))

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Tempo", 60, 200, 140}, {"Energy", 0, 100, 70}, {"Echo", 0, 100, 40}, {"Mood", 0, 100, 30},
    {"PU1", 0, 100, 75},     {"PU2", 0, 100, 75},    {"TRI", 0, 100, 75}, {"NOISE", 0, 100, 75},
    {"Swing", 0, 100, 0},    {"Tone", 0, 100, 60},   {"Loops", 1, 4, 2},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }
int en_energy_level(int v) { return v < 20 ? 0 : v < 40 ? 1 : v < 60 ? 2 : v < 80 ? 3 : 4; }

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
    static const char *const MOOD[5] = {"Happy", "Bright", "Wistful", "Moody", "Dark"};
    static const char *const NRG[5] = {"Calm", "Light", "Steady", "Lively", "Full"};
    if (i == P_MOOD)
        put_s(buf, MOOD[v < 20 ? 0 : v < 40 ? 1 : v < 60 ? 2 : v < 80 ? 3 : 4]);
    else if (i == P_ENERGY)
        put_s(buf, NRG[en_energy_level(v)]);
    else if (i == P_SWING && v == 0)
        put_s(buf, "Straight");
    else if (i == P_ECHO && v == 0)
        put_s(buf, "Off");
    else if (i == P_LOOPS)
        put_s(put_u(buf, (uint32_t)(v < 1 ? 1 : v)), v == 1 ? " loop" : " loops");
    else
        put_u(buf, (uint32_t)(v < 0 ? 0 : v));
}

/* ---------------------------------------------------------- harmony --- */
/* modes, brightest first; Mood picks among them (each has its own place on the knob) */
enum { MD_MAJOR, MD_MIXO, MD_DORIAN, MD_MINOR, MD_HARM, MD_N };
static const uint8_t MODE_PCS[MD_N][7] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 10},
};
static const uint8_t MODE_MOOD[MD_N] = {8, 32, 58, 78, 96};
static const char *const MODE_NAME[MD_N] = {"MAJOR", "MIXOLYDIAN", "DORIAN", "MINOR", "MINOR"};
const char *en_mode_name(int m) { return MODE_NAME[(unsigned)m < MD_N ? m : 0]; }

/* chords: a root (semitones above the key's tonic) and a quality, as root | q << 4 */
enum { QM = 0, Qm = 1, Qd = 2 };
static const uint8_t Q_IV[3][3] = {{0, 4, 7}, {0, 3, 7}, {0, 3, 6}};
#define CH(r, q) ((uint8_t)((r) | (q) << 4))
void en_chord_text(uint8_t c, char *b)
{
    if (c == 0xFFu) {
        b[0] = 0;
        return;
    }
    put_s(put_s(b, EN_NOTE_NAME[(c & 15u) % 12u]), (c >> 4) == Qm ? "m" : (c >> 4) == Qd ? "dim" : "");
}

/* the progressions: four bars, a chord each. Each is something players of the genre use; a song takes one for
 * its verse, another for its chorus and a third for its bridge (the bridge's lead back to the chorus) */
enum { R_V = 1, R_C = 2, R_B = 4 };
typedef struct {
    uint8_t mode, roles;
    uint8_t c[4];
} prog_t;
static const prog_t PROGS[] = {
    {MD_MAJOR, R_V | R_C, {CH(0, QM), CH(7, QM), CH(9, Qm), CH(5, QM)}},     /* I V vi IV */
    {MD_MAJOR, R_V | R_C, {CH(0, QM), CH(9, Qm), CH(5, QM), CH(7, QM)}},     /* I vi IV V */
    {MD_MAJOR, R_V, {CH(9, Qm), CH(5, QM), CH(0, QM), CH(7, QM)}},           /* vi IV I V */
    {MD_MAJOR, R_V, {CH(0, QM), CH(5, QM), CH(0, QM), CH(7, QM)}},           /* I IV I V */
    {MD_MAJOR, R_C, {CH(5, QM), CH(7, QM), CH(0, QM), CH(9, Qm)}},           /* IV V I vi */
    {MD_MAJOR, R_C, {CH(0, QM), CH(5, QM), CH(9, Qm), CH(7, QM)}},           /* I IV vi V */
    {MD_MAJOR, R_B, {CH(5, QM), CH(7, QM), CH(4, Qm), CH(9, Qm)}},           /* IV V iii vi: the royal road */
    {MD_MAJOR, R_B, {CH(2, Qm), CH(5, QM), CH(7, QM), CH(7, QM)}},           /* ii IV V V */
    {MD_MAJOR, R_B, {CH(9, Qm), CH(5, QM), CH(2, Qm), CH(7, QM)}},           /* vi IV ii V */
    {MD_MIXO, R_V | R_C, {CH(0, QM), CH(10, QM), CH(5, QM), CH(0, QM)}},     /* I bVII IV I */
    {MD_MIXO, R_V | R_C, {CH(10, QM), CH(5, QM), CH(0, QM), CH(0, QM)}},     /* bVII IV I */
    {MD_MIXO, R_V, {CH(0, QM), CH(10, QM), CH(0, QM), CH(5, QM)}},           /* I bVII I IV */
    {MD_MIXO, R_C, {CH(0, QM), CH(7, Qm), CH(10, QM), CH(5, QM)}},           /* I v bVII IV */
    {MD_MIXO, R_B, {CH(5, QM), CH(5, QM), CH(10, QM), CH(10, QM)}},          /* IV bVII */
    {MD_MIXO, R_B, {CH(2, Qm), CH(5, QM), CH(2, Qm), CH(10, QM)}},           /* ii IV ii bVII */
    {MD_DORIAN, R_V, {CH(0, Qm), CH(5, QM), CH(0, Qm), CH(5, QM)}},          /* i IV: the Dorian vamp */
    {MD_DORIAN, R_V | R_C, {CH(0, Qm), CH(3, QM), CH(5, QM), CH(0, Qm)}},    /* i bIII IV i */
    {MD_DORIAN, R_C, {CH(0, Qm), CH(2, Qm), CH(3, QM), CH(5, QM)}},          /* i ii bIII IV */
    {MD_DORIAN, R_C, {CH(0, Qm), CH(10, QM), CH(5, QM), CH(0, Qm)}},         /* i bVII IV i */
    {MD_DORIAN, R_B, {CH(3, QM), CH(5, QM), CH(10, QM), CH(5, QM)}},         /* bIII IV bVII IV */
    {MD_DORIAN, R_B, {CH(10, QM), CH(10, QM), CH(5, QM), CH(5, QM)}},        /* bVII IV */
    {MD_MINOR, R_V | R_C, {CH(0, Qm), CH(8, QM), CH(10, QM), CH(0, Qm)}},    /* i bVI bVII i */
    {MD_MINOR, R_V, {CH(0, Qm), CH(10, QM), CH(8, QM), CH(10, QM)}},         /* i bVII bVI bVII */
    {MD_MINOR, R_V, {CH(0, Qm), CH(5, Qm), CH(0, Qm), CH(7, Qm)}},           /* i iv i v */
    {MD_MINOR, R_C, {CH(0, Qm), CH(8, QM), CH(3, QM), CH(10, QM)}},          /* i bVI bIII bVII */
    {MD_MINOR, R_C, {CH(8, QM), CH(10, QM), CH(0, Qm), CH(0, Qm)}},          /* bVI bVII i */
    {MD_MINOR, R_V | R_C, {CH(0, Qm), CH(5, Qm), CH(10, QM), CH(3, QM)}},    /* i iv bVII bIII */
    {MD_MINOR, R_B, {CH(5, Qm), CH(7, Qm), CH(8, QM), CH(10, QM)}},          /* iv v bVI bVII */
    {MD_MINOR, R_B, {CH(8, QM), CH(10, QM), CH(5, Qm), CH(7, QM)}},          /* bVI bVII iv V */
    {MD_HARM, R_V | R_C, {CH(0, Qm), CH(8, QM), CH(5, Qm), CH(7, QM)}},      /* i bVI iv V */
    {MD_HARM, R_V, {CH(0, Qm), CH(5, Qm), CH(7, QM), CH(0, Qm)}},            /* i iv V i */
    {MD_HARM, R_V | R_C, {CH(0, Qm), CH(0, Qm), CH(8, QM), CH(7, QM)}},      /* i bVI V */
    {MD_HARM, R_C, {CH(0, Qm), CH(7, QM), CH(8, QM), CH(7, QM)}},            /* i V bVI V */
    {MD_HARM, R_B, {CH(8, QM), CH(5, Qm), CH(8, QM), CH(7, QM)}},            /* bVI iv bVI V */
    {MD_HARM, R_B, {CH(5, Qm), CH(5, Qm), CH(8, QM), CH(7, QM)}},            /* iv bVI V */
};

/* ------------------------------------------------------------ motifs --- */
/* A melody is built from one-bar motifs: 16 sixteenths, a token each. "cN": the Nth chord tone from the section's
 * anchor (c0 the first at or above it; negative below), so a motif over another chord comes out as its sequence;
 * "u" / "d" (or "u2", "d2"): scale steps from the note before (passing and neighbour tones); "tN": N scale steps
 * above the cadence's target; "-" the note held; "." a rest. A motif starts with a chord tone or a rest. On beats
 * one and three, and wherever a note is held a beat or more, a note that is not in the chord is moved onto one. */
enum { F_BOUNCE = 1, F_LYRIC = 2, F_HERO = 4, F_DARK = 8, F_DRIVE = 16 };
typedef struct {
    uint8_t fam;
    const char *s;
} motif_t;
static const motif_t MOTIFS[] = {
    {F_BOUNCE, "c0 . c1 . c2 . u . c2 - . c1 c0 - . ."},
    {F_BOUNCE, "c2 . . c1 . . c0 . c1 . c2 - - - . ."},
    {F_BOUNCE, "c0 c0 . c0 . c1 . c2 - - . . d . c1 ."},
    {F_BOUNCE, "c2 . c3 . c2 . c1 . c0 - - - c1 . . ."},
    {F_BOUNCE, ". . c0 . c1 . c2 . c3 - - - c2 . c1 ."},
    {F_BOUNCE | F_HERO, "c1 - c2 - c3 - - - c2 . c1 . c2 - - -"},
    {F_LYRIC, "c2 - - - - - u . c2 - - - c1 - - ."},
    {F_LYRIC, "c0 - - - c1 - c2 - c3 - - - - - d -"},
    {F_LYRIC, "c2 - - d c1 - - - c0 - - - u - c1 -"},
    {F_LYRIC, "c1 - - - - - - - c2 - - u c3 - - -"},
    {F_LYRIC | F_DARK, ". . . . c0 - u - c1 - - - - - - -"},
    {F_LYRIC, "c3 - - - c2 - - - c1 - u - c2 - - -"},
    {F_HERO, "c0 - - c0 c1 - - - c2 - - - c3 - - -"},
    {F_HERO, "c3 - - - u - d - c2 - - - - - . ."},
    {F_HERO, "c0 - - c1 c2 - - c3 c2 - - - c1 . c2 ."},
    {F_HERO | F_LYRIC, "c2 - - - - - c1 c2 c3 - - - c2 - - -"},
    {F_DARK, "c0 - - - - - u . c0 - - - . . . ."},
    {F_DARK, "c2 - - - - - d . c1 - - - . . . ."},
    {F_DARK | F_DRIVE, "c0 . c1 . c2 . c1 . c0 . u . c0 - - -"},
    {F_DARK, ". . c-1 . c0 - - - - - d . c-1 - - -"},
    {F_DRIVE, "c0 . c0 . c2 . c0 . c1 . c0 . c2 . c1 ."},
    {F_DRIVE, "c2 - - c1 - - c0 - c1 - u - c2 - - -"},
    {F_DRIVE | F_HERO, "c0 - c1 - c2 - c3 - c2 - c1 - c0 - - -"},
    {F_DRIVE | F_BOUNCE, "c0 - c0 c1 - c1 c2 - c3 - - - c2 - . ."},
};
/* the phrase endings: the last bar of a four-bar phrase, falling onto (or rising to) its target */
static const char *const ENDS[] = {
    "t2 - t1 - t0 - - - - - - - . . . .",
    "t4 . t2 . t1 . t0 - - - - - - - . .",
    "t0 - - - - - - - - - - - . . . .",
    "t-1 - t0 - - - - - - - - - . . . .",
    "t2 . t2 . t1 . t1 . t0 - - - - - . .",
    "t2 - - t1 t0 - - - - - - - . . . .",
};
#define NENDS NA(ENDS)

/* the bass (the triangle): a bar each. R the root, O its octave, F the fifth, T the third, A a step onto the next
 * bar's root; "-" held, "." cut */
static const char *const BASS[] = {
    "R - O - R - O - R - O - R - O -",          /* 0 octaves */
    "R . O . R . O . R . O . R . O .",          /* 1 octaves, bounced */
    "R - R - R - R - R - R - R - A -",          /* 2 driving roots */
    "R - - - T - - - F - - - A - - -",          /* 3 walking */
    "R - - - O - - - F - - - A - - -",          /* 4 walking, wider */
    "R - - - - - - - F - - - - - - -",          /* 5 halves */
    "R - R R - R R - R - R R - R A -",          /* 6 gallop */
    "R - - R - - R - R - - R - - A -",          /* 7 three-three-two */
};
/* the drums (the noise): K kick, S snare, h hat, o open hat, C crash, g ghost, m metal hat */
static const char *const DRUMS[] = {
    "K . h . S . h . K . K . S . h .",          /* 0 the beat */
    "K . h . S . h K . K h . S . h .",          /* 1 the beat, pushed */
    "K h h h S h h h K h K h S h h h",          /* 2 driving sixteenths */
    "K . h . h . h . S . h . h . h .",          /* 3 half time */
    "K . h . S . h . K . h h S . h .",          /* 4 shuffle */
    "K . . . h . . . S . . . h . . .",          /* 5 sparse */
    "K . o . S . o . K . o . S . o .",          /* 6 open offbeats */
    "K . g g S . g . K . g g S g S .",          /* 7 march */
    "K . m . S . m m K . m . S . m .",          /* 8 metal */
};
static const char *const FILLS[] = {"S S S S", "S . S S", "K S S S", "S g S S"};
/* the second pulse's chord rhythms (x a hit), and its broken chords (chord tones 0..3, 3 the root's octave) */
static const char *const ARPR[] = {
    "x - x - x - x - x - x - x - x -",          /* 0 eighths */
    "- - x - - - x - - - x - - - x -",          /* 1 offbeats */
    "x - - x - - x - x - - x - - x -",          /* 2 three-three-two */
    "x - - - x - - - x - - - x - - -",          /* 3 quarters */
    "x - - - - - - - x - - - - - - -",          /* 4 halves */
    "x - x x - x x - x - x x - x x -",          /* 5 gallop */
};
static const char *const BROKEN[] = {
    "0 - 2 - 1 - 2 - 0 - 2 - 1 - 2 -",          /* 0 Alberti */
    "0 1 2 3 2 1 0 1 2 3 2 1 0 1 2 1",          /* 1 sixteenths up and down */
    "0 - 1 - 2 - 3 - 2 - 1 - 2 - 1 -",          /* 2 eighths up and down */
    "0 2 3 2 0 2 3 2 0 2 3 2 0 2 3 2",          /* 3 a rolling figure */
};

/* -------------------------------------------------------- instruments --- */
/* each a volume envelope (a step a frame, the last held; after a release one step down each rel frames), a duty
 * envelope (0 12.5 %, 1 25 %, 2 50 %; the noise: 1 its short mode), a pitch envelope (semitones; the noise: period
 * steps) and a built-in vibrato */
typedef struct {
    const char *name;
    const int8_t *vol;
    uint8_t nvol, rel;
    const int8_t *duty;
    uint8_t nduty;
    const int8_t *pit;
    uint8_t npit;
} ins_t;
static const int8_t V_SUS[] = {11, 13, 12, 12, 11, 11, 10};
static const int8_t V_SOFT[] = {6, 8, 9, 10, 10, 10, 9, 9};
static const int8_t V_PLK[] = {13, 12, 10, 9, 8, 7, 7, 6, 6, 5, 5, 4};
static const int8_t V_PLK12[] = {14, 12, 10, 8, 7, 6, 6, 5, 5, 4};
static const int8_t V_ECHO[] = {5, 6, 6, 5, 5, 5, 4};
static const int8_t V_ARP[] = {10, 10, 9, 8, 8, 7, 7, 6, 6, 6, 5, 5, 5, 4, 4, 4, 3};
static const int8_t V_HARM[] = {4, 6, 7, 7, 7, 6};
static const int8_t V_BRK[] = {9, 8, 7, 6, 5, 5, 4, 4, 3};
static const int8_t V_ON[] = {15};
static const int8_t V_KICK[] = {15, 13, 10, 7, 5, 3, 2, 1, 0};
static const int8_t V_SNARE[] = {13, 12, 11, 9, 8, 7, 6, 5, 4, 3, 2, 2, 1, 1, 0};
static const int8_t V_HAT[] = {7, 4, 2, 1, 0};
static const int8_t V_OHAT[] = {7, 6, 6, 5, 5, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 1, 0};
static const int8_t V_CRASH[] = {11, 11, 10, 10, 9, 9, 9, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6, 6, 5, 5, 5, 5, 5, 4, 4, 4, 4,
                                 4, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 0};
static const int8_t V_GHOST[] = {6, 5, 4, 3, 2, 1, 0};
static const int8_t V_METAL[] = {6, 4, 3, 2, 1, 0};
static const int8_t D0[] = {0}, D1[] = {1}, D2[] = {2};
static const int8_t D_SWEEP[] = {2, 2, 2, 1, 1, 1, 1, 0};
static const int8_t P0[] = {0};
static const int8_t P_PUNCH[] = {19, 10, 4, 1, 0};
static const int8_t P_KICK[] = {0, 2, 3, 4, 5};
static const int8_t P_SNARE[] = {0, 0, 1};
#define E(a) a, (uint8_t)NA(a)
enum {
    I_LEAD12, I_LEAD25, I_LEAD50, I_PLUCK25, I_SWEEP, I_PLUCK12, I_ECHO12, I_ECHO25, I_ECHO50, I_ARP25, I_ARP50, I_ARP12,
    I_HARM50, I_HARM25, I_BRK25, I_BRK12, I_TRI, I_PUNCH, I_KICK, I_SNARE, I_HAT, I_OHAT, I_CRASH, I_GHOST, I_METAL, I_N
};
static const ins_t INS[I_N] = {
    {"LEAD 12", E(V_SUS), 2, E(D0), E(P0)},       {"LEAD 25", E(V_SUS), 2, E(D1), E(P0)},
    {"LEAD 50", E(V_SOFT), 3, E(D2), E(P0)},      {"PLUCK 25", E(V_PLK), 1, E(D1), E(P0)},
    {"SWEEP", E(V_SUS), 2, E(D_SWEEP), E(P0)},    {"PLUCK 12", E(V_PLK12), 1, E(D0), E(P0)},
    {"ECHO 12", E(V_ECHO), 2, E(D0), E(P0)},      {"ECHO 25", E(V_ECHO), 2, E(D1), E(P0)},
    {"ECHO 50", E(V_ECHO), 2, E(D2), E(P0)},      {"ARP 25", E(V_ARP), 1, E(D1), E(P0)},
    {"ARP 50", E(V_ARP), 1, E(D2), E(P0)},        {"ARP 12", E(V_ARP), 1, E(D0), E(P0)},
    {"HARM 50", E(V_HARM), 3, E(D2), E(P0)},      {"HARM 25", E(V_HARM), 3, E(D1), E(P0)},
    {"BROKEN 25", E(V_BRK), 1, E(D1), E(P0)},     {"BROKEN 12", E(V_BRK), 1, E(D0), E(P0)},
    {"TRI BASS", E(V_ON), 0, E(D0), E(P0)},       {"TRI PUNCH", E(V_ON), 0, E(D0), E(P_PUNCH)},
    {"KICK", E(V_KICK), 0, E(D0), E(P_KICK)},     {"SNARE", E(V_SNARE), 0, E(D0), E(P_SNARE)},
    {"HAT", E(V_HAT), 0, E(D0), E(P0)},           {"OPEN HAT", E(V_OHAT), 0, E(D0), E(P0)},
    {"CRASH", E(V_CRASH), 0, E(D0), E(P0)},       {"GHOST", E(V_GHOST), 0, E(D0), E(P_SNARE)},
    {"METAL", E(V_METAL), 0, E(D1), E(P0)},
};
const char *en_ins_name(int i) { return (unsigned)i < I_N ? INS[i].name : ""; }

/* ------------------------------------------------------------- styles --- */
/* the knobs' recipe (Tempo Energy Echo Mood, PU1 PU2 TRI NOISE, Swing Tone) and the style's own flavour: the
 * lead's instruments (verse, chorus: its duty colours), the second pulse's, the lead's register, the motif
 * families, what the second pulse may do in each section (bits: R2_*), bass and drum patterns per section, the
 * chord rhythms and broken chords allowed, intros and forms, the swing's unit, the echo's delay, the vibrato on
 * long notes, slides, the arpeggio's speed, the bass's punch on kicks, metal hats, and the title words */
enum { R2_ECHO = 1, R2_ARP = 2, R2_BROKEN = 4, R2_HARM = 8, R2_PAD = 16 };
enum { IN_GROOVE = 1, IN_ARP = 2, IN_FANFARE = 4, IN_DRUMS = 8 };
enum { FM_FULL = 1, FM_DOUBLE = 2, FM_SHORT = 4 };
static const char *const FORMS[3] = {"ABABCB", "AABCB", "ABCB"};
typedef struct {
    const char *name;
    int16_t v[P_NSTYLE];
    uint8_t leadA, leadB, echo, arp, harm, brk;
    uint8_t reg, motifs;
    uint8_t p2[3];
    uint8_t bass[3];
    uint16_t drums[3];
    uint8_t arpr, brkr, intro, form;
    uint8_t swing8, echo_d, vib, slides, arp_sp, punch;
    const char *adj, *noun;                      /* title words, "|" between */
} style_t;
#define B(x) (1u << (x))
static const style_t STYLES[EN_NSTYLES] = {
    {"Extra Life", {150, 75, 30, 12, 75, 70, 75, 70, 0, 60}, I_LEAD25, I_SWEEP, I_ECHO25, I_ARP50, I_HARM25, I_BRK25,
     74, F_BOUNCE | F_HERO, {R2_ARP | R2_ECHO, R2_HARM | R2_ARP, R2_BROKEN}, {B(1) | B(0), B(0) | B(2), B(4)},
     {B(0), B(1) | B(0), B(3)}, B(1) | B(0) | B(3), B(0) | B(2), IN_GROOVE | IN_FANFARE, FM_FULL,
     0, 3, 0x52, 1, 1, 0, "GREEN|SUNNY|WINDY|GRASSY|HAPPY|BRIGHT|ROLLING|MUSHROOM",
     "HILLS|FIELDS|PLAINS|MEADOW|ROAD|VALLEY|VILLAGE|BRIDGE|COUNTRY|HIGHWAY"},
    {"Lantern Oil", {96, 45, 70, 70, 75, 75, 80, 60, 0, 40}, I_LEAD12, I_LEAD12, I_ECHO12, I_ARP12, I_HARM50, I_BRK12,
     70, F_DARK | F_LYRIC, {R2_ECHO, R2_ECHO | R2_PAD, R2_BROKEN | R2_PAD}, {B(5) | B(3), B(3), B(5)},
     {B(5), B(3) | B(5), B(5)}, B(4) | B(3), B(0) | B(3), IN_ARP, FM_DOUBLE,
     0, 3, 0x63, 1, 2, 0, "CRYSTAL|DEEP|DRIPPING|HIDDEN|DARK|FORGOTTEN|ECHO|SILENT",
     "CAVERN|MINES|GROTTO|TUNNELS|DEPTHS|HOLLOW|LAKE|SHAFT|PASSAGE"},
    {"Press Start", {124, 70, 25, 22, 75, 70, 75, 70, 0, 60}, I_LEAD50, I_LEAD25, I_ECHO50, I_ARP25, I_HARM50, I_BRK25,
     72, F_HERO | F_LYRIC, {R2_ARP | R2_BROKEN, R2_HARM, R2_PAD | R2_BROKEN}, {B(3) | B(2), B(2) | B(0), B(5)},
     {B(0) | B(7), B(7) | B(0), B(3)}, B(3) | B(0), B(0) | B(2), IN_FANFARE, FM_FULL,
     0, 3, 0x52, 1, 1, 0, "LEGEND OF|RETURN OF|QUEST FOR|TALES OF|SONG OF|FATE OF|HEART OF|CROWN OF",
     "THE HERO|THE KINGDOM|THE STARS|THE SWORD|THE ISLES|THE DRAGON|THE SKY|THE CROWN|THE NORTH"},
    {"Second Form", {168, 90, 10, 92, 75, 70, 80, 75, 0, 65}, I_LEAD12, I_SWEEP, I_ECHO12, I_ARP12, I_HARM25, I_BRK12,
     72, F_DRIVE | F_HERO, {R2_ARP | R2_HARM, R2_HARM | R2_ARP, R2_ARP}, {B(6) | B(0), B(6) | B(0), B(0)},
     {B(2), B(2), B(0) | B(2)}, B(5) | B(0), B(1), IN_DRUMS, FM_FULL,
     0, 3, 0, 2, 1, 1, "FINAL|DARK|IRON|BLOOD|SHADOW|DEMON|STORM|ROGUE",
     "LORD|KNIGHT|ENGINE|GOLEM|FORTRESS|DRAGON|EMPEROR|GUARDIAN|MACHINE"},
    {"Innkeeper's Cat", {88, 50, 60, 58, 70, 75, 75, 60, 50, 35}, I_LEAD50, I_LEAD50, I_ECHO50, I_ARP50, I_HARM50, I_BRK25,
     72, F_LYRIC, {R2_ECHO | R2_BROKEN, R2_PAD | R2_HARM, R2_BROKEN}, {B(3) | B(5), B(3) | B(4), B(5)},
     {B(3) | B(4), B(4), B(3)}, B(4), B(0) | B(2), IN_ARP, FM_DOUBLE,
     1, 2, 0x52, 2, 2, 0, "COBBLED|QUIET|GREY|SLEEPY|CROOKED|LITTLE|OLD|LONELY",
     "TOWN|STREETS|WELL|STATION|INN|SQUARE|CHAPEL|LANE|MARKET"},
    {"Air Bubble Timer", {92, 45, 55, 35, 70, 70, 75, 55, 20, 30}, I_LEAD50, I_LEAD50, I_ECHO50, I_ARP50, I_HARM50, I_BRK25,
     74, F_LYRIC, {R2_BROKEN, R2_BROKEN | R2_HARM, R2_PAD}, {B(5) | B(3), B(5) | B(4), B(5)},
     {B(3), B(3) | B(5), B(5)}, B(4), B(1) | B(3), IN_ARP, FM_DOUBLE,
     0, 3, 0x73, 3, 2, 0, "SUNKEN|CORAL|AZURE|SILENT|DEEP|SAPPHIRE|DROWSY|BRINY",
     "REEF|LAGOON|TRENCH|SHIPWRECK|CATHEDRAL|PALACE|CURRENT|KELP FOREST|GROTTO"},
    {"Cobalt Airlock", {132, 70, 50, 62, 70, 70, 75, 70, 0, 55}, I_LEAD12, I_SWEEP, I_ECHO12, I_ARP12, I_HARM25, I_BRK12,
     74, F_DRIVE | F_DARK, {R2_BROKEN | R2_ECHO, R2_ARP | R2_BROKEN, R2_BROKEN}, {B(0) | B(7), B(7) | B(0), B(5)},
     {B(8), B(8) | B(6), B(3)}, B(0) | B(2), B(1) | B(3), IN_ARP | IN_GROOVE, FM_FULL,
     0, 3, 0x42, 1, 1, 0, "ORBITAL|CHROME|ZERO-G|STELLAR|COBALT|VECTOR|NOVA|ION",
     "DOCK|STATION|CORE|REACTOR|HANGAR|COLONY|BEACON|ARRAY|LAB"},
    {"Haggle Mode", {116, 60, 20, 22, 75, 70, 75, 65, 55, 55}, I_PLUCK25, I_PLUCK25, I_ECHO25, I_ARP50, I_HARM25, I_BRK25,
     72, F_BOUNCE, {R2_ARP, R2_ARP | R2_HARM, R2_BROKEN}, {B(3) | B(4), B(4) | B(3), B(3)},
     {B(4), B(4), B(3)}, B(1), B(0), IN_GROOVE, FM_SHORT,
     1, 2, 0, 0, 1, 0, "ITEM|CORNER|POTION|LUCKY|COZY|BRASS|MAGIC|TINY",
     "SHOP|STORE|MARKET|BAZAAR|EMPORIUM|STALL|TRADER|COUNTER"},
    {"Photo Finish", {176, 90, 15, 10, 75, 70, 80, 75, 0, 65}, I_LEAD25, I_SWEEP, I_ECHO25, I_ARP25, I_HARM25, I_BRK25,
     74, F_HERO | F_BOUNCE, {R2_HARM | R2_ARP, R2_HARM, R2_ARP}, {B(0), B(0) | B(2), B(2)},
     {B(6) | B(0), B(2) | B(6), B(0)}, B(0) | B(2), B(2), IN_FANFARE, FM_FULL,
     0, 3, 0, 1, 1, 1, "VICTORY|CHAMPION|CHECKERED|FINAL|HIGH|STAR|TURBO|GRAND",
     "LAP|CIRCUIT|PARADE|RACE|RUN|FINISH|CUP|PRIX|ROAD"},
    {"Candelabra", {104, 55, 60, 96, 70, 70, 75, 60, 50, 45}, I_LEAD12, I_LEAD12, I_ECHO12, I_ARP12, I_HARM50, I_BRK12,
     72, F_DARK, {R2_ARP | R2_ECHO, R2_ECHO | R2_ARP, R2_PAD}, {B(3) | B(4), B(3), B(5)},
     {B(4) | B(5), B(4), B(5)}, B(2) | B(4), B(0), IN_ARP, FM_DOUBLE,
     1, 2, 0x64, 1, 2, 0, "HAUNTED|CREEPING|HOLLOW|PALE|WHISPERING|CURSED|CREAKY|VELVET",
     "MANOR|ATTIC|HALLWAY|LIBRARY|CELLAR|BALLROOM|GALLERY|CHAPEL|STAIRS"},
    {"Staff Roll", {108, 60, 40, 30, 70, 70, 75, 60, 0, 50}, I_LEAD50, I_LEAD25, I_ECHO50, I_ARP50, I_HARM50, I_BRK25,
     72, F_LYRIC | F_HERO, {R2_BROKEN | R2_ECHO, R2_HARM | R2_PAD, R2_PAD}, {B(3) | B(2), B(2) | B(4), B(5)},
     {B(0), B(0) | B(1), B(3)}, B(3), B(0) | B(2), IN_GROOVE | IN_ARP, FM_FULL,
     0, 3, 0x52, 2, 2, 0, "FAREWELL|THANK YOU|LAST|DISTANT|HOMEWARD|AMBER|LONG|STARLIT",
     "FRIENDS|JOURNEY|ROAD|MEMORIES|HORIZON|CAMPFIRE|SKY|SHORES|HOME"},
    {"Save And Quit", {76, 40, 45, 40, 70, 70, 75, 50, 0, 40}, I_LEAD50, I_LEAD50, I_ECHO50, I_ARP50, I_HARM50, I_BRK25,
     72, F_LYRIC, {R2_BROKEN, R2_PAD | R2_BROKEN, R2_PAD}, {B(5), B(5) | B(3), B(5)},
     {B(3), B(3), B(5)}, B(4), B(0) | B(2), IN_ARP, FM_SHORT,
     0, 3, 0x63, 2, 2, 0, "PEACEFUL|THE LAST|QUIET|NEW|GENTLE|SILVER|FADING|ENDLESS",
     "EPILOGUE|MORNING|HOME|LIGHT|SKIES|DREAM|GOODBYE|SPRING|ORCHARD"},
    {"Propeller Keep", {144, 80, 30, 42, 75, 70, 80, 70, 0, 60}, I_SWEEP, I_LEAD25, I_ECHO25, I_ARP25, I_HARM25, I_BRK25,
     74, F_HERO | F_DRIVE, {R2_ARP | R2_ECHO, R2_HARM | R2_ARP, R2_BROKEN}, {B(2) | B(7), B(0) | B(7), B(4)},
     {B(0) | B(1), B(1) | B(2), B(3)}, B(0) | B(2), B(1), IN_FANFARE | IN_GROOVE, FM_FULL,
     0, 3, 0x52, 1, 1, 1, "SKY|CLOUD|THUNDER|FLOATING|WIND|STORM|HIGH|AIR",
     "FORTRESS|CITADEL|AIRSHIP|TOWER|BASTION|KEEP|SPIRE|ARMADA|ISLAND"},
};
const char *en_style_name(int i) { return STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES].name; }
void en_style_recipe(int i, int16_t *par)
{
    int k;
    const style_t *s = &STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES];
    for (k = 0; k < P_NSTYLE; k++)
        par[k] = s->v[k];
}

/* ---------------------------------------------------- the song maker --- */
uint32_t en_song_rand(uint32_t base, uint32_t no)
{
    uint32_t h = base ^ (no * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h ? h : 1u;
}

static uint32_t grng = 1u;
static uint32_t gx(void)
{
    uint32_t x = grng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    grng = x;
    return x;
}
static int gi(int n) { return n > 0 ? (int)((gx() >> 8) % (uint32_t)n) : 0; }
static int gbit(uint32_t mask)                   /* one of mask's bits, at random */
{
    int i, n = 0, k;
    for (i = 0; i < 32; i++)
        n += (int)(mask >> i & 1u);
    if (!n)
        return 0;
    k = gi(n);
    for (i = 0; i < 32; i++)
        if (mask >> i & 1u) {
            if (!k)
                return i;
            k--;
        }
    return 0;
}

/* the maker's view of the song: the key and mode, the sections' chords (absolute: root pc | q << 4) */
static struct {
    const style_t *st;
    int key, mode;
    uint8_t ch[3][4];                            /* verse, chorus, bridge */
    int anchor[3];
    int lead_last;
    int pad_last;
} G;

static int chord_has(uint8_t c, int p)
{
    int r = c & 15, q = c >> 4, i, pc = ((p % 12) + 12) % 12;
    for (i = 0; i < 3; i++)
        if ((r + Q_IV[q][i]) % 12 == pc)
            return 1;
    return 0;
}
static int chord_tone_at(uint8_t c, int anchor, int n)   /* the nth chord tone counted from the first >= anchor */
{
    int p = anchor, k = 0;
    while (!chord_has(c, p))
        p++;
    if (n >= 0) {
        while (k < n) {
            p++;
            if (chord_has(c, p))
                k++;
        }
    } else {
        while (k > n) {
            p--;
            if (chord_has(c, p))
                k--;
        }
    }
    return p;
}
/* the scale over a chord: the mode's, with the chord's own notes in place of any a semitone off them */
static void chord_scale(uint8_t c, int *pcs)
{
    int i, j, r = c & 15, q = c >> 4;
    for (i = 0; i < 7; i++)
        pcs[i] = (G.key + MODE_PCS[G.mode][i]) % 12;
    for (j = 0; j < 3; j++) {
        int t = (r + Q_IV[q][j]) % 12, have = 0;
        for (i = 0; i < 7; i++)
            have |= pcs[i] == t;
        if (have)
            continue;
        for (i = 0; i < 7; i++)
            if ((pcs[i] + 1) % 12 == t || (pcs[i] + 11) % 12 == t) {
                pcs[i] = t;
                break;
            }
    }
}
static int in_scale(const int *pcs, int p)
{
    int i, pc = ((p % 12) + 12) % 12;
    for (i = 0; i < 7; i++)
        if (pcs[i] == pc)
            return 1;
    return 0;
}
static int scale_step(uint8_t c, int p, int k)  /* k scale steps from p (p snapped down onto the scale first) */
{
    int pcs[7];
    chord_scale(c, pcs);
    while (!in_scale(pcs, p))
        p--;
    while (k > 0) {
        p++;
        if (in_scale(pcs, p))
            k--;
    }
    while (k < 0) {
        p--;
        if (in_scale(pcs, p))
            k++;
    }
    return p;
}
static int snap_chord(uint8_t c, int p, int prev)   /* the nearest chord tone (a tie: the one nearer prev) */
{
    int d;
    for (d = 1; d < 7; d++) {
        int up = chord_has(c, p + d), dn = chord_has(c, p - d);
        if (up && dn)
            return (prev > p) ? p + d : p - d;
        if (up)
            return p + d;
        if (dn)
            return p - d;
    }
    return p;
}

/* a motif's tokens */
enum { TK_REST, TK_HOLD, TK_CT, TK_SC, TK_TG };
typedef struct {
    int8_t k, v;
} tok_t;
static void parse16(const char *s, tok_t *t)
{
    int i;
    for (i = 0; i < 16; i++) {
        int neg = 0, v = 0, dig = 0;
        while (*s == ' ')
            s++;
        t[i].k = TK_REST;
        t[i].v = 0;
        if (!*s)
            continue;
        if (*s == '.' || *s == '-') {
            t[i].k = *s == '.' ? TK_REST : TK_HOLD;
            s++;
            continue;
        }
        {
            char c = *s++;
            if (*s == '-')
                neg = 1, s++;
            while (*s >= '0' && *s <= '9')
                v = v * 10 + (*s++ - '0'), dig = 1;
            if (neg)
                v = -v;
            if (c == 'c')
                t[i].k = TK_CT, t[i].v = (int8_t)v;
            else if (c == 't')
                t[i].k = TK_TG, t[i].v = (int8_t)v;
            else if (c == 'u')
                t[i].k = TK_SC, t[i].v = (int8_t)(dig ? v : 1);
            else if (c == 'd')
                t[i].k = TK_SC, t[i].v = (int8_t)(dig ? -v : -1);
            else if (c == 'r')
                t[i].k = TK_SC, t[i].v = 0;
        }
        while (*s && *s != ' ')
            s++;
    }
}

/* the lead's bar: tokens against a chord into notes (n[s]: a MIDI note, 0 nothing new, NOTE_REL a release) */
typedef struct {
    uint8_t n[16];
    uint8_t len[16];
} bar_t;
static void realize(const tok_t *t, uint8_t c, int anchor, int target, bar_t *b, int lo, int hi)
{
    int s, prev = G.lead_last, sounding = 0;
    for (s = 0; s < 16; s++) {
        int p, len, k;
        b->n[s] = 0;
        b->len[s] = 0;
        if (t[s].k == TK_HOLD)
            continue;
        if (t[s].k == TK_REST) {
            if (sounding)
                b->n[s] = NOTE_REL;
            sounding = 0;
            continue;
        }
        for (len = 1, k = s + 1; k < 16 && t[k].k == TK_HOLD; k++)
            len++;
        if (t[s].k == TK_CT)
            p = chord_tone_at(c, anchor, t[s].v);
        else if (t[s].k == TK_TG)
            p = scale_step(c, target, t[s].v);
        else
            p = scale_step(c, prev, t[s].v);
        if (p > hi || p < lo) {                  /* out of the register: the contour bent inward, not an octave's leap */
            if (t[s].k == TK_CT) {
                while (p > hi)
                    p = chord_tone_at(c, p - 1, -1);
                while (p < lo)
                    p = chord_tone_at(c, p + 1, 0);
            } else if (t[s].k == TK_SC) {
                p = scale_step(c, prev, -t[s].v);
            }
            while (p > hi)
                p -= 12;
            while (p < lo)
                p += 12;
        }
        if ((s == 0 || s == 8 || len >= 4 || (len >= 3 && (s & 3) == 0)) && !chord_has(c, p))
            p = snap_chord(c, p, prev);
        b->n[s] = (uint8_t)p;
        b->len[s] = (uint8_t)len;
        prev = p;
        sounding = 1;
    }
    G.lead_last = prev;
}
static void vary(tok_t *t, int how)             /* a motif's variation: 1 ends higher, 2 a chord tone higher */
{
    int s, last = -1;
    if (how == 2) {
        for (s = 0; s < 16; s++)
            if (t[s].k == TK_CT)
                t[s].v++;
        return;
    }
    for (s = 0; s < 16; s++)
        if (t[s].k == TK_CT || t[s].k == TK_SC)
            last = s;
    if (last < 0 || how != 1)
        return;
    if (t[last].k == TK_CT)
        t[last].v++;
    else
        t[last].v = (int8_t)-t[last].v;
}
static int motif_pick(int fam, int not1, int not2)
{
    int tries, m = 0;
    for (tries = 0; tries < 40; tries++) {
        m = gi(NA(MOTIFS));
        if ((MOTIFS[m].fam & fam) && m != not1 && m != not2)
            return m;
    }
    return m;
}

static en_song_t *S;
static void cell(int pat, int row, int ch, int note, int ins, int fx, int fxp, int lay)
{
    en_cell_t *c = &S->pat[pat][row][ch];
    c->note = (uint8_t)note;
    c->ins = (uint8_t)ins;
    c->fx = (uint8_t)fx;
    c->fxp = (uint8_t)fxp;
    c->lay = (uint8_t)lay;
}
static void clear_pat(int p, int rows)
{
    int r, c;
    for (r = 0; r < EN_ROWS; r++)
        for (c = 0; c < EN_NCH; c++)
            cell(p, r, c, NOTE_NONE, 0xFF, 0, 0, 0);
    S->plen[p] = (uint8_t)rows;
    S->pchord[p][0] = S->pchord[p][1] = 0xFFu;
}

/* the lead's bar into a pattern (at row r0), with its effects: vibrato on long notes, now and then a slide up into
 * a note (the note a few semitones under it, slid up: "Q") */
static void put_lead(int pat, int r0, const bar_t *b, int ins)
{
    int s;
    for (s = 0; s < 16; s++) {
        int n = b->n[s], fx = 0, fxp = 0;
        if (!n)
            continue;
        if (n == NOTE_REL) {
            cell(pat, r0 + s, CH_PU1, NOTE_REL, 0xFF, 0, 0, 0);
            continue;
        }
        if (b->len[s] >= 6 && G.st->vib) {
            fx = '4';
            fxp = G.st->vib;
        } else if (G.st->slides && b->len[s] >= 3 && gi(12) < G.st->slides) {
            int up = 1 + gi(2) * 1;
            fx = 'Q';
            fxp = 0x60 | up;
            n -= up;
        }
        cell(pat, r0 + s, CH_PU1, n, ins, fx, fxp, 0);
    }
}

/* the second pulse, a bar of it */
static int p2_root(uint8_t c)                    /* the chord's root in the second pulse's register (57..68) */
{
    int p = 57;
    while ((p % 12) != (c & 15))
        p++;
    return p;
}
static void put_arp(int pat, int r0, uint8_t c, int rh)
{
    const char *s = ARPR[rh];
    int i, q = c >> 4, x = Q_IV[q][1], y = Q_IV[q][2];
    for (i = 0; i < 16; i++) {
        while (*s == ' ')
            s++;
        if (*s == 'x')
            cell(pat, r0 + i, CH_PU2, p2_root(c), G.st->arp, '0', x << 4 | y, (i & 7) == 0 ? 2 : (i & 1) ? 4 : 3);
        if (*s)
            s++;
    }
}
static void put_broken(int pat, int r0, uint8_t c, int pt)
{
    const char *s = BROKEN[pt];
    int i, base = p2_root(c), tones[4];
    tones[0] = base;
    tones[1] = chord_tone_at(c, base + 1, 0);
    tones[2] = chord_tone_at(c, tones[1] + 1, 0);
    tones[3] = base + 12;
    for (i = 0; i < 16; i++) {
        while (*s == ' ')
            s++;
        if (*s >= '0' && *s <= '3')
            cell(pat, r0 + i, CH_PU2, tones[*s - '0'], G.st->brk, 0, 0, (i & 3) == 0 ? 2 : 3);
        if (*s)
            s++;
    }
}
static void put_pad(int pat, int r0, uint8_t c)  /* a long note: the chord's third or fifth, nearest the last */
{
    int best = 64, bd = 99, p;
    for (p = 57; p <= 72; p++) {
        int d = p - G.pad_last;
        if (!chord_has(c, p) || p % 12 == (c & 15))
            continue;                            /* the third or the fifth: the bass has the root */
        d = d < 0 ? -d : d;
        if (d < bd)
            bd = d, best = p;
    }
    G.pad_last = best;
    cell(pat, r0, CH_PU2, best, G.st->harm, '4', 0x42, 2);
}
/* harmony: a third under the lead (a chord tone under it on the strong beats), its rhythm */
static void put_harm(int pat, int r0, const bar_t *b, uint8_t c)
{
    int s;
    for (s = 0; s < 16; s++) {
        int n = b->n[s], h;
        if (!n)
            continue;
        if (n == NOTE_REL) {
            cell(pat, r0 + s, CH_PU2, NOTE_REL, 0xFF, 0, 0, 2);
            continue;
        }
        if (chord_has(c, n))                     /* under a chord tone a chord tone; under a passing note, its third */
            h = chord_has(c, n - 3) ? n - 3 : chord_tone_at(c, n - 3, -1);   /* the chord tone a third or more under */
        else
            h = scale_step(c, n, -2);
        while (h < 52)
            h += 12;
        if (h >= n)
            continue;
        cell(pat, r0 + s, CH_PU2, h, G.st->harm, 0, 0, 2);
    }
}
/* the echo: the lead's cells of this pattern, d rows later, quieter (the Echo knob plays them) */
static void put_echo(int pat, int d)
{
    int r, n = S->plen[pat];
    for (r = n - 1 - d; r >= 0; r--) {
        en_cell_t *l = &S->pat[pat][r][CH_PU1];
        if (!l->note)
            continue;
        cell(pat, r + d, CH_PU2, l->note, l->note == NOTE_REL ? 0xFF : G.st->echo, 0, 0, LAY_ECHO | 1);
    }
}

/* the bass, a bar */
static int bass_root(int pc)                     /* the chord's root in the triangle's register: D2 .. C#3 */
{
    return 38 + ((pc - 2 + 12) % 12);
}
static void put_bass(int pat, int r0, uint8_t c, uint8_t next, int bp, int kicks)
{
    const char *s = BASS[bp];
    int i, root = bass_root(c & 15), nroot = bass_root(next & 15), q = c >> 4;
    for (i = 0; i < 16; i++) {
        int p = -1, lay;
        char k;
        while (*s == ' ')
            s++;
        k = *s;
        if (*s)
            s++;
        switch (k) {
        case 'R': p = root; break;
        case 'O': p = root + 12; break;
        case 'F': p = root + Q_IV[q][2]; break;
        case 'T': p = root + Q_IV[q][1]; break;
        case 'A':
            if ((next & 15) == (c & 15))
                p = root + Q_IV[q][2];
            else {
                p = scale_step(next, nroot, -1);
                if (p == root)
                    p = scale_step(next, nroot, 1);
            }
            break;
        case '.':
            cell(pat, r0 + i, CH_TRI, NOTE_CUT, 0xFF, 0, 0, i == 0 ? 0 : 1);
            break;
        default: break;
        }
        if (p < 0)
            continue;
        lay = i == 0 ? 0 : k == 'O' ? 2 : (i & 1) ? 3 : 1;
        cell(pat, r0 + i, CH_TRI, p, (kicks >> i & 1u) && G.st->punch ? I_PUNCH : I_TRI, 0, 0, lay);
    }
}

/* the drums, a bar (fill: the last four steps a fill); the kicks it put, as bits */
static int put_drums(int pat, int r0, int dp, int fill, int crash)
{
    const char *s = DRUMS[dp];
    char t[16];
    int i, kicks = 0;
    for (i = 0; i < 16; i++) {
        while (*s == ' ')
            s++;
        t[i] = *s ? *s : '.';
        if (*s)
            s++;
    }
    if (fill >= 0) {
        const char *f = FILLS[fill];
        for (i = 12; i < 16; i++) {
            while (*f == ' ')
                f++;
            t[i] = *f ? *f : '.';
            if (*f)
                f++;
        }
    }
    if (crash)
        t[0] = 'C';
    for (i = 0; i < 16; i++) {
        int r = r0 + i, h = (i & 1) ? 3 : 2;
        switch (t[i]) {
        case 'K': cell(pat, r, CH_NOI, 1 + 9, I_KICK, 0, 0, 1); kicks |= 1 << i; break;
        case 'S': cell(pat, r, CH_NOI, 1 + 6, I_SNARE, 0, 0, 1); break;
        case 'h': cell(pat, r, CH_NOI, 1 + 0, I_HAT, 0, 0, h); break;
        case 'o': cell(pat, r, CH_NOI, 1 + 1, I_OHAT, 0, 0, 3); break;
        case 'C': cell(pat, r, CH_NOI, 1 + 3, I_CRASH, 0, 0, 1); kicks |= 1 << i; break;
        case 'g': cell(pat, r, CH_NOI, 1 + 6, I_GHOST, 0, 0, 4); break;
        case 'm': cell(pat, r, CH_NOI, 1 + 3, I_METAL, 0, 0, h); break;
        default: break;
        }
    }
    return kicks;
}

/* a section: verse (0), chorus (1) or bridge (2), into patterns p, p+1 (and p+2); the section after it */
typedef struct {
    int m1, m2;                                  /* motifs */
    int v1, v2;                                  /* their variations */
    int e_half, e_full;                          /* the endings */
    int role, arpr, brkr, bass, drums, lead;
} plan_t;
static void bar_lead(int sec, int bar, const plan_t *pl, uint8_t c, bar_t *b)
{
    tok_t t[16];
    int anchor = G.anchor[sec], lo = anchor - 8, hi = anchor + 13, target = 0, kind;
    /* the bar's part: A A' B end (the second phrase: A A' B' end) */
    static const int8_t PLAN[3][8] = {{0, 1, 2, 3, 0, 1, 5, 4}, {0, 0, 2, 3, 0, 0, 5, 4}, {0, 0, 1, 3, 0, 0, 1, 3}};
    kind = PLAN[sec][bar];
    if (kind == 3 || kind == 4) {               /* an ending: half (onto an open chord tone) or full (home) */
        int tonic = G.key, k, best = 999;
        if (kind == 4 && chord_has(c, tonic)) {
            target = anchor - 6;
            while (target % 12 != tonic)
                target++;
        } else {
            for (k = -1; k < 3; k++) {
                int p = chord_tone_at(c, anchor, k), d = p - (anchor + (kind == 3 ? 3 : 0));
                if (kind == 3 && p % 12 == tonic)
                    continue;                    /* a half close stays open */
                if (kind == 4 && p % 12 != (c & 15))
                    continue;                    /* a full close away from home: the chord's root */
                d = d < 0 ? -d : d;
                if (d < best)
                    best = d, target = p;
            }
            if (best == 999)
                target = chord_tone_at(c, anchor, 0);
        }
        parse16(ENDS[kind == 3 ? pl->e_half : pl->e_full], t);
    } else {
        int m = kind == 2 || kind == 5 ? pl->m2 : pl->m1;
        parse16(MOTIFS[m].s, t);
        if (kind == 1)
            vary(t, pl->v1);
        if (kind == 5)
            vary(t, pl->v2);
    }
    realize(t, c, anchor, target, b, lo, hi);
}

static void make_section(int sec, int p0, const plan_t *pl, uint8_t next_chord)
{
    static const int8_t BARS[2][3][2] = {{{0, 1}, {2, 3}, {6, 7}}, {{0, 1}, {2, 3}, {-1, -1}}};
    int np = sec == 2 ? 2 : 3, k, j;
    for (k = 0; k < np; k++) {
        int pat = p0 + k;
        clear_pat(pat, 32);
        for (j = 0; j < 2; j++) {
            int bar = BARS[sec == 2][k][j], r0 = j * 16, kicks, fill = -1;
            uint8_t c = G.ch[sec][bar & 3], nx;
            bar_t b;
            nx = (bar & 3) == 3 ? ((bar == 7 || sec == 2) ? next_chord : G.ch[sec][0]) : G.ch[sec][(bar + 1) & 3];
            S->pchord[pat][j] = c;
            if (bar == 7 || (sec == 2 && bar == 3))
                fill = gi(NA(FILLS));
            kicks = put_drums(pat, r0, pl->drums, fill, bar == 0 && sec == 1);
            if (bar == 3 && sec != 2 && pl->drums != 5) {    /* the half-way: a short pickup on the snare */
                cell(pat, r0 + 14, CH_NOI, 7, I_SNARE, 0, 0, 1);
                cell(pat, r0 + 15, CH_NOI, 7, I_SNARE, 0, 0, 3);
            }
            put_bass(pat, r0, c, nx, pl->bass, kicks);
            bar_lead(sec, bar, pl, c, &b);
            put_lead(pat, r0, &b, pl->lead);
            switch (pl->role) {
            case R2_ARP: put_arp(pat, r0, c, pl->arpr); break;
            case R2_BROKEN: put_broken(pat, r0, c, pl->brkr); break;
            case R2_HARM: put_harm(pat, r0, &b, c); break;
            case R2_PAD: put_pad(pat, r0, c); break;
            default: break;
            }
        }
        if (pl->role == R2_ECHO)
            put_echo(pat, G.st->echo_d);
    }
}

/* the intro: a bar or two before the verse */
static void make_intro(int pat, int kind, const plan_t *pv)
{
    static const uint8_t TWO[MD_N] = {7, 10, 5, 10, 7};   /* the chord the intro turns to: V, bVII, IV, bVII, V */
    uint8_t home = CH(G.key, (G.mode <= MD_MIXO) ? QM : Qm);
    uint8_t two = CH((G.key + TWO[G.mode]) % 12, QM);
    int j;
    if (kind == IN_FANFARE || kind == IN_DRUMS) {
        clear_pat(pat, 16);
        S->pchord[pat][0] = home;
        if (kind == IN_FANFARE) {                /* hits on the home chord, the lead and a third under it */
            static const int8_t HIT[16] = {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 0};
            int top = chord_tone_at(home, G.anchor[0], 1), i;
            for (i = 0; i < 16; i++) {
                if (HIT[i] > 0) {
                    cell(pat, i, CH_PU1, top, G.st->leadA, 0, 0, 0);
                    cell(pat, i, CH_PU2, chord_tone_at(home, top - 1, -1), G.st->harm, 0, 0, 2);
                    cell(pat, i, CH_TRI, bass_root(G.key), I_TRI, 0, 0, 0);
                    cell(pat, i, CH_NOI, i ? 10 : 4, i ? I_KICK : I_CRASH, 0, 0, 1);
                } else if (HIT[i] < 0) {
                    cell(pat, i, CH_PU1, NOTE_REL, 0xFF, 0, 0, 0);
                    cell(pat, i, CH_PU2, NOTE_REL, 0xFF, 0, 0, 2);
                    cell(pat, i, CH_TRI, NOTE_CUT, 0xFF, 0, 0, 0);
                }
            }
            cell(pat, 12, CH_NOI, 7, I_SNARE, 0, 0, 1);
            cell(pat, 13, CH_NOI, 7, I_SNARE, 0, 0, 3);
            cell(pat, 14, CH_NOI, 7, I_SNARE, 0, 0, 1);
            cell(pat, 15, CH_NOI, 7, I_SNARE, 0, 0, 1);
        } else {                                 /* drums and the bass on the home chord, a fill */
            int kicks = put_drums(pat, 0, pv->drums, 0, 0);
            put_bass(pat, 0, home, G.ch[0][0], pv->bass, kicks);
        }
        return;
    }
    clear_pat(pat, 32);
    for (j = 0; j < 2; j++) {
        uint8_t c = j ? two : home, nx = j ? G.ch[0][0] : two;
        S->pchord[pat][j] = c;
        if (kind == IN_GROOVE) {
            int kicks = put_drums(pat, j * 16, pv->drums, j ? gi(NA(FILLS)) : -1, 0);
            put_bass(pat, j * 16, c, nx, pv->bass, kicks);
            if (pv->role == R2_BROKEN)
                put_broken(pat, j * 16, c, pv->brkr);
            else
                put_arp(pat, j * 16, c, pv->role == R2_ARP ? pv->arpr : 3);
        } else {                                 /* the chords broken, the bass under them, a soft pickup */
            put_broken(pat, j * 16, c, gbit(G.st->brkr));
            put_bass(pat, j * 16, c, nx, 5, 0);
            if (j) {
                cell(pat, 28, CH_NOI, 1, I_HAT, 0, 0, 2);
                cell(pat, 30, CH_NOI, 1, I_HAT, 0, 0, 2);
            }
        }
    }
}

/* the ending jingle: bVI bVII I (the stage cleared) or IV V I, the lead running up the chords onto home */
static void make_jingle(int pat)
{
    int run = G.mode == MD_MAJOR && gi(2) ? 1 : 0, j, i;
    uint8_t c1 = run ? CH((G.key + 5) % 12, QM) : CH((G.key + 8) % 12, QM);
    uint8_t c2 = run ? CH((G.key + 7) % 12, QM) : CH((G.key + 10) % 12, QM);
    uint8_t home = CH(G.key, (G.mode >= MD_MINOR && gi(3) == 0) ? Qm : QM);   /* mostly the major: a Picardy close */
    int anchor = G.anchor[0], top;
    clear_pat(pat, 32);
    S->pchord[pat][0] = c1;
    S->pchord[pat][1] = home;
    for (j = 0; j < 2; j++) {                    /* bar one: two chords, a run up each */
        uint8_t c = j ? c2 : c1;
        static const int8_t RUN[8] = {0, -1, 1, -1, 2, -1, 3, -1};
        for (i = 0; i < 8; i++) {
            int r = j * 8 + i;
            if (RUN[i] >= 0) {
                int p = chord_tone_at(c, anchor - 5 + j * 2, RUN[i]);
                cell(pat, r, CH_PU1, p, G.st->leadB, 0, 0, 0);
                cell(pat, r, CH_PU2, chord_tone_at(c, p - 1, -1), G.st->harm, 0, 0, 2);
            }
        }
        cell(pat, j * 8, CH_TRI, bass_root(c & 15), I_TRI, 0, 0, 0);
        cell(pat, j * 8 + 4, CH_TRI, bass_root(c & 15) + 12, I_TRI, 0, 0, 1);
        cell(pat, j * 8, CH_NOI, 10, I_KICK, 0, 0, 1);
        cell(pat, j * 8 + 4, CH_NOI, 7, I_SNARE, 0, 0, 1);
    }
    cell(pat, 14, CH_NOI, 7, I_SNARE, 0, 0, 1);
    cell(pat, 15, CH_NOI, 7, I_SNARE, 0, 0, 1);
    top = anchor + 2;                            /* home: the tonic on top, a long note */
    while (top % 12 != G.key)
        top++;
    cell(pat, 16, CH_PU1, top, G.st->leadB, '4', 0x52, 0);
    cell(pat, 16, CH_PU2, chord_tone_at(home, top - 1, -1), G.st->harm, 0, 0, 2);
    cell(pat, 16, CH_TRI, bass_root(G.key), I_TRI, 0, 0, 0);
    cell(pat, 16, CH_NOI, 4, I_CRASH, 0, 0, 1);
    cell(pat, 28, CH_PU1, NOTE_REL, 0xFF, 0, 0, 0);
    cell(pat, 28, CH_PU2, NOTE_REL, 0xFF, 0, 0, 2);
    cell(pat, 28, CH_TRI, NOTE_CUT, 0xFF, 0, 0, 0);
}

static int pick_mode(int mood)
{
    int i;
    float w[MD_N], tot = 0.0f, r;
    for (i = 0; i < MD_N; i++) {                 /* the nearest mode mostly, now and then a neighbour */
        float d = (float)(MODE_MOOD[i] - mood) * (1.0f / 14.0f);
        w[i] = 1.0f / (1.0f + d * d * d * d);
        tot += w[i];
    }
    r = (float)(gx() >> 8) * (1.0f / 16777216.0f) * tot;
    for (i = 0; i < MD_N; i++) {
        if (r < w[i])
            return i;
        r -= w[i];
    }
    return MD_MAJOR;
}
static int pick_prog(int role, int not1)
{
    int i, n = 0, k;
    for (i = 0; i < NA(PROGS); i++)
        n += PROGS[i].mode == G.mode && (PROGS[i].roles & role) && i != not1;
    k = gi(n);
    for (i = 0; i < NA(PROGS); i++)
        if (PROGS[i].mode == G.mode && (PROGS[i].roles & role) && i != not1) {
            if (!k)
                return i;
            k--;
        }
    return 0;
}

static void title(const style_t *st, char *b)
{
    const char *a = st->adj, *n = st->noun;
    int na = 1, nn = 1, ia, in, i;
    char *e = b;
    for (i = 0; a[i]; i++)
        na += a[i] == '|';
    for (i = 0; n[i]; i++)
        nn += n[i] == '|';
    ia = gi(na);
    in = gi(nn);
    while (ia) ia -= *a++ == '|';
    while (in) in -= *n++ == '|';
    while (*a && *a != '|')
        *e++ = *a++;
    *e++ = ' ';
    while (*n && *n != '|' && e - b < 23)
        *e++ = *n++;
    *e = 0;
}

void en_make_song(en_song_t *s, int style, uint32_t seed, int mood, int bpm)
{
    plan_t pl[3];
    int sec, pi[3], p, i, o = 0;
    const char *form;
    int intro;
    S = s;
    grng = seed ? seed : 1u;
    gx();
    G.st = &STYLES[(style % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES];
    s->style = (uint8_t)(G.st - STYLES);
    s->bpm = (uint8_t)bpm;
    G.key = gi(12);
    G.mode = pick_mode(mood);
    s->key = (uint8_t)G.key;
    s->mode = (uint8_t)G.mode;
    title(G.st, s->title);
    pi[0] = pick_prog(R_V, -1);
    pi[1] = pick_prog(R_C, pi[0]);
    pi[2] = pick_prog(R_B, -1);
    for (sec = 0; sec < 3; sec++)
        for (i = 0; i < 4; i++) {
            uint8_t c = PROGS[pi[sec]].c[i];
            G.ch[sec][i] = CH(((c & 15) + G.key) % 12, c >> 4);
        }
    {                                            /* the lead's register: the verse near the style's, the chorus up */
        int a = 60 + G.key;
        while (a < G.st->reg - 6)
            a += 12;
        while (a > G.st->reg + 5)
            a -= 12;
        G.anchor[0] = a;
        G.anchor[1] = a + 4;
        G.anchor[2] = a + 2;
    }
    for (sec = 0; sec < 3; sec++) {
        plan_t *q = &pl[sec];
        q->m1 = motif_pick(G.st->motifs, -1, -1);
        q->m2 = motif_pick(G.st->motifs, q->m1, sec ? pl[0].m1 : -1);
        if (sec == 1)
            q->m1 = motif_pick(G.st->motifs, pl[0].m1, pl[0].m2);
        q->v1 = gi(3);
        q->v2 = 1 + gi(2);
        q->e_half = gi(NENDS);
        q->e_full = gi(NENDS);
        if (q->e_full == q->e_half)
            q->e_full = (q->e_full + 1) % NENDS;
        q->role = 1 << gbit(G.st->p2[sec]);
        q->arpr = gbit(G.st->arpr);
        q->brkr = gbit(G.st->brkr);
        q->bass = gbit(G.st->bass[sec]);
        q->drums = gbit(G.st->drums[sec]);
        q->lead = sec == 1 ? G.st->leadB : G.st->leadA;
    }
    G.lead_last = G.anchor[0];
    G.pad_last = 64;
    /* the patterns: 00 the intro, the verse's three, the chorus's three, the bridge's two, the jingle */
    intro = 1 << gbit(G.st->intro);
    make_intro(0, intro, &pl[0]);
    make_section(0, 1, &pl[0], G.ch[1][0]);
    make_section(1, 4, &pl[1], G.ch[0][0]);
    make_section(2, 7, &pl[2], G.ch[1][0]);
    make_jingle(9);
    s->npat = 10;
    /* the order: the intro, then the form (each verse and chorus its first pattern twice: a period) */
    form = FORMS[gbit(G.st->form)];
    s->order[o] = 0;
    s->osect[o++] = 'I';
    s->loop_to = (uint8_t)o;
    for (i = 0; form[i] && o < EN_NORD - 5; i++) {
        int sc = form[i] - 'A', b0 = 1 + sc * 3;
        static const int8_t SEQ[4] = {0, 1, 0, 2};
        for (p = 0; p < (sc == 2 ? 2 : 4); p++) {
            s->order[o] = (uint8_t)(b0 + (sc == 2 ? p : SEQ[p]));
            s->osect[o++] = (uint8_t)form[i];
        }
    }
    s->order[o] = 9;
    s->osect[o++] = 'E';
    s->nord = (uint8_t)o;
}

int en_next_ord(const en_song_t *s, int ord, int loop, int loops, int repeat)
{
    if (ord + 1 < s->nord - 1)
        return ord + 1;
    if (ord + 1 == s->nord - 1)
        return (repeat || loop + 1 < loops) ? s->loop_to : ord + 1;
    return -1;
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_STYLE, C_PLAY, C_SKIP, C_REPEAT, C_SONG_NO, C_SEED_HI, C_SEED_LO, C_NOTE, C_PANIC, C_RETUNE };
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
void en_retune(void) { post(C_RETUNE, 0, 0); }
void en_song_at(uint32_t no, uint32_t seed)
{
    post(C_SEED_HI, 0, (int)(seed >> 16));
    post(C_SEED_LO, 0, (int)(seed & 0xFFFFu));
    post(C_SONG_NO, (int)(no >> 16 & 0xFFu), (int)(no & 0xFFFFu));
}

/* -------------------------------------------------------------- state --- */
static en_song_t songs[2] EN_POOL;
static int song_i;
const en_song_t *volatile en_cur = &songs[0];
volatile uint8_t en_playing, en_phase, en_repeat_on, en_ord, en_row, en_loop, en_loops = 2, en_bpm = 140, en_kick;
volatile uint32_t en_song_no, en_seed_base = 0x43484950u, en_rows_played, en_songs_played;
volatile float en_vu;
volatile uint8_t en_ch_vol[EN_NCH], en_ch_note[EN_NCH];
int8_t en_scope[EN_NCH][EN_SCOPE] EN_POOL;
volatile uint32_t en_scope_w;
volatile float en_ch_hz[EN_NCH];

static int16_t par[P_NPARAMS];
static int style_i;
static int pord, prow;                           /* the next row to play (en_ord, en_row: the one playing) */

/* --------------------------------------------------------- the player --- */
typedef struct {
    uint8_t on, rel, ins, echo;
    float pitch;                                 /* semitones now (with slides) */
    float to, sp;                                /* a slide or portamento: its target and speed (semitones a frame) */
    uint8_t ax, ay;                              /* arpeggio */
    uint8_t vs, vd;                              /* vibrato: speed, depth */
    float vph;
    int fr, rfr, relv;                           /* frames since the note, since its release; the level at release */
    int vol, duty;
    float hz;
    /* the oscillator */
    float ph, inc, amp, amp_t, dutyf;
    uint32_t lfsr;
    float nacc, nrate, nout;
    int nshort;
} chn_t;
static chn_t ch[EN_NCH], kb;                     /* the four, and the keys played along (a pulse) */
static int kb_note;
static float row_left, frame_left;
static int gap_left;
static int retune, retune_go;                     /* the mood moved: samples until it counts as rested; then go */
static float row_len = 4725.0f;
static float lv, hp_l, hp_r, t1l, t2l, t1r, t2r, tone_a = 0.5f;
static float vu_acc;
static float fade = 1.0f;

static const uint16_t NES_NOISE[16] = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};
static inline float mtof(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }

static void tempo_set(void)
{
    int b = par[P_TEMPO];
    en_bpm = (uint8_t)b;
    row_len = EN_SR * 15.0f / (float)b;
}
static float row_time(int row)                   /* a row's length, with the swing (the style's: eighths or 16ths) */
{
    float s = (float)par[P_SWING] * (0.32f / 100.0f);
    int first = STYLES[en_cur->style].swing8 ? (row & 2) == 0 : (row & 1) == 0;
    return row_len * (first ? 1.0f + s : 1.0f - s);
}

static void note_on(chn_t *c, int note, int ins, int fx, int fxp, int echo)
{
    c->on = 1;
    c->rel = 0;
    c->ins = (uint8_t)ins;
    c->echo = (uint8_t)echo;
    c->fr = 0;
    c->rfr = 0;
    c->ax = c->ay = 0;
    c->vs = c->vd = 0;
    c->sp = 0.0f;
    if (fx == '3' && c->pitch > 0.0f) {         /* portamento from where it was */
        c->to = (float)note;
        c->sp = (float)fxp * (1.0f / 32.0f);
    } else {
        c->pitch = (float)note;
        c->to = (float)note;
    }
    if (fx == '0') {
        c->ax = (uint8_t)(fxp >> 4);
        c->ay = (uint8_t)(fxp & 15);
    } else if (fx == '4') {
        c->vs = (uint8_t)(fxp >> 4);
        c->vd = (uint8_t)(fxp & 15);
        c->vph = 0.0f;
    } else if (fx == 'Q' || fx == 'R') {        /* a slide by y semitones, speed x */
        c->to = (float)(note + (fx == 'Q' ? 1 : -1) * (fxp & 15));
        c->sp = (float)(fxp >> 4) * (1.0f / 8.0f);
    }
}

static void row_step(void)
{
    const en_song_t *s = en_cur;
    int p = s->order[pord], k, lvl = en_energy_level(par[P_ENERGY]);
    en_ord = (uint8_t)pord;
    en_row = (uint8_t)prow;
    en_phase = s->osect[pord] == 'I' ? PH_INTRO : s->osect[pord] == 'E' ? PH_JINGLE : PH_BODY;
    for (k = 0; k < EN_NCH; k++) {
        const en_cell_t *e = &s->pat[p][prow][k];
        chn_t *c = &ch[k];
        if (!e->note)
            continue;
        if (e->lay & LAY_ECHO) {
            if (par[P_ECHO] == 0 || (e->lay & 7) > lvl)
                continue;
        } else if ((e->lay & 7) > lvl) {
            continue;
        }
        if (e->note == NOTE_REL) {
            if (c->on && !c->rel) {
                c->rel = 1;
                c->rfr = 0;
                c->relv = c->vol;
            }
        } else if (e->note == NOTE_CUT) {
            c->on = 0;
        } else {
            note_on(c, e->note, e->ins, e->fx, e->fxp, (e->lay & LAY_ECHO) != 0);
            if (k == CH_NOI && e->ins == I_KICK)
                en_kick++;
        }
    }
    en_rows_played++;
}

static float vib_tab(float ph)                   /* a sine from fastmath */
{
    return fm_sinf(ph * FM_TWO_PI);
}
static void frame(chn_t *c, int k)               /* a 60 Hz tick: envelopes, arpeggio, vibrato, slides */
{
    const ins_t *in = &INS[c->ins < I_N ? c->ins : 0];
    int v, f = c->fr, idx;
    float pt;
    if (!c->on) {
        c->vol = 0;
        return;
    }
    v = in->vol[f < in->nvol ? f : in->nvol - 1];
    if (c->rel) {
        if (!in->rel || k == CH_TRI) {
            v = 0;
            c->on = 0;
        } else {
            int dv = c->relv - c->rfr / in->rel;
            v = dv < v ? dv : v;
            if (v <= 0) {
                v = 0;
                c->on = 0;
            }
        }
        c->rfr++;
    }
    if (c->echo) {
        v = (v * par[P_ECHO] + 40) / 70;
        if (v > 9)
            v = 9;
    }
    c->vol = v < 0 ? 0 : v > 15 ? 15 : v;
    c->duty = in->duty[f < in->nduty ? f : in->nduty - 1];
    pt = (float)in->pit[f < in->npit ? f : in->npit - 1];
    if (c->sp > 0.0f) {                          /* slides and portamento */
        if (c->pitch < c->to)
            c->pitch = fm_minf(c->pitch + c->sp, c->to);
        else if (c->pitch > c->to)
            c->pitch = fm_maxf(c->pitch - c->sp, c->to);
    }
    if (c->ax | c->ay) {                         /* the arpeggio: a step a frame (or two) */
        int sp = STYLES[en_cur->style].arp_sp, st = (f / (sp ? sp : 1)) % 3;
        pt += (float)(st == 1 ? c->ax : st == 2 ? c->ay : 0);
    }
    if (c->vd) {                                 /* vibrato, easing in over its first quarter second */
        float ramp = fm_minf((float)f * (1.0f / 15.0f), 1.0f);
        c->vph += (float)c->vs * (0.9f / 60.0f);
        if (c->vph >= 1.0f)
            c->vph -= 1.0f;
        pt += vib_tab(c->vph) * (float)c->vd * 0.055f * ramp;
    }
    if (k == CH_NOI) {
        idx = (int)c->pitch - 1 + (int)pt;
        idx = idx < 0 ? 0 : idx > 15 ? 15 : idx;
        c->nrate = 1789773.0f / (float)NES_NOISE[idx] / EN_SR;
        c->nshort = c->duty == 1;
        c->hz = c->nrate * EN_SR * (c->nshort ? 1.0f / 93.0f : 1.0f / 64.0f);
    } else {
        c->hz = mtof(c->pitch + pt);
        c->inc = c->hz / EN_SR;
        c->dutyf = c->duty == 0 ? 0.125f : c->duty == 1 ? 0.25f : 0.5f;
    }
    c->fr++;
}

/* ------------------------------------------------------------- control --- */
static void quiet(void)
{
    int k;
    for (k = 0; k < EN_NCH; k++) {
        ch[k].on = 0;
        ch[k].vol = 0;
    }
    kb.on = 0;
}
static void new_song(void)
{
    uint32_t seed;
    en_song_t *s = &songs[song_i ^ 1];
    en_song_no++;
    seed = en_song_rand(en_seed_base, en_song_no);
    en_make_song(s, style_i, seed, par[P_MOOD], par[P_TEMPO]);
    song_i ^= 1;
    BARRIER();
    en_cur = s;
    en_ord = 0;
    en_row = 0;
    pord = prow = 0;
    en_loop = 0;
    en_phase = PH_INTRO;
    row_left = 0.0f;
    retune = retune_go = 0;
    en_songs_played++;
}
static void end_song(float gap_s)
{
    int k;
    for (k = 0; k < EN_NCH; k++)
        if (ch[k].on && !ch[k].rel) {
            ch[k].rel = 1;
            ch[k].rfr = 0;
            ch[k].relv = ch[k].vol;
        }
    en_phase = PH_GAP;
    gap_left = (int)(EN_SR * gap_s);
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
                int v = b < p->lo ? p->lo : b > p->hi ? p->hi : b;
                par[a] = (int16_t)v;
                if (a == P_TEMPO)
                    tempo_set();
                if (a == P_LOOPS)
                    en_loops = (uint8_t)v;
            }
            break;
        case C_STYLE: style_i = a % EN_NSTYLES; break;
        case C_PLAY:
            if (a && !en_playing) {
                en_playing = 1;
                fade = 0.0f;
            } else if (!a && en_playing) {
                en_playing = 0;
                quiet();
            }
            break;
        case C_SKIP:
            quiet();
            en_phase = PH_GAP;
            gap_left = (int)(EN_SR * 0.25f);
            break;
        case C_REPEAT: en_repeat_on = (uint8_t)(a != 0); break;
        case C_RETUNE:
            if (en_phase != PH_GAP) {
                retune = (int)(EN_SR * 1.5f);    /* once the knob rests, the song closes (its jingle) for a new one */
                retune_go = 0;
            }
            break;
        case C_SEED_HI: en_seed_base = (en_seed_base & 0xFFFFu) | ub << 16; break;
        case C_SEED_LO: en_seed_base = (en_seed_base & 0xFFFF0000u) | ub; break;
        case C_SONG_NO:
            en_song_no = (uint32_t)a << 16 | ub;  /* the next is no + 1 */
            quiet();
            new_song();
            break;
        case C_NOTE:
            if (b > 0) {
                kb_note = a;
                note_on(&kb, a, I_PLUCK25, 0, 0, 0);
                frame(&kb, CH_PU1);
            } else if (a == kb_note && kb.on && !kb.rel) {
                kb.rel = 1;
                kb.rfr = 0;
                kb.relv = kb.vol;
            }
            break;
        case C_PANIC: quiet(); break;
        }
    }
}

void en_init(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        par[i] = PARAMS[i].def;
    tempo_set();
    en_loops = (uint8_t)par[P_LOOPS];
    for (i = 0; i < EN_NCH; i++) {
        ch[i].lfsr = 1u;
        ch[i].on = 0;
        ch[i].pitch = 0.0f;
    }
    kb.on = 0;
    for (i = 0; i < EN_NCH; i++) {
        int j;
        for (j = 0; j < EN_SCOPE; j++)
            en_scope[i][j] = 0;
    }
    q_r = q_w;
    quiet();
    en_song_no = 0;
    song_i = 1;
    new_song();
}

/* -------------------------------------------------------------- render --- */
static inline float blep(float t, float dt)     /* polyBLEP: the step's correction near t = 0 (t in turns) */
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
static inline float pulse(chn_t *c)
{
    float d = c->dutyf, p = c->ph, t, y;
    c->ph = p + c->inc;
    if (c->ph >= 1.0f)
        c->ph -= 1.0f;
    y = (p < d ? 1.0f : 0.0f) - d;               /* a unipolar pulse, its mean taken off */
    y += 0.5f * blep(p, c->inc);
    t = p - d;
    if (t < 0.0f)
        t += 1.0f;
    y -= 0.5f * blep(t, c->inc);
    return y;
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 1.5f;
    float g1 = (float)par[P_PU1] * 0.01f * 0.27f, g2 = (float)par[P_PU2] * 0.01f * 0.27f;
    float gt = (float)par[P_TRI] * 0.01f * 0.34f, gn = (float)par[P_NOISE] * 0.01f * 0.22f;
    float fc = 2600.0f * fm_exp2f((float)par[P_TONE] * 0.026f), ta, hp_a = 1.0f - fm_expf(-FM_TWO_PI * 25.0f / EN_SR);
    float sm = 1.0f - fm_expf(-1.0f / (0.0004f * EN_SR));   /* volume steps, smoothed: 0.4 ms */
    float lv_rel = fm_expf(-1.0f / (0.4f * EN_SR)), lv_att = 1.0f - fm_expf(-1.0f / (0.002f * EN_SR));
    uint32_t sw = en_scope_w;
    int k;
    fc = fm_clampf(fc, 1000.0f, 18000.0f);
    ta = 1.0f - fm_expf(-FM_TWO_PI * fc / EN_SR);
    tone_a += (ta - tone_a) * 0.2f;
    drain();
    while (n) {
        uint32_t m = n, j;
        float a[EN_NCH], kba;
        if (en_playing) {
            if (en_phase == PH_GAP) {
                if (gap_left <= 0) {
                    new_song();
                } else if ((uint32_t)gap_left < m) {
                    m = (uint32_t)gap_left;
                }
            } else if (row_left <= 0.0f) {
                const en_song_t *s = en_cur;
                row_step();
                for (k = 0; k < EN_NCH; k++)
                    if (ch[k].on && ch[k].fr == 0)
                        frame(&ch[k], k);   /* a new note sounds at once */
                row_left += row_time(prow);
                if (++prow >= s->plen[s->order[pord]]) {   /* the pattern's end: the next in the order */
                    int nx;
                    prow = 0;
                    if (retune_go && s->osect[pord] != 'E') {
                        nx = s->nord - 1;        /* the mood moved: on to the jingle */
                        retune_go = 0;
                    } else
                        nx = en_next_ord(s, pord, en_loop, en_loops, en_repeat_on);
                    if (nx < 0) {
                        end_song(1.4f);
                    } else {
                        if (nx < pord && s->osect[nx] != 'E')
                            en_loop++;
                        pord = nx;
                    }
                }
            }
            if (en_phase != PH_GAP && (float)m > row_left)
                m = (uint32_t)row_left + 1u;
        }
        if (frame_left <= 0.0f) {
            for (k = 0; k < EN_NCH; k++)
                frame(&ch[k], k);
            frame(&kb, CH_PU1);
            frame_left += EN_SR / 60.0f;
        }
        if ((float)m > frame_left)
            m = (uint32_t)frame_left + 1u;
        if (m > n)
            m = n;
        if (m == 0)
            m = 1;
        for (k = 0; k < EN_NCH; k++) {
            ch[k].amp_t = (float)ch[k].vol * (1.0f / 15.0f);
            en_ch_vol[k] = (uint8_t)ch[k].vol;
            en_ch_note[k] = (uint8_t)(ch[k].on ? (int)ch[k].pitch : 0);
            en_ch_hz[k] = ch[k].hz;
        }
        kb.amp_t = (float)kb.vol * (1.0f / 15.0f);
        a[0] = g1;
        a[1] = g2;
        a[2] = gt;
        a[3] = gn;
        kba = 0.27f;
        for (j = 0; j < m; j++) {
            float p1, p2, tr, ns, kv = 0.0f, l, r, x, vol;
            chn_t *c;
            /* the pulses */
            c = &ch[0];
            c->amp += (c->amp_t - c->amp) * sm;
            p1 = c->amp > 1e-4f ? pulse(c) * c->amp : 0.0f;
            c = &ch[1];
            c->amp += (c->amp_t - c->amp) * sm;
            p2 = c->amp > 1e-4f ? pulse(c) * c->amp : 0.0f;
            /* the triangle: 32 steps of 4 bits */
            c = &ch[2];
            c->amp += ((c->on ? 1.0f : 0.0f) - c->amp) * sm;
            if (c->amp > 1e-4f) {
                float ph = c->ph, s;
                int st;
                c->ph = ph + c->inc;
                if (c->ph >= 1.0f)
                    c->ph -= 1.0f;
                st = (int)(ph * 32.0f);
                s = (float)(st < 16 ? 15 - st : st - 16) * (1.0f / 15.0f);
                tr = (s - 0.5f) * c->amp;
            } else {
                tr = 0.0f;
            }
            /* the noise: the LFSR clocked at its rate, its bits averaged over the sample */
            c = &ch[3];
            c->amp += (c->amp_t - c->amp) * sm;
            if (c->amp > 1e-4f) {
                float acc = 0.0f, cnt = 0.0f;
                c->nacc += c->nrate;
                while (c->nacc >= 1.0f) {
                    uint32_t fb = ((c->lfsr ^ (c->lfsr >> (c->nshort ? 6 : 1))) & 1u);
                    c->lfsr = (c->lfsr >> 1) | (fb << 14);
                    c->nout = (c->lfsr & 1u) ? 0.5f : -0.5f;
                    acc += c->nout;
                    cnt += 1.0f;
                    c->nacc -= 1.0f;
                }
                ns = (cnt > 0.0f ? acc / cnt : c->nout) * c->amp;
            } else {
                ns = 0.0f;
            }
            /* the keys played along */
            kb.amp += (kb.amp_t - kb.amp) * sm;
            if (kb.amp > 1e-4f)
                kv = pulse(&kb) * kb.amp * kba;
            en_scope[0][sw] = (int8_t)(p1 * 200.0f);
            en_scope[1][sw] = (int8_t)(p2 * 200.0f);
            en_scope[2][sw] = (int8_t)(tr * 200.0f);
            en_scope[3][sw] = (int8_t)(ns * 200.0f);
            sw = (sw + 1u) & (EN_SCOPE - 1u);
            p1 *= a[0];
            p2 *= a[1];
            tr *= a[2];
            ns *= a[3];
            l = p1 * 0.6f + p2 * 0.4f + tr + ns + kv;
            r = p1 * 0.4f + p2 * 0.6f + tr + ns + kv;
            l *= fade;
            r *= fade;
            fade = fade < 1.0f ? fade + 1.0f / (0.05f * EN_SR) : 1.0f;
            /* tone: a two-pole low-pass (the edges' last harshness), a DC cut */
            t1l += (l - t1l) * tone_a;
            t2l += (t1l - t2l) * tone_a;
            t1r += (r - t1r) * tone_a;
            t2r += (t1r - t2r) * tone_a;
            hp_l += (t2l - hp_l) * hp_a;
            hp_r += (t2r - hp_r) * hp_a;
            l = t2l - hp_l;
            r = t2r - hp_r;
            vu_acc += l * l + r * r;
            /* the leveller: a quick attack, a slow release, above -6 dB */
            x = fm_maxf(fm_fabsf(l), fm_fabsf(r)) * gain;
            lv = x > lv ? lv + (x - lv) * lv_att : lv * lv_rel;
            vol = lv > 0.5f ? 0.5f / lv : 1.0f;
            l *= gain * vol;
            r *= gain * vol;
            out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
            out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
        }
        en_scope_w = sw;
        if (en_playing) {
            if (en_phase == PH_GAP)
                gap_left -= (int)m;
            else
                row_left -= (float)m;
            if (retune > 0 && (retune -= (int)m) <= 0)
                retune_go = 1;
        }
        frame_left -= (float)m;
        en_vu = en_vu + (fm_minf(fm_sqrtf(vu_acc / (float)(2u * m)) * 3.0f, 1.0f) - en_vu) * 0.1f;
        vu_acc = 0.0f;
        out += 2 * m;
        n -= m;
    }
}
