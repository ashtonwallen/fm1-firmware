/* SPDX-License-Identifier: MPL-2.0 */
#include "app.h"
#include "radio.h"

#define SHORTWAVE_SETTINGS 0x31565753u /* SWV1 */

enum {
    SCAN_HOLD_MS = 40000,
    SCAN_TRAVEL_MS = 18000,
    DISPLAY_PERIOD_MS = 120,
    SPECTRUM_COLS = 106,
    WATERFALL_ROWS = 24,
    SCOPE_LEFT = 14,
    WATERFALL_TOP = 65
};

typedef struct {
    uint32_t magic;
    uint16_t dial;
    uint8_t noise;
    uint8_t motion;
    uint8_t scan;
    uint8_t reserved[3];
} shortwave_settings_t;

static shortwave_settings_t sw_settings;
typedef struct {
    uint32_t buttons;
    uint32_t scan_started;
    uint32_t last_display;
    uint32_t last_sample;
    uint32_t spectrum_rng;
    uint32_t notice_at;
    int scan_from;
    int scan_to;
    int direction;
    int moving;
    int playing;
    int dirty;
    int redraw;
    char notice[32];
    uint8_t history[WATERFALL_ROWS][SPECTRUM_COLS];
    unsigned head;
} shortwave_ui_t;
static shortwave_ui_t sw_ui;
static volatile int sw_panic;
static const char *const sw_names[RADIO_STATIONS] = {
    "DEEP FIELD", "GLASS FM", "NIGHT SERVICE", "SOFT RELAY"
};

static int sw_abs(int number) { return number < 0 ? -number : number; }

static int sw_station(int dial)
{
    int nearest = 0;
    for (int i = 1; i < RADIO_STATIONS; ++i) {
        if (sw_abs(dial - radio_station_center(i)) <
            sw_abs(dial - radio_station_center(nearest)))
            nearest = i;
    }
    return nearest;
}

static void sw_apply_controls(void)
{
    radio_controls(sw_settings.dial, sw_settings.noise, sw_settings.motion, sw_ui.playing);
}

void app_init(void)
{
    if (plat_store_load(OBJ_SETTINGS, &sw_settings, sizeof sw_settings) != (int)sizeof sw_settings ||
        sw_settings.magic != SHORTWAVE_SETTINGS || sw_settings.dial > 1000 ||
        sw_settings.noise > 100 || sw_settings.motion > 100 || sw_settings.scan > 1) {
        sw_settings = (shortwave_settings_t){
            .magic = SHORTWAVE_SETTINGS, .dial = 80, .noise = 24, .motion = 50, .scan = 1
        };
    }
    sw_ui.playing = 1;
    sw_panic = 0;
    radio_init();
    sw_apply_controls();
}

void app_render(int32_t *lr, uint32_t frames, uint32_t master_q12)
{
    if (sw_panic) {
        for (uint32_t i = 0; i < 2u * frames; ++i)
            lr[i] = 0;
        return;
    }
    radio_render(lr, frames, master_q12);
}

void app_panic(void) { sw_panic = 1; }

static void sw_tune(int dial)
{
    sw_settings.dial = (uint16_t)clamp(dial, 0, 1000);
    sw_apply_controls();
    sw_ui.redraw = 1;
}

static void sw_scan_reset(uint32_t now)
{
    sw_ui.moving = 0;
    sw_ui.scan_started = now;
}

static void sw_manual_tune(int dial)
{
    sw_settings.scan = 0;
    sw_ui.moving = 0;
    sw_ui.dirty = 1;
    sw_tune(dial);
}

static void sw_scan(uint32_t now)
{
    if (!sw_settings.scan || !sw_ui.playing)
        return;

    uint32_t elapsed = now - sw_ui.scan_started;
    if (!sw_ui.moving) {
        if (elapsed < SCAN_HOLD_MS)
            return;
        int station = sw_station(sw_settings.dial);
        if (station == 0 || station == RADIO_STATIONS - 1)
            sw_ui.direction = station == 0 ? 1 : -1;
        sw_ui.scan_from = sw_settings.dial;
        sw_ui.scan_to = radio_station_center(station + sw_ui.direction);
        sw_ui.scan_started = now;
        sw_ui.moving = 1;
        return;
    }

    if (elapsed >= SCAN_TRAVEL_MS) {
        sw_tune(sw_ui.scan_to);
        sw_scan_reset(now);
    } else {
        int dial = sw_ui.scan_from +
            (sw_ui.scan_to - sw_ui.scan_from) * (int)elapsed / SCAN_TRAVEL_MS;
        if (dial != sw_settings.dial)
            sw_tune(dial);
    }
}

