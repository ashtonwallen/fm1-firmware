/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CAVE: ashtonwallen, 2026 */
/* CAVE's UI: the panel and the screen (from AMB-1's by way of FELT PIANO: its knobs, help cards, messages and screen
 * reader). Part of the unity build (after gfx.c and project.c); the host simulator includes it too.
 *
 *   PLAY           playing / resting                 EDIT   the river moves on (its next harmony)
 *   PRESETS        the cave (each kept as left)      ALGORITHM     Ambience
 *   HOME ENV GLO   the pages: Cave, Space, Setup (SELECT steps through them)
 *   KNOB 1-4       the page's four values            OCT- / OCT+   the keys' octave
 *   keys           a pebble into a pool (the note snapped to the cave's set)
 *   SAVE           saves (it also saves by itself at a quiet moment: see autosave); held 1.5 s: the cave's knobs
 *                  back to its recipe */
#include "fastmath.h"

enum { V_CAVE, V_SPACE, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Cave", "Space", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_GLO};
enum { K_LEDS = 100, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_DRIPS, P_RING, P_RIVER, P_AMB},
    {P_CAVERN, P_ECHO, P_TONE, P_WIDTH},
    {P_GROWTH, P_EVENTS, P_PEBBLE, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Drips", "Ring", "River", "Ambience"},
    {"Cavern", "Echo", "Tone", "Width"},
    {"Growth", "Events", "Pebbles", "Lights"},
};

/* the screen's colours: wet limestone by lamplight: blue-black rock, cream stone, teal water, amber light */
#define K_BG RGB(9, 11, 18)
#define K_PANEL RGB(30, 34, 46)
#define K_LINE RGB(48, 54, 70)
#define K_DIM RGB(122, 130, 150)
#define K_TEXT RGB(232, 228, 214)
#define K_ACC RGB(236, 186, 110)
#define K_SOFT RGB(110, 196, 206)
#define K_LOW RGB(210, 92, 64)                 /* the battery at one bar or none */

static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];
    uint32_t down_t[NB];
    uint32_t turn_t;
    uint8_t help;                      /* a button's card is up: the button + 1 */
    uint8_t reset_done;
    uint8_t dirty;                     /* the settings changed since the last save */
    uint32_t act_t, saved_t;           /* the last touch, the last save */
    char msg[2][28];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    uint32_t keys_on;
} ui;

/* the screen reader: a message went to it this frame, so speak() leaves what it changed be */
static uint8_t sp_said;
static struct {
    uint8_t playing;
    uint32_t play_t;
} sp_tp;
static void msg_text(const char *a, const char *b, char *t)
{
    uint32_t i, k = 0;
    for (i = 0; i < 27u && a && a[i]; i++)
        t[k++] = a[i];
    if (b && b[0])
        t[k++] = ' ';
    for (i = 0; i < 27u && b && b[i]; i++)
        t[k++] = b[i];
    while (k && t[k - 1] == '.')
        k--;
    t[k] = 0;
}
static void say_as(const char *a, const char *b, uint32_t flags)
{
    uint32_t i;
    char t[64];
    for (i = 0; i < 27u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 27u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1400u;
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
static char *cat_age(char *b, uint32_t min)      /* "3h 05m", "12m", "2d 4h" */
{
    if (min >= 2880u) {
        b = cat_s(cat_u(b, min / 1440u), "d ");
        return cat_s(cat_u(b, min % 1440u / 60u), "h");
    }
    if (min >= 60u) {
        b = cat_s(cat_u(b, min / 60u), "h ");
        *b++ = (char)('0' + min % 60u / 10u);
        *b++ = (char)('0' + min % 10u);
        return cat_s(b, "m");
    }
    return cat_s(cat_u(b, min), "m");
}

static void mark_dirty(void)
{
    ui.dirty = 1;
    ui.act_t = plat_ms();
}
int ui_dirty(void) { return ui.dirty != 0; }

/* ------------------------------------------------------------ values --- */
static int knob_get(int k)
{
    if (k < P_NPARAMS)
        return proj.par[k];
    if (k == K_LEDS)
        return proj.leds;
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = en_param_info(k)->lo;
        *hi = en_param_info(k)->hi;
    } else {
        *lo = 0;
        *hi = k == K_LEDS ? 2 : 0;
    }
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k < P_NPARAMS)
        en_param_text(k, v, b);
    else if (k == K_LEDS)
        cat_s(b, v == 2 ? "Keys" : v ? "On" : "Off");
    else
        b[0] = 0;
}
static void knob_set(int k, int v)
{
    int lo, hi;
    if (k == K_NONE)
        return;
    knob_range(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (v == knob_get(k))
        return;
    if (k < P_NPARAMS) {
        proj.par[k] = (int16_t)v;
        en_set(k, v);
    } else if (k == K_LEDS) {
        proj.leds = (uint8_t)v;
    }
    mark_dirty();
}

/* knob acceleration by speed (X0X's): a few clicks are one step each; a longer quick turn speeds up */
#ifdef PLAT_HOST
#define accel_off plat_host_exact            /* the simulator's "spin": exact steps */
#else
#define accel_off 0
#endif
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
    m = dt < 12u ? 8u : dt < 25u ? 5u : dt < 45u ? 3u : 1u;
    if (range < 100 && m > 3u)
        m = 3u;
    return s * (int32_t)m;
}

static void turn(int role, int k, int32_t e)
{
    int lo, hi;
    if (!e || k == K_NONE)
        return;
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------- actions --- */
static void sp_style(void);
static void set_style(int i)                    /* the cave playing kept as it is; cave i as it was left */
{
    project_style(i);
    project_apply();
    if (!en_playing)
        en_play(1);
    sp_tp.playing = 1;
    say_as("CAVE", en_style_name(proj.style), 0);
    sp_style();
    mark_dirty();
}

static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
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
    case B_EDIT:
        en_next_harm();
        say("THE RIVER", "MOVES ON");
        if (!en_playing) {
            en_play(1);
            sp_tp.playing = 1;
        }
        break;
    case B_PLAY:
        sp_tp.playing = !en_playing;
        sp_tp.play_t = plat_ms();
        if (en_playing) {
            en_play(0);
            say("RESTING", 0);
        } else {
            en_play(1);
            say("PLAYING", 0);
        }
        break;
    case B_OCTDN: case B_OCTUP: {
        int o = proj.oct + (b == B_OCTUP ? 1 : -1);
        char t[8];
        if (o < 0 || o > 4) {
            warn("KEYS OCTAVE", o < 0 ? "LOWEST" : "HIGHEST");
            break;
        }
        proj.oct = (uint8_t)o;
        mark_dirty();
        t[0] = o < 2 ? '-' : '+';
        t[1] = (char)('0' + (o < 2 ? 2 - o : o - 2));
        t[2] = 0;
        say("KEYS OCTAVE", o == 2 ? "0" : t);
        break;
    }
    case B_SAVE:
        if (project_save() == 0) {
            ui.dirty = 0;
            ui.saved_t = plat_ms();
            say("SAVED", 0);
        } else {
            warn("NOT SAVED", "FLASH ERROR");
        }
        break;
    default: break;
    }
}

