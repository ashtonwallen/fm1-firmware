/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (MONASTERY) */
/* MONASTERY's UI: the panel and the screen (from AMB-1's). Part of the unity build (after gfx.c, project.c and
 * world.c); the host simulator includes it too.
 *
 *   PLAY           the choir sings / rests (the world's clock stops while it rests)
 *   EDIT           the bell: the next office, now
 *   REC            HOLD: the note holds, the chapel rings on        OCT- / OCT+   the chant's pace
 *   white keys 1-7 a brother: asleep, he wakes; else he stands to sing the next verse
 *   HOME CHAPEL    ARP BROTHERS    FX SPACE    GLO SETUP          (SELECT steps through them)
 *   KNOB 1-4       the page's four values
 *   SAVE           saves (it also saves by itself, rarely, while nobody sings); held 1.5 s: the sound's defaults */
#include "fastmath.h"

enum { V_CHAPEL, V_BROTHERS, V_SPACE, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Chapel", "Brothers", "Space", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ARP, B_FX, B_GLO};
enum { K_LEDS = 100, K_DAY, K_PACE, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_PAD, P_BASS, P_BELLS, P_REVERB},
    {K_NONE, K_NONE, K_NONE, K_NONE},
    {P_ECHO, P_SIZE, P_BRIGHT, K_PACE},
    {K_DAY, P_TUNE, P_MIDI, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Voices", "Drone", "Bell", "Reverb"},
    {"", "", "", ""},
    {"Echo", "Size", "Colour", "Pace"},
    {"Day", "Tune", "MIDI out", "Lights"},
};
static const char *const DAY_NAME[WD_NDAY] = {"30 min", "1 h", "2 h", "4 h", "8 h"};

/* the chapel's colours: stone at night, oak stalls, black habits, candlelight */
#define K_BG RGB(20, 18, 26)
#define K_WALL RGB(38, 35, 45)
#define K_JOINT RGB(30, 28, 37)
#define K_STONE RGB(70, 66, 76)
#define K_WOOD RGB(84, 58, 38)
#define K_WOOD_D RGB(58, 40, 27)
#define K_WOOD_L RGB(112, 80, 52)
#define K_ROBE RGB(28, 25, 33)
#define K_ROBE_E RGB(92, 86, 102)
#define K_NOV RGB(196, 188, 168)
#define K_NOV_E RGB(140, 132, 116)
#define K_FACE RGB(132, 96, 72)
#define K_FACE_LIT RGB(222, 168, 118)
#define K_HOODIN RGB(10, 9, 12)
#define K_GOLD RGB(228, 178, 84)
#define K_FLAME RGB(255, 206, 120)
#define K_GLOW RGB(255, 164, 64)
#define K_TEXT RGB(236, 226, 204)
#define K_DIM RGB(138, 128, 116)
#define K_FAINT RGB(78, 72, 70)
#define K_ACC RGB(228, 178, 84)
#define K_RED RGB(214, 92, 70)
#define K_ZZZ RGB(130, 150, 210)
#define K_PANEL RGB(44, 40, 50)

static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];
    uint32_t down_t[NB];
    uint8_t reset_done;
    uint8_t dirty;                     /* the settings changed since the last save */
    uint32_t act_t, saved_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    uint32_t saves;                    /* (tests) saves by itself */
} ui;

static uint8_t sp_said;
static void say_as(const char *a, const char *b, uint32_t flags)
{
    uint32_t i, k = 0;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    if (flags && ann_on()) {
        for (i = 0; ui.msg[0][i]; i++)
            t[k++] = ui.msg[0][i];
        if (ui.msg[1][0])
            t[k++] = ' ';
        for (i = 0; ui.msg[1][i]; i++)
            t[k++] = ui.msg[1][i];
        t[k] = 0;
        ann_say(t, 0, flags);
        sp_said = 1;
    }
}
static void say(const char *a, const char *b) { say_as(a, b, ANN_NOW); }
static void warn(const char *a, const char *b) { say_as(a, b, ANN_NOW | ANN_KEEP); }
void ui_say(const char *a, const char *b) { say(a, b); }

static void mark_dirty(void) { ui.dirty = 1; }
int ui_dirty(void) { return ui.dirty != 0; }

