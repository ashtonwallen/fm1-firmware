/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * BOSSA LOUNGE: ashtonwallen, 2026 */
/* BOSSA LOUNGE's UI: the panel and the screen (from AMB-1's: its knobs, help cards, messages and screen reader).
 * Part of the unity build (after gfx.c and project.c); the host simulator includes it too.
 *
 *   PLAY           the tape rolls / stops            EDIT   the next tune (a short fade, a new one)
 *   REC            REPEAT: the tune plays on         OCT- / OCT+   the keys' octave
 *   PRESETS        the style (each kept as left)     ALGORITHM     Tone
 *   HOME ENV GLO   the pages: Tune, Combo, Room (SELECT steps through them)
 *   KNOB 1-4       the page's four values
 *   keys           play along on the vibraphone
 *   SAVE           saves (it also saves by itself at a quiet moment: see autosave); held 1.5 s: the style back to
 *                  its recipe */
#include "fastmath.h"

enum { V_TUNE, V_COMBO, V_ROOM, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Tune", "Combo", "Room"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_GLO};
enum { K_LEDS = 100, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_TEMPO, P_FEEL, P_MOOD, P_MELODY},
    {P_GUITAR, P_BASS, P_DRUMS, P_LEAD},
    {P_TONE, P_REVERB, P_LENGTH, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Tempo", "Feel", "Mood", "Melody"},
    {"Guitar", "Bass", "Drums", "Lead"},
    {"Tone", "Reverb", "Length", "Lights"},
};

/* the screen's colours: a dim bar: a wine-dark wall, a deck in black and brushed steel, amber light */
#define K_BG RGB(20, 14, 20)
#define K_PANEL RGB(50, 34, 46)
#define K_LINE RGB(78, 58, 72)
#define K_DIM RGB(164, 138, 140)
#define K_TEXT RGB(248, 232, 210)
#define K_ACC RGB(246, 174, 86)                /* amber */
#define K_PINK RGB(232, 110, 150)
#define K_WALL RGB(48, 20, 31)
#define K_WALL_HI RGB(57, 25, 37)
#define K_WOOD RGB(88, 50, 32)
#define K_WOOD_HI RGB(132, 80, 50)
#define K_DECK RGB(30, 28, 33)
#define K_DECK_HI RGB(66, 62, 70)
#define K_STEEL RGB(200, 198, 204)
#define K_STEEL_LO RGB(112, 108, 118)
#define K_TAPE RGB(64, 40, 28)
#define K_TAPE_HI RGB(96, 62, 42)
#define K_FACE RGB(252, 202, 118)              /* the VUs' faces, lit from behind */
#define K_INK RGB(54, 34, 22)
#define K_RED RGB(206, 60, 48)
#define K_LCD RGB(12, 8, 12)
#define K_LOW RGB(210, 92, 64)                 /* the battery at one bar or none */
/* the neon sign's tubes, a colour for each style (en_style_neon) */
static const uint8_t NEON_RGB[8][3] = {
    {255, 84, 172}, {64, 228, 222}, {255, 168, 64}, {184, 116, 255},
    {255, 76, 76},  {88, 164, 255}, {120, 240, 150}, {255, 226, 196},
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
    uint32_t saved_tune;               /* the tune the last save kept */
    char msg[2][28];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    uint32_t keys_on;                  /* the keys sounding (played along), as MIDI notes, key bit */
    float ang[2];                      /* the reels' angles (turns) */
    float tp;                          /* the tape as drawn: 0 at the tune's start .. 1 at its end */
    float vu[2];                       /* the VUs' needles as drawn */
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
    en_skip();                                   /* on to a new tune in the new style */
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
            ui.saved_tune = en_tune_no;
            say("SAVED", 0);
        } else {
            warn("NOT SAVED", "FLASH ERROR");
        }
        break;
    default: break;
    }
}

/* MIDI in (USB and the TRS jack): notes play along on the vibraphone */
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
/* three bands: the header (28 px), the hi-fi (144), the knobs (68) */
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

/* ---- the hi-fi in the bar. Above: the wall, a neon martini glass, the tune's title. Below: a reel-to-reel deck,
 * and beside it the amplifier: the chord in its window, two VU meters, the time */
