/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; NIGHT TRAIN: ashtonwallen */
/* NIGHT TRAIN UI: the panel, the screen, and the conductor between the world (world.c) and the engine (dsp/amb.c).
 * Part of the unity build (after gfx.c, world.c and project.c); the host simulator includes it too.
 *
 *   HOME           the window: the landscape going by, the next station, who sits where
 *   SEQ            the journal: everything that happened on board (KNOB 1 or ALGORITHM scrolls)
 *   ENV            the passengers: the six regulars, where they are, their stories
 *   GLO            setup: tune, MIDI out, lights, the line; the journey so far
 *   FX             the next line (1.1): the train's sound palette (LINES[]); the journey goes on as it was
 *   SELECT         steps through the pages
 *   KNOB 1-4       on the window: Stretch (minutes between stations), Clack (the wheels), Tone, Space
 *   PRESETS        express: on to the next station
 *   PLAY           the train runs / stands (sound and journey both)
 *   REC            freeze: the chord holds, the reverb rings on     EDIT  the next chord
 *   keys, MIDI in  a soft bell, in the key of the stretch
 *   SAVE           saves the journey (it saves itself rarely, at a station or standing: see keep_due)
 *
 * The music: each landscape is one of the engine's recipes (amb.c PIECES), in its own key for each stretch. The
 * tempo follows the train's speed, so the wheels' clack (on the grid) slows into a station and quickens away from
 * it; at a station the bells, the bass and the pulse thin out, a chime rings, and the key moves to the next
 * stretch's while the train stands. Tunnels darken and rumble, bridges ring, rain patters, Theo's playing brings the
 * bells forward. */
#include "fastmath.h"

enum { V_WINDOW, V_LOG, V_PASS, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Window", "Journal", "Passengers", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_SEQ, B_ENV, B_GLO};
enum { K_STRETCH, K_CLACK, K_TONE, K_SPACE, K_TUNE, K_MIDI, K_LEDS, K_LINE, K_SCROLL, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {K_STRETCH, K_CLACK, K_TONE, K_SPACE},
    {K_SCROLL, K_NONE, K_NONE, K_NONE},
    {K_NONE, K_NONE, K_NONE, K_NONE},
    {K_TUNE, K_MIDI, K_LEDS, K_LINE},
};
static const char *const KNOB_NAME[K_NONE + 1] = {"Stretch", "Clack", "Tone", "Space", "Tune", "MIDI out", "Lights",
                                                  "Line", "Scroll", ""};

/* the night's colours */
#define N_BG RGB(18, 17, 26)
#define N_PANEL RGB(32, 30, 44)
#define N_LINE RGB(58, 54, 72)
#define N_TEXT RGB(242, 232, 208)
#define N_DIM RGB(146, 134, 118)
#define N_ACC RGB(255, 186, 92)
#define N_RED RGB(236, 72, 60)
#define N_GREEN RGB(96, 220, 128)
#define N_SKY0 RGB(8, 12, 30)
#define N_SKY1 RGB(30, 40, 78)
#define N_STAR RGB(214, 218, 240)
#define N_MOON RGB(250, 240, 204)
#define N_FAR RGB(42, 54, 94)
#define N_MID RGB(24, 32, 60)
#define N_NEAR RGB(9, 11, 20)
#define N_WARM RGB(255, 204, 116)
#define N_WATER RGB(16, 28, 58)
#define N_GLINT RGB(150, 168, 214)
#define N_SNOW RGB(112, 126, 164)
#define N_WALL RGB(56, 38, 34)
#define N_WALL2 RGB(74, 52, 44)
#define N_FRAME RGB(128, 98, 64)
#define N_SEAT RGB(118, 40, 46)
#define N_SEAT2 RGB(88, 28, 36)
#define N_CURT RGB(96, 30, 44)
#define N_LOW RGB(196, 84, 52)

static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];
    uint32_t down_t[NB];
    uint32_t turn_t;
    uint8_t help;
    uint8_t dirty;                     /* the settings changed since the last save */
    uint8_t wdirty;                    /* the journey moved since the last save */
    uint32_t act_t, saved_t, save_at;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t frame, prev;
    uint16_t scroll;                   /* the journal: entries back from the newest */
    uint16_t log_n;                    /* the log as last seen (a new entry lights up) */
    uint32_t log_t;
    uint32_t sig[4];
} ui;

static uint8_t sp_said;
static void msg_text(const char *a, const char *b, char *t)
{
    uint32_t i, k = 0;
    for (i = 0; i < 23u && a && a[i]; i++)
        t[k++] = a[i];
    if (b && b[0])
        t[k++] = ' ';
    for (i = 0; i < 23u && b && b[i]; i++)
        t[k++] = b[i];
    while (k && t[k - 1] == '.')
        k--;
    t[k] = 0;
}
static void say_as(const char *a, const char *b, uint32_t flags)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1500u;
    if (flags && ann_on()) {
        msg_text(a, b, t);
        ann_say(t, 0, flags);
        sp_said = 1;
    }
}
static void say(const char *a, const char *b) { say_as(a, b, ANN_NOW); }
static void warn(const char *a, const char *b) { say_as(a, b, ANN_NOW | ANN_KEEP); }
void ui_say(const char *a, const char *b) { say(a, b); }

static char *cat_s(char *b, const char *s)
{
    while ((*b = *s++))
        b++;
    return b;
}
static char *itoa_u(uint32_t v, char *b)
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
static char *itoa_s(int v, char *b)
{
    if (v > 0)
        *b++ = '+';
    if (v < 0) {
        *b++ = '-';
        v = -v;
    }
    return itoa_u((uint32_t)v, b);
}
static char *km_text(float km, char *b)      /* "4.2 km", "12 km" */
{
    uint32_t t = (uint32_t)(km * 10.0f + 0.5f);
    if (t < 100u) {
        b = itoa_u(t / 10u, b);
        *b++ = '.';
        *b++ = (char)('0' + t % 10u);
        *b = 0;
    } else {
        b = itoa_u(t / 10u, b);
    }
    return cat_s(b, " km");
}
int ui_dirty(void) { return ui.dirty != 0; }

/* ------------------------------------------------------------- music --- */
/* the targets: the landscape's recipe, then the train (its speed, the stop, a tunnel, a bridge, the rain, Theo) and
 * the player's Tone, Space and Clack over it. The levels and the tempo slide to them; the key, the scale and the
 * rest of the discrete settings change at the station (music_swap). */
static int16_t mus_cur[P_NPARAMS];
static uint32_t mus_t;

/* the lines (1.1): the train's sound palette, over every landscape. A line keeps the landscapes' own recipes and
 * changes their instruments (the pad's colour, Drift 1.1's strings, lead, air and shimmer), leans the key's mode, the
 * tempo, the space and the wheels. The journey is the same whichever line it runs on: only the sound changes.
 * Night Line, the first, is 1.0's sound (no change); a project saved by 1.0 runs on it.
 *   PT pad colour (-1 the landscape's, 0 Rhodes, 1 Voices, 2 Strings); Md mode (-1 the landscape's); Tm tempo %;
 *   Pad Bel Bas Pul levels %; BT bell tone (-1 the landscape's); Str Den STo SO strings; Led LTo Oft LO lead;
 *   Tx texture (0 rain, 1 wind, 2 sea, 3 vinyl) and Air its level, Sw sway; Sh shimmer; Rv Ec Br reverb, echo,
 *   brightness added; Ck the wheels' clack and the rumble % */
enum { L_PT, L_MODE, L_TEMPO, L_PAD, L_BELLS, L_BASS, L_PULSE, L_BTONE, L_STR, L_SDEN, L_STONE, L_SOCT, L_LEAD, L_LTONE,
       L_LOFT, L_LOCT, L_TEX, L_AIR, L_SWAY, L_SHIM, L_REV, L_ECHO, L_BRIGHT, L_CLACK, L_N };
