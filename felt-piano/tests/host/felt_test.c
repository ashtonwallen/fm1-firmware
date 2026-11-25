/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * FELT PIANO: ashtonwallen, 2026 */
/* The instrument on the host: the pure parts (titles, the styles' recipes) and the render: every style makes sound,
 * nothing is NaN, the peaks stay under -1 dBFS over long runs, pieces move on, a skip works, resting is silent, the
 * keys play along, repeat holds. Then the render's cost, in ns a frame (the CPU check: compare with AMB-1's busiest
 * piece), with the pedal down and everything ringing.
 *   felt_test                       the checks
 *   felt_test dump STYLE SEED BARS  what a piece plays, bar by bar (chords, bass, chords, melody), to read
 *   felt_test demos DIR             the demo clips: NN-<style>.wav (20 s, settled) and 00-listen.wav (3 min) */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "felt.h"

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

static void run(double sec, stats_t *s)
{
    static int32_t buf[512];
    uint32_t b, nb = (uint32_t)(sec * 44100.0 / 256.0), j;
    for (b = 0; b < nb; b++) {
        en_render(buf, 256, 4096);
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
static const char *const ROLE[] = {"B", "C", "a", "M", "L", "P"};
static int last_bar = -1;
static char line[4096];
static void nname(int n, char *b) { sprintf(b, "%s%d", EN_NOTE_NAME[n % 12], n / 12 - 1); }
static void flush_bar(void)
{
    if (line[0])
        printf("%s\n", line);
    line[0] = 0;
}
static void dbg_note(int bar, int tick, int note, float vel, int role, int dur)
{
    char t[64], nn[24];
    if (bar != last_bar) {
        flush_bar();
        sprintf(line, "bar %3d |", bar);
        last_bar = bar;
    }
    nname(note, nn);
    sprintf(t, " %s:%s@%d/%d(%.2f)", ROLE[role], nn, tick, dur, (double)vel);
    if (strlen(line) + strlen(t) < sizeof line - 1)
        strcat(line, t);
}
static void dbg_chord(int bar, int tick, int root, uint16_t pcs)
{
    char t[64];
    int p, k = 0;
    if (bar != last_bar) {
        flush_bar();
        sprintf(line, "bar %3d |", bar);
        last_bar = bar;
    }
    k = sprintf(t, " [%s@%d:", EN_NOTE_NAME[root], tick);
    for (p = 0; p < 12; p++)
        if (pcs >> p & 1u)
            k += sprintf(t + k, " %s", EN_NOTE_NAME[p]);
    sprintf(t + k, "]");
    strcat(line, t);
}

static int dump(int st, uint32_t seed, int bars)
{
    stats_t a = {0};
    char t[32];
    en_dbg_note = dbg_note;
    en_dbg_chord = dbg_chord;
    en_init();
    style(st);
    en_set(P_LENGTH, 1);
    en_track(0, seed);
    en_play(1);
    run(0.05, &a);
    en_title(en_track_seed, en_idiom, t);
    printf("# %s: '%s', %s in %s %s, %d/%d, %d bpm\n", en_style_name(st), t, en_idiom_name(en_idiom),
           EN_NOTE_NAME[en_key], en_mode_name(en_mode), en_compound ? 12 : en_meter, en_compound ? 8 : 4, en_bpm);
    {
        uint32_t tp0 = en_tracks_played;
        while (last_bar < bars && en_tracks_played == tp0)
            run(1.0, &a);
    }
    if (0)
        run(1.0, &a);
    flush_bar();
    printf("# peak %.1f dB, max voices %u, length %u s\n", db(a.peak), (unsigned)a.maxv, (unsigned)(en_track_len_ms / 1000u));
    return 0;
}

static int demos(const char *dir)
{
    char path[512], name[64];
    int i, k;
    stats_t a = {0};
    for (i = 0; i < EN_NSTYLES; i++) {
        en_init();
        style(i);
        en_track(0, 0xFE170000u + (uint32_t)i * 7919u);
        en_play(1);
        run(i == 1 || i == 3 || i == 12 ? 40.0 : 28.0, &a);   /* past the opening (the loops take longer to gather) */
        {                                        /* a file name: spaces to dashes, no punctuation */
            const char *sn = en_style_name(i);
            int j = 0;
            for (k = 0; sn[k] && j < 63; k++)
                if (sn[k] == ' ')
                    name[j++] = '-';
                else if (sn[k] != 39 && sn[k] != ',')   /* (39: the apostrophe) */
                    name[j++] = sn[k];
            name[j] = 0;
        }
        snprintf(path, sizeof path, "%s/%02d-%s.wav", dir, i + 1, name);
        wav_open(path);
        run(20.0, &a);
        wav_close();
        printf("%s\n", path);
    }
    en_init();
    style(0);
    en_track(0, 0x46454C54u);
    en_play(1);
    snprintf(path, sizeof path, "%s/00-listen.wav", dir);
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
    if (argc > 4 && !strcmp(argv[1], "dump"))
        return dump(atoi(argv[2]), (uint32_t)strtoul(argv[3], 0, 0), atoi(argv[4]));
    if (argc > 2 && !strcmp(argv[1], "demos"))
        return demos(argv[2]);
    /* the pure parts */
    for (i = 0; i < 3000; i++) {
        en_title(en_track_rand(1234u, (uint32_t)i), i % ID_N, t);
        CHECK(strlen(t) > 3 && strlen(t) < 28, "title %d: '%s'", i, t);
    }
    en_title(42u, ID_GYM, t);
    en_title(42u, ID_GYM, u);
    CHECK(!strcmp(t, u), "the same seed, the same title");
    for (i = 0; i < EN_NSTYLES; i++) {
        int16_t p[P_NPARAMS];
        int k;
        en_style_recipe(i, p);
        for (k = 0; k < P_NSTYLE; k++)
            CHECK(p[k] >= en_param_info(k)->lo && p[k] <= en_param_info(k)->hi, "style %d param %d", i, k);
        CHECK(strlen(en_style_name(i)) <= 14, "style name %s too long", en_style_name(i));
    }
    /* every style: sound within seconds, bounded over a long run, pieces move on */
    for (i = 0; i < EN_NSTYLES; i++) {
        stats_t a = {0}, b = {0};
        uint32_t tr0;
        en_init();
        style(i);
        en_set(P_LENGTH, 0);                     /* two-minute pieces: several in the run */
        en_track((uint32_t)i * 10u, 0xC0FFEEu + (uint32_t)i);
        en_play(1);
        run(60.0, &a);
        tr0 = en_tracks_played;
        run(480.0, &b);
        printf("%-14s first minute rms %5.1f dB peak %5.1f dB; next 8 min peak %5.1f dB, rms %5.1f dB, pieces %u, "
               "voices %u\n",
               en_style_name(i), db(sqrt(a.sum2 / a.n)), db(a.peak), db(b.peak), db(sqrt(b.sum2 / b.n)),
               (unsigned)(en_tracks_played - tr0), (unsigned)(a.maxv > b.maxv ? a.maxv : b.maxv));
        CHECK(a.nan == 0 && b.nan == 0, "%s: NaN", en_style_name(i));
        CHECK(db(sqrt(a.sum2 / a.n)) > -36.0, "%s: too quiet", en_style_name(i));
        CHECK(db(a.peak) < -1.0 && db(b.peak) < -1.0, "%s: peaks over -1 dBFS", en_style_name(i));
        CHECK(en_tracks_played - tr0 >= 2, "%s: pieces did not move on", en_style_name(i));
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
        printf("everything up: peak %.1f dB rms %.1f dB voices %u\n", db(a.peak), db(sqrt(a.sum2 / a.n)), a.maxv);
        CHECK(a.nan == 0 && db(a.peak) < -1.0, "everything up: peak %.1f", db(a.peak));
        for (k = 0; k < P_NSTYLE; k++)
            en_set(k, en_param_info(k)->lo);
        memset(&a, 0, sizeof a);
        run(120.0, &a);
        CHECK(a.nan == 0 && db(a.peak) < -1.0, "everything down");
        CHECK(db(a.peak) > -50.0, "everything down still plays: %.1f", db(a.peak));
    }
    /* a skip: the next piece within a few seconds */
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
        CHECK(en_track_no == no + 1, "skip: piece %u -> %u", (unsigned)no, (unsigned)en_track_no);
        CHECK(en_track_ms < 3000u, "skip: %u ms into the new one", (unsigned)en_track_ms);
    }
    /* resting: silence; playing again: sound */
    {
        stats_t a = {0}, b = {0};
        en_init();
        style(2);
        en_play(1);
        run(15.0, &a);
        en_play(0);
        run(4.0, &a);
        run(2.0, &b);
        CHECK(db(b.peak) < -70.0, "rested: peak %.1f dB", db(b.peak));
        en_play(1);
        memset(&b, 0, sizeof b);
        run(8.0, &b);
        CHECK(db(sqrt(b.sum2 / b.n)) > -45.0, "resumed: rms %.1f", db(sqrt(b.sum2 / b.n)));
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
    /* repeat: the piece plays on */
    {
        stats_t a = {0};
        uint32_t no;
        int s;
        for (s = 0; s < EN_NSTYLES; s += 3) {
            en_init();
            style(s);
            en_set(P_LENGTH, 0);
            en_repeat(1);
            en_play(1);
            no = en_track_no;
            run(400.0, &a);
            CHECK(en_track_no == no, "repeat (%s): moved on to %u", en_style_name(s), (unsigned)en_track_no);
            CHECK(a.nan == 0, "repeat NaN");
        }
    }
    /* the cost: ns a frame, each style as it is, then each with the pedal held and Density full */
    {
        static int32_t buf[512];
        double worst = 0.0;
        int wi = 0, pass;
        for (pass = 0; pass < 2; pass++)
            for (i = 0; i < EN_NSTYLES; i++) {
                double t0, dt;
                int b, mv = 0;
                en_init();
                style(i);
                en_track(0, 0xBEEF00u + (uint32_t)i);
                if (pass) {
                    en_set(P_PEDAL, 100);
                    en_set(P_DENSITY, 100);
                }
                en_play(1);
                for (b = 0; b < 4000; b++)
                    en_render(buf, 256, 4096);
                t0 = now();
                for (b = 0; b < 12000; b++) {
                    en_render(buf, 256, 4096);
                    mv = en_voices > mv ? en_voices : mv;
                }
                dt = (now() - t0) / (12000.0 * 256.0) * 1e9;
                if (pass)
                    printf("  %-14s pedal held, dense: %.1f ns/frame, %d voices at most\n", en_style_name(i), dt, mv);
                else
                    printf("  %-14s %.1f ns/frame, %d voices at most\n", en_style_name(i), dt, mv);
                if (dt > worst)
                    worst = dt, wi = i + pass * 100;
            }
        printf("render: busiest %s%s %.1f ns/frame\n", en_style_name(wi % 100), wi >= 100 ? " (pedal held, dense)" : "",
               worst);
        /* all sixteen voices ringing at once (the worst moment): ns a frame */
        {
            double t0, dt;
            int b, n;
            en_init();
            en_play(0);
            for (b = 0; b < 200; b++)
                en_render(buf, 256, 4096);
            for (n = 0; n < 16; n++)
                en_note(36 + n * 3, 90);
            t0 = now();
            for (b = 0; b < 400; b++)
                en_render(buf, 256, 4096);
            dt = (now() - t0) / (400.0 * 256.0) * 1e9;
            printf("render: sixteen voices held %.1f ns/frame\n", dt);
        }
    }
    printf(fails ? "felt_test: %d FAILED\n" : "felt_test: all passed\n", fails);
    return fails != 0;
}
