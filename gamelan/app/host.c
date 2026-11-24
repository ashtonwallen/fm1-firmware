/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; GAMELAN: Copyright (C) 2026 ashtonwallen */
/* GAMELAN in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): the checks for
 * "expect", the commands, a reboot and the screen reader's dump.
 *
 * Checks: playing view preset dirty parN slotNpM leds hold state pieces gongs strokes ticks irama gongan nbeats form
 * battery (fm1-base's own: store_writes peak_db_max peak_db_min ...). Commands: battery N (plat_battery as N, -1..4;
 * "battery off" the simulator's own); storeforeign (another firmware's project in the same place: AMB-1 Drift's
 * magic and format); storebad (ours, damaged: a value out of range). */

int app_expect(const char *what, int *got)
{
    int g;
    if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "preset"))
        g = proj.preset;
    else if (!strcmp(what, "dirty"))
        g = ui.dirty;
    else if (!strcmp(what, "leds"))
        g = proj.leds;
    else if (!strcmp(what, "hold"))
        g = proj.hold * 10 + en_hold_on;
    else if (!strcmp(what, "state"))
        g = en_state;
    else if (!strcmp(what, "pieces"))
        g = (int)en_pieces;
    else if (!strcmp(what, "gongs"))
        g = (int)en_gongs;
    else if (!strcmp(what, "strokes"))
        g = (int)en_strokes;
    else if (!strcmp(what, "ticks"))
        g = (int)en_ticks;
    else if (!strcmp(what, "irama"))
        g = en_level;
    else if (!strcmp(what, "iramachanges"))
        g = (int)en_irama_changes;
    else if (!strcmp(what, "gongan"))
        g = en_gongan;
    else if (!strcmp(what, "nbeats"))
        g = en_nbeats;
    else if (!strcmp(what, "form"))
        g = en_form;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strncmp(what, "slot", 4) && strchr(what, 'p')) {   /* slotNpM: preset N's kept parameter M */
        const char *q = strchr(what, 'p');
        g = proj.slot[atoi(what + 4) % NPRESETS].par[atoi(q + 1) % P_NPARAMS];
    } else if (!strncmp(what, "par", 3))          /* parN: parameter N (gam.h P_*) */
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
    if (!strcmp(cmd, "storeforeign")) {          /* AMB-1 Drift's project (magic "ENO1", format 6) in our place */
        static uint8_t o[1200];
        uint32_t m = 0x314F4E45u, f = 6u;
        int i;
        for (i = 0; i < (int)sizeof o; i++)
            o[i] = (uint8_t)(i * 37 + 11);
        memcpy(o, &m, 4);
        memcpy(o + 4, &f, 4);
        plat_store_save(OBJ_PROJ, o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "storebad")) {              /* ours, with a value out of its range */
        static project_t o;
        o = proj;
        o.slot[3].par[P_TEMPO] = 999;
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        return 1;
    }
    return 0;
}

void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }

void app_speak_dump(void) { speak_dump(); }
