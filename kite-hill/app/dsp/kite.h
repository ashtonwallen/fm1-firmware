/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * KITE HILL: ashtonwallen, 2026 */
/* The instrument: a windy hilltop full of kites that plays itself. Each kite's line hums at a pitch set by its
 * length and tension, always a note of the hill's tuning set and of the harmony now. The world runs on its own:
 *
 *   DAY     still morning, the wind rising at midday, steady afternoon, a gusty evening, easing at dusk; then the
 *           next day, in a near key (a fifth up or down). Its length is a setting (20 min .. 4 h)
 *   WIND    the day's curve, a slow wander, gusts. Kites climb in steady wind and sink in lulls, so their notes swell
 *           and fade; a gust lifts them all together (a big one plucks the lines, low to high)
 *   KITES   new kites go up with new notes from the chord (spread out, never a second apart); now and then one
 *           breaks loose and drifts away, its tone falling slowly to a lower note of the chord as it fades
 *   HARMONY each hill has a short curated ring of chords (common tones between neighbours); the wind sets how often
 *           it moves on (rarely in the morning, on the evening's gusts). The lines retune by a step or two, gliding
 *   HILL    a low drone on the chord's root and fifth; the wind bed (roar, whoosh, grass, a kite's flapping) on its
 *           own level, Ambience
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at the
 * start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only, no
 * libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NSTYLES 12

/* parameters: ranges in en_param_info(). The first eight are a hill's own (each hill keeps them as left);
 * Ambience and Day are global */
enum {
    P_WIND, P_KITES, P_SPACE, P_TONE,                 /* the hill page (with Ambience) */
    P_BREATH, P_DRONE, P_SWELL, P_WIDTH,              /* the line page, the setup page */
    P_AMBIENCE, P_DAYLEN,                             /* global */
    P_NPARAMS
};
#define P_NSTYLE P_AMBIENCE                     /* the ones a hill keeps */
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
void en_style(int i);                          /* a hill: its tuning, its sound, its weather */
void en_play(int on);                          /* playing / resting (the kites reeled in; the wind stays) */
void en_skip(void);                            /* on to the next part of the day */
void en_hold(int on);                          /* the day stands still: this weather goes on */
void en_world(uint32_t day, uint32_t pos, int chord);   /* the world as kept: the day, its position (0..9999), the chord */
void en_note(int note, int vel);               /* a key: a kite goes up on the nearest fitting note (vel 0: nothing) */
void en_panic(void);                           /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
enum { PH_MORNING, PH_MIDDAY, PH_AFTERNOON, PH_EVENING, PH_DUSK, PH_N };
#define EN_NK 6
enum { KS_OFF, KS_FLY, KS_LAND, KS_LOOSE };
typedef struct {
    float x, y;                                /* the kite: 0..1 across, 0..1 up from the hill */
    float ax;                                  /* its anchor on the hill, 0..1 across */
    float amp;                                 /* how loud its line sings, 0..1 */
    float pitch;                               /* MIDI, as it is now (gliding) */
    float to;                                  /* where it is going: its note */
    uint8_t state, col, note, shape;           /* KS_*, its colour, its note (rounded), its kind (diamond, delta, box) */
} en_kite_t;
extern volatile en_kite_t EN_KITE[EN_NK];
extern volatile uint8_t en_playing, en_phase, en_hold_on, en_key, en_chord_i, en_quiet;
extern volatile uint16_t en_chord_pcs;         /* the harmony now, as pitch classes (bit i) */
extern volatile uint8_t en_chord_root;         /* its root (pitch class) */
extern volatile float en_wind, en_gust;        /* 0..1: the wind now, the gust part of it */
extern volatile float en_daypos;               /* 0..1 through the day */
extern volatile uint32_t en_day;               /* the day (1, 2, ...) */
extern volatile float en_vu_l, en_vu_r;        /* the output, 0..1 */
extern volatile uint32_t en_now_ms;            /* the engine's clock */
extern volatile uint32_t en_launches;          /* kites sent up since the start (tests) */
extern volatile uint32_t en_breaks;            /* kites broken loose (tests) */
extern volatile uint32_t en_gusts;             /* gusts (tests) */
extern volatile uint32_t en_changes;           /* harmony changes (tests) */
extern volatile uint8_t en_kites;              /* kites flying (tests) */

/* the pure parts, for the screen and the tests */
const char *en_style_name(int i);
void en_style_recipe(int i, int16_t *par);     /* a hill's own settings (P_NSTYLE of them) */
const char *en_style_mode(int i);              /* its tuning set: "major pentatonic", "Lydian", ... */
int en_style_sky(int i);                       /* its sky's tint (0..3) */
const char *en_phase_name(int ph);             /* "still", "rising", "steady", "gusty", "easing" */
void en_clock(float daypos, char *buf);        /* the hill's time: "14:20" */

#ifdef EN_HOST
/* the tests' ear: every launch, retune, break, gust and chord as it happens (seconds since the start) */
enum { DB_LAUNCH, DB_RETUNE, DB_LOOSE, DB_LAND, DB_GUST, DB_CHORD, DB_DAY, DB_PLUCK };
extern void (*en_dbg)(int what, float t, int kite, float pitch, float x);
#endif
