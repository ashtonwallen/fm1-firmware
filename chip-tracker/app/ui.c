/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CHIP TRACKER: ashtonwallen, 2026 */
/* CHIP TRACKER's UI: the panel and the screen (from AMB-1's: its knobs, help cards, messages and screen reader).
 * The screen is the tracker: the song's title and where it is, four oscilloscopes (one a channel), the pattern's
 * rows scrolling under a cursor bar, and the knob strip. Part of the unity build (after gfx.c and project.c); the
 * host simulator includes it too.
 *
 *   PLAY           play / stop                       EDIT   the next song
 *   REC            REPEAT: the song loops on         OCT- / OCT+   the keys' octave
 *   PRESETS        the style (each kept as left)     ALGORITHM     Tone
 *   HOME ENV GLO   the pages: Play, Mix, Song (SELECT steps through them)
 *   KNOB 1-4       the page's four values
 *   keys           play along on a pulse
 *   SAVE           saves (it also saves by itself at a quiet moment: see autosave); held 1.5 s: the style back to
 *                  its recipe */
#include "fastmath.h"
#include "pixfont.h"

enum { V_PLAY, V_MIX, V_SONG, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Play", "Mix", "Song"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_GLO};
enum { K_LEDS = 100, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_TEMPO, P_ENERGY, P_ECHO, P_MOOD},
    {P_PU1, P_PU2, P_TRI, P_NOISE},
    {P_SWING, P_TONE, P_LOOPS, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Tempo", "Energy", "Echo", "Mood"},
    {"Pulse 1", "Pulse 2", "Triangle", "Noise"},
    {"Swing", "Tone", "Loops", "Lights"},
};
static const char *const KNOB_TAG[NVIEWS][4] = {          /* as the strip shows them */
    {"TEMPO", "ENERGY", "ECHO", "MOOD"},
    {"PU1", "PU2", "TRI", "NOISE"},
    {"SWING", "TONE", "LOOPS", "LIGHTS"},
};

/* the screen's colours: a tracker at night. Deep navy rows, a beat every four lighter, a bar every sixteen; the
 * cursor bar blue; notes cream, instruments green, effects pink and cyan; each channel its own colour */
#define K_BG RGB(12, 14, 28)
#define K_BEAT RGB(22, 26, 48)
#define K_BAR RGB(32, 37, 68)
#define K_CUR RGB(46, 66, 168)
#define K_LINE RGB(44, 50, 88)
#define K_DIM RGB(88, 96, 140)
#define K_EMPTY RGB(52, 58, 96)
#define K_TEXT RGB(236, 238, 252)
#define K_NOTE RGB(232, 228, 210)
#define K_INS RGB(112, 214, 132)
#define K_FX RGB(250, 128, 176)
#define K_FXP RGB(128, 196, 255)
#define K_ACC RGB(255, 196, 72)
#define K_LOW RGB(236, 88, 72)
static const uint16_t CH_COL[EN_NCH] = {RGB(255, 196, 72), RGB(96, 214, 255), RGB(255, 112, 128), RGB(196, 200, 228)};
static const char *const CH_TAG[EN_NCH] = {"PU1", "PU2", "TRI", "NOI"};
static const char *const SECT_NAME[] = {"INTRO", "VERSE", "CHORUS", "BRIDGE", "ENDING"};
static const uint16_t SECT_COL[] = {RGB(150, 160, 210), RGB(112, 214, 132), RGB(255, 196, 72), RGB(200, 140, 255),
                                    RGB(250, 128, 176)};

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
    uint32_t saved_song;               /* the song the last save kept */
    char msg[2][28];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[4];
    uint32_t keys_on;                  /* the keys sounding (played along), key bit */
    float vu;
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
static char *cat_hex2(char *b, uint32_t v)
{
    static const char H[] = "0123456789ABCDEF";
    b[0] = H[(v >> 4) & 15u];
    b[1] = H[v & 15u];
    b[2] = 0;
    return b + 2;
}
static void caps(char *d, const char *s, int n)
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}
static void title_case(char *d, const char *s, int n)   /* "CRYSTAL CAVERN" -> "Crystal Cavern" (for the reader) */
{
    int i, first = 1;
    for (i = 0; i < n - 1 && s[i]; i++) {
        char c = s[i];
        d[i] = (char)(!first && c >= 'A' && c <= 'Z' ? c + 32 : c);
        first = c == ' ' || c == '-';
    }
    d[i] = 0;
}

