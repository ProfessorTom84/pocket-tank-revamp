/* dish_brain.c — what a strain decides, and the line a model would read.
 *
 * The rule brain stands in until the dish model is distilled (the plan's
 * phase 6); dish_encode is the state line the teacher will label, written
 * now so the rules and the model see the same world. */
#include "dish.h"
#include <stdio.h>

static int d9(float v) { v *= 9; return v < 0 ? 0 : v > 9 ? 9 : (int)(v + 0.5f); }

/* the food around a strain: the mean of the field over a disc at its centroid */
static float food_near(const dish_t *d, const strain_t *st) {
    float s = 0; int n = 0;
    for (int k = -2; k <= 2; k++)
        for (int m = -2; m <= 2; m++) { s += dish_field(d->nutr, st->cx + k * 22, st->cy + m * 22); n++; }
    return s / n;
}
static int others(const dish_t *d, int strain, bool virus) {
    int n = 0;
    for (int s = 0; s < DISH_MAX_STRAINS; s++)
        if (s != strain && d->strain[s].used && (d->strain[s].species == SP_PHAGE) == virus) n += d->strain[s].count;
    return n;
}

strat_t dish_brain_rules(const dish_t *d, int strain) {
    const strain_t *st = &d->strain[strain];
    strat_t r = { st->strat.id, 5, 1 };
    if (st->species == SP_PHAGE) { r.id = ST_HUNT; return r; }
    float food = food_near(d, st);
    bool sporer = st->species == SP_BACILLUS || st->species == SP_MYXO;
    if (st->health < 0.4f || st->infected > 0.3f) { r.id = sporer ? ST_SPORE : ST_HIDE; r.urgency = 8; return r; }
    if (st->spores > st->count / 2 && st->health < 0.7f) { r.id = ST_SPORE; return r; }     /* wait it out */
    /* the whole dish is wrong (pH, salt, heat everywhere): hiding can't help, so a sporer sits it out */
    if (st->comfort < 0.3f && sporer) { r.id = ST_SPORE; r.urgency = 7; return r; }
    if (st->comfort < 0.45f) { r.id = ST_HIDE; r.urgency = 7; return r; }
    if (!d->light && st->energy > 0.3f) { r.id = ST_REST; r.urgency = 2; return r; }
    if (st->energy < 0.35f) { r.id = food > 0.08f ? ST_FORAGE : ST_REST; r.urgency = 7; return r; }
    if (st->species == SP_MYXO && others(d, strain, false) > 0 && st->energy < 0.6f) { r.id = ST_HUNT; return r; }   /* hunts when hungry */
    if (st->crowd > 0.6f) { r.id = ST_SPREAD; return r; }
    if (st->energy > 0.75f && food > 0.05f) { r.id = ST_DIVIDE; r.urgency = 4; return r; }
    r.id = st->species == SP_COCCUS || st->species == SP_CYANO ? ST_SWARM : ST_FORAGE;
    r.urgency = 4;
    return r;
}

void dish_encode(const dish_t *d, int strain, char *out, int n) {
    const strain_t *st = &d->strain[strain];
    int cnt = st->count > 99 ? 99 : st->count;
    snprintf(out, (size_t)n,
             "species %s count %d energy %d health %d comfort %d crowd %d filth %d infected %d "
             "food %d rivals %d phage %d spores %d temp %d ph %d salt %d uv %s light %s last %s",
             SPECIES_NAMES[st->species], cnt, d9(st->energy), d9(st->health), d9(st->comfort), d9(st->crowd),
             d9(st->filth * 2), d9(st->infected), d9(food_near(d, st) * 3), others(d, strain, false) > 9 ? 9 : others(d, strain, false),
             others(d, strain, true) > 9 ? 9 : others(d, strain, true), d9(st->count ? (float)st->spores / st->count : 0),
             d9(d->temp), d9(d->ph), d9(d->salt), d->uv_t > 0 ? "on" : "off", d->light ? "on" : "off", STRAT_NAMES[st->strat.id]);
}
