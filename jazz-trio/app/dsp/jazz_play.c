/* SPDX-License-Identifier: MPL-2.0
 * JAZZ TRIO: ashtonwallen, 2026 */
/* The players (part of jazz.c): each bar of the arrangement planned for the drums, the bass and the piano's two
 * hands, then played beat by beat with the swing the tempo asks for; applause and the pause between tunes. */

/* the bar's plan: its notes, in ticks (twelfths of a beat) from the bar's start */
typedef struct {
    int16_t tick, dur;
    uint8_t inst, note, vel, fl;
} pn_t;
#define NPLAN 128
static pn_t pl[NPLAN];
static int npl;
static int rec_i, beat_i;
static float swing_r = 0.62f, energy = 0.5f;
static line_t Lp, Lb;                             /* the piano's right hand, the bass's solo */
static int lh[5], nlh, lh_root;                   /* the left hand's last voicing, its chord's root */
static int bass_last = 40, bass_aim = -1, quart_up, fill_prev;
static fbar_t fin_bar;
static float appl_small;

static void plan_note(int inst, int tick, int dur, int note, int vel, int fl)
{
    pn_t *p;
    if (npl >= NPLAN)
        return;
    p = &pl[npl++];
    p->tick = (int16_t)tick;
    p->dur = (int16_t)dur;
    p->inst = (uint8_t)inst;
    p->note = (uint8_t)note;
    p->vel = (uint8_t)vel;
    p->fl = (uint8_t)fl;
#ifdef EN_HOST
    if (en_trace)
        en_trace(inst, rec_i, tu.rec[rec_i].role, tu.rec[rec_i].fbar, tick, dur, note, vel);
#endif
}
static void pn(int inst, int tick, int dur, int note, float vel, int fl)
{
    int v = (int)(vel * 127.0f);
    plan_note(inst, tick, dur, note, v < 2 ? 2 : v > 127 ? 127 : v, fl);
}

static const rec_t *nxt(int a)
{
    int i = rec_i + a;
    if (en_repeat_on && i >= tu.out_rec && rec_i < tu.out_rec)   /* (blowing on: the solos again) */
        i = tu.solo_rec + (i - tu.out_rec);
    return &tu.rec[i < tu.nrec ? i : tu.nrec - 1];
}
static const fbar_t *rbar(const rec_t *r) { return r->role == R_FINAL ? &fin_bar : &tu.f[r->fbar]; }

/* ---------------------------------------------------------------- drums --- */
/* comping figures: a bar on the triplet grid (three a beat): S snare, s a ghost, K the bass drum */
static const char *const DCOMP[] = {
    "..........s.", ".....S......", "..s.....s...", "........S..K", ".....s.....S", "..S.....K...",
    "........s.S.", "K.........S.", "..s..s..s...", ".S.........s", "...........S", "..K.....s...",
};
/* fills: the bar's last beat (or two) into the next section */
static const char *const DFILL1[] = {"SsS", "S.S", "..S", "SSS", "s.S"};
static const char *const DFILL2[] = {"..SS.S", "S.sS.S", "SSSTTT", "..S.TF", "S.SF.F"};
/* breaks (trading fours): openers, the middle, the build, the last bar into the downbeat */
static const char *const BRK[4][4] = {
    {"S....SK..S..", "K.S.S.K.S...", "..S..S..T..F", "C.....S.S.SS"},
    {"S.sS.sS.sS..", "S.TS.FS.TK.F", "SsSsS.F.FK..", "K.SK.SK.ST.T"},
    {"S.TS.FS.TK.F", "..S..S..T..F", "S..S..S.SSSS", "S.sS.sS.sS.s"},
    {"SSSTTTFFFK..", "S..S..S.SSSS", "F.FT.TS.SSSS", "SsSTtTFfFK.."},
};
static void dchar(char c, int tick, float v)
{
    switch (c) {
    case 'S': pn(I_SNARE, tick, 2, 0, v * 0.85f, FL_RAW); break;
    case 's': pn(I_SNARE, tick, 2, 0, v * 0.32f, FL_RAW); break;
    case 'K': pn(I_KICK, tick, 2, 0, v * 0.7f, FL_RAW); break;
    case 'T': pn(I_TOM, tick, 2, 0, v * 0.8f, FL_RAW); break;
    case 't': pn(I_TOM, tick, 2, 0, v * 0.4f, FL_RAW); break;
    case 'F': pn(I_TOM, tick, 2, 1, v * 0.85f, FL_RAW); break;
    case 'f': pn(I_TOM, tick, 2, 1, v * 0.42f, FL_RAW); break;
    case 'C':
        pn(I_CRASH, tick, 2, 0, v * 0.7f, FL_RAW);
        pn(I_KICK, tick, 2, 0, v * 0.75f, FL_RAW);
        break;
    default: break;
    }
}

