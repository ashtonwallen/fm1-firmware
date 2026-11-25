/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * BOSSA LOUNGE: ashtonwallen, 2026 */
/* The instrument: endless generated bossa nova and lounge jazz, played by a small combo. Each "tune" is made from a
 * seed: a key, a tempo, one of a few curated song forms from the bossa and jazz-standard repertoire (AABA or ABAC,
 * ii-V chains, chromatic ii-Vs, the I-ii-iii-biii turnaround, minor ii-V-i, a lift up a half step for the last
 * chorus), a head melody that the form repeats, a solo chorus, the head out, a soft tag ending. Then a pause and a
 * new tune with a new title.
 *
 *   GUITAR   nylon strings (Karplus-Strong): the thumb on 1 and 3, the fingers in the bossa figures (the clave, its
 *            2-3 turn, a busier batida, a ballad roll), jazz voicings voice-led, anticipations
 *   BASS     an upright, pizzicato: root and fifth in the bossa's long-short figures, or a two-feel for ballads
 *   DRUMS    brushes: a swirl that circles the half bar, taps and swishes, a rim click on the bossa clave, a soft kick
 *   MELODY   flute or vibraphone (alternating, if asked): chord tones on the strong beats, steps between, phrases that
 *            answer each other and end on stable notes, space between them
 *   ROOM     a small hall (four lines, two diffusers); the bus low-passed (Tone), warmed and levelled
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
    P_TEMPO, P_FEEL, P_MOOD, P_MELODY,                /* the tune page */
    P_GUITAR, P_BASS, P_DRUMS, P_LEAD,                /* the combo */
    P_TONE, P_REVERB,                                 /* the room */
    P_LENGTH,                                         /* how many choruses a tune runs (global) */
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
void en_style(int i);                          /* a style's flavour (patterns, kit): from the next tune */
void en_play(int on);                          /* the tape rolls / stops */
void en_skip(void);                            /* fade out and on to the next tune */
void en_repeat(int on);                        /* the tune plays on, no ending */
void en_tune(uint32_t no, uint32_t seed);      /* where the set is: the next tune starts from no + 1 */
void en_note(int note, int vel);               /* keys played along on the vibraphone (vel 0: off) */
void en_panic(void);                           /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
enum { PT_INTRO, PT_HEAD, PT_SOLO, PT_OUT, PT_END, PT_GAP };
extern volatile uint8_t en_playing, en_part, en_repeat_on;
extern volatile uint32_t en_tune_no;           /* the tune playing (1, 2, ...) */
extern volatile uint32_t en_tune_seed;         /* its seed: its title */
extern volatile uint32_t en_seed_base;         /* the set's seed (saved: the tunes to come) */
extern volatile uint32_t en_tune_ms;           /* time into the tune */
extern volatile uint32_t en_tune_len_ms;       /* how long it will run */
extern volatile uint8_t en_key, en_minor;      /* its key now (the lift included): pitch class, minor */
extern volatile uint8_t en_bpm;                /* its tempo */
extern volatile uint8_t en_chord_root;         /* the chord now: pitch class */
extern volatile uint8_t en_chord_q;            /* its quality (en_chord_name) */
extern volatile uint16_t en_chord_pcs;         /* its notes as pitch classes (bit i) */
extern volatile uint8_t en_letter;             /* the form's section now: 'A', 'B', 'C' (0 outside the form) */
extern volatile uint8_t en_chorus, en_choruses;   /* which chorus (0..), of how many */
extern volatile uint8_t en_lead;               /* this tune's melody: 0 flute, 1 vibraphone */
extern volatile float en_vu_l, en_vu_r;        /* the output, 0..1 on a VU's scale */
extern volatile float en_accent;               /* 0..1: a rim click or a melody note just hit (the neon) */
extern volatile uint32_t en_beats;             /* beats played since the start (tests, the screen) */
extern volatile uint32_t en_tunes_played;      /* tunes begun since the start (tests) */
extern volatile float en_tape;                 /* 0 stopped .. 1 rolling (the reels) */

/* the pure parts, for the screen and the tests */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a style's own settings (P_NSTYLE of them) */
uint8_t en_style_neon(int i);                  /* its neon sign's colour: 0..7 */
void en_title(uint32_t seed, char *buf);       /* a tune's title (28 bytes) */
const char *en_chord_name(int root, int q, char *buf);   /* "Dm7", "G7b9", "Cmaj7" (12 bytes) */
uint32_t en_tune_rand(uint32_t base, uint32_t no);       /* tune no's seed from the set's */
const char *en_form_name(void);                /* the tune's form (tests) */

#ifdef EN_HOST
/* the tests' ear: every note as it is scheduled (type: 0 guitar, 1 bass, 2 melody, 3 kick, 4 rim, 5 brush),
 * the bar and the sixteenth it belongs to, the note, the velocity */
extern void (*en_dbg)(int type, int bar, int step, int note, float vel, int dur16);
int en_check_forms(void);                       /* the forms as written: 0 when all are whole */
#endif
