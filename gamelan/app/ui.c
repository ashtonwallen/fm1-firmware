/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; GAMELAN: Copyright (C) 2026 ashtonwallen */
/* GAMELAN UI: the panel and the screen (after AMB-1's: its knob strip, help cards, saving and screen reader). Part
 * of the unity build (after gfx.c and project.c); the host simulator includes it too.
 *
 *   white keys     a bar in the tuning (from low 3 up)      black keys  gong, kempul, kenong, ketuk, kempyang, drum
 *   PLAY           plays / rests             EDIT   a new piece: this one ends at the next kenong or gong
 *   REC            HOLD: stay in this piece  OCT- / OCT+  a slower / faster irama (the kendang leads it)
 *   SELECT pages (HOME held: tempo)   ALGORITHM the tuning   PRESETS the preset
 *   HOME ENV ARP GLO   the pages: Gamelan, Mix, Colour, Setup
 *   KNOB 1-4       the page's four values
 *   SAVE           saves at once (it also saves by itself, at a quiet moment); held 1.5 s: the preset's recipe */
#include "fastmath.h"

enum { V_HOME, V_MIX, V_COLOUR, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Gamelan", "Mix", "Colour", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_ARP, B_GLO};
enum { K_LEDS = 100, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_TEMPO, P_DENSITY, P_TUNING, P_OMBAK},
    {P_GONGS, P_BALUNGAN, P_KOTEKAN, P_BONANG},
    {P_KENDANG, P_SULING, P_REVERB, P_MALLET},
    {P_PITCH, P_IRAMA, P_FORM, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Tempo", "Density", "Tuning", "Ombak"},
    {"Gongs", "Balungan", "Kotekan", "Bonang"},
    {"Kendang", "Suling", "Reverb", "Mallet"},
    {"Pitch", "Irama", "Form", "Lights"},
};

/* the colours: one set a preset (dawn cream, night blue, temple stone ...). Each: the ground, a panel, rules,
 * dim text, text, the accent, then the gong, kenong, kempul and the elaboration's petals */
typedef struct {
    uint16_t bg, panel, line, dim, text, acc, gong, kenong, kempul, elab;
} pal_t;
#define P(a, b, c) RGB(a, b, c)
static const pal_t PAL[NPRESETS] = {
    {P(250, 238, 218), P(255, 248, 236), P(228, 204, 178), P(140, 100, 80), P(62, 32, 22), P(196, 84, 76),
     P(200, 140, 40), P(190, 96, 66), P(150, 108, 60), P(220, 120, 100)},                            /* Pelog Dawn */
    {P(14, 20, 40), P(26, 34, 62), P(46, 58, 96), P(116, 130, 176), P(222, 228, 246), P(132, 172, 255),
     P(232, 202, 122), P(150, 182, 250), P(190, 160, 232), P(120, 210, 232)},                        /* Slendro Night */
    {P(38, 34, 30), P(54, 48, 42), P(82, 72, 62), P(156, 140, 118), P(238, 228, 208), P(228, 162, 72),
     P(242, 182, 82), P(212, 142, 72), P(182, 122, 92), P(232, 202, 142)},                           /* Temple */
    {P(222, 230, 226), P(238, 244, 240), P(194, 208, 202), P(96, 118, 112), P(28, 46, 44), P(44, 118, 108),
     P(56, 98, 120), P(66, 138, 118), P(108, 128, 88), P(78, 158, 170)},                             /* Rain Court */
    {P(54, 14, 14), P(80, 24, 22), P(112, 40, 34), P(196, 136, 104), P(255, 234, 204), P(255, 192, 62),
     P(255, 202, 82), P(242, 122, 62), P(222, 94, 72), P(255, 172, 92)},                             /* Kotekan */
    {P(24, 18, 10), P(40, 30, 18), P(70, 54, 30), P(156, 126, 84), P(242, 222, 182), P(222, 162, 62),
     P(244, 184, 72), P(202, 142, 72), P(172, 112, 62), P(232, 192, 112)},                           /* Gong Ageng */
    {P(12, 46, 52), P(20, 64, 70), P(36, 92, 98), P(122, 182, 182), P(222, 246, 242), P(92, 222, 202),
     P(242, 212, 132), P(112, 212, 222), P(152, 192, 242), P(132, 242, 212)},                        /* Water Palace */
    {P(30, 30, 50), P(44, 44, 70), P(66, 66, 100), P(152, 152, 192), P(234, 234, 250), P(202, 202, 255),
     P(242, 232, 182), P(192, 192, 242), P(172, 152, 222), P(212, 222, 255)},                        /* Moon Pavilion */
    {P(232, 240, 206), P(244, 250, 226), P(204, 218, 166), P(98, 118, 58), P(34, 50, 20), P(84, 136, 36),
     P(158, 118, 28), P(106, 148, 48), P(138, 108, 58), P(66, 156, 88)},                             /* Rice Terraces */
    {P(20, 12, 8), P(36, 22, 14), P(64, 40, 24), P(172, 122, 72), P(255, 222, 162), P(255, 162, 62),
     P(255, 192, 92), P(232, 132, 62), P(202, 102, 62), P(255, 202, 122)},                           /* Shadow Play */
    {P(244, 232, 206), P(252, 244, 226), P(222, 202, 166), P(128, 98, 58), P(50, 36, 18), P(166, 106, 36),
     P(178, 118, 38), P(138, 108, 48), P(108, 128, 58), P(188, 128, 68)},                            /* Bronze Garden */
    {P(34, 44, 52), P(48, 60, 70), P(72, 86, 98), P(142, 162, 176), P(228, 236, 242), P(122, 202, 232),
     P(222, 202, 142), P(132, 192, 222), P(152, 172, 202), P(112, 222, 212)},                        /* Monsoon */
    {P(46, 30, 40), P(62, 42, 54), P(90, 62, 80), P(172, 142, 162), P(242, 228, 236), P(232, 152, 192),
     P(242, 202, 152), P(212, 152, 192), P(182, 142, 202), P(242, 182, 212)},                        /* Incense */
    {P(236, 232, 212), P(248, 246, 232), P(212, 206, 178), P(108, 106, 78), P(40, 40, 28), P(58, 128, 88),
     P(168, 128, 48), P(78, 138, 98), P(118, 118, 68), P(88, 148, 118)},                             /* Palm Shade */
};
#undef P
static const pal_t *cp = &PAL[0];

static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];
    uint32_t down_t[NB];
    uint32_t turn_t;
    uint8_t help;                      /* a button's card is up: the button + 1 */
    uint8_t reset_done;
    uint8_t dirty;                     /* the project changed since the last save */
    uint32_t act_t, saved_t, dirty_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
} ui;

