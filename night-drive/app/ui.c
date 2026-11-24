/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Changed for NIGHT DRIVE by ashtonwallen: the road, the sunset and their controls (AMB-1's knob strip, help cards,
 * screen reader and autosave kept in shape). */
/* NIGHT DRIVE UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host simulator
 * includes it too.
 *
 *   PRESETS        the preset: its sounds, colours, speed, key and progression (a new drive, from the intro)
 *   ALGORITHM      the progression (the next bar)
 *   keys           the key (minor), on the next bar           OCT- / OCT+   the key a semitone down / up
 *   PLAY           drive / park        EDIT  the next section        ARP  the arpeggio's next pattern
 *   REC            a key change of its own choosing, on the next bar
 *   HOME, GLO      the pages: the Drive (Speed, Bright, Arp, Drift) and Setup (Drums, Lead, Lights, USB);
 *                  SELECT steps through them; with HOME held, the speed
 *   SAVE           saves now; held 1.5 s: the preset back to its recipe */
#include "fastmath.h"

enum { V_DRIVE, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Road", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_GLO};
enum { K_LEDS = 100, K_USB, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {{P_TEMPO, P_BRIGHT, P_ARP, P_DRIFT}, {P_DRUMS, P_LEAD, K_LEDS, K_USB}};
static const char *const KNOB_LABEL[NVIEWS][4] = {{"Speed", "Bright", "Arp", "Drift"},
                                                  {"Drums", "Lead", "Lights", "USB"}};

/* the colours of each preset: 8-bit RGB, dithered to the panel's 16 bits where they shade */
enum { PC_SKY_TOP, PC_SKY_HZ, PC_SUN_TOP, PC_SUN_BOT, PC_GRID, PC_MOUNT, PC_GROUND, PC_ACCENT, NPC };
static const uint8_t PAL[NPRESETS][NPC][3] = {
    {{8, 4, 28}, {78, 22, 98}, {255, 214, 90}, {255, 40, 140}, {255, 60, 200}, {34, 12, 66}, {44, 10, 66}, {255, 96, 214}},
    {{40, 10, 62}, {255, 112, 84}, {255, 240, 140}, {255, 70, 70}, {255, 120, 60}, {92, 22, 72}, {72, 16, 52}, {255, 156, 84}},
    {{4, 10, 26}, {44, 86, 138}, {232, 246, 255}, {110, 172, 232}, {92, 222, 255}, {22, 32, 64}, {16, 32, 58}, {144, 232, 255}},
    {{22, 0, 12}, {156, 22, 32}, {255, 232, 64}, {255, 52, 32}, {255, 44, 64}, {54, 6, 22}, {52, 6, 16}, {255, 212, 64}},
    {{0, 10, 22}, {22, 74, 94}, {152, 255, 232}, {40, 142, 204}, {44, 255, 204}, {6, 32, 48}, {6, 28, 42}, {84, 255, 214}},
    {{22, 20, 62}, {255, 152, 142}, {255, 250, 204}, {255, 122, 152}, {84, 222, 232}, {64, 42, 104}, {32, 42, 84}, {124, 242, 232}},
    {{0, 0, 0}, {22, 44, 12}, {232, 255, 84}, {62, 222, 62}, {64, 255, 92}, {10, 32, 10}, {6, 22, 6}, {232, 255, 92}},
    {{10, 0, 32}, {204, 42, 112}, {255, 204, 44}, {255, 32, 92}, {0, 232, 255}, {64, 0, 74}, {42, 0, 54}, {0, 232, 255}},
    {{26, 6, 36}, {194, 72, 124}, {255, 184, 124}, {204, 52, 144}, {255, 112, 184}, {74, 22, 84}, {54, 10, 54}, {255, 174, 204}},
    {{32, 10, 52}, {255, 124, 174}, {255, 242, 174}, {255, 102, 174}, {184, 112, 255}, {84, 42, 114}, {44, 22, 74}, {255, 184, 232}},
    {{12, 0, 0}, {124, 32, 0}, {255, 255, 144}, {255, 124, 0}, {255, 172, 0}, {44, 12, 0}, {42, 12, 0}, {255, 204, 54}},
    {{0, 0, 12}, {32, 22, 74}, {204, 184, 255}, {124, 62, 224}, {144, 92, 255}, {16, 12, 42}, {16, 10, 42}, {184, 154, 255}},
    {{10, 6, 32}, {92, 42, 144}, {255, 222, 112}, {242, 72, 172}, {112, 142, 255}, {36, 22, 72}, {26, 16, 56}, {255, 124, 204}},
    {{6, 0, 22}, {62, 0, 92}, {255, 124, 242}, {112, 42, 255}, {255, 92, 255}, {26, 0, 52}, {24, 0, 44}, {124, 255, 255}},
};
static const uint8_t *pc(int i) { return PAL[proj.preset % NPRESETS][i]; }
static uint16_t pc16(int i) { const uint8_t *c = pc(i); return RGB(c[0], c[1], c[2]); }
#define K_BG RGB(12, 6, 26)              /* the knob strip's night */
#define K_LINE RGB(52, 36, 84)
#define K_DIM RGB(150, 132, 190)
#define K_TEXT RGB(244, 236, 255)
#define K_PANEL RGB(30, 18, 56)
#define K_LOW RGB(255, 84, 72)

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
    uint32_t dirty_t;                  /* when it last changed */
    uint32_t act_t, saved_t, due_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;                    /* the knob turned last (0..3), shown bright for a moment */
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig;                      /* what the knob strip shows: redrawn when it changes */
    uint32_t odo_ms;                   /* driven since the last tenth of a mile, in ms times mph */
    uint32_t odo_saved;                /* the odometer when last saved */
    uint32_t prev_ms;
} ui;

/* the screen reader (announce.c; speak() below): a message went to it this frame, so speak() leaves what it changed
 * be; the transport as last said */
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
    ui.dirty = 1;
    ui.dirty_t = plat_ms();
}
int ui_dirty(void) { return ui.dirty != 0; }

