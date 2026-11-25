/* SPDX-License-Identifier: MPL-2.0
 * ORGAN TRIO: ashtonwallen, 2026 */
/* The players (part of organ.c): each bar of the arrangement planned for the drums, the organist's two hands (the
 * left one the bass) and the guitar, the speaker and the drawbars and the swell pedal; then played beat by beat
 * with the feel the tune asks for; applause and the pause between tunes. */

/* the bar's plan: its notes, in ticks (twelfths of a beat) from the bar's start */
typedef struct {
    int16_t tick, dur;
    uint8_t inst, note, vel, fl;
} pn_t;
#define NPLAN 256
static pn_t pl[NPLAN];
static int npl;
static int rec_i, beat_i;
static float swing_r = 0.62f, energy = 0.5f;
static line_t Lo, Lg;                             /* the organ's line, the guitar's */
static int lh[5], nlh, lh_root;                   /* the organ's chord (rootless), its root */
static int gv[5], ngv;                            /* the guitar's grip */
static int bass_last = 40, bass_aim = -1, fill_prev;
static fbar_t fin_bar, amen_bar;
static float appl_small;
static int la_o, lb_o, la_g, lb_g;                /* the chorus's call and answer, the organ's and the guitar's */
static int shout_i, lift_g, lift_h;               /* the climax's riff; the speaker lifted behind the guitar, on the head's long notes */
static int lsl_asked, reg_asked;                  /* what the organist last did with the speaker, the drawbars */

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
    if (tu.feel == F_TRIP || (tick % 6) != 0)
        fl |= FL_RAW;
    plan_note(inst, tick, dur, note, v < 2 ? 2 : v > 127 ? 127 : v, fl);
}

static const rec_t *nxt(int a)
{
    int i = rec_i + a;
    if (en_repeat_on && i >= tu.out_rec && rec_i < tu.out_rec)   /* (blowing on: the solos again) */
        i = tu.solo_rec + (i - tu.out_rec);
    return &tu.rec[i < tu.nrec ? i : tu.nrec - 1];
}
static const fbar_t *rbar(const rec_t *r)
{
    if (r->role == R_FINAL)
        return &fin_bar;
    if (r->role == R_TAG && r->sub == 99)
        return &amen_bar;
    return &tu.f[r->fbar];
}
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

/* ---------------------------------------------------------------- drums --- */
/* comping figures (swing): a bar on the triplet grid (three a beat): S snare, s a ghost, K the bass drum */
static const char *const DCOMP[] = {
    "..........s.", ".....S......", "..s.....s...", "........S..K", ".....s.....S", "..S.....K...",
    "........s.S.", "K.........S.", "..s..s..s...", ".S.........s", "...........S", "..K.....s...",
};
static const char *const DFILL1[] = {"SsS", "S.S", "..S", "SSS", "s.S"};
static const char *const DFILL2[] = {"..SS.S", "S.sS.S", "SSSTTT", "..S.TF", "S.SF.F"};
/* the grooves: a bar of sixteenths: the bass drum, the snare (S, s a ghost), the hi-hat (x closed, o open) */
static const char *const GK[3] = {"x.....x.x..x....", "x..x......x.....", "x.....x...x..x.."};
static const char *const GS_[3] = {"....S..s....S...", "....S.....s.S..s", "....S..s.s..S..."};
static const char *const GH[3] = {"x.x.x.x.x.x.x.o.", "x.x.x.x.x.x.x.x.", "xxx.xxx.xxx.xxo."};
static void dchar(char c, int tick, float v)
{
    switch (c) {
    case 'S': pn(I_SNARE, tick, 2, 0, v * 0.85f, FL_RAW); break;
    case 's': pn(I_SNARE, tick, 2, 0, v * 0.32f, FL_RAW); break;
    case 'K': pn(I_KICK, tick, 2, 0, v * 0.7f, FL_RAW); break;
    case 'T': pn(I_TOM, tick, 2, 0, v * 0.8f, FL_RAW); break;
    case 'F': pn(I_TOM, tick, 2, 1, v * 0.85f, FL_RAW); break;
    case 'C':
        pn(I_CRASH, tick, 2, 0, v * 0.7f, FL_RAW);
        pn(I_KICK, tick, 2, 0, v * 0.75f, FL_RAW);
        break;
    default: break;
    }
}
static void fill_bar(int lim, float dv, int brush)   /* a fill on the triplet grid from beat lim/3 */
{
    int i, two = lim == 6;
    const char *s = two ? DFILL2[tirand(NA(DFILL2))] : DFILL1[tirand(NA(DFILL1))];
    for (i = 0; i < (two ? 6 : 3); i++) {
        char c = s[i];
        int t = lim / 3 * 12 + (i / 3) * 12 + (i % 3) * 4;
        if (brush) {
            if (c == 'S')
                pn(I_BRUSH, t, 2, 0, 0.55f * dv, FL_RAW);
        } else {
            dchar(c, t, dv * (0.75f + 0.05f * (float)i));
        }
    }
    fill_prev = 1;
}
static void cym_swell(int from, float dv)         /* a cymbal rolled soft to loud into the next bar */
{
    int i;
    for (i = from; i < 48; i += 2) {
        float x = (float)(i - from) / (float)(48 - from);
        pn(I_RIDE, i, 2, 0, (0.06f + 0.3f * x * x) * dv, FL_RAW);
    }
}

