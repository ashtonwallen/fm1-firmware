/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen; built on AMB-1 by Charles Vestal */
/* PATCH BAY: the project (everything that is saved), the app's entry points and its half of the contract with
 * fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "patch.h"

#define APP_NAME "PATCH BAY"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_CANVAS_PX (240u * 176u)      /* the patch, all four rows of modules, drawn and sent in one go */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION APP_VERSION
#else
#define EN_VERSION "DEV"
#endif

/* the store: the project and the panel calibration where AMB-1 keeps them */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}
#define PROJ_MAGIC 0x59414250u           /* "PBAY" */
#define PROJ_FORMAT 1u

#define NAUTO 12                         /* the auto-patcher's rates (ui.c AUTO_S) */

typedef struct {
    uint32_t magic, format;
    uint8_t preset;                      /* the preset it started from */
    uint8_t autor;                       /* the auto-patcher's rate: AUTO_S[]; 0 off */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t sel;                         /* the module the cursor is on */
    uint8_t g[PB_NG];                    /* tempo, space, key, scale and the voicing (patch.h G_*) */
    uint8_t par[PB_NMOD];                /* each module's knob */
    int8_t seq[8];                       /* the sequencer's steps (-1 a rest) */
    int8_t cab[PB_NIN];                  /* the patch: the output each input takes, -1 none */
    uint8_t rsv[16];                     /* room to grow */
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */
void project_preset(int i);              /* preset i's recipe into the project */
int project_valid(const project_t *p);

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
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
