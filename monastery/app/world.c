/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen (MONASTERY), built on AMB-1 by Charles Vestal */
/* MONASTERY's world: a small community of monks singing the hours in a stone chapel. Part of the unity build (after
 * project.c, before ui.c).
 *
 * The day: eight offices (Matins, Lauds, Prime, Terce, Sext, None, Vespers, Compline), compressed into Day length
 * (2 hours by default), each with its own mode, final, pace, colour, choir and number of singers. An office: the bell
 * tolls, the abbot intones, all respond; then psalms: a cantor sings the antiphon, the verses are sung (side answering
 * side, two cantors trading them, or a soloist answered by the choir), all sing the antiphon again; at the end an
 * Amen, then a quiet interlude (after Compline: the night). A drone (the bass, as an ison) holds the final under the
 * chant.
 *
 * The chant: psalm tones (an intonation up to the reciting tone, recitation, a mediant cadence, recitation, a fall to
 * the final), antiphons as stepwise melismas, all modal; each monk sings the line in his octave. In the full
 * choruses (the response, the antiphon's return, the Amen) the low voices take a part of their own, by office: the
 * organum (a fifth under) at Lauds and Vespers, the ison (the final, held) at Matins, Sext and Compline.
 *
 * The brothers (each a voice of the engine's choir while he sings: his range, his vowel colour, his tuning):
 *   Benedict   the abbot: intones each office
 *   Anselm     the novice: given more to sing as the days pass (the choruses, then the verses, then a solo verse),
 *              sings an antiphon alone in the interludes, and in time takes his vows
 *   Ambrose    the old brother, a bass: sings the organum
 *   Gregory    and Bernard: the cantors. They sing the antiphons, and some psalms they trade the verses between them
 *   Bernard
 *   Columba    a bass: holds the ison
 *   Bede       a new novice who comes to the gate after some days
 * Fuller or sparser choirs by office: all at Lauds and Vespers, fewer at the little hours. The brothers, their
 * histories and the chronicle are kept (project_t.world); the world's clock runs while the choir sings (PLAY: it
 * rests, the clock stops). Nobody sings a wrong note: every voice sings the mode, in time, in tune. */

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
enum { MK_ABBOT, MK_NOVICE, MK_ELDER, MK_GREGORY, MK_BERNARD, MK_COLUMBA, MK_NEW };
static const char *const MONK_NAME[NMONK] = {"Benedict", "Anselm", "Ambrose", "Gregory", "Bernard", "Columba", "Bede"};
enum { RG_BASS, RG_BARI, RG_TENOR };
static const uint8_t MONK_RANGE[NMONK] = {RG_BARI, RG_TENOR, RG_BASS, RG_BARI, RG_TENOR, RG_BASS, RG_TENOR};
static const uint8_t RANGE_LO[3] = {38, 45, 50}, RANGE_HI[3] = {54, 62, 67};
static const uint8_t MONK_ROW[NMONK] = {0, 1, 1, 0, 0, 0, 1};          /* the stalls: 0 north (above), 1 south */
static const uint8_t MONK_POS[NMONK] = {1, 1, 0, 2, 3, 0, 2};          /* 0..3, left to right */
static const uint8_t MONK_VOWEL[NMONK] = {45, 66, 18, 52, 72, 30, 60}; /* his colour: 0 oo .. 100 eh */
static const uint8_t MONK_FORMANT[NMONK] = {95, 104, 87, 96, 102, 89, 106};
static const int8_t MONK_CENTS[NMONK] = {0, 2, -2, 3, -3, -1, 2};       /* a small chorus, no more */
static const uint8_t MONK_VIB[NMONK] = {30, 22, 40, 35, 38, 28, 20};
static const uint16_t MONK_LEVEL[NMONK] = {820, 700, 740, 780, 760, 800, 680};
#define PART_VERSES 30                   /* the novice's part (monk_t.part): from here he sings the verses, */
#define PART_SOLO 60                     /* from here a verse alone */

