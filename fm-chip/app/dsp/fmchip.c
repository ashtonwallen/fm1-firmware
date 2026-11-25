/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * FM CHIP: ashtonwallen, 2026 */
/* The instrument (fmchip.h). One compilation unit, -O2. The command queue and the render's shape are AMB-1's; the
 * song maker and the tracker are CHIP TRACKER's, grown to six FM channels, PCM drums and a noise hat. */
#include "fmchip.h"
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
    {"Tempo", 60, 200, 150}, {"Energy", 0, 100, 70}, {"Echo", 0, 100, 40}, {"Mood", 0, 100, 20},
    {"Lead", 0, 100, 75},    {"Chords", 0, 100, 70}, {"Bass", 0, 100, 75}, {"Drums", 0, 100, 75},
    {"Swing", 0, 100, 0},    {"Tone", 0, 100, 55},   {"Loops", 1, 4, 2},
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

/* chords: a root (semitones above the key's tonic) and a quality, as root | q << 4. The melody counts the first
 * three tones as the chord's (the triad); the voicings take the seventh too */
enum { QM, Qm, Qd, QM7, Qm7, Q7, Qs7, QN };
static const uint8_t Q_T[QN][4] = {{0, 4, 7, 0}, {0, 3, 7, 0}, {0, 3, 6, 0}, {0, 4, 7, 11},
                                   {0, 3, 7, 10}, {0, 4, 7, 10}, {0, 5, 7, 10}};
static const uint8_t Q_N[QN] = {3, 3, 3, 4, 4, 4, 4};
static const char *const Q_SUF[QN] = {"", "m", "dim", "maj7", "m7", "7", "7sus"};
#define CH(r, q) ((uint8_t)((r) | (q) << 4))
#define CQ(c) ((c) >> 4 < QN ? (c) >> 4 : 0)
void en_chord_text(uint8_t c, char *b)
{
    if (c == 0xFFu) {
        b[0] = 0;
        return;
    }
    put_s(put_s(b, EN_NOTE_NAME[(c & 15u) % 12u]), Q_SUF[CQ(c)]);
}

/* the progressions: four bars, a chord each, the sevenths where the era's composers put them (the royal road,
 * the ii-V, the Dorian vamp, the bVI-bVII lift). A song takes one for its verse, another for its chorus, a third
 * for its bridge (the bridge's lead back to the chorus) */
enum { R_V = 1, R_C = 2, R_B = 4 };
typedef struct {
    uint8_t mode, roles;
    uint8_t c[4];
} prog_t;
static const prog_t PROGS[] = {
    {MD_MAJOR, R_V | R_C, {CH(5, QM7), CH(7, Q7), CH(4, Qm7), CH(9, Qm7)}},     /* IVM7 V7 iii7 vi7: the royal road */
    {MD_MAJOR, R_V, {CH(0, QM7), CH(9, Qm7), CH(2, Qm7), CH(7, Q7)}},          /* IM7 vi7 ii7 V7 */
    {MD_MAJOR, R_V | R_C, {CH(0, QM), CH(7, QM), CH(9, Qm7), CH(5, QM7)}},      /* I V vi7 IVM7 */
    {MD_MAJOR, R_V, {CH(0, QM), CH(5, QM7), CH(0, QM), CH(7, Qs7)}},           /* I IVM7 I V7sus */
    {MD_MAJOR, R_C, {CH(5, QM7), CH(7, Q7), CH(0, QM7), CH(9, Qm7)}},          /* IVM7 V7 IM7 vi7 */
    {MD_MAJOR, R_C, {CH(0, QM), CH(5, QM), CH(9, Qm7), CH(7, Qs7)}},           /* I IV vi7 V7sus */
    {MD_MAJOR, R_B, {CH(2, Qm7), CH(4, Qm7), CH(5, QM7), CH(7, Qs7)}},         /* ii7 iii7 IVM7 V7sus */
    {MD_MAJOR, R_B, {CH(9, Qm7), CH(5, QM7), CH(2, Qm7), CH(7, Q7)}},          /* vi7 IVM7 ii7 V7 */
    {MD_MAJOR, R_B, {CH(5, QM7), CH(7, Q7), CH(8, QM7), CH(10, Q7)}},          /* IV V bVI bVII: the lift home */
    {MD_MIXO, R_V | R_C, {CH(0, QM), CH(10, QM), CH(5, QM), CH(0, QM)}},       /* I bVII IV I */
    {MD_MIXO, R_V | R_C, {CH(10, QM), CH(5, QM), CH(0, QM), CH(0, QM)}},       /* bVII IV I */
    {MD_MIXO, R_V, {CH(0, QM), CH(10, QM), CH(0, QM), CH(5, QM7)}},            /* I bVII I IVM7 */
    {MD_MIXO, R_C, {CH(0, QM), CH(7, Qm7), CH(10, QM), CH(5, QM)}},            /* I v7 bVII IV */
    {MD_MIXO, R_B, {CH(5, QM7), CH(5, QM7), CH(10, QM), CH(10, QM)}},          /* IVM7 bVII */
    {MD_MIXO, R_B, {CH(2, Qm7), CH(5, QM), CH(2, Qm7), CH(10, QM)}},           /* ii7 IV ii7 bVII */
    {MD_DORIAN, R_V, {CH(0, Qm7), CH(5, Q7), CH(0, Qm7), CH(5, Q7)}},          /* i7 IV7: the Dorian vamp */
    {MD_DORIAN, R_V | R_C, {CH(0, Qm7), CH(3, QM7), CH(5, Q7), CH(0, Qm7)}},    /* i7 bIIIM7 IV7 i7 */
    {MD_DORIAN, R_C, {CH(0, Qm7), CH(2, Qm7), CH(3, QM7), CH(5, Q7)}},         /* i7 ii7 bIIIM7 IV7 */
    {MD_DORIAN, R_C, {CH(0, Qm7), CH(10, QM), CH(5, QM), CH(0, Qm7)}},         /* i7 bVII IV i7 */
    {MD_DORIAN, R_B, {CH(3, QM7), CH(5, Q7), CH(10, QM7), CH(5, Q7)}},         /* bIIIM7 IV7 bVIIM7 IV7 */
    {MD_DORIAN, R_B, {CH(10, QM7), CH(10, QM7), CH(5, Q7), CH(5, Q7)}},        /* bVIIM7 IV7 */
    {MD_MINOR, R_V | R_C, {CH(0, Qm7), CH(8, QM7), CH(10, QM), CH(0, Qm7)}},    /* i7 bVIM7 bVII i7 */
    {MD_MINOR, R_V, {CH(0, Qm), CH(10, QM), CH(8, QM), CH(10, QM)}},           /* i bVII bVI bVII */
    {MD_MINOR, R_V, {CH(0, Qm7), CH(5, Qm7), CH(0, Qm7), CH(7, Qm7)}},         /* i7 iv7 i7 v7 */
    {MD_MINOR, R_C, {CH(0, Qm), CH(8, QM), CH(3, QM), CH(10, QM)}},            /* i bVI bIII bVII */
    {MD_MINOR, R_C, {CH(8, QM7), CH(10, Q7), CH(0, Qm7), CH(0, Qm7)}},         /* bVIM7 bVII7 i7 */
    {MD_MINOR, R_V | R_C, {CH(0, Qm7), CH(5, Qm7), CH(10, Q7), CH(3, QM7)}},    /* i7 iv7 bVII7 bIIIM7 */
    {MD_MINOR, R_B, {CH(5, Qm7), CH(7, Qm7), CH(8, QM7), CH(10, Q7)}},         /* iv7 v7 bVIM7 bVII7 */
    {MD_MINOR, R_B, {CH(8, QM7), CH(10, Q7), CH(5, Qm7), CH(7, Q7)}},          /* bVIM7 bVII7 iv7 V7 */
    {MD_HARM, R_V | R_C, {CH(0, Qm), CH(8, QM7), CH(5, Qm7), CH(7, Q7)}},       /* i bVIM7 iv7 V7 */
    {MD_HARM, R_V, {CH(0, Qm), CH(5, Qm7), CH(7, Q7), CH(0, Qm)}},             /* i iv7 V7 i */
    {MD_HARM, R_V | R_C, {CH(0, Qm), CH(0, Qm), CH(8, QM), CH(7, Q7)}},         /* i bVI V7 */
    {MD_HARM, R_C, {CH(0, Qm), CH(7, Q7), CH(8, QM), CH(7, Q7)}},              /* i V7 bVI V7 */
    {MD_HARM, R_B, {CH(8, QM7), CH(5, Qm7), CH(8, QM7), CH(7, Q7)}},           /* bVIM7 iv7 bVIM7 V7 */
    {MD_HARM, R_B, {CH(5, Qm7), CH(5, Qm7), CH(7, Qs7), CH(7, Q7)}},           /* iv7 V7sus V7 */
};

/* ------------------------------------------------------------ motifs --- */
/* A melody is built from one-bar motifs: 16 sixteenths, a token each. "cN": the Nth chord tone from the section's
 * anchor (c0 the first at or above it; negative below), so a motif over another chord comes out as its sequence;
 * "u" / "d" (or "u2", "d2"): scale steps from the note before (passing and neighbour tones); "tN": N scale steps
 * above the cadence's target; "-" the note held; "." a rest. A motif starts with a chord tone or a rest. On beats
 * one and three, and wherever a note is held a beat or more, a note that is not in the chord is moved onto one. */
enum { F_BOUNCE = 1, F_LYRIC = 2, F_HERO = 4, F_DARK = 8, F_DRIVE = 16, F_FUNK = 32 };
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
    {F_FUNK, "c0 - - c1 - - c2 - - - c1 - c2 - - ."},
    {F_FUNK, ". . c2 - c1 - c0 . c1 - - c2 - - . ."},
    {F_FUNK | F_DRIVE, "c0 . c0 c1 - c0 - c2 - - c1 - c0 - . ."},
    {F_FUNK, "c2 - c1 . c0 - - - . c0 c1 - c2 - - -"},
    {F_FUNK | F_HERO, "c0 - - c2 - - c1 - c3 - - - - - . ."},
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

/* the bass: a bar each. R the root, O its octave, F the fifth, T the third, A a step onto the next bar's root;
 * r and o the same as ghost notes (the slap's dead thumbs); "-" held, "." cut */
static const char *const BASS[] = {
    "R - O - R - O - R - O - R - O -",          /* 0 octaves */
    "R . O r . R O . R . O r . R O .",          /* 1 slap octaves */
    "R - R - R - R - R - R - R - A -",          /* 2 driving roots */
    "R - - - T - - - F - - - A - - -",          /* 3 walking */
    "R - - - - - - - F - - - - - - -",          /* 4 halves */
    "R - R R - R R - R - R R - R A -",          /* 5 gallop */
    "R - - O - - R - - O - - R - A -",          /* 6 three-three-two, octaves */
    "R - O - r O R - O - R - O r A -",          /* 7 disco slap */
    "R - - O - - R - . R O - F - A -",          /* 8 funk walk */
};
/* the drums. The DAC: K kick, S snare, g ghost snare, T t toms, P clap, r rim, C crash; the PSG: h hat, o open */
static const char *const DAC[] = {
    "K . . . S . . . K . K . S . . .",          /* 0 the beat */
    "K . . . S . . K . . K . S . . .",          /* 1 the beat, pushed */
    "K . . K S . . . K . K K S . . .",          /* 2 driving */
    "K . . . . . . . S . . . . . . .",          /* 3 half time */
    "K . . . P . . K K . . . P . . .",          /* 4 clap house */
    "K . . . r . . . K . . . r . . .",          /* 5 light, a rim */
    "K . . g S . g K . g K . S . g .",          /* 6 funk ghosts */
    "K . g g S . g . K . g g S g S .",          /* 7 march */
    "K . . K S . . . . . K . S . . .",          /* 8 shuffle */
    "K . K . S . K . K . K . S . K K",          /* 9 double kick */
};
static const char *const HATS[] = {
    "h . h . h . h . h . h . h . h .",          /* 0 eighths */
    "h h h h h h h h h h h h h h h h",          /* 1 sixteenths */
    ". . o . . . o . . . o . . . o .",          /* 2 open offbeats */
    "h . . . h . . . h . . . h . . .",          /* 3 quarters */
    "h . h h h . h h h . h h h . h h",          /* 4 skipping */
    "h . o . h . o . h . o . h . o .",          /* 5 closed, open */
};
static const char *const FILLS[] = {"S S S S", "T T t t", "S T t t", "g S S S", "T t S S"};
/* the chords' rhythms (x a hit, "-" held, "." off) and their broken figures (voicing tones 0..2, 3 the lowest an
 * octave up) */
