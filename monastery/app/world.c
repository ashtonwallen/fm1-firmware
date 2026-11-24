/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen (MONASTERY), built on AMB-1 by Charles Vestal */
/* MONASTERY's world: a small community of monks singing the hours in a stone chapel. Part of the unity build (after
 * project.c, before ui.c).
 *
 * The day: eight offices (Matins, Lauds, Prime, Terce, Sext, None, Vespers, Compline), compressed into Day length
 * (2 hours by default), each with its own mode, final, pace, mood and number of singers. An office: the bell tolls,
 * the abbot intones, all respond; then psalms: a cantor sings the antiphon, the two sides of the choir alternate the
 * verses (each side's last note held while the other comes in), all sing the antiphon again; at the end an Amen, then
 * a quiet interlude (after Compline: the night). A drone (the bass, as an ison) holds the final under the chant.
 *
 * The chant: psalm tones (an intonation up to the reciting tone, recitation, a mediant cadence, recitation, a fall to
 * the final), antiphons as stepwise melismas, all modal; each monk sings the line in his octave (a bass an octave
 * under), the full offices' Amen in organum (a fifth under).
 *
 * The brothers (each a voice of the engine's choir while he sings: his range, his vowel colour, his tuning):
 *   Benedict   the abbot: intones, leads; frowns or nods at the novice
 *   Anselm     the novice: sometimes comes in early or misses his entry; practises in the interludes; improves day
 *              by day, and in time takes his vows
 *   Ambrose    the old brother: drifts flat through a phrase until the abbot gives him the note; falls ill now and
 *              then (the infirmary), and comes back
 *   Gregory    and Bernard: argue over the pitch (a brief clash, a step apart, until one gives way); now and then
 *   Bernard    they make peace, for a while
 *   Columba    falls asleep at Matins, and wakes with a start
 *   Bede       a new novice who comes to the gate after some days
 * The brothers, their histories and the chronicle are kept (project_t.world); the world's clock runs while the choir
 * sings (PLAY: it rests, the clock stops). */

static void itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
}

/* ---- the brothers */
enum { MK_ABBOT, MK_NOVICE, MK_ELDER, MK_GREGORY, MK_BERNARD, MK_SLEEPER, MK_NEW };
static const char *const MONK_NAME[NMONK] = {"Benedict", "Anselm", "Ambrose", "Gregory", "Bernard", "Columba", "Bede"};
enum { RG_BASS, RG_BARI, RG_TENOR };
static const uint8_t MONK_RANGE[NMONK] = {RG_BARI, RG_TENOR, RG_BASS, RG_BARI, RG_TENOR, RG_BASS, RG_TENOR};
static const uint8_t RANGE_LO[3] = {38, 45, 50}, RANGE_HI[3] = {54, 62, 67};
static const uint8_t MONK_ROW[NMONK] = {0, 1, 1, 0, 0, 0, 1};          /* the stalls: 0 north (above), 1 south */
static const uint8_t MONK_POS[NMONK] = {1, 1, 0, 2, 3, 0, 2};          /* 0..3, left to right */
static const uint8_t MONK_VOWEL[NMONK] = {45, 66, 18, 52, 72, 30, 60}; /* his colour: 0 oo .. 100 eh */
static const uint8_t MONK_FORMANT[NMONK] = {95, 104, 87, 96, 102, 89, 106};
static const int8_t MONK_CENTS[NMONK] = {0, 6, -4, 5, -5, -2, 8};       /* his own tuning */
static const uint8_t MONK_VIB[NMONK] = {30, 18, 65, 35, 40, 28, 15};
static const uint16_t MONK_LEVEL[NMONK] = {820, 640, 700, 780, 760, 820, 600};

/* ---- the offices */
enum { OF_MATINS, OF_LAUDS, OF_PRIME, OF_TERCE, OF_SEXT, OF_NONE, OF_VESPERS, OF_COMPLINE, NOFFICE };
static const struct {
    const char *name;
    uint8_t hour, min;                   /* when it is sung (the in-world clock) */
    uint8_t mode, final;                 /* amb.h MODE_*, the final (MIDI) */
    uint8_t weight;                      /* its part of the day (of 150) */
    uint8_t singers;                     /* how many sing together, at most */
    uint16_t pace;                       /* a syllable, ms */
    uint8_t tolls;                       /* the bell's strokes */
    uint8_t vowel;                       /* the mood: dark (0) .. bright (100) */
    uint8_t organum;                     /* the Amen a fifth apart */
    uint8_t end;                         /* where the chant ends, % of its part of the day */
} OFFICE[NOFFICE] = {
    {"Matins", 2, 0, MODE_PHRYGIAN, 52, 26, 3, 1000, 3, 25, 0, 86},
    {"Lauds", 5, 30, MODE_MIXOLYDIAN, 55, 18, 4, 650, 5, 75, 1, 88},
    {"Prime", 7, 0, MODE_DORIAN, 50, 14, 3, 720, 3, 50, 0, 86},
    {"Terce", 9, 0, MODE_LYDIAN, 53, 14, 3, 700, 3, 60, 0, 86},
    {"Sext", 12, 0, MODE_AEOLIAN, 45, 14, 2, 680, 3, 55, 0, 84},
    {"None", 15, 0, MODE_IONIAN, 48, 14, 3, 700, 3, 50, 0, 86},
    {"Vespers", 18, 0, MODE_DORIAN, 50, 24, 4, 760, 5, 60, 1, 88},
    {"Compline", 20, 0, MODE_AEOLIAN, 52, 26, 2, 1000, 3, 30, 0, 66},
};
static const uint8_t RECITE[EN_NMODES] = {4, 4, 5, 4, 4, 4, 3, 3};     /* the reciting tone: a degree above the final */
static int of_final(int o) { return preset_final(OFFICE[o].final); }   /* the office's final and mode, by the preset */
static int of_mode(int o) { return preset_mode(OFFICE[o].mode); }
static const uint16_t DAY_MIN_TAB[WD_NDAY] = {30, 60, 120, 240, 480};

/* ---- the chronicle's events */
enum { EV_NONE, EV_EARLY, EV_LATE, EV_FLAT, EV_NOTE, EV_CLASH, EV_NAP, EV_WAKE, EV_ILL, EV_BACK, EV_ARRIVE, EV_VOWS,
       EV_CLEAN, EV_FROWN, EV_NOD, EV_PEACE, EV_PRACTICE, EV_NUDGE, EV_ASK, EV_DAY, NEV };

