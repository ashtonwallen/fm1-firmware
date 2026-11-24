/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (MINIMAL MACHINE) */
/* MINIMAL MACHINE on fm1-base: the app's unity build (compiled into fm1-base's, after the platform) and the glue
 * between fm1-base's contract (app.h) and the parts: the project (project.c), the UI (ui.c) and the engine
 * (dsp/mm.c, its own -O2 unit). (AMB-1's.) */
#include "project.c"
#include "ui.c"

/* ---- the contract */
void app_init(void)
{
    mm_init();
    project_load();                      /* defaults when nothing (or another firmware's project) is saved */
    project_apply();
}
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12) { mm_render(lr, n, gain_q12); }
void app_panic(void)
{
    mm_play(0);
    mm_panic();
}
void app_ui_init(void) { ui_init(); }
void app_frame(void) { ui_frame(); }
void app_input_only(void) { ui_input_only(); }
void app_say(const char *a, const char *b) { ui_say(a, b); }

#ifdef PLAT_HOST
#include "host.c"                        /* the host simulator's part: checks and commands */
#endif