static void gen_drums(const rec_t *r, const fbar_t *b)
{
    int bpb = tu.bpb, i, role = r->role, brush, two = (r->fl & BF_TWO) != 0;
    float dv = 0.5f + 0.42f * fm_minf(energy, 1.1f), bpm = (float)en_bpm;
    (void)b;
    brush = (role == R_HEAD || role == R_HEADOUT || role == R_INTRO || role == R_TAG || role == R_FINAL)
                ? tu.brush_head
                : tu.brush_solo;
    if (role == R_BSOLO)
        brush = tu.brush_head | tu.brush_solo;
    en_brushes = (uint8_t)brush;
    sweep_lvl = 0.0f;
    if (role == R_COUNT) {                       /* the count: two bars, half time then the beats */
        for (i = 0; i < bpb; i++)
            if (r->sub == 1 || i % 2 == 0 || bpb == 3)
                pn(I_STICK, i * 12, 2, 0, 0.55f + (r->sub ? 0.1f : 0.0f), FL_RAW);
        return;
    }
    if (role == R_INTRO && (tu.intro == IN_PIANO || (tu.intro == IN_BASS && r->sub < 2)))
        return;
    if (role == R_TRADE_D) {                     /* a break: four bars, building, the hat on two and four */
        const char *s = BRK[r->sub % 4][tirand(4)];
        float v = 0.68f + 0.1f * (float)(r->sub % 4);
        for (i = 0; i < 12; i++)
            dchar(s[i], (i / 3) * 12 + (i % 3) * 4, v);
        pn(I_HAT, 12, 2, 0, 0.5f, 0);
        pn(I_HAT, 36, 2, 0, 0.5f, 0);
        fill_prev = 1;
        return;
    }
    if (role == R_FINAL) {                       /* the last chord: a crash, then the cymbal swells and fades */
        int at = (r->fl & BF_BASIE) ? 24 : 0;
        if (brush) {
            pn(I_BRUSH, at, 2, 0, 0.7f * dv, FL_RAW);
            pn(I_KICK, at, 2, 0, 0.35f * dv, FL_RAW);
            sweep_lvl = 0.3f * dv;
        } else {
            pn(I_CRASH, at, 2, 0, 0.75f * dv, FL_RAW);
            pn(I_KICK, at, 2, 0, 0.6f * dv, FL_RAW);
            for (i = at + 6; i < bpb * 12; i += 2) {
                float x = (float)(i - at) / (float)(bpb * 12 - at), sw = x < 0.45f ? x / 0.45f : (1.0f - x) / 0.55f;
                pn(I_RIDE, i, 2, 0, (0.10f + 0.28f * sw) * dv, FL_RAW);
            }
        }
        return;
    }
    /* time: the ride's spang-a-lang (or brushes), the hat's foot on two and four, the bass drum feathered */
    if (brush)
        sweep_lvl = (tu.kind == K_BALLAD ? 0.34f : 0.24f) * dv;
    for (i = 0; i < bpb; i++) {
        int t = i * 12, back = bpb == 4 ? (i & 1) : (i == 1);
        float rv = (back ? 0.92f : 0.76f) * dv * (two ? 0.86f : 1.0f);
        int skip = bpb == 4 ? (i & 1) : (i == 1);
        if (bpm > 250.0f)
            skip = skip && i == bpb - 1 && trand() < 0.5f;
        else if (!skip && energy > 0.75f && trand() < 0.12f)
            skip = 1;                            /* an extra skip, pushing */
        if (brush) {
            pn(I_BRUSH, t, 2, 0, rv * (back ? 0.55f : 0.32f), 0);
            if (skip && tu.kind != K_BALLAD)
                pn(I_BRUSH, t + 6, 2, 0, rv * 0.26f, 0);
        } else {
            pn(I_RIDE, t, 2, 0, rv * (tu.kind == K_BALLAD ? 0.7f : 1.0f), 0);
            if (skip)
                pn(I_RIDE, t + 6, 2, 0, rv * 0.62f, 0);
        }
        if (back || (bpb == 3 && i == 2 && trand() < 0.5f))
            pn(I_HAT, t, 2, 0, 0.55f * dv * (brush ? 0.8f : 1.0f), 0);
        if (!brush && bpm < 230.0f)
            pn(I_KICK, t, 2, 0, 0.15f * dv, 0);
        else if (brush && i == 0)
            pn(I_KICK, t, 2, 0, 0.1f * dv, 0);
    }
    /* a crash to open a part, the top of a chorus in the solos, after a fill */
    if (((r->fl & BF_PSTART) && role != R_INTRO && role != R_HEAD) || (role == R_HEAD && (r->fl & BF_PSTART) && tu.intro == IN_COUNT) ||
        (role == R_TRADE_P && r->sub % 4 == 0 && r->sub) || (role == R_PSOLO && r->fbar == 0 && trand() < 0.5f) ||
        (fill_prev && !two)) {
        if (brush)
            pn(I_BRUSH, 0, 2, 0, 0.62f * dv, FL_RAW);
        else
            pn(I_CRASH, 0, 2, 0, 0.55f * dv, FL_RAW);
        pn(I_KICK, 0, 2, 0, 0.5f * dv, FL_RAW);
    }
    fill_prev = 0;
    /* comping on the snare and the bass drum; fills into the next section */
    {
        float d = (0.12f + 0.62f * energy) * (role == R_HEAD || role == R_HEADOUT ? 0.55f : 1.0f) *
                  (role == R_BSOLO ? 0.3f : 1.0f) * (two ? 0.4f : 1.0f);
        int fill = (r->fl & BF_SEND) && trand() < 0.35f + 0.45f * energy && role != R_BSOLO && !two;
        int lim = fill ? (bpb - ((r->fl & BF_FORMEND) && energy > 0.6f && bpb == 4 ? 2 : 1)) * 3 : bpb * 3;
        if (trand() < d) {
            const char *s = DCOMP[tirand(NA(DCOMP))];
            for (i = 0; i < lim && i < 12; i++) {
                char c = s[i];
                if (brush && c == 's')
                    continue;
                if (brush && c == 'S')
                    pn(I_BRUSH, (i / 3) * 12 + (i % 3) * 4, 2, 0, 0.6f * dv, FL_RAW);
                else
                    dchar(c, (i / 3) * 12 + (i % 3) * 4, dv * 0.9f);
            }
        }
        if (fill) {
            const char *s = lim == (bpb - 2) * 3 ? DFILL2[tirand(NA(DFILL2))] : DFILL1[tirand(NA(DFILL1))];
            int n = lim == (bpb - 2) * 3 ? 6 : 3;
            for (i = 0; i < n; i++) {
                char c = s[i];
                if (brush)
                    c = c == 'S' ? 'S' : '.';
                if (brush && c == 'S')
                    pn(I_BRUSH, lim / 3 * 12 + (i / 3) * 12 + (i % 3) * 4, 2, 0, 0.55f * dv, FL_RAW);
                else
                    dchar(c, lim / 3 * 12 + (i / 3) * 12 + (i % 3) * 4, dv * (0.75f + 0.05f * (float)i));
            }
            fill_prev = 1;
        }
    }
}

