/* SPDX-License-Identifier: MPL-2.0
 * JAZZ TRIO: ashtonwallen, 2026 */
/* The tunes (part of jazz.c): which tune comes next in the set, its form and changes, its arrangement (bar by bar,
 * from the count to the last chord), its head melody; and the line player the heads and the solos share. */

/* the music's level (the transitions); the set's clock between tunes */
static float fade = 1.0f, fade_t = 1.0f, fade_rate = 0.0f;
static int skipping;
static float gap_left, appl_left, appl_peak;
static float beat_left, beat_len = 22050.0f;

/* --------------------------------------------------------- arrangement --- */
enum { BF_TWO = 1, BF_PSTART = 2, BF_SEND = 4, BF_RIT = 8, BF_LASTCH = 16, BF_BASIE = 32, BF_FORMEND = 64 };
typedef struct {
    uint8_t fbar, role, chorus, nch, fl, sub;    /* sub: the bar within its part */
} rec_t;
#define NREC 420
enum { E_TAG, E_RIT, E_BASIE, E_HOLD };
enum { IN_COUNT, IN_PIANO, IN_BASS, IN_VAMP };
enum { MK_LYRIC, MK_BEBOP, MK_RIFF, MK_MODAL };

typedef struct {
    int16_t tick, dur;                           /* from the section's start, in twelfths of a beat */
    uint8_t note, vel, pick;                     /* pick: a pickup into the next section */
} mn_t;
#define NMEL 96

static struct {
    int kind, key, minor, bpb, nbars;
    float bpm;
    fbar_t f[MAXBARS + 1];
    int fin_root, fin_q;
    uint8_t two_head, brush_head, brush_solo, ending, intro, mk;
    rec_t rec[NREC];
    int nrec, solo_rec, out_rec;                 /* the first bar of the solos, of the head out */
    mn_t mel[2][NMEL];
    int nmel[2];
    int last_kind;
    float last_bpm;
    int prev_kind[3];
} tu;

/* ------------------------------------------------------- pitch helpers --- */
static uint16_t rot(uint16_t m, int r)
{
    uint32_t x = m;
    r = (r % 12 + 12) % 12;
    x = (x << r) | (x >> (12 - r));
    return (uint16_t)(x & 0xFFFu);
}
static int in_mask(int note, uint16_t m) { return m >> (((note % 12) + 12) % 12) & 1u; }
static int step_in(int p, int dir, uint16_t m)   /* the next note strictly above (dir > 0) or below p in m */
{
    int i;
    if (!m)
        return p + dir;
    for (i = 1; i <= 12; i++)
        if (in_mask(p + dir * i, m))
            return p + dir * i;
    return p + dir;
}
static int near_in(int p, uint16_t m)           /* the nearest note in m (ties: below) */
{
    int i;
    if (!m || in_mask(p, m))
        return p;
    for (i = 1; i <= 6; i++) {
        if (in_mask(p - i, m))
            return p - i;
        if (in_mask(p + i, m))
            return p + i;
    }
    return p;
}

/* a bar's chord at a tick (twelfths of a beat from the bar's start) */
static int ch_root(const fbar_t *b, int tick)
{
    return b->n == 2 && tick >= tu.bpb * 6 ? b->root[1] : b->root[0];
}
static int ch_q(const fbar_t *b, int tick) { return b->n == 2 && tick >= tu.bpb * 6 ? b->q[1] : b->q[0]; }
static uint16_t m_ct(const fbar_t *b, int t) { return rot(Q_CT[ch_q(b, t)], ch_root(b, t)); }
static uint16_t m_strong(const fbar_t *b, int t) { return rot(Q_CT[ch_q(b, t)] | Q_COLOR[ch_q(b, t)], ch_root(b, t)); }
static uint16_t m_scale(const fbar_t *b, int t) { return rot(Q_SCALE[ch_q(b, t)], ch_root(b, t)); }
static uint16_t m_bebop(const fbar_t *b, int t) { return rot(Q_SCALE[ch_q(b, t)] | Q_BEBOP[ch_q(b, t)], ch_root(b, t)); }

/* ------------------------------------------------------------ the line --- */
/* notes go to the bar's plan (the players) or into a melody (a head being written) */
static mn_t *em_mel;
static int *em_n, em_base;
static void plan_note(int inst, int tick, int dur, int note, int vel, int fl);
enum { FL_RAW = 1, FL_DT = 2, FL_PICK = 4 };
static void emit(int inst, int tick, int dur, int note, float vel, int fl)
{
    int v = (int)(vel * 127.0f);
    v = v < 4 ? 4 : v > 127 ? 127 : v;
    if (em_mel) {
        if (*em_n < NMEL) {
            mn_t *m = &em_mel[(*em_n)++];
            m->tick = (int16_t)(em_base + tick);
            m->dur = (int16_t)dur;
            m->note = (uint8_t)note;
            m->vel = (uint8_t)v;
            m->pick = (uint8_t)((fl & FL_PICK) != 0);
        }
        return;
    }
    plan_note(inst, tick, dur, note, v, fl);
}

/* a line: bebop phrases with space between them. Chord tones on the downbeats (the bebop scales keep them there),
 * passing tones between; arpeggios up, scales down; enclosures round the next chord's tone; triplets; phrases
 * that start, build and end on a held chord tone or a short accented one */
typedef struct {
    int p, dir, rest, left, lo, hi, pend, inst;
    float dens, trip, dt, arp, quarter, vel;
    int bass, nphr;
} line_t;

