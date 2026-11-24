/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 ashtonwallen */
/* PATCH BAY's hands on the patch: the cursor and the cable it holds, and the auto-patcher, which moves one thing at
 * a time: a cable (its end flies to another jack, the sound changes as it lands), a module's knob (turned slowly) or
 * a sequencer step. Part of the unity build, after project.c (ui.c draws it all).
 *
 * The auto-patcher waits AUTO_S[proj.autor] seconds between moves (a little more or less each time), only while the
 * music plays, never while a cable is held, and for HAND_MS after the patch or a module's knob is touched by hand.
 * Each cable move is pb_auto_move's: the patch after it is always pb_musical. */

#include "fastmath.h"

static const uint16_t AUTO_S[NAUTO] = {0, 6, 10, 15, 20, 30, 45, 60, 90, 120, 180, 300};
#define MOVE_MS 1800u                    /* a cable's end in the air */
#define LAND_AT 1350u                    /* ... and plugged in (the sound changes) */
#define TWIST_MS 2600u                   /* a knob turned by the patcher */
#define SEQ_MS 1200u                     /* a step shown changing */
#define HAND_MS 90000u                   /* hands on the patch: the patcher waits this long after */

static void say(const char *a, const char *b);
static void warn(const char *a, const char *b);
static void mark_dirty(void);

enum { AU_CABLE, AU_TWIST, AU_SEQ };
static struct {
    uint32_t next_t;                     /* when the next move is due (0: from now on, one wait) */
    uint8_t busy, what, landed;          /* a move under way: AU_*; its cable has landed */
    pb_move_t mv, last;
    uint8_t have_last;
    uint32_t t0;
    uint8_t tmod, tfrom, tto;            /* a knob turned: its module, from, to */
    uint8_t sstep;                       /* a step changed */
    int8_t sfrom, sto;
    uint32_t seed;
    uint32_t hand_until;
    uint32_t moves;                      /* moves made since the start (tests) */
    uint8_t changed;                     /* the patcher changed the patch since the last save (kept hourly) */
    uint8_t ev_kind;                     /* the last move, for the footer: AU_* + 1, 0 none */
    pb_move_t ev_mv;
    uint8_t ev_mod, ev_val;
    uint32_t ev_t;
} au;

static struct {
    int8_t hold;                         /* the jack a held cable hangs from; -1 none */
    uint8_t jack;                        /* the cursor's jack within its module */
    uint32_t seen_until;                 /* the cursor is shown until */
} cur = {-1, 0, 0};

static int jack_of(int m, int k)         /* module m's k-th jack (wraps) */
{
    int j[4], n = pb_mod_jacks(m, j);
    return j[((k % n) + n) % n];
}
static int mod_njacks(int m)
{
    int j[4];
    return pb_mod_jacks(m, j);
}
static int cursor_jack(void) { return jack_of(proj.sel, cur.jack); }
static int jack_is_out(int j) { return j >= PB_NIN; }

static char *cat(char *b, const char *s)
{
    while ((*b = *s++))
        b++;
    return b;
}
static void jack_label(int j, char *b)   /* "SEQ CV", "LFO 1 SINE" */
{
    b = cat(b, PB_MOD_NAME[PB_JACK[j].mod]);
    *b++ = ' ';
    cat(b, PB_JACK[j].name);
}

static uint32_t au_rnd(void)
{
    uint32_t x = au.seed ? au.seed : 0x1234567u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    au.seed = x;
    return x;
}

static void au_schedule(uint32_t now)
{
    uint32_t s = AUTO_S[proj.autor] * 1000u;
    au.next_t = now + s - s / 5u + (s ? au_rnd() % (2u * s / 5u + 1u) : 0u);
    if (!au.next_t)
        au.next_t = 1;
}

static void au_connect_move(const pb_move_t *mv)
{
    pb_apply_move(proj.cab, mv);
    if (mv->in_old >= 0)
        pb_connect(mv->in_old, proj.cab[mv->in_old]);
    if (mv->in >= 0)
        pb_connect(mv->in, proj.cab[mv->in]);
}

static void au_land(void)                /* the cable plugs in: the sound changes now */
{
    if (au.landed)
        return;
    au.landed = 1;
    if (au.mv.kind != MV_DEL)            /* (a cable taken out left the patch at the start) */
        au_connect_move(&au.mv);
    au.changed = 1;
}