/* the key, said: "F sharp minor" (the screen's "F#m") */
static const char *const SAY_SHARP[12] = {"C", "C sharp", "D", "D sharp", "E", "F", "F sharp", "G", "G sharp", "A",
                                          "A sharp", "B"};
static const char *const SAY_FLAT[12] = {"C", "D flat", "D", "E flat", "E", "F", "G flat", "G", "A flat", "A",
                                         "B flat", "B"};
static const char *note_say(int pc, int key) { return (en_sharp_key(key) ? SAY_SHARP : SAY_FLAT)[((pc % 12) + 12) % 12]; }

/* ------------------------------------------------------------ values --- */
static int knob_get(int k)
{
    if (k <= P_DRIFT)
        return proj.slot[proj.preset].knob[k - P_TEMPO];
    if (k == P_DRUMS)
        return proj.drums;
    if (k == P_LEAD)
        return proj.lead;
    if (k == K_LEDS)
        return proj.leds;
    if (k == K_USB)
        return proj.usb_full;
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = en_param_info(k)->lo;
        *hi = en_param_info(k)->hi;
    } else {
        *lo = 0;
        *hi = k == K_LEDS ? 2 : k == K_USB ? 1 : 0;
    }
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k < P_NPARAMS)
        en_param_text(k, v, b);
    else if (k == K_LEDS)                       /* On: the keys, and the unlit buttons glow; Keys: no glow */
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
    if (k <= P_DRIFT) {
        proj.slot[proj.preset].knob[k - P_TEMPO] = (int16_t)v;
        en_set(k, v);
    } else if (k == P_DRUMS) {
        proj.drums = (int16_t)v;
        en_set(k, v);
    } else if (k == P_LEAD) {
        proj.lead = (int16_t)v;
        en_set(k, v);
    } else if (k == K_LEDS) {
        proj.leds = (uint8_t)v;
    } else if (k == K_USB) {
        proj.usb_full = (uint8_t)v;
        plat_usb_audio_full(v);
    }
    mark_dirty();
}

/* knob acceleration by speed (X0X's): a few clicks are one step each however quick; a longer quick turn speeds up.
 * Small ranges never accelerate. */
#ifdef PLAT_HOST
#define accel_off plat_host_exact       /* the host simulator's "spin": exact steps */
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
    m = dt < 12u ? 5u : dt < 25u ? 3u : dt < 45u ? 2u : 1u;
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

/* ------------------------------------------------------- the drive --- */
static void set_key(int pc)                     /* the home key, on the next bar */
{
    char t[16];
    pc = ((pc % 12) + 12) % 12;
    proj.slot[proj.preset].key = (uint8_t)pc;
    en_set(P_ROOT, pc);
    en_param_text(P_ROOT, pc, t);
    say_as("KEY", t, 0);
    if (ann_on()) {
        char s[32];
        cat_s(cat_s(cat_s(s, "KEY "), note_say(pc, pc)), " minor");
        ann_say(s, 0, ANN_NOW);
        sp_said = 1;
    }
    mark_dirty();
}

static void set_prog(int p)
{
    p = ((p % EN_NPROG) + EN_NPROG) % EN_NPROG;
    proj.slot[proj.preset].prog = (uint8_t)p;
    en_set(P_PROG, p);
    say("CHORDS", en_prog_name(p));
    mark_dirty();
}

static void mountains_make(void);
static void set_preset(int i)
{
    proj.preset = (uint8_t)(((i % NPRESETS) + NPRESETS) % NPRESETS);
    project_apply();
    en_seed(plat_ms());
    en_restart();
    mountains_make();
    say_as("PRESET", en_preset_name(proj.preset), 0);
    sp_preset();
    ui.sig = 0;
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
    case B_EDIT:
        en_next();
        say("NEXT SECTION", en_playing ? "ON THE BAR" : 0);
        break;
    case B_ARP: {
        static const char *const AP[5] = {"UP", "UP AND DOWN", "PEDAL", "ROLL", "FIFTHS"};
        en_newarp();
        say("ARP", AP[(en_arp_pat + 1) % 5]);
        break;
    }
    case B_REC:
        en_newkey();
        say("KEY CHANGE", en_playing ? "ON THE BAR" : 0);
        break;
    case B_PLAY:
        sp_tp.playing = !en_playing;
        sp_tp.play_t = plat_ms();
        if (en_playing) {
            en_play(0);
            say("PARKED", 0);
        } else {
            en_seed(plat_ms());
            en_play(1);
            say("DRIVING", 0);
        }
        break;
    case B_OCTDN: case B_OCTUP:
        set_key(proj.slot[proj.preset].key + (b == B_OCTUP ? 1 : -1));
        break;
    case B_SAVE:
        if (project_save() == 0) {
            ui.dirty = 0;
            ui.odo_saved = proj.odo;
            ui.saved_t = plat_ms();
            say("SAVED", 0);
        } else {
            warn("NOT SAVED", "FLASH ERROR");
        }
        break;
    default: break;
    }
}

/* MIDI in (USB and the TRS jack): a channel 1 note sets the key */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0)) {
        uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == 0x90u && v)
            set_key((int)(n % 12u));
    }
}

