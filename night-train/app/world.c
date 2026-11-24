/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen */
/* NIGHT TRAIN's world (world.h): the route, the train, the passengers and their stories. Single precision, no
 * libm, no 64-bit division (the FM-1); part of the unity build, and compiled alone by the host tests. */
#ifndef APP_NAME                             /* (alone, in the host tests; the unity build has its own) */
#include <string.h>
#endif
#include "world.h"

static uint32_t w_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

const uint8_t W_STRETCH_MIN[W_NSTRETCH] = {3, 5, 8, 12, 20};

#define w_ACCEL 0.42f                          /* m/s/s pulling away */
#define w_BRAKE 0.40f                          /* m/s/s the driver plans to stop with */
#define w_DWELL_S 40.0f                        /* a station stop */
#define w_NONE 255

/* ----------------------------------------------------------- randomness --- */
static uint32_t w_mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u);
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}
static uint32_t w_rnd(world_t *w)
{
    uint32_t x = w->rng ? w->rng : 0x1234567u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return w->rng = x;
}
static int w_irand(world_t *w, int n) { return n > 0 ? (int)((w_rnd(w) >> 8) % (uint32_t)n) : 0; }
static float w_frand(world_t *w) { return (float)(w_rnd(w) >> 8) * (1.0f / 16777216.0f); }
static int w_chance(world_t *w, int pct) { return w_irand(w, 100) < pct; }

/* -------------------------------------------------------------- the route --- */
static const char *const w_LAND_NAME[NLAND] = {"Forest", "Coast", "City Lights", "Snowfields", "Mountains", "Villages",
                                             "Lakes", "Moors"};
static const uint8_t w_LAND_KMH[NLAND] = {95, 90, 70, 100, 65, 85, 90, 105};
static const uint8_t w_LAND_TUN[NLAND] = {20, 12, 15, 30, 70, 10, 15, 20};
static const uint8_t w_LAND_BRG[NLAND] = {15, 40, 25, 15, 35, 20, 60, 15};
static const uint8_t w_KEYS[5] = {0, 2, 5, 7, 10};

const char *land_name(int l) { return w_LAND_NAME[(unsigned)l < NLAND ? l : 0]; }

int land_of(uint32_t seed, uint32_t idx)     /* the landscape of the stretch running to station idx */
{
    uint32_t h = w_mix(seed, idx >> 1);        /* a landscape most often lasts two stretches */
    if ((w_mix(seed ^ 0x55u, idx) & 3u) == 0u)
        h = w_mix(seed, idx + 7777u);
    return (int)(h % NLAND);
}

static const char *const w_PRE[24] = {"Ash", "Elm", "Thorn", "Wren", "Fen", "Marl", "Brack", "Hollin", "Raven", "Kes",
                                    "Lind", "Sedge", "Wych", "Cold", "Bram", "Oak", "Mill", "Hay", "Sil", "Calder",
                                    "Rook", "Alder", "Bell", "Fox"};
static const char *const w_SUF[12] = {"ford", "brook", "wick", "by", "ton", "field", "ham", "stead", "ley", "worth",
                                    "bury", "well"};
static const char *const w_LSUF[NLAND][4] = {
    {"wood", "hurst", "holt", "dene"},   {"haven", "mouth", "sands", "cliff"}, {" Central", " Road", " Square", " Junction"},
    {"fell", "rigg", "shaw", "frost"},   {"pass", "crag", "tor", "scar"},      {"thorpe", " Green", " Halt", "cott"},
    {"mere", "water", "pool", "ness"},   {"moor", "heath", "barrow", "rigg"},
};
static const char *const w_LEAD[4] = {"Upper ", "Lower ", "Little ", "St "};

