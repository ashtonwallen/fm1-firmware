/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (AMB-1)
 * Copyright (C) 2026 ashtonwallen (DECAY LOOPS) */
/* DECAY LOOPS app: the project (everything that is saved), the app's entry points and its half of the contract
 * with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "decay.h"

#define APP_NAME "DECAY LOOPS"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION APP_VERSION
#else
#define EN_VERSION "DEV"
#endif

/* the store: the project and the panel calibration where AMB-1 keeps them (another firmware's project there is
 * not ours: its magic differs, and the defaults come up) */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}
#define PROJ_MAGIC 0x31594344u           /* "DCY1" */
#define PROJ_FORMAT 1u
#define NPRESETS EN_NPRESETS
#define APP_CANVAS_PX (240u * 72u)       /* the deck is drawn in two bands of 72 rows: the pool goes to the tape */

typedef struct {
    int16_t par[P_NPARAMS];
} slot_t;

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS];              /* the preset playing, as it is now (decay.h P_*) */
    uint8_t preset;                      /* which preset */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    int8_t keyoct;                       /* the keys' octave, -2..2 */
    uint8_t rsv0;
    slot_t slot[NPRESETS];               /* every preset as it was left (Tune is global) */
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
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's part (app/host.c) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_SPEAKDUMP
void app_speak_dump(void);
#define APP_HOST_REBOOT
void app_host_reboot(void);
