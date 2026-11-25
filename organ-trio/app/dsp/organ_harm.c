/* SPDX-License-Identifier: MPL-2.0
 * ORGAN TRIO: ashtonwallen, 2026 */
/* The harmony (part of organ.c): chord qualities and their voicings, scales and tensions; the forms' changes as
 * curated templates (written as chord symbols over the key: "2- 57 | 1^"); the drawbar registrations; the tunes'
 * titles. */

/* ------------------------------------------------------- chord qualities --- */
enum { Q_MAJ7, Q_MAJ6, Q_M7, Q_M6, Q_DOM, Q_ALT, Q_B9, Q_SUS, Q_HD, Q_DIM, Q_NQ };
static const char *const Q_NAME[Q_NQ] = {"maj7", "6", "m7", "m6", "7", "7#9", "7b9", "7sus", "m7b5", "dim7"};
/* masks of pitch classes above the root (bit i: i semitones up): the chord tones, the tensions a player may land
 * on (9ths, 11ths, 13ths, the altered ones), the scale, the bebop scale (the scale and its passing tone) */
#define M(a) (1u << (a))
static const uint16_t Q_CT[Q_NQ] = {
    M(0) | M(4) | M(7) | M(11), M(0) | M(4) | M(7) | M(9), M(0) | M(3) | M(7) | M(10), M(0) | M(3) | M(7) | M(9),
    M(0) | M(4) | M(7) | M(10), M(0) | M(4) | M(7) | M(10), M(0) | M(4) | M(7) | M(10), M(0) | M(5) | M(7) | M(10),
    M(0) | M(3) | M(6) | M(10), M(0) | M(3) | M(6) | M(9)};
static const uint16_t Q_COLOR[Q_NQ] = {M(2), M(2), M(2) | M(5), M(2), M(2) | M(9), M(3) | M(8), M(1) | M(8),
                                       M(2) | M(9), M(5), M(2)};
