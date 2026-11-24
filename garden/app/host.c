/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (GARDEN) */
/* GARDEN in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): the garden's checks for
 * "expect", its commands, a reboot and the screen reader's dump.
 *
 * Checks: playing frozen view pool dirty sixteenths pad_level parN battery growth (the engine and the settings);
 * living stageN (plants in stage N: 1 seed .. 7 compost) born died hybrids nibbles frosts rains wild yours (counts)
 * gardener (name index) mood day weather seconds tended rung (plot notes rung) saves logseq fav plotN (plot N's stage)
 * lineage (plants with a parent); 1.1: preset, format (the project's), pvN (what the engine gets for P_* N, the
 * preset's colour on it).
 * Commands: battery N; weather N (the weather now, W_*); foreign (AMB-1 Drift's project in the store, as another
 * firmware left it); junk (a damaged project of ours: a byte changed); age N (every plant N quarter-seconds older);
 * storev1 (the project as it is, saved as 1.0 saved it: format 1, the preset's byte spare; and snap). */

static world_t snap_w;
int app_expect(const char *what, int *got)
{
    int g, i;
    if (!strcmp(what, "same"))
        g = !memcmp(&snap_w, &proj.w, sizeof snap_w);
    else if (!strcmp(what, "preset"))
        g = proj.preset;
    else if (!strcmp(what, "format"))
        g = (int)proj.format;
    else if (!strncmp(what, "pv", 2))
        g = pv(atoi(what + 2) % P_NPARAMS);
    else if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "frozen"))
        g = en_frozen;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "pool"))
        g = proj.pool;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strcmp(what, "dirty"))
        g = ui.dirty;
    else if (!strcmp(what, "sixteenths"))
        g = (int)en_bars;
    else if (!strcmp(what, "pad_level"))
        g = (int)(en_pad_level * 100.0f);
    else if (!strcmp(what, "growth"))
        g = proj.growth;
    else if (!strcmp(what, "living"))
        g = garden_living();
    else if (!strncmp(what, "stage", 5))
        g = garden_count(atoi(what + 5));
    else if (!strncmp(what, "plot", 4))
        g = proj.w.p[atoi(what + 4) % G_NPLOT].stage;
    else if (!strcmp(what, "born"))
        g = proj.w.born;
    else if (!strcmp(what, "died"))
        g = proj.w.died;
    else if (!strcmp(what, "hybrids"))
        g = proj.w.hybrids;
    else if (!strcmp(what, "nibbles"))
        g = proj.w.nibbles;
    else if (!strcmp(what, "frosts"))
        g = proj.w.frosts;
    else if (!strcmp(what, "rains"))
        g = proj.w.rains;
    else if (!strcmp(what, "wild"))
        g = proj.w.wild;
    else if (!strcmp(what, "yours"))
        g = proj.w.yours;
    else if (!strcmp(what, "gardener"))
        g = proj.w.g.name;
    else if (!strcmp(what, "mood"))
        g = proj.w.g.mood;
    else if (!strcmp(what, "tended"))
        g = proj.w.g.tended;
    else if (!strcmp(what, "fav"))
        g = proj.w.g.fav_has;
    else if (!strcmp(what, "day"))
        g = proj.w.day;
    else if (!strcmp(what, "weather"))
        g = proj.w.weather;
    else if (!strcmp(what, "seconds"))
        g = (int)proj.w.seconds;
    else if (!strcmp(what, "rung"))
        g = (int)en_plot_rung;
    else if (!strcmp(what, "saves"))
        g = (int)saves;
    else if (!strcmp(what, "logseq"))
        g = (int)garden_log_seq;
    else if (!strcmp(what, "lineage")) {
        for (g = 0, i = 0; i < G_NPLOT; i++)
            g += proj.w.p[i].stage != ST_EMPTY && proj.w.p[i].pa_var != NO_PARENT;
    } else if (!strncmp(what, "par", 3))
        g = proj.par[atoi(what + 3) % P_NSAVED];
    else
        return 0;
    *got = g;
    return 1;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)b, (void)c, (void)d;
    if (!strcmp(cmd, "battery")) {
        batt_host = (int8_t)(strcmp(a, "off") ? atoi(a) : -2);
        return 1;
    }
    if (!strcmp(cmd, "weather")) {               /* the weather now (and its consequences: rain, frost) */
        weather_set(atoi(a) % W_N);
        return 1;
    }
    if (!strcmp(cmd, "foreign")) {               /* AMB-1 Drift's project (magic "ENO1", format 5, its size) */
        static uint8_t o[1200];
        uint32_t magic = 0x314F4E45u, format = 5u;
        int i;
        for (i = 0; i < (int)sizeof o; i++)
            o[i] = (uint8_t)(i * 7 + 3);
        memcpy(o, &magic, 4);
        memcpy(o + 4, &format, 4);
        plat_store_save(OBJ_PROJ, o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "junk")) {                  /* ours, damaged: a plant's stage out of range */
        static project_t o;
        o = proj;
        o.w.p[3].stage = 42;
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "longest")) {               /* the widest event line there can be, on top (for a screenshot) */
        plant_t a;
        memset(&a, 0, sizeof a);
        a.adj = 0, a.var = 9;                    /* Lantern Foxglove */
        bee_line(&a, &a);
        ev_push();
        return 1;
    }
    if (!strcmp(cmd, "storev1")) {               /* 1.0's project: format 1, no preset (the byte was spare, 0) */
        project_t o = proj;
        o.format = 1u;
        o.preset = 0;
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        snap_w = proj.w;
        return 1;
    }
    if (!strcmp(cmd, "snap")) {                  /* the garden remembered (expect same: it is as it was) */
        snap_w = proj.w;
        return 1;
    }
    if (!strcmp(cmd, "clear")) {                 /* every plot emptied */
        int i;
        for (i = 0; i < G_NPLOT; i++) {
            proj.w.p[i].stage = ST_EMPTY;
            proj.w.g.fav_has = 0;           /* (no favourite to replant: the wall's seeds) */
            gw.sync |= (uint16_t)(1u << i);
        }
        return 1;
    }
    if (!strcmp(cmd, "age")) {
        int i, n = atoi(a);
        for (i = 0; i < G_NPLOT; i++)
            if (proj.w.p[i].stage)
                proj.w.p[i].age = (uint16_t)(proj.w.p[i].age + n > 60000 ? 60000 : proj.w.p[i].age + n);
        return 1;
    }
    return 0;
}

void app_host_reboot(void)
{
    memset(&proj, 0, sizeof proj);
    memset(&gw, 0, sizeof gw);
    garden_changed = 0;
}

void app_speak_dump(void) { speak_dump(); }
