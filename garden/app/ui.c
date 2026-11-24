/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (GARDEN) */
/* GARDEN UI: the panel and the screen. Part of the unity build (after gfx.c, project.c and garden.c); the host
 * simulator includes it too. Drawing helpers, the knob strip, help cards and the screen reader's ways are AMB-1's.
 *
 *   keys           plant a seed at that pitch (in the garden's scale; low keys left, high keys right)
 *   PLAY           the garden grows / rests        REC   FREEZE: time stops, the chord holds, the reverb rings
 *   EDIT           the gardener waters everything  ARP   a seed at a random note of the scale
 *   OCT- / OCT+    the bells' octave               ALGORITHM   the mode (the scale)
 *   HOME ENV GLO SEQ   the pages: Garden, Sound, Setup, Ledger (SELECT steps through them; HOME + SELECT: tempo)
 *   PRESETS        scrolls the Ledger              KNOB 1-4   the page's four values
 *   LFO / SEL      the garden's sound (1.1): the next / the last preset, named on the screen; the garden stays
 *   SAVE           saves (it also saves by itself, rarely: see autosave); held 3 s: a new garden */
#include "fastmath.h"

enum { V_GARDEN, V_SOUND, V_SETUP, V_LEDGER, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Garden", "Sound", "Setup", "Ledger"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_GLO, B_SEQ};
enum { K_LEDS = 100, K_GROWTH, K_SCROLL, K_NONE };
static const char *const GROWTH_NAME[G_NGROWTH] = {"Slow", "Calm", "Lively", "Quick"};
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_TEMPO, K_GROWTH, P_BELLS, P_PAD},
    {P_BASS, P_ECHO, P_REVERB, P_PADTONE},
    {P_ROOT, P_TUNE, P_MIDI, K_LEDS},
    {K_SCROLL, K_NONE, K_NONE, K_NONE},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Tempo", "Growth", "Bells", "Pad"},
    {"Bass", "Echo", "Reverb", "Colour"},
    {"Root", "Tune", "MIDI out", "Lights"},
    {"Scroll", "", "", ""},
};

/* the bands' colours: AMB-1's paper and ink */
#define K_BG RGB(246, 238, 181)
#define K_PANEL RGB(255, 251, 224)
#define K_LINE RGB(208, 214, 150)
#define K_DIM RGB(112, 106, 58)
#define K_TEXT RGB(29, 27, 2)
#define K_ACC RGB(64, 128, 124)
#define K_PAD RGB(112, 142, 62)
#define K_PAD_T RGB(208, 220, 146)
#define K_FROST RGB(60, 132, 160)
#define K_LOW RGB(196, 84, 52)

static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];
    uint32_t down_t[NB];
    uint32_t turn_t;
    uint8_t help;
    uint8_t reset_done;
    uint8_t dirty;                     /* a setting changed since the last save */
    uint32_t act_t, saved_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[4];
    uint32_t prev_ms;                  /* the last frame's time (the garden's clock) */
    uint8_t scroll;                    /* the ledger's first plant */
    uint32_t log_seen;                 /* the event line last shown */
    uint32_t log_t;                    /* when it came: it glows a moment */
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

static void mark_dirty(void) { ui.dirty = 1; }
int ui_dirty(void) { return ui.dirty != 0; }

static uint32_t pool_abs(void)
{
    uint32_t p = g_pool(), r = (uint32_t)g_root();
    return ((p << r) | (p >> (12u - r))) & 0xFFFu;
}
static const char *mode_name(void)
{
    return g_custom() ? "Custom" : EN_MODE_NAME[g_mode()];   /* (as the preset colours it) */
}

/* ------------------------------------------------------------ values --- */
static int knob_get(int k)
{
    if (k < P_NSAVED)
        return proj.par[k];
    if (k == K_LEDS)
        return proj.leds;
    if (k == K_GROWTH)
        return proj.growth;
    if (k == K_SCROLL)
        return ui.scroll;
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NSAVED) {
        *lo = en_param_info(k)->lo;
        *hi = en_param_info(k)->hi;
    } else {
        *lo = 0;
        *hi = k == K_LEDS ? 2 : k == K_GROWTH ? G_NGROWTH - 1 : k == K_SCROLL ? G_NPLOT - 1 : 0;
    }
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k < P_NSAVED)
        en_param_text(k, k == P_ROOT ? g_root() : v, b);   /* (the key as the preset moves it) */
    else if (k == K_LEDS)
        cat_s(b, v == 2 ? "Keys" : v ? "On" : "Off");
    else if (k == K_GROWTH)
        cat_s(b, GROWTH_NAME[v % G_NGROWTH]);
    else if (k == K_SCROLL)
        itoa_u((uint32_t)v + 1u, b);
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
    if (k == K_SCROLL) {
        ui.scroll = (uint8_t)v;
        return;
    }
    if (k < P_NSAVED) {
        proj.par[k] = (int16_t)v;
        en_set(k, pv(k));
        if (k == P_ROOT)
            garden_resync();
        else
            garden_bend_again();
    } else if (k == K_LEDS) {
        proj.leds = (uint8_t)v;
    } else if (k == K_GROWTH) {
        proj.growth = (uint8_t)v;
    }
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
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------- the garden --- */
static void plant_key(int pitch)
{
    int i = garden_plant_key(pitch);
    char b[24];
    if (i < 0) {
        warn("NO ROOM", "WAIT FOR COMPOST");
        return;
    }
    say("PLANTED", plant_name(&proj.w.p[i], b));
}

static void set_mode(int m)
{
    m = (m % EN_NMODES + EN_NMODES) % EN_NMODES;
    proj.par[P_MODE] = (int16_t)m;
    proj.pool = EN_MODE_POOL[m];
    en_set(P_MODE, pv(P_MODE));
    en_pool(g_pool());
    garden_resync();
    say("MODE", mode_name());
    mark_dirty();
}

static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
    ui.touch_until = 0;
    ui.sig[1] = ui.sig[2] = 0;           /* the bands drawn anew */
}

static void preset_step(int dir)                /* LFO / SEL: the next or the last preset, named */
{
    garden_preset((int)proj.preset + (dir > 0 ? 1 : -1));
    mark_dirty();
    say("PRESET", preset_name(proj.preset));
    ui.msg_until = plat_ms() + 2500u;
}

