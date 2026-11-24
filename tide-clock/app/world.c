/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen */
/* TIDE CLOCK's world: a small coast that keeps the time of day, and the conductor that turns it into the music.
 * Part of the unity build, after project.c and before ui.c.
 *
 * Time: the world's clock is a time of day in ms. Clock mode: it runs at the real pace from the time set at power-on
 * (the FM-1 has no real-time clock: plat_ms only). Cycle mode: a whole day every CYCLE_H hours.
 * Every world minute the world takes a step: the weather (calm, fog, wind, now and then a storm), the boats, the
 * keeper's mood, Captain the gull, the log. What can be worked out from the time alone (the sun, the tide, where the
 * boats are, what the keeper is doing, the lamp) is worked out, so a clock set by hand never leaves the world wrong.
 *
 * The music: the day's eight phases are recipes (the engine's pieces, Night .. Dusk). At any time the targets are a
 * smooth blend of the two phases around it, bent by the weather, the tide and the keeper's mood; the engine's
 * values step toward them a little at a time (never a jump). The key, the mode, the pad's colour and the octaves
 * change together, on a new chord, when the phase changes (each phase's key is one note from the last). */

#include "fastmath.h"

static const uint8_t CYCLE_H[NCYCLE] = {1, 2, 4, 8, 12};
#define DAY_MS 86400000u
#define MIN_MS 60000u

/* the phases (the engine's pieces): where each starts and where its recipe is whole, in minutes of the day */
enum { PH_NIGHT, PH_PREDAWN, PH_DAWN, PH_MORNING, PH_MIDDAY, PH_AFTERNOON, PH_EVENING, PH_DUSK, NPH };
static const uint16_t PH_START[NPH] = {1320, 240, 330, 420, 660, 840, 1020, 1170};
static const int16_t PH_MID[NPH + 2] = {-195, 60, 285, 375, 540, 750, 930, 1095, 1245, 1500};
static const uint8_t PH_OF_MID[NPH + 2] = {PH_DUSK, PH_NIGHT, PH_PREDAWN, PH_DAWN, PH_MORNING, PH_MIDDAY,
                                           PH_AFTERNOON, PH_EVENING, PH_DUSK, PH_NIGHT};

static const char *const BOAT_NAME[NBOATS] = {"Petrel", "Kittiwake", "Mary Ann"};
static const char *const WX_NAME[NWX] = {"Calm", "Fog", "Wind", "Storm"};
#define KEEPER "Abel"

/* the keeper's activities */
enum { KA_SLEEP, KA_TEA, KA_WATCH_OUT, KA_LAMP_OUT, KA_CHORE, KA_GULLS, KA_LUNCH, KA_LOG, KA_BEACH, KA_WALK,
       KA_LAMP_LIT, KA_WAIT_BOATS, KA_READ, KA_STORM, KA_WORRY, KA_HORN, NKA };
/* where he is: in the cottage (not seen), on the gallery, at the cliff edge, by the lighthouse, on the beach, by the post */
enum { AT_HOME, AT_GALLERY, AT_EDGE, AT_TOWER, AT_BEACH, AT_POST };

/* the engine's values the world sets: all but the player's (Tune, MIDI out) */
static int world_par(int p) { return p != P_TUNE && p != P_MIDI; }
static int discrete_par(int p)
{
    return p == P_ROOT || p == P_MODE || p == P_BOCT || p == P_PADTONE || p == P_BASSOCT || p == P_ETIME ||
           p == P_TEXTURE || p == P_PLOCT || p == P_LDOCT;
}

static struct {
    uint32_t ms;                       /* the time of day, ms */
    uint32_t prev;                     /* the last tick's plat_ms */
    float fog, wind, storm;            /* the weather's amounts, 0..1, easing toward the weather (40 world minutes) */
    int16_t tgt[P_NPARAMS];            /* the targets */
    int16_t cur[P_NPARAMS];            /* what the engine has */
    uint32_t slew_t, swap_t;           /* the last step toward the targets; the last change of key */
    uint8_t settled;                   /* the engine has been given everything once */
    uint8_t want_save;                 /* something worth keeping happened */
    uint8_t new_event;                 /* a log entry since the UI last looked */
    uint8_t phase;                     /* the phase the key is from */
    uint32_t quick_t;                  /* a new preset: the values go to it quickly until then */
} w;

