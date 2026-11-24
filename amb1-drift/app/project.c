/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* AMB-1 project: one flash object (OBJ_PROJ). A project of another format, or none, gives the
 * defaults. Part of the unity build, after app.h.
 *
 * The pieces (their recipes are the engine's, en_piece): each keeps its own settings as you leave it;
 * Tune and MIDI out are global. */
project_t proj;

const char *piece_name(int i) { return en_piece_name(i); }

static int global_par(int p) { return p == P_TUNE || p == P_MIDI; }

static void recipe(int i, slot_t *s) { en_piece(i, s->par, &s->pool); }

static void take(const slot_t *s)                /* a piece's settings into play, the globals kept */
{
    int p;
    for (p = 0; p < P_NPARAMS; p++)
        if (!global_par(p))
            proj.par[p] = s->par[p];
    proj.pool = s->pool;
}

static uint8_t wander_busy;                      /* (ui.c) a Wander glide is under way: proj.par is between two pieces */

static void keep(void)                           /* the piece playing, into its slot */
{
    int p;
    if (wander_busy)                             /* mid-glide: neither piece's settings, so its slot stays as it was */
        return;
    for (p = 0; p < P_NPARAMS; p++)
        proj.slot[proj.piece].par[p] = proj.par[p];
    proj.slot[proj.piece].pool = proj.pool;
}

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < NPIECES; i++)
        recipe(i, &proj.slot[i]);
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = en_param_info(i)->def;
    take(&proj.slot[0]);
    proj.leds = 1;
    proj.wander = 0;                             /* Wander off, as AMB-1 */
    proj.wevery = 2;                             /* every 10 minutes */
    proj.worder = 0;                             /* the next piece */
    proj.wglide = 3;                             /* over a minute */
}

