/* dish_brain.c — what a strain decides, and the line the AI reads (dish schema v1, model/schema_dish.md).
 *
 * Every strain - the pet and every visitor - has a mind. It is asked what to
 * do every few seconds and answers with a GOAL and the strain it is about (a
 * TARGET among the three nearest others). The AI (dish_ai.c) answers from a
 * small model distilled from gemma4:26b; the rules here answer when there is
 * no model, and they speak the same language, so the game plays either way.
 *
 * dish_encode writes the exact line the model is trained on. The sampler
 * (dishsim --dump-states) prints the same line for the teacher, and
 * dish_lexicon lists every word in it for the tokenizer: one source, no drift. */
#include "dish.h"
#include "dish_mut.h"
#include "dish_talk.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int d9(float v) { v *= 9; return v < 0 ? 0 : v > 9 ? 9 : (int)(v + 0.5f); }

/* what a strain is, from anyone's side: the pet, a friend, a threat, a rival */
static const char *role_of(const dish_t *d, int s) {
    if (s == DISH_PET) return "pet";
    int sp = d->strain[s].species;
    if (SPECIES_FRIENDLY[sp]) return "friend";
    return sp == SP_PHAGE || sp == SP_MYXO || sp == SP_VIBRIO || sp == SP_MOLD ? "threat" : "rival";
}
static const char *count_word(int n) { return n <= 0 ? "none" : n == 1 ? "one" : n <= 5 ? "few" : n <= 20 ? "some" : n <= 60 ? "many" : "swarm"; }
static const char *dist_word(float d) { return d < 70 ? "near" : d < 180 ? "mid" : "far"; }
/* the direction from (x0,y0) to (x1,y1) as a clock position, 12 = up the glass */
static int clock_to(float x0, float y0, float x1, float y1) {
    float a = atan2f(x1 - x0, -(y1 - y0));        /* 0 = up, clockwise */
    if (a < 0) a += 6.2831853f;
    int h = (int)(a / 6.2831853f * 12 + 0.5f) % 12;
    return h == 0 ? 12 : h;
}
static float sdist(const strain_t *a, const strain_t *b) { return sqrtf((a->cx - b->cx) * (a->cx - b->cx) + (a->cy - b->cy) * (a->cy - b->cy)); }

void dish_encode(const dish_t *d, int s, char *out, int n) {
    const strain_t *st = &d->strain[s];
    int t[3], k = dish_top_traits(st->mut, t, 3);
    char traits[3][12];
    for (int q = 0; q < 3; q++) {
        if (q < k) snprintf(traits[q], sizeof traits[q], "%s%d", MUTS[t[q]].word, st->mut[t[q]]);
        else snprintf(traits[q], sizeof traits[q], "none");
    }
    char light[24];
    if (d->beam > 0.05f) {
        float dd = sqrtf((d->beam_x - st->cx) * (d->beam_x - st->cx) + (d->beam_y - st->cy) * (d->beam_y - st->cy));
        snprintf(light, sizeof light, "%s %d", dist_word(dd), clock_to(st->cx, st->cy, d->beam_x, d->beam_y));
    } else snprintf(light, sizeof light, "none 0");
    char others[3][64];
    for (int q = 0; q < 3; q++) {
        int o = st->other[q];
        if (o < 0 || !d->strain[o].used || !d->strain[o].count) { snprintf(others[q], sizeof others[q], "none none none none 0 none none"); continue; }
        const strain_t *os = &d->strain[o];
        const char *rel = !ST_TARGETED(os->strat.id) || os->strat.target < 0 ? "none" : os->strat.target == s ? "me" : "them";
        snprintf(others[q], sizeof others[q], "%s %s %s %s %d %s %s", SPECIES_NAMES[os->species], role_of(d, o), count_word(os->count),
                 dist_word(sdist(st, os)), clock_to(st->cx, st->cy, os->cx, os->cy), STRAT_NAMES[os->strat.id], rel);
    }
    snprintf(out, (size_t)n,
             "kind %s role %s count %s energy %d health %d comfort %d crowd %d traits %s %s %s light %s "
             "other1 %s other2 %s other3 %s time %s last %s",
             SPECIES_NAMES[st->species], role_of(d, s), count_word(st->count), d9(st->energy), d9(st->health), d9(st->comfort), d9(st->crowd),
             traits[0], traits[1], traits[2], light, others[0], others[1], others[2], d->light ? "day" : "night", STRAT_NAMES[st->strat.id]);
}

