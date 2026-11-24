/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen */
/* POND: the world (pond.h) and how the music follows it. Part of the unity build, after project.c, before ui.c.
 *
 *   fish      swim, each to its own ends: shy ones keep to the edges and the lily pads, greedy ones race for food,
 *             social ones look for their friends, grumpy ones chase others off. Each carries a note (a step of the
 *             season's scale) and sings it now and then in its species' voice (a bell, panned where it swims).
 *   meetings  two fish close together play their notes as a chord, ripples spread and the pad's chord moves to
 *             one that holds both; how they get on (their temperaments) makes friends or rivals.
 *   food      a key drops food where the key sits along the pond and adds its note to the scale for 90 s; fish
 *             swarm to it, grow when fed; insects land now and then too.
 *   life      well-fed friends in spring and summer may have fry (a parent's species, a note near a parent's); old
 *             fish drift away (a gentle low chord); a quiet pond gets a newcomer at dawn.
 *   time      a day takes Day length minutes (24 by default: an in-world minute a second); four days a season.
 *             Spring Lydian, summer Ionian, autumn Dorian, winter Aeolian. Nights are slower and darker, the pad
 *             turns to Voices and frogs croak (the pulse). Lily pads by season, a heron now and then by day:
 *             sudden silence, the fish scatter, then their notes come back in a flurry.
 *   density   follows how busy the pond is (food, meetings, eating) and the Busy setting: the bell loops, the
 *             brightness, how often fish sing, how often the chord moves. */
#include "pond.h"

const char *const PD_NAMES[PD_NNAMES] = {
    "Bram", "Pip", "Olive", "Moss", "Juno", "Biscuit", "Wren", "Nori", "Clem", "Dot", "Fern", "Gus", "Hazel", "Ike",
    "Kip", "Lou", "Mabel", "Ned", "Opal", "Pike", "Quill", "Rue", "Sid", "Tilly", "Umi", "Vera", "Wally", "Yuzu",
    "Zed", "Basil", "Cora", "Dune", "Edie", "Fig", "Gil", "Hob", "Ivy", "Jot", "Kelp", "Lark"};
const char *const PD_SPECIES[PD_NSPECIES] = {"Koi", "Goldfish", "Minnow", "Carp"};
const char *const PD_TEMPER[PD_NTEMPER] = {"shy", "greedy", "social", "grumpy"};
const char *const PD_SEASON[4] = {"Spring", "Summer", "Autumn", "Winter"};
const char *const PD_TOD[4] = {"Night", "Dawn", "Day", "Dusk"};

/* the species: length (px at size 0), cruising speed (px/s), biggest size, lifespan (in-world days, + up to half
 * again), register (a MIDI note the scale is laid from), voice (the bell loops' timbres) */
static const uint8_t SP_LEN[PD_NSPECIES] = {12, 9, 7, 15};
static const uint8_t SP_SPEED[PD_NSPECIES] = {14, 13, 22, 9};
static const uint8_t SP_MAXSIZE[PD_NSPECIES] = {12, 9, 6, 14};
static const uint8_t SP_LIFE[PD_NSPECIES] = {40, 30, 18, 50};
static const uint8_t SP_BASE[PD_NSPECIES] = {60, 67, 72, 48};
static const uint8_t SEASON_MODE[4] = {MODE_LYDIAN, MODE_IONIAN, MODE_DORIAN, MODE_AEOLIAN};
static const uint8_t DAY_MIN[] = {6, 12, 24, 48, 96};
#define PD_NDAYLEN 5

/* ------------------------------------------------------------ runtime --- */
typedef struct {
    float x, y, vx, vy, hx, hy;          /* where, how fast, which way it faces */
    float tx, ty, tt;                    /* where it is going, for how long more */
    float tail, sing, glow, fade, flee;
    float chase_t, follow_t;
    int8_t chase, follow;                /* a fish it chases (grumpy) or follows (a friend); -1 */
    int8_t dying;
} fishrt_t;

#define NRIP 16
#define NFOOD 10
typedef struct {
    float x, y, r, v, a;
    uint8_t kind;                        /* 0 water, 1 a song (the fish's colour), 2 a meeting, 3 the heron */
    uint16_t col;
} ripple_t;
typedef struct {
    float x, y, life;
    int8_t pc;                           /* its note (pitch class), -1 an insect */
} food_t;

static struct {
    fishrt_t f[PD_NFISH];
    float meet_cd[PD_NFISH][PD_NFISH];
    ripple_t rip[NRIP];
    food_t food[NFOOD];
    uint32_t food_until[12];             /* the notes the food added to the scale: until (plat_ms) */
    uint32_t prev, rng;
    float wmin;                          /* the in-world minute's fraction */
    float act, meet_heat, eat_heat;      /* how busy the pond is, 0..1 */
    float chord_t, plip_t, save_t, meet_gap;   /* meet_gap: seconds to the next meeting the pond allows */
    int chord_deg;
    uint16_t pool_sent;
    int16_t sent[P_NPARAMS];
    /* the heron */
    uint8_t heron;                       /* 0 none, 1 landing, 2 standing (hush), 3 standing, 4 leaving */
    float hx, hy, ht, hdx, hdy;
    /* the caption: what just happened, and who */
    char cap[32];
    int8_t cap_fish;
    uint32_t cap_until;
    int8_t sel;                          /* the fish chosen (the Fish page, PRESETS) */
    uint32_t sel_until;                  /* marked in the pond until */
    uint8_t want_save;                   /* 1 something happened worth keeping; 2 periodic */
    uint8_t ff;                          /* fast-forward (tests): no sound */
    uint8_t started;
    float frog_puff[3];
} pd;

static uint32_t pd_rnd(void)
{
    uint32_t x = pd.rng ? pd.rng : 0x2545F491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    pd.rng = x;
    return x;
}
static float pd_frand(void) { return (float)(pd_rnd() >> 8) * (1.0f / 16777216.0f); }
static int pd_irand(int n) { return n > 0 ? (int)((pd_rnd() >> 8) % (uint32_t)n) : 0; }

static world_t *W(void) { return &proj.w; }