/* MIDI in (USB and the TRS jack): notes drop pebbles too */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0)) {
        uint32_t st = (pkt >> 8) & 0xF0u, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == 0x90u && v)
            en_note((int)n, (int)v);
    }
}

/* HELP CARDS (X0X's): hold one button alone for HELP_HOLD and a card says what it does. A key, a turn or
 * another button puts it away. PLAY acts on its press; SEL, and the buttons with no job, have no card */
#define HELP_LINES 4
#define HELP_HOLD 900u
#define RESET_HOLD 1500u
static int help_lines(int b, const char *l[HELP_LINES]);
static void help_tick(uint32_t btn, uint32_t keys)
{
    uint32_t now = plat_ms();
    int i;
    const char *l[HELP_LINES];
    if (ui.help) {
        uint32_t m = 1u << (ui.help - 1);
        if (btn != m || keys || (int32_t)(ui.turn_t - ui.down_t[ui.help - 1]) > 0)
            ui.help = 0;
        return;
    }
    if (!btn || (btn & (btn - 1u)) || keys)
        return;
    for (i = 0; i < NB; i++)
        if (btn == 1u << i && i != B_PLAY && help_lines(i, l) && !(ui.btn_used & btn) &&
            now - ui.down_t[i] >= HELP_HOLD && (int32_t)(ui.turn_t - ui.down_t[i]) <= 0) {
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
            if (i == B_SAVE)
                ui.reset_done = 0;
            if (i == B_PLAY)
                button((int)i);
        } else if (!(ui.btn_used & m) && i != B_PLAY) {
            button((int)i);
        }
    }
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++)
        if ((ch >> i & 1u) && (keys >> i & 1u))  /* a key: a pebble (key 0 is F3) */
            en_note(53 + (int)i + (proj.oct - 2) * 12, 100);
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {        /* SELECT: the pages */
        int v = (int)ui.view + (e > 0 ? 1 : -1);
        if (v >= 0 && v < NVIEWS)
            set_view(v);
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {          /* ALGORITHM: Ambience, from any page */
        char t[12];
        turn(EN_ALGO, P_AMB, e);
        en_param_text(P_AMB, proj.par[P_AMB], t);
        if (ui.view == V_CAVE) {
            ui.touched = 3;
            ui.touch_until = plat_ms() + 1200u;
        } else {
            say("AMBIENCE", t);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        set_style(proj.style + (e > 0 ? 1 : -1));
        ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && plat_ms() - ui.down_t[B_SAVE] > RESET_HOLD) {
        ui.reset_done = 1;                       /* held: the cave's knobs back to its recipe (not saved) */
        ui.btn_used |= 1u << B_SAVE;
        ui.help = 0;
        project_style_reset();
        {
            int p;
            for (p = 0; p < P_NPARAMS; p++)
                en_set(p, proj.par[p]);
        }
        say("RESET", en_style_name(proj.style));
        mark_dirty();
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            turn(EN_K1 + (int)i, VIEW_KNOB[ui.view][i], e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 1200u;
            ui.turn_t = plat_ms();
        }
    if (btn || keys)
        ui.act_t = plat_ms();
    help_tick(btn, keys);
}

/* ------------------------------------------------------------- screen --- */
/* three bands: the header (28 px), the cavern (144), the knobs (68) */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68

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

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

static uint16_t mix(uint16_t a, uint16_t b, int t)   /* t 0..16: a -> b */
{
    uint32_t r, g, bl, u = (uint32_t)(t < 0 ? 0 : t > 16 ? 16 : t);
    r = ((uint32_t)(a >> 11) * (16u - u) + (uint32_t)(b >> 11) * u) >> 4;
    g = ((uint32_t)((a >> 5) & 63u) * (16u - u) + (uint32_t)((b >> 5) & 63u) * u) >> 4;
    bl = ((uint32_t)(a & 31u) * (16u - u) + (uint32_t)(b & 31u) * u) >> 4;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static int q16(float x) { return x <= 0.0f ? 0 : x >= 1.0f ? 16 : (int)(x * 16.0f + 0.5f); }

/* anti-aliased drawing (X0X's): a pixel blended over the canvas, a disc */
static void px_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *q;
    uint32_t bg;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1 || !a)
        return;
    q = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    if (a >= 16u) {
        *q = swap16(c);
        return;
    }
    bg = swap16(*q);
    *q = swap16((uint32_t)(((((c >> 11) * a + (bg >> 11) * (16u - a)) >> 4) << 11) |
                           (((((c >> 5) & 63u) * a + ((bg >> 5) & 63u) * (16u - a)) >> 4) << 5) |
                           (((c & 31u) * a + (bg & 31u) * (16u - a)) >> 4)));
}
static void disc(float cx, float cy, float r, uint16_t c)
{
    int32_t x0 = (int32_t)fm_floorf(cx - r - 1.0f), x1 = (int32_t)(cx + r + 1.0f);
    int32_t y0 = (int32_t)fm_floorf(cy - r - 1.0f), y1 = (int32_t)(cy + r + 1.0f), x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy);
            px_blend(x, y, c, (uint32_t)(fm_clampf(r - d + 0.5f, 0.0f, 1.0f) * 16.0f + 0.5f));
        }
}
/* a soft glow: a disc fading out from its centre, a (0..1) at the middle */
static void glow(float cx, float cy, float r, uint16_t c, float a)
{
    int32_t x0 = (int32_t)fm_floorf(cx - r), x1 = (int32_t)(cx + r + 1.0f);
    int32_t y0 = (int32_t)fm_floorf(cy - r), y1 = (int32_t)(cy + r + 1.0f), x, y;
    float k = 1.0f / (r * r);
    if (a <= 0.02f)
        return;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = 1.0f - (dx * dx + dy * dy) * k;
            if (d > 0.0f)
                px_blend(x, y, c, (uint32_t)(d * d * a * 16.0f));
        }
}
/* a thin anti-aliased line: discs along it */
static void rod(float x0, float y0, float x1, float y1, float r, uint16_t c)
{
    float dx = x1 - x0, dy = y1 - y0, len = fm_sqrtf(dx * dx + dy * dy), t;
    int i, n = (int)(len / 1.2f) + 1;
    for (i = 0; i <= n; i++) {
        t = (float)i / (float)n;
        disc(x0 + dx * t, y0 + dy * t, r, c);
    }
}

