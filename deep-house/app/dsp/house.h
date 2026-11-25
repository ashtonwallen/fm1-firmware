/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * DEEP HOUSE DECKS: ashtonwallen, 2026 */
/* The instrument: an endless deep house DJ set. Two decks, a mixer and a virtual DJ. Each track is generated from a
 * seed: a key, a curated two- or four-chord vamp (minor 9ths and 11ths, the borrowed changes), a chord rhythm, a
 * rolling bassline, a drum kit and its percussion, and now and then a lead hook or a chopped vocal stab. Tracks run
 * in 16-bar phrases: a drum intro, the groove, the full section, a breakdown with the kick out and a filter build,
 * the drop, a drum outro. The DJ blends each into the next on phrase boundaries: the new record's fader up, the bass
 * swapped on the low EQ, filter sweeps, the crossfader across; the next key always a neighbour on the wheel.
 *
 *   CHORDS   FM electric piano, organ or pad stabs (by set), rootless 9th/11th voicings, voice-led; a soft pad layer
 *   BASS     a round FM sub (or an organ bass), roots, octaves, fifths and approach notes, locked to the kick
 *   DRUMS    synthesized: kick, clap (or snare, rim), offbeat open hats, shuffled 16th hats, shaker, conga, rim,
 *            clave
 *   LEAD     a soft FM hook from the chord's tones, or a formant "vocal" stab, through a dotted echo
 *   MIXER    per deck: fader, low EQ, a DJ filter (low-pass / high-pass); a crossfader; the master's own filter,
 *            warmth, a hall, the leveller
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at
 * the start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only,
 * no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTYLES 13

/* parameters: ranges in en_param_info(). The first ten are a set's own (each set keeps them as left); Filter and
 * Mix are the player's */
enum {
    P_ENERGY, P_WARMTH, P_TEMPO, P_SWING, P_MOOD, P_SPACE,   /* the set's character */
    P_CHORDS, P_DRUMS, P_BASS, P_LEAD,                       /* the levels */
    P_FILTER,                                                /* the master filter: < 0 low-pass, > 0 high-pass */
    P_MIX,                                                   /* how long a blend runs: 16, 32 or 64 bars */
    P_NPARAMS
};
#define P_NSTYLE P_FILTER                      /* the ones a set keeps */
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
void en_style(int i);                          /* a set's sound: from the next record */
void en_play(int on);                          /* the decks run / stop */
void en_mix(int quick);                        /* the DJ starts the next blend at the next 4-bar line (quick: 16
                                                * bars long, whatever Mix says) */
void en_hold(int on);                          /* the record playing plays on: no blend */
void en_track(uint32_t no, uint32_t seed);     /* where the set is: the next record is no + 1 */
void en_note(int note, int vel);               /* keys played along (vel 0: off) */
void en_panic(void);                           /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* sections of a record (en_deck_t.sect) */
enum { S_IDLE, S_INTRO, S_GROOVE, S_FULL, S_BREAK, S_BUILD, S_OUTRO, S_TAIL, S_NSECT };
extern const char *const EN_SECT_NAME[S_NSECT];

/* a deck, for the screen (UI) */
typedef struct {
    volatile uint8_t on;                       /* a record is playing */
    volatile uint8_t loaded;                   /* a record is on it (cued or playing) */
    volatile uint8_t sect;                     /* S_*: where the record is */
    volatile uint8_t key;                      /* its key (the minor tonic's pitch class) */
    volatile uint8_t lead;                     /* its lead: 0 none, 1 hook, 2 vocal */
    volatile uint16_t bar;                     /* bars into it (0 ..) */
    volatile uint16_t bars;                    /* how many it will run (planned; grows while held) */
    volatile uint32_t no, seed;                /* its number in the set, its seed (title, label) */
    volatile float fader, low, filter;         /* the DJ's hands: 0..1, 0..1 (low EQ), -1..1 (LP .. HP) */
    volatile float meter;                      /* its level after the fader, 0..1 */
    volatile float spin;                       /* the platter's speed, 0..1 */
    volatile float rot;                        /* its angle, turns */
} en_deck_t;
extern en_deck_t en_deck[2];
extern volatile float en_xfade;                /* the crossfader: 0 deck A .. 1 deck B */
extern volatile uint8_t en_master;             /* the deck the set is on (0, 1) */
extern volatile uint8_t en_mixing;             /* a blend is under way */
extern volatile uint8_t en_playing, en_hold_on;
extern volatile uint8_t en_bpm;                /* the tempo */
extern volatile uint32_t en_track_no;          /* the last record begun (1, 2, ...) */
extern volatile uint32_t en_seed_base;         /* the set's seed (saved: the records to come) */
extern volatile uint8_t en_chord_root, en_chord_q;   /* the chord now (the master deck's) */
extern volatile uint16_t en_chord_pcs;         /* its notes as pitch classes (bit i) */
extern volatile float en_vu_l, en_vu_r;        /* the master output, 0..1 on a meter's scale */
extern volatile uint32_t en_beats;             /* beats played since the start (tests, the screen) */
extern volatile uint32_t en_tracks_played;     /* records begun since the start (tests) */
extern volatile uint32_t en_mixes;             /* blends finished (tests) */

/* the pure parts, for the screen and the tests */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a set's own settings (P_NSTYLE of them) */
void en_title(uint32_t seed, char *buf);       /* a record's title (24 bytes) */
const char *en_mix_tag(uint32_t seed, int lead);   /* "Dub", "Vocal Mix", ... */
uint8_t en_label_colour(uint32_t seed);        /* its label's colour: 0..11 */
const char *en_chord_name(int root, int q, char *buf);   /* "Am9", "Fmaj9" (12 bytes) */
uint32_t en_track_rand(uint32_t base, uint32_t no);      /* record no's seed from the set's */

#ifdef EN_HOST
/* the tests' window on the sequence: every note as it is decided (deck, the step in the set, what, the note,
 * its length in steps, its velocity 0..1); kind: 'C' chord tone, 'P' pad, 'B' bass, 'L' lead, 'V' vocal, 'K' kick,
 * 'S' clap/snare, 'H' hat, 'O' open hat, 'R' percussion, '|' a bar begins (note: the section, len: the chord root
 * and quality as root * 16 + q) */
extern void (*en_log)(int deck, uint32_t step, char kind, int note, int len, float vel);
#endif
