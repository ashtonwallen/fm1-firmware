/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Changed for NIGHT DRIVE by ashtonwallen: one object, the presets' knobs, the setup and the odometer. */
/* NIGHT DRIVE project: one flash object (OBJ_PROJ). A project of another magic or format (AMB-1's, any other
 * firmware's), a damaged one or none gives the defaults. Part of the unity build, after app.h. */
project_t proj;

static void recipe(int i, slot_t *s)
{
    int16_t p[P_NPARAMS];
    int k;
    en_preset(i, p);
    for (k = 0; k < 4; k++)
        s->knob[k] = p[P_TEMPO + k];
    s->key = (uint8_t)p[P_ROOT];
    s->prog = (uint8_t)p[P_PROG];
}

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < NPRESETS; i++)
        recipe(i, &proj.slot[i]);
    proj.preset = 0;
    proj.leds = 1;
    proj.usb_full = 0;                           /* the USB audio follows MASTER */
    proj.drums = en_param_info(P_DRUMS)->def;
    proj.lead = en_param_info(P_LEAD)->def;
    proj.odo = 0;
}

typedef char proj_fits_the_store[sizeof(project_t) <= PLAT_STORE_MAX ? 1 : -1];   /* one flash object */

static int in_range(int p, int v) { return v >= en_param_info(p)->lo && v <= en_param_info(p)->hi; }

static int project_valid(void)
{
    int i, k;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT)
        return 0;
    if (proj.preset >= NPRESETS || proj.leds > 2 || proj.usb_full > 1)
        return 0;
    if (!in_range(P_DRUMS, proj.drums) || !in_range(P_LEAD, proj.lead))
        return 0;
    for (i = 0; i < NPRESETS; i++) {
        for (k = 0; k < 4; k++)
            if (!in_range(P_TEMPO + k, proj.slot[i].knob[k]))
                return 0;
        if (proj.slot[i].key > 11 || proj.slot[i].prog >= EN_NPROG)
            return 0;
    }
    return 1;
}

int project_load(void)
{
    int n;
    memset(&proj, 0, sizeof proj);
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (n == (int)sizeof proj && project_valid())
        return 0;
    project_defaults();
    return -1;
}

int project_save(void) { return plat_store_save(OBJ_PROJ, &proj, sizeof proj); }

void project_apply(void)
{
    const slot_t *s = &proj.slot[proj.preset];
    int k;
    en_set(P_SOUND, proj.preset);
    for (k = 0; k < 4; k++)
        en_set(P_TEMPO + k, s->knob[k]);
    en_set(P_ROOT, s->key);
    en_set(P_PROG, s->prog);
    en_set(P_DRUMS, proj.drums);
    en_set(P_LEAD, proj.lead);
    plat_usb_audio_full(proj.usb_full);
}

void project_preset_reset(void) { recipe(proj.preset, &proj.slot[proj.preset]); }
