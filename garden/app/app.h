/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (GARDEN) */
/* GARDEN app: the project (everything that is saved: the settings and the garden itself), the app's entry points and
 * its half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "amb.h"

#define APP_NAME "GARDEN"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION APP_VERSION
#else
#define EN_VERSION "DEV"
#endif

/* the store: the project at 0xFC000 and the panel calibration at 0xDC000, where AMB-1 kept them */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}
#define PROJ_MAGIC 0x4E445247u           /* "GRDN" */
#define PROJ_FORMAT 2u                  /* 2 (1.1): the preset; format 1 (1.0) loads with Cottage */
#define P_NSAVED P_PLUCK                 /* the player's settings kept: AMB-1's (1.1's new sounds come from the preset) */
#define NPRESETS 13                      /* the garden's sound palettes (garden.c PRESETS) */

/* ---------------------------------------------------------------- the garden (app/garden.c) */
#define G_NPLOT EN_NPLOT                 /* twelve plots: 0..5 the back row, 6..11 the front, left to right */
#define G_NLOG 3                         /* event lines kept */
#define G_LOGLEN 56
enum { ST_EMPTY, ST_SEED, ST_SPROUT, ST_GROW, ST_FLOWER, ST_SEEDING, ST_WITHER, ST_COMPOST, ST_N };
enum { W_SUN, W_CLOUD, W_RAIN, W_FROST, W_N };
#define F_YOURS 1u                       /* planted by a key */
#define F_HYBRID 2u                      /* a bee's cross */
#define F_NIBBLED 4u                     /* a rabbit had a go */
#define F_FAV 8u                         /* the gardener's favourite */
#define F_KEPT 16u                       /* planted by the gardener */
#define PH_REST (-128)
#define NO_PARENT 0xFFu

typedef struct {
    uint8_t stage, pitch, var, adj;      /* pitch: MIDI (the key's, in the scale); name: ADJ[adj] VAR[var] */
    uint8_t pa_adj, pa_var, pb_adj, pb_var;   /* parents' names (var NO_PARENT: none) */
    uint8_t gen, health, water, flags;   /* health, water 0..100 */
    uint16_t age, life;                  /* quarter growth-seconds in this stage, and its length */
    uint8_t plen, rate;                  /* the phrase: its length (2..8), a step every rate quarters */
    int8_t ph[EN_PH_MAX];                /* scale steps from its pitch; PH_REST a rest */
} plant_t;

typedef struct {
    uint8_t name, mood, coat, likes;     /* likes: a weather (W_*) that cheers them */
    uint8_t fav_adj, fav_var, fav_pitch, fav_has;
    uint16_t tended, planted;
} gardener_t;

typedef struct {
    plant_t p[G_NPLOT];
    gardener_t g;
    uint32_t seconds;                    /* seconds the garden has grown */
    uint32_t rng;
    uint16_t day, daysec;                /* the garden's day and how far into it (G_DAY_S a day) */
    uint8_t weather, wleft;              /* the weather and its minutes left */
    uint8_t rsv0[2];
    uint16_t born, died, hybrids, nibbles, frosts, rains, wild, yours;
    char log[G_NLOG][G_LOGLEN];          /* [0] the newest */
} world_t;

#define G_NGROWTH 4                      /* Growth: Slow, Calm, Lively, Quick */

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NSAVED];               /* the player's settings (amb.h P_*); the garden bends some as it plays */
    uint16_t pool;                       /* the scale, relative to the root (bit 0 = the root) */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t growth;                      /* 0..G_NGROWTH-1 */
    uint8_t preset;                      /* the sound's palette (1.1, garden.c PRESETS; 0 Cottage, as 1.0) */
    uint8_t rsv[11];
    world_t w;
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place (a new garden) */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */

/* the garden (garden.c) */
void garden_new(uint32_t seed);          /* a new garden: a new gardener, three starters */
void garden_tick_ms(uint32_t dt);        /* growing time passes (only while playing, not frozen) */
void garden_frame(void);                 /* each frame: the walkers, the music kept in step */
int garden_plant_key(int pitch);         /* a key: a seed at that pitch; the plot, or -1 none free */
void garden_water_all(void);
void garden_resync(void);                /* the scale changed: every plot's notes again */
void garden_sound_all(void);             /* every setting to the engine, as the preset colours it (1.1) */
void garden_preset(int i);               /* PRESETS (1.1): the sound's palette; the garden stays */
void garden_bend_again(void);            /* a setting changed: the bent parameters again */
int garden_living(void);
int garden_count(int stage);
const char *plant_name(const plant_t *p, char *buf);
const char *gardener_name(void);
const char *mood_word(void);

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine, then the saved project (or the defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's part (app/host.c): the garden's checks, its commands, the screen reader's dump */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_SPEAKDUMP
void app_speak_dump(void);
#define APP_HOST_REBOOT
void app_host_reboot(void);              /* the project forgotten, as RAM is at a restart */
