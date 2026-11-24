/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen */
/* DECAY LOOPS' engine on its own (app/dsp/decay.c): mu-law, every preset (it makes sound, records loops, never
 * clips), a long fast-wearing run (loops come and go, the tape never overflows, peaks stay in bounds), the keys.
 *   dl_test                  the checks
 *   dl_test wav N SECS FILE  preset N for SECS seconds into a WAV (16-bit stereo), with a line of stats a second
 *   dl_test bench SECS       the render's time for SECS seconds of the busiest preset */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "decay.h"

#define GAIN 1024u                              /* MASTER at half: APP_GAIN(2048) */
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void preset(int i)
{
    int16_t p[P_NPARAMS];
    int k;
    en_init();
    en_preset(i, p);
    for (k = 0; k < P_NPARAMS; k++)
        en_set(k, p[k]);
    en_seed(1234u + (uint32_t)i * 77u);
    en_play(1);
}

typedef struct {
    double peak, sum2;
    long n;
    int nan;
} stat_t;

static void run(double secs, stat_t *s, FILE *wav)
{
    static int32_t buf[2 * 256];
    long blocks = (long)(secs * 44100.0 / 256.0), b;
    int j;
    for (b = 0; b < blocks; b++) {
        en_render(buf, 256, GAIN);
        for (j = 0; j < 512; j++) {
            double v = buf[j] / 8388608.0;
            if (v != v)
                s->nan = 1;
            if (fabs(v) > s->peak)
                s->peak = fabs(v);
            s->sum2 += v * v;
            s->n++;
            if (wav) {
                short o = (short)(v * 32767.0);
                fwrite(&o, 2, 1, wav);
            }
        }
    }
}
static double db(double x) { return 20.0 * log10(x > 1e-12 ? x : 1e-12); }
static int live(void)
{
    int i, n = 0;
    for (i = 0; i < EN_NLAYER; i++)
        n += en_lay_state[i] != LS_FREE;
    return n;
}

static void wav_head(FILE *f, long frames)
{
    unsigned hdr[11];
    memcpy(&hdr[0], "RIFF", 4);
    hdr[1] = (unsigned)(36 + frames * 4);
    memcpy(&hdr[2], "WAVE", 4);
    memcpy(&hdr[3], "fmt ", 4);
    hdr[4] = 16;
    hdr[5] = 1u | 2u << 16;
    hdr[6] = 44100;
    hdr[7] = 44100 * 4;
    hdr[8] = 4u | 16u << 16;
    memcpy(&hdr[9], "data", 4);
    hdr[10] = (unsigned)(frames * 4);
    fwrite(hdr, 4, 11, f);
}