void station_name(uint32_t seed, uint32_t idx, char *b)
{
    uint32_t h = w_mix(seed ^ 0xA5A5u, idx), k = 0;
    const char *p, *s;
    int l = land_of(seed, idx);
    if (idx == 0u) {                         /* where the journey began */
        p = "Old Harbour";
        while (*p)
            b[k++] = *p++;
        b[k] = 0;
        return;
    }
    p = w_PRE[h % 24u];
    s = (h >> 8) % 10u < 6u ? w_LSUF[l][(h >> 12) & 3u] : w_SUF[(h >> 16) % 12u];
    if ((h >> 20) % 100u < 12u && s[0] != ' ' && w_len(p) + w_len(s) <= 10u) {
        const char *q = w_LEAD[(h >> 24) & 3u];
        while (*q)
            b[k++] = *q++;
    }
    while (*p)
        b[k++] = *p++;
    while (*s && k < 19u)
        b[k++] = *s++;
    b[k] = 0;
}

/* ----------------------------------------------------------- the people --- */
static const char *const w_NAME[NREG] = {"Ines", "Mara", "Theo", "Aldous", "June", "Biscuit"};
static const char *const w_ROLE[NREG] = {"conductor", "night nurse", "musician", "old man", "art student", "a cat"};
const char *who_name(int c) { return w_NAME[(unsigned)c < NREG ? c : 0]; }
const char *who_role(int c) { return w_ROLE[(unsigned)c < NREG ? c : 0]; }

/* their stories: a beat needs them aboard and awake, the one it names (need) aboard and awake too, liked at least
 * aff, and this many rides (trips) behind them */
typedef struct {
    const char *text;
    uint8_t need;
    int8_t aff;
    uint8_t trips;
} w_beat_t;
#define w_NBEAT 7
static const w_beat_t w_BEAT[NREG][w_NBEAT] = {
    {{"Ines says this is her last winter on the line.", w_NONE, 0, 0},
     {"Ines knows every signal by its sound.", w_NONE, 0, 1},
     {"Ines shows June her notebook of stations.", CH_JUNE, 1, 2},
     {"Ines and Aldous nod like old colleagues.", CH_ALDOUS, 2, 3},
     {"Ines lets Biscuit ride in the guard's van.", CH_BISCUIT, 2, 4},
     {"Ines finds Aldous in an old staff photo.", CH_ALDOUS, 4, 5},
     {"Ines decides one more winter won't hurt.", w_NONE, 0, 7}},
    {{"Mara rubs her eyes. A twelve-hour shift ahead.", w_NONE, 0, 1},
     {"Mara asks Theo what is in the case.", CH_THEO, 0, 2},
     {"Theo saves Mara a seat by the window.", CH_THEO, 2, 3},
     {"Mara wakes Theo at his stop. They're even.", CH_THEO, 3, 4},
     {"Mara brings two coffees. One is for Theo.", CH_THEO, 4, 5},
     {"Mara tells Theo about the night ward.", CH_THEO, 5, 6},
     {"Mara and Theo share headphones till dawn.", CH_THEO, 6, 7}},
    {{"Theo has an audition in the city on Friday.", w_NONE, 0, 1},
     {"Theo practises fingering on his knee.", w_NONE, 0, 2},
     {"Theo opens the case: a cello, patched with tape.", w_NONE, 0, 3},
     {"Theo plays for the carriage. Nobody speaks.", w_NONE, 0, 4},
     {"Theo didn't get the part. He plays anyway.", w_NONE, 0, 5},
     {"Second audition. June draws him for luck.", CH_JUNE, 2, 6},
     {"Theo got the part! The carriage claps.", w_NONE, 0, 8}},
    {{"Aldous will not say where he is going.", w_NONE, 0, 1},
     {"Aldous counts the bridges under his breath.", w_NONE, 0, 2},
     {"Aldous tells Ines he drove engines once.", CH_INES, 2, 3},
     {"Aldous asks June not to draw his hands.", CH_JUNE, 0, 4},
     {"Aldous keeps a ticket older than June.", w_NONE, 0, 5},
     {"Aldous points at a dark field: 'Was a station.'", w_NONE, 0, 6},
     {"Aldous: 'Nowhere. I just like the train.'", CH_JUNE, 4, 8}},
    {{"June fills a page with telegraph poles.", w_NONE, 0, 1},
     {"June sketches the conductor's cap.", CH_INES, 0, 2},
     {"June draws Biscuit asleep.", CH_BISCUIT, 0, 3},
     {"June's sketchbook is half full of faces.", w_NONE, 0, 4},
     {"June gives Mara a drawing of the ward lights.", CH_MARA, 2, 5},
     {"June sends an art school form from the train.", w_NONE, 0, 6},
     {"June got in. She draws the train one last time.", w_NONE, 0, 8}},
    {{"A ginger cat boards. Nobody knows whose.", w_NONE, 0, 1},
     {"Ines names the cat Biscuit.", CH_INES, 0, 2},
     {"Biscuit sleeps on Theo's cello case.", CH_THEO, 0, 3},
     {"Biscuit follows Aldous off. Back next stop.", CH_ALDOUS, 1, 4},
     {"Biscuit has a collar now. Ines's doing.", CH_INES, 3, 5},
     {"Biscuit has a seat of its own. Nobody argues.", w_NONE, 0, 6},
     {"Biscuit brings Ines a mouse. A gift.", CH_INES, 5, 7}},
};
#define w_NEPI 3
static const char *const w_EPI[NREG][w_NEPI] = {
    {"Ines tells the story of the 1987 snow.", "Ines polishes her ticket punch.", "Ines waves at a signalman."},
    {"Mara naps between the hospital and home.", "Mara hums Theo's tune.", "Mara counts the hours till dawn."},
    {"Theo tunes the cello by the window.", "Theo writes a melody for the line.", "Theo plays the wheels' rhythm."},
    {"Aldous names a bridge before it comes.", "Aldous smiles at nothing in the dark.", "Aldous lends June his pencil."},
    {"June draws the passing lights.", "June swaps sketches with Theo.", "June draws Aldous, hands and all."},
    {"Biscuit patrols the carriage.", "Biscuit sleeps in Ines's cap.", "Biscuit watches the moths."},
};
int world_beats(int c) { (void)c; return w_NBEAT; }

