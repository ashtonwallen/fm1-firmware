/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; POND changes (C) 2026 ashtonwallen */
/* POND UI: the panel and the screen. Part of the unity build (after gfx.c, project.c and pond.c); the host
 * simulator includes it too. Built on AMB-1's (the knobs, help cards, the screen reader, the drawing helpers).
 *
 *   keys           food: dropped where the key sits along the pond; its note joins the scale for 90 s
 *   PLAY           the music on / resting (the pond lives on)      REC   FREEZE: the chord holds, the reverb rings
 *   ARP            a pinch of food all over        EDIT  the next chord (by the fish)
 *   OCT- / OCT+    the fish sing an octave down / up
 *   HOME ENV SEQ FX GLO   the pages: POND, FISH, JOURNAL, MIX, SETUP (GLO again: WORLD)
 *   SELECT pages   ALGORITHM Busy   PRESETS the fish to follow (its name and a ring in the pond; the Fish page)
 *   KNOB 1-4       the page's four values
 *   SAVE           saves now (the pond saves itself: on events and every 20 minutes, in a short hush);
 *                  held 3 s: a NEW POND (the old one is gone) */
#include "fastmath.h"

enum { V_POND, V_FISH, V_LOG, V_MIX, V_SETUP, V_WORLD, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Pond", "Fish", "Journal", "Mix", "Setup", "World"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_SEQ, B_FX, B_GLO, B_GLO};
enum { K_LEDS = 100, K_BUSY, K_FISH, K_DAYLEN, K_CAP, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {K_BUSY, P_TEMPO, P_REVERB, P_BTONE},
    {K_FISH, K_NONE, K_NONE, K_NONE},
    {K_NONE, K_NONE, K_NONE, K_NONE},
    {P_PAD, P_BELLS, P_BASS, P_PULSE},
    {P_ROOT, P_TUNE, P_MIDI, K_LEDS},
    {K_DAYLEN, K_CAP, P_ECHO, P_SIZE},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Busy", "Tempo", "Space", "Tone"},
    {"Fish", "", "", ""},
    {"", "", "", ""},
    {"Pad", "Fish", "Bass", "Frogs"},
    {"Root", "Tune", "MIDI out", "Lights"},
    {"Day", "Fish max", "Echo", "Size"},
};

/* the colours: AMB-1's paper and ink for the text, the pond's own for the water */
#define K_BG RGB(246, 238, 181)
#define K_PANEL RGB(255, 251, 224)
#define K_LINE RGB(208, 214, 150)
#define K_DIM RGB(112, 106, 58)
#define K_TEXT RGB(29, 27, 2)
#define K_ACC RGB(40, 120, 128)
#define K_PAD RGB(112, 142, 62)
#define K_LOW RGB(196, 84, 52)
#define K_FROST RGB(60, 132, 160)

static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];
    uint32_t down_t[NB];
    uint32_t turn_t;
    uint8_t help;
    uint8_t reset_done;
    uint8_t dirty;                     /* the settings changed since the last save */
    uint32_t act_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    uint8_t sv_state;                  /* the save's hush: 1 hushing (2 the empty battery's save) */
    uint8_t sv_force;                  /* a new pond: kept at the next quiet moment */
    uint32_t sv_t, sv_last;
} ui;

static uint8_t sp_said;
static struct {
    uint8_t playing;
    uint32_t play_t;
} sp_tp;
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

static void itoa_u(uint32_t v, char *b) { pd_num(b, v); }
static char *cat_s(char *b, const char *s) { return pd_cat(b, s); }

static void mark_dirty(void) { ui.dirty = 1; }
int ui_dirty(void) { return ui.dirty != 0; }

static uint32_t pool_abs(void)                 /* the scale as absolute pitch classes */
{
    uint32_t p = pd_pool(), r = (uint32_t)proj.par[P_ROOT];
    return ((p << r) | (p >> (12u - r))) & 0xFFFu;
}

/* ------------------------------------------------------------ values --- */
static const uint8_t DAY_MIN_UI[PD_NDAYLEN] = {6, 12, 24, 48, 96};
static int knob_get(int k)
{
    if (k < P_NPARAMS)
        return proj.par[k];
    if (k == K_LEDS)
        return proj.leds;
    if (k == K_BUSY)
        return proj.busy;
    if (k == K_FISH)
        return pd_selected() < 0 ? 0 : pd_selected();
    if (k == K_DAYLEN)
        return proj.daylen;
    if (k == K_CAP)
        return proj.cap;
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = en_param_info(k)->lo;
        *hi = en_param_info(k)->hi;
        return;
    }
    *lo = k == K_CAP ? 3 : 0;
    *hi = k == K_LEDS ? 2 : k == K_BUSY ? 100 : k == K_FISH ? PD_NFISH - 1 : k == K_DAYLEN ? PD_NDAYLEN - 1 :
          k == K_CAP ? PD_NFISH : 0;
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k < P_NPARAMS) {
        en_param_text(k, v, b);
    } else if (k == K_LEDS) {
        cat_s(b, v == 2 ? "Keys" : v ? "On" : "Off");
    } else if (k == K_FISH) {
        cat_s(b, pd_selected() < 0 ? "-" : fname(pd_selected()));
    } else if (k == K_DAYLEN) {
        cat_s(pd_num(b, DAY_MIN_UI[v]), " min");
    } else {
        itoa_u((uint32_t)v, b);
    }
}

