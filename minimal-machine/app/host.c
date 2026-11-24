/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (MINIMAL MACHINE) */
/* MINIMAL MACHINE in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): the checks for
 * "expect", the commands, a reboot and the screen reader's dump. (AMB-1's.)
 *
 * Checks: gen_same (after gen_mark) playing held eheld view preset dirty sixteenths muts mute chord parN lenN patN levelN gen_ok slotNpM
 * battery usb_full leds (fm1-base's own: store_writes peak_db_max peak_db_min ...).
 * Commands: gen_mark; battery N (plat_battery as N, -1..4; "battery off" the simulator's own); store_other (another firmware's
 * project in the same place: a different magic); autosave_skip M (the last save M minutes ago, for the autosave's
 * clocks). */

static int popc32(uint32_t m)
{
    int c = 0;
    while (m) {
        c += (int)(m & 1u);
        m >>= 1;
    }
    return c;
}

static uint32_t gen_sig(void)                   /* the loops as they are now, hashed */
{
    mm_gen_t g;
    const uint8_t *p = (const uint8_t *)&g;
    uint32_t h = 2166136261u, i;
    mm_gen_get(&g);
    for (i = 0; i < sizeof g; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
static uint32_t gen_marked;

int app_expect(const char *what, int *got)
{
    int g;
    if (!strcmp(what, "gen_same"))                /* the loops as they were at gen_mark */
        g = gen_sig() == gen_marked;
    else if (!strcmp(what, "playing"))
        g = mm_playing;
    else if (!strcmp(what, "held"))
        g = proj.held;
    else if (!strcmp(what, "eheld"))              /* the engine's own */
        g = mm_held;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "preset"))
        g = proj.preset;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strcmp(what, "dirty"))
        g = ui.dirty;
    else if (!strcmp(what, "sixteenths"))
        g = (int)mm_six;
    else if (!strcmp(what, "muts"))               /* mutations since the start */
        g = (int)mm_muts;
    else if (!strcmp(what, "mute"))
        g = proj.mute;
    else if (!strcmp(what, "chord"))
        g = mm_chord;
    else if (!strcmp(what, "gen_ok"))
        g = proj.gen_ok;
    else if (!strcmp(what, "usb_full"))
        g = proj.usb_full;
    else if (!strcmp(what, "leds"))
        g = proj.leds;
    else if (!strncmp(what, "len", 3))            /* lenN: part N's loop length */
        g = mm_len[atoi(what + 3) % MM_NT];
    else if (!strncmp(what, "hits", 4))           /* hitsN: part N's sounding steps */
        g = popc32(mm_pat[atoi(what + 4) % MM_NT]);
    else if (!strncmp(what, "level", 5))          /* levelN: part N's level now, % */
        g = (int)(mm_level[atoi(what + 5) % MM_NT] * 100.0f);
    else if (!strncmp(what, "slot", 4) && strchr(what, 'p')) {   /* slotNpM: preset N's kept parameter M */
        const char *q = strchr(what, 'p');
        g = proj.slot[atoi(what + 4) % NPRESETS].par[atoi(q + 1) % P_NPARAMS];
    } else if (!strncmp(what, "par", 3))          /* parN: parameter N (mm.h P_*) */
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
    if (!strcmp(cmd, "store_other")) {           /* another firmware's project where ours lives (AMB-1 Drift's magic) */
        static uint8_t o[2048];
        uint32_t m = 0x314F4E45u, f = 6u;
        memset(o, 0x5A, sizeof o);
        memcpy(o, &m, 4);
        memcpy(o + 4, &f, 4);
        plat_store_save(OBJ_PROJ, o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "store_garbage")) {         /* our magic and format, nonsense after it */
        static project_t o;
        memset(&o, 0x77, sizeof o);
        o.magic = PROJ_MAGIC;
        o.format = PROJ_FORMAT;
        plat_store_save(OBJ_PROJ, &o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "gen_mark")) {              /* gen_mark: remember the loops as they are (expect gen_same) */
        gen_marked = gen_sig();
        return 1;
    }
    if (!strcmp(cmd, "autosave_skip")) {         /* autosave_skip M: the last save M minutes ago */
        ui.saved_t -= (uint32_t)atoi(a) * 60000u;
        return 1;
    }
    return 0;
}

void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }

void app_speak_dump(void) { speak_dump(); }