static void autosave_now(void);
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
        garden_water_all();
        say("WATERING", 0);
        break;
    case B_ARP: {
        int n = 53 + (int)(plat_ms() * 2654435761u >> 27) % 27;
        plant_key(n);
        break;
    }
    case B_PLAY:
        sp_tp.playing = !en_playing;
        sp_tp.play_t = plat_ms();
        if (en_playing) {
            en_play(0);
            say("RESTING", 0);
        } else {
            en_seed(plat_ms());
            en_play(1);
            say("GROWING", 0);
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
            say("BELL OCTAVE", t);
        }
        break;
    case B_LFO: case B_SEL: preset_step(b == B_LFO ? 1 : -1); break;
    case B_SAVE:
        autosave_now();
        if (!ui.dirty && !garden_changed)
            say("SAVED", 0);
        else
            warn("NOT SAVED", "FLASH ERROR");
        break;
    default: break;
    }
}

static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0)) {
        uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)
            en_clock(st == 0xF8u ? EN_CLK_TICK : st == 0xFAu ? EN_CLK_START : st == 0xFBu ? EN_CLK_CONTINUE : EN_CLK_STOP);
        else if (st == 0x90u && v)       /* a note: a seed (folded into the keys' range) */
            plant_key(53 + (int)((n + 120u - 53u) % 12u) + (n >= 66u ? 12 : 0));
    }
}

#define HELP_HOLD 900u
#define RESET_HOLD 3000u
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
            plant_key(53 + (int)i);      /* key 0 is F3 */
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (btn >> B_HOME & 1u) {
            char t[8];
            turn(EN_SELECT, P_TEMPO, e);
            ui.btn_used |= 1u << B_HOME;
            itoa_u((uint32_t)proj.par[P_TEMPO], t);
            say("TEMPO", t);
        } else {
            int v = (int)ui.view + (e > 0 ? 1 : -1);
            if (v >= 0 && v < NVIEWS)
                set_view(v);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        set_mode(proj.par[P_MODE] + (e > 0 ? 1 : -1));
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {        /* the ledger, scrolled */
        if (ui.view != V_LEDGER)
            set_view(V_LEDGER);
        else
            knob_set(K_SCROLL, ui.scroll + (e > 0 ? 1 : -1));
        ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && plat_ms() - ui.down_t[B_SAVE] > RESET_HOLD) {
        ui.reset_done = 1;                       /* held: a new garden (saved at once) */
        ui.btn_used |= 1u << B_SAVE;
        ui.help = 0;
        garden_new(plat_ms() * 2654435761u ^ proj.w.rng);
        say("NEW GARDEN", gardener_name());
        autosave_now();
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            turn(EN_K1 + (int)i, VIEW_KNOB[ui.view][i], e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 2500u;
            ui.act_t = ui.turn_t = plat_ms();
        }
    help_tick(btn, keys);
}

/* ------------------------------------------------------------- drawing helpers (AMB-1's) --- */
#define HDR_H 24
#define SCN_Y 24
#define SCN_H 144
#define BOT_Y 168
#define BOT_H 72

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
    cv_rect(x + 1, y + 1, 16, 8, K_BG);
    cv_rect(x + 18, y + 3, 2, 4, c);
    if (b == 4) {
        for (i = 0; i < 7; i++)
            cv_rect(x + BOLT[i][0], y + 1 + i, BOLT[i][1], 1, K_TEXT);
        return;
    }
    for (i = 0; i < b && i < 3; i++)
        cv_rect(x + 2 + i * 5, y + 2, 4, 6, b <= 1 ? K_LOW : K_PAD);
}
static const char *batt_words(int b)
{
    static const char *const Wd[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                      "On USB power"};
    return b >= 0 && b <= 4 ? Wd[b] : 0;
}

/* ------------------------------------------------------------- the scene's colours --- */
/* the flowers: a colour a pitch class, round the wheel from C (red) */
static const uint16_t PITCH_C[12] = {
    RGB(232, 72, 72), RGB(240, 120, 64), RGB(246, 164, 52), RGB(246, 204, 60), RGB(236, 228, 84), RGB(170, 214, 80),
    RGB(96, 196, 120), RGB(64, 186, 176), RGB(76, 164, 226), RGB(96, 120, 232), RGB(156, 100, 226), RGB(220, 90, 190)};
static const uint16_t COAT_C[4] = {RGB(70, 96, 150), RGB(150, 70, 60), RGB(80, 120, 70), RGB(196, 150, 60)};
static int nightk;                       /* 0..10 of 16: how dark the scene is */
static uint16_t sh(uint16_t c) { return mix(c, RGB(18, 22, 52), nightk); }   /* a colour at this hour */

static void sky_colours(uint16_t *top, uint16_t *bot)
{
    float t = (float)proj.w.daysec / (float)G_DAY_S;
    uint16_t a, b;
    int k;
    if (t < 0.08f) {                     /* dawn */
        k = (int)(t / 0.08f * 16.0f);
        a = mix(RGB(40, 46, 96), RGB(150, 196, 236), k);
        b = mix(RGB(250, 190, 160), RGB(214, 234, 242), k);
    } else if (t < 0.62f) {
        a = RGB(150, 196, 236);
        b = RGB(214, 234, 242);
    } else if (t < 0.75f) {              /* dusk */
        k = (int)((t - 0.62f) / 0.13f * 16.0f);
        a = mix(RGB(150, 196, 236), RGB(40, 46, 96), k);
        b = mix(RGB(240, 176, 128), RGB(70, 70, 120), k);
    } else {
        a = RGB(18, 24, 58);
        b = RGB(40, 50, 92);
    }
    switch (proj.w.weather) {
    case W_CLOUD: a = mix(a, RGB(160, 166, 176), 6), b = mix(b, RGB(190, 194, 200), 6); break;
    case W_RAIN: a = mix(a, RGB(96, 108, 124), 10), b = mix(b, RGB(140, 150, 162), 10); break;
    case W_FROST: a = mix(a, RGB(200, 220, 236), 6), b = mix(b, RGB(230, 240, 248), 8); break;
    default: break;
    }
    *top = a;
    *bot = b;
}

/* ------------------------------------------------------------- the scene --- */
#define WALL_Y 58
#define GRASS_Y 72
#define BACK_G 92                         /* the back row's ground */
#define FRONT_G 132
#define PATH_Y 104