/* atan2 in turns, 0..1 from +x going clockwise on screen (y down) */
static float turns(float y, float x)
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), a = ax < ay ? ax / (ay + 1e-9f) : ay / (ax + 1e-9f), s = a * a, r;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax)
        r = 1.57079637f - r;
    if (x < 0.0f)
        r = FM_PI - r;
    if (y < 0.0f)
        r = FM_TWO_PI - r;
    return r * (1.0f / FM_TWO_PI);
}

/* the knob ring (AMB-1's): a 270-degree track, the value filled, a round handle */
static void arc(int32_t cx, int32_t cy, int32_t r, float v, uint16_t track, uint16_t fill, uint16_t bg)
{
    const float th = 3.4f, a0 = 0.375f, sweep = 0.75f;
    float ro = (float)r, ri = ro - th, rm = ro - th * 0.5f, fx = (float)cx + 0.5f, fy = (float)cy + 0.5f;
    int32_t x, y;
    v = fm_clampf(v, 0.0f, 1.0f);
    for (y = -r - 1; y <= r + 1; y++)
        for (x = -r - 1; x <= r + 1; x++) {
            float dx = (float)x, dy = (float)y, d = fm_sqrtf(dx * dx + dy * dy), t, a;
            a = fm_clampf(ro + 0.5f - d, 0.0f, 1.0f) * fm_clampf(d - ri + 0.5f, 0.0f, 1.0f);
            if (a <= 0.0f)
                continue;
            t = turns(dy, dx) - a0;
            if (t < 0.0f)
                t += 1.0f;
            t /= sweep;
            if (t > 1.0f)
                continue;
            px_blend(cx + x, cy + y, t <= v ? fill : track, (uint32_t)(a * 16.0f + 0.5f));
        }
    {
        float e0 = a0 * FM_TWO_PI, e1 = (a0 + sweep) * FM_TWO_PI, ev = (a0 + sweep * v) * FM_TWO_PI;
        disc(fx + fm_cosf(e0) * rm - 0.5f, fy + fm_sinf(e0) * rm - 0.5f, th * 0.5f, v > 0.0f ? fill : track);
        disc(fx + fm_cosf(e1) * rm - 0.5f, fy + fm_sinf(e1) * rm - 0.5f, th * 0.5f, v >= 1.0f ? fill : track);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th * 0.5f + 1.3f, bg);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th * 0.5f + 0.6f, fill);
    }
}

static void caps(char *d, const char *s, int n)
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}

/* the battery (plat_battery): 0 (low) .. 3 bars, 4 on USB power, -1 not read yet */
#ifdef PLAT_HOST
static int8_t batt_host = -2;
static int batt(void) { return batt_host > -2 ? batt_host : plat_battery(); }
#else
#define batt() plat_battery()
#endif
#define BATT_W 20
static void draw_batt(int32_t x, int b)
{
    static const int8_t BOLT[7][2] = {{10, 2}, {9, 2}, {8, 2}, {7, 5}, {9, 2}, {8, 2}, {7, 2}};
    uint16_t c = b <= 1 ? K_LOW : K_DIM;
    int i;
    cv_round(x, 9, 18, 10, 2, c);
    cv_rect(x + 1, 10, 16, 8, K_BG);
    cv_rect(x + 18, 12, 2, 4, c);
    if (b == 4) {
        for (i = 0; i < 7; i++)
            cv_rect(x + BOLT[i][0], 10 + i, BOLT[i][1], 1, K_TEXT);
        return;
    }
    for (i = 0; i < b && i < 3; i++)
        cv_rect(x + 2 + i * 5, 11, 4, 6, b <= 1 ? K_LOW : K_ACC);
}
static const char *batt_words(int b)
{
    static const char *const W[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                     "On USB power"};
    return b >= 0 && b <= 4 ? W[b] : 0;
}

/* the header: the cave in a tag; on the right the battery, unsaved, the river's note in a pill, resting */
static void draw_header(void)
{
    char t[16];
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(hash(2166136261u, proj.style | (uint32_t)en_harm_pc << 8 | (uint32_t)en_playing << 16 |
                                                   (uint32_t)ui.dirty << 18 | (uint32_t)msg << 19),
                           (uint32_t)(batt() + 1)),
                      msg ? ui.msg_until : 0u);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], K_ACC);
    } else {
        int32_t x = 232, w;
        if (batt() >= 0) {
            x -= BATT_W;
            draw_batt(x, batt());
            x -= 8;
        }
        if (ui.dirty) {
            cv_round(x - 5, 12, 5, 5, 2, K_DIM);
            x -= 11;
        }
        cat_s(t, EN_NOTE_NAME[en_harm_pc % 12]);  /* the river's root */
        w = text_w(&FONT_S, t) + 10;
        x -= w;
        cv_round(x, 6, w, 17, 5, en_playing ? K_SOFT : K_LINE);
        text_c(x + w / 2, 6, &FONT_S, t, K_BG);
        x -= text_w(&FONT_XS, "RIVER") + 4;      /* (the river's note: the harmony now) */
        cv_text(x, 8, &FONT_XS, "RIVER", K_DIM);
        x -= 4;
        if (!en_playing) {                       /* resting: two bars */
            x -= 10;
            cv_rect(x, 9, 3, 10, K_DIM);
            cv_rect(x + 5, 9, 3, 10, K_DIM);
            x -= 4;
        }
        w = text_w(&FONT_B, en_style_name(proj.style)) + 12;
        if (4 + w < x - 2) {
            cv_round(4, 4, w, 20, 6, K_ACC);
            cv_text(10, 6, &FONT_B, en_style_name(proj.style), K_BG);
        } else {
            cv_text(6, 6, &FONT_S, en_style_name(proj.style), K_ACC);
        }
    }
    cv_blit(0, 0);
}

/* ---- the cavern in cross-section: the ceiling with its drip points, drops falling, stalagmites growing and
 * glowing as they ring, pools rippling, the river along the bottom (rising in a flood), bats, a fallen stone */
#define FLOOR_Y 96
#define INFO_Y 113
#define RIVER_H 13
static uint8_t ceil_y[240];
static uint8_t ceil_style = 255;
static en_world_t uw;                            /* the world, as last read */

static void ceil_make(int st)                    /* each cave its own ceiling: a few bumps, smooth */
{
    int x, k;
    uint32_t h = en_hash(0x524F4F46u, (uint32_t)st);
    float pts[14];
    for (k = 0; k < 14; k++) {
        h = en_hash(h, (uint32_t)k);
        pts[k] = 6.0f + (float)(h % 11u);
    }
    for (x = 0; x < 240; x++) {
        float f = (float)x / 239.0f * 13.0f, u = f - fm_floorf(f);
        int i = (int)f;
        float a = pts[i], b = pts[i < 13 ? i + 1 : 13];
        u = u * u * (3.0f - 2.0f * u);
        ceil_y[x] = (uint8_t)(a + (b - a) * u);
    }
    ceil_style = (uint8_t)st;
}

