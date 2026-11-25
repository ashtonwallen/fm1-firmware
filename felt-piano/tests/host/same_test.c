/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * FELT PIANO: ashtonwallen, 2026 */
/* A fingerprint of what the first fourteen styles play: each style for several pieces from a few seeds, the
 * rendered samples hashed. Built against 1.0's engine and the current one, the lines must match (1.1 kept 1.0's
 * styles as they were).
 *   same_test [SECONDS]        one line a style: its index and the hash of its samples */
#include <stdio.h>
#include <stdlib.h>
#include "felt.h"

int main(int argc, char **argv)
{
    static int32_t buf[512];
    int st, k, seed;
    double sec = argc > 1 ? atof(argv[1]) : 240.0;
    for (st = 0; st < 14; st++) {
        uint32_t h = 2166136261u;
        for (seed = 0; seed < 3; seed++) {
            int16_t p[P_NPARAMS];
            uint32_t b, nb = (uint32_t)(sec * 44100.0 / 256.0);
            en_init();
            en_style_recipe(st, p);
            en_style(st);
            for (k = 0; k < P_NSTYLE; k++)
                en_set(k, p[k]);
            en_set(P_LENGTH, 0);
            en_track((uint32_t)seed * 7u, 0x1234567u + (uint32_t)seed * 0x9E37u + (uint32_t)st);
            en_play(1);
            for (b = 0; b < nb; b++) {
                en_render(buf, 256, 4096);
                for (k = 0; k < 512; k++)
                    h = (h ^ (uint32_t)buf[k]) * 16777619u;
            }
        }
        printf("%2d %08x %s\n", st, (unsigned)h, en_style_name(st));
    }
    return 0;
}
