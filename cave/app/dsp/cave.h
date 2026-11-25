/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CAVE: ashtonwallen, 2026 */
/* The instrument: water dripping in a cavern, played as music. Each cave (preset) has a tuning set a composer
 * would choose (a pentatonic, a just or slendro set, a harmonic series, a whole-tone set) and its own layout.
 *
 *   DRIPS      points on the ceiling drip at their own steady periods, so their notes phase against each other.
 *              How many drip, and how fast, follows the cave's wetness, which wanders over hours.
 *   OBJECTS    each drip lands on a stalagmite (a tuned stone) or a pool (a tuned water note). Stalagmites grow
 *              over hours; as one grows its pitch falls to the next note of the set, so the chord drifts but every
 *              note stays in the set. A falling stone can snap a tip off (its note jumps back up).
 *   RIVER      an underground river is the drone: its root moves slowly along a curated path (common tones, near
 *              keys), and the drips that are chord tones of it sound a little louder.
 *   EVENTS     a trickle down the flowstone (a short falling phrase), a flood (the river swells, the drips pour,
 *              then the pools and drip points are rearranged), a falling stone (a thud and bouncing shards), bats
 *              (a flurry of high glints). Keys drop a pebble into a pool (snapped to the set).
 *   SPACE      a big four-line cavern reverb and a slap echo; the AMBIENCE bed (the river's rush, far untuned drips)
 *              has its own level, apart from the music.
 *
 * Threads: en_render() runs in the audio interrupt, and runs the world too. Everything else posts commands, which
 * the render drains at the start of a block. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTYLES 12

/* parameters: ranges in en_param_info(). The first ten are a cave's own (each cave keeps them as left); Pebbles
 * (the keys' level) is the player's */
enum {
    P_DRIPS, P_RING, P_RIVER, P_AMB,                  /* the cave page */
    P_CAVERN, P_ECHO, P_TONE, P_WIDTH,                /* the space page */
    P_GROWTH, P_EVENTS,                               /* the setup page */
    P_PEBBLE,                                         /* the keys' level (global) */
    P_NPARAMS
};
#define P_NSTYLE P_PEBBLE                      /* the ones a cave keeps */
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
void en_param_text(int i, int v, char *buf);   /* the value as the screen shows it (12 bytes) */

extern const char *const EN_NOTE_NAME[12];

/* ---- the world (saved: a cave's growth, its pools and drip points, its weather) */
#define EN_NOBJ 9
#define EN_NDRIP 8
#define EN_WORLD_MAGIC 0x31564143u             /* "CAV1" */
enum { OB_STAL, OB_POOL };
typedef struct {
    uint8_t kind, x;                           /* OB_*, its place across the screen (px) */
    uint8_t deg, lo, hi;                       /* its note: a degree of the set; a stalagmite's range */
    uint8_t rsv;
    uint16_t grow;                             /* growth toward the next note down (to need) */
    uint16_t need;
} en_obj_t;
typedef struct {
    uint8_t x, target, thresh, rsv;            /* place, the object it lands on, the wetness it needs (/250) */
    uint16_t period;                           /* ms between drips at middling wetness */
    uint16_t rsv2;
} en_drip_t;
typedef struct {
    uint32_t magic;                            /* EN_WORLD_MAGIC: a world; anything else: a fresh one */
    uint32_t minutes;                          /* the cave's own time */
    uint32_t rng;
    uint16_t wet, wet_to, wet_left;            /* wetness 0..1000, where it heads, minutes to a new heading */
    uint16_t harm_left;                        /* seconds to the river's next harmony */
    uint16_t flood_left, stone_left, bats_left;   /* minutes to the next */
    uint8_t style, harm, nobj, ndrip;
    uint16_t floods, stones;                   /* how many so far */
    en_obj_t obj[EN_NOBJ];
    en_drip_t drip[EN_NDRIP];
} en_world_t;
typedef char en_world_small[sizeof(en_world_t) <= 200 ? 1 : -1];

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_style(int i);                          /* a cave: a fresh world for it (en_world_put after, to restore one) */
void en_world_put(const en_world_t *w);        /* this world (if valid for the cave playing); w must stay as it is
                                                  until the next block has run */
