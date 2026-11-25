/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (MONASTERY) */
/* MONASTERY's project: one flash object (OBJ_PROJ): the sound's settings and the world (the brothers, the day, the
 * chronicle). A project of another format or another firmware (AMB-1's, AMB-1 Drift's), or none, gives the
 * defaults. Formats 1 (1.0) and 2 (1.1) have the same layout (1.0's spare byte, always 0, is now the preset; the
 * room to grow, always 0, now the Ambience per preset): they are carried over, the settings, the preset and the world
 * kept (world.c world_migrate drops what 1.2 no longer has: the brothers' mistakes and dramas), every preset's
 * Ambience at 100 %. Part of the unity build, after app.h. */
project_t proj;
_Static_assert(sizeof(project_t) <= PLAT_STORE_MAX, "the project fits one store object");

/* the chapel's sound: the pad is the choir (Voices), the bass a drone, the bells the tower bell; no pulse, no loops;
 * a big stone space. Order as amb.h P_* */
static const int16_t SOUND[P_NSAVED] = {
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
    for (i = 0; i < P_NSAVED; i++)
        proj.par[i] = SOUND[i];
    proj.pace = 100;
    for (i = 0; i < NPRESET; i++)
        proj.amb[i] = AMB_DEF;
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
    for (i = 0; i < P_NSAVED; i++) {
        const en_param_t *p = en_param_info(i);
        if (par[i] < p->lo || par[i] > p->hi)
            return 0;
    }
    return 1;
}

static int project_valid(void)
{
    int i;
    for (i = 0; i < NPRESET; i++)
        if (proj.amb[i] > AMB_MAX)
            return 0;
    return proj.magic == PROJ_MAGIC && proj.format == PROJ_FORMAT && pars_ok(proj.par) && proj.leds <= 1 &&
           proj.daylen < WD_NDAY && proj.pace >= 70 && proj.pace <= 140 && proj.preset < NPRESET &&
           world_valid(&proj.world);
}

int project_load(void)
{
    int n;
    memset(&proj, 0, sizeof proj);
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (n == (int)sizeof proj && proj.magic == PROJ_MAGIC && (proj.format == 1u || proj.format == 2u)) {
        int i;
        if (proj.format == 1u)           /* 1.0's: on the first preset */
            proj.preset = 0;
        proj.format = PROJ_FORMAT;
        for (i = 0; i < NPRESET; i++)    /* the Ambience as the preset had it */
            proj.amb[i] = AMB_DEF;
        memset(proj.rsv, 0, sizeof proj.rsv);
        world_migrate(&proj.world);
    }
    if (n == (int)sizeof proj && project_valid())
        return 0;
    project_defaults();
    return -1;
}

int project_save(void) { return plat_store_save(OBJ_PROJ, &proj, sizeof proj); }

/* the presets (1.1): where and how the brothers sing. Each moves the knobs' values (levels as %, the space as
 * offsets), may move the offices' finals and modes, the chant's pace, the voices' size and vibrato, and brings in
 * Drift 1.1's sounds: a harp or kalimba (strings), a bowed or glass lead, air (rain, wind, sea, vinyl) and shimmer.
 * Abbey Chapel, the first, changes nothing: 1.0's sound. The world (the brothers, the day, the chronicle) is never
 * touched.
 *   Tr finals moved (semitones); Md mode (-1 the office's); Vo Dr Bl the choir, drone and bell %; BT bell tone (-1 the
 *   knob's); Rv Sz Ec Co reverb, size, echo, colour added; Pc pace %; Fm formants %; Vb vibrato added; Str Den STo SO
 *   strings; Led LTo Oft LO lead; Tx Air Sw air (0 rain, 1 wind, 2 sea, 3 vinyl); Sh shimmer */
enum { R_TR, R_MODE, R_VO, R_DR, R_BL, R_BT, R_RV, R_SZ, R_EC, R_CO, R_PC, R_FM, R_VB, R_STR, R_SDEN, R_STONE, R_SOCT,
       R_LEAD, R_LTONE, R_LOFT, R_LOCT, R_TEX, R_AIR, R_SWAY, R_SHIM, R_N };