/* ----------------------------------------------------------------- bass --- */
static int oct_near(int pc, int prev, int lo, int hi)   /* pitch class pc, the octave nearest prev, in lo..hi */
{
    int n = prev - ((prev - pc) % 12 + 12) % 12;
    if (prev - n > 6)
        n += 12;
    while (n < lo)
        n += 12;
    while (n > hi)
        n -= 12;
    return n;
}
/* a walking line: the chord's root (or another chord tone when the chord goes on) on its first beat, an approach
 * to the next chord on its last, scale or chord tones between, chosen for a smooth line */
static void walk_seg(int t0, int L, const fbar_t *b, int tc, int newc, int next_root, float vel)
{
    int root = ch_root(b, tc), q = ch_q(b, tc), n0, nl = 0, i, a, c;
    uint16_t sc = m_scale(b, tc), ct = m_ct(b, tc);
    int out[4];
    if (!newc && bass_aim >= 0)                  /* where the last bar's approach was going */
        n0 = oct_near(bass_aim, bass_last, 31, 52);
    else if (newc || trand() < 0.25f)
        n0 = oct_near(root, bass_last, 31, 50);
    else {                                       /* the chord goes on: its third or fifth */
        int pc = (root + (trand() < 0.5f ? Q_3RD[q] : 7)) % 12;
        n0 = oct_near(pc, bass_last, 31, 52);
        if (n0 == bass_last)
            n0 = oct_near(root, bass_last + 7, 31, 50);
    }
    out[0] = n0;
    if (L >= 2) {                                /* the approach to the next chord's root */
        int T = oct_near(next_root, out[0], 31, 50), cand[5], nc = 0, best = 0, bs = 999;
        cand[nc++] = T - 1;
        cand[nc++] = T + 1;
        cand[nc++] = step_in(T, -1, sc);
        cand[nc++] = step_in(T, 1, sc);
        cand[nc++] = T + 7 > 52 ? T - 5 : T + 7;
        for (i = 0; i < nc; i++) {
            int prev = L == 2 ? out[0] : out[0], d = cand[i] - prev, s = (d < 0 ? -d : d);
            s = s > 5 ? s * 2 : s;
            s += i < 2 ? 0 : i < 4 ? 2 : 4;       /* chromatic first, then the scale, then the fifth */
            s += tirand(4);
            if (cand[i] == prev || cand[i] < 28 || cand[i] > 55)
                s += 50;
            if (s < bs)
                bs = s, best = i;
        }
        nl = cand[best];
        out[L - 1] = nl;
    }
    if (L == 3 || L == 4) {                      /* the middle: scored for a smooth line, a chord tone on beat 3 */
        int bs = 999, b1 = n0, b2 = n0;
        for (a = -7; a <= 7; a++) {
            int m1 = n0 + a, s1;
            if (!in_mask(m1, sc) || m1 == n0 || m1 < 28 || m1 > 55)
                continue;
            s1 = a < 0 ? -a : a;
            s1 = s1 > 4 ? s1 + 3 : s1;
            if (L == 3) {
                int e = nl - m1, s = s1 + (e < 0 ? -e : e) + (m1 == nl ? 20 : 0) + tirand(3);
                if (s < bs)
                    bs = s, b1 = m1;
                continue;
            }
            for (c = -7; c <= 7; c++) {
                int m2 = m1 + c, s, e;
                if (!in_mask(m2, sc) || m2 == m1 || m2 < 28 || m2 > 55)
                    continue;
                e = nl - m2;
                s = s1 + (c < 0 ? -c : c) + (e < 0 ? -e : e) * 2 + (m2 == nl ? 20 : 0);
                if ((a > 0) != (c > 0))
                    s += 2;                      /* turning back */
                if (!in_mask(m2, ct))
                    s += 3;                      /* beat three: a chord tone */
                if (m2 == n0 || m1 == nl)
                    s += 7;                      /* not back and forth */
                s += tirand(3);
                if (s < bs)
                    bs = s, b1 = m1, b2 = m2;
            }
        }
        out[1] = b1;
        if (L == 4)
            out[2] = b2;
    }
    for (i = 0; i < L; i++) {
        pn(I_BASS, t0 + i * 12, 11, out[i], vel * (i == 0 ? 1.0f : 0.9f) * (0.94f + 0.12f * trand()), 0);
        if (i > 0 && i < L && en_bpm < 200 && trand() < 0.05f)   /* a skip: a ghosted triplet into the beat */
            pn(I_BASS, t0 + i * 12 - 4, 3, out[i], vel * 0.4f, FL_RAW);
    }
    bass_last = out[L - 1];
}
static void walk(const fbar_t *b, const fbar_t *nb, int newbar, float vel)
{
    int bpb = tu.bpb;
    if (b->n == 2 && bpb == 4) {
        walk_seg(0, 2, b, 0, newbar || 1, b->root[1], vel);
        walk_seg(24, 2, b, 24, 1, nb->root[0], vel);
    } else {
        int tgt = nb->root[0];
        if (nb->root[0] == b->root[0] && nb->q[0] == b->q[0]) {   /* the chord goes on: aim at its third or fifth too */
            float r = trand();
            tgt = r < 0.4f ? b->root[0] : r < 0.7f ? (b->root[0] + Q_3RD[b->q[0]]) % 12 : (b->root[0] + 7) % 12;
        }
        walk_seg(0, bpb, b, 0, newbar, tgt, vel);
        bass_aim = tgt;
        return;
    }
    bass_aim = -1;
}
/* in two: half notes, the root and the fifth (or the next root's approach), a pickup now and then */
static void two_feel(const fbar_t *b, const fbar_t *nb, int newc, float vel)
{
    int r0 = oct_near(b->root[0], bass_last, 31, 48), n2, T = oct_near(nb->root[0], r0, 31, 48);
    bass_aim = -1;
    if (tu.bpb == 3) {                           /* the waltz: one to the bar */
        pn(I_BASS, 0, 22, r0, vel, 0);
        if (trand() < 0.45f)
            pn(I_BASS, 24, 10, nb->root[0] != b->root[0] ? (trand() < 0.5f ? T - 1 : T + 1) : r0 + 7, vel * 0.82f, 0);
        bass_last = r0;
        return;
    }
    (void)newc;
    if (b->n == 2)
        n2 = oct_near(b->root[1], r0, 31, 52);
    else if (nb->root[0] != b->root[0] && trand() < 0.4f) {   /* toward the next root: a half step (slow: the scale's) */
        if (en_bpm < 110) {
            uint16_t sc = m_scale(b, 24);
            n2 = trand() < 0.5f ? step_in(T, -1, sc) : step_in(T, 1, sc);
        } else
            n2 = trand() < 0.5f ? T - 1 : T + 1;
    }
    else
        n2 = oct_near((b->root[0] + 7) % 12, r0, 31, 52);
    pn(I_BASS, 0, 22, r0, vel, 0);
    if (trand() < 0.22f && tu.kind != K_BALLAD) {   /* the dotted quarter and its eighth */
        pn(I_BASS, 24, 16, n2, vel * 0.9f, 0);
        pn(I_BASS, 42, 5, en_bpm < 110 ? step_in(T, -1, m_scale(b, 36)) : T + (trand() < 0.5f ? -1 : 1), vel * 0.6f, 0);
    } else {
        pn(I_BASS, 24, 22, n2, vel * 0.9f, 0);
    }
    bass_last = n2;
}
static void ostinato(const fbar_t *b, float vel)  /* the sus vamp's figure: root, fifth, octave, seventh */
{
    int r0 = oct_near(b->root[0], 36, 31, 43), s = ch_q(b, 0) == Q_MAJ7 ? 11 : 10;
    pn(I_BASS, 0, 16, r0, vel, 0);
    pn(I_BASS, 18, 6, r0 + 7, vel * 0.8f, 0);
    pn(I_BASS, 24, 11, r0 + 12, vel * 0.9f, 0);
    pn(I_BASS, 36, 11, r0 + s, vel * 0.8f, 0);
    bass_last = r0;
}