static void knob_set(int k, int v)
{
    int lo, hi;
    if (k == K_NONE || k == K_FISH)
        return;
    knob_range(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (v == knob_get(k))
        return;
    if (k < P_NPARAMS)
        proj.par[k] = (int16_t)v;
    else if (k == K_LEDS)
        proj.leds = (uint8_t)v;
    else if (k == K_BUSY)
        proj.busy = (uint8_t)v;
    else if (k == K_DAYLEN)
        proj.daylen = (uint8_t)v;
    else if (k == K_CAP)
        proj.cap = (uint8_t)v;
    pd_sound();
    mark_dirty();
}

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
    if (k == K_FISH) {
        pd_select(e > 0 ? 1 : -1);
        return;
    }
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------ buttons --- */
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

static void follow(int dir)                     /* PRESETS: the next fish, named */
{
    int s = pd_select(dir);
    if (s >= 0) {
        char t[24];
        pd_cat(pd_cat(pd_cat(t, PD_SPECIES[W()->fish[s].species]), ", "), PD_TEMPER[W()->fish[s].temper]);
        say(fname(s), t);
    }
}

static void do_save(void)
{
    if (project_save() == 0) {
        ui.dirty = 0;
        pd.want_save = 0;
        pd.save_t = 0.0f;
        ui.sv_force = 0;
        ui.sv_last = plat_ms();
        say("SAVED", 0);
    } else {
        warn("NOT SAVED", "FLASH ERROR");
    }
}

static void button(int b)
{
    int v;
    if (b == B_GLO) {
        set_view(ui.view == V_SETUP ? V_WORLD : V_SETUP);
        return;
    }
    for (v = 0; v < NVIEWS; v++)
        if (VIEW_BTN[v] == b) {
            set_view(v);
            return;
        }
    switch (b) {
    case B_EDIT:
        chord_choose(-1, -1);
        say("NEXT CHORD", 0);
        break;
    case B_ARP:
        pd_sprinkle();
        say("FOOD", "ALL OVER");
        break;
    case B_PLAY:
        sp_tp.playing = !en_playing;
        sp_tp.play_t = plat_ms();
        if (en_playing) {
            en_play(0);
            say("RESTING", 0);
        } else {
            en_seed(plat_ms());
            en_play(1);
            say("SINGING", 0);
        }
        break;
    case B_REC:
        say("FREEZE", en_frozen ? "OFF" : "ON");
        en_freeze(!en_frozen);
        break;
    case B_OCTDN: case B_OCTUP:
        knob_set(P_BOCT, proj.par[P_BOCT] + (b == B_OCTUP ? 1 : -1));
        {
            char t[8];
            en_param_text(P_BOCT, proj.par[P_BOCT], t);
            say("OCTAVE", t);
        }
        break;
    case B_SAVE: do_save(); break;
    default: break;
    }
}

static void feed_key(int k)
{
    int pc = (5 + k) % 12;
    pd_feed(k, pc);
    say("FOOD", EN_NOTE_NAME[pc]);
}

static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0)) {
        uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)
            en_clock(st == 0xF8u ? EN_CLK_TICK : st == 0xFAu ? EN_CLK_START : st == 0xFBu ? EN_CLK_CONTINUE : EN_CLK_STOP);
        else if (st == 0x90u && v) {
            int k = (int)n - 53;                 /* F3 is the first key */
            while (k < 0)
                k += 12;
            while (k >= NKEYS)
                k -= 12;
            feed_key(k);
        }
    }
}

#define HELP_HOLD 900u
#define NEWPOND_HOLD 3000u
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
            feed_key((int)i);
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {
        int v = (int)ui.view + (e > 0 ? 1 : -1);
        if (v >= 0 && v < NVIEWS)
            set_view(v);
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        knob_set(K_BUSY, proj.busy + (e > 0 ? 5 : -5));
        {
            char t[8];
            itoa_u(proj.busy, t);
            say("BUSY", t);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        follow(e > 0 ? 1 : -1);
        ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && plat_ms() - ui.down_t[B_SAVE] > NEWPOND_HOLD) {
        ui.reset_done = 1;                     /* held: a new pond */
        ui.btn_used |= 1u << B_SAVE;
        ui.help = 0;
        pd_new_world(plat_ms() * 2654435761u ^ ui.frame);
        pd_sound();
        say("NEW POND", 0);
        pd.want_save = 1;
        ui.sv_force = 1;                         /* kept at the next quiet moment */
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

/* ------------------------------------------------------------- drawing --- */
#define HDR_H 24
#define POND_Y 24
#define BAND_H 88                               /* the pond in two bands (the canvas holds 240 x 144) */
#define STRIP_Y 200
#define STRIP_H 40

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

/* a disc, anti-aliased, of strength a (0..1) */
static void disc_a(float cx, float cy, float r, uint16_t c, float al)
{
    int32_t x0 = (int32_t)fm_floorf(cx - r - 1.0f), x1 = (int32_t)(cx + r + 1.0f);
    int32_t y0 = (int32_t)fm_floorf(cy - r - 1.0f), y1 = (int32_t)(cy + r + 1.0f), x, y;
    if (y1 + cv_oy < cv_y0 || y0 + cv_oy >= cv_y1)
        return;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy);
            px_blend(x, y, c, (uint32_t)(fm_clampf(r - d + 0.5f, 0.0f, 1.0f) * al * 16.0f + 0.5f));
        }
}
static void disc(float cx, float cy, float r, uint16_t c) { disc_a(cx, cy, r, c, 1.0f); }

static void ring(float cx, float cy, float r, uint16_t c, float al)   /* a ripple */
{
    int n = (int)(r * 6.0f) + 8, i;
    float step = FM_TWO_PI / (float)n;
    if (cy + r + cv_oy < (float)cv_y0 || cy - r + cv_oy >= (float)cv_y1)
        return;
    for (i = 0; i < n; i++) {
        float a = (float)i * step;
        px_blend((int32_t)(cx + fm_cosf(a) * r), (int32_t)(cy + fm_sinf(a) * r), c,
                 (uint32_t)(al * 16.0f + 0.5f));
    }
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

static void caps(char *d, const char *s, int n)
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}

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
    cv_round(x, 7, 18, 10, 2, c);
    cv_rect(x + 1, 8, 16, 8, K_BG);
    cv_rect(x + 18, 10, 2, 4, c);
    if (b == 4) {
        for (i = 0; i < 7; i++)
            cv_rect(x + BOLT[i][0], 8 + i, BOLT[i][1], 1, K_TEXT);
        return;
    }
    for (i = 0; i < b && i < 3; i++)
        cv_rect(x + 2 + i * 5, 9, 4, 6, b <= 1 ? K_LOW : K_PAD);
}
static const char *batt_words(int b)
{
    static const char *const W[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                     "On USB power"};
    return b >= 0 && b <= 4 ? W[b] : 0;
}

/* the header: the day in an ink tag, the season and the time of day; at the right, the battery and a light
 * (singing) or a play triangle (resting). Messages take its place for a moment. */