int pd_season(void) { return (int)(((W()->day ? W()->day - 1u : 0u) / 4u) % 4u); }
int pd_tod(void)
{
    int m = W()->minute;
    return m < 300 ? TOD_NIGHT : m < 420 ? TOD_DAWN : m < 1080 ? TOD_DAY : m < 1200 ? TOD_DUSK : TOD_NIGHT;
}
/* daylight 0 (night) .. 1 (day), smooth through dawn and dusk */
float pd_light(void)
{
    float m = (float)W()->minute + pd.wmin;
    if (m < 300.0f || m >= 1200.0f)
        return 0.0f;
    if (m < 420.0f)
        return (m - 300.0f) / 120.0f;
    if (m < 1080.0f)
        return 1.0f;
    return 1.0f - (m - 1080.0f) / 120.0f;
}
int pd_alive(void)
{
    int i, n = 0;
    for (i = 0; i < PD_NFISH; i++)
        n += W()->fish[i].alive == 1;
    return n;
}

/* ------------------------------------------------------------ text --- */
static char *pd_cat(char *d, const char *s)
{
    while ((*d = *s++))
        d++;
    return d;
}
static char *pd_num(char *d, uint32_t v)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *d++ = t[--n];
    *d = 0;
    return d;
}
static const char *fname(int i) { return PD_NAMES[W()->fish[i].name % PD_NNAMES]; }

static int pd_same(const char *a, const char *b)  /* (no libc on the FM-1) */
{
    int i;
    for (i = 0; i < PD_LOGW - 1 && a[i] == b[i]; i++)
        if (!a[i])
            return 1;
    return i == PD_LOGW - 1;
}

/* the journal: the newest last, the oldest falls off */
static void pd_log(const char *t)
{
    world_t *w = W();
    int i;
    if (w->nlog && pd_same(w->log[w->nlog - 1].t, t))
        return;                                  /* the same again: once is enough */
    if (w->nlog >= PD_NLOG) {
        for (i = 1; i < PD_NLOG; i++)
            w->log[i - 1] = w->log[i];
        w->nlog = PD_NLOG - 1;
    }
    w->log[w->nlog].day = w->day;
    for (i = 0; i < PD_LOGW - 1 && t[i]; i++)
        w->log[w->nlog].t[i] = t[i];
    w->log[w->nlog].t[i] = 0;
    w->nlog++;
}
/* what just happened, in the pond's corner (and, if it matters, the journal and a save soon) */
static void pd_event(int fish, const char *t, int keep)
{
    int i;
    for (i = 0; i < 31 && t[i]; i++)
        pd.cap[i] = t[i];
    pd.cap[i] = 0;
    pd.cap_fish = (int8_t)fish;
    pd.cap_until = plat_ms() + 5000u;
    if (keep) {
        pd_log(t);
        pd.want_save = 1;
    }
}

/* ------------------------------------------------------------ music --- */
static uint32_t season_pool(void) { return EN_MODE_POOL[SEASON_MODE[pd_season()]]; }

static int scale_note(int base, int deg)        /* a step of the season's scale from the root at or below base */
{
    int st[12], n = en_scale(season_pool(), st), root = proj.par[P_ROOT], b, note;
    b = base - ((base - root) % 12 + 12) % 12;
    note = b + st[deg % n] + 12 * (deg / n) + 12 * proj.par[P_BOCT];
    while (note > 100)
        note -= 12;
    while (note < 36)
        note += 12;
    return note;
}
static int fish_pc(int i)
{
    return scale_note(SP_BASE[W()->fish[i].species], W()->fish[i].deg) % 12;
}

static void sing(int i, float loud)
{
    fish_t *f = &W()->fish[i];
    float v = (0.38f + 0.03f * (float)f->size) * loud;
    if (f->temper == T_SHY)
        v *= 0.65f;
    pd.f[i].glow = 1.0f;
    if (!pd.ff && en_playing) {
        en_bell(scale_note(SP_BASE[f->species], f->deg), (int)(fm_clampf(v, 0.05f, 1.0f) * 127.0f), f->species,
                (pd.f[i].x - 120.0f) * (1.0f / 120.0f));
    }
}

static void ripple(float x, float y, float rmax, int kind, uint16_t col)
{
    int i, best = 0;
    float lo = 9.0f;
    for (i = 0; i < NRIP; i++) {
        float a = pd.rip[i].a;
        if (a < lo) {
            lo = a;
            best = i;
        }
    }
    pd.rip[best].x = x;
    pd.rip[best].y = y;
    pd.rip[best].r = 1.0f;
    pd.rip[best].v = rmax;
    pd.rip[best].a = 1.0f;
    pd.rip[best].kind = (uint8_t)kind;
    pd.rip[best].col = col;
}

/* the chord: the step of the season's scale whose triad holds the most fish, weighed by how close they swim to
 * others and how lately they sang; the one playing counts less, so it moves */
static void chord_choose(int must_a, int must_b)
{
    int st[12], n = en_scale(season_pool(), st), d, i, j, best = 0;
    float bs = -1.0f;
    for (d = 0; d < n; d++) {
        float s = d == 0 ? 0.4f : 0.0f;
        for (i = 0; i < PD_NFISH; i++) {
            int k;
            float w = 1.0f + pd.f[i].glow;
            if (W()->fish[i].alive != 1)
                continue;
            for (j = 0; j < PD_NFISH; j++)
                if (j != i && W()->fish[j].alive == 1) {
                    float dx = pd.f[i].x - pd.f[j].x, dy = pd.f[i].y - pd.f[j].y;
                    if (dx * dx + dy * dy < 45.0f * 45.0f)
                        w += 0.5f;
                }
            k = ((int)W()->fish[i].deg - d + 2 * n) % n;
            if (k == 0 || k == 2 || k == 4)
                s += w * (i == must_a || i == must_b ? 4.0f : 1.0f);
            else if (k == 6)
                s += 0.3f * w;
        }
        if (d == pd.chord_deg)
            s *= 0.55f;
        s += pd_frand() * 0.8f;
        if (s > bs) {
            bs = s;
            best = d;
        }
    }
    pd.chord_deg = best;
    if (!pd.ff)
        en_chord_root_to(proj.par[P_ROOT] + st[best]);
    pd.chord_t = 18.0f + 40.0f * (1.0f - pd.act) + 10.0f * pd_frand();
}

/* ------------------------------------------------------------ fish --- */
static float flen(int i) { return (float)SP_LEN[W()->fish[i].species] + 0.7f * (float)W()->fish[i].size; }