static void sw_notice(const char *text)
{
    unsigned i = 0;
    while (text && text[i] && i < sizeof sw_ui.notice - 1u) {
        sw_ui.notice[i] = text[i];
        ++i;
    }
    sw_ui.notice[i] = 0;
    sw_ui.notice_at = plat_ms();
    sw_ui.redraw = 1;
    if (ann_on() && sw_ui.notice[0])
        ann_say(sw_ui.notice, 0, ANN_NOW);
}

void app_say(const char *first, const char *second)
{
    sw_notice(second && *second ? second : first);
}

static void sw_controls(uint32_t now)
{
    uint32_t buttons = plat_buttons();
    uint32_t pressed = buttons & ~sw_ui.buttons;
    sw_ui.buttons = buttons;

    int select = clamp(plat_enc(EN_SELECT), -125, 125);
    int preset = clamp(plat_enc(EN_PRESET), -RADIO_STATIONS, RADIO_STATIONS);
    int noise = clamp(plat_enc(EN_K1), -50, 50);
    int motion = clamp(plat_enc(EN_K2), -50, 50);
    if (select)
        sw_manual_tune(sw_settings.dial + select * 8);
    if (preset) {
        int next = sw_station(sw_settings.dial) + preset;
        sw_manual_tune(radio_station_center(clamp(next, 0, RADIO_STATIONS - 1)));
    }
    if (pressed & (1u << B_HOME))
        sw_manual_tune(radio_station_center(sw_station(sw_settings.dial)));
    if (noise || motion) {
        sw_settings.noise = (uint8_t)clamp(sw_settings.noise + 2 * noise, 0, 100);
        sw_settings.motion = (uint8_t)clamp(sw_settings.motion + 2 * motion, 0, 100);
        sw_ui.dirty = 1;
        sw_apply_controls();
        sw_ui.redraw = 1;
    }
    if (pressed & (1u << B_SEQ)) {
        sw_settings.scan = (uint8_t)!sw_settings.scan;
        sw_scan_reset(now);
        sw_ui.dirty = 1;
        sw_notice(sw_settings.scan ? "SCAN" : "HOLD");
    }
    if (pressed & (1u << B_PLAY)) {
        sw_ui.playing = !sw_ui.playing;
        sw_scan_reset(now);
        sw_apply_controls();
        sw_ui.redraw = 1;
    }
    if (pressed & (1u << B_SAVE)) {
        if (plat_store_save(OBJ_SETTINGS, &sw_settings, sizeof sw_settings) == 0) {
            sw_ui.dirty = 0;
            sw_notice("SAVED");
        } else {
            sw_notice("SAVE FAILED");
        }
    }

    plat_lights((sw_ui.playing ? LED_PLAY_GREEN : 0u) |
                    (sw_settings.scan ? 1u << B_SEQ : 0u) |
                    (sw_ui.dirty ? 1u << B_SAVE : 0u),
                0, (1u << B_HOME) | (1u << B_SAVE) | (1u << B_PLAY) | (1u << B_SEQ), 0, 1);
}

void app_input_only(void) { sw_controls(plat_ms()); }

#define SW_BG RGB(10, 17, 20)
#define SW_PANEL RGB(13, 24, 27)
#define SW_RULE RGB(35, 61, 64)
#define SW_MUTED RGB(141, 175, 169)
#define SW_TEXT RGB(222, 239, 228)
#define SW_SIGNAL RGB(142, 224, 166)
#define SW_POINTER RGB(255, 202, 126)

static const uint16_t sw_water_colors[8] = {
    RGB(9, 18, 23), RGB(17, 32, 42), RGB(25, 53, 62), RGB(32, 75, 78),
    RGB(38, 104, 97), RGB(47, 140, 111), RGB(83, 175, 125), RGB(144, 216, 154)
};

static int sw_x_for_dial(int dial) { return SCOPE_LEFT + (2 * SPECTRUM_COLS - 1) * dial / 1000; }