#define NLINES 12
_Static_assert(NLINES == PROJ_NLINES, "the project knows how many lines there are");
static const struct {
    const char *name;
    int16_t v[L_N];
} LINES[NLINES] = {
    /*                   PT  Md  Tm  Pad Bel Bas Pul  BT  Str Den STo SO  Led LTo Oft LO  Tx Air Sw  Sh  Rv  Ec  Br  Ck */
    {"Night Line",     {-1, -1, 100, 100, 100, 100, 100, -1,  0, 40, 30, 0,  0, 50, 35, 0,  0,  0, 50,  0,  0,  0,  0, 100}},
    {"Alpine Sleeper", { 2,  3,  85, 100,  70, 90,  40, 20,  0, 40, 30, 0, 45, 85, 35, 1,  1, 45, 60, 55, 10,  0,  5,  80}},
    {"Coastal Line",   {-1,  4,  95, 90,  70, 100,  60, -1, 60, 45, 20, 0,  0, 50, 35, 0,  2, 60, 70, 15,  5, 15, -5,  90}},
    {"Desert Express", { 1,  2,  90, 90,  55, 110,  50, 70, 45, 35, 80, 0, 45, 10, 40, -1, 1, 40, 80,  0, -5,  5, 10, 100}},
    {"City Loop",      { 0,  1, 110, 90, 100, 110, 150, 80, 30, 50, 70, 1,  0, 50, 35, 0,  3, 40, 40,  0,-15,  0, 10, 110}},
    {"Snow Route",     { 1,  6,  80, 100,  60, 70,  0, 30, 45, 30, 85, 1, 40, 95, 30, 1,  1, 30, 30, 60, 10,  0, 10,  60}},
    {"Old Steam",      { 0,  4,  85, 90,  80, 110, 80, 40, 40, 40, 10, 0,  0, 50, 35, 0,  3, 70, 50,  0,  0,  5,-20, 140}},
    {"Rain Mail",      { 2,  5,  90, 100,  50, 100, 40, 20, 35, 30, 20, 0, 50, 15, 30, 0,  0, 60, 60, 20,  5, 10,-10,  90}},
    {"Lantern Local",  { 0,  6, 100, 90,  80, 90, 60, 90, 55, 50, 90, 0,  0, 50, 35, 0,  3, 25, 30,  0,  0,  5,  5, 100}},
    {"Aurora Line",    { 1,  3,  80, 100,  60, 80,  0, 10, 35, 30, 40, 1, 45, 80, 35, 1,  1, 35, 70, 70, 15,  0,  0,  60}},
    {"Border Night",   { 2,  1,  95, 90,  50, 110, 50, -1, 50, 40, 15, -1, 50, 20, 35, -1, 1, 45, 75, 10,  0,  5,-10, 100}},
    {"Last Train",     { 1,  7,  75, 100,  50, 90,  0, 20, 30, 25, 60, 0, 40, 60, 25, 0,  0, 40, 40, 35, 15, 10,-15,  70}},
};
static const char *line_name(int i) { return LINES[(unsigned)i < NLINES ? i : 0].name; }
static int pct(int v, int p) { return (v * p + 50) / 100; }
static void line_apply(int16_t *t, int f_run)  /* the line over the landscape's recipe (f_run 0..100: how hard it runs) */
{
    const int16_t *L = LINES[proj.line < NLINES ? proj.line : 0].v;
    if (L[L_PT] >= 0)
        t[P_PADTONE] = L[L_PT];
    if (L[L_MODE] >= 0)
        t[P_MODE] = L[L_MODE];
    if (L[L_BTONE] >= 0)
        t[P_BTONE] = L[L_BTONE];
    t[P_TEMPO] = (int16_t)pct(t[P_TEMPO], L[L_TEMPO]);
    t[P_PAD] = (int16_t)pct(t[P_PAD], L[L_PAD]);
    t[P_BELLS] = (int16_t)pct(t[P_BELLS], L[L_BELLS]);
    t[P_BASS] = (int16_t)pct(t[P_BASS], L[L_BASS]);
    t[P_PULSE] = (int16_t)pct(t[P_PULSE], L[L_PULSE]);
    t[P_PLUCK] = (int16_t)pct(L[L_STR], 40 + 60 * f_run / 100);   /* the strings rest at the station */
    t[P_PLDENS] = L[L_SDEN];
    t[P_PLTONE] = L[L_STONE];
    t[P_PLOCT] = L[L_SOCT];
    t[P_LEAD] = L[L_LEAD];
    t[P_LDTONE] = L[L_LTONE];
    t[P_LDDENS] = L[L_LOFT];
    t[P_LDOCT] = L[L_LOCT];
    t[P_TEXTURE] = L[L_TEX];
    t[P_TEXLVL] = L[L_AIR];
    t[P_TEXMOD] = L[L_SWAY];
    t[P_SHIMMER] = L[L_SHIM];
    t[P_REVERB] = (int16_t)(t[P_REVERB] + L[L_REV]);
    t[P_ECHO] = (int16_t)(t[P_ECHO] + L[L_ECHO]);
    t[P_BRIGHT] = (int16_t)(t[P_BRIGHT] + L[L_BRIGHT]);
    t[P_RUMBLE] = (int16_t)pct(t[P_RUMBLE], L[L_CLACK]);
}
static int mus_discrete(int p)
{
    return p == P_ETIME || p == P_ROOT || p == P_MODE || p == P_BOCT || p == P_PADTONE || p == P_BASSOCT || p == P_TUNE ||
           p == P_MIDI || p == P_TEXTURE || p == P_PLOCT || p == P_LDOCT;
}
static int clampi(int v, int p)
{
    const en_param_t *pi = en_param_info(p);
    return v < pi->lo ? pi->lo : v > pi->hi ? pi->hi : v;
}
static float speed_ratio(void)
{
    const world_t *w = &proj.w;
    return w->cruise > 1.0f ? fm_clampf(w->speed / w->cruise, 0.0f, 1.0f) : 0.0f;
}
static void music_targets(int16_t *t)
{
    const world_t *w = &proj.w;
    uint16_t pool;
    float s = speed_ratio(), f;
    int tn = world_in_tunnel(w), br = world_on_bridge(w), p, theo = w->theo_play > 0;
    en_piece(w->land, t, &pool);
    f = w->phase == PH_DWELL ? 0.0f : w->phase == PH_SIGNAL ? 0.25f * s : 0.25f + 0.75f * s;
    line_apply(t, (int)(f * 100.0f));
    t[P_ROOT] = (int16_t)((t[P_ROOT] + w->key) % 12);
    t[P_PAD] = (int16_t)((float)t[P_PAD] * (tn ? 0.75f : 1.0f));
    t[P_BELLS] = (int16_t)((float)t[P_BELLS] * (0.3f + 0.7f * f) * (tn ? 0.55f : 1.0f) + (theo ? 25.0f : 0.0f));
    t[P_DENSITY] = (int16_t)(t[P_DENSITY] + (theo ? 25 : 0));
    t[P_BASS] = (int16_t)((float)t[P_BASS] * (0.35f + 0.65f * f));
    t[P_PULSE] = (int16_t)((float)t[P_PULSE] * f * f);
    t[P_TEMPO] = (int16_t)(34.0f + (float)(t[P_TEMPO] - 34) * s);
    t[P_TEXLVL] = (int16_t)((float)t[P_TEXLVL] * (tn ? 0.3f : 1.0f));   /* the air is outside */
    t[P_RAIL] = (int16_t)((float)pct(proj.clack, LINES[proj.line < NLINES ? proj.line : 0].v[L_CLACK]) * fm_minf(1.0f, s * 1.6f) * (br ? 1.25f : 1.0f) * (s > 0.02f ? 1.0f : 0.0f));
    t[P_RUMBLE] = (int16_t)(s > 0.02f ? (float)t[P_RUMBLE] * (0.35f + 0.65f * s) + (tn ? 35.0f : 0.0f) + (br ? 15.0f : 0.0f) : 0.0f);
    t[P_RAIN] = (int16_t)(w->rain && !tn ? (w->land == LS_SNOW || w->land == LS_MOUNTAIN ? 25 : 60) : 0);
    t[P_BRIGHT] = (int16_t)(t[P_BRIGHT] + proj.tone - (tn ? 25 : 0));
    t[P_REVERB] = (int16_t)(t[P_REVERB] + proj.space / 2 + (tn ? 12 : 0) + (w->phase == PH_DWELL ? 8 : 0));
    t[P_ECHO] = (int16_t)(t[P_ECHO] + proj.space / 2);
    t[P_TUNE] = proj.tune;
    t[P_MIDI] = proj.midi;
    for (p = 0; p < P_NPARAMS; p++)
        t[p] = (int16_t)clampi(t[p], p);
}
static uint32_t music_pool(void)
{
    int16_t t[P_NPARAMS];
    uint16_t pool;
    en_piece(proj.w.land, t, &pool);
    line_apply(t, 100);
    return EN_MODE_POOL[t[P_MODE] % EN_NMODES];
}
static uint8_t mus_line;                     /* the line the music is on */
static void music_apply_all(void)            /* boot, load: everything at once */
{
    int p;
    mus_line = proj.line;
    music_targets(mus_cur);
    for (p = 0; p < P_NPARAMS; p++)
        en_set(p, mus_cur[p]);
    en_pool(music_pool());
}
static void music_swap(void)                 /* a new stretch: its key, its scale, on a new chord */
{
    int16_t t[P_NPARAMS];
    int p;
    music_targets(t);
    for (p = 0; p < P_NPARAMS; p++)
        if (mus_discrete(p) && t[p] != mus_cur[p]) {
            mus_cur[p] = t[p];
            en_set(p, t[p]);
        }
    en_pool(music_pool());
    en_next();
}
static void music_tick(uint32_t now)         /* ten times a second, a step towards the targets */
{
    int16_t t[P_NPARAMS];
    int p;
    if (now - mus_t < 100u)
        return;
    mus_t = now;
    if (mus_line != proj.line) {             /* a new line: its colours at once, on a new chord; levels slide */
        mus_line = proj.line;
        music_swap();
    }
    music_targets(t);
    for (p = 0; p < P_NPARAMS; p++) {
        int d = t[p] - mus_cur[p];
        if (!d)
            continue;
        if (mus_discrete(p)) {
            if (p == P_TUNE || p == P_MIDI) {    /* the player's: at once */
                mus_cur[p] = t[p];
                en_set(p, t[p]);
            }
            continue;
        }
        mus_cur[p] = (int16_t)(mus_cur[p] + (d > 2 ? 2 : d < -2 ? -2 : d));
        en_set(p, mus_cur[p]);
    }
}
static uint32_t pool_abs(void)
{
    uint32_t p = music_pool(), r = (uint32_t)mus_cur[P_ROOT] % 12u;
    return ((p << r) | (p >> (12u - r))) & 0xFFFu;
}

/* -------------------------------------------------------------- knobs --- */
static int nlog_kept(void) { return proj.w.nlog < NLOG ? proj.w.nlog : NLOG; }
static int knob_get(int k)
{
    switch (k) {
    case K_STRETCH: return proj.stretch;
    case K_CLACK: return proj.clack;
    case K_TONE: return proj.tone;
    case K_SPACE: return proj.space;
    case K_TUNE: return proj.tune;
    case K_MIDI: return proj.midi;
    case K_LEDS: return proj.leds;
    case K_LINE: return proj.line;
    case K_SCROLL: return ui.scroll;
    default: return 0;
    }
}
static void knob_range(int k, int *lo, int *hi)
{
    *lo = 0;
    *hi = 0;
    switch (k) {
    case K_STRETCH: *hi = W_NSTRETCH - 1; break;
    case K_CLACK: *hi = 100; break;
    case K_TONE: case K_SPACE: case K_TUNE: *lo = -50; *hi = 50; break;
    case K_MIDI: *hi = 1; break;
    case K_LEDS: *hi = 2; break;
    case K_LINE: *hi = NLINES - 1; break;
    case K_SCROLL: *hi = nlog_kept() > 0 ? nlog_kept() - 1 : 0; break;
    default: break;
    }
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    switch (k) {
    case K_STRETCH: cat_s(itoa_u(W_STRETCH_MIN[v], b), " min"); break;
    case K_CLACK: itoa_u((uint32_t)v, b); break;
    case K_TONE: case K_SPACE: case K_TUNE: itoa_s(v, b); break;
    case K_MIDI: cat_s(b, v ? "On" : "Off"); break;
    case K_LEDS: cat_s(b, v == 2 ? "Keys" : v ? "On" : "Off"); break;
    case K_LINE: cat_s(b, line_name(v)); break;
    case K_SCROLL: itoa_u((uint32_t)v, b); break;
    default: b[0] = 0; break;
    }
}
static void knob_set(int k, int v)
{
    int lo, hi;
    knob_range(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (v == knob_get(k) || k == K_NONE)
        return;
    switch (k) {
    case K_STRETCH: proj.stretch = (uint8_t)v; break;
    case K_CLACK: proj.clack = (uint8_t)v; break;
    case K_TONE: proj.tone = (int8_t)v; break;
    case K_SPACE: proj.space = (int8_t)v; break;
    case K_TUNE: proj.tune = (int8_t)v; break;
    case K_MIDI: proj.midi = (uint8_t)v; break;
    case K_LEDS: proj.leds = (uint8_t)v; break;
    case K_LINE: proj.line = (uint8_t)v; break;   /* the music follows (music_tick) */
    case K_SCROLL: ui.scroll = (uint16_t)v; return;
    default: return;
    }
    ui.dirty = 1;
}

static uint8_t accel_off;
static int32_t accel(int role, int32_t s, int range)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (!a)
        return s;
    if (a > 1)
        dt /= a;
    ui.enc_run[role] = (uint8_t)(dt < 60u ? (ui.enc_run[role] + a > 255u ? 255u : ui.enc_run[role] + a) : a);
    if (range <= 24 || accel_off || ui.enc_run[role] < 4u)
        return s;
    m = dt < 12u ? 5u : dt < 25u ? 3u : dt < 45u ? 2u : 1u;
    return s * (int32_t)m;
}

