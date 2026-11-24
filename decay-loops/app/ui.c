/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (AMB-1, whose panel, knob strip, help cards and screen reader this keeps)
 * Copyright (C) 2026 ashtonwallen (DECAY LOOPS) */
/* DECAY LOOPS UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too.
 *
 *   keys           notes: heard at once, and recorded onto the next loop (OCT- / OCT+ their octave)
 *   PLAY           the tape runs / stops          REC    a new loop now
 *   EDIT           HOLD: the tape stops wearing
 *   SELECT pages   ALGORITHM mode                 PRESETS the tape stock and mood
 *   HOME ENV ARP GLO   the pages: Tape, Tone, Phrase, Setup
 *   KNOB 1-4       the page's four values
 *   SAVE           saves (it also saves by itself, at a quiet moment, once the knobs have rested);
 *                  held 1.5 s: the preset back to its recipe */
#include "fastmath.h"

enum { V_TAPE, V_TONE, V_PHRASE, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Tape", "Tone", "Phrase", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_ARP, B_GLO};
enum { K_LEDS = 100, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_DECAY, P_LENGTH, P_LAYERS, P_WOW},
    {P_STOCK, P_TONE, P_HISS, P_SPACE},
    {P_VOICE, P_PHRASE, P_DENSITY, P_OCT},
    {P_ROOT, P_MODE, P_TUNE, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Decay", "Length", "Layers", "Wow"},
    {"Stock", "Tone", "Hiss", "Space"},
    {"Voice", "Phrase", "Density", "Octave"},
    {"Root", "Mode", "Tune", "Lights"},
};

/* the screen's colours: a dark room, a deck under a lamp. Brushed reels, oxide tape that fades as it wears */
#define K_BG RGB(19, 16, 14)
#define K_PANEL RGB(40, 34, 29)
#define K_LINE RGB(66, 57, 49)
#define K_DIM RGB(140, 124, 106)
#define K_TEXT RGB(236, 223, 198)
#define K_ACC RGB(224, 160, 90)          /* the accent: the page, the knob being turned, a message's word */
#define K_ACC_D RGB(150, 106, 64)
#define K_REC RGB(226, 82, 62)
#define K_HOLD RGB(140, 184, 204)
#define K_FILM RGB(44, 38, 33)           /* the clear base where the oxide has gone */
#define K_SPOT RGB(214, 192, 146)        /* oxide lifting */
#define K_MET_D RGB(84, 82, 78)          /* the reels' metal, in shade and in the light */
#define K_MET_L RGB(196, 192, 182)
#define K_HUB RGB(214, 208, 196)
#define K_PLATE RGB(29, 25, 22)          /* the deck under the reels */

/* each stock's oxide: fresh, then worn pale */
static const uint16_t OX_NEW[EN_NSTOCK] = {RGB(118, 62, 36), RGB(88, 52, 36), RGB(156, 112, 52), RGB(112, 114, 118)};
static const uint16_t OX_OLD[EN_NSTOCK] = {RGB(164, 130, 96), RGB(146, 120, 98), RGB(196, 172, 124), RGB(160, 138, 112)};

static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];               /* detents in the current quick run, per knob (accel) */
    uint32_t down_t[NB];               /* when each button went down */
    uint32_t turn_t;                   /* the last knob turn */
    uint8_t help;                      /* a button's card is up: the button + 1 */
    uint8_t reset_done;                /* SAVE held: the preset went back to its recipe this hold */
    uint8_t dirty;                     /* the project changed since the last save */
    uint32_t act_t, saved_t, due_t;    /* the last touch, the last save, since when a save is due */
    uint8_t saved_once;
    float lvl_avg;                     /* the output's slow average (autosave waits for a dip) */
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;                    /* the knob turned last (0..3), shown bright for a moment */
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];                   /* what each band shows: redrawn when it changes */
    int8_t knote[NKEYS];               /* the note each key sounds while held (-1 none) */
} ui;

/* the screen reader (announce.c; speak() below): a message went to it this frame, so speak() leaves what it
 * changed be */
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
    while (k && t[k - 1] == '.')               /* "UPDATE MODE IN 3..": the dots are for the eye */
        k--;
    t[k] = 0;
}
/* a message for a moment in the header; flags: how a screen reader gets it (0: the caller tells it) */
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
/* something did not happen: a screen reader says it whole (what follows does not cut it off) */
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
static char *cat_s(char *b, const char *s)      /* s onto b; the end */
{
    while ((*b = *s++))
        b++;
    return b;
}

static void mark_dirty(void)
{
    if (!ui.dirty)
        ui.due_t = plat_ms();
    ui.dirty = 1;
}
int ui_dirty(void) { return ui.dirty != 0; }

static uint32_t pool_abs(void)                 /* the mode's scale as absolute pitch classes */
{
    uint32_t p = EN_MODE_POOL[proj.par[P_MODE]], r = (uint32_t)proj.par[P_ROOT];
    return ((p << r) | (p >> (12u - r))) & 0xFFFu;
}

/* the loops: the one the reels show is the newest playing (else the one being recorded); -1 none */
static int prim(void)
{
    int i, best = -1, rec = -1;
    for (i = 0; i < EN_NLAYER; i++) {
        if (en_lay_state[i] == LS_PLAY && (best < 0 || (int16_t)(en_lay_serial[i] - en_lay_serial[best]) > 0))
            best = i;
        if (en_lay_state[i] == LS_REC)
            rec = i;
    }
    return best >= 0 ? best : rec;
}
static int rec_layer(void)
{
    int i;
    for (i = 0; i < EN_NLAYER; i++)
        if (en_lay_state[i] == LS_REC)
            return i;
    return -1;
}
static int live_layers(int *order)              /* the loops there are, newest first */
{
    int i, j, n = 0;
    for (i = 0; i < EN_NLAYER; i++)
        if (en_lay_state[i] != LS_FREE) {
            for (j = n; j > 0 && (int16_t)(en_lay_serial[i] - en_lay_serial[order[j - 1]]) > 0; j--)
                order[j] = order[j - 1];
            order[j] = i;
            n++;
        }
    return n;
}

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
    else if (k == K_LEDS)                        /* On: the keys, and the unlit buttons glow; Keys: no glow */
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

/* knob acceleration by speed (X0X's): a few clicks are one step each however quick, so small
 * corrections stay exact; a longer quick turn speeds up to 8 a detent, so half a turn sweeps 0-100.
 * Small ranges never accelerate. */
#ifdef PLAT_HOST
#define accel_off plat_host_exact      /* the host simulator's "spin": exact steps */
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
        dt /= a;                                      /* per detent */
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

static void set_mode(int m)
{
    m = (m % EN_NMODES + EN_NMODES) % EN_NMODES;
    proj.par[P_MODE] = (int16_t)m;
    en_set(P_MODE, m);
    say("MODE", EN_MODE_NAME[m]);
    mark_dirty();
}

