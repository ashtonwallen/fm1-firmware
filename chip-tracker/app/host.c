/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * CHIP TRACKER: ashtonwallen, 2026 */
/* CHIP TRACKER in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): its checks for
 * "expect", its commands, a reboot and the screen reader's dump.
 *
 * Checks: playing view style dirty song songs rows phase section repeat bpm key mode oct leds loop loops ord row
 * vu help parN slotNpM savedsong battery (fm1-base's own: store_writes peak_db_max peak_db_min ...). Section: 0 intro
 * 1 verse 2 chorus 3 bridge 4 ending. Commands: battery N (plat_battery as N, "battery off" the simulator's own);
 * storeamb (an AMB-1 Drift project in the store); storelofi (LOFI RADIO's); storejunk (this firmware's magic, a
 * wrong format). */

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
    else if (!strcmp(what, "song"))
        g = (int)en_song_no;
    else if (!strcmp(what, "songs"))
        g = (int)en_songs_played;
    else if (!strcmp(what, "rows"))
        g = (int)en_rows_played;
    else if (!strcmp(what, "phase"))
        g = en_phase;
    else if (!strcmp(what, "section"))
        g = sect_index(en_cur->osect[en_ord]);
    else if (!strcmp(what, "repeat"))
        g = en_repeat_on;
    else if (!strcmp(what, "bpm"))
        g = en_bpm;
    else if (!strcmp(what, "key"))
        g = en_cur->key;
    else if (!strcmp(what, "mode"))
        g = en_cur->mode;
    else if (!strcmp(what, "oct"))
        g = proj.oct;
    else if (!strcmp(what, "leds"))
        g = proj.leds;
    else if (!strcmp(what, "loop"))
        g = en_loop;
    else if (!strcmp(what, "loops"))
        g = en_loops;
    else if (!strcmp(what, "ord"))
        g = en_ord;
    else if (!strcmp(what, "row"))
        g = en_row;
    else if (!strcmp(what, "help"))
        g = ui.help;
    else if (!strcmp(what, "vu"))
        g = (int)(en_vu * 100.0f);
    else if (!strcmp(what, "savedsong"))
        g = (int)proj.song_no;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strncmp(what, "slot", 4) && strchr(what, 'p')) {   /* slotNpM: style N's kept parameter M */
        const char *q = strchr(what, 'p');
        g = proj.slot[atoi(what + 4) % NSTYLES][atoi(q + 1) % P_NSTYLE];
    } else if (!strncmp(what, "par", 3))         /* parN: parameter N (chip.h P_*) */
        g = proj.par[atoi(what + 3) % P_NPARAMS];
    else
        return 0;
    *got = g;
    return 1;
}

static void store_other(uint32_t m, uint32_t f, unsigned n)   /* another firmware's project, as it saves it */
{
    static uint8_t o[3000];
    unsigned i;
    for (i = 0; i < n && i < sizeof o; i++)
        o[i] = (uint8_t)(i * 7u);
    memcpy(o, &m, 4);
    memcpy(o + 4, &f, 4);
    plat_store_save(OBJ_PROJ, o, n);
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)b, (void)c, (void)d;
    if (!strcmp(cmd, "battery")) {
        batt_host = (int8_t)(strcmp(a, "off") ? atoi(a) : -2);
        return 1;
    }
    if (!strcmp(cmd, "storeamb")) {               /* AMB-1 Drift's: magic "ENO1", format 6 */
        store_other(0x314F4E45u, 6u, 2400u);
        return 1;
    }
    if (!strcmp(cmd, "storelofi")) {              /* LOFI RADIO's: magic "LOF1", format 1, this one's size */
        store_other(0x31464F4Cu, 1u, (unsigned)sizeof(project_t));
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
