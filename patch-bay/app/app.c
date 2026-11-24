/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen; built on AMB-1 by Charles Vestal */
/* PATCH BAY on fm1-base: the app's unity build (compiled into fm1-base's, after the platform) and the glue between
 * fm1-base's contract (app.h) and the parts: the project (project.c), the patch's hands (bay.c), the UI (ui.c) and
 * the instrument (dsp/patch.c, its own -O2 unit). */
#include "project.c"
#include "bay.c"
#include "ui.c"

void app_init(void)
{
    pb_init();
    project_load();                      /* defaults when nothing (or another firmware's) is saved */
    project_apply();
}
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12) { pb_render(lr, n, gain_q12); }
void app_panic(void)
{
    pb_play(0);
    pb_panic();
}
void app_ui_init(void) { ui_init(); }
void app_frame(void) { ui_frame(); }
void app_input_only(void) { ui_input_only(); }
void app_say(const char *a, const char *b) { ui_say(a, b); }

#ifdef PLAT_HOST
#include "host.c"                        /* the host simulator's part: checks and commands */
#endif
