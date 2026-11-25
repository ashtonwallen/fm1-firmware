/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * BOSSA LOUNGE: ashtonwallen, 2026 */
/* The instrument on the host: the pure parts (titles, chord names, the styles' recipes) and the render: every
 * style makes sound, nothing is NaN, the peaks stay under -1 dBFS over long runs, tunes move on, a skip works,
 * the keys play along; the melody keeps to its rules (chord tones on the strong beats, mostly steps). Then the
 * render's cost, in ns a frame (the CPU check: compare with AMB-1 Drift's busiest piece).
 *   bossa_test                 the checks
 *   bossa_test dump STYLE N    what N tunes of a style play, bar by bar (chords, bass, melody), to read
 *   bossa_test wav DIR         a minute of each style, for listening */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "bossa.h"

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

/* ---- the ear: every note as scheduled */
#define NREC 200000
static struct {
    int type, bar, step, note, dur;
    float vel;
    uint8_t root, q, part, letter;
    uint32_t tune;
} rec[NREC];
static int nrec;
static void ear(int type, int bar, int step, int note, float vel, int dur16)
{
    if (nrec < NREC) {
        rec[nrec].type = type;
        rec[nrec].bar = bar;
        rec[nrec].step = step;
        rec[nrec].note = note;
        rec[nrec].vel = vel;
        rec[nrec].dur = dur16;
        rec[nrec].root = en_chord_root;
        rec[nrec].q = en_chord_q;
        rec[nrec].part = en_part;
        rec[nrec].letter = en_letter;
        rec[nrec].tune = en_tune_no;
        nrec++;
    }
}
static const char *nn(int n, char *b)
{
    sprintf(b, "%s%d", EN_NOTE_NAME[n % 12], n / 12 - 1);
    return b;
}

/* bar by bar: the chord(s), the bass, the melody (beat.eighth), for a reader */
static void dump(int st, int ntunes)
{
    static const char *const PART[] = {"intro", "head", "solo", "out", "end", "gap"};
    int i, tune0, bar = -1;
    uint32_t t;
    char a[16], b[16];
    en_init();
    style(st);
    en_set(P_LENGTH, 1);
    en_tune(0, 0x5EED0000u + (uint32_t)st);
    en_play(1);
    nrec = 0;
    en_dbg = ear;
    t = en_tunes_played;
    tune0 = -1;
    while (en_tunes_played - t < (uint32_t)ntunes + 1u && nrec < NREC - 100) {
        stats_t s = {0};
        run(1.0, &s);
    }
    en_dbg = 0;
    printf("== %s\n", en_style_name(st));
    for (i = 0; i < nrec; i++) {
        if ((int)rec[i].tune != tune0) {
            char ti[32];
            tune0 = (int)rec[i].tune;
            en_title(en_tune_rand(0x5EED0000u + (uint32_t)st, rec[i].tune), ti);
            printf("\n--- tune %d \"%s\"\n", tune0, ti);
            bar = -1;
        }
        if (rec[i].bar != bar) {
            int k;
            bar = rec[i].bar;
            printf("\n%3d %-5s %c ", bar, PART[rec[i].part], rec[i].letter ? rec[i].letter : ' ');
            /* the chords of this bar: from the guitar's notes' moments */
            for (k = i; k < nrec && rec[k].bar == bar && (int)rec[k].tune == tune0; k++)
                if (rec[k].type == 0 && (rec[k].step == 0 || rec[k].step == 8))
                    printf("[%s]", en_chord_name(rec[k].root, rec[k].q, a));
            printf("\n      bass:");
            for (k = i; k < nrec && rec[k].bar == bar && (int)rec[k].tune == tune0; k++)
                if (rec[k].type == 1)
                    printf(" %d.%d:%s", rec[k].step / 4 + 1, rec[k].step % 4 / 2, nn(rec[k].note, b));
            printf("\n      mel :");
            for (k = i; k < nrec && rec[k].bar == bar && (int)rec[k].tune == tune0; k++)
                if (rec[k].type == 2)
                    printf(" %d.%d:%s/%d", rec[k].step / 4 + 1, rec[k].step % 4 / 2, nn(rec[k].note, b), rec[k].dur / 2);
        }
    }
    printf("\n");
}