/* the screen reader (announce.c; speak() below): a message went to it this frame */
static uint8_t sp_said;
static void sp_preset(void);
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
static char *cat_s(char *b, const char *s)
{
    while ((*b = *s++))
        b++;
    return b;
}

static void mark_dirty(void)
{
    if (!ui.dirty)
        ui.dirty_t = plat_ms();
    ui.dirty = 1;
}
int ui_dirty(void) { return ui.dirty != 0; }

/* ------------------------------------------------------------ values --- */
static int knob_get(int k) { return k < P_NPARAMS ? proj.par[k] : k == K_LEDS ? proj.leds : 0; }
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

/* knob acceleration by speed (X0X's, as AMB-1) */
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
#ifdef PLAT_HOST
    accel_off = plat_host_exact;           /* the simulator's "spin": exact steps */
#endif
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
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------ actions --- */
static void set_preset(int i)
{
    project_preset(i);
    cp = &PAL[proj.preset];
    project_apply();
    en_seed(plat_ms());
    en_restart();
    ui.sig[0] = ui.sig[1] = ui.sig[2] = 0;
    say_as("PRESET", preset_name(proj.preset), 0);
    sp_preset();
    mark_dirty();
}
static void set_tuning(int t)
{
    t = (t % EN_NTUNING + EN_NTUNING) % EN_NTUNING;
    knob_set(P_TUNING, t);
    say("TUNING", EN_TUNING_LONG[t]);
}

static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

static void key_down(int k)                     /* white keys: bars; black keys: the gongs and the drum */
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    static const uint8_t KIND[11] = {K_GONG, K_KEMPUL, K_KENONG, K_KETUK, K_KEMPYANG, K_DHE, K_TAK, K_TUNG,
                                     K_KENONG, K_KEMPUL, K_GONG};
    int i, d = en_cur >= 0 ? en_cur : 7;
    for (i = 0; i < 16; i++)
        if (WK[i] == k) {
            en_key(K_BAR, 3 + i);
            return;
        }
    for (i = 0; i < 11; i++)
        if (BK[i] == k) {
            en_key(KIND[i], d);
            return;
        }
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
            say("RESTING", 0);
        } else {
            en_seed(plat_ms());
            en_play(1);
            say("PLAYING", 0);
        }
        break;
    case B_EDIT:
        en_new_piece();
        say("NEW PIECE", en_state == S_PLAY ? "AT THE NEXT GONG" : 0);
        break;
    case B_REC:
        proj.hold = (uint8_t)!proj.hold;
        en_hold(proj.hold);
        say("HOLD", proj.hold ? "ON" : "OFF");
        mark_dirty();
        break;
    case B_OCTDN: case B_OCTUP: {
        int dir = b == B_OCTDN ? 1 : -1, to = en_level + dir;
        if (to < 0 || to > 2 || en_state != S_PLAY) {
            warn("IRAMA", to < 0 ? "AT ITS FASTEST" : to > 2 ? "AT ITS SLOWEST" : "NOT NOW");
            break;
        }
        en_irama(dir);
        if (proj.par[P_IRAMA])                   /* a set irama follows */
            knob_set(P_IRAMA, to + 1);
        say("IRAMA", EN_IRAMA_NAME[to + 1]);
        break;
    }
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