static const char *const STABR[] = {
    ". . x . . . x . . . x . . . x .",          /* 0 offbeat stabs */
    "x . . x . . x . . . x . . . . .",          /* 1 three-three-four */
    "x . . x . . x . x . . x . . x .",          /* 2 three-three-two, twice */
    "x - - - - - x . . . x - - - - .",          /* 3 long, short, long */
    "x - - - - - - . x - - - - - - .",          /* 4 halves */
    "x - - - - - - - - - - - - - - .",          /* 5 the bar (a pad) */
    "x . x . x . x . x . x . x . x .",          /* 6 eighths pumping */
    ". . x - . . x - . . x - . . x -",          /* 7 offbeat comping */
    "x . . x . . . . x . . x . . . .",          /* 8 the push, twice */
};
static const char *const BROKEN[] = {
    "0 - 2 - 1 - 2 - 0 - 2 - 1 - 2 -",          /* 0 Alberti */
    "0 1 2 3 2 1 0 1 2 3 2 1 0 1 2 1",          /* 1 sixteenths up and down */
    "0 - 1 - 2 - 3 - 2 - 1 - 2 - 1 -",          /* 2 eighths up and down */
    "0 2 3 2 0 2 3 2 0 2 3 2 0 2 3 2",          /* 3 a rolling figure */
};
static const char *const ARPR[] = {
    "x - x - x - x - x - x - x - x -",          /* 0 eighths */
    "- - x - - - x - - - x - - - x -",          /* 1 offbeats */
    "x - - x - - x - x - - x - - x -",          /* 2 three-three-two */
    "x - - - x - - - x - - - x - - -",          /* 3 quarters */
};

/* ------------------------------------------------------------ patches --- */
/* the sound bank, in YM2612 terms: algorithm, feedback, then each operator's MUL TL AR DR SL SR RR KS DT */
enum {
    PT_BRASS, PT_SYNLEAD, PT_REED, PT_FLUTE, PT_GUITAR, PT_BELL, PT_EP, PT_STRINGS, PT_ORGAN, PT_CLAV, PT_MARIMBA,
    PT_HARP, PT_STAB, PT_SLAP, PT_SYNBASS, PT_FINGER, PT_N
};
#define OP(mul, tl, ar, dr, sl, sr, rr, ks, dt) {mul, tl, ar, dr, sl, sr, rr, ks, dt}
static const en_patch_t PATCHES[PT_N] = {
    {"BRASS", 4, 5, {OP(1, 20, 16, 7, 3, 1, 8, 1, 0), OP(1, 0, 22, 4, 1, 0, 9, 1, -1),
                     OP(1, 24, 16, 7, 3, 1, 8, 1, 1), OP(1, 3, 22, 4, 1, 0, 9, 1, 2)}},
    {"SYN LEAD", 4, 6, {OP(1, 21, 31, 6, 3, 2, 8, 0, 0), OP(1, 0, 30, 4, 1, 1, 9, 0, 0),
                        OP(2, 38, 31, 8, 5, 2, 8, 0, 3), OP(1, 6, 30, 4, 1, 1, 9, 0, -3)}},
    {"REED", 3, 4, {OP(1, 36, 20, 6, 4, 2, 8, 1, 0), OP(3, 44, 18, 8, 6, 2, 8, 1, 0),
                    OP(1, 24, 18, 6, 3, 1, 8, 1, 0), OP(1, 0, 21, 3, 1, 0, 9, 1, 0)}},
    {"FLUTE", 4, 2, {OP(1, 48, 16, 4, 2, 1, 8, 0, 0), OP(1, 0, 17, 2, 1, 0, 8, 0, 1),
                     OP(4, 52, 18, 6, 4, 2, 8, 0, 0), OP(2, 24, 17, 3, 2, 1, 8, 0, -1)}},
    {"GUITAR", 0, 6, {OP(1, 27, 31, 5, 3, 2, 8, 1, 0), OP(3, 36, 31, 6, 4, 2, 8, 1, 0),
                      OP(1, 24, 31, 4, 2, 2, 8, 1, 1), OP(1, 0, 31, 3, 2, 2, 9, 1, 0)}},
    {"BELL", 4, 0, {OP(7, 34, 31, 8, 15, 0, 8, 1, 0), OP(2, 6, 31, 5, 15, 0, 7, 1, 0),
                    OP(1, 38, 31, 7, 15, 0, 7, 1, 2), OP(1, 0, 31, 4, 15, 0, 7, 1, -2)}},
    {"E.PIANO", 4, 0, {OP(14, 44, 31, 13, 15, 0, 8, 2, 0), OP(1, 10, 31, 6, 15, 0, 8, 2, 0),
                       OP(1, 36, 31, 8, 8, 3, 8, 1, 2), OP(1, 0, 31, 5, 6, 3, 8, 1, -2)}},
    {"STRINGS", 4, 4, {OP(1, 26, 12, 2, 2, 0, 6, 0, 0), OP(1, 2, 13, 2, 1, 0, 6, 0, 3),
                       OP(1, 28, 12, 2, 2, 0, 6, 0, 0), OP(1, 2, 13, 2, 1, 0, 6, 0, -3)}},
    {"ORGAN", 7, 3, {OP(1, 8, 31, 0, 0, 0, 12, 0, 1), OP(2, 10, 31, 0, 0, 0, 12, 0, 0),
                     OP(3, 22, 31, 0, 0, 0, 12, 0, -1), OP(4, 18, 31, 0, 0, 0, 12, 0, 0)}},
    {"HARPSI", 4, 3, {OP(5, 30, 31, 10, 15, 0, 9, 1, 0), OP(1, 4, 31, 7, 15, 0, 9, 1, 0),
                      OP(3, 40, 31, 9, 15, 0, 9, 1, 1), OP(2, 12, 31, 7, 15, 0, 9, 1, 0)}},
    {"MARIMBA", 4, 0, {OP(4, 36, 31, 13, 15, 0, 10, 1, 0), OP(1, 2, 31, 9, 15, 0, 9, 1, 0),
                       OP(10, 52, 31, 16, 15, 0, 10, 1, 0), OP(4, 22, 31, 12, 15, 0, 10, 1, 0)}},
    {"HARP", 4, 0, {OP(2, 32, 31, 9, 15, 0, 8, 1, 0), OP(1, 2, 31, 6, 15, 0, 7, 1, 0),
                    OP(3, 46, 31, 10, 15, 0, 8, 1, 0), OP(2, 18, 31, 7, 15, 0, 7, 1, 0)}},
    {"STAB", 4, 5, {OP(1, 18, 31, 10, 5, 4, 10, 1, 0), OP(1, 0, 31, 6, 3, 3, 11, 1, -1),
                    OP(1, 22, 31, 11, 6, 4, 10, 1, 1), OP(1, 4, 31, 6, 3, 3, 11, 1, 2)}},
    {"SLAP", 4, 5, {OP(1, 22, 31, 14, 10, 6, 10, 1, 0), OP(1, 0, 31, 7, 4, 4, 11, 1, 0),
                    OP(3, 32, 31, 18, 15, 0, 11, 1, 0), OP(1, 6, 31, 9, 6, 4, 11, 1, 0)}},
    {"SYN BASS", 4, 6, {OP(1, 22, 31, 9, 6, 3, 10, 0, 0), OP(1, 0, 31, 5, 3, 2, 11, 0, 0),
                        OP(2, 40, 31, 10, 8, 3, 10, 0, 0), OP(1, 8, 31, 5, 3, 2, 11, 0, 1)}},
    {"FINGER", 4, 2, {OP(1, 34, 31, 10, 8, 4, 10, 1, 0), OP(1, 0, 31, 6, 4, 3, 10, 1, 0),
                      OP(2, 50, 31, 12, 15, 0, 10, 1, 0), OP(1, 14, 31, 6, 4, 3, 10, 1, 1)}},
};
const en_patch_t *en_patch(int i) { return &PATCHES[(unsigned)i < PT_N ? i : 0]; }
int en_npatches(void) { return PT_N; }
static const uint8_t ALG_CAR[8] = {8, 8, 8, 8, 10, 14, 14, 15};
int en_alg_carriers(int alg) { return ALG_CAR[alg & 7]; }

static const char *const DRUM_NAME[EN_NDRUMS] = {"KICK", "SNARE", "TOM HI", "TOM LO", "CLAP", "CRASH", "HIT", "RIM"};
const char *en_drum_name(int i) { return DRUM_NAME[(unsigned)i < EN_NDRUMS ? i : 0]; }
enum { HT_CLOSED, HT_OPEN };

/* ------------------------------------------------------------- styles --- */
/* each game: the knobs' recipe (Tempo Energy Echo Mood, Lead Chords Bass Drums, Swing Tone) and its own sound
 * bank and habits: the lead's patches (verse, chorus), the duo's, the chords' (stabs, pads, arpeggio), the
 * bass's; the lead's register and motif families; what the duo and the chords do in each section (bits), their
 * rhythms; bass, DAC and hat patterns per section; intros and forms; the swing's unit, the echo's delay, the
 * vibrato on long notes, bends, the arpeggio's speed, the push before the bar (0..3), the key change (semitones),
 * the orchestra hit; the title words */