static void turn(int role, int k, int32_t e)
{
    int lo, hi;
    char t[16], n[16];
    if (!e || k == K_NONE)
        return;
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
    if (k != K_SCROLL && ui.view == V_WINDOW) {   /* the window shows no knobs: the header says it */
        int i;
        for (i = 0; KNOB_NAME[k][i] && i < 15; i++)
            n[i] = (char)(KNOB_NAME[k][i] >= 'a' ? KNOB_NAME[k][i] - 32 : KNOB_NAME[k][i]);
        n[i] = 0;
        knob_text(k, t);
        say_as(n, t, 0);
    }
}

/* ------------------------------------------------------------ buttons --- */
static void caps(char *d, const char *s, int n);
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.sig[1] = ui.sig[2] = 0;
}

static int save_now(void)
{
    if (project_save() == 0) {
        ui.dirty = ui.wdirty = 0;
        ui.saved_t = plat_ms();
        return 0;
    }
    return -1;
}

static void button(int b)
{
    int v;
    for (v = 0; v < NVIEWS; v++)
        if (VIEW_BTN[v] == b) {
            set_view(v);
            return;
        }
    switch (b) {
    case B_FX: {                             /* the next line */
        char t[24];
        knob_set(K_LINE, (proj.line + 1) % NLINES);
        caps(t, line_name(proj.line), sizeof t);
        say("LINE", t);
        break;
    }
    case B_EDIT:
        en_next();
        say("NEXT CHORD", en_playing ? "ON THE BAR" : 0);
        break;
    case B_PLAY:
        if (en_playing) {
            en_play(0);
            say("THE TRAIN", "STANDS");
        } else {
            en_seed(plat_ms());
            en_play(1);
            say("THE TRAIN", "RUNS");
        }
        break;
    case B_REC:
        say("FREEZE", en_frozen ? "OFF" : "ON");
        en_freeze(!en_frozen);
        break;
    case B_SAVE:
        if (save_now() == 0)
            say("SAVED", 0);
        else
            warn("NOT SAVED", "FLASH ERROR");
        break;
    default: break;
    }
}

static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0)) {
        uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)
            en_clock(st == 0xF8u ? EN_CLK_TICK : st == 0xFAu ? EN_CLK_START : st == 0xFBu ? EN_CLK_CONTINUE : EN_CLK_STOP);
        else if (st == 0x90u && v)
            en_bell((int)n);
    }
}

#define HELP_HOLD 900u
static void help_tick(uint32_t btn, uint32_t keys)
{
    uint32_t now = plat_ms();
    int i;
    if (ui.help) {
        uint32_t m = 1u << (ui.help - 1);
        if (btn != m || keys || (int32_t)(ui.turn_t - ui.down_t[ui.help - 1]) > 0)
            ui.help = 0;
        return;
    }
    if (!btn || (btn & (btn - 1u)) || keys)
        return;
    for (i = 0; i < NB; i++)
        if (btn == 1u << i && i != B_PLAY && i != B_SEL && !(ui.btn_used & btn) && now - ui.down_t[i] >= HELP_HOLD &&
            (int32_t)(ui.turn_t - ui.down_t[i]) <= 0) {
            ui.help = (uint8_t)(i + 1);
            ui.btn_used |= btn;
        }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            ui.down_t[i] = plat_ms();
            if (i == B_PLAY)
                button((int)i);
        } else if (!(ui.btn_used & m) && i != B_PLAY) {
            button((int)i);
        }
    }
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++)
        if ((ch & keys) >> i & 1u)
            en_bell((int)(53u + i));         /* key 0 is F3 */
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {
        int v = (int)ui.view + (e > 0 ? 1 : -1);
        if (v >= 0 && v < NVIEWS)
            set_view(v);
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {      /* the journal's scroll, from any page */
        if (ui.view != V_LOG)
            set_view(V_LOG);
        knob_set(K_SCROLL, ui.scroll + (e > 0 ? 1 : -1));
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        if (e > 0 && proj.w.phase == PH_RUN) {
            world_express(&proj.w);
            say("EXPRESS", "NEXT STOP");
        } else if (e > 0) {
            warn("NOT NOW", proj.w.phase == PH_DWELL ? "AT THE STATION" : "STOPPING");
        }
        ui.turn_t = plat_ms();
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            turn(EN_K1 + (int)i, VIEW_KNOB[ui.view][i], e);
            ui.act_t = ui.turn_t = plat_ms();
        }
    help_tick(btn, keys);
}

/* a button's card (help_tick): its name, then what it does. A line starting with a space goes on from the one
 * before (a screen reader joins them) */
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("THE WINDOW"); L("K1 STRETCH, K2 CLACK,"); L(" K3 TONE, K4 SPACE"); break;
    case B_SEQ: L("THE JOURNAL"); L("WHAT HAPPENED ON BOARD"); L("KNOB 1 OR ALGO: SCROLL"); break;
    case B_ENV: L("THE PASSENGERS"); L("WHO RIDES, AND THEIR"); L(" STORIES SO FAR"); break;
    case B_GLO: L("SETUP"); L("TUNE, MIDI OUT, LIGHTS,"); L(" K4 THE LINE"); L("THE JOURNEY SO FAR"); break;
    case B_FX: L("THE NEXT LINE:"); L(" THE TRAIN'S SOUND"); L("THE JOURNEY GOES ON"); L("SETUP K4: ANY LINE"); break;
    case B_EDIT: L("THE NEXT CHORD,"); L(" ON THE NEXT BAR"); break;
    case B_REC: L("FREEZE: THE CHORD HOLDS"); L(" AND THE REVERB RINGS ON"); L("AGAIN: THAW"); break;
    case B_SAVE: L("SAVES THE JOURNEY"); L("IT ALSO SAVES ITSELF"); L(" AT STATIONS"); break;
    case B_OCTDN: case B_OCTUP: L("WITH OCT- AND OCT+"); L(" HELD 5 S: UPDATE MODE"); break;
    default: break;
    }
#undef L
    return n;
}

/* ----------------------------------------------------------- the world --- */
/* SAVING, and the flash's wear: every save erases one of the same two 4 KiB sectors (APP_STORE_MAP's A/B pair), rated
 * about 100k erases, so the journey saves itself rarely. keep_due (after batt, below) is the whole policy:
 *   the journey (ui.wdirty: it moved) at most once an hour after the last save; nothing changed, no save. Up to an
 *     hour of the journey is lost at a power-off (the FM-1 gives no warning);
 *   a setting (ui.dirty) a few seconds after the knobs rest, at most once every 5 minutes;
 *   on the battery, once it reads empty (0): one save of whatever changed, then none of these until it reads above 0;
 *   SAVE saves at once (save_now).
 * When: a flash write can glitch the sound, so while the train runs only at a station (the music thin, 7 s after the
 * chime); standing and untouched a few seconds (the pad gone), any time. Each save resets the clock: it keeps the
 * settings and the journey alike. */
#define KEEP_WORLD_MS (60u * 60000u)
#define KEEP_SETTINGS_MS (5u * 60000u)
static uint8_t keep_low;                     /* the empty battery's save done; again once it reads above 0 */
static int keep_due(uint32_t now);           /* 0 not yet, 1 due, 2 due: the empty battery's save */
static void keep_watch(void);                /* every pass: the battery read above 0 again */
static void keep_save(uint32_t now)
{
    int due = keep_due(now);
    if (!due)
        return;
    save_now();
    ui.saved_t = now;                        /* (a failed write: tried again later, not every pass) */
    if (due == 2)
        keep_low = 1;
}
static float scroll_m;                       /* the landscape's own odometer (RAM only, wraps) */
static void journey(uint32_t now)
{
    uint32_t dt = now - ui.prev, ev;
    ui.prev = now;
    if (!en_playing || en_ext)
        return;                              /* standing (or under a MIDI clock's start / stop): the train waits */
    ev = world_tick(&proj.w, dt, W_STRETCH_MIN[proj.stretch]);
    scroll_m += proj.w.speed * (float)dt * 0.001f;
    if (scroll_m > 65536.0f)
        scroll_m -= 65536.0f;
    ui.wdirty = 1;
    if (ev & W_CHIME) {
        en_chime();
        if (keep_due(now))
            ui.save_at = now + 7000u;        /* the music thin, the chime rung: a quiet moment to save */
    }
    if (ev & W_SWAP)
        music_swap();
    if (ui.save_at && (int32_t)(now - ui.save_at) >= 0) {
        ui.save_at = 0;
        keep_save(now);
    }
}

#define AUTOSAVE_QUIET 4000u
static void autosave(void)                   /* standing */
{
    uint32_t now = plat_ms();
    keep_watch();
    if (en_playing || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_QUIET || en_pad_level > 0.01f)
        return;
    keep_save(now);
}

/* ------------------------------------------------------------- drawing --- */
#define HDR_H 22
#define WIN_Y 22
#define WIN_BAND 144
#define INFO_Y 166
#define INFO_H 74

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}
static void cv_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    static const uint8_t HW[7][7] = {{0}, {1, 0}, {2, 1, 0}, {3, 2, 2, 0}, {4, 4, 3, 2, 0}, {5, 5, 4, 4, 3, 0},
                                     {6, 6, 5, 5, 4, 3, 0}};
    int32_t j;
    if (r <= 0) {
        cv_rect(cx, cy, 1, 1, c);
        return;
    }
    if (r > 6)
        r = 6;
    for (j = -r + 1; j < r; j++) {
        int32_t w = HW[r][j < 0 ? -j : j];
        cv_rect(cx - w, cy + j, 2 * w + 1, 1, c);
    }
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}
static uint16_t mix(uint16_t a, uint16_t b, int t)
{
    int r, g, bl;
    if (t <= 0)
        return a;
    if (t >= 16)
        return b;
    r = ((a >> 11) * (16 - t) + (b >> 11) * t) / 16;
    g = (((a >> 5) & 63) * (16 - t) + ((b >> 5) & 63) * t) / 16;
    bl = ((a & 31) * (16 - t) + (b & 31) * t) / 16;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static void caps(char *d, const char *s, int n)
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}
/* text in up to `lines` lines of width w, broken at spaces; the last one cut with ".." if it does not fit */
static int text_wrap(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, int32_t lh, const char *s, uint16_t c,
                     int lines)
{
    char ln[64];
    int n = 0;
    while (*s && n < lines) {
        int k = 0, brk = -1, i;
        while (s[k] && k < 62) {
            ln[k] = s[k];
            ln[k + 1] = 0;
            if (text_w(f, ln) > w)
                break;
            if (s[k] == ' ')
                brk = k;
            k++;
        }
        if (!s[k] || k >= 62) {
            brk = k;
        } else if (n == lines - 1) {         /* the last line: cut */
            while (k > 0) {
                ln[k] = 0;
                cat_s(ln + k, "..");
                if (text_w(f, ln) <= w)
                    break;
                k--;
            }
            cv_text(x, y + n * lh, f, ln, c);
            return n + 1;
        } else if (brk < 0) {
            brk = k;
        }
        for (i = 0; i < brk; i++)
            ln[i] = s[i];
        ln[brk] = 0;
        cv_text(x, y + n * lh, f, ln, c);
        s += brk;
        while (*s == ' ')
            s++;
        n++;
    }
    return n;
}