static void midi_in(void)                       /* drained: nothing to do with it */
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0))
        ;
}

#define HELP_HOLD 900u
#define RESET_HOLD 1500u
static int help_lines(int b, const char **l);
static void help_tick(uint32_t btn, uint32_t keys)
{
    uint32_t now = plat_ms();
    int i;
    const char *l[4];
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
        if ((ch & keys) >> i & 1u)
            key_down((int)i);
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {        /* SELECT: the pages; with HOME held, the tempo */
        if (btn >> B_HOME & 1u) {
            turn(EN_SELECT, P_TEMPO, e);
            ui.btn_used |= 1u << B_HOME;
            if (ui.view == V_HOME) {
                ui.touched = 0;
                ui.touch_until = plat_ms() + 1200u;
            } else {
                char t[8];
                itoa_u((uint32_t)proj.par[P_TEMPO], t);
                say("TEMPO", t);
            }
        } else {
            int v = (int)ui.view + (e > 0 ? 1 : -1);
            if (v >= 0 && v < NVIEWS)
                set_view(v);
        }
        ui.act_t = ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        set_tuning(proj.par[P_TUNING] + (e > 0 ? 1 : -1));
        ui.act_t = ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        set_preset(proj.preset + (e > 0 ? 1 : -1));
        ui.act_t = ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && plat_ms() - ui.down_t[B_SAVE] > RESET_HOLD) {
        ui.reset_done = 1;                       /* held: the preset back to its recipe (not saved) */
        ui.btn_used |= 1u << B_SAVE;
        ui.help = 0;
        project_preset_reset();
        project_apply();
        say("RESET", preset_name(proj.preset));
        mark_dirty();
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            turn(EN_K1 + (int)i, VIEW_KNOB[ui.view][i], e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 1200u;
            ui.act_t = ui.turn_t = plat_ms();
        }
    help_tick(btn, keys);
}

/* ------------------------------------------------------------- screen --- */
/* three bands: the header (28 px), the gongan (144), the knobs (68) */
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

/* anti-aliased drawing (X0X's, as AMB-1): a pixel blended over the canvas, a disc, a ring, a knob's arc */
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
static void disc_a(float cx, float cy, float r, uint16_t c, float alpha)
{
    int32_t x0 = (int32_t)fm_floorf(cx - r - 1.0f), x1 = (int32_t)(cx + r + 1.0f);
    int32_t y0 = (int32_t)fm_floorf(cy - r - 1.0f), y1 = (int32_t)(cy + r + 1.0f), x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy);
            px_blend(x, y, c, (uint32_t)(fm_clampf(r - d + 0.5f, 0.0f, 1.0f) * alpha * 16.0f + 0.5f));
        }
}
static void disc(float cx, float cy, float r, uint16_t c) { disc_a(cx, cy, r, c, 1.0f); }
static float turns(float y, float x)            /* atan2 in turns, 0..1 from +x going clockwise (y down) */
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
/* a ring of radius r and width th, from the top clockwise to `upto` turns (1: whole), alpha 0..1; row by row, only
 * the ring's own pixels */