/* ------------------------------------------------------------ values --- */
static int knob_get(int k)
{
    if (k < P_NPARAMS)
        return proj.par[k];
    if (k == K_LEDS)
        return proj.leds;
    if (k == K_DAY)
        return proj.daylen;
    if (k == K_PACE)
        return proj.pace;
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = en_param_info(k)->lo;
        *hi = en_param_info(k)->hi;
    } else {
        *lo = k == K_PACE ? 70 : 0;
        *hi = k == K_LEDS ? 1 : k == K_DAY ? WD_NDAY - 1 : k == K_PACE ? 140 : 0;
    }
}
static char *cat_s(char *b, const char *s)
{
    while ((*b = *s++))
        b++;
    return b;
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k == P_MIDI) {
        cat_s(b, v ? "On" : "Off");
    } else if (k < P_NPARAMS) {
        en_param_text(k, v, b);
    } else if (k == K_LEDS) {
        cat_s(b, v ? "On" : "Off");
    } else if (k == K_DAY) {
        cat_s(b, DAY_NAME[v]);
    } else if (k == K_PACE) {
        itoa_u((uint32_t)v, b);
        cat_s(b + (v >= 100 ? 3 : 2), "%");
    } else {
        b[0] = 0;
    }
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
    } else if (k == K_DAY) {             /* the office keeps its place, at the new pace */
        int32_t old = wr.slot_ms;
        proj.daylen = (uint8_t)v;
        wr.slot_ms = (int32_t)((uint32_t)day_ms() / 150u * OFFICE[proj.world.office].weight);
        wr.end_ms = wr.slot_ms / 100 * OFFICE[proj.world.office].end;
        if (old > 0 && wr.phase == PH_REST)
            wr.t = (int32_t)((float)wr.t * (float)wr.slot_ms / (float)old);
    } else if (k == K_PACE) {
        proj.pace = (uint8_t)v;
    }
    mark_dirty();
}

#ifdef PLAT_HOST
static const uint8_t accel_off = 1;      /* the simulator's "spin": exact steps */
#else
static const uint8_t accel_off = 0;
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
    case B_SEL:
        set_view((ui.view + 1) % NVIEWS);
        break;
    case B_EDIT:
        world_skip();
        say("THE BELL", OFFICE[proj.world.office].name);
        break;
    case B_PLAY:
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
        say("HOLD", en_frozen ? "OFF" : "ON");
        en_freeze(!en_frozen);
        break;
    case B_OCTDN: case B_OCTUP: {
        char t[8];
        knob_set(K_PACE, proj.pace + (b == B_OCTUP ? 10 : -10));
        knob_text(K_PACE, t);
        say("PACE", t);
        break;
    }
    case B_SAVE:
        if (project_save() == 0) {
            ui.dirty = 0;
            wr.dirty = wr.urgent = 0;
            ui.saved_t = plat_ms();
            say("SAVED", 0);
        } else {
            warn("NOT SAVED", "FLASH ERROR");
        }
        break;
    default: break;
    }
}

static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0)) {
        uint32_t st = (pkt >> 8) & 0xFFu;
        if (st == 0xFAu && !en_playing)
            en_play(1);
        else if (st == 0xFCu && en_playing)
            en_play(0);
    }
}

#define RESET_HOLD 1500u
static const uint8_t WHITE[16] = PLAT_WHITE_KEYS;
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
    for (i = 0; i < NMONK; i++)
        if ((ch & keys) >> WHITE[i] & 1u)
            world_nudge((int)i);
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {
        int v = (int)ui.view + (e > 0 ? 1 : -1);
        if (v >= 0 && v < NVIEWS)
            set_view(v);
    }
    plat_enc(EN_ALGO);
    plat_enc(EN_PRESET);
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && plat_ms() - ui.down_t[B_SAVE] > RESET_HOLD) {
        ui.reset_done = 1;
        ui.btn_used |= 1u << B_SAVE;
        project_sound_defaults();
        project_apply();
        say("RESET", "THE SOUND");
        mark_dirty();
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            turn(EN_K1 + (int)i, VIEW_KNOB[ui.view][i], e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 1200u;
            ui.act_t = plat_ms();
        }
}

/* ------------------------------------------------------------- screen --- */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144
#define LOW_Y 172
#define LOW_H 68

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

/* candlelight: a soft round glow of colour c, strength 0..1, falling off to nothing at r */
static void glow(int32_t cx, int32_t cy, int32_t r, uint16_t c, float k)
{
    int32_t x, y;
    float r2 = (float)(r * r), inv = 1.0f / r2;
    if (k <= 0.02f)
        return;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++) {
            float d = (float)(x * x + y * y);
            if (d < r2) {
                float f = 1.0f - d * inv;
                px_blend(cx + x, cy + y, c, (uint32_t)(f * f * k * 10.0f));
            }
        }
}

static uint16_t mix(uint16_t a, uint16_t b, int t)
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
    uint16_t c = b <= 1 ? K_RED : K_DIM;
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
        cv_rect(x + 2 + i * 5, 11, 4, 6, b <= 1 ? K_RED : K_GOLD);
}
static const char *batt_words(int b)
{
    static const char *const W[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                     "On USB power"};
    return b >= 0 && b <= 4 ? W[b] : 0;
}

static void clock_text(char *t)          /* "18:40" */
{
    int m = world_minutes();
    t[0] = (char)('0' + m / 600);
    t[1] = (char)('0' + m / 60 % 10);
    t[2] = ':';
    t[3] = (char)('0' + m % 60 / 10);
    t[4] = (char)('0' + m % 10);
    t[5] = 0;
}

