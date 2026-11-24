/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen (GARDEN) */
/* GARDEN: the world. Twelve plots in two rows. A key plants a melody seed at its pitch; over minutes it sprouts (a
 * soft note now and then), grows into a phrase (a short loop from its pitch in the garden's scale, a note more as it
 * grows), flowers (the whole phrase, with bells above it), goes to seed (new seeds on neighbouring plots, related
 * notes), withers (fewer notes, slower) and leaves compost (a low drone that fades). A gardener tends it: waters,
 * prunes, plants favourites, turns the compost. Bees cross two flowers into a hybrid; a rabbit nibbles sprouts; rain
 * makes everything grow fast; frost withers the weak. With nothing growing, seeds blow in over the wall.
 *
 * Time: the world moves a second at a time while the garden plays (resting or frozen, it waits). Plants age in
 * quarter growth-seconds: Growth (Slow .. Quick) and the weather set how many a second. A day is G_DAY_S seconds.
 * The music: each plot is one of the engine's plots (amb.h en_plot), set whenever what it plays changes; the
 * newest compost feeds the two drones; how full the garden is sets the pad, the brightness, the bells' tone and the
 * chord's pace (music_apply). Part of the unity build, after project.c. */

#define G_DAY_S 900u                     /* a day: 15 minutes of growing */
#define PLOT_X(i) (20 + ((i) % 6) * 40)

static const char *const VAR_NAME[16] = {"Rose", "Bell", "Lily", "Fern", "Poppy", "Thistle", "Clover", "Iris",
                                         "Sage", "Foxglove", "Aster", "Mallow", "Tansy", "Violet", "Yarrow", "Sorrel"};
static const char *const ADJ_NAME[16] = {"Lantern", "Moss", "Dusk", "Copper", "Silver", "Hollow", "Amber", "Mist",
                                         "River", "Velvet", "Paper", "Ember", "Wren", "Honey", "Salt", "Moon"};
static const char *const GARDENER[8] = {"Maud", "Odile", "Bertram", "Wren", "Ines", "Tobias", "Agnes", "Hollis"};
const char *const STAGE_NAME[ST_N] = {"empty", "seed", "sprout", "growing", "in flower", "seeding", "withering",
                                      "compost"};
const char *const WEATHER_NAME[W_N] = {"Sun", "Cloud", "Rain", "Frost"};
static const char *const BED_NAME[4] = {"by the well", "by the wall", "by the gate", "in the sun bed"};
/* each stage's length, quarter growth-seconds (a third either way) */
static const uint16_t STAGE_LIFE[ST_N] = {0, 160, 600, 1200, 960, 360, 480, 800};

uint8_t garden_changed;
uint32_t garden_log_seq;

/* what is not saved: the walkers, the timers, the music's last state */
static struct {
    float gx, tx;                        /* the gardener's x and where they are going */
    uint8_t task, task_plot;             /* what they are doing (TK_*) and where */
    uint32_t task_t;                     /* ms left showing it */
    int16_t act_in;                      /* seconds to the next job */
    uint8_t bee, bee_a, bee_b;           /* a bee on its way from a to b */
    uint32_t bee_t;                      /* ms into its flight */
    uint8_t rabbit, rabbit_plot;
    uint32_t rabbit_t;
    uint32_t ms_acc;                     /* ms toward the next second */
    float qacc;                          /* growth quarters toward the next whole one */
    uint16_t sync;                       /* plots whose music must be sent */
    uint8_t shown[G_NPLOT];              /* the notes each plot plays now (0: silent) */
    int8_t drone_plot[2];
    uint8_t drone_lvl[2];
    int16_t last[P_NPARAMS];             /* the values last sent for the parameters the garden bends */
    uint8_t wild_in;                     /* seconds to the next wild seed while the garden is empty */
} gw;
enum { TK_IDLE, TK_WATER, TK_PRUNE, TK_PLANT, TK_SHOO, TK_ADMIRE, TK_COMPOST };

#define W (proj.w)

static uint32_t grnd(void)
{
    uint32_t x = W.rng ? W.rng : 0x2545F491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    W.rng = x;
    return x;
}
static int girand(int n) { return n > 0 ? (int)((grnd() >> 8) % (uint32_t)n) : 0; }
static int chance(int per_mille) { return girand(1000) < per_mille; }

/* ------------------------------------------------------------- names, words --- */
const char *plant_name(const plant_t *p, char *b)
{
    const char *a = ADJ_NAME[p->adj & 15], *v = VAR_NAME[p->var & 15];
    char *o = b;
    while (*a)
        *o++ = *a++;
    *o++ = ' ';
    while (*v)
        *o++ = *v++;
    *o = 0;
    return b;
}
static const char *pair_name(uint8_t adj, uint8_t var, char *b)
{
    plant_t t;
    t.adj = adj;
    t.var = var;
    return plant_name(&t, b);
}
const char *gardener_name(void) { return GARDENER[W.g.name & 7]; }
const char *mood_word(void)
{
    int m = W.g.mood;
    return m < 20 ? "gloomy" : m < 40 ? "weary" : m < 60 ? "content" : m < 80 ? "cheerful" : "delighted";
}
static void mood(int d)
{
    int m = (int)W.g.mood + d;
    W.g.mood = (uint8_t)(m < 0 ? 0 : m > 100 ? 100 : m);
}

