/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * DUB DESK: ashtonwallen, 2026 */
/* DUB DESK's UI: the panel and the screen (from AMB-1's, by way of LOFI RADIO: its knobs, help cards, messages and
 * screen reader). Part of the unity build (after gfx.c and project.c); the host simulator includes it too.
 *
 *   PLAY           the tape rolls / stops                EDIT   the next riddim (stripped down, then on)
 *   REC            REPEAT: this riddim plays on           OCT- / OCT+   the keys' octave
 *   FX             THROW: what is playing, into the echo  LFO    SPRING: kick the tank
 *   PRESETS        the style (each kept as left)          ALGORITHM     Tone
 *   HOME ENV GLO   the pages: Desk, Band, Room (SELECT steps through them)
 *   KNOB 1-4       the page's four values
 *   keys           play along on the organ
 *   SAVE           saves (it also saves by itself at a quiet moment: see autosave); held 1.5 s: the style back to
 *                  its recipe */
#include "fastmath.h"

enum { V_DESK, V_BAND, V_ROOM, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Desk", "Band", "Room"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_GLO};
enum { K_NONE = 100 };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_ENGINEER, P_ECHO, P_TIME, P_BASS},
    {P_DRUMS, P_SKANK, P_ORGAN, P_LEAD},
    {P_SPRING, P_TONE, P_TEMPO, P_LENGTH},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Engineer", "Echo", "Time", "Bass"},
    {"Drums", "Skank", "Organ", "Lead"},
    {"Spring", "Tone", "Tempo", "Length"},
};

/* the screen's colours: a studio at night: a dark desk, cream caps, amber lamps; red, gold and green on the first
 * three channels */
#define K_BG RGB(17, 19, 18)
#define K_PANEL RGB(44, 49, 46)
#define K_DESK RGB(36, 40, 38)
#define K_DESK_HI RGB(62, 68, 64)
#define K_DESK_LO RGB(22, 25, 24)
#define K_LINE RGB(70, 77, 72)
#define K_DIM RGB(146, 152, 142)
#define K_TEXT RGB(238, 232, 212)
#define K_ACC RGB(248, 176, 58)
#define K_RED RGB(214, 66, 50)
#define K_GREEN RGB(84, 172, 90)
#define K_ECHO RGB(62, 82, 70)
#define K_ECHO_HI RGB(88, 110, 96)
#define K_TAPE RGB(104, 66, 40)
#define K_TAPE_HI RGB(146, 98, 62)
#define K_STEEL RGB(128, 134, 138)
#define K_STEEL_LO RGB(70, 74, 78)
#define K_COIL RGB(196, 204, 210)
#define K_MASK RGB(226, 212, 168)
#define K_MASK_LO RGB(196, 180, 136)
#define K_INK RGB(34, 30, 40)
#define K_LOW RGB(210, 92, 64)
/* the eight strips: six channels, the echo's return, the spring's */
#define NSTRIP 8
static const char *const STRIP_NAME[NSTRIP] = {"DRUM", "BASS", "GTR", "ORGN", "MEL", "HORN", "ECHO", "SPRG"};
static const uint8_t STRIP_RGB[NSTRIP][3] = {
    {214, 72, 56}, {240, 200, 64}, {86, 176, 92}, {232, 222, 192}, {110, 172, 228}, {216, 132, 82},
    {248, 176, 58}, {176, 186, 196},
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
    uint32_t saved_track;              /* the version the last save kept */
    char msg[2][28];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    uint32_t keys_on;                  /* the keys sounding (played along), key bit */
    uint32_t draw_t;
    float fad[NSTRIP], met[NSTRIP], snd[NSTRIP];   /* the strips as drawn */
    float tape;                        /* the tape loop's travel (px) */
    float shake, spring_t;             /* the tank: its shake, its springs' time */
    uint32_t move_no;                  /* the engineer's last move seen */
    uint8_t move;
    uint32_t move_t;
    float last_send[EN_NCH];
} ui;

