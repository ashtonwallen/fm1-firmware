/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Tavern: ashtonwallen, 2026 */
/* TAVERN's UI: the panel and the screen. Part of the unity build (after gfx.c, project.c and world.c); the host
 * simulator includes it too. AMB-1's drawing helpers (rounded boxes, discs, knob rings), its help cards and its
 * messages, around a new screen: the tavern's corner.
 *
 *   PLAY           the session plays / pauses (the night waits)
 *   REC            stand the house a round (once a tavern hour)
 *   EDIT           call for a tune (between tunes: now)
 *   keys           ask for the next tune in that key
 *   HOME ENV ARP   the pages: the tavern, the regulars, memorable nights
 *   FX LFO GLO     the knobs: mix, room, setup (SELECT steps through all six)
 *   KNOB 1-4       the page's four values (on the tavern: the mix)
 *   PRESETS        the preset (1.1): the session's style (project.c PRESET[]); the regulars and the night go on
 *   SAVE           saves (it also saves by itself, rarely, between tunes) */
#include "fastmath.h"

enum { V_TAVERN, V_REGULARS, V_NIGHTS, V_MIX, V_ROOM, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Tavern", "Regulars", "Nights", "Mix", "Room", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_ARP, B_FX, B_LFO, B_GLO};
enum { K_LEDS = 100, K_CPU, K_NIGHT, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_LEAD, P_BACK, P_DRUM, P_ECHO}, {P_LEAD, P_BACK, P_DRUM, P_ECHO}, {P_LEAD, P_BACK, P_DRUM, P_ECHO},
    {P_LEAD, P_BACK, P_DRUM, P_ECHO}, {P_REVERB, P_ROOM, P_TONE, K_NIGHT}, {P_TUNE, P_MIDI, K_LEDS, K_CPU},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Melody", "Backing", "Bodhran", "Echo"}, {"Melody", "Backing", "Bodhran", "Echo"},
    {"Melody", "Backing", "Bodhran", "Echo"}, {"Melody", "Backing", "Bodhran", "Echo"},
    {"Reverb", "Room", "Tone", "Night"},      {"Tune", "MIDI out", "Lights", "CPU"},
};
static int knob_view(int v) { return v >= V_MIX; }   /* a page of knobs (the others turn the mix's) */

/* the screen's colours: a tavern corner by lamplight */
#define K_BG RGB(30, 21, 16)
#define K_PANEL RGB(54, 38, 28)
#define K_LINE RGB(84, 62, 44)
#define K_DIM RGB(160, 132, 100)
#define K_TEXT RGB(246, 230, 202)
#define K_ACC RGB(255, 188, 88)                /* the lamp: the leader, the page, the knob being turned */
#define K_LOW RGB(214, 96, 70)
#define K_GOOD RGB(132, 186, 96)
#define K_PAD K_ACC
#define K_PAD_T K_LINE
/* the room */
#define R_PLASTER RGB(96, 64, 44)
#define R_WAIN RGB(68, 44, 31)
#define R_PLANK RGB(56, 36, 25)
#define R_FLOOR RGB(50, 35, 25)
#define R_BOARD RGB(40, 28, 20)
#define R_TABLE RGB(136, 94, 56)
#define R_TABLE_D RGB(92, 61, 36)
#define R_WOOD RGB(110, 70, 40)
#define R_DARKWOOD RGB(64, 40, 26)
#define R_GLASS RGB(196, 196, 184)
#define R_ALE RGB(214, 150, 50)
#define R_FOAM RGB(244, 236, 214)

static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];               /* detents in the current quick run, per knob (accel) */
    uint32_t down_t[NB];               /* when each button went down */
    uint32_t turn_t;                   /* the last knob turn */
    uint8_t help;                      /* a button's card is up: the button + 1 */
    uint8_t dirty;                     /* the settings changed since the last save */
    uint32_t act_t, saved_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;                    /* the knob turned last (0..3), shown bright for a moment */
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];                   /* what each band shows: redrawn when it changes */
} ui;

/* ------------------------------------------------------------ messages --- */
static uint8_t sp_said;                 /* a message went to a screen reader this frame */
static void msg_text(const char *a, const char *b, char *t)   /* the message as a reader gets it: "a b" */
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
    ui.msg_until = plat_ms() + 1300u;
    if (flags && ann_on()) {
        msg_text(a, b, t);
        ann_say(t, 0, flags);
        sp_said = 1;
    }
}
static void say(const char *a, const char *b) { say_as(a, b, ANN_NOW); }
void ui_say(const char *a, const char *b) { say(a, b); }
void say_event(const char *t)          /* (world.c) something happened: a screen reader hears it, queued */
{
    if (ann_on())
        ann_say(t, 0, 0);
}

static void itoa_u(uint32_t v, char *b)
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
}
static char *cat_s(char *b, const char *s)      /* s onto b; the end */
{
    while ((*b = *s++))
        b++;
    return b;
}
static void mark_dirty(void) { ui.dirty = 1; }
int ui_dirty(void) { return ui.dirty != 0 || ws.dirty; }