enum { R2_ECHO = 1, R2_HARM = 2, R2_OCT = 4, R2_ARP = 8, R2_CTR = 16 };
enum { CR_STAB = 1, CR_PAD = 2, CR_ARP = 4 };
enum { IN_GROOVE = 1, IN_ARP = 2, IN_FANFARE = 4, IN_DRUMS = 8 };
enum { FM_FULL = 1, FM_DOUBLE = 2, FM_SHORT = 4 };
static const char *const FORMS[3] = {"ABABCB", "AABCB", "ABCB"};
typedef struct {
    const char *name;
    int16_t v[P_NSTYLE];
    uint8_t leadA, leadB, duo, stab, pad, arp, bass;
    uint8_t reg, motifs;
    uint8_t duo_r[3], chr[3];
    uint16_t stabr;
    uint8_t brk;
    uint16_t bassp[3], dac[3];
    uint8_t hats[3];
    uint8_t intro, form;
    uint8_t swing8, echo_d, vib, slides, arp_sp, antic, kc, hit;
    const char *adj, *noun;                      /* title words, "|" between */
} style_t;
#define B(x) (1u << (x))
static const style_t STYLES[EN_NSTYLES] = {
    /* a speed platformer's first zone: bright, a slap bass, stabs with the push */
    {"Loop De Loop", {150, 75, 35, 12, 75, 65, 75, 75, 0, 60}, PT_SYNLEAD, PT_BRASS, PT_BELL, PT_EP, PT_STRINGS,
     PT_BELL, PT_SLAP, 72, F_BOUNCE | F_HERO | F_FUNK, {R2_ECHO | R2_OCT, R2_HARM, R2_ARP}, {CR_STAB, CR_STAB, CR_PAD},
     B(0) | B(2) | B(7), 0x3, {B(1) | B(7), B(1) | B(0), B(4)}, {B(1), B(1) | B(0), B(3)}, {B(0), B(1), B(0)},
     IN_GROOVE | IN_FANFARE, FM_FULL, 0, 3, 0x52, 2, 1, 2, 0, 0,
     "EMERALD|TURQUOISE|SPRING|CHECKER|PALM|SPEEDWAY|SUNNY|CORAL", "COAST|HILL|RAMP|BOARDWALK|CLIFFS|CORKSCREW|SPRINGS"},
    /* the boss: harmonic minor, a driving synth bass, brass stabs, the orchestra hit */
    {"Weak Point Glow", {168, 90, 20, 94, 75, 70, 75, 80, 0, 62}, PT_SYNLEAD, PT_BRASS, PT_CLAV, PT_STAB, PT_STRINGS,
     PT_CLAV, PT_SYNBASS, 70, F_DRIVE | F_DARK, {R2_HARM | R2_ARP, R2_HARM, R2_ARP}, {CR_STAB, CR_STAB, CR_STAB},
     B(6) | B(2) | B(1), 0x1, {B(5) | B(2), B(5) | B(2), B(2)}, {B(9), B(9) | B(2), B(3)}, {B(1), B(1), B(0)},
     IN_DRUMS, FM_FULL, 0, 3, 0x43, 1, 1, 0, 0, 1,
     "MECHA|IRON|FINAL|DOOM|SPIKED|ROCKET|HOVER|DRILL", "EGG|TITAN|WALKER|TANK|HYDRA|SENTRY|CRUSHER|CORE"},
    /* a racer: rock, a guitar lead, organ, the driving roots */
    {"Pit Lane Duel", {160, 85, 25, 30, 75, 60, 75, 80, 0, 62}, PT_GUITAR, PT_GUITAR, PT_GUITAR, PT_ORGAN, PT_ORGAN,
     PT_BELL, PT_SYNBASS, 69, F_DRIVE | F_HERO, {R2_OCT, R2_OCT | R2_HARM, R2_CTR}, {CR_STAB, CR_PAD, CR_PAD},
     B(4) | B(6) | B(3), 0x1, {B(2) | B(0), B(2) | B(0), B(4)}, {B(1) | B(2), B(2) | B(0), B(3)}, {B(0), B(1), B(3)},
     IN_DRUMS | IN_GROOVE, FM_FULL, 0, 3, 0x53, 3, 1, 1, 0, 0,
     "TURBO|NITRO|GRID|APEX|HAIRPIN|SLICK|CHICANE|GEARBOX", "SPRINT|CIRCUIT|GRAND PRIX|RALLY|STRAIGHT|DERBY|TROPHY"},
    /* a shooter's long scroll: Dorian, arpeggios, a slap bass that never stops */
    {"Option Pods", {156, 85, 45, 62, 75, 65, 75, 75, 0, 64}, PT_SYNLEAD, PT_BRASS, PT_BELL, PT_STAB, PT_STRINGS,
     PT_BELL, PT_SLAP, 72, F_DRIVE | F_HERO | F_FUNK, {R2_ECHO, R2_ARP | R2_HARM, R2_ECHO}, {CR_STAB, CR_STAB, CR_ARP},
     B(2) | B(0) | B(3), 0x3, {B(1) | B(0), B(7) | B(1), B(6)}, {B(2), B(2) | B(9), B(1)}, {B(1), B(1), B(4)},
     IN_ARP | IN_DRUMS, FM_FULL, 0, 3, 0x42, 1, 1, 2, 0, 0,
     "ORBITAL|PLASMA|NEBULA|VECTOR|PHOTON|STARBOARD|SOLAR|AXIS", "FLEET|CORRIDOR|ARMADA|RING|BELT|CARRIER|GATE"},
    /* the options menu: electric piano sevenths, a finger bass, ghost notes */
    {"Sound Test 07", {104, 60, 30, 20, 70, 75, 75, 65, 25, 50}, PT_REED, PT_SYNLEAD, PT_STRINGS, PT_EP, PT_STRINGS,
     PT_MARIMBA, PT_FINGER, 69, F_LYRIC | F_FUNK, {R2_CTR, R2_HARM, R2_CTR}, {CR_STAB, CR_STAB, CR_PAD},
     B(7) | B(3) | B(8), 0x1, {B(8) | B(3), B(8) | B(6), B(4)}, {B(6), B(6), B(5)}, {B(0), B(0), B(3)},
     IN_ARP | IN_GROOVE, FM_DOUBLE, 0, 3, 0x52, 1, 1, 2, 0, 0,
     "OPTION|SELECT|STEREO|BGM|SE|VOICE|CONFIG|MONO", "MENU|SCREEN|TEST|CHANNEL|ROOM|LIST|NUMBER"},
    /* the credits: warm, a counter-line, the last chorus a whole step up */
    {"Good Ending", {112, 60, 35, 12, 70, 70, 70, 60, 0, 52}, PT_FLUTE, PT_BRASS, PT_STRINGS, PT_EP, PT_STRINGS,
     PT_HARP, PT_FINGER, 72, F_LYRIC | F_HERO, {R2_CTR, R2_HARM | R2_CTR, R2_CTR}, {CR_PAD | CR_ARP, CR_STAB, CR_PAD},
     B(7) | B(4) | B(3), 0x5, {B(3) | B(4), B(0) | B(3), B(4)}, {B(5), B(0), B(3)}, {B(3), B(0), B(3)},
     IN_ARP, FM_FULL, 0, 3, 0x52, 1, 1, 1, 2, 0,
     "THANK YOU|FAREWELL|HOMEWARD|AMBER|LAST|STARRY|WARM|FOREVER", "HEROES|FRIENDS|JOURNEY|CAPTAIN|ISLAND|PLANET|CREW"},
    /* an ice stage: bells, marimba, a gentle shuffle */
    {"Penguin Physics", {120, 55, 50, 22, 70, 65, 70, 60, 30, 50}, PT_BELL, PT_MARIMBA, PT_FLUTE, PT_EP, PT_STRINGS,
     PT_MARIMBA, PT_FINGER, 74, F_BOUNCE | F_LYRIC, {R2_ECHO, R2_ECHO | R2_CTR, R2_CTR}, {CR_ARP, CR_PAD | CR_ARP, CR_PAD},
     B(4) | B(5), 0x9, {B(3) | B(6), B(6) | B(0), B(4)}, {B(5), B(8) | B(0), B(5)}, {B(3), B(0), B(3)},
     IN_ARP, FM_DOUBLE, 1, 3, 0x42, 1, 1, 1, 0, 0,
     "FROZEN|ICY|POLAR|SLIPPERY|GLACIAL|SNOWBALL|IGLOO|AURORA", "SLOPE|CAVERN|LAKE|PEAKS|FLOE|RINK|SUMMIT"},
    /* a street brawler: Dorian house, organ stabs on the offbeat, octave bass, claps */
    {"Pipe Wrench", {126, 80, 45, 66, 75, 70, 80, 80, 0, 60}, PT_SYNLEAD, PT_SYNLEAD, PT_STRINGS, PT_ORGAN, PT_STRINGS,
     PT_EP, PT_SYNBASS, 71, F_FUNK | F_DARK | F_DRIVE, {R2_ECHO, R2_ECHO | R2_OCT, R2_CTR}, {CR_STAB, CR_STAB, CR_PAD},
     B(0) | B(7), 0x3, {B(0) | B(7), B(0) | B(7), B(4)}, {B(4), B(4), B(3)}, {B(2) | B(5), B(5), B(2)},
     IN_DRUMS | IN_GROOVE, FM_FULL, 0, 3, 0x42, 1, 1, 1, 0, 0,
     "DOWNTOWN|RUSTY|BACK|CROWBAR|STEEL|UPTOWN|DOCKSIDE|BRICK", "ALLEY|ROOFTOP|FACTORY|ELEVATOR|ARCADE|SUBWAY|PIER"},
    /* a role-playing game's town: a flute, a harp's broken chords, sparse drums, a lilt */
    {"Inn Is 10 Gold", {94, 45, 30, 22, 70, 70, 70, 50, 40, 46}, PT_FLUTE, PT_FLUTE, PT_STRINGS, PT_HARP, PT_STRINGS,
     PT_HARP, PT_FINGER, 73, F_LYRIC | F_BOUNCE, {R2_CTR, R2_HARM, R2_CTR}, {CR_ARP, CR_ARP | CR_PAD, CR_PAD},
     B(5), 0x5, {B(4) | B(3), B(3), B(4)}, {B(5), B(5) | B(8), B(5)}, {B(3), B(3), B(3)},
     IN_ARP, FM_DOUBLE, 1, 3, 0x52, 1, 2, 0, 0, 0,
     "WINDMILL|MOSSY|LITTLE|HARVEST|STONE|SLEEPY|MILL|WILLOW", "VILLAGE|HAMLET|WELL|BAKERY|SQUARE|FERRY|TOWN"},
    /* a castle dungeon: harmonic minor, harpsichord, organ, a march */
    {"Spike Ceiling", {104, 60, 40, 98, 70, 70, 75, 60, 0, 48}, PT_BRASS, PT_ORGAN, PT_CLAV, PT_CLAV, PT_ORGAN,
     PT_CLAV, PT_SYNBASS, 69, F_DARK | F_LYRIC, {R2_CTR, R2_HARM, R2_ARP}, {CR_ARP, CR_PAD | CR_ARP, CR_PAD},
     B(4) | B(5), 0x9, {B(3) | B(4), B(3), B(4)}, {B(7) | B(3), B(7), B(3)}, {B(3), B(0), B(3)},
     IN_ARP, FM_DOUBLE, 0, 3, 0x63, 1, 2, 0, 0, 0,
     "CURSED|SUNKEN|TORCHLIT|IRON|BONE|CRUMBLING|GRIM|ASHEN", "DUNGEON|KEEP|CRYPT|VAULT|BELFRY|CATACOMB|DRAWBRIDGE"},
    /* a fighting game's bonus round: funk, a slap bass, a reed, comping */
    {"Bonus Car Smash", {128, 80, 30, 58, 75, 70, 80, 75, 20, 58}, PT_REED, PT_BRASS, PT_BRASS, PT_EP, PT_STRINGS,
     PT_MARIMBA, PT_SLAP, 68, F_FUNK | F_HERO, {R2_OCT, R2_HARM, R2_CTR}, {CR_STAB, CR_STAB, CR_PAD},
     B(7) | B(2) | B(8), 0x1, {B(1) | B(8), B(7) | B(1), B(4)}, {B(6), B(6) | B(1), B(3)}, {B(0), B(4), B(0)},
     IN_GROOVE, FM_FULL, 0, 3, 0x52, 2, 1, 3, 0, 0,
     "ROUND|PERFECT|BONUS|SUPER|COMBO|HYPER|SPECIAL|TIME", "BREAK|STAGE|MATCH|FIGHT|DOJO|BOUT|KNOCKOUT"},
    /* the title screen: brass fanfares, a march, the hit, the last chorus a step up */
    {"Blast Processing", {132, 80, 25, 8, 75, 70, 75, 75, 0, 60}, PT_BRASS, PT_BRASS, PT_BRASS, PT_STAB, PT_STRINGS,
     PT_BELL, PT_SLAP, 70, F_HERO | F_BOUNCE, {R2_HARM, R2_HARM, R2_CTR}, {CR_STAB, CR_STAB, CR_PAD},
     B(1) | B(3) | B(8), 0x1, {B(0) | B(5), B(0) | B(5), B(4)}, {B(7), B(7) | B(0), B(3)}, {B(0), B(0), B(3)},
     IN_FANFARE, FM_FULL, 0, 3, 0x52, 1, 1, 1, 1, 1,
     "PRESS|LEGEND|MIGHTY|HYPER|SUPER|SAGA|GOLDEN|ETERNAL", "START|CHAMPIONS|WARRIORS|QUEST|EMPIRE|KINGDOM|BLADE"},
    /* the last stage: minor, strings over a galloping bass, the hit */
    {"Last Continue", {148, 85, 30, 80, 75, 70, 75, 80, 0, 60}, PT_GUITAR, PT_SYNLEAD, PT_BRASS, PT_STAB, PT_STRINGS,
     PT_CLAV, PT_SYNBASS, 69, F_DRIVE | F_DARK | F_HERO, {R2_HARM, R2_OCT | R2_HARM, R2_CTR}, {CR_PAD, CR_STAB, CR_ARP},
     B(2) | B(3) | B(5), 0x2, {B(5) | B(2), B(5) | B(0), B(4)}, {B(2), B(9) | B(2), B(3)}, {B(0), B(1), B(0)},
     IN_DRUMS | IN_FANFARE, FM_FULL, 0, 3, 0x53, 1, 1, 1, 0, 1,
     "LAST|ZERO|SHATTERED|BURNING|SILENT|HOLLOW|ENDLESS|FALLEN", "FORTRESS|HORIZON|STARSHIP|CITADEL|EDEN|THRONE|TOWER"},
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
    int ctr_last;
    int vc[3];                                   /* the chords' last voicing */
} G;

