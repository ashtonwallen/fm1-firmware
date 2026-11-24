/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* AMB-1 in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): AMB-1's checks for
 * "expect", its command, a reboot and the screen reader's dump.
 *
 * AMB-1's checks: playing frozen view piece pool dirty sixteenths ext pad_level bass_level chordroot chordN loopnotes
 * parN battery (fm1-base's own: store_writes clocks_out midi_out peak_db_max peak_db_min). Its commands: storev3 N
 * (the project as AMB-1 0.9 before the layer pages wrote it, format 3, the playing piece's Pad at N); storev4 N, storev5 N (AMB-1 1.x's, Drift 1.0's); battery N (the
 * battery read as N, plat.h's plat_battery: -1..4; "battery off" the simulator's own, full). */

int app_expect(const char *what, int *got)
{
    int g;
    if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "frozen"))
        g = en_frozen;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "piece"))
        g = proj.piece;
    else if (!strcmp(what, "pool"))               /* the scale, relative to the root (hex ok) */
        g = proj.pool;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strcmp(what, "dirty"))
        g = ui.dirty;
    else if (!strcmp(what, "sixteenths"))
        g = (int)en_bars;
    else if (!strcmp(what, "ext"))
        g = en_ext;
    else if (!strcmp(what, "pad_level"))
        g = (int)(en_pad_level * 100.0f);
    else if (!strcmp(what, "bass_level"))
        g = (int)(en_bass_level * 100.0f);
    else if (!strcmp(what, "chordroot"))
        g = en_chord_root;
    else if (!strncmp(what, "chord", 5))          /* chordN: pad voice N's note */
        g = en_chord[atoi(what + 5) & 3];
    else if (!strcmp(what, "loopnotes")) {        /* the notes in all the loops */
        int l, i;
        g = 0;
        for (l = 0; l < EN_NLOOP; l++)
            for (i = 0; i < EN_LOOP_MAX; i++)
                g += en_loop_note[l][i] >= 0;
    } else if (!strcmp(what, "wander"))           /* Wander: on, busy (a glide under way) */
        g = proj.wander;
    else if (!strcmp(what, "wbusy"))
        g = wander_busy;
    else if (!strncmp(what, "slotpad", 7))        /* slotpadN: piece N's kept Pad level */
        g = proj.slot[atoi(what + 7) % NPIECES].par[P_PAD];
    else if (!strcmp(what, "wevery"))             /* Wander's settings */
        g = proj.wevery;
    else if (!strcmp(what, "wglide"))
        g = proj.wglide;
    else if (!strcmp(what, "worder"))
        g = proj.worder;
    else if (!strncmp(what, "slot", 4) && strchr(what, 'p')) {   /* slotNpM: piece N's kept parameter M */
        const char *q = strchr(what, 'p');
        g = proj.slot[atoi(what + 4) % NPIECES].par[atoi(q + 1) % P_NPARAMS];
    }
    else if (!strncmp(what, "par", 3))          /* parN: parameter N (amb.h P_*) */
        g = proj.par[atoi(what + 3)];
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
    if (!strcmp(cmd, "storev3")) {
        static project_v3_t o;
        int i, k;
        memset(&o, 0, sizeof o);
        o.magic = PROJ_MAGIC;
        o.format = 3u;
        for (k = 0; k < V3_NPARAMS; k++)
            o.par[k] = proj.par[k];
        o.par[P_PAD] = (int16_t)atoi(a);
        o.pool = proj.pool;
        o.leds = proj.leds;
        o.piece = proj.piece;
        for (i = 0; i < (int)V4_NPIECES; i++) {
            for (k = 0; k < V3_NPARAMS; k++)
                o.slot[i].par[k] = proj.slot[i].par[k];
            o.slot[i].pool = proj.slot[i].pool;
        }
        o.slot[proj.piece].par[P_PAD] = o.par[P_PAD];
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "storev4")) {               /* storev4 N: AMB-1 1.x's project, the playing piece's Pad at N */
        static project_v4_t o;
        int i, k;
        memset(&o, 0, sizeof o);
        o.magic = PROJ_MAGIC;
        o.format = 4u;
        for (i = 0; i < V5_NPARAMS; i++)
            o.par[i] = proj.par[i];
        o.par[P_PAD] = (int16_t)atoi(a);
        o.pool = proj.pool;
        o.leds = proj.leds;
        o.piece = proj.piece < V4_NPIECES ? proj.piece : 0;
        for (i = 0; i < (int)V4_NPIECES; i++) {
            for (k = 0; k < V5_NPARAMS; k++)
                o.slot[i].par[k] = proj.slot[i].par[k];
            o.slot[i].pool = proj.slot[i].pool;
        }
        o.slot[o.piece].par[P_PAD] = o.par[P_PAD];
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "storev5")) {               /* storev5 N: Drift 1.0's project (format 5), the playing piece's
                                                  * Pad at N, Wander as it is now */
        static project_v5_t o;
        int i, k;
        memset(&o, 0, sizeof o);
        o.magic = PROJ_MAGIC;
        o.format = 5u;
        for (i = 0; i < V5_NPARAMS; i++)
            o.par[i] = proj.par[i];
        o.par[P_PAD] = (int16_t)atoi(a);
        o.pool = proj.pool;
        o.leds = proj.leds;
        o.piece = proj.piece < V5_NPIECES ? proj.piece : 0;
        for (i = 0; i < (int)V5_NPIECES; i++) {
            for (k = 0; k < V5_NPARAMS; k++)
                o.slot[i].par[k] = proj.slot[i].par[k];
            o.slot[i].pool = proj.slot[i].pool;
        }
        o.slot[o.piece].par[P_PAD] = o.par[P_PAD];
        o.wander = proj.wander;
        o.wevery = proj.wevery;
        o.worder = proj.worder;
        o.wglide = proj.wglide;
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "wander_skip")) {           /* wander_skip: Wander's clock to its next move (tests) */
        wd.acc = 0xFFFFFFF0u;
        return 1;
    }
    return 0;
}

void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }

void app_speak_dump(void) { speak_dump(); }