static int free_name(void)
{
    int tries, n, i, used;
    for (tries = 0; tries < 80; tries++) {
        n = pd_irand(PD_NNAMES);
        used = 0;
        for (i = 0; i < PD_NFISH; i++)
            if (W()->fish[i].alive && W()->fish[i].name == n)
                used = 1;
        if (!used)
            return n;
    }
    return pd_irand(PD_NNAMES);
}

static void place(int i, float x, float y)
{
    fishrt_t *r = &pd.f[i];
    memset(r, 0, sizeof *r);
    r->x = x;
    r->y = y;
    r->hx = pd_frand() < 0.5f ? 1.0f : -1.0f;
    r->tx = x;
    r->ty = y;
    r->sing = 2.0f + 8.0f * pd_frand();
    r->fade = 1.0f;
    r->chase = r->follow = -1;
}

static int new_fish(int species, int temper, int deg, int size, int age_days)
{
    world_t *w = W();
    int i, j;
    for (i = 0; i < PD_NFISH; i++)
        if (!w->fish[i].alive)
            break;
    if (i >= PD_NFISH)
        return -1;
    memset(&w->fish[i], 0, sizeof w->fish[i]);
    w->fish[i].alive = 1;
    w->fish[i].species = (uint8_t)species;
    w->fish[i].temper = (uint8_t)temper;
    w->fish[i].name = (uint8_t)free_name();
    w->fish[i].deg = (uint8_t)(deg < 0 ? 0 : deg > 13 ? 13 : deg);
    w->fish[i].size = (uint8_t)size;
    w->fish[i].hunger = 40;
    w->fish[i].age = (uint16_t)(age_days * 24);
    w->fish[i].life = (uint16_t)((SP_LIFE[species] + pd_irand(SP_LIFE[species] / 2 + 1)) * 24);
    for (j = 0; j < PD_NFISH; j++) {
        w->rel[i][j] = w->rel[j][i] = 0;
        pd.meet_cd[i][j] = pd.meet_cd[j][i] = 10.0f;
    }
    place(i, 40.0f + 160.0f * pd_frand(), 30.0f + 116.0f * pd_frand());
    return i;
}

static int in_water(float x, float y)           /* a squircle, 112 x 80 about the middle */
{
    float nx = (x - 120.0f) * (1.0f / 112.0f), ny = (y - 88.0f) * (1.0f / 80.0f);
    nx *= nx;
    ny *= ny;
    return nx * nx + ny * ny < 1.0f;
}

void pd_new_world(uint32_t seed)
{
    world_t *w = W();
    static const uint8_t SPECIES0[5] = {SP_KOI, SP_GOLD, SP_MINNOW, SP_CARP, SP_KOI};
    int i, t[4] = {0, 1, 2, 3};
    memset(w, 0, sizeof *w);
    memset(&pd, 0, sizeof pd);
    pd.rng = seed | 1u;
    w->seed = seed;
    w->day = 1;
    w->minute = 480;
    for (i = 3; i > 0; i--) {                    /* every temperament once, then one more */
        int k = pd_irand(i + 1), x = t[i];
        t[i] = t[k];
        t[k] = x;
    }
    for (i = 0; i < 5; i++) {
        int f = new_fish(SPECIES0[i], i < 4 ? t[i] : pd_irand(PD_NTEMPER), (i * 3 + pd_irand(2)) % 10, 3 + pd_irand(4),
                         3 + pd_irand(12));
        if (f >= 0) {
            int j;
            for (j = 0; j < f; j++)
                w->rel[f][j] = w->rel[j][f] = (int8_t)(pd_irand(31) - 10);
        }
    }
    for (i = 0; i < PD_NLILY; i++) {             /* lily pads: near the banks, apart */
        int k, ok = 0;
        for (k = 0; k < 40 && !ok; k++) {
            float a = pd_frand() * FM_TWO_PI, rr = 0.55f + 0.35f * pd_frand();
            int x = 120 + (int)(fm_cosf(a) * 100.0f * rr), y = 88 + (int)(fm_sinf(a) * 70.0f * rr), j;
            ok = in_water((float)x, (float)y);
            for (j = 0; j < i && ok; j++) {
                int dx = x - w->lily[j].x, dy = y - w->lily[j].y;
                if (dx * dx + dy * dy < 26 * 26)
                    ok = 0;
            }
            if (ok) {
                w->lily[i].x = (uint8_t)x;
                w->lily[i].y = (uint8_t)y;
                w->lily[i].r = (uint8_t)(7 + pd_irand(5));
                w->lily[i].flower = (uint8_t)(pd_irand(3) == 0);
            }
        }
    }
    pd_log("A new pond. Five fish.");
    pd.cap_fish = -1;
    pd.sel = 0;
    pd.chord_deg = 0;
    pd.started = 0;
}

/* after a load: the runtime from what was kept */
void pd_resume(void)
{
    world_t *w = W();
    int i, j;
    uint32_t seed = w->seed ^ ((uint32_t)w->day * 2654435761u) ^ w->minute;
    memset(&pd, 0, sizeof pd);
    pd.rng = seed | 1u;
    for (i = 0; i < PD_NFISH; i++) {
        if (w->fish[i].alive) {
            float x = (float)w->fish[i].x * (1.0f / 16.0f), y = (float)w->fish[i].y * (1.0f / 16.0f);
            if (!in_water(x, y))
                x = 120.0f, y = 88.0f;
            place(i, x, y);
        }
        for (j = 0; j < PD_NFISH; j++)
            pd.meet_cd[i][j] = 8.0f;
    }
    pd.cap_fish = -1;
    pd.sel = 0;
    pd.started = 0;
}

void pd_keep(void)                               /* the runtime's positions into what is kept */
{
    int i;
    for (i = 0; i < PD_NFISH; i++)
        if (W()->fish[i].alive) {
            W()->fish[i].x = (int16_t)(pd.f[i].x * 16.0f);
            W()->fish[i].y = (int16_t)(pd.f[i].y * 16.0f);
        }
}

static void rel_add(int a, int b, int d)
{
    int v = W()->rel[a][b] + d;
    v = v < -100 ? -100 : v > 100 ? 100 : v;
    W()->rel[a][b] = W()->rel[b][a] = (int8_t)v;
}

