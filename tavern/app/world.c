/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen */
/* The tavern (world.h): the regulars, the visitor, Hugh, the night. Part of the unity build, after project.c and
 * before ui.c; the main loop's, never the audio interrupt's. It tells the engine (dsp/amb.h) what to play and who
 * plays it, and watches where the tune is (en_sess, en_part, en_rep) to bring players in on the repeat.
 *
 * The night runs 18:00 to 01:00 on its own clock (the length a night takes is a setting). Early, a few come in and
 * play slow airs and waltzes; the middle is busy, reels and jigs, most of the room in on them; late, airs again, a
 * lament perhaps, people drifting home; then last orders, "Time, please!", one last waltz for everyone, and Hugh
 * plays an air for the empty room before the next night begins. A leader starts each tune alone; the others come in
 * on the repeat (or on the B part), unless they won't play under that leader. Between tunes people go up for a
 * pint, mend strings, arrive and leave. */

/* the regulars: name, instrument, skill, temper (the mood they come back to), warmth (to strangers), whether they
 * sing, when they usually come and go (10-minute slots from 18:00), and how they look (hair, shirt, skin) */
static const struct {
    const char *name;
    uint8_t inst, skill;
    int8_t temper;
    uint8_t warmth, sings, arrive, leave, hair, shirt, skin;
} REG[NREG] = {
    {"Old Tom", I_FIDDLE, 88, -15, 20, 0, 6, 39, 0, 0, 0},
    {"Rosie", I_FIDDLE, 74, 20, 55, 0, 12, 41, 1, 1, 1},
    {"Maeve", I_WHISTLE, 76, 10, 65, 1, 3, 33, 2, 2, 1},
    {"Brigid", I_HARP, 82, 15, 70, 0, 0, 36, 3, 3, 0},
    {"Declan", I_LUTE, 55, 30, 80, 1, 15, 42, 4, 4, 2},
    {"Nora", I_BOX, 70, 0, 40, 0, 9, 30, 5, 5, 0},
    {"Finn", I_BASS, 62, 10, 60, 1, 6, 37, 6, 6, 2},
    {"Seamus", I_DRUM, 50, 25, 50, 0, 12, 41, 7, 7, 1},
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
    uint8_t kind, root, mode, passes, harp, type, joined, refused, fav;
    uint16_t seed;
    uint32_t done_seen;                          /* en_tunes_done as last seen */
    int8_t req_root;                             /* a key asked for (-1 none) */
    uint8_t dirty, urgent;                       /* to save; something memorable (ui.c keep_due) */
    uint16_t warp;                               /* the host's "timewarp": minutes go by N times as fast */
    uint8_t string_bar, string_who;              /* a string to break at this bar (+1), and whose */
    uint8_t waiting_said, last_leader, since_song, called;   /* (called: "another after this")*/
    uint32_t tune_ms, quiet_t;                   /* when the tune began; when the room went quiet */
    char tune_name[40];
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
static int temper(int i) { return i < NREG ? REG[i].temper : 10; }
static int warmth(int i) { return i < NREG ? REG[i].warmth : 50; }
static int present(int i) { return W->ch[i].state == S_SEAT || W->ch[i].state == S_BAR || W->ch[i].state == S_MEND; }
static int free_to_play(int i) { return W->ch[i].state == S_SEAT && !W->ch[i].busy; }
static int melodic(int i) { return W->ch[i].inst == I_FIDDLE || W->ch[i].inst == I_WHISTLE; }
static int skill_now(int i)                      /* drink takes the edge off; a good mood puts some back */
{
    const ch_t *c = &W->ch[i];
    return clampi((int)c->skill - (c->drinks > 2 ? (c->drinks - 2) * 7 : 0) + c->mood / 10, 5, 100);
}
static void mood(int i, int d) { W->ch[i].mood = (int8_t)clampi(W->ch[i].mood + d, -100, 100); }
static void aff(int a, int b, int d)
{
    if (a == b || a >= NCH || b >= NCH)
        return;
    W->aff[a][b] = W->aff[b][a] = (int8_t)clampi(W->aff[a][b] + d, -100, 100);
}

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
}
static const char *pick(const char *const *a, int n) { return a[wr(n)]; }
#define PICK(a) pick(a, (int)(sizeof a / sizeof a[0]))

