/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * ORGAN TRIO: ashtonwallen, 2026 */
/* The instrument: an endless soul-jazz organ trio (tonewheel organ with a rotating speaker, jazz guitar, drums) on
 * a bandstand, playing a set. The organist's left hand plays the bass. Each tune is generated from a seed: a kind
 * (greasy medium blues, slow minor blues in 12/8, a soul-jazz groove, a gospel ballad, a shuffle, a swinging
 * standard), a key, a tempo, a form from curated idiomatic changes, a head (riffs, licks, a lyric line), and an
 * arrangement: count-in or intro, the head, organ and guitar solos (the organ building to a climax with the speaker
 * going fast), sometimes fours traded, the head out and an ending. Applause, a pause, and the next tune.
 *
 *   ORGAN   tonewheels from drawbar registrations (two manuals), percussion, key click, scanner chorus, the
 *           preamp's drive, a rotating speaker (horn and drum, each speeding up and slowing down at its own pace)
 *   BASS    the organist's left hand / pedals: walking lines, shuffles, groove ostinatos
 *   GUITAR  plucked strings (Karplus-Strong) through a warm archtop body: comping, lines, octaves, chords
 *   DRUMS   synthesized ride, hi-hat, snare and cross-stick, kick, toms, crash, brushes
 *   CROWD   applause, a murmur and glasses between tunes
 *   ROOM    a four-line club reverb; the bus toned, levelled
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at
 * the start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only,
 * no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTYLES 13
#define EN_NREG 8                              /* the drawbar registrations */

/* parameters: ranges in en_param_info(). The first fourteen are a style's own (each keeps them as left); Stretch
 * is the player's */
enum {
    P_TEMPO, P_INTENS, P_ROOM, P_SWING,               /* the band page */
    P_ORGAN, P_GUITAR, P_DRUMS, P_CROWD,              /* the mix */
    P_DRAW, P_SPEAKER, P_DRIVE, P_PERC,               /* the organ page */
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
void en_note(int note, int vel);               /* keys played along on the organ (vel 0: off) */
void en_panic(void);                           /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
enum { PH_TUNE, PH_APPLAUSE, PH_GAP };
enum { R_COUNT, R_INTRO, R_HEAD, R_OSOLO, R_GSOLO, R_TRADE_O, R_TRADE_G, R_HEADOUT, R_TAG, R_FINAL };
enum { K_BLUES, K_MINOR, K_GROOVE, K_GOSPEL, K_SHUFFLE, K_SWING, K_NKINDS };
enum { HIT_ORG, HIT_CMP, HIT_BASS, HIT_GTR, HIT_RIDE, HIT_HAT, HIT_SNARE, HIT_KICK, HIT_TOM, HIT_CRASH, NHITS };
extern volatile uint8_t en_playing, en_phase, en_repeat_on;
extern volatile uint8_t en_role;               /* the bar's part (R_*) */
extern volatile uint8_t en_soloist;            /* the spotlight: 0 the band, 1 organ, 2 guitar */
extern volatile uint8_t en_climax;             /* the organ is at its peak */
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
extern volatile uint8_t en_beat, en_bpb;       /* the beat in the bar, beats a bar */
extern volatile uint8_t en_hits[NHITS];        /* counters: each hit adds one (the players on screen) */
extern volatile uint8_t en_gtr_note, en_bass_note;   /* the latest notes (MIDI) */
extern volatile uint32_t en_keys_up[2];        /* the upper manual's keys down, MIDI 36..99 (bit n-36) */
extern volatile uint32_t en_keys_lo[2];        /* the lower manual's */
extern volatile float en_draw[9];              /* the upper manual's drawbars as they stand (0..8) */
extern volatile uint8_t en_perc_on;            /* percussion: 0 off, 2 second, 3 third */
extern volatile float en_horn_ph, en_drum_ph;  /* the rotors' angles (turns) */
extern volatile float en_horn_hz;              /* the horn's speed */
extern volatile float en_expr;                 /* the swell pedal, 0..1 */
extern volatile float en_applause;             /* 0..1: the room applauding */
extern volatile float en_vu_l, en_vu_r;        /* the output, 0..1 on a VU's scale */

/* the pure parts, for the screen and the tests */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a style's own settings (P_NSTYLE of them) */
const char *en_kind_name(int k);               /* "Blues", "Gospel ballad", ... */
void en_title(uint32_t seed, int kind, char *buf);   /* a tune's title (32 bytes) */
const char *en_chord_name(int root, int q, char *buf);   /* "Dm7", "G7alt", "Cmaj7" (12 bytes) */
uint32_t en_tune_rand(uint32_t base, uint32_t no);       /* tune no's seed from the set's */
const char *en_reg_text(int i, char *buf);     /* a registration as organists write it: "88 8000 000" */

#ifdef EN_HOST
/* the tests' ear: every note as it is planned (inst: I_* in organ.c: 0 organ right hand, 1 organ chords, 2 bass,
 * 3 guitar line, 4 guitar chords, then the drums); tick in twelfths of a beat from the bar's start */
extern void (*en_trace)(int inst, int rec, int role, int fbar, int tick, int dur, int note, int vel);
extern void (*en_trace_bar)(int rec, int role, int fbar, int chorus, const char *chords);
#endif