static const char *const w_TALK[8] = {
    "%a and %b talk about the weather.", "%a shares a thermos of tea with %b.", "%a and %b watch the lights go by.",
    "%a lends %b a scarf.", "%a tells %b a long story.", "%a and %b play cards.", "%b laughs at something %a said.",
    "%a and %b sit in easy silence.",
};
static const char *const w_CAT[3] = {"Biscuit curls up on %b's lap.", "%b feeds Biscuit a crust.", "Biscuit purrs at %b."};
static const char *const w_ARGUE[3] = {"%a and %b argue about the window.", "%a and %b bicker over a sandwich.",
                                     "%a snaps at %b. Too tired."};
static const char *const w_SOLO[5] = {"%a reads by the little lamp.", "%a stares out at the dark.",
                                    "%a hums along with the wheels.", "%a counts the passing lights.",
                                    "%a draws on the misted glass."};
static const char *const w_SOLO_INES[2] = {"Ines checks tickets down the carriage.", "Ines calls out the next station."};
static const char *const w_SOLO_CAT[2] = {"Biscuit washes a paw.", "Biscuit watches the lamps go by."};

/* ------------------------------------------------------------------ log --- */
static void w_put(world_t *w, int type, int a, int b, int arg)
{
    logrec_t *r = &w->log[w->nlog % NLOG];
    float km = world_km(w);
    r->type = (uint8_t)type;
    r->a = (uint8_t)a;
    r->b = (uint8_t)b;
    r->arg = (uint8_t)arg;
    r->station = (uint16_t)w->station;
    r->km = (uint16_t)(uint32_t)km;
    w->nlog++;
}
const logrec_t *world_log(const world_t *w, int back)
{
    if (back < 0 || back >= NLOG || back >= (int)w->nlog)
        return 0;
    return &w->log[(w->nlog - 1u - (uint32_t)back) % NLOG];
}

