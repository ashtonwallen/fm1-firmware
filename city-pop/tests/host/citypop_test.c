/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CITY POP: ashtonwallen, 2026 */
/* The instrument on the host: the pure parts (titles, chord names, the styles' recipes) and the render: every
 * style makes sound, nothing is NaN, the peaks stay under -1 dBFS over long runs, songs move on, a skip works,
 * the keys play along, the melody stays in a singer's range and on the chords. Then the render's cost, in ns a
 * frame (the CPU check: compare with AMB-1's busiest piece).
 *   citypop_test                       the checks
 *   citypop_test dump STYLE SONGS      what each song plays, bar by bar, to read as a musician would
 *   citypop_test wav STYLE SEC FROM OUT.wav   SEC seconds of a style from FROM seconds in (16-bit stereo) */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "citypop.h"

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

static void run(double sec, stats_t *s)
{
    static int32_t buf[512];
    uint32_t b, nb = (uint32_t)(sec * 44100.0 / 256.0), j;
    for (b = 0; b < nb; b++) {
        en_render(buf, 256, 4096);
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

/* ---- the dump: every note logged, then printed bar by bar */
static const char *const NN[12] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static char *nname(char *b, int n)
{
    sprintf(b, "%s%d", NN[n % 12], n / 12 - 1);
    return b;
}
typedef struct {
    int kind, bar, step, note, vel, dur;
} logn_t;
static logn_t lg[60000];
static int nlg;
static int log_on;
static uint32_t log_song;
static void logger(int kind, int bar, int step, int note, int vel, int dur)
{
    if (!log_on || en_tracks_played != log_song || nlg >= (int)(sizeof lg / sizeof lg[0]))
        return;
    lg[nlg].kind = kind, lg[nlg].bar = bar, lg[nlg].step = step, lg[nlg].note = note, lg[nlg].vel = vel,
    lg[nlg].dur = dur;
    nlg++;
}
static void pl(const char *s) { printf("%s\n", s); }
static void print_bar(int bar, int kind, const char *label)
{
    int i, any = 0;
    char t[16], u[16];
    for (i = 0; i < nlg; i++)
        if (lg[i].bar == bar && lg[i].kind == kind) {
            if (!any)
                printf("      %s", label);
            any = 1;
            if (kind == 'D')
                printf(" %d:%s", lg[i].step,
                       lg[i].note == 36 ? "K" : lg[i].note == 38 ? "S" : lg[i].note == 37 ? "r" : lg[i].note == 42 ? "h" :
                       lg[i].note == 46 ? "o" : lg[i].note == 45 ? "T" : "C");
            else
                printf(" %d.%d:%s%s", lg[i].step / 4 + 1, lg[i].step % 4, nname(t, lg[i].note),
                       kind == 'L' || kind == 'B' ? (sprintf(u, "/%d", lg[i].dur), u) : "");
        }
    if (any)
        printf("\n");
}
static void dump(int st, int songs, int all)
{
    int k, bar;
    en_init();
    style(st);
    en_log = logger;
    for (k = 0; k < songs; k++) {
        stats_t s = {0};
        char title[32];
        uint32_t no = en_tracks_played;
        en_track((uint32_t)k * 7u, 0x5EED0000u + (uint32_t)st * 131u + (uint32_t)k);
        en_play(1);
        run(0.01, &s);
        en_title(en_track_seed, title);
        printf("\n==== %s, song %d: \"%s\"\n", en_style_name(st), k + 1, title);
        en_song_dump(pl);
        nlg = 0;
        log_on = 1;
        log_song = en_tracks_played;
        while (en_tracks_played == no + 1 && en_track_ms < 400000u)
            run(1.0, &s);
        log_on = 0;
        printf("---- notes (beat.16th:note/len)\n");
        for (bar = 0; bar < 100; bar++) {
            int i, has = 0;
            for (i = 0; i < nlg; i++)
                has |= lg[i].bar == bar;
            if (!has)
                continue;
            printf("bar %d\n", bar);
            print_bar(bar, 'L', "lead   ");
            print_bar(bar, 'B', "bass   ");
            if (all) {
                print_bar(bar, 'K', "keys   ");
                print_bar(bar, 'G', "guitar ");
                print_bar(bar, 'H', "brass  ");
                print_bar(bar, 'D', "drums  ");
            }
        }
        en_play(0);
        run(0.05, &s);
    }
    en_log = 0;
}

int main(int argc, char **argv)
{
    int i;
    char t[32], u[32];
    if (argc >= 4 && !strcmp(argv[1], "dump")) {
        dump(atoi(argv[2]), atoi(argv[3]), argc > 4);
        return 0;
    }
    if (argc >= 6 && !strcmp(argv[1], "wav")) {   /* wav STYLE SEC FROM OUT */
        stats_t s = {0};
        en_init();
        style(atoi(argv[2]));
        en_track(0, 0x43495459u);
        en_play(1);
        run(atof(argv[4]), &s);
        wav_open(argv[5]);
        memset(&s, 0, sizeof s);
        run(atof(argv[3]), &s);
        wav_close();
        printf("%s: peak %.1f dB rms %.1f dB\n", argv[5], db(s.peak), db(sqrt(s.sum2 / s.n)));
        return 0;
    }
    /* the pure parts */
    for (i = 0; i < 3000; i++) {
        en_title(en_track_rand(1234u, (uint32_t)i), t);
        CHECK(strlen(t) > 3 && strlen(t) < 24, "title %d: '%s'", i, t);
    }
    en_title(42u, t);
    en_title(42u, u);
    CHECK(!strcmp(t, u), "the same seed, the same title");
    CHECK(!strcmp(en_chord_name(2, 3, -1, t), "Dm9"), "chord name: %s", t);
    CHECK(!strcmp(en_chord_name(5, 0, 7, t), "Fmaj7/G"), "slash chord name: %s", t);
    for (i = 0; i < EN_NSTYLES; i++) {
        int16_t p[P_NPARAMS];
        int k;
        en_style_recipe(i, p);
        for (k = 0; k < P_NSTYLE; k++)
            CHECK(p[k] >= en_param_info(k)->lo && p[k] <= en_param_info(k)->hi, "style %d param %d", i, k);
        CHECK(strlen(en_style_name(i)) <= 16, "style name %s too long", en_style_name(i));
    }
    /* the melody: in a singer's range, every strong note a chord tone (the dump's job is the ear's) */
    {
        int st, k, lo = 200, hi = 0;
        for (st = 0; st < EN_NSTYLES; st++) {
            en_init();
            style(st);
            en_log = logger;
            for (k = 0; k < 3; k++) {
                stats_t s = {0};
                uint32_t no;
                int j;
                nlg = 0;
                en_track((uint32_t)k * 3u, 0xABC00u + (uint32_t)st * 17u + (uint32_t)k);
                en_play(1);
                run(0.01, &s);
                no = en_tracks_played;
                log_song = no;
                log_on = 1;
                while (en_tracks_played == no && en_track_ms < 400000u)
                    run(2.0, &s);
                log_on = 0;
                for (j = 0; j < nlg; j++)
                    if (lg[j].kind == 'L') {
                        lo = lg[j].note < lo ? lg[j].note : lo;
                        hi = lg[j].note > hi ? lg[j].note : hi;
                    }
            }
            en_log = 0;
        }
        printf("melody range: %d .. %d\n", lo, hi);
        CHECK(lo >= 53 && hi <= 88, "melody out of range: %d .. %d", lo, hi);
    }
    /* every style: sound within seconds, bounded over a long run, songs move on */
    for (i = 0; i < EN_NSTYLES; i++) {
        stats_t a = {0}, b = {0};
        uint32_t tr0;
        char path[256];
        en_init();
        style(i);
        en_track((uint32_t)i * 10u, 0xC0FFEEu + (uint32_t)i);
        en_play(1);
        if (argc > 1) {
            snprintf(path, sizeof path, "%s/style%02d.wav", argv[1], i);
            wav_open(path);
        }
        run(60.0, &a);
        wav_close();
        tr0 = en_tracks_played;
        run(480.0, &b);
        printf("%-16s first minute rms %5.1f dB peak %5.1f dB; next 8 min peak %5.1f dB, rms %5.1f dB, songs %u\n",
               en_style_name(i), db(sqrt(a.sum2 / a.n)), db(a.peak), db(b.peak), db(sqrt(b.sum2 / b.n)),
               (unsigned)(en_tracks_played - tr0));
        CHECK(a.nan == 0 && b.nan == 0, "%s: NaN", en_style_name(i));
        CHECK(db(sqrt(a.sum2 / a.n)) > -30.0, "%s: too quiet", en_style_name(i));
        CHECK(db(a.peak) < -1.0 && db(b.peak) < -1.0, "%s: peaks over -1 dBFS", en_style_name(i));
        CHECK(en_tracks_played - tr0 >= 2, "%s: songs did not move on", en_style_name(i));
    }
    /* the extremes: everything up, then down */
    {
        stats_t a = {0};
        int k;
        en_init();
        style(4);
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
    /* a skip: the next song within a few seconds */
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
        CHECK(en_track_no == no + 1, "skip: song %u -> %u", (unsigned)no, (unsigned)en_track_no);
        CHECK(en_track_ms < 3000u, "skip: %u ms into the new one", (unsigned)en_track_ms);
    }
    /* paused: silence; again: sound */
    {
        stats_t a = {0}, b = {0};
        en_init();
        style(0);
        en_play(1);
        run(10.0, &a);
        en_play(0);
        run(4.0, &a);
        run(2.0, &b);
        CHECK(db(b.peak) < -70.0, "paused: peak %.1f dB", db(b.peak));
        en_play(1);
        memset(&b, 0, sizeof b);
        run(8.0, &b);
        CHECK(db(sqrt(b.sum2 / b.n)) > -40.0, "resumed: rms %.1f", db(sqrt(b.sum2 / b.n)));
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
    /* repeat: the song plays on */
    {
        stats_t a = {0};
        uint32_t no;
        en_init();
        style(0);
        en_repeat(1);
        en_play(1);
        run(0.1, &a);
        no = en_track_no;
        run(500.0, &a);
        CHECK(en_track_no == no, "repeat: moved on to %u", (unsigned)en_track_no);
    }
    /* the cost: ns a frame, each style */
    {
        static int32_t buf[512];
        double worst = 0.0;
        int wi = 0;
        for (i = 0; i < EN_NSTYLES; i++) {
            double t0, dt;
            int b;
            en_init();
            style(i);
            en_play(1);
            for (b = 0; b < 2000; b++)
                en_render(buf, 256, 4096);
            t0 = now();
            for (b = 0; b < 10000; b++)
                en_render(buf, 256, 4096);
            dt = (now() - t0) / (10000.0 * 256.0) * 1e9;
            printf("render %-16s %.1f ns/frame\n", en_style_name(i), dt);
            if (dt > worst)
                worst = dt, wi = i;
        }
        printf("render: busiest %s %.1f ns/frame\n", en_style_name(wi), worst);
    }
    printf(fails ? "citypop_test: %d FAILED\n" : "citypop_test: all passed\n", fails);
    return fails != 0;
}
