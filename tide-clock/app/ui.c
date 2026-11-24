/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Rewritten for TIDE CLOCK by ashtonwallen (the scene, the log, the clock); AMB-1's helpers kept. */
/* TIDE CLOCK UI: the panel and the screen. Part of the unity build (after gfx.c, project.c and world.c); the host
 * simulator includes it too.
 *
 *   PLAY           the music plays / rests (the world goes on either way); while setting the clock: done
 *   EDIT           set the clock (any knob: 15 minutes a detent; PLAY, EDIT or HOME: done; 20 s alone: done)
 *   HOME           the coast        SEQ   Abel's log (KNOB 1 or ALGORITHM scrolls)        GLO   setup
 *   SELECT         the pages, in turn
 *   PRESETS        the sound's palette (1.1: Harbour, Fjord ...); the world goes on as it was
 *   KNOB 1-4       on setup: Mode (Clock / Cycle), Cycle (a day every 1..12 h), Sea (its level), Lights
 *   SAVE           saves now (the world saves by itself: settings a few seconds after a change, the world after
 *                  something notable, every 10 minutes at most, and the time every 15 minutes)
 * Hold a button for a moment: a card says what it does. In Clock mode the clock is asked for at power-on, starting
 * at the time last saved. */
#include "fastmath.h"

enum { V_SCENE, V_LOG, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Coast", "Log", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_SEQ, B_GLO};
static const char *const SETUP_LABEL[4] = {"Mode", "Cycle", "Sea", "Lights"};
#define SET_IDLE 20000u                  /* the clock screen confirms itself after this long untouched */
#define SAVE_SETTINGS_MS 5000u           /* a setting: kept once the knobs rest this long ... */
#define KEEP_SETTINGS_MS (5u * 60000u)   /* ... and this long after the last save at least */
#define KEEP_WORLD_MS (60u * 60000u)     /* the world and the time: once an hour at most (keep_due) */

static struct {
    uint8_t view;
    uint32_t btn, btn_used;
    uint32_t down_t[NB];
    uint32_t turn_t;
    uint8_t help;                      /* a button's card is up: the button + 1 */
    uint8_t setting;                   /* the clock screen is up */
    uint8_t set_boot;                  /* ... as asked at power-on */
    uint32_t set_t;                    /* its last touch */
    uint32_t boot_min;                 /* the minute saved, at power-on */
    uint8_t dirty;                     /* settings changed since the last save */
    uint32_t dirty_t, saved_t, due_t;
    uint8_t log_off;                   /* the log page's scroll */
    char msg[2][24];
    uint32_t msg_until;
    uint32_t frame, draw_t;
    uint32_t sig;
} ui;

/* ---------------------------------------------------------- messages --- */
static uint8_t sp_said;                /* a message went to the screen reader this frame */
static void msg_text(const char *a, const char *b, char *t)
{
    uint32_t i, k = 0;
    for (i = 0; i < 23u && a && a[i]; i++)
        t[k++] = a[i];
    if (b && b[0])
        t[k++] = ' ';
    for (i = 0; i < 23u && b && b[i]; i++)
        t[k++] = b[i];
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
void ui_say(const char *a, const char *b) { say(a, b); }
int ui_dirty(void) { return ui.dirty != 0; }

static void hhmm(uint32_t min, char *b)        /* "06:45" */
{
    min %= 1440u;
    b[0] = (char)('0' + min / 600u);
    b[1] = (char)('0' + min / 60u % 10u);
    b[2] = ':';
    b[3] = (char)('0' + min % 60u / 10u);
    b[4] = (char)('0' + min % 10u);
    b[5] = 0;
}

/* ------------------------------------------------------------ setup --- */
static int setup_get(int i)
{
    return i == 0 ? proj.clock : i == 1 ? proj.cycle : i == 2 ? proj.sea : proj.leds;
}
static int setup_hi(int i) { return i == 0 ? 1 : i == 1 ? NCYCLE - 1 : i == 2 ? 10 : 2; }
static void setup_text(int i, int v, char *b)
{
    b[0] = 0;
    if (i == 0)
        cat_s(b, v ? "Cycle" : "Clock");
    else if (i == 1)
        cat_s(cat_u(cat_s(b, "1 day / "), CYCLE_H[v]), " h");
    else if (i == 2)
        cat_u(b, (uint32_t)v);
    else
        cat_s(b, v == 2 ? "Keys" : v ? "On" : "Off");
}
static void setup_set(int i, int v)
{
    int hi = setup_hi(i);
    v = v < 0 ? 0 : v > hi ? hi : v;
    if (v == setup_get(i))
        return;
    if (i == 0)
        proj.clock = (uint8_t)v;
    else if (i == 1)
        proj.cycle = (uint8_t)v;
    else if (i == 2)
        proj.sea = (uint8_t)v;
    else
        proj.leds = (uint8_t)v;
    ui.dirty = 1;
    ui.dirty_t = plat_ms();
}

/* ------------------------------------------------------- the clock --- */
static void set_open(int boot)
{
    ui.setting = 1;
    ui.set_boot = (uint8_t)boot;
    ui.set_t = plat_ms();
    ui.help = 0;
}
static void set_done(void)
{
    char t[8];
    if (!ui.setting)
        return;
    ui.setting = 0;
    /* at power-on, a time well before the one saved: the FM-1 was off overnight, a new day */
    if (ui.set_boot && world_min() + 60u < ui.boot_min) {
        new_day();
        log_add(EV_NEWDAY, 0);
    }
    ui.set_boot = 0;
    hhmm(world_min(), t);
    say("TIME SET", t);
    ui.dirty = 1;                                /* kept soon, as a setting */
    ui.dirty_t = plat_ms();
}
static void set_turn(int32_t e)
{
    uint32_t m = world_min() / 15u * 15u;
    int32_t k = (int32_t)(m / 15u) + e;
    k = ((k % 96) + 96) % 96;
    world_set_min((uint32_t)k * 15u);
    ui.set_t = plat_ms();
}

/* ------------------------------------------------------------ buttons --- */
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.log_off = 0;
}

static void preset_turn(int32_t e)             /* PRESETS: the next palette (or the last), named on the screen */
{
    preset_set((int)proj.preset + (e > 0 ? 1 : -1));
    ui.dirty = 1;                                /* kept as a setting */
    ui.dirty_t = plat_ms();
    if (ui.view == V_LOG)
        set_view(V_SCENE);                       /* (its name shows on the coast) */
    say("PRESET", preset_name(proj.preset));
    ui.msg_until = plat_ms() + 2500u;
}

static void play_toggle(void)
{
    if (en_playing) {
        en_play(0);
        say("RESTING", 0);
    } else {
        en_seed(plat_ms());
        en_play(1);
        say("PLAYING", 0);
    }
}

static void button(int b)
{
    int v;
    if (ui.setting) {
        if (b == B_EDIT || b == B_HOME || b == B_PLAY)
            set_done();
        return;
    }
    for (v = 0; v < NVIEWS; v++)
        if (VIEW_BTN[v] == b) {
            set_view(v);
            return;
        }
    switch (b) {
    case B_PLAY: play_toggle(); break;
    case B_EDIT: set_open(0); break;
    case B_SAVE:
        proj.tod = w.ms / 1000u;
        if (project_save() == 0) {
            ui.dirty = 0;
            w.want_save = 0;
            ui.saved_t = plat_ms();
            ui.due_t = 0;
            say("SAVED", 0);
        } else {
            say_as("NOT SAVED", "FLASH ERROR", ANN_NOW | ANN_KEEP);
        }
        break;
    default: break;
    }
}

/* MIDI in: a clock and start / stop drive the grid, as AMB-1 */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0)) {
        uint32_t st = (pkt >> 8) & 0xFFu;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)
            en_clock(st == 0xF8u ? EN_CLK_TICK : st == 0xFAu ? EN_CLK_START : st == 0xFBu ? EN_CLK_CONTINUE : EN_CLK_STOP);
    }
}

