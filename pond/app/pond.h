/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen */
/* POND: the world. Fish in a pond, each with a name, a species (its voice), a temperament and a note; friendships
 * and rivalries; days, seasons, lily pads, a heron, frogs at night. What is kept across power-offs is world_t (in
 * the project, app.h); the rest is pond.c's runtime. Main loop only. */
#pragma once
#include <stdint.h>

#define PD_NFISH 10                      /* the population cap's ceiling */
#define PD_NLILY 7
#define PD_NLOG 8                        /* the journal */
#define PD_LOGW 30
#define PD_NNAMES 40
#define PD_W 240                         /* the pond on the screen: 240 x 176 */
#define PD_H 176

enum { SP_KOI, SP_GOLD, SP_MINNOW, SP_CARP, PD_NSPECIES };
enum { T_SHY, T_GREEDY, T_SOCIAL, T_GRUMPY, PD_NTEMPER };
enum { SEA_SPRING, SEA_SUMMER, SEA_AUTUMN, SEA_WINTER };
enum { TOD_NIGHT, TOD_DAWN, TOD_DAY, TOD_DUSK };

typedef struct {
    uint8_t alive;                       /* 0 none, 1 alive */
    uint8_t species, temper, name;       /* SP_*, T_*, PD_NAMES[] */
    uint8_t deg;                         /* its note: a step of the season's scale, 0..13 (two octaves) */
    uint8_t size;                        /* 1 (fry) .. the species' biggest */
    uint8_t hunger;                      /* 0 full .. 100 starving */
    uint8_t gen;                         /* 0 an original or a newcomer; fry: a parent's + 1 */
    uint16_t age, life;                  /* in in-world hours: how old, how old it will grow */
    uint8_t ma, pa;                      /* its parents' names + 1 (0: none) */
    uint8_t feed;                        /* food eaten since it last grew */
    uint8_t spawn_cd;                    /* in-world hours to its next fry */
    int16_t x, y;                        /* where it was (pixels x 16) */
} fish_t;

typedef struct {
    uint8_t x, y, r, flower;
} lily_t;

typedef struct {
    uint16_t day;
    char t[PD_LOGW];
} logline_t;

typedef struct {
    uint32_t seed;
    uint16_t day, minute;                /* day 1 .. ; minute of the day 0..1439 */
    uint16_t born, died, herons, meets;
    fish_t fish[PD_NFISH];
    int8_t rel[PD_NFISH][PD_NFISH];      /* -100 rivals .. +100 friends (symmetric) */
    lily_t lily[PD_NLILY];
    uint8_t nlog, rsv[3];
    logline_t log[PD_NLOG];              /* the newest last */
} world_t;

extern const char *const PD_NAMES[PD_NNAMES];
extern const char *const PD_SPECIES[PD_NSPECIES];
extern const char *const PD_TEMPER[PD_NTEMPER];
extern const char *const PD_SEASON[4];
extern const char *const PD_TOD[4];