static void set_preset(int i)                   /* the preset playing kept as it is; preset i as it was left */
{
    project_preset(i);
    project_apply();
    en_seed(plat_ms());
    en_regrow();
    say_as("PRESET", preset_name(proj.preset), 0);
    sp_preset();                                 /* a screen reader: the preset with its key */
    mark_dirty();
}

static void key_oct(int d)
{
    char t[8];
    int o = proj.keyoct + d;
    if (o < -2 || o > 2) {
        warn("KEYS OCTAVE", d > 0 ? "AT THE TOP" : "AT THE BOTTOM");
        return;
    }
    proj.keyoct = (int8_t)o;
    t[0] = o > 0 ? '+' : '-';
    itoa_u((uint32_t)(o < 0 ? -o : o), t + (o != 0));   /* "+1", "0", "-2" */
    say("KEYS OCTAVE", t);
    mark_dirty();
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
            say("TAPE", "STOPPED");
        } else {
            en_seed(plat_ms());
            en_play(1);
            say("TAPE", "RUNNING");
        }
        break;
    case B_REC:
        en_new();
        say("NEW LOOP", 0);
        break;
    case B_EDIT:
        say("HOLD", en_frozen ? "OFF" : "ON");
        en_freeze(!en_frozen);                   /* the engine takes it at its next block */
        break;
    case B_OCTDN: case B_OCTUP:
        key_oct(b == B_OCTUP ? 1 : -1);
        break;
    case B_SAVE:
        if (project_save() == 0) {
            ui.dirty = 0;
            ui.saved_t = plat_ms();
            ui.saved_once = 1;
            say("SAVED", 0);
        } else {
            warn("NOT SAVED", "FLASH ERROR");
        }
        break;
    default: break;
    }
}

/* MIDI in (USB and the TRS jack): notes play, as the keys do */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0)) {
        uint32_t st = (pkt >> 8) & 0xF0u, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24 & 0x7Fu;
        if (st == 0x90u && v)
            en_note((int)n, (int)v);
        else if (st == 0x80u || st == 0x90u)
            en_note((int)n, 0);
    }
}

/* HELP CARDS (X0X's): hold one button alone for HELP_HOLD and a card says what it does. A key, a
 * turn or another button puts it away; letting go after its card does nothing else. PLAY acts on
 * its press, and buttons with no job here have no card. */
#define HELP_HOLD 900u
#define RESET_HOLD 1500u
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("THE TAPE"); L("DECAY, LENGTH, LAYERS, WOW"); break;
    case B_ENV: L("THE TONE"); L("STOCK, TONE, HISS, SPACE"); break;
    case B_ARP: L("THE PHRASE"); L("VOICE, PHRASE, DENSITY,"); L(" OCTAVE"); break;
    case B_GLO: L("SETUP"); L("ROOT, MODE, TUNE, LIGHTS"); break;
    case B_REC: L("A NEW LOOP, NOW"); L("KEYS PLAYED GO ONTO IT"); break;
    case B_EDIT: L("HOLD: THE TAPE STOPS"); L(" WEARING"); L("AGAIN: IT WEARS ON"); break;
    case B_OCTDN: L("THE KEYS AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE KEYS AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES EVERY PRESET"); L("KEEP HOLDING: THE PRESET"); L(" BACK TO ITS RECIPE"); break;
    default: break;
    }
#undef L
    return n;
}

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
            ui.btn_used |= btn;                  /* its release now does nothing */
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
            if (i == B_PLAY)                   /* transport on the press */
                button((int)i);
        } else if (!(ui.btn_used & m) && i != B_PLAY) {
            button((int)i);
        }
    }
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++) {
        if (!(ch >> i & 1u))
            continue;
        if (keys >> i & 1u) {                    /* key 0 is F3 */
            int n = 53 + (int)i + 12 * proj.keyoct;
            ui.knote[i] = (int8_t)n;
            en_note(n, 100);
        } else if (ui.knote[i] >= 0) {
            en_note(ui.knote[i], 0);
            ui.knote[i] = -1;
        }
    }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {        /* SELECT: the pages */
        int v = ui.view + (e > 0 ? 1 : -1);
        if (v >= 0 && v < NVIEWS)
            set_view(v);
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        set_mode(proj.par[P_MODE] + (int)e);
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        set_preset(proj.preset + (int)e);
        ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && plat_ms() - ui.down_t[B_SAVE] > RESET_HOLD) {
        ui.reset_done = 1;                     /* held: the preset back to its recipe (not saved) */
        ui.btn_used |= 1u << B_SAVE;
        ui.help = 0;
        project_preset_reset();
        project_apply();
        en_regrow();
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
/* three bands: the header (28 px), the deck (144), the knobs (68) */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* a rectangle with rounded corners (radius r, up to 6) */
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

/* anti-aliased drawing (X0X's): a pixel blended over the canvas, a disc, and a knob's ring */
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
static uint32_t qa(float a) { return a <= 0.0f ? 0u : a >= 1.0f ? 16u : (uint32_t)(a * 16.0f + 0.5f); }

/* the deck in bands: the canvas holds BAND_H rows (APP_CANVAS_PX); band b draws the deck's rows b * BAND_H on. The
 * rows a band holds, in the deck's own y, so the pixel loops skip the rest */
#define BAND_H 72
static void band_begin(int32_t b)
{
    cv_begin(240, BAND_H, K_BG);
    cv_oy = -b * BAND_H;
    cv_y0 = 0;
    cv_y1 = BAND_H;
}
static int32_t row_lo(void) { return cv_y0 - cv_oy; }
static int32_t row_hi(void) { return cv_y1 - cv_oy; }   /* (one past) */
static void rows_clip(int32_t *y0, int32_t *y1)         /* y0..y1 inclusive, to the band's rows */
{
    if (*y0 < row_lo())
        *y0 = row_lo();
    if (*y1 > row_hi() - 1)
        *y1 = row_hi() - 1;
}

static void disc(float cx, float cy, float r, uint16_t c)
{
    int32_t x0 = (int32_t)fm_floorf(cx - r - 1.0f), x1 = (int32_t)(cx + r + 1.0f);
    int32_t y0 = (int32_t)fm_floorf(cy - r - 1.0f), y1 = (int32_t)(cy + r + 1.0f), x, y;
    rows_clip(&y0, &y1);
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy);
            px_blend(x, y, c, qa(r - d + 0.5f));
        }
}

/* a line of width w from (ax, ay) to (bx, by), anti-aliased */
static void seg(float ax, float ay, float bx, float by, float w, uint16_t c)
{
    float vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy + 1e-6f, hw = w * 0.5f;
    int32_t x0 = (int32_t)fm_floorf(fm_minf(ax, bx) - w - 1.0f), x1 = (int32_t)(fm_maxf(ax, bx) + w + 1.0f);
    int32_t y0 = (int32_t)fm_floorf(fm_minf(ay, by) - w - 1.0f), y1 = (int32_t)(fm_maxf(ay, by) + w + 1.0f), x, y;
    rows_clip(&y0, &y1);
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float px = (float)x + 0.5f - ax, py = (float)y + 0.5f - ay;
            float t = fm_clampf((px * vx + py * vy) / l2, 0.0f, 1.0f), dx = px - t * vx, dy = py - t * vy;
            px_blend(x, y, c, qa(hw + 0.5f - fm_sqrtf(dx * dx + dy * dy)));
        }
}