/* ---- the chant: phrases */
#define PH_MAX 40
#define REST (-100)
enum { K_INTONE, K_RESP, K_ANTI, K_VERSE, K_AMEN, K_PRACTICE, K_SOLO };
typedef struct {
    int8_t deg[PH_MAX];                  /* scale degrees from the final (REST: a breath) */
    uint16_t at[PH_MAX + 1];             /* each note's start, ms into the phrase (at[n]: its end) */
    uint8_t vow[PH_MAX];                 /* the syllable's vowel 0..100 (the text: all sing it) */
    uint8_t n, kind, nsing, used;
    int32_t t0;                          /* its start, ms into the office */
    uint8_t who[4];                      /* the singers */
    int8_t slot[4];                      /* their voices */
    int16_t entry[4];                    /* when each comes in (ms from t0: < 0 early, > 0 late) */
    int8_t shift[4];                     /* semitones: his octave, or a fifth under */
    uint16_t level[4];
    uint8_t flat[4];                     /* cents he sinks by the phrase's end */
    int8_t clash;                        /* the singer who argues (-1 none), against clash_with */
    int8_t clash_with;
    uint8_t clash_i, clash_n;            /* on these notes */
    uint8_t clash_yield;                 /* 0: he gives way halfway, 1: the other does */
    uint8_t clash_said;
} phrase_t;
#define NPH 3

enum { PH_TOLL, PH_CHANT, PH_REST };
static struct {
    uint32_t prev;                       /* the last tick's plat_ms */
    int32_t t;                           /* ms into the office's part of the day */
    int32_t slot_ms, end_ms;             /* its part of the day; where the chant ends */
    uint8_t phase, tolls_left, part, side, verses, psalm, amen_done, practised;
    int32_t next_toll, next_start;
    uint8_t next_kind;
    phrase_t ph[NPH];
    uint8_t phw;
    uint8_t asleep[NMONK];
    uint8_t ask[NMONK];                  /* a key asked him to sing the next verse */
    uint8_t slips, novice_sang;          /* the novice this office */
    uint8_t flat_said, noted;            /* Ambrose's flat said this office; the abbot gave him the note */
    int8_t drone;
    /* what each voice sings now, and what the engine was last told */
    int16_t v_pitch[EN_NPAD], v_level[EN_NPAD], v_vowel[EN_NPAD];
    int16_t s_pitch[EN_NPAD], s_level[EN_NPAD], s_vowel[EN_NPAD];
    int8_t v_monk[EN_NPAD];              /* whose voice it is (-1 none) */
    uint8_t s_monk[EN_NPAD];             /* the monk the voice was set up for (formant, pan...) + 1 */
    int8_t monk_slot[NMONK];             /* his voice while singing (-1 silent) */
    int8_t monk_cents[NMONK];            /* his drift now (Ambrose's flat), for the screen */
    uint8_t clash_now;                   /* Gregory and Bernard are apart right now */
    char line[48];                       /* the event line */
    uint32_t line_t;                     /* when it was written (plat_ms) */
    uint32_t nevents;                    /* events since boot (tests) */
    uint8_t dirty;                       /* the world changed since the last save */
    uint8_t urgent;                      /* a big event (an arrival, an illness): kept at the hourly save */
    uint8_t said;                        /* a new event line for the screen reader */
    uint8_t practice;                    /* this interlude, a novice practises */
    uint32_t dt;                         /* the last tick's ms */
} wr;

static uint32_t wrand(void)
{
    uint32_t x = proj.world.seed ? proj.world.seed : 0x2545F491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    proj.world.seed = x;
    return x;
}
static int wirand(int n) { return n > 0 ? (int)((wrand() >> 8) % (uint32_t)n) : 0; }
static int wchance(int pct) { return wirand(1000) < pct * 10; }
static int wchance_f(float p) { return (float)wirand(10000) < p * 10000.0f; }

static monk_t *mk(int i) { return &proj.world.m[i]; }
static int here(int i) { return mk(i)->here == 1; }
static int awake(int i) { return here(i) && !wr.asleep[i]; }
static int is_novice(int i) { return (i == MK_NOVICE || i == MK_NEW) && !mk(i)->vowed; }

static int32_t day_ms(void) { return (int32_t)DAY_MIN_TAB[proj.daylen < WD_NDAY ? proj.daylen : 2] * 60000; }

/* ------------------------------------------------------------ the chronicle --- */
static const char *mname(int i) { return (unsigned)i < NMONK ? MONK_NAME[i] : "?"; }

static char *wcat(char *b, const char *s)
{
    while ((*b = *s++))
        b++;
    return b;
}

/* an event's words: "Br. Anselm came in early" (the line), at most 47 characters */
static void ev_text(int type, int who, int who2, int office, char *b)
{
    const char *n = mname(who), *n2 = mname(who2);
    char *p = b;
    *p = 0;
    if (type != EV_CLASH && type != EV_PEACE && type != EV_ARRIVE && type != EV_FROWN && type != EV_NOD &&
        type != EV_NOTE && type != EV_DAY)
        p = wcat(wcat(p, who == MK_ABBOT ? "Abbot " : "Br. "), n);
    switch (type) {
    case EV_EARLY: wcat(p, " came in early"); break;
    case EV_LATE: wcat(p, " missed his entry"); break;
    case EV_FLAT: wcat(p, " sank flat"); break;
    case EV_NOTE: wcat(wcat(wcat(p, "The abbot gave "), n), " the note"); break;
    case EV_CLASH: wcat(wcat(wcat(wcat(p, n), " yielded the pitch to "), n2), ""); break;
    case EV_NAP: wcat(wcat(p, " nodded off at "), OFFICE[office % NOFFICE].name); break;
    case EV_WAKE: wcat(p, " woke with a start"); break;
    case EV_ILL: wcat(p, " fell ill"); break;
    case EV_BACK: wcat(p, " is back in choir"); break;
    case EV_ARRIVE: wcat(wcat(wcat(p, "A new novice, "), n), ", came"); break;
    case EV_VOWS: wcat(p, " took his vows"); break;
    case EV_CLEAN: wcat(wcat(wcat(p, " sang "), OFFICE[office % NOFFICE].name), " clean"); break;
    case EV_FROWN: wcat(wcat(p, "The abbot frowned at "), n); break;
    case EV_NOD: wcat(wcat(p, "The abbot nodded to "), n); break;
    case EV_PEACE: wcat(wcat(wcat(wcat(p, n), " and "), n2), " made peace"); break;
    case EV_PRACTICE: wcat(p, " practised alone"); break;
    case EV_NUDGE: wcat(p, " stood to sing"); break;
    case EV_ASK: wcat(p, " is in the infirmary"); break;
    case EV_DAY: {
        char t[8];
        p = wcat(p, "Day ");
        itoa_u((uint32_t)(who | who2 << 8), t);
        wcat(p, t);
        break;
    }
    default: break;
    }
}

