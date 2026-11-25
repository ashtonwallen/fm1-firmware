/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * DUB DESK: ashtonwallen, 2026 */
/* The instrument on the host: the pure parts (names, the styles' recipes) and the render: every style makes sound,
 * nothing is NaN, the peaks stay under -1 dBFS over long runs, versions move on, a skip works, the keys play along,
 * the engineer moves the faders. Then the render's cost, in ns a frame (the CPU check: compare with AMB-1's busiest
 * piece).
 *   dub_test                     the checks
 *   dub_test wav DIR             and a minute of each style, for listening
 *   dub_test dump STYLE BARS     what the band plays, bar by bar (to read as a musician would)
 *   dub_test levels STYLE        each part alone: its loudness */
#define _POSIX_C_SOURCE 199309L
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "dub.h"

static int fails;
#define CHECK(c, ...)                                                                                              \
    do {                                                                                                           \
        if (!(c)) {                                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                            \
            printf(__VA_ARGS__);                                                                                   \
            printf("\n");                                                                                          \
            fails++;                                                                                               \
        }                                                                                                          \
    } while (0)

static void style(int i)
{
    int16_t p[P_NPARAMS];
    int k;
    en_style_recipe(i, p);
    en_style(i);
    for (k = 0; k < P_NSTYLE; k++)
        en_set(k, p[k]);
}

typedef struct {
    double peak, sum2;
    uint32_t n, nan;
} stats_t;

static FILE *wav;
static uint32_t wav_n;
static void wav_open(const char *path)
{
    static const uint8_t H[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
                                  1, 0, 2, 0, 0x44, 0xAC, 0, 0, 0x10, 0xB1, 2, 0, 4, 0, 16, 0,
                                  'd', 'a', 't', 'a', 0, 0, 0, 0};
    wav = fopen(path, "wb");
    if (wav)
        fwrite(H, 1, 44, wav);
    wav_n = 0;
}
static void wav_close(void)
{
    uint32_t d = wav_n * 4u, r = d + 36u;
    if (!wav)
        return;
    fseek(wav, 4, SEEK_SET);
    fwrite(&r, 4, 1, wav);
    fseek(wav, 40, SEEK_SET);
    fwrite(&d, 4, 1, wav);
    fclose(wav);
    wav = 0;
}

