/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * KITE HILL: ashtonwallen, 2026 */
/* KITE HILL's UI: the panel and the screen (from AMB-1's by way of FELT PIANO: its knobs, help cards, messages and
 * screen reader). Part of the unity build (after gfx.c and project.c); the host simulator includes it too.
 *
 *   PLAY           playing / resting                 EDIT   on to the next part of the day
 *   REC            HOLD: the day stands still        OCT- / OCT+   the keys' octave
 *   PRESETS        the hill (each kept as left)      ALGORITHM     Ambience (the wind bed)
 *   HOME ENV GLO   the pages: Hill, Line, Setup (SELECT steps through them)
 *   KNOB 1-4       the page's four values
 *   keys           a kite goes up on the nearest note that fits
 *   SAVE           saves (it also saves by itself at a quiet moment: see autosave); held 1.5 s: the hill back to
 *                  its recipe */
#include "fastmath.h"

enum { V_HILL, V_LINE, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Hill", "Line", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_GLO};
enum { K_LEDS = 100, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_WIND, P_KITES, P_AMBIENCE, P_SPACE},
    {P_TONE, P_BREATH, P_DRONE, P_SWELL},
    {P_WIDTH, P_DAYLEN, K_LEDS, K_NONE},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Wind", "Kites", "Ambience", "Space"},
    {"Tone", "Breath", "Drone", "Swell"},
    {"Width", "Day", "Lights", ""},
};

/* the chrome's colours: a deep evening blue, chalk white, a kite's orange; sky blue for what is on */
#define K_BG RGB(16, 20, 32)
#define K_PANEL RGB(34, 40, 58)
#define K_LINE RGB(52, 60, 82)
#define K_DIM RGB(128, 138, 160)
#define K_TEXT RGB(240, 238, 228)
#define K_ACC RGB(246, 160, 72)
#define K_SOFT RGB(130, 190, 236)
#define K_LOW RGB(214, 84, 64)                 /* the battery at one bar or none */

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
    uint32_t saved_w_t;                /* the world's last save */
    char msg[2][28];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    float cloud;                       /* the clouds' drift, px */
    uint32_t last_ms;
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
static void caps(char *d, const char *s, int n)
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
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
static void set_style(int i)                    /* the hill playing kept as it is; hill i as it was left */
{
    project_style(i);
    project_apply();
    if (!en_playing)
        en_play(1);
    sp_tp.playing = 1;
    say_as("HILL", en_style_name(proj.style), 0);
    sp_style();
    mark_dirty();
}

static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

static const char *const NEXT_WORDS[PH_N] = {"STILL MORNING", "WIND RISING", "STEADY AFTERNOON", "GUSTY EVENING",
                                             "EASING"};
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
        en_skip();
        say("ON TO", NEXT_WORDS[(en_phase + 1) % PH_N]);
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
    case B_REC:
        proj.hold = (uint8_t)!proj.hold;
        en_hold(proj.hold);
        say("HOLD WEATHER", proj.hold ? "ON" : "OFF");
        mark_dirty();
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
            ui.saved_t = ui.saved_w_t = plat_ms();
            say("SAVED", 0);
        } else {
            warn("NOT SAVED", "FLASH ERROR");
        }
        break;
    default: break;
    }
}