#ifdef PLAT_HOST
static int8_t batt_host = -2;
static int batt(void) { return batt_host > -2 ? batt_host : plat_battery(); }
#else
#define batt() plat_battery()
#endif
static void keep_watch(void)
{
    if (batt() != 0)
        keep_low = 0;
}
static int keep_due(uint32_t now)
{
    uint32_t since = now - ui.saved_t;
    if (!ui.dirty && !ui.wdirty)
        return 0;
    if (batt() == 0 && !keep_low && since >= KEEP_SETTINGS_MS)
        return 2;
    return since >= KEEP_WORLD_MS || (ui.dirty && since >= KEEP_SETTINGS_MS);
}
static void draw_batt(int32_t x, int32_t y, int b)
{
    uint16_t c = b <= 1 ? N_LOW : N_DIM;
    int i;
    cv_round(x, y, 18, 10, 2, c);
    cv_rect(x + 1, y + 1, 16, 8, N_BG);
    cv_rect(x + 18, y + 3, 2, 4, c);
    if (b == 4) {
        cv_rect(x + 7, y + 2, 4, 6, N_ACC);
        return;
    }
    for (i = 0; i < b && i < 3; i++)
        cv_rect(x + 2 + i * 5, y + 2, 4, 6, b <= 1 ? N_LOW : N_GREEN);
}
static const char *batt_words(int b)
{
    static const char *const W[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                     "On USB power"};
    return b >= 0 && b <= 4 ? W[b] : 0;
}

/* the header: the landscape and its key; the speed, the battery. Messages take its place for a moment. */
static void draw_header(void)
{
    char t[24];
    int msg = plat_ms() < ui.msg_until, kmh = (int)(proj.w.speed * 3.6f + 0.5f);
    uint32_t h = hash(hash(hash(2166136261u, proj.w.land | (uint32_t)mus_cur[P_ROOT] << 8 | (uint32_t)msg << 16 |
                                                  (uint32_t)en_playing << 17 | (uint32_t)ui.dirty << 18),
                           (uint32_t)kmh | (uint32_t)(batt() + 1) << 12 | (uint32_t)world_in_tunnel(&proj.w) << 16),
                      msg ? (uint32_t)ui.msg[0][0] << 8 ^ (uint32_t)ui.msg[1][0] ^ ui.msg_until : 0u);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, N_BG);
    if (msg) {
        int32_t x = cv_text(8, 3, &FONT_B, ui.msg[0], N_TEXT);
        cv_text(x + 8, 3, &FONT_B, ui.msg[1], N_ACC);
    } else {
        int32_t x = 232;
        if (batt() >= 0) {
            x -= 20;
            draw_batt(x, 6, batt());
            x -= 8;
        }
        if (en_playing) {
            cat_s(itoa_u((uint32_t)kmh, t), " km/h");
            x -= text_w(&FONT_S, t);
            cv_text(x, 3, &FONT_S, t, N_TEXT);
        } else {
            int32_t j;
            x -= 8;
            for (j = 0; j < 5; j++)
                cv_rect(x + j, 6 + j, 1, 11 - 2 * j, N_DIM);
        }
        caps(t, land_name(proj.w.land), sizeof t);
        x = cv_text(8, 3, &FONT_B, t, N_ACC);
        cv_text(x + 8, 5, &FONT_XS, EN_NOTE_NAME[mus_cur[P_ROOT] % 12], N_DIM);
        if (ui.dirty)
            cv_round(x + 8 + text_w(&FONT_XS, EN_NOTE_NAME[mus_cur[P_ROOT] % 12]) + 6, 10, 4, 4, 2, N_DIM);
    }
    cv_blit(0, 0);
}

/* ---- the window: sky, far land, near land, the track's side, the glass; then the seats */
#define SH 101                               /* the scenery's rows (band rows 6..106) */
#define HZ 60                                /* the horizon */
#define PPM_NEAR 4.0f                        /* px a metre, near */
#define PPM_MID 0.55f
#define PPM_FAR 0.07f

static int floor_div(int32_t a, int32_t b) { return a >= 0 ? (int)(a / b) : (int)((a - b + 1) / b); }
static uint32_t hh(uint32_t a, uint32_t b) { return w_mix(a, b); }
static int hf(uint32_t salt, int32_t xw, int32_t tile, int amp)   /* a smooth height field: 0..amp */
{
    int32_t i = floor_div(xw, tile);
    float f = (float)(xw - i * tile) / (float)tile, s = f * f * (3.0f - 2.0f * f);
    int a = (int)(hh(salt, (uint32_t)i) % (uint32_t)(amp + 1)), b = (int)(hh(salt, (uint32_t)(i + 1)) % (uint32_t)(amp + 1));
    return a + (int)((float)(b - a) * s);
}

static void pine(int32_t x, int32_t base, int32_t h, uint16_t c, uint16_t tip)
{
    int32_t y;
    for (y = 0; y < h; y++) {
        int32_t hw = (y * 5) / 11 + ((y % 6) < 2 ? 0 : 1);
        cv_rect(x - hw / 2, base - h + y, hw + 1, 1, tip && (y % 6) == 2 ? tip : c);
    }
    cv_rect(x, base, 1, 3, c);
}

static void draw_sky(int land)
{
    int32_t y, i;
    uint16_t hz = land == LS_CITY ? RGB(64, 42, 70) : N_SKY1;
    for (y = 0; y < HZ + 6; y += 3)
        cv_rect(0, y, 240, 3, mix(N_SKY0, hz, (int)(y * 16 / (HZ + 6))));
    for (i = 0; i < 40; i++) {
        uint32_t h = hh(0x57A2u + proj.w.seed, (uint32_t)i);
        int32_t sx = (int32_t)(h % 240u), sy = (int32_t)((h >> 9) % (uint32_t)(HZ - 8));
        if (land == LS_CITY && (i % 3))
            continue;
        cv_rect(sx, sy, 1, 1, ((ui.frame / 20u + (uint32_t)i) % 9u) ? mix(N_SKY1, N_STAR, 6 + (int)(h >> 20) % 10) : N_SKY1);
    }
    {
        uint32_t h = hh(proj.w.seed ^ 0x300Du, proj.w.station);
        int32_t mx = 40 + (int32_t)(h % 160u), my = 12 + (int32_t)((h >> 8) % 14u);
        if ((h >> 16) % 4u) {
            cv_disc(mx, my, 6, N_MOON);
            if ((h >> 18) & 1u)                     /* a crescent */
                cv_disc(mx + 3, my - 2, 5, mix(N_SKY0, hz, (int)(my * 16 / (HZ + 6))));
        }
    }
}

static void draw_far(int land)
{
    int32_t off = (int32_t)(scroll_m * PPM_FAR * 10.0f) / 10, x;
    uint32_t salt = proj.w.seed + (uint32_t)land * 977u;
    if (land == LS_CITY) {                   /* a skyline, its windows lit */
        int32_t s0 = floor_div(off, 12), s;
        for (s = s0; s <= s0 + 21; s++) {
            uint32_t h = hh(salt, (uint32_t)s);
            int32_t bx = s * 12 - off, bw = 8 + (int32_t)(h % 8u), bh = 10 + (int32_t)((h >> 4) % 34u), wy, wx;
            cv_rect(bx, HZ - bh, bw, bh + 8, N_FAR);
            for (wy = HZ - bh + 3; wy < HZ + 4; wy += 4)
                for (wx = bx + 2; wx < bx + bw - 1; wx += 3)
                    if ((hh(h, (uint32_t)(wy * 31 + wx)) & 7u) < 3u)
                        cv_rect(wx, wy, 1, 1, mix(N_FAR, N_WARM, 9));
        }
        return;
    }
    if (land == LS_COAST || land == LS_LAKES) {   /* the water to the horizon, a headland, the moon's path */
        uint32_t hm = hh(proj.w.seed ^ 0x300Du, proj.w.station);
        int32_t mx = 40 + (int32_t)(hm % 160u), y;
        cv_rect(0, HZ, 240, SH - HZ, N_WATER);
        for (x = 0; x < 240; x++) {
            int h = hf(salt, x + off, 70, 16) - 9;
            if (h > 0)
                cv_rect(x, HZ - h, 1, h, N_FAR);
        }
        for (y = HZ + 2; y < SH; y += 3) {
            int32_t w = 2 + (int32_t)(hh((uint32_t)y, ui.frame / 6u) % 6u);
            cv_rect(mx - w / 2 + (int32_t)(hh((uint32_t)y, 7u) % 5u) - 2, y, w, 1, N_GLINT);
        }
        if (land == LS_COAST) {              /* a lighthouse, far out */
            int32_t lx = 200 - (off % 400);
            if (lx > -10 && lx < 250) {
                cv_rect(lx, HZ - 14, 3, 14, N_FAR);
                if ((ui.frame / 30u) % 3u == 0u)
                    cv_rect(lx - 10, HZ - 14, 23, 1, N_WARM);
                cv_rect(lx, HZ - 15, 3, 2, N_WARM);
            }
        }
        return;
    }
    for (x = 0; x < 240; x++) {
        int h;
        uint16_t c = N_FAR;
        if (land == LS_MOUNTAIN) {
            h = 12 + hf(salt, x + off, 44, 40);
            cv_rect(x, HZ - h, 1, h + 10, c);
            cv_rect(x, HZ - h, 1, h > 30 ? 4 : 1, h > 30 ? RGB(128, 138, 170) : RGB(48, 58, 96));
            continue;
        }
        if (land == LS_SNOW)
            c = RGB(70, 80, 116);
        h = 3 + hf(salt, x + off, 64, 14);
        cv_rect(x, HZ - h, 1, h + 10, c);
        if (land == LS_SNOW)
            cv_rect(x, HZ - h, 1, 1, RGB(130, 142, 180));
    }
}

