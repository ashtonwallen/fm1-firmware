/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * BOSSA LOUNGE: ashtonwallen, 2026 */
/* The instrument (bossa.h). One compilation unit, -O2. The command queue and the render's shape are AMB-1's. */
#include "bossa.h"
#include "fastmath.h"
#ifdef EN_HOST
#include <stdio.h>
#endif

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

#ifdef EN_HOST
void (*en_dbg)(int type, int bar, int step, int note, float vel, int dur16);
#define DBG(t, n, v, d)                                                                                            \
    do {                                                                                                           \
        if (en_dbg)                                                                                                \
            en_dbg(t, tk.bar, tk.step, n, v, d);                                                                   \
    } while (0)
#else
#define DBG(t, n, v, d) ((void)0)
#endif

const char *const EN_NOTE_NAME[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Tempo", 60, 150, 132}, {"Feel", 0, 100, 35},  {"Mood", 0, 100, 30},    {"Melody", 0, 100, 65},
    {"Guitar", 0, 100, 75},  {"Bass", 0, 100, 70},  {"Drums", 0, 100, 60},   {"Lead", 0, 2, 2},
    {"Tone", 0, 100, 55},    {"Reverb", 0, 100, 40}, {"Length", 0, 2, 1},
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
    if (i == P_LENGTH) {
        put_s(buf, v <= 0 ? "Short" : v == 1 ? "Normal" : "Long");
    } else if (i == P_LEAD) {
        put_s(buf, v <= 0 ? "Flute" : v == 1 ? "Vibes" : "Both");
    } else if (i == P_MOOD) {                     /* sunny (major) .. saudade (minor) */
        put_s(buf, v < 20 ? "Sunny" : v < 40 ? "Warm" : v < 60 ? "Mellow" : v < 80 ? "Wistful" : "Saudade");
    } else if (i == P_MELODY && v <= 0) {
        put_s(buf, "Off");
    } else {
        put_u(buf, (uint32_t)(v < 0 ? 0 : v));
    }
}

/* ------------------------------------------------------------ styles --- */
/* the player's ten (Tempo Feel Mood Melody, Guitar Bass Drums Lead, Tone Reverb) and the style's own flavour:
 * gpats: the guitar's figures it may use (bit i of G_*); bfigs: the bass's (bit i of B_*); kit: K_*; cells: the
 * melody's rhythms (0 as written, 1 busier, 2 sparer); neon: the sign's colour */
enum { G_CLAVE, G_23, G_BATIDA, G_BALLAD, G_SPARSE, G_N };
enum { B_BOUNCE, B_ANTIC, B_TWO, B_LONG, B_N };
enum { K_BOSSA, K_BALLAD, K_SAMBA, K_LIGHT };
typedef struct {
    const char *name;
    int16_t v[P_NSTYLE];
    uint8_t gpats, bfigs, kit, cells, neon;
} style_t;
static const style_t STYLES[EN_NSTYLES] = {
    /*                Tmp Fel Moo Mel  Gtr Bas Drm Lead Ton Rev   gpats  bfigs  kit       cells neon */
    {"Cigarette Boat",  {138, 25, 15, 70, 75, 70, 65, 0, 66, 35},   0x05, 0x03, K_BOSSA,  1, 0},
    {"Umbrella Stand",  {122, 45, 75, 60, 75, 65, 45, 1, 40, 55},   0x03, 0x01, K_BOSSA,  0, 1},
    {"Bellhop's Break", {126, 35, 40, 45, 72, 65, 50, 2, 55, 40},   0x01, 0x01, K_LIGHT,  0, 2},
    {"Slow Caipirinha", {96, 50, 55, 65, 75, 60, 40, 0, 45, 50},    0x09, 0x04, K_BALLAD, 2, 3},
    {"Four Mallets",    {132, 40, 35, 80, 55, 75, 65, 1, 60, 35},   0x11, 0x03, K_BOSSA,  1, 4},
    {"Melted Ice",      {76, 60, 70, 55, 72, 60, 35, 1, 35, 65},    0x08, 0x0C, K_BALLAD, 2, 5},
    {"Seersucker",      {128, 20, 8, 65, 75, 65, 55, 0, 75, 40},    0x03, 0x01, K_BOSSA,  0, 6},
    {"Olive Number Two",{84, 70, 55, 60, 72, 60, 35, 0, 40, 55},    0x18, 0x04, K_BALLAD, 2, 7},
    {"Chlorine Blue",   {144, 20, 15, 70, 70, 70, 75, 2, 70, 30},   0x04, 0x02, K_SAMBA,  1, 0},
    {"Unmailed Letters",{116, 40, 92, 65, 80, 60, 45, 0, 45, 50},   0x01, 0x01, K_BOSSA,  0, 3},
    {"Red Banquette",   {70, 55, 45, 50, 72, 55, 25, 1, 35, 70},    0x08, 0x08, K_BALLAD, 2, 5},
    {"Striped Awning",  {124, 35, 60, 60, 75, 65, 55, 2, 55, 45},   0x07, 0x03, K_BOSSA,  0, 2},
    {"Two Chairs Left", {112, 30, 40, 70, 85, 0, 0, 0, 60, 45},     0x05, 0x01, K_LIGHT,  0, 1},
    {"Paper Parasol",   {145, 25, 25, 75, 65, 75, 75, 1, 70, 30},   0x05, 0x03, K_SAMBA,  1, 6},
};
static const style_t *sty(int i) { return &STYLES[(i % EN_NSTYLES + EN_NSTYLES) % EN_NSTYLES]; }
const char *en_style_name(int i) { return sty(i)->name; }
uint8_t en_style_neon(int i) { return sty(i)->neon; }
void en_style_recipe(int i, int16_t *par)
{
    int k;
    for (k = 0; k < P_NSTYLE; k++)
        par[k] = sty(i)->v[k];
}

/* ------------------------------------------------------------ harmony --- */
enum { Q_MAJ7, Q_MAJ9, Q_69, Q_6, Q_M7, Q_M9, Q_M6, Q_M69, Q_7, Q_9, Q_13, Q_7B9, Q_M7B5, Q_DIM7, Q_9SUS, Q_7S11, Q_NQ };
static const char *const Q_NAME[Q_NQ] = {"maj7", "maj9", "6/9", "6", "m7", "m9", "m6", "m6/9",
                                         "7", "9", "13", "7b9", "m7b5", "dim7", "9sus", "7#11"};
#define B(i) (1u << (i))
/* each quality: its chord tones and the tensions a melody may rest on (intervals above the root, bit i), the
 * scale it takes when it is foreign to the key, and the guitar's four notes (a close stack, dropped later) */
static const uint16_t Q_TONES[Q_NQ] = {
    B(0) | B(4) | B(7) | B(11), B(0) | B(4) | B(7) | B(11) | B(2), B(0) | B(4) | B(7) | B(9) | B(2),
    B(0) | B(4) | B(7) | B(9), B(0) | B(3) | B(7) | B(10), B(0) | B(3) | B(7) | B(10) | B(2),
    B(0) | B(3) | B(7) | B(9), B(0) | B(3) | B(7) | B(9) | B(2), B(0) | B(4) | B(7) | B(10),
    B(0) | B(4) | B(7) | B(10) | B(2), B(0) | B(4) | B(7) | B(10) | B(9), B(0) | B(4) | B(7) | B(10) | B(1),
    B(0) | B(3) | B(6) | B(10), B(0) | B(3) | B(6) | B(9), B(0) | B(5) | B(7) | B(10) | B(2),
    B(0) | B(4) | B(7) | B(10) | B(6),
};
static const uint16_t Q_TENS[Q_NQ] = {
    B(2) | B(9), B(9), 0, B(2), B(2) | B(5), B(5), B(2), 0, B(2) | B(9), B(9), B(2), B(8), B(5), 0, B(9), B(2) | B(9),
};
#define SC_ION (B(0) | B(2) | B(4) | B(5) | B(7) | B(9) | B(11))
#define SC_LYD (B(0) | B(2) | B(4) | B(6) | B(7) | B(9) | B(11))
#define SC_DOR (B(0) | B(2) | B(3) | B(5) | B(7) | B(9) | B(10))
#define SC_MEL (B(0) | B(2) | B(3) | B(5) | B(7) | B(9) | B(11))
#define SC_MIX (B(0) | B(2) | B(4) | B(5) | B(7) | B(9) | B(10))
#define SC_PHD (B(0) | B(1) | B(4) | B(5) | B(7) | B(8) | B(10))
#define SC_LOC (B(0) | B(1) | B(3) | B(5) | B(6) | B(8) | B(10))
#define SC_WH (B(0) | B(2) | B(3) | B(5) | B(6) | B(8) | B(9) | B(11))
#define SC_LDOM (B(0) | B(2) | B(4) | B(6) | B(7) | B(9) | B(10))
#define SC_AEO (B(0) | B(2) | B(3) | B(5) | B(7) | B(8) | B(10))
static const uint16_t Q_SCALE[Q_NQ] = {SC_LYD, SC_LYD, SC_LYD, SC_LYD, SC_DOR, SC_DOR, SC_MEL, SC_MEL,
                                       SC_MIX, SC_MIX, SC_MIX, SC_PHD, SC_LOC, SC_WH, SC_MIX, SC_LDOM};
static const int8_t Q_GTR[Q_NQ][4] = {
    {0, 4, 7, 11}, {4, 7, 11, 14}, {4, 7, 9, 14}, {0, 4, 7, 9}, {0, 3, 7, 10}, {3, 7, 10, 14}, {0, 3, 7, 9},
    {3, 7, 9, 14}, {0, 4, 7, 10}, {4, 7, 10, 14}, {4, 10, 14, 21}, {4, 7, 10, 13}, {0, 3, 6, 10}, {0, 3, 6, 9},
    {5, 7, 10, 14}, {4, 6, 10, 14},
};
static int q_fifth(int q) { return q == Q_M7B5 || q == Q_DIM7 ? 6 : 7; }
static uint16_t rot12(uint16_t m, int r)        /* intervals above a root -> pitch classes */
{
    r = (r % 12 + 12) % 12;
    return (uint16_t)(((uint32_t)m << r | (uint32_t)m >> (12 - r)) & 0xFFFu);
}
const char *en_chord_name(int root, int q, char *buf)
{
    put_s(put_s(buf, EN_NOTE_NAME[(root % 12 + 12) % 12]), Q_NAME[q < 0 || q >= Q_NQ ? 0 : q]);
    return buf;
}

/* ------------------------------------------------------------- forms --- */
/* the tunes' forms: sections of chords from the repertoire's stock (each chord: its root above the key, its
 * quality, its length in half bars, and a bass note other than the root, -1 none). A section: 8, 12 or 16 bars.
 * The layout names the sections in order: A, a (the A that ends home: before the bridge, or last), B, C */