static void leaf(int32_t x, int32_t y, int side, int var, uint16_t c)
{
    if (var == 3) {                      /* a fern: little fronds */
        cv_rect(side < 0 ? x - 4 : x + 1, y, 4, 1, c);
        cv_rect(side < 0 ? x - 3 : x + 1, y - 1, 2, 1, c);
        return;
    }
    cv_rect(side < 0 ? x - 3 : x + 1, y, 3, 2, c);
    cv_rect(side < 0 ? x - 2 : x + 1, y - 1, 2, 1, c);
}

static void flower_head(int32_t x, int32_t y, int var, uint16_t c, int big)
{
    uint16_t mid = mix(c, RGB(255, 240, 160), 9);
    switch (var & 3) {
    case 0:                              /* round: a rose, a poppy */
        cv_disc(x, y, 3 + big, sh(c));
        cv_rect(x, y, 1, 1, sh(mid));
        cv_rect(x - 1, y - 1, 1, 1, sh(mix(c, RGB(255, 255, 255), 6)));
        break;
    case 1:                              /* a bell, hanging */
        cv_rect(x, y - 3, 1, 2, sh(RGB(70, 120, 50)));
        cv_rect(x - 2, y - 1, 5, 3 + big, sh(c));
        cv_rect(x - 3, y + 2 + big, 7, 1, sh(c));
        cv_rect(x, y + 3 + big, 1, 1, sh(mid));
        break;
    case 2:                              /* a star: a lily */
        cv_rect(x - 3 - big, y, 7 + 2 * big, 1, sh(c));
        cv_rect(x, y - 3 - big, 1, 7 + 2 * big, sh(c));
        cv_rect(x - 1, y - 1, 3, 3, sh(c));
        cv_rect(x, y, 1, 1, sh(mid));
        break;
    default:                             /* a spike: foxglove, yarrow */
        cv_disc(x, y, 2, sh(c));
        cv_disc(x, y + 4, 2 + big, sh(c));
        cv_rect(x, y - 3, 1, 1, sh(mid));
        break;
    }
}

/* a plant at (x, ground y), up to hmax tall */
static void plant_draw(int i, int32_t x, int32_t gy, int32_t hmax)
{
    const plant_t *p = &proj.w.p[i];
    uint16_t stem = RGB(70, 132, 56), lf = RGB(96, 160, 70), col = PITCH_C[p->pitch % 12];
    int32_t h, k, y;
    float prog = p->life ? (float)p->age / (float)p->life : 0.0f;
    int big = en_plot_glow[i] > 0.55f;
    if (prog > 1.0f)
        prog = 1.0f;
    if (p->health < 45) {                /* poorly: paler */
        stem = mix(stem, RGB(170, 160, 90), 8);
        lf = mix(lf, RGB(180, 170, 100), 8);
    }
    switch (p->stage) {
    case ST_SEED:
        cv_rect(x - 3, gy - 2, 7, 2, sh(RGB(110, 80, 50)));
        cv_rect(x - 2, gy - 3, 5, 1, sh(RGB(110, 80, 50)));
        cv_rect(x, gy - 3, 1, 1, sh(RGB(60, 40, 20)));
        if (p->flags & F_YOURS)          /* yours: a little marker stick */
            cv_rect(x + 4, gy - 8, 1, 7, sh(RGB(190, 160, 110)));
        break;
    case ST_SPROUT:
        h = 3 + (int32_t)(4.0f * prog);
        cv_rect(x, gy - h, 1, h, sh(stem));
        leaf(x, gy - h, -1, p->var, sh(lf));
        leaf(x, gy - h, 1, p->var, sh(lf));
        if (big)
            cv_rect(x, gy - h - 2, 1, 1, sh(col));
        break;
    case ST_GROW: case ST_FLOWER: case ST_SEEDING:
        h = p->stage == ST_GROW ? 8 + (int32_t)((float)(hmax - 14) * prog) : hmax - 4 + (int32_t)(p->plen & 3);
        cv_rect(x, gy - h, 1 + (gy > BACK_G), h, sh(stem));
        for (k = 0, y = gy - 4; y > gy - h + 3; y -= 5, k++)
            leaf(x, y, (k & 1) ? 1 : -1, p->var, sh(lf));
        if (p->stage == ST_GROW && prog > 0.6f)   /* a bud */
            cv_disc(x, gy - h - 1, 1, sh(mix(lf, col, 8)));
        else if (p->stage == ST_FLOWER) {  /* the whole phrase: a head, and side heads for a long one */
            if (p->plen >= 6) {
                int32_t by = gy - h * 3 / 5;
                cv_rect(x - 5, by, 5, 1, sh(stem));
                cv_rect(x - 5, by - 4, 1, 4, sh(stem));
                flower_head(x - 5, by - 6, p->var, mix(col, RGB(255, 255, 255), 3), 0);
            }
            if (p->plen >= 7) {
                int32_t by = gy - h * 2 / 5;
                cv_rect(x + 1, by, 5, 1, sh(stem));
                cv_rect(x + 5, by - 3, 1, 3, sh(stem));
                flower_head(x + 5, by - 5, p->var, mix(col, RGB(0, 0, 0), 3), 0);
            }
            flower_head(x, gy - h - 2, p->var, col, big + (gy > BACK_G));
        } else {                           /* seeding: a pale puff and seeds drifting off */
            int32_t f = (int32_t)(ui.frame / 6u + (uint32_t)i * 7u);
            cv_disc(x, gy - h - 2, 3, sh(RGB(236, 232, 214)));
            cv_rect(x, gy - h - 2, 1, 1, sh(col));
            cv_rect(x + 4 + f % 9, gy - h - 4 - (f % 7), 1, 1, sh(RGB(240, 236, 220)));
            cv_rect(x - 5 - (f + 4) % 8, gy - h - 1 - ((f + 3) % 6), 1, 1, sh(RGB(240, 236, 220)));
        }
        if (p->flags & F_FAV)            /* the gardener's favourite: a ribbon on its stem */
            cv_rect(x - 1, gy - 6, 3, 2, sh(RGB(220, 60, 90)));
        break;
    case ST_WITHER: {
        uint16_t br = RGB(150, 120, 70), bl = RGB(170, 140, 80);
        h = hmax / 2 + 6 - (int32_t)((float)(hmax / 3) * prog);
        cv_rect(x, gy - h, 1, h, sh(br));
        cv_rect(x + 1, gy - h, 3, 1, sh(br));    /* bent over */
        cv_rect(x + 4, gy - h, 1, 3, sh(br));
        cv_rect(x + 3, gy - h + 3, 3, 2, sh(mix(col, br, 10)));
        for (y = gy - 4; y > gy - h + 3; y -= 6)
            cv_rect(x - 3, y + 1, 3, 1, sh(bl));
        break;
    }
    case ST_COMPOST: {
        uint16_t c = RGB(84, 60, 40);
        cv_rect(x - 6, gy - 3, 13, 3, sh(c));
        cv_rect(x - 4, gy - 5, 9, 2, sh(c));
        cv_rect(x - 2, gy - 6, 5, 1, sh(c));
        cv_rect(x - 3, gy - 4, 1, 1, sh(RGB(130, 100, 60)));
        cv_rect(x + 2, gy - 2, 1, 1, sh(RGB(130, 100, 60)));
        if (p->age < p->life / 2) {      /* fresh: a wisp of steam */
            int32_t f = (int32_t)((ui.frame / 8u) % 6u);
            cv_rect(x + (f & 1), gy - 9 - f, 1, 2, sh(RGB(200, 200, 200)));
        }
        break;
    }
    default: break;
    }
}