static int line_rest(line_t *L)                  /* the space after a phrase, in ticks (eighths) */
{
    float beats = 1.0f + (1.0f - L->dens) * (1.5f + 5.0f * trand());
    if (L->bass)
        beats += 1.0f + 2.0f * trand();
    return (int)(beats * 2.0f) * 6;
}
static void line_start(line_t *L, const fbar_t *b, int t)
{
    int beats = 2 + tirand(2 + (int)(L->dens * 9.0f)), target;
    if (L->bass)
        beats = 2 + tirand(4);
    L->left = beats * 12;
    target = L->lo + (int)((float)(L->hi - L->lo) * (0.35f + 0.45f * trand()));
    if (L->nphr && trand() < 0.5f)               /* or on from where the last one ended, a leap away */
        target = L->p + (L->p > (L->lo + L->hi) / 2 ? -5 : 5);
    L->p = near_in(target, m_strong(b, t));
    L->dir = L->p > (L->lo + L->hi) / 2 ? -1 : (trand() < 0.5f ? -1 : 1);
    L->pend = -1;
    L->nphr++;
}
static int line_clamp(line_t *L, int n, int p, uint16_t m)
{
    if (n > L->hi) {
        L->dir = -1;
        n = step_in(p, -1, m);
    } else if (n < L->lo) {
        L->dir = 1;
        n = step_in(p, 1, m);
    }
    return n;
}
static int line_mv(line_t *L, int n, uint16_t m)   /* clamped, and never the same note twice running */
{
    n = line_clamp(L, n, L->p, m);
    if (n == L->p)
        n = line_clamp(L, step_in(L->p, L->dir, m), L->p, m);
    return n;
}
static float line_v(line_t *L, int off)
{
    float v = L->vel * (off ? 1.0f : 0.9f);
    if (trand() < 0.1f)
        v *= 0.68f;                              /* a ghosted note */
    return v;
}

/* one bar of the line: b the bar, nb the next (the chord after the barline); bpb beats */
static void line_bar(line_t *L, const fbar_t *b, const fbar_t *nb, int bpb)
{
    int beat;
    for (beat = 0; beat < bpb; beat++) {
        int t0 = beat * 12, off = 0, used = 12, n, k;
        const fbar_t *b2 = beat + 1 < bpb ? b : nb;
        int t2 = beat + 1 < bpb ? t0 + 12 : 0;
        float r;
        if (L->p < L->lo + 4)                    /* the line's centre pulls */
            L->dir = 1;
        else if (L->p > L->hi - 4)
            L->dir = -1;
        if (L->left <= 0) {                      /* resting */
            if (L->rest >= 12) {
                L->rest -= 12;
                continue;
            }
            off = L->rest;                       /* (6: the phrase starts on the off-beat) */
            L->rest = 0;
            line_start(L, b, t0 + off);
            if (off) {                           /* the first note, on the "and" */
                emit(L->inst, t0 + 6, 6, L->p, line_v(L, 1) * 1.05f, 0);
                L->left -= 6;
                continue;
            }
            emit(L->inst, t0, 6, L->p, line_v(L, 0), 0);
            n = step_in(L->p, L->dir, m_bebop(b, t0 + 6));
            n = line_clamp(L, n, L->p, m_bebop(b, t0 + 6));
            emit(L->inst, t0 + 6, 6, n, line_v(L, 1), 0);
            L->p = n;
            L->left -= 12;
            continue;
        }
        if (L->left <= 12) {                     /* the phrase's end */
            uint16_t me = rot(Q_CT[ch_q(b, t0)] | M(2), ch_root(b, t0));
            int fin = near_in(L->p + L->dir * 2, me);
            if (fin == L->p)
                fin = step_in(L->p, L->dir, me);
            fin = line_clamp(L, fin, L->p, me);
            if (trand() < 0.4f) {                /* doo-DAT: short and accented on the "and" */
                n = step_in(fin, -L->dir, m_scale(b, t0));
                if (n == L->p)                   /* (not the note just played: from the other side) */
                    n = step_in(fin, L->dir, m_scale(b, t0));
                emit(L->inst, t0, 6, n, line_v(L, 0), 0);
                emit(L->inst, t0 + 6, 4, fin, L->vel * 1.12f, 0);
            } else {                             /* held */
                emit(L->inst, t0, L->bass ? 12 : 12 + 6 * tirand(4), fin, L->vel * 1.04f, 0);
            }
            L->p = fin;
            L->left = 0;
            L->rest = line_rest(L);
            continue;
        }
        r = trand();
        if (L->pend >= 0) {                      /* an enclosure lands: eighths on from its target */
            n = L->pend;
            L->pend = -1;
            emit(L->inst, t0, 6, n, line_v(L, 0) * 1.05f, 0);
            L->p = n;
            n = step_in(n, L->dir, m_bebop(b, t0 + 6));
            n = line_clamp(L, n, L->p, m_bebop(b, t0 + 6));
            emit(L->inst, t0 + 6, 6, n, line_v(L, 1), 0);
            L->p = n;
        } else if (L->bass && r < 0.38f) {       /* the bass: a quarter, a chord tone */
            n = near_in(L->p + L->dir * 3, m_ct(b, t0));
            if (n == L->p)
                n = step_in(n, L->dir, m_ct(b, t0));
            n = line_mv(L, n, m_ct(b, t0));
            emit(L->inst, t0, 11, n, line_v(L, 0), 0);
            L->p = n;
        } else if (!L->bass && L->dt > 0.0f && r < L->dt) {   /* sixteenths (slow tempos) */
            for (k = 0; k < 4; k++) {
                int t = t0 + 3 * k;
                uint16_t m = m_bebop(b, t);
                n = step_in(L->p, L->dir, m);
                if (!(k & 1) && !in_mask(n, m_strong(b, t)))
                    n = step_in(n, L->dir, m_strong(b, t));
                n = line_clamp(L, n, L->p, m);
                emit(L->inst, t, 3, n, line_v(L, k & 1) * 0.95f, FL_DT);
                L->p = n;
            }
        } else if (r < L->dt + L->trip) {        /* a triplet: an arpeggio's piece, or a turn */
            uint16_t m = m_strong(b, t0);
            if (trand() < 0.45f && L->left >= 24) {
                int tgt = near_in(L->p, m_ct(b2, t2)), up = step_in(tgt, 1, m_scale(b2, t2));
                if (up == L->p)
                    up = step_in(up, 1, m_scale(b2, t2));
                emit(L->inst, t0, 4, up, line_v(L, 0), FL_RAW);
                emit(L->inst, t0 + 4, 4, tgt, line_v(L, 1) * 0.9f, FL_RAW);
                emit(L->inst, t0 + 8, 4, tgt - 1, line_v(L, 1), FL_RAW);
                L->pend = tgt;
                L->p = tgt - 1;
            } else {
                for (k = 0; k < 3; k++) {
                    n = step_in(L->p, L->dir, m);
                    n = line_clamp(L, n, L->p, m);
                    emit(L->inst, t0 + 4 * k, 4, n, line_v(L, k != 0), FL_RAW);
                    L->p = n;
                }
            }
        } else if (!L->bass && r < L->dt + L->trip + L->arp && L->left >= 30 && beat + 1 < bpb) {
            /* an arpeggio up through the chord and its ninth, two beats; then down */
            int c = near_in(L->p - 5, m_ct(b, t0));
            if (c < L->lo)
                c += 12;
            for (k = 0; k < 4; k++) {
                int t = t0 + 6 * k;
                uint16_t m = rot(Q_CT[ch_q(b, t)] | M(2), ch_root(b, t));
                if (k)
                    c = step_in(c, 1, m);
                if (c > L->hi)
                    c -= 12;
                emit(L->inst, t, 6, c, line_v(L, k & 1) * (k == 3 ? 1.1f : 1.0f), 0);
            }
            L->p = c;
            L->dir = -1;
            used = 24;
            beat++;
        } else if (r < L->dt + L->trip + L->arp + 0.22f && L->left >= 24 &&
                   (beat + 1 == bpb || ch_root(b2, t2) != ch_root(b, t0))) {
            /* an enclosure: round the next chord's tone, from above and from a half step under */
            uint16_t mt = rot(M(Q_3RD[ch_q(b2, t2)]) | M(Q_7TH[ch_q(b2, t2)]) | M(7) | M(0), ch_root(b2, t2));
            int tgt = near_in(L->p + L->dir * 2, mt);
            int up = step_in(tgt, 1, m_scale(b2, t2));
            if (tgt > L->hi)
                tgt -= 12, up -= 12;
            if (tgt - 1 < L->lo)
                tgt += 12, up += 12;
            if (up == L->p)                      /* (not the note just played) */
                up = step_in(up, 1, m_scale(b2, t2));
            emit(L->inst, t0, 6, up, line_v(L, 0), 0);
            emit(L->inst, t0 + 6, 6, tgt - 1, line_v(L, 1), 0);
            L->pend = tgt;
            L->p = tgt - 1;
        } else if (r < L->dt + L->trip + L->arp + 0.22f + L->quarter) {   /* a quarter: a breath in the line */
            n = near_in(L->p + L->dir * 2, m_strong(b, t0));
            n = line_mv(L, n, m_strong(b, t0));
            emit(L->inst, t0, 10, n, line_v(L, 0), 0);
            L->p = n;
        } else {                                 /* eighths along the bebop scale */
            for (k = 0; k < 2; k++) {
                int t = t0 + 6 * k;
                uint16_t m = m_bebop(b, t);
                n = step_in(L->p, L->dir, m);
                if (!k && !in_mask(n, m_strong(b, t)))
                    n = near_in(n + L->dir, m_strong(b, t)) == L->p ? step_in(n, L->dir, m_strong(b, t))
                                                                   : near_in(n + L->dir, m_strong(b, t));
                n = line_mv(L, n, m);
                emit(L->inst, t, 6, n, line_v(L, k), 0);
                L->p = n;
                if (!k && trand() < 0.14f)
                    L->dir = -L->dir;
            }
        }
        L->left -= used;
    }
}