/* ------------------------------------------------------------ values --- */
static int knob_get(int k)
{
    if (k < P_NSAVED)
        return proj.par[k];
    if (k == K_LEDS)
        return proj.leds;
    if (k == K_CPU)
        return (int)plat_cpu_pct();
    if (k == K_NIGHT)
        return proj.night_len;
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NSAVED) {
        *lo = en_param_info(k)->lo;
        *hi = en_param_info(k)->hi;
    } else {
        *lo = 0;
        *hi = k == K_LEDS ? 2 : k == K_CPU ? 100 : k == K_NIGHT ? 5 : 0;
    }
}
static void knob_text(int k, char *b)
{
    static const char *const NIGHT[6] = {"20 min", "30 min", "45 min", "1 hour", "90 min", "2 hours"};
    int v = knob_get(k);
    if (k < P_NSAVED) {
        en_param_text(k, v, b);
    } else if (k == K_LEDS) {
        cat_s(b, v == 2 ? "Keys" : v ? "On" : "Off");
    } else if (k == K_CPU) {
        itoa_u((uint32_t)v, b);
        cat_s(b + (v >= 100 ? 3 : v >= 10 ? 2 : 1), "%");
    } else if (k == K_NIGHT) {
        cat_s(b, NIGHT[v % 6]);
    } else {
        b[0] = 0;
    }
}
static void knob_set(int k, int v)
{
    int lo, hi;
    if (k == K_CPU || k == K_NONE)
        return;
    knob_range(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (v == knob_get(k))
        return;
    if (k < P_NSAVED) {
        proj.par[k] = (int16_t)v;
        en_set(k, preset_par(k, v));
    } else if (k == K_LEDS) {
        proj.leds = (uint8_t)v;
    } else if (k == K_NIGHT) {
        proj.night_len = (uint8_t)v;
    }
    mark_dirty();
}

/* knob acceleration by speed (X0X's): a few clicks are one step each however quick; a longer quick turn speeds
 * up to 8 a detent. Small ranges never accelerate. */
static uint8_t accel_off;              /* the host simulator's "spin": exact steps */
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
static void caps(char *d, const char *s, int n);
static void preset_step(int d)          /* PRESETS: the next or the last preset, round (from the next tune on: the
                                         * kinds, modes, tempo, songs; now: the room, the band's balance, the air) */
{
    char t[24];
    proj.preset = (uint8_t)((proj.preset + NPRESET + d) % NPRESET);
    project_apply();
    world_restyle();
    mark_dirty();
    caps(t, preset_name(proj.preset), sizeof t);
    say("PRESET", t);
}

static void turn(int role, int k, int32_t e)
{
    int lo, hi;
    if (!e || k == K_NONE || k == K_CPU)
        return;
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------ buttons --- */
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
    case B_PLAY:
        if (en_playing) {
            en_play(0);
            say("PAUSED", "THE NIGHT WAITS");
        } else {
            en_seed(plat_ms());
            en_play(1);
            say("PLAYING", 0);
        }
        break;
    case B_REC: world_round(); say_as("A ROUND", "FOR THE HOUSE", 0); break;
    case B_EDIT: world_call(); say_as("A TUNE", "IS CALLED FOR", 0); break;
    case B_SAVE:
        if (project_save() == 0) {
            ui.dirty = 0;
            ws.dirty = ws.urgent = 0;
            ui.saved_t = plat_ms();
            say("SAVED", 0);
        } else {
            say_as("NOT SAVED", "FLASH ERROR", ANN_NOW | ANN_KEEP);
        }
        break;
    default: break;
    }
}

/* MIDI in (USB and the TRS jack): a note asks for the next tune's key; clock and start / stop drive the grid */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0)) {
        uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)
            en_clock(st == 0xF8u ? EN_CLK_TICK : st == 0xFAu ? EN_CLK_START : st == 0xFBu ? EN_CLK_CONTINUE : EN_CLK_STOP);
        else if (st == 0x90u && v)
            world_key((int)(n % 12u));
    }
}

/* HELP CARDS (X0X's): hold one button alone and a card says what it does */
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
        if ((ch & keys) >> i & 1u) {
            world_key((int)((5u + i) % 12u));    /* key 0 is F3 */
            say_as("NEXT TUNE IN", EN_NOTE_NAME[(5u + i) % 12u], 0);
        }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {
        int v = (int)ui.view + (e > 0 ? 1 : -1);
        if (v >= 0 && v < NVIEWS)
            set_view(v);
        ui.turn_t = plat_ms();
    }
    (void)plat_enc(EN_ALGO);
    if ((e = plat_enc(EN_PRESET)) != 0) {
        preset_step(e > 0 ? 1 : -1);
        ui.act_t = ui.turn_t = plat_ms();
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            if (!knob_view(ui.view))
                set_view(V_MIX);                 /* the tavern's knobs are the mix's: show them */
            turn(EN_K1 + (int)i, VIEW_KNOB[ui.view][i], e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 1200u;
            ui.act_t = ui.turn_t = plat_ms();
        }
    help_tick(btn, keys);
}

/* ------------------------------------------------------------- drawing --- */
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

/* a soft glow: a disc of colour c fading from a at its middle to nothing at radius r */
static void glow(int32_t cx, int32_t cy, int32_t r, uint16_t c, uint32_t a)
{
    int32_t x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++) {
            int32_t d2 = x * x + y * y;
            if (d2 < r * r)
                px_blend(cx + x, cy + y, c, a * (uint32_t)(r * r - d2) / (uint32_t)(r * r));
        }
}

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

static void arc(int32_t cx, int32_t cy, int32_t r, float v, uint16_t track, uint16_t fill, uint16_t bg, int bipolar)
{
    const float th = 3.4f, a0 = 0.375f, sweep = 0.75f;
    float ro = (float)r, ri = ro - th, rm = ro - th * 0.5f, fx = (float)cx + 0.5f, fy = (float)cy + 0.5f, lo, hi;
    int32_t x, y;
    v = fm_clampf(v, 0.0f, 1.0f);
    lo = bipolar ? (v < 0.5f ? v : 0.5f) : 0.0f;
    hi = bipolar ? (v < 0.5f ? 0.5f : v) : v;
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
            px_blend(cx + x, cy + y, (t >= lo && t <= hi) ? fill : track, (uint32_t)(a * 16.0f + 0.5f));
        }
    {
        float e0 = a0 * FM_TWO_PI, e1 = (a0 + sweep) * FM_TWO_PI, ev = (a0 + sweep * v) * FM_TWO_PI;
        float el = (a0 + sweep * lo) * FM_TWO_PI;
        disc(fx + fm_cosf(e0) * rm - 0.5f, fy + fm_sinf(e0) * rm - 0.5f, th * 0.5f, lo <= 0.0f && hi > 0.0f ? fill : track);
        disc(fx + fm_cosf(e1) * rm - 0.5f, fy + fm_sinf(e1) * rm - 0.5f, th * 0.5f, hi >= 1.0f ? fill : track);
        if (hi > lo)
            disc(fx + fm_cosf(el) * rm - 0.5f, fy + fm_sinf(el) * rm - 0.5f, th * 0.5f, fill);
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

static uint16_t mix(uint16_t a, uint16_t b, int t)   /* t 0..16: a -> b */
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
static int q16(float x) { return x <= 0.0f ? 0 : x >= 1.0f ? 16 : (int)(x * 16.0f + 0.5f); }

/* the battery (plat_battery): 0 (low) .. 3 bars, 4 a USB host powers the FM-1, -1 not read yet */
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
        cv_rect(x + 2 + i * 5, 11, 4, 6, b <= 1 ? K_LOW : K_GOOD);
}

