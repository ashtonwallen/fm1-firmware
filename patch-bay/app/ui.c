/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen; built on AMB-1 by Charles Vestal (its message, knob, help-card, autosave and
 * screen-reader machinery, and its anti-aliased drawing from X0X) */
/* PATCH BAY's UI: the panel and the screen. Part of the unity build (after gfx.c, project.c and bay.c); the host
 * simulator includes it too.
 *
 *   SELECT         the cursor: a module                ALGO     a jack on it
 *   EDIT, SEL      plug / unplug at the cursor's jack  HOME     the cursor away (a held cable dropped)
 *   KNOB 1         AUTO: how often the patch moves by itself (OFF .. 5 min)
 *   KNOB 2, 3      TEMPO, SPACE (the reverb)           KNOB 4   the cursor's module's knob
 *   PRESETS        the starting patches                REC      one move of the auto-patcher now
 *   ENV LFO SEQ FX ARP   the cursor to those modules (again: the next of them)
 *   keys           the key (the root)                  OCT- OCT+   the scale
 *   PLAY           play / pause                        GLO      the lights
 *   SAVE           saves at once; held 1.5 s: the preset back to its recipe */
#include "fastmath.h"

/* ------------------------------------------------------------ colours --- */
#define K_BG RGB(12, 13, 18)
#define K_BAR RGB(8, 9, 13)                    /* the header and the footer */
#define K_RULE RGB(36, 39, 48)
#define K_PANEL RGB(37, 40, 50)
#define K_PANEL_HI RGB(56, 60, 73)
#define K_PANEL_LO RGB(25, 27, 34)
#define K_SCREEN RGB(18, 20, 26)               /* a panel's little display */
#define K_TITLE RGB(224, 227, 234)
#define K_TEXT RGB(236, 238, 244)
#define K_DIM RGB(118, 124, 138)
#define K_FAINT RGB(62, 66, 80)
#define K_HOLE RGB(5, 5, 8)
#define K_RING RGB(152, 158, 170)
#define K_PLATE RGB(84, 88, 104)
#define K_LOW RGB(230, 90, 70)
#define K_WHITE 0xFFFFu
static const uint16_t ACCENT[4] = {RGB(246, 172, 72), RGB(84, 214, 198), RGB(252, 116, 98), RGB(168, 140, 252)};
static const uint16_t ACCENT_T[4] = {RGB(92, 70, 42), RGB(40, 82, 80), RGB(96, 50, 46), RGB(66, 58, 100)};
/* each output's cable: gates yellows, CVs blues and greens, audio reds */
static const uint16_t OUT_COL[PB_NOUT] = {
    RGB(255, 214, 80),  RGB(255, 172, 64),  RGB(90, 176, 255),  RGB(255, 228, 120), RGB(150, 124, 255),
    RGB(196, 146, 255), RGB(64, 222, 232),  RGB(236, 240, 130), RGB(96, 232, 150),  RGB(178, 238, 96),
    RGB(64, 196, 124),  RGB(140, 214, 84),  RGB(255, 140, 204), RGB(240, 108, 176), RGB(255, 96, 84),
    RGB(255, 134, 60),  RGB(226, 228, 236), RGB(255, 236, 160), RGB(255, 82, 122),  RGB(255, 154, 112),
    RGB(226, 104, 244),
};

/* ------------------------------------------------------------ state --- */
static struct {
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];
    uint32_t down_t[NB];
    uint32_t turn_t;
    uint8_t help;                      /* a button's card is up: the button + 1 */
    uint8_t reset_done;
    uint8_t dirty;                     /* a setting changed since the last save */
    uint32_t dirty_t, act_t, saved_t, due_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;                    /* the knob turned last (0..3), shown bright for a moment */
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[2];                   /* the header's and the footer's: redrawn when they change */
    uint8_t playing;
} ui;

/* each panel's history (its little scope), newest last */
#define NH 50
static uint8_t hist[6][NH];            /* RAND S&H, RAND SMOOTH, LFO 1, LFO 2, ENV 1, ENV 2 */
static uint8_t hist_w;

/* the screen reader (announce.c; speak() below): a message went to it this frame */
static uint8_t sp_said;
static void msg_text(const char *a, const char *b, char *t)
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
    ui.msg_until = plat_ms() + 1500u;
    if (flags && ann_on()) {
        msg_text(a, b, t);
        ann_say(t, 0, flags);
        sp_said = 1;
    }
}
static void say(const char *a, const char *b) { say_as(a, b, ANN_NOW); }
static void warn(const char *a, const char *b) { say_as(a, b, ANN_NOW | ANN_KEEP); }
void ui_say(const char *a, const char *b) { say(a, b); }

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
static void mark_dirty(void)
{
    ui.dirty = 1;
    ui.dirty_t = plat_ms();
}
static void caps(char *d, const char *s, int n)
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}

/* ------------------------------------------------------------ knobs --- */
enum { KN_AUTO, KN_TEMPO, KN_SPACE, KN_MOD };
static const char *const KNOB_NAME[3] = {"Auto", "Tempo", "Space"};
static int knob_get(int k)
{
    return k == KN_AUTO ? proj.autor : k == KN_TEMPO ? proj.g[G_TEMPO] : k == KN_SPACE ? proj.g[G_SPACE] :
           proj.par[proj.sel];
}
static void knob_range(int k, int *lo, int *hi)
{
    *lo = k == KN_TEMPO ? 40 : 0;
    *hi = k == KN_AUTO ? NAUTO - 1 : k == KN_TEMPO ? 160 : 100;
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k == KN_AUTO) {
        uint32_t s = AUTO_S[v];
        if (!s) {
            cat(b, "Off");
        } else if (s < 60u) {
            itoa_u(s, b);
            cat(b + (s >= 10u ? 2 : 1), " s");
        } else {
            itoa_u(s / 60u, b);
            cat(b + 1, s % 60u ? ".5 min" : " min");
        }
    } else if (k == KN_TEMPO) {
        itoa_u((uint32_t)v, b);
    } else if (k == KN_SPACE) {
        itoa_u((uint32_t)v, b);
        cat(b + (v >= 100 ? 3 : v >= 10 ? 2 : 1), "%");
    } else {
        pb_param_text(proj.sel, v, b);
    }
}
static void knob_set(int k, int v)
{
    int lo, hi;
    knob_range(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (v == knob_get(k))
        return;
    if (k == KN_AUTO) {
        proj.autor = (uint8_t)v;
        au.next_t = 0;                       /* a whole wait at the new rate */
    } else if (k == KN_TEMPO) {
        proj.g[G_TEMPO] = (uint8_t)v;
        pb_global(G_TEMPO, v);
    } else if (k == KN_SPACE) {
        proj.g[G_SPACE] = (uint8_t)v;
        pb_global(G_SPACE, v);
    } else {
        hands_on();
        proj.par[proj.sel] = (uint8_t)v;
        pb_param(proj.sel, v);
    }
    mark_dirty();
}
/* knob acceleration by speed (X0X's, as in AMB-1) */
static uint8_t accel_off;
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
    m = dt < 12u ? 6u : dt < 25u ? 4u : dt < 45u ? 2u : 1u;
    return s * (int32_t)m;
}

/* ------------------------------------------------------------ actions --- */
static void sp_preset(void);
static void cursor_show(void) { cur.seen_until = plat_ms() + 20000u; }
static void set_sel(int m)
{
    proj.sel = (uint8_t)((m % PB_NMOD + PB_NMOD) % PB_NMOD);
    cur.jack = 0;
    cursor_show();
}
static void set_preset(int i)
{
    bay_reset();
    project_preset(i);
    project_apply();
    pb_seed(plat_ms());
    say_as("PRESET", PB_PRESET[proj.preset].name, 0);
    sp_preset();
    mark_dirty();
}
static void set_key(int pc)
{
    proj.g[G_ROOT] = (uint8_t)pc;
    pb_global(G_ROOT, pc);
    say("KEY", PB_NOTE_NAME[pc]);
    mark_dirty();
}
static void set_scale(int s)
{
    s = (s % PB_NSCALES + PB_NSCALES) % PB_NSCALES;
    proj.g[G_SCALE] = (uint8_t)s;
    pb_global(G_SCALE, s);
    say("SCALE", PB_SCALE_NAME[s]);
    mark_dirty();
}
static void jump(const uint8_t *mods, int n)  /* a shortcut button: the first of its modules, then the next */
{
    int k;
    for (k = 0; k < n; k++)
        if (proj.sel == mods[k] && plat_ms() < cur.seen_until) {
            set_sel(mods[(k + 1) % n]);
            return;
        }
    set_sel(mods[0]);
}

