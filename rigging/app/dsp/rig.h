/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * RIGGING: ashtonwallen, 2026 */
/* The instrument: a tall ship crossing open ocean, played by the wind. Every rope and stay is a tuned string
 * that hums when the wind hits it (an Aeolian tone: a few partials and a breath); which ropes sound depends on
 * the wind's strength and its angle to the ship. The angle off the wind (close-hauled .. running) picks the chord
 * from the preset's chain, so a change of course moves the harmony one step at a time; each rope takes the
 * chord tone nearest its own pitch, and only once it has gone quiet (the old and the new never overlap).
 *
 *   WORLD   the wind (a slow mean, gusts on the swell's crests, squalls, calms), the swell, the course (the helm
 *           changes it now and then; keys trim it), the watch's bells every half hour, and a slow move to a near
 *           key ("waters") every hour or so
 *   ROPES   twelve: shrouds low, stays in the middle, topmast stays high. Each a pair of strands a hair apart, an
 *           octave and a twelfth (the harmonics take over as the wind rises), and a noise-driven resonator
 *   HULL    a low hum on the chord's root, by the ship's way through the water; the bell
 *   BED     the Ambience: wind, the sea's wash, sails filling and flapping, the hull's creaks
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at
 * the start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only,
 * no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTYLES 12
#define EN_NR 12                               /* ropes */
#define EN_NSECT 5                             /* points of sail: close-hauled, close reach, beam, broad, run */

/* parameters: ranges in en_param_info(). All ten are a preset's own (each kept as left) */
enum {
    P_WIND, P_GUSTS, P_SWELL, P_AMB,                  /* the sea page */
    P_BREATH, P_TONE, P_HULL, P_BELL,                 /* the rig page */
    P_ROOM, P_WIDTH,                                  /* the setup page */
    P_NPARAMS
};
#define P_NSTYLE P_NPARAMS
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
void en_param_text(int i, int v, char *buf);   /* the value as the screen shows it (12 bytes) */

extern const char *const EN_NOTE_NAME[12];

/* the world worth keeping (saved at most hourly) */
typedef struct {
    float heading, wind_dir, wind_mean;        /* degrees (0 north, clockwise); the wind's slow mean 0..1 */
    uint32_t watch_s;                          /* seconds into the watch's bells (eight bells: 4 h) */
    uint32_t rng;
    uint32_t next_course_s, next_weather_s, next_waters_s, next_squall_s, next_calm_s;   /* seconds to go */
    uint8_t waters, rsv[3];
} en_world_t;

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_style(int i);                          /* a preset: its harmony and weather */
void en_play(int on);                          /* playing / resting */
void en_trim(int deg);                         /* the course a few degrees (+ to starboard) */
void en_about(void);                           /* a new course now */
void en_hold(int on);                          /* the helm held: no course changes by itself */
void en_world_put(const en_world_t *w);        /* a saved world back (boot) */
void en_world_get(en_world_t *w);              /* the world as it is now (main loop) */
void en_panic(void);

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
extern volatile uint8_t en_playing, en_hold_on;
extern volatile float en_rope_amp[EN_NR];      /* how loud each rope sounds, 0..1 */
extern volatile float en_rope_harm[EN_NR];     /* its harmonics' share (whistling), 0..1 */
extern volatile uint8_t en_rope_note[EN_NR];   /* its note (MIDI, rounded) */
extern volatile float en_heading, en_wind_dir; /* degrees */
extern volatile float en_rel;                  /* the wind's angle off the bow, -180..180 (+: on the starboard side) */
extern volatile float en_wind, en_gust;        /* the wind now (0..1, gusts in), the gust's swell */
extern volatile float en_swell, en_swell_ph;   /* the swell's height 0..1, its phase (turns) */
extern volatile float en_fill, en_luff;        /* the sails: full 0..1, flapping 0..1 */
extern volatile uint8_t en_sector;             /* the point of sail (0 close-hauled .. 4 running) */
extern volatile uint8_t en_squall, en_calm;    /* weather now */
extern volatile uint8_t en_key;                /* the tonic's pitch class (with the waters) */
extern volatile uint8_t en_waters;
extern volatile uint32_t en_watch_s;
extern volatile uint8_t en_bells;              /* the last bell's count (1..8) */
extern volatile uint32_t en_bell_no;           /* bells rung since the start (the screen sees a new one) */
extern volatile float en_vu_l, en_vu_r;
extern volatile uint32_t en_now_ms;
extern volatile uint32_t en_n_gusts, en_n_squalls, en_n_chords, en_n_courses, en_n_onsets;   /* (tests) */
extern volatile uint8_t en_sounding;           /* ropes sounding (tests) */

/* the pure parts, for the screen and the tests */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a preset's own settings */
const char *en_style_mode(int i);              /* "Dorian", "major pentatonic", "just" ... */
const char *en_sector_name(int s);             /* "close-hauled", "close reach", ... */
void en_chord_text(char *buf);                 /* the chord sounding now: "Dm9" (16 bytes) */
int en_beaufort(float wind);                   /* the wind as a force, 0..10 */

#ifdef EN_HOST
/* the tests' ear: every rope that starts to sound (amp rising past a threshold) and every chord as it begins, and
 * a fast mode with no synthesis (the world and the ropes' levels only) */
extern void (*en_dbg_onset)(uint32_t ms, int rope, float midi, float amp);
extern void (*en_dbg_chord)(uint32_t ms, int sector, const float *midi, int n);
extern int en_dbg_fast;
extern int en_dbg_mute;                        /* bit 0 the ropes, 1 the Ambience, 2 the hull and bell (levels) */
/* every sounding rope's pitch (MIDI, fractional for the just presets) now, and its level */
int en_dbg_sounding(float *midi, float *amp);
/* the current chord's tones as pitch classes in cents (0..1199), for the checks */
int en_dbg_chord_cents(int which, int *cents);  /* which 0: the chord now, 1: the one before */
#endif
#if defined EN_HOST || defined PLAT_HOST
void en_dbg_squall(void);                      /* a squall now (the tests) */
#endif