static uint32_t sw_random(void)
{
    uint32_t next = sw_ui.spectrum_rng;
    next ^= next << 13;
    next ^= next >> 17;
    next ^= next << 5;
    sw_ui.spectrum_rng = next;
    return next;
}

static void sw_sample_spectrum(void)
{
    int gain[RADIO_STATIONS];
    int strongest = 0;
    for (int station = 0; station < RADIO_STATIONS; ++station) {
        gain[station] = radio_signal(sw_settings.dial, station);
        if (gain[station] > strongest)
            strongest = gain[station];
    }

    sw_ui.head = (sw_ui.head + WATERFALL_ROWS - 1u) % WATERFALL_ROWS;
    uint8_t *spectrum = sw_ui.history[sw_ui.head];
    for (int column = 0; column < SPECTRUM_COLS; ++column) {
        int frequency = column * 1000 / (SPECTRUM_COLS - 1);
        int floor = 2 + (1000 - strongest) * sw_settings.noise / 4500;
        int strength = (int)(sw_random() & 7u) * floor / 4;
        for (int station = 0; station < RADIO_STATIONS; ++station) {
            int distance = sw_abs(frequency - radio_station_center(station));
            int carrier = clamp(82 - distance * 3, 0, 82);
            int sideband = clamp(31 - sw_abs(distance - 33) * 2, 0, 31);
            int receiver = 210 + gain[station] * 7 / 10;
            strength += (carrier + sideband) * receiver / 1000;
        }
        spectrum[column] = (uint8_t)clamp(strength, 0, 100);
    }
}

static void sw_value_text(char *out, int value)
{
    if (value == 100)
        *out++ = '1';
    if (value >= 10)
        *out++ = (char)('0' + (value / 10) % 10);
    *out++ = (char)('0' + value % 10);
    *out++ = '%';
    *out = 0;
}

static void sw_frequency_text(char out[8], int dial)
{
    int frequency = 500 + dial; /* fake band 5.00–15.00 MHz */
    char *at = out;
    if (frequency >= 1000)
        *at++ = '1';
    *at++ = (char)('0' + (frequency / 100) % 10);
    *at++ = '.';
    *at++ = (char)('0' + (frequency / 10) % 10);
    *at++ = (char)('0' + frequency % 10);
    *at = 0;
}

static void sw_draw_header(const char *station, const char *mode)
{
    char frequency[8];
    sw_frequency_text(frequency, sw_settings.dial);

    cv_begin(240, 58, SW_BG);
    cv_text(12, 5, &FONT_XS, APP_NAME, SW_MUTED);
    int mode_x = 227 - text_w(&FONT_XS, mode);
    cv_rect(mode_x - 9, 9, 4, 4, sw_ui.playing ? (radio_level() > 60 ? SW_SIGNAL : SW_MUTED) : SW_POINTER);
    cv_text(mode_x, 5, &FONT_XS, mode, SW_TEXT);

    int end = cv_text(11, 20, &FONT_L, frequency, SW_TEXT);
    cv_text(end + 4, 37, &FONT_XS, "MHz", SW_MUTED);
    cv_text(228 - text_w(&FONT_XS, station), 38, &FONT_XS, station, SW_SIGNAL);
    cv_blit(0, 0);
}

static void sw_draw_scope(void)
{
    cv_begin(240, 116, SW_PANEL);
    cv_rect(0, 0, 240, 1, SW_RULE);
    int pointer = sw_x_for_dial(sw_settings.dial);
    static const char *const ticks[] = {"5", "7", "9", "11", "13", "15"};
    for (int i = 0; i < 6; ++i) {
        int x = sw_x_for_dial(i * 200);
        cv_rect(x, 17, 1, 45, SW_RULE);
        cv_text(x - text_w(&FONT_XS, ticks[i]) / 2, 3, &FONT_XS, ticks[i], SW_MUTED);
    }
    for (int i = 1; i < 4; ++i)
        cv_rect(SCOPE_LEFT, 17 + i * 11, 212, 1, SW_RULE);

    const uint8_t *live = sw_ui.history[sw_ui.head];
    int last_y = 61 - live[0] * 41 / 100;
    for (int column = 1; column < SPECTRUM_COLS; ++column) {
        int x = SCOPE_LEFT + column * 2;
        int y = 61 - live[column] * 41 / 100;
        cv_line(x - 2, last_y, x, y, SW_SIGNAL);
        last_y = y;
    }
    cv_rect(pointer, 15, 1, 48, SW_POINTER);
    cv_rect(pointer - 2, 15, 5, 2, SW_POINTER);

    for (unsigned age = 0; age < WATERFALL_ROWS; ++age) {
        const uint8_t *row = sw_ui.history[(sw_ui.head + age) % WATERFALL_ROWS];
        for (int column = 0; column < SPECTRUM_COLS; ++column) {
            uint16_t color = sw_water_colors[row[column] * 7 / 100];
            cv_rect(SCOPE_LEFT + column * 2, WATERFALL_TOP + (int)age * 2, 2, 2, color);
        }
    }
    cv_rect(pointer - 2, 113, 5, 2, SW_POINTER);
    cv_blit(0, 58);
}