static int pcm(int p) { return ((p % 12) + 12) % 12; }
static int chord_has(uint8_t c, int p)           /* a tone of the chord's triad */
{
    int r = c & 15, q = CQ(c), i, pc = pcm(p);
    for (i = 0; i < 3; i++)
        if ((r + Q_T[q][i]) % 12 == pc)
            return 1;
    return 0;
}
static int chord_has_all(uint8_t c, int p)       /* any tone of the chord, the seventh too */
{
    int r = c & 15, q = CQ(c), i, pc = pcm(p);
    for (i = 0; i < Q_N[q]; i++)
        if ((r + Q_T[q][i]) % 12 == pc)
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
/* the scale over a chord: the mode's, with each chord tone it lacks in place of a neighbour (the altered degree:
 * the third, sixth or seventh first, never the tonic or the fifth) */
static void chord_scale(uint8_t c, int *pcs)
{
    int i, j, r = c & 15, q = CQ(c);
    for (i = 0; i < 7; i++)
        pcs[i] = (G.key + MODE_PCS[G.mode][i]) % 12;
    for (j = 0; j < Q_N[q]; j++) {
        int t = (r + Q_T[q][j]) % 12, have = 0, best = -1, bs = -1;
        for (i = 0; i < 7; i++)
            have |= pcs[i] == t;
        if (have)
            continue;
        for (i = 0; i < 7; i++) {
            int rel = pcm(pcs[i] - G.key), s;
            if ((pcs[i] + 1) % 12 != t && (pcs[i] + 11) % 12 != t)
                continue;
            if (chord_has_all(c, pcs[i]))
                continue;
            s = (rel == 0 || rel == 7) ? 0 : (rel == 3 || rel == 4 || rel == 8 || rel == 9 || rel == 10 || rel == 11) ? 2 : 1;
            if (s > bs)
                bs = s, best = i;
        }
        if (best >= 0)
            pcs[best] = t;
    }
}
static int in_scale(const int *pcs, int p)
{
    int i, pc = pcm(p);
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
        }
        while (*s && *s != ' ')
            s++;
    }
}
/* a string of 16 one-character steps */
static void steps16(const char *s, char *t)
{
    int i;
    for (i = 0; i < 16; i++) {
        while (*s == ' ')
            s++;
        t[i] = *s ? *s : '.';
        if (*s)
            s++;
    }
}

/* the lead's bar: tokens against a chord into notes (n[s]: a MIDI note, 0 nothing new, NOTE_REL a release);
 * snd[s]: the note sounding at each step (0 none), for the parts that keep out of its way */
