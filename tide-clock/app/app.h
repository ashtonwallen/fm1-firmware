/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Changed for TIDE CLOCK by ashtonwallen: the project is the coast's world, not pieces. */
/* TIDE CLOCK app: the project (everything that is saved: the settings and the little world), the app's entry
 * points and its half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "amb.h"

#define APP_NAME "TIDE CLOCK"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION APP_VERSION
#else
#define EN_VERSION "DEV"
#endif

/* the store: the project at 0xFC000 and the panel calibration at 0xDC000 (AMB-1's map, kept) */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}
#define PROJ_MAGIC 0x4B444954u           /* "TIDK": another firmware's project (AMB-1's "ENO1") gives the defaults */
#define PROJ_FORMAT 1u

#define NCYCLE 5                         /* Cycle mode: a whole day every 1, 2, 4, 8 or 12 hours */
#define LOG_N 24                         /* the keeper's log: the last notable events */

/* the weather */
enum { WX_CALM, WX_FOG, WX_WIND, WX_STORM, NWX };
/* what goes in the log (log_t.type); arg: a boat, a count, a length in hours */
enum { EV_NONE, EV_NEWDAY, EV_FOG, EV_FOG_LIFT, EV_WIND, EV_STORM, EV_STORM_END, EV_BOAT_LATE, EV_BOAT_HOME,
       EV_BOAT_SHELTER, EV_SHELL, EV_CAPTAIN, EV_LIGHTNING, EV_MILESTONE, EV_CLEAR_NIGHT, NEV };
#define NBOATS 3

typedef struct {
    uint16_t day;                        /* which day */
    uint16_t min;                        /* minute of that day, 0..1439 */
    uint8_t type, arg;                   /* EV_*; its detail */
} log_t;

typedef struct {
    uint32_t magic, format;
    /* settings */
    uint8_t clock;                       /* 0 Clock (real time, set at power-on), 1 Cycle (a day every N hours) */
    uint8_t cycle;                       /* Cycle's length: CYCLE_H[] */
    uint8_t sea;                         /* the sea's level: 0..10 (5 as composed) */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    int8_t tune;                         /* -50..50 cents */
    uint8_t midi;                        /* MIDI out of the notes: 0 off, 1 on */
    uint8_t rsv0[2];
    /* the world */
    uint32_t tod;                        /* the time of day when last saved, seconds 0..86399 */
    uint32_t seed;                       /* the world's dice */
    uint16_t day;                        /* days survived (the first is 1) */
    uint8_t wx;                          /* the weather now (WX_*) */
    uint8_t stayed;                      /* today: the boats that stayed in harbour (bit a boat) */
    uint16_t wx_left;                    /* world minutes until it may change */
    uint8_t mood;                        /* the keeper's: 0 low .. 100 high */
    uint8_t trust;                       /* Captain the gull's trust in him, 0..100 */
    uint8_t late;                        /* a boat out late: its index + 1, else 0 */
    uint8_t sheltered;                   /* a boat sheltered in the next harbour overnight: its index + 1 */
    uint16_t storms, fogs, shells, lates, visits, nights;   /* the keeper's tallies */
    uint16_t storm_min;                  /* how long this storm has blown (world minutes) */
    uint8_t log_n, log_at;               /* log entries kept; the next one's slot */
    log_t log[LOG_N];
    uint8_t rsv[16];                     /* room to grow */
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* the player's values to the engine (boot, load) */

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

/* the host simulator's part (app/host.c): the world's checks, its commands, the screen reader's dump */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_SPEAKDUMP
void app_speak_dump(void);
#define APP_HOST_REBOOT
void app_host_reboot(void);              /* the project forgotten, as RAM is at a restart */
