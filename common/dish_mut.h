/* dish_mut.h — mutations: the strain's build, drafted 1 of 3 like a roguelite.
 *
 * A mutation is either a GENOME edit (applied to every living cell and the
 * vault the moment it is taken, then inherited and drifting like any gene) or
 * a POWER the physics reads from the strain's ranks (spikes, a toxin, eating
 * other germs...). Each one changes how the cells look too (dish_render.c),
 * so every build is a different organism.
 *
 * The draft is weighted by what the dish is doing to the strain right now:
 * each mutation answers some PRESSURES, and a strong pressure puts at least
 * one answer among the three cards. */
#ifndef POCKET_DISH_MUT_H
#define POCKET_DISH_MUT_H

#include "dish.h"

enum {
    /* body */      M_BIG, M_SPIKES, M_SHELL, M_SLIME, M_SLIM,
    /* motion */    M_FLAGELLA, M_CILIA, M_GLIDE, M_TWITCH,
    /* metabolism */M_GLUTTON, M_CHLORO, M_SCAVENGER, M_FERMENT, M_FASTDIV,
    /* defense */   M_EFFLUX, M_PHAGESHIELD, M_HEATSHOCK, M_COLDSHOCK, M_SALTPUMP, M_ACIDARMOR, M_BASEARMOR,
    /* social */    M_QUORUM, M_BIOFILM, M_SIGNAL,
    /* powers */    M_TOXIN, M_PREDATOR, M_SPOREMASTER, M_GLOW, M_GENETHIEF, M_CRISPR,
    M_COUNT
};
enum { MF_BODY, MF_MOTION, MF_METAB, MF_DEFENSE, MF_SOCIAL, MF_POWER };
enum { MR_COMMON, MR_RARE, MR_EPIC };

/* what the dish can be doing to a strain (bit i = pressure i) */
enum { P_HEAT, P_COLD, P_SALT, P_ACID, P_BASE, P_DRUG, P_VIRUS, P_HUNTER, P_RIVAL, P_STARVE, P_CROWD, P_DARK, P_COUNT };
extern const char *const PRESSURE_NAMES[P_COUNT];

typedef struct {
    const char *name;     /* the card's title */
    const char *says;     /* one line: what it does */
    const char *adj;      /* for the form's name: "SPINY" */
    uint8_t family, rarity, max_rank;
    uint16_t answers;     /* the pressures it helps against */
} mut_info_t;
extern const mut_info_t MUTS[M_COUNT];

void  dish_pressures(const dish_t *d, float out[P_COUNT]);    /* 0..1 each, for the pet strain */
void  dish_draft_deal(dish_t *d);                             /* fills d->offer[3] (when a draft is waiting and none is dealt) */
bool  dish_draft_take(dish_t *d, int card);                   /* takes d->offer[card]: applies it, a draft fewer */
void  dish_mut_take(dish_t *d, int strain, int m);            /* a mutation's rank +1, and its genome edit */
int   dish_mut_total(const dish_t *d, int strain);            /* ranks taken */
void  dish_mut_preview(const dish_t *d, int m, uint8_t *mut, genome_t *g);   /* the pet as it would be with m: its ranks and genome */
void  dish_form_name(const dish_t *d, int strain, char *out, int n);   /* "SPINY GLOWING BACILLUS" */
void  dish_form_name_of(const uint8_t *mut, species_t sp, char *out, int n);

#endif
