/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen */
/* The tavern (world.h): the regulars, the visitor, Hugh, the night. Part of the unity build, after project.c and
 * before ui.c; the main loop's, never the audio interrupt's. It tells the engine (dsp/amb.h) what to play and who
 * plays it, and watches where the tune is (en_sess, en_pass, en_part, en_rep, en_bar) to shape the arrangement.
 *
 * The night runs 18:00 to 01:00 on its own clock (the length a night takes is a setting). Early, a few come in and
 * play slow airs and waltzes; the middle is busy, reels and jigs, sets of two or three run together (each tune in
 * its own key, each a touch quicker), most of the room in on them; late, airs again, a lament perhaps, people
 * drifting home; then last orders, "Time, please!", one last waltz for everyone, and Hugh plays an air for the empty
 * room before the next night begins. A leader starts each tune alone; the others come in where a phrase begins: on
 * the repeat, or on the B part. In a long tune two melody players may trade parts on the middle time through (one
 * takes the A part alone, the other answers with the B), then all in for the last. Between tunes people go up for a
 * pint, arrive and leave.
 *
 * 1.2: everyone plays well. 1.1's moods, skill, drink, quarrels, refusals, stand-offs, broken strings and strangers
 * frozen out are gone, with every mishap they drove; what is left is the music's own shape. */

/* the regulars: name, instrument, whether they sing, when they usually come and go (10-minute slots from 18:00),
 * and how they look (hair, shirt, skin) */
static const struct {
    const char *name;
    uint8_t inst, sings, arrive, leave, hair, shirt, skin;
} REG[NREG] = {
    {"Old Tom", I_FIDDLE, 0, 6, 39, 0, 0, 0},
    {"Rosie", I_FIDDLE, 0, 12, 41, 1, 1, 1},
    {"Maeve", I_WHISTLE, 1, 3, 33, 2, 2, 1},
    {"Brigid", I_HARP, 0, 0, 36, 3, 3, 0},
    {"Declan", I_LUTE, 1, 15, 42, 4, 4, 2},
    {"Nora", I_BOX, 0, 9, 30, 5, 5, 0},
    {"Finn", I_BASS, 1, 6, 37, 6, 6, 2},
    {"Seamus", I_DRUM, 0, 12, 41, 7, 7, 1},
};
#define LANDLORD "Hugh"
static const char *const VISITOR_NAME[] = {"Ciaran", "Aoife", "Padraig", "Siobhan", "Eamon", "Grainne", "Liam",
                                           "Orla", "Cathal", "Niamh", "Colm", "Una"};
#define NVNAME (sizeof VISITOR_NAME / sizeof VISITOR_NAME[0])
static const uint8_t VISITOR_INST[5] = {I_FIDDLE, I_WHISTLE, I_LUTE, I_DRUM, I_BOX};
static const uint8_t NIGHT_MIN[6] = {20, 30, 45, 60, 90, 120};   /* how long a night takes, real minutes */

static world_t *W;                               /* the project's (project.c) */

/* ------------------------------------------------------------- the moment --- */
enum { W_GAP, W_TUNE, W_CLOSED };
enum { T_TUNE, T_SONG, T_LAMENT, T_LAST, T_HUGH };
static struct {
    uint8_t state;
    uint32_t prev, acc, gap;                     /* the last frame; ms toward the next minute; ms of the gap left */
    int8_t leader;                               /* who leads (NCH: Hugh), -1 nobody */
    uint8_t kind, root, mode, passes, harp, type, joined, fav;
    int16_t tempo;
    uint16_t seed;
    uint32_t done_seen, segs_seen;               /* en_tunes_done, en_segues as last seen */
    int8_t req_root;                             /* a key asked for (-1 none) */
    uint8_t dirty, urgent;                       /* to save; something memorable (ui.c keep_due) */
    uint16_t warp;                               /* the host's "timewarp": minutes go by N times as fast */
    uint8_t waiting_said, last_leader, since_song, called;   /* (called: "another after this")*/
    uint8_t all_in;                              /* a round was stood: everyone in on this (or the next) tune */
    uint8_t set_left, set_n, queued;             /* a set: tunes still to come, this one's place, the next queued */
    struct {
        uint16_t seed;
        uint8_t kind, root, mode;
        int16_t tempo;
    } nx;                                        /* the set's next tune, as queued */
    int8_t solo, trade_with;                     /* trading parts: the instrument with the melody alone (-1 none);
                                                  * the one who answers */
    uint8_t trade;                               /* its stage: 0 not yet, 1 the leader's A, 2 the B, 3 done, 9 none */
    uint32_t tune_ms, quiet_t;                   /* when the tune began; when the room went quiet */
    uint32_t rounds, trades;                     /* (the host's checks: rounds stood, parts traded, this run) */
    char tune_name[40], next_name[40];
} ws;
static uint32_t host_hold;                       /* (the host simulator's "hold": the next gap this long) */

