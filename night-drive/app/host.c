/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Changed for NIGHT DRIVE by ashtonwallen: its own checks and commands. */
/* NIGHT DRIVE in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): the checks for
 * "expect", the commands, a reboot and the screen reader's dump.
 *
 * Checks: playing view preset dirty section secbar seclen key (the key now) home (the preset's home key) prog chord
 * (the chord's root) sixteenths keychanges arppat lead (a lead phrase on) speed bright arp drift drums leadlvl leds usb
 * odo battery energy (x100) gr (the limiter's gain x100) (fm1-base's own: store_writes peak_db_max peak_db_min ...).
 * Commands: battery N (plat_battery as N, -1..4; "battery off" the simulator's own); storeamb (an AMB-1 project in
 * the store: another firmware's); odo N (the odometer at N tenths). */

int app_expect(const char *what, int *got)
{
    int g;
    const slot_t *s = &proj.slot[proj.preset];
    if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "preset"))
        g = proj.preset;
    else if (!strcmp(what, "dirty"))
        g = ui.dirty;
    else if (!strcmp(what, "section"))
        g = en_section;
    else if (!strcmp(what, "secbar"))
        g = en_sec_bar;
    else if (!strcmp(what, "seclen"))
        g = en_sec_len;
    else if (!strcmp(what, "key"))
        g = en_key;
    else if (!strcmp(what, "home"))
        g = s->key;
    else if (!strcmp(what, "prog"))
        g = s->prog;
    else if (!strcmp(what, "engprog"))
        g = en_prog;
    else if (!strcmp(what, "chord"))
        g = en_chord_root;
    else if (!strcmp(what, "sixteenths"))
        g = (int)en_sixteenths;
    else if (!strcmp(what, "keychanges"))
        g = (int)en_keychanges;
    else if (!strcmp(what, "arppat"))
        g = en_arp_pat;
    else if (!strcmp(what, "lead"))
        g = en_lead_on;
    else if (!strcmp(what, "speed"))
        g = s->knob[0];
    else if (!strcmp(what, "bright"))
        g = s->knob[1];
    else if (!strcmp(what, "arp"))
        g = s->knob[2];
    else if (!strcmp(what, "drift"))
        g = s->knob[3];
    else if (!strcmp(what, "drums"))
        g = proj.drums;
    else if (!strcmp(what, "leadlvl"))
        g = proj.lead;
    else if (!strcmp(what, "leds"))
        g = proj.leds;
    else if (!strcmp(what, "usb"))
        g = proj.usb_full;
    else if (!strcmp(what, "odo"))
        g = (int)proj.odo;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strcmp(what, "energy"))
        g = (int)(en_energy * 100.0f);
    else if (!strcmp(what, "gr"))
        g = (int)(en_gr * 100.0f);
    else if (!strcmp(what, "help"))
        g = ui.help;
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
    if (!strcmp(cmd, "storeamb")) {              /* AMB-1's project (magic "ENO1", format 6), as big as its own */
        static uint8_t o[1200];
        uint32_t m = 0x314F4E45u, f = 6u;
        memset(o, 0x11, sizeof o);
        memcpy(o, &m, 4);
        memcpy(o + 4, &f, 4);
        plat_store_save(OBJ_PROJ, o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "storeshort")) {            /* NIGHT DRIVE's magic and format, but cut short: damaged */
        plat_store_save(OBJ_PROJ, &proj, sizeof proj / 2);
        return 1;
    }
    if (!strcmp(cmd, "odo")) {
        proj.odo = (uint32_t)atoi(a);
        return 1;
    }
    return 0;
}

void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }

void app_speak_dump(void) { speak_dump(); }