/* ----------------------------------------------------------- the dice --- */
static uint32_t wrand(void)                    /* xorshift on the world's seed (saved: the world goes on) */
{
    uint32_t x = proj.seed ? proj.seed : 0x7D1DEC0Cu;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    proj.seed = x;
    return x;
}
static int wchance(uint32_t per_mille) { return wrand() % 1000u < per_mille; }
static uint32_t wrange(uint32_t lo, uint32_t hi) { return lo + wrand() % (hi - lo + 1u); }

/* ------------------------------------------------------------- the time --- */
static uint32_t world_min(void) { return w.ms / MIN_MS; }
static int phase_at(uint32_t m)
{
    int p, best = PH_NIGHT;
    for (p = PH_PREDAWN; p < NPH; p++)
        if (m >= PH_START[p])
            best = p;
    return m >= PH_START[PH_NIGHT] ? PH_NIGHT : best;
}
static int world_phase(void) { return phase_at(world_min()); }
static const char *phase_name(int p) { return en_piece_name(p); }

/* the tide: two a day, 12 h 25 min apart, a little later each day; 0 low .. 1 high */
#define TIDE_MIN 745u
static float tide_at(uint32_t day, uint32_t ms)
{
    uint32_t m = ((day % TIDE_MIN) * 1440u + ms / MIN_MS) % TIDE_MIN;
    float f = ((float)m + (float)(ms % MIN_MS) * (1.0f / 60000.0f)) * (1.0f / (float)TIDE_MIN);
    return 0.5f + 0.5f * fm_cosf(FM_TWO_PI * f);
}
static float world_tide(void) { return tide_at(proj.day, w.ms); }
static int tide_rising(void)
{
    return tide_at(proj.day, (w.ms + 5u * MIN_MS) % DAY_MS) > world_tide();
}

/* the sun: up 6:00 .. 19:00; the moon: 19:30 .. 5:30. 0..1 along its arc, < 0 not up */
static float sun_arc(uint32_t m) { return m >= 360u && m <= 1140u ? (float)(m - 360u) / 780.0f : -1.0f; }
static float moon_arc(uint32_t m)
{
    uint32_t k = m >= 1170u ? m - 1170u : m <= 330u ? m + 270u : 9999u;
    return k <= 600u ? (float)k / 600.0f : -1.0f;
}

/* ------------------------------------------------------------ the boats --- */
#define BOAT_OUT(i) (345u + 20u * (uint32_t)(i))       /* leaving at dawn, 50 min to the grounds */
#define BOAT_BACK(i) (1110u + 15u * (uint32_t)(i))     /* back at dusk, 45 min in */
/* where boat i is: 0 in harbour .. 1 at the fishing grounds; -1 not here (sheltered elsewhere) */
static float boat_at(int i, uint32_t m)
{
    if (proj.sheltered == i + 1)
        return -1.0f;
    if (proj.late == i + 1)
        return 1.0f;
    if (proj.stayed >> i & 1u)
        return 0.0f;
    if (m < BOAT_OUT(i))
        return 0.0f;
    if (m < BOAT_OUT(i) + 50u)
        return (float)(m - BOAT_OUT(i)) / 50.0f;
    if (m < BOAT_BACK(i))
        return 1.0f;
    if (m < BOAT_BACK(i) + 45u)
        return 1.0f - (float)(m - BOAT_BACK(i)) / 45.0f;
    return 0.0f;
}
static int boats_out(void)
{
    int i, n = 0;
    for (i = 0; i < NBOATS; i++)
        n += boat_at(i, world_min()) > 0.0f;
    return n;
}

/* ------------------------------------------------------------ the keeper --- */
static int lamp_lit(void)
{
    uint32_t m = world_min();
    return m >= 1110u || m < 390u || w.fog > 0.5f || w.storm > 0.5f;
}

