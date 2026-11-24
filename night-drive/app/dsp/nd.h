/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen
 * NIGHT DRIVE's engine, on AMB-1's pattern (a command queue drained by the render, float only, fastmath.h, Dattorro's
 * plate from AMB-1). */
/* The instrument: endless generated synthwave. A minor key, a four-chord progression a bar (or two) a chord, and
 * these parts on a 1/16 grid:
 *
 *   PADS    four voices of two detuned saws (or pulses), voice-led from chord to chord, a slow low-pass, ducked a
 *           little by the kick (the pump)
 *   BASS    a saw and a sub through a plucked low-pass: rolling 16ths, octave 8ths or a gallop
 *   ARP     a plucked saw or pulse on the chord's tones, up / up-down / pedal; ARP sets how many steps it plays
 *   LEAD    now and then: a phrase of two bars made on the chord's degree, played twice; saw, FM brass or square
 *   DRUMS   kick, snare through a gated reverb, hats, a riser and toms for the build
 *
 * Sections, endlessly: INTRO, DRIVE, BREAK (no drums), BUILD (a snare roll, a riser), DRIVE again; DRIFT sets how
 * long they are and how often the key and the progression change (a key change lands on a section's first bar).
 * Then a ping-pong delay (a dotted eighth), the plate, and a gentle limiter: never clips.
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at the
 * start of a block; the state below marked (UI) is written by the render and only read elsewhere. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f

/* parameters: ranges in en_param_info() */
enum {
    P_TEMPO, P_BRIGHT, P_ARP, P_DRIFT,                /* the four knobs (each preset keeps its own) */
    P_DRUMS, P_LEAD,                                  /* setup: the drums' level, the lead's level and how often */
    P_TUNE,                                           /* cents */
    P_ROOT,                                           /* the key (A minor = 9): changes on the next bar */
    P_PROG,                                           /* the progression (EN_NPROG) */
    P_SOUND,                                          /* the preset's sounds (EN_NPRESETS) */
    P_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
void en_param_text(int i, int v, char *buf);   /* the value as the screen shows it (16 bytes) */

/* sections */
enum { SEC_INTRO, SEC_DRIVE, SEC_BREAK, SEC_BUILD, EN_NSEC };
extern const char *const EN_SEC_NAME[EN_NSEC];
const char *en_note(int pc, int key);          /* "F#", "Db": a note as the minor key spells it */
int en_sharp_key(int key);                     /* 1: the key is written with sharps */

/* progressions: four chords each, by their root above the key and minor or major */
#define EN_NPROG 8
const char *en_prog_name(int i);               /* "i VI III VII" */
void en_prog_chord(int prog, int k, int *off, int *minor);

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_play(int on);                          /* drive / park (the tails ring on) */
void en_next(void);                            /* the next section, on the next bar */
void en_newkey(void);                          /* a key change of the engine's own choosing, on the next bar */
void en_newarp(void);                          /* the arpeggio's next pattern */
void en_seed(uint32_t s);
void en_panic(void);                           /* everything quiet, now */
void en_restart(void);                         /* a new drive: the intro, from the top */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
extern volatile uint8_t en_playing;
extern volatile uint8_t en_key;                /* the key now (0..11, minor): may have drifted from P_ROOT */
extern volatile uint8_t en_chord_root, en_chord_minor;
extern volatile uint8_t en_chord_idx;          /* 0..3 in the progression */
extern volatile uint8_t en_prog;               /* the progression now (may have drifted from P_PROG) */
extern volatile uint8_t en_section, en_sec_bar, en_sec_len;
extern volatile uint8_t en_step;               /* 0..15 */
extern volatile uint8_t en_arp_pat;
extern volatile uint8_t en_lead_on;            /* a lead phrase is playing */
extern volatile uint32_t en_sixteenths;        /* played since the start (for tests) */
extern volatile uint32_t en_keychanges;        /* key changes so far */
extern volatile float en_beats;                /* 0..16: where in four bars, fractional (the road's scroll) */
extern volatile float en_kick, en_snare, en_hat, en_arp_glow, en_lead_glow, en_bass_glow;   /* 0..1, decaying */
extern volatile float en_pad_level;            /* 0..1 */
extern volatile float en_energy;               /* 0..1: how full the section is (drums, bass) */
extern volatile float en_gr;                   /* the limiter's gain, 0..1 (1: untouched) */

/* the pure parts, for the tests and the screen */
const char *en_chord_name(int root, int minor, int key, char *buf);   /* "F#m", "Db": as the key spells it */
int en_scale_note(int key, int deg, int base);  /* natural minor: degree deg (any) of the key, near base */

/* the presets: recipes for the four knobs, the key, the progression; the sounds are the engine's (P_SOUND) */
#define EN_NPRESETS 14
const char *en_preset_name(int i);
void en_preset(int i, int16_t *par);           /* P_NPARAMS values (Drums, Lead and Tune as their defaults) */