/* the header: the office in a gold tag, the hour; on the right the day and the battery */
static void draw_header(void)
{
    char t[24], c[8];
    int msg = plat_ms() < ui.msg_until;
    uint32_t h;
    clock_text(c);
    h = hash(hash(hash(2166136261u, proj.world.office | (uint32_t)proj.world.day << 8),
                  (uint32_t)en_playing | ui.dirty << 1 | wr.dirty << 2 | (uint32_t)msg << 3 | (uint32_t)en_frozen << 4 |
                      (uint32_t)wr.phase << 5),
             (uint32_t)(c[0] << 24 | c[1] << 16 | c[3] << 8 | c[4]) ^ (uint32_t)(batt() + 1) << 28);
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
        if (ui.dirty || wr.dirty) {
            cv_round(x - 5, 12, 5, 5, 2, K_DIM);
            x -= 11;
        }
        t[0] = 'D', t[1] = 'A', t[2] = 'Y', t[3] = ' ';
        itoa_u(proj.world.day, t + 4);
        x -= text_w(&FONT_S, t);
        cv_text(x, 6, &FONT_S, t, K_DIM);
        caps(t, OFFICE[proj.world.office].name, sizeof t);
        w = text_w(&FONT_B, t) + 12;
        cv_round(4, 4, w, 20, 6, en_playing ? K_GOLD : K_FAINT);
        cv_text(10, 6, &FONT_B, t, K_BG);
        w += 12;
        cv_text(w, 6, &FONT_S, c, K_TEXT);
        w += text_w(&FONT_S, c) + 8;
        if (!en_playing)
            cv_text(w, 8, &FONT_XS, "resting", K_DIM);
        else if (en_frozen)
            cv_text(w, 8, &FONT_XS, "held", K_ZZZ);
        else if (wr.phase == PH_REST)
            cv_text(w, 8, &FONT_XS, proj.world.office == OF_COMPLINE ? "night" : "quiet", K_DIM);
    }
    cv_blit(0, 0);
}

/* ---- the chapel */
static const int32_t POS_X[4] = {26, 72, 168, 214};
static const int32_t ROW_BASE[2] = {84, 128};

static uint16_t sky(void)                /* the window: the hour's light */
{
    static const struct {
        int16_t m;
        uint8_t r, g, b;
    } K[] = {{0, 14, 18, 44},    {300, 20, 24, 56},   {330, 170, 104, 96}, {420, 150, 180, 210},
             {960, 140, 176, 206}, {1080, 214, 124, 70}, {1170, 70, 52, 96},  {1260, 16, 20, 48}, {1440, 14, 18, 44}};
    int m = world_minutes(), i;
    for (i = 0; i + 1 < (int)(sizeof K / sizeof K[0]); i++)
        if (m < K[i + 1].m) {
            int t = (m - K[i].m) * 16 / (K[i + 1].m - K[i].m);
            return mix(RGB(K[i].r, K[i].g, K[i].b), RGB(K[i + 1].r, K[i + 1].g, K[i + 1].b), t);
        }
    return RGB(14, 18, 44);
}

static void candle(int32_t x, int32_t y, int lit, uint32_t fr)   /* a candle standing on y */
{
    cv_rect(x, y - 7, 3, 7, RGB(226, 216, 190));
    if (lit) {
        int fl = (int)(fr / 5 + (uint32_t)x) % 3;
        glow(x + 1, y - 10, 9, K_GLOW, 0.5f);
        cv_rect(x + 1, y - 11 - (fl == 1), 1, 3 + (fl == 1), K_FLAME);
        cv_rect(x, y - 9, 3, 1, mix(K_FLAME, K_GLOW, 8));
    }
}

static void draw_bell(int32_t cx, int32_t cy, float g)
{
    int32_t sw = g > 0.05f ? (int32_t)(fm_sinf(g * 18.0f) * 3.0f * g) : 0, j;
    glow(cx, cy + 6, 16, K_GOLD, g * 0.9f);
    cv_rect(cx - 1, cy - 2, 3, 2, K_GOLD);
    for (j = 0; j < 9; j++) {            /* the bell, swinging */
        int32_t hw = 2 + j / 2 + (j == 8);
        cv_rect(cx - hw + sw * j / 9, cy + j, 2 * hw + 1, 1, j == 8 ? K_GOLD : mix(K_GOLD, K_WOOD, 6));
    }
    cv_rect(cx + sw - 1, cy + 9, 3, 2, K_GOLD);
}

static void zigzag(int32_t x0, int32_t x1, int32_t y, uint16_t c)   /* the rivals' quarrel */
{
    int32_t x, k = 0;
    for (x = x0; x < x1; x += 4, k++)
        cv_line(x, y + ((k & 1) ? -2 : 2), x + 4, y + ((k & 1) ? 2 : -2), c);
}

