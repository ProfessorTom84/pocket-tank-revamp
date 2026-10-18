/* dish_ui.h — the pet layer over the dish: the glass is the interface.
 *
 * The platforms only route: they pass touches in (frame coordinates), the
 * box's tilt to dish_set_tilt, and call dish_ui_tick / dish_ui_render once a
 * frame. On the agar: HOLD to shine light (food blooms under it), DRAG for a
 * current, TAP a germ to poke it (it answers, and the lens shows it), SWIPE a
 * germ fast to flick it away, DOUBLE-TAP for day or night. The germs talk in
 * speech bubbles - tap one to hear what it means. Three corner buttons: STORY
 * (top left), DISH (top right), WALL (bottom left: draw a wall). A banner
 * shows what the colony is asking for, or the wave that is coming. Nothing is
 * left unexplained: the first time each thing matters, a hint says what to do.
 * A mutation draft fills the screen and the dish waits (dish_ui_paused). */
#ifndef POCKET_DISH_UI_H
#define POCKET_DISH_UI_H

#include "dish.h"

typedef enum { PAGE_NONE, PAGE_CARD, PAGE_DRAFT, PAGE_DEX, PAGE_BADGES, PAGE_RUNOVER, PAGE_STORY } page_t;

typedef struct { int8_t strain; uint8_t emoji; uint32_t cell_id; float t, ttl, delay; char text[72]; } dish_bubble_t;
#define DISH_BUBBLES 5

typedef struct {
    float mood;                  /* 0 sad .. 1 happy, eased */
    page_t page;
    int lens;                    /* the cell under the microscope, -1 none */
    uint32_t lens_id;            /* (its id: a reused slot is a different cell) */
    float lens_zoom;
    /* touch tracking */
    bool down, dragged, lighting; int on_button;
    float dx0, dy0, lx, ly, down_t, press_t;
    int down_cell; uint32_t down_id;   /* the germ the finger came down on (a flick, or a poke) */
    bool walling; int wall_i;          /* the WALL button is armed / the wall being drawn */
    /* speech bubbles, and the explanation bar */
    dish_bubble_t bub[DISH_BUBBLES];
    float chat_cd;
    char info[128]; float info_t; float info_x, info_y;   /* (info_x < 0: no pointer) */
    float hint_cd;
    int prev_quest, prev_wave;
    float tap_t, tap_x, tap_y;   /* the last tap: how long ago, where (a double-tap is the lamp) */
    /* the story page */
    int story_tab, story_top; float scroll_acc;
    /* toasts and moments */
    char toast[64]; float toast_t;
    float extinct_show, back_show, confirm_t;
    float clock;
    int sel;                     /* the dex's / badges' tile being read, -1 none */
    int setup, starter, name_i;  /* the first-run setup: its page (0 = done), the chosen starter and name */
    /* sounds waiting for the platform (dish_ui_take_sound), and each cue's cooldown */
    int snd_q[8], snd_pitch[8], snd_n;
    float snd_cd[32];
    int prev_page, prev_lens;
} dish_ui_t;

void dish_ui_init(dish_ui_t *u);
void dish_ui_tick(dish_ui_t *u, dish_t *d, float dt);
void dish_ui_render(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int stride);
void dish_ui_touch(dish_ui_t *u, dish_t *d, float x, float y, bool down);   /* call on every touch sample: down, move, up */
void dish_ui_event(dish_ui_t *u, const dish_t *d, int ev, int strain);     /* forward dish events here (toasts) */
void dish_ui_toast(dish_ui_t *u, const char *s, float seconds);
void dish_ui_begin_setup(dish_ui_t *u);                                     /* a new dish: welcome, pick a starter, name it, tips */
bool dish_ui_paused(const dish_ui_t *u);                                    /* true: don't tick the dish (a draft waits for its pick) */
/* the next sound to play (SND_* from sounds.h, its pitch_q8 in *pitch: 256 = as recorded), -1 when none */
int  dish_ui_take_sound(dish_ui_t *u, int *pitch);
/* what a strain is thinking, in words: "BLIP: HUNTING THE VIBRIO  SURE 82%" */
void dish_ui_thought(const dish_t *d, int strain, char *out, int n);
void dish_ui_say(dish_ui_t *u, const dish_t *d, int strain, int emoji, const char *text, float delay);   /* a speech bubble */
int  dish_ui_bubble_at(const dish_ui_t *u, const dish_t *d, float x, float y);   /* the bubble under a point, -1 none */
/* first-time hints (bits of dish_t.hints) */
enum { HINT_LIGHT, HINT_TALK, HINT_VISITOR, HINT_QUEST, HINT_WALL, HINT_FLICK, HINT_STORY, HINT_DISH, HINT_WAVE, HINT_TILT, HINT_POKE, HINT_COUNT };

/* layout, so a test or the director can tap things without a literal pixel */
typedef enum { HS_DONE, HS_STORY, HS_DISH, HS_CARD_DEX, HS_CARD_BADGES, HS_CARD_NEWRUN,
               HS_STARTER_0, HS_STARTER_1, HS_STARTER_2, HS_STARTER_3, HS_STARTER_4, HS_STARTER_5,
               HS_NAME_PREV, HS_NAME_NEXT, HS_NAME, HS_DRAFT_0, HS_DRAFT_1, HS_DRAFT_2,
               HS_TAB_JOURNAL, HS_TAB_CHARTS, HS_STORY_BACK, HS_WALL, HS_QUEST } hotspot_t;   /* (NEW RUN on the run-over page is HS_DONE) */
void dish_ui_tile(page_t page, int i, float *x, float *y);   /* the centre of the dex's / badges' i-th tile */
void dish_ui_hotspot(hotspot_t h, float *x, float *y);       /* the centre of a page's button */
#define DISH_BTN_R 21

#endif