static uint16_t pc_col(int pc)                   /* a note's light: by its place in the octave, warm to cool */
{
    static const uint16_t C[12] = {
        RGB(255, 196, 120), RGB(255, 170, 120), RGB(250, 214, 140), RGB(236, 226, 150), RGB(200, 232, 160),
        RGB(160, 232, 190), RGB(130, 224, 220), RGB(140, 200, 250), RGB(170, 180, 255), RGB(200, 170, 250),
        RGB(236, 170, 220), RGB(255, 180, 170)};
    return C[(pc % 12 + 12) % 12];
}

static int stal_h(const en_obj_t *o)             /* taller as it grows (and its note falls) */
{
    float g = o->need ? (float)o->grow / (float)o->need : 0.0f;
    int h = 12 + (int)((float)(o->hi - o->deg) * 5.0f + fm_minf(g, 1.0f) * 5.0f);
    return h > 70 ? 70 : h;
}
static int pool_w(const en_obj_t *o)             /* wider, lower */
{
    int w = 14 + (o->hi - o->deg) * 3;
    return w > 40 ? 40 : w;
}
static int land_y(const en_obj_t *o) { return o->kind == OB_STAL ? FLOOR_Y - stal_h(o) : FLOOR_Y; }

/* each cave's light: the lamp's colour on its walls */
static const uint16_t LAMP[EN_NSTYLES] = {
    RGB(52, 50, 58), RGB(40, 54, 58), RGB(62, 48, 42), RGB(50, 44, 40), RGB(26, 42, 58), RGB(46, 48, 62),
    RGB(26, 26, 34), RGB(56, 46, 40), RGB(44, 40, 52), RGB(58, 62, 62), RGB(48, 44, 64), RGB(32, 44, 54)};

static void draw_back(uint32_t now)
{
    int32_t y, x, k;
    uint16_t lamp = LAMP[proj.style % EN_NSTYLES];
    (void)now;
    for (y = 0; y < MAIN_H; y++)                 /* the dark */
        cv_rect(0, y, 240, 1, mix(RGB(16, 18, 28), K_BG, y < 60 ? (60 - y) / 5 : (y - 60) / 3));
    for (k = 0; k < 6; k++) {                    /* lamplight on the far wall: soft rings, brighter inward */
        float rx = 150.0f - (float)k * 22.0f, ry = 52.0f - (float)k * 7.0f;
        uint16_t c = mix(RGB(16, 18, 28), lamp, (k + 1) * 16 / 6);
        for (y = (int32_t)(56.0f - ry); y <= (int32_t)(56.0f + ry); y++) {
            float dy = ((float)y - 56.0f) / ry, hw = rx * fm_sqrtf(fm_maxf(1.0f - dy * dy, 0.0f));
            cv_rect((int32_t)(120.0f - hw), y, (int32_t)(2.0f * hw), 1, c);
        }
    }
    {                                            /* far formations: columns and curtains in the back, dim */
        uint32_t h = en_hash(0x434F4C53u, proj.style);
        for (k = 0; k < 5; k++) {
            int32_t cx, w, top;
            h = en_hash(h, (uint32_t)k);
            cx = 10 + (int32_t)(h % 220u);
            w = 5 + (int32_t)(h >> 8) % 9;
            top = (h >> 16) & 1u ? 0 : 30 + (int32_t)(h >> 20) % 30;   /* a column, or a stalagmite far off */
            for (y = top; y < FLOOR_Y; y++) {
                float t = top ? (float)(y - top) / (float)(FLOOR_Y - top) : fm_fabsf((float)y - 48.0f) / 48.0f;
                int32_t hw = (int32_t)((float)w * (top ? fm_sqrtf(t) : 0.45f + 0.55f * t * t)) + 1;
                cv_rect(cx - hw, y, 2 * hw, 1, mix(lamp, RGB(16, 18, 28), 9));
            }
        }
    }
    for (x = 0; x < 240; x++) {                 /* the ceiling's rock and its lit lip */
        cv_rect(x, 0, 1, ceil_y[x], RGB(36, 34, 42));
        cv_pset(x, ceil_y[x], RGB(62, 58, 66));
    }
    {                                            /* the rock's grain */
        uint32_t h = 0x9E3779B9u ^ ceil_style;
        int k;
        for (k = 0; k < 70; k++) {
            int32_t px, py;
            h = en_hash(h, (uint32_t)k);
            px = (int32_t)(h % 240u);
            py = (int32_t)(h >> 12) % 100 + 12;
            if (py > ceil_y[px] + 3 && py < FLOOR_Y - 4)
                cv_pset(px, py, RGB(28, 32, 44));
            else if (py <= ceil_y[px])
                cv_pset(px, py, RGB(50, 46, 56));
        }
    }
    for (y = FLOOR_Y; y < MAIN_H; y++)           /* the floor, darker down */
        cv_rect(0, y, 240, 1, mix(mix(lamp, RGB(34, 32, 40), 8), K_BG, (y - FLOOR_Y) / 2));
    cv_rect(0, FLOOR_Y, 240, 1, mix(lamp, RGB(150, 140, 130), 8));
}

/* the strike log: each object's light now, the latest strike's age */
static float obj_lit[EN_NOBJ];
static uint8_t obj_pc[EN_NOBJ];
static void lights(uint32_t now)
{
    uint32_t i, w = en_log_w;
    int k;
    for (k = 0; k < EN_NOBJ; k++)
        obj_lit[k] = 0.0f;
    for (i = w > EN_NLOG ? w - EN_NLOG : 0u; i != w; i++) {
        en_logn_t l = *(const en_logn_t *)&EN_LOG[i % EN_NLOG];
        float age, b;
        if (!l.on || l.obj >= EN_NOBJ || (int32_t)(now - l.on) < 0)
            continue;
        age = (float)(now - l.on);
        b = (float)l.vel * (1.0f / 100.0f) * fm_expf(-age / (float)(l.tau ? l.tau : 300));
        if (b > obj_lit[l.obj]) {
            obj_lit[l.obj] = b;
            obj_pc[l.obj] = (uint8_t)l.pc;
        }
    }
}