static void draw_header(void)
{
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(2166136261u, W()->day | (uint32_t)pd_tod() << 16 | (uint32_t)pd_season() << 20 |
                                            (uint32_t)msg << 24 | (uint32_t)en_playing << 25 | (uint32_t)en_frozen << 26),
                      (uint32_t)(batt() + 1) | (uint32_t)(W()->minute / 10u) << 8);
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1]), ui.msg_until);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(8, 4, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 4, &FONT_B, ui.msg[1], K_ACC);
    } else {
        char t[24], c[8];
        int32_t x = 232, w, hh = W()->minute / 60, mm = W()->minute % 60;
        if (batt() >= 0) {
            x -= BATT_W;
            draw_batt(x, batt());
            x -= 6;
        }
        x -= 12;
        if (en_frozen)
            cv_round(x, 7, 10, 10, 2, K_FROST);
        else if (en_playing)
            cv_round(x, 7, 10, 10, 5, K_ACC);
        else {
            int32_t j;
            for (j = 0; j < 5; j++)
                cv_rect(x + 2 + j, 7 + j, 1, 11 - 2 * j, K_DIM);
        }
        c[0] = (char)('0' + hh / 10);              /* the in-world time, to ten minutes */
        c[1] = (char)('0' + hh % 10);
        c[2] = ':';
        c[3] = (char)('0' + mm / 10);
        c[4] = '0';
        c[5] = 0;
        x -= text_w(&FONT_S, c) + 8;
        cv_text(x, 4, &FONT_S, c, K_DIM);
        pd_num(pd_cat(t, "DAY "), W()->day);
        w = text_w(&FONT_B, t) + 12;
        cv_round(4, 3, w, 18, 6, K_TEXT);
        cv_text(10, 4, &FONT_B, t, K_BG);
        pd_cat(pd_cat(pd_cat(t, PD_SEASON[pd_season()]), " "), PD_TOD[pd_tod()]);
        if (w + 10 + text_w(&FONT_S, t) < x - 4)
            cv_text(w + 10, 4, &FONT_S, t, K_TEXT);
    }
    cv_blit(0, 0);
}

/* ---- the pond's colours, by season and light */
static uint16_t water_col(int depth)            /* 0 the shallows .. 2 the deep */
{
    static const uint16_t DAY[4][3] = {
        {RGB(104, 172, 160), RGB(78, 148, 140), RGB(56, 122, 120)},    /* spring: clear green-teal */
        {RGB(86, 160, 140), RGB(60, 134, 118), RGB(40, 106, 98)},      /* summer: deeper, greener */
        {RGB(118, 146, 120), RGB(92, 122, 100), RGB(70, 98, 84)},      /* autumn: brown-green */
        {RGB(150, 182, 196), RGB(120, 156, 176), RGB(96, 130, 156)},   /* winter: pale, icy */
    };
    static const uint16_t NIGHT[3] = {RGB(30, 50, 70), RGB(22, 38, 58), RGB(14, 26, 44)};
    return mix(NIGHT[depth], DAY[pd_season()][depth], q16(pd_light()));
}
static uint16_t bank_col(int k)
{
    static const uint16_t DAY[4][2] = {
        {RGB(140, 186, 96), RGB(112, 160, 76)}, {RGB(98, 150, 70), RGB(76, 126, 54)},
        {RGB(186, 150, 82), RGB(158, 118, 62)}, {RGB(226, 230, 232), RGB(196, 204, 210)}};
    static const uint16_t NIGHT[2] = {RGB(26, 40, 34), RGB(20, 32, 28)};
    return mix(NIGHT[k], DAY[pd_season()][k], q16(pd_light()));
}

/* the squircle's half width at row y (scale s: 1 the bank) */
static float half_w(float y, float s)
{
    float ny = (y - 88.0f) / (80.0f * s), t;
    ny *= ny;
    t = 1.0f - ny * ny;
    if (t <= 0.0f)
        return -1.0f;
    return 112.0f * s * fm_sqrtf(fm_sqrtf(t));
}

static void draw_water(void)
{
    int32_t y;
    uint16_t w0 = water_col(0), w1 = water_col(1), w2 = water_col(2), b0 = bank_col(0), b1 = bank_col(1);
    for (y = 0; y < PD_H; y++) {
        float h;
        if (y + cv_oy < cv_y0 || y + cv_oy >= cv_y1)
            continue;
        cv_rect(0, y, 240, 1, ((y / 3 + (y * 7) % 5) & 3) ? b0 : b1);
        if ((h = half_w((float)y + 0.5f, 1.0f)) > 0.0f) {
            int32_t a = (int32_t)(120.0f - h), b = (int32_t)(120.0f + h);
            cv_rect(a - 2, y, b - a + 4, 1, mix(b1, w0, 6));       /* the wet edge */
            cv_rect(a, y, b - a, 1, w0);
        }
        if ((h = half_w((float)y + 0.5f, 0.78f)) > 0.0f)
            cv_rect((int32_t)(120.0f - h), y, (int32_t)(2.0f * h), 1, w1);
        if ((h = half_w((float)y + 0.5f, 0.48f)) > 0.0f)
            cv_rect((int32_t)(120.0f - h), y, (int32_t)(2.0f * h), 1, w2);
    }
    {   /* reeds and stones on the banks, from the pond's seed */
        uint32_t s = W()->seed | 1u;
        int i;
        uint16_t reed = mix(bank_col(1), RGB(40, 70, 30), 8), stone = mix(bank_col(0), RGB(130, 126, 118), 10);
        for (i = 0; i < 14; i++) {
            int32_t x, yy;
            s = s * 1664525u + 1013904223u;
            x = (int32_t)(s >> 24) % 240;
            yy = (int32_t)((s >> 12) & 0xFFu) % PD_H;
            if (in_water((float)x, (float)yy))
                continue;
            if (i & 1) {
                cv_line(x, yy, x - 2, yy - 7, reed);
                cv_line(x + 2, yy, x + 3, yy - 8, reed);
                cv_line(x + 1, yy, x + 1, yy - 9, reed);
            } else {
                disc((float)x, (float)yy, 2.5f, stone);
            }
        }
    }
}