/* -------------------------------------------------------------- forms --- */
static void add_sect(const char *body, const char *end, int s)
{
    int at = tu.nbars, n, k;
    if (at >= MAXBARS)
        return;
    n = parse(body, tu.f, at, tu.key, s);
    if (end && at + n < MAXBARS)
        n += parse(end, tu.f, at + n, tu.key, s);
    for (k = 0; k < n; k++) {
        tu.f[at + k].sbar = (uint8_t)k;
        tu.f[at + k].sect = (uint8_t)s;
    }
    tu.nbars += n;
}
static int starts_iv(const char *s) { return s[0] == '4' && s[1] == '^'; }

static void build_form(void)
{
    int k = tu.kind, r;
    tu.nbars = 0;
    tu.bpb = k == K_WALTZ ? 3 : 4;
    switch (k) {
    case K_BLUES:
        add_sect(BLUES[tirand(NA(BLUES))], 0, 0);
        break;
    case K_BIRD:
        add_sect(BIRD, 0, 0);
        break;
    case K_MINOR:
        add_sect(MBLUES[tirand(NA(MBLUES))], 0, 0);
        break;
    case K_RHYTHM:
        r = tirand(NA(RHY_A));
        add_sect(RHY_A[r], RHY_TURN, 0);
        add_sect(RHY_A[r], RHY_CLOSE, 0);
        add_sect(RHY_B[tirand(NA(RHY_B))], 0, 1);
        add_sect(RHY_A[r], RHY_TURN, 0);
        break;
    case K_MODAL:
        r = tirand(NA(MOD_B));
        add_sect(MOD_A, 0, 0);
        add_sect(MOD_A, 0, 0);
        add_sect(MOD_B[r], 0, 1);
        add_sect(MOD_A, 0, 0);
        break;
    case K_SUS:
        add_sect(SUS16, 0, 0);
        break;
    case K_WALTZ: {
        const char *const *a = W_A[tirand(NA(W_A))];
        add_sect(a[0], a[1], 0);
        add_sect(a[0], CLOSE_MAJ, 0);
        add_sect(W_B[tirand(NA(W_B))], 0, 1);
        add_sect(a[0], a[1], 0);
        break;
    }
    default:                                     /* AABA songs: medium, up, ballads; major or minor */
        if (tu.minor) {
            const char *a = A_MIN[tirand(NA(A_MIN))], *t = TURN_MIN[tirand(NA(TURN_MIN))];
            add_sect(a, t, 0);
            add_sect(a, CLOSE_MIN, 0);
            add_sect(B_MIN[tirand(NA(B_MIN))], 0, 1);
            add_sect(a, t, 0);
        } else {
            const char *a = A_MAJ[tirand(NA(A_MAJ))], *t = TURN_MAJ[tirand(NA(TURN_MAJ))];
            const char *b = B_MAJ[tirand(NA(B_MAJ))];
            add_sect(a, t, 0);
            add_sect(a, starts_iv(b) && trand() < 0.6f ? "1^ | 5- 17" : CLOSE_MAJ, 0);
            add_sect(b, 0, 1);
            add_sect(a, t, 0);
        }
        break;
    }
}

