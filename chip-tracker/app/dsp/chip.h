/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CHIP TRACKER: ashtonwallen, 2026 */
/* The instrument: endless generated chiptune in the style of 8-bit console game music, written as real tracker
 * patterns and played by a small tracker. Each "song" is made from a seed and a style: a key and a mode, a verse,
 * a chorus and a bridge (each with its own progression from a short list of idiomatic ones), melodies built from
 * curated one-bar motifs against the chords, a triangle bass, noise drums, and a second pulse that echoes, arpeggiates
 * or harmonises. The song loops a few times like a game track, ends with a short jingle, and the next one starts.
 *
 *   PU1   the lead: a pulse wave, 12.5 / 25 / 50 % duty, volume and duty envelopes, vibrato, slides
 *   PU2   the second pulse: an echo (a quiet delayed copy of the lead), chord arpeggios (0xy), broken chords,
 *         harmony in thirds, or long pad notes
 *   TRI   the bass: a 4-bit stepped triangle, octaves and walking lines
 *   NOI   the drums: a 15-bit LFSR noise channel (and its short "metal" mode), kick, snare, hats, crash
 *
 * Pulses are band-limited (polyBLEP), the noise averaged over each sample, the bus gently low-passed (Tone).
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at
 * the start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only,
 * no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTYLES 13

/* parameters: ranges in en_param_info(). The first ten are a style's own (each style keeps them as left); Loops
 * is the player's */
enum {
    P_TEMPO, P_ENERGY, P_ECHO, P_MOOD,                /* the play page */
    P_PU1, P_PU2, P_TRI, P_NOISE,                     /* the mix */
    P_SWING, P_TONE,                                  /* the song page */
    P_LOOPS,                                          /* how many times a song loops before its jingle (global) */
    P_NPARAMS
};
#define P_NSTYLE P_LOOPS                       /* the ones a style keeps */
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
void en_param_text(int i, int v, char *buf);   /* the value as the screen shows it (12 bytes) */
int en_energy_level(int v);                    /* Energy 0..100 as the layers it lets play: 0..4 */

extern const char *const EN_NOTE_NAME[12];

/* ---- the song: patterns and an order list, as a tracker keeps them (UI reads them; the render writes a new song
 * into the buffer not playing, then switches) */
#define EN_ROWS 32                             /* a pattern: 16 or 32 rows (sixteenths) */
#define EN_NPAT 16
#define EN_NORD 40
#define EN_NCH 4
enum { CH_PU1, CH_PU2, CH_TRI, CH_NOI };
#define NOTE_NONE 0u
#define NOTE_REL 0xFEu                         /* "===": the note released */
#define NOTE_CUT 0xFFu                         /* "---": the note cut */
#define LAY_ECHO 0x80u                         /* a cell of the echo (the Echo knob plays it) */
typedef struct {
    uint8_t note;                              /* MIDI note; the noise: its period 0..15 as 1..16 */
    uint8_t ins;                               /* instrument, 0xFF none */
    uint8_t fx, fxp;                           /* effect letter ('0' arp, '3' porta, '4' vibrato, 'Q' 'R' slides, 'V'
                                                * duty), 0 none; its parameter */
    uint8_t lay;                               /* the energy layer 0..4 that plays it (| LAY_ECHO) */
} en_cell_t;
typedef struct {
    en_cell_t pat[EN_NPAT][EN_ROWS][EN_NCH];
    uint8_t plen[EN_NPAT];                     /* rows in each pattern: 16 or 32 */
    uint8_t pchord[EN_NPAT][2];                /* each bar's chord: root pc | quality << 4 (0xFF none) */
    uint8_t order[EN_NORD];                    /* the patterns in play order */
    uint8_t osect[EN_NORD];                    /* the section of each: 'I' intro 'A' verse 'B' chorus 'C' bridge
                                                * 'E' the ending jingle */
    uint8_t nord;                              /* the order list's length (the jingle last) */
    uint8_t loop_to;                           /* where a loop goes back to */
    uint8_t npat;
    uint8_t key, mode;                         /* its key (pitch class) and mode (en_mode_name) */
    uint8_t style;
    uint8_t bpm;
    char title[24];                            /* upper case */
} en_song_t;

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_style(int i);                          /* a style: from the next song */
void en_play(int on);                          /* playing / stopped */
void en_skip(void);                            /* on to the next song (at once, its intro) */
void en_repeat(int on);                        /* the song loops on, no jingle */
void en_song_at(uint32_t no, uint32_t seed);   /* where the player is: the next song is no + 1 */
void en_note(int note, int vel);               /* keys played along (vel 0: off) */
void en_panic(void);                           /* everything quiet, now */
void en_retune(void);                          /* the Mood moved: once it rests the song closes, the next in the new mood */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
enum { PH_INTRO, PH_BODY, PH_JINGLE, PH_GAP };
extern const en_song_t *volatile en_cur;       /* the song playing */
extern volatile uint8_t en_playing, en_phase, en_repeat_on;
extern volatile uint8_t en_ord, en_row;        /* where it is: the order list's entry, the row */
extern volatile uint8_t en_loop;               /* the loop playing (0 the first) */
extern volatile uint8_t en_loops;              /* loops before the jingle (Loops) */
extern volatile uint32_t en_song_no;           /* the song playing (1, 2, ...) */
extern volatile uint32_t en_seed_base;         /* the player's seed (saved: the songs to come) */
extern volatile uint8_t en_bpm;
extern volatile uint32_t en_rows_played;       /* rows played since the start (tests, the screen) */
extern volatile uint32_t en_songs_played;      /* songs begun since the start (tests) */
extern volatile float en_vu;                   /* the output, 0..1 */
extern volatile uint8_t en_ch_vol[EN_NCH];     /* each channel's volume now, 0..15 */
extern volatile uint8_t en_ch_note[EN_NCH];    /* its note now (0 silent) */
extern volatile uint8_t en_kick;               /* counts the kicks (a beat light) */
/* the oscilloscopes: each channel's last EN_SCOPE samples (int8), the write index, the frequency now (Hz) */
#define EN_SCOPE 2048
extern int8_t en_scope[EN_NCH][EN_SCOPE];
extern volatile uint32_t en_scope_w;
extern volatile float en_ch_hz[EN_NCH];

/* the pure parts, for the screen and the tests */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a style's own settings (P_NSTYLE of them) */
const char *en_mode_name(int m);               /* "MAJOR", "DORIAN" ... */
const char *en_ins_name(int i);                /* an instrument's name ("LEAD 25") */
void en_chord_text(uint8_t pc_q, char *buf);   /* "Am", "F", "Bdim" (8 bytes) */
uint32_t en_song_rand(uint32_t base, uint32_t no);   /* song no's seed from the player's */
void en_make_song(en_song_t *s, int style, uint32_t seed, int mood, int bpm);   /* a song, as the player makes it */
int en_next_ord(const en_song_t *s, int ord, int loop, int loops, int repeat);   /* the entry after ord (-1: the end) */
