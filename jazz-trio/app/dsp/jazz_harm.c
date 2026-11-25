/* SPDX-License-Identifier: MPL-2.0
 * JAZZ TRIO: ashtonwallen, 2026 */
/* The harmony (part of jazz.c): chord qualities and their voicings, scales and tensions; the forms' changes as
 * curated templates (written as chord symbols over the key: "2- 57 | 1^"); the tunes' titles. */

/* ------------------------------------------------------- chord qualities --- */
enum { Q_MAJ7, Q_MAJ6, Q_M7, Q_M6, Q_DOM, Q_ALT, Q_B9, Q_SUS, Q_HD, Q_DIM, Q_NQ };
static const char *const Q_NAME[Q_NQ] = {"maj7", "6", "m7", "m6", "7", "7alt", "7b9", "7sus", "m7b5", "dim7"};
/* the four chord tones, semitones above the root */
static const int8_t Q_TONE[Q_NQ][4] = {
    {0, 4, 7, 11}, {0, 4, 7, 9}, {0, 3, 7, 10}, {0, 3, 7, 9}, {0, 4, 7, 10},
    {0, 4, 8, 10}, {0, 4, 7, 10}, {0, 5, 7, 10}, {0, 3, 6, 10}, {0, 3, 6, 9},
};
/* masks of pitch classes above the root (bit i: i semitones up): the chord tones, the tensions a player may land
 * on (9ths, 11ths, 13ths, the altered ones), the scale, the bebop scale (the scale and its passing tone) */
#define M(a) (1u << (a))
static const uint16_t Q_CT[Q_NQ] = {
    M(0) | M(4) | M(7) | M(11), M(0) | M(4) | M(7) | M(9), M(0) | M(3) | M(7) | M(10), M(0) | M(3) | M(7) | M(9),
    M(0) | M(4) | M(7) | M(10), M(0) | M(4) | M(8) | M(10), M(0) | M(4) | M(7) | M(10), M(0) | M(5) | M(7) | M(10),
    M(0) | M(3) | M(6) | M(10), M(0) | M(3) | M(6) | M(9)};
static const uint16_t Q_COLOR[Q_NQ] = {M(2), M(2), M(2) | M(5), M(2), M(2) | M(9), M(1) | M(3), M(1) | M(8),
                                       M(2) | M(9), M(5), M(2)};
static const uint16_t Q_SCALE[Q_NQ] = {
    M(0) | M(2) | M(4) | M(5) | M(7) | M(9) | M(11),          /* ionian */
    M(0) | M(2) | M(4) | M(5) | M(7) | M(9) | M(11),
    M(0) | M(2) | M(3) | M(5) | M(7) | M(9) | M(10),          /* dorian */
    M(0) | M(2) | M(3) | M(5) | M(7) | M(9) | M(11),          /* melodic minor */
    M(0) | M(2) | M(4) | M(5) | M(7) | M(9) | M(10),          /* mixolydian */
    M(0) | M(1) | M(3) | M(4) | M(6) | M(8) | M(10),          /* altered */
    M(0) | M(1) | M(4) | M(5) | M(7) | M(8) | M(10),          /* phrygian dominant */
    M(0) | M(2) | M(5) | M(7) | M(9) | M(10),                 /* sus: no third */
    M(0) | M(2) | M(3) | M(5) | M(6) | M(8) | M(10),          /* locrian #2 */
    M(0) | M(2) | M(3) | M(5) | M(6) | M(8) | M(9) | M(11),   /* whole-half */
};
static const uint16_t Q_BEBOP[Q_NQ] = {M(8), M(8), M(4), 0, M(11), 0, 0, M(11), 0, 0};   /* added passing tones */
/* rootless voicings (the bass has the root): A from the third, B from the seventh */
static const int8_t Q_VA[Q_NQ][4] = {
    {4, 7, 11, 14}, {4, 7, 9, 14}, {3, 7, 10, 14}, {3, 7, 9, 14}, {4, 9, 10, 14},
    {4, 8, 10, 15}, {4, 8, 10, 13}, {5, 9, 10, 14}, {3, 6, 10, 12}, {3, 6, 9, 12},
};
static const int8_t Q_VB[Q_NQ][4] = {
    {11, 14, 16, 19}, {9, 14, 16, 19}, {10, 14, 15, 19}, {9, 14, 15, 19}, {10, 14, 16, 21},
    {10, 13, 16, 20}, {10, 13, 16, 20}, {10, 14, 17, 21}, {10, 12, 15, 18}, {9, 12, 15, 18},
};
static const int8_t Q_3RD[Q_NQ] = {4, 4, 3, 3, 4, 4, 4, 5, 3, 3};
static const int8_t Q_7TH[Q_NQ] = {11, 9, 10, 9, 10, 10, 10, 10, 10, 9};
static int q_is_dom(int q) { return q == Q_DOM || q == Q_ALT || q == Q_B9 || q == Q_SUS; }
static int q_is_minor(int q) { return q == Q_M7 || q == Q_M6 || q == Q_HD || q == Q_DIM; }

