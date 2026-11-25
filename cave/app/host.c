/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CAVE: ashtonwallen, 2026 */
/* CAVE in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): its checks for "expect",
 * its commands, a reboot and the screen reader's dump.
 *
 * Checks: playing view style dirty notes voices oct leds vu harm minutes floods stones retunes wet worldN (cave N's
 * saved world minutes, -1 none) parN slotNpM (fm1-base's own: store_writes peak_db_max peak_db_min ...).
 * Commands: battery N (plat_battery as N, "battery off" the simulator's own); ff MIN (the world runs on MIN minutes,
 * silently); flood | stone | bats | trickle (an event now); storeamb (an AMB-1 Drift project in the store);
 * storejunk (this firmware's magic, a wrong format). */

int app_expect(const char *what, int *got)
{
    int g;
    en_world_t w;
    en_world_get(&w);
    if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "style"))
        g = proj.style;
    else if (!strcmp(what, "dirty"))
        g = ui.dirty;
    else if (!strcmp(what, "notes"))
        g = (int)en_notes_played;
    else if (!strcmp(what, "voices"))
        g = en_voices;
    else if (!strcmp(what, "oct"))
        g = proj.oct;
    else if (!strcmp(what, "leds"))
        g = proj.leds;
    else if (!strcmp(what, "vu"))
        g = (int)(fm_maxf(en_vu_l, en_vu_r) * 100.0f);
    else if (!strcmp(what, "harm"))
        g = w.harm;
    else if (!strcmp(what, "minutes"))
        g = (int)w.minutes;
    else if (!strcmp(what, "floods"))
        g = w.floods;
    else if (!strcmp(what, "stones"))
        g = w.stones;
    else if (!strcmp(what, "retunes"))
        g = (int)en_retunes;
    else if (!strcmp(what, "wet"))
        g = (int)(en_wet * 100.0f);
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strncmp(what, "world", 5)) {       /* worldN: cave N's world as saved: its minutes (-1: none) */
        int i = atoi(what + 5) % NSTYLES;
        g = proj.world[i].magic ? (int)proj.world[i].minutes : -1;
    } else if (!strncmp(what, "slot", 4) && strchr(what, 'p')) {   /* slotNpM: cave N's kept parameter M */
        const char *q = strchr(what, 'p');
        g = proj.slot[atoi(what + 4) % NSTYLES][atoi(q + 1) % P_NSTYLE];
    } else if (!strncmp(what, "par", 3))         /* parN: parameter N (cave.h P_*) */
        g = proj.par[atoi(what + 3) % P_NPARAMS];
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
    if (!strcmp(cmd, "ff")) {
        en_ff(atoi(a));
        return 1;
    }
    if (!strcmp(cmd, "flood") || !strcmp(cmd, "stone") || !strcmp(cmd, "bats") || !strcmp(cmd, "trickle")) {
        en_event(cmd[0] == 'f' ? EV_FLOOD : cmd[0] == 's' ? EV_STONE : cmd[0] == 'b' ? EV_BATS : EV_RUN);
        return 1;
    }
    if (!strcmp(cmd, "storeamb")) {               /* AMB-1 Drift's project as it saves it: magic "ENO1", format 6 */
        static uint8_t o[2400];
        uint32_t m = 0x314F4E45u, f = 6u;
        unsigned i;
        for (i = 0; i < sizeof o; i++)
            o[i] = (uint8_t)(i * 7u);
        memcpy(o, &m, 4);
        memcpy(o + 4, &f, 4);
        plat_store_save(OBJ_PROJ, o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "storejunk")) {              /* this firmware's magic, the size right, a format it never wrote */
        static project_t o;
        o = proj;
        o.format = 99u;
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        return 1;
    }
    return 0;
}

void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }

void app_speak_dump(void) { speak_dump(); }
