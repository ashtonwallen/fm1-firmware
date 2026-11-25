/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * FELT PIANO: ashtonwallen, 2026 */
/* FELT PIANO in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): its checks for
 * "expect", its commands, a reboot and the screen reader's dump.
 *
 * Checks: playing view style dirty track tracks notes phase idiom repeat bpm key mode pedal voices oct leds trackms
 * tracklen vu parN slotNpM savedtrack (fm1-base's own: store_writes peak_db_max peak_db_min ...). Commands: battery N
 * (plat_battery as N, "battery off" the simulator's own); storeamb (an AMB-1 Drift project in the store);
 * storejunk (this firmware's magic, a wrong format); storev1, storev2 (a project 1.0, 1.1 saved). */

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
    else if (!strcmp(what, "track"))
        g = (int)en_track_no;
    else if (!strcmp(what, "tracks"))
        g = (int)en_tracks_played;
    else if (!strcmp(what, "notes"))
        g = (int)en_notes_played;
    else if (!strcmp(what, "phase"))
        g = en_phase;
    else if (!strcmp(what, "idiom"))
        g = en_idiom;
    else if (!strcmp(what, "pedal"))
        g = en_pedal;
    else if (!strcmp(what, "voices"))
        g = en_voices;
    else if (!strcmp(what, "repeat"))
        g = en_repeat_on;
    else if (!strcmp(what, "bpm"))
        g = en_bpm;
    else if (!strcmp(what, "key"))
        g = en_key;
    else if (!strcmp(what, "mode"))
        g = en_mode;
    else if (!strcmp(what, "oct"))
        g = proj.oct;
    else if (!strcmp(what, "leds"))
        g = proj.leds;
    else if (!strcmp(what, "trackms"))
        g = (int)en_track_ms;
    else if (!strcmp(what, "tracklen"))
        g = (int)en_track_len_ms;
    else if (!strcmp(what, "vu"))
        g = (int)(fm_maxf(en_vu_l, en_vu_r) * 100.0f);
    else if (!strcmp(what, "savedtrack"))
        g = (int)proj.track_no;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strncmp(what, "slot", 4) && strchr(what, 'p')) {   /* slotNpM: style N's kept parameter M */
        const char *q = strchr(what, 'p');
        g = proj.slot[atoi(what + 4) % NSTYLES][atoi(q + 1) % P_NSTYLE];
    } else if (!strncmp(what, "par", 3))         /* parN: parameter N (felt.h P_*) */
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
    if (!strcmp(cmd, "storev1")) {                /* 1.0's project (format 1, fourteen styles): style 12, its slot's
                                                  * first knob 44, Length 3, octave 4, lights 2, piece 40 */
        static project_v1_t o;
        int i;
        memset(&o, 0, sizeof o);
        o.magic = PROJ_MAGIC;
        o.format = 1u;
        for (i = 0; i < EN_NSTYLES_V1; i++)
            en_style_recipe(i, o.slot[i]);
        o.slot[12][0] = 44;
        memcpy(o.par, o.slot[12], sizeof o.slot[12]);
        o.par[P_LENGTH] = 3;
        o.style = 12;
        o.leds = 2;
        o.oct = 4;
        o.track_no = 40u;
        o.seed = 0x46454C54u;
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "storev2")) {                /* 1.1's project (format 2, thirty styles): style 29, its slot's
                                                  * first knob 61, Length 2, octave 1, lights 0, piece 70 */
        static project_v2_t o;
        int i;
        memset(&o, 0, sizeof o);
        o.magic = PROJ_MAGIC;
        o.format = 2u;
        for (i = 0; i < EN_NSTYLES_V2; i++)
            en_style_recipe(i, o.slot[i]);
        o.slot[29][0] = 61;
        memcpy(o.par, o.slot[29], sizeof o.slot[29]);
        o.par[P_LENGTH] = 2;
        o.style = 29;
        o.leds = 0;
        o.oct = 1;
        o.track_no = 70u;
        o.seed = 0x46454C54u;
        plat_store_save(OBJ_PROJ, &o, sizeof o);
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