/* the echo's blips (a throw on the tape, from the record head to the play head) and their ripples */
#define NBLIP 10
#define NRIP 8
static struct {
    float ph, amp;
} blip[NBLIP];
static struct {
    float r, amp;
} rip[NRIP];

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
static int knob_get(int k) { return k < P_NPARAMS ? proj.par[k] : 0; }
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = en_param_info(k)->lo;
        *hi = en_param_info(k)->hi;
    } else {
        *lo = *hi = 0;
    }
}
static void knob_text(int k, char *b)
{
    if (k < P_NPARAMS)
        en_param_text(k, knob_get(k), b);
    else
        b[0] = 0;
}
static void knob_set(int k, int v)
{
    int lo, hi;
    if (k >= P_NPARAMS)
        return;
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
    if (!e || k == K_NONE)
        return;
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------- actions --- */
static void sp_style(void);
static void set_style(int i)                    /* the style playing kept as it is; style i as it was left */
{
    project_style(i);
    project_apply();
    en_skip();                                   /* on to a riddim in the new style */
    if (!en_playing)
        en_play(1);
    sp_tp.playing = 1;
    say_as("STYLE", en_style_name(proj.style), 0);
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
        say("NEXT RIDDIM", 0);
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
            say("STOPPED", 0);
        } else {
            en_play(1);
            say("ROLLING", 0);
        }
        break;
    case B_REC:
        say("REPEAT", en_repeat_on ? "OFF" : "ON");
        en_repeat(!en_repeat_on);
        break;
    case B_FX:
        en_throw();
        say("ECHO THROW", 0);
        break;
    case B_LFO:
        en_crash();
        say("SPRING", 0);
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
 * another button puts it away. PLAY, FX and LFO act on their press; SEL, and the buttons with no job, have no card */
#define HELP_LINES 4
#define HELP_HOLD 900u
#define RESET_HOLD 1500u
static int help_lines(int b, const char *l[HELP_LINES]);
static int on_press(int i) { return i == B_PLAY || i == B_FX || i == B_LFO; }
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
            if (on_press((int)i))
                button((int)i);
        } else if (!(ui.btn_used & m) && !on_press((int)i)) {
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
        if (ui.view == V_ROOM) {
            ui.touched = 1;
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
        ui.reset_done = 1;                       /* held: the style back to its recipe (not saved) */
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
/* three bands: the header (28 px), the desk (144), the knobs (68) */
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
static uint16_t strip_c(int i, int shade)        /* a strip's colour, darker by shade (0..16) */
{
    const uint8_t *c = STRIP_RGB[i % NSTRIP];
    return mix(RGB(c[0], c[1], c[2]), K_DESK_LO, shade);
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

/* the header: the style in a tag; on the right the battery, the tempo with a beat light, REPEAT, unsaved */
static void draw_header(void)
{
    char t[16];
    int msg = plat_ms() < ui.msg_until, beat = (int)(en_beats & 1u);
    uint32_t h = hash(hash(hash(2166136261u, proj.style | (uint32_t)en_bpm << 8 | (uint32_t)en_playing << 16 |
                                                   (uint32_t)en_repeat_on << 17 | (uint32_t)ui.dirty << 18 |
                                                   (uint32_t)msg << 19 | (uint32_t)beat << 20),
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
            cv_round(x, 10, 8, 8, 4, beat ? K_RED : K_LINE);
        } else {                                 /* stopped: a square */
            cv_rect(x, 10, 8, 8, K_DIM);
        }
        if (en_repeat_on) {
            x -= text_w(&FONT_XS, "REPEAT") + 14;
            cv_round(x, 7, text_w(&FONT_XS, "REPEAT") + 8, 15, 5, K_GREEN);
            cv_text(x + 4, 7, &FONT_XS, "REPEAT", K_BG);
        }
        w = text_w(&FONT_B, en_style_name(proj.style)) + 12;
        if (4 + w < x - 4) {
            cv_round(4, 4, w, 20, 6, K_ACC);
            cv_text(10, 6, &FONT_B, en_style_name(proj.style), K_BG);
        }
    }
    cv_blit(0, 0);
}

/* ---- the tape echo: a loop of tape past three heads (erase, record, play). A throw is a blip written at the record
 * head; it rides to the play head (the echo's time), is heard (a ripple), and is written again, fainter (feedback) */
#define EC_X 3
#define EC_Y 2
#define EC_W 136
#define EC_H 44
#define LOOP_X0 (EC_X + 22)                      /* the loop: a stadium, its ends' centres */
#define LOOP_X1 (EC_X + 114)
#define LOOP_YC (EC_Y + 20)
#define LOOP_R 14
#define REC_HX (EC_X + 46)                       /* the heads, under the loop's bottom run */
#define PLAY_HX (EC_X + 104)
#define HEAD_Y (LOOP_YC + LOOP_R + 2)
static uint8_t rip_d4[EC_W * EC_H] PLAT_POOL;    /* each pixel's distance from the play head, in quarter pixels */
static void echo_tables(void)
{
    int x, y;
    for (y = 0; y < EC_H; y++)
        for (x = 0; x < EC_W; x++) {
            float dx = (float)(EC_X + x) + 0.5f - (float)PLAY_HX, dy = (float)(EC_Y + y) + 0.5f - (float)HEAD_Y;
            float d = fm_sqrtf(dx * dx + dy * dy) * 4.0f;
            rip_d4[y * EC_W + x] = (uint8_t)(d > 255.0f ? 255.0f : d);
        }
}

static void stadium(float x0, float x1, float yc, float r, uint16_t c)
{
    disc(x0, yc, r, c);
    disc(x1, yc, r, c);
    cv_rect((int32_t)x0, (int32_t)(yc - r + 0.5f), (int32_t)(x1 - x0), (int32_t)(2.0f * r), c);
}

static void draw_echo(void)
{
    int i, x, y;
    char t[12];
    float ro = (float)LOOP_R + 1.5f, ri = (float)LOOP_R - 1.5f, pos;
    cv_round(EC_X, EC_Y, EC_W, EC_H, 5, K_DESK_LO);
    cv_round(EC_X, EC_Y, EC_W, EC_H - 1, 5, K_ECHO);
    cv_rect(EC_X + 5, EC_Y, EC_W - 10, 1, K_ECHO_HI);
    /* the loop of tape, its run marked so it is seen to move */
    stadium((float)LOOP_X0, (float)LOOP_X1, (float)LOOP_YC, ro, K_TAPE);
    stadium((float)LOOP_X0, (float)LOOP_X1, (float)LOOP_YC, ri, mix(K_ECHO, K_DESK_LO, 6));
    pos = ui.tape - fm_floorf(ui.tape / 9.0f) * 9.0f;
    for (x = LOOP_X0 + (int)pos; x < LOOP_X1; x += 9) {
        cv_rect(x, LOOP_YC + LOOP_R - 1, 2, 3, K_TAPE_HI);              /* the bottom run, left to right */
        cv_rect(LOOP_X0 + LOOP_X1 - x, LOOP_YC - LOOP_R - 1, 2, 3, K_TAPE_HI);   /* the top, back */
    }
    /* the heads: erase, record, play */
    {
        static const int16_t HX[3] = {EC_X + 32, REC_HX, PLAY_HX};
        for (i = 0; i < 3; i++) {
            cv_round(HX[i] - 4, HEAD_Y - 1, 9, 7, 2, i == 2 ? K_STEEL : K_STEEL_LO);
            cv_rect(HX[i], HEAD_Y - 1, 1, 2, K_DESK_LO);
        }
    }
    /* inside the loop: its name, the time */
    text_c((LOOP_X0 + LOOP_X1) / 2, LOOP_YC - 11, &FONT_T, "TAPE ECHO", mix(K_TEXT, K_ECHO, 6));
    en_param_text(P_TIME, proj.par[P_TIME], t);
    text_c((LOOP_X0 + LOOP_X1) / 2, LOOP_YC - 3, &FONT_S, t, K_TEXT);
    /* the blips on their way to the play head */
    for (i = 0; i < NBLIP; i++)
        if (blip[i].amp > 0.0f) {
            float bx = (float)REC_HX + (float)(PLAY_HX - REC_HX) * blip[i].ph;
            disc(bx, (float)(LOOP_YC + LOOP_R), 2.2f + 1.3f * blip[i].amp, mix(K_TAPE, K_ACC, (int)(4.0f + 12.0f * blip[i].amp)));
        }
    /* the ripples from the play head */
    {
        int any = 0;
        for (i = 0; i < NRIP; i++)
            any |= rip[i].amp > 0.0f;
        if (any)
            for (y = 0; y < EC_H - 1; y++)
                for (x = 1; x < EC_W - 1; x++) {
                    float d = (float)rip_d4[y * EC_W + x] * 0.25f, a = 0.0f;
                    for (i = 0; i < NRIP; i++)
                        if (rip[i].amp > 0.0f) {
                            float e = fm_fabsf(d - rip[i].r);
                            if (e < 1.6f)
                                a += rip[i].amp * (1.6f - e) * (1.0f / 1.6f);
                        }
                    if (a > 0.02f)
                        px_blend(EC_X + x, EC_Y + y, K_ACC, (uint32_t)(fm_minf(a, 1.0f) * 13.0f));
                }
    }
}

/* ---- the spring tank: two springs between their transducers, shaking with the reverb; kicked, the tank jumps */
#define SP_X 143
#define SP_Y 2
#define SP_W 94
#define SP_H 44
static void draw_spring(void)
{
    int i, s, ox = (int)(ui.shake * 3.0f * fm_sinf((float)ui.frame * 2.7f));
    float m = fm_clampf(en_spring * 1.3f + ui.shake * 1.5f, 0.0f, 2.2f), t = ui.spring_t;
    cv_round(SP_X, SP_Y, SP_W, SP_H, 5, K_DESK_LO);
    cv_round(SP_X + ox, SP_Y, SP_W, SP_H - 1, 5, K_STEEL_LO);
    cv_round(SP_X + 3 + ox, SP_Y + 3, SP_W - 6, SP_H - 7, 3, RGB(28, 30, 33));
    text_c(SP_X + SP_W / 2 + ox, SP_Y + SP_H - 15, &FONT_T, "SPRING REVERB", K_DIM);
    for (i = 0; i < 2; i++) {
        float x0 = (float)(SP_X + 13 + ox), x1 = (float)(SP_X + SP_W - 13 + ox), yc = (float)(SP_Y + 10 + i * 11);
        float len = x1 - x0;
        cv_rect((int32_t)x0 - 8, (int32_t)yc - 4, 7, 9, K_STEEL);   /* the transducers */
        cv_rect((int32_t)x1 + 1, (int32_t)yc - 4, 7, 9, K_STEEL);
        cv_rect((int32_t)x0 - 8, (int32_t)yc - 4, 7, 1, K_COIL);
        cv_rect((int32_t)x1 + 1, (int32_t)yc - 4, 7, 1, K_COIL);
        for (s = 0; s <= (int)len * 3; s++) {   /* the coil: a helix seen from the side, its near half lit */
            float x = x0 + (float)s * (1.0f / 3.0f), u = (x - x0) / len, ph = (x - x0) * (FM_TWO_PI / 5.0f);
            float disp = m * (2.6f * fm_sinf(FM_PI * u) * fm_sinf(t * 13.0f + (float)i * 1.7f) +
                              1.2f * fm_sinf(3.0f * FM_PI * u) * fm_sinf(t * 29.0f + (float)i));
            float yy = yc + disp + 3.0f * fm_sinf(ph), c = fm_cosf(ph);
            uint16_t col = c > 0.0f ? mix(K_STEEL, K_COIL, (int)(c * 16.0f)) : mix(K_STEEL_LO, K_STEEL, (int)((1.0f + c) * 10.0f));
            px_blend((int32_t)x, (int32_t)fm_floorf(yy), col, c > 0.0f ? 16u : 9u);
        }
    }
}

/* ---- the scribble strip: masking tape with the riddim's name in marker; the engineer's move as he makes it */
#define TAPE_Y 49
static void draw_scribble(void)
{
    static const char *const MV_WORD[] = {"", "ECHO THROW", "FEEDBACK", "SPRING!", "HIGH-PASS", "SIREN", "DROP", "BACK IN", "STRIP"};
    char t[24], c[12];
    int x;
    uint32_t now = plat_ms();
    cv_rect(6, TAPE_Y + 1, 228, 15, K_MASK_LO);
    cv_rect(6, TAPE_Y, 228, 15, K_MASK);
    for (x = 0; x < 5; x++) {                    /* torn ends */
        cv_rect(6, TAPE_Y + x * 3, 1 + (x & 1) * 2, 2, K_DESK);
        cv_rect(231 + (x & 1), TAPE_Y + x * 3 + 1, 3, 2, K_DESK);
    }
    caps(t, en_riddim_name(en_riddim), sizeof t);
    cv_text(12, TAPE_Y - 1, &FONT_B, t, K_INK);
    if (ui.move && now - ui.move_t < 1600u) {
        const char *w = MV_WORD[ui.move < 9 ? ui.move : 0];
        cv_text(228 - text_w(&FONT_XS, w), TAPE_Y + 1, &FONT_XS, w, K_RED);
    } else {
        char *e = cat_s(t, "V");
        e = cat_u(e, en_track_no);
        cat_s(cat_s(e, "  "), en_chord_name(en_chord_root, en_chord_q, c));
        cv_text(228 - text_w(&FONT_XS, t), TAPE_Y + 1, &FONT_XS, t, mix(K_INK, K_MASK, 5));
    }
}

/* ---- the channel strips: a send knob (lit when thrown), a meter, a fader, the name lit while it is up */
#define STRIP_Y 67
#define FAD_TOP (STRIP_Y + 16)
#define FAD_BOT (STRIP_Y + 64)
static void draw_strips(void)
{
    int i, k;
    for (i = 0; i < NSTRIP; i++) {
        int x0 = i * 30, cx = x0 + 15, on = ui.fad[i] > 0.02f || (i >= 6);
        float f = fm_clampf(ui.fad[i], 0.0f, 1.0f), cy;
        uint16_t col = strip_c(i, 0);
        const char *name = STRIP_NAME[i];
        if (i == 5 && en_siren > 0.05f)
            name = "SIRN";
        if (i == 6)                              /* the returns stand apart */
            cv_rect(x0, STRIP_Y - 1, 60, MAIN_H - STRIP_Y + 1, RGB(30, 33, 32));
        if (i)
            cv_rect(x0, STRIP_Y + 2, 1, MAIN_H - STRIP_Y - 4, i == 6 ? K_DESK_HI : K_DESK_LO);
        /* the send */
        {
            float s = fm_clampf(ui.snd[i], 0.0f, 1.0f), a = (0.375f + 0.75f * (i < 6 ? s : i == 6 ? (float)proj.par[P_TIME] / 3.0f : (float)proj.par[P_SPRING] * 0.01f)) * FM_TWO_PI;
            if (s > 0.1f && i < 6)
                disc((float)cx, (float)(STRIP_Y + 6), 6.5f + 1.5f * s, mix(K_DESK, K_ACC, (int)(s * 14.0f)));
            disc((float)cx, (float)(STRIP_Y + 6), 5.0f, K_DESK_LO);
            disc((float)cx, (float)(STRIP_Y + 6), 4.0f, mix(K_DESK_HI, K_TEXT, 3));
            rod((float)cx, (float)(STRIP_Y + 6), (float)cx + fm_cosf(a) * 3.4f, (float)(STRIP_Y + 6) + fm_sinf(a) * 3.4f, 0.7f,
                s > 0.1f && i < 6 ? K_RED : K_INK);
        }
        /* the meter: nine lamps */
        for (k = 0; k < 9; k++) {
            int y = FAD_BOT - 4 - k * 5, lit = ui.met[i] > ((float)k + 0.5f) / 9.0f;
            uint16_t lc = k < 6 ? K_GREEN : k < 8 ? K_ACC : K_RED;
            cv_rect(x0 + 5, y, 4, 4, lit ? lc : mix(lc, K_DESK_LO, 12));
        }
        /* the fader: its slot, its scale, its cap */
        cv_rect(cx + 2, FAD_TOP, 2, FAD_BOT - FAD_TOP, K_DESK_LO);
        for (k = 0; k <= 4; k++)
            cv_rect(cx + 7, FAD_TOP + 2 + k * 11, k == 1 ? 4 : 2, 1, K_LINE);
        cy = (float)(FAD_BOT - 4) - f * (float)(FAD_BOT - FAD_TOP - 8);
        cv_round(cx - 3, (int32_t)cy - 3, 12, 8, 2, K_DESK_LO);
        cv_round(cx - 3, (int32_t)cy - 4, 12, 8, 2, on ? col : mix(col, K_DESK, 9));
        cv_rect(cx - 2, (int32_t)cy, 10, 1, mix(col, K_INK, 10));
        /* the name */
        text_c(cx, FAD_BOT + 1, &FONT_T, name, on ? col : K_LINE);
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
    case B_HOME: L("THE DESK"); L("ENGINEER, ECHO, TIME, BASS"); break;
    case B_ENV: L("THE BAND"); L("DRUMS, SKANK, ORGAN, LEAD"); break;
    case B_GLO: L("THE ROOM"); L("SPRING, TONE, TEMPO, LENGTH"); break;
    case B_EDIT: L("THE NEXT RIDDIM:"); L("STRIPPED DOWN, THEN ON"); break;
    case B_REC: L("REPEAT: THIS RIDDIM"); L(" PLAYS ON"); L("AGAIN: ON TO THE NEXT"); break;
    case B_FX: L("ECHO THROW:"); L("WHAT IS PLAYING"); L(" INTO THE TAPE ECHO"); break;
    case B_LFO: L("SPRING:"); L("KICK THE REVERB TANK"); break;
    case B_OCTDN: L("THE KEYS AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE KEYS AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES EVERY STYLE"); L("KEEP HOLDING: THE STYLE"); L(" BACK TO ITS RECIPE"); break;
    case B_SEL: L("SELECT TURNS THE PAGES"); L("PRESETS: THE STYLE"); L("ALGORITHM: TONE"); break;
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
        text_c(120, 56 + k * 20, &FONT_S, l[k] + (l[k][0] == ' '), K_TEXT);
}

/* the desk's motion: the faders slide, the meters fall, the tape runs, the blips ride, the ripples spread */
static void animate(float dt)
{
    int i, c;
    float es = fm_maxf(en_echo_s, 0.1f), fb = en_echo_fb;
    for (i = 0; i < NSTRIP; i++) {
        float tf, tm, ts;
        if (i < EN_NCH) {
            tf = en_fader[i];
            tm = en_meter[i];
            ts = en_send[i];
        } else if (i == 6) {
            tf = fb / 0.97f;
            tm = en_echo_out;
            ts = 0.0f;
        } else {
            tf = (float)proj.par[P_SPRING] * 0.01f;
            tm = en_spring;
            ts = 0.0f;
        }
        tm = fm_sqrtf(fm_clampf(tm, 0.0f, 1.0f));   /* the lamps: roughly in decibels */
        ui.fad[i] += (tf - ui.fad[i]) * fm_minf(dt * 11.0f, 1.0f);
        ui.met[i] = tm > ui.met[i] ? tm : ui.met[i] + (tm - ui.met[i]) * fm_minf(dt * 7.0f, 1.0f);
        ui.snd[i] = ts > ui.snd[i] ? ts : ui.snd[i] + (ts - ui.snd[i]) * fm_minf(dt * 4.0f, 1.0f);
    }
    for (c = 0; c < EN_NCH; c++) {               /* a throw: a blip on the tape */
        if (en_send[c] > 0.9f && ui.last_send[c] <= 0.9f)
            for (i = 0; i < NBLIP; i++)
                if (blip[i].amp <= 0.0f) {
                    blip[i].amp = 1.0f;
                    blip[i].ph = 0.0f;
                    break;
                }
        ui.last_send[c] = en_send[c];
    }
    for (i = 0; i < NBLIP; i++)
        if (blip[i].amp > 0.0f) {
            blip[i].ph += dt / es;
            if (blip[i].ph >= 1.0f) {            /* heard: a ripple; written again, fainter */
                int k, w = 0;
                for (k = 0; k < NRIP; k++)
                    if (rip[k].amp <= 0.0f || rip[k].r > rip[w].r)
                        w = k;
                for (k = 0; k < NRIP; k++)
                    if (rip[k].amp <= 0.0f) {
                        w = k;
                        break;
                    }
                rip[w].r = 1.0f;
                rip[w].amp = blip[i].amp;
                blip[i].ph -= 1.0f;
                blip[i].amp *= fb;
                if (blip[i].amp < 0.08f)
                    blip[i].amp = 0.0f;
            }
        }
    for (i = 0; i < NRIP; i++)
        if (rip[i].amp > 0.0f) {
            rip[i].r += dt * 58.0f;
            rip[i].amp *= fm_expf(-dt * 1.6f);
            if (rip[i].r > 62.0f || rip[i].amp < 0.04f)
                rip[i].amp = 0.0f;
        }
    ui.tape += dt * (float)(PLAY_HX - REC_HX) / es * (en_playing ? 1.0f : 0.0f);
    if (ui.tape > 9000.0f)
        ui.tape -= 9000.0f;
    ui.spring_t += dt;
    if (ui.spring_t > 100.0f)
        ui.spring_t -= 100.0f;
    ui.shake *= fm_expf(-dt * 5.0f);
    if (en_move_no != ui.move_no) {
        ui.move_no = en_move_no;
        ui.move = en_move;
        ui.move_t = plat_ms();
        if (en_move == MV_SPRING)
            ui.shake = 1.0f;
    }
}

static void draw_main(void)
{
    uint32_t now = plat_ms();
    float dt = (float)(now - ui.draw_t) * 0.001f;
    if (ui.frame % 3u)                           /* 20 frames a second */
        return;
    ui.draw_t = now;
    if (dt > 0.2f)
        dt = 0.2f;
    animate(dt);
    if (ui.help) {
        uint32_t h = hash(0x48454C50u, ui.help);
        if (h != ui.sig[1]) {
            ui.sig[1] = h;
            cv_begin(240, MAIN_H, K_BG);
            draw_help();
            cv_blit(0, MAIN_Y);
        }
        return;
    }
    ui.sig[1] = 0;
    cv_begin(240, MAIN_H, K_DESK);
    draw_echo();
    draw_spring();
    draw_scribble();
    draw_strips();
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
        uint16_t bg = hot ? K_PANEL : K_BG, fill = hot ? K_ACC : mix(K_ACC, K_RED, 6);
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
    if (ui.snd[0] > 0.5f || ui.snd[2] > 0.5f || ui.snd[4] > 0.5f || ui.snd[5] > 0.5f)
        b |= 1u << B_FX;                         /* the engineer's throw lights FX */
    if (proj.leds && en_playing)
        for (i = 0; i < NKEYS; i++)
            if (pcs >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    plat_glow(proj.leds == 1);
    plat_leds(b, k | ui.keys);
}

/* AUTOSAVE (the flash wears, and a write can glitch the sound): the settings once the knobs have rested 10 s, at
 * most every 5 minutes; where the session is (the version) at most hourly. Both at a quiet moment: the silent beat
 * at the end of a version, or stopped. SAVE saves at once */
#define AUTOSAVE_REST 10000u
#define AUTOSAVE_SETTINGS 300000u
#define AUTOSAVE_TRACK 3600000u
static int quiet_now(void)
{
    return !en_playing || (en_phase == PH_OUTRO && en_bar_in_block == 3 && en_step >= 11);
}
static void autosave(void)
{
    uint32_t now = plat_ms(), since = now - ui.saved_t;
    int want;
    if (!quiet_now() || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_REST)
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
 * knob as it turns; a help card as it comes up; each new riddim (its name, key, tempo); rolling and stopped; what
 * a button did (say). WHERE is the whole screen in two: the page and the header, then the riddim. */
static struct {
    uint8_t view, help, started;
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
    sp_cat(KNOB_LABEL[view][i]);
    sp_cat(" ");
    knob_text(VIEW_KNOB[view][i], t);
    sp_cat(t);
}
static void sp_key(void)                         /* "E minor" */
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
    sp_cat(". Style ");
    sp_cat(en_style_name(proj.style));
    sp_cat(sp_tp.playing ? ", rolling" : ", stopped");
    if (en_repeat_on)
        sp_cat(", repeat on");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static void sp_track_text(void)                  /* "Version 12, Iron Gate riddim. E minor, 72 BPM" */
{
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("Version ");
    sp_int(en_track_no);
    sp_cat(", ");
    sp_cat(en_riddim_name(en_riddim));
    sp_cat(" riddim. ");
    sp_key();
    sp_cat(", ");
    sp_int(en_bpm);
    sp_cat(" BPM");
}
static void sp_record(void)                      /* WHERE's second part: the riddim, how far in */
{
    char t[12];
    sp_track_text();
    sp_cat(". ");
    cat_time(t, en_track_ms);
    sp_cat(t);
    sp_cat(" of ");
    cat_time(t, en_track_len_ms);
    sp_cat(t);
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
    sp_cat("STYLE ");
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
    sp.track = en_track_no;
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
            sp_record();
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
        ann_say(en_playing ? "ROLLING" : "STOPPED", 0, 0);
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
    if (en_track_no != sp.track) {               /* a new riddim: queued */
        sp.track = en_track_no;
        sp_track_text();
        ann_say(sp_buf, 0, 0);
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card, riddim and message as a screen reader gets them (the "speakdump" command) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}, {"NEXT RIDDIM", 0},
                                         {"STOPPED", 0}, {"ROLLING", 0}, {"REPEAT", "ON"}, {"REPEAT", "OFF"},
                                         {"ECHO THROW", 0}, {"SPRING", 0},
                                         {"SAVED", 0}, {"KEYS OCTAVE", "-2"}, {"KEYS OCTAVE", "-1"},
                                         {"KEYS OCTAVE", "0"}, {"KEYS OCTAVE", "+1"}, {"KEYS OCTAVE", "+2"}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}, {"KEYS OCTAVE", "LOWEST"},
                                          {"KEYS OCTAVE", "HIGHEST"}};
    project_t keep = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty, keep_play = sp_tp.playing;
    uint32_t keep_no = en_track_no;
    uint8_t keep_key = en_key, keep_minor = en_minor, keep_rep = en_repeat_on, keep_rd = en_riddim;
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
                proj.par[k] = (int16_t)x;
                sp_n = 0;
                sp_knob(v, i);
                sp_print("KNOB", sp_buf);
            }
            proj = keep;
        }
    }
    for (x = 0; x < 8; x++) {                    /* WHERE: rolling, stopped, unsaved, repeat, minor and major */
        ui.view = (uint8_t)(x % NVIEWS);
        sp_tp.playing = (uint8_t)(x & 1);
        ui.dirty = (uint8_t)(x >> 1 & 1);
        en_repeat_on = (uint8_t)(x == 5);
        en_key = (uint8_t)(x * 5 % 12);
        en_minor = (uint8_t)(x >> 2 & 1);
        proj.style = (uint8_t)(x * 3 % NSTYLES);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_record();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
    for (x = 0; x < EN_NRIDDIMS * 2; x++) {      /* the riddims: their names, keys and tempos */
        en_track_no = (uint32_t)x + 1u;
        en_riddim = (uint8_t)(x % EN_NRIDDIMS);
        en_key = (uint8_t)(x % 12);
        en_minor = (uint8_t)(x & 1);
        sp_track_text();
        sp_print("TRACK", sp_buf);
    }
    en_track_no = keep_no;
    en_riddim = keep_rd;
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
    memset(blip, 0, sizeof blip);
    memset(rip, 0, sizeof rip);
    ui.touched = -1;
    ui.saved_t = plat_ms();
    ui.saved_track = proj.track_no + 1u;          /* the version the boot starts: nothing new to keep yet */
    ui.act_t = plat_ms();
    echo_tables();
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
