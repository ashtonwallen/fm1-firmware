/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * JUKE JOINT: ashtonwallen, 2026 */
/* The instrument: a blues trio in a back room, playing an endless set. Piano, upright bass and brushes, and a
 * harmonica player who sits in now and then. Each tune comes from a seed and is one of four kinds:
 *
 *   SLOW      slow 12/8 blues: twelve-bar choruses, the piano's calls, triplet chords behind
 *   SHUFFLE   medium shuffle: the bass's two-beat (walking or boogie once it warms up), brushes on two and four
 *   MINOR     minor blues in an even, slow 4/4: i iv i, bVI7 V7 i
 *   BALLAD    an eight-bar slow blues, softer, the harp in long tones
 *
 * A tune: a count-off or an intro, the head (the piano calls, the harp answers), solos (piano choruses, a harp
 * chorus, now and then a stop-time chorus), the head again and an ending. Applause, a few seconds of the room, the
 * next tune.
 *
 *   PIANO     modal synthesis (FELT PIANO's voice): stretched partials, a felt hammer, dampers, a little pedal
 *   BASS      a plucked string (Karplus-Strong), damped as the next note comes
 *   DRUMS     brushes (slaps, taps and the circling sweep), a feathered bass drum, the hi-hat's foot
 *   HARP      a reed: a band-limited wave through the cupped hands' filter, breath, bends, shakes and vibrato
 *   ROOM      a small four-line room; the murmur, glasses and applause on their own level (Ambience)
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at
 * the start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only,
 * no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTYLES 13

/* parameters: ranges in en_param_info(). The first ten are a night's own (each keeps them as left); Length is the
 * player's */
enum {
    P_TEMPO, P_HARP, P_DRIVE, P_ROOM,                 /* the band page */
    P_PIANO, P_BASS, P_DRUMS, P_AMBI,                 /* the mix page */
    P_AGE, P_WIDTH,                                   /* the setup page */
    P_LENGTH,                                         /* how long a tune runs (global) */
    P_NPARAMS
};
#define P_NSTYLE P_LENGTH                      /* the ones a night keeps */
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
void en_style(int i);                          /* a night: from the next tune */
void en_play(int on);                          /* playing / resting */
void en_skip(void);                            /* the band stops, the next tune */
void en_repeat(int on);                        /* the solos go on, no head out */
void en_track(uint32_t no, uint32_t seed);     /* where the set is: the next tune starts from no + 1 */
void en_note(int note, int vel);               /* keys played along on the piano (vel 0: off) */
void en_panic(void);                           /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
enum { PH_INTRO, PH_BODY, PH_END, PH_GAP };    /* a tune: its count or intro, its choruses, its ending, the break */
enum { K_SLOW, K_SHUF, K_MINOR, K_BALLAD, K_N };
enum { S_COUNT, S_INTRO, S_HEAD, S_PIANO, S_HARP, S_STOP, S_OUT, S_END, S_BREAK, S_N };   /* what the band is at */
enum { HIT_RH, HIT_LH, HIT_BASS, HIT_SNARE, HIT_KICK, HIT_HAT, HIT_HARP, NHITS };
extern volatile uint8_t en_playing, en_phase, en_repeat_on;
extern volatile uint32_t en_track_no;          /* the tune playing (1, 2, ...) */
extern volatile uint32_t en_track_seed;        /* its seed: its title */
extern volatile uint32_t en_seed_base;         /* the set's seed (saved: the tunes to come) */
extern volatile uint32_t en_track_ms;          /* time into the tune */
extern volatile uint32_t en_track_len_ms;      /* about how long it will run */
extern volatile uint8_t en_key, en_kind;       /* its key (pitch class), its kind (K_*) */
extern volatile uint8_t en_bpm;                /* its tempo: beats a minute (12/8: dotted quarters) */
extern volatile uint8_t en_section;            /* S_*: where the band is */
extern volatile uint8_t en_bar, en_form_len;   /* the bar in the form (0..11, or 0..7), its length */
extern volatile uint8_t en_chorus, en_nchorus; /* the chorus (1..), how many */
extern volatile int8_t en_form_root[12];       /* the form's chords: root (semitones over the key), quality */
extern volatile uint8_t en_form_q[12];
extern volatile uint16_t en_chord_pcs;         /* the harmony now, as pitch classes (bit i) */
extern volatile uint8_t en_chord_root, en_chord_q;   /* its root (pitch class), its quality (en_q_name) */
extern volatile uint8_t en_harp_in;            /* the harp player is on the stage */
extern volatile uint8_t en_harp_on;            /* ... and blowing */
extern volatile uint8_t en_harp_note, en_rh_note, en_bass_note;   /* the last notes (the players' hands) */
extern volatile uint8_t en_hits[NHITS];        /* counts, each instrument's strokes (the screen's animation) */
extern volatile float en_applause;             /* the room clapping, 0..1 */
extern volatile float en_beat;                 /* where in the beat (0..1) and the bar (en_beat_no) */
extern volatile uint8_t en_beat_no;
extern volatile float en_vu_l, en_vu_r;        /* the output, 0..1 */
extern volatile uint32_t en_now_ms;            /* the engine's clock */
extern volatile uint32_t en_notes_played;      /* notes struck since the start (tests) */
extern volatile uint32_t en_tracks_played;     /* tunes begun since the start (tests) */
extern volatile uint8_t en_voices;             /* piano voices sounding (tests) */

/* the pure parts, for the screen and the tests */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a night's own settings (P_NSTYLE of them) */
int en_style_scene(int i);                     /* its room's look on the screen (0..3) */
const char *en_kind_name(int k);               /* "Slow blues", "Shuffle", ... */
const char *en_section_name(int s);            /* "Head", "Piano", "Harp", ... */
void en_chord_name(int root_pc, int q, char *buf);   /* "Bb7", "Ebm7", "F#dim" (8 bytes) */
const char *en_q_name(int q);                  /* the quality's suffix: "7", "m7", ... */
void en_title(uint32_t seed, int kind, char *buf);   /* a tune's title (lower case, 28 bytes) */
uint32_t en_track_rand(uint32_t base, uint32_t no);  /* tune no's seed from the set's */

enum { R_BASS, R_COMP, R_LH, R_MEL, R_HARP, R_DRUM, R_PLAYER };   /* a note's part */
#ifdef EN_HOST
/* the tests' ear: every note as it is scheduled (bar, tick of 12 a beat, note, velocity 0..1, role, length) and
 * every harmony as it begins; the arrangement's marks */
extern void (*en_dbg_note)(int bar, int tick, int note, float vel, int role, int dur_ticks);
extern void (*en_dbg_chord)(int bar, int tick, int root_pc, uint16_t pcs);
extern void (*en_dbg_mark)(int bar, int section, int chorus, int bar12);
#endif