static void draw_lily(const lily_t *l, int sea)
{
    static const uint16_t PADC[4] = {RGB(96, 170, 72), RGB(70, 150, 60), RGB(170, 150, 60), 0};
    float cx = (float)l->x, cy = (float)l->y, r = (float)l->r, a0 = (float)(l->x * 7 % 20) * 0.31f;
    uint16_t c = mix(RGB(20, 40, 30), PADC[sea], 6 + q16(pd_light()) * 10 / 16), wc = water_col(1);
    int i;
    if (sea == SEA_WINTER || !l->r)
        return;
    disc(cx + 1.5f, cy + 2.0f, r, mix(wc, RGB(0, 0, 0), 4));     /* its shadow */
    disc(cx, cy, r, c);
    for (i = 0; i <= 6; i++) {                   /* the notch */
        float a = a0 + (float)i * 0.08f;
        cv_line((int32_t)cx, (int32_t)cy, (int32_t)(cx + fm_cosf(a) * (r + 1.0f)), (int32_t)(cy + fm_sinf(a) * (r + 1.0f)),
                mix(wc, c, 3));
    }
    cv_line((int32_t)cx, (int32_t)cy, (int32_t)(cx - fm_cosf(a0) * r * 0.7f), (int32_t)(cy - fm_sinf(a0) * r * 0.7f),
            mix(c, RGB(255, 255, 255), 3));
    if (l->flower && sea <= SEA_SUMMER && pd_light() > 0.3f) {
        disc(cx + r * 0.3f, cy - r * 0.2f, 3.2f, RGB(244, 160, 196));
        disc(cx + r * 0.3f, cy - r * 0.2f, 1.3f, RGB(255, 236, 140));
    }
}

/* a fish's colours: body, patch (koi), fin */
static void fish_cols(int i, uint16_t *body, uint16_t *patch, uint16_t *fin)
{
    switch (W()->fish[i].species) {
    case SP_KOI:
        *body = RGB(246, 240, 230);
        *patch = (W()->fish[i].name & 1) ? RGB(232, 92, 40) : RGB(214, 48, 40);
        *fin = RGB(250, 210, 190);
        break;
    case SP_GOLD:
        *body = RGB(246, 150, 40);
        *patch = RGB(252, 196, 70);
        *fin = RGB(252, 200, 120);
        break;
    case SP_MINNOW:
        *body = RGB(186, 200, 210);
        *patch = RGB(110, 130, 150);
        *fin = RGB(200, 214, 222);
        break;
    default:
        *body = RGB(150, 120, 60);
        *patch = RGB(110, 86, 44);
        *fin = RGB(170, 140, 80);
        break;
    }
}

/* a fish from above: a chain of discs from the head back, bending with its tail's wag, then the tail fin */
static void draw_fish_at(int i, float x, float y, float hx, float hy, float len, float tail, float fade, float glow,
                         float scale)
{
    uint16_t body, patch, fin, wc = water_col(1);
    float px = -hy, py = hx, wid = len * 0.2f + 0.6f, t, light = pd_light();
    int n = (int)(len / 1.4f) + 3, k, sp = W()->fish[i].species;
    uint32_t pat = (uint32_t)W()->fish[i].name * 2654435761u;
    fish_cols(i, &body, &patch, &fin);
    if (light < 1.0f) {                          /* by night, darker */
        int d = (int)((1.0f - light) * 7.0f);
        body = mix(body, wc, d);
        patch = mix(patch, wc, d);
        fin = mix(fin, wc, d);
    }
    if (glow > 0.0f) {
        body = mix(body, RGB(255, 255, 240), q16(glow * 0.5f));
        fin = mix(fin, RGB(255, 255, 240), q16(glow * 0.5f));
    }
    if (fade < 1.0f) {
        int d = 16 - q16(fade);
        body = mix(body, wc, d);
        patch = mix(patch, wc, d);
        fin = mix(fin, wc, d);
    }
    if (scale <= 1.0f)                           /* its shadow on the bottom */
        for (k = 0; k < n; k += 2) {
            t = (float)k / (float)(n - 1);
            disc_a(x - hx * t * len + 2.0f, y - hy * t * len + 3.0f, wid * 0.8f, RGB(10, 30, 30), 0.25f * fade);
        }
    for (k = n - 1; k >= 0; k--) {
        float w, off, cx, cy, r;
        t = (float)k / (float)(n - 1);
        off = fm_sinf(tail * FM_TWO_PI - t * 3.0f) * t * t * len * 0.16f;
        cx = x - hx * t * len + px * off;
        cy = y - hy * t * len + py * off;
        w = t < 0.25f ? 0.7f + 1.2f * t : 1.0f - 0.8f * (t - 0.25f);
        r = wid * w;
        if (k == n - 1) {                        /* the tail fin: two lobes */
            float s = fm_sinf(tail * FM_TWO_PI - 3.3f) * 0.5f;
            float bx = -hx, by = -hy;
            float l1x = bx * 0.8f + px * (0.6f + s), l1y = by * 0.8f + py * (0.6f + s);
            float l2x = bx * 0.8f - px * (0.6f - s), l2y = by * 0.8f - py * (0.6f - s);
            float fl = wid * 1.6f + 1.0f, j;
            for (j = 0.0f; j <= fl; j += 0.8f) {
                disc(cx + l1x * j, cy + l1y * j, fm_maxf(0.5f, wid * 0.55f * (1.0f - j / fl) + 0.4f), fin);
                disc(cx + l2x * j, cy + l2y * j, fm_maxf(0.5f, wid * 0.55f * (1.0f - j / fl) + 0.4f), fin);
            }
        }
        if (k == n / 3 && len > 7.0f) {          /* the side fins */
            disc(cx + px * (r + 1.0f), cy + py * (r + 1.0f), r * 0.45f, fin);
            disc(cx - px * (r + 1.0f), cy - py * (r + 1.0f), r * 0.45f, fin);
        }
        disc(cx, cy, r, sp == SP_KOI && (pat >> (k % 16) & 1u) && k > 0 ? patch : body);
        if (sp != SP_KOI && k > 0 && k < n - 1)  /* a darker back */
            disc(cx, cy, r * 0.4f, patch);
    }
    if (len >= 7.0f) {                           /* eyes */
        float ex = x - hx * len * 0.08f, ey = y - hy * len * 0.08f, eo = wid * 0.75f;
        disc(ex + px * eo, ey + py * eo, 0.6f * scale + 0.2f, RGB(20, 20, 24));
        disc(ex - px * eo, ey - py * eo, 0.6f * scale + 0.2f, RGB(20, 20, 24));
    }
}