static void gardener_draw(int32_t x, int32_t feet)
{
    uint16_t coat = COAT_C[proj.w.g.coat & 3], skin = RGB(236, 190, 150), hat = RGB(190, 150, 80);
    int walking = fm_fabsf(gw.tx - gw.gx) > 0.6f, step = (int)(ui.frame / 6u) & 1, m = proj.w.g.mood;
    cv_rect(x - 2, feet - 5, 2, 5 - (walking && step), sh(RGB(60, 50, 40)));      /* legs */
    cv_rect(x + 1, feet - 5, 2, 5 - (walking && !step), sh(RGB(60, 50, 40)));
    cv_rect(x - 3, feet - 13, 7, 8, sh(coat));                                      /* coat */
    cv_rect(x - 4, feet - 12, 1, 5, sh(coat));                                      /* arms */
    cv_rect(x + 4, feet - 12, 1, 5, sh(coat));
    cv_rect(x - 2, feet - 17, 5, 4, sh(skin));                                      /* face */
    cv_rect(x - 1, feet - 16, 1, 1, sh(RGB(40, 30, 20)));
    cv_rect(x + 1, feet - 16, 1, 1, sh(RGB(40, 30, 20)));
    if (m >= 60)
        cv_rect(x - 1, feet - 14, 3, 1, sh(RGB(150, 60, 50)));                     /* a smile */
    else if (m < 35)
        cv_rect(x, feet - 14, 1, 1, sh(RGB(90, 50, 40)));
    cv_rect(x - 4, feet - 18, 9, 1, sh(hat));                                       /* the hat */
    cv_rect(x - 2, feet - 21, 5, 3, sh(hat));
    switch (gw.task) {
    case TK_WATER:                       /* a watering can and drops */
        cv_rect(x + 5, feet - 9, 4, 3, sh(RGB(120, 140, 150)));
        cv_rect(x + 9, feet - 10, 2, 1, sh(RGB(120, 140, 150)));
        if (!walking)
            cv_rect(x + 11 + step, feet - 8 + step * 2, 1, 2, sh(RGB(90, 150, 220)));
        break;
    case TK_PRUNE:
        cv_rect(x + 5, feet - 10, 1, 4, sh(RGB(170, 170, 180)));
        cv_rect(x + 6, feet - 11 + step, 1, 3, sh(RGB(170, 170, 180)));
        break;
    case TK_PLANT:
        cv_rect(x + 5, feet - 9, 3, 4, sh(RGB(200, 170, 110)));
        break;
    case TK_COMPOST:
        cv_rect(x + 5, feet - 14, 1, 12, sh(RGB(140, 110, 70)));
        cv_rect(x + 4, feet - 3, 3, 2, sh(RGB(150, 150, 160)));
        break;
    case TK_ADMIRE:
        if (step)
            cv_rect(x + 1, feet - 24, 1, 2, sh(RGB(220, 60, 90)));
        break;
    default: break;
    }
}

static void plot_pos(int i, int32_t *x, int32_t *gy)
{
    *x = PLOT_X(i) + (i < 6 ? -6 : 6);
    *gy = i < 6 ? BACK_G : FRONT_G;
}

static void flower_top(int i, float *x, float *y)
{
    int32_t px, gy;
    plot_pos(i, &px, &gy);
    *x = (float)px;
    *y = (float)(gy - (i < 6 ? 38 : 48) - 2);
}