static void gen_drums(const rec_t *r, const fbar_t *b)
{
    int i, role = r->role, two = (r->fl & BF_TWO) != 0, brush = tu.brushes, k = tu.kind;
    int send = (r->fl & BF_SEND) != 0, peak = (r->fl & BF_PEAK) != 0;
    float dv = 0.5f + 0.42f * fm_minf(energy, 1.1f), bpm = (float)en_bpm;
    (void)b;
    sweep_lvl = 0.0f;
    if (role == R_COUNT) {                       /* the count: two bars, half time then the beats */
        for (i = 0; i < 4; i++)
            if (r->sub == 1 || i % 2 == 0)
                pn(I_STICK, i * 12, 2, 0, 0.55f + (r->sub ? 0.1f : 0.0f), FL_RAW);
        return;
    }
    if (role == R_INTRO) {
        if (tu.intro == IN_ORGAN) {              /* the organ alone; a cymbal swells the band in */
            if (r->sub == 1)
                cym_swell(24, dv);
            return;
        }
        if (tu.intro == IN_PICKUP && r->sub == 0) {
            pn(I_RIDE, 36, 2, 0, 0.3f * dv, FL_RAW);
            return;
        }
    }
    if (role == R_FINAL) {                       /* the last chord: a crash, then the cymbal swells and fades */
        int at = (r->fl & BF_BASIE) ? 24 : 0;
        if (tu.ending == E_STOP) {               /* the groove's stop: one hit together */
            pn(I_CRASH, 0, 2, 0, 0.8f * dv, FL_RAW);
            pn(I_KICK, 0, 2, 0, 0.8f * dv, FL_RAW);
            pn(I_SNARE, 0, 2, 0, 0.7f * dv, FL_RAW);
            return;
        }
        if (brush) {
            pn(I_BRUSH, at, 2, 0, 0.7f * dv, FL_RAW);
            pn(I_KICK, at, 2, 0, 0.35f * dv, FL_RAW);
            sweep_lvl = 0.3f * dv;
        } else {
            pn(I_CRASH, at, 2, 0, 0.75f * dv, FL_RAW);
            pn(I_KICK, at, 2, 0, 0.6f * dv, FL_RAW);
            for (i = at + 6; i < 48; i += 2) {
                float x = (float)(i - at) / (float)(48 - at), sw = x < 0.45f ? x / 0.45f : (1.0f - x) / 0.55f;
                pn(I_RIDE, i, 2, 0, (0.10f + 0.28f * sw) * dv, FL_RAW);
            }
        }
        return;
    }
    if (role == R_TAG && r->sub == 99) {         /* the amen: the cymbal rolled under it */
        cym_swell(0, dv * 1.1f);
        pn(I_KICK, 0, 2, 0, 0.4f * dv, FL_RAW);
        return;
    }
    /* a crash to open a part, the peak, after a fill */
    if (((r->fl & BF_PSTART) && role != R_INTRO && (role != R_HEAD || tu.intro == IN_COUNT)) || fill_prev ||
        (peak && r->sub % 4 == 0 && k != K_GOSPEL)) {
        if (brush && !peak)
            pn(I_BRUSH, 0, 2, 0, 0.62f * dv, FL_RAW);
        else
            pn(I_CRASH, 0, 2, 0, (peak ? 0.62f : 0.5f) * dv, FL_RAW);
        pn(I_KICK, 0, 2, 0, 0.5f * dv, FL_RAW);
    }
    fill_prev = 0;
    switch (tu.feel) {
    case F_STRAIGHT: {                           /* the groove: kick, backbeat and ghosts, the hat's sixteenths */
        int p = tu.drum_pat % 3, last = send && (r->fl & BF_FORMEND || r->sub % 8 == 7);
        float hv = 0.36f * dv;
        if (role == R_INTRO && r->sub < 2)       /* the drums alone first */
            dv *= 0.9f;
        for (i = 0; i < 16; i++) {
            int t = i * 3;
            if (last && i >= 12)
                break;
            if (GK[p][i] == 'x')
                pn(I_KICK, t, 2, 0, (i % 4 ? 0.55f : 0.68f) * dv, FL_RAW);
            if (GS_[p][i] == 'S')
                pn(peak ? I_SNARE : I_SNARE, t, 2, 0, 0.62f * dv, FL_RAW);
            else if (GS_[p][i] == 's')
                pn(I_SNARE, t, 2, 0, 0.16f * dv, FL_RAW);
            if (GH[p][i] == 'x')
                pn(peak ? I_RIDE : I_HAT, t, 2, 0, hv * (i % 2 ? 0.6f : i % 4 ? 0.85f : 1.0f) * (peak ? 1.6f : 1.0f), FL_RAW);
            else if (GH[p][i] == 'o')
                pn(I_OHAT, t, 2, 0, hv * 0.9f, FL_RAW);
        }
        if (last) {                              /* the turn: a fill of sixteenths on the snare and toms */
            static const char F16[] = "SsSTSTFF";
            int f0 = tirand(2) * 2;
            for (i = 0; i < 4; i++) {
                char c = F16[f0 + i * 2];
                if (c == 'S' || c == 's')
                    pn(I_SNARE, 36 + i * 3, 2, 0, (0.45f + 0.08f * (float)i) * dv, FL_RAW);
                else
                    pn(I_TOM, 36 + i * 3, 2, c == 'F', (0.5f + 0.08f * (float)i) * dv, FL_RAW);
            }
            fill_prev = 1;
        }
        return;
    }
    case F_TRIP: {                               /* 12/8: the cymbal on every triplet, the backbeat, the kick's pickups */
        for (i = 0; i < 12; i++) {
            int t = (i / 3) * 12 + (i % 3) * 4;
            float a = i % 3 == 0 ? 1.0f : 0.62f;
            if (brush)
                pn(I_BRUSH, t, 2, 0, 0.24f * a * dv, FL_RAW);
            else
                pn(peak || energy > 0.75f ? I_RIDE : I_HAT, t, 2, 0, (peak ? 0.62f : 0.42f) * a * dv, FL_RAW);
        }
        if (brush)
            sweep_lvl = 0.2f * dv;
        pn(brush ? I_BRUSH : I_SNARE, 12, 2, 0, (brush ? 0.62f : 0.6f) * dv, FL_RAW);
        pn(brush ? I_BRUSH : I_SNARE, 36, 2, 0, (brush ? 0.62f : 0.62f) * dv, FL_RAW);
        pn(I_KICK, 0, 2, 0, 0.5f * dv, FL_RAW);
        pn(I_KICK, 24, 2, 0, 0.42f * dv, FL_RAW);
        if (trand() < 0.5f)
            pn(I_KICK, 20, 2, 0, 0.3f * dv, FL_RAW);
        if (send && !brush && trand() < 0.5f + 0.4f * energy)
            fill_bar(9, dv, 0);
        else if (!brush && trand() < 0.3f)
            pn(I_SNARE, 44, 2, 0, 0.2f * dv, FL_RAW);
        return;
    }
    case F_SHUFFLE: {                            /* the shuffle: the cymbal's long-short, the backbeat, four on the floor soft */
        for (i = 0; i < 4; i++) {
            int t = i * 12, back = i & 1;
            pn(I_RIDE, t, 2, 0, (back ? 0.7f : 0.6f) * dv, FL_RAW);
            pn(I_RIDE, t + 8, 2, 0, 0.42f * dv, FL_RAW);
            pn(I_KICK, t, 2, 0, (back ? 0.22f : 0.36f) * dv, FL_RAW);
            if (back) {
                pn(I_SNARE, t, 2, 0, (0.62f + 0.1f * energy) * dv, FL_RAW);
                pn(I_HAT, t, 2, 0, 0.4f * dv, FL_RAW);
            } else if (trand() < 0.25f * energy) {
                pn(I_SNARE, t + 8, 2, 0, 0.18f * dv, FL_RAW);   /* a ghost, the shuffle's swing */
            }
        }
        if (send && trand() < 0.35f + 0.45f * energy)
            fill_bar(9, dv, 0);
        return;
    }
    default: break;
    }
    if (k == K_GOSPEL) {                         /* the gospel ballad: the cross-stick on two and four, the ride soft */
        int stick = peak;
        for (i = 0; i < 4; i++) {
            int t = i * 12, back = i & 1;
            if (brush && !stick)
                pn(I_BRUSH, t, 2, 0, (back ? 0.5f : 0.26f) * dv, 0);
            else
                pn(I_RIDE, t, 2, 0, (back ? 0.5f : 0.42f) * dv * (stick ? 1.2f : 1.0f), 0);
            if (back)
                pn(stick ? I_SNARE : I_RIM, t, 2, 0, (stick ? 0.55f : 0.5f) * dv, 0);
            if (i == 0 || (i == 2 && trand() < 0.5f))
                pn(I_KICK, t, 2, 0, 0.32f * dv, 0);
            if (back && !stick)
                pn(I_HAT, t, 2, 0, 0.3f * dv, 0);
            if (stick && trand() < 0.5f)
                pn(I_RIDE, t + 8, 2, 0, 0.3f * dv, FL_RAW);
        }
        if (brush && !stick)
            sweep_lvl = 0.3f * dv;
        if (send && (r->fl & BF_FORMEND || r->sub % 8 == 7))
            cym_swell(30, dv);
        return;
    }
    /* swing: the ride's spang-a-lang, the hat's foot on two and four, the bass drum feathered */
    for (i = 0; i < 4; i++) {
        int t = i * 12, back = i & 1;
        float rv = (back ? 0.92f : 0.76f) * dv * (two ? 0.86f : 1.0f);
        int skip = back;
        if (bpm > 250.0f)
            skip = skip && i == 3 && trand() < 0.5f;
        else if (!skip && energy > 0.75f && trand() < 0.12f)
            skip = 1;
        pn(I_RIDE, t, 2, 0, rv, 0);
        if (skip)
            pn(I_RIDE, t + 6, 2, 0, rv * 0.62f, 0);
        if (back)
            pn(I_HAT, t, 2, 0, 0.55f * dv, 0);
        if (bpm < 230.0f)
            pn(I_KICK, t, 2, 0, 0.15f * dv, 0);
    }
    {
        float d = (0.12f + 0.62f * energy) * (role == R_HEAD || role == R_HEADOUT ? 0.55f : 1.0f) * (two ? 0.4f : 1.0f);
        int fill = send && trand() < 0.35f + 0.45f * energy && !two;
        int lim = fill ? ((r->fl & BF_FORMEND) && energy > 0.6f ? 6 : 9) : 12;
        if (trand() < d) {
            const char *s = DCOMP[tirand(NA(DCOMP))];
            for (i = 0; i < lim && i < 12; i++)
                dchar(s[i], (i / 3) * 12 + (i % 3) * 4, dv * 0.9f);
        }
        if (fill)
            fill_bar(lim, dv, 0);
    }
}

/* ----------------------------------------------------------------- bass --- */
/* a walking line: the chord's root (or another chord tone when the chord goes on) on its first beat, an approach
 * to the next chord on its last, scale or chord tones between, chosen for a smooth line */