static void button(int b)
{
    static const uint8_t J_ENV[2] = {M_ENV1, M_ENV2}, J_LFO[2] = {M_LFO1, M_LFO2}, J_SEQ[3] = {M_SEQ, M_CLK, M_QUANT},
                         J_FX[3] = {M_DLY, M_VERB, M_MIX}, J_ARP[4] = {M_VCO, M_FM, M_NOISE, M_VCF};
    switch (b) {
    case B_EDIT: case B_SEL: bay_plug(); break;
    case B_HOME:
        bay_drop();
        cur.seen_until = 0;
        break;
    case B_PLAY:
        ui.playing = !pb_playing;
        if (pb_playing) {
            pb_play(0);
            say("PAUSED", 0);
        } else {
            pb_play(1);
            say("PLAYING", 0);
        }
        break;
    case B_REC:
        if (au.busy) {
            au_finish();
        }
        au.t0 = 0;
        au_start(plat_ms());
        au.t0 = plat_ms();
        au.hand_until = plat_ms();
        say("MOVE", au.what == AU_CABLE ? "A CABLE" : au.what == AU_TWIST ? PB_MOD_NAME[au.tmod] : "THE SEQUENCE");
        break;
    case B_ENV: jump(J_ENV, 2); break;
    case B_LFO: jump(J_LFO, 2); break;
    case B_SEQ: jump(J_SEQ, 3); break;
    case B_FX: jump(J_FX, 3); break;
    case B_ARP: jump(J_ARP, 4); break;
    case B_GLO:
        proj.leds = (uint8_t)((proj.leds + 1u) % 3u);
        say("LIGHTS", proj.leds == 1 ? "ON" : proj.leds == 2 ? "KEYS" : "OFF");
        mark_dirty();
        break;
    case B_OCTDN: case B_OCTUP: set_scale(proj.g[G_SCALE] + (b == B_OCTUP ? 1 : -1)); break;
    case B_SAVE:
        if (project_save() == 0) {
            ui.dirty = 0;
            au.changed = 0;
            ui.saved_t = plat_ms();
            say("SAVED", 0);
        } else {
            warn("NOT SAVED", "FLASH ERROR");
        }
        break;
    default: break;
    }
}

/* MIDI in (USB and the TRS jack): a note sets the key */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0)) {
        uint32_t st = (pkt >> 8) & 0xF0u, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == 0x90u && v)
            set_key((int)(n % 12u));
    }
}

/* HELP CARDS (X0X's): hold one button alone and a card says what it does */
#define HELP_HOLD 900u
#define RESET_HOLD 1500u
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
        if (btn == 1u << i && i != B_PLAY && !(ui.btn_used & btn) && now - ui.down_t[i] >= HELP_HOLD &&
            (int32_t)(ui.turn_t - ui.down_t[i]) <= 0) {
            ui.help = (uint8_t)(i + 1);
            ui.btn_used |= btn;
        }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i, now = plat_ms();
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            ui.down_t[i] = now;
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
        if ((ch & keys) >> i & 1u)
            set_key((int)((5u + i) % 12u));
    if (btn || keys || ch)
        ui.act_t = now;
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {
        set_sel(proj.sel + (e > 0 ? 1 : -1));
        ui.turn_t = ui.act_t = now;
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        if (now >= cur.seen_until)
            cursor_show();
        else
            cur.jack = (uint8_t)((cur.jack + (e > 0 ? 1 : mod_njacks(proj.sel) - 1)) % mod_njacks(proj.sel));
        cursor_show();
        ui.turn_t = ui.act_t = now;
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        set_preset(proj.preset + (e > 0 ? 1 : -1));
        ui.turn_t = ui.act_t = now;
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && now - ui.down_t[B_SAVE] > RESET_HOLD) {
        ui.reset_done = 1;
        ui.btn_used |= 1u << B_SAVE;
        ui.help = 0;
        set_preset(proj.preset);
        say("RESET", PB_PRESET[proj.preset].name);
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            int lo, hi;
            knob_range((int)i, &lo, &hi);
            if (i == KN_MOD)
                cursor_show();
            knob_set((int)i, knob_get((int)i) + accel(EN_K1 + (int)i, e, hi - lo));
            ui.touched = (int8_t)i;
            ui.touch_until = now + 1500u;
            ui.act_t = ui.turn_t = now;
        }
    help_tick(btn, keys);
}

/* ------------------------------------------------------------- drawing --- */
#define HDR_H 22
#define MAIN_Y 22
#define MAIN_H 176
#define FTR_Y 198
#define FTR_H 42

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

/* the battery (plat_battery): the simulator's "battery N" stands in for it */
#ifdef PLAT_HOST
static int8_t batt_host = -2;
static int batt(void) { return batt_host > -2 ? batt_host : plat_battery(); }
#else
#define batt() plat_battery()
#endif
#define BATT_W 20
static void draw_batt(int32_t x, int32_t y, int b)
{
    static const int8_t BOLT[7][2] = {{10, 2}, {9, 2}, {8, 2}, {7, 5}, {9, 2}, {8, 2}, {7, 2}};
    uint16_t c = b <= 1 ? K_LOW : K_DIM;
    int i;
    cv_round(x, y, 18, 10, 2, c);
    cv_rect(x + 1, y + 1, 16, 8, K_BAR);
    cv_rect(x + 18, y + 3, 2, 4, c);
    if (b == 4) {
        for (i = 0; i < 7; i++)
            cv_rect(x + BOLT[i][0], y + 1 + i, BOLT[i][1], 1, K_TEXT);
        return;
    }
    for (i = 0; i < b && i < 3; i++)
        cv_rect(x + 2 + i * 5, y + 2, 4, 6, b <= 1 ? K_LOW : ACCENT[1]);
}
static const char *batt_words(int b)
{
    static const char *const W[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                     "On USB power"};
    return b >= 0 && b <= 4 ? W[b] : 0;
}

/* an arrow, 7 wide, its middle on y: right (dir 1) or left (-1); the x after it */
static int32_t arrow(int32_t x, int32_t y, int dir, uint16_t c)
{
    int i;
    cv_rect(x, y, 7, 1, c);
    for (i = 1; i <= 3; i++) {
        int32_t ax = dir > 0 ? x + 6 - i : x + i;
        cv_rect(ax, y - i, 1, 1, c);
        cv_rect(ax, y + i, 1, 1, c);
    }
    return x + 11;
}

/* the auto-patcher's ring in the header: a track and its progress, a dot at its head */
static void auto_ring(int32_t cx, int32_t cy, float p, uint16_t c)
{
    int i;
    for (i = 0; i < 24; i++) {
        float a = (float)i * (FM_TWO_PI / 24.0f);
        int32_t x = cx + (int32_t)(fm_sinf(a) * 6.0f + (fm_sinf(a) < 0.0f ? -0.5f : 0.5f));
        int32_t y = cy - (int32_t)(fm_cosf(a) * 6.0f + (fm_cosf(a) < 0.0f ? -0.5f : 0.5f));
        cv_rect(x, y, 1, 1, p >= 0.0f && (float)i < p * 24.0f ? c : K_FAINT);
    }
    if (p >= 1.0f)
        cv_disc(cx, cy, 2, c);
}

/* the header: the preset in a tag, the key; on the right the auto-patcher, the tempo and its beat, the battery;
 * messages take its place for a moment */
