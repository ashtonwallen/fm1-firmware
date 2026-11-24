/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; GAMELAN: Copyright (C) 2026 ashtonwallen */
/* GAMELAN app: the project (everything that is saved), the app's entry points and its half of the contract with
 * fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "gam.h"

#define APP_NAME "GAMELAN"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION APP_VERSION
#else
#define EN_VERSION "DEV"
#endif

/* the store: the project at 0xFC000 and the panel calibration at 0xDC000 (AMB-1's places) */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}
#define PROJ_MAGIC 0x314C4D47u           /* "GML1" */
#define PROJ_FORMAT 1u                   /* 1 (GAMELAN 1.0) */
#define NPRESETS EN_NPRESETS

typedef struct {
    int16_t par[P_NPARAMS];
} slot_t;

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS];              /* the preset playing, as it is now (gam.h P_*) */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t preset;                      /* which preset */
    uint8_t hold;                        /* stay in one piece */
    uint8_t rsv0;
    slot_t slot[NPRESETS];               /* every preset as it was left */
    uint8_t rsv[16];                     /* room to grow */
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */
const char *preset_name(int i);
void project_preset(int i);              /* the playing preset kept, preset i taken up as it was left */
void project_preset_reset(void);         /* the playing preset back to its recipe */

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
