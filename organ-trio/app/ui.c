/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * ORGAN TRIO: ashtonwallen, 2026 */
/* ORGAN TRIO's UI: the panel and the screen (from AMB-1's: its knobs, help cards, messages and screen reader).
 * Part of the unity build (after gfx.c and project.c); the host simulator includes it too.
 *
 *   PLAY           the band plays / takes a break    EDIT   the next tune (the band stops, the next is counted in)
 *   REC            KEEP BLOWING: the solos go on     OCT- / OCT+   the keys' octave
 *   PRESETS        the band (each kept as left)      ALGORITHM     Tone
 *   HOME ENV LFO GLO   the pages: Band, Mix, Organ, Set (SELECT steps through them)
 *   KNOB 1-4       the page's four values
 *   keys           play along on the organ
 *   SAVE           saves (it also saves by itself at a quiet moment: see autosave); held 1.5 s: the band back to
 *                  its recipe */
#include "fastmath.h"

enum { V_BAND, V_MIX, V_ORGAN, V_SET, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Band", "Mix", "Organ", "Set"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_LFO, B_GLO};
enum { K_LEDS = 100, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_TEMPO, P_INTENS, P_ROOM, P_SWING},
    {P_ORGAN, P_GUITAR, P_DRUMS, P_CROWD},
    {P_DRAW, P_SPEAKER, P_DRIVE, P_PERC},
    {P_TONE, P_FOCUS, P_STRETCH, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Tempo", "Intensity", "Room", "Swing"},
    {"Organ", "Guitar", "Drums", "Crowd"},
    {"Drawbars", "Speaker", "Drive", "Perc"},
    {"Tone", "Focus", "Solos", "Lights"},
};

/* the screen's colours: walnut, amber lamps, cream */
#define K_BG RGB(16, 11, 10)
#define K_PANEL RGB(46, 32, 26)
#define K_LINE RGB(76, 56, 44)
#define K_DIM RGB(164, 138, 112)
#define K_TEXT RGB(248, 230, 202)
#define K_ACC RGB(242, 172, 82)
#define K_PINK RGB(214, 112, 96)
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
    uint32_t saved_tune;               /* the tune the last save kept */
    char msg[2][28];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    uint32_t keys_on;                  /* the keys sounding (played along), as MIDI notes, key bit */
    uint32_t draw_t;
    float t;                           /* the scene's clock (s) */
    float horn_ph, drum_ph;            /* the rotors as drawn (turns) ... */
    float horn_dph, drum_dph;          /* ... and how far each turned since the frame before */
    uint8_t bass_hit;                  /* the bass's hit counter as last seen */
    float bass_on;                     /* the bass note's key held down (s left) */
    float clap;                        /* the room applauding, as drawn */
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
static void set_style(int i)                    /* the band playing kept as it is; band i as it was left */
{
    project_style(i);
    project_apply();
    en_skip();                                   /* on to a new tune with the new band */
    if (!en_playing)
        en_play(1);
    sp_tp.playing = 1;
    say_as("BAND", en_style_name(proj.style), 0);
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
            say("BREAK", 0);
        } else {
            en_play(1);
            say("PLAYING", 0);
        }
        break;
    case B_REC:
        say("KEEP BLOWING", en_repeat_on ? "OFF" : "ON");
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

/* MIDI in (USB and the TRS jack): notes play along on the organ */
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
        if (ui.view == V_SET) {
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
        ui.reset_done = 1;                       /* held: the band back to its recipe (not saved) */
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
/* three bands: the header (28 px), the club (144), the knobs (68) */
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

/* the header: the band in a tag; on the right the battery, the tempo with a beat light, KEEP BLOWING, unsaved */
static void draw_header(void)
{
    char t[16];
    int msg = plat_ms() < ui.msg_until, beat = (int)(en_beats & 1u);
    uint32_t h = hash(hash(hash(2166136261u, proj.style | (uint32_t)en_bpm << 8 | (uint32_t)en_playing << 18 |
                                                   (uint32_t)en_repeat_on << 19 | (uint32_t)ui.dirty << 20 |
                                                   (uint32_t)msg << 21 | (uint32_t)beat << 22),
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
        cat_u(t, en_bpm);
        x -= text_w(&FONT_XS, "BPM");
        cv_text(x, 8, &FONT_XS, "BPM", K_DIM);
        x -= text_w(&FONT_S, t) + 3;
        cv_text(x, 6, &FONT_S, t, en_playing ? K_TEXT : K_DIM);
        x -= 14;
        if (en_playing) {                        /* the beat light */
            cv_round(x, 10, 8, 8, 4, beat ? K_ACC : K_LINE);
        } else {                                 /* a break: two bars */
            cv_rect(x, 9, 3, 10, K_DIM);
            cv_rect(x + 5, 9, 3, 10, K_DIM);
        }
        if (en_repeat_on) {
            x -= text_w(&FONT_XS, "BLOWING") + 14;
            cv_round(x, 7, text_w(&FONT_XS, "BLOWING") + 8, 15, 5, K_PINK);
            cv_text(x + 4, 7, &FONT_XS, "BLOWING", K_BG);
        }
        w = text_w(&FONT_B, en_style_name(proj.style)) + 12;
        if (4 + w < x - 4) {
            cv_round(4, 4, w, 20, 6, K_ACC);
            cv_text(10, 6, &FONT_B, en_style_name(proj.style), K_BG);
        }
    }
    cv_blit(0, 0);
}

/* ---- the organ and its speaker cabinet: the wood, the frames and the keys at rest drawn once (bg_px); every frame
 * the drawbars where they stand, the keys down on both manuals, the speaker's horn and drum turning */
static uint16_t bg_px[240 * MAIN_H] PLAT_POOL;  /* (as the canvas keeps them: byte-swapped) */
#define DB_X0 8                                  /* the drawbar well */
#define DB_Y0 6
#define DB_W 136
#define DB_H 66
#define DB_STEP 15.0f
#define KB_X 4                                   /* the manuals: 36 white keys of 4 px, C2 .. C7 */
#define KB_UP_Y 78
#define KB_LO_Y 95
#define KB_H 14
#define PLQ_Y 113                                /* the nameplate: the tune, the part, the chord */
#define CAB_X 154                                /* the speaker cabinet */
#define CAB_W 82
#define HORN_X 195.0f
#define HORN_Y 34.0f
#define GR_Y 10                                  /* the horn's grille ... */
#define GR_H 50
#define DR_Y 82                                  /* ... the drum's */
#define DR_H 50
static const int8_t WHITE_OF[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};

static uint16_t rgbf(float r, float g, float b)  /* a colour from 0..255 floats, clamped */
{
    int R = (int)fm_clampf(r, 0.0f, 255.0f), G = (int)fm_clampf(g, 0.0f, 255.0f), B = (int)fm_clampf(b, 0.0f, 255.0f);
    return RGB(R, G, B);
}
static uint16_t shade(int r, int g, int b, float l)   /* a colour at light l (1: as given) */
{
    return rgbf((float)r * l, (float)g * l, (float)b * l);
}
/* light added to a pixel: warm, a (0..1) */
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
/* a filled ellipse (anti-aliased at its sides) */
static void ellipse(float cx, float cy, float rx, float ry, uint16_t c)
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
static void wood(int32_t x0, int32_t y0, int32_t w, int32_t h, int r, int g, int b)   /* a panel with its grain */
{
    int32_t x, y;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++) {
            float gr = fm_sinf((float)y * 0.9f + fm_sinf((float)x * 0.05f + (float)y * 0.13f) * 2.4f);
            uint32_t hh = hash(2166136261u, (uint32_t)(x / 3 + y * 131));
            cv_px[(uint32_t)y * 240u + (uint32_t)x] = swap16(shade(r, g, b, 0.86f + 0.1f * gr + (float)(hh % 7u) * 0.008f));
        }
}
static void black_keys(int32_t y, uint16_t c)
{
    int w;
    for (w = 0; w < 35; w++) {
        int p = w % 7;
        if (p == 2 || p == 6)
            continue;
        cv_rect(KB_X + w * 4 + 2, y, 3, 8, c);
    }
}

/* the room and the instruments at rest, once */
static void bg_build(void)
{
    int32_t x, y, i;
    cv_begin(240, MAIN_H, K_BG);
    for (y = 0; y < MAIN_H; y++)                 /* the wall behind: dark, warm toward the floor */
        for (x = 0; x < 240; x++)
            cv_px[(uint32_t)y * 240u + (uint32_t)x] = swap16(shade(34, 22, 20, 0.45f + 0.4f * (float)y / (float)MAIN_H));
    /* the console: walnut, the drawbar well, the two manuals, the nameplate */
    wood(2, 2, 148, MAIN_H - 4, 104, 58, 32);
    cv_rect(DB_X0 - 2, DB_Y0 - 2, DB_W + 4, DB_H + 4, RGB(60, 34, 20));
    cv_rect(DB_X0, DB_Y0, DB_W, DB_H, RGB(22, 17, 16));
    for (i = 0; i < 9; i++) {                    /* the slots the drawbars come out of */
        int32_t cx = DB_X0 + 8 + (int32_t)((float)i * DB_STEP);
        cv_rect(cx - 5, DB_Y0 + 1, 10, 3, RGB(6, 5, 5));
        cv_rect(cx - 5, DB_Y0 + 4, 10, 1, RGB(48, 40, 36));
    }
    cv_rect(DB_X0 + 8 + (int32_t)(2.0f * DB_STEP) - 8, DB_Y0 + 2, 1, DB_H - 4, RGB(40, 30, 26));   /* the footages' groups */
    cv_rect(DB_X0 + 8 + (int32_t)(5.5f * DB_STEP), DB_Y0 + 2, 1, DB_H - 4, RGB(40, 30, 26));
    for (i = 0; i < 2; i++) {                    /* the manuals: the key bed, the ivory keys, the black ones */
        int32_t ky = i ? KB_LO_Y : KB_UP_Y, w;
        cv_rect(2, ky - 2, 148, KB_H + 3, RGB(18, 12, 10));
        for (w = 0; w < 36; w++)
            cv_rect(KB_X + w * 4, ky, 3, KB_H, RGB(222, 212, 188));
        cv_rect(KB_X, ky + KB_H - 2, 143, 1, RGB(170, 160, 140));
        black_keys(ky, RGB(20, 18, 18));
    }
    cv_rect(4, PLQ_Y, 144, MAIN_H - PLQ_Y - 4, RGB(26, 18, 14));
    cv_rect(4, PLQ_Y, 144, 1, RGB(170, 128, 66));
    /* the cabinet: walnut, two grilles (horn above, drum below), the speed lamps between */
    wood(CAB_X, 2, CAB_W, MAIN_H - 4, 116, 64, 34);
    cv_rect(CAB_X, 2, CAB_W, 2, RGB(150, 92, 50));
    cv_rect(CAB_X + 6, GR_Y, CAB_W - 12, GR_H, RGB(12, 9, 8));
    cv_rect(CAB_X + 6, DR_Y, CAB_W - 12, DR_H, RGB(12, 9, 8));
    cv_rect(CAB_X + 6, GR_Y + GR_H, CAB_W - 12, 1, RGB(150, 92, 50));
    cv_rect(CAB_X + 6, DR_Y + DR_H, CAB_W - 12, 1, RGB(150, 92, 50));
    cv_round(CAB_X + 5, 64, CAB_W - 10, 15, 3, RGB(30, 20, 14));
    memcpy(bg_px, cv_px, sizeof bg_px);
}

/* the drawbars: each pulled out (down) as far as it stands, its numbers showing, its coloured tip */
static void draw_drawbars(void)
{
    static const uint8_t COL[9] = {0, 0, 1, 1, 2, 1, 2, 2, 1};   /* 16' 5 1/3' brown; the octaves white; the rest black */
    int i, u;
    for (i = 0; i < 9; i++) {
        float v = fm_clampf(en_draw[i], 0.0f, 8.0f), len = 3.0f + v * 6.4f;
        int32_t cx = DB_X0 + 8 + (int32_t)((float)i * DB_STEP), y0 = DB_Y0 + 2, yt = y0 + (int32_t)(len + 0.5f);
        uint16_t tip = COL[i] == 0 ? RGB(150, 76, 36) : COL[i] == 1 ? RGB(238, 230, 212) : RGB(34, 30, 30);
        uint16_t rim = COL[i] == 0 ? RGB(196, 120, 66) : COL[i] == 1 ? RGB(255, 252, 240) : RGB(120, 110, 104);
        cv_rect(cx - 3, y0, 7, yt - y0, RGB(46, 36, 32));       /* the shaft */
        cv_rect(cx - 3, y0, 1, yt - y0, RGB(84, 70, 62));
        for (u = 1; u <= (int)v; u++) {                         /* its numbers: a mark a step */
            int32_t my = y0 + (int32_t)((float)u * 6.4f) - 3;
            cv_rect(cx - 1, my, 3, 1, RGB(214, 204, 180));
        }
        cv_round(cx - 6, yt - 2, 13, 12, 3, rim);               /* the tip */
        cv_round(cx - 5, yt - 1, 11, 10, 3, tip);
        cv_rect(cx - 3, yt, 7, 1, rim);
    }
}

/* the keys down: the right hand (and the comping) on the upper manual; the left hand's bass on the lower */
static void key_lit(int32_t ky, int note, uint16_t c)
{
    int o = (note - 36) / 12, pc = (note - 36) % 12, w = WHITE_OF[pc];
    if (note < 36 || note > 96)
        return;
    if (w >= 0)
        cv_rect(KB_X + (o * 7 + w) * 4, ky + 8, 3, KB_H - 8, c);
    else
        cv_rect(KB_X + (o * 7 + WHITE_OF[pc - 1]) * 4 + 2, ky, 3, 8, c);
}
static void draw_manuals(void)
{
    int n, lit = 0;
    uint16_t hot = RGB(255, 186, 84), deep = RGB(214, 120, 40);
    for (n = 36; n <= 96; n++) {
        uint32_t m = 1u << ((n - 36) & 31), on = ((en_keys_up[(n - 36) >> 5] | en_keys_lo[(n - 36) >> 5]) & m) != 0;
        if (on) {
            key_lit(KB_UP_Y, n, WHITE_OF[(n - 36) % 12] >= 0 ? hot : deep);
            lit = 1;
        }
    }
    if (ui.bass_on > 0.0f)
        key_lit(KB_LO_Y, en_bass_note < 36 ? en_bass_note + 12 : en_bass_note, WHITE_OF[en_bass_note % 12] >= 0 ? hot : deep);
    if (lit)                                     /* the manual's warm light on the drawbar well's edge */
        cv_rect(DB_X0, DB_Y0 + DB_H - 1, DB_W, 1, RGB(120, 70, 30));
}

/* the nameplate: the tune's title; who has the spotlight (or where the band is); the chord now */
static const char *const PART[] = {"COUNT IN", "INTRO", "THE HEAD", "ORGAN SOLO", "GUITAR SOLO", "FOURS: ORGAN",
                                   "FOURS: GUITAR", "HEAD OUT", "THE TAG", "LAST CHORD"};
static void draw_plate(void)
{
    char t[32], c[16];
    const char *part;
    int32_t cw;
    en_title(en_tune_seed, en_kind, t);
    caps(t, t, sizeof t);
    while (text_w(&FONT_T, t) > 138) {           /* (too long: shortened) */
        int k = 0;
        while (t[k])
            k++;
        t[k - 1] = 0;
        if (k > 2 && t[k - 2] == ' ')
            t[k - 2] = 0;
    }
    cv_text(8, PLQ_Y + 2, &FONT_T, t, RGB(238, 226, 198));
    if (!en_playing)
        part = "ON A BREAK";
    else if (en_phase == PH_APPLAUSE)
        part = "APPLAUSE";
    else if (en_phase == PH_GAP)
        part = "NEXT TUNE";
    else
        part = PART[en_role < 10 ? en_role : 0];
    if (en_climax && en_phase == PH_TUNE)
        part = "THE PEAK";
    cv_text(8, PLQ_Y + 14, &FONT_XS, part, en_climax ? RGB(255, 140, 70) : K_ACC);
    if (en_phase == PH_TUNE && en_role != R_COUNT)
        en_chord_name(en_chord_root, en_chord_q, c);
    else
        cat_s(cat_s(c, EN_NOTE_NAME[en_key % 12]), en_minor ? "m" : "");
    cw = text_w(&FONT_S, c);
    cv_text(146 - cw, PLQ_Y + 12, &FONT_S, c, RGB(238, 226, 198));
}

/* the rotating speaker: the horn (a bell one end, a dummy the other) turning behind its grille, the drum's scoop
 * below; ghosts of where they were when they spin fast; the lamps: slow, fast */
static void horn_at(float ph, uint16_t arm, uint16_t bell, uint16_t mouth, int ghost)
{
    float a = ph * FM_TWO_PI, s = fm_sinf(a), c = fm_cosf(a), L = 29.0f;
    float bx = HORN_X + L * s, dx = HORN_X - L * s * 0.8f;
    int front = c > 0.0f;                        /* the bell facing us */
    if (!front) {                                /* the bell behind: drawn first */
        rod(HORN_X, HORN_Y, bx, HORN_Y, 3.0f, arm, 999.0f);
        ellipse(bx, HORN_Y, 3.0f + 4.0f * fm_fabsf(c), 9.0f + 3.0f * fm_fabsf(s), bell);
    }
    rod(HORN_X, HORN_Y, dx, HORN_Y, 2.0f, arm, 999.0f);
    disc(dx, HORN_Y, 3.2f, arm);
    if (front) {
        rod(HORN_X, HORN_Y, bx, HORN_Y, 3.0f, arm, 999.0f);
        ellipse(bx, HORN_Y, 3.0f + 8.0f * c, 10.0f + 3.0f * fm_fabsf(s), bell);
        if (!ghost && c > 0.2f)
            ellipse(bx, HORN_Y, 1.5f + 6.0f * c, 7.0f, mouth);
    }
    if (!ghost)
        disc(HORN_X, HORN_Y, 4.0f, RGB(70, 64, 60));
}
static void drum_at(float ph, uint16_t c, float w)
{
    float a = ph * FM_TWO_PI, s = fm_sinf(a), co = fm_cosf(a);
    int32_t x0, x1;
    if (co <= 0.0f)
        return;
    x0 = (int32_t)(HORN_X + 30.0f * s - w * co);
    x1 = (int32_t)(HORN_X + 30.0f * s + w * co);
    if (x0 < CAB_X + 8)
        x0 = CAB_X + 8;
    if (x1 > CAB_X + CAB_W - 8)
        x1 = CAB_X + CAB_W - 8;
    if (x1 > x0)
        cv_rect(x0, DR_Y + 8, x1 - x0, DR_H - 16, c);
}
static void draw_speaker(void)
{
    int i, k, ng;
    float hz = en_horn_hz, f = fm_clampf((hz - 0.8f) / 6.0f, 0.0f, 1.0f), dh = ui.horn_dph, dd = ui.drum_dph;
    uint16_t brass = RGB(222, 196, 150), arm = RGB(150, 140, 130);
    int32_t x;
    if (en_climax)                               /* the climax: the cabinet's insides lit */
        glow(HORN_X, HORN_Y, 44.0f, 0.22f, 31.0f, 14.0f, 4.0f);
    /* the drum: its body lit from inside, the scoop's opening coming round */
    for (x = CAB_X + 8; x < CAB_X + CAB_W - 8; x++) {
        float e = ((float)x - HORN_X) / 32.0f, l = fm_maxf(0.0f, 1.0f - e * e);
        cv_rect(x, DR_Y + 6, 1, DR_H - 12, shade(176, 128, 84, 0.25f + 0.75f * l));
    }
    cv_rect(CAB_X + 8, DR_Y + 6, CAB_W - 16, 1, RGB(60, 40, 28));
    cv_rect(CAB_X + 8, DR_Y + DR_H - 7, CAB_W - 16, 1, RGB(60, 40, 28));
    ng = dd > 0.06f ? 3 : 0;                     /* fast: the ghosts of where it was this frame */
    for (k = ng; k >= 0; k--)
        drum_at(ui.drum_ph - dd * (float)k / 4.0f, k ? mix(RGB(20, 14, 12), RGB(150, 110, 74), 4 + k * 3) : RGB(16, 11, 9), 13.0f);
    ng = dh > 0.06f ? 4 : 0;
    for (k = ng; k >= 1; k--) {
        uint16_t g = mix(arm, RGB(12, 9, 8), 6 + k * 2);
        horn_at(ui.horn_ph - dh * (float)k / 5.0f, g, g, g, 1);
    }
    horn_at(ui.horn_ph, arm, brass, RGB(30, 24, 22), 0);
    /* the grilles' louvres, in front */
    for (i = 0; i < 2; i++) {
        int32_t gy = i ? DR_Y : GR_Y, gh = i ? DR_H : GR_H;
        for (x = CAB_X + 11; x < CAB_X + CAB_W - 7; x += 10) {
            cv_rect(x, gy, 2, gh, RGB(70, 40, 22));
            cv_rect(x, gy, 1, gh, RGB(140, 86, 46));
        }
    }
    /* the lamps: SLOW and FAST, the speed between them */
    {
        int fast = f > 0.5f;
        uint16_t off = RGB(96, 74, 58), hot = RGB(255, 140, 70);
        cv_text(CAB_X + 9, 64, &FONT_XS, "SLOW", fast ? off : K_ACC);
        cv_text(CAB_X + CAB_W - 9 - text_w(&FONT_XS, "FAST"), 64, &FONT_XS, "FAST", fast ? hot : off);
        cv_rect(CAB_X + 34, 70, 14, 3, RGB(60, 44, 34));
        cv_rect(CAB_X + 34, 70, 1 + (int32_t)(13.0f * f), 3, fast ? hot : K_ACC);
    }
}

static void draw_scene(void)
{
    cv_begin(240, MAIN_H, K_BG);
    memcpy(cv_px, bg_px, sizeof bg_px);
    draw_drawbars();
    draw_manuals();
    draw_plate();
    draw_speaker();
    if (ui.clap > 0.08f)                         /* the room applauding: the lamps flicker warm */
        glow(76.0f, 72.0f, 90.0f, 0.10f * ui.clap * (0.6f + 0.4f * fm_sinf(ui.t * 23.0f)), 31.0f, 20.0f, 8.0f);
}

/* the scene's state, from the band's: the rotors' angles and how far they turned since the last frame */
static void scene_tick(float dt)
{
    float h = en_horn_ph, d = en_drum_ph, e;
    uint8_t b = en_hits[HIT_BASS];
    ui.t += dt;
    if (ui.t > 3600.0f)
        ui.t -= 3600.0f;
    e = h - ui.horn_ph;
    ui.horn_dph = e < 0.0f ? e + 1.0f : e;
    e = d - ui.drum_ph;
    ui.drum_dph = e < 0.0f ? e + 1.0f : e;
    if (ui.horn_dph > 0.5f)                      /* (a stopped rotor's jitter, or a wrap: no ghosts) */
        ui.horn_dph = 0.0f;
    if (ui.drum_dph > 0.5f)
        ui.drum_dph = 0.0f;
    ui.horn_ph = h;
    ui.drum_ph = d;
    if (b != ui.bass_hit) {
        ui.bass_hit = b;
        ui.bass_on = 0.3f;
    } else {
        ui.bass_on -= dt;
    }
    ui.clap += (fm_clampf(en_applause * 1.6f, 0.0f, 1.0f) - ui.clap) * fm_minf(dt * 4.0f, 1.0f);
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
    case B_HOME: L("THE BAND"); L("TEMPO, INTENSITY,"); L(" ROOM, SWING"); break;
    case B_ENV: L("THE MIX"); L("ORGAN, GUITAR,"); L(" DRUMS, CROWD"); break;
    case B_LFO: L("THE ORGAN"); L("DRAWBARS, SPEAKER,"); L(" DRIVE, PERCUSSION"); break;
    case B_GLO: L("THE SET"); L("TONE, FOCUS, SOLOS, LIGHTS"); break;
    case B_EDIT: L("THE NEXT TUNE:"); L("THE BAND STOPS,"); L(" COUNTS IN ANOTHER"); break;
    case B_REC: L("KEEP BLOWING: THE SOLOS"); L(" GO ON AND ON"); L("AGAIN: THE HEAD OUT"); break;
    case B_OCTDN: L("THE KEYS AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE KEYS AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES EVERY BAND"); L("KEEP HOLDING: THE BAND"); L(" BACK TO ITS RECIPE"); break;
    case B_SEL: L("SELECT TURNS THE PAGES"); L("PRESETS: THE BAND"); L("ALGORITHM: TONE"); break;
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
    draw_scene();                                /* (always moving: the candle, the light) */
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
 * most every 5 minutes; where the set is (the tune) at most hourly. Both at a quiet moment: between tunes, or on a
 * break. SAVE saves at once */
#define AUTOSAVE_REST 10000u
#define AUTOSAVE_SETTINGS 300000u
#define AUTOSAVE_TUNE 3600000u
static void autosave(void)
{
    uint32_t now = plat_ms(), since = now - ui.saved_t;
    int quiet = !en_playing || en_phase != PH_TUNE, want;
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
 * knob as it turns; a help card as it comes up; each new tune (its title, kind, key, tempo); playing and on a break;
 * what a button did (say). WHERE is the whole screen in two: the page and the header, then the tune. */
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
    sp_cat(". Band ");
    sp_cat(en_style_name(proj.style));
    sp_cat(sp_tp.playing ? ", playing" : ", on a break");
    if (en_repeat_on)
        sp_cat(", keep blowing on");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static const char *const ROLE_WORDS[] = {"Counting in", "The intro", "The head", "Organ solo", "Guitar solo",
                                          "Trading fours, organ", "Trading fours, guitar", "The head out", "The tag",
                                          "The last chord"};
static void sp_tune_text(void)                   /* "Tune 12, Song for Doc. Ballad in F minor, 62 BPM" */
{
    char t[32];
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("Tune ");
    sp_int(en_tune_no);
    sp_cat(", ");
    en_title(en_tune_seed, en_kind, t);
    sp_cat(t);
    sp_cat(". ");
    sp_cat(en_kind_name(en_kind));
    sp_cat(" in ");
    sp_key();
    sp_cat(", ");
    sp_int(en_bpm);
    sp_cat(" BPM");
}
static void sp_where2(void)                      /* WHERE's second part: the tune, where the band is in it */
{
    sp_tune_text();
    sp_cat(". ");
    if (en_phase != PH_TUNE) {
        sp_cat("Between tunes");
        return;
    }
    sp_cat(ROLE_WORDS[en_role < 10 ? en_role : 0]);
    if (en_climax)
        sp_cat(", at the peak");
    if (en_role >= R_HEAD && en_role <= R_HEADOUT && en_nchorus > 1) {
        sp_cat(", chorus ");
        sp_int((uint32_t)en_chorus + 1u);
        sp_cat(" of ");
        sp_int(en_nchorus);
    }
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
    sp_cat("BAND ");
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
            sp_where2();
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
        ann_say(en_playing ? "PLAYING" : "BREAK", 0, 0);
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
    if (en_tune_no != sp.tune && en_phase == PH_TUNE) {   /* a new tune: queued */
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
                                         {"BREAK", 0}, {"PLAYING", 0}, {"KEEP BLOWING", "ON"}, {"KEEP BLOWING", "OFF"},
                                         {"SAVED", 0}, {"KEYS OCTAVE", "-2"}, {"KEYS OCTAVE", "-1"},
                                         {"KEYS OCTAVE", "0"}, {"KEYS OCTAVE", "+1"}, {"KEYS OCTAVE", "+2"}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}, {"KEYS OCTAVE", "LOWEST"},
                                          {"KEYS OCTAVE", "HIGHEST"}};
    project_t keep = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty, keep_play = sp_tp.playing;
    uint32_t keep_no = en_tune_no, keep_seed = en_tune_seed;
    uint8_t keep_kind = en_kind, keep_role = en_role, keep_phase = en_phase;
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
        sp_where2();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
    for (x = 0; x < 300; x++) {                  /* tunes: their titles, kinds, keys and tempos */
        en_tune_no = (uint32_t)x + 1u;
        en_tune_seed = en_tune_rand(0x5A5A414Au, (uint32_t)x + 1u);
        en_kind = (uint8_t)(x % K_NKINDS);
        en_key = (uint8_t)(x % 12);
        en_minor = (uint8_t)(x & 1);
        sp_tune_text();
        sp_print("TUNE", sp_buf);
    }
    for (x = 0; x < 11; x++) {                   /* where the band is */
        en_phase = (uint8_t)(x == 10 ? PH_APPLAUSE : PH_TUNE);
        en_role = (uint8_t)(x % 10);
        en_nchorus = 3;
        en_chorus = 1;
        sp_where2();
        sp_print("WHERE", sp_buf);
    }
    en_tune_no = keep_no;
    en_tune_seed = keep_seed;
    en_kind = keep_kind;
    en_role = keep_role;
    en_phase = keep_phase;
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
    ui.saved_tune = proj.tune_no + 1u;           /* the tune the boot starts: nothing new to keep yet */
    ui.act_t = plat_ms();
    bg_build();
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
