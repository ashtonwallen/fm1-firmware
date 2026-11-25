/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * RIGGING: ashtonwallen, 2026 */
/* The instrument on the host. The checks: every preset makes sound, nothing is NaN, the peaks stay under -1 dBFS,
 * resting is quiet; and the music's rules over a day at sea per preset (the world alone, no synthesis): every rope
 * sounding is a tone of the chord tuned now, no two sounding ropes a semitone or a minor ninth apart, the chord
 * moves only to its neighbour in the chain (or a near key), the weather and the helm move on. Then the levels (the
 * music, the Ambience bed alone) and the render's cost in ns a frame, in a squall (the whole rig sounding).
 *   rig_test                        the checks
 *   rig_test dump PRESET MINUTES    what the ship plays: chords and their tunings, each rope as it starts, the weather
 *   rig_test demos DIR              the demo clips: NN-<preset>.wav (20 s, settled) and 00-listen.wav (3 min) */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "rig.h"

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

static void preset(int i)
{
    int16_t p[P_NPARAMS];
    int k;
    en_style_recipe(i, p);
    en_style(i);
    for (k = 0; k < P_NSTYLE; k++)
        en_set(k, p[k]);
}
static void fresh(int i)
{
    static int32_t buf[512];
    en_dbg_fast = 0;
    en_dbg_mute = 0;
    en_init();
    preset(i);
    en_play(1);
    en_render(buf, 1, 4096);
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
            if (wav) {
                int16_t x = (int16_t)(v * 32767.0);
                fwrite(&x, 2, 1, wav);
            }
        }
        if (wav)
            wav_n += 256u;
        s->n += 512;
    }
}
static double db(double x) { return 20.0 * log10(x + 1e-12); }
static double rms_db(const stats_t *s) { return db(sqrt(s->sum2 / (double)(s->n ? s->n : 1))); }

/* ---- the music's rules, block by block */
static int in_chord(float midi, const int *c, int n)
{
    int k, cents = (int)lroundf(midi * 100.0f) % 1200;
    for (k = 0; k < n; k++) {
        int d = abs(cents - c[k]);
        if (d < 6 || d > 1194)
            return 1;
    }
    return 0;
}
static uint32_t bad_tone, bad_iv, blocks, snd_sum, snd_max, hist[EN_NR + 1];
static void rules(void)
{
    float m[EN_NR], a[EN_NR];
    int c[6], nc = en_dbg_chord_cents(0, c), n = en_dbg_sounding(m, a), i, j;
    blocks++;
    snd_sum += (uint32_t)n;
    hist[n]++;
    if ((uint32_t)n > snd_max)
        snd_max = (uint32_t)n;
    for (i = 0; i < n; i++) {
        if (!in_chord(m[i], c, nc)) {
            if (bad_tone < 5)
                printf("  off the chord: %.2f (amp %.2f) at %u ms\n", m[i], a[i], en_now_ms);
            bad_tone++;
        }
        for (j = 0; j < i; j++) {
            float d = fabsf(m[i] - m[j]);
            if ((d > 0.7f && d < 1.3f) || (d > 12.7f && d < 13.3f)) {
                if (bad_iv < 5)
                    printf("  a semitone: %.2f %.2f at %u ms\n", m[i], m[j], en_now_ms);
                bad_iv++;
            }
        }
    }
}
static int last_sect = -1;
static uint32_t bad_step;
static void on_chord(uint32_t ms, int sector, const float *midi, int n)
{
    (void)ms, (void)midi, (void)n;
    if (last_sect >= 0 && abs(sector - last_sect) > 1)
        bad_step++;                              /* (a near key keeps the sector: a step of 0) */
    last_sect = sector;
}