/* atan2 in turns, 0..1 from +x going clockwise on screen (y down) */
static float turns(float y, float x)
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), a = ax < ay ? ax / (ay + 1e-9f) : ay / (ax + 1e-9f), s = a * a, r;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;   /* atan on [0, 1] */
    if (ay > ax)
        r = 1.57079637f - r;
    if (x < 0.0f)
        r = FM_PI - r;
    if (y < 0.0f)
        r = FM_TWO_PI - r;
    return r * (1.0f / FM_TWO_PI);
}

/* a knob: a 270-degree track from 7:30 to 4:30, anti-aliased, round ends; the value filled from
 * the start (or from the top, for a centred value) and a round handle */
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
            px_blend(cx + x, cy + y, (t >= lo && t <= hi) ? fill : track, qa(a));
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

static void caps(char *d, const char *s, int n)  /* s in capitals, at most n - 1 characters */
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

/* the battery (plat_battery): 0 (low) .. 3 bars, 4 a USB host powers the FM-1, -1 not read yet. The
 * simulator's "battery N" (host.c) stands in for it */
#ifdef PLAT_HOST
static int8_t batt_host = -2;                  /* -2: plat_battery's */
static int batt(void) { return batt_host > -2 ? batt_host : plat_battery(); }
#else
#define batt() plat_battery()
#endif
#define BATT_W 20                              /* the body 18 wide and its nub */
static void draw_batt(int32_t x, int b)
{
    static const int8_t BOLT[7][2] = {{10, 2}, {9, 2}, {8, 2}, {7, 5}, {9, 2}, {8, 2}, {7, 2}};
    uint16_t c = b <= 1 ? K_REC : K_DIM;
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
        cv_rect(x + 2 + i * 5, 11, 4, 6, b <= 1 ? K_REC : K_ACC_D);
}
static const char *batt_words(int b)
{
    static const char *const W[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                     "On USB power"};
    return b >= 0 && b <= 4 ? W[b] : 0;
}

/* the header: the preset in a tag, its key; on the right the battery, a dot while there is something unsaved,
 * HOLD while the wear holds, and the record lamp (a pause sign while the tape is stopped). Messages take its
 * place for a moment. */
static void draw_header(void)
{
    int msg = plat_ms() < ui.msg_until, rec = rec_layer() >= 0;
    uint32_t h = hash(hash(hash(2166136261u, proj.preset | (uint32_t)proj.par[P_ROOT] << 8 | (uint32_t)proj.par[P_MODE] << 12),
                           en_playing | ui.dirty << 4 | (uint32_t)msg << 5 | (uint32_t)en_frozen << 6 | (uint32_t)rec << 7),
                      (uint32_t)(batt() + 1) << 24);
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1]), ui.msg_until);
    if (rec)
        h = hash(h, (plat_ms() / 600u) & 1u);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], K_ACC);
    } else {
        int32_t x = 232, w;
        char key[24], *p;
        if (batt() >= 0) {
            x -= BATT_W;
            draw_batt(x, batt());
            x -= 8;
        }
        if (ui.dirty) {                            /* unsaved */
            cv_round(x - 5, 12, 5, 5, 2, K_DIM);
            x -= 12;
        }
        if (!en_playing) {                         /* stopped: a pause sign */
            cv_rect(x - 9, 8, 3, 12, K_DIM);
            cv_rect(x - 4, 8, 3, 12, K_DIM);
            x -= 15;
        } else if (rec) {                          /* recording: the lamp, breathing */
            uint16_t c = (plat_ms() / 600u) & 1u ? K_REC : mix(K_BG, K_REC, 10);
            cv_round(x - 10, 9, 10, 10, 5, c);
            x -= 16;
        } else {
            cv_round(x - 10, 9, 10, 10, 5, K_LINE);
            cv_round(x - 8, 11, 6, 6, 3, K_BG);
            x -= 16;
        }
        if (en_frozen) {
            x -= text_w(&FONT_XS, "HOLD");
            cv_text(x, 8, &FONT_XS, "HOLD", K_HOLD);
            x -= 8;
        }
        w = text_w(&FONT_B, preset_name(proj.preset)) + 12;   /* the preset, in an amber tag */
        cv_round(4, 4, w, 20, 6, K_ACC);
        cv_text(10, 6, &FONT_B, preset_name(proj.preset), K_BG);
        w += 10;                                  /* the key, where it clears the right side */
        p = cat_s(key, EN_NOTE_NAME[proj.par[P_ROOT]]);
        *p++ = ' ';
        cat_s(p, EN_MODE_NAME[proj.par[P_MODE]]);
        if (w + text_w(&FONT_XS, key) + 4 <= x)
            cv_text(w, 8, &FONT_XS, key, K_DIM);
        else if (w + text_w(&FONT_XS, EN_NOTE_NAME[proj.par[P_ROOT]]) + 4 <= x)
            cv_text(w, 8, &FONT_XS, EN_NOTE_NAME[proj.par[P_ROOT]], K_DIM);
    }
    cv_blit(0, 0);
}

/* ---- the deck: two reels turning, the tape between them over two guides and past the head, wearing as it
 * plays (it scrolls by, showing the loop's wear map: pale where it has faded, gaps where the oxide has gone),
 * flakes of oxide falling from it; under it every loop's wear map with its playhead */
#define RL_X 60
#define RR_X 180
#define R_Y 46
#define R_FL 38.0f                               /* the flange */
#define R_HUB 9.5f
#define R_MIN 12.5f                              /* the pack's radius: empty .. full */
#define R_MAX 36.0f
#define TAPE_Y 99                                /* the tape's run, under the reels: its top row */
#define TAPE_H 5
#define HEAD_X 120
#define GL_X 18                                  /* the guides */
#define GR_X 222
#define G_Y 101
#define STRIP_Y 115
#define STRIP_X 28
#define STRIP_W 192
#define NFLAKE 28

static struct {
    float rot[2], pack[2];                       /* the reels' angle (turns) and pack radius */
    uint32_t t;                                  /* the last draw */
    uint32_t flakes;                             /* en_flakes as last seen */
    uint32_t rng;
    float fx[NFLAKE], fy[NFLAKE], fvx[NFLAKE], fvy[NFLAKE];
    uint8_t flife[NFLAKE], fmax[NFLAKE];
    uint16_t fcol[NFLAKE];
} dk;

static float urand(void)
{
    dk.rng = dk.rng * 1664525u + 1013904223u;
    return (float)(dk.rng >> 8) * (1.0f / 16777216.0f);
}