static void line_set(const char *s)
{
    uint32_t i;
    for (i = 0; i < sizeof wr.line - 1 && s[i]; i++)
        wr.line[i] = s[i];
    wr.line[i] = 0;
    wr.line_t = plat_ms();
    wr.said = 1;
}

/* an event: on the line; kept in the chronicle unless it is small */
static void event(int type, int who, int who2)
{
    char t[48];
    world_t *w = &proj.world;
    ev_text(type, who, who2, w->office, t);
    line_set(t);
#ifdef PLAT_HOST
    printf("  [day %u %s %d:%02d] %s\n", (unsigned)w->day, OFFICE[w->office].name, world_minutes() / 60,
           world_minutes() % 60, t);
#endif
    wr.nevents++;
    wr.dirty = 1;
    if (type == EV_NUDGE || type == EV_ASK || type == EV_DAY)
        return;
    w->chron[w->chron_w].day = w->day;
    w->chron[w->chron_w].office = w->office;
    w->chron[w->chron_w].type = (uint8_t)type;
    w->chron[w->chron_w].who = (uint8_t)who;
    w->chron[w->chron_w].who2 = (uint8_t)who2;
    w->chron_w = (uint8_t)((w->chron_w + 1) % NCHRON);
    if (w->nchron < NCHRON)
        w->nchron++;
    if (type == EV_ILL || type == EV_BACK || type == EV_ARRIVE || type == EV_VOWS || type == EV_PEACE)
        wr.urgent = 1;
}

/* the chronicle's k-th latest (0 the latest): 0 when there is none */
static const chron_t *chron_get(int k)
{
    const world_t *w = &proj.world;
    if (k >= w->nchron)
        return 0;
    return &w->chron[(w->chron_w + NCHRON - 1 - k) % NCHRON];
}

/* ---------------------------------------------------------------- the world --- */
void world_defaults(world_t *w)
{
    int i;
    memset(w, 0, sizeof *w);
    w->seed = 0x6D6F6E6Bu;
    w->day = 1;
    w->office = OF_LAUDS;                /* it starts at dawn */
    w->rivalry = 45;
    for (i = 0; i < NMONK; i++) {
        monk_t *m = &w->m[i];
        m->here = i == MK_NEW ? 0 : 1;
        m->skill = i == MK_NOVICE ? 22 : i == MK_NEW ? 8 : 90;
        m->health = i == MK_ELDER ? 70 : 92;
        m->mood = 60;
        m->days = i == MK_NOVICE ? 3 : i == MK_NEW ? 0 : (uint16_t)(400 + 900 * (i == MK_ELDER) + 37 * i);
    }
}

int world_valid(const world_t *w)
{
    int i;
    if (w->office >= NOFFICE || w->nchron > NCHRON || w->chron_w >= NCHRON || w->rivalry > 100 || !w->day)
        return 0;
    for (i = 0; i < NMONK; i++)
        if (w->m[i].here > 2 || w->m[i].skill > 100 || w->m[i].health > 100 || w->m[i].mood > 100)
            return 0;
    return 1;
}

static void mood_add(int i, int d)
{
    int v = mk(i)->mood + d;
    mk(i)->mood = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
}

/* a new day, at Matins: the novices learn, the old brother's health, a newcomer, the two rivals */
static void new_day(void)
{
    world_t *w = &proj.world;
    int i;
    w->day++;
    for (i = 0; i < NMONK; i++)
        if (mk(i)->here)
            mk(i)->days++;
    for (i = 0; i < NMONK; i++) {
        monk_t *m = mk(i);
        if (!here(i) || !is_novice(i))
            continue;
        m->skill = (uint8_t)(m->skill + 2 + wirand(4) + (wr.practised ? 2 : 0) > 100 ? 100 :
                             m->skill + 2 + wirand(4) + (wr.practised ? 2 : 0));
        if (m->skill >= 85 && m->days >= 8) {
            m->vowed = 1;
            mood_add(i, 20);
            event(EV_VOWS, i, 0);
        }
    }
    wr.practised = 0;
    if (!mk(MK_NEW)->here && w->day >= 6 && (mk(MK_NOVICE)->vowed || w->day >= 12) && wchance(35)) {
        monk_t *m = mk(MK_NEW);
        m->here = 1;
        m->skill = 8;
        m->mood = 45;
        m->days = 0;
        event(EV_ARRIVE, MK_NEW, 0);
    }
    {   /* the old brother */
        monk_t *m = mk(MK_ELDER);
        if (m->here == 2) {
            if (m->ill_days)
                m->ill_days--;
            if (!m->ill_days) {
                m->here = 1;
                m->health = 55;
                event(EV_BACK, MK_ELDER, 0);
            }
        } else if (m->here == 1) {
            if (wchance_f(0.08f + (float)(100 - m->health) * 0.0025f)) {
                m->here = 2;
                m->ill_days = (uint8_t)(1 + wirand(3));
                event(EV_ILL, MK_ELDER, 0);
            } else if (m->health < 85) {
                m->health = (uint8_t)(m->health + 4);
            }
        }
    }
    if (w->peace_days) {                 /* the rivals: peace wears off; the rivalry wanders */
        w->peace_days--;
    } else {
        int r = (int)w->rivalry - 3 - wirand(8) + (wchance(15) ? 15 : 0);   /* now and then a new grudge */
        w->rivalry = (uint8_t)(r < 0 ? 0 : r > 100 ? 100 : r);
        if (w->rivalry < 15 || (w->rivalry > 75 && wchance(30))) {   /* worn out, or the abbot steps in */
            w->peace_days = (uint8_t)(2 + wirand(3));
            w->rivalry = 30;
            event(EV_PEACE, MK_GREGORY, MK_BERNARD);
        }
    }
    for (i = 0; i < NMONK; i++)          /* moods drift home */
        mood_add(i, mk(i)->mood < 60 ? 3 : -2);
    wr.dirty = 1;
}