/* ---- the offices */
enum { OF_MATINS, OF_LAUDS, OF_PRIME, OF_TERCE, OF_SEXT, OF_NONE, OF_VESPERS, OF_COMPLINE, NOFFICE };
enum { LOW_UNISON, LOW_ISON, LOW_ORGANUM };
static const struct {
    const char *name;
    uint8_t hour, min;                   /* when it is sung (the in-world clock) */
    uint8_t mode, final;                 /* amb.h MODE_*, the final (MIDI) */
    uint8_t weight;                      /* its part of the day (of 150) */
    uint8_t singers;                     /* how many sing together, at most */
    uint16_t pace;                       /* a syllable, ms */
    uint8_t tolls;                       /* the bell's strokes */
    uint8_t vowel;                       /* the colour: dark (0) .. bright (100) */
    uint8_t low;                         /* the low voices' part in the choruses (LOW_*) */
    uint8_t end;                         /* where the chant ends, % of its part of the day */
    uint8_t choir;                       /* brothers in the stalls (all: 7) */
} OFFICE[NOFFICE] = {
    {"Matins", 2, 0, MODE_PHRYGIAN, 52, 26, 3, 1000, 3, 25, LOW_ISON, 86, 5},
    {"Lauds", 5, 30, MODE_MIXOLYDIAN, 55, 18, 4, 650, 5, 75, LOW_ORGANUM, 88, 7},
    {"Prime", 7, 0, MODE_DORIAN, 50, 14, 3, 720, 3, 50, LOW_UNISON, 86, 5},
    {"Terce", 9, 0, MODE_LYDIAN, 53, 14, 3, 700, 3, 60, LOW_UNISON, 86, 4},
    {"Sext", 12, 0, MODE_AEOLIAN, 45, 14, 2, 680, 3, 55, LOW_ISON, 84, 3},
    {"None", 15, 0, MODE_IONIAN, 48, 14, 3, 700, 3, 50, LOW_UNISON, 86, 4},
    {"Vespers", 18, 0, MODE_DORIAN, 50, 24, 4, 760, 5, 60, LOW_ORGANUM, 88, 7},
    {"Compline", 20, 0, MODE_AEOLIAN, 52, 26, 2, 1000, 3, 30, LOW_ISON, 66, 5},
};
static const uint8_t RECITE[EN_NMODES] = {4, 4, 5, 4, 4, 4, 3, 3};     /* the reciting tone: a degree above the final */
static int of_final(int o) { return preset_final(OFFICE[o].final); }   /* the office's final and mode, by the preset */
static int of_mode(int o) { return preset_mode(OFFICE[o].mode); }
static const uint16_t DAY_MIN_TAB[WD_NDAY] = {30, 60, 120, 240, 480};

/* ---- the chronicle's events. 1.0 and 1.1's numbers are kept (a saved chronicle reads the same); the ones marked
 * "gone" were the brothers' mistakes and dramas, no longer sung: a 1.1 chronicle drops them (project.c) */
enum { EV_NONE, EV_GONE1, EV_GONE2, EV_GONE3, EV_GONE4, EV_GONE5, EV_GONE6, EV_GONE7, EV_GONE8, EV_GONE9,
       EV_ARRIVE, EV_VOWS, EV_GONE12, EV_GONE13, EV_GONE14, EV_GONE15, EV_PRACTICE, EV_NUDGE, EV_GONE18, EV_DAY,
       EV_INTONE, EV_CANTOR, EV_SOLO, EV_TRADE, EV_ORGANUM, EV_ISON, EV_VERSES, EV_SOLOS, EV_COMES, NEV };
#define EV_KEPT(t) ((t) == EV_ARRIVE || (t) == EV_VOWS || (t) == EV_PRACTICE || ((t) >= EV_INTONE && (t) < NEV))

/* ---- the chant: phrases */
#define PH_MAX 40
#define REST (-100)
enum { K_INTONE, K_RESP, K_ANTI, K_VERSE, K_AMEN, K_PRACTICE };
enum { PT_MELODY, PT_ORGANUM, PT_ISON };  /* a singer's part: the line, the line a fifth under, the final held */
typedef struct {
    int8_t deg[PH_MAX];                  /* scale degrees from the final (REST: a breath) */
    uint16_t at[PH_MAX + 1];             /* each note's start, ms into the phrase (at[n]: its end) */
    uint8_t vow[PH_MAX];                 /* the syllable's vowel 0..100 (the text: all sing it) */
    uint8_t n, kind, nsing, used;
    int32_t t0;                          /* its start, ms into the office */
    uint8_t who[4];                      /* the singers */
    int8_t slot[4];                      /* their voices */
    int8_t shift[4];                     /* semitones: his octave */
    uint8_t part[4];                     /* PT_* */
    uint16_t level[4];
} phrase_t;
#define NPH 3

