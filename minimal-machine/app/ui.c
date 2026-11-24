/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (MINIMAL MACHINE) */
/* MINIMAL MACHINE's UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too. AMB-1's UI (its drawing helpers, the knob strip, help cards, the screen reader and the
 * knobs' acceleration, which are X0X's) with a new screen: the six loops as rings turning against each other.
 *
 *   PLAY           running / stopped            REC    HOLD: no mutations while held
 *   EDIT           one mutation, now            OCT- / OCT+   the previous / next preset
 *   SELECT         the pages (with HOME held: the tempo)    ALGORITHM  the key    PRESETS  the preset
 *   HOME ENV FX GLO   the pages: Machine, Mix, Space, Setup
 *   KNOB 1-4       the page's four values
 *   white keys 1-6 mute / unmute Kick, Hats, Perc, Stab, Bass, Bleep
 *   SAVE           saves now (it also saves by itself, rarely: see autosave); held 1.5 s: the preset back to its
 *                  recipe */
#include "fastmath.h"

enum { V_MACHINE, V_MIX, V_SPACE, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Machine", "Mix", "Space", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_FX, B_GLO};
enum { K_LEDS = 100, K_USB, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_DENSITY, P_DRIFT, P_FILTER, P_ECHO},
    {P_DRUMS, P_STAB, P_BASS, P_BLEEP},
    {P_SWING, P_ETIME, P_REVERB, P_DUCK},
    {P_TEMPO, P_KEY, K_USB, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Density", "Drift", "Filter", "Echo"},
    {"Drums", "Stab", "Bass", "Bleep"},
    {"Swing", "Echo time", "Reverb", "Duck"},
    {"Tempo", "Key", "USB level", "Lights"},
};
static const uint8_t PART_KEY[MM_NT] = {0, 2, 4, 6, 7, 9};   /* the first six white keys (F3 .. D4) */

/* the screen's colours: a dark room, each loop its own lamp */
#define K_BG RGB(12, 14, 19)
#define K_PANEL RGB(30, 34, 44)
#define K_LINE RGB(46, 52, 64)
#define K_DIM RGB(110, 118, 134)
#define K_TEXT RGB(226, 230, 236)
#define K_ACC RGB(255, 196, 92)          /* the accent: the knob being turned, a message's word, a mutation */
#define K_FLASH RGB(255, 250, 236)
static const uint16_t PART_C[MM_NT] = {RGB(236, 236, 228), RGB(120, 210, 230), RGB(222, 112, 172),
                                       RGB(255, 176, 72), RGB(226, 82, 70), RGB(120, 222, 150)};

static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];               /* detents in the current quick run, per knob (accel) */
    uint32_t down_t[NB];               /* when each button went down */
    uint32_t turn_t;                   /* the last knob turn */
    uint8_t help;                      /* a button's card is up: the button + 1 */
    uint8_t reset_done;                /* SAVE held: the preset went back to its recipe this hold */
    uint8_t dirty;                     /* a setting changed since the last save */
    uint32_t dirty_t;                  /* the last change */
    uint32_t act_t, saved_t;
    uint32_t muts_saved;               /* the mutations the last save had seen */
    uint32_t due_t, hush_t;            /* a save due since; the last quiet moment asked for */
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;                    /* the knob turned last (0..3), shown bright for a moment */
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];                   /* what each band shows: redrawn when it changes */
} ui;

/* the screen reader (announce.c; speak() below): a message went to it this frame, so speak() leaves what it
 * changed be; and the transport as last said */
static uint8_t sp_said;
static struct {
    uint8_t playing;
    uint32_t play_t;
} sp_tp;
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
    ui.dirty = 1;
    ui.dirty_t = plat_ms();
}
int ui_dirty(void) { return ui.dirty != 0; }

static const char *chord_now(char *nm) { return mm_chord_name(proj.preset, mm_chord, proj.par[P_KEY], nm); }