/* ------------------------------------------------------------------ the chant --- */
static int steps7[12], nsteps7;

static int deg_semi(int d)               /* a degree from the final, in semitones */
{
    int o = 0;
    while (d < 0) {
        d += nsteps7;
        o -= 12;
    }
    o += 12 * (d / nsteps7);
    return o + steps7[d % nsteps7];
}

static int pace_ms(void)
{
    int p = OFFICE[proj.world.office].pace, k = preset_pace(proj.pace ? proj.pace : 100);
    return p * 100 / (k > 0 ? k : 100);
}

static void ph_note(phrase_t *p, int deg, int dur, int vow)
{
    if (p->n >= PH_MAX)
        return;
    p->deg[p->n] = (int8_t)deg;
    p->vow[p->n] = (uint8_t)(vow < 0 ? 0 : vow > 100 ? 100 : vow);
    p->at[p->n + 1] = (uint16_t)(p->at[p->n] + dur);
    p->n++;
}
static int syl(int pace, int pct) { return pace * pct / 100 * (88 + wirand(25)) / 100; }
static int vow_rand(void)
{
    static const uint8_t V[6] = {10, 35, 55, 70, 85, 45};   /* oo, o, ah, a, eh, o-ah */
    int m = OFFICE[proj.world.office].vowel;
    return (V[wirand(6)] * 2 + m) / 3;
}

/* the line, by kind */
static void ph_build(phrase_t *p, int kind)
{
    int pace = pace_ms(), r = RECITE[of_mode(proj.world.office)], i, k, cur;
    p->n = 0;
    p->at[0] = 0;
    p->kind = (uint8_t)kind;
    switch (kind) {
    case K_INTONE:                       /* "Deus in adjutorium meum intende": up to the tone, a half close */
        ph_note(p, 0, syl(pace, 110), 40);
        ph_note(p, -1, syl(pace, 70), 30);
        ph_note(p, 0, syl(pace, 90), 55);
        ph_note(p, 2, syl(pace, 90), 65);
        for (i = 0; i < 3 + wirand(3); i++)
            ph_note(p, r, syl(pace, 80), vow_rand());
        ph_note(p, r + 1, syl(pace, 100), 60);
        ph_note(p, r, syl(pace, 90), 50);
        ph_note(p, r - 1, pace * 3, 35);
        break;
    case K_RESP:                         /* the answer: recited, then down to the final */
    case K_VERSE:
        if (kind == K_VERSE) {           /* the intonation, to the reciting tone */
            if (wchance(60))
                ph_note(p, r - 2, syl(pace, 90), vow_rand());
            ph_note(p, r - 1, syl(pace, 90), vow_rand());
        }
        for (i = 0, k = 3 + wirand(5); i < k; i++)
            ph_note(p, r, syl(pace, 75), vow_rand());
        if (kind == K_VERSE) {           /* the mediant, a breath */
            if (wchance(50)) {
                ph_note(p, r + 1, syl(pace, 90), vow_rand());
                ph_note(p, r, pace * 2, 45);
            } else {
                ph_note(p, r - 1, syl(pace, 90), vow_rand());
                ph_note(p, r, pace * 2, 45);
            }
            ph_note(p, REST, pace / 2 + 150, 0);
            for (i = 0, k = 2 + wirand(5); i < k; i++)
                ph_note(p, r, syl(pace, 75), vow_rand());
        }
        cur = r;                         /* the termination: stepwise down to the final */
        while (cur > 0 && p->n < PH_MAX - 2) {
            int s = wirand(100);
            if (s < 14 && cur < r)
                cur++;                   /* a neighbour */
            else if (s < 26 && cur >= 2)
                cur -= 2;
            else if (s >= 34)
                cur--;
            ph_note(p, cur, syl(pace, cur ? 95 : 100), vow_rand());
        }
        p->at[p->n] = (uint16_t)(p->at[p->n - 1] + pace * 4);   /* the final held */
        p->vow[p->n - 1] = 30;
        break;
    case K_ANTI:                         /* an antiphon: a stepwise melisma up and home */
    case K_PRACTICE:
    case K_SOLO: {
        int top = r + (wchance(50) ? 1 : 0), n = 9 + wirand(6), up = 1;
        cur = wchance(40) ? 0 : wchance(50) ? -1 : 2;
        for (i = 0; i < n && p->n < PH_MAX - 3; i++) {
            int dur = wchance(30) ? syl(pace, 55) : syl(pace, 100 + 15 * wirand(3));
            ph_note(p, cur, dur, vow_rand());
            if (up) {
                cur += wchance(70) ? 1 : wchance(50) ? 2 : -1;
                if (cur >= top)
                    up = 0;
            } else {
                cur += wchance(65) ? -1 : 1;
                if (cur > top)
                    cur = top - 1;
            }
            if (!up && i > n - 4 && cur > 1)
                cur--;
            if (cur < -2)
                cur = -1;
        }
        while (cur > 0 && p->n < PH_MAX - 1)
            ph_note(p, --cur, syl(pace, 110), vow_rand());
        if (cur < 0)
            ph_note(p, 0, syl(pace, 110), 40);
        p->at[p->n] = (uint16_t)(p->at[p->n - 1] + pace * 3);
        break;
    }
    case K_AMEN:
    default:
        ph_note(p, 0, pace * 2, 60);     /* "A-": the final, a step up and home */
        ph_note(p, wchance(50) ? 1 : -1, pace * 2, 50);
        ph_note(p, 0, pace * 2, 40);
        p->at[p->n] = (uint16_t)(p->at[p->n - 1] + pace * 6);
        break;
    }
}

/* his octave for this line: the one where it fits his range best */
static int fit_octave(const phrase_t *p, int m)
{
    int lo = 127, hi = 0, i, best = 0, bo = 1 << 20, o, f = of_final(proj.world.office);
    for (i = 0; i < p->n; i++)
        if (p->deg[i] != REST) {
            int n = f + deg_semi(p->deg[i]);
            lo = n < lo ? n : lo;
            hi = n > hi ? n : hi;
        }
    for (o = -24; o <= 12; o += 12) {
        int over = (lo + o < RANGE_LO[MONK_RANGE[m]] ? RANGE_LO[MONK_RANGE[m]] - lo - o : 0) +
                   (hi + o > RANGE_HI[MONK_RANGE[m]] ? hi + o - RANGE_HI[MONK_RANGE[m]] : 0);
        over = over * 4 + (o < 0 ? -o : o) / 12;   /* the nearest octave when two fit */
        if (over < bo) {
            bo = over;
            best = o;
        }
    }
    return best;
}