static void au_finish(void)              /* the move done (at once, when something else needs the patch) */
{
    if (!au.busy)
        return;
    if (au.what == AU_CABLE)
        au_land();
    else if (au.what == AU_TWIST && proj.par[au.tmod] != au.tto) {
        proj.par[au.tmod] = au.tto;
        pb_param(au.tmod, au.tto);
    }
    au.busy = 0;
}

static int au_start_cable(uint32_t now)
{
    pb_move_t mv;
    if (!pb_auto_move(proj.cab, proj.par, &au.seed, au.have_last ? &au.last : 0, &mv))
        return 0;
    au.mv = mv;
    au.last = mv;
    au.have_last = 1;
    au.what = AU_CABLE;
    au.landed = 0;
    if (mv.kind == MV_DEL) {             /* pulled out at once; it falls back to its output */
        au_connect_move(&mv);
        au.landed = 1;
        au.changed = 1;
    }
    au.ev_kind = AU_CABLE + 1;
    au.ev_mv = mv;
    au.ev_t = now;
    return 1;
}

static int au_start_twist(uint32_t now)
{
    int tries;
    for (tries = 0; tries < 20; tries++) {
        int m = (int)(au_rnd() % PB_NMOD), lo = PB_SAFE[m][0], hi = PB_SAFE[m][1], v = proj.par[m], d, t;
        uint8_t keep;
        d = 8 + (int)(au_rnd() % 20u);
        t = (au_rnd() & 1u) ? v + d : v - d;
        if (t > hi)
            t = v - d;
        if (t < lo)
            t = v + d;
        t = t < lo ? lo : t > hi ? hi : t;
        if (t == v)
            continue;
        keep = proj.par[m];
        proj.par[m] = (uint8_t)t;
        if (!pb_musical(proj.cab, proj.par)) {
            proj.par[m] = keep;
            continue;
        }
        proj.par[m] = keep;
        au.what = AU_TWIST;
        au.tmod = (uint8_t)m;
        au.tfrom = keep;
        au.tto = (uint8_t)t;
        au.ev_kind = AU_TWIST + 1;
        au.ev_mod = (uint8_t)m;
        au.ev_val = (uint8_t)t;
        au.ev_t = now;
        au.changed = 1;
        return 1;
    }
    return 0;
}

static void au_start_seq(uint32_t now)
{
    int s = (int)(au_rnd() % 8u), v = proj.seq[s], rests = 0, i, t;
    for (i = 0; i < 8; i++)
        rests += proj.seq[i] < 0;
    if (v < 0)
        t = (int)(au_rnd() % 11u);
    else if (rests < 2 && au_rnd() % 6u == 0u)
        t = -1;
    else {
        int d = 1 + (int)(au_rnd() % 3u);
        t = (au_rnd() & 1u) ? v + d : v - d;
        if (t < 0 || t > 14)
            t = v - (t - v);
        t = t < 0 ? 0 : t > 14 ? 14 : t;
    }
    au.what = AU_SEQ;
    au.sstep = (uint8_t)s;
    au.sfrom = (int8_t)v;
    au.sto = (int8_t)t;
    proj.seq[s] = (int8_t)t;
    pb_seq(s, t);
    au.ev_kind = AU_SEQ + 1;
    au.ev_mod = (uint8_t)s;
    au.ev_t = now;
    au.changed = 1;
}

static void au_start(uint32_t now)
{
    uint32_t r = au_rnd() % 100u;
    au.t0 = now;
    au.busy = 1;
    au.moves++;
    if (r < 68u && au_start_cable(now))
        return;
    if (r < 90u && au_start_twist(now))
        return;
    if (au_start_cable(now))
        return;
    au_start_seq(now);
}

