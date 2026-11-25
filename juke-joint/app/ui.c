/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * JUKE JOINT: ashtonwallen, 2026 */
/* JUKE JOINT's UI: the panel and the screen (from AMB-1's: its knobs, help cards, messages and screen reader).
 * Part of the unity build (after gfx.c and project.c); the host simulator includes it too.
 *
 *   PLAY           playing / resting                 EDIT   the next tune (the band stops, counts off another)
 *   REC            REPEAT: the solos go round again  OCT- / OCT+   the keys' octave
 *   PRESETS        the night (each kept as left)     ALGORITHM     Harp (how often he sits in; 0: never)
 *   HOME ENV GLO   the pages: Band, Mix, Setup (SELECT steps through them)
 *   KNOB 1-4       the page's four values
 *   keys           play along on the piano
 *   SAVE           saves (it also saves by itself at a quiet moment: see autosave); held 1.5 s: the night back to
 *                  its recipe */
#include "fastmath.h"

enum { V_BAND, V_MIX, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Band", "Mix", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_GLO};
enum { K_LEDS = 100, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_TEMPO, P_HARP, P_DRIVE, P_ROOM},
    {P_PIANO, P_BASS, P_DRUMS, P_AMBI},
    {P_AGE, P_WIDTH, P_LENGTH, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Tempo", "Harp", "Drive", "Room"},
    {"Piano", "Bass", "Drums", "Ambience"},
    {"Age", "Width", "Length", "Lights"},
};

/* the screen's colours: a back room after dark: soot, old wood, a bare bulb's amber; brass, a red pilot light */
#define K_BG RGB(16, 12, 10)
#define K_PANEL RGB(44, 34, 28)
#define K_LINE RGB(70, 56, 46)
#define K_DIM RGB(156, 138, 118)
#define K_TEXT RGB(246, 234, 212)
#define K_ACC RGB(248, 184, 84)
#define K_RED RGB(222, 88, 62)
#define K_LOW RGB(222, 88, 62)                 /* the battery at one bar or none */

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
    uint32_t saved_track;              /* the tune the last save kept */
    char msg[2][28];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    uint32_t keys_on;                  /* the keys sounding (played along), as MIDI notes, key bit */
    /* the stage, eased toward the band's state */
    uint32_t draw_t;
    float t, hv[NHITS], rh_x, neck_y, clap, harp_a, bulb;
    uint8_t hits[NHITS];
    int8_t bg_scene;                   /* the room the backdrop was drawn for (-1: none yet) */
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
static char *cat_time(char *b, uint32_t ms)      /* "2:05" */
{
    uint32_t s = ms / 1000u;
    b = cat_u(b, s / 60u);
    *b++ = ':';
    *b++ = (char)('0' + s % 60u / 10u);
    *b++ = (char)('0' + s % 10u);
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
static void set_style(int i)                    /* the night playing kept as it is; night i as it was left */
{
    project_style(i);
    project_apply();
    en_skip();                                   /* on to a new tune on the new night */
    if (!en_playing)
        en_play(1);
    sp_tp.playing = 1;
    say_as("NIGHT", en_style_name(proj.style), 0);
    sp_style();
    mark_dirty();
}

static void keys_off(void)
{
    int k;
    for (k = 0; k < NKEYS; k++)
        if (ui.keys_on >> k & 1u)
            en_note(53 + k + (proj.oct - 2) * 12, 0);
    ui.keys_on = 0;
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
        en_skip();
        say("NEXT TUNE", 0);
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
        say("REPEAT", en_repeat_on ? "OFF" : "ON");
        en_repeat(!en_repeat_on);
        break;
    case B_OCTDN: case B_OCTUP: {
        int o = proj.oct + (b == B_OCTUP ? 1 : -1);
        char t[8];
        if (o < 0 || o > 4) {
            warn("KEYS OCTAVE", o < 0 ? "LOWEST" : "HIGHEST");
            break;
        }
        keys_off();
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
            ui.saved_track = en_track_no;
            say("SAVED", 0);
        } else {
            warn("NOT SAVED", "FLASH ERROR");
        }
        break;
    default: break;
    }
}

/* MIDI in (USB and the TRS jack): notes play along on the piano */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0)) {
        uint32_t st = (pkt >> 8) & 0xF0u, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == 0x90u && v)
            en_note((int)n, (int)v);
        else if (st == 0x80u || st == 0x90u)
            en_note((int)n, 0);
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
        if (ch >> i & 1u) {
            int note = 53 + (int)i + (proj.oct - 2) * 12;   /* key 0 is F3 */
            if (keys >> i & 1u) {
                en_note(note, 96);
                ui.keys_on |= 1u << i;
            } else if (ui.keys_on >> i & 1u) {
                en_note(note, 0);
                ui.keys_on &= ~(1u << i);
            }
        }
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {        /* SELECT: the pages */
        int v = (int)ui.view + (e > 0 ? 1 : -1);
        if (v >= 0 && v < NVIEWS)
            set_view(v);
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {          /* ALGORITHM: Harp, from any page */
        char t[12];
        turn(EN_ALGO, P_HARP, e);
        en_param_text(P_HARP, proj.par[P_HARP], t);
        if (ui.view == V_BAND) {
            ui.touched = 1;
            ui.touch_until = plat_ms() + 1200u;
        } else {
            say("HARP", t);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        set_style(proj.style + (e > 0 ? 1 : -1));
        ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && plat_ms() - ui.down_t[B_SAVE] > RESET_HOLD) {
        ui.reset_done = 1;                       /* held: the night back to its recipe (not saved) */
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
/* three bands: the header (28 px), the stage (144), the knobs (68) */
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
/* a thick anti-aliased line: discs along it (ymax: nothing drawn below) */
static void rod(float x0, float y0, float x1, float y1, float r, uint16_t c, float ymax)
{
    float dx = x1 - x0, dy = y1 - y0, len = fm_sqrtf(dx * dx + dy * dy), t;
    int i, n = (int)(len / 1.2f) + 1;
    for (i = 0; i <= n; i++) {
        t = (float)i / (float)n;
        if (y0 + dy * t <= ymax)
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


/* the header: the night in a tag; on the right the battery, unsaved, the tempo, HARP (lit while he's up), REPEAT */
static void draw_header(void)
{
    char t[16];
    int msg = plat_ms() < ui.msg_until, harp = en_harp_in ? 1 + en_harp_on : 0;
    uint32_t h = hash(hash(hash(2166136261u, proj.style | (uint32_t)en_bpm << 8 | (uint32_t)en_playing << 16 |
                                                   (uint32_t)en_repeat_on << 17 | (uint32_t)ui.dirty << 18 |
                                                   (uint32_t)msg << 19 | (uint32_t)harp << 20),
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
        int32_t x = 232, w, hw = text_w(&FONT_XS, "HARP");
        if (batt() >= 0) {
            x -= BATT_W;
            draw_batt(x, batt());
            x -= 8;
        }
        if (ui.dirty) {
            cv_round(x - 5, 12, 5, 5, 2, K_DIM);
            x -= 11;
        }
        cat_u(t, en_bpm);
        x -= text_w(&FONT_XS, "BPM");
        cv_text(x, 8, &FONT_XS, "BPM", K_DIM);
        x -= text_w(&FONT_S, t) + 3;
        cv_text(x, 6, &FONT_S, t, en_playing ? K_TEXT : K_DIM);
        if (!en_playing) {                       /* resting: two bars */
            x -= 14;
            cv_rect(x, 9, 3, 10, K_DIM);
            cv_rect(x + 5, 9, 3, 10, K_DIM);
        }
        x -= hw + 14;                            /* the harp player: up on the stage, blowing */
        if (harp) {
            cv_round(x, 7, hw + 8, 15, 5, harp == 2 ? K_RED : mix(K_RED, K_BG, 6));
            cv_text(x + 4, 7, &FONT_XS, "HARP", K_TEXT);
        } else {
            cv_round(x, 7, hw + 8, 15, 5, K_LINE);
            cv_round(x + 1, 8, hw + 6, 13, 4, K_BG);
            cv_text(x + 4, 7, &FONT_XS, "HARP", K_LINE);
        }
        if (en_repeat_on) {
            x -= text_w(&FONT_XS, "REPEAT") + 14;
            cv_round(x, 7, text_w(&FONT_XS, "REPEAT") + 8, 15, 5, K_ACC);
            cv_text(x + 4, 7, &FONT_XS, "REPEAT", K_BG);
        }
        w = text_w(&FONT_B, en_style_name(proj.style)) + 12;
        if (4 + w < x - 4) {
            cv_round(4, 4, w, 20, 6, K_ACC);
            cv_text(10, 6, &FONT_B, en_style_name(proj.style), K_BG);
        }
    }
    cv_blit(0, 0);
}

/* ---- the back room: a plank wall, one bare bulb on its cord, a low stage with the band on it: the upright piano
 * side on, the harp player at the front with his little amp (when he's sat in), the bass, a small kit with
 * brushes; the crowd's heads in front; along the bottom the form, its bar lit. The room is drawn once (bg_px), the
 * band, the bulb and the crowd every frame */
static uint16_t bg_px[240 * MAIN_H] PLAT_POOL;  /* (as the canvas keeps them: byte-swapped) */
#define TOP_H 28                                 /* the title, its line */
#define BEAM_Y 28                                /* the rafter the bulb hangs from */
#define FLOOR_Y 100                              /* the stage's boards ... */
#define EDGE_Y 108                               /* ... its front edge */
#define FORM_Y 127                               /* the form strip */
#define BULB_X 120.0f
#define CORD 18.0f

static uint16_t rgbf(float r, float g, float b)  /* a colour from 0..255 floats, clamped */
{
    int R = (int)fm_clampf(r, 0.0f, 255.0f), G = (int)fm_clampf(g, 0.0f, 255.0f), B = (int)fm_clampf(b, 0.0f, 255.0f);
    return RGB(R, G, B);
}
static uint16_t shade(int r, int g, int b, float l) { return rgbf((float)r * l, (float)g * l, (float)b * l); }
/* light added to a pixel, a (0..1) of the colour r g b (in 5-6-5 steps) */
static void px_add(int32_t x, int32_t y, float a, float r, float g, float b)
{
    uint16_t *q;
    uint32_t c;
    int R, G, B;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1 || a <= 0.004f)
        return;
    q = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    c = swap16(*q);
    R = (int)(c >> 11) + (int)(a * r);
    G = (int)((c >> 5) & 63u) + (int)(a * g);
    B = (int)(c & 31u) + (int)(a * b);
    *q = swap16((uint32_t)((R > 31 ? 31 : R) << 11 | (G > 63 ? 63 : G) << 5 | (B > 31 ? 31 : B)));
}
static void glow(float cx, float cy, float r, float a, float cr, float cg, float cb)   /* a soft round light */
{
    int32_t x, y;
    for (y = (int32_t)(cy - r); y <= (int32_t)(cy + r); y++)
        for (x = (int32_t)(cx - r); x <= (int32_t)(cx + r); x++) {
            float dx = (float)x - cx, dy = (float)y - cy, d = (dx * dx + dy * dy) / (r * r);
            if (d < 1.0f)
                px_add(x, y, a * (1.0f - d) * (1.0f - d), cr, cg, cb);
        }
}
static void ellipse(float cx, float cy, float rx, float ry, uint16_t c)   /* filled, its sides anti-aliased */
{
    int32_t y;
    for (y = (int32_t)fm_floorf(cy - ry); y <= (int32_t)(cy + ry); y++) {
        float dy = ((float)y + 0.5f - cy) / ry, w, x0, x1;
        int32_t x;
        if (dy * dy >= 1.0f)
            continue;
        w = rx * fm_sqrtf(1.0f - dy * dy);
        x0 = cx - w;
        x1 = cx + w;
        for (x = (int32_t)fm_floorf(x0); x <= (int32_t)x1; x++) {
            float a = fm_clampf(fm_minf((float)x + 1.0f - x0, x1 - (float)x), 0.0f, 1.0f);
            px_blend(x, y, c, (uint32_t)(a * 16.0f + 0.5f));
        }
    }
}

/* each room's wall and its bulb: pine boards, boards painted blue and peeling, oxblood paint, a tin wall */
static const uint8_t ROOM_WALL[4][3] = {{126, 82, 46}, {58, 86, 104}, {112, 36, 30}, {104, 108, 98}};
static const uint8_t ROOM_BULB[4][3] = {{31, 21, 8}, {31, 22, 10}, {31, 19, 7}, {28, 24, 14}};

static void bg_build(int room)
{
    int32_t x, y, i;
    const uint8_t *W = ROOM_WALL[room & 3], *L = ROOM_BULB[room & 3];
    cv_begin(240, MAIN_H, K_BG);
    for (y = 0; y < BEAM_Y; y++)                 /* the ceiling: soot */
        cv_rect(0, y, 240, 1, RGB(18 + y / 6, 13 + y / 8, 11 + y / 10));
    for (y = BEAM_Y + 4; y < FLOOR_Y; y++)       /* the wall */
        for (x = 0; x < 240; x++) {
            float v;
            if ((room & 3) == 3) {               /* tin: corrugated, a rust stain here and there */
                float rib = fm_sinf((float)x * 0.9f);
                uint32_t hh = hash(hash(0x71Au, (uint32_t)x / 9u), (uint32_t)y / 13u);
                v = 0.78f + 0.16f * rib;
                cv_px[(uint32_t)y * 240u + (uint32_t)x] =
                    swap16(hh % 11u == 0 ? shade(120, 64, 34, v * 0.9f) : shade(W[0], W[1], W[2], v));
            } else {                             /* boards standing side by side: a gap, the grain, a knot */
                int col = x / 11, seam = x % 11 == 0, edge = x % 11 == 1;
                uint32_t hh = hash(hash(0xB0A4u, (uint32_t)col), (uint32_t)(y + col * 37) / 47u);
                float grain = 0.06f * fm_sinf((float)y * 0.33f + (float)col * 2.7f + 0.8f * fm_sinf((float)y * 0.07f + (float)col));
                v = seam ? 0.42f : edge ? 1.02f : 0.8f + (float)(hash(0x51u, (uint32_t)col) % 9u) * 0.025f + grain;
                if ((y + col * 37) % 47 == 0 && !seam)   /* a nail */
                    v = 0.6f;
                if (room == 1 && (hh >> 8) % 4u == 0 && !seam)   /* the paint gone: the wood */
                    cv_px[(uint32_t)y * 240u + (uint32_t)x] = swap16(shade(120, 84, 52, v * 0.8f));
                else
                    cv_px[(uint32_t)y * 240u + (uint32_t)x] = swap16(shade(W[0], W[1], W[2], v));
            }
        }
    for (i = 0; i < 5; i++) {                    /* knots in the boards */
        uint32_t hh = hash(0x4E07u + (uint32_t)room, (uint32_t)i);
        if ((room & 3) != 3)
            ellipse(10.0f + (float)(hh % 220u), (float)(BEAM_Y + 10) + (float)((hh >> 8) % 56u), 2.2f, 1.2f,
                    shade(W[0], W[1], W[2], 0.45f));
    }
    /* the rafter */
    cv_rect(0, BEAM_Y, 240, 4, RGB(58, 38, 24));
    cv_rect(0, BEAM_Y + 3, 240, 1, RGB(30, 20, 14));
    cv_rect(0, BEAM_Y, 240, 1, RGB(84, 58, 36));
    /* a show bill tacked up, faded; a horseshoe over the stage */
    cv_rect(176, 40, 30, 38, RGB(196, 176, 132));
    cv_rect(178, 43, 26, 7, RGB(150, 52, 36));
    for (i = 0; i < 5; i++)
        cv_rect(179, 54 + i * 4, 18 + (int32_t)(hash(0x5BEu, (uint32_t)i) % 7u), 1, RGB(120, 104, 80));
    ellipse(183.0f, 72.0f, 4.0f, 3.0f, RGB(150, 52, 36));
    cv_rect(190, 70, 12, 2, RGB(120, 104, 80));
    for (i = 0; i < 9; i++) {                    /* the horseshoe, open end up */
        float a = 0.2f + 0.33f * (float)i, hx = 46.0f + 4.2f * fm_cosf(a), hy = 44.0f + 4.6f * fm_sinf(a);
        disc(hx, hy, 1.2f, RGB(110, 100, 92));
    }
    /* the stage: boards running away from us, the wash on them, the lip */
    for (y = FLOOR_Y; y < EDGE_Y; y++) {
        float l = 0.7f + 0.4f * (float)(y - FLOOR_Y) / (float)(EDGE_Y - FLOOR_Y);
        for (x = 0; x < 240; x++) {
            float s = ((float)x - 120.0f) / (float)(EDGE_Y + 30 - y);   /* the boards' lines converge */
            int seam = (int)(s * 22.0f + 100.0f) != (int)(s * 22.0f + 100.4f);
            cv_px[(uint32_t)y * 240u + (uint32_t)x] = swap16(shade(104, 66, 38, l * (seam ? 0.7f : 0.92f)));
        }
    }
    cv_rect(0, EDGE_Y, 240, 1, RGB(150, 104, 62));
    cv_rect(0, EDGE_Y + 1, 240, FORM_Y - EDGE_Y - 1, RGB(26, 18, 14));
    /* the bulb's light: strongest round it, falling off over the wall and the boards */
    for (y = BEAM_Y + 4; y < EDGE_Y; y++)
        for (x = 0; x < 240; x++) {
            float dx = ((float)x - BULB_X) / 1.3f, dy = (float)y - (float)(BEAM_Y + CORD + 6), d2 = dx * dx + dy * dy;
            float a = 0.55f / (1.0f + d2 * (1.0f / 1800.0f)) - 0.12f;
            if (a > 0.0f)
                px_add(x, y, a, (float)L[0], (float)L[1] * 2.0f, (float)L[2]);
        }
    /* the dark at the room's corners */
    for (y = BEAM_Y + 4; y < EDGE_Y; y++)
        for (x = 0; x < 240; x++) {
            float e = fm_fabsf((float)x - 120.0f) / 120.0f;
            if (e > 0.6f)
                px_blend(x, y, K_BG, (uint32_t)((e - 0.6f) * 22.0f));
        }
    memcpy(bg_px, cv_px, sizeof bg_px);
    ui.bg_scene = (int8_t)room;
}

static float bob(float k) { return fm_sinf((en_beat + (float)en_beat_no) * FM_PI * k); }

/* the upright piano, side on at the left; the pianist on a stool facing it, in his hat, the hands on the keys */
static void draw_piano(void)
{
    float hb = ui.hv[HIT_RH], lb = ui.hv[HIT_LH], rx = ui.rh_x;
    uint16_t body = RGB(46, 26, 18), edge = RGB(96, 62, 40), suit = RGB(40, 34, 40), skin = RGB(150, 100, 70);
    uint16_t hat = RGB(30, 24, 20), band = RGB(150, 52, 36);
    cv_round(6, 56, 26, 44, 2, body);            /* the case, its top, the candle stain of a glass on it */
    cv_rect(4, 53, 30, 4, edge);
    cv_rect(8, 60, 1, 36, RGB(70, 44, 30));
    cv_rect(31, 76, 15, 4, body);                /* the keyboard's shelf, the keys' edge */
    cv_rect(31, 75, 15, 1, RGB(220, 210, 186));
    cv_rect(42, 80, 3, 20, body);                /* its leg */
    cv_rect(12, 46, 6, 7, RGB(150, 168, 160));   /* the tip jar */
    cv_rect(13, 50, 4, 3, RGB(184, 150, 70));
    cv_rect(22, 50, 7, 3, RGB(70, 66, 60));      /* an ashtray */
    cv_rect(56, 88, 12, 3, RGB(70, 44, 30));     /* the stool */
    cv_rect(60, 91, 3, 9, RGB(50, 32, 22));
    cv_round(52, 62 + (int32_t)(0.6f * bob(1.0f)), 15, 28, 5, suit);   /* the pianist, leaning in */
    disc(57.0f, 55.0f + 0.6f * bob(1.0f), 5.6f, skin);
    cv_rect(50, 50, 15, 2, hat);                 /* his hat: brim, band, crown */
    cv_rect(53, 45, 9, 5, hat);
    cv_rect(53, 48, 9, 1, band);
    rod(54.0f, 67.0f, rx + 2.0f, 74.0f - 2.0f * lb, 1.7f, suit, 999.0f);   /* the arms to the keys */
    rod(57.0f, 68.0f, rx - 4.0f, 74.0f - 2.0f * hb, 1.7f, suit, 999.0f);
    disc(rx + 2.0f, 74.0f - 2.0f * lb, 2.0f, skin);
    disc(rx - 4.0f, 74.0f - 2.0f * hb, 2.0f, skin);
    rod(58.0f, 89.0f, 50.0f, 100.0f, 2.0f, suit, 999.0f);   /* a leg, the foot on the pedal */
}

/* the harp player at the front, at his little amp: cupped hands, the bullet mic, its cable */
static void draw_harp(void)
{
    float a = ui.harp_a, on = (float)en_harp_on, dy = (1.0f - a) * 52.0f, sh = on * 0.7f * fm_sinf(ui.t * 31.0f);
    float hx = 100.0f + sh, hy = 55.0f + dy + on * 0.8f * bob(1.0f);
    uint16_t suit = RGB(70, 40, 34), skin = RGB(146, 98, 66), hat = RGB(36, 30, 26);
    cv_round(76, 86, 16, 14, 2, RGB(52, 40, 32));   /* the amp, its grille, the pilot light */
    cv_rect(78, 89, 12, 8, RGB(120, 104, 80));
    if (a > 0.02f) {
        disc(89.0f, 88.0f, 1.0f, on > 0.0f ? RGB(255, 90, 60) : RGB(90, 30, 24));
        if (on > 0.0f)
            glow(89.0f, 88.0f, 5.0f, 0.4f, 31.0f, 8.0f, 4.0f);
    }
    if (a < 0.02f)
        return;
    rod(96.0f, 84.0f + dy, 96.0f, 100.0f, 2.2f, suit, (float)FLOOR_Y);   /* his legs, his body, his head */
    rod(104.0f, 84.0f + dy, 105.0f, 100.0f, 2.2f, suit, (float)FLOOR_Y);
    if (60.0f + dy < (float)FLOOR_Y)
        cv_round(92, (int32_t)(60.0f + dy), 17, (int32_t)fm_minf(27.0f, (float)FLOOR_Y - 60.0f - dy), 5, suit);
    if (50.0f + dy < (float)FLOOR_Y) {
        disc(100.0f, 50.0f + dy, 5.6f, skin);
        cv_rect(93, (int32_t)(45.0f + dy), 15, 2, hat);
        cv_rect(96, (int32_t)(40.0f + dy), 9, 5, hat);
    }
    if (hy < (float)FLOOR_Y) {
        rod(93.0f, 64.0f + dy, hx - 2.0f, hy, 1.8f, suit, (float)FLOOR_Y);   /* the arms up to the mouth */
        rod(108.0f, 64.0f + dy, hx + 2.0f, hy, 1.8f, suit, (float)FLOOR_Y);
        ellipse(hx, hy, 4.4f, 3.4f, skin);       /* the cupped hands, the mic's end showing */
        disc(hx, hy + 2.5f, 1.6f, RGB(150, 150, 150));
        rod(hx, hy + 3.0f, 92.0f, 94.0f, 0.4f, RGB(20, 16, 14), (float)FLOOR_Y);   /* the cable to the amp */
        if (on > 0.0f)
            glow(hx, hy, 9.0f, 0.18f + 0.2f * ui.hv[HIT_HARP], 31.0f, 26.0f, 14.0f);
    }
}

/* the bass, upright, the bassist beside it: the left hand up the neck where the note is, the right plucking */
static void draw_bass(void)
{
    float p = ui.hv[HIT_BASS], ny = ui.neck_y;
    uint16_t wood = RGB(150, 74, 34), wood_hi = RGB(196, 112, 56), dark = RGB(36, 22, 14);
    uint16_t suit = RGB(44, 40, 52), skin = RGB(120, 80, 56), hat = RGB(70, 64, 58);
    rod(158.0f, 82.0f, 157.0f, 100.0f, 2.2f, suit, 999.0f);   /* the bassist */
    rod(166.0f, 82.0f, 168.0f, 100.0f, 2.2f, suit, 999.0f);
    cv_round(154, 44, 17, 42, 5, suit);
    disc(162.0f, 38.0f + 0.5f * bob(1.0f), 5.6f, skin);
    cv_rect(155, 33, 15, 2, hat);
    cv_rect(158, 28, 9, 5, hat);
    rod(146.0f, 62.0f, 149.0f, 30.0f, 1.5f, dark, 999.0f);   /* the neck and scroll, the body, the endpin */
    disc(149.5f, 29.0f, 2.4f, dark);
    ellipse(145.0f, 70.0f, 8.0f, 8.5f, wood);
    ellipse(145.0f, 86.0f, 11.5f, 10.0f, wood);
    ellipse(142.0f, 68.0f, 3.5f, 4.5f, wood_hi);
    ellipse(141.0f, 84.0f, 4.5f, 5.5f, wood_hi);
    cv_rect(142, 89, 7, 2, dark);
    rod(145.0f, 95.0f, 145.0f, 100.0f, 0.5f, dark, 999.0f);
    rod(148.8f, 32.0f, 145.4f, 90.0f, 0.4f, RGB(210, 200, 180), 999.0f);
    rod(156.0f, 50.0f, 149.0f - (ny - 32.0f) * 0.06f, ny, 1.7f, suit, 999.0f);
    disc(148.5f - (ny - 32.0f) * 0.06f, ny, 2.1f, skin);
    rod(168.0f, 52.0f, 152.0f, 76.0f - 2.0f * p, 1.7f, suit, 999.0f);
    disc(150.0f + 2.0f * p, 77.0f - 2.0f * p, 2.1f, skin);
}

/* a small kit: the bass drum, the snare, the hi-hat, one ride; brushes, the right one circling with the beat */
static void draw_drums(void)
{
    float sn = ui.hv[HIT_SNARE], k = ui.hv[HIT_KICK], hat = ui.hv[HIT_HAT], ang = en_beat * FM_TWO_PI;
    uint16_t suit = RGB(52, 44, 36), skin = RGB(136, 92, 62), metal = RGB(170, 168, 164), shell = RGB(120, 30, 28);
    float bx = 192.0f + 4.5f * fm_cosf(ang), by = 76.0f + 1.2f * fm_sinf(ang), lx = 186.0f, ly = 72.0f - 4.0f * sn;
    cv_round(198, 52, 18, 32, 5, suit);          /* the drummer */
    disc(207.0f, 46.0f + 0.5f * bob(2.0f), 5.6f, skin);
    cv_rect(201, 40, 13, 2, RGB(26, 22, 20));    /* a flat cap */
    disc(207.0f, 39.5f, 4.0f, RGB(26, 22, 20));
    rod(178.0f, 68.0f, 178.0f, 100.0f, 0.6f, metal, 999.0f);   /* the stands */
    rod(230.0f, 60.0f, 233.0f, 100.0f, 0.6f, metal, 999.0f);
    ellipse(178.0f, 67.0f - (1.0f - hat) * 1.4f, 8.0f, 1.3f, RGB(196, 150, 70));   /* the hat, closing on 2 and 4 */
    ellipse(178.0f, 69.0f, 8.0f, 1.3f, RGB(196, 150, 70));
    ellipse(229.0f, 58.0f, 12.0f, 2.6f, RGB(190, 146, 70));
    ellipse(206.0f, 90.0f, 11.0f, 11.0f, shell); /* the bass drum, its head and its painted mark */
    ellipse(206.0f, 90.0f, 9.5f, 9.5f, shade(214, 196, 164, 1.0f + 0.25f * k));
    ellipse(206.0f, 90.0f, 4.5f, 4.5f, ROOM_WALL[ui.bg_scene & 3][0] > 110 ? RGB(150, 52, 36) : RGB(60, 90, 110));
    cv_rect(184, 78, 18, 5, metal);              /* the snare */
    ellipse(193.0f, 77.0f, 9.0f, 2.0f, shade(220, 214, 200, 1.0f + 0.3f * sn));
    rod(212.0f, 58.0f, bx + 4.0f, by - 2.0f, 1.6f, suit, 999.0f);   /* the arms, the brushes on the head */
    rod(201.0f, 58.0f, lx + 5.0f, ly + 2.0f, 1.6f, suit, 999.0f);
    disc(bx + 4.0f, by - 2.0f, 1.9f, skin);
    disc(lx + 5.0f, ly + 2.0f, 1.9f, skin);
    rod(bx + 4.0f, by - 2.0f, bx - 1.0f, by, 0.5f, metal, 999.0f);
    rod(lx + 5.0f, ly + 2.0f, lx, ly + 4.0f, 0.5f, metal, 999.0f);
}

/* the crowd in front: heads and shoulders against the light, a table with glasses; hands up when they clap */
static void draw_crowd(void)
{
    static const int16_t P[7][3] = {{12, 116, 7}, {40, 120, 8}, {70, 114, 7}, {128, 121, 8}, {160, 115, 7},
                                    {196, 119, 8}, {228, 114, 7}};
    uint16_t sil = RGB(12, 8, 8), rim = RGB(96, 62, 38);
    int i;
    for (i = 0; i < 7; i++) {
        float px = (float)P[i][0], py = (float)P[i][1], r = (float)P[i][2], nod = 0.6f * bob(1.0f + 0.1f * (float)i);
        if (ui.clap > 0.1f && (i % 3) * 0.3f < ui.clap) {   /* clapping: two hands up, meeting */
            float up = fm_sinf(ui.t * (10.0f + (float)i) + (float)i);
            disc(px - 3.0f - 2.0f * up, py - r - 4.0f, 1.8f, rim);
            disc(px + 3.0f + 2.0f * up, py - r - 4.0f, 1.8f, rim);
        }
        cv_round((int32_t)(px - r * 1.8f), (int32_t)(py + r * 0.6f), (int32_t)(r * 3.6f), 20, 6, rim);
        cv_round((int32_t)(px - r * 1.8f), (int32_t)(py + r * 0.6f) + 1, (int32_t)(r * 3.6f), 20, 6, sil);
        disc(px, py - 0.7f + nod, r, rim);
        disc(px, py + nod, r, sil);
    }
    cv_rect(90, 121, 26, FORM_Y - 121, RGB(54, 34, 22));   /* a table's edge, two glasses on it */
    cv_rect(90, 121, 26, 1, RGB(110, 74, 44));
    cv_rect(95, 113, 5, 8, RGB(130, 120, 96));
    cv_rect(96, 116, 3, 5, RGB(176, 110, 40));
    cv_rect(105, 111, 4, 10, RGB(60, 90, 60));   /* a bottle */
    cv_rect(106, 108, 2, 3, RGB(60, 90, 60));
    px_blend(96, 114, RGB(255, 240, 210), 12u);
}

/* the bulb: on its cord, swaying a little when the room gets going; its light */
static void draw_bulb(void)
{
    const uint8_t *L = ROOM_BULB[ui.bg_scene & 3];
    float sw = ui.bulb, bx = BULB_X + CORD * sw, by = (float)BEAM_Y + 3.0f + CORD * (1.0f - 0.5f * sw * sw);
    rod(BULB_X, (float)BEAM_Y + 3.0f, bx, by - 3.0f, 0.5f, RGB(20, 16, 12), 999.0f);
    cv_rect((int32_t)bx - 1, (int32_t)by - 4, 3, 3, RGB(90, 84, 70));
    glow(bx, by + 1.0f, 30.0f, 0.3f, (float)L[0], (float)L[1] * 2.0f, (float)L[2]);
    ellipse(bx, by + 1.5f, 2.6f, 3.2f, RGB(255, 236, 190));
    disc(bx, by + 1.0f, 1.2f, RGB(255, 255, 240));
}

/* the title, its kind, key and tempo; where the band is and the time on the right */
static void draw_top(void)
{
    char t[40], *e;
    int32_t w;
    en_title(en_track_seed, en_kind, t);
    if (t[0] >= 'a' && t[0] <= 'z')
        t[0] = (char)(t[0] - 32);
    cv_text(6, 0, &FONT_S, t, en_playing ? K_TEXT : K_DIM);
    e = cat_s(t, en_kind_name(en_kind));
    e = cat_s(e, " in ");
    cat_s(e, EN_NOTE_NAME[en_key % 12]);
    cv_text(6, 16, &FONT_XS, t, K_DIM);
    if (en_phase == PH_GAP)
        cat_s(t, "next");
    else
        cat_time(t, en_track_ms);
    w = text_w(&FONT_XS, t);
    cv_text(234 - w, 16, &FONT_XS, t, en_phase == PH_GAP ? K_ACC : K_DIM);
    if (en_phase != PH_GAP && en_playing) {      /* the section, the chorus */
        e = cat_s(t, en_section_name(en_section));
        if (en_section != S_COUNT && en_section != S_INTRO && en_section != S_END && en_section != S_BREAK &&
            en_chorus) {
            e = cat_s(e, " ");
            e = cat_u(e, en_chorus);
            e = cat_s(e, "/");
            cat_u(e, en_nchorus);
        }
        caps(t, t, sizeof t);
        cv_text(234 - w - 8 - text_w(&FONT_XS, t), 16, &FONT_XS, t, K_ACC);
    }
}

/* the form along the bottom: a box a bar with its chord, the bar the band is in lit */
static void draw_form(void)
{
    int n = en_form_len, i, now = en_phase == PH_BODY || en_phase == PH_END ? en_bar : -1;
    int32_t bw;
    cv_rect(0, FORM_Y, 240, MAIN_H - FORM_Y, K_BG);
    if (n < 1 || n > 12)
        return;
    bw = 240 / n;
    for (i = 0; i < n; i++) {
        char c[10];
        int32_t x = (240 - bw * n) / 2 + i * bw;
        int lit = i == now && en_playing;
        uint16_t box = lit ? K_ACC : i == now ? K_LINE : K_PANEL;
        cv_round(x + 1, FORM_Y + 1, bw - 2, MAIN_H - FORM_Y - 2, 3, box);
        en_chord_name((en_key + en_form_root[i] + 12) % 12, en_form_q[i], c);
        if (text_w(&FONT_XS, c) > bw - 2) {      /* too wide: the root and the quality's first sign ("Em", "B7") */
            int r = c[1] == '#' || c[1] == 'b' ? 2 : 1;
            c[r + 1] = 0;
            if (text_w(&FONT_XS, c) > bw - 2)
                c[r] = 0;
        }
        text_c(x + bw / 2, FORM_Y + 2, &FONT_XS, c, lit ? K_BG : en_playing ? K_TEXT : K_DIM);
    }
}

static void draw_scene(void)
{
    if (ui.bg_scene != en_style_scene(proj.style))
        bg_build(en_style_scene(proj.style));
    cv_begin(240, MAIN_H, K_BG);
    memcpy(cv_px, bg_px, sizeof bg_px);
    draw_harp();
    memcpy(&cv_px[240u * FLOOR_Y], &bg_px[240u * FLOOR_Y], 240u * 2u * (EDGE_Y - FLOOR_Y));   /* (up from the front) */
    draw_piano();
    draw_bass();
    draw_drums();
    draw_bulb();
    draw_crowd();
    draw_top();
    draw_form();
}

/* the scene's state, eased toward the band's: hands follow the notes, the guest steps up and down */
static void scene_tick(float dt)
{
    float e;
    int i;
    ui.t += dt;
    if (ui.t > 3600.0f)
        ui.t -= 3600.0f;
    for (i = 0; i < NHITS; i++) {
        uint8_t h = en_hits[i];
        if (h != ui.hits[i]) {
            ui.hits[i] = h;
            ui.hv[i] = 1.0f;
        } else {
            ui.hv[i] = fm_maxf(0.0f, ui.hv[i] - dt * 7.0f);
        }
    }
    e = fm_minf(dt * 12.0f, 1.0f);
    ui.rh_x += (fm_clampf(36.0f + ((float)en_rh_note - 66.0f) * 0.35f, 32.0f, 45.0f) - ui.rh_x) * e;
    ui.neck_y += (fm_clampf(34.0f + ((float)en_bass_note - 28.0f) * 1.1f, 34.0f, 62.0f) - ui.neck_y) * e;
    ui.clap += (fm_clampf(en_applause * 1.6f, 0.0f, 1.0f) - ui.clap) * fm_minf(dt * 4.0f, 1.0f);
    ui.harp_a += ((en_harp_in ? 1.0f : 0.0f) - ui.harp_a) * fm_minf(dt * 1.5f, 1.0f);
    ui.bulb = (0.04f + 0.05f * fm_maxf(ui.hv[HIT_KICK], ui.clap)) * fm_sinf(ui.t * 1.3f);
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
    case B_HOME: L("THE BAND"); L("TEMPO, HARP, DRIVE, ROOM"); break;
    case B_ENV: L("THE MIX"); L("PIANO, BASS, DRUMS,"); L(" AMBIENCE"); break;
    case B_GLO: L("THE SETUP"); L("AGE, WIDTH, LENGTH, LIGHTS"); break;
    case B_EDIT: L("THE NEXT TUNE:"); L("THE BAND STOPS,"); L(" COUNTS OFF ANOTHER"); break;
    case B_REC: L("REPEAT: THE SOLOS"); L(" GO ROUND AGAIN"); L("AGAIN: THE HEAD OUT"); break;
    case B_OCTDN: L("THE KEYS AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE KEYS AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES EVERY NIGHT"); L("KEEP HOLDING: THE NIGHT"); L(" BACK TO ITS RECIPE"); break;
    case B_SEL: L("SELECT TURNS THE PAGES"); L("PRESETS: THE NIGHT"); L("ALGORITHM: HARP"); break;
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
        text_c(120, 50 + k * 20, &FONT_S, l[k] + (l[k][0] == ' '), K_TEXT);
}

static void draw_main(void)
{
    uint32_t now = plat_ms(), h;
    float dt = (float)(now - ui.draw_t) * 0.001f;
    if (ui.frame % 3u)                           /* 20 frames a second */
        return;
    ui.draw_t = now;
    if (dt > 0.2f)
        dt = 0.2f;
    scene_tick(dt);
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
    draw_scene();                                /* (always moving: the bulb, the hands) */
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
        uint16_t bg = hot ? K_PANEL : K_BG, fill = hot ? K_ACC : mix(K_ACC, K_RED, 6);
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

/* the lights: the page's button, PLAY, REPEAT; the chord's notes on the keys */
static void leds(void)
{
    uint32_t b = 0, k = 0, i, pcs = en_chord_pcs;
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (en_repeat_on)
        b |= 1u << B_REC;
    if (proj.leds && en_playing && en_phase != PH_GAP)
        for (i = 0; i < NKEYS; i++)
            if (pcs >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    plat_glow(proj.leds == 1);
    plat_leds(b, k | ui.keys);
}

/* AUTOSAVE (the flash wears, and a write can glitch the sound): the settings once the knobs have rested 10 s, at
 * most every 5 minutes; where the set is (the tune) at most hourly. Both at a quiet moment: a tune's ending or the
 * break after it, or resting. SAVE saves at once */
#define AUTOSAVE_REST 10000u
#define AUTOSAVE_SETTINGS 300000u
#define AUTOSAVE_TRACK 3600000u
static void autosave(void)
{
    uint32_t now = plat_ms(), since = now - ui.saved_t;
    int quiet = !en_playing || en_phase == PH_GAP || en_phase == PH_END, want;
    if (!quiet || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_REST)
        return;
    want = (ui.dirty && since >= AUTOSAVE_SETTINGS) || (en_track_no != ui.saved_track && since >= AUTOSAVE_TRACK);
    if (!want)
        return;
    if (project_save() == 0) {
        ui.dirty = 0;
        ui.saved_track = en_track_no;
    }
    ui.saved_t = now;                            /* (a failed save waits its turn too) */
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader (announce.c; fm1-reader): a page once it settles, with its knobs; a
 * knob as it turns; a help card as it comes up; each new tune (its title, kind, key, tempo); the harp player
 * stepping up; playing and paused; what a button did (say). WHERE is the whole screen in two: the page and the
 * header, then the tune and where the band is in it. */
static struct {
    uint8_t view, help, started, harp;
    int16_t val[4];
    uint32_t track;
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
    sp_cat(". Night ");
    sp_cat(en_style_name(proj.style));
    sp_cat(sp_tp.playing ? ", playing" : ", paused");
    if (en_repeat_on)
        sp_cat(", repeat on");
    if (en_harp_in)
        sp_cat(", harp sitting in");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static void sp_track_text(void)                  /* "Tune 12, mojo hand blues. Shuffle in B, 112 BPM" */
{
    char t[32];
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("Tune ");
    sp_int(en_track_no);
    sp_cat(", ");
    en_title(en_track_seed, en_kind, t);
    sp_cat(t);
    sp_cat(". ");
    sp_cat(en_kind_name(en_kind));
    sp_cat(" in ");
    sp_cat(EN_NOTE_NAME[en_key % 12]);
    sp_cat(", ");
    sp_int(en_bpm);
    sp_cat(" BPM");
}
static void sp_record(void)                      /* WHERE's second part: the tune, where the band is, how far in */
{
    char t[12];
    sp_track_text();
    if (en_phase == PH_GAP) {
        sp_cat(". Between tunes");
        return;
    }
    sp_cat(". ");
    sp_cat(en_section_name(en_section));
    if (en_chorus && en_section != S_COUNT && en_section != S_INTRO && en_section != S_END && en_section != S_BREAK) {
        sp_cat(", chorus ");
        sp_int(en_chorus);
        sp_cat(" of ");
        sp_int(en_nchorus);
    }
    sp_cat(". ");
    cat_time(t, en_track_ms);
    sp_cat(t);
    sp_cat(" of ");
    cat_time(t, en_track_len_ms);
    sp_cat(t);
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
    sp_cat("NIGHT ");
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
    sp.track = en_track_no;
    sp.harp = en_harp_in;
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
    if (en_track_no != sp.track && en_phase != PH_GAP) {   /* a new tune: queued */
        sp.track = en_track_no;
        sp_track_text();
        ann_say(sp_buf, 0, 0);
    }
    if (en_harp_in != sp.harp) {                 /* the guest steps up, or sits down */
        sp.harp = en_harp_in;
        ann_say(en_harp_in ? "HARP SITTING IN" : "HARP SITS DOWN", 0, 0);
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card, title and message as a screen reader gets them (the "speakdump" command) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}, {"NEXT TUNE", 0},
                                         {"PAUSED", 0}, {"PLAYING", 0}, {"REPEAT", "ON"}, {"REPEAT", "OFF"},
                                         {"SAVED", 0}, {"KEYS OCTAVE", "-2"}, {"KEYS OCTAVE", "-1"},
                                         {"KEYS OCTAVE", "0"}, {"KEYS OCTAVE", "+1"}, {"KEYS OCTAVE", "+2"},
                                         {"HARP SITTING IN", 0}, {"HARP SITS DOWN", 0}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}, {"KEYS OCTAVE", "LOWEST"},
                                          {"KEYS OCTAVE", "HIGHEST"}};
    project_t keep = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty, keep_play = sp_tp.playing;
    uint32_t keep_no = en_track_no, keep_seed = en_track_seed;
    uint8_t keep_key = en_key, keep_rep = en_repeat_on, keep_kind = en_kind, keep_harp = en_harp_in;
    uint8_t keep_phase = en_phase, keep_sec = en_section, keep_ch = en_chorus, keep_nch = en_nchorus;
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
    for (x = 0; x < 10; x++) {                   /* WHERE: playing, paused, unsaved, repeat, the sections */
        ui.view = (uint8_t)(x % NVIEWS);
        sp_tp.playing = (uint8_t)(x & 1);
        ui.dirty = (uint8_t)(x >> 1 & 1);
        en_repeat_on = (uint8_t)(x == 5);
        en_harp_in = (uint8_t)(x % 3 == 1);
        en_key = (uint8_t)(x * 5 % 12);
        en_kind = (uint8_t)(x % K_N);
        en_phase = (uint8_t)(x == 9 ? PH_GAP : PH_BODY);
        en_section = (uint8_t)(x % S_N);
        en_chorus = (uint8_t)(1 + x % 4);
        en_nchorus = 5;
        proj.style = (uint8_t)(x * 3 % NSTYLES);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_record();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
    for (x = 0; x < 300; x++) {                  /* tunes: their titles, kinds, keys and tempos */
        en_track_no = (uint32_t)x + 1u;
        en_track_seed = en_track_rand(0x4A4B4A54u, (uint32_t)x + 1u);
        en_key = (uint8_t)(x % 12);
        en_kind = (uint8_t)(x / 5 % K_N);
        sp_track_text();
        sp_print("TRACK", sp_buf);
    }
    en_track_no = keep_no;
    en_track_seed = keep_seed;
    en_key = keep_key;
    en_kind = keep_kind;
    en_harp_in = keep_harp;
    en_phase = keep_phase;
    en_section = keep_sec;
    en_chorus = keep_ch;
    en_nchorus = keep_nch;
    en_repeat_on = keep_rep;
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
    for (x = 0; x <= 100; x += 5) {
        char o[12];
        en_param_text(P_HARP, x, o);
        msg_text("HARP", o, t);
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
    ui.bg_scene = -1;
    ui.rh_x = 38.0f;
    ui.neck_y = 44.0f;
    ui.saved_t = plat_ms();
    ui.saved_track = proj.track_no + 1u;          /* the tune the boot starts: nothing new to keep yet */
    ui.act_t = plat_ms();
    ui.draw_t = plat_ms();
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