typedef struct {
    uint8_t n[16];
    uint8_t len[16];
    uint8_t snd[16];
} bar_t;
static void realize(const tok_t *t, uint8_t c, int anchor, int target, bar_t *b, int lo, int hi)
{
    int s, prev = G.lead_last, sounding = 0;
    for (s = 0; s < 16; s++) {
        int p, len, k;
        b->n[s] = 0;
        b->len[s] = 0;
        b->snd[s] = (uint8_t)(sounding ? prev : 0);
        if (t[s].k == TK_HOLD)
            continue;
        if (t[s].k == TK_REST) {
            if (sounding)
                b->n[s] = NOTE_REL;
            sounding = 0;
            b->snd[s] = 0;
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
        b->snd[s] = (uint8_t)p;
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
static void cellv(int pat, int row, int ch, int note, int ins, int fx, int fxp, int lay, int vol)
{
    en_cell_t *c = &S->pat[pat][row][ch];
    c->note = (uint8_t)note;
    c->ins = (uint8_t)ins;
    c->fx = (uint8_t)fx;
    c->fxp = (uint8_t)fxp;
    c->lay = (uint8_t)lay;
    c->vol = (uint8_t)vol;
}
static void cell(int pat, int row, int ch, int note, int ins, int fx, int fxp, int lay)
{
    cellv(pat, row, ch, note, ins, fx, fxp, lay, 0);
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
static int lead_low(const bar_t *b, int s0, int s1)   /* the lowest lead note sounding in steps s0..s1-1 (127 none) */
{
    int s, m = 127;
    for (s = s0; s < s1; s++)
        if (b->snd[s] && b->snd[s] < m)
            m = b->snd[s];
    return m;
}
static int clashes(const bar_t *b, int s0, int s1, int v)   /* a minor second (or ninth, or major seventh) on the lead */
{
    int s, n = 0;
    for (s = s0; s < s1; s++) {
        int d = b->snd[s] - v;
        if (!b->snd[s])
            continue;
        d = d < 0 ? -d : d;
        n += d == 1 || d == 11 || d == 13;
    }
    return n;
}

/* the lead's bar into a pattern (at row r0), with its effects: vibrato on long notes, now and then a bend up into
 * a note (the note a semitone or two under it, slid up: "Q") */
static void put_lead(int pat, int r0, const bar_t *b, int ins)
{
    int s;
    for (s = 0; s < 16; s++) {
        int n = b->n[s], fx = 0, fxp = 0;
        if (!n)
            continue;
        if (n == NOTE_REL) {
            cell(pat, r0 + s, CH_LEAD, NOTE_REL, 0xFF, 0, 0, 0);
            continue;
        }
        if (b->len[s] >= 6 && G.st->vib) {
            fx = '4';
            fxp = G.st->vib;
        } else if (G.st->slides && b->len[s] >= 3 && gi(12) < G.st->slides) {
            int up = 1 + gi(2);
            fx = 'Q';
            fxp = 0x60 | up;
            n -= up;
        }
        cellv(pat, r0 + s, CH_LEAD, n, ins, fx, fxp, 0, (s & 3) == 0 ? 15 : 13);
    }
}

/* ---- the duo (FM2), a bar of it */
static int duo_root(uint8_t c)                   /* the chord's root in the arpeggio's register (57..68) */
{
    int p = 57;
    while ((p % 12) != (c & 15))
        p++;
    return p;
}
static void put_arp(int pat, int r0, uint8_t c, int rh)
{
    char t[16];
    int i, q = CQ(c), x = Q_T[q][1], y = Q_T[q][2];
    steps16(ARPR[rh], t);
    for (i = 0; i < 16; i++)
        if (t[i] == 'x')
            cellv(pat, r0 + i, CH_DUO, duo_root(c), G.st->duo, '0', x << 4 | y, (i & 7) == 0 ? 2 : (i & 1) ? 4 : 3, 11);
}
/* the countermelody: guide tones (the third or the seventh), voice-led, under the lead and clear of it */
static void put_ctr(int pat, int r0, uint8_t c, const bar_t *b, int halves)
{
    int h;
    for (h = 0; h < (halves ? 2 : 1); h++) {
        int s0 = h * 8, s1 = halves ? s0 + 8 : 16, best = -1, bs = 9999, p, lm = lead_low(b, s0, s1);
        int r = c & 15, q = CQ(c);
        for (p = 58; p <= 72; p++) {
            int sc, d, pc = pcm(p - r);
            if (!chord_has_all(c, p) || pc == 0)
                continue;
            d = p - G.ctr_last;
            d = d < 0 ? -d : d;
            sc = d * 2 + clashes(b, s0, s1, p) * 30 + (p > lm - 3 ? (p - lm + 3) * 8 : 0);
            sc += (pc == Q_T[q][1] || (Q_N[q] == 4 && pc == Q_T[q][3])) ? 0 : 3;   /* the third or the seventh */
            if (sc < bs)
                bs = sc, best = p;
        }
        if (best < 0)
            continue;
        G.ctr_last = best;
        cellv(pat, r0 + s0, CH_DUO, best, G.st->duo, '4', 0x32, 2, 11);
    }
}
/* harmony: a third under the lead (a chord tone under it on chord tones), its rhythm, the lead's patch */
static void put_harm(int pat, int r0, const bar_t *b, uint8_t c, int ins)
{
    int s;
    for (s = 0; s < 16; s++) {
        int n = b->n[s], h;
        if (!n)
            continue;
        if (n == NOTE_REL) {
            cell(pat, r0 + s, CH_DUO, NOTE_REL, 0xFF, 0, 0, 2);
            continue;
        }
        if (chord_has(c, n))
            h = chord_has(c, n - 3) ? n - 3 : chord_tone_at(c, n - 3, -1);
        else
            h = scale_step(c, n, -2);
        while (h < 55)
            h += 12;
        if (h >= n)
            continue;
        cellv(pat, r0 + s, CH_DUO, h, ins, 0, 0, 2, 11);
    }
}
/* the lead an octave down, the same patch a little detuned by its own operators */
static void put_oct(int pat, int r0, const bar_t *b, int ins)
{
    int s;
    for (s = 0; s < 16; s++) {
        const en_cell_t *l = &S->pat[pat][r0 + s][CH_LEAD];
        if (!l->note)
            continue;
        if (l->note == NOTE_REL)
            cell(pat, r0 + s, CH_DUO, NOTE_REL, 0xFF, 0, 0, 2);
        else
            cellv(pat, r0 + s, CH_DUO, l->note - 12 >= 48 ? l->note - 12 : l->note, ins, l->fx, l->fxp, 2, 12);
    }
    (void)b;
}
/* the echo: the lead's cells of this pattern, d rows later, quieter (the Echo knob plays them) */
static void put_echo(int pat, int d)
{
    int r, n = S->plen[pat];
    for (r = n - 1 - d; r >= 0; r--) {
        en_cell_t *l = &S->pat[pat][r][CH_LEAD];
        if (!l->note || S->pat[pat][r + d][CH_DUO].note)
            continue;
        cellv(pat, r + d, CH_DUO, l->note, l->note == NOTE_REL ? 0xFF : l->ins, l->fx == '4' ? '4' : 0, l->fxp,
              LAY_ECHO | 1, 0);
    }
}

/* ---- the chords (FM3-5): three voices, voice-led */
static void voicing(uint8_t c, const bar_t *b, int s0, int s1, int *v)
{
    int pcs[3], np = 3, st, best = 9999, bv[3] = {60, 64, 67}, r = c & 15, q = CQ(c), i, set;
    int lm = b ? lead_low(b, s0, s1) : 127;
    /* a seventh: the root left to the bass (third, fifth, seventh); where the seventh would rub on the lead, the
     * plain triad */
    for (set = Q_N[q] == 4 ? 0 : 1; set < 2; set++) {
        for (i = 0; i < 3; i++)
            pcs[i] = (r + Q_T[q][set ? i : i + 1]) % 12;
        for (st = 50; st <= 68; st++) {
            int w[3], k, sc = set && Q_N[q] == 4 ? 6 : 0, ok = 0;
            for (i = 0; i < np; i++)
                ok |= pcm(st) == pcs[i];
            if (!ok)
                continue;
            w[0] = st;
            for (k = 1; k < 3; k++) {
                int p = w[k - 1] + 1, f = 0;
                for (; !f; p++)
                    for (i = 0; i < np; i++)
                        if (pcm(p) == pcs[i])
                            f = 1, w[k] = p;
            }
            for (k = 0; k < 3; k++) {
                int d = w[k] - G.vc[k];
                sc += d < 0 ? -d : d;
                if (b)
                    sc += clashes(b, s0, s1, w[k]) * 60;
            }
            if (w[2] > lm - 1)
                sc += (w[2] - lm + 1) * 6;
            if (w[2] > 72)
                sc += (w[2] - 72) * 4;
            if (w[0] < 52)
                sc += (52 - w[0]) * 4;
            if (sc < best) {
                best = sc;
                for (k = 0; k < 3; k++)
                    bv[k] = w[k];
            }
        }
    }
    for (i = 0; i < 3; i++)
        v[i] = G.vc[i] = bv[i];
}
/* the push: the next chord a sixteenth before the bar (stabs and bass), when the lead sits in it there */
static int can_push(const bar_t *b, uint8_t c, uint8_t nx)
{
    int s;
    if (nx == c || nx == 0xFFu || !G.st->antic || gi(4) >= G.st->antic)
        return 0;
    for (s = 14; s < 16; s++)
        if (b->snd[s] && !chord_has_all(nx, b->snd[s]))
            return 0;
    return 1;
}
static void put_chords(int pat, int r0, uint8_t c, uint8_t nx, int role, int rh, int brk, const bar_t *b, int push)
{
    int v[3], k, i, on = 0;
    char t[16];
    if (role == CR_ARP) {                        /* a broken chord on FM3; the other two hold it, softly, when it's full */
        int tones[4];
        voicing(c, b, 0, 16, v);
        tones[0] = v[0], tones[1] = v[1], tones[2] = v[2], tones[3] = v[0] + 12;
        steps16(BROKEN[brk], t);
        for (i = 0; i < 16; i++)
            if (t[i] >= '0' && t[i] <= '3')
                cellv(pat, r0 + i, CH_C1, tones[t[i] - '0'], G.st->arp, 0, 0, (i & 3) == 0 ? 1 : 2, (i & 3) == 0 ? 13 : 10);
        cellv(pat, r0, CH_C2, v[0], G.st->pad, 0, 0, 3, 9);
        cellv(pat, r0, CH_C3, v[1], G.st->pad, 0, 0, 3, 9);
        cell(pat, r0 + 15, CH_C2, NOTE_REL, 0xFF, 0, 0, 3);
        cell(pat, r0 + 15, CH_C3, NOTE_REL, 0xFF, 0, 0, 3);
        return;
    }
    steps16(role == CR_PAD ? STABR[5] : STABR[rh], t);
    if (push) {                                  /* the push: a hit at step 14 on the next chord, held into the bar */
        t[13] = '.';
        t[14] = 'x';
        t[15] = '-';
    }
    voicing(c, b, 0, push ? 14 : 16, v);
    for (i = 0; i < 16; i++) {
        if (t[i] == 'x') {
            int pv[3], *w = v, lay, vol;
            if (push && i == 14) {
                voicing(nx, b, 14, 16, pv);
                w = pv;
            }
            lay = role == CR_PAD ? 1 : (i & 3) == 0 ? 1 : 2;
            vol = role == CR_PAD ? 12 : (i == 0 || i == 14) ? 15 : 12;
            for (k = 0; k < 3; k++)
                cellv(pat, r0 + i, CH_C1 + k, w[k], role == CR_PAD ? G.st->pad : G.st->stab, 0, 0, lay + (k == 2), vol);
            on = 1;
        } else if (t[i] == '.' && on) {
            for (k = 0; k < 3; k++)
                cell(pat, r0 + i, CH_C1 + k, NOTE_REL, 0xFF, 0, 0, 1);
            on = 0;
        }
    }
}

/* ---- the bass (FM6), a bar */
static int bass_root(int pc) { return 31 + pcm(pc - 7); }   /* G1 .. F#2 */
static void put_bass(int pat, int r0, uint8_t c, uint8_t next, int bp, int push)
{
    char t[16];
    int i, root = bass_root(c & 15), nroot = bass_root(next & 15), q = CQ(c);
    steps16(BASS[bp], t);
    if (push) {
        t[14] = 'N';
        t[15] = '-';
    }
    for (i = 0; i < 16; i++) {
        int p = -1, lay, vol = 13;
        char k = t[i];
        switch (k) {
        case 'R': p = root; break;
        case 'r': p = root, vol = 7; break;
        case 'O': p = root + 12; break;
        case 'o': p = root + 12, vol = 7; break;
        case 'F': p = root + Q_T[q][2]; break;
        case 'T': p = root + Q_T[q][1]; break;
        case 'N': p = nroot, vol = 15; break;
        case 'A':
            if ((next & 15) == (c & 15))
                p = root + Q_T[q][2];
            else {
                p = scale_step(next, nroot, -1);
                if (p == root)
                    p = scale_step(next, nroot, 1);
            }
            break;
        case '.': cell(pat, r0 + i, CH_BASS, NOTE_REL, 0xFF, 0, 0, i == 0 ? 0 : 1); break;
        default: break;
        }
        if (p < 0)
            continue;
        if (i == 0)
            vol = 15;
        lay = i == 0 ? 0 : vol < 10 ? 3 : (k == 'O' || (i & 1)) ? 2 : 1;
        cellv(pat, r0 + i, CH_BASS, p, G.st->bass, 0, 0, lay, vol);
    }
}

/* ---- the drums: the DAC and the hats, a bar (fill: the last four steps a fill) */
static void dac_hit(int pat, int r, int d, int note, int lay, int vol)
{
    cellv(pat, r, CH_DAC, note, d, 0, 0, lay, vol);
}
static void put_drums(int pat, int r0, int dp, int hp, int fill, int crash, int hit, uint8_t c)
{
    char t[16], h[16];
    int i;
    steps16(DAC[dp], t);
    steps16(HATS[hp], h);
    if (fill >= 0) {
        char f[16];
        steps16(FILLS[fill], f);
        for (i = 12; i < 16; i++)
            t[i] = f[i - 12], h[i] = '.';
    }
    if (crash)
        t[0] = hit ? 'H' : 'C';
    for (i = 0; i < 16; i++) {
        int r = r0 + i;
        switch (t[i]) {
        case 'K': dac_hit(pat, r, DR_KICK, 60, 1, 15); break;
        case 'S': dac_hit(pat, r, DR_SNARE, 60, 1, (i & 3) == 0 ? 15 : 13); break;
        case 'g': dac_hit(pat, r, DR_SNARE, 60, 3, 6); break;
        case 'T': dac_hit(pat, r, DR_TOMH, 60, 2, 14); break;
        case 't': dac_hit(pat, r, DR_TOML, 60, 2, 14); break;
        case 'P': dac_hit(pat, r, DR_CLAP, 60, 1, 14); break;
        case 'r': dac_hit(pat, r, DR_RIM, 60, 1, 11); break;
        case 'C': dac_hit(pat, r, DR_CRASH, 60, 1, 14); break;
        case 'H': dac_hit(pat, r, DR_HIT, 55 + pcm((c & 15) - 7), 0, 14); break;
        default: break;
        }
        if (h[i] == 'h')
            cellv(pat, r, CH_PSG, 1, HT_CLOSED, 0, 0, (i & 1) ? 3 : 2, (i & 3) == 2 ? 12 : 9);
        else if (h[i] == 'o')
            cellv(pat, r, CH_PSG, 1, HT_OPEN, 0, 0, 2, 11);
    }
}

/* a section: verse (0), chorus (1) or bridge (2), into patterns p, p+1 (and p+2) */
typedef struct {
    int m1, m2;                                  /* motifs */
    int v1, v2;                                  /* their variations */
    int e_half, e_full;                          /* the endings */
    int duo, chr, stabr, brk, arpr, bass, dac, hats, lead;
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

static void put_duo(int pat, int r0, int role, uint8_t c, const bar_t *b, const plan_t *pl)
{
    switch (role) {
    case R2_ARP: put_arp(pat, r0, c, pl->arpr); break;
    case R2_HARM: put_harm(pat, r0, b, c, pl->lead); break;
    case R2_OCT: put_oct(pat, r0, b, pl->lead); break;
    case R2_CTR: put_ctr(pat, r0, c, b, pl->stabr & 1); break;
    default: break;
    }
}

static void make_section(int sec, int p0, const plan_t *pl, uint8_t next_chord)
{
    static const int8_t BARS[2][3][2] = {{{0, 1}, {2, 3}, {6, 7}}, {{0, 1}, {2, 3}, {-1, -1}}};
    int np = sec == 2 ? 2 : 3, k, j;
    for (k = 0; k < np; k++) {
        int pat = p0 + k;
        clear_pat(pat, 32);
        for (j = 0; j < 2; j++) {
            int bar = BARS[sec == 2][k][j], r0 = j * 16, fill = -1, push;
            uint8_t c = G.ch[sec][bar & 3], nx;
            bar_t b;
            nx = (bar & 3) == 3 ? ((bar == 7 || sec == 2) ? next_chord : G.ch[sec][0]) : G.ch[sec][(bar + 1) & 3];
            S->pchord[pat][j] = c;
            if (bar == 7 || (sec == 2 && bar == 3))
                fill = gi(NA(FILLS));
            bar_lead(sec, bar, pl, c, &b);
            put_lead(pat, r0, &b, pl->lead);
            push = pl->chr == CR_STAB && can_push(&b, c, nx);
            put_drums(pat, r0, pl->dac, pl->hats, fill, bar == 0 && sec == 1, G.st->hit && bar == 0 && sec == 1, c);
            if (bar == 3 && sec != 2 && fill < 0) {      /* the half-way: a short pickup on the snare */
                cellv(pat, r0 + 14, CH_DAC, 60, DR_SNARE, 0, 0, 2, 11);
                cellv(pat, r0 + 15, CH_DAC, 60, DR_SNARE, 0, 0, 2, 13);
            }
            put_bass(pat, r0, c, nx, pl->bass, push);
            put_chords(pat, r0, c, nx, pl->chr, pl->stabr, pl->brk, &b, push);
            put_duo(pat, r0, pl->duo, c, &b, pl);
        }
        if (pl->duo == R2_ECHO)
            put_echo(pat, G.st->echo_d);
    }
}

/* the intro: a bar or two before the verse */
static void make_intro(int pat, int kind, const plan_t *pv)
{
    static const uint8_t TWO[MD_N] = {7, 10, 5, 10, 7};   /* the chord the intro turns to: V, bVII, IV, bVII, V */
    uint8_t home = CH(G.key, (G.mode <= MD_MIXO) ? QM : Qm);
    uint8_t two = CH((G.key + TWO[G.mode]) % 12, QM);
    int j, v[3];
    if (kind == IN_FANFARE || kind == IN_DRUMS) {
        clear_pat(pat, 16);
        S->pchord[pat][0] = home;
        if (kind == IN_FANFARE) {                /* hits on the home chord: the lead, a third under it, the chords */
            static const int8_t HIT[16] = {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 0};
            int top = chord_tone_at(home, G.anchor[0], 1), i, k;
            voicing(home, 0, 0, 0, v);
            for (i = 0; i < 16; i++) {
                if (HIT[i] > 0) {
                    cellv(pat, i, CH_LEAD, top, G.st->leadA, 0, 0, 0, 15);
                    cellv(pat, i, CH_DUO, chord_tone_at(home, top - 1, -1), G.st->leadA, 0, 0, 1, 12);
                    for (k = 0; k < 3; k++)
                        cellv(pat, i, CH_C1 + k, v[k], G.st->stab, 0, 0, 1, 15);
                    cellv(pat, i, CH_BASS, bass_root(G.key), G.st->bass, 0, 0, 0, 15);
                    if (G.st->hit)
                        dac_hit(pat, i, DR_HIT, 55 + pcm(G.key - 7), 0, 15);
                    else
                        dac_hit(pat, i, i ? DR_KICK : DR_CRASH, 60, 1, 15);
                } else if (HIT[i] < 0) {
                    for (k = 0; k < 6; k++)
                        cell(pat, i, CH_LEAD + k, NOTE_REL, 0xFF, 0, 0, 0);
                }
            }
            dac_hit(pat, 12, DR_TOMH, 60, 1, 14);
            dac_hit(pat, 13, DR_TOMH, 60, 1, 13);
            dac_hit(pat, 14, DR_TOML, 60, 1, 14);
            dac_hit(pat, 15, DR_TOML, 60, 1, 15);
        } else {                                 /* drums and the bass on the home chord, a fill */
            put_drums(pat, 0, pv->dac, pv->hats, gi(NA(FILLS)), 0, 0, home);
            put_bass(pat, 0, home, G.ch[0][0], pv->bass, 0);
        }
        return;
    }
    clear_pat(pat, 32);
    for (j = 0; j < 2; j++) {
        uint8_t c = j ? two : home, nx = j ? G.ch[0][0] : two;
        S->pchord[pat][j] = c;
        if (kind == IN_GROOVE) {
            put_drums(pat, j * 16, pv->dac, pv->hats, j ? gi(NA(FILLS)) : -1, j == 0, 0, c);
            put_bass(pat, j * 16, c, nx, pv->bass, 0);
            put_chords(pat, j * 16, c, nx, pv->chr == CR_PAD ? CR_STAB : pv->chr, pv->stabr, pv->brk, 0, 0);
        } else {                                 /* the chords broken, the bass under them, a soft pickup */
            put_chords(pat, j * 16, c, nx, CR_ARP, 0, gbit(G.st->brk), 0, 0);
            put_bass(pat, j * 16, c, nx, 4, 0);
            if (j) {
                cellv(pat, 28, CH_PSG, 1, HT_CLOSED, 0, 0, 1, 9);
                cellv(pat, 30, CH_PSG, 1, HT_CLOSED, 0, 0, 1, 11);
            }
        }
    }
}

/* the ending jingle: bVI bVII I (the stage cleared) or IV V I, the lead running up the chords onto home */
static void make_jingle(int pat)
{
    int run = G.mode == MD_MAJOR && gi(2) ? 1 : 0, j, i, k, v[3];
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
                cellv(pat, r, CH_LEAD, p, G.st->leadB, 0, 0, 0, 14);
                cellv(pat, r, CH_DUO, chord_tone_at(c, p - 1, -1), G.st->leadB, 0, 0, 2, 11);
            }
        }
        voicing(c, 0, 0, 0, v);
        for (k = 0; k < 3; k++)
            cellv(pat, j * 8, CH_C1 + k, v[k], G.st->stab, 0, 0, 1, 14);
        cellv(pat, j * 8, CH_BASS, bass_root(c & 15), G.st->bass, 0, 0, 0, 15);
        cellv(pat, j * 8 + 4, CH_BASS, bass_root(c & 15) + 12, G.st->bass, 0, 0, 1, 13);
        dac_hit(pat, j * 8, DR_KICK, 60, 1, 15);
        dac_hit(pat, j * 8 + 4, DR_SNARE, 60, 1, 14);
    }
    dac_hit(pat, 14, DR_TOMH, 60, 1, 14);
    dac_hit(pat, 15, DR_TOML, 60, 1, 15);
    top = anchor + 2;                            /* home: the tonic on top, a long note */
    while (top % 12 != G.key)
        top++;
    cellv(pat, 16, CH_LEAD, top, G.st->leadB, '4', 0x52, 0, 15);
    cellv(pat, 16, CH_DUO, chord_tone_at(home, top - 1, -1), G.st->leadB, 0, 0, 1, 12);
    voicing(home, 0, 0, 0, v);
    for (k = 0; k < 3; k++)
        cellv(pat, 16, CH_C1 + k, v[k], G.st->pad, 0, 0, 1, 14);
    cellv(pat, 16, CH_BASS, bass_root(G.key), G.st->bass, 0, 0, 0, 15);
    dac_hit(pat, 16, G.st->hit ? DR_HIT : DR_CRASH, G.st->hit ? 55 + pcm(G.key - 7) : 60, 0, 15);
    for (k = 0; k < 6; k++)
        cell(pat, 28, CH_LEAD + k, NOTE_REL, 0xFF, 0, 0, 0);
}

/* the key change: the chorus's patterns again, everything pitched a few semitones up */
static void make_lift(int from, int to, int kc)
{
    int r, k;
    for (r = 0; r < EN_ROWS; r++)
        for (k = 0; k < EN_NCH; k++) {
            en_cell_t e = S->pat[from][r][k];
            if (k < CH_DAC && e.note && e.note < NOTE_REL)
                e.note = (uint8_t)(e.note + kc);
            else if (k == CH_DAC && e.ins == DR_HIT && e.note)
                e.note = (uint8_t)(e.note + kc);
            S->pat[to][r][k] = e;
        }
    S->plen[to] = S->plen[from];
    for (r = 0; r < 2; r++)
        S->pchord[to][r] = S->pchord[from][r] == 0xFFu ? 0xFFu
                                                       : (uint8_t)((((S->pchord[from][r] & 15) + kc) % 12) |
                                                                   (S->pchord[from][r] & 0xF0u));
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
    while (*a && *a != '|' && e - b < 12)
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
        q->duo = 1 << gbit(G.st->duo_r[sec]);
        q->chr = 1 << gbit(G.st->chr[sec]);
        q->stabr = gbit(G.st->stabr);
        q->brk = gbit(G.st->brk);
        q->arpr = gi(NA(ARPR));
        q->bass = gbit(G.st->bassp[sec]);
        q->dac = gbit(G.st->dac[sec]);
        q->hats = gbit(G.st->hats[sec]);
        q->lead = sec == 1 ? G.st->leadB : G.st->leadA;
    }
    G.lead_last = G.anchor[0];
    G.ctr_last = 64;
    G.vc[0] = 55, G.vc[1] = 60, G.vc[2] = 64;
    /* the patterns: 00 the intro, the verse's three, the chorus's three, the bridge's two, the jingle, the lifted
     * chorus's three */
    intro = 1 << gbit(G.st->intro);
    make_intro(0, intro, &pl[0]);
    make_section(0, 1, &pl[0], G.ch[1][0]);
    make_section(1, 4, &pl[1], G.ch[0][0]);
    make_section(2, 7, &pl[2], G.ch[1][0]);
    s->kc = G.st->kc;
    if (s->kc) {
        for (i = 0; i < 3; i++)
            make_lift(4 + i, 10 + i, s->kc);
        G.key = (G.key + s->kc) % 12;            /* the jingle in the new key */
        G.anchor[0] += s->kc;
    }
    make_jingle(9);
    s->npat = s->kc ? 13 : 10;
    /* the order: the intro, then the form (each verse and chorus its first pattern twice: a period), the lift */
    form = FORMS[gbit(G.st->form)];
    s->order[o] = 0;
    s->osect[o++] = 'I';
    s->loop_to = (uint8_t)o;
    for (i = 0; form[i] && o < EN_NORD - 10; i++) {
        int sc = form[i] - 'A', b0 = 1 + sc * 3;
        static const int8_t SEQ[4] = {0, 1, 0, 2};
        for (p = 0; p < (sc == 2 ? 2 : 4); p++) {
            s->order[o] = (uint8_t)(b0 + (sc == 2 ? p : SEQ[p]));
            s->osect[o++] = (uint8_t)form[i];
        }
    }
    s->kc_at = (uint8_t)o;
    if (s->kc) {
        static const int8_t SEQ[4] = {0, 1, 0, 2};
        for (p = 0; p < 4; p++) {
            s->order[o] = (uint8_t)(10 + SEQ[p]);
            s->osect[o++] = 'K';
        }
    }
    s->order[o] = 9;
    s->osect[o++] = 'E';
    s->nord = (uint8_t)o;
}

int en_next_ord(const en_song_t *s, int ord, int loop, int loops, int repeat)
{
    if (ord + 1 < s->kc_at)
        return ord + 1;
    if (ord + 1 == s->kc_at)
        return (repeat || loop + 1 < loops) ? s->loop_to : ord + 1;
    return ord + 1 < s->nord ? ord + 1 : -1;
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_STYLE, C_PLAY, C_SKIP, C_REPEAT, C_SONG_NO, C_SEED_HI, C_SEED_LO, C_NOTE, C_PANIC, C_RETUNE, C_KPATCH };
#define QN_ 64u
static uint32_t q[QN_];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN_)
        return;
    q[w % QN_] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void en_set(int p, int v) { post(C_SET, p, v); }
