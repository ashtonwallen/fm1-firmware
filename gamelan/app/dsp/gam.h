/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen; built on AMB-1, Copyright (C) 2026 Charles Vestal */
/* The instrument: endless generated gamelan, in a Javanese / Balinese-inspired style.
 *
 *   THE CYCLE   a gongan of 8, 16 or 32 beats (Lancaran, Ketawang, Ladrang, Gilak). Gong at its end; kenong,
 *               kempul, ketuk and kempyang mark its parts (colotomy).
 *   BALUNGAN    the core melody, one note a beat (or every other: nibani), in gatra of four, each gatra leading
 *               to its last note (seleh). Saron plays it, slenthem an octave below.
 *   ELABORATION peking (each note twice, ahead), bonang (pairs, mipil), and two interlocking parts, polos and
 *               sangsih (kotekan: norot, nyog cag, telu), on the grid between the balungan notes.
 *   KENDANG     a soft drum that keeps the gatra and leads the tempo: it signals a change of irama, then the beat
 *               slows (or quickens) until the elaboration doubles (or halves). Pieces end slowing to the gong.
 *   SULING      (some presets) a bamboo flute line floating over it.
 *   TUNING      slendro and pelog in cents, not 12-TET; octaves slightly stretched. Each metallophone is a pair a
 *               few Hz apart, so the two beat (ombak).
 *   SOUND       modal synthesis: each stroke a few decaying sinusoids (complex phasors), damped by the next stroke
 *               of the same instrument, as players damp; then Dattorro's plate (from AMB-1 / FoMni).
 *
 * New pieces are made endlessly: a buka (bonang alone, then the gong), several gongan with irama changes and
 * tempo drift, an ending (suwuk) slowing to the gong, a few seconds of ringing silence, the next piece.
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at the
 * start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only, no
 * libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_MAXBEATS 32
#define EN_NTONE 5                        /* five tones an octave: slendro, and pelog's modes */

/* the tunings (laras and pathet) */
enum { T_MANYURA, T_SANGA, T_SLNEM, T_PLNEM, T_PLLIMA, T_BARANG, T_SELISIR, EN_NTUNING };
extern const char *const EN_TUNING_NAME[EN_NTUNING];    /* short: the knob */
extern const char *const EN_TUNING_LONG[EN_NTUNING];    /* long: the screen's corner, the reader */
extern const char *const EN_TONE_LABEL[EN_NTUNING];     /* the tones' numbers (kepatihan), "12356" */
extern const int16_t EN_CENTS[EN_NTUNING][EN_NTONE];

/* the forms (P_FORM: 0 Auto, then these) */
enum { F_AUTO, F_LANCARAN, F_KETAWANG, F_LADRANG, F_GILAK, EN_NFORM };
extern const char *const EN_FORM_NAME[EN_NFORM];
extern const char *const EN_IRAMA_NAME[4];              /* P_IRAMA: Auto, I, II, III */

/* the piece's state (en_state) */
enum { S_REST, S_BUKA, S_PLAY, S_SUWUK, S_GAP };

/* parameters: value ranges in en_param_info() */
enum {
    P_TEMPO, P_DENSITY, P_TUNING, P_OMBAK,             /* the main page */
    P_GONGS, P_BALUNGAN, P_KOTEKAN, P_BONANG,          /* the mix */
    P_KENDANG, P_SULING, P_REVERB, P_MALLET,           /* colour */
    P_PITCH, P_IRAMA, P_FORM,                          /* setup */
    P_STYLE, P_FLOW,                                   /* the preset's: Jawa or Bali, how far the tempo moves */
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
void en_play(int on);                          /* plays / rests (what rings, rings on) */
void en_new_piece(void);                       /* the piece ends at the next kenong or gong, then a new one */
void en_restart(void);                         /* a new piece at once (a preset chosen) */
void en_hold(int on);                          /* stay in this piece: no ending */
void en_irama(int dir);                        /* +1 a slower irama (the elaboration doubles), -1 a faster one */
void en_key(int kind, int deg);                /* a key: kind 0 a bar at degree deg, 1.. the gongs and drum (K_*) */
void en_seed(uint32_t s);                      /* stir some entropy into the generators */
void en_panic(void);                           /* everything quiet, now */
enum { K_BAR, K_GONG, K_KEMPUL, K_KENONG, K_KETUK, K_KEMPYANG, K_DHE, K_TAK, K_TUNG, EN_NKEYKIND };

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
extern volatile uint8_t en_playing, en_state, en_hold_on, en_form, en_nbeats, en_beat, en_level, en_sub, en_sirep;
extern volatile uint8_t en_tuning;              /* the tuning the piece plays in */
extern volatile int8_t en_bal[EN_MAXBEATS];     /* this gongan's balungan: degrees (5 an octave), -1 a rest */
extern volatile uint32_t en_mk_gong, en_mk_kempul, en_mk_kenong, en_mk_ketuk, en_mk_kempyang;   /* bit b: beat b */
extern volatile float en_pos;                   /* where in the gongan, in beats (0 .. nbeats) */
extern volatile float en_glow[EN_NKEYKIND];     /* 0..1: a stroke just now (K_GONG ..; K_BAR the balungan) */
extern volatile float en_tone_glow[EN_NTONE];   /* the elaboration's tones, just played */
extern volatile int8_t en_trend;                /* the beat: +1 slowing, -1 quickening, 0 steady */
extern volatile int8_t en_cur;                  /* the balungan's last note (degree), -1 none yet */
extern volatile int8_t en_kenong_at;            /* the beat the last kenong (or gong) stroke marked, -1 none */
extern volatile uint8_t en_gongan;              /* gongan played in this piece */
extern volatile uint32_t en_ticks, en_gongs, en_pieces, en_strokes, en_irama_changes;   /* (tests) */
extern volatile float en_out_level;             /* the output's level, slow (0..1) */

/* the pure parts, for the tests and the screen */
float en_deg_hz(int tuning, int pitch, int deg); /* a degree's frequency */
void en_gen_test(uint32_t seed, int form, int tuning, int8_t *bal, int *nbeats);   /* a gongan, made */

/* the presets: recipes for every parameter */
#define EN_NPRESETS 14
const char *en_preset_name(int i);
void en_preset(int i, int16_t *par);