static void spawn(int a, int b)
{
    world_t *w = W();
    fish_t *fa = &w->fish[a], *fb = &w->fish[b];
    int par = pd_irand(2) ? a : b, sp = w->fish[par].species, tm, dg, f;
    char t[48], *p;
    tm = pd_frand() < 0.6f ? w->fish[pd_irand(2) ? a : b].temper : pd_irand(PD_NTEMPER);
    dg = (int)w->fish[par].deg + (pd_irand(3) - 1) * (1 + pd_irand(2));
    f = new_fish(sp, tm, dg, 1, 0);
    if (f < 0)
        return;
    w->fish[f].ma = (uint8_t)(fa->name + 1);
    w->fish[f].pa = (uint8_t)(fb->name + 1);
    w->fish[f].gen = (uint8_t)((fa->gen > fb->gen ? fa->gen : fb->gen) + 1);
    place(f, (pd.f[a].x + pd.f[b].x) * 0.5f, (pd.f[a].y + pd.f[b].y) * 0.5f);
    pd.f[f].fade = 0.0f;
    pd.f[f].follow = (int8_t)par;
    pd.f[f].follow_t = 60.0f;
    w->rel[f][a] = w->rel[a][f] = 60;
    w->rel[f][b] = w->rel[b][f] = 60;
    fa->spawn_cd = fb->spawn_cd = 72;
    fa->hunger = (uint8_t)(fa->hunger + 15);
    fb->hunger = (uint8_t)(fb->hunger + 15);
    w->born++;
    p = pd_cat(t, fname(a));
    p = pd_cat(p, " & ");
    p = pd_cat(p, fname(b));
    p = pd_cat(p, ": fry ");
    pd_cat(p, fname(f));
    pd_event(f, t, 1);
    ripple(pd.f[f].x, pd.f[f].y, 26.0f, 2, 0);
    ripple(pd.f[f].x, pd.f[f].y, 16.0f, 2, 0);
    sing(a, 1.0f);
    sing(b, 1.0f);
    sing(f, 0.8f);
}

/* two fish meet: their notes together, ripples, and how they get on */
static void meet(int a, int b)
{
    world_t *w = W();
    fish_t *fa = &w->fish[a], *fb = &w->fish[b];
    int before = w->rel[a][b], d = 4 + pd_irand(5), after, food_near = 0, i;
    char t[48], *p;
    for (i = 0; i < NFOOD; i++)
        if (pd.food[i].life > 0.0f) {
            float dx = pd.food[i].x - pd.f[a].x, dy = pd.food[i].y - pd.f[a].y;
            if (dx * dx + dy * dy < 40.0f * 40.0f)
                food_near = 1;
        }
    if (fa->temper == T_SOCIAL || fb->temper == T_SOCIAL)
        d += 5;
    if (fa->temper == T_SHY || fb->temper == T_SHY)
        d -= 3;
    if (fa->temper == T_GRUMPY || fb->temper == T_GRUMPY)
        d -= 13 + pd_irand(6);
    if (fa->temper == T_GREEDY && fb->temper == T_GREEDY && food_near)
        d -= 12;
    if (fa->species == fb->species)
        d += 2;
    rel_add(a, b, d);
    after = w->rel[a][b];
    w->meets++;
    pd.meet_cd[a][b] = pd.meet_cd[b][a] = 60.0f + 60.0f * pd_frand();
    pd.meet_heat += 1.0f;
    pd.meet_gap = 6.0f + 10.0f * (1.0f - pd.act);
    ripple((pd.f[a].x + pd.f[b].x) * 0.5f, (pd.f[a].y + pd.f[b].y) * 0.5f, 30.0f, 2, 0);
    ripple((pd.f[a].x + pd.f[b].x) * 0.5f, (pd.f[a].y + pd.f[b].y) * 0.5f, 18.0f, 2, 0);
    sing(a, 0.9f);
    sing(b, 0.9f);
    pd.f[a].sing = pd.f[b].sing = 6.0f + 6.0f * pd_frand();
    if (pd.chord_t > 6.0f)
        chord_choose(a, b);
    p = t;
    if (before < 50 && after >= 50) {
        p = pd_cat(pd_cat(pd_cat(p, fname(a)), " & "), fname(b));
        pd_cat(p, ": friends");
        pd_event(a, t, 1);
    } else if (before > -50 && after <= -50) {
        p = pd_cat(pd_cat(pd_cat(p, fname(a)), " & "), fname(b));
        pd_cat(p, ": rivals");
        pd_event(a, t, 1);
    } else if (d < 0 && (fa->temper == T_GRUMPY || fb->temper == T_GRUMPY)) {
        int g = fa->temper == T_GRUMPY ? a : b, o = g == a ? b : a;
        p = pd_cat(pd_cat(pd_cat(p, fname(g)), " chases "), fname(o));
        pd.f[g].chase = (int8_t)o;
        pd.f[g].chase_t = 4.0f;
        pd.f[o].flee = 2.5f;
        pd_event(g, t, 0);
    } else if (after >= 50) {
        p = pd_cat(pd_cat(pd_cat(p, fname(a)), " & "), fname(b));
        pd_cat(p, " swim together");
        pd.f[a].follow = (int8_t)b;
        pd.f[a].follow_t = 20.0f + 20.0f * pd_frand();
        pd_event(a, t, 0);
    } else {
        p = pd_cat(pd_cat(pd_cat(p, fname(a)), " meets "), fname(b));
        pd_event(a, t, 0);
    }
    /* fry: well-fed grown friends, in spring and summer, room in the pond */
    if (after >= 60 && fa->hunger < 40 && fb->hunger < 40 && fa->age >= 72 && fb->age >= 72 && !fa->spawn_cd &&
        !fb->spawn_cd && pd_season() <= SEA_SUMMER && pd_alive() < proj.cap && pd_frand() < 0.4f)
        spawn(a, b);
}