static void draw_heron(void)
{
    float t = pd.ht, k = pd.heron == 1 ? 1.0f - t / 1.2f : pd.heron == 4 ? t / 1.5f : 0.0f;
    float x = pd.hx + pd.hdx * k * 70.0f, y = pd.hy + pd.hdy * k * 70.0f, nx = -pd.hdx, ny = -pd.hdy;
    float bob = (pd.heron == 3 || pd.heron == 2) ? fm_sinf(t * 1.3f) * 2.0f : 0.0f, j;
    uint16_t grey = mix(RGB(70, 80, 92), RGB(196, 202, 212), q16(pd_light())), dark = RGB(56, 60, 70);
    uint16_t wing = mix(grey, dark, 7);
    float hx2, hy2;
    disc_a(x + 8.0f, y + 12.0f, 13.0f, RGB(0, 0, 0), 0.3f);   /* its shadow */
    if (k > 0.0f) {                              /* flying: wings out */
        for (j = -26.0f; j <= 26.0f; j += 1.2f)
            disc(x + ny * j - nx * fm_fabsf(j) * 0.2f, y - nx * j - ny * fm_fabsf(j) * 0.2f, 4.0f - fm_fabsf(j) * 0.11f,
                 fm_fabsf(j) > 17.0f ? dark : wing);
    }
    for (j = -10.0f; j <= 4.0f; j += 1.0f)       /* the body, a long oval; folded wings at its sides */
        disc(x + nx * j, y + ny * j, 6.5f - fm_fabsf(j + 3.0f) * 0.35f, j < -4.0f ? wing : grey);
    disc(x - nx * 12.0f, y - ny * 12.0f, 2.5f, dark);   /* the tail */
    for (j = 4.0f; j < 18.0f; j += 1.0f)         /* the neck, swaying, out over the water */
        disc(x + nx * j + ny * bob * (j - 4.0f) * 0.12f, y + ny * j - nx * bob * (j - 4.0f) * 0.12f, 2.0f, grey);
    hx2 = x + nx * 19.0f + ny * bob * 1.7f;
    hy2 = y + ny * 19.0f - nx * bob * 1.7f;
    disc(hx2, hy2, 3.6f, RGB(236, 236, 240));    /* the head, its black crest */
    disc(hx2 - nx * 2.0f + ny * 1.2f, hy2 - ny * 2.0f - nx * 1.2f, 1.5f, RGB(20, 20, 24));
    for (j = 3.0f; j < 12.0f; j += 0.7f)         /* the beak */
        disc(hx2 + nx * j, hy2 + ny * j, 1.3f - j * 0.06f, RGB(236, 186, 50));
}

static void draw_frogs(void)
{
    int i, n = 0;
    uint16_t green = RGB(70, 120, 60), pale = RGB(200, 214, 150);
    if (pd_season() == SEA_WINTER || pd_light() > 0.3f)
        return;
    for (i = 0; i < PD_NLILY && n < 3; i++) {
        const lily_t *l = &W()->lily[i];
        float x, y;
        if (!l->r || (i % 2))
            continue;
        x = (float)l->x - 1.0f;
        y = (float)l->y + 1.0f;
        disc(x, y + 1.0f, 3.0f + pd.frog_puff[n] * 2.0f, pale);
        disc(x, y, 3.4f, green);
        disc(x - 2.2f, y - 2.6f, 1.4f, green);
        disc(x + 2.2f, y - 2.6f, 1.4f, green);
        disc(x - 2.2f, y - 2.8f, 0.6f, RGB(240, 230, 120));
        disc(x + 2.2f, y - 2.8f, 0.6f, RGB(240, 230, 120));
        n++;
    }
}

static void draw_caption(void)
{
    int sel = pd.sel >= 0 && plat_ms() < pd.sel_until && W()->fish[pd.sel].alive == 1;
    const char *t = 0;
    char buf[40];
    int f = -1;
    if (plat_ms() < pd.cap_until) {
        t = pd.cap;
        f = pd.cap_fish;
    } else if (sel) {
        f = pd.sel;
        pd_cat(pd_cat(pd_cat(buf, fname(f)), "  "), PD_SPECIES[W()->fish[f].species]);
        t = buf;
    }
    if (sel) {                                   /* the fish followed: a ring and its name */
        fishrt_t *r = &pd.f[pd.sel];
        float rr = flen(pd.sel) * 0.7f + 4.0f;
        ring(r->x - r->hx * flen(pd.sel) * 0.4f, r->y - r->hy * flen(pd.sel) * 0.4f, rr, RGB(255, 250, 210), 0.9f);
    }
    if (t && t[0]) {
        int32_t w = text_w(&FONT_XS, t) + (f >= 0 ? 22 : 12), x = 6, y = PD_H - 22;
        if (w > 228)
            w = 228;
        cv_round(x, y, w, 18, 6, RGB(24, 34, 36));
        if (f >= 0 && W()->fish[f].alive) {
            uint16_t b, p, fi;
            fish_cols(f, &b, &p, &fi);
            disc((float)x + 9.0f, (float)y + 9.0f, 4.0f, W()->fish[f].species == SP_KOI ? p : b);
            x += 12;
        }
        cv_text(x + 5, y + 1, &FONT_XS, t, RGB(240, 236, 210));
    }
}

