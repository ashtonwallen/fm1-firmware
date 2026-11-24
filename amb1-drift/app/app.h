/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* AMB-1 app: the project (everything that is saved), the app's entry points and its half of the contract with
 * fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "amb.h"

#define APP_NAME "AMB-1 DRIFT"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION APP_VERSION
#else
#define EN_VERSION "DEV"
#endif

/* the store: the project at 0xFC000 and the panel calibration at 0xDC000, where AMB-1 0.9 kept them (an update keeps
 * the pieces) */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}
#define PROJ_MAGIC 0x314F4E45u           /* "ENO1" */
#define PROJ_FORMAT 6u                   /* 6 (AMB-1 Drift 1.1): 24 pieces, the strings, lead, air and shimmer;
                                          * 5 (Drift 1.0): sixteen pieces and Wander; 4: the bass's and the
                                          * pulse's own settings (3, 4 and 5 are carried over; older formats give
                                          * the recipes) */
#define NPIECES EN_NPIECES
#define V4_NPIECES 8u                    /* AMB-1's eight: formats 3 and 4 */
#define V5_NPIECES 16u                   /* Drift 1.0's sixteen: format 5 */
#define V5_NPARAMS (P_HATS + 1)          /* the settings of formats 4 and 5 */

/* Wander (AMB-1 Drift): the garden moves on to another piece by itself */
#define WD_NEVERY 8
#define WD_NGLIDE 6

typedef struct {
    int16_t par[P_NPARAMS];
    uint16_t pool;
} slot_t;

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS];              /* the piece playing, as it is now (amb.h P_*) */
    uint16_t pool;                       /* its scale, relative to the root (bit 0 = the root) */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t piece;                       /* which piece */
    uint8_t rsv0[2];
    slot_t slot[NPIECES];                /* every piece as it was left (Tune and MIDI out are global) */
    uint8_t wander;                      /* Wander: 0 off, 1 on */
    uint8_t wevery;                      /* how often it moves on: WD_EVERY_MIN[] */
    uint8_t worder;                      /* 0 the next piece, 1 shuffled */
    uint8_t wglide;                      /* how long the move takes: WD_GLIDE_S[] */
    uint8_t rsv[12];                     /* room to grow */
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */
const char *piece_name(int i);
void project_piece(int i);               /* the playing piece kept, piece i taken up as it was left */
void project_piece_reset(void);          /* the playing piece back to its recipe */

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

/* the host simulator's part (app/host.c): AMB-1's checks, its commands, the screen reader's dump */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_SPEAKDUMP
void app_speak_dump(void);
#define APP_HOST_REBOOT
void app_host_reboot(void);              /* the project forgotten, as RAM is at a restart */
