/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen */
/* The tavern: eight regulars, a visitor now and then, Hugh the landlord, and the night they share (world.c). All
 * of this is saved (the project, app.h), so the regulars remember who they fell out with. */
#pragma once
#include <stdint.h>

#define NREG 8                           /* the regulars */
#define NCH (NREG + 1)                   /* and a visitor's chair */
#define VISITOR NREG
#define NMEM 8                           /* memorable nights kept */
#define NLOG 6                           /* tonight's last events */
#define LOG_W 56                         /* an event, its text (with its end) */

/* where someone is tonight */
enum { S_HOME, S_SEAT, S_BAR, S_MEND, S_GONE };
/* the night's shape, by the clock (minutes from 18:00) */
enum { PH_EARLY, PH_BUSY, PH_LATE, PH_CLOSING, PH_CLOSED };
#define MIN_BUSY 90                      /* 19:30 */
#define MIN_LATE 300                     /* 23:00 */
#define MIN_ORDERS 390                   /* 00:30 last orders */
#define MIN_TIME 405                     /* 00:45 time, please */
#define MIN_SHUT 420                     /* 01:00 the doors shut */
#define MIN_DAWN 440                     /* the next night begins */

/* what is remembered */
enum { M_NONE, M_LAMENT, M_SONGS, M_STRING, M_STANDOFF, M_PEACE, M_WELCOME, M_FROZEN, M_GREAT, M_NEWTUNE, M_ROUND,
       M_N };

typedef struct {
    int8_t mood;                         /* -100 low .. 100 merry */
    uint8_t skill;                       /* 0..100: grows slowly with playing */
    uint8_t thirst;                      /* 0..100 */
    uint8_t drinks;                      /* tonight */
    uint8_t state;                       /* S_* */
    uint8_t inst;                        /* amb.h I_* */
    uint8_t name;                        /* the visitor: VISITOR_NAME[]; a regular: their own */
    uint8_t arrive, leave;               /* tonight, in 10-minute slots from 18:00 */
    uint8_t busy;                        /* tunes left to sit out (a pint, a string) */
    uint8_t play;                        /* in the tune now */
    uint8_t flags;                       /* CF_* */
    uint16_t led, nights;                /* tunes led, nights come, ever */
    uint16_t fav[2];                     /* favourite tunes: seed, the kind in the top two bits */
} ch_t;
#define CF_WELCOME 1u                    /* the visitor: the room took them in */
#define CF_KNOWN 2u                      /* the visitor: has been welcomed before (may come back) */
#define CF_ASKED 4u                      /* the visitor: asked to sit in tonight */

typedef struct {
    uint16_t night;
    uint8_t type, a, b, n;
} mem_t;

typedef struct {
    ch_t ch[NCH];
    int8_t aff[NCH][NCH];                /* friendship: -100 at daggers drawn .. 100 thick as thieves */
    uint16_t night;                      /* nights so far */
    uint16_t minute;                     /* tonight's clock: minutes from 18:00 */
    uint32_t tunes;                      /* tunes played, ever */
    uint32_t rng;
    uint16_t round_at;                   /* the minute the last round was bought (+1; 0 none tonight) */
    uint8_t songs;                       /* songs tonight */
    uint8_t nmem;
    mem_t mem[NMEM];                     /* the newest first */
    char log[NLOG][LOG_W];               /* the newest first */
    uint16_t log_min[NLOG];
    uint8_t lament_done, peak;           /* tonight: a lament was played; the most who played one tune */
    uint8_t rsv[14];
} world_t;

/* world.c */
void world_bind(world_t *w);
void world_defaults(void);
int world_valid(void);
void world_start(void);                  /* boot: the room as saved, between tunes */
void world_tick(void);                   /* each frame */
void world_round(void);                  /* REC: a round for the house */
void world_call(void);                   /* EDIT: call for a tune */
void world_key(int pc);                  /* a key: the next tune in it */
void world_restyle(void);                /* a new preset: the band's balance now (the rest from the next tune) */
int world_state(void);                   /* 0 between tunes, 1 a tune, 2 closed */
int world_leader(void);                  /* who leads (NCH: Hugh), -1 nobody */
int world_type(void);                    /* 0 a tune, 1 a song, 2 a lament, 3 the last tune, 4 Hugh's air */
const char *world_tune(void);            /* its name ("" between tunes) */
int world_kind(void), world_root(void), world_mode(void), world_phase(void);
int world_quiet(uint32_t now);           /* a quiet moment to save in */
void world_warp(int n);                  /* (host) the clock N times as fast */
const char *ch_name(int i);              /* NCH: Hugh */
int ch_hair(int i), ch_shirt(int i), ch_skin(int i), ch_sings(int i), ch_rival(int i), ch_friend(int i);
void clock_text(char *b);                /* "9:40 PM" */
void tune_name(uint16_t seed, int kind, char *out, int n);
void mem_text(const mem_t *m, char *out, int n);