static void day_at_sea(int p)
{
    static int32_t buf[512];
    uint32_t b, nb = (uint32_t)(24.0 * 3600.0 * 44100.0 / 256.0), bells = en_bell_no;
    uint32_t c0, g0, s0, k0;
    double wind_sum = 0.0;
    fresh(p);
    en_dbg_fast = 1;
    bad_tone = bad_iv = blocks = snd_sum = snd_max = bad_step = 0;
    memset(hist, 0, sizeof hist);
    last_sect = -1;
    en_dbg_chord = on_chord;
    c0 = en_n_chords, g0 = en_n_gusts, s0 = en_n_squalls, k0 = en_n_courses;
    for (b = 0; b < nb; b++) {
        en_render(buf, 256, 4096);
        rules();
        wind_sum += en_wind;
    }
    en_dbg_chord = 0;
    en_dbg_fast = 0;
    printf("  %-16s 24 h: chords %4u, courses %3u, gusts %5u, squalls %2u, bells %2u; wind %.2f; ropes %.1f "
           "(silent %2.0f%%, 1-3 %2.0f%%, 4+ %2.0f%%, max %u)\n",
           en_style_name(p), en_n_chords - c0, en_n_courses - k0, en_n_gusts - g0, en_n_squalls - s0,
           en_bell_no - bells, wind_sum / nb, (double)snd_sum / blocks, 100.0 * hist[0] / blocks,
           100.0 * (hist[1] + hist[2] + hist[3]) / blocks, 100.0 * (blocks - hist[0] - hist[1] - hist[2] - hist[3]) / blocks,
           snd_max);
    CHECK(bad_tone == 0, "%s: %u blocks with a rope off the chord", en_style_name(p), bad_tone);
    CHECK(bad_iv == 0, "%s: %u semitones between sounding ropes", en_style_name(p), bad_iv);
    CHECK(bad_step == 0, "%s: %u chord jumps", en_style_name(p), bad_step);
    CHECK(en_n_chords - c0 >= 20, "%s: the harmony hardly moves", en_style_name(p));
    CHECK(en_n_courses - k0 >= 10, "%s: the helm hardly moves", en_style_name(p));
    CHECK(en_bell_no - bells >= 47, "%s: the bells", en_style_name(p));
}

static double nsframe(int p)
{
    static int32_t buf[512];
    struct timespec t0, t1;
    int b;
    fresh(p);
    en_set(P_WIND, 100);
    en_set(P_GUSTS, 100);
    en_set(P_BREATH, 60);
    en_dbg_squall();
    {
        stats_t s = {0};
        run(40.0, &s);
    }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (b = 0; b < 3000; b++)
        en_render(buf, 256, 4096);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return ((double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec)) / (3000.0 * 256.0);
}

static void nname(float m, char *b)
{
    int n = (int)lroundf(m);
    float c = (m - (float)n) * 100.0f;
    if (fabsf(c) > 3.0f)
        sprintf(b, "%s%d%+.0f", EN_NOTE_NAME[n % 12], n / 12 - 1, c);
    else
        sprintf(b, "%s%d", EN_NOTE_NAME[n % 12], n / 12 - 1);
}
static void d_chord(uint32_t ms, int sector, const float *midi, int n)
{
    char t[16], c[16];
    int i;
    en_chord_text(c);
    printf("%6.1f  CHORD %-10s (%s)  ropes:", ms / 60000.0, c, en_sector_name(sector));
    for (i = 0; i < n; i++) {
        nname(midi[i], t);
        printf(" %s", t);
    }
    printf("\n");
}
static void d_onset(uint32_t ms, int rope, float midi, float amp)
{
    char t[16];
    nname(midi, t);
    printf("%6.1f    rope %2d %-6s %.2f   wind %.2f%s\n", ms / 60000.0, rope, t, amp, en_wind,
           en_squall ? " SQUALL" : "");
}
static int dump(int p, int minutes)
{
    static int32_t buf[512];
    int m;
    uint32_t b, bells = 0;
    fresh(p);
    en_dbg_fast = 1;
    en_dbg_chord = d_chord;
    en_dbg_onset = d_onset;
    printf("%s (%s)\n", en_style_name(p), en_style_mode(p));
    for (m = 0; m < minutes; m++) {
        for (b = 0; b < (uint32_t)(60.0 * 44100.0 / 256.0); b++) {
            en_render(buf, 256, 4096);
            if (en_bell_no != bells) {
                bells = en_bell_no;
                printf("%6.1f  BELLS %u\n", en_now_ms / 60000.0, en_bells);
            }
        }
        printf("%6d  -- heading %3.0f, wind from %3.0f (%+4.0f off the bow) force %d%s%s, sounding %u\n", m + 1,
               en_heading, en_wind_dir, en_rel, en_beaufort(en_wind), en_squall ? ", squall" : "",
               en_calm ? ", calm" : "", en_sounding);
    }
    return 0;
}