static uint32_t run_gain = 4096;
static void run(double sec, stats_t *s)
{
    static int32_t buf[512];
    uint32_t b, nb = (uint32_t)(sec * 44100.0 / 256.0), j;
    for (b = 0; b < nb; b++) {
        en_render(buf, 256, run_gain);
        for (j = 0; j < 512; j++) {
            double v = (double)buf[j] / 8388608.0;
            if (v != v)
                s->nan++;
            if (fabs(v) > s->peak)
                s->peak = fabs(v);
            s->sum2 += v * v;
            s->n++;
            if (wav) {
                int16_t x = (int16_t)(buf[j] >> 8);
                fwrite(&x, 2, 1, wav);
            }
        }
        if (wav)
            wav_n += 256u;
    }
}
static double db(double x) { return 20.0 * log10(x + 1e-12); }

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/* ---- the dump: a bar a line per part */
static const char *nn(int n)
{
    static char b[8][16];
    static int k;
    char *s = b[k++ & 7];
    n &= 127;
    snprintf(s, 16, "%s%d", EN_NOTE_NAME[n % 12], n / 12 - 1);
    return s;
}
static char line[16][400];
static int cur_bar = -1, moves, fills_n;
static void flush(void)
{
    int i;
    if (cur_bar < 0)
        return;
    for (i = 0; i < 16; i++)
        if (line[i][0])
            printf("%s\n", line[i]);
    for (i = 0; i < 16; i++)
        line[i][0] = 0;
}
static void add(int row, const char *fmt, ...)
{
    char t[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    if (strlen(line[row]) + strlen(t) < sizeof line[row] - 1)
        strcat(line[row], t);
}
static const char *const BLK_NAME[] = {"intro-drums", "intro-band", "intro-dnb", "FULL", "FULL+MEL", "riddim",
                                       "keys", "drum&bass", "dnb+skank", "drums", "no-bass", "BUILD", "strip",
                                       "tease", "mel+dnb", "OUTRO"};
static const char *const MV_NAME[] = {"", "throw", "SWELL", "SPRING", "filter", "SIREN", "drop", "back", "strip"};
static void logger(int type, int bar, int step, int note, float vel)
{
    char cn[12];
    if (bar != cur_bar) {
        flush();
        cur_bar = bar;
    }
    switch (type) {
    case L_BAR: {
        int m = (int)vel;
        snprintf(line[0], sizeof line[0], "--- v%u %-13s bar %3d %-11s [%s%s%s%s%s%s] %s %3d bpm  %s", (unsigned)en_track_no,
                 en_riddim_name(en_riddim), bar, BLK_NAME[note], m & 1 ? "D" : "-", m & 2 ? "B" : "-",
                 m & 4 ? "G" : "-", m & 8 ? "O" : "-", m & 16 ? "M" : "-", m & 32 ? "H" : "-",
                 en_chord_name(en_chord_root, en_chord_q, cn), en_bpm, step == 0 ? "" : "?");
        break;
    }
    case L_BASS: if (!line[1][0]) add(1, "  bass  "); add(1, "%2d:%-4s ", step, nn(note)); break;
    case L_MEL: if (!line[2][0]) add(2, "  mel   "); add(2, "%2d:%-4s ", step, nn(note)); break;
    case L_HORN: if (!line[3][0]) add(3, "  horns "); add(3, "%2d:%-4s ", step, nn(note)); break;
    case L_GTR: if (!line[4][0]) add(4, "  skank "); if (strlen(line[4]) < 60) add(4, "%2d:%s ", step, nn(note)); break;
    case L_ORG: if (!line[5][0]) add(5, "  organ "); if (strlen(line[5]) < 50) add(5, "%2d:%s ", step, nn(note)); break;
    case L_KICK: if (!line[6][0]) add(6, "  drums "); add(6, "%2dK ", step); break;
    case L_RIM: if (!line[6][0]) add(6, "  drums "); add(6, "%2dR%s ", step, vel < 0.5f ? "g" : ""); break;
    case L_SNARE: if (!line[6][0]) add(6, "  drums "); add(6, "%2dS ", step); break;
    case L_TOM: if (!line[7][0]) add(7, "  fill  "); add(7, "T%d ", note); fills_n++; break;
    case L_MOVE: if (!line[8][0]) add(8, "  desk  "); add(8, "%2d:%s ", step, MV_NAME[note]); moves++; break;
    case L_SIREN: add(8, "(siren %d) ", note); break;
    default: break;
    }
}

static int dump(int st, int bars)
{
    stats_t a = {0};
    en_init();
    style(st);
    en_set(P_LENGTH, 1);
    en_track(0, 0x5EED0000u + (uint32_t)st);
    en_log = logger;
    en_play(1);
    printf("=== %s\n", en_style_name(st));
    while (en_beats / 4 < (uint32_t)bars)
        run(1.0, &a);
    flush();
    en_log = 0;
    printf("moves %d, fill hits %d\n", moves, fills_n);
    return 0;
}

static int levels(int st)
{
    static const int P[] = {P_DRUMS, P_BASS, P_SKANK, P_ORGAN, P_LEAD};
    int i, k;
    run_gain = 400;                          /* below the leveller: the parts as mixed */
    for (i = 0; i < 5; i++) {
        stats_t a = {0};
        en_init();
        style(st);
        en_set(P_ENGINEER, 0);
        en_set(P_SPRING, 0);
        en_set(P_ECHO, 0);
        for (k = 0; k < 5; k++)
            if (k != i)
                en_set(P[k], 0);
        en_track(0, 0x1234u);
        en_play(1);
        run(20.0, &a);
        memset(&a, 0, sizeof a);
        run(60.0, &a);
        printf("%-8s alone: rms %5.1f dB peak %5.1f dB\n", en_param_info(P[i])->name, db(sqrt(a.sum2 / a.n)), db(a.peak));
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i;
    char t[32];
    if (argc > 3 && !strcmp(argv[1], "dump"))
        return dump(atoi(argv[2]), atoi(argv[3]));
    if (argc > 2 && !strcmp(argv[1], "levels"))
        return levels(atoi(argv[2]));
    /* the pure parts */
    CHECK(!strcmp(en_chord_name(9, 0, t), "Am"), "chord name: %s", t);
    CHECK(!strcmp(en_chord_name(2, 1, t), "D"), "chord name: %s", t);
    for (i = 0; i < EN_NRIDDIMS; i++)
        CHECK(strlen(en_riddim_name(i)) > 3 && strlen(en_riddim_name(i)) <= 16, "riddim name %d", i);
    for (i = 0; i < EN_NSTYLES; i++) {
        int16_t p[P_NPARAMS];
        int k;
        en_style_recipe(i, p);
        for (k = 0; k < P_NSTYLE; k++)
            CHECK(p[k] >= en_param_info(k)->lo && p[k] <= en_param_info(k)->hi, "style %d param %d", i, k);
        CHECK(strlen(en_style_name(i)) <= 16, "style name %s too long", en_style_name(i));
    }
    /* every style: sound within seconds, bounded over a long run, versions move on, the engineer works */
    for (i = 0; i < EN_NSTYLES; i++) {
        stats_t a = {0}, b = {0};
        uint32_t tr0, mv0;
        char path[256];
        en_init();
        style(i);
        en_set(P_LENGTH, 0);                     /* two-minute versions: several in the run */
        en_track((uint32_t)i * 10u, 0xC0FFEEu + (uint32_t)i);
        en_play(1);
        if (argc > 2 && !strcmp(argv[1], "wav")) {
            snprintf(path, sizeof path, "%s/style%02d.wav", argv[2], i);
            wav_open(path);
        }
        run(60.0, &a);
        wav_close();
        tr0 = en_tracks_played;
        mv0 = en_move_no;
        run(420.0, &b);
        printf("%-14s first minute rms %5.1f dB peak %5.1f dB; next 7 min peak %5.1f dB, rms %5.1f dB, versions %u, moves %u\n",
               en_style_name(i), db(sqrt(a.sum2 / a.n)), db(a.peak), db(b.peak), db(sqrt(b.sum2 / b.n)),
               (unsigned)(en_tracks_played - tr0), (unsigned)(en_move_no - mv0));
        CHECK(a.nan == 0 && b.nan == 0, "%s: NaN", en_style_name(i));
        CHECK(db(sqrt(a.sum2 / a.n)) > -36.0, "%s: too quiet", en_style_name(i));
        CHECK(db(a.peak) < -1.0 && db(b.peak) < -1.0, "%s: peaks over -1 dBFS", en_style_name(i));
        CHECK(en_tracks_played - tr0 >= 2, "%s: versions did not move on", en_style_name(i));
        CHECK(en_move_no - mv0 >= 20, "%s: the engineer sat still", en_style_name(i));
    }
    /* the extremes: everything up, everything down */
    {
        stats_t a = {0};
        int k;
        en_init();
        style(8);
        for (k = 0; k < P_NSTYLE; k++)
            en_set(k, en_param_info(k)->hi);
        en_play(1);
        run(300.0, &a);
        printf("everything up: peak %.1f dB rms %.1f dB\n", db(a.peak), db(sqrt(a.sum2 / a.n)));
        CHECK(a.nan == 0 && db(a.peak) < -1.0, "everything up: peak %.1f", db(a.peak));
        for (k = 0; k < P_NSTYLE; k++)
            en_set(k, en_param_info(k)->lo);
        memset(&a, 0, sizeof a);
        run(60.0, &a);
        CHECK(a.nan == 0 && db(a.peak) < -1.0, "everything down");
    }
    /* a skip: the next riddim within a few bars */
    {
        stats_t a = {0};
        uint32_t no;
        en_init();
        style(0);
        en_play(1);
        run(20.0, &a);
        no = en_track_no;
        en_skip();
        run(14.0, &a);
        CHECK(en_track_no == no + 1, "skip: version %u -> %u", (unsigned)no, (unsigned)en_track_no);
    }
    /* stopped: the echo's tail dies away, then silence; rolling again: sound */
    {
        stats_t a = {0}, b = {0};
        en_init();
        style(0);
        en_play(1);
        run(10.0, &a);
        en_play(0);
        run(25.0, &a);
        run(2.0, &b);
        CHECK(db(b.peak) < -60.0, "stopped: peak %.1f dB", db(b.peak));
        en_play(1);
        memset(&b, 0, sizeof b);
        run(8.0, &b);
        CHECK(db(sqrt(b.sum2 / b.n)) > -40.0, "rolling: rms %.1f", db(sqrt(b.sum2 / b.n)));
    }
    /* the keys play along */
    {
        stats_t a = {0};
        en_init();
        en_play(0);
        run(3.0, &a);
        memset(&a, 0, sizeof a);
        en_note(60, 100);
        run(0.5, &a);
        en_note(60, 0);
        CHECK(db(a.peak) > -40.0, "a key: peak %.1f dB", db(a.peak));
    }
    /* repeat: the riddim plays on */
    {
        stats_t a = {0};
        uint32_t no;
        en_init();
        style(0);
        en_set(P_LENGTH, 0);
        en_repeat(1);
        en_play(1);
        run(1.0, &a);
        no = en_track_no;
        run(400.0, &a);
        CHECK(en_track_no == no, "repeat: moved on to %u", (unsigned)en_track_no);
    }
    /* the cost: ns a frame, each style, everything up */
    {
        static int32_t buf[512];
        double worst = 0.0;
        int wi = 0;
        for (i = 0; i < EN_NSTYLES; i++) {
            double t0, dt;
            int b;
            en_init();
            style(i);
            en_set(P_LEAD, 100);
            en_set(P_SKANK, 100);
            en_set(P_ORGAN, 100);
            en_play(1);
            for (b = 0; b < 2000; b++)
                en_render(buf, 256, 4096);
            t0 = now();
            for (b = 0; b < 10000; b++)
                en_render(buf, 256, 4096);
            dt = (now() - t0) / (10000.0 * 256.0) * 1e9;
            if (dt > worst)
                worst = dt, wi = i;
        }
        printf("render: busiest %s %.1f ns/frame\n", en_style_name(wi), worst);
    }
    printf(fails ? "dub_test: %d FAILED\n" : "dub_test: all passed\n", fails);
    return fails != 0;
}