/* a hooded brother standing in his stall: base the floor of the stall; lit 0..1 (he sings) */
static void draw_monk(int m, int32_t cx, int32_t base, float lit, uint32_t fr)
{
    const monk_t *k = mk(m);
    int nov = is_novice(m), sleep = wr.asleep[m], old = m == MK_ELDER;
    uint16_t robe = nov ? K_NOV : K_ROBE, edge = nov ? K_NOV_E : K_ROBE_E;
    int32_t hx = cx + (sleep ? 3 : 0), hy = base - 21 + (sleep ? 3 : 0) + (old ? 2 : 0), y;
    (void)k;
    if (lit > 0.02f)
        glow(cx, base - 16, 26, K_GLOW, 0.5f + lit);
    for (y = base - 15; y <= base; y++) {   /* the habit, widening to the floor */
        int32_t hw = 6 + (y - (base - 15)) * 4 / 15;
        cv_rect(cx - hw, y, 2 * hw + 1, 1, robe);
        cv_rect(cx - hw, y, 1, 1, edge);
        if (lit > 0.1f)
            px_blend(cx - hw + 1, y, K_GLOW, (uint32_t)(lit * 8.0f));
    }
    cv_disc(hx, hy, 6, robe);            /* the hood */
    cv_rect(hx - 1, hy - 7, 3, 2, robe);
    cv_disc(hx - 1, hy - 1, 1, edge);
    cv_disc(hx, hy + 1, 3, K_HOODIN);    /* the face, in its shadow */
    cv_rect(hx - 2, hy + 1, 5, 3, mix(K_FACE, K_FACE_LIT, q16(lit)));
    if (old)                             /* a white beard */
        cv_rect(hx - 1, hy + 4, 3, 2, RGB(200, 200, 196));
    if (!sleep) {                        /* the book */
        cv_rect(cx - 5, base - 11, 11, 4, K_WOOD_D);
        cv_rect(cx - 4, base - 12, 4, 2, RGB(232, 222, 196));
        cv_rect(cx + 1, base - 12, 4, 2, RGB(232, 222, 196));
        if (lit > 0.3f)
            cv_rect(cx - 1, base - 13 - (int32_t)(fr / 4 % 2), 1, 1, K_FLAME);
    } else {                             /* asleep: z z */
        int32_t b = (int32_t)(fr / 12 % 3);
        cv_text(cx + 7, base - 34 - b, &FONT_XS, "z", K_ZZZ);
        cv_text(cx + 12, base - 42 - b, &FONT_XS, "Z", K_ZZZ);
    }
    if (m == MK_ABBOT) {                 /* the abbot's cross and crozier */
        cv_rect(cx, base - 9, 1, 5, K_GOLD);
        cv_rect(cx - 1, base - 8, 3, 1, K_GOLD);
        cv_rect(cx + 10, base - 26, 1, 26, K_GOLD);
        cv_rect(cx + 10, base - 28, 3, 1, K_GOLD);
        cv_rect(cx + 13, base - 27, 1, 3, K_GOLD);
    }
    if (wr.monk_cents[m] <= -12) {       /* sinking flat: an arrow down */
        int32_t x = cx - 13, y0 = base - 28;
        cv_rect(x, y0, 1, 6, K_ZZZ);
        cv_rect(x - 1, y0 + 4, 3, 1, K_ZZZ);
        cv_rect(x, y0 + 6, 1, 1, K_ZZZ);
    }
}

static void draw_stalls(int row)        /* the oak stalls of a row, their arched backs */
{
    int32_t base = ROW_BASE[row], s;
    for (s = 0; s < 2; s++) {
        int32_t x0 = s ? 142 : 4, x1 = s ? 236 : 98, x;
        cv_rect(x0, base - 30, x1 - x0, 30, K_WOOD_D);
        for (x = 0; x < 2; x++) {
            int32_t cx = POS_X[s * 2 + x];
            cv_round(cx - 20, base - 34, 41, 30, 6, K_WOOD);
            cv_round(cx - 17, base - 31, 35, 26, 5, K_WOOD_D);
        }
        cv_rect(x0, base, x1 - x0, 3, K_WOOD_L);
        cv_rect(x0, base + 3, x1 - x0, 1, K_WOOD_D);
    }
}

static float monk_lit(int m)             /* how loud he sings now: his voice's level */
{
    int s = wr.monk_slot[m];
    float a;
    if (s < 0) {
        int k;                           /* his voice's tail (he has just stopped) */
        for (k = 0; k < EN_NPAD; k++)
            if (wr.v_monk[k] == m && en_pad_amp[k] > 0.02f)
                return fm_minf(en_pad_amp[k] * 1.3f, 1.0f) * 0.6f;
        return 0.0f;
    }
    a = en_pad_amp[s] * 2.0f;
    return fm_clampf(a, 0.25f, 1.0f);
}

