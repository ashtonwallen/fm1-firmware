/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * ORGAN TRIO: ashtonwallen, 2026 */
/* ORGAN TRIO in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): its checks for
 * "expect", its commands, a reboot and the screen reader's dump.
 *
 * Checks: playing view style dirty tune tunes beats phase role soloist climax kind repeat bpm key minor oct leds bpb
 * chorus vu applause parN slotNpM savedtune (fm1-base's own: store_writes peak_db_max peak_db_min ...). Commands: battery N
 * (plat_battery as N, "battery off" the simulator's own); storeamb (an AMB-1 Drift project in the store); storejazz (a JAZZ TRIO project, its
 * shape much like this one's);
 * storejunk (this firmware's magic, a wrong format). */

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
    else if (!strcmp(what, "tune"))
        g = (int)en_tune_no;
    else if (!strcmp(what, "tunes"))
        g = (int)en_tunes_played;
    else if (!strcmp(what, "beats"))
        g = (int)en_beats;
    else if (!strcmp(what, "phase"))
        g = en_phase;
    else if (!strcmp(what, "role"))
        g = en_role;
    else if (!strcmp(what, "soloist"))
        g = en_soloist;
    else if (!strcmp(what, "climax"))
        g = en_climax;
    else if (!strcmp(what, "kind"))
        g = en_kind;
    else if (!strcmp(what, "bpb"))
        g = en_bpb;
    else if (!strcmp(what, "chorus"))
        g = en_chorus;
    else if (!strcmp(what, "applause"))
        g = (int)(en_applause * 100.0f);
    else if (!strcmp(what, "repeat"))
        g = en_repeat_on;
    else if (!strcmp(what, "bpm"))
        g = en_bpm;
    else if (!strcmp(what, "key"))
        g = en_key;
    else if (!strcmp(what, "minor"))
        g = en_minor;
    else if (!strcmp(what, "oct"))
        g = proj.oct;
    else if (!strcmp(what, "leds"))
        g = proj.leds;
    else if (!strcmp(what, "vu"))
        g = (int)(fm_maxf(en_vu_l, en_vu_r) * 100.0f);
    else if (!strcmp(what, "savedtune"))
        g = (int)proj.tune_no;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strncmp(what, "slot", 4) && strchr(what, 'p')) {   /* slotNpM: style N's kept parameter M */
        const char *q = strchr(what, 'p');
        g = proj.slot[atoi(what + 4) % NSTYLES][atoi(q + 1) % P_NSTYLE];
    } else if (!strncmp(what, "par", 3))         /* parN: parameter N (organ.h P_*) */
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
    if (!strcmp(cmd, "storejazz")) {             /* JAZZ TRIO's project: magic "JAZ1", format 1, a similar size */
        static uint8_t o[sizeof(project_t)];
        uint32_t m = 0x315A414Au, f = 1u;
        unsigned i;
        for (i = 0; i < sizeof o; i++)
            o[i] = (uint8_t)(i & 3u);
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
