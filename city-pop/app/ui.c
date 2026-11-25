/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CITY POP: ashtonwallen, 2026 */
/* CITY POP's UI: the panel and the screen (from AMB-1's: its knobs, help cards, messages and screen reader).
 * Part of the unity build (after gfx.c and project.c); the host simulator includes it too.
 *
 *   PLAY           play / pause                      EDIT   the next song (a short fade, the tape turns over)
 *   REC            REPEAT: the song's last chorus    OCT- / OCT+   the keys' octave
 *                  goes round
 *   PRESETS        the style (each kept as left)     ALGORITHM     Sparkle
 *   HOME ENV GLO   the pages: Drive, Band, Front (SELECT steps through them)
 *   KNOB 1-4       the page's four values
 *   keys           play along on the electric piano
 *   SAVE           saves (it also saves by itself at a quiet moment: see autosave); held 1.5 s: the style back to
 *                  its recipe
 *
 * The screen: a city at dusk over a car stereo. The sky's colours drift as the song goes and the sun sinks behind
 * the towers; the windows light up through the pre-chorus, all of them and the rooftop signs by the chorus. On the
 * stereo the cassette's reels turn (the tape moving from one to the other as the song goes), and the display shows
 * the title, the section, the chord and five meters (bass, keys, guitar, lead, drums). */
#include "fastmath.h"