typedef struct {
    int8_t r;
    uint8_t q, h;
    int8_t b;
} fc_t;
typedef struct {
    uint8_t n;
    fc_t c[18];
} sect_t;
typedef struct {
    const char *name;
    uint8_t minor, mood;
    const char *layout;
    sect_t s[4];                                 /* A, a, B, C */
} form_t;
#define C(r, q, h) {r, Q_##q, h, -1}
#define CB(r, q, h, b) {r, Q_##q, h, b}
static const form_t FORMS[] = {
    {"lagoon", 0, 25, "AaBA", {
        {6, {C(0, MAJ7, 4), C(2, 9, 4), C(2, M7, 2), C(1, 7S11, 2), C(0, MAJ7, 2), C(1, 7S11, 2)}},
        {5, {C(0, MAJ7, 4), C(2, 9, 4), C(2, M7, 2), C(1, 7S11, 2), C(0, 69, 4)}},
        {10, {C(1, MAJ7, 4), C(6, 9, 4), C(1, M7, 4), C(9, 9, 4), C(2, M7, 4), C(10, 9, 4), C(4, M7, 2), C(9, 7B9, 2),
              C(2, M7, 2), C(7, 7B9, 2)}},
        {0, {C(0, MAJ7, 2)}}}},
    {"one note", 0, 30, "AaBA", {
        {8, {C(4, M7, 2), C(3, 7, 2), C(2, M7, 2), C(1, 7S11, 2), C(4, M7, 2), C(3, 7, 2), C(2, M7, 2), C(1, 7S11, 2)}},
        {6, {C(4, M7, 2), C(3, 7, 2), C(2, M7, 2), C(1, 7S11, 2), C(0, MAJ7, 4), C(0, 6, 4)}},
        {8, {C(7, M7, 2), C(0, 9, 2), C(5, MAJ7, 4), C(5, M7, 2), C(10, 9, 2), C(3, MAJ7, 2), C(2, M7, 1), C(7, 13, 1)}},
        {0, {C(0, MAJ7, 2)}}}},
    {"carnival", 1, 85, "ABAC", {
        {10, {C(0, M7, 2), C(2, M7B5, 1), C(7, 7B9, 1), C(0, M7, 2), C(2, M7B5, 1), C(7, 7B9, 1), C(0, M7, 2), C(5, M7, 1),
              C(10, 9, 1), C(3, MAJ7, 4)}},
        {0, {C(0, M7, 2)}},
        {8, {C(5, M7, 2), C(10, 9, 2), C(3, MAJ7, 2), C(8, MAJ7, 2), C(2, M7B5, 2), C(7, 7B9, 2), C(0, M7, 2), C(7, 7B9, 2)}},
        {11, {C(0, M7, 2), C(2, M7B5, 1), C(7, 7B9, 1), C(0, M7, 2), C(0, 7B9, 2), C(5, M7, 2), C(10, 9, 2), C(3, MAJ7, 1),
              C(8, MAJ7, 1), C(2, M7B5, 1), C(7, 7B9, 1)}}}},
    {"borrowed four", 0, 40, "AaBA", {
        {10, {C(0, MAJ7, 2), C(3, DIM7, 2), C(2, M7, 2), C(7, 13, 2), C(5, MAJ7, 2), C(5, M6, 2), C(4, M7, 1), C(9, 7B9, 1),
              C(2, M7, 1), C(7, 9, 1)}},
        {7, {C(0, MAJ7, 2), C(3, DIM7, 2), C(2, M7, 2), C(7, 13, 2), C(5, MAJ7, 2), C(5, M6, 2), C(0, 69, 4)}},
        {8, {C(7, M7, 2), C(0, 9, 2), C(5, MAJ7, 4), C(10, 9, 4), C(4, M7, 1), C(9, 7B9, 1), C(2, M7, 1), C(7, 9SUS, 1)}},
        {0, {C(0, MAJ7, 2)}}}},
    {"so nice", 0, 15, "AaBA", {
        {9, {C(0, MAJ7, 4), C(7, M7, 2), C(0, 9, 2), C(5, MAJ7, 2), C(5, M6, 2), C(4, M7, 1), C(9, 7B9, 1), C(2, M7, 1),
             C(7, 13, 1)}},
        {8, {C(0, MAJ7, 4), C(7, M7, 2), C(0, 9, 2), C(5, MAJ7, 2), C(5, M6, 2), C(2, M7, 1), C(7, 13, 1), C(0, 69, 2)}},
        {10, {C(5, MAJ7, 2), C(10, 9, 2), C(4, M7, 2), C(9, 7B9, 2), C(2, M7, 2), C(7, 9, 2), C(4, M7, 1), C(3, M7, 1),
              C(2, M7, 1), C(1, 7S11, 1)}},
        {0, {C(0, MAJ7, 2)}}}},
    {"blue minor", 1, 75, "ABAB", {
        {5, {C(0, M9, 4), C(5, M7, 4), C(2, M7B5, 2), C(7, 7B9, 2), C(0, M9, 4)}},
        {0, {C(0, M9, 2)}},
        {8, {C(3, M7, 2), C(8, 9, 2), C(1, MAJ7, 4), C(2, M7B5, 2), C(7, 7B9, 2), C(0, M9, 2), C(2, M7B5, 1), C(7, 7B9, 1)}},
        {0, {C(0, M9, 2)}}}},
    {"lament", 1, 90, "AaBA", {
        {9, {C(0, M9, 2), C(11, DIM7, 2), C(10, M6, 2), CB(5, 13, 2, 9), C(8, MAJ7, 2), C(2, M7B5, 1), C(7, 7B9, 1),
             C(0, M9, 2), C(7, 7B9, 2)}},
        {8, {C(0, M9, 2), C(11, DIM7, 2), C(10, M6, 2), CB(5, 13, 2, 9), C(8, MAJ7, 2), C(2, M7B5, 1), C(7, 7B9, 1),
             C(0, M69, 4)}},
        {8, {C(5, M9, 2), C(10, 13, 2), C(3, MAJ9, 2), C(8, MAJ7, 2), C(8, 7S11, 2), C(7, 7B9, 2), C(0, M9, 2), C(7, 7B9, 2)}},
        {0, {C(0, M9, 2)}}}},
    {"summer", 0, 20, "AaBA", {
        {7, {C(0, 69, 4), C(2, 9, 4), C(2, M9, 2), C(7, 13, 2), C(0, 69, 2), C(2, M7, 1), C(7, 13, 1)}},
        {5, {C(0, 69, 4), C(2, 9, 4), C(2, M9, 2), C(7, 13, 2), C(0, 69, 4)}},
        {6, {C(5, MAJ7, 4), C(10, 9, 4), C(4, M7, 2), C(9, 7B9, 2), C(2, M9, 2), C(7, 13, 2)}},
        {0, {C(0, 69, 2)}}}},
    {"turnaround", 0, 35, "AaBA", {
        {14, {C(0, MAJ7, 1), C(2, M7, 1), C(4, M7, 1), C(3, M7, 1), C(2, M9, 2), C(7, 13, 2), C(0, MAJ7, 1), C(2, M7, 1),
              C(4, M7, 1), C(3, M7, 1), C(4, M7, 1), C(9, 7B9, 1), C(2, M7, 1), C(7, 13, 1)}},
        {13, {C(0, MAJ7, 1), C(2, M7, 1), C(4, M7, 1), C(3, M7, 1), C(2, M9, 2), C(7, 13, 2), C(0, MAJ7, 1), C(2, M7, 1),
              C(4, M7, 1), C(3, M7, 1), C(2, M7, 1), C(7, 13, 1), C(0, 69, 2)}},
        {8, {C(4, M7B5, 2), C(9, 7B9, 2), C(2, M9, 4), C(7, M7, 2), C(0, 9, 2), C(5, MAJ7, 2), C(5, M6, 1), C(10, 9, 1)}},
        {0, {C(0, MAJ7, 2)}}}},
    {"dorian", 1, 65, "AABA", {
        {8, {C(0, M9, 2), C(5, 9, 2), C(0, M9, 2), C(5, 9, 2), C(8, MAJ7, 2), C(7, 7B9, 2), C(0, M9, 2), C(7, 7B9, 2)}},
        {0, {C(0, M9, 2)}},
        {5, {C(3, MAJ7, 4), C(8, 9, 4), C(2, M7B5, 2), C(7, 7B9, 2), C(0, M9, 4)}},
        {0, {C(0, M9, 2)}}}},
    {"ballad", 0, 45, "AaBA", {
        {8, {C(0, MAJ9, 2), C(9, M9, 2), C(2, M9, 2), C(7, 13, 2), C(4, M7, 2), C(9, 7B9, 2), C(2, M9, 2), C(7, 7B9, 2)}},
        {9, {C(0, MAJ9, 2), C(9, M9, 2), C(2, M9, 2), C(7, 13, 2), C(4, M7, 2), C(9, 7B9, 2), C(2, M7, 1), C(7, 13, 1),
             C(0, 69, 2)}},
        {9, {C(5, MAJ9, 2), C(5, M6, 2), C(4, M7, 2), C(9, 7B9, 2), C(2, M9, 2), C(7, 13, 2), C(3, DIM7, 2), C(2, M7, 1),
             C(7, 7B9, 1)}},
        {0, {C(0, MAJ9, 2)}}}},
    {"minor ballad", 1, 95, "ABAC", {
        {8, {C(2, M7B5, 2), C(7, 7B9, 2), C(0, M9, 4), C(7, M7B5, 1), C(0, 7B9, 1), C(5, M9, 2), C(8, 9, 2), C(7, 7B9, 2)}},
        {0, {C(0, M9, 2)}},
        {7, {C(5, M7, 2), C(10, 13, 2), C(3, MAJ9, 4), C(8, MAJ7, 2), C(2, M7B5, 1), C(7, 7B9, 1), C(0, M9, 4)}},
        {9, {C(2, M7B5, 2), C(7, 7B9, 2), C(0, M9, 2), C(5, M9, 2), C(8, MAJ7, 2), C(1, 7S11, 2), C(2, M7B5, 1),
             C(7, 7B9, 1), C(0, M69, 2)}}}},
    {"keys down", 0, 25, "ABAC", {
        {6, {C(2, M7, 2), C(7, 9, 2), C(0, MAJ7, 4), C(0, M7, 2), C(5, 9, 2), C(10, MAJ7, 4)}},
        {0, {C(0, MAJ7, 2)}},
        {7, {C(10, M7, 2), C(3, 9, 2), C(8, MAJ7, 4), C(2, M7, 2), C(7, 7B9, 2), C(0, MAJ7, 2), C(7, 9SUS, 2)}},
        {10, {C(2, M7, 2), C(7, 9, 2), C(0, MAJ7, 4), C(4, M7, 1), C(9, 7B9, 1), C(2, M7, 1), C(7, 13, 1), C(0, 69, 2),
              C(2, M7, 1), C(7, 9, 1)}}}},
    {"sea", 1, 60, "ABAC", {
        {8, {C(0, M69, 4), C(2, M7B5, 2), C(7, 7B9, 2), C(0, M69, 2), C(5, 9, 2), C(3, MAJ7, 2), C(8, 9, 1), C(7, 7B9, 1)}},
        {0, {C(0, M69, 2)}},
        {6, {C(10, M7, 2), C(3, 9, 2), C(8, MAJ7, 4), C(5, M7, 2), C(10, 9, 2), C(3, MAJ7, 4)}},
        {8, {C(5, M9, 2), C(10, 13, 2), C(3, MAJ7, 2), C(8, MAJ7, 2), C(2, M7B5, 2), C(7, 7B9, 2), C(0, M69, 2), C(7, 7B9, 2)}}}},
    {"blues", 0, 45, "AA", {
        {16, {C(0, 9, 2), C(5, 9, 2), C(0, 9, 2), C(7, M7, 1), C(0, 9, 1), C(5, 9, 2), C(6, DIM7, 2), C(0, 13, 2),
              C(4, M7, 1), C(9, 7B9, 1), C(2, M9, 2), C(7, 13, 2), C(0, 9, 1), C(9, 7B9, 1), C(2, M7, 1), C(7, 7B9, 1)}},
        {0, {C(0, 9, 2)}},
        {0, {C(0, 9, 2)}},
        {0, {C(0, 9, 2)}}}},
};
#undef C
#undef CB
#define NFORMS (int)(sizeof FORMS / sizeof FORMS[0])
#define INTRO_BARS 4
#define END_BARS 6

/* ------------------------------------------------------------- titles --- */
static const char *const T_PT[] = {"Chuva", "Brisa", "Samba", "Tarde", "Canto", "Luz", "Sonho", "Saudade",
                                   "Ondas", "Barco", "Varanda", "Janela", "Estrela", "Areia", "Cais", "Flor", "Vento",
                                   "Rede", "Sombra", "Retrato", "Bilhete"};
static const char *const T_PTB[] = {"Abril", "Maio", "Julho", "Setembro", "Dezembro", "Domingo", "Inverno",
                                    "Outono", "Leblon", "Ipanema", "Urca", "Bahia", "Santos", "Recife", "Paraty",
                                    "Lisboa", "Agosto", "Janeiro"};
static const char *const T_PLACE[] = {"Leblon", "Ipanema", "Urca", "Bahia", "Santos", "Recife", "Paraty", "Lisboa",
                                      "Rio", "Leme", "Lapa"};
static const char *const T_ADJ[] = {"Wicker", "Crushed", "Starched", "Lacquered", "Rattan", "Sugared", "Unhurried",
                                    "Barefoot", "Sunburnt", "Tipsy", "Seersucker"};
static const char *const T_EN[] = {"Veranda", "Promenade", "Cabana", "Orchid", "Palms", "Sailboat", "Avenue",
                                   "Matchbook", "Cufflink", "Swizzle", "Ashtray", "Telegram", "Shutters", "Jukebox",
                                   "Ice Bucket", "Hammock", "Parasol", "Ceiling Fan"};
