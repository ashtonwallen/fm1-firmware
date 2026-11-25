/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * KITE HILL: ashtonwallen, 2026 */
/* KITE HILL in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): its checks for
 * "expect", its commands, a reboot and the screen reader's dump.
 *
 * Checks: playing view style dirty hold day phase pos chord kites launches gusts changes oct leds vu parN slotNpM
 * savedday savedpos (fm1-base's own: store_writes peak_db_max peak_db_min ...). Commands: battery N (plat_battery
 * as N, "battery off" the simulator's own); storeamb (an AMB-1 Drift project in the store); storejunk (this
 * firmware's magic, a wrong format). */

int app_expect(const char *what, int *got)
{
    int g;
    if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "style"))
        g = proj.style;
    else if (!strcmp(what, "dirty"))
        g = ui.dirty;
    else if (!strcmp(what, "hold"))
        g = en_hold_on;
    else if (!strcmp(what, "day"))
        g = (int)en_day;
    else if (!strcmp(what, "phase"))
        g = en_phase;
    else if (!strcmp(what, "pos"))
        g = (int)(en_daypos * 10000.0f);
    else if (!strcmp(what, "chord"))
        g = en_chord_i;
    else if (!strcmp(what, "kites"))
        g = en_kites;
    else if (!strcmp(what, "launches"))
        g = (int)en_launches;
    else if (!strcmp(what, "gusts"))
        g = (int)en_gusts;
    else if (!strcmp(what, "changes"))
        g = (int)en_changes;
    else if (!strcmp(what, "oct"))
        g = proj.oct;
    else if (!strcmp(what, "leds"))
        g = proj.leds;
    else if (!strcmp(what, "vu"))
        g = (int)(fm_maxf(en_vu_l, en_vu_r) * 100.0f);
    else if (!strcmp(what, "savedday"))
        g = (int)proj.day;
    else if (!strcmp(what, "savedpos"))
        g = (int)proj.pos;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strncmp(what, "slot", 4) && strchr(what, 'p')) {   /* slotNpM: hill N's kept parameter M */
        const char *q = strchr(what, 'p');
        g = proj.slot[atoi(what + 4) % NSTYLES][atoi(q + 1) % P_NSTYLE];
    } else if (!strncmp(what, "par", 3))         /* parN: parameter N (kite.h P_*) */
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