static int pool_ok(uint32_t p)                   /* the root and at least two more notes */
{
    int n = 0, i;
    for (i = 0; i < 12; i++)
        n += (int)(p >> i & 1u);
    return (p & 1u) && !(p >> 12) && n >= 3;
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

static int project_valid(void)                  /* an older format: the recipes again */
{
    int i;
    if (proj.magic != PROJ_MAGIC || !pool_ok(proj.pool) || !pars_ok(proj.par))
        return 0;
    if (proj.format != PROJ_FORMAT || proj.piece >= NPIECES)
        return 0;
    if (proj.wander > 1 || proj.wevery >= WD_NEVERY || proj.worder > 1 || proj.wglide >= WD_NGLIDE)
        return 0;
    for (i = 0; i < NPIECES; i++)
        if (!pool_ok(proj.slot[i].pool) || !pars_ok(proj.slot[i].par))
            return 0;
    return 1;
}

/* format 3 (0.9 before the layer pages): 23 settings a piece. Carried over: each piece's own, the
 * new ones from its recipe */
#define V3_NPARAMS (P_PADTONE + 1)
typedef struct {
    int16_t par[V3_NPARAMS];
    uint16_t pool;
} slot_v3_t;
typedef struct {
    uint32_t magic, format;
    int16_t par[V3_NPARAMS];
    uint16_t pool;
    uint8_t leds, piece, rsv0[2];
    slot_v3_t slot[V4_NPIECES];
    uint8_t rsv[16];
} project_v3_t;

/* formats 4 and 5: Drift 1.1's settings were not there yet */
typedef struct {
    int16_t par[V5_NPARAMS];
    uint16_t pool;
} slot_v5_t;

/* a piece's settings as an older format kept them (its first n), the rest from its recipe */
static void carry_slot(int i, const int16_t *par, int n, uint16_t pool)
{
    int p;
    recipe(i, &proj.slot[i]);
    for (p = 0; p < n; p++)
        proj.slot[i].par[p] = par[p];
    proj.slot[i].pool = pool;
}
static void carry_play(const int16_t *par, int n)   /* the piece playing: the same */
{
    slot_t r;
    int p;
    recipe(proj.piece, &r);
    for (p = 0; p < P_NPARAMS; p++)
        proj.par[p] = p < n ? par[p] : r.par[p];
}

/* format 4 (AMB-1 1.x): eight pieces, no Wander. Carried over whole; the new pieces from their recipes, Wander
 * off */
typedef struct {
    uint32_t magic, format;
    int16_t par[V5_NPARAMS];
    uint16_t pool;
    uint8_t leds, piece, rsv0[2];
    slot_v5_t slot[V4_NPIECES];
    uint8_t rsv[16];
} project_v4_t;

static void from_v4(const project_v4_t *o)
{
    int i;
    project_defaults();                          /* the new pieces' recipes, Wander's defaults */
    proj.pool = o->pool;
    proj.leds = o->leds;
    proj.piece = o->piece < V4_NPIECES ? o->piece : 0;
    for (i = 0; i < (int)V4_NPIECES; i++)
        carry_slot(i, o->slot[i].par, V5_NPARAMS, o->slot[i].pool);
    carry_play(o->par, V5_NPARAMS);
}

/* format 5 (AMB-1 Drift 1.0): sixteen pieces and Wander. Carried over whole, Wander's settings too; Drift 1.1's
 * settings and its new pieces from their recipes */
typedef struct {
    uint32_t magic, format;
    int16_t par[V5_NPARAMS];
    uint16_t pool;
    uint8_t leds, piece, rsv0[2];
    slot_v5_t slot[V5_NPIECES];
    uint8_t wander, wevery, worder, wglide;
    uint8_t rsv[12];
} project_v5_t;

static void from_v5(const project_v5_t *o)
{
    int i;
    project_defaults();
    proj.pool = o->pool;
    proj.leds = o->leds;
    proj.piece = o->piece < V5_NPIECES ? o->piece : 0;
    for (i = 0; i < (int)V5_NPIECES; i++)
        carry_slot(i, o->slot[i].par, V5_NPARAMS, o->slot[i].pool);
    carry_play(o->par, V5_NPARAMS);
    proj.wander = o->wander;
    proj.wevery = o->wevery;
    proj.worder = o->worder;
    proj.wglide = o->wglide;
}

static void from_v3(const project_v3_t *o)
{
    int i;
    project_defaults();                          /* the new pieces' recipes, Wander's defaults */
    proj.pool = o->pool;
    proj.leds = o->leds;
    proj.piece = o->piece < V4_NPIECES ? o->piece : 0;
    for (i = 0; i < (int)V4_NPIECES; i++)
        carry_slot(i, o->slot[i].par, V3_NPARAMS, o->slot[i].pool);
    carry_play(o->par, V3_NPARAMS);
}

int project_load(void)
{
    static union {
        project_t now;
        project_v3_t v3;
        project_v4_t v4;
        project_v5_t v5;
    } buf;
    int n;
    memset(&buf, 0, sizeof buf);
    n = plat_store_load(OBJ_PROJ, &buf, sizeof buf);
    if (n >= 8 && buf.now.magic == PROJ_MAGIC && buf.now.format == 3u)
        from_v3(&buf.v3);
    else if (n >= 8 && buf.now.magic == PROJ_MAGIC && buf.now.format == 4u)
        from_v4(&buf.v4);
    else if (n >= 8 && buf.now.magic == PROJ_MAGIC && buf.now.format == 5u)
        from_v5(&buf.v5);
    else
        proj = buf.now;
    if (n >= 8 && project_valid())
        return 0;
    project_defaults();
    return -1;
}

int project_save(void)
{
    keep();
    return plat_store_save(OBJ_PROJ, &proj, sizeof proj);
}

void project_piece(int i)
{
    keep();
    proj.piece = (uint8_t)((i % NPIECES + NPIECES) % NPIECES);
    take(&proj.slot[proj.piece]);
}

void project_piece_reset(void)
{
    slot_t s;
    recipe(proj.piece, &s);
    take(&s);
}

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, proj.par[i]);
    en_pool(proj.pool);
}