static int voice_busy(int s, int except)  /* a voice still used by a phrase (its singer's tail included) */
{
    int k, j;
    for (k = 0; k < NPH; k++) {
        const phrase_t *p = &wr.ph[k];
        if (k == except || !p->used)
            continue;
        for (j = 0; j < p->nsing; j++)
            if (p->slot[j] == s && wr.t < p->t0 + (int32_t)p->at[p->n] + 1500)
                return 1;
    }
    return 0;
}

/* the singers for a line of this kind: who is awake and in choir */
static int pick(int kind, uint8_t *who)
{
    int n = 0, i, cap = OFFICE[proj.world.office].singers, cand[NMONK], nc = 0;
    if (kind == K_INTONE) {
        who[0] = here(MK_ABBOT) ? MK_ABBOT : MK_GREGORY;
        return 1;
    }
    if (kind == K_PRACTICE) {
        for (i = 0; i < NMONK; i++)
            if (awake(i) && is_novice(i))
                cand[nc++] = i;
        if (!nc)
            return 0;
        who[0] = (uint8_t)cand[wirand(nc)];
        return 1;
    }
    if (kind == K_VERSE) {               /* a side of the choir */
        int side = wr.side, tries;
        for (tries = 0; tries < 2 && !nc; tries++, side ^= 1)
            for (i = 0; i < NMONK; i++)
                if (awake(i) && MONK_ROW[i] == side)
                    cand[nc++] = i;
        cap = cap >= 4 ? 2 : cap == 3 ? 1 + wirand(2) : 1;
        for (i = 0; i < NMONK; i++)      /* one a key asked for sings it */
            if (wr.ask[i] && awake(i)) {
                wr.ask[i] = 0;
                who[n++] = (uint8_t)i;
                break;
            }
    } else {
        for (i = 0; i < NMONK; i++)
            if (awake(i))
                cand[nc++] = i;
        if (kind == K_ANTI && wr.part == 2) {   /* the antiphon's first time: a cantor */
            int c = wchance(50) ? MK_ABBOT : wchance(60) ? MK_BERNARD : MK_GREGORY;
            if (!awake(c))
                c = MK_ABBOT;
            who[0] = (uint8_t)c;
            return 1;
        }
        if (kind == K_AMEN || kind == K_RESP)
            cap = cap < 4 ? cap + 1 : 4;
        if (awake(MK_ABBOT) && kind != K_VERSE)
            who[n++] = MK_ABBOT;
    }
    while (n < cap && nc) {              /* the rest at random */
        int k = wirand(nc), m = cand[k], dup = 0, j;
        for (j = 0; j < n; j++)
            dup |= who[j] == m;
        if (!dup)
            who[n++] = (uint8_t)m;
        cand[k] = cand[--nc];
    }
    if (kind == K_VERSE && awake(MK_GREGORY) && awake(MK_BERNARD) && MONK_ROW[MK_GREGORY] == wr.side && n < 3 &&
        !proj.world.peace_days && wchance(15 + proj.world.rivalry / 4)) {   /* the two like to sing together */
        int j, g = 0, b = 0;
        for (j = 0; j < n; j++)
            g |= who[j] == MK_GREGORY, b |= who[j] == MK_BERNARD;
        if (!g && n < 3)
            who[n++] = MK_GREGORY;
        if (!b && n < 3)
            who[n++] = MK_BERNARD;
    }
    return n;
}

/* a new line, from t0: its singers, their voices, their entries; the brothers' habits */
static void ph_start(int kind, int32_t t0)
{
    phrase_t *p = &wr.ph[wr.phw];
    const int pace = pace_ms();
    int j, s, free_n = 0;
    int8_t free_s[EN_NPAD];
    wr.phw = (uint8_t)((wr.phw + 1) % NPH);
    memset(p, 0, sizeof *p);
    p->clash = p->clash_with = -1;
    ph_build(p, kind);
    p->t0 = t0;
    p->nsing = (uint8_t)pick(kind, p->who);
    for (s = 0; s < EN_NPAD; s++)
        if (!voice_busy(s, (int)(p - wr.ph)))
            free_s[free_n++] = (int8_t)s;
    if (p->nsing > free_n)
        p->nsing = (uint8_t)free_n;
    p->used = p->nsing > 0;
    for (j = 0; j < p->nsing; j++) {
        int m = p->who[j];
        p->slot[j] = free_s[j];
        p->shift[j] = (int8_t)fit_octave(p, m);
        p->level[j] = MONK_LEVEL[m];
        if (kind == K_PRACTICE)
            p->level[j] = (uint16_t)(p->level[j] / 2);
        if (is_novice(m))
            p->level[j] = (uint16_t)(p->level[j] * (70 + mk(m)->skill * 3 / 10) / 100);
        if (m == MK_ELDER && mk(m)->health < 60)
            p->level[j] = (uint16_t)(p->level[j] * 8 / 10);
        mk(m)->sung++;
    }
    /* the Amen of a full office: a bass a fifth under (organum) */
    if (kind == K_AMEN && OFFICE[proj.world.office].organum)
        for (j = 0; j < p->nsing; j++)
            if (MONK_RANGE[p->who[j]] == RG_BASS) {
                p->shift[j] = (int8_t)(fit_octave(p, p->who[j]) + 12 - 7);
                if (of_final(proj.world.office) + p->shift[j] < RANGE_LO[RG_BASS])
                    p->shift[j] = (int8_t)(p->shift[j] + 12);
                break;
            }
    /* the novice's entries */
    for (j = 0; j < p->nsing; j++) {
        int m = p->who[j];
        float slip;
        if (!is_novice(m) || kind == K_PRACTICE)
            continue;
        wr.novice_sang++;
        slip = 0.32f * (1.0f - (float)mk(m)->skill * 0.01f) + (mk(m)->mood < 40 ? 0.06f : 0.0f);
        if (wchance_f(slip)) {
            mk(m)->slips++;
            wr.slips++;
            mood_add(m, -4);
            if (wchance(40) && kind == K_VERSE) {
                p->entry[j] = (int16_t)-(pace * 6 / 10 + wirand(pace / 2));
                event(EV_EARLY, m, 0);
            } else {
                int k = 1 + wirand(3);
                if (k > p->n - 1)
                    k = p->n - 1;
                p->entry[j] = (int16_t)(p->at[k] + 60);
                event(EV_LATE, m, 0);
            }
        }
    }
    /* the old brother sinks */
    for (j = 0; j < p->nsing; j++)
        if (p->who[j] == MK_ELDER && kind != K_INTONE) {
            const monk_t *m = mk(MK_ELDER);
            if (wchance_f(0.1f + (float)(100 - m->health) * 0.003f))
                p->flat[j] = (uint8_t)(14 + wirand(18) + (100 - m->health) / 4);
        }
    /* the rivals: a clash on the recitation */
    if (kind == K_VERSE && !proj.world.peace_days) {
        int g = -1, b = -1;
        for (j = 0; j < p->nsing; j++) {
            if (p->who[j] == MK_GREGORY)
                g = j;
            if (p->who[j] == MK_BERNARD)
                b = j;
        }
        if (g >= 0 && b >= 0) {
            if (wchance(8 + proj.world.rivalry / 4)) {
                int i, r = RECITE[of_mode(proj.world.office)], first = -1;
                for (i = 0; i < p->n; i++)
                    if (p->deg[i] == r) {
                        first = i;
                        break;
                    }
                if (first >= 0) {
                    p->clash = (int8_t)(wchance(50) ? g : b);
                    p->clash_with = (int8_t)(p->clash == g ? b : g);
                    p->clash_i = (uint8_t)first;
                    p->clash_n = (uint8_t)(2 + wirand(3) + proj.world.rivalry / 40);
                    p->clash_yield = (uint8_t)(mk(p->who[(int)p->clash])->mood > mk(p->who[(int)p->clash_with])->mood
                                                   ? wchance(65) : wchance(35));
                }
            } else if (wchance(10)) {
                int r = (int)proj.world.rivalry - 2;
                proj.world.rivalry = (uint8_t)(r < 0 ? 0 : r);
            }
        }
    }
}

