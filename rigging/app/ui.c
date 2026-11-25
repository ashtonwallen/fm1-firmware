/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * RIGGING: ashtonwallen, 2026 */
/* RIGGING's UI: the panel and the screen (from AMB-1's by way of FELT PIANO: its knobs, help cards, messages and
 * screen reader). Part of the unity build (after gfx.c and project.c); the host simulator includes it too.
 *
 *   PLAY           playing / resting                 EDIT   come about: a new course now
 *   REC            HOLD: the helm keeps its course    OCT- / OCT+   the course 5 degrees to port / starboard
 *   PRESETS        the preset (each kept as left)    ALGORITHM     Ambience
 *   HOME ENV GLO   the pages: Sea, Rig, Setup (SELECT steps through them)
 *   KNOB 1-4       the page's four values
 *   keys           trim the course: left of the middle key to port, right to starboard, a degree a key
 *   SAVE           saves (it also saves by itself at a quiet moment: see autosave); held 1.5 s: the preset back
 *                  to its recipe */
#include "fastmath.h"

enum { V_SEA, V_RIG, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Sea", "Rig", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_GLO};
enum { K_LEDS = 100, K_HELM, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_WIND, P_GUSTS, P_SWELL, P_AMB},
    {P_BREATH, P_TONE, P_HULL, P_BELL},
    {P_ROOM, P_WIDTH, K_HELM, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Wind", "Gusts", "Swell", "Ambience"},
    {"Breath", "Tone", "Hull", "Bell"},
    {"Room", "Width", "Helm", "Lights"},
};

/* the screen's colours: a night ship's lantern on a dark sea: ink, sail-canvas cream, brass; a cold sea-green */
#define K_BG RGB(10, 14, 22)
#define K_PANEL RGB(28, 36, 50)
#define K_LINE RGB(46, 56, 74)
#define K_DIM RGB(120, 134, 156)
#define K_TEXT RGB(236, 230, 214)
#define K_ACC RGB(238, 186, 96)
#define K_SOFT RGB(120, 196, 206)
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
    uint32_t bell_no;                  /* the bells the screen has told */
    uint8_t squall;
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
static char *cat_deg(char *b, float d)           /* "045" */
{
    uint32_t v = (uint32_t)(d + 0.5f) % 360u;
    *b++ = (char)('0' + v / 100u);
    *b++ = (char)('0' + v / 10u % 10u);
    *b++ = (char)('0' + v % 10u);
    *b = 0;
    return b;
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
    if (k == K_HELM)
        return proj.hold;
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = en_param_info(k)->lo;
        *hi = en_param_info(k)->hi;
    } else {
        *lo = 0;
        *hi = k == K_LEDS ? 2 : k == K_HELM ? 1 : 0;
    }
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k < P_NPARAMS)
        en_param_text(k, v, b);
    else if (k == K_LEDS)
        cat_s(b, v == 2 ? "Keys" : v ? "On" : "Off");
    else if (k == K_HELM)
        cat_s(b, v ? "Hold" : "Free");
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
    } else if (k == K_HELM) {
        proj.hold = (uint8_t)v;
        en_hold(v);
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
static void set_style(int i)                    /* the preset playing kept as it is; preset i as it was left */
{
    project_style(i);
    project_apply();
    if (!en_playing)
        en_play(1);
    sp_tp.playing = 1;
    say_as("PRESET", en_style_name(proj.style), 0);
    sp_style();
    mark_dirty();
}

static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

static void trim(int deg)
{
    char t[12];
    en_trim(deg);
    cat_u(t, (uint32_t)(deg < 0 ? -deg : deg));
    say(deg < 0 ? "TO PORT" : "TO STARBOARD", t);
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
        en_about();
        say("COMING ABOUT", 0);
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
            say("PAUSED", 0);
        } else {
            en_play(1);
            say("PLAYING", 0);
        }
        break;
    case B_REC:
        knob_set(K_HELM, !proj.hold);
        say("HELM", proj.hold ? "HOLD" : "FREE");
        break;
    case B_OCTDN: trim(-5); break;
    case B_OCTUP: trim(5); break;
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

/* MIDI in (USB and the TRS jack): read and let go (the wind plays this one) */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0))
        ;
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

#define KEY_MID 13                               /* the middle key (D4): steady */
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
    for (i = 0; i < NKEYS; i++)                  /* a key: the course a degree a key from the middle */
        if ((ch >> i & 1u) && (keys >> i & 1u) && (int)i != KEY_MID)
            trim((int)i - KEY_MID);
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
        if (ui.view == V_SEA) {
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
        ui.reset_done = 1;                       /* held: the preset back to its recipe (not saved) */
        ui.btn_used |= 1u << B_SAVE;
        ui.help = 0;
        project_style_reset();
        project_apply();
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
/* three bands: the header (28 px), the scene (144), the knobs (68) */
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
static float fmodp(float x, float m) { return x - fm_floorf(x / m) * m; }   /* (x >= 0) */
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
static void disc(float cx, float cy, float r, uint16_t c, float alpha)
{
    int32_t x0 = (int32_t)fm_floorf(cx - r - 1.0f), x1 = (int32_t)(cx + r + 1.0f);
    int32_t y0 = (int32_t)fm_floorf(cy - r - 1.0f), y1 = (int32_t)(cy + r + 1.0f), x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy);
            px_blend(x, y, c, (uint32_t)(fm_clampf(r - d + 0.5f, 0.0f, 1.0f) * alpha * 16.0f + 0.5f));
        }
}