static char *w_cat(char *b, char *e, const char *s)
{
    while (*s && b < e)
        *b++ = *s++;
    return b;
}
static char *w_num(char *b, char *e, uint32_t v)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n && b < e)
        *b++ = t[--n];
    return b;
}
static void w_fill(const world_t *w, const logrec_t *r, const char *f, char **bp, char *e)
{
    char *b = *bp, st[20];
    for (; *f && b < e; f++) {
        if (*f != '%') {
            *b++ = *f;
            continue;
        }
        f++;
        if (*f == 'a')
            b = w_cat(b, e, w_NAME[r->a % NREG]);
        else if (*f == 'b')
            b = w_cat(b, e, w_NAME[r->b % NREG]);
        else if (*f == 's') {
            station_name(w->seed, r->station, st);
            b = w_cat(b, e, st);
        } else if (*f == 'n')
            b = w_num(b, e, r->arg);
        else if (!*f)
            break;
    }
    *bp = b;
}
int world_text(const world_t *w, const logrec_t *r, char *b0, int n)
{
    char *b = b0, *e = b0 + n - 1;
    const char *f = "";
    switch (r->type) {
    case E_START: f = "The night train leaves %s."; break;
    case E_STATION: f = "%s."; break;
    case E_BOARD: f = "%a boards at %s."; break;
    case E_LEAVE: f = "%a gets off at %s."; break;
    case E_MISSED: f = "%a slept through %s!"; break;
    case E_WOKEN: f = "Ines wakes %a just in time."; break;
    case E_TALK: f = r->a == CH_BISCUIT || r->b == CH_BISCUIT ? w_CAT[r->arg % 3] : w_TALK[r->arg % 8]; break;
    case E_ARGUE: f = w_ARGUE[r->arg % 3]; break;
    case E_SLEEP: f = "%a falls asleep against the window."; break;
    case E_WAKE: f = "%a wakes with a start."; break;
    case E_SOLO:
        f = r->a == CH_INES ? w_SOLO_INES[r->arg % 2] : r->a == CH_BISCUIT ? w_SOLO_CAT[r->arg % 2] : w_SOLO[r->arg % 5];
        break;
    case E_STORY: f = w_BEAT[r->a % NREG][r->arg % w_NBEAT].text; break;
    case E_EPI: f = w_EPI[r->a % NREG][r->arg % w_NEPI]; break;
    case E_PLAY: f = "Theo plays softly. The carriage listens."; break;
    case E_SIGNAL: f = "Red signal. The train waits in the dark."; break;
    case E_GREEN: f = "Green signal. Moving again."; break;
    case E_DELAY: f = "Ines: 'A delay of %n minutes. Sorry, all.'"; break;
    case E_RAIN: f = r->arg ? "Snow begins to fall." : "Rain streaks the windows."; break;
    case E_DRY: f = "The weather clears."; break;
    case E_TUNNEL: f = "Into a long tunnel."; break;
    case E_BRIDGE: f = "Over the bridge, water far below."; break;
    default: break;
    }
    if (r->type == E_STATION) {
        b = w_cat(b, e, "Station ");
        b = w_num(b, e, r->station);
        b = w_cat(b, e, ": ");
    }
    w_fill(w, r, f, &b, e);
    *b = 0;
    return (int)(b - b0);
}

const char *world_story(const world_t *w, int c)
{
    int i;
    for (i = 0; i < (int)w->nlog && i < NLOG; i++) {
        const logrec_t *r = world_log(w, i);
        if (r->a == c && r->type == E_STORY)
            return w_BEAT[c][r->arg % w_NBEAT].text;
        if (r->a == c && r->type == E_EPI)
            return w_EPI[c][r->arg % w_NEPI];
    }
    if ((unsigned)c < NREG && w->who[c].beat)
        return w_BEAT[c][w->who[c].beat - 1].text;
    return 0;
}

/* ------------------------------------------------------------ geometry --- */
float world_km(const world_t *w) { return ((float)w->total_m + w->pos) * 0.001f; }
float world_left_km(const world_t *w) { return (w->len - w->pos) * 0.001f; }
int world_in_tunnel(const world_t *w) { return w->tun1 > w->tun0 && w->pos >= w->tun0 && w->pos < w->tun1; }
int world_on_bridge(const world_t *w) { return w->brg1 > w->brg0 && w->pos >= w->brg0 && w->pos < w->brg1; }
int world_at_station(const world_t *w) { return w->phase == PH_DWELL && w->step < 3; }

