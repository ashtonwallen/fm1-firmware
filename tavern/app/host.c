/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Tavern: ashtonwallen, 2026 */
/* TAVERN in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): the tavern's checks
 * for "expect", its commands, a reboot and the screen reader's dump.
 *
 * Checks: playing view sess (the engine: 0 no tune, 1 playing, 2 the last note) wstate (the world: 0 between
 * tunes, 1 a tune, 2 closed) night minute present playing_n (players in the tune now) live (the engine's
 * instruments, a bitmask) tunes (ever) done (the engine's, since the start) mem (memorable nights kept) leader
 * type kind dirty battery parN moodN stateN thirstN drinksN visitor (its state) aff_A_B nightlen.
 * Commands: battery N; timewarp N (the tavern's clock N times as fast; the music as it is); minute N (the clock
 * set); moodN V (a character's mood set); storeamb (AMB-1 Drift's project in the store: another firmware's);
 * storejunk (this firmware's magic and format, the rest nonsense); endtune (the tune ends after this pass); hold N
 * (the next gap between tunes N ms). Checks also: root (the tune's key, 0 C .. 11 B). */

int app_expect(const char *what, int *got)
{
    const world_t *w = &proj.world;
    int g, i;
    if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "sess"))
        g = en_sess;
    else if (!strcmp(what, "wstate"))
        g = world_state();
    else if (!strcmp(what, "night"))
        g = w->night;
    else if (!strcmp(what, "minute"))
        g = w->minute;
    else if (!strcmp(what, "present")) {
        for (g = i = 0; i < NCH; i++)
            g += present(i);
    } else if (!strcmp(what, "playing_n")) {
        for (g = i = 0; i < NCH; i++)
            g += w->ch[i].play != 0;
    } else if (!strcmp(what, "live"))
        g = en_live;
    else if (!strcmp(what, "tunes"))
        g = (int)w->tunes;
    else if (!strcmp(what, "done"))
        g = (int)en_tunes_done;
    else if (!strcmp(what, "mem"))
        g = w->nmem;
    else if (!strcmp(what, "leader"))
        g = world_leader();
    else if (!strcmp(what, "type"))
        g = world_type();
    else if (!strcmp(what, "root"))
        g = world_root();
    else if (!strcmp(what, "kind"))
        g = world_kind();
    else if (!strcmp(what, "dirty"))
        g = ui_dirty();
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strcmp(what, "visitor"))
        g = w->ch[VISITOR].state;
    else if (!strcmp(what, "nightlen"))
        g = proj.night_len;
    else if (!strncmp(what, "mood", 4))
        g = w->ch[atoi(what + 4) % NCH].mood;
    else if (!strncmp(what, "state", 5))
        g = w->ch[atoi(what + 5) % NCH].state;
    else if (!strncmp(what, "thirst", 6))
        g = w->ch[atoi(what + 6) % NCH].thirst;
    else if (!strncmp(what, "drinks", 6))
        g = w->ch[atoi(what + 6) % NCH].drinks;
    else if (!strncmp(what, "aff_", 4)) {        /* aff_A_B */
        const char *b = strchr(what + 4, '_');
        g = b ? w->aff[atoi(what + 4) % NCH][atoi(b + 1) % NCH] : 0;
    } else if (!strncmp(what, "par", 3))
        g = proj.par[atoi(what + 3) % P_NPARAMS];
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
    if (!strcmp(cmd, "timewarp")) {
        world_warp(atoi(a));
        return 1;
    }
    if (!strcmp(cmd, "endtune")) {               /* the tune ends after this pass */
        en_end();
        return 1;
    }
    if (!strcmp(cmd, "hold")) {                  /* hold N: the next gap between tunes N ms */
        world_host_hold(atoi(a));
        return 1;
    }
    if (!strcmp(cmd, "minute")) {
        proj.world.minute = (uint16_t)atoi(a);
        return 1;
    }
    if (!strncmp(cmd, "mood", 4)) {
        proj.world.ch[atoi(cmd + 4) % NCH].mood = (int8_t)atoi(a);
        return 1;
    }
    if (!strcmp(cmd, "storeamb")) {              /* AMB-1 Drift's project (magic "ENO1", format 5, its size) */
        static uint8_t o[1216];
        uint32_t m = 0x314F4E45u, f = 5u;
        int i;
        for (i = 0; i < (int)sizeof o; i++)
            o[i] = (uint8_t)(i * 37 + 11);
        memcpy(o, &m, 4);
        memcpy(o + 4, &f, 4);
        plat_store_save(OBJ_PROJ, o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "storejunk")) {             /* ours in name, nonsense inside */
        static project_t o;
        memset(&o, 0xA5, sizeof o);
        o.magic = PROJ_MAGIC;
        o.format = PROJ_FORMAT;
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        return 1;
    }
    return 0;
}

void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }

void app_speak_dump(void) { speak_dump(); }