/* a line a pixel wide, anti-aliased (Wu's), at alpha 0..1 */
static void wline(float x0, float y0, float x1, float y1, uint16_t c, float alpha)
{
    float dx = x1 - x0, dy = y1 - y0, t;
    int steep = fm_fabsf(dy) > fm_fabsf(dx), i, n;
    if (steep) {
        t = x0, x0 = y0, y0 = t;
        t = x1, x1 = y1, y1 = t;
        t = dx, dx = dy, dy = t;
    }
    if (x0 > x1) {
        t = x0, x0 = x1, x1 = t;
        t = y0, y0 = y1, y1 = t;
        dx = -dx, dy = -dy;
    }
    n = (int)(x1 - x0 + 0.5f);
    for (i = 0; i <= n; i++) {
        float x = x0 + (float)i, y = y0 + (dx != 0.0f ? dy / dx : 0.0f) * (float)i, fy = fm_floorf(y), f = y - fy;
        int32_t xi = (int32_t)x, yi = (int32_t)fy;
        uint32_t a0 = (uint32_t)((1.0f - f) * alpha * 16.0f + 0.5f), a1 = (uint32_t)(f * alpha * 16.0f + 0.5f);
        if (steep) {
            px_blend(yi, xi, c, a0);
            px_blend(yi + 1, xi, c, a1);
        } else {
            px_blend(xi, yi, c, a0);
            px_blend(xi, yi + 1, c, a1);
        }
    }
}

