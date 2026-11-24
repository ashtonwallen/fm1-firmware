/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Tavern session: ashtonwallen, 2026 */
/* The instrument: a small folk session in a tavern corner, on AMB-1's engine (its plate, its tape echo, its
 * FM voices). The world (app/world.c) decides who is in the room and who plays; this plays the tune.
 *
 *   TUNES   generated from a seed in a folk mode (Ionian, Dorian, Mixolydian, Aeolian): two parts, A and B,
 *           eight bars each, each played twice (AABB) with a first and a second ending, the whole 1..4 times.
 *           Four kinds: reel (4/4), jig (6/8), waltz (3/4), air (slow 4/4, long notes).
 *   BAND    eight instruments, each with a level (0: sitting out) and a skill (timing, ornaments):
 *             fiddle, whistle, voice   the melody (heterophony: each its own ornaments and timing)
 *             harp                     arpeggios under the tune; in an air it may take the melody itself
 *             lute                     the bar's root and soft strums on the off beats
 *             box                      the bar's chord, held (reeds: AMB-1's formant voices)
 *             bass                     plucked roots and fifths
 *             bodhran                  a soft frame drum on the beats (none in an air)
 *           Levels set while a tune plays take effect on the next bar (a player joins on the bar).
 *
 * Then AMB-1's space: an Echoplex and a tape loop (quiet here), and Dattorro's plate as the room.
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render
 * drains at the start of a block; the state below marked (UI) is written by the render and only read
 * elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NPAD 4                      /* the box's reeds */

/* modes: pools relative to the root (bit i: root + i semitones) */
enum { MODE_IONIAN, MODE_DORIAN, MODE_PHRYGIAN, MODE_LYDIAN, MODE_MIXOLYDIAN, MODE_AEOLIAN, MODE_PENT_MAJ,
       MODE_PENT_MIN, EN_NMODES };
extern const char *const EN_MODE_NAME[EN_NMODES];
extern const uint16_t EN_MODE_POOL[EN_NMODES];
extern const char *const EN_NOTE_NAME[12];

/* the band */
enum { I_FIDDLE, I_WHISTLE, I_VOICE, I_HARP, I_LUTE, I_BOX, I_BASS, I_DRUM, EN_NINST };
extern const char *const EN_INST_NAME[EN_NINST];
/* the kinds of tune */
enum { K_REEL, K_JIG, K_WALTZ, K_AIR, EN_NKIND };
extern const char *const EN_KIND_NAME[EN_NKIND];

/* parameters: value ranges in en_param_info() */
enum {
    P_LEAD, P_BACK, P_DRUM,                           /* the mix: melody, backing, bodhran (the player's) */
    P_ECHO, P_REVERB, P_ROOM, P_TONE,                 /* the room */
    P_TUNE, P_MIDI,                                   /* setup */
    P_TEMPO, P_ROOT, P_MODE,                          /* the tune's: set by the world before en_tune */
    P_TEXTURE, P_TEXLVL, P_TEXMOD, P_SHIMMER,         /* 1.1, from AMB-1 Drift 1.1: the air outside (rain, wind,
                                                       * sea, the fire's crackle), its level and sway; shimmer.
                                                       * The preset's (app/project.c), not saved; 0: none */
    P_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
void en_param_text(int i, int v, char *buf);   /* the value as the screen shows it (10 bytes) */

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_play(int on);                          /* the session runs / pauses (the tails ring on) */
void en_seed(uint32_t s);                      /* stir some entropy into the generators */
void en_panic(void);                           /* everything quiet, now */
/* a new tune, now, from bar one (Tempo, Root and Mode as set): its seed, its kind, how many times through
 * (1..4); harp: the harp plays the melody (an air) */
void en_tune(uint16_t seed, int kind, int passes, int harp);
void en_end(void);                             /* the tune ends at the end of this pass's B part (closing time) */
void en_inst(int inst, int level, int skill);  /* 0..100 each: level 0 sits out (from the next bar) */
void en_twang(int inst);                       /* a string breaks: out now, with a twang */
/* MIDI clock in (as FoMni): ticks drive the 1/16 grid (6 a step) while they keep coming */
enum { EN_CLK_TICK, EN_CLK_START, EN_CLK_CONTINUE, EN_CLK_STOP };
void en_clock(int msg);

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* MIDI out, from the render: the app sends it (USB); status, data 1, data 2 */
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

/* state for the screen and the world (UI) */
extern volatile uint8_t en_playing, en_ext;
extern volatile uint8_t en_sess;               /* 0 no tune, 1 a tune playing, 2 its last note ringing */
extern volatile uint8_t en_part, en_rep, en_bar, en_pass, en_passes, en_kind;   /* where in the tune */
extern volatile uint32_t en_tunes_done;        /* tunes finished since the start */
extern volatile uint8_t en_live;               /* bit i: instrument i is playing */
extern volatile float en_glow[EN_NINST];       /* 0..1: instrument i just played a note */
extern volatile uint8_t en_chord_root;         /* the bar's chord: its root, pitch class */
extern volatile uint8_t en_chord[EN_NPAD];     /* the box's notes (MIDI) */
extern volatile uint8_t en_mel;                /* the melody's note now (MIDI), 0 none */
extern volatile uint32_t en_bars;              /* sixteenths played since the start (for tests) */
extern volatile float en_level;                /* the output's level, slowly (quiet moments: saving) */

/* the pure parts, for the tests, the world and the screen */
int en_scale(uint32_t pool, int *steps);       /* the pool's pitch classes (relative), ascending; count */
/* the chord on scale degree deg: its tones (MIDI pitch classes, absolute), 3 or 4 of them; count */
int en_chord_tones(uint32_t pool, int root, int deg, int *pcs);
/* voice leading: new notes for the tones, each voice moving as little as possible, all in lo..hi */
void en_voice_lead(const int *from, const int *pcs, int ntones, int *to, int lo, int hi);
const char *en_chord_name(const int *pcs, int n, char *buf);   /* "Em7", "Dsus4", "C" ... */

/* a tune: the melody in scale degrees from the tonic (EN_TIE: the note before goes on; EN_REST: silence), an
 * eighth a slot; bar 8 of each part is its second ending (bar 7 the first) */
#define EN_TIE (-100)
#define EN_REST (-101)
typedef struct {
    uint8_t kind, steps, eighths, beat;        /* sixteenths a bar (16, 12), eighths a bar (8, 6), eighths a beat */
    int8_t mel[2][9][8];
    int8_t chord[2][9];                        /* each bar's chord: its scale degree */
} en_tune_t;
void en_tune_make(uint16_t seed, int kind, int mode, en_tune_t *t);