/* ------------------------------------------------------------ values --- */
static int knob_get(int k)
{
    if (k < P_NPARAMS)
        return proj.par[k];
    if (k == K_LEDS)
        return proj.leds;
    if (k == K_USB)
        return proj.usb_full;
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = mm_param_info(k)->lo;
        *hi = mm_param_info(k)->hi;
    } else {
        *lo = 0;
        *hi = k == K_LEDS ? 2 : k == K_USB ? 1 : 0;
    }
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k < P_NPARAMS)
        mm_param_text(k, v, b);
    else if (k == K_LEDS)                         /* On: the keys, and the unlit buttons glow; Keys: no glow */
        cat_s(b, v == 2 ? "Keys" : v ? "On" : "Off");
    else if (k == K_USB)
        cat_s(b, v ? "Full" : "Master");
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
        mm_set(k, v);
    } else if (k == K_LEDS) {
        proj.leds = (uint8_t)v;
    } else if (k == K_USB) {
        proj.usb_full = (uint8_t)v;
        plat_usb_audio_full(v);
    }
    mark_dirty();
}

/* knob acceleration by speed (X0X's): a few clicks are one step each however quick; a longer quick turn speeds up
 * to 8 a detent. Small ranges never accelerate. */
#ifdef PLAT_HOST
#define accel_off plat_host_exact             /* the host simulator's "spin": exact steps */
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
static void set_preset(int i)                   /* the preset playing kept as it is; preset i as it was left */
{
    project_preset(i);
    project_apply();
    mm_seed(plat_ms());
    ui.muts_saved = mm_muts;
    say_as("PRESET", preset_name(proj.preset), 0);
    sp_preset();                                 /* a screen reader: the preset with its key and tempo */
    mark_dirty();
}

static void set_key(int k)
{
    k = (k % 12 + 12) % 12;
    knob_set(P_KEY, k);
    say("KEY", MM_NOTE_NAME[k]);
}

static void toggle_part(int t)                  /* a key: a part muted or back */
{
    proj.mute ^= (uint8_t)(1u << t);
    mm_mute(proj.mute);
    say(proj.mute >> t & 1u ? "MUTED" : "UNMUTED", MM_PART_NAME[t]);
    mark_dirty();
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
        mm_mutate();
        say("MUTATE", 0);
        break;
    case B_PLAY:
        sp_tp.playing = !mm_playing;
        sp_tp.play_t = plat_ms();
        if (mm_playing) {
            mm_play(0);
            say("STOPPED", 0);
        } else {
            mm_seed(plat_ms());
            mm_play(1);
            say("PLAYING", 0);
        }
        break;
    case B_REC:
        proj.held = !proj.held;
        mm_hold(proj.held);
        say("HOLD", proj.held ? "ON" : "OFF");
        mark_dirty();
        break;
    case B_OCTDN: case B_OCTUP:
        set_preset(proj.preset + (b == B_OCTUP ? 1 : -1));
        break;
    case B_SAVE:
        if (project_save() == 0) {
            ui.dirty = 0;
            ui.saved_t = plat_ms();
            ui.muts_saved = mm_muts;
            say("SAVED", 0);
        } else {
            say_as("NOT SAVED", "FLASH ERROR", ANN_NOW | ANN_KEEP);
        }
        break;
    default: break;
    }
}

