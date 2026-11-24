/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Tavern: ashtonwallen, 2026 */
/* TAVERN's project: one flash object (OBJ_PROJ): the settings and the whole tavern (the regulars, their moods and
 * quarrels, tonight's clock, the memorable nights). A project of another format or another firmware, or none,
 * gives the defaults: a fresh tavern on its first night. Part of the unity build, after app.h. */
project_t proj;

static int player_par(int p) { return p < P_TEMPO; }   /* the tune's own (tempo, root, mode) are the world's */

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = en_param_info(i)->def;
    proj.leds = 1;
    proj.night_len = 3;                          /* an hour a night */
    world_bind(&proj.world);
    world_defaults();
}

static int project_valid(void)
{
    int i;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT || proj.leds > 2 || proj.night_len > 5)
        return 0;
    for (i = 0; i < P_NPARAMS; i++) {
        const en_param_t *p = en_param_info(i);
        if (proj.par[i] < p->lo || proj.par[i] > p->hi)
            return 0;
    }
    return world_valid();
}

int project_load(void)
{
    int n;
    memset(&proj, 0, sizeof proj);
    world_bind(&proj.world);
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (n == (int)sizeof proj && project_valid())
        return 0;
    project_defaults();
    return -1;
}

int project_save(void) { return plat_store_save(OBJ_PROJ, &proj, sizeof proj); }

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        if (player_par(i))
            en_set(i, proj.par[i]);
}