static void ring(float cx, float cy, float r, float th, uint16_t c, float alpha, float upto)
{
    float ro = r + th * 0.5f, ri = r - th * 0.5f;
    int32_t y, y0 = (int32_t)fm_floorf(cy - ro - 1.0f), y1 = (int32_t)(cy + ro + 1.0f);
    if (alpha <= 0.02f || ro < 1.0f)
        return;
    for (y = y0; y <= y1; y++) {
        float dy = (float)y + 0.5f - cy, xo2 = (ro + 1.0f) * (ro + 1.0f) - dy * dy, xi2, xo, xi;
        int32_t side;
        if (xo2 <= 0.0f)
            continue;
        xo = fm_sqrtf(xo2);
        xi2 = (ri - 1.0f) * (ri - 1.0f) - dy * dy;
        xi = ri > 1.0f && xi2 > 0.0f ? fm_sqrtf(xi2) : 0.0f;
        for (side = -1; side <= 1; side += 2) {
            int32_t xa = (int32_t)fm_floorf(cx + (side < 0 ? -xo : xi)), xb = (int32_t)(cx + (side < 0 ? -xi : xo)), x;
            for (x = xa; x <= xb; x++) {
                float dx = (float)x + 0.5f - cx, d = fm_sqrtf(dx * dx + dy * dy), a;
                a = fm_clampf(ro + 0.5f - d, 0.0f, 1.0f) * fm_clampf(d - ri + 0.5f, 0.0f, 1.0f);
                if (a <= 0.0f)
                    continue;
                if (upto < 1.0f) {
                    float t = turns(dy, dx) + 0.25f;
                    if (t >= 1.0f)
                        t -= 1.0f;
                    if (t > upto)
                        continue;
                }
                px_blend(x, y, c, (uint32_t)(a * alpha * 16.0f + 0.5f));
            }
        }
    }
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
#define K_LOW RGB(214, 84, 52)
#define BATT_W 20
static void draw_batt(int32_t x, int b)
{
    static const int8_t BOLT[7][2] = {{10, 2}, {9, 2}, {8, 2}, {7, 5}, {9, 2}, {8, 2}, {7, 2}};
    uint16_t c = b <= 1 ? K_LOW : cp->dim;
    int i;
    cv_round(x, 9, 18, 10, 2, c);
    cv_rect(x + 1, 10, 16, 8, cp->bg);
    cv_rect(x + 18, 12, 2, 4, c);
    if (b == 4) {
        for (i = 0; i < 7; i++)
            cv_rect(x + BOLT[i][0], 10 + i, BOLT[i][1], 1, cp->text);
        return;
    }
    for (i = 0; i < b && i < 3; i++)
        cv_rect(x + 2 + i * 5, 11, 4, 6, b <= 1 ? K_LOW : cp->acc);
}
static const char *batt_words(int b)
{
    static const char *const W[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                     "On USB power"};
    return b >= 0 && b <= 4 ? W[b] : 0;
}

/* the header: the preset in a tag, the tuning; on the right the battery, a dot while unsaved, HOLD, the tempo with
 * a light on the balungan's beat. Messages take its place for a moment. */
static void draw_header(void)
{
    char t[24];
    int msg = plat_ms() < ui.msg_until, beat_on = en_playing && en_glow[K_BAR] > 0.55f;
    uint32_t h = hash(hash(hash(2166136261u, proj.preset | (uint32_t)proj.par[P_TUNING] << 8 | (uint32_t)proj.hold << 12),
                           en_playing | ui.dirty << 4 | (uint32_t)msg << 5 | (uint32_t)beat_on << 6),
                      (uint32_t)proj.par[P_TEMPO] | (uint32_t)(batt() + 1) << 24);
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1]), ui.msg_until);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, cp->bg);
    if (msg) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], cp->text);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], cp->acc);
    } else {
        int32_t x = 232, w;
        if (batt() >= 0) {
            x -= BATT_W;
            draw_batt(x, batt());
            x -= 6;
        }
        if (ui.dirty) {
            cv_round(x - 5, 12, 5, 5, 2, cp->dim);
            x -= 11;
        }
        itoa_u((uint32_t)proj.par[P_TEMPO], t);
        x -= text_w(&FONT_S, t);
        cv_text(x, 6, &FONT_S, t, en_playing ? cp->text : cp->dim);
        x -= 16;
        if (en_playing) {
            cv_round(x, 9, 10, 10, 5, beat_on ? cp->acc : cp->line);
        } else {
            int32_t j;
            for (j = 0; j < 5; j++)
                cv_rect(x + 2 + j, 9 + j, 1, 11 - 2 * j, cp->dim);
        }
        if (proj.hold) {
            int32_t hw = text_w(&FONT_XS, "HOLD") + 8;
            x -= hw + 6;
            cv_round(x, 7, hw, 14, 4, cp->line);
            cv_text(x + 4, 7, &FONT_XS, "HOLD", cp->text);
        }
        w = text_w(&FONT_B, preset_name(proj.preset)) + 12;
        cv_round(4, 4, w, 20, 6, cp->text);
        cv_text(10, 6, &FONT_B, preset_name(proj.preset), cp->bg);
        w += 10;
        if (w + text_w(&FONT_XS, EN_TUNING_LONG[proj.par[P_TUNING]]) + 4 <= x)
            cv_text(w, 8, &FONT_XS, EN_TUNING_LONG[proj.par[P_TUNING]], cp->dim);
        else if (w + text_w(&FONT_XS, EN_TUNING_NAME[proj.par[P_TUNING]]) + 4 <= x)
            cv_text(w, 8, &FONT_XS, EN_TUNING_NAME[proj.par[P_TUNING]], cp->dim);
    }
    cv_blit(0, 0);
}

/* the gongan as a mandala: the beats round a ring, the gong at the top (the cycle ends and begins there). Outside,
 * what marks each beat: gong, kenong, kempul, ketuk (and kempyang); a strike blooms. Inside, the balungan in its
 * numbers (a dot above: high, below: low), the beat now lit. At the heart, a lotus of the five tones: a petal lights
 * as the elaboration plays it, and the balungan's note in the middle. The corners: the form and the gongan, the
 * irama, what the piece is doing, the kendang. */
#define CX 120
#define CY 72
#define R_MARK 61.0f
#define R_DIG 45.0f
#define R_PETAL 20.0f

static void beat_xy(int b, int n, float r, float *x, float *y)   /* beat b's place: (b+1)/n of a turn from the top */
{
    float a = (float)(b + 1) * (FM_TWO_PI / (float)n);
    *x = (float)CX + fm_sinf(a) * r;
    *y = (float)CY - fm_cosf(a) * r;
}
static const char *tone_label(int deg, char *t)
{
    int o = deg / EN_NTONE;
    t[0] = EN_TONE_LABEL[en_tuning % EN_NTUNING][deg - o * EN_NTONE];
    t[1] = 0;
    return t;
}
static const char *state_word(void)
{
    if (!en_playing)
        return "resting";
    switch (en_state) {
    case S_BUKA: return "buka";
    case S_SUWUK: return "suwuk";
    case S_GAP: return "new piece";
    default: break;
    }
    if (en_trend > 0)
        return "slowing";
    if (en_trend < 0)
        return "quickening";
    if (en_sirep)
        return "sirep";
    return "";
}

