/* dish_mut.c — see dish_mut.h. */
#include "dish_mut.h"
#include "dish_story.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

_Static_assert(M_COUNT == DISH_MUT_N, "dish.h's DISH_MUT_N must match the mutation table");

const char *const PRESSURE_NAMES[P_COUNT] = { "HUNTERS", "VIRUS", "MEDICINE", "ACID", "SALT", "HUNGER", "DARK", "CROWDING", "RIVALS" };

#define A(p) (1u << (p))
const mut_info_t MUTS[M_COUNT] = {
    [M_SPIKES]     = { "SPIKES",       { "TOUCHING IT HURTS", "SHARPER SPINES", "ATTACKERS REEL BACK" },               "spikes",  "SPINY",     MR_COMMON, A(P_HUNTER) | A(P_RIVAL) },
    [M_TOXIN]      = { "TOXIN CLOUD",  { "A HAZE THAT SICKENS OTHERS", "A WIDER HAZE", "A DEADLY FOG" },              "toxin",   "TOXIC",     MR_RARE,   A(P_RIVAL) | A(P_HUNTER) },
    [M_ZAP]        = { "ZAP",          { "A SHOCK THAT FREEZES NEIGHBORS", "STRONGER, MORE OFTEN", "A STORM OF SPARKS" }, "zap",  "SPARKING",  MR_RARE,   A(P_HUNTER) | A(P_VIRUS) },
    [M_JET]        = { "JET",          { "BURSTS AWAY FROM DANGER", "FASTER BURSTS", "GONE IN A FLASH" },             "jet",     "DARTING",   MR_COMMON, A(P_HUNTER) },
    [M_ENGULF]     = { "ENGULF",       { "SWALLOWS SMALLER GERMS WHOLE", "SWALLOWS MORE", "EATS ANYTHING IT CATCHES" }, "engulf", "GREEDY",   MR_EPIC,   A(P_RIVAL) | A(P_STARVE) },
    [M_SWARM]      = { "SWARM MIND",   { "MOVES AS ONE SCHOOL", "A TIGHTER SCHOOL", "ONE MIND, MANY CELLS" },         "swarm",   "SWARMING",  MR_COMMON, A(P_HUNTER) | A(P_CROWD) },
    [M_BIOFILM]    = { "BIOFILM WALL", { "SOME CELLS BUILD A WALL", "A THICKER WALL", "A FORTRESS" },                 "wall",    "WALLED",    MR_RARE,   A(P_DRUG) | A(P_ACID) | A(P_SALT) | A(P_HUNTER) },
    [M_PHOTO]      = { "SUNLIGHT",     { "MAKES FOOD FROM LIGHT", "MORE FROM LESS LIGHT", "LIVES ON LIGHT ALONE" },   "photo",   "GREEN",     MR_COMMON, A(P_STARVE) },
    [M_LURE]       = { "GLOW LURE",    { "GLOWS AND DRAWS PREY IN", "A BRIGHTER LURE", "NOTHING CAN RESIST IT" },     "lure",    "GLOWING",   MR_RARE,   A(P_STARVE) | A(P_DARK) },
    [M_SPOREBURST] = { "SPORE BURST",  { "A DYING CELL LEAVES A SPORE", "TWO SPORES", "THREE SPORES" },              "burst",   "UNDYING",   MR_RARE,   A(P_DRUG) | A(P_VIRUS) | A(P_ACID) | A(P_SALT) },
    [M_TAMER]      = { "VIRUS TAMER",  { "SOME VIRUSES JOIN YOU", "MORE OF THEM JOIN", "VIRUSES SERVE YOU" },         "tamer",   "VIRAL",     MR_EPIC,   A(P_VIRUS) },
    [M_MIMIC]      = { "MIMICRY",      { "LOOKS LIKE ITS ENEMIES", "A BETTER DISGUISE", "ALMOST INVISIBLE" },         "mimic",   "SNEAKY",    MR_RARE,   A(P_HUNTER) },
    [M_SCOUTS]     = { "SCOUTS",       { "SCOUTS FIND FOOD FOR ALL", "MORE SCOUTS", "A SCOUT ARMY" },                 "scouts",  "ROVING",    MR_COMMON, A(P_STARVE) | A(P_CROWD) },
    [M_GRAPPLE]    = { "GRAPPLE",      { "LATCHES ON AND DRAINS", "A STRONGER GRIP", "DRAGS PREY HOME" },             "grapple", "CLINGING",  MR_COMMON, A(P_RIVAL) | A(P_HUNTER) },
    [M_SYMBIOSIS]  = { "SYMBIOSIS",    { "FRIENDS STAY AND SHARE", "FRIENDS SHARE MORE", "FRIENDS FIGHT FOR YOU" },   "symbio",  "FRIENDLY",  MR_RARE,   A(P_STARVE) | A(P_HUNTER) },
    [M_QUORUM]     = { "QUORUM BLOOM", { "A BIG COLONY BLOOMS AND GROWS", "BLOOMS SOONER", "BLOOMS OFTEN" },      "quorum",  "BLOOMING",  MR_RARE,   A(P_CROWD) | A(P_RIVAL) },
};

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void dish_pressures(const dish_t *d, float p[P_COUNT]) {
    const strain_t *st = &d->strain[DISH_PET];
    const genome_t *g = &st->vault;
    for (int i = 0; i < P_COUNT; i++) p[i] = 0;
    p[P_HUNTER] = dish_visitor_here(d, SP_MYXO) || dish_visitor_here(d, SP_VIBRIO) ? 1 : 0;
    p[P_VIRUS] = dish_visitor_here(d, SP_PHAGE) ? 1 : clamp01(st->infected * 2);
    p[P_DRUG] = clamp01(dish_field(d->drug, st->cx, st->cy) * 2 + (dish_visitor_here(d, SP_MOLD) ? 0.7f : 0));
    p[P_ACID] = clamp01((fabsf(g->ph_opt - d->ph) - 0.08f) * 4);
    p[P_SALT] = clamp01((d->salt - (0.25f + g->salt_tol * 0.6f) + 0.08f) * 4);
    p[P_STARVE] = clamp01((0.55f - st->energy) * 2.5f);
    p[P_DARK] = d->light ? 0 : 1;
    p[P_CROWD] = clamp01((st->crowd - 0.4f) * 2);
    p[P_RIVAL] = dish_visitor_here(d, SP_COCCUS) || dish_visitor_here(d, SP_LACTO) || dish_visitor_here(d, SP_HALO) ? 0.7f : 0;
}