static void gen_bass(const rec_t *r, const rec_t *rn, const fbar_t *b, const fbar_t *nb)
{
    int role = r->role, newbar = 1;
    float vel = 0.72f * (0.8f + 0.3f * fm_minf(energy, 1.0f));
    if (r != &tu.rec[0]) {
        const rec_t *pr = &tu.rec[rec_i > 0 ? rec_i - 1 : 0];
        const fbar_t *pb = rbar(pr);
        newbar = pb->root[pb->n - 1] != b->root[0] || pb->q[pb->n - 1] != b->q[0];
    }
    (void)rn;
    if (role == R_COUNT || role == R_TRADE_D || (role == R_INTRO && tu.intro == IN_PIANO))
        return;
    if (role == R_FINAL) {
        int at = (r->fl & BF_BASIE) ? 24 : 0;
        pn(I_BASS, at, tu.bpb * 12 - at, oct_near(b->root[0], 36, 28, 40), 0.9f, 0);
        return;
    }
    if (role == R_BSOLO) {
        if (r->fl & BF_PSTART) {
            Lb.left = 0;
            Lb.rest = 6;
            Lb.p = 45;
        }
        Lb.dens = 0.3f + 0.25f * pf(P_INTENS) + 0.2f * (float)r->sub / (float)(r->nch * tu.nbars);
        Lb.vel = 0.86f;
        if (nxt(2)->role != R_BSOLO && Lb.left <= 0)
            Lb.rest = 99;
        line_bar(&Lb, b, nb, tu.bpb);
        return;
    }
    if (tu.kind == K_SUS && (role == R_HEAD || role == R_HEADOUT || role == R_INTRO))
        ostinato(b, vel);
    else if ((r->fl & BF_TWO) || (role == R_INTRO && tu.intro == IN_VAMP))
        two_feel(b, nb, newbar, vel);
    else
        walk(b, nb, newbar, vel);
}

