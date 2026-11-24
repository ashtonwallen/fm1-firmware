/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen */
/* NIGHT TRAIN's world alone (app/world.c): a long night run fast. The train keeps going and keeps stopping, every
 * regular rides, the stories move on, the log reads as words that fit the screen, the state stays valid, and the
 * same seed makes the same journey. */
#include <stdio.h>
#include <string.h>
#include "world.h"

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

static world_t w, w2;

int main(void)
{
    int c, i, hours = 0, longest = 0, maxname = 0, types[E_NTYPES] = {0}, rode[NREG] = {0}, signals = 0, tunnels = 0;
    uint32_t ev, chimes = 0, swaps = 0, t;
    char b[96];
    float maxv = 0.0f;
    world_new(&w, 12345u);
    CHECK(world_valid(&w), "new world not valid");
    for (t = 0; t < 30u * 3600u * 10u; t++) {          /* thirty hours, in 100 ms ticks */
        uint16_t n0 = w.nlog;
        ev = world_tick(&w, 100u, 5);
        chimes += (ev & W_CHIME) != 0u;
        swaps += (ev & W_SWAP) != 0u;
        if (w.speed > maxv)
            maxv = w.speed;
        for (c = 0; c < NREG; c++)
            rode[c] |= w.who[c].aboard;
        while (n0 != w.nlog) {
            const logrec_t *r = &w.log[n0 % NLOG];
            int n = world_text(&w, r, b, sizeof b);
            types[r->type]++;
            if (n > longest)
                longest = n;
            CHECK(n > 3, "empty log text, type %d", r->type);
            if (t < 3u * 3600u * 10u && r->type != E_STATION && r->type != E_TUNNEL && r->type != E_BRIDGE)
                printf("  %5.1f km  %s\n", (double)world_km(&w), b);
            signals += r->type == E_SIGNAL;
            tunnels += r->type == E_TUNNEL;
            n0++;
        }
        if (t % 36000u == 0u) {
            CHECK(world_valid(&w), "invalid at hour %d", hours);
            hours++;
        }
    }
    for (i = 0; i < 200; i++) {
        station_name(w.seed, (uint32_t)i, b);
        if ((int)strlen(b) > maxname)
            maxname = (int)strlen(b);
    }
    printf("  30 h: %u stations, %.0f km, top speed %.0f km/h, %u chimes, longest line %d, longest name %d\n",
           w.station, (double)world_km(&w), (double)(maxv * 3.6f), chimes, longest, maxname);
    printf("  stories:");
    for (c = 0; c < NREG; c++)
        printf(" %s %d/%d (%d rides)", who_name(c), w.who[c].beat, world_beats(c), w.who[c].trips);
    printf("\n  signals %d, tunnels %d, talks %d, missed stops %d\n", signals, tunnels, types[E_TALK], types[E_MISSED]);
    CHECK(w.station > 200u && w.station < 500u, "stations in 30 h at 5 min: %u", w.station);
    CHECK(chimes + 1u >= w.station && swaps + 1u >= w.station, "a chime and a swap at each station (%u, %u)", chimes, swaps);
    CHECK(maxv * 3.6f > 60.0f && maxv * 3.6f < 130.0f, "top speed %f", (double)maxv);
    for (c = 0; c < NREG; c++) {
        CHECK(rode[c], "%s never rode", who_name(c));
        CHECK(w.who[c].beat >= 4, "%s's story stuck at %d", who_name(c), w.who[c].beat);
    }
    CHECK(types[E_BOARD] > 50 && types[E_LEAVE] > 50 && types[E_TALK] > 50, "boarding, leaving, talk");
    CHECK(types[E_MISSED] > 0 && types[E_SLEEP] > 0, "naps and missed stops");
    CHECK(signals > 0 && tunnels > 0 && types[E_RAIN] > 0, "events");
    CHECK(longest <= 60, "a log line too long (%d)", longest);
    CHECK(maxname <= 18, "a station name too long (%d)", maxname);
    /* the same seed, the same journey; another, another */
    world_new(&w, 777u);
    world_new(&w2, 777u);
    for (t = 0; t < 2u * 3600u * 10u; t++) {
        world_tick(&w, 100u, 8);
        world_tick(&w2, 100u, 8);
    }
    CHECK(!memcmp(&w, &w2, sizeof w), "same seed, different journey");
    world_new(&w2, 778u);
    station_name(777u, 5u, b);
    {
        char b2[20];
        station_name(778u, 5u, b2);
        printf("  station 5: %s / %s\n", b, b2);
    }
    /* express: on to the next station's approach */
    world_new(&w, 1u);
    for (t = 0; t < 900u; t++)
        world_tick(&w, 100u, 20);
    i = (int)w.station;
    world_express(&w);
    for (t = 0; t < 2400u && (int)w.station == i && w.phase != PH_DWELL; t++)
        world_tick(&w, 100u, 20);
    CHECK(w.phase == PH_DWELL, "express: at the station within 4 min (phase %d)", w.phase);
    printf(fails ? "world: %d FAILED\n" : "world: ok\n", fails);
    return fails != 0;
}