int main(int argc, char **argv)
{
    int i;
    if (argc >= 5 && !strcmp(argv[1], "wav")) {
        int p = atoi(argv[2]), secs = atoi(argv[3]), t;
        FILE *f = fopen(argv[4], "wb");
        if (!f)
            return 1;
        wav_head(f, (long)secs * 44100 / 256 * 256);
        preset(p);
        for (t = 0; t < secs; t++) {
            stat_t s = {0};
            int k;
            run(1.0, &s, f);
            printf("%3d s  peak %6.1f  rms %6.1f  loops %u  live %d  tape %3.0f%%  wear", t + 1, db(s.peak),
                   db(sqrt(s.sum2 / (double)s.n)), (unsigned)en_loops, live(), (double)en_tape_used * 100.0);
            for (k = 0; k < EN_NLAYER; k++)
                if (en_lay_state[k] != LS_FREE)
                    printf(" %c%u:%.2f", "-RPF"[en_lay_state[k]], en_lay_serial[k], (double)en_lay_wear[k]);
            printf("\n");
        }
        fclose(f);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "bench")) {
        double secs = atof(argv[2]);
        stat_t s = {0};
        clock_t c0;
        preset(13);                             /* Night Wire: four layers, the choir */
        c0 = clock();
        run(secs, &s, 0);
        printf("bench %.0f s of audio in %.3f s\n", secs, (double)(clock() - c0) / CLOCKS_PER_SEC);
        return 0;
    }
    /* mu-law: within its step */
    for (i = -1000; i <= 1000; i++) {
        float x = (float)i / 1000.0f, y = en_unmulaw(en_mulaw(x));
        CHECK(fabsf(y - x) <= 0.035f * fabsf(x) + 0.0006f, "mulaw %f -> %f", (double)x, (double)y);
    }
    CHECK(en_unmulaw(0xFF) == 0.0f, "mulaw silence");
    /* every preset: sound from the start, a loop recorded, no clipping */
    for (i = 0; i < EN_NPRESETS; i++) {
        stat_t a = {0}, b = {0};
        preset(i);
        run(20.0, &a, 0);
        run(130.0, &b, 0);
        printf("  %-13s peak %5.1f dB  rms %5.1f / %5.1f dB  loops %u  live %d\n", en_preset_name(i), db(b.peak > a.peak ? b.peak : a.peak),
               db(sqrt(a.sum2 / a.n)), db(sqrt(b.sum2 / b.n)), (unsigned)en_loops, live());
        CHECK(!a.nan && !b.nan, "%s: NaN", en_preset_name(i));
        CHECK(db(a.peak) < -0.5 && db(b.peak) < -0.5, "%s: clips", en_preset_name(i));
        CHECK(db(sqrt(a.sum2 / a.n)) > -38.0, "%s: quiet at the start", en_preset_name(i));
        CHECK(db(sqrt(b.sum2 / b.n)) > -38.0, "%s: quiet later", en_preset_name(i));
        CHECK(en_loops >= 1 && live() >= 1, "%s: no loop", en_preset_name(i));
    }
    /* fast wear, four layers, long loops: they come and go for 15 minutes; the tape never runs over */
    {
        stat_t s = {0};
        int m, maxlive = 0;
        preset(13);
        en_set(P_DECAY, 100);
        en_set(P_LENGTH, 12);
        for (m = 0; m < 90; m++) {
            run(10.0, &s, 0);
            if (live() > maxlive)
                maxlive = live();
            CHECK(en_tape_used <= 1.0001f, "tape over");
        }
        printf("  long run: %u loops, peak %.1f dB, rms %.1f dB, most at once %d\n", (unsigned)en_loops, db(s.peak),
               db(sqrt(s.sum2 / s.n)), maxlive);
        CHECK(en_loops >= 15, "long run: only %u loops", (unsigned)en_loops);
        CHECK(db(s.peak) < -0.5 && !s.nan, "long run peak");
        CHECK(db(sqrt(s.sum2 / s.n)) > -36.0, "long run quiet");
    }
    /* stop: it slows and falls silent; start: the loops play on */
    {
        stat_t s = {0}, t = {0};
        preset(0);
        run(30.0, &s, 0);
        en_play(0);
        run(3.0, &s, 0);
        s = t;
        run(2.0, &s, 0);
        CHECK(s.peak < 0.01, "stopped but sounding (%f)", s.peak);
        en_play(1);
        run(3.0, &t, 0);
        CHECK(db(sqrt(t.sum2 / t.n)) > -40.0, "no sound after starting again");
    }
    /* REC: a new loop at once; the keys go onto it */
    {
        stat_t s = {0};
        uint32_t l0;
        preset(1);
        run(25.0, &s, 0);
        l0 = en_loops;
        en_new();
        en_note(60, 100);
        run(0.5, &s, 0);
        en_note(60, 0);
        CHECK(en_loops == l0 + 1, "REC: no new loop");
        en_regrow();
        run(4.0, &s, 0);
        CHECK(en_loops == l0 + 2, "a new preset: no new loop");
    }
    printf(fails ? "engine: %d failures\n" : "engine: ok\n", fails);
    return fails != 0;
}