static void draw_scene(void)
{
    uint16_t top, bot;
    int32_t y, i;
    float t = (float)proj.w.daysec / (float)G_DAY_S;
    uint32_t f = ui.frame;
    nightk = t >= 0.75f ? 9 : t >= 0.68f ? (int)((t - 0.68f) / 0.07f * 9.0f) : t < 0.05f ? (int)((0.05f - t) / 0.05f * 9.0f) : 0;
    sky_colours(&top, &bot);
    for (y = 0; y < WALL_Y + 4; y += 4)
        cv_rect(0, y, 240, 4, mix(top, bot, (int)(y * 16 / (WALL_Y + 4))));
    if (t < 0.75f) {                     /* the sun, or the moon */
        float u = t / 0.75f, v = 2.0f * u - 1.0f;
        int32_t sx = 12 + (int32_t)(216.0f * u), sy = 10 + (int32_t)(v * v * 34.0f);
        if (proj.w.weather == W_SUN || proj.w.weather == W_FROST)
            cv_disc(sx, sy, 5, mix(RGB(255, 236, 140), RGB(250, 160, 90), t > 0.6f ? 10 : 0));
        else
            cv_disc(sx, sy, 5, mix(top, RGB(250, 246, 220), 6));
    } else {
        float u = (t - 0.75f) / 0.25f, v = 2.0f * u - 1.0f;
        int32_t mx = 20 + (int32_t)(200.0f * u), my = 10 + (int32_t)(v * v * 26.0f);
        for (i = 0; i < 18; i++)         /* stars */
            cv_rect((int32_t)((uint32_t)(i * 97 + 13) % 236u), (int32_t)((uint32_t)(i * 53 + 7) % 50u), 1, 1,
                    ((f / 20u + (uint32_t)i) % 7u) ? RGB(220, 220, 240) : RGB(120, 120, 160));
        cv_disc(mx, my, 5, RGB(236, 236, 210));
        cv_disc(mx + 2, my - 1, 4, top);
    }
    if (proj.w.weather == W_CLOUD || proj.w.weather == W_RAIN) {   /* clouds drift */
        uint16_t cc = proj.w.weather == W_RAIN ? RGB(130, 136, 148) : RGB(238, 240, 244);
        for (i = 0; i < 4; i++) {
            int32_t cx = (int32_t)((f / 10u + (uint32_t)i * 67u) % 300u) - 30, cy = 10 + (i * 13) % 26;
            cv_disc(cx, cy, 6, sh(cc));
            cv_disc(cx + 8, cy - 2, 6, sh(cc));
            cv_disc(cx + 16, cy + 1, 5, sh(cc));
            cv_rect(cx - 4, cy + 2, 24, 4, sh(cc));
        }
    }
    /* the wall, a gate in it */
    cv_rect(0, WALL_Y, 240, GRASS_Y - WALL_Y, sh(RGB(172, 150, 126)));
    for (y = WALL_Y + 4; y < GRASS_Y; y += 5)
        cv_rect(0, y, 240, 1, sh(RGB(140, 120, 100)));
    for (y = WALL_Y; y < GRASS_Y; y += 5)
        for (i = ((y / 5) & 1) * 8; i < 240; i += 16)
            cv_rect(i, y, 1, 4, sh(RGB(140, 120, 100)));
    cv_rect(0, WALL_Y - 2, 240, 2, sh(RGB(120, 150, 90)));   /* moss on top */
    cv_rect(110, WALL_Y - 4, 20, GRASS_Y - WALL_Y + 4, sh(RGB(120, 84, 52)));
    for (i = 112; i < 130; i += 4)
        cv_rect(i, WALL_Y - 2, 1, GRASS_Y - WALL_Y + 2, sh(RGB(90, 62, 38)));
    /* grass, the beds, the path */
    cv_rect(0, GRASS_Y, 240, SCN_H - GRASS_Y, sh(RGB(122, 168, 88)));
    for (i = 0; i < 60; i++)             /* tufts */
        cv_rect((int32_t)((uint32_t)(i * 37 + 5) % 240u), GRASS_Y + 2 + (int32_t)((uint32_t)(i * 29) % 70u), 1, 2,
                sh(RGB(100, 146, 70)));
    cv_rect(0, PATH_Y, 240, 12, sh(RGB(206, 190, 150)));
    cv_rect(0, PATH_Y, 240, 1, sh(RGB(180, 164, 124)));
    cv_rect(4, BACK_G - 2, 112, 6, sh(RGB(110, 80, 54)));
    cv_rect(124, BACK_G - 2, 112, 6, sh(RGB(110, 80, 54)));
    cv_rect(4, FRONT_G - 2, 112, 7, sh(RGB(104, 74, 50)));
    cv_rect(124, FRONT_G - 2, 112, 7, sh(RGB(104, 74, 50)));
    if (proj.w.weather == W_FROST)       /* rime on the grass */
        for (i = 0; i < 70; i++)
            cv_rect((int32_t)((uint32_t)(i * 41 + 3) % 240u), GRASS_Y + (int32_t)((uint32_t)(i * 23) % 72u), 1, 1,
                    RGB(236, 244, 250));
    for (i = 0; i < 6; i++) {            /* the back row */
        int32_t x, gy;
        plot_pos(i, &x, &gy);
        plant_draw(i, x, gy, 40);
    }
    gardener_draw((int32_t)gw.gx, PATH_Y + 10);
    if (gw.rabbit) {                     /* in from the side, a nibble, away again */
        int32_t px, gy, rx, hop;
        uint32_t rt = gw.rabbit_t;
        int from_left = gw.rabbit_plot % 2 == 0;
        float u;
        plot_pos(gw.rabbit_plot, &px, &gy);
        px -= 8;
        if (rt < 3000u)
            u = (float)rt / 3000.0f;
        else if (rt < 5000u)
            u = 1.0f;
        else
            u = 1.0f - (float)(rt - 5000u) / 3000.0f;
        rx = from_left ? (int32_t)(-10.0f + (float)(px + 10) * u) : (int32_t)(250.0f - (float)(250 - px) * u);
        hop = (u > 0.0f && u < 1.0f && ((rt / 250u) & 1u)) ? 2 : 0;
        gy += 2 - hop;
        cv_rect(rx - 4, gy - 4, 8, 4, sh(RGB(150, 130, 110)));
        cv_rect(rx + (from_left ? 3 : -6), gy - 6, 3, 3, sh(RGB(150, 130, 110)));
        cv_rect(rx + (from_left ? 4 : -5), gy - 9, 1, 3, sh(RGB(150, 130, 110)));
        cv_rect(rx + (from_left ? 5 : -4), gy - 9, 1, 3, sh(RGB(170, 150, 130)));
        cv_rect(rx + (from_left ? -5 : 4), gy - 4, 2, 2, sh(RGB(240, 240, 240)));
    }
    for (i = 6; i < G_NPLOT; i++) {      /* the front row */
        int32_t x, gy;
        plot_pos(i, &x, &gy);
        plant_draw(i, x, gy, 50);
    }
    if (gw.bee) {                        /* from one flower to the other, wobbling */
        float ax, ay, bx, by, u = (float)gw.bee_t / 5000.0f, x, yy;
        flower_top(gw.bee_a, &ax, &ay);
        flower_top(gw.bee_b, &bx, &by);
        u = u > 1.0f ? 1.0f : u;
        x = ax + (bx - ax) * u + fm_sinf((float)gw.bee_t * 0.006f) * 3.0f;
        yy = ay + (by - ay) * u - fm_sinf(u * FM_PI) * 18.0f + fm_sinf((float)gw.bee_t * 0.011f) * 2.0f - 4.0f;
        cv_rect((int32_t)x - 2, (int32_t)yy, 5, 3, RGB(240, 200, 40));
        cv_rect((int32_t)x, (int32_t)yy, 1, 3, RGB(40, 30, 10));
        cv_rect((int32_t)x - 1 + (int32_t)((f / 2u) & 1u), (int32_t)yy - 2, 2, 2, RGB(236, 244, 250));
    }
    if (proj.w.weather == W_RAIN)        /* rain */
        for (i = 0; i < 44; i++) {
            int32_t rx = (int32_t)((uint32_t)(i * 53) % 250u), ry = (int32_t)((f * 5u + (uint32_t)i * 37u) % 150u) - 6;
            cv_rect(rx - ry / 8, ry, 1, 4, RGB(150, 176, 210));
        }
}

/* ------------------------------------------------------------- the bands --- */
static const char *weather_word(void) { return WEATHER_NAME[proj.w.weather % W_N]; }