static void draw_mid(int land)
{
    int32_t off = (int32_t)(scroll_m * PPM_MID), x, s0, s;
    uint32_t salt = proj.w.seed * 3u + (uint32_t)land * 131u;
    uint16_t ground = land == LS_SNOW ? N_SNOW : land == LS_MOOR ? RGB(30, 26, 42) : N_MID;
    if (land == LS_COAST || land == LS_LAKES) {   /* glints on the water near by, reeds or rocks on the shore */
        s0 = floor_div(off, 9);
        for (s = s0; s <= s0 + 27; s++) {
            uint32_t h = hh(salt, (uint32_t)s);
            if (h % 3u == 0u)
                cv_rect(s * 9 - off, HZ + 8 + (int32_t)((h >> 4) % 20u), 3 + (int32_t)((h >> 8) % 4u), 1,
                        mix(N_WATER, N_GLINT, 6));
        }
        for (x = 0; x < 240; x++) {
            int h = hf(salt, x + off, 30, 6);
            cv_rect(x, SH - 14 - h, 1, 14 + h, N_MID);
        }
        if (land == LS_LAKES) {
            s0 = floor_div(off, 7);
            for (s = s0; s <= s0 + 35; s++)
                if (hh(salt ^ 9u, (uint32_t)s) % 3u == 0u)
                    cv_rect(s * 7 - off, SH - 24 - (int32_t)(hh(salt, (uint32_t)s) % 6u), 1, 12, N_MID);
        }
        return;
    }
    for (x = 0; x < 240; x++) {
        int h = hf(salt, x + off, 80, land == LS_MOUNTAIN ? 22 : 8);
        cv_rect(x, HZ + 12 - h, 1, SH - (HZ + 12 - h), ground);
    }
    switch (land) {
    case LS_FOREST: case LS_SNOW: case LS_MOUNTAIN: {
        int32_t step = land == LS_FOREST ? 11 : 26, pct = land == LS_FOREST ? 85 : 40;
        s0 = floor_div(off, step);
        for (s = s0 - 2; s <= s0 + 240 / step + 2; s++) {
            uint32_t h = hh(salt ^ 0x77u, (uint32_t)s);
            int32_t tx = s * step - off + (int32_t)(h % (uint32_t)step), base;
            if ((int32_t)(h >> 8) % 100 >= pct)
                continue;
            base = HZ + 12 - hf(salt, tx + off, 80, land == LS_MOUNTAIN ? 22 : 8) + 2;
            pine(tx, base, 18 + (int32_t)((h >> 16) % 24u), N_MID, land == LS_SNOW ? RGB(150, 160, 196) : 0);
        }
        break;
    }
    case LS_VILLAGE: case LS_MOOR: {
        int32_t step = land == LS_VILLAGE ? 34 : 90;
        s0 = floor_div(off, step);
        for (s = s0 - 1; s <= s0 + 240 / step + 1; s++) {
            uint32_t h = hh(salt ^ 0x51u, (uint32_t)s);
            int32_t hx = s * step - off + (int32_t)(h % 10u), hw = 16 + (int32_t)((h >> 4) % 12u), hgt = 10 + (int32_t)((h >> 8) % 6u);
            int32_t base = HZ + 12 - hf(salt, hx + off, 80, 8) + 3, j;
            if (land == LS_MOOR && (h >> 12) % 3u)
                continue;
            if (land == LS_VILLAGE && (h >> 12) % 5u == 0u) {   /* a tree */
                pine(hx + 8, base, 20, N_MID, 0);
                continue;
            }
            cv_rect(hx, base - hgt, hw, hgt, N_MID);
            for (j = 0; j < hw / 2 + 2; j++)                   /* the roof */
                cv_rect(hx - 1 + j, base - hgt - j / 2 - 1, hw + 2 - 2 * j, 1, N_MID);
            if ((h >> 14) & 1u)
                cv_rect(hx + 3, base - hgt + 3, 3, 3, N_WARM);
            if ((h >> 15) & 1u)
                cv_rect(hx + hw - 6, base - hgt + 3, 3, 3, mix(N_WARM, N_MID, 5));
            if (land == LS_VILLAGE && (h >> 16) % 7u == 0u)    /* a spire */
                cv_rect(hx + hw / 2, base - hgt - hw / 2 - 12, 2, 12, N_MID);
        }
        if (land == LS_MOOR)                                   /* a dry-stone wall */
            for (x = 0; x < 240; x += 2)
                cv_rect(x, HZ + 16 - hf(salt, x + off, 80, 8) / 2, 2, 1, RGB(44, 40, 58));
        break;
    }
    case LS_CITY: {
        s0 = floor_div(off, 28);
        for (s = s0 - 1; s <= s0 + 10; s++) {
            uint32_t h = hh(salt ^ 0xC1u, (uint32_t)s);
            int32_t bx = s * 28 - off, bw = 20 + (int32_t)(h % 7u), bh = 26 + (int32_t)((h >> 3) % 36u), wy, wx;
            cv_rect(bx, HZ + 14 - bh, bw, bh + 30, N_MID);
            for (wy = HZ + 18 - bh; wy < HZ + 12; wy += 6)
                for (wx = bx + 3; wx < bx + bw - 3; wx += 5)
                    if ((hh(h, (uint32_t)(wy * 17 + wx)) & 15u) < 7u)
                        cv_rect(wx, wy, 2, 3, (hh(h, (uint32_t)wx) & 3u) ? N_WARM : RGB(170, 200, 255));
        }
        break;
    }
    default: break;
    }
}

/* the near side: the embankment, telegraph poles and their wires, a level crossing now and then */
static void draw_near(int land)
{
    int32_t off = (int32_t)(scroll_m * PPM_NEAR), x, s0, s;
    uint32_t salt = proj.w.seed ^ 0xBEEFu;
    cv_rect(0, SH - 9, 240, 9, N_NEAR);
    for (x = 0; x < 240; x += 3)
        cv_rect(x - (off % 3), SH - 8 + (int32_t)(hh(salt, (uint32_t)((x + off) / 3)) % 6u), 1, 1, RGB(30, 30, 40));
    s0 = floor_div(off, 240);
    for (s = s0; s <= s0 + 1; s++) {         /* poles, every 60 m */
        int32_t px = s * 240 - off + 30, k;
        cv_rect(px, 14, 3, SH - 14, N_NEAR);
        cv_rect(px - 6, 16, 15, 2, N_NEAR);
        for (k = 0; k < 240; k += 2) {       /* the wires, sagging to the next pole */
            float t = (float)k / 240.0f;
            int32_t sag = (int32_t)(10.0f * 4.0f * t * (1.0f - t));
            cv_rect(px + k, 17 + sag, 2, 1, N_NEAR);
            cv_rect(px + k, 21 + sag * 9 / 10, 2, 1, N_NEAR);
        }
    }
    if (land == LS_VILLAGE || land == LS_MOOR || land == LS_FOREST) {   /* a level crossing, its lights blinking */
        s0 = floor_div(off, 1400);
        for (s = s0; s <= s0 + 1; s++) {
            int32_t cx = s * 1400 - off + 700;
            if (hh(salt, (uint32_t)s) % 3u || cx < -20 || cx > 260)
                continue;
            cv_rect(cx, 40, 2, SH - 40, N_NEAR);
            cv_rect(cx - 6, 44, 14, 6, N_NEAR);
            cv_disc(cx - 3, 47, 2, (ui.frame / 15u) & 1u ? N_RED : RGB(60, 20, 20));
            cv_disc(cx + 5, 47, 2, (ui.frame / 15u) & 1u ? RGB(60, 20, 20) : N_RED);
        }
    }
}

static int32_t near_x(float m)               /* the screen's x of a place on the line, m metres from here */
{
    float x = 120.0f + m * PPM_NEAR;
    return x < -2000.0f ? -2000 : x > 2000.0f ? 2000 : (int32_t)x;
}

static void draw_tunnel(void)                /* inside (or at the mouth of) a tunnel */
{
    const world_t *w = &proj.w;
    int32_t x0 = near_x(w->tun0 - w->pos), x1 = near_x(w->tun1 - w->pos), off = (int32_t)(scroll_m * PPM_NEAR), k;
    if (x0 >= 240 || x1 <= 0 || w->tun1 <= w->tun0)
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 > 240)
        x1 = 240;
    cv_rect(x0, 0, x1 - x0, SH, RGB(12, 11, 15));
    for (k = -(off % 24); k < 240; k += 24)
        if (k >= x0 && k < x1)
            cv_rect(k, 0, 1, SH, RGB(24, 22, 28));
    for (k = -(off % 110); k < 240; k += 110)   /* the lamps going by, smeared by the speed */
        if (k >= x0 && k + 3 < x1) {
            int32_t len = 2 + (int32_t)(w->speed * 0.4f);
            cv_rect(k - len, 34, len + 3, 2, mix(RGB(12, 11, 15), N_ACC, 8));
            cv_disc(k, 35, 2, N_ACC);
        }
    if (x0 > 0)                              /* the portal's stone */
        cv_rect(x0 - 3, 0, 4, SH, RGB(46, 42, 50));
    if (x1 < 240)
        cv_rect(x1, 0, 4, SH, RGB(46, 42, 50));
}

static void draw_bridge(void)                /* the girders going by, the river below */
{
    const world_t *w = &proj.w;
    int32_t x0 = near_x(w->brg0 - w->pos), x1 = near_x(w->brg1 - w->pos), off = (int32_t)(scroll_m * PPM_NEAR), k, j;
    if (x0 >= 240 || x1 <= 0 || w->brg1 <= w->brg0)
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 > 240)
        x1 = 240;
    cv_rect(x0, HZ + 14, x1 - x0, SH - HZ - 14, N_WATER);
    for (k = x0; k < x1; k += 7)
        if (hh((uint32_t)(k + off / 3), ui.frame / 8u) % 4u == 0u)
            cv_rect(k, HZ + 18 + (int32_t)(hh((uint32_t)k, 3u) % 20u), 3, 1, N_GLINT);
    cv_rect(x0, 4, x1 - x0, 3, N_NEAR);
    cv_rect(x0, SH - 8, x1 - x0, 4, N_NEAR);
    for (k = -(off % 36); k < 240; k += 36) {
        if (k < x0 || k >= x1)
            continue;
        cv_rect(k, 4, 2, SH - 8, N_NEAR);
        for (j = 0; j < 36 && k + j < x1; j += 2)
            cv_rect(k + j, 7 + j * (SH - 15) / 36, 2, 2, N_NEAR);
    }
}

