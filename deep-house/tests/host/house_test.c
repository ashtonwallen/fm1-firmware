/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * DEEP HOUSE DECKS: ashtonwallen, 2026 */
/* The instrument on the host: the pure parts (titles, chord names, the sets' recipes, the patterns) and the render:
 * every set makes sound, nothing is NaN, the peaks stay under -1 dBFS over long runs, the DJ blends record into
 * record (the next key a neighbour), a blend asked for comes, the keys play along. Then the render's cost, in ns a
 * frame (the CPU check: compare with AMB-1's busiest piece).
 *   house_test                      the tests
 *   house_test dump SET SEED BARS   what each deck plays, bar by bar (to read as a musician would)
 *   house_test wav DIR              a minute of each set */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "house.h"

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

static void set(int i)
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
static uint32_t wav_frames;
static void wav_open(const char *path)
{
    static const uint8_t H[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
                                  1, 0, 2, 0, 0x44, 0xAC, 0, 0, 0x10, 0xB1, 2, 0, 4, 0, 16, 0,
                                  'd', 'a', 't', 'a', 0, 0, 0, 0};
    wav = fopen(path, "wb");
    if (wav)
        fwrite(H, 1, 44, wav);
    wav_frames = 0;
}
static void wav_close(void)
{
    uint32_t d = wav_frames * 4u, r = d + 36u;
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
            wav_frames += 256u;
    }
}
static double db(double x) { return 20.0 * log10(x + 1e-12); }
static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------ the dump --- */
static const char *nn(int n, char *b)
{
    n &= 127;
    snprintf(b, 16, "%s%d", EN_NOTE_NAME[n % 12], n / 12 - 1);
    return b;
}
static char line[2][2048];
static int last_chord_step[2] = {-1, -1}, bar_step[2], drums[2][8];
static void flush(int d)
{
    if (line[d][0]) {
        static const char K[] = "KSOHR";
        int i;
        printf("%s  drums:", line[d]);
        for (i = 0; i < 5; i++)
            if (drums[d][i])
                printf("%c%d", K[i], drums[d][i]);
        printf("\n");
    }
    line[d][0] = 0;
    memset(drums[d], 0, sizeof drums[d]);
}
static void logf_(int d, uint32_t step, char kind, int note, int len, float vel)
{
    char *l = line[d], t[16], c[16];
    int s = (int)step - bar_step[d];
    size_t o = strlen(l);
    if (o > 1900)
        return;
    switch (kind) {
    case '|':
        flush(d);
        bar_step[d] = (int)step;
        last_chord_step[d] = -1;
        snprintf(l, 2048, "%c bar %3d %-6s %2d", 'A' + d, en_deck[d].bar, EN_SECT_NAME[note], (int)vel);
        break;
    case 'C':
        if (last_chord_step[d] != (int)step) {
            last_chord_step[d] = (int)step;
            snprintf(l + o, 2048 - o, " | C%d/%d:%s", s, len, nn(note, t));
        } else {
            snprintf(l + o, 2048 - o, ",%s", nn(note, t));
        }
        break;
    case 'P': snprintf(l + o, 2048 - o, " P:%s", nn(note, t)); break;
    case 'B': snprintf(l + o, 2048 - o, " B%d:%s", s, nn(note, t)); break;
    case 'L': snprintf(l + o, 2048 - o, " L%d/%d:%s", s, len, nn(note, t)); break;
    case 'V': snprintf(l + o, 2048 - o, " V%d/%d:%s", s, len, nn(note, t)); break;
    case 'K': drums[d][0]++; break;
    case 'S': drums[d][1]++; break;
    case 'O': drums[d][2]++; break;
    case 'H': drums[d][3]++; break;
    case 'R': drums[d][4]++; break;
    }
    (void)c;
}

static int dump(int st, uint32_t seed, int bars)
{
    stats_t a = {0};
    int b, last_no[2] = {0, 0};
    en_log = logf_;
    en_init();
    set(st);
    en_set(P_MIX, 1);
    en_track(0, seed);
    en_play(1);
    run(0.01, &a);
    printf("set %s, %d BPM\n", en_style_name(st), en_bpm);
    for (b = 0; b < bars; b++) {
        int d;
        run(60.0 / en_bpm * 4.0, &a);
        for (d = 0; d < 2; d++)
            if (en_deck[d].loaded && en_deck[d].no != (uint32_t)last_no[d]) {
                char t[32], c[16];
                last_no[d] = (int)en_deck[d].no;
                en_title(en_deck[d].seed, t);
                printf("== deck %c: record %u \"%s\" (%s), key %sm, lead %d, chord now %s\n", 'A' + d,
                       (unsigned)en_deck[d].no, t, en_mix_tag(en_deck[d].seed, en_deck[d].lead),
                       EN_NOTE_NAME[en_deck[d].key], en_deck[d].lead,
                       en_chord_name(en_chord_root, en_chord_q, c));
            }
    }
    flush(0);
    flush(1);
    printf("peak %.1f dB, rms %.1f dB, mixes %u\n", db(a.peak), db(sqrt(a.sum2 / a.n)), (unsigned)en_mixes);
    return 0;
}