static void weather_icon(int32_t x, int32_t y)
{
    switch (proj.w.weather) {
    case W_SUN:
        cv_disc(x + 6, y + 6, 4, RGB(236, 170, 40));
        break;
    case W_CLOUD:
        cv_disc(x + 4, y + 7, 3, K_DIM);
        cv_disc(x + 9, y + 6, 4, K_DIM);
        break;
    case W_RAIN:
        cv_disc(x + 4, y + 5, 3, K_FROST);
        cv_disc(x + 9, y + 4, 4, K_FROST);
        cv_rect(x + 3, y + 9, 1, 3, K_FROST);
        cv_rect(x + 7, y + 10, 1, 3, K_FROST);
        cv_rect(x + 11, y + 9, 1, 3, K_FROST);
        break;
    default:
        cv_rect(x + 6, y + 1, 1, 11, K_FROST);
        cv_rect(x + 1, y + 6, 11, 1, K_FROST);
        cv_rect(x + 3, y + 3, 7, 7, K_BG);
        cv_disc(x + 6, y + 6, 2, K_FROST);
        break;
    }
}

static void draw_header(void)
{
    char t[24];
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(2166136261u, proj.w.g.name | (uint32_t)proj.w.g.mood << 8 | (uint32_t)proj.w.weather << 16 |
                                            (uint32_t)en_playing << 20 | (uint32_t)ui.dirty << 21 | (uint32_t)msg << 22 |
                                            (uint32_t)en_frozen << 23 | (uint32_t)(batt() + 1) << 24),
                      proj.w.day);
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
        int32_t x = 234;
        if (batt() >= 0) {
            x -= BATT_W;
            draw_batt(x, 7, batt());
            x -= 6;
        }
        if (en_frozen) {
            x -= text_w(&FONT_XS, "held");
            cv_text(x, 5, &FONT_XS, "held", K_FROST);
            x -= 6;
        } else if (!en_playing) {        /* resting: a play triangle */
            int32_t j;
            x -= 8;
            for (j = 0; j < 5; j++)
                cv_rect(x + j, 7 + j, 1, 11 - 2 * j, K_DIM);
            x -= 6;
        }
        cat_s(cat_s(t, "Day "), "");
        itoa_u(proj.w.day, t + 4);
        x -= text_w(&FONT_XS, t);
        cv_text(x, 5, &FONT_XS, t, K_DIM);
        x -= 18;
        weather_icon(x, 5);
        x = cv_text(8, 3, &FONT_B, gardener_name(), K_TEXT);
        cv_text(x + 5, 5, &FONT_XS, mood_word(), proj.w.g.mood < 35 ? K_LOW : K_ACC);
        if (ui.dirty)
            cv_round(2, 10, 4, 4, 2, K_DIM);
    }
    cv_blit(0, 0);
}

/* help cards */
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("THE GARDEN"); L("TEMPO, GROWTH, BELLS, PAD"); L("+ SELECT: TEMPO"); break;
    case B_ENV: L("SOUND"); L("BASS, ECHO, REVERB, COLOUR"); break;
    case B_GLO: L("SETUP"); L("ROOT, TUNE, MIDI OUT, LIGHTS"); break;
    case B_SEQ: L("THE LEDGER"); L("EVERY PLANT AND ITS PARENTS"); L("KNOB 1 OR PRESETS: SCROLL"); break;
    case B_LFO: L("THE NEXT PRESET"); L("THE GARDEN'S SOUND"); L("THE PLANTS STAY AS THEY ARE"); L("SEL: THE LAST ONE"); break;
    case B_SEL: L("THE LAST PRESET"); L("THE GARDEN'S SOUND"); L("LFO: THE NEXT ONE"); break;
    case B_EDIT: L("THE GARDENER WATERS"); L(" EVERYTHING"); break;
    case B_ARP: L("A SEED AT A RANDOM NOTE"); break;
    case B_REC: L("FREEZE: TIME STOPS,"); L(" THE REVERB RINGS ON"); L("AGAIN: THAW"); break;
    case B_OCTDN: L("THE BELLS AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE BELLS AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES THE GARDEN"); L("KEEP HOLDING 3 S:"); L(" A NEW GARDEN"); break;
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

static void draw_ledger(void);
static void draw_main(void)
{
    if (ui.help) {
        uint32_t h = hash(0x48454C50u, ui.help);
        if (h != ui.sig[1]) {
            ui.sig[1] = h;
            cv_begin(240, SCN_H, K_BG);
            draw_help();
            cv_blit(0, SCN_Y);
        }
        return;
    }
    if (ui.view == V_LEDGER) {
        draw_ledger();
        return;
    }
    if (ui.frame % 4u)                   /* 15 frames a second */
        return;
    ui.sig[1] = 0;
    cv_begin(240, SCN_H, K_BG);
    draw_scene();
    cv_blit(0, SCN_Y);
}

static const felucca_font_t *fit(const char *s, int32_t w)
{
    return text_w(&FONT_XS, s) <= w ? &FONT_XS : &FONT_T;
}

static void draw_events(void)            /* the newest event bright, two before it fainter, a summary */
{
    char t[48];
    int k;
    cv_begin(240, BOT_H, K_PANEL);
    cv_rect(0, 0, 240, 1, K_LINE);
    for (k = 0; k < G_NLOG; k++) {
        const char *s = proj.w.log[k];
        uint16_t c = k == 0 ? (plat_ms() - ui.log_t < 4000u ? K_ACC : K_TEXT) : mix(K_DIM, K_PANEL, k * 4);
        cv_text(6, 3 + k * 16, fit(s, 230), s, c);
    }
    {
        int live = garden_living(), fl = garden_count(ST_FLOWER);
        char *e = t;
        itoa_u((uint32_t)live, e);
        e = cat_s(e + (live >= 10 ? 2 : 1), live == 1 ? " plant, " : " plants, ");
        itoa_u((uint32_t)fl, e);
        e = cat_s(e + (fl >= 10 ? 2 : 1), " in flower");
        cv_rect(6, 52, 228, 1, K_LINE);
        cv_text(6, 54, &FONT_XS, t, K_DIM);
        cv_text(234 - text_w(&FONT_XS, GROWTH_NAME[proj.growth % G_NGROWTH]), 54, &FONT_XS,
                GROWTH_NAME[proj.growth % G_NGROWTH], K_DIM);
    }
    cv_blit(0, BOT_Y);
}

