/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Tavern session: ashtonwallen, 2026 */
/* The instrument on the host: the pure parts (scales, chords, voice leading, names), the tunes (their form,
 * their range, their endings, the same seed the same tune) and the render (silence at rest, sound when the band
 * plays, never NaN, bounded, a tune ends after its passes, players come in and rest only where a phrase begins,
 * a set goes straight on into its next tune in a new key, every note in the key and every backing note in the
 * chord, the air outside silent at 0). Then the render's cost, in ns a frame. */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "amb.h"

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

static uint32_t midi_count, notes, off_key, off_chord;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)   /* every note: in the key; the backing's in the chord */
{
    int ch = (int)(st & 15u), pc = (int)(d1 % 12u);
    midi_count++;
    if ((st & 0xF0u) != 0x90u || !d2 || ch == 9)
        return;
    notes++;
    if (!(en_keymask >> pc & 1u))
        off_key++;
    if ((ch == 2 || ch == 3 || (ch == 1 && !en_harp)) && !(en_chordmask >> pc & 1u)) {
        off_chord++;
        if (off_chord < 6)
            printf("  note %u (channel %d) outside the chord %03x (bar %d sess %d)\n", d1, ch, en_chordmask, en_bar, en_sess);
    }
}

static int32_t buf[2 * 256];
static float peak, sum2;
static int bad, midphrase, changes;
static void run(float secs)
{
    int blocks = (int)(secs * 44100.0f / 256.0f), b, j;
    for (b = 0; b < blocks; b++) {
        uint8_t live = en_live, sess = en_sess;
        uint32_t done = en_tunes_done;
        en_render(buf, 256, 2048);
        if (sess == 1 && en_sess == 1 && done == en_tunes_done && live != en_live) {
            changes++;                           /* someone came in or rested: only where a phrase begins */
            if (en_bar & 3)
                midphrase++;
        }
        for (j = 0; j < 512; j++) {
            float x = (float)buf[j] / 8388608.0f;
            if (x != x)
                bad++;
            if (fabsf(x) > peak)
                peak = fabsf(x);
            sum2 += x * x;
        }
    }
}

static void band(int level)
{
    int i;
    for (i = 0; i < EN_NINST; i++)
        en_inst(i, i == I_VOICE ? 0 : level);
}

#define ALL_BUT_VOICE (0xFFu & ~(1u << I_VOICE))