/* the office's order: intonation, response, then psalms (antiphon, verses, antiphon), the Amen */
static int next_kind(void)
{
    if (wr.part == 0)
        return K_INTONE;
    if (wr.part == 1)
        return K_RESP;
    if (wr.t >= wr.end_ms && !wr.amen_done)
        return K_AMEN;
    if (wr.part == 2)
        return K_ANTI;
    if (wr.verses)
        return K_VERSE;
    return K_ANTI;                       /* the antiphon again: a psalm ends */
}

static void office_begin(void)
{
    const int o = proj.world.office;
    int i;
    wr.t = 0;
    wr.slot_ms = (int32_t)((uint32_t)day_ms() / 150u * OFFICE[o].weight);
    wr.end_ms = wr.slot_ms / 100 * OFFICE[o].end;
    wr.phase = PH_TOLL;
    wr.tolls_left = OFFICE[o].tolls;
    wr.next_toll = 500;
    wr.part = 0;
    wr.side = 0;
    wr.psalm = 0;
    wr.amen_done = 0;
    wr.slips = wr.novice_sang = 0;
    wr.flat_said = wr.noted = 0;
    for (i = 0; i < NPH; i++)
        wr.ph[i].used = 0;
    for (i = 0; i < NMONK; i++)
        wr.asleep[i] = 0;
    nsteps7 = en_scale(EN_MODE_POOL[of_mode(o)], steps7);
    wr.drone = -1;
    en_drone(-1);
    world_key();
}

static void office_end(void)            /* the office sung: the abbot's verdict on the novice */
{
    int i;
    for (i = 0; i < NMONK; i++)
        if (is_novice(i) && here(i)) {
            if (wr.slips >= 2 && wchance(70)) {
                mood_add(i, -8);
                event(EV_FROWN, i, 0);
            } else if (!wr.slips && wr.novice_sang >= 4 && wchance(45)) {
                mood_add(i, 8);
                mk(i)->skill = (uint8_t)(mk(i)->skill < 100 ? mk(i)->skill + 1 : 100);
                event(wchance(50) ? EV_CLEAN : EV_NOD, i, 0);
            }
            break;
        }
    for (i = 0; i < NMONK; i++)
        if (wr.asleep[i]) {
            wr.asleep[i] = 0;
            event(EV_WAKE, i, 0);
        }
}

static void next_office(void)
{
    world_t *w = &proj.world;
    w->office = (uint8_t)((w->office + 1) % NOFFICE);
    if (w->office == OF_MATINS)
        new_day();
    wr.dirty = 1;
    office_begin();
    if (w->office == OF_MATINS) {
        char t[48];
        ev_text(EV_DAY, w->day & 0xFF, w->day >> 8, 0, t);
        line_set(t);
    }
}