/* a station's platform: lamps, the sign, a bench. s: where the station is, in metres from here */
static void draw_platform(float s, uint32_t idx)
{
    int32_t a = near_x(s - 95.0f), b = near_x(s + 30.0f), k;
    char nm[20];
    if (a >= 240 || b <= 0)
        return;
    cv_rect(a < 0 ? 0 : a, SH - 16, (b > 240 ? 240 : b) - (a < 0 ? 0 : a), 16, RGB(62, 60, 70));
    cv_rect(a < 0 ? 0 : a, SH - 16, (b > 240 ? 240 : b) - (a < 0 ? 0 : a), 1, RGB(200, 170, 60));
    for (k = 0; k < 5; k++) {                /* lamps every 25 m */
        int32_t lx = near_x(s - 90.0f + 25.0f * (float)k);
        if (lx < -10 || lx > 250)
            continue;
        cv_rect(lx, 22, 2, SH - 38, RGB(40, 38, 46));
        cv_rect(lx - 3, 20, 8, 3, RGB(40, 38, 46));
        cv_disc(lx + 1, 25, 3, N_WARM);
        cv_rect(lx - 4, 30, 10, 1, mix(N_MID, N_WARM, 5));
    }
    {   /* the sign */
        int32_t sx = near_x(s - 12.0f), sw;
        station_name(proj.w.seed, idx, nm);
        sw = text_w(&FONT_XS, nm) + 12;
        if (sx - sw / 2 < 250 && sx + sw / 2 > -10) {
            cv_rect(sx - 1, 52, 2, SH - 68, RGB(40, 38, 46));
            cv_round(sx - sw / 2, 38, sw, 17, 3, RGB(28, 50, 120));
            cv_rect(sx - sw / 2 + 2, 40, sw - 4, 1, RGB(220, 220, 230));
            text_c(sx, 39, &FONT_XS, nm, RGB(240, 240, 250));
        }
        sx = near_x(s - 50.0f);              /* a bench */
        cv_rect(sx, SH - 24, 16, 3, RGB(90, 60, 40));
        cv_rect(sx + 1, SH - 21, 2, 5, RGB(60, 40, 30));
        cv_rect(sx + 13, SH - 21, 2, 5, RGB(60, 40, 30));
    }
}

static void draw_weather(int land)           /* rain on the glass, or snow going by */
{
    int i;
    const int snow = land == LS_SNOW || land == LS_MOUNTAIN;
    float v = proj.w.speed;
    for (i = 0; i < 46; i++) {
        uint32_t h = hh(0xD20Fu + (uint32_t)i, 5u);
        int32_t x, y;
        if (snow) {
            x = (int32_t)((h % 240u + 400u * 240u - (uint32_t)(scroll_m * 6.0f) - (ui.frame >> 1)) % 240u);
            y = (int32_t)(((h >> 8) % (uint32_t)SH + ui.frame / 3u + (uint32_t)i) % (uint32_t)SH);
            cv_rect(x, y, i & 1 ? 2 : 1, i & 1 ? 2 : 1, RGB(220, 226, 240));
        } else {
            int32_t sl = 1 + (int32_t)(v * 0.12f);
            x = (int32_t)((h % 240u + 240u * 64u - (uint32_t)(ui.frame * (2u + (uint32_t)sl))) % 240u);
            y = (int32_t)(((h >> 8) % (uint32_t)SH + ui.frame * 2u) % (uint32_t)SH);
            cv_rect(x, y, 1, 3, RGB(150, 168, 210));
            cv_rect(x - sl, y + 3, 1, 3, RGB(126, 142, 186));
            cv_rect(x - 2 * sl, y + 6, 1, 3, RGB(96, 110, 150));
        }
    }
    if (!snow)
        for (i = 0; i < 18; i++) {           /* drops sitting on the glass */
            uint32_t h = hh(0xD0Fu + (uint32_t)i, proj.w.station);
            cv_rect((int32_t)(h % 236u), (int32_t)((h >> 8) % (uint32_t)SH + (ui.frame / 40u)) % SH, 2, 2,
                    RGB(110, 124, 168));
        }
}

/* a passenger, the head's middle at (cx, y); c: a regular, or -1 a stranger (shade: which) */
static const uint16_t BODY[NREG] = {RGB(40, 58, 120), RGB(60, 152, 150), RGB(130, 86, 52), RGB(112, 112, 118),
                                    RGB(206, 164, 64), RGB(232, 140, 60)};
static void draw_person(int32_t cx, int32_t y, int c, int asleep, uint32_t shade)
{
    int32_t hx = cx + (asleep ? 2 : 0), hy = y + (asleep ? 2 : 0);
    uint16_t skin = c < 0 ? RGB(70, 62, 76) : (c == CH_THEO ? RGB(150, 100, 70) : c == CH_MARA ? RGB(196, 150, 116) :
                                               RGB(226, 186, 154));
    if (c == CH_BISCUIT) {                   /* a cat, sitting */
        uint16_t o = BODY[CH_BISCUIT];
        cv_round(cx - 6, y + 4, 12, 9, 3, o);
        cv_disc(cx + 4, y + 2, 3, o);
        cv_rect(cx + 2, y - 2, 1, 2, o);
        cv_rect(cx + 6, y - 2, 1, 2, o);
        cv_rect(cx - 8, y + 6, 3, 2, o);
        if (asleep)
            cv_rect(cx + 3, y + 2, 3, 1, RGB(120, 60, 20));
        else
            cv_rect(cx + 4, y + 1, 1, 1, RGB(30, 30, 20));
    } else {
        uint16_t body = c < 0 ? mix(RGB(48, 44, 60), RGB(70, 62, 80), (int)(shade % 12u)) : BODY[c];
        cv_round(cx - 8, y + 6, 17, 24, 5, body);
        cv_disc(hx, hy, 5, skin);
        switch (c) {
        case CH_INES:                        /* the conductor's cap */
            cv_rect(hx - 5, hy - 6, 11, 4, RGB(28, 34, 70));
            cv_rect(hx - 5, hy - 3, 11, 1, RGB(230, 190, 80));
            cv_rect(hx + 1, hy - 2, 6, 1, RGB(28, 34, 70));
            cv_rect(cx - 2, y + 9, 2, 2, RGB(230, 190, 80));
            break;
        case CH_MARA:
            cv_rect(hx - 5, hy - 5, 10, 3, RGB(50, 30, 24));
            cv_disc(hx - 5, hy - 3, 2, RGB(50, 30, 24));
            cv_rect(cx - 3, y + 8, 6, 1, RGB(230, 240, 240));
            break;
        case CH_THEO:
            cv_rect(hx - 5, hy - 6, 11, 3, RGB(30, 20, 14));
            break;
        case CH_ALDOUS:                      /* a hat with a brim */
            cv_rect(hx - 7, hy - 4, 15, 2, RGB(50, 50, 56));
            cv_rect(hx - 4, hy - 8, 9, 4, RGB(50, 50, 56));
            cv_rect(cx + 9, y + 2, 1, 26, RGB(90, 70, 50));
            break;
        case CH_JUNE:
            cv_rect(hx - 6, hy - 6, 12, 4, RGB(24, 20, 24));
            cv_rect(hx - 6, hy - 3, 2, 6, RGB(24, 20, 24));
            cv_rect(hx + 4, hy - 3, 2, 6, RGB(24, 20, 24));
            cv_rect(cx - 8, y + 6, 17, 3, RGB(200, 56, 56));
            if (!asleep)
                cv_rect(cx - 6, y + 14, 9, 7, RGB(236, 230, 214));
            break;
        default: break;
        }
    }
    if (asleep) {
        uint32_t z = (ui.frame / 25u + (uint32_t)cx) % 3u;
        cv_text(hx + 5, hy - 18 - (int32_t)z * 2, &FONT_XS, "z", RGB(170, 170, 200));
    }
}

static int32_t seat_x(int s) { return 18 + s * 29; }

static void draw_seats(void)
{
    const world_t *w = &proj.w;
    int s, c, n = w->strangers, used = 0;
    uint32_t h0 = hh(w->seed, w->station * 7u + 1u);
    for (c = 0; c < NREG; c++)
        if (w->who[c].aboard && w->who[c].seat < NSEAT)
            used |= 1 << w->who[c].seat;
    for (s = 0; s < NSEAT && n; s++) {       /* strangers in the free seats, the same ones for the whole stretch */
        int k = (int)((h0 + (uint32_t)s * 5u) % NSEAT);
        if (used >> k & 1)
            continue;
        used |= 1 << k;
        draw_person(seat_x(k), 82, -1, (hh(h0, (uint32_t)k) % 5u) == 0u, hh(h0, (uint32_t)k));
        n--;
    }
    for (c = 0; c < NREG; c++)
        if (w->who[c].aboard && w->who[c].seat < NSEAT && c != CH_BISCUIT)
            draw_person(seat_x(w->who[c].seat), 82, c, w->who[c].asleep, 0);
    for (s = 0; s < NSEAT; s++) {            /* the seat backs, in front of them */
        int32_t x = seat_x(s) - 12;
        cv_round(x, 100, 25, 44, 4, N_SEAT);
        cv_rect(x + 2, 116, 21, 28, N_SEAT2);
        cv_rect(x + 4, 101, 17, 5, RGB(206, 196, 176));
    }
    if (w->who[CH_THEO].aboard && w->who[CH_THEO].seat < NSEAT) {   /* the cello case, beside him */
        int32_t x = seat_x(w->who[CH_THEO].seat) + 10;
        cv_round(x, 74, 7, 64, 3, RGB(70, 40, 28));
        cv_rect(x + 2, 80, 3, 1, RGB(150, 120, 60));
    }
    if (w->who[CH_BISCUIT].aboard && w->who[CH_BISCUIT].seat < NSEAT)   /* the cat, on top of the seat back */
        draw_person(seat_x(w->who[CH_BISCUIT].seat), 89, CH_BISCUIT, w->who[CH_BISCUIT].asleep, 0);
}

static void draw_window(void)
{
    const world_t *w = &proj.w;
    uint32_t h;
    int land = w->land, k;
    if (ui.frame % 3u)                       /* 20 frames a second */
        return;
    if (ui.help) {
        h = hash(0x48454C50u, ui.help);
    } else {
        h = hash(hash(2166136261u, (uint32_t)(scroll_m * 8.0f)), ui.frame / 6u);
        for (k = 0; k < NREG; k++)
            h = hash(h, w->who[k].aboard | w->who[k].seat << 1 | w->who[k].asleep << 5);
        h = hash(h, w->strangers | (uint32_t)land << 4 | w->rain << 8 | w->phase << 9 | w->station << 12);
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, WIN_BAND, N_WALL);
    if (ui.help) {
        const char *l[4];
        int n = help_lines(ui.help - 1, l), j;
        cv_round(14, 12, 212, 118, 6, N_PANEL);
        cv_round(14, 12, 212, 26, 6, N_ACC);
        cv_rect(14, 32, 212, 6, N_ACC);
        text_c(120, 16, &FONT_B, BTN_LABEL[ui.help - 1], N_BG);
        for (j = 0; j < n; j++)
            text_c(120, 52 + j * 20, &FONT_S, l[j] + (l[j][0] == ' '), N_TEXT);
        cv_blit(0, WIN_Y);
        return;
    }
    cv_band(6, SH);
    draw_sky(land);
    if (world_in_tunnel(w) && w->tun1 - w->pos > 40.0f && w->pos - w->tun0 > 40.0f) {
        draw_tunnel();
    } else {
        draw_far(land);
        draw_mid(land);
        if (w->phase == PH_SIGNAL) {         /* the signal ahead, red */
            int32_t sx = near_x(w->sig + 24.0f - w->pos);
            cv_rect(sx, 20, 3, SH - 20, N_NEAR);
            cv_round(sx - 4, 16, 11, 20, 3, N_NEAR);
            cv_disc(sx + 1, 22, 3, N_RED);
            cv_rect(sx - 8, 22, 19, 1, mix(N_MID, N_RED, 6));
        }
        draw_near(land);
        draw_bridge();
        draw_tunnel();
        if (w->station > 0u || w->phase != PH_DWELL)
            draw_platform(0.0f - w->pos, w->station ? w->station - 1u : 0u);
        draw_platform(w->len - w->pos, w->station);
        if (w->rain && !world_in_tunnel(w))
            draw_weather(land);
    }
    cv_band(0, WIN_BAND);
    cv_rect(0, 0, 240, 6, N_WALL);           /* the wall, the frame, the curtains */
    cv_rect(0, 0, 8, 108, N_WALL);
    cv_rect(232, 0, 8, 108, N_WALL);
    cv_rect(6, 4, 228, 2, N_FRAME);
    cv_rect(6, 4, 2, 104, N_FRAME);
    cv_rect(232, 4, 2, 104, N_FRAME);
    for (k = 0; k < 12; k++) {
        uint16_t c = (k & 3) == 1 ? N_SEAT2 : N_CURT;
        cv_rect(8 + k, 6, 1, 100 - k * 3, c);
        cv_rect(231 - k, 6, 1, 100 - k * 3, c);
    }
    cv_rect(0, 107, 240, 37, N_WALL2);
    cv_rect(6, 106, 228, 3, N_FRAME);
    draw_seats();
    cv_blit(0, WIN_Y);
}