static uint16_t oxide(int stock, float wear, int dmg, uint32_t grain)   /* a piece of tape's colour */
{
    uint16_t c = mix(OX_NEW[stock], OX_OLD[stock], q16(fm_minf(wear * 1.25f, 1.0f)));
    grain *= 2654435761u;
    grain = (grain ^ grain >> 15) >> 16;
    if (dmg > 0)
        c = mix(c, K_SPOT, dmg * 11 / 255);
    if ((grain & 7u) == 0u)                      /* grain */
        c = mix(c, K_BG, 3);
    else if ((grain & 15u) == 5u && wear > 0.3f)
        c = mix(c, K_SPOT, 4);
    return c;
}

/* a reel: the tape pack (its rings), the brushed flange over it with three windows turning, the hub */
static void reel(int32_t cx, float pr, float rot, int stock, float wear)
{
    const float W0 = 14.5f, W1 = 33.0f, WH = 0.30f;
    uint16_t pk0 = mix(OX_NEW[stock], K_BG, 6), pk1 = mix(mix(OX_NEW[stock], OX_OLD[stock], q16(wear)), K_BG, 2);
    int32_t x, y;
    const int32_t R = (int32_t)R_FL + 1;
    int32_t ya = R_Y - R, yb = R_Y + R;
    rows_clip(&ya, &yb);
    for (y = ya - R_Y; y <= yb - R_Y; y++)
        for (x = -R; x <= R; x++) {
            float fx = (float)x + 0.5f, fy = (float)y + 0.5f, d2 = fx * fx + fy * fy, d, ao, t, ang, win, sh;
            if (d2 > (R_FL + 1.0f) * (R_FL + 1.0f))
                continue;
            d = fm_sqrtf(d2);
            if (d < pr + 0.5f && d > R_HUB - 0.5f) {         /* the pack, wound in rings */
                uint32_t ring = hash(0x9E37u, (uint32_t)(d * 1.6f));
                px_blend(cx + x, R_Y + y, mix(pk0, pk1, (int)(ring >> 28) / 2 + 4), qa(pr + 0.5f - d));
            }
            ao = fm_clampf(R_FL + 0.5f - d, 0.0f, 1.0f);
            if (d < R_HUB + 0.5f) {                          /* the hub, its three keys turning */
                t = turns(fy, fx) - rot;
                t *= 3.0f;
                t -= fm_floorf(t);
                ang = fm_fabsf(t - 0.5f);
                if (d < 3.2f)
                    px_blend(cx + x, R_Y + y, K_BG, qa(3.7f - d));
                else if (ang > 0.40f && d < R_HUB - 1.5f)
                    px_blend(cx + x, R_Y + y, K_MET_D, 16u);
                else
                    px_blend(cx + x, R_Y + y, mix(K_HUB, K_MET_D, d > R_HUB - 1.0f ? 6 : 0), qa(R_HUB + 0.5f - d));
                continue;
            }
            win = 0.0f;                                      /* the windows: the pack seen through */
            if (d > W0 - 1.0f && d < W1 + 1.0f) {
                float arcd, radd;
                t = turns(fy, fx) - rot;
                t *= 3.0f;
                t -= fm_floorf(t);
                ang = fm_fabsf(t - 0.5f);
                arcd = (WH - ang) * (FM_TWO_PI / 3.0f) * d;
                radd = fm_minf(d - W0, W1 - d);
                win = fm_clampf(fm_minf(arcd, radd) + 0.5f, 0.0f, 1.0f);
            }
            sh = 0.5f + 0.5f * (-0.6f * fx - 0.8f * fy) / (d + 1.0f);   /* lit from the top left */
            {
                uint16_t m = mix(K_MET_D, K_MET_L, q16(sh * 0.85f + 0.05f));
                if (d > R_FL - 1.6f || (win > 0.0f && win < 1.0f))   /* the rim and the windows' edges */
                    m = mix(m, K_MET_L, 8);
                if ((hash(0x51EDu, (uint32_t)(d * 3.0f)) & 7u) == 0u)   /* brushed */
                    m = mix(m, K_MET_D, 3);
                px_blend(cx + x, R_Y + y, m, qa(ao * (1.0f - win) * 0.9f));
            }
        }
}

/* where a line from (px, py) touches the circle (cx, cy, r), on the side s (1 the left one, -1 the right) */
static void tangent(float px, float py, float cx, float cy, float r, float s, float *tx, float *ty)
{
    float ux = px - cx, uy = py - cy, d = fm_sqrtf(ux * ux + uy * uy), c, sn;
    ux /= d;
    uy /= d;
    c = fm_clampf(r / d, 0.0f, 1.0f);
    sn = fm_sqrtf(1.0f - c * c) * s;
    *tx = cx + r * (ux * c - uy * sn);
    *ty = cy + r * (ux * sn + uy * c);
}

static void draw_tape_run(int p, int stock)
{
    float tx, ty;
    uint16_t edge = mix(OX_NEW[stock], K_BG, 6);
    int32_t x, r;
    /* down from the left pack to its guide, and from the right guide up to its pack */
    tangent((float)GL_X - 4.0f, (float)G_Y, (float)RL_X, (float)R_Y, dk.pack[0], 1.0f, &tx, &ty);
    seg((float)GL_X - 4.0f, (float)G_Y, tx, ty, 1.6f, edge);
    tangent((float)GR_X + 4.0f, (float)G_Y, (float)RR_X, (float)R_Y, dk.pack[1], -1.0f, &tx, &ty);
    seg((float)GR_X + 4.0f, (float)G_Y, tx, ty, 1.6f, edge);
    /* the run past the head: the loop scrolls by, 3 px a cell */
    for (x = GL_X; x <= GR_X; x++) {
        float u = 0.0f, w = 0.0f;
        int cell = 0, dmg = 0, gone;
        if (p >= 0) {
            u = en_lay_pos[p] - (float)(x - HEAD_X) * (1.0f / 384.0f);
            u -= fm_floorf(u);
            cell = (int)(u * (float)EN_NCELL) & (EN_NCELL - 1);
            dmg = en_lay_dmg[p][cell];
            w = en_lay_wear[p];
        }
        gone = dmg >= 220;
        for (r = 0; r < TAPE_H; r++) {
            uint16_t c;
            if (gone)
                c = (r == 0 || r == TAPE_H - 1) ? mix(K_FILM, OX_OLD[stock], 4) : K_FILM;
            else
                c = oxide(stock, w, dmg, hash((uint32_t)cell * 31u + (uint32_t)(u * 384.0f), (uint32_t)r));
            if (!gone && (r == 0 || r == TAPE_H - 1))
                c = mix(c, K_BG, 5);
            cv_rect(x, TAPE_Y + r, 1, 1, c);
        }
    }
    /* the guides: rollers at each end */
    disc((float)GL_X, (float)G_Y, 5.0f, K_MET_D);
    disc((float)GL_X, (float)G_Y, 3.0f, K_MET_L);
    disc((float)GR_X, (float)G_Y, 5.0f, K_MET_D);
    disc((float)GR_X, (float)G_Y, 3.0f, K_MET_L);
    /* the head, under the tape: its lamp red while a loop records, amber while the tape runs */
    cv_round(HEAD_X - 12, TAPE_Y + TAPE_H, 24, 8, 3, K_MET_D);
    cv_rect(HEAD_X - 10, TAPE_Y + TAPE_H, 20, 2, K_MET_L);
    cv_rect(HEAD_X, TAPE_Y + TAPE_H, 1, 3, K_BG);
    {
        uint16_t c = rec_layer() >= 0 ? K_REC : en_speed > 0.05f ? K_ACC : K_LINE;
        cv_round(HEAD_X + 6, TAPE_Y + TAPE_H + 4, 3, 3, 1, c);
    }
}