static void draw_header(void)
{
    uint32_t now = plat_ms(), h;
    int msg = now < ui.msg_until, beat = pb_playing && pb_val[O_CLK_BEAT] > 0.5f;
    float p = au_progress(now);
    h = hash(hash(hash(2166136261u, proj.preset | (uint32_t)proj.g[G_ROOT] << 8 | (uint32_t)proj.g[G_SCALE] << 12 |
                                       (uint32_t)msg << 16 | (uint32_t)beat << 17 | (uint32_t)ui.dirty << 18 |
                                       (uint32_t)pb_playing << 19 | (uint32_t)(batt() + 1) << 20 |
                                       (uint32_t)(now < au.hand_until) << 24 | (uint32_t)au.busy << 25),
                  (uint32_t)proj.g[G_TEMPO] | (uint32_t)proj.autor << 8 | (uint32_t)(p * 24.0f + 2.0f) << 16),
             msg ? hash((uint32_t)ui.msg[0][0] << 8 | (uint8_t)ui.msg[1][0], ui.msg_until) : 0u);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BAR);
    if (msg) {
        int32_t x = cv_text(8, 3, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 3, &FONT_B, ui.msg[1], ACCENT[0]);
    } else {
        char t[16];
        int32_t x = 236, w;
        const char *st;
        if (batt() >= 0) {
            x -= BATT_W;
            draw_batt(x, 6, batt());
            x -= 7;
        }
        itoa_u(proj.g[G_TEMPO], t);
        x -= text_w(&FONT_S, t);
        cv_text(x, 3, &FONT_S, t, pb_playing ? K_TEXT : K_DIM);
        x -= 9;
        if (pb_playing)
            cv_disc(x, 11, 3, beat ? ACCENT[0] : ACCENT_T[0]);
        else {
            int32_t j;
            for (j = 0; j < 4; j++)
                cv_rect(x - 2 + j, 7 + j, 1, 9 - 2 * j, K_DIM);
        }
        x -= 14;
        st = !proj.autor ? "OFF" : now < au.hand_until ? "HAND" : 0;   /* (the ring alone: patching itself) */
        auto_ring(x, 11, now < au.hand_until ? -1.0f : p, au.busy ? K_TEXT : ACCENT[1]);
        x -= 9;
        if (st) {
            x -= text_w(&FONT_T, st);
            cv_text(x, 5, &FONT_T, st, K_DIM);
        }
        if (ui.dirty)
            cv_disc(x - 7, 11, 2, K_DIM);
        w = text_w(&FONT_B, PB_PRESET[proj.preset].name) + 12;
        cv_round(3, 2, w, 18, 5, ACCENT[0]);
        cv_text(9, 3, &FONT_B, PB_PRESET[proj.preset].name, K_BAR);
        w += 9;
        if (w + text_w(&FONT_XS, PB_NOTE_NAME[proj.g[G_ROOT]]) <= x - 10) {
            w = cv_text(w, 4, &FONT_XS, PB_NOTE_NAME[proj.g[G_ROOT]], K_TEXT);
            if (w + 3 + text_w(&FONT_XS, PB_SCALE_NAME[proj.g[G_SCALE]]) <= x - 10)
                cv_text(w + 3, 4, &FONT_XS, PB_SCALE_NAME[proj.g[G_SCALE]], K_DIM);
        }
    }
    cv_blit(0, 0);
}

/* ------------------------------------------------------------ the bay --- */
#define PX(m) ((int32_t)((m) % 4) * 60 + 1)
#define PY(m) ((int32_t)((m) / 4) * 44 + 1)
static void jack_xy(int j, int32_t *x, int32_t *y)
{
    int js[4], n = pb_mod_jacks(PB_JACK[j].mod, js), k;
    for (k = 0; k < n && js[k] != j; k++)
        ;
    *x = PX(PB_JACK[j].mod) + (n == 1 ? 29 : n == 2 ? 15 + 28 * k : 10 + 19 * k);
    *y = PY(PB_JACK[j].mod) + 34;
}

static float out_level(int o)                    /* an output's signal now, 0..1, for its cable's glow */
{
    float v = pb_val[o];
    if (PB_JACK[PB_NIN + o].type == T_AUDIO)
        v *= 2.5f;
    return fm_clampf(v, 0.0f, 1.0f);
}

/* a panel's little display, x 4..53, y 15..27 of the panel */
static void scope_line(int32_t x, int32_t y, const uint8_t *hv, uint16_t c, int filled)
{
    int i;
    int32_t py = -1;
    for (i = 0; i < NH; i++) {
        uint8_t v = hv[(hist_w + i) % NH];
        int32_t yy = y + 12 - (int32_t)v * 12 / 255;
        if (filled)
            cv_rect(x + i, yy, 1, y + 13 - yy, mix(K_SCREEN, c, 5));
        if (py >= 0 && py != yy)
            cv_rect(x + i, py < yy ? py : yy, 1, (py < yy ? yy - py : py - yy) + 1, c);
        else
            cv_rect(x + i, yy, 1, 1, c);
        py = yy;
    }
}

static void wave_scope(int32_t x, int32_t y, int note, float amp, int fm, uint16_t c)
{
    int i;
    float cyc = fm_clampf(1.0f + (float)(note - 36) * 0.07f, 1.0f, 4.0f), w = (float)proj.par[M_VCO] * 0.01f;
    float r = 1.0f, ix = pb_fm_index;
    int32_t py = -1;
    static const float RT[10] = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f, 7.0f};
    if (fm)
        r = RT[proj.par[M_FM] * 9 / 100];
    amp = fm_clampf(amp * 2.2f, 0.08f, 1.0f);
    for (i = 0; i < 50; i++) {
        float ph = (float)i * cyc / 50.0f, s, p = ph - fm_floorf(ph);
        int32_t yy;
        if (fm) {
            s = fm_sinf(FM_TWO_PI * ph + ix * fm_sinf(FM_TWO_PI * ph * r));
        } else {
            float sn = fm_sinf(FM_TWO_PI * p), tr = 1.0f - 4.0f * fm_fabsf(p - 0.5f), sw = 2.0f * p - 1.0f;
            s = w < 0.5f ? sn * (1.0f - 2.0f * w) + tr * 2.0f * w : tr * (2.0f - 2.0f * w) + sw * (2.0f * w - 1.0f);
            s = -s;
        }
        yy = y + 6 - (int32_t)(s * amp * 5.5f);
        if (py >= 0 && py != yy)
            cv_rect(x + i, py < yy ? py : yy, 1, (py < yy ? yy - py : py - yy) + 1, c);
        else
            cv_rect(x + i, yy, 1, 1, c);
        py = yy;
    }
}

static void note_text(int n, char *b)            /* "Eb4" */
{
    const char *s = PB_NOTE_NAME[n % 12];
    while (*s)
        *b++ = *s++;
    itoa_u((uint32_t)(n / 12 - 1), b);
}

