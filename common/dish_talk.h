/* dish_talk.h — what the germs say: a closed set of things to say, each with an emoji and three
 * phrasings (short, medium, long). The strain's mind picks WHAT to say (the say word: the AI's
 * fourth answer word, or the rules); the words around it are picked here. Bubbles (dish_ui.c)
 * show them over the cell that speaks, and the story writes the memorable ones down. */
#ifndef POCKET_DISH_TALK_H
#define POCKET_DISH_TALK_H

#include "dish.h"

enum { SAY_HELLO, SAY_FRIEND, SAY_SHARE, SAY_THANKS, SAY_PROTECT, SAY_LUNCH, SAY_TAUNT, SAY_ATTACK, SAY_RUN,
       SAY_SCARED, SAY_HIDE, SAY_HUNGRY, SAY_YUM, SAY_GROW, SAY_SLEEPY, SAY_CURIOUS, SAY_SUNNY, SAY_SHADE,
       SAY_SQUISHED, SAY_TOGETHER, SAY_BRAVE, SAY_OUCH, SAY_PROUD, SAY_BORED, SAY_INFECT, SAY_ZAP, SAY_BYE,
       SAY_HELP, SAY_COUNT };
/* the emoji (drawn by dish_ui.c) */
enum { EMO_WAVE, EMO_HEART, EMO_FOOD, EMO_HAPPY, EMO_SHIELD, EMO_YUM, EMO_ANGRY, EMO_BOLT, EMO_SCARED,
       EMO_MOON, EMO_SPARKLE, EMO_ZZZ, EMO_EYES, EMO_SUN, EMO_SAD, EMO_QUESTION, EMO_EXCLAIM, EMO_STAR,
       EMO_RING, EMO_WALL, EMO_COUNT };

extern const char *const SAY_WORDS[SAY_COUNT];   /* the say word the AI answers with ("lunch") */
extern const uint8_t SAY_EMOJI[SAY_COUNT];

/* the line: phrasing 0 short .. 2 long; {T} becomes the target's name. Returns the emoji. */
int dish_say_text(int say, int variant, const char *target, char *out, int n);
/* which phrasing a strain uses now (the same for the bubble and the story) */
int dish_say_variant(const dish_t *d, int strain);
/* the rules' say word for a decision (the AI picks its own) */
uint8_t dish_say_rules(const dish_t *d, int strain, int goal, int target);
/* the giant's finger: what a poked or flicked germ says */
int dish_poke_text(const dish_t *d, int strain, bool flicked, char *out, int n);

#endif