/* reharmonization, once a tune (so every chorus is the same): tritone subs for the short dominants that resolve
 * down a fifth, a ii before a long one, altered dominants */
static void reharm(float h)
{
    int i, j, blues = tu.kind == K_BLUES || tu.kind == K_BIRD || tu.kind == K_MINOR;
    float p_tri = 0.08f + 0.6f * h, p_ii = 0.05f + 0.45f * h, p_alt = 0.1f + 0.5f * h;
    if (tu.kind == K_MODAL || tu.kind == K_SUS)
        return;
    for (i = 0; i < tu.nbars; i++) {
        fbar_t *b = &tu.f[i];
        for (j = 0; j < b->n; j++) {
            int q = b->q[j], r = b->root[j], nr, nq;
            if (j + 1 < b->n)
                nr = b->root[j + 1], nq = b->q[j + 1];
            else
                nr = tu.f[(i + 1) % tu.nbars].root[0], nq = tu.f[(i + 1) % tu.nbars].q[0];
            if (!(q == Q_DOM || q == Q_B9) || (r + 5) % 12 != nr)
                continue;
            if (b->n == 2) {                     /* a two-beat dominant: bII7 for V7, or altered */
                float x = trand();
                if (x < p_tri) {
                    b->root[j] = (int8_t)((r + 6) % 12);
                    b->q[j] = Q_DOM;
                } else if (q == Q_DOM && x < p_tri + p_alt * 0.5f && !q_is_minor(nq)) {
                    b->q[j] = Q_ALT;
                }
            } else if (!blues && tu.bpb == 4 && trand() < p_ii) {   /* a bar of V7: its ii first */
                b->n = 2;
                b->root[1] = (int8_t)r;
                b->q[1] = (uint8_t)q;
                b->root[0] = (int8_t)((r + 7) % 12);
                b->q[0] = Q_M7;
            } else if (q == Q_DOM && trand() < p_alt * 0.4f && !q_is_dom(nq)) {
                b->q[j] = Q_ALT;
            }
        }
    }
}

/* ------------------------------------------------------------- melodies --- */
/* the section's first bar in the form */
static int sect_bar0(int s)
{
    int i;
    for (i = 0; i < tu.nbars; i++)
        if (tu.f[i].sect == s && tu.f[i].sbar == 0)
            return i;
    return 0;
}
static int sect_len(int s)
{
    int i = sect_bar0(s), n = 0;
    while (i + n < tu.nbars && tu.f[i + n].sect == s && (n == 0 || tu.f[i + n].sbar != 0))
        n++;
    return n;
}

/* rhythm cells for one bar (ticks: twelfths of a beat): a motif, an answer, a cadence */
typedef struct {
    int8_t n, t[6], d[6];
} cell_t;
static const cell_t MOTIF4[] = {
    {4, {0, 12, 18, 24}, {12, 6, 6, 24}},        /* quarter, two eighths, half */
    {5, {6, 12, 18, 24, 36}, {6, 6, 6, 12, 12}}, /* from the "and" of one */
    {4, {0, 18, 24, 42}, {18, 6, 18, 6}},        /* Charleston twice */
    {4, {0, 6, 12, 18}, {6, 6, 6, 30}},          /* four eighths, a long one */
    {4, {12, 18, 24, 30}, {6, 6, 6, 18}},        /* from beat two */
    {3, {6, 18, 30}, {12, 12, 18}},              /* on the off-beats */
    {3, {0, 24, 36}, {24, 12, 12}},              /* half, two quarters */
};
static const cell_t MOTIF_SLOW[] = {
    {3, {0, 24, 36}, {24, 12, 12}},
    {3, {0, 18, 24}, {18, 6, 24}},
    {2, {0, 36}, {36, 12}},
    {4, {0, 12, 24, 36}, {12, 12, 12, 12}},
    {3, {12, 24, 36}, {12, 12, 12}},
};
static const cell_t ANSWER4[] = {
    {1, {0}, {36}}, {2, {0, 24}, {24, 12}}, {1, {0}, {44}}, {2, {0, 12}, {12, 30}}, {2, {6, 24}, {18, 18}},
};
static const cell_t MOTIF3[] = {
    {1, {0}, {36}}, {2, {0, 12}, {12, 24}}, {2, {0, 24}, {24, 12}}, {3, {0, 12, 24}, {12, 12, 12}},
    {3, {0, 18, 24}, {18, 6, 12}},
};
static const int8_t CONTOUR[][6] = {
    {0, 1, 1, -2, -1, 1}, {0, 2, -1, -1, 1, -1}, {0, -1, -1, 3, -1, -1}, {0, 3, -1, -1, -1, 2},
    {0, 1, -1, 2, -1, -1}, {0, -2, 1, 2, -1, 1}, {0, 2, 1, -1, -2, 1},
};