void en_play(int on);                          /* playing / resting (what rings, rings on) */
void en_next_harm(void);                       /* the river moves on to its next harmony now */
void en_ff(int minutes);                       /* the world runs on this long, silently (tests) */
void en_event(int kind);                       /* an event now: EV_* (tests) */
void en_note(int note, int vel);               /* a key: a pebble into a pool (vel 0: nothing) */
void en_panic(void);                           /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
enum { EV_NONE, EV_RUN, EV_FLOOD, EV_STONE, EV_BATS };
int en_world_get(en_world_t *w);               /* a consistent copy of the world playing (0 = copied) */
extern volatile uint8_t en_playing, en_style_i;
extern volatile float en_flood;                /* the flood now, 0..1 */
extern volatile float en_wet;                  /* the wetness now, 0..1 */
extern volatile uint32_t en_bats_ms, en_stone_ms, en_run_ms;   /* when the last began (en_now_ms; 0 never) */
extern volatile uint8_t en_stone_x, en_bats_dir;
extern volatile int16_t en_harm_cents;         /* the river's root, cents above the cave's tonic */
extern volatile uint8_t en_harm_pc;            /* the river's root as a pitch class (nearest) */
extern volatile uint8_t en_drips_on;           /* drip points dripping (bit i) */
extern volatile uint32_t en_drip_next[EN_NDRIP];   /* each one's next landing (en_now_ms) */
extern volatile float en_vu_l, en_vu_r;        /* the output, 0..1 */
extern volatile uint32_t en_now_ms;            /* the engine's clock */
extern volatile uint32_t en_last_strike_ms;    /* the last note (a quiet moment: the UI saves then) */
extern volatile uint32_t en_notes_played;      /* notes struck since the start (tests) */
extern volatile uint32_t en_retunes;           /* stalagmites retuned since the start (tests) */
extern volatile uint8_t en_voices;             /* voices sounding (tests) */
#define EN_FALL_MS 520u                        /* a drop's fall, ceiling to floor */

/* the strike log, for the screen: each note, when, what it landed on, how long it rings. A ring: en_log_w counts the
 * strikes ever logged; entry i is EN_LOG[i % EN_NLOG] */
#define EN_NLOG 64
enum { R_DRIP, R_SPLASH, R_RUN, R_FLOOD, R_STONE, R_BATS, R_PEBBLE };
typedef struct {
    uint32_t on;                               /* ms (en_now_ms) */
    uint16_t tau;                              /* ms to fade to 1/e */
    uint8_t obj;                               /* the object (255: none) */
    uint8_t x, role, vel, kind;                /* where across the screen; R_*; 0..127; OB_* */
    int8_t pc;                                 /* its pitch class */
} en_logn_t;
extern volatile en_logn_t EN_LOG[EN_NLOG];
extern volatile uint32_t en_log_w;

/* the pure parts, for the screen and the tests */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a cave's own settings (P_NSTYLE of them) */
const char *en_style_scale(int i);             /* "major pentatonic", "slendro", ... */
int en_style_tonic(int i);                     /* the set's lowest note, MIDI */
int en_style_nset(int i);                      /* notes an octave in its set */
int en_deg_cents(int style, int deg);          /* a degree's pitch: cents above MIDI 0 */
int en_cents_pc(int cents);                    /* the nearest pitch class */
void en_world_fresh(int style, en_world_t *w); /* a cave as it is first found */
int en_world_valid(const en_world_t *w, int style);
uint32_t en_hash(uint32_t base, uint32_t no);

#ifdef EN_HOST
/* the tests' ear: every note as it sounds (ms, cents above MIDI 0, velocity 0..1, R_*, the object), every move of the
 * river (its root's cents above MIDI 0) and every world event */
extern void (*en_dbg_note)(uint32_t ms, int cents, float vel, int role, int obj);
extern void (*en_dbg_harm)(uint32_t ms, int cents);
extern void (*en_dbg_event)(uint32_t ms, int kind, const char *what);
#endif