/* an event line: built in pieces, then pushed on top of the log */
static char ev[G_LOGLEN];
static int evn;
static void ev_s(const char *s)
{
    while (*s && evn < G_LOGLEN - 1)
        ev[evn++] = *s++;
    ev[evn] = 0;
}
static void ev_p(const plant_t *p)
{
    char b[24];
    ev_s(plant_name(p, b));
}
static void ev_u(uint32_t v)
{
    char t[12];
    int n = 0;
    do
        t[n++] = (char)('0' + v % 10u);
    while ((v /= 10u) && n < 11);
    while (n) {
        char c[2] = {t[--n], 0};
        ev_s(c);
    }
}
static void ev_push(void)
{
    int i;
    for (i = G_NLOG - 1; i > 0; i--)
        memcpy(W.log[i], W.log[i - 1], G_LOGLEN);
    memcpy(W.log[0], ev, G_LOGLEN);
    evn = 0;
    ev[0] = 0;
    garden_log_seq++;
}

/* ------------------------------------------------------------- presets --- */
/* PRESETS (1.1): a palette for the garden, over the player's knobs and the garden's own bending. The key moved, the
 * mode a step brighter or darker (or pentatonic; a custom scale stays as it is), the tempo scaled, AMB-1 Drift 1.1's
 * strings, lead, air and shimmer, and the space. The pad's colour stays the player's (Sound page: Colour, which now
 * has Strings too). Only the sound changes: every plant, its pitch and phrase (steps of the scale), the gardener,
 * the day and the ledger stay as they are. Cottage (0) is the garden as 1.0 played it. Saved with the settings. */
#define PR_PENT 9                        /* mode: the pentatonic of the player's mode (major or minor) */
typedef struct {
    const char *name;
    int8_t key;                          /* semitones the key moves (from the player's Root) */
    int8_t mode;                         /* +1 a step brighter, -1 darker, PR_PENT pentatonic, 0 the player's */
    uint8_t tempo;                       /* percent of the player's tempo */
    int8_t tex;                          /* the air's texture (0 rain, 1 wind, 2 sea, 3 vinyl); its level is in d */
    int8_t d[P_NPARAMS];                 /* added to the values */
} preset_t;
static const preset_t PRESETS[NPRESETS] = {
    {"Cottage", 0, 0, 100, 0, {0}},
    {"Zen", 0, PR_PENT, 80, 1, {[P_LEAD] = 25, [P_LDTONE] = 40, [P_SHIMMER] = 25, [P_REVERB] = 15, [P_BELLS] = -5,
                                [P_BTONE] = 15, [P_TEXLVL] = 12, [P_TEXMOD] = -20}},
    {"Moonlight", -2, -1, 80, -1, {[P_SHIMMER] = 35, [P_LEAD] = 30, [P_LDTONE] = 45, [P_LDOCT] = 1, [P_BRIGHT] = -15,
                                   [P_REVERB] = 10, [P_PAD] = 10}},
    {"Greenhouse", 2, 1, 105, 0, {[P_TEXLVL] = 15, [P_PLUCK] = 35, [P_PLTONE] = 50, [P_BRIGHT] = 10, [P_ECHO] = 10}},
    {"Wildflower", 0, 1, 110, -1, {[P_PLUCK] = 45, [P_PLTONE] = -10, [P_PLDENS] = 10, [P_BRIGHT] = 10, [P_BELLS] = 5}},
    {"Herb", 5, 0, 95, 1, {[P_PLUCK] = 25, [P_PLTONE] = -10, [P_TEXLVL] = 15, [P_ECHO] = 5}},
    {"Rain Garden", 0, 0, 95, 0, {[P_TEXLVL] = 45, [P_TEXMOD] = 10, [P_PLUCK] = 20, [P_BRIGHT] = -10, [P_ECHO] = 15}},
    {"Orchard Wall", 0, 0, 90, -1, {[P_PLUCK] = 35, [P_PLTONE] = -15, [P_LEAD] = 25, [P_LDTONE] = -35, [P_LDOCT] = -1,
                                    [P_BASS] = 10}},
    {"Winter Bed", -2, -1, 75, 1, {[P_TEXLVL] = 30, [P_TEXMOD] = 30, [P_SHIMMER] = 30, [P_BTONE] = 25,
                                   [P_BRIGHT] = -15, [P_REVERB] = 10}},
    {"Kitchen Garden", 3, 0, 95, 3, {[P_TEXLVL] = 25, [P_ECHO] = 10, [P_BASS] = 5, [P_PLUCK] = 25, [P_PLTONE] = 45}},
    {"Rose Arbour", 0, 1, 90, -1, {[P_LEAD] = 30, [P_LDTONE] = -30, [P_SHIMMER] = 15, [P_REVERB] = 10,
                                   [P_SWELL] = 10}},
    {"Fern Hollow", -3, -1, 90, 0, {[P_TEXLVL] = 20, [P_TEXMOD] = 20, [P_LEAD] = 30, [P_LDTONE] = 40, [P_LDOCT] = -1,
                                    [P_BRIGHT] = -10, [P_REVERB] = 10}},
    {"Sundial", 0, PR_PENT, 115, -1, {[P_PLUCK] = 45, [P_PLTONE] = 50, [P_BRIGHT] = 15, [P_REVERB] = -10,
                                      [P_ECHO] = -5}},
};
static const preset_t *preset(void) { return &PRESETS[proj.preset < NPRESETS ? proj.preset : 0]; }
static const char *preset_name(int i) { return PRESETS[(unsigned)i < NPRESETS ? i : 0].name; }