static int keeper_act(void)
{
    uint32_t m = world_min();
    int awake_storm = w.storm > 0.5f;
    if (proj.late)
        return KA_WORRY;
    if (m < 330u || m >= 1350u)
        return awake_storm ? KA_STORM : KA_SLEEP;
    if (awake_storm)
        return KA_STORM;
    if (w.fog > 0.5f && m >= 420u && m < 1110u && (m / 30u) % 2u == 0u)
        return KA_HORN;
    if (m < 360u)
        return KA_TEA;
    if (m < 380u)
        return KA_WATCH_OUT;
    if (m < 400u)
        return KA_LAMP_OUT;
    if (m < 420u)
        return KA_WATCH_OUT;
    if (m < 600u)
        return KA_CHORE;
    if (m < 690u)
        return w.storm < 0.3f ? KA_GULLS : KA_CHORE;
    if (m < 735u)
        return KA_LUNCH;
    if (m < 780u)
        return KA_LOG;
    if (m < 960u)
        return world_tide() < 0.4f && w.fog < 0.3f && w.wind < 0.6f ? KA_BEACH : KA_CHORE;
    if (m < 1095u)
        return KA_WALK;
    if (m < 1115u)
        return KA_LAMP_LIT;
    if (m < 1200u && boats_out())
        return KA_WAIT_BOATS;
    return KA_READ;
}

static int keeper_where(int a)
{
    switch (a) {
    case KA_LAMP_OUT: case KA_LAMP_LIT: case KA_STORM: case KA_WORRY: case KA_HORN: return AT_GALLERY;
    case KA_WATCH_OUT: case KA_WAIT_BOATS: case KA_WALK: return AT_EDGE;
    case KA_CHORE: return AT_TOWER;
    case KA_BEACH: return AT_BEACH;
    case KA_GULLS: return AT_POST;
    default: return AT_HOME;
    }
}

/* what he is doing, in words ("lighting the lamp"); b: 32 bytes */
static void keeper_doing(char *b)
{
    static const char *const CHORES[4] = {"painting the rail", "mending nets", "polishing the lens", "tending the garden"};
    static const char *const T[NKA] = {"asleep", "making tea", "seeing the boats out", "putting out the lamp", 0,
                                       "feeding the gulls", "having lunch", "writing the log", "beachcombing",
                                       "walking the cliff", "lighting the lamp", "counting boats home", "reading",
                                       "watching the storm", 0, "sounding the horn"};
    int a = keeper_act();
    const char *s = a == KA_CHORE ? CHORES[proj.day % 4u] : T[a];
    uint32_t k = 0;
    if (a == KA_WORRY) {
        const char *p = "waiting up for the ", *q = BOAT_NAME[(proj.late - 1u) % NBOATS];
        while (*p)
            b[k++] = *p++;
        while (*q && k < 31u)
            b[k++] = *q++;
        b[k] = 0;
        return;
    }
    while (s && *s && k < 31u)
        b[k++] = *s++;
    b[k] = 0;
}

static const char *keeper_mood(void)
{
    int a = keeper_act();
    if (a == KA_SLEEP)
        return "asleep";
    if (a == KA_WORRY)
        return "worried";
    if (a == KA_STORM)
        return proj.mood < 40 ? "anxious" : "watchful";
    return proj.mood >= 78 ? "cheerful" : proj.mood >= 56 ? "content" : proj.mood >= 38 ? "pensive" :
           proj.mood >= 22 ? "weary" : "low";
}

/* -------------------------------------------------------------- the log --- */
static void log_add(int type, int arg)
{
    log_t *e = &proj.log[proj.log_at];
    e->day = proj.day;
    e->min = (uint16_t)world_min();
    e->type = (uint8_t)type;
    e->arg = (uint8_t)arg;
    proj.log_at = (uint8_t)((proj.log_at + 1u) % LOG_N);
    if (proj.log_n < LOG_N)
        proj.log_n++;
    w.want_save = 1;
    w.new_event = 1;
}
static const log_t *log_get(int back)          /* 0 = the newest */
{
    if (back < 0 || back >= proj.log_n)
        return 0;
    return &proj.log[(proj.log_at + LOG_N - 1u - (uint32_t)back) % LOG_N];
}

static char *cat_s(char *b, const char *s)      /* s onto b; the end */
{
    while ((*b = *s++))
        b++;
    return b;
}
static char *cat_u(char *b, uint32_t v)
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
    return b;
}