/* HELP CARDS (X0X's): hold one button alone and a card says what it does */
#define HELP_HOLD 900u
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("THE COAST"); L("THE TIME, THE DAY, THE KEEPER"); L("PRESETS: THE SOUND"); break;
    case B_SEQ: L("ABEL'S LOG"); L("KNOB 1 SCROLLS"); break;
    case B_GLO: L("SETUP"); L("MODE, CYCLE, SEA, LIGHTS"); L("PRESETS: THE SOUND"); break;
    case B_EDIT: L("SET THE CLOCK"); L("ANY KNOB: 15 MINUTES"); L("PLAY: DONE"); break;
    case B_SAVE: L("SAVES THE WORLD NOW"); L("IT ALSO SAVES BY ITSELF"); break;
    case B_OCTDN: case B_OCTUP: L("WITH OCT- AND OCT+"); L("HELD 5 S: UPDATE MODE"); break;
    default: break;
    }
#undef L
    return n;
}
static void help_tick(uint32_t btn)
{
    uint32_t now = plat_ms();
    const char *l[HELP_LINES];
    int i;
    if (ui.help) {
        if (btn != 1u << (ui.help - 1) || (int32_t)(ui.turn_t - ui.down_t[ui.help - 1]) > 0)
            ui.help = 0;
        return;
    }
    if (!btn || (btn & (btn - 1u)) || ui.setting)
        return;
    for (i = 0; i < NB; i++)
        if (btn == 1u << i && i != B_PLAY && !(ui.btn_used & btn) && now - ui.down_t[i] >= HELP_HOLD &&
            (int32_t)(ui.turn_t - ui.down_t[i]) <= 0 && help_lines(i, l)) {
            ui.help = (uint8_t)(i + 1);
            ui.btn_used |= btn;
        }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), ch, i;
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
            if (i == B_PLAY)                     /* on the press */
                button((int)i);
        } else if (!(ui.btn_used & m) && i != B_PLAY) {
            button((int)i);
        }
    }
    (void)plat_keys();
    midi_in();
    for (i = 0; i < NE; i++) {
        if ((e = plat_enc((int)i)) == 0)
            continue;
        ui.turn_t = plat_ms();
        if (ui.setting) {                        /* any knob: the clock, 15 minutes a detent */
            set_turn(e);
            continue;
        }
        if (i == EN_PRESET) {
            preset_turn(e);
        } else if (i == EN_SELECT) {
            int v = (int)ui.view + (e > 0 ? 1 : -1);
            set_view((v + NVIEWS) % NVIEWS);
        } else if (ui.view == V_LOG && (i == EN_K1 || i == EN_ALGO)) {
            int o = (int)ui.log_off + (e > 0 ? 1 : -1), hi = proj.log_n > 8 ? proj.log_n - 8 : 0;
            ui.log_off = (uint8_t)(o < 0 ? 0 : o > hi ? hi : o);
        } else if (ui.view == V_SETUP && i >= EN_K1) {
            int k = (int)i - EN_K1;
            setup_set(k, setup_get(k) + (int)e);
        }
    }
    if (ui.setting && plat_ms() - ui.set_t >= SET_IDLE)
        set_done();
    help_tick(btn);
}

/* ------------------------------------------------------------- drawing --- */
static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static void cv_round(int32_t x, int32_t y, int32_t w_, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w_, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w_ - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w_ - 2 * d, 1, c);
    }
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}
static void text_r(int32_t rx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(rx - text_w(f, s), y, f, s, c);
}

/* a pixel blended over the canvas (a: 0..16) */
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
static void blend_rect(int32_t x, int32_t y, int32_t w_, int32_t h, uint16_t c, uint32_t a)
{
    int32_t i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w_; i++)
            px_blend(i, j, c, a);
}
/* an anti-aliased disc */
static void disc(float cx, float cy, float r, uint16_t c)
{
    int32_t x0 = (int32_t)fm_floorf(cx - r - 1.0f), x1 = (int32_t)(cx + r + 1.0f);
    int32_t y0 = (int32_t)fm_floorf(cy - r - 1.0f), y1 = (int32_t)(cy + r + 1.0f), x, y;
    if (y1 + cv_oy < cv_y0 || y0 + cv_oy >= cv_y1)
        return;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy);
            px_blend(x, y, c, (uint32_t)(fm_clampf(r - d + 0.5f, 0.0f, 1.0f) * 16.0f + 0.5f));
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
static uint16_t rgbf(float r, float g, float b)
{
    int ri = (int)fm_clampf(r, 0.0f, 255.0f), gi = (int)fm_clampf(g, 0.0f, 255.0f), bi = (int)fm_clampf(b, 0.0f, 255.0f);
    return (uint16_t)(((ri >> 3) << 11) | ((gi >> 2) << 5) | (bi >> 3));
}

/* ------------------------------------------------------- the light --- */
/* the sky through the day: minute, the zenith's colour, the horizon's */
static const uint16_t SKY[][7] = {
    {0, 8, 10, 30, 22, 26, 58},       {240, 10, 12, 36, 30, 30, 72},     {330, 42, 40, 92, 160, 96, 118},
    {390, 96, 124, 186, 250, 172, 120}, {480, 92, 160, 226, 186, 218, 240}, {750, 58, 138, 230, 172, 216, 250},
    {960, 80, 148, 220, 206, 216, 228}, {1080, 118, 118, 180, 250, 182, 100}, {1140, 82, 60, 122, 240, 124, 80},
    {1200, 32, 30, 72, 92, 60, 102},  {1290, 12, 14, 40, 30, 30, 64},    {1440, 8, 10, 30, 22, 26, 58},
};
#define NSKY (sizeof SKY / sizeof SKY[0])

static struct {
    float zen[3], hor[3];             /* the sky's two colours, as floats */
    float light;                      /* 0.28 night .. 1 day */
    float warm;                       /* the low sun's gold */
    uint8_t flash;                    /* lightning, frames left */
} lt;

