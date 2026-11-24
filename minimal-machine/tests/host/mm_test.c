/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen */
/* MINIMAL MACHINE's engine on the host: every preset makes sound, stays under its ceiling over a long run (no NaN,
 * no clipping), the loops mutate (and Hold stops them), Density adds and takes away hits in order, the evolved
 * loops survive a round trip, and the render's cost (MM_TIME=1: per preset, in microseconds a second of sound).
 *   mm_test [WAVDIR]   (WAVDIR: a minute of each preset as a WAV, to listen to) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "mm.h"

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

static int32_t buf[2 * 256];
static double peak, sum2, nsamp;
static int bad;
static FILE *wav;
static void run(float secs)
{
    int blocks = (int)(secs * 44100.0f / 256.0f), b, j;
    for (b = 0; b < blocks; b++) {
        mm_render(buf, 256, 4096);
        for (j = 0; j < 512; j++) {
            double x = (double)buf[j] / 8388608.0;
            if (x != x)
                bad++;
            if (fabs(x) > peak)
                peak = fabs(x);
            sum2 += x * x;
            nsamp += 1.0;
            if (wav) {
                int16_t s = (int16_t)(buf[j] >> 8);
                fwrite(&s, 2, 1, wav);
            }
        }
    }
}
static void reset_meter(void) { peak = sum2 = nsamp = 0.0; bad = 0; }
static double db(double x) { return 20.0 * log10(x > 1e-9 ? x : 1e-9); }

static void wav_head(FILE *f, uint32_t frames)
{
    uint32_t v;
    uint16_t h;
    fwrite("RIFF", 1, 4, f); v = 36 + frames * 4; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    h = 1; fwrite(&h, 2, 1, f); h = 2; fwrite(&h, 2, 1, f);
    v = 44100; fwrite(&v, 4, 1, f); v = 44100 * 4; fwrite(&v, 4, 1, f);
    h = 4; fwrite(&h, 2, 1, f); h = 16; fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f); v = frames * 4; fwrite(&v, 4, 1, f);
}

static void preset(int i)
{
    int16_t par[P_NPARAMS];
    int p;
    mm_preset_pars(i, par);
    for (p = 0; p < P_NPARAMS; p++)
        mm_set(p, par[p]);
    mm_preset(i);
}

static int popc(uint32_t m)
{
    int c = 0;
    while (m) {
        c += (int)(m & 1u);
        m >>= 1;
    }
    return c;
}

int main(int argc, char **argv)
{
    int i, t;
    char nm[16];
    const char *wavdir = argc > 1 ? argv[1] : 0;
    int timing = getenv("MM_TIME") != 0;

    mm_init();
    mm_play(1);

    /* the presets' recipes are in range, their loops valid, names distinct */
    for (i = 0; i < MM_NPRESETS; i++) {
        int16_t par[P_NPARAMS];
        mm_gen_t g;
        int p, k;
        mm_preset_pars(i, par);
        for (p = 0; p < P_NPARAMS; p++)
            CHECK(par[p] >= mm_param_info(p)->lo && par[p] <= mm_param_info(p)->hi, "preset %d par %d = %d", i, p, par[p]);
        mm_preset_home(i, &g);
        CHECK(mm_gen_ok(&g), "preset %d loops", i);
        for (k = 0; k < i; k++)
            CHECK(strcmp(mm_preset_name(i), mm_preset_name(k)), "names %d %d", i, k);
        printf("  %-14s %s / %s, loops %d %d %d %d %d %d\n", mm_preset_name(i), mm_chord_name(i, 0, par[P_KEY], nm),
               mm_chord_name(i, 1, par[P_KEY], nm + 8), g.len[0], g.len[1], g.len[2], g.len[3], g.len[4], g.len[5]);
    }

    /* every preset: sound from the start, every part plays, three minutes under the ceiling */
    for (i = 0; i < MM_NPRESETS; i++) {
        double hits[MM_NT] = {0};
        double secs = 180.0, t0, us;
        if (wavdir) {
            char path[512];
            snprintf(path, sizeof path, "%s/%02d.wav", wavdir, i);
            wav = fopen(path, "wb");
            if (wav)
                wav_head(wav, (uint32_t)(60.0 * 44100.0 / 256.0) * 256u);
        }
        preset(i);
        reset_meter();
        mm_lim_min = 1.0f;
        t0 = (double)clock();
        run(60.0f);
        if (wav) {
            fclose(wav);
            wav = 0;
        }
        for (t = 0; t < MM_NT; t++)
            hits[t] = (double)popc(mm_pat[t]);
        run((float)secs - 60.0f);
        us = ((double)clock() - t0) / CLOCKS_PER_SEC * 1e6 / secs;
        CHECK(!bad, "%s NaN", mm_preset_name(i));
        CHECK(db(peak) < -1.0, "%s peak %.1f dB", mm_preset_name(i), db(peak));
        CHECK(db(sqrt(sum2 / nsamp)) > -30.0, "%s quiet: rms %.1f dB", mm_preset_name(i), db(sqrt(sum2 / nsamp)));
        CHECK(hits[T_KICK] >= 4, "%s kick", mm_preset_name(i));
        CHECK(db((double)mm_lim_min) > -4.0, "%s limiter pressed %.1f dB", mm_preset_name(i), db((double)mm_lim_min));
        printf("  %-14s peak %5.1f dB  rms %5.1f dB  limiter %4.1f dB  hits %g %g %g %g %g %g%s", mm_preset_name(i),
               db(peak), db(sqrt(sum2 / nsamp)), db((double)mm_lim_min), hits[0], hits[1], hits[2], hits[3], hits[4], hits[5],
               timing ? "" : "\n");
        if (timing)
            printf("  %.0f us/s\n", us);
    }

    /* mutations: they come every few bars, and Hold stops them */
    preset(0);
    mm_set(P_DRIFT, 80);                             /* every 2 bars */
    run(0.1f);
    {
        uint32_t m0 = mm_muts;
        run(60.0f);                                  /* 30 bars at 118 */
        CHECK(mm_muts - m0 >= 10, "mutations: %u in a minute", (unsigned)(mm_muts - m0));
        mm_hold(1);
        run(0.1f);
        m0 = mm_muts;
        run(30.0f);
        CHECK(mm_muts == m0, "held: %u mutations", (unsigned)(mm_muts - m0));
        mm_hold(0);
        mm_set(P_DRIFT, 0);
        run(0.1f);
        m0 = mm_muts;
        run(30.0f);
        CHECK(mm_muts == m0, "drift 0: %u mutations", (unsigned)(mm_muts - m0));
        mm_mutate();
        run(0.5f);
        CHECK(mm_muts == m0 + 1, "mutate now");
    }

    /* a long evolution keeps its shape: the kick on the four, nothing out of range */
    preset(5);
    mm_set(P_DRIFT, 100);
    reset_meter();
    run(600.0f);                                     /* ten minutes, a mutation every bar */
    {
        mm_gen_t g;
        mm_gen_get(&g);
        CHECK(mm_gen_ok(&g), "evolved loops valid");
        CHECK((mm_pat[T_KICK] & 0x1111u) == 0x1111u, "the kick on the four: %x", (unsigned)mm_pat[T_KICK]);
        CHECK(db(peak) < -1.0 && !bad, "ten minutes evolving: peak %.1f", db(peak));
        CHECK(db(sqrt(sum2 / nsamp)) > -30.0, "ten minutes evolving: rms %.1f", db(sqrt(sum2 / nsamp)));
        printf("  ten minutes of drift: peak %.1f dB, %u mutations, loops %d %d %d %d %d %d\n", db(peak),
               (unsigned)mm_muts, g.len[0], g.len[1], g.len[2], g.len[3], g.len[4], g.len[5]);
        /* the round trip: what was evolved comes back */
        preset(5);
        run(0.1f);
        mm_gen_put(&g);
        run(0.1f);
        {
            mm_gen_t h;
            mm_gen_get(&h);
            CHECK(!memcmp(&g, &h, sizeof g), "gen round trip");
        }
        g.len[T_KICK] = 7;                           /* a bad one is refused */
        CHECK(!mm_gen_ok(&g), "bad gen refused");
    }

    /* Density: more hits as it rises, the kick always */
    preset(0);
    {
        int last = -1, d;
        for (d = 0; d <= 100; d += 25) {
            int n = 0;
            mm_set(P_DENSITY, d);
            run(0.05f);
            for (t = 0; t < MM_NT; t++)
                n += popc(mm_pat[t]);
            CHECK(n >= last, "density %d: %d hits (was %d)", d, n, last);
            CHECK(popc(mm_pat[T_KICK]) >= 4, "density %d: the kick", d);
            last = n;
        }
    }

    /* MASTER down: the output follows; stopped: the tails die out */
    preset(1);
    run(5.0f);
    reset_meter();
    {
        int b, j;
        for (b = 0; b < 400; b++) {
            mm_render(buf, 256, 1024);
            for (j = 0; j < 512; j++)
                if (fabs((double)buf[j] / 8388608.0) > peak)
                    peak = fabs((double)buf[j] / 8388608.0);
        }
        CHECK(db(peak) < -12.0, "master at a quarter: peak %.1f", db(peak));
    }
    mm_play(0);
    run(12.0f);
    reset_meter();
    run(1.0f);
    CHECK(db(peak) < -60.0, "stopped: still %.1f dB", db(peak));

    printf(fails ? "%d FAILED\n" : "mm: all ok\n", fails);
    return fails != 0;
}
