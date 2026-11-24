/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Changed for TIDE CLOCK by ashtonwallen: the world's checks and commands. */
/* TIDE CLOCK in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this).
 *
 * Checks for "expect": playing view setting clock cycle sea leds dirty min (the time of day in minutes) day phase wx
 * fog wind storm (x100) lamp act (keeper's activity, KA_*) where (AT_*) mood trust late sheltered stayed storms fogs
 * shells lates visits logn logtype (the newest entry's EV_*) boatsout tide (x100) parN (the engine's value N, amb.h
 * P_*) tgtN (its target) battery preset (1.1) format (the project's). Commands: battery N (plat_battery as N, "off" its own); settime HH MM (the world's
 * clock, as set by hand); weather calm|fog|wind|storm [MINUTES] (the weather now, its amounts at once); storeamb
 * (another firmware's project, AMB-1 Drift's "ENO1" format 5, in the store); storev1 (the project as it is now, saved
 * as 1.0 saved it: format 1, the preset's byte spare); seed N (the world's dice). */

int app_expect(const char *what, int *got)
{
    int g;
    if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "setting"))
        g = ui.setting;
    else if (!strcmp(what, "clock"))
        g = proj.clock;
    else if (!strcmp(what, "cycle"))
        g = proj.cycle;
    else if (!strcmp(what, "sea"))
        g = proj.sea;
    else if (!strcmp(what, "leds"))
        g = proj.leds;
    else if (!strcmp(what, "preset"))
        g = proj.preset;
    else if (!strcmp(what, "format"))
        g = (int)proj.format;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strcmp(what, "dirty"))
        g = ui.dirty;
    else if (!strcmp(what, "min"))
        g = (int)world_min();
    else if (!strcmp(what, "day"))
        g = proj.day;
    else if (!strcmp(what, "phase"))
        g = world_phase();
    else if (!strcmp(what, "wx"))
        g = proj.wx;
    else if (!strcmp(what, "fog"))
        g = (int)(w.fog * 100.0f + 0.5f);
    else if (!strcmp(what, "wind"))
        g = (int)(w.wind * 100.0f + 0.5f);
    else if (!strcmp(what, "storm"))
        g = (int)(w.storm * 100.0f + 0.5f);
    else if (!strcmp(what, "lamp"))
        g = lamp_lit();
    else if (!strcmp(what, "act"))
        g = keeper_act();
    else if (!strcmp(what, "where"))
        g = keeper_where(keeper_act());
    else if (!strcmp(what, "mood"))
        g = proj.mood;
    else if (!strcmp(what, "trust"))
        g = proj.trust;
    else if (!strcmp(what, "late"))
        g = proj.late;
    else if (!strcmp(what, "sheltered"))
        g = proj.sheltered;
    else if (!strcmp(what, "stayed"))
        g = proj.stayed;
    else if (!strcmp(what, "storms"))
        g = proj.storms;
    else if (!strcmp(what, "fogs"))
        g = proj.fogs;
    else if (!strcmp(what, "shells"))
        g = proj.shells;
    else if (!strcmp(what, "lates"))
        g = proj.lates;
    else if (!strcmp(what, "visits"))
        g = proj.visits;
    else if (!strcmp(what, "logn"))
        g = proj.log_n;
    else if (!strcmp(what, "logtype"))
        g = log_get(0) ? log_get(0)->type : 0;
    else if (!strcmp(what, "boatsout"))
        g = boats_out();
    else if (!strcmp(what, "tide"))
        g = (int)(world_tide() * 100.0f + 0.5f);
    else if (!strncmp(what, "par", 3))
        g = w.cur[atoi(what + 3) % P_NPARAMS];
    else if (!strncmp(what, "tgt", 3))
        g = w.tgt[atoi(what + 3) % P_NPARAMS];
    else
        return 0;
    *got = g;
    return 1;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)c, (void)d;
    if (!strcmp(cmd, "battery")) {               /* battery N: plat_battery as N (-1..4), "battery off" its own */
        batt_host = (int8_t)(strcmp(a, "off") ? atoi(a) : -2);
        return 1;
    }
    if (!strcmp(cmd, "settime")) {
        world_set_min((uint32_t)(atoi(a) * 60 + atoi(b)));
        return 1;
    }
    if (!strcmp(cmd, "weather")) {
        int k;
        for (k = 0; k < NWX; k++) {
            char n[8];
            int i;
            for (i = 0; WX_NAME[k][i]; i++)
                n[i] = (char)(WX_NAME[k][i] | 0x20);
            n[i] = 0;
            if (!strcmp(a, n)) {
                weather_set(k, b && b[0] ? (uint32_t)atoi(b) : 120u);
                weather_ease(1.0f);
                return 1;
            }
        }
        return 0;
    }
    if (!strcmp(cmd, "storeamb")) {               /* AMB-1 Drift's project (magic "ENO1", format 5): not ours */
        static uint8_t o[1200];
        memset(o, 0x2A, sizeof o);
        o[0] = 0x45, o[1] = 0x4E, o[2] = 0x4F, o[3] = 0x31;
        o[4] = 5, o[5] = o[6] = o[7] = 0;
        plat_store_save(OBJ_PROJ, o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "storev1")) {                /* 1.0's project: format 1, no preset (the byte was spare, 0) */
        project_t o = proj;
        o.format = 1u;
        o.preset = 0;
        o.tod = w.ms / 1000u;
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "seed")) {
        proj.seed = (uint32_t)strtoul(a, 0, 0);
        return 1;
    }
    return 0;
}

void app_host_reboot(void)
{
    memset(&proj, 0, sizeof proj);
    memset(&w, 0, sizeof w);
}

void app_speak_dump(void) { speak_dump(); }