/* every loop, newest at the top: its wear map (pale where faded, gaps where gone), its fade, its playhead; the
 * one recording fills in red as it is written */
static void draw_strips(void)
{
    int order[EN_NLAYER], n = live_layers(order), k, stock = proj.par[P_STOCK];
    int32_t rh, gap = 2, y;
    if (!n) {
        text_c(120, STRIP_Y + 6, &FONT_T, "NO TAPE", K_LINE);
        return;
    }
    rh = (MAIN_H - STRIP_Y - 1) / n - gap;
    if (rh > 9)
        rh = 9;
    y = STRIP_Y + (MAIN_H - STRIP_Y - n * (rh + gap)) / 2;
    for (k = 0; k < n; k++, y += rh + gap) {
        int i = order[k], st = en_lay_state[i], fade = q16(1.0f - en_lay_gain[i]);
        float w = en_lay_wear[i], rf = en_lay_rec[i];
        int32_t x, ph;
        uint16_t lamp = st == LS_REC ? K_REC : st == LS_FADE ? K_LINE : K_ACC_D;
        cv_round(STRIP_X - 12, y + rh / 2 - 2, 5, 5, 2, lamp);
        for (x = 0; x < STRIP_W; x++) {
            int cell = x * EN_NCELL / STRIP_W, dmg = en_lay_dmg[i][cell];
            uint16_t c;
            if (st == LS_REC && (float)x > rf * (float)STRIP_W) {
                if (!(x & 3))
                    cv_rect(STRIP_X + x, y + rh / 2, 1, 1, K_LINE);
                continue;
            }
            c = dmg >= 220 ? K_FILM : oxide(stock, w, dmg, hash((uint32_t)i, (uint32_t)x));
            if (st == LS_REC)
                c = mix(c, K_REC, 5);
            cv_rect(STRIP_X + x, y, 1, rh, mix(c, K_BG, fade * 3 / 4));
        }
        ph = st == LS_REC ? (int32_t)(rf * (float)STRIP_W) : (int32_t)(en_lay_pos[i] * (float)STRIP_W);
        if (ph >= STRIP_W)
            ph = STRIP_W - 1;
        cv_rect(STRIP_X + ph, y - 1, 1, rh + 2, st == LS_REC ? K_REC : mix(K_TEXT, K_BG, fade * 3 / 4));
        {   /* its wear, right of the strip, as a short bar filling */
            int32_t f = (int32_t)(fm_clampf(w, 0.0f, 1.0f) * (float)(rh + 2) + 0.5f);
            cv_rect(STRIP_X + STRIP_W + 5, y - 1, 2, rh + 2, K_LINE);
            if (f)
                cv_rect(STRIP_X + STRIP_W + 5, y + rh + 1 - f, 2, f, mix(K_DIM, K_BG, fade * 3 / 4));
        }
    }
}

static void flakes_tick(float dt, int p)
{
    int i, spawn = 0;
    uint32_t nf = en_flakes;
    float w = p >= 0 ? en_lay_wear[p] : 0.0f;
    if (nf != dk.flakes) {                        /* cells that began to go: a few flakes each */
        spawn = (int)(nf - dk.flakes) * 4;
        dk.flakes = nf;
    }
    if (en_speed > 0.05f && urand() < dt * (0.8f + 8.0f * w * w) * en_speed)
        spawn++;
    if (spawn > 6)
        spawn = 6;
    for (i = 0; i < NFLAKE; i++) {
        if (dk.flife[i]) {                        /* drifting down slowly, as dust in lamplight */
            dk.fvy[i] = fm_minf(dk.fvy[i] + 6.0f * dt, 9.0f);
            dk.fx[i] += (dk.fvx[i] + 5.0f * fm_sinf(dk.fy[i] * 0.17f + (float)i)) * dt;
            dk.fy[i] += dk.fvy[i] * dt;
            if (dk.flife[i] > 1 && dk.fy[i] < (float)MAIN_H)
                dk.flife[i]--;
            else
                dk.flife[i] = 0;
        } else if (spawn > 0) {
            spawn--;
            dk.fx[i] = (float)GL_X + 10.0f + urand() * (float)(GR_X - GL_X - 20);
            dk.fy[i] = (float)(TAPE_Y + TAPE_H - 1);
            dk.fvx[i] = (urand() - 0.5f) * 10.0f;
            dk.fvy[i] = 1.0f + urand() * 3.0f;
            dk.fmax[i] = dk.flife[i] = (uint8_t)(60 + (int)(urand() * 100.0f));
            dk.fcol[i] = mix(OX_NEW[proj.par[P_STOCK]], K_SPOT, 6 + (int)(urand() * 10.0f));
        }
    }
}

static void draw_flakes(void)
{
    int i;
    for (i = 0; i < NFLAKE; i++)
        if (dk.flife[i]) {
            uint32_t a = (uint32_t)dk.flife[i] * 16u / dk.fmax[i];
            int32_t x = (int32_t)dk.fx[i], y = (int32_t)dk.fy[i];
            if (dk.flife[i] > dk.fmax[i] - 4)     /* coming away: a glint */
                a = 16u;
            px_blend(x, y, dk.fcol[i], a);
            if (i % 3 == 0) {                     /* some bigger flakes */
                px_blend(x + 1, y, dk.fcol[i], a * 2 / 3);
                px_blend(x, y + 1, dk.fcol[i], a / 2);
            }
        }
}

/* between the reels: the passes the newest loop has made, which loop it is */
static void draw_count(int p)
{
    char t[16];
    int r = rec_layer();
    if (!en_playing && en_speed <= 0.0f) {
        text_c(120, R_Y - 8, &FONT_XS, "STOP", K_DIM);
        return;
    }
    text_c(120, 18, &FONT_T, "PASS", K_DIM);
    itoa_u(p >= 0 ? en_passes[p] + (en_lay_state[p] == LS_PLAY ? 1u : 0u) : 0u, t);
    text_c(120, 28, text_w(&FONT_M, t) <= 40 ? &FONT_M : &FONT_B, t, en_frozen ? K_HOLD : K_TEXT);
    if (r >= 0) {
        cat_s(t, "REC ");
        itoa_u(en_lay_serial[r], t + 4);
        text_c(120, 58, &FONT_T, t, K_REC);
    } else if (p >= 0) {
        cat_s(t, "LOOP ");
        itoa_u(en_lay_serial[p], t + 5);
        text_c(120, 58, &FONT_T, t, K_DIM);
    }
}