/* the strip under the window: the next station and how far, the stretch as a line, the newest line of the log */
static void draw_info(void)
{
    const world_t *w = &proj.w;
    char nm[24], t[64], d[16];
    const logrec_t *r = world_log(w, 0);
    int fresh = r && plat_ms() - ui.log_t < 8000u;
    uint32_t h = hash(hash(2166136261u, (uint32_t)(world_left_km(w) * 10.0f) | (uint32_t)w->phase << 20 | w->step << 23),
                      w->station ^ (uint32_t)w->nlog << 16 ^ (uint32_t)fresh << 31);
    h = hash(h, (uint32_t)(w->pos * 0.05f));
    if (ui.frame % 6u || h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, INFO_H, N_BG);
    cv_rect(0, 0, 240, 1, N_LINE);
    if (world_at_station(w)) {
        station_name(w->seed, w->station, nm);
        cv_text(8, 4, &FONT_XS, "AT", N_DIM);
        cv_text(28, 2, &FONT_B, nm, N_TEXT);
        cv_text(232 - text_w(&FONT_XS, "doors open"), 4, &FONT_XS, "doors open", N_ACC);
    } else {
        station_name(w->seed, w->station, nm);
        cv_text(8, 4, &FONT_XS, w->phase == PH_DWELL ? "TO" : "NEXT", N_DIM);
        cv_text(w->phase == PH_DWELL ? 28 : 40, 2, &FONT_B, nm, N_TEXT);
        if (w->phase == PH_SIGNAL && w->speed == 0.0f) {
            cv_text(232 - text_w(&FONT_XS, "signal stop"), 4, &FONT_XS, "signal stop", N_RED);
        } else {
            km_text(world_left_km(w), d);
            cv_text(232 - text_w(&FONT_S, d), 2, &FONT_S, d, N_ACC);
        }
    }
    {   /* the stretch: the line, its tunnel and bridge, the train on it */
        float L = w->len > 1.0f ? w->len : 1.0f;
        int32_t x0 = 12, x1 = 228, tx = x0 + (int32_t)((float)(x1 - x0) * fm_clampf(w->pos / L, 0.0f, 1.0f));
        cv_rect(x0, 26, x1 - x0, 1, N_LINE);
        if (w->tun1 > w->tun0)
            cv_rect(x0 + (int32_t)((float)(x1 - x0) * w->tun0 / L), 25, (int32_t)((float)(x1 - x0) * (w->tun1 - w->tun0) / L) + 1, 3, RGB(90, 84, 100));
        if (w->brg1 > w->brg0)
            cv_rect(x0 + (int32_t)((float)(x1 - x0) * w->brg0 / L), 25, (int32_t)((float)(x1 - x0) * (w->brg1 - w->brg0) / L) + 1, 3, RGB(80, 120, 200));
        cv_disc(x0, 26, 3, N_DIM);
        cv_disc(x1, 26, 3, N_TEXT);
        cv_round(tx - 5, 23, 11, 7, 3, N_ACC);
    }
    if (r) {
        world_text(w, r, t, sizeof t);
        text_wrap(8, 36, 224, &FONT_XS, 16, t, fresh ? N_ACC : N_TEXT, 2);
    }
    cv_blit(0, INFO_Y);
}

/* ---- the pages over the window and the strip (218 rows: two canvases of 109) */
#define PAGE_H 109
static void page_title(const char *t, const char *right)
{
    cv_text(8, 2, &FONT_B, t, N_ACC);
    if (right)
        cv_text(232 - text_w(&FONT_XS, right), 4, &FONT_XS, right, N_DIM);
    cv_rect(8, 21, 224, 1, N_LINE);
}

static void draw_log(int32_t oy)             /* the journal: newest first, from the scroll */
{
    const world_t *w = &proj.w;
    char t[64], meta[32];
    int i;
    if (oy == 0) {
        char r[24];
        itoa_u((uint32_t)nlog_kept(), cat_s(itoa_u((uint32_t)(ui.scroll + 1u), r), " / "));
        page_title("JOURNAL", r);
    }
    for (i = 0; i < 6; i++) {
        const logrec_t *rec = world_log(w, ui.scroll + i);
        int32_t y = 26 + i * 32 - oy;
        if (!rec)
            break;
        if (y > PAGE_H || y + 32 < 0)
            continue;
        world_text(w, rec, t, sizeof t);
        cat_s(itoa_u(rec->km, meta), " km");
        cv_text(232 - text_w(&FONT_XS, meta), y, &FONT_XS, meta, N_DIM);
        text_wrap(8, y, 176, &FONT_XS, 14, t, i == 0 && ui.scroll == 0 ? N_ACC : N_TEXT, 2);
        cv_rect(8, y + 30, 224, 1, N_PANEL);
    }
    if (!w->nlog && oy == 0)
        cv_text(8, 30, &FONT_XS, "Nothing yet.", N_DIM);
}

static void draw_pass(int32_t oy)            /* the six regulars: where they are, their mood, their story */
{
    const world_t *w = &proj.w;
    int c;
    for (c = 0; c < NREG; c++) {
        const who_t *p = &w->who[c];
        int32_t y = 2 + c * 36 - oy, x, k;
        char t[32];
        const char *st = world_story(w, c);
        if (y > PAGE_H || y + 36 < 0)
            continue;
        draw_person(16, y + 10, c, p->aboard && p->asleep, 0);
        x = cv_text(36, y, &FONT_B, who_name(c), p->aboard ? N_TEXT : N_DIM);
        cv_text(x + 6, y + 2, &FONT_XS, who_role(c), N_DIM);
        if (p->aboard)
            cat_s(t, p->asleep ? "asleep" : "aboard");
        else
            cat_s(t, p->trips ? "off the train" : "not seen yet");
        cv_text(232 - text_w(&FONT_XS, t), y + 2, &FONT_XS, t, p->aboard ? (p->asleep ? RGB(170, 170, 210) : N_GREEN) : N_DIM);
        for (k = 0; k < 5; k++)              /* mood, five dots */
            cv_round(36 + k * 6, y + 31, 4, 3, 1, k * 2 < p->mood ? N_ACC : N_LINE);
        for (k = 0; k < world_beats(c); k++) /* the story so far */
            cv_rect(76 + k * 5, y + 32, 3, 2, k < p->beat ? RGB(150, 200, 240) : N_LINE);
        text_wrap(36, y + 16, 196, &FONT_XS, 14, st ? st : "...", st ? N_TEXT : N_DIM, 1);
        cv_rect(8, y + 35, 224, 1, N_PANEL);
    }
}

static void draw_setup(int32_t oy)
{
    const world_t *w = &proj.w;
    char t[40], v[16];
    int i;
    static const uint8_t SK[8] = {K_TUNE, K_MIDI, K_LEDS, K_LINE, K_STRETCH, K_CLACK, K_TONE, K_SPACE};
    static const char *const KL[4] = {"K1", "K2", "K3", "K4"};
    if (oy == 0)
        page_title("SETUP", "KNOBS 1-4");
    for (i = 0; i < 8; i++) {                /* the settings: the first four on the knobs here */
        int32_t y = 26 + i * 16 - oy;
        if (SK[i] == K_LINE) {               /* the line: its name across the column */
            cv_text(8, y, &FONT_XS, KL[i], N_DIM);
            cv_text(28, y, &FONT_XS, line_name(proj.line), N_ACC);
            continue;
        }
        knob_text(SK[i], v);
        cv_text(8, y, &FONT_XS, i < 4 ? KL[i] : "", N_DIM);
        cv_text(28, y, &FONT_XS, KNOB_NAME[SK[i]], N_TEXT);
        cv_text(120 - text_w(&FONT_XS, v), y, &FONT_XS, v, N_ACC);
    }
    {
        int32_t y = 26 - oy;
        cv_text(132, y, &FONT_XS, "THE JOURNEY", N_DIM);
        km_text(world_km(w), t);
        cv_text(132, y + 16, &FONT_XS, t, N_TEXT);
        cat_s(itoa_u(w->station, t), w->station == 1u ? " station" : " stations");
        cv_text(132, y + 32, &FONT_XS, t, N_TEXT);
        cat_s(itoa_u(w->run_s / 60u % 60u, cat_s(itoa_u(w->run_s / 3600u, t), " h ")), " min");
        cv_text(132, y + 48, &FONT_XS, t, N_TEXT);
        t[0] = 0;
        cat_s(cat_s(cat_s(t, EN_NOTE_NAME[mus_cur[P_ROOT] % 12]), " "), EN_MODE_NAME[mus_cur[P_MODE] % EN_NMODES]);
        cv_text(132, y + 64, &FONT_XS, t, N_TEXT);
        cv_text(132, y + 92, &FONT_XS, "NIGHT TRAIN " EN_VERSION, N_DIM);
        cat_s(itoa_u(plat_cpu_pct(), cat_s(t, "CPU ")), "%");
        cv_text(132, y + 108, &FONT_XS, t, N_DIM);
        if (batt_words(batt()))
            cv_text(132, y + 124, &FONT_XS, batt_words(batt()), N_DIM);
        cv_text(8, y + 154, &FONT_XS, "Hold a button for its card. PRESETS: express.", N_DIM);
        cv_text(8, y + 170, &FONT_XS, "FX: the next line, the train's sound.", N_DIM);
    }
}