/* HELP CARDS (X0X's): hold one button alone for HELP_HOLD and a card says what it does. A key, a turn or another
 * button puts it away; letting go after its card does nothing else. PLAY acts on its press, SEL has no job: neither
 * has a card; nor do the buttons NIGHT DRIVE leaves free */
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
    case B_HOME: L("THE ROAD"); L("SPEED, BRIGHT, ARP, DRIFT"); L("+ SELECT: SPEED"); L("KEYS: THE KEY"); break;
    case B_GLO: L("SETUP"); L("DRUMS, LEAD, LIGHTS,"); L("USB LEVEL"); break;
    case B_EDIT: L("THE NEXT SECTION,"); L(" ON THE NEXT BAR"); break;
    case B_ARP: L("THE ARPEGGIO'S"); L(" NEXT PATTERN"); break;
    case B_REC: L("A KEY CHANGE,"); L(" ON THE NEXT BAR"); break;
    case B_OCTDN: L("THE KEY A SEMITONE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE KEY A SEMITONE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
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
        if (btn == 1u << i && i != B_PLAY && i != B_SEL && help_lines(i, l) && !(ui.btn_used & btn) &&
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
    for (i = 0; i < NKEYS; i++)
        if ((ch & keys) >> i & 1u)
            set_key((int)((5u + i) % 12u));      /* key 0 is F3 */
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {        /* SELECT: the pages; with HOME held, the speed */
        if (btn >> B_HOME & 1u) {
            turn(EN_SELECT, P_TEMPO, e);
            ui.btn_used |= 1u << B_HOME;
            if (ui.view == V_DRIVE) {
                ui.touched = 0;
                ui.touch_until = plat_ms() + 1200u;
            } else {
                char t[16];
                knob_text(P_TEMPO, t);
                say("SPEED", t);
            }
        } else {
            int v = ui.view + (e > 0 ? 1 : -1);
            if (v >= 0 && v < NVIEWS)
                set_view(v);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        set_prog(proj.slot[proj.preset].prog + (e > 0 ? 1 : -1));
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
        say("RESET", en_preset_name(proj.preset));
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
/* the scene, 240 x 172 in two bands (the canvas holds 240 x 144): the sky with the sun and the mountains, the road
 * from the horizon (HZ) down; then the knob strip, 68 rows */
#define HZ 104
#define SCENE_H 172
#define BAND1 96
#define KNB_Y 172
#define KNB_H 68
#define SUN_X 120
#define SUN_Y (HZ + 6)
#define SUN_R 50
#define HALO 22

static int32_t by0, by1;                         /* the band being drawn: scene rows by0 .. by1 - 1 */
static void band_begin(int32_t y0, int32_t h)
{
    cv_begin(240, (uint32_t)h, 0);
    cv_oy = -y0;                                 /* (gfx.c: drawing in scene rows, clipped to the band) */
    cv_y0 = 0;
    cv_y1 = h;
    by0 = y0;
    by1 = y0 + h;
}
static inline uint16_t *row_px(int32_t y) { return &cv_px[(uint32_t)(y - by0) * 240u]; }

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* 8-bit colour to the panel's 16, with a 4 x 4 ordered dither: the gradients without bands */
static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static inline uint16_t dith(int32_t r, int32_t g, int32_t b, int32_t x, int32_t y)
{
    int32_t d = BAYER[y & 3][x & 3];
    r = (r + (d >> 1)) >> 3;
    g = (g + (d >> 2)) >> 2;
    b = (b + (d >> 1)) >> 3;
    r = r < 0 ? 0 : r > 31 ? 31 : r;
    g = g < 0 ? 0 : g > 63 ? 63 : g;
    b = b < 0 ? 0 : b > 31 ? 31 : b;
    return (uint16_t)(r << 11 | g << 5 | b);
}
static inline int32_t lerp8(int32_t a, int32_t b, int32_t t) { return a + (((b - a) * t) >> 8); }   /* t 0..256 */

/* a pixel blended over the canvas (alpha 0..16) */
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
/* a pixel brightened: c added over the canvas at alpha a (0..16), for the glows */
static void px_add(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *q;
    uint32_t bg, r, g, b;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1 || !a)
        return;
    q = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    bg = swap16(*q);
    r = (bg >> 11) + (((c >> 11) * a) >> 4);
    g = ((bg >> 5) & 63u) + ((((c >> 5) & 63u) * a) >> 4);
    b = (bg & 31u) + (((c & 31u) * a) >> 4);
    *q = swap16((r > 31u ? 31u : r) << 11 | (g > 63u ? 63u : g) << 5 | (b > 31u ? 31u : b));
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
static void caps(char *d, const char *s, int n)  /* s in capitals, at most n - 1 characters */
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}

/* ------------------------------------------------------------ the sky --- */
/* the sky's rows, the sun's halo, the sun (its colours top to bottom, its stripes widening and sinking toward the
 * horizon), all in 8 bits, then dithered: one pass a pixel */
static void draw_sky(void)
{
    const uint8_t *st = pc(PC_SKY_TOP), *sh = pc(PC_SKY_HZ), *s0 = pc(PC_SUN_TOP), *s1 = pc(PC_SUN_BOT);
    int32_t y, x, y0 = by0 > 0 ? by0 : 0, y1 = by1 < HZ ? by1 : HZ;
    float stripe = en_beats * 0.5f, glow = 0.55f + 0.25f * en_pad_level + 0.15f * en_kick;
    for (y = y0; y < y1; y++) {
        uint16_t *p = row_px(y);
        int32_t t = (y * 256) / HZ, r, g, b;
        float dy = (float)y + 0.5f - (float)SUN_Y;
        int32_t sun_lo = SUN_X - SUN_R - HALO, sun_hi = SUN_X + SUN_R + HALO;
        int gap = 0, sun_row = dy > -(float)(SUN_R + HALO);
        t = (t * t) >> 8;                                  /* (darker longer at the top) */
        r = lerp8(st[0], sh[0], t);
        g = lerp8(st[1], sh[1], t);
        b = lerp8(st[2], sh[2], t);
        if (sun_row) {                                     /* the stripes: from 45% down, ever wider */
            float k = (dy + (float)SUN_R) / (float)SUN_R;   /* 0 the sun's top, 1 its middle */
            if (k > 0.30f) {
                float w = 1.0f + 5.5f * (k - 0.30f), u = (float)y - stripe * 8.0f, per = 8.0f;
                float m = u - per * fm_floorf(u / per);
                gap = m < w;
            }
        }
        for (x = 0; x < 240; x++) {
            int32_t R = r, G = g, B = b;
            if (sun_row && x >= sun_lo && x < sun_hi) {
                float dx = (float)x + 0.5f - (float)SUN_X, d = fm_sqrtf(dx * dx + dy * dy);
                if (d < (float)SUN_R + 0.5f && !gap) {
                    int32_t tt = (int32_t)((dy + (float)SUN_R) * (256.0f / (2.0f * (float)SUN_R) * 1.6f)), a;
                    tt = tt < 0 ? 0 : tt > 256 ? 256 : tt;
                    a = (int32_t)(fm_clampf((float)SUN_R + 0.5f - d, 0.0f, 1.0f) * 256.0f);
                    R = lerp8(R, lerp8(s0[0], s1[0], tt), a);
                    G = lerp8(G, lerp8(s0[1], s1[1], tt), a);
                    B = lerp8(B, lerp8(s0[2], s1[2], tt), a);
                } else if (d < (float)(SUN_R + HALO)) {
                    float h = 1.0f - (d - (float)SUN_R) * (1.0f / (float)HALO);
                    int32_t a = (int32_t)(h * h * glow * 200.0f);
                    if (gap && d < (float)SUN_R + 0.5f)
                        a = (int32_t)(glow * 110.0f);      /* (the sky through a stripe: lit by the sun) */
                    R = lerp8(R, s1[0], a);
                    G = lerp8(G, s1[1], a);
                    B = lerp8(B, s1[2], a);
                }
            }
            p[x] = swap16(dith(R, G, B, x, y));
        }
    }
}

/* the stars: fixed, twinkling; brighter when the music thins */
static void draw_stars(void)
{
    int i;
    float bright = 0.45f + 0.55f * (1.0f - en_energy);
    for (i = 0; i < 46; i++) {
        uint32_t h = hash(hash(0x5354u, (uint32_t)i), 77u);
        int32_t x = (int32_t)(h % 240u), y = 3 + (int32_t)((h >> 8) % 78u);
        float dx = (float)(x - SUN_X), dy = (float)(y - SUN_Y), tw;
        uint32_t a;
        if (dx * dx + dy * dy < (float)((SUN_R + HALO) * (SUN_R + HALO)))
            continue;
        tw = 0.5f + 0.5f * fm_sinf((float)ui.frame * (0.05f + 0.01f * (float)(h >> 24 & 7u)) + (float)(h & 63u));
        a = (uint32_t)(fm_clampf(bright * (0.35f + 0.65f * tw) * (1.0f - (float)y / 110.0f), 0.0f, 1.0f) * 16.0f);
        px_add(x, y, RGB(255, 240, 255), a);
        if ((h >> 20 & 7u) == 0) {                         /* a few bright ones, a cross */
            px_add(x - 1, y, RGB(200, 180, 255), a / 3);
            px_add(x + 1, y, RGB(200, 180, 255), a / 3);
            px_add(x, y - 1, RGB(200, 180, 255), a / 3);
            px_add(x, y + 1, RGB(200, 180, 255), a / 3);
        }
    }
}

/* two ridges of mountains, low-poly, lower in the middle where the road meets the sun; outlined in neon */
static uint8_t mt_far[240], mt_near[240];
static void ridge(uint8_t *h, uint32_t seed, int lo, int hi, int step)
{
    int32_t x = -((int32_t)(seed % 13u)), px = x, ph = lo + (int)(seed % (uint32_t)(hi - lo + 1)), i;
    while (px < 240) {
        int32_t nx = px + step / 2 + (int32_t)(seed % (uint32_t)step), nh;
        seed = seed * 1103515245u + 12345u;
        nh = lo + (int32_t)((seed >> 8) % (uint32_t)(hi - lo + 1));
        for (i = px; i < nx && i < 240; i++)
            if (i >= 0) {
                float f = (float)(i - px) / (float)(nx - px), m = fm_minf(fm_fabsf((float)(i - 120)) / 105.0f, 1.0f);
                float v = ((float)ph + (float)(nh - ph) * f) * (0.06f + 0.94f * fm_clampf(m * m * (3.0f - 2.0f * m), 0.0f, 1.0f));
                h[i] = (uint8_t)(v + 0.5f);
            }
        px = nx;
        ph = nh;
        seed = seed * 1103515245u + 12345u;
    }
    (void)x;
}
static void mountains_make(void)
{
    uint32_t s = 2166136261u ^ (proj.preset * 2654435761u);
    ridge(mt_far, s, 14, 34, 34);
    ridge(mt_near, s * 7u + 3u, 4, 20, 22);
}
static void draw_ridge(const uint8_t *h, uint16_t fill, uint16_t edge, uint32_t ea)
{
    int32_t x, y;
    for (x = 0; x < 240; x++) {
        int32_t top = HZ - h[x], prev = x ? HZ - h[x - 1] : top, a, b;
        if (top >= HZ)
            continue;
        for (y = top < by0 ? by0 : top; y < HZ && y < by1; y++)
            row_px(y)[x] = swap16(fill);
        a = prev < top ? prev : top;                       /* the edge: joined to the column before */
        b = prev < top ? top : prev;
        for (y = a; y <= b; y++)
            px_blend(x, y, edge, ea);
    }
}

static void draw_horizon(void)
{
    uint16_t c = pc16(PC_SUN_BOT), g = pc16(PC_GRID);
    int32_t x;
    uint32_t a = (uint32_t)(10 + q16(en_kick) / 3);
    for (x = 0; x < 240; x++) {
        px_add(x, HZ - 1, c, a / 2);
        px_blend(x, HZ, mix(c, g, 8), a);
    }
}

/* ------------------------------------------------------------ the road --- */
/* the ground from the horizon down: depth z = A / dy (dy rows below the horizon); the grid's lines across at whole
 * z + the beat (one a beat, coming on), its lines along converging on the middle; the road in the middle, its edges
 * lit, its dashes passing two a beat */
#define GA 60.0f
static float road_hw(float dy) { return 2.0f + dy * 0.62f; }
static void draw_ground(void)
{
    const uint8_t *g0 = pc(PC_GROUND);
    int32_t y, x, y0 = by0 > HZ ? by0 : HZ, y1 = by1 < SCENE_H ? by1 : SCENE_H;
    float f = en_beats - fm_floorf(en_beats), lit = 0.35f + 0.3f * en_energy + 0.35f * en_kick;
    uint16_t grid = pc16(PC_GRID), edge = mix(pc16(PC_SUN_BOT), RGB(255, 255, 255), 5), dash = RGB(255, 236, 196);
    int n;
    for (y = y0; y < y1; y++) {
        uint16_t *p = row_px(y);
        int32_t dy = y - HZ + 1, t = dy * 256 / (SCENE_H - HZ), r, g, b, rr, rg, rb, hw = (int32_t)road_hw((float)dy);
        r = lerp8(g0[0], g0[0] / 5, t);
        g = lerp8(g0[1], g0[1] / 5, t);
        b = lerp8(g0[2], g0[2] / 5 + 4, t);
        rr = r / 2 + 6;
        rg = g / 2 + 6;
        rb = b / 2 + 12;
        for (x = 0; x < 240; x++) {
            int32_t ax = x - 120 < 0 ? 119 - x : x - 120;
            p[x] = swap16(ax < hw ? dith(rr, rg, rb, x, y) : dith(r, g, b, x, y));
        }
    }
    /* the lines along: x = 120 +- (the road's half width + k a cell), each row */
    for (y = y0; y < y1; y++) {
        float dy = (float)(y - HZ + 1), hw = road_hw(dy), fade = fm_clampf(dy / 30.0f, 0.12f, 1.0f);
        uint32_t a = (uint32_t)(lit * fade * 16.0f);
        int k;
        for (k = 1; k <= 16; k++) {
            float off = hw + (float)k * 0.34f * dy, fr;
            int32_t xi;
            if (off > 125.0f)
                break;
            xi = (int32_t)fm_floorf(120.0f + off);
            fr = 120.0f + off - (float)xi;
            px_add(xi, y, grid, (uint32_t)((float)a * (1.0f - fr)));
            px_add(xi + 1, y, grid, (uint32_t)((float)a * fr));
            xi = (int32_t)fm_floorf(120.0f - off);
            fr = 120.0f - off - (float)xi;
            px_add(xi, y, grid, (uint32_t)((float)a * (1.0f - fr)));
            px_add(xi + 1, y, grid, (uint32_t)((float)a * fr));
        }
    }
    /* the lines across */
    for (n = 0; n < 40; n++) {
        float dyf = GA / ((float)n + 1.0f - f), yf = (float)(HZ - 1) + dyf, fr, hw, fade;
        int32_t yi, xl, xr;
        uint32_t a, a0, a1;
        if (dyf > (float)(SCENE_H - HZ + 2))
            continue;
        if (dyf < 1.0f)
            break;
        yi = (int32_t)fm_floorf(yf);
        fr = yf - (float)yi;
        hw = road_hw(dyf);
        fade = fm_clampf(dyf / 26.0f, 0.1f, 1.0f);
        a = (uint32_t)(lit * fade * 18.0f);
        a0 = (uint32_t)((float)a * (1.0f - fr));
        a1 = (uint32_t)((float)a * fr);
        xl = (int32_t)(120.0f - hw);
        xr = (int32_t)(120.0f + hw);
        for (x = 0; x < 240; x++) {
            if (x > xl && x < xr)
                continue;
            px_add(x, yi, grid, a0);
            px_add(x, yi + 1, grid, a1);
            if (dyf > 30.0f)                                /* near: thicker */
                px_add(x, yi + 2, grid, a1 / 2 + a0 / 3);
        }
    }
    /* the road's edges and its dashes */
    for (y = y0; y < y1; y++) {
        float dy = (float)(y - HZ + 1), hw = road_hw(dy), z = GA / dy, u, w = 0.8f + dy * 0.03f, fr;
        int32_t xi;
        uint32_t a = (uint32_t)(fm_clampf(dy / 14.0f, 0.3f, 1.0f) * 16.0f);
        xi = (int32_t)fm_floorf(120.0f - hw);
        fr = 120.0f - hw - (float)xi;
        px_blend(xi, y, edge, (uint32_t)((float)a * (1.0f - fr)));
        px_blend(xi + 1, y, edge, a);
        if (w > 1.6f)
            px_blend(xi + 2, y, edge, (uint32_t)((float)a * fr));
        xi = (int32_t)fm_floorf(120.0f + hw);
        fr = 120.0f + hw - (float)xi;
        px_blend(xi - 1, y, edge, a);
        px_blend(xi, y, edge, (uint32_t)((float)a * (1.0f - fr) + 4.0f));
        if (w > 1.6f)
            px_blend(xi + 1, y, edge, (uint32_t)((float)a * fr));
        u = (z + en_beats) * 1.0f;
        if (u - fm_floorf(u) < 0.45f) {
            float half = w * 0.5f;
            int32_t x0 = (int32_t)fm_floorf(120.0f - half), x1 = (int32_t)fm_floorf(120.0f + half);
            for (xi = x0; xi <= x1; xi++)
                px_blend(xi, y, dash, a * 3 / 4);
        }
    }
}

/* the car, from behind: low and wide, its tail lights flaring with the snare; swaying in its lane */
static void draw_car(void)
{
    float sway = fm_sinf((float)ui.frame * 0.013f) * 5.0f + fm_sinf((float)ui.frame * 0.031f) * 1.5f;
    int32_t cx = 120 + (int32_t)(sway + (sway < 0.0f ? -0.5f : 0.5f)), y, i;
    uint16_t hi = pc16(PC_ACCENT), glass = mix(pc16(PC_SKY_HZ), RGB(0, 0, 0), 10), dark = RGB(8, 4, 14);
    uint16_t tl = RGB(255, 30, 64), tl_core = RGB(255, 170, 180);
    int flare = q16(en_snare * 0.8f + en_kick * 0.25f);
    if (by1 <= 138)
        return;
    /* the glow of the tail lights on the road, and the shadow */
    for (y = 165; y < SCENE_H; y++)
        for (i = -44; i <= 44; i++) {
            int32_t d = (i < 0 ? -i : i) + (y - 165) * 4;
            if (d < 44)
                px_add(cx + i, y, tl, (uint32_t)((3 + flare / 3) * (44 - d) / 44));
        }
    for (i = -38; i <= 38; i++)
        px_blend(cx + i, 170, RGB(0, 0, 0), 10);
    /* the tyres */
    cv_round(cx - 36, 159, 12, 12, 2, dark);
    cv_round(cx + 25, 159, 12, 12, 2, dark);
    /* the roof and the rear window: a trapezoid, outlined */
    for (y = 139; y < 148; y++) {
        int32_t w = 15 + (y - 139) * 7 / 8;
        cv_rect(cx - w, y, 2 * w + 1, 1, RGB(40, 24, 66));
        if (y > 140)
            cv_rect(cx - w + 2, y, 2 * w - 3, 1, mix(glass, pc16(PC_SUN_BOT), (148 - y) / 3));
        px_blend(cx - w, y, hi, 10);
        px_blend(cx + w, y, hi, 10);
    }
    for (i = -15; i <= 15; i++)
        px_blend(cx + i, 139, hi, 12);
    /* the body: wide and low, lighter at the top */
    for (y = 148; y < 163; y++) {
        int32_t w = y < 150 ? 34 + (y - 148) : 36;
        cv_rect(cx - w, y, 2 * w + 1, 1, mix(RGB(58, 34, 92), RGB(18, 10, 32), (y - 148) * 16 / 14));
    }
    for (i = -34; i <= 34; i++)
        px_blend(cx + i, 148, hi, 13);
    px_blend(cx - 36, 150, hi, 8);
    px_blend(cx + 36, 150, hi, 8);
    /* the tail lights: a grille of red slats right across */
    cv_rect(cx - 33, 151, 67, 7, dark);
    for (y = 152; y <= 156; y += 2) {
        cv_rect(cx - 32, y, 65, 1, tl);
        for (i = -32; i <= 32; i++)
            px_blend(cx + i, y, tl_core, (uint32_t)(2 + flare / 2));
    }
    for (i = -33; i <= 33; i++) {
        px_add(cx + i, 150, tl, (uint32_t)(3 + flare / 3));
        px_add(cx + i, 158, tl, (uint32_t)(2 + flare / 3));
    }
    /* the plate, the exhausts */
    cv_rect(cx - 7, 159, 15, 4, RGB(206, 200, 226));
    cv_rect(cx - 26, 162, 4, 2, RGB(90, 84, 110));
    cv_rect(cx + 23, 162, 4, 2, RGB(90, 84, 110));
}

/* ---------------------------------------------------------- the header --- */
#ifdef PLAT_HOST
static int8_t batt_host = -2;                  /* -2: plat_battery's */
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
    cv_rect(x + 1, y + 1, 16, 8, RGB(10, 4, 22));
    cv_rect(x + 18, y + 3, 2, 4, c);
    if (b == 4) {
        for (i = 0; i < 7; i++)
            cv_rect(x + BOLT[i][0], y + 1 + i, BOLT[i][1], 1, K_TEXT);
        return;
    }
    for (i = 0; i < b && i < 3; i++)
        cv_rect(x + 2 + i * 5, y + 2, 4, 6, b <= 1 ? K_LOW : pc16(PC_ACCENT));
}
static const char *batt_words(int b)
{
    static const char *const W[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                     "On USB power"};
    return b >= 0 && b <= 4 ? W[b] : 0;
}

static void odo_text(char *t)                    /* "1234.5 mi" */
{
    char *e;
    itoa_u(proj.odo / 10u, t);
    e = t;
    while (*e)
        e++;
    *e++ = '.';
    *e++ = (char)('0' + proj.odo % 10u);
    cat_s(e, " mi");
}

static void draw_header(void)
{
    char t[24];
    uint16_t acc = pc16(PC_ACCENT);
    if (by0 > 40)
        return;
    if (plat_ms() < ui.msg_until) {
        int32_t x = cv_text(8, 3, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 7, 3, &FONT_B, ui.msg[1], acc);
    } else {
        int32_t x = 232;
        if (batt() >= 0) {
            x -= BATT_W;
            draw_batt(x, 6, batt());
            x -= 7;
        }
        if (ui.dirty) {                                    /* unsaved */
            cv_round(x - 5, 9, 5, 5, 2, K_DIM);
            x -= 10;
        }
        x -= text_w(&FONT_T, "BPM");
        cv_text(x, 7, &FONT_T, "BPM", K_DIM);
        itoa_u((uint32_t)knob_get(P_TEMPO), t);
        x -= text_w(&FONT_S, t) + 3;
        cv_text(x, 3, &FONT_S, t, en_playing ? K_TEXT : K_DIM);
        x -= 14;
        if (en_playing) {                                  /* the beat */
            float b = en_beats - fm_floorf(en_beats);
            cv_round(x, 6, 9, 9, 4, b < 0.25f ? acc : mix(acc, RGB(20, 10, 40), 11));
        } else {                                           /* parked: two bars */
            cv_rect(x + 1, 6, 3, 10, K_DIM);
            cv_rect(x + 6, 6, 3, 10, K_DIM);
        }
        cv_text(8, 3, &FONT_B, en_preset_name(proj.preset), acc);
    }
    /* the second line: the key and the chord; the section and how far through it (Setup: the odometer) */
    {
        char k[16], c[8];
        int32_t x;
        en_param_text(P_ROOT, en_key, k);
        x = cv_text(8, 21, &FONT_XS, k, K_DIM);
        x = cv_text(x + 6, 21, &FONT_XS, "/", mix(K_DIM, RGB(0, 0, 0), 8));
        cv_text(x + 6, 21, &FONT_XS, en_chord_name(en_chord_root, en_chord_minor, en_key, c), K_TEXT);
        if (ui.view == V_SETUP) {
            odo_text(t);
            cv_text(232 - text_w(&FONT_XS, t), 21, &FONT_XS, t, K_DIM);
        } else {
            int32_t w = 44, f = en_sec_len ? (int32_t)((en_sec_bar + 1) * w / en_sec_len) : 0;
            caps(t, EN_SEC_NAME[en_section % EN_NSEC], sizeof t);
            cv_text(232 - w - 6 - text_w(&FONT_XS, t), 21, &FONT_XS, t, acc);
            cv_round(232 - w, 27, w, 4, 2, RGB(40, 24, 70));
            if (f > 0)
                cv_round(232 - w, 27, f < 4 ? 4 : f, 4, 2, acc);
        }
    }
}

/* a button's card (help_tick): its name, then what it does, over the scene */
static void draw_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    uint16_t acc = pc16(PC_ACCENT);
    cv_round(16, 24, 208, 124, 6, K_PANEL);
    cv_round(16, 24, 208, 26, 6, acc);
    cv_rect(16, 44, 208, 6, acc);
    text_c(120, 28, &FONT_B, BTN_LABEL[ui.help - 1], K_BG);
    for (k = 0; k < n; k++)
        text_c(120, 64 + k * 20, &FONT_S, l[k] + (l[k][0] == ' '), K_TEXT);
}

static void draw_band(int32_t y0, int32_t h)
{
    band_begin(y0, h);
    if (ui.help) {
        cv_rect(0, y0, 240, h, RGB(8, 4, 18));
        draw_help();
    } else {
        draw_sky();
        if (by0 < HZ) {
            uint16_t m = pc16(PC_MOUNT), sh = pc16(PC_SKY_HZ), g = pc16(PC_GRID);
            draw_stars();
            draw_ridge(mt_far, mix(m, sh, 6), mix(g, sh, 8), 9);
            draw_ridge(mt_near, m, g, 14);
        }
        draw_ground();
        draw_horizon();
        draw_car();
        draw_header();
    }
    cv_blit(0, (uint32_t)y0);
}

/* ---------------------------------------------------------- the knobs --- */
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
/* a knob: a 270-degree track, anti-aliased, round ends; the value filled from the start and a round handle (AMB-1's) */
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

static void draw_knobs(void)
{
    uint32_t h = hash(hash(2166136261u, ui.view | (uint32_t)ui.touched << 8), proj.preset), i;
    int now_touch = plat_ms() < ui.touch_until;
    uint16_t acc = pc16(PC_ACCENT);
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(VIEW_KNOB[ui.view][i]));
    h = hash(h, (uint32_t)now_touch);
    if (h == ui.sig)
        return;
    ui.sig = h;
    cv_begin(240, KNB_H, K_BG);
    {   /* the rule; at its right end, the page, then its dots */
        char t[12];
        int k;
        int32_t xd = 237 - NVIEWS * 6, xt;
        caps(t, VIEW_NAME[ui.view], sizeof t);
        xt = xd - 5 - text_w(&FONT_XS, t);
        cv_rect(4, 5, xt - 9, 1, mix(K_LINE, acc, 5));
        cv_text(xt, -1, &FONT_XS, t, acc);
        for (k = 0; k < NVIEWS; k++)
            cv_round(xd + k * 6, 3, 4, 4, 2, k == ui.view ? acc : K_LINE);
    }
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v, hot = now_touch && ui.touched == (int)i;
        int32_t x = (int32_t)i * 60, cx = x + 30;
        char t[16];
        uint16_t bg = hot ? K_PANEL : K_BG, fill = acc;
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
            int n = hi - lo + 1, j, pw = 9, gap = 3, w0 = n * (pw + gap) - gap;
            for (j = 0; j < n; j++)
                cv_round(cx - w0 / 2 + j * (pw + gap), 33, pw, 8, 2, j == v - lo ? fill : K_LINE);
        } else {
            arc(cx, 37, 12, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, K_LINE, fill, bg);
        }
        knob_text(k, t);
        text_c(cx, 50, &FONT_B, t, hot ? acc : K_TEXT);
    }
    cv_blit(0, KNB_Y);
}