/* MIDI in (USB and the TRS jack): a note sends a kite up */
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
        if ((ch >> i & 1u) && (keys >> i & 1u))
            en_note(53 + (int)i + (proj.oct - 2) * 12, 96);   /* key 0 is F3 */
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {        /* SELECT: the pages */
        int v = (int)ui.view + (e > 0 ? 1 : -1);
        if (v >= 0 && v < NVIEWS)
            set_view(v);
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {          /* ALGORITHM: Ambience, from any page */
        char t[12];
        turn(EN_ALGO, P_AMBIENCE, e);
        en_param_text(P_AMBIENCE, proj.par[P_AMBIENCE], t);
        if (ui.view == V_HILL) {
            ui.touched = 2;
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
        ui.reset_done = 1;                       /* held: the hill back to its recipe (not saved) */
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
/* three bands: the header (28 px), the hill (144), the knobs (68) */
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
/* a thin anti-aliased line (Wu's): alpha 0..1 */
static void aa_line(float x0, float y0, float x1, float y1, uint16_t c, float alpha)
{
    float dx = x1 - x0, dy = y1 - y0, adx = fm_fabsf(dx), ady = fm_fabsf(dy), t, n;
    int i, steps;
    if (adx < 0.01f && ady < 0.01f)
        return;
    n = fm_maxf(adx, ady);
    steps = (int)n + 1;
    for (i = 0; i <= steps; i++) {
        float x, y, f;
        int32_t ix, iy;
        t = (float)i / (float)steps;
        x = x0 + dx * t;
        y = y0 + dy * t;
        if (adx >= ady) {
            ix = (int32_t)(x + 0.5f);
            iy = (int32_t)fm_floorf(y);
            f = y - (float)iy;
            px_blend(ix, iy, c, (uint32_t)((1.0f - f) * alpha * 16.0f + 0.5f));
            px_blend(ix, iy + 1, c, (uint32_t)(f * alpha * 16.0f + 0.5f));
        } else {
            iy = (int32_t)(y + 0.5f);
            ix = (int32_t)fm_floorf(x);
            f = x - (float)ix;
            px_blend(ix, iy, c, (uint32_t)((1.0f - f) * alpha * 16.0f + 0.5f));
            px_blend(ix + 1, iy, c, (uint32_t)(f * alpha * 16.0f + 0.5f));
        }
    }
}
/* a filled triangle, its edges softened */
static void tri(float ax, float ay, float bx, float by, float cx, float cy, uint16_t c)
{
    float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax), s = area < 0.0f ? -1.0f : 1.0f;
    float la = fm_sqrtf((cx - bx) * (cx - bx) + (cy - by) * (cy - by)) + 1e-6f;
    float lb = fm_sqrtf((ax - cx) * (ax - cx) + (ay - cy) * (ay - cy)) + 1e-6f;
    float lc = fm_sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay)) + 1e-6f;
    int32_t x0 = (int32_t)fm_floorf(fm_minf(ax, fm_minf(bx, cx))), x1 = (int32_t)(fm_maxf(ax, fm_maxf(bx, cx)) + 1.0f);
    int32_t y0 = (int32_t)fm_floorf(fm_minf(ay, fm_minf(by, cy))), y1 = (int32_t)(fm_maxf(ay, fm_maxf(by, cy)) + 1.0f);
    int32_t x, y;
    if (fm_fabsf(area) < 0.5f)
        return;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f, e0, e1, e2, d;
            e0 = s * ((cx - bx) * (py - by) - (cy - by) * (px - bx)) / la;
            e1 = s * ((ax - cx) * (py - cy) - (ay - cy) * (px - cx)) / lb;
            e2 = s * ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) / lc;
            d = fm_minf(e0, fm_minf(e1, e2));
            if (d > -0.5f)
                px_blend(x, y, c, (uint32_t)(fm_clampf(d + 0.5f, 0.0f, 1.0f) * 16.0f + 0.5f));
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
        disc(fx + fm_cosf(e0) * rm - 0.5f, fy + fm_sinf(e0) * rm - 0.5f, th * 0.5f, v > 0.0f ? fill : track, 1.0f);
        disc(fx + fm_cosf(e1) * rm - 0.5f, fy + fm_sinf(e1) * rm - 0.5f, th * 0.5f, v >= 1.0f ? fill : track, 1.0f);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th * 0.5f + 1.3f, bg, 1.0f);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th * 0.5f + 0.6f, fill, 1.0f);
    }
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