static int mode_shift(int m, int s)      /* the modes, brightest first: Lydian, Ionian, Mixolydian, Dorian, Aeolian, Phrygian */
{
    static const uint8_t ORDER[6] = {MODE_LYDIAN, MODE_IONIAN, MODE_MIXOLYDIAN, MODE_DORIAN, MODE_AEOLIAN, MODE_PHRYGIAN};
    int r;
    if (!s)
        return m;
    if (m == MODE_PENT_MAJ)
        m = MODE_IONIAN;
    else if (m == MODE_PENT_MIN)
        m = MODE_AEOLIAN;
    for (r = 0; r < 6 && ORDER[r] != m; r++)
        ;
    if (s == PR_PENT)
        return r <= 2 ? MODE_PENT_MAJ : MODE_PENT_MIN;
    r -= s;
    return ORDER[r < 0 ? 0 : r > 5 ? 5 : r];
}
static int g_root(void) { return ((proj.par[P_ROOT] + preset()->key) % 12 + 12) % 12; }   /* the key heard */
static int g_custom(void) { return proj.pool != EN_MODE_POOL[proj.par[P_MODE] % EN_NMODES]; }
static int g_mode(void) { return g_custom() ? proj.par[P_MODE] : mode_shift(proj.par[P_MODE], preset()->mode); }
static uint32_t g_pool(void) { return g_custom() ? proj.pool : EN_MODE_POOL[g_mode()]; }   /* the scale heard */

/* a value as the engine gets it: the player's (or, for 1.1's sounds, the engine's default), the preset's on it */
static int pv(int p)
{
    const preset_t *pr = preset();
    const en_param_t *pi = en_param_info(p);
    int v = p < P_NSAVED ? proj.par[p] : pi->def;
    if (p == P_ROOT)
        return g_root();
    if (p == P_MODE)
        return g_mode();
    if (p == P_TEXTURE)
        return pr->tex >= 0 ? pr->tex : v;
    if (p == P_TEMPO)
        v = v * pr->tempo / 100;
    v += pr->d[p];
    return v < pi->lo ? pi->lo : v > pi->hi ? pi->hi : v;
}

/* ------------------------------------------------------------- the scale --- */
static uint8_t sc[80];                   /* every note of the scale, MIDI 24..119, ascending */
static int scn;
static void scale_build(void)
{
    uint32_t r = (uint32_t)g_root(), p = g_pool(), ab = ((p << r) | (p >> (12u - r))) & 0xFFFu;
    int n;
    scn = 0;
    for (n = 24; n < 120 && scn < 80; n++)
        if (ab >> (n % 12) & 1u)
            sc[scn++] = (uint8_t)n;
}
static int sc_index(int pitch)           /* the scale's note at or below pitch */
{
    int k = 0;
    while (k + 1 < scn && sc[k + 1] <= pitch)
        k++;
    return k;
}
static int snap(int pitch)               /* the nearest note of the scale (down when between) */
{
    int k = sc_index(pitch);
    if (k + 1 < scn && sc[k + 1] - pitch < pitch - sc[k])
        k++;
    return sc[k];
}
static int sc_note(int pitch, int off)   /* off steps of the scale from pitch */
{
    int k = sc_index(pitch) + off;
    while (k < 0)
        k += 7;
    while (k >= scn)
        k -= 7;
    return k < 0 ? sc[0] : sc[k];
}

/* ------------------------------------------------------------- plants --- */
static int prow(int i) { return i / 6; }
static int pcol(int i) { return i % 6; }
static int pdist(int a, int b)
{
    int dc = pcol(a) - pcol(b);
    return (dc < 0 ? -dc : dc) + (prow(a) != prow(b));
}
static const char *bed_of(int i) { return BED_NAME[prow(i) * 2 + (pcol(i) >= 3)]; }
static int living(const plant_t *p) { return p->stage >= ST_SEED && p->stage <= ST_WITHER; }
int garden_living(void)
{
    int i, n = 0;
    for (i = 0; i < G_NPLOT; i++)
        n += living(&W.p[i]);
    return n;
}
int garden_count(int st)
{
    int i, n = 0;
    for (i = 0; i < G_NPLOT; i++)
        n += W.p[i].stage == st;
    return n;
}

static void stage_set(int i, int st)
{
    plant_t *p = &W.p[i];
    int l = STAGE_LIFE[st];
    p->stage = (uint8_t)st;
    p->age = 0;
    p->life = (uint16_t)(l * 2 / 3 + girand(l * 2 / 3 + 1));
    gw.sync |= (uint16_t)(1u << i);
}

static void phrase_new(plant_t *p)       /* a motif from its pitch: a short walk through the scale, a few rests */
{
    int k, at = 0;
    p->plen = (uint8_t)(4 + girand(5));
    p->rate = (uint8_t)(1 + (p->var % 3 == 0) + (girand(4) == 0));
    for (k = 0; k < EN_PH_MAX; k++) {
        if (k && girand(100) < 28) {
            p->ph[k] = PH_REST;
            continue;
        }
        p->ph[k] = (int8_t)at;
        at += (int)"\xFE\xFF\x01\x02\x00\x01\xFF\x03"[girand(8)];   /* -2 -1 +1 +2 0 +1 -1 +3 */
        at = at < -4 ? -4 : at > 7 ? 7 : at;
    }
    p->ph[0] = 0;
}

static void plant_at(int i, int pitch, int var, int adj, uint8_t flags)
{
    plant_t *p = &W.p[i];
    int k, tries;
    memset(p, 0, sizeof *p);
    while (pitch > 79)                   /* in the keys' range: families never drift off the top or the bottom */
        pitch -= 12;
    while (pitch < 53)
        pitch += 12;
    p->pitch = (uint8_t)snap(pitch);
    p->var = (uint8_t)(var & 15);
    p->adj = (uint8_t)(adj & 15);
    for (tries = 0; tries < 16; tries++) {   /* a name of its own: no two plants in the garden share one */
        for (k = 0; k < G_NPLOT; k++)
            if (k != i && W.p[k].stage != ST_EMPTY && W.p[k].adj == p->adj && W.p[k].var == p->var)
                break;
        if (k == G_NPLOT)
            break;
        p->adj = (uint8_t)((p->adj + 5) & 15);
    }
    p->pa_var = p->pb_var = NO_PARENT;
    p->health = 85;
    p->water = 70;
    p->flags = flags;
    phrase_new(p);
    stage_set(i, ST_SEED);
    if (flags & F_YOURS)
        p->life = 40;                    /* a seed of yours sprouts within seconds */
    W.born++;
}