/* the chant's next steps, at office time t */
static void chant_step(void)
{
    const int o = proj.world.office;
    phrase_t *last = &wr.ph[(wr.phw + NPH - 1) % NPH];
    if (wr.phase == PH_TOLL) {
        if (wr.tolls_left && wr.t >= wr.next_toll) {
            en_toll(50, 80);
            wr.tolls_left--;
            wr.next_toll = wr.t + 4600;
        }
        if (!wr.tolls_left && wr.t >= wr.next_toll + 1200) {
            wr.phase = PH_CHANT;
            wr.next_start = wr.t + 400;
            wr.drone = (int8_t)(of_final(o) > 52 ? of_final(o) - 12 : of_final(o));
            en_drone(wr.drone);
        }
        return;
    }
    if (wr.phase == PH_REST) {
        if (wr.practice && wr.t > wr.end_ms + 15000 && wr.t < wr.slot_ms - 20000) {   /* a novice practises */
            wr.practice = 0;
            wr.practised = 1;
            ph_start(K_PRACTICE, wr.t + 200);
            if (wr.ph[(wr.phw + NPH - 1) % NPH].used) {
                int m = wr.ph[(wr.phw + NPH - 1) % NPH].who[0];
                mk(m)->skill = (uint8_t)(mk(m)->skill < 100 ? mk(m)->skill + 1 : 100);
                event(EV_PRACTICE, m, 0);
            }
        }
        return;
    }
    /* chanting: Columba nods off at Matins */
    if (o == OF_MATINS && here(MK_SLEEPER) && !wr.asleep[MK_SLEEPER] && wr.part >= 3 && !wr.amen_done &&
        wchance_f((float)wr.dt / (0.5f * (float)wr.slot_ms) * (float)(120 - mk(MK_SLEEPER)->mood) * (1.0f / 60.0f))) {
        int j, k, singing = 0;
        for (k = 0; k < NPH; k++)
            for (j = 0; j < wr.ph[k].nsing; j++)
                singing |= wr.ph[k].used && wr.ph[k].who[j] == MK_SLEEPER &&
                           wr.t < wr.ph[k].t0 + (int32_t)wr.ph[k].at[wr.ph[k].n];
        if (!singing) {
            wr.asleep[MK_SLEEPER] = 1;
            mk(MK_SLEEPER)->naps++;
            event(EV_NAP, MK_SLEEPER, 0);
        }
    }
    if (wr.amen_done) {                  /* the Amen sung: the interlude when it has rung out */
        if (wr.t >= last->t0 + (int32_t)last->at[last->n] + 800) {
            wr.phase = PH_REST;
            wr.practice = (uint8_t)(o != OF_COMPLINE && wchance(55));
            wr.drone = -1;
            en_drone(-1);
            office_end();
            if (o == OF_VESPERS || o == OF_LAUDS)
                en_toll(74, 22);         /* the abbot's handbell */
        }
        return;
    }
    if (wr.t + 1500 < wr.next_start)
        return;
    {
        int kind = next_kind(), gap;
        if (kind == K_AMEN && wr.asleep[MK_SLEEPER]) {   /* the Amen wakes him */
            wr.asleep[MK_SLEEPER] = 0;
            event(EV_WAKE, MK_SLEEPER, 0);
        }
        if (kind == K_VERSE && here(MK_ELDER) && wr.flat_said && !wr.noted) {
            wr.noted = 1;                /* the abbot gives him the note */
            event(EV_NOTE, MK_ELDER, 0);
        }
        if (kind != K_VERSE && last->used && wr.next_start < last->t0 + (int32_t)last->at[last->n] + 900) {
            wr.next_start = last->t0 + (int32_t)last->at[last->n] + 900;   /* everyone free first */
            return;
        }
        ph_start(kind, wr.next_start);
        last = &wr.ph[(wr.phw + NPH - 1) % NPH];
        switch (kind) {                  /* what follows, and when */
        case K_INTONE: wr.part = 1; gap = 500; break;
        case K_RESP: wr.part = 2; gap = 1200; break;
        case K_ANTI:
            if (wr.part == 2) {
                wr.part = 3;
                wr.verses = (uint8_t)(4 + wirand(5));
                gap = 700;
            } else {
                wr.part = 2;
                wr.psalm++;
                gap = 2500;
            }
            break;
        case K_VERSE:
            wr.side ^= 1;
            if (wr.verses)
                wr.verses--;
            gap = wr.verses ? -(int)(last->at[last->n] - last->at[last->n - 1]) * 2 / 3 : 1000;   /* the other side
                                                                                     * over the held final */
            break;
        default:
            wr.amen_done = 1;
            gap = 0;
            break;
        }
        wr.next_start = last->t0 + (int32_t)last->at[last->n] + gap;
        if (!last->used && kind != K_AMEN)   /* nobody to sing it (all ill or asleep): on */
            wr.next_start = wr.t + 2000;
    }
}

/* what each voice sings at office time t */
static void voices_step(void)
{
    const int f = of_final(proj.world.office), r = RECITE[of_mode(proj.world.office)];
    int s, k, j, flat_now = 0;
    wr.clash_now = 0;
    for (s = 0; s < EN_NPAD; s++) {
        wr.v_level[s] = 0;
        wr.v_monk[s] = -1;
    }
    for (j = 0; j < NMONK; j++) {
        wr.monk_slot[j] = -1;
        wr.monk_cents[j] = 0;
    }
    for (k = 0; k < NPH; k++) {
        phrase_t *p = &wr.ph[k];
        int32_t pt = wr.t - p->t0;
        if (!p->used || pt < -3000 || pt > (int32_t)p->at[p->n] + 1500)
            continue;
        for (j = 0; j < p->nsing; j++) {
            int m = p->who[j], sl = p->slot[j], i, deg, cents, note;
            int32_t q;
            if (sl < 0)
                continue;
            if (wr.v_monk[sl] < 0 || pt < (int32_t)p->at[p->n] + 200)
                wr.v_monk[sl] = (int8_t)m;   /* his voice (its tail too, while no one else takes it) */
            if (pt < p->entry[j] || pt >= (int32_t)p->at[p->n] || wr.asleep[m])
                continue;
            q = pt < 0 ? 0 : pt;
            for (i = 0; i + 1 < p->n && (int32_t)p->at[i + 1] <= q; i++)
                ;
            deg = p->deg[i];
            if (deg == REST)
                continue;
            if (p->clash == j && i >= p->clash_i && i < p->clash_i + p->clash_n) {
                int half = i >= p->clash_i + p->clash_n / 2;
                if (!(half && !p->clash_yield))
                    deg = r + 1;         /* his pitch, a step above */
                wr.clash_now |= (uint8_t)!half;
                if (half && !p->clash_said) {   /* one gives way: said, and remembered */
                    int y = p->clash_yield ? p->clash_with : p->clash, w2 = p->clash_yield ? p->clash : p->clash_with;
                    int ym = p->who[y], wm = p->who[w2];
                    p->clash_said = 1;
                    mk(ym)->clashes++;
                    mk(wm)->clashes++;
                    mood_add(ym, -6);
                    mood_add(wm, 3);
                    proj.world.rivalry = (uint8_t)(proj.world.rivalry + 2 > 100 ? 100 : proj.world.rivalry + 2);
                    event(EV_CLASH, ym, wm);
                }
            } else if (p->clash_with == j && p->clash_yield && i >= p->clash_i + p->clash_n / 2 &&
                       i < p->clash_i + p->clash_n) {
                deg = r + 1;             /* the other gave way: to his pitch */
            }
            note = f + deg_semi(deg) + p->shift[j];
            cents = MONK_CENTS[m];
            if (p->flat[j]) {            /* sinking through the line */
                int32_t d = (int32_t)p->flat[j] * q / (int32_t)p->at[p->n];
                cents -= (int)d;
                wr.monk_cents[m] = (int8_t)(-d);
                if (d >= 22)
                    flat_now = 1;
            }
            wr.v_pitch[sl] = (int16_t)(note * 100 + cents);
            wr.v_level[sl] = (int16_t)p->level[j];
            wr.v_vowel[sl] = (int16_t)((p->vow[i] * 6 + MONK_VOWEL[m] * 4) * 1);
            wr.v_monk[sl] = (int8_t)m;
            wr.monk_slot[m] = (int8_t)sl;
        }
    }
    if (flat_now && !wr.flat_said) {
        wr.flat_said = 1;
        mk(MK_ELDER)->flats++;
        event(EV_FLAT, MK_ELDER, 0);
    }
    for (s = 0; s < EN_NPAD; s++) {      /* to the engine: what changed */
        int m = wr.v_monk[s];
        if (m >= 0 && wr.s_monk[s] != m + 1) {
            int pan = (MONK_ROW[m] ? 30 : -30) + (MONK_POS[m] * 2 - 3) * 18;
            wr.s_monk[s] = (uint8_t)(m + 1);
            en_voice(s, VF_FORMANT, preset_formant(MONK_FORMANT[m]));
            en_voice(s, VF_PAN, pan);
            en_voice(s, VF_VIB, preset_vib(MONK_VIB[m] + (m == MK_ELDER && mk(m)->health < 60 ? 20 : 0)));
            en_voice(s, VF_GLIDE, m == MK_ELDER ? 2 : is_novice(m) ? 4 : 3);
            if (wr.s_level[s] <= 0)      /* from silence: no glide from the last singer's note */
                en_voice(s, VF_GLIDE, 100);
        }
        if (wr.v_level[s] > 0 && wr.v_pitch[s] != wr.s_pitch[s]) {
            en_voice(s, VF_PITCH, wr.v_pitch[s]);
            if (wr.s_level[s] <= 0 && m >= 0)
                en_voice(s, VF_GLIDE, m == MK_ELDER ? 2 : is_novice(m) ? 4 : 3);
            wr.s_pitch[s] = wr.v_pitch[s];
        }
        if (wr.v_level[s] > 0 && wr.v_vowel[s] != wr.s_vowel[s]) {
            en_voice(s, VF_VOWEL, wr.v_vowel[s]);
            wr.s_vowel[s] = wr.v_vowel[s];
        }
        if (wr.v_level[s] != wr.s_level[s]) {
            en_voice(s, VF_LEVEL, wr.v_level[s]);
            wr.s_level[s] = wr.v_level[s];
        }
    }
}