#define WALL_H 58
#define NEON_CX 36.0f
#define NEON_CY 26.0f
#define GLOW_W 100
#define REEL_R 28
#define REEL_BOX 58
#define REEL_LX 38
#define REEL_RX 110
#define REEL_Y 92
#define PACK_HUB 10.0f
#define PACK_FULL 24.0f
static uint8_t glow_tab[WALL_H * GLOW_W] PLAT_POOL;   /* the sign's light on the wall: 0..8 */
static uint8_t reel_r4[REEL_BOX * REEL_BOX] PLAT_POOL, reel_ang[REEL_BOX * REEL_BOX] PLAT_POOL;
static uint8_t sheen_tab[256];
static void screen_tables(void)
{
    int x, y;
    for (y = 0; y < REEL_BOX; y++)
        for (x = 0; x < REEL_BOX; x++) {
            float dx = (float)x - 28.5f, dy = (float)y - 28.5f, d = fm_sqrtf(dx * dx + dy * dy) * 4.0f;
            reel_r4[y * REEL_BOX + x] = (uint8_t)(d > 255.0f ? 255.0f : d);
            reel_ang[y * REEL_BOX + x] = (uint8_t)((int)(turns(dy, dx) * 256.0f) & 255);
        }
    for (y = 0; y < WALL_H; y++)
        for (x = 0; x < GLOW_W; x++) {
            float dx = (float)x - NEON_CX, dy = ((float)y - NEON_CY) * 1.25f, d = fm_sqrtf(dx * dx + dy * dy);
            float g = 1.0f - d / 50.0f;
            glow_tab[y * GLOW_W + x] = (uint8_t)(g > 0.0f ? g * g * 8.0f + 0.5f : 0.0f);
        }
    for (x = 0; x < 256; x++) {                  /* the steel's sheen: the bar's light from above left, still */
        float a = (float)x * (FM_TWO_PI / 256.0f), c = fm_cosf(a + 2.2f);
        c = c > 0.0f ? c * c : 0.0f;
        sheen_tab[x] = (uint8_t)(c * 10.0f + 0.5f);
    }
}

static uint16_t neon_rgb(void)
{
    const uint8_t *n = NEON_RGB[en_style_neon(proj.style) & 7u];
    return RGB(n[0], n[1], n[2]);
}

/* the neon's brightness (0..1): lit steady; an accent (a rim click, a melody note) makes the tube buzz brighter
 * and now and then catch, a flicker; stopped, it dims */
static float neon_level(void)
{
    uint32_t f = hash(0x4E454F4Eu, ui.frame / 3u);
    float acc = fm_clampf(en_accent, 0.0f, 1.0f), b = 0.74f + 0.26f * acc;
    if (acc > 0.45f && (f & 7u) < 2u)
        b = 0.32f;
    else if ((f >> 8 & 255u) == 7u)              /* an old tube's hiccup, once in a while */
        b = 0.5f;
    if (!en_playing)
        b *= 0.6f;
    return b;
}

static void draw_wall(float b)
{
    uint16_t nc = neon_rgb(), col[2];
    int x, y, gq = q16(b);
    for (y = 0; y < WALL_H; y++) {
        uint16_t *row;
        int sy = y + cv_oy, dark = (WALL_H - y) / 5;
        if (sy < cv_y0 || sy >= cv_y1)
            continue;
        row = &cv_px[(uint32_t)sy * cv_w];
        col[0] = mix(K_WALL, K_BG, dark);       /* the wallpaper's stripes, darker towards the ceiling */
        col[1] = mix(K_WALL_HI, K_BG, dark);
        for (x = 0; x < 240; x++) {
            uint16_t c = col[(x / 7) & 1];
            if (x < GLOW_W) {
                int g = glow_tab[y * GLOW_W + x] * gq >> 4;
                if (g)
                    c = mix(c, nc, g);
            }
            row[x] = swap16(c);
        }
    }
    cv_rect(0, WALL_H - 4, 240, 1, K_WOOD_HI);   /* the shelf's lit edge, the shelf */
    cv_rect(0, WALL_H - 3, 240, 3, K_WOOD);
}