static int empty_near(int to, int maxd)  /* an empty plot (or old compost) near plot to; -1 none */
{
    int i, best = -1, bd = 99;
    for (i = 0; i < G_NPLOT; i++) {
        int d = pdist(i, to) * 4 + girand(3);
        if (W.p[i].stage == ST_EMPTY && d < bd && pdist(i, to) <= maxd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

/* ------------------------------------------------------------- the music --- */
static int fullness100(void)              /* 0..100: how much is growing */
{
    static const uint8_t WT[ST_N] = {0, 0, 3, 7, 10, 7, 3, 0};
    int i, s = 0;
    for (i = 0; i < G_NPLOT; i++)
        s += WT[W.p[i].stage];
    s = s * 100 / 60;
    return s > 100 ? 100 : s;
}

static void plot_send(int i)
{
    plant_t *p = &W.p[i];
    int len = 0, rate = p->rate, vel = 0, shimmer = 0, k, show = 0;
    int pan = pcol(i) * 7 / 5, timbre = p->var & 3;
    uint8_t n[EN_PH_MAX];
    memset(n, 0, sizeof n);
    switch (p->stage) {
    case ST_SEED:                        /* yours: a soft note to say it is in the ground */
        if (p->flags & F_YOURS) {
            len = 8, rate = 3, vel = 6;
            n[0] = (uint8_t)(p->pitch + 12);
        }
        break;
    case ST_SPROUT:                      /* a single soft note now and then */
        len = 8, rate = 2, vel = 5;
        n[0] = (uint8_t)(p->pitch + 12);
        break;
    case ST_GROW:
        show = 1 + (p->plen - 1) * p->age / (p->life ? p->life : 1);
        show = show < 2 ? 2 : show > p->plen ? p->plen : show;
        len = p->plen, vel = 8;
        for (k = 0; k < show; k++)
            if (p->ph[k] != PH_REST)
                n[k] = (uint8_t)(sc_note(p->pitch, p->ph[k]) + 12);
        break;
    case ST_FLOWER:
        len = p->plen, vel = 11, shimmer = 1;
        for (k = 0; k < len; k++)
            if (p->ph[k] != PH_REST)
                n[k] = (uint8_t)(sc_note(p->pitch, p->ph[k]) + 12);
        break;
    case ST_SEEDING:                     /* scattering: every other note */
        len = p->plen, vel = 8;
        for (k = 0; k < len; k += 2)
            if (p->ph[k] != PH_REST)
                n[k] = (uint8_t)(sc_note(p->pitch, p->ph[k]) + 12);
        break;
    case ST_WITHER:                      /* fading: its first notes, slower, softer */
        len = p->plen, vel = 5, rate = rate + 1 > 3 ? 3 : rate + 1;
        for (k = 0; k < 2; k++)
            if (p->ph[k] != PH_REST)
                n[k] = (uint8_t)(sc_note(p->pitch, p->ph[k]) + 12);
        break;
    default: break;
    }
    if (p->stage >= ST_SPROUT && p->stage <= ST_WITHER && p->health < 40 && vel > 3)
        vel -= 2;                        /* a poorly plant plays quietly */
    for (k = 0; k < EN_PH_MAX; k++)
        en_plot_note(i, k, n[k]);
    en_plot(i, len, rate, vel, timbre, shimmer, pan);
    for (k = 0, show = 0; k < len; k++)
        show += n[k] != 0;
    gw.shown[i] = (uint8_t)show;
}

static void drones(void)                 /* the two freshest heaps of compost hum */
{
    int v, i, used = -1;
    for (v = 0; v < 2; v++) {
        int best = -1, lvl = 0, note = 0;
        uint32_t fresh = 0;
        for (i = 0; i < G_NPLOT; i++) {
            const plant_t *p = &W.p[i];
            uint32_t f;
            if (p->stage != ST_COMPOST || i == used || p->age >= p->life)
                continue;
            f = (uint32_t)(p->life - p->age) * 1000u / p->life;
            if (f > fresh) {
                fresh = f;
                best = i;
            }
        }
        if (best >= 0) {
            used = best;
            lvl = (int)(fresh * 9u / 1000u) + 1;
            note = 36 + (W.p[best].pitch - 36) % 12;
            if (note < 43)
                note += 12;
        }
        if (best != gw.drone_plot[v] || lvl != gw.drone_lvl[v]) {
            gw.drone_plot[v] = (int8_t)best;
            gw.drone_lvl[v] = (uint8_t)lvl;
            if (best >= 0 || lvl == 0)
                en_drone(v, note ? note : 43, lvl);
        }
    }
}

static int night(void) { return W.daysec >= G_DAY_S * 3u / 4u; }

static void bend(int p, int v)           /* a parameter the garden bends: sent when it changes */
{
    const en_param_t *pi = en_param_info(p);
    v = v < pi->lo ? pi->lo : v > pi->hi ? pi->hi : v;
    if (gw.last[p] != v) {
        gw.last[p] = (int16_t)v;
        en_set(p, v);
    }
}

static void music_apply(void)            /* how full the garden is, the weather and the hour: the sound */
{
    int f = fullness100(), wx = W.weather, nt = night();
    bend(P_PAD, pv(P_PAD) * (100 - f * 3 / 10) / 100);
    bend(P_BRIGHT, pv(P_BRIGHT) - 20 + f * 45 / 100 - (nt ? 12 : 0) - (wx == W_FROST ? 15 : 0) +
                       (wx == W_SUN ? 6 : 0));
    bend(P_BTONE, pv(P_BTONE) - 15 + f * 25 / 100 + (wx == W_FROST ? 30 : 0));
    bend(P_REVERB, pv(P_REVERB) + (100 - f) * 15 / 100 + (wx == W_FROST ? 10 : 0));
    bend(P_DRIFT, pv(P_DRIFT) + (100 - f) * 25 / 100);
    bend(P_PULSE, wx == W_RAIN ? 24 + pv(P_PULSE) : pv(P_PULSE));
    bend(P_HITS, wx == W_RAIN ? 75 : pv(P_HITS));
    bend(P_HATS, wx == W_RAIN ? 90 : pv(P_HATS));
    bend(P_SWELL, pv(P_SWELL) + (nt ? 15 : 0));
    bend(P_DENSITY, 0);
}

void garden_sound_all(void)              /* every setting to the engine as the preset colours it, and the scale */
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, pv(i));
    en_pool(g_pool());
    garden_bend_again();
}

void garden_preset(int i)                /* another preset: the sound changes, the garden stays */
{
    proj.preset = (uint8_t)((i % NPRESETS + NPRESETS) % NPRESETS);
    garden_sound_all();
    garden_resync();                     /* (the scale may have moved: every plot's notes again) */
}

void garden_resync(void)
{
    int i;
    scale_build();
    for (i = 0; i < P_NPARAMS; i++)
        gw.last[i] = (int16_t)-999;
    gw.sync = (uint16_t)((1u << G_NPLOT) - 1u);
    gw.drone_plot[0] = gw.drone_plot[1] = -2;
    if (gw.gx < 1.0f && gw.tx < 1.0f) {   /* after a boot: the gardener in the middle, a job soon */
        gw.gx = gw.tx = 120.0f;
        gw.act_in = 12;
    }
}

/* ------------------------------------------------------------- the gardener --- */
static void walk_to(int i, int task)
{
    gw.tx = (float)PLOT_X(i);
    gw.task = (uint8_t)task;
    gw.task_plot = (uint8_t)i;
    gw.task_t = 6000u;
}

static int pick(int (*ok)(const plant_t *))   /* a plot whose plant passes ok, at random; -1 none */
{
    int i, n = 0, c[G_NPLOT];
    for (i = 0; i < G_NPLOT; i++)
        if (ok(&W.p[i]))
            c[n++] = i;
    return n ? c[girand(n)] : -1;
}
static int is_flower(const plant_t *p) { return p->stage == ST_FLOWER; }
static int is_sprout(const plant_t *p) { return p->stage == ST_SPROUT; }
static int is_old_compost(const plant_t *p) { return p->stage == ST_COMPOST && p->age > p->life / 2; }
static int is_prunable(const plant_t *p)
{
    return (p->stage == ST_GROW || p->stage == ST_FLOWER) && p->plen > 5;
}

static void gardener_act(void)
{
    int i, driest = -1, dw = 101, live = garden_living();
    char b[24];
    for (i = 0; i < G_NPLOT; i++)
        if (living(&W.p[i]) && W.p[i].water < dw) {
            dw = W.p[i].water;
            driest = i;
        }
    ev_s(gardener_name());
    if (driest >= 0 && dw < 40 && W.weather != W_RAIN) {
        plant_t *p = &W.p[driest];
        p->water = 100;
        if (p->health < 95)
            p->health = (uint8_t)(p->health + 5);
        walk_to(driest, TK_WATER);
        ev_s(" watered ");
        ev_p(p);
        mood(2);
    } else if (live >= 8 && (i = pick(is_prunable)) >= 0) {
        plant_t *p = &W.p[i];
        int k = p->plen - 1;
        p->plen--;                       /* the phrase loses its last note */
        p->ph[k] = PH_REST;
        gw.sync |= (uint16_t)(1u << i);
        walk_to(i, TK_PRUNE);
        ev_s(" pruned ");
        ev_p(p);
        mood(1);
    } else if (live < 5 && W.g.fav_has && (i = empty_near(girand(G_NPLOT), 6)) >= 0) {
        plant_at(i, W.g.fav_pitch, W.g.fav_var, W.g.fav_adj, F_KEPT);
        W.g.planted++;
        walk_to(i, TK_PLANT);
        ev_s(" planted a ");
        ev_s(pair_name(W.g.fav_adj, W.g.fav_var, b));
        garden_changed = 1;
        mood(4);
    } else if ((i = pick(is_old_compost)) >= 0) {
        W.p[i].stage = ST_EMPTY;
        gw.sync |= (uint16_t)(1u << i);
        walk_to(i, TK_COMPOST);
        ev_s(" turned the compost ");
        ev_s(bed_of(i));
        garden_changed = 1;
    } else if ((i = pick(is_flower)) >= 0) {
        plant_t *p = &W.p[i];
        int k;
        walk_to(i, TK_ADMIRE);
        if (!W.g.fav_has || chance(300)) {   /* a new favourite */
            for (k = 0; k < G_NPLOT; k++)
                W.p[k].flags &= (uint8_t)~F_FAV;
            p->flags |= F_FAV;
            W.g.fav_has = 1;
            W.g.fav_adj = p->adj;
            W.g.fav_var = p->var;
            W.g.fav_pitch = p->pitch;
            ev_s(" loves ");
            ev_p(p);
            garden_changed = 1;
        } else {
            ev_s(" admired ");
            ev_p(p);
        }
        mood(3);
    } else {
        gw.tx = (float)(20 + girand(200));
        gw.task = TK_IDLE;
        evn = 0;                         /* a stroll: nothing to say */
        ev[0] = 0;
        mood(W.weather == W.g.likes ? 1 : -1);
        return;
    }
    W.g.tended++;
    ev_push();
}

/* ------------------------------------------------------------- visitors, weather --- */
static void bee_line(const plant_t *pa, const plant_t *pb)
{
    ev_s("A bee crossed ");
    ev_p(pa);
    ev_s(" with ");
    ev_p(pb);
    if (text_w(&FONT_XS, ev) > 228) {   /* too wide for the event line: the short way */
        evn = 0;
        ev_s("Bee: ");
        ev_p(pa);
        ev_s(" x ");
        ev_p(pb);
    }
}

static void hybrid(int a, int b)         /* a bee carried a's pollen to b */
{
    plant_t *pa = &W.p[a], *pb = &W.p[b], *c;
    int i = empty_near(b, 3), k, h;

    bee_line(pa, pb);
    if (i < 0) {
        ev_push();
        return;
    }
    plant_at(i, girand(2) ? pa->pitch : pb->pitch, girand(100) < 30 ? girand(16) : pb->var, pa->adj, F_HYBRID);
    c = &W.p[i];
    h = pa->plen / 2;
    for (k = 0; k < EN_PH_MAX; k++)      /* the first half a's, the rest b's */
        c->ph[k] = k < h ? pa->ph[k] : pb->ph[k];
    c->ph[0] = 0;
    c->plen = (uint8_t)((pa->plen + pb->plen + 1) / 2);
    c->rate = girand(2) ? pa->rate : pb->rate;
    c->pa_adj = pa->adj, c->pa_var = pa->var;
    c->pb_adj = pb->adj, c->pb_var = pb->var;
    c->gen = (uint8_t)((pa->gen > pb->gen ? pa->gen : pb->gen) + 1);

    W.hybrids++;
    garden_changed = 1;
    ev_push();
}

static void go_to_seed(int i)            /* new seeds on the plots around, related notes */
{
    static const int8_t REL[6] = {0, 2, -2, 4, -3, 1};
    plant_t *p = &W.p[i];
    int n = 1 + girand(2), k, made = 0;
    for (k = 0; k < n; k++) {
        int j = empty_near(i, 2), m;
        plant_t *c;
        if (j < 0)
            break;
        plant_at(j, sc_note(p->pitch, REL[girand(6)]), girand(100) < 12 ? girand(16) : p->var, girand(4) ? p->adj : girand(16), 0);
        c = &W.p[j];
        memcpy(c->ph, p->ph, sizeof c->ph);
        c->plen = p->plen;
        c->rate = p->rate;
        m = 1 + girand(EN_PH_MAX - 1);   /* one note different */
        if (m < c->plen)
            c->ph[m] = (int8_t)(c->ph[m] == PH_REST ? 1 : c->ph[m] >= 8 ? 7 : c->ph[m] <= -6 ? -5 : c->ph[m] + (girand(2) ? 1 : -1));
        c->pa_adj = p->adj, c->pa_var = p->var;
        c->gen = (uint8_t)(p->gen + 1);
        made++;
    }
    ev_p(p);
    if (made) {
        ev_s(" went to seed: ");
        ev_u((uint32_t)made);
        ev_s(made == 1 ? " seed" : " seeds");
        garden_changed = 1;
    } else {
        ev_s(" seeded, but no room");
    }
    ev_push();
}

static void weather_set(int w);
static void weather_new(void)
{
    int r = girand(100), w = r < 42 ? W_SUN : r < 70 ? W_CLOUD : r < 92 ? W_RAIN : W_FROST;
    if (w == W_FROST && W.day < 2)
        w = W_CLOUD;                     /* no frost on a new garden's first days */
    weather_set(w);
}
static void weather_set(int w)
{
    int was = W.weather, k;
    W.weather = (uint8_t)w;
    W.wleft = (uint8_t)(3 + girand(6));
    if (W.weather == was)
        return;
    if (W.weather == W.g.likes)
        mood(8);
    if (W.weather == W_RAIN) {
        W.rains++;
        ev_s("Rain. Everything drinks and grows");
        for (k = 0; k < G_NPLOT; k++)
            if (living(&W.p[k]))
                W.p[k].water = 100;
    } else if (W.weather == W_FROST) {
        int killed = 0;
        W.frosts++;
        for (k = 0; k < G_NPLOT; k++) {
            plant_t *p = &W.p[k];
            if (!living(p) || p->stage == ST_WITHER)
                continue;
            if (p->stage == ST_SPROUT || p->health < 50) {
                stage_set(k, ST_WITHER);
                killed++;
            } else {
                p->health = (uint8_t)(p->health > 20 ? p->health - 20 : 0);
            }
        }
        ev_s("Frost");
        if (killed) {
            ev_s(": ");
            ev_u((uint32_t)killed);
            ev_s(killed == 1 ? " plant withered" : " plants withered");
        }
        mood(W.weather == W.g.likes ? 0 : -10);
        garden_changed = 1;
    } else if (W.weather == W_SUN) {
        ev_s("The sun comes out");
    } else {
        ev_s("Clouds roll over");
    }
    ev_push();
}

static void visitors(void)
{
    int a, b;
    if (!gw.bee && W.weather != W_RAIN && W.weather != W_FROST && !night() && garden_count(ST_FLOWER) >= 2 &&
        chance(14)) {
        a = pick(is_flower);
        do
            b = pick(is_flower);
        while (b == a);
        gw.bee = 1;
        gw.bee_a = (uint8_t)a;
        gw.bee_b = (uint8_t)b;
        gw.bee_t = 0;
    }
    if (!gw.rabbit && (a = pick(is_sprout)) >= 0 && chance(night() || W.daysec < G_DAY_S / 8u ? 8 : 3)) {
        gw.rabbit = 1;
        gw.rabbit_plot = (uint8_t)a;
        gw.rabbit_t = 0;
    }
}

static void rabbit_bites(void)
{
    plant_t *p = &W.p[gw.rabbit_plot];
    int near = gw.gx > (float)(PLOT_X(gw.rabbit_plot) - 50) && gw.gx < (float)(PLOT_X(gw.rabbit_plot) + 50);
    if (p->stage != ST_SPROUT && p->stage != ST_GROW)
        return;
    W.nibbles++;
    if (near && chance(600)) {
        ev_s(gardener_name());
        ev_s(" shooed a rabbit off ");
        ev_p(p);
        walk_to(gw.rabbit_plot, TK_SHOO);
        mood(-2);
    } else if (p->health <= 45 || p->flags & F_NIBBLED) {
        ev_s("A rabbit ate ");
        ev_p(p);
        p->stage = ST_EMPTY;
        W.died++;
        gw.sync |= (uint16_t)(1u << gw.rabbit_plot);
        mood(-8);
        garden_changed = 1;
    } else {
        ev_s("A rabbit nibbled ");
        ev_p(p);
        p->flags |= F_NIBBLED;
        p->health = (uint8_t)(p->health - 40);
        mood(-4);
    }
    ev_push();
}

/* ------------------------------------------------------------- a second of growing --- */
static void plant_second(int i, int q)
{
    plant_t *p = &W.p[i];
    int st = p->stage;
    if (st == ST_EMPTY)
        return;
    p->age = (uint16_t)(p->age + q > 60000 ? 60000 : p->age + q);
    if (living(p)) {
        if (W.weather == W_RAIN)
            p->water = 100;
        else if (girand(48) < q)
            p->water = (uint8_t)(p->water ? p->water - 1 : 0);
        if (p->water < 15 && girand(24) < q && p->health)
            p->health--;
        else if (p->water > 60 && p->health < 100 && girand(80) < q)
            p->health++;
        if (!p->health && st != ST_WITHER) {
            stage_set(i, ST_WITHER);
            ev_p(p);
            ev_s(" dried out");
            ev_push();
            return;
        }
    }
    if (st == ST_GROW) {                 /* a note more as it grows */
        int show = 1 + (p->plen - 1) * p->age / (p->life ? p->life : 1);
        int n = 0, k;
        show = show < 2 ? 2 : show > p->plen ? p->plen : show;
        for (k = 0; k < show; k++)
            n += p->ph[k] != PH_REST;
        if (n != gw.shown[i])
            gw.sync |= (uint16_t)(1u << i);
    }
    if (p->age < p->life)
        return;
    switch (st) {
    case ST_SEED: stage_set(i, ST_SPROUT); break;
    case ST_SPROUT: stage_set(i, ST_GROW); break;
    case ST_GROW:
        stage_set(i, ST_FLOWER);
        if (p->flags & (F_YOURS | F_HYBRID) || chance(250)) {
            ev_p(p);
            ev_s(" is in flower");
            ev_push();
        }
        break;
    case ST_FLOWER:
        stage_set(i, ST_SEEDING);
        go_to_seed(i);
        break;
    case ST_SEEDING: stage_set(i, ST_WITHER); break;
    case ST_WITHER:
        stage_set(i, ST_COMPOST);
        W.died++;
        ev_p(p);
        ev_s(" withered to compost");
        ev_push();
        garden_changed = 1;
        break;
    case ST_COMPOST:
        p->stage = ST_EMPTY;
        gw.sync |= (uint16_t)(1u << i);
        break;
    default: break;
    }
}

static void wild_seed(void)
{
    int i = empty_near(girand(G_NPLOT), 6);
    if (i < 0)
        return;
    plant_at(i, 60 + girand(17), girand(16), girand(16), 0);
    W.wild++;
    ev_s("A wild seed blew in: ");
    ev_p(&W.p[i]);
    ev_push();
}

static int varieties(void)               /* how many kinds of plant are growing */
{
    uint32_t m = 0;
    int i, n = 0;
    for (i = 0; i < G_NPLOT; i++)
        if (living(&W.p[i]))
            m |= 1u << W.p[i].var;
    for (i = 0; i < 16; i++)
        n += (int)(m >> i & 1u);
    return n;
}

static void second(void)
{
    static const float GROW[G_NGROWTH] = {2.0f, 4.0f, 8.0f, 16.0f};   /* quarters a second: x0.5 .. x4 */
    static const float WX[W_N] = {1.2f, 1.0f, 2.2f, 0.4f};
    int q, i, live;
    W.seconds++;
    gw.qacc += GROW[proj.growth % G_NGROWTH] * WX[W.weather % W_N];
    q = (int)gw.qacc;
    gw.qacc -= (float)q;
    for (i = 0; i < G_NPLOT; i++)
        plant_second(i, q);
    if (++W.daysec >= G_DAY_S) {
        W.daysec = 0;
        W.day++;
        ev_s("Day ");
        ev_u(W.day);
        ev_s(" in ");
        ev_s(gardener_name());
        ev_s("'s garden");
        ev_push();
        garden_changed = 1;
    }
    if (W.seconds % 60u == 0u && (!W.wleft || !--W.wleft))
        weather_new();
    if (--gw.act_in <= 0) {
        gw.act_in = (int16_t)(30 + girand(45));
        gardener_act();
    }
    /* mood drifts to what the garden gives: flowers cheer, an empty garden weighs */
    if (W.seconds % 20u == 0u) {
        int target = 35 + garden_count(ST_FLOWER) * 8 + (W.weather == W.g.likes ? 15 : 0) - (garden_living() < 2 ? 15 : 0);
        mood(target > W.g.mood ? 1 : target < W.g.mood ? -1 : 0);
    }
    visitors();
    live = garden_living();
    if (!live) {                         /* nothing growing: a seed blows in soon */
        if (!gw.wild_in) {
            gw.wild_in = (uint8_t)(15 + girand(20));
        } else if (!--gw.wild_in) {
            wild_seed();
            garden_changed = 1;
        }
    } else if ((live < 3 && chance(6)) || (varieties() < 3 && chance(3))) {
        wild_seed();
    }
    music_apply();
}

/* ------------------------------------------------------------- the API --- */
void garden_new(uint32_t seed)
{
    static const int8_t START[3][2] = {{ST_FLOWER, 2}, {ST_GROW, 8}, {ST_SPROUT, 4}};
    int k;
    memset(&W, 0, sizeof W);
    W.rng = seed ? seed : 1u;
    W.g.name = (uint8_t)girand(8);
    W.g.mood = 55;
    W.g.coat = (uint8_t)girand(4);
    W.g.likes = (uint8_t)girand(3);       /* sun, cloud or rain */
    W.weather = W_SUN;
    W.wleft = 5;
    W.day = 1;
    scale_build();
    for (k = 0; k < 3; k++) {            /* three starters, already on their way, so it plays from the first second */
        int i = START[k][1], st = START[k][0];
        plant_at(i, sc_note(62 + 12 * (k == 1), k * 2), girand(16), girand(16), F_KEPT);
        stage_set(i, st);
        W.p[i].age = (uint16_t)(W.p[i].life / 3);
        W.p[i].water = 80;
    }
    W.born = 0;
    ev_s(gardener_name());
    ev_s(" starts a new garden");
    ev_push();
    garden_changed = 1;
    gw.act_in = 12;
    gw.gx = gw.tx = 120.0f;
    gw.bee = gw.rabbit = 0;
    gw.wild_in = 0;
    garden_resync();
}

void garden_tick_ms(uint32_t dt)
{
    gw.ms_acc += dt;
    while (gw.ms_acc >= 1000u) {
        gw.ms_acc -= 1000u;
        second();
    }
    if (gw.bee) {
        gw.bee_t += dt;
        if (gw.bee_t >= 7000u) {
            gw.bee = 0;
            if (W.p[gw.bee_a].stage == ST_FLOWER && W.p[gw.bee_b].stage == ST_FLOWER)
                hybrid(gw.bee_a, gw.bee_b);
        }
    }
    if (gw.rabbit) {
        uint32_t was = gw.rabbit_t;
        gw.rabbit_t += dt;
        if (was < 4000u && gw.rabbit_t >= 4000u)
            rabbit_bites();
        if (gw.rabbit_t >= 8000u)
            gw.rabbit = 0;
    }
    gw.task_t = gw.task_t > dt ? gw.task_t - dt : 0u;
    if (!gw.task_t && gw.task != TK_IDLE)
        gw.task = TK_IDLE;
}

void garden_frame(void)
{
    int i, sent = 0;
    float d = gw.tx - gw.gx;             /* the gardener walks: 30 px a second at 60 frames */
    gw.gx += d > 0.5f ? 0.5f : d < -0.5f ? -0.5f : d;
    for (i = 0; i < G_NPLOT && sent < 3; i++)   /* the music: a few plots a frame (the queue has room) */
        if (gw.sync >> i & 1u) {
            gw.sync &= (uint16_t)~(1u << i);
            plot_send(i);
            sent++;
        }
    if (!gw.sync)
        drones();
    if (gw.last[P_DENSITY] == -999)
        music_apply();
}

int garden_plant_key(int pitch)
{
    int col = (pitch - 53) * 6 / 27, i, best = -1, bd = 99;
    col = col < 0 ? 0 : col > 5 ? 5 : col;
    for (i = 0; i < G_NPLOT; i++) {      /* an empty plot nearest the key's place (low left, high right) */
        int d = pcol(i) - col;
        d = (d < 0 ? -d : d) * 2 + (prow(i) == 1 ? 0 : 1);
        if (W.p[i].stage == ST_COMPOST)
            d += 20;                     /* compost is dug in only when nothing is free */
        else if (W.p[i].stage != ST_EMPTY)
            continue;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    if (best < 0)
        return -1;
    plant_at(best, pitch, girand(16), girand(16), F_YOURS);
    W.yours++;
    ev_s("You planted ");
    ev_p(&W.p[best]);
    ev_s(" ");
    ev_s(bed_of(best));
    ev_push();
    walk_to(best, TK_PLANT);
    garden_changed = 1;
    return best;
}

void garden_water_all(void)
{
    int i;
    for (i = 0; i < G_NPLOT; i++)
        if (living(&W.p[i]))
            W.p[i].water = 100;
    walk_to(girand(G_NPLOT), TK_WATER);
    ev_s(gardener_name());
    ev_s(" watered the whole garden");
    ev_push();
    mood(2);
}

void garden_bend_again(void)             /* a setting changed: the bent parameters sent again on the next frame */
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        gw.last[i] = (int16_t)-999;
}
