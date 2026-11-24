/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; GAMELAN: Copyright (C) 2026 ashtonwallen */
/* GAMELAN project: one flash object (OBJ_PROJ). A project of another firmware or format, or none, gives the
 * defaults. Part of the unity build, after app.h. Each preset keeps its own settings as it is left. */
project_t proj;

const char *preset_name(int i) { return en_preset_name(i); }

static void recipe(int i, slot_t *s) { en_preset(i, s->par); }

static void take(const slot_t *s)
{
    int p;
    for (p = 0; p < P_NPARAMS; p++)
        proj.par[p] = s->par[p];
}

static void keep(void)                           /* the preset playing, into its slot */
{
    int p;
    for (p = 0; p < P_NPARAMS; p++)
        proj.slot[proj.preset].par[p] = proj.par[p];
}

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < NPRESETS; i++)
        recipe(i, &proj.slot[i]);
    take(&proj.slot[0]);
    proj.leds = 1;
    proj.hold = 0;
}

typedef char proj_fits_the_store[sizeof(project_t) <= PLAT_STORE_MAX ? 1 : -1];   /* one flash object */

static int pars_ok(const int16_t *par)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++) {
        const en_param_t *p = en_param_info(i);
        if (par[i] < p->lo || par[i] > p->hi)
            return 0;
    }
    return 1;
}

static int project_valid(void)
{
    int i;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT || proj.preset >= NPRESETS || proj.leds > 2 ||
        proj.hold > 1 || !pars_ok(proj.par))
        return 0;
    for (i = 0; i < NPRESETS; i++)
        if (!pars_ok(proj.slot[i].par))
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
    project_defaults();                          /* nothing, another firmware's, or damaged */
    return -1;
}

int project_save(void)
{
    keep();
    return plat_store_save(OBJ_PROJ, &proj, sizeof proj);
}

void project_preset(int i)
{
    keep();
    proj.preset = (uint8_t)((i % NPRESETS + NPRESETS) % NPRESETS);
    take(&proj.slot[proj.preset]);
}

void project_preset_reset(void)
{
    slot_t s;
    recipe(proj.preset, &s);
    take(&s);
}

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, proj.par[i]);
    en_hold(proj.hold);
}