/* HELP CARDS (X0X's): hold one button alone for HELP_HOLD and a card says what it does. A key, a turn or another
 * button puts it away; letting go after its card does nothing else. PLAY acts on its press: no card. */
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
    case B_HOME: L("THE MACHINE"); L("DENSITY, DRIFT, FILTER, ECHO"); L("+ SELECT: TEMPO"); break;
    case B_ENV: L("THE MIX"); L("DRUMS, STAB, BASS, BLEEP"); L("KEYS 1-6: MUTE A PART"); break;
    case B_FX: L("THE SPACE"); L("SWING, ECHO TIME,"); L("REVERB, DUCK"); break;
    case B_GLO: L("SETUP"); L("TEMPO, KEY, USB LEVEL,"); L("LIGHTS"); break;
    case B_EDIT: L("ONE MUTATION, NOW"); break;
    case B_REC: L("HOLD: THE LOOPS STAY"); L(" AS THEY ARE"); L("AGAIN: THEY DRIFT"); break;
    case B_OCTDN: L("THE PREVIOUS PRESET"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE NEXT PRESET"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES NOW"); L("KEEP HOLDING: THE PRESET"); L(" BACK TO ITS RECIPE"); break;
    default: break;
    }
#undef L
    return n;
}

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
    for (i = 0; i < NB; i++) {
        const char *l[HELP_LINES];
        if (btn == 1u << i && i != B_PLAY && help_lines(i, l) && !(ui.btn_used & btn) && now - ui.down_t[i] >= HELP_HOLD &&
            (int32_t)(ui.turn_t - ui.down_t[i]) <= 0) {
            ui.help = (uint8_t)(i + 1);
            ui.btn_used |= btn;                  /* its release now does nothing */
        }
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
    for (i = 0; i < MM_NT; i++)
        if ((ch & keys) >> PART_KEY[i] & 1u)
            toggle_part((int)i);
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    {
        uint32_t pkt;                            /* MIDI in: nothing it does (the queue kept empty) */
        while (plat_midi_in(&pkt, 0))
            ;
    }
    if ((e = plat_enc(EN_SELECT)) != 0) {        /* SELECT: the pages; with HOME held, the tempo */
        if (btn >> B_HOME & 1u) {
            turn(EN_SELECT, P_TEMPO, e);
            ui.btn_used |= 1u << B_HOME;
            {
                char t[8];
                itoa_u((uint32_t)proj.par[P_TEMPO], t);
                say("TEMPO", t);
            }
        } else {
            int v = (int)ui.view + (e > 0 ? 1 : -1);
            if (v >= 0 && v < NVIEWS)
                set_view(v);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        set_key(proj.par[P_KEY] + (e > 0 ? 1 : -1));
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        set_preset(proj.preset + (e > 0 ? 1 : -1));
        ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && plat_ms() - ui.down_t[B_SAVE] > RESET_HOLD) {
        ui.reset_done = 1;                     /* held: the preset back to its recipe (not saved) */
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
/* three bands: the header (28 px), the machine (144), the knobs (68) */
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

/* anti-aliased drawing (X0X's): a pixel blended over the canvas, a disc, a knob's ring */
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

/* a knob: a 270-degree track, anti-aliased, round ends; the value filled from the start and a round handle */
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

/* the battery (plat_battery): 0 (low) .. 3 bars, 4 a USB host powers the FM-1, -1 not read yet. The simulator's
 * "battery N" (host.c) stands in for it */
#ifdef PLAT_HOST
static int8_t batt_host = -2;                  /* -2: plat_battery's */
static int batt(void) { return batt_host > -2 ? batt_host : plat_battery(); }
#else
#define batt() plat_battery()
#endif
#define K_LOW RGB(220, 84, 52)
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
        cv_rect(x + 2 + i * 5, 11, 4, 6, b <= 1 ? K_LOW : K_DIM);
}
static const char *batt_words(int b)
{
    static const char *const W[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                     "On USB power"};
    return b >= 0 && b <= 4 ? W[b] : 0;
}

/* the header: the preset in a tag, the key's chord; on the right the battery, an unsaved dot, the tempo with a beat
 * light (a play triangle while stopped). Messages take its place for a moment. */
static void draw_header(void)
{
    char t[24];
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(hash(2166136261u, proj.preset | (uint32_t)proj.par[P_KEY] << 8 | (uint32_t)mm_chord << 12 |
                                                 (uint32_t)proj.held << 13),
                           mm_playing | ui.dirty << 4 | (uint32_t)msg << 5),
                      (uint32_t)proj.par[P_TEMPO] | (uint32_t)(batt() + 1) << 24);
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1]), ui.msg_until);
    if (mm_playing)
        h = hash(h, (mm_six / 4u) & 1u);
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
            x -= 6;
        }
        if (ui.dirty) {
            cv_round(x - 5, 12, 5, 5, 2, K_DIM);
            x -= 11;
        }
        itoa_u((uint32_t)proj.par[P_TEMPO], t);
        x -= text_w(&FONT_S, t);
        cv_text(x, 6, &FONT_S, t, mm_playing ? K_TEXT : K_DIM);
        x -= 16;
        if (mm_playing) {                         /* the beat */
            int on = ((mm_six / 4u) & 1u) == 0u;
            cv_round(x, 9, 10, 10, 5, on ? K_ACC : K_LINE);
        } else {
            int32_t j;
            for (j = 0; j < 5; j++)
                cv_rect(x + 2 + j, 9 + j, 1, 11 - 2 * j, K_DIM);
        }
        w = text_w(&FONT_B, preset_name(proj.preset)) + 12;
        cv_round(4, 4, w, 20, 6, K_TEXT);
        cv_text(10, 6, &FONT_B, preset_name(proj.preset), K_BG);
        w += 10;
        chord_now(t);
        if (w + text_w(&FONT_XS, t) + 4 <= x)
            w = cv_text(w, 8, &FONT_XS, t, K_ACC);
        if (proj.held && w + 3 + text_w(&FONT_XS, "HOLD") + 4 <= x)
            cv_text(w + 6, 8, &FONT_XS, "HOLD", K_DIM);
    }
    cv_blit(0, 0);
}

