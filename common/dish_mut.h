/* dish_mut.h — mutations: the strain's build, one card at a time like a roguelite.
 *
 * Each mutation is a new thing the cells DO (zap, engulf, jet, flock...), not
 * a stat: the physics reads the strain's rank (tier 1-3) and acts on it, the
 * renderer draws it, and its trait word goes into the line the AI reads, so
 * the model learns when to use it.
 *
 * A draft is dealt when one is due and the dish waits for the pick. The deal
 * is weighted by what the dish is doing to the strain: each mutation answers
 * some PRESSURES, and a strong pressure always gets an answer among the three. */
#ifndef POCKET_DISH_MUT_H
#define POCKET_DISH_MUT_H

#include "dish.h"

enum { M_SPIKES, M_TOXIN, M_ZAP, M_JET, M_ENGULF, M_SWARM, M_BIOFILM, M_PHOTO,
       M_LURE, M_SPOREBURST, M_TAMER, M_MIMIC, M_SCOUTS, M_GRAPPLE, M_SYMBIOSIS, M_QUORUM, M_COUNT };
enum { MR_COMMON, MR_RARE, MR_EPIC };

/* what the dish can be doing to a strain (bit i = pressure i) */
enum { P_HUNTER, P_VIRUS, P_DRUG, P_ACID, P_SALT, P_STARVE, P_DARK, P_CROWD, P_RIVAL, P_COUNT };
extern const char *const PRESSURE_NAMES[P_COUNT];

typedef struct {
    const char *name;     /* the card's title */
    const char *says[3];  /* what each tier does */
    const char *word;     /* the trait word the AI reads (with the tier: "zap2") */
    const char *adj;      /* for the form's name: "SPARKING" */
    uint8_t rarity;
    uint16_t answers;     /* the pressures it helps against */
} mut_info_t;
extern const mut_info_t MUTS[M_COUNT];
#define MUT_TIERS 3

void  dish_pressures(const dish_t *d, float out[P_COUNT]);    /* 0..1 each, for the pet strain */
void  dish_draft_deal(dish_t *d);                             /* fills d->offer[3] when a draft is due and none is dealt */
bool  dish_draft_take(dish_t *d, int card);                   /* takes d->offer[card]: the next tier of it */
void  dish_mut_take(dish_t *d, int strain, int m);            /* a mutation's tier +1 */
int   dish_mut_total(const dish_t *d, int strain);            /* tiers taken */
void  dish_mut_preview(const dish_t *d, int m, uint8_t *mut, genome_t *g);   /* the pet as it would be with m */
void  dish_form_name(const dish_t *d, int strain, char *out, int n);         /* "SPARKING SWARMING BACILLUS" */
void  dish_form_name_of(const uint8_t *mut, species_t sp, char *out, int n);
int   dish_top_traits(const uint8_t *mut, int *out, int n);   /* the strongest n mutations (tier, then rarity) */

#endif