int main(void)
{
    int pcs[4], n, i;
    char name[16];

    /* scales and chords */
    {
        int st[12];
        CHECK(en_scale(EN_MODE_POOL[MODE_DORIAN], st) == 7 && st[2] == 3 && st[6] == 10, "Dorian steps");
        CHECK(en_scale(EN_MODE_POOL[MODE_PENT_MIN], st) == 5, "pentatonic minor has 5");
        CHECK(en_scale(0, st) == 1, "the root is always in");
    }
    n = en_chord_tones(EN_MODE_POOL[MODE_IONIAN], 0, 0, pcs);
    CHECK(n == 4 && pcs[0] == 0 && pcs[1] == 4 && pcs[2] == 7 && pcs[3] == 11, "C Ionian I = Cmaj7");
    CHECK(!strcmp(en_chord_name(pcs, 3, name), "C"), "name %s", name);
    en_chord_tones(EN_MODE_POOL[MODE_DORIAN], 2, 0, pcs);
    CHECK(!strcmp(en_chord_name(pcs, 3, name), "Dm"), "D Dorian i: %s", name);
    en_chord_tones(EN_MODE_POOL[MODE_DORIAN], 2, 6, pcs);
    CHECK(!strcmp(en_chord_name(pcs, 3, name), "C"), "D Dorian VII: %s", name);
    en_chord_tones(EN_MODE_POOL[MODE_MIXOLYDIAN], 9, 6, pcs);
    CHECK(!strcmp(en_chord_name(pcs, 3, name), "G"), "A Mixolydian VII: %s", name);

    /* voice leading: small moves, in range, no unisons */
    {
        int from[4] = {55, 59, 62, 67}, to[4], g[3] = {7, 11, 2}, c[3] = {0, 4, 7}, tot = 0;
        en_voice_lead(from, g, 3, to, 53, 72);
        memcpy(from, to, sizeof from);
        en_voice_lead(from, c, 3, to, 53, 72);
        for (i = 0; i < 4; i++) {
            CHECK(to[i] >= 53 && to[i] <= 72, "range %d", to[i]);
            tot += abs(to[i] - from[i]);
        }
        CHECK(tot <= 8, "G -> C moved %d", tot);
        CHECK(to[0] != to[1] && to[1] != to[2] && to[2] != to[3] && to[0] != to[3], "no unisons");
    }

    /* texts */
    {
        char t[12];
        en_param_text(P_TUNE, 3, t);
        CHECK(!strcmp(t, "+3"), "tune: %s", t);
        en_param_text(P_MODE, MODE_MIXOLYDIAN, t);
        CHECK(!strcmp(t, "Mixolyd."), "mode: %s", t);
        en_param_text(P_ROOT, 9, t);
        CHECK(!strcmp(t, "A"), "root: %s", t);
    }

    /* the tunes: every bar starts on a note, each part's second ending comes home, the notes stay in a fiddle's
     * reach, the second phrase opens as the first, and a seed always makes the same tune */
    {
        static const int MODES[4] = {MODE_IONIAN, MODE_DORIAN, MODE_MIXOLYDIAN, MODE_AEOLIAN};
        int k, m, s, p, b, e, notes = 0, same = 0, total = 0, beats = 0, off_beat_tones = 0;
        for (k = 0; k < EN_NKIND; k++)
            for (m = 0; m < 4; m++)
                for (s = 0; s < 60; s++) {
                    en_tune_t t, u;
                    memset(&t, 0, sizeof t);
                    memset(&u, 0, sizeof u);
                    en_tune_make((uint16_t)(s * 977 + 13), k, MODES[m], &t);
                    en_tune_make((uint16_t)(s * 977 + 13), k, MODES[m], &u);
                    CHECK(!memcmp(&t, &u, sizeof t), "seed %d kind %d: not the same twice", s, k);
                    CHECK(t.eighths == (k == K_JIG || k == K_WALTZ ? 6 : 8), "eighths %d", t.eighths);
                    for (p = 0; p < 2; p++) {
                        int last = -1000;
                        for (b = 0; b < 9; b++) {
                            CHECK(t.mel[p][b][0] > -50 || (b == 6 && t.mel[p][b][0] == EN_TIE),
                                  "kind %d part %d bar %d starts with %d", k, p, b, t.mel[p][b][0]);
                            for (e = 0; e < t.eighths; e++) {
                                int v = t.mel[p][b][e];
                                if (v > -50) {
                                    CHECK(v >= -3 && v <= 11, "note %d out of reach", v);
                                    notes++;
                                    if (b == 8)
                                        last = v;
                                }
                            }
                        }
                        CHECK(last == (p ? 7 : 0), "kind %d part %d ends on %d", k, p, last);
                        for (b = 0; b < 8; b++)  /* on the beats, a tone of the bar's chord (bar 9, home, aside) */
                            for (e = 0; e < t.eighths; e += t.beat) {
                                int v = t.mel[p][b][e], d = ((v - t.chord[p][b]) % 7 + 7) % 7;
                                if (v > -50) {
                                    beats++;
                                    off_beat_tones += d != 0 && d != 2 && d != 4;
                                }
                            }
                        CHECK(t.chord[p][8] == 0, "the second ending's chord");
                        for (e = 0; e < t.eighths; e++)
                            same += t.mel[p][4][e] == t.mel[p][0][e], total++;
                    }
                }
        CHECK(same == total, "the second phrase opens as the first (%d of %d)", same, total);
        CHECK(notes > 10000, "notes %d", notes);
        CHECK(off_beat_tones == 0, "%d of %d notes on the beat not in the chord", off_beat_tones, beats);
    }

    /* the render: silent at rest, sound when the band plays, never NaN, bounded */
    en_init();
    run(1.0f);
    CHECK(peak == 0.0f, "silent before play (%f)", (double)peak);
    en_play(1);
    band(80);
    en_set(P_TEMPO, 100);
    en_set(P_ROOT, 2);
    en_set(P_MODE, MODE_DORIAN);
    en_tune(1234, K_REEL, 1, 0);
    peak = sum2 = 0.0f;
    run(20.0f);
    CHECK(!bad, "NaN in the output");
    CHECK(peak > 0.1f && peak < 0.99f, "peak %f", (double)peak);
    printf("  20 s of a reel: peak %.3f, rms %.4f, sixteenths %u, live %02x\n", (double)peak,
           (double)sqrtf(sum2 / (20.0f * 44100.0f * 2.0f)), en_bars, en_live);
    CHECK(en_sess == 1, "the tune plays (%d)", en_sess);
    CHECK(en_live == ALL_BUT_VOICE, "the band plays: %02x", en_live);

    /* the tune ends after its pass: 32 bars at 100 bpm (77 s) and the last note */
    {
        uint32_t done = en_tunes_done;
        int k;
        for (k = 0; k < 80 && en_tunes_done == done; k++)
            run(1.0f);
        CHECK(en_tunes_done == done + 1, "the reel ended (%u)", en_tunes_done);
        CHECK(k + 20 >= 76 && k + 20 <= 86, "it took %d s", k + 20);
        CHECK(en_sess == 0 && en_live == 0, "the room after the tune");
    }

    /* each kind, solo then the band: asked for in the first bar, they come in where the next phrase begins */
    {
        int k;
        for (k = 0; k < EN_NKIND; k++) {
            band(0);
            en_inst(k == K_AIR ? I_HARP : I_FIDDLE, 80);
            en_set(P_TEMPO, k == K_AIR ? 54 : 90);
            en_tune((uint16_t)(77 + k), k, 2, k == K_AIR);
            run(0.5f);
            CHECK(en_live == (k == K_AIR ? 1u << I_HARP : 1u << I_FIDDLE), "kind %d: one alone (%02x)", k, en_live);
            band(70);
            run(0.05f);
            CHECK(en_live == (k == K_AIR ? 1u << I_HARP : 1u << I_FIDDLE), "kind %d: not mid-bar", k);
            peak = 0.0f;
            while (en_bar < 3)
                run(0.1f);
            CHECK(en_live == (k == K_AIR ? 1u << I_HARP : 1u << I_FIDDLE), "kind %d: not mid-phrase (bar %d)", k, en_bar);
            while (en_bar < 4)
                run(0.05f);
            run(0.4f);                           /* (en_bar turns a sixteenth before the bar begins) */
            CHECK(en_live == ALL_BUT_VOICE, "kind %d: in on the phrase (%02x)", k, en_live);
            CHECK(!bad && peak > 0.05f && peak < 0.99f, "kind %d peak %f", k, (double)peak);
        }
        /* closing time: the tune ends at the end of its pass */
        en_end();
        run(1.0f);
        CHECK(en_passes == en_pass + 1, "en_end: this pass the last");
    }

    /* a player asked to rest mid-phrase plays on to the phrase's end */
    run(3.0f);
    band(70);
    en_set(P_TEMPO, 90);
    en_tune(5, K_JIG, 2, 0);
    run(0.3f);
    run(2.5f);
    CHECK(en_bar >= 1 && en_bar < 3, "jig bar %d", en_bar);
    en_inst(I_FIDDLE, 0);
    run(0.1f);
    CHECK(en_live & 1u << I_FIDDLE, "the fiddle stopped mid-phrase");
    while (en_bar < 4)
        run(0.05f);
    run(0.4f);
    CHECK(!(en_live & 1u << I_FIDDLE), "the fiddle rests from the phrase (%02x)", en_live);

    /* a set: the next tune queued goes straight on at the end of the last pass, in its own key and tempo, with no
     * last note between; en_end takes a queued one back */
    {
        uint32_t done = en_tunes_done, seg = en_segues;
        int k;
        band(70);
        en_set(P_TEMPO, 120);
        en_set(P_ROOT, 2);
        en_set(P_MODE, MODE_DORIAN);
        en_tune(321, K_REEL, 1, 0);
        run(1.0f);
        en_next(654, K_REEL, 1, 9, MODE_MIXOLYDIAN, 126);
        for (k = 0; k < 80 && en_segues == seg; k++)
            run(1.0f);
        CHECK(en_segues == seg + 1 && en_tunes_done == done + 1, "the set went on (%u %u)", en_segues, en_tunes_done);
        CHECK(en_sess == 1 && en_bar == 0 && en_part == 0, "straight on, from the top (%d %d)", en_sess, en_bar);
        CHECK(en_keymask == (((EN_MODE_POOL[MODE_MIXOLYDIAN] << 9) | (EN_MODE_POOL[MODE_MIXOLYDIAN] >> 3)) & 0xFFFu),
              "A Mixolydian: %03x", en_keymask);
        CHECK(en_live == ALL_BUT_VOICE, "the band plays on into it (%02x)", en_live);
        en_next(987, K_JIG, 1, 7, MODE_IONIAN, 100);
        en_end();
        for (k = 0; k < 80 && en_sess != 0; k++)
            run(1.0f);
        CHECK(en_segues == seg + 1 && en_sess == 0, "en_end: no next tune (%u)", en_segues);
    }

    /* every note in the key, every backing note in the chord, and players in and out only on a phrase: each kind
     * in each folk mode, the harp leading the airs, the band changing its mind every second and a half */
    {
        static const int MODES[4] = {MODE_IONIAN, MODE_DORIAN, MODE_MIXOLYDIAN, MODE_AEOLIAN};
        int k, m, t, ok_notes;
        notes = off_key = off_chord = 0;
        midphrase = changes = 0;
        for (k = 0; k < EN_NKIND; k++)
            for (m = 0; m < 4; m++) {
                uint32_t done = en_tunes_done;
                band(70);
                en_set(P_TEMPO, 130);
                en_set(P_ROOT, (k * 5 + m * 7) % 12);
                en_set(P_MODE, MODES[m]);
                en_tune((uint16_t)(1000 + k * 37 + m), k, 1, k == K_AIR && (m & 1));
                for (t = 0; t < 90 && en_tunes_done == done; t++) {
                    run(1.5f);
                    if (t % 3 == 1)
                        en_inst((t * 3 + m) % EN_NINST, (t & 4) ? 0 : 70);
                }
                CHECK(en_tunes_done == done + 1, "kind %d mode %d ended", k, m);
            }
        ok_notes = notes > 5000u;
        printf("  %u notes: %u outside the key, %u backing notes outside the chord; %d entries, %d mid-phrase\n",
               notes, off_key, off_chord, changes, midphrase);
        CHECK(ok_notes, "notes %u", notes);
        CHECK(off_key == 0, "%u notes outside the key", off_key);
        CHECK(off_chord == 0, "%u backing notes outside the chord", off_chord);
        CHECK(changes > 20 && midphrase == 0, "%d of %d entries mid-phrase", midphrase, changes);
    }

    /* the air outside (the ambience): each texture sounds at its level, and at 0 is silent */
    {
        int tx;
        band(0);
        en_play(0);
        run(25.0f);
        for (tx = 0; tx < 4; tx++) {
            en_set(P_TEXTURE, tx);
            en_set(P_TEXMOD, 60);
            en_set(P_TEXLVL, 55);
            run(4.0f);
            peak = 0.0f;
            run(3.0f);
            CHECK(peak > 0.003f && peak < 0.5f, "air %d at 55: peak %f", tx, (double)peak);
            en_set(P_TEXLVL, 0);
            run(15.0f);
            peak = 0.0f;
            run(2.0f);
            CHECK(peak < 0.00002f, "air %d at 0: peak %f", tx, (double)peak);
        }
        en_play(1);
    }

    /* the singer */
    band(0);
    en_inst(I_VOICE, 90);
    en_inst(I_HARP, 50);
    en_tune(99, K_AIR, 1, 0);
    peak = 0.0f;
    run(10.0f);
    CHECK(!bad && peak > 0.03f && peak < 0.99f, "a song: peak %f", (double)peak);

    /* every setting at its extremes, the whole band loud: still bounded */
    {
        int p;
        for (p = 0; p < P_NPARAMS; p++)
            en_set(p, en_param_info(p)->hi);
        for (i = 0; i < EN_NINST; i++)
            en_inst(i, 100);
        en_tune(4321, K_REEL, 1, 0);
        peak = 0.0f;
        run(30.0f);
        CHECK(!bad && peak < 1.0f, "all at max: peak %f", (double)peak);
        for (p = 0; p < P_NPARAMS; p++)
            en_set(p, en_param_info(p)->lo);
        run(10.0f);
        CHECK(!bad, "all at min: NaN");
    }

    /* stop: the tails die away */
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, en_param_info(i)->def);
    en_play(0);
    run(20.0f);
    peak = 0.0f;
    run(1.0f);
    CHECK(peak < 0.01f, "quiet after stopping (%f)", (double)peak);
    CHECK(midi_count > 50u, "MIDI out (%u)", midi_count);

    /* the cost: ns a frame, the busiest the session gets (the whole band and the singer on a quick reel, the
     * rain, the shimmer, the stone hall's room); compare with AMB-1 Drift's busiest piece measured the same way */
    {
        struct timespec t0, t1;
        double ns;
        en_init();
        en_play(1);
        for (i = 0; i < EN_NINST; i++)
            en_inst(i, 90);
        en_set(P_TEMPO, 120);
        en_set(P_ROOM, 90);
        en_set(P_REVERB, 75);
        en_set(P_ECHO, 25);
        en_set(P_TEXTURE, 0);
        en_set(P_TEXLVL, 70);
        en_set(P_SHIMMER, 35);
        en_tune(2222, K_REEL, 4, 0);
        run(2.0f);
        clock_gettime(CLOCK_MONOTONIC, &t0);
        run(60.0f);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ns = ((double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec)) / (60.0 * 44100.0);
        printf("  render: %.1f ns/frame (the band, the singer, rain, shimmer)\n", ns);
    }

    printf(fails ? "amb: %d FAILED\n" : "amb: ok\n", fails);
    return fails != 0;
}
