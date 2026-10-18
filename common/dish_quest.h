/* dish_quest.h — the colony asks for things, and now and then the dish is attacked.
 *
 * A QUEST is one request at a time from the pet ("SHINE LIGHT ON US!", "TAKE US TO THE RING!",
 * "FLICK THE VIBRIOS AWAY!"), picked from what is going on in the dish, with a timer and a DNA
 * reward. A WAVE is announced from one side of the glass ("PHAGES FROM THE LEFT IN 5..."), then
 * comes: the keeper defends with currents, light, walls and flicks, and the losses are counted. */
#ifndef POCKET_DISH_QUEST_H
#define POCKET_DISH_QUEST_H

#include "dish.h"

enum { Q_NONE, Q_LIGHT, Q_GROW, Q_HERD, Q_DEFEND, Q_FLICK, Q_WALL, Q_FRIEND, Q_COUNT };
enum { QS_NONE, QS_ON, QS_WON, QS_LOST };
enum { WAVE_NONE, WAVE_COMING, WAVE_ON };
#define WAVE_WARN_S  5.0f
#define WAVE_FIGHT_S 35.0f

void  dish_quest_tick(dish_t *d, float dt);                 /* (dish_prog_tick calls it) */
void  dish_quest_start(dish_t *d, int kind);                /* a quest now (the tests; false starts are ignored) */
void  dish_wave_start(dish_t *d, int side, species_t sp);   /* a wave announced now (the tests) */
const char *dish_quest_name(int kind);                      /* "LIGHT" for the story */
void  dish_quest_text(const dish_t *d, char *out, int n);   /* the pill: "SHINE LIGHT ON US" */
void  dish_quest_ask(const dish_t *d, char *out, int n);    /* what the pet says when it asks */
const char *dish_quest_help(int kind);                      /* how to do it, for a tap on the pill */
float dish_quest_progress(const dish_t *d);                 /* 0..1 */
void  dish_wave_origin(const dish_t *d, float *x, float *y);/* where the wave comes in */

#endif
