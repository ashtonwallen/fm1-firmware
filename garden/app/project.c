/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (GARDEN) */
/* GARDEN project: one flash object (OBJ_PROJ): the settings and the whole garden (plots, plants, lineage, the
 * gardener, the event lines). Anything else in the store (another firmware's, an older format, damage) gives the
 * defaults and a new garden. Part of the unity build, after app.h. */
project_t proj;

/* the player's settings at the start: AMB-1's "Garden" piece, the garden's own layers (no loops of its own, a
 * quiet pad, no pulse until it rains) */
static void settings_defaults(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = en_param_info(i)->def;
    proj.par[P_TEMPO] = 72;
    proj.par[P_PAD] = 50;
    proj.par[P_BELLS] = 70;
    proj.par[P_BASS] = 30;
    proj.par[P_PULSE] = 0;
    proj.par[P_DENSITY] = 0;             /* the engine's own loops stay empty: the plants are the bells */
    proj.par[P_ECHO] = 40;
    proj.par[P_REVERB] = 65;
    proj.par[P_SIZE] = 85;
    proj.par[P_DRIFT] = 40;
    proj.par[P_ROOT] = 2;
    proj.par[P_MODE] = MODE_DORIAN;
    proj.pool = EN_MODE_POOL[MODE_DORIAN];
    proj.leds = 1;
    proj.growth = 1;                     /* Calm */
}

void project_defaults(void)
{
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    settings_defaults();
    garden_new(0x6A4D3E1u ^ plat_ms());
}

static int pool_ok(uint32_t p)                   /* the root and at least two more notes */
{
    int n = 0, i;
    for (i = 0; i < 12; i++)
        n += (int)(p >> i & 1u);
    return (p & 1u) && !(p >> 12) && n >= 3;
}

static int world_ok(const world_t *w)
{
    int i, k;
    if (w->weather >= W_N || w->g.mood > 100)
        return 0;
    for (i = 0; i < G_NPLOT; i++) {
        const plant_t *p = &w->p[i];
        if (p->stage >= ST_N || p->plen > EN_PH_MAX || p->health > 100 || p->water > 100)
            return 0;
        if (p->stage != ST_EMPTY && (p->pitch < 36 || p->pitch > 96 || p->plen < 1 || !p->rate || p->rate > 3))
            return 0;
        for (k = 0; k < EN_PH_MAX; k++)
            if (p->ph[k] != PH_REST && (p->ph[k] < -14 || p->ph[k] > 14))
                return 0;
    }
    for (i = 0; i < G_NLOG; i++) {      /* each line ends inside its room */
        for (k = 0; k < G_LOGLEN && w->log[i][k]; k++)
            ;
        if (k == G_LOGLEN)
            return 0;
    }
    return 1;
}

static int project_valid(void)
{
    int i;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT || !pool_ok(proj.pool))
        return 0;
    for (i = 0; i < P_NPARAMS; i++) {
        const en_param_t *p = en_param_info(i);
        if (proj.par[i] < p->lo || proj.par[i] > p->hi)
            return 0;
    }
    if (proj.leds > 2 || proj.growth >= G_NGROWTH)
        return 0;
    return world_ok(&proj.w);
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
    return plat_store_save(OBJ_PROJ, &proj, sizeof proj);
}

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, proj.par[i]);
    en_pool(proj.pool);
    garden_resync();
}