/* --------------------------------------------------------------- seats --- */
static int w_seat_free(const world_t *w, int s)
{
    int c;
    for (c = 0; c < NREG; c++)
        if (w->who[c].aboard && w->who[c].seat == s)
            return 0;
    return 1;
}
static int w_pick_seat(world_t *w)
{
    int k, s = w_irand(w, NSEAT - 1);
    for (k = 0; k < NSEAT - 1; k++, s = (s + 1) % (NSEAT - 1))
        if (w_seat_free(w, s))
            return s;
    return w_NONE;
}
static int w_nfree(const world_t *w)
{
    int s, n = 0;
    for (s = 0; s < NSEAT; s++)
        n += w_seat_free(w, s);
    return n;
}

static void w_like(world_t *w, int a, int b, int d)
{
    int v = w->aff[a][b] + d;
    v = v < -5 ? -5 : v > 12 ? 12 : v;
    w->aff[a][b] = w->aff[b][a] = (int8_t)v;
}
static void w_mood(world_t *w, int c, int d)
{
    int v = w->who[c].mood + d;
    w->who[c].mood = (uint8_t)(v < 0 ? 0 : v > 9 ? 9 : v);
}

/* -------------------------------------------------------- the journey --- */
static void w_new_stretch(world_t *w, int stretch_min)
{
    float t, kmh;
    int l;
    w->total_m += (uint32_t)w->len;
    w->station++;
    w->prev_land = w->land;
    l = land_of(w->seed, w->station);
    w->land = (uint8_t)l;
    w->key = w_KEYS[w_mix(w->seed ^ 0x3Cu, w->station) % 5u];
    kmh = (float)w_LAND_KMH[l] * (0.9f + 0.2f * w_frand(w));
    w->cruise = kmh * (1.0f / 3.6f);
    t = (float)stretch_min * 60.0f * (0.75f + 0.5f * w_frand(w));
    w->len = w->cruise * (t - 50.0f);        /* the time spent pulling away and braking, roughly */
    if (w->len < 1500.0f)
        w->len = 1500.0f;
    w->pos = 0.0f;
    w->tun0 = w->tun1 = w->brg0 = w->brg1 = w->sig = 0.0f;
    if (w_chance(w, w_LAND_TUN[l])) {
        float d = w->cruise * (25.0f + 45.0f * w_frand(w));
        if (d > w->len * 0.3f)
            d = w->len * 0.3f;
        w->tun0 = w->len * (0.15f + 0.25f * w_frand(w));
        w->tun1 = w->tun0 + d;
    }
    if (w_chance(w, w_LAND_BRG[l])) {
        float d = w->cruise * (10.0f + 12.0f * w_frand(w));
        w->brg0 = w->len * (0.6f + 0.15f * w_frand(w));
        w->brg1 = w->brg0 + d;
    }
    if (w_chance(w, 10))
        w->sig = w->len * (0.3f + 0.3f * w_frand(w));
    if (w->rain && w_chance(w, 40)) {
        w->rain = 0;
        w_put(w, E_DRY, 0, 0, 0);
    } else if (!w->rain && w_chance(w, 14)) {
        w->rain = 1;
        w_put(w, E_RAIN, 0, 0, l == LS_SNOW || l == LS_MOUNTAIN);
    }
}

void world_new(world_t *w, uint32_t seed)
{
    int c;
    memset(w, 0, sizeof *w);
    w->seed = seed ? seed : 0x5EEDu;
    w->rng = w_mix(w->seed, 1u) | 1u;
    w->phase = PH_DWELL;
    w->step = 1;                             /* at the first platform, doors open */
    w->timer = 34.0f;
    w->land = w->prev_land = (uint8_t)land_of(w->seed, 1u);
    w->talk = 60.0f;
    for (c = 0; c < NREG; c++) {
        w->who[c].mood = 6;
        w->who[c].seat = w_NONE;
        w->who[c].away = (uint8_t)(c == CH_BISCUIT ? 3 : c == CH_THEO ? 1 : 0);
    }
    w->who[CH_INES].aboard = 1;              /* the conductor rides every night, on the guard's seat */
    w->who[CH_INES].seat = NSEAT - 1;
    w->who[CH_INES].trips = 1;
    w->aff[CH_INES][CH_ALDOUS] = w->aff[CH_ALDOUS][CH_INES] = 1;
    w->strangers = 2;
    w_put(w, E_START, 0, 0, 0);
}