static void light_at(uint32_t min_x10)    /* (a tenth of a minute, for smooth colours) */
{
    float m = (float)min_x10 * 0.1f, f;
    uint32_t k;
    int c;
    for (k = 0; k + 1u < NSKY && !(m >= (float)SKY[k][0] && m < (float)SKY[k + 1][0]); k++)
        ;
    if (k + 1u >= NSKY)
        k = NSKY - 2u;
    f = fm_clampf((m - (float)SKY[k][0]) / (float)(SKY[k + 1][0] - SKY[k][0]), 0.0f, 1.0f);
    for (c = 0; c < 3; c++) {
        lt.zen[c] = (float)SKY[k][1 + c] + (float)(SKY[k + 1][1 + c] - SKY[k][1 + c]) * f;
        lt.hor[c] = (float)SKY[k][4 + c] + (float)(SKY[k + 1][4 + c] - SKY[k][4 + c]) * f;
    }
    lt.light = m < 330.0f ? 0.28f : m < 420.0f ? 0.28f + 0.72f * (m - 330.0f) / 90.0f : m < 1080.0f ? 1.0f :
               m < 1200.0f ? 1.0f - 0.72f * (m - 1080.0f) / 120.0f : 0.28f;
    lt.warm = m >= 360.0f && m < 450.0f ? 1.0f - fm_fabsf(m - 405.0f) / 45.0f :
              m >= 1020.0f && m < 1200.0f ? 1.0f - fm_fabsf(m - 1120.0f) / 100.0f : 0.0f;
    lt.warm = fm_clampf(lt.warm, 0.0f, 1.0f);
    /* fog greys it, a storm darkens it */
    for (c = 0; c < 3; c++) {
        float grey = 70.0f + 110.0f * lt.light, s = 1.0f - 0.45f * w.storm;
        lt.zen[c] = (lt.zen[c] + (grey - lt.zen[c]) * w.fog * 0.75f) * s;
        lt.hor[c] = (lt.hor[c] + (grey - lt.hor[c]) * w.fog * 0.8f) * s;
        if (lt.flash) {
            lt.zen[c] += (230.0f - lt.zen[c]) * 0.6f;
            lt.hor[c] += (230.0f - lt.hor[c]) * 0.6f;
        }
    }
}

/* a thing's colour in this light: dimmed at night, gilded by a low sun, greyed by fog */
static uint16_t shade(int r, int g, int b)
{
    float l = lt.light * (1.0f - 0.3f * w.storm) + (lt.flash ? 0.4f : 0.0f), wm = lt.warm;
    float fr = (float)r * l * (1.0f + 0.22f * wm), fg = (float)g * l * (1.0f + 0.04f * wm), fb = (float)b * l * (1.0f - 0.3f * wm);
    float k = w.fog * 0.55f;
    fr += (lt.hor[0] - fr) * k;
    fg += (lt.hor[1] - fg) * k;
    fb += (lt.hor[2] - fb) * k;
    return rgbf(fr, fg, fb);
}

/* --------------------------------------------------------- the scene --- */
#define HORIZON 104
#define SHORE 142                     /* where the sand starts at low water */
#define PANEL_Y 156
#define LAMP_X 211
#define LAMP_Y 35

static uint32_t rhash(uint32_t a)
{
    a ^= a >> 16;
    a *= 0x7FEB352Du;
    a ^= a >> 15;
    a *= 0x846CA68Bu;
    return a ^ (a >> 16);
}

static void draw_sky(void)
{
    int32_t y;
    for (y = 0; y < HORIZON; y++) {
        float t = (float)y / (float)HORIZON;
        t = t * t;
        cv_rect(0, y, 240, 1, rgbf(lt.zen[0] + (lt.hor[0] - lt.zen[0]) * t, lt.zen[1] + (lt.hor[1] - lt.zen[1]) * t,
                                   lt.zen[2] + (lt.hor[2] - lt.zen[2]) * t));
    }
}

static void draw_stars(uint32_t now)
{
    float dark = fm_clampf((0.7f - lt.light) * 2.2f, 0.0f, 1.0f) * (1.0f - w.fog) * (1.0f - w.storm);
    uint32_t i;
    if (dark <= 0.05f)
        return;
    for (i = 0; i < 46u; i++) {
        uint32_t h = rhash(i * 2654435761u + 77u);
        int32_t x = (int32_t)(h % 240u), y = (int32_t)((h >> 8) % 88u);
        uint32_t tw = rhash(h + now / 700u) % 4u;
        px_blend(x, y, RGB(255, 250, 220), (uint32_t)(dark * (float)(8u + tw * 2u)));
    }
}

static void draw_sun_moon(void)
{
    uint32_t m = world_min();
    float s = sun_arc(m), mo = moon_arc(m);
    if (s >= 0.0f) {
        float x = 12.0f + s * 186.0f, y = (float)HORIZON + 4.0f - fm_sinf(FM_PI * s) * 84.0f;
        float low = 1.0f - fm_sinf(FM_PI * s);
        uint16_t c = rgbf(255.0f, 236.0f - 90.0f * low, 150.0f - 110.0f * low);
        if (w.fog < 0.9f && w.storm < 0.8f) {
            disc(x, y, 9.0f, mix(rgbf(lt.hor[0], lt.hor[1], lt.hor[2]), c, 5));
            disc(x, y, 6.0f, mix(c, rgbf(lt.hor[0], lt.hor[1], lt.hor[2]), q16(w.fog * 0.7f)));
        }
    }
    if (mo >= 0.0f && w.storm < 0.8f) {
        float x = 12.0f + mo * 186.0f, y = (float)HORIZON + 2.0f - fm_sinf(FM_PI * mo) * 78.0f;
        uint32_t age = proj.day % 30u;           /* its phase: new at 0, full at 15 */
        float off = ((float)age - 15.0f) * 0.8f;
        uint16_t sky = rgbf(lt.zen[0], lt.zen[1], lt.zen[2]);
        disc(x, y, 5.5f, mix(RGB(236, 232, 210), sky, q16(w.fog * 0.8f)));
        if (age != 15u)
            disc(x + off, y - 0.5f, 5.0f, mix(sky, RGB(236, 232, 210), 2));
    }
}

static void draw_clouds(uint32_t now)
{
    int n = 1 + (int)(w.wind * 3.0f) + (int)(w.storm * 3.0f), i;
    float speed = 0.0015f + 0.006f * w.wind + 0.01f * w.storm;
    uint16_t c = shade(236, 236, 240);
    if (w.storm > 0.3f)
        c = mix(c, RGB(52, 56, 66), q16(w.storm));
    for (i = 0; i < n; i++) {
        uint32_t h = rhash((uint32_t)i * 977u + 13u);
        int32_t span = 300, x = (int32_t)((float)(h % 300u) + (float)now * speed * (0.6f + 0.1f * (float)(i % 4))) % span - 40;
        int32_t y = 10 + (int32_t)((h >> 9) % 54u), wd = 26 + (int32_t)((h >> 3) % 30u);
        uint32_t a = 9u + (uint32_t)(w.storm * 6.0f);
        blend_rect(x, y + 4, wd, 5, c, a);
        blend_rect(x + 5, y, wd - 12, 5, c, a);
        blend_rect(x + wd / 3, y - 3, wd / 3, 4, c, a);
    }
}