const char *en_chord_name(int root, int q, char *buf)
{
    put_s(put_s(buf, EN_NOTE_NAME[(root % 12 + 12) % 12]), Q_NAME[q < 0 || q >= Q_NQ ? 0 : q]);
    return buf;
}

/* --------------------------------------------------------------- forms --- */
/* templates: bars split by '|', chords by spaces, each a degree over the key ("b7": a whole step under the tonic;
 * a minor key's degrees still count from its tonic) and a quality: ^ maj7, 6 maj6, - m7, -6 m6, 7 dom7, 7a alt,
 * 7b 7b9, 7s sus, o half-diminished, d diminished. A bar's chords share it evenly. */

/* major A sections: six bars, each leading home to I at the seventh */
static const char *const A_MAJ[] = {
    "1^ | 5- 17 | 4^ | 4- b77 | 3- 67 | 2- 57",           /* up to IV, the backdoor, iii VI ii V */
    "1^ | 37b | 6- | 27 | 2- | 57",                       /* I III7 vi II7, the long ii V */
    "2- 57 | 3- 67 | 2- 57 | 1^ | b3- b67 | b6^ b27",     /* ii-V chains, bII7 home */
    "1^ | #4o 77b | 3- 67 | 2- 57 | 3- b37 | 2- b27",     /* down by half steps */
    "1^ 6- | 2- 57 | 1^ 17 | 4^ #4d | 3- 67 | 2- 57",     /* I vi ii V, IV and the #iv diminished */
    "1^ | 1^ | 2- | 57 | 1^ | 4- b77",                    /* the long ii V, the backdoor */
    "1^ | 4- b77 | 1^ | 5- 17 | 4^ | 4- b77",             /* the backdoor twice */
};
/* their last two bars: turnarounds back to the top, or a close (before a bridge, at the end) */
static const char *const TURN_MAJ[] = {"1^ 6- | 2- 57", "3- 67 | 2- 57", "1^ b37 | b67 b27", "1^ 67b | 2- b27"};
static const char *const CLOSE_MAJ = "1^ | 1^";
/* bridges: eight bars, leading back to the A */
static const char *const B_MAJ[] = {
    "5- | 17 | 4^ | 4^ | 6- | 27 | 2- | 57",              /* to IV, then vi II7 ii V */
    "37 | 37 | 67 | 67 | 27 | 27 | 57 | 57",              /* dominants round the cycle */
    "3- 67 | 2^ | 2- 57 | 1^ | 1- 47 | b7^ | 2- | 57",    /* ii-Vs down by whole steps */
    "4^ | 4- b77 | 1^ | 6- | 2- 57 | 3- 67 | 2- | b27",   /* IV, the backdoor, home and round */
    "7o 37b | 6- | 7o 37b | 6- | 27 | 27 | 2- | 57",      /* to the relative minor */
};
/* minor: six-bar A sections home to i at the seventh, turnarounds, bridges */
static const char *const A_MIN[] = {
    "1- | 1- | 4- | b77 | b3^ | 2o 57b",                  /* i iv bVII bIII, the minor ii V */
    "1- | 2o 57b | 1- | 47 | b6^ | 2o 57b",               /* i, ii V, the dorian IV7, bVI */
    "1- | 1- | 6o 27b | 5- 17b | 4- | 2o 57b",            /* ii-Vs down a fifth at a time */
};
static const char *const TURN_MIN[] = {"1- 6o | 2o 57b", "1- b6^ | 2o 57a"};
static const char *const CLOSE_MIN = "1- | 1-";
static const char *const B_MIN[] = {
    "4- | b77 | b3^ | b6^ | 2o | 57b | 1- | 57b",
    "b6^ | b6^ | b2^ | b2^ | 2o | 57b | 1- | 57a",
};
/* blues: twelve bars */
static const char *const BLUES[] = {
    "17 | 47 | 17 | 5- 17 | 47 | #4d | 17 | 67b | 2- | 57 | 17 67b | 2- 57",
    "17 | 47 | 17 | 17 | 47 | 47 | 17 | 3- 67 | 2- | 57 | 3- 67 | 2- 57",
    "17 | 47 #4d | 17 | 5- 17 | 47 | #4d | 3- | 67 | 2- | 57 | 17 b37 | b67 b27",
};
static const char *const BIRD = "1^ | 7o 37b | 6- 27 | 5- 17 | 47 | 4- b77 | 3- 67 | b3- b67 | 2- | 57 | 1^ 67b | 2- 57";
static const char *const MBLUES[] = {
    "1- | 4- | 1- | 1- | 4- | 4- | 1- | 1- | b67 | 57a | 1- | 2o 57a",
    "1- | 1- | 1- | 1- | 4- | 4- | 1- | 1- | 2o | 57a | 1- 6o | 2o 57a",
};
/* rhythm changes: the A (to the turnaround, or closed) and its bridge */
static const char *const RHY_A[] = {"1^ 6- | 2- 57 | 3- 67 | 2- 57 | 1^ 17 | 4^ #4d", "1^ 67b | 2- 57 | 1^ 67b | 2- 57 | 1^ 17 | 4^ 4-"};
static const char *const RHY_TURN = "3- 67 | 2- 57";
static const char *const RHY_CLOSE = "2- 57 | 1^";
static const char *const RHY_B[] = {"37 | 37 | 67 | 67 | 27 | 27 | 57 | 57", "7- 37 | 7- 37 | 3- 67 | 3- 67 | 6- 27 | 6- 27 | 2- 57 | 2- 57"};
/* modal: eight bars of a mode, eight of another */
static const char *const MOD_A = "1- | 1- | 1- | 1- | 1- | 1- | 1- | 1-";
static const char *const MOD_B[] = {"b2- | b2- | b2- | b2- | b2- | b2- | b2- | b2-", "b3^ | b3^ | b3^ | b3^ | b2^ | b2^ | b2^ | b2^"};
static const char *const SUS16 = "17s | 17s | 17s | 17s | b37s | b37s | b37s | b37s | 17s | 17s | 17s | 17s | b6^ | b6^ | 57s | 57s";
/* jazz waltz: eight bars of three, a chord a bar: six, then two that turn back (or a close: "1^ | 1^") */
static const char *const W_A[][2] = {
    {"1^ | 4^ | 3- | 67 | 2- | 57", "1^ | 57"},
    {"1^ | 4- | 3- | b3d | 2- | 57", "1^ | 57"},
    {"6- | 27 | 2- | 57 | 1^ | b27", "1^ | 37b"},
};
static const char *const W_B[] = {"4^ | 4^ | 3- | 67 | 2- | 2- | 57 | 57", "2- | 57 | 1^ | 6- | 4^ | 4- | 2- | 57"};