#define NA(a) (sizeof a / sizeof a[0])

uint32_t en_tune_rand(uint32_t base, uint32_t no)
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
    uint32_t h = en_tune_rand(seed, 77u);
    const char *pt = T_PT[h % NA(T_PT)], *ptb = T_PTB[(h >> 8) % NA(T_PTB)];
    const char *adj = T_ADJ[(h >> 13) % NA(T_ADJ)], *en = T_EN[(h >> 18) % NA(T_EN)];
    switch ((h >> 27) % 8u) {
    case 0: case 1: case 2:                          /* "Chuva de Abril" */
        put_s(put_s(put_s(b, pt), " de "), ptb);
        break;
    case 3: case 4:                                  /* "Wicker Jukebox" */ 
        put_s(put_s(put_s(b, adj), " "), en);
        break;
    case 5:                                          /* "Swizzle in Urca" */
        put_s(put_s(put_s(b, en), (h & 1u) ? " to " : " in "), T_PLACE[(h >> 22) % NA(T_PLACE)]);
        break;
    case 6:                                          /* "Tipsy Saudade" */ 
        put_s(put_s(put_s(b, adj), " "), pt);
        break;
    default:                                         /* "Samba de Domingo" */
        put_s(put_s(b, (h & 2u) ? "Samba de " : "Bossa de "), ptb);
        break;
    }
}

/* ------------------------------------------------------------ commands --- */
enum { CM_SET, CM_STYLE, CM_PLAY, CM_SKIP, CM_REPEAT, CM_TUNE_NO, CM_SEED_HI, CM_SEED_LO, CM_NOTE, CM_PANIC };
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
void en_set(int p, int v) { post(CM_SET, p, v); }
void en_style(int i) { post(CM_STYLE, i, 0); }
void en_play(int on) { post(CM_PLAY, on, 0); }
void en_skip(void) { post(CM_SKIP, 0, 0); }
void en_repeat(int on) { post(CM_REPEAT, on, 0); }
void en_note(int note, int vel) { post(CM_NOTE, note, vel); }
void en_panic(void) { post(CM_PANIC, 0, 0); }
void en_tune(uint32_t no, uint32_t seed)
{
    post(CM_SEED_HI, 0, (int)(seed >> 16));
    post(CM_SEED_LO, 0, (int)(seed & 0xFFFFu));
    post(CM_TUNE_NO, (int)(no >> 16 & 0xFFu), (int)(no & 0xFFFFu));
}

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_part, en_repeat_on;
volatile uint32_t en_tune_no, en_tune_seed, en_seed_base = 0x424F5341u, en_tune_ms, en_tune_len_ms;
volatile uint8_t en_key, en_minor, en_bpm = 132, en_chord_root, en_chord_q, en_letter, en_chorus, en_choruses, en_lead;
volatile uint16_t en_chord_pcs;
volatile float en_vu_l, en_vu_r, en_accent, en_tape;
volatile uint32_t en_beats, en_tunes_played;

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
static float urand(uint32_t *s) { return (float)(xs(s) >> 8) * (1.0f / 16777216.0f); }
static int uirand(uint32_t *s, int n) { return n > 0 ? (int)((xs(s) >> 8) % (uint32_t)n) : 0; }
static float trand(void) { return urand(&trng); }   /* the tune's own */
static int tirand(int n) { return uirand(&trng, n); }
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

/* -------------------------------------------------------------- guitar --- */
/* six nylon strings, Karplus-Strong: a delay a period long, a two-point average and a loss in the loop, an
 * all-pass for the fraction. A pluck writes a finger's shape (a soft triangle, a little noise, smoothed) into the
 * period about to be read. The thumb has string 0, the fingers 2..5; damping is a faster loss */
#define NGS 6
#define GS_N 1024
static float gs_buf[NGS][GS_N] EN_POOL;
static struct {
    int L, w;
    float c, apx, apy, rho, rho_damp, pan, peak;
    int32_t gate;
    uint8_t on, note;
} gs[NGS];
static float g_body_lo, g_body_bd;               /* the box: a resonance near 180 Hz */

static void pluck(int s, int note, float vel, int32_t gate, float bright)
{
    static float exc[GS_N];
    float f, P, d, t60, keep, sum = 0.0f, a, y1 = 0.0f, y2 = 0.0f, k;
    int L, i, start;
    float *b = gs_buf[s];
    while (note < 40)
        note += 12;
    while (note > 84)
        note -= 12;
    f = mtof((float)note);
    P = EN_SR / f;
    L = (int)(P - 0.65f);
    d = P - 0.5f - (float)L;                     /* 0.15 .. 1.15 */
    keep = gs[s].on && gs[s].note == note ? 0.2f : 0.0f;
    t60 = fm_clampf(2.4f * fm_sqrtf(196.0f / f), 1.1f, 4.2f);
    gs[s].L = L;
    gs[s].c = (1.0f - d) / (1.0f + d);
    gs[s].rho = fm_exp2f(-9.966f / (t60 * f));
    gs[s].rho_damp = fm_exp2f(-9.966f / (0.14f * f));
    if (keep == 0.0f)
        gs[s].apx = gs[s].apy = 0.0f;
    /* the finger's shape: a triangle peaked near the bridge, some noise, smoothed by the flesh; then the mean
     * taken off, and laid over a little of what the string still held (the same note struck again) */
    k = (0.13f + 0.08f * frand()) * (float)L;
    a = onepole(fm_clampf(700.0f + 2600.0f * vel * bright, 500.0f, 5000.0f));
    start = gs[s].w - L - 1;
    for (i = 0; i <= L; i++) {
        float x = (float)i < k ? (float)i / k : ((float)L - (float)i) / ((float)L - k);
        x += 0.25f * noise();
        y1 += (x - y1) * a;
        y2 += (y1 - y2) * a;
        exc[i] = y2;
        sum += y2;
    }
    sum /= (float)(L + 1);
    for (i = 0; i <= L; i++) {
        float *p = &b[(start + i) & (GS_N - 1)];
        *p = *p * keep + (exc[i] - sum) * vel * 0.9f;
    }
    gs[s].gate = gate;
    gs[s].on = 1;
    gs[s].note = (uint8_t)note;
    gs[s].peak = vel;
    gs[s].pan = fm_clampf((float)(note - 58) * 0.02f, -0.3f, 0.3f);
}

/* ---------------------------------------------------------------- bass --- */
/* an upright, plucked: a sine with its own phase in it (the attack's wood), a second harmonic fading, a decay */
static struct {
    float ph, inc, amp, peak, idx, h2, lp;
    int32_t gate;
    uint8_t on, atk;
} bs;
static void bass_on(int note, float vel, int32_t gate)
{
    bs.inc = mtof((float)note) / EN_SR;
    bs.on = 1;
    bs.atk = 1;
    bs.gate = gate;
    bs.peak = vel;
    bs.idx = 0.55f + 0.35f * vel;
    bs.h2 = 0.32f;
}

/* --------------------------------------------------------------- drums --- */
static struct {
    float ph, f, fend, pdec, amp, adec;
} kick;
static struct {
    float ph, inc, amp, dec, namp, ndec, lo, bd;
} rim;
static struct {
    float tap, tap_dec, sw, sw_tgt, sw_att, sw_dec;  /* a tap, a swish (its swell, its fall) */
    float lvl, lvl_t;                                /* the swirl's level (and where it is going) */
    float ph;                                        /* the swirl: once round each half bar */
    float lo, bd, hp;
} br;

static void kick_hit(float v)
{
    kick.ph = 0.0f;
    kick.f = 105.0f;
    kick.fend = 50.0f + 4.0f * frand();
    kick.pdec = tau(0.03f);
    kick.amp = v;
    kick.adec = tau(0.20f);
}
static void rim_hit(float v)
{
    rim.ph = 0.0f;
    rim.inc = (510.0f + 15.0f * frand()) / EN_SR;
    rim.amp = v * 0.6f;
    rim.dec = tau(0.022f);
    rim.namp = v * 0.9f;
    rim.ndec = tau(0.008f);
    if (v > en_accent)
        en_accent = v;
}
static void tap_hit(float v) { br.tap = fm_maxf(br.tap, v); }
static void swish_hit(float v, float len)
{
    br.sw_tgt = fm_maxf(br.sw_tgt, v);
    br.sw_att = 1.0f / (0.03f * EN_SR);
    br.sw_dec = tau(len);
}

/* -------------------------------------------------------------- melody --- */
/* the flute (one voice, legato: a slur glides) and the vibraphone (three, a motor's tremolo); the keys played
 * along have the vibraphone's other four */
#define NMV 7
#define MV_PLAYER 3
static struct {
    float ph, inc, tinc, amp, att, att_inc, dec, rel, b1, b1d, b2, b2d, vib, chiff;
    float n_lo, n_bd;
    int32_t gate;                                /* samples until released; < 0 held (the player) */
    uint8_t on, flute, note;
} mv[NMV];
static float mel_lp_l, vib_ph, flute_vph;
static int last_mel = -1;

static void vibe_on(int i, int note, float vel, int32_t gate)
{
    float f = mtof((float)note);
    mv[i].inc = mv[i].tinc = f / EN_SR;
    mv[i].ph = 0.0f;
    mv[i].on = 1;
    mv[i].flute = 0;
    mv[i].note = (uint8_t)note;
    mv[i].gate = gate;
    mv[i].amp = vel;
    mv[i].att = 0.0f;
    mv[i].att_inc = 1.0f / (0.0015f * EN_SR);
    mv[i].dec = tau(fm_clampf(2.6f * fm_sqrtf(440.0f / f), 1.2f, 3.6f));
    mv[i].rel = tau(0.22f);
    mv[i].b1 = vel * 0.30f;
    mv[i].b1d = tau(0.22f);
    mv[i].b2 = vel * 0.10f;
    mv[i].b2d = tau(0.025f);
}
static void flute_on(int note, float vel, int32_t gate)
{
    float f = mtof((float)note);
    int legato = mv[0].on && mv[0].flute && mv[0].amp > 0.04f && (mv[0].gate > 0 || mv[0].att > 0.5f);
    mv[0].tinc = f / EN_SR;
    if (!legato) {
        mv[0].inc = mv[0].tinc * 0.994f;         /* (a breath's scoop up to it) */
        mv[0].ph = 0.0f;
        mv[0].att = 0.0f;
        mv[0].vib = 0.0f;
        mv[0].chiff = 0.5f * vel;
    } else {
        mv[0].chiff = 0.15f * vel;
        mv[0].vib *= 0.5f;
    }
    mv[0].on = 1;
    mv[0].flute = 1;
    mv[0].note = (uint8_t)note;
    mv[0].gate = gate;
    mv[0].amp = vel;
    mv[0].att_inc = 1.0f / (0.05f * EN_SR);
    mv[0].dec = tau(7.0f);
    mv[0].rel = tau(0.08f);
}
static void mel_on(int lead, int note, float vel, int32_t gate)
{
    if (vel * 0.5f > en_accent)
        en_accent = vel * 0.5f;
    if (lead == 0) {
        flute_on(note, vel, gate);
    } else {
        int i, best = 0;
        float lo = 9.0f;
        for (i = 0; i < MV_PLAYER; i++) {
            if (!mv[i].on) {
                best = i;
                break;
            }
            if (mv[i].amp < lo)
                lo = mv[i].amp, best = i;
        }
        vibe_on(best, note, vel, gate);
    }
}
static void player_on(int note, float vel)
{
    int i, best = MV_PLAYER;
    float lo = 9.0f;
    for (i = MV_PLAYER; i < NMV; i++) {
        if (!mv[i].on) {
            best = i;
            break;
        }
        if (mv[i].amp < lo)
            lo = mv[i].amp, best = i;
    }
    vibe_on(best, note, vel, -1);
}
static void player_off(int note)
{
    int i;
    for (i = MV_PLAYER; i < NMV; i++)
        if (mv[i].on && mv[i].note == note && mv[i].gate < 0)
            mv[i].gate = 0;
}

/* -------------------------------------------------------------- spaces --- */
/* the room: four delay lines, a Householder mix, damped; two diffusers before */
#define RV0 1693
#define RV1 2053
#define RV2 2399
#define RV3 2711
#define AP0 331
#define AP1 503
static float rv_buf[RV0 + RV1 + RV2 + RV3 + AP0 + AP1] EN_POOL;
static float *rv_d[4];
static const int RV_N[4] = {RV0, RV1, RV2, RV3};
static int rv_p[4], ap_p[2];
static float *ap_d[2];
static const int AP_N[2] = {AP0, AP1};
static float rv_lp[4];

