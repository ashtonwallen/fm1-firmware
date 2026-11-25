/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CAVE: ashtonwallen, 2026 */
/* The instrument on the host: the caves' tunings (no semitones in a set, no minor ninth over any river root), the
 * worlds (fresh ones valid, a day of growth keeps every note in its set), and the render: every cave makes sound,
 * nothing is NaN, the peaks stay under -1 dBFS over long runs, resting is silent, the keys play, events sound. Then
 * the render's cost in ns a frame (the CPU check: compare with AMB-1's busiest piece).
 *   cave_test                          the checks
 *   cave_test dump STYLE MINUTES       what a cave plays, note by note, with the river's moves and the events
 *   cave_test hours STYLE HOURS        a cave's world over hours (fast): its notes, floods, stones, retunes
 *   cave_test demos DIR                the demo clips: NN-<cave>.wav (20 s, settled) and 00-listen.wav (3 min) */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "cave.h"

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

static void cname(int cents, char *b)            /* "F#4", "E4-14" */
{
    int n = (cents + 50) / 100, d = cents - n * 100;
    if (d > 4 || d < -4)
        sprintf(b, "%s%d%+d", EN_NOTE_NAME[n % 12], n / 12 - 1, d);
    else
        sprintf(b, "%s%d", EN_NOTE_NAME[n % 12], n / 12 - 1);
}

/* ---- the dump: every note */
static const char *const ROLE[] = {"drip", "splash", "run", "flood", "stone", "bats", "pebble"};
static int notes_in[128];
static void dbg_note(uint32_t ms, int cents, float vel, int role, int obj)
{
    char t[24];
    cname(cents, t);
    printf("%3u:%04.1f  %-8s %.2f %-6s %s\n", ms / 60000u, (double)(ms % 60000u / 100u) / 10.0, t, (double)vel, ROLE[role],
           obj == 255 ? "" : "");
    (void)obj;
}
static void dbg_count(uint32_t ms, int cents, float vel, int role, int obj)
{
    (void)ms, (void)vel, (void)role, (void)obj;
    notes_in[((cents + 50) / 100) & 127]++;
}
static void dbg_harm(uint32_t ms, int cents)
{
    char t[24];
    cname(cents, t);
    printf("%3u:%04.1f  ---- river on %s\n", ms / 60000u, (double)(ms % 60000u / 100u) / 10.0, t);
}
static void dbg_event(uint32_t ms, int kind, const char *what)
{
    (void)kind;
    printf("%3u:%04.1f  ==== %s\n", ms / 60000u, (double)(ms % 60000u / 100u) / 10.0, what);
}
static void print_world(int st)
{
    en_world_t w;
    int i;
    char t[24];
    en_world_get(&w);
    printf("# world: %u min, wet %u, floods %u, stones %u, harm %u |", (unsigned)w.minutes, w.wet, w.floods, w.stones,
           w.harm);
    for (i = 0; i < w.nobj; i++) {
        cname(en_deg_cents(st, w.obj[i].deg), t);
        printf(" %s%s", w.obj[i].kind == OB_STAL ? "S:" : "P:", t);
    }
    printf("\n");
}

static int dump(int st, int minutes)
{
    stats_t a = {0};
    en_dbg_note = dbg_note;
    en_dbg_harm = dbg_harm;
    en_dbg_event = dbg_event;
    en_init();
    style(st);
    en_play(1);
    run(0.05, &a);
    printf("# %s: %s on %s\n", en_style_name(st), en_style_scale(st), EN_NOTE_NAME[en_style_tonic(st) % 12]);
    print_world(st);
    run(60.0 * minutes, &a);
    print_world(st);
    printf("# peak %.1f dB, rms %.1f dB, max voices %u\n", db(a.peak), db(sqrt(a.sum2 / a.n)), (unsigned)a.maxv);
    return 0;
}

static int hours(int st, int h)
{
    int i;
    en_dbg_event = dbg_event;
    en_init();
    style(st);
    en_play(1);
    {
        stats_t a = {0};
        run(0.05, &a);
    }
    print_world(st);
    for (i = 0; i < h; i++) {
        stats_t a = {0};
        en_ff(60);
        run(0.05, &a);
        print_world(st);
    }
    return 0;
}

static void file_name(int i, char *name)
{
    const char *sn = en_style_name(i);
    int j = 0, k;
    for (k = 0; sn[k] && j < 63; k++)
        if (sn[k] == ' ')
            name[j++] = '-';
        else if (sn[k] != 39 && sn[k] != ',')
            name[j++] = sn[k];
    name[j] = 0;
}

