/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Changed for NIGHT DRIVE by ashtonwallen: the project is the presets' knobs, the setup and the odometer. */
/* NIGHT DRIVE app: the project (everything that is saved), the app's entry points and its half of the contract with
 * fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "nd.h"

#define APP_NAME "NIGHT DRIVE"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION APP_VERSION
#else
#define EN_VERSION "DEV"
#endif

/* the store: the project at 0xFC000 and the panel calibration at 0xDC000 (AMB-1's map, kept) */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}
#define PROJ_MAGIC 0x5652444Eu           /* "NDRV": another firmware's project (AMB-1's "ENO1") gives the defaults */
#define PROJ_FORMAT 1u
#define NPRESETS EN_NPRESETS

typedef struct {
    int16_t knob[4];                     /* Speed, Bright, Arp, Drift (P_TEMPO .. P_DRIFT) */
    uint8_t key, prog;                   /* the home key (0..11, minor) and the progression */
} slot_t;

typedef struct {
    uint32_t magic, format;
    uint8_t preset;                      /* which preset */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t usb_full;                    /* the USB audio's level: 0 follows MASTER, 1 full */
    uint8_t rsv0;
    int16_t drums, lead;                 /* the drums' level, the lead's (global) */
    slot_t slot[NPRESETS];               /* every preset as it was left */
    uint32_t odo;                        /* the odometer: tenths of a mile driven */
    uint8_t rsv[16];                     /* room to grow */
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load, a preset) */
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