/* -------------------------------------------------------- the sequence --- */
#define BLK 256
#define NEV 64
enum { E_GTR, E_BASS, E_KICK, E_RIM, E_TAP, E_SWISH, E_MEL };
typedef struct {
    int32_t wait;
    uint8_t type, note, used, aux;
    float vel;
    int32_t gate;
} ev_t;
static ev_t evq[NEV];

static void sched(int type, int32_t wait, int note, float vel, int32_t gate, int aux)
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
            evq[i].aux = (uint8_t)aux;
            return;
        }
}

/* the melody's notes: onset and length in eighths from the section's start */
typedef struct {
    uint8_t on, dur, pitch, vel;
} mn_t;
#define MN_MAX 100
#define NSLOT 5                                  /* the heads: A, a, B, C and the last section of the last chorus */
enum { SL_A, SL_a, SL_B, SL_C, SL_LAST };
static mn_t hm[NSLOT][MN_MAX], sm[2][MN_MAX];
static uint8_t hm_n[NSLOT], sm_n[2];
typedef struct {
    uint32_t r;
    int prev, piv, rep, c1, c2, cad, home, rest;
} gst_t;

/* the tune */
static struct {
    const form_t *fm;
    int form;
    int nlet, fbars, nch, bars;                  /* letters in the layout, bars a chorus, choruses, bars in all */
    int last_slot;                               /* the layout's last section (its plain version) */
    int key, gpat, bfig, kit, cells, lead, mod, tempo_off;
    int lo, hi, home;                            /* the melody's range */
    int bar, step;                               /* where the sequence is */
    float step_left;                             /* samples to the next sixteenth */
    float rit;                                   /* the ending's slowing */
    int voicing[4];                              /* the guitar's fingers now */
    int antic;                                   /* the fingers struck this half bar's chord early */
    int thumb;                                   /* the thumb's last note */
    int bass_last;
    int gen_slot, gen_p;                         /* the heads being written, a phrase at a time */
    gst_t gs_head[NSLOT], gs_solo;
    int mel_src, mel_i, mel_occ;                 /* the melody playing: its notes (slot, or 8+ a solo buffer) */
    float dyn;                                   /* the combo's level for this part */
} tk;

static float step_len = 5000.0f;                 /* samples a sixteenth */
static uint32_t trk_samples;                     /* into the tune */
static int gap_left;                             /* between tunes: samples to the next */
static float fade = 0.0f, fade_t = 1.0f, fade_rate = 0.0f;   /* the music's level (the transitions) */
static int skipping;
static float tape_t;

static int cur_bpm(void)
{
    int b = par[P_TEMPO] + tk.tempo_off;
    return b < 60 ? 60 : b > 150 ? 150 : b;
}
static void tempo_set(void)
{
    int b = cur_bpm();
    en_bpm = (uint8_t)b;
    step_len = EN_SR * 15.0f / (float)b * tk.rit;
    en_tune_len_ms = (uint32_t)((float)(tk.bars * 16) * (EN_SR * 15.0f / (float)b) * (1000.0f / EN_SR));
}

/* ---- the form: sections, where a bar is, the chord there */
static int slot_of(char c) { return c == 'A' ? SL_A : c == 'a' ? SL_a : c == 'B' ? SL_B : SL_C; }
static const sect_t *slot_sect(int slot)
{
    if (slot == SL_LAST) {
        char c = tk.fm->layout[tk.nlet - 1];
        if (c == 'A' && tk.fm->s[SL_a].n > 1)    /* the last A ends home */
            c = 'a';
        slot = slot_of(c);
    }
    return &tk.fm->s[slot];
}
static int sect_halves(const sect_t *s)
{
    int k, n = 0;
    for (k = 0; k < s->n; k++)
        n += s->c[k].h;
    return n;
}
static int slot_bars(int slot) { return sect_halves(slot_sect(slot)) / 2; }
/* the chord at half bar h of a section: root above the key, quality, bass (above the key, or -1). The last
 * section of the last chorus ends on the tonic */
static void slot_chord(int slot, int h, int *r, int *qq, int *bb)
{
    const sect_t *s = slot_sect(slot);
    int k, at = 0, n = sect_halves(s);
    if (slot == SL_LAST && h >= n - 2) {
        *r = 0;
        *qq = tk.fm->minor ? Q_M69 : Q_69;
        *bb = -1;
        return;
    }
    h = n ? h % n : 0;
    for (k = 0; k < s->n; k++) {
        if (h < at + s->c[k].h) {
            *r = s->c[k].r;
            *qq = s->c[k].q;
            *bb = s->c[k].b;
            return;
        }
        at += s->c[k].h;
    }
    *r = 0;
    *qq = Q_MAJ7;
    *bb = -1;
}

typedef struct {
    int part, chorus, li, slot, sbar, sbars, keyoff, ebar;
} where_t;
static void where(int bar, where_t *w)
{
    int b, c, fb, i;
    w->keyoff = 0;
    w->li = -1;
    w->chorus = 0;
    w->ebar = 0;
    if (bar < INTRO_BARS) {                      /* the intro: the form's last four bars, its turnaround */
        w->part = PT_INTRO;
        w->slot = tk.last_slot;
        w->sbars = slot_bars(w->slot);
        w->sbar = w->sbars - INTRO_BARS + bar;
        return;
    }
    b = bar - INTRO_BARS;
    c = b / tk.fbars;
    if (c >= tk.nch) {
        w->part = PT_END;
        w->chorus = tk.nch - 1;
        w->ebar = b - tk.nch * tk.fbars;
        w->keyoff = tk.mod;
        w->slot = SL_LAST;
        w->sbar = 0;
        w->sbars = END_BARS;
        return;
    }
    fb = b % tk.fbars;
    w->chorus = c;
    w->part = c == 0 ? PT_HEAD : c == tk.nch - 1 ? PT_OUT : PT_SOLO;
    w->keyoff = c == tk.nch - 1 ? tk.mod : 0;
    for (i = 0; i < tk.nlet; i++) {
        int sl = (c == tk.nch - 1 && i == tk.nlet - 1) ? SL_LAST : slot_of(tk.fm->layout[i]), n = slot_bars(sl);
        if (fb < n) {
            w->li = i;
            w->slot = sl;
            w->sbar = fb;
            w->sbars = n;
            return;
        }
        fb -= n;
    }
    w->li = tk.nlet - 1;                         /* (never: the layout covers the chorus) */
    w->slot = SL_A;
    w->sbar = 0;
    w->sbars = 8;
}
/* the ending's tag: twice round a turnaround, then home, held */
static void end_chord(int ebar, int half, int *r, int *qq)
{
    static const int8_t MAJ[4][2] = {{4, Q_M7}, {9, Q_7B9}, {2, Q_M7}, {7, Q_13}};
    static const int8_t MIN[4][2] = {{0, Q_M9}, {8, Q_9}, {2, Q_M7B5}, {7, Q_7B9}};
    int k = (ebar & 1) * 2 + half;
    if (ebar >= 4) {
        *r = 0;
        *qq = tk.fm->minor ? Q_M69 : Q_69;
        return;
    }
    *r = tk.fm->minor ? MIN[k][0] : MAJ[k][0];
    *qq = tk.fm->minor ? MIN[k][1] : MAJ[k][1];
}
/* the chord sounding at a bar's half: its root as a pitch class, its quality, its bass as a pitch class */
static void chord_at(int bar, int half, int *root, int *qq, int *bass)
{
    where_t w;
    int r, b = -1;
    where(bar < tk.bars ? bar : tk.bars - 1, &w);
    if (w.part == PT_END)
        end_chord(w.ebar, half, &r, qq);
    else
        slot_chord(w.slot, w.sbar * 2 + half, &r, qq, &b);
    *root = (tk.key + w.keyoff + r) % 12;
    *bass = b < 0 ? *root : (tk.key + w.keyoff + b) % 12;
}
static int same_chord(int bar0, int h0, int bar1, int h1)
{
    int r0, q0, b0, r1, q1, b1;
    chord_at(bar0, h0, &r0, &q0, &b0);
    chord_at(bar1, h1, &r1, &q1, &b1);
    return r0 == r1 && q0 == q1 && b0 == b1;
}

/* ------------------------------------------------------------ the melody --- */
/* rhythms, two bars of eighths each: x a note, - held, . a rest. The head's phrases: a call, its answer in the
 * same rhythm, a contrast and its answer, a cadence that holds; the solo's busier, with rests between */
static const char *const CELL_HEAD[8] = {
    "x-x--x-xx-----..", "x--x--x-x-----..", "..x-x-x--x----..", ".x-x-x-x-x----..",
    "x-x-x--x--x-x-..", "x--x--x---x-x-..", "x-xx-x--x-----..", "..x--x--x-x---..",
};
static const uint8_t CELL_SET[3] = {0xAF, 0x7B, 0xA7};   /* as written, busier, sparer */
static const char *const CELL_CAD[5] = {
    "x-x-x-x-x-------", "..x-x-x-x-------", "x--x--x-x-------", ".x-x-x--x-------", "x-x-x---x-------",
};
static const char *const CELL_SOLO[8] = {
    "xxxx-x-x-x-x--..", ".xxxx-x-xxx-x-..", "x-x-xxx-x--x-x..", "..xxxxx-x-xx-x..",
    "x--x--x-xxxx-x..", "xxx-x-x-x-----..", ".x-xx-xx-x-x--..", "x-x-x-x-xxxx-x..",
};
static int pick_cell(uint32_t *r, int not)
{
    uint8_t set = CELL_SET[tk.cells < 0 || tk.cells > 2 ? 0 : tk.cells];
    int i, tries;
    for (tries = 0; tries < 16; tries++) {
        i = uirand(r, 8);
        if ((set >> i & 1u) && i != not)
            return i;
    }
    return 0;
}

/* one note's pitch: chord tones (or a resting tension) on the strong beats and long notes, the key's scale (or
 * the chord's own, when it is foreign to the key) between; small steps, a leap answered by a step back, towards
 * the phrase's goal; a phrase ends open (third, fifth), a section's last note home */
static int pick_pitch(gst_t *g, int r, int qq, int strength, int final, int cad, int dur, int desired)
{
    uint16_t keysc = tk.fm->minor ? SC_AEO : SC_ION, tones = rot12(Q_TONES[qq], r), tens = rot12(Q_TENS[qq], r);
    uint16_t scale, avoid;
    int c, best = -1, lo = g->prev - 9, hi = g->prev + 9;
    float bc = 1e9f;
    if ((tones & ~keysc) == 0)
        scale = (uint16_t)(keysc | tones);
    else
        scale = (uint16_t)(rot12(Q_SCALE[qq], r) | tones);
    tens &= scale;
    avoid = (uint16_t)(rot12(tones, 1) & ~(tones | tens));
    if (lo < tk.lo)
        lo = tk.lo;
    if (hi > tk.hi)
        hi = tk.hi;
    for (c = lo; c <= hi; c++) {
        int pc = (c - tk.key + 120) % 12, rel = (pc - r + 12) % 12, iv = c - g->prev, a = iv < 0 ? -iv : iv;
        uint16_t bit = (uint16_t)(1u << pc);
        int tone = (tones & bit) != 0, ten = (tens & bit) != 0;
        float cost = 0.0f;
        if (!(scale & bit))
            continue;
        if ((strength == 2 || final) && !tone && !ten)
            continue;
        if (dur >= 2 && (avoid & bit))
            continue;
        if (strength == 1 && !tone)
            cost += ten ? 0.3f : 0.6f;
        if ((strength == 2 || final) && !tone)
            cost += final ? 0.5f : 0.7f;
        cost += a == 0 ? 1.3f : a <= 2 ? 0.0f : a <= 4 ? (tone ? 0.45f : 0.9f) : a <= 7 ? 2.4f : 5.0f;
        if (g->piv != 0 && c == g->prev - g->piv && a <= 2)
            cost += 1.1f;                        /* not back and forth: a neighbour now and then, not a trill */
        if ((g->piv >= 5 || g->piv <= -5) && (a > 2 || iv * g->piv > 0))
            cost += 2.5f;                        /* a leap: then a step back */
        if (a == 0 && g->rep)
            cost += 3.0f;
        cost += 0.16f * (float)(c > desired ? c - desired : desired - c);
        if (cad)
            cost += rel == 0 ? 0.0f : (rel == 3 || rel == 4) ? 0.4f : rel == 7 ? 0.9f : 1.5f;
        else if (final)
            cost += (rel == 3 || rel == 4) ? 0.0f : rel == 7 ? 0.2f : rel == 0 ? 0.35f : 0.6f;
        if (c <= tk.lo + 1 || c >= tk.hi - 1)
            cost += 0.8f;
        cost += urand(&g->r) * 0.7f;
        if (cost < bc)
            bc = cost, best = c;
    }
    if (best < 0) {                              /* (nothing in reach: the nearest chord tone) */
        int d = 99;
        for (c = tk.lo; c <= tk.hi; c++) {
            int pc = (c - tk.key + 120) % 12, e = c > g->prev ? c - g->prev : g->prev - c;
            if ((tones >> pc & 1u) && e < d)
                d = e, best = c;
        }
        if (best < 0)
            best = g->prev;
    }
    g->rep = best == g->prev;
    g->piv = best - g->prev;
    g->prev = best;
    return best;
}

