/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * LOFI RADIO: ashtonwallen, 2026 */
/* LOFI RADIO's project: one flash object (OBJ_PROJ): each style's settings as it was left, Length, the lights, and
 * where the radio is (the last track and the seed of those to come). Format 1 (LOFI RADIO 1.0) is the same layout
 * and is carried over as it is; a project of another format or another firmware, or none, gives the defaults. Part of the unity build, after app.h. */
project_t proj;

static void take(int i)                          /* a style's settings into play (Length is the player's) */
{
    int p;
    for (p = 0; p < P_NSTYLE; p++)
        proj.par[p] = proj.slot[i][p];
}
static void keep(void)                           /* the style playing, into its slot */
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
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = en_param_info(i)->def;
    take(0);
    proj.leds = 1;
    proj.oct = 2;
    proj.track_no = 0;
    proj.seed = 0x4C4F4649u;                     /* "LOFI": every FM-1 starts on the same first record */
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
        proj.oct > 4 || !pars_ok(proj.par, P_NPARAMS))
        return 0;
    for (i = 0; i < NSTYLES; i++)
        if (!pars_ok(proj.slot[i], P_NSTYLE))
            return 0;
    return 1;
}

int project_load(void)
{
    int n;
    memset(&proj, 0, sizeof proj);
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (n == (int)sizeof proj && proj.magic == PROJ_MAGIC && proj.format == 1u)
        proj.format = PROJ_FORMAT;               /* 1.0's: the same layout, every value kept (saved as 2 next time) */
    if (n == (int)sizeof proj && project_valid())
        return 0;
    project_defaults();
    return -1;
}

int project_save(void)
{
    keep();
    proj.track_no = en_track_no;
    proj.seed = en_seed_base;
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
}