/* the header: the hill in a tag; on the right the battery, unsaved, the day, HOLD */
static void draw_header(void)
{
    char t[16];
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(hash(2166136261u, proj.style | (uint32_t)en_playing << 16 | (uint32_t)en_hold_on << 17 |
                                                   (uint32_t)ui.dirty << 18 | (uint32_t)msg << 19),
                           (uint32_t)(batt() + 1) | en_day << 4),
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
        cat_u(cat_s(t, "DAY "), en_day);
        x -= text_w(&FONT_XS, t);
        cv_text(x, 8, &FONT_XS, t, en_playing ? K_DIM : K_LINE);
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

/* ---- the hill: the sky by the time of day, the sun, clouds drifting with the wind, the hilltop with its grass, the
 * kite flyers and their lines (lit by how loud each sings), the kites with their tails, gust streaks */
static uint16_t lerp_rgb(const uint8_t *a, const uint8_t *b, float t)
{
    t = fm_clampf(t, 0.0f, 1.0f);
    return RGB((int)((float)a[0] + ((float)b[0] - (float)a[0]) * t), (int)((float)a[1] + ((float)b[1] - (float)a[1]) * t),
               (int)((float)a[2] + ((float)b[2] - (float)a[2]) * t));
}
/* the sky: top and horizon colours through the day */
static const float SKY_X[6] = {0.0f, 0.15f, 0.5f, 0.75f, 0.9f, 1.0f};
static const uint8_t SKY_TOP[6][3] = {{120, 150, 200}, {104, 160, 222}, {62, 128, 206}, {70, 92, 168}, {44, 46, 104},
                                      {22, 26, 58}};
static const uint8_t SKY_LOW[6][3] = {{242, 208, 178}, {206, 228, 242}, {168, 208, 238}, {238, 168, 124},
                                      {226, 118, 92}, {88, 66, 108}};
static void sky_at(float dp, uint8_t top[3], uint8_t low[3])
{
    int i, k;
    for (i = 1; i < 5 && dp > SKY_X[i]; i++)
        ;
    {
        float t = fm_clampf((dp - SKY_X[i - 1]) / (SKY_X[i] - SKY_X[i - 1]), 0.0f, 1.0f);
        for (k = 0; k < 3; k++) {
            top[k] = (uint8_t)((float)SKY_TOP[i - 1][k] + ((float)SKY_TOP[i][k] - (float)SKY_TOP[i - 1][k]) * t);
            low[k] = (uint8_t)((float)SKY_LOW[i - 1][k] + ((float)SKY_LOW[i][k] - (float)SKY_LOW[i - 1][k]) * t);
        }
    }
}
/* the hills' grass: a colour each (the hill's sky number), darker as evening comes */
static const uint8_t GRASS[4][3] = {{84, 140, 72}, {120, 136, 62}, {98, 110, 84}, {70, 128, 96}};
static const uint16_t KITE_COL[8] = {RGB(240, 84, 62), RGB(250, 196, 58), RGB(62, 150, 226), RGB(236, 236, 228),
                                     RGB(210, 90, 170), RGB(96, 200, 120), RGB(250, 140, 50), RGB(150, 110, 230)};

static float hill_y(float x)                     /* the ground's top at x (px) */
{
    float u = (x - 96.0f) / 150.0f;
    return 112.0f + 26.0f * u * u;
}

static void draw_clouds(float dp, uint16_t c)
{
    static const int16_t CX[5] = {20, 92, 150, 208, 260}, CY[5] = {30, 52, 22, 46, 62};
    static const uint8_t CW[5] = {26, 18, 32, 22, 16};
    int i, k;
    (void)dp;
    for (i = 0; i < 5; i++) {
        float x = fm_floorf((float)CX[i] + ui.cloud * (0.6f + 0.15f * (float)i));
        int32_t y, w = CW[i];
        x = x - 300.0f * fm_floorf((x + 40.0f) / 300.0f);   /* round again off the right edge */
        for (k = 0; k < 3; k++) {
            int32_t ex = (int32_t)x + (k - 1) * w * 2 / 3, ey = CY[i] + (k == 1 ? -4 : 0), rw = k == 1 ? w : w * 2 / 3,
                    rh = k == 1 ? 8 : 6;
            for (y = -rh; y <= rh; y++) {
                float f = 1.0f - ((float)y * (float)y) / ((float)rh * (float)rh);
                int32_t hw = (int32_t)((float)rw * fm_sqrtf(fm_maxf(f, 0.0f)));
                cv_rect(ex - hw, ey + y, 2 * hw, 1, y < -rh / 3 ? mix(c, RGB(255, 255, 255), 6) : c);
            }
        }
    }
}

static void draw_kite(float kx, float ky, float s, float tilt, int shape, uint16_t c, float amp)
{
    float ct = fm_cosf(tilt), st = fm_sinf(tilt);
    uint16_t dark = mix(c, RGB(20, 20, 30), 7), lit = mix(c, RGB(255, 255, 240), q16(amp * 0.5f));
#define RX(px, py) (kx + (px) * ct - (py) * st)
#define RY(px, py) (ky + (px) * st + (py) * ct)
    if (shape == 0) {                            /* a diamond */
        tri(RX(0, -1.4f * s), RY(0, -1.4f * s), RX(-0.9f * s, -0.2f * s), RY(-0.9f * s, -0.2f * s), RX(0, 1.2f * s),
            RY(0, 1.2f * s), lit);
        tri(RX(0, -1.4f * s), RY(0, -1.4f * s), RX(0.9f * s, -0.2f * s), RY(0.9f * s, -0.2f * s), RX(0, 1.2f * s),
            RY(0, 1.2f * s), mix(lit, dark, 5));
        aa_line(RX(-0.9f * s, -0.2f * s), RY(-0.9f * s, -0.2f * s), RX(0.9f * s, -0.2f * s), RY(0.9f * s, -0.2f * s),
                dark, 0.8f);
    } else if (shape == 1) {                     /* a delta */
        tri(RX(-1.5f * s, -0.5f * s), RY(-1.5f * s, -0.5f * s), RX(0, -0.9f * s), RY(0, -0.9f * s), RX(0, 0.9f * s),
            RY(0, 0.9f * s), lit);
        tri(RX(1.5f * s, -0.5f * s), RY(1.5f * s, -0.5f * s), RX(0, -0.9f * s), RY(0, -0.9f * s), RX(0, 0.9f * s),
            RY(0, 0.9f * s), mix(lit, dark, 5));
    } else {                                     /* a box kite: two cells */
        tri(RX(-0.6f * s, -1.3f * s), RY(-0.6f * s, -1.3f * s), RX(0.6f * s, -1.3f * s), RY(0.6f * s, -1.3f * s),
            RX(0.6f * s, -0.4f * s), RY(0.6f * s, -0.4f * s), lit);
        tri(RX(-0.6f * s, -1.3f * s), RY(-0.6f * s, -1.3f * s), RX(-0.6f * s, -0.4f * s), RY(-0.6f * s, -0.4f * s),
            RX(0.6f * s, -0.4f * s), RY(0.6f * s, -0.4f * s), lit);
        tri(RX(-0.6f * s, 0.3f * s), RY(-0.6f * s, 0.3f * s), RX(0.6f * s, 0.3f * s), RY(0.6f * s, 0.3f * s),
            RX(0.6f * s, 1.2f * s), RY(0.6f * s, 1.2f * s), mix(lit, dark, 4));
        tri(RX(-0.6f * s, 0.3f * s), RY(-0.6f * s, 0.3f * s), RX(-0.6f * s, 1.2f * s), RY(-0.6f * s, 1.2f * s),
            RX(0.6f * s, 1.2f * s), RY(0.6f * s, 1.2f * s), mix(lit, dark, 4));
        aa_line(RX(0, -1.3f * s), RY(0, -1.3f * s), RX(0, 1.2f * s), RY(0, 1.2f * s), dark, 0.9f);
    }
#undef RX
#undef RY
}

static void draw_hill(void)
{
    uint8_t top[3], low[3], g3[3];
    float dp = en_daypos, wind = en_wind, gust = en_gust, t = (float)en_now_ms * 0.001f, dusk;
    int32_t y, x, i;
    uint16_t grass, ridge, cloud, sun, linec;
    const uint8_t *g = GRASS[en_style_sky(proj.style) & 3];
    sky_at(dp, top, low);
    for (y = 0; y < MAIN_H; y++)                 /* the sky */
        cv_rect(0, y, 240, 1, lerp_rgb(top, low, (float)y / 120.0f));
    dusk = fm_clampf((dp - 0.7f) / 0.3f, 0.0f, 1.0f);
    {                                            /* the sun: low in the east, high at noon, down in the west */
        float sx = 14.0f + dp * 212.0f, sy = 104.0f - 84.0f * fm_sin_pi(fm_clampf(dp, 0.0f, 1.0f)) + 30.0f * dusk * dusk;
        uint8_t s0[3] = {255, 244, 210}, s1[3] = {255, 150, 80};
        sun = lerp_rgb(s0, s1, dusk * 1.3f);
        disc(sx, sy, 15.0f, sun, 0.12f);
        disc(sx, sy, 10.0f, sun, 0.25f);
        disc(sx, sy, 6.5f, sun, 1.0f);
    }
    {
        uint8_t c0[3] = {250, 250, 252}, c1[3] = {250, 196, 186};
        cloud = mix(lerp_rgb(c0, c1, dusk * 1.5f), lerp_rgb(top, low, 0.3f), q16(0.25f + 0.4f * dusk));
        draw_clouds(dp, cloud);
    }
    for (i = 0; i < 7 && wind > 0.3f; i++) {     /* the wind's streaks */
        float sp = 30.0f + 160.0f * wind, len = 8.0f + 26.0f * (wind - 0.3f) + 30.0f * gust;
        float x0 = fm_fabsf((float)((i * 97) % 260) + t * sp * (0.8f + 0.1f * (float)i));
        float yy = 20.0f + (float)((i * 53) % 80);
        x0 = x0 - 280.0f * fm_floorf(x0 / 280.0f) - 30.0f;
        aa_line(x0, yy, x0 + len, yy + 0.5f, RGB(255, 255, 255), fm_clampf((wind - 0.3f) * 0.6f + gust, 0.0f, 0.55f));
    }
    /* the hilltop */
    g3[0] = (uint8_t)((float)g[0] * (1.0f - 0.55f * dusk));
    g3[1] = (uint8_t)((float)g[1] * (1.0f - 0.55f * dusk));
    g3[2] = (uint8_t)((float)g[2] * (1.0f - 0.4f * dusk));
    grass = RGB(g3[0], g3[1], g3[2]);
    ridge = mix(grass, RGB(250, 240, 200), 5);
    for (x = 0; x < 240; x++) {
        float hy = hill_y((float)x);
        int32_t iy = (int32_t)hy;
        cv_rect(x, iy + 1, 1, MAIN_H - iy, grass);
        px_blend(x, iy, ridge, (uint32_t)((1.0f - (hy - (float)iy)) * 16.0f));
        cv_rect(x, iy + 1, 1, 1, ridge);
    }
    for (x = 2; x < 240; x += 5) {               /* the grass, leaning with the wind */
        float hy = hill_y((float)x) + 1.0f, lean = 1.0f + 4.0f * wind + 5.0f * gust + 1.2f * fm_sinf(t * 3.0f + (float)x * 0.4f);
        aa_line((float)x, hy, (float)x + lean, hy - 3.5f - (float)(x * 7 % 3), mix(grass, RGB(20, 40, 20), 6), 0.9f);
    }
    /* the kites: the flyers and lines first, then the kites and tails over them */
    linec = mix(lerp_rgb(top, low, 0.5f), RGB(40, 40, 50), 8);
    for (i = 0; i < EN_NK; i++) {
        en_kite_t k = *(const en_kite_t *)&EN_KITE[i];
        float ax = 10.0f + k.ax * 200.0f, ay = hill_y(ax), kx, ky, sag, mx, my, px, py, a = fm_clampf(k.amp, 0.0f, 1.0f);
        int j;
        uint16_t c = KITE_COL[k.col & 7];
        if (k.state == KS_OFF)
            continue;
        kx = 10.0f + k.x * 200.0f;
        ky = ay - 8.0f - k.y * (ay - 36.0f);
        if (k.state != KS_LOOSE) {
            /* the flyer: a small figure at the line's foot */
            disc(ax - 1.0f, ay - 8.5f, 1.8f, RGB(30, 30, 40), 1.0f);
            cv_rect((int32_t)ax - 2, (int32_t)ay - 7, 3, 6, RGB(30, 30, 40));
            /* the line: slack ones sag; a singing one is lit in its kite's colour */
            sag = 4.0f + 16.0f * (1.0f - a) * (0.4f + k.y);
            mx = (ax + kx) * 0.5f;
            my = (ay - 6.0f + ky) * 0.5f + sag;
            px = ax;
            py = ay - 6.0f;
            for (j = 1; j <= 10; j++) {
                float u = (float)j / 10.0f, v = 1.0f - u;
                float qx = v * v * ax + 2.0f * v * u * mx + u * u * kx, qy = v * v * (ay - 6.0f) + 2.0f * v * u * my + u * u * ky;
                aa_line(px, py, qx, qy, mix(linec, mix(c, RGB(255, 255, 255), 6), q16(a * 1.2f)), 0.75f + 0.25f * a);
                px = qx;
                py = qy;
            }
        }
    }
    for (i = 0; i < EN_NK; i++) {
        en_kite_t k = *(const en_kite_t *)&EN_KITE[i];
        float ax = 10.0f + k.ax * 200.0f, ay = hill_y(ax), kx, ky, s, tilt, a = fm_clampf(k.amp, 0.0f, 1.0f), tx, ty;
        int j;
        uint16_t c = KITE_COL[k.col & 7];
        if (k.state == KS_OFF)
            continue;
        kx = 10.0f + k.x * 200.0f;
        ky = ay - 8.0f - k.y * (ay - 36.0f);
        if (kx < -20.0f || kx > 260.0f || ky < -20.0f)
            continue;
        s = 8.5f - 3.0f * fm_clampf(k.y, 0.0f, 1.0f);
        tilt = 0.25f * fm_sinf(t * (0.8f + 0.1f * (float)i) + (float)i) * (0.4f + wind) + 0.3f * wind;
        if (k.state == KS_LOOSE)
            tilt = t * 1.6f + (float)i;          /* tumbling away */
        /* the tail: bows streaming downwind, waving */
        tx = kx;
        ty = ky + s * 1.2f;
        for (j = 1; j <= 7; j++) {
            float nx = kx + (float)j * (1.2f + 2.6f * wind) - (float)j * 0.6f * fm_sinf(tilt),
                  ny = ky + s * 1.2f + (float)j * (3.2f - 1.5f * wind) + 1.6f * fm_sinf(t * 7.0f + (float)j * 0.9f + (float)i);
            aa_line(tx, ty, nx, ny, mix(c, RGB(40, 40, 50), 4), 0.8f);
            if (j & 1)
                disc(nx, ny, 1.6f, j & 2 ? c : RGB(250, 250, 240), 1.0f);
            tx = nx;
            ty = ny;
        }
        if (a > 0.3f && k.state == KS_FLY)       /* a singing kite glows a little */
            disc(kx, ky, s * 2.0f, mix(c, RGB(255, 255, 255), 8), (a - 0.3f) * 0.3f);
        draw_kite(kx, ky, s, tilt, k.shape, c, a);
        if (a > 0.2f && k.state == KS_FLY) {      /* its note */
            char nb[8];
            cat_s(nb, EN_NOTE_NAME[k.note % 12]);
            cv_text((int32_t)(kx + s + 3.0f), (int32_t)(ky - s - 9.0f), &FONT_XS, nb,
                    mix(lerp_rgb(top, low, ky / 120.0f), RGB(255, 255, 255), q16(0.35f + a * 0.65f)));
        }
    }
}

/* the top: the hill's time and its weather; the key and the tuning on the right */
static void draw_top(void)
{
    char t[40], *e;
    uint16_t sh = RGB(10, 14, 30), fg = RGB(255, 255, 255);
    int32_t w;
    en_clock(en_daypos, t);
    e = t;
    while (*e)
        e++;
    e = cat_s(e, "  ");
    cat_s(e, en_phase_name(en_phase));
    cv_text(9, 3, &FONT_S, t, sh);
    cv_text(8, 2, &FONT_S, t, fg);
    e = cat_s(t, EN_NOTE_NAME[en_key % 12]);
    e = cat_s(e, " ");
    cat_s(e, en_style_mode(proj.style));
    w = text_w(&FONT_XS, t);
    cv_text(233 - w, 6, &FONT_XS, t, sh);
    cv_text(232 - w, 5, &FONT_XS, t, mix(fg, RGB(255, 220, 160), 5));
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
    case B_HOME: L("THE HILL"); L("WIND, KITES, AMBIENCE,"); L(" SPACE"); break;
    case B_ENV: L("THE LINES"); L("TONE, BREATH, DRONE, SWELL"); break;
    case B_GLO: L("THE SETUP"); L("WIDTH, DAY LENGTH, LIGHTS"); break;
    case B_EDIT: L("ON TO THE NEXT"); L(" PART OF THE DAY"); break;
    case B_REC: L("HOLD THE WEATHER:"); L(" THE DAY STANDS STILL"); L("AGAIN: IT GOES ON"); break;
    case B_OCTDN: L("THE KEYS AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE KEYS AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES EVERY HILL"); L("KEEP HOLDING: THE HILL"); L(" BACK TO ITS RECIPE"); break;
    case B_SEL: L("SELECT TURNS THE PAGES"); L("PRESETS: THE HILL"); L("ALGORITHM: AMBIENCE"); L("KEYS: A KITE GOES UP"); break;
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
    uint32_t h, now = plat_ms();
    ui.cloud += (float)(now - ui.last_ms) * 0.001f * (2.0f + 14.0f * en_wind) * (en_playing ? 1.0f : 0.2f);
    if (ui.cloud > 3000.0f)
        ui.cloud -= 3000.0f;
    ui.last_ms = now;
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
    draw_hill();
    draw_top();
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
        if (hi - lo < 7) {
            int n = hi - lo + 1, j, pw = n > 4 ? 6 : 9, gap = n > 5 ? 2 : 3, w0 = n * (pw + gap) - gap;
            for (j = 0; j < n; j++)
                cv_round(cx - w0 / 2 + j * (pw + gap), 33, pw, 8, 2, (k == P_KITES ? j <= v - lo : j == v - lo) ? fill : K_LINE);
        } else {
            arc(cx, 37, 12, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, K_LINE, fill, bg);
        }
        knob_text(k, t);
        text_c(cx, 50, text_w(&FONT_B, t) > 56 ? &FONT_S : &FONT_B, t, hot ? K_ACC : K_TEXT);
    }
    cv_blit(0, KNB_Y);
}

