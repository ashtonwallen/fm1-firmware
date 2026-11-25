/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * JAZZ TRIO: ashtonwallen, 2026 */
/* JAZZ TRIO's UI: the panel and the screen (from AMB-1's: its knobs, help cards, messages and screen reader).
 * Part of the unity build (after gfx.c and project.c); the host simulator includes it too.
 *
 *   PLAY           the band plays / takes a break    EDIT   the next tune (the band stops, the next is counted in)
 *   REC            KEEP BLOWING: the solos go on     OCT- / OCT+   the keys' octave
 *   PRESETS        the band (each kept as left)      ALGORITHM     Tone
 *   HOME ENV GLO   the pages: Band, Mix, Set (SELECT steps through them)
 *   KNOB 1-4       the page's four values
 *   keys           play along on the piano
 *   SAVE           saves (it also saves by itself at a quiet moment: see autosave); held 1.5 s: the band back to
 *                  its recipe */
#include "fastmath.h"

enum { V_BAND, V_MIX, V_SET, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Band", "Mix", "Set"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_GLO};
enum { K_LEDS = 100, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_TEMPO, P_INTENS, P_ROOM, P_SWING},
    {P_PIANO, P_BASS, P_DRUMS, P_CROWD},
    {P_TONE, P_FOCUS, P_STRETCH, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Tempo", "Intensity", "Room", "Swing"},
    {"Piano", "Bass", "Drums", "Crowd"},
    {"Tone", "Focus", "Solos", "Lights"},
};

/* the screen's colours: a small club late on: dark wood, amber light, cream */
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
    float spot, spot_w, spot_a;        /* the spotlight: where (x), how wide, how bright, as drawn */
    uint8_t hits[NHITS];               /* the hit counters as last seen */
    float hv[NHITS];                   /* each player's last hit, fading (1 at the hit) */
    float rh_x, lh_x, neck_y;          /* the pianist's hands, the bassist's left hand, as drawn */
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

/* ---- the club, from a table near the front: the room drawn once (bg_px), the band and the light every frame */
static uint16_t bg_px[240 * MAIN_H] PLAT_POOL;  /* (as the canvas keeps them: byte-swapped) */
#define STAGE_Y 92                               /* the stage's floor ... */
#define LIP_Y 101                                /* ... its front edge */
#define BOARD_X 4
#define BOARD_Y 4
#define BOARD_W 70
#define BOARD_H 44
#define X_PIANO 50
#define X_BASS 132
#define X_DRUMS 205
#define CANDLE_X 196
#define CANDLE_Y 116

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
/* a filled four-sided shape (convex, corners in order), by rows */
static void quad(const float *px, const float *py, uint16_t c)
{
    int32_t y, ymin = 999, ymax = -999, i;
    for (i = 0; i < 4; i++) {
        if ((int32_t)py[i] < ymin)
            ymin = (int32_t)py[i];
        if ((int32_t)py[i] > ymax)
            ymax = (int32_t)py[i];
    }
    for (y = ymin; y <= ymax; y++) {
        float yc = (float)y + 0.5f, lo = 999.0f, hi = -999.0f;
        for (i = 0; i < 4; i++) {
            float x0 = px[i], y0 = py[i], x1 = px[(i + 1) & 3], y1 = py[(i + 1) & 3], x;
            if ((yc < y0) == (yc < y1))
                continue;
            x = x0 + (x1 - x0) * (yc - y0) / (y1 - y0);
            lo = fm_minf(lo, x);
            hi = fm_maxf(hi, x);
        }
        if (hi > lo)
            cv_rect((int32_t)(lo + 0.5f), y, (int32_t)(hi + 0.5f) - (int32_t)(lo + 0.5f), 1, c);
    }
}

/* the room, once: the wall and its chalkboard, the curtain, the stage, the people at the tables, our table */
static void bg_build(void)
{
    int32_t x, y, i;
    cv_begin(240, MAIN_H, K_BG);
    for (y = 0; y < STAGE_Y; y++) {             /* the wall: old brick, warm, darker up high */
        float l = 0.55f + 0.45f * (float)y / (float)STAGE_Y;
        for (x = 0; x < 84; x++) {
            int row = y / 6, joint = (x + (row & 1) * 7) % 14 == 0 || y % 6 == 0;
            uint32_t hh = hash(hash(2166136261u, (uint32_t)(x + (row & 1) * 7) / 14u), (uint32_t)row);
            float v = (joint ? 0.62f : 0.86f + (float)(hh % 13u) * 0.012f) * l;
            cv_px[(uint32_t)y * 240u + (uint32_t)x] = swap16(shade(66, 36, 28, v));
        }
        for (x = 84; x < 240; x++) {             /* the curtain: deep red folds */
            float f = fm_sinf((float)x * 0.42f + fm_sinf((float)x * 0.07f) * 1.6f), v = (0.55f + 0.32f * f) * l;
            if (y < 9)
                v *= 0.6f;                       /* its valance */
            cv_px[(uint32_t)y * 240u + (uint32_t)x] = swap16(shade(104, 24, 30, v));
        }
        cv_rect(84, y, 2, 1, shade(30, 10, 12, l));   /* where the curtain meets the wall */
    }
    for (i = 0; i < 12; i++) {                   /* a string of small bulbs over the stage */
        float bx = 92.0f + (float)i * 13.0f, by = 3.0f + 1.6f * fm_sinf((float)i * 1.3f);
        glow(bx, by, 7.0f, 0.35f, 31.0f, 22.0f, 8.0f);
        disc(bx, by, 1.3f, RGB(255, 214, 150));
    }
    /* the chalkboard: a wooden frame, the slate, old chalk dust */
    cv_round(BOARD_X, BOARD_Y, BOARD_W, BOARD_H, 2, RGB(96, 62, 38));
    cv_rect(BOARD_X + 3, BOARD_Y + 3, BOARD_W - 6, BOARD_H - 6, RGB(30, 38, 34));
    for (i = 0; i < 160; i++) {
        uint32_t hh = hash(hash(0xC4A1u, (uint32_t)i), 7u);
        px_blend(BOARD_X + 3 + (int32_t)(hh % (uint32_t)(BOARD_W - 6)), BOARD_Y + 3 + (int32_t)((hh >> 8) % (uint32_t)(BOARD_H - 6)),
                 RGB(120, 130, 124), 3u + (hh >> 20) % 4u);
    }
    cv_rect(BOARD_X + 1, BOARD_Y + BOARD_H - 2, BOARD_W - 2, 1, RGB(130, 92, 60));
    /* the stage: planks, a warm wash, its lip */
    for (y = STAGE_Y; y < LIP_Y; y++) {
        float l = 0.7f + 0.5f * (float)(y - STAGE_Y) / (float)(LIP_Y - STAGE_Y);
        for (x = 0; x < 240; x++) {
            uint32_t hh = hash(2166136261u, (uint32_t)((x + y * 37) / 23));
            float v = l * ((y - STAGE_Y) % 3 == 0 ? 0.8f : 0.95f + (float)(hh % 9u) * 0.012f);
            cv_px[(uint32_t)y * 240u + (uint32_t)x] = swap16(shade(92, 58, 34, v));
        }
    }
    cv_rect(0, LIP_Y, 240, 1, RGB(138, 96, 60));
    cv_rect(0, LIP_Y + 1, 240, 5, RGB(34, 22, 18));
    for (x = 0; x < 240; x++)                    /* the stage's light on the back wall and the curtain */
        for (y = 30; y < STAGE_Y; y++)
            px_add(x, y, 0.12f * (float)(y - 30) / (float)(STAGE_Y - 30), 31.0f, 20.0f, 8.0f);
    /* the tables between us and the stage: people, little lamps */
    {
        static const int16_t P[7][3] = {{18, 108, 6}, {52, 112, 7}, {88, 106, 6}, {118, 110, 6}, {156, 107, 7},
                                        {190, 111, 6}, {226, 106, 6}};
        uint16_t sil = RGB(14, 9, 9), rim = RGB(84, 52, 34);
        for (i = 0; i < 4; i++)                  /* the lamps on their tables */
            glow(36.0f + (float)i * 62.0f, 118.0f, 9.0f, 0.4f, 31.0f, 18.0f, 6.0f);
        for (i = 0; i < 7; i++) {
            float px = (float)P[i][0], py = (float)P[i][1], r = (float)P[i][2];
            cv_round((int32_t)(px - r * 1.9f), (int32_t)(py + r * 0.6f), (int32_t)(r * 3.8f), 30, 6, rim);
            cv_round((int32_t)(px - r * 1.9f), (int32_t)(py + r * 0.6f) + 1, (int32_t)(r * 3.8f), 30, 6, sil);
            disc(px, py - 0.6f, r, rim);
            disc(px, py, r, sil);
        }
    }
    /* our table: polished dark wood, its near edge out of sight */
    for (y = 124; y < MAIN_H; y++)
        for (x = 0; x < 240; x++) {
            float e = (float)(x - 120) / 120.0f, top = 124.0f + e * e * 5.0f, v;
            if ((float)y < top)
                continue;
            v = 0.95f - 0.4f * ((float)y - top) / 20.0f;
            cv_px[(uint32_t)y * 240u + (uint32_t)x] =
                swap16((float)y - top < 1.0f ? RGB(132, 86, 52) : shade(66, 38, 24, v));
        }
    /* a drink on a napkin */
    quad((const float[]){22.0f, 58.0f, 62.0f, 18.0f}, (const float[]){132.0f, 132.0f, 138.0f, 138.0f}, RGB(206, 196, 176));
    cv_rect(30, 112, 22, 22, RGB(52, 40, 34));
    cv_rect(31, 113, 20, 20, RGB(70, 58, 50));
    cv_rect(31, 120, 20, 13, RGB(160, 90, 34));
    cv_rect(34, 117, 7, 7, RGB(190, 170, 150));  /* ice */
    cv_rect(42, 119, 6, 6, RGB(176, 150, 120));
    cv_rect(31, 120, 20, 1, RGB(220, 150, 70));
    cv_rect(31, 113, 1, 20, RGB(150, 140, 130));
    cv_rect(49, 113, 1, 20, RGB(120, 110, 100));
    /* the candle in its glass */
    cv_round(CANDLE_X - 8, CANDLE_Y + 2, 16, 16, 3, RGB(110, 28, 22));
    cv_rect(CANDLE_X - 6, CANDLE_Y + 4, 12, 12, RGB(170, 54, 34));
    cv_rect(CANDLE_X - 6, CANDLE_Y + 4, 2, 12, RGB(210, 96, 60));
    memcpy(bg_px, cv_px, sizeof bg_px);
}

/* the chalkboard: the tune's title, its chord now, the tempo */
static void draw_board(void)
{
    char t[32], line[2][32], c[16];
    int ln = 0, n = 0, i = 0, w = BOARD_W - 10;
    uint16_t chalk = RGB(226, 228, 214), dim = RGB(150, 160, 150);
    en_title(en_tune_seed, en_kind, t);
    caps(t, t, sizeof t);
    line[0][0] = line[1][0] = 0;
    while (t[i] && ln < 2) {                     /* the title, its words wrapped onto two lines */
        char word[24];
        int k = 0, keep_n = n;
        while (t[i] == ' ')
            i++;
        while (t[i] && t[i] != ' ' && k < 23)
            word[k++] = t[i++];
        word[k] = 0;
        if (!k)
            break;
        if (n) {
            line[ln][n++] = ' ';
            line[ln][n] = 0;
        }
        cat_s(&line[ln][n], word);
        if (text_w(&FONT_T, line[ln]) > w && keep_n) {
            line[ln][keep_n] = 0;
            if (++ln >= 2)
                break;
            cat_s(line[ln], word);
            n = k;
        } else {
            n += k;
        }
    }
    cv_text(BOARD_X + 5, BOARD_Y + 3, &FONT_T, line[0], chalk);
    if (line[1][0])
        cv_text(BOARD_X + 5, BOARD_Y + 13, &FONT_T, line[1], chalk);
    if (en_phase == PH_TUNE && en_role != R_COUNT)
        en_chord_name(en_chord_root, en_chord_q, c);
    else
        cat_s(cat_s(c, EN_NOTE_NAME[en_key % 12]), en_minor ? "m" : "");
    cv_text(BOARD_X + 5, BOARD_Y + 25, &FONT_B, c, chalk);
    cat_u(t, en_tune_no);
    cv_text(BOARD_X + BOARD_W - 6 - text_w(&FONT_T, t), BOARD_Y + 29, &FONT_T, t, dim);
}

/* the spotlight: a cone from above onto the soloist (or the whole band, softer), its pool on the stage */
static void draw_spot(void)
{
    int32_t y, x;
    float sx = ui.spot + 18.0f, a = ui.spot_a;
    if (a < 0.02f)
        return;
    for (y = 0; y < LIP_Y; y++) {
        float f = (float)(y + 30) / (float)(STAGE_Y + 30), c = sx + (ui.spot - sx) * f, hw = 5.0f + ui.spot_w * f;
        float rowa = a * (0.45f + 0.4f * f);
        for (x = (int32_t)(c - hw); x <= (int32_t)(c + hw); x++) {
            float e = ((float)x - c) / hw, k = 1.0f - e * e;
            if (k > 0.0f)
                px_add(x, y, rowa * k, 15.0f, 13.0f, 8.0f);
        }
    }
    for (y = STAGE_Y - 4; y < LIP_Y; y++)        /* the pool on the boards */
        for (x = (int32_t)(ui.spot - ui.spot_w - 6.0f); x <= (int32_t)(ui.spot + ui.spot_w + 6.0f); x++) {
            float ex = ((float)x - ui.spot) / (ui.spot_w + 6.0f), ey = ((float)y - (float)(STAGE_Y + 4)) / 7.0f;
            float k = 1.0f - ex * ex - ey * ey;
            if (k > 0.0f)
                px_add(x, y, a * 0.5f * k, 16.0f, 12.0f, 6.0f);
        }
}
static float lit(float x)                        /* how lit a player at x is: the stage's wash and the spot */
{
    float d = (x - ui.spot) / (ui.spot_w + 14.0f);
    return 0.62f + 0.5f * ui.spot_a * fm_maxf(0.0f, 1.0f - d * d);
}

static void draw_piano(void)
{
    float l = lit(X_PIANO), b = ui.hv[HIT_RH] > ui.hv[HIT_LH] ? ui.hv[HIT_RH] : ui.hv[HIT_LH];
    uint16_t blk = shade(22, 19, 22, l), edge = shade(96, 86, 84, l), skin = shade(176, 126, 96, l);
    uint16_t suit = shade(34, 28, 36, l), hair = shade(30, 22, 18, l);
    int32_t x;
    /* the lid up, its stick, the body, the keys */
    quad((const float[]){12.0f, 86.0f, 78.0f, 16.0f}, (const float[]){74.0f, 74.0f, 44.0f, 56.0f}, blk);
    rod(16.0f, 56.0f, 78.0f, 44.0f, 0.8f, edge, 999.0f);
    rod(66.0f, 74.0f, 64.0f, 50.0f, 0.6f, edge, 999.0f);
    cv_round(8, 72, 86, 12, 4, blk);
    cv_rect(10, 72, 82, 1, edge);
    cv_rect(16, 79, 70, 3, shade(226, 216, 196, l));
    for (x = 17; x < 85; x += 4)
        if ((x / 4) % 7 != 2 && (x / 4) % 7 != 6)
            cv_rect(x + 2, 79, 2, 2, blk);
    rod(14.0f, 84.0f, 14.0f, 98.0f, 1.1f, blk, 999.0f);
    rod(86.0f, 84.0f, 86.0f, 98.0f, 1.1f, blk, 999.0f);
    rod(50.0f, 84.0f, 50.0f, 96.0f, 0.7f, edge, 999.0f);
    /* the pianist, from behind on the bench: the hands on the keys, where the notes are */
    cv_rect(36, 92, 30, 3, shade(70, 44, 30, l));
    cv_round(41, 64, 20, 30, 6, suit);
    disc(51.0f, 56.2f, 6.4f, shade(150, 96, 60, l * 0.8f));   /* the stage light on the hair */
    disc(51.0f, 57.0f, 6.2f, hair);
    rod(43.0f, 68.0f, ui.lh_x, 80.0f - 1.5f * ui.hv[HIT_LH], 1.8f, suit, 999.0f);
    rod(59.0f, 68.0f, ui.rh_x, 80.0f - 1.5f * ui.hv[HIT_RH], 1.8f, suit, 999.0f);
    disc(ui.lh_x, 80.0f - 1.5f * ui.hv[HIT_LH], 2.2f, skin);
    disc(ui.rh_x, 80.0f - 1.5f * ui.hv[HIT_RH], 2.2f, skin);
    (void)b;
}

static void draw_bass(void)
{
    float l = lit(X_BASS), p = ui.hv[HIT_BASS];
    uint16_t wood = shade(150, 76, 36, l), wood_hi = shade(196, 116, 60, l), dark = shade(40, 24, 16, l);
    uint16_t suit = shade(36, 30, 34, l), skin = shade(176, 126, 96, l), hair = shade(52, 40, 34, l);
    /* the bassist behind it, standing */
    rod(136.0f, 84.0f, 134.0f, 98.0f, 2.2f, suit, 999.0f);
    rod(146.0f, 84.0f, 148.0f, 98.0f, 2.2f, suit, 999.0f);
    cv_round(131, 34, 20, 54, 6, suit);
    disc(141.0f, 25.2f, 6.2f, shade(150, 96, 60, l * 0.8f));
    disc(141.0f, 26.0f, 6.0f, hair);
    /* the bass: its neck and scroll, its body, strings, the endpin */
    rod(126.0f, 62.0f, 129.0f, 14.0f, 1.7f, dark, 999.0f);
    disc(129.5f, 13.0f, 2.6f, dark);
    ellipse(125.0f, 66.0f, 9.0f, 9.0f, wood);
    ellipse(125.0f, 82.0f, 12.5f, 11.0f, wood);
    ellipse(122.0f, 64.0f, 4.0f, 5.0f, wood_hi);
    ellipse(121.0f, 80.0f, 5.0f, 6.0f, wood_hi);
    rod(119.0f, 72.0f, 120.0f, 80.0f, 0.5f, dark, 999.0f);
    rod(131.0f, 72.0f, 130.0f, 80.0f, 0.5f, dark, 999.0f);
    cv_rect(121, 86, 9, 2, dark);
    rod(125.0f, 92.0f, 125.0f, 99.0f, 0.5f, dark, 999.0f);
    rod(128.6f, 18.0f, 125.5f, 86.0f, 0.4f, shade(210, 200, 180, l * 0.8f), 999.0f);
    /* the left hand up the neck (higher notes lower down), the right plucking */
    rod(134.0f, 40.0f, 129.0f - (ui.neck_y - 18.0f) * 0.06f, ui.neck_y, 1.8f, suit, 999.0f);
    disc(128.0f - (ui.neck_y - 18.0f) * 0.06f, ui.neck_y, 2.3f, skin);
    rod(148.0f, 44.0f, 134.0f, 70.0f - 2.0f * p, 1.8f, suit, 999.0f);
    disc(132.0f + 2.0f * p, 72.0f - 2.0f * p, 2.3f, skin);
}

static void draw_drums(void)
{
    float l = lit(X_DRUMS), ride = ui.hv[HIT_RIDE], hat = ui.hv[HIT_HAT], sn = ui.hv[HIT_SNARE], k = ui.hv[HIT_KICK];
    float crash = ui.hv[HIT_CRASH], sh;
    uint16_t brass = shade(196, 150, 70, l), metal = shade(170, 168, 164, l), shell = shade(122, 34, 30, l);
    uint16_t suit = shade(34, 30, 36, l), skin = shade(176, 126, 96, l), hair = shade(26, 20, 18, l);
    float rx, ry, sx, sy;
    /* the drummer behind the kit */
    cv_round(196, 46, 20, 36, 6, suit);
    disc(206.0f, 37.2f, 6.2f, shade(150, 96, 60, l * 0.8f));
    disc(206.0f, 38.0f, 6.0f, hair);
    /* stands; the hi-hat (closing on two and four), the crash, the ride */
    rod(172.0f, 64.0f, 172.0f, 98.0f, 0.6f, metal, 999.0f);
    rod(228.0f, 58.0f, 232.0f, 98.0f, 0.6f, metal, 999.0f);
    rod(186.0f, 50.0f, 182.0f, 72.0f, 0.5f, metal, 999.0f);
    ellipse(172.0f, 63.0f - (1.0f - hat) * 1.5f, 9.0f, 1.4f, brass);
    ellipse(172.0f, 65.0f, 9.0f, 1.4f, brass);
    ellipse(186.0f, 48.0f, 11.0f, 2.2f, shade(196, 150, 70, l * (1.0f + 0.5f * crash)));
    sh = 1.0f + 0.55f * ride + 0.12f * fm_sinf(ui.t * 9.0f);
    ellipse(227.0f, 56.0f, 14.0f, 3.0f, shade(200, 154, 72, l * sh));
    ellipse(227.0f, 55.4f, 9.0f, 1.4f, shade(240, 200, 120, l * sh));
    if (ride > 0.3f)                             /* the shimmer: a glint moving round the edge */
        disc(227.0f + 11.0f * fm_cosf(ui.t * 5.0f), 56.0f + 2.0f * fm_sinf(ui.t * 5.0f), 1.0f + ride,
             RGB(255, 240, 200));
    /* the kick, the snare, the floor tom */
    ellipse(206.0f, 85.0f, 13.0f, 13.0f, shell);
    ellipse(206.0f, 85.0f, 11.0f, 11.0f, shade(214, 196, 164, l * (1.0f + 0.3f * k)));
    ellipse(206.0f, 85.0f, 4.0f, 4.0f, shade(150, 120, 90, l));
    cv_rect(176, 73, 18, 6, metal);
    ellipse(185.0f, 72.0f, 9.0f, 2.2f, shade(220, 214, 200, l * (1.0f + 0.4f * sn)));
    cv_round(222, 74, 14, 18, 3, shell);
    ellipse(229.0f, 74.0f, 7.0f, 1.8f, shade(214, 196, 164, l));
    /* the arms: the right to the ride (or the snare), the left to the snare or the hat */
    rx = 222.0f;
    ry = 56.0f - 4.0f * (1.0f - ride);
    if (en_brushes) {
        rx = 190.0f;
        ry = 71.0f - 2.0f * sn;
    }
    sx = 184.0f + 2.0f * fm_sinf(ui.t * 3.1f);
    sy = 70.0f - 5.0f * sn;
    rod(214.0f, 52.0f, rx - 4.0f, ry + 4.0f, 1.7f, suit, 999.0f);
    rod(198.0f, 52.0f, sx + 6.0f, sy + 2.0f, 1.7f, suit, 999.0f);
    disc(rx - 4.0f, ry + 4.0f, 2.0f, skin);
    disc(sx + 6.0f, sy + 2.0f, 2.0f, skin);
    rod(rx - 4.0f, ry + 4.0f, rx + 2.0f, ry - 1.0f, 0.6f, en_brushes ? metal : shade(220, 190, 140, l), 999.0f);
    rod(sx + 6.0f, sy + 2.0f, sx, sy, 0.6f, en_brushes ? metal : shade(220, 190, 140, l), 999.0f);
}

/* the candle: its flame, flickering, its light on the glass and the table */
static void draw_candle(void)
{
    float f = 0.8f + 0.12f * fm_sinf(ui.t * 7.3f) + 0.08f * fm_sinf(ui.t * 17.1f + 1.3f), sway = 0.7f * fm_sinf(ui.t * 2.3f);
    glow((float)CANDLE_X, (float)CANDLE_Y + 4.0f, 26.0f, 0.38f * f, 31.0f, 16.0f, 4.0f);
    ellipse((float)CANDLE_X + sway * 0.5f, (float)CANDLE_Y + 3.0f, 1.8f, 4.0f * f, RGB(255, 170, 70));
    ellipse((float)CANDLE_X + sway * 0.3f, (float)CANDLE_Y + 4.5f, 1.0f, 2.2f * f, RGB(255, 240, 200));
}

/* the room applauding: hands catching the light over the tables */
static void draw_clap(void)
{
    static const int16_t H[9][2] = {{14, 96}, {26, 98}, {48, 100}, {62, 99}, {96, 95}, {150, 97}, {164, 96}, {186, 99}, {230, 95}};
    int i;
    if (ui.clap < 0.08f)
        return;
    for (i = 0; i < 9; i++) {
        float ph = ui.t * (9.0f + (float)i * 0.7f) + (float)i * 2.1f, up = fm_sinf(ph);
        if ((float)(i % 4) * 0.25f > ui.clap)
            continue;
        disc((float)H[i][0], (float)H[i][1] - 2.0f * up, 1.6f, RGB(150, 106, 80));
    }
}

static void draw_scene(void)
{
    cv_begin(240, MAIN_H, K_BG);
    memcpy(cv_px, bg_px, sizeof bg_px);
    draw_spot();
    draw_board();
    draw_piano();
    draw_bass();
    draw_drums();
    draw_clap();
    draw_candle();
}

/* the scene's state, eased toward the band's: the spotlight finds the soloist; hands follow the notes */
static void scene_tick(float dt)
{
    float tx = 120.0f, tw = 78.0f, ta = 0.45f, e;
    int i;
    ui.t += dt;
    if (ui.t > 3600.0f)
        ui.t -= 3600.0f;
    if (!en_playing || en_phase != PH_TUNE)
        ta = en_playing ? 0.3f : 0.12f;
    else if (en_soloist == 1)
        tx = X_PIANO, tw = 30.0f, ta = 1.0f;
    else if (en_soloist == 2)
        tx = X_BASS, tw = 24.0f, ta = 1.0f;
    else if (en_soloist == 3)
        tx = X_DRUMS, tw = 30.0f, ta = 1.0f;
    e = fm_minf(dt * 1.6f, 1.0f);
    ui.spot += (tx - ui.spot) * e;
    ui.spot_w += (tw - ui.spot_w) * e;
    ui.spot_a += (ta - ui.spot_a) * e;
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
    ui.rh_x += (fm_clampf(62.0f + ((float)en_rh_note - 70.0f) * 1.0f, 58.0f, 84.0f) - ui.rh_x) * e;
    ui.lh_x += (fm_clampf(38.0f + ((float)en_lh_note - 56.0f) * 1.2f, 18.0f, 44.0f) - ui.lh_x) * e;
    ui.neck_y += (fm_clampf(20.0f + ((float)en_bass_note - 28.0f) * 1.3f, 20.0f, 58.0f) - ui.neck_y) * e;
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
    case B_ENV: L("THE MIX"); L("PIANO, BASS, DRUMS, CROWD"); break;
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
static const char *const ROLE_WORDS[] = {"Counting in", "The intro", "The head", "Piano solo", "Bass solo",
                                          "Trading fours", "Trading fours", "The head out", "The tag", "The last chord"};
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
    ui.spot = 120.0f;
    ui.spot_w = 78.0f;
    ui.rh_x = 66.0f;
    ui.lh_x = 36.0f;
    ui.neck_y = 36.0f;
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