static void remember(int type, int a, int b, int n)
{
    int i;
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
    const char *a = m->a < NREG ? REG[m->a].name : m->a == NCH ? LANDLORD : VISITOR_NAME[m->b % NVNAME];
    const char *b = m->b < NREG ? REG[m->b].name : "";
    char num[6];
    num[0] = (char)('0' + m->n / 10 % 10);
    num[1] = (char)('0' + m->n % 10);
    num[2] = 0;
    switch (m->type) {
    case M_LAMENT: fill(out, n, "%1 played a lament", a, 0, 0); break;
    case M_SONGS: fill(out, n, "a singers' night, %1 began it", a, 0, 0); break;
    case M_STRING: fill(out, n, "%1 broke a string mid-tune", a, 0, 0); break;
    case M_STANDOFF: fill(out, n, "%1 and %2 would not play", a, b, 0); break;
    case M_PEACE: fill(out, n, "%1 and %2 made it up", a, b, 0); break;
    case M_WELCOME: fill(out, n, "%1 the stranger, made welcome", a, 0, 0); break;
    case M_FROZEN: fill(out, n, "%1 the stranger, frozen out", a, 0, 0); break;
    case M_GREAT: fill(out, n, "a great session: %1 in one tune", num[0] == '0' ? num + 1 : num, 0, 0); break;
    case M_NEWTUNE: fill(out, n, "%1 brought a new tune", a, 0, 0); break;
    case M_ROUND: fill(out, n, "you stood the house a round", 0, 0, 0); break;
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
    static const uint8_t MODES[10] = {MODE_IONIAN, MODE_IONIAN, MODE_IONIAN, MODE_DORIAN, MODE_DORIAN,
                                      MODE_DORIAN, MODE_MIXOLYDIAN, MODE_MIXOLYDIAN, MODE_AEOLIAN, MODE_AEOLIAN};
    static const uint8_t KEYS[4][4] = {{2, 7, 9, 0}, {2, 4, 9, 11}, {2, 9, 7, 2}, {4, 9, 11, 2}};
    uint32_t h = (uint32_t)seed * 40503u;
    int m = MODES[(h >> 3) % 10u];
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

static void band(void)                           /* who plays now, to the engine */
{
    int i, k, li = lead_inst();
    for (k = 0; k < EN_NINST; k++) {
        int n = 0, sk = 0, level = 0;
        for (i = 0; i < NCH; i++)
            if (W->ch[i].play && W->ch[i].inst == k && present(i)) {
                n++;
                sk += skill_now(i);
            }
        if (ws.state == W_TUNE && k == I_VOICE && ws.type == T_SONG && ws.leader >= 0 && ws.leader < NCH) {
            n = 1;
            sk = skill_now(ws.leader);
        }
        if (ws.state == W_TUNE && k == I_WHISTLE && ws.leader == NCH) {   /* Hugh, after hours */
            n++;
            sk += 60;
        }
        if (n) {
            level = 66 + sk / n / 6 + (n - 1) * 10;
            if ((ws.type == T_SONG || ws.kind == K_AIR) && k != li)
                level = level * 2 / 3;            /* under a song or an air: softly */
        }
        en_inst(k, level, n ? sk / n : 50);
    }
}

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
        int comes = i < NREG ? chance(80 + c->mood / 8) : 0;
        c->state = S_HOME;
        c->busy = c->play = 0;
        c->drinks = 0;
        c->thirst = (uint8_t)(15 + wr(25));
        c->flags &= (uint8_t)~CF_ASKED;
        if (i < NREG) {
            c->mood = (int8_t)clampi(c->mood + (temper(i) - c->mood) / 3, -100, 100);
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
    {                                            /* a visitor: an old friend back, or a stranger, or nobody */
        ch_t *v = &W->ch[VISITOR];
        if ((v->flags & CF_KNOWN) && chance(35)) {
            v->flags |= CF_WELCOME;
        } else if (chance(35)) {
            v->name = (uint8_t)wr(NVNAME);
            v->inst = VISITOR_INST[wr(5)];
            v->skill = (uint8_t)(40 + wr(40));
            v->mood = (int8_t)(wr(40));
            v->flags = 0;
            v->led = v->nights = 0;
            v->fav[0] = (uint16_t)(wrnd() & 0xFFFFu);
            v->fav[1] = (uint16_t)(wrnd() & 0xFFFFu);
            for (i = 0; i < NCH; i++)
                W->aff[VISITOR][i] = W->aff[i][VISITOR] = 0;
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
    static const char *const GLUM[] = {"%1 comes in, scowling.", "%1 slumps in by the fire.",
                                       "%1 arrives with a face like thunder."};
    ch_t *c = &W->ch[i];
    c->state = S_SEAT;
    c->busy = 0;
    if (i < NREG)
        c->nights++;
    if (i == VISITOR) {
        if (c->flags & CF_WELCOME)
            ev("%1 is back, %2 and all.", ch_name(i), EN_INST_NAME[c->inst], 0);
        else
            ev("A stranger comes in with a %2: %1.", ch_name(i), EN_INST_NAME[c->inst], 0);
        return;
    }
    ev(c->mood < -40 ? PICK(GLUM) : PICK(IN), ch_name(i), EN_INST_NAME[c->inst], 0);
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
    if (W->minute % 60 == 0)                     /* an hour: tempers settle */
        for (i = 0; i < NCH; i++)
            mood(i, (temper(i) - W->ch[i].mood) / 6);
    if (W->minute == MIN_ORDERS)
        ev("Hugh calls last orders.", 0, 0, 0);
    if (W->minute == MIN_TIME) {
        ev("\"Time, please!\" calls Hugh.", 0, 0, 0);
        if (ws.state == W_TUNE && ws.type != T_LAST)
            en_end();                            /* this one finishes its pass */
    }
    ws.dirty = 1;
}

/* between tunes: pints, the door, strangers */
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
        if (c->state == S_MEND && !c->busy) {
            c->state = S_SEAT;
            ev("%1 has a new string on.", ch_name(i), 0, 0);
        }
        if (!present(i) || ph >= PH_CLOSING)
            continue;
        if (slot >= c->leave || c->drinks >= 6) {
            static const char *const FULL[] = {"%1 has had enough and goes.", "%1 is poured out the door."};
            depart(i, c->drinks >= 6 ? PICK(FULL) : PICK(HOME));
            continue;
        }
        if (c->mood < -75 && chance(30)) {
            depart(i, "%1 storms out into the night.");
            continue;
        }
        if (c->state == S_SEAT && c->thirst > 65 && chance(60)) {
            c->state = S_BAR;
            c->busy = 1;
            c->thirst = (uint8_t)clampi(c->thirst - 55, 0, 100);
            c->drinks++;
            mood(i, 5);
            ev(PICK(PINT), ch_name(i), 0, 0);
        }
    }
    {   /* a stranger asks to sit in: the room decides */
        ch_t *v = &W->ch[VISITOR];
        if (v->state == S_SEAT && !(v->flags & CF_ASKED)) {
            int n = 0, s = 0, host = -1;
            v->flags |= CF_ASKED;
            for (i = 0; i < NREG; i++)
                if (present(i)) {
                    n++;
                    s += warmth(i) + W->ch[i].mood / 2;
                    if (host < 0 || warmth(i) + W->ch[i].mood > warmth(host) + W->ch[host].mood)
                        host = i;
                }
            if (!n)
                return;
            s = s / n + (v->flags & CF_KNOWN ? 40 : 0) + wr(60) - 30;
            if (s > 40) {
                if (!(v->flags & CF_KNOWN))
                    remember(M_WELCOME, VISITOR, v->name, 0);
                v->flags |= CF_WELCOME | CF_KNOWN;
                aff(VISITOR, host, 25);
                ev("%1 shifts up to make room for %2.", ch_name(host), ch_name(VISITOR), 0);
            } else {
                v->flags &= (uint8_t)~CF_WELCOME;
                v->leave = (uint8_t)(slot + 2);
                remember(M_FROZEN, VISITOR, v->name, 0);
                ev("Nobody shifts up. %1 sips a pint alone.", ch_name(VISITOR), 0, 0);
            }
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
        w = 20 + skill_now(i) / 3 + W->ch[i].mood / 4 + wr(40);
        if (i == ws.last_leader)
            w /= 3;
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
    int r = wr(100), k;
    const uint8_t *w = W4[ph < PH_CLOSING ? ph : 3];
    if (leader >= 0 && leader < NCH && W->ch[leader].inst == I_HARP)
        return chance(60) ? K_AIR : K_WALTZ;     /* the harp leads the slow ones */
    if (leader >= 0 && leader < NCH && W->ch[leader].mood < -30 && chance(50))
        return K_AIR;
    for (k = 0; k < 4; k++) {
        r -= w[k];
        if (r < 0)
            return k;
    }
    return K_REEL;
}

static void start_tune(int leader, int type)
{
    static const char *const START[] = {"%1 starts %2.", "%1 strikes up %2.", "%1 counts in %2.",
                                        "%1 launches into %2."};
    static const char *const FAVS[] = {"%1 plays an old favourite, %2.", "%1 starts %2, as ever."};
    int i, ph = phase(), tempo;
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
    switch (ws.kind) {
    case K_REEL: tempo = 74 + wr(10); break;
    case K_JIG: tempo = 84 + wr(12); break;
    case K_WALTZ: tempo = 92 + wr(14); break;
    default: tempo = 50 + wr(8); break;
    }
    if (ph == PH_BUSY && ws.kind != K_AIR)
        tempo += 4;
    if (ph >= PH_LATE)
        tempo -= tempo / 12;
    if (L)
        tempo += (skill_now(leader) - 60) / 10;
    tune_name(ws.seed, ws.kind, ws.tune_name, (int)sizeof ws.tune_name);
    if (type == T_LAST)
        fill(ws.tune_name, (int)sizeof ws.tune_name, "The Parting Glass", 0, 0, 0);
    for (i = 0; i < NCH; i++)
        W->ch[i].play = 0;
    if (L && type != T_SONG)
        L->play = 1;
    ws.state = W_TUNE;
    ws.joined = 0;
    ws.refused = 0;
    ws.string_bar = 0;
    ws.tune_ms = plat_ms();
    if (type == T_SONG)                          /* a song: the harp and the box under it from the start */
        for (i = 0; i < NCH; i++)
            if (free_to_play(i) && (W->ch[i].inst == I_HARP || W->ch[i].inst == I_BOX) && i != leader)
                W->ch[i].play = 1;
    if (type == T_TUNE && L && (L->inst == I_FIDDLE || L->inst == I_LUTE) && chance(6 + (L->drinks > 2 ? 5 : 0))) {
        ws.string_bar = (uint8_t)(3 + wr(10));   /* a string will go */
        ws.string_who = (uint8_t)leader;
    }
    en_set(P_TEMPO, tempo);
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
        default: ev(ws.fav ? PICK(FAVS) : PICK(START), ch_name(leader), q, 0); break;
        }
    }
    ws.since_song++;
}

static void join(int stage)                      /* the repeat (1) or the B part (2): the others come in */
{
    static const char *const REFUSE[] = {"%1 won't play under %2.", "%1 folds arms while %2 plays.",
                                         "%1 sits this one out: %2 again."};
    int i, L = ws.leader, changed = 0;
    if (ws.type == T_LAMENT || ws.type == T_HUGH)
        return;                                  /* a lament, Hugh's air: alone */
    for (i = 0; i < NCH; i++) {
        ch_t *c = &W->ch[i];
        int p, a = L >= 0 && L < NCH ? W->aff[i][L] : 0;
        if (c->play || !free_to_play(i) || i == L)
            continue;
        if (i == VISITOR && !(c->flags & CF_WELCOME))
            continue;
        if (ws.type == T_SONG && c->inst != I_LUTE && c->inst != I_BASS)
            continue;                            /* a song: the room listens */
        if (ws.kind == K_AIR && c->inst == I_DRUM)
            continue;
        if (ws.type == T_LAST) {
            c->play = 1;
            changed = 1;
            continue;
        }
        if (a < -40 && !ws.refused && chance(75)) {
            ws.refused = 1;
            mood(i, -4);
            aff(i, L, -2);
            ev(PICK(REFUSE), ch_name(i), ch_name(L), 0);
            continue;
        }
        if (a < -40)
            continue;
        p = (ws.kind == K_AIR ? 45 : 82) + c->mood / 5 + a / 5 + (stage == 2 ? 10 : 0);
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

static void tune_over(void)                      /* the tune ended: how it went, and what it did to them */
{
    static const char *const CHEER[] = {"A roar for %1.", "Feet stamp for %1.", "\"Grand!\" says someone.",
                                        "Glasses raised for %1."};
    static const char *const MILD[] = {"A murmur of approval.", "Polite applause.", "Someone whistles."};
    int i, j, n = players(), q = 0, L = ws.leader;
    for (i = 0; i < NCH; i++)
        if (W->ch[i].play)
            q += skill_now(i) + W->ch[i].mood / 4;
    q = n ? q / n : 50;
    W->tunes++;
    if (L >= 0 && L < NCH)
        W->ch[L].led++;
    if (n > W->peak)
        W->peak = (uint8_t)n;
    for (i = 0; i < NCH; i++) {
        ch_t *c = &W->ch[i];
        int played = c->play || (i == L && ws.type == T_SONG);
        if (c->busy && (c->state == S_BAR || c->state == S_MEND))
            c->busy--;
        if (!present(i))
            continue;
        if (played) {
            c->thirst = (uint8_t)clampi(c->thirst + 9 + wr(8) + (c->inst == I_DRUM ? 8 : 0), 0, 100);
            mood(i, (q - 55) / 8 + 2);
            if (chance(3) && c->skill < 95)
                c->skill++;
            for (j = 0; j < NCH; j++)
                if (j != i && W->ch[j].play) {
                    int before = W->aff[i][j];
                    aff(i, j, before < -40 ? 2 : 1);
                    if (before < -10 && W->aff[i][j] >= -10 && i < j) {
                        remember(M_PEACE, i, j, 0);
                        ev("%1 and %2 shake hands at last.", ch_name(i), ch_name(j), 0);
                    }
                }
        } else {
            c->thirst = (uint8_t)clampi(c->thirst + 4, 0, 100);
        }
    }
    if (ws.type == T_LAMENT) {
        for (i = 0; i < NCH; i++)
            if (present(i) && i != L) {
                aff(i, L, 3);
                mood(i, -3);
            }
        mood(L, 15);                             /* it did them good */
        W->lament_done = 1;
    } else if (ws.type == T_TUNE && n >= 2) {
        if (n >= 5 && q > 60)
            ev(PICK(CHEER), ws.tune_name, 0, 0);
        else if (chance(40))
            ev(PICK(MILD), 0, 0, 0);
    }
    if (n >= 7 && ws.type == T_TUNE)
        remember(M_GREAT, L >= 0 ? L : 0, 0, n);
    if (ws.type == T_TUNE && !ws.fav && L >= 0 && L < NREG && q > 62 && chance(10)) {
        W->ch[L].fav[1] = W->ch[L].fav[0];
        W->ch[L].fav[0] = (uint16_t)(ws.seed | (uint16_t)ws.kind << 14);
        remember(M_NEWTUNE, L, 0, 0);
        ev("%1 has a new one now: %2.", ch_name(L), ws.tune_name, 0);
    }
    for (i = 0; i < NCH; i++)
        W->ch[i].play = 0;
    ws.dirty = 1;
}

static uint32_t gap_len(void)                    /* how long the room talks before the next tune */
{
    int ph = phase();
    return (uint32_t)(ph == PH_BUSY ? 6000 + wr(9000) : 12000 + wr(14000));
}

static void next_tune(void)                      /* the gap is over: who starts what */
{
    int ph = phase(), L, i, rival = -1;
    if (ph >= PH_CLOSING)
        return;
    gap_life();
    if (ph >= PH_LATE && !W->lament_done) {      /* late: a lament, from the lowest heart */
        int low = -1;
        for (i = 0; i < NCH; i++)
            if (free_to_play(i) && (melodic(i) || W->ch[i].inst == I_HARP) &&
                (low < 0 || W->ch[i].mood < W->ch[low].mood))
                low = i;
        if (low >= 0 && (W->ch[low].mood < -20 || chance(20))) {
            remember(M_LAMENT, low, 0, 0);
            start_tune(low, T_LAMENT);
            return;
        }
    }
    if (ph >= PH_BUSY && ws.since_song >= 3 && chance(20)) {
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
    for (i = 0; i < NCH; i++)                    /* a rival with a tune of their own? */
        if (i != L && free_to_play(i) && melodic(i) && W->aff[i][L] < -50)
            rival = i;
    if (rival >= 0 && chance(35)) {
        int win = W->ch[L].skill + wr(30) >= W->ch[rival].skill + wr(30) ? L : rival, lose = win == L ? rival : L;
        ev("%1 and %2 both start at once.", ch_name(L), ch_name(rival), 0);
        ev("%1 plays on; %2 stalks to the bar.", ch_name(win), ch_name(lose), 0);
        remember(M_STANDOFF, win, lose, 0);
        W->ch[lose].state = S_BAR;
        W->ch[lose].busy = 1;
        mood(lose, -12);
        aff(win, lose, -4);
        L = win;
    }
    start_tune(L, T_TUNE);
}

static void closing(void)                        /* after "Time": the last tune, the goodbyes, Hugh's air */
{
    int i, any = 0;
    for (i = 0; i < NCH; i++)
        any |= present(i);
    if (W->minute < MIN_SHUT && any && ws.type != T_LAST) {
        int L = pick_leader(0);
        for (i = 0; i < NCH; i++)                /* everyone back to the table for it */
            if (present(i)) {
                W->ch[i].state = S_SEAT;
                W->ch[i].busy = 0;
            }
        if (L < 0)
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
    int i, j;
    memset(W, 0, sizeof *W);
    W->rng = 0x7A5E1u;
    for (i = 0; i < NREG; i++) {
        ch_t *c = &W->ch[i];
        c->inst = REG[i].inst;
        c->skill = REG[i].skill;
        c->mood = REG[i].temper;
        c->name = (uint8_t)i;
        c->fav[0] = (uint16_t)((wrnd() & 0x3FFFu) | (uint32_t)(c->inst == I_HARP ? K_AIR : i & 1 ? K_JIG : K_REEL) << 14);
        c->fav[1] = (uint16_t)((wrnd() & 0x3FFFu) | (uint32_t)(c->inst == I_HARP ? K_WALTZ : i & 2 ? K_WALTZ : K_REEL) << 14);
    }
    for (i = 0; i < NCH; i++)
        for (j = 0; j < NCH; j++)
            W->aff[i][j] = (int8_t)(i == j ? 0 : 5 + (int)((i * 7 + j * 7) % 11));
    aff(0, 1, -75);                              /* Old Tom and Rosie: an old quarrel over a tune */
    aff(0, 3, 35);
    aff(1, 4, 30);
    aff(2, 3, 30);
    aff(5, 7, -45);                              /* Nora can't abide the drum */
    aff(6, 7, 25);
    aff(4, 7, 35);
    aff(2, 5, 20);
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
        if (W->ch[i].state > S_GONE || W->ch[i].inst >= EN_NINST || W->ch[i].skill > 100 || W->ch[i].thirst > 100)
            return 0;
    return 1;
}

void world_start(void)                           /* boot: the room as it was saved, between tunes */
{
    int i;
    memset(&ws, 0, sizeof ws);
    ws.leader = -1;
    ws.req_root = -1;
    ws.prev = plat_ms();
    ws.done_seen = en_tunes_done;
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
            tune_over();
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
        if (!ws.joined && en_sess == 1 && (en_rep == 1 || en_part == 1)) {
            ws.joined = 1;
            join(1);
        }
        if (ws.joined == 1 && en_sess == 1 && en_part == 1) {
            ws.joined = 2;
            join(2);
        }
        if (ws.string_bar && en_sess == 1 && en_part * 16 + en_rep * 8 + en_bar >= ws.string_bar) {
            int who = ws.string_who;             /* snap */
            static const char *const SNAP[] = {"Snap! %1 breaks a string.", "Twang! A string goes on %1's %2."};
            ws.string_bar = 0;
            if (W->ch[who].play) {
                W->ch[who].play = 0;
                W->ch[who].state = S_MEND;
                W->ch[who].busy = 2;
                mood(who, -10);
                en_twang(W->ch[who].inst);
                remember(M_STRING, who, 0, 0);
                ev(PICK(SNAP), ch_name(who), EN_INST_NAME[W->ch[who].inst], 0);
                if (who == ws.leader) {          /* someone takes the tune on, if they can */
                    int i;
                    for (i = 0; i < NCH; i++)
                        if (W->ch[i].play && melodic(i)) {
                            ev("%1 carries the tune on.", ch_name(i), 0, 0);
                            break;
                        }
                    if (i == NCH)
                        en_end();
                }
                band();
            }
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
void world_round(void)                           /* REC: a round for the house */
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
    for (i = 0; i < NCH; i++)
        if (present(i)) {
            int j;
            W->ch[i].thirst = (uint8_t)clampi(W->ch[i].thirst - 50, 0, 100);
            W->ch[i].drinks++;
            mood(i, 12);
            for (j = i + 1; j < NCH; j++)
                if (present(j))
                    aff(i, j, 3);
        }
    remember(M_ROUND, 0, 0, (int)n);
    ev("You stand the house a round. Slainte!", 0, 0, 0);
}

void world_call(void)                            /* EDIT: call for a tune */
{
    if (ws.state == W_CLOSED) {
        ev("The doors are shut. Hugh shakes his head.", 0, 0, 0);
    } else if (ws.state == W_GAP) {
        ws.gap = 0;
        ev("You call for a tune.", 0, 0, 0);
    } else {
        int i;
        ev("\"Another after this!\" you call.", 0, 0, 0);
        ws.called = 1;
        for (i = 0; i < NCH; i++)
            if (W->ch[i].play)
                mood(i, 3);
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
int world_quiet(uint32_t now)                   /* a quiet moment for the flash: between tunes, the tails gone */
{
    return ws.state != W_TUNE && en_sess == 0 && now - ws.quiet_t > 2500u && en_level < 0.03f;
}
void world_warp(int n) { ws.warp = (uint16_t)(n < 1 ? 1 : n); }
int ch_hair(int i) { return i < NREG ? REG[i].hair : 8 + W->ch[i].name % 3; }
int ch_shirt(int i) { return i < NREG ? REG[i].shirt : 8 + W->ch[i].name % 4; }
int ch_skin(int i) { return i < NREG ? REG[i].skin : W->ch[i].name % 3; }
int ch_sings(int i) { return sings(i); }
int ch_rival(int i)                              /* the one they like least, if they truly don't */
{
    int j, r = -1;
    for (j = 0; j < NCH; j++)
        if (j != i && W->aff[i][j] < -40 && (r < 0 || W->aff[i][j] < W->aff[i][r]))
            r = j;
    return r;
}
int ch_friend(int i)
{
    int j, r = -1;
    for (j = 0; j < NCH; j++)
        if (j != i && W->aff[i][j] > 30 && (r < 0 || W->aff[i][j] > W->aff[i][r]))
            r = j;
    return r;
}

#ifdef PLAT_HOST
void world_host_hold(int ms) { if (ws.state == W_GAP) ws.gap = (uint32_t)ms; else host_hold = (uint32_t)ms; }
#endif