/* ------------------------------------------------------------- header --- */
static const char *const PHASE_NAME[5] = {"early", "busy", "late", "closing", "closed"};
static void draw_header(void)
{
    char t[16], n[16];
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(2166136261u, proj.world.minute | (uint32_t)proj.world.night << 12),
                      en_playing | (uint32_t)msg << 1 | (uint32_t)(batt() + 1) << 4 | (uint32_t)ui_dirty() << 8 |
                      (uint32_t)world_phase() << 12);
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1]), ui.msg_until);
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
        if (!en_playing) {                        /* paused: two bars */
            cv_rect(x - 8, 8, 3, 12, K_ACC);
            cv_rect(x - 3, 8, 3, 12, K_ACC);
            x -= 14;
        }
        clock_text(t);
        w = cv_text(8, 5, &FONT_B, t, K_TEXT);
        cat_s(cat_s(n, "NIGHT "), "");
        itoa_u(proj.world.night, n + 6);
        w = cv_text(w + 8, 8, &FONT_XS, n, K_DIM);
        if (w + 6 + text_w(&FONT_XS, PHASE_NAME[world_phase()]) < x)
            cv_text(w + 6, 8, &FONT_XS, PHASE_NAME[world_phase()], K_ACC);
    }
    cv_rect(0, HDR_H - 1, 240, 1, K_LINE);
    cv_blit(0, 0);
}

/* -------------------------------------------------------------- the room --- */
static const uint16_t HAIR[11] = {RGB(176, 176, 172), RGB(196, 84, 40), RGB(112, 72, 42), RGB(226, 196, 120),
                                  RGB(44, 32, 26), RGB(146, 62, 42), RGB(76, 50, 32), RGB(216, 128, 56),
                                  RGB(94, 62, 40), RGB(206, 206, 196), RGB(60, 40, 30)};
static const uint16_t SHIRT[12] = {RGB(124, 84, 52), RGB(76, 128, 76), RGB(76, 106, 156), RGB(222, 210, 178),
                                   RGB(172, 64, 54), RGB(116, 76, 128), RGB(58, 66, 108), RGB(192, 152, 64),
                                   RGB(104, 114, 92), RGB(154, 94, 62), RGB(84, 124, 124), RGB(134, 134, 144)};
static const uint16_t SKIN[3] = {RGB(238, 198, 162), RGB(222, 172, 132), RGB(176, 128, 94)};
#define TY 104                                   /* the table's top */
#define SEAT_X(i) (14 + (i) * 19)

static int playing_now(int i)                    /* sitting in on this tune, and the engine has them in */
{
    const ch_t *c = &proj.world.ch[i];
    return world_state() == 1 && c->play && (en_live >> c->inst & 1u);
}

/* an instrument in a player's hands: a the note's glow (0..1), so the bow moves, the box breathes */
static void draw_inst(int inst, int32_t cx, int32_t hy, float a, int live, uint16_t skin)
{
    int32_t k = live ? (int32_t)(a * 3.0f + 0.5f) : 0;
    switch (inst) {
    case I_FIDDLE:                               /* under the chin, the bow across */
        cv_round(cx - 10, hy + 4, 8, 5, 2, RGB(150, 76, 36));
        cv_rect(cx - 7, hy + 5, 1, 3, RGB(70, 36, 20));
        cv_line(cx - 13, hy + 1 + k, cx + 6, hy + 10 - k, RGB(214, 196, 156));
        cv_rect(cx + 5, hy + 9 - k, 2, 2, skin);
        break;
    case I_WHISTLE:
        cv_line(cx + 1, hy + 3, cx + 4, hy + 15, RGB(206, 206, 214));
        cv_line(cx + 2, hy + 3, cx + 5, hy + 15, RGB(150, 150, 160));
        cv_rect(cx + 2, hy + 7, 2, 2, skin);
        cv_rect(cx + 3, hy + 11, 2, 2, skin);
        break;
    case I_HARP:                                 /* beside them: the pillar, the neck, the strings */
        cv_rect(cx + 9, hy - 9, 2, TY - hy + 9, RGB(176, 130, 60));
        cv_line(cx + 10, hy - 9, cx + 3, hy - 4, RGB(176, 130, 60));
        cv_line(cx + 3, hy - 4, cx + 6, TY - 1, RGB(132, 84, 40));
        {
            int32_t s;
            for (s = 0; s < 3; s++)
                cv_line(cx + 5 + s, hy - 5 + s, cx + 7 + s / 2, TY - 2,
                        live && a > 0.3f && s == (int32_t)(a * 7.0f) % 3 ? K_ACC : RGB(220, 206, 170));
        }
        cv_rect(cx + 4, hy + 8 - k, 2, 2, skin);
        break;
    case I_LUTE:
        cv_line(cx - 2, hy + 11, cx - 12, hy + 2, RGB(70, 44, 26));
        cv_disc(cx + 2, hy + 14, 5, RGB(156, 98, 50));
        cv_disc(cx + 2, hy + 14, 1, RGB(50, 30, 18));
        cv_rect(cx + 4, hy + 10 - k, 2, 2, skin);
        break;
    case I_BOX: {                                /* the bellows open and close */
        int32_t w = 14 + k;
        cv_rect(cx - w / 2 - 2, hy + 9, 4, 9, RGB(170, 44, 44));
        cv_rect(cx + w / 2 - 2, hy + 9, 4, 9, RGB(170, 44, 44));
        {
            int32_t x;
            for (x = cx - w / 2 + 2; x < cx + w / 2 - 2; x += 2)
                cv_rect(x, hy + 10, 1, 7, RGB(60, 20, 20));
        }
        cv_rect(cx - w / 2 - 1, hy + 11, 1, 1, R_FOAM);
        cv_rect(cx + w / 2, hy + 11, 1, 1, R_FOAM);
        break;
    }
    case I_BASS:                                 /* standing beside them, taller than they are */
        cv_round(cx + 6, hy + 2, 9, TY - hy - 2, 3, RGB(132, 70, 34));
        cv_rect(cx + 10, hy - 16, 2, 18, RGB(50, 30, 18));
        cv_disc(cx + 11, hy - 17, 1, RGB(50, 30, 18));
        cv_rect(cx + 8, hy + 6, 1, 1, RGB(30, 18, 10));
        cv_rect(cx + 12, hy + 6, 1, 1, RGB(30, 18, 10));
        cv_rect(cx + 5, hy + 12 + k, 2, 2, skin);
        break;
    case I_DRUM:                                 /* the bodhran on the knee, the tipper */
        cv_disc(cx - 4, hy + 13, 6, RGB(112, 72, 40));
        cv_disc(cx - 4, hy + 13, 5, RGB(226, 206, 162));
        cv_line(cx + 5, hy + 5 + k, cx + 1, hy + 12 + k, RGB(60, 36, 22));
        cv_rect(cx + 4, hy + 5 + k, 2, 2, skin);
        break;
    default: break;
    }
}

