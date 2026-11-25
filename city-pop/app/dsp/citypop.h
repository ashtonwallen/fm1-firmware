/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CITY POP: ashtonwallen, 2026 */
/* The instrument: endless generated 1980s Japanese city pop. Each song is generated from a seed: a key, a tempo, a
 * form (intro, verse, pre-chorus, chorus, interlude, verse, pre-chorus, chorus, bridge or solo, a key change up for
 * the last chorus, a fade), chosen from a small set of the genre's progressions (the royal road IV-V-iii-vi, the
 * IV-III7-vi of "two of us", ii-V-I, borrowed iv, secondary dominants, slash chords), a groove, and a sung melody
 * written for that song: chord tones on strong beats, steps between, phrases that breathe and repeat, the hook in
 * the chorus. When a song fades, the tape turns over and the next one starts with a new title.
 *
 *   KEYS     a Rhodes or a DX-style FM electric piano, comping in voice-led 7th and 9th chords
 *   GUITAR   four plucked strings (Karplus-Strong), 16th-note cutting and arpeggios, through the chorus
 *   BASS     fingered or slapped (thumb, pops, ghost notes), locked with the kick
 *   BRASS    FM brass stabs, answering the melody's breaths
 *   STRINGS  a detuned-saw section pad, in the choruses (and the bridge)
 *   LEAD     a glossy two-saw synth lead, or a soprano sax (FM with breath), carrying the melody
 *   DRUMS    tight kick, gated snare, 16th hats, open hats, toms into the choruses, a crash
 *   SPACE    a stereo chorus (the gloss), a plate-like four-line reverb, the lead's echo; the bus brightened by
 *            Sparkle, gently saturated and levelled
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at
 * the start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only,
 * no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTYLES 13

/* parameters: ranges in en_param_info(). Every one is a style's own (each style keeps them as left) */
enum {
    P_TEMPO, P_SPARKLE, P_GROOVE, P_GLOSS,            /* the drive page */
    P_KEYS, P_GUITAR, P_BASS, P_DRUMS,                /* the band */
    P_LEAD, P_BRASS, P_STRINGS,                       /* the front line */
    P_NPARAMS
};
#define P_NSTYLE P_NPARAMS                     /* the ones a style keeps: all */
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
void en_style(int i);                          /* a style's flavour (sounds, grooves, progressions): next song */
void en_play(int on);                          /* play / pause */
void en_skip(void);                            /* fade out and on to the next song */
void en_repeat(int on);                        /* the song plays again, no next one */
void en_track(uint32_t no, uint32_t seed);     /* where the tape is: the next song starts from no + 1 */
void en_note(int note, int vel);               /* keys played along (vel 0: off) */
void en_panic(void);                           /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* the song's sections (en_section) */
enum { SEC_INTRO, SEC_VERSE, SEC_PRE, SEC_CHORUS, SEC_INTER, SEC_BRIDGE, SEC_SOLO, SEC_LAST, SEC_FADE, SEC_GAP,
       SEC_N };
const char *en_section_name(int s);            /* "VERSE", "PRE-CHORUS", "KEY UP" ... */

/* state for the screen (UI) */
enum { PH_PLAY, PH_GAP };
extern volatile uint8_t en_playing, en_phase, en_repeat_on;
extern volatile uint32_t en_track_no;          /* the song playing (1, 2, ...) */
extern volatile uint32_t en_track_seed;        /* its seed: its title, key, colours */
extern volatile uint32_t en_seed_base;         /* the tape's seed (saved: the songs to come) */
extern volatile uint32_t en_track_ms;          /* time into the song */
extern volatile uint32_t en_track_len_ms;      /* how long it runs */
extern volatile uint8_t en_key;                /* the key now (pitch class; up after the key change) */
extern volatile uint8_t en_key_up;             /* semitones the last chorus went up (0 before it) */
extern volatile uint8_t en_bpm;                /* its tempo */
extern volatile uint8_t en_chord_root;         /* the chord now: pitch class */
extern volatile uint8_t en_chord_q;            /* its quality (en_chord_name) */
extern volatile uint8_t en_chord_bass;         /* its bass (pitch class; the root unless a slash chord) */
extern volatile uint16_t en_chord_pcs;         /* its notes as pitch classes (bit i) */
extern volatile uint8_t en_section;            /* SEC_*: the section playing */
extern volatile uint8_t en_section_no;         /* 1, 2: which verse, which chorus */
extern volatile uint8_t en_section_bar;        /* bars into it */
extern volatile uint8_t en_section_bars;       /* its length */
extern volatile uint8_t en_lead_sax;           /* this song's lead is the sax */
extern volatile float en_vu_l, en_vu_r;        /* the output, 0..1 on a VU's scale */
extern volatile float en_kick_glow;            /* 0..1: the kick just hit */
extern volatile float en_lead_glow;            /* 0..1: the melody is singing */
extern volatile float en_band[5];              /* 0..1: bass, keys, guitar, lead, drums (the screen's meters) */
extern volatile uint32_t en_beats;             /* beats played since the start (tests, the screen) */
extern volatile uint32_t en_tracks_played;     /* songs begun since the start (tests) */

/* the pure parts, for the screen and the tests */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a style's own settings (P_NSTYLE of them) */
uint8_t en_style_sky(int i);                   /* its sky's colour family */
void en_title(uint32_t seed, char *buf);       /* a song's title (24 bytes) */
uint8_t en_sky_colour(uint32_t seed);          /* its sky: 0..5 */
const char *en_chord_name(int root, int q, int bass, char *buf);   /* "Dm9", "G13", "Fmaj7/G" (16 bytes) */
uint32_t en_track_rand(uint32_t base, uint32_t no);                 /* song no's seed from the tape's */

#ifdef EN_HOST
/* the tests' window on the music: every note as it is scheduled. kind: 'K' keys, 'B' bass, 'G' guitar,
 * 'L' lead, 'S' strings, 'H' brass, 'D' drums (note: 36 kick, 38 snare, 37 rim, 42 hat, 46 open hat, 45 tom,
 * 49 crash); bar and step (16ths) into the song; dur in 16ths */
extern void (*en_log)(int kind, int bar, int step, int note, int vel, int dur);
void en_song_dump(void (*line)(const char *s));   /* the song's plan: its sections and chords, bar by bar */
#endif