/* every word the line and the answer can hold, in a fixed order: the tokenizer's vocabulary */
int dish_lexicon(const char **out, int cap) {
    static char trait_words[DISH_MUT_N * 3][12];
    static const char *const FIXED[] = {
        "kind", "role", "count", "energy", "health", "comfort", "crowd", "traits", "light",
        "other1", "other2", "other3", "time", "last", "urgency", "->",
        "pet", "friend", "threat", "rival", "none", "one", "few", "some", "many", "swarm",
        "near", "mid", "far", "me", "them", "day", "night", "say",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12" };
    int n = 0;
    for (size_t i = 0; i < sizeof FIXED / sizeof FIXED[0] && n < cap; i++) out[n++] = FIXED[i];
    for (int sp = 0; sp < SP_COUNT && n < cap; sp++) out[n++] = SPECIES_NAMES[sp];
    for (int g = 0; g < ST_COUNT && n < cap; g++) out[n++] = STRAT_NAMES[g];
    for (int k = 0; k < SAY_COUNT && n < cap; k++) out[n++] = SAY_WORDS[k];
    for (int m = 0; m < DISH_MUT_N; m++)
        for (int tier = 1; tier <= 3 && n < cap; tier++) {
            snprintf(trait_words[m * 3 + tier - 1], sizeof trait_words[0], "%s%d", MUTS[m].word, tier);
            out[n++] = trait_words[m * 3 + tier - 1];
        }
    return n;
}

/* ---- the rules: the same goals and targets, chosen by hand ---- */
static bool predator_kind(int sp) { return sp == SP_MYXO || sp == SP_VIBRIO; }
static bool fights(const uint8_t *m) { return m[M_ENGULF] || m[M_GRAPPLE] || m[M_TOXIN] >= 2 || m[M_ZAP] >= 2 || m[M_SPIKES] >= 2; }
static strat_t pick(int g, int target, float urg) { strat_t r = { (uint8_t)g, (int8_t)target, urg, 1, 0 }; return r; }
/* what the nearest-first others are, for the tests below */
enum { IS_ANY, IS_PET, IS_FRIEND, IS_PREYABLE, IS_HOSTILE, IS_BAD_FOR_CILIATE, IS_THREAT_TO };
static bool is(const dish_t *d, int s, int x, int what) {
    int sp = d->strain[x].species;
    switch (what) {
    case IS_PET: return x == DISH_PET;
    case IS_FRIEND: return SPECIES_FRIENDLY[sp] && x != DISH_PET;
    case IS_PREYABLE: return !SPECIES_FRIENDLY[sp] && sp != SP_PHAGE;
    case IS_HOSTILE: return !SPECIES_FRIENDLY[sp] || x == DISH_PET;
    case IS_BAD_FOR_CILIATE: return sp == SP_PHAGE || predator_kind(sp);
    case IS_THREAT_TO: return (d->strain[x].strat.id == ST_HUNT && d->strain[x].strat.target == s) ||
                              ((predator_kind(sp) || sp == SP_PHAGE) && sdist(&d->strain[s], &d->strain[x]) < 180);
    }
    return true;
}
static int first(const dish_t *d, int s, int what) {
    for (int q = 0; q < 3; q++) {
        int x = d->strain[s].other[q];
        if (x >= 0 && d->strain[x].used && d->strain[x].count && is(d, s, x, what)) return x;
    }
    return -1;
}

static strat_t rules_goal(const dish_t *d, int s) {
    const strain_t *st = &d->strain[s];
    int sp = st->species;
    if (sp == SP_PHAGE) { int t = first(d, s, IS_HOSTILE); return pick(ST_HUNT, t >= 0 ? t : first(d, s, IS_ANY), 6); }
    if (sp == SP_MOLD) return pick(ST_REST, -1, 2);
    if (sp == SP_CILIATE) {
        int bad = first(d, s, IS_BAD_FOR_CILIATE);
        if (bad >= 0) return pick(ST_HUNT, bad, 7);
        int pet = first(d, s, IS_PET);
        return pet >= 0 ? pick(ST_GUARD, pet, 4) : pick(ST_EXPLORE, -1, 3);
    }
    if (predator_kind(sp) && s != DISH_PET) {
        if (st->energy < 0.9f) {
            int weakest = -1;
            for (int q = 0; q < 3; q++) {
                int x = st->other[q];
                if (x >= 0 && d->strain[x].count && d->strain[x].species != SP_PHAGE && (weakest < 0 || d->strain[x].count < d->strain[weakest].count)) weakest = x;
            }
            if (weakest >= 0) return pick(ST_HUNT, weakest, 7);
        }
        return pick(ST_REST, -1, 2);
    }
    if (s != DISH_PET && SPECIES_FRIENDLY[sp]) {
        int pet = first(d, s, IS_PET);
        return pet >= 0 ? pick(ST_BEFRIEND, pet, 4) : pick(ST_EXPLORE, -1, 3);
    }
    if (s != DISH_PET) return st->energy < 0.4f ? pick(ST_FORAGE, -1, 6) : st->crowd > 0.6f ? pick(ST_EXPLORE, -1, 4) : pick(ST_FORAGE, -1, 4);
    /* the pet */
    const uint8_t *m = st->mut;
    bool sporer = sp == SP_BACILLUS || sp == SP_MYXO || m[M_SPOREBURST];
    int threat = first(d, s, IS_THREAT_TO);
    if (threat >= 0) {
        if (st->health < 0.3f && sporer) return pick(ST_SPORE, -1, 9);
        if ((fights(m) || predator_kind(sp)) && st->health > 0.5f) return pick(ST_HUNT, threat, 8);
        if (st->count < d->strain[threat].count * 2 || st->health < 0.5f) return pick(ST_FLEE, threat, 8);
        return pick(ST_CLUSTER, -1, 6);
    }
    if (st->comfort < 0.3f && sporer) return pick(ST_SPORE, -1, 7);
    if (m[M_PHOTO] && d->beam > 0.1f) return pick(ST_SEEK_LIGHT, -1, 5);
    if (st->energy < 0.35f) return pick(ST_FORAGE, -1, 7);
    int friend = first(d, s, IS_FRIEND);
    if (friend >= 0 && m[M_SYMBIOSIS]) return pick(ST_BEFRIEND, friend, 4);
    int prey = first(d, s, IS_PREYABLE);
    if (prey >= 0 && (m[M_ENGULF] || m[M_GRAPPLE] || predator_kind(sp)) && st->energy < 0.8f) return pick(ST_HUNT, prey, 6);
    if (st->crowd > 0.6f) return pick(ST_EXPLORE, -1, 4);
    if (!d->light && st->energy > 0.3f) return pick(ST_REST, -1, 2);
    if (st->energy > 0.75f) return pick(ST_DIVIDE, -1, 4);
    return pick(m[M_SWARM] ? ST_CLUSTER : ST_FORAGE, -1, 4);
}

/* the rules' decision, and what the strain says about it (the AI answers both itself) */
strat_t dish_brain_rules(const dish_t *d, int s) {
    strat_t r = rules_goal(d, s);
    r.say = dish_say_rules(d, s, r.id, r.target);
    return r;
}