static void draw_mandala(void)
{
    int n = en_nbeats ? en_nbeats : 16, b, cb = en_beat;
    float hand = (en_pos + 1.0f) / (float)n, gg = en_glow[K_GONG];
    char t[4];
    if (hand >= 1.0f)
        hand -= 1.0f;
    /* the gong's bloom, behind it all */
    if (gg > 0.03f) {
        float r = 10.0f + (1.0f - gg) * 66.0f;
        ring(CX, CY, r, 3.0f + 4.0f * gg, cp->gong, gg * 0.55f, 1.0f);
        ring(CX, CY, r * 0.62f, 2.0f, cp->gong, gg * 0.35f, 1.0f);
        disc_a(CX, CY, 30.0f, cp->gong, gg * 0.18f);
    }
    ring(CX, CY, R_MARK, 1.2f, cp->line, 1.0f, 1.0f);
    ring(CX, CY, R_DIG + 9.0f, 1.0f, cp->line, 0.6f, 1.0f);
    if (en_playing && en_state != S_GAP)        /* the way round so far */
        ring(CX, CY, R_MARK, 3.0f, cp->acc, 0.55f, hand);
    for (b = 0; b < n; b++) {                   /* the colotomy */
        uint32_t m = 1u << b;
        float x, y;
        int now = b == cb && en_playing;
        beat_xy(b, n, R_MARK, &x, &y);
        if (en_mk_gong & m) {
            disc(x, y, 6.5f, mix(cp->gong, cp->text, now ? q16(gg) / 2 : 0));
            disc(x, y, 3.0f, cp->bg);
        } else if (en_mk_kenong & m) {
            disc(x, y, 4.5f, now ? mix(cp->kenong, cp->text, q16(en_glow[K_KENONG]) / 2) : cp->kenong);
        } else if (en_mk_kempul & m) {
            disc(x, y, 3.6f, cp->kempul);
            disc(x, y, 1.6f, cp->bg);
        } else if (en_mk_ketuk & m) {
            disc(x, y, 2.4f, now ? mix(cp->dim, cp->text, q16(en_glow[K_KETUK])) : cp->dim);
        } else if (en_mk_kempyang & m) {
            disc(x, y, 1.3f, cp->dim);
        } else {
            disc(x, y, 1.0f, cp->line);
        }
        if ((en_mk_kenong & m) && b == en_kenong_at && en_glow[K_KENONG] > 0.03f) {   /* the kenong blooms */
            float g = en_glow[K_KENONG];
            ring(x, y, 5.0f + (1.0f - g) * 13.0f, 2.0f, cp->kenong, g * 0.8f, 1.0f);
        }
        if ((en_mk_kempul & m) && now && en_glow[K_KEMPUL] > 0.03f) {
            float g = en_glow[K_KEMPUL];
            ring(x, y, 4.0f + (1.0f - g) * 10.0f, 1.6f, cp->kempul, g * 0.8f, 1.0f);
        }
    }
    for (b = 0; b < n; b++) {                   /* the balungan, in its numbers */
        int d = en_bal[b];
        float x, y;
        int now = b == cb && en_playing && en_state != S_GAP;
        beat_xy(b, n, R_DIG, &x, &y);
        if (now)
            disc_a(x, y, 7.5f, cp->elab, 0.45f + 0.5f * en_glow[K_BAR]);
        if (d < 0) {
            disc(x, y, 1.1f, cp->dim);
            continue;
        }
        tone_label(d, t);                       /* a gatra's last note (its seleh) in ink, the rest lighter */
        text_c((int32_t)x, (int32_t)y - 8, &FONT_XS, t, now || b % 4 == 3 || n <= 8 ? cp->text : cp->dim);
        if (d >= 2 * EN_NTONE || d < EN_NTONE) { /* the octave: a dot outward (high) or inward (low) */
            float ox, oy;
            beat_xy(b, n, d < EN_NTONE ? R_DIG - 7.0f : R_DIG + 6.5f, &ox, &oy);
            disc(ox, oy, 1.0f, now ? cp->text : cp->dim);
        }
    }
    for (b = 0; b < EN_NTONE; b++) {            /* the lotus */
        float a = (float)b * (FM_TWO_PI / (float)EN_NTONE), x = (float)CX + fm_sinf(a) * R_PETAL;
        float y = (float)CY - fm_cosf(a) * R_PETAL, g = en_tone_glow[b];
        int cur = en_cur >= 0 && en_cur % EN_NTONE == b;
        uint16_t c = mix(cp->panel, cp->elab, 3 + q16(g) * 13 / 16);
        disc(x, y, 7.5f, cur ? mix(c, cp->acc, 6) : c);
        t[0] = EN_TONE_LABEL[en_tuning % EN_NTUNING][b];
        t[1] = 0;
        text_c((int32_t)x, (int32_t)y - 7, &FONT_XS, t, g > 0.5f ? cp->bg : cp->dim);
    }
    if (en_cur >= 0 && en_playing) {           /* the balungan's note, in the middle */
        tone_label(en_cur, t);
        text_c(CX, CY - 13, &FONT_M, t, cp->text);
    }
    if (en_playing && en_state != S_GAP) {      /* the hand */
        float x, y, a = hand * FM_TWO_PI;
        x = (float)CX + fm_sinf(a) * R_MARK;
        y = (float)CY - fm_cosf(a) * R_MARK;
        disc(x, y, 3.2f, cp->bg);
        disc(x, y, 2.4f, cp->acc);
    }
}