static void mark_dirty(void)
{
    ui.dirty = 1;
    ui.act_t = plat_ms();
}
int ui_dirty(void) { return ui.dirty != 0; }

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
        if (k == P_MOOD)
            en_retune();                         /* once it rests: the song's jingle, a new song in the new mood */
    } else if (k == K_LEDS) {
        proj.leds = (uint8_t)v;
    }
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
    en_skip();                                   /* on to a new song in the new style */
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
            en_note(65 + k + (proj.oct - 2) * 12, 0);
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
        say("NEXT SONG", 0);
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
            say("PLAYING", 0);
        }
        break;
    case B_REC:
        say("REPEAT", en_repeat_on ? "OFF" : "ON");
        en_repeat(!en_repeat_on);
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
            ui.saved_song = en_song_no;
            say("SAVED", 0);
        } else {
            warn("NOT SAVED", "FLASH ERROR");
        }
        break;
    default: break;
    }
}

/* MIDI in (USB and the TRS jack): notes play along on the pulse */
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
 * another button puts it away. PLAY acts on its press; SEL, and the buttons with no job, have no card */
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
            int note = 65 + (int)i + (proj.oct - 2) * 12;   /* key 0 is F4: the lead's register */
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
        if (v >= 0 && v < NVIEWS) {
            char t[8];
            set_view(v);
            caps(t, VIEW_NAME[v], sizeof t);
            say_as("PAGE", t, 0);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {          /* ALGORITHM: Tone, from any page */
        char t[12];
        turn(EN_ALGO, P_TONE, e);
        en_param_text(P_TONE, proj.par[P_TONE], t);
        if (ui.view == V_SONG) {
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
/* four bands: the header (42 px: the style and tempo, the song's title, the section and the pattern), the scopes
 * (32), the pattern's rows (130: thirteen of ten pixels, the cursor in the middle), the knob strip (36) */
#define HDR_H 42
#define SCO_Y 42
#define SCO_H 32
#define TRK_Y 74
#define TRK_ROWS 13
#define TRK_H (TRK_ROWS * 10)
#define KNB_Y (TRK_Y + TRK_H)
#define KNB_H (240 - KNB_Y)
#define COL_X(k) (14 + (k) * 56)

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static uint16_t mix(uint16_t a, uint16_t b, int t)   /* t 0..16: a -> b */
{
    uint32_t r, g, bl, u = (uint32_t)(t < 0 ? 0 : t > 16 ? 16 : t);
    r = ((uint32_t)(a >> 11) * (16u - u) + (uint32_t)(b >> 11) * u) >> 4;
    g = ((uint32_t)((a >> 5) & 63u) * (16u - u) + (uint32_t)((b >> 5) & 63u) * u) >> 4;
    bl = ((uint32_t)(a & 31u) * (16u - u) + (uint32_t)(b & 31u) * u) >> 4;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* the pixel fonts (pixfont.h: Terminus 12 and 16 bold), drawn straight into the canvas */
static int32_t pf_text(int32_t x, int32_t y, const char *s, uint16_t c, int big)
{
    uint16_t sc = swap16(c);
    int h = big ? PF16_H : PF12_H, w = big ? PF16_W : PF12_W;
    for (; *s; s++, x += w) {
        int ch = (unsigned char)*s, r;
        const uint8_t *g;
        if (ch < 32 || ch > 126)
            ch = '?';
        if (ch == ' ')
            continue;
        g = big ? PF16[ch - 32] : PF12[ch - 32];
        for (r = 0; r < h; r++) {
            uint32_t bits = g[r], b;
            int32_t yy = y + r + cv_oy;
            uint16_t *row;
            if (!bits || yy < cv_y0 || yy >= cv_y1)
                continue;
            row = &cv_px[(uint32_t)yy * cv_w];
            for (b = 0; b < 8u; b++)
                if (bits & (0x80u >> b) && (uint32_t)(x + (int32_t)b) < cv_w)
                    row[x + (int32_t)b] = sc;
        }
    }
    return x;
}
static int32_t pf_w(const char *s, int big)
{
    int32_t n = 0;
    while (s[n])
        n++;
    return n * (big ? PF16_W : PF12_W);
}

/* the battery (plat_battery): 0 (low) .. 3 bars, 4 on USB power, -1 not read yet */
#ifdef PLAT_HOST
static int8_t batt_host = -2;
static int batt(void) { return batt_host > -2 ? batt_host : plat_battery(); }
#else
#define batt() plat_battery()
#endif
static void draw_batt(int32_t x, int32_t y, int b)
{
    static const uint8_t BOLT[7] = {0x10, 0x20, 0x60, 0xF8, 0x30, 0x20, 0x40};
    uint16_t c = b <= 1 ? K_LOW : K_DIM;
    int i, j;
    cv_rect(x, y, 16, 1, c);
    cv_rect(x, y + 8, 16, 1, c);
    cv_rect(x, y, 1, 9, c);
    cv_rect(x + 15, y, 1, 9, c);
    cv_rect(x + 16, y + 3, 2, 3, c);
    if (b == 4) {
        for (i = 0; i < 7; i++)
            for (j = 0; j < 8; j++)
                if (BOLT[i] & (0x80 >> j))
                    cv_rect(x + 4 + j, y + 1 + i, 1, 1, K_ACC);
        return;
    }
    for (i = 0; i < b && i < 3; i++)
        cv_rect(x + 2 + i * 4, y + 2, 3, 5, b <= 1 ? K_LOW : K_INS);
}
static const char *batt_words(int b)
{
    static const char *const W[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                     "On USB power"};
    return b >= 0 && b <= 4 ? W[b] : 0;
}

static int sect_index(uint8_t c) { return c == 'I' ? 0 : c == 'A' ? 1 : c == 'B' ? 2 : c == 'C' ? 3 : 4; }

/* the header: the style, the tempo with a beat light, the battery; the song's title; the section, the pattern,
 * the order, the loop (or a message) */
static void draw_header(void)
{
    const en_song_t *s = en_cur;
    char t[40], *e;
    int msg = plat_ms() < ui.msg_until, beat = (en_kick & 1u), ord = en_ord, sec = sect_index(s->osect[ord]);
    uint32_t h = hash(hash(hash(hash(2166136261u, proj.style | (uint32_t)en_bpm << 8 | (uint32_t)en_playing << 16 |
                                                       (uint32_t)en_repeat_on << 17 | (uint32_t)ui.dirty << 18 |
                                                       (uint32_t)msg << 19 | (uint32_t)beat << 20 |
                                                       (uint32_t)en_phase << 21),
                                (uint32_t)(batt() + 1) | (uint32_t)ord << 8 | (uint32_t)en_loop << 16),
                           msg ? ui.msg_until : 0u),
                      (uint32_t)(uintptr_t)s ^ en_song_no);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    {                                            /* line one: the style; the tempo, the beat, the battery */
        int32_t x = 238;
        caps(t, en_style_name(proj.style), sizeof t);
        pf_text(2, 0, t, K_ACC, 0);
        if (batt() >= 0) {
            x -= 18;
            draw_batt(x, 2, batt());
            x -= 6;
        }
        if (ui.dirty) {
            cv_rect(x - 3, 5, 3, 3, K_DIM);
            x -= 8;
        }
        e = cat_u(t, en_bpm);
        cat_s(e, " BPM");
        x -= pf_w(t, 0);
        pf_text(x, 0, t, en_playing ? K_TEXT : K_DIM, 0);
        x -= 10;
        if (en_playing) {                        /* a beat light, lit on the kicks */
            cv_rect(x, 3, 6, 6, beat ? K_ACC : K_LINE);
        } else {                                 /* stopped: a square */
            cv_rect(x, 3, 6, 6, K_DIM);
        }
    }
    pf_text(2, 12, s->title, K_TEXT, 1);         /* line two: the title */
    cv_rect(0, 29, 240, 1, K_LINE);
    if (msg) {                                   /* line three: a message, or where the song is */
        int32_t x = pf_text(2, 30, ui.msg[0], K_TEXT, 0);
        pf_text(x + 6, 30, ui.msg[1], K_ACC, 0);
    } else {
        int32_t x, w;
        const char *sn = en_phase == PH_GAP ? "NEXT" : SECT_NAME[sec];
        uint16_t sc = en_phase == PH_GAP ? K_DIM : SECT_COL[sec];
        w = pf_w(sn, 0) + 6;
        cv_rect(1, 31, w, 10, sc);
        pf_text(4, 30, sn, K_BG, 0);
        x = 1 + w + 6;
        x = pf_text(x, 30, "PAT", K_DIM, 0);
        cat_hex2(t, s->order[ord]);
        x = pf_text(x + 4, 30, t, K_TEXT, 0);
        x = pf_text(x + 8, 30, "ORD", K_DIM, 0);
        e = cat_hex2(t, (uint32_t)ord);
        *e++ = '/';
        cat_hex2(e, (uint32_t)(s->nord - 1));
        pf_text(x + 4, 30, t, K_TEXT, 0);
        if (en_repeat_on) {
            cv_rect(240 - 2 - 42, 31, 42, 10, K_FX);
            pf_text(240 - 2 - 42 + 3, 30, "REPEAT", K_BG, 0);
        } else {
            e = cat_s(t, "LOOP ");
            e = cat_u(e, (uint32_t)(en_loop + 1 > en_loops ? en_loops : en_loop + 1));
            *e++ = '/';
            cat_u(e, en_loops);
            pf_text(238 - pf_w(t, 0), 30, t, K_DIM, 0);
        }
    }
    cv_blit(0, 0);
}

/* the oscilloscopes: each channel's last samples, from a rising zero crossing, about two and a half cycles */
static void draw_scopes(void)
{
    int k;
    uint32_t w = en_scope_w;
    cv_begin(240, SCO_H, K_BG);
    {                                            /* a level meter, left of the scopes */
        int lvl = (int)(ui.vu * 28.0f + 0.5f), y;
        for (y = 0; y < 14; y++)
            cv_rect(4, 29 - y * 2, 6, 1, y * 2 < lvl ? (y > 10 ? K_LOW : y > 7 ? K_ACC : K_INS) : K_LINE);
    }
    for (k = 0; k < EN_NCH; k++) {
        int32_t x0 = COL_X(k) + 1, x;
        uint16_t c = CH_COL[k], dim = mix(c, K_BG, 11);
        const int8_t *sc = en_scope[k];
        float hz = en_ch_hz[k];
        uint32_t win, start, i, mask = EN_SCOPE - 1u;
        int prev = 0, on = en_ch_vol[k] > 0 || (k == CH_TRI && en_ch_note[k]);
        cv_rect(x0, 0, 54, 1, dim);              /* its frame */
        cv_rect(x0, 31, 54, 1, dim);
        cv_rect(x0, 0, 1, 32, dim);
        cv_rect(x0 + 53, 0, 1, 32, dim);
        cv_rect(x0 + 1, 21, 52, 1, mix(c, K_BG, 14));
        pf_text(x0 + 2, 0, CH_TAG[k], on ? c : dim, 0);
        if (k == CH_NOI || hz < 30.0f)
            win = 220u;
        else
            win = (uint32_t)(EN_SR * 2.5f / hz);
        win = win < 104u ? 104u : win > 1600u ? 1600u : win;
        start = w - win - 4u;
        if (k != CH_NOI)                         /* the trigger: a rising crossing, up to a cycle back */
            for (i = 0; i < 1600u && i < win * 2u; i++) {
                uint32_t a = (w - win - 4u - i) & mask;
                if (sc[(a - 1u) & mask] < 0 && sc[a] >= 0) {
                    start = a;
                    break;
                }
            }
        for (x = 0; x < 52; x++) {
            int v = sc[(start + (uint32_t)x * win / 52u) & mask], y = 21 - v * 9 / 100, y0, y1;
            y = y < 12 ? 12 : y > 30 ? 30 : y;
            if (!x)
                prev = y;
            y0 = prev < y ? prev : y;
            y1 = prev < y ? y : prev;
            cv_rect(x0 + 1 + x, y0, 1, y1 - y0 + 1, c);
            prev = y;
        }
    }
    cv_blit(0, SCO_Y);
}

/* the rows: thirteen, the one playing in the middle under the cursor; the patterns before and after it dimmed */
static void note_text(int ch, uint8_t n, char *t)
{
    static const char *const NM[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    if (!n) {
        cat_s(t, "...");
    } else if (n == NOTE_REL) {
        cat_s(t, "===");
    } else if (n == NOTE_CUT) {
        cat_s(t, "---");
    } else if (ch == CH_NOI) {
        t[0] = "0123456789ABCDEF"[(n - 1) & 15];
        t[1] = '-';
        t[2] = '#';
        t[3] = 0;
    } else {
        cat_s(t, NM[n % 12]);
        t[2] = (char)('0' + (n / 12 - 1));
        t[3] = 0;
    }
}
static void draw_row(int32_t y, const en_song_t *s, int pat, int row, int cur, int far, int lvl)
{
    char t[8];
    int k;
    uint16_t bg = cur ? K_CUR : (row & 15) == 0 ? K_BAR : (row & 3) == 0 ? K_BEAT : K_BG;
    int fade = far ? 9 : 0;
    if (bg != K_BG)
        cv_rect(0, y, 240, 10, far ? mix(bg, K_BG, 8) : bg);
    cat_hex2(t, (uint32_t)row);
    pf_text(1, y - 1, t, mix(cur ? K_TEXT : (row & 3) == 0 ? K_TEXT : K_DIM, K_BG, fade), 0);
    for (k = 0; k < EN_NCH; k++) {
        const en_cell_t *e = &s->pat[pat][row][k];
        int32_t x = COL_X(k) + 2;
        int muted = e->note && ((e->lay & 7) > lvl || ((e->lay & LAY_ECHO) && proj.par[P_ECHO] == 0));
        int f = muted ? (fade > 10 ? fade : 10) : fade;
        uint16_t cn = e->note ? (cur ? K_TEXT : K_NOTE) : (cur ? mix(K_CUR, K_TEXT, 6) : K_EMPTY);
        note_text(k, e->note, t);
        pf_text(x, y - 1, t, mix(cn, K_BG, f), 0);
        if (e->ins != 0xFF) {
            cat_hex2(t, e->ins);
            pf_text(x + 20, y - 1, t, mix(K_INS, K_BG, f), 0);
        } else {
            pf_text(x + 20, y - 1, "..", cur ? mix(K_CUR, K_TEXT, 6) : mix(K_EMPTY, K_BG, fade), 0);
        }
        if (e->fx) {
            t[0] = (char)e->fx;
            t[1] = 0;
            pf_text(x + 34, y - 1, t, mix(K_FX, K_BG, f), 0);
            cat_hex2(t, e->fxp);
            pf_text(x + 40, y - 1, t, mix(K_FXP, K_BG, f), 0);
        } else {
            pf_text(x + 34, y - 1, "...", cur ? mix(K_CUR, K_TEXT, 6) : mix(K_EMPTY, K_BG, fade), 0);
        }
    }
}
static void draw_tracker(void)
{
    const en_song_t *s = en_cur;
    int ord = en_ord, row = en_row, lvl = en_energy_level(proj.par[P_ENERGY]), i, k;
    int pat = s->order[ord], len = s->plen[pat];
    int prev_ord = ord > 0 ? ord - 1 : -1;
    int next_ord = en_next_ord(s, ord, en_loop, en_loops, en_repeat_on);
    uint32_t h = hash(hash(hash(2166136261u, (uint32_t)ord | (uint32_t)row << 8 | (uint32_t)lvl << 16 |
                                                 (uint32_t)(proj.par[P_ECHO] > 0) << 20 | (uint32_t)en_phase << 21),
                           (uint32_t)(uintptr_t)s ^ en_song_no),
                      (uint32_t)ui.help);
    if (en_phase == PH_GAP)
        row = len;                               /* between songs: the last row gone past */
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, TRK_H, K_BG);
    if (ui.help) {
        static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP",
                                                  "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
        const char *l[HELP_LINES];
        int n = help_lines(ui.help - 1, l);
        cv_rect(10, 12, 220, 104, K_LINE);
        cv_rect(11, 13, 218, 102, K_BEAT);
        cv_rect(11, 13, 218, 20, K_ACC);
        pf_text(120 - pf_w(BTN_LABEL[ui.help - 1], 1) / 2, 15, BTN_LABEL[ui.help - 1], K_BG, 1);
        for (k = 0; k < n; k++)
            pf_text(120 - pf_w(l[k] + (l[k][0] == ' '), 0) / 2, 42 + k * 16, l[k] + (l[k][0] == ' '), K_TEXT, 0);
        cv_blit(0, TRK_Y);
        return;
    }
    for (k = 1; k < EN_NCH; k++)                 /* the channels' rules */
        cv_rect(COL_X(k), 0, 1, TRK_H, K_LINE);
    cv_rect(COL_X(0) - 1, 0, 1, TRK_H, K_LINE);
    for (i = 0; i < TRK_ROWS; i++) {
        int r = row + i - TRK_ROWS / 2, o = ord;
        int32_t y = i * 10;
        if (r < 0) {                             /* the pattern before, dimmed */
            if (prev_ord < 0)
                continue;
            o = prev_ord;
            r += s->plen[s->order[o]];
            if (r < 0)
                continue;
            draw_row(y, s, s->order[o], r, 0, 1, lvl);
        } else if (r >= len) {                   /* the pattern after, dimmed */
            if (next_ord < 0 || en_phase == PH_GAP)
                continue;
            o = next_ord;
            r -= len;
            if (r >= s->plen[s->order[o]])
                continue;
            if (r == 0)
                cv_rect(0, y, 240, 1, K_DIM);
            draw_row(y, s, s->order[o], r, 0, 1, lvl);
        } else {
            draw_row(y, s, pat, r, i == TRK_ROWS / 2 && en_phase != PH_GAP, 0, lvl);
        }
    }
    for (k = 1; k < EN_NCH; k++)                 /* the rules over the cursor bar too */
        cv_rect(COL_X(k), TRK_ROWS / 2 * 10, 1, 10, mix(K_CUR, K_TEXT, 4));
    cv_blit(0, TRK_Y);
}

/* the knob strip: four cells, a knob each: its name, its value, a bar */
static void draw_knobs(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8), i;
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(VIEW_KNOB[ui.view][i]));
    h = hash(h, (uint32_t)now_touch);
    if (h == ui.sig[3])
        return;
    ui.sig[3] = h;
    cv_begin(240, KNB_H, K_BG);
    cv_rect(0, 0, 240, 1, K_LINE);
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v, hot = now_touch && ui.touched == (int)i, f;
        int32_t x = (int32_t)i * 60;
        char t[16];
        if (i)
            cv_rect(x, 4, 1, 28, K_LINE);
        if (hot)
            cv_rect(x + 1, 2, 58, 34, K_BEAT);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        pf_text(x + 5, 2, KNOB_TAG[ui.view][i], hot ? K_ACC : K_DIM, 0);
        knob_text(k, t);
        caps(t, t, sizeof t);
        pf_text(x + 5, 13, t, hot ? K_ACC : K_TEXT, 0);
        cv_rect(x + 5, 28, 50, 2, K_LINE);
        f = hi > lo ? (v - lo) * 50 / (hi - lo) : 0;
        if (f > 0)
            cv_rect(x + 5, 28, f, 2, hot ? K_ACC : CH_COL[1]);
        cv_rect(x + 5 + (f > 49 ? 49 : f), 26, 1, 6, hot ? K_ACC : K_TEXT);
    }
    for (i = 0; i < NVIEWS; i++)                 /* the page: three marks along the bottom */
        cv_rect(104 + (int32_t)i * 12, 34, 8, 1, i == ui.view ? K_ACC : K_LINE);
    cv_blit(0, KNB_Y);
}

/* a button's card (help_tick): its name, then what it does. A line starting with a space goes on from the one
 * before (a screen reader joins them: sp_line) */
static const char *const BTN_NAME[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                         "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("THE PLAY PAGE:"); L("TEMPO, ENERGY, ECHO, MOOD"); break;
    case B_ENV: L("THE MIX:"); L("PULSE 1, PULSE 2,"); L(" TRIANGLE, NOISE"); break;
    case B_GLO: L("THE SONG PAGE:"); L("SWING, TONE, LOOPS, LIGHTS"); break;
    case B_EDIT: L("THE NEXT SONG:"); L("A NEW KEY AND TUNE"); break;
    case B_REC: L("REPEAT: THIS SONG"); L(" LOOPS ON"); L("AGAIN: ITS ENDING, THEN"); L(" THE NEXT"); break;
    case B_OCTDN: L("THE KEYS AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE KEYS AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES EVERY STYLE"); L("KEEP HOLDING: THE STYLE"); L(" BACK TO ITS RECIPE"); break;
    case B_SEL: L("SELECT TURNS THE PAGES"); L("PRESETS: THE STYLE"); L("ALGORITHM: TONE"); break;
    default: break;
    }
#undef L
    return n;
}