static void draw_stal(const en_obj_t *o, int k)
{
    uint32_t hs = en_hash(0x5354414Cu, (uint32_t)(proj.style * 16 + k));
    int h = stal_h(o), top = FLOOR_Y - h, y;
    float wf = 0.8f + (float)(hs % 50u) * 0.01f, lean = (float)((int)(hs >> 8) % 5 - 2);
    float bw = (4.0f + (float)h * 0.10f) * wf, lit = fm_minf(obj_lit[k] * 1.5f, 1.0f);
    uint16_t lc = mix(RGB(206, 190, 160), RGB(214, 200, 184), (int)(hs >> 12) % 17),
             dc = mix(RGB(112, 100, 92), RGB(96, 92, 100), (int)(hs >> 16) % 17), rim = RGB(240, 232, 214);
    if (lit > 0.02f) {
        uint16_t pc = pc_col(obj_pc[k]);
        lc = mix(lc, pc, q16(lit));
        dc = mix(dc, pc, q16(lit * 0.6f));
        rim = mix(rim, RGB(255, 255, 255), q16(lit));
        glow((float)o->x + lean, (float)top + 6.0f, 12.0f + 12.0f * lit, pc, lit * 0.55f);
    }
    for (y = top; y < FLOOR_Y; y++) {
        float t = (float)(y - top) / (float)h, hw = 0.8f + (bw - 0.8f) * fm_sqrtf(t), c = (float)o->x + lean * (1.0f - t);
        float xl = c - hw, xr = c + hw;
        int32_t a = (int32_t)fm_floorf(xl + 1.0f), b = (int32_t)fm_floorf(xr), m = (int32_t)(c - hw * 0.25f);
        if (b >= a) {
            cv_rect(a, y, m - a + 1, 1, lc);
            if (b > m)
                cv_rect(m + 1, y, b - m, 1, dc);
            cv_pset(a, y, rim);                  /* the lit edge */
        }
        px_blend(a - 1, y, lc, (uint32_t)(((float)a - xl) * 15.0f));
        px_blend(b + 1, y, dc, (uint32_t)((xr - (float)b) * 15.0f));
        if ((y - top) % 6 == 4 && b > a + 1)     /* its rings of growth */
            cv_rect(a + 1, y, b - a - 1, 1, mix(lc, dc, 10));
    }
    disc((float)o->x + lean + 0.5f, (float)top + 0.8f, 1.1f, rim);
}

static void draw_pool(const en_obj_t *o, int k, uint32_t now)
{
    int w = pool_w(o), y, i;
    float lit = fm_minf(obj_lit[k] * 1.4f, 1.0f), hw = (float)w * 0.5f;
    uint16_t wc = mix(RGB(30, 104, 122), pc_col(obj_pc[k]), q16(lit * 0.6f));
    for (y = -2; y < 10; y++) {                  /* the rimstone lip, then the water, darker down */
        float yy = (float)(y < 0 ? 0 : y), e = (hw + 2.0f) * fm_sqrtf(fm_maxf(1.0f - yy * yy / 100.0f, 0.0f));
        int32_t a = (int32_t)((float)o->x - e), b = (int32_t)((float)o->x + e);
        if (y < 0) {
            cv_rect(a - 1, FLOOR_Y + y, 3, 1, RGB(150, 138, 120));
            cv_rect(b - 1, FLOOR_Y + y, 3, 1, RGB(120, 110, 100));
            continue;
        }
        cv_rect(a - 1, FLOOR_Y + y, b - a + 3, 1, RGB(92, 84, 82));
        cv_rect(a + 1, FLOOR_Y + y, b - a - 1, 1, mix(wc, RGB(10, 24, 34), y * 14 / 10));
    }
    cv_rect(o->x - (int)hw, FLOOR_Y, (int)(2.0f * hw) + 1, 1, mix(RGB(120, 200, 210), pc_col(obj_pc[k]), q16(lit)));
    if (lit > 0.02f)
        glow((float)o->x, (float)FLOOR_Y + 2.0f, hw + 8.0f, pc_col(obj_pc[k]), lit * 0.45f);
    /* ripples: rings spreading from each strike, flattened on the water */
    {
        uint32_t j, lw = en_log_w;
        for (j = lw > 16u ? lw - 16u : 0u; j != lw; j++) {
            en_logn_t l = *(const en_logn_t *)&EN_LOG[j % EN_NLOG];
            float age, r, a;
            if (l.obj != k || !l.on || (int32_t)(now - l.on) < 0 || now - l.on > 2400u)
                continue;
            age = (float)(now - l.on) * 0.001f;
            r = 2.0f + age * 16.0f;
            a = (float)l.vel * (1.0f / 127.0f) * (1.0f - age / 2.4f);
            for (i = 0; i < 40; i++) {
                float an = (float)i * (FM_TWO_PI / 40.0f), dx = fm_cosf(an) * r, dy = fm_sinf(an) * r * 0.22f;
                if (dx > hw + 3.0f || dx < -hw - 3.0f)
                    continue;
                px_blend(o->x + (int32_t)dx, FLOOR_Y + (int32_t)(dy + 0.5f), RGB(200, 240, 244), (uint32_t)(a * 14.0f));
            }
        }
    }
}

static void draw_drips(uint32_t now)
{
    int i;
    for (i = 0; i < uw.ndrip; i++) {
        const en_drip_t *d = &uw.drip[i];
        int32_t cy = ceil_y[d->x < 240 ? d->x : 239], len = 7 + (i * 7) % 12, tip = cy + len, y;
        int on = en_drips_on >> i & 1u, tl;
        float bw = 3.0f + (float)(i % 3);
        for (y = cy; y < tip; y++) {             /* the stalactite */
            float hw = bw * (1.0f - (float)(y - cy) / (float)len) + 0.4f;
            cv_rect(d->x - (int32_t)hw, y, (int32_t)(2.0f * hw) + 1, 1, on ? RGB(150, 140, 124) : RGB(84, 78, 76));
        }
        if (!on || !en_playing)
            continue;
        tl = (int32_t)(en_drip_next[i] - now);
        if (tl > 0 && tl < (int)EN_FALL_MS && d->target < uw.nobj) {   /* falling */
            float p = 1.0f - (float)tl / (float)EN_FALL_MS, ly = (float)land_y(&uw.obj[d->target]);
            float yy = (float)tip + (ly - (float)tip) * p * p;
            float tx = (float)d->x + ((float)uw.obj[d->target].x - (float)d->x) * p;
            disc(tx, yy - 4.0f, 0.7f, RGB(70, 110, 130));
            disc(tx, yy - 2.0f, 1.0f, RGB(120, 170, 190));
            disc(tx, yy, 1.8f, RGB(214, 244, 252));
        } else if (tl >= (int)EN_FALL_MS && tl < (int)EN_FALL_MS + 900) {   /* gathering at the tip */
            float g = 1.0f - (float)(tl - (int)EN_FALL_MS) / 900.0f;
            disc((float)d->x + 0.5f, (float)tip + 0.5f + g, 0.5f + 1.0f * g, RGB(170, 214, 228));
        }
    }
}

