/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * FELT PIANO: ashtonwallen, 2026 */
/* The instrument: endless generated solo piano. Each "piece" comes from a seed and is played in one of twelve idioms,
 * chosen by the style (preset):
 *
 *   GYMNOPEDIE  slow 3/4: a low bass note, a chord on beat two, two chords rocking; a modal melody above
 *   AIRPORT     a few short phrases, each looping at its own length, so they drift against each other
 *   OSTINATO    a broken-chord figure that repeats, one of its notes moving now and then; a swell and a fall
 *   NOCTURNE    an arpeggiated left hand over simple progressions in periods (to V, then home), a singing right hand
 *   MODAL       sparse improvising in Dorian, Lydian or Aeolian over an open drone of two chords
 *   MINIMALIST  a broken-chord figure repeated, chords changing every two or four bars through a short cycle; the
 *               figure grows a note at a time, then sheds them
 *   CHORALE     a hymn in four parts, a chord a beat, cadences and breaths; verses, then an amen
 *   STRIDE      a slow stride ballad: bass and chord in the left hand, a swung song melody with blue touches
 *   PLANING     one chord shape slid in parallel over a long bass pedal; pentatonic arabesques above
 *   LULLABY     6/8: a rocking left hand, a singable tune in four-bar pairs
 *   BLUES       slow 12/8 blues: twelve-bar choruses, licks in call and response, a turnaround, a bII9 ending
 *   MINOR BLUES slow 12/8 minor blues: i, iv, bVI7 to V7; a rolling, sparse or stabbing left hand, minor
 *               blues-scale calls and answers, the minor line cliche turning round, a bVI7-V7b9 to i6/9 close
 *
 * A piece ends on its tonic, slowing, the pedal held while it rings away; a few seconds of quiet; the next.
 *
 *   PIANO   modal synthesis: each note a handful of decaying partials (stretched, the lowest two doubled and a hair
 *           apart, as the unison strings beat), a felt hammer's soft attack, decay by register, dampers (none above
 *           the top dampers), the sustain pedal
 *   BODY    the hammer's thump, the key and damper noise, the pedal's; sympathetic strings tuned to the harmony
 *   ROOM    a small four-line room, its size and send by Room; an old upright's wobble and hiss with Age
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at
 * the start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only,
 * no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTYLES 33
#define EN_NSTYLES_V1 14                       /* 1.0's (a project saved by 1.0 holds these) */
#define EN_NSTYLES_V2 30                       /* 1.1's */

/* parameters: ranges in en_param_info(). The first ten are a style's own (each style keeps them as left);
 * Length is the player's */
enum {
    P_TEMPO, P_DENSITY, P_FELT, P_ROOM,               /* the piece page */
    P_RUBATO, P_DYN, P_PEDAL, P_NOISE,                /* the touch page */
    P_AGE, P_WIDTH,                                   /* the setup page */
    P_LENGTH,                                         /* how long a piece runs (global) */
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
void en_style(int i);                          /* a style: from the next piece */
void en_play(int on);                          /* playing / resting (what rings, rings on) */
void en_skip(void);                            /* a quick close and on to the next piece */
void en_repeat(int on);                        /* the piece plays on, no next one */
void en_track(uint32_t no, uint32_t seed);     /* where the player is: the next piece starts from no + 1 */
void en_note(int note, int vel);               /* keys played along (vel 0: off) */
void en_panic(void);                           /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
enum { PH_INTRO, PH_BODY, PH_END, PH_GAP };    /* a piece: its opening, its body, its close, the quiet after */
enum { ID_GYM, ID_AIR, ID_OST, ID_NOCT, ID_MODAL, ID_ARP, ID_HYMN, ID_STRIDE, ID_PLANE, ID_LULL, ID_BLUES, ID_MBLUES,
       ID_N };
extern volatile uint8_t en_playing, en_phase, en_repeat_on;
extern volatile uint32_t en_track_no;          /* the piece playing (1, 2, ...) */
extern volatile uint32_t en_track_seed;        /* its seed: its title */
extern volatile uint32_t en_seed_base;         /* the player's seed (saved: the pieces to come) */
extern volatile uint32_t en_track_ms;          /* time into the piece */
extern volatile uint32_t en_track_len_ms;      /* about how long it will run */
extern volatile uint8_t en_key, en_mode;       /* its key: pitch class; its mode (en_mode_name) */
extern volatile uint8_t en_idiom, en_meter;    /* ID_*; beats a bar (3, 4; 4 in 12/8, 2 in 6/8 with en_compound) */
extern volatile uint8_t en_compound;           /* compound time: 12/8 (en_meter 4) or 6/8 (en_meter 2) */
extern volatile uint8_t en_bpm;                /* its tempo */
extern volatile uint16_t en_chord_pcs;         /* the harmony now, as pitch classes (bit i) */
extern volatile uint8_t en_chord_root;         /* its root (pitch class) */
extern volatile uint8_t en_pedal;              /* the sustain pedal down */
extern volatile float en_vu_l, en_vu_r;        /* the output, 0..1 */
extern volatile uint32_t en_now_ms;            /* the engine's clock (the note log's times) */
extern volatile uint32_t en_notes_played;      /* notes struck since the start (tests) */
extern volatile uint32_t en_tracks_played;     /* pieces begun since the start (tests) */
extern volatile uint8_t en_voices;             /* voices sounding (tests: the polyphony) */

/* the note log, for the screen: each note struck, when, how hard, how fast it fades, and when it was damped.
 * A ring: en_log_w counts the notes ever logged; entry i is EN_LOG[i % EN_NLOG] */
#define EN_NLOG 128
typedef struct {
    uint32_t on, off;                          /* ms (en_now_ms); off 0: still ringing */
    uint16_t tau;                              /* its natural fade: ms to 1/e */
    uint8_t note, vel;                         /* MIDI note, 0..127 */
} en_logn_t;
extern volatile en_logn_t EN_LOG[EN_NLOG];
extern volatile uint32_t en_log_w;
/* the pedal's log: down and up, a ring of times (ms); entry i down if i even. en_ped_w counts the changes */
#define EN_NPED 32
extern volatile uint32_t EN_PED[EN_NPED];
extern volatile uint32_t en_ped_w;

/* the pure parts, for the screen and the tests */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a style's own settings (P_NSTYLE of them) */
int en_style_idiom(int i);
const char *en_idiom_name(int id);             /* "Gymnopedie", "Airport", ... */
const char *en_mode_name(int m);               /* "major", "minor", "Dorian", "Lydian", ... */
void en_title(uint32_t seed, int idiom, char *buf);   /* a piece's title (lower case, 28 bytes) */
uint32_t en_track_rand(uint32_t base, uint32_t no);   /* piece no's seed from the player's */

enum { R_BASS, R_CHORD, R_ARP, R_MEL, R_LOOP, R_PLAYER };   /* a note's part in the piece */
#ifdef EN_HOST
/* the tests' ear: every note as it is scheduled (bar, tick of 12 a beat, note, velocity 0..1, role) and every
 * harmony as it begins */
extern void (*en_dbg_note)(int bar, int tick, int note, float vel, int role, int dur_ticks);
extern void (*en_dbg_chord)(int bar, int tick, int root_pc, uint16_t pcs);
/* the chorale's four parts as they are chosen (soprano, alto, tenor, bass), a chord at a time */
extern void (*en_dbg_satb)(int bar, int tick, const int *satb);
#endif
