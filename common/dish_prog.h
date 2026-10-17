/* dish_prog.h — the long game: DNA points, badges, the Microbe-dex, unlocks.
 *
 * Everything is detected from what happens in the dish, never scripted (the
 * tank's rule). DNA is earned by caring (feeding a hungry strain, cleaning a
 * dirty dish, curing a sick one), by the pet's own divisions and by badges,
 * and spent on lab tools and new germs. The state lives in dish_t and is
 * saved with it. */
#ifndef POCKET_DISH_PROG_H
#define POCKET_DISH_PROG_H

#include "dish.h"

enum { BADGE_FIRST_SPLIT, BADGE_GEN10, BADGE_GEN50, BADGE_CROWD, BADGE_COMEBACK, BADGE_SURVIVOR,
       BADGE_RESIST, BADGE_SHELL, BADGE_FIRE, BADGE_ICE, BADGE_SPORES, BADGE_ZOO, BADGE_COUNT };
extern const char *const BADGE_NAMES[BADGE_COUNT];
extern const char *const BADGE_SAYS[BADGE_COUNT];
#define BADGE_DNA 10

/* the dex: the six species, then the evolved forms any strain can take */
enum { FORM_WARM, FORM_ICY, FORM_SHELL, FORM_ARMOR, FORM_COUNT };
#define DEX_SPECIES(sp) (1u << (sp))
#define DEX_FORM(f)     (1u << (8 + (f)))
extern const char *const FORM_NAMES[FORM_COUNT];
extern const char *const FORM_SAYS[FORM_COUNT];

/* what DNA buys */
enum { UNL_PH, UNL_SALT, UNL_DRUG, UNL_UV, UNL_CYANO, UNL_SPIRILLUM, UNL_MYXO, UNL_COUNT };
extern const char *const UNL_NAMES[UNL_COUNT];
extern const uint16_t UNL_COST[UNL_COUNT];

enum { CARE_FEED, CARE_CLEAN, CARE_CURE };

void dish_prog_tick(dish_t *d, float dt);            /* (dish_tick calls it) */
void dish_prog_care(dish_t *d, int kind);            /* a good deed: pays DNA when it was needed */
bool dish_has(const dish_t *d, int unl);
bool dish_prog_unlock(dish_t *d, int unl);           /* false: not enough DNA (or already owned) */
int  dish_unl_for_species(species_t sp);             /* the unlock a sample needs, -1 none */

#endif
