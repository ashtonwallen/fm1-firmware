/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "plat.h"

#define APP_NAME "SHORTWAVE"
#define APP_SPLASH_RGB RGB(154, 221, 173)
#define APP_BLOCK 256u
#define APP_CANVAS_PX (240u * 116u)

enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t frames, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *first, const char *second);

#define APP_HOST_EXPECT
int app_expect(const char *name, int *value);
