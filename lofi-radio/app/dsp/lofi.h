/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * LOFI RADIO: ashtonwallen, 2026 */
/* The instrument: an endless lo-fi hip-hop radio. Each "track" is generated from a seed: a key, a tempo, a
 * progression (ii-V-I and friends, in rootless 7th and 9th voicings), a boom-bap groove, a bassline and now and
 * then a short melody. A track runs a few minutes (intro, phrases, a breakdown, an outro), then the needle runs
 * out, a few seconds of crackle, and the next one starts in a new key.
 *
 *   KEYS     FM electric piano (Rhodes, Wurli or a felt piano by style), rolled chords, voice-led, a soft autopan;
 *            the changes vary as the track goes (substitutions, borrowed chords, passing chords; B sections most)
 *   DRUMS    synthesized: a soft sine kick, a snare or a side-stick, closed and open hats; brushes, a rim click, a
 *            shaker by style; swung, laid back; fills where sections end, half-time breakdowns, thinner bars
 *   BASS     a round sine with a little second harmonic, roots, fifths and approach notes, a short glide
 *   MELODY   a muted lead or a vibraphone, a two-bar motif in question-and-answer phrases, developed later in the
 *            track, each phrase ending on the chord; through a dotted echo
 *   GUESTS   one or two a track, from the style's set, in a part's place: a nylon guitar, a muted trumpet, a sine
 *            sub, chopped formant vocal
 *   DUST     vinyl hiss and crackle, a scratch once a turn, tape wow and flutter (a modulated delay)
 *   ROOM     a small four-line room reverb; the whole bus low-passed, gently saturated and levelled
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at
 * the start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only,
 * no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTYLES 14

/* parameters: ranges in en_param_info(). The first ten are a style's own (each style keeps them as left);
 * Length is the player's */
enum {
    P_SWING, P_DUST, P_TEMPO, P_MOOD,                 /* the radio page */
    P_KEYS, P_DRUMS, P_BASS, P_MELODY,                /* the mix */
    P_TONE, P_RAIN,                                   /* the room */
    P_LENGTH,                                         /* how long a track runs (global) */
    P_NPARAMS
};
#define P_NSTYLE P_LENGTH                      /* the ones a style keeps */
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
void en_param_text(int i, int v, char *buf);   /* the value as the screen shows it (12 bytes) */

extern const char *const EN_NOTE_NAME[12];

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_style(int i);                          /* a style's flavour (timbres, grooves): from the next track */
void en_play(int on);                          /* the needle down / up */
void en_skip(void);                            /* fade out and on to the next track */
void en_repeat(int on);                        /* the track plays on, no next one */
void en_track(uint32_t no, uint32_t seed);     /* where the radio is: the next track starts from no + 1 */
void en_note(int note, int vel);               /* keys played along (vel 0: off) */
void en_panic(void);                           /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
enum { PH_INTRO, PH_BODY, PH_OUTRO, PH_RUNOUT };
extern volatile uint8_t en_playing, en_phase, en_repeat_on;
extern volatile uint32_t en_track_no;          /* the track playing (1, 2, ...) */
extern volatile uint32_t en_track_seed;        /* its seed: its title, key, label */
extern volatile uint32_t en_seed_base;         /* the radio's seed (saved: the tracks to come) */
extern volatile uint32_t en_track_ms;          /* time into the track */
extern volatile uint32_t en_track_len_ms;      /* how long it will run */
extern volatile uint8_t en_key, en_minor;      /* its key: pitch class, minor */
extern volatile uint8_t en_bpm;                /* its tempo */
extern volatile uint8_t en_chord_root;         /* the chord now: pitch class */
extern volatile uint8_t en_chord_q;            /* its quality (en_chord_name) */
extern volatile uint16_t en_chord_pcs;         /* its notes as pitch classes (bit i) */
extern volatile uint8_t en_section;            /* 0 intro, 1 A, 2 B (melody), 3 breakdown, 4 outro, 5 run-out */
extern volatile float en_vu_l, en_vu_r;        /* the output, 0..1 on a VU's scale */
extern volatile float en_kick_glow;            /* 0..1: the kick just hit */
extern volatile uint32_t en_beats;             /* beats played since the start (tests, the screen) */
extern volatile uint32_t en_tracks_played;     /* tracks begun since the start (tests) */
extern volatile float en_needle;               /* 0 lifted .. 1 in the groove (the tonearm) */

/* the pure parts, for the screen and the tests */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a style's own settings (P_NSTYLE of them) */
uint8_t en_style_label(int i);                 /* its label colour family */
void en_title(uint32_t seed, char *buf);       /* a track's title (lower case, 24 bytes) */
uint8_t en_label_colour(uint32_t seed);        /* its record label's colour: 0..11 */
const char *en_chord_name(int root, int q, char *buf);   /* "Dm9", "G13", "Cmaj7" (12 bytes) */
uint32_t en_track_rand(uint32_t base, uint32_t no);      /* track no's seed from the radio's */

#ifdef EN_HOST                                 /* the engine tests' view: events fired by type, each part's level */
enum { EN_PART_KEYS, EN_PART_BASS, EN_PART_SUB, EN_PART_DRUMS, EN_PART_KIT, EN_PART_GTR, EN_PART_VOX, EN_PART_MEL,
       EN_PART_TPT, EN_NPART };
enum { EN_EV_SUB = 8, EN_EV_BRUSH, EN_EV_CLICK, EN_EV_SHAKE, EN_EV_GTR, EN_EV_VOX };
typedef struct {
    double sum2;                               /* the part's energy (mean of the two sides), where it sounded */
    uint32_t n;                                /* frames it sounded in */
    float peak;
} en_part_t;
extern uint32_t en_fired[16];
extern en_part_t en_part[EN_NPART];
extern int en_meas;                            /* 1: measure the parts (slower) */
#endif