enum { PH_TOLL, PH_CHANT, PH_REST };
enum { PS_SIDES, PS_TRADE, PS_SOLO };    /* how a psalm's verses are sung */
static struct {
    uint32_t prev;                       /* the last tick's plat_ms */
    int32_t t;                           /* ms into the office's part of the day */
    int32_t slot_ms, end_ms;             /* its part of the day; where the chant ends */
    uint8_t phase, tolls_left, part, side, verses, psalm, amen_done, practised;
    uint8_t style, soloist;              /* this psalm's verses (PS_*), its soloist */
    int32_t next_toll, next_start;
    phrase_t ph[NPH];
    uint8_t phw;
    uint8_t inchoir[NMONK];              /* in the stalls this office */
    uint8_t ask[NMONK];                  /* a key asked him to sing the next verse */
    uint8_t low_said;                    /* the low part said this office */
    int8_t drone;
    /* what each voice sings now, and what the engine was last told */
    int16_t v_pitch[EN_NPAD], v_level[EN_NPAD], v_vowel[EN_NPAD];
    int16_t s_pitch[EN_NPAD], s_level[EN_NPAD], s_vowel[EN_NPAD];
    int8_t v_monk[EN_NPAD];              /* whose voice it is (-1 none) */
    uint8_t v_part[EN_NPAD];             /* his part (PT_*) */
    uint8_t s_monk[EN_NPAD];             /* the monk the voice was set up for (formant, pan...) + 1 */
    int8_t monk_slot[NMONK];             /* his voice while singing (-1 silent) */
    uint8_t monk_part[NMONK];            /* his part while singing */
    char line[48];                       /* the event line */
    uint32_t line_t;                     /* when it was written (plat_ms) */
    uint32_t nevents;                    /* events since boot (tests) */
    uint8_t dirty;                       /* the world changed since the last save */
    uint8_t urgent;                      /* a big event (an arrival, vows): kept at the hourly save */
    uint8_t said;                        /* a new event line for the screen reader */
    uint8_t practice;                    /* this interlude, a novice sings an antiphon alone */
    uint32_t dt;                         /* the last tick's ms */
#ifdef PLAT_HOST
    uint32_t h_gone;                     /* (tests) events of the old mistake kinds: none */
    uint32_t h_offmode, h_offtune, h_clash, h_notes;   /* (tests) notes outside the mode, out of tune, clashing */
    uint8_t h_log;                       /* (tests) print every note sung */
    int16_t h_last[EN_NPAD];
#endif
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

static monk_t *mk(int i) { return &proj.world.m[i]; }
static int here(int i) { return mk(i)->here == 1; }
static int in_choir(int i) { return here(i) && wr.inchoir[i]; }
static int is_novice(int i) { return (i == MK_NOVICE || i == MK_NEW) && !mk(i)->vowed; }
static int may_verse(int i) { return !is_novice(i) || mk(i)->part >= PART_VERSES; }
static int may_solo(int i) { return !is_novice(i) || mk(i)->part >= PART_SOLO; }

static int32_t day_ms(void) { return (int32_t)DAY_MIN_TAB[proj.daylen < WD_NDAY ? proj.daylen : 2] * 60000; }

/* ------------------------------------------------------------ the chronicle --- */
static const char *mname(int i) { return (unsigned)i < NMONK ? MONK_NAME[i] : "?"; }

static char *wcat(char *b, const char *s)
{
    while ((*b = *s++))
        b++;
    return b;
}

/* an event's words: "Br. Anselm takes the solo verse" (the line), at most 47 characters */
static void ev_text(int type, int who, int who2, int office, char *b)
{
    const char *n = mname(who), *n2 = mname(who2), *of = OFFICE[office % NOFFICE].name;
    char *p = b;
    *p = 0;
    if (type == EV_NONE || type >= NEV || (!EV_KEPT(type) && type != EV_NUDGE && type != EV_DAY))
        return;
    if (type != EV_ARRIVE && type != EV_TRADE && type != EV_DAY)
        p = wcat(wcat(p, who == MK_ABBOT ? "Abbot " : "Br. "), n);
    switch (type) {
    case EV_ARRIVE: wcat(wcat(wcat(p, "A new novice, "), n), ", came"); break;
    case EV_VOWS: wcat(p, " took his vows"); break;
    case EV_PRACTICE: wcat(p, " sings an antiphon alone"); break;
    case EV_NUDGE: wcat(p, " stood to sing"); break;
    case EV_INTONE: wcat(wcat(p, " intones "), of); break;
    case EV_CANTOR: wcat(p, " sings the antiphon"); break;
    case EV_SOLO: wcat(p, " sings solo verses"); break;
    case EV_TRADE: wcat(wcat(wcat(wcat(p, n), " and "), n2), " trade verses"); break;
    case EV_ORGANUM: wcat(p, " sings the fifth below"); break;
    case EV_ISON: wcat(p, " holds the ison"); break;
    case EV_VERSES: wcat(p, " now sings verses"); break;
    case EV_SOLOS: wcat(p, " is given solos"); break;
    case EV_COMES: wcat(wcat(p, " comes to "), of); break;
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

/* an event: on the line; the bigger ones kept in the chronicle */
static void event(int type, int who, int who2)
{
    char t[48];
    world_t *w = &proj.world;
    ev_text(type, who, who2, w->office, t);
    line_set(t);
#ifdef PLAT_HOST
    printf("  [day %u %s %d:%02d] %s\n", (unsigned)w->day, OFFICE[w->office].name, world_minutes() / 60,
           world_minutes() % 60, t);
    if (!EV_KEPT(type) && type != EV_NUDGE && type != EV_DAY)
        wr.h_gone++;
#endif
    wr.nevents++;
    wr.dirty = 1;
    if (type == EV_NUDGE || type == EV_DAY || type == EV_INTONE || type == EV_CANTOR || type == EV_ORGANUM ||
        type == EV_ISON || type == EV_COMES || type == EV_PRACTICE)
        return;
    w->chron[w->chron_w].day = w->day;
    w->chron[w->chron_w].office = w->office;
    w->chron[w->chron_w].type = (uint8_t)type;
    w->chron[w->chron_w].who = (uint8_t)who;
    w->chron[w->chron_w].who2 = (uint8_t)who2;
    w->chron_w = (uint8_t)((w->chron_w + 1) % NCHRON);
    if (w->nchron < NCHRON)
        w->nchron++;
    if (type == EV_ARRIVE || type == EV_VOWS || type == EV_VERSES || type == EV_SOLOS)
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
static int part_stage(int part) { return part >= PART_SOLO ? 2 : part >= PART_VERSES ? 1 : 0; }

void world_defaults(world_t *w)
{
    int i;
    memset(w, 0, sizeof *w);
    w->seed = 0x6D6F6E6Bu;
    w->day = 1;
    w->office = OF_LAUDS;                /* it starts at dawn */
    for (i = 0; i < NMONK; i++) {
        monk_t *m = &w->m[i];
        m->here = i == MK_NEW ? 0 : 1;
        m->part = i == MK_NOVICE ? 22 : i == MK_NEW ? 8 : 100;
        m->stage = (uint8_t)part_stage(m->part);
        m->days = i == MK_NOVICE ? 3 : i == MK_NEW ? 0 : (uint16_t)(400 + 900 * (i == MK_ELDER) + 37 * i);
    }
}

int world_valid(const world_t *w)
{
    int i;
    if (w->office >= NOFFICE || w->nchron > NCHRON || w->chron_w >= NCHRON || !w->day)
        return 0;
    for (i = 0; i < NMONK; i++)
        if (w->m[i].here > 1 || w->m[i].part > 100 || w->m[i].stage > 2)
            return 0;
    return 1;
}

/* a 1.0 or 1.1 world (format 1 or 2: the same layout) to 1.2's: the brothers, their days, what they have sung, the
 * novices' progress (their skill, now their part) and vows, the day and the office are kept. The infirmary, moods,
 * health, the rivalry and the histories of mistakes are gone; so are the chronicle's entries about them. */
void world_migrate(world_t *w)
{
    int i, k, n = 0;
    chron_t keep[NCHRON];
    for (i = 0; i < NMONK; i++) {
        monk_t *m = &w->m[i];
        if (m->here > 1)                 /* the infirmary: back in choir */
            m->here = 1;
        if (m->part > 100)
            m->part = 100;
        if (!((i == MK_NOVICE || i == MK_NEW) && !m->vowed))
            m->part = 100;
        m->stage = (uint8_t)part_stage(m->part);
        memset(m->rsv, 0, sizeof m->rsv);
        m->solos = m->cantor = 0;        /* (1.1: slips, naps) */
        m->rsv2[0] = m->rsv2[1] = 0;     /* (1.1: flats, clashes) */
    }
    w->rsv1[0] = w->rsv1[1] = 0;         /* (1.1: the rivalry, the peace) */
    w->rsv = 0;
    if (w->nchron > NCHRON || w->chron_w >= NCHRON) {
        w->nchron = w->chron_w = 0;
        return;
    }
    for (k = w->nchron - 1; k >= 0; k--) {   /* oldest first */
        const chron_t *c = &w->chron[(w->chron_w + NCHRON - 1 - k) % NCHRON];
        if (EV_KEPT(c->type) && c->who < NMONK && c->who2 < NMONK && c->office < NOFFICE)
            keep[n++] = *c;
    }
    memset(w->chron, 0, sizeof w->chron);
    for (k = 0; k < n; k++)
        w->chron[k] = keep[k];
    w->nchron = (uint8_t)n;
    w->chron_w = (uint8_t)(n % NCHRON);
}

/* a new day, at Matins: the novices are given more to sing, a newcomer may come */
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
        int p;
        if (!here(i) || !is_novice(i))
            continue;
        p = m->part + 2 + wirand(4) + (wr.practised ? 2 : 0);
        m->part = (uint8_t)(p > 100 ? 100 : p);
        if (part_stage(m->part) > m->stage) {
            m->stage = (uint8_t)part_stage(m->part);
            event(m->stage == 2 ? EV_SOLOS : EV_VERSES, i, 0);
        }
        if (m->part >= 85 && m->days >= 8) {
            m->vowed = 1;
            event(EV_VOWS, i, 0);
        }
    }
    wr.practised = 0;
    if (!mk(MK_NEW)->here && w->day >= 6 && (mk(MK_NOVICE)->vowed || w->day >= 12) && wchance(35)) {
        monk_t *m = mk(MK_NEW);
        m->here = 1;
        m->part = 8;
        m->stage = 0;
        m->days = 0;
        event(EV_ARRIVE, MK_NEW, 0);
    }
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
static int consonant(int semis)          /* unisons, thirds, fourths, fifths, sixths, octaves */
{
    semis %= 12;
    if (semis < 0)
        semis += 12;
    return semis == 0 || semis == 3 || semis == 4 || semis == 5 || semis == 7 || semis == 8 || semis == 9;
}

/* the organum's degree under the line's: a fifth below; on the mode's diminished fifth a fourth; else the octave */
static int org_deg(int d)
{
    if (deg_semi(d) - deg_semi(d - 4) == 7)
        return d - 4;
    if (deg_semi(d) - deg_semi(d - 3) == 5)
        return d - 3;
    return d - nsteps7;
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
static int syl(int pace, int pct) { return pace * pct / 100 * (90 + wirand(21)) / 100; }   /* the text's rhythm */
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
    case K_PRACTICE: {
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

/* the degree singer j sings on note i (REST: none) */
static int sung_deg(const phrase_t *p, int j, int i)
{
    int d = p->deg[i];
    if (d == REST)
        return REST;
    if (p->part[j] == PT_ISON)
        return 0;
    if (p->part[j] == PT_ORGANUM)
        return org_deg(d);
    return d;
}

/* his octave for his part of this line: the one where it fits his range best */
static int fit_octave(const phrase_t *p, int j, int m)
{
    int lo = 127, hi = 0, i, best = 0, bo = 1 << 20, o, f = of_final(proj.world.office);
    for (i = 0; i < p->n; i++) {
        int d = sung_deg(p, j, i);
        if (d != REST) {
            int n = f + deg_semi(d);
            lo = n < lo ? n : lo;
            hi = n > hi ? n : hi;
        }
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

static int add_who(uint8_t *who, int n, int m)
{
    int j;
    for (j = 0; j < n; j++)
        if (who[j] == m)
            return n;
    who[n] = (uint8_t)m;
    return n + 1;
}

/* the singers for a line of this kind, from the brothers in choir */
static int pick(int kind, uint8_t *who)
{
    int n = 0, i, cap = OFFICE[proj.world.office].singers, cand[NMONK], nc = 0;
    if (kind == K_INTONE) {
        who[0] = in_choir(MK_ABBOT) ? MK_ABBOT : in_choir(MK_GREGORY) ? MK_GREGORY : MK_BERNARD;
        return 1;
    }
    if (kind == K_PRACTICE) {
        for (i = 0; i < NMONK; i++)
            if (here(i) && is_novice(i) && mk(i)->part >= PART_VERSES)
                cand[nc++] = i;
        if (!nc)
            return 0;
        who[0] = (uint8_t)cand[wirand(nc)];
        return 1;
    }
    if (kind == K_VERSE) {
        for (i = 0; i < NMONK; i++)      /* one a key asked for sings it alone */
            if (wr.ask[i] && in_choir(i)) {
                wr.ask[i] = 0;
                who[0] = (uint8_t)i;
                mk(i)->solos++;
                return 1;
            }
        if (wr.style == PS_TRADE) {      /* the two cantors, verse about */
            who[0] = (uint8_t)(wr.side ? MK_BERNARD : MK_GREGORY);
            return 1;
        }
        if (wr.style == PS_SOLO && !wr.side) {   /* the soloist; the choir answers */
            who[0] = wr.soloist;
            mk(wr.soloist)->solos++;
            return 1;
        }
        if (wr.style == PS_SOLO) {
            for (i = 0; i < NMONK; i++)
                if (in_choir(i) && i != wr.soloist && may_verse(i))
                    cand[nc++] = i;
            cap = cap >= 4 ? 3 : cap;
        } else {                         /* a side of the choir */
            int side = wr.side, tries;
            for (tries = 0; tries < 2 && !nc; tries++, side ^= 1)
                for (i = 0; i < NMONK; i++)
                    if (in_choir(i) && MONK_ROW[i] == side && may_verse(i))
                        cand[nc++] = i;
            cap = cap >= 4 ? 2 : cap == 3 ? 1 + wirand(2) : 1;
        }
    } else {
        for (i = 0; i < NMONK; i++)
            if (in_choir(i))
                cand[nc++] = i;
        if (kind == K_ANTI && wr.part == 2) {   /* the antiphon's first time: a cantor */
            int c = wirand(10) < 4 ? MK_GREGORY : wirand(10) < 6 ? MK_BERNARD : MK_ABBOT;
            if (!in_choir(c))
                c = in_choir(MK_GREGORY) ? MK_GREGORY : in_choir(MK_BERNARD) ? MK_BERNARD : MK_ABBOT;
            for (i = 0; i < NMONK; i++)  /* a novice who took his vows has his turn too */
                if ((i == MK_NOVICE || i == MK_NEW) && in_choir(i) && mk(i)->vowed && wchance(20))
                    c = i;
            who[0] = (uint8_t)c;
            return 1;
        }
        if (kind == K_AMEN || kind == K_RESP)
            cap = cap < 4 ? cap + 1 : 4;
        if (in_choir(MK_ABBOT))
            n = add_who(who, n, MK_ABBOT);
        if (OFFICE[proj.world.office].low != LOW_UNISON && n < cap) {   /* the low part's singer */
            int lw = OFFICE[proj.world.office].low == LOW_ISON ? MK_COLUMBA : MK_ELDER;
            if (!in_choir(lw))
                lw = lw == MK_COLUMBA ? MK_ELDER : MK_COLUMBA;
            if (in_choir(lw))
                n = add_who(who, n, lw);
        }
    }
    while (n < cap && nc) {              /* the rest at random */
        int k = wirand(nc);
        n = add_who(who, n, cand[k]);
        cand[k] = cand[--nc];
    }
    return n;
}

/* a new line, from t0: its singers, their parts, their voices */
static void ph_start(int kind, int32_t t0)
{
    phrase_t *p = &wr.ph[wr.phw];
    const int low = OFFICE[proj.world.office].low;
    int j, s, free_n = 0, tutti;
    int8_t free_s[EN_NPAD];
    wr.phw = (uint8_t)((wr.phw + 1) % NPH);
    memset(p, 0, sizeof *p);
    ph_build(p, kind);
    p->t0 = t0;
    p->nsing = (uint8_t)pick(kind, p->who);
    for (s = 0; s < EN_NPAD; s++)
        if (!voice_busy(s, (int)(p - wr.ph)))
            free_s[free_n++] = (int8_t)s;
    if (p->nsing > free_n)
        p->nsing = (uint8_t)free_n;
    p->used = p->nsing > 0;
    tutti = kind == K_RESP || kind == K_AMEN || (kind == K_ANTI && wr.part != 2);
    for (j = 0; j < p->nsing; j++) {     /* the low part in the choruses: a bass, while others sing the line */
        int m = p->who[j];
        if (tutti && low != LOW_UNISON && p->nsing >= 2 && MONK_RANGE[m] == RG_BASS) {
            p->part[j] = (uint8_t)(low == LOW_ISON ? PT_ISON : PT_ORGANUM);
            if (!wr.low_said) {
                wr.low_said = 1;
                event(low == LOW_ISON ? EV_ISON : EV_ORGANUM, m, 0);
            }
            break;
        }
    }
    for (j = 0; j < p->nsing; j++) {
        int m = p->who[j];
        p->slot[j] = free_s[j];
        p->shift[j] = (int8_t)fit_octave(p, j, m);
        p->level[j] = MONK_LEVEL[m];
        if (kind == K_PRACTICE)
            p->level[j] = (uint16_t)(p->level[j] * 6 / 10);
        if (is_novice(m))                /* a young voice, filling out */
            p->level[j] = (uint16_t)(p->level[j] * (85 + mk(m)->part * 15 / 100) / 100);
        if (p->part[j] == PT_ISON)
            p->level[j] = (uint16_t)(p->level[j] * 7 / 10);
        else if (p->part[j] == PT_ORGANUM)
            p->level[j] = (uint16_t)(p->level[j] * 85 / 100);
        mk(m)->sung++;
    }
    if (kind == K_INTONE && p->used) {
        mk(p->who[0])->cantor++;
        event(EV_INTONE, p->who[0], 0);
    } else if (kind == K_ANTI && wr.part == 2 && p->used) {
        mk(p->who[0])->cantor++;
        event(EV_CANTOR, p->who[0], 0);
    }
}

/* how the next psalm's verses go: the sides, the cantors trading, or a soloist answered by the choir */
static void psalm_style(void)
{
    const int o = proj.world.office;
    int i, nc = 0, cand[NMONK], r = wirand(100);
    wr.style = PS_SIDES;
    wr.side = 0;
    if (in_choir(MK_GREGORY) && in_choir(MK_BERNARD) && r < 25) {
        wr.style = PS_TRADE;
        event(EV_TRADE, MK_GREGORY, MK_BERNARD);
        return;
    }
    if (r >= 25 && r < (OFFICE[o].choir >= 5 ? 50 : 40)) {
        for (i = 0; i < NMONK; i++)
            if (in_choir(i) && i != MK_ABBOT && may_solo(i))
                cand[nc++] = i;
        for (i = 0; i < NMONK; i++)      /* a novice ready for it is given it first */
            if (in_choir(i) && is_novice(i) && may_solo(i) && wchance(60))
                nc = 1, cand[0] = i;
        if (nc >= 1 && OFFICE[o].choir >= 3) {
            wr.style = PS_SOLO;
            wr.soloist = (uint8_t)cand[wirand(nc)];
            event(EV_SOLO, wr.soloist, 0);
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

/* who is in the stalls for this office: all at Lauds and Vespers, fewer at the little hours. The abbot always, the
 * office's low voice, the novices more as their part grows; the rest by turns */
static void choir_pick(void)
{
    const int o = proj.world.office;
    int i, n = 0, want = OFFICE[o].choir, cand[NMONK], nc = 0;
    for (i = 0; i < NMONK; i++)
        wr.inchoir[i] = 0;
    for (i = 0; i < NMONK; i++)
        if (here(i))
            nc++;
    if (want >= nc) {
        for (i = 0; i < NMONK; i++)
            wr.inchoir[i] = (uint8_t)here(i);
        return;
    }
    nc = 0;
    if (here(MK_ABBOT))
        wr.inchoir[MK_ABBOT] = 1, n++;
    if (OFFICE[o].low == LOW_ISON && here(MK_COLUMBA))
        wr.inchoir[MK_COLUMBA] = 1, n++;
    for (i = 0; i < NMONK; i++)
        if (here(i) && is_novice(i) && !wr.inchoir[i] && n < want && wirand(100) < 30 + mk(i)->part / 2)
            wr.inchoir[i] = 1, n++;
    for (i = 0; i < NMONK; i++)
        if (here(i) && !wr.inchoir[i] && !is_novice(i))
            cand[nc++] = i;
    while (n < want && nc) {
        int k = wirand(nc);
        wr.inchoir[cand[k]] = 1, n++;
        cand[k] = cand[--nc];
    }
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
    wr.low_said = 0;
    wr.style = PS_SIDES;
    for (i = 0; i < NPH; i++)
        wr.ph[i].used = 0;
    nsteps7 = en_scale(EN_MODE_POOL[of_mode(o)], steps7);
    choir_pick();
    wr.drone = -1;
    en_drone(-1);
    world_key();
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
        if (wr.practice && wr.t > wr.end_ms + 15000 && wr.t < wr.slot_ms - 20000) {   /* a novice sings alone */
            wr.practice = 0;
            ph_start(K_PRACTICE, wr.t + 200);
            if (wr.ph[(wr.phw + NPH - 1) % NPH].used) {
                int m = wr.ph[(wr.phw + NPH - 1) % NPH].who[0];
                wr.practised = 1;
                event(EV_PRACTICE, m, 0);
            }
        }
        return;
    }
    if (wr.amen_done) {                  /* the Amen sung: the interlude when it has rung out */
        if (wr.t >= last->t0 + (int32_t)last->at[last->n] + 800) {
            wr.phase = PH_REST;
            wr.practice = (uint8_t)(o != OF_COMPLINE && wchance(55));
            wr.drone = -1;
            en_drone(-1);
            if (o == OF_VESPERS || o == OF_LAUDS)
                en_toll(74, 22);         /* the abbot's handbell */
        }
        return;
    }
    if (wr.t + 1500 < wr.next_start)
        return;
    {
        int kind = next_kind(), gap;
        phrase_t *p;
        if (kind != K_VERSE && last->used && wr.next_start < last->t0 + (int32_t)last->at[last->n] + 900) {
            wr.next_start = last->t0 + (int32_t)last->at[last->n] + 900;   /* everyone free first */
            return;
        }
        ph_start(kind, wr.next_start);
        p = &wr.ph[(wr.phw + NPH - 1) % NPH];
        if (kind == K_VERSE && last->used && last != p) {   /* over the last verse's held final: only in harmony */
            int32_t end = last->t0 + (int32_t)last->at[last->n];
            int i;
            for (i = 0; i < p->n && p->t0 + (int32_t)p->at[i] < end; i++)
                if (p->deg[i] != REST && !consonant(deg_semi(p->deg[i]))) {
                    p->t0 = end + 150 - (int32_t)p->at[i];   /* that note waits for the final's end */
                    break;
                }
        }
        last = p;
        switch (kind) {                  /* what follows, and when */
        case K_INTONE: wr.part = 1; gap = 500; break;
        case K_RESP: wr.part = 2; gap = 1200; break;
        case K_ANTI:
            if (wr.part == 2) {
                wr.part = 3;
                wr.verses = (uint8_t)(4 + wirand(5));
                psalm_style();
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
        if (!last->used && kind != K_AMEN)   /* nobody to sing it: on */
            wr.next_start = wr.t + 2000;
    }
}

#ifdef PLAT_HOST
/* (tests) a musician's ear on what is sung now: every note in the mode, in tune, the voices in harmony (the ison, a
 * held final like the drone, aside) */
static void host_listen(int f)
{
    int s, s2;
    for (s = 0; s < EN_NPAD; s++) {
        int pc, ok = 0, k, c;
        if (wr.v_level[s] <= 0)
            continue;
        c = (wr.v_pitch[s] % 100 + 150) % 100 - 50;
        pc = ((wr.v_pitch[s] - c) / 100 - f) % 12;
        pc = pc < 0 ? pc + 12 : pc;
        for (k = 0; k < nsteps7; k++)
            ok |= steps7[k] == pc;
        wr.h_offmode += !ok;
        wr.h_offtune += c > 4 || c < -4;
        if (wr.v_part[s] == PT_ISON)
            continue;
        for (s2 = s + 1; s2 < EN_NPAD; s2++)
            if (wr.v_level[s2] > 0 && wr.v_part[s2] != PT_ISON &&
                !consonant((wr.v_pitch[s] + 50) / 100 - (wr.v_pitch[s2] + 50) / 100)) {
                if (wr.h_log && (wr.h_clash % 50) == 0)
                    printf("CLASH t=%d v%d %s %d / v%d %s %d\n", (int)wr.t, s, mname(wr.v_monk[s]),
                           (wr.v_pitch[s] + 50) / 100, s2, mname(wr.v_monk[s2]), (wr.v_pitch[s2] + 50) / 100);
                wr.h_clash++;
            }
    }
}
#endif

/* what each voice sings at office time t */
static void voices_step(void)
{
    const int f = of_final(proj.world.office);
    int s, k, j;
    for (s = 0; s < EN_NPAD; s++) {
        wr.v_level[s] = 0;
        wr.v_monk[s] = -1;
        wr.v_part[s] = PT_MELODY;
    }
    for (j = 0; j < NMONK; j++)
        wr.monk_slot[j] = -1;
    for (k = 0; k < NPH; k++) {
        phrase_t *p = &wr.ph[k];
        int32_t pt = wr.t - p->t0;
        if (!p->used || pt < -3000 || pt > (int32_t)p->at[p->n] + 1500)
            continue;
        for (j = 0; j < p->nsing; j++) {
            int m = p->who[j], sl = p->slot[j], i, deg;
            if (sl < 0)
                continue;
            if (wr.v_monk[sl] < 0 || pt < (int32_t)p->at[p->n] + 200)
                wr.v_monk[sl] = (int8_t)m;   /* his voice (its tail too, while no one else takes it) */
            if (pt < 0 || pt >= (int32_t)p->at[p->n])
                continue;
            for (i = 0; i + 1 < p->n && (int32_t)p->at[i + 1] <= pt; i++)
                ;
            deg = sung_deg(p, j, i);
            if (deg == REST)
                continue;
            wr.v_pitch[sl] = (int16_t)((f + deg_semi(deg) + p->shift[j]) * 100 + MONK_CENTS[m]);
            wr.v_level[sl] = (int16_t)p->level[j];
            wr.v_vowel[sl] = (int16_t)(p->part[j] == PT_ISON ? (20 * 6 + MONK_VOWEL[m] * 4)
                                                             : (p->vow[i] * 6 + MONK_VOWEL[m] * 4));
            wr.v_monk[sl] = (int8_t)m;
            wr.v_part[sl] = p->part[j];
            wr.monk_slot[m] = (int8_t)sl;
            wr.monk_part[m] = p->part[j];
        }
    }
#ifdef PLAT_HOST
    host_listen(f);
    for (s = 0; s < EN_NPAD; s++) {
        int16_t key = wr.v_level[s] > 0 ? wr.v_pitch[s] : -1;
        if (key != wr.h_last[s] && key >= 0) {
            static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            int n = (key + 50) / 100;
            wr.h_notes++;
            if (wr.h_log)
                printf("NOTE %u %s %d.%03d v%d %-8s %-6s %s%d\n", (unsigned)proj.world.day,
                       OFFICE[proj.world.office].name, (int)(wr.t / 1000), (int)(wr.t % 1000), s,
                       mname(wr.v_monk[s]), wr.v_part[s] == PT_ISON ? "ison" : wr.v_part[s] == PT_ORGANUM ? "organ" : "line",
                       NN[n % 12], n / 12 - 1);
        }
        wr.h_last[s] = key;
    }
#endif
    for (s = 0; s < EN_NPAD; s++) {      /* to the engine: what changed */
        int m = wr.v_monk[s];
        if (m >= 0 && wr.s_monk[s] != m + 1) {
            int pan = (MONK_ROW[m] ? 30 : -30) + (MONK_POS[m] * 2 - 3) * 18;
            wr.s_monk[s] = (uint8_t)(m + 1);
            en_voice(s, VF_FORMANT, preset_formant(MONK_FORMANT[m]));
            en_voice(s, VF_PAN, pan);
            en_voice(s, VF_VIB, preset_vib(MONK_VIB[m]));
            en_voice(s, VF_GLIDE, 3);
            if (wr.s_level[s] <= 0)      /* from silence: no glide from the last singer's note */
                en_voice(s, VF_GLIDE, 100);
        }
        if (wr.v_level[s] > 0 && wr.v_pitch[s] != wr.s_pitch[s]) {
            en_voice(s, VF_PITCH, wr.v_pitch[s]);
            if (wr.s_level[s] <= 0 && m >= 0)
                en_voice(s, VF_GLIDE, 3);
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
#ifdef PLAT_HOST
        wr.h_last[s] = -1;
#endif
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
    for (s = 0; s < NPH; s++)
        wr.ph[s].used = 0;
    next_office();
}

/* a key: brother i stands to sing the next verse alone; one not in choir this office comes to it first */
void world_nudge(int i)
{
    if ((unsigned)i >= NMONK || !here(i))
        return;
    if (!wr.inchoir[i]) {
        wr.inchoir[i] = 1;
        event(EV_COMES, i, 0);
    }
    if (is_novice(i) && !may_verse(i)) {  /* not yet: he sings with the choir */
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
