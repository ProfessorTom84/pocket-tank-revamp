/* dish_ui.h — the pet layer over the dish: needs, the icon arc, the strain
 * card, the lab and the medicine panels, the microscope, extinction.
 *
 * The platforms only route: they pass touches in (frame coordinates) and
 * call dish_ui_tick / dish_ui_render once a frame. Gestures on the agar:
 * tap a cell to look at it, tap the agar to feed, drag to stir, hold still
 * to warm, three quick raps to scare. With a tool in hand (the arc's MEDS
 * or CLEAN) the agar takes that tool instead. */
#ifndef POCKET_DISH_UI_H
#define POCKET_DISH_UI_H

#include "dish.h"

typedef enum { ICON_FEED, ICON_LAB, ICON_MEDS, ICON_CLEAN, ICON_ZOOM, ICON_CARD, ICON_COUNT } icon_t;
typedef enum { PAGE_NONE, PAGE_CARD, PAGE_LAB, PAGE_MEDS, PAGE_SAMPLES, PAGE_DEX, PAGE_BADGES } page_t;
typedef enum { TOOL_NONE, TOOL_DRUG, TOOL_WIPE, TOOL_ZOOM } tool_t;
typedef enum { NEED_FOOD, NEED_COMFORT, NEED_SPACE, NEED_CLEAN, NEED_HEALTH, NEED_COUNT } need_t;
extern const char *const NEED_NAMES[NEED_COUNT];

typedef struct {
    float needs[NEED_COUNT];     /* 0 bad .. 1 good, eased */
    float mood;                  /* 0 sad .. 1 happy */
    page_t page;
    tool_t tool; float tool_idle;
    int lens;                    /* the cell under the microscope, -1 none */
    uint32_t lens_id;            /* (its id: a reused slot is a different cell) */
    float lens_zoom;
    /* touch tracking */
    bool down; float dx0, dy0, lx, ly, down_t, still_t; bool dragged, warmed;
    float tap_t[3]; int taps;
    /* toasts and moments */
    char toast[40]; float toast_t;
    float emote_cd, emote_t; int emote;   /* emote_cd: the wait before the next emote */
    float extinct_show, back_show;
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
void dish_ui_begin_setup(dish_ui_t *u);
/* the next sound to play (SND_* from sounds.h, its pitch_q8 in *pitch: 256 = as recorded), -1 when none.
 * The dish has no sounds of its own yet: it plays the tank's bank at other pitches. */
int  dish_ui_take_sound(dish_ui_t *u, int *pitch);                                     /* a new dish: welcome, pick a starter, name it, tips */

/* layout, so a test or the director can tap things without a literal pixel */
void dish_ui_icon_pos(icon_t i, float *x, float *y);
typedef enum { HS_LAB_MINUS, HS_LAB_PLUS, HS_LAB_LIGHT, HS_DONE, HS_MEDS_DRUG, HS_MEDS_VIRUS, HS_TOOL_DONE,
               HS_PH_MINUS, HS_PH_PLUS, HS_SALT_MINUS, HS_SALT_PLUS, HS_LAB_UV, HS_LAB_SAMPLES, HS_SAMPLE_0, HS_SAMPLE_1, HS_SAMPLE_2,
               HS_CARD_DEX, HS_CARD_BADGES, HS_STARTER_0, HS_STARTER_1, HS_STARTER_2, HS_NAME_PREV, HS_NAME_NEXT, HS_NAME } hotspot_t;
void dish_ui_tile(page_t page, int i, float *x, float *y);   /* the centre of the dex's / badges' i-th tile */
void dish_ui_hotspot(hotspot_t h, float *x, float *y);   /* the centre of a page's button */
#define DISH_ICON_R 19

#endif