/* the sign: a martini glass in tubes, an olive on a pick; a halo, then the tube's hot core */
static void draw_neon(float b)
{
    static const int8_t SEG[][4] = {
        {16, 9, 56, 9}, {16, 9, 36, 31}, {56, 9, 36, 31}, {36, 31, 36, 45}, {26, 47, 46, 47},
        {22, 14, 50, 14}, {41, 19, 51, 4},
    };
    uint16_t nc = neon_rgb(), halo = mix(K_WALL, nc, q16(b * 0.5f)), core = mix(K_WALL, mix(nc, K_TEXT, 7), q16(b));
    uint16_t olive = mix(K_WALL, RGB(130, 214, 90), q16(b)), olive_h = mix(K_WALL, RGB(110, 170, 80), q16(b * 0.45f));
    int i, n = (int)(sizeof SEG / sizeof SEG[0]);
    for (i = 0; i < n; i++)
        rod(SEG[i][0], SEG[i][1], SEG[i][2], SEG[i][3], 2.6f, i >= 5 ? (i == 5 ? halo : olive_h) : halo, 999.0f);
    disc(40.0f, 20.0f, 5.2f, olive_h);
    for (i = 0; i < n; i++)
        rod(SEG[i][0], SEG[i][1], SEG[i][2], SEG[i][3], 1.0f, i == 6 ? olive : core, 999.0f);
    disc(40.0f, 20.0f, 3.4f, olive);
    disc(39.0f, 19.0f, 1.0f, mix(olive, K_TEXT, 8));
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
        cv_text(x, y + 16, &FONT_S, line[1], K_TEXT);
}

static const char *const PART_NAME[6] = {"INTRO", "HEAD", "SOLO", "HEAD OUT", "ENDING", "NEXT TUNE"};

/* beside the sign: the tune's number and where it is, its title, its choruses as pips */
static void draw_card(void)
{
    char t[32], *e;
    int32_t x = 76, i;
    e = cat_u(cat_s(t, "TUNE "), en_tune_no);
    (void)e;
    x = cv_text(x, 2, &FONT_XS, t, K_ACC);
    x = cv_text(x + 8, 2, &FONT_XS, PART_NAME[en_part < 6 ? en_part : 0], K_DIM);
    if (en_letter && en_part >= PT_HEAD && en_part <= PT_OUT) {   /* the form's section: A, B, C */
        t[0] = (char)en_letter;
        t[1] = 0;
        cv_round(x + 6, 3, 13, 13, 3, K_LINE);
        text_c(x + 12, 2, &FONT_XS, t, K_TEXT);
    }
    en_title(en_tune_seed, t);
    draw_title(76, 15, 160, t);
    for (i = 0; i < en_choruses && i < 8; i++) {   /* the choruses: head, solos, head out */
        int on = en_part >= PT_HEAD && en_part <= PT_OUT && i == en_chorus;
        cv_round(76 + i * 14, 48, 10, 3, 1, on ? K_ACC : i < en_chorus || en_part >= PT_END ? K_DIM : K_LINE);
    }
}

/* ---- the deck: brushed black, two reels, the tape across the heads */
static void draw_deck(void)
{
    int y;
    cv_round(2, 60, 144, 83, 5, K_DECK_HI);
    cv_round(3, 61, 142, 81, 5, K_DECK);
    for (y = 63; y < 140; y += 2) {               /* the brushing: faint streaks */
        uint32_t h = hash(2166136261u, (uint32_t)y * 3u);
        int32_t x0 = 6 + (int32_t)(h % 50u), w = 30 + (int32_t)((h >> 8) % 80u);
        if (x0 + w > 142)
            w = 142 - x0;
        cv_rect(x0, y, w, 1, mix(K_DECK, K_DECK_HI, 2 + (int)((h >> 16) % 3u)));
    }
    disc(9.0f, 67.0f, 1.8f, K_STEEL_LO);         /* the screws */
    disc(139.0f, 67.0f, 1.8f, K_STEEL_LO);
    disc(9.0f, 136.0f, 1.8f, K_STEEL_LO);
    disc(139.0f, 136.0f, 1.8f, K_STEEL_LO);
}

