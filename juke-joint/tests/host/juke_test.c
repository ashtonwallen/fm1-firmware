/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * JUKE JOINT: ashtonwallen, 2026 */
/* The instrument on the host: the pure parts (titles, the nights' recipes) and the render: every night makes sound,
 * nothing is NaN, the peaks stay under -1 dBFS over long runs, tunes move on, a skip works, resting is silent, the
 * keys play along, repeat holds. Then the render's cost in ns a frame (the CPU check: AMB-1 Drift's busiest piece,
 * Low Tide, is ~168 ns/frame on this host).
 *   juke_test                        the checks
 *   juke_test dump NIGHT SEED [DRUMS] what a tune plays, bar by bar (chords, bass, piano, harp; the drums with 1)
 *   juke_test demos DIR              the demo clips: NN-<night>.wav (20 s, settled) and 00-listen.wav (3 min)
 *   juke_test wav NIGHT SEED SEC FILE   one tune from its start */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "juke.h"

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
    uint32_t n, nan, maxv;
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

static int master = 4096;
static void run(double sec, stats_t *s)
{
    static int32_t buf[512];
    uint32_t b, nb = (uint32_t)(sec * 44100.0 / 256.0), j;
    for (b = 0; b < nb; b++) {
        en_render(buf, 256, master);
        if (en_voices > s->maxv)
            s->maxv = en_voices;
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

/* ---- the dump: what each bar plays */
static const char *const ROLE[] = {"B", "c", "L", "P", "H", "D", "K"};
static const char *const SEC[] = {"count", "intro", "head", "piano", "harp", "STOP", "out", "END", "break"};
static int last_bar = -1, show_drums;
static char line[8192];
static void nname(int n, char *b) { sprintf(b, "%s%d", EN_NOTE_NAME[n % 12], n / 12 - 1); }
static void flush_bar(void)
{
    if (line[0])
        printf("%s\n", line);
    line[0] = 0;
}
static void newbar(int bar)
{
    if (bar != last_bar) {
        flush_bar();
        sprintf(line, "%3d |", bar);
        last_bar = bar;
    }
}
static void app(const char *t)
{
    if (strlen(line) + strlen(t) < sizeof line - 1)
        strcat(line, t);
}
static void dbg_note(int bar, int tick, int note, float vel, int role, int dur)
{
    char t[64], nn[24];
    if (role == R_DRUM && !show_drums)
        return;
    newbar(bar);
    if (role == R_DRUM) {
        sprintf(t, " D:%s@%d", note == 36 ? "k" : note == 37 ? "t" : note == 38 ? "S" : note == 44 ? "h" : "x", tick);
    } else {
        nname(note, nn);
        sprintf(t, " %s:%s@%d/%d", ROLE[role], nn, tick, dur);
    }
    (void)vel;
    app(t);
}
static void dbg_chord(int bar, int tick, int root, uint16_t pcs)
{
    char t[64];
    int p, k;
    newbar(bar);
    k = sprintf(t, " [%s@%d:", EN_NOTE_NAME[root], tick);
    for (p = 0; p < 12; p++)
        if (pcs >> p & 1u)
            k += sprintf(t + k, " %s", EN_NOTE_NAME[p]);
    sprintf(t + k, "]");
    app(t);
}
static void dbg_mark(int bar, int sec, int chorus, int b12)
{
    char t[64];
    newbar(bar);
    sprintf(t, " <%s ch%d b%d>", SEC[sec], chorus, b12 + 1);
    app(t);
}

static int dump(int st, uint32_t seed)
{
    stats_t a = {0};
    char t[32];
    en_dbg_note = dbg_note;
    en_dbg_chord = dbg_chord;
    en_dbg_mark = dbg_mark;
    en_init();
    style(st);
    en_set(P_LENGTH, 1);
    en_track(0, seed);
    en_play(1);
    run(0.05, &a);
    en_title(en_track_seed, en_kind, t);
    printf("# %s: '%s', %s in %s, %d bpm, %d choruses, harp %s\n", en_style_name(st), t, en_kind_name(en_kind),
           EN_NOTE_NAME[en_key], en_bpm, en_nchorus, en_harp_in ? "in" : "out");
    {
        uint32_t tp0 = en_tracks_played;
        while (en_tracks_played == tp0)
            run(1.0, &a);
    }
    flush_bar();
    printf("# peak %.1f dB, max voices %u, length %u s\n", db(a.peak), (unsigned)a.maxv, (unsigned)(en_track_len_ms / 1000u));
    return 0;
}

static void fname(int i, char *name)
{
    const char *sn = en_style_name(i);
    int j = 0, k;
    for (k = 0; sn[k] && j < 63; k++)
        if (sn[k] == ' ')
            name[j++] = '-';
        else if (sn[k] != 39 && sn[k] != ',')
            name[j++] = sn[k];
    name[j] = 0;
}
static int demos(const char *dir)
{
    char path[512], name[64];
    int i;
    stats_t a = {0};
    for (i = 0; i < EN_NSTYLES; i++) {
        en_init();
        style(i);
        en_track(0, 0x4A4B0000u + (uint32_t)i * 7919u);
        en_play(1);
        run(40.0, &a);                           /* past the count and the head's start */
        fname(i, name);
        snprintf(path, sizeof path, "%s/%02d-%s.wav", dir, i + 1, name);
        wav_open(path);
        run(20.0, &a);
        wav_close();
        printf("%s (%s)\n", path, en_kind_name(en_kind));
    }
    snprintf(path, sizeof path, "%s/00-listen.wav", dir);
    en_init();
    style(0);
    en_track(0, 0x4A4B4A54u);
    en_play(1);
    wav_open(path);
    run(180.0, &a);
    wav_close();
    printf("%s (peak %.1f dB)\n", path, db(a.peak));
    return 0;
}

int main(int argc, char **argv)
{
    int i;
    char t[32], u[32];
    if (argc > 3 && !strcmp(argv[1], "dump")) {
        show_drums = argc > 4 && atoi(argv[4]);
        return dump(atoi(argv[2]), (uint32_t)strtoul(argv[3], 0, 0));
    }
    if (argc > 2 && !strcmp(argv[1], "demos"))
        return demos(argv[2]);
    if (argc > 5 && !strcmp(argv[1], "wav")) {
        stats_t a = {0};
        en_init();
        style(atoi(argv[2]));
        en_track(0, (uint32_t)strtoul(argv[3], 0, 0));
        en_play(1);
        wav_open(argv[5]);
        run(atof(argv[4]), &a);
        wav_close();
        printf("peak %.1f dB rms %.1f dB\n", db(a.peak), db(sqrt(a.sum2 / a.n)));
        return 0;
    }
    /* the pure parts */
    for (i = 0; i < 5000; i++) {
        en_title(en_track_rand(1234u, (uint32_t)i), i % K_N, t);
        CHECK(strlen(t) > 3 && strlen(t) < 28, "title %d: '%s'", i, t);
    }
    en_title(42u, K_SLOW, t);
    en_title(42u, K_SLOW, u);
    CHECK(!strcmp(t, u), "the same seed, the same title");
    for (i = 0; i < EN_NSTYLES; i++) {
        int16_t p[P_NPARAMS];
        int k;
        en_style_recipe(i, p);
        for (k = 0; k < P_NSTYLE; k++)
            CHECK(p[k] >= en_param_info(k)->lo && p[k] <= en_param_info(k)->hi, "night %d param %d", i, k);
        CHECK(strlen(en_style_name(i)) <= 15, "night name %s too long", en_style_name(i));
    }
    /* every night: sound within seconds, bounded over a long run, tunes move on */
    for (i = 0; i < EN_NSTYLES; i++) {
        stats_t a = {0}, b = {0};
        uint32_t tr0;
        en_init();
        style(i);
        en_set(P_LENGTH, 0);
        en_track((uint32_t)i * 10u, 0xC0FFEEu + (uint32_t)i);
        en_play(1);
        run(60.0, &a);
        tr0 = en_tracks_played;
        run(480.0, &b);
        printf("%-16s first minute rms %5.1f dB peak %5.1f dB; next 8 min peak %5.1f dB, rms %5.1f dB, tunes %u, "
               "voices %u\n",
               en_style_name(i), db(sqrt(a.sum2 / a.n)), db(a.peak), db(b.peak), db(sqrt(b.sum2 / b.n)),
               (unsigned)(en_tracks_played - tr0), (unsigned)(a.maxv > b.maxv ? a.maxv : b.maxv));
        CHECK(a.nan == 0 && b.nan == 0, "%s: NaN", en_style_name(i));
        CHECK(db(sqrt(a.sum2 / a.n)) > -34.0, "%s: too quiet", en_style_name(i));
        CHECK(db(a.peak) < -1.0 && db(b.peak) < -1.0, "%s: peaks over -1 dBFS", en_style_name(i));
        CHECK(en_tracks_played - tr0 >= 2, "%s: tunes did not move on", en_style_name(i));
    }
    /* the extremes: everything up, then down */
    {
        stats_t a = {0};
        int k;
        en_init();
        style(2);
        for (k = 0; k < P_NSTYLE; k++)
            en_set(k, en_param_info(k)->hi);
        en_play(1);
        run(300.0, &a);
        printf("everything up: peak %.1f dB rms %.1f dB\n", db(a.peak), db(sqrt(a.sum2 / a.n)));
        CHECK(a.nan == 0 && db(a.peak) < -1.0, "everything up: peak %.1f", db(a.peak));
        for (k = 0; k < P_NSTYLE; k++)
            en_set(k, en_param_info(k)->lo);
        memset(&a, 0, sizeof a);
        run(120.0, &a);
        CHECK(a.nan == 0 && db(a.peak) < -1.0, "everything down");
        printf("everything down: peak %.1f dB\n", db(a.peak));
    }
    /* a skip: the next tune within a few seconds */
    {
        stats_t a = {0};
        uint32_t no;
        en_init();
        style(0);
        en_play(1);
        run(20.0, &a);
        no = en_track_no;
        en_skip();
        run(4.0, &a);
        CHECK(en_track_no == no + 1, "skip: tune %u -> %u", (unsigned)no, (unsigned)en_track_no);
    }
    /* resting: silence within a second or two; the keys still play */
    {
        stats_t a = {0}, b = {0};
        en_init();
        style(3);
        en_play(1);
        run(30.0, &a);
        en_play(0);
        run(3.0, &a);
        run(2.0, &b);
        CHECK(db(b.peak) < -70.0, "resting: %.1f dB", db(b.peak));
        en_note(60, 100);
        memset(&b, 0, sizeof b);
        run(0.5, &b);
        CHECK(db(b.peak) > -40.0, "the keys while resting: %.1f dB", db(b.peak));
        en_note(60, 0);
    }
    /* repeat: the tune goes on past its length */
    {
        stats_t a = {0};
        uint32_t no;
        en_init();
        style(2);
        en_set(P_LENGTH, 0);
        en_play(1);
        run(5.0, &a);
        no = en_track_no;
        en_repeat(1);
        run(400.0, &a);
        CHECK(en_track_no == no, "repeat: tune %u -> %u", (unsigned)no, (unsigned)en_track_no);
        en_repeat(0);
        run(240.0, &a);
        CHECK(en_track_no > no, "repeat off: on to the next");
    }
    /* the cost: the busiest nights, timed */
    {
        double best = 0.0;
        int k;
        for (k = 0; k < EN_NSTYLES; k++) {
            stats_t a = {0};
            double t0, ns;
            en_init();
            style(k);
            en_play(1);
            run(30.0, &a);
            t0 = now();
            run(120.0, &a);
            ns = (now() - t0) / (120.0 * 44100.0) * 1e9;
            if (ns > best)
                best = ns;
            printf("cost %-16s %.1f ns/frame\n", en_style_name(k), ns);
        }
        printf("busiest: %.1f ns/frame (AMB-1 Low Tide ~168)\n", best);
    }
    printf(fails ? "%d FAILED\n" : "all engine checks passed\n", fails);
    return fails ? 1 : 0;
}