static void display(int m, int32_t x, int32_t y, uint16_t ac)
{
    int i;
    uint32_t now = plat_ms();
    cv_round(x, y, 50, 13, 2, K_SCREEN);
    switch (m) {
    case M_CLK: {
        int div = 2 + proj.par[M_CLK] * 6 / 100;
        uint32_t e = pb_eighths, base = e - e % 8u;
        for (i = 0; i < 8; i++) {
            int32_t cx = x + 4 + i * 6;
            int on = pb_playing && (uint32_t)i == e % 8u && pb_val[O_CLK_BEAT] > 0.5f;
            if ((base + (uint32_t)i) % (uint32_t)div == 0u)
                cv_disc(cx, y + 6, 2, on ? K_TEXT : (uint32_t)i == e % 8u ? ac : mix(K_SCREEN, ac, 9));
            else
                cv_disc(cx, y + 6, on ? 2 : 1, on ? K_TEXT : (uint32_t)i == e % 8u ? ac : K_FAINT);
        }
        break;
    }
    case M_SEQ: {
        int len = 2 + proj.par[M_SEQ] * 6 / 100;
        for (i = 0; i < 8; i++) {
            int v = proj.seq[i], hh;
            int32_t bx = x + 2 + i * 6;
            uint16_t c = i >= len ? K_FAINT : i == pb_seq_pos ? (pb_val[O_SEQ_GATE] > 0.5f ? K_TEXT : ac) :
                         mix(K_SCREEN, ac, 8);
            if (au.busy && au.what == AU_SEQ && au.sstep == i)
                c = K_TEXT;
            if (v < 0) {
                cv_rect(bx, y + 10, 4, 1, i >= len ? K_FAINT : K_DIM);
                continue;
            }
            hh = 2 + v * 9 / 14;
            cv_rect(bx, y + 11 - hh, 4, hh, c);
        }
        break;
    }
    case M_RAND:
        scope_line(x, y, hist[1], mix(K_SCREEN, ac, 7), 0);
        scope_line(x, y, hist[0], ac, 0);
        break;
    case M_QUANT: {
        char t[8];
        int hot = pb_val[O_QUANT_CHG] > 0.5f;
        note_text(pb_note[0], t);
        text_c(x + 25, y - 1, &FONT_XS, t, hot ? K_TEXT : ac);
        break;
    }
    case M_LFO1: case M_LFO2:
        scope_line(x, y, hist[m == M_LFO1 ? 2 : 3], ac, 0);
        break;
    case M_ENV1: case M_ENV2:
        scope_line(x, y, hist[m == M_ENV1 ? 4 : 5], ac, 1);
        break;
    case M_VCO:
        wave_scope(x, y, pb_note[1], pb_val[O_VCO], 0, ac);
        break;
    case M_FM:
        wave_scope(x, y, pb_note[2], pb_val[O_FM], 1, ac);
        break;
    case M_NOISE: {
        uint32_t h = now * 2654435761u;
        float a = fm_clampf(pb_val[O_NOISE] * 3.0f, 0.15f, 1.0f);
        for (i = 0; i < 50; i += 2) {
            int32_t yy;
            h = h * 1664525u + 1013904223u;
            yy = y + 6 + (int32_t)((float)((int32_t)(h >> 28) - 8) * a * 0.7f);
            cv_rect(x + i, yy, 2, 1, mix(K_SCREEN, ac, 10));
        }
        if (pb_val[O_DUST] > 0.5f)
            cv_disc(x + 8 + (int32_t)(now % 34u), y + 6, 2, K_TEXT);
        break;
    }
    case M_VCF: {
        float fc = pb_vcf_hz, q = 1.0f + (float)proj.g[G_RES] * 0.012f;
        int32_t py = -1;
        for (i = 0; i < 50; i++) {
            float f = 30.0f * fm_exp2f((float)i * 0.173f), rr = f / fc, mg;
            int32_t yy;
            mg = 1.0f / fm_sqrtf((1.0f - rr * rr) * (1.0f - rr * rr) + rr * rr / (q * q));
            mg = fm_clampf(mg, 0.0f, 2.0f);
            yy = y + 12 - (int32_t)(mg * 7.0f);
            if (yy < y + 1)
                yy = y + 1;
            if (py >= 0 && py != yy)
                cv_rect(x + i, py < yy ? py : yy, 1, (py < yy ? yy - py : py - yy) + 1, ac);
            else
                cv_rect(x + i, yy, 1, 1, ac);
            py = yy;
        }
        break;
    }
    case M_VCA: {
        int32_t g = (int32_t)(fm_clampf(pb_vca_gain, 0.0f, 1.0f) * 46.0f), l = (int32_t)(out_level(O_VCA) * 46.0f);
        cv_rect(x + 2, y + 3, 46, 3, K_FAINT);
        cv_rect(x + 2, y + 3, g, 3, mix(K_SCREEN, ac, 9));
        cv_rect(x + 2, y + 8, 46, 2, K_FAINT);
        cv_rect(x + 2, y + 8, l, 2, ac);
        break;
    }
    case M_DLY:
        for (i = 0; i < 5; i++) {
            float fb = (float)proj.par[M_DLY] * 0.0075f, lv = out_level(O_DLY), a = lv;
            int k;
            int32_t hh;
            for (k = 0; k < i; k++)
                a *= fb + 0.15f;
            hh = 1 + (int32_t)(fm_clampf(a, 0.0f, 1.0f) * 10.0f);
            cv_rect(x + 3 + i * 10, y + 11 - hh, 3, hh, i ? mix(K_SCREEN, ac, 14 - 2 * i) : ac);
        }
        break;
    case M_MIX:
        for (i = 0; i < 3; i++) {
            int o = proj.cab[I_MIX_A + i];
            int32_t hh = o >= 0 ? (int32_t)(out_level(o) * 11.0f) : 0, bx = x + 9 + i * 14;
            cv_rect(bx, y + 1, 5, 11, K_FAINT);
            if (hh)
                cv_rect(bx, y + 12 - hh, 5, hh, ac);
        }
        break;
    case M_VERB: {
        float lv = fm_clampf(pb_level * 2.5f, 0.0f, 1.0f), sz = (float)proj.par[M_VERB] * 0.01f;
        for (i = 0; i < 7; i++) {
            int32_t r = 1 + i * 3;
            int t = q16(lv * (1.0f - (float)i / (2.0f + 6.0f * sz)));
            if (t > 0) {
                cv_rect(x + 25 - r, y + 6, 1, 1, mix(K_SCREEN, ac, t));
                cv_rect(x + 25 + r, y + 6, 1, 1, mix(K_SCREEN, ac, t));
                cv_rect(x + 25 - r / 2, y + 6 - (i < 5 ? i : 5), 1, 1, mix(K_SCREEN, ac, t / 2));
                cv_rect(x + 25 + r / 2, y + 6 + (i < 5 ? i : 5), 1, 1, mix(K_SCREEN, ac, t / 2));
            }
        }
        cv_disc(x + 25, y + 6, 1 + (int32_t)(lv * 3.0f), ac);
        break;
    }
    default: break;
    }
}

/* the knob in a panel's corner: a dial with its pointer, 270 degrees */
static void dial(int32_t cx, int32_t cy, int v, uint16_t c)
{
    float a = (-0.75f + 1.5f * (float)v * 0.01f) * FM_PI;
    cv_disc(cx, cy, 4, K_PANEL_LO);
    cv_line(cx, cy, cx + (int32_t)(fm_sinf(a) * 4.4f + (fm_sinf(a) < 0.0f ? -0.5f : 0.5f)),
            cy - (int32_t)(fm_cosf(a) * 4.4f + (fm_cosf(a) < 0.0f ? -0.5f : 0.5f)), c);
}

static float led_of(int m)                        /* a panel's light: its gate, or its level */
{
    switch (m) {
    case M_CLK: return pb_val[O_CLK_BEAT];
    case M_SEQ: return pb_val[O_SEQ_GATE];
    case M_RAND: return pb_val[O_RAND_SH] * 0.6f;
    case M_QUANT: return pb_val[O_QUANT_CHG];
    case M_LFO1: return pb_val[O_LFO1_SIN];
    case M_LFO2: return pb_val[O_LFO2_SIN];
    case M_ENV1: return pb_val[O_ENV1];
    case M_ENV2: return pb_val[O_ENV2];
    case M_VCO: return out_level(O_VCO);
    case M_FM: return out_level(O_FM);
    case M_NOISE: return pb_val[O_DUST] > 0.5f ? 1.0f : out_level(O_NOISE) * 0.5f;
    case M_VCF: return out_level(O_VCF);
    case M_VCA: return out_level(O_VCA);
    case M_DLY: return out_level(O_DLY);
    default: return fm_clampf(pb_level * 2.5f, 0.0f, 1.0f);
    }
}

static void draw_panel(int m)
{
    int32_t x = PX(m), y = PY(m), jx, jy;
    int js[4], n = pb_mod_jacks(m, js), k, row = m / 4;
    uint16_t ac = ACCENT[row];
    int glow = (au.busy && ((au.what == AU_TWIST && au.tmod == m) ||
                            (au.what == AU_CABLE && ((au.mv.in >= 0 && PB_JACK[au.mv.in].mod == m) ||
                                                     PB_JACK[PB_NIN + au.mv.out].mod == m))));
    cv_round(x, y, 58, 42, 3, K_PANEL_LO);
    cv_round(x, y, 58, 41, 3, K_PANEL);
    cv_rect(x + 3, y, 52, 1, K_PANEL_HI);
    cv_rect(x + 3, y + 1, 52, 1, mix(K_PANEL, ac, glow ? 14 : 6));
    cv_disc(x + 6, y + 7, 2, mix(ACCENT_T[row], ac, q16(led_of(m))));
    cv_text(x + 11, y + 2, &FONT_T, PB_MOD_NAME[m], glow ? ac : K_TITLE);
    if (m != M_MIX || 1)
        dial(x + 51, y + 7, proj.par[m], glow && au.what == AU_TWIST ? K_TEXT : mix(K_DIM, ac, 6));
    display(m, x + 4, y + 14, ac);
    for (k = 0; k < n; k++) {
        jack_xy(js[k], &jx, &jy);
        if (jack_is_out(js[k]))
            cv_round(jx - 7, jy - 6, 15, 13, 3, K_PLATE);
        cv_disc(jx, jy, 4, K_RING);
        cv_disc(jx, jy, 2, K_HOLE);
    }
}

#define CABLE_A 11u                              /* a cable's opacity (of 16): the panels show through */
/* a cable: a sagging curve (a quadratic Bezier, its middle pulled down) from (x0, y0) to (x1, y1), 3 px wide over a
 * soft shadow, a highlight along its top */