static void draw_pond_band(int band)
{
    world_t *w = W();
    int i, sea = pd_season();
    cv_begin(240, BAND_H, K_BG);
    cv_oy = -band * BAND_H;
    draw_water();
    for (i = 0; i < NFOOD; i++)                  /* food, sinking */
        if (pd.food[i].life > 0.0f) {
            float al = fm_minf(1.0f, pd.food[i].life * 3.0f);
            disc_a(pd.food[i].x, pd.food[i].y, pd.food[i].pc < 0 ? 1.6f : 2.6f, RGB(60, 40, 20), al * 0.6f);
            disc_a(pd.food[i].x, pd.food[i].y, pd.food[i].pc < 0 ? 1.0f : 1.8f, pd.food[i].pc < 0 ? RGB(90, 80, 60) :
                   RGB(250, 206, 120), al);
        }
    for (i = 0; i < PD_NFISH; i++)
        if (w->fish[i].alive == 1) {
            fishrt_t *r = &pd.f[i];
            draw_fish_at(i, r->x, r->y, r->hx, r->hy, flen(i), r->tail, r->fade, r->glow, 1.0f);
        }
    for (i = 0; i < PD_NLILY; i++)               /* the pads float over the fish */
        draw_lily(&w->lily[i], sea);
    draw_frogs();
    if (sea == SEA_AUTUMN) {                     /* leaves on the water */
        uint32_t s = w->seed ^ w->day;
        int k;
        for (k = 0; k < 6; k++) {
            float x, y;
            s = s * 1664525u + 1013904223u;
            x = 30.0f + (float)(s >> 24) * 0.7f + fm_sinf((float)(plat_ms() % 60000u) * 0.0001f + (float)k) * 4.0f;
            y = 30.0f + (float)((s >> 16) & 0x7Fu);
            if (in_water(x, y))
                disc(x, y, 2.0f, k & 1 ? RGB(210, 120, 40) : RGB(190, 160, 50));
        }
    }
    for (i = 0; i < NRIP; i++)
        if (pd.rip[i].a > 0.0f) {
            uint16_t c = pd.rip[i].kind == 2 ? RGB(255, 248, 210) : RGB(236, 246, 246);
            ring(pd.rip[i].x, pd.rip[i].y, pd.rip[i].r, c, pd.rip[i].a * (pd.rip[i].kind == 2 ? 0.9f : 0.55f));
        }
    if (pd.heron)
        draw_heron();
    draw_caption();
    cv_blit(0, POND_Y + band * BAND_H);
}

/* ---- the Fish page: the chosen fish's card */
static void row(int32_t y, const char *k, const char *v)
{
    cv_text(110, y, &FONT_XS, k, K_DIM);
    cv_text(160, y, &FONT_S, v, K_TEXT);
}
static void draw_card_band(int band)
{
    int s = pd_selected();
    char t[40];
    cv_begin(240, BAND_H, K_BG);
    cv_oy = -band * BAND_H;
    if (s < 0) {
        text_c(120, 70, &FONT_B, "No fish. A newcomer at dawn", K_DIM);
    } else {
        fish_t *f = &W()->fish[s];
        int fr = pd_best(s, 1), rv = pd_best(s, -1), n, k;
        cv_round(6, 4, 228, 166, 6, K_PANEL);
        cv_round(12, 34, 90, 70, 6, water_col(1));
        draw_fish_at(s, 82.0f, 69.0f, 1.0f, 0.0f, 22.0f + (float)f->size * 2.0f, pd.f[s].tail, 1.0f, pd.f[s].glow, 2.0f);
        cv_text(14, 6, &FONT_M, fname(s), K_TEXT);
        pd_cat(pd_cat(pd_cat(t, PD_SPECIES[f->species]), ", "), PD_TEMPER[f->temper]);
        cv_text(14 + text_w(&FONT_M, fname(s)) + 10, 13, &FONT_S, t, K_ACC);
        pd_cat(pd_num(t, f->age / 24u), f->age / 24u == 1 ? " day" : " days");
        row(36, "AGE", t);
        pd_num(t, f->size);
        row(54, "SIZE", t);
        row(72, "NOTE", EN_NOTE_NAME[fish_pc(s)]);
        row(90, "FRIEND", fr >= 0 ? fname(fr) : "-");
        row(108, "RIVAL", rv >= 0 ? fname(rv) : "-");
        cv_text(14, 110, &FONT_XS, "FED", K_DIM);           /* hunger as how fed */
        cv_round(44, 114, 56, 7, 3, K_LINE);
        k = (100 - f->hunger) * 56 / 100;
        if (k > 0)
            cv_round(44, 114, k < 7 ? 7 : k, 7, 3, f->hunger > 70 ? K_LOW : K_PAD);
        if (f->ma && f->pa) {
            pd_cat(pd_cat(pd_cat(pd_cat(t, "Fry of "), PD_NAMES[(f->ma - 1) % PD_NNAMES]), " & "), PD_NAMES[(f->pa - 1) % PD_NNAMES]);
        } else {
            pd_cat(t, f->gen ? "Fry" : "Came to the pond");
        }
        cv_text(14, 130, &FONT_XS, t, K_DIM);
        n = pd_alive();
        {
            int k2, idx = 0;
            for (k2 = 0; k2 <= s; k2++)
                idx += W()->fish[k2].alive == 1;
            pd_num(pd_cat(pd_num(t, (uint32_t)idx), " of "), (uint32_t)n);
        }
        cv_text(14, 148, &FONT_XS, t, K_ACC);
        cv_text(232 - text_w(&FONT_XS, "PRESETS: next"), 148, &FONT_XS, "PRESETS: next", K_DIM);
    }
    cv_blit(0, POND_Y + band * BAND_H);
}

/* ---- the Journal page */
static void draw_log_band(int band)
{
    world_t *w = W();
    int i, n = w->nlog;
    char t[48];
    cv_begin(240, BAND_H, K_BG);
    cv_oy = -band * BAND_H;
    cv_round(6, 4, 228, 166, 6, K_PANEL);
    cv_text(14, 6, &FONT_B, "JOURNAL", K_ACC);
    for (i = 0; i < n; i++) {
        const logline_t *l = &w->log[n - 1 - i];
        int32_t y = 26 + i * 18;
        pd_num(pd_cat(t, "D"), l->day);
        cv_text(14, y, &FONT_XS, t, K_DIM);
        cv_text(46, y, &FONT_XS, l->t, i ? K_TEXT : K_ACC);
    }
    cv_blit(0, POND_Y + band * BAND_H);
}

static void draw_help(void)
{
    /* drawn in the main area by draw_main */
}

