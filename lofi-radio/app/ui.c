/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * LOFI RADIO: ashtonwallen, 2026 */
/* LOFI RADIO's UI: the panel and the screen (from AMB-1's: its knobs, help cards, messages and screen reader).
 * Part of the unity build (after gfx.c and project.c); the host simulator includes it too.
 *
 *   PLAY           the needle down / up             EDIT   the next track (a short fade, the run-out)
 *   REC            REPEAT: the track plays on        OCT- / OCT+   the keys' octave
 *   PRESETS        the style (each kept as left)     ALGORITHM     Tone
 *   HOME ENV GLO   the pages: Radio, Mix, Room (SELECT steps through them)
 *   KNOB 1-4       the page's four values
 *   keys           play along on the electric piano
 *   SAVE           saves (it also saves by itself at a quiet moment: see autosave); held 1.5 s: the style back to
 *                  its recipe */
#include "fastmath.h"

enum { V_RADIO, V_MIX, V_ROOM, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Radio", "Mix", "Room"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_GLO};
enum { K_LEDS = 100, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_SWING, P_DUST, P_TEMPO, P_MOOD},
    {P_KEYS, P_DRUMS, P_BASS, P_MELODY},
    {P_TONE, P_RAIN, P_LENGTH, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Swing", "Dust", "Tempo", "Mood"},
    {"Keys", "Drums", "Bass", "Melody"},
    {"Tone", "Rain", "Length", "Lights"},
};

/* the screen's colours: a room at night, a lamp on: plum dark, cream, a warm orange; the turntable in walnut */
#define K_BG RGB(30, 24, 36)
#define K_PANEL RGB(52, 41, 58)
#define K_LINE RGB(78, 64, 84)
#define K_DIM RGB(156, 134, 146)
#define K_TEXT RGB(246, 230, 206)
#define K_ACC RGB(242, 160, 96)
#define K_PINK RGB(236, 128, 140)
#define K_WOOD RGB(92, 61, 44)
#define K_WOOD_HI RGB(128, 88, 62)
#define K_WOOD_LO RGB(72, 47, 34)
#define K_METAL RGB(196, 192, 188)
#define K_METAL_LO RGB(112, 108, 108)
#define K_FACE RGB(240, 222, 180)
#define K_INK RGB(52, 40, 34)
#define K_RED RGB(206, 72, 56)
#define K_LOW RGB(210, 92, 64)                 /* the battery at one bar or none */
/* the records' labels */
static const uint8_t LABEL_RGB[12][3] = {
    {226, 98, 76}, {232, 180, 72}, {72, 160, 156}, {150, 180, 120}, {222, 142, 150}, {160, 140, 200},
    {236, 220, 180}, {110, 160, 210}, {236, 140, 70}, {170, 90, 130}, {130, 200, 170}, {180, 84, 64},
};

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
    uint32_t saved_track;              /* the track the last save kept */
    char msg[2][28];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    uint32_t keys_on;                  /* the keys sounding (played along), as MIDI notes, key bit */
    float rot, spin;                   /* the platter: its angle (turns), its speed (0..1) */
    float arm;                         /* the tonearm's angle as drawn */
    float vu;                          /* the VU's needle as drawn */
    uint32_t draw_t;
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
static void set_style(int i)                    /* the style playing kept as it is; style i as it was left */
{
    project_style(i);
    project_apply();
    en_skip();                                   /* on to a new record in the new style */
    if (!en_playing)
        en_play(1);
    sp_tp.playing = 1;
    say_as("STYLE", en_style_name(proj.style), 0);
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
        say("NEXT TRACK", 0);
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

/* MIDI in (USB and the TRS jack): notes play along on the electric piano */
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
    if ((e = plat_enc(EN_ALGO)) != 0) {          /* ALGORITHM: Tone, from any page */
        char t[12];
        turn(EN_ALGO, P_TONE, e);
        en_param_text(P_TONE, proj.par[P_TONE], t);
        if (ui.view == V_ROOM) {
            ui.touched = 0;
            ui.touch_until = plat_ms() + 1200u;
        } else {
            say("TONE", t);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        set_style(proj.style + (e > 0 ? 1 : -1));
        ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && plat_ms() - ui.down_t[B_SAVE] > RESET_HOLD) {
        ui.reset_done = 1;                       /* held: the style back to its recipe (not saved) */
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
/* three bands: the header (28 px), the turntable (144), the knobs (68) */
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

/* the header: the style in a tag; on the right the battery, the tempo with a beat light, REPEAT, unsaved */
static void draw_header(void)
{
    char t[16];
    int msg = plat_ms() < ui.msg_until, beat = (int)(en_beats & 1u);
    uint32_t h = hash(hash(hash(2166136261u, proj.style | (uint32_t)en_bpm << 8 | (uint32_t)en_playing << 16 |
                                                   (uint32_t)en_repeat_on << 17 | (uint32_t)ui.dirty << 18 |
                                                   (uint32_t)msg << 19 | (uint32_t)beat << 20),
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
        cat_s(cat_u(t, en_bpm), "");
        x -= text_w(&FONT_XS, "BPM");
        cv_text(x, 8, &FONT_XS, "BPM", K_DIM);
        x -= text_w(&FONT_S, t) + 3;
        cv_text(x, 6, &FONT_S, t, en_playing ? K_TEXT : K_DIM);
        x -= 14;
        if (en_playing) {                        /* the beat light */
            cv_round(x, 10, 8, 8, 4, beat ? K_ACC : K_LINE);
        } else {                                 /* paused: two bars */
            cv_rect(x, 9, 3, 10, K_DIM);
            cv_rect(x + 5, 9, 3, 10, K_DIM);
        }
        if (en_repeat_on) {
            x -= text_w(&FONT_XS, "REPEAT") + 14;
            cv_round(x, 7, text_w(&FONT_XS, "REPEAT") + 8, 15, 5, K_PINK);
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

/* ---- the turntable: the record from two tables (each pixel's radius in quarter pixels, its angle in 256ths) */
#define REC_R 62
#define REC_CX 76
#define REC_CY 72
#define RIM_R 65
#define REC_BOX 136                      /* the record's square (and the platter's rim), centred */
static uint8_t rec_r4[REC_BOX * REC_BOX] PLAT_POOL, rec_ang[REC_BOX * REC_BOX] PLAT_POOL;
static uint8_t sheen_tab[256];
static void rec_tables(void)
{
    int x, y;
    for (y = 0; y < REC_BOX; y++)
        for (x = 0; x < REC_BOX; x++) {
            float dx = (float)x - 67.5f, dy = (float)y - 67.5f, d = fm_sqrtf(dx * dx + dy * dy) * 4.0f;
            rec_r4[y * REC_BOX + x] = (uint8_t)(d > 255.0f ? 255.0f : d);
            rec_ang[y * REC_BOX + x] = (uint8_t)((int)(turns(dy, dx) * 256.0f) & 255);
        }
    for (x = 0; x < 256; x++) {                  /* the lamp's reflection: two soft wedges, still as the record turns */
        float a = (float)x * (FM_TWO_PI / 256.0f), c = fm_cosf(2.0f * (a - 0.6f));
        c = c > 0.0f ? c * c * c * c : 0.0f;
        sheen_tab[x] = (uint8_t)(c * 9.0f + 0.5f);
    }
}

static uint16_t label_c(int k, int shade)       /* the label's colour, darker by shade (0..16) */
{
    const uint8_t *c = LABEL_RGB[k % 12];
    return mix(RGB(c[0], c[1], c[2]), RGB(20, 14, 22), shade);
}

static void draw_record(void)
{
    static const uint16_t VIN[2] = {RGB(24, 21, 27), RGB(32, 28, 36)};
    uint16_t vin[2][10], lab[4], hole = K_METAL, bgc = K_WOOD, rim[2] = {RGB(88, 84, 88), RGB(40, 36, 42)};
    int lc = en_label_colour(en_track_seed), i, x, y, rot = (int)(ui.rot * 256.0f) & 255;
    int ox = REC_CX - REC_BOX / 2 + 1, oy = REC_CY - REC_BOX / 2 + 1;
    for (i = 0; i < 10; i++) {
        vin[0][i] = mix(VIN[0], RGB(150, 140, 160), i);
        vin[1][i] = mix(VIN[1], RGB(150, 140, 160), i);
    }
    lab[0] = label_c(lc, 0);
    lab[1] = label_c(lc, 4);
    lab[2] = label_c(lc, 8);
    lab[3] = mix(label_c(lc, 0), K_TEXT, 8);
    for (y = 0; y < REC_BOX; y++) {
        uint16_t *row;
        int sy = oy + y + cv_oy;
        if (sy < cv_y0 || sy >= cv_y1)
            continue;
        row = &cv_px[(uint32_t)sy * cv_w];
        for (x = 0; x < REC_BOX; x++) {
            int r4 = rec_r4[y * REC_BOX + x], a = rec_ang[y * REC_BOX + x], sx = ox + x;
            uint16_t c;
            if (r4 >= RIM_R * 4 + 2 || (uint32_t)sx >= cv_w)
                continue;
            if (r4 >= REC_R * 4 + 2) {           /* the platter's rim: a lit edge, its shadow outside */
                c = r4 >= RIM_R * 4 - 2 ? mix(rim[1], bgc, (r4 - (RIM_R * 4 - 2)) * 4) : r4 >= REC_R * 4 + 6 ? rim[0] : rim[1];
            } else if (r4 < 9) {                        /* the spindle */
                c = hole;
            } else if (r4 < 80) {                /* the label: turning; two lines of print, a ring, a mark */
                int ra = (a - rot) & 255;
                c = lab[0];
                if (r4 >= 70)
                    c = lab[1];
                else if ((r4 >= 36 && r4 < 46 && ra > 150 && ra < 230) || (r4 >= 50 && r4 < 58 && ra > 160 && ra < 220))
                    c = lab[2];
                else if (r4 >= 52 && r4 < 64 && ra > 20 && ra < 44)
                    c = lab[3];
                else if (r4 < 14)
                    c = lab[1];
            } else {                             /* the grooves, the gaps between the songs, the lamp's sheen */
                int g = (r4 >> 1) & 1, s = sheen_tab[a];
                if (r4 < 92 || (r4 > 150 && r4 < 156) || (r4 > 196 && r4 < 201) || r4 > 238)
                    g = 0, s += 1;
                c = vin[g][s > 9 ? 9 : s];
                if (r4 >= REC_R * 4 - 2)         /* the edge, softened */
                    c = mix(c, rim[0], (r4 - (REC_R * 4 - 2)) * 4);
            }
            row[sx] = swap16(c);
        }
    }
}

/* the tonearm: from its pivot to the needle. Playing, the needle crosses the record as the track goes (outer to
 * inner groove); lifted, it swings back to its rest */
#define ARM_PX 142.0f
#define ARM_PY 16.0f
#define ARM_L 94.0f
static float arm_target(void)
{
    float p, rn, dx = (float)REC_CX - ARM_PX, dy = (float)REC_CY - ARM_PY, d = fm_sqrtf(dx * dx + dy * dy), a, h, mx,
             my, nx, ny, rest = turns(112.0f - ARM_PY, 150.0f - ARM_PX), play;
    uint32_t len = en_track_len_ms ? en_track_len_ms : 1u;
    p = en_phase == PH_RUNOUT ? 1.0f : (float)(en_repeat_on ? en_track_ms % len : en_track_ms) / (float)len;
    p = fm_clampf(p, 0.0f, 1.0f);
    rn = 57.0f - 32.0f * p;                      /* outer groove to inner */
    if (en_phase == PH_RUNOUT)
        rn = 23.0f;
    a = (ARM_L * ARM_L - rn * rn + d * d) / (2.0f * d);   /* the circles' meeting: the pivot's L, the record's rn */
    h = fm_sqrtf(fm_maxf(ARM_L * ARM_L - a * a, 0.0f));
    mx = ARM_PX + a * dx / d;
    my = ARM_PY + a * dy / d;
    nx = mx + h * dy / d;                        /* the meeting on the right of the line to the centre */
    ny = my - h * dx / d;
    play = turns(ny - ARM_PY, nx - ARM_PX);
    return rest + (play - rest) * fm_clampf(en_needle, 0.0f, 1.0f);
}
static void draw_arm(void)
{
    float a = ui.arm * FM_TWO_PI, c = fm_cosf(a), s = fm_sinf(a), ex = ARM_PX + c * (ARM_L - 12.0f),
          ey = ARM_PY + s * (ARM_L - 12.0f), hx, hy, h2 = a + 0.45f;
    disc(ARM_PX + 2.0f, ARM_PY + 3.0f, 10.0f, K_WOOD_LO);   /* the base's shadow, the base */
    disc(ARM_PX, ARM_PY, 9.5f, K_METAL_LO);
    disc(ARM_PX, ARM_PY, 6.5f, K_METAL);
    rod(ARM_PX - c * 16.0f, ARM_PY - s * 16.0f, ARM_PX - c * 7.0f, ARM_PY - s * 7.0f, 4.5f, K_METAL_LO, 999.0f);
    rod(ARM_PX + 2.0f, ARM_PY + 3.0f, ex + 2.0f, ey + 3.0f, 1.6f, RGB(14, 10, 16), 999.0f);   /* its shadow */
    rod(ARM_PX, ARM_PY, ex, ey, 1.7f, K_METAL, 999.0f);
    hx = ex + fm_cosf(h2) * 13.0f;               /* the headshell, angled */
    hy = ey + fm_sinf(h2) * 13.0f;
    rod(ex, ey, hx, hy, 2.6f, RGB(60, 56, 60), 999.0f);
    disc(hx, hy, 1.4f, K_ACC);
    disc(ARM_PX, ARM_PY, 2.5f, K_METAL_LO);
}

/* the VU meter: a cream face, its scale, a red zone, a needle with a little weight to it */
#define VU_X 160
#define VU_Y 98
#define VU_W 74
#define VU_H 42
static void draw_vu(void)
{
    float px = (float)VU_X + (float)VU_W * 0.5f, py = (float)(VU_Y + VU_H) + 22.0f, a, i;
    int k;
    cv_round(VU_X - 2, VU_Y - 2, VU_W + 4, VU_H + 4, 5, K_WOOD_LO);
    cv_round(VU_X, VU_Y, VU_W, VU_H, 4, K_FACE);
    for (k = 0; k <= 8; k++) {                   /* the scale: -20 .. +3, red from 0 */
        float t = (float)k / 8.0f, aa = (-0.13f + 0.26f * t) * FM_TWO_PI - FM_PI * 0.5f;
        float r0 = 52.0f, r1 = k % 2 ? 56.0f : 58.0f;
        rod(px + fm_cosf(aa) * r0, py + fm_sinf(aa) * r0, px + fm_cosf(aa) * r1, py + fm_sinf(aa) * r1, 0.6f,
            t > 0.74f ? K_RED : K_INK, 999.0f);
    }
    for (i = 0.0f; i <= 1.0f; i += 0.02f) {      /* the arc, red at the end */
        float aa = (-0.13f + 0.26f * i) * FM_TWO_PI - FM_PI * 0.5f;
        disc(px + fm_cosf(aa) * 52.0f, py + fm_sinf(aa) * 52.0f, i > 0.74f ? 1.1f : 0.6f, i > 0.74f ? K_RED : K_INK);
    }
    cv_text(VU_X + VU_W / 2 - text_w(&FONT_XS, "VU") / 2, VU_Y + VU_H - 16, &FONT_XS, "VU", K_INK);
    a = (-0.13f + 0.26f * fm_clampf(ui.vu, 0.0f, 1.05f)) * FM_TWO_PI - FM_PI * 0.5f;
    rod(px + fm_cosf(a) * 26.0f, py + fm_sinf(a) * 26.0f, px + fm_cosf(a) * 58.0f, py + fm_sinf(a) * 58.0f, 0.7f, K_INK,
        (float)(VU_Y + VU_H - 2));
    cv_rect(VU_X, VU_Y + VU_H - 4, VU_W, 1, mix(K_FACE, K_INK, 4));
}

/* the title, in up to two lines of w px: words wrapped */
static void draw_title(int32_t x, int32_t y, int32_t w, const char *s)
{
    char line[2][28];
    int ln = 0, n = 0, i = 0;
    line[0][0] = line[1][0] = 0;
    while (s[i] && ln < 2) {
        char word[24];
        int k = 0, keep_n = n;
        while (s[i] == ' ')
            i++;
        while (s[i] && s[i] != ' ' && k < 23)
            word[k++] = s[i++];
        word[k] = 0;
        if (!k)
            break;
        if (n) {
            line[ln][n++] = ' ';
            line[ln][n] = 0;
        }
        cat_s(&line[ln][n], word);
        if (text_w(&FONT_S, line[ln]) > w && keep_n) {   /* this word goes on the next line */
            line[ln][keep_n] = 0;
            if (++ln >= 2)
                break;
            cat_s(line[ln], word);
            n = k;
        } else {
            n += k;
        }
    }
    cv_text(x, y, &FONT_S, line[0], K_TEXT);
    if (line[1][0])
        cv_text(x, y + 17, &FONT_S, line[1], K_TEXT);
}

static void draw_side(void)                      /* the right: the track, its title, its time, the chord, the VU */
{
    char t[32], *e;
    uint32_t len = en_track_len_ms, ms = en_track_ms;
    int32_t x = 160, w;
    if (en_repeat_on && len)
        ms %= len;
    e = cat_u(cat_s(t, "TRACK "), en_track_no);
    (void)e;
    cv_text(x, 2, &FONT_XS, t, K_DIM);
    en_title(en_track_seed, t);
    draw_title(x, 15, 76, t);
    cat_time(t, en_phase == PH_RUNOUT ? len : ms);
    w = cv_text(x, 52, &FONT_B, t, K_ACC);
    cat_s(cat_time(cat_s(t, "/ "), len), "");
    cv_text(w + 4, 55, &FONT_XS, t, K_DIM);
    {                                            /* the way through it */
        int32_t f = len ? (int32_t)((float)(en_phase == PH_RUNOUT ? len : ms) / (float)len * 74.0f) : 0;
        cv_rect(x, 72, 74, 2, K_LINE);
        if (f > 0)
            cv_rect(x, 72, f > 74 ? 74 : f, 2, K_ACC);
    }
    {                                            /* the chord and the key */
        char c[16];
        e = cat_s(t, en_chord_name(en_chord_root, en_chord_q, c));
        e = cat_s(e, "  in ");
        e = cat_s(e, EN_NOTE_NAME[en_key % 12]);
        cat_s(e, en_minor ? "m" : "");
        if (text_w(&FONT_XS, t) > 78) {         /* a long chord ("C#maj#11"): the key without "in" */
            e = cat_s(cat_s(t, c), " / ");
            e = cat_s(e, EN_NOTE_NAME[en_key % 12]);
            cat_s(e, en_minor ? "m" : "");
        }
        if (text_w(&FONT_XS, t) > 78)           /* still too long: the chord alone */
            cat_s(t, c);
        cv_text(x, 78, &FONT_XS, t, K_DIM);
    }
    draw_vu();
}

static void draw_plinth(void)
{
    int y;
    cv_round(4, 2, 150, 140, 6, K_WOOD_LO);
    cv_round(4, 2, 150, 138, 6, K_WOOD);
    cv_rect(10, 2, 138, 1, K_WOOD_HI);
    for (y = 6; y < 138; y += 3) {               /* the grain: faint streaks */
        uint32_t h = hash(2166136261u, (uint32_t)y);
        int32_t x0 = 8 + (int32_t)(h % 60u), w = 20 + (int32_t)((h >> 8) % 70u);
        if ((h >> 16) % 3u == 0u)
            cv_rect(x0, y, w, 1, mix(K_WOOD, K_WOOD_LO, 8));
        else if ((h >> 16) % 3u == 1u)
            cv_rect(x0 + 30, y, w / 2, 1, mix(K_WOOD, K_WOOD_HI, 6));
    }
    disc(147.0f, 128.0f, 3.0f, en_playing ? K_ACC : K_WOOD_LO);   /* the pilot light */
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
    case B_HOME: L("THE RADIO"); L("SWING, DUST, TEMPO, MOOD"); break;
    case B_ENV: L("THE MIX"); L("KEYS, DRUMS, BASS, MELODY"); break;
    case B_GLO: L("THE ROOM"); L("TONE, RAIN, LENGTH, LIGHTS"); break;
    case B_EDIT: L("THE NEXT TRACK:"); L("A NEW KEY AND GROOVE,"); L("NEW GUEST PLAYERS"); break;
    case B_REC: L("REPEAT: THIS TRACK"); L(" PLAYS ON"); L("AGAIN: ON TO THE NEXT"); break;
    case B_OCTDN: L("THE KEYS AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE KEYS AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES EVERY STYLE"); L("KEEP HOLDING: THE STYLE"); L(" BACK TO ITS RECIPE"); break;
    case B_SEL: L("SELECT TURNS THE PAGES"); L("PRESETS: THE STYLE"); L("ALGORITHM: TONE"); break;
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

static float vu_target(void)                     /* the VU's reading: -20 .. +3 VU on 0 .. 1 */
{
    float v = fm_maxf(en_vu_l, en_vu_r), db = fm_log2f(fm_maxf(v, 1e-4f)) * 6.0206f;
    return fm_clampf((db + 13.0f) / 14.0f, 0.0f, 1.05f);
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
    ui.spin += ((en_playing ? 1.0f : 0.0f) - ui.spin) * fm_minf(dt * 1.5f, 1.0f);   /* the platter spins up, down */
    ui.rot += ui.spin * 0.5556f * dt;            /* 33 1/3 turns a minute */
    if (ui.rot >= 1.0f)
        ui.rot -= 1.0f;
    {
        float t = arm_target();
        ui.arm += (t - ui.arm) * fm_minf(dt * 3.0f, 1.0f);
    }
    ui.vu += (vu_target() - ui.vu) * fm_minf(dt * 9.0f, 1.0f);
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
    h = hash(hash(hash(2166136261u, (uint32_t)(ui.rot * 256.0f) | (uint32_t)(ui.arm * 4000.0f) << 8),
                  (uint32_t)(ui.vu * 200.0f) | en_chord_root << 8 | (uint32_t)en_chord_q << 12 | en_phase << 16),
             en_track_seed ^ en_track_ms / 1000u);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    draw_plinth();
    draw_record();
    draw_arm();
    draw_side();
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
        uint16_t bg = hot ? K_PANEL : K_BG, fill = hot ? K_ACC : mix(K_ACC, K_PINK, 10);
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
    if (proj.leds && en_playing)
        for (i = 0; i < NKEYS; i++)
            if (pcs >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    plat_glow(proj.leds == 1);
    plat_leds(b, k | ui.keys);
}

/* AUTOSAVE (the flash wears, and a write can glitch the sound): the settings once the knobs have rested 10 s, at
 * most every 5 minutes; where the radio is (the track) at most hourly. Both at a quiet moment: the run-out between
 * tracks, or paused. SAVE saves at once */
#define AUTOSAVE_REST 10000u
#define AUTOSAVE_SETTINGS 300000u
#define AUTOSAVE_TRACK 3600000u
static void autosave(void)
{
    uint32_t now = plat_ms(), since = now - ui.saved_t;
    int quiet = !en_playing || en_phase == PH_RUNOUT, want;
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
 * knob as it turns; a help card as it comes up; each new track (its title, key, tempo); playing and paused; what
 * a button did (say). WHERE is the whole screen in two: the page and the header, then the record. */
static struct {
    uint8_t view, help, started;
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
static void sp_key(void)                         /* "F minor" */
{
    sp_cat(EN_NOTE_NAME[en_key % 12]);
    sp_cat(en_minor ? " minor" : " major");
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
    sp_cat(". Style ");
    sp_cat(en_style_name(proj.style));
    sp_cat(sp_tp.playing ? ", playing" : ", paused");
    if (en_repeat_on)
        sp_cat(", repeat on");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static void sp_track_text(void)                  /* "Track 12, sleepy window. F minor, 78 BPM" */
{
    char t[28];
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("Track ");
    sp_int(en_track_no);
    sp_cat(", ");
    en_title(en_track_seed, t);
    sp_cat(t);
    sp_cat(". ");
    sp_key();
    sp_cat(", ");
    sp_int(en_bpm);
    sp_cat(" BPM");
}
static void sp_record(void)                      /* WHERE's second part: the track, how far in */
{
    char t[12];
    sp_track_text();
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
    sp_cat("STYLE ");
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
    if (en_track_no != sp.track && en_phase != PH_RUNOUT) {   /* a new track: queued */
        sp.track = en_track_no;
        sp_track_text();
        ann_say(sp_buf, 0, 0);
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card, title and message as a screen reader gets them (the "speakdump" command) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}, {"NEXT TRACK", 0},
                                         {"PAUSED", 0}, {"PLAYING", 0}, {"REPEAT", "ON"}, {"REPEAT", "OFF"},
                                         {"SAVED", 0}, {"KEYS OCTAVE", "-2"}, {"KEYS OCTAVE", "-1"},
                                         {"KEYS OCTAVE", "0"}, {"KEYS OCTAVE", "+1"}, {"KEYS OCTAVE", "+2"}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}, {"KEYS OCTAVE", "LOWEST"},
                                          {"KEYS OCTAVE", "HIGHEST"}};
    project_t keep = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty, keep_play = sp_tp.playing;
    uint32_t keep_no = en_track_no, keep_seed = en_track_seed;
    uint8_t keep_key = en_key, keep_minor = en_minor, keep_rep = en_repeat_on;
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
    for (x = 0; x < 8; x++) {                    /* WHERE: playing, paused, unsaved, repeat, minor and major */
        ui.view = (uint8_t)(x % NVIEWS);
        sp_tp.playing = (uint8_t)(x & 1);
        ui.dirty = (uint8_t)(x >> 1 & 1);
        en_repeat_on = (uint8_t)(x == 5);
        en_key = (uint8_t)(x * 5 % 12);
        en_minor = (uint8_t)(x >> 2 & 1);
        proj.style = (uint8_t)(x * 3 % NSTYLES);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_record();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
    for (x = 0; x < 300; x++) {                  /* tracks: their titles, keys and tempos */
        en_track_no = (uint32_t)x + 1u;
        en_track_seed = en_track_rand(0x4C4F4649u, (uint32_t)x + 1u);
        en_key = (uint8_t)(x % 12);
        en_minor = (uint8_t)(x & 1);
        sp_track_text();
        sp_print("TRACK", sp_buf);
    }
    en_track_no = keep_no;
    en_track_seed = keep_seed;
    en_key = keep_key;
    en_minor = keep_minor;
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
    for (x = 0; x <= 100; x += 10) {
        char o[12];
        en_param_text(P_TONE, x, o);
        msg_text("TONE", o, t);
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
    ui.saved_track = proj.track_no + 1u;          /* the track the boot starts: nothing new to keep yet */
    ui.act_t = plat_ms();
    rec_tables();
    ui.arm = turns(112.0f - ARM_PY, 150.0f - ARM_PX);
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