/* the machine: six loops as rings around a centre, each turning at its own speed so that the step playing is always
 * at the top, under the read head; the shorter the loop, the faster it turns. Lit steps sound; a step flashes as
 * it plays; a mutation flashes white where it happened. Left: the bar; right: the loops' lengths */
#define CX 120
#define CY 72
static const float RING_R[MM_NT] = {17.0f, 27.5f, 38.0f, 48.5f, 58.5f, 68.0f};

/* the rings' circles, anti-aliased, worked out once: each pixel's offset from the centre and its coverage */
#define RL_MAX 3600
static struct {
    int8_t x, y;
    uint8_t a;
} rl[RL_MAX];
static uint16_t rl_at[MM_NT + 1];
static void rings_make(void)
{
    int t, n = 0;
    for (t = 0; t < MM_NT; t++) {
        float r = RING_R[t];
        int32_t ri = (int32_t)r + 2, x, y;
        rl_at[t] = (uint16_t)n;
        for (y = -ri; y <= ri; y++)
            for (x = -ri; x <= ri; x++) {
                float dx = (float)x + 0.5f, dy = (float)y + 0.5f, d = fm_sqrtf(dx * dx + dy * dy);
                float k = fm_clampf(1.0f - fm_fabsf(d - r), 0.0f, 1.0f);
                if (k > 0.05f && n < RL_MAX) {
                    rl[n].x = (int8_t)x;
                    rl[n].y = (int8_t)y;
                    rl[n].a = (uint8_t)(k * 16.0f + 0.5f);
                    n++;
                }
            }
    }
    rl_at[MM_NT] = (uint16_t)n;
}
static void ring_draw(int t, uint16_t c, uint32_t a16)  /* ring t, its brightness 0..16 */
{
    int i;
    for (i = rl_at[t]; i < rl_at[t + 1]; i++)
        px_blend(CX + rl[i].x, CY + rl[i].y, c, rl[i].a * a16 / 16u);
}