/* one phrase (two bars) of a section's melody, after the notes already there */
static void gen_phrase(mn_t *dst, uint8_t *dn, int slot, int p, int np, gst_t *g, int solo)
{
    static const int8_t TG[4] = {3, -1, 6, -2};
    const char *cell;
    int e, last = -1, start = g->prev, tgt, nh = slot_bars(slot) * 2;
    if (solo) {
        if ((p & 1) && p < np - 1 && !g->rest && urand(&g->r) < 0.25f) {
            g->rest = 1;                         /* a phrase of space */
            return;
        }
        g->rest = 0;
        if (p == np - 1)
            cell = CELL_CAD[uirand(&g->r, 5)];
        else if (tk.cells == 2)
            cell = CELL_HEAD[pick_cell(&g->r, -1)];
        else
            cell = CELL_SOLO[uirand(&g->r, 8)];
        tgt = g->home + uirand(&g->r, 13) - 5;
    } else {
        cell = p == np - 1 ? CELL_CAD[g->cad] : CELL_HEAD[(p & 2) ? g->c2 : g->c1];
        tgt = p == np - 1 ? g->home - 3 : g->home + TG[p & 3] + (slot == SL_B ? 2 : 0);
    }
    for (e = 0; e < 16; e++)
        if (cell[e] == 'x')
            last = e;
    for (e = 0; e < 16; e++) {
        int dur = 1, on8 = p * 16 + e, half, h2, strength, r, qq, bb, pitch, desired;
        float v;
        if (cell[e] != 'x')
            continue;
        while (e + dur < 16 && cell[e + dur] == '-')
            dur++;
        half = on8 / 4;
        strength = (e & 3) == 0 ? 2 : (e & 1) == 0 ? 1 : 0;
        if (dur >= 3)
            strength = 2;
        h2 = half;
        if ((e & 3) == 3 && dur >= 2 && half + 1 < nh) {   /* an anticipation: the next chord's */
            h2 = half + 1;
            strength = 2;
        }
        slot_chord(slot, h2, &r, &qq, &bb);
        desired = start + (tgt - start) * (e + 1) / 16;
        pitch = pick_pitch(g, r, qq, strength, e == last, e == last && p == np - 1, dur, desired);
        v = strength == 2 ? 0.84f : strength == 1 ? 0.74f : 0.64f;
        v += 0.012f * (float)(pitch - g->home) + 0.08f * (urand(&g->r) - 0.5f);
        if (*dn < MN_MAX) {
            mn_t *m = &dst[(*dn)++];
            m->on = (uint8_t)on8;
            m->dur = (uint8_t)dur;
            m->pitch = (uint8_t)pitch;
            m->vel = (uint8_t)(fm_clampf(v, 0.3f, 1.0f) * 100.0f);
        }
    }
}

static int slot_used(int slot)
{
    const char *l = tk.fm->layout;
    int i;
    if (slot == SL_LAST)
        return 1;
    for (i = 0; l[i]; i++)
        if (slot_of(l[i]) == slot && (slot != SL_a || tk.fm->s[SL_a].n > 1))
            return 1;
    return 0;
}
static char slot_family(int slot)                /* the heads that share a melody: A, a and a last A */
{
    const sect_t *s = slot_sect(slot);
    return s == &tk.fm->s[SL_B] ? 'B' : s == &tk.fm->s[SL_C] ? 'C' : 'A';
}
static void gst_init(gst_t *g, uint32_t seed)
{
    g->r = seed;
    g->home = tk.home + uirand(&g->r, 5) - 2;
    g->prev = g->home + uirand(&g->r, 5) - 2;
    g->piv = g->rep = g->rest = 0;
    g->c1 = pick_cell(&g->r, -1);
    g->c2 = pick_cell(&g->r, g->c1);
    g->cad = uirand(&g->r, 5);
}
/* the heads, a phrase each sixteenth (all are written in the intro's four bars) */
static void gen_next(void)
{
    int np;
    while (tk.gen_slot < NSLOT && !slot_used(tk.gen_slot))
        tk.gen_slot++;
    if (tk.gen_slot >= NSLOT)
        return;
    np = slot_bars(tk.gen_slot) / 2;
    if (tk.gen_p == 0) {
        hm_n[tk.gen_slot] = 0;
        gst_init(&tk.gs_head[tk.gen_slot], en_tune_rand(en_tune_seed, 100u + (uint32_t)slot_family(tk.gen_slot)));
    }
    gen_phrase(hm[tk.gen_slot], &hm_n[tk.gen_slot], tk.gen_slot, tk.gen_p, np, &tk.gs_head[tk.gen_slot], 0);
    if (++tk.gen_p >= np) {
        tk.gen_p = 0;
        tk.gen_slot++;
    }
}

/* ------------------------------------------------------------ the combo --- */
/* the guitar's fingers, two bars of eighths: X a stressed chord, x a chord. The thumb plays 1 and 3 */
static const char *const GPAT[G_N] = {
    "X..x..x...x..x..",                          /* the bossa clave (3-2) */
    "..x..x..X..x..x.",                          /* turned round (2-3) */
    "X.xx.x.x.xx.x.x.",                          /* the batida: busier, pairs of offbeats */
    "X..x....X..x....",                          /* a ballad's: on 1 and the and of 2, rolled */
    "x.....x...x.....",                          /* sparse, out of the vibraphone's way */
};
/* the bass, a bar of eighths: R the root (r softer), F the fifth (f softer), N the next bar's root early */
static const char *const BFIG[B_N] = {"R..rF..f", "R..FF..N", "R...F...", "R......N"};

static void voice(int root, int qq)
{
    int pcs[4], best[4], bestd = 1 << 30, i, inv, sh;
    for (i = 0; i < 4; i++) {
        pcs[i] = (root + Q_GTR[qq][i]) % 12;
        best[i] = tk.voicing[i];
    }
    for (inv = 0; inv < 4; inv++) {              /* each tone at the bottom of a close stack, then drop 2 */
        int n[4], m[4], j;
        n[0] = 48 + ((pcs[inv] - 48) % 12 + 12) % 12;
        for (j = 1; j < 4; j++) {
            int pc = pcs[(inv + j) % 4], x = n[j - 1] + ((pc - n[j - 1]) % 12 + 12) % 12;
            n[j] = x == n[j - 1] ? x + 12 : x;
        }
        m[0] = n[2] - 12, m[1] = n[0], m[2] = n[1], m[3] = n[3];
        for (sh = -12; sh <= 12; sh += 12) {
            int d = 0, mean = 0, k;
            for (j = 0; j < 4; j++) {
                int mm = 99;
                for (k = 0; k < 4; k++) {
                    int x = m[j] + sh - tk.voicing[k];
                    x = x < 0 ? -x : x;
                    mm = x < mm ? x : mm;
                }
                d += mm;
                mean += m[j] + sh;
            }
            mean /= 4;
            d += (mean > 60 ? mean - 60 : 60 - mean) / 2;
            if (m[3] + sh > 70)
                d += (m[3] + sh - 70) * 4;
            if (m[0] + sh < 47)
                d += (47 - m[0] - sh) * 4;
            if (d < bestd) {
                bestd = d;
                for (j = 0; j < 4; j++)
                    best[j] = m[j] + sh;
            }
        }
    }
    for (i = 0; i < 4; i++)
        tk.voicing[i] = best[i];
}

static int32_t swing_off(int s)                  /* the offbeat eighths late (Feel), the sixteenths between */
{
    static const float W[4] = {0.0f, 0.12f, 0.42f, 0.18f};
    return (int32_t)(W[s & 3] * step_len * (float)par[P_FEEL] * 0.01f);
}
static int32_t hum(float ms) { return (int32_t)((frand() * 2.0f - 1.0f) * ms * 44.1f); }
static int32_t ms(float m) { return (int32_t)(m * 44.1f); }
static float bright(void) { return 0.55f + 0.45f * (float)par[P_TONE] * 0.01f; }

static void chord_show(int root, int qq)
{
    int i;
    uint16_t m = (uint16_t)(1u << root);
    en_chord_root = (uint8_t)root;
    en_chord_q = (uint8_t)qq;
    for (i = 0; i < 4; i++)
        m |= (uint16_t)(1u << ((root + Q_GTR[qq][i]) % 12));
    en_chord_pcs = m;
}

static void play_chord(int root, int qq, int32_t at, float vel, int32_t gate, float roll_ms)
{
    int i;
    voice(root, qq);
    for (i = 0; i < 4; i++) {
        float v = vel * (0.86f + 0.14f * frand()) * (i == 3 ? 1.1f : 1.0f);
        int32_t off = roll_ms > 0.0f ? ms(roll_ms * (float)i) : ms(4.0f * frand());
        sched(E_GTR, at + off, tk.voicing[i], v, gate - off, 2 + i);
        DBG(0, tk.voicing[i], v, (int)((float)gate / step_len));
    }
}
static int thumb_note(int pc, int fifth_of)      /* E2 .. D#3; a fifth up, or down to stay there */
{
    int n = 40 + (pc - 4 + 12) % 12;
    if (fifth_of >= 0) {
        n = 40 + (fifth_of - 4 + 12) % 12 + (pc - fifth_of + 12) % 12;
        if (n > 52)
            n -= 12;
    }
    return n;
}
static int bass_note(int pc) { return 31 + (pc - 7 + 12) % 12; }   /* G1 .. F#2 */

static void guitar_step(int bar, int s, const where_t *w, int32_t sw)
{
    const char *pat = GPAT[tk.gpat];
    int e = s >> 1, cyc = (bar & 1) * 8 + e, ballad = tk.gpat >= G_BALLAD, maxd = ballad ? 6 : 3;
    char c = pat[cyc];
    int32_t lay = ms(3.0f + 6.0f * (float)par[P_FEEL] * 0.01f);
    float tv;
    if ((e & 3) == 0 && tk.antic) {              /* struck early: the fingers hold */
        tk.antic = 0;
        c = '.';
    }
    if (c != '.') {
        int half = e >> 2, rr, qq, bpc, d = 1, ante = 0;
        chord_at(bar, half, &rr, &qq, &bpc);
        if ((e & 3) == 3 && !(w->part == PT_END && w->ebar >= 3)) {   /* the and before a change: the next chord */
            int nb = bar + half, nh = half ^ 1, nr, nq, nbp;
            chord_at(nb, nh, &nr, &nq, &nbp);
            if (nr != rr || nq != qq) {
                rr = nr, qq = nq, ante = 1;
                tk.antic = 1;
            }
        }
        while (d < maxd && pat[(cyc + d) & 15] == '.')
            d++;
        play_chord(rr, qq, sw + lay + hum(4.0f),
                   (c == 'X' ? 0.85f : 0.68f) * tk.dyn * (ante ? 1.05f : 1.0f) * (0.92f + 0.16f * frand()),
                   (int32_t)((float)d * 2.0f * step_len * 0.9f), ballad ? 20.0f : 0.0f);
    }
    if (s == 0 || s == 8) {                      /* the thumb: the root on 1, the fifth on 3 (or the new root) */
        int rr, qq, bpc, n;
        chord_at(bar, s >> 3, &rr, &qq, &bpc);
        if (s == 8 && same_chord(bar, 0, bar, 1))
            n = thumb_note((rr + q_fifth(qq)) % 12, bpc);
        else
            n = thumb_note(bpc, -1);
        tv = (par[P_BASS] > 0 ? 0.5f : 0.8f) * tk.dyn * (s == 0 ? 1.0f : 0.85f);
        sched(E_GTR, sw + lay - ms(6.0f), n, tv, (int32_t)(step_len * (ballad ? 7.0f : 3.5f)), 0);
        tk.thumb = n;
        DBG(0, n, tv, ballad ? 7 : 4);
    }
}