#define NA(a) (int)(sizeof a / sizeof a[0])
#define MAXBARS 32
typedef struct {
    int8_t root[2];                              /* pitch class (the key added) */
    uint8_t q[2], n;                             /* qualities; one or two chords */
    uint8_t sect;                                /* 0 an A, 1 the bridge */
    uint8_t sbar;                                /* the bar within its section */
} fbar_t;

/* a template's bars into the form at bar `at` (the key added); returns the bars read */
static int parse(const char *s, fbar_t *f, int at, int key, int sect)
{
    static const int8_t DEG[8] = {0, 0, 2, 4, 5, 7, 9, 11};
    int nb = 0, k = 0;
    fbar_t *b = &f[at];
    b->n = 0;
    while (*s && at + nb < MAXBARS) {
        int acc = 0, d, q = Q_MAJ7;
        while (*s == ' ')
            s++;
        if (*s == '|') {
            s++;
            nb++;
            b = &f[at + nb];
            b->n = 0;
            continue;
        }
        if (!*s)
            break;
        if (*s == 'b')
            acc = -1, s++;
        else if (*s == '#')
            acc = 1, s++;
        d = *s >= '1' && *s <= '7' ? DEG[*s - '0'] : 0;
        s++;
        switch (*s) {
        case '^': q = Q_MAJ7, s++; break;
        case '6': q = Q_MAJ6, s++; break;
        case '-':
            s++;
            q = Q_M7;
            if (*s == '6')
                q = Q_M6, s++;
            break;
        case '7':
            s++;
            q = Q_DOM;
            if (*s == 'a')
                q = Q_ALT, s++;
            else if (*s == 'b')
                q = Q_B9, s++;
            else if (*s == 's')
                q = Q_SUS, s++;
            break;
        case 'o': q = Q_HD, s++; break;
        case 'd': q = Q_DIM, s++; break;
        default: break;
        }
        if (b->n < 2) {
            b->root[b->n] = (int8_t)(((key + d + acc) % 12 + 12) % 12);
            b->q[b->n] = (uint8_t)q;
            b->n++;
        }
        b->sect = (uint8_t)sect;
        k++;
    }
    if (b->n)
        nb++;
    for (k = 0; k < nb; k++)
        f[at + k].sbar = (uint8_t)k;
    return nb;
}