/* a filled convex polygon (scanlines; edges blended a little) */
static void poly(const float *xs, const float *ys, int n, uint16_t c)
{
    float ymin = 1e9f, ymax = -1e9f;
    int32_t y, i;
    for (i = 0; i < n; i++) {
        ymin = fm_minf(ymin, ys[i]);
        ymax = fm_maxf(ymax, ys[i]);
    }
    for (y = (int32_t)fm_floorf(ymin); y <= (int32_t)ymax; y++) {
        float yc = (float)y + 0.5f, xa = 1e9f, xb = -1e9f;
        for (i = 0; i < n; i++) {
            int j = (i + 1) % n;
            float ya = ys[i], yb = ys[j];
            if ((yc >= ya && yc < yb) || (yc >= yb && yc < ya)) {
                float x = xs[i] + (yc - ya) * (xs[j] - xs[i]) / (yb - ya);
                xa = fm_minf(xa, x);
                xb = fm_maxf(xb, x);
            }
        }
        if (xb > xa) {
            int32_t a = (int32_t)(xa + 0.5f), b = (int32_t)(xb + 0.5f);
            cv_rect(a, y, b - a, 1, c);
            px_blend(a - 1, y, c, (uint32_t)(((fm_floorf(xa) + 1.0f) - xa) * 8.0f));
        }
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

/* the header: the preset in a tag; on the right the battery, unsaved, the wind's force, HOLD */
static void draw_header(void)
{
    char t[16];
    int msg = plat_ms() < ui.msg_until, force = en_beaufort(en_wind);
    uint32_t h = hash(hash(hash(2166136261u, proj.style | (uint32_t)force << 8 | (uint32_t)en_playing << 16 |
                                                   (uint32_t)en_hold_on << 17 | (uint32_t)ui.dirty << 18 |
                                                   (uint32_t)msg << 19 | (uint32_t)en_squall << 20),
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
        t[0] = 'F';
        cat_u(t + 1, (uint32_t)force);
        x -= text_w(&FONT_S, t);
        cv_text(x, 6, &FONT_S, t, en_squall ? K_ACC : en_playing ? K_TEXT : K_DIM);
        if (!en_playing) {                       /* resting: two bars */
            x -= 14;
            cv_rect(x, 9, 3, 10, K_DIM);
            cv_rect(x + 5, 9, 3, 10, K_DIM);
        }
        if (en_hold_on) {
            x -= text_w(&FONT_XS, "HOLD") + 14;
            cv_round(x, 7, text_w(&FONT_XS, "HOLD") + 8, 15, 5, K_SOFT);
            cv_text(x + 4, 7, &FONT_XS, "HOLD", K_BG);
        }
        w = text_w(&FONT_B, en_style_name(proj.style)) + 12;
        if (4 + w < x - 4) {
            cv_round(4, 4, w, 20, 6, K_ACC);
            cv_text(10, 6, &FONT_B, en_style_name(proj.style), K_BG);
        }
    }
    cv_blit(0, 0);
}

/* ---- the scene: the ship from the side (her bow to the right), sails full or flapping, every rope lit as it
 * sounds; the sky and the swell by the preset and the weather; the wind's streaks; a compass with her heading and
 * the wind */
typedef struct {
    uint16_t top, horizon, sea, deep;
} sky_t;
static const sky_t SKY[] = {
    {RGB(44, 84, 140), RGB(150, 178, 196), RGB(34, 70, 96), RGB(8, 22, 38)},     /* trade-wind blue */
    {RGB(120, 150, 188), RGB(236, 214, 182), RGB(70, 112, 130), RGB(20, 44, 60)},   /* a pale morning */
    {RGB(30, 40, 54), RGB(88, 100, 110), RGB(28, 40, 48), RGB(6, 12, 18)},       /* slate, a gale */
    {RGB(206, 150, 92), RGB(242, 214, 150), RGB(118, 120, 104), RGB(36, 44, 50)},   /* a glassy, hot calm */
    {RGB(14, 16, 40), RGB(70, 52, 98), RGB(20, 24, 46), RGB(4, 6, 16)},          /* a thundery night */
    {RGB(60, 52, 96), RGB(214, 140, 120), RGB(52, 56, 88), RGB(14, 16, 32)},      /* evening violet */
    {RGB(84, 96, 112), RGB(176, 172, 160), RGB(52, 66, 74), RGB(14, 22, 28)},     /* a merchant's grey */
    {RGB(150, 182, 204), RGB(226, 236, 240), RGB(66, 100, 118), RGB(16, 34, 48)},   /* polar, pale */
    {RGB(40, 62, 82), RGB(140, 160, 170), RGB(30, 60, 74), RGB(6, 18, 28)},       /* spray and cold blue */
    {RGB(86, 140, 170), RGB(218, 226, 196), RGB(62, 118, 110), RGB(14, 40, 44)},   /* a weed-green slack */
    {RGB(30, 90, 170), RGB(170, 210, 230), RGB(24, 84, 130), RGB(4, 26, 52)},     /* a bright run */
    {RGB(20, 26, 34), RGB(64, 72, 80), RGB(22, 30, 36), RGB(4, 8, 12)},          /* hove to, the dark */
};
#define HORIZON 92
#define NROPES EN_NR

/* the ship in her own frame: x aft- / fore+, y up- (from the waterline); the rig's points */
static const int8_t ROPE_P[NROPES][4] = {
    {92, -26, 34, -86},  {60, -14, 34, -70},  {38, -13, 34, -50},  {22, -13, 34, -86},
    {28, -13, -4, -72},  {2, -13, -4, -54},   {-4, -94, 34, -66},  {-4, -88, -30, -13},
    {-40, -74, -8, -60}, {-36, -13, -40, -46}, {-40, -70, -62, -14}, {-40, -60, -72, -24},
};
static float sh_c, sh_s, sh_x, sh_y;              /* the ship's pose: rotation, where her waterline's middle is */
static void ship_pt(float x, float y, float *ox, float *oy)
{
    *ox = sh_x + x * sh_c - y * sh_s;
    *oy = sh_y + x * sh_s + y * sh_c;
}
static void ship_poly(const int8_t *p, int n, uint16_t c)
{
    float xs[8], ys[8];
    int i;
    for (i = 0; i < n; i++)
        ship_pt((float)p[2 * i], (float)p[2 * i + 1], &xs[i], &ys[i]);
    poly(xs, ys, n, c);
}
static void ship_line(float x0, float y0, float x1, float y1, uint16_t c, float a)
{
    float ax, ay, bx, by;
    ship_pt(x0, y0, &ax, &ay);
    ship_pt(x1, y1, &bx, &by);
    wline(ax, ay, bx, by, c, a);
}

/* a square sail between two yards (its foot bellied out by the wind, or shaking in a lull) */
static void sail(float mx, float ytop, float ybot, float wtop, float wbot, uint16_t c, float fill, float flap, float t)
{
    float xs[6], ys[6], b = 2.5f * fill, sh = flap * 2.2f * fm_sin_turns(t * 3.1f + mx * 0.05f);
    ship_pt(mx - wtop * 0.5f, ytop, &xs[0], &ys[0]);
    ship_pt(mx + wtop * 0.5f, ytop, &xs[1], &ys[1]);
    ship_pt(mx + wbot * 0.5f + b * 0.5f + sh, ybot, &xs[2], &ys[2]);
    ship_pt(mx + sh * 0.5f, ybot + b, &xs[3], &ys[3]);
    ship_pt(mx - wbot * 0.5f - b * 0.5f + sh, ybot, &xs[4], &ys[4]);
    poly(xs, ys, 5, c);
}
static void tri(float ax, float ay, float bx, float by, float cx, float cy, uint16_t c)
{
    float xs[3], ys[3];
    ship_pt(ax, ay, &xs[0], &ys[0]);
    ship_pt(bx, by, &xs[1], &ys[1]);
    ship_pt(cx, cy, &xs[2], &ys[2]);
    poly(xs, ys, 3, c);
}

static uint16_t rope_col(float a, float h)        /* dark tarred line -> lantern gold; whistling: pale */
{
    uint16_t base = RGB(84, 74, 62), lit = mix(RGB(255, 204, 96), RGB(225, 248, 255), q16(h));
    return mix(base, lit, q16(fm_sqrtf(a) * 1.6f));
}

static void draw_scene(void)
{
    const sky_t *sk = &SKY[proj.style % (sizeof SKY / sizeof SKY[0])];
    float t = (float)en_now_ms * 0.001f, sw = en_swell, ph = en_swell_ph, storm = en_squall ? 1.0f : 0.0f;
    float wind = en_wind, rel = en_rel, ar = fm_fabsf(rel);
    uint16_t top = mix(sk->top, RGB(18, 20, 26), (int)(storm * 9.0f)), hor = mix(sk->horizon, RGB(60, 66, 74), (int)(storm * 9.0f));
    int32_t y, i;
    /* the sky, the sea */
    for (y = 0; y < HORIZON; y++)
        cv_rect(0, y, 240, 1, mix(top, hor, y * 16 / HORIZON));
    for (y = HORIZON; y < MAIN_H; y++)
        cv_rect(0, y, 240, 1, mix(sk->sea, sk->deep, (y - HORIZON) * 16 / (MAIN_H - HORIZON)));
    /* the swell's long lines, moving towards us and past */
    for (i = 0; i < 7; i++) {
        float yy = (float)HORIZON + 3.0f + (float)(i * i) * 1.0f + (float)i * 2.0f;
        int32_t x;
        uint16_t c = mix(sk->sea, sk->horizon, 3 + i / 2);
        float amp = (0.6f + 2.6f * sw) * (0.3f + 0.12f * (float)i), k = 0.025f + 0.004f * (float)(6 - i);
        for (x = 0; x < 240; x += 2) {
            float yv = yy + amp * fm_sin_turns((float)x * k * 0.16f - ph - (float)i * 0.13f);
            if (yv > HORIZON && yv < MAIN_H - 1)
                cv_rect(x, (int32_t)yv, 2, 1, c);
        }
    }
    /* the wind's streaks: from the bow aft with the wind ahead, from astern forward with it behind */
    if (wind > 0.06f) {
        float dir = ar < 90.0f ? -1.0f : 1.0f, sp = 20.0f + 120.0f * wind, len = 6.0f + 22.0f * wind;
        int n = 3 + (int)(wind * 9.0f);
        for (i = 0; i < n; i++) {
            uint32_t hs = hash(2166136261u, (uint32_t)i * 7919u);
            float yy = 6.0f + (float)(hs % 70u), x0 = fmodp(((float)(hs >> 8 & 255u) + t * sp * (0.7f + 0.3f * (float)(hs >> 16 & 7u) / 7.0f)), 280.0f);
            float xx = dir > 0.0f ? x0 - 20.0f : 260.0f - x0;
            wline(xx, yy, xx - dir * len, yy + 0.6f, RGB(250, 252, 255), 0.75f);
            wline(xx, yy + 1.0f, xx - dir * len * 0.7f, yy + 1.4f, RGB(250, 252, 255), 0.35f);
        }
    }
    /* rain in a squall */
    if (en_squall)
        for (i = 0; i < 40; i++) {
            uint32_t hs = hash(0x5eedu, (uint32_t)i * 2654435761u);
            float x = (float)(hs % 240u), yy = fmodp((float)(hs >> 9 & 255u) + t * 160.0f, 150.0f) - 6.0f;
            wline(x, yy, x - 2.0f, yy + 6.0f, RGB(150, 160, 176), 0.5f);
        }
    /* the ship's pose on the swell */
    {
        float pitch = sw * 0.07f * fm_sin_turns(ph + 0.25f), heave = sw * 3.0f * fm_sin_turns(ph);
        sh_c = fm_cosf(pitch);
        sh_s = fm_sinf(pitch);
        sh_x = 110.0f;
        sh_y = 112.0f - heave;
    }
    /* the sails: full by the wind, flapping in a lull or turning through it */
    {
        float fill = en_fill, flap = en_luff + fm_clampf((0.2f - wind) * 4.0f, 0.0f, 1.0f) * 0.6f;
        uint16_t canvas = mix(RGB(120, 112, 98), RGB(238, 228, 204), q16(0.45f + 0.55f * fill));
        uint16_t canvas2 = mix(canvas, RGB(0, 0, 0), 3);
        tri(92, -26, 36, -84, 52, -18, canvas2);                    /* the jibs */
        tri(70, -18, 35, -66, 50, -16, canvas);
        sail(34, -82, -68, 18, 24, canvas2, fill, flap, t);         /* the fore: topgallant, topsail, course */
        sail(34, -66, -48, 24, 32, canvas, fill, flap, t);
        sail(34, -46, -24, 32, 40, canvas2, fill, flap, t);
        sail(-4, -90, -74, 20, 26, canvas2, fill, flap, t);         /* the main */
        sail(-4, -72, -52, 26, 36, canvas, fill, flap, t);
        sail(-4, -50, -26, 36, 44, canvas2, fill, flap, t);
        {                                                            /* the spanker */
            static const int8_t SP[8] = {-41, -60, -41, -18, -72, -24, -62, -52};
            ship_poly(SP, 4, canvas);
        }
    }
    /* the masts, the yards */
    {
        static const int8_t MAST[3][2] = {{34, -88}, {-4, -96}, {-40, -76}};
        for (i = 0; i < 3; i++) {
            ship_line((float)MAST[i][0], -12.0f, (float)MAST[i][0], (float)MAST[i][1], RGB(70, 54, 40), 1.0f);
            ship_line((float)MAST[i][0] + 1.0f, -12.0f, (float)MAST[i][0] + 1.0f, (float)MAST[i][1], RGB(70, 54, 40), 0.8f);
        }
        ship_line(60, -14, 94, -27, RGB(70, 54, 40), 1.0f);         /* the bowsprit */
    }
    /* the ropes: every one lit as it sounds (brighter, paler as it whistles) */
    for (i = 0; i < NROPES; i++) {
        float a = en_rope_amp[i], h = en_rope_harm[i];
        const int8_t *p = ROPE_P[i];
        uint16_t c = rope_col(a, h);
        if (a > 0.04f) {                         /* a glow about it, wider as it sounds */
            float ax, ay, bx, by, g = fm_minf(a * 1.4f, 1.0f);
            ship_pt(p[0], p[1], &ax, &ay);
            ship_pt(p[2], p[3], &bx, &by);
            wline(ax + 2.0f, ay, bx + 2.0f, by, c, g * 0.25f);
            wline(ax - 2.0f, ay, bx - 2.0f, by, c, g * 0.25f);
            wline(ax + 1.0f, ay, bx + 1.0f, by, c, g * 0.75f);
            wline(ax - 1.0f, ay, bx - 1.0f, by, c, g * 0.75f);
        }
        ship_line(p[0], p[1], p[2], p[3], c, 1.0f);
        if (i == 2 || i == 5 || i == 9)          /* the shrouds: a fan */
            ship_line(p[0] - 5.0f, p[1], p[2], p[3], c, 0.85f);
    }
    /* the hull: dark wood, a pale band, the sea at her side */
    {
        static const int8_t HULL[12] = {-62, -14, 60, -16, 72, -10, 56, 6, -50, 6, -66, -4};
        static const int8_t BAND[8] = {-62, -9, 64, -11, 66, -8, -64, -6};
        ship_poly(HULL, 6, RGB(40, 30, 24));
        ship_poly(BAND, 4, mix(RGB(40, 30, 24), RGB(210, 190, 150), 9));
        for (i = 0; i < 8; i++) {                /* the ports */
            float px, py;
            ship_pt(-46.0f + (float)i * 13.0f, -7.5f, &px, &py);
            cv_rect((int32_t)px, (int32_t)py, 3, 2, RGB(30, 22, 18));
        }
        {                                        /* her stern lantern */
            float lx, ly;
            ship_pt(-64.0f, -18.0f, &lx, &ly);
            disc(lx, ly, 2.0f, K_ACC, 1.0f);
            disc(lx, ly, 4.5f, K_ACC, 0.25f);
        }
    }
    /* the water along her: a band of the sea over her lower hull, foam at the bow by her way */
    for (y = (int32_t)sh_y - 1; y < MAIN_H; y++) {
        uint16_t c = mix(sk->sea, sk->deep, (y - HORIZON) * 16 / (MAIN_H - HORIZON));
        cv_rect(30, y, 160, 1, c);
        if (y > (int32_t)sh_y + 3)
            break;
    }
    if (wind > 0.1f) {
        float fx, fy;
        ship_pt(66.0f, 0.0f, &fx, &fy);
        for (i = 0; i < 6; i++)
            disc(fx + (float)i * 4.0f - 2.0f * fm_sin_turns(t * 0.7f + (float)i * 0.3f), fy + 1.0f, 1.6f,
                 RGB(230, 240, 240), fm_minf(wind, 0.8f) * (1.0f - (float)i * 0.15f));
    }
    /* in a squall: a pale fire at the mastheads */
    if (en_squall) {
        static const int8_t TOP[3][2] = {{34, -88}, {-4, -96}, {-40, -76}};
        for (i = 0; i < 3; i++) {
            float mx, my, f = 0.6f + 0.4f * fm_sin_turns(t * 2.3f + (float)i * 0.37f);
            ship_pt(TOP[i][0], TOP[i][1], &mx, &my);
            disc(mx, my, 2.0f, RGB(200, 236, 255), f);
            disc(mx, my, 5.0f, RGB(140, 200, 255), f * 0.3f);
        }
    }
    /* the compass: north up, her heading (the needle), the wind's arrow coming in */
    {
        float cx = 214.0f, cy = 26.0f, r = 17.0f, hd = en_heading * (FM_PI / 180.0f), wdr = en_wind_dir * (FM_PI / 180.0f);
        disc(cx, cy, r + 1.0f, RGB(8, 12, 20), 0.55f);
        for (i = 0; i < 16; i++) {
            float a = (float)i * (FM_TWO_PI / 16.0f), r0 = (i & 3) ? r - 2.5f : r - 5.0f;
            wline(cx + fm_sinf(a) * r0, cy - fm_cosf(a) * r0, cx + fm_sinf(a) * r, cy - fm_cosf(a) * r,
                  (i & 3) ? K_DIM : K_TEXT, 0.8f);
        }
        cv_text((int32_t)cx - 3, (int32_t)(cy - r) - 13, &FONT_XS, "N", K_TEXT);
        wline(cx - fm_sinf(hd) * 6.0f, cy + fm_cosf(hd) * 6.0f, cx + fm_sinf(hd) * (r - 3.0f), cy - fm_cosf(hd) * (r - 3.0f), K_ACC, 1.0f);
        disc(cx + fm_sinf(hd) * (r - 4.0f), cy - fm_cosf(hd) * (r - 4.0f), 1.8f, K_ACC, 1.0f);
        if (wind > 0.04f) {                      /* the wind: from outside the ring, in */
            float ox = cx + fm_sinf(wdr) * (r + 9.0f), oy = cy - fm_cosf(wdr) * (r + 9.0f);
            float ix = cx + fm_sinf(wdr) * (r - 7.0f), iy = cy - fm_cosf(wdr) * (r - 7.0f);
            wline(ox, oy, ix, iy, K_SOFT, 1.0f);
            disc(ix, iy, 1.8f, K_SOFT, 1.0f);
        }
    }
    /* the words: the chord, the key and point of sail; the heading */
    {
        char tx[40], *e;
        uint32_t lum = ((uint32_t)(top >> 11) * 16u + (uint32_t)((top >> 5) & 63u) * 20u + (uint32_t)(top & 31u) * 8u) / 8u;
        uint16_t ink = lum > 130u ? RGB(18, 22, 32) : K_TEXT, ink2 = lum > 130u ? RGB(40, 46, 60) : mix(K_TEXT, top, 4);
        en_chord_text(tx);
        cv_text(8, 2, &FONT_B, tx, ink);
        e = cat_s(tx, EN_NOTE_NAME[en_key % 12]);
        e = cat_s(e, " ");
        e = cat_s(e, en_style_mode(proj.style));
        cv_text(8, 24, &FONT_XS, tx, ink2);
        e = cat_s(tx, en_sector_name(en_sector));
        if (en_calm)
            cat_s(e, ", becalmed");
        else if (en_squall)
            cat_s(e, ", squall");
        else if (ar < 40.0f)
            cat_s(e, ", in stays");
        (void)e;
        cv_text(8, 38, &FONT_XS, tx, ink2);
        e = cat_deg(tx, en_heading);
        cv_text(203, 48, &FONT_XS, tx, ink2);
    }
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
    case B_HOME: L("THE SEA"); L("WIND, GUSTS, SWELL,"); L(" AMBIENCE"); break;
    case B_ENV: L("THE RIG"); L("BREATH, TONE, HULL, BELL"); break;
    case B_GLO: L("THE SETUP"); L("ROOM, WIDTH, HELM, LIGHTS"); break;
    case B_EDIT: L("COME ABOUT:"); L("A NEW COURSE NOW"); break;
    case B_REC: L("HOLD THE HELM:"); L(" NO COURSE CHANGES"); L("AGAIN: THE HELM FREE"); break;
    case B_OCTDN: L("FIVE DEGREES TO PORT"); L("THE KEYS TRIM TOO"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("FIVE DEGREES TO STARBOARD"); L("THE KEYS TRIM TOO"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES THE VOYAGE"); L("KEEP HOLDING: THE PRESET"); L(" BACK TO ITS RECIPE"); break;
    case B_SEL: L("SELECT TURNS THE PAGES"); L("PRESETS: THE PRESET"); L("ALGORITHM: AMBIENCE"); break;
    default: break;
    }
#undef L
    return n;
}

static void draw_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    cv_round(16, 14, 208, 116, 6, K_PANEL);
    cv_round(16, 14, 208, 26, 6, K_ACC);
    cv_rect(16, 34, 208, 6, K_ACC);
    text_c(120, 18, &FONT_B, BTN_LABEL[ui.help - 1], K_BG);
    for (k = 0; k < n; k++)
        text_c(120, 56 + k * 20, &FONT_S, l[k] + (l[k][0] == ' '), K_TEXT);
}

static void draw_main(void)
{
    uint32_t h;
    int i;
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
    h = hash(2166136261u, en_now_ms / 50u);
    for (i = 0; i < NROPES; i++)
        h = hash(h, (uint32_t)(en_rope_amp[i] * 64.0f));
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    draw_scene();
    cv_blit(0, MAIN_Y);
}

/* the knob strip (X0X's): a rule with the page's name and its dots; a cell a knob: name, ring (or pips), value */
static void arc(int32_t cx, int32_t cy, int32_t r, float v, uint16_t track, uint16_t fill, uint16_t bg);
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
        disc(fx + fm_cosf(e0) * rm - 0.5f, fy + fm_sinf(e0) * rm - 0.5f, th * 0.5f, v > 0.0f ? fill : track, 1.0f);
        disc(fx + fm_cosf(e1) * rm - 0.5f, fy + fm_sinf(e1) * rm - 0.5f, th * 0.5f, v >= 1.0f ? fill : track, 1.0f);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th * 0.5f + 1.3f, bg, 1.0f);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th * 0.5f + 0.6f, fill, 1.0f);
    }
}

