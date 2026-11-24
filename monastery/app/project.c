/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (MONASTERY) */
/* MONASTERY's project: one flash object (OBJ_PROJ): the sound's settings and the world (the brothers, the day, the
 * chronicle). A project of another format or another firmware (AMB-1's, AMB-1 Drift's), or none, gives the
 * defaults. Part of the unity build, after app.h. */
project_t proj;
_Static_assert(sizeof(project_t) <= PLAT_STORE_MAX, "the project fits one store object");

/* the chapel's sound: the pad is the choir (Voices), the bass a drone, the bells the tower bell; no pulse, no loops;
 * a big stone space. Order as amb.h P_* */
static const int16_t SOUND[P_NPARAMS] = {
    72, 62, 34, 0,                       /* Pad (the voices), Bells (the bell), Bass (the drone), Pulse */
    0, 0, 100, 0,                        /* Density (no bell loops), Evolve, Drift, Hits */
    14, 2, 82, 94,                       /* Echo, Echo time, Reverb, Size */
    48, 50, 40, 40,                      /* Bright (the vowels' colour), Swell, Bell tone, Motion */
    0, 0, 60, 2, MODE_DORIAN, 0,         /* Tune, MIDI out, Tempo, Root, Mode, Bell oct */
    1,                                   /* Pad tone: Voices */
    0, 70, 0,                            /* Bass oct, Glide, Fifth */
    0, 50,                               /* Swing, Hats */
};

void project_sound_defaults(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = SOUND[i];
    proj.pace = 100;
}

void project_defaults(void)
{
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    project_sound_defaults();
    proj.leds = 1;
    proj.daylen = 2;                     /* a day in two hours */
    world_defaults(&proj.world);
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

static int project_valid(void)
{
    return proj.magic == PROJ_MAGIC && proj.format == PROJ_FORMAT && pars_ok(proj.par) && proj.leds <= 1 &&
           proj.daylen < WD_NDAY && proj.pace >= 70 && proj.pace <= 140 && world_valid(&proj.world);
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
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, proj.par[i]);
    en_pool(EN_MODE_POOL[MODE_DORIAN]);
}
