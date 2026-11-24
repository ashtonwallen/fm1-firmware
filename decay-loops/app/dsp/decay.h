/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (AMB-1, whose harmony, plate reverb and command queue this keeps)
 * Copyright (C) 2026 ashtonwallen (DECAY LOOPS) */
/* The instrument: a tape looper whose loops fall apart, after William Basinski's Disintegration Loops.
 *
 *   PHRASE   a small synth (piano, choir, strings, brass, organ, glass, bells) plays a phrase: a few chords, a
 *            figure or a slow melody over a drone, in the key and mode. Each new phrase keeps most of the last
 *            one's chords (one may move), so the layers that overlap agree.
 *   TAPE     the phrase is recorded once onto the tape and played from it over and over. The tape is one ring of
 *            mu-law bytes at a quarter of the rate (11025 Hz, 8 bits: 27 s in all), shared by every layer:
 *            each new loop is written after the last, and the oldest is let go when there is no room.
 *   DECAY    each pass the loop wears: its highs roll off and it thins, holes open at a few places and grow
 *            (the same places every pass, as oxide flaking off), the pitch wobbles more and sags, the hiss and
 *            crackle rise, and at the end only fragments are left; then it fades away. Its wear map is a row of
 *            EN_NCELL cells, each going at its own point of the loop's life.
 *   LAYERS   when the newest loop is worn 1/Layers of the way, the next phrase is recorded (starting on the
 *            newest loop's downbeat when the lengths match) and fades in while the older ones keep dying.
 *   KEYS     notes played go onto the loop being recorded; between recordings they sound at once and are
 *            remembered for the next loop.
 * Then a plate reverb (Dattorro's, from AMB-1, at half the rate, 16-bit) and a soft clip.
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at
 * the start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only,
 * no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_TSR 11025.0f                /* the tape's rate */
#define EN_NLAYER 6                    /* loops at once: recording, playing, fading */
#define EN_NCELL 128                   /* a loop's wear map */
#define EN_TAPE_N 297675u              /* the tape, in samples (bytes): 27 s */

/* modes: pools relative to the root (bit i: root + i semitones), AMB-1's */
enum { MODE_IONIAN, MODE_DORIAN, MODE_PHRYGIAN, MODE_LYDIAN, MODE_MIXOLYDIAN, MODE_AEOLIAN, MODE_PENT_MAJ,
       MODE_PENT_MIN, EN_NMODES };
extern const char *const EN_MODE_NAME[EN_NMODES];
extern const uint16_t EN_MODE_POOL[EN_NMODES];
extern const char *const EN_NOTE_NAME[12];

/* tape stocks, voices, phrases */
enum { ST_CASSETTE, ST_REEL, ST_WAX, ST_WIRE, EN_NSTOCK };
enum { VX_PIANO, VX_CHOIR, VX_STRINGS, VX_BRASS, VX_ORGAN, VX_GLASS, VX_BELLS, EN_NVOICE };
enum { PH_CHORDS, PH_FIGURE, PH_MELODY, PH_MIXED, EN_NPHRASE };
extern const char *const EN_STOCK_NAME[EN_NSTOCK];
extern const char *const EN_VOICE_NAME[EN_NVOICE];
extern const char *const EN_PHRASE_NAME[EN_NPHRASE];

/* parameters: value ranges in en_param_info() */
enum {
    P_DECAY, P_LENGTH, P_LAYERS, P_WOW,       /* the tape: how fast it wears, the loop's length (s), overlap, wobble */
    P_STOCK, P_TONE, P_HISS, P_SPACE,         /* its sound: stock, brightness, noise floor, reverb */
    P_VOICE, P_PHRASE, P_DENSITY, P_OCT,      /* the phrase: instrument, kind, how busy, register */
    P_ROOT, P_MODE, P_TUNE,                   /* setup */
    P_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
void en_param_text(int i, int v, char *buf);   /* the value as the screen shows it (12 bytes) */
float en_life_secs(int v);                     /* Decay's value: a loop's life, in seconds */

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_play(int on);                          /* the tape runs / stops (it slows to a halt, and starts up again) */
void en_freeze(int on);                        /* the wear holds */
void en_new(void);                             /* a new loop now */
void en_regrow(void);                          /* a new preset: the loops let go, a fresh one at once */
void en_note(int note, int vel);               /* a key: vel 1..127 down, 0 up */
void en_seed(uint32_t s);                      /* some entropy into the generators */
void en_panic(void);                           /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
enum { LS_FREE, LS_REC, LS_PLAY, LS_FADE };
extern volatile uint8_t en_playing, en_frozen;
extern volatile uint8_t en_lay_state[EN_NLAYER];
extern volatile uint16_t en_lay_serial[EN_NLAYER];   /* which loop: counted from 1 at the start */
extern volatile uint8_t en_lay_voice[EN_NLAYER];
extern volatile float en_lay_wear[EN_NLAYER];        /* 0 fresh .. 1 gone */
extern volatile float en_lay_pos[EN_NLAYER];         /* the playhead, 0..1 of the loop */
extern volatile float en_lay_rec[EN_NLAYER];         /* how much of it is recorded, 0..1 */
extern volatile float en_lay_level[EN_NLAYER];       /* how loud it is now, 0..1 */
extern volatile float en_lay_gain[EN_NLAYER];        /* its fade, 0..1 */
extern volatile float en_lay_secs[EN_NLAYER];        /* its length */
extern volatile uint8_t en_lay_dmg[EN_NLAYER][EN_NCELL];   /* its wear map: 0 whole .. 255 gone */
extern volatile uint32_t en_passes[EN_NLAYER];       /* times round */
extern volatile float en_tape_used;                  /* 0..1 of the tape */
extern volatile float en_speed;                      /* the transport, 0 stopped .. 1 */
extern volatile uint32_t en_loops;                   /* loops recorded since the start */
extern volatile uint32_t en_flakes;                  /* cells that began to go, since the start */
extern volatile float en_out_level;                  /* the output, a slow envelope (autosave's quiet moment) */
extern volatile uint8_t en_prog_n;                   /* the newest phrase's chords: how many */
extern volatile uint8_t en_prog_pcs[4][4];           /* each chord's tones (pitch classes; the first its root) */
extern volatile uint8_t en_prog_nt[4];

/* the pure parts, for the tests and the screen (AMB-1's) */
int en_scale(uint32_t pool, int *steps);
int en_chord_tones(uint32_t pool, int root, int deg, int *pcs);
void en_voice_lead(const int *from, const int *pcs, int ntones, int *to, int lo, int hi);
const char *en_chord_name(const int *pcs, int n, char *buf);
uint8_t en_mulaw(float x);                     /* -1..1 to a mu-law byte (G.711) */
float en_unmulaw(uint8_t u);

/* the presets: tape stocks and moods (Tune is the player's, not the preset's) */
#define EN_NPRESETS 14
const char *en_preset_name(int i);
void en_preset(int i, int16_t *par);