void en_style(int i) { post(C_STYLE, i, 0); }
void en_play(int on) { post(C_PLAY, on, 0); }
void en_skip(void) { post(C_SKIP, 0, 0); }
void en_repeat(int on) { post(C_REPEAT, on, 0); }
void en_note(int note, int vel) { post(C_NOTE, note, vel); }
void en_keys_patch(int p) { post(C_KPATCH, p < 0 ? 255 : p, 0); }
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
volatile uint8_t en_playing, en_phase, en_repeat_on, en_ord, en_row, en_loop, en_loops = 2, en_bpm = 150, en_kick;
volatile uint32_t en_song_no, en_seed_base = 0x464D3532u, en_rows_played, en_songs_played;
volatile float en_vu;
volatile uint8_t en_ch_note[EN_NCH], en_ch_ins[EN_NCH], en_ch_lvl[EN_NCH];
volatile uint8_t en_op_lvl[EN_NFM][4], en_op_stage[EN_NFM][4];
volatile uint8_t en_drum_hits[EN_NDRUMS];

static int16_t par[P_NPARAMS];
static int style_i;
static int pord, prow;                           /* the next row to play (en_ord, en_row: the one playing) */

/* ------------------------------------------------------------- the FM --- */
#define FM_SR EN_SR
#define SUB 32                                   /* frames a control tick (0.73 ms) */
#define FSUB SUB
#define ENV_TOP 96.0f                            /* the envelope's floor, dB down */
static float SIN[1024];
static float rate_db[48];                        /* a decay rate (0..47) as dB a tick */
static float pnorm[PT_N];                        /* each patch's carriers summed to one */

typedef struct {
    uint32_t ph, inc;
    float g, dg;                                 /* the gain now, its step a sample (ramped to the tick's target) */
    float att;                                   /* the envelope, dB down */
    uint8_t st;                                  /* 0 off, 1 attack, 2 decay, 3 sustain decay, 4 release */
    float ka, d1, sl, d2, rr, tl;
    float mulr;
    uint8_t mute;                                /* over the FM's Nyquist */
} op_t;
typedef struct {
    op_t op[4];
    uint8_t alg, car, on, rel, ins, echo, vol;
    float fbk, fb1, fb2, extra;                  /* feedback; the carriers' extra attenuation (dB) */
    /* the tracker's side */
    float pitch, to, sp;
    uint8_t ax, ay, vs, vd;
    float vph;
    int fr;
    float hz;
} fmch_t;
#define NFMV 7                                   /* the six, and the keys played along */
static fmch_t fm[NFMV];

static float rate_of(int r) { return r <= 0 ? 0.0f : rate_db[r > 47 ? 47 : r]; }
static void fm_tables(void)
{
    int i, p, k;
    for (i = 0; i < 1024; i++)
        SIN[i] = fm_sinf((float)i * (FM_TWO_PI / 1024.0f));
    for (i = 0; i < 48; i++) {                   /* 96 dB in 12 s * 2^(-R/2.4): R 31 ~1.6 ms, R 10 ~0.7 s */
        float T = 12.0f * fm_exp2f(-(float)i / 2.4f);
        rate_db[i] = i ? ENV_TOP / T * ((float)SUB / EN_SR) : 0.0f;
    }
    for (p = 0; p < PT_N; p++) {
        float s = 0.0f;
        for (k = 0; k < 4; k++)
            if (ALG_CAR[PATCHES[p].alg] >> k & 1)
                s += fm_exp2f(-(float)PATCHES[p].op[k].tl * 0.75f / 6.0206f);
        pnorm[p] = 1.0f / (s > 0.2f ? s : 0.2f);
    }
}
static void fm_pitch(fmch_t *c, float semis)     /* every operator's step for a pitch */
{
    float hz = 440.0f * fm_exp2f((semis - 69.0f) * (1.0f / 12.0f));
    int k;
    c->hz = hz;
    for (k = 0; k < 4; k++) {
        float f = hz * c->op[k].mulr;
        c->op[k].mute = f > 20000.0f;
        c->op[k].inc = c->op[k].mute ? 0u : (uint32_t)(f * (4294967296.0f / FM_SR));
    }
}
static void fm_key_on(fmch_t *c, int ins, int note, int vol, int echo)
{
    const en_patch_t *pt = &PATCHES[ins < PT_N ? ins : 0];
    int k, quiet = 1;
    float kn = (float)(note - 48) * (1.0f / 12.0f);
    for (k = 0; k < 4; k++)
        if ((ALG_CAR[c->alg] >> k & 1) && c->op[k].st && c->op[k].att < 48.0f)
            quiet = 0;
    c->ins = (uint8_t)ins;
    c->alg = pt->alg;
    c->car = ALG_CAR[pt->alg];
    c->fbk = pt->fb ? (float)(1u << (pt->fb + 13)) : 0.0f;
    c->on = 1;
    c->rel = 0;
    c->echo = (uint8_t)echo;
    c->vol = (uint8_t)(vol ? vol : 15);
    c->extra = (float)(15 - c->vol) * 1.6f;
    if (echo)
        c->extra += 7.0f + (float)(100 - par[P_ECHO]) * 0.15f;
    for (k = 0; k < 4; k++) {
        const en_op_t *o = &pt->op[k];
        op_t *p = &c->op[k];
        int ks = (int)((float)o->ks * kn * 1.2f), ar = o->ar + ks;
        float T;
        p->mulr = (o->mul ? (float)o->mul : 0.5f) * (1.0f + (float)o->dt * 0.0012f);
        p->tl = (float)o->tl * 0.75f;
        p->sl = o->sl >= 15 ? ENV_TOP : (float)o->sl * 3.0f;
        p->d1 = rate_of(o->dr ? o->dr + ks : 0);
        p->d2 = rate_of(o->sr ? o->sr + ks : 0);
        p->rr = rate_of(o->rr * 2 + 1 + ks);
        if (ar >= 31) {
            p->ka = 1.0f;
        } else {
            T = 12.0f * fm_exp2f(-(float)(ar < 1 ? 1 : ar) / 2.4f) * (1.0f / 36.0f);
            p->ka = 1.0f - fm_expf(-((float)SUB / EN_SR) / T);
        }
        if (quiet) {                             /* from silence: the phases from zero, as a key on does */
            p->ph = 0u;
            p->att = ENV_TOP;
            p->g = 0.0f;
            p->dg = 0.0f;
        }
        p->st = 1;
    }
    if (quiet)
        c->fb1 = c->fb2 = 0.0f;
}
static void fm_key_off(fmch_t *c, int fast)
{
    int k;
    if (!c->on)
        return;
    c->rel = 1;
    for (k = 0; k < 4; k++)
        if (c->op[k].st) {
            c->op[k].st = 4;
            if (fast)
                c->op[k].rr = rate_of(26);
        }
}
static int fm_tick(fmch_t *c, int ch)            /* the envelopes, a control tick: each operator's gain target */
{
    int k, live = 0;
    for (k = 0; k < 4; k++) {
        op_t *p = &c->op[k];
        float a = p->att, tgt, x;
        switch (p->st) {
        case 1:
            a -= a * p->ka;
            if (a < 0.1f || p->ka >= 1.0f)
                a = 0.0f, p->st = 2;
            break;
        case 2:
            a += p->d1;
            if (a >= p->sl)
                a = p->sl, p->st = 3;
            break;
        case 3: a += p->d2; break;
        case 4: a += p->rr; break;
        default: a = ENV_TOP; break;
        }
        if (a >= ENV_TOP) {
            a = ENV_TOP;
            if (p->st >= 3)
                p->st = 0;
        }
        p->att = a;
        x = a + p->tl + ((c->car >> k & 1) ? c->extra : 0.0f);
        tgt = (p->st && x < ENV_TOP && !p->mute) ? fm_exp2f(-x * (1.0f / 6.0206f)) : 0.0f;
        p->dg = (tgt - p->g) * (1.0f / (float)FSUB);
        if ((c->car >> k & 1) && (p->st || p->g > 1e-5f))
            live = 1;
        if (ch >= 0 && ch < EN_NFM) {
            en_op_lvl[ch][k] = (uint8_t)(p->st ? (int)(255.0f * (1.0f - a * (1.0f / ENV_TOP))) : 0);
            en_op_stage[ch][k] = p->st;
        }
    }
    if (!live)
        c->on = 0;
    return live;
}