static void bass_step(int bar, int s, const where_t *w, int32_t sw)
{
    int e = s >> 1, half = e >> 2, rr, qq, bpc, n = 0, d = 1;
    char c = BFIG[tk.bfig][e];
    float v = 0.0f;
    if (c == '.')
        return;
    chord_at(bar, half, &rr, &qq, &bpc);
    if (half == 1 && !same_chord(bar, 0, bar, 1))   /* the chord changed on 3: its root */
        c = c == 'F' ? 'R' : c == 'f' ? 'r' : c;
    if (c == 'N') {
        int nr, nq, nb;
        if (bar + 1 >= tk.bars || (w->part == PT_END && w->ebar >= 3))
            return;
        chord_at(bar + 1, 0, &nr, &nq, &nb);
        if (nr != rr || nq != qq)
            n = bass_note(nb), v = 0.72f;
        else if (tk.bfig == B_ANTIC)
            c = 'f';
        else
            return;
    }
    if (c == 'R' || c == 'r') {
        n = bass_note(bpc);
        v = c == 'R' ? 0.92f : 0.6f;
    } else if (c == 'F' || c == 'f') {
        n = bass_note(rr) + q_fifth(qq);
        if (n > 45)
            n -= 12;
        v = c == 'F' ? 0.82f : 0.58f;
    }
    while (e + d < 8 && BFIG[tk.bfig][e + d] == '.')
        d++;
    if (w->part == PT_INTRO)
        v *= 0.75f;
    v *= tk.dyn * (0.92f + 0.12f * frand());
    sched(E_BASS, sw + ms(2.0f) + hum(3.0f), n, v, (int32_t)((float)d * 2.0f * step_len * 0.92f) - 100, 0);
    tk.bass_last = n;
    DBG(1, n, v, d * 2);
}

/* the brushes: a swirl round each half bar, taps on the eighths, a swish on 2 and 4; a rim click on the bossa
 * clave; a soft kick under the bass */
static void drums_step(int bar, int s, const where_t *w, int32_t sw)
{
    int kit = tk.kit, p = bar & 1, rimq;
    float kv = 0.0f, dy = tk.dyn, ghost = w->part == PT_SOLO ? 0.3f : 0.15f;
    int32_t rl = ms(4.0f + 14.0f * (float)par[P_FEEL] * 0.01f);
    if (s == 0 || s == 8)
        br.ph = 0.0f;
    if (par[P_DRUMS] <= 0 || (w->part == PT_INTRO && bar < 2)) {
        br.lvl_t = 0.0f;
        return;
    }
    if (w->part == PT_INTRO) {                   /* the last two bars of the intro: the swirl comes in */
        br.lvl_t = 0.45f;
        if (s == 4 || s == 12)
            sched(E_SWISH, sw + hum(3.0f), 0, 0.25f, 110, 0);
        return;
    }
    if (w->part == PT_END && w->ebar >= 4) {
        br.lvl_t = 0.0f;
        return;
    }
    br.lvl_t = kit == K_BALLAD ? 0.85f : kit == K_SAMBA ? 0.35f : kit == K_LIGHT ? 0.5f : 0.55f;
    if (kit == K_BALLAD)
        kv = s == 0 ? 0.42f : (s == 8 && (en_tune_rand(en_tune_seed, (uint32_t)bar) & 1u)) ? 0.25f : 0.0f;
    else if (kit != K_LIGHT)
        kv = (s == 0 ? 0.6f : s == 6 ? 0.32f : s == 8 ? 0.5f : s == 14 ? 0.32f : 0.0f) * (kit == K_SAMBA ? 1.15f : 1.0f);
    if (kv > 0.0f) {
        sched(E_KICK, sw + hum(2.0f), 0, kv * dy, 0, 0);
        DBG(3, 36, kv * dy, 1);
    }
    if (kit == K_BALLAD)
        rimq = s == 12 && p;
    else {
        int pp = tk.gpat == G_23 ? !p : p;
        rimq = pp == 0 ? (s == 0 || s == 6 || s == 12) : (s == 4 || s == 10);
    }
    if (rimq) {
        float v = (kit == K_BALLAD ? 0.32f : 0.5f) * dy * (0.9f + 0.15f * frand());
        sched(E_RIM, sw + rl + hum(3.0f), 0, v, 0, 0);
        DBG(4, 37, v, 1);
    }
    if (s == 4 || s == 12)
        sched(E_SWISH, sw + hum(3.0f), 0, (kit == K_BALLAD ? 0.55f : 0.36f) * dy, kit == K_BALLAD ? 170 : 95, 0);
    else if (!(s & 1))
        sched(E_TAP, sw + hum(3.0f), 0, (kit == K_BALLAD ? 0.13f : (s & 3) == 2 ? 0.3f : 0.24f) * dy, 0, 0);
    else if (kit == K_SAMBA)
        sched(E_TAP, sw + hum(2.0f), 0, ((s & 3) == 3 ? 0.3f : 0.15f) * dy, 0, 0);
    else if (frand() < ghost)
        sched(E_TAP, sw + hum(3.0f), 0, 0.1f * dy, 0, 0);
    if (w->sbar == w->sbars - 1 && s == 14 && (en_tune_rand(en_tune_seed, (uint32_t)bar * 7u) & 1u))
        sched(E_SWISH, sw + hum(3.0f), 0, 0.42f * dy, 140, 0);   /* the section's end */
    if (s == 0 && w->sbar == 0 && w->li == 0 && w->chorus > 0 && w->part != PT_END)
        sched(E_SWISH, hum(3.0f), 0, 0.5f * dy, 380, 0);         /* the top of a chorus */
}

static void melody_step(int bar, int s, const where_t *w, int32_t sw)
{
    int e8, occ, src, n;
    const mn_t *m;
    float pres = (float)par[P_MELODY] * 0.01f;
    (void)bar;
    if (w->part != PT_HEAD && w->part != PT_SOLO && w->part != PT_OUT)
        return;
    occ = w->chorus * 8 + w->li;
    src = w->part == PT_SOLO ? 8 + (occ & 1) : w->slot;
    if (occ != tk.mel_occ || src != tk.mel_src) {
        tk.mel_occ = occ;
        tk.mel_src = src;
        tk.mel_i = 0;
    }
    m = src < 8 ? hm[src] : sm[src - 8];
    n = src < 8 ? hm_n[src] : sm_n[src - 8];
    e8 = w->sbar * 8 + (s >> 1);
    while (tk.mel_i < n && m[tk.mel_i].on < e8)
        tk.mel_i++;
    for (; tk.mel_i < n && m[tk.mel_i].on == e8; tk.mel_i++) {
        const mn_t *x = &m[tk.mel_i];
        int p = x->pitch + w->keyoff;
        float v = (float)x->vel * 0.01f * (0.45f + 0.55f * pres);
        int32_t g = (int32_t)((float)x->dur * 2.0f * step_len * (tk.lead ? 1.0f : 0.97f));
        if (pres <= 0.0f || (w->part == PT_SOLO && pres < 0.35f))
            continue;
        sched(E_MEL, sw + ms(8.0f + 28.0f * (float)par[P_FEEL] * 0.01f) + hum(7.0f), p, v, g, 0);
        last_mel = p;
        DBG(2, p, v, x->dur * 2);
    }
}

/* the ending: the tag's last bar slows, then home: the chord rolled and left to ring, the bass's root, a swish,
 * the melody's last note on a chord tone near where it was */
static void ending_step(int bar, int s, const where_t *w)
{
    int rr, qq, bpc;
    if (w->ebar == 3)
        tk.rit = 1.0f + 0.4f * (float)(s + 1) / 16.0f;
    if (w->ebar != 4 || s != 0)
        return;
    chord_at(bar, 0, &rr, &qq, &bpc);
    chord_show(rr, qq);
    play_chord(rr, qq, ms(30.0f), 0.72f, (int32_t)(step_len * 30.0f), 45.0f);
    sched(E_GTR, 0, thumb_note(bpc, -1), 0.6f, (int32_t)(step_len * 30.0f), 0);
    if (par[P_BASS] > 0)
        sched(E_BASS, ms(2.0f), bass_note(bpc), 0.85f, (int32_t)(step_len * 24.0f), 0);
    if (par[P_DRUMS] > 0) {
        sched(E_SWISH, 0, 0, 0.5f, 800, 0);
        if (tk.kit != K_LIGHT)
            sched(E_KICK, 0, 0, 0.3f, 0, 0);
    }
    if (par[P_MELODY] > 0 && last_mel > 0) {
        static const int8_t PREF[2][3] = {{2, 4, 7}, {2, 3, 7}};
        int i, o, best = last_mel, bd = 99, key = (tk.key + tk.mod) % 12;
        for (i = 0; i < 3; i++)
            for (o = tk.lo; o <= tk.hi + 1; o++)
                if ((o - key + 120) % 12 == PREF[tk.fm->minor][i]) {
                    int d = (o > last_mel ? o - last_mel : last_mel - o) + i;
                    if (d < bd)
                        bd = d, best = o;
                }
        sched(E_MEL, ms(60.0f), best, 0.62f * (0.45f + 0.55f * (float)par[P_MELODY] * 0.01f),
              (int32_t)(step_len * 26.0f), 0);
        DBG(2, best, 0.6f, 32);
    }
    fade_t = 0.0f;                               /* over the two bars it rings, and a breath */
    fade_rate = 1.0f / (32.0f * step_len + 0.6f * EN_SR);
}

/* the solo's phrases, written a phrase ahead */
static int loop_from(void) { return INTRO_BARS + (tk.nch > 2 ? 1 : 0) * tk.fbars; }
static int last_chorus_at(void) { return INTRO_BARS + (tk.nch - 1) * tk.fbars; }
static void solo_ahead(int bar)
{
    where_t w;
    int t = bar + 2, occ, buf, p;
    if (en_repeat_on && t >= last_chorus_at())
        t = t - last_chorus_at() + loop_from();
    where(t, &w);
    if (w.part != PT_SOLO)
        return;
    occ = w.chorus * 8 + w.li;
    buf = occ & 1;
    p = w.sbar / 2;
    if (p == 0)
        sm_n[buf] = 0;
    gen_phrase(sm[buf], &sm_n[buf], w.slot, p, w.sbars / 2, &tk.gs_solo, 1);
}

static void new_tune(void);
static void seq_step(void)
{
    int bar = tk.bar, s = tk.step;
    int32_t sw = swing_off(s);
    where_t w;
    if (tk.gen_slot < NSLOT)
        gen_next();
    where(bar, &w);
    if (s == 0) {
        en_part = (uint8_t)w.part;
        en_letter = (uint8_t)(w.li >= 0 ? tk.fm->layout[w.li] & ~0x20 : 0);
        en_chorus = (uint8_t)w.chorus;
        en_key = (uint8_t)((tk.key + w.keyoff) % 12);
        tk.dyn = w.part == PT_INTRO ? 0.85f : w.part == PT_SOLO ? 0.92f : w.part == PT_END ? (w.ebar < 2 ? 0.88f : 0.76f) : 1.0f;
        if (w.part == PT_SOLO && par[P_MELODY] < 35)
            tk.dyn = 1.0f;                       /* no solo: the guitar has it */
        if (!(bar & 1))
            solo_ahead(bar);
    }
    if ((s == 0 || s == 8) && !(w.part == PT_END && w.ebar >= 4)) {
        int rr, qq, bpc;
        chord_at(bar, s >> 3, &rr, &qq, &bpc);
        chord_show(rr, qq);
    }
    if (w.part == PT_END)
        ending_step(bar, s, &w);
    if (!(s & 1)) {
        if (!(w.part == PT_END && w.ebar >= 4) && par[P_GUITAR] > 0)
            guitar_step(bar, s, &w, sw);
        if (par[P_BASS] > 0 && !(w.part == PT_INTRO && bar < 2) && !(w.part == PT_END && w.ebar >= 4))
            bass_step(bar, s, &w, sw);
        melody_step(bar, s, &w, sw);
    }
    drums_step(bar, s, &w, sw);
    if (s % 4 == 0)
        en_beats++;
    if (++tk.step >= 16) {
        tk.step = 0;
        tk.bar++;
        if (en_repeat_on && tk.nch > 1 && tk.bar == last_chorus_at())
            tk.bar = loop_from();                /* repeat: round the solos again */
        if (tk.bar >= tk.bars) {                 /* the tune is over: a pause, then the next */
            en_part = PT_GAP;
            en_letter = 0;
            gap_left = (int)(EN_SR * 3.0f);
        }
    }
    tempo_set();
}