static void walk_seg(int t0, int L, const fbar_t *b, int tc, int newc, int next_root, float vel)
{
    int root = ch_root(b, tc), q = ch_q(b, tc), n0, nl = 0, i, a, c;
    uint16_t sc = m_scale(b, tc), ct = m_ct(b, tc);
    int out[4];
    if (!newc && bass_aim >= 0)
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
            int d = cand[i] - out[0], s = (d < 0 ? -d : d);
            s = s > 5 ? s * 2 : s;
            s += i < 2 ? 0 : i < 4 ? 2 : 4;       /* chromatic first, then the scale, then the fifth */
            s += tirand(4);
            if (cand[i] == out[0] || cand[i] < 28 || cand[i] > 55)
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
                    s += 2;
                if (!in_mask(m2, ct))
                    s += 3;
                if (m2 == n0 || m1 == nl)
                    s += 15;
                s += tirand(3);
                if (s < bs)
                    bs = s, b1 = m1, b2 = m2;
            }
        }
        out[1] = b1;
        if (L == 4)
            out[2] = b2;
    }
    for (i = 0; i < L; i++)                      /* the organist's left hand: legato, the pedal on the beat */
        pn(I_BASS, t0 + i * 12, 11, out[i], vel * (i == 0 ? 1.0f : 0.9f) * (0.94f + 0.12f * trand()), 0);
    bass_last = out[L - 1];
}
static void walk(const fbar_t *b, const fbar_t *nb, int newbar, float vel)
{
    if (b->n == 2) {
        walk_seg(0, 2, b, 0, 1, b->root[1], vel);
        walk_seg(24, 2, b, 24, 1, nb->root[0], vel);
        bass_aim = -1;
    } else {
        int tgt = nb->root[0];
        if (nb->root[0] == b->root[0] && nb->q[0] == b->q[0]) {   /* the chord goes on: aim at its third or fifth too */
            float r = trand();
            tgt = r < 0.4f ? b->root[0] : r < 0.7f ? (b->root[0] + Q_3RD[b->q[0]]) % 12 : (b->root[0] + 7) % 12;
        }
        walk_seg(0, 4, b, 0, newbar, tgt, vel);
        bass_aim = tgt;
    }
}
/* in two: half notes, the root and the fifth (or a step toward the next root); the gospel's walk up to IV */
static void two_feel(const fbar_t *b, const fbar_t *nb, float vel, int gospel)
{
    int r0 = oct_near(b->root[0], bass_last, 31, 48), n2, T = oct_near(nb->root[0], r0, 31, 52);
    bass_aim = -1;
    if (gospel && b->n == 1 && ((nb->root[0] - b->root[0] + 12) % 12 == 5 || (nb->root[0] - b->root[0] + 12) % 12 == 2)) {
        uint16_t sc = m_scale(b, 0);              /* the walk up: the root, then the scale's steps to the next */
        int n = r0, i;
        pn(I_BASS, 0, 22, r0, vel, 0);
        for (i = 1; i < 3; i++) {
            n = step_in(n, 1, sc);
            if (n >= T)
                n = T - 1;
            pn(I_BASS, 12 + 12 * i, 11, n, vel * 0.85f, 0);
        }
        bass_last = n;
        return;
    }
    if (b->n == 2 && b->root[1] == b->root[0])  /* (I6 to I7: up to its third, leading on) */
        n2 = r0 + Q_3RD[b->q[1]];
    else if (b->n == 2)
        n2 = oct_near(b->root[1], r0, 31, 52);
    else if (nb->root[0] != b->root[0] && trand() < (gospel ? 0.6f : 0.4f)) {
        uint16_t sc = m_scale(b, 24);
        n2 = trand() < 0.5f ? step_in(T, -1, sc) : (gospel ? T - 1 : T + 1);
    } else
        n2 = oct_near((b->root[0] + (b->q[0] == Q_HD || b->q[0] == Q_DIM ? 6 : 7)) % 12, r0, 31, 52);
    pn(I_BASS, 0, 22, r0, vel, 0);
    pn(I_BASS, 24, 22, n2, vel * 0.9f, 0);
    bass_last = n2;
}
/* the slow blues in 12/8: a note a beat, up the chord and back, the triplet pickups, the last beat leaning in */
static void slow12(const fbar_t *b, const fbar_t *nb, float vel)
{
    int k, beats = b->n == 2 ? 2 : 4;
    for (k = 0; k < b->n; k++) {
        int t0 = k * 24, root = b->root[k], q = b->q[k], r = oct_near(root, 38, 31, 43), i;
        int nr = k + 1 < b->n ? b->root[k + 1] : nb->root[0];
        int up[4], th = Q_3RD[q], fi = q == Q_HD || q == Q_DIM ? 6 : 7, sv = Q_7TH[q];
        up[0] = r, up[1] = r + th, up[2] = r + fi, up[3] = r + (q_is_minor(q) ? sv : 9);
        if (trand() < 0.4f)
            up[1] = r + fi, up[2] = r + 12, up[3] = r + sv;
        for (i = 0; i < beats; i++) {
            int n = up[i];
            if (i == beats - 1 && nr != root) {  /* the last beat leans into the next chord, a half step away */
                int T = oct_near(nr, 38, 31, 43);
                n = trand() < 0.5f ? T - 1 : T + 1;
            }
            pn(I_BASS, t0 + 12 * i, 10, n, vel * (i == 0 ? 1.0f : 0.86f), 0);
            if (i == 1 && trand() < 0.35f)       /* a pickup triplet into the next beat */
                pn(I_BASS, t0 + 12 * i + 8, 3, n, vel * 0.55f, FL_RAW);
            bass_last = n;
        }
    }
    bass_aim = -1;
}
/* the groove's ostinatos: a bar each, from the chord's root (3: its third, 10: its seventh) */
typedef struct {
    uint8_t n, t[8];
    int8_t iv[8];
    uint8_t d[8];
} briff_t;
static const briff_t BRIFF[5] = {
    {6, {0, 9, 18, 24, 30, 42}, {0, 0, -2, 0, 12, 7}, {8, 3, 6, 6, 6, 6}},
    {6, {0, 18, 24, 30, 36, 42}, {0, 7, 12, 10, 7, 3}, {14, 6, 6, 6, 6, 6}},
    {7, {0, 6, 15, 18, 24, 36, 42}, {0, 0, 3, 5, 7, 10, 7}, {5, 3, 3, 6, 9, 6, 6}},
    {5, {0, 18, 24, 36, 42}, {0, 0, 12, 10, 7}, {16, 6, 10, 6, 6}},
    {6, {0, 12, 18, 24, 30, 42}, {0, 7, 12, 10, 7, 3}, {10, 6, 6, 6, 9, 6}},
};
static void groove_bass(const fbar_t *b, const fbar_t *nb, float vel, int phrase_end)
{
    const briff_t *R = &BRIFF[tu.bass_riff % 5];
    int i, q = b->q[0], r = oct_near(b->root[0], 36, 31, 42), nr = nb->root[0];
    for (i = 0; i < R->n; i++) {
        int iv = R->iv[i], t = R->t[i], n;
        const fbar_t *cb = b;
        if (b->n == 2 && t >= 24) {              /* a bar of two chords: the riff's second half on the second */
            cb = b;
            r = oct_near(b->root[1], 36, 31, 42);
            q = b->q[1];
        }
        (void)cb;
        if (iv == 3 || iv == 15)
            iv += Q_3RD[q] - 3;
        else if (iv == 10 || iv == -2)
            iv += Q_7TH[q] - 10;
        else if ((iv == 7 || iv == -5) && (q == Q_HD || q == Q_DIM))
            iv -= 1;
        n = r + iv;
        if (i == R->n - 1 && t >= 36 && nr != b->root[b->n - 1] && (phrase_end || trand() < 0.5f)) {
            int T = oct_near(nr, n, 31, 50);     /* the riff's last note: a half step into the next chord */
            n = trand() < 0.5f ? T - 1 : T + 1;
        }
        pn(I_BASS, t, R->d[i], n, vel * (t % 12 == 0 ? 1.0f : 0.85f), 0);
        bass_last = n;
    }
}

