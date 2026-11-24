/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; NIGHT TRAIN: ashtonwallen */
/* NIGHT TRAIN in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): the app's checks
 * for "expect", its commands, a reboot and the screen reader's dump.
 *
 * Checks: playing frozen view dirty sixteenths pad_level station phase land kmh km nlog stretch clack tone scroll
 * aboardN asleepN beatN tripsN (regular N) strangers rain tunnel parN (the engine's setting N as the app sets it)
 * battery line format (the project's). storev1: the project now, saved as 1.0 saved it (format 1, no line). Commands: battery N; express (PRESETS' express); ff MIN (the journey MIN minutes on, the world alone, no
 * sound: days in a moment); storeold (a project of AMB-1's format "ENO1" in the store); story (print the journal). */

int app_expect(const char *what, int *got)
{
    int g;
    const world_t *w = &proj.w;
    if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "frozen"))
        g = en_frozen;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strcmp(what, "dirty"))
        g = ui.dirty;
    else if (!strcmp(what, "sixteenths"))
        g = (int)en_bars;
    else if (!strcmp(what, "pad_level"))
        g = (int)(en_pad_level * 100.0f);
    else if (!strcmp(what, "station"))
        g = (int)w->station;
    else if (!strcmp(what, "phase"))
        g = w->phase;
    else if (!strcmp(what, "land"))
        g = w->land;
    else if (!strcmp(what, "kmh"))
        g = (int)(w->speed * 3.6f + 0.5f);
    else if (!strcmp(what, "km"))
        g = (int)world_km(w);
    else if (!strcmp(what, "nlog"))
        g = w->nlog;
    else if (!strcmp(what, "stretch"))
        g = proj.stretch;
    else if (!strcmp(what, "clack"))
        g = proj.clack;
    else if (!strcmp(what, "line"))
        g = proj.line;
    else if (!strcmp(what, "format"))
        g = (int)proj.format;
    else if (!strcmp(what, "tone"))
        g = proj.tone;
    else if (!strcmp(what, "scroll"))
        g = ui.scroll;
    else if (!strcmp(what, "strangers"))
        g = w->strangers;
    else if (!strcmp(what, "rain"))
        g = w->rain;
    else if (!strcmp(what, "tunnel"))
        g = world_in_tunnel(w);
    else if (!strncmp(what, "aboard", 6))
        g = w->who[atoi(what + 6) % NREG].aboard;
    else if (!strncmp(what, "asleep", 6))
        g = w->who[atoi(what + 6) % NREG].asleep;
    else if (!strncmp(what, "beat", 4))
        g = w->who[atoi(what + 4) % NREG].beat;
    else if (!strncmp(what, "trips", 5))
        g = w->who[atoi(what + 5) % NREG].trips;
    else if (!strncmp(what, "par", 3))
        g = mus_cur[atoi(what + 3) % P_NPARAMS];
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
    if (!strcmp(cmd, "express")) {
        world_express(&proj.w);
        return 1;
    }
    if (!strcmp(cmd, "ff")) {                    /* ff MIN: the world alone, MIN minutes on */
        uint32_t k, n = (uint32_t)atoi(a) * 60u * 10u, ev = 0;
        for (k = 0; k < n; k++)
            ev |= world_tick(&proj.w, 100u, W_STRETCH_MIN[proj.stretch]);
        if (ev & W_SWAP)
            music_apply_all();
        ui.wdirty = 1;
        return 1;
    }
    if (!strcmp(cmd, "land")) {                  /* land N: this stretch's landscape (screenshots) */
        proj.w.land = (uint8_t)(atoi(a) % NLAND);
        music_swap();
        ui.sig[1] = 0;
        return 1;
    }
    if (!strcmp(cmd, "tunnel") || !strcmp(cmd, "bridge")) {   /* the train inside one, now */
        float *p0 = cmd[0] == 't' ? &proj.w.tun0 : &proj.w.brg0, *p1 = cmd[0] == 't' ? &proj.w.tun1 : &proj.w.brg1;
        *p0 = proj.w.pos - 100.0f > 0.0f ? proj.w.pos - 100.0f : 0.0f;
        *p1 = proj.w.pos + 400.0f;
        return 1;
    }
    if (!strcmp(cmd, "rain")) {
        proj.w.rain = (uint8_t)atoi(a);
        return 1;
    }
    if (!strcmp(cmd, "storeold")) {              /* AMB-1's project: magic "ENO1", format 4 */
        static uint8_t o[2048];
        uint32_t m = 0x314F4E45u, f = 4u;
        memset(o, 0x41, sizeof o);
        memcpy(o, &m, 4);
        memcpy(o + 4, &f, 4);
        plat_store_save(OBJ_PROJ, o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "storev1")) {               /* 1.0's project: format 1, its spare byte (now the line) 0 */
        static project_t o;
        o = proj;
        o.format = 1u;
        o.line = 0;
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "story")) {                 /* the journal, oldest first */
        int i;
        char t[64];
        for (i = nlog_kept() - 1; i >= 0; i--) {
            world_text(&proj.w, world_log(&proj.w, i), t, sizeof t);
            printf("  journal: %s\n", t);
        }
        return 1;
    }
    return 0;
}

void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }

void app_speak_dump(void) { speak_dump(); }
