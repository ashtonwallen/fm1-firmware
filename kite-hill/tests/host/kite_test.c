/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * KITE HILL: ashtonwallen, 2026 */
/* The instrument on the host: every hill makes sound, nothing is NaN, the peaks stay under -1 dBFS over long runs
 * (a whole day, every knob at its top), the notes always fit (each sounding line a note of the chord, no two a
 * second or a minor ninth apart), the day moves on (gusts, launches, harmony changes, a new day in a near key),
 * resting is silent, a key launches a kite, Ambience is the bed alone. Then the render's cost, ns a frame.
 *   kite_test                      the checks
 *   kite_test dump HILL MIN [DAY]  what a hill plays: every launch, retune, gust, chord; a snapshot every 15 s
 *   kite_test demos DIR            the demo clips: NN-<hill>.wav (20 s, settled) and 00-listen.wav (3 min) */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "kite.h"

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

static void hill(int i)
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
    int bad_pc, bad_iv, snaps;
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

static int ji_hill, keyed;
static void dbg_key(int what, float t, int k, float p, float x)
{
    (void)t, (void)k, (void)p;
    if (what == DB_LAUNCH && x > 0.0f)
        keyed++;
}
/* the ear: every line sounding a note of the chord (where it is going: a retune glides there in a second or two);
 * no two a second or minor ninth apart */
static void listen(stats_t *s)
{
    int i, j;
    s->snaps++;
    for (i = 0; i < EN_NK; i++) {
        float p = EN_KITE[i].to;
        if (EN_KITE[i].state != KS_FLY || EN_KITE[i].amp < 0.1f)
            continue;
        if (!ji_hill && !(en_chord_pcs >> ((int)roundf(p) % 12) & 1u))
            s->bad_pc++;
        for (j = i + 1; j < EN_NK; j++) {
            float d = fabsf(p - EN_KITE[j].to);
            if (EN_KITE[j].state != KS_FLY || EN_KITE[j].amp < 0.1f)
                continue;
            if (d < 2.4f || (d > 12.5f && d < 13.5f))
                s->bad_iv++;
        }
    }
}