static void eat(int i, int k)
{
    fish_t *f = &W()->fish[i];
    food_t *fd = &pd.food[k];
    f->hunger = (uint8_t)(f->hunger > 30 ? f->hunger - 30 : 0);
    fd->life = 0.0f;
    pd.eat_heat += 0.6f;
    ripple(fd->x, fd->y, 10.0f, 0, 0);
    sing(i, 1.1f);
    if (++f->feed >= 3) {
        f->feed = 0;
        if (f->size < SP_MAXSIZE[f->species]) {
            f->size++;
            if (f->size == SP_MAXSIZE[f->species] || f->size % 4 == 0) {
                char t[48];
                pd_cat(pd_cat(t, fname(i)), " grew");
                pd_event(i, t, f->size == SP_MAXSIZE[f->species]);
                return;
            }
        }
    }
    if (pd_frand() < 0.35f) {
        char t[48];
        pd_cat(pd_cat(t, fname(i)), f->temper == T_GREEDY ? " gobbles" : " eats");
        pd_event(i, t, 0);
    }
}

static void die(int i)                           /* old age: a gentle low chord, the journal */
{
    world_t *w = W();
    char t[48], *p;
    int j, days = w->fish[i].age / 24;
    p = pd_cat(pd_cat(t, fname(i)), " drifted off, ");
    p = pd_num(p, (uint32_t)days);
    pd_cat(p, "d");
    w->fish[i].alive = 0;
    w->died++;
    for (j = 0; j < PD_NFISH; j++) {
        w->rel[i][j] = w->rel[j][i] = 0;
        if (pd.f[j].follow == i)
            pd.f[j].follow = -1;
        if (pd.f[j].chase == i)
            pd.f[j].chase = -1;
    }
    pd_event(-1, t, 1);
    if (!pd.ff && en_playing) {
        en_bell(scale_note(40, 0), 70, 3, 0.0f);
        en_bell(scale_note(52, 4), 50, 1, -0.3f);
    }
    if (pd.sel == i)
        pd.sel = -1;
}

static void newcomer(void)
{
    static const uint8_t SPW[8] = {SP_KOI, SP_GOLD, SP_GOLD, SP_MINNOW, SP_MINNOW, SP_MINNOW, SP_CARP, SP_KOI};
    int f = new_fish(SPW[pd_irand(8)], pd_irand(PD_NTEMPER), pd_irand(10), 2 + pd_irand(3), 1 + pd_irand(6));
    char t[48];
    if (f < 0)
        return;
    place(f, pd_frand() < 0.5f ? 16.0f : 224.0f, 60.0f + 50.0f * pd_frand());
    pd.f[f].fade = 0.0f;
    pd.f[f].tx = 120.0f;
    pd.f[f].ty = 88.0f;
    pd.f[f].tt = 6.0f;
    pd_cat(pd_cat(t, fname(f)), " arrived");
    pd_event(f, t, 1);
}

/* ------------------------------------------------------------ time --- */
static void hour(void)
{
    world_t *w = W();
    int i;
    for (i = 0; i < PD_NFISH; i++) {
        fish_t *f = &w->fish[i];
        if (f->alive != 1)
            continue;
        if (f->age < 65000u)
            f->age++;
        if (f->hunger < 100)
            f->hunger = (uint8_t)(f->hunger + (f->size > 8 ? 3 : 2) > 100 ? 100 : f->hunger + (f->size > 8 ? 3 : 2));
        if (f->spawn_cd)
            f->spawn_cd--;
        if (f->age >= f->life && !pd.f[i].dying) {
            pd.f[i].dying = 1;
            pd.f[i].tx = pd.f[i].x;
            pd.f[i].ty = pd.f[i].y;
        }
    }
    /* friendships fade a little without meetings, rivalries cool */
    if (w->minute % 360 == 0) {
        int j;
        for (i = 0; i < PD_NFISH; i++)
            for (j = i + 1; j < PD_NFISH; j++) {
                int v = w->rel[i][j];
                if (v > 20)
                    rel_add(i, j, -1);
                else if (v < -20)
                    rel_add(i, j, 1);
            }
    }
}

static void minute_tick(void)
{
    world_t *w = W();
    int s0 = pd_season(), t0 = pd_tod();
    if (++w->minute >= 1440) {
        w->minute = 0;
        w->day++;
    }
    if (w->minute % 60 == 0)
        hour();
    if (pd_season() != s0) {
        char t[32];
        pd_cat(pd_cat(t, PD_SEASON[pd_season()]), " comes");
        pd_event(-1, t, 1);
    }
    if (pd_tod() != t0) {
        if (pd_tod() == TOD_DAWN) {
            int i;
            if (pd_alive() < 3 || (pd_alive() < proj.cap && pd_irand(6) == 0))
                newcomer();
            for (i = 0; i < PD_NLILY; i++)       /* the flowers open and close by the day */
                w->lily[i].flower = (uint8_t)(pd_irand(3) == 0);
        }
        if (pd_tod() == TOD_NIGHT && (int32_t)(plat_ms() - pd.cap_until) > 0)
            pd_event(-1, "Night falls", 0);
    }
    /* the heron: by day, about once every day or two */
    if (!pd.heron && pd_tod() == TOD_DAY && pd_irand(1800) == 0 && pd_alive() > 0) {
        float a = pd_frand() * FM_TWO_PI;
        pd.heron = 1;
        pd.ht = 0.0f;
        pd.hx = 120.0f + fm_cosf(a) * 92.0f;
        pd.hy = 88.0f + fm_sinf(a) * 62.0f;
        pd.hdx = fm_cosf(a);
        pd.hdy = fm_sinf(a);
    }
}

/* ------------------------------------------------------------ the heron --- */
static void heron_tick(float dt)
{
    int i;
    if (!pd.heron)
        return;
    pd.ht += dt;
    if (pd.heron == 1 && pd.ht > 1.2f) {         /* landed: sudden silence, everyone flees */
        pd.heron = 2;
        pd.ht = 0.0f;
        W()->herons++;
        if (!pd.ff)
            en_hush(4, 1);
        pd_event(-1, "Heron! Everyone hides", 1);
        ripple(pd.hx, pd.hy, 40.0f, 3, 0);
        for (i = 0; i < PD_NFISH; i++)
            if (W()->fish[i].alive == 1)
                pd.f[i].flee = 6.0f;
    } else if (pd.heron == 2 && pd.ht > 3.5f) {  /* the scatter: the notes come back in a flurry */
        pd.heron = 3;
        pd.ht = 0.0f;
        if (!pd.ff)
            en_hush(100, 2);
        for (i = 0; i < PD_NFISH; i++)
            if (W()->fish[i].alive == 1)
                pd.f[i].sing = 0.1f + 1.6f * pd_frand();
    } else if (pd.heron == 3 && pd.ht > 5.0f) {
        pd.heron = 4;
        pd.ht = 0.0f;
        pd_event(-1, "The heron flew off", 0);
    } else if (pd.heron == 4 && pd.ht > 1.5f) {
        pd.heron = 0;
    }
}