/* a button's card (help_tick): its name, then what it does, in the deck's place. A line starting with a
 * space goes on from the one before (a screen reader joins them: sp_line) */
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

static void draw_main(void)
{
    uint32_t now = plat_ms(), h, i;
    float dt;
    int p, stock = proj.par[P_STOCK], live = 0;
    if (ui.frame % 3u)                           /* the tape moves slowly: 20 frames a second is plenty */
        return;
    if (ui.help) {
        h = hash(0x48454C50u, ui.help);
        if (h != ui.sig[1]) {
            ui.sig[1] = h;
            for (i = 0; i < MAIN_H / BAND_H; i++) {
                band_begin((int32_t)i);
                draw_help();
                cv_blit(0, MAIN_Y + i * BAND_H);
            }
        }
        return;
    }
    dt = fm_clampf((float)(now - dk.t) * 0.001f, 0.0f, 0.2f);
    dk.t = now;
    p = prim();
    {   /* the reels: the pack goes from left to right as the loop wears; they turn with the tape */
        float w = p >= 0 ? fm_clampf(en_lay_wear[p], 0.0f, 1.0f) : 0.0f, a = R_MAX * R_MAX + R_MIN * R_MIN;
        float l = R_MIN + (R_MAX - R_MIN) * (1.0f - w), r = fm_sqrtf(a - l * l), k = fm_minf(dt * 1.5f, 1.0f);
        dk.pack[0] += (l - dk.pack[0]) * k;
        dk.pack[1] += (r - dk.pack[1]) * k;
        for (i = 0; i < 2; i++) {
            dk.rot[i] += en_speed * dt * 26.0f / (FM_TWO_PI * dk.pack[i]);
            dk.rot[i] -= fm_floorf(dk.rot[i]);
        }
    }
    flakes_tick(dt, p);
    for (i = 0; i < NFLAKE; i++)
        live += dk.flife[i] != 0;
    h = hash(hash(2166136261u, (uint32_t)(dk.rot[0] * 4096.0f) | (uint32_t)(dk.pack[0] * 16.0f) << 12),
             (uint32_t)live | (uint32_t)stock << 6 | (uint32_t)en_frozen << 8 | (uint32_t)en_playing << 9);
    for (i = 0; i < EN_NLAYER; i++)
        h = hash(h, en_lay_state[i] | (uint32_t)(en_lay_pos[i] * 400.0f) << 2 | (uint32_t)(en_lay_wear[i] * 400.0f) << 12 |
                        (uint32_t)(en_lay_rec[i] * 200.0f) << 22);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    for (i = 0; i < MAIN_H / BAND_H; i++) {      /* in two bands (the canvas holds one: APP_CANVAS_PX) */
        band_begin((int32_t)i);
        cv_round(3, 1, 234, 112, 6, K_PLATE);              /* the deck plate */
        draw_tape_run(p, stock);
        reel(RL_X, dk.pack[0], dk.rot[0], stock, p >= 0 ? en_lay_wear[p] : 0.0f);
        reel(RR_X, dk.pack[1], dk.rot[1], stock, p >= 0 ? en_lay_wear[p] : 0.0f);
        draw_count(p);
        if (row_hi() > STRIP_Y - 2)
            draw_strips();
        draw_flakes();
        cv_blit(0, MAIN_Y + i * BAND_H);
    }
}

/* the knob strip (X0X's): a rule with the page's name and the page dots at its right end; under
 * it a cell a knob: its name, a ring (or pips, for a switch of a few positions), the value. The
 * knob being turned is drawn on a tint. Only redrawn when what it shows changes (the rings cost a
 * square root and an arctangent per pixel). */
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
    {   /* the rule; at its right end, where you are: the page, then the dots */
        char t[12];
        int k;
        int32_t xd = 237 - NVIEWS * 7, xt;
        caps(t, VIEW_NAME[ui.view], sizeof t);
        xt = xd - 5 - text_w(&FONT_XS, t);
        cv_rect(4, 5, xt - 9, 1, K_LINE);
        cv_text(xt, -1, &FONT_XS, t, K_ACC);
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 7, 3, 4, 4, 2, k == ui.view ? K_ACC : K_LINE);
    }
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v, hot = now_touch && ui.touched == (int)i;
        int32_t x = (int32_t)i * 60, cx = x + 30;
        char t[16];
        uint16_t bg = hot ? K_PANEL : K_BG, fill = hot ? K_ACC : K_ACC_D;
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
        if (hi - lo < 6) {                       /* a switch: its positions as pips */
            int n = hi - lo + 1, j, pw = n > 4 ? 6 : 9, gap = 3, w0 = n * (pw + gap) - gap;
            for (j = 0; j < n; j++)
                cv_round(cx - w0 / 2 + j * (pw + gap), 33, pw, 8, 2, j == v - lo ? fill : K_LINE);
        } else {
            arc(cx, 37, 12, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, K_LINE, fill, bg, lo < 0);
        }
        knob_text(k, t);
        text_c(cx, 50, text_w(&FONT_B, t) <= 56 ? &FONT_B : &FONT_XS, t, hot ? K_ACC : K_TEXT);
    }
    cv_blit(0, KNB_Y);
}