static void draw_knob_strip(void)
{
    int now_touch = plat_ms() < ui.touch_until, i;
    cv_begin(240, BOT_H, K_BG);
    {
        char t[40];
        int k;
        int32_t xd = 237 - NVIEWS * 6, xt;
        caps(t, VIEW_NAME[ui.view], 12);
        if (ui.view == V_SOUND || ui.view == V_GARDEN) {   /* and the preset (1.1) */
            char q[24], *e = t;
            caps(q, preset_name(proj.preset), sizeof q);
            while (*e)
                e++;
            cat_s(cat_s(e, ": "), q);
        }
        xt = xd - 5 - text_w(&FONT_XS, t);
        cv_rect(4, 7, xt - 9, 1, K_LINE);
        cv_text(xt, 1, &FONT_XS, t, K_ACC);
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 6, 5, 4, 4, 2, k == ui.view ? K_ACC : K_LINE);
    }
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v, hot = now_touch && ui.touched == i;
        int32_t x = i * 60, cx = x + 30;
        char t[16];
        uint16_t bg = hot ? K_PANEL : K_BG, fill = hot ? K_ACC : K_PAD;
        if (k == K_NONE)
            continue;
        if (i)
            cv_rect(x, 20, 1, 46, K_LINE);
        if (hot)
            cv_round(x + 3, 14, 54, 57, 6, K_PANEL);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        caps(t, KNOB_LABEL[ui.view][i], sizeof t);
        text_c(cx, 15, &FONT_XS, t, hot ? K_TEXT : K_DIM);
        if (hi - lo < 6) {
            int n = hi - lo + 1, j, pw = n > 4 ? 6 : 9, gap = 3, w0 = n * (pw + gap) - gap;
            for (j = 0; j < n; j++)
                cv_round(cx - w0 / 2 + j * (pw + gap), 36, pw, 8, 2, j == v - lo ? fill : K_LINE);
        } else {
            arc(cx, 40, 12, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, K_LINE, fill, bg, lo < 0);
        }
        knob_text(k, t);
        text_c(cx, 53, &FONT_B, t, hot ? K_ACC : K_TEXT);
    }
    cv_blit(0, BOT_Y);
}

static int strip_shown(void)
{
    return ui.view == V_SOUND || ui.view == V_SETUP || (ui.view == V_GARDEN && plat_ms() < ui.touch_until);
}

static void draw_bottom(void)
{
    uint32_t h, i;
    if (ui.view == V_LEDGER)
        return;
    if (garden_log_seq != ui.log_seen) {
        ui.log_seen = garden_log_seq;
        ui.log_t = plat_ms();
    }
    if (strip_shown()) {
        h = hash(hash(2166136261u, ui.view | (uint32_t)ui.touched << 8 | 0x100000u), plat_ms() < ui.touch_until);
        for (i = 0; i < 4; i++)
            h = hash(h, (uint32_t)knob_get(VIEW_KNOB[ui.view][i]));
        h = hash(h, proj.preset);                /* (named on the rule; it moves the Root shown) */
        if (h != ui.sig[2]) {
            ui.sig[2] = h;
            draw_knob_strip();
        }
        return;
    }
    h = hash(hash(hash(2166136261u, garden_log_seq), (uint32_t)garden_living() | (uint32_t)garden_count(ST_FLOWER) << 8 |
                                                         (uint32_t)proj.growth << 16),
             plat_ms() - ui.log_t < 4000u);
    if (h != ui.sig[2]) {
        ui.sig[2] = h;
        draw_events();
    }
}

static const char *lineage(const plant_t *p, char *b)
{
    char x[24];
    char *e = b;
    b[0] = 0;
    if (p->pa_var != NO_PARENT && p->pb_var != NO_PARENT) {
        plant_t a;
        a.adj = p->pa_adj, a.var = p->pa_var;
        e = cat_s(e, plant_name(&a, x));
        e = cat_s(e, " x ");
        a.adj = p->pb_adj, a.var = p->pb_var;
        cat_s(e, plant_name(&a, x));
    } else if (p->pa_var != NO_PARENT) {
        plant_t a;
        a.adj = p->pa_adj, a.var = p->pa_var;
        e = cat_s(e, "seed of ");
        cat_s(e, plant_name(&a, x));
    } else {
        cat_s(e, p->flags & F_YOURS ? "planted by you" : p->flags & F_KEPT ? "the gardener's" : "blew in, wild");
    }
    return b;
}

static void ledger_rows(int first)       /* the ledger: the plants, two lines each; 3 to a band */
{
    int r, i, n = 0;
    for (i = 0; i < G_NPLOT && n < first + 6; i++) {
        const plant_t *p = &proj.w.p[i];
        char b[56], nm[24];
        int32_t y;
        if (p->stage == ST_EMPTY)
            continue;
        r = n++ - first;
        if (r < 0)
            continue;
        y = 2 + r * 35;
        cv_disc(9, y + 8, 4, PITCH_C[p->pitch % 12]);
        plant_name(p, nm);
        cv_text(18, y, &FONT_XS, nm, K_TEXT);
        if (p->flags & F_FAV)
            cv_text(20 + text_w(&FONT_XS, nm), y, &FONT_XS, "*", K_LOW);
        cv_text(234 - text_w(&FONT_XS, STAGE_NAME[p->stage]), y, &FONT_XS, STAGE_NAME[p->stage],
                p->stage == ST_FLOWER ? K_ACC : K_DIM);
        lineage(p, b);
        {
            char *e = b;
            while (*e)
                e++;
            e = cat_s(e, "  ");
            e = cat_s(e, EN_NOTE_NAME[p->pitch % 12]);
            itoa_u((uint32_t)(p->pitch / 12 - 1), e);
        }
        cv_text(18, y + 16, &FONT_T, b, K_DIM);
        cv_rect(18, y + 32, 216, 1, K_LINE);
    }
}

