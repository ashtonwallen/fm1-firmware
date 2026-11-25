/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * JUKE JOINT: ashtonwallen, 2026 */
/* The band (included by juke.c): the forms, the piano's and the harp's licks, the arrangement of a tune and the
 * four players, a bar at a time into the schedule. Time: twelve ticks a beat, four beats a bar; in 12/8 and the
 * shuffle the eighths are triplets (ticks 0, 4, 8; the shuffle's off-beat at 8), in the minor blues they are even
 * (0, 6). FELT PIANO's 12/8 blues (its licks, their fitting, the turnaround and the pickup) is where this began. */

#define TPB 12
#define TBAR 48

/* ------------------------------------------------------------ forms --- */
/* a bar: one chord, or two (the second from its half); root 99: none */
typedef struct {
    ch_t c[12][2];
} form_t;
#define I7 {0, T_DOM7}
#define IV7 {5, T_DOM7}
#define V7 {7, T_DOM7}
#define NXC {99, 0}
static const form_t F_BLUES[] = {
    /* quick change: IV in bar two */
    {{{I7, NXC}, {IV7, NXC}, {I7, NXC}, {I7, NXC}, {IV7, NXC}, {IV7, NXC}, {I7, NXC}, {I7, NXC}, {V7, NXC},
      {IV7, NXC}, {I7, NXC}, {V7, NXC}}},
    /* four bars of I; the last bar I, then V */
    {{{I7, NXC}, {I7, NXC}, {I7, NXC}, {I7, NXC}, {IV7, NXC}, {IV7, NXC}, {I7, NXC}, {I7, NXC}, {V7, NXC},
      {IV7, NXC}, {I7, NXC}, {I7, V7}}},
    /* quick change, the turnaround I IV, I V */
    {{{I7, NXC}, {IV7, NXC}, {I7, NXC}, {I7, NXC}, {IV7, NXC}, {IV7, NXC}, {I7, NXC}, {I7, NXC}, {V7, NXC},
      {IV7, NXC}, {I7, IV7}, {I7, V7}}},
    /* the uptown slow blues: the diminished passing chord, VI7 to ii, a ii V turnaround */
    {{{I7, NXC}, {IV7, NXC}, {I7, NXC}, {I7, NXC}, {IV7, NXC}, {{6, T_DIM7}, NXC}, {I7, NXC}, {{9, T_DOM7}, NXC},
      {{2, T_M7}, NXC}, {V7, NXC}, {I7, {9, T_DOM7}}, {{2, T_M7}, V7}}},
};
#define i7 {0, T_M7}
#define iv7 {5, T_M7}
static const form_t F_MINOR[] = {
    /* i iv i i, iv iv i i, bVI7 V7 i V7 */
    {{{i7, NXC}, {iv7, NXC}, {i7, NXC}, {i7, NXC}, {iv7, NXC}, {iv7, NXC}, {i7, NXC}, {i7, NXC},
      {{8, T_DOM7}, NXC}, {{7, T_7B9}, NXC}, {i7, NXC}, {{7, T_7B9}, NXC}}},
    /* four bars of i; the turnaround through bVI7 */
    {{{i7, NXC}, {i7, NXC}, {i7, NXC}, {i7, NXC}, {iv7, NXC}, {iv7, NXC}, {i7, NXC}, {i7, NXC},
      {{8, T_DOM7}, NXC}, {{7, T_7B9}, NXC}, {i7, NXC}, {{8, T_DOM7}, {7, T_7B9}}}},
};
static const form_t F_BALLAD[] = {
    /* eight bars: I V IV IV, I V, I IV, I V */
    {{{I7, NXC}, {V7, NXC}, {IV7, NXC}, {IV7, NXC}, {I7, NXC}, {V7, NXC}, {I7, IV7}, {I7, V7}}},
    /* I I7 IV iv6, I V7, I IV, I V7 */
    {{{{0, T_6}, NXC}, {I7, NXC}, {{5, T_6}, NXC}, {{5, T_M6}, NXC}, {{0, T_6}, NXC}, {V7, NXC}, {{0, T_6}, IV7},
      {{0, T_6}, V7}}},
};

/* ----------------------------------------------------------- licks --- */
/* the piano's: (tick, semitones from the tonic, length, 1 a grace from below, 2 the fourth under it, 4 the sixth) */
#define LN 10
typedef struct {
    uint8_t n, at[LN];
    int8_t o[LN];
    uint8_t du[LN], fl[LN];
} lick_t;
static const lick_t LICK[] = {   /* 12/8 and the shuffle (triplet eighths) */
    {5, {0, 8, 12, 20, 24}, {4, 0, -2, -5, 0}, {8, 4, 8, 4, 20}, {1, 0, 0, 0, 0}},
    {6, {0, 8, 12, 16, 20, 24}, {12, 12, 10, 7, 3, 0}, {8, 4, 4, 4, 4, 20}, {2, 2, 0, 0, 0, 0}},
    {6, {0, 4, 8, 12, 16, 20}, {10, 7, 6, 5, 3, 0}, {4, 4, 4, 4, 4, 24}, {0}},
    {4, {0, 4, 8, 12}, {-5, -2, 0, 4}, {4, 4, 4, 22}, {0, 0, 0, 1}},
    {5, {0, 4, 8, 20, 24}, {5, 6, 7, 3, 0}, {4, 4, 12, 4, 20}, {0}},
    {2, {0, 24}, {4, 0}, {22, 20}, {1, 0}},
    {7, {0, 4, 8, 12, 20, 24, 32}, {0, -2, 0, 3, 0, -2, -5}, {4, 4, 4, 8, 4, 8, 14}, {0}},
    {3, {0, 12, 24}, {9, 7, 4}, {12, 12, 20}, {4, 4, 5}},
    {5, {12, 16, 20, 24, 32}, {7, 9, 7, 4, 0}, {4, 4, 4, 8, 14}, {0, 0, 0, 1, 0}},
    {5, {0, 8, 12, 16, 24}, {16, 12, 10, 12, 7}, {8, 4, 4, 8, 20}, {1, 0, 0, 0, 0}},
    {8, {0, 4, 8, 12, 16, 20, 24, 32}, {15, 12, 10, 7, 6, 5, 3, 0}, {4, 4, 4, 4, 4, 4, 8, 14}, {0}},
    {5, {0, 8, 12, 20, 24}, {7, 7, 10, 7, 4}, {8, 4, 8, 4, 20}, {2, 2, 0, 0, 1}},
    {5, {0, 4, 8, 12, 24}, {0, 3, 4, 7, 10}, {4, 4, 4, 12, 20}, {0, 0, 1, 0, 0}},
    {4, {0, 8, 12, 24}, {3, 0, -2, 0}, {8, 4, 12, 20}, {0}},
};
static const uint8_t LK_CALL[] = {0, 1, 3, 4, 5, 7, 8, 9, 11, 12}, LK_HOME[] = {2, 6, 0, 7, 13, 10};
static const uint8_t LK_TAIL[] = {5, 3, 13};
static const lick_t MLICK[] = {   /* the minor blues (even eighths) */
    {4, {0, 6, 12, 24}, {7, 10, 12, 10}, {6, 6, 12, 20}, {0}},
    {5, {0, 6, 12, 18, 24}, {12, 10, 7, 5, 3}, {6, 6, 6, 6, 20}, {0}},
    {5, {6, 12, 18, 24, 36}, {3, 5, 7, 8, 7}, {6, 6, 6, 12, 10}, {0}},
    {4, {0, 12, 18, 24}, {0, -2, 0, 3}, {12, 6, 6, 22}, {0}},
    {5, {0, 3, 6, 12, 24}, {15, 14, 12, 10, 7}, {3, 3, 6, 12, 22}, {0}},
    {6, {0, 6, 12, 24, 30, 36}, {7, 8, 7, 5, 3, 0}, {6, 6, 12, 6, 6, 10}, {0}},
    {4, {0, 6, 12, 24}, {5, 3, 2, 0}, {6, 6, 12, 24}, {0}},
    {5, {12, 18, 24, 30, 36}, {-5, -2, 0, 3, 7}, {6, 6, 6, 6, 10}, {0}},
};
static const uint8_t MK_CALL[] = {0, 1, 2, 4, 5, 7}, MK_HOME[] = {3, 6, 1}, MK_TAIL[] = {3, 6};

/* the harp's: (tick (up to two bars), semitones from its tonic, length, the bend up into it, flags). Cross harp
 * (second position): its notes are the holes' and their draw bends, so the licks keep to them (H2_OK) */