enum { V_DRIVE, V_BAND, V_FRONT, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Drive", "Band", "Front"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_GLO};
enum { K_LEDS = 100, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_TEMPO, P_SPARKLE, P_GROOVE, P_GLOSS},
    {P_KEYS, P_GUITAR, P_BASS, P_DRUMS},
    {P_LEAD, P_BRASS, P_STRINGS, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Tempo", "Sparkle", "Groove", "Gloss"},
    {"Keys", "Guitar", "Bass", "Drums"},
    {"Lead", "Brass", "Strings", "Lights"},
};

/* the panel's colours: a dark plum-navy, a pastel pink, a cool teal */
#define K_BG RGB(22, 18, 34)
#define K_PANEL RGB(44, 36, 62)
#define K_LINE RGB(66, 58, 90)
#define K_DIM RGB(152, 142, 178)
#define K_TEXT RGB(246, 238, 250)
#define K_ACC RGB(255, 138, 172)
#define K_TEAL RGB(104, 218, 226)
#define K_LOW RGB(236, 96, 80)                  /* the battery at one bar or none */
/* the stereo */
#define K_BODY RGB(30, 29, 38)
#define K_CHROME RGB(196, 198, 212)
#define K_CHROME_LO RGB(98, 100, 116)
#define K_BEZEL RGB(9, 9, 13)
#define K_LCD RGB(10, 22, 30)
#define K_LCD_ON RGB(136, 238, 228)
#define K_LCD_AMB RGB(255, 178, 98)
#define K_LCD_DIM RGB(30, 58, 66)
#define K_SHELL RGB(48, 46, 60)
#define K_LABEL RGB(236, 228, 208)
#define K_INK RGB(52, 44, 60)
#define K_TAPE RGB(96, 64, 50)

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
    uint32_t saved_track;              /* the song the last save kept */
    char msg[2][28];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    uint32_t keys_on;                  /* the keys sounding (played along), key bit */
    float lights;                      /* the city's windows lit: 0..1, following the song's sections */
    float sky;                         /* the way through the song, as the sky shows it */
    float pack;                        /* the tape on the left reel: 1 full .. 0 */
    float rot_l, rot_r, spin;          /* the reels' angles (turns), the tape's speed */
    float band[5];                     /* the meters as drawn */
    float sign_kick;                   /* the signs' pulse */
    uint32_t city_t, deck_t;           /* the last draws */
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
    en_skip();                                   /* on to a new song in the new style */
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
        say("NEXT SONG", 0);
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
    if ((e = plat_enc(EN_ALGO)) != 0) {          /* ALGORITHM: Sparkle, from any page */
        char t[12];
        turn(EN_ALGO, P_SPARKLE, e);
        en_param_text(P_SPARKLE, proj.par[P_SPARKLE], t);
        if (ui.view == V_DRIVE) {
            ui.touched = 1;
            ui.touch_until = plat_ms() + 1200u;
        } else {
            say("SPARKLE", t);
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
/* three bands: the city (108 px, the header over its sky), the stereo (72), the knobs (60) */
#define CITY_H 108
#define DECK_Y 108
#define DECK_H 72
#define KNB_Y 180
#define KNB_H 60

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t mixh(uint32_t a, uint32_t b)    /* a well-stirred hash of two numbers (the city's layout) */
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

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

/* colours as 8-bit triples, mixed finely (the sky's gradients) */
typedef struct {
    int16_t r, g, b;
} rgb_t;
static rgb_t lerp3(rgb_t a, rgb_t b, int t)             /* t 0..256 */
{
    rgb_t o;
    t = t < 0 ? 0 : t > 256 ? 256 : t;
    o.r = (int16_t)(a.r + (((b.r - a.r) * t) >> 8));
    o.g = (int16_t)(a.g + (((b.g - a.g) * t) >> 8));
    o.b = (int16_t)(a.b + (((b.b - a.b) * t) >> 8));
    return o;
}
static uint16_t c565(rgb_t c) { return RGB(c.r < 0 ? 0 : c.r > 255 ? 255 : c.r, c.g < 0 ? 0 : c.g > 255 ? 255 : c.g,
                                           c.b < 0 ? 0 : c.b > 255 ? 255 : c.b); }

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
static void draw_batt(int32_t x, int32_t y, int b, uint16_t bg)
{
    static const int8_t BOLT[7][2] = {{10, 2}, {9, 2}, {8, 2}, {7, 5}, {9, 2}, {8, 2}, {7, 2}};
    uint16_t c = b <= 1 ? K_LOW : K_TEXT;
    int i;
    cv_round(x, y, 18, 10, 2, c);
    cv_rect(x + 1, y + 1, 16, 8, bg);
    cv_rect(x + 18, y + 3, 2, 4, c);
    if (b == 4) {
        for (i = 0; i < 7; i++)
            cv_rect(x + BOLT[i][0], y + 1 + i, BOLT[i][1], 1, K_TEXT);
        return;
    }
    for (i = 0; i < b && i < 3; i++)
        cv_rect(x + 2 + i * 5, y + 2, 4, 6, b <= 1 ? K_LOW : K_TEAL);
}
static const char *batt_words(int b)
{
    static const char *const W[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                     "On USB power"};
    return b >= 0 && b <= 4 ? W[b] : 0;
}

/* ---- the song, as the screen follows it */
static float song_pos(void)                     /* 0..1 through the song (the repeat: round its loop) */
{
    uint32_t len = en_track_len_ms, ms = en_track_ms;
    if (!len)
        return 0.0f;
    if (en_repeat_on)
        ms %= len;
    if (en_phase == PH_GAP)
        return 1.0f;
    return fm_clampf((float)ms / (float)len, 0.0f, 1.0f);
}
static float lights_target(void)                /* the windows lit: few in the verse, rising, all in the chorus */
{
    float b = (float)en_section_bar, n = (float)(en_section_bars ? en_section_bars : 1u);
    switch (en_section) {
    case SEC_INTRO: return 0.12f + 0.10f * b / n;
    case SEC_VERSE: return 0.26f + 0.06f * b / n;
    case SEC_PRE: return 0.34f + 0.50f * (b + 1.0f) / n;
    case SEC_CHORUS: case SEC_LAST: return 1.0f;
    case SEC_INTER: return 0.48f;
    case SEC_BRIDGE: return 0.42f;
    case SEC_SOLO: return 0.66f;
    case SEC_FADE: return 0.9f - 0.75f * b / n;
    default: return 0.08f;
    }
}

/* ---- the city */
/* six skies, each from its early colours (top, middle, horizon) to its late ones, and the sun's */
static const rgb_t SKY[6][6] = {
    {{70, 92, 172}, {196, 128, 168}, {255, 178, 124}, {26, 26, 74}, {104, 52, 112}, {232, 104, 106}},   /* peach */
    {{84, 78, 170}, {178, 112, 178}, {255, 156, 160}, {30, 22, 72}, {92, 44, 112}, {196, 78, 140}},     /* rose */
    {{56, 108, 182}, {170, 150, 170}, {255, 196, 110}, {26, 36, 88}, {96, 64, 116}, {240, 122, 82}},    /* amber */
    {{112, 104, 200}, {214, 140, 200}, {255, 182, 196}, {38, 28, 92}, {120, 56, 140}, {214, 96, 150}},  /* candy */
    {{34, 86, 146}, {136, 136, 176}, {244, 204, 152}, {18, 40, 82}, {72, 64, 116}, {214, 124, 118}},   /* teal */
    {{66, 68, 150}, {150, 110, 182}, {236, 150, 186}, {18, 18, 58}, {62, 38, 104}, {140, 72, 142}},     /* lilac */
};
static const rgb_t SUN_TOP = {255, 232, 156}, SUN_LOW = {255, 112, 124};

/* the near skyline, drawn by hand: x, width, roof (y); a sign's word (or 0); 1 a tower with a mast */
typedef struct {
    int16_t x, w, top;
    const char *sign;
    uint8_t mast;
} bld_t;
static const bld_t NEAR[] = {
    {-2, 20, 76, 0, 0},   {18, 24, 68, "BAR", 0},  {42, 14, 82, 0, 0},     {56, 30, 60, "HOTEL", 0},
    {86, 16, 74, 0, 0},   {102, 20, 66, 0, 0},     {122, 13, 80, 0, 0},    {135, 26, 50, 0, 1},
    {161, 18, 72, 0, 0},  {179, 28, 64, "DISCO", 0}, {207, 11, 78, 0, 0},  {218, 22, 70, "FM", 0},
};
#define NNEAR (int)(sizeof NEAR / sizeof NEAR[0])
#define ROAD_Y 98
static const uint16_t SIGN_C[4] = {RGB(255, 110, 170), RGB(110, 236, 240), RGB(255, 206, 96), RGB(186, 140, 255)};

static void sky_stops(rgb_t *top, rgb_t *mid, rgb_t *hor, float p)
{
    int k = (en_sky_colour(en_track_seed) + en_style_sky(proj.style)) % 6, t = (int)(p * 256.0f);
    *top = lerp3(SKY[k][0], SKY[k][3], t);
    *mid = lerp3(SKY[k][1], SKY[k][4], t);
    *hor = lerp3(SKY[k][2], SKY[k][5], t);
}

static void draw_sky(rgb_t top, rgb_t mid, rgb_t hor, float p, uint32_t ms)
{
    int32_t y, i;
    for (y = 0; y < ROAD_Y; y++)                 /* the gradient: top to middle to horizon */
        cv_rect(0, y, 240, 1, c565(y < 46 ? lerp3(top, mid, y * 256 / 46) : lerp3(mid, hor, (y - 46) * 256 / 44)));
    {                                            /* the stars, coming out as the song goes on */
        int n = (int)((p - 0.3f) * 70.0f);
        uint16_t sc = RGB(250, 244, 255);
        for (i = 0; i < n && i < 36; i++) {
            uint32_t h = mixh(0x53544152u, (uint32_t)i);
            int32_t x = (int32_t)(h % 240u), sy = 3 + (int32_t)((h >> 8) % 40u);
            uint32_t tw = ((h >> 16) + ms / 260u) % 7u;
            px_blend(x, sy, sc, tw == 0 ? 4u : (uint32_t)(5 + (i % 4) * 2 + (n - i > 6 ? 3 : 0)));
        }
    }
    {                                            /* the sun, sinking behind the towers, cut by the haze's lines */
        float cy = 48.0f + 52.0f * p, cx = 114.0f, r = 15.0f;
        for (y = (int32_t)(cy - r); y <= (int32_t)(cy + r) && y < ROAD_Y; y++) {
            float dy = (float)y + 0.5f - cy, hw2 = r * r - dy * dy, hw, f;
            int32_t xa, xb, d = (int32_t)dy;
            uint16_t c;
            if (hw2 <= 0.0f || y < 0)
                continue;
            if ((d >= 1 && d % 5 == 2) || (d >= 6 && d % 5 == 3) || (d >= 10 && d % 5 == 4))
                continue;                        /* the stripes, wider low down */
            hw = fm_sqrtf(hw2);
            c = c565(lerp3(SUN_TOP, SUN_LOW, (int)((dy + r) * 256.0f / (2.0f * r))));
            xa = (int32_t)(cx - hw);
            xb = (int32_t)(cx + hw);
            f = cx - hw - (float)xa;
            cv_rect(xa + 1, y, xb - xa - 1, 1, c);
            px_blend(xa, y, c, (uint32_t)((1.0f - f) * 16.0f));
            px_blend(xb, y, c, (uint32_t)((cx + hw - (float)xb) * 16.0f));
        }
    }
    for (i = 0; i < 4; i++) {                    /* thin clouds, drifting; lit from below early, dark late */
        uint32_t h = mixh(0x434C4F55u, (uint32_t)i);
        int32_t len = 34 + (int32_t)(h % 40u), cy = 22 + i * 8 + (int32_t)((h >> 8) % 5u);
        int32_t x = (int32_t)(((h >> 12) % 300u + ms / (220u + 60u * (uint32_t)i)) % 300u) - 40;
        rgb_t lit = lerp3(lerp3(hor, (rgb_t){255, 236, 220}, 120), mid, (int)(p * 200.0f));
        uint16_t cl = c565(lit), cd = c565(lerp3(mid, top, 110));
        cv_rect(x + 4, cy, len - 8, 1, cd);
        cv_rect(x, cy + 1, len, 2, cl);
        px_blend(x - 1, cy + 1, cl, 8);
        px_blend(x + len, cy + 2, cl, 8);
        cv_rect(x + len / 3, cy + 3, len / 2, 1, mix(cl, cd, 8));
    }
}

static void draw_far(rgb_t top, rgb_t hor)       /* the far towers: hazy, a few lights */
{
    uint16_t c = c565(lerp3(lerp3(top, hor, 120), (rgb_t){20, 14, 40}, 120));
    uint16_t lit = c565(lerp3(hor, (rgb_t){255, 230, 180}, 140));
    int32_t x = -6, i = 0;
    while (x < 240) {
        uint32_t h = mixh(0x46415221u, (uint32_t)i++);
        int32_t w = 8 + (int32_t)(h % 13u), t = 54 + (int32_t)((h >> 5) % 26u), wy, wx;
        cv_rect(x, t, w, ROAD_Y - t, c);
        if ((h >> 12) % 4u == 0u)
            cv_rect(x + w / 2, t - 6, 1, 6, c);  /* an aerial */
        for (wy = t + 4; wy < ROAD_Y - 14; wy += 4)
            for (wx = x + 2; wx < x + w - 2; wx += 3)
                if ((mixh(h, (uint32_t)(wy * 64 + wx)) & 255u) < (uint32_t)(ui.lights * 70.0f))
                    cv_pset(wx, wy, lit);
        x += w + (int32_t)((h >> 20) % 3u);
    }
}

static void draw_near(rgb_t top, rgb_t hor, int beat)
{
    static const rgb_t WIN[4] = {{255, 214, 132}, {255, 236, 196}, {196, 220, 255}, {255, 160, 196}};
    rgb_t body = lerp3(top, (rgb_t){12, 10, 24}, 170);
    uint16_t bc = c565(body), edge = c565(lerp3(body, hor, 40)), dark = c565(lerp3(body, (rgb_t){14, 16, 32}, 60));
    int i, s = 0;
    for (i = 0; i < NNEAR; i++) {
        const bld_t *b = &NEAR[i];
        int32_t wx, wy;
        uint32_t lim = (uint32_t)(ui.lights * 205.0f);
        cv_rect(b->x, b->top, b->w, ROAD_Y - b->top, bc);
        cv_rect(b->x, b->top, b->w, 1, edge);    /* the roof's edge, catching the sky */
        cv_rect(b->x + b->w - 1, b->top, 1, ROAD_Y - b->top, dark);
        for (wy = b->top + 5; wy < ROAD_Y - 4; wy += 5)
            for (wx = b->x + 3; wx + 2 < b->x + b->w - 1; wx += 4) {
                uint32_t h = mixh((uint32_t)i * 977u + 13u, (uint32_t)(wy * 256 + wx));
                if ((h & 255u) < lim)
                    cv_rect(wx, wy, 2, 2, c565(WIN[(h >> 8) % 4u == 3u ? (h >> 12) % 4u : (h >> 10) % 2u]));
                else
                    cv_rect(wx, wy, 2, 2, mix(bc, edge, 5));
            }
        if (b->mast) {                           /* the tower's mast and its red light, blinking on the beat */
            int32_t mx = b->x + b->w / 2;
            cv_rect(mx, b->top - 16, 1, 16, bc);
            cv_rect(mx - 2, b->top - 4, 5, 4, bc);
            if (beat || !en_playing)
                disc((float)mx + 0.5f, (float)(b->top - 17), 1.6f, RGB(255, 70, 70));
            else
                cv_pset(mx, b->top - 17, RGB(110, 30, 40));
        }
        if (b->sign) {                           /* a rooftop sign: on, one after another, as the chorus comes */
            int32_t tw = text_w(&FONT_T, b->sign), w = tw + 8, x = b->x + b->w / 2 - w / 2, y = b->top - 17;
            float on = fm_clampf((ui.lights - 0.62f - 0.07f * (float)s) * 8.0f, 0.0f, 1.0f);
            uint16_t col = SIGN_C[s % 4], lit = mix(col, K_TEXT, (int)(ui.sign_kick * 6.0f));
            cv_rect(x + 2, y + 13, 1, 4, bc);    /* its legs */
            cv_rect(x + w - 3, y + 13, 1, 4, bc);
            cv_round(x, y, w, 14, 2, on > 0.0f ? mix(RGB(26, 16, 38), col, (int)(on * 3.0f)) : RGB(30, 24, 44));
            text_c(x + w / 2, y + 1, &FONT_T, b->sign, on > 0.0f ? mix(RGB(70, 56, 86), lit, (int)(on * 16.0f))
                                                                  : RGB(70, 60, 88));
            if (on > 0.5f) {                     /* the glow on the roof below */
                cv_rect(x + 2, y + 14, w - 4, 1, mix(bc, col, 5));
                cv_rect(x - 1, y + 2, 1, 10, mix(c565(top), col, 4));
                cv_rect(x + w, y + 2, 1, 10, mix(c565(top), col, 4));
            }
            s++;
        }
    }
}

static void draw_road(uint32_t ms)              /* the elevated road: its rail, tail lights going, headlights coming */
{
    int i;
    cv_rect(0, ROAD_Y, 240, CITY_H - ROAD_Y, RGB(16, 13, 26));
    cv_rect(0, ROAD_Y, 240, 1, RGB(92, 80, 116));
    for (i = 0; i < 240; i += 12)
        cv_rect(i, ROAD_Y + 1, 1, 2, RGB(60, 52, 80));
    cv_rect(0, CITY_H - 1, 240, 1, RGB(8, 7, 14));
    for (i = 0; i < 7; i++) {
        uint32_t h = mixh(0x43415253u, (uint32_t)i);
        int32_t xl = 250 - (int32_t)(((h % 300u) + ms / (22u + (h >> 9) % 8u)) % 300u);
        int32_t xr = (int32_t)((((h >> 4) % 300u) + ms / (18u + (h >> 13) % 9u)) % 300u) - 30;
        cv_rect(xl, ROAD_Y + 3, 2, 1, RGB(255, 70, 84));
        cv_rect(xl + 5, ROAD_Y + 3, 2, 1, RGB(255, 70, 84));
        px_blend(xl - 1, ROAD_Y + 3, RGB(255, 70, 84), 6);
        cv_rect(xr, ROAD_Y + 6, 2, 1, RGB(255, 244, 210));
        cv_rect(xr + 5, ROAD_Y + 6, 2, 1, RGB(255, 244, 210));
        px_blend(xr + 7, ROAD_Y + 6, RGB(255, 244, 210), 7);
        px_blend(xr + 8, ROAD_Y + 6, RGB(255, 244, 210), 3);
    }
}

/* the header over the sky: the style in a tag; on the right the tempo with a beat light, unsaved, the battery. A
 * message takes its place for a moment */
static void draw_header(rgb_t top)
{
    uint16_t pill = c565(lerp3(top, (rgb_t){10, 8, 22}, 170));
    char t[16];
    if (plat_ms() < ui.msg_until) {
        int32_t w = text_w(&FONT_B, ui.msg[0]) + (ui.msg[1][0] ? text_w(&FONT_B, ui.msg[1]) + 8 : 0) + 16, x;
        cv_round(4, 3, w > 232 ? 232 : w, 22, 6, pill);
        x = cv_text(12, 6, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], K_ACC);
        return;
    }
    {
        int32_t x = 234, w, x0;
        int b = batt();
        if (b >= 0) {
            x -= BATT_W;
            x0 = x;
        } else {
            x0 = x;
        }
        cat_u(t, en_bpm);
        w = text_w(&FONT_S, t) + 3 + text_w(&FONT_XS, "BPM") + 18 + (ui.dirty ? 11 : 0) + (b >= 0 ? BATT_W + 8 : 0);
        cv_round(234 - w - 6, 4, w + 8, 19, 6, pill);
        if (b >= 0)
            draw_batt(x0, 9, b, pill);
        x = x0 - (b >= 0 ? 8 : 0);
        if (ui.dirty) {
            cv_round(x - 5, 11, 5, 5, 2, K_DIM);
            x -= 11;
        }
        x -= text_w(&FONT_XS, "BPM");
        cv_text(x, 5, &FONT_XS, "BPM", K_DIM);
        x -= text_w(&FONT_S, t) + 3;
        cv_text(x, 4, &FONT_S, t, en_playing ? K_TEXT : K_DIM);
        x -= 12;
        if (en_playing) {
            cv_round(x, 10, 7, 7, 3, (en_beats & 1u) ? K_ACC : mix(pill, K_DIM, 6));
        } else {
            cv_rect(x, 9, 2, 9, K_DIM);
            cv_rect(x + 4, 9, 2, 9, K_DIM);
        }
        w = text_w(&FONT_XS, en_style_name(proj.style)) + 14;
        if (4 + w < 234 - (text_w(&FONT_S, t) + 3 + text_w(&FONT_XS, "BPM") + 18 + (ui.dirty ? 11 : 0) +
                           (b >= 0 ? BATT_W + 8 : 0)) - 10) {
            cv_round(4, 4, w, 19, 6, pill);
            cv_round(9, 11, 4, 4, 2, K_ACC);
            cv_text(16, 5, &FONT_XS, en_style_name(proj.style), K_TEXT);
        }
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
    case B_HOME: L("THE DRIVE"); L("TEMPO, SPARKLE,"); L(" GROOVE, GLOSS"); break;
    case B_ENV: L("THE BAND"); L("KEYS, GUITAR, BASS, DRUMS"); break;
    case B_GLO: L("THE FRONT LINE"); L("LEAD, BRASS, STRINGS,"); L(" LIGHTS"); break;
    case B_EDIT: L("THE NEXT SONG:"); L("A NEW KEY, A NEW TITLE"); break;
    case B_REC: L("REPEAT: THE LAST CHORUS"); L(" GOES ROUND"); L("AGAIN: ON TO THE NEXT"); break;
    case B_OCTDN: L("THE KEYS AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE KEYS AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES EVERY STYLE"); L("KEEP HOLDING: THE STYLE"); L(" BACK TO ITS RECIPE"); break;
    case B_SEL: L("SELECT TURNS THE PAGES"); L("PRESETS: THE STYLE"); L("ALGORITHM: SPARKLE"); break;
    default: break;
    }
#undef L
    return n;
}

static void draw_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    cv_round(10, 4, 220, 100, 6, K_PANEL);
    cv_round(10, 4, 220, 24, 6, K_ACC);
    cv_rect(10, 22, 220, 6, K_ACC);
    text_c(120, 8, &FONT_B, BTN_LABEL[ui.help - 1], K_BG);
    for (k = 0; k < n; k++)
        text_c(120, 34 + k * 18, &FONT_S, l[k] + (l[k][0] == ' '), K_TEXT);
}

static void draw_city(float dt)
{
    uint32_t now = plat_ms();
    float p = song_pos();
    rgb_t top, mid, hor;
    ui.city_t = now;                             /* (15 frames a second: ui_frame) */
    ui.lights += (lights_target() - ui.lights) * fm_minf(dt * 1.1f, 1.0f);
    ui.sky += (p - ui.sky) * fm_minf(dt * (en_phase == PH_GAP || p < ui.sky - 0.2f ? 0.7f : 3.0f), 1.0f);
    ui.sign_kick = fm_maxf(ui.sign_kick * 0.6f, (en_section == SEC_CHORUS || en_section == SEC_LAST) ? en_kick_glow : 0.0f);
    cv_begin(240, CITY_H, K_BG);
    if (ui.help) {
        draw_help();
        cv_blit(0, 0);
        return;
    }
    sky_stops(&top, &mid, &hor, ui.sky);
    draw_sky(top, mid, hor, ui.sky, now);
    draw_far(top, hor);
    draw_near(top, hor, (int)((en_beats >> 1) & 1u));
    draw_road(now);
    draw_header(top);
    cv_blit(0, 0);
}

/* ---- the stereo: the cassette behind its door, the display */
static void reel(float cx, float cy, float r, float rot)
{
    int k;
    disc(cx, cy, r, K_TAPE);                     /* the tape wound on it */
    if (r > 6.0f)
        disc(cx, cy, r - 1.2f, RGB(110, 76, 60));
    disc(cx, cy, 4.6f, K_LABEL);                 /* the hub, its teeth turning */
    disc(cx, cy, 2.8f, RGB(26, 24, 32));
    for (k = 0; k < 3; k++) {
        float a = (rot + (float)k / 3.0f) * FM_TWO_PI;
        disc(cx + fm_cosf(a) * 2.2f, cy + fm_sinf(a) * 2.2f, 0.9f, K_LABEL);
    }
}

static void draw_cassette(void)
{
    char t[16];
    float rl = 4.6f + 6.2f * ui.pack, rr = 4.6f + 6.2f * (1.0f - ui.pack);
    int32_t y;
    cv_round(4, 6, 110, 61, 4, K_CHROME_LO);     /* the door: chrome, its smoked glass */
    cv_round(5, 7, 108, 59, 4, RGB(14, 13, 20));
    cv_round(9, 11, 100, 51, 3, K_SHELL);         /* the cassette */
    cv_round(14, 13, 90, 17, 2, K_LABEL);          /* its label: the side, the song's number, a stripe */
    {
        rgb_t top, mid, hor;
        sky_stops(&top, &mid, &hor, 0.0f);
        cv_rect(14, 26, 90, 3, c565(hor));
        cv_rect(14, 29, 90, 1, c565(mid));
    }
    t[0] = (char)((en_track_no & 1u) ? 'A' : 'B');
    t[1] = 0;
    cv_text(18, 14, &FONT_T, t, K_INK);
    cat_u(cat_s(t, "NO."), en_track_no);
    cv_text(30, 14, &FONT_T, t, K_INK);
    cv_text(104 - text_w(&FONT_T, "C-60") - 2, 14, &FONT_T, "C-60", mix(K_INK, K_LABEL, 7));
    cv_round(24, 33, 72, 22, 3, RGB(18, 16, 24)); /* the window, the reels */
    reel(40.0f, 44.0f, rl, ui.rot_l);
    reel(80.0f, 44.0f, rr, ui.rot_r);
    cv_rect(40, 54, 40, 1, K_TAPE);               /* the tape across the bottom */
    cv_round(34, 56, 52, 6, 2, RGB(38, 36, 48));  /* the head opening */
    disc(14.5f, 57.5f, 1.4f, K_CHROME_LO);
    disc(103.5f, 57.5f, 1.4f, K_CHROME_LO);
    for (y = 8; y < 65; y++) {                    /* the glass's sheen: a soft diagonal */
        int32_t x = 74 - (y - 8) / 2;
        px_blend(x, y, K_CHROME, 2);
        px_blend(x + 1, y, K_CHROME, 3);
        px_blend(x + 2, y, K_CHROME, 2);
        px_blend(x + 7, y, K_CHROME, 1);
    }
}

static void draw_display(void)
{
    char t[32], c[16];
    uint32_t now = plat_ms();
    int32_t w, x;
    int i;
    cv_round(118, 5, 118, 63, 4, K_BEZEL);
    cv_rect(121, 8, 112, 57, K_LCD);
    en_title(en_track_seed, t);                  /* the title, scrolling when it is long */
    w = text_w(&FONT_S, t);
    if (w <= 106) {
        cv_text(124, 9, &FONT_S, t, K_LCD_ON);
    } else {
        int32_t span = w + 34, off = (int32_t)((now / 45u) % (uint32_t)span);
        if ((now / 45u) % (uint32_t)(span * 2) < (uint32_t)span && off < 24)
            off = 0;                             /* a rest at the start */
        cv_text(124 - off, 9, &FONT_S, t, K_LCD_ON);
        cv_text(124 - off + span, 9, &FONT_S, t, K_LCD_ON);
        cv_rect(114, 8, 7, 18, K_BEZEL);         /* (the text clipped to the glass) */
        cv_rect(233, 8, 3, 18, K_BEZEL);
        cv_rect(236, 8, 4, 18, K_BODY);
    }
    cv_rect(124, 27, 106, 1, K_LCD_DIM);
    {                                            /* the section; on the right the time, paused, repeat */
        char *e = cat_s(t, en_section_name(en_section));
        if ((en_section == SEC_VERSE || en_section == SEC_CHORUS) && en_section_no > 1)
            cat_u(cat_s(e, " "), en_section_no);
    }
    cv_text(124, 29, &FONT_XS, t, K_LCD_AMB);
    if (!en_playing) {
        if ((now / 500u) & 1u)
            cv_text(230 - text_w(&FONT_XS, "PAUSE"), 29, &FONT_XS, "PAUSE", K_LCD_ON);
    } else {
        cat_time(t, en_repeat_on && en_track_len_ms ? en_track_ms % en_track_len_ms : en_track_ms);
        x = 230 - text_w(&FONT_XS, t);
        cv_text(x, 29, &FONT_XS, t, K_LCD_ON);
        if (en_repeat_on)
            cv_text(x - 6 - text_w(&FONT_T, "RPT"), 31, &FONT_T, "RPT", K_LCD_AMB);
    }
    if (en_phase == PH_GAP) {                    /* the tape turning over */
        cv_text(124, 46, &FONT_T, (now / 300u) & 1u ? "<< >>" : " < > ", K_LCD_ON);
    } else {
        en_chord_name(en_chord_root, en_chord_q, en_chord_bass, c);
        cv_text(124, 45, text_w(&FONT_B, c) > 68 ? &FONT_S : &FONT_B, c, K_LCD_ON);
    }
    for (i = 0; i < 5; i++) {                    /* the meters: bass, keys, guitar, lead, drums */
        int s, n = (int)(ui.band[i] * 6.0f + 0.5f);
        int32_t mx = 197 + i * 7;
        for (s = 0; s < 6; s++)
            cv_rect(mx, 61 - s * 3, 5, 2, s < n ? (s >= 5 ? K_ACC : s >= 4 ? K_LCD_AMB : K_LCD_ON) : K_LCD_DIM);
    }
}

static void draw_deck(float dt)
{
    uint32_t now = plat_ms(), h;
    float want, v;
    int i;
    ui.deck_t = now;                             /* (20 frames a second: ui_frame) */
    want = en_phase == PH_GAP ? -3.0f : en_playing ? 1.0f : 0.0f;   /* the tape: playing, still, rewinding */
    ui.spin += (want - ui.spin) * fm_minf(dt * 4.0f, 1.0f);
    v = ui.spin * 0.55f * dt;
    ui.rot_l += v * 10.0f / (4.6f + 6.2f * ui.pack);
    ui.rot_r += v * 10.0f / (4.6f + 6.2f * (1.0f - ui.pack));
    ui.rot_l -= (float)(int)ui.rot_l;
    ui.rot_r -= (float)(int)ui.rot_r;
    if (ui.rot_l < 0.0f)
        ui.rot_l += 1.0f;
    if (ui.rot_r < 0.0f)
        ui.rot_r += 1.0f;
    ui.pack += ((en_phase == PH_GAP ? 1.0f : 1.0f - song_pos()) - ui.pack) * fm_minf(dt * 2.0f, 1.0f);
    for (i = 0; i < 5; i++) {
        float b = en_playing ? en_band[i] : 0.0f;
        ui.band[i] += (b - ui.band[i]) * fm_minf(dt * (b > ui.band[i] ? 18.0f : 5.0f), 1.0f);
    }
    h = hash(hash(hash(2166136261u, (uint32_t)(ui.rot_l * 512.0f) | (uint32_t)(ui.rot_r * 512.0f) << 9 |
                                        (uint32_t)(ui.pack * 60.0f) << 18 | (uint32_t)en_phase << 26),
                  (uint32_t)en_chord_root | (uint32_t)en_chord_q << 4 | (uint32_t)en_chord_bass << 9 |
                      (uint32_t)en_section << 13 | (uint32_t)en_playing << 17 | (uint32_t)en_repeat_on << 18 |
                      (uint32_t)(ui.band[0] * 6.5f) << 19 | (uint32_t)(ui.band[1] * 6.5f) << 22 |
                      (uint32_t)(ui.band[2] * 6.5f) << 25 | (uint32_t)(ui.band[3] * 6.5f) << 28),
             en_track_seed ^ (uint32_t)(ui.band[4] * 6.5f) ^ en_track_ms / 1000u ^ (now / 45u) << 8 ^ (now / 500u));
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, DECK_H, K_BODY);
    cv_rect(0, 0, 240, 1, K_CHROME);             /* the chrome trim */
    cv_rect(0, 1, 240, 1, K_CHROME_LO);
    cv_rect(0, DECK_H - 2, 240, 1, K_CHROME_LO);
    cv_rect(0, DECK_H - 1, 240, 1, RGB(12, 11, 16));
    draw_display();
    cv_rect(0, 2, 118, DECK_H - 4, K_BODY);      /* (the scrolled title kept off the door's side) */
    draw_cassette();
    cv_blit(0, DECK_Y);
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
        xt = xd - 6 - text_w(&FONT_T, t);
        cv_rect(4, 4, xt - 9, 1, K_LINE);
        cv_text(xt, -2, &FONT_T, t, K_ACC);
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 7, 2, 5, 5, 2, k == ui.view ? K_ACC : K_LINE);
    }
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v, hot = now_touch && ui.touched == (int)i;
        int32_t x = (int32_t)i * 60, cx = x + 30;
        char t[16];
        uint16_t bg = hot ? K_PANEL : K_BG, fill = hot ? K_ACC : mix(K_ACC, K_TEAL, 9);
        if (k == K_NONE)
            continue;
        if (i)
            cv_rect(x, 12, 1, 44, K_LINE);
        if (hot)
            cv_round(x + 3, 8, 54, 51, 6, K_PANEL);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        caps(t, KNOB_LABEL[ui.view][i], sizeof t);
        text_c(cx, 9, &FONT_T, t, hot ? K_TEXT : K_DIM);
        if (hi - lo < 6) {
            int n = hi - lo + 1, j, pw = n > 4 ? 6 : 9, gap = 3, w0 = n * (pw + gap) - gap;
            for (j = 0; j < n; j++)
                cv_round(cx - w0 / 2 + j * (pw + gap), 28, pw, 8, 2, j == v - lo ? fill : K_LINE);
        } else {
            arc(cx, 32, 11, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, K_LINE, fill, bg);
        }
        knob_text(k, t);
        text_c(cx, 43, text_w(&FONT_B, t) > 56 ? &FONT_S : &FONT_B, t, hot ? K_ACC : K_TEXT);
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
 * most every 5 minutes; where the tape is (the song) at most hourly. Both at a quiet moment: the turn-over between
 * songs, or paused. SAVE saves at once */