/* ---------------------------------------------------------------- piano --- */
/* the left hand's voicing for a chord: rootless A or B (voice-led from the last), quartal (modal), or a shell */
enum { V_ROOTLESS, V_QUARTAL, V_SHELL };
static void voice(int root, int q, int how)
{
    int best[5], bn = 0, bs = 1 << 30, f, o, i, prevc = 0;
    lh_root = root;
    for (i = 0; i < nlh; i++)
        prevc += lh[i];
    prevc = nlh ? prevc / nlh : 60;
    if (how == V_SHELL) {                        /* root, seventh, third: low */
        int r0 = oct_near(root, 46, 40, 52);
        lh[0] = r0;
        lh[1] = r0 + Q_7TH[q];
        lh[2] = r0 + 12 + Q_3RD[q];
        nlh = 3;
        return;
    }
    if (how == V_QUARTAL) {                      /* fourths up the mode from one of its tones, near the last */
        uint16_t sc = rot(Q_SCALE[q], root);
        int s0;
        for (s0 = 50; s0 <= 62; s0++) {
            int n[4], k = 1, d = 0;
            if (!in_mask(s0, sc))
                continue;
            n[0] = s0;
            while (k < 4) {
                int x = n[k - 1] + 5;
                if (!in_mask(x, sc))
                    break;
                n[k++] = x;
            }
            if (k < 3)
                continue;
            if (k == 3 && in_mask(n[2] + 4, sc))  /* the "so what": a third on top */
                n[k++] = n[2] + 4;
            for (i = 0; i < k; i++)
                d += n[i];
            d = d / k - prevc;
            d = (d < 0 ? -d : d) * 4 + (quart_up ? (s0 > lh[0] ? 0 : 6) : (s0 < lh[0] ? 0 : 6)) + tirand(4);
            if (d < bs) {
                bs = d;
                bn = k;
                for (i = 0; i < k; i++)
                    best[i] = n[i];
            }
        }
        if (bn) {
            for (i = 0; i < bn; i++)
                lh[i] = best[i];
            nlh = bn;
            quart_up ^= 1;
            return;
        }
    }
    for (f = 0; f < 2; f++) {                    /* rootless: each form, each octave that puts it in 48..62 */
        const int8_t *iv = f ? Q_VB[q] : Q_VA[q];
        for (o = 36; o <= 72; o += 12) {
            int n[4], lo, d = 0, k;
            for (i = 0; i < 4; i++)
                n[i] = o + ((root + 120) % 12) + iv[i];
            lo = n[0];
            if (lo < 48 || lo > 62)
                continue;
            for (i = 0; i < 4; i++) {            /* each note to its nearest in the last voicing */
                int m = 99;
                for (k = 0; k < nlh; k++) {
                    int e = n[i] - lh[k];
                    e = e < 0 ? -e : e;
                    m = e < m ? e : m;
                }
                d += nlh ? m : 0;
            }
            d += ((n[0] + n[3]) / 2 - 61 < 0 ? 61 - (n[0] + n[3]) / 2 : (n[0] + n[3]) / 2 - 61);
            d += tirand(2);
            if (d < bs) {
                bs = d;
                bn = 4;
                for (i = 0; i < 4; i++)
                    best[i] = n[i];
            }
        }
    }
    for (i = 0; i < bn; i++)
        lh[i] = best[i];
    nlh = bn ? bn : nlh;
}
static void play_voicing(int tick, int dur, float vel, int top_only)
{
    int i;
    for (i = 0; i < nlh; i++) {
        if (top_only && nlh > 3 && ((lh[i] - lh_root) % 12 + 12) % 12 == 7)
            continue;                            /* lighter: three notes, the fifth left out */
        pn(I_LH, tick, dur, lh[i], vel * (i == nlh - 1 ? 1.06f : 0.95f), (i & 7) << 4);
    }
}

/* comping rhythms: a bar in eighths: x a hit, - held */
static const char *const CP_SPARSE[] = {"x---....", "...x-...", "........", ".......x", "x-......", "...x....", "......x-"};
static const char *const CP_EVEN[] = {"x--x-...", ".x-..x-.", "...x...x", "x---....", "...x-..x", "..x...x.", ".......x", ".x...x.."};
static const char *const CP_BUSY[] = {"...x...x", ".x-..x-.", "x..x.x..", "..x..x.x", "...x.x.x", ".x.x...x"};
static const char *const CP_WALTZ[] = {"x-----", "..x.x.", "..x-x-", "x--x--", "...x-.", ".....x", "..x---"};

static void comp_bar(const rec_t *r, const fbar_t *b, const fbar_t *nb, float vel, int density, int how)
{
    const char *s;
    int bpb = tu.bpb, ne = bpb * 2, e, second = 0, hit_any = 0;
    float stacc = en_bpm > 190 ? 0.6f : 1.0f;
    if (bpb == 3)
        s = CP_WALTZ[tirand(NA(CP_WALTZ))];
    else if (tu.kind == K_BALLAD)
        s = b->n == 2 ? "x---x---" : (trand() < 0.7f ? "x-------" : "x---..x-");
    else if (density == 0)
        s = CP_SPARSE[tirand(NA(CP_SPARSE))];
    else if (density == 1)
        s = trand() < 0.25f ? CP_SPARSE[tirand(NA(CP_SPARSE))] : CP_EVEN[tirand(NA(CP_EVEN))];
    else
        s = trand() < 0.3f ? CP_EVEN[tirand(NA(CP_EVEN))] : CP_BUSY[tirand(NA(CP_BUSY))];
    (void)r;
    for (e = 0; e < ne; e++) {
        int len = 1, t = e * 6, ant;
        const fbar_t *cb = b;
        int ct = t;
        if (s[e] != 'x')
            continue;
        while (e + len < ne && s[e + len] == '-')
            len++;
        /* an anticipation: an off-beat hit just before a change plays the new chord */
        ant = (e & 1) && ((e == ne - 1) || (b->n == 2 && e + 1 == bpb));
        if (e == ne - 1)
            cb = nb, ct = 0;
        else if (ant)
            ct = t + 6;
        if (e >= bpb || (ant && e + 1 == bpb))
            second = 1;
        voice(ch_root(cb, ct), ch_q(cb, ct), how);
        play_voicing(t, (int)((float)(len * 6) * stacc * (tu.kind == K_BALLAD ? 1.0f : 0.85f)) + (e == ne - 1 ? 8 : 0),
                     vel * (ant ? 1.08f : 1.0f), density == 0 && how != V_QUARTAL);
        hit_any = 1;
    }
    if (b->n == 2 && !second && bpb == 4 && trand() < 0.7f) {   /* the second chord heard */
        voice(b->root[1], b->q[1], how);
        play_voicing(24, en_bpm > 190 ? 7 : 10, vel * 0.92f, 1);
    }
    if (!hit_any && (r->fl & BF_PSTART))         /* a new part: the chord heard on one */
        voice(ch_root(b, 0), ch_q(b, 0), how), play_voicing(0, 10, vel, 1);
}

