/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (MONASTERY) */
/* MONASTERY (on AMB-1's app): the project (everything that is saved), the app's entry points and its half of the contract with
 * fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "amb.h"

#define APP_NAME "MONASTERY"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION APP_VERSION
#else
#define EN_VERSION "DEV"
#endif

/* the store: the project at 0xFC000 and the panel calibration at 0xDC000, where AMB-1 kept them */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}
#define PROJ_MAGIC 0x314E4F4Du           /* "MON1": another firmware's project (AMB-1's "ENO1") gives the defaults */
#define PROJ_FORMAT 1u

/* the world (world.c): the brothers, the day, the chronicle */
#define NMONK 7
#define NCHRON 12
#define WD_NDAY 5                        /* Day length: 30 min, 1 h, 2 h, 4 h, 8 h */
typedef struct {
    uint8_t here;                        /* 0 not (yet) in the community, 1 in choir, 2 in the infirmary */
    uint8_t skill, health, mood;         /* 0..100 */
    uint8_t ill_days;                    /* days left in the infirmary */
    uint8_t vowed;                       /* a novice who took his vows */
    uint8_t rsv[2];
    uint16_t days, sung, slips, naps, flats, clashes;   /* his history */
} monk_t;
typedef struct {
    uint16_t day;
    uint8_t office, type, who, who2;
} chron_t;
typedef struct {
    uint32_t seed;                       /* the world's dice */
    uint16_t day;                        /* 1 .. */
    uint8_t office;                      /* the office now (world.c OF_*) */
    uint8_t rivalry;                     /* Gregory and Bernard, 0..100 */
    uint8_t peace_days;                  /* they made peace: days left */
    uint8_t nchron, chron_w;             /* the chronicle: entries, the next one's place */
    uint8_t rsv;
    monk_t m[NMONK];
    chron_t chron[NCHRON];
} world_t;

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS];              /* the sound (amb.h P_*) */
    uint8_t leds;                        /* lights: 0 off, 1 on */
    uint8_t daylen;                      /* Day length: 0..WD_NDAY-1 */
    uint8_t pace;                        /* the chant's pace, % (70..140) */
    uint8_t rsv0;
    world_t world;
    uint8_t rsv[16];                     /* room to grow */
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */
void project_sound_defaults(void);       /* the sound back to the defaults (the world kept) */

void world_defaults(world_t *w);
int world_valid(const world_t *w);
void world_init(void);
void world_tick(void);
void world_skip(void);
void world_nudge(int i);
int world_quiet(void);
int world_minutes(void);

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

/* the host simulator's part (app/host.c): the checks, the commands, the screen reader's dump */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_SPEAKDUMP
void app_speak_dump(void);
#define APP_HOST_REBOOT
void app_host_reboot(void);              /* the project forgotten, as RAM is at a restart */