static void draw_chapel(void)
{
    uint32_t fr = ui.frame;
    int i, lit = en_playing && wr.phase != PH_REST;
    int32_t y;
    cv_begin(240, MAIN_H, K_WALL);
    for (y = 6; y < MAIN_H; y += 11) {   /* the stones */
        int32_t x;
        cv_rect(0, y, 240, 1, K_JOINT);
        for (x = (y / 11 & 1) * 13; x < 240; x += 26)
            cv_rect(x, y - 10, 1, 10, K_JOINT);
    }
    {   /* the window: the hour's sky in an arch */
        uint16_t c = sky();
        cv_round(105, 4, 30, 40, 6, K_STONE);
        cv_disc(120, 16, 6, K_STONE);
        cv_rect(108, 16, 24, 26, c);
        for (y = 0; y < 12; y++) {       /* the arch */
            int32_t hw = (int32_t)fm_sqrtf((float)(144 - (12 - y) * (12 - y)));
            cv_rect(120 - hw, 4 + y, 2 * hw, 1, c);
        }
        cv_rect(119, 5, 2, 37, K_STONE);
        cv_rect(108, 24, 24, 2, K_STONE);
        if (world_minutes() < 330 || world_minutes() >= 1230)
            cv_disc(113, 12, 2, RGB(226, 226, 204));   /* the moon */
        glow(120, 26, 30, c, 0.35f);
    }
    cv_rect(88, 44, 64, 9, K_STONE);     /* the altar, its cloth, its candles */
    cv_rect(88, 44, 64, 2, RGB(206, 196, 170));
    cv_rect(92, 46, 2, 7, K_GOLD);
    cv_rect(146, 46, 2, 7, K_GOLD);
    candle(95, 44, lit, fr);
    candle(142, 44, lit, fr);
    cv_rect(119, 34, 2, 10, K_GOLD);     /* the altar cross */
    cv_rect(116, 37, 8, 2, K_GOLD);
    draw_bell(16, 8, en_toll_glow);
    draw_stalls(0);
    draw_stalls(1);
    /* the lectern in the aisle, the great book open, its candle */
    cv_rect(118, 92, 4, 32, K_WOOD);
    cv_rect(112, 122, 16, 3, K_WOOD_D);
    cv_rect(110, 88, 20, 5, K_WOOD_L);
    cv_rect(111, 85, 8, 3, RGB(232, 222, 196));
    cv_rect(121, 85, 8, 3, RGB(232, 222, 196));
    candle(126, 86, wr.phase == PH_CHANT && en_playing, fr + 7);
    for (i = 0; i < NMONK; i++) {
        int32_t cx = POS_X[MONK_POS[i]], base = ROW_BASE[MONK_ROW[i]];
        if (mk(i)->here == 1) {
            draw_monk(i, cx, base, monk_lit(i), fr);
        } else if (mk(i)->here == 2) {   /* in the infirmary: his stall empty, a small red cross */
            cv_rect(cx - 1, base - 16, 3, 9, K_RED);
            cv_rect(cx - 4, base - 13, 9, 3, K_RED);
        }
    }
    if (wr.clash_now && here(MK_GREGORY) && here(MK_BERNARD))
        zigzag(POS_X[MONK_POS[MK_GREGORY]] + 9, POS_X[MONK_POS[MK_BERNARD]] - 9, ROW_BASE[0] - 26, K_RED);
    for (i = 0; i < NMONK; i++) {        /* the names under the stalls */
        int32_t cx = POS_X[MONK_POS[i]], base = ROW_BASE[MONK_ROW[i]];
        uint16_t c = wr.monk_slot[i] >= 0 ? K_TEXT : mk(i)->here == 1 ? K_DIM : K_FAINT;
        if (mk(i)->here)
            text_c(cx, base + 1, &FONT_XS, MONK_NAME[i], c);
    }
    if (en_frozen)
        text_c(120, 128, &FONT_XS, "held", K_ZZZ);
}