static void draw_gulls(uint32_t now)
{
    uint32_t m = world_min(), i;
    if (m < 400u || m > 1110u || w.storm > 0.5f)
        return;
    for (i = 0; i < 3u; i++) {
        float t = (float)now * 0.001f * (0.5f + 0.13f * (float)i);
        int32_t x = (int32_t)(t * 18.0f + (float)i * 80.0f) % 280 - 20;
        int32_t y = 40 + (int32_t)i * 12 + (int32_t)(fm_sinf(t * 1.3f + (float)i) * 6.0f);
        int flap = (int)(now / (220u + 40u * i)) & 1;
        uint16_t c = shade(250, 250, 250);
        cv_rect(x - 3, y - flap, 3, 1, c);
        cv_rect(x + 1, y - flap, 3, 1, c);
        cv_rect(x, y, 1, 1, c);
    }
}

static void draw_beam(uint32_t now)
{
    float a = (float)(now % 8000u) * (FM_TWO_PI / 8000.0f), d = fm_cosf(a), len = fm_fabsf(d) * 150.0f;
    int32_t i;
    uint32_t al = (uint32_t)(3.0f + 4.0f * (1.0f - lt.light) + 3.0f * w.fog);
    if (fm_sinf(a) > 0.0f)                       /* turned away from us: only a glow */
        len *= 0.35f;
    for (i = 4; i < (int32_t)len; i++) {
        int32_t x = LAMP_X + (d < 0.0f ? -i : i), half = 1 + i / 9, j;
        uint32_t fa = al * (uint32_t)(len - (float)i) / (uint32_t)(len + 1.0f) + 1u;
        for (j = -half; j <= half; j++)
            px_blend(x, LAMP_Y + j, RGB(255, 236, 160), fa);
    }
}

static void draw_sea(uint32_t now)
{
    float tide = world_tide();
    int32_t y, wl = SHORE + (int32_t)(tide * 12.0f + 0.5f), i;
    uint16_t far = rgbf(lt.hor[0] * 0.55f + 20.0f * lt.light, lt.hor[1] * 0.6f + 40.0f * lt.light,
                        lt.hor[2] * 0.6f + 70.0f * lt.light);
    uint16_t near = shade(24, 70, 104), sand = shade(214, 192, 140), wet = shade(170, 150, 108);
    for (y = HORIZON; y < PANEL_Y; y++)
        cv_rect(0, y, 240, 1, mix(far, near, (y - HORIZON) * 16 / (PANEL_Y - HORIZON)));
    /* the beach: sand from the shore down, the water over it as high as the tide */
    cv_rect(0, SHORE, 176, PANEL_Y - SHORE, sand);
    cv_rect(0, SHORE, 176, 2, wet);
    for (y = SHORE; y < wl && y < PANEL_Y; y++)
        cv_rect(0, y, 176, 1, mix(near, sand, 4));
    if (wl < PANEL_Y)
        cv_rect(0, wl, 176, 2, wet);
    {   /* the foam at the water's edge, washing in and out */
        float sw = fm_sinf((float)now * 0.0011f) * 1.5f;
        int32_t fy = wl - 1 + (int32_t)sw;
        for (i = 0; i < 176; i += 3)
            px_blend(i, fy + (int32_t)(rhash((uint32_t)i + now / 400u) % 2u), RGB(240, 244, 240), 9);
    }
    /* the swell: dashes riding along, more and longer with the wind */
    {
        int rows = 6 + (int)(w.wind * 4.0f + w.storm * 4.0f), r;
        uint16_t c = mix(far, RGB(230, 240, 245), 5 + q16(w.storm * 0.5f));
        for (r = 0; r < rows; r++) {
            int32_t yy = HORIZON + 3 + r * r * (wl - HORIZON - 6) / (rows * rows);
            int32_t len = 3 + r + (int32_t)(w.wind * 4.0f);
            float sp = 0.006f + 0.004f * (float)r + 0.012f * w.storm;
            for (i = 0; i < 4; i++) {
                int32_t x = ((int32_t)((float)now * sp) + (int32_t)(rhash((uint32_t)(r * 7 + i)) % 240u)) % 260 - 10;
                if (yy < wl - 2)
                    blend_rect(x, yy, len, 1, c, 9u);
            }
        }
    }
    /* the tide post, marked every 3 pixels */
    {
        uint16_t post = shade(92, 70, 48);
        cv_rect(150, 124, 2, PANEL_Y - 124, post);
        for (y = 128; y < PANEL_Y; y += 3)
            cv_rect(152, y, 2, 1, shade(230, 230, 220));
    }
    /* the jetty */
    {
        uint16_t wood = shade(110, 82, 54);
        cv_rect(0, 137, 56, 3, wood);
        for (i = 6; i < 56; i += 12)
            cv_rect(i, 140, 2, 12, mix(wood, RGB(0, 0, 0), 4));
    }
}

static void draw_boat(int32_t x, int32_t y, int big, uint16_t hull, uint16_t sail, int light)
{
    if (big) {
        cv_rect(x - 6, y, 13, 2, hull);
        cv_rect(x - 5, y + 2, 11, 1, hull);
        cv_rect(x, y - 9, 1, 9, hull);
        cv_rect(x + 1, y - 8, 3, 7, sail);
        cv_rect(x + 4, y - 5, 1, 4, sail);
    } else {
        cv_rect(x - 2, y, 5, 1, hull);
        cv_rect(x, y - 3, 1, 3, sail);
    }
    if (light)
        px_blend(x, y - (big ? 10 : 4), RGB(255, 220, 120), 16);
}

static void draw_boats(uint32_t now)
{
    static const int16_t HX[NBOATS] = {66, 86, 106}, SX[NBOATS] = {36, 78, 120};
    uint32_t m = world_min();
    int i;
    for (i = 0; i < NBOATS; i++) {
        float p = boat_at(i, m), bob = fm_sinf((float)now * 0.002f + (float)i * 2.0f);
        int32_t x, y;
        if (p < 0.0f)
            continue;
        x = (int32_t)((float)HX[i] + (float)(SX[i] - HX[i]) * p + (proj.late == i + 1 ? bob * 6.0f : 0.0f));
        y = (int32_t)((float)(SHORE + 2) + (float)(HORIZON + 2 - SHORE - 2) * p + (p < 1.0f ? bob : 0.0f));
        draw_boat(x, y, p < 0.55f, shade(80, 46, 34), shade(240, 234, 214), lt.light < 0.6f);
    }
}