static const struct {
    const char *name;
    int16_t v[R_N];
} PRESET[NPRESET] = {
    /*                  Tr  Md  Vo   Dr   Bl   BT  Rv  Sz  Ec  Co   Pc   Fm   Vb  Str Den STo SO  Led LTo Oft LO  Tx Air Sw  Sh */
    {"Abbey Chapel",  { 0, -1, 100, 100, 100, -1,  0,  0,  0,  0, 100, 100,  0,  0, 40, 30, 0,  0, 50, 35, 0,  0,  0, 50,  0}},
    {"Cathedral",     {-2, -1, 100, 110, 120, 20, 12,  6, 10,  5,  85, 103,  0,  0, 40, 30, 0, 30, 20, 25, -1, 0,  0, 50, 50}},
    {"Hermitage",     { 0, -1, 115, 100,  70, -1,-35,-40,-14,  0, 110, 100,  5, 35, 30, 15, 0,  0, 50, 35, 0,  1, 35, 60,  0}},
    {"Cloister",      { 0, -1, 105,  95,  80, -1,-15,-20,  0, -5, 100, 100,  0, 30, 35, 25, 0,  0, 50, 35, 0,  0, 45, 50,  0}},
    {"Mountain Abbey",{-3, -1, 100, 120, 100, 10,  5,  4, 10,  0,  95,  97,  0,  0, 40, 30, 0, 35, 90, 30, 1,  1, 55, 85, 25}},
    {"Byzantine",     { 0,  2, 105, 150,  70, 60,  0,  0,  0, 10,  85,  98, 30,  0, 40, 30, 0,  0, 50, 35, 0,  0,  0, 50, 15}},
    {"Candlelight",   { 0, -1,  90,  90,  60, 80,-10,-10,  0,-15,  90, 100,  0,  0, 40, 30, 0, 30, 95, 20, 1,  3, 30, 40,  0}},
    {"Island Abbey",  { 2, -1, 100, 100,  90, -1,  0,  0, 15,  0, 100, 100,  0, 30, 30, 20, 0,  0, 50, 35, 0,  2, 55, 70, 10}},
    {"Crypt",         {-5, -1, 100, 120,  80, 10,  8,  5,  0,-20,  90,  90,  0,  0, 40, 30, 0, 40, 10, 25, -1, 0,  0, 50,  0}},
    {"Snow Priory",   { 2, -1,  95,  80,  80, 90,  5,  4,  0, 10, 100, 104,  0, 35, 30, 85, 1, 30, 90, 25, 1,  1, 30, 30, 40}},
    {"Pilgrims",      { 0, -1,  95,  90,  70, -1,  0, -5,  5,  0, 105, 100,  5, 45, 40, 20, 0, 35, 15, 30, 0,  1, 35, 70,  0}},
    {"Easter Vigil",  { 0,  3,  95,  90, 115, 70, 10,  5,  0, 15,  95, 102,  0,  0, 40, 30, 0, 30, 85, 35, 1,  0,  0, 50, 60}},
};
const char *preset_name(int i) { return PRESET[(unsigned)i < NPRESET ? i : 0].name; }
static const int16_t *pv(void) { return PRESET[proj.preset < NPRESET ? proj.preset : 0].v; }
static int pctv(int v, int p) { return (v * p + 50) / 100; }
int preset_par(int p, int v)
{
    const int16_t *r = pv();
    const en_param_t *pi = en_param_info(p);
    switch (p) {
    case P_PAD: v = pctv(v, r[R_VO]); break;
    case P_BASS: v = pctv(v, r[R_DR]); break;
    case P_BELLS: v = pctv(v, r[R_BL]); break;
    case P_BTONE: v = r[R_BT] >= 0 ? r[R_BT] : v; break;
    case P_REVERB: v += r[R_RV]; break;
    case P_SIZE: v += r[R_SZ]; break;
    case P_ECHO: v += r[R_EC]; break;
    case P_BRIGHT: v += r[R_CO]; break;
    case P_PLUCK: v = r[R_STR]; break;
    case P_PLDENS: v = r[R_SDEN]; break;
    case P_PLTONE: v = r[R_STONE]; break;
    case P_PLOCT: v = r[R_SOCT]; break;
    case P_LEAD: v = r[R_LEAD]; break;
    case P_LDTONE: v = r[R_LTONE]; break;
    case P_LDDENS: v = r[R_LOFT]; break;
    case P_LDOCT: v = r[R_LOCT]; break;
    case P_TEXTURE: v = r[R_TEX]; break;
    case P_TEXLVL:                       /* the Ambience knob: 0 .. 120 % of the preset's air */
        v = r[R_AIR] * proj.amb[proj.preset < NPRESET ? proj.preset : 0] * 5 / 100;
        break;
    case P_TEXMOD: v = r[R_SWAY]; break;
    case P_SHIMMER: v = r[R_SHIM]; break;
    default: break;
    }
    return v < pi->lo ? pi->lo : v > pi->hi ? pi->hi : v;
}
int preset_has_air(void) { return pv()[R_AIR] > 0; }
const char *preset_air_name(void)
{
    static const char *const N[4] = {"rain", "wind", "sea", "candles"};
    return N[pv()[R_TEX] & 3];
}
int preset_final(int final) { return final + pv()[R_TR]; }
int preset_mode(int mode) { return pv()[R_MODE] >= 0 ? pv()[R_MODE] : mode; }
int preset_pace(int pct) { return pct * pv()[R_PC] / 100; }
int preset_formant(int f)
{
    f = pctv(f, pv()[R_FM]);
    return f < 70 ? 70 : f > 130 ? 130 : f;
}
int preset_vib(int v) { return v + pv()[R_VB] > 100 ? 100 : v + pv()[R_VB]; }

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, preset_par(i, i < P_NSAVED ? proj.par[i] : 0));
    en_pool(EN_MODE_POOL[MODE_DORIAN]);
    world_key();
}