/* the weight of a card: 1, more for what answers the dish's pressures, less for rarer ones;
   0 when the strain has it at the top tier */
static float card_weight(const dish_t *d, int m, const float *p) {
    const strain_t *st = &d->strain[DISH_PET];
    if (st->mut[m] >= MUT_TIERS) return 0;
    float best = 0;
    for (int i = 0; i < P_COUNT; i++) if (MUTS[m].answers >> i & 1) best = fmaxf(best, p[i]);
    static const float RAR[3] = { 1.0f, 0.7f, 0.3f };
    return (1 + 4 * best) * RAR[MUTS[m].rarity] * (st->mut[m] ? 1.3f : 1);   /* (a tier up is likelier: builds deepen) */
}

void dish_draft_deal(dish_t *d) {
    if (!d->drafts || d->offer[0] != 0xFF) return;
    float p[P_COUNT]; dish_pressures(d, p);
    float w[M_COUNT], total = 0;
    for (int m = 0; m < M_COUNT; m++) { w[m] = card_weight(d, m, p); total += w[m]; }
    for (int k = 0; k < 3; k++) {
        d->offer[k] = 0xFF;
        if (total <= 0) break;
        float r = dish_rand(d, 0, total);
        for (int m = 0; m < M_COUNT; m++) {
            if (w[m] <= 0) continue;
            if ((r -= w[m]) <= 0) { d->offer[k] = (uint8_t)m; break; }
        }
        if (d->offer[k] == 0xFF)                      /* (float rounding left r a hair over: the last one with weight) */
            for (int m = M_COUNT - 1; m >= 0; m--) if (w[m] > 0) { d->offer[k] = (uint8_t)m; break; }
        if (d->offer[k] != 0xFF) { total -= w[d->offer[k]]; w[d->offer[k]] = 0; }
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

void dish_mut_take(dish_t *d, int s, int m) {
    strain_t *st = &d->strain[s];
    if (m < 0 || m >= M_COUNT || st->mut[m] >= MUT_TIERS) return;
    st->mut[m]++;
    if (s == DISH_PET) { d->run_muts++; dish_story_note(d, SE_MUT, st->species, 0, m, st->mut[m]); }
}
bool dish_draft_take(dish_t *d, int card) {
    if (!d->drafts || card < 0 || card > 2 || d->offer[card] == 0xFF) return false;
    dish_mut_take(d, DISH_PET, d->offer[card]);
    d->drafts = 0;                                    /* (never more than one: the dish waited for this pick) */
    d->offer[0] = d->offer[1] = d->offer[2] = 0xFF;
    return true;
}
void dish_mut_preview(const dish_t *d, int m, uint8_t *mut, genome_t *g) {
    const strain_t *st = &d->strain[DISH_PET];
    memcpy(mut, st->mut, DISH_MUT_N);
    *g = st->vault;
    if (m >= 0 && m < M_COUNT && mut[m] < MUT_TIERS) mut[m]++;
}
int dish_mut_total(const dish_t *d, int s) {
    int n = 0;
    for (int m = 0; m < M_COUNT; m++) n += d->strain[s].mut[m];
    return n;
}
int dish_top_traits(const uint8_t *mut, int *out, int n) {
    int k = 0;
    for (int pass = 0; pass < n; pass++) {
        int best = -1, bs = 0;
        for (int m = 0; m < M_COUNT; m++) {
            bool used = false; for (int q = 0; q < k; q++) used |= out[q] == m;
            int score = mut[m] * 4 + MUTS[m].rarity;
            if (mut[m] && !used && score > bs) { bs = score; best = m; }
        }
        if (best < 0) break;
        out[k++] = best;
    }
    return k;
}
void dish_form_name_of(const uint8_t *mut, species_t sp, char *out, int n) {
    int t[2], k = dish_top_traits(mut, t, 2);
    if (k == 0) snprintf(out, (size_t)n, "%s", SPECIES_NAMES[sp]);
    else if (k == 1) snprintf(out, (size_t)n, "%s %s", MUTS[t[0]].adj, SPECIES_NAMES[sp]);
    else snprintf(out, (size_t)n, "%s %s %s", MUTS[t[0]].adj, MUTS[t[1]].adj, SPECIES_NAMES[sp]);
    for (char *c = out; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 'a' - 'A';
}
void dish_form_name(const dish_t *d, int s, char *out, int n) { dish_form_name_of(d->strain[s].mut, (species_t)d->strain[s].species, out, n); }
