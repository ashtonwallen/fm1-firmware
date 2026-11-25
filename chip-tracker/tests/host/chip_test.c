/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CHIP TRACKER: ashtonwallen, 2026 */
/* The instrument on the host: the pure parts (the songs as they are made: every pattern's rows in range, the order
 * sound, titles) and the render: every style makes sound, nothing is NaN, the peaks stay under -1 dBFS over long
 * runs, songs move on (loops, the jingle, the next), a skip works, the keys play along. Then the render's cost, in
 * ns a frame (the CPU check: compare with AMB-1's busiest piece).
 *   chip_test                    the checks
 *   chip_test dump STYLE N       N songs of a style, as a musician reads them: chords, bass, lead, second pulse
 *   chip_test demos DIR          the demo clips: NN-<style>.wav (20 s each, after the intro), 00-listen.wav (3 min)
 *   chip_test wavs DIR           a minute of each style */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "chip.h"

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
            if (s) {
                if (v != v)
                    s->nan++;
                if (fabs(v) > s->peak)
                    s->peak = fabs(v);
                s->sum2 += v * v;
                s->n++;
            }
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

/* ---- the dump: a song as a musician reads it */
static const char *nn(int n, char *b)
{
    if (n == NOTE_REL)
        return "==";
    if (n == NOTE_CUT)
        return "--";
    sprintf(b, "%s%d", EN_NOTE_NAME[n % 12], n / 12 - 1);
    return b;
}
static void dump_song(const en_song_t *s)
{
    int o, seen[EN_NPAT] = {0};
    char t[16];
    printf("== %s  (%s, key %s %s) order:", s->title, en_style_name(s->style), EN_NOTE_NAME[s->key],
           en_mode_name(s->mode));
    for (o = 0; o < s->nord; o++)
        printf(" %c%02X", s->osect[o], s->order[o]);
    printf("\n");
    for (o = 0; o < s->nord; o++) {
        int p = s->order[o], bar, ch;
        if (seen[p]++)
            continue;
        for (bar = 0; bar < s->plen[p] / 16; bar++) {
            en_chord_text(s->pchord[p][bar], t);
            printf("  pat %02X %c bar %d  [%-4s]\n", p, s->osect[o], bar, t);
            for (ch = 0; ch < 4; ch++) {
                int r;
                static const char *const CN[4] = {"lead", "pu2 ", "bass", "drum"};
                printf("    %s:", CN[ch]);
                for (r = bar * 16; r < bar * 16 + 16; r++) {
                    const en_cell_t *e = &s->pat[p][r][ch];
                    if (!e->note) {
                        printf(ch == 3 ? " ." : "    .");
                        continue;
                    }
                    if (ch == 3) {
                        static const char D[] = "?KSHOCGM";
                        int k = e->ins;
                        printf(" %c", k >= 18 && k <= 24 ? "KSHOCGM"[k - 18] : D[0]);
                        continue;
                    }
                    if (e->note >= NOTE_REL)
                        printf(" %4s", nn(e->note, t));
                    else
                        printf(" %4s", nn(e->note, t));
                    if (e->fx)
                        printf("%c", e->fx);
                }
                printf("\n");
            }
        }
    }
}