static void draw_machine(void)
{
    int t;
    float ph = mm_playing ? mm_phase : 0.0f;
    char s[16];
    if (!rl_at[MM_NT])
        rings_make();
    /* the read head: a faint line from the centre up, a notch above the rings */
    cv_rect(CX, CY - 71, 1, 58, K_LINE);
    cv_rect(CX - 2, 0, 5, 2, K_DIM);
    for (t = 0; t < MM_NT; t++) {
        int len = mm_len[t], k, pos = mm_pos[t];
        uint32_t pat = mm_pat[t];
        float r = RING_R[t], lv = mm_level[t], g = mm_glow[t];
        uint16_t c = mix(K_LINE, PART_C[t], 3 + (int)(lv * 13.0f));
        int mut = mm_mut_part == t && mm_mut_glow > 0.0f;
        if (len < 1)
            continue;
        ring_draw(t, PART_C[t], 2u + (uint32_t)(lv * 3.0f));
        if (mut && mm_mut_step < 0)              /* a whole loop changed: its ring flashes */
            ring_draw(t, K_FLASH, (uint32_t)q16(mm_mut_glow));
        for (k = 0; k < len; k++) {
            float u = ((float)(pos - k) + ph) / (float)len;   /* turns past the head, clockwise */
            float a, x, y;
            int on = (int)(pat >> k & 1u);
            if (u < 0.0f)
                u += 1.0f;
            a = u * FM_TWO_PI;
            x = (float)CX + fm_sinf(a) * r;
            y = (float)CY - fm_cosf(a) * r;
            if (mut && k == mm_mut_step)
                disc(x, y, 3.0f + 5.0f * mm_mut_glow, mix(K_BG, K_FLASH, q16(mm_mut_glow) * 3 / 4));
            if (on) {
                int hot = k == pos && g > 0.05f && mm_playing;
                float trail = mm_playing && u < 0.18f ? 1.0f - u * (1.0f / 0.18f) : 0.0f;   /* the sweep's wake */
                float rr = hot ? 3.0f + 2.2f * g : 2.7f;
                if (hot)
                    disc(x, y, rr + 3.0f, mix(K_BG, c, q16(g) / 3));
                disc(x, y, rr, mix(c, K_FLASH, hot ? q16(g) * 3 / 4 : q16(trail) / 3));
            } else {
                disc(x, y, 1.0f, mix(K_LINE, PART_C[t], 3));
            }
            if (k == 0)                         /* the loop's start: a tick outside the ring */
                disc(x + fm_sinf(a) * 4.5f, y - fm_cosf(a) * 4.5f, 0.9f, mix(K_LINE, PART_C[t], 8));
        }
    }
    disc((float)CX, (float)CY, 3.0f + 7.0f * mm_kick_env * mm_level[T_KICK],
         mix(K_PANEL, PART_C[T_KICK], 4 + q16(mm_kick_env) * 3 / 4));
    /* left: the bar, a mutation's words */
    cv_text(6, 4, &FONT_XS, "BAR", K_DIM);
    itoa_u(mm_six / 16u + 1u, s);
    cv_text(6, 18, &FONT_S, s, K_TEXT);
    if (proj.held)
        cv_text(6, 40, &FONT_XS, "HOLD", K_ACC);
    if (mm_mut_glow > 0.0f && mm_mut_part >= 0 && mm_mut_kind >= 0) {
        uint16_t c = mix(K_BG, K_ACC, 4 + q16(mm_mut_glow) * 3 / 4);
        caps(s, MM_PART_NAME[mm_mut_part], sizeof s);
        cv_text(6, 108, &FONT_XS, s, c);
        cv_text(6, 123, &FONT_XS, MM_MUT_NAME[mm_mut_kind], c);
    }
    /* right: each loop's name and length */
    for (t = 0; t < MM_NT; t++) {
        int32_t y = 8 + t * 22, x;
        int off = (proj.mute >> t & 1u) != 0;
        uint16_t c = off ? K_LINE : mix(K_DIM, PART_C[t], 4 + (int)(mm_level[t] * 12.0f));
        caps(s, MM_PART_NAME[t], sizeof s);
        x = cv_text(194, y, &FONT_XS, s, c);
        (void)x;
        itoa_u(mm_len[t], s);
        cv_text(236 - text_w(&FONT_XS, s), y, &FONT_XS, s, off ? K_LINE : K_TEXT);
    }
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

static void draw_main(void)
{
    uint32_t h;
    int t;
    if (ui.frame % 3u)                           /* 20 frames a second */
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
    h = hash(2166136261u, mm_six | (uint32_t)(mm_phase * 64.0f) << 24);
    for (t = 0; t < MM_NT; t++)
        h = hash(hash(h, mm_pat[t] ^ (uint32_t)mm_len[t] << 26), mm_pos[t] | (uint32_t)q16(mm_glow[t]) << 8 |
                 (uint32_t)q16(mm_level[t]) << 16);
    h = hash(h, (uint32_t)q16(mm_mut_glow) | (uint32_t)mm_mut_part << 8 | proj.mute << 16 | (uint32_t)proj.held << 24);
    h = hash(h, (uint32_t)q16(mm_kick_env));
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    draw_machine();
    cv_blit(0, MAIN_Y);
}

/* the knob strip (X0X's): a rule with the page's name and its dots; under it a cell a knob: its name, a ring (or
 * pips, for a switch of a few positions), the value. The knob being turned is drawn on a tint. */
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
        int32_t xd = 237 - NVIEWS * 6, xt;
        caps(t, VIEW_NAME[ui.view], sizeof t);
        xt = xd - 5 - text_w(&FONT_XS, t);
        cv_rect(4, 5, xt - 9, 1, K_LINE);
        cv_text(xt, -1, &FONT_XS, t, K_ACC);
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 6, 3, 4, 4, 2, k == ui.view ? K_ACC : K_LINE);
    }
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v, hot = now_touch && ui.touched == (int)i;
        int32_t x = (int32_t)i * 60, cx = x + 30;
        char t[16];
        uint16_t bg = hot ? K_PANEL : K_BG, fill = hot ? K_ACC : PART_C[T_HATS];
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
            arc(cx, 37, 12, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, K_LINE, fill, bg);
        }
        knob_text(k, t);
        text_c(cx, 50, &FONT_B, t, hot ? K_ACC : K_TEXT);
    }
    cv_blit(0, KNB_Y);
}