static void event_text(const log_t *e, char *b)  /* b: 40 bytes */
{
    static const char *const SHELL[5] = {"a cowrie", "a whelk", "sea glass", "a razor shell", "a sand dollar"};
    static const char *const CAPT[4] = {"Captain came close", "Captain ate from his hand", "Captain waits by the door",
                                        "Captain is family now"};
    const char *boat = BOAT_NAME[e->arg % NBOATS];
    b[0] = 0;
    switch (e->type) {
    case EV_NEWDAY: cat_s(b, "Back after a night away"); break;
    case EV_FOG: cat_s(b, "Fog rolled in"); break;
    case EV_FOG_LIFT: cat_s(b, "The fog lifted"); break;
    case EV_WIND: cat_s(b, "Wind got up"); break;
    case EV_STORM: cat_s(b, "Storm came in"); break;
    case EV_STORM_END: cat_s(cat_u(cat_s(b, "Storm passed after "), e->arg), e->arg == 1 ? " hour" : " hours"); break;
    case EV_BOAT_LATE: cat_s(cat_s(b, boat), " late home"); break;
    case EV_BOAT_HOME: cat_s(cat_s(b, boat), " home safe"); break;
    case EV_BOAT_SHELTER: cat_s(cat_s(b, boat), " sheltered at Porthkell"); break;
    case EV_SHELL: cat_s(cat_s(b, "Found "), SHELL[e->arg % 5u]); break;
    case EV_CAPTAIN: cat_s(b, CAPT[e->arg % 4u]); break;
    case EV_LIGHTNING: cat_s(b, "Lightning struck the point"); break;
    case EV_MILESTONE: cat_s(cat_u(b, e->day), " days on the point"); break;
    case EV_CLEAR_NIGHT: cat_s(b, "Clear night, all the stars"); break;
    default: break;
    }
}

/* ------------------------------------------------------------ the weather --- */
static void weather_set(int wx, uint32_t mins)
{
    int was = proj.wx;
    proj.wx = (uint8_t)wx;
    proj.wx_left = (uint16_t)(mins > 65535u ? 65535u : mins);
    if (wx == was)
        return;
    if (was == WX_STORM) {
        log_add(EV_STORM_END, (int)((proj.storm_min + 30u) / 60u) ? (int)((proj.storm_min + 30u) / 60u) : 1);
        proj.mood = (uint8_t)(proj.mood + 12u > 100u ? 100u : proj.mood + 12u);
    }
    if (was == WX_FOG && proj.fogs)
        log_add(EV_FOG_LIFT, 0);
    if (wx == WX_STORM) {
        proj.storms++;
        proj.storm_min = 0;
        log_add(EV_STORM, 0);
    } else if (wx == WX_FOG) {
        proj.fogs++;
        log_add(EV_FOG, 0);
    } else if (wx == WX_WIND && was == WX_CALM) {
        log_add(EV_WIND, 0);
    }
}

static void weather_step(uint32_t m)
{
    uint32_t r;
    if (proj.wx == WX_STORM)
        proj.storm_min++;
    if (proj.wx_left > 1u) {
        proj.wx_left--;
        return;
    }
    r = wrand() % 1000u;
    switch (proj.wx) {
    case WX_CALM:
        if (r < 70u)
            weather_set(WX_STORM, wrange(90, 210));
        else if (r < ((m >= 120u && m < 600u) ? 420u : 250u))
            weather_set(WX_FOG, wrange(90, 240));
        else if (r < 480u)
            weather_set(WX_WIND, wrange(120, 300));
        else
            weather_set(WX_CALM, wrange(180, 540));
        break;
    case WX_WIND:
        if (r < 200u)
            weather_set(WX_STORM, wrange(90, 210));
        else
            weather_set(WX_CALM, wrange(180, 540));
        break;
    case WX_FOG: weather_set(r < 800u ? WX_CALM : WX_WIND, r < 800u ? wrange(180, 540) : wrange(120, 300)); break;
    default: weather_set(WX_WIND, wrange(60, 150)); break;   /* a storm blows itself out to a wind */
    }
}

static void weather_ease(float k)              /* the amounts toward the weather; k: the share to go */
{
    float tf = proj.wx == WX_FOG ? 1.0f : 0.0f, tw = proj.wx == WX_WIND ? 1.0f : proj.wx == WX_STORM ? 0.6f : 0.0f;
    float ts = proj.wx == WX_STORM ? 1.0f : 0.0f;
    w.fog += (tf - w.fog) * k;
    w.wind += (tw - w.wind) * k;
    w.storm += (ts - w.storm) * k;
}

/* --------------------------------------------------------- a world minute --- */
static void new_day(void)
{
    proj.day = (uint16_t)(proj.day < 65535u ? proj.day + 1u : 65535u);
    proj.stayed = 0;
    if (proj.day % 10u == 0u)
        log_add(EV_MILESTONE, 0);
    w.want_save = 1;
}

