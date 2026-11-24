/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen; after AMB-1's amb_test.c, Copyright (C) 2026 Charles Vestal */
/* The instrument on the host: the pure parts (tunings, the balungan made) and the render (sound, no NaN, bounded,
 * every preset plays, pieces end and new ones start, the irama changes). GAM_BENCH=1: the time per second of
 * audio, for each preset. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "gam.h"

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
static void run(double secs)
{
    int blocks = (int)(secs * 44100.0 / 256.0), b, j;
    for (b = 0; b < blocks; b++) {
        en_render(buf, 256, 2048);
        for (j = 0; j < 512; j++) {
            double x = (double)buf[j] / 8388608.0;
            if (x != x)
                bad = 1;
            if (fabs(x) > peak)
                peak = fabs(x);
            sum2 += x * x;
        }
        nsamp += 512.0;
    }
}
static void reset_meter(void) { peak = sum2 = nsamp = 0.0; }
static double db(double x) { return x > 1e-12 ? 20.0 * log10(x) : -240.0; }

static void preset(int i)
{
    int16_t p[P_NPARAMS];
    int k;
    en_preset(i, p);
    for (k = 0; k < P_NPARAMS; k++)
        en_set(k, p[k]);
    en_restart();
}

int main(void)
{
    int i, t, f;
    /* tunings: each tone higher than the last, octaves near 2:1 (stretched a little) */
    for (t = 0; t < EN_NTUNING; t++) {
        float last = 0.0f;
        for (i = 0; i < 15; i++) {
            float hz = en_deg_hz(t, 0, i);
            CHECK(hz > last, "tuning %d degree %d not rising", t, i);
            last = hz;
        }
        {
            double r = (double)en_deg_hz(t, 0, 5) / (double)en_deg_hz(t, 0, 0);
            CHECK(r > 2.0 && r < 2.01, "tuning %d octave %f", t, r);
        }
    }
    CHECK(fabs((double)en_deg_hz(T_PLNEM, 12, 0) / (double)en_deg_hz(T_PLNEM, 0, 0) - 2.0) < 1e-3, "pitch +12");
    /* slendro is not 12-TET: its step near 240 cents */
    {
        double c = 1200.0 * log2((double)en_deg_hz(T_MANYURA, 0, 1) / (double)en_deg_hz(T_MANYURA, 0, 0));
        CHECK(c > 220.0 && c < 250.0, "slendro step %f cents", c);
    }
    /* the balungan made: in range, the gatra's ends sounded, the gongan ends on a strong tone */
    for (f = F_LANCARAN; f <= F_GILAK; f++)
        for (t = 0; t < EN_NTUNING; t++)
            for (i = 1; i < 40; i++) {
                int8_t b[EN_MAXBEATS];
                int n = 0, k;
                en_gen_test((uint32_t)(i * 7919 + f * 31 + t), f, t, b, &n);
                CHECK(n == 8 || n == 16 || n == 32, "form %d beats %d", f, n);
                for (k = 0; k < n; k++) {
                    CHECK(b[k] == -1 || (b[k] >= 3 && b[k] <= 11), "form %d beat %d degree %d", f, k, b[k]);
                    if (k % 4 == 3)
                        CHECK(b[k] >= 0, "form %d gatra end %d a rest", f, k);
                }
                for (k = 0; k + 1 < n; k++)
                    if (b[k] >= 0 && b[k + 1] >= 0)
                        CHECK(abs(b[k] - b[k + 1]) <= 5, "form %d leap %d -> %d", f, b[k], b[k + 1]);
            }

    en_init();
    run(1.0);
    CHECK(peak == 0.0, "resting is silent: %f", peak);
    en_play(1);
    reset_meter();
    run(20.0);
    CHECK(en_playing == 1, "playing");
    CHECK(!bad, "NaN");
    CHECK(peak > 0.05, "plays: peak %f", peak);
    CHECK(en_gongs >= 1, "a gong in 20 s: %u", (unsigned)en_gongs);

    /* every preset: a minute, sound, no NaN, bounded */
    {
        const char *bench = getenv("GAM_BENCH");
        for (i = 0; i < EN_NPRESETS; i++) {
            uint32_t s0 = en_strokes, g0 = en_gongs;
            clock_t c0;
            double secs;
            preset(i);
            run(2.0);
            reset_meter();
            c0 = clock();
            run(60.0);
            secs = (double)(clock() - c0) / CLOCKS_PER_SEC;
            printf("  %-14s peak %6.1f dB  rms %6.1f dB  strokes %4u  gongs %u  form %d  irama %d%s",
                   en_preset_name(i), db(peak), db(sqrt(sum2 / nsamp)), (unsigned)(en_strokes - s0),
                   (unsigned)(en_gongs - g0), en_form, en_level, bench ? "" : "\n");
            if (bench)
                printf("  cpu %.4f s/s\n", secs / 60.0);
            CHECK(!bad, "%s NaN", en_preset_name(i));
            CHECK(peak > 0.03, "%s silent: %f", en_preset_name(i), peak);
            CHECK(peak < 0.85, "%s too loud: %f", en_preset_name(i), peak);
            CHECK(en_strokes - s0 > 60, "%s few strokes", en_preset_name(i));
        }
    }

    /* a long run on Pelog Dawn: pieces end and begin, the irama moves, the peak stays bounded */
    {
        uint32_t p0, ir0;
        preset(0);
        run(3.0);
        p0 = en_pieces;
        ir0 = en_irama_changes;
        reset_meter();
        run(40.0 * 60.0);
        printf("  40 min: pieces %u, irama changes %u, peak %.1f dB, rms %.1f dB\n", (unsigned)(en_pieces - p0),
               (unsigned)(en_irama_changes - ir0), db(peak), db(sqrt(sum2 / nsamp)));
        CHECK(en_pieces - p0 >= 3, "new pieces: %u", (unsigned)(en_pieces - p0));
        CHECK(en_irama_changes - ir0 >= 2, "irama changes: %u", (unsigned)(en_irama_changes - ir0));
        CHECK(peak < 0.85 && !bad, "long run peak %f", peak);
    }
    /* hold: no new piece */
    {
        uint32_t p0;
        en_hold(1);
        run(10.0);
        p0 = en_pieces;
        run(15.0 * 60.0);
        CHECK(en_pieces == p0, "hold: %u new pieces", (unsigned)(en_pieces - p0));
        en_hold(0);
    }
    /* a new piece on request */
    {
        uint32_t p0 = en_pieces;
        en_new_piece();
        run(150.0);
        CHECK(en_pieces > p0, "new piece on request");
    }
    /* irama by hand */
    {
        uint32_t ir0;
        int lv;
        en_set(P_IRAMA, 0);
        preset(0);
        run(40.0);
        lv = en_level;
        ir0 = en_irama_changes;
        en_irama(lv < 2 ? 1 : -1);
        run(60.0);
        CHECK(en_irama_changes > ir0, "irama on request (level %d -> %d)", lv, en_level);
    }
    /* rest: the ring dies away to silence */
    en_play(0);
    run(30.0);
    reset_meter();
    run(1.0);
    CHECK(peak < 1e-4, "silent after resting: %f", peak);
    /* a key sounds while resting */
    en_key(K_BAR, 7);
    reset_meter();
    run(0.5);
    CHECK(peak > 0.01, "a key: %f", peak);
    en_key(K_GONG, 0);
    reset_meter();
    run(1.0);
    CHECK(peak > 0.01, "the gong key: %f", peak);

    printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails != 0;
}
