/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; POND changes (C) 2026 ashtonwallen */
/* POND: the project (everything that is saved: the settings and the pond itself), the app's entry points and its
 * half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "amb.h"
#include "pond.h"

#define APP_NAME "POND"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION APP_VERSION
#else
#define EN_VERSION "DEV"
#endif

/* the store: the project at 0xFC000 and the panel calibration at 0xDC000 (AMB-1's places) */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}
#define PROJ_MAGIC 0x444E4F50u           /* "POND": another firmware's project (AMB-1's "ENO1") gives a new pond */
#define PROJ_FORMAT 1u

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS];              /* the player's settings (amb.h P_*); the pond shapes some (pond.c pd_sound) */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t busy;                        /* 0..100: how lively the pond is */
    uint8_t daylen;                      /* a day's length: pond.c DAY_MIN[] (6 .. 96 minutes) */
    uint8_t cap;                         /* the most fish: 3 .. PD_NFISH */
    uint8_t rsv[12];                     /* room to grow */
    world_t w;                           /* the pond */
} project_t;

extern project_t proj;

void project_defaults(void);             /* the settings and a new pond */
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */

/* the pond (pond.c) */
void pd_new_world(uint32_t seed);
void pd_resume(void);
void pd_keep(void);
void pd_tick(uint32_t now);
void pd_sound(void);
void pd_fast(uint32_t minutes);

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

/* the host simulator's part (app/host.c): POND's checks, its commands, the screen reader's dump */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_SPEAKDUMP
void app_speak_dump(void);
#define APP_HOST_REBOOT
void app_host_reboot(void);              /* the project forgotten, as RAM is at a restart */