/* one channel's FM, FSUB samples, added into the bus */
#define OPS(o, m) (SIN[(uint32_t)((int32_t)((o)->ph >> 12) + (int32_t)(m)) >> 10 & 1023u] * (o)->g)
#define MODK 4194304.0f                          /* a modulator at full level: four cycles of phase (in 2^20ths) */
static void fm_run(fmch_t *c, float *bl, float *br, float gl, float gr)
{
    op_t *o1 = &c->op[0], *o2 = &c->op[1], *o3 = &c->op[2], *o4 = &c->op[3];
    float fb1 = c->fb1, fb2 = c->fb2, fbk = c->fbk;
    int j, alg = c->alg;
    for (j = 0; j < FSUB; j++) {
        float a, b, d, e, y;
        o1->g += o1->dg, o2->g += o2->dg, o3->g += o3->dg, o4->g += o4->dg;
        a = OPS(o1, (fb1 + fb2) * fbk);
        fb2 = fb1;
        fb1 = a;
        switch (alg) {
        case 0:
            b = OPS(o2, a * MODK);
            d = OPS(o3, b * MODK);
            y = OPS(o4, d * MODK);
            break;
        case 1:
            b = OPS(o2, 0.0f);
            d = OPS(o3, (a + b) * MODK);
            y = OPS(o4, d * MODK);
            break;
        case 2:
            b = OPS(o2, 0.0f);
            d = OPS(o3, b * MODK);
            y = OPS(o4, (a + d) * MODK);
            break;
        case 3:
            b = OPS(o2, a * MODK);
            d = OPS(o3, 0.0f);
            y = OPS(o4, (b + d) * MODK);
            break;
        case 4:
            b = OPS(o2, a * MODK);
            d = OPS(o3, 0.0f);
            e = OPS(o4, d * MODK);
            y = b + e;
            break;
        case 5:
            b = OPS(o2, a * MODK);
            d = OPS(o3, a * MODK);
            e = OPS(o4, a * MODK);
            y = b + d + e;
            break;
        case 6:
            b = OPS(o2, a * MODK);
            d = OPS(o3, 0.0f);
            e = OPS(o4, 0.0f);
            y = b + d + e;
            break;
        default:
            b = OPS(o2, 0.0f);
            d = OPS(o3, 0.0f);
            e = OPS(o4, 0.0f);
            y = a + b + d + e;
            break;
        }
        o1->ph += o1->inc, o2->ph += o2->inc, o3->ph += o3->inc, o4->ph += o4->inc;
        bl[j] += y * gl;
        br[j] += y * gr;
    }
    c->fb1 = fb1;
    c->fb2 = fb2;
}

/* ------------------------------------------------- the DAC and the PSG --- */
#define DAC_SR 11025.0f
static const uint16_t DLEN[EN_NDRUMS] = {3528, 2425, 3307, 3858, 2756, 13230, 5512, 441};
static int8_t dbank[3528 + 2425 + 3307 + 3858 + 2756 + 13230 + 5512 + 441] EN_POOL;
static const int8_t *dtab[EN_NDRUMS];
static uint32_t nseed = 0x1234567u;
static float nz(void)
{
    nseed = nseed * 1664525u + 1013904223u;
    return (float)(int32_t)nseed * (1.0f / 2147483648.0f);
}
static void dac_tables(void)                     /* the samples, made once: 8 bits at 11 kHz, as a cartridge kept them */
{
    int d, i;
    int8_t *w = dbank;
    for (d = 0; d < EN_NDRUMS; d++) {
        float ph = 0.0f, ph2 = 0.0f, lp = 0.0f, hp = 0.0f, prev = 0.0f;
        dtab[d] = w;
        for (i = 0; i < DLEN[d]; i++) {
            float t = (float)i * (1.0f / DAC_SR), x = 0.0f, n = nz(), f;
            switch (d) {
            case DR_KICK:
                f = 48.0f + 140.0f * fm_expf(-t / 0.028f);
                ph += f / DAC_SR;
                x = fm_sinf(ph * FM_TWO_PI) * fm_expf(-t / 0.13f) * 1.3f + (t < 0.004f ? n * 0.6f : 0.0f);
                x = fm_tanhf(x * 1.6f);
                break;
            case DR_SNARE:
                ph += 185.0f / DAC_SR;
                ph2 += 330.0f / DAC_SR;
                hp = n - prev;
                prev = n;
                x = fm_sinf(ph * FM_TWO_PI) * fm_expf(-t / 0.045f) * 0.6f +
                    fm_sinf(ph2 * FM_TWO_PI) * fm_expf(-t / 0.03f) * 0.3f + (n * 0.5f + hp * 0.35f) * fm_expf(-t / 0.075f);
                x = fm_tanhf(x * 1.3f);
                break;
            case DR_TOMH: case DR_TOML:
                f = d == DR_TOMH ? 150.0f + 70.0f * fm_expf(-t / 0.05f) : 98.0f + 45.0f * fm_expf(-t / 0.05f);
                ph += f / DAC_SR;
                x = fm_sinf(ph * FM_TWO_PI) * fm_expf(-t / 0.17f) + n * 0.12f * fm_expf(-t / 0.02f);
                x = fm_tanhf(x * 1.4f);
                break;
            case DR_CLAP: {
                float e = 0.0f;
                int b;
                for (b = 0; b < 3; b++) {
                    float u = t - (float)b * 0.011f;
                    if (u >= 0.0f)
                        e = fm_maxf(e, fm_expf(-u / 0.004f));
                }
                if (t > 0.022f)
                    e = fm_maxf(e, 0.7f * fm_expf(-(t - 0.022f) / 0.08f));
                lp += (n - lp) * 0.45f;
                hp += (lp - hp) * 0.08f;
                x = (lp - hp) * e * 2.2f;
                break;
            }
            case DR_CRASH: {
                float m = (fm_sinf(t * 540.0f * FM_TWO_PI) > 0.0f ? 1.0f : -1.0f) *
                          (fm_sinf(t * 817.0f * FM_TWO_PI) > 0.0f ? 1.0f : -1.0f) *
                          (fm_sinf(t * 1253.0f * FM_TWO_PI) > 0.0f ? 1.0f : -1.0f);
                hp = n - prev;
                prev = n;
                x = (hp * 0.55f + m * 0.25f + n * 0.2f) * (0.25f * fm_expf(-t / 0.02f) + 0.75f * fm_expf(-t / 0.42f));
                break;
            }
            case DR_HIT: {                       /* an orchestra hit on C4: root, fifth, octaves, brassy */
                static const float FH[5] = {130.81f, 196.0f, 261.63f, 392.0f, 523.25f};
                int v, h;
                float env = (t < 0.004f ? t / 0.004f : 1.0f) * fm_expf(-t / 0.16f);
                for (v = 0; v < 5; v++)
                    for (h = 1; h <= 4; h++)
                        x += fm_sinf(t * FH[v] * (float)h * (1.0f + (float)v * 0.002f) * FM_TWO_PI) / (float)h;
                x = fm_tanhf(x * 0.32f * env * 2.0f) + n * 0.15f * fm_expf(-t / 0.01f);
                break;
            }
            default:                             /* the rim */
                ph += 1700.0f / DAC_SR;
                x = fm_sinf(ph * FM_TWO_PI) * fm_expf(-t / 0.007f) + n * 0.4f * fm_expf(-t / 0.004f);
                break;
            }
            x = fm_clampf(x, -1.0f, 1.0f);
            *w++ = (int8_t)(x * 120.0f);
        }
    }
}
static struct {
    int d;
    float pos, step, g;
} dac;
static struct {
    int on, fr, kind;
    uint32_t lfsr;
    float g, gt, hp;
    int vol;
} psg;
static const int8_t V_HAT[] = {12, 8, 5, 3, 1, 0};
static const int8_t V_OHAT[] = {12, 11, 10, 9, 8, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0};

/* --------------------------------------------------------- the player --- */
static float row_left, frame_left;
static int gap_left;
static int retune, retune_go;
static float row_len = 4410.0f;
static float lv, hp_l, hp_r, t1l, t2l, t1r, t2r, tone_a = 0.5f;
static float fade = 1.0f;
static int kb_note, kb_patch = -1;          /* the keys: their note, their patch (-1 the game's lead) */

static void tempo_set(void)
{
    int b = par[P_TEMPO];
    en_bpm = (uint8_t)b;
    row_len = EN_SR * 15.0f / (float)b;
}
static float row_time(int row)                   /* a row's length, with the swing (the game's: eighths or 16ths) */
{
    float s = (float)par[P_SWING] * (0.32f / 100.0f);
    int first = STYLES[en_cur->style].swing8 ? (row & 2) == 0 : (row & 1) == 0;
    return row_len * (first ? 1.0f + s : 1.0f - s);
}