/* the strikes' splashes on stone, the trickles' glints along the ceiling */
static void draw_sparks(uint32_t now)
{
    uint32_t j, lw = en_log_w;
    for (j = lw > 24u ? lw - 24u : 0u; j != lw; j++) {
        en_logn_t l = *(const en_logn_t *)&EN_LOG[j % EN_NLOG];
        float age;
        if (!l.on || (int32_t)(now - l.on) < 0)
            continue;
        age = (float)(now - l.on) * 0.001f;
        if (l.role == R_RUN || l.role == R_BATS) {
            if (age < 2.5f && l.role == R_RUN) {
                float b = (float)l.vel * (1.0f / 127.0f) * fm_expf(-age * 1000.0f / (float)(l.tau ? l.tau : 500));
                int32_t y = ceil_y[l.x] + 10 + (l.x / 7) % 8;
                glow((float)l.x, (float)y, 11.0f, pc_col(l.pc), fm_minf(b * 2.2f, 0.9f));
                disc((float)l.x, (float)y, 1.2f, mix(K_BG, RGB(255, 250, 236), q16(b * 2.0f)));
            }
            continue;
        }
        if (l.kind == OB_STAL && l.obj < uw.nobj && age < 0.35f) {   /* a drop shatters on the stone */
            const en_obj_t *o = &uw.obj[l.obj];
            float ty = (float)land_y(o), k;
            for (k = -1.0f; k <= 1.0f; k += 1.0f) {
                float sx = (float)o->x + k * age * 34.0f, sy = ty - age * 46.0f + age * age * 260.0f;
                disc(sx, sy, 0.9f, mix(K_BG, RGB(200, 236, 246), q16(1.0f - age / 0.35f)));
            }
        }
    }
}

static void draw_river(uint32_t now)
{
    float fl = en_flood;
    int32_t h = RIVER_H + (int32_t)(fl * 46.0f), y0 = MAIN_H - h, y, k;
    float sp = 0.026f + 0.05f * fl;
    for (y = y0; y < MAIN_H; y++) {             /* water: see-through as it rises over the floor */
        int32_t x;
        uint16_t c = mix(RGB(22, 70, 92), RGB(12, 34, 52), (y - y0) * 16 / (h ? h : 1));
        if (y < FLOOR_Y + 10)
            for (x = 0; x < 240; x++)
                px_blend(x, y, c, 11u);
        else
            cv_rect(0, y, 240, 1, c);
    }
    for (k = 0; k < 14; k++) {                   /* the current's light */
        int32_t x = (int32_t)((uint32_t)(k * 61 + (int)((float)now * sp * (1.0f + 0.2f * (float)(k % 3)))) % 280u) - 20,
                y = y0 + 3 + (k * 5) % (h - 4 > 1 ? h - 4 : 1), w = 8 + (k * 7) % 14;
        cv_rect(x, y, w, 1, RGB(60, 130, 150));
    }
    cv_rect(0, y0, 240, 1, RGB(90, 170, 186));
}

static void draw_bats(uint32_t now)
{
    uint32_t age = now - en_bats_ms;
    int k;
    if (!en_bats_ms || age > 3400u)
        return;
    for (k = 0; k < 8; k++) {
        float p = (float)age / 3000.0f * (1.0f + 0.12f * (float)k) - 0.05f * (float)k, x, y, f;
        if (p < 0.0f || p > 1.1f)
            continue;
        x = en_bats_dir ? 250.0f - p * 270.0f + (float)(k * 9 % 23) : -10.0f + p * 270.0f - (float)(k * 9 % 23);
        y = 26.0f + (float)(k * 11 % 34) + 8.0f * fm_sinf(p * 9.0f + (float)k);
        f = (((age / 70u) + (uint32_t)k) & 1u) ? -3.0f : 2.0f;   /* the wings, up and down */
        rod(x - 5.0f, y + f, x, y, 0.6f, RGB(150, 140, 160));
        rod(x, y, x + 5.0f, y + f, 0.6f, RGB(150, 140, 160));
        disc(x, y + 0.5f, 1.1f, RGB(150, 140, 160));
    }
}

static void draw_stone(uint32_t now)
{
    uint32_t age = now - en_stone_ms;
    int k;
    float x = (float)en_stone_x, top = (float)FLOOR_Y - 12.0f;
    if (!en_stone_ms || age > 30000u)
        return;
    for (k = 0; k < uw.nobj; k++)               /* where it lands: the stalagmite's top */
        if (uw.obj[k].x == en_stone_x && uw.obj[k].kind == OB_STAL)
            top = (float)land_y(&uw.obj[k]);
    if (age < 500u) {                            /* it falls */
        float p = (float)age / 500.0f, y = (float)ceil_y[en_stone_x] + (top - (float)ceil_y[en_stone_x]) * p * p;
        disc(x - 1.0f, y - 3.0f, 2.6f, RGB(150, 138, 124));
        disc(x - 1.5f, y - 3.5f, 1.2f, RGB(200, 190, 172));
        return;
    }
    age -= 500u;
    disc(x + 9.0f, (float)FLOOR_Y - 2.5f, 3.0f, mix(RGB(150, 138, 124), RGB(60, 56, 60), (int)(age / 1800u)));
    if (age < 2200u) {                           /* the dust, and chips flying */
        float t = (float)age * 0.001f;
        for (k = 0; k < 8; k++) {
            float an = (float)k * 0.78f, r = 3.0f + t * 20.0f;
            glow(x + fm_cosf(an) * r, top - fm_sinf(an) * r * 0.5f - t * 6.0f, 6.0f + t * 5.0f, RGB(196, 186, 168),
                 0.8f * (1.0f - t / 2.2f));
        }
        for (k = 0; k < 5 && t < 0.6f; k++) {
            float vx = (float)(k - 2) * 26.0f, vy = -40.0f - (float)(k % 3) * 15.0f;
            disc(x + vx * t, top + vy * t + 220.0f * t * t, 0.9f, RGB(230, 220, 200));
        }
    }
}

static void draw_info(void)                      /* along the floor: the set, the river's note, the cave's age */
{
    char t[40], *e;
    int32_t w;
    e = cat_s(t, EN_NOTE_NAME[en_style_tonic(proj.style) % 12]);
    e = cat_s(e, " ");
    cat_s(e, en_style_scale(proj.style));
    cv_text(6, INFO_Y, &FONT_XS, t, K_DIM);
    e = cat_s(t, "wet ");
    e = cat_u(e, (uint32_t)(en_wet * 100.0f + 0.5f));
    e = cat_s(e, "%  ");
    cat_age(e, uw.minutes);
    w = text_w(&FONT_XS, t);
    cv_text(234 - w, INFO_Y, &FONT_XS, t, K_DIM);
}

