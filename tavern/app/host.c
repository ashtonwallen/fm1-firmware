/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Tavern: ashtonwallen, 2026 */
/* TAVERN in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): the tavern's checks
 * for "expect", its commands, a reboot and the screen reader's dump.
 *
 * Checks: playing view sess (the engine: 0 no tune, 1 playing, 2 the last note) wstate (the world: 0 between
 * tunes, 1 a tune, 2 closed) night minute present playing_n (players in the tune now) live (the engine's
 * instruments, a bitmask) tunes (ever) done (the engine's, since the start) mem (memorable nights kept) leader
 * type kind dirty battery parN stateN thirstN ledN visitor (its state) nightlen root (the tune's key, 0 C .. 11 B).
 * 1.1: preset, format, effN (the engine's setting N as the preset makes it).
 * 1.2: amb (this preset's ambience, 0..25), ambN (preset N's); rounds (rounds stood this run); segues (tunes of a
 * set gone straight into the next), trades (parts traded), set (the tune's place in its set); the checks that
 * nobody plays badly: badev (events, this run, in words of 1.1's mishaps and quarrels: none), logbad (tonight's
 * log lines in those words), memx (memories of 1.1's mishaps kept), xfields (1.1's moods, skill, drink and
 * friendships left non-zero), notes, offkey (notes outside the tune's key), offchord (backing notes outside the
 * bar's chord): every note the band plays, as MIDI out sends it.
 * Commands: battery N; timewarp N (the tavern's clock N times as fast; the music as it is); minute N (the clock
 * set); storeamb (AMB-1 Drift's project in the store: another firmware's); storejunk (this firmware's magic and
 * format, the rest nonsense); endtune (the tune ends after this pass); hold N (the next gap between tunes N ms);
 * storev1 (the project now, saved as 1.0 saved it: format 1, no preset); storev2 (as 1.1 saved it: format 2, no
 * ambience, and 1.1's moods, a quarrel, a string being mended, a memory of it and a log line); worlddirty (the
 * world marked changed); notelog FILE | notelog off (what the band plays, a bar a line, into FILE). */

/* the words of 1.1's mishaps and quarrels: an event with any of them is a failure (badev) */
static const char *const BAD_WORDS[] = {"string", "Snap", "Twang", "won't play", "folds arms", "sits this one out",
                                        "storms", "had enough", "poured", "frozen", "Nobody shifts", "scowling",
                                        "thunder", "slumps", "both start", "stalks", "shake hands", "carries the tune",
                                        "drunk", "quarrel", "wrong", "flat", "late", "misses", "falls out"};
static uint32_t bad_events, notes_n, off_key, off_chord;
static int has_bad(const char *t)
{
    unsigned k;
    for (k = 0; k < sizeof BAD_WORDS / sizeof BAD_WORDS[0]; k++)
        if (strstr(t, BAD_WORDS[k]))
            return 1;
    return 0;
}
void host_event(const char *t)
{
    if (has_bad(t)) {
        bad_events++;
        printf("FAIL event in 1.1's words: %s\n", t);
    }
}