static void draw_ledger(void)
{
    uint32_t h = hash(2166136261u, 0x4C454447u | ui.scroll), i;
    int total = 0, first;
    for (i = 0; i < G_NPLOT; i++) {
        const plant_t *p = &proj.w.p[i];
        h = hash(h, p->stage | (uint32_t)p->adj << 4 | (uint32_t)p->var << 8 | (uint32_t)p->flags << 12 |
                        (uint32_t)p->pitch << 20);
        total += p->stage != ST_EMPTY;
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    first = ui.scroll;
    if (first > total - 6)
        first = total - 6;
    if (first < 0)
        first = 0;
    cv_begin(240, 108, K_PANEL);
    if (!total)
        text_c(120, 40, &FONT_S, "Nothing growing yet", K_DIM);
    ledger_rows(first);
    cv_blit(0, SCN_Y);
    cv_begin(240, 108, K_PANEL);
    cv_oy = -105;                        /* the next three rows: drawn as one tall page, its lower half */
    cv_y0 = 0;
    cv_y1 = 108;
    ledger_rows(first);
    cv_oy = 0;
    cv_blit(0, SCN_Y + 108);
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

/* SAVING, and the flash's wear: every save erases one of the same two 4 KiB sectors (APP_STORE_MAP's A/B pair), rated
 * about 100k erases, so the garden saves itself rarely. keep_due is the whole policy:
 *   the world (a planting, a death, a hybrid, a new day: garden_changed; the plants aging while it grows) at most once
 *     an hour after the last save; nothing changed, no save. Up to an hour of the garden is lost at a power-off (the
 *     FM-1 gives no warning);
 *   a setting (ui.dirty) a few seconds after the knobs rest, at most once every 5 minutes;
 *   on the battery, once it reads empty (0): one save of whatever changed, then none of these until it reads above 0;
 *   SAVE, and SAVE held (a new garden), save at once (autosave_now).
 * Growing or resting the same, always at a quiet moment (growing: no bell for half a second; resting: the pad gone).
 * Each save resets the clock: it keeps the settings and the world alike. */
#define AUTOSAVE_QUIET 4000u
#define KEEP_WORLD_MS (60u * 60000u)
#define KEEP_SETTINGS_MS (5u * 60000u)
static uint32_t saves;                   /* (tests) */
static uint8_t keep_grown;               /* the plants aged (the garden grew) since the last save */
static uint8_t keep_low;                 /* the empty battery's save done; again once it reads above 0 */
static void autosave_now(void)
{
    if (project_save() == 0) {
        ui.dirty = 0;
        garden_changed = 0;
        keep_grown = 0;
        ui.saved_t = plat_ms();
        saves++;
    }
}
static int keep_due(uint32_t now)        /* 0 not yet, 1 due, 2 due: the empty battery's save */
{
    uint32_t since = now - ui.saved_t;
    if (!ui.dirty && !garden_changed && !keep_grown)
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
    if (en_playing && !en_frozen)
        keep_grown = 1;
    if (ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_QUIET)
        return;
    if (en_playing ? en_bell_age < 22050u : en_pad_level >= 0.01f)
        return;
    if (!(due = keep_due(now)))
        return;
    autosave_now();
    ui.saved_t = now;                    /* (a failed write: tried again in an hour, not every pass) */
    if (due == 2)
        keep_low = 1;
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader on the computer (announce.c): a page once it settles, a knob's value as
 * it turns, a help card, each new event line, what a button did. WHERE: the page and the header, then the garden. */
static struct {
    uint8_t view, help, started;
    int16_t val[4];
    uint32_t log;
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
        if (VIEW_KNOB[ui.view][i] == K_NONE)
            continue;
        sp_cat(i ? ", " : ": ");
        sp_knob(ui.view, i);
    }
    if (!all)
        return;
    sp_cat(". ");
    sp_cat(gardener_name());
    sp_cat(", ");
    sp_cat(mood_word());
    sp_cat(". Day ");
    sp_int(proj.w.day);
    sp_cat(", ");
    sp_cat(weather_word());
    sp_cat(sp_tp.playing ? ", growing" : ", resting");
    sp_cat(". Preset ");
    sp_cat(preset_name(proj.preset));
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static void sp_garden(void)
{
    int live = garden_living(), fl = garden_count(ST_FLOWER);
    sp_n = 0;
    sp_buf[0] = 0;
    sp_int((uint32_t)live);
    sp_cat(live == 1 ? " plant, " : " plants, ");
    sp_int((uint32_t)fl);
    sp_cat(" in flower. Scale ");
    sp_cat(EN_NOTE_NAME[g_root()]);
    sp_cat(" ");
    sp_cat(mode_name());
    if (proj.w.log[0][0]) {
        sp_cat(". ");
        sp_cat(proj.w.log[0]);
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
static void sp_snap(void)
{
    int i;
    for (i = 0; i < 4; i++)
        sp.val[i] = (int16_t)knob_get(VIEW_KNOB[ui.view][i]);
    sp.view = ui.view;
    sp.help = ui.help;
    sp.log = garden_log_seq;
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
            sp_garden();
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
        ann_say(en_playing ? "GROWING" : "RESTING", 0, 0);
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
    if (garden_log_seq != sp.log) {      /* a new event: queued after what is being said */
        sp.log = garden_log_seq;
        ann_say(proj.w.log[0], 0, 0);
    }
}

#ifdef PLAT_HOST
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    uint8_t keep_view = ui.view, keep_help = ui.help;
    int v, i;
    for (v = 0; v < NVIEWS; v++) {
        ui.view = (uint8_t)v;
        sp_page(1);
        sp_print("SCREEN", sp_buf);
    }
    ui.view = keep_view;
    sp_garden();
    sp_print("WHERE", sp_buf);
    for (i = 0; i < NB; i++) {
        const char *l[HELP_LINES];
        if (!help_lines(i, l))
            continue;
        ui.help = (uint8_t)(i + 1);
        sp_help();
        sp_print("HELP", sp_buf);
    }
    ui.help = keep_help;
    for (i = 0; i < G_NLOG; i++)
        sp_print("EVENT", proj.w.log[i]);
    for (i = 0; i < NPRESETS; i++) {              /* LFO / SEL: each preset named */
        char t[48];
        msg_text("PRESET", preset_name(i), t);
        sp_print("MSG", t);
    }
}
#endif

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.prev_ms = ui.saved_t = plat_ms();
    ui.log_seen = garden_log_seq;
    lcd_fill(0, 0, 240, 240, K_BG);
    if (!en_playing)
        en_play(1);                      /* it grows from the start */
    sp_tp.playing = 1;
    sp_tp.play_t = plat_ms();
}

void ui_frame(void)
{
    uint32_t now = plat_ms(), dt = now - ui.prev_ms;
    ui.prev_ms = now;
    input();
    if (en_playing && !en_frozen)
        garden_tick_ms(dt > 1000u ? 1000u : dt);
    garden_frame();
    autosave();
    draw_header();
    draw_main();
    draw_bottom();
    leds();
    speak();
    ann_tick(now);
    ui.frame++;
}

void ui_input_only(void) { input(); }
