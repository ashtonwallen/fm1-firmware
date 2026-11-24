/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * LOFI RADIO: ashtonwallen, 2026 */
/* The instrument on the host: the pure parts (titles, chord names, the styles' recipes) and the render: every
 * style makes sound, nothing is NaN, the peaks stay under -1 dBFS over long runs, tracks move on, a skip works,
 * the keys play along. Then the render's cost, in ns a frame (the CPU check: compare with AMB-1's busiest piece).
 *   lofi_test [WAVDIR]   (WAVDIR: a minute of each style, for listening) */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "lofi.h"

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
            wav_n++;
    }
    if (wav)
        wav_n = wav_n * 256u;
}
static double db(double x) { return 20.0 * log10(x + 1e-12); }

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    int i;
    char t[32], u[32];
    /* the pure parts */
    for (i = 0; i < 2000; i++) {
        en_title(en_track_rand(1234u, (uint32_t)i), t);
        CHECK(strlen(t) > 3 && strlen(t) < 24, "title %d: '%s'", i, t);
    }
    en_title(42u, t);
    en_title(42u, u);
    CHECK(!strcmp(t, u), "the same seed, the same title");
    CHECK(!strcmp(en_chord_name(2, 3, t), "Dm9"), "chord name: %s", t);
    for (i = 0; i < EN_NSTYLES; i++) {
        int16_t p[P_NPARAMS];
        int k;
        en_style_recipe(i, p);
        for (k = 0; k < P_NSTYLE; k++)
            CHECK(p[k] >= en_param_info(k)->lo && p[k] <= en_param_info(k)->hi, "style %d param %d", i, k);
        CHECK(strlen(en_style_name(i)) <= 12, "style name %s too long", en_style_name(i));
    }
    /* every style: sound within seconds, bounded over a long run, tracks move on */
    en_init();
    for (i = 0; i < EN_NSTYLES; i++) {
        stats_t a = {0}, b = {0};
        uint32_t tr0;
        char path[256];
        en_init();
        style(i);
        en_set(P_LENGTH, 0);                     /* two-minute tracks: several in the run */
        en_track((uint32_t)i * 10u, 0xC0FFEEu + (uint32_t)i);
        en_play(1);
        if (argc > 1) {
            snprintf(path, sizeof path, "%s/style%02d.wav", argv[1], i);
            wav_open(path);
        }
        run(60.0, &a);
        wav_close();
        tr0 = en_tracks_played;
        run(420.0, &b);
        printf("%-13s first minute rms %5.1f dB peak %5.1f dB; next 7 min peak %5.1f dB, rms %5.1f dB, tracks %u\n",
               en_style_name(i), db(sqrt(a.sum2 / a.n)), db(a.peak), db(b.peak), db(sqrt(b.sum2 / b.n)),
               (unsigned)(en_tracks_played - tr0));
        CHECK(a.nan == 0 && b.nan == 0, "%s: NaN", en_style_name(i));
        CHECK(db(sqrt(a.sum2 / a.n)) > -36.0, "%s: too quiet", en_style_name(i));
        CHECK(db(a.peak) < -1.0 && db(b.peak) < -1.0, "%s: peaks over -1 dBFS", en_style_name(i));
        CHECK(en_tracks_played - tr0 >= 2, "%s: tracks did not move on", en_style_name(i));
    }
    /* the extremes: everything up, dust and rain full, fastest and slowest */
    {
        stats_t a = {0};
        int k;
        en_init();
        style(5);
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
    /* a skip: the next track within a few seconds */
    {
        stats_t a = {0};
        uint32_t no;
        en_init();
        style(0);
        en_play(1);
        run(20.0, &a);
        no = en_track_no;
        en_skip();
        run(5.0, &a);
        CHECK(en_track_no == no + 1, "skip: track %u -> %u", (unsigned)no, (unsigned)en_track_no);
        CHECK(en_track_ms < 3000u, "skip: %u ms into the new one", (unsigned)en_track_ms);
    }
    /* the needle up: silence; down again: sound */
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
    /* repeat: the track plays on */
    {
        stats_t a = {0};
        uint32_t no;
        en_init();
        style(0);
        en_set(P_LENGTH, 0);
        en_repeat(1);
        en_play(1);
        no = en_track_no;
        run(400.0, &a);
        CHECK(en_track_no == no, "repeat: moved on to %u", (unsigned)en_track_no);
    }
    /* the guests and the extra kit: each shows up in some style, none louder than the keys it stands in for */
    {
        static const char *const PN[EN_NPART] = {"keys", "bass", "sub", "drums", "kit", "guitar", "vocal", "melody",
                                                 "trumpet"};
        static const char *const EVN[6] = {"sub", "brush", "click", "shaker", "guitar", "vocal"};
        double tot2[EN_NPART] = {0}, totn[EN_NPART] = {0};
        float pk[EN_NPART] = {0};
        uint32_t ev[16] = {0};
        int p;
        en_meas = 1;
        printf("parts, active rms dB (12 min a style):\n%-13s", "");
        for (p = 0; p < EN_NPART; p++)
            printf("%8s", PN[p]);
        printf("\n");
        for (i = 0; i < EN_NSTYLES; i++) {
            stats_t a = {0};
            en_init();
            style(i);
            en_track((uint32_t)i * 50u, 0xBEEF00u + (uint32_t)i);
            memset(en_part, 0, sizeof en_part);
            memset(en_fired, 0, sizeof en_fired);
            en_play(1);
            run(720.0, &a);
            CHECK(a.nan == 0 && db(a.peak) < -1.0, "%s guests: peak %.1f", en_style_name(i), db(a.peak));
            printf("%-13s", en_style_name(i));
            for (p = 0; p < EN_NPART; p++) {
                if (en_part[p].n)
                    printf("%8.1f", db(sqrt(en_part[p].sum2 / en_part[p].n)));
                else
                    printf("%8s", "-");
                tot2[p] += en_part[p].sum2;
                totn[p] += en_part[p].n;
                if (en_part[p].peak > pk[p])
                    pk[p] = en_part[p].peak;
            }
            printf("\n");
            for (p = 0; p < 16; p++)
                ev[p] += en_fired[p];
        }
        printf("%-13s", "all, peak");
        for (p = 0; p < EN_NPART; p++)
            printf("%8.1f", db(pk[p]));
        printf("\n%-13s", "all, rms");
        for (p = 0; p < EN_NPART; p++)
            printf("%8.1f", totn[p] > 0 ? db(sqrt(tot2[p] / totn[p])) : -999.0);
        printf("\nevents:");
        for (p = 0; p < 6; p++) {
            printf(" %s %u", EVN[p], (unsigned)ev[EN_EV_SUB + p]);
            CHECK(ev[EN_EV_SUB + p] > 0, "%s never played", EVN[p]);
        }
        printf("\n");
        for (p = EN_PART_SUB; p < EN_NPART; p++)
            CHECK(totn[p] > 0, "%s never sounded", PN[p]);
        {
            double keys = db(sqrt(tot2[EN_PART_KEYS] / totn[EN_PART_KEYS]));
            double bass = db(sqrt(tot2[EN_PART_BASS] / totn[EN_PART_BASS]));
            double mel = db(sqrt(tot2[EN_PART_MEL] / totn[EN_PART_MEL]));
            CHECK(db(sqrt(tot2[EN_PART_GTR] / totn[EN_PART_GTR])) < keys + 1.0, "the guitar louder than the keys");
            CHECK(db(sqrt(tot2[EN_PART_VOX] / totn[EN_PART_VOX])) < keys + 1.0, "the vocal louder than the keys");
            CHECK(db(sqrt(tot2[EN_PART_SUB] / totn[EN_PART_SUB])) < bass + 1.0, "the sub louder than the bass");
            CHECK(db(sqrt(tot2[EN_PART_TPT] / totn[EN_PART_TPT])) < mel + 1.5, "the trumpet louder than the lead");
            CHECK(db(sqrt(tot2[EN_PART_KIT] / totn[EN_PART_KIT])) < db(sqrt(tot2[EN_PART_DRUMS] / totn[EN_PART_DRUMS])),
                  "the extra kit louder than the drums");
        }
    }
    en_meas = 0;
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
    printf(fails ? "lofi_test: %d FAILED\n" : "lofi_test: all passed\n", fails);
    return fails != 0;
}