static void solo_params(const rec_t *r, line_t *L, int hot)
{
    const style_t *st = &STYLES[style_i];
    float I = pf(P_INTENS), prog = (float)r->sub / (float)(r->nch * tu.nbars + 1);
    L->lo = 65;
    L->hi = 82 + (int)(6.0f * prog) + (hot ? 3 : 0);
    L->dens = fm_clampf(0.18f + 0.3f * I + 0.28f * prog + 0.3f * (float)st->lines * 0.01f + (hot ? 0.25f : 0.0f), 0.15f, 0.95f);
    L->trip = 0.04f + 0.1f * I;
    L->arp = 0.10f;
    L->quarter = 0.12f * (1.0f - L->dens);
    L->dt = en_bpm < 100 ? 0.05f + 0.14f * I * (0.3f + prog) : 0.0f;
    L->vel = 0.6f + 0.22f * fm_minf(energy, 1.0f);
    L->inst = I_RH;
}

static void gen_piano(const rec_t *r, const rec_t *rn, const fbar_t *b, const fbar_t *nb)
{
    int role = r->role, how = (tu.kind == K_MODAL || (tu.kind == K_SUS && b->q[0] == Q_SUS)) ? V_QUARTAL : V_ROOTLESS;
    const style_t *st = &STYLES[style_i];
    float cv = 0.42f * (0.7f + 0.45f * fm_minf(energy, 1.0f));
    int dens = st->comp;
    if (role == R_COUNT || role == R_TRADE_D)
        return;
    if (role == R_INTRO && tu.intro == IN_BASS && r->sub < 2)
        return;
    if (role == R_FINAL) {                       /* the last chord: rolled, both hands; or the Basie plinks first */
        int i, at = 0;
        float v = 0.62f;
        if (r->fl & BF_BASIE) {
            int top = 84 + ((tu.key + 7) % 12 - 7);
            pn(I_RH, 0, 6, top + 4, 0.5f, 0);
            pn(I_RH, 0, 6, top + 7, 0.45f, 0);
            pn(I_RH, 12, 6, top + 4, 0.55f, 0);
            pn(I_RH, 12, 6, top + 7, 0.5f, 0);
            at = 24;
            v = 0.75f;
        }
        voice(b->root[0], b->q[0], tu.kind == K_MODAL ? V_QUARTAL : V_ROOTLESS);
        for (i = 0; i < nlh; i++)
            pn(I_LH, at, tu.bpb * 12 - at, lh[i], v, (i + 1) << 4);
        {                                        /* the right hand: the chord's colours above, its third on top */
            int t3 = oct_near((b->root[0] + Q_3RD[b->q[0]]) % 12, 78, 72, 84);
            int t9 = oct_near((b->root[0] + 2) % 12, t3 - 5, 64, 80);
            pn(I_RH, at, tu.bpb * 12 - at, t9, v * 0.9f, 5 << 4);
            pn(I_RH, at, tu.bpb * 12 - at, t3, v, 6 << 4);
            if (tu.kind == K_BALLAD || tu.ending == E_RIT)
                pn(I_RH, at, tu.bpb * 12 - at, t3 + 12 - (Q_3RD[b->q[0]] == 3 ? 3 : 4) + 2, v * 0.7f, 7 << 4);
        }
        return;
    }
    /* the right hand */
    if (role == R_HEAD || role == R_HEADOUT) {
        int s = b->sect, i, t0 = tu.f[r->fbar].sbar * tu.bpb * 12, t1 = t0 + tu.bpb * 12;
        int cont = rn->role == R_HEAD || rn->role == R_HEADOUT;
        for (i = 0; i < tu.nmel[s]; i++) {
            const mn_t *m = &tu.mel[s][i];
            if (m->tick < t0 || m->tick >= t1 || (m->pick && !cont))
                continue;
            pn(I_RH, m->tick - t0, m->dur, m->note, (float)m->vel * (1.0f / 127.0f) * (0.85f + 0.25f * fm_minf(energy, 1.0f)), 0);
            if ((tu.kind == K_BALLAD || tu.kind == K_WALTZ) && m->dur >= 18) {   /* a long note: a chord tone under it */
                uint16_t ct = m_ct(b, m->tick - t0);
                int h = m->note - 3;
                while (h > m->note - 10 && !in_mask(h, ct))
                    h--;
                if (h > m->note - 10)
                    pn(I_RH, m->tick - t0, m->dur, h, (float)m->vel * (1.0f / 127.0f) * 0.6f, 1 << 4);
            }
        }
        comp_bar(r, b, nb, cv * 0.85f, dens > 0 ? dens - 1 : 0, how);
        return;
    }
    if (role == R_PSOLO || role == R_TRADE_P || role == R_TAG || (role == R_INTRO && tu.intro == IN_PIANO)) {
        if ((r->fl & BF_PSTART) && (role != R_TRADE_P || r->sub == 0)) {
            Lp.left = 0;
            Lp.rest = role == R_TAG ? 12 : 6;
            Lp.nphr = 0;
        }
        if (role == R_TRADE_P && r->sub % 8 == 0) {   /* back in after the drums: straight in */
            Lp.left = 0;
            Lp.rest = 0;
        }
        solo_params(r, &Lp, role == R_TRADE_P);
        if (role == R_INTRO || role == R_TAG) {
            Lp.dens = 0.3f;
            Lp.vel *= 0.85f;
        }
        if ((role == R_PSOLO && nxt(2)->role != R_PSOLO) ||   /* the solo winds down */
            (role == R_TRADE_P && r->sub % 4 == 3) || (role == R_INTRO && r->sub == 3)) {
            if (Lp.left <= 0)
                Lp.rest = 99;
            else if (Lp.left > 24)
                Lp.left = 24;
        }
        line_bar(&Lp, b, nb, tu.bpb);
        if (role == R_PSOLO && st->shells && trand() * 100.0f < (float)st->shells && how == V_ROOTLESS)
            how = V_SHELL;
        comp_bar(r, b, nb, cv * (role == R_TRADE_P ? 1.0f : 0.88f), dens, how);
        return;
    }
    /* behind the bass, and the intros' vamps: comping only, lighter */
    comp_bar(r, b, nb, cv * (role == R_BSOLO ? 0.62f : 0.9f), role == R_BSOLO ? 0 : dens, how);
}

