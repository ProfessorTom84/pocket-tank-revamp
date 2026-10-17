/* dish_mut.c — see dish_mut.h. */
#include "dish_mut.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

_Static_assert(M_COUNT == DISH_MUT_N, "dish.h's DISH_MUT_N must match the mutation table");

const char *const PRESSURE_NAMES[P_COUNT] = { "HEAT", "COLD", "SALT", "ACID", "SOAP", "MEDICINE", "VIRUS", "HUNTERS", "RIVALS", "HUNGER", "CROWDING", "DARK" };

#define A(p) (1u << (p))
const mut_info_t MUTS[M_COUNT] = {
    [M_BIG]         = { "BIG BODY",     "BIGGER CELLS: HARDER TO EAT",       "GIANT",     MF_BODY,    MR_COMMON, 3, A(P_HUNTER) },
    [M_SPIKES]      = { "SPIKES",       "HURTS ANYTHING THAT TOUCHES IT",    "SPINY",     MF_BODY,    MR_RARE,   3, A(P_HUNTER) | A(P_RIVAL) },
    [M_SHELL]       = { "SHELL",        "A HARD COAT AGAINST SALT AND DRUGS", "ARMORED",  MF_BODY,    MR_COMMON, 3, A(P_SALT) | A(P_DRUG) },
    [M_SLIME]       = { "SLIME COAT",   "HUNTERS AND VIRUSES SLIP OFF",      "SLIMY",     MF_BODY,    MR_RARE,   3, A(P_HUNTER) | A(P_VIRUS) },
    [M_SLIM]        = { "SLIM",         "THINNER AND FASTER",                "SLENDER",   MF_BODY,    MR_COMMON, 3, A(P_HUNTER) | A(P_STARVE) },
    [M_FLAGELLA]    = { "MORE TAILS",   "AN EXTRA FLAGELLUM: SWIMS FASTER",  "WHIPPING",  MF_MOTION,  MR_COMMON, 3, A(P_STARVE) | A(P_HUNTER) },
    [M_CILIA]       = { "CILIA",        "SMELLS AND STEERS TO FOOD",         "HAIRY",     MF_MOTION,  MR_RARE,   3, A(P_STARVE) | A(P_RIVAL) },
    [M_GLIDE]       = { "GLIDING",      "MOVES AS A CLUMP",                  "GLIDING",   MF_MOTION,  MR_COMMON, 2, A(P_HUNTER) | A(P_CROWD) },
    [M_TWITCH]      = { "TWITCH",       "DODGES BITES AND VIRUSES",          "TWITCHY",   MF_MOTION,  MR_RARE,   3, A(P_HUNTER) | A(P_VIRUS) },
    [M_GLUTTON]     = { "GLUTTON",      "EATS MUCH FASTER",                  "HUNGRY",    MF_METAB,   MR_COMMON, 3, A(P_STARVE) | A(P_RIVAL) },
    [M_CHLORO]      = { "CHLOROPLASTS", "MAKES FOOD FROM LIGHT",             "GREEN",     MF_METAB,   MR_RARE,   3, A(P_STARVE) | A(P_RIVAL) },
    [M_SCAVENGER]   = { "SCAVENGER",    "EATS WASTE AND THE DEAD",           "SCAVENGING",MF_METAB,   MR_RARE,   2, A(P_STARVE) | A(P_CROWD) },
    [M_FERMENT]     = { "FERMENTER",    "LIVES ON VERY LITTLE",              "BUBBLY",    MF_METAB,   MR_COMMON, 3, A(P_STARVE) | A(P_DARK) },
    [M_FASTDIV]     = { "FAST SPLIT",   "DIVIDES SOONER",                    "SPLITTING", MF_METAB,   MR_COMMON, 3, A(P_RIVAL) | A(P_VIRUS) },
    [M_EFFLUX]      = { "PUMPS",        "PUMPS ANTIBIOTIC BACK OUT",         "PUMPING",   MF_DEFENSE, MR_COMMON, 3, A(P_DRUG) },
    [M_PHAGESHIELD] = { "VIRUS SHIELD", "HIDES THE DOORS VIRUSES USE",       "SHIELDED",  MF_DEFENSE, MR_COMMON, 3, A(P_VIRUS) },
    [M_HEATSHOCK]   = { "HEAT LOVER",   "THRIVES HOTTER",                    "FIERY",     MF_DEFENSE, MR_COMMON, 3, A(P_HEAT) },
    [M_COLDSHOCK]   = { "COLD LOVER",   "THRIVES COLDER",                    "FROSTY",    MF_DEFENSE, MR_COMMON, 3, A(P_COLD) },
    [M_SALTPUMP]    = { "SALT PUMP",    "BAILS SALT OUT",                    "SALTY",     MF_DEFENSE, MR_COMMON, 3, A(P_SALT) },
    [M_ACIDARMOR]   = { "SOUR SKIN",    "LIKES IT SOUR",                     "SOUR",      MF_DEFENSE, MR_COMMON, 3, A(P_ACID) },
    [M_BASEARMOR]   = { "SOAP SKIN",    "LIKES IT SOAPY",                    "SOAPY",     MF_DEFENSE, MR_COMMON, 3, A(P_BASE) },
    [M_QUORUM]      = { "QUORUM",       "STRONGER IN A CROWD",               "CHATTY",    MF_SOCIAL,  MR_RARE,   3, A(P_RIVAL) | A(P_HUNTER) | A(P_CROWD) },
    [M_BIOFILM]     = { "BIOFILM",      "CLUMPS SHARE THE DAMAGE",           "STICKY",    MF_SOCIAL,  MR_RARE,   3, A(P_DRUG) | A(P_HUNTER) },
    [M_SIGNAL]      = { "STRESS SIGNAL","WARNS THE OTHERS: LESS HARM",       "CALM",      MF_SOCIAL,  MR_RARE,   3, A(P_HEAT) | A(P_COLD) | A(P_SALT) | A(P_ACID) | A(P_BASE) },
    [M_TOXIN]       = { "TOXIN",        "POISONS OTHER GERMS NEARBY",        "TOXIC",     MF_POWER,   MR_RARE,   3, A(P_RIVAL) | A(P_HUNTER) },
    [M_PREDATOR]    = { "PREDATOR",     "EATS OTHER GERMS IT TOUCHES",       "SAVAGE",    MF_POWER,   MR_EPIC,   2, A(P_RIVAL) | A(P_STARVE) },
    [M_SPOREMASTER] = { "SPORE MASTER", "HIDES IN SPORES FAST AND SAFE",     "HARDY",     MF_POWER,   MR_RARE,   2, A(P_DRUG) | A(P_VIRUS) | A(P_HEAT) | A(P_COLD) },
    [M_GLOW]        = { "GLOW",         "MAKES ITS OWN LIGHT",               "GLOWING",   MF_POWER,   MR_RARE,   2, A(P_DARK) },
    [M_GENETHIEF]   = { "GENE THIEF",   "STEALS GENES: FREE CARDS",          "THIEVING",  MF_POWER,   MR_EPIC,   1, 0 },
    [M_CRISPR]      = { "CRISPR",       "REMEMBERS VIRUSES IT BEAT",         "WISE",      MF_POWER,   MR_EPIC,   1, A(P_VIRUS) },
};

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void dish_pressures(const dish_t *d, float p[P_COUNT]) {
    const strain_t *st = &d->strain[DISH_PET];
    const genome_t *g = &st->vault;
    for (int i = 0; i < P_COUNT; i++) p[i] = 0;
    p[P_HEAT] = clamp01((d->temp - g->heat_opt - 0.08f) * 4);
    p[P_COLD] = clamp01((g->heat_opt - d->temp - 0.08f) * 4);
    p[P_SALT] = clamp01((d->salt - (0.25f + g->salt_tol * 0.6f) + 0.08f) * 4);
    p[P_ACID] = clamp01((g->ph_opt - d->ph - 0.08f) * 4);
    p[P_BASE] = clamp01((d->ph - g->ph_opt - 0.08f) * 4);
    p[P_DRUG] = clamp01(dish_field(d->drug, st->cx, st->cy) * 2 + (dish_visitor_here(d, SP_MOLD) ? 0.7f : 0));
    p[P_VIRUS] = dish_visitor_here(d, SP_PHAGE) ? 1 : clamp01(st->infected * 2);
    p[P_HUNTER] = dish_visitor_here(d, SP_MYXO) || dish_visitor_here(d, SP_VIBRIO) ? 1 : 0;
    p[P_RIVAL] = dish_visitor_here(d, SP_COCCUS) || dish_visitor_here(d, SP_LACTO) || dish_visitor_here(d, SP_HALO) ? 0.7f : 0;
    p[P_STARVE] = clamp01((0.55f - st->energy) * 2.5f);
    p[P_CROWD] = clamp01((st->crowd - 0.4f) * 2);
    p[P_DARK] = d->light ? 0 : 1;
}