#define HN 8
typedef struct {
    uint8_t n, at[HN];
    int8_t o[HN];
    uint8_t du[HN], bd[HN], fl[HN];
} hlick_t;
#define SH HF_SHAKE
#define CHD HF_CHORD
#define FAL HF_FALL
static const hlick_t HLICK[] = {   /* 12/8 and the shuffle */
    {3, {0, 24, 36}, {0, 4, 0}, {22, 10, 10}, {2, 1, 0}, {0}},                                     /* 0 the wail */
    {4, {0, 24, 28, 32}, {7, 5, 4, 0}, {22, 4, 4, 14}, {0, 0, 1, 0}, {SH, 0, 0, 0}},             /* 1 a shake */
    {5, {0, 12, 16, 20, 24}, {12, 10, 7, 10, 12}, {10, 4, 4, 4, 22}, {0}, {0, 0, 0, 0, FAL}},     /* 2 up high */
    {6, {0, 8, 12, 20, 24, 36}, {0, 0, 0, 0, 4, 0}, {3, 3, 3, 3, 8, 10}, {0, 0, 0, 0, 1, 0}, {CHD, CHD, CHD, CHD, 0, 0}},
    {7, {0, 4, 8, 24, 48, 60, 72}, {-5, -3, 0, 3, 4, 3, 0}, {4, 4, 14, 20, 10, 10, 20}, {0, 0, 2, 0, 1, 0, 0}, {0}},
    {7, {0, 8, 12, 24, 32, 36, 48}, {4, 5, 7, 10, 7, 5, 4}, {8, 4, 12, 8, 4, 10, 30}, {1, 0, 0, 0, 0, 0, 1}, {0}},
    /* answers (6..) */
    {5, {0, 4, 8, 12, 24}, {7, 6, 5, 4, 0}, {4, 4, 4, 10, 22}, {0, 0, 0, 1, 2}, {0}},
    {5, {0, 12, 16, 24, 48}, {12, 10, 7, 4, 0}, {10, 4, 4, 20, 30}, {0, 0, 0, 1, 2}, {0}},
    {3, {12, 20, 24}, {4, 3, 0}, {8, 4, 22}, {1, 0, 2}, {0}},
    {3, {0, 8, 24}, {0, 0, 0}, {6, 12, 22}, {0}, {CHD, CHD, 0}},
    {4, {0, 4, 8, 24}, {-5, -3, -5, 0}, {4, 4, 12, 22}, {0, 0, 0, 2}, {0}},
    {1, {0}, {4}, {40}, {1}, {SH}},
};
static const uint8_t HK_CALL[] = {0, 1, 2, 3, 4, 5}, HK_ANS[] = {6, 7, 8, 9, 10, 11}, HK_ANS1[] = {6, 8, 9, 10};
static const hlick_t MHLICK[] = {  /* the minor blues: third position */
    {4, {0, 12, 18, 24}, {0, 3, 5, 7}, {10, 6, 6, 22}, {0}, {0}},
    {5, {0, 6, 12, 24, 36}, {12, 10, 7, 5, 3}, {6, 6, 10, 10, 10}, {0}, {0}},
    {4, {0, 6, 12, 24}, {7, 5, 3, 0}, {6, 6, 10, 22}, {0}, {0}},
    {3, {0, 24, 36}, {10, 7, 3}, {22, 10, 10}, {0}, {SH, 0, 0}},
    {2, {0, 12}, {3, 0}, {10, 34}, {1, 0}, {0}},
    {5, {0, 6, 12, 30, 48}, {-5, -2, 0, 3, 0}, {6, 6, 18, 18, 30}, {0, 0, 0, 1, 0}, {0}},
};
static const uint8_t MHK_CALL[] = {0, 1, 3, 5}, MHK_ANS[] = {2, 4, 1}, MHK_ANS1[] = {2, 4};
/* the holes: second position (from the tonic: 1 blow .. 9 blow, with the draw bends), third (minor) */
static const int8_t H2_OK[] = {-7, -6, -5, -3, -2, -1, 0, 2, 3, 4, 5, 6, 7, 9, 10, 12, 13, 14, 16, 17, 19, 21, 22, 24};
static const int8_t H3_OK[] = {-12, -10, -7, -5, -4, -3, -2, 0, 2, 3, 5, 7, 9, 10, 12, 14, 15, 17, 19};

/* ------------------------------------------------------------ the tune --- */
enum { IN_COUNT, IN_V, IN_PIANO };
enum { END_B2, END_6, END_MIN };
typedef struct {
    int16_t lh[4];
    int n;
} vmem_t;
static struct {
    int kind, fl, form, even;                    /* even: the minor's straight eighths */
    const form_t *F;
    float spt;
    int T, H;                                    /* the piano's tonic (right hand), the harp's */
    int bar, nbars, intro, ncount, introtype, nch, role[16], stopc, nsolo, first_solo;
    int harp, hbg, hans[3];                      /* the harp in tonight; behind the piano; answering the head */
    int la, lb, ha, hb, sa, sb, ending;          /* the head's licks; a solo chorus's */
    int bass_prev, bass_up, last_lick, fill_prev, ped;
    vmem_t rh, lh;
} pc;
static int cur_bar, last_kind = -1;