/* the brothers: a row each, his place, his state, his history */
static void monk_state(int i, char *a, char *b)
{
    const monk_t *k = mk(i);
    char n[12];
    a[0] = b[0] = 0;
    if (!k->here) {
        cat_s(a, i == MK_NEW ? "not yet come" : "gone");
        return;
    }
    if (k->here == 2) {
        cat_s(a, "in the infirmary");
    } else if (i == MK_ABBOT && wr.monk_slot[i] < 0 && !wr.asleep[i]) {
        cat_s(a, "abbot, leads");
    } else if (is_novice(i) && wr.monk_slot[i] < 0) {
        itoa_u(k->skill, n);
        cat_s(cat_s(cat_s(a, "novice, "), n), "%");
    } else if (wr.asleep[i]) {
        cat_s(a, "asleep");
    } else if (wr.monk_slot[i] >= 0) {
        cat_s(a, "singing");
    } else {
        switch (i) {
        case MK_ELDER: cat_s(a, k->health < 60 ? "elder, frail" : "elder, steady"); break;
        case MK_GREGORY: case MK_BERNARD:
            cat_s(cat_s(a, proj.world.peace_days ? "at peace with " : "rival of "),
                  MONK_NAME[i == MK_GREGORY ? MK_BERNARD : MK_GREGORY]);
            break;
        case MK_SLEEPER: cat_s(a, "dozes at Matins"); break;
        default: cat_s(a, "took his vows"); break;
        }
    }
    switch (i) {                          /* his history, in one number */
    case MK_ABBOT: itoa_u(k->sung, n); cat_s(cat_s(b, n), " sung"); break;
    case MK_ELDER: itoa_u(k->flats, n); cat_s(cat_s(b, n), " flat"); break;
    case MK_GREGORY: case MK_BERNARD: itoa_u(k->clashes, n); cat_s(cat_s(b, n), " clashes"); break;
    case MK_SLEEPER: itoa_u(k->naps, n); cat_s(cat_s(b, n), " naps"); break;
    default: itoa_u(k->slips, n); cat_s(cat_s(b, n), " slips"); break;
    }
}
static const char *monk_title(int i)
{
    return i == MK_ABBOT ? "Abbot" : is_novice(i) ? "Novice" : i == MK_ELDER ? "Elder" : "Brother";
}

static void draw_brothers(void)
{
    int i;
    cv_begin(240, MAIN_H, K_BG);
    for (i = 0; i < NMONK; i++) {
        int32_t y = 2 + i * 20;
        char a[32], b[16];
        uint16_t c = mk(i)->here == 1 ? K_TEXT : K_FAINT;
        monk_state(i, a, b);
        if (wr.monk_slot[i] >= 0)
            glow(10, y + 8, 9, K_GLOW, 0.8f);
        cv_disc(10, y + 7, 4, is_novice(i) ? K_NOV : K_ROBE_E);   /* a hood */
        cv_disc(10, y + 8, 2, K_HOODIN);
        cv_text(20, y, &FONT_B, MONK_NAME[i], c);
        cv_text(86, y + 1, &FONT_XS, a, mk(i)->here == 2 ? K_RED : wr.monk_slot[i] >= 0 ? K_ACC : K_DIM);
        if (mk(i)->here)
            cv_text(236 - text_w(&FONT_XS, b), y + 1, &FONT_XS, b, K_DIM);
        if (i < NMONK - 1)
            cv_rect(20, y + 18, 216, 1, K_PANEL);
    }
}

static void draw_main(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)wr.phase << 4 | (uint32_t)en_frozen << 6 |
                                       (uint32_t)en_playing << 7 | (uint32_t)wr.clash_now << 8),
             i;
    if (ui.frame % 3u)
        return;
    for (i = 0; i < NMONK; i++)
        h = hash(h, (uint32_t)mk(i)->here | (uint32_t)wr.asleep[i] << 2 | (uint32_t)(wr.monk_slot[i] + 1) << 3 |
                        (uint32_t)q16(monk_lit((int)i)) << 6 | (uint32_t)(wr.monk_cents[i] <= -12) << 11 |
                        (uint32_t)mk(i)->vowed << 12);
    h = hash(h, (uint32_t)q16(en_toll_glow) | (uint32_t)world_minutes() << 8);
    if (ui.view == V_CHAPEL)
        h = hash(h, ui.frame / 6u);      /* candle flames, the sleeper's z */
    else
        h = hash(h, (uint32_t)proj.world.peace_days | (uint32_t)mk(MK_NOVICE)->skill << 8 |
                        (uint32_t)mk(MK_NEW)->skill << 16 | (uint32_t)proj.world.nchron << 24);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    if (ui.view == V_BROTHERS)
        draw_brothers();
    else
        draw_chapel();
    cv_blit(0, MAIN_Y);
}

/* the lower band: the event line, then the page's knobs (the Brothers: the chronicle) */
static void chron_line(const chron_t *c, char *t)
{
    char d[48];
    char *p = cat_s(t, "Day ");
    itoa_u(c->day, p);
    p += c->day >= 100 ? 3 : c->day >= 10 ? 2 : 1;
    p = cat_s(cat_s(cat_s(p, " "), OFFICE[c->office % NOFFICE].name), ": ");
    ev_text(c->type, c->who, c->who2, c->office, d);
    cat_s(p, d);
}