static uint32_t wrnd(void)
{
    uint32_t x = W->rng ? W->rng : 0x2545F491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return W->rng = x;
}
static int wr(int n) { return n > 0 ? (int)((wrnd() >> 8) % (uint32_t)n) : 0; }
static int chance(int pct) { return wr(100) < pct; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

const char *ch_name(int i)
{
    if (i == NCH)
        return LANDLORD;
    if (i == VISITOR)
        return VISITOR_NAME[W->ch[VISITOR].name % NVNAME];
    return REG[i].name;
}
static int sings(int i) { return i < NREG && REG[i].sings; }
static int present(int i) { return W->ch[i].state == S_SEAT || W->ch[i].state == S_BAR; }
static int free_to_play(int i) { return W->ch[i].state == S_SEAT && !W->ch[i].busy; }
static int melodic(int i) { return W->ch[i].inst == I_FIDDLE || W->ch[i].inst == I_WHISTLE; }

/* ---------------------------------------------------------------- words --- */
static char *wcat(char *d, const char *s, char *end)
{
    while (*s && d < end)
        *d++ = *s++;
    *d = 0;
    return d;
}
/* a template's %1 %2 %3 filled in */
static void fill(char *out, int n, const char *t, const char *a, const char *b, const char *c)
{
    char *d = out, *end = out + n - 1;
    *d = 0;
    for (; *t && d < end; t++) {
        if (t[0] == '%' && t[1] >= '1' && t[1] <= '3') {
            const char *s = t[1] == '1' ? a : t[1] == '2' ? b : c;
            d = wcat(d, s ? s : "", end);
            t++;
        } else {
            *d++ = *t;
            *d = 0;
        }
    }
}

void say_event(const char *t);                   /* (ui.c) a screen reader hears it */
#ifdef PLAT_HOST
void host_event(const char *t);                  /* (host.c) the host's check of every event's words */
#endif
static void ev(const char *t, const char *a, const char *b, const char *c)
{
    int i;
    for (i = NLOG - 1; i > 0; i--) {
        memcpy(W->log[i], W->log[i - 1], LOG_W);
        W->log_min[i] = W->log_min[i - 1];
    }
    fill(W->log[0], LOG_W, t, a, b, c);
    W->log_min[0] = W->minute;
    ws.dirty = 1;
    say_event(W->log[0]);
#ifdef PLAT_HOST
    host_event(W->log[0]);
#endif
}
static const char *pick(const char *const *a, int n) { return a[wr(n)]; }
#define PICK(a) pick(a, (int)(sizeof a / sizeof a[0]))

static void remember(int type, int a, int b, int n)
{
    int i;
    if (a == VISITOR)
        b = W->ch[VISITOR].name;                 /* (a visitor: their name in b) */
    for (i = 0; i < W->nmem; i++)                /* once a night a kind of thing */
        if (W->mem[i].night == W->night && W->mem[i].type == type)
            return;
    for (i = NMEM - 1; i > 0; i--)
        W->mem[i] = W->mem[i - 1];
    W->mem[0].night = W->night;
    W->mem[0].type = (uint8_t)type;
    W->mem[0].a = (uint8_t)a;
    W->mem[0].b = (uint8_t)b;
    W->mem[0].n = (uint8_t)n;
    if (W->nmem < NMEM)
        W->nmem++;
    ws.urgent = 1;
}

/* a memorable night in words (the Nights page) */
void mem_text(const mem_t *m, char *out, int n)
{
    static const char *const NUM[5] = {"", "", "two", "three", "four"};
    const char *a = m->a < NREG ? REG[m->a].name : m->a == NCH ? LANDLORD : VISITOR_NAME[m->b % NVNAME];
    const char *b = m->b < NREG ? REG[m->b].name : m->b == VISITOR ? "the visitor" : "";
    char num[6];
    num[0] = (char)('0' + m->n / 10 % 10);
    num[1] = (char)('0' + m->n % 10);
    num[2] = 0;
    switch (m->type) {
    case M_LAMENT: fill(out, n, "%1 played a lament", a, 0, 0); break;
    case M_SONGS: fill(out, n, "a singers' night, %1 began it", a, 0, 0); break;
    case M_WELCOME: fill(out, n, "%1, a visitor, sat in", a, 0, 0); break;
    case M_GREAT: fill(out, n, "a great session: %1 in one tune", num[0] == '0' ? num + 1 : num, 0, 0); break;
    case M_NEWTUNE: fill(out, n, "%1 brought a new tune", a, 0, 0); break;
    case M_ROUND: fill(out, n, "you stood the house a round", 0, 0, 0); break;
    case M_SET: fill(out, n, "%1 led a set of %2", a, NUM[m->n < 5 ? m->n : 4], 0); break;
    case M_TRADE: fill(out, n, "%1 and %2 traded parts", a, b, 0); break;
    default: out[0] = 0; break;
    }
}

/* a tune's name, from its seed and kind: the same tune always has the same name */
void tune_name(uint16_t seed, int kind, char *out, int n)
{
    static const char *const ADJ[] = {"Lonesome", "Merry", "Silver", "Crooked", "Wild", "Gentle", "Morning",
                                      "Drunken", "Rambling", "Foggy", "Blue", "Golden", "Little", "Stony",
                                      "Windy", "Quiet"};
    static const char *const NOUN[] = {"Heron", "Fiddler", "Piper", "Ploughman", "Sailor", "Lark", "Kettle",
                                       "Road", "Hare", "Fox", "Boatman", "Miller", "Weaver", "Lantern", "Gate",
                                       "Bridge"};
    static const char *const OWNER[] = {"Molly", "Paddy", "Kitty", "Johnny", "Peggy", "Dinny", "Maggie", "Sean",
                                        "Biddy", "Mick", "Nell", "Tim", "Hugh", "Brigid", "Nora", "Finn"};
    static const char *const THING[] = {"Fancy", "Favourite", "Delight", "Farewell", "Welcome", "Rambles",
                                        "Frolic", "Dream"};
    static const char *const SAD[] = {"Lament", "Farewell", "Dream", "Sorrow", "Return", "Lullaby", "Lament",
                                      "Goodnight"};
    static const char *const PLACE[] = {"Ballyglen", "Kilmorrow", "Dunmara", "Carrickbeg", "Ardnacree",
                                        "Glenmoran", "Inishfallow", "Rathcoole"};
    static const char *const WHERE[] = {"Rushes", "Heather", "Hayloft", "Glen", "Mist", "Corner", "Rain",
                                        "Long Grass"};
    uint32_t h = (uint32_t)seed * 2246822519u + (uint32_t)kind * 3266489917u;
    char poss[16];
    h ^= h >> 15;
    h *= 2654435761u;
    h ^= h >> 13;
    switch ((kind == K_AIR ? 1u + (h & 1u) * 2u : h) & 3u) {
    case 0: fill(out, n, "The %1 %2", ADJ[h >> 4 & 15], NOUN[h >> 8 & 15], 0); break;
    case 1:
        fill(poss, (int)sizeof poss, "%1's", OWNER[h >> 4 & 15], 0, 0);
        fill(out, n, "%1 %2", poss, kind == K_AIR ? SAD[h >> 8 & 7] : THING[h >> 8 & 7], 0);
        break;
    case 2: fill(out, n, "The Humours of %1", PLACE[h >> 4 & 7], 0, 0); break;
    default: fill(out, n, "The %1 in the %2", NOUN[h >> 4 & 15], WHERE[h >> 8 & 7], 0); break;
    }
}

/* a tune's key and mode, from its seed: folk keys, folk modes (a lament: Aeolian) */
static void tune_key(uint16_t seed, int type, uint8_t *root, uint8_t *mode)
{
    static const uint8_t KEYS[4][4] = {{2, 7, 9, 0}, {2, 4, 9, 11}, {2, 9, 7, 2}, {4, 9, 11, 2}};
    uint32_t h = (uint32_t)seed * 40503u;
    int m = preset_mode((h >> 3) % 10u);         /* the preset's folk modes (1.0's: 3 Ionian, 3 Dorian, 2, 2) */
    if (type == T_LAMENT)
        m = MODE_AEOLIAN;
    *mode = (uint8_t)m;
    *root = KEYS[m == MODE_IONIAN ? 0 : m == MODE_DORIAN ? 1 : m == MODE_MIXOLYDIAN ? 2 : 3][(h >> 7) & 3u];
}

/* --------------------------------------------------------------- the clock --- */
static int phase(void)
{
    int m = W->minute;
    return m < MIN_BUSY ? PH_EARLY : m < MIN_LATE ? PH_BUSY : m < MIN_TIME ? PH_LATE : m < MIN_SHUT ? PH_CLOSING : PH_CLOSED;
}
/* the clock as the screen shows it: "9:40 PM" */
void clock_text(char *b)
{
    int m = W->minute, h = (18 + m / 60) % 24, mm = m % 60, h12 = h % 12 ? h % 12 : 12;
    int k = 0;
    if (h12 >= 10)
        b[k++] = '1';
    b[k++] = (char)('0' + h12 % 10);
    b[k++] = ':';
    b[k++] = (char)('0' + mm / 10);
    b[k++] = (char)('0' + mm % 10);
    b[k++] = ' ';
    b[k++] = h >= 12 ? 'P' : 'A';
    b[k++] = 'M';
    b[k] = 0;
}
static uint32_t minute_ms(void) { return (uint32_t)NIGHT_MIN[proj.night_len % 6u] * 60000u / MIN_DAWN; }


/* ------------------------------------------------------------- the band --- */
static int lead_inst(void)                       /* the instrument with the tune */
{
    if (ws.type == T_SONG)
        return I_VOICE;
    if (ws.leader == NCH)
        return I_WHISTLE;
    return ws.leader >= 0 ? W->ch[ws.leader].inst : -1;
}

static void band(void)                           /* who plays now, to the engine (it takes it from the next phrase) */
{
    int i, k, li = lead_inst();
    for (k = 0; k < EN_NINST; k++) {
        int n = 0, level = 0;
        for (i = 0; i < NCH; i++)
            if (W->ch[i].play && W->ch[i].inst == k && present(i))
                n++;
        if (ws.state == W_TUNE && k == I_VOICE && ws.type == T_SONG && ws.leader >= 0 && ws.leader < NCH)
            n = 1;
        if (ws.state == W_TUNE && k == I_WHISTLE && ws.leader == NCH)   /* Hugh, after hours */
            n++;
        if (ws.solo >= 0 && k != ws.solo && (k == I_FIDDLE || k == I_WHISTLE || k == I_VOICE))
            n = 0;                               /* trading parts: the other melody rests for the part */
        if (n) {
            level = 77 + (n - 1) * 10;
            if ((ws.type == T_SONG || ws.kind == K_AIR) && k != li)
                level = level * 2 / 3;            /* under a song or an air: softly */
        }
        en_inst(k, preset_inst(k, level));
    }
}

void world_restyle(void) { band(); }

static int players(void)
{
    int i, n = 0;
    for (i = 0; i < NCH; i++)
        n += W->ch[i].play != 0;
    return n + (ws.type == T_SONG);
}

/* --------------------------------------------------------- comings, goings --- */
static void new_night(void)
{
    int i;
    W->night++;
    W->minute = 0;
    W->songs = 0;
    W->round_at = 0;
    W->lament_done = 0;
    W->peak = 0;
    for (i = 0; i < NCH; i++) {
        ch_t *c = &W->ch[i];
        int comes = i < NREG ? chance(80) : 0;
        c->state = S_HOME;
        c->busy = c->play = 0;
        c->thirst = (uint8_t)(15 + wr(25));
        if (i < NREG) {
            c->arrive = (uint8_t)clampi(REG[i].arrive + wr(7) - 3, 0, 30);
            c->leave = (uint8_t)clampi(REG[i].leave + wr(7) - 3, c->arrive + 6, 41);
            if (!comes)
                c->state = S_GONE;               /* not tonight */
        }
    }
    {                                            /* the two first in: one with a tune, one to back them */
        int k, lead = -1, back = -1;
        for (i = 0; i < NREG; i++) {
            ch_t *c = &W->ch[i];
            if (c->state != S_HOME)
                continue;
            k = c->inst == I_FIDDLE || c->inst == I_WHISTLE || c->inst == I_HARP;
            if (k && (lead < 0 || c->arrive < W->ch[lead].arrive))
                lead = i;
        }
        for (i = 0; i < NREG; i++)
            if (i != lead && W->ch[i].state == S_HOME && (back < 0 || W->ch[i].arrive < W->ch[back].arrive))
                back = i;
        if (lead < 0) {                          /* (a night nobody with a tune would come: Brigid does) */
            lead = 3;
            W->ch[lead].state = S_HOME;
        }
        W->ch[lead].arrive = 0;
        if (back >= 0)
            W->ch[back].arrive = 0;
    }
    {                                            /* a visitor: one who has sat in before, back, or a new one, or nobody */
        ch_t *v = &W->ch[VISITOR];
        v->flags &= (uint8_t)~CF_WELCOME;
        if ((v->flags & CF_KNOWN) && chance(35)) {
            /* the same one, back */
        } else if (chance(35)) {
            v->name = (uint8_t)wr(NVNAME);
            v->inst = VISITOR_INST[wr(5)];
            v->flags = 0;
            v->led = v->nights = 0;
            v->fav[0] = (uint16_t)(wrnd() & 0xFFFFu);
            v->fav[1] = (uint16_t)(wrnd() & 0xFFFFu);
        } else {
            v->state = S_GONE;
            v->arrive = 99;
        }
        if (v->state != S_GONE) {
            v->arrive = (uint8_t)(10 + wr(16));
            v->leave = (uint8_t)clampi(v->arrive + 8 + wr(10), 0, 41);
        }
    }
    {
        char num[8], tmp[8];
        uint32_t n = W->night, k = 0, m = 0;
        do
            tmp[k++] = (char)('0' + n % 10u);
        while ((n /= 10u) && k < 6);
        while (k)
            num[m++] = tmp[--k];
        num[m] = 0;
        ev("Night %1. Hugh opens the doors.", num, 0, 0);
    }
    ws.dirty = ws.urgent = 1;                    /* (a new night: kept at the hourly save) */
}

static void arrive(int i)
{
    static const char *const IN[] = {"%1 comes in with the %2.", "%1 arrives, %2 under one arm.",
                                     "In comes %1 with the %2.", "%1 takes a seat, %2 out."};
    ch_t *c = &W->ch[i];
    c->state = S_SEAT;
    c->busy = 0;
    if (i == VISITOR) {                          /* a visitor: a chair is found, and in they play */
        if (c->flags & CF_KNOWN) {
            ev("%1 is back, %2 and all.", ch_name(i), EN_INST_NAME[c->inst], 0);
        } else {
            ev("A visitor sits in with a %2: %1.", ch_name(i), EN_INST_NAME[c->inst], 0);
            remember(M_WELCOME, VISITOR, c->name, 0);
        }
        c->flags |= CF_WELCOME | CF_KNOWN;
        c->nights++;
        return;
    }
    c->nights++;
    ev(PICK(IN), ch_name(i), EN_INST_NAME[c->inst], 0);
}

static void depart(int i, const char *why)
{
    ch_t *c = &W->ch[i];
    c->state = S_GONE;
    c->play = 0;
    ev(why, ch_name(i), EN_INST_NAME[c->inst], 0);
}

static void arrivals(void)                       /* whoever's due through the door */
{
    int i, slot = W->minute / 10;
    if (phase() >= PH_CLOSING)
        return;
    for (i = 0; i < NCH; i++)
        if (W->ch[i].state == S_HOME && slot >= W->ch[i].arrive)
            arrive(i);
}

static void minute_tick(void)                    /* a minute of the tavern's clock */
{
    int i;
    if (W->minute % 10 == 0) {
        arrivals();
        for (i = 0; i < NCH; i++)                /* talk is thirsty work */
            if (present(i))
                W->ch[i].thirst = (uint8_t)clampi(W->ch[i].thirst + 2, 0, 100);
    }
    if (W->minute == MIN_ORDERS)
        ev("Hugh calls last orders.", 0, 0, 0);
    if (W->minute == MIN_TIME) {
        ev("\"Time, please!\" calls Hugh.", 0, 0, 0);
        ws.set_left = 0;                         /* (a set queued: not now) */
        ws.queued = 0;
        if (ws.state == W_TUNE && ws.type != T_LAST)
            en_end();                            /* this one finishes its time through */
    }
    ws.dirty = 1;
}

/* between tunes: pints, the door */
static void gap_life(void)
{
    static const char *const PINT[] = {"%1 goes up for a pint.", "%1 is at the bar.", "%1 gets a round in.",
                                       "%1 wets the whistle."};
    static const char *const HOME[] = {"%1 heads home.", "%1 says goodnight.", "%1 packs up the %2 and goes.",
                                       "%1 slips away."};
    int i, ph = phase(), slot = W->minute / 10;
    for (i = 0; i < NCH; i++) {
        ch_t *c = &W->ch[i];
        if (c->state == S_BAR && !c->busy)
            c->state = S_SEAT;
        if (!present(i) || ph >= PH_CLOSING)
            continue;
        if (slot >= c->leave) {
            depart(i, PICK(HOME));
            continue;
        }
        if (c->state == S_SEAT && c->thirst > 65 && chance(60)) {   /* sits out the next tune at the bar */
            c->state = S_BAR;
            c->busy = 1;
            c->thirst = (uint8_t)clampi(c->thirst - 55, 0, 100);
            ev(PICK(PINT), ch_name(i), 0, 0);
        }
    }
}

/* -------------------------------------------------------------- the tunes --- */
static int pick_leader(int song)
{
    int i, best = -1, bw = -1;
    for (i = 0; i < NCH; i++) {
        int w;
        if (!free_to_play(i))
            continue;
        if (i == VISITOR && !(W->ch[i].flags & CF_WELCOME))
            continue;
        if (song ? !sings(i) : !(melodic(i) || W->ch[i].inst == I_HARP))
            continue;
        w = 30 + wr(40);
        if (i == ws.last_leader)
            w /= 3;                              /* turns go round */
        if (W->ch[i].inst == I_HARP && !song)
            w = w * 2 / 3;
        if (w > bw) {
            bw = w;
            best = i;
        }
    }
    return best;
}

static int pick_kind(int ph, int leader)
{
    static const uint8_t W4[4][4] = {{10, 20, 35, 35}, {45, 35, 12, 8}, {10, 25, 30, 35}, {0, 0, 100, 0}};
    int r, k, w[4], sum = 0;
    for (k = 0; k < 4; k++)
        w[k] = W4[ph < PH_CLOSING ? ph : 3][k];
    if (ph < PH_CLOSING)                         /* the preset's kinds (the closing waltz stays) */
        preset_kinds(w);
    for (k = 0; k < 4; k++)
        sum += w[k];
    r = wr(sum > 0 ? sum : 100);
    if (leader >= 0 && leader < NCH && W->ch[leader].inst == I_HARP)
        return chance(60) ? K_AIR : K_WALTZ;     /* the harp leads the slow ones */
    for (k = 0; k < 4; k++) {
        r -= w[k];
        if (r < 0)
            return k;
    }
    return K_REEL;
}

static int kind_tempo(int kind, int ph)          /* a kind's tempo, by the night (the preset's after) */
{
    int tempo;
    switch (kind) {
    case K_REEL: tempo = 74 + wr(10); break;
    case K_JIG: tempo = 84 + wr(12); break;
    case K_WALTZ: tempo = 92 + wr(14); break;
    default: tempo = 50 + wr(8); break;
    }
    if (ph == PH_BUSY && kind != K_AIR)
        tempo += 4;
    if (ph >= PH_LATE)
        tempo -= tempo / 12;
    return preset_tempo(tempo);
}

static void key_text(char *q, int n, int root, int mode)   /* "A Dorian" */
{
    fill(q, n, "%1 %2", EN_NOTE_NAME[root % 12], EN_MODE_NAME[mode % EN_NMODES], 0);
}

static void start_tune(int leader, int type)
{
    static const char *const START[] = {"%1 starts %2.", "%1 strikes up %2.", "%1 counts in %2.",
                                        "%1 launches into %2."};
    static const char *const FAVS[] = {"%1 plays an old favourite, %2.", "%1 starts %2, as ever."};
    static const char *const SET[] = {"%1 starts a set: %2.", "%1 leads off a set with %2."};
    int i, ph = phase();
    ch_t *L = leader >= 0 && leader < NCH ? &W->ch[leader] : 0;
    ws.type = (uint8_t)type;
    ws.leader = (int8_t)leader;
    ws.fav = 0;
    if (type == T_LAST || type == T_HUGH) {
        ws.kind = type == T_LAST ? K_WALTZ : K_AIR;
        ws.seed = (uint16_t)(type == T_LAST ? 0x2A17u + (W->night & 3u) : 0x1F00u + (W->night & 7u));
    } else if (type == T_LAMENT || type == T_SONG) {
        ws.kind = K_AIR;
        ws.seed = (uint16_t)(wrnd() & 0x3FFFu);
        if (type == T_SONG && chance(35))
            ws.kind = K_WALTZ;
    } else if (L && chance(50)) {                /* one of theirs */
        uint16_t f = L->fav[wr(2)];
        ws.seed = f & 0x3FFFu;
        ws.kind = (uint8_t)(f >> 14);
        ws.fav = 1;
        if (L->inst == I_HARP && (ws.kind == K_REEL || ws.kind == K_JIG))
            ws.kind = K_AIR;
        if (preset_kind_pct(ws.kind) < 60 && chance(70)) {   /* not tonight's style: something else */
            ws.kind = (uint8_t)pick_kind(ph, leader);
            ws.fav = 0;
        }
    } else {
        ws.kind = (uint8_t)pick_kind(ph, leader);
        ws.seed = (uint16_t)(wrnd() & 0x3FFFu);
    }
    tune_key(ws.seed, type, &ws.root, &ws.mode);
    if (ws.req_root >= 0 && type != T_HUGH) {
        ws.root = (uint8_t)ws.req_root;
        ws.req_root = -1;
    }
    ws.harp = L && L->inst == I_HARP && type != T_SONG;
    ws.passes = (uint8_t)(type == T_TUNE ? (ws.kind == K_AIR ? 1 + (ph == PH_BUSY) : ph == PH_BUSY ? 2 + chance(50) : 2)
                                         : type == T_LAST ? 2 : 1);
    /* a set: in the busy hours a reel or a jig may run straight into one or two more; a waltz into one more late */
    ws.set_left = 0;
    if (type == T_TUNE && !ws.harp && ph < PH_CLOSING) {
        if ((ws.kind == K_REEL || ws.kind == K_JIG) && ph == PH_BUSY && chance(50))
            ws.set_left = (uint8_t)(1 + chance(40));
        else if (ws.kind == K_WALTZ && ph != PH_BUSY && chance(25))
            ws.set_left = 1;
    }
    if (ws.set_left)
        ws.passes = 2;                           /* (in a set, each tune twice through) */
    ws.set_n = ws.set_left ? 1 : 0;
    ws.queued = 0;
    ws.tempo = (int16_t)kind_tempo(ws.kind, ph);
    tune_name(ws.seed, ws.kind, ws.tune_name, (int)sizeof ws.tune_name);
    if (type == T_LAST)
        fill(ws.tune_name, (int)sizeof ws.tune_name, "The Parting Glass", 0, 0, 0);
    for (i = 0; i < NCH; i++)
        W->ch[i].play = 0;
    if (L && type != T_SONG)
        L->play = 1;
    ws.state = W_TUNE;
    ws.joined = 0;
    ws.solo = -1;
    ws.trade_with = -1;
    ws.trade = 0;
    ws.tune_ms = plat_ms();
    if (type == T_SONG)                          /* a song: the harp and the box under it from the start */
        for (i = 0; i < NCH; i++)
            if (free_to_play(i) && (W->ch[i].inst == I_HARP || W->ch[i].inst == I_BOX) && i != leader)
                W->ch[i].play = 1;
    en_set(P_TEMPO, ws.tempo);
    en_set(P_ROOT, ws.root);
    en_set(P_MODE, ws.mode);
    band();
    en_tune(ws.seed, ws.kind, ws.passes, ws.harp);
    if (leader >= 0 && leader < NCH)
        ws.last_leader = (uint8_t)leader;
    {
        char q[44];
        fill(q, (int)sizeof q, "%1", ws.tune_name, 0, 0);
        switch (type) {
        case T_SONG: ev("%1 sings \"%2\".", ch_name(leader), q, 0); break;
        case T_LAMENT: ev("Hush. %1 plays a lament.", ch_name(leader), 0, 0); break;
        case T_LAST: ev("The last tune: everyone in.", 0, 0, 0); break;
        case T_HUGH: ev("Hugh plays one for the empty room.", 0, 0, 0); break;
        default: ev(ws.set_left ? PICK(SET) : ws.fav ? PICK(FAVS) : PICK(START), ch_name(leader), q, 0); break;
        }
    }
    ws.since_song++;
}

/* the set's next tune, chosen as the last time through begins its B part, queued to follow straight on: the same
 * kind (a jig set may turn to reels), another key, a touch quicker */
static void queue_next(void)
{
    int ph = phase(), kind = ws.kind, tempo;
    uint8_t root, mode;
    uint16_t seed = (uint16_t)(wrnd() & 0x3FFFu);
    if (kind == K_JIG && chance(40))
        kind = K_REEL;
    tune_key(seed, T_TUNE, &root, &mode);
    if (ws.req_root >= 0) {
        root = (uint8_t)ws.req_root;
        ws.req_root = -1;
    } else if (root == ws.root) {
        root = (uint8_t)((root + 5) % 12);       /* a new key: up a fourth */
    }
    tempo = kind_tempo(kind, ph);
    if (kind == ws.kind && tempo < ws.tempo + 2)
        tempo = ws.tempo + 2;                    /* the set lifts */
    tempo = clampi(tempo, 30, 140);
    ws.nx.seed = seed;
    ws.nx.kind = (uint8_t)kind;
    ws.nx.root = root;
    ws.nx.mode = mode;
    ws.nx.tempo = (int16_t)tempo;
    tune_name(seed, kind, ws.next_name, (int)sizeof ws.next_name);
    en_next(seed, kind, 2, root, mode, tempo);
    ws.queued = 1;
}

/* the set went straight on into the queued tune: it is now the tune; the band plays on as it was */
static void segue(void)
{
    char q[44], k[24];
    ws.seed = ws.nx.seed;
    ws.kind = ws.nx.kind;
    ws.root = ws.nx.root;
    ws.mode = ws.nx.mode;
    ws.tempo = ws.nx.tempo;
    ws.passes = 2;
    ws.harp = 0;
    ws.fav = 0;
    ws.queued = 0;
    ws.set_left--;
    ws.set_n++;
    ws.joined = 2;                               /* (everyone who was in, stays in) */
    ws.solo = -1;
    ws.trade = 9;
    ws.tune_ms = plat_ms();
    memcpy(ws.tune_name, ws.next_name, sizeof ws.tune_name);
    fill(q, (int)sizeof q, "%1", ws.tune_name, 0, 0);
    key_text(k, (int)sizeof k, ws.root, ws.mode);
    ev("On into %1, in %2.", q, k, 0);
    if (ws.set_n >= 3 && ws.leader >= 0 && ws.leader < NCH)
        remember(M_SET, ws.leader, 0, ws.set_n);
    band();
}

static void join(int stage)                      /* the repeat (1), the B part (2), a round (3): the others come in */
{
    int i, L = ws.leader, changed = 0;
    if (ws.type == T_LAMENT || ws.type == T_HUGH)
        return;                                  /* a lament, Hugh's air: alone */
    for (i = 0; i < NCH; i++) {
        ch_t *c = &W->ch[i];
        int p;
        if (c->play || !free_to_play(i) || i == L)
            continue;
        if (i == VISITOR && !(c->flags & CF_WELCOME))
            continue;
        if (ws.type == T_SONG && c->inst != I_LUTE && c->inst != I_BASS)
            continue;                            /* a song: the room listens */
        if (ws.kind == K_AIR && c->inst == I_DRUM)
            continue;
        if (ws.type == T_LAST || ws.all_in || stage == 3) {
            c->play = 1;
            changed = 1;
            continue;
        }
        p = (ws.kind == K_AIR ? 45 : 82) + (stage == 2 ? 10 : 0);
        if (stage == 1 && chance(35))
            continue;                            /* some wait for the B part */
        if (chance(p)) {
            c->play = 1;
            changed = 1;
        }
    }
    if (changed)
        band();
}

/* trading parts, on the middle time through of a tune played three times: the leader takes the A part alone, the
 * other melody instrument answers with the B, then all in for the last. Each change is sent in the last bar before
 * it, so it lands where the part begins. pos: bars into the tune */
static void trade_tick(int pos)
{
    int i, L = ws.leader;
    if (ws.trade == 0 && pos >= 31) {
        int pick = -1, n = 0;
        ws.trade = 9;
        if (ws.type != T_TUNE || ws.passes < 3 || L < 0 || L >= NCH || !melodic(L) || en_passes < 3)
            return;
        for (i = 0; i < NCH; i++)                /* who answers: another melody instrument, in the tune */
            if (i != L && W->ch[i].play && present(i) && melodic(i) && W->ch[i].inst != W->ch[L].inst &&
                wr(++n) == 0)
                pick = i;
        if (pick < 0)
            return;
        ws.trade_with = (int8_t)pick;
        ws.solo = (int8_t)W->ch[L].inst;
        ws.trade = 1;
        ws.trades++;
        if (L < NREG && pick < NREG)
            remember(M_TRADE, L, pick, 0);
        ev("%1 takes the A part alone; %2 will answer.", ch_name(L), ch_name(pick), 0);
        band();
    } else if (ws.trade == 1 && pos >= 47) {
        ws.trade = 2;
        ws.solo = (int8_t)W->ch[(int)ws.trade_with].inst;
        ev("%1 answers with the B part.", ch_name(ws.trade_with), 0, 0);
        band();
    } else if (ws.trade == 2 && pos >= 63) {
        ws.trade = 3;
        ws.solo = -1;
        ev("All in together, the last time through.", 0, 0, 0);
        band();
    }
}

static void tune_over(int segued)                /* the tune ended: how it went */
{
    static const char *const CHEER[] = {"A roar for %1.", "Feet stamp for %1.", "\"Grand!\" says someone.",
                                        "Glasses raised for %1."};
    static const char *const MILD[] = {"A murmur of approval.", "Polite applause.", "Someone whistles."};
    int i, n = players(), L = ws.leader;
    W->tunes++;
    if (L >= 0 && L < NCH)
        W->ch[L].led++;
    if (n > W->peak)
        W->peak = (uint8_t)n;
    for (i = 0; i < NCH; i++) {
        ch_t *c = &W->ch[i];
        int played = c->play || (i == L && ws.type == T_SONG);
        if (c->busy && c->state == S_BAR)
            c->busy--;
        if (!present(i))
            continue;
        c->thirst = (uint8_t)clampi(c->thirst + (played ? 9 + wr(8) + (c->inst == I_DRUM ? 8 : 0) : 4), 0, 100);
    }
    if (ws.type == T_LAMENT) {
        W->lament_done = 1;
    } else if (ws.type == T_TUNE && n >= 2 && !segued) {
        if (n >= 5 || ws.set_n >= 2)
            ev(PICK(CHEER), ws.set_n >= 2 ? "the set" : ws.tune_name, 0, 0);
        else if (chance(40))
            ev(PICK(MILD), 0, 0, 0);
    }
    if (n >= 7 && ws.type == T_TUNE)
        remember(M_GREAT, L >= 0 ? L : 0, 0, n);
    if (ws.type == T_TUNE && !ws.fav && L >= 0 && L < NREG && n >= 3 && chance(10)) {
        W->ch[L].fav[1] = W->ch[L].fav[0];
        W->ch[L].fav[0] = (uint16_t)(ws.seed | (uint16_t)ws.kind << 14);
        remember(M_NEWTUNE, L, 0, 0);
        ev("%1 has a new one now: %2.", ch_name(L), ws.tune_name, 0);
    }
    if (!segued) {
        for (i = 0; i < NCH; i++)
            W->ch[i].play = 0;
        ws.all_in = 0;
        ws.solo = -1;
        ws.set_left = ws.set_n = ws.queued = 0;
    }
    ws.dirty = 1;
}

static uint32_t gap_len(void)                    /* how long the room talks before the next tune */
{
    int ph = phase();
    return (uint32_t)(ph == PH_BUSY ? 6000 + wr(9000) : 12000 + wr(14000));
}

static void next_tune(void)                      /* the gap is over: who starts what */
{
    int ph = phase(), L, i;
    if (ph >= PH_CLOSING)
        return;
    gap_life();
    if (ph >= PH_LATE && !W->lament_done && chance(30)) {   /* late: a lament, alone */
        L = pick_leader(0);
        if (L >= 0) {
            remember(M_LAMENT, L, 0, 0);
            start_tune(L, T_LAMENT);
            return;
        }
    }
    if (ph >= PH_BUSY && ws.since_song >= 3 && chance(preset_songs())) {
        L = pick_leader(1);
        if (L >= 0) {
            W->songs++;
            ws.since_song = 0;
            if (W->songs == 3)
                remember(M_SONGS, L, 0, 0);
            start_tune(L, T_SONG);
            return;
        }
    }
    L = pick_leader(0);
    if (L < 0) {
        int any = 0;
        for (i = 0; i < NCH; i++)
            any |= present(i);
        if (any && !ws.waiting_said) {
            ev("The room waits for a tune-player.", 0, 0, 0);
            ws.waiting_said = 1;
        }
        ws.gap = 15000;
        return;
    }
    ws.waiting_said = 0;
    start_tune(L, T_TUNE);
}

static void closing(void)                        /* after "Time": the last tune, the goodbyes, Hugh's air */
{
    int i, any = 0;
    for (i = 0; i < NCH; i++)
        any |= present(i);
    if (W->minute < MIN_SHUT && any && ws.type != T_LAST) {
        int L;
        for (i = 0; i < NCH; i++)                /* everyone back to the table for it */
            if (present(i)) {
                W->ch[i].state = S_SEAT;
                W->ch[i].busy = 0;
            }
        L = pick_leader(0);
        if (L >= 0) {
            start_tune(L, T_LAST);
            return;
        }
    }
    if (any) {
        ev("\"Goodnight, all!\" The room empties.", 0, 0, 0);
        for (i = 0; i < NCH; i++)
            if (present(i))
                W->ch[i].state = S_GONE;
    }
    ws.state = W_CLOSED;
    ws.type = T_TUNE;
    ws.tune_name[0] = 0;
}

/* --------------------------------------------------------------- the API --- */
void world_bind(world_t *w) { W = w; }

void world_defaults(void)
{
    int i;
    memset(W, 0, sizeof *W);
    W->rng = 0x7A5E1u;
    for (i = 0; i < NREG; i++) {
        ch_t *c = &W->ch[i];
        c->inst = REG[i].inst;
        c->name = (uint8_t)i;
        c->fav[0] = (uint16_t)((wrnd() & 0x3FFFu) | (uint32_t)(c->inst == I_HARP ? K_AIR : i & 1 ? K_JIG : K_REEL) << 14);
        c->fav[1] = (uint16_t)((wrnd() & 0x3FFFu) | (uint32_t)(c->inst == I_HARP ? K_WALTZ : i & 2 ? K_WALTZ : K_REEL) << 14);
    }
    W->ch[VISITOR].state = S_GONE;
    W->ch[VISITOR].arrive = 99;
    W->night = 0;
    new_night();
    W->minute = 0;
}

int world_valid(void)                            /* a loaded world that makes sense */
{
    int i;
    if (W->minute > MIN_DAWN || W->nmem > NMEM)
        return 0;
    for (i = 0; i < NCH; i++)
        if (W->ch[i].state > S_GONE || W->ch[i].inst >= EN_NINST || W->ch[i].thirst > 100)
            return 0;
    return 1;
}

/* a 1.0 or 1.1 save (project.c): the night, who has come, the tunes, favourites and memories go on; 1.1's moods,
 * skill, drink and quarrels are cleared, a player mending a string sits down, a stranger left out is made welcome,
 * memories of mishaps (a string, a stand-off, a peace, a stranger frozen out) are dropped, and tonight's last events
 * (some of them 1.1's quarrels) are let go */
void world_migrate(void)
{
    int i, k = 0;
    for (i = 0; i < NCH; i++) {
        ch_t *c = &W->ch[i];
        c->x_mood = 0;
        c->x_skill = 0;
        c->x_drinks = 0;
        c->flags &= (uint8_t)~CF_X_ASKED;
        if (c->state == S_X_MEND) {
            c->state = S_SEAT;
            c->busy = 0;
        }
        c->play = 0;
    }
    if (present(VISITOR))
        W->ch[VISITOR].flags |= CF_WELCOME | CF_KNOWN;
    memset(W->x_aff, 0, sizeof W->x_aff);
    for (i = 0; i < W->nmem && i < NMEM; i++) {
        int t = W->mem[i].type;
        if (t == M_X_STRING || t == M_X_STANDOFF || t == M_X_PEACE || t == M_X_FROZEN || t >= M_N || t == M_NONE)
            continue;
        W->mem[k++] = W->mem[i];
    }
    for (i = k; i < NMEM; i++)
        memset(&W->mem[i], 0, sizeof W->mem[i]);
    W->nmem = (uint8_t)k;
    memset(W->log, 0, sizeof W->log);
    memset(W->log_min, 0, sizeof W->log_min);
}

void world_start(void)                           /* boot: the room as it was saved, between tunes */
{
    int i;
    memset(&ws, 0, sizeof ws);
    ws.leader = -1;
    ws.req_root = -1;
    ws.solo = ws.trade_with = -1;
    ws.prev = plat_ms();
    ws.done_seen = en_tunes_done;
    ws.segs_seen = en_segues;
    ws.gap = 2500;                               /* a moment, then a tune */
    ws.warp = 1;
    ws.since_song = 3;
    for (i = 0; i < NCH; i++)
        W->ch[i].play = 0;
    ws.state = W->minute >= MIN_SHUT ? W_CLOSED : W_GAP;
    arrivals();                                  /* (a fresh night: the early ones are in) */
    band();
}

void world_tick(void)
{
    uint32_t now = plat_ms(), dt = now - ws.prev, mm = minute_ms();
    ws.prev = now;
    if (!en_playing)
        return;                                  /* paused: the night waits */
    if (dt > 1000u)
        dt = 1000u;
    ws.acc += dt * ws.warp;
    while (ws.acc >= mm) {
        ws.acc -= mm;
        if (W->minute < MIN_DAWN)
            W->minute++;
        minute_tick();
    }
    if (ws.state == W_TUNE) {
        if (en_tunes_done != ws.done_seen) {     /* it ended */
            ws.done_seen = en_tunes_done;
            if (en_segues != ws.segs_seen) {     /* a set: straight on into the next */
                ws.segs_seen = en_segues;
                tune_over(1);
                segue();
                return;
            }
            tune_over(0);
            ws.state = W_GAP;
            ws.quiet_t = now;
            ws.gap = ws.called ? 1500u : gap_len();
            ws.called = 0;
            if (host_hold) {                     /* (the host's "hold") */
                ws.gap = host_hold;
                host_hold = 0;
            }
            if (ws.type == T_HUGH || ws.type == T_LAST)
                ws.gap = 6000;
            if (ws.type == T_HUGH)
                ws.state = W_CLOSED;
            ws.leader = -1;
            band();
            return;
        }
        if (now - ws.tune_ms > 3000u && en_sess == 0 && ws.joined < 9) {
            ws.joined = 9;                       /* (the engine never took it: let it end) */
            ws.done_seen = en_tunes_done - 1u;
        }
        if (en_sess == 1) {
            int pos = en_pass * 32 + en_part * 16 + en_rep * 8 + en_bar;   /* bars into the tune */
            if (!ws.joined && pos >= 7) {        /* in the A part's last bar: in from the repeat */
                ws.joined = 1;
                join(1);
            }
            if (ws.joined == 1 && pos >= 15) {   /* in the repeat's last bar: in from the B part */
                ws.joined = 2;
                join(2);
            }
            trade_tick(pos);
            if (ws.set_left && !ws.queued && en_pass + 1 >= en_passes && en_part == 1 && W->minute < MIN_ORDERS)
                queue_next();
        }
        return;
    }
    if (ws.gap > dt) {
        ws.gap -= dt;
        return;
    }
    ws.gap = 0;
    if (ws.state == W_CLOSED) {
        if (W->minute >= MIN_DAWN) {
            new_night();
            arrivals();
            ws.state = W_GAP;
            ws.gap = 4000;
            return;
        }
        if (W->minute >= MIN_SHUT + 2 && W->minute < MIN_SHUT + 8 && ws.type != T_HUGH) {
            start_tune(NCH, T_HUGH);             /* Hugh, alone, with his whistle */
            return;
        }
        ws.gap = 2000;
        return;
    }
    if (phase() >= PH_CLOSING || W->minute >= MIN_TIME) {
        closing();
        if (ws.state == W_CLOSED)
            ws.gap = 2000;
        return;
    }
    next_tune();
}

/* the player's part */
void world_round(void)                           /* REC: a round for the house: full glasses, everyone in */
{
    int i, n = 0;
    for (i = 0; i < NCH; i++)
        n += present(i);
    if (!n) {
        ev("You'd be drinking alone.", 0, 0, 0);
        return;
    }
    if (W->round_at && W->minute < W->round_at - 1 + 60) {
        ev("Hugh: \"Steady on. One an hour.\"", 0, 0, 0);
        return;
    }
    W->round_at = (uint16_t)(W->minute + 1);
    ws.rounds++;
    for (i = 0; i < NCH; i++)
        if (present(i)) {
            W->ch[i].thirst = 0;
            if (W->ch[i].state == S_BAR) {       /* back to the table with it */
                W->ch[i].state = S_SEAT;
                W->ch[i].busy = 0;
            }
        }
    ws.all_in = 1;
    remember(M_ROUND, 0, 0, (int)n);
    ev("You stand the house a round. Slainte! Everyone in.", 0, 0, 0);
    if (ws.state == W_TUNE && en_sess == 1)
        join(3);                                 /* in from the next phrase */
}

void world_call(void)                            /* EDIT: call for a tune */
{
    if (ws.state == W_CLOSED) {
        ev("The doors are shut. Hugh shakes his head.", 0, 0, 0);
    } else if (ws.state == W_GAP) {
        ws.gap = 0;
        ev("You call for a tune.", 0, 0, 0);
    } else {
        ev("\"Another after this!\" you call.", 0, 0, 0);
        ws.called = 1;
    }
}

void world_key(int pc)                           /* a key: the next tune in it */
{
    ws.req_root = (int8_t)(pc % 12);
    ev("You ask for one in %1.", EN_NOTE_NAME[pc % 12], 0, 0);
}

/* for the screen */
int world_state(void) { return ws.state; }
int world_leader(void) { return ws.leader; }
int world_type(void) { return ws.type; }
const char *world_tune(void) { return ws.state == W_TUNE ? ws.tune_name : ""; }
int world_kind(void) { return ws.kind; }
int world_root(void) { return ws.root; }
int world_mode(void) { return ws.mode; }
int world_phase(void) { return phase(); }
int world_set(void) { return ws.state == W_TUNE ? ws.set_n : 0; }
int world_solo(void)
{
    if (ws.state != W_TUNE)
        return -1;
    return ws.trade == 1 ? ws.leader : ws.trade == 2 ? ws.trade_with : -1;
}
int world_quiet(uint32_t now)                   /* a quiet moment for the flash: between tunes, the tails gone */
{
    return ws.state != W_TUNE && en_sess == 0 && now - ws.quiet_t > 2500u && en_level < 0.03f;
}
void world_warp(int n) { ws.warp = (uint16_t)(n < 1 ? 1 : n); }
int ch_hair(int i) { return i < NREG ? REG[i].hair : 8 + W->ch[i].name % 3; }
int ch_shirt(int i) { return i < NREG ? REG[i].shirt : 8 + W->ch[i].name % 4; }
int ch_skin(int i) { return i < NREG ? REG[i].skin : W->ch[i].name % 3; }
int ch_sings(int i) { return sings(i); }

#ifdef PLAT_HOST
void world_host_hold(int ms) { if (ws.state == W_GAP) ws.gap = (uint32_t)ms; else host_hold = (uint32_t)ms; }
#endif
