/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 ashtonwallen (MINIMAL MACHINE) */
/* MINIMAL MACHINE's project: one flash object (OBJ_PROJ). A project of another magic or format, or none, gives the
 * defaults. Part of the unity build, after app.h. (AMB-1's, reshaped.)
 *
 * The presets (their recipes are the engine's, mm_preset_pars): each keeps its own knobs as you leave it. The loops
 * as they evolved are kept for the preset playing (gen); choosing another preset starts its loops fresh. */
project_t proj;

const char *preset_name(int i) { return mm_preset_name(i); }

static void recipe(int i, slot_t *s) { mm_preset_pars(i, s->par); }

static void take(const slot_t *s)                /* a preset's knobs into play */
{
    int p;
    for (p = 0; p < P_NPARAMS; p++)
        proj.par[p] = s->par[p];
}

static void keep(void)                           /* the preset playing, into its slot */
{
    int p;
    for (p = 0; p < P_NPARAMS; p++)
        proj.slot[proj.preset].par[p] = proj.par[p];
}

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < NPRESETS; i++)
        recipe(i, &proj.slot[i]);
    take(&proj.slot[0]);
    proj.leds = 1;
    proj.usb_full = 0;                           /* the USB recording follows MASTER */
    proj.gen_ok = 0;
}

typedef char proj_fits_the_store[sizeof(project_t) <= PLAT_STORE_MAX ? 1 : -1];   /* one flash object */

static int pars_ok(const int16_t *par)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++) {
        const mm_param_t *p = mm_param_info(i);
        if (par[i] < p->lo || par[i] > p->hi)
            return 0;
    }
    return 1;
}

static int project_valid(void)
{
    int i;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT || !pars_ok(proj.par))
        return 0;
    if (proj.preset >= NPRESETS || proj.leds > 2 || proj.usb_full > 1 || proj.held > 1 || proj.mute > 0x3Fu ||
        proj.gen_ok > 1)
        return 0;
    for (i = 0; i < NPRESETS; i++)
        if (!pars_ok(proj.slot[i].par))
            return 0;
    if (proj.gen_ok && !mm_gen_ok(&proj.gen))
        proj.gen_ok = 0;                         /* the loops start fresh; the rest stands */
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

int project_save(void)
{
    keep();
    mm_gen_get(&proj.gen);
    proj.gen_ok = 1;
    return plat_store_save(OBJ_PROJ, &proj, sizeof proj);
}

void project_preset(int i)
{
    keep();
    proj.preset = (uint8_t)((i % NPRESETS + NPRESETS) % NPRESETS);
    take(&proj.slot[proj.preset]);
    proj.gen_ok = 0;                             /* its loops from the start */
}

void project_preset_reset(void)
{
    slot_t s;
    recipe(proj.preset, &s);
    take(&s);
    proj.gen_ok = 0;
}

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        mm_set(i, proj.par[i]);
    mm_preset(proj.preset);
    if (proj.gen_ok)
        mm_gen_put(&proj.gen);                   /* the loops as they had evolved */
    mm_hold(proj.held);
    mm_mute(proj.mute);

    plat_usb_audio_full(proj.usb_full);

}