static void draw_low(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8), i;
    int now_touch = plat_ms() < ui.touch_until;
    uint32_t age = plat_ms() - wr.line_t;
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(VIEW_KNOB[ui.view][i]));
    h = hash(hash(h, (uint32_t)now_touch | (age > 90000u) << 1), wr.line_t);
    h = hash(h, proj.world.nchron | (uint32_t)proj.world.chron_w << 8);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, LOW_H, K_BG);
    cv_rect(0, 0, 240, 1, K_PANEL);
    if (wr.line[0]) {                    /* the latest event, a candle beside it */
        cv_rect(7, 7, 3, 7, RGB(226, 216, 190));
        cv_rect(8, 4, 1, 3, K_FLAME);
        cv_text(16, 1, &FONT_XS, wr.line, age > 90000u ? K_DIM : K_TEXT);
    }
    if (ui.view == V_BROTHERS) {         /* the chronicle */
        for (i = 0; i < 3; i++) {
            const chron_t *c = chron_get((int)i + 1);
            char t[64];
            if (!c)
                break;
            chron_line(c, t);
            cv_text(8, 19 + (int32_t)i * 16, &FONT_XS, t, i ? K_FAINT : K_DIM);
        }
        if (!proj.world.nchron || !chron_get(1))
            cv_text(8, 19, &FONT_XS, "The chronicle is short yet.", K_FAINT);
        cv_blit(0, LOW_Y);
        return;
    }
    {
        char t[12];
        int k;
        int32_t xd = 237 - NVIEWS * 6, xt;
        caps(t, VIEW_NAME[ui.view], sizeof t);
        xt = xd - 5 - text_w(&FONT_XS, t);
        cv_rect(4, 23, xt - 9, 1, K_PANEL);
        cv_text(xt, 16, &FONT_XS, t, K_ACC);
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 6, 21, 4, 4, 2, k == ui.view ? K_ACC : K_PANEL);
    }
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v, hot = now_touch && ui.touched == (int)i;
        int32_t x = (int32_t)i * 60, cx = x + 30, fw;
        char t[16];
        if (k == K_NONE)
            continue;
        if (hot)
            cv_round(x + 3, 28, 54, 39, 6, K_PANEL);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        caps(t, KNOB_LABEL[ui.view][i], sizeof t);
        text_c(cx, 28, &FONT_XS, t, hot ? K_TEXT : K_DIM);
        knob_text(k, t);
        text_c(cx, 42, &FONT_B, t, hot ? K_ACC : K_TEXT);
        cv_round(cx - 22, 61, 44, 3, 1, K_PANEL);
        fw = hi > lo ? (int32_t)((v - lo) * 44 / (hi - lo)) : 0;
        if (fw > 0)
            cv_round(cx - 22, 61, fw < 3 ? 3 : fw, 3, 1, hot ? K_ACC : K_GOLD);
    }
    cv_blit(0, LOW_Y);
}

static void leds(void)
{
    uint32_t b = 0, kb = 0, kd = 0;
    int i;
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (en_frozen)
        b |= 1u << B_REC;
    if (proj.leds)
        for (i = 0; i < NMONK; i++) {
            if (wr.monk_slot[i] >= 0)
                kb |= 1u << WHITE[i];
            else if (mk(i)->here == 1)
                kd |= 1u << WHITE[i];
        }
    plat_lights(b, kb, 0, kd, proj.leds ? 1 : 0);
}

/* SAVING, and the flash's wear: every save erases one of the same two 4 KiB sectors (APP_STORE_MAP's A/B pair), rated
 * about 100k erases, so the monastery saves itself rarely. keep_due is the whole policy:
 *   the world (wr.dirty; a big event, wr.urgent, no sooner) at most once an hour after the last save; nothing
 *     changed, no save. Up to an hour of the world is lost at a power-off (the FM-1 gives no warning);
 *   a setting (ui.dirty) a few seconds after the knobs rest, at most once every 5 minutes;
 *   on the battery, once it reads empty (0): one save of whatever changed, then none of these until it reads above 0;
 *   SAVE saves at once.
 * When: only while nobody sings and the bell has rung out (a flash erase silences the sound for a moment); resting,
 * a few seconds after the last touch, the pad gone. Each save resets the clock: it keeps the settings and the world
 * alike. */
#define SAVE_QUIET 4000u
#define KEEP_WORLD_MS (60u * 60000u)
#define KEEP_SETTINGS_MS (5u * 60000u)
static uint8_t keep_low;                 /* the empty battery's save done; again once it reads above 0 */
static int keep_due(uint32_t now)        /* 0 not yet, 1 due, 2 due: the empty battery's save */
{
    uint32_t since = now - ui.saved_t;
    if (!ui.dirty && !wr.dirty)
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
        keep_low = 0;                    /* (every pass: the battery read above 0 again) */
    if (ui.btn || ui.keys || now - ui.act_t < SAVE_QUIET)
        return;
    if (en_playing ? !world_quiet() : en_pad_level >= 0.01f)
        return;
    if (!(due = keep_due(now)))
        return;
    if (project_save() == 0) {
        ui.dirty = 0;
        wr.dirty = wr.urgent = 0;
        ui.saves++;
    }
    ui.saved_t = now;                    /* (a failed write: tried again later, not every pass) */
    if (due == 2)
        keep_low = 1;
}