static void gen_bass(const rec_t *r, const fbar_t *b, const fbar_t *nb)
{
    int role = r->role, newbar = 1;
    float vel = 0.74f * (0.85f + 0.25f * fm_minf(energy, 1.0f));
    if (rec_i > 0 && r->fbar != 0) {
        const fbar_t *pb = rbar(&tu.rec[rec_i - 1]);
        newbar = pb->root[pb->n - 1] != b->root[0] || pb->q[pb->n - 1] != b->q[0];
    }
    if (newbar)
        bass_aim = -1;
    if (role == R_COUNT || (role == R_INTRO && tu.intro == IN_GROOVE && r->sub < 2))
        return;
    if (role == R_FINAL) {
        int at = (r->fl & BF_BASIE) ? 24 : 0;
        pn(I_BASS, at, tu.ending == E_STOP ? 6 : 48 - at, oct_near(b->root[0], 36, 29, 41), 0.9f, 0);
        return;
    }
    if (role == R_TAG && r->sub == 99) {         /* the amen: IV, iv, held */
        pn(I_BASS, 0, 22, oct_near(b->root[0], 38, 31, 43), vel, 0);
        pn(I_BASS, 24, 22, oct_near(b->root[1], 38, 31, 43), vel * 0.9f, 0);
        return;
    }
    switch (tu.kind) {
    case K_GROOVE: groove_bass(b, nb, vel, (r->fl & BF_SEND) != 0); break;
    case K_MINOR: slow12(b, nb, vel); break;
    case K_GOSPEL: two_feel(b, nb, vel * 0.9f, 1); break;
    default:
        if (r->fl & BF_TWO)
            two_feel(b, nb, vel, 0);
        else
            walk(b, nb, newbar, vel);
        break;
    }
}

