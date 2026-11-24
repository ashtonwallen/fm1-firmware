/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen */
/* NIGHT DRIVE's engine on the host: the pure parts (the scale, the names, the progressions) and the render: every
 * preset makes sound, never a NaN, never over its ceiling at full MASTER; the sections come round; Drift moves the
 * key, Drift 0 keeps it; parked, it falls silent. Prints the engine's time against real time (the CPU's ratio). */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "nd.h"

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
static double peak, sum2;
static long nsamp;
static int bad, seen;
static void run(double secs, uint32_t gain)
{
    int blocks = (int)(secs * 44100.0 / 256.0), b, j;
    for (b = 0; b < blocks; b++) {
        en_render(buf, 256, gain);
        seen |= 1 << en_section;
        for (j = 0; j < 512; j++) {
            double x = (double)buf[j] / 8388608.0;
            if (x != x)
                bad++;
            if (fabs(x) > peak)
                peak = fabs(x);
            sum2 += x * x;
        }
        nsamp += 512;
    }
}
static void reset_meter(void)
{
    peak = sum2 = 0.0;
    nsamp = 0;
    bad = 0;
    seen = 0;
}
static double rms_db(void) { return 10.0 * log10(sum2 / (double)(nsamp ? nsamp : 1) + 1e-20); }

static void preset(int i, int drift)
{
    int16_t p[P_NPARAMS];
    int k;
    en_init();
    en_preset(i, p);
    if (drift >= 0)
        p[P_DRIFT] = (int16_t)drift;
    for (k = 0; k < P_NPARAMS; k++)
        en_set(k, p[k]);
    en_restart();
    en_play(1);
}

int main(void)
{
    char t[16];
    int i, k, off, minor;
    double worst = 0.0;
    uint32_t kc;

    /* the scale: natural minor, from the root at or below base */
    CHECK(en_scale_note(9, 0, 60) == 57, "A minor degree 0 near 60: %d", en_scale_note(9, 0, 60));
    CHECK(en_scale_note(9, 2, 60) == 60, "A minor degree 2: %d", en_scale_note(9, 2, 60));
    CHECK(en_scale_note(9, 7, 60) == 69, "A minor degree 7: %d", en_scale_note(9, 7, 60));
    CHECK(en_scale_note(9, -1, 60) == 55, "A minor degree -1: %d", en_scale_note(9, -1, 60));
    CHECK(en_scale_note(0, 5, 60) == 68, "C minor degree 5 (Ab): %d", en_scale_note(0, 5, 60));
    /* the names, as the key spells them */
    CHECK(!strcmp(en_chord_name(1, 0, 5, t), "Db"), "Db in F minor: %s", t);
    CHECK(!strcmp(en_chord_name(6, 1, 6, t), "F#m"), "F#m: %s", t);
    CHECK(!strcmp(en_chord_name(3, 0, 0, t), "Eb"), "Eb in C minor: %s", t);
    en_param_text(P_ROOT, 8, t);
    CHECK(!strcmp(t, "G#m"), "key 8: %s", t);
    en_param_text(P_ROOT, 10, t);
    CHECK(!strcmp(t, "Bbm"), "key 10: %s", t);
    en_param_text(P_TEMPO, 108, t);
    CHECK(!strcmp(t, "108 bpm"), "speed: %s", t);
    en_param_text(P_ARP, 0, t);
    CHECK(!strcmp(t, "Off"), "arp 0: %s", t);
    /* the progressions: four chords, each on a degree of the minor key (V the one borrowed major) */
    for (i = 0; i < EN_NPROG; i++)
        for (k = 0; k < 4; k++) {
            en_prog_chord(i, k, &off, &minor);
            CHECK(off == 0 || off == 3 || off == 5 || off == 7 || off == 8 || off == 10, "prog %d chord %d: %d", i, k, off);
        }
    /* the presets' recipes in range, their names short enough for the header */
    for (i = 0; i < EN_NPRESETS; i++) {
        int16_t p[P_NPARAMS];
        en_preset(i, p);
        for (k = 0; k < P_NPARAMS; k++)
            CHECK(p[k] >= en_param_info(k)->lo && p[k] <= en_param_info(k)->hi, "preset %d param %d: %d", i, k, p[k]);
        CHECK(strlen(en_preset_name(i)) <= 12, "preset %d name", i);
    }

    /* every preset: sound, no NaN, under the ceiling at full MASTER (the limiter), and the intro gives way */
    for (i = 0; i < EN_NPRESETS; i++) {
        struct timespec t0, t1;
        double s;
        preset(i, -1);
        reset_meter();
        clock_gettime(CLOCK_MONOTONIC, &t0);
        run(120.0, 4096);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        s = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
        if (s > worst)
            worst = s;
        printf("  %-13s peak %5.1f dB  rms %5.1f dB  %.3f s for 120 s\n", en_preset_name(i), 20.0 * log10(peak + 1e-12),
               rms_db(), s);
        CHECK(bad == 0, "%s: %d NaN", en_preset_name(i), bad);
        CHECK(peak < 0.60, "%s: peak %.3f at full MASTER", en_preset_name(i), peak);
        CHECK(rms_db() > -30.0, "%s: too quiet, %.1f dB", en_preset_name(i), rms_db());
        CHECK(seen & (1 << SEC_DRIVE), "%s: never reached a drive", en_preset_name(i));
        CHECK(en_sixteenths > 600u, "%s: %u sixteenths", en_preset_name(i), (unsigned)en_sixteenths);
    }
    printf("  the busiest preset: %.3f s of engine for 120 s of sound (%.2f%% of real time)\n", worst,
           worst / 120.0 * 100.0);

    /* Drift high: the sections come round (a break, a build), the key moves */
    preset(0, 100);
    reset_meter();
    kc = en_keychanges;
    run(600.0, 2048);
    CHECK((seen & 15) == 15, "Drift 100: sections seen %x", seen);
    CHECK(en_keychanges - kc >= 2u, "Drift 100: %u key changes in 10 minutes", (unsigned)(en_keychanges - kc));
    CHECK(peak < 0.60, "Drift 100: peak %.3f", peak);
    /* Drift 0: the key stays home */
    preset(3, 0);
    kc = en_keychanges;
    run(600.0, 2048);
    CHECK(en_keychanges == kc && en_key == 2, "Drift 0: %u key changes, key %d", (unsigned)(en_keychanges - kc), en_key);
    /* a key asked for lands on the next bar */
    en_set(P_ROOT, 4);
    run(3.0, 2048);
    CHECK(en_key == 4, "key 4 asked: %d", en_key);
    /* the next section, asked */
    {
        int s0 = en_section;
        en_next();
        run(3.0, 2048);
        CHECK(en_section != s0 && en_sec_bar <= 1, "next section: %d -> %d bar %d", s0, en_section, en_sec_bar);
    }
    /* parked: silent once the tails are gone */
    en_play(0);
    run(8.0, 4096);
    reset_meter();
    run(2.0, 4096);
    CHECK(peak < 0.003, "parked: peak %.4f", peak);
    CHECK(!en_playing, "parked");
    /* and on again */
    en_play(1);
    reset_meter();
    run(10.0, 2048);
    CHECK(peak > 0.02, "driving again: peak %.4f", peak);

    if (fails)
        printf("%d FAILED\n", fails);
    else
        printf("engine ok\n");
    return fails != 0;
}
