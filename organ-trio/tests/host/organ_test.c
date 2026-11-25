/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * ORGAN TRIO: ashtonwallen, 2026 */
/* The instrument on the host: the pure parts (titles, chord names, the bands' recipes) and the render: every style
 * makes sound, nothing is NaN, the peaks stay under -1 dBFS over long runs, tunes move on, a skip works, the keys
 * play along. Then the render's cost, in ns a frame (the CPU check: compare with AMB-1's busiest piece).
 *   organ_test                     the checks
 *   organ_test wav DIR             a minute of each style, for listening
 *   organ_test dump BAND TUNES     the tunes as a musician reads them: each bar's chords, the bass, the melody
 *   organ_test demo BAND SEC FILE [SKIP [SEED]]   SEC seconds of a band (after SKIP seconds; SEED in hex), as a WAV */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "organ.h"

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

/* ---- the dump: a bar a line */
static const char *const ROLE[] = {"count", "intro", "head", "o-solo", "g-solo", "trade-o", "trade-g", "head-out", "tag", "final"};
static const char *const NN[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
static char bass_s[400], mel_s[700], lh_s[500], gtr_s[700], gch_s[500], drm_s[400], ev_s[200];
static int cur_rec = -1, lh_tick = -1, gch_tick = -1;
static void flush_bar(void)
{
    if (cur_rec >= 0) {
        printf("    bass: %s\n", bass_s);
        if (mel_s[0])
            printf("    org:  %s\n", mel_s);
        if (lh_s[0])
            printf("    ochd: %s\n", lh_s);
        if (gtr_s[0])
            printf("    gtr:  %s\n", gtr_s);
        if (gch_s[0])
            printf("    gchd: %s\n", gch_s);
        printf("    drum: %s\n", drm_s);
        if (ev_s[0])
            printf("    ctl:  %s\n", ev_s);
    }
    bass_s[0] = mel_s[0] = lh_s[0] = gtr_s[0] = gch_s[0] = drm_s[0] = ev_s[0] = 0;
}
static void nm(char *d, size_t n, int note, int tick, int dur, int with_t)
{
    char x[40];
    if (with_t)
        snprintf(x, sizeof x, "%d.%d:%s%d/%d ", tick / 12 + 1, tick % 12, NN[note % 12], note / 12 - 1, dur);
    else
        snprintf(x, sizeof x, "%s%d ", NN[note % 12], note / 12 - 1);
    strncat(d, x, n - strlen(d) - 1);
}
static void tr(int inst, int rec, int role, int fbar, int tick, int dur, int note, int vel)
{
    static const char DR[] = "RHOSKTCBXbr";
    (void)role, (void)fbar, (void)rec;
    if (inst == 2)
        nm(bass_s, sizeof bass_s, note, tick, dur, 1);
    else if (inst == 0)
        nm(mel_s, sizeof mel_s, note, tick, dur, 1);
    else if (inst == 3)
        nm(gtr_s, sizeof gtr_s, note, tick, dur, 1);
    else if (inst == 1 || inst == 4) {
        char *d = inst == 1 ? lh_s : gch_s;
        size_t n = inst == 1 ? sizeof lh_s : sizeof gch_s;
        int *lt = inst == 1 ? &lh_tick : &gch_tick;
        if (tick != *lt) {
            char x[32];
            snprintf(x, sizeof x, "|%d.%d ", tick / 12 + 1, tick % 12);
            strncat(d, x, n - strlen(d) - 1);
        }
        *lt = tick;
        nm(d, n, note, tick, dur, 0);
    } else if (inst >= 5 && inst <= 15) {
        char x[32];
        snprintf(x, sizeof x, "%c%d%s ", DR[inst - 5], tick, vel < 40 ? "'" : "");
        strncat(drm_s, x, sizeof drm_s - strlen(drm_s) - 1);
    } else {
        char x[24];
        snprintf(x, sizeof x, "%s%d@%d ", inst == 16 ? "swell" : inst == 17 ? "spd" : "reg", note, tick);
        strncat(ev_s, x, sizeof ev_s - strlen(ev_s) - 1);
    }
}
static void tr_bar(int rec, int role, int fbar, int chorus, const char *chords)
{
    char t[32];
    flush_bar();
    if (rec == 0) {
        en_title(en_tune_seed, en_kind, t);
        printf("\n=== tune %u \"%s\": %s in %s%s, %u bpm, %u/4, form %u bars\n", (unsigned)en_tune_no, t,
               en_kind_name(en_kind), NN[en_key], en_minor ? " minor" : "", (unsigned)en_bpm, (unsigned)en_bpb,
               (unsigned)en_form_bars);
    }
    printf("%3d %-8s ch%d bar %2d  %s\n", rec, ROLE[role], chorus, fbar + 1, chords);
    cur_rec = rec;
    lh_tick = gch_tick = -1;
}

int main(int argc, char **argv)
{
    int i;
    char t[32], u[32];
    if (argc > 3 && !strcmp(argv[1], "dump")) {
        int band = atoi(argv[2]), n = atoi(argv[3]);
        stats_t a = {0};
        en_init();
        style(band);
        en_set(P_STRETCH, argc > 4 ? atoi(argv[4]) : 0);
        en_tune(0, 0x5A5A414Au + (uint32_t)band * 7919u);
        en_trace = tr;
        en_trace_bar = tr_bar;
        en_play(1);
        run(0.01, &a);
        n += (int)en_tunes_played;
        while (en_tunes_played < (uint32_t)n)
            run(1.0, &a);
        flush_bar();
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "balance")) {   /* each player alone (the others' faders down): its level */
        static const int P[3] = {P_ORGAN, P_GUITAR, P_DRUMS};
        static const char *const NM[3] = {"organ+bass", "guitar", "drums"};
        int band = atoi(argv[2]), k, j;
        for (k = 0; k < 3; k++) {
            stats_t a = {0};
            int16_t p[P_NPARAMS];
            en_init();
            style(band);
            en_style_recipe(band, p);
            for (j = 0; j < 3; j++)
                en_set(P[j], j == k ? p[P[j]] : 0);
            en_set(P_CROWD, 0);
            en_tune(0, 0x5A5A414Au + (uint32_t)band * 7919u);
            en_play(1);
            run(240.0, &a);
            printf("%s %-11s rms %5.1f dB peak %5.1f dB\n", en_style_name(band), NM[k], db(sqrt(a.sum2 / a.n)), db(a.peak));
        }
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "demo")) {
        stats_t a = {0};
        int band = atoi(argv[2]);
        double skip = argc > 5 ? atof(argv[5]) : 0.0;
        uint32_t seed = argc > 6 ? (uint32_t)strtoul(argv[6], 0, 16) : 0x5A5A414Au + (uint32_t)band * 7919u;
        en_init();
        style(band);
        en_tune(0, seed);
        en_play(1);
        if (skip > 0.0)
            run(skip, &a);
        wav_open(argv[4]);
        run(atof(argv[3]), &a);
        wav_close();
        printf("%s: peak %.1f dB rms %.1f dB\n", en_style_name(band), db(a.peak), db(sqrt(a.sum2 / a.n)));
        return 0;
    }
    /* the pure parts */
    for (i = 0; i < 4000; i++) {
        en_title(en_tune_rand(1234u, (uint32_t)i), i % K_NKINDS, t);
        CHECK(strlen(t) > 3 && strlen(t) <= 26, "title %d: '%s'", i, t);
    }
    en_title(42u, K_BLUES, t);
    en_title(42u, K_BLUES, u);
    CHECK(!strcmp(t, u), "the same seed, the same title");
    printf("chords: %s ", en_chord_name(2, 2, t));
    printf("%s ", en_chord_name(7, 4, t));
    printf("%s ", en_chord_name(0, 0, t));
    printf("%s\n", en_chord_name(11, 8, t));
    for (i = 0; i < EN_NSTYLES; i++) {
        int16_t p[P_NPARAMS];
        int k;
        en_style_recipe(i, p);
        for (k = 0; k < P_NSTYLE; k++)
            CHECK(p[k] >= en_param_info(k)->lo && p[k] <= en_param_info(k)->hi, "style %d param %d", i, k);
        CHECK(strlen(en_style_name(i)) <= 16, "style name %s too long", en_style_name(i));
    }
    /* every style: sound within seconds, bounded over a long run, tunes move on */
    for (i = 0; i < EN_NSTYLES; i++) {
        stats_t a = {0}, b = {0};
        uint32_t tr0;
        char path[256];
        en_init();
        style(i);
        en_set(P_STRETCH, 0);                    /* short solos: several tunes in the run */
        en_tune((uint32_t)i * 10u, 0xC0FFEEu + (uint32_t)i);
        en_play(1);
        if (argc > 2 && !strcmp(argv[1], "wav")) {
            snprintf(path, sizeof path, "%s/band%02d.wav", argv[2], i);
            wav_open(path);
        }
        run(60.0, &a);
        wav_close();
        tr0 = en_tunes_played;
        run(600.0, &b);
        printf("%-16s first minute rms %5.1f dB peak %5.1f dB; next 10 min peak %5.1f dB, rms %5.1f dB, tunes %u\n",
               en_style_name(i), db(sqrt(a.sum2 / a.n)), db(a.peak), db(b.peak), db(sqrt(b.sum2 / b.n)),
               (unsigned)(en_tunes_played - tr0));
        CHECK(a.nan == 0 && b.nan == 0, "%s: NaN", en_style_name(i));
        CHECK(db(sqrt(a.sum2 / a.n)) > -36.0, "%s: too quiet", en_style_name(i));
        CHECK(db(a.peak) < -1.0 && db(b.peak) < -1.0, "%s: peaks over -1 dBFS", en_style_name(i));
        CHECK(en_tunes_played - tr0 >= 2, "%s: tunes did not move on", en_style_name(i));
    }
    /* the extremes */
    {
        stats_t a = {0};
        int k;
        en_init();
        style(1);
        for (k = 0; k < P_NPARAMS; k++)
            en_set(k, en_param_info(k)->hi);
        en_play(1);
        run(600.0, &a);
        printf("everything up: peak %.1f dB rms %.1f dB\n", db(a.peak), db(sqrt(a.sum2 / a.n)));
        CHECK(a.nan == 0 && db(a.peak) < -1.0, "everything up: peak %.1f", db(a.peak));
        for (k = 0; k < P_NPARAMS; k++)
            en_set(k, en_param_info(k)->lo);
        memset(&a, 0, sizeof a);
        run(300.0, &a);
        printf("everything down: peak %.1f dB rms %.1f dB\n", db(a.peak), db(sqrt(a.sum2 / a.n)));
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
        run(6.0, &a);
        CHECK(en_tune_no == no + 1, "skip: tune %u -> %u", (unsigned)no, (unsigned)en_tune_no);
    }
    /* a break: silence; back: sound */
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
    /* keep blowing: the tune plays on */
    {
        stats_t a = {0};
        uint32_t no;
        en_init();
        style(0);
        en_set(P_STRETCH, 0);
        en_repeat(1);
        en_play(1);
        run(1.0, &a);
        no = en_tune_no;
        run(600.0, &a);
        CHECK(en_tune_no == no, "repeat: moved on to %u", (unsigned)en_tune_no);
        en_repeat(0);
        run(400.0, &a);
        CHECK(en_tune_no != no, "repeat off: stayed on %u", (unsigned)en_tune_no);
    }
    /* the cost: ns a frame, each band at full intensity */
    {
        static int32_t buf[512];
        double worst = 0.0;
        int wi = 0;
        for (i = 0; i < EN_NSTYLES; i++) {
            double t0, dt;
            int b;
            en_init();
            style(i);
            en_set(P_INTENS, 100);
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
    printf(fails ? "organ_test: %d FAILED\n" : "organ_test: all passed\n", fails);
    return fails != 0;
}
