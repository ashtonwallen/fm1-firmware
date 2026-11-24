/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Changed for TIDE CLOCK by ashtonwallen: one object, the settings and the world. */
/* TIDE CLOCK project: one flash object (OBJ_PROJ). A project of another magic or format (AMB-1's, AMB-1 Drift's), a
 * damaged one or none gives the defaults. Part of the unity build, after app.h. The engine's settings are not saved:
 * the world (world.c) makes them from the time of day. */
project_t proj;

void project_defaults(void)
{
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    proj.clock = 0;                              /* Clock: asks the time at power-on */
    proj.cycle = 2;                              /* (Cycle: a day every 4 hours) */
    proj.sea = 5;
    proj.leds = 1;
    proj.tune = 0;
    proj.midi = 1;
    proj.tod = 9u * 3600u;                       /* the first morning */
    proj.seed = 0x7D1DEC0Cu;
    proj.day = 1;
    proj.wx = WX_CALM;
    proj.wx_left = 240;
    proj.mood = 60;
    proj.trust = 10;
}

static int project_valid(void)
{
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT)
        return 0;
    if (proj.clock > 1 || proj.cycle >= NCYCLE || proj.sea > 10 || proj.leds > 2 || proj.midi > 1)
        return 0;
    if (proj.tune < -50 || proj.tune > 50 || proj.tod >= 86400u || !proj.day || proj.wx >= NWX)
        return 0;
    if (proj.mood > 100 || proj.trust > 100 || proj.late > NBOATS || proj.sheltered > NBOATS)
        return 0;
    if (proj.log_n > LOG_N || proj.log_at >= LOG_N)
        return 0;
    return 1;
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

void project_apply(void)
{
    en_set(P_TUNE, proj.tune);
    en_set(P_MIDI, proj.midi);
}