static void minute(void)
{
    uint32_t m = world_min();
    int i, a, tgt;
    weather_step(m);
    weather_ease(1.0f / 40.0f);
    for (i = 0; i < NBOATS; i++) {                  /* the boats: out at dawn unless it storms; home at dusk */
        if (m == BOAT_OUT(i) && w.storm > 0.5f && !(proj.stayed >> i & 1u))
            proj.stayed |= (uint8_t)(1u << i);
        if (m == BOAT_BACK(i) + 44u && !proj.late && !(proj.stayed >> i & 1u) && proj.sheltered != i + 1 &&
            wchance(w.storm > 0.3f ? 450u : w.wind > 0.6f ? 120u : 8u)) {
            proj.late = (uint8_t)(i + 1);
            proj.lates++;
            log_add(EV_BOAT_LATE, i);
        }
    }
    if (proj.late) {                                /* a late boat: home at last, or sheltered for the night */
        if (m >= 1200u && wchance(w.storm > 0.5f ? 8u : 18u)) {
            log_add(EV_BOAT_HOME, proj.late - 1);
            proj.late = 0;
            proj.mood = (uint8_t)(proj.mood + 15u > 100u ? 100u : proj.mood + 15u);
        } else if (m == 1410u) {
            log_add(EV_BOAT_SHELTER, proj.late - 1);
            proj.sheltered = proj.late;
            proj.late = 0;
        }
    }
    if (proj.sheltered && m == 540u) {              /* back from Porthkell in the morning, no fishing today */
        log_add(EV_BOAT_HOME, proj.sheltered - 1);
        proj.stayed |= (uint8_t)(1u << (proj.sheltered - 1u));
        proj.sheltered = 0;
        proj.mood = (uint8_t)(proj.mood + 10u > 100u ? 100u : proj.mood + 10u);
    }
    a = keeper_act();
    if (a == KA_GULLS && m % 10u == 0u && proj.trust < 100u) {   /* Captain the gull, a little bolder each time */
        if (m == 600u)
            proj.visits++;
        proj.trust++;
        if (proj.trust % 25u == 0u)
            log_add(EV_CAPTAIN, proj.trust / 25u - 1u);
    }
    if (a == KA_BEACH && wchance(9u)) {
        proj.shells++;
        log_add(EV_SHELL, (int)(wrand() % 5u));
        proj.mood = (uint8_t)(proj.mood + 5u > 100u ? 100u : proj.mood + 5u);
    }
    if (proj.wx == WX_STORM && w.storm > 0.7f && wchance(2u))
        log_add(EV_LIGHTNING, 0);
    if (m == 1380u && proj.wx == WX_CALM && w.fog < 0.2f) {
        proj.nights++;
        if (wchance(250u))
            log_add(EV_CLEAR_NIGHT, 0);
    }
    /* his mood drifts toward the day's: weather, the boats, the gull's company */
    tgt = 62 + (proj.wx == WX_CALM ? 8 : 0) - (int)(w.fog * 10.0f) - (int)(w.storm * 35.0f) - (proj.late ? 30 : 0) +
          (int)proj.trust / 10;
    if (m % 8u == 0u) {
        if ((int)proj.mood < tgt)
            proj.mood++;
        else if ((int)proj.mood > tgt && proj.mood)
            proj.mood--;
    }
}

/* the world's clock forward by dt real ms */
static void world_advance(uint32_t dt)
{
    uint32_t add = proj.clock ? dt * (24u / CYCLE_H[proj.cycle]) : dt, total;
    if (add > 10u * MIN_MS)
        add = 10u * MIN_MS;                        /* (a stall: never more than ten world minutes at once) */
    total = w.ms + add;
    while (w.ms / MIN_MS != total / MIN_MS) {
        w.ms = (w.ms / MIN_MS + 1u) * MIN_MS;
        if (w.ms >= DAY_MS) {
            w.ms -= DAY_MS;
            total -= DAY_MS;
            new_day();
        }
        minute();
    }
    w.ms = total;
}

/* a time set by hand: the world is there now, nothing simulated on the way */
static void world_set_min(uint32_t m)
{
    w.ms = (m % 1440u) * MIN_MS;
}