/* the headland: a cliff up from the sea to a grassy top, the cottage, the lighthouse */
static void draw_land(uint32_t now)
{
    uint16_t rock = shade(96, 88, 84), rock2 = shade(70, 64, 64), grass = shade(96, 140, 70);
    uint16_t white = shade(240, 238, 228), red = shade(196, 52, 44), roof = shade(140, 58, 46), dark = shade(40, 40, 46);
    int32_t x;
    for (x = 156; x < 240; x++) {
        int32_t top = x < 180 ? 96 + (180 - x) * (180 - x) / 12 : 96;
        if (top > PANEL_Y)
            continue;
        cv_rect(x, top, 1, PANEL_Y - top, (x * 7 + top) % 11 < 3 ? rock2 : rock);
        cv_rect(x, top, 1, 2, grass);
    }
    /* the cottage */
    cv_rect(176, 87, 20, 9, white);
    for (x = 0; x < 6; x++)
        cv_rect(174 + x, 86 - x, 24 - 2 * x, 1, roof);
    cv_rect(192, 90, 3, 6, dark);
    {
        int a = keeper_act(), lit = keeper_where(a) == AT_HOME && a != KA_SLEEP && lt.light < 0.8f;
        cv_rect(180, 89, 5, 4, lit ? RGB(255, 210, 110) : dark);
        if (a == KA_SLEEP && (now / 900u) % 3u) {    /* asleep: a z drifting up */
            int32_t zy = 78 - (int32_t)(now / 300u % 6u);
            cv_rect(184, zy, 3, 1, white);
            cv_rect(185, zy + 1, 1, 1, white);
            cv_rect(184, zy + 2, 3, 1, white);
        }
    }
    /* the tower, tapering, with two red bands */
    for (x = 44; x < 96; x++) {
        int32_t half = 4 + (x - 44) / 18, b = (x >= 56 && x < 63) || (x >= 75 && x < 82);
        cv_rect(LAMP_X - half, x, 2 * half + 1, 1, b ? red : white);
    }
    cv_rect(LAMP_X - 8, 41, 17, 3, dark);                /* the gallery */
    cv_rect(LAMP_X - 7, 39, 15, 2, dark);
    {
        int lit = lamp_lit();
        uint16_t glass = lit ? RGB(255, 226, 120) : shade(90, 110, 120);
        cv_rect(LAMP_X - 4, 30, 9, 9, glass);
        cv_rect(LAMP_X, 30, 1, 9, dark);
        if (lit)
            disc((float)LAMP_X + 0.5f, (float)LAMP_Y, 7.0f, mix(rgbf(lt.zen[0], lt.zen[1], lt.zen[2]), RGB(255, 226, 120), 6));
        if (lit)
            cv_rect(LAMP_X - 3, 32, 7, 5, RGB(255, 246, 200));
    }
    for (x = 0; x < 6; x++)                              /* the cap */
        cv_rect(LAMP_X - 5 + x, 29 - x, 11 - 2 * x, 1, dark);
    cv_rect(LAMP_X, 22, 1, 2, dark);
}

/* Abel: a coat, a face, a cap; yellow oilskins in a storm */
static void draw_keeper(uint32_t now)
{
    static const int16_t AX[6] = {0, LAMP_X - 6, 166, 201, 112, 144}, AY[6] = {0, 33, 89, 89, 0, 135};
    int a = keeper_act(), at = keeper_where(a);
    int32_t x, y;
    uint16_t coat = w.storm > 0.5f || w.wind > 0.7f ? shade(232, 196, 40) : shade(40, 58, 96);
    if (at == AT_HOME)
        return;
    x = AX[at];
    y = AY[at];
    if (at == AT_BEACH) {                                /* along the water's edge, slowly */
        x = 40 + (int32_t)(now / 1500u % 100u);
        y = SHORE + (int32_t)(world_tide() * 12.0f) - 9;
        if (y > PANEL_Y - 9)
            y = PANEL_Y - 9;
    }
    if (at == AT_EDGE)
        x += (int32_t)(fm_sinf((float)now * 0.0004f) * 3.0f);
    cv_rect(x, y, 3, 1, shade(30, 30, 34));              /* cap */
    cv_rect(x, y + 1, 3, 2, shade(232, 190, 160));       /* face */
    cv_rect(x - 1, y + 3, 5, 4, coat);
    cv_rect(x, y + 7, 1, 2, shade(30, 30, 34));
    cv_rect(x + 2, y + 7, 1, 2, shade(30, 30, 34));
    if (a == KA_HORN && (now / 600u) % 2u)               /* the horn's call: rings out */
        disc((float)x - 4.0f, (float)y + 2.0f, 2.0f, RGB(250, 250, 250));
}

/* Captain, on the post once he trusts Abel (or beside him at feeding time) */
static void draw_captain(uint32_t now)
{
    uint32_t m = world_min();
    int32_t x = 149, y = 120;
    if (proj.trust < 25u || m < 420u || m > 1080u || w.storm > 0.5f)
        return;
    if (keeper_act() == KA_GULLS) {
        x = 138;
        y = 140 + (int32_t)((now / 500u) % 2u);
    }
    cv_rect(x, y, 4, 3, shade(246, 246, 246));
    cv_rect(x + 1, y, 3, 1, shade(150, 156, 168));
    cv_rect(x - 1, y, 1, 1, shade(250, 170, 40));        /* beak */
    cv_rect(x + 1, y + 3, 1, 1, shade(250, 170, 40));
}

static void draw_weather(uint32_t now)
{
    uint32_t i;
    if (w.storm > 0.2f) {                                /* rain, slanting */
        uint16_t c = RGB(170, 180, 196);
        uint32_t n = (uint32_t)(w.storm * 70.0f);
        for (i = 0; i < n; i++) {
            uint32_t h = rhash(i * 7919u + now / 90u);
            int32_t x = (int32_t)(h % 250u), y = (int32_t)((h >> 9) % (uint32_t)PANEL_Y);
            px_blend(x, y, c, 8);
            px_blend(x - 1, y + 1, c, 8);
            px_blend(x - 2, y + 2, c, 6);
        }
    }
    if (w.fog > 0.1f) {                                  /* low banks of fog over the water */
        uint16_t c = rgbf(lt.hor[0] + 20.0f, lt.hor[1] + 20.0f, lt.hor[2] + 20.0f);
        int32_t y;
        for (y = HORIZON - 18; y < HORIZON + 30; y += 6) {
            int32_t x0 = (int32_t)((float)now * 0.004f + (float)y * 13.0f) % 240;
            blend_rect(x0 - 240, y, 200, 4, c, (uint32_t)(w.fog * 7.0f));
            blend_rect(x0, y, 200, 4, c, (uint32_t)(w.fog * 7.0f));
        }
    }
}

