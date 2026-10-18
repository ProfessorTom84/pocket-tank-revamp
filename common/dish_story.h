/* dish_story.h — what happened in the dish, kept for the STORY page.
 *
 * Two records live in dish_t (and so in the save): a log of events (a ring of
 * 96, each the minute it happened, a type, who and whom) and a sample of the
 * dish once a minute (a ring of 120: two hours). The journal is written from
 * the log by templates; the charts read the samples. */
#ifndef POCKET_DISH_STORY_H
#define POCKET_DISH_STORY_H

#include "dish.h"

/* a = the actor's species, b = the other's species, v = a goal or a mutation, x = a number */
enum { SE_START, SE_VISIT, SE_LEAVE, SE_WIPED, SE_DECIDE, SE_MUT, SE_EXTINCT, SE_COMEBACK, SE_RUN_OVER,
       SE_RECORD, SE_BLOOM, SE_ZAP, SE_TAMED, SE_GIFT, SE_LOSS, SE_COUNT };

void dish_story_note(dish_t *d, int type, int a, int b, int v, uint16_t x);
void dish_story_death(dish_t *d, int strain, int cause);
void dish_story_birth(dish_t *d, int strain);
void dish_story_tick(dish_t *d, float dt);

/* reading it back: the i-th newest event (0 = newest), and its sentence */
int  dish_story_count(const dish_t *d);
bool dish_story_line(const dish_t *d, int i, char *out, int n, int *minute);
/* the i-th oldest minute sample (0 = oldest kept) */
int  dish_story_samples(const dish_t *d);
const void *dish_story_sample(const dish_t *d, int i);   /* points into d->series */
extern const char *const DIE_NAMES[DIE_COUNT];

#endif