static void sw_draw_footer(int signal)
{
    char noise[6], motion[6];
    sw_value_text(noise, sw_settings.noise);
    sw_value_text(motion, sw_settings.motion);

    cv_begin(240, 66, SW_BG);
    cv_rect(0, 0, 240, 1, SW_RULE);
    cv_text(13, 6, &FONT_XS, "SIGNAL", SW_MUTED);
    for (int i = 0; i < 16; ++i)
        cv_rect(68 + i * 10, 8, 7, 7,
                sw_ui.playing && signal >= i * 62 + 31 ? SW_SIGNAL : SW_RULE);
    cv_rect(13, 25, 214, 1, SW_RULE);
    cv_text(13, 29, &FONT_XS, "NOISE", SW_MUTED);
    cv_text(64, 27, &FONT_B, noise, SW_TEXT);
    cv_text(122, 29, &FONT_XS, "MOTION", SW_MUTED);
    cv_text(180, 27, &FONT_B, motion, SW_TEXT);

    const char *footer = "SELECT TUNE   PRESETS STATION";
    uint16_t color = SW_MUTED;
    if (sw_ui.notice[0]) {
        footer = sw_ui.notice;
        color = SW_POINTER;
    }
    cv_text(13, 49, &FONT_XS, footer, color);
    cv_blit(0, 174);
}

static void sw_draw(void)
{
    int station = sw_station(sw_settings.dial);
    int signal = radio_signal(sw_settings.dial, station);
    const char *name = signal >= 420 ? sw_names[station] : "BETWEEN STATIONS";
    const char *mode = !sw_ui.playing ? "PAUSED" : sw_settings.scan ? "SCAN" : "HOLD";
    sw_draw_header(name, mode);
    sw_draw_scope();
    sw_draw_footer(signal);

    if (ann_on() && ann_where())
        ann_say(name, 0, ANN_NOW);
}

void app_ui_init(void)
{
    uint32_t now = plat_ms();
    sw_ui = (shortwave_ui_t){0};
    sw_ui.playing = 1;
    sw_ui.direction = 1;
    sw_ui.redraw = 1;
    sw_ui.spectrum_rng = 0xA3B27E91u;
    sw_ui.last_sample = now - DISPLAY_PERIOD_MS;
    sw_scan_reset(now);
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    sw_controls(now);
    sw_scan(now);
    if (sw_ui.notice[0] && now - sw_ui.notice_at >= 1400u) {
        sw_ui.notice[0] = 0;
        sw_ui.redraw = 1;
    }
    if (now - sw_ui.last_sample >= DISPLAY_PERIOD_MS) {
        sw_ui.last_sample = now;
        if (sw_ui.playing)
            sw_sample_spectrum();
        sw_ui.redraw = 1;
    }
    if (sw_ui.redraw && now - sw_ui.last_display >= 60u) {
        sw_ui.redraw = 0;
        sw_ui.last_display = now;
        sw_draw();
    }
    ann_tick(now);
}

#ifdef PLAT_HOST
int app_expect(const char *name, int *value)
{
    if (!strcmp(name, "dial"))
        *value = sw_settings.dial;
    else if (!strcmp(name, "station"))
        *value = sw_station(sw_settings.dial);
    else if (!strcmp(name, "scan"))
        *value = sw_settings.scan;
    else if (!strcmp(name, "playing"))
        *value = sw_ui.playing;
    else if (!strcmp(name, "noise"))
        *value = sw_settings.noise;
    else if (!strcmp(name, "motion"))
        *value = sw_settings.motion;
    else
        return 0;
    return 1;
}
#endif