static const fbar_t *mbar;                       /* the melody's bar */
static int mel_pitch(int prev, int steps, int t, int dur, int strong)
{
    uint16_t ms = m_strong(mbar, t), sc = m_scale(mbar, t);
    int target = prev + steps * 2, n;
    if (strong || dur >= 18)
        n = near_in(target, ms);
    else
        n = near_in(target, sc);
    if (n == prev && steps)
        n = step_in(prev, steps > 0 ? 1 : -1, strong || dur >= 18 ? ms : sc);
    while (n > 79)
        n -= 12;
    while (n < 60)
        n += 12;
    return n;
}
/* a cell's notes on bar sb of the section, from pitch p along contour c; returns the last pitch */
static int put_cell(const cell_t *c, const int8_t *ct, int sb, int p, float vel, int first)
{
    int i, bt = tu.bpb * 12;
    for (i = 0; i < c->n; i++) {
        int t = c->t[i], strong = t % 12 == 0 && (t % 24 == 0 || c->d[i] >= 12);
        int n = (i == 0 && first > 0) ? first : mel_pitch(p, i ? ct[i % 6] : 0, t, c->d[i], strong);
        if (t + c->d[i] > bt + 12)
            continue;
        emit(I_RH, sb * bt + t, c->d[i], n, vel * (strong ? 1.0f : 0.92f), 0);
        p = n;
    }
    return p;
}
/* the same chord-relative note on another chord: a motif sequenced */
static int seq_note(int note, const fbar_t *from, const fbar_t *to)
{
    int iv = ((note - ch_root(from, 0)) % 12 + 12) % 12;
    int n = note - iv + (((ch_root(to, 0) + iv) - (note - iv)) % 12 + 12) % 12;
    n = near_in(n, m_strong(to, 0)) ;
    if (n - note > 6)
        n -= 12;
    if (note - n > 6)
        n += 12;
    return n;
}

static void mel_begin(int s)
{
    em_mel = tu.mel[s];
    em_n = &tu.nmel[s];
    tu.nmel[s] = 0;
    em_base = 0;
}
static void mel_end(void) { em_mel = 0; }

/* a lyrical head: a motif and its answer, the motif sequenced on the next chords, varied, a run, the cadence */
static void mel_lyric(int s, int bridge)
{
    int b0 = sect_bar0(s), len = sect_len(s), bt = tu.bpb * 12, slow = tu.kind == K_BALLAD, sb, p, first, k;
    const cell_t *mc, *ac;
    const int8_t *ct = CONTOUR[tirand(NA(CONTOUR))];
    float v = 0.72f;
    if (tu.bpb == 3) {
        mc = &MOTIF3[tirand(NA(MOTIF3))];
        ac = &MOTIF3[tirand(2)];
    } else {
        mc = slow ? &MOTIF_SLOW[tirand(NA(MOTIF_SLOW))] : &MOTIF4[tirand(NA(MOTIF4))];
        ac = &ANSWER4[tirand(NA(ANSWER4))];
    }
    if (bridge && tu.bpb == 4)                   /* the bridge: longer values */
        mc = &MOTIF_SLOW[tirand(NA(MOTIF_SLOW))];
    mel_begin(s);
    mbar = &tu.f[b0];
    first = near_in(bridge ? 71 + tirand(4) : 67 + tirand(6), m_strong(mbar, mc->t[0]));
    p = first;
    for (sb = 0; sb < len; sb++) {
        int ph = sb % 8, top = len >= 16 && sb >= 8 ? 2 : 0;
        em_base = 0;
        mbar = &tu.f[b0 + sb];
        if (ph == 0 || ph == 2 || ph == 4) {     /* the motif: sequenced onto this bar's chord */
            int f = sb == 0 ? first : seq_note(first, &tu.f[b0], mbar);
            if (ph == 4)
                f = step_in(f, 1, m_strong(mbar, 0));   /* varied: a step higher */
            if (bridge && ph == 4)
                f = step_in(f, 1, m_strong(mbar, 0));
            f += top;
            f = near_in(f, m_strong(mbar, mc->t[0]));
            p = put_cell(mc, ct, sb, p, v * (ph == 4 ? 1.05f : 1.0f), f);
        } else if (ph == 1 || ph == 3) {         /* the answer: long notes, settling */
            p = put_cell(ac, CONTOUR[0], sb, p, v * 0.95f, near_in(p - 2, m_ct(mbar, ac->t[0])));
        } else if (ph == 5) {                    /* a run of eighths to the cadence */
            const fbar_t *nb = &tu.f[b0 + (sb + 1 < len ? sb + 1 : sb)];
            int tgt = near_in(p, rot(M(Q_3RD[ch_q(nb, 0)]) | M(7) | M(0), ch_root(nb, 0)));
            int dir = tgt >= p ? 1 : -1, t;
            for (t = (tu.bpb == 3 ? 0 : 12); t < bt; t += 6) {
                int n = step_in(p, dir, m_bebop(mbar, t));
                if (n == tgt && t < bt - 6)
                    n = step_in(n, dir, m_bebop(mbar, t));
                if (n > 79 || n < 62) {          /* the run turns at the melody's edges */
                    dir = -dir;
                    n = step_in(p, dir, m_bebop(mbar, t));
                }
                if (t % 12 == 0 && !in_mask(n, m_strong(mbar, t)))   /* the beats: the chord's tones */
                    n = step_in(n, dir, m_strong(mbar, t));
                emit(I_RH, sb * bt + t, 6, n, v * (t % 12 ? 0.98f : 0.9f), 0);
                p = n;
            }
        } else if (ph == 6) {                    /* the cadence: a held chord tone */
            int n = near_in(p, rot(M(Q_3RD[ch_q(mbar, 0)]) | M(7) | M(0) | M(2), ch_root(mbar, 0)));
            int d = bridge ? bt : bt + bt / 2;
            emit(I_RH, sb * bt, d, n, v, 0);
            p = n;
        } else if (!bridge && sb == len - 1 && trand() < 0.7f) {   /* a pickup into the next A */
            int f = first;
            for (k = 0; k < 3; k++) {
                int n = k == 2 ? f - 1 : k == 1 ? step_in(f, 1, m_scale(&tu.f[b0], 0)) : step_in(f, -1, m_scale(&tu.f[b0], 0));
                emit(I_RH, sb * bt + bt - 18 + 6 * k, 6, n, v * 0.85f, FL_PICK);
            }
        } else if (bridge && ph == 7) {          /* the bridge's last bar: a held tone on its dominant */
            int n = near_in(p - 1, m_strong(mbar, 0));
            emit(I_RH, sb * bt, bt - 12, n, v * 0.9f, 0);
            p = n;
        }
    }
    mel_end();
}