/* every frame: a move under way goes on; one is started when due */
static void au_tick(uint32_t now, int playing)
{
    if (au.busy) {
        uint32_t el = now - au.t0;
        if (au.what == AU_CABLE) {
            if (el >= LAND_AT)
                au_land();
            if (el >= MOVE_MS)
                au.busy = 0;
        } else if (au.what == AU_TWIST) {
            int v = el >= TWIST_MS ? au.tto : au.tfrom + ((int)au.tto - (int)au.tfrom) * (int)el / (int)TWIST_MS;
            if (v != proj.par[au.tmod]) {
                proj.par[au.tmod] = (uint8_t)v;
                pb_param(au.tmod, v);
            }
            if (el >= TWIST_MS)
                au.busy = 0;
        } else if (el >= SEQ_MS) {
            au.busy = 0;
        }
        if (!au.busy)
            au_schedule(now);
        return;
    }
    if (!proj.autor || !playing || cur.hold >= 0 || (int32_t)(now - au.hand_until) < 0) {
        if (!proj.autor || cur.hold >= 0 || (int32_t)(now - au.hand_until) < 0)
            au.next_t = 0;
        return;
    }
    if (!au.next_t)
        au_schedule(now);
    if ((int32_t)(now - au.next_t) >= 0)
        au_start(now);
}

/* how far to the next move, 0..1 (the header's ring); -1: none coming */
static float au_progress(uint32_t now)
{
    uint32_t s = AUTO_S[proj.autor] * 1000u;
    if (au.busy)
        return 1.0f;
    if (!proj.autor || !au.next_t || !s)
        return -1.0f;
    if ((int32_t)(au.next_t - now) <= 0)
        return 1.0f;
    return fm_clampf(1.0f - (float)(au.next_t - now) / (float)s, 0.0f, 1.0f);
}

static void hands_on(void)               /* the player touched the patch: the patcher waits */
{
    au_finish();
    au.hand_until = plat_ms() + HAND_MS;
    au.next_t = 0;
}

/* ------------------------------------------------------------ by hand --- */
/* EDIT (or SEL) at the cursor's jack: nothing held: an output gives a new cable; an input with a cable pulls it out
 * (it stays on its output, held); an empty input gives a cable from there. Something held: a jack of the other end's
 * kind takes it (an input's old cable goes); the jack it hangs from drops it. */
static void bay_plug(void)
{
    int j = cursor_jack(), in, out;
    char t[24];
    hands_on();
    cur.seen_until = plat_ms() + 20000u;
    if (cur.hold < 0) {
        if (jack_is_out(j)) {
            cur.hold = (int8_t)j;
            jack_label(j, t);
            say("PICK AN INPUT", t);
        } else if (proj.cab[j] >= 0) {
            cur.hold = (int8_t)(PB_NIN + proj.cab[j]);
            proj.cab[j] = -1;
            pb_connect(j, -1);
            jack_label(j, t);
            say("PULLED OUT", t);
            mark_dirty();
        } else {
            cur.hold = (int8_t)j;
            jack_label(j, t);
            say("PICK AN OUTPUT", t);
        }
        return;
    }
    if (j == cur.hold) {
        cur.hold = -1;
        say("DROPPED", 0);
        return;
    }
    if (jack_is_out(j) == jack_is_out(cur.hold)) {
        warn(jack_is_out(j) ? "AN INPUT" : "AN OUTPUT", "TO PLUG IN");
        return;
    }
    in = jack_is_out(j) ? cur.hold : j;
    out = (jack_is_out(j) ? j : cur.hold) - PB_NIN;
    if (!pb_fits(in, out)) {
        warn(PB_JACK[in].type == T_AUDIO ? "AUDIO IN" : "NO AUDIO", PB_JACK[in].type == T_AUDIO ? "TAKES AUDIO" : "INTO CV");
        return;
    }
    {
        int8_t t2[PB_NIN];
        memcpy(t2, proj.cab, sizeof t2);
        t2[in] = (int8_t)out;
        if (pb_loops(t2)) {
            warn("NO LOOPS", "IN THE AUDIO");
            return;
        }
    }
    proj.cab[in] = (int8_t)out;
    pb_connect(in, out);
    cur.hold = -1;
    jack_label(in, t);
    say("PATCHED", t);
    mark_dirty();
}

static void bay_drop(void)               /* HOME: a held cable dropped */
{
    if (cur.hold >= 0) {
        cur.hold = -1;
        say("DROPPED", 0);
    }
}

static void bay_reset(void)              /* a new patch: nothing in the air */
{
    au.busy = 0;
    au.have_last = 0;
    au.next_t = 0;
    au.ev_kind = 0;
    au.hand_until = plat_ms();
    cur.hold = -1;
}