/* ------------------------------------------------------ screen reader --- */
/* what a screen reader on the computer hears (announce.c): WHERE, the office, the hour and who sings, and the page;
 * each event as it happens (queued); a knob as it turns; a page once it settles. Nothing is built while no reader
 * listens. */
static char sp_buf[ANN_MAX + 1];
static uint32_t sp_n;
static struct {
    uint8_t view, started;
    int16_t val[4];
} sp;
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
    if (ui.view == V_BROTHERS) {
        for (i = 0; i < NMONK; i++) {
            char a[32], b[16];
            if (!mk(i)->here)
                continue;
            monk_state(i, a, b);
            sp_cat(i ? ". " : ": ");
            sp_cat(monk_title(i));
            sp_cat(" ");
            sp_cat(MONK_NAME[i]);
            sp_cat(", ");
            sp_cat(a);
            sp_cat(", ");
            sp_cat(b);
        }
        return;
    }
    for (i = 0; i < 4; i++) {
        sp_cat(i ? ", " : ": ");
        sp_knob(ui.view, i);
    }
}
static void sp_where(void)
{
    char c[8], t[8];
    int i, n = 0;
    sp_n = 0;
    sp_buf[0] = 0;
    clock_text(c);
    sp_cat(OFFICE[proj.world.office].name);
    sp_cat(", ");
    sp_cat(c);
    sp_cat(", day ");
    itoa_u(proj.world.day, t);
    sp_cat(t);
    sp_cat(en_playing ? (wr.phase == PH_REST ? ", quiet" : ", singing") : ", resting");
    for (i = 0; i < NMONK; i++)
        if (wr.monk_slot[i] >= 0) {
            sp_cat(n++ ? ", " : ". Singing: ");
            sp_cat(MONK_NAME[i]);
        }
    if (wr.line[0]) {
        sp_cat(". ");
        sp_cat(wr.line);
    }
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static void sp_snap(void)
{
    int i;
    for (i = 0; i < 4; i++)
        sp.val[i] = (int16_t)knob_get(VIEW_KNOB[ui.view][i]);
    sp.view = ui.view;
}
static void speak(void)
{
    uint32_t now = plat_ms();
    int where = ann_where(), i, changed = -1;
    if (!ann_on()) {
        sp.started = 0;
        sp_said = 0;
        wr.said = 0;
        return;
    }
    if (!sp.started || where) {
        sp.started = 1;
        sp_snap();
        sp_where();
        ann_say(sp_buf, 0, ANN_NOW);
        sp_page();
        ann_say(sp_buf, 0, 0);
        sp_said = 0;
        wr.said = 0;
        return;
    }
    if (sp_said) {
        sp_said = 0;
        sp_snap();
        return;
    }
    if (wr.said) {                       /* an event: queued */
        wr.said = 0;
        ann_say(wr.line, 0, 0);
    }
    if (ui.view != sp.view) {
        sp_snap();
        sp_page();
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
    if (changed >= 0 && VIEW_KNOB[ui.view][changed] != K_NONE) {
        sp_n = 0;
        sp_knob(ui.view, changed);
        ann_later(sp_buf, ANN_NOW, 120u, now);
    }
}

#ifdef PLAT_HOST
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    uint8_t keep_view = ui.view;
    project_t keep = proj;
    int v, i, x, lo, hi, e;
    char t[64];
    for (v = 0; v < NVIEWS; v++) {
        ui.view = (uint8_t)v;
        sp_page();
        sp_print("SCREEN", sp_buf);
        for (i = 0; i < 4; i++) {
            int k = VIEW_KNOB[v][i];
            if (k == K_NONE)
                continue;
            knob_range(k, &lo, &hi);
            for (x = lo; x <= hi; x++) {
                if (k < P_NPARAMS)
                    proj.par[k] = (int16_t)x;
                else if (k == K_LEDS)
                    proj.leds = (uint8_t)x;
                else if (k == K_DAY)
                    proj.daylen = (uint8_t)x;
                else
                    proj.pace = (uint8_t)x;
                sp_n = 0;
                sp_knob(v, i);
                sp_print("KNOB", sp_buf);
            }
            proj = keep;
        }
    }
    ui.view = keep_view;
    sp_where();
    sp_print("WHERE", sp_buf);
    for (e = 1; e < NEV; e++)
        for (i = 0; i < NMONK; i++) {
            ev_text(e, i, (i + 1) % NMONK, i, t);
            sp_print("EVENT", t);
        }
    for (x = 0; x <= 4; x++)
        sp_print("WHERE", batt_words(x));
}
#endif

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.saved_t = ui.act_t = plat_ms();
    lcd_fill(0, 0, 240, 240, K_BG);
    if (!en_playing)
        en_play(1);                      /* it sings from the start */
}

void ui_frame(void)
{
    input();
    world_tick();
    autosave();
    draw_header();
    draw_main();
    draw_low();
    leds();
    speak();
    ann_tick(plat_ms());
    ui.frame++;
}

void ui_input_only(void) { input(); }