/* a bebop head: the line player, frozen; phrases of two bars, the cadence at bar seven */
static void mel_bebop(int s)
{
    int b0 = sect_bar0(s), len = sect_len(s), sb;
    line_t L;
    memset_l(&L, sizeof L);
    L.lo = 62;
    L.hi = 82;
    L.p = 70;
    L.dens = 0.85f;
    L.trip = 0.1f;
    L.arp = 0.12f;
    L.quarter = 0.04f;
    L.vel = 0.74f;
    L.inst = I_RH;
    L.pend = -1;
    L.left = 0;
    L.rest = 6;
    mel_begin(s);
    for (sb = 0; sb < len; sb++) {
        int ph = sb % 8;
        em_base = sb * tu.bpb * 12;
        if (ph == 6 || ph == 7) {                /* the cadence: held, then space */
            if (ph == 6) {
                mbar = &tu.f[b0 + sb];
                emit(I_RH, 0, 30, near_in(L.p, rot(M(Q_3RD[ch_q(mbar, 0)]) | M(7) | M(0), ch_root(mbar, 0))), 0.76f, 0);
            }
            L.left = 0;
            L.rest = ph == 7 ? 6 : 48;
            continue;
        }
        if (ph == 0 || ph == 2 || ph == 4) {     /* a phrase starts with the bar */
            L.left = 0;
            L.rest = ph == 0 ? 6 : 0;
        }
        if (ph == 1 || ph == 3)
            L.left = L.left > 24 ? 24 : L.left;  /* ...and ends inside the next */
        line_bar(&L, &tu.f[b0 + sb], &tu.f[b0 + (sb + 1) % len], tu.bpb);
    }
    mel_end();
}

/* a riff head for the blues: a riff from the blues scale, again over IV, an answer, the cadence */
static const cell_t RIFF[] = {
    {5, {6, 12, 18, 30, 36}, {6, 6, 12, 6, 12}},
    {5, {0, 6, 12, 24, 30}, {6, 6, 12, 6, 18}},
    {4, {6, 18, 24, 30}, {12, 6, 6, 18}},
    {4, {0, 18, 24, 36}, {12, 6, 12, 12}},
};
static const int8_t RIFF_DEG[][5] = {{4, 5, 4, 2, 0}, {0, 1, 2, 1, 0}, {2, 4, 5, 4, 3}, {5, 4, 2, 1, 0}, {1, 2, 3, 4, 1}};
static void mel_riff(void)
{
    static const int8_t BS[8] = {0, 3, 5, 6, 7, 10, 12, 15};   /* the blues scale, two octaves' start */
    const cell_t *c = &RIFF[tirand(NA(RIFF))];
    const int8_t *dg = RIFF_DEG[tirand(NA(RIFF_DEG))];
    int base = 60 + tu.key, i, sb, p = 0, last = 0;
    float v = 0.76f;
    if (base < 62)
        base += 12;
    if (base > 70)
        base -= 12;
    mel_begin(0);
    for (sb = 0; sb < tu.nbars; sb++) {
        mbar = &tu.f[sb];
        em_base = sb * 48;
        if (sb == 0 || sb == 4) {                /* the riff (over IV too: a strong note that clashes moves) */
            for (i = 0; i < c->n; i++) {
                int n = base + BS[dg[i]], t = c->t[i];
                uint16_t bad = rot(M(11) | (q_is_dom(ch_q(mbar, t)) ? M(5) : 0), ch_root(mbar, t));
                if ((t % 12 == 0 || c->d[i] >= 12) && in_mask(n, bad))
                    n = near_in(n - 1, m_strong(mbar, t));
                emit(I_RH, t, c->d[i], n, v * (t % 12 ? 1.0f : 0.92f), 0);
                last = n;
            }
        } else if (sb == 1 || sb == 5) {         /* the riff's tail, held */
            int n = near_in(last, rot(M(0) | M(3) | M(7) | M(10), tu.key));
            if (!in_mask(n, m_strong(mbar, 0)) || in_mask(n, rot(M(11), ch_root(mbar, 0))))
                n = near_in(n, m_strong(mbar, 0));
            emit(I_RH, 0, 30, n, v, 0);
            p = n;
        } else if (sb == 8 || sb == 9) {         /* the answer: the motif's cells on the turn of the blues */
            const cell_t *mc = &MOTIF4[sb == 8 ? 3 : 0];
            em_base = sb * 48;
            p = put_cell(mc, CONTOUR[sb == 8 ? 2 : 1], 0, sb == 8 ? base + 7 : p, v, sb == 8 ? near_in(base + 7, m_strong(mbar, 0)) : 0);
        } else if (sb == 10) {                   /* home: the tonic's third or root, held */
            int n = near_in(p, rot(M(0) | M(4) | M(3) | M(7), tu.key));
            n = near_in(n, m_strong(mbar, 0));
            emit(I_RH, 0, 54, n, v, 0);
        }
    }
    (void)p;
    mel_end();
}

/* a modal head: long tones from the mode's pentatonic, a phrase and its answer */
static void mel_modal(int s)
{
    static const cell_t MC[] = {{2, {0, 36}, {36, 12}}, {2, {12, 24}, {12, 24}}, {3, {0, 24, 36}, {24, 12, 12}},
                                {1, {0}, {48}}, {2, {6, 24}, {18, 24}}};
    int b0 = sect_bar0(s), len = sect_len(s), sb, p, i;
    float v = 0.72f;
    const int8_t *ct = CONTOUR[tirand(NA(CONTOUR))];
    int a = tirand(NA(MC)), b = tirand(NA(MC));
    mel_begin(s);
    mbar = &tu.f[b0];
    p = near_in(68 + tirand(5), m_strong(mbar, 0));
    for (sb = 0; sb < len; sb++) {
        const cell_t *c = sb % 4 == 3 ? 0 : &MC[(sb % 4 == 1) ? b : (sb % 4 == 2) ? 3 : a];
        uint16_t pent;
        mbar = &tu.f[b0 + sb];
        pent = rot(q_is_minor(ch_q(mbar, 0)) ? (M(0) | M(3) | M(5) | M(7) | M(10) | M(2))
                                             : (M(0) | M(2) | M(7) | M(9) | M(4)), ch_root(mbar, 0));
        if (!c)
            continue;
        for (i = 0; i < c->n; i++) {
            int n = near_in(p + ct[(sb + i) % 6] * 3, pent);
            if (n == p)
                n = step_in(p, (sb & 4) ? -1 : 1, pent);
            while (n > 80)
                n -= 12;
            while (n < 60)
                n += 12;
            emit(I_RH, sb * 48 + c->t[i], c->d[i], n, v, 0);
            p = n;
        }
    }
    mel_end();
}