/* ------------------------------------------------------------ the music --- */
/* the weather's pull on each value: fog, wind, storm (added at full weather) */
static const int8_t FOG_D[P_NPARAMS] = {[P_REVERB] = 12, [P_SIZE] = 5, [P_BRIGHT] = -20, [P_BELLS] = -25,
                                        [P_ECHO] = 15, [P_SWELL] = 10, [P_DENSITY] = -12, [P_PULSE] = -20,
                                        [P_SURF] = -12, [P_TEMPO] = -4, [P_DRIFT] = 10};
static const int8_t WIND_D[P_NPARAMS] = {[P_HATS] = 15, [P_HITS] = 10, [P_SEA] = 12, [P_SURF] = 30,
                                         [P_MOTION] = 20, [P_EVOLVE] = 15, [P_DRIFT] = -10, [P_BTONE] = 10};
static const int8_t STORM_D[P_NPARAMS] = {[P_PULSE] = 40, [P_HITS] = 40, [P_BASS] = 20, [P_TEMPO] = 16,
                                          [P_SEA] = 30, [P_SURF] = 55, [P_EVOLVE] = 30, [P_DRIFT] = -35,
                                          [P_BRIGHT] = -10, [P_ECHO] = 10, [P_SWING] = -20, [P_BELLS] = -15,
                                          [P_MOTION] = 30, [P_BTONE] = 25, [P_HATS] = 20, [P_GLIDE] = -15};

/* PRESETS (1.1): a palette for the coast. The day's phases still set the music; a preset recolours them: the key
 * moved, the modes a step brighter or darker (or pentatonic), the tempo scaled, the pad's colour, AMB-1 Drift 1.1's
 * strings, lead, air and shimmer, and the space. Only the sound changes: the time, the weather, the boats, Abel and
 * his log go on as they were. Harbour (0) is the coast as composed in 1.0. Saved with the settings. */
#define PR_PENT 9                        /* mode: the pentatonic of the phase's mode (major or minor) */
typedef struct {
    const char *name;
    int8_t key;                          /* semitones the key moves */
    int8_t mode;                         /* the modes: +1 a step brighter, -1 darker, PR_PENT pentatonic, 0 as they are */
    uint8_t tempo;                       /* percent of the phase's tempo */
    int8_t pt;                           /* the pad's colour: -1 the phase's, 0 Rhodes, 1 Voices, 2 Strings */
    int8_t tex;                          /* the air's texture (0 rain, 1 wind, 2 sea, 3 vinyl); its level is in d */
    int8_t d[P_NPARAMS];                 /* added to the phase's values */
} preset_t;
static const preset_t PRESETS[NPRESETS] = {
    {"Harbour", 0, 0, 100, -1, 0, {0}},
    {"Fjord", -2, -1, 85, 2, 1, {[P_REVERB] = 12, [P_SIZE] = 5, [P_BRIGHT] = -10, [P_BELLS] = -15, [P_PULSE] = -15,
                                 [P_LEAD] = 40, [P_LDTONE] = 35, [P_LDOCT] = -1, [P_TEXLVL] = 30, [P_TEXMOD] = 20,
                                 [P_DRIFT] = 10}},
    {"Tropical Atoll", 3, PR_PENT, 110, 0, -1, {[P_PLUCK] = 55, [P_PLDENS] = 20, [P_PLTONE] = 55, [P_BELLS] = 5,
                                                [P_BRIGHT] = 15, [P_PULSE] = 15, [P_HATS] = 10, [P_SWING] = 30,
                                                [P_REVERB] = -12, [P_SEA] = 8, [P_BTONE] = 15}},
    {"Winter Coast", -1, -1, 80, 2, 1, {[P_TEXLVL] = 40, [P_TEXMOD] = 30, [P_BELLS] = -20, [P_BTONE] = 25,
                                        [P_SHIMMER] = 35, [P_BRIGHT] = -15, [P_PULSE] = -30, [P_BOCT] = 1,
                                        [P_REVERB] = 8}},
    {"Misty Isles", 5, 0, 90, 1, 0, {[P_LEAD] = 35, [P_LDTONE] = -35, [P_PLUCK] = 30, [P_PLTONE] = -10,
                                     [P_ECHO] = 15, [P_REVERB] = 10, [P_BRIGHT] = -10, [P_TEXLVL] = 18, [P_BELLS] = -10}},
    {"Lighthouse Choir", 0, 0, 85, 1, -1, {[P_SHIMMER] = 50, [P_LEAD] = 30, [P_LDTONE] = 40, [P_BELLS] = -25,
                                           [P_PULSE] = -40, [P_REVERB] = 15, [P_SIZE] = 5, [P_PAD] = 10}},
    {"Sea Glass", 2, 1, 100, 0, -1, {[P_PLUCK] = 40, [P_PLTONE] = 40, [P_BTONE] = 30, [P_BOCT] = 1, [P_BELLS] = 5,
                                     [P_LEAD] = 25, [P_LDTONE] = 50, [P_LDOCT] = 1, [P_ECHO] = 10, [P_PULSE] = -15}},
    {"Gale Warning", -3, -1, 115, 1, 1, {[P_PULSE] = 25, [P_HITS] = 20, [P_BASS] = 15, [P_TEXLVL] = 45,
                                         [P_TEXMOD] = 40, [P_EVOLVE] = 20, [P_MOTION] = 20, [P_BELLS] = -10,
                                         [P_HATS] = 15, [P_SURF] = 15}},
    {"Night Ferry", -4, -1, 95, 0, 3, {[P_BASS] = 12, [P_ECHO] = 15, [P_TEXLVL] = 25, [P_PULSE] = 10,
                                       [P_SWING] = 25, [P_BRIGHT] = -8, [P_SEA] = -10, [P_LEAD] = 25, [P_LDTONE] = -40,
                                       [P_LDOCT] = -1}},
    {"Saltmarsh", 2, 0, 95, -1, 0, {[P_PLUCK] = 45, [P_PLTONE] = -5, [P_PLDENS] = 10, [P_TEXLVL] = 35,
                                    [P_BELLS] = -10, [P_SEA] = -15, [P_PULSE] = -10}},
    {"Long Light", 0, 1, 105, 2, -1, {[P_SHIMMER] = 30, [P_LEAD] = 30, [P_LDTONE] = 40, [P_LDOCT] = 1,
                                      [P_BRIGHT] = 10, [P_BELLS] = 5, [P_PLUCK] = 20, [P_PLTONE] = 10}},
    {"Chapel Bay", 0, 0, 80, 1, -1, {[P_REVERB] = 18, [P_SIZE] = 8, [P_BELLS] = 10, [P_BTONE] = 20, [P_PULSE] = -45,
                                     [P_SHIMMER] = 25, [P_DRIFT] = 15}},
    {"Kelp Forest", -5, -1, 85, 2, -1, {[P_BRIGHT] = -25, [P_ECHO] = 20, [P_REVERB] = 12, [P_BELLS] = -10,
                                        [P_LEAD] = 35, [P_LDTONE] = 40, [P_LDOCT] = -1, [P_SWELL] = 10,
                                        [P_PULSE] = -10, [P_SURF] = -10}},
};
static const char *preset_name(int i) { return PRESETS[(unsigned)i < (unsigned)NPRESETS ? i : 0].name; }

