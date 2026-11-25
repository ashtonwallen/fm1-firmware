/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * RIGGING: ashtonwallen, 2026 */
/* RIGGING in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): its checks for
 * "expect", its commands, a reboot and the screen reader's dump.
 *
 * Checks: playing view style dirty hold leds sector squall calm sounding onsets chords courses gusts heading wind
 * (force, 0..10) bells (rung since the start) watch (seconds into the watch) waters worldok parN slotNpM (fm1-base's
 * own: store_writes peak_db_max peak_db_min ...). Commands: battery N (plat_battery as N, "battery off" the
 * simulator's own); storeamb (an AMB-1 Drift project in the store); storejunk (this firmware's magic, a wrong
 * format); squall (one now). */

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
    else if (!strcmp(what, "leds"))
        g = proj.leds;
    else if (!strcmp(what, "sector"))
        g = en_sector;
    else if (!strcmp(what, "squall"))
        g = en_squall;
    else if (!strcmp(what, "calm"))
        g = en_calm;
    else if (!strcmp(what, "sounding"))
        g = en_sounding;
    else if (!strcmp(what, "onsets"))
        g = (int)en_n_onsets;
    else if (!strcmp(what, "chords"))
        g = (int)en_n_chords;
    else if (!strcmp(what, "courses"))
        g = (int)en_n_courses;
    else if (!strcmp(what, "gusts"))
        g = (int)en_n_gusts;
    else if (!strcmp(what, "heading"))
        g = (int)(en_heading + 0.5f);
    else if (!strcmp(what, "wind"))
        g = en_beaufort(en_wind);
    else if (!strcmp(what, "bells"))
        g = (int)en_bell_no;
    else if (!strcmp(what, "watch"))
        g = (int)en_watch_s;
    else if (!strcmp(what, "waters"))
        g = en_waters;
    else if (!strcmp(what, "worldok"))
        g = proj.world_ok;
    else if (!strcmp(what, "vu"))
        g = (int)(fm_maxf(en_vu_l, en_vu_r) * 100.0f);
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strncmp(what, "slot", 4) && strchr(what, 'p')) {   /* slotNpM: preset N's kept parameter M */
        const char *q = strchr(what, 'p');
        g = proj.slot[atoi(what + 4) % NSTYLES][atoi(q + 1) % P_NSTYLE];
    } else if (!strncmp(what, "par", 3))         /* parN: parameter N (rig.h P_*) */
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
    if (!strcmp(cmd, "squall")) {
        en_dbg_squall();
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