static void draw_cave(void)
{
    uint32_t now = en_now_ms;
    int k;
    en_world_get(&uw);
    if (ceil_style != proj.style)
        ceil_make(proj.style);
    lights(now);
    draw_back(now);
    draw_info();
    for (k = 0; k < uw.nobj; k++)
        if (uw.obj[k].kind == OB_POOL)
            draw_pool(&uw.obj[k], k, now);
    for (k = 0; k < uw.nobj; k++)
        if (uw.obj[k].kind == OB_STAL)
            draw_stal(&uw.obj[k], k);
    draw_stone(now);
    draw_drips(now);
    draw_sparks(now);
    draw_river(now);
    draw_bats(now);
}

/* a button's card (help_tick): its name, then what it does. A line starting with a space goes on from the one
 * before (a screen reader joins them: sp_line) */
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("THE CAVE"); L("DRIPS, RING, RIVER,"); L(" AMBIENCE"); break;
    case B_ENV: L("THE SPACE"); L("CAVERN, ECHO, TONE, WIDTH"); break;
    case B_GLO: L("THE SETUP"); L("GROWTH, EVENTS,"); L(" PEBBLES, LIGHTS"); break;
    case B_EDIT: L("THE RIVER MOVES ON"); L("TO ITS NEXT HARMONY"); break;
    case B_OCTDN: L("THE KEYS AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE KEYS AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES EVERY CAVE"); L("KEEP HOLDING: THIS CAVE'S"); L(" KNOBS BACK TO ITS RECIPE"); break;
    case B_SEL: L("SELECT TURNS THE PAGES"); L("PRESETS: THE CAVE"); L("ALGORITHM: AMBIENCE"); L("KEYS: PEBBLES"); break;
    default: break;
    }
#undef L
    return n;
}

static void draw_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    cv_round(16, 10, 208, 124, 6, K_PANEL);
    cv_round(16, 10, 208, 26, 6, K_ACC);
    cv_rect(16, 30, 208, 6, K_ACC);
    text_c(120, 14, &FONT_B, BTN_LABEL[ui.help - 1], K_BG);
    for (k = 0; k < n; k++)
        text_c(120, 48 + k * 20, &FONT_S, l[k] + (l[k][0] == ' '), K_TEXT);
}

static void draw_main(void)
{
    uint32_t h;
    if (ui.frame % 3u)                           /* 20 frames a second */
        return;
    if (ui.help) {
        h = hash(0x48454C50u, ui.help);
        if (h != ui.sig[1]) {
            ui.sig[1] = h;
            cv_begin(240, MAIN_H, K_BG);
            draw_help();
            cv_blit(0, MAIN_Y);
        }
        return;
    }
    ui.sig[1] = 0;
    cv_begin(240, MAIN_H, K_BG);
    draw_cave();
    cv_blit(0, MAIN_Y);
}

/* the knob strip (X0X's): a rule with the page's name and its dots; a cell a knob: name, ring (or pips), value */
static void draw_knobs(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8), i;
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(VIEW_KNOB[ui.view][i]));
    h = hash(h, (uint32_t)now_touch);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    {
        char t[12];
        int k;
        int32_t xd = 237 - NVIEWS * 7, xt;
        caps(t, VIEW_NAME[ui.view], sizeof t);
        xt = xd - 6 - text_w(&FONT_XS, t);
        cv_rect(4, 5, xt - 9, 1, K_LINE);
        cv_text(xt, -1, &FONT_XS, t, K_ACC);
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 7, 3, 5, 5, 2, k == ui.view ? K_ACC : K_LINE);
    }
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v, hot = now_touch && ui.touched == (int)i;
        int32_t x = (int32_t)i * 60, cx = x + 30;
        char t[16];
        uint16_t bg = hot ? K_PANEL : K_BG, fill = hot ? K_ACC : mix(K_ACC, K_SOFT, 8);
        if (k == K_NONE)
            continue;
        if (i)
            cv_rect(x, 16, 1, 46, K_LINE);
        if (hot)
            cv_round(x + 3, 11, 54, 57, 6, K_PANEL);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        caps(t, KNOB_LABEL[ui.view][i], sizeof t);
        text_c(cx, 12, &FONT_XS, t, hot ? K_TEXT : K_DIM);
        if (hi - lo < 6) {
            int n = hi - lo + 1, j, pw = n > 4 ? 6 : 9, gap = 3, w0 = n * (pw + gap) - gap;
            for (j = 0; j < n; j++)
                cv_round(cx - w0 / 2 + j * (pw + gap), 33, pw, 8, 2, j == v - lo ? fill : K_LINE);
        } else {
            arc(cx, 37, 12, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, K_LINE, fill, bg);
        }
        knob_text(k, t);
        text_c(cx, 50, text_w(&FONT_B, t) > 56 ? &FONT_S : &FONT_B, t, hot ? K_ACC : K_TEXT);
    }
    cv_blit(0, KNB_Y);
}