#define AUTOSAVE_REST 10000u
#define AUTOSAVE_SETTINGS 300000u
#define AUTOSAVE_TRACK 3600000u
static void autosave(void)
{
    uint32_t now = plat_ms(), since = now - ui.saved_t;
    int quiet = !en_playing || en_phase == PH_GAP, want;
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
 * knob as it turns; a help card as it comes up; each new song (its title, key, tempo); playing and paused; what
 * a button did (say). WHERE is the whole screen in two: the page and the header, then the song. */
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
static void sp_track_text(void)                  /* "Song 12, Lime Escalator. Key of F, 108 BPM" */
{
    char t[28];
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("Song ");
    sp_int(en_track_no);
    sp_cat(", ");
    en_title(en_track_seed, t);
    sp_cat(t);
    sp_cat(". Key of ");
    sp_cat(EN_NOTE_NAME[en_key % 12]);
    sp_cat(", ");
    sp_int(en_bpm);
    sp_cat(" BPM");
}
static void sp_record(void)                      /* WHERE's second part: the song, its section, how far in */
{
    char t[12];
    sp_track_text();
    sp_cat(". ");
    sp_cat(en_section_name(en_section));
    sp_cat(", ");
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
    if (en_track_no != sp.track && en_phase != PH_GAP) {   /* a new song: queued */
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
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}, {"NEXT SONG", 0},
                                         {"PAUSED", 0}, {"PLAYING", 0}, {"REPEAT", "ON"}, {"REPEAT", "OFF"},
                                         {"SAVED", 0}, {"KEYS OCTAVE", "-2"}, {"KEYS OCTAVE", "-1"},
                                         {"KEYS OCTAVE", "0"}, {"KEYS OCTAVE", "+1"}, {"KEYS OCTAVE", "+2"}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}, {"KEYS OCTAVE", "LOWEST"},
                                          {"KEYS OCTAVE", "HIGHEST"}};
    project_t keep = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty, keep_play = sp_tp.playing;
    uint32_t keep_no = en_track_no, keep_seed = en_track_seed;
    uint8_t keep_key = en_key, keep_rep = en_repeat_on, keep_sec = en_section;
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
    for (x = 0; x < SEC_N; x++) {                /* WHERE: playing, paused, unsaved, repeat, every section */
        ui.view = (uint8_t)(x % NVIEWS);
        sp_tp.playing = (uint8_t)(x & 1);
        ui.dirty = (uint8_t)(x >> 1 & 1);
        en_repeat_on = (uint8_t)(x == 5);
        en_key = (uint8_t)(x * 5 % 12);
        en_section = (uint8_t)x;
        proj.style = (uint8_t)(x * 3 % NSTYLES);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_record();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
    for (x = 0; x < 300; x++) {                  /* songs: their titles, keys and tempos */
        en_track_no = (uint32_t)x + 1u;
        en_track_seed = en_track_rand(0x43495459u, (uint32_t)x + 1u);
        en_key = (uint8_t)(x % 12);
        sp_track_text();
        sp_print("TRACK", sp_buf);
    }
    en_track_no = keep_no;
    en_track_seed = keep_seed;
    en_key = keep_key;
    en_section = keep_sec;
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
        en_param_text(P_SPARKLE, x, o);
        msg_text("SPARKLE", o, t);
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
    ui.saved_track = proj.track_no + 1u;          /* the song the boot starts: nothing new to keep yet */
    ui.act_t = plat_ms();
    ui.lights = 0.1f;
    ui.pack = 1.0f;
    ui.city_t = ui.deck_t = plat_ms() - 1000u;
    lcd_fill(0, 0, 240, 240, K_BG);
    if (!en_playing)
        en_play(1);                              /* it plays from the start */
    sp_tp.playing = 1;
    sp_tp.play_t = plat_ms();
}

void ui_frame(void)
{
    uint32_t now = plat_ms();
    input();
    autosave();
    if (now - ui.city_t >= 60u)
        draw_city(fm_minf((float)(now - ui.city_t) * 0.001f, 0.2f));
    else if (now - ui.deck_t >= 45u)
        draw_deck(fm_minf((float)(now - ui.deck_t) * 0.001f, 0.2f));
    draw_knobs();
    leds();
    speak();
    ann_tick(plat_ms());
    ui.frame++;
}

void ui_input_only(void) { input(); }