/* ------------------------------------------------------------- the API --- */
void world_init(void)
{
    int s;
    memset(&wr, 0, sizeof wr);
    wr.prev = plat_ms();
    for (s = 0; s < EN_NPAD; s++) {
        wr.s_pitch[s] = wr.s_vowel[s] = -1;
        wr.v_monk[s] = -1;
    }
    for (s = 0; s < NMONK; s++)
        wr.monk_slot[s] = -1;
    en_choir(1);
    office_begin();
    {
        char t[48];
        ev_text(EV_DAY, proj.world.day & 0xFF, proj.world.day >> 8, 0, t);
        line_set(t);
        wr.said = 0;
    }
}

void world_tick(void)
{
    uint32_t now = plat_ms(), dt = now - wr.prev;
    wr.prev = now;
    if (dt > 1000u)
        dt = 1000u;
    wr.dt = dt;
    if (en_playing && !en_frozen) {
        wr.t += (int32_t)dt;
        if (wr.t >= wr.slot_ms)
            next_office();
        chant_step();
    }
    voices_step();
}

void world_skip(void)                    /* EDIT: the bell for the next office, now */
{
    int s;
    if (wr.phase == PH_CHANT)
        office_end();
    for (s = 0; s < NPH; s++)
        wr.ph[s].used = 0;
    next_office();
}

/* a key: brother i. Asleep: he wakes; ill: he is in the infirmary; else he sings the next verse */
void world_nudge(int i)
{
    if ((unsigned)i >= NMONK || !mk(i)->here) {
        return;
    }
    if (mk(i)->here == 2) {
        event(EV_ASK, i, 0);
        return;
    }
    if (wr.asleep[i]) {
        wr.asleep[i] = 0;
        event(EV_WAKE, i, 0);
        return;
    }
    wr.ask[i] = 1;
    event(EV_NUDGE, i, 0);
}

/* the preset's key: the office's final (moved) and mode, as the engine's root and scale, for Drift 1.1's strings and
 * lead (the choir and the drone are sung by hand, from world.c) */
void world_key(void)
{
    en_set(P_ROOT, of_final(proj.world.office) % 12);
    en_pool(EN_MODE_POOL[of_mode(proj.world.office)]);
}

/* a new preset: the scale and the drone move now; each voice takes its new size and vibrato */
void world_preset(void)
{
    int s;
    nsteps7 = en_scale(EN_MODE_POOL[of_mode(proj.world.office)], steps7);
    world_key();
    if (wr.drone >= 0) {
        wr.drone = (int8_t)(of_final(proj.world.office) > 52 ? of_final(proj.world.office) - 12 : of_final(proj.world.office));
        en_drone(wr.drone);
    }
    for (s = 0; s < EN_NPAD; s++)
        wr.s_monk[s] = 0;
}

/* quiet: nobody sings, the bell has rung out (a flash write may glitch the sound a moment) */
int world_quiet(void) { return wr.phase == PH_REST && en_pad_level < 0.01f && en_toll_glow < 0.05f; }

/* the in-world time: minutes since midnight, between this office's hour and the next's */
int world_minutes(void)
{
    int o = proj.world.office, a = OFFICE[o].hour * 60 + OFFICE[o].min, b;
    b = OFFICE[(o + 1) % NOFFICE].hour * 60 + OFFICE[(o + 1) % NOFFICE].min;
    if (b <= a)
        b += 24 * 60;
    a += (int)((float)(b - a) * (float)(wr.t < 0 ? 0 : wr.t) / (float)(wr.slot_ms > 0 ? wr.slot_ms : 1));
    return a % (24 * 60);
}