/* the lights: the page's button, PLAY; the cave's notes on the keys (the set's pitch classes, nearest) */
static void leds(void)
{
    uint32_t b = 0, k = 0, i, pcs = 0;
    int d, n = en_style_nset(proj.style);
    for (d = 0; d < n; d++)
        pcs |= 1u << en_cents_pc(en_deg_cents(proj.style, d));
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (proj.leds && en_playing)
        for (i = 0; i < NKEYS; i++)
            if (pcs >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    plat_glow(proj.leds == 1);
    plat_leds(b, k | ui.keys);
}

/* AUTOSAVE (the flash wears, and a write can glitch the sound): the settings once the knobs have rested 10 s, at
 * most every 5 minutes; the world at most hourly. Both at a lull: no note for a moment and none about to land, or
 * resting. SAVE saves at once */
#define AUTOSAVE_REST 10000u
#define AUTOSAVE_SETTINGS 300000u
#define AUTOSAVE_WORLD 3600000u
static int lull(void)
{
    uint32_t now = en_now_ms;
    int i;
    if (!en_playing)
        return 1;
    if (now - en_last_strike_ms < 300u)
        return 0;
    for (i = 0; i < EN_NDRIP; i++)
        if ((en_drips_on >> i & 1u) && (int32_t)(en_drip_next[i] - now) < 400)
            return 0;
    return 1;
}
static void autosave(void)
{
    uint32_t now = plat_ms(), since = now - ui.saved_t;
    int want;
    if (ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_REST || !lull())
        return;
    want = (ui.dirty && since >= AUTOSAVE_SETTINGS) || (en_playing && since >= AUTOSAVE_WORLD);
    if (!want)
        return;
    if (project_save() == 0)
        ui.dirty = 0;
    ui.saved_t = now;                            /* (a failed save waits its turn too) */
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader (announce.c; fm1-reader): a page once it settles, with its knobs; a
 * knob as it turns; a help card as it comes up; the cave as it changes; playing and resting; what a button did
 * (say). WHERE is the whole screen in two: the page and the header, then the cave. */
static struct {
    uint8_t view, help, started;
    int16_t val[4];
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
    cat_u(t, v);
    sp_cat(t);
}
static void sp_knob(int view, int i)
{
    char t[16];
    sp_cat(KNOB_LABEL[view][i]);
    sp_cat(" ");
    knob_text(VIEW_KNOB[view][i], t);
    sp_cat(t);
}
static void sp_page(int all)
{
    int i;
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat(VIEW_NAME[ui.view]);
    sp_cat(" page");
    for (i = 0; i < 4; i++) {
        sp_cat(i ? ", " : ": ");
        sp_knob(ui.view, i);
    }
    if (!all)
        return;
    sp_cat(". Cave ");
    sp_cat(en_style_name(proj.style));
    sp_cat(sp_tp.playing ? ", playing" : ", resting");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static void sp_record(void)                      /* WHERE's second part: the cave, its set, its river, its age */
{
    char t[16];
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat(en_style_name(proj.style));
    sp_cat(", ");
    sp_cat(en_style_scale(proj.style));
    sp_cat(" on ");
    sp_cat(EN_NOTE_NAME[en_style_tonic(proj.style) % 12]);
    sp_cat(". River on ");
    sp_cat(EN_NOTE_NAME[en_harm_pc % 12]);
    sp_cat(". Wet ");
    sp_int((uint32_t)(en_wet * 100.0f + 0.5f));
    sp_cat(" percent. Cave time ");
    en_world_get(&uw);
    if (uw.minutes >= 60u) {
        sp_int(uw.minutes / 60u);
        sp_cat(uw.minutes / 60u == 1u ? " hour " : " hours ");
    }
    sp_int(uw.minutes % 60u);
    sp_cat(uw.minutes % 60u == 1u ? " minute" : " minutes");
    (void)t;
}
static void sp_line(const char *s)
{
    char p = sp_n ? sp_buf[sp_n - 1] : 0;
    if (*s == ' ')
        s++;
    else if (p != ',' && p != ':')
        sp_cat(".");
    sp_cat(" ");
    sp_cat(s);
}
static void sp_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat(BTN_LABEL[ui.help - 1]);
    sp_cat(" button");
    for (k = 0; k < n; k++)
        sp_line(l[k]);
}
static void sp_style_text(void)
{
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("CAVE ");
    sp_cat(en_style_name(proj.style));
}
static void sp_style(void)
{
    if (!ann_on())
        return;
    sp_style_text();
    ann_say(sp_buf, 0, ANN_NOW);
    sp_said = 1;
}
static void sp_snap(void)
{
    int i;
    for (i = 0; i < 4; i++)
        sp.val[i] = (int16_t)knob_get(VIEW_KNOB[ui.view][i]);
    sp.view = ui.view;
    sp.help = ui.help;
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
            sp_page(1);
        } else {
            sp_page(1);
            ann_say(sp_buf, 0, ANN_NOW);
            sp_record();
        }
        ann_say(sp_buf, 0, 0);
        sp_said = 0;
        return;
    }
    if (sp_said) {
        sp_said = 0;
        sp_snap();
        return;
    }
    if (en_playing != sp_tp.playing && now - sp_tp.play_t > 150u) {
        sp_tp.playing = en_playing;
        ann_say(en_playing ? "PLAYING" : "RESTING", 0, 0);
    }
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
        sp_n = 0;
        sp_knob(ui.view, changed);
        ann_later(sp_buf, ANN_NOW, 120u, now);
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card, cave and message as a screen reader gets them (the "speakdump" command) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"},
                                         {"THE RIVER", "MOVES ON"}, {"RESTING", 0}, {"PLAYING", 0}, {"SAVED", 0},
                                         {"KEYS OCTAVE", "-2"}, {"KEYS OCTAVE", "-1"}, {"KEYS OCTAVE", "0"},
                                         {"KEYS OCTAVE", "+1"}, {"KEYS OCTAVE", "+2"}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}, {"KEYS OCTAVE", "LOWEST"},
                                          {"KEYS OCTAVE", "HIGHEST"}};
    project_t keep = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty, keep_play = sp_tp.playing;
    int v, i, x, lo, hi;
    char t[64];
    for (v = 0; v < NVIEWS; v++) {
        ui.view = (uint8_t)v;
        sp_page(0);
        sp_print("SCREEN", sp_buf);
        for (i = 0; i < 4; i++) {
            int k = VIEW_KNOB[v][i];
            knob_range(k, &lo, &hi);
            for (x = lo; x <= hi; x++) {
                if (k < P_NPARAMS)
                    proj.par[k] = (int16_t)x;
                else
                    proj.leds = (uint8_t)x;
                sp_n = 0;
                sp_knob(v, i);
                sp_print("KNOB", sp_buf);
            }
            proj = keep;
        }
    }
    for (x = 0; x < NSTYLES * 2; x++) {          /* WHERE: every cave, playing, resting, unsaved */
        ui.view = (uint8_t)(x % NVIEWS);
        sp_tp.playing = (uint8_t)(x & 1);
        ui.dirty = (uint8_t)(x >> 1 & 1);
        proj.style = (uint8_t)(x % NSTYLES);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_record();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
    ui.dirty = keep_dirty;
    sp_tp.playing = keep_play;
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
    for (i = 0; i < (int)(sizeof WARN / sizeof WARN[0]); i++) {
        msg_text(WARN[i][0], WARN[i][1], t);
        sp_print("WARN", t);
    }
    for (x = 0; x <= 100; x += 10) {
        char o[12];
        en_param_text(P_AMB, x, o);
        msg_text("AMBIENCE", o, t);
        sp_print("MSG", t);
    }
    for (x = 0; x < NSTYLES; x++) {
        proj.style = (uint8_t)x;
        sp_style_text();
        sp_print("MSG", sp_buf);
        msg_text("RESET", en_style_name(x), t);
        sp_print("MSG", t);
    }
    proj = keep;
    for (x = 0; x <= 4; x++)
        sp_print("WHERE", batt_words(x));
}
#endif

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.saved_t = plat_ms();
    ui.act_t = plat_ms();
    lcd_fill(0, 0, 240, 240, K_BG);
    if (!en_playing)
        en_play(1);                              /* it plays from the start */
    sp_tp.playing = 1;
    sp_tp.play_t = plat_ms();
}

void ui_frame(void)
{
    input();
    autosave();
    draw_header();
    draw_main();
    draw_knobs();
    leds();
    speak();
    ann_tick(plat_ms());
    ui.frame++;
}

void ui_input_only(void) { input(); }