/* the melody's rules, counted over many tunes: on beats 1 and 3, a chord tone (or a tension); mostly steps */
static void melody_rules(void)
{
    int st, i, strong = 0, strong_ok = 0, moves = 0, steps = 0, leaps = 0, notes = 0;
    for (st = 0; st < EN_NSTYLES; st++) {
        uint32_t t;
        en_init();
        style(st);
        en_set(P_LENGTH, 0);
        en_tune(0, 0xABC00u + (uint32_t)st);
        en_play(1);
        nrec = 0;
        en_dbg = ear;
        t = en_tunes_played;
        while (en_tunes_played - t < 3u && nrec < NREC - 100) {
            stats_t s = {0};
            run(2.0, &s);
        }
        en_dbg = 0;
        {
            int prev = -1;
            uint32_t tune = 0;
            for (i = 0; i < nrec; i++) {
                int pc, rel;
                if (rec[i].type != 2)
                    continue;
                if (rec[i].tune != tune)
                    prev = -1, tune = rec[i].tune;
                notes++;
                pc = rec[i].note % 12;
                rel = (pc - rec[i].root + 12) % 12;
                if (rec[i].step % 8 == 0 && rec[i].part != 4) {
                    strong++;
                    /* chord tones: 1 3 5 7 of any quality, 9, 6/13, and 11 on minor chords, b9 and b13 on 7b9 */
                    if (rel != 5 || rec[i].q == 4 || rec[i].q == 5 || rec[i].q == 12)
                        strong_ok += rel != 1 || rec[i].q == 11;
                }
                if (prev >= 0) {
                    int d = abs(rec[i].note - prev);
                    moves++;
                    steps += d <= 2;
                    leaps += d > 7;
                }
                prev = rec[i].note;
            }
        }
    }
    printf("melody: %d notes; on 1 and 3, %d of %d stable; moves: %.0f%% steps, %.1f%% over a fifth\n", notes,
           strong_ok, strong, 100.0 * steps / (moves ? moves : 1), 100.0 * leaps / (moves ? moves : 1));
    CHECK(notes > 1000, "melody: too few notes (%d)", notes);
    CHECK(strong_ok > strong * 95 / 100, "melody: strong beats off the chord (%d of %d)", strong - strong_ok, strong);
    CHECK(steps * 100 > moves * 45, "melody: too few steps (%d of %d)", steps, moves);
    CHECK(leaps * 100 < moves * 4, "melody: too many leaps (%d of %d)", leaps, moves);
}

