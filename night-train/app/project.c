/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; NIGHT TRAIN: ashtonwallen */
/* NIGHT TRAIN project: one flash object (OBJ_PROJ), the settings and the journey. A project of another format or
 * another firmware (AMB-1's "ENO1"), or none, gives the defaults: a new journey. Part of the unity build. */
project_t proj;
_Static_assert(sizeof(project_t) <= 3840u, "the project fits one store object (PLAT_STORE_MAX)");

void project_defaults(void)
{
    uint32_t seed = 0x4E54u ^ plat_ms() * 2654435761u;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    proj.stretch = 1;                            /* 5 minutes */
    proj.clack = 70;
    proj.leds = 1;
    proj.midi = 0;
    world_new(&proj.w, seed);
}

static int project_valid(void)
{
    return proj.magic == PROJ_MAGIC && proj.format == PROJ_FORMAT && proj.stretch < W_NSTRETCH && proj.clack <= 100 &&
           proj.leds <= 2 && proj.midi <= 1 && proj.tune >= -50 && proj.tune <= 50 && proj.tone >= -50 &&
           proj.tone <= 50 && proj.space >= -50 && proj.space <= 50 && world_valid(&proj.w);
}

int project_load(void)
{
    int n;
    memset(&proj, 0, sizeof proj);
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (n == (int)sizeof proj && project_valid())
        return 0;
    project_defaults();
    return -1;
}

int project_save(void) { return plat_store_save(OBJ_PROJ, &proj, sizeof proj); }