int world_valid(const world_t *w)
{
    int c;
    if (w->phase > PH_SIGNAL || w->land >= NLAND || w->step > 3 || !(w->len >= 0.0f && w->len < 1e7f) ||
        !(w->pos >= 0.0f && w->pos <= w->len + 1.0f) || !(w->speed >= 0.0f && w->speed < 80.0f) || w->strangers > NSEAT)
        return 0;
    for (c = 0; c < NREG; c++)
        if (w->who[c].mood > 9 || w->who[c].beat > w_NBEAT || (w->who[c].aboard && w->who[c].seat >= NSEAT))
            return 0;
    return 1;
}

/* at a station: who gets off (or sleeps through it) */
static void w_alight(world_t *w)
{
    int c;
    for (c = 1; c < NREG; c++) {
        who_t *p = &w->who[c];
        if (!p->aboard) {
            if (p->away)
                p->away--;
            continue;
        }
        if (p->stops > 1) {
            p->stops--;
            continue;
        }
        if (p->asleep && c != CH_BISCUIT) {
            if (w_chance(w, 55)) {
                w_put(w, E_WOKEN, c, CH_INES, 0);
                w_like(w, c, CH_INES, 1);
            } else {
                w_put(w, E_MISSED, c, 0, 0);
                w_mood(w, c, -2);
                p->asleep = 0;
                p->stops = 1;
                continue;
            }
        }
        p->asleep = 0;
        p->aboard = 0;
        p->seat = w_NONE;
        p->away = (uint8_t)(1 + w_irand(w, c == CH_MARA ? 2 : 4));
        w_put(w, E_LEAVE, c, 0, 0);
    }
    if (w->strangers)
        w->strangers = (uint8_t)(w->strangers - w_irand(w, w->strangers + 1));
}

static void w_board(world_t *w)
{
    int c, n;
    for (c = 1; c < NREG; c++) {
        who_t *p = &w->who[c];
        int s;
        if (p->aboard || p->away || !w_chance(w, c == CH_BISCUIT ? 35 : 55))
            continue;
        s = w_pick_seat(w);
        if (s == w_NONE)
            break;
        p->aboard = 1;
        p->seat = (uint8_t)s;
        p->asleep = 0;
        p->stops = (uint8_t)(c == CH_ALDOUS ? 3 + w_irand(w, 6) : 2 + w_irand(w, 4));
        if (p->trips < 255)
            p->trips++;
        w_mood(w, c, 1);
        w_put(w, E_BOARD, c, 0, 0);
    }
    n = w_nfree(w);
    w->strangers = (uint8_t)w_irand(w, (n > 4 ? 4 : n) + 1);
    if (w->strangers > n)
        w->strangers = (uint8_t)n;
}

/* a w_moment among the passengers: a story beat, a talk, a nap, a tune */
static int w_awake_aboard(const world_t *w, int c) { return w->who[c].aboard && !w->who[c].asleep; }
static int w_beat_ready(const world_t *w, int c)
{
    const who_t *p = &w->who[c];
    const w_beat_t *b;
    if (!w_awake_aboard(w, c) || p->beat >= w_NBEAT)
        return 0;
    b = &w_BEAT[c][p->beat];
    if (p->trips < b->trips * 3u)          /* a story takes many nights */
        return 0;
    if (b->need != w_NONE && (!w_awake_aboard(w, b->need) || w->aff[c][b->need] < b->aff))
        return 0;
    return 1;
}

