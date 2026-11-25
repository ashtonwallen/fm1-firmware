/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * BOSSA LOUNGE: ashtonwallen, 2026 */
/* BOSSA LOUNGE on fm1-base (AMB-1's shape): the app's unity build (compiled into fm1-base's, after the platform) and
 * the glue between fm1-base's contract (app.h) and its parts: the project (project.c), the UI (ui.c) and the engine
 * (dsp/bossa.c, its own -O2 unit). */
#include "project.c"
#include "ui.c"

/* ---- the contract */
void app_init(void)
{
    en_init();
    project_load();                      /* defaults when nothing (or another firmware's) is saved */
    project_apply();
    en_tune(proj.tune_no, proj.seed);    /* the set goes on: the next tune after the last one kept */
}
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12) { en_render(lr, n, gain_q12); }
void app_panic(void)
{
    en_play(0);
    en_panic();
}
void app_ui_init(void) { ui_init(); }
void app_frame(void) { ui_frame(); }
void app_input_only(void) { ui_input_only(); }
void app_say(const char *a, const char *b) { ui_say(a, b); }

#ifdef PLAT_HOST
#include "host.c"                        /* the host simulator's part: checks and commands */
#endif
