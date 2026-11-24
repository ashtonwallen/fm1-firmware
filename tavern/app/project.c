/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Tavern: ashtonwallen, 2026 */
/* TAVERN's project: one flash object (OBJ_PROJ): the settings and the whole tavern (the regulars, their moods and
 * quarrels, tonight's clock, the memorable nights). A project of another format or another firmware, or none,
 * gives the defaults: a fresh tavern on its first night. Format 1 (1.0) has the same layout (its spare byte, always
 * 0, is now the preset): it is carried over whole, on preset 0, The Corner, 1.0's session. Part of the unity build,
 * after app.h. */
project_t proj;

static int player_par(int p) { return p < P_TEMPO; }   /* the tune's own (tempo, root, mode) are the world's */

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < P_NSAVED; i++)
        proj.par[i] = en_param_info(i)->def;
    proj.leds = 1;
    proj.night_len = 3;                          /* an hour a night */
    world_bind(&proj.world);
    world_defaults();
}

static int project_valid(void)
{
    int i;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT || proj.leds > 2 || proj.night_len > 5 ||
        proj.preset >= NPRESET)
        return 0;
    for (i = 0; i < P_NSAVED; i++) {
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
    if (n == (int)sizeof proj && proj.magic == PROJ_MAGIC && proj.format == 1u) {   /* 1.0's: on the first preset */
        proj.format = PROJ_FORMAT;
        proj.preset = 0;
    }
    if (n == (int)sizeof proj && project_valid())
        return 0;
    project_defaults();
    return -1;
}

int project_save(void) { return plat_store_save(OBJ_PROJ, &proj, sizeof proj); }

/* the presets (1.1): the session's style. Each leans the kinds of tune (reels, jigs, waltzes, airs), the folk
 * modes, the tempo and how often someone sings; brings instruments forward or back; moves the room (offsets over the
 * knobs); and puts AMB-1 Drift 1.1's air outside (rain, wind, sea, the fire's crackle) and its shimmer. The Corner,
 * the first, changes nothing: 1.0's session. The tavern (who is in, their moods, the nights) is never touched; the
 * regulars keep their own instruments.
 *   Re Ji Wa Ai the kinds' weights %; Io Do Mx Ae the modes, in tenths (1.0: 3 3 2 2); Tm tempo %; Sg songs %;
 *   Fi Wh Vo Hp Lu Bx Bs Dr the instruments %; Ec Rv Rm To echo, reverb, room, tone added; Tx Air Sw air (0 rain,
 *   1 wind, 2 sea, 3 fire); Sh shimmer */
enum { R_RE, R_JI, R_WA, R_AI, R_IO, R_DO, R_MX, R_AE, R_TM, R_SG, R_FI, R_WH, R_VO, R_HP, R_LU, R_BX, R_BS, R_DR, R_EC,
       R_RV, R_RM, R_TO, R_TX, R_AIR, R_SW, R_SH, R_N };