/* the bar's time: samples from its start to a tick, the bar slowing (rit) */
static float bar_spt, bar_rit;
static int32_t at_tick(float t) { return (int32_t)((t + bar_rit * t * t / (2.0f * (float)TBAR)) * bar_spt); }
static int32_t dur(float from, float len) { return at_tick(from + len) - at_tick(from); }
static int32_t hum(float ms) { return (int32_t)((frand() * 2.0f - 1.0f) * ms * 44.1f); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int iabs(int v) { return v < 0 ? -v : v; }
static int in_rng(int pcl, int lo) { return lo + ((pcl - lo) % 12 + 12) % 12; }   /* a pitch class in lo .. lo+11 */

/* loudness: x 0..1 the music's level here; Drive widens it */
static float loud(float x)
{
    float d = pf(P_DRIVE), lo = 0.2f + 0.06f * (1.0f - d), hi = lo + 0.18f + 0.42f * d;
    return fm_clampf(lo + (hi - lo) * x, 0.05f, 1.0f) * (0.94f + 0.12f * frand());
}

static uint16_t ch_pcs(ch_t c)
{
    uint16_t m = 0;
    int i;
    for (i = 0; i < CT_N[c.t]; i++)
        m |= (uint16_t)(1u << ((en_key + c.root + CT_IV[c.t][i]) % 12));
    return m;
}
static int ch_rootpc(ch_t c) { return (en_key + c.root) % 12; }
static ch_t form_ch(int b12, int tick)           /* the form's chord at a bar and tick (on into the next bars) */
{
    const ch_t *c;
    b12 += tick / TBAR;
    tick %= TBAR;
    c = pc.F->c[((b12 % pc.fl) + pc.fl) % pc.fl];
    return (tick >= 24 && c[1].root != 99) ? c[1] : c[0];
}

/* ------------------------------------------------------- the schedule --- */
static void dbg(float tick, int note, float vel, int role, float len)
{
#ifdef EN_HOST
    if (en_dbg_note)
        en_dbg_note(cur_bar, (int)tick, note, vel, role, (int)len);
#else
    (void)tick, (void)note, (void)vel, (void)role, (void)len;
#endif
}
static void pno(float tick, const int *n, int k, float vel, float len, int role, int32_t extra)
{
    ev_t *e = ev_new(E_PIANO, at_tick(tick) + extra);
    int i, g = dur(tick, len);
    for (i = 0; i < k; i++)
        dbg(tick, n[i], vel, role, len);
    if (!e)
        return;
    e->n = (uint8_t)(k > 3 ? 3 : k);
    for (i = 0; i < e->n; i++)
        e->note[i] = (uint8_t)clampi(n[i], 24, 100);
    e->vel = vel;
    e->gate = g < 400 ? 400 : g;
}
static void pn1(float tick, int n, float vel, float len, int role, int32_t extra) { pno(tick, &n, 1, vel, len, role, extra); }
static void bas(float tick, int n, float vel, float len)
{
    ev_t *e = ev_new(E_BASS, at_tick(tick) + hum(4.0f) + (int32_t)(0.004f * EN_SR));
    dbg(tick, n, vel, R_BASS, len);
    if (!e)
        return;
    e->note[0] = (uint8_t)n;
    e->vel = vel;
    e->gate = dur(tick, len);
    pc.bass_prev = n;
}
static void hrp(float tick, int n, float vel, float len, int bend, int fl, int c1, int c2, int32_t extra)
{
    ev_t *e = ev_new(E_HARP, at_tick(tick) + extra);
    dbg(tick, n, vel, R_HARP, len);
    if (!e)
        return;
    e->note[0] = (uint8_t)n;
    e->note[1] = (uint8_t)(c1 ? 64 + c1 : 0);
    e->note[2] = (uint8_t)(c2 ? 64 + c2 : 0);
    e->a = (int8_t)bend;
    e->fl = (uint8_t)fl;
    e->vel = vel;
    e->gate = dur(tick, len);
}
enum { DR_KICK = 36, DR_TAP = 37, DR_SLAP = 38, DR_HAT = 44, DR_STICK = 75 };
static void drum(float tick, int what, float vel)
{
    static const uint8_t TY[] = {E_KICK, E_SLAP, E_SLAP, E_HAT, E_STICK};
    int k = what == DR_KICK ? 0 : what == DR_TAP ? 1 : what == DR_SLAP ? 2 : what == DR_HAT ? 3 : 4;
    ev_t *e = ev_new(TY[k], at_tick(tick) + hum(2.5f));
    dbg(tick, what, vel, R_DRUM, 1.0f);
    if (!e)
        return;
    e->vel = fm_minf(vel, 1.0f);
    e->a = (int8_t)(what == DR_TAP);
}
static void ev_at(float tick, int type, float vel, int a)
{
    ev_t *e = ev_new(type, at_tick(tick));
    if (e) {
        e->vel = vel;
        e->a = (int8_t)a;
    }
}
static void chord_ev(float tick, ch_t c)
{
    ev_t *e;
#ifdef EN_HOST
    if (en_dbg_chord)
        en_dbg_chord(cur_bar, (int)tick, ch_rootpc(c), ch_pcs(c));
#endif
    e = ev_new(E_CHORD, at_tick(tick));
    if (e) {
        e->pcs = ch_pcs(c);
        e->note[0] = (uint8_t)ch_rootpc(c);
        e->note[1] = c.t;
    }
}
static void mark(int sec, int b12, int chorus)
{
    ev_t *e = ev_new(E_MARK, 0);
#ifdef EN_HOST
    if (en_dbg_mark)
        en_dbg_mark(cur_bar, sec, chorus, b12);
#endif
    if (e) {
        e->note[0] = (uint8_t)sec;
        e->note[1] = (uint8_t)b12;
        e->note[2] = (uint8_t)chorus;
    }
}

/* ----------------------------------------------------------- voicing --- */
/* these pitch classes as a close chord in lo..hi, moving least from the last (FELT PIANO's) */
static void voice_lead(vmem_t *mem, const int *pcs, int n, int lo, int hi, int *out)
{
    int r, j, best = 1 << 30, prevm = 0, tmp[4];
    for (j = 0; j < mem->n; j++)
        prevm += mem->lh[j];
    prevm = mem->n ? prevm / mem->n : (lo + hi) / 2;
    for (r = 0; r < n; r++) {
        int base = lo + ((pcs[r] - lo) % 12 + 12) % 12, oct;
        for (oct = 0; oct < 2; oct++) {
            int d = 0, m = 0;
            tmp[0] = base + 12 * oct;
            for (j = 1; j < n; j++) {
                int x = tmp[j - 1] + ((pcs[(r + j) % n] - tmp[j - 1]) % 12 + 12) % 12;
                if (x == tmp[j - 1])
                    x += 12;
                tmp[j] = x;
            }
            for (j = 0; j < n; j++) {
                int k, e = 99;
                m += tmp[j];
                for (k = 0; k < mem->n; k++) {
                    int dd = iabs(tmp[j] - mem->lh[k]);
                    e = dd < e ? dd : e;
                }
                d += mem->n ? e : 0;
            }
            m /= n;
            d += 2 * iabs(m - prevm) / 3;
            if (tmp[n - 1] > hi)
                d += 30 + 4 * (tmp[n - 1] - hi);
            if (tmp[0] < lo)
                d += 30 + 4 * (lo - tmp[0]);
            for (j = 1; j < n; j++)
                if (tmp[j] - tmp[j - 1] == 1)
                    d += 12;
            if (d < best) {
                best = d;
                for (j = 0; j < n; j++)
                    out[j] = tmp[j];
            }
        }
    }
    for (j = 0; j < n; j++)
        mem->lh[j] = (int16_t)out[j];
    mem->n = n;
}
/* the comping chord: third, seventh and ninth (the sixth's, the diminished's own) */
static int comp_voicing(ch_t c, int lo, int hi, int *out)
{
    static const int8_t V[T_N][3] = {{4, 10, 14}, {4, 10, 14}, {3, 10, 14}, {3, 10, 14}, {3, 6, 9}, {4, 9, 14},
                                     {3, 9, 14}, {4, 10, 13}, {4, 7, 12}, {3, 7, 12}, {3, 6, 10}};
    int pcs[3], i;
    for (i = 0; i < 3; i++)
        pcs[i] = (en_key + c.root + V[c.t][i]) % 12;
    voice_lead(&pc.rh, pcs, 3, lo, hi, out);
    return 3;
}
static int shell(ch_t c, int *out)               /* the left hand's third and seventh (or sixth) */
{
    int pcs[2], sev = c.t == T_6 || c.t == T_M6 || c.t == T_DIM7 ? CT_IV[c.t][3] : CT_N[c.t] >= 4 ? CT_IV[c.t][3] : 10;
    pcs[0] = (en_key + c.root + CT_IV[c.t][1]) % 12;
    pcs[1] = (en_key + c.root + sev) % 12;
    voice_lead(&pc.lh, pcs, 2, 50, 62, out);
    return 2;
}

/* a lick's note fitted to the chord under it (FELT PIANO's): chord notes, the ninth and thirteenth, the sharp nine
 * on a dominant (the blue third); short ones may pass on the fourth or the flat five; else the chord note nearest */
static int bl_fit(int n, ch_t c, int shortn)
{
    uint16_t m = ch_pcs(c);
    int rel = ((n - ch_rootpc(c)) % 12 + 12) % 12, dom = c.t == T_DOM7 || c.t == T_DOM9 || c.t == T_6;
    if (m >> (n % 12) & 1u)
        return n;
    if (rel == 2 || (rel == 9 && c.t != T_7B9))
        return n;
    if (dom && rel == 3)
        return n;
    if ((c.t == T_M7 || c.t == T_M9) && rel == 5)
        return n;                                /* the minor's eleventh */
    if (shortn && c.t != T_DIM7 && (rel == 5 || rel == 6 || rel == 8))
        return n;
    if (m >> ((n + 11) % 12) & 1u)
        return n - 1;
    if (m >> ((n + 10) % 12) & 1u)
        return n - 2;
    if (m >> ((n + 1) % 12) & 1u)
        return n + 1;
    return n + 2;
}
static int h_ok(int rel)                         /* a note the harp has (from its tonic) */
{
    const int8_t *L = pc.kind == K_MINOR ? H3_OK : H2_OK;
    int i, n = pc.kind == K_MINOR ? NA(H3_OK) : NA(H2_OK);
    for (i = 0; i < n; i++)
        if (L[i] == rel)
            return 1;
    return 0;
}
static int h_fit(int rel, ch_t c, int shortn)    /* the harp's note fitted: on its holes, then to the chord */
{
    static const int8_t D[7] = {0, -1, 1, -2, 2, -3, 3};
    int i;
    for (i = 0; i < 7; i++) {
        int r = rel + D[i], n = pc.H + r;
        if (h_ok(r) && bl_fit(n, c, shortn) == n)
            return r;
    }
    return rel;
}

static int bar_b12;                              /* the bar playing (for the licks' chords) */
static int ch_two;
static ch_t bar_c[2];
static ch_t bar_ch(int tick) { return (ch_two && tick >= 24) ? bar_c[1] : bar_c[0]; }

static void p_lick(const lick_t *L, float t0, float x, int oct)
{
    int i;
    for (i = 0; i < L->n && oct > 0; i++)        /* the octave up only while it stays under D6 */
        if (pc.T + oct + L->o[i] > 86)
            oct -= 12;
    for (i = 0; i < L->n; i++) {
        float t = t0 + (float)L->at[i];
        ch_t c = bar_ch((int)t);
        int raw = pc.T + oct + L->o[i], n = bl_fit(raw, c, L->du[i] <= 8), fl = L->fl[i];
        float v = loud(x) * (i == 0 ? 0.95f : L->du[i] >= 12 ? 0.9f : 0.78f);
        int32_t h = (int32_t)(0.008f * EN_SR) + hum(9.0f);
        if ((fl & 1) && n == raw && (ch_pcs(c) >> ((n + 11) % 12) & 1u) == 0)
            pn1(t - 0.7f, n - 1, v * 0.7f, 0.8f, R_MEL, h);   /* a grace from below: the blue third, bent up */
        if (fl & 6) {
            int two[2];
            two[0] = bl_fit(n - ((fl & 2) ? 5 : 9), c, 0);
            two[1] = n;
            pno(t, two, 2, v, (float)L->du[i] * 0.95f, R_MEL, h);
        } else {
            pn1(t, n, v, (float)L->du[i] * 0.95f, R_MEL, h);
        }
    }
}
static void h_lick(const hlick_t *L, float t0, float x, int b12)
{
    int i;
    for (i = 0; i < L->n; i++) {
        int at = (int)t0 + L->at[i], fl = L->fl[i], bd = L->bd[i], r;
        ch_t c = form_ch(b12, at);
        int32_t h = (int32_t)(0.014f * EN_SR) + hum(12.0f);   /* the harp a hair behind */
        float v = loud(x) * (L->du[i] >= 12 ? 0.95f : 0.82f) * 1.05f;
        if (fl & HF_CHORD) {                     /* the draw chord on I, the blow chord on IV; else one reed */
            int cr = c.root;
            if (cr == 0 && pc.kind != K_MINOR)
                hrp((float)at, pc.H, v * 0.85f, (float)L->du[i], 0, HF_CHORD, 4, 7, h);
            else if (cr == 5 && pc.kind != K_MINOR)
                hrp((float)at, pc.H + 5, v * 0.85f, (float)L->du[i], 0, HF_CHORD, 4, 7, h);
            else
                hrp((float)at, pc.H + h_fit(L->o[i], c, 1), v * 0.85f, (float)L->du[i], 0, 0, 0, 0, h);
            continue;
        }
        r = h_fit(L->o[i], c, L->du[i] <= 8);
        if (r != L->o[i] && !(r == 0 || r == 4 || r == 3 || r == 7 || r == 14 || r == -5 || r == 12))
            bd = 0;                              /* moved off a draw hole: no bend */
        if ((fl & HF_SHAKE) && !h_ok(r + 3))
            fl &= ~HF_SHAKE;
        hrp((float)at, pc.H + r, v, (float)L->du[i] * 0.97f, bd, fl, (fl & HF_SHAKE) ? 3 : 0, 0, h);
    }
}

/* a held chord of the piano's own (both hands), rolled a little */
static void p_chord(float t, ch_t c, float v, float len, int low)
{
    int a[3], s[2];
    comp_voicing(c, 60, 75, a);
    pno(t + 0.6f, a, 3, v, len, R_COMP, hum(3.0f) + (int32_t)(0.012f * EN_SR));
    shell(c, s);
    pno(t, s, 2, v * 0.9f, len, R_LH, hum(3.0f));
    if (low)
        pn1(t - 0.4f, in_rng(ch_rootpc(c), 36), v * 0.8f, len, R_LH, 0);
}

/* ------------------------------------------------------------ players --- */
typedef struct {
    int b12, line, lb, sec, chorus, last, stop, pre, fin, band, two, nlines;
    ch_t c0, c1, nx;
    float x;
} bctx_t;

static void drums_bar(const bctx_t *B)
{
    float dv = (0.6f + 0.5f * pf(P_DRIVE)) * (0.7f + 0.5f * B->x), sweep;
    int i, fill;
    if (!B->band)
        return;
    if (B->fin) {                                /* the last chord: a slap and the kick, the brushes circling */
        drum(0.0f, DR_SLAP, 0.7f * dv);
        drum(0.0f, DR_KICK, 0.6f * dv);
        ev_at(0.0f, E_SWEEP, 0.34f * dv, 0);
        ev_at(40.0f, E_SWEEP, 0.0f, 0);
        return;
    }
    if (B->pre) {                                /* the ending's hits */
        float h1 = pc.ending == END_B2 ? 36.0f : 24.0f;
        drum(0.0f, DR_SLAP, 0.5f * dv);
        drum(0.0f, DR_KICK, 0.35f * dv);
        drum(12.0f, DR_HAT, 0.3f);
        drum(h1, DR_SLAP, 0.62f * dv);
        drum(h1, DR_KICK, 0.5f * dv);
        if (pc.ending != END_B2) {
            drum(36.0f, DR_SLAP, 0.66f * dv);
            drum(36.0f, DR_KICK, 0.5f * dv);
        }
        ev_at(0.0f, E_SWEEP, 0.0f, 0);
        return;
    }
    if (B->stop) {                               /* stop time: the hit on one; the foot keeps two and four */
        drum(0.0f, DR_SLAP, 0.66f * dv);
        drum(0.0f, DR_KICK, 0.55f * dv);
        drum(12.0f, DR_HAT, 0.3f);
        drum(36.0f, DR_HAT, 0.3f);
        ev_at(0.0f, E_SWEEP, 0.0f, 0);
        return;
    }
    if (pc.fill_prev && B->b12 == 0) {           /* out of a fill: the top of the chorus marked */
        drum(0.0f, DR_SLAP, 0.6f * dv);
        drum(0.0f, DR_KICK, 0.42f * dv);
    }
    fill = 0;
    if (B->b12 == pc.fl - 1 || (B->chorus >= 0 && B->sec != S_INTRO && B->b12 + 1 < pc.fl && 0)) {
        fill = trand() < 0.45f + 0.45f * pf(P_DRIVE);
    } else if (B->lb == 3 && trand() < 0.12f + 0.3f * pf(P_DRIVE)) {
        fill = 1;
    }
    if (B->sec == S_INTRO && B->b12 == pc.fl - 1)
        fill = 1;
    pc.fill_prev = fill;
    switch (pc.kind) {
    case K_SLOW: sweep = 0.22f; break;
    case K_SHUF: sweep = 0.12f; break;
    case K_MINOR: sweep = 0.18f; break;
    default: sweep = 0.3f; break;
    }
    ev_at(0.0f, E_SWEEP, sweep * dv, 0);
    for (i = 0; i < 4; i++) {
        float t = 12.0f * (float)i;
        int back = i & 1;
        if (fill && i == 3)
            break;
        if (back)
            drum(t, DR_HAT, 0.4f);
        if (!back)
            drum(t, DR_KICK, (i == 0 ? 0.24f : 0.16f) * dv * (pc.kind == K_BALLAD ? 0.7f : 1.0f));
        switch (pc.kind) {
        case K_SLOW:                             /* triplets on the snare, the slap on two and four */
            drum(t, back ? DR_SLAP : DR_TAP, back ? 0.5f * dv : 0.2f * dv);
            drum(t + 4.0f, DR_TAP, 0.1f * dv);
            drum(t + 8.0f, DR_TAP, 0.15f * dv);
            break;
        case K_SHUF:                             /* the shuffle: long-short, the backbeat */
            drum(t, back ? DR_SLAP : DR_TAP, back ? 0.56f * dv : 0.24f * dv);
            drum(t + 8.0f, DR_TAP, 0.17f * dv);
            break;
        case K_MINOR:                            /* even eighths, the backbeat, the kick pushing */
            drum(t, back ? DR_SLAP : DR_TAP, back ? 0.5f * dv : 0.16f * dv);
            drum(t + 6.0f, DR_TAP, 0.11f * dv);
            if (i == 1)
                drum(18.0f, DR_KICK, 0.13f * dv);
            break;
        default:                                 /* the ballad: the sweep, a soft slap, triplets into the next */
            if (back)
                drum(t, DR_SLAP, 0.32f * dv);
            if (i == 3) {
                drum(t + 4.0f, DR_TAP, 0.08f * dv);
                drum(t + 8.0f, DR_TAP, 0.12f * dv);
            }
            break;
        }
    }
    if (fill) {                                  /* the last beat: brush strokes rising into the next bar */
        if (pc.even) {
            drum(36.0f, DR_SLAP, 0.34f * dv);
            drum(39.0f, DR_TAP, 0.3f * dv);
            drum(42.0f, DR_SLAP, 0.44f * dv);
            drum(45.0f, DR_TAP, 0.38f * dv);
        } else {
            drum(36.0f, DR_SLAP, 0.34f * dv);
            drum(40.0f, DR_SLAP, 0.42f * dv);
            drum(44.0f, DR_SLAP, 0.52f * dv);
        }
        drum(36.0f, DR_HAT, 0.4f);
    }
}

/* the bass: the chord's root (the octave nearest the last note) and its line */
static int b_root(ch_t c)
{
    int r = in_rng(ch_rootpc(c), 28);
    if (pc.bass_prev - r > 7 && r + 12 <= 45)
        r += 12;
    return r;
}
static int b_approach(int from, ch_t nx)         /* a half step into the next root, from where the line is */
{
    int nr = b_root(nx);
    return iabs(nr + 1 - from) < iabs(nr - 1 - from) ? nr + 1 : nr - 1;
}
static void bass_bar(const bctx_t *B)
{
    float v = 0.55f + 0.3f * B->x;
    int r = b_root(B->c0), i, third = CT_IV[B->c0.t][1], fifth = CT_IV[B->c0.t][2];
    int change = B->nx.root != 99 && (B->nx.root != (B->two ? B->c1.root : B->c0.root) || B->nx.t != (B->two ? B->c1.t : B->c0.t));
    if (!B->band)
        return;
    {                                            /* a new chord: the walk starts up from its root */
        static int last;
        int now = B->c0.root * 16 + B->c0.t;
        if (now != last)
            pc.bass_up = 1;
        last = B->two ? B->c1.root * 16 + B->c1.t : now;
    }
    if (B->fin) {
        bas(0.0f, in_rng(ch_rootpc(B->c0), 28), v, 92.0f);
        return;
    }
    if (B->pre) {
        bas(0.0f, r, v, 10.0f);
        if (pc.ending == END_B2) {
            bas(36.0f, in_rng((en_key + 1) % 12, 30), v, 11.0f);
        } else {
            bas(24.0f, in_rng((en_key + 8) % 12, 30), v, 6.0f);
            bas(36.0f, in_rng((en_key + 7) % 12, 30), v, 11.0f);
        }
        return;
    }
    if (B->stop) {
        bas(0.0f, r, v, 6.0f);
        return;
    }
    if (B->two) {                                /* two chords: root and fifth (or third) of each, the last leaning */
        int r2 = b_root(B->c1);
        bas(0.0f, r, v, 10.0f);
        bas(12.0f, r + (fifth < 8 ? fifth : third), v * 0.85f, 10.0f);
        pc.bass_prev = r + fifth;
        r2 = b_root(B->c1);
        bas(24.0f, r2, v * 0.95f, 10.0f);
        bas(36.0f, change ? b_approach(r2, B->nx) : r2 + CT_IV[B->c1.t][2], v * 0.85f, 10.0f);
        return;
    }
    {
        int up[4], dn[4], *w, sev = CT_N[B->c0.t] >= 4 ? CT_IV[B->c0.t][3] : 10, six = B->c0.t == T_M7 || B->c0.t == T_M9 ? 10 : 9;
        int walk = pc.kind == K_SLOW || (pc.kind == K_SHUF && B->sec != S_HEAD && B->sec != S_INTRO);
        int boogie = pc.kind == K_SHUF && walk && (pf(P_DRIVE) > 0.62f || B->sec == S_OUT || B->sec == S_STOP) &&
                     B->sec != S_HARP;
        if (B->c0.t == T_DIM7)
            six = 9, sev = 9;
        up[0] = r, up[1] = r + third, up[2] = r + fifth, up[3] = r + six;
        dn[0] = r + (sev == 9 ? 12 : sev), dn[1] = r + six, dn[2] = r + fifth, dn[3] = r + third;
        if (pc.kind == K_MINOR) {               /* the minor: root, fifth, and on into the next */
            int f5 = r + fifth;
            if (change && f5 - b_root(B->nx) > 9 && f5 - 12 >= 28)
                f5 -= 12;                        /* the fifth below: the line steps down into the next root */
            bas(0.0f, r, v, 20.0f);
            bas(24.0f, f5, v * 0.85f, 10.0f);
            bas(36.0f, change ? b_approach(f5, B->nx) : r + 10, v * 0.8f, 10.0f);
            return;
        }
        if (pc.kind == K_BALLAD || !walk) {      /* the two-beat: root and fifth, a pickup now and then */
            int p = change && trand() < 0.5f + 0.3f * B->x;
            bas(0.0f, r, v, 22.0f);
            bas(24.0f, r + fifth > 47 ? r + fifth - 12 : r + fifth, v * 0.85f, p ? 14.0f : 20.0f);
            if (p)
                bas(pc.kind == K_SHUF ? 44.0f : 44.0f, b_approach(r + fifth, B->nx), v * 0.6f, 3.5f);
            return;
        }
        if (boogie) {                            /* the boogie: each beat long-short, up the chord and down */
            w = pc.bass_up ? up : dn;
            for (i = 0; i < 4; i++) {
                int n = w[i];
                bas(12.0f * (float)i, n, v * (i == 0 ? 1.0f : 0.85f), 7.0f);
                if (i == 3 && change)
                    n = b_approach(n, B->nx);
                bas(12.0f * (float)i + 8.0f, n, v * 0.6f, 3.5f);
            }
            pc.bass_up = change ? 1 : pc.bass_up ^ 1;
            return;
        }
        w = pc.bass_up ? up : dn;                /* the walk: up the chord, down again; leaning into the next */
        for (i = 0; i < 4; i++) {
            int n = w[i];
            if (i == 3 && change && !(pc.kind == K_SLOW && trand() < 0.4f))
                n = b_approach(w[2], B->nx);
            bas(12.0f * (float)i, n, v * (i == 0 ? 1.0f : 0.84f), 10.0f);
            if (i == 3 && change && n == w[3])  /* a triplet's pickup on the last eighth */
                bas(44.0f, b_approach(n, B->nx), v * 0.55f, 3.5f);
        }
        pc.bass_up = change ? 1 : pc.bass_up ^ 1;
    }
}

/* the piano behind someone: the kind's comping, in the right hand */
static void comp(const bctx_t *B, float x)
{
    int a[3], i, k;
    float v = loud(x) * 0.55f;
    for (k = 0; k < (B->two ? 2 : 1); k++) {
        ch_t c = k ? B->c1 : B->c0;
        float t0 = k ? 24.0f : 0.0f, t1 = B->two && !k ? 24.0f : 48.0f;
        comp_voicing(c, 57, 71, a);
        switch (pc.kind) {
        case K_SLOW: {                           /* the triplets: every eighth, or from two, or the "and-a"s */
            int pat = pf(P_DRIVE) + 0.3f * x > 0.55f || B->sec == S_HARP ? 0 : tirand(3);
            for (i = 0; i < 12; i++) {
                float t = 4.0f * (float)i;
                if (t < t0 || t >= t1)
                    continue;
                if ((pat == 1 && i < 3) || (pat == 2 && i % 3 == 0))
                    continue;
                pno(t, a, 3, v * (i % 3 == 0 ? (i % 6 == 3 ? 1.0f : 0.85f) : 0.7f), 3.4f, R_COMP, hum(3.0f));
            }
            break;
        }
        case K_SHUF: {                           /* the chank on two and four, an "and" pushing now and then */
            int pat = tirand(3);
            for (i = 0; i < 4; i++) {
                float t = 12.0f * (float)i;
                if (t < t0 || t >= t1)
                    continue;
                if (i & 1)
                    pno(t, a, 3, v, 5.0f, R_COMP, hum(3.0f));
                if (pat == 1 && i == 2)
                    pno(t + 8.0f, a, 3, v * 0.75f, 3.0f, R_COMP, hum(3.0f));
                if (pat == 2 && (i & 1))
                    pno(t + 8.0f, a, 3, v * 0.65f, 3.0f, R_COMP, hum(3.0f));
            }
            break;
        }
        case K_MINOR:                            /* held, struck again on three, or pushed on the and of two */
            if (tirand(3) == 0 && !B->two) {
                pno(0.0f, a, 3, v * 0.9f, 16.0f, R_COMP, hum(3.0f));
                pno(18.0f, a, 3, v * 0.8f, 28.0f, R_COMP, hum(3.0f));
            } else {
                pno(t0, a, 3, v * 0.9f, t1 - t0 - 2.0f, R_COMP, hum(3.0f));
            }
            break;
        default: {                               /* the ballad: a rocking broken chord in triplets */
            static const int8_t RK[12] = {0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1};
            int lad[4];
            lad[0] = a[0], lad[1] = a[1], lad[2] = a[2], lad[3] = a[0] + 12;
            for (i = 0; i < 12; i++) {
                float t = 4.0f * (float)i;
                if (t < t0 || t >= t1)
                    continue;
                pn1(t, lad[RK[i]], v * (i % 3 == 0 ? 0.9f : 0.7f), 6.0f, R_COMP, hum(4.0f));
            }
            break;
        }
        }
    }
}
/* the left hand while the right plays: a shell on one (and three), or the shuffle's chank */
static void lh(const bctx_t *B, float x)
{
    int s[2], k;
    float v = loud(x) * 0.5f;
    for (k = 0; k < (B->two ? 2 : 1); k++) {
        ch_t c = k ? B->c1 : B->c0;
        float t0 = k ? 24.0f : 0.0f;
        shell(c, s);
        if (pc.kind == K_SHUF) {
            pno(t0 + 12.0f, s, 2, v, 5.0f, R_LH, hum(3.0f));
            if (!B->two)
                pno(36.0f, s, 2, v, 5.0f, R_LH, hum(3.0f));
        } else {
            pno(t0, s, 2, v, B->two ? 22.0f : 22.0f, R_LH, hum(3.0f));
            if (!B->two)
                pno(24.0f, s, 2, v * 0.8f, 20.0f, R_LH, hum(3.0f));
        }
    }
}

static const lick_t *lk(int i) { return pc.even ? &MLICK[i] : &LICK[i]; }
static int pick(const uint8_t *set, int n, int not) /* one of a set, not the last */
{
    int i = set[tirand(n)];
    if (i == not)
        i = set[tirand(n)];
    return i;
}
#define PICK(s, nt) pick(s, NA(s), nt)
static int call_pick(int not) { return pc.even ? PICK(MK_CALL, not) : PICK(LK_CALL, not); }
static int home_pick(int not) { return pc.even ? PICK(MK_HOME, not) : PICK(LK_HOME, not); }
static int tail_pick(int not) { return pc.even ? PICK(MK_TAIL, not) : PICK(LK_TAIL, not); }

static void turnaround(float x)                  /* the tonic on top, a line falling under it (b7 6 b6 5) */
{
    int i;
    for (i = 0; i < 4; i++) {
        float t = 12.0f * (float)i;
        int two[2];
        two[0] = pc.T - 2 - i;
        two[1] = pc.T;
        pno(t, two, 2, loud(x) * 0.62f, pc.even ? 5.0f : 7.0f, R_MEL, hum(4.0f));
        pn1(t + (pc.even ? 6.0f : 8.0f), pc.T, loud(x) * 0.45f, 3.5f, R_MEL, hum(4.0f));
    }
}
static void pickup(float x)                      /* the last bar: climbing into the next chorus's call */
{
    int nx = call_pick(-1), tg = pc.T + lk(nx)->o[0];
    int n1 = bl_fit(tg - 5, bar_ch(36), 1), n2 = bl_fit(tg - 3, bar_ch(40), 1);
    float s = pc.even ? 3.0f : 4.0f;
    pc.sa = nx;
    pn1(48.0f - 3.0f * s, n1, loud(x) * 0.6f, s - 0.5f, R_MEL, hum(5.0f));
    pn1(48.0f - 2.0f * s, n2 > n1 && n2 < tg - 1 ? n2 : tg - 2, loud(x) * 0.6f, s - 0.5f, R_MEL, hum(5.0f));
    pn1(48.0f - s, tg - 1, loud(x) * 0.62f, s - 0.5f, R_MEL, hum(5.0f));
}
static void stab(float x)                        /* the piano answering itself: a soft stab, held */
{
    ch_t c = bar_ch(20);
    int a[3];
    comp_voicing(c, 60, 74, a);
    pno(pc.even ? 18.0f : 20.0f, a, 3, loud(x) * 0.45f, 3.5f, R_COMP, hum(4.0f));
    pno(pc.even ? 30.0f : 32.0f, a, 3, loud(x) * 0.42f, 10.0f, R_COMP, hum(4.0f));
}
static void trem(float t0, float len, int a, int b, float x)   /* the right hand shaking between two notes */
{
    float t;
    int k = 0;
    for (t = t0; t < t0 + len; t += 2.0f, k++)
        pn1(t, k & 1 ? b : a, loud(x) * (0.55f + 0.1f * (t - t0) / len), 1.8f, R_MEL, hum(2.0f));
}

/* the piano: whatever it leads this bar, its left hand under it; else it comps */
static void piano_bar(const bctx_t *B)
{
    float x = B->x;
    int lead = 0, hl = B->line == B->nlines - 1;
    if (B->fin) {                                /* the last chord, rolled; a little fall on top */
        ch_t c = pc.kind == K_MINOR ? (ch_t){0, T_M6} : (ch_t){0, T_DOM9};
        p_chord(0.0f, c, loud(0.5f) * 0.6f, 92.0f, 1);
        if (!pc.harp) {
            pn1(12.0f, pc.T + 12 + (pc.kind == K_MINOR ? 3 : 4), loud(0.4f) * 0.5f, 6.0f, R_MEL, hum(5.0f));
            pn1(18.0f, pc.T + 12 + 2, loud(0.4f) * 0.45f, 6.0f, R_MEL, hum(5.0f));
            pn1(24.0f, pc.T + 12, loud(0.4f) * 0.5f, 60.0f, R_MEL, hum(5.0f));
        }
        return;
    }
    if (B->pre) {                                /* the ending: home, then the hits */
        ch_t e1 = pc.ending == END_B2 ? (ch_t){1, T_DOM9} : (ch_t){8, T_DOM9}, e2 = {7, T_DOM9};
        if (pc.kind == K_MINOR)
            e1 = (ch_t){8, T_DOM7}, e2 = (ch_t){7, T_7B9};
        bar_c[0] = B->c0, ch_two = 0;
        p_lick(lk(home_pick(-1)), 0.0f, x * 0.9f, 0);
        if (pc.ending == END_B2) {
            chord_ev(36.0f, e1);
            p_chord(36.0f, e1, loud(x) * 0.6f, 10.0f, 0);
        } else {
            chord_ev(24.0f, e1);
            p_chord(24.0f, e1, loud(x) * 0.6f, 6.0f, 0);
            chord_ev(36.0f, e2);
            p_chord(36.0f, e2, loud(x) * 0.62f, 10.0f, 0);
        }
        return;
    }
    if (B->sec == S_COUNT)
        return;
    if (B->stop) {                               /* stop time: the hit; the soloist fills the space */
        if (B->sec == S_PIANO || (B->sec == S_HARP && 0)) {
            int s[2];
            shell(B->c0, s);
            pno(0.0f, s, 2, loud(x) * 0.6f, 5.0f, R_LH, 0);
            p_lick(lk(B->lb & 1 ? home_pick(pc.last_lick) : call_pick(pc.last_lick)), 0.0f, x, 0);
        } else {
            int a[3];
            comp_voicing(B->c0, 57, 71, a);
            pno(0.0f, a, 3, loud(x) * 0.6f, 5.0f, R_COMP, 0);
        }
        return;
    }
    switch (B->sec) {
    case S_INTRO:
        if (B->b12 == pc.fl - 4)
            lead = 1, p_lick(lk(pc.la), 0.0f, x, 0);
        else if (B->b12 == pc.fl - 3)
            lead = 1, p_lick(lk(home_pick(pc.la)), 0.0f, x * 0.9f, 0);
        else if (B->b12 == pc.fl - 2 && !B->two)
            lead = 1, turnaround(x);
        else if (B->b12 == pc.fl - 2)
            lead = 1, p_lick(lk(home_pick(-1)), 0.0f, x, 0);
        else
            lead = 1, pickup(x);
        if (B->b12 == pc.fl - 1)
            pc.sa = -1;
        break;
    case S_HEAD:
    case S_OUT: {
        int answer = pc.harp && pc.hans[B->line];
        if (B->b12 == pc.fl - 2 && !(answer && hl)) {
            lead = 1;
            if (B->two)
                p_lick(lk(home_pick(-1)), 0.0f, x, 0);
            else
                turnaround(x);
        } else if (B->b12 == pc.fl - 1) {
            if (trand() < 0.65f)
                lead = 1, pickup(x);
        } else if (B->lb == 0) {
            lead = 1, p_lick(lk(hl ? pc.lb : pc.la), 0.0f, x, 0);
        } else if (B->lb == 1 && trand() < 0.25f + 0.45f * pf(P_DRIVE)) {
            lead = 1, p_lick(lk(tail_pick(hl ? pc.lb : pc.la)), 0.0f, x * 0.9f, 0);
        } else if (!answer && B->lb == 2) {
            if (trand() < 0.5f)
                lead = 1, p_lick(lk(home_pick(pc.la)), 0.0f, x * 0.85f, 0);
            else
                lead = 1, stab(x);
        } else if (!answer && B->lb == 3 && trand() < 0.5f) {
            lead = 1, p_lick(lk(tail_pick(-1)), 0.0f, x * 0.8f, 0);
        }
        break;
    }
    case S_PIANO: {
        int top = B->chorus == pc.first_solo + pc.nsolo - 1 || B->x > 0.72f;
        int o = top && B->lb == 0 && trand() < 0.5f ? 12 : 0;
        lead = 1;
        if (B->b12 == pc.fl - 2 && !B->two)
            turnaround(x);
        else if (B->b12 == pc.fl - 1)
            pickup(x);
        else if (B->lb == 0)
            p_lick(lk(pc.last_lick = hl ? pc.sb : pc.sa), 0.0f, x, o);
        else if (B->lb == 1 && top && B->line == 1 && !pc.even)
            trem(0.0f, 24.0f, pc.T + 7, pc.T + 12, x), pn1(24.0f, pc.T + 12, loud(x) * 0.7f, 16.0f, R_MEL, hum(4.0f));
        else if (B->lb == 1 && trand() < 0.45f + 0.4f * pf(P_DRIVE))
            p_lick(lk(pc.last_lick = call_pick(pc.last_lick)), 0.0f, x * 0.92f, top ? 12 : 0);
        else if (B->lb == 2)
            p_lick(lk(pc.last_lick = home_pick(pc.last_lick)), 0.0f, x * 0.95f, 0);
        else if (trand() < 0.55f)
            p_lick(lk(pc.last_lick = tail_pick(pc.last_lick)), 0.0f, x * 0.85f, 0);
        else
            lead = 0;
        break;
    }
    case S_HARP:                                 /* behind the harp: comping, a word where he rests */
        if (B->lb == 3 && B->b12 != pc.fl - 1 && trand() < 0.55f) {
            lead = 1;
            p_lick(lk(tail_pick(-1)), 0.0f, x * 0.8f, 0);
        } else if (B->b12 == pc.fl - 1 && trand() < 0.5f) {
            lead = 1, pickup(x);
        }
        break;
    default: break;
    }
    if (lead && B->band)
        lh(B, x);
    else if (lead)
        lh(B, x * 0.9f);
    else
        comp(B, x);
}

/* the harp: answering the head, his chorus, a pad behind the piano, the last word */
static const hlick_t *hk(int i) { return pc.kind == K_MINOR ? &MHLICK[i] : &HLICK[i]; }
static int h_call(void) { return pc.kind == K_MINOR ? PICK(MHK_CALL, -1) : PICK(HK_CALL, -1); }
static int h_ans(int one) { return pc.kind == K_MINOR ? (one ? PICK(MHK_ANS1, -1) : PICK(MHK_ANS, -1)) : one ? PICK(HK_ANS1, -1) : PICK(HK_ANS, -1); }
static int h_bars(int i)                         /* how many bars a lick spans */
{
    const hlick_t *L = hk(i);
    return L->at[L->n - 1] + L->du[L->n - 1] > 50 ? 2 : 1;
}
static void harp_bar(const bctx_t *B)
{
    float x = B->x;
    int hl = B->line == B->nlines - 1;
    if (!pc.harp || !B->band)
        return;
    if (B->fin) {                                /* the last word: a long wail over the final chord */
        if (pc.kind == K_MINOR)
            hrp(4.0f, pc.H + 12, loud(0.55f) * 1.0f, 80.0f, 0, HF_SHAKE, 3, 0, (int32_t)(0.02f * EN_SR));
        else
            hrp(4.0f, pc.H + 12, loud(0.55f) * 1.0f, 80.0f, 2, 0, 0, 0, (int32_t)(0.02f * EN_SR));
        return;
    }
    if (B->pre)
        return;
    if (B->stop) {
        if (B->sec == S_HARP)
            h_lick(hk(B->lb & 1 ? h_ans(1) : pc.kind == K_MINOR ? 0 : (B->lb ? 3 : 0)), 8.0f, x, B->b12);
        return;
    }
    switch (B->sec) {
    case S_HEAD:
    case S_OUT:
        if (B->lb == 2 && pc.hans[B->line] && B->b12 + 1 < pc.fl - (hl ? 0 : 0)) {
            int a = hl ? h_ans(1) : (B->line ? pc.hb : pc.ha);
            if (hl && h_bars(a) > 1)
                a = h_ans(1);
            h_lick(hk(a), 0.0f, x, B->b12);
        }
        break;
    case S_HARP:
        if (B->lb == 0 && B->b12 < pc.fl - 2) {  /* his call, the same twice, then the third line's */
            int a = hl ? pc.sb : pc.sa;
            h_lick(hk(a), 0.0f, x, B->b12);
            if (h_bars(a) == 1 && trand() < 0.5f + 0.3f * pf(P_DRIVE))
                h_lick(hk(h_ans(1)), 48.0f, x * 0.9f, B->b12);
        } else if (B->lb == 2 && !hl && trand() < 0.35f + 0.3f * pf(P_DRIVE)) {
            h_lick(hk(h_ans(1)), 0.0f, x * 0.9f, B->b12);
        } else if (hl && B->lb == 2) {
            h_lick(hk(h_ans(1)), 0.0f, x, B->b12);
        }
        break;
    case S_PIANO:                                /* behind the piano, sometimes: a soft held chord on I or IV */
        if (pc.hbg && B->line < 2 && (B->b12 & 1) == 0 && !B->two && pc.kind != K_MINOR &&
            (B->c0.root == 0 || B->c0.root == 5))
            hrp(0.0f, pc.H + B->c0.root, loud(x) * 0.42f, 40.0f, 0, HF_CHORD, 4, 7, hum(10.0f));
        break;
    default: break;
    }
}

static void count_bar(int b)                     /* the brushes' handles: the count (in two, then in four) */
{
    int i;
    if (pc.ncount == 2 && b == 0) {
        drum(0.0f, DR_STICK, 0.55f);
        drum(24.0f, DR_STICK, 0.55f);
        return;
    }
    for (i = 0; i < 4; i++)
        drum(12.0f * (float)i, DR_STICK, i ? 0.6f : 0.66f);
}

/* ------------------------------------------------------------ the bar --- */
static void band_bar(int b)
{
    bctx_t B;
    int k;
    __builtin_memset(&B, 0, sizeof B);
    B.chorus = -1;
    B.band = 1;
    B.nlines = pc.fl / 4;
    bar_rit = 0.0f;
    bar_spt = pc.spt;
    if (b < pc.ncount) {
        mark(S_COUNT, 0, 0);
        count_bar(b);
        return;
    }
    if (b < pc.intro) {
        B.b12 = pc.fl - (pc.intro - b);
        B.sec = S_INTRO;
        B.band = pc.introtype != IN_PIANO || B.b12 >= pc.fl - 2;
        B.x = 0.45f;
    } else if (b == pc.nbars - 1) {
        B.fin = 1;
        B.sec = S_END;
        B.c0 = form_ch(0, 0);
        B.x = 0.5f;
        mark(S_END, 0, pc.nch);
        chord_ev(0.0f, pc.kind == K_MINOR ? (ch_t){0, T_M6} : (ch_t){0, T_DOM9});
        drums_bar(&B);
        bass_bar(&B);
        piano_bar(&B);
        harp_bar(&B);
        return;
    } else {
        k = b - pc.intro;
        B.chorus = k / pc.fl;
        B.b12 = k % pc.fl;
        B.sec = pc.role[B.chorus];
        B.last = B.chorus == pc.nch - 1;
        B.stop = B.chorus == pc.stopc && B.b12 < 4;
        B.pre = B.last && B.b12 == pc.fl - 1;
        if (B.sec == S_HEAD)
            B.x = 0.45f;
        else if (B.sec == S_OUT)
            B.x = 0.6f;
        else {
            int i = B.chorus - pc.first_solo;
            B.x = 0.5f + 0.28f * (float)i / (float)(pc.nsolo > 1 ? pc.nsolo - 1 : 1);
        }
        if (B.stop)
            B.x = fm_maxf(B.x, 0.66f);
        if (B.pre)
            bar_rit = 0.3f;
    }
    B.line = B.b12 / 4;
    B.lb = B.b12 % 4;
    B.x *= B.line == 2 ? 1.06f : B.line == 1 ? 1.0f : 0.95f;
    B.c0 = form_ch(B.b12, 0);
    B.c1 = pc.F->c[B.b12][1];
    B.two = B.c1.root != 99;
    B.nx = B.pre ? (ch_t){0, T_DOM7} : form_ch(B.b12 + 1, 0);
    bar_b12 = B.b12;
    bar_c[0] = B.c0;
    bar_c[1] = B.c1;
    ch_two = B.two;
    if (B.sec == S_HEAD && B.b12 == 0 && B.chorus == 0)
        pc.sa = pc.la;
    if (B.b12 == 0 && (B.sec == S_PIANO || B.sec == S_HARP)) {   /* a solo chorus: its two calls */
        if (B.sec == S_PIANO) {
            pc.sa = pc.sa >= 0 && B.chorus == pc.first_solo ? pc.sa : call_pick(pc.la);
            pc.sb = home_pick(-1);
        } else {
            pc.sa = h_call();
            do
                pc.sb = h_call();
            while (pc.sb == pc.sa && pc.kind != K_MINOR && 0);
            if (pc.sb == pc.sa)
                pc.sb = h_call();
        }
    }
    mark(B.stop ? S_STOP : B.pre ? S_END : B.sec, B.b12, B.chorus + 1);
    chord_ev(0.0f, B.c0);
    if (B.two)
        chord_ev(24.0f, B.c1);
    if (pc.ped && B.band) {                      /* a little pedal on the slow ones, changed with the harmony */
        ev_at(0.0f + 0.3f, E_PEDUP, 0.0f, 0);
        ev_at(1.5f, E_PEDDN, 0.0f, 0);
        if (B.two) {
            ev_at(24.3f, E_PEDUP, 0.0f, 0);
            ev_at(25.5f, E_PEDDN, 0.0f, 0);
        }
    }
    drums_bar(&B);
    bass_bar(&B);
    harp_bar(&B);
    piano_bar(&B);
}

/* ----------------------------------------------------------- planning --- */
static void tempo_set(void)
{
    static const uint8_t BASE[K_N] = {54, 112, 70, 46};
    float bpm = (float)BASE[pc.kind] * (1.0f + 0.12f * ((float)(en_track_seed >> 24 & 0xFF) / 255.0f - 0.5f));
    bpm *= (float)par[P_TEMPO] * 0.01f;
    en_bpm = (uint8_t)clampi((int)(bpm + 0.5f), 30, 200);
    pc.spt = EN_SR * 60.0f / ((float)en_bpm * (float)TPB);
    beat_len = pc.spt * 12.0f;
}

static void new_tune(void)
{
    const style_t *s = sty(style_i);
    int i, tot = 0, pick, heads, m, hp, pp;
    float chs, want;
    static const uint8_t KEYS[] = {4, 4, 9, 9, 7, 7, 10, 10, 0, 2, 5, 5, 3, 11};   /* the blues' keys */
    en_track_no++;
    en_track_seed = en_track_rand(en_seed_base, en_track_no);
    trng = en_track_seed;
    __builtin_memset(&pc, 0, sizeof pc);
    for (i = 0; i < K_N; i++)                    /* the kind: by the night's taste, not the same twice running */
        tot += s->w[i] * (i == last_kind ? 1 : 3);
    pick = tirand(tot);
    for (i = 0; i < K_N; i++) {
        pick -= s->w[i] * (i == last_kind ? 1 : 3);
        if (pick < 0)
            break;
    }
    pc.kind = i < K_N ? i : K_SLOW;
    if (last_kind < 0) {                         /* the set's first: the night's own kind (a shuffle on a tie) */
        static const uint8_t PREF[K_N] = {K_SHUF, K_SLOW, K_MINOR, K_BALLAD};
        pc.kind = PREF[0];
        for (i = 1; i < K_N; i++)
            if (s->w[PREF[i]] > s->w[pc.kind])
                pc.kind = PREF[i];
    }
    last_kind = pc.kind;
    en_key = KEYS[tirand(NA(KEYS))];
    pc.even = pc.kind == K_MINOR;
    switch (pc.kind) {
    case K_SLOW: pc.form = s->jazz && trand() < 0.45f ? 3 : tirand(3); pc.F = &F_BLUES[pc.form]; break;
    case K_SHUF: pc.form = tirand(3); pc.F = &F_BLUES[pc.form]; break;
    case K_MINOR: pc.form = tirand(2); pc.F = &F_MINOR[pc.form]; break;
    default: pc.form = tirand(2); pc.F = &F_BALLAD[pc.form]; break;
    }
    pc.fl = pc.kind == K_BALLAD ? 8 : 12;
    felt = (float)s->felt * 0.01f;
    harp_amp = (float)s->amp;
    pc.ped = pc.kind == K_BALLAD || pc.kind == K_MINOR;
    pc.T = in_rng(en_key, 62);
    pc.H = in_rng(en_key, pc.kind == K_MINOR ? 62 : 60);
    pc.bass_prev = 36;
    pc.bass_up = 1;
    pc.sa = -1;
    tempo_set();
    /* the harp player: in or not tonight; answering which lines of the head; behind the piano */
    pc.harp = par[P_HARP] > 0 && trand() < 0.2f + 0.85f * pf(P_HARP);
    for (i = 0; i < 3; i++)
        pc.hans[i] = pc.harp && trand() < 0.5f + 0.5f * pf(P_HARP);
    pc.hbg = pc.harp && trand() < 0.15f + 0.4f * pf(P_HARP);
    /* the head's licks: a call for the first lines, another for the last; the harp's answers */
    pc.la = call_pick(-1);
    pc.lb = home_pick(pc.la) == pc.la ? call_pick(pc.la) : call_pick(pc.la);
    if (pc.lb == pc.la)
        pc.lb = home_pick(pc.la);
    pc.ha = h_ans(0);
    pc.hb = trand() < 0.6f ? pc.ha : h_ans(0);
    pc.last_lick = -1;
    /* the intro */
    switch (pc.kind) {
    case K_SLOW: pc.introtype = trand() < 0.5f ? IN_PIANO : IN_V; pc.ncount = pc.introtype == IN_V; break;
    case K_SHUF: pc.introtype = trand() < 0.6f ? IN_COUNT : IN_V; pc.ncount = 2; break;
    case K_MINOR: pc.introtype = trand() < 0.6f ? IN_V : IN_PIANO; pc.ncount = pc.introtype == IN_V; break;
    default: pc.introtype = IN_PIANO; pc.ncount = 0; break;
    }
    pc.intro = pc.ncount + (pc.introtype == IN_COUNT ? 0 : 4);
    /* the choruses: as many as Length wants (three at least): the head (two on a long one), solos, the head out */
    chs = (float)(pc.fl * 4) * 60.0f / (float)en_bpm;
    want = (float)LEN_MIN[clampi(par[P_LENGTH], 0, 4)] * 60.0f * (0.88f + 0.24f * trand());
    pc.nch = clampi((int)(want / chs + 0.5f), 3, 14);
    heads = pc.nch >= 8 ? 2 : 1;
    m = pc.nch - heads - 1;
    hp = pc.harp ? (m >= 2 ? m / 2 : m) : 0;
    if (pc.harp && pf(P_HARP) < 0.3f && hp > 1)
        hp = 1;
    pp = m - hp;
    for (i = 0; i < heads; i++)
        pc.role[i] = S_HEAD;
    pc.first_solo = heads;
    pc.nsolo = m;
    {
        int harp_first = trand() < 0.4f;
        for (i = 0; i < m; i++)
            pc.role[heads + i] = (harp_first ? i < hp : i >= pp) ? S_HARP : S_PIANO;
    }
    pc.role[pc.nch - 1] = S_OUT;
    pc.stopc = -1;
    if (pc.kind != K_BALLAD && m >= 2 && trand() < (float)s->stop * 0.01f * (0.6f + 0.8f * pf(P_DRIVE)))
        pc.stopc = heads + 1 + tirand(m - 1);
    pc.ending = pc.kind == K_MINOR ? END_MIN : (pc.kind == K_SHUF ? (trand() < 0.7f ? END_6 : END_B2)
                                                                  : (trand() < 0.65f ? END_B2 : END_6));
    pc.nbars = pc.intro + pc.nch * pc.fl + 1;
    en_track_len_ms = (uint32_t)((float)pc.nbars * 48.0f * pc.spt / EN_SR * 1000.0f + 8000.0f);
    /* for the screen */
    en_kind = (uint8_t)pc.kind;
    en_form_len = (uint8_t)pc.fl;
    en_nchorus = (uint8_t)pc.nch;
    en_chorus = 0;
    en_bar = 0;
    for (i = 0; i < 12; i++) {
        en_form_root[i] = (int8_t)(i < pc.fl ? pc.F->c[i][0].root : 0);
        en_form_q[i] = (uint8_t)(i < pc.fl ? pc.F->c[i][0].t : 0);
    }
    en_section = pc.ncount ? S_COUNT : S_INTRO;
    {
        ev_t *e = ev_new(E_HARPIN, 0);           /* the harp player steps up (or stays at the bar) */
        if (e)
            e->a = (int8_t)pc.harp;
    }
    pc.rh.n = pc.lh.n = 0;
    cur_bar = 0;
    en_track_ms = 0;
    pstart = en_now_ms;
    en_phase = PH_INTRO;
    en_tracks_played++;
}

static void compose(void)
{
    int b = pc.bar;
    if (en_repeat_on && pc.nsolo > 0 && b == pc.intro + (pc.nch - 1) * pc.fl)
        b = pc.intro + pc.first_solo * pc.fl;    /* repeat: the solos again, no head out */
    if (b >= pc.nbars) {
        en_phase = PH_END;
        ring_left = (int)(EN_SR * (3.2f + 1.5f * pf(P_ROOM)));
        pc.bar = b;
        return;
    }
    cur_bar = b;
    en_phase = (uint8_t)(b < pc.intro ? PH_INTRO : b >= pc.nbars - 2 ? PH_END : PH_BODY);
    band_bar(b);
    bar_left += (float)at_tick((float)TBAR);
    beat_len = (float)at_tick(12.0f);
    pc.bar = b + 1;
}