static void cable(float x0, float y0, float x1, float y1, float sag, uint16_t c)
{
    float mx = 0.5f * (x0 + x1), my = 0.5f * (y0 + y1) + sag, d;
    int i, n, pass;
    uint16_t hi = mix(c, K_WHITE, 7), lo = mix(c, K_BG, 6);
    if (my > 172.0f)
        my = 172.0f;
    d = fm_fabsf(x1 - x0) + fm_fabsf(y1 - y0) + 2.0f * fm_fabsf(sag);
    n = (int)(d * 0.9f) + 4;
    if (n > 400)
        n = 400;
    for (pass = 0; pass < 3; pass++)
        for (i = 0; i <= n; i += pass == 1 ? 1 : 2) {
            float t = (float)i / (float)n, u = 1.0f - t;
            float x = u * u * x0 + 2.0f * u * t * mx + t * t * x1, y = u * u * y0 + 2.0f * u * t * my + t * t * y1;
            int32_t ix = (int32_t)(x + 0.5f), iy = (int32_t)(y + 0.5f);
            if (pass == 0) {
                px_blend(ix + 1, iy + 2, K_HOLE, 4);
            } else if (pass == 1) {
                px_blend(ix - 1, iy, lo, CABLE_A);
                px_blend(ix + 1, iy, lo, CABLE_A);
                px_blend(ix, iy + 1, lo, CABLE_A);
                px_blend(ix, iy, c, CABLE_A + 2);
            } else {
                px_blend(ix, iy - 1, hi, CABLE_A - 2);
            }
        }
}
static void plug(int32_t x, int32_t y, uint16_t c)
{
    cv_disc(x, y, 4, mix(c, K_HOLE, 7));
    cv_disc(x, y, 3, c);
    cv_disc(x, y, 1, mix(c, K_WHITE, 9));
}
static float sag_of(float x0, float y0, float x1, float y1)
{
    float d = fm_fabsf(x1 - x0) + fm_fabsf(y1 - y0) * 0.5f;
    return 5.0f + d * 0.2f;
}
static uint16_t cable_col(int out)
{
    return mix(OUT_COL[out], K_WHITE, (int)(out_level(out) * 6.0f));
}

static void draw_cables(void)
{
    uint32_t now = plat_ms();
    int i, skip = -1;
    int32_t ax, ay, bx, by;
    if (au.busy && au.what == AU_CABLE && !au.landed)
        skip = au.mv.kind == MV_SRC ? au.mv.in : au.mv.in_old;   /* (the cable in the air is drawn below) */
    for (i = 0; i < PB_NIN; i++) {
        if (proj.cab[i] < 0 || i == skip)
            continue;
        jack_xy(PB_NIN + proj.cab[i], &ax, &ay);
        jack_xy(i, &bx, &by);
        cable((float)ax, (float)ay, (float)bx, (float)by, sag_of((float)ax, (float)ay, (float)bx, (float)by),
              cable_col(proj.cab[i]));
    }
    if (au.busy && au.what == AU_CABLE) {          /* the auto-patcher's: one end flying to its new jack */
        uint32_t el = now - au.t0;
        float u = fm_clampf(((float)el - 150.0f) / (float)(LAND_AT - 150u), 0.0f, 1.0f), e = u * u * (3.0f - 2.0f * u);
        float fx, fy, px, py, lift;
        int32_t sx, sy, tx, ty;
        int fixed_out = au.mv.kind != MV_SRC, out = au.mv.out;
        uint16_t c = OUT_COL[out];
        if (au.mv.kind == MV_SRC) {                /* the output end moves */
            jack_xy(au.mv.in, &bx, &by);
            jack_xy(PB_NIN + au.mv.out_old, &sx, &sy);
            jack_xy(PB_NIN + au.mv.out, &tx, &ty);
            if (au.landed)
                c = cable_col(out);
        } else if (au.mv.kind == MV_DEL) {         /* back to its output */
            jack_xy(PB_NIN + out, &bx, &by);
            jack_xy(au.mv.in_old, &sx, &sy);
            tx = bx;
            ty = by;
        } else {
            jack_xy(PB_NIN + out, &bx, &by);
            if (au.mv.kind == MV_ADD) {
                sx = bx;
                sy = by;
            } else {
                jack_xy(au.mv.in_old, &sx, &sy);
            }
            jack_xy(au.mv.in, &tx, &ty);
        }
        lift = el < 150u ? (float)el * 0.04f : 6.0f + 18.0f * fm_sinf(FM_PI * u);
        if (au.landed && au.mv.kind != MV_DEL)
            lift = 0.0f;
        px = (float)sx + ((float)tx - (float)sx) * e;
        py = (float)sy + ((float)ty - (float)sy) * e - lift;
        fx = (float)bx;
        fy = (float)by;
        if (!(au.mv.kind == MV_DEL && el >= LAND_AT)) {
            cable(fx, fy, px, py, sag_of(fx, fy, px, py) * (1.0f + 0.5f * fm_sinf(FM_PI * u)), c);
            plug((int32_t)px, (int32_t)py, c);
        }
        if (au.landed && au.mv.kind != MV_DEL && el < MOVE_MS) {   /* the landing: a ring that spreads and fades */
            int r = 4 + (int)((el - LAND_AT) / 60u), a = 16 - (int)((el - LAND_AT) / 30u), k;
            for (k = 0; k < 32 && a > 0; k++) {
                float an = (float)k * (FM_TWO_PI / 32.0f);
                px_blend(tx + (int32_t)(fm_sinf(an) * (float)r), ty + (int32_t)(fm_cosf(an) * (float)r), K_WHITE,
                         (uint32_t)a);
            }
        }
        (void)fixed_out;
    }
    if (cur.hold >= 0) {                           /* the player's: from its jack to the cursor */
        int j = cursor_jack(), o = jack_is_out(cur.hold) ? cur.hold - PB_NIN : -1;
        uint16_t c = o >= 0 ? OUT_COL[o] : RGB(220, 222, 230);
        float sway = fm_sinf((float)now * 0.004f) * 3.0f;
        jack_xy(cur.hold, &ax, &ay);
        jack_xy(j, &bx, &by);
        if (j == cur.hold) {
            bx += (int32_t)sway;
            by += 14;
        } else {
            by -= 5;
        }
        cable((float)ax, (float)ay, (float)bx + sway, (float)by, sag_of((float)ax, (float)ay, (float)bx, (float)by) + 6.0f,
              c);
        plug(bx + (int32_t)sway, by, c);
    }
    for (i = 0; i < PB_NIN; i++) {                 /* the plugs, over the cables */
        if (proj.cab[i] < 0 || i == skip)
            continue;
        jack_xy(PB_NIN + proj.cab[i], &ax, &ay);
        jack_xy(i, &bx, &by);
        plug(ax, ay, cable_col(proj.cab[i]));
        plug(bx, by, cable_col(proj.cab[i]));
    }
    if (au.busy && au.what == AU_CABLE && au.mv.kind == MV_SRC && !au.landed) {
        jack_xy(au.mv.in, &bx, &by);
        plug(bx, by, OUT_COL[au.mv.out]);
    }
    if (au.busy && au.what == AU_CABLE && au.mv.kind != MV_SRC) {
        jack_xy(PB_NIN + au.mv.out, &bx, &by);
        plug(bx, by, OUT_COL[au.mv.out]);
    }
    if (cur.hold >= 0) {
        jack_xy(cur.hold, &ax, &ay);
        plug(ax, ay, jack_is_out(cur.hold) ? OUT_COL[cur.hold - PB_NIN] : RGB(220, 222, 230));
    }
}

static void draw_cursor(void)
{
    uint32_t now = plat_ms();
    int32_t x = PX(proj.sel), y = PY(proj.sel), jx, jy, k;
    int pulse = (int)((now / 60u) % 16u);
    if (now >= cur.seen_until && cur.hold < 0)
        return;
    pulse = pulse < 8 ? pulse : 16 - pulse;
    for (k = 0; k < 2; k++) {
        uint16_t c = k ? mix(K_TEXT, ACCENT[proj.sel / 4], 8) : K_TEXT;
        cv_rect(x - k, y - k, 58 + 2 * k, 1, c);
        cv_rect(x - k, y + 41 + k, 58 + 2 * k, 1, c);
        cv_rect(x - k, y - k, 1, 42 + 2 * k, c);
        cv_rect(x + 57 + k, y - k, 1, 42 + 2 * k, c);
    }
    jack_xy(cursor_jack(), &jx, &jy);
    for (k = 0; k < 36; k++) {
        float an = (float)k * (FM_TWO_PI / 36.0f);
        px_blend(jx + (int32_t)(fm_sinf(an) * 7.0f), jy + (int32_t)(fm_cosf(an) * 7.0f), K_WHITE, (uint32_t)(10 + pulse));
    }
}