/* the panel under the scene: the time big, the phase, the weather and the tide, the day, what Abel is doing */
#define K_PANEL RGB(14, 18, 30)
#define K_TEXT RGB(238, 230, 204)
#define K_DIM RGB(128, 132, 150)
static void draw_panel(uint32_t now)
{
    char t[48], d[40];
    uint16_t acc = rgbf(fm_maxf(lt.hor[0], 120.0f), fm_maxf(lt.hor[1], 110.0f), fm_maxf(lt.hor[2], 90.0f));
    int32_t x;
    const log_t *e = log_get(0);
    cv_rect(0, PANEL_Y, 240, 240 - PANEL_Y, K_PANEL);
    cv_rect(0, PANEL_Y, 240, 1, acc);
    hhmm(world_min(), t);
    x = cv_text(8, PANEL_Y + 4, &FONT_L, t, K_TEXT);
    if (ui.setting)
        cv_rect(8, PANEL_Y + 36, x - 8, 2, acc);
    cv_text(x + 10, PANEL_Y + 5, &FONT_B, phase_name(world_phase()), acc);
    t[0] = 0;
    cat_s(cat_s(cat_s(t, WX_NAME[proj.wx]), ", tide "), tide_rising() ? "rising" : "falling");
    cv_text(x + 10, PANEL_Y + 22, &FONT_XS, t, K_DIM);
    t[0] = 0;
    cat_u(cat_s(t, "DAY "), proj.day);
    text_r(234, PANEL_Y + 5, &FONT_XS, t, K_TEXT);
    if (!en_playing)
        text_r(234, PANEL_Y + 22, &FONT_XS, "RESTING", acc);
    else if (proj.clock) {
        t[0] = 0;
        cat_s(cat_u(t, CYCLE_H[proj.cycle]), "H DAY");
        text_r(234, PANEL_Y + 22, &FONT_XS, t, K_DIM);
    }
    if (now < ui.msg_until) {                             /* a message for a moment */
        x = cv_text(8, PANEL_Y + 44, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 6, PANEL_Y + 44, &FONT_B, ui.msg[1], acc);
    } else {
        keeper_doing(d);
        t[0] = 0;
        cat_s(cat_s(cat_s(t, KEEPER), ": "), d);
        cv_text(8, PANEL_Y + 44, &FONT_S, t, K_TEXT);
    }
    /* the newest event while it is fresh (within two world hours), else his mood and the gull */
    if (e && e->day == proj.day && world_min() >= e->min && world_min() - e->min < 120u) {
        hhmm(e->min, t);
        x = cv_text(8, PANEL_Y + 63, &FONT_XS, t, K_DIM);
        event_text(e, d);
        cv_text(x + 6, PANEL_Y + 63, &FONT_XS, d, acc);
    } else {
        t[0] = 0;
        if (keeper_act() == KA_SLEEP)
            cat_s(t, "Sleeping soundly");
        else
            cat_s(cat_s(t, "Feels "), keeper_mood());
        if (proj.trust >= 25u)
            cat_s(t, ", Captain about");
        cv_text(8, PANEL_Y + 63, &FONT_XS, t, K_DIM);
    }
}

/* a button's card over the scene */
static void draw_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    cv_round(16, 30, 208, 100, 6, RGB(250, 246, 230));
    cv_round(16, 30, 208, 26, 6, K_PANEL);
    cv_rect(16, 50, 208, 6, K_PANEL);
    text_c(120, 34, &FONT_B, BTN_LABEL[ui.help - 1], K_TEXT);
    for (k = 0; k < n; k++)
        text_c(120, 66 + k * 20, &FONT_S, l[k], K_PANEL);
}

static void scene_band(int band, uint32_t now)
{
    cv_begin(240, 120, K_PANEL);
    cv_oy = -120 * band;
    if (band == 0) {
        draw_sky();
        draw_stars(now);
        draw_sun_moon();
        draw_clouds(now);
        draw_gulls(now);
    }
    draw_sea(now);
    draw_boats(now);
    if (lamp_lit())
        draw_beam(now);
    draw_land(now);
    draw_keeper(now);
    draw_captain(now);
    draw_weather(now);
    if (band == 1)
        draw_panel(now);
    if (ui.help)
        draw_help();
    cv_oy = 0;
    cv_blit(0, (uint32_t)(120 * band));
}

/* the clock screen: the sky of the time being set, the time big */
static void draw_setclock(uint32_t now)
{
    char t[8];
    int band;
    uint32_t left = SET_IDLE - (now - ui.set_t > SET_IDLE ? SET_IDLE : now - ui.set_t);
    for (band = 0; band < 2; band++) {
        cv_begin(240, 120, K_PANEL);
        cv_oy = -120 * band;
        if (band == 0) {
            draw_sky();
            draw_stars(now);
            draw_sun_moon();
        }
        draw_sea(now);
        draw_land(now);
        cv_round(20, 64, 200, 116, 6, K_PANEL);
        text_c(120, 72, &FONT_XS, ui.set_boot ? "WHAT TIME IS IT?" : "SET THE CLOCK", K_DIM);
        hhmm(world_min(), t);
        text_c(120, 92, &FONT_L, t, K_TEXT);
        text_c(120, 128, &FONT_B, phase_name(world_phase()), rgbf(fm_maxf(lt.hor[0], 120.0f), fm_maxf(lt.hor[1], 110.0f),
                                                                  fm_maxf(lt.hor[2], 90.0f)));
        text_c(120, 148, &FONT_XS, "ANY KNOB: 15 MIN   PLAY: DONE", K_DIM);
        cv_rect(30, 170, (int32_t)(180u * left / SET_IDLE), 2, K_DIM);
        cv_oy = 0;
        cv_blit(0, (uint32_t)(120 * band));
    }
}

/* the log page: Abel's tallies, then the entries, newest first */
static void draw_log(void)
{
    char t[48], d[40];
    int band, k;
    for (band = 0; band < 2; band++) {
        cv_begin(240, 120, K_PANEL);
        cv_oy = -120 * band;
        cv_text(10, 6, &FONT_B, "ABEL'S LOG", K_TEXT);
        t[0] = 0;
        cat_u(cat_s(t, "DAY "), proj.day);
        text_r(230, 8, &FONT_XS, t, K_DIM);
        t[0] = 0;
        cat_u(cat_s(cat_u(cat_s(cat_u(cat_s(cat_u(cat_s(t, "Storms "), proj.storms), "  Fogs "), proj.fogs), "  Shells "),
                                proj.shells), "  Late "), proj.lates);
        cv_text(10, 26, &FONT_XS, t, RGB(200, 170, 110));
        cv_rect(10, 44, 220, 1, RGB(50, 56, 76));
        if (!proj.log_n)
            cv_text(10, 52, &FONT_S, "Nothing yet. A quiet coast.", K_DIM);
        for (k = 0; k < 8; k++) {
            const log_t *e = log_get(k + ui.log_off);
            int32_t y = 50 + k * 23, x;
            if (!e)
                break;
            t[0] = 0;
            cat_u(cat_s(t, "D"), e->day);
            x = cv_text(10, y, &FONT_XS, t, K_DIM);
            hhmm(e->min, t);
            cv_text(x > 44 ? x + 4 : 48, y, &FONT_XS, t, K_DIM);
            event_text(e, d);
            cv_text(84, y + (text_w(&FONT_S, d) > 146 ? 1 : 0), text_w(&FONT_S, d) > 146 ? &FONT_XS : &FONT_S, d, K_TEXT);
        }
        if (proj.log_n > 8)
            text_r(230, 228, &FONT_T, "KNOB 1: MORE", K_DIM);
        cv_oy = 0;
        cv_blit(0, (uint32_t)(120 * band));
    }
}

