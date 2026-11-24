/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (AMB-1)
 * Copyright (C) 2026 ashtonwallen (DECAY LOOPS) */
/* DECAY LOOPS project: one flash object (OBJ_PROJ). Anything else there (none, another format, another firmware's)
 * gives the defaults. Part of the unity build, after app.h.
 *
 * The presets (their recipes are the engine's, en_preset): each keeps its own settings as you leave it; Tune is
 * global. The loops themselves are not kept: a power-on records a fresh one. */
project_t proj;

const char *preset_name(int i) { return en_preset_name(i); }

static int global_par(int p) { return p == P_TUNE; }

static void recipe(int i, slot_t *s) { en_preset(i, s->par); }

static void take(const slot_t *s)                /* a preset's settings into play, the globals kept */
{
    int p;
    for (p = 0; p < P_NPARAMS; p++)
        if (!global_par(p))
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
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = en_param_info(i)->def;
    take(&proj.slot[0]);
    proj.leds = 1;
    proj.keyoct = 0;
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
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT || !pars_ok(proj.par))
        return 0;
    if (proj.preset >= NPRESETS || proj.leds > 2 || proj.keyoct < -2 || proj.keyoct > 2)
        return 0;
    for (i = 0; i < NPRESETS; i++)
        if (!pars_ok(proj.slot[i].par))
            return 0;
    return 1;
}

int project_load(void)
{
    static project_t buf;
    int n;
    memset(&buf, 0, sizeof buf);
    n = plat_store_load(OBJ_PROJ, &buf, sizeof buf);
    proj = buf;
    if (n == (int)sizeof buf && project_valid())
        return 0;
    project_defaults();
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
}