/* a button's card: its name, then what it does, over the bay */
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_EDIT: case B_SEL: L("PLUG IN OR PULL OUT"); L("AT THE CURSOR'S JACK"); L("SELECT: MODULE"); L("ALGO: JACK"); break;
    case B_HOME: L("THE CURSOR AWAY,"); L(" A HELD CABLE DROPPED"); break;
    case B_REC: L("ONE MOVE OF THE"); L(" AUTO-PATCHER, NOW"); L("KNOB 1: HOW OFTEN"); break;
    case B_ENV: L("THE CURSOR TO ENV 1"); L("AGAIN: ENV 2"); break;
    case B_LFO: L("THE CURSOR TO LFO 1"); L("AGAIN: LFO 2"); break;
    case B_SEQ: L("THE CURSOR TO SEQ"); L("AGAIN: CLOCK, QUANT"); break;
    case B_FX: L("THE CURSOR TO DELAY"); L("AGAIN: REVERB, MIX"); break;
    case B_ARP: L("THE CURSOR TO VCO"); L("AGAIN: FM, NOISE, VCF"); break;
    case B_GLO: L("THE LIGHTS:"); L(" ON, KEYS ONLY, OFF"); break;
    case B_OCTDN: L("THE SCALE BEFORE"); L("KEYS: THE KEY"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE NEXT SCALE"); L("KEYS: THE KEY"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES THE PATCH"); L("KEEP HOLDING: THE PRESET"); L(" BACK TO ITS RECIPE"); break;
    default: break;
    }
#undef L
    return n;
}
static void draw_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    cv_round(14, 22, 212, 128, 6, K_RULE);
    cv_round(16, 24, 208, 124, 6, K_PANEL);
    cv_round(16, 24, 208, 26, 6, ACCENT[0]);
    cv_rect(16, 44, 208, 6, ACCENT[0]);
    text_c(120, 28, &FONT_B, BTN_LABEL[ui.help - 1], K_BAR);
    for (k = 0; k < n; k++)
        text_c(120, 62 + k * 20, &FONT_S, l[k] + (l[k][0] == ' '), K_TEXT);
}

static void history(void)
{
    static const uint8_t SRC[6] = {O_RAND_SH, O_RAND_SM, O_LFO1_SIN, O_LFO2_SIN, O_ENV1, O_ENV2};
    int i;
    if (ui.frame % 2u)
        return;
    for (i = 0; i < 6; i++) {
        if (i == 3 && ui.frame % 8u)
            continue;                              /* LFO 2: slower */
        hist[i][(hist_w + NH - 1) % NH] = (uint8_t)(fm_clampf(pb_val[SRC[i]], 0.0f, 1.0f) * 255.0f);
    }
    hist_w = (uint8_t)((hist_w + 1u) % NH);
    if (ui.frame % 8u)                             /* (LFO 2's ring moves with the others: it repeats its last) */
        hist[3][(hist_w + NH - 1) % NH] = hist[3][(hist_w + NH - 2) % NH];
}

static void draw_main(void)
{
    int m;
    if (ui.frame % 2u)
        return;
    cv_begin(240, MAIN_H, K_BG);
    for (m = 0; m < PB_NMOD; m++)
        draw_panel(m);
    draw_cables();
    draw_cursor();
    if (ui.help)
        draw_help();
    cv_blit(0, MAIN_Y);
}

/* the footer: a line about the jack under the cursor, the cable held or the last move; under it the four knobs */
static int32_t lbl(int32_t x, int32_t y, int j, uint16_t c)    /* a jack's "MOD JACK" */
{
    x = cv_text(x, y, &FONT_XS, PB_MOD_NAME[PB_JACK[j].mod], c);
    return cv_text(x + 4, y, &FONT_XS, PB_JACK[j].name, mix(c, K_BAR, 5));
}
static void draw_footer(void)
{
    uint32_t now = plat_ms(), h = 2166136261u;
    int k, cursor = now < cur.seen_until || cur.hold >= 0, hot = now < ui.touch_until, j = cursor_jack();
    int ev = au.ev_kind && now - au.ev_t < 20000u;
    for (k = 0; k < 4; k++)
        h = hash(h, (uint32_t)knob_get(k));
    h = hash(h, proj.sel | (uint32_t)cur.jack << 8 | (uint32_t)(cur.hold + 1) << 12 | (uint32_t)cursor << 20 |
                    (uint32_t)(hot ? ui.touched + 1 : 0) << 21 | (uint32_t)ev << 24);
    h = hash(h, (uint32_t)proj.cab[j < PB_NIN ? j : 0] + 7u);
    for (k = 0; k < PB_NIN; k++)
        h = hash(h, (uint32_t)proj.cab[k] + 2u);
    h = hash(h, au.ev_t);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, FTR_H, K_BAR);
    cv_rect(0, 0, 240, 1, K_RULE);
    {   /* the line */
        int32_t x = 6, y = 2;
        if (cur.hold >= 0) {
            uint16_t c = jack_is_out(cur.hold) ? OUT_COL[cur.hold - PB_NIN] : K_TEXT;
            x = cv_text(x, y, &FONT_XS, "HOLDING", K_DIM) + 5;
            x = lbl(x, y, cur.hold, c) + 6;
            x = arrow(x, y + 8, jack_is_out(cur.hold) ? 1 : -1, K_DIM);
            if (j != cur.hold)
                lbl(x, y, j, K_TEXT);
            else
                cv_text(x, y, &FONT_XS, "a jack", K_DIM);
        } else if (cursor) {
            x = lbl(x, y, j, K_TEXT) + 6;
            if (!jack_is_out(j)) {
                x = arrow(x, y + 8, -1, K_DIM);
                if (proj.cab[j] >= 0)
                    lbl(x, y, PB_NIN + proj.cab[j], OUT_COL[proj.cab[j]]);
                else
                    cv_text(x, y, &FONT_XS, "empty", K_FAINT);
            } else {
                int i, first = -1, more = 0;
                for (i = 0; i < PB_NIN; i++)
                    if (proj.cab[i] == j - PB_NIN) {
                        if (first < 0)
                            first = i;
                        else
                            more++;
                    }
                x = arrow(x, y + 8, 1, K_DIM);
                if (first < 0) {
                    cv_text(x, y, &FONT_XS, "empty", K_FAINT);
                } else {
                    x = lbl(x, y, first, OUT_COL[j - PB_NIN]);
                    if (more) {
                        char t[6] = " +";
                        itoa_u((uint32_t)more, t + 2);
                        cv_text(x, y, &FONT_XS, t, K_DIM);
                    }
                }
            }
        } else if (ev && au.ev_kind == AU_CABLE + 1) {
            const pb_move_t *mv = &au.ev_mv;
            uint16_t c = OUT_COL[mv->out];
            if (mv->kind == MV_DEL) {
                x = cv_text(x, y, &FONT_XS, "OUT:", K_DIM) + 5;
                x = lbl(x, y, PB_NIN + mv->out, c) + 6;
                x = arrow(x, y + 8, 1, K_DIM);
                lbl(x, y, mv->in_old, K_DIM);
            } else {
                x = lbl(x, y, PB_NIN + mv->out, c) + 6;
                x = arrow(x, y + 8, 1, c);
                lbl(x, y, mv->in, K_TEXT);
            }
        } else if (ev && au.ev_kind == AU_TWIST + 1) {
            char t[16];
            x = cv_text(x, y, &FONT_XS, PB_MOD_NAME[au.ev_mod], ACCENT[au.ev_mod / 4]) + 4;
            caps(t, PB_PARAM_NAME[au.ev_mod], sizeof t);
            x = cv_text(x, y, &FONT_XS, t, K_DIM) + 6;
            pb_param_text(au.ev_mod, au.ev_val, t);
            cv_text(x, y, &FONT_XS, t, K_TEXT);
        } else if (ev && au.ev_kind == AU_SEQ + 1) {
            char t[16] = "SEQ STEP ";
            itoa_u(au.ev_mod + 1u, t + 9);
            x = cv_text(x, y, &FONT_XS, t, ACCENT[0]) + 6;
            cv_text(x, y, &FONT_XS, au.sto < 0 ? "rest" : "new note", K_DIM);
        } else {
            cv_text(x, y, &FONT_XS, proj.autor ? "self-patching" : "auto off: knob 1", K_FAINT);
        }
    }
    for (k = 0; k < 4; k++) {                      /* the knobs */
        int32_t x = k * 60, lo, hi, v = knob_get(k), on = hot && ui.touched == k;
        char t[24], c2[16];
        uint16_t ac = k == KN_MOD ? ACCENT[proj.sel / 4] : ACCENT[k == KN_AUTO ? 1 : 0];
        if (k)
            cv_rect(x, 19, 1, 21, K_RULE);
        if (k == KN_MOD) {
            int32_t xx = x + 5;
            xx = cv_text(xx, 18, &FONT_T, PB_MOD_NAME[proj.sel], on ? K_TEXT : ac) + 3;
            caps(c2, PB_PARAM_NAME[proj.sel], sizeof c2);
            if (xx + text_w(&FONT_T, c2) < x + 60)
                cv_text(xx, 18, &FONT_T, c2, K_DIM);
        } else {
            caps(c2, KNOB_NAME[k], sizeof c2);
            cv_text(x + 5, 18, &FONT_T, c2, on ? K_TEXT : K_DIM);
        }
        knob_text(k, t);
        cv_text(x + 5, 25, &FONT_B, t, on ? ac : K_TEXT);
        knob_range(k, &lo, &hi);
        cv_rect(x + 5, 40, 50, 1, K_RULE);
        cv_rect(x + 5, 40, hi > lo ? (v - lo) * 50 / (hi - lo) : 0, 1, ac);
    }
    cv_blit(0, FTR_Y);
}

