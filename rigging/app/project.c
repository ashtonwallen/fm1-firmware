/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * RIGGING: ashtonwallen, 2026 */
/* RIGGING's project: one flash object (OBJ_PROJ): each preset's settings as it was left, the lights, the helm, and
 * the world (the ship's course, the wind, the watch's bells, the near key). A project of another format or another
 * firmware, or none, gives the defaults. Part of the unity build, after app.h. */
project_t proj;

static void take(int i)                          /* a preset's settings into play */
{
    int p;
    for (p = 0; p < P_NSTYLE; p++)
        proj.par[p] = proj.slot[i][p];
}
static void keep(void)                           /* the preset playing, into its slot */
{
    int p;
    for (p = 0; p < P_NSTYLE; p++)
        proj.slot[proj.style][p] = proj.par[p];
}

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < NSTYLES; i++)
        en_style_recipe(i, proj.slot[i]);
    take(0);
    proj.leds = 1;
    proj.hold = 0;
    proj.world_ok = 0;                           /* a new voyage: the engine's own start */
}

static int pars_ok(const int16_t *par, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        const en_param_t *p = en_param_info(i);
        if (par[i] < p->lo || par[i] > p->hi)
            return 0;
    }
    return 1;
}

static int project_valid(void)
{
    int i;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT || proj.style >= NSTYLES || proj.leds > 2 ||
        proj.hold > 1 || proj.world_ok > 1 || !pars_ok(proj.par, P_NPARAMS))
        return 0;
    for (i = 0; i < NSTYLES; i++)
        if (!pars_ok(proj.slot[i], P_NSTYLE))
            return 0;
    if (proj.world_ok && !(proj.world.heading >= 0.0f && proj.world.heading < 360.0f && proj.world.wind_dir >= 0.0f &&
                           proj.world.wind_dir < 360.0f && proj.world.wind_mean >= -0.3f &&
                           proj.world.wind_mean <= 0.3f))
        proj.world_ok = 0;                       /* (a world that makes no sense: a new voyage, the settings kept) */
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

int project_save(void)
{
    keep();
    en_world_get(&proj.world);
    proj.world_ok = 1;
    return plat_store_save(OBJ_PROJ, &proj, sizeof proj);
}

void project_style(int i)
{
    keep();
    proj.style = (uint8_t)((i % NSTYLES + NSTYLES) % NSTYLES);
    take(proj.style);
}

void project_style_reset(void)
{
    en_style_recipe(proj.style, proj.slot[proj.style]);
    take(proj.style);
}

void project_apply(void)
{
    int i;
    en_style(proj.style);
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, proj.par[i]);
    en_hold(proj.hold);
}