static float rand01(dish_t *d) { return dish_rand(d, 0, 1); }

/* the weight of a card: 1, more for what answers the dish's pressures, a little for its family's synergy,
   less for rarer ones; 0 when the strain already has it at full rank */
static float card_weight(const dish_t *d, int m, const float *p) {
    const strain_t *st = &d->strain[DISH_PET];
    if (st->mut[m] >= MUTS[m].max_rank) return 0;
    if (m == M_GLOW && !st->mut[M_CHLORO] && d->light) return 0.2f;   /* (glow is for the dark, or for chloroplasts) */
    float best = 0;
    for (int i = 0; i < P_COUNT; i++) if (MUTS[m].answers >> i & 1) best = fmaxf(best, p[i]);
    float fam = 0;
    for (int k = 0; k < M_COUNT; k++) if (st->mut[k] && MUTS[k].family == MUTS[m].family) fam = 0.6f;
    static const float RAR[3] = { 1.0f, 0.6f, 0.22f };
    return (1 + 4 * best + fam) * RAR[MUTS[m].rarity] * (st->mut[m] ? 1.2f : 1);   /* (a rank-up is a little likelier: builds deepen) */
}

void dish_draft_deal(dish_t *d) {
    if (!d->drafts || d->offer[0] != 0xFF) return;
    float p[P_COUNT]; dish_pressures(d, p);
    float w[M_COUNT], total = 0;
    for (int m = 0; m < M_COUNT; m++) { w[m] = card_weight(d, m, p); total += w[m]; }
    for (int k = 0; k < 3; k++) {
        d->offer[k] = 0xFF;
        if (total <= 0) break;
        float r = rand01(d) * total;
        for (int m = 0; m < M_COUNT; m++) {
            if (w[m] <= 0) continue;
            if ((r -= w[m]) <= 0 || m == M_COUNT - 1) { d->offer[k] = (uint8_t)m; total -= w[m]; w[m] = 0; break; }
        }
        if (d->offer[k] == 0xFF)                      /* (float rounding left r a hair over: the last one with weight) */
            for (int m = M_COUNT - 1; m >= 0; m--) if (w[m] > 0) { d->offer[k] = (uint8_t)m; total -= w[m]; w[m] = 0; break; }
    }
    /* a strong pressure always gets an answer among the three */
    int worst = 0;
    for (int i = 1; i < P_COUNT; i++) if (p[i] > p[worst]) worst = i;
    if (p[worst] >= 0.6f) {
        bool answered = false;
        for (int k = 0; k < 3; k++) if (d->offer[k] != 0xFF && MUTS[d->offer[k]].answers >> worst & 1) answered = true;
        if (!answered) {
            int best = -1; float bw = 0;
            for (int m = 0; m < M_COUNT; m++) {
                bool dealt = m == d->offer[0] || m == d->offer[1] || m == d->offer[2];
                float cw = card_weight(d, m, p);
                if (!dealt && (MUTS[m].answers >> worst & 1) && cw > bw) { bw = cw; best = m; }
            }
            if (best >= 0) d->offer[2] = (uint8_t)best;
        }
    }
}

