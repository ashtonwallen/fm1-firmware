/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; POND changes (C) 2026 ashtonwallen */
/* POND in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): POND's checks for
 * "expect", its commands, a reboot and the screen reader's dump.
 *
 * Checks: playing frozen view dirty sixteenths pad_level pool parN battery, and the pond's: fish (alive), day,
 * minute, season, tod, born, died, herons, meets, act (x 100), food (pellets in the water), heron (its phase),
 * relmax / relmin (the warmest friendship, the worst rivalry), sel, hush (want_save), names (a hash of the living
 * fish's names: the same pond after a reboot), cap, busy; 1.1: preset, format, sentN (what the engine was sent for
 * P_* N), same (1 while the pond is the one storev1 or snap kept: its fish, their notes and ties, the day, the journal),
 * fishsame (the same fish, their notes, the day: the ties and the journal may have moved on).
 * Commands: battery N; ff MIN (MIN real minutes of the pond at once, quietly); heron (one lands now); night (the
 * clock to 23:00) / noon (12:00); storeamb (AMB-1 Drift's project in the store: another firmware's data); hunger N
 * (every fish's hunger); age (every fish at the end of its life); friends (the first two fish best friends, grown);
 * storev1 (the project as it is, saved as 1.0 saved it: format 1, the preset's byte spare); snap (the pond kept, for
 * same and fishsame). */
static world_t v1_world;                         /* the pond storev1 saved */