static int demos(const char *dir)
{
    char path[512], name[64];
    int i, k;
    stats_t s;
    for (i = 0; i < EN_NSTYLES; i++) {
        fresh(i);
        en_dbg_fast = 1;
        {
            static int32_t buf[512];
            uint32_t b;
            for (b = 0; b < (uint32_t)(150.0 * 44100.0 / 256.0); b++)
                en_render(buf, 256, 4096);
        }
        en_dbg_fast = 0;
        memset(&s, 0, sizeof s);
        run(12.0, &s);
        for (k = 0; en_style_name(i)[k] && k < 63; k++) {
            char ch = en_style_name(i)[k];
            name[k] = (ch == ' ' || ch == '\'' || ch == '.') ? (ch == ' ' ? '-' : 0) : ch;
            if (!name[k])
                name[k] = '_';
        }
        name[k] = 0;
        {                                        /* (the apostrophes and dots out) */
            int r = 0, w = 0;
            for (; name[r]; r++)
                if (name[r] != '_')
                    name[w++] = name[r];
            name[w] = 0;
        }
        snprintf(path, sizeof path, "%s/%02d-%s.wav", dir, i + 1, name);
        wav_open(path);
        memset(&s, 0, sizeof s);
        run(20.0, &s);
        wav_close();
        printf("%s: rms %.1f dB, peak %.1f dB\n", path, rms_db(&s), db(s.peak));
    }
    fresh(0);
    snprintf(path, sizeof path, "%s/00-listen.wav", dir);
    wav_open(path);
    memset(&s, 0, sizeof s);
    run(180.0, &s);
    wav_close();
    printf("%s: rms %.1f dB, peak %.1f dB\n", path, rms_db(&s), db(s.peak));
    return 0;
}