/* ------------------------------------------------------------ steering --- */
static void steer(int i, float dt, float night)
{
    world_t *w = W();
    fish_t *f = &w->fish[i];
    fishrt_t *r = &pd.f[i];
    float sp = (float)SP_SPEED[f->species] * (1.0f - 0.025f * (float)f->size) * night, tx, ty, dx, dy, d, ax = 0.0f,
          ay = 0.0f, nx, ny, q, len = flen(i);
    int j, k, best = -1;
    float bd = 1e9f;
    if (r->dying) {                              /* drifting up, slowing, fading */
        r->vx *= 1.0f - dt;
        r->vy *= 1.0f - dt;
        r->x += r->vx * dt;
        r->y += r->vy * dt;
        r->fade -= dt * (1.0f / 10.0f);
        r->tail += dt * 0.5f;
        if (r->fade <= 0.0f)
            die(i);
        return;
    }
    if (r->fade < 1.0f)
        r->fade = fm_minf(1.0f, r->fade + dt * (1.0f / 3.0f));
    r->tt -= dt;
    if (r->tt <= 0.0f) {                         /* somewhere new to go */
        if (f->temper == T_SHY && pd_frand() < 0.6f) {
            int l = pd_irand(PD_NLILY);
            if (w->lily[l].r && pd_season() != SEA_WINTER) {
                r->tx = (float)w->lily[l].x + (pd_frand() - 0.5f) * 10.0f;
                r->ty = (float)w->lily[l].y + (pd_frand() - 0.5f) * 10.0f;
            } else {
                float a = pd_frand() * FM_TWO_PI;
                r->tx = 120.0f + fm_cosf(a) * 90.0f;
                r->ty = 88.0f + fm_sinf(a) * 60.0f;
            }
        } else {
            r->tx = 30.0f + 180.0f * pd_frand();
            r->ty = 24.0f + 128.0f * pd_frand();
        }
        r->tt = 4.0f + 9.0f * pd_frand();
    }
    tx = r->tx;
    ty = r->ty;
    /* food: the nearest within reach of its appetite */
    for (k = 0; k < NFOOD; k++)
        if (pd.food[k].life > 0.0f) {
            float reach = f->temper == T_GREEDY ? 260.0f : f->temper == T_SHY ? 70.0f : 130.0f;
            dx = pd.food[k].x - r->x;
            dy = pd.food[k].y - r->y;
            d = dx * dx + dy * dy;
            if (f->hunger + (f->temper == T_GREEDY ? 40 : 0) > 12 && d < reach * reach && d < bd) {
                bd = d;
                best = k;
            }
        }
    if (best >= 0) {
        tx = pd.food[best].x;
        ty = pd.food[best].y;
        sp *= f->temper == T_GREEDY ? 2.2f : 1.7f;
        if (bd < (len * 0.5f + 3.0f) * (len * 0.5f + 3.0f))
            eat(i, best);
    } else if (r->chase >= 0 && r->chase_t > 0.0f && w->fish[r->chase].alive) {
        r->chase_t -= dt;
        tx = pd.f[r->chase].x;
        ty = pd.f[r->chase].y;
        sp *= 1.8f;
    } else if (r->follow >= 0 && r->follow_t > 0.0f && w->fish[r->follow].alive) {
        r->follow_t -= dt;
        tx = pd.f[r->follow].x - pd.f[r->follow].hx * 10.0f;
        ty = pd.f[r->follow].y - pd.f[r->follow].hy * 10.0f + 6.0f;
    } else if (f->temper == T_SOCIAL) {          /* social: towards its best friend */
        int bf = -1, bv = 20;
        for (j = 0; j < PD_NFISH; j++)
            if (j != i && w->fish[j].alive == 1 && w->rel[i][j] > bv) {
                bv = w->rel[i][j];
                bf = j;
            }
        if (bf >= 0 && pd_frand() < dt * 0.2f) {
            r->tx = pd.f[bf].x;
            r->ty = pd.f[bf].y;
            r->tt = 5.0f;
        }
    }
    if (r->flee > 0.0f) {                        /* away from the heron (or a grumpy one) */
        r->flee -= dt;
        sp *= 2.4f;
        if (pd.heron) {
            tx = r->x + (r->x - pd.hx);
            ty = r->y + (r->y - pd.hy);
        } else {
            tx = r->x - (r->tx - r->x);
            ty = r->y - (r->ty - r->y);
            for (j = 0; j < PD_NFISH; j++)
                if (pd.f[j].chase == i) {
                    tx = r->x + (r->x - pd.f[j].x) * 3.0f;
                    ty = r->y + (r->y - pd.f[j].y) * 3.0f;
                }
        }
    }
    if (pd.heron == 2)                           /* hiding: still */
        sp *= r->flee > 4.5f ? 1.0f : 0.25f;
    dx = tx - r->x;
    dy = ty - r->y;
    d = fm_sqrtf(dx * dx + dy * dy) + 0.001f;
    if (d < 4.0f && best < 0)
        sp *= d * 0.25f;                         /* arrived: drift */
    ax = dx / d * sp;
    ay = dy / d * sp;
    for (j = 0; j < PD_NFISH; j++) {             /* room: shy ones want more */
        float room = f->temper == T_SHY ? 26.0f : 11.0f;
        if (j == i || w->fish[j].alive != 1)
            continue;
        dx = r->x - pd.f[j].x;
        dy = r->y - pd.f[j].y;
        d = dx * dx + dy * dy;
        if (d < room * room && d > 0.01f) {
            d = fm_sqrtf(d);
            ax += dx / d * (room - d) * 1.2f;
            ay += dy / d * (room - d) * 1.2f;
        }
    }
    nx = (r->x - 120.0f) * (1.0f / 112.0f);      /* the banks */
    ny = (r->y - 88.0f) * (1.0f / 80.0f);
    q = nx * nx * nx * nx + ny * ny * ny * ny;
    if (q > 0.45f) {
        ax -= nx * (q - 0.45f) * 120.0f;
        ay -= ny * (q - 0.45f) * 120.0f;
    }
    k = dt * 1.6f > 1.0f;
    r->vx += (ax - r->vx) * (k ? 1.0f : dt * 1.6f);
    r->vy += (ay - r->vy) * (k ? 1.0f : dt * 1.6f);
    r->x += r->vx * dt;
    r->y += r->vy * dt;
    if (!in_water(r->x, r->y)) {                 /* never on the bank */
        r->x += (120.0f - r->x) * 0.05f;
        r->y += (88.0f - r->y) * 0.05f;
    }
    d = fm_sqrtf(r->vx * r->vx + r->vy * r->vy);
    if (d > 1.0f) {                              /* it turns towards where it swims */
        float t = fm_minf(1.0f, dt * 5.0f), hx = r->hx + (r->vx / d - r->hx) * t, hy = r->hy + (r->vy / d - r->hy) * t;
        float n = fm_sqrtf(hx * hx + hy * hy) + 1e-6f;
        r->hx = hx / n;
        r->hy = hy / n;
    }
    r->tail += dt * (1.5f + d * 0.35f);
    if (r->tail > 100.0f)
        r->tail -= 100.0f;
}