static int pick_form(int mood)
{
    float w[NFORMS], tot = 0.0f, r;
    int i;
    for (i = 0; i < NFORMS; i++) {
        float d = ((float)FORMS[i].mood - (float)mood) * (1.0f / 20.0f);
        w[i] = 1.0f / (1.0f + d * d * d * d);
        if (tk.fm && i == tk.form)               /* not the same one again */
            w[i] *= 0.1f;
        tot += w[i];
    }
    r = trand() * tot;
    for (i = 0; i < NFORMS; i++) {
        r -= w[i];
        if (r <= 0.0f)
            return i;
    }
    return NFORMS - 1;
}
static int pick_bit(uint32_t mask, int n)
{
    int i, k = 0, c;
    for (i = 0; i < n; i++)
        k += (int)(mask >> i & 1u);
    if (!k)
        return 0;
    c = tirand(k);
    for (i = 0; i < n; i++)
        if ((mask >> i & 1u) && c-- == 0)
            return i;
    return 0;
}
const char *en_form_name(void) { return tk.fm ? tk.fm->name : ""; }

static void new_tune(void)
{
    const style_t *st = sty(style_i);
    int i;
    en_tune_no++;
    en_tune_seed = en_tune_rand(en_seed_base, en_tune_no);
    trng = en_tune_seed;
    tk.form = pick_form(par[P_MOOD]);
    tk.fm = &FORMS[tk.form];
    tk.key = tirand(12);
    en_minor = tk.fm->minor;
    tk.tempo_off = tirand(7) - 3;
    tk.gpat = pick_bit(st->gpats, G_N);
    tk.bfig = pick_bit(st->bfigs, B_N);
    tk.kit = st->kit;
    tk.cells = st->cells;
    tk.nch = 2 + (par[P_LENGTH] < 0 ? 0 : par[P_LENGTH] > 2 ? 2 : par[P_LENGTH]);
    tk.mod = trand() < 0.4f;
    tk.lead = par[P_LEAD] >= 2 ? (int)(en_tune_no & 1u) : par[P_LEAD];
    en_lead = (uint8_t)tk.lead;
    tk.lo = tk.lead ? 64 : 67;
    tk.hi = tk.lead ? 84 : 88;
    tk.home = tk.lead ? 74 : 77;
    for (i = 0; tk.fm->layout[i]; i++)
        ;
    tk.nlet = i;
    tk.fbars = 0;
    for (i = 0; i < tk.nlet; i++)
        tk.fbars += slot_bars(slot_of(tk.fm->layout[i]));
    tk.last_slot = slot_of(tk.fm->layout[tk.nlet - 1]);
    tk.bars = INTRO_BARS + tk.nch * tk.fbars + END_BARS;
    tk.gen_slot = 0;
    tk.gen_p = 0;
    gst_init(&tk.gs_solo, en_tune_rand(en_tune_seed, 999u));
    sm_n[0] = sm_n[1] = 0;
    tk.rit = 1.0f;
    tk.bar = 0;
    tk.step = 0;
    tk.step_left = 0.0f;
    tk.antic = 0;
    tk.mel_src = tk.mel_occ = -1;
    tk.mel_i = 0;
    tk.dyn = 0.85f;
    last_mel = -1;
    trk_samples = 0;
    en_tune_ms = 0;
    en_part = PT_INTRO;
    en_chorus = 0;
    en_choruses = (uint8_t)tk.nch;
    en_letter = 0;
    en_key = (uint8_t)tk.key;
    tempo_set();
    fade = 0.0f;
    fade_t = 1.0f;
    fade_rate = 1.0f / (0.3f * EN_SR);
    skipping = 0;
    en_tunes_played++;
}

/* ------------------------------------------------------------- control --- */
static void quiet(void)
{
    int i;
    for (i = 0; i < NGS; i++)
        gs[i].on = 0;
    for (i = 0; i < NMV; i++)
        mv[i].on = 0;
    for (i = 0; i < NEV; i++)
        evq[i].used = 0;
    bs.on = 0;
    bs.amp = 0.0f;
    kick.amp = rim.amp = rim.namp = br.tap = br.sw = br.sw_tgt = 0.0f;
}

