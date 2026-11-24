/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (MONASTERY) */
/* MONASTERY in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): the checks for
 * "expect", the commands, a reboot and the screen reader's dump.
 *
 * Checks: playing frozen view dirty pad_level bass_level parN battery saves (saves by itself) office day phase
 * (0 bell, 1 chant, 2 quiet) singing (voices singing now) here (brothers in choir) events (since boot) chron
 * (chronicle entries) asleep (asleep now) skillN hereN vowedN slipsN napsN flatsN clashesN (brother N) rivalry.
 * Commands: battery N; days N (N new days at once, the world's daily turn: tests); office N (to office N, its bell);
 * storeamb (a project as AMB-1 Drift saves it, magic "ENO1", in the store: another firmware's) */

int app_expect(const char *what, int *got)
{
    int g, i;
    if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "frozen"))
        g = en_frozen;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strcmp(what, "dirty"))
        g = ui.dirty || wr.dirty;
    else if (!strcmp(what, "saves"))
        g = (int)ui.saves;
    else if (!strcmp(what, "pad_level"))
        g = (int)(en_pad_level * 100.0f);
    else if (!strcmp(what, "bass_level"))
        g = (int)(en_bass_level * 100.0f);
    else if (!strcmp(what, "office"))
        g = proj.world.office;
    else if (!strcmp(what, "day"))
        g = proj.world.day;
    else if (!strcmp(what, "phase"))
        g = wr.phase;
    else if (!strcmp(what, "singing")) {
        for (g = 0, i = 0; i < NMONK; i++)
            g += wr.monk_slot[i] >= 0;
    } else if (!strcmp(what, "here")) {
        for (g = 0, i = 0; i < NMONK; i++)
            g += mk(i)->here == 1;
    } else if (!strcmp(what, "asleep")) {
        for (g = 0, i = 0; i < NMONK; i++)
            g += wr.asleep[i];
    } else if (!strcmp(what, "events"))
        g = (int)wr.nevents;
    else if (!strcmp(what, "chron"))
        g = proj.world.nchron;
    else if (!strcmp(what, "rivalry"))
        g = proj.world.rivalry;
    else if (!strncmp(what, "skill", 5))
        g = mk(atoi(what + 5) % NMONK)->skill;
    else if (!strncmp(what, "here", 4))
        g = mk(atoi(what + 4) % NMONK)->here;
    else if (!strncmp(what, "vowed", 5))
        g = mk(atoi(what + 5) % NMONK)->vowed;
    else if (!strncmp(what, "slips", 5))
        g = mk(atoi(what + 5) % NMONK)->slips;
    else if (!strncmp(what, "naps", 4))
        g = mk(atoi(what + 4) % NMONK)->naps;
    else if (!strncmp(what, "flats", 5))
        g = mk(atoi(what + 5) % NMONK)->flats;
    else if (!strncmp(what, "clashes", 7))
        g = mk(atoi(what + 7) % NMONK)->clashes;
    else if (!strncmp(what, "sung", 4))
        g = mk(atoi(what + 4) % NMONK)->sung;
    else if (!strncmp(what, "par", 3))
        g = proj.par[atoi(what + 3)];
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
    if (!strcmp(cmd, "days")) {
        int n = atoi(a);
        while (n-- > 0)
            new_day();
        return 1;
    }
    if (!strcmp(cmd, "office")) {
        int o = atoi(a) % NOFFICE;
        proj.world.office = (uint8_t)((o + NOFFICE - 1) % NOFFICE);   /* (to Matins: a new day) */
        world_skip();
        return 1;
    }
    if (!strcmp(cmd, "storeamb")) {
        static uint8_t o[1014];
        memset(o, 0x11, sizeof o);
        o[0] = 0x45, o[1] = 0x4E, o[2] = 0x4F, o[3] = 0x31;   /* "ENO1" */
        o[4] = 5, o[5] = o[6] = o[7] = 0;                    /* format 5 */
        plat_store_save(OBJ_PROJ, o, sizeof o);
        return 1;
    }
    return 0;
}

void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }

void app_speak_dump(void) { speak_dump(); }
