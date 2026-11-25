/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * FM CHIP: ashtonwallen, 2026 */
/* The instrument: endless generated 16-bit console game music in the style of the Mega Drive / Genesis sound chip,
 * written as real tracker patterns and played by a small tracker. Each "song" is made from a seed and a game (a
 * style): a key and a mode, a verse, a chorus and a bridge (each with its own progression from a short list of
 * idiomatic ones, seventh chords included), melodies built from curated one-bar motifs against the chords, a busy
 * FM bass, chord stabs or pads, PCM drums. The song loops a few times like a game track, ends with a jingle (some
 * games first lift the last chorus a key up, as ending credits do), and the next one starts.
 *
 *   FM1   the lead: four-operator FM (brass, synth lead, reed, flute, guitar, bells), vibrato, bends
 *   FM2   the duo: an echo of the lead, harmony in thirds, the lead an octave down, chip arpeggios, or a
 *         countermelody of long guide tones
 *   FM3-5 the chords: three voices, voice-led, as stabs (with the push before the bar), comping, pads or a
 *         broken-chord arpeggio
 *   FM6   the bass: slap, synth or finger bass, octaves, walking and funk lines
 *   DAC   PCM drums: 8-bit samples at 11 kHz (kick, snare, toms, clap, crash, an orchestra hit)
 *   PSG   the hats: the square chip's noise channel
 *
 * The FM: six channels of four operators (phase-modulated sines, 10-bit sine table), the eight classic algorithms,
 * operator 1's feedback, per-operator ADSR envelopes in dB (attack, decay, sustain level, sustain decay, release),
 * multiples, detune and key scaling, as the YM2612 has them, at the output rate, envelopes every 0.73 ms;
 * the bus goes through a two-pole low-pass (Tone: the console's own output filter) and a leveller.
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at
 * the start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only,
 * no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTYLES 13

/* parameters: ranges in en_param_info(). The first ten are a game's own (each keeps them as left); Loops is the
 * player's */