/* the lights: the page's button, PLAY, HOLD; the ropes sounding on the keys (their notes, F3 .. G5) */
static void leds(void)
{
    uint32_t b = 0, k = 0, i;
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (en_hold_on)
        b |= 1u << B_REC;
    if (proj.leds && en_playing)
        for (i = 0; i < NROPES; i++)
            if (en_rope_amp[i] > 0.2f) {
                int n = en_rope_note[i];
                while (n < 53)
                    n += 12;
                while (n > 79)
                    n -= 12;
                k |= 1u << (n - 53);
            }
    plat_glow(proj.leds == 1);
    plat_leds(b, k);
}

/* AUTOSAVE (the flash wears, and a write can glitch the sound): the settings once the knobs have rested 10 s, at
 * most every 5 minutes; the voyage (the course, the wind, the watch) at most hourly. At a quiet moment: resting,
 * or two ropes or fewer sounding and no squall (after a long wait, four will do). SAVE saves at once */
#define AUTOSAVE_REST 10000u
#define AUTOSAVE_SETTINGS 300000u
#define AUTOSAVE_WORLD 3600000u
static void autosave(void)
{
    uint32_t now = plat_ms(), since = now - ui.saved_t;
    uint32_t due = ui.dirty ? AUTOSAVE_SETTINGS : AUTOSAVE_WORLD;
    int want = (ui.dirty && since >= AUTOSAVE_SETTINGS) || (en_playing && since >= AUTOSAVE_WORLD);
    /* quiet: few ropes sounding, no squall. The longer a save is overdue, the more ropes may sound (one more a
     * minute), so a busy sea still saves within minutes */
    uint32_t over = since > due ? (since - due) / 60000u : 0;
    int quiet = !en_playing || (!en_squall && en_sounding <= 2 + (int)(over > 12 ? 12 : over));
    if (!want || !quiet || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_REST)
        return;
    if (project_save() == 0)
        ui.dirty = 0;
    ui.saved_t = now;                            /* (a failed save waits its turn too) */
}