static void play_on(int on)
{
    int i;
    if (on && !en_playing) {
        en_playing = 1;
        if (fade_t > 0.5f || en_part == PT_INTRO) {
            fade = 0.0f;
            fade_rate = 1.0f / (0.4f * EN_SR);
        }
    } else if (!on && en_playing) {
        en_playing = 0;
        for (i = 0; i < NEV; i++)
            evq[i].used = 0;
        for (i = 0; i < NGS; i++)
            if (gs[i].on)
                gs[i].gate = 1;
        for (i = 0; i < MV_PLAYER; i++)
            mv[i].gate = 0;
        bs.gate = 0;
        br.lvl_t = 0.0f;
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
        case CM_SET:
            if (a < P_NPARAMS) {
                const en_param_t *p = &PARAMS[a];
                par[a] = (int16_t)(b < p->lo ? p->lo : b > p->hi ? p->hi : b);
                if (a == P_TEMPO)
                    tempo_set();
            }
            break;
        case CM_STYLE: style_i = a % EN_NSTYLES; break;
        case CM_PLAY: play_on(a); break;
        case CM_SKIP:
            if (en_part != PT_GAP && !skipping) {
                skipping = 1;
                fade_t = 0.0f;
                fade_rate = 1.0f / (1.2f * EN_SR);
            }
            break;
        case CM_REPEAT: en_repeat_on = (uint8_t)(a != 0); break;
        case CM_SEED_HI: en_seed_base = (en_seed_base & 0xFFFFu) | ub << 16; break;
        case CM_SEED_LO: en_seed_base = (en_seed_base & 0xFFFF0000u) | ub; break;
        case CM_TUNE_NO:
            en_tune_no = (uint32_t)a << 16 | ub;   /* the next is no + 1 */
            quiet();
            new_tune();
            break;
        case CM_NOTE:
            if (b > 0)
                player_on(a, (float)b * (1.0f / 127.0f));
            else
                player_off(a);
            break;
        case CM_PANIC: quiet(); break;
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
    for (i = 0; i < NGS; i++) {
        int k;
        for (k = 0; k < GS_N; k++)
            gs_buf[i][k] = 0.0f;
        gs[i].w = 0;
    }
    for (i = 0; i < 4; i++)
        tk.voicing[i] = 52 + 5 * i;
    tk.fm = 0;
    quiet();
    en_tune_no = 0;
    new_tune();
}

/* -------------------------------------------------------------- render --- */
static float bufl[BLK], bufr[BLK], rsend[BLK], gmono[BLK], esend[BLK], trem[BLK];
static float tone_a = 0.3f, t1l, t2l, t1r, t2r;  /* the bus's low-pass */
static float hp_l, hp_r;                         /* a DC / rumble cut */
static float lev = 0.0f;                         /* the leveller's envelope */
static float vu_acc_l, vu_acc_r;
static uint32_t vu_n;

static void render_guitar(uint32_t n)
{
    int i;
    uint32_t j;
    float lv = (float)par[P_GUITAR] * 0.01f * 0.62f, f = 2.0f * fm_sinf(FM_PI * 180.0f / EN_SR);
    int any = 0;
    for (j = 0; j < n; j++)
        gmono[j] = 0.0f;
    for (i = 0; i < NGS; i++) {
        float *b = gs_buf[i], c = gs[i].c, apx = gs[i].apx, apy = gs[i].apy, rho = gs[i].rho, pk = 0.0f;
        float gl = lv * (0.62f - 0.5f * gs[i].pan), gr = lv * (0.5f + 0.5f * gs[i].pan);
        int L = gs[i].L, w = gs[i].w;
        uint32_t seg, j0 = 0;
        if (!gs[i].on)
            continue;
        any = 1;
        while (j0 < n) {                         /* in two runs: ringing, then damped */
            seg = n - j0;
            if (gs[i].gate > 0 && (uint32_t)gs[i].gate < seg)
                seg = (uint32_t)gs[i].gate;
            for (j = j0; j < j0 + seg; j++) {
                float a = b[(w - L) & (GS_N - 1)], bb = b[(w - L - 1) & (GS_N - 1)], v = rho * 0.5f * (a + bb), y;
                y = c * v + apx - c * apy;
                apx = v;
                apy = y;
                b[w] = y;
                w = (w + 1) & (GS_N - 1);
                bufl[j] += y * gl;
                bufr[j] += y * gr;
                gmono[j] += y;
                pk = fm_maxf(pk, fm_fabsf(y));
            }
            if (gs[i].gate > 0) {
                gs[i].gate -= (int32_t)seg;
                if (gs[i].gate <= 0) {
                    gs[i].gate = 0;
                    rho = gs[i].rho = gs[i].rho_damp;
                }
            }
            j0 += seg;
        }
        gs[i].apx = apx, gs[i].apy = apy, gs[i].w = w;
        if (pk < (gs[i].gate == 0 ? 2e-4f : 2e-5f)) {
            gs[i].on = 0;
            for (j = 0; j < GS_N; j++)           /* (the string quiet: its line cleared for the next) */
                b[j] = 0.0f;
        }
    }
    if (!any)
        return;
    for (j = 0; j < n; j++) {                    /* the box: its low resonance, both sides; the room's send */
        float x = gmono[j] * lv, hp;
        g_body_lo += f * g_body_bd;
        hp = x - g_body_lo - 0.7f * g_body_bd;
        g_body_bd += f * hp;
        bufl[j] += g_body_bd * 0.22f;
        bufr[j] += g_body_bd * 0.22f;
        rsend[j] += x * 0.4f;
    }
}

static void render_bass(uint32_t n)
{
    uint32_t j;
    float lv = (float)par[P_BASS] * 0.01f * 0.42f, a = onepole(1100.0f), hold = tau(1.3f), rel = tau(0.05f);
    float idec = tau(0.045f), hdec = tau(0.3f), att = 1.0f / (0.004f * EN_SR);
    if (!bs.on)
        return;
    for (j = 0; j < n; j++) {
        float s = sn(bs.ph + sn(bs.ph) * bs.idx * 0.35f) + bs.h2 * sn(bs.ph * 2.0f);
        bs.ph = wrap1(bs.ph + bs.inc);
        if (bs.atk) {
            bs.amp += att * bs.peak;
            if (bs.amp >= bs.peak)
                bs.amp = bs.peak, bs.atk = 0;
        } else if (bs.gate > 0) {
            bs.amp *= hold;
        } else {
            bs.amp *= rel;
        }
        if (bs.gate > 0)
            bs.gate--;
        bs.idx *= idec;
        bs.h2 = 0.1f + (bs.h2 - 0.1f) * hdec;
        s *= bs.amp;
        s = s * (1.0f - 0.1f * s * s);
        bs.lp += (s - bs.lp) * a;
        bufl[j] += bs.lp * lv;
        bufr[j] += bs.lp * lv;
        rsend[j] += bs.lp * lv * 0.08f;
    }
    if (bs.gate <= 0 && bs.amp < 0.0005f && !bs.atk)
        bs.on = 0;
}

static void render_drums(uint32_t n)
{
    uint32_t j;
    float lv = (float)par[P_DRUMS] * 0.01f * 0.62f;
    float fr = 2.0f * fm_sinf(FM_PI * 1900.0f / EN_SR), fb = 2.0f * fm_sinf(FM_PI * 4200.0f / EN_SR);
    float tapd = tau(0.022f), dph = 1.0f / (8.0f * step_len);
    br.lvl += (br.lvl_t - br.lvl) * fm_minf((float)n / (EN_SR * 0.4f), 1.0f);
    if (lv <= 0.0f || (kick.amp < 1e-3f && rim.amp < 1e-3f && rim.namp < 1e-3f && br.lvl < 1e-3f && br.tap < 1e-3f &&
                       br.sw < 1e-3f && br.sw_tgt <= 0.0f))
        return;
    for (j = 0; j < n; j++) {
        float k = 0.0f, r = 0.0f, x, env, sw, pan, hp;
        if (kick.amp > 1e-3f) {
            kick.f = kick.fend + (kick.f - kick.fend) * kick.pdec;
            kick.ph = wrap1(kick.ph + kick.f / EN_SR);
            k = sn(kick.ph) * kick.amp;
            kick.amp *= kick.adec;
        }
        if (rim.amp > 1e-3f || rim.namp > 1e-3f) {
            x = noise() * rim.namp;
            rim.lo += fr * rim.bd;
            rim.bd += fr * (x - rim.lo - 0.4f * rim.bd);
            r = rim.bd * 0.6f + sn(rim.ph) * rim.amp;
            rim.ph = wrap1(rim.ph + rim.inc);
            rim.amp *= rim.dec;
            rim.namp *= rim.ndec;
        }
        /* the brushes: one noise, its loudness the swirl's swell, the taps and the swishes */
        sw = sn(br.ph * 0.5f);
        br.ph += dph;
        if (br.ph > 1.0f)
            br.ph = 1.0f;
        if (br.sw_tgt > 0.0f) {
            br.sw += br.sw_att * br.sw_tgt;
            if (br.sw >= br.sw_tgt)
                br.sw_tgt = 0.0f;
        } else {
            br.sw *= br.sw_dec;
        }
        br.tap *= tapd;
        env = br.lvl * 0.16f * (0.3f + 0.7f * sw * sw) + br.tap + br.sw * 0.7f;
        x = noise() * env;
        br.lo += fb * br.bd;
        hp = x - br.lo - 1.1f * br.bd;
        br.bd += fb * hp;
        x = br.bd + 0.35f * hp;
        pan = 0.25f * sn(br.ph * 0.5f + 0.25f);
        bufl[j] += (k * 0.85f + r * 0.8f + x * (0.6f - pan)) * lv;
        bufr[j] += (k * 0.85f + r * 0.45f + x * (0.6f + pan)) * lv;
        rsend[j] += (r * 0.5f + x * 0.25f) * lv;
    }
}

static void render_mel(uint32_t n)
{
    int i;
    uint32_t j;
    float lv = 0.30f, a = onepole(5200.0f), vtr = 5.4f / EN_SR, fvr = 5.0f / EN_SR, vin = 1.0f / (0.45f * EN_SR);
    int any = 0;
    for (i = 0; i < NMV; i++)
        any |= mv[i].on;
    if (!any) {
        for (j = 0; j < n; j++)
            mel_lp_l *= 0.99f;
        return;
    }
    for (j = 0; j < n; j++) {                    /* the vibraphone's motor */
        trem[j] = 1.0f - 0.3f * (0.5f + 0.5f * sn(vib_ph));
        vib_ph = wrap1(vib_ph + vtr);
        esend[j] = 0.0f;
    }
    for (i = 0; i < NMV; i++) {
        if (!mv[i].on)
            continue;
        if (mv[i].flute) {
            float ph = mv[i].ph, vph = flute_vph;
            for (j = 0; j < n; j++) {
                float s, nz, inc;
                mv[i].inc += (mv[i].tinc - mv[i].inc) * 0.0016f;
                mv[i].vib = fm_minf(mv[i].vib + vin, 1.0f);
                inc = mv[i].inc * (1.0f + 0.0045f * mv[i].vib * mv[i].vib * sn(vph));
                vph = wrap1(vph + fvr);
                s = sn(ph) + 0.22f * sn(ph * 2.0f) + 0.07f * sn(ph * 3.0f);
                ph = wrap1(ph + inc);
                nz = noise();                    /* breath: a band of noise, more at the start */
                mv[i].n_lo += (nz - mv[i].n_lo) * 0.3f;
                mv[i].n_bd += (mv[i].n_lo - mv[i].n_bd) * 0.06f;
                s += (mv[i].n_lo - mv[i].n_bd) * (0.06f + mv[i].chiff * 1.4f);
                mv[i].chiff *= 0.9992f;
                if (mv[i].att < 1.0f)
                    mv[i].att = fm_minf(mv[i].att + mv[i].att_inc, 1.0f);
                s *= mv[i].amp * mv[i].att * (1.0f + 0.04f * mv[i].vib * sn(vph + 0.3f));
                if (mv[i].gate > 0) {
                    mv[i].gate--;
                    mv[i].amp *= mv[i].dec;
                } else {
                    mv[i].amp *= mv[i].rel;
                }
                esend[j] += s * 0.9f;
            }
            mv[i].ph = ph;
            flute_vph = vph;
        } else {
            float ph = mv[i].ph;
            for (j = 0; j < n; j++) {
                float s = sn(ph) + mv[i].b1 * sn(ph * 4.0f) + mv[i].b2 * sn(ph * 10.0f);
                ph = wrap1(ph + mv[i].inc);
                if (mv[i].att < 1.0f)
                    mv[i].att = fm_minf(mv[i].att + mv[i].att_inc, 1.0f);
                s *= mv[i].amp * mv[i].att * trem[j];
                if (mv[i].gate != 0) {
                    if (mv[i].gate > 0)
                        mv[i].gate--;
                    mv[i].amp *= mv[i].dec;
                } else {
                    mv[i].amp *= mv[i].rel;
                }
                mv[i].b1 *= mv[i].b1d;
                mv[i].b2 *= mv[i].b2d;
                esend[j] += s * 0.75f;
            }
            mv[i].ph = ph;
        }
        if (mv[i].amp < 0.0008f)
            mv[i].on = 0;
    }
    for (j = 0; j < n; j++) {                    /* the melody: right of centre, to the room */
        mel_lp_l += (esend[j] - mel_lp_l) * a;
        bufl[j] += mel_lp_l * lv * 0.8f;
        bufr[j] += mel_lp_l * lv * 1.05f;
        rsend[j] += mel_lp_l * lv * 0.6f;
    }
}

static void render_space(uint32_t n)
{
    uint32_t j;
    float rv = (float)par[P_REVERB] * 0.01f, vs = 0.1f + 0.6f * rv, damp = 0.42f, g = 0.70f + 0.16f * rv;
    for (j = 0; j < n; j++) {
        float in = rsend[j] * vs, x[4], sum, yl, yr;
        int i;
        for (i = 0; i < 2; i++) {                /* the diffusers */
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
        bufl[j] += yl * 0.85f;
        bufr[j] += yr * 0.85f;
    }
}

/* the bus: low-passed (Tone), warmed, the transitions' fade; a DC cut, the leveller, MASTER */
static void render_bus(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    float fc, ta, lv_rel = tau(0.4f), lv_att = 1.0f - tau(0.002f), hp_a = onepole(30.0f), vol;
    fc = 2600.0f * fm_exp2f((float)par[P_TONE] * 0.026f);   /* 2.6 .. ~16 kHz */
    fc = fm_clampf(fc, 250.0f, 16000.0f);
    ta = onepole(fc);
    tone_a += (ta - tone_a) * 0.2f;
    for (j = 0; j < n; j++) {
        float l = bufl[j], r = bufr[j], x;
        t1l += (l - t1l) * tone_a;
        t2l += (t1l - t2l) * tone_a;
        t1r += (r - t1r) * tone_a;
        t2r += (t1r - t2r) * tone_a;
        l = t2l * fade;
        r = t2r * fade;
        fade = fade < fade_t ? fm_minf(fade + fade_rate, fade_t) : fm_maxf(fade - fade_rate, fade_t);
        l = l * (27.0f + l * l) / (27.0f + 9.0f * l * l);   /* warmth: a soft knee */
        r = r * (27.0f + r * r) / (27.0f + 9.0f * r * r);
        hp_l += (l - hp_l) * hp_a;
        hp_r += (r - hp_r) * hp_a;
        l -= hp_l;
        r -= hp_r;
        vu_acc_l += l * l;                       /* the VU reads the music, not MASTER */
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

static void control(uint32_t n)
{
    float k = (float)n / EN_SR;
    en_accent *= fm_expf(-k * 7.0f);
    tape_t += ((en_playing && en_part != PT_GAP ? 1.0f : 0.0f) - tape_t) * fm_minf(k * 4.0f, 1.0f);
    en_tape = tape_t;
    if (++vu_n >= 8u) {                          /* the VU: about 46 ms of RMS, then ballistics */
        float l = fm_sqrtf(vu_acc_l / (float)(n * 8u)), r = fm_sqrtf(vu_acc_r / (float)(n * 8u));
        en_vu_l = en_vu_l + (fm_minf(l * 4.0f, 1.2f) - en_vu_l) * 0.45f;
        en_vu_r = en_vu_r + (fm_minf(r * 4.0f, 1.2f) - en_vu_r) * 0.45f;
        vu_acc_l = vu_acc_r = 0.0f;
        vu_n = 0;
    }
}

static void fire(ev_t *e)
{
    switch (e->type) {
    case E_GTR: pluck(e->aux, e->note, e->vel, e->gate, bright()); break;
    case E_BASS: bass_on(e->note, e->vel, e->gate); break;
    case E_KICK: kick_hit(e->vel); break;
    case E_RIM: rim_hit(e->vel); break;
    case E_TAP: tap_hit(e->vel); break;
    case E_SWISH: swish_hit(e->vel, (float)e->gate * 0.001f); break;
    case E_MEL: mel_on(tk.lead, e->note, e->vel, e->gate); break;
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
            if (en_part == PT_GAP) {
                if (gap_left <= 0)
                    new_tune();
                else if ((int)k > gap_left)
                    k = (uint32_t)gap_left;
            } else if (skipping && fade < 0.002f) {   /* skipped: faded out, a short pause */
                en_part = PT_GAP;
                en_letter = 0;
                gap_left = (int)(EN_SR * 1.5f);
                skipping = 0;
                for (i = 0; i < NEV; i++)
                    evq[i].used = 0;
                for (i = 0; i < NGS; i++)
                    if (gs[i].on)
                        gs[i].gate = 1;
            } else {
                if (tk.step_left <= 0.0f) {
                    seq_step();
                    tk.step_left += step_len;
                }
                if (en_part != PT_GAP) {
                    seq = 1;
                    if ((float)k > tk.step_left)
                        k = (uint32_t)tk.step_left + 1u;
                }
            }
        }
        /* events due now fire; the chunk ends at the next one */
        for (i = 0; i < NEV; i++)
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
            tk.step_left -= (float)k;
            trk_samples += k;
            en_tune_ms = (uint32_t)((float)trk_samples * (1000.0f / EN_SR));
        } else if (en_playing && en_part == PT_GAP) {
            gap_left -= (int)k;
        }
        control(k);
        for (j = 0; j < k; j++)
            bufl[j] = bufr[j] = rsend[j] = 0.0f;
        render_guitar(k);
        render_bass(k);
        render_drums(k);
        render_mel(k);
        render_space(k);
        render_bus(out, k, gain);
        out += 2 * k;
        n -= k;
    }
}

#ifdef EN_HOST
/* the forms as written: each section's count right, its length whole bars (8, 12 or 16), an 'a' as long as its A */
int en_check_forms(void)
{
    int f, s, k, bad = 0;
    for (f = 0; f < NFORMS; f++) {
        const form_t *fm = &FORMS[f];
        for (s = 0; s < 4; s++) {
            const sect_t *x = &fm->s[s];
            int n = 0, h = 0, used = 0, i;
            for (i = 0; fm->layout[i]; i++)
                used |= slot_of(fm->layout[i]) == s;
            if (s == SL_a && x->n > 1)
                used = 1;
            if (!used)
                continue;
            for (k = 0; k < 18 && x->c[k].h; k++)
                n++, h += x->c[k].h;
            if (n != x->n || (h != 16 && h != 24 && h != 32)) {
                printf("form %s section %d: %d chords (says %d), %d half bars\n", fm->name, s, n, x->n, h);
                bad++;
            }
            if (s == SL_a) {
                int ha = 0;
                for (k = 0; k < fm->s[SL_A].n; k++)
                    ha += fm->s[SL_A].c[k].h;
                if (ha != h) {
                    printf("form %s: a (%d) and A (%d) differ\n", fm->name, h, ha);
                    bad++;
                }
            }
        }
    }
    return bad;
}
#endif