/* someone at the table: head, hair, shirt, instrument; bobbing when they play */
static void draw_person(int i, int32_t cx, int leader)
{
    const ch_t *c = &proj.world.ch[i];
    int live = playing_now(i), sing = leader && world_type() == 1 && world_state() == 1;
    float a = live || sing ? en_glow[sing ? I_VOICE : c->inst] : 0.0f;
    int32_t hy = 76 + (live && a > 0.55f ? 1 : 0);
    uint16_t skin = SKIN[ch_skin(i) % 3], hair = HAIR[ch_hair(i) % 11], shirt = SHIRT[ch_shirt(i) % 12];
    if (leader) {                                /* the lamp finds the leader */
        glow(cx, hy + 8, 18, K_ACC, 6);
        cv_rect(cx - 2, hy - 15, 5, 1, K_ACC);
        cv_rect(cx - 1, hy - 14, 3, 1, K_ACC);
        cv_rect(cx, hy - 13, 1, 1, K_ACC);
    }
    if (c->inst == I_BASS)
        draw_inst(c->inst, cx, hy, a, live, skin);
    cv_round(cx - 6, hy + 5, 13, TY - hy - 5, 3, shirt);    /* the body */
    cv_disc(cx, hy, 4, skin);                    /* the head */
    cv_rect(cx - 4, hy - 4, 9, 2, hair);         /* the hair */
    cv_rect(cx - 4, hy - 2, 2, 3, hair);
    if (ch_hair(i) == 1 || ch_hair(i) == 3 || ch_hair(i) == 5)
        cv_rect(cx + 3, hy - 2, 2, 5, hair);     /* long */
    if (i == 0)                                  /* Old Tom's cap */
        cv_rect(cx - 5, hy - 5, 11, 2, RGB(70, 62, 52));
    cv_rect(cx - 2, hy, 1, 1, RGB(40, 26, 20));  /* eyes */
    cv_rect(cx + 2, hy, 1, 1, RGB(40, 26, 20));
    if (sing) {                                  /* singing: an open mouth, a note in the air */
        cv_rect(cx, hy + 2, 2, 2, RGB(90, 30, 30));
        if (a > 0.2f) {
            cv_rect(cx + 8, hy - 12, 1, 6, K_ACC);
            cv_rect(cx + 8, hy - 12, 3, 1, K_ACC);
            cv_disc(cx + 7, hy - 6, 1, K_ACC);
        }
    } else if (c->mood > 40) {
        cv_rect(cx - 1, hy + 2, 3, 1, RGB(120, 60, 50));   /* a smile */
    } else if (c->mood < -40) {
        cv_rect(cx - 2, hy - 2, 2, 1, RGB(40, 26, 20));    /* a frown */
        cv_rect(cx + 2, hy - 2, 2, 1, RGB(40, 26, 20));
    }
    if (c->inst != I_BASS && !(sing && c->inst != I_HARP))
        draw_inst(c->inst, cx, hy, a, live, skin);
    if (c->state == S_MEND)                       /* mending: the broken string hangs loose */
        cv_line(cx - 9, hy + 6, cx - 13, hy + 16, K_LOW);
}

/* someone standing at the bar */
static void draw_standing(int i, int32_t cx)
{
    uint16_t skin = SKIN[ch_skin(i) % 3], hair = HAIR[ch_hair(i) % 11], shirt = SHIRT[ch_shirt(i) % 12];
    cv_rect(cx - 3, 96, 3, 12, RGB(46, 40, 44));
    cv_rect(cx + 1, 96, 3, 12, RGB(46, 40, 44));
    cv_round(cx - 6, 72, 13, 26, 3, shirt);
    cv_disc(cx, 67, 4, skin);
    cv_rect(cx - 4, 63, 9, 2, hair);
    cv_rect(cx + 6, 78, 2, 2, skin);             /* a hand on the pint */
}