static void draw_setup(void)
{
    char t[32];
    int band, k;
    for (band = 0; band < 2; band++) {
        cv_begin(240, 120, K_PANEL);
        cv_oy = -120 * band;
        cv_text(10, 6, &FONT_B, "SETUP", K_TEXT);
        text_r(230, 8, &FONT_XS, "TIDE CLOCK " EN_VERSION, K_DIM);
        cv_rect(10, 26, 220, 1, RGB(50, 56, 76));
        for (k = 0; k < 4; k++) {
            int32_t y = 36 + k * 34;
            t[0] = 0;
            cat_u(cat_s(t, "KNOB "), (uint32_t)k + 1u);
            cv_text(10, y + 2, &FONT_XS, t, K_DIM);
            cv_text(62, y, &FONT_B, SETUP_LABEL[k], K_TEXT);
            setup_text(k, setup_get(k), t);
            text_r(230, y, &FONT_B, t, RGB(255, 196, 110));
        }
        cv_rect(10, 168, 220, 1, RGB(50, 56, 76));
        cv_text(10, 178, &FONT_XS, "PRESETS", K_DIM);
        text_r(230, 176, &FONT_B, preset_name(proj.preset), RGB(255, 196, 110));
        cv_text(10, 200, &FONT_XS, proj.clock ? "Cycle: a whole day every few hours." :
                                                "Clock: the real time, set at power-on.", K_DIM);
        cv_text(10, 218, &FONT_XS, ui.dirty ? "EDIT sets the clock. (Saves in a moment.)" :
                                              "EDIT sets the clock. PLAY rests.", K_DIM);
        cv_oy = 0;
        cv_blit(0, (uint32_t)(120 * band));
    }
}

static void draw(void)
{
    uint32_t now = plat_ms(), h;
    if (lt.flash)
        lt.flash--;
    else if (w.storm > 0.7f && rhash(now) % 900u == 0u)
        lt.flash = 3;
    if (now - ui.draw_t < 100u && !lt.flash)     /* ten frames a second is plenty for a coast */
        return;
    ui.draw_t = now;
    light_at(world_min() * 10u + w.ms % MIN_MS / 6000u);
    if (ui.setting) {
        draw_setclock(now);
        return;
    }
    if (ui.view == V_LOG || ui.view == V_SETUP) {  /* still pages: only when they change */
        h = hash(hash(hash(2166136261u, ui.view | (uint32_t)ui.log_off << 8 | (uint32_t)ui.dirty << 16),
                      proj.log_at | (uint32_t)proj.log_n << 8 | proj.day << 16),
                 (uint32_t)setup_get(0) | (uint32_t)setup_get(1) << 4 | (uint32_t)setup_get(2) << 8 | (uint32_t)setup_get(3) << 12 |
                     (uint32_t)proj.preset << 16);
        if (h == ui.sig)
            return;
        ui.sig = h;
        if (ui.view == V_LOG)
            draw_log();
        else
            draw_setup();
        return;
    }
    ui.sig = 0;
    scene_band(0, now);
    scene_band(1, now);
}