/* ------------------------------------------------------------ the beat --- */
static float tempo_now(const rec_t *r, int beat)
{
    float b = tu.bpm * fm_exp2f((pf(P_TEMPO) - 0.5f) * 0.83f);
    if (r->fl & BF_RIT) {                        /* the last two bars before the last chord: slowing */
        float pos = ((float)((rec_i - (tu.nrec - 3)) * tu.bpb + beat)) / (float)(2 * tu.bpb);
        b *= 1.0f - 0.36f * fm_clampf(pos, 0.0f, 1.0f);
    }
    if (r->role == R_FINAL && !(r->fl & BF_BASIE))
        b *= 0.42f;
    return fm_clampf(b, 38.0f, 330.0f);
}
static float swing_map(int t, int fl)            /* a tick in the beat to the fraction of the beat it sounds at */
{
    if (fl & FL_RAW)
        return (float)t * (1.0f / 12.0f);
    if (fl & FL_DT) {                            /* sixteenths, swung a little within each eighth */
        float rr = 0.5f + (swing_r - 0.5f) * 0.45f, h = t >= 6 ? 0.5f : 0.0f;
        int sub = t % 6;
        return h + (sub == 0 ? 0.0f : sub == 3 ? rr * 0.5f : (float)sub * (1.0f / 12.0f));
    }
    if (t <= 6)
        return (float)t * (1.0f / 6.0f) * swing_r;
    return swing_r + (float)(t - 6) * (1.0f / 6.0f) * (1.0f - swing_r);
}
static float sweep_shape(void)
{
    float ph = beat_len > 1.0f ? 1.0f - fm_clampf(beat_left / beat_len, 0.0f, 1.0f) : 0.0f, x = sn(ph * 0.5f);
    return x * x;
}

static void gen_bar(void)
{
    const rec_t *r = &tu.rec[rec_i], *rn = nxt(1);
    const fbar_t *b, *nb;
    float e, I = pf(P_INTENS), prog = (float)r->sub / (float)(r->nch * tu.nbars + 1);
    fin_bar.n = 1;
    fin_bar.root[0] = (int8_t)tu.fin_root;
    fin_bar.q[0] = (uint8_t)tu.fin_q;
    fin_bar.sect = 0;
    fin_bar.sbar = 0;
    b = rbar(r);
    nb = rbar(rn);
    switch (r->role) {
    case R_HEAD: e = 0.42f; break;
    case R_HEADOUT: e = 0.52f; break;
    case R_PSOLO: e = 0.48f + 0.42f * prog; break;
    case R_BSOLO: e = 0.25f; break;
    case R_TRADE_P: case R_TRADE_D: e = 0.85f; break;
    case R_TAG: e = 0.55f; break;
    case R_INTRO: e = 0.35f; break;
    default: e = 0.5f; break;
    }
    energy = fm_clampf(e * (0.6f + 0.85f * I), 0.1f, 1.2f);
    en_role = r->role;
    en_soloist = (uint8_t)(r->role == R_PSOLO || r->role == R_TRADE_P ? 1 : r->role == R_BSOLO ? 2 : r->role == R_TRADE_D ? 3 : 0);
    en_bar = r->fbar;
    en_chorus = r->chorus;
    en_nchorus = r->nch;
    if ((r->fl & BF_PSTART) && rec_i > 0) {      /* a solo just ended: the room applauds a little */
        int pr = tu.rec[rec_i - 1].role;
        if ((pr == R_PSOLO && trand() < 0.45f) || (pr == R_BSOLO && trand() < 0.8f) || (pr == R_TRADE_P || pr == R_TRADE_D))
            appl_small = pr == R_PSOLO ? 0.55f : 0.45f;
    }
    npl = 0;
#ifdef EN_HOST
    if (en_trace_bar) {
        char t[40], *p = t, c[12];
        int k;
        for (k = 0; k < b->n; k++) {
            p = put_s(p, en_chord_name(b->root[k], b->q[k], c));
            if (k + 1 < b->n)
                p = put_s(p, " ");
        }
        en_trace_bar(rec_i, r->role, r->fbar, r->chorus, t);
    }
#endif
    gen_drums(r, b);
    gen_bass(r, rn, b, nb);
    gen_piano(r, rn, b, nb);
}