/* the lights: the page's button, PLAY, HOLD; the chord's notes on the keys */
static void leds(void)
{
    uint32_t b = 0, k = 0, i, pcs = en_chord_pcs;
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (en_hold_on)
        b |= 1u << B_REC;
    if (proj.leds && en_playing)
        for (i = 0; i < NKEYS; i++)
            if (pcs >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    plat_glow(proj.leds == 1);
    plat_leds(b, k | ui.keys);
}

/* AUTOSAVE (the flash wears, and a write can glitch the sound): the settings once the knobs have rested 10 s, at
 * most every 5 minutes; the world (the day, its hour, the chord) at most hourly. Both at a quiet moment: a lull (no
 * gust, the lines soft) or resting. SAVE saves at once */
#define AUTOSAVE_REST 10000u
#define AUTOSAVE_SETTINGS 300000u
#define AUTOSAVE_WORLD 3600000u
static void autosave(void)
{
    uint32_t now = plat_ms(), since = now - ui.saved_t;
    int quiet = !en_playing || en_quiet, want;
    if (!quiet || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_REST)
        return;
    want = (ui.dirty && since >= AUTOSAVE_SETTINGS) || (en_playing && now - ui.saved_w_t >= AUTOSAVE_WORLD);
    if (!want)
        return;
    if (project_save() == 0) {
        ui.dirty = 0;
        ui.saved_w_t = now;
    }
    ui.saved_t = now;                            /* (a failed save waits its turn too) */
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader (announce.c; fm1-reader): a page once it settles, with its knobs; a
 * knob as it turns; a help card as it comes up; each new part of the day (its time, its wind, the kites up); playing
 * and resting; what a button did (say). WHERE is the whole screen in two: the page and the header, then the hill. */
static struct {
    uint8_t view, help, started, phase;
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
        if (VIEW_KNOB[ui.view][i] == K_NONE)
            continue;
        sp_cat(i ? ", " : ": ");
        sp_knob(ui.view, i);
    }
    if (!all)
        return;
    sp_cat(". Hill ");
    sp_cat(en_style_name(proj.style));
    sp_cat(sp_tp.playing ? ", playing" : ", resting");
    if (en_hold_on)
        sp_cat(", weather held");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static void sp_world(void)                       /* "Day 3, 14:20, gusty wind. D major pentatonic, 4 kites up" */
{
    char t[12];
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("Day ");
    sp_int(en_day);
    sp_cat(", ");
    en_clock(en_daypos, t);
    sp_cat(t);
    sp_cat(", ");
    sp_cat(en_phase_name(en_phase));
    sp_cat(" wind. ");
    sp_cat(EN_NOTE_NAME[en_key % 12]);
    sp_cat(" ");
    sp_cat(en_style_mode(proj.style));
    sp_cat(", ");
    sp_int(en_kites);
    sp_cat(en_kites == 1 ? " kite up" : " kites up");
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
    sp_cat("HILL ");
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
    sp.phase = en_phase;
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
            sp_world();
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
    if (en_phase != sp.phase) {                  /* a new part of the day: queued */
        sp.phase = en_phase;
        sp_world();
        ann_say(sp_buf, 0, 0);
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card, world line and message as a screen reader gets them (the "speakdump" command) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}, {"RESTING", 0},
                                         {"PLAYING", 0}, {"HOLD WEATHER", "ON"}, {"HOLD WEATHER", "OFF"},
                                         {"SAVED", 0}, {"KEYS OCTAVE", "-2"}, {"KEYS OCTAVE", "-1"},
                                         {"KEYS OCTAVE", "0"}, {"KEYS OCTAVE", "+1"}, {"KEYS OCTAVE", "+2"}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}, {"KEYS OCTAVE", "LOWEST"},
                                          {"KEYS OCTAVE", "HIGHEST"}};
    project_t keep = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty, keep_play = sp_tp.playing;
    uint8_t keep_key = en_key, keep_phase = en_phase, keep_hold = en_hold_on, keep_kites = en_kites;
    uint32_t keep_day = en_day;
    float keep_dp = en_daypos;
    int v, i, x, lo, hi;
    char t[64];
    for (v = 0; v < NVIEWS; v++) {
        ui.view = (uint8_t)v;
        sp_page(0);
        sp_print("SCREEN", sp_buf);
        for (i = 0; i < 4; i++) {
            int k = VIEW_KNOB[v][i];
            if (k == K_NONE)
                continue;
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
    for (x = 0; x < 60; x++) {                   /* WHERE: playing, resting, unsaved, held; the days, the hills */
        ui.view = (uint8_t)(x % NVIEWS);
        sp_tp.playing = (uint8_t)(x & 1);
        ui.dirty = (uint8_t)(x >> 1 & 1);
        en_hold_on = (uint8_t)(x % 7 == 5);
        en_key = (uint8_t)(x * 5 % 12);
        en_phase = (uint8_t)(x % PH_N);
        en_day = (uint32_t)x / 3u + 1u;
        en_daypos = (float)(x % 20) * 0.05f;
        en_kites = (uint8_t)(x % 7);
        proj.style = (uint8_t)(x % NSTYLES);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_world();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
    en_key = keep_key;
    en_phase = keep_phase;
    en_hold_on = keep_hold;
    en_kites = keep_kites;
    en_day = keep_day;
    en_daypos = keep_dp;
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
    for (i = 0; i < PH_N; i++) {
        msg_text("ON TO", NEXT_WORDS[i], t);
        sp_print("MSG", t);
    }
    for (i = 0; i < (int)(sizeof WARN / sizeof WARN[0]); i++) {
        msg_text(WARN[i][0], WARN[i][1], t);
        sp_print("WARN", t);
    }
    for (x = 0; x <= 100; x += 10) {
        char o[12];
        en_param_text(P_AMBIENCE, x, o);
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
    ui.saved_t = ui.saved_w_t = ui.last_ms = plat_ms();
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