static void draw_room(void)
{
    const world_t *w = &proj.world;
    int i, m = w->minute, ldr = world_leader();
    int32_t x;
    uint16_t sky, sky2;
    cv_rect(0, 0, 240, 52, R_PLASTER);           /* the wall */
    cv_rect(0, 52, 240, TY - 52, R_WAIN);
    for (x = 6; x < 240; x += 14)
        cv_rect(x, 54, 1, TY - 54, R_PLANK);
    cv_rect(0, 51, 240, 2, R_DARKWOOD);
    cv_rect(0, TY, 240, MAIN_H - TY, R_FLOOR);   /* the floor */
    for (i = 0; i < 4; i++)
        cv_rect(0, TY + 10 + i * 9, 240, 1, R_BOARD);
    /* the window: the sky by the hour */
    if (m < 60) {
        sky = RGB(108, 92, 150);
        sky2 = RGB(214, 132, 92);
    } else if (m < 120) {
        sky = RGB(52, 50, 104);
        sky2 = RGB(120, 80, 110);
    } else {
        sky = RGB(18, 22, 48);
        sky2 = RGB(26, 30, 62);
    }
    cv_rect(10, 8, 40, 38, R_DARKWOOD);
    cv_rect(13, 11, 34, 16, sky);
    cv_rect(13, 27, 34, 16, sky2);
    if (m >= 100) {                              /* the moon climbs, stars */
        int32_t mx = 18 + (m - 100) * 26 / (MIN_DAWN - 100), my = 30 - (m - 100) * 14 / (MIN_DAWN - 100);
        cv_disc(mx, my, 3, RGB(236, 232, 200));
        cv_rect(40, 14, 1, 1, RGB(220, 220, 200));
        cv_rect(22, 36, 1, 1, RGB(200, 200, 190));
        cv_rect(44, 33, 1, 1, RGB(200, 200, 190));
        cv_rect(31, 16, 1, 1, RGB(220, 220, 200));
    }
    cv_rect(29, 11, 2, 32, R_DARKWOOD);
    cv_rect(13, 26, 34, 2, R_DARKWOOD);
    /* a picture of a ship, slightly crooked, and the lamp */
    cv_rect(70, 14, 26, 18, R_DARKWOOD);
    cv_rect(72, 16, 22, 14, RGB(120, 140, 150));
    cv_rect(72, 25, 22, 5, RGB(60, 90, 120));
    cv_rect(82, 18, 1, 8, RGB(60, 40, 30));
    cv_rect(78, 19, 4, 5, R_FOAM);
    cv_rect(83, 19, 4, 5, R_FOAM);
    cv_rect(124, 0, 1, 10, RGB(40, 30, 24));
    if (w->minute < MIN_SHUT) {
        glow(124, 15, 30, RGB(255, 200, 110), 7);
        cv_disc(124, 14, 4, RGB(255, 214, 130));
    } else {
        cv_disc(124, 14, 4, RGB(120, 96, 60));
    }
    /* the bar: the shelf, bottles, Hugh, the counter */
    cv_rect(180, 0, 60, 82, RGB(78, 52, 36));
    cv_rect(182, 24, 56, 2, R_DARKWOOD);
    cv_rect(182, 46, 56, 2, R_DARKWOOD);
    {
        static const uint16_t BOT[6] = {RGB(60, 110, 60), RGB(140, 60, 40), RGB(200, 170, 90), RGB(60, 70, 120),
                                        RGB(110, 50, 70), RGB(170, 140, 60)};
        for (i = 0; i < 9; i++) {
            cv_rect(185 + i * 6, 14 + (i % 3 == 1 ? 1 : 0), 4, 10 - (i % 3 == 1 ? 1 : 0), BOT[i % 6]);
            cv_rect(186 + i * 6, 11 + (i % 3 == 1 ? 1 : 0), 2, 3, BOT[i % 6]);
            if (i < 7)
                cv_rect(186 + i * 7, 38, 5, 8, BOT[(i + 3) % 6]);
        }
    }
    {   /* Hugh: bald, a moustache, an apron; a whistle after hours, a broom when it's all over */
        int32_t hx = 226, hy = 58, hugh_plays = ldr == NCH && world_state() == 1;
        cv_round(hx - 7, hy + 6, 15, 20, 3, RGB(90, 86, 96));
        cv_rect(hx - 5, hy + 10, 11, 16, RGB(226, 222, 210));
        cv_disc(hx, hy, 5, SKIN[1]);
        cv_rect(hx - 3, hy + 2, 7, 1, RGB(120, 90, 70));
        cv_rect(hx - 2, hy - 1, 1, 1, RGB(40, 26, 20));
        cv_rect(hx + 2, hy - 1, 1, 1, RGB(40, 26, 20));
        if (hugh_plays) {
            glow(hx, hy + 8, 16, K_ACC, 6);
            draw_inst(I_WHISTLE, hx, hy, en_glow[I_WHISTLE], 1, SKIN[1]);
        } else if (w->minute >= MIN_SHUT) {
            cv_line(hx - 10, hy + 4, hx - 16, hy + 40, RGB(150, 110, 60));
        }
    }
    cv_rect(178, 82, 62, 3, R_TABLE);            /* the counter */
    cv_rect(178, 85, 62, TY - 85 + 6, R_DARKWOOD);
    for (x = 184; x < 240; x += 12)
        cv_rect(x, 88, 1, TY - 88 + 2, R_WAIN);
    {   /* those at the bar, in front of it */
        int k = 0;
        for (i = 0; i < NCH && k < 3; i++)
            if (w->ch[i].state == S_BAR) {
                int32_t bx = 190 + k * 16;
                draw_standing(i, bx);
                cv_rect(bx + 6, 75, 4, 7, R_ALE);
                cv_rect(bx + 6, 74, 4, 1, R_FOAM);
                k++;
            }
    }
    /* the table and those at it */
    for (i = 0; i < NCH; i++) {
        const ch_t *c = &w->ch[i];
        int32_t cx = SEAT_X(i);
        if (c->state == S_SEAT || c->state == S_MEND) {
            draw_person(i, cx, i == ldr && world_state() == 1);
        } else {                                 /* an empty chair */
            cv_rect(cx - 5, TY - 20, 2, 20, R_DARKWOOD);
            cv_rect(cx + 4, TY - 20, 2, 20, R_DARKWOOD);
            cv_rect(cx - 5, TY - 18, 11, 2, R_DARKWOOD);
            cv_rect(cx - 5, TY - 12, 11, 2, R_DARKWOOD);
        }
    }
    cv_rect(4, TY, 174, 5, R_TABLE);
    cv_rect(4, TY + 5, 174, 3, R_TABLE_D);
    cv_rect(10, TY + 8, 3, 14, R_TABLE_D);
    cv_rect(168, TY + 8, 3, 14, R_TABLE_D);
    for (i = 0; i < NCH; i++) {                  /* the pints: emptier the thirstier */
        const ch_t *c = &w->ch[i];
        int32_t cx = SEAT_X(i) + 6, lv = (100 - c->thirst) * 7 / 100;
        if (c->state != S_SEAT && c->state != S_MEND)
            continue;
        cv_rect(cx, TY - 8, 4, 8, R_GLASS);
        cv_rect(cx + 1, TY - 7, 2, 7, RGB(150, 150, 140));
        if (lv > 0) {
            cv_rect(cx, TY - lv, 4, lv, R_ALE);
            cv_rect(cx, TY - lv - 1, 4, 1, R_FOAM);
        }
    }
    for (i = 0; i < NCH; i++) {                  /* the names, two rows so they fit */
        const ch_t *c = &w->ch[i];
        uint16_t col;
        if (c->state == S_HOME || c->state == S_GONE)
            continue;
        col = i == ldr && world_state() == 1 ? K_ACC : c->state == S_MEND ? K_LOW : c->state == S_BAR ? K_DIM
              : playing_now(i) ? K_TEXT : RGB(206, 186, 156);
        {
            int32_t nx = SEAT_X(i), hw = text_w(&FONT_T, ch_name(i)) / 2 + 2;
            text_c(nx < hw ? hw : nx > 240 - hw ? 240 - hw : nx, TY + 10 + (i & 1) * 12, &FONT_T, ch_name(i), col);
        }
    }
    if (w->minute >= MIN_SHUT && world_state() != 1)
        text_c(90, 70, &FONT_B, "Closed", RGB(206, 186, 156));
}

