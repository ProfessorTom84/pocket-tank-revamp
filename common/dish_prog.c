/* dish_prog.c — see dish_prog.h. */
#include "dish_prog.h"

const char *const BADGE_NAMES[BADGE_COUNT] = {
    "FIRST SPLIT", "GENERATION 10", "GENERATION 50", "A HUNDRED", "COMEBACK", "SURVIVOR",
    "RESISTANT", "SHELLED", "FIRE GERM", "ICE GERM", "SPORES", "THE ZOO" };
const char *const BADGE_SAYS[BADGE_COUNT] = {
    "YOUR GERM SPLIT IN TWO.", "TEN GENERATIONS OF YOUR STRAIN.", "FIFTY GENERATIONS!",
    "A HUNDRED OF YOUR GERMS AT ONCE.", "BACK FROM THE SPORE VAULT.", "LIVED THROUGH A VIRUS OUTBREAK.",
    "EVOLVED TO SHRUG OFF MEDICINE.", "EVOLVED A THICK SALT SHELL.", "EVOLVED TO LOVE THE HEAT.",
    "EVOLVED TO LOVE THE COLD.", "HID AS SPORES TO STAY ALIVE.", "FIVE KINDS OF GERM AT ONCE." };
const char *const FORM_NAMES[FORM_COUNT] = { "WARM FORM", "ICY FORM", "SHELL FORM", "ARMOR FORM" };
const char *const FORM_SAYS[FORM_COUNT] = { "RAISED WARM, IT TURNS RED.", "RAISED COLD, IT TURNS BLUE.",
                                            "RAISED SALTY, IT GROWS A SHELL.", "RAISED ON MEDICINE: ARMOR." };
const char *const UNL_NAMES[UNL_COUNT] = { "PH DIAL", "SALT DIAL", "ANTIBIOTIC", "UV LAMP", "CYANO", "SPIRILLUM", "MYXO" };
const uint16_t UNL_COST[UNL_COUNT] = { 20, 30, 40, 60, 40, 60, 100 };

bool dish_has(const dish_t *d, int unl) { return unl < 0 || (d->unlocked >> unl & 1); }
bool dish_prog_unlock(dish_t *d, int unl) {
    if (unl < 0 || unl >= UNL_COUNT || dish_has(d, unl) || d->dna < UNL_COST[unl]) return false;
    d->dna -= UNL_COST[unl];
    d->unlocked |= 1u << unl;
    return true;
}
int dish_unl_for_species(species_t sp) {
    return sp == SP_CYANO ? UNL_CYANO : sp == SP_SPIRILLUM ? UNL_SPIRILLUM : sp == SP_MYXO ? UNL_MYXO : -1;
}

void dish_prog_care(dish_t *d, int kind) {
    const strain_t *pet = &d->strain[DISH_PET];
    if (d->care_cd > 0 || pet->count == 0) return;
    bool needed = kind == CARE_FEED ? pet->energy < 0.4f : kind == CARE_CLEAN ? pet->filth > 0.12f : pet->infected > 0;
    if (!needed) return;
    d->dna += kind == CARE_CURE ? 3 : 2;
    d->care_cd = 15;
}

static void award(dish_t *d, int b) {
    if (d->badges >> b & 1) return;
    d->badges |= 1u << b; d->badges_new |= 1u << b;
    d->dna += BADGE_DNA;
    dish_emit(DEV_BADGE, b);
}

void dish_prog_tick(dish_t *d, float dt) {
    if (d->care_cd > 0) d->care_cd -= dt;
    const strain_t *pet = &d->strain[DISH_PET];
    /* the pet's divisions pay: one DNA for every four */
    while (pet->births >= d->births_paid + 4) { d->births_paid += 4; d->dna++; }
    if (pet->births > 0) award(d, BADGE_FIRST_SPLIT);
    if (pet->max_gen >= 10) award(d, BADGE_GEN10);
    if (pet->max_gen >= 50) award(d, BADGE_GEN50);
    if (pet->count >= 100) award(d, BADGE_CROWD);
    if (pet->spores >= 3) award(d, BADGE_SPORES);
    if (pet->count > 0) {
        const genome_t *g = &pet->vault;
        if (g->drug_res > 0.4f) award(d, BADGE_RESIST);
        if (g->salt_tol > 0.75f) award(d, BADGE_SHELL);
        if (g->heat_opt > 0.68f) award(d, BADGE_FIRE);
        if (g->heat_opt < 0.32f) award(d, BADGE_ICE);
    }
    /* the dex: who is here, and what any strain has become */
    int kinds = 0, phages = 0;
    for (int s = 0; s < DISH_MAX_STRAINS; s++) {
        const strain_t *st = &d->strain[s];
        if (!st->used || st->count == 0) continue;
        d->dex |= DEX_SPECIES(st->species);
        kinds++;
        if (st->species == SP_PHAGE) phages = st->count;
        const genome_t *g = &st->vault;
        if (g->heat_opt > 0.62f) d->dex |= DEX_FORM(FORM_WARM);
        if (g->heat_opt < 0.38f) d->dex |= DEX_FORM(FORM_ICY);
        if (g->salt_tol > 0.6f && st->species != SP_PHAGE) d->dex |= DEX_FORM(FORM_SHELL);
        if (g->drug_res > 0.35f) d->dex |= DEX_FORM(FORM_ARMOR);
    }
    if (kinds >= 5) award(d, BADGE_ZOO);
    /* back from the vault */
    if (pet->extinct_t > 0) d->was_extinct = 1;
    else if (d->was_extinct && pet->count > 0) { d->was_extinct = 0; award(d, BADGE_COMEBACK); }
    /* an outbreak survived: the phages came and went, and the pet is still here */
    if (phages > 0) d->outbreak_on = 1;
    else if (d->outbreak_on) { d->outbreak_on = 0; if (pet->count > 0) award(d, BADGE_SURVIVOR); }
}
