/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * FELT PIANO: ashtonwallen, 2026 */
/* FELT PIANO app: the project (everything that is saved: the styles' settings and where the player is), the app's
 * entry points and its half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "felt.h"

#define APP_NAME "FELT PIANO"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION APP_VERSION
#else
#define EN_VERSION "DEV"
#endif

/* the store: the project at 0xFC000 and the panel calibration at 0xDC000, where AMB-1 kept them */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}
#define PROJ_MAGIC 0x31544C46u           /* "FLT1": another firmware's project (AMB-1's "ENO1") gives the defaults */
#define PROJ_FORMAT 2u                   /* 2: thirty styles (1.1); 1: 1.0's fourteen, still read */
#define NSTYLES EN_NSTYLES

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS];              /* the style playing, as it is now, and Length */
    int16_t slot[NSTYLES][P_NSTYLE];     /* every style as it was left */
    uint8_t style;                       /* which style */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t oct;                         /* the keys' octave, played along: 0..4, 2 the middle */
    uint8_t rsv0;
    uint32_t track_no;                   /* where the player is: the last piece begun */
    uint32_t seed;                       /* the player's seed (the pieces to come) */
    uint8_t rsv[16];                     /* room to grow */
} project_t;
typedef struct {                         /* format 1 (1.0): the same, with fourteen slots */
    uint32_t magic, format;
    int16_t par[P_NPARAMS];
    int16_t slot[EN_NSTYLES_V1][P_NSTYLE];
    uint8_t style, leds, oct, rsv0;
    uint32_t track_no, seed;
    uint8_t rsv[16];
} project_v1_t;
typedef char project_fits_its_store[sizeof(project_t) <= 3840 ? 1 : -1];   /* (a saved object: 3840 bytes at most) */

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */
void project_style(int i);               /* the playing style kept, style i taken up as it was left */
void project_style_reset(void);          /* the playing style back to its recipe */

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

/* the host simulator's part (app/host.c): the checks, its commands, the screen reader's dump */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_SPEAKDUMP
void app_speak_dump(void);
#define APP_HOST_REBOOT
void app_host_reboot(void);              /* the project forgotten, as RAM is at a restart */