/* ------------------------------------------------------------ the sound --- */
static void send(int p, int v)
{
    if (pd.sent[p] != v || !pd.started) {
        pd.sent[p] = (int16_t)v;
        en_set(p, v);
    }
}
/* the engine's settings: the player's (proj.par), shaped by the pond */
void pd_sound(void)
{
    float a = pd.act, light = pd_light();
    int p, tod = pd_tod(), frogs = pd_season() != SEA_WINTER && light < 0.3f;
    uint32_t pool = season_pool(), now = plat_ms();
    for (p = 0; p < P_NPARAMS; p++) {
        int v = proj.par[p];
        switch (p) {
        case P_BELLS: v = (int)((float)v * (0.75f + 0.3f * a)); break;
        case P_PAD: v = (int)((float)v * (1.0f - 0.15f * a)); break;
        case P_PULSE: v = frogs ? (int)((float)v * (0.6f + 0.4f * (1.0f - light))) : 0; break;
        case P_DENSITY: v = 5 + (int)(45.0f * a); break;
        case P_EVOLVE: v = 25 + (int)(45.0f * a); break;
        case P_BRIGHT: v = 18 + (int)(40.0f * a + 18.0f * light); break;
        case P_HITS: v = 15 + (int)(20.0f * a); break;
        case P_DRIFT: v = 100; break;            /* the pond moves the chord */
        case P_MOTION: v = 35; break;
        case P_PADTONE: v = tod == TOD_NIGHT || tod == TOD_DUSK; break;   /* Voices by night */
        case P_MODE: v = SEASON_MODE[pd_season()]; break;
        case P_SWELL: v = 55 + (int)(25.0f * (1.0f - a)); break;
        default: break;
        }
        send(p, v);
    }
    for (p = 0; p < 12; p++)                     /* the notes food added */
        if ((int32_t)(pd.food_until[p] - now) > 0)
            pool |= 1u << ((p - proj.par[P_ROOT] + 12) % 12);
    if (pool != pd.pool_sent || !pd.started) {
        pd.pool_sent = (uint16_t)pool;
        en_pool(pool);
    }
    pd.started = 1;
}
uint32_t pd_pool(void) { return pd.pool_sent; }

/* ------------------------------------------------------------ the player --- */
void pd_feed(int key, int pc)                    /* a key: food where the key sits along the pond, its note */
{
    int i, best = 0;
    float lo = 9.0f;
    for (i = 0; i < NFOOD; i++)
        if (pd.food[i].life < lo) {
            lo = pd.food[i].life;
            best = i;
        }
    pd.food[best].x = 22.0f + (float)key * (196.0f / 26.0f) + (pd_frand() - 0.5f) * 6.0f;
    pd.food[best].y = 40.0f + 96.0f * pd_frand();
    while (!in_water(pd.food[best].x, pd.food[best].y))
        pd.food[best].y += (88.0f - pd.food[best].y) * 0.2f;
    pd.food[best].life = 1.0f;
    pd.food[best].pc = (int8_t)pc;
    pd.food_until[pc % 12] = plat_ms() + 90000u;
    ripple(pd.food[best].x, pd.food[best].y, 14.0f, 0, 0);
    if (!pd.ff && en_playing)
        en_bell(72 + pc % 12 + 12 * proj.par[P_BOCT], 45, 2,
                (pd.food[best].x - 120.0f) * (1.0f / 120.0f));
}

void pd_sprinkle(void)                           /* a pinch all over: food without notes */
{
    int i;
    for (i = 0; i < 5; i++) {
        int k = (i * 2 + pd_irand(2)) % NFOOD;
        pd.food[k].x = 30.0f + 180.0f * pd_frand();
        pd.food[k].y = 30.0f + 116.0f * pd_frand();
        pd.food[k].life = 1.0f;
        pd.food[k].pc = -1;
        ripple(pd.food[k].x, pd.food[k].y, 10.0f, 0, 0);
    }
}

/* the chosen fish: the next living one */
int pd_select(int dir)
{
    int i, s = pd.sel < 0 ? 0 : pd.sel;
    if (!pd_alive())
        return pd.sel = -1;
    for (i = 0; i < PD_NFISH; i++) {
        s = ((s + (dir < 0 ? -1 : 1)) % PD_NFISH + PD_NFISH) % PD_NFISH;
        if (W()->fish[s].alive == 1)
            break;
    }
    pd.sel = (int8_t)s;
    pd.sel_until = plat_ms() + 6000u;
    return s;
}
int pd_selected(void)
{
    if (pd.sel < 0 || W()->fish[pd.sel].alive != 1)
        pd_select(1);
    return pd.sel;
}

int pd_best(int i, int sign)                     /* its best friend (sign 1) or worst rival (-1): -1 none */
{
    int j, b = -1, bv = 30;
    for (j = 0; j < PD_NFISH; j++)
        if (j != i && W()->fish[j].alive == 1 && W()->rel[i][j] * sign > bv) {
            bv = W()->rel[i][j] * sign;
            b = j;
        }
    return b;
}