enum {
    P_TEMPO, P_ENERGY, P_ECHO, P_MOOD,                /* the play page */
    P_LEAD, P_CHORDS, P_BASS, P_DRUMS,                /* the mix */
    P_SWING, P_TONE,                                  /* the song page */
    P_LOOPS,                                          /* how many times a song loops before its ending (global) */
    P_NPARAMS
};
#define P_NSTYLE P_LOOPS                       /* the ones a game keeps */
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
#define EN_NORD 48
#define EN_NCH 8
#define EN_NFM 6
enum { CH_LEAD, CH_DUO, CH_C1, CH_C2, CH_C3, CH_BASS, CH_DAC, CH_PSG };
#define NOTE_NONE 0u
#define NOTE_REL 0xFEu                         /* "===": key off (the release) */
#define NOTE_CUT 0xFFu                         /* "---": the note cut short */
#define LAY_ECHO 0x80u                         /* a cell of the echo (the Echo knob plays it) */
typedef struct {
    uint8_t note;                              /* MIDI note; the DAC and the PSG: 1 (a hit) */
    uint8_t ins;                               /* FM: the patch; DAC: the sample; PSG: the hat. 0xFF none */
    uint8_t fx, fxp;                           /* effect letter ('0' arp, '4' vibrato, 'Q' slide up), 0 none */
    uint8_t lay;                               /* the energy layer 0..4 that plays it (| LAY_ECHO) */
    uint8_t vol;                               /* 1..15 (an accent 15, a ghost note low); 0 the default */
} en_cell_t;
typedef struct {
    en_cell_t pat[EN_NPAT][EN_ROWS][EN_NCH];
    uint8_t plen[EN_NPAT];                     /* rows in each pattern: 16 or 32 */
    uint8_t pchord[EN_NPAT][2];                /* each bar's chord: root pc | quality << 4 (0xFF none) */
    uint8_t order[EN_NORD];                    /* the patterns in play order */
    uint8_t osect[EN_NORD];                    /* the section of each: 'I' intro 'A' verse 'B' chorus 'C' bridge
                                                * 'K' the last chorus a key up, 'E' the ending jingle */
    uint8_t nord;                              /* the order list's length (the jingle last) */
    uint8_t loop_to;                           /* where a loop goes back to */
    uint8_t kc_at;                             /* the first entry after the loops (the key change, or the jingle) */
    uint8_t npat;
    uint8_t key, mode;                         /* its key (pitch class) and mode (en_mode_name) */
    uint8_t kc;                                /* the key change, semitones (0 none) */
    uint8_t style;
    uint8_t bpm;
    char title[24];                            /* upper case */
} en_song_t;

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_style(int i);                          /* a game: from the next song */
void en_play(int on);                          /* playing / stopped */
void en_skip(void);                            /* on to the next song (at once, its intro) */
void en_repeat(int on);                        /* the song loops on, no ending */
void en_song_at(uint32_t no, uint32_t seed);   /* where the player is: the next song is no + 1 */
void en_note(int note, int vel);               /* keys played along (vel 0: off) */
void en_panic(void);                           /* everything quiet, now */
void en_keys_patch(int p);                     /* the patch the keys play (-1: the game's lead) */
void en_retune(void);                          /* the Mood moved: once it rests the song closes, the next in the new mood */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
enum { PH_INTRO, PH_BODY, PH_JINGLE, PH_GAP };
extern const en_song_t *volatile en_cur;       /* the song playing */
extern volatile uint8_t en_playing, en_phase, en_repeat_on;
extern volatile uint8_t en_ord, en_row;        /* where it is: the order list's entry, the row */
extern volatile uint8_t en_loop;               /* the loop playing (0 the first) */
extern volatile uint8_t en_loops;              /* loops before the ending (Loops) */
extern volatile uint32_t en_song_no;           /* the song playing (1, 2, ...) */
extern volatile uint32_t en_seed_base;         /* the player's seed (saved: the songs to come) */
extern volatile uint8_t en_bpm;
extern volatile uint32_t en_rows_played;       /* rows played since the start (tests, the screen) */
extern volatile uint32_t en_songs_played;      /* songs begun since the start (tests) */
extern volatile float en_vu;                   /* the output, 0..1 */
extern volatile uint8_t en_ch_note[EN_NCH];    /* each channel's note now (0 silent) */
extern volatile uint8_t en_ch_ins[EN_NCH];     /* its patch (sample, hat) now */
extern volatile uint8_t en_ch_lvl[EN_NCH];     /* its level now, 0..255 */
extern volatile uint8_t en_op_lvl[EN_NFM][4];  /* each FM operator's envelope now, 0..255 (0 silent) */
extern volatile uint8_t en_op_stage[EN_NFM][4];/* its stage: 0 off, 1 attack, 2 decay, 3 sustain, 4 release */
extern volatile uint8_t en_kick;               /* counts the kicks (a beat light) */
extern volatile uint8_t en_drum_hits[8];       /* counts each DAC sample's hits (the pads light) */

/* the pure parts, for the screen and the tests */
#define EN_NDRUMS 8
enum { DR_KICK, DR_SNARE, DR_TOMH, DR_TOML, DR_CLAP, DR_CRASH, DR_HIT, DR_RIM };
const char *en_drum_name(int i);               /* "KICK" ... */
typedef struct {
    uint8_t mul, tl, ar, dr, sl, sr, rr, ks;   /* YM2612 units: MUL 0..15 (0 a half), TL 0..127 (0.75 dB), rates 0..31,
                                                * SL 0..15 (3 dB), KS 0..3 */
    int8_t dt;                                 /* detune -3..3 */
} en_op_t;
typedef struct {
    const char *name;                          /* "BRASS" (8 characters at most) */
    uint8_t alg, fb;                           /* algorithm 0..7, operator 1's feedback 0..7 */
    en_op_t op[4];
} en_patch_t;
const en_patch_t *en_patch(int i);             /* a patch (an FM channel's ins) */
int en_npatches(void);
int en_alg_carriers(int alg);                  /* the algorithm's carriers, as bits (operator 1 bit 0) */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a game's own settings (P_NSTYLE of them) */
const char *en_mode_name(int m);               /* "MAJOR", "DORIAN" ... */
void en_chord_text(uint8_t pc_q, char *buf);   /* "Am7", "Fmaj7", "G7" (8 bytes) */
uint32_t en_song_rand(uint32_t base, uint32_t no);   /* song no's seed from the player's */
void en_make_song(en_song_t *s, int style, uint32_t seed, int mood, int bpm);   /* a song, as the player makes it */
int en_next_ord(const en_song_t *s, int ord, int loop, int loops, int repeat);   /* the entry after ord (-1: the end) */