static void draw_corners(void)
{
    char t[16];
    const char *s = state_word();
    cv_text(4, 2, &FONT_XS, EN_FORM_NAME[en_form % EN_NFORM], cp->dim);
    cat_s(cat_s(t, "gongan "), "");
    itoa_u(en_gongan, t + 7);
    if (en_playing && en_state != S_BUKA && en_gongan)
        cv_text(4, 16, &FONT_XS, t, cp->dim);
    cat_s(cat_s(t, "IRAMA "), EN_IRAMA_NAME[1 + (en_level % 3)]);
    cv_text(236 - text_w(&FONT_XS, t), 2, &FONT_XS, t, cp->acc);
    if (proj.par[P_IRAMA])
        cv_text(236 - text_w(&FONT_XS, "set"), 16, &FONT_XS, "set", cp->dim);
    if (s[0])
        cv_text(4, 126, &FONT_XS, s, en_state == S_SUWUK || en_trend ? cp->acc : cp->dim);
    {                                            /* the kendang: a barrel, its two heads lit as it is struck */
        int32_t x = 206, y = 132;
        cv_round(x - 9, y - 5, 18, 10, 4, cp->line);
        disc_a((float)x - 9.0f, (float)y, 4.5f, cp->kempul, 0.35f + 0.65f * en_glow[K_DHE]);
        disc_a((float)x + 9.0f, (float)y, 3.5f, cp->elab, 0.35f + 0.65f * fm_maxf(en_glow[K_TAK], en_glow[K_TUNG]));
    }
}

/* a button's card (help_tick): its name, then what it does */
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char **l)
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("GAMELAN"); L("TEMPO, DENSITY, TUNING,"); L("OMBAK"); L("+ SELECT: TEMPO"); break;
    case B_ENV: L("THE MIX"); L("GONGS, BALUNGAN,"); L("KOTEKAN, BONANG"); break;
    case B_ARP: L("COLOUR"); L("KENDANG, SULING, REVERB,"); L("MALLET"); break;
    case B_GLO: L("SETUP"); L("PITCH, IRAMA, FORM, LIGHTS"); break;
    case B_EDIT: L("A NEW PIECE:"); L(" THIS ONE ENDS"); L("AT THE NEXT GONG"); break;
    case B_REC: L("HOLD: STAY IN"); L(" THIS PIECE"); L("AGAIN: MOVE ON"); break;
    case B_OCTDN: L("A SLOWER IRAMA:"); L(" THE DRUM LEADS IT"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("A FASTER IRAMA"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES EVERY PRESET"); L("KEEP HOLDING: THE PRESET"); L(" BACK TO ITS RECIPE"); break;
    default: break;
    }
#undef L
    return n;
}
static void draw_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    cv_round(16, 14, 208, 116, 6, cp->panel);
    cv_round(16, 14, 208, 26, 6, cp->text);
    cv_rect(16, 34, 208, 6, cp->text);
    text_c(120, 18, &FONT_B, BTN_LABEL[ui.help - 1], cp->bg);
    for (k = 0; k < n; k++)
        text_c(120, 50 + k * 19, &FONT_S, l[k] + (l[k][0] == ' '), cp->text);
}

static void draw_main(void)
{
    uint32_t h;
    int i;
    if (ui.frame % 3u)                           /* 20 frames a second is plenty */
        return;
    if (ui.help) {
        h = hash(0x48454C50u, ui.help | (uint32_t)proj.preset << 8);
        if (h != ui.sig[1]) {
            ui.sig[1] = h;
            cv_begin(240, MAIN_H, cp->bg);
            draw_help();
            cv_blit(0, MAIN_Y);
        }
        return;
    }
    h = hash(2166136261u, (uint32_t)(en_pos * 24.0f) | (uint32_t)en_state << 12 | (uint32_t)en_playing << 16 |
                              (uint32_t)en_form << 17 | (uint32_t)en_level << 20 | (uint32_t)(en_trend + 1) << 22 |
                              (uint32_t)proj.preset << 24);
    for (i = 0; i < EN_NKEYKIND; i++)
        h = hash(h, (uint32_t)q16(en_glow[i]));
    for (i = 0; i < EN_NTONE; i++)
        h = hash(h, (uint32_t)q16(en_tone_glow[i]));
    for (i = 0; i < en_nbeats; i++)
        h = hash(h, (uint32_t)(uint8_t)en_bal[i]);
    h = hash(hash(h, (uint32_t)en_cur | (uint32_t)en_gongan << 8 | (uint32_t)en_tuning << 16), (uint32_t)proj.par[P_IRAMA]);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, cp->bg);
    draw_mandala();
    draw_corners();
    cv_blit(0, MAIN_Y);
}