/* the note log: a bar a line, the chord, the melody, the plucks (harp, lute), the bass, the box */
static FILE *nl_f;
static int nl_pos = -1, nl_root, nl_got;
static uint16_t nl_mask;
static char nl_mel[200], nl_pl[200], nl_bs[48], nl_bx[48], nl_tune[48];
static void nl_add(char *b, int cap, int note)
{
    int n = (int)strlen(b);
    if (n + 6 < cap)
        snprintf(b + n, (size_t)(cap - n), "%s%s%d", n ? " " : "", EN_NOTE_NAME[note % 12], note / 12 - 1);
}
static void nl_flush(void)
{
    if (nl_f && nl_pos >= 0 && (nl_mel[0] || nl_pl[0] || nl_bs[0] || nl_bx[0])) {
        int pcs[4], n = 0, k;
        char ch[12];
        pcs[n++] = nl_root;                      /* the bar's chord, as it began */
        for (k = 1; k < 12; k++)
            if (nl_mask >> ((nl_root + k) % 12) & 1u && n < 4)
                pcs[n++] = (nl_root + k) % 12;
        en_chord_name(pcs, n, ch);
        fprintf(nl_f, "%d:%c%d.%d %-6s| %-44s| %-34s| %-12s| %s\n", nl_pos / 32 + 1, nl_pos & 16 ? 'B' : 'A',
                (nl_pos >> 3 & 1) + 1, (nl_pos & 7) + 1, ch, nl_mel, nl_pl, nl_bs, nl_bx);
    }
    nl_mel[0] = nl_pl[0] = nl_bs[0] = nl_bx[0] = 0;
}
void host_note(uint32_t st, uint32_t d1, uint32_t d2)
{
    int ch = (int)(st & 15u), pc = (int)(d1 % 12u);
    if ((st & 0xF0u) != 0x90u || !d2 || ch == 9)
        return;
    notes_n++;
    if (!(en_keymask >> pc & 1u)) {
        off_key++;
        if (off_key < 20)
            printf("FAIL note %u outside the key (mask %03x), channel %d\n", d1, en_keymask, ch);
    }
    if ((ch == 2 || ch == 3 || (ch == 1 && !en_harp)) && !(en_chordmask >> pc & 1u)) {
        off_chord++;
        if (off_chord < 20)
            printf("FAIL note %u outside the chord (mask %03x), channel %d\n", d1, en_chordmask, ch);
    }
    if (nl_f) {
        int pos = en_pass * 32 + en_part * 16 + en_rep * 8 + en_bar;
        if (pos != nl_pos || strcmp(nl_tune, world_tune())) {
            nl_flush();
            nl_pos = pos;
            nl_root = en_chord_root;
            nl_mask = en_chordmask;
            nl_got = 0;
            if (strcmp(nl_tune, world_tune())) {
                snprintf(nl_tune, sizeof nl_tune, "%s", world_tune());
                fprintf(nl_f, "\n== %s: %s, %s, %s %s, %d bpm%s%s\n", ch_name(world_leader() < 0 ? 0 : world_leader()),
                        nl_tune, EN_KIND_NAME[world_kind() & 3], EN_NOTE_NAME[world_root() % 12],
                        EN_MODE_NAME[world_mode() % EN_NMODES], ws.tempo, world_set() ? ", set " : "",
                        world_set() ? (world_set() == 1 ? "1" : world_set() == 2 ? "2" : "3") : "");
            }
        }
        if (ch != 0 && !nl_got) {               /* the chord as the backing first plays it */
            nl_got = 1;
            nl_root = en_chord_root;
            nl_mask = en_chordmask;
        }
        nl_add(ch == 0 ? nl_mel : ch == 1 ? nl_pl : ch == 2 ? nl_bs : nl_bx,
               ch == 0 || ch == 1 ? (int)sizeof nl_mel : (int)sizeof nl_bs, (int)d1);
    }
}

