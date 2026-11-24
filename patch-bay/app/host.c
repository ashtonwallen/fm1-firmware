/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen; built on AMB-1 by Charles Vestal */
/* PATCH BAY in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): its checks for
 * "expect", its commands, a reboot and the screen reader's dump.
 *
 * Checks: playing preset cables musical sel jack hold dirty autor moves rmoves level (the output's level x 1000)
 * eighths battery tempo space root scale parN (module N's knob) cabN (input N's output, -1 none) seqN.
 * Commands: battery N (plat_battery as N, -1..4; "battery off" the simulator's own); storeforeign (another
 * firmware's project in the store: AMB-1's, its magic and size); automove (the next move due now); musical_all
 * (every move so far kept the patch musical: a failure counted otherwise). */

int app_expect(const char *what, int *got)
{
    int g;
    if (!strcmp(what, "playing"))
        g = pb_playing;
    else if (!strcmp(what, "preset"))
        g = proj.preset;
    else if (!strcmp(what, "cables"))
        g = pb_ncables(proj.cab);
    else if (!strcmp(what, "musical"))
        g = pb_musical(proj.cab, proj.par);
    else if (!strcmp(what, "legal"))
        g = pb_legal(proj.cab);
    else if (!strcmp(what, "sel"))
        g = proj.sel;
    else if (!strcmp(what, "jack"))
        g = cursor_jack();
    else if (!strcmp(what, "hold"))
        g = cur.hold;
    else if (!strcmp(what, "dirty"))
        g = ui.dirty;
    else if (!strcmp(what, "autor"))
        g = proj.autor;
    else if (!strcmp(what, "moves"))
        g = (int)au.moves;
    else if (!strcmp(what, "rmoves"))            /* connections the render has taken */
        g = (int)pb_moves;
    else if (!strcmp(what, "level"))
        g = (int)(pb_level * 1000.0f);
    else if (!strcmp(what, "eighths"))
        g = (int)pb_eighths;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strcmp(what, "tempo"))
        g = proj.g[G_TEMPO];
    else if (!strcmp(what, "space"))
        g = proj.g[G_SPACE];
    else if (!strcmp(what, "root"))
        g = proj.g[G_ROOT];
    else if (!strcmp(what, "scale"))
        g = proj.g[G_SCALE];
    else if (!strcmp(what, "leds"))
        g = proj.leds;
    else if (!strncmp(what, "par", 3))
        g = proj.par[atoi(what + 3) % PB_NMOD];
    else if (!strncmp(what, "cab", 3))
        g = proj.cab[atoi(what + 3) % PB_NIN];
    else if (!strncmp(what, "seq", 3))
        g = proj.seq[atoi(what + 3) & 7];
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
    if (!strcmp(cmd, "storeforeign")) {          /* AMB-1 Drift's project ("ENO1", format 6), roughly its size */
        static uint8_t o[2200];
        uint32_t m = 0x314F4E45u, f = 6u;
        int i;
        for (i = 0; i < (int)sizeof o; i++)
            o[i] = (uint8_t)(i * 37 + 11);
        memcpy(o, &m, 4);
        memcpy(o + 4, &f, 4);
        plat_store_save(OBJ_PROJ, o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "storeshort")) {            /* our magic and format, but cut short (an older or damaged one) */
        project_t p = proj;
        plat_store_save(OBJ_PROJ, &p, 40);
        return 1;
    }
    if (!strcmp(cmd, "automove")) {
        au.next_t = plat_ms();
        au.hand_until = plat_ms();
        return 1;
    }
    return 0;
}

void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }

void app_speak_dump(void) { speak_dump(); }