int main(int argc, char **argv)
{
    int i;
    if (argc > 3 && !strcmp(argv[1], "dump"))
        return dump(atoi(argv[2]), atoi(argv[3]));
    if (argc > 2 && !strcmp(argv[1], "demos"))
        return demos(argv[2]);

    /* names: short enough for the screen, ASCII */
    for (i = 0; i < EN_NSTYLES; i++) {
        const char *n = en_style_name(i);
        int k;
        CHECK(strlen(n) <= 16, "%s: too long", n);
        for (k = 0; n[k]; k++)
            CHECK(n[k] >= 32 && n[k] < 127, "%s: not ASCII", n);
    }
    /* every preset: sound in the first two minutes, then ten minutes of peaks and level, music and bed */
    for (i = 0; i < EN_NSTYLES; i++) {
        stats_t a = {0}, b = {0}, m = {0}, e = {0};
        fresh(i);
        run(90.0, &a);
        run(600.0, &b);
        en_dbg_mute = 2;                         /* the music alone, then the bed alone */
        run(60.0, &m);
        en_dbg_mute = 1 | 4;
        run(60.0, &e);
        en_dbg_mute = 0;
        printf("  %-16s first 90 s rms %6.1f dB; next 10 min peak %5.1f dB, rms %6.1f dB; music %6.1f, bed %6.1f dB\n",
               en_style_name(i), rms_db(&a), db(b.peak), rms_db(&b), rms_db(&m), rms_db(&e));
        CHECK(a.nan == 0 && b.nan == 0, "%s: NaN", en_style_name(i));
        CHECK(rms_db(&a) > -55.0, "%s: silent at the start", en_style_name(i));
        CHECK(db(b.peak) < -1.0, "%s: peak %.1f dB", en_style_name(i), db(b.peak));
        CHECK(rms_db(&b) > -50.0, "%s: too quiet", en_style_name(i));
    }
    /* a squall at full wind: the peaks hold */
    for (i = 0; i < EN_NSTYLES; i += 3) {
        stats_t s = {0};
        fresh(i);
        en_set(P_WIND, 100);
        en_set(P_GUSTS, 100);
        en_set(P_AMB, 100);
        en_set(P_HULL, 100);
        en_set(P_BELL, 100);
        en_dbg_squall();
        run(180.0, &s);
        CHECK(db(s.peak) < -1.0, "%s in a squall: peak %.1f dB", en_style_name(i), db(s.peak));
        CHECK(en_sounding >= 6, "%s in a squall: only %u ropes", en_style_name(i), en_sounding);
    }
    /* resting: quiet within seconds */
    {
        stats_t s = {0};
        fresh(0);
        run(60.0, &s);
        en_play(0);
        memset(&s, 0, sizeof s);
        run(8.0, &s);
        memset(&s, 0, sizeof s);
        run(4.0, &s);
        CHECK(db(s.peak) < -65.0, "resting: peak %.1f dB", db(s.peak));
    }
    /* the helm: a trim moves the course; held, the course stays */
    {
        stats_t s = {0};
        float h;
        fresh(0);
        run(5.0, &s);
        h = en_heading;
        en_trim(10);
        run(30.0, &s);
        CHECK(fabsf(en_heading - h) > 8.0f && fabsf(en_heading - h) < 12.0f, "trim: %.1f -> %.1f", h, en_heading);
        en_hold(1);
        h = en_heading;
        en_dbg_fast = 1;
        run(7200.0, &s);
        en_dbg_fast = 0;
        CHECK(fabsf(en_heading - h) < 40.0f, "held: %.1f -> %.1f", h, en_heading);
        en_about();
        en_dbg_fast = 1;
        run(200.0, &s);
        en_dbg_fast = 0;
        CHECK(fabsf(en_heading - h) > 10.0f, "about: %.1f -> %.1f", h, en_heading);
    }
    /* the world kept: put back, it goes on where it was */
    {
        en_world_t w, w2;
        stats_t s = {0};
        fresh(0);
        en_dbg_fast = 1;
        run(3000.0, &s);
        en_world_get(&w);
        fresh(0);
        en_world_put(&w);
        run(0.1, &s);
        en_world_get(&w2);
        en_dbg_fast = 0;
        CHECK(fabsf(w2.heading - w.heading) < 1.0f && w2.watch_s - w.watch_s < 2u, "the world kept");
    }
    /* a day at sea per preset: the music's rules */
    for (i = 0; i < EN_NSTYLES; i++)
        day_at_sea(i);
    /* the cost: the whole rig in a squall */
    {
        double worst = 0.0;
        for (i = 0; i < EN_NSTYLES; i++) {
            double ns = nsframe(i);
            printf("  %-16s %6.1f ns/frame in a squall, %u ropes\n", en_style_name(i), ns, en_sounding);
            if (ns > worst)
                worst = ns;
        }
        printf("  busiest %.1f ns/frame: %.2fx AMB-1 Drift's Low Tide (168 ns)\n", worst, worst / 168.0);
    }
    printf(fails ? "%d FAILED\n" : "all engine checks passed\n", fails);
    return fails ? 1 : 0;
}
