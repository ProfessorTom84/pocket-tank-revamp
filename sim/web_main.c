/* web_main.c — Pocket Dish in a browser (Emscripten -> WebAssembly).
 *
 * The same common/ code as the board and the simulator; the page
 * (sim/web/index.html) runs the loop, draws the RGB565 frame onto a canvas,
 * turns pointer events into touches, plays the cues through Web Audio and
 * keeps the save in localStorage. One module per glass: the board's shape is
 * compiled in (TANK_ROUND / TANK_WATCH / TANK_SQUARE), as on the device. */
#include <emscripten/emscripten.h>
#include <stdlib.h>
#include <string.h>
#include "dish.h"
#include "dish_render.h"
#include "dish_ui.h"
#include "sounds.h"

static dish_t dish;
static dish_ui_t ui;
static uint16_t fb[DISH_W * DISH_H], scene[DISH_W * DISH_H], bg[DISH_W * DISH_H];
static uint8_t save_buf[DISH_SAVE_MAX];
static float speed = 1;

static void on_event(int ev, int strain, void *ud) { (void)ud; dish_ui_event(&ui, &dish, ev, strain); }

EMSCRIPTEN_KEEPALIVE int web_w(void) { return DISH_W; }
EMSCRIPTEN_KEEPALIVE int web_h(void) { return DISH_H; }
EMSCRIPTEN_KEEPALIVE uint16_t *web_fb(void) { return fb; }
EMSCRIPTEN_KEEPALIVE uint8_t *web_save_buf(void) { return save_buf; }   /* the page writes a save here before web_init */

/* save_len > 0: resume the dish the page kept (the time away lived through first); else a new dish and its setup */
EMSCRIPTEN_KEEPALIVE void web_init(uint32_t seed, int save_len, float away_s) {
    dish_render_set_scene(scene);
    dish_render_set_background(bg);
    dish_events_set(on_event, NULL);
    dish_ui_init(&ui);
    if (save_len > 0 && dish_load(&dish, save_buf, (size_t)save_len, seed | 1)) {
        if (away_s > 0) dish_sleep(&dish, away_s);
    } else {
        dish_init(&dish, seed | 1, SP_BACILLUS, "BLIP");
        dish_ui_begin_setup(&ui);
    }
}

/* one frame: dt seconds of the world (x10 when fast), then the picture */
EMSCRIPTEN_KEEPALIVE void web_frame(float dt) {
    if (dt > 0.1f) dt = 0.1f;
    for (int k = 0; k < (int)speed; k++) { dish_tick(&dish, dt, dish_brain_rules); dish_ui_tick(&ui, &dish, dt); }
    dish_render(&dish, fb, DISH_W);
    dish_ui_render(&ui, &dish, fb, DISH_W);
}

EMSCRIPTEN_KEEPALIVE void web_touch(float x, float y, int down) { dish_ui_touch(&ui, &dish, x, y, down != 0); }

EMSCRIPTEN_KEEPALIVE int web_save(void) { return (int)dish_save(&dish, save_buf, sizeof save_buf); }

/* the next sound: the clip to play (offset and length into the bank, in samples), its pitch and gain (q8);
 * -1 when none is waiting, 0 when the cue has no recording yet */
static int s_clip[4];
EMSCRIPTEN_KEEPALIVE int *web_sound(void) {
    int pitch, cue;
    while ((cue = dish_ui_take_sound(&ui, &pitch)) >= 0) {
        const snd_cue_t *q = &SND_CUES[cue];
        if (!q->n_var) continue;                         /* deferred: a valid cue with nothing recorded */
        const snd_clip_t *c = &SND_CLIPS[q->first + rand() % q->n_var];
        s_clip[0] = (int)c->off; s_clip[1] = (int)c->len; s_clip[2] = pitch; s_clip[3] = q->gain_q8 ? q->gain_q8 : 256;
        return s_clip;
    }
    return NULL;
}

/* the keys the desktop window has: N light, O outbreak, F 10x time, D +50 DNA */
EMSCRIPTEN_KEEPALIVE void web_key(int c) {
    switch (c) {
    case 'n': dish.light = !dish.light; break;
    case 'o': dish_outbreak(&dish, 6); break;
    case 'f': speed = speed > 1 ? 1 : 10; break;
    case 'd': dish.dna += 50; break;
    }
}
EMSCRIPTEN_KEEPALIVE int web_fast(void) { return speed > 1; }
EMSCRIPTEN_KEEPALIVE void web_reset(uint32_t seed) { web_init(seed, 0, 0); }