int app_expect(const char *what, int *got)
{
    const world_t *w = &proj.world;
    int g, i, j;
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
    else if (!strcmp(what, "rounds"))
        g = (int)ws.rounds;
    else if (!strcmp(what, "segues"))
        g = (int)en_segues;
    else if (!strcmp(what, "trades"))
        g = (int)ws.trades;
    else if (!strcmp(what, "set"))
        g = world_set();
    else if (!strcmp(what, "badev"))
        g = (int)bad_events;
    else if (!strcmp(what, "logbad")) {
        for (g = i = 0; i < NLOG; i++)
            g += has_bad(w->log[i]);
    } else if (!strcmp(what, "memx")) {
        for (g = i = 0; i < w->nmem && i < NMEM; i++)
            g += w->mem[i].type == M_X_STRING || w->mem[i].type == M_X_STANDOFF || w->mem[i].type == M_X_PEACE ||
                 w->mem[i].type == M_X_FROZEN;
    } else if (!strcmp(what, "xfields")) {
        for (g = i = 0; i < NCH; i++) {
            g += w->ch[i].x_mood != 0 || w->ch[i].x_skill != 0 || w->ch[i].x_drinks != 0 || w->ch[i].state == S_X_MEND;
            for (j = 0; j < NCH; j++)
                g += w->x_aff[i][j] != 0;
        }
    } else if (!strcmp(what, "notes"))
        g = (int)notes_n;
    else if (!strcmp(what, "offkey"))
        g = (int)off_key;
    else if (!strcmp(what, "offchord"))
        g = (int)off_chord;
    else if (!strcmp(what, "amb"))
        g = proj.amb[proj.preset % NPRESET];
    else if (!strncmp(what, "amb", 3))
        g = proj.amb[atoi(what + 3) % NPRESET];
    else if (!strncmp(what, "state", 5))
        g = w->ch[atoi(what + 5) % NCH].state;
    else if (!strncmp(what, "thirst", 6))
        g = w->ch[atoi(what + 6) % NCH].thirst;
    else if (!strncmp(what, "led", 3))
        g = w->ch[atoi(what + 3) % NCH].led;
    else if (!strncmp(what, "par", 3))
        g = proj.par[atoi(what + 3) % P_NSAVED];
    else if (!strncmp(what, "eff", 3)) {
        int p = atoi(what + 3) % P_NPARAMS;
        g = preset_par(p, p < P_NSAVED ? proj.par[p] : 0);
    } else if (!strcmp(what, "preset"))
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
    if (!strcmp(cmd, "worlddirty")) {            /* the world changed (to be kept at the hourly save) */
        ws.dirty = 1;
        return 1;
    }
    if (!strcmp(cmd, "notelog")) {               /* notelog FILE | notelog off */
        if (nl_f) {
            nl_flush();
            fclose(nl_f);
            nl_f = 0;
        }
        if (strcmp(a, "off")) {
            nl_f = fopen(a, "w");
            nl_pos = -1;
            nl_tune[0] = 0;
            if (!nl_f)
                printf("FAIL notelog: cannot write %s\n", a);
        }
        return 1;
    }
    if (!strcmp(cmd, "storev1")) {               /* 1.0's project: format 1, its spare byte (now the preset) 0 */
        static project_t o;
        o = proj;
        o.format = 1u;
        o.preset = 0;
        memset(o.amb, 0, sizeof o.amb);          /* (1.0's spare bytes) */
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "storev2")) {               /* 1.1's project: format 2, no ambience, and 1.1's mishaps */
        static project_t o;
        int i;
        o = proj;
        o.format = 2u;
        memset(o.amb, 0, sizeof o.amb);          /* (1.1's spare bytes) */
        o.world.ch[0].x_mood = -50;
        o.world.ch[0].x_skill = 88;
        o.world.ch[0].x_drinks = 3;
        o.world.ch[2].x_mood = 40;
        o.world.ch[2].x_skill = 76;
        o.world.ch[1].state = S_X_MEND;
        o.world.ch[1].busy = 2;
        o.world.x_aff[0][1] = o.world.x_aff[1][0] = -75;
        o.world.x_aff[5][7] = o.world.x_aff[7][5] = -45;
        for (i = NMEM - 1; i > 0; i--)
            o.world.mem[i] = o.world.mem[i - 1];
        o.world.mem[0].night = o.world.night;
        o.world.mem[0].type = M_X_STRING;
        o.world.mem[0].a = 0;
        if (o.world.nmem < NMEM)
            o.world.nmem++;
        snprintf(o.world.log[0], LOG_W, "Snap! Old Tom breaks a string.");
        snprintf(o.world.log[1], LOG_W, "Rosie won't play under Old Tom.");
        plat_store_save(OBJ_PROJ, &o, sizeof o);
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