int main(int argc, char **argv)
{
    int i;
    static en_song_t song;
    if (argc > 3 && !strcmp(argv[1], "dump")) {
        int st = atoi(argv[2]), n = atoi(argv[3]);
        int16_t p[P_NPARAMS];
        en_style_recipe(st, p);
        for (i = 0; i < n; i++) {
            en_make_song(&song, st, en_song_rand(0x43484950u + (uint32_t)st, (uint32_t)i + 1u), p[P_MOOD], p[P_TEMPO]);
            dump_song(&song);
        }
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "demos")) {
        char path[512], name[64];
        for (i = 0; i < EN_NSTYLES; i++) {
            int k, j;
            en_init();
            style(i);
            en_set(P_LOOPS, 2);
            en_song_at(0, 0x43484950u + (uint32_t)i * 7u);
            en_play(1);
            run(14.0, 0);                        /* past the intro, into the verse */
            strcpy(name, en_style_name(i));
            for (k = 0, j = 0; name[k]; k++)       /* spaces as dashes, no apostrophes */
                if (name[k] != 39)
                    name[j++] = name[k] == ' ' ? '-' : name[k];
            name[j] = 0;
            snprintf(path, sizeof path, "%s/%02d-%s.wav", argv[2], i + 1, name);
            wav_open(path);
            run(20.0, 0);
            wav_close();
        }
        en_init();
        style(0);
        en_song_at(0, 0x43484950u);
        en_play(1);
        snprintf(path, sizeof path, "%s/00-listen.wav", argv[2]);
        wav_open(path);
        run(180.0, 0);
        wav_close();
        return 0;
    }
    /* the pure parts: songs of every style, in range */
    for (i = 0; i < EN_NSTYLES * 40; i++) {
        int st = i % EN_NSTYLES, p, r, c, o;
        int16_t pr[P_NPARAMS];
        en_style_recipe(st, pr);
        en_make_song(&song, st, en_song_rand(77u, (uint32_t)i), (i * 37) % 101, pr[P_TEMPO]);
        CHECK(song.nord > 4 && song.nord <= EN_NORD, "song %d: order length %d", i, song.nord);
        CHECK(song.osect[0] == 'I' && song.osect[song.nord - 1] == 'E', "song %d: intro and jingle", i);
        CHECK(strlen(song.title) > 3 && strlen(song.title) < 24, "title '%s'", song.title);
        for (o = 0; o < song.nord; o++)
            CHECK(song.order[o] < song.npat, "song %d: order %d -> %d", i, o, song.order[o]);
        for (p = 0; p < song.npat; p++) {
            CHECK(song.plen[p] == 16 || song.plen[p] == 32, "song %d pattern %d: %d rows", i, p, song.plen[p]);
            for (r = 0; r < song.plen[p]; r++)
                for (c = 0; c < EN_NCH; c++) {
                    const en_cell_t *e = &song.pat[p][r][c];
                    if (!e->note || e->note >= NOTE_REL)
                        continue;
                    if (c == CH_NOI)
                        CHECK(e->note >= 1 && e->note <= 16, "noise note %d", e->note);
                    else if (c == CH_TRI)
                        CHECK(e->note >= 36 && e->note <= 64, "bass note %d (style %d)", e->note, st);
                    else
                        CHECK(e->note >= 48 && e->note <= 96, "pulse note %d (style %d ch %d)", e->note, st, c);
                    CHECK(e->ins != 0xFF, "a note with no instrument");
                }
        }
    }
    for (i = 0; i < EN_NSTYLES; i++) {
        int16_t p[P_NPARAMS];
        int k;
        en_style_recipe(i, p);
        for (k = 0; k < P_NSTYLE; k++)
            CHECK(p[k] >= en_param_info(k)->lo && p[k] <= en_param_info(k)->hi, "style %d param %d", i, k);
        CHECK(strlen(en_style_name(i)) <= 16, "style name %s too long", en_style_name(i));
    }
    /* every style: sound within seconds, bounded over a long run, songs move on */
    for (i = 0; i < EN_NSTYLES; i++) {
        stats_t a = {0}, b = {0};
        uint32_t s0;
        char path[256];
        en_init();
        style(i);
        en_set(P_LOOPS, 1);
        en_song_at((uint32_t)i * 10u, 0xC0FFEEu + (uint32_t)i);
        en_play(1);
        if (argc > 2 && !strcmp(argv[1], "wavs")) {
            snprintf(path, sizeof path, "%s/style%02d.wav", argv[2], i);
            wav_open(path);
        }
        run(60.0, &a);
        wav_close();
        s0 = en_songs_played;
        run(420.0, &b);
        printf("%-16s first minute rms %5.1f dB peak %5.1f dB; next 7 min peak %5.1f dB, rms %5.1f dB, songs %u\n",
               en_style_name(i), db(sqrt(a.sum2 / a.n)), db(a.peak), db(b.peak), db(sqrt(b.sum2 / b.n)),
               (unsigned)(en_songs_played - s0));
        CHECK(a.nan == 0 && b.nan == 0, "%s: NaN", en_style_name(i));
        CHECK(db(sqrt(a.sum2 / a.n)) > -30.0, "%s: too quiet", en_style_name(i));
        CHECK(db(a.peak) < -1.0 && db(b.peak) < -1.0, "%s: peaks over -1 dBFS", en_style_name(i));
        CHECK(en_songs_played - s0 >= 2, "%s: songs did not move on", en_style_name(i));
    }
    /* the extremes: everything up, then everything down */
    {
        stats_t a = {0};
        int k;
        en_init();
        style(3);
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
    /* a skip: the next song at once */
    {
        stats_t a = {0};
        uint32_t no;
        en_init();
        style(0);
        en_play(1);
        run(20.0, &a);
        no = en_song_no;
        en_skip();
        run(1.0, &a);
        CHECK(en_song_no == no + 1, "skip: song %u -> %u", (unsigned)no, (unsigned)en_song_no);
        CHECK(en_ord == 0 && en_phase == PH_INTRO, "skip: in the intro (ord %d)", en_ord);
    }
    /* the mood moved: the song closes with its jingle within a few seconds, a new one starts */
    {
        uint32_t no;
        int saw = 0, k;
        en_init();
        style(0);
        en_play(1);
        run(25.0, 0);
        no = en_song_no;
        en_set(P_MOOD, 90);
        en_retune();
        for (k = 0; k < 120 && !saw; k++) {
            run(0.1, 0);
            saw = en_phase == PH_JINGLE;
        }
        CHECK(saw, "mood: no jingle");
        run(8.0, 0);
        CHECK(en_song_no == no + 1, "mood: no new song (%u)", (unsigned)en_song_no);
    }
    /* stopped: silence; again: sound */
    {
        stats_t a = {0}, b = {0};
        en_init();
        style(0);
        en_play(1);
        run(10.0, &a);
        en_play(0);
        run(1.0, &a);
        run(2.0, &b);
        CHECK(db(b.peak) < -70.0, "stopped: peak %.1f dB", db(b.peak));
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
        run(1.0, &a);
        memset(&a, 0, sizeof a);
        en_note(72, 100);
        run(0.5, &a);
        en_note(72, 0);
        CHECK(db(a.peak) > -40.0, "a key: peak %.1f dB", db(a.peak));
    }
    /* repeat: the song loops on */
    {
        uint32_t no;
        en_init();
        style(0);
        en_set(P_LOOPS, 1);
        en_repeat(1);
        en_play(1);
        run(0.1, 0);
        no = en_song_no;
        run(400.0, 0);
        CHECK(en_song_no == no, "repeat: moved on to %u", (unsigned)en_song_no);
        CHECK(en_loop >= 2, "repeat: loop %d", en_loop);
    }
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
            en_set(P_ENERGY, 100);
            en_set(P_ECHO, 100);
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
    printf(fails ? "chip_test: %d FAILED\n" : "chip_test: all passed\n", fails);
    return fails != 0;
}