static const uint16_t Q_SCALE[Q_NQ] = {
    M(0) | M(2) | M(4) | M(5) | M(7) | M(9) | M(11),          /* ionian */
    M(0) | M(2) | M(4) | M(5) | M(7) | M(9) | M(11),
    M(0) | M(2) | M(3) | M(5) | M(7) | M(9) | M(10),          /* dorian */
    M(0) | M(2) | M(3) | M(5) | M(7) | M(9) | M(11),          /* melodic minor */
    M(0) | M(2) | M(4) | M(5) | M(7) | M(9) | M(10),          /* mixolydian */
    M(0) | M(1) | M(3) | M(4) | M(6) | M(8) | M(10),          /* altered (the 7#9: the blues' own) */
    M(0) | M(1) | M(4) | M(5) | M(7) | M(8) | M(10),          /* phrygian dominant */
    M(0) | M(2) | M(5) | M(7) | M(9) | M(10),                 /* sus: no third */
    M(0) | M(2) | M(3) | M(5) | M(6) | M(8) | M(10),          /* locrian #2 */
    M(0) | M(2) | M(3) | M(5) | M(6) | M(8) | M(9) | M(11),   /* whole-half */
};
static const uint16_t Q_BEBOP[Q_NQ] = {M(8), M(8), M(4), 0, M(11), 0, 0, M(11), 0, 0};   /* added passing tones */
/* rootless voicings (the bass has the root): A from the third, B from the seventh */
static const int8_t Q_VA[Q_NQ][4] = {
    {4, 7, 11, 14}, {4, 7, 9, 14}, {3, 7, 10, 14}, {3, 7, 9, 14}, {4, 9, 10, 14},
    {4, 10, 15, 20}, {4, 8, 10, 13}, {5, 9, 10, 14}, {3, 6, 10, 12}, {3, 6, 9, 12},
};
static const int8_t Q_VB[Q_NQ][4] = {
    {11, 14, 16, 19}, {9, 14, 16, 19}, {10, 14, 15, 19}, {9, 14, 15, 19}, {10, 14, 16, 21},
    {10, 15, 16, 20}, {10, 13, 16, 20}, {10, 14, 17, 21}, {10, 12, 15, 18}, {9, 12, 15, 18},
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
 * a minor key's degrees still count from its tonic) and a quality: ^ maj7, 6 maj6, - m7, -6 m6, 7 dom7, 7a 7#9,
 * 7b 7b9, 7s sus, o half-diminished, d diminished. A bar's chords share it evenly. */

/* the greasy medium blues: the plain one, the jazz one with its #iv diminished, the one with the quick ii-V */
static const char *const BLUES_F[] = {
    "17 | 47 | 17 | 17 | 47 | 47 | 17 | 17 | 57 | 47 | 17 | 57",
    "17 | 47 | 17 | 17 | 47 | #4d | 17 | 67b | 2- | 57 | 17 67b | 2- 57",
    "17 | 47 | 17 | 5- 17 | 47 | 47 | 17 | 3- 67 | 2- | 57 | 17 47 | 17 57",
};
/* the shuffle: a twelve-bar, the slow change or the quick one, the IV in the turnaround */
static const char *const SHUF_F[] = {
    "17 | 17 | 17 | 17 | 47 | 47 | 17 | 17 | 57 | 47 | 17 | 57",
    "17 | 47 | 17 | 17 | 47 | 47 | 17 | 17 | 57 | 47 | 17 47 | 17 57",
};
/* the slow minor blues (12/8): i iv, the bVI7 to the V7#9, the turnaround */
static const char *const MINOR_F[] = {
    "1- | 4- | 1- | 1- | 4- | 4- | 1- | 1- | b67 | 57a | 1- 4- | 1- 57a",
    "1- | 1- | 1- | 1- | 4- | 4- | 1- | 1- | b67 | 57a | 1- | b67 57a",
    "1- | 4- | 1- | 1- | 4- | 4- | 1- | b67 | 2o | 57a | 1- 4- | 1- 57a",
};
/* soul-jazz grooves, sixteen bars over an ostinato: a minor vamp, a dominant one, the dorian i-IV7 */
static const char *const GROOVE_F[] = {
    "1- | 1- | 1- | 1- | 4- | 4- | 1- | 1- | b67 | b67 | 57a | 57a | 1- | 1- | 1- | 1-",
    "17 | 17 | 17 | 17 | 47 | 47 | 17 | 17 | 57 | 47 | 57 | 47 | 17 | 47 | 17 | 57",
    "1- | 1- | 47 | 47 | 1- | 1- | 47 | 47 | b77 | b77 | 1- | 1- | b67 | 57a | 1- | 57a",
};
static const uint8_t GROOVE_MINOR[] = {1, 0, 1};
/* the gospel ballad: eight bars to the half cadence, eight home with the plagal close (IV, iv, I) */
static const char *const GOSPEL_A[][2] = {
    {"16 | 17 | 4^ | #4d | 16 6- | 2- 57 | 16 | 57", "16 | 17 | 4^ | 4-6 | 16 67 | 2- 57 | 16 4^ | 16"},
    {"16 | 3- 67 | 2- 57 | 16 17 | 4^ | 4-6 | 3- 67 | 2- 57", "16 | 3- 67 | 2- 57 | 16 17 | 4^ | 4-6 | 16 57 | 16"},
    {"16 | 4^ | 16 | 17 | 4^ | #4d | 16 6- | 2- 57", "16 | 4^ | 16 | 17 | 4^ | 4-6 | 16 57 | 16"},
};
/* the swinging standard: AABA songs (major A sections, six bars each to I, and their last two) */
static const char *const A_MAJ[] = {
    "1^ | 5- 17 | 4^ | 4- b77 | 3- 67 | 2- 57",           /* up to IV, the backdoor, iii VI ii V */
    "1^ | 37b | 6- | 27 | 2- | 57",                       /* I III7 vi II7, the long ii V */
    "2- 57 | 3- 67 | 2- 57 | 1^ | b3- b67 | b6^ b27",     /* ii-V chains, bII7 home */
    "1^ 6- | 2- 57 | 1^ 17 | 4^ #4d | 3- 67 | 2- 57",     /* I vi ii V, IV and the #iv diminished */
    "1^ | 4- b77 | 1^ | 5- 17 | 4^ | 4- b77",             /* the backdoor twice */
};
static const char *const TURN_MAJ[] = {"1^ 6- | 2- 57", "3- 67 | 2- 57", "1^ b37 | b67 b27", "1^ 67b | 2- b27"};
static const char *const CLOSE_MAJ = "1^ | 1^";
static const char *const B_MAJ[] = {
    "5- | 17 | 4^ | 4^ | 6- | 27 | 2- | 57",              /* to IV, then vi II7 ii V */
    "37 | 37 | 67 | 67 | 27 | 27 | 57 | 57",              /* dominants round the cycle */
    "4^ | 4- b77 | 1^ | 6- | 2- 57 | 3- 67 | 2- | b27",   /* IV, the backdoor, home and round */
};
static const char *const A_MIN[] = {
    "1- | 1- | 4- | b77 | b3^ | 2o 57b",                  /* i iv bVII bIII, the minor ii V */
    "1- | 2o 57b | 1- | 47 | b6^ | 2o 57b",               /* i, ii V, the dorian IV7, bVI */
};
static const char *const TURN_MIN[] = {"1- 6o | 2o 57b", "1- b6^ | 2o 57a"};
static const char *const CLOSE_MIN = "1- | 1-";
static const char *const B_MIN[] = {
    "4- | b77 | b3^ | b6^ | 2o | 57b | 1- | 57b",
    "b6^ | b6^ | b2^ | b2^ | 2o | 57b | 1- | 57a",
};

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

/* --------------------------------------------------------- registrations --- */
/* the upper manual's drawbars (16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1'), the lower manual's for the chords */
static const uint8_t REG_UP[EN_NREG][9] = {
    {8, 8, 8, 0, 0, 0, 0, 0, 0}, {8, 8, 8, 8, 0, 0, 0, 0, 0}, {8, 0, 8, 0, 0, 0, 0, 0, 8}, {8, 8, 6, 8, 0, 0, 0, 0, 0},
    {8, 6, 8, 0, 0, 0, 0, 0, 0}, {8, 8, 6, 0, 0, 0, 0, 0, 0}, {0, 0, 8, 0, 8, 0, 8, 0, 0}, {8, 3, 8, 0, 0, 0, 0, 0, 0},
};
static const uint8_t REG_LO[EN_NREG][9] = {
    {0, 0, 8, 6, 0, 0, 0, 0, 0}, {0, 0, 8, 6, 0, 0, 0, 0, 0}, {0, 0, 8, 4, 0, 0, 0, 0, 2}, {0, 0, 8, 8, 0, 0, 0, 0, 0},
    {0, 0, 7, 4, 0, 0, 0, 0, 0}, {0, 0, 8, 5, 0, 0, 0, 0, 0}, {0, 0, 8, 0, 4, 0, 0, 0, 0}, {0, 0, 6, 6, 0, 0, 0, 0, 0},
};
/* where the organist pulls out to at a climax: everything up to these */
static const uint8_t REG_FULL[9] = {8, 8, 8, 8, 6, 5, 4, 4, 5};

const char *en_reg_text(int i, char *b)
{
    const uint8_t *r = REG_UP[(i % EN_NREG + EN_NREG) % EN_NREG];
    int k, n = 0;
    for (k = 0; k < 9; k++) {
        if (k == 2 || k == 6)
            b[n++] = ' ';
        b[n++] = (char)('0' + r[k]);
    }
    b[n] = 0;
    return b;
}

/* -------------------------------------------------------------- titles --- */
static const char *const T_NAME[] = {
    "Bishop", "Ida Mae", "Junebug", "Odell", "Buckshot", "Peaches", "Lil Earl", "Velma", "Cleophus", "Ozella",
    "Thaddeus", "Nettie", "Pookie", "Lucius", "Toot", "Beulah", "Arvell", "Queenie", "Mose", "Fannie Lou",
    "Shug", "Elroy", "Opal", "Huey", "Hattie", "Booker", "Inez", "Willie Mae", "Clarence", "Lottie", "Rufus",
    "Geneva", "Sweet Pea", "Alberta", "Junior", "Tootie"};
static const char *const T_FOOD[] = {
    "Neckbones", "Hot Sauce", "Red Beans", "Fatback", "Pig Feet", "Turnip Greens", "Spoonbread", "Pot Liquor",
    "Chitlins", "Smothered Pork", "Fried Pies", "Hoecake", "Okra", "Sweet Potato", "Cobbler", "Hominy", "Grits",
    "Catfish", "Rib Tips", "Candied Yams", "Biscuits", "Corn Pone", "Gravy", "Butter Beans"};
static const char *const T_PLACE[] = {
    "Pool Hall", "Car Wash", "Coin Laundry", "Fish Market", "Barbershop", "Rib Joint", "Bait Shop", "Body Shop",
    "Bus Depot", "Tent Meeting", "Pawn Window", "Social Club", "Lodge Hall", "Beauty Parlor", "Shoe Shine",
    "Corner Store", "Feed Store", "Tire Shop", "Domino Table", "Ice House"};

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

void en_title(uint32_t seed, int kind, char *b)
{
    uint32_t h = en_tune_rand(seed, 77u);
    const char *nm = T_NAME[h % (uint32_t)NA(T_NAME)], *fd = T_FOOD[(h >> 8) % (uint32_t)NA(T_FOOD)];
    const char *pl = T_PLACE[(h >> 16) % (uint32_t)NA(T_PLACE)];
    int pat = (int)((h >> 24) % 4u);
    switch (kind) {
    case K_BLUES:
        if (pat == 0)
            put_s(put_s(b, "Back at the "), pl);
        else if (pat == 1)
            put_s(put_s(put_s(b, nm), "'s "), fd);
        else if (pat == 2)
            put_s(put_s(b, "Mo' "), fd);
        else
            put_s(put_s(b, pl), " Blues");
        break;
    case K_SHUFFLE:
        if (pat == 0)
            put_s(put_s(b, fd), " Shuffle");
        else if (pat == 1)
            put_s(put_s(b, "Shufflin' "), nm);
        else if (pat == 2)
            put_s(put_s(b, pl), " Shuffle");
        else
            put_s(put_s(b, "Struttin' with "), nm);
        break;
    case K_MINOR:
        if (pat == 0)
            put_s(put_s(b, "Cryin' for "), nm);
        else if (pat == 1)
            put_s(put_s(b, nm), ", Don't Go");
        else if (pat == 2)
            put_s(put_s(b, "Closing the "), pl);
        else
            put_s(put_s(b, "Low Down "), nm);
        break;
    case K_GROOVE:
        if (pat == 0)
            put_s(put_s(b, fd), " Boogaloo");
        else if (pat == 1)
            put_s(put_s(b, "Do the "), nm);
        else if (pat == 2)
            put_s(put_s(b, nm), "'s Thing");
        else
            put_s(put_s(b, "Hot "), fd);
        break;
    case K_GOSPEL:
        if (pat == 0)
            put_s(put_s(b, "Testimony for "), nm);
        else if (pat == 1)
            put_s(put_s(b, nm), "'s Revival");
        else if (pat == 2)
            put_s(put_s(b, "Sanctified "), nm);
        else
            put_s(put_s(b, "Walk with "), nm);
        break;
    default:
        if (pat == 0)
            put_s(put_s(put_s(b, nm), " at the "), pl);
        else if (pat == 1)
            put_s(put_s(b, fd), " to Go");
        else if (pat == 2)
            put_s(put_s(b, "Easy on the "), fd);
        else
            put_s(put_s(b, "Steppin' with "), nm);
        break;
    }
    {                                            /* (too long for the nameplate: the short one) */
        int n = 0;
        while (b[n])
            n++;
        if (n > 24)
            put_s(put_s(put_s(b, nm), "'s "), fd);
        for (n = 0; b[n]; n++)
            ;
        if (n > 24)
            put_s(put_s(b, "Mo' "), fd);
    }
}