int main(int argc, char **argv)
{
    int i;
    char t[32], u[32];
    if (argc > 3 && !strcmp(argv[1], "dump")) {
        dump(atoi(argv[2]), atoi(argv[3]));
        return 0;
    }
    /* the pure parts */
    for (i = 0; i < 2000; i++) {
        en_title(en_tune_rand(1234u, (uint32_t)i), t);
        CHECK(strlen(t) > 3 && strlen(t) < 26, "title %d: '%s'", i, t);
    }
    en_title(42u, t);
    en_title(42u, u);
    CHECK(!strcmp(t, u), "the same seed, the same title");
    CHECK(!strcmp(en_chord_name(2, 4, t), "Dm7"), "chord name: %s", t);
    for (i = 0; i < EN_NSTYLES; i++) {
        int16_t p[P_NPARAMS];
        int k;
        en_style_recipe(i, p);
        for (k = 0; k < P_NSTYLE; k++)
            CHECK(p[k] >= en_param_info(k)->lo && p[k] <= en_param_info(k)->hi, "style %d param %d", i, k);
        CHECK(strlen(en_style_name(i)) <= 16, "style name %s too long", en_style_name(i));
    }
    CHECK(en_check_forms() == 0, "forms");
    melody_rules();
    /* every style: sound within seconds, bounded over a long run, tunes move on */
    for (i = 0; i < EN_NSTYLES; i++) {
        stats_t a = {0}, b = {0};
        uint32_t tr0;
        char path[256];
        en_init();
        style(i);
        en_set(P_LENGTH, 0);                     /* two choruses: several tunes in the run */
        en_tune((uint32_t)i * 10u, 0xC0FFEEu + (uint32_t)i);
        en_play(1);
        if (argc > 2 && !strcmp(argv[1], "wav")) {
            snprintf(path, sizeof path, "%s/style%02d.wav", argv[2], i);
            wav_open(path);
        }
        run(60.0, &a);
        wav_close();
        tr0 = en_tunes_played;
        run(600.0, &b);
        printf("%-17s first minute rms %5.1f dB peak %5.1f dB; next 10 min peak %5.1f dB, rms %5.1f dB, tunes %u\n",
               en_style_name(i), db(sqrt(a.sum2 / a.n)), db(a.peak), db(b.peak), db(sqrt(b.sum2 / b.n)),
               (unsigned)(en_tunes_played - tr0));
        CHECK(a.nan == 0 && b.nan == 0, "%s: NaN", en_style_name(i));
        CHECK(db(sqrt(a.sum2 / a.n)) > -36.0, "%s: too quiet", en_style_name(i));
        CHECK(db(a.peak) < -1.0 && db(b.peak) < -1.0, "%s: peaks over -1 dBFS", en_style_name(i));
        CHECK(en_tunes_played - tr0 >= 2, "%s: tunes did not move on", en_style_name(i));
    }
    /* the extremes: everything up, then everything down */
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
        run(120.0, &a);
        CHECK(a.nan == 0 && db(a.peak) < -1.0, "everything down");
    }
    /* a skip: the next tune within a few seconds */
    {
        stats_t a = {0};
        uint32_t no;
        en_init();
        style(0);
        en_play(1);
        run(20.0, &a);
        no = en_tune_no;
        en_skip();
        run(5.0, &a);
        CHECK(en_tune_no == no + 1, "skip: tune %u -> %u", (unsigned)no, (unsigned)en_tune_no);
        CHECK(en_tune_ms < 3000u, "skip: %u ms into the new one", (unsigned)en_tune_ms);
    }
    /* stopped: silence; rolling again: sound */
    {
        stats_t a = {0}, b = {0};
        en_init();
        style(0);
        en_play(1);
        run(10.0, &a);
        en_play(0);
        run(5.0, &a);
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
        en_note(72, 100);
        run(0.5, &a);
        en_note(72, 0);
        CHECK(db(a.peak) > -40.0, "a key: peak %.1f dB", db(a.peak));
    }
    /* repeat: the tune plays on */
    {
        stats_t a = {0};
        uint32_t no;
        en_init();
        style(0);
        en_set(P_LENGTH, 0);
        en_repeat(1);
        en_play(1);
        run(1.0, &a);
        no = en_tune_no;
        run(600.0, &a);
        CHECK(en_tune_no == no, "repeat: moved on to %u", (unsigned)en_tune_no);
    }
    /* the cost: ns a frame, each style, Melody full (as AMB-1 Drift's: 2000 blocks to settle, 10000 timed) */
    {
        static int32_t buf[512];
        double worst = 0.0;
        int wi = 0;
        for (i = 0; i < EN_NSTYLES; i++) {
            double t0, dt;
            int b;
            en_init();
            style(i);
            en_set(P_MELODY, 100);
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
    printf(fails ? "bossa_test: %d FAILED\n" : "bossa_test: all passed\n", fails);
    return fails != 0;
}