/* the lights: the page's button, PLAY, REPEAT; the chord's notes on the keys */
static void leds(void)
{
    uint32_t b = 0, k = 0, i, pcs = 0;
    const en_song_t *s = en_cur;
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (en_repeat_on)
        b |= 1u << B_REC;
    if (proj.leds && en_playing && en_phase != PH_GAP) {
        uint8_t c = s->pchord[s->order[en_ord]][en_row >> 4 & 1];
        if (c != 0xFFu) {
            static const uint8_t IV[3][3] = {{0, 4, 7}, {0, 3, 7}, {0, 3, 6}};
            for (i = 0; i < 3; i++)
                pcs |= 1u << (((c & 15u) + IV[(c >> 4) % 3u][i]) % 12u);
        }
        for (i = 0; i < NKEYS; i++)
            if (pcs >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    }
    plat_glow(proj.leds == 1);
    plat_leds(b, k | ui.keys);
}

/* AUTOSAVE (the flash wears, and a write can glitch the sound): the settings once the knobs have rested 10 s, at
 * most every 5 minutes; where the player is (the song) at most hourly. Both at a quiet moment: the gap between
 * songs, or stopped. SAVE saves at once */
#define AUTOSAVE_REST 10000u
#define AUTOSAVE_SETTINGS 300000u
#define AUTOSAVE_SONG 3600000u
static void autosave(void)
{
    uint32_t now = plat_ms(), since = now - ui.saved_t;
    int quiet = !en_playing || en_phase == PH_GAP, want;
    if (!quiet || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_REST)
        return;
    want = (ui.dirty && since >= AUTOSAVE_SETTINGS) || (en_song_no != ui.saved_song && since >= AUTOSAVE_SONG);
    if (!want)
        return;
    if (project_save() == 0) {
        ui.dirty = 0;
        ui.saved_song = en_song_no;
    }
    ui.saved_t = now;                            /* (a failed save waits its turn too) */
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader (announce.c; fm1-reader): a page once it settles, with its knobs; a
 * knob as it turns; a help card as it comes up; each new song (its title, key, tempo); playing and stopped; what
 * a button did (say). WHERE is the whole screen in two: the page and the header, then the song. */
static struct {
    uint8_t view, help, started;
    int16_t val[4];
    uint32_t song;
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
    sp_cat(sp_tp.playing ? ", playing" : ", stopped");
    if (en_repeat_on)
        sp_cat(", repeat on");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}
static void sp_song_text(void)                   /* "Song 12, Crystal Cavern. F minor, 96 BPM" */
{
    const en_song_t *s = en_cur;
    char t[28];
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("Song ");
    sp_int(en_song_no);
    sp_cat(", ");
    title_case(t, s->title, sizeof t);
    sp_cat(t);
    sp_cat(". ");
    sp_cat(EN_NOTE_NAME[s->key % 12]);
    sp_cat(" ");
    title_case(t, en_mode_name(s->mode), sizeof t);
    t[0] = (char)(t[0] + 32);
    sp_cat(t);
    sp_cat(", ");
    sp_int(en_bpm);
    sp_cat(" BPM");
}
static void sp_where_song(void)                  /* WHERE's second part: the song, the section */
{
    static const char *const SW[] = {"intro", "verse", "chorus", "bridge", "ending"};
    const en_song_t *s = en_cur;
    sp_song_text();
    sp_cat(". ");
    sp_cat(en_phase == PH_GAP ? "between songs" : SW[sect_index(s->osect[en_ord])]);
    sp_cat(", pattern ");
    sp_int(s->order[en_ord]);
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
    sp_cat(BTN_NAME[ui.help - 1]);
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
    sp.song = en_song_no;
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
            sp_where_song();
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
    if (en_song_no != sp.song && en_phase != PH_GAP) {   /* a new song: queued */
        sp.song = en_song_no;
        sp_song_text();
        ann_say(sp_buf, 0, 0);
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card, title and message as a screen reader gets them (the "speakdump" command) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"}, {"NEXT SONG", 0},
                                         {"STOPPED", 0}, {"PLAYING", 0}, {"REPEAT", "ON"}, {"REPEAT", "OFF"},
                                         {"SAVED", 0}, {"KEYS OCTAVE", "-2"}, {"KEYS OCTAVE", "-1"},
                                         {"KEYS OCTAVE", "0"}, {"KEYS OCTAVE", "+1"}, {"KEYS OCTAVE", "+2"}};
    static const char *const WARN[][2] = {{"NOT SAVED", "FLASH ERROR"}, {"KEYS OCTAVE", "LOWEST"},
                                          {"KEYS OCTAVE", "HIGHEST"}};
    static en_song_t tmp;
    project_t keep = proj;
    const en_song_t *keep_song = en_cur;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty, keep_play = sp_tp.playing;
    uint32_t keep_no = en_song_no;
    uint8_t keep_rep = en_repeat_on;
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
    for (x = 0; x < 8; x++) {                    /* WHERE: playing, stopped, unsaved, repeat */
        ui.view = (uint8_t)(x % NVIEWS);
        sp_tp.playing = (uint8_t)(x & 1);
        ui.dirty = (uint8_t)(x >> 1 & 1);
        en_repeat_on = (uint8_t)(x == 5);
        proj.style = (uint8_t)(x * 3 % NSTYLES);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_where_song();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
    for (x = 0; x < 300; x++) {                  /* songs: their titles, keys and modes */
        en_make_song(&tmp, x % NSTYLES, en_song_rand(0x43484950u, (uint32_t)x + 1u), (x * 37) % 101, 120);
        en_cur = &tmp;
        en_song_no = (uint32_t)x + 1u;
        sp_song_text();
        sp_print("SONG", sp_buf);
    }
    en_cur = keep_song;
    en_song_no = keep_no;
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
    ui.touched = -1;
    ui.saved_t = plat_ms();
    ui.saved_song = proj.song_no + 1u;            /* the song the boot starts: nothing new to keep yet */
    ui.act_t = plat_ms();
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
    ui.vu += (en_vu - ui.vu) * 0.3f;
    draw_header();
    if (!(ui.frame & 1u))                        /* the scopes: 30 frames a second */
        draw_scopes();
    draw_tracker();
    draw_knobs();
    leds();
    speak();
    ann_tick(plat_ms());
    ui.frame++;
}

void ui_input_only(void) { input(); }