static void make_melodies(void)
{
    tu.nmel[0] = tu.nmel[1] = 0;
    switch (tu.mk) {
    case MK_RIFF: mel_riff(); break;
    case MK_MODAL:
        mel_modal(0);
        if (tu.kind == K_MODAL)
            mel_modal(1);
        break;
    case MK_BEBOP:
        mel_bebop(0);
        if (tu.kind == K_RHYTHM)
            mel_lyric(1, 1);
        else
            mel_bebop(1);
        break;
    default:
        mel_lyric(0, 0);
        if (tu.nbars > sect_len(0))
            mel_lyric(1, 1);
        break;
    }
}

/* --------------------------------------------------------- the timeline --- */
static void add(int role, int fbar, int ch, int nch, int fl, int sub)
{
    rec_t *r;
    if (tu.nrec >= NREC)
        return;
    r = &tu.rec[tu.nrec++];
    r->role = (uint8_t)role;
    r->fbar = (uint8_t)fbar;
    r->chorus = (uint8_t)ch;
    r->nch = (uint8_t)nch;
    r->fl = (uint8_t)fl;
    r->sub = (uint8_t)sub;
}
static int send_flag(int i)                      /* the end of a section (a phrase of eight, or four in the blues) */
{
    const fbar_t *b = &tu.f[i];
    if (i == tu.nbars - 1)
        return BF_SEND | BF_FORMEND;
    if (tu.nbars == 12)
        return (i % 4 == 3) ? BF_SEND : 0;
    return tu.f[i + 1].sbar == 0 || b->sbar % 8 == 7 ? BF_SEND : 0;
}
static void add_bars(int role, int nch, int two, int b0, int b1)   /* nch times through bars b0..b1-1 */
{
    int c, i, sub = 0;
    for (c = 0; c < nch; c++)
        for (i = b0; i < b1; i++) {
            int fl = send_flag(i) | (c == nch - 1 ? BF_LASTCH : 0) | (sub == 0 ? BF_PSTART : 0);
            int r = role;
            if (two && (tu.f[i].sect == 0 || tu.kind == K_BALLAD))
                fl |= BF_TWO;
            if (role == R_TRADE_P && (sub / 4) % 2 == 1)
                r = R_TRADE_D;
            add(r, i, c, nch, fl, sub);
            sub++;
        }
}
static void add_choruses(int role, int nch, int two) { add_bars(role, nch, two, 0, tu.nbars); }

static void arrange(void)
{
    int k = tu.kind, npc, nbc = 0, ntc = 0, i, nhead = tu.nbars <= 16 ? 2 : 1, cap;
    float chorus_s = (float)(tu.nbars * tu.bpb) * 60.0f / tu.bpm, focus = pf(P_FOCUS), target;
    static const float STRETCH_S[4] = {40.0f, 75.0f, 115.0f, 170.0f};
    target = STRETCH_S[par[P_STRETCH] < 0 ? 0 : par[P_STRETCH] > 3 ? 3 : par[P_STRETCH]] * (0.8f + 0.4f * trand());
    if (k == K_BALLAD)
        target *= 0.6f;
    npc = (int)(target / chorus_s + 0.5f);
    npc = npc < 1 ? 1 : npc > 10 ? 10 : npc;
    if (k == K_BALLAD && npc > 2)
        npc = 2;
    if (k != K_UP && k != K_RHYTHM && k != K_BIRD && trand() < (0.08f + 0.55f * focus) * (k == K_BALLAD ? 0.5f : 1.0f))
        nbc = chorus_s < 22.0f ? 2 : 1;
    if (k != K_BALLAD && k != K_WALTZ && k != K_SUS && trand() < (0.04f + 0.62f * focus) * (tu.bpm > 140.0f ? 1.0f : 0.45f))
        ntc = tu.nbars == 12 ? 2 : 1;
    cap = (NREC - 16) / tu.nbars - 2 * nhead - nbc - ntc;   /* the timeline has room for so many */
    if (npc > cap)
        npc = cap < 1 ? 1 : cap;
    tu.nrec = 0;
    /* the way in */
    switch (tu.intro) {
    case IN_COUNT:
        add(R_COUNT, 0, 0, 1, BF_PSTART, 0);
        add(R_COUNT, 0, 0, 1, 0, 1);
        break;
    case IN_PIANO:
        for (i = 0; i < 4; i++)
            add(R_INTRO, tu.nbars - 4 + i, 0, 1, (i == 0 ? BF_PSTART : 0) | (i == 3 ? BF_SEND : 0), i);
        break;
    case IN_BASS:
        for (i = 0; i < 4; i++)
            add(R_INTRO, 0, 0, 1, (i == 0 ? BF_PSTART : 0) | (i == 3 ? BF_SEND : 0), i);
        break;
    default:
        for (i = 0; i < 4; i++)
            add(R_INTRO, i & 1, 0, 1, (i == 0 ? BF_PSTART : 0) | (i == 3 ? BF_SEND : 0), i);
        break;
    }
    add_choruses(R_HEAD, nhead, tu.two_head);
    tu.solo_rec = tu.nrec;
    if (k == K_BALLAD && tu.nbars == 32 && chorus_s > 95.0f && trand() < 0.85f) {
        /* a slow ballad: a solo on the first half, the head back in at the bridge */
        add_bars(R_PSOLO, 1, 0, 0, 16);
        tu.out_rec = tu.nrec;
        add_bars(R_HEADOUT, 1, tu.two_head, 16, 32);
        tu.rec[tu.nrec - 1].fl |= BF_RIT;
        tu.rec[tu.nrec - 2].fl |= BF_RIT;
        add(R_FINAL, tu.nbars - 1, 0, 1, BF_PSTART, 0);
        return;
    }
    add_choruses(R_PSOLO, npc, 0);
    if (nbc)
        add_choruses(R_BSOLO, nbc, k == K_BALLAD || k == K_WALTZ);
    if (ntc)
        add_choruses(R_TRADE_P, ntc, 0);
    tu.out_rec = tu.nrec;
    add_choruses(R_HEADOUT, nhead, tu.two_head && k == K_BALLAD);
    /* the way out */
    switch (tu.ending) {
    case E_TAG:
        for (i = 0; i < 4; i++)
            add(R_TAG, tu.nbars - 2 + (i & 1), 0, 1, (i == 0 ? BF_PSTART : 0) | (i & 1 ? BF_SEND : 0), i);
        break;
    case E_RIT:
        tu.rec[tu.nrec - 1].fl |= BF_RIT;
        tu.rec[tu.nrec - 2].fl |= BF_RIT;
        break;
    default: break;
    }
    add(R_FINAL, tu.nbars - 1, 0, 1, BF_PSTART | (tu.ending == E_BASIE ? BF_BASIE : 0), 0);
}