/* ------------------------------------------------------------ the tick --- */
void pd_step(float dt)                           /* dt seconds of the pond */
{
    world_t *w = W();
    int i, j;
    float night = 0.65f + 0.35f * pd_light(), rate;
    if (pd_season() == SEA_WINTER)
        night *= 0.8f;
    /* the clock */
    pd.wmin += dt * 1440.0f / ((float)DAY_MIN[proj.daylen % PD_NDAYLEN] * 60.0f);
    while (pd.wmin >= 1.0f) {
        pd.wmin -= 1.0f;
        minute_tick();
    }
    heron_tick(dt);
    for (i = 0; i < PD_NFISH; i++)
        if (w->fish[i].alive == 1)
            steer(i, dt, night);
    /* meetings: one at a time, a few seconds apart */
    pd.meet_gap -= dt;
    for (i = 0; i < PD_NFISH; i++)
        for (j = i + 1; j < PD_NFISH; j++) {
            float dx, dy, near;
            if (pd.meet_cd[i][j] > 0.0f) {
                pd.meet_cd[i][j] -= dt;
                continue;
            }
            if (w->fish[i].alive != 1 || w->fish[j].alive != 1 || pd.f[i].dying || pd.f[j].dying || pd.heron == 2 ||
                pd.f[i].fade < 1.0f || pd.f[j].fade < 1.0f)
                continue;
            dx = pd.f[i].x - pd.f[j].x;
            dy = pd.f[i].y - pd.f[j].y;
            near = (flen(i) + flen(j)) * 0.5f + 2.0f;
            if (dx * dx + dy * dy < near * near && pd.meet_gap <= 0.0f)
                meet(i, j);
        }
    /* songs: social ones most, shy ones least; busier, more; at night, less */
    rate = (0.55f + 0.9f * pd.act) * (0.6f + 0.4f * pd_light());
    for (i = 0; i < PD_NFISH; i++) {
        fishrt_t *r = &pd.f[i];
        if (w->fish[i].alive != 1 || r->dying)
            continue;
        r->glow = fm_maxf(0.0f, r->glow - dt * 0.8f);
        if (pd.heron == 2)
            continue;
        r->sing -= dt * rate;
        if (r->sing <= 0.0f) {
            static const uint8_t GAP[PD_NTEMPER] = {22, 13, 8, 15};
            sing(i, 0.8f + 0.3f * pd_frand());
            ripple(r->x, r->y, 7.0f + flen(i) * 0.6f, 1, 0);
            r->sing = (float)GAP[w->fish[i].temper] * (0.6f + 0.8f * pd_frand());
        }
    }
    /* ripples, food */
    for (i = 0; i < NRIP; i++)
        if (pd.rip[i].a > 0.0f) {
            pd.rip[i].r += dt * (6.0f + pd.rip[i].v * 0.5f);
            pd.rip[i].a = 1.0f - pd.rip[i].r / pd.rip[i].v;
        }
    for (i = 0; i < NFOOD; i++)
        if (pd.food[i].life > 0.0f)
            pd.food[i].life -= dt * (1.0f / 45.0f);
    /* now and then, a plip: an insect lands, or a fish rises */
    pd.plip_t -= dt;
    if (pd.plip_t <= 0.0f) {
        float x = 30.0f + 180.0f * pd_frand(), y = 24.0f + 128.0f * pd_frand();
        pd.plip_t = 9.0f + 22.0f * pd_frand() * (1.2f - pd.act);
        if (in_water(x, y) && pd.heron != 2) {
            ripple(x, y, 12.0f, 0, 0);
            if (!pd.ff && en_playing)
                en_bell(scale_note(79, pd_irand(7)), 30 + pd_irand(20), 2, (x - 120.0f) * (1.0f / 120.0f));
            if (pd_frand() < 0.45f && pd_light() > 0.2f) {
                int k = pd_irand(NFOOD);
                if (pd.food[k].life <= 0.0f) {
                    pd.food[k].x = x;
                    pd.food[k].y = y;
                    pd.food[k].life = 0.7f;
                    pd.food[k].pc = -1;
                }
            }
        }
    }
    /* the chord, by the fish */
    pd.chord_t -= dt;
    if (pd.chord_t <= 0.0f)
        chord_choose(-1, -1);
    /* how busy */
    {
        int nf = 0;
        float t;
        for (i = 0; i < NFOOD; i++)
            nf += pd.food[i].life > 0.0f;
        pd.meet_heat *= 1.0f - dt * (1.0f / 30.0f);
        pd.eat_heat *= 1.0f - dt * (1.0f / 20.0f);
        t = 0.06f + 0.4f * (float)proj.busy * 0.01f + 0.08f * (float)(nf < 4 ? nf : 4) + 0.1f * fm_minf(pd.meet_heat, 2.5f) +
            0.1f * fm_minf(pd.eat_heat, 2.5f);
        t *= 0.6f + 0.4f * pd_light();
        if (pd.heron == 2)
            t = 0.0f;
        t = fm_clampf(t, 0.0f, 1.0f);
        pd.act += (t - pd.act) * fm_minf(1.0f, dt * (1.0f / 8.0f));
    }
    /* frogs puff with the thump */
    for (i = 0; i < 3; i++)
        pd.frog_puff[i] = fm_maxf(pd.frog_puff[i] - dt * 3.0f, 0.0f);
    if (en_drum_glow[0] > 0.7f)
        pd.frog_puff[pd_irand(3)] = 1.0f;
    /* 20 minutes of the pond: worth keeping (ui.c keep_due: at most once an hour) */
    pd.save_t += dt;
    if (pd.save_t > 1200.0f) {
        pd.save_t = 0.0f;
        if (!pd.want_save)
            pd.want_save = 2;
    }
}

void pd_tick(uint32_t now)                       /* the main loop: the pond in real time */
{
    uint32_t d = now - pd.prev;
    pd.prev = now;
    if (d > 250u)
        d = 250u;
    if (!pd.started) {
        pd_sound();
        chord_choose(-1, -1);
        return;
    }
    pd_step((float)d * 0.001f);
    pd_sound();
}

/* tests: minutes of the pond at once (quietly) */
void pd_fast(uint32_t minutes)
{
    uint32_t k, n = minutes * 600u;
    pd.ff = 1;
    for (k = 0; k < n; k++)
        pd_step(0.1f);
    pd.ff = 0;
    pd.started = 0;                              /* the sound catches up */
}