/* lights: the page's button, PLAY while running, REC while held; the six part keys: dim while the part plays,
 * bright as it sounds, dark when muted */
static void leds(void)
{
    uint32_t b = 1u << VIEW_BTN[ui.view], k = 0, dk = 0;
    int t;
    if (mm_playing)
        b |= 1u << B_PLAY;
    if (proj.held)
        b |= 1u << B_REC;
    if (proj.leds)
        for (t = 0; t < MM_NT; t++) {
            if (proj.mute >> t & 1u)
                continue;
            if (mm_glow[t] > 0.35f && mm_level[t] > 0.3f)
                k |= 1u << PART_KEY[t];
            else
                dk |= 1u << PART_KEY[t];
        }
    plat_lights(b, k, proj.leds == 1 ? ~b & ((1u << NB) - 1u) : 0u, dk, proj.leds ? 1 : 0);
}

/* AUTOSAVE, and the flash's wear: every save erases one of the same two 4 KiB sectors (APP_STORE_MAP's A/B pair),
 * so the machine saves itself rarely:
 *   a setting (ui.dirty): once the knobs have rested 5 s, at most once every 5 minutes;
 *   the loops as they evolved (mutations since the last save): at most once an hour;
 *   SAVE saves at once.
 * A flash erase stalls the sound for a moment, so a due save asks the engine for a quiet moment (mm_hush: the last
 * sixteenth of a bar faded out, as a DJ's cut before the downbeat) and writes in it; stopped, at once. If no quiet
 * moment comes within a minute, it saves anyway. */