/* ------------------------------------------------------------- the set --- */
/* the next tune: a kind the band's book favours, not the last one again, and a contrast in tempo */
static int pick_kind(void)
{
    const style_t *st = &STYLES[style_i];
    float w[K_NKINDS], tot = 0.0f, r;
    int i;
    for (i = 0; i < K_NKINDS; i++) {
        float mid = 0.5f * (float)(KINDS[i].lo + KINDS[i].hi), c;
        w[i] = (float)st->w[i];
        if (tu.last_bpm > 0.0f) {
            c = fm_fabsf(fm_log2f(mid / tu.last_bpm));
            w[i] *= c > 0.3f ? 1.6f : c > 0.15f ? 1.0f : 0.45f;
        }
        if (i == tu.prev_kind[0] || (i == K_BALLAD && tu.prev_kind[0] == K_BALLAD))
            w[i] = 0.0f;
        else if (i == tu.prev_kind[1])
            w[i] *= 0.4f;
        tot += w[i];
    }
    if (tot <= 0.0f)
        return K_MEDIUM;
    r = trand() * tot;
    for (i = 0; i < K_NKINDS; i++) {
        r -= w[i];
        if (r <= 0.0f && w[i] > 0.0f)
            return i;
    }
    return K_MEDIUM;
}

static void new_tune(void)
{
    static const int8_t KEYS_MAJ[8] = {5, 10, 3, 0, 8, 7, 1, 2};   /* F Bb Eb C Ab G Db D */
    static const int8_t KEYS_MIN[7] = {0, 2, 5, 7, 10, 9, 4};      /* C D F G Bb A E */
    const style_t *st = &STYLES[style_i];
    uint32_t seed;
    int k, br = st->brush;
    en_tune_no++;
    seed = en_tune_rand(en_seed_base, en_tune_no);
    en_tune_seed = seed;
    trng = seed;
    k = pick_kind();
    tu.kind = k;
    tu.prev_kind[2] = tu.prev_kind[1];
    tu.prev_kind[1] = tu.prev_kind[0];
    tu.prev_kind[0] = k;
    tu.minor = k == K_MINOR || k == K_MODAL || ((k == K_MEDIUM || k == K_BALLAD || k == K_UP) && trand() < 0.3f);
    tu.key = tu.minor ? KEYS_MIN[tirand(k == K_MODAL ? 3 : 7)] : KEYS_MAJ[tirand(k == K_BALLAD ? 8 : 6)];
    tu.bpm = ((float)KINDS[k].lo + (float)(KINDS[k].hi - KINDS[k].lo) * trand()) * (float)st->tempo * 0.01f;
    tu.last_bpm = tu.bpm;
    build_form();
    reharm((float)st->harm * 0.01f);
    tu.fin_root = tu.key;
    tu.fin_q = k == K_SUS ? Q_SUS : tu.minor ? (k == K_MODAL ? Q_M7 : Q_M6) : Q_MAJ6;
    tu.two_head = (uint8_t)(k == K_BALLAD || (k == K_MEDIUM && trand() < 0.55f) || (k == K_BLUES && trand() < 0.3f) ||
                            k == K_WALTZ);
    tu.brush_head = (uint8_t)(br == 3 || (br >= 1 && k == K_BALLAD) || (br == 2) || (br == 1 && k == K_WALTZ));
    tu.brush_solo = (uint8_t)(br == 3 || (br >= 1 && k == K_BALLAD));
    tu.intro = (uint8_t)(k == K_BALLAD ? IN_PIANO
                         : k == K_MODAL || k == K_SUS ? IN_BASS
                         : k == K_WALTZ ? IN_VAMP
                         : (k == K_MEDIUM && trand() < 0.3f) ? IN_PIANO : IN_COUNT);
    tu.ending = (uint8_t)(k == K_BALLAD ? E_RIT
                          : k == K_WALTZ ? (trand() < 0.5f ? E_RIT : E_TAG)
                          : k == K_MODAL || k == K_SUS ? E_HOLD
                          : (k == K_BLUES || k == K_BIRD || k == K_MINOR) && trand() < 0.5f ? E_BASIE
                          : trand() < 0.75f ? E_TAG : E_HOLD);
    tu.mk = (uint8_t)(k == K_BLUES || k == K_MINOR ? MK_RIFF
                      : k == K_BIRD || k == K_RHYTHM || (k == K_UP && trand() < 0.6f) ? MK_BEBOP
                      : k == K_MODAL || k == K_SUS ? MK_MODAL : MK_LYRIC);
    make_melodies();
    arrange();
    en_kind = (uint8_t)k;
    en_bpm = (uint16_t)(tu.bpm * fm_exp2f((pf(P_TEMPO) - 0.5f) * 0.83f) + 0.5f);
    en_key = (uint8_t)tu.key;
    en_minor = (uint8_t)tu.minor;
    en_form_bars = (uint8_t)tu.nbars;
    en_bpb = (uint8_t)tu.bpb;
    play_reset();
    en_phase = PH_TUNE;
    skipping = 0;
    fade = 1.0f;
    fade_t = 1.0f;
    en_tunes_played++;
}
