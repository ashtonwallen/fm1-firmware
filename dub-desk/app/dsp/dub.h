/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * DUB DESK: ashtonwallen, 2026 */
/* The instrument: endless roots dub, a reggae band mixed live by a virtual engineer. A "version" is one riddim (a
 * curated bass figure and its one or two chords, or a plain I-IV-V) played for a few minutes in 4-bar blocks; the
 * engineer mutes and brings back parts on bar lines, throws snare hits, skank chops, horn stabs and melodica phrase
 * ends into a tape echo, kicks the spring reverb, sweeps a high-pass, sounds a siren now and then, and brings the
 * full band back. Then the next riddim.
 *
 *   DRUMS    one-drop, rockers or steppers: a round kick, a cross-stick rim or a tight snare, hats, toms; rolls
 *   BASS     a deep sine with a little second harmonic: the riddim's 1-2 bar figure, the lead instrument
 *   SKANK    guitar chops on the off-beats (two and four, or doubled)
 *   ORGAN    the bubble: low and high halves of the chord on the shuffled off-beat sixteenths
 *   MELODICA a reed lead: a call and its answer over the chords, now and then
 *   HORNS    three-part stabs, once in a while
 *   DESK     a tape echo (feedback swells, wow), a two-spring reverb, a high-pass sweep, a siren, the bus
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at the
 * start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only, no libm:
 * fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTYLES 13
#define EN_NRIDDIMS 16

/* parameters: ranges in en_param_info(). The first eleven are a style's own (each style keeps them as left);
 * Length is the player's */
enum {
    P_ENGINEER, P_ECHO, P_TIME, P_BASS,               /* the desk page */
    P_DRUMS, P_SKANK, P_ORGAN, P_LEAD,                /* the band */
    P_SPRING, P_TONE, P_TEMPO,                        /* the room */
    P_LENGTH,                                         /* how long a riddim plays (global) */
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

/* the desk's channels */
enum { CH_DRUMS, CH_BASS, CH_GTR, CH_ORGAN, CH_MEL, CH_HORN, EN_NCH };

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_style(int i);                          /* a style's flavour (drums, riddims, the engineer): next riddim */
void en_play(int on);                          /* the tape rolls / stops */
void en_skip(void);                            /* strip it down and on to the next riddim */
void en_repeat(int on);                        /* this riddim plays on */
void en_track(uint32_t no, uint32_t seed);     /* where the session is: the next version starts from no + 1 */
void en_note(int note, int vel);               /* keys played along on the organ (vel 0: off) */
void en_throw(void);                           /* the player throws what is playing into the echo */
void en_crash(void);                           /* the player kicks the spring tank */
void en_panic(void);                           /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* engineer's moves, for the screen (en_move) */
enum { MV_NONE, MV_THROW, MV_SWELL, MV_SPRING, MV_FILTER, MV_SIREN, MV_DROP, MV_BACK, MV_STRIP };

/* state for the screen (UI) */
enum { PH_INTRO, PH_BODY, PH_OUTRO };
extern volatile uint8_t en_playing, en_phase, en_repeat_on;
extern volatile uint32_t en_track_no;          /* the version playing (1, 2, ...) */
extern volatile uint32_t en_track_seed;        /* its seed */
extern volatile uint32_t en_seed_base;         /* the session's seed (saved: the versions to come) */
extern volatile uint32_t en_track_ms;          /* time into the version */
extern volatile uint32_t en_track_len_ms;      /* how long it will run */
extern volatile uint8_t en_riddim;             /* which riddim */
extern volatile uint8_t en_key, en_minor;      /* its key: pitch class, minor */
extern volatile uint8_t en_bpm;                /* its tempo */
extern volatile uint8_t en_chord_root;         /* the chord now: pitch class */
extern volatile uint8_t en_chord_q;            /* 0 minor, 1 major, 2 minor 7th */
extern volatile uint16_t en_chord_pcs;         /* its notes as pitch classes (bit i) */
extern volatile uint8_t en_bar_in_block;       /* 0..3 */
extern volatile uint8_t en_step;               /* sixteenth in the bar */
extern volatile float en_fader[EN_NCH];        /* each channel's fader: 0..1 (the knob's level, the mute) */
extern volatile float en_meter[EN_NCH];        /* each channel's level: 0..1 */
extern volatile float en_send[EN_NCH];         /* each channel's echo send: 0..1 (a throw lights it) */
extern volatile float en_echo_fb;              /* the echo's feedback now: 0..1 */
extern volatile float en_echo_out;             /* the echo's return level: 0..1 */
extern volatile float en_echo_s;               /* the echo's time, seconds */
extern volatile float en_spring;               /* the spring's motion: 0..1 */
extern volatile float en_hp;                   /* the high-pass sweep: 0 open .. 1 thin */
extern volatile float en_siren;                /* the siren: 0..1 */
extern volatile uint8_t en_move;               /* the engineer's last move (MV_*) */
extern volatile uint32_t en_move_no;           /* counts the moves (the screen sees a new one) */
extern volatile float en_vu_l, en_vu_r;        /* the output, 0..1 on a VU's scale */
extern volatile float en_kick_glow;            /* 0..1: the kick just hit */
extern volatile uint32_t en_beats;             /* beats played since the start (tests, the screen) */
extern volatile uint32_t en_tracks_played;     /* versions begun since the start (tests) */

/* the pure parts, for the screen and the tests */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a style's own settings (P_NSTYLE of them) */
const char *en_riddim_name(int i);
const char *en_chord_name(int root, int q, char *buf);   /* "Am", "D", "Bm7" (12 bytes) */
uint32_t en_track_rand(uint32_t base, uint32_t no);      /* version no's seed from the session's */

#ifdef EN_HOST
/* the tests' ear: every note as it is scheduled (type E_*, the bar, the step, the note, the velocity) */
extern void (*en_log)(int type, int bar, int step, int note, float vel);
enum { L_KICK, L_RIM, L_SNARE, L_HAT, L_OHAT, L_TOM, L_BASS, L_GTR, L_ORG, L_MEL, L_HORN, L_BAR, L_MOVE, L_SIREN };
#endif