/* the regulars: each one's state of mind and glass */
static void draw_regulars(void)
{
    static const char *const ST[5] = {"not in yet", "at the table", "at the bar", "mending", "gone home"};
    const world_t *w = &proj.world;
    int i;
    for (i = 0; i < NCH; i++) {
        const ch_t *c = &w->ch[i];
        int32_t y = i * 16, mx = 132, mw, r;
        int here = c->state == S_SEAT || c->state == S_BAR || c->state == S_MEND;
        const char *st = playing_now(i) ? (i == world_leader() ? "leading" : "playing") : ST[c->state % 5];
        if (i == VISITOR && c->arrive == 99 && c->state == S_GONE)
            st = "nobody tonight";
        if (i == VISITOR && c->state == S_GONE && c->arrive != 99 && !c->nights && !(c->flags & CF_ASKED))
            st = "not in yet";
        if (i & 1)
            cv_rect(0, y, 240, 16, K_PANEL);
        cv_rect(4, y + 3, 6, 10, SHIRT[ch_shirt(i) % 12]);
        cv_text(14, y + 1, &FONT_XS, ch_name(i), here ? (playing_now(i) ? K_ACC : K_TEXT) : K_DIM);
        cv_text(68, y + 3, &FONT_T, EN_INST_NAME[c->inst], K_DIM);
        cv_rect(mx - 15, y + 7, 30, 2, K_LINE);  /* mood: from the middle, green up, rust down */
        mw = c->mood * 15 / 100;
        if (mw > 0)
            cv_rect(mx, y + 6, mw, 4, K_GOOD);
        else if (mw < 0)
            cv_rect(mx + mw, y + 6, -mw, 4, K_LOW);
        cv_rect(mx, y + 4, 1, 8, K_DIM);
        cv_rect(153, y + 3, 5, 10, R_GLASS);     /* the glass */
        r = (100 - c->thirst) * 9 / 100;
        if (r > 0)
            cv_rect(153, y + 13 - r, 5, r, R_ALE);
        cv_text(164, y + 3, &FONT_T, st, here ? RGB(214, 196, 166) : K_DIM);
        if (i < VISITOR && ch_rival(i) >= 0 && here && present(ch_rival(i)))
            cv_rect(236, y + 5, 3, 6, K_LOW);   /* their rival is in */
    }
}

/* memorable nights */
static void draw_nights(void)
{
    const world_t *w = &proj.world;
    char t[48], n[8];
    int k;
    cat_s(t, "NIGHT ");
    itoa_u(w->night, t + 6);
    cv_text(8, 3, &FONT_XS, t, K_ACC);
    itoa_u(w->tunes, n);
    cat_s(cat_s(t, n), " TUNES PLAYED");
    cv_text(232 - text_w(&FONT_XS, t), 3, &FONT_XS, t, K_DIM);
    cv_rect(8, 20, 224, 1, K_LINE);
    if (!w->nmem)
        text_c(120, 60, &FONT_XS, "Nothing to tell yet.", K_DIM);
    for (k = 0; k < w->nmem && k < NMEM; k++) {
        int32_t y = 25 + k * 15;
        n[0] = 'N';
        itoa_u(w->mem[k].night, n + 1);
        cv_text(8, y + 2, &FONT_T, n, K_DIM);
        mem_text(&w->mem[k], t, (int)sizeof t);
        cv_text(36, y, &FONT_XS, t, k == 0 ? K_TEXT : RGB(214, 196, 166));
    }
}

/* the tune (its name, its kind and key, where in it the band is) and tonight's last two events */
static void tune_line(char *t)
{
    static const char *const TYPE[5] = {0, "song", "lament", "last tune", "air"};
    char *p = t;
    int st = world_state();
    t[0] = 0;
    if (st != 1)
        return;
    p = cat_s(p, TYPE[world_type()] ? TYPE[world_type()] : EN_KIND_NAME[world_kind()]);
    p = cat_s(p, ", ");
    p = cat_s(p, EN_NOTE_NAME[world_root()]);
    p = cat_s(p, " ");
    p = cat_s(p, EN_MODE_NAME[world_mode()]);
    if (en_sess == 1) {
        p = cat_s(p, en_part ? ", B part" : ", A part");
        if (en_rep)
            p = cat_s(p, " again");
        if (en_passes > 1) {
            char n[4];
            p = cat_s(p, ", ");
            n[0] = (char)('1' + en_pass);
            n[1] = 0;
            p = cat_s(p, n);
            p = cat_s(p, " of ");
            n[0] = (char)('0' + en_passes);
            p = cat_s(p, n);
        }
    } else if (en_sess == 2) {
        cat_s(p, ", the last note");
    }
}

static int slen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

/* s into a (LOG_W + 4), up to the last word that fits px wide ("..." after it when dots and it's cut);
 * returns where it was cut (its length when it all fits) */
static int fit_words(const char *s, char *a, int px, int dots)
{
    int n = slen(s), cut, i;
    if (n > LOG_W - 1)
        n = LOG_W - 1;
    memcpy(a, s, (uint32_t)n);
    a[n] = 0;
    cut = n;
    while (text_w(&FONT_XS, a) > px) {
        for (i = cut - 1; i > 0 && s[i] != ' '; i--)
            ;
        if (i <= 0)
            break;
        cut = i;
        a[cut] = 0;
        if (dots) {
            memcpy(a + cut, "...", 4);
            if (text_w(&FONT_XS, a) <= px)
                break;
            a[cut] = 0;
        }
    }
    return cut;
}

static void draw_info(void)
{
    const world_t *w = &proj.world;
    char t[48];
    const char *nm = world_tune();
    uint32_t h = hash(hash(2166136261u, (uint32_t)w->log_min[0] | (uint32_t)w->log[0][0] << 16 | (uint32_t)w->log[0][1] << 24),
                      (uint32_t)world_state() | (uint32_t)en_sess << 2 | (uint32_t)en_part << 4 | (uint32_t)en_rep << 5 |
                          (uint32_t)en_pass << 6 | (uint32_t)nm[0] << 10 | (uint32_t)w->log[0][5] << 18);
    h = hash(h, (uint32_t)w->log[0][9] | (uint32_t)w->log[0][14] << 8 | (uint32_t)w->log[1][3] << 16);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    cv_rect(0, 0, 240, 1, K_LINE);
    if (world_state() == 1 && nm[0]) {
        cv_text(8, 3, &FONT_B, nm, K_ACC);
        tune_line(t);
        cv_text(8, 20, &FONT_XS, t, K_DIM);
    } else {
        cv_text(8, 3, &FONT_B, world_state() == 2 ? "The doors are shut" : "Between tunes", RGB(206, 186, 156));
        cv_text(8, 20, &FONT_XS, world_state() == 2 ? "Hugh sweeps up." : "Talk, a pint, a tuning peg.", K_DIM);
    }
    cv_rect(8, 37, 224, 1, K_LINE);
    {                                            /* the latest event, in two lines when it needs them */
        char a[LOG_W + 4];
        int n = slen(w->log[0]), cut = fit_words(w->log[0], a, 224, 0);
        cv_text(8, 39, &FONT_XS, a, K_TEXT);
        if (cut < n) {
            fit_words(w->log[0] + cut + 1, a, 216, 1);
            cv_text(16, 53, &FONT_XS, a, K_TEXT);
        } else {
            fit_words(w->log[1], a, 224, 1);
            cv_text(8, 53, &FONT_XS, a, K_DIM);
        }
    }
    cv_blit(0, KNB_Y);
}

