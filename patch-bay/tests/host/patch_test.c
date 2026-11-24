/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen; built on AMB-1 by Charles Vestal */
/* PATCH BAY's instrument on the host (app/dsp/patch.c alone): every preset is legal and musical and sounds; a long
 * run of auto-patcher moves keeps every patch musical and the output inside its bounds; the render's cost.
 *   cc -O2 -DEN_HOST -Iapp/dsp -Ibase/include tests/host/patch_test.c app/dsp/patch.c -lm
 *   patch_test [bench]     bench: the render's time per second of sound, each preset with its moves */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "patch.h"

static int fails;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)

static int8_t cab[PB_NIN];
static uint8_t par[PB_NMOD];

static void load(int p)
{
    const pb_preset_t *s = &PB_PRESET[p];
    int i;
    for (i = 0; i < PB_NIN; i++)
        cab[i] = -1;
    for (i = 0; i < PB_MAXCAB && s->cab[i][0] != 0xFF; i++)
        cab[s->cab[i][0]] = (int8_t)s->cab[i][1];
    for (i = 0; i < PB_NMOD; i++)
        par[i] = s->par[i];
}
static void apply(int p)
{
    const pb_preset_t *s = &PB_PRESET[p];
    int i;
    for (i = 0; i < PB_NG; i++)
        pb_global(i, s->g[i]);
    for (i = 0; i < PB_NMOD; i++)
        pb_param(i, par[i]);
    for (i = 0; i < 8; i++)
        pb_seq(i, s->seq[i]);
    for (i = 0; i < PB_NIN; i++)
        pb_connect(i, cab[i]);
    pb_restart();
    pb_play(1);
}

static int32_t buf[2 * 256];
/* secs of sound; the peak and the mean level (dB) */
static void run(float secs, double *peak_db, double *rms_db, int moves, uint32_t *seed)
{
    uint32_t blocks = (uint32_t)(secs * 44100.0f / 256.0f), b, j;
    double pk = 0.0, e = 0.0;
    pb_move_t last, mv;
    int have = 0;
    for (b = 0; b < blocks; b++) {
        pb_render(buf, 256, 4096u);
        for (j = 0; j < 512; j++) {
            double x = (double)buf[j] / 8388608.0;
            if (fabs(x) > pk)
                pk = fabs(x);
            e += x * x;
        }
        if (moves && b % (uint32_t)(4.0f * 44100.0f / 256.0f) == 0u && b) {   /* a move every 4 s */
            if (pb_auto_move(cab, par, seed, have ? &last : 0, &mv)) {
                pb_apply_move(cab, &mv);
                if (mv.in_old >= 0)
                    pb_connect(mv.in_old, cab[mv.in_old]);
                if (mv.in >= 0)
                    pb_connect(mv.in, cab[mv.in]);
                last = mv;
                have = 1;
                CHECK(pb_musical(cab, par), "a move left an unmusical patch");
            }
        }
    }
    *peak_db = (20.0 * log10(pk + 1e-12));
    *rms_db = (10.0 * log10(e / (double)(blocks * 512u) + 1e-20));
}

int main(int argc, char **argv)
{
    int p, i, k;
    double pk, rms;
    uint32_t seed = 12345u;
    int bench = argc > 1 && !strcmp(argv[1], "bench");
    pb_init();
    /* the presets: legal, musical, distinct names */
    for (p = 0; p < PB_NPRESETS; p++) {
        load(p);
        CHECK(pb_legal(cab), "%s: not legal", PB_PRESET[p].name);
        CHECK(pb_musical(cab, par), "%s: not musical", PB_PRESET[p].name);
        for (i = 0; i < PB_NMOD; i++)
            CHECK(par[i] <= 100, "%s: knob %d", PB_PRESET[p].name, i);
        for (k = 0; k < p; k++)
            CHECK(strcmp(PB_PRESET[p].name, PB_PRESET[k].name), "two presets called %s", PB_PRESET[p].name);
    }
    /* the rules */
    load(0);
    cab[I_VCF_IN] = O_DLY;                       /* VCO.. no: DLY -> VCF -> VCA -> DLY, a circle */
    CHECK(pb_loops(cab), "a circle not seen");
    CHECK(!pb_legal(cab), "a circle legal");
    load(0);
    cab[I_VCF_IN] = O_SEQ_CV;
    CHECK(!pb_legal(cab), "a CV into an audio input legal");
    load(0);
    cab[I_VCA_IN] = O_NOISE;                     /* the noise straight on: through a VCA that ENV 1 opens */
    CHECK(pb_musical(cab, par), "gated noise refused");
    cab[I_VCA_CV] = -1;
    CHECK(!pb_musical(cab, par), "raw noise allowed");
    load(0);
    cab[I_SEQ_CLK] = -1;                         /* no clock: the sequencer stands, ENV 1 never comes */
    CHECK(!pb_alive(cab, par, O_ENV1), "a dead ENV alive");
    if (!bench) {
        /* every preset sounds, inside its bounds, with and without moves */
        for (p = 0; p < PB_NPRESETS; p++) {
            load(p);
            apply(p);
            run(20.0f, &pk, &rms, 0, &seed);
            printf("%-16s peak %6.1f dB  mean %6.1f dB\n", PB_PRESET[p].name, pk, rms);
            CHECK(pk < -1.0, "%s: peak %.1f dB", PB_PRESET[p].name, pk);
            CHECK(pk > -24.0, "%s: too quiet, peak %.1f dB", PB_PRESET[p].name, pk);
            CHECK(rms > -50.0, "%s: silent, %.1f dB", PB_PRESET[p].name, rms);
        }
        for (p = 0; p < PB_NPRESETS; p++) {
            load(p);
            apply(p);
            run(240.0f, &pk, &rms, 1, &seed);
            printf("%-16s 4 min of moves: peak %6.1f dB  mean %6.1f dB, %d cables\n", PB_PRESET[p].name, pk, rms,
                   pb_ncables(cab));
            CHECK(pk < -1.0, "%s with moves: peak %.1f dB", PB_PRESET[p].name, pk);
            CHECK(rms > -50.0, "%s with moves: silent, %.1f dB", PB_PRESET[p].name, rms);
        }
        /* many moves alone: always found, always musical */
        for (p = 0; p < PB_NPRESETS; p++) {
            pb_move_t mv, last;
            int found = 0, have = 0;
            load(p);
            for (i = 0; i < 2000; i++) {
                if (pb_auto_move(cab, par, &seed, have ? &last : 0, &mv)) {
                    pb_apply_move(cab, &mv);
                    last = mv;
                    have = 1;
                    found++;
                    if (!pb_musical(cab, par)) {
                        CHECK(0, "%s: move %d unmusical", PB_PRESET[p].name, i);
                        break;
                    }
                }
            }
            CHECK(found > 1900, "%s: only %d moves of 2000 found", PB_PRESET[p].name, found);
        }
    } else {
        /* the render's cost: seconds of CPU per second of sound, the busiest preset */
        double worst = 0.0;
        for (p = 0; p < PB_NPRESETS; p++) {
            clock_t t0;
            double s;
            load(p);
            apply(p);
            run(2.0f, &pk, &rms, 0, &seed);
            t0 = clock();
            run(60.0f, &pk, &rms, 1, &seed);
            s = (double)(clock() - t0) / CLOCKS_PER_SEC / 60.0;
            printf("%-16s %.5f s per s\n", PB_PRESET[p].name, s);
            if (s > worst)
                worst = s;
        }
        printf("worst %.5f s per s\n", worst);
    }
    printf(fails ? "%d FAILED\n" : "patch: all ok\n", fails);
    return fails != 0;
}
