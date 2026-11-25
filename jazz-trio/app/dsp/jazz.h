/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * JAZZ TRIO: ashtonwallen, 2026 */
/* The instrument: an endless piano trio (piano, upright bass, drums) in a small club, playing a set. Each tune is
 * generated from a seed: a kind (medium swing, up tempo, ballad, blues, minor blues, rhythm changes, modal, sus vamp,
 * jazz waltz), a key, a tempo, a form with original changes built from curated idiomatic material (ii-V-I chains,
 * turnarounds, tritone subs, backdoor cadences), a head melody, and an arrangement: count-in or intro, the head,
 * piano solo choruses, sometimes a bass solo and trading fours with the drums, the head out and an ending (a tag, a
 * ritardando or a Basie ending). Soft applause, a pause, and the next tune is counted in.
 *
 *   PIANO   FM acoustic piano: rootless A/B voicings, shells and quartal voicings; heads and bebop solos
 *   BASS    a plucked string (Karplus-Strong): walking lines, two-feel, solos
 *   DRUMS   synthesized ride (spang-a-lang), hi-hat foot on 2 and 4, snare and kick comping, toms, crash, brushes
 *   CROWD   soft applause, a murmur and glasses between tunes
 *   ROOM    a four-line club reverb; the bus toned, levelled
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at
 * the start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only,
 * no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTYLES 13

/* parameters: ranges in en_param_info(). The first ten are a style's own (each keeps them as left); Stretch is the
 * player's */
enum {
    P_TEMPO, P_INTENS, P_ROOM, P_SWING,               /* the band page */
    P_PIANO, P_BASS, P_DRUMS, P_CROWD,                /* the mix */
    P_TONE, P_FOCUS,                                  /* the set page */
    P_STRETCH,                                        /* how long the solos run (global) */
    P_NPARAMS
};
#define P_NSTYLE P_STRETCH                     /* the ones a style keeps */
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
void en_style(int i);                          /* a style (a band, a set): from the next tune */
void en_play(int on);                          /* the band plays / takes a break */
void en_skip(void);                            /* the band stops this tune; on to the next */
void en_repeat(int on);                        /* the band keeps blowing on this tune */
void en_tune(uint32_t no, uint32_t seed);      /* where the set is: the next tune starts from no + 1 */
void en_note(int note, int vel);               /* keys played along on the piano (vel 0: off) */
void en_panic(void);                           /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
enum { PH_TUNE, PH_APPLAUSE, PH_GAP };
enum { R_COUNT, R_INTRO, R_HEAD, R_PSOLO, R_BSOLO, R_TRADE_P, R_TRADE_D, R_HEADOUT, R_TAG, R_FINAL };
enum { K_MEDIUM, K_UP, K_BALLAD, K_BLUES, K_BIRD, K_MINOR, K_RHYTHM, K_MODAL, K_SUS, K_WALTZ, K_NKINDS };
enum { HIT_RH, HIT_LH, HIT_BASS, HIT_RIDE, HIT_HAT, HIT_SNARE, HIT_KICK, HIT_TOM, HIT_CRASH, NHITS };
extern volatile uint8_t en_playing, en_phase, en_repeat_on;
extern volatile uint8_t en_role;               /* the bar's part (R_*) */
extern volatile uint8_t en_soloist;            /* the spotlight: 0 the band, 1 piano, 2 bass, 3 drums */
extern volatile uint8_t en_brushes;            /* the drummer has brushes */
extern volatile uint32_t en_tune_no;           /* the tune playing (1, 2, ...) */
extern volatile uint32_t en_tune_seed;         /* its seed: its title */
extern volatile uint32_t en_seed_base;         /* the set's seed (saved: the tunes to come) */
extern volatile uint32_t en_tunes_played;      /* tunes begun since the start (tests) */
extern volatile uint32_t en_beats;             /* beats played since the start */
extern volatile uint8_t en_kind;               /* the tune's kind (K_*) */
extern volatile uint8_t en_key, en_minor;      /* its key: pitch class, minor */
extern volatile uint16_t en_bpm;               /* its tempo now */
extern volatile uint8_t en_chord_root, en_chord_q;   /* the chord now (en_chord_name) */
extern volatile uint16_t en_chord_pcs;         /* its notes as pitch classes (bit i) */
extern volatile uint8_t en_bar, en_form_bars;  /* where in the form, how long the form is */
extern volatile uint8_t en_chorus, en_nchorus; /* the chorus of this part, of how many */
extern volatile uint8_t en_beat, en_bpb;       /* the beat in the bar, beats a bar (3 or 4) */
extern volatile uint8_t en_hits[NHITS];        /* counters: each hit adds one (the players on screen) */
extern volatile uint8_t en_rh_note, en_lh_note, en_bass_note;   /* the latest notes (MIDI) */
extern volatile float en_applause;             /* 0..1: the room applauding */
extern volatile float en_vu_l, en_vu_r;        /* the output, 0..1 on a VU's scale */

/* the pure parts, for the screen and the tests */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a style's own settings (P_NSTYLE of them) */
const char *en_kind_name(int k);               /* "Blues", "Ballad", ... */
void en_title(uint32_t seed, int kind, char *buf);   /* a tune's title (32 bytes) */
const char *en_chord_name(int root, int q, char *buf);   /* "Dm7", "G7alt", "Cmaj7" (12 bytes) */
uint32_t en_tune_rand(uint32_t base, uint32_t no);       /* tune no's seed from the set's */

#ifdef EN_HOST
/* the tests' ear: every note as it is planned (inst: 0 piano right hand, 1 piano left hand, 2 bass, 3 ride,
 * 4 hat, 5 snare, 6 kick, 7 tom, 8 crash, 9 brush, 10 stick); tick in twelfths of a beat from the bar's start */
extern void (*en_trace)(int inst, int rec, int role, int fbar, int tick, int dur, int note, int vel);
extern void (*en_trace_bar)(int rec, int role, int fbar, int chorus, const char *chords);
#endif