static void w_moment(world_t *w)
{
    int c, n = 0, list[NREG], a, b, r = w_irand(w, 100);
    for (c = 0; c < NREG; c++)
        if (w_awake_aboard(w, c))
            list[n++] = c;
    for (c = 0; c < NREG; c++)               /* sleepers stir */
        if (w->who[c].aboard && w->who[c].asleep && w_chance(w, 30)) {
            w->who[c].asleep = 0;
            w_put(w, E_WAKE, c, 0, 0);
            return;
        }
    if (r < 22) {                            /* a story moves on */
        int ready[NREG], k = 0;
        for (c = 0; c < NREG; c++)
            if (w_beat_ready(w, c))
                ready[k++] = c;
        if (k) {
            const w_beat_t *bt;
            c = ready[w_irand(w, k)];
            bt = &w_BEAT[c][w->who[c].beat];
            w_put(w, E_STORY, c, bt->need == w_NONE ? c : bt->need, w->who[c].beat);
            w->who[c].beat++;
            w_mood(w, c, 1);
            if (bt->need != w_NONE)
                w_like(w, c, bt->need, 1);
            if (c == CH_THEO && (w->who[c].beat == 4 || w->who[c].beat == 5 || w->who[c].beat == 7))
                w->theo_play = 45;
            return;
        }
    }
    if (n >= 2 && r < 75) {                  /* two of them */
        a = list[w_irand(w, n)];
        do
            b = list[w_irand(w, n)];
        while (b == a);
        if (a == CH_BISCUIT) {
            a = b;
            b = CH_BISCUIT;
        }
        if (b == CH_BISCUIT) {
            w_put(w, E_TALK, CH_BISCUIT, a, w_irand(w, 3));
            w_like(w, a, b, 1);
            w_mood(w, a, 1);
        } else if (w_chance(w, 9 + (w->who[a].mood < 3 ? 15 : 0))) {
            w_put(w, E_ARGUE, a, b, w_irand(w, 3));
            w_like(w, a, b, -1);
            w_mood(w, a, -1);
            w_mood(w, b, -1);
        } else {
            w_put(w, E_TALK, a, b, w_irand(w, 8));
            w_like(w, a, b, 1);
            w_mood(w, a, 1);
            w_mood(w, b, 1);
        }
        return;
    }
    if (!n)
        return;
    a = list[w_irand(w, n)];
    if (a == CH_THEO && w_chance(w, 30)) {
        w_put(w, E_PLAY, a, 0, 0);
        w->theo_play = 40;
        for (c = 0; c < NREG; c++)
            if (c != a && w_awake_aboard(w, c))
                w_like(w, a, c, 1);
        return;
    }
    if (a != CH_INES && w_chance(w, a == CH_MARA || a == CH_BISCUIT || a == CH_ALDOUS ? 45 : 20)) {
        w->who[a].asleep = 1;
        w_put(w, E_SLEEP, a, 0, 0);
        return;
    }
    if (w->who[a].beat >= w_NBEAT && w_chance(w, 50)) {   /* their story told: its after-life */
        w_put(w, E_EPI, a, a, w_irand(w, w_NEPI));
        return;
    }
    w_put(w, E_SOLO, a, 0, w_irand(w, 5));
}

static void w_arrive(world_t *w)
{
    w->pos = w->len;
    w->speed = 0.0f;
    w->phase = PH_DWELL;
    w->step = 0;
    w->timer = w_DWELL_S;
}

static uint32_t w_step_dwell(world_t *w, float dt, int stretch_min)
{
    uint32_t ev = 0;
    float el;
    w->timer -= dt;
    el = w_DWELL_S - w->timer;
    if (w->step == 0) {                      /* arrived: the chime, who gets off */
        w_put(w, E_STATION, 0, 0, 0);
        if ((w->station & 1u) && w->who[CH_INES].trips < 255)
            w->who[CH_INES].trips++;         /* (the conductor's nights: one every two stations) */
        w_alight(w);
        w->step = 1;
        ev |= W_CHIME | W_ARRIVE | W_LOG;
    }
    if (w->step == 1 && el >= 12.0f) {       /* who gets on */
        uint16_t n0 = w->nlog;
        w_board(w);
        w->step = 2;
        if (w->nlog != n0)
            ev |= W_LOG;
    }
    if (w->step == 2 && el >= 22.0f) {       /* the next stretch begins: the music moves to it */
        uint16_t n0 = w->nlog;
        w_new_stretch(w, stretch_min);
        w->step = 3;
        ev |= W_SWAP;
        if (w_chance(w, 4))
            w_put(w, E_DELAY, CH_INES, 0, 2 + w_irand(w, 6));
        if (w->nlog != n0)
            ev |= W_LOG;
    }
    if (w->timer <= 0.0f && w->step == 3) {  /* doors close, away */
        w->phase = PH_RUN;
        w->talk = 20.0f + 40.0f * w_frand(w);
        ev |= W_DEPART;
    }
    return ev;
}