/* ------------------------------------------------------------- voicings --- */
/* the organ's chord: rootless A or B, voice-led from the last, in the lower manual's middle */
static void voice(int root, int q)
{
    int best[5], bn = 0, bs = 1 << 30, f, o, i, k;
    lh_root = root;
    for (f = 0; f < 2; f++) {
        const int8_t *iv = f ? Q_VB[q] : Q_VA[q];
        for (o = 36; o <= 72; o += 12) {
            int n[4], lo, d = 0;
            for (i = 0; i < 4; i++)
                n[i] = o + ((root + 120) % 12) + iv[i];
            lo = n[0];
            if (lo < 50 || lo > 63)
                continue;
            for (i = 0; i < 4; i++) {
                int m = 99;
                for (k = 0; k < nlh; k++) {
                    int e = n[i] - lh[k];
                    e = e < 0 ? -e : e;
                    m = e < m ? e : m;
                }
                d += nlh ? m : 0;
            }
            d += ((n[0] + n[3]) / 2 - 62 < 0 ? 62 - (n[0] + n[3]) / 2 : (n[0] + n[3]) / 2 - 62);
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
/* the guitar's grip: a shell low (root, seventh, the tenth) or three notes up the neck (from the rootless forms) */
static void gvoice(int root, int q, int shell)
{
    int i, k, f, o, bs = 1 << 30, best[3] = {0, 0, 0};
    if (shell) {
        int r0 = oct_near(root, 45, 40, 51);
        gv[0] = r0;
        gv[1] = r0 + Q_7TH[q];
        gv[2] = r0 + 12 + Q_3RD[q];
        ngv = 3;
        return;
    }
    for (f = 0; f < 2; f++) {
        const int8_t *iv = f ? Q_VB[q] : Q_VA[q];
        for (o = 48; o <= 72; o += 12) {
            int n[3], d = 0;
            for (i = 0; i < 3; i++)
                n[i] = o + ((root + 120) % 12) + iv[i + (f ? 1 : 0)];
            if (n[0] < 55 || n[2] > 74)
                continue;
            for (i = 0; i < 3; i++) {
                int m = 99;
                for (k = 0; k < ngv; k++) {
                    int e = n[i] - gv[k];
                    e = e < 0 ? -e : e;
                    m = e < m ? e : m;
                }
                d += ngv ? m : 0;
            }
            d += (n[1] > 64 ? n[1] - 64 : 64 - n[1]) + tirand(2);
            if (d < bs) {
                bs = d;
                for (i = 0; i < 3; i++)
                    best[i] = n[i];
            }
        }
    }
    if (bs < (1 << 30)) {
        for (i = 0; i < 3; i++)
            gv[i] = best[i];
        ngv = 3;
    }
}
static void strum(int tick, int dur, float vel, int inst)   /* the grip, strummed low to high */
{
    int i;
    for (i = 0; i < ngv; i++)
        pn(inst, tick, dur, gv[i], vel * (i == ngv - 1 ? 1.05f : 0.92f), (i & 7) << 4);
}
static void organ_chord(int tick, int dur, float vel)
{
    int i;
    for (i = 0; i < nlh; i++)
        pn(I_CMP, tick, dur, lh[i], vel, 0);
}

/* ------------------------------------------------------------- comping --- */
/* the organ behind the guitar: held chords that breathe with the swell pedal, a push now and then */
static const char *const OCP[] = {"x-------", "x---....", "x--x----", "...x----", "x-----.x", "x---x---", ".x------"};
static const char *const OCP_G[] = {"x-------", "x-----.x", "......x-", "x---..x-"};
static void comp_organ(const rec_t *r, const fbar_t *b, const fbar_t *nb, float vel)
{
    const char *s;
    int e, second = 0;
    if (tu.kind == K_GROOVE)
        s = (r->sub & 1) ? OCP_G[1 + tirand(3)] : OCP_G[0];
    else if (tu.kind == K_MINOR || tu.kind == K_GOSPEL)
        s = b->n == 2 ? "x---x---" : "x-------";
    else
        s = OCP[tirand(NA(OCP))];
    for (e = 0; e < 8; e++) {
        int len = 1, t = e * 6, ant;
        const fbar_t *cb = b;
        int ct = t;
        if (s[e] != 'x')
            continue;
        while (e + len < 8 && s[e + len] == '-')
            len++;
        ant = (e & 1) && (e == 7 || (b->n == 2 && e == 3));   /* an anticipation plays the next chord */
        if (e == 7)
            cb = nb, ct = 0;
        else if (ant)
            ct = t + 6;
        if (e >= 4 || (ant && e == 3))
            second = 1;
        voice(ch_root(cb, ct), ch_q(cb, ct));
        organ_chord(t, len * 6 - 1 + (e == 7 ? 6 : 0), vel * (ant ? 1.05f : 1.0f));
    }
    if (b->n == 2 && !second) {
        voice(b->root[1], b->q[1]);
        organ_chord(24, 23, vel * 0.95f);
    }
}

/* the guitar behind the organ: stabs, four to the bar, the shuffle's backbeat, the groove's chanks */
static const char *const CP_SPARSE[] = {"x---....", "...x-...", "........", ".......x", "x-......", "...x....", "......x-"};
static const char *const CP_EVEN[] = {"x--x-...", ".x-..x-.", "...x...x", "x---....", "...x-..x", "..x...x.", ".......x", ".x...x.."};
static const char *const CHANK[] = {"....x..x....x..x", "..x...x...x...x.", "....x.x.....x.x.", "....x.......x..x"};
static void comp_gtr(const rec_t *r, const fbar_t *b, const fbar_t *nb, float vel)
{
    int e, k = tu.kind;
    (void)r;
    if (k == K_GROOVE) {                         /* chanks: short, high, on the sixteenths */
        const char *s = CHANK[(tu.drum_pat + (r->sub & 1)) % NA(CHANK)];
        for (e = 0; e < 16; e++)
            if (s[e] == 'x') {
                int t = e * 3;
                gvoice(ch_root(b, t), ch_q(b, t), 0);
                strum(t, 2, vel * (e % 4 == 0 ? 0.9f : 0.75f), I_GCH);
            }
        return;
    }
    if (k == K_SHUFFLE) {
        if (tu.head != HD_GTR && (r->role == R_HEAD || r->role == R_HEADOUT) && tu.gtr_comp != 0) {
            /* the low-string boogie: root and fifth, root and sixth, the shuffle's long-short */
            int i, r0 = oct_near(ch_root(b, 0), 45, 40, 50);
            for (i = 0; i < 4; i++) {
                int t = i * 12, rt = b->n == 2 && t >= 24 ? oct_near(b->root[1], 45, 40, 50) : r0;
                int six = (i & 1) ? 9 : 7;
                pn(I_GCH, t, 7, rt, vel * 0.8f, 0);
                pn(I_GCH, t, 7, rt + 7, vel * 0.7f, 1 << 4);
                pn(I_GCH, t + 8, 4, rt, vel * 0.6f, FL_RAW);
                pn(I_GCH, t + 8, 4, rt + six, vel * 0.55f, FL_RAW | (1 << 4));
            }
            return;
        }
        for (e = 1; e < 4; e += 2) {             /* the backbeat chop */
            gvoice(ch_root(b, e * 12), ch_q(b, e * 12), 0);
            strum(e * 12, 3, vel * 0.85f, I_GCH);
        }
        if (trand() < 0.3f) {
            gvoice(nb->root[0], nb->q[0], 0);
            strum(44, 4, vel * 0.8f, I_GCH);
        }
        return;
    }
    if (k == K_MINOR) {                          /* 12/8: the chord on one, a soft arpeggio of triplets on three */
        gvoice(ch_root(b, 0), ch_q(b, 0), 0);
        strum(0, 20, vel * 0.7f, I_GCH);
        if (trand() < 0.6f) {
            int i;
            gvoice(ch_root(b, 24), ch_q(b, 24), 0);
            for (i = 0; i < 3; i++)
                pn(I_GCH, 24 + 4 * i, 4, gv[i], vel * (0.55f + 0.05f * (float)i), FL_RAW);
        }
        return;
    }
    if (k == K_GOSPEL) {                         /* the ballad: a chord on one, rolled; triplets up on three */
        int i;
        gvoice(ch_root(b, 0), ch_q(b, 0), 0);
        strum(0, 22, vel * 0.6f, I_GCH);
        gvoice(ch_root(b, 24), ch_q(b, 24), 0);
        for (i = 0; i < 3; i++)
            pn(I_GCH, 24 + 4 * i, 8, gv[i], vel * (0.42f + 0.06f * (float)i), FL_RAW);
        return;
    }
    if (tu.gtr_comp == 2 && k != K_SWING) {      /* four to the bar: shells, short, two and four leaned on */
        for (e = 0; e < 4; e++) {
            gvoice(ch_root(b, e * 12), ch_q(b, e * 12), 1);
            strum(e * 12, 4, vel * ((e & 1) ? 0.85f : 0.7f), I_GCH);
        }
        return;
    }
    {
        const char *s = tu.gtr_comp == 0 ? CP_SPARSE[tirand(NA(CP_SPARSE))]
                        : trand() < 0.3f ? CP_SPARSE[tirand(NA(CP_SPARSE))]
                                         : CP_EVEN[tirand(NA(CP_EVEN))];
        for (e = 0; e < 8; e++) {
            int len = 1, t = e * 6, ct = t;
            const fbar_t *cb = b;
            if (s[e] != 'x')
                continue;
            while (e + len < 8 && s[e + len] == '-')
                len++;
            if (e == 7)
                cb = nb, ct = 0;
            gvoice(ch_root(cb, ct), ch_q(cb, ct), 0);
            strum(t, len * 6 - 2, vel, I_GCH);
        }
    }
}

/* --------------------------------------------------------------- solos --- */
enum { SM_LICKS, SM_LINE, SM_SHOUT };
static const lick_t SHOUT4[] = {                 /* the climax: riffs to hammer, double-stops and the organ's trill */
    {5, {0, 6, 12, 18, 24}, {12, 12, 10, 12, 7}, {4, 4, 6, 6, 18}, {2, 2, 0, 2, 0}},
    {7, {0, 4, 8, 12, 16, 20, 24}, {15, 12, 15, 12, 15, 12, 12}, {4, 4, 4, 4, 4, 4, 18}, {0}},
    {5, {6, 12, 24, 30, 36}, {12, 12, 12, 10, 7}, {4, 6, 4, 6, 12}, {2, 2, 2, 2, 0}},
    {6, {0, 6, 12, 18, 24, 30}, {7, 10, 12, 15, 12, 10}, {6, 6, 6, 6, 6, 14}, {0, 0, 0, 1, 0, 0}},
    {4, {0, 12, 18, 30}, {12, 12, 15, 12}, {10, 4, 10, 14}, {2, 2, 1, 2}},
    {6, {0, 4, 8, 12, 24, 36}, {12, 15, 12, 10, 12, 7}, {4, 4, 4, 10, 10, 10}, {0, 1, 0, 0, 2, 0}},
};
/* the tails between the riffs: a short answer, low */
static const lick_t TAIL4[] = {
    {3, {0, 6, 12}, {3, 4, 0}, {6, 6, 22}, {1, 0, 0}},
    {4, {0, 6, 12, 18}, {10, 7, 6, 5}, {6, 6, 6, 18}, {0}},
    {2, {6, 12}, {7, 0}, {6, 28}, {0}},
};
/* words: a short phrase after a call */
static const lick_t WORD4[] = {
    {2, {0, 6}, {3, 0}, {4, 16}, {1, 0}},
    {3, {0, 6, 12}, {7, 10, 7}, {6, 6, 16}, {0}},
    {3, {0, 4, 8}, {12, 10, 7}, {4, 4, 16}, {0}},
    {2, {0, 12}, {10, 7}, {10, 18}, {0}},
};
static const lick_t WORD12[] = {
    {2, {0, 8}, {3, 0}, {8, 14}, {1, 0}},
    {3, {0, 4, 8}, {7, 10, 7}, {4, 4, 14}, {0}},
    {2, {0, 12}, {12, 10}, {10, 12}, {2, 0}},
};
static int solo_mode(const rec_t *r, int gtr)
{
    if ((r->fl & BF_PEAK) || (gtr && r->chorus == r->nch - 1 && r->nch >= 3))
        return SM_SHOUT;
    if (r->role == R_TRADE_O || r->role == R_TRADE_G)
        return SM_LICKS;
    if (tu.kind == K_GOSPEL)                     /* the ballad: words first, then the line sings */
        return r->fbar < 8 ? SM_LICKS : SM_LINE;
    if (tu.kind == K_SWING)
        return SM_LINE;
    return r->chorus % 2 == 0 ? SM_LICKS : SM_LINE;
}
static void solo_params(const rec_t *r, line_t *L, int gtr)
{
    const style_t *st = &STYLES[style_i];
    float I = pf(P_INTENS), prog = (float)r->sub / (float)(r->nch * tu.nbars + 1);
    L->lo = gtr ? 55 : 62;
    L->hi = (gtr ? 76 : 82) + (int)(4.0f * prog);
    L->dens = fm_clampf(0.15f + 0.3f * I + 0.28f * prog + 0.3f * (float)st->lines * 0.01f, 0.15f, 0.92f);
    L->trip = (tu.feel == F_TRIP ? 0.18f : 0.05f) + 0.08f * I;
    L->arp = 0.10f;
    L->quarter = 0.12f * (1.0f - L->dens);
    L->dt = tu.feel == F_STRAIGHT ? 0.12f + 0.1f * I : en_bpm < 100 ? 0.05f + 0.14f * I * (0.3f + prog) : 0.0f;
    if (tu.feel == F_TRIP)
        L->dt = 0.0f;
    L->vel = 0.66f + 0.2f * fm_minf(energy, 1.0f);
    L->inst = gtr ? I_GTR : I_RH;
    L->blues = blues_kind();
}
/* one bar of a solo: the chorus's licks (a call, the call again, the answer: AAB), a line, or the climax */
static void solo_bar(const rec_t *r, const fbar_t *b, const fbar_t *nb, line_t *L, int gtr)
{
    int mode = solo_mode(r, gtr), i = r->fbar, nb4 = tu.nbars, ph = (i % 4), line = i / 4;
    int T = mel_tonic(gtr ? 57 : 63), *la = gtr ? &la_g : &la_o, *lb = gtr ? &lb_g : &lb_o, trip = tu.feel == F_TRIP;
    const lick_t *LK = trip ? LICK12 : LICK4;
    float v = L->vel;
    if (r->role == R_TRADE_O || r->role == R_TRADE_G) {
        line = (r->sub % 4) / 2;
        ph = r->sub % 2 ? 2 : 0;
    }
    if ((r->fl & BF_PSTART) || (i == 0 && mode != SM_LINE) || ((r->role == R_TRADE_O || r->role == R_TRADE_G) && r->sub % 4 == 0)) {
        *la = trip ? CALL12[tirand(NA(CALL12))] : CALL4[tirand(NA(CALL4))];
        do
            *lb = trip ? ANS12[tirand(NA(ANS12))] : ANS4[tirand(NA(ANS4))];
        while (*lb == *la);
        shout_i = tirand(NA(SHOUT4));
    }
    if (mode == SM_LINE) {
        line_bar(L, b, nb, tu.bpb);
        return;
    }
    L->left = 0;                                 /* (a line in progress ends with the chorus) */
    L->rest = 12;
    if (mode == SM_SHOUT) {
        int hold = nb4 >= 12 && (i == nb4 - 4 || (nb4 == 32 && i == 24));
        if (hold) {                              /* the hold: a high chord leaned on for two bars, the speaker whirling */
            uint16_t m = rot(Q_CT[ch_q(b, 0)] | Q_COLOR[ch_q(b, 0)], ch_root(b, 0));
            int top = near_in(T + 12 + 3, m), mid = step_in(top, -1, m), low = step_in(mid, -1, m);
            if (gtr) {
                top -= 12, mid -= 12, low -= 12;
            }
            emit(L->inst, 0, 90, top, v * 1.05f, 0);
            emit(L->inst, 0, 90, mid, v, 0);
            emit(L->inst, 0, 90, low, v * 0.95f, 0);
            return;
        }
        if (i == nb4 - 3)                        /* (still held) */
            return;
        if (i >= nb4 - 2 || (nb4 == 32 && i >= 16 && i < 24)) {   /* down from it: the answer */
            if (ph == 2 || ph == 0)
                lick_put(&LK[*lb], 0, b, nb, T, v, L->inst);
            return;
        }
        /* the riff hammered: twice, then a tail, then the riff again; a new riff for the next line */
        {
            int k = (shout_i + line) % NA(SHOUT4), tail = (ph == 3) || (ph == 1 && tirand(3) == 0);
            if (trip)
                lick_put(&LICK12[tail ? 2 : ((r->sub & 1) ? 1 : 9)], 0, b, nb, T, v * 1.05f, L->inst);
            else if (tail)
                lick_put(&TAIL4[tirand(NA(TAIL4))], 12 * tirand(2), b, nb, T, v * 0.95f, L->inst);
            else
                lick_put(&SHOUT4[k], 0, b, nb, T, v * (ph & 1 ? 1.0f : 1.06f), L->inst);
        }
        return;
    }
    /* the licks: lines one and two call (the second over IV), the third answers; words between */
    {
        int last_line = (i + 4 >= nb4), lk = last_line && nb4 <= 16 ? *lb : *la, off = (tirand(3) == 0 && !trip) ? 6 : 0;
        if (nb4 == 16 && line == 2)
            lk = *lb;
        if (ph == 0) {
            if (LK[lk].at[0] + off >= 12)
                off = 0;
            lick_put(&LK[lk], off, b, nb, T, v, L->inst);
        } else if (ph == 2 && trand() < 0.35f + 0.45f * L->dens) {   /* a short word after the call */
            if (trip)
                lick_put(&WORD12[tirand(NA(WORD12))], 24, b, nb, T, v * 0.88f, L->inst);
            else
                lick_put(&WORD4[tirand(NA(WORD4))], 12 + 12 * tirand(2), b, nb, T, v * 0.88f, L->inst);
        } else if (ph == 3 && last_line && trand() < 0.5f) {    /* the turnaround: a pickup into the next chorus */
            int nx = trip ? CALL12[tirand(NA(CALL12))] : CALL4[tirand(NA(CALL4))];
            int tg = T + LK[nx].o[0];
            emit(L->inst, 36, 4, fit(tg - 5, b, 36, 1), v * 0.8f, FL_RAW);
            emit(L->inst, 40, 4, fit(tg - 3, b, 40, 1), v * 0.82f, FL_RAW);
            emit(L->inst, 44, 4, tg - 1, v * 0.85f, FL_RAW);
            *la = nx;
        }
    }
}
/* the guitar's chorus as Wes built it: single notes, then octaves, then chords */
static void gtr_dress(int from, int how)
{
    int i, n = npl;
    for (i = from; i < n; i++) {
        pn_t p = pl[i];
        const fbar_t *b = rbar(&tu.rec[rec_i]);
        if (p.inst != I_GTR || p.dur < 2)
            continue;
        if (how == 1) {
            plan_note(I_GCH, p.tick, p.dur, p.note - 12, (int)((float)p.vel * 0.9f), p.fl);
        } else if (how == 2 && (p.dur >= 6 || p.tick % 12 == 0)) {
            uint16_t ct = m_strong(b, p.tick);
            int a = step_in(p.note, -1, ct), c = step_in(a, -1, ct);
            if (p.note - c < 5)
                c = step_in(c, -1, ct);
            plan_note(I_GCH, p.tick, p.dur, a, (int)((float)p.vel * 0.8f), p.fl | (1 << 4));
            plan_note(I_GCH, p.tick, p.dur, c, (int)((float)p.vel * 0.75f), p.fl);
        }
    }
}

/* ---------------------------------------------------------- the organist --- */
/* the speaker, the drawbars, the swell pedal: asked for at a bar's start (or within it) */
static void ask_speed(int fast, int tick)
{
    if (fast != lsl_asked) {
        pn(I_SPEED, tick, 1, fast, 0.0f, 0);
        lsl_asked = fast;
    }
}
static void ask_draw(int mode, int tick)
{
    if (mode != reg_asked) {
        pn(I_DRAW, tick, 1, mode, 0.0f, 0);
        reg_asked = mode;
    }
}
static void ask_swell(int level, int tick, int ticks)   /* the swell pedal to level % over so many ticks */
{
    pn(I_SWELL, tick, ticks, level, 0.0f, 0);
}

static void head_notes(const rec_t *r, const rec_t *rn, const fbar_t *b, int inst, int shift, int octaves, float vg)
{
    int s = b->sect, i, t0 = tu.f[r->fbar].sbar * 48, t1 = t0 + 48;
    int cont = rn->role == R_HEAD || rn->role == R_HEADOUT;
    if (tu.kind != K_SWING && tu.kind != K_GOSPEL)
        s = 0, t0 = r->fbar * 48, t1 = t0 + 48;
    for (i = 0; i < tu.nmel[s]; i++) {
        const mn_t *m = &tu.mel[s][i];
        float v;
        if (m->tick < t0 || m->tick >= t1 || (m->pick && !cont))
            continue;
        v = (float)m->vel * (1.0f / 127.0f) * vg * (0.88f + 0.22f * fm_minf(energy, 1.0f));
        pn(inst, m->tick - t0, m->dur, m->note + shift, v, m->tick % 6 ? FL_RAW : 0);
        if (octaves)
            pn(I_GCH, m->tick - t0, m->dur, m->note + shift - 12, v * 0.88f, m->tick % 6 ? FL_RAW : 0);
        if (inst == I_RH && tu.kind == K_GOSPEL && m->dur >= 18) {   /* a long note: a chord tone under it */
            uint16_t ct = m_ct(b, m->tick - t0);
            int h = m->note - 3;
            while (h > m->note - 10 && !in_mask(h, ct))
                h--;
            if (h > m->note - 10)
                pn(I_RH, m->tick - t0, m->dur, h, v * 0.8f, 0);
        }
    }
}

static void gen_organ(const rec_t *r, const rec_t *rn, const fbar_t *b, const fbar_t *nb)
{
    int role = r->role, peak = (r->fl & BF_PEAK) != 0, k = tu.kind;
    float cv = 0.62f * (0.8f + 0.3f * fm_minf(energy, 1.0f));
    /* the speaker and the drawbars: fast and pulled out at the peak, back after; lifted behind a guitar's last
     * bars now and then */
    if (role == R_FINAL) {
        ask_speed(1, 0);
        ask_speed(0, tu.ending == E_AMEN ? 47 : 30);
        lsl_asked = 0;
    } else if (peak) {
        ask_speed(1, 0);
        ask_draw(k == K_GOSPEL ? 2 : 1, 0);
    } else if (role == R_GSOLO && lift_g && (r->fl & BF_LASTCH) && r->fbar >= tu.nbars - 4) {
        ask_speed(1, 0);
    } else if (role != R_COUNT) {
        ask_speed(0, 0);
        ask_draw(0, 0);
    }
    en_climax = (uint8_t)(peak && role != R_TAG);
    if (role == R_COUNT || (role == R_INTRO && tu.intro == IN_GROOVE))
        return;
    if (role == R_FINAL) {                       /* the last chord: held, the speaker spinning down under it */
        int i, at = 0, T = mel_tonic(67);
        float v = 0.8f;
        if (r->fl & BF_BASIE) {                  /* the plinks first, high */
            pn(I_RH, 0, 5, fit(T + 4, b, 0, 0), 0.62f, 0);
            pn(I_RH, 12, 5, fit(T + 4, b, 12, 0), 0.66f, 0);
            pn(I_RH, 0, 5, fit(T + 7, b, 0, 0), 0.6f, 0);
            pn(I_RH, 12, 5, fit(T + 7, b, 12, 0), 0.64f, 0);
            at = 24;
        }
        voice(b->root[0], b->q[0]);
        if (tu.ending == E_STOP) {
            for (i = 0; i < nlh; i++)
                pn(I_RH, 0, 5, lh[i] + 12, v, 0);
            return;
        }
        {
            int top = oct_near((b->root[0] + 2) % 12, T + 12, 74, 88), dup = 0;   /* its ninth on top */
            for (i = 0; i < nlh; i++) {
                pn(I_RH, at, 48 - at, lh[i] + 12, v * 0.92f, 0);
                dup |= lh[i] + 12 == top;
            }
            if (!dup && top > lh[nlh - 1] + 12)
                pn(I_RH, at, 48 - at, top, v, 0);
        }
        ask_swell(100, at, 12);
        return;
    }
    if (role == R_TAG && r->sub == 99) {         /* the amen: IV, then iv, held and swelled */
        voice(b->root[0], b->q[0]);
        organ_chord(0, 23, cv);
        pn(I_RH, 0, 23, oct_near((b->root[0] + 4) % 12, 76, 72, 84), cv * 1.1f, 0);
        voice(b->root[1], b->q[1]);
        organ_chord(24, 23, cv);
        pn(I_RH, 24, 23, oct_near((b->root[1] + 3) % 12, 76, 72, 84), cv * 1.1f, 0);
        ask_swell(60, 0, 2);
        ask_swell(100, 4, 40);
        return;
    }
    /* the right hand */
    if ((role == R_HEAD || role == R_HEADOUT) && tu.head != HD_GTR) {
        if (k == K_GOSPEL) {                     /* the hymn's breath: down at the bar, up through it */
            ask_swell(peak ? 85 : 62, 0, 4);
            ask_swell(peak ? 100 : 86, 12, 30);
        } else {
            ask_swell(peak ? 100 : 86, 0, 12);
        }
        {
            int from = npl, i;
            head_notes(r, rn, b, I_RH, 0, 0, 1.0f);
            for (i = from; lift_h && i < npl; i++)   /* a long note: the speaker up under it */
                if (pl[i].inst == I_RH && pl[i].dur >= 36 && k != K_GROOVE) {
                    ask_speed(1, pl[i].tick + 6);
                    break;
                }
        }
        return;
    }
    if (role == R_OSOLO || role == R_TRADE_O || role == R_TAG) {
        if ((r->fl & BF_PSTART) && (role != R_TRADE_O || r->sub == 0)) {
            Lo.left = 0;
            Lo.rest = role == R_TAG ? 12 : 6;
            Lo.nphr = 0;
        }
        solo_params(r, &Lo, 0);
        if (role == R_TAG)
            Lo.dens = 0.3f;
        if (role == R_OSOLO && nxt(2)->role != R_OSOLO && Lo.left > 24)   /* the solo winds down */
            Lo.left = 24;
        ask_swell(peak ? 100 : 80 + (int)(15.0f * fm_minf(energy, 1.0f)), 0, peak ? 24 : 48);
        solo_bar(r, b, nb, &Lo, 0);
        return;
    }
    if (role == R_INTRO && tu.intro == IN_ORGAN) {
        if (k == K_GOSPEL) {                     /* the church: chords held and swelled, a turn on top */
            int i, T = mel_tonic(64);
            static const int8_t TURN[5] = {4, 2, 0, -3, 0};
            voice(b->root[0], b->q[0]);
            organ_chord(0, b->n == 2 ? 23 : 47, cv);
            if (b->n == 2) {
                voice(b->root[1], b->q[1]);
                organ_chord(24, 23, cv);
            }
            ask_swell(55, 0, 2);
            ask_swell(92, 6, 36);
            if (r->sub == 1)
                for (i = 0; i < 5; i++)
                    pn(I_RH, 6 * i + 12, i == 4 ? 12 : 6, fit(T + TURN[i], b, 6 * i + 12, 1), 0.7f, 0);
            else
                pn(I_RH, 0, 46, oct_near((b->root[0] + Q_3RD[b->q[0]]) % 12, 76, 72, 84), 0.7f, 0);
            return;
        }
        Lo.vel = 0.75f;
        solo_params(r, &Lo, 0);
        solo_bar(r, b, nb, &Lo, 0);             /* the blues: a lick over the turnaround, the bass under it */
        return;
    }
    if (role == R_INTRO && tu.intro == IN_PICKUP && tu.head != HD_GTR) {
        if (r->sub == 1) {
            int T = mel_tonic(63), nx = CALL12[tirand(NA(CALL12))], tg = T + LICK12[nx].o[0];
            emit(I_RH, 32, 4, fit(tg - 5, b, 32, 1), 0.6f, FL_RAW);
            emit(I_RH, 36, 4, fit(tg - 3, b, 36, 1), 0.62f, FL_RAW);
            emit(I_RH, 40, 4, fit(tg - 2, b, 40, 1), 0.64f, FL_RAW);
            emit(I_RH, 44, 4, tg - 1, 0.66f, FL_RAW);
        }
        comp_organ(r, b, nb, cv * 0.8f);
        return;
    }
    /* behind the guitar (its solo, its head) and in the intros: comping on the lower manual */
    if (role == R_HEAD || role == R_HEADOUT || role == R_GSOLO || role == R_TRADE_G || role == R_INTRO) {
        int phrase_end = (r->fl & BF_SEND) != 0;
        if (tu.kind == K_GROOVE && (r->sub & 1) == 0)
            ask_swell(58, 0, 2);
        else if (!phrase_end)
            ask_swell(64, 0, 12);
        if (phrase_end)
            ask_swell(84, 12, 30);              /* the swell into the next phrase */
        if (role == R_TRADE_G)
            return;                              /* (the guitar's four alone over bass and drums) */
        comp_organ(r, b, nb, cv);
    }
}

static void gen_guitar(const rec_t *r, const rec_t *rn, const fbar_t *b, const fbar_t *nb)
{
    int role = r->role, from = npl;
    float gv_ = 0.62f * (0.8f + 0.3f * fm_minf(energy, 1.0f));
    if (role == R_COUNT || (role == R_INTRO && (tu.intro == IN_ORGAN || (tu.intro == IN_GROOVE && r->sub < 2))))
        return;
    if (role == R_FINAL) {                       /* a ninth chord, rolled slowly */
        if (tu.ending == E_STOP) {
            gvoice(b->root[0], b->q[0], 0);
            strum(0, 4, 0.75f, I_GCH);
            return;
        }
        gvoice(b->root[0], b->q[0], 0);
        strum((r->fl & BF_BASIE) ? 24 : 0, 40, 0.6f, I_GCH);
        return;
    }
    if (role == R_TAG && r->sub == 99) {
        int i;
        gvoice(b->root[0], b->q[0], 0);
        for (i = 0; i < 3; i++)
            pn(I_GCH, 4 * i, 20, gv[i], 0.5f, FL_RAW);
        gvoice(b->root[1], b->q[1], 0);
        for (i = 0; i < 3; i++)
            pn(I_GCH, 24 + 4 * i, 20, gv[i], 0.48f, FL_RAW);
        return;
    }
    if ((role == R_HEAD || role == R_HEADOUT) && tu.head != HD_ORGAN) {
        if (tu.head == HD_GTR)                   /* the melody in octaves */
            head_notes(r, rn, b, I_GTR, 0, 1, 0.95f);
        else                                     /* with the organ, an octave under it */
            head_notes(r, rn, b, I_GTR, -12, 0, 0.9f);
        return;
    }
    if (role == R_INTRO && tu.intro == IN_PICKUP && tu.head == HD_GTR) {
        if (r->sub == 1) {
            int T = mel_tonic(58), nx = CALL12[tirand(NA(CALL12))], tg = T + LICK12[nx].o[0];
            emit(I_GTR, 36, 4, fit(tg - 5, b, 36, 1), 0.62f, FL_RAW);
            emit(I_GTR, 40, 4, fit(tg - 3, b, 40, 1), 0.64f, FL_RAW);
            emit(I_GTR, 44, 4, tg - 1, 0.66f, FL_RAW);
        }
        gvoice(ch_root(b, 0), ch_q(b, 0), 0);
        strum(0, 20, 0.45f, I_GCH);
        return;
    }
    if (role == R_GSOLO || role == R_TRADE_G) {
        int how = 0;
        if ((r->fl & BF_PSTART) && (role != R_TRADE_G || r->sub == 4)) {
            Lg.left = 0;
            Lg.rest = 6;
            Lg.nphr = 0;
        }
        solo_params(r, &Lg, 1);
        if (role == R_GSOLO) {
            how = r->nch >= 3 ? (r->chorus >= 2 ? 2 : r->chorus) : r->nch == 2 ? r->chorus : 0;
            if (how == 1)                        /* octaves: fewer notes, higher */
                Lg.dens *= 0.7f, Lg.lo += 5, Lg.hi += 5, Lg.dt = 0.0f, Lg.trip *= 0.5f;
            else if (how == 2)
                Lg.dens *= 0.6f, Lg.lo += 7, Lg.hi += 6, Lg.dt = 0.0f, Lg.trip = 0.0f;
            if (nxt(2)->role != R_GSOLO && Lg.left > 24)
                Lg.left = 24;
        }
        solo_bar(r, b, nb, &Lg, 1);
        if (how)
            gtr_dress(from, how);
        return;
    }
    if (role == R_TRADE_O)
        return;                                  /* (the organ's four alone) */
    comp_gtr(r, b, nb, gv_ * (role == R_INTRO ? 0.8f : 1.0f));
}

/* ------------------------------------------------------------ the beat --- */
static float tempo_now(const rec_t *r, int beat)
{
    float b = tu.bpm * fm_exp2f((pf(P_TEMPO) - 0.5f) * 0.83f);
    if (r->fl & BF_RIT) {                        /* the last bars before the last chord: slowing */
        float pos = ((float)((rec_i - tu.rit_rec) * tu.bpb + beat)) / (float)(tu.nrit * tu.bpb);
        b *= 1.0f - 0.36f * fm_clampf(pos, 0.0f, 1.0f);
    }
    if (r->role == R_FINAL && !(r->fl & BF_BASIE) && tu.ending != E_STOP)
        b *= 0.42f;
    return fm_clampf(b, 30.0f, 330.0f);
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
    int i, nc = ncarry;
    fin_bar.n = 1;
    fin_bar.root[0] = (int8_t)tu.fin_root;
    fin_bar.q[0] = (uint8_t)tu.fin_q;
    fin_bar.sect = 0;
    fin_bar.sbar = 0;
    amen_bar.n = 2;                              /* IV6, iv6 */
    amen_bar.root[0] = amen_bar.root[1] = (int8_t)((tu.key + 5) % 12);
    amen_bar.q[0] = Q_MAJ6;
    amen_bar.q[1] = Q_M6;
    amen_bar.sect = 1;
    amen_bar.sbar = 0;
    b = rbar(r);
    nb = rbar(rn);
    switch (r->role) {
    case R_HEAD: e = 0.45f; break;
    case R_HEADOUT: e = 0.55f; break;
    case R_OSOLO: case R_GSOLO: e = 0.48f + 0.42f * prog; break;
    case R_TRADE_O: case R_TRADE_G: e = 0.85f; break;
    case R_TAG: e = 0.6f; break;
    case R_INTRO: e = 0.38f; break;
    default: e = 0.5f; break;
    }
    if (r->fl & BF_PEAK)
        e = fm_maxf(e, 0.95f);
    energy = fm_clampf(e * (0.6f + 0.85f * I), 0.1f, 1.2f);
    en_role = r->role;
    en_soloist = (uint8_t)(r->role == R_OSOLO || r->role == R_TRADE_O ? 1 : r->role == R_GSOLO || r->role == R_TRADE_G ? 2 : 0);
    en_bar = r->fbar;
    en_chorus = r->chorus;
    en_nchorus = r->nch;
    if ((r->fl & BF_PSTART) && rec_i > 0) {      /* a solo just ended: the room applauds a little */
        int pr = tu.rec[rec_i - 1].role;
        if ((pr == R_OSOLO && trand() < 0.6f) || (pr == R_GSOLO && trand() < 0.5f) || pr == R_TRADE_O || pr == R_TRADE_G)
            appl_small = 0.5f;
    }
    npl = 0;
    ncarry = 0;
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
    for (i = 0; i < nc; i++)                     /* a lick's notes over the barline */
        plan_note(carry[i].inst, carry[i].tick, carry[i].dur, carry[i].note, carry[i].vel, carry[i].fl);
    gen_drums(r, b);
    gen_bass(r, b, nb);
    gen_organ(r, rn, b, nb);
    gen_guitar(r, rn, b, nb);
}

static void play_reset(void)
{
    rec_i = 0;
    beat_i = 0;
    beat_left = 0.0f;
    npl = 0;
    ncarry = 0;
    memset_l(&Lo, sizeof Lo);
    memset_l(&Lg, sizeof Lg);
    Lo.pend = Lg.pend = -1;
    Lo.p = 72;
    Lg.p = 64;
    bass_aim = -1;
    fill_prev = 0;
    nlh = ngv = 0;
    lift_g = trand() < 0.5f;
    lift_h = trand() < 0.6f;
    lsl_asked = -1;
    reg_asked = -1;
    en_climax = 0;
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
    if (r->role != R_FINAL)                      /* (the held last chord: the tune's tempo stays on screen) */
        en_bpm = (uint16_t)(bpm + 0.5f);
    beat_len = EN_SR * 60.0f / bpm;
    switch (tu.feel) {                           /* the eighth notes' lilt */
    case F_STRAIGHT: swing_r = 0.5f + (pf(P_SWING) - 0.5f) * 0.1f; break;
    case F_SHUFFLE: case F_TRIP: swing_r = 0.667f + (pf(P_SWING) - 0.5f) * 0.05f; break;
    default:
        swing_r = fm_clampf(0.72f - (bpm - 60.0f) * 0.00085f, 0.53f, 0.72f) + (pf(P_SWING) - 0.5f) * 0.14f;
        break;
    }
    swing_r = fm_clampf(swing_r, 0.47f, 0.78f);
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
        switch (p->inst) {                       /* where each sits on the beat: the ride ahead, the organ behind */
        case I_RIDE: case I_BELL: ms = -3.0f; break;
        case I_HAT: case I_OHAT: ms = -1.0f; break;
        case I_BASS: ms = -1.0f; break;
        case I_RH: ms = r->role == R_OSOLO ? 7.0f : 4.0f; break;
        case I_CMP: ms = 3.0f; break;
        case I_GTR: ms = 4.0f; break;
        case I_GCH: ms = 3.0f + (float)(p->fl >> 4) * (tu.kind == K_GOSPEL || r->role == R_FINAL ? 28.0f : 7.0f); break;
        case I_SWELL: case I_SPEED: case I_DRAW: ms = 0.0f; break;
        default: break;
        }
        if (p->inst <= I_GCH)
            ms += (frand() * 2.0f - 1.0f) * 4.0f;
        else if (p->inst < I_SWELL)
            ms += (frand() * 2.0f - 1.0f) * 2.5f;
        off = swing_map(p->tick - t0, p->fl & 3) * beat_len + ms * 44.1f;
        gate = (int32_t)((float)p->dur * (1.0f / 12.0f) * beat_len);
        if (p->inst == I_RH && p->dur >= 4)      /* the organist's legato: each key up as the next goes down */
            gate += (int32_t)(0.012f * EN_SR);
        if (p->inst == I_SWELL)
            gate = gate > 64 ? gate : 64;
        sched(p->inst, (int32_t)off, p->note, (float)p->vel * (1.0f / 127.0f), gate);
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
            en_climax = 0;
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
        if (appl_t > 1.1f)
            tgt = appl_peak * fm_clampf((appl_t - 1.1f) * 2.0f, 0.0f, 1.0f) * fm_clampf(left / 2.2f, 0.0f, 1.0f);
    }
    if (appl_small > 0.0f) {
        tgt = fm_maxf(tgt, appl_small > 0.3f ? 0.45f : appl_small * 1.5f);
        appl_small -= dt * 0.25f;
    }
    appl_lvl += (tgt - appl_lvl) * fm_minf(dt * 4.0f, 1.0f);
    en_applause = appl_lvl;
}