static void leds(void)
{
    uint32_t b = 0, k = 0, i, chord = 0;
    int r = en_chord_root;
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    chord = 1u << r | 1u << ((r + (en_chord_minor ? 3 : 4)) % 12) | 1u << ((r + 7) % 12);
    if (proj.leds)
        for (i = 0; i < NKEYS; i++)
            if (chord >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    plat_glow(proj.leds == 1);
    plat_leds(b, k);
}

/* ------------------------------------------------------------ the odometer --- */
static void odo_tick(void)                       /* miles at the speed's mph (0.6 a bpm), while driving */
{
    uint32_t now = plat_ms(), dt = now - ui.prev_ms;
    ui.prev_ms = now;
    if (!en_playing || dt > 1000u)
        return;
    ui.odo_ms += dt * (uint32_t)(knob_get(P_TEMPO) * 6 / 10);
    while (ui.odo_ms >= 360000u) {               /* a tenth of a mile: 360 000 ms at 1 mph */
        ui.odo_ms -= 360000u;
        proj.odo++;
    }
}

/* AUTOSAVE, and the flash's wear: every save erases one of the same two 4 KiB sectors, so NIGHT DRIVE saves itself
 * rarely. keep_due is the whole policy:
 *   a setting (a knob, the key, the chords, the preset): 5 s after the last change, and at most once every 5 minutes;
 *   the odometer: at most once an hour (up to an hour of miles is lost at a power-off: the FM-1 gives no warning);
 *   SAVE saves at once.
 * A flash erase can stall the sound for a moment: a due save waits for a quiet moment in the music (parked, or a
 * section without drums) for up to three minutes */
#define SAVE_SETTINGS_MS 5000u
#define KEEP_SETTINGS_MS (5u * 60000u)
#define KEEP_ODO_MS (60u * 60000u)
static int keep_due(uint32_t now)
{
    uint32_t since = now - ui.saved_t;
    if (ui.dirty && now - ui.dirty_t >= SAVE_SETTINGS_MS && since >= KEEP_SETTINGS_MS)
        return 1;
    return proj.odo != ui.odo_saved && since >= KEEP_ODO_MS;
}
static void autosave(void)
{
    uint32_t now = plat_ms();
    int quiet;
    if (!keep_due(now) || ui.btn || ui.keys) {
        if (!keep_due(now))
            ui.due_t = 0;
        return;
    }
    if (!ui.due_t)
        ui.due_t = now ? now : 1u;
    quiet = !en_playing || en_energy < 0.3f || now - ui.due_t >= 180000u;
    if (!quiet)
        return;
    if (project_save() == 0) {
        ui.dirty = 0;
        ui.odo_saved = proj.odo;
    }
    ui.saved_t = now;                            /* (a failed save waits its turn too: no hammering the flash) */
    ui.due_t = 0;
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader on the computer (announce.c; fm1-reader): a page once it settles, with its
 * knobs; a knob's value as it turns; a help card as it comes up; a new section and a key change (queued); what a
 * button did (say). WHERE is the whole screen in two: the page and its knobs with the header, then the drive (the
 * section, the key, the chord). Nothing is built while no reader listens. */
static struct {
    uint8_t view, help, started, section, key;
    int16_t val[4];
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
static void sp_knob(int view, int i)            /* "Bright 55": the name the page gives it, then its value */
{
    char t[16];
    sp_cat(KNOB_LABEL[view][i]);
    sp_cat(" ");
    knob_text(VIEW_KNOB[view][i], t);
    if (VIEW_KNOB[view][i] == K_USB)
        sp_cat(knob_get(K_USB) ? "full level" : "follows master");
    else
        sp_cat(t);
}
static void sp_key(int k)                       /* "F sharp minor" */
{
    sp_cat(note_say(k, k));
    sp_cat(" minor");
}
static void sp_chord(void)
{
    sp_cat(note_say(en_chord_root, en_key));
    sp_cat(en_chord_minor ? " minor" : " major");
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
    sp_cat(en_preset_name(proj.preset));
    if (ui.view != V_DRIVE) {
        char t[16];
        sp_cat(", speed ");
        knob_text(P_TEMPO, t);
        sp_cat(t);
    }
    sp_cat(sp_tp.playing ? ", driving" : ", parked");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (ui.view == V_SETUP) {
        char t[16];
        sp_cat(". Odometer ");
        odo_text(t);
        sp_cat(t);
    }
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static void sp_drive(void)                     /* "Drive, bar 3 of 16. Key F minor, chord D flat major" */
{
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat(EN_SEC_NAME[en_section % EN_NSEC]);
    sp_cat(", bar ");
    sp_int(en_sec_bar + 1u);
    sp_cat(" of ");
    sp_int(en_sec_len);
    sp_cat(". Key ");
    sp_key(en_key);
    sp_cat(", chord ");
    sp_chord();
    sp_cat(". Chords ");
    sp_cat(en_prog_name(en_prog));
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
static void sp_preset_text(void)               /* "PRESET Midnight, F minor, speed 100 bpm" */
{
    char t[16];
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("PRESET ");
    sp_cat(en_preset_name(proj.preset));
    sp_cat(", ");
    sp_key(proj.slot[proj.preset].key);
    sp_cat(", speed ");
    knob_text(P_TEMPO, t);
    sp_cat(t);
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
    sp.section = en_section;
    sp.key = en_key;
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
            sp_drive();
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
        ann_say(en_playing ? "DRIVING" : "PARKED", 0, 0);
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
    if (en_section != sp.section) {             /* a new section: queued */
        sp.section = en_section;
        sp_n = 0;
        sp_cat(EN_SEC_NAME[en_section % EN_NSEC]);
        ann_say(sp_buf, 0, 0);
    }
    if (en_key != sp.key) {                     /* a key change: queued */
        sp.key = en_key;
        sp_n = 0;
        sp_cat("Key ");
        sp_key(en_key);
        ann_say(sp_buf, 0, 0);
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card and message as a screen reader gets them (the "speakdump" command) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"},
                                         {"NEXT SECTION", "ON THE BAR"}, {"KEY CHANGE", "ON THE BAR"},
                                         {"PARKED", 0}, {"DRIVING", 0}, {"SAVED", 0}};
    project_t keep = proj;
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
                if (k <= P_DRIFT)
                    proj.slot[proj.preset].knob[k - P_TEMPO] = (int16_t)x;
                else if (k == P_DRUMS)
                    proj.drums = (int16_t)x;
                else if (k == P_LEAD)
                    proj.lead = (int16_t)x;
                else if (k == K_LEDS)
                    proj.leds = (uint8_t)x;
                else
                    proj.usb_full = (uint8_t)x;
                sp_n = 0;
                sp_knob(v, i);
                sp_print("KNOB", sp_buf);
            }
            proj = keep;
        }
    }
    for (x = 0; x < 4; x++) {
        ui.view = (uint8_t)(x & 1);
        sp_tp.playing = (uint8_t)(x >> 1);
        ui.dirty = (uint8_t)(x == 1);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_drive();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
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
    msg_text("NOT SAVED", "FLASH ERROR", t);
    sp_print("WARN", t);
    for (x = 0; x < 12; x++) {
        sp_n = 0;
        sp_cat("KEY ");
        sp_key(x);
        sp_print("MSG", sp_buf);
    }
    for (x = 0; x < EN_NPROG; x++) {
        msg_text("CHORDS", en_prog_name(x), t);
        sp_print("MSG", t);
    }
    for (x = 0; x < NPRESETS; x++) {
        proj.preset = (uint8_t)x;
        sp_preset_text();
        sp_print("MSG", sp_buf);
        msg_text("RESET", en_preset_name(x), t);
        sp_print("MSG", t);
    }
    proj = keep;
    for (x = 0; x < EN_NSEC; x++)
        sp_print("MSG", EN_SEC_NAME[x]);
    for (x = 0; x <= 4; x++)
        sp_print("WHERE", batt_words(x));
}
#endif

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.prev_ms = plat_ms();
    ui.odo_saved = proj.odo;
    mountains_make();
    lcd_fill(0, 0, 240, 240, K_BG);
    if (!en_playing)
        en_play(1);                              /* it drives from the start */
    sp_tp.playing = 1;
    sp_tp.play_t = plat_ms();
}

void ui_frame(void)
{
    input();
    odo_tick();
    autosave();
    if ((ui.frame & 1u) == 0u) {                 /* the scene at 30 frames a second */
        draw_band(0, BAND1);
        draw_band(BAND1, SCENE_H - BAND1);
    }
    draw_knobs();
    leds();
    speak();
    ann_tick(plat_ms());
    ui.frame++;
}

void ui_input_only(void) { input(); }