static void draw_page(void)
{
    uint32_t h = hash(hash(2166136261u, ui.view | (uint32_t)ui.scroll << 8 | (uint32_t)ui.help << 24), proj.w.nlog);
    int k;
    if (ui.frame % 6u)
        return;
    for (k = 0; k < NREG; k++)
        h = hash(h, proj.w.who[k].aboard | proj.w.who[k].asleep << 1 | proj.w.who[k].mood << 2 | proj.w.who[k].beat << 6);
    for (k = 0; k < 4; k++)
        h = hash(h, (uint32_t)knob_get(VIEW_KNOB[ui.view][k]) + 1000u * (uint32_t)k);
    if (ui.view == V_SETUP)
        h = hash(h, plat_cpu_pct() | (uint32_t)(world_km(&proj.w) * 10.0f) << 8 | (uint32_t)(batt() + 1) << 28);
    if (h == ui.sig[3])
        return;
    ui.sig[3] = h;
    for (k = 0; k < 2; k++) {
        cv_begin(240, PAGE_H, N_BG);
        if (ui.view == V_LOG)
            draw_log(k * PAGE_H);
        else if (ui.view == V_PASS)
            draw_pass(k * PAGE_H);
        else
            draw_setup(k * PAGE_H);
        cv_blit(0, (uint32_t)(WIN_Y + k * PAGE_H));
    }
}

static void leds(void)
{
    uint32_t b = 0, k = 0, i, ap = pool_abs();
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (en_frozen)
        b |= 1u << B_REC;
    if (proj.leds)
        for (i = 0; i < NKEYS; i++)
            if (ap >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    plat_glow(proj.leds == 1);
    plat_leds(b, k);
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader on the computer (announce.c; fm1-reader): a page once it settles; a knob's
 * value as it turns; a help card as it comes up; each new line of the journal as it is written (queued); what a
 * button did (say). WHERE: the whole page. Nothing is built while no reader listens. */
static struct {
    uint8_t view, help, started, playing;
    int16_t val[4];
    uint16_t nlog;
} sp;
static char sp_buf[ANN_MAX + 1];
static uint32_t sp_n;
static void sp_cat(const char *s)
{
    while (*s && sp_n < ANN_MAX)
        sp_buf[sp_n++] = *s++;
    sp_buf[sp_n] = 0;
}
static void sp_int(uint32_t v)
{
    char t[12];
    itoa_u(v, t);
    sp_cat(t);
}
static void sp_knob(int k)
{
    char t[16];
    knob_text(k, t);
    sp_cat(KNOB_NAME[k]);
    sp_cat(" ");
    sp_cat(t);
}
static void sp_entry(int back)
{
    char t[64];
    const logrec_t *r = world_log(&proj.w, back);
    if (!r) {
        sp_cat("Nothing yet");
        return;
    }
    world_text(&proj.w, r, t, sizeof t);
    sp_cat(t);
}
static void sp_page(int all)
{
    const world_t *w = &proj.w;
    char t[24];
    int i;
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat(VIEW_NAME[ui.view]);
    sp_cat(" page: ");
    switch (ui.view) {
    case V_WINDOW:
        sp_cat(land_name(w->land));
        station_name(w->seed, w->station, t);
        if (world_at_station(w)) {
            sp_cat(", at ");
            sp_cat(t);
        } else {
            sp_cat(", next ");
            sp_cat(t);
            sp_cat(", ");
            km_text(world_left_km(w), t);
            sp_cat(t);
            if (w->phase == PH_SIGNAL && w->speed == 0.0f)
                sp_cat(", signal stop");
        }
        break;
    case V_LOG:
        sp_cat("entry ");
        sp_int(ui.scroll + 1u);
        sp_cat(" of ");
        sp_int((uint32_t)nlog_kept());
        sp_cat(". ");
        sp_entry(ui.scroll);
        break;
    case V_PASS:
        for (i = 0; i < NREG; i++) {
            if (i)
                sp_cat(", ");
            sp_cat(who_name(i));
            sp_cat(w->who[i].aboard ? (w->who[i].asleep ? " asleep" : " aboard") : " off the train");
        }
        break;
    default:
        for (i = 0; i < 4; i++) {
            if (i)
                sp_cat(", ");
            sp_knob(VIEW_KNOB[V_SETUP][i]);
        }
        sp_cat(". Journey ");
        km_text(world_km(w), t);
        sp_cat(t);
        sp_cat(", ");
        sp_int(w->station);
        sp_cat(" stations");
        break;
    }
    if (!all)
        return;
    sp_cat(sp.playing ? ". Running" : ". Standing");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static void sp_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat(BTN_LABEL[ui.help - 1]);
    sp_cat(" button");
    for (k = 0; k < n; k++) {
        char p = sp_n ? sp_buf[sp_n - 1] : 0;
        const char *s = l[k];
        if (*s == ' ')
            s++;
        else if (p != ',' && p != ':')
            sp_cat(".");
        sp_cat(" ");
        sp_cat(s);
    }
}
static void sp_snap(void)
{
    int i;
    for (i = 0; i < 4; i++)
        sp.val[i] = (int16_t)knob_get(VIEW_KNOB[ui.view][i]);
    sp.view = ui.view;
    sp.help = ui.help;
    sp.nlog = proj.w.nlog;
    sp.playing = en_playing;
}
static void speak(void)
{
    uint32_t now = plat_ms();
    int where = ann_where(), i, changed = -1;
    if (!ann_on()) {
        sp.started = 0;
        sp_said = 0;
        return;
    }
    if (!sp.started || where) {
        sp.started = 1;
        sp_snap();
        if (ui.help) {
            sp_help();
            ann_say(sp_buf, 0, ANN_NOW);
        }
        sp_page(1);
        ann_say(sp_buf, 0, ui.help ? 0 : ANN_NOW);
        sp_said = 0;
        return;
    }
    if (sp_said) {
        sp_said = 0;
        sp_snap();
        return;
    }
    sp.playing = en_playing;
    if (ui.help != sp.help) {
        sp.help = ui.help;
        if (ui.help) {
            sp_help();
            ann_say(sp_buf, 0, ANN_NOW);
            return;
        }
    }
    if (ui.view != sp.view) {
        sp_snap();
        sp_page(0);
        ann_later(sp_buf, ANN_NOW, 200u, now);
        return;
    }
    for (i = 0; i < 4; i++) {
        int v = knob_get(VIEW_KNOB[ui.view][i]);
        if (v != sp.val[i]) {
            sp.val[i] = (int16_t)v;
            if (changed < 0)
                changed = i;
        }
    }
    if (changed >= 0) {
        int k = VIEW_KNOB[ui.view][changed];
        sp_n = 0;
        sp_buf[0] = 0;
        if (k == K_SCROLL)
            sp_entry(ui.scroll);
        else
            sp_knob(k);
        ann_later(sp_buf, ANN_NOW, 120u, now);
    }
    while (sp.nlog != proj.w.nlog) {             /* the journal's new lines: queued */
        int back = (int)(uint16_t)(proj.w.nlog - sp.nlog) - 1;
        sp.nlog++;
        if (back >= NLOG)
            continue;
        sp_n = 0;
        sp_buf[0] = 0;
        sp_entry(back);
        ann_say(sp_buf, 0, 0);
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card, message and kind of journal line as a screen reader gets them */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"},
                                         {"NEXT CHORD", "ON THE BAR"}, {"NEXT CHORD", 0}, {"THE TRAIN", "RUNS"},
                                         {"THE TRAIN", "STANDS"}, {"FREEZE", "ON"}, {"FREEZE", "OFF"}, {"SAVED", 0},
                                         {"EXPRESS", "NEXT STOP"}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}, {"NOT NOW", "AT THE STATION"},
                                          {"NOT NOW", "STOPPING"}};
    static project_t keep;
    uint8_t keep_view = ui.view, keep_help = ui.help;
    int v, i, x, lo, hi;
    char t[64];
    keep = proj;
    for (v = 0; v < NVIEWS; v++) {
        ui.view = (uint8_t)v;
        sp_page(0);
        sp_print("SCREEN", sp_buf);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        for (i = 0; i < 4; i++) {
            int k = VIEW_KNOB[v][i];
            if (k == K_NONE || k == K_SCROLL)
                continue;
            knob_range(k, &lo, &hi);
            for (x = lo; x <= hi; x++) {
                knob_set(k, x);
                sp_n = 0;
                sp_knob(k);
                sp_print("KNOB", sp_buf);
            }
            proj = keep;
        }
    }
    ui.view = keep_view;
    for (i = 0; i < NB; i++) {
        const char *l[HELP_LINES];
        if (!help_lines(i, l))
            continue;
        ui.help = (uint8_t)(i + 1);
        sp_help();
        sp_print("HELP", sp_buf);
    }
    ui.help = keep_help;
    for (i = 0; i < (int)(sizeof MSG / sizeof MSG[0]); i++) {
        msg_text(MSG[i][0], MSG[i][1], t);
        sp_print("MSG", t);
    }
    for (i = 0; i < NLINES; i++) {            /* FX: the next line */
        char n[24];
        caps(n, line_name(i), sizeof n);
        msg_text("LINE", n, t);
        sp_print("MSG", t);
    }
    for (i = 0; i < (int)(sizeof WARN / sizeof WARN[0]); i++) {
        msg_text(WARN[i][0], WARN[i][1], t);
        sp_print("WARN", t);
    }
    for (i = 1; i < E_NTYPES; i++)               /* a line of each kind of the journal */
        for (x = 0; x < 8; x++) {
            logrec_t r;
            r.type = (uint8_t)i;
            r.a = (uint8_t)(x % NREG);
            r.b = (uint8_t)((x + 1) % NREG);
            r.arg = (uint8_t)x;
            r.station = (uint16_t)(x + 1);
            r.km = 0;
            world_text(&proj.w, &r, t, sizeof t);
            sp_print("LOG", t);
        }
    proj = keep;
    for (x = 0; x <= 4; x++)
        sp_print("WHERE", batt_words(x));
}
#endif

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.prev = plat_ms();
    ui.saved_t = plat_ms();
    ui.log_n = proj.w.nlog;
    lcd_fill(0, 0, 240, 240, N_BG);
    if (!en_playing)
        en_play(1);                              /* the train runs from the start */
    sp.playing = 1;
}

void ui_frame(void)
{
    uint32_t now = plat_ms();
    input();
    journey(now);
    music_tick(now);
    if (ui.log_n != proj.w.nlog) {
        ui.log_n = proj.w.nlog;
        ui.log_t = now;
        if (ui.scroll)                           /* the journal stays on the line being read */
            knob_set(K_SCROLL, ui.scroll + 1);
    }
    autosave();
    draw_header();
    if (ui.view == V_WINDOW || ui.help) {
        draw_window();
        draw_info();
        ui.sig[3] = 0;
    } else {
        draw_page();
        ui.sig[1] = ui.sig[2] = 0;
    }
    leds();
    speak();
    ann_tick(now);
    ui.frame++;
}

void ui_input_only(void) { input(); }