int main(int argc, char **argv)
{
    int i;
    char t[32], u[32];
    if (argc > 1 && !strcmp(argv[1], "time"))
        goto timing;
    if (argc > 4 && !strcmp(argv[1], "dump"))
        return dump(atoi(argv[2]), (uint32_t)strtoul(argv[3], 0, 0), atoi(argv[4]));
    if (argc > 2 && !strcmp(argv[1], "wav")) {
        for (i = 0; i < EN_NSTYLES; i++) {
            stats_t a = {0};
            char path[256];
            en_init();
            set(i);
            en_track(0, 0xD33Fu + (uint32_t)i);
            en_play(1);
            run(40.0, &a);
            snprintf(path, sizeof path, "%s/set%02d.wav", argv[2], i);
            wav_open(path);
            run(60.0, &a);
            wav_close();
        }
        return 0;
    }
    /* the pure parts */
    for (i = 0; i < 3000; i++) {
        en_title(en_track_rand(1234u, (uint32_t)i), t);
        CHECK(strlen(t) > 2 && strlen(t) <= 16, "title %d: '%s'", i, t);
    }
    en_title(42u, t);
    en_title(42u, u);
    CHECK(!strcmp(t, u), "the same seed, the same title");
    CHECK(!strcmp(en_chord_name(9, 1, t), "Am9"), "chord name: %s", t);
    for (i = 0; i < EN_NSTYLES; i++) {
        int16_t p[P_NPARAMS];
        int k;
        en_style_recipe(i, p);
        for (k = 0; k < P_NSTYLE; k++)
            CHECK(p[k] >= en_param_info(k)->lo && p[k] <= en_param_info(k)->hi, "set %d param %d: %d", i, k, p[k]);
        CHECK(p[P_TEMPO] >= 118 && p[P_TEMPO] <= 124, "set %d tempo %d", i, p[P_TEMPO]);
        CHECK(strlen(en_style_name(i)) <= 15, "set name %s too long", en_style_name(i));
    }
    en_param_text(P_SWING, 100, t);
    CHECK(!strcmp(t, "66%"), "swing 100: %s", t);
    en_param_text(P_FILTER, -25, t);
    CHECK(!strcmp(t, "LP 50"), "filter -25: %s", t);
    /* every set: sound within seconds, bounded over a long run, blends happen, keys stay neighbours */
    for (i = 0; i < EN_NSTYLES; i++) {
        stats_t a = {0}, b = {0};
        uint32_t m0;
        int key0, moves = 0, bad = 0, lastkey;
        en_init();
        set(i);
        en_set(P_MIX, 0);
        en_track((uint32_t)i * 10u, 0xC0FFEEu + (uint32_t)i);
        en_play(1);
        run(30.0, &a);
        key0 = en_deck[en_master].key;
        lastkey = key0;
        m0 = en_mixes;
        {
            int s;
            for (s = 0; s < 900; s++) {          /* 15 minutes, a second at a time: each new record's key */
                run(1.0, &b);
                if (en_deck[en_master].key != lastkey) {
                    int dk = (en_deck[en_master].key - lastkey + 12) % 12;
                    moves++;
                    if (dk != 5 && dk != 7)
                        bad++;
                    lastkey = en_deck[en_master].key;
                }
            }
        }
        printf("%-15s first 30 s rms %5.1f dB peak %5.1f dB; next 15 min peak %5.1f dB, rms %5.1f dB, blends %u, key "
               "moves %d\n",
               en_style_name(i), db(sqrt(a.sum2 / a.n)), db(a.peak), db(b.peak), db(sqrt(b.sum2 / b.n)),
               (unsigned)(en_mixes - m0), moves);
        CHECK(a.nan == 0 && b.nan == 0, "%s: NaN", en_style_name(i));
        CHECK(db(sqrt(a.sum2 / a.n)) > -34.0, "%s: too quiet", en_style_name(i));
        CHECK(db(a.peak) < -1.0 && db(b.peak) < -1.0, "%s: peaks over -1 dBFS", en_style_name(i));
        CHECK(en_mixes - m0 >= 2, "%s: the DJ did not blend", en_style_name(i));
        CHECK(bad == 0, "%s: %d key moves not to a neighbour", en_style_name(i), bad);
    }
    /* the extremes: everything up, then down */
    {
        stats_t a = {0};
        int k;
        en_init();
        set(2);
        for (k = 0; k < P_NSTYLE; k++)
            en_set(k, en_param_info(k)->hi);
        en_set(P_MIX, 0);
        en_play(1);
        run(400.0, &a);
        printf("everything up: peak %.1f dB rms %.1f dB\n", db(a.peak), db(sqrt(a.sum2 / a.n)));
        CHECK(a.nan == 0 && db(a.peak) < -1.0, "everything up: peak %.1f", db(a.peak));
        for (k = 0; k < P_NSTYLE; k++)
            en_set(k, en_param_info(k)->lo);
        en_set(P_FILTER, -50);
        memset(&a, 0, sizeof a);
        run(120.0, &a);
        CHECK(a.nan == 0 && db(a.peak) < -1.0, "everything down");
        en_set(P_FILTER, 50);
        run(30.0, &a);
        CHECK(a.nan == 0 && db(a.peak) < -1.0, "high-passed");
    }
    /* a blend asked for: under way within four bars, done within its length */
    {
        stats_t a = {0};
        uint32_t m0;
        en_init();
        set(0);
        en_set(P_MIX, 0);
        en_play(1);
        run(70.0, &a);
        CHECK(!en_mixing, "no blend yet at 70 s");
        m0 = en_mixes;
        en_mix(0);
        run(9.0, &a);
        CHECK(en_mixing, "asked: blending within four bars");
        run(40.0, &a);
        CHECK(en_mixes == m0 + 1 && !en_mixing, "the blend done in 16 bars");
        CHECK(en_deck[en_master].on && !en_deck[en_master ^ 1].on, "one deck on after");
    }
    /* held: no blend for 15 minutes */
    {
        stats_t a = {0};
        uint32_t no;
        en_init();
        set(1);
        en_set(P_MIX, 0);
        en_hold(1);
        en_play(1);
        no = en_deck[en_master].no;
        run(900.0, &a);
        CHECK(en_mixes == 0 && en_deck[en_master].no == no, "held: blended %u", (unsigned)en_mixes);
    }
    /* stopped: silence; running again: sound */
    {
        stats_t a = {0}, b = {0};
        en_init();
        set(0);
        en_play(1);
        run(20.0, &a);
        en_play(0);
        run(5.0, &a);
        run(2.0, &b);
        CHECK(db(b.peak) < -70.0, "stopped: peak %.1f dB", db(b.peak));
        en_play(1);
        memset(&b, 0, sizeof b);
        run(8.0, &b);
        CHECK(db(sqrt(b.sum2 / b.n)) > -40.0, "running again: rms %.1f", db(sqrt(b.sum2 / b.n)));
    }
    /* the keys play along */
    {
        stats_t a = {0};
        en_init();
        run(3.0, &a);
        memset(&a, 0, sizeof a);
        en_note(60, 100);
        run(0.5, &a);
        en_note(60, 0);
        CHECK(db(a.peak) > -40.0, "a key: peak %.1f dB", db(a.peak));
    }
    /* the cost: ns a frame, each set, over a stretch with a blend in it */
timing:
    {
        static int32_t buf[512];
        double worst = 0.0;
        int wi = 0;
        for (i = 0; i < EN_NSTYLES; i++) {
            double t0, dt, tot = 0.0, peak = 0.0;
            int b, w;
            en_init();
            set(i);
            en_set(P_ENERGY, 100);
            en_set(P_LEAD, 100);
            en_set(P_MIX, 0);
            en_play(1);
            for (w = 0; w < 40; w++) {           /* 40 windows of 6 s: the worst window counts */
                t0 = now();
                for (b = 0; b < 1034; b++)
                    en_render(buf, 256, 4096);
                dt = (now() - t0) / (1034.0 * 256.0) * 1e9;
                tot += dt;
                if (dt > peak && w > 0)
                    peak = dt;
            }
            printf("  %-15s mean %.1f ns/frame, worst 6 s %.1f\n", en_style_name(i), tot / 40.0, peak);
            if (peak > worst)
                worst = peak, wi = i;
        }
        printf("render: busiest %s %.1f ns/frame (worst 6 s window)\n", en_style_name(wi), worst);
    }
    printf(fails ? "house_test: %d FAILED\n" : "house_test: all passed\n", fails);
    return fails != 0;
}