/* a reel: the spindle, the hub, the tape wound to radius pack, a steel flange with three windows, turned by ang */
static void draw_reel(int cx, int cy, float ang, float pack)
{
    int ox = cx - REEL_BOX / 2, oy = cy - REEL_BOX / 2, x, y, rot = (int)(ang * 256.0f) & 255;
    int pk4 = (int)(pack * 4.0f), edge = REEL_R * 4;
    uint16_t steel[11];
    for (x = 0; x < 11; x++)
        steel[x] = mix(K_STEEL_LO, K_STEEL, x + 5);
    for (y = 0; y < REEL_BOX; y++) {
        uint16_t *row;
        int sy = oy + y + cv_oy;
        if (sy < cv_y0 || sy >= cv_y1)
            continue;
        row = &cv_px[(uint32_t)sy * cv_w];
        for (x = 0; x < REEL_BOX; x++) {
            int r4 = reel_r4[y * REEL_BOX + x], raw = reel_ang[y * REEL_BOX + x], a = (raw - rot) & 255, sx = ox + x;
            uint16_t c;
            if (r4 >= edge + 2 || (uint32_t)sx >= cv_w)
                continue;
            if (r4 < 12) {                       /* the spindle, its three keys turning */
                c = (r4 >= 7 && (a % 85) < 14) ? K_DECK : K_STEEL_LO;
            } else if (r4 < 34) {                /* the hub */
                c = r4 >= 28 ? K_STEEL_LO : steel[sheen_tab[raw]];
            } else {
                int win = r4 > 40 && r4 < 98 && ((a + 12) % 85) < 50;
                if (win) {                       /* through a window: the tape wound on, or the deck behind */
                    if (r4 < pk4)
                        c = ((r4 >> 3) & 1) ? K_TAPE : mix(K_TAPE, K_TAPE_HI, 6);
                    else
                        c = mix(K_DECK, K_BG, 8);
                    if (r4 >= pk4 - 3 && r4 < pk4)
                        c = K_TAPE_HI;           /* the pack's edge catching the light */
                } else {                         /* the flange */
                    c = steel[sheen_tab[raw] > 10 ? 10 : sheen_tab[raw]];
                    if (r4 >= edge - 6)
                        c = mix(c, K_TEXT, 5);
                }
                if (r4 >= edge - 2)              /* the rim, softened */
                    c = mix(c, K_DECK, (r4 - (edge - 2)) * 4);
            }
            row[sx] = swap16(c);
        }
    }
}

static float pack_r(float wound)                 /* a pack's radius from how much tape is on it (0..1) */
{
    return fm_sqrtf(PACK_HUB * PACK_HUB + (PACK_FULL * PACK_FULL - PACK_HUB * PACK_HUB) * fm_clampf(wound, 0.0f, 1.0f));
}

static void draw_transport(void)
{
    float wl = 0.96f - 0.92f * ui.tp, rl = pack_r(wl), rr = pack_r(1.0f - wl);
    /* the tape: off the left pack (behind the flange) to its guide, across the heads, up to the right pack */
    rod((float)REEL_LX - rl * 0.80f, (float)REEL_Y + rl * 0.60f, 16.0f, 129.0f, 0.7f, K_TAPE_HI, 999.0f);
    rod(16.0f, 131.5f, 132.0f, 131.5f, 0.7f, K_TAPE_HI, 999.0f);
    rod(132.0f, 129.0f, (float)REEL_RX + rr * 0.80f, (float)REEL_Y + rr * 0.60f, 0.7f, K_TAPE_HI, 999.0f);
    draw_reel(REEL_LX, REEL_Y, ui.ang[0], rl);
    draw_reel(REEL_RX, REEL_Y, ui.ang[1], rr);
    disc(16.0f, 130.0f, 3.6f, K_STEEL);          /* the guides */
    disc(16.0f, 130.0f, 1.4f, K_DECK);
    disc(132.0f, 130.0f, 3.6f, K_STEEL);
    disc(132.0f, 130.0f, 1.4f, K_DECK);
    cv_round(54, 122, 40, 10, 2, K_STEEL_LO);    /* the head block: erase, record, play */
    cv_rect(55, 123, 38, 1, K_STEEL);
    cv_rect(62, 126, 4, 6, K_DECK);
    cv_rect(72, 126, 4, 6, K_DECK);
    cv_rect(82, 126, 4, 6, K_DECK);
    disc(102.0f, 134.0f, 2.2f, K_STEEL);         /* the capstan, the pinch roller */
    disc(102.0f, 138.5f, 2.6f, RGB(20, 18, 22));
    disc(74.0f, 70.0f, 2.4f, en_playing ? K_ACC : K_DECK_HI);   /* the pilot lamp */
    if (en_playing)
        disc(74.0f, 70.0f, 1.0f, mix(K_ACC, K_TEXT, 9));
}

