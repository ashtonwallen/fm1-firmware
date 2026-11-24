/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen; built on AMB-1 by Charles Vestal */
/* PATCH BAY's project: one flash object (OBJ_PROJ), format 1. Anything else in the store (none, another firmware's,
 * a damaged one) gives the defaults: the first preset. Part of the unity build, after app.h. */
project_t proj;

typedef char proj_fits_the_store[sizeof(project_t) <= PLAT_STORE_MAX ? 1 : -1];

static const uint8_t G_LO[PB_NG] = {40, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0};
static const uint8_t G_HI[PB_NG] = {160, 100, 11, PB_NSCALES - 1, 5, 5, 100, 100, 100, 4, 2};

void project_preset(int i)
{
    const pb_preset_t *s;
    int k;
    i = (i % PB_NPRESETS + PB_NPRESETS) % PB_NPRESETS;
    s = &PB_PRESET[i];
    proj.preset = (uint8_t)i;
    proj.autor = s->autor;
    for (k = 0; k < PB_NG; k++)
        proj.g[k] = s->g[k];
    for (k = 0; k < PB_NMOD; k++)
        proj.par[k] = s->par[k];
    for (k = 0; k < 8; k++)
        proj.seq[k] = s->seq[k];
    for (k = 0; k < PB_NIN; k++)
        proj.cab[k] = -1;
    for (k = 0; k < PB_MAXCAB && s->cab[k][0] != 0xFF; k++)
        proj.cab[s->cab[k][0]] = (int8_t)s->cab[k][1];
}

void project_defaults(void)
{
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    proj.leds = 1;
    proj.sel = M_VCO;
    project_preset(0);
}

int project_valid(const project_t *p)
{
    int i;
    if (p->magic != PROJ_MAGIC || p->format != PROJ_FORMAT || p->preset >= PB_NPRESETS || p->autor >= NAUTO ||
        p->leds > 2 || p->sel >= PB_NMOD)
        return 0;
    for (i = 0; i < PB_NG; i++)
        if (p->g[i] < G_LO[i] || p->g[i] > G_HI[i])
            return 0;
    for (i = 0; i < PB_NMOD; i++)
        if (p->par[i] > 100)
            return 0;
    for (i = 0; i < 8; i++)
        if (p->seq[i] < -1 || p->seq[i] > 20)
            return 0;
    for (i = 0; i < PB_NIN; i++)
        if (p->cab[i] < -1 || p->cab[i] >= PB_NOUT)
            return 0;
    return pb_legal(p->cab);
}

int project_load(void)
{
    static project_t buf;
    int n;
    memset(&buf, 0, sizeof buf);
    n = plat_store_load(OBJ_PROJ, &buf, sizeof buf);
    if (n == (int)sizeof buf && project_valid(&buf)) {
        proj = buf;
        return 0;
    }
    project_defaults();
    return -1;
}

int project_save(void) { return plat_store_save(OBJ_PROJ, &proj, sizeof proj); }

void project_apply(void)
{
    int i;
    for (i = 0; i < PB_NG; i++)
        pb_global(i, proj.g[i]);
    for (i = 0; i < PB_NMOD; i++)
        pb_param(i, proj.par[i]);
    for (i = 0; i < 8; i++)
        pb_seq(i, proj.seq[i]);
    for (i = 0; i < PB_NIN; i++)
        pb_connect(i, proj.cab[i]);
    pb_restart();
}