#define SAVE_REST_MS 5000u
#define KEEP_SETTINGS_MS 300000u
#define KEEP_LOOPS_MS 3600000u
static int save_due(uint32_t now)
{
    uint32_t since = now - ui.saved_t;
    if (ui.dirty && now - ui.dirty_t >= SAVE_REST_MS && since >= KEEP_SETTINGS_MS)
        return 1;
    return mm_muts != ui.muts_saved && since >= KEEP_LOOPS_MS;
}
static void autosave(void)
{
    uint32_t now = plat_ms();
    if (!save_due(now) || ui.btn || ui.keys || now - ui.turn_t < SAVE_REST_MS) {
        ui.due_t = 0;
        return;
    }
    if (!ui.due_t)
        ui.due_t = now ? now : 1u;
    if (!mm_hushed && now - ui.due_t < 60000u) {
        if (now - ui.hush_t > 2500u) {           /* a quiet moment, please (the bar after next at the latest) */
            ui.hush_t = now;
            mm_hush();
        }
        return;
    }
    if (project_save() == 0)
        ui.dirty = 0;
    ui.muts_saved = mm_muts;
    ui.saved_t = now;                            /* (a failed save waits its turn too: no hammering the flash) */
    ui.due_t = 0;
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader on the computer (announce.c; fm1-reader): a page once it settles, with its
 * knobs; a knob's value as it turns; a help card as it comes up; the stab's chord as a mutation moves it (queued);
 * what a button did (say). WHERE is the whole screen in two: the page, its knobs and the header, then the machine
 * (the loops' lengths, what is muted or resting). Nothing is built while no reader listens. */
static struct {
    uint8_t view, help, started, chord;
    int16_t val[4];
} sp;
static char sp_buf[ANN_MAX + 1];
static uint32_t sp_n;
static const char *const CHORD_Q[][2] = {
    {"m7", "minor 7"}, {"m9", "minor 9"}, {"m11", "minor 11"}, {"maj9", "major 9"}, {"madd9", "minor add 9"},
    {"m", "minor"},
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
static void sp_knob(int view, int i)           /* "Density 50": the name the page gives it, then its value */
{
    char t[16];
    sp_cat(KNOB_LABEL[view][i]);
    sp_cat(" ");
    knob_text(VIEW_KNOB[view][i], t);
    sp_cat(t);
}
static void sp_chord(void)                     /* "F minor 7" */
{
    char nm[16];
    const char *q = chord_now(nm);
    char r[3];
    uint32_t i, n = (q[1] == '#' || q[1] == 'b') ? 2u : 1u;   /* the root: "F", "Bb", "C#" (no quality starts so) */
    r[0] = q[0];
    r[1] = n > 1u ? q[1] : 0;
    r[2] = 0;
    sp_cat(r);
    q += n;
    for (i = 0; i < NCHORD_Q; i++)
        if (sp_same(q, CHORD_Q[i][0])) {
            sp_cat(" ");
            sp_cat(CHORD_Q[i][1]);
            return;
        }
    if (*q) {
        sp_cat(" ");
        sp_cat(q);
    }
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
    sp_cat(". Preset ");
    sp_cat(preset_name(proj.preset));
    sp_cat(", ");
    sp_chord();
    if (ui.view != V_SETUP) {                  /* (Setup's first knob says it) */
        sp_cat(", tempo ");
        sp_int((uint32_t)proj.par[P_TEMPO]);
    }
    sp_cat(sp_tp.playing ? ", playing" : ", stopped");
    if (proj.held)
        sp_cat(", held");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}

static void sp_machine(void)                   /* the loops: each one's length; muted and resting parts */
{
    int t;
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("Loops");
    for (t = 0; t < MM_NT; t++) {
        sp_cat(t ? ", " : " ");
        sp_cat(MM_PART_NAME[t]);
        sp_cat(" ");
        sp_int(mm_len[t]);
        if (proj.mute >> t & 1u)
            sp_cat(" muted");
        else if (mm_level[t] < 0.5f)
            sp_cat(" resting");
    }
    sp_cat(". Bar ");
    sp_int(mm_six / 16u + 1u);
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

static void sp_preset_text(void)               /* "PRESET Dub Chamber, F minor 7, tempo 118" */
{
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("PRESET ");
    sp_cat(preset_name(proj.preset));
    sp_cat(", ");
    sp_chord();
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
    sp.chord = (uint8_t)(mm_chord | proj.par[P_KEY] << 1);
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
            sp_machine();
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
    if (mm_playing != sp_tp.playing && now - sp_tp.play_t > 150u) {
        sp_tp.playing = mm_playing;
        ann_say(mm_playing ? "PLAYING" : "STOPPED", 0, 0);
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
    if ((uint8_t)(mm_chord | proj.par[P_KEY] << 1) != sp.chord) {   /* the stab's chord moved: queued */
        sp.chord = (uint8_t)(mm_chord | proj.par[P_KEY] << 1);
        sp_n = 0;
        sp_cat("Chord ");
        sp_chord();
        ann_say(sp_buf, 0, 0);
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card, chord and message as a screen reader gets them (the "speakdump" command) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}, {"MUTATE", 0},
                                         {"STOPPED", 0}, {"PLAYING", 0}, {"HOLD", "ON"}, {"HOLD", "OFF"},
                                         {"SAVED", 0}, {"NOT SAVED", "FLASH ERROR"}};
    project_t keep_p = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty, keep_play = sp_tp.playing;
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
                else if (k == K_LEDS)
                    proj.leds = (uint8_t)x;
                else
                    proj.usb_full = (uint8_t)x;
                sp_n = 0;
                sp_knob(v, i);
                sp_print("KNOB", sp_buf);
            }
            proj = keep_p;
        }
    }
    for (x = 0; x < 8; x++) {
        ui.view = (uint8_t)(x % NVIEWS);
        sp_tp.playing = (uint8_t)(x & 1);
        ui.dirty = (uint8_t)(x >> 1 & 1);
        proj.held = (uint8_t)(x == 5);
        proj.mute = (uint8_t)(x == 6 ? 0x05u : 0u);
        proj.par[P_KEY] = (int16_t)(x * 5 % 12);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_machine();
        sp_print("WHERE", sp_buf);
    }
    proj = keep_p;
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
    for (x = 0; x < MM_NT; x++) {
        msg_text("MUTED", MM_PART_NAME[x], t);
        sp_print("MSG", t);
        msg_text("UNMUTED", MM_PART_NAME[x], t);
        sp_print("MSG", t);
    }
    for (x = 0; x < 12; x++) {
        msg_text("KEY", MM_NOTE_NAME[x], t);
        sp_print("MSG", t);
    }
    for (x = 0; x < NPRESETS; x++) {
        int k;
        project_preset(x);
        sp_preset_text();
        sp_print("MSG", sp_buf);
        msg_text("RESET", preset_name(x), t);
        sp_print("MSG", t);
        for (k = 0; k < 12; k++) {
            proj.par[P_KEY] = (int16_t)k;
            sp_n = 0;
            sp_cat("Chord ");
            sp_chord();
            sp_print("CHORD", sp_buf);
        }
    }
    proj = keep_p;
    for (x = 90; x <= 140; x += 25) {
        char o[8];
        itoa_u((uint32_t)x, o);
        msg_text("TEMPO", o, t);
        sp_print("MSG", t);
    }
    for (x = 0; x <= 4; x++)
        sp_print("WHERE", batt_words(x));
}
#endif

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.saved_t = plat_ms();
    ui.muts_saved = mm_muts;
    lcd_fill(0, 0, 240, 240, K_BG);
    if (!mm_playing)
        mm_play(1);                              /* it plays from the start */
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