static uint32_t w_step_world(world_t *w, float dt, int stretch_min)
{
    uint32_t ev = 0;
    uint16_t n0 = w->nlog;
    int tun = world_in_tunnel(w), brg = world_on_bridge(w);
    w->sec += dt;
    while (w->sec >= 1.0f) {
        w->sec -= 1.0f;
        w->run_s++;
        if (w->theo_play)
            w->theo_play--;
    }
    switch (w->phase) {
    case PH_DWELL: return w_step_dwell(w, dt, stretch_min);
    case PH_RUN: {
        float left = w->len - w->pos, stop = w->speed * w->speed * (0.5f / w_BRAKE) + 2.0f;
        if (w->speed < w->cruise)
            w->speed = w->speed + w_ACCEL * dt > w->cruise ? w->cruise : w->speed + w_ACCEL * dt;
        if (w->sig > 0.0f && w->pos < w->sig && w->sig - w->pos <= stop) {
            w->phase = PH_SIGNAL;
            w->brake = w->speed * w->speed * 0.5f / (w->sig - w->pos > 1.0f ? w->sig - w->pos : 1.0f);
            w->timer = 0.0f;
        } else if (left <= stop) {
            w->phase = PH_BRAKE;
            w->brake = w->speed * w->speed * 0.5f / (left > 1.0f ? left : 1.0f);
        }
        break;
    }
    case PH_BRAKE:
        w->speed -= w->brake * dt;
        if (w->speed < 0.3f || w->pos >= w->len) {
            w_arrive(w);
            return W_ARRIVE;                 /* (w_step_dwell does the rest on the next tick) */
        }
        break;
    case PH_SIGNAL:
        if (w->speed > 0.0f) {
            w->speed -= w->brake * dt;
            if (w->speed <= 0.2f) {
                w->speed = 0.0f;
                w->timer = 30.0f + 40.0f * w_frand(w);
                w_put(w, E_SIGNAL, 0, 0, 0);
                if (w_chance(w, 50))
                    w_put(w, E_DELAY, CH_INES, 0, 2 + w_irand(w, 8));
            }
        } else {
            w->timer -= dt;
            if (w->timer <= 0.0f) {
                w->sig = 0.0f;
                w->phase = PH_RUN;
                w_put(w, E_GREEN, 0, 0, 0);
            }
        }
        break;
    default: break;
    }
    w->pos += w->speed * dt;
    if (w->pos > w->len)
        w->pos = w->len;
    if (!tun && world_in_tunnel(w))
        w_put(w, E_TUNNEL, 0, 0, 0);
    if (!brg && world_on_bridge(w))
        w_put(w, E_BRIDGE, 0, 0, 0);
    if (w->phase == PH_RUN || (w->phase == PH_SIGNAL && w->speed == 0.0f)) {
        w->talk -= dt;
        if (w->talk <= 0.0f) {
            w_moment(w);
            w->talk = 45.0f + 85.0f * w_frand(w);
        }
    }
    if (w->nlog != n0)
        ev |= W_LOG;
    return ev;
}

uint32_t world_tick(world_t *w, uint32_t ms, int stretch_min)
{
    uint32_t ev = 0;
    if (ms > 2000u)
        ms = 2000u;
    while (ms) {
        uint32_t k = ms > 100u ? 100u : ms;
        ev |= w_step_world(w, (float)k * 0.001f, stretch_min);
        ms -= k;
    }
    return ev;
}

void world_express(world_t *w)
{
    float stop;
    if (w->phase != PH_RUN)
        return;
    stop = w->speed * w->speed * (0.5f / w_BRAKE) + 2.0f;
    if (w->len - w->pos > stop + 600.0f) {
        w->pos = w->len - stop - 600.0f;
        if (w->sig > 0.0f && w->sig < w->pos)
            w->sig = 0.0f;
        if (w->tun1 > 0.0f && w->tun1 < w->pos)
            w->tun0 = w->tun1 = 0.0f;
        if (w->brg1 > 0.0f && w->brg1 < w->pos)
            w->brg0 = w->brg1 = 0.0f;
    }
}
