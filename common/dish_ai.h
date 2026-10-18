/* dish_ai.h — every strain's mind, on a small language model (model/schema_dish.md).
 *
 * The model is distilled from gemma4:26b (docs/dish-ai.md): it reads the state
 * line dish_encode writes and answers "<goal> <target> urgency <d>". Each of
 * the three answer words is picked from its closed set (the goals, then
 * other1..3 / none, then 0..9), so the answer is always a valid decision; the
 * goal's softmax is how sure the strain is.
 *
 * The dish asks through dish_ai_brain (a dish_brain_fn): it posts the strain's
 * line and answers at once - with the model's last answer for that strain
 * when it is fresh, else with the rules (dish_brain_rules). The thinking
 * happens in dish_ai_step, one strain per call, the neediest first (its
 * situation changed most, it has waited longest, it is the pet): on the board
 * a task on core 1 calls it in a loop; the sim and the browser call it once a
 * frame. Without a model every call answers with the rules. */
#ifndef POCKET_DISH_AI_H
#define POCKET_DISH_AI_H

#include "dish.h"

/* model: a version-3 q4 export (q4_model.h), mapped or in RAM; tok: its tokenizer.bin (schema 10).
 * false: no model, a bad file, or not enough memory - the rules think instead. */
bool dish_ai_open(const uint8_t *model, size_t model_len, const uint8_t *tok, size_t tok_len, void *(*alloc)(size_t));
bool dish_ai_ready(void);
strat_t dish_ai_brain(const dish_t *d, int strain);   /* pass this to dish_tick */
bool dish_ai_step(void);                               /* think for one strain; false when no one is waiting */
/* the one decode: a state line -> a decision (target as other1..3 -> the strain ids in other[]); false on failure */
bool dish_ai_decide(const char *line, const int8_t other[3], strat_t *out);
/* the board runs dish_ai_step on the other core: the posts and answers are guarded by these (default: none) */
void dish_ai_set_lock(void (*lock)(void), void (*unlock)(void));
typedef struct { uint32_t asked, answered, used, fallback; float ms_mean; } dish_ai_stats_t;
dish_ai_stats_t dish_ai_stats(void);
int dish_ai_answers(int strain);                      /* the model's answers for the strain in that slot so far */
extern int64_t (*dish_ai_clock_us)(void);             /* optional: times each decode for the stats */

/* the test model: random weights over the real vocabulary, so the whole decode path runs in the
 * selftests without the trained file. Writes the model and its tokenizer into the caller's buffers
 * (sizes from dish_ai_test_model_size); returns false if they are too small. */
size_t dish_ai_test_model_size(size_t *tok_len);
bool   dish_ai_test_model(uint8_t *model, size_t model_len, uint8_t *tok, size_t tok_len, uint32_t seed);

#endif
