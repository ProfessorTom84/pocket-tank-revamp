/* dish_render.h — the dark-field microscope: draws a dish_t into RGB565.
 * Same contract as render.h: a bare framebuffer, no LVGL/SDL. */
#ifndef POCKET_DISH_RENDER_H
#define POCKET_DISH_RENDER_H

#include <stdint.h>
#include "dish.h"

/* The glass, the agar and the rim are baked once into a scene cache
 * (DISH_W * DISH_H uint16) and copied each frame. NULL = rebuilt every frame. */
void dish_render_set_scene(uint16_t *buf);
void dish_render(const dish_t *d, uint16_t *fb, int stride);

/* The microscope lens: a magnified live view of the world around (wx, wy),
 * drawn in a circle of radius rad centred at screen (cx, cy). */
void dish_render_lens(const dish_t *d, float wx, float wy, float zoom, uint16_t *fb, int stride, float cx, float cy, float rad);

/* The mood the pet's faces wear: 0 sad .. 1 happy (dish_ui.c sets it from the needs). */
extern float dish_render_mood;

/* drawing helpers for the pages (frame coordinates) */
uint32_t dr_hsv(float h, float s, float v);                      /* 0xRRGGBB */
void dr_add(uint16_t *fb, int stride, int x, int y, uint32_t rgb, int a);       /* additive, a 0..255 */
void dr_blend(uint16_t *fb, int stride, int x, int y, uint32_t rgb, int a);     /* over, a 0..255 */
void dr_rect(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb, int a);
void dr_round_rect(uint16_t *fb, int stride, int x, int y, int w, int h, int r, uint32_t rgb, int a);
void dr_glow(uint16_t *fb, int stride, float x, float y, float r, uint32_t rgb, int a);   /* soft additive dot */
void dr_disc(uint16_t *fb, int stride, float x, float y, float r, uint32_t rgb, int a);   /* antialiased, over */
void dr_ring(uint16_t *fb, int stride, float x, float y, float r, float w, uint32_t rgb, int a);
void dr_line(uint16_t *fb, int stride, float x0, float y0, float x1, float y1, float w, uint32_t rgb, int a);
int  dr_text(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const char *s);   /* returns the width */
int  dr_text_w(const char *s, int scale);
/* a cell drawn on its own (the dex, the card): species look, genome color, at (x, y) scale s */
void dr_cell_icon(uint16_t *fb, int stride, species_t sp, const genome_t *g, float x, float y, float s, float ang, float clock, bool face, float clip_r);   /* clip_r > 0: clipped to that circle */

#endif