#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("THE POND"); L("BUSY, TEMPO, SPACE, TONE"); L("KEYS: FOOD AND ITS NOTE"); break;
    case B_ENV: L("THE FISH"); L("NAME, AGE, FRIEND, RIVAL"); L("PRESETS: THE NEXT FISH"); break;
    case B_SEQ: L("THE JOURNAL"); L("WHAT HAPPENED, NEWEST FIRST"); break;
    case B_FX: L("THE MIX"); L("PAD, FISH, BASS, FROGS"); break;
    case B_GLO: L("SETUP"); L("ROOT, TUNE, MIDI OUT, LIGHTS"); L("AGAIN: THE WORLD"); break;
    case B_EDIT: L("THE NEXT CHORD,"); L("CHOSEN BY THE FISH"); break;
    case B_ARP: L("A PINCH OF FOOD"); L("ALL OVER THE POND"); break;
    case B_REC: L("FREEZE: THE CHORD HOLDS"); L(" AND THE REVERB RINGS ON"); L("AGAIN: THAW"); break;
    case B_OCTDN: L("THE FISH SING LOWER"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE FISH SING HIGHER"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES THE POND NOW"); L("HOLD 3 S: A NEW POND"); L("(THE FISH ARE GONE)"); break;
    default: break;
    }
#undef L
    return n;
}

static void draw_main(void)
{
    uint32_t h;
    if (ui.frame % 4u)                           /* 15 frames a second */
        return;
    if (ui.help) {
        const char *l[HELP_LINES];
        int n = help_lines(ui.help - 1, l), k, band;
        h = hash(0x48454C50u, ui.help);
        if (h == ui.sig[1])
            return;
        ui.sig[1] = h;
        for (band = 0; band < 2; band++) {
            cv_begin(240, BAND_H, K_BG);
            cv_oy = -band * BAND_H;
            cv_round(16, 20, 208, 130, 6, K_PANEL);
            cv_round(16, 20, 208, 26, 6, K_TEXT);
            cv_rect(16, 40, 208, 6, K_TEXT);
            text_c(120, 24, &FONT_B, BTN_LABEL[ui.help - 1], K_BG);
            for (k = 0; k < n; k++)
                text_c(120, 62 + k * 22, &FONT_S, l[k] + (l[k][0] == ' '), K_TEXT);
            cv_blit(0, POND_Y + band * BAND_H);
        }
        return;
    }
    ui.sig[1] = 0;
    if (ui.view == V_FISH) {
        draw_card_band(0);
        draw_card_band(1);
    } else if (ui.view == V_LOG) {
        draw_log_band(0);
        draw_log_band(1);
    } else {
        draw_pond_band(0);
        draw_pond_band(1);
    }
}

/* the strip: four cells, the knob's name, its value and a thin bar; the Fish and Journal pages show their own */
static void draw_strip(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8 | (uint32_t)(plat_ms() < ui.touch_until) << 16), i;
    char t[48];
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(VIEW_KNOB[ui.view][i]));
    if (ui.view == V_LOG)
        h = hash(h, W()->born | (uint32_t)W()->died << 8 | (uint32_t)W()->herons << 16 | (uint32_t)pd_alive() << 24);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, STRIP_H, K_BG);
    {   /* the page and its dots, on the rule */
        int32_t xd = 237 - NVIEWS * 6, xt;
        int k;
        caps(t, VIEW_NAME[ui.view], 12);
        xt = xd - 5 - text_w(&FONT_T, t);
        cv_rect(4, 4, xt - 9, 1, K_LINE);
        cv_text(xt, -2, &FONT_T, t, K_ACC);
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 6, 2, 4, 4, 2, k == ui.view ? K_ACC : K_LINE);
    }
    if (ui.view == V_LOG) {
        char *p = pd_num(pd_cat(t, "Fish "), (uint32_t)pd_alive());
        p = pd_num(pd_cat(p, "   Born "), W()->born);
        p = pd_num(pd_cat(p, "   Gone "), W()->died);
        pd_num(pd_cat(p, "   Herons "), W()->herons);
        text_c(120, 16, &FONT_S, t, K_TEXT);
        cv_blit(0, STRIP_Y);
        return;
    }
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v, hot = plat_ms() < ui.touch_until && ui.touched == (int)i;
        int32_t x = (int32_t)i * 60, cx = x + 30, bw;
        if (k == K_NONE)
            continue;
        if (i)
            cv_rect(x, 12, 1, 26, K_LINE);
        if (hot)
            cv_round(x + 3, 9, 54, 31, 5, K_PANEL);
        caps(t, KNOB_LABEL[ui.view][i], 16);
        text_c(cx, 7, &FONT_T, t, hot ? K_TEXT : K_DIM);
        knob_text(k, t);
        text_c(cx, 17, &FONT_B, t, hot ? K_ACC : K_TEXT);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        if (k != K_FISH && hi > lo) {
            bw = (v - lo) * 40 / (hi - lo);
            cv_round(cx - 20, 35, 40, 3, 1, K_LINE);
            if (bw > 0)
                cv_round(cx - 20, 35, bw < 3 ? 3 : bw, 3, 1, hot ? K_ACC : K_PAD);
        }
    }
    if (ui.view == V_FISH)
        cv_text(70, 17, &FONT_XS, "K1 or PRESETS: choose a fish", K_DIM);
    cv_blit(0, STRIP_Y);
}