/* a button's card (help_tick): its name, then what it does */
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("THE TAVERN"); L("WHO'S IN, WHO LEADS,"); L(" WHAT THEY PLAY"); L("PRESETS: THE STYLE"); break;
    case B_ENV: L("THE REGULARS"); L("MOOD, GLASS, WHERE"); L(" THEY ARE"); break;
    case B_ARP: L("MEMORABLE NIGHTS"); break;
    case B_FX: L("THE MIX"); L("MELODY, BACKING,"); L(" BODHRAN, ECHO"); break;
    case B_LFO: L("THE ROOM"); L("REVERB, ROOM, TONE,"); L(" NIGHT LENGTH"); L("PRESETS: THE STYLE"); break;
    case B_GLO: L("SETUP"); L("TUNE, MIDI OUT, LIGHTS"); break;
    case B_EDIT: L("CALL FOR A TUNE"); L("BETWEEN TUNES: NOW"); break;
    case B_REC: L("A ROUND FOR THE HOUSE"); L("ONCE AN HOUR"); break;
    case B_SAVE: L("SAVES THE TAVERN"); L("IT SAVES BY ITSELF"); L(" NOW AND THEN, TOO"); break;
    case B_OCTDN: case B_OCTUP: L("WITH OCT- AND OCT+,"); L(" 5 S: UPDATE MODE"); break;
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
    cv_round(16, 14, 208, 26, 6, K_TEXT);
    cv_rect(16, 34, 208, 6, K_TEXT);
    text_c(120, 18, &FONT_B, BTN_LABEL[ui.help - 1], K_BG);
    for (k = 0; k < n; k++)
        text_c(120, 56 + k * 20, &FONT_S, l[k] + (l[k][0] == ' '), K_TEXT);
}

static void draw_main(void)
{
    const world_t *w = &proj.world;
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)w->minute << 4 | (uint32_t)world_state() << 20 |
                                       (uint32_t)(world_leader() + 1) << 24), i;
    if (ui.frame % 3u)                           /* 20 frames a second is plenty */
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
    for (i = 0; i < NCH; i++)
        h = hash(h, w->ch[i].state | (uint32_t)w->ch[i].play << 3 | (uint32_t)w->ch[i].thirst << 4 |
                        (uint32_t)(w->ch[i].mood / 8 + 16) << 12 | (uint32_t)w->ch[i].busy << 20);
    h = hash(h, en_live | (uint32_t)world_type() << 8 | (uint32_t)w->nmem << 12 | w->tunes << 16);
    if (ui.view != V_REGULARS && ui.view != V_NIGHTS)
        for (i = 0; i < EN_NINST; i++)
            h = hash(h, (uint32_t)(en_glow[i] * 4.0f));
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    if (ui.view == V_REGULARS)
        draw_regulars();
    else if (ui.view == V_NIGHTS)
        draw_nights();
    else
        draw_room();
    cv_blit(0, MAIN_Y);
}

/* the knob strip (X0X's, AMB-1's): a rule with the page's name, then a cell a knob */
static void draw_knobs(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8 | 0x8000u), i;
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(VIEW_KNOB[ui.view][i]));
    h = hash(h, (uint32_t)now_touch | (uint32_t)proj.preset << 8);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    {
        char t[12];
        int k;
        int32_t xd = 237 - NVIEWS * 6, xt;
        caps(t, VIEW_NAME[ui.view], sizeof t);
        xt = xd - 5 - text_w(&FONT_XS, t);
        {                                        /* the preset, at the left of the rule */
            char pn[24];
            int32_t xp;
            caps(pn, preset_name(proj.preset), sizeof pn);
            xp = cv_text(6, -1, &FONT_XS, pn, K_DIM) + 6;
            cv_rect(xp, 5, xt - 9 - xp, 1, K_LINE);
        }
        cv_text(xt, -1, &FONT_XS, t, K_ACC);
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 6, 3, 4, 4, 2, k == ui.view ? K_ACC : K_LINE);
    }
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v, hot = now_touch && ui.touched == (int)i;
        int32_t x = (int32_t)i * 60, cx = x + 30;
        char t[16];
        uint16_t bg = hot ? K_PANEL : K_BG, fill = hot ? K_TEXT : K_ACC;
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
        if (k != K_CPU && hi - lo < 6) {
            int n = hi - lo + 1, j, pw = n > 4 ? 6 : 9, gap = 3, w0 = n * (pw + gap) - gap;
            for (j = 0; j < n; j++)
                cv_round(cx - w0 / 2 + j * (pw + gap), 33, pw, 8, 2, j == v - lo ? fill : K_LINE);
        } else if (k != K_CPU) {
            arc(cx, 37, 12, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, K_LINE, fill, bg, lo < 0);
        }
        knob_text(k, t);
        text_c(cx, 50, &FONT_B, t, hot ? K_ACC : K_TEXT);
    }
    cv_blit(0, KNB_Y);
}

