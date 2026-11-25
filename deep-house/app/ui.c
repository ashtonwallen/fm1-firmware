/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * DEEP HOUSE DECKS: ashtonwallen, 2026 */
/* DEEP HOUSE DECKS' UI: the panel and the screen (from AMB-1's: its knobs, help cards, messages and screen reader).
 * Part of the unity build (after gfx.c and project.c); the host simulator includes it too.
 *
 *   PLAY           the decks run / stop               EDIT   the next mix (at the next four-bar line)
 *   REC            HOLD: the record playing plays on   OCT- / OCT+   the keys' octave
 *   PRESETS        the set (each kept as left)         ALGORITHM     Filter
 *   HOME ENV GLO   the pages: Set, Groove, Levels (SELECT steps through them)
 *   KNOB 1-4       the page's four values
 *   keys           play along on the set's keys
 *   SAVE           saves (it also saves by itself at a quiet moment: see autosave); held 1.5 s: the set back to its
 *                  recipe */
#include "fastmath.h"

enum { V_SET, V_GROOVE, V_LEVELS, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Set", "Groove", "Levels"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_GLO};
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_ENERGY, P_FILTER, P_MIX, P_WARMTH},
    {P_TEMPO, P_SWING, P_MOOD, P_SPACE},
    {P_CHORDS, P_DRUMS, P_BASS, P_LEAD},
};

/* the screen's colours: a dim club, a deep blue booth; deck A in amber, deck B in teal */
#define K_BG RGB(14, 16, 28)
#define K_PANEL RGB(30, 33, 52)
#define K_PANEL_HI RGB(42, 46, 70)
#define K_LINE RGB(56, 60, 86)
#define K_DIM RGB(132, 138, 172)
#define K_TEXT RGB(238, 234, 222)
#define K_ACC RGB(255, 178, 74)
#define K_TEAL RGB(78, 208, 196)
#define K_RED RGB(236, 84, 72)
#define K_METAL RGB(196, 198, 206)
#define K_METAL_LO RGB(104, 108, 122)
#define K_LOW RGB(236, 96, 72)                 /* the battery at one bar or none */
static const uint16_t DECK_C[2] = {K_ACC, K_TEAL};
/* the records' labels */
static const uint8_t LABEL_RGB[12][3] = {
    {226, 98, 76}, {232, 180, 72}, {72, 160, 156}, {150, 180, 120}, {222, 142, 150}, {160, 140, 200},
    {236, 220, 180}, {110, 160, 210}, {236, 140, 70}, {170, 90, 130}, {130, 200, 170}, {180, 84, 64},
};

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
    uint32_t saved_track;              /* the record the last save kept */
    char msg[2][28];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    uint32_t keys_on;                  /* the keys sounding (played along), key bit */
    float rot[2];                      /* the platters' angles as drawn (turns) */
    float arm[2];                      /* the tonearms' angles as drawn */
    float vu[2], deck_vu[2];           /* the meters as drawn (0..1) */
    uint32_t draw_t;
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

static void mark_dirty(void)
{
    ui.dirty = 1;
    ui.act_t = plat_ms();
}
int ui_dirty(void) { return ui.dirty != 0; }