static int mode_shift(int m, int s)      /* the modes, brightest first: Lydian, Ionian, Mixolydian, Dorian, Aeolian, Phrygian */
{
    static const uint8_t ORDER[6] = {MODE_LYDIAN, MODE_IONIAN, MODE_MIXOLYDIAN, MODE_DORIAN, MODE_AEOLIAN, MODE_PHRYGIAN};
    int r;
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

static float smooth(float x) { return x * x * (3.0f - 2.0f * x); }

static void targets(void)
{
    int16_t a[P_NPARAMS], b[P_NPARAMS], ph[P_NPARAMS];
    uint16_t pool;
    int32_t m = (int32_t)world_min(), k, p;
    float f, mood = ((float)proj.mood - 50.0f) * 0.1f, tide = world_tide();
    const preset_t *pr = &PRESETS[proj.preset < NPRESETS ? proj.preset : 0];
    for (k = 0; k < NPH + 1 && !(m >= PH_MID[k] && m < PH_MID[k + 1]); k++)
        ;
    if (k > NPH)
        k = NPH;
    f = smooth(fm_clampf(((float)m + (float)(w.ms % MIN_MS) * (1.0f / 60000.0f) - (float)PH_MID[k]) /
                             (float)(PH_MID[k + 1] - PH_MID[k]), 0.0f, 1.0f));
    en_piece(PH_OF_MID[k], a, &pool);
    en_piece(PH_OF_MID[k + 1], b, &pool);
    en_piece(world_phase(), ph, &pool);
    for (p = 0; p < P_NPARAMS; p++) {
        float v;
        const en_param_t *pi = en_param_info(p);
        if (!world_par(p))
            continue;
        if (discrete_par(p)) {                      /* the phase's, as the preset colours it */
            int d = ph[p] + pr->d[p];
            if (p == P_ROOT)
                d = ((ph[p] + pr->key) % 12 + 12) % 12;
            else if (p == P_MODE)
                d = pr->mode ? mode_shift(ph[p], pr->mode) : ph[p];
            else if (p == P_PADTONE && pr->pt >= 0)
                d = pr->pt;
            else if (p == P_TEXTURE && pr->tex >= 0)
                d = pr->tex;
            w.tgt[p] = (int16_t)(d < pi->lo ? pi->lo : d > pi->hi ? pi->hi : d);
            continue;
        }
        v = (float)a[p] + (float)(b[p] - a[p]) * f;
        if (p == P_TEMPO)
            v = v * (float)pr->tempo * 0.01f;
        v += (float)pr->d[p];
        v += w.fog * (float)FOG_D[p] + w.wind * (float)WIND_D[p] + w.storm * (float)STORM_D[p];
        if (p == P_BRIGHT || p == P_BELLS)
            v += mood;
        if (p == P_SEA)
            v = (v + (tide - 0.5f) * 12.0f) * (float)proj.sea * 0.2f;
        v = fm_clampf(v + 0.5f, (float)pi->lo, (float)pi->hi);
        w.tgt[p] = (int16_t)v;
    }
    if (w.storm > 0.5f) {                           /* a storm: the same root, a darker mode, the choir, shorter echoes */
        int md = w.tgt[P_MODE];
        w.tgt[P_MODE] = (int16_t)(md == MODE_AEOLIAN || md == MODE_PENT_MIN ? MODE_PHRYGIAN : MODE_AEOLIAN);
        w.tgt[P_PADTONE] = 1;
        w.tgt[P_ETIME] = 2;
    }
}

static void engine_set(int p, int v)
{
    w.cur[p] = (int16_t)v;
    en_set(p, v);
}

/* the engine's values a step toward the targets (every 300 ms); the key changes on its own, once in a while */
static void conduct(uint32_t now)
{
    int p, swap = 0;
    targets();
    if (!w.settled) {                               /* the first time: everything at once, a fresh garden */
        for (p = 0; p < P_NPARAMS; p++)
            if (world_par(p))
                engine_set(p, w.tgt[p]);
        en_pool(EN_MODE_POOL[w.tgt[P_MODE]]);
        en_regrow();
        w.settled = 1;
        w.slew_t = w.swap_t = now;
        w.phase = (uint8_t)world_phase();
        return;
    }
    if (now - w.slew_t < 300u)
        return;
    w.slew_t = now;
    for (p = 0; p < P_NPARAMS; p++) {
        int d;
        if (!world_par(p))
            continue;
        d = w.tgt[p] - w.cur[p];
        if (discrete_par(p)) {
            swap |= d != 0;
            continue;
        }
        if (d) {
            int s = d / ((int32_t)(w.quick_t - now) > 0 ? 6 : 40);
            engine_set(p, w.cur[p] + (s ? s : d > 0 ? 1 : -1));
        }
    }
    if (swap && now - w.swap_t >= 15000u) {         /* a new key: all of it at once, on a new chord */
        for (p = 0; p < P_NPARAMS; p++)
            if (world_par(p) && discrete_par(p) && w.cur[p] != w.tgt[p])
                engine_set(p, w.tgt[p]);
        en_pool(EN_MODE_POOL[w.cur[P_MODE]]);
        en_next();
        w.swap_t = now;
    }
}

/* another preset (PRESETS): the world goes on; the sound goes to it over a few seconds, the key at once */
static void preset_set(int i)
{
    uint32_t now = plat_ms();
    proj.preset = (uint8_t)(((i % NPRESETS) + NPRESETS) % NPRESETS);
    w.quick_t = now + 8000u;
    w.swap_t = now - 15000u;
}

/* --------------------------------------------------------------- entry --- */
static void world_init(void)
{
    memset(&w, 0, sizeof w);
    w.ms = (proj.tod % 86400u) * 1000u;
    w.prev = plat_ms();
    weather_ease(1.0f);                             /* the weather as it was, at once */
    w.phase = (uint8_t)world_phase();
}

static void world_tick(uint32_t now)
{
    uint32_t dt = now - w.prev;
    w.prev = now;
    world_advance(dt);
    conduct(now);
}
