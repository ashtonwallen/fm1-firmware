/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen
 * (the command queue and the render's shape after AMB-1's engine, Copyright (C) 2026 Charles Vestal) */
/* MINIMAL MACHINE's engine: endless minimal techno from six short loops, each its own length, so they phase
 * against each other (Steve Reich's phasing, Basic Channel's chords):
 *
 *   KICK    a deep sine kick, pitch falling, four to the floor (16 steps, the anchor)
 *   HATS    soft noise hats, closed, accented or open
 *   PERC    a rim, a clap or a shaker
 *   STAB    a dub chord stab: detuned saws through a resonant low-pass, into the echo
 *   BASS    a sub bass, a sine with a little warmth, on a few notes of the key
 *   BLEEP   a small FM ping, a wooden knock or a glass bell
 *
 * Each part is a loop of 3..32 sixteenths. Its steps have priorities: DENSITY sets how many of them sound (the
 * most important first), so turning it adds and removes hits in order. Every few bars (DRIFT) one thing mutates:
 * a hit moves, a note changes, a step goes back home, a loop grows or shrinks by a step, a part's tone leans, a
 * part rests or comes back, the stab moves to its second chord. Never a jump.
 *
 * Then a tempo-synced ping-pong echo (darker and a little driven each repeat), a small room, a kick-keyed duck and
 * a soft limiter: no clipping, nothing harsh.
 *
 * Threads: mm_render() runs in the audio interrupt. Everything else posts commands, which the render drains at the
 * start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only, no
 * libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define MM_SR 44100.0f
#define MM_NT 6                          /* the parts */
#define MM_MAXLEN 32                     /* a loop's longest */
enum { T_KICK, T_HATS, T_PERC, T_STAB, T_BASS, T_BLEEP };
extern const char *const MM_PART_NAME[MM_NT];
extern const char *const MM_NOTE_NAME[12];

/* parameters (the knobs): ranges in mm_param_info() */
enum {
    P_DENSITY, P_DRIFT, P_FILTER, P_ECHO,             /* the machine */
    P_DRUMS, P_STAB, P_BASS, P_BLEEP,                 /* the mix */
    P_SWING, P_ETIME, P_REVERB, P_DUCK,               /* the space */
    P_TEMPO, P_KEY,                                   /* setup */
    P_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} mm_param_t;
const mm_param_t *mm_param_info(int i);
void mm_param_text(int i, int v, char *buf);   /* the value as the screen shows it (12 bytes) */

/* the generative state worth keeping (the loops as they have evolved): saved with the project */
typedef struct {
    uint8_t len[MM_NT];
    uint8_t rest;                                 /* bit t: part t is resting (a mutation) */
    uint8_t chord;                                /* the stab's chord: 0 or 1 */
    int8_t tone[MM_NT];                           /* each part's lean, -100..100 */
    uint8_t prio[MM_NT][MM_MAXLEN];               /* low: sounds first */
    uint8_t note[MM_NT][MM_MAXLEN];               /* the step's note or colour */
} mm_gen_t;

/* commands (main loop -> render) */
void mm_init(void);
void mm_set(int param, int value);
void mm_preset(int i);                         /* the preset's sound and its loops as they start */
void mm_play(int on);                          /* running / stopped (the tails ring on) */
void mm_hold(int on);                          /* no mutations while held */
void mm_mute(uint32_t mask);                   /* bit t: part t muted by hand */
void mm_mutate(void);                          /* one mutation, on the next sixteenth */
void mm_seed(uint32_t s);
void mm_panic(void);
void mm_hush(void);                            /* a quiet moment, please: the last sixteenth of the next bar */
void mm_gen_put(const mm_gen_t *g);            /* evolved loops back (g kept until the render took it) */
void mm_gen_get(mm_gen_t *g);                  /* the loops now (main loop; a copy, maybe a step torn) */
int mm_gen_ok(const mm_gen_t *g);              /* in range */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void mm_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
extern volatile uint8_t mm_playing, mm_held, mm_hushed;
extern volatile uint32_t mm_six;               /* sixteenths played since start */
extern volatile float mm_phase;                /* 0..1 through the sixteenth now */
extern volatile uint8_t mm_pos[MM_NT], mm_len[MM_NT];
extern volatile uint32_t mm_pat[MM_NT];        /* bit s: step s sounds */
extern volatile float mm_glow[MM_NT];          /* 0..1: the part just played */
extern volatile float mm_level[MM_NT];         /* 0..1: the part's level now (rests and mutes fade) */
extern volatile int8_t mm_mut_part, mm_mut_step, mm_mut_kind;   /* the last mutation (-1 none / the whole loop) */
extern volatile float mm_mut_glow;             /* 1 when it happened, fading */
extern volatile uint32_t mm_muts;              /* mutations since start */
extern volatile uint8_t mm_chord;              /* the stab's chord (0, 1) */
extern volatile float mm_kick_env;             /* the kick's envelope, 0..1 */
extern volatile float mm_lim_min;              /* the limiter's least gain since set to 1 (tests) */
enum { MU_MOVE, MU_NOTE, MU_HOME, MU_TONE, MU_REST, MU_BACK, MU_LONGER, MU_SHORTER, MU_CHORD, MU_N };
extern const char *const MM_MUT_NAME[MU_N];

/* the presets */
#define MM_NPRESETS 12
const char *mm_preset_name(int i);
void mm_preset_pars(int i, int16_t *par);      /* its knobs as they start */
void mm_preset_home(int i, mm_gen_t *g);       /* its loops as they start */
const char *mm_chord_name(int preset, int which, int key, char *buf);   /* "Fm7", "Bbm7" ... */