static void leds(void)
{
    uint32_t b = 0, k = 0, i, ap;
    int root = world_root(), mode = world_mode();
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (world_state() == 1)
        b |= 1u << B_EDIT;                       /* a tune on */
    ap = EN_MODE_POOL[mode % EN_NMODES];
    ap = ((ap << root) | (ap >> (12u - (uint32_t)root))) & 0xFFFu;
    if (proj.leds && world_state() == 1)         /* the keys: the tune's scale */
        for (i = 0; i < NKEYS; i++)
            if (ap >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    plat_glow(proj.leds == 1);
    plat_leds(b, k);
}

/* AUTOSAVE, and the flash's wear: every save erases one of the same two 4 KiB sectors (APP_STORE_MAP's A/B pair),
 * rated about 100k erases, so the tavern saves itself rarely. keep_due is the whole policy:
 *   the world (ws.dirty; something memorable, ws.urgent, a new night: no sooner) at most once an hour after the last
 *     save; nothing changed, no save. Up to an hour of the tavern is lost at a power-off (the FM-1 gives no warning);
 *   a setting (ui.dirty) a few seconds after the knobs rest, at most once every 5 minutes;
 *   on the battery, once it reads empty (0): one save of whatever changed, then none of these until it reads above 0;
 *   SAVE saves at once.
 * When: a flash write can catch the sound for a moment (AMB-1 saved only while resting), so while playing only
 * between tunes, once the last tune's tails have gone; paused, a few seconds after the last touch. Each save resets
 * the clock: it keeps the settings and the world alike. */
#define KEEP_WORLD_MS (60u * 60000u)
#define KEEP_SETTINGS_MS (5u * 60000u)
static uint8_t keep_low;                         /* the empty battery's save done; again once it reads above 0 */
static int keep_due(uint32_t now)                /* 0 not yet, 1 due, 2 due: the empty battery's save */
{
    uint32_t since = now - ui.saved_t;
    if (!ui.dirty && !ws.dirty && !ws.urgent)
        return 0;
    if (batt() == 0 && !keep_low && since >= KEEP_SETTINGS_MS)
        return 2;
    return since >= KEEP_WORLD_MS || (ui.dirty && since >= KEEP_SETTINGS_MS);
}
static void autosave(void)
{
    uint32_t now = plat_ms();
    int due;
    if (batt() != 0)
        keep_low = 0;                            /* (every pass: the battery read above 0 again) */
    if (en_playing ? !world_quiet(now) : now - ui.act_t <= 4000u || ui.btn || ui.keys || en_level >= 0.01f)
        return;
    if (!(due = keep_due(now)))
        return;
    if (project_save() == 0) {
        ui.dirty = 0;
        ws.dirty = ws.urgent = 0;
    }
    ui.saved_t = now;                            /* (a failed write: tried again later, not every pass) */
    if (due == 2)
        keep_low = 1;
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader on the computer (announce.c; fm1-reader): the page as it settles, a knob
 * as it turns, the tavern's events as they happen (world.c, queued), and WHERE: the clock, the tune, who plays. */
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
static void sp_knob(int view, int i)
{
    char t[16];
    sp_cat(KNOB_LABEL[view][i]);
    sp_cat(" ");
    knob_text(VIEW_KNOB[view][i], t);
    sp_cat(t);
}
static void sp_page(void)
{
    int i;
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat(VIEW_NAME[ui.view]);
    sp_cat(" page");
    if (knob_view(ui.view))
        for (i = 0; i < 4; i++) {
            sp_cat(i ? ", " : ": ");
            sp_knob(ui.view, i);
        }
}
static void sp_where(void)                       /* "9:40 PM, night 3. Old Tom plays The Lonesome Heron. 5 playing." */
{
    char t[48];
    int i, n = 0;
    sp_n = 0;
    sp_buf[0] = 0;
    clock_text(t);
    sp_cat(t);
    sp_cat(", night ");
    itoa_u(proj.world.night, t);
    sp_cat(t);
    if (world_state() == 1) {
        sp_cat(". ");
        sp_cat(ch_name(world_leader() < 0 ? 0 : world_leader()));
        sp_cat(world_type() == 1 ? " sings " : " plays ");
        sp_cat(world_tune());
        tune_line(t);
        sp_cat(", ");
        sp_cat(t);
        for (i = 0; i < NCH; i++)
            n += playing_now(i);
        sp_cat(". ");
        itoa_u((uint32_t)n, t);
        sp_cat(t);
        sp_cat(" playing");
    } else {
        sp_cat(world_state() == 2 ? ". Closed" : ". Between tunes");
    }
    if (!en_playing)
        sp_cat(". Paused");
    sp_cat(". Preset ");
    sp_cat(preset_name(proj.preset));
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
        sp_cat(l[k][0] == ' ' ? "" : ". ");
        sp_cat(l[k]);
    }
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
        sp_page();
        ann_say(sp_buf, 0, ANN_NOW);
        sp_where();
        ann_say(sp_buf, 0, 0);
        sp_said = 0;
        return;
    }
    if (sp_said) {
        sp_said = 0;
        sp_snap();
        return;
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
        sp_page();
        ann_later(sp_buf, ANN_NOW, 200u, now);
        return;
    }
    for (i = 0; i < 4 && knob_view(ui.view); i++) {
        int v = knob_get(VIEW_KNOB[ui.view][i]);
        if (v != sp.val[i]) {
            sp.val[i] = (int16_t)v;
            if (changed < 0)
                changed = i;
        }
    }
    if (changed >= 0 && VIEW_KNOB[ui.view][changed] != K_CPU) {
        sp_n = 0;
        sp_knob(ui.view, changed);
        ann_later(sp_buf, ANN_NOW, 120u, now);
    }
}

#ifdef PLAT_HOST
/* every page, knob value and help card as a screen reader gets them (the "speakdump" command) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    project_t keep = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help;
    int v, i, x, lo, hi;
    for (v = 0; v < NVIEWS; v++) {
        ui.view = (uint8_t)v;
        sp_page();
        sp_print("SCREEN", sp_buf);
        for (i = 0; i < 4 && knob_view(v); i++) {
            int k = VIEW_KNOB[v][i];
            knob_range(k, &lo, &hi);
            for (x = lo; x <= hi && k != K_CPU; x++) {
                if (k < P_NSAVED)
                    proj.par[k] = (int16_t)x;
                else if (k == K_LEDS)
                    proj.leds = (uint8_t)x;
                else if (k == K_NIGHT)
                    proj.night_len = (uint8_t)x;
                sp_n = 0;
                sp_knob(v, i);
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
    sp_where();
    sp_print("WHERE", sp_buf);
    for (i = 0; i < NPRESET; i++) {              /* PRESETS: its message */
        char n[24], t[48];
        caps(n, preset_name(i), sizeof n);
        cat_s(cat_s(t, "PRESET "), n);
        sp_print("MSG", t);
    }
}
#endif

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    lcd_fill(0, 0, 240, 240, K_BG);
    world_start();
    if (!en_playing)
        en_play(1);                              /* the session starts by itself */
}

void ui_frame(void)
{
    input();
    world_tick();
    autosave();
    draw_header();
    draw_main();
    if (knob_view(ui.view))
        draw_knobs();
    else
        draw_info();
    leds();
    speak();
    ann_tick(plat_ms());
    ui.frame++;
}

void ui_input_only(void) { input(); }