/* -------------------------------------------------------------- titles --- */
static const char *const T_NAME[] = {
    "Dex", "Hattie", "Otis", "Lucille", "Booker", "Cyrus", "Ira", "Vernon", "Delia", "Pearl", "Rollo", "Wendell",
    "Minnie", "Lonnie", "Odessa", "Clement", "Augie", "Bea", "Fitz", "Tully", "Marvella", "Sully", "Inez", "Doc",
    "Abner", "Ozzie", "Viola"};
static const char *const T_NOUN[] = {
    "Fire Escape", "Hatbox", "Cufflinks", "Streetcar", "Pawnshop", "Stairwell", "Typewriter", "Transom",
    "Newsstand", "Pocket Watch", "Rent Party", "Dime Store", "Switchboard", "Shoeshine", "Barber Pole", "Trolley",
    "Fedora", "Suspenders", "Lunch Counter", "Spats", "Bowling Shoes", "Galoshes", "Dumbwaiter", "Phone Booth",
    "Sugar Bowl", "Cigar Band", "Fountain Pen", "Bus Token", "Cloakroom", "Wristwatch", "Necktie", "Bowler Hat",
    "Card Table", "Shoe Polish", "Tie Clip"};
static const char *const T_ADJ[] = {"Sideways", "Upstairs", "Unpaid", "Ragged", "Tilted", "Sly", "Scuffed", "Pinstripe",
                                    "Dog-Eared", "Off-Key", "Overdue", "Lopsided", "Threadbare", "Cockeyed",
                                    "Second-Hand"};

uint32_t en_tune_rand(uint32_t base, uint32_t no)
{
    uint32_t h = base ^ (no * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h ? h : 1u;
}

static int slen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

void en_title(uint32_t seed, int kind, char *b)
{
    uint32_t h = en_tune_rand(seed, 77u);
    const char *nm = T_NAME[h % (uint32_t)NA(T_NAME)], *no = T_NOUN[(h >> 8) % (uint32_t)NA(T_NOUN)];
    const char *ad = T_ADJ[(h >> 16) % (uint32_t)NA(T_ADJ)];
    int pat = (int)((h >> 24) % 3u);
    if (slen(nm) + slen(no) > 19)                /* "Marvella's Lunch Counter": too long for the board */
        no = T_NOUN[1];
    switch (kind) {
    case K_BLUES: case K_BIRD: case K_MINOR:
        if (pat == 0)
            put_s(put_s(b, nm), "'s Strut");
        else if (pat == 1)
            put_s(put_s(b, no), " Strut");
        else
            put_s(put_s(put_s(b, nm), "'s "), no);
        break;
    case K_BALLAD:
        if (pat == 2)
            put_s(put_s(put_s(b, ad), " "), no);
        else
            put_s(put_s(b, "Song for "), nm);
        break;
    case K_WALTZ:
        if (pat == 2)
            put_s(put_s(put_s(b, ad), " "), no);
        else
            put_s(put_s(b, nm), " in Three");
        break;
    case K_RHYTHM:
        if (pat == 0)
            put_s(put_s(b, nm), " Steps Out");
        else
            put_s(put_s(put_s(b, nm), "'s "), no);
        break;
    default:
        if (pat == 0)
            put_s(put_s(put_s(b, nm), "'s "), no);
        else if (pat == 1)
            put_s(put_s(put_s(b, ad), " "), no);
        else
            put_s(put_s(put_s(b, no), " for "), nm);
        break;
    }
}
