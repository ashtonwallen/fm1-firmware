/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; POND changes (C) 2026 ashtonwallen */
/* POND project: one flash object (OBJ_PROJ): the settings and the pond. Anything else there (another firmware's
 * project, a damaged one, none) gives the defaults and a new pond. Part of the unity build, after app.h. */
project_t proj;

typedef char proj_fits[sizeof(project_t) <= PLAT_STORE_MAX ? 1 : -1];

/* the player's settings to start from: a calm pond (the rest the engine's defaults; the pond shapes some) */
static const struct {
    uint8_t p;
    int16_t v;
} POND_RECIPE[] = {
    {P_PAD, 50},    {P_BELLS, 70}, {P_BASS, 38},   {P_PULSE, 35},   {P_ECHO, 40},  {P_ETIME, 3},   {P_REVERB, 70},
    {P_SIZE, 85},   {P_BTONE, 30}, {P_TEMPO, 66},  {P_ROOT, 2},     {P_BOCT, 0},   {P_GLIDE, 40},  {P_FIFTH, 30},
    {P_SWING, 10},  {P_HATS, 20},  {P_BASSOCT, 0}, {P_MIDI, 1},     {P_TUNE, 0},
};

void project_defaults(void)
{
    uint32_t i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = en_param_info((int)i)->def;
    for (i = 0; i < sizeof POND_RECIPE / sizeof POND_RECIPE[0]; i++)
        proj.par[POND_RECIPE[i].p] = POND_RECIPE[i].v;
    proj.leds = 1;
    proj.busy = 50;
    proj.daylen = 2;                             /* 24 minutes a day */
    proj.cap = 8;
    pd_new_world(plat_ms() * 2654435761u ^ 0x504F4E44u);
}

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

static int world_ok(const world_t *w)
{
    int i;
    if (!w->day || w->minute >= 1440 || w->nlog > PD_NLOG)
        return 0;
    for (i = 0; i < PD_NFISH; i++) {
        const fish_t *f = &w->fish[i];
        if (f->alive > 1 || (f->alive && (f->species >= PD_NSPECIES || f->temper >= PD_NTEMPER ||
                                          f->name >= PD_NNAMES || f->deg > 13 || f->size > 20 || f->hunger > 100)))
            return 0;
    }
    for (i = 0; i < PD_NLOG; i++)
        if (w->log[i].t[PD_LOGW - 1])            /* every line ends in the line */
            return 0;
    return 1;
}

static int project_valid(void)
{
    return proj.magic == PROJ_MAGIC && proj.format == PROJ_FORMAT && pars_ok(proj.par) && proj.leds <= 2 &&
           proj.busy <= 100 && proj.daylen < 5 && proj.cap >= 3 && proj.cap <= PD_NFISH && world_ok(&proj.w);
}

int project_load(void)
{
    int n;
    memset(&proj, 0, sizeof proj);
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (n == (int)sizeof proj && project_valid()) {
        pd_resume();
        return 0;
    }
    project_defaults();
    return -1;
}

int project_save(void)
{
    pd_keep();
    return plat_store_save(OBJ_PROJ, &proj, sizeof proj);
}

void project_apply(void) { pd_sound(); }