static void run(double sec, stats_t *s)
{
    static int32_t buf[512];
    uint32_t b, nb = (uint32_t)(sec * 44100.0 / 256.0), j;
    for (b = 0; b < nb; b++) {
        en_render(buf, 256, 4096);
        if (b % 172u == 0u)
            listen(s);
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
static double rms_db(const stats_t *s) { return db(sqrt(s->sum2 / (double)(s->n ? s->n : 1))); }

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void nname(float p, char *b)
{
    int n = (int)roundf(p);
    float c = (p - (float)n) * 100.0f;
    if (fabsf(c) > 2.0f)
        sprintf(b, "%s%d%+.0fc", EN_NOTE_NAME[(n % 12 + 12) % 12], n / 12 - 1, (double)c);
    else
        sprintf(b, "%s%d", EN_NOTE_NAME[(n % 12 + 12) % 12], n / 12 - 1);
}
static void pcs_text(uint16_t m, char *b)
{
    int p;
    b[0] = 0;
    for (p = 0; p < 12; p++)
        if (m >> p & 1u) {
            strcat(b, EN_NOTE_NAME[p]);
            strcat(b, " ");
        }
}
static void dbg(int what, float t, int k, float p, float x)
{
    char a[16], b[16], c[64];
    int m = (int)(t / 60.0f), s = (int)t % 60;
    switch (what) {
    case DB_LAUNCH: nname(p, a); printf("%3d:%02d  launch  kite %d on %s%s\n", m, s, k, a, x > 0.0f ? " (key)" : ""); break;
    case DB_RETUNE: nname(p, a); nname(x, b); printf("%3d:%02d  retune  kite %d %s -> %s\n", m, s, k, a, b); break;
    case DB_LOOSE: nname(p, a); nname(x, b); printf("%3d:%02d  LOOSE   kite %d %s falling to %s\n", m, s, k, a, b); break;
    case DB_LAND: nname(p, a); printf("%3d:%02d  land    kite %d (%s)\n", m, s, k, a); break;
    case DB_GUST: printf("%3d:%02d  gust    +%.2f on %.2f\n", m, s, (double)p, (double)x); break;
    case DB_CHORD:
        nname(p, a);
        pcs_text((uint16_t)x, c);
        printf("%3d:%02d  CHORD   %d: bass %s | %s\n", m, s, k, a, c);
        break;
    case DB_DAY: printf("%3d:%02d  ===== day %d, key +%d\n", m, s, k, (int)p); break;
    case DB_PLUCK: nname(p, a); printf("%3d:%02d  pluck   %s %.2f\n", m, s, a, (double)x); break;
    }
}

static int dump(int st, double min, int daylen)
{
    stats_t a = {0};
    int i, t;
    char c[16], clk[8];
    en_dbg = dbg;
    en_init();
    hill(st);
    en_set(P_DAYLEN, daylen);
    en_play(1);
    ji_hill = st == 2;
    printf("== %s (%s)\n", en_style_name(st), en_style_mode(st));
    for (t = 0; t < (int)(min * 4.0); t++) {
        run(15.0, &a);
        en_clock(en_daypos, clk);
        printf("%3d:%02d  [%s %-6s wind %.2f gust %.2f] ", (t + 1) * 15 / 60, (t + 1) * 15 % 60, clk,
               en_phase_name(en_phase), (double)en_wind, (double)en_gust);
        for (i = 0; i < EN_NK; i++)
            if (EN_KITE[i].state != KS_OFF) {
                nname(EN_KITE[i].pitch, c);
                printf(" %s%s:%.2f", EN_KITE[i].state == KS_FLY ? "" : EN_KITE[i].state == KS_LAND ? "v" : "~", c,
                       (double)EN_KITE[i].amp);
            }
        printf("\n");
    }
    printf("peak %.1f dB, rms %.1f dB, %d snapshots, %d off the chord, %d clashes; %u launches, %u gusts, %u breaks, %u changes\n",
           db(a.peak), rms_db(&a), a.snaps, a.bad_pc, a.bad_iv, en_launches, en_gusts, en_breaks, en_changes);
    en_dbg = 0;
    return 0;
}

static void slug(const char *s, char *d)
{
    for (; *s; s++)
        if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9'))
            *d++ = (char)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s);
        else if (*s == ' ')
            *d++ = '-';
    *d = 0;
}

static int demos(const char *dir)
{
    char path[512], sl[64];
    int i;
    for (i = 0; i < EN_NSTYLES; i++) {
        stats_t a = {0};
        en_init();
        hill(i);
        en_play(1);
        run(150.0, &a);                          /* settled: the kites up */
        slug(en_style_name(i), sl);
        snprintf(path, sizeof path, "%s/%02d-%s.wav", dir, i + 1, sl);
        wav_open(path);
        run(20.0, &a);
        wav_close();
        printf("%s\n", path);
    }
    {
        stats_t a = {0};
        en_init();
        hill(0);
        en_play(1);
        snprintf(path, sizeof path, "%s/00-listen.wav", dir);
        wav_open(path);
        run(180.0, &a);
        wav_close();
        printf("%s\n", path);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i;
    char t[16];
    if (argc >= 4 && !strcmp(argv[1], "dump"))
        return dump(atoi(argv[2]), atof(argv[3]), argc >= 5 ? atoi(argv[4]) : 2);
    if (argc >= 3 && !strcmp(argv[1], "demos"))
        return demos(argv[2]);

    /* the pure parts */
    for (i = 0; i < EN_NSTYLES; i++) {
        int16_t p[P_NPARAMS];
        int k, j;
        en_style_recipe(i, p);
        for (k = 0; k < P_NSTYLE; k++)
            CHECK(p[k] >= en_param_info(k)->lo && p[k] <= en_param_info(k)->hi, "%s param %d", en_style_name(i), k);
        CHECK(strlen(en_style_name(i)) <= 16, "%s: long", en_style_name(i));
        for (j = 0; j < i; j++)
            CHECK(strcmp(en_style_name(i), en_style_name(j)), "%s twice", en_style_name(i));
    }
    en_clock(0.0f, t);
    CHECK(!strcmp(t, "6:00"), "clock %s", t);
    en_clock(0.5f, t);
    CHECK(!strcmp(t, "13:30"), "clock %s", t);
    en_param_text(P_DAYLEN, 2, t);
    CHECK(!strcmp(t, "1 h"), "day %s", t);

    /* every hill: sound, no NaN, the notes fit, peaks under -1 dBFS; a long day for each at 20-minute days */
    for (i = 0; i < EN_NSTYLES; i++) {
        stats_t a = {0};
        uint32_t l0;
        en_init();
        hill(i);
        en_set(P_DAYLEN, 0);
        en_play(1);
        ji_hill = i == 2;
        l0 = en_launches;
        run(25.0, &a);
        CHECK(en_launches > l0, "%s: no kite up", en_style_name(i));
        run(1500.0, &a);
        CHECK(a.nan == 0, "%s: NaN", en_style_name(i));
        CHECK(db(a.peak) < -1.0, "%s: peak %.1f dB", en_style_name(i), db(a.peak));
        CHECK(rms_db(&a) > -40.0, "%s: rms %.1f dB", en_style_name(i), rms_db(&a));
        CHECK(a.bad_pc == 0, "%s: %d lines off the chord", en_style_name(i), a.bad_pc);
        CHECK(a.bad_iv == 0, "%s: %d clashes", en_style_name(i), a.bad_iv);
        CHECK(en_changes >= 4, "%s: %u chord changes", en_style_name(i), en_changes);
        CHECK(en_gusts >= 5, "%s: %u gusts", en_style_name(i), en_gusts);
        CHECK(en_day >= 2, "%s: day %u", en_style_name(i), en_day);
        printf("  %-16s peak %5.1f dB rms %5.1f dB  %3u launches %3u gusts %2u breaks %3u changes\n", en_style_name(i),
               db(a.peak), rms_db(&a), en_launches, en_gusts, en_breaks, en_changes);
    }
    /* every knob at its top: still under -1 dBFS */
    {
        stats_t a = {0};
        int k;
        en_init();
        hill(4);
        for (k = 0; k < P_NPARAMS; k++)
            en_set(k, en_param_info(k)->hi);
        en_set(P_DAYLEN, 0);
        en_play(1);
        run(1300.0, &a);
        CHECK(db(a.peak) < -1.0, "all up: peak %.1f dB", db(a.peak));
        CHECK(a.nan == 0, "all up: NaN");
        printf("  all knobs up     peak %5.1f dB rms %5.1f dB\n", db(a.peak), rms_db(&a));
    }
    /* resting: silent; a key: a kite goes up; Ambience: the bed alone */
    {
        stats_t a = {0}, b = {0}, c = {0}, d = {0};
        uint32_t l0;
        en_init();
        hill(0);
        en_play(1);
        run(90.0, &a);
        en_play(0);
        run(5.0, &a);
        run(3.0, &b);
        CHECK(db(b.peak) < -80.0, "resting: %.1f dB", db(b.peak));
        en_play(1);
        run(20.0, &a);
        l0 = en_launches;
        en_dbg = dbg_key;
        en_note(67, 100);
        run(8.0, &a);
        CHECK(keyed == 1 && en_launches > l0, "a key: no kite");
        en_dbg = 0;
        en_set(P_AMBIENCE, 0);
        en_set(P_DRONE, 0);
        en_set(P_KITES, 1);
        run(240.0, &a);                          /* the others come down */
        run(30.0, &c);
        en_set(P_AMBIENCE, 100);
        run(30.0, &d);
        printf("  ambience off rms %.1f dB, full rms %.1f dB\n", rms_db(&c), rms_db(&d));
        CHECK(rms_db(&d) > rms_db(&c) + 3.0, "ambience: %.1f vs %.1f", rms_db(&d), rms_db(&c));
    }
    /* the world as kept comes back */
    en_init();
    en_world(5, 7000, 2);
    hill(0);
    en_play(1);
    {
        stats_t a = {0};
        run(1.0, &a);
    }
    CHECK(en_day == 5 && en_phase == PH_EVENING && en_chord_i == 2, "world: day %u phase %d chord %d", en_day, en_phase,
          en_chord_i);

    /* the cost: ns a frame, each hill with every kite up and a big gust's plucks */
    {
        static int32_t buf[512];
        double worst = 0.0;
        int wi = 0;
        for (i = 0; i < EN_NSTYLES; i++) {
            stats_t a = {0};
            double t0, dt;
            int b;
            en_init();
            hill(i);
            en_set(P_KITES, EN_NK);
            en_set(P_WIND, 100);
            en_set(P_AMBIENCE, 100);
            en_set(P_DRONE, 100);
            en_world(1, 7200, 0);
            en_play(1);
            run(240.0, &a);
            t0 = now();
            for (b = 0; b < 3000; b++)
                en_render(buf, 256, 4096);
            dt = (now() - t0) * 1e9 / (3000.0 * 256.0);
            printf("  %-16s %.1f ns/frame, %d kites\n", en_style_name(i), dt, en_kites);
            if (dt > worst)
                worst = dt, wi = i;
        }
        printf("render: busiest %s %.1f ns/frame\n", en_style_name(wi), worst);
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
