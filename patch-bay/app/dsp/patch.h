/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen; built on AMB-1 by Charles Vestal */
/* PATCH BAY's instrument: a small virtual modular synth of sixteen fixed modules, patched by cables.
 *
 *   CLOCK  SEQ    RAND   QUANT      control: gates (G) and CVs (C), computed every 32 samples
 *   LFO 1  LFO 2  ENV 1  ENV 2
 *   VCO    FM     NOISE  VCF        sound (A): computed a block of 32 samples at a time, in the patch's order
 *   VCA    DELAY  MIX    REVERB
 *
 * Every input takes one cable; an output feeds any number. Gates and CVs go to gate and CV inputs, audio to audio
 * inputs only. Pitch is in scale steps: a pitch CV of 0..1 is three octaves of the scale (quantized at the
 * oscillator, so anything patched to a pitch input plays in key). MIX goes to the speaker through the plate
 * (REVERB, whose input is the mix plus anything patched into it), then a slow level keeper and a soft limiter.
 *
 * Threads: pb_render() runs in the audio interrupt; everything else posts commands that the render drains at the
 * start of a block. The state marked (UI) is written by the render and only read elsewhere. Float only, no libm. */
#pragma once
#include <stdint.h>

#define PB_SR 44100.0f

/* the modules, in the screen's order (four rows of four) */
enum { M_CLK, M_SEQ, M_RAND, M_QUANT, M_LFO1, M_LFO2, M_ENV1, M_ENV2, M_VCO, M_FM, M_NOISE, M_VCF, M_VCA, M_DLY,
       M_MIX, M_VERB, PB_NMOD };

/* the jacks: inputs, then outputs */
enum {
    I_SEQ_CLK, I_RAND_CLK, I_QUANT_IN, I_ENV1_TRIG, I_ENV2_TRIG, I_VCO_PITCH, I_FM_PITCH, I_FM_INDEX, I_VCF_IN,
    I_VCF_CUT, I_VCA_IN, I_VCA_CV, I_DLY_IN, I_MIX_A, I_MIX_B, I_MIX_C, I_VERB_IN, PB_NIN
};
enum {
    O_CLK_BEAT, O_CLK_DIV, O_SEQ_CV, O_SEQ_GATE, O_RAND_SH, O_RAND_SM, O_QUANT_NOTE, O_QUANT_CHG, O_LFO1_SIN,
    O_LFO1_SQR, O_LFO2_SIN, O_LFO2_SQR, O_ENV1, O_ENV2, O_VCO, O_FM, O_NOISE, O_DUST, O_VCF, O_VCA, O_DLY, PB_NOUT
};
#define PB_NJACK (PB_NIN + PB_NOUT)      /* jack j: input j, or output j - PB_NIN */
enum { T_GATE, T_CV, T_AUDIO };

typedef struct {
    uint8_t mod, type, out;              /* its module, its kind (T_*), 1 an output */
    const char *name;                    /* as the panel's footer shows it */
} pb_jack_t;
extern const pb_jack_t PB_JACK[PB_NJACK];
extern const char *const PB_MOD_NAME[PB_NMOD];    /* "VCO", "LFO 1" */
extern const char *const PB_MOD_WORDS[PB_NMOD];   /* for the screen reader: "Oscillator" */
extern const char *const PB_PARAM_NAME[PB_NMOD];  /* each module's one knob */
int pb_mod_jacks(int m, int *j);         /* module m's jacks, left to right as on its panel (ins, then outs) */
void pb_param_text(int m, int v, char *b);        /* the knob's value as shown (10 bytes) */

/* the patch: cab[in] = the output feeding input in, or -1 */
int pb_fits(int in, int out);            /* the kinds go together */
int pb_loops(const int8_t *cab);         /* 1: the audio path runs in a circle */
int pb_legal(const int8_t *cab);         /* every cable fits, no circle */
int pb_alive(const int8_t *cab, const uint8_t *par, int out);   /* the output moves (a gate that comes, a CV that changes) */
int pb_heard(const int8_t *cab, int out);   /* an audio output reaches the speaker */
int pb_musical(const int8_t *cab, const uint8_t *par);   /* the auto-patcher's test: sounds, moves, never a raw hiss */
int pb_ncables(const int8_t *cab);

