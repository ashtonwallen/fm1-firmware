/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; NIGHT TRAIN: ashtonwallen */
/* NIGHT TRAIN app: the project (everything that is saved: the settings and the whole journey), the app's entry
 * points and its half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "amb.h"
#include "world.h"

#define APP_NAME "NIGHT TRAIN"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION APP_VERSION
#else
#define EN_VERSION "DEV"
#endif

/* the store: the project at 0xFC000 and the panel calibration at 0xDC000, where AMB-1 keeps them */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}
#define PROJ_MAGIC 0x3152544Eu           /* "NTR1": AMB-1's and AMB-1 Drift's projects ("ENO1") give the defaults */
#define PROJ_FORMAT 1u

typedef struct {
    uint32_t magic, format;
    uint8_t stretch;                     /* minutes between stations: W_STRETCH_MIN[] */
    uint8_t clack;                       /* the wheels' level, 0..100 */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t midi;                        /* MIDI out: 0 off, 1 on */
    int8_t tune;                         /* cents, -50..50 */
    int8_t tone;                         /* darker .. brighter, -50..50 */
    int8_t space;                        /* drier .. wetter, -50..50 */
    uint8_t rsv0;
    uint8_t rsv[8];                      /* room to grow */
    world_t w;                           /* the journey */
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */

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
