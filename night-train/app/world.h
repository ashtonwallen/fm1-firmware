/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen */
/* NIGHT TRAIN's world (world.c): an endless night journey. The train runs a seeded route between stations; each
 * stretch has its landscape, its key, maybe a tunnel, a bridge, a signal stop, rain. At each station it slows, stops,
 * chimes, passengers get off and on, then it pulls away into the next stretch. Six regulars ride it, with small
 * stories that go on from trip to trip. Everything here is saved (project.c), and only plain C: the host tests run
 * it alone. */
#pragma once
#include <stdint.h>

#define NREG 6                               /* the regulars */
enum { CH_INES, CH_MARA, CH_THEO, CH_ALDOUS, CH_JUNE, CH_BISCUIT };
enum { LS_FOREST, LS_COAST, LS_CITY, LS_SNOW, LS_MOUNTAIN, LS_VILLAGE, LS_LAKES, LS_MOOR, NLAND };   /* = amb.c PIECES */
enum { PH_RUN, PH_BRAKE, PH_DWELL, PH_SIGNAL };
#define NSEAT 8
#define NLOG 100
#define W_NSTRETCH 5                         /* the Stretch setting: minutes between stations */
extern const uint8_t W_STRETCH_MIN[W_NSTRETCH];

/* the log: what happened, kept as codes; the words are made when it is read (world_text) */
enum { E_NONE, E_START, E_STATION, E_BOARD, E_LEAVE, E_MISSED, E_WOKEN, E_TALK, E_ARGUE, E_SLEEP, E_WAKE, E_SOLO,
       E_STORY, E_EPI, E_PLAY, E_SIGNAL, E_GREEN, E_DELAY, E_RAIN, E_DRY, E_TUNNEL, E_BRIDGE, E_NTYPES };

typedef struct {
    uint8_t aboard, seat, asleep, mood;      /* mood 0..9 */
    uint8_t stops, beat, trips, away;        /* stops: stations to their own; beat: their story so far; away:
                                              * stations before they may ride again */
} who_t;

typedef struct {
    uint8_t type, a, b, arg;
    uint16_t station;                        /* the station it happened at or before (low 16 bits) */
    uint16_t km;                             /* the journey's km then */
} logrec_t;

typedef struct {
    uint32_t seed, rng;
    uint32_t station;                        /* the stretch running to station `station` (station 0: the start) */
    uint32_t total_m;                        /* metres travelled before this stretch */
    uint32_t run_s;                          /* seconds of journey */
    float pos, len, speed, cruise;           /* m, m, m/s, m/s */
    float timer;                             /* s: the stop's time left (dwell, signal) */
    float talk;                              /* s: to the next moment among the passengers */
    float brake;                             /* m/s/s for this stop */
    float tun0, tun1, brg0, brg1, sig;       /* the tunnel, the bridge (m); a signal stop at (0: none) */
    float sec;                               /* s: towards the next whole second of run_s */
    uint8_t phase, land, rain, key;          /* key: semitones over the landscape's own root */
    uint8_t strangers, theo_play, step, prev_land;   /* step: how far into the stop (0 arrived .. 3 left) */
    uint16_t nlog;                           /* entries ever written: the newest is log[(nlog - 1) % NLOG] */
    uint8_t rsv[2];
    who_t who[NREG];
    int8_t aff[NREG][NREG];                  /* how they get on, -5 .. 12 */
    logrec_t log[NLOG];
} world_t;

/* what a tick did, for the app: the chime, a new stretch (the music moves to it), a new log line */
#define W_CHIME 1u
#define W_SWAP 2u
#define W_LOG 4u
#define W_ARRIVE 8u
#define W_DEPART 16u

void world_new(world_t *w, uint32_t seed);
int world_valid(const world_t *w);
uint32_t world_tick(world_t *w, uint32_t ms, int stretch_min);
void world_express(world_t *w);              /* on to the approach of the next station */
void station_name(uint32_t seed, uint32_t idx, char *b);   /* 20 bytes */
int land_of(uint32_t seed, uint32_t idx);
const char *land_name(int l);
const char *who_name(int c);
const char *who_role(int c);
int world_text(const world_t *w, const logrec_t *r, char *b, int n);   /* the entry in words; its length */
const logrec_t *world_log(const world_t *w, int back);   /* 0 the newest; 0 = none */
int world_in_tunnel(const world_t *w);
int world_on_bridge(const world_t *w);
float world_km(const world_t *w);            /* the journey's distance */
float world_left_km(const world_t *w);       /* to the next station */
int world_at_station(const world_t *w);      /* stopped at a station, the next stretch not yet begun */
const char *world_story(const world_t *w, int c);   /* their latest story line (0: none yet) */
int world_beats(int c);                      /* their story's length */