/* ------------------------------------------------------------ values --- */
static int knob_get(int k) { return proj.par[k]; }
static void knob_range(int k, int *lo, int *hi)
{
    *lo = en_param_info(k)->lo;
    *hi = en_param_info(k)->hi;
}
static void knob_text(int k, char *b) { en_param_text(k, knob_get(k), b); }
static void knob_set(int k, int v)
{
    int lo, hi;
    knob_range(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (v == knob_get(k))
        return;
    proj.par[k] = (int16_t)v;
    en_set(k, v);
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
    if (!e)
        return;
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------- actions --- */
static void sp_style(void);
static void set_style(int i)                    /* the set playing kept as it is; set i as it was left */
{
    project_style(i);
    project_apply();
    if (!en_playing)
        en_play(1);
    en_mix(1);                                   /* the DJ mixes into the new set: a short blend */
    sp_tp.playing = 1;
    say_as("SET", en_style_name(proj.style), 0);
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
        if (!en_playing) {
            en_play(1);
            sp_tp.playing = 1;
        }
        if (en_mixing) {
            warn("MIXING", "ALREADY");
        } else {
            en_mix(0);
            say("NEXT MIX", 0);
        }
        break;
    case B_PLAY:
        sp_tp.playing = !en_playing;
        sp_tp.play_t = plat_ms();
        if (en_playing) {
            en_play(0);
            say("STOPPED", 0);
        } else {
            en_play(1);
            say("PLAYING", 0);
        }
        break;
    case B_REC:
        say("HOLD", en_hold_on ? "OFF" : "ON");
        en_hold(!en_hold_on);
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

/* MIDI in (USB and the TRS jack): notes play along on the set's keys */
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
 * another button puts it away. PLAY acts on its press; the buttons with no job have no card */
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
    if ((e = plat_enc(EN_ALGO)) != 0) {          /* ALGORITHM: Filter, from any page */
        char t[12];
        turn(EN_ALGO, P_FILTER, e);
        en_param_text(P_FILTER, proj.par[P_FILTER], t);
        if (ui.view == V_SET) {
            ui.touched = 1;
            ui.touch_until = plat_ms() + 1200u;
        } else {
            say("FILTER", t);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        set_style(proj.style + (e > 0 ? 1 : -1));
        ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && plat_ms() - ui.down_t[B_SAVE] > RESET_HOLD) {
        ui.reset_done = 1;                       /* held: the set back to its recipe (not saved) */
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
/* three bands: the header (22 px), the decks and the mixer (164), the knobs (54) */
#define HDR_H 22
#define MAIN_Y 22
#define MAIN_H 164
#define KNB_Y 186
#define KNB_H 54

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
/* a thick anti-aliased line: discs along it */
static void rod(float x0, float y0, float x1, float y1, float r, uint16_t c)
{
    float dx = x1 - x0, dy = y1 - y0, len = fm_sqrtf(dx * dx + dy * dy), t;
    int i, n = (int)(len / 1.2f) + 1;
    for (i = 0; i <= n; i++) {
        t = (float)i / (float)n;
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

/* a knob ring (AMB-1's): a 270-degree track, filled from v0 to v1 (a bipolar knob fills from its middle), a handle
 * at v1 */
static void arc(int32_t cx, int32_t cy, int32_t r, float th, float v0, float v1, uint16_t track, uint16_t fill,
                uint16_t bg)
{
    const float a0 = 0.375f, sweep = 0.75f;
    float ro = (float)r, ri = ro - th, rm = ro - th * 0.5f, fx = (float)cx + 0.5f, fy = (float)cy + 0.5f;
    float lo = fm_minf(v0, v1), hi = fm_maxf(v0, v1);
    int32_t x, y;
    v1 = fm_clampf(v1, 0.0f, 1.0f);
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
            px_blend(cx + x, cy + y, t >= lo && t <= hi ? fill : track, (uint32_t)(a * 16.0f + 0.5f));
        }
    {
        float ev = (a0 + sweep * v1) * FM_TWO_PI;
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th * 0.5f + 1.2f, bg);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, th * 0.5f + 0.5f, fill);
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
    cv_round(x, 6, 18, 10, 2, c);
    cv_rect(x + 1, 7, 16, 8, K_BG);
    cv_rect(x + 18, 9, 2, 4, c);
    if (b == 4) {
        for (i = 0; i < 7; i++)
            cv_rect(x + BOLT[i][0], 7 + i, BOLT[i][1], 1, K_TEXT);
        return;
    }
    for (i = 0; i < b && i < 3; i++)
        cv_rect(x + 2 + i * 5, 8, 4, 6, b <= 1 ? K_LOW : K_ACC);
}
static const char *batt_words(int b)
{
    static const char *const W[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                     "On USB power"};
    return b >= 0 && b <= 4 ? W[b] : 0;
}

/* the header: the set in a tag; on the right the battery, unsaved, the tempo with a beat light, HOLD */
static void draw_header(void)
{
    char t[16];
    int msg = plat_ms() < ui.msg_until, beat = (int)(en_beats & 1u);
    uint32_t h = hash(hash(hash(2166136261u, proj.style | (uint32_t)en_bpm << 8 | (uint32_t)en_playing << 16 |
                                                   (uint32_t)en_hold_on << 17 | (uint32_t)ui.dirty << 18 |
                                                   (uint32_t)msg << 19 | (uint32_t)beat << 20),
                           (uint32_t)(batt() + 1)),
                      msg ? ui.msg_until : 0u);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(8, 3, &FONT_S, ui.msg[0], K_TEXT);
        cv_text(x + 7, 3, &FONT_S, ui.msg[1], K_ACC);
    } else {
        int32_t x = 234, w;
        if (batt() >= 0) {
            x -= BATT_W;
            draw_batt(x, batt());
            x -= 7;
        }
        if (ui.dirty) {
            cv_round(x - 5, 9, 5, 5, 2, K_DIM);
            x -= 11;
        }
        cat_u(t, en_bpm);
        x -= text_w(&FONT_T, "BPM");
        cv_text(x, 7, &FONT_T, "BPM", K_DIM);
        x -= text_w(&FONT_S, t) + 3;
        cv_text(x, 3, &FONT_S, t, en_playing ? K_TEXT : K_DIM);
        x -= 13;
        if (en_playing) {                        /* the beat light */
            cv_round(x, 7, 8, 8, 4, beat ? K_ACC : K_LINE);
        } else {                                 /* stopped: a square */
            cv_rect(x, 6, 9, 9, K_DIM);
        }
        if (en_hold_on) {
            x -= text_w(&FONT_T, "HOLD") + 13;
            cv_round(x, 4, text_w(&FONT_T, "HOLD") + 8, 14, 4, K_TEAL);
            cv_text(x + 4, 6, &FONT_T, "HOLD", K_BG);
        }
        w = text_w(&FONT_S, en_style_name(proj.style)) + 12;
        if (3 + w < x - 4) {
            cv_round(3, 2, w, 18, 5, K_ACC);
            cv_text(9, 3, &FONT_S, en_style_name(proj.style), K_BG);
        }
    }
    cv_blit(0, 0);
}

/* ---- the turntables: a platter from two tables (each pixel's radius in quarter pixels, its angle in 256ths) */
#define PL_R 36                                  /* the platter's radius */
#define PL_BOX 74                                /* its square */
#define PL_Y 6                                   /* its square's top */
static const int16_t PL_X[2] = {4, 162};         /* its square's left, by deck */
static uint8_t pl_r4[PL_BOX * PL_BOX] PLAT_POOL, pl_ang[PL_BOX * PL_BOX] PLAT_POOL;
static uint8_t sheen_tab[256];
static void pl_tables(void)
{
    int x, y;
    for (y = 0; y < PL_BOX; y++)
        for (x = 0; x < PL_BOX; x++) {
            float dx = (float)x + 0.5f - (float)PL_BOX * 0.5f, dy = (float)y + 0.5f - (float)PL_BOX * 0.5f,
                  d = fm_sqrtf(dx * dx + dy * dy) * 4.0f;
            pl_r4[y * PL_BOX + x] = (uint8_t)(d > 255.0f ? 255.0f : d);
            pl_ang[y * PL_BOX + x] = (uint8_t)((int)(turns(dy, dx) * 256.0f) & 255);
        }
    for (x = 0; x < 256; x++) {                  /* the booth light's reflection: two soft wedges, still as it turns */
        float a = (float)x * (FM_TWO_PI / 256.0f), c = fm_cosf(2.0f * (a - 0.6f));
        c = c > 0.0f ? c * c * c * c : 0.0f;
        sheen_tab[x] = (uint8_t)(c * 9.0f + 0.5f);
    }
}

static uint16_t label_c(int k, int shade)       /* a label's colour, darker by shade (0..16) */
{
    const uint8_t *c = LABEL_RGB[k % 12];
    return mix(RGB(c[0], c[1], c[2]), RGB(16, 12, 20), shade);
}

/* a platter: its rim with the strobe dots turning, the record on it (grooves, the sheen, the label turning), or the
 * bare slipmat */
static void draw_platter(int d)
{
    static const uint16_t VIN[2] = {RGB(18, 18, 24), RGB(26, 26, 34)};
    uint16_t vin[2][10], lab[4], mat = RGB(34, 34, 42), mat_hi = RGB(70, 72, 88);
    int lc = en_label_colour(en_deck[d].seed), i, x, y, rot = (int)(ui.rot[d] * 256.0f) & 255,
        rec = en_deck[d].loaded;
    int ox = PL_X[d], oy = PL_Y;
    for (i = 0; i < 10; i++) {
        vin[0][i] = mix(VIN[0], RGB(150, 150, 176), i);
        vin[1][i] = mix(VIN[1], RGB(150, 150, 176), i);
    }
    lab[0] = label_c(lc, 0);
    lab[1] = label_c(lc, 5);
    lab[2] = label_c(lc, 9);
    lab[3] = mix(label_c(lc, 0), K_TEXT, 9);
    for (y = 0; y < PL_BOX; y++) {
        uint16_t *row;
        int sy = oy + y + cv_oy;
        if (sy < cv_y0 || sy >= cv_y1)
            continue;
        row = &cv_px[(uint32_t)sy * cv_w];
        for (x = 0; x < PL_BOX; x++) {
            int r4 = pl_r4[y * PL_BOX + x], a = pl_ang[y * PL_BOX + x], ra = (a - rot) & 255, sx = ox + x;
            uint16_t c;
            if (r4 >= PL_R * 4 + 2 || (uint32_t)sx >= cv_w)
                continue;
            if (r4 >= PL_R * 4 - 2) {             /* the platter's edge, softened into the panel */
                c = mix(K_METAL_LO, K_BG, (r4 - (PL_R * 4 - 2)) * 4);
            } else if (r4 >= PL_R * 4 - 13) {     /* the rim: metal, the strobe dots on it */
                c = r4 >= PL_R * 4 - 9 && r4 < PL_R * 4 - 4 && (ra & 15) < 6 ? K_METAL : K_METAL_LO;
                if (r4 < PL_R * 4 - 11)
                    c = RGB(20, 20, 26);
            } else if (!rec) {                   /* the slipmat, its print turning */
                c = mat;
                if ((r4 >= 76 && r4 < 82) || (r4 >= 40 && r4 < 112 && ra < 6))
                    c = mat_hi;
                if (r4 < 6)
                    c = K_METAL;
            } else if (r4 < 6) {                 /* the spindle */
                c = K_METAL;
            } else if (r4 < 46) {                /* the label: turning; two lines of print, a ring, a mark */
                c = lab[0];
                if (r4 >= 40)
                    c = lab[1];
                else if ((r4 >= 18 && r4 < 24 && ra > 150 && ra < 230) || (r4 >= 28 && r4 < 33 && ra > 160 && ra < 220))
                    c = lab[2];
                else if (r4 >= 28 && r4 < 38 && ra > 16 && ra < 44)
                    c = lab[3];
            } else {                             /* the grooves, the gaps between the cuts, the sheen */
                int g = (r4 >> 1) & 1, s = sheen_tab[a];
                if (r4 < 52 || (r4 > 84 && r4 < 88) || (r4 > 108 && r4 < 111) || r4 > 128)
                    g = 0, s += 1;
                c = vin[g][s > 9 ? 9 : s];
            }
            row[sx] = swap16(c);
        }
    }
}

/* the tonearms: deck A's pivot at its top right, deck B's mirrored at its top left, so both lean in to the mixer.
 * Playing, the needle crosses the record as it goes (outer groove to inner); cued, it waits on the lead-in; with no
 * record it rests beside the platter */
#define ARM_L 50.0f
static float arm_px(int d) { return d ? (float)PL_X[1] : (float)(PL_X[0] + PL_BOX); }
#define ARM_PY 6.0f
static float pl_cx(int d) { return (float)PL_X[d] + (float)PL_BOX * 0.5f; }
#define PL_CY ((float)PL_Y + (float)PL_BOX * 0.5f)
static float arm_target(int d)
{
    float px = arm_px(d), dx = pl_cx(d) - px, dy = PL_CY - ARM_PY, dist = fm_sqrtf(dx * dx + dy * dy), a, h, mx, my,
          nx, ny, rn, p, rest = 0.25f;
    const en_deck_t *k = &en_deck[d];
    if (!k->loaded)
        return rest;
    p = k->on && k->bars ? fm_clampf((float)k->bar / (float)k->bars, 0.0f, 1.0f) : 0.0f;
    rn = k->on ? 31.0f - 17.0f * p : 32.5f;
    a = (ARM_L * ARM_L - rn * rn + dist * dist) / (2.0f * dist);
    h = fm_sqrtf(fm_maxf(ARM_L * ARM_L - a * a, 0.0f));
    mx = px + a * dx / dist;
    my = ARM_PY + a * dy / dist;
    if (d) {                                     /* the meeting on the far side from the mixer */
        nx = mx - h * dy / dist;
        ny = my + h * dx / dist;
    } else {
        nx = mx + h * dy / dist;
        ny = my - h * dx / dist;
    }
    return turns(ny - ARM_PY, nx - px);
}
static void draw_arm(int d)
{
    float px = arm_px(d), a = ui.arm[d] * FM_TWO_PI, c = fm_cosf(a), s = fm_sinf(a), ex = px + c * (ARM_L - 8.0f),
          ey = ARM_PY + s * (ARM_L - 8.0f), hx, hy, h2 = a + (d ? -0.5f : 0.5f);
    disc(px + 1.5f, ARM_PY + 2.0f, 6.5f, RGB(6, 6, 12));   /* the base's shadow, the base */
    disc(px, ARM_PY, 6.0f, K_METAL_LO);
    disc(px, ARM_PY, 4.0f, K_METAL);
    rod(px - c * 10.0f, ARM_PY - s * 10.0f, px - c * 5.0f, ARM_PY - s * 5.0f, 3.0f, K_METAL_LO);   /* the weight */
    rod(px + 1.5f, ARM_PY + 2.0f, ex + 1.5f, ey + 2.0f, 1.1f, RGB(6, 6, 12));   /* the tube's shadow, the tube */
    rod(px, ARM_PY, ex, ey, 1.2f, K_METAL);
    hx = ex + fm_cosf(h2) * 9.0f;                /* the headshell, angled */
    hy = ey + fm_sinf(h2) * 9.0f;
    rod(ex, ey, hx, hy, 2.0f, RGB(70, 72, 84));
    disc(hx, hy, 1.1f, DECK_C[d]);
    disc(px, ARM_PY, 1.8f, K_METAL_LO);
}

/* the meters: 0..1 from the level (-30 dB .. +3) */
static float meter_of(float v)
{
    float db = fm_log2f(fm_maxf(v, 1e-4f)) * 6.0206f;
    return fm_clampf((db + 30.0f) / 33.0f, 0.0f, 1.0f);
}
#define SEGS 12
static void draw_meter(int32_t x, int32_t y, int32_t w, int32_t h, float v)
{
    int i, lit = (int)(v * (float)SEGS + 0.5f);
    int32_t sh = h / SEGS;
    for (i = 0; i < SEGS; i++) {
        uint16_t on = i >= SEGS - 1 ? K_RED : i >= SEGS - 3 ? K_ACC : K_TEAL;
        cv_rect(x, y + h - (i + 1) * sh + 1, w, sh - 1, i < lit ? on : mix(K_PANEL, on, 3));
    }
}

/* the mixer, between the decks: per channel the low EQ and the filter (a knob each), a fader and its meter; the
 * master's meters in the middle; the crossfader below; the chord now */
#define MX_X 84
#define MX_W 72
static const int16_t CH_X[2] = {97, 143};
static void draw_mixer(void)
{
    int d;
    char t[16];
    cv_round(MX_X, 1, MX_W, 162, 6, K_PANEL);
    cv_rect(MX_X + 4, 1, MX_W - 8, 1, K_PANEL_HI);
    text_c(120, 8, &FONT_T, "LOW", K_DIM);
    text_c(120, 32, &FONT_T, "FLT", K_DIM);
    for (d = 0; d < 2; d++) {
        const en_deck_t *k = &en_deck[d];
        int32_t cx = CH_X[d], fy;
        float f = fm_clampf(k->filter, -1.0f, 1.0f);
        uint16_t dc = k->on ? DECK_C[d] : K_DIM;
        arc(cx, 14, 8, 2.6f, 0.0f, fm_clampf(k->low, 0.0f, 1.0f), K_LINE, dc, K_PANEL);
        arc(cx, 38, 8, 2.6f, 0.5f, 0.5f + f * 0.5f, K_LINE, f < -0.02f || f > 0.02f ? K_TEXT : dc, K_PANEL);
        cv_rect(cx - 1, 54, 3, 70, RGB(8, 8, 14));   /* the fader's slot, its cap */
        fy = 54 + (int32_t)((1.0f - fm_clampf(k->fader, 0.0f, 1.0f)) * 62.0f + 0.5f);
        cv_round(cx - 7, fy, 15, 8, 2, K_METAL);
        cv_rect(cx - 6, fy + 3, 13, 2, dc);
        draw_meter(d ? 128 : 108, 54, 4, 72, ui.deck_vu[d]);
    }
    draw_meter(115, 54, 3, 72, ui.vu[0]);        /* the master, left and right */
    draw_meter(121, 54, 3, 72, ui.vu[1]);
    {                                            /* the crossfader */
        int32_t x = 101 + (int32_t)(fm_clampf(en_xfade, 0.0f, 1.0f) * 38.0f + 0.5f);
        cv_text(MX_X + 4, 133, &FONT_T, "A", en_deck[0].on ? K_ACC : K_DIM);
        cv_text(MX_X + MX_W - 10, 133, &FONT_T, "B", en_deck[1].on ? K_TEAL : K_DIM);
        cv_rect(99, 138, 42, 3, RGB(8, 8, 14));
        cv_round(x - 5, 132, 11, 14, 2, K_METAL);
        cv_rect(x, 134, 1, 10, mix(DECK_C[0], DECK_C[1], (int)(en_xfade * 16.0f + 0.5f)));
    }
    if (en_playing && en_deck[en_master].on) {
        en_chord_name(en_chord_root, en_chord_q, t);
        text_c(120, 148, &FONT_XS, t, K_TEXT);
    }
}

/* a title, in up to two lines of w px: words wrapped */
static void draw_title(int32_t x, int32_t y, int32_t w, const char *s, uint16_t c)
{
    char line[2][28];
    int ln = 0, n = 0, i = 0;
    line[0][0] = line[1][0] = 0;
    while (s[i] && ln < 2) {
        char word[24];
        int k = 0, keep_n = n;
        while (s[i] == ' ')
            i++;
        while (s[i] && s[i] != ' ' && k < 23)
            word[k++] = s[i++];
        word[k] = 0;
        if (!k)
            break;
        if (n) {
            line[ln][n++] = ' ';
            line[ln][n] = 0;
        }
        cat_s(&line[ln][n], word);
        if (text_w(&FONT_XS, line[ln]) > w && keep_n) {   /* this word goes on the next line */
            line[ln][keep_n] = 0;
            if (++ln >= 2)
                break;
            cat_s(line[ln], word);
            n = k;
        } else {
            n += k;
        }
    }
    cv_text(x, y, &FONT_XS, line[0], c);
    if (line[1][0])
        cv_text(x, y + 12, &FONT_XS, line[1], c);
}

/* under each deck: its badge and where the record is, its title, its mix, the phrase and the phrase's bars */
static void draw_info(int d)
{
    const en_deck_t *k = &en_deck[d];
    int32_t x = d ? 158 : 4, w = 78, i;
    uint16_t dc = DECK_C[d], tc = k->on ? K_TEXT : K_DIM;
    char t[28], *e;
    cv_round(x, 82, 13, 13, 3, k->on ? dc : K_LINE);
    text_c(x + 7, 84, &FONT_T, d ? "B" : "A", k->on ? K_BG : K_DIM);
    cv_text(x + 17, 84, &FONT_T, !k->loaded ? "EMPTY" : !k->on ? "CUED" : EN_SECT_NAME[k->sect], tc);
    if (!k->loaded)
        return;
    e = cat_s(t, EN_NOTE_NAME[k->key % 12]);
    cat_s(e, "m");
    cv_text(x + w - text_w(&FONT_XS, t), 82, &FONT_XS, t, k->on ? dc : K_DIM);
    en_title(k->seed, t);
    draw_title(x, 98, w, t, tc);
    cv_text(x, 123, &FONT_XS, en_mix_tag(k->seed, k->lead), K_DIM);
    if (!k->on)
        return;
    cat_u(cat_s(t, "PHRASE "), (uint32_t)k->bar / 16u + 1u);
    cv_text(x, 138, &FONT_T, t, K_DIM);
    for (i = 0; i < 16; i++) {                   /* the phrase's sixteen bars */
        int32_t b = (int32_t)(k->bar % 16u);
        cv_rect(x + i * 5, 152, 4, i % 4 ? 5 : 7, i < b ? mix(dc, K_BG, 5) : i == b ? dc : K_LINE);
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
    case B_HOME: L("THE SET"); L("ENERGY, FILTER, MIX, WARMTH"); break;
    case B_ENV: L("THE GROOVE"); L("TEMPO, SWING, MOOD, SPACE"); break;
    case B_GLO: L("THE LEVELS"); L("CHORDS, DRUMS, BASS, LEAD"); break;
    case B_EDIT: L("THE NEXT MIX:"); L("THE DJ BLENDS IN"); L(" A NEW RECORD"); break;
    case B_REC: L("HOLD: THIS RECORD"); L(" PLAYS ON"); L("AGAIN: THE DJ MIXES ON"); break;
    case B_OCTDN: L("THE KEYS AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE KEYS AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES EVERY SET"); L("KEEP HOLDING: THE SET"); L(" BACK TO ITS RECIPE"); break;
    case B_SEL: L("SELECT TURNS THE PAGES"); L("PRESETS: THE SET"); L("ALGORITHM: FILTER"); break;
    default: break;
    }
#undef L
    return n;
}

static void draw_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    cv_round(16, 18, 208, 120, 6, K_PANEL);
    cv_round(16, 18, 208, 26, 6, K_ACC);
    cv_rect(16, 38, 208, 6, K_ACC);
    text_c(120, 22, &FONT_B, BTN_LABEL[ui.help - 1], K_BG);
    for (k = 0; k < n; k++)
        text_c(120, 60 + k * 20, &FONT_S, l[k] + (l[k][0] == ' '), K_TEXT);
}

static void draw_main(void)
{
    uint32_t now = plat_ms(), h;
    float dt = (float)(now - ui.draw_t) * 0.001f;
    int d;
    if (ui.frame % 3u)                           /* 20 frames a second */
        return;
    ui.draw_t = now;
    if (dt > 0.2f)
        dt = 0.2f;
    for (d = 0; d < 2; d++) {
        ui.rot[d] = en_deck[d].rot;              /* the engine turns the platters: 33 1/3, started and stopped */
        ui.arm[d] += (arm_target(d) - ui.arm[d]) * fm_minf(dt * 3.0f, 1.0f);
        ui.deck_vu[d] += (meter_of(en_deck[d].meter) - ui.deck_vu[d]) * fm_minf(dt * 12.0f, 1.0f);
    }
    ui.vu[0] += (meter_of(en_vu_l) - ui.vu[0]) * fm_minf(dt * 12.0f, 1.0f);
    ui.vu[1] += (meter_of(en_vu_r) - ui.vu[1]) * fm_minf(dt * 12.0f, 1.0f);
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
    h = 2166136261u;
    for (d = 0; d < 2; d++) {
        const en_deck_t *k = &en_deck[d];
        h = hash(h, (uint32_t)(ui.rot[d] * 256.0f) | (uint32_t)(ui.arm[d] * 4000.0f) << 8);
        h = hash(h, (uint32_t)(ui.deck_vu[d] * 24.0f) | (uint32_t)(k->fader * 62.0f) << 8 |
                        (uint32_t)(k->low * 40.0f) << 16 | (uint32_t)((k->filter + 1.0f) * 40.0f) << 24);
        h = hash(h, k->seed ^ (uint32_t)k->bar << 20 ^ (uint32_t)k->sect << 16 ^ (uint32_t)k->on << 28 ^
                        (uint32_t)k->loaded << 29);
    }
    h = hash(h, (uint32_t)(ui.vu[0] * 24.0f) | (uint32_t)(ui.vu[1] * 24.0f) << 8 | (uint32_t)(en_xfade * 44.0f) << 16);
    h = hash(h, en_chord_root | (uint32_t)en_chord_q << 4 | (uint32_t)en_playing << 8);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    for (d = 0; d < 2; d++) {
        draw_platter(d);
        draw_info(d);
    }
    draw_mixer();
    draw_arm(0);
    draw_arm(1);
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
        xt = xd - 6 - text_w(&FONT_T, t);
        cv_rect(4, 4, xt - 9, 1, K_LINE);
        cv_text(xt, 0, &FONT_T, t, K_ACC);
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 7, 2, 5, 5, 2, k == ui.view ? K_ACC : K_LINE);
    }
    for (i = 0; i < 4; i++) {
        int k = (int)VIEW_KNOB[ui.view][i], lo, hi, v, hot = now_touch && ui.touched == (int)i;
        int32_t x = (int32_t)i * 60, cx = x + 30;
        char t[16];
        uint16_t bg = hot ? K_PANEL : K_BG, fill = hot ? K_ACC : mix(K_ACC, K_TEAL, 9);
        if (i)
            cv_rect(x, 14, 1, 36, K_LINE);
        if (hot)
            cv_round(x + 3, 9, 54, 45, 6, K_PANEL);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        caps(t, en_param_info(k)->name, sizeof t);
        text_c(cx, 10, &FONT_T, t, hot ? K_TEXT : K_DIM);
        if (hi - lo < 6) {
            int n = hi - lo + 1, j, pw = 9, gap = 3, w0 = n * (pw + gap) - gap;
            for (j = 0; j < n; j++)
                cv_round(cx - w0 / 2 + j * (pw + gap), 26, pw, 7, 2, j == v - lo ? fill : K_LINE);
        } else if (lo < 0) {                     /* bipolar: from the middle */
            arc(cx, 29, 9, 3.0f, 0.5f, (float)(v - lo) / (float)(hi - lo), K_LINE, fill, bg);
        } else {
            arc(cx, 29, 9, 3.0f, 0.0f, (float)(v - lo) / (float)(hi - lo), K_LINE, fill, bg);
        }
        knob_text(k, t);
        text_c(cx, 39, text_w(&FONT_S, t) > 56 ? &FONT_XS : &FONT_S, t, hot ? K_ACC : K_TEXT);
    }
    cv_blit(0, KNB_Y);
}

/* the lights: the page's button, PLAY, HOLD; the chord's notes on the keys */
static void leds(void)
{
    uint32_t b = 0, k = 0, i, pcs = en_chord_pcs;
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (en_hold_on)
        b |= 1u << B_REC;
    if (en_mixing)
        b |= 1u << B_EDIT;
    if (proj.leds && en_playing)
        for (i = 0; i < NKEYS; i++)
            if (pcs >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    plat_glow(proj.leds == 1);
    plat_leds(b, k | ui.keys);
}

/* AUTOSAVE (the flash wears, and a write can glitch the sound): the settings once the knobs have rested 10 s, at
 * most every 5 minutes; where the set is (the record) at most hourly. Both at a quiet moment: a breakdown (the kick
 * out) with no blend under way, or stopped. SAVE saves at once */
#define AUTOSAVE_REST 10000u
#define AUTOSAVE_SETTINGS 300000u
#define AUTOSAVE_TRACK 3600000u
static void autosave(void)
{
    uint32_t now = plat_ms(), since = now - ui.saved_t;
    int quiet = !en_playing || (!en_mixing && en_deck[en_master].sect == S_BREAK), want;
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
 * knob as it turns; a help card as it comes up; each record the set moves onto (its title, mix, key, tempo);
 * playing and stopped; what a button did (say). WHERE is the whole screen in two: the page and the header, then
 * the decks. */
static struct {
    uint8_t view, help, started, master;
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
    int k = VIEW_KNOB[view][i];
    sp_cat(en_param_info(k)->name);
    sp_cat(" ");
    knob_text(k, t);
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
    sp_cat(". Set ");
    sp_cat(en_style_name(proj.style));
    sp_cat(sp_tp.playing ? ", playing" : ", stopped");
    if (en_hold_on)
        sp_cat(", hold on");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static void sp_deck(int d)                       /* "Deck A, record 12: Smoky Love, Club Mix. B minor" */
{
    const en_deck_t *k = &en_deck[d];
    char t[28];
    sp_cat(d ? "Deck B" : "Deck A");
    if (!k->loaded) {
        sp_cat(" empty");
        return;
    }
    sp_cat(", record ");
    sp_int(k->no);
    sp_cat(": ");
    en_title(k->seed, t);
    sp_cat(t);
    sp_cat(", ");
    sp_cat(en_mix_tag(k->seed, k->lead));
    sp_cat(". ");
    sp_cat(EN_NOTE_NAME[k->key % 12]);
    sp_cat(" minor");
}
static void sp_track_text(void)                  /* the record the set is on, its tempo */
{
    sp_n = 0;
    sp_buf[0] = 0;
    sp_deck(en_master);
    sp_cat(", ");
    sp_int(en_bpm);
    sp_cat(" BPM");
}
static void sp_decks(void)                       /* WHERE's second part: both decks, where each is, the blend */
{
    int d;
    char t[12];
    sp_n = 0;
    sp_buf[0] = 0;
    for (d = 0; d < 2; d++) {
        const en_deck_t *k = &en_deck[d];
        if (d)
            sp_cat(". ");
        sp_deck(d);
        if (k->loaded && !k->on) {
            sp_cat(", cued");
        } else if (k->on) {
            int i;
            const char *s = EN_SECT_NAME[k->sect];
            for (i = 0; i < 11 && s[i]; i++)     /* "Groove" */
                t[i] = (char)(i && s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i]);
            t[i] = 0;
            sp_cat(", ");
            sp_cat(t);
            sp_cat(", phrase ");
            sp_int((uint32_t)k->bar / 16u + 1u);
        }
    }
    sp_cat(en_mixing ? ". Mixing" : "");
    sp_cat(". ");
    sp_int(en_bpm);
    sp_cat(" BPM");
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
    sp_cat("SET ");
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
    sp.master = en_master;
    sp.track = en_deck[en_master].no;
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
            sp_decks();
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
        ann_say(en_playing ? "PLAYING" : "STOPPED", 0, 0);
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
    if ((en_master != sp.master || en_deck[en_master].no != sp.track) && en_deck[en_master].on) {
        sp.master = en_master;                   /* the set moved onto a new record: queued */
        sp.track = en_deck[en_master].no;
        sp_track_text();
        ann_say(sp_buf, 0, 0);
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card, title and message as a screen reader gets them (the "speakdump" command) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}, {"NEXT MIX", 0},
                                         {"STOPPED", 0}, {"PLAYING", 0}, {"HOLD", "ON"}, {"HOLD", "OFF"},
                                         {"SAVED", 0}, {"KEYS OCTAVE", "-2"}, {"KEYS OCTAVE", "-1"},
                                         {"KEYS OCTAVE", "0"}, {"KEYS OCTAVE", "+1"}, {"KEYS OCTAVE", "+2"}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}, {"KEYS OCTAVE", "LOWEST"},
                                          {"KEYS OCTAVE", "HIGHEST"}, {"MIXING", "ALREADY"}};
    project_t keep = proj;
    en_deck_t keep_deck[2];
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty, keep_play = sp_tp.playing;
    uint8_t keep_hold = en_hold_on, keep_master = en_master, keep_mixing = en_mixing;
    int v, i, x, lo, hi;
    char t[64];
    memcpy(keep_deck, (const void *)en_deck, sizeof keep_deck);
    for (v = 0; v < NVIEWS; v++) {
        ui.view = (uint8_t)v;
        sp_page(0);
        sp_print("SCREEN", sp_buf);
        for (i = 0; i < 4; i++) {
            int k = VIEW_KNOB[v][i];
            knob_range(k, &lo, &hi);
            for (x = lo; x <= hi; x++) {
                proj.par[k] = (int16_t)x;
                sp_n = 0;
                sp_knob(v, i);
                sp_print("KNOB", sp_buf);
            }
            proj = keep;
        }
    }
    for (x = 0; x < 8; x++) {                    /* WHERE: playing, stopped, unsaved, hold, a deck cued, a blend */
        ui.view = (uint8_t)(x % NVIEWS);
        sp_tp.playing = (uint8_t)(x & 1);
        ui.dirty = (uint8_t)(x >> 1 & 1);
        en_hold_on = (uint8_t)(x == 5);
        en_master = (uint8_t)(x >> 2 & 1);
        en_mixing = (uint8_t)(x == 6);
        for (i = 0; i < 2; i++) {
            en_deck[i].loaded = (uint8_t)(i == en_master || x % 3 != 0 || x == 6);
            en_deck[i].on = (uint8_t)(i == en_master || x == 6);
            en_deck[i].sect = (uint8_t)(1 + (x + i) % 7);
            en_deck[i].bar = (uint16_t)(x * 9 + i * 20);
            en_deck[i].no = (uint32_t)(x * 2 + i + 1);
            en_deck[i].seed = en_track_rand(0x44485331u, en_deck[i].no);
            en_deck[i].key = (uint8_t)((x * 5 + i * 7) % 12);
            en_deck[i].lead = (uint8_t)((x + i) % 3);
        }
        proj.style = (uint8_t)(x * 3 % NSTYLES);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_decks();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
    for (x = 0; x < 300; x++) {                  /* records: their titles, mixes, keys */
        en_master = 0;
        en_deck[0].loaded = en_deck[0].on = 1;
        en_deck[0].no = (uint32_t)x + 1u;
        en_deck[0].seed = en_track_rand(0x44485331u, (uint32_t)x + 1u);
        en_deck[0].key = (uint8_t)(x % 12);
        en_deck[0].lead = (uint8_t)(x % 3);
        sp_track_text();
        sp_print("TRACK", sp_buf);
    }
    memcpy((void *)en_deck, keep_deck, sizeof keep_deck);
    en_master = keep_master;
    en_mixing = keep_mixing;
    en_hold_on = keep_hold;
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
    for (x = -50; x <= 50; x += 5) {
        char o[12];
        en_param_text(P_FILTER, x, o);
        msg_text("FILTER", o, t);
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
    ui.saved_track = proj.track_no + 1u;          /* the record the boot starts: nothing new to keep yet */
    ui.act_t = plat_ms();
    pl_tables();
    ui.arm[0] = ui.arm[1] = 0.25f;
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
