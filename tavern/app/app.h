/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Tavern: ashtonwallen, 2026 */
/* TAVERN app: the project (everything that is saved: the settings and the tavern), the app's entry points and
 * its half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "amb.h"
#include "world.h"

#define APP_NAME "TAVERN"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#ifdef APP_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION APP_VERSION
#else
#define EN_VERSION "DEV"
#endif

/* the store: the project at 0xFC000 and the panel calibration at 0xDC000, where AMB-1 kept them */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}
#define PROJ_MAGIC 0x31564154u           /* "TAV1": another firmware's project (AMB-1's "ENO1") gives the defaults */
#define PROJ_FORMAT 3u                   /* 3 (1.2): each preset's ambience; formats 1 (1.0) and 2 (1.1) carried over */
#define P_NSAVED P_TEXTURE               /* the settings saved: 1.0's (Drift 1.1's air and shimmer are the preset's) */
#define NPRESET 12
#define AMB_STEPS 25                     /* the ambience knob: 0 (off) .. 25, 5 % a step: 125 % of the preset's air */
#define AMB_DEF 20                       /* 100 %: the preset's own level */

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NSAVED];               /* the mix, the room, setup (the tune's own are the world's), under the preset */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t night_len;                   /* how long a night takes: world.c NIGHT_MIN[] */
    uint8_t preset;                      /* the preset (1.1): project.c PRESET[], 0 The Corner (1.0's session) */
    uint8_t rsv0;
    world_t world;                       /* the tavern */
    uint8_t amb[NPRESET];                /* (1.2) each preset's ambience: AMB_STEPS, AMB_DEF its own level */
    uint8_t rsv[4];                      /* room to grow */
} project_t;
typedef char project_fits_its_store[sizeof(project_t) <= 3840 ? 1 : -1];   /* (a saved object: 3840 bytes at most) */

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* the settings to the engine (boot, load) */
/* the presets (1.1): the session's style, over the knobs and the world's choices (project.c) */
const char *preset_name(int i);
int preset_par(int p, int v);            /* the engine's value of setting p: the knob's v under the preset */
void preset_kinds(int *w);               /* the kinds' weights (reel, jig, waltz, air), moved by the preset */
int preset_kind_pct(int kind);           /* the preset's weight for a kind, % (100: as 1.0) */
int preset_mode(uint32_t r10);           /* a tune's mode from r10 (0..9): the preset's folk modes */
int preset_tempo(int t);
int preset_songs(void);                  /* % chance of a song when one may come (1.0: 20) */
int preset_inst(int inst, int level);    /* an instrument's level in the band, by the preset */
int preset_air(int i);                   /* the preset's air outside: -1 none, else P_TEXTURE's (0 rain .. 3 fire) */

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

/* the host simulator's part (app/host.c): the tavern's checks, its commands, the screen reader's dump */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_SPEAKDUMP
void app_speak_dump(void);
#define APP_HOST_REBOOT
void app_host_reboot(void);              /* the project forgotten, as RAM is at a restart */