/* a VU meter: an amber face lit from behind, its scale with the red past 0, a needle with some weight */
static void draw_vu(int32_t x, int32_t y, int32_t w, int32_t h, float v, const char *ch)
{
    float px = (float)x + (float)w * 0.5f, py = (float)(y + h) + 10.0f, a, i, r = (float)h + 1.0f;
    int k;
    cv_round(x - 1, y - 1, w + 2, h + 2, 3, K_DECK_HI);
    cv_round(x, y, w, h, 3, K_FACE);
    for (i = 0.0f; i <= 1.0f; i += 0.04f) {      /* the arc, thicker and red at the end */
        float aa = (-0.075f + 0.15f * i) * FM_TWO_PI - FM_PI * 0.5f;
        disc(px + fm_cosf(aa) * r, py + fm_sinf(aa) * r, i > 0.72f ? 0.9f : 0.5f, i > 0.72f ? K_RED : K_INK);
    }
    for (k = 0; k <= 4; k++) {
        float aa = (-0.075f + 0.15f * (float)k / 4.0f) * FM_TWO_PI - FM_PI * 0.5f;
        rod(px + fm_cosf(aa) * r, py + fm_sinf(aa) * r, px + fm_cosf(aa) * (r + 3.0f), py + fm_sinf(aa) * (r + 3.0f),
            0.5f, k >= 3 ? K_RED : K_INK, 999.0f);
    }
    cv_text(x + 3, y + h - 13, &FONT_XS, ch, mix(K_FACE, K_INK, 9));
    a = (-0.075f + 0.15f * fm_clampf(v, 0.0f, 1.05f)) * FM_TWO_PI - FM_PI * 0.5f;
    rod(px + fm_cosf(a) * 12.0f, py + fm_sinf(a) * 12.0f, px + fm_cosf(a) * (r + 2.0f), py + fm_sinf(a) * (r + 2.0f),
        0.6f, K_INK, (float)(y + h - 2));
    cv_rect(x + 2, y + h - 3, w - 4, 1, mix(K_FACE, K_INK, 5));
}