static void ch_note(fmch_t *c, int note, int ins, int fx, int fxp, int vol, int echo)
{
    c->ax = c->ay = 0;
    c->vs = c->vd = 0;
    c->sp = 0.0f;
    c->fr = 0;
    c->pitch = (float)note;
    c->to = (float)note;
    if (fx == '0') {
        c->ax = (uint8_t)(fxp >> 4);
        c->ay = (uint8_t)(fxp & 15);
    } else if (fx == '4') {
        c->vs = (uint8_t)(fxp >> 4);
        c->vd = (uint8_t)(fxp & 15);
        c->vph = 0.0f;
    } else if (fx == 'Q') {                      /* a bend up by y semitones, speed x */
        c->to = (float)(note + (fxp & 15));
        c->sp = (float)(fxp >> 4) * (1.0f / 8.0f);
    }
    fm_key_on(c, ins, note, vol, echo);
    fm_pitch(c, c->pitch);
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
        if (!e->note)
            continue;
        if (e->lay & LAY_ECHO) {
            if (par[P_ECHO] == 0 || (e->lay & 7) > lvl)
                continue;
        } else if ((e->lay & 7) > lvl) {
            continue;
        }
        if (k < EN_NFM) {
            fmch_t *c = &fm[k];
            if (e->note == NOTE_REL)
                fm_key_off(c, 0);
            else if (e->note == NOTE_CUT)
                fm_key_off(c, 1);
            else
                ch_note(c, e->note, e->ins, e->fx, e->fxp, e->vol, (e->lay & LAY_ECHO) != 0);
            en_ch_note[k] = e->note < NOTE_REL ? e->note : 0;
            if (e->ins != 0xFF)
                en_ch_ins[k] = e->ins;
        } else if (k == CH_DAC) {
            int d = e->ins < EN_NDRUMS ? e->ins : 0;
            dac.d = d;
            dac.pos = 0.0f;
            dac.step = (DAC_SR / EN_SR) * fm_exp2f((float)(e->note - 60) * (1.0f / 12.0f));
            dac.g = (float)(e->vol ? e->vol : 15) * (1.0f / 15.0f);
            en_drum_hits[d]++;
            en_ch_ins[k] = (uint8_t)d;
            en_ch_note[k] = e->note;
            if (d == DR_KICK)
                en_kick++;
        } else {
            psg.on = 1;
            psg.fr = 0;
            psg.kind = e->ins;
            psg.vol = e->vol ? e->vol : 12;
            en_ch_ins[k] = e->ins;
            en_ch_note[k] = 1;
        }
    }
    en_rows_played++;
}

static void frame(fmch_t *c)                     /* a 60 Hz tick: arpeggio, vibrato, bends */
{
    float pt = 0.0f;
    if (!c->on)
        return;
    if (c->sp > 0.0f) {
        if (c->pitch < c->to)
            c->pitch = fm_minf(c->pitch + c->sp, c->to);
        else if (c->pitch > c->to)
            c->pitch = fm_maxf(c->pitch - c->sp, c->to);
    }
    if (c->ax | c->ay) {
        int sp = STYLES[en_cur->style].arp_sp, st = (c->fr / (sp ? sp : 1)) % 3;
        pt += (float)(st == 1 ? c->ax : st == 2 ? c->ay : 0);
    }
    if (c->vd) {                                 /* vibrato, easing in over its first third of a second */
        float ramp = fm_minf((float)c->fr * (1.0f / 20.0f), 1.0f);
        c->vph += (float)c->vs * (0.9f / 60.0f);
        if (c->vph >= 1.0f)
            c->vph -= 1.0f;
        pt += fm_sinf(c->vph * FM_TWO_PI) * (float)c->vd * 0.05f * ramp;
    }
    if (c->sp > 0.0f || c->ax || c->ay || c->vd)
        fm_pitch(c, c->pitch + pt);
    c->fr++;
}

/* ------------------------------------------------------------- control --- */
static void quiet(void)
{
    int k, j;
    for (k = 0; k < NFMV; k++) {
        fm[k].on = 0;
        for (j = 0; j < 4; j++) {
            fm[k].op[j].st = 0;
            fm[k].op[j].att = ENV_TOP;
            fm[k].op[j].g = fm[k].op[j].dg = 0.0f;
        }
    }
    dac.d = -1;
    psg.on = 0;
    psg.g = 0.0f;
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
    for (k = 0; k < EN_NFM; k++)
        fm_key_off(&fm[k], 0);
    en_phase = PH_GAP;
    gap_left = (int)(EN_SR * gap_s);
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN_];
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
                ch_note(&fm[6], a, kb_patch >= 0 ? kb_patch : STYLES[style_i].leadA, 0, 0, 14, 0);
            } else if (a == kb_note) {
                fm_key_off(&fm[6], 0);
            }
            break;
        case C_PANIC: quiet(); break;
        case C_KPATCH: kb_patch = a < PT_N ? a : -1; break;
        }
    }
}

void en_init(void)
{
    int i;
    fm_tables();
    dac_tables();
    for (i = 0; i < P_NPARAMS; i++)
        par[i] = PARAMS[i].def;
    tempo_set();
    en_loops = (uint8_t)par[P_LOOPS];
    for (i = 0; i < NFMV; i++) {
        fm[i].alg = 4;
        fm[i].car = ALG_CAR[4];
    }
    psg.lfsr = 0x4000u;
    q_r = q_w;
    quiet();
    en_song_no = 0;
    song_i = 1;
    new_song();
}

/* -------------------------------------------------------------- render --- */
/* the parts' places: hard-ish pans as the console's channels had them */
static const float PAN_L[NFMV] = {0.72f, 0.45f, 0.82f, 0.68f, 0.40f, 0.72f, 0.72f};
static const float PAN_R[NFMV] = {0.72f, 0.82f, 0.40f, 0.68f, 0.82f, 0.72f, 0.72f};

static int32_t sub_out[SUB * 2];
static int sub_pos = SUB;
static void render_sub(uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 1.4f;
    float bl[FSUB], br[FSUB];
    float pg[NFMV];
    float gl = (float)par[P_LEAD] * 0.01f * 0.34f, gc = (float)par[P_CHORDS] * 0.01f * 0.20f;
    float gb = (float)par[P_BASS] * 0.01f * 0.42f, gd = (float)par[P_DRUMS] * 0.01f;
    float fc = 2400.0f * fm_exp2f((float)par[P_TONE] * 0.028f), ta, hp_a = 1.0f - fm_expf(-FM_TWO_PI * 22.0f / EN_SR);
    float lv_rel = fm_expf(-1.0f / (0.4f * EN_SR)), lv_att = 1.0f - fm_expf(-1.0f / (0.002f * EN_SR));
    float vu_acc = 0.0f;
    int k, j;
    fc = fm_clampf(fc, 1000.0f, 16000.0f);
    ta = 1.0f - fm_expf(-FM_TWO_PI * fc / EN_SR);
    tone_a += (ta - tone_a) * 0.2f;
    /* the song: rows and the 60 Hz frames, on the tick */
    if (en_playing) {
        if (en_phase == PH_GAP) {
            gap_left -= SUB;
            if (gap_left <= 0)
                new_song();
        }
        while (en_phase != PH_GAP && row_left <= 0.0f) {
            const en_song_t *s = en_cur;
            row_step();
            for (k = 0; k < EN_NFM; k++)
                if (fm[k].on && fm[k].fr == 0)
                    frame(&fm[k]);
            row_left += row_time(prow);
            if (++prow >= s->plen[s->order[pord]]) {   /* the pattern's end: the next in the order */
                int nx;
                prow = 0;
                if (retune_go && s->osect[pord] != 'E') {
                    nx = s->nord - 1;            /* the mood moved: on to the jingle */
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
        if (en_phase != PH_GAP)
            row_left -= (float)SUB;
        if (retune > 0 && (retune -= SUB) <= 0)
            retune_go = 1;
    }
    frame_left -= (float)SUB;
    if (frame_left <= 0.0f) {
        for (k = 0; k < NFMV; k++)
            frame(&fm[k]);
        frame_left += EN_SR / 60.0f;
        if (psg.on) {                            /* the hat's volume steps */
            const int8_t *v = psg.kind == HT_OPEN ? V_OHAT : V_HAT;
            int n = psg.kind == HT_OPEN ? NA(V_OHAT) : NA(V_HAT);
            int x = v[psg.fr < n ? psg.fr : n - 1];
            psg.gt = (float)(x * psg.vol) * (1.0f / 180.0f);
            if (!x)
                psg.on = 0;
            psg.fr++;
        } else {
            psg.gt = 0.0f;
        }
    }
    /* the FM: the envelopes, then each live channel's sixteen samples */
    for (j = 0; j < FSUB; j++)
        bl[j] = br[j] = 0.0f;
    for (k = 0; k < NFMV; k++) {
        float g = k == CH_LEAD || k == CH_DUO || k == 6 ? gl : k == CH_BASS ? gb : gc;
        pg[k] = g * pnorm[fm[k].ins < PT_N ? fm[k].ins : 0];
        if (!fm[k].on) {
            if (k < EN_NFM) {
                int i;
                for (i = 0; i < 4; i++)
                    en_op_lvl[k][i] = 0, en_op_stage[k][i] = 0;
                en_ch_lvl[k] = 0;
            }
            continue;
        }
        if (fm_tick(&fm[k], k < EN_NFM ? k : -1)) {
            fm_run(&fm[k], bl, br, pg[k] * PAN_L[k], pg[k] * PAN_R[k]);
            if (k < EN_NFM) {
                int i, m = 0;
                for (i = 0; i < 4; i++)
                    if ((fm[k].car >> i & 1) && en_op_lvl[k][i] > m)
                        m = en_op_lvl[k][i];
                en_ch_lvl[k] = (uint8_t)m;
            }
        } else if (k < EN_NFM) {
            en_ch_lvl[k] = 0;
            en_ch_note[k] = 0;
        }
    }
    /* up to the output's rate (linear), the DAC and the hat, the tone, the leveller */
    en_ch_lvl[CH_DAC] = (uint8_t)(dac.d >= 0 ? 200 : 0);
    en_ch_lvl[CH_PSG] = (uint8_t)(psg.g * 600.0f > 255.0f ? 255 : (int)(psg.g * 600.0f));
    for (j = 0; j < SUB; j++) {
        float l, r, x, vol, dd = 0.0f, hh = 0.0f;
        l = bl[j];
        r = br[j];
        if (dac.d >= 0) {
            int ip = (int)dac.pos;
            if (ip >= DLEN[dac.d]) {
                dac.d = -1;
            } else {
                dd = (float)dtab[dac.d][ip] * (1.0f / 127.0f) * dac.g;
                dac.pos += dac.step;
            }
        }
        psg.g += (psg.gt - psg.g) * 0.05f;
        if (psg.g > 1e-4f) {
            uint32_t fb = (psg.lfsr ^ (psg.lfsr >> 1)) & 1u;
            float n;
            psg.lfsr = (psg.lfsr >> 1) | (fb << 14);
            n = (psg.lfsr & 1u) ? 0.5f : -0.5f;
            psg.hp += (n - psg.hp) * 0.35f;
            hh = (n - psg.hp) * psg.g;
        }
        l += (dd * 0.55f + hh * 0.30f) * gd;
        r += (dd * 0.55f + hh * 0.38f) * gd;
        l *= fade;
        r *= fade;
        fade = fade < 1.0f ? fade + 1.0f / (0.05f * EN_SR) : 1.0f;
        /* tone: a two-pole low-pass (the console's output filter), a DC cut */
        t1l += (l - t1l) * tone_a;
        t2l += (t1l - t2l) * tone_a;
        t1r += (r - t1r) * tone_a;
        t2r += (t1r - t2r) * tone_a;
        hp_l += (t2l - hp_l) * hp_a;
        hp_r += (t2r - hp_r) * hp_a;
        l = t2l - hp_l;
        r = t2r - hp_r;
        vu_acc += l * l + r * r;
        /* the leveller: a quick attack, a slow release, above -6 dB; the tanh tops out at -1.2 dBFS */
        x = fm_maxf(fm_fabsf(l), fm_fabsf(r)) * gain;
        lv = x > lv ? lv + (x - lv) * lv_att : lv * lv_rel;
        vol = lv > 0.5f ? 0.5f / lv : 1.0f;
        l *= gain * vol;
        r *= gain * vol;
        sub_out[2 * j] = (int32_t)(fm_tanhf(l) * 7300000.0f);
        sub_out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 7300000.0f);
    }
    en_vu = en_vu + (fm_minf(fm_sqrtf(vu_acc / (float)(2 * SUB)) * 3.0f, 1.0f) - en_vu) * 0.02f;
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    drain();
    while (n) {
        uint32_t m;
        if (sub_pos >= SUB) {
            render_sub(gain_q12);
            sub_pos = 0;
        }
        m = (uint32_t)(SUB - sub_pos);
        if (m > n)
            m = n;
        {
            uint32_t i;
            for (i = 0; i < 2u * m; i++)
                out[i] = sub_out[2 * sub_pos + (int)i];
        }
        sub_pos += (int)m;
        out += 2 * m;
        n -= m;
    }
}