/* the knob strip (X0X's, as AMB-1): a rule with the page's name and its dots, then a cell a knob */
static void draw_knobs(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8 | (uint32_t)proj.preset << 16), i;
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(VIEW_KNOB[ui.view][i]));
    h = hash(h, (uint32_t)now_touch);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, cp->bg);
    {
        char t[12];
        int k;
        int32_t xd = 237 - NVIEWS * 6, xt;
        caps(t, VIEW_NAME[ui.view], sizeof t);
        xt = xd - 5 - text_w(&FONT_XS, t);
        cv_rect(4, 5, xt - 9, 1, cp->line);
        cv_text(xt, -1, &FONT_XS, t, cp->acc);
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 6, 3, 4, 4, 2, k == ui.view ? cp->acc : cp->line);
    }
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v, hot = now_touch && ui.touched == (int)i;
        int32_t x = (int32_t)i * 60, cx = x + 30;
        char t[16];
        uint16_t bg = hot ? cp->panel : cp->bg, fill = hot ? cp->acc : cp->kenong;
        if (k == K_NONE)
            continue;
        if (i)
            cv_rect(x, 16, 1, 46, cp->line);
        if (hot)
            cv_round(x + 3, 11, 54, 57, 6, cp->panel);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        caps(t, KNOB_LABEL[ui.view][i], sizeof t);
        text_c(cx, 12, &FONT_XS, t, hot ? cp->text : cp->dim);
        if (hi - lo < 8) {                       /* a switch: its positions as pips */
            int nn = hi - lo + 1, j, pw = nn > 4 ? 5 : 9, gap = nn > 4 ? 2 : 3, w0 = nn * (pw + gap) - gap;
            for (j = 0; j < nn; j++)
                cv_round(cx - w0 / 2 + j * (pw + gap), 33, pw, 8, 2, j == v - lo ? fill : cp->line);
        } else {
            arc(cx, 37, 12, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, cp->line, fill, bg, lo < 0);
        }
        knob_text(k, t);
        text_c(cx, 50, &FONT_B, t, hot ? cp->acc : cp->text);
    }
    cv_blit(0, KNB_Y);
}

static void leds(void)
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS;
    uint32_t b = 0, k = 0;
    int i;
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (proj.hold)
        b |= 1u << B_REC;
    if (proj.leds && en_cur >= 0)                /* the white keys of the balungan's tone */
        for (i = 0; i < 16; i++)
            if ((3 + i) % EN_NTONE == en_cur % EN_NTONE)
                k |= 1u << WK[i];
    plat_glow(proj.leds == 1);
    plat_leds(b, k);
}

/* AUTOSAVE: the settings, once the knobs have rested 4 s, at most every 5 minutes, at a quiet moment (a flash erase
 * can glitch the audio): resting, or the silence between two pieces. Held in one piece (no silences), after 20
 * minutes it saves as a gong rings, which hides a glitch. SAVE saves at once. */
#define AUTOSAVE_REST 4000u
#define AUTOSAVE_GAP (5u * 60u * 1000u)
#define AUTOSAVE_HOLD (20u * 60u * 1000u)
static void autosave(void)
{
    uint32_t now = plat_ms();
    int quiet = !en_playing || en_state == S_GAP;
    if (!ui.dirty || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_REST)
        return;
    if (now - ui.saved_t < AUTOSAVE_GAP && ui.saved_t)
        return;
    if (!quiet && !(now - ui.dirty_t >= AUTOSAVE_HOLD && en_glow[K_GONG] > 0.9f))
        return;
    if (project_save() == 0) {
        ui.dirty = 0;
        ui.saved_t = now;
    }
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader on the computer (announce.c; fm1-reader): a page once it settles, with
 * its knobs; a knob's value as it turns; a help card as it comes up; a new piece, an irama change, the ending (all
 * queued); what a button did (say). WHERE is the whole screen in two: the page, its knobs and the header, then the
 * piece. Nothing is built while no reader listens. */