static const struct {
    const char *name;
    int16_t v[R_N];
} PRESET[NPRESET] = {
    /*                    Re   Ji   Wa   Ai  Io Do Mx Ae  Tm  Sg   Fi   Wh   Vo   Hp   Lu   Bx   Bs   Dr  Ec  Rv  Rm  To Tx Air Sw Sh */
    {"The Corner",      {100, 100, 100, 100, 3, 3, 2, 2, 100, 20, 100, 100, 100, 100, 100, 100, 100, 100,  0,  0,  0,  0, 0,  0, 50,  0}},
    {"Highland Pub",    {150, 120,  60,  70, 3, 1, 5, 1, 104, 15, 100, 120, 100,  70,  90, 100, 100, 115,  0, -5,-10,  5, 0, 30, 60,  0}},
    {"Breton Fest-noz", {170,  40,  30,  40, 2, 3, 1, 4, 100, 55,  90, 110, 125,  80,  90, 120, 100,  75, 10,  0,  5,  0, 3, 20, 40,  0}},
    {"Nordic Hall",     { 50,  40, 180, 120, 2, 4, 0, 4,  92, 15, 120,  80,  90, 110, 100,  80, 100,  50,  5, 15, 25, -5, 1, 35, 70, 20}},
    {"Sea Shanty Inn",  { 80, 120, 120,  80, 5, 1, 2, 2,  95, 65,  90,  90, 130,  80,  90, 120, 110, 110,  0,  0, -5,  0, 2, 50, 70,  0}},
    {"Quiet Corner",    { 30,  30, 150, 200, 3, 3, 1, 3,  88, 20, 100, 100, 100, 120, 100,  90,  80,  40,  0, -5,-10,-10, 3, 30, 30,  0}},
    {"Harvest Night",   {130, 130,  90,  40, 4, 2, 3, 1, 106, 25, 110, 100, 100,  90, 100, 110, 110, 120,  0,  0,  0,  5, 3, 35, 50,  0}},
    {"Rainy Night",     { 70,  80, 130, 150, 2, 3, 1, 4,  94, 25, 100, 100, 100, 110, 100, 100, 100,  70,  5,  5,  0, -5, 0, 55, 60,  0}},
    {"Midwinter",       { 60,  50, 150, 150, 1, 3, 1, 5,  90, 20, 110,  80,  90, 120, 100,  90, 100,  50,  5, 10, 15,  5, 1, 45, 80, 35}},
    {"Wedding Party",   {140, 150,  80,  20, 6, 1, 3, 0, 110, 40, 110, 110, 110,  90, 100, 115, 110, 120,  0,  0,  0, 10, 3, 20, 40,  0}},
    {"Stone Hall",      { 80,  80, 120, 140, 2, 3, 2, 3,  92, 20, 100, 100, 100, 115, 100, 100,  90,  70, 10, 20, 35,  0, 1, 20, 60, 25}},
    {"Lock-in",         { 50,  60, 140, 170, 2, 3, 1, 4,  86, 45, 100,  80, 110, 110, 110, 100,  90,  50,  0,  0, -5,-15, 3, 45, 40,  0}},
};
const char *preset_name(int i) { return PRESET[(unsigned)i < NPRESET ? i : 0].name; }
static const int16_t *pv(void) { return PRESET[proj.preset < NPRESET ? proj.preset : 0].v; }
int preset_par(int p, int v)
{
    const int16_t *r = pv();
    const en_param_t *pi = en_param_info(p);
    switch (p) {
    case P_ECHO: v += r[R_EC]; break;
    case P_REVERB: v += r[R_RV]; break;
    case P_ROOM: v += r[R_RM]; break;
    case P_TONE: v += r[R_TO]; break;
    case P_TEXTURE: v = r[R_TX]; break;
    case P_TEXLVL: v = r[R_AIR]; break;
    case P_TEXMOD: v = r[R_SW]; break;
    case P_SHIMMER: v = r[R_SH]; break;
    default: break;
    }
    return v < pi->lo ? pi->lo : v > pi->hi ? pi->hi : v;
}
void preset_kinds(int *w)
{
    int k;
    for (k = 0; k < 4; k++)
        w[k] = w[k] * pv()[R_RE + k] / 100;
}
int preset_kind_pct(int kind) { return pv()[R_RE + (kind & 3)]; }
int preset_mode(uint32_t r10)
{
    static const uint8_t M[4] = {MODE_IONIAN, MODE_DORIAN, MODE_MIXOLYDIAN, MODE_AEOLIAN};
    int k, r = (int)(r10 % 10u), sum = 0;
    for (k = 0; k < 4; k++)
        sum += pv()[R_IO + k];
    r = sum == 10 ? r : r * sum / 10;
    for (k = 0; k < 4; k++) {
        r -= pv()[R_IO + k];
        if (r < 0)
            return M[k];
    }
    return MODE_IONIAN;
}
int preset_tempo(int t) { return t * pv()[R_TM] / 100; }
int preset_songs(void) { return pv()[R_SG]; }
int preset_inst(int inst, int level)
{
    level = level * pv()[R_FI + inst] / 100;
    return level > 100 ? 100 : level;
}

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        if (player_par(i) || i >= P_NSAVED)
            en_set(i, preset_par(i, i < P_NSAVED ? proj.par[i] : 0));
}