static void play_reset(void)
{
    rec_i = 0;
    beat_i = 0;
    beat_left = 0.0f;
    npl = 0;
    memset_l(&Lp, sizeof Lp);
    memset_l(&Lb, sizeof Lb);
    Lp.pend = Lb.pend = -1;
    Lp.p = 72;
    Lb.p = 45;
    Lb.lo = 36;
    Lb.hi = 60;
    Lb.bass = 1;
    Lb.trip = 0.14f;
    Lb.inst = I_BASS;
    Lb.quarter = 0.3f;
    bass_aim = -1;
    fill_prev = 0;
    nlh = 0;
}

static void seq_beat(void)
{
    const rec_t *r;
    int i, t0, t1;
    float bpm;
    if (beat_i == 0)
        gen_bar();
    r = &tu.rec[rec_i];
    bpm = tempo_now(r, beat_i);
    en_bpm = (uint16_t)(bpm + 0.5f);
    beat_len = EN_SR * 60.0f / bpm;
    swing_r = fm_clampf(0.72f - (bpm - 60.0f) * 0.00085f, 0.53f, 0.72f) + (pf(P_SWING) - 0.5f) * 0.14f;
    swing_r = fm_clampf(swing_r, 0.5f, 0.78f);
    t0 = beat_i * 12;
    t1 = t0 + 12;
    {                                            /* the chord now (the screen) */
        const fbar_t *b = rbar(r);
        int root = r->role == R_COUNT ? tu.f[0].root[0] : ch_root(b, t0), q = r->role == R_COUNT ? tu.f[0].q[0] : ch_q(b, t0);
        en_chord_root = (uint8_t)root;
        en_chord_q = (uint8_t)q;
        en_chord_pcs = rot(Q_CT[q], root);
    }
    for (i = 0; i < npl; i++) {
        const pn_t *p = &pl[i];
        float off, ms = 0.0f;
        int32_t gate;
        if (p->tick < t0 || p->tick >= t1)
            continue;
        switch (p->inst) {                       /* where each sits on the beat: the ride ahead, the piano behind */
        case I_RIDE: case I_BELL: ms = -4.0f; break;
        case I_HAT: ms = -2.0f; break;
        case I_BASS: ms = -1.0f; break;
        case I_RH: ms = (r->role == R_PSOLO ? 9.0f : 5.0f) + (float)(p->fl >> 4) * 9.0f; break;
        case I_LH: ms = 4.0f + (float)(p->fl >> 4) * (tu.kind == K_BALLAD || r->role == R_FINAL ? 22.0f : 6.0f); break;
        default: break;
        }
        ms += (frand() * 2.0f - 1.0f) * (p->inst == I_RH || p->inst == I_LH ? 5.0f : 3.0f);
        off = swing_map(p->tick - t0, p->fl & 3) * beat_len + ms * 44.1f;
        gate = (int32_t)((float)p->dur * (1.0f / 12.0f) * beat_len);
        sched(p->inst, (int32_t)off, p->note, (float)p->vel * (1.0f / 127.0f), gate);
#ifdef EN_HOST
        (void)0;
#endif
    }
    en_beat = (uint8_t)beat_i;
    en_beats++;
    if (++beat_i >= tu.bpb) {                    /* on to the next bar */
        beat_i = 0;
        rec_i++;
        if (en_repeat_on && rec_i == tu.out_rec)
            rec_i = tu.solo_rec;
        if (rec_i >= tu.nrec) {                  /* the last chord rings; the room applauds */
            rec_i = tu.nrec - 1;
            en_phase = PH_APPLAUSE;
            appl_t = 0.0f;
            appl_left = EN_SR * (5.5f + 2.0f * frand());
            appl_peak = 0.55f + 0.35f * frand();
            sweep_lvl = 0.0f;
        }
    }
}

/* between tunes: the applause, then a pause, then the next is counted in */
static void between(uint32_t *k)
{
    if (en_phase == PH_APPLAUSE) {
        appl_left -= (float)*k;
        if (appl_left <= 0.0f) {
            en_phase = PH_GAP;
            gap_left = EN_SR * (1.5f + 1.8f * frand());
        }
    } else {
        gap_left -= (float)*k;
        if (gap_left <= 0.0f) {
            int i;
            for (i = 0; i < NEV; i++)
                evq[i].used = 0;
            new_tune();
        }
    }
}
static void applause_tick(float dt)
{
    float tgt = 0.0f;
    if (!en_playing) {
        appl_lvl = 0.0f;
        return;
    }
    if (en_phase == PH_APPLAUSE) {
        float left = appl_left / EN_SR;
        appl_t += dt;
        if (appl_t > 1.1f)                       /* after the last chord has had its moment */
            tgt = appl_peak * fm_clampf((appl_t - 1.1f) * 2.0f, 0.0f, 1.0f) * fm_clampf(left / 2.2f, 0.0f, 1.0f);
    }
    if (appl_small > 0.0f) {
        tgt = fm_maxf(tgt, appl_small > 0.3f ? 0.45f : appl_small * 1.5f);
        appl_small -= dt * 0.25f;
    }
    appl_lvl += (tgt - appl_lvl) * fm_minf(dt * 4.0f, 1.0f);
    en_applause = appl_lvl;
}