/* the bells and the weather, told on the header (and to a screen reader, queued) */
static const char *const BELL_WORD[9] = {"", "ONE BELL", "TWO BELLS", "THREE BELLS", "FOUR BELLS", "FIVE BELLS",
                                         "SIX BELLS", "SEVEN BELLS", "EIGHT BELLS"};
static void tell(const char *a)                 /* on the header; queued for a screen reader (not cut in) */
{
    say_as(a, 0, 0);
    if (ann_on())
        ann_say(a, 0, 0);
}
static void events(void)
{
    if (en_bell_no != ui.bell_no) {
        ui.bell_no = en_bell_no;
        if (en_bells >= 1 && en_bells <= 8)
            tell(BELL_WORD[en_bells]);
    }
    if (en_squall != ui.squall) {
        ui.squall = en_squall;
        if (en_squall)
            tell("SQUALL");
    }
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader (announce.c; fm1-reader): a page once it settles, with its knobs; a
 * knob as it turns; a help card as it comes up; each new chord with the point of sail; playing and paused; what a
 * button did (say). WHERE is the whole screen in two: the page and the header, then the ship. */
static struct {
    uint8_t view, help, started, sector;
    int16_t val[4];
    char chord[16];
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
    sp_cat(". Preset ");
    sp_cat(en_style_name(proj.style));
    sp_cat(sp_tp.playing ? ", playing" : ", paused");
    if (en_hold_on)
        sp_cat(", helm held");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
/* a chord in words: "A minor eleven", "C four to five to six" (the screen's "Am11", "C 4:5:6") */
static void sp_chord_words(void)
{
    static const char *const SYM[][2] = {
        {"maj7#11", "major seven sharp eleven"}, {"m7sus4", "minor seven sus four"}, {"9sus4", "nine sus four"},
        {"6sus4", "six sus four"}, {"maj9", "major nine"}, {"add9", "add nine"}, {"5ths", "in fifths"},
        {"6/9", "six nine"}, {"m11", "minor eleven"}, {"m9", "minor nine"}, {"m7", "minor seven"}, {"9", "nine"}};
    char t[16], w[4];
    int i, k;
    en_chord_text(t);
    k = (t[1] == '#' || t[1] == 'b') ? 2 : 1;
    for (i = 0; i < k && i < 3; i++)
        w[i] = t[i];
    w[i] = 0;
    sp_cat(w);                                   /* the root ("Eb": the words file says it) */
    if (t[k] == ' ') {                           /* just ratios: "4:5:6" */
        sp_cat(" harmonics ");
        for (i = k + 1; t[i]; i++) {
            char d[2] = {t[i] == ':' ? ' ' : t[i], 0};
            sp_cat(d);
        }
        return;
    }
    for (i = 0; i < (int)(sizeof SYM / sizeof SYM[0]); i++) {
        const char *s = SYM[i][0];
        int j;
        for (j = 0; s[j] && s[j] == t[k + j]; j++)
            ;
        if (!s[j] && !t[k + j]) {
            sp_cat(" ");
            sp_cat(SYM[i][1]);
            return;
        }
    }
    sp_cat(t + k);
}
static void sp_chord_text(void)                  /* "A minor eleven in D Dorian, beam reach" */
{
    sp_n = 0;
    sp_buf[0] = 0;
    sp_chord_words();
    sp_cat(" in ");
    sp_cat(EN_NOTE_NAME[en_key % 12]);
    sp_cat(" ");
    sp_cat(en_style_mode(proj.style));
    sp_cat(", ");
    sp_cat(en_sector_name(en_sector));
}
static void sp_record(void)                      /* WHERE's second part: the ship */
{
    char t[8];
    sp_chord_text();
    sp_cat(". Heading ");
    cat_deg(t, en_heading);
    sp_cat(t);
    sp_cat(", wind force ");
    sp_int((uint32_t)en_beaufort(en_wind));
    if (en_squall)
        sp_cat(", a squall");
    if (en_calm)
        sp_cat(", becalmed");
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
    sp_cat("PRESET ");
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
    sp.sector = en_sector;
    en_chord_text(sp.chord);
}

static void speak(void)
{
    uint32_t now = plat_ms();
    int where = ann_where(), i, changed = -1;
    char ch[16];
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
        ann_say(en_playing ? "PLAYING" : "PAUSED", 0, 0);
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
    en_chord_text(ch);
    if (memcmp(ch, sp.chord, sizeof ch) != 0) {  /* a new chord: queued */
        memcpy(sp.chord, ch, sizeof ch);
        sp_chord_text();
        ann_say(sp_buf, 0, 0);
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card and message as a screen reader gets them (the "speakdump" command) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}, {"COMING ABOUT", 0},
                                         {"PAUSED", 0}, {"PLAYING", 0}, {"HELM", "HOLD"}, {"HELM", "FREE"},
                                         {"SAVED", 0}, {"SQUALL", 0}, {"TO PORT", "5"}, {"TO STARBOARD", "5"},
                                         {"TO PORT", "13"}, {"TO STARBOARD", "1"}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}};
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
                else if (k == K_LEDS)
                    proj.leds = (uint8_t)x;
                else
                    proj.hold = (uint8_t)x;
                sp_n = 0;
                sp_knob(v, i);
                sp_print("KNOB", sp_buf);
            }
            proj = keep;
        }
    }
    for (x = 0; x < NSTYLES; x++) {              /* WHERE: every preset's page and its ship */
        ui.view = (uint8_t)(x % NVIEWS);
        sp_tp.playing = (uint8_t)(x & 1);
        ui.dirty = (uint8_t)(x >> 1 & 1);
        proj.style = (uint8_t)x;
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_record();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
    ui.dirty = keep_dirty;
    sp_tp.playing = keep_play;
    ui.view = keep_view;
    for (x = 0; x < EN_NSECT; x++) {
        sp_n = 0;
        sp_cat(en_sector_name(x));
        sp_print("WHERE", sp_buf);
    }
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
    for (i = 1; i <= 8; i++)
        sp_print("MSG", BELL_WORD[i]);
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
    ui.bell_no = en_bell_no;
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
    events();
    draw_header();
    draw_main();
    draw_knobs();
    leds();
    speak();
    ann_tick(plat_ms());
    ui.frame++;
}

void ui_input_only(void) { input(); }