static void leds(void)
{
    uint32_t b = 0, k = 0, i, root = (uint32_t)w.cur[P_ROOT] % 12u, p = EN_MODE_POOL[w.cur[P_MODE] % EN_NMODES];
    uint32_t ap = ((p << root) | (p >> (12u - root))) & 0xFFFu;
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (ui.setting)
        b |= 1u << B_EDIT;
    if (proj.leds)
        for (i = 0; i < NKEYS; i++)
            if (ap >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    plat_glow(proj.leds == 1);
    plat_leds(b, k);
}

/* AUTOSAVE, and the flash's wear: every save erases one of the same two 4 KiB sectors (APP_STORE_MAP's A/B pair),
 * rated about 100k erases, so the coast saves itself rarely. keep_due is the whole policy:
 *   the world and the time of day (something notable: w.want_save; the clock moved) at most once an hour after the
 *     last save; nothing changed, no save. Up to an hour is lost at a power-off (the FM-1 gives no warning), and the
 *     clock asks for the time at power-on anyway;
 *   a setting, the time set by hand too (ui.dirty), 5 s after the change, at most once every 5 minutes;
 *   on the battery, once it reads empty (0): one save of whatever changed, then none of these until it reads above 0;
 *   SAVE saves at once.
 * Playing or resting the same. Each save resets the clock: it keeps the settings and the world alike. A flash erase
 * can stall the sound for a moment: a due save waits for a quiet moment in the music (the pad low, no bell ringing)
 * for up to a minute (10 s for a setting), never while the clock is being set */
#ifdef PLAT_HOST
static int8_t batt_host = -2;            /* (host.c's battery N) */
static int batt(void) { return batt_host > -2 ? batt_host : plat_battery(); }
#else
#define batt() plat_battery()
#endif
static uint8_t keep_low;                 /* the empty battery's save done; again once it reads above 0 */
static int keep_due(uint32_t now)        /* 0 not yet, 1 due, 2 due: the empty battery's save */
{
    uint32_t since = now - ui.saved_t;
    int world = w.want_save || proj.tod != w.ms / 1000u;
    if (!ui.dirty && !world)
        return 0;
    if (ui.dirty && now - ui.dirty_t < SAVE_SETTINGS_MS)
        return 0;                        /* (the knobs still moving) */
    if (batt() == 0 && !keep_low && since >= KEEP_SETTINGS_MS)
        return 2;
    return since >= KEEP_WORLD_MS || (ui.dirty && since >= KEEP_SETTINGS_MS);
}
static void autosave(void)
{
    uint32_t now = plat_ms(), i;
    int due, quiet;
    float bell = 0.0f;
    if (batt() != 0)
        keep_low = 0;                    /* (every pass: the battery read above 0 again) */
    due = keep_due(now);
    if (!due || ui.setting || ui.btn) {
        if (!due)
            ui.due_t = 0;
        return;
    }
    if (!ui.due_t)
        ui.due_t = now ? now : 1u;
    for (i = 0; i < EN_NLOOP; i++)
        bell = fm_maxf(bell, en_loop_glow[i]);
    quiet = !en_playing || (en_pad_level < 0.3f && bell < 0.15f) || now - ui.due_t >= (ui.dirty ? 10000u : 60000u);
    if (!quiet)
        return;
    proj.tod = w.ms / 1000u;
    if (project_save() == 0) {
        ui.dirty = 0;
        w.want_save = 0;
    }
    ui.saved_t = now;                            /* (a failed save waits its turn too: no hammering the flash) */
    ui.due_t = 0;
    if (due == 2)
        keep_low = 1;
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader (announce.c; fm1-reader): the coast when it starts listening or asks
 * (WHERE), a page as it settles, a setting as it turns, the clock as it is set, the phase as it turns, each new log
 * entry (queued). Nothing is built while no reader listens. */
static struct {
    uint8_t view, setting, started, phase;
    int8_t val[4];
    uint16_t set_min;
    uint8_t log_at, log_n;
} sp;
static char sp_buf[ANN_MAX + 1];
static uint32_t sp_n;
static void sp_cat(const char *s)
{
    while (*s && sp_n < ANN_MAX)
        sp_buf[sp_n++] = *s++;
    sp_buf[sp_n] = 0;
}
static void sp_start(void)
{
    sp_n = 0;
    sp_buf[0] = 0;
}
static void sp_coast(void)                     /* "06:45, Dawn. Fog, tide rising. Day 12. Abel is ..., content." */
{
    char t[40];
    sp_start();
    hhmm(world_min(), t);
    sp_cat(t);
    sp_cat(", ");
    sp_cat(phase_name(world_phase()));
    sp_cat(". ");
    sp_cat(WX_NAME[proj.wx]);
    sp_cat(tide_rising() ? ", tide rising. Day " : ", tide falling. Day ");
    cat_u(t, proj.day);
    sp_cat(t);
    sp_cat(". " KEEPER " is ");
    keeper_doing(t);
    sp_cat(t);
    if (keeper_act() != KA_SLEEP) {
        sp_cat(", feels ");
        sp_cat(keeper_mood());
    }
    sp_cat(". Preset ");
    sp_cat(preset_name(proj.preset));
    if (!en_playing)
        sp_cat(". Resting");
}
static void sp_setting(int k)
{
    char t[24];
    sp_cat(SETUP_LABEL[k]);
    sp_cat(" ");
    setup_text(k, setup_get(k), t);
    sp_cat(t);
}
static void sp_page(void)
{
    int k;
    char t[40];
    if (ui.view == V_SCENE) {
        sp_coast();
        return;
    }
    sp_start();
    if (ui.view == V_SETUP) {
        sp_cat("Setup page: ");
        for (k = 0; k < 4; k++) {
            if (k)
                sp_cat(", ");
            sp_setting(k);
        }
        sp_cat(", Preset ");
        sp_cat(preset_name(proj.preset));
        return;
    }
    sp_cat("Log page. Day ");
    cat_u(t, proj.day);
    sp_cat(t);
    for (k = 0; k < 3; k++) {
        const log_t *e = log_get(k + ui.log_off);
        if (!e)
            break;
        sp_cat(". ");
        event_text(e, t);
        sp_cat(t);
    }
}
static void sp_clock(void)
{
    char t[8];
    sp_start();
    sp_cat(ui.set_boot ? "What time is it? " : "Set the clock: ");
    hhmm(world_min(), t);
    sp_cat(t);
    sp_cat(", ");
    sp_cat(phase_name(world_phase()));
}
static void sp_snap(void)
{
    int k;
    sp.view = ui.view;
    sp.setting = ui.setting;
    sp.phase = (uint8_t)world_phase();
    sp.set_min = (uint16_t)world_min();
    sp.log_at = proj.log_at;
    sp.log_n = proj.log_n;
    for (k = 0; k < 4; k++)
        sp.val[k] = (int8_t)setup_get(k);
}

static void speak(void)
{
    uint32_t now = plat_ms();
    int k;
    if (!ann_on()) {
        sp.started = 0;
        sp_said = 0;
        return;
    }
    if (!sp.started || ann_where()) {
        sp.started = 1;
        sp_snap();
        if (ui.setting)
            sp_clock();
        else
            sp_page();
        ann_say(sp_buf, 0, ANN_NOW);
        sp_said = 0;
        return;
    }
    if (sp_said) {
        sp_said = 0;
        sp_snap();
        return;
    }
    if (ui.setting != sp.setting || (ui.setting && world_min() != sp.set_min)) {
        sp.setting = ui.setting;
        sp.set_min = (uint16_t)world_min();
        if (ui.setting) {
            sp_clock();
            ann_later(sp_buf, ANN_NOW, 150u, now);
        }
        return;
    }
    if (ui.view != sp.view) {
        sp_snap();
        sp_page();
        ann_later(sp_buf, ANN_NOW, 200u, now);
        return;
    }
    for (k = 0; k < 4; k++)
        if (setup_get(k) != sp.val[k]) {
            sp.val[k] = (int8_t)setup_get(k);
            sp_start();
            sp_setting(k);
            ann_later(sp_buf, ANN_NOW, 120u, now);
        }
    if (world_phase() != sp.phase) {
        char t[8];
        sp.phase = (uint8_t)world_phase();
        sp_start();
        sp_cat(phase_name(sp.phase));
        sp_cat(", ");
        hhmm(world_min(), t);
        sp_cat(t);
        ann_say(sp_buf, 0, 0);
    }
    if (proj.log_at != sp.log_at || proj.log_n != sp.log_n) {   /* a new entry in the log */
        char t[40];
        sp.log_at = proj.log_at;
        sp.log_n = proj.log_n;
        event_text(log_get(0), t);
        ann_say(t, 0, 0);
    }
}

#ifdef PLAT_HOST
/* every text a reader can get (the "speakdump" command): KIND <tab> text */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"RESTING", 0}, {"PLAYING", 0}, {"SAVED", 0}, {"NOT SAVED", "FLASH ERROR"},
                                         {"TIME SET", "06:45"}};
    project_t keep = proj;
    uint8_t kv = ui.view, ks = ui.setting, kb = ui.set_boot;
    uint32_t kms = w.ms;
    float kf = w.fog, kw = w.wind, kst = w.storm;
    int v, k, x;
    char t[48];
    log_t e;
    for (v = 0; v < NVIEWS; v++) {
        ui.view = (uint8_t)v;
        sp_page();
        sp_print("SCREEN", sp_buf);
    }
    for (k = 0; k < 4; k++)
        for (x = 0; x <= setup_hi(k); x++) {
            setup_set(k, x);
            sp_start();
            sp_setting(k);
            sp_print("KNOB", sp_buf);
        }
    proj = keep;
    ui.view = V_SCENE;
    for (x = 0; x < 24; x++) {                   /* the coast at every hour, in turn calm, fog, wind, storm */
        w.ms = (uint32_t)x * 3600000u;
        proj.wx = (uint8_t)(x % NWX);
        w.fog = proj.wx == WX_FOG;
        w.wind = proj.wx == WX_WIND;
        w.storm = proj.wx == WX_STORM;
        proj.late = (uint8_t)(x == 21 ? 1 : 0);
        sp_coast();
        sp_print("WHERE", sp_buf);
    }
    ui.setting = 1;
    for (x = 0; x < 2; x++) {
        ui.set_boot = (uint8_t)x;
        sp_clock();
        sp_print("WHERE", sp_buf);
    }
    for (x = 1; x < NEV; x++)
        for (k = 0; k < 5; k++) {
            e.day = 10;
            e.min = 600;
            e.type = (uint8_t)x;
            e.arg = (uint8_t)k;
            event_text(&e, t);
            sp_print("EVENT", t);
        }
    for (x = 0; x < (int)(sizeof MSG / sizeof MSG[0]); x++) {
        msg_text(MSG[x][0], MSG[x][1], t);
        sp_print("MSG", t);
    }
    for (x = 0; x < NPRESETS; x++) {              /* PRESETS, each named */
        msg_text("PRESET", preset_name(x), t);
        sp_print("MSG", t);
    }
    proj = keep;
    ui.view = kv;
    ui.setting = ks;
    ui.set_boot = kb;
    w.ms = kms;
    w.fog = kf;
    w.wind = kw;
    w.storm = kst;
}
#endif

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.saved_t = plat_ms();
    ui.boot_min = world_min();
    lcd_fill(0, 0, 240, 240, K_PANEL);
    if (!en_playing)
        en_play(1);                              /* it plays from the start */
    if (!proj.clock)
        set_open(1);                             /* Clock: what time is it? (the music plays meanwhile) */
}

void ui_frame(void)
{
    input();
    world_tick(plat_ms());
    autosave();
    draw();
    leds();
    speak();
    ann_tick(plat_ms());
    ui.frame++;
}

void ui_input_only(void) { input(); }