static void leds(void)
{
    uint32_t b = 0, k = 0, i, ap = pool_abs();
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (en_frozen)
        b |= 1u << B_REC;
    if (proj.leds)
        for (i = 0; i < NKEYS; i++)
            if (ap >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    plat_glow(proj.leds == 1);
    plat_leds(b, k);
}

/* KEEPING, and the flash's wear: every save erases one of the same two 4 KiB sectors (APP_STORE_MAP's A/B pair),
 * rated about 100k erases, so the pond saves itself rarely. keep_due is the whole policy:
 *   the world (events, the 20-minute mark: pd.want_save) at most once an hour after the last save; nothing changed,
 *     no save. Up to an hour of the pond is lost at a power-off (the FM-1 gives no warning);
 *   a setting (ui.dirty) after the knobs rest a few seconds, at most once every 5 minutes;
 *   on the battery, once it reads empty (0): one save of whatever changed, then none of these until it reads above 0;
 *   SAVE saves at once (do_save); SAVE held (a new pond) is kept at the next quiet moment (ui.sv_force).
 * Playing or resting the same. Each save resets the clock: it keeps the settings and the world alike. */
#define KEEP_WORLD_MS (60u * 60000u)
#define KEEP_SETTINGS_MS (5u * 60000u)
static uint8_t keep_low;                        /* the empty battery's save done; again once it reads above 0 */
static int keep_due(uint32_t now)
{
    uint32_t since = now - ui.sv_last;
    if (ui.sv_force)
        return 1;
    if (!pd.want_save && !ui.dirty)
        return 0;
    if (batt() == 0 && !keep_low && since >= KEEP_SETTINGS_MS)
        return 2;
    return since >= KEEP_WORLD_MS || (ui.dirty && since >= KEEP_SETTINGS_MS);
}
static void keep_saved(uint32_t now, int due)
{
    if (project_save() == 0) {
        ui.dirty = 0;
        pd.want_save = 0;
        ui.sv_force = 0;
    }
    ui.sv_last = now;
    if (due == 2)
        keep_low = 1;
}
/* A flash erase stalls the audio for a moment, so the music hushes first (0.4 s), then comes back over a second.
 * While resting, a few seconds after the last touch, no hush needed. */
static void autosave(void)
{
    uint32_t now = plat_ms();
    int due;
    if (batt() != 0)
        keep_low = 0;                    /* (every pass: the battery read above 0 again) */
    if (ui.sv_state) {
        if (now - ui.sv_t > 450u) {
            keep_saved(now, ui.sv_state);
            ui.sv_state = 0;
            en_hush(100, 2);
        }
        return;
    }
    if (pd.heron || ui.btn || ui.keys || now - ui.act_t < 4000u || !(due = keep_due(now)))
        return;
    if (!en_playing) {
        keep_saved(now, due);
        return;
    }
    if (due == 1 && !ui.sv_force && !ui.dirty && pd.act > 0.75f && now - ui.sv_last < KEEP_WORLD_MS + 600000u)
        return;                                  /* a busy moment: a little later */
    ui.sv_state = (uint8_t)due;
    ui.sv_t = now;
    en_hush(0, 0);
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader (announce.c; fm1-reader): a page once it settles, with its values; a
 * value as it turns; a help card; what a button did; what happens in the pond (queued). */
static struct {
    uint8_t view, help, started;
    int16_t val[4];
    uint32_t cap;
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
static void sp_knob(int view, int i)
{
    char t[24];
    if (VIEW_KNOB[view][i] == K_NONE)
        return;
    sp_cat(KNOB_LABEL[view][i]);
    sp_cat(" ");
    knob_text(VIEW_KNOB[view][i], t);
    sp_cat(t);
}
static void sp_page(int all)
{
    int i, first = 1;
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat(VIEW_NAME[ui.view]);
    sp_cat(" page");
    for (i = 0; i < 4; i++)
        if (VIEW_KNOB[ui.view][i] != K_NONE) {
            sp_cat(first ? ": " : ", ");
            first = 0;
            sp_knob(ui.view, i);
        }
    if (!all)
        return;
    sp_cat(". Day ");
    sp_int(W()->day);
    sp_cat(", ");
    sp_cat(PD_SEASON[pd_season()]);
    sp_cat(" ");
    sp_cat(PD_TOD[pd_tod()]);
    sp_cat(", ");
    sp_int((uint32_t)pd_alive());
    sp_cat(" fish");
    sp_cat(sp_tp.playing ? ", singing" : ", resting");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
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
static uint32_t cap_sig(void)
{
    uint32_t h = 2166136261u + pd.cap_until;
    const char *s = pd.cap;
    while (*s)
        h = hash(h, (uint8_t)*s++);
    return h;
}
static void sp_snap(void)
{
    int i;
    for (i = 0; i < 4; i++)
        sp.val[i] = (int16_t)knob_get(VIEW_KNOB[ui.view][i]);
    sp.view = ui.view;
    sp.help = ui.help;
    sp.cap = cap_sig();
}

static void speak(void)
{
    uint32_t now = plat_ms(), c;
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
        }
        sp_page(1);
        ann_say(sp_buf, 0, ui.help ? 0 : ANN_NOW);
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
        ann_say(en_playing ? "SINGING" : "RESTING", 0, 0);
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
    c = cap_sig();
    if (c != sp.cap) {                           /* something happened in the pond: queued */
        sp.cap = c;
        if (now < pd.cap_until && pd.cap[0])
            ann_say(pd.cap, 0, 0);
    }
}

#ifdef PLAT_HOST
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}, {"NEXT CHORD", 0},
                                         {"RESTING", 0}, {"SINGING", 0}, {"FREEZE", "ON"}, {"FREEZE", "OFF"},
                                         {"SAVED", 0}, {"NEW POND", 0}, {"FOOD", "ALL OVER"}};
    uint8_t keep_view = ui.view, keep_help = ui.help;
    int v, i, x;
    char t[48];
    for (v = 0; v < NVIEWS; v++) {
        ui.view = (uint8_t)v;
        sp_page(0);
        sp_print("SCREEN", sp_buf);
        sp_page(1);
        sp_print("WHERE", sp_buf);
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
    for (i = 0; i < (int)(sizeof MSG / sizeof MSG[0]); i++) {
        msg_text(MSG[i][0], MSG[i][1], t);
        sp_print("MSG", t);
    }
    for (x = 0; x < 12; x++) {
        msg_text("FOOD", EN_NOTE_NAME[x], t);
        sp_print("MSG", t);
    }
    msg_text("NOT SAVED", "FLASH ERROR", t);
    sp_print("WARN", t);
}
#endif

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    lcd_fill(0, 0, 240, 240, K_BG);
    if (!en_playing)
        en_play(1);                              /* it sings from the start */
    sp_tp.playing = 1;
    sp_tp.play_t = plat_ms();
    pd.prev = plat_ms();
    (void)draw_help;
}

void ui_frame(void)
{
    input();
    pd_tick(plat_ms());
    autosave();
    draw_header();
    draw_main();
    draw_strip();
    leds();
    speak();
    ann_tick(plat_ms());
    ui.frame++;
}

void ui_input_only(void) { input(); }