static int demos(const char *dir)
{
    char path[512], name[64];
    int i;
    stats_t a = {0};
    for (i = 0; i < EN_NSTYLES; i++) {
        en_init();
        style(i);
        en_play(1);
        run(30.0, &a);
        file_name(i, name);
        snprintf(path, sizeof path, "%s/%02d-%s.wav", dir, i + 1, name);
        wav_open(path);
        run(20.0, &a);
        wav_close();
        printf("%s\n", path);
    }
    en_init();
    style(0);
    en_play(1);
    snprintf(path, sizeof path, "%s/00-listen.wav", dir);
    memset(&a, 0, sizeof a);
    wav_open(path);
    run(180.0, &a);
    wav_close();
    printf("%s (peak %.1f dB)\n", path, db(a.peak));
    return 0;
}

int main(int argc, char **argv)
{
    int i;
    if (argc > 3 && !strcmp(argv[1], "dump"))
        return dump(atoi(argv[2]), atoi(argv[3]));
    if (argc > 3 && !strcmp(argv[1], "hours"))
        return hours(atoi(argv[2]), atoi(argv[3]));
    if (argc > 2 && !strcmp(argv[1], "demos"))
        return demos(argv[2]);
    /* the tunings */
    for (i = 0; i < EN_NSTYLES; i++) {
        int16_t p[P_NPARAMS];
        int k, n = en_style_nset(i), a, b;
        en_style_recipe(i, p);
        for (k = 0; k < P_NSTYLE; k++)
            CHECK(p[k] >= en_param_info(k)->lo && p[k] <= en_param_info(k)->hi, "style %d param %d", i, k);
        CHECK(strlen(en_style_name(i)) <= 16, "name %s too long", en_style_name(i));
        for (a = 0; a < n; a++)
            for (b = 0; b < n; b++) {
                int d = ((en_deg_cents(i, b) - en_deg_cents(i, a)) % 1200 + 1200) % 1200;
                CHECK(a == b || d > 150, "%s: a semitone in the set (%d, %d)", en_style_name(i), a, b);
            }
    }
    /* every note of every set over every root of its river: no minor ninth above the root or its fifth's tritone */
    for (i = 0; i < EN_NSTYLES; i++) {
        en_world_t w;
        int h, d, n = en_style_nset(i);
        en_init();
        style(i);
        en_play(1);
        {
            stats_t a = {0};
            run(0.3, &a);
        }
        for (h = 0; h < 6; h++) {
            int root = en_harm_cents;
            printf("  %-16s river %+5d:", en_style_name(i), root);
            for (d = 0; d < n; d++) {
                int iv = ((en_deg_cents(i, d) - en_style_tonic(i) * 100 - root) % 1200 + 1200) % 1200;
                printf(" %4d", iv);
                CHECK(!(iv > 60 && iv < 160), "%s: a minor ninth over root %d (%d)", en_style_name(i), root, iv);
            }
            printf("\n");
            en_next_harm();
            {
                stats_t a = {0};
                run(0.3, &a);
            }
            en_world_get(&w);
            if (w.harm == 0)
                break;
        }
    }
    /* the worlds: fresh ones valid; a day keeps every note in its range, grows them, floods them */
    for (i = 0; i < EN_NSTYLES; i++) {
        en_world_t w, w0;
        int k, hr;
        uint32_t rt0;
        en_world_fresh(i, &w0);
        CHECK(en_world_valid(&w0, i), "%s: fresh world invalid", en_style_name(i));
        CHECK(!en_world_valid(&w0, (i + 1) % EN_NSTYLES), "%s: world valid for another cave", en_style_name(i));
        en_init();
        style(i);
        en_play(1);
        rt0 = en_retunes;
        for (hr = 0; hr < 24; hr++) {
            stats_t a = {0};
            en_ff(60);
            run(0.3, &a);
            en_world_get(&w);
            CHECK(en_world_valid(&w, i), "%s: world invalid after %d h", en_style_name(i), hr);
            for (k = 0; k < w.nobj; k++)
                CHECK(w.obj[k].deg >= w.obj[k].lo && w.obj[k].deg <= w.obj[k].hi, "%s: object %d out of range",
                      en_style_name(i), k);
        }
        printf("  %-16s a day: %u retunes, %u floods, %u stones\n", en_style_name(i), (unsigned)(en_retunes - rt0),
               w.floods, w.stones);
        CHECK(en_retunes - rt0 >= 3, "%s: nothing grew", en_style_name(i));
        CHECK(w.floods >= 2, "%s: no floods in a day", en_style_name(i));
    }
    /* every cave: sound within seconds, bounded over a long run, nothing NaN */
    for (i = 0; i < EN_NSTYLES; i++) {
        stats_t a = {0}, b = {0};
        uint32_t n0;
        en_init();
        style(i);
        en_play(1);
        run(60.0, &a);
        n0 = en_notes_played;
        run(540.0, &b);
        printf("%-16s first minute rms %5.1f dB peak %5.1f dB; next 9 min peak %5.1f dB, rms %5.1f dB, notes/min %4.1f, "
               "voices %u\n",
               en_style_name(i), db(sqrt(a.sum2 / a.n)), db(a.peak), db(b.peak), db(sqrt(b.sum2 / b.n)),
               (double)(en_notes_played - n0) / 9.0, (unsigned)(a.maxv > b.maxv ? a.maxv : b.maxv));
        CHECK(a.nan == 0 && b.nan == 0, "%s: NaN", en_style_name(i));
        CHECK(db(sqrt(a.sum2 / a.n)) > -40.0, "%s: too quiet", en_style_name(i));
        CHECK(db(a.peak) < -1.0 && db(b.peak) < -1.0, "%s: peaks over -1 dBFS", en_style_name(i));
    }
    /* events: each sounds; a flood rearranges, the extremes stay bounded */
    for (i = 0; i < EN_NSTYLES; i += 3) {
        stats_t a = {0};
        uint32_t n0;
        int k;
        en_init();
        style(i);
        for (k = 0; k < P_NSTYLE; k++)
            en_set(k, en_param_info(k)->hi);
        en_play(1);
        run(5.0, &a);
        for (k = EV_RUN; k <= EV_BATS; k++) {
            n0 = en_notes_played;
            en_event(k);
            run(k == EV_FLOOD ? 360.0 : 6.0, &a);
            CHECK(en_notes_played > n0, "%s: event %d silent", en_style_name(i), k);
        }
        printf("%-16s everything up, every event: peak %.1f dB, voices %u\n", en_style_name(i), db(a.peak), a.maxv);
        CHECK(a.nan == 0 && db(a.peak) < -1.0, "%s everything up: peak %.1f", en_style_name(i), db(a.peak));
        for (k = 0; k < P_NSTYLE; k++)
            en_set(k, en_param_info(k)->lo);
        memset(&a, 0, sizeof a);
        run(120.0, &a);
        CHECK(a.nan == 0 && db(a.peak) < -1.0, "everything down");
        CHECK(db(a.peak) > -60.0, "%s everything down still plays: %.1f", en_style_name(i), db(a.peak));
    }
    /* resting: silence; playing again: sound; the ambience off: the music alone */
    {
        stats_t a = {0}, b = {0};
        en_init();
        style(0);
        en_play(1);
        run(15.0, &a);
        en_play(0);
        run(25.0, &a);
        run(2.0, &b);
        CHECK(db(b.peak) < -70.0, "rested: peak %.1f dB", db(b.peak));
        en_play(1);
        memset(&b, 0, sizeof b);
        run(10.0, &b);
        CHECK(db(sqrt(b.sum2 / b.n)) > -50.0, "resumed: rms %.1f", db(sqrt(b.sum2 / b.n)));
    }
    /* a key: a pebble, always a note of the set */
    {
        stats_t a = {0};
        int k;
        en_dbg_note = dbg_count;
        en_init();
        style(5);
        en_play(0);
        run(3.0, &a);
        memset(&a, 0, sizeof a);
        for (k = 53; k < 80; k++) {
            en_note(k, 100);
            run(0.2, &a);
        }
        CHECK(db(a.peak) > -40.0, "a key: peak %.1f dB", db(a.peak));
        en_dbg_note = 0;
    }
    /* the cost: ns a frame, each cave as it is, then dense with every event going */
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
                if (pass) {
                    en_set(P_DRIPS, 100);
                    en_set(P_RING, 100);
                    en_set(P_AMB, 100);
                    en_set(P_ECHO, 100);
                }
                en_play(1);
                for (b = 0; b < 3000; b++)
                    en_render(buf, 256, 4096);
                if (pass) {
                    en_event(EV_FLOOD);
                    en_event(EV_BATS);
                    en_event(EV_RUN);
                }
                t0 = now();
                for (b = 0; b < 12000; b++) {
                    en_render(buf, 256, 4096);
                    mv = en_voices > mv ? en_voices : mv;
                    if (pass && b % 1500 == 0)
                        en_event(b % 3000 ? EV_STONE : EV_BATS);
                }
                dt = (now() - t0) / (12000.0 * 256.0) * 1e9;
                printf("  %-16s %s%.1f ns/frame, %d voices at most\n", en_style_name(i), pass ? "dense: " : "", dt, mv);
                if (dt > worst)
                    worst = dt, wi = i + pass * 100;
            }
        printf("render: busiest %s%s %.1f ns/frame\n", en_style_name(wi % 100), wi >= 100 ? " (dense)" : "", worst);
    }
    printf(fails ? "cave_test: %d FAILED\n" : "cave_test: all passed\n", fails);
    return fails != 0;
}
