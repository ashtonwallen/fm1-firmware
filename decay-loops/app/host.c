/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (AMB-1)
 * Copyright (C) 2026 ashtonwallen (DECAY LOOPS) */
/* DECAY LOOPS in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): the checks for
 * "expect", the commands, a reboot and the screen reader's dump.
 *
 * Checks: playing frozen view preset dirty keyoct leds layers (loops there are) rec (one recording) loops (recorded
 * since the start) wear (the newest playing loop's, %) passes tape (% used) speed (%) parN slotNpM battery
 * (fm1-base's own: store_writes clocks_out midi_out peak_db_max peak_db_min).
 * Commands: storeamb (another firmware's project where ours goes: AMB-1 Drift's, magic "ENO1");
 * storeold (ours, but another format); battery N (plat_battery as N, -1..4; "battery off" the simulator's own). */

int app_expect(const char *what, int *got)
{
    int g, i;
    if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "frozen"))
        g = en_frozen;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "preset"))
        g = proj.preset;
    else if (!strcmp(what, "dirty"))
        g = ui.dirty;
    else if (!strcmp(what, "keyoct"))
        g = proj.keyoct;
    else if (!strcmp(what, "leds"))
        g = proj.leds;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strcmp(what, "layers")) {
        for (g = 0, i = 0; i < EN_NLAYER; i++)
            g += en_lay_state[i] != LS_FREE;
    } else if (!strcmp(what, "rec"))
        g = rec_layer() >= 0;
    else if (!strcmp(what, "loops"))
        g = (int)en_loops;
    else if (!strcmp(what, "wear")) {
        i = prim();
        g = i >= 0 ? (int)(en_lay_wear[i] * 100.0f) : -1;
    } else if (!strcmp(what, "passes")) {
        i = prim();
        g = i >= 0 ? (int)en_passes[i] : -1;
    } else if (!strcmp(what, "tape"))
        g = (int)(en_tape_used * 100.0f);
    else if (!strcmp(what, "speed"))
        g = (int)(en_speed * 100.0f);
    else if (!strncmp(what, "slot", 4) && strchr(what, 'p')) {   /* slotNpM: preset N's kept parameter M */
        const char *q = strchr(what, 'p');
        g = proj.slot[atoi(what + 4) % NPRESETS].par[atoi(q + 1) % P_NPARAMS];
    } else if (!strncmp(what, "par", 3))          /* parN: parameter N (decay.h P_*) */
        g = proj.par[atoi(what + 3) % P_NPARAMS];
    else
        return 0;
    *got = g;
    return 1;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)b, (void)c, (void)d;
    if (!strcmp(cmd, "battery")) {               /* battery N: plat_battery as N (-1..4), "battery off" its own */
        batt_host = (int8_t)(strcmp(a, "off") ? atoi(a) : -2);
        return 1;
    }
    if (!strcmp(cmd, "storeamb")) {              /* AMB-1 Drift's project (format 6) where ours is kept */
        static uint8_t o[1800];
        uint32_t i, m = 0x314F4E45u, f = 6u;
        for (i = 0; i < sizeof o; i++)
            o[i] = (uint8_t)(i * 37u + 11u);
        memcpy(o, &m, 4);
        memcpy(o + 4, &f, 4);
        plat_store_save(OBJ_PROJ, o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "storeold")) {              /* ours, a format this build does not know */
        static project_t o;
        o = proj;
        o.format = PROJ_FORMAT + 1u;
        o.par[P_DECAY] = 99;
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        return 1;
    }
    return 0;
}

void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }

void app_speak_dump(void) { speak_dump(); }