/* globals (pb_global) */
enum { G_TEMPO, G_SPACE, G_ROOT, G_SCALE, G_VCO_OCT, G_FM_OCT, G_ATT1, G_ATT2, G_RES, G_DLY, G_HARM, PB_NG };
#define PB_NSCALES 8
extern const char *const PB_SCALE_NAME[PB_NSCALES];
extern const char *const PB_NOTE_NAME[12];
int pb_scale(int s, int *steps);         /* scale s's steps (semitones from the root); count */

/* commands (main loop -> render) */
void pb_init(void);
void pb_connect(int in, int out);        /* out -1: unplugged */
void pb_param(int m, int v);             /* module m's knob, 0..100 */
void pb_global(int g, int v);
void pb_seq(int step, int v);            /* the sequencer's step (0..7): scale steps 0..20, 31 a rest */
void pb_play(int on);
void pb_panic(void);
void pb_seed(uint32_t s);
void pb_restart(void);                   /* a new patch: the sequencer, the harmony and the clock from the top */

void pb_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);   /* audio ISR: n stereo frames, 24-bit; MASTER Q12 */

/* state for the screen (UI) */
extern volatile uint8_t pb_playing;
extern volatile float pb_val[PB_NOUT];   /* each output now: a gate 0/1, a CV 0..1, audio its level 0..1 */
extern volatile uint8_t pb_seq_pos;
extern volatile uint8_t pb_note[3];      /* the notes (MIDI) of QUANT, VCO, FM */
extern volatile float pb_fm_index;       /* FM's index now */
extern volatile float pb_vcf_hz;         /* VCF's cutoff now */
extern volatile float pb_vca_gain;
extern volatile float pb_level;          /* the output's level (0..1, slow) */
extern volatile float pb_peak;           /* the output's peak since the UI last cleared it (0..1) */
extern volatile uint32_t pb_eighths;     /* eighth notes played since the start */
extern volatile uint8_t pb_harm;         /* the harmony's degree now (scale steps above the root) */
extern volatile float pb_dly_echo[4];    /* the echoes' levels (DELAY's panel) */
extern volatile uint32_t pb_moves;       /* (tests) the connections the render has taken */

/* the presets */
#define PB_NPRESETS 12
#define PB_MAXCAB 16
typedef struct {
    const char *name;
    uint8_t g[PB_NG];                    /* the globals */
    uint8_t autor;                       /* the auto-patcher's rate (an index, ui.c's AUTO_S) */
    uint8_t par[PB_NMOD];
    int8_t seq[8];                       /* -1 a rest */
    uint8_t cab[PB_MAXCAB][2];           /* input, output; ends at an input of 0xFF */
} pb_preset_t;
extern const pb_preset_t PB_PRESET[PB_NPRESETS];
/* each module's knob, the auto-patcher's safe range (it never turns one out of it) */
extern const uint8_t PB_SAFE[PB_NMOD][2];

/* the auto-patcher: one move, the first random candidate whose patch is pb_musical (seed: its random numbers);
 * last: the move before (not undone straight away), or 0. 1 = found */
enum { MV_SRC, MV_DST, MV_ADD, MV_DEL };
typedef struct {
    uint8_t kind;                        /* MV_* */
    int8_t in, out;                      /* the cable after: its input and output (DEL: in -1, out the cable's) */
    int8_t in_old, out_old;              /* before: its input and output (ADD: -1) */
} pb_move_t;
int pb_auto_move(const int8_t *cab, const uint8_t *par, uint32_t *seed, const pb_move_t *last, pb_move_t *mv);
void pb_apply_move(int8_t *cab, const pb_move_t *mv);