/* the genome edit a mutation makes, per rank (applied to every living cell of the strain and its vault) */
static void edit_genome(genome_t *g, int m) {
    switch (m) {
    case M_BIG:         g->size = fminf(1.35f, g->size + 0.12f); break;
    case M_SLIM:        g->speed = fminf(1.6f, g->speed + 0.15f); g->size = fmaxf(0.75f, g->size - 0.06f); break;
    case M_SHELL:       g->salt_tol = fminf(1, g->salt_tol + 0.12f); break;
    case M_FASTDIV:     g->div_rate = fminf(1.6f, g->div_rate + 0.15f); break;
    case M_EFFLUX:      g->drug_res = fminf(1, g->drug_res + 0.22f); break;
    case M_PHAGESHIELD: g->phage_res = fminf(0.95f, g->phage_res + 0.22f); break;
    case M_HEATSHOCK:   g->heat_opt = fminf(0.95f, g->heat_opt + 0.1f); break;
    case M_COLDSHOCK:   g->heat_opt = fmaxf(0.05f, g->heat_opt - 0.1f); break;
    case M_SALTPUMP:    g->salt_tol = fminf(1, g->salt_tol + 0.22f); break;
    case M_ACIDARMOR:   g->ph_opt = fmaxf(0, g->ph_opt - 0.12f); break;
    case M_BASEARMOR:   g->ph_opt = fminf(1, g->ph_opt + 0.12f); break;
    }
}
void dish_mut_take(dish_t *d, int s, int m) {
    strain_t *st = &d->strain[s];
    if (m < 0 || m >= M_COUNT || st->mut[m] >= MUTS[m].max_rank) return;
    st->mut[m]++;
    edit_genome(&st->vault, m);
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        cell_t *c = &d->cell[i];
        if (c->strain == s && (c->state == CS_LIVE || c->state == CS_SPORE)) edit_genome(&c->g, m);
    }
    if (s == DISH_PET) d->run_muts++;
}
bool dish_draft_take(dish_t *d, int card) {
    if (!d->drafts || card < 0 || card > 2 || d->offer[card] == 0xFF) return false;
    dish_mut_take(d, DISH_PET, d->offer[card]);
    d->drafts--;
    d->offer[0] = d->offer[1] = d->offer[2] = 0xFF;
    return true;
}
void dish_mut_preview(const dish_t *d, int m, uint8_t *mut, genome_t *g) {
    const strain_t *st = &d->strain[DISH_PET];
    memcpy(mut, st->mut, DISH_MUT_N);
    *g = st->vault;
    if (m >= 0 && m < M_COUNT && mut[m] < MUTS[m].max_rank) { mut[m]++; edit_genome(g, m); }
}
int dish_mut_total(const dish_t *d, int s) {
    int n = 0;
    for (int m = 0; m < M_COUNT; m++) n += d->strain[s].mut[m];
    return n;
}

void dish_form_name_of(const uint8_t *mut, species_t sp, char *out, int n) {
    /* the two strongest: rank first, then rarity */
    int a = -1, b = -1;
    for (int m = 0; m < M_COUNT; m++) {
        if (!mut[m]) continue;
        int score = mut[m] * 4 + MUTS[m].rarity;
        int sa = a < 0 ? -1 : mut[a] * 4 + MUTS[a].rarity, sb = b < 0 ? -1 : mut[b] * 4 + MUTS[b].rarity;
        if (score > sa) { b = a; a = m; } else if (score > sb) b = m;
    }
    if (a < 0) snprintf(out, (size_t)n, "%s", SPECIES_NAMES[sp]);
    else if (b < 0) snprintf(out, (size_t)n, "%s %s", MUTS[a].adj, SPECIES_NAMES[sp]);
    else snprintf(out, (size_t)n, "%s %s %s", MUTS[a].adj, MUTS[b].adj, SPECIES_NAMES[sp]);
    for (char *c = out; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 'a' - 'A';
}
void dish_form_name(const dish_t *d, int s, char *out, int n) { dish_form_name_of(d->strain[s].mut, (species_t)d->strain[s].species, out, n); }
