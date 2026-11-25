/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (MONASTERY) */
/* MONASTERY in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): the checks for
 * "expect", the commands, a reboot and the screen reader's dump.
 *
 * Checks: playing frozen view dirty pad_level bass_level parN battery saves (saves by itself) office day phase
 * (0 bell, 1 chant, 2 quiet) singing (voices singing now) here (brothers in the community) choir (in the stalls this
 * office) events (since boot) chron (chronicle entries) partN hereN vowedN sungN solosN cantorN (brother N) amb (the
 * Ambience, this preset) help (a help card up: its button + 1); a musician's ear since boot: notes (notes sung),
 * offmode (ms of a note outside the mode), offtune (ms of a note more than 4 cents off), clash (ms of two voices a
 * second, a tritone or a seventh apart), gone (events of 1.1's mistake kinds); and over the chronicle, chrongone
 * (entries of those kinds).
 * Commands: battery N; days N (N new days at once, the world's daily turn: tests); office N (to office N, its bell);
 * notelog 1|0 (print every note sung); storeamb (a project as AMB-1 Drift saves it, magic "ENO1", in the store:
 * another firmware's); storev1, storev2 (this project as 1.0 / 1.1 saved it, with 1.1's brothers: the infirmary,
 * the rivalry, the histories of mistakes, the chronicle's mistakes) */

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
    } else if (!strcmp(what, "choir")) {
        for (g = 0, i = 0; i < NMONK; i++)
            g += in_choir(i);
    } else if (!strcmp(what, "events"))
        g = (int)wr.nevents;
    else if (!strcmp(what, "chron"))
        g = proj.world.nchron;
    else if (!strcmp(what, "chrongone")) {
        for (g = 0, i = 0; i < proj.world.nchron; i++)
            g += !EV_KEPT(chron_get(i)->type);
    } else if (!strcmp(what, "notes"))
        g = (int)wr.h_notes;
    else if (!strcmp(what, "offmode"))
        g = (int)wr.h_offmode;
    else if (!strcmp(what, "offtune"))
        g = (int)wr.h_offtune;
    else if (!strcmp(what, "clash"))
        g = (int)wr.h_clash;
    else if (!strcmp(what, "gone"))
        g = (int)wr.h_gone;
    else if (!strcmp(what, "amb"))
        g = proj.amb[proj.preset % NPRESET];
    else if (!strcmp(what, "help"))
        g = ui.help;
    else if (!strncmp(what, "part", 4))
        g = mk(atoi(what + 4) % NMONK)->part;
    else if (!strncmp(what, "here", 4))
        g = mk(atoi(what + 4) % NMONK)->here;
    else if (!strncmp(what, "vowed", 5))
        g = mk(atoi(what + 5) % NMONK)->vowed;
    else if (!strncmp(what, "solos", 5))
        g = mk(atoi(what + 5) % NMONK)->solos;
    else if (!strncmp(what, "cantor", 6))
        g = mk(atoi(what + 6) % NMONK)->cantor;
    else if (!strncmp(what, "sung", 4))
        g = mk(atoi(what + 4) % NMONK)->sung;
    else if (!strncmp(what, "par", 3))
        g = proj.par[atoi(what + 3) % P_NSAVED];
    else if (!strncmp(what, "eff", 3))           /* effN: the engine's setting N as the preset makes it */
        g = preset_par(atoi(what + 3) % P_NPARAMS, atoi(what + 3) % P_NPARAMS < P_NSAVED ? proj.par[atoi(what + 3) % P_NSAVED] : 0);
    else if (!strcmp(what, "preset"))
        g = proj.preset;
    else if (!strcmp(what, "format"))
        g = (int)proj.format;
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
    if (!strcmp(cmd, "notelog")) {
        wr.h_log = (uint8_t)atoi(a);
        return 1;
    }
    if (!strcmp(cmd, "storev1") || !strcmp(cmd, "storev2")) {   /* 1.0's (format 1) or 1.1's (format 2) project */
        static project_t o;
        uint8_t *m;
        int i;
        o = proj;
        o.format = cmd[6] == '1' ? 1u : 2u;
        if (o.format == 1u)
            o.preset = 0;                        /* (1.0's spare byte, now the preset: 0) */
        memset(o.amb, 0, sizeof o.amb);          /* (1.1's room to grow: 0) */
        memset(o.rsv, 0, sizeof o.rsv);
        o.world.rsv1[0] = 77;                    /* 1.1's rivalry and peace */
        o.world.rsv1[1] = 2;
        for (i = 0; i < NMONK; i++) {            /* 1.1's brothers: health, mood, days ill; their mistakes */
            m = (uint8_t *)&o.world.m[i];
            m[2] = 64, m[3] = 55, m[4] = 0;
            o.world.m[i].solos = (uint16_t)(5 + i);   /* (slips, naps) */
            o.world.m[i].cantor = (uint16_t)(3 + i);
            o.world.m[i].rsv2[0] = 9;            /* (flats, clashes) */
            o.world.m[i].rsv2[1] = 4;
        }
        m = (uint8_t *)&o.world.m[MK_ELDER];     /* the old brother in the infirmary, two days */
        m[0] = 2, m[4] = 2;
        o.world.m[MK_NOVICE].vowed = 0;          /* the novice's skill: 1.2's part */
        o.world.m[MK_NOVICE].part = 47;
        o.world.nchron = NCHRON;                 /* a full chronicle: mostly mistakes, a vow, an arrival */
        o.world.chron_w = 5;
        for (i = 0; i < NCHRON; i++) {
            chron_t *c = &o.world.chron[i];
            c->day = (uint16_t)(1 + i);
            c->office = (uint8_t)(i % NOFFICE);
            c->type = (uint8_t)(i == 3 ? EV_VOWS : i == 8 ? EV_ARRIVE : i == 10 ? EV_PRACTICE : 1 + i % 9);
            c->who = (uint8_t)(i % NMONK);
            c->who2 = (uint8_t)((i + 1) % NMONK);
        }
        plat_store_save(OBJ_PROJ, &o, sizeof o);
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