static void leds(void)
{
    uint32_t b = 0, k = 0, i, ap = pool_abs();
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (rec_layer() >= 0)
        b |= 1u << B_REC;
    if (en_frozen)
        b |= 1u << B_EDIT;
    if (proj.leds)
        for (i = 0; i < NKEYS; i++)
            if (ap >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    plat_glow(proj.leds == 1);
    plat_leds(b, k);
}

/* AUTOSAVE: only the settings are kept (the loops are not: a power-on records a fresh one). Once the knobs have
 * rested AUTOSAVE_REST, at most every AUTOSAVE_GAP, and at a quiet moment: a flash erase silences the audio for
 * a moment, so it waits for a dip in the music (or, after AUTOSAVE_WAIT, takes the next quieter moment). Stopped,
 * it saves once rested. SAVE saves at once. */
#define AUTOSAVE_REST 10000u
#define AUTOSAVE_GAP 300000u
#define AUTOSAVE_WAIT 120000u
static void autosave(void)
{
    uint32_t now = plat_ms();
    float lvl = en_out_level;
    int quiet;
    ui.lvl_avg += (lvl - ui.lvl_avg) * 0.01f;
    if (!ui.dirty || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_REST)
        return;
    if (ui.saved_once && now - ui.saved_t < AUTOSAVE_GAP)
        return;
    if (!en_playing && en_speed <= 0.0f)
        quiet = 1;
    else
        quiet = lvl < 0.02f || lvl < ui.lvl_avg * 0.55f ||
                (now - ui.due_t > AUTOSAVE_WAIT && now - ui.act_t > AUTOSAVE_WAIT && lvl < ui.lvl_avg);
    if (!quiet)
        return;
    if (project_save() == 0) {
        ui.dirty = 0;
        ui.saved_t = now;
        ui.saved_once = 1;
    }
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader on the computer (announce.c; fm1-reader): a page once it settles,
 * with its knobs; a knob's value as it turns; a help card as it comes up; each new loop as it starts recording,
 * with its chords (queued); what a button did (say). WHERE (the reader asks, or it has just started listening)
 * is the whole screen in two: the page, its knobs and the header, then the deck (the loops and their wear, the
 * chords). Nothing is built while no reader listens. */
static struct {
    uint8_t view, help, started;
    int16_t val[4];
    uint16_t rec;                              /* the loop being recorded, as last said (0 none) */
} sp;
static char sp_buf[ANN_MAX + 1];
static uint32_t sp_n;
/* the chord names (en_chord_name's, after the root) in words: "m" and "M7" differ only in capitals, which
 * the words file ignores */
static const char *const CHORD_Q[][2] = {
    {"", "major"}, {"m", "minor"}, {"7", "7"}, {"M7", "major 7"}, {"m7", "minor 7"}, {"6", "6"},
    {"m6", "minor 6"}, {"add9", "add 9"}, {"madd9", "minor add 9"}, {"aug", "augmented"}, {"dim", "diminished"},
    {"dim7", "diminished 7"}, {"m7b5", "half diminished"}, {"mM7", "minor major 7"}, {"sus2", "suspended 2"},
    {"sus4", "suspended 4"}, {"7sus4", "7 suspended 4"}, {"5", "5"},
};
#define NCHORD_Q (sizeof CHORD_Q / sizeof CHORD_Q[0])

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
static int sp_same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

/* a value as it is said: "8 s" as "8 seconds", "4 min" as "4 minutes", the rest as shown */
static void sp_value(int k)
{
    char t[16];
    uint32_t i, v = 0;
    knob_text(k, t);
    for (i = 0; t[i] >= '0' && t[i] <= '9'; i++)
        v = v * 10u + (uint32_t)(t[i] - '0');
    if (i && t[i] == ' ' && t[i + 1] == 's' && !t[i + 2]) {
        sp_int(v);
        sp_cat(v == 1u ? " second" : " seconds");
    } else if (i && t[i] == ' ' && sp_same(t + i + 1, "min")) {
        sp_int(v);
        sp_cat(v == 1u ? " minute" : " minutes");
    } else {
        sp_cat(t);
    }
}
static void sp_knob(int view, int i)           /* "Decay 4 minutes": the name the page gives it, then its value */
{
    sp_cat(KNOB_LABEL[view][i]);
    sp_cat(" ");
    sp_value(VIEW_KNOB[view][i]);
}

static void sp_chord_q(int root, const char *q)   /* "E minor 7" */
{
    uint32_t i;
    sp_cat(EN_NOTE_NAME[root % 12]);
    sp_cat(" ");
    for (i = 0; i < NCHORD_Q; i++)
        if (sp_same(q, CHORD_Q[i][0])) {
            sp_cat(CHORD_Q[i][1]);
            return;
        }
    sp_cat(q);
}
static void sp_chords(void)                    /* the newest phrase's chords: "D major, G major, B minor" */
{
    int k, n = en_prog_n > 4 ? 4 : en_prog_n;
    for (k = 0; k < n; k++) {
        int pcs[4], j, nt = en_prog_nt[k] > 4 ? 4 : en_prog_nt[k];
        char nm[16];
        const char *q, *r;
        for (j = 0; j < nt; j++)
            pcs[j] = en_prog_pcs[k][j];
        if (!nt)
            continue;
        q = en_chord_name(pcs, nt, nm);
        r = EN_NOTE_NAME[pcs[0] % 12];
        while (*r && *q == *r)
            q++, r++;
        if (k)
            sp_cat(", ");
        sp_chord_q(pcs[0] % 12, q);
    }
}

/* the page: its name and its knobs; all: the header too (the preset, its key, the tape running or stopped) */
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
    sp_cat(". Preset ");
    sp_cat(preset_name(proj.preset));
    sp_cat(", ");
    sp_cat(EN_NOTE_NAME[proj.par[P_ROOT]]);
    sp_cat(" ");
    sp_cat(EN_MODE_NAME[proj.par[P_MODE]]);
    sp_cat(en_playing ? ", tape running" : ", tape stopped");
    if (en_frozen)
        sp_cat(", hold");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}

/* the deck: each loop, newest first ("Loop 4 recording, loop 3 worn 52%, loop 2 worn 90% fading"), the chords */
static void sp_deck(void)
{
    int order[EN_NLAYER], n = live_layers(order), k;
    sp_n = 0;
    sp_buf[0] = 0;
    if (!n)
        sp_cat("No loops yet");
    for (k = 0; k < n; k++) {
        int i = order[k];
        sp_cat(k ? ", loop " : "Loop ");
        sp_int(en_lay_serial[i]);
        if (en_lay_state[i] == LS_REC) {
            sp_cat(" recording");
            continue;
        }
        sp_cat(" worn ");
        sp_int((uint32_t)(fm_clampf(en_lay_wear[i], 0.0f, 1.0f) * 100.0f + 0.5f));
        sp_cat("%");
        if (en_lay_state[i] == LS_FADE)
            sp_cat(" fading");
    }
    if (en_prog_n) {
        sp_cat(". Chords ");
        sp_chords();
    }
}

/* a help card: its button, then its lines ("ENV button. THE TONE. STOCK, TONE, HISS, SPACE") */
static void sp_line(const char *s)
{
    char p = sp_n ? sp_buf[sp_n - 1] : 0;
    if (*s == ' ')                             /* it goes on from the line before */
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

static void sp_preset_text(void)               /* "PRESET Cassette, D Ionian" */
{
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("PRESET ");
    sp_cat(preset_name(proj.preset));
    sp_cat(", ");
    sp_cat(EN_NOTE_NAME[proj.par[P_ROOT]]);
    sp_cat(" ");
    sp_cat(EN_MODE_NAME[proj.par[P_MODE]]);
}
static void sp_preset(void)
{
    if (!ann_on())
        return;
    sp_preset_text();
    ann_say(sp_buf, 0, ANN_NOW);
    sp_said = 1;
}

static void sp_recording(int r)                /* "Recording loop 4: D major, G major" */
{
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("Recording loop ");
    sp_int(en_lay_serial[r]);
    if (en_prog_n) {
        sp_cat(": ");
        sp_chords();
    }
}

static void sp_snap(void)
{
    int i, r = rec_layer();
    for (i = 0; i < 4; i++)
        sp.val[i] = (int16_t)knob_get(VIEW_KNOB[ui.view][i]);
    sp.view = ui.view;
    sp.help = ui.help;
    sp.rec = r >= 0 ? en_lay_serial[r] : 0;
}

static void speak(void)
{
    uint32_t now = plat_ms();
    int where = ann_where(), i, changed = -1, r;
    if (!ann_on()) {
        sp.started = 0;
        sp_said = 0;
        return;
    }
    if (!sp.started || where) {                       /* everything on the screen, in two */
        sp.started = 1;
        sp_snap();
        if (ui.help) {
            sp_help();
            ann_say(sp_buf, 0, ANN_NOW);
            sp_page(1);
        } else {
            sp_page(1);
            ann_say(sp_buf, 0, ANN_NOW);
            sp_deck();
        }
        ann_say(sp_buf, 0, 0);
        sp_said = 0;
        return;
    }
    if (sp_said) {                                    /* a button said what it did (and what it changed) */
        sp_said = 0;
        sp_snap();
        return;
    }
    if (ui.help != sp.help) {                         /* a help card: at once */
        sp.help = ui.help;
        if (ui.help) {
            sp_help();
            ann_say(sp_buf, 0, ANN_NOW);
            return;
        }
    }
    if (ui.view != sp.view) {                         /* a new page: said when it settles */
        sp_snap();
        sp_page(0);
        ann_later(sp_buf, ANN_NOW, 200u, now);
        return;
    }
    for (i = 0; i < 4; i++) {                         /* a knob turning */
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
    r = rec_layer();
    if (r >= 0 && en_lay_serial[r] != sp.rec) {       /* a new loop: queued, after what is being said */
        sp.rec = en_lay_serial[r];
        sp_recording(r);
        ann_say(sp_buf, 0, 0);
    } else if (r < 0) {
        sp.rec = 0;
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card, chord and message as a screen reader gets them (the "speakdump"
 * command): KIND <tab> the text sent (fm1-reader/tools/audit.mjs puts it in words with web/words/ and checks it) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"},
                                         {"TAPE", "STOPPED"}, {"TAPE", "RUNNING"}, {"NEW LOOP", 0},
                                         {"HOLD", "ON"}, {"HOLD", "OFF"}, {"SAVED", 0}, {"KEYS OCTAVE", "-2"},
                                         {"KEYS OCTAVE", "-1"}, {"KEYS OCTAVE", "0"}, {"KEYS OCTAVE", "+1"},
                                         {"KEYS OCTAVE", "+2"}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}, {"KEYS OCTAVE", "AT THE TOP"},
                                          {"KEYS OCTAVE", "AT THE BOTTOM"}};
    project_t keep = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty;
    uint8_t keep_play = en_playing, keep_frozen = en_frozen;
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
                sp_n = 0;
                sp_knob(v, i);
                sp_print("KNOB", sp_buf);
            }
            proj = keep;
        }
    }
    uint8_t k_state[EN_NLAYER], k_pcs[4][4], k_nt[4], k_n = en_prog_n;
    uint16_t k_serial[EN_NLAYER];
    float k_wear[EN_NLAYER];
    for (i = 0; i < EN_NLAYER; i++)
        k_state[i] = en_lay_state[i], k_serial[i] = en_lay_serial[i], k_wear[i] = en_lay_wear[i];
    for (i = 0; i < 4; i++) {
        k_nt[i] = en_prog_nt[i];
        for (x = 0; x < 4; x++)
            k_pcs[i][x] = en_prog_pcs[i][x];
    }
    for (x = 0; x < 8; x++) {                         /* WHERE: running, stopped, held, unsaved, every mode, loops */
        int j;
        for (j = 0; j < EN_NLAYER; j++)               /* the loops as they might be (the render puts them back) */
            en_lay_state[j] = (uint8_t)(j < x % 4 ? (j == 0 && x > 4 ? LS_REC : j == 2 ? LS_FADE : LS_PLAY) : LS_FREE),
            en_lay_serial[j] = (uint16_t)(x * 3 + 4 - j), en_lay_wear[j] = 0.13f * (float)(j * 3 + x);
        en_prog_n = (uint8_t)(x % 4);
        for (j = 0; j < 4; j++) {
            en_prog_nt[j] = 3;
            en_prog_pcs[j][0] = (uint8_t)((x + j * 5) % 12);
            en_prog_pcs[j][1] = (uint8_t)((x + j * 5 + 3 + (j & 1)) % 12);
            en_prog_pcs[j][2] = (uint8_t)((x + j * 5 + 7) % 12);
        }
        ui.view = (uint8_t)(x % NVIEWS);
        en_playing = (uint8_t)(x & 1);
        ui.dirty = (uint8_t)(x >> 1 & 1);
        en_frozen = (uint8_t)(x == 5);
        proj.par[P_MODE] = (int16_t)x;
        proj.par[P_ROOT] = (int16_t)(x * 5 % 12);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_deck();
        sp_print("WHERE", sp_buf);
    }
    for (i = 0; i < EN_NLAYER; i++)
        en_lay_state[i] = k_state[i], en_lay_serial[i] = k_serial[i], en_lay_wear[i] = k_wear[i];
    en_prog_n = k_n;
    for (i = 0; i < 4; i++) {
        en_prog_nt[i] = k_nt[i];
        for (x = 0; x < 4; x++)
            en_prog_pcs[i][x] = k_pcs[i][x];
    }
    proj = keep;
    ui.dirty = keep_dirty;
    en_playing = keep_play;
    en_frozen = keep_frozen;
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
    for (x = 0; x < 12; x++)
        for (i = 0; i < (int)NCHORD_Q; i++) {
            sp_n = 0;
            sp_cat("Chords ");
            sp_chord_q(x, CHORD_Q[i][0]);
            sp_print("CHORD", sp_buf);
        }
    for (i = 0; i < (int)(sizeof MSG / sizeof MSG[0]); i++) {
        msg_text(MSG[i][0], MSG[i][1], t);
        sp_print("MSG", t);
    }
    for (i = 0; i < (int)(sizeof WARN / sizeof WARN[0]); i++) {
        msg_text(WARN[i][0], WARN[i][1], t);
        sp_print("WARN", t);
    }
    for (x = 0; x < EN_NMODES; x++) {
        msg_text("MODE", EN_MODE_NAME[x], t);
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
    sp_print("MSG", "Recording loop 12: D major, B minor 7");
    for (x = 0; x <= 4; x++)                          /* the battery, at the end of WHERE's first part */
        sp_print("WHERE", batt_words(x));
}
#endif

void ui_init(void)
{
    int i;
    memset(&ui, 0, sizeof ui);
    memset(&dk, 0, sizeof dk);
    ui.touched = -1;
    for (i = 0; i < NKEYS; i++)
        ui.knote[i] = -1;
    dk.pack[0] = R_MAX;
    dk.pack[1] = R_MIN;
    dk.rng = 0x2545F491u ^ plat_ms();
    dk.t = plat_ms();
    lcd_fill(0, 0, 240, 240, K_BG);
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