static struct {
    uint8_t view, help, started, level, state;
    int16_t val[4];
    uint32_t pieces;
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
    sp_start();
    sp_cat(VIEW_NAME[ui.view]);
    sp_cat(" page");
    for (i = 0; i < 4; i++) {
        sp_cat(i ? ", " : ": ");
        sp_knob(ui.view, i);
    }
    if (!all)
        return;
    sp_cat(". Preset ");
    sp_cat(preset_name(proj.preset));
    sp_cat(", ");
    sp_cat(EN_TUNING_LONG[proj.par[P_TUNING]]);
    if (ui.view != V_HOME) {
        sp_cat(", tempo ");
        sp_int((uint32_t)proj.par[P_TEMPO]);
    }
    sp_cat(en_playing ? ", playing" : ", resting");
    if (proj.hold)
        sp_cat(", hold");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static void sp_piece(void)                     /* "Ladrang, irama II, gongan 3, slowing" */
{
    const char *s = state_word();
    sp_start();
    sp_cat(EN_FORM_NAME[en_form % EN_NFORM]);
    sp_cat(", irama ");
    sp_cat(EN_IRAMA_NAME[1 + en_level % 3]);
    if (en_playing && en_state != S_BUKA && en_gongan) {
        sp_cat(", gongan ");
        sp_int(en_gongan);
    }
    if (s[0]) {
        sp_cat(", ");
        sp_cat(s);
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
static void sp_preset_text(void)               /* "PRESET Pelog Dawn, Pelog nem, tempo 66" */
{
    sp_start();
    sp_cat("PRESET ");
    sp_cat(preset_name(proj.preset));
    sp_cat(", ");
    sp_cat(EN_TUNING_LONG[proj.par[P_TUNING]]);
    sp_cat(", tempo ");
    sp_int((uint32_t)proj.par[P_TEMPO]);
}
static void sp_preset(void)
{
    if (!ann_on())
        return;
    sp_preset_text();
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
    sp.level = en_level;
    sp.state = en_state;
    sp.pieces = en_pieces;
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
            sp_piece();
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
        sp_start();
        sp_knob(ui.view, changed);
        ann_later(sp_buf, ANN_NOW, 120u, now);
    }
    if (en_pieces != sp.pieces && en_state == S_BUKA) {   /* the music's own events: queued */
        sp.pieces = en_pieces;
        sp_start();
        sp_cat("New piece, ");
        sp_cat(EN_FORM_NAME[en_form % EN_NFORM]);
        ann_say(sp_buf, 0, 0);
    }
    if (en_level != sp.level) {
        sp.level = en_level;
        sp_start();
        sp_cat("Irama ");
        sp_cat(EN_IRAMA_NAME[1 + en_level % 3]);
        ann_say(sp_buf, 0, 0);
    }
    if (en_state != sp.state) {
        sp.state = en_state;
        if (en_state == S_SUWUK)
            ann_say("Ending", 0, 0);
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card and message as a screen reader gets them (the "speakdump" command) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}, {"RESTING", 0},
                                         {"PLAYING", 0}, {"NEW PIECE", "AT THE NEXT GONG"}, {"NEW PIECE", 0},
                                         {"HOLD", "ON"}, {"HOLD", "OFF"}, {"SAVED", 0}, {"IRAMA", "I"},
                                         {"IRAMA", "II"}, {"IRAMA", "III"}};
    static const char *const WARN[][2] = {{"IRAMA", "AT ITS FASTEST"}, {"IRAMA", "AT ITS SLOWEST"},
                                          {"IRAMA", "NOT NOW"}, {"NOT SAVED", "FLASH ERROR"}};
    project_t keep = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty;
    int v, i, x, lo, hi;
    char t[48];
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
                sp_start();
                sp_knob(v, i);
                sp_print("KNOB", sp_buf);
            }
            proj = keep;
        }
    }
    for (x = 0; x < 4; x++) {
        ui.view = (uint8_t)x;
        ui.dirty = (uint8_t)(x & 1);
        proj.hold = (uint8_t)(x >> 1);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_piece();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
    ui.dirty = keep_dirty;
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
    for (x = 0; x < EN_NTUNING; x++) {
        msg_text("TUNING", EN_TUNING_LONG[x], t);
        sp_print("MSG", t);
    }
    for (x = 1; x < EN_NFORM; x++) {
        sp_start();
        sp_cat("New piece, ");
        sp_cat(EN_FORM_NAME[x]);
        sp_print("MSG", sp_buf);
    }
    for (x = 40; x <= 120; x += 40) {
        char o[8];
        itoa_u((uint32_t)x, o);
        msg_text("TEMPO", o, t);
        sp_print("MSG", t);
    }
    for (x = 0; x < NPRESETS; x++) {
        project_preset(x);
        sp_preset_text();
        sp_print("MSG", sp_buf);
        msg_text("RESET", preset_name(x), t);
        sp_print("MSG", t);
    }
    proj = keep;
    sp_print("MSG", "Ending");
    for (x = 0; x <= 4; x++)
        sp_print("WHERE", batt_words(x));
}
#endif

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    cp = &PAL[proj.preset % NPRESETS];
    lcd_fill(0, 0, 240, 240, cp->bg);
    if (!en_playing)
        en_play(1);                              /* it plays from the start */
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