static void leds(void)
{
    uint32_t b = 0, k = 0, i, sc;
    int st[8], n = pb_scale(proj.g[G_SCALE], st);
    sc = 0;
    for (i = 0; i < (uint32_t)n; i++)
        sc |= 1u << ((proj.g[G_ROOT] + st[i]) % 12);
    if (pb_playing)
        b |= 1u << B_PLAY;
    if (proj.autor && plat_ms() >= au.hand_until)
        b |= 1u << B_REC;
    if (cur.hold >= 0 && (plat_ms() / 300u) % 2u)
        b |= 1u << B_EDIT;
    if (proj.leds)
        for (i = 0; i < NKEYS; i++)
            if (sc >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    plat_glow(proj.leds == 1);
    plat_leds(b, k);
}

/* AUTOSAVE and the flash's wear: every save erases one of the same two 4 KiB sectors. A setting (a knob, a preset,
 * a cable by hand) is saved 5 s after it rests, at most every 5 minutes; what the auto-patcher changed, at most once
 * an hour (a power-off can lose up to an hour of its moves); once when the battery reads empty; SAVE at once. A due
 * save waits for a quiet moment in the music (up to 10 s for a setting, a minute for the patcher's), never in the
 * middle of a move or with a cable held. */
#define KEEP_SETTINGS_MS (5u * 60000u)
#define KEEP_WORLD_MS (60u * 60000u)
#define SAVE_SETTINGS_MS 5000u
static uint8_t keep_low;
static int keep_due(uint32_t now)
{
    uint32_t since = now - ui.saved_t;
    if (!ui.dirty && !au.changed)
        return 0;
    if (ui.dirty && now - ui.dirty_t < SAVE_SETTINGS_MS)
        return 0;
    if (batt() == 0 && !keep_low && since >= KEEP_SETTINGS_MS)
        return 2;
    return since >= KEEP_WORLD_MS || (ui.dirty && since >= KEEP_SETTINGS_MS);
}
static void autosave(void)
{
    uint32_t now = plat_ms();
    int due, quiet;
    if (batt() != 0)
        keep_low = 0;
    due = keep_due(now);
    if (!due || ui.btn || au.busy || cur.hold >= 0) {
        if (!due)
            ui.due_t = 0;
        return;
    }
    if (!ui.due_t)
        ui.due_t = now ? now : 1u;
    quiet = !pb_playing || pb_level < 0.08f || now - ui.due_t >= (ui.dirty ? 10000u : 60000u);
    if (!quiet)
        return;
    if (project_save() == 0) {
        ui.dirty = 0;
        au.changed = 0;
    }
    ui.saved_t = now;
    ui.due_t = 0;
    if (due == 2)
        keep_low = 1;
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader on the computer (announce.c; fm1-reader): everything when it starts
 * listening or asks (WHERE); the cursor's module and jack as they settle; a knob as it turns; a help card; each move
 * of the auto-patcher (queued); what a button did (say). Nothing is built while no reader listens. */
static struct {
    uint8_t sel, jack, help, started, playing;
    int8_t hold;
    int16_t val[4];
    uint32_t ev_t;
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
    itoa_u(v, t);
    sp_cat(t);
}
static void sp_start(void)
{
    sp_n = 0;
    sp_buf[0] = 0;
}
static void sp_words(const char *t)           /* a value as it is said: units in words */
{
    char w[24];
    int i = 0, k = 0;
    if (t[0] == '/') {
        sp_cat("every ");
        t++;
    } else if (t[0] == 'x') {
        sp_cat("times ");
        t++;
    } else if (t[0] == '.') {
        sp_cat("0");
    }
    while (t[i] && t[i] != ' ' && k < 20)
        w[k++] = t[i++];
    w[k] = 0;
    sp_cat(w);
    if (!t[i])
        return;
    t += i + 1;
    sp_cat(" ");
    sp_cat(t[0] == 'H' ? "hertz" : t[0] == 'k' ? "kilohertz" : t[0] == '/' ? "per second" : t[0] == 's' && !t[1] ?
           "seconds" : t[0] == 'm' ? "minutes" : t);
}
static void sp_knob(int k)
{
    char t[24];
    if (k == KN_MOD) {
        sp_cat(PB_MOD_WORDS[proj.sel]);
        sp_cat(" ");
        sp_cat(PB_PARAM_NAME[proj.sel]);
    } else {
        sp_cat(KNOB_NAME[k]);
    }
    sp_cat(" ");
    knob_text(k, t);
    if (k == KN_AUTO && proj.autor)
        sp_cat("every ");
    sp_words(t);
}
static void sp_jack(int j)                    /* "Filter cut" */
{
    char t[12];
    int i;
    sp_cat(PB_MOD_WORDS[PB_JACK[j].mod]);
    sp_cat(" ");
    for (i = 0; PB_JACK[j].name[i] && i < 11; i++)
        t[i] = (char)(PB_JACK[j].name[i] >= 'A' && PB_JACK[j].name[i] <= 'Z' ? PB_JACK[j].name[i] + 32 :
                      PB_JACK[j].name[i]);
    t[i] = 0;
    sp_cat(t[0] == 's' && t[1] == '&' ? "sample and hold" : t[0] == 'c' && t[1] == 'v' && !t[2] ? "C V" : t);
}
static void sp_jack_where(int j)              /* "Cut, from LFO 2 sine" / "Out, to Filter in" */
{
    int i, n = 0;
    sp_jack(j);
    if (!jack_is_out(j)) {
        if (proj.cab[j] >= 0) {
            sp_cat(", from ");
            sp_jack(PB_NIN + proj.cab[j]);
        } else {
            sp_cat(", empty");
        }
        return;
    }
    for (i = 0; i < PB_NIN; i++)
        if (proj.cab[i] == j - PB_NIN) {
            sp_cat(n++ ? " and " : ", to ");
            sp_jack(i);
        }
    if (!n)
        sp_cat(", empty");
}
static void sp_module(void)                   /* the cursor's module: its knob and every jack */
{
    int js[4], n = pb_mod_jacks(proj.sel, js), k;
    sp_knob(KN_MOD);
    for (k = 0; k < n; k++) {
        sp_cat(". ");
        sp_jack_where(js[k]);
    }
}
static void sp_header(void)
{
    sp_cat("Preset ");
    sp_cat(PB_PRESET[proj.preset].name);
    sp_cat(", ");
    sp_cat(PB_NOTE_NAME[proj.g[G_ROOT]]);
    sp_cat(" ");
    sp_cat(PB_SCALE_NAME[proj.g[G_SCALE]]);
    sp_cat(", tempo ");
    sp_int(proj.g[G_TEMPO]);
    sp_cat(ui.playing ? ", playing" : ", paused");
    sp_cat(". ");
    sp_knob(KN_AUTO);
    if (proj.autor && plat_ms() < au.hand_until)
        sp_cat(", waiting: hands on the patch");
    sp_cat(". Space ");
    sp_int(proj.g[G_SPACE]);
    sp_cat(" percent. ");
    sp_int((uint32_t)pb_ncables(proj.cab));
    sp_cat(" cables");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static void sp_move(void)                     /* the last move of the auto-patcher */
{
    sp_start();
    if (au.ev_kind == AU_CABLE + 1) {
        const pb_move_t *mv = &au.ev_mv;
        if (mv->kind == MV_DEL) {
            sp_cat("Cable out: ");
            sp_jack(PB_NIN + mv->out);
            sp_cat(" from ");
            sp_jack(mv->in_old);
        } else {
            sp_cat("Cable: ");
            sp_jack(PB_NIN + mv->out);
            sp_cat(" to ");
            sp_jack(mv->in);
        }
    } else if (au.ev_kind == AU_TWIST + 1) {
        char t[16];
        sp_cat(PB_MOD_WORDS[au.ev_mod]);
        sp_cat(" ");
        sp_cat(PB_PARAM_NAME[au.ev_mod]);
        sp_cat(" ");
        pb_param_text(au.ev_mod, au.ev_val, t);
        sp_words(t);
    } else {
        sp_cat("Sequencer step ");
        sp_int(au.ev_mod + 1u);
        sp_cat(au.sto < 0 ? ", a rest" : ", a new note");
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
    sp_start();
    sp_cat(BTN_LABEL[ui.help - 1]);
    sp_cat(" button");
    for (k = 0; k < n; k++)
        sp_line(l[k]);
}
static void sp_preset(void)
{
    if (!ann_on())
        return;
    sp_start();
    sp_cat("PRESET ");
    sp_cat(PB_PRESET[proj.preset].name);
    sp_cat(", ");
    sp_cat(PB_NOTE_NAME[proj.g[G_ROOT]]);
    sp_cat(" ");
    sp_cat(PB_SCALE_NAME[proj.g[G_SCALE]]);
    sp_cat(", tempo ");
    sp_int(proj.g[G_TEMPO]);
    ann_say(sp_buf, 0, ANN_NOW);
    sp_said = 1;
}
static void sp_snap(void)
{
    int i;
    for (i = 0; i < 4; i++)
        sp.val[i] = (int16_t)knob_get(i);
    sp.sel = proj.sel;
    sp.jack = cur.jack;
    sp.help = ui.help;
    sp.hold = cur.hold;
    sp.ev_t = au.ev_t;
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
        sp_start();
        if (ui.help) {
            sp_help();
            ann_say(sp_buf, 0, ANN_NOW);
            sp_start();
        }
        sp_header();
        ann_say(sp_buf, 0, ui.help ? 0 : ANN_NOW);
        sp_start();
        sp_cat("Cursor on ");
        sp_module();
        ann_say(sp_buf, 0, 0);
        sp_said = 0;
        return;
    }
    if (sp_said) {
        sp_said = 0;
        sp_snap();
        return;
    }
    if (pb_playing != ui.playing && pb_playing == sp.playing) {
    }
    if (ui.help != sp.help) {
        sp.help = ui.help;
        if (ui.help) {
            sp_help();
            ann_say(sp_buf, 0, ANN_NOW);
            return;
        }
    }
    if (proj.sel != sp.sel) {                     /* a module: said when it settles */
        sp_snap();
        sp_start();
        sp_module();
        ann_later(sp_buf, ANN_NOW, 250u, now);
        return;
    }
    if (cur.jack != sp.jack) {
        sp_snap();
        sp_start();
        sp_jack_where(cursor_jack());
        ann_later(sp_buf, ANN_NOW, 150u, now);
        return;
    }
    for (i = 0; i < 4; i++) {
        int v = knob_get(i);
        if (v != sp.val[i] && !(i == KN_MOD && au.busy && au.what == AU_TWIST)) {
            sp.val[i] = (int16_t)v;
            if (changed < 0)
                changed = i;
        }
        sp.val[i] = (int16_t)v;
    }
    if (changed >= 0) {
        sp_start();
        sp_knob(changed);
        ann_later(sp_buf, ANN_NOW, 120u, now);
    }
    if (au.ev_t != sp.ev_t && au.ev_kind) {       /* a move: queued, after what is being said */
        sp.ev_t = au.ev_t;
        sp_move();
        ann_say(sp_buf, 0, 0);
    }
}

#ifdef PLAT_HOST
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}, {"PAUSED", 0},
                                         {"PLAYING", 0}, {"SAVED", 0}, {"DROPPED", 0}, {"MOVE", "A CABLE"},
                                         {"MOVE", "THE SEQUENCE"}, {"LIGHTS", "ON"}, {"LIGHTS", "KEYS"},
                                         {"LIGHTS", "OFF"}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}, {"AN INPUT", "TO PLUG IN"},
                                          {"AN OUTPUT", "TO PLUG IN"}, {"AUDIO IN", "TAKES AUDIO"},
                                          {"NO AUDIO", "INTO CV"}, {"NO LOOPS", "IN THE AUDIO"}};
    project_t keep = proj;
    uint8_t keep_help = ui.help, keep_jack = cur.jack;
    int m, i, x;
    char t[48], l[24];
    for (m = 0; m < PB_NMOD; m++) {
        int js[4], n = pb_mod_jacks(m, js);
        proj.sel = (uint8_t)m;
        sp_start();
        sp_module();
        sp_print("SCREEN", sp_buf);
        for (x = 0; x <= 100; x++) {
            proj.par[m] = (uint8_t)x;
            sp_start();
            sp_knob(KN_MOD);
            sp_print("KNOB", sp_buf);
        }
        proj = keep;
        proj.sel = (uint8_t)m;
        for (i = 0; i < n; i++) {
            sp_start();
            sp_jack_where(js[i]);
            sp_print("JACK", sp_buf);
            jack_label(js[i], l);
            msg_text("PICK AN INPUT", l, t);
            sp_print("MSG", t);
            msg_text("PICK AN OUTPUT", l, t);
            sp_print("MSG", t);
            msg_text("PULLED OUT", l, t);
            sp_print("MSG", t);
            msg_text("PATCHED", l, t);
            sp_print("MSG", t);
        }
        msg_text("MOVE", PB_MOD_NAME[m], t);
        sp_print("MSG", t);
    }
    proj = keep;
    for (x = 0; x < NAUTO; x++) {
        proj.autor = (uint8_t)x;
        sp_start();
        sp_knob(KN_AUTO);
        sp_print("KNOB", sp_buf);
    }
    for (x = 40; x <= 160; x += 30) {
        proj.g[G_TEMPO] = (uint8_t)x;
        sp_start();
        sp_knob(KN_TEMPO);
        sp_print("KNOB", sp_buf);
    }
    proj = keep;
    for (x = 0; x < 4; x++) {
        ui.playing = (uint8_t)(x & 1);
        ui.dirty = (uint8_t)(x >> 1);
        sp_start();
        sp_header();
        sp_print("WHERE", sp_buf);
    }
    for (i = 0; i < NB; i++) {
        const char *ll[HELP_LINES];
        if (!help_lines(i, ll))
            continue;
        ui.help = (uint8_t)(i + 1);
        sp_help();
        sp_print("HELP", sp_buf);
    }
    ui.help = keep_help;
    cur.jack = keep_jack;
    for (i = 0; i < (int)(sizeof MSG / sizeof MSG[0]); i++) {
        msg_text(MSG[i][0], MSG[i][1], t);
        sp_print("MSG", t);
    }
    for (i = 0; i < (int)(sizeof WARN / sizeof WARN[0]); i++) {
        msg_text(WARN[i][0], WARN[i][1], t);
        sp_print("WARN", t);
    }
    for (x = 0; x < 12; x++) {
        msg_text("KEY", PB_NOTE_NAME[x], t);
        sp_print("MSG", t);
    }
    for (x = 0; x < PB_NSCALES; x++) {
        msg_text("SCALE", PB_SCALE_NAME[x], t);
        sp_print("MSG", t);
    }
    for (x = 0; x < PB_NPRESETS; x++) {
        project_preset(x);
        sp_start();
        sp_cat("PRESET ");
        sp_cat(PB_PRESET[x].name);
        sp_print("MSG", sp_buf);
        msg_text("RESET", PB_PRESET[x].name, t);
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
    lcd_fill(0, 0, 240, 240, K_BG);
    if (!pb_playing)
        pb_play(1);                              /* it plays from the start */
    ui.playing = 1;
    sp.playing = 1;
    au.seed = plat_ms() * 2654435761u + 0x9E37u;
    if (!au.seed)
        au.seed = 1;
    bay_reset();
}

void ui_frame(void)
{
    uint32_t now = plat_ms();
    input();
    au_tick(now, pb_playing);
    autosave();
    history();
    draw_header();
    draw_main();
    draw_footer();
    leds();
    speak();
    ann_tick(now);
    ui.frame++;
}

void ui_input_only(void) { input(); }