/* the amplifier: the chord in a dark window (and the section, the key), the VUs, the time */
static void draw_amp(void)
{
    char t[32], c[16], *e;
    uint32_t len = en_tune_len_ms, ms = en_tune_ms;
    int32_t w;
    uint16_t lcd_dim = mix(K_ACC, K_LCD, 8);
    cv_round(149, 60, 89, 83, 5, K_DECK_HI);
    cv_round(150, 61, 87, 81, 5, RGB(26, 21, 26));
    cv_round(155, 65, 77, 30, 3, K_LCD);
    if (en_part == PT_GAP) {
        text_c(193, 72, &FONT_B, "...", lcd_dim);
    } else {
        en_chord_name(en_chord_root, en_chord_q, c);
        text_c(193, 64, &FONT_B, c, K_ACC);
        e = cat_s(t, "KEY OF ");
        e = cat_s(e, EN_NOTE_NAME[en_key % 12]);
        cat_s(e, en_minor ? "m" : "");
        text_c(193, 81, &FONT_XS, t, lcd_dim);
    }
    draw_vu(155, 99, 36, 25, ui.vu[0], "L");
    draw_vu(196, 99, 36, 25, ui.vu[1], "R");
    if (en_repeat_on && len)
        ms %= len;
    if (en_part >= PT_END)
        ms = len;
    cat_time(t, ms);
    w = cv_text(155, 126, &FONT_XS, t, K_ACC);
    cat_time(cat_s(t, "/ "), len);
    cv_text(w + 4, 126, &FONT_XS, t, K_DIM);
    {
        int32_t f = len ? (int32_t)((float)ms / (float)len * 77.0f) : 0;
        cv_rect(155, 138, 77, 2, K_LINE);
        if (f > 0)
            cv_rect(155, 138, f > 77 ? 77 : f, 2, K_ACC);
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
    case B_HOME: L("THE TUNE"); L("TEMPO, FEEL, MOOD, MELODY"); break;
    case B_ENV: L("THE COMBO"); L("GUITAR, BASS, DRUMS, LEAD"); break;
    case B_GLO: L("THE ROOM"); L("TONE, REVERB, LENGTH, LIGHTS"); break;
    case B_EDIT: L("THE NEXT TUNE:"); L("A NEW KEY AND FORM"); break;
    case B_REC: L("REPEAT: THIS TUNE"); L(" PLAYS ON"); L("AGAIN: ON TO THE NEXT"); break;
    case B_OCTDN: L("THE VIBES AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE VIBES AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
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


static float vu_db(float v)                      /* a VU's reading: -20 .. +3 VU on 0 .. 1 */
{
    float db = fm_log2f(fm_maxf(v, 1e-4f)) * 6.0206f;
    return fm_clampf((db + 10.0f) / 16.0f, 0.0f, 1.05f);
}

/* the tape's motion: the reels turn at the tape's speed over their packs' radii (the small pack quick, the full
 * one slow); between tunes the deck rewinds */
static void tape_tick(float dt)
{
    uint32_t len = en_tune_len_ms ? en_tune_len_ms : 1u, ms = en_tune_ms;
    float target, v, wl, rl, rr, w;
    int i;
    if (en_repeat_on)
        ms %= len;
    target = en_part == PT_GAP ? 0.0f : en_part >= PT_END ? 1.0f : fm_clampf((float)ms / (float)len, 0.0f, 1.0f);
    if (target < ui.tp - 0.02f && en_playing) {  /* rewinding */
        ui.tp = fm_maxf(ui.tp - dt * 0.55f, target);
        v = -320.0f;
    } else {
        ui.tp += (target - ui.tp) * fm_minf(dt * 2.0f, 1.0f);
        v = 34.0f * en_tape;
    }
    wl = 0.96f - 0.92f * ui.tp;
    rl = pack_r(wl);
    rr = pack_r(1.0f - wl);
    for (i = 0; i < 2; i++) {
        w = v / (FM_TWO_PI * (i ? rr : rl));     /* turns a second */
        w = fm_clampf(w, -1.3f, 1.3f);           /* (quicker would strobe) */
        ui.ang[i] += w * dt;
        ui.ang[i] -= fm_floorf(ui.ang[i]);
    }
}

static void draw_main(void)
{
    uint32_t now = plat_ms(), h;
    float dt = (float)(now - ui.draw_t) * 0.001f, b;
    if (ui.frame % 3u)                           /* 20 frames a second */
        return;
    ui.draw_t = now;
    if (dt > 0.2f)
        dt = 0.2f;
    tape_tick(dt);
    ui.vu[0] += (vu_db(en_vu_l) - ui.vu[0]) * fm_minf(dt * 9.0f, 1.0f);
    ui.vu[1] += (vu_db(en_vu_r) - ui.vu[1]) * fm_minf(dt * 9.0f, 1.0f);
    b = neon_level();
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
    h = hash(hash(hash(2166136261u, (uint32_t)(ui.ang[0] * 256.0f) | (uint32_t)(ui.ang[1] * 256.0f) << 8 |
                                         (uint32_t)(ui.tp * 1000.0f) << 16),
                  (uint32_t)(ui.vu[0] * 100.0f) | (uint32_t)(ui.vu[1] * 100.0f) << 7 | en_chord_root << 14 |
                      (uint32_t)en_chord_q << 18 | (uint32_t)en_part << 23 | (uint32_t)q16(b) << 26),
             en_tune_seed ^ en_tune_ms / 1000u ^ (uint32_t)en_letter << 24 ^ (uint32_t)en_chorus << 20 ^ proj.style);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    draw_wall(b);
    draw_neon(b);
    draw_card();
    draw_deck();
    draw_transport();
    draw_amp();
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
 * most every 5 minutes; where the set is (the tune) at most hourly. Both at a quiet moment: the pause between
 * tunes, or stopped. SAVE saves at once */
#define AUTOSAVE_REST 10000u
#define AUTOSAVE_SETTINGS 300000u
#define AUTOSAVE_TUNE 3600000u
static void autosave(void)
{
    uint32_t now = plat_ms(), since = now - ui.saved_t;
    int quiet = !en_playing || en_part == PT_GAP, want;
    if (!quiet || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_REST)
        return;
    want = (ui.dirty && since >= AUTOSAVE_SETTINGS) || (en_tune_no != ui.saved_tune && since >= AUTOSAVE_TUNE);
    if (!want)
        return;
    if (project_save() == 0) {
        ui.dirty = 0;
        ui.saved_tune = en_tune_no;
    }
    ui.saved_t = now;                            /* (a failed save waits its turn too) */
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader (announce.c; fm1-reader): a page once it settles, with its knobs; a
 * knob as it turns; a help card as it comes up; each new tune (its title, key, tempo); playing and paused; what
 * a button did (say). WHERE is the whole screen in two: the page and the header, then the tune. */
static struct {
    uint8_t view, help, started;
    int16_t val[4];
    uint32_t tune;
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
static void sp_tune_text(void)                   /* "Tune 12, Chuva de Abril. F minor, 132 BPM" */
{
    char t[28];
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("Tune ");
    sp_int(en_tune_no);
    sp_cat(", ");
    en_title(en_tune_seed, t);
    sp_cat(t);
    sp_cat(". ");
    sp_key();
    sp_cat(", ");
    sp_int(en_bpm);
    sp_cat(" BPM");
}
static void sp_where_tune(void)                      /* WHERE's second part: the tune, the chorus, how far in */
{
    char t[12];
    sp_tune_text();
    if (en_choruses && en_part >= PT_HEAD && en_part <= PT_OUT) {
        sp_cat(". Chorus ");
        sp_int(en_chorus + 1u);
        sp_cat(" of ");
        sp_int(en_choruses);
    }
    sp_cat(". ");
    cat_time(t, en_tune_ms);
    sp_cat(t);
    sp_cat(" of ");
    cat_time(t, en_tune_len_ms);
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
    sp.tune = en_tune_no;
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
            sp_where_tune();
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
    if (en_tune_no != sp.tune && en_part != PT_GAP) {   /* a new tune: queued */
        sp.tune = en_tune_no;
        sp_tune_text();
        ann_say(sp_buf, 0, 0);
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
                                         {"KEYS OCTAVE", "0"}, {"KEYS OCTAVE", "+1"}, {"KEYS OCTAVE", "+2"}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}, {"KEYS OCTAVE", "LOWEST"},
                                          {"KEYS OCTAVE", "HIGHEST"}};
    project_t keep = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty, keep_play = sp_tp.playing;
    uint32_t keep_no = en_tune_no, keep_seed = en_tune_seed;
    uint8_t keep_key = en_key, keep_minor = en_minor, keep_rep = en_repeat_on, keep_ch = en_chorus, keep_chs = en_choruses, keep_part = en_part;
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
        en_part = (uint8_t)(x % 6);
        en_chorus = (uint8_t)(x % 3);
        en_choruses = 3;
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_where_tune();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
    for (x = 0; x < 300; x++) {                  /* tunes: their titles, keys and tempos */
        en_tune_no = (uint32_t)x + 1u;
        en_tune_seed = en_tune_rand(0x424F5341u, (uint32_t)x + 1u);
        en_key = (uint8_t)(x % 12);
        en_minor = (uint8_t)(x & 1);
        sp_tune_text();
        sp_print("TUNE", sp_buf);
    }
    en_tune_no = keep_no;
    en_tune_seed = keep_seed;
    en_chorus = keep_ch;
    en_choruses = keep_chs;
    en_part = keep_part;
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
    ui.saved_tune = proj.tune_no + 1u;            /* the tune the boot starts: nothing new to keep yet */
    ui.act_t = plat_ms();
    screen_tables();
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