int app_expect(const char *what, int *got)
{
    int g, i, j;
    world_t *w = &proj.w;
    if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "frozen"))
        g = en_frozen;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "pool"))
        g = (int)pd_pool();
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strcmp(what, "dirty"))
        g = ui.dirty;
    else if (!strcmp(what, "sixteenths"))
        g = (int)en_bars;
    else if (!strcmp(what, "pad_level"))
        g = (int)(en_pad_level * 100.0f);
    else if (!strcmp(what, "fish"))
        g = pd_alive();
    else if (!strcmp(what, "day"))
        g = w->day;
    else if (!strcmp(what, "minute"))
        g = w->minute;
    else if (!strcmp(what, "season"))
        g = pd_season();
    else if (!strcmp(what, "tod"))
        g = pd_tod();
    else if (!strcmp(what, "born"))
        g = w->born;
    else if (!strcmp(what, "died"))
        g = w->died;
    else if (!strcmp(what, "herons"))
        g = w->herons;
    else if (!strcmp(what, "meets"))
        g = w->meets;
    else if (!strcmp(what, "act"))
        g = (int)(pd.act * 100.0f);
    else if (!strcmp(what, "heron"))
        g = pd.heron;
    else if (!strcmp(what, "food")) {
        g = 0;
        for (i = 0; i < NFOOD; i++)
            g += pd.food[i].life > 0.0f;
    } else if (!strcmp(what, "relmax") || !strcmp(what, "relmin")) {
        int mx = !strcmp(what, "relmax");
        g = 0;
        for (i = 0; i < PD_NFISH; i++)
            for (j = i + 1; j < PD_NFISH; j++)
                if (w->fish[i].alive && w->fish[j].alive && (mx ? w->rel[i][j] > g : w->rel[i][j] < g))
                    g = w->rel[i][j];
    } else if (!strcmp(what, "sel"))
        g = pd.sel;
    else if (!strcmp(what, "hush"))
        g = pd.want_save;
    else if (!strcmp(what, "names")) {
        uint32_t h = 2166136261u;
        for (i = 0; i < PD_NFISH; i++)
            if (w->fish[i].alive)
                h = (h ^ (uint32_t)(w->fish[i].name + 1u + 64u * i)) * 16777619u;
        g = (int)(h & 0x7FFFFFFFu);
    } else if (!strcmp(what, "cap"))
        g = proj.cap;
    else if (!strcmp(what, "busy"))
        g = proj.busy;
    else if (!strncmp(what, "par", 3))
        g = proj.par[atoi(what + 3) % P_NSAVED];
    else if (!strcmp(what, "preset"))
        g = proj.preset;
    else if (!strcmp(what, "format"))
        g = (int)proj.format;
    else if (!strncmp(what, "sent", 4))
        g = pd.sent[atoi(what + 4) % P_NPARAMS];
    else if (!strcmp(what, "same") || !strcmp(what, "fishsame")) {
        g = w->day == v1_world.day && (what[0] == 'f' || (w->born == v1_world.born && w->died == v1_world.died &&
            w->meets == v1_world.meets && !memcmp(w->rel, v1_world.rel, sizeof w->rel) &&
            !memcmp(w->log, v1_world.log, sizeof w->log)));
        for (i = 0; i < PD_NFISH; i++)
            g = g && w->fish[i].alive == v1_world.fish[i].alive && w->fish[i].name == v1_world.fish[i].name &&
                w->fish[i].deg == v1_world.fish[i].deg && w->fish[i].species == v1_world.fish[i].species;
    }
    else
        return 0;
    *got = g;
    return 1;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    world_t *w = &proj.w;
    int i;
    (void)b, (void)c, (void)d;
    if (!strcmp(cmd, "battery")) {
        batt_host = (int8_t)(strcmp(a, "off") ? atoi(a) : -2);
        return 1;
    }
    if (!strcmp(cmd, "names")) {                 /* print the pond: who is there */
        for (i = 0; i < PD_NFISH; i++)
            if (w->fish[i].alive)
                printf("  fish %d %s %s %s deg %d size %d age %dd hunger %d\n", i, fname(i), PD_SPECIES[w->fish[i].species],
                       PD_TEMPER[w->fish[i].temper], w->fish[i].deg, w->fish[i].size, w->fish[i].age / 24,
                       w->fish[i].hunger);
        for (i = 0; i < w->nlog; i++)
            printf("  log D%d %s\n", w->log[i].day, w->log[i].t);
        return 1;
    }
    if (!strcmp(cmd, "ff")) {
        pd_fast((uint32_t)atoi(a));
        return 1;
    }
    if (!strcmp(cmd, "heron")) {
        pd.heron = 1;
        pd.ht = 0.0f;
        pd.hx = 200.0f;
        pd.hy = 60.0f;
        pd.hdx = 0.8f;
        pd.hdy = -0.6f;
        return 1;
    }
    if (!strcmp(cmd, "night") || !strcmp(cmd, "noon")) {
        w->minute = (uint16_t)(!strcmp(cmd, "night") ? 23 * 60 : 12 * 60);
        return 1;
    }
    if (!strcmp(cmd, "season")) {                /* season N: the first day of season N this year */
        w->day = (uint16_t)(1 + 4 * (atoi(a) & 3));
        return 1;
    }
    if (!strcmp(cmd, "hunger")) {
        for (i = 0; i < PD_NFISH; i++)
            if (w->fish[i].alive)
                w->fish[i].hunger = (uint8_t)atoi(a);
        return 1;
    }
    if (!strcmp(cmd, "age")) {
        for (i = 0; i < PD_NFISH; i++)
            if (w->fish[i].alive)
                w->fish[i].age = (uint16_t)(w->fish[i].life - 1u);
        return 1;
    }
    if (!strcmp(cmd, "friends")) {               /* the first two living fish: grown best friends, side by side */
        int f[2], n = 0;
        for (i = 0; i < PD_NFISH && n < 2; i++)
            if (w->fish[i].alive)
                f[n++] = i;
        if (n == 2) {
            w->rel[f[0]][f[1]] = w->rel[f[1]][f[0]] = 90;
            for (i = 0; i < 2; i++) {
                w->fish[f[i]].age = 24 * 10;
                w->fish[f[i]].hunger = 0;
                w->fish[f[i]].spawn_cd = 0;
                pd.f[f[i]].x = 120.0f + (float)i * 3.0f;
                pd.f[f[i]].y = 88.0f;
            }
            pd.meet_cd[f[0]][f[1]] = pd.meet_cd[f[1]][f[0]] = 0.0f;
        }
        return 1;
    }
    if (!strcmp(cmd, "snap")) {
        v1_world = proj.w;
        return 1;
    }
    if (!strcmp(cmd, "storev1")) {               /* 1.0's project: format 1, no preset (the byte was spare, 0) */
        uint8_t kp = proj.preset;
        project_save();                          /* (as a save leaves it: the fish's places kept) */
        proj.format = 1u;
        proj.preset = 0;
        v1_world = proj.w;
        plat_store_save(OBJ_PROJ, &proj, sizeof proj);
        proj.format = PROJ_FORMAT;
        proj.preset = kp;
        return 1;
    }
    if (!strcmp(cmd, "storeamb")) {              /* AMB-1 Drift's project (format 5, "ENO1"), as it left the flash */
        static uint8_t o[1200];
        memset(o, 0x11, sizeof o);
        o[0] = 0x45, o[1] = 0x4E, o[2] = 0x4F, o[3] = 0x31;
        o[4] = 5, o[5] = o[6] = o[7] = 0;
        plat_store_save(OBJ_PROJ, o, sizeof o);
        return 1;
    }
    return 0;
}

void app_host_reboot(void)
{
    memset(&proj, 0, sizeof proj);
    memset(&pd, 0, sizeof pd);
}

void app_speak_dump(void) { speak_dump(); }
