#define _POSIX_C_SOURCE 200809L
/* dish_main.c — the Pocket Dish simulator.
 *
 * Headless always works (selftests, --snapshot, --narrate); with SDL2 found
 * at build time (DISH_SDL) it also opens a window where the mouse is the
 * finger. Same flags as fishsim where they mean the same thing. */
#include "dish.h"
#include "dish_render.h"
#include "dish_ui.h"
#include "dish_prog.h"
#include "sounds.h"
#include "dish_mut.h"
#include "dish_story.h"
#include "dish_ai.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stddef.h>

static dish_t dish;
static int64_t now_us(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000; }
static dish_ui_t ui;
static void on_event(int ev, int strain, void *ud) { (void)ud; dish_ui_event(&ui, &dish, ev, strain); }
static uint16_t fb[DISH_W * DISH_H], scene[DISH_W * DISH_H], bg[DISH_W * DISH_H], fb2[DISH_W * DISH_H];

static void write_ppm(const char *path, const uint16_t *px) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", DISH_W, DISH_H);
    for (int i = 0; i < DISH_W * DISH_H; i++) {
        uint16_t p = px[i];
        unsigned char rgb[3] = { (unsigned char)((p >> 11) << 3 | (p >> 13)), (unsigned char)(((p >> 5) & 63) << 2 | ((p >> 9) & 3)), (unsigned char)((p & 31) << 3 | ((p >> 2) & 7)) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("wrote %s\n", path);
}

static void census_line(const dish_t *d) {
    printf("t=%6.0fs live %3d |", d->clock, d->n_live);
    for (int s = 0; s < DISH_MAX_STRAINS; s++) {
        const strain_t *st = &d->strain[s];
        if (!st->used) continue;
        printf(" %s:%d e%.2f h%.2f %s%s |", st->name, st->count, st->energy, st->health, STRAT_NAMES[st->strat.id],
               st->extinct_t > 0 ? " (extinct)" : "");
    }
    float food = 0; for (int c = 0; c < FIELD_N * FIELD_N; c++) food += d->nutr[c];
    printf(" food %.1f\n", food);
}

/* run the dish for `seconds` of sim time at 30 Hz; feed every `feed_s` (0 = never) */
static bool auto_pick = true;                       /* a draft pauses the dish: the test keeper takes the first card */
static dish_brain_fn brain = dish_brain_rules;
static void tap_at(float x, float y);
static void run(dish_t *d, float seconds, float feed_s, bool print) {
    float next_feed = feed_s, next_print = 0;
    for (float t = 0; t < seconds; t += 1 / 30.0f) {
        if (dish_ui_paused(&ui)) {
            if (!auto_pick) { dish_ui_tick(&ui, d, 1 / 30.0f); continue; }
            float hx, hy; dish_ui_hotspot(HS_DRAFT_0, &hx, &hy); tap_at(hx, hy);
        }
        dish_tick(d, 1 / 30.0f, brain);
        dish_ui_tick(&ui, d, 1 / 30.0f);
        if (feed_s > 0 && t >= next_feed) {
            next_feed += feed_s;
            const strain_t *pet = &d->strain[DISH_PET];
            float x = pet->count ? pet->cx : DISH_CX, y = pet->count ? pet->cy : DISH_CY;
            dish_feed(d, x + dish_rand(d, -20, 20), y + dish_rand(d, -20, 20), 1.0f);
        }
        if (print && t >= next_print) { census_line(d); next_print += 30; }
    }
}

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static int selftest_dish(void) {
    /* 0. the glass: the whole screen, its own corners */
    CHECK(fabsf(dish_edge_dist(DISH_CX, 0.5f) - 0.5f) < 0.01f, "the top of the glass is %.2f px from the top edge", dish_edge_dist(DISH_CX, 0.5f));
    CHECK(fabsf(dish_edge_dist(0.5f, DISH_CY) - 0.5f) < 0.01f, "the side of the glass is %.2f px from the side edge", dish_edge_dist(0.5f, DISH_CY));
    CHECK(!dish_inside(1, 1, 0), "the frame's very corner counts as glass");
    CHECK(dish_inside(DISH_CORNER_R, DISH_CORNER_R, DISH_CORNER_R * 0.4f), "a corner's arc centre is not well inside");
    {   float x = 2, y = DISH_H - 2; dish_toward_inside(&x, &y, 12);
        CHECK(fabsf(dish_edge_dist(x, y) - 12) < 0.05f, "toward_inside left a corner point %.2f px in, not 12", dish_edge_dist(x, y)); }
    /* 1. a tended dish lives and stays inside its pool */
    dish_init(&dish, 7, SP_BACILLUS, "BLIP");
    run(&dish, 600, 40, true);
    CHECK(dish.strain[DISH_PET].count > 0 || dish.strain[DISH_PET].extinct_t > 0, "the pet neither lives nor waits in the vault");
    CHECK(dish.n_live <= DISH_MAX_CELLS, "pool overflow");
    CHECK(dish.strain[DISH_PET].births > 5, "a fed strain never divided (%u births)", dish.strain[DISH_PET].births);
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        const cell_t *c = &dish.cell[i];
        if (c->state == CS_LIVE) CHECK(dish_inside(c->x, c->y, 0), "a cell left the dish at %.0f,%.0f", c->x, c->y);
    }
    /* 2. an untended dish starves: no immortality without food */
    dish_init(&dish, 11, SP_BACILLUS, "BLIP");
    run(&dish, 1800, 0, false);
    census_line(&dish);
    CHECK(dish.strain[DISH_PET].deaths > 0, "nothing ever starved in 30 minutes without food");
    /* 3. an outbreak: phages infect, hosts burst, the phages run out */
    dish_init(&dish, 13, SP_COCCUS, "DOT");
    run(&dish, 60, 30, false);
    dish_outbreak(&dish, 1);                          /* (makes sure the phage strain exists) */
    for (int s = 0; s < DISH_MAX_STRAINS; s++)        /* then a cloud of them right on the colony: no luck involved */
        if (dish.strain[s].used && dish.strain[s].species == SP_PHAGE)
            for (int k = 0; k < 16; k++) dish_spawn(&dish, s, dish.strain[DISH_PET].cx + dish_rand(&dish, -25, 25), dish.strain[DISH_PET].cy + dish_rand(&dish, -25, 25), NULL);
    unsigned deaths0 = dish.strain[DISH_PET].deaths + dish.strain[1].deaths;
    run(&dish, 240, 30, false);
    census_line(&dish);
    CHECK(dish.strain[DISH_PET].deaths + dish.strain[1].deaths > deaths0, "an outbreak killed nobody");
    /* 4. extinction -> the spore vault brings the pet back within 30 s */
    dish_init(&dish, 17, SP_BACILLUS, "BLIP");
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET && dish.cell[i].state == CS_LIVE) dish.cell[i].state = CS_DYING;
    run(&dish, 2, 0, false);
    CHECK(dish.strain[DISH_PET].count == 0 && dish.strain[DISH_PET].extinct_t > 0, "a wiped-out pet did not go extinct");
    run(&dish, 31, 0, false);
    CHECK(dish.strain[DISH_PET].count > 0, "the vault never brought the pet back");
    /* 5. time away: a fed dish sleeps through a night; days away and the vault has to bring it back */
    dish_init(&dish, 19, SP_BACILLUS, "BLIP");
    run(&dish, 120, 20, false);
    dish_sleep(&dish, 9 * 3600); run(&dish, 0.1f, 0, false);
    CHECK(dish.strain[DISH_PET].count > 0, "a fed pet did not survive a night asleep");
    dish_sleep(&dish, 4 * 86400.0f); run(&dish, 0.1f, 0, false);
    CHECK(dish.strain[DISH_PET].count == 0 && dish.strain[DISH_PET].extinct_t > 0, "four days asleep and the pet is fine (%d cells)", dish.strain[DISH_PET].count);
    run(&dish, 31, 0, false);
    CHECK(dish.strain[DISH_PET].count > 0, "the vault did not bring the pet back after the time away");
    printf("selftest-dish: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

static void shot(const char *pre, const char *name) {
    char path[512];
    dish_render(&dish, fb, DISH_W);
    dish_ui_render(&ui, &dish, fb, DISH_W);
    snprintf(path, sizeof path, "%s%s.ppm", pre, name);
    write_ppm(path, fb);
}
static void tap_at(float x, float y) { dish_ui_touch(&ui, &dish, x, y, true); dish_ui_touch(&ui, &dish, x, y, false); }
static void tap_hs(hotspot_t h) { float x, y; dish_ui_hotspot(h, &x, &y); tap_at(x, y); }
/* a spot of bare agar, away from every cell (so a tap or a hold lands on nothing) */
static void bare_spot(float *x, float *y) {
    for (int k = 0; k < 400; k++) {
        float tx = DISH_CX + dish_rand(&dish, -DISH_RX * 0.6f, DISH_RX * 0.6f), ty = DISH_CY + dish_rand(&dish, -DISH_RY * 0.5f, DISH_RY * 0.5f);
        bool clear = true;
        for (int i = 0; i < DISH_MAX_CELLS && clear; i++)
            if (dish.cell[i].state != CS_FREE && fabsf(dish.cell[i].x - tx) < 30 && fabsf(dish.cell[i].y - ty) < 30) clear = false;
        if (clear) { *x = tx; *y = ty; return; }
    }
    *x = 30; *y = DISH_CY;
}
/* the pet's cell nearest its colony's middle (away from the corner buttons) */
static int pet_cell(void) {
    int best = -1; float bd = 1e9f; const strain_t *p = &dish.strain[DISH_PET];
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        const cell_t *c = &dish.cell[i];
        if (c->strain != DISH_PET || c->state != CS_LIVE) continue;
        float d = (c->x - p->cx) * (c->x - p->cx) + (c->y - p->cy) * (c->y - p->cy);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
static int snapshot(const char *pre) {
    dish_render_set_scene(scene);
    dish_events_set(on_event, NULL);
    dish_init(&dish, 3, SP_BACILLUS, "BLIP");
    dish_ui_init(&ui);
    run(&dish, 150, 25, false);
    dish_visit(&dish, SP_VIBRIO); dish_visit(&dish, SP_SYMBIONT);
    run(&dish, 25, 25, false);
    ui.toast_t = 0;
    shot(pre, "dish");
    /* the light: a finger held on the agar */
    {   float x, y; bare_spot(&x, &y);
        dish_ui_touch(&ui, &dish, x, y, true); run(&dish, 1.2f, 0, false); shot(pre, "light"); dish_ui_touch(&ui, &dish, x, y, false); }
    {   int c = pet_cell(); if (c >= 0) { tap_at(dish.cell[c].x, dish.cell[c].y); run(&dish, 0.1f, 0, false); shot(pre, "lens"); ui.lens = -1; } }
    ui.tap_t = 9; tap_hs(HS_DISH); shot(pre, "card"); tap_at(DISH_CX, DISH_CY);
    tap_hs(HS_STORY); shot(pre, "story"); tap_hs(HS_TAB_CHARTS); shot(pre, "charts"); tap_hs(HS_STORY_BACK);
    /* extinct: the vault */
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET && dish.cell[i].state != CS_FREE) dish.cell[i].state = CS_DYING;
    run(&dish, 8, 0, false); shot(pre, "extinct");
    run(&dish, 25, 10, false);
    /* a build: what drafting does to a strain, and the interactions it brings */
    { static const int B[] = { M_SPIKES, M_SPIKES, M_ZAP, M_ZAP, M_PHOTO, M_TOXIN, M_GRAPPLE, M_SWARM, M_BIOFILM };
      for (int k = 0; k < 9; k++) dish_mut_take(&dish, DISH_PET, B[k]); }
    dish_visit(&dish, SP_MOLD); dish_visit(&dish, SP_CILIATE);
    run(&dish, 60, 10, false);
    ui.toast_t = 0; shot(pre, "interactions");
    auto_pick = false;
    dish.drafts = 1; dish.offer[0] = dish.offer[1] = dish.offer[2] = 0xFF;
    run(&dish, 0.1f, 0, false); shot(pre, "draft");
    tap_hs(HS_DRAFT_1); auto_pick = true;
    /* show off evolution: a warm-raised, salt-shelled, drug-armored pet */
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        cell_t *c = &dish.cell[i];
        if (c->strain != DISH_PET) continue;
        c->g.heat_opt = 0.5f + (i % 3) * 0.12f; c->g.salt_tol = 0.5f + (i % 4) * 0.12f; c->g.drug_res = (i % 2) * 0.7f;
    }
    run(&dish, 0.5f, 0, false); ui.toast_t = 0; shot(pre, "zoo");
    int ci = -1; for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET && dish.cell[i].state == CS_LIVE && dish.cell[i].g.drug_res > 0.5f && dish.cell[i].g.salt_tol > 0.7f) { ci = i; break; }
    if (ci >= 0) { ui.lens = ci; ui.lens_id = dish.cell[ci].id; shot(pre, "evolved"); }
    ui.lens = -1;
    /* the card's buttons, the dex, the badges, the setup */
    dish.badges |= 1u << BADGE_FIRST_SPLIT | 1u << BADGE_GEN10 | 1u << BADGE_COMEBACK | 1u << BADGE_SHELL | 1u << BADGE_ZOO;
    dish.badges_new = 1u << BADGE_ZOO;
    run(&dish, 0.2f, 0, false);
    ui.toast_t = 0; tap_hs(HS_DISH); shot(pre, "card2");
    tap_hs(HS_CARD_DEX);
    float hx, hy; dish_ui_tile(PAGE_DEX, SP_MYXO, &hx, &hy); tap_at(hx, hy); shot(pre, "dex");
    tap_hs(HS_DONE); tap_hs(HS_CARD_BADGES);
    dish_ui_tile(PAGE_BADGES, BADGE_SHELL, &hx, &hy); tap_at(hx, hy); shot(pre, "badges");
    ui.page = PAGE_NONE; ui.toast_t = 0;
    dish_ui_begin_setup(&ui);
    shot(pre, "setup1"); tap_hs(HS_DONE);
    shot(pre, "setup2"); tap_hs(HS_DONE);
    shot(pre, "setup3"); tap_hs(HS_DONE);
    shot(pre, "setup4");
    return 0;
}

/* the controls: hold for light, drag for a current, tilt, tap to look, double-tap for the lamp, the corners */
static int selftest_pet(void) {
    dish_events_set(on_event, NULL);
    dish_init(&dish, 21, SP_BACILLUS, "BLIP");
    dish_ui_init(&ui);
    run(&dish, 20, 0, false);
    int pitch; while (dish_ui_take_sound(&ui, &pitch) >= 0) {}
    /* HOLD: light, and food blooms under it; nothing opens */
    float x, y; bare_spot(&x, &y);
    float n0 = dish_field(dish.nutr, x, y);
    dish_ui_touch(&ui, &dish, x, y, true); run(&dish, 3, 0, false);
    CHECK(dish.beam > 0.8f && fabsf(dish.beam_x - x) < 1, "a held finger made no light (%.2f)", dish.beam);
    CHECK(dish_field(dish.nutr, x, y) > n0 + 0.05f, "no food bloomed under the light (%.3f -> %.3f)", n0, dish_field(dish.nutr, x, y));
    dish_ui_touch(&ui, &dish, x, y, false);
    CHECK(ui.lens < 0 && ui.page == PAGE_NONE, "letting go of the light opened something");
    run(&dish, 3, 0, false);
    CHECK(dish.beam < 0.2f, "the light did not fade after the finger lifted (%.2f)", dish.beam);
    /* DRAG: a current along the stroke */
    bare_spot(&x, &y);
    float x0 = x - 60;
    dish_ui_touch(&ui, &dish, x0, y, true);
    for (int k = 1; k <= 8; k++) { dish_ui_touch(&ui, &dish, x0 + k * 15, y, true); run(&dish, 1 / 30.0f, 0, false); }
    int fc = (int)(y / (DISH_H / (float)FIELD_N)) * FIELD_N + (int)((x0 + 60) / (DISH_W / (float)FIELD_N));
    CHECK(dish.flowx[fc] > 20, "a drag made no current (%.1f px/s)", dish.flowx[fc]);
    dish_ui_touch(&ui, &dish, x0 + 120, y, false);
    CHECK(dish.beam < 0.05f, "a drag made light");
    /* TILT: everything drifts downhill */
    run(&dish, 6, 0, false);
    {   float y0 = dish.strain[DISH_PET].cy;
        dish_set_tilt(&dish, 0, 1); run(&dish, 5, 0, false); dish_set_tilt(&dish, 0, 0);
        CHECK(dish.strain[DISH_PET].cy > y0 + 15 || dish.strain[DISH_PET].cy > DISH_H * 0.75f, "tilting down moved the colony %.0f px", dish.strain[DISH_PET].cy - y0); }
    /* TAP a germ: the lens, and its thought */
    run(&dish, 0.5f, 0, false);
    int c = pet_cell();
    ui.tap_t = 9;
    if (c >= 0) tap_at(dish.cell[c].x, dish.cell[c].y);
    CHECK(c >= 0 && ui.lens == c, "a tap on a germ did not look at it");
    {   char t[64]; dish_ui_thought(&dish, DISH_PET, t, sizeof t);
        CHECK(!strncmp(t, "BLIP: ", 6) && strstr(t, "SURE"), "the pet's thought reads \"%s\"", t); }
    /* DOUBLE-TAP: the lamp */
    run(&dish, 1, 0, false); bare_spot(&x, &y);
    bool lamp = dish.light;
    tap_at(x, y); run(&dish, 0.1f, 0, false); tap_at(x + 3, y);
    CHECK(dish.light != lamp, "a double-tap did not switch the lamp");
    run(&dish, 1, 0, false); tap_at(x, y);
    CHECK(dish.light != lamp, "a single tap switched the lamp");
    dish.light = true;
    /* the corners: STORY and DISH */
    run(&dish, 1, 0, false); while (dish_ui_take_sound(&ui, &pitch) >= 0) {}
    tap_hs(HS_STORY); CHECK(ui.page == PAGE_STORY, "STORY did not open the story");
    run(&dish, 0.05f, 0, false);
    int heard = -1; for (int k; (k = dish_ui_take_sound(&ui, &pitch)) >= 0;) if (k == SND_CARD_OPEN) heard = k;
    CHECK(heard == SND_CARD_OPEN, "the story opened silently");
    tap_hs(HS_TAB_CHARTS); CHECK(ui.story_tab == 1, "CHARTS did not switch the tab");
    tap_hs(HS_STORY_BACK); CHECK(ui.page == PAGE_NONE, "BACK did not close the story");
    tap_hs(HS_DISH); CHECK(ui.page == PAGE_CARD, "DISH did not open the card");
    tap_hs(HS_CARD_DEX); CHECK(ui.page == PAGE_DEX, "the card's DEX did not open the dex");
    tap_hs(HS_DONE); CHECK(ui.page == PAGE_CARD, "the dex's BACK did not go back to the card");
    tap_at(DISH_CX, DISH_CY); CHECK(ui.page == PAGE_NONE, "a tap off the card's buttons did not close it");
    /* the save round-trips, the story with it */
    run(&dish, 125, 20, false);
    static uint8_t buf[DISH_SAVE_MAX]; static dish_t back;
    size_t n = dish_save(&dish, buf, sizeof buf);
    CHECK(n > 0 && n < DISH_SAVE_MAX, "save failed (%zu bytes)", n);
    printf("save: %zu bytes\n", n);
    CHECK(dish_load(&back, buf, n, 5), "load failed");
    int a = 0, b = 0;
    for (int i = 0; i < DISH_MAX_CELLS; i++) { a += dish.cell[i].state == CS_LIVE || dish.cell[i].state == CS_SPORE; b += back.cell[i].state == CS_LIVE || back.cell[i].state == CS_SPORE; }
    CHECK(a == b, "cells %d saved, %d loaded", a, b);
    CHECK(!strcmp(back.strain[DISH_PET].name, "BLIP"), "the pet's name was lost");
    CHECK(fabsf(back.strain[DISH_PET].vault.heat_opt - dish.strain[DISH_PET].vault.heat_opt) < 1e-6f, "the vault genome changed");
    {   char l0[96], l1[96]; int m0 = 0, m1 = 0;
        bool ok = dish_story_count(&dish) > 0 && dish_story_line(&dish, 0, l0, sizeof l0, &m0) && dish_story_line(&back, 0, l1, sizeof l1, &m1);
        CHECK(ok && back.log_n == dish.log_n && back.series_n == dish.series_n && !strcmp(l0, l1) && m0 == m1 && dish.series_n >= 2,
              "the story did not survive the save (%d/%d lines, %d/%d samples)", back.log_n, dish.log_n, back.series_n, dish.series_n); }
    CHECK(!dish_load(&back, buf, n - 1, 5), "a truncated save loaded");
    back.dna = 777; CHECK(!dish_load(&back, buf, n + 5, 5) && back.dna == 777, "a padded save loaded, or touched the dish");
    {   /* each cell's age comes back (old age keeps working across power-offs) */
        float age_a = 0, age_b = 0;
        for (int i = 0; i < DISH_MAX_CELLS; i++) { if (dish.cell[i].state == CS_LIVE) age_a += (int)dish.cell[i].age; if (back.cell[i].state == CS_LIVE) age_b += back.cell[i].age; }
        CHECK(fabsf(age_a - age_b) < 1, "the cells' ages did not survive the save (%.0f vs %.0f)", age_a, age_b);
    }
    {   /* a NaN in the save's pH is refused, not loaded into the world */
        static uint8_t bad[DISH_SAVE_MAX]; memcpy(bad, buf, n);
        float nan = NAN; memcpy(bad + 6 + 13, &nan, 4);          /* (header 6, then temp, clock, outbreak_t, light: the pH) */
        CHECK(dish_load(&back, bad, n, 5) && back.ph == 0.5f, "a NaN pH loaded as %f", back.ph);
    }
    buf[0] ^= 0xFF;
    CHECK(!dish_load(&back, buf, n, 5), "a corrupt save loaded");
    run(&dish, 1, 0, false);
    printf("selftest-pet: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

#ifdef DISH_SDL
#include <SDL.h>
/* the window: the mouse is the finger, the arrow keys tilt the box. Keys: N lamp, O an outbreak, F 10x time,
 * D fifty DNA to try the unlocks, S save now, X a fresh dish (with the setup), Q quit. The dish saves to dish.sav every 30 s. */
static void save_file(void) {
    static uint8_t buf[DISH_SAVE_MAX];
    size_t n = dish_save(&dish, buf, sizeof buf);
    FILE *f = n ? fopen("dish.sav", "wb") : NULL;
    if (f) { fwrite(buf, 1, n, f); fclose(f); }
}
static bool load_file(void) {
    static uint8_t buf[DISH_SAVE_MAX];
    FILE *f = fopen("dish.sav", "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
    return dish_load(&dish, buf, n, (uint32_t)SDL_GetTicks() | 1);
}
static int window(bool fresh) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL: %s\n", SDL_GetError()); return 1; }
    const int scale = 2;
    SDL_Window *w = SDL_CreateWindow("Pocket Dish", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DISH_W * scale, DISH_H * scale, 0);
    SDL_Renderer *r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, DISH_W, DISH_H);
    dish_render_set_scene(scene);
    dish_events_set(on_event, NULL);
    dish_ui_init(&ui);
    if (fresh || !load_file()) { dish_init(&dish, SDL_GetTicks() | 1, SP_BACILLUS, "BLIP"); dish_ui_begin_setup(&ui); }   /* a new dish: the setup over a demo dish */
    float speed = 1, save_t = 30;
    bool down = false;
    uint32_t last = SDL_GetTicks();
    for (bool quit = false; !quit;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            else if (e.type == SDL_MOUSEBUTTONDOWN) { down = true; dish_ui_touch(&ui, &dish, e.button.x / (float)scale, e.button.y / (float)scale, true); }
            else if (e.type == SDL_MOUSEMOTION && down) dish_ui_touch(&ui, &dish, e.motion.x / (float)scale, e.motion.y / (float)scale, true);
            else if (e.type == SDL_MOUSEBUTTONUP) { down = false; dish_ui_touch(&ui, &dish, e.button.x / (float)scale, e.button.y / (float)scale, false); }
            else if (e.type == SDL_KEYDOWN) switch (e.key.keysym.sym) {
                case SDLK_q: quit = true; break;
                case SDLK_n: dish.light = !dish.light; break;
                case SDLK_o: dish_outbreak(&dish, 6); break;
                case SDLK_f: speed = speed > 1 ? 1 : 10; printf("time x%.0f\n", speed); break;
                case SDLK_s: save_file(); printf("saved dish.sav\n"); break;
                case SDLK_x: dish_init(&dish, SDL_GetTicks() | 1, SP_BACILLUS, "BLIP"); dish_ui_init(&ui); dish_ui_begin_setup(&ui); break;
                case SDLK_d: dish.dna += 50; printf("+50 DNA (%u)\n", dish.dna); break;
            }
        }
        {   const uint8_t *k = SDL_GetKeyboardState(NULL);
            dish_set_tilt(&dish, (k[SDL_SCANCODE_RIGHT] - k[SDL_SCANCODE_LEFT]) * 0.8f, (k[SDL_SCANCODE_DOWN] - k[SDL_SCANCODE_UP]) * 0.8f);
        }
        uint32_t now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f; last = now;
        if (dt > 0.1f) dt = 0.1f;
        for (int k = 0; k < (int)speed; k++) { if (!dish_ui_paused(&ui)) dish_tick(&dish, dt, brain); dish_ui_tick(&ui, &dish, dt); }
        for (int64_t t0 = now_us(); dish_ai_ready() && now_us() - t0 < 8000 && dish_ai_step();) {}   /* (the minds: up to 8 ms a frame) */
        if ((save_t -= dt) <= 0) { save_file(); save_t = 30; }
        dish_render(&dish, fb, DISH_W);
        dish_ui_render(&ui, &dish, fb, DISH_W);
        SDL_UpdateTexture(tex, NULL, fb, DISH_W * 2);
        SDL_RenderCopy(r, tex, NULL, NULL);
        SDL_RenderPresent(r);
    }
    save_file();
    SDL_Quit();
    return 0;
}
#endif

static int selftest_evolve(void) {
    /* selection: half the colony resists the drug; dosed, the resistant half is what is left */
    int res = 0, all = 0;
    for (uint32_t seed = 31; seed < 61; seed += 10) {
        dish_init(&dish, seed, SP_BACILLUS, "BLIP"); dish_ui_init(&ui);
        dish.run_muts = 1; dish.visit_t = dish.outbreak_t = 1e9f;
        genome_t g = dish_species_genome(SP_BACILLUS);
        for (int k = 0; k < 24; k++) { g.drug_res = k & 1 ? 0.7f : 0; dish_spawn(&dish, DISH_PET, DISH_CX + dish_rand(&dish, -50, 50), DISH_CY + dish_rand(&dish, -50, 50), &g); }
        for (float t = 0; t < 60; t += 10) { dish_dose(&dish, dish.strain[DISH_PET].cx, dish.strain[DISH_PET].cy, 0.35f); run(&dish, 10, 5, false); }
        for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET && dish.cell[i].state == CS_LIVE) { all++; res += dish.cell[i].g.drug_res > 0.35f; }
    }
    printf("after the drug: %d of %d cells resistant (half were at the start, the founders' few aside)\n", res, all);
    CHECK(all > 0 && res > all * 0.7f, "antibiotic pressure did not select for resistance (%d of %d)", res, all);
    /* comfort: a dish too sour for the pet hurts it; set right, it recovers */
    dish_init(&dish, 33, SP_BACILLUS, "BLIP");
    run(&dish, 30, 15, false);
    dish.ph = 0.05f; run(&dish, 20, 15, false);
    CHECK(dish.strain[DISH_PET].comfort < 0.3f, "a sour dish left the pet comfortable (%.2f)", dish.strain[DISH_PET].comfort);
    dish.ph = 0.5f; run(&dish, 20, 15, false);
    CHECK(dish.strain[DISH_PET].count == 0 || dish.strain[DISH_PET].comfort > 0.7f, "a neutral dish did not comfort the pet");
    /* the samples join, once each */
    CHECK(dish_add_sample(&dish, SP_CYANO) >= 0, "the cyano sample did not join");
    CHECK(dish_add_sample(&dish, SP_CYANO) < 0, "a second cyano colony joined");
    {   /* a sample that died out can be added again */
        int cs = -1; for (int s2 = 0; s2 < DISH_MAX_STRAINS; s2++) if (dish.strain[s2].used && dish.strain[s2].species == SP_CYANO) cs = s2;
        for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == cs && dish.cell[i].state != CS_FREE) dish.cell[i].state = CS_DYING;
        run(&dish, 1, 0, false);
        CHECK(dish_add_sample(&dish, SP_CYANO) == cs, "a cyano colony that died out could not come back");
        run(&dish, 0.1f, 0, false);
        CHECK(dish.strain[cs].count > 0 && dish.strain[cs].extinct_t == 0, "the reseeded cyano is not alive (%d)", dish.strain[cs].count);
    }
    int cy = 0; for (int s = 0; s < DISH_MAX_STRAINS; s++) if (dish.strain[s].used && dish.strain[s].species == SP_CYANO) cy = s;
    int my = dish_add_sample(&dish, SP_MYXO);
    CHECK(my >= 0, "the myxo sample did not join");
    run(&dish, 240, 20, false);
    printf("after 4 min: pet %d, myxo %d, cyano %d\n", dish.strain[DISH_PET].count, dish.strain[my].count, dish.strain[cy].count);
    CHECK(dish.strain[cy].count + dish.strain[DISH_PET].count > 0, "the hunters ate everything in four minutes");
    printf("selftest-evolve: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

static int selftest_prog(void) {
    dish_events_set(on_event, NULL);
    dish_init(&dish, 41, SP_BACILLUS, "BLIP");
    dish_ui_init(&ui);
    CHECK(dish.dna == 0 && dish.badges == 0, "a new dish starts with DNA or badges");
    run(&dish, 60, 20, false);
    CHECK(dish.badges >> BADGE_FIRST_SPLIT & 1, "the first split earned no badge");
    CHECK(dish.dna >= BADGE_DNA, "a badge paid %u DNA", dish.dna);
    CHECK(dish.dex & DEX_SPECIES(SP_BACILLUS), "the dex missed the pet");
    /* buying */
    dish.dna = 30; CHECK(!dish_prog_unlock(&dish, UNL_CYANO), "unlocked the cyano starter on 30 DNA");
    dish.dna = 45; CHECK(dish_prog_unlock(&dish, UNL_CYANO) && dish.dna == 5, "the cyano starter did not cost 40");
    CHECK(!dish_prog_unlock(&dish, UNL_CYANO), "bought the cyano twice");
    /* the starters cost DNA: a locked one can't be picked, and says its price */
    dish.dna = 0; float hx, hy;
    dish_ui_begin_setup(&ui);
    tap_hs(HS_DONE);                                  /* START */
    tap_hs(HS_STARTER_5);                             /* the myxo: 100 DNA, and there is none */
    CHECK(ui.starter != 5, "a locked myxo starter was picked");
    ui.setup = 0; ui.page = PAGE_NONE;
    CHECK(strstr(ui.toast, "100 DNA") != NULL || ui.toast_t <= 0, "a locked starter did not say its price (\"%s\")", ui.toast);
    /* care pays when it was needed, not twice in a row: light for a hungry pet */
    run(&dish, 16, 0, false);
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET) dish.cell[i].energy = 0.2f;
    run(&dish, 0.1f, 0, false);
    unsigned dna0 = dish.dna;
    float x, y; bare_spot(&x, &y);
    dish_ui_touch(&ui, &dish, x, y, true); run(&dish, 0.6f, 0, false); dish_ui_touch(&ui, &dish, x, y, false);
    CHECK(dish.dna == dna0 + 2, "light for a hungry pet paid %u", dish.dna - dna0);
    dish_ui_touch(&ui, &dish, x, y, true); run(&dish, 0.6f, 0, false); dish_ui_touch(&ui, &dish, x, y, false);
    CHECK(dish.dna == dna0 + 2, "a second light paid again at once");
    /* survivor: an outbreak comes and goes, the pet lives */
    dish_outbreak(&dish, 4); run(&dish, 1, 0, false);
    dish_antiviral(&dish); run(&dish, 2, 0, false);
    CHECK(dish.badges >> BADGE_SURVIVOR & 1, "surviving an outbreak earned no badge");
    /* comeback: extinct, then back from the vault */
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET && dish.cell[i].state != CS_FREE) dish.cell[i].state = CS_DYING;
    run(&dish, 33, 0, false);
    CHECK(dish.badges >> BADGE_COMEBACK & 1, "coming back from the vault earned no badge");
    /* the card's DEX and BADGES */
    tap_hs(HS_DISH); tap_hs(HS_CARD_DEX);
    CHECK(ui.page == PAGE_DEX, "the card's DEX did not open the dex");
    dish_ui_tile(PAGE_DEX, SP_PHAGE, &hx, &hy); tap_at(hx, hy);
    CHECK(ui.sel == SP_PHAGE, "a dex tile did not select");
    tap_hs(HS_DONE); tap_hs(HS_CARD_BADGES);
    CHECK(ui.page == PAGE_BADGES && dish.badges_new, "BADGES did not open, or nothing was new");
    tap_hs(HS_DONE);
    CHECK(ui.page == PAGE_CARD && !dish.badges_new, "leaving the badges did not clear the new ones");
    ui.page = PAGE_NONE;
    /* the save keeps it all */
    static uint8_t buf[DISH_SAVE_MAX]; static dish_t back;
    size_t n = dish_save(&dish, buf, sizeof buf);
    CHECK(dish_load(&back, buf, n, 3) && back.dna == dish.dna && back.badges == dish.badges && back.unlocked == dish.unlocked && back.dex == dish.dex,
          "the save lost the progression");
    /* the first-run setup: pick the coccus, the second name, begin */
    unsigned dna_before = dish.dna;
    dish_ui_begin_setup(&ui);
    run(&dish, 8, 0, false);                          /* the demo dish lives behind the setup... */
    CHECK(ui.toast_t <= 0, "a toast (\"%s\") showed over the setup", ui.toast);   /* ...without talking over it */
    tap_hs(HS_DONE);                                  /* START */
    tap_hs(HS_STARTER_1);
    tap_hs(HS_DONE);                                  /* NEXT */
    tap_hs(HS_NAME_NEXT);
    tap_hs(HS_DONE); tap_hs(HS_DONE);                 /* NEXT, BEGIN */
    CHECK(!ui.setup && dish.strain[DISH_PET].species == SP_COCCUS && !strcmp(dish.strain[DISH_PET].name, "DOT"),
          "the setup made a %s called %s", SPECIES_NAMES[dish.strain[DISH_PET].species], dish.strain[DISH_PET].name);
    CHECK(dish.dna == dna_before, "the new dish lost the DNA (%u, was %u): a new germ keeps it", dish.dna, dna_before);
    run(&dish, 0.1f, 0, false);                       /* (the census counts on the next tick) */
    CHECK(dish.strain[DISH_PET].count > 0, "the new dish has no %s", dish.strain[DISH_PET].name);
    printf("selftest-prog: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

static int selftest_render(void) {
    dish_init(&dish, 51, SP_BACILLUS, "BLIP"); dish_ui_init(&ui);
    run(&dish, 200, 15, false); dish_outbreak(&dish, 6); run(&dish, 10, 0, false);
    dish_feed(&dish, DISH_CX, DISH_CY, 1);           /* everything at once: a drop, ripples, the light, thoughts */
    dish.beam = 1; dish.beam_x = DISH_CX - 40; dish.beam_y = DISH_H / 2 - 10;   /* (across the bands' split) */
    for (int k = 0; k < 3; k++)                       /* and food glowing under the lamp's glints on the rim: the two must overlap */
        dish_feed(&dish, DISH_CX + cosf(-2.356f) * (DISH_RX - 14), DISH_CY + sinf(-2.356f) * (DISH_RY - 14), 1);
    run(&dish, 0.1f, 0, false);
    {   /* a bacillus just under the split, heading down: its tail crosses into the top band */
        genome_t g = dish_species_genome(SP_BACILLUS);
        int i = dish_spawn(&dish, DISH_PET, DISH_CX + 30, DISH_H / 2 + 16, &g);   /* (16: past a 3-radius cull, the tail still ~9 px over) */
        if (i >= 0) dish.cell[i].ang = 1.5708f;
    }
    /* 1. no caches: the full frame */
    dish_render_set_scene(NULL); dish_render_set_background(NULL);
    dish_render(&dish, fb, DISH_W);
    /* 2. scene + background caches, rendered as two bands: the same pixels */
    dish_render_set_scene(scene); dish_render_set_background(bg);
    dish_render_band(&dish, fb2, DISH_W, 0, DISH_H / 2);
    dish_render_band(&dish, fb2, DISH_W, DISH_H / 2, DISH_H);
    int diff = 0, lit = 0;
    for (int i = 0; i < DISH_W * DISH_H; i++) { diff += fb[i] != fb2[i]; lit += fb[i] > 0x0841; }
    if (diff) for (int i = 0, shown = 0; i < DISH_W * DISH_H && shown < 8; i++)
        if (fb[i] != fb2[i]) { printf("  differs at %d,%d: %04x vs %04x\n", i % DISH_W, i / DISH_W, fb[i], fb2[i]); shown++; }
    CHECK(diff == 0, "the cached two-band frame differs from the plain frame in %d pixels", diff);
    CHECK(lit > DISH_W * DISH_H / 4, "the frame is mostly black (%d lit)", lit);
    /* 3. a band never writes outside its rows */
    memset(fb2, 0xAB, sizeof fb2);
    dish_render_band(&dish, fb2, DISH_W, DISH_H / 2, DISH_H);
    int stray = 0; for (int i = 0; i < DISH_W * (DISH_H / 2); i++) stray += fb2[i] != 0xABAB;
    CHECK(stray == 0, "the lower band wrote %d pixels into the upper half", stray);
    printf("selftest-render: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

static int ev_count[DEV_COUNT];
static void count_events(int ev, int strain, void *ud) { (void)ud; if (ev >= 0 && ev < DEV_COUNT) ev_count[ev]++; dish_ui_event(&ui, &dish, ev, strain); }
static void kill_pet(void) { for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET && dish.cell[i].state != CS_FREE) dish.cell[i].state = CS_DYING; }
static int lit_diff(const uint16_t *a, const uint16_t *b) { int n = 0; for (int i = 0; i < DISH_W * DISH_H; i++) n += a[i] != b[i]; return n; }

static int selftest_run(void) {
    dish_events_set(count_events, NULL);
    dish_init(&dish, 61, SP_BACILLUS, "BLIP"); dish_ui_init(&ui);
    auto_pick = false;
    run(&dish, 1, 0, false);
    CHECK(dish.strain[1].used == false && dish.n_live == dish.strain[DISH_PET].count, "a new run starts with more than the pet");
    /* 1. a draft comes due, the dish waits for the pick, and they never pile up */
    dish.strain[DISH_PET].max_gen = 40; run(&dish, 0.1f, 0, false);
    CHECK(dish.drafts == 1 && dish.next_draft_gen == 40 + DISH_DRAFT_GENS, "forty generations brought %d drafts (next at %u): it must be one", dish.drafts, dish.next_draft_gen);
    CHECK(ui.page == PAGE_DRAFT && dish_ui_paused(&ui), "a due draft did not take the screen and pause the dish");
    float clk = dish.clock; run(&dish, 2, 0, false);
    CHECK(dish.clock == clk, "the dish ran on while a draft waited");
    dish.strain[DISH_PET].max_gen = 80; dish_prog_tick(&dish, 0.1f);
    CHECK(dish.drafts == 1, "a second draft piled up behind the first (%d)", dish.drafts);
    tap_hs(HS_DONE); CHECK(ui.page == PAGE_DRAFT, "the draft could be put off (there is no LATER)");
    /* 2. a deal is three different cards, and taking one applies it */
    CHECK(dish.offer[0] != 0xFF && dish.offer[1] != 0xFF && dish.offer[2] != 0xFF && dish.offer[0] != dish.offer[1] && dish.offer[1] != dish.offer[2] && dish.offer[0] != dish.offer[2],
          "a deal was not three different cards (%d %d %d)", dish.offer[0], dish.offer[1], dish.offer[2]);
    int m = dish.offer[1], r0 = dish.strain[DISH_PET].mut[m];
    tap_hs(HS_DRAFT_1);
    CHECK(dish.strain[DISH_PET].mut[m] == r0 + 1 && dish.drafts == 0 && dish.offer[0] == 0xFF && ui.page == PAGE_NONE && !dish_ui_paused(&ui), "picking a card did not apply it and resume");
    {   bool noted = false; char l[96]; int mn;
        for (int i = 0; i < dish_story_count(&dish) && dish_story_line(&dish, i, l, sizeof l, &mn); i++) if (strstr(l, MUTS[m].name)) noted = true;
        CHECK(noted, "the pick is not in the story"); }
    /* time away grants at most one */
    dish.strain[DISH_PET].max_gen = 200; dish_sleep(&dish, 3600);
    CHECK(dish.drafts <= 1, "time away left %d drafts waiting", dish.drafts);
    run(&dish, 0.1f, 0, false);
    if (dish_ui_paused(&ui)) tap_hs(HS_DRAFT_0);
    auto_pick = true;
    /* 3. the look changes with the build */
    {   genome_t g = dish_species_genome(SP_BACILLUS); uint8_t mu[DISH_MUT_N] = { 0 };
        memset(fb, 0, sizeof fb); dr_cell_icon(fb, DISH_W, SP_BACILLUS, &g, mu, 100, 100, 4, 0, 0, true, 0);
        for (int k = 0; k < M_COUNT; k++) {
            uint8_t one[DISH_MUT_N] = { 0 }; one[k] = 3;
            memset(fb2, 0, sizeof fb2); dr_cell_icon(fb2, DISH_W, SP_BACILLUS, &g, one, 100, 100, 4, 0, 0.4f, true, 0);
            CHECK(lit_diff(fb, fb2) > 40, "%s does not show on the cell (%d pixels)", MUTS[k].name, lit_diff(fb, fb2));
        } }
    /* 4. the deal answers what the dish is doing: a virus, then a hunter */
    dish_outbreak(&dish, 6); run(&dish, 0.1f, 0, false);
    int answered = 0;
    for (int k = 0; k < 200; k++) {
        dish.drafts = 1; dish.offer[0] = dish.offer[1] = dish.offer[2] = 0xFF; dish_draft_deal(&dish);
        for (int c = 0; c < 3; c++) if (dish.offer[c] != 0xFF && MUTS[dish.offer[c]].answers >> P_VIRUS & 1) { answered++; break; }
    }
    CHECK(answered >= 198, "a virus in the dish, and only %d of 200 deals answered it", answered);
    dish_antiviral(&dish); run(&dish, 1, 0, false);
    dish_visit(&dish, SP_VIBRIO); run(&dish, 0.5f, 0, false);
    answered = 0;
    for (int k = 0; k < 200; k++) {
        dish.drafts = 1; dish.offer[0] = dish.offer[1] = dish.offer[2] = 0xFF; dish_draft_deal(&dish);
        for (int c = 0; c < 3; c++) if (dish.offer[c] != 0xFF && MUTS[dish.offer[c]].answers >> P_HUNTER & 1) { answered++; break; }
    }
    CHECK(answered >= 198, "a hunter in the dish, and only %d of 200 deals answered it", answered);
    dish.drafts = 0; dish.offer[0] = dish.offer[1] = dish.offer[2] = 0xFF;
    for (int s = 1; s < DISH_MAX_STRAINS; s++) if (dish.strain[s].used) dish.strain[s].stay_t = 0;
    run(&dish, 40, 10, false);
    /* 5. one comeback, then the run is over - and what was earned stays */
    dish.dna = 55; dish.badges |= 1u << BADGE_ZOO; dish.dex |= DEX_SPECIES(SP_MOLD); dish.unlocked |= 1u << UNL_CYANO;
    uint32_t badges = dish.badges, dex = dish.dex, unl = dish.unlocked;
    int built = dish_mut_total(&dish, DISH_PET);
    CHECK(dish.run_muts > 0, "(the run has taken mutations, so the vault's one comeback counts)");
    kill_pet(); run(&dish, 31.5f, 0, false);
    CHECK(dish.strain[DISH_PET].count > 0 && dish.vault_used && !dish.run_over, "the first extinction did not come back from the vault");
    CHECK(dish_mut_total(&dish, DISH_PET) == built && built > 0, "the vault lost the build (%d of %d)", dish_mut_total(&dish, DISH_PET), built);
    kill_pet(); run(&dish, 2, 0, false);
    CHECK(dish.run_over && ev_count[DEV_RUN_OVER] == 1 && ui.page == PAGE_RUNOVER, "the second extinction did not end the run (page %d)", ui.page);
    uint32_t dna_kept = dish.dna;                                           /* (badges earned during the run add to it) */
    tap_hs(HS_DONE);                                                        /* NEW RUN */
    CHECK(ui.setup == 2, "NEW RUN did not open the starter pick");
    tap_hs(HS_STARTER_1);                                                   /* the coccus */
    tap_hs(HS_DONE); tap_hs(HS_DONE); tap_hs(HS_DONE);                      /* NEXT, NEXT, BEGIN */
    run(&dish, 0.1f, 0, false);
    CHECK(dish.run == 2 && !dish.run_over && !dish.vault_used && dish.strain[DISH_PET].species == SP_COCCUS && dish_mut_total(&dish, DISH_PET) == 0,
          "the new run is not fresh (run %d, over %d, vault %d, %d mutations)", dish.run, dish.run_over, dish.vault_used, dish_mut_total(&dish, DISH_PET));
    CHECK(dish.dna == dna_kept && dna_kept >= 55 && (dish.badges & badges) == badges && (dish.dex & dex) == dex && dish.unlocked == unl, "the new run lost what was earned (DNA %u of %u)", dish.dna, dna_kept);
    CHECK(ui.page == PAGE_NONE && !ui.setup, "the new run left a page up");
    /* 6. a new run by choice: the DISH page's NEW RUN asks twice */
    tap_hs(HS_DISH); tap_hs(HS_CARD_NEWRUN);
    CHECK(ui.page == PAGE_CARD && !ui.setup, "one tap on NEW RUN ended the run");
    tap_hs(HS_CARD_NEWRUN);
    CHECK(ui.setup == 2, "two taps on NEW RUN did not open the starter pick");
    tap_hs(HS_DONE); tap_hs(HS_DONE); tap_hs(HS_DONE);
    run(&dish, 0.1f, 0, false);
    CHECK(dish.run == 3 && !dish.run_over, "the chosen new run is run %d", dish.run);
    /* 7. a save from before runs (v4) loads: run 1, the dex's forms moved up */
    {   static uint8_t buf[DISH_SAVE_MAX]; static dish_t back;
        FILE *f = fopen("testdata/dish-v4.sav", "rb");
        size_t n = f ? fread(buf, 1, sizeof buf, f) : 0; if (f) fclose(f);
        CHECK(n > 0 && dish_load(&back, buf, n, 3), "the v4 save (testdata/dish-v4.sav) did not load");
        CHECK(back.run == 1 && back.dna == 77 && !strcmp(back.strain[DISH_PET].name, "OLDIE"), "the v4 save loaded wrong (run %d, DNA %u)", back.run, back.dna);
        CHECK((back.dex & DEX_FORM(FORM_SHELL)) && !(back.dex & (1u << 10)) && (back.dex & DEX_SPECIES(SP_COCCUS)), "the v4 dex was not moved (%08x)", back.dex);
        CHECK(back.offer[0] == 0xFF && back.next_draft_gen > back.strain[DISH_PET].max_gen, "the v4 save's draft state is wrong (next at %u)", back.next_draft_gen);
        size_t n6 = dish_save(&back, buf, sizeof buf); static dish_t again;
        CHECK(dish_load(&again, buf, n6, 3) && again.dex == back.dex && again.run == 1, "a migrated save does not round-trip"); }
    printf("selftest-run: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

/* a lazy keeper: drops a little food on the colony every 45 s (the drafts take care of themselves: run() picks) */
static void lazy_run(dish_t *d, float seconds) {
    for (float t = 0; t < seconds && !d->run_over; t += 15) {
        run(d, 15, 0, false);
        const strain_t *pet = &d->strain[DISH_PET];
        if ((int)t % 45 == 0 && pet->count) dish_feed(d, pet->cx, pet->cy, 1.0f);
    }
}
static int selftest_visitors(void) {
    dish_events_set(count_events, NULL);
    /* each visitor does its job */
    #define FRESH(seed) do { dish_init(&dish, seed, SP_BACILLUS, "BLIP"); dish_ui_init(&ui); run(&dish, 20, 10, false); } while (0)
    FRESH(71); int s = dish_visit(&dish, SP_MOLD); run(&dish, 40, 0, false);
    float mx = 0; for (int c = 0; c < FIELD_N * FIELD_N; c++) if (dish.drug[c] > mx) mx = dish.drug[c];
    CHECK(s > 0 && mx > 0.2f, "the mold leaked no antibiotic (max %.2f)", mx);
    FRESH(72); dish_visit(&dish, SP_LACTO); run(&dish, 120, 10, false);
    CHECK(dish.ph < 0.45f, "the acid maker left the pH at %.2f", dish.ph);
    FRESH(73); dish_visit(&dish, SP_HALO); run(&dish, 120, 10, false);
    CHECK(dish.salt > 0.26f, "the halophile left the salt at %.2f", dish.salt);
    {   FRESH(74); float f0 = 0, f1 = 0;
        static dish_t ctrl; ctrl = dish; dish_visit(&dish, SP_SYMBIONT);
        run(&dish, 60, 0, false); for (int c = 0; c < FIELD_N * FIELD_N; c++) f1 += dish.nutr[c];
        dish = ctrl; run(&dish, 60, 0, false); for (int c = 0; c < FIELD_N * FIELD_N; c++) f0 += dish.nutr[c];
        CHECK(f1 > f0 + 1, "the symbiont added no food (%.1f vs %.1f without it)", f1, f0); }
    {   FRESH(75); int cs = dish_visit(&dish, SP_CILIATE); dish_outbreak(&dish, 1);
        int gi = -1, pi = -1;
        for (int i = 0; i < DISH_MAX_CELLS; i++) { if (dish.cell[i].strain == cs && gi < 0) gi = i; if (dish.cell[i].species == SP_PHAGE && dish.cell[i].state == CS_LIVE) pi = i; }
        CHECK(gi >= 0 && pi >= 0, "no guardian or no phage to test with");
        if (gi >= 0 && pi >= 0) { dish.cell[pi].x = dish.cell[gi].x + 3; dish.cell[pi].y = dish.cell[gi].y; run(&dish, 0.2f, 0, false);
            CHECK(dish.cell[pi].state != CS_LIVE, "the guardian did not eat a virus touching it"); } }
    {   FRESH(76); auto_pick = false; int ds = dish_visit(&dish, SP_DONOR); int di = -1, pi = -1;
        for (int i = 0; i < DISH_MAX_CELLS; i++) { if (dish.cell[i].strain == ds && di < 0) di = i; if (dish.cell[i].strain == DISH_PET && dish.cell[i].state == CS_LIVE) pi = i; }
        int g0 = ev_count[DEV_GIFT];
        if (di >= 0 && pi >= 0) { dish.cell[di].x = dish.cell[pi].x + 4; dish.cell[di].y = dish.cell[pi].y; run(&dish, 0.2f, 0, false); }
        CHECK(dish.drafts == 1 && ev_count[DEV_GIFT] == g0 + 1 && ui.page == PAGE_DRAFT, "touching the donor gave no free card");
        tap_hs(HS_DRAFT_0); auto_pick = true; }
    {   FRESH(77); unsigned dead0 = dish.strain[DISH_PET].deaths; dish_visit(&dish, SP_VIBRIO); run(&dish, 90, 10, false);
        CHECK(dish.strain[DISH_PET].deaths > dead0, "the vibrio hunted nothing"); }
    /* a visitor whose time is up leaves, frees its slot, and the story says so */
    {   FRESH(78); int vs = dish_visit(&dish, SP_HALO); int gone0 = ev_count[DEV_VISIT_GONE];
        dish.strain[vs].stay_t = 0; run(&dish, 45, 10, false);
        CHECK(!dish.strain[vs].used && ev_count[DEV_VISIT_GONE] == gone0 + 1, "a visitor past its time did not leave (%d cells)", dish.strain[vs].count);
        char l[96]; int mn; bool said = false;
        for (int i = 0; i < dish_story_count(&dish) && dish_story_line(&dish, i, l, sizeof l, &mn); i++) if (strstr(l, "HALOPHILE") || strstr(l, "HALO")) said = true;
        CHECK(said, "the halophile's visit is not in the story"); }
    /* the director brings visitors, and every run lives to see its first draft */
    int ended_early = 0, visits = 0, drafts = 0, overs = 0;
    for (uint32_t seed = 1; seed <= 30; seed++) {
        dish_init(&dish, seed * 977, (species_t)(seed % 3), "BLIP"); dish_ui_init(&ui);
        int v0 = ev_count[DEV_VISIT] + ev_count[DEV_OUTBREAK], d0 = ev_count[DEV_DRAFT];
        lazy_run(&dish, 20 * 60);
        visits += ev_count[DEV_VISIT] + ev_count[DEV_OUTBREAK] - v0; drafts += ev_count[DEV_DRAFT] - d0; overs += dish.run_over;
        if (dish.run_over && dish.run_muts == 0) ended_early++;
    }
    printf("30 lazy 20-minute runs: %d visitors, %d drafts, %d runs over, %d over before a first card\n", visits, drafts, overs, ended_early);
    CHECK(visits >= 30 * 3, "too few visitors: %d in 30 runs of 20 minutes", visits);
    CHECK(ended_early == 0, "%d runs ended before their first mutation", ended_early);
    printf("selftest-visitors: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

/* ---- the verbs: each mutation does its thing, measured against the same dish without it ---- */
static int forced_goal = -1;
static strat_t forced_brain(const dish_t *d, int s) {
    strat_t r = dish_brain_rules(d, s);
    if (s != DISH_PET || forced_goal < 0) return r;
    int t = -1;
    for (int q = 0; q < 3 && t < 0; q++) { int o = d->strain[s].other[q]; if (o >= 0 && d->strain[o].used && d->strain[o].species == SP_VIBRIO) t = o; }
    return (strat_t){ (uint8_t)forced_goal, (int8_t)(ST_TARGETED(forced_goal) ? t : -1), 6, 1 };
}
typedef struct { int stunned, tethers, jets, anchors, scouts, bloomed, tamed, burst; float pet_e, pet_h, vib_h, align, vib_dist, pet_sum; int vib_dead, pet_dead; } rig_t;
/* a bacillus colony with a vibrio pack beside it and phages about: `seconds` with mutation m at tier 3 (-1 none) */
static rig_t verb_rig(int m, int goal, uint32_t seed, float seconds, int extra_cells, bool starve) {
    rig_t r; memset(&r, 0, sizeof r);
    dish_init(&dish, seed, SP_BACILLUS, "BLIP"); dish_ui_init(&ui);
    genome_t g = dish_species_genome(SP_BACILLUS);
    for (int k = 0; k < extra_cells; k++) dish_spawn(&dish, DISH_PET, DISH_CX + dish_rand(&dish, -40, 40), DISH_CY + dish_rand(&dish, -40, 40), &g);
    if (m >= 0) dish.strain[DISH_PET].mut[m] = 3;
    dish.run_muts = 1;                                 /* (so the director sends no visitors of its own) */
    dish.visit_t = 1e9f; dish.outbreak_t = 1e9f;
    int vs = dish_visit(&dish, SP_VIBRIO);
    for (int i = 0; i < DISH_MAX_CELLS; i++)           /* the pack right next to the colony */
        if (dish.cell[i].strain == vs && dish.cell[i].state == CS_LIVE) { dish.cell[i].x = DISH_CX + 45 + dish_rand(&dish, -10, 10); dish.cell[i].y = DISH_CY + dish_rand(&dish, -10, 10); }
    if (m == M_TAMER) { dish_outbreak(&dish, 1); for (int s = 0; s < DISH_MAX_STRAINS; s++) if (dish.strain[s].used && dish.strain[s].species == SP_PHAGE)
        for (int k = 0; k < 16; k++) dish_spawn(&dish, s, DISH_CX + dish_rand(&dish, -30, 30), DISH_CY + dish_rand(&dish, -30, 30), NULL); }
    if (starve) memset(dish.nutr, 0, sizeof dish.nutr);
    dish.strain[vs].stay_t = 1e9f;
    forced_goal = goal; brain = forced_brain;
    unsigned vd0 = 0;
    for (float t = 0; t < seconds; t += 1 / 30.0f) {
        if (dish_ui_paused(&ui)) tap_hs(HS_DRAFT_0);
        if (starve) dish_light(&dish, dish.strain[DISH_PET].cx, dish.strain[DISH_PET].cy, 1 / 30.0f);
        dish_tick(&dish, 1 / 30.0f, brain); dish_ui_tick(&ui, &dish, 1 / 30.0f);
        if (starve) memset(dish.nutr, 0, sizeof dish.nutr);   /* (the light's bloom aside: only the light itself feeds) */
        for (int i = 0; i < DISH_MAX_CELLS; i++) {
            const cell_t *c = &dish.cell[i];
            if (c->state == CS_FREE) continue;
            r.stunned += c->stun_t > 0 && c->strain != DISH_PET;
            r.tethers += c->tether != 0;
            r.jets += c->jet_t > 0;
            r.anchors += c->strain == DISH_PET && dish_cell_anchor(&dish, c);
            r.scouts += c->strain == DISH_PET && dish_cell_scout(&dish, c);
            r.tamed += c->species == SP_PHAGE && c->strain == DISH_PET;
            r.burst += c->strain == DISH_PET && c->state == CS_SPORE && c->hatch_t > 0;
        }
        r.bloomed += dish.strain[DISH_PET].bloom_t < 0;
        if (m == M_SPOREBURST && t < 0.05f)            /* a few cells about to die of the dish */
            for (int i = 0, k = 0; i < DISH_MAX_CELLS && k < 4; i++) if (dish.cell[i].strain == DISH_PET && dish.cell[i].state == CS_LIVE) { dish.cell[i].health = 0.0001f; dish.cell[i].hurt = DIE_HARSH; dish.ph = 0.0f; k++; }
        if (m == M_SPOREBURST && t > 0.5f) dish.ph = 0.5f;
    }
    brain = dish_brain_rules; forced_goal = -1;
    const strain_t *pet = &dish.strain[DISH_PET], *vib = &dish.strain[vs];
    r.pet_e = pet->energy; r.pet_h = pet->health; r.vib_h = vib->count ? vib->health : 0; r.vib_dead = (int)(vib->deaths - vd0); r.pet_dead = (int)pet->deaths;
    float ax = 0, ay = 0; int n = 0; float dsum = 0; int dn = 0;
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        const cell_t *c = &dish.cell[i];
        if (c->state != CS_LIVE) continue;
        if (c->strain == DISH_PET) { ax += cosf(c->ang); ay += sinf(c->ang); n++; r.pet_sum += c->energy; }
        if (c->strain == vs) { dsum += sqrtf((c->x - pet->cx) * (c->x - pet->cx) + (c->y - pet->cy) * (c->y - pet->cy)); dn++; }
    }
    r.align = n ? sqrtf(ax * ax + ay * ay) / n : 0;
    r.vib_dist = dn ? dsum / dn : 0;
    return r;
}
static int selftest_verbs(void) {
    dish_events_set(count_events, NULL);
    rig_t c0 = verb_rig(-1, ST_REST, 81, 12, 12, false);
    #define VERB(M, GOAL, SECS, EXTRA, STARVE, COND, ...) do { rig_t r = verb_rig(M, GOAL, 81, SECS, EXTRA, STARVE); rig_t c = (EXTRA != 12 || STARVE || GOAL != ST_REST || SECS != 12) ? verb_rig(-1, GOAL, 81, SECS, EXTRA, STARVE) : c0; (void)c; \
        CHECK(COND, "%s: " __VA_ARGS__); } while (0)
    VERB(M_SPIKES, ST_REST, 12, 12, false, r.vib_h < c.vib_h - 0.05f || r.vib_dead > c.vib_dead, "attackers were not hurt (health %.2f vs %.2f)", MUTS[M_SPIKES].name, r.vib_h, c.vib_h);
    VERB(M_TOXIN, ST_REST, 12, 12, false, r.vib_h < c.vib_h - 0.05f || r.vib_dead > c.vib_dead, "the haze did not sicken the vibrio (%.2f vs %.2f)", MUTS[M_TOXIN].name, r.vib_h, c.vib_h);
    VERB(M_ZAP, ST_REST, 12, 12, false, r.stunned > 0 && c.stunned == 0, "stunned nobody (%d)", MUTS[M_ZAP].name, r.stunned);
    VERB(M_JET, ST_FLEE, 6, 12, false, r.jets > 0 && c.jets == 0, "no cell jetted away (%d)", MUTS[M_JET].name, r.jets);
    VERB(M_ENGULF, ST_HUNT, 12, 12, false, r.vib_dead > c.vib_dead, "swallowed nothing (%d vs %d vibrio lost)", MUTS[M_ENGULF].name, r.vib_dead, c.vib_dead);
    VERB(M_SWARM, ST_FORAGE, 8, 12, false, r.align > c.align + 0.15f, "the colony did not school (alignment %.2f vs %.2f)", MUTS[M_SWARM].name, r.align, c.align);
    VERB(M_BIOFILM, ST_REST, 2, 12, false, r.anchors > 0 && c.anchors == 0, "built no wall", MUTS[M_BIOFILM].name);
    VERB(M_PHOTO, ST_REST, 12, 12, true, r.pet_sum > c.pet_sum + 1, "lived no better on light (energy %.1f vs %.1f in all)", MUTS[M_PHOTO].name, r.pet_sum, c.pet_sum);
    VERB(M_LURE, ST_REST, 12, 12, false, r.vib_dist < c.vib_dist - 3, "drew nothing in (vibrio %.0f px away vs %.0f)", MUTS[M_LURE].name, r.vib_dist, c.vib_dist);
    VERB(M_SPOREBURST, ST_REST, 2, 12, false, r.burst > 0 && c.burst == 0, "a dying cell left no spore", MUTS[M_SPOREBURST].name);
    VERB(M_TAMER, ST_REST, 12, 12, false, r.tamed > 0 && c.tamed == 0, "no virus joined", MUTS[M_TAMER].name);
    VERB(M_MIMIC, ST_REST, 12, 12, false, r.pet_dead <= c.pet_dead && r.pet_h >= c.pet_h - 0.02f, "the hunters were not fooled (%d lost vs %d)", MUTS[M_MIMIC].name, r.pet_dead, c.pet_dead);
    VERB(M_SCOUTS, ST_FORAGE, 2, 12, false, r.scouts > 0 && c.scouts == 0, "sent out no scouts", MUTS[M_SCOUTS].name);
    VERB(M_GRAPPLE, ST_HUNT, 12, 12, false, r.tethers > 0 && c.tethers == 0, "latched onto nothing", MUTS[M_GRAPPLE].name);
    VERB(M_QUORUM, ST_REST, 4, 40, false, r.bloomed > 0 && c.bloomed == 0, "a big colony never bloomed", MUTS[M_QUORUM].name);
    {   /* symbiosis: a friend stays past its time */
        dish_init(&dish, 82, SP_BACILLUS, "BLIP"); dish_ui_init(&ui); dish.run_muts = 1; dish.visit_t = 1e9f;
        dish.strain[DISH_PET].mut[M_SYMBIOSIS] = 1;
        int fs = dish_visit(&dish, SP_SYMBIONT); dish.strain[fs].stay_t = 5;
        run(&dish, 30, 10, false);
        CHECK(dish.strain[fs].used && dish.strain[fs].count > 0, "%s: the friend left anyway", MUTS[M_SYMBIOSIS].name); }
    printf("selftest-verbs: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

/* ---- the minds: every goal becomes movement; visitors decide too; the story writes it up ---- */
static float pet_to(int s) { const strain_t *a = &dish.strain[DISH_PET], *b = &dish.strain[s]; return sqrtf((a->cx - b->cx) * (a->cx - b->cx) + (a->cy - b->cy) * (a->cy - b->cy)); }
static int selftest_mind(void) {
    dish_events_set(count_events, NULL);
    /* flee, aimed at the vibrio pack: the colony gets away from where the pack came in (the pack chases, so not from it) */
    {   float px = DISH_CX + 45, py = DISH_CY;
        verb_rig(-1, ST_FLEE, 91, 6, 12, false); float far_flee = hypotf(dish.strain[DISH_PET].cx - px, dish.strain[DISH_PET].cy - py);
        verb_rig(-1, ST_REST, 91, 6, 12, false); float far_rest = hypotf(dish.strain[DISH_PET].cx - px, dish.strain[DISH_PET].cy - py);
        CHECK(far_flee > far_rest + 15, "fleeing the vibrio took the colony %.0f px from where it came in (resting: %.0f)", far_flee, far_rest); }
    /* light: seek it and the colony comes; avoid it and it goes */
    for (int k = 0; k < 2; k++) {
        dish_init(&dish, 92, SP_BACILLUS, "BLIP"); dish_ui_init(&ui); dish.run_muts = 1; dish.visit_t = dish.outbreak_t = 1e9f;
        run(&dish, 1, 0, false);
        float bx = dish.strain[DISH_PET].cx + (dish.strain[DISH_PET].cx < DISH_CX ? 1 : -1) * (k == 0 ? 110 : -45), by = dish.strain[DISH_PET].cy;
        float d0 = fabsf(dish.strain[DISH_PET].cx - bx);
        forced_goal = k == 0 ? ST_SEEK_LIGHT : ST_AVOID_LIGHT; brain = forced_brain;
        for (float t = 0; t < 8; t += 1 / 30.0f) { dish_light(&dish, bx, by, 1 / 30.0f); dish_tick(&dish, 1 / 30.0f, brain); }
        brain = dish_brain_rules; forced_goal = -1;
        float d1 = fabsf(dish.strain[DISH_PET].cx - bx);
        if (k == 0) CHECK(d1 < d0 - 20, "seeking the light, the colony went from %.0f to %.0f px of it", d0, d1);
        else CHECK(d1 > d0 + 10, "avoiding the light, the colony went from %.0f to %.0f px of it", d0, d1);
    }
    /* visitors have minds of their own: a vibrio sets out after the pet, a symbiont befriends it, a ciliate guards it */
    dish_init(&dish, 93, SP_BACILLUS, "BLIP"); dish_ui_init(&ui); dish.run_muts = 1; dish.visit_t = dish.outbreak_t = 1e9f;
    run(&dish, 10, 10, false);
    int vs = dish_visit(&dish, SP_VIBRIO);
    bool vib_hunts = false, friend_kind = false, guard = false;
    for (float t = 0; t < 20 && !vib_hunts; t += 1) {   /* the pet the only prey: the vibrio comes for it */
        run(&dish, 1, 0, false);
        vib_hunts |= dish.strain[vs].strat.id == ST_HUNT && dish.strain[vs].strat.target == DISH_PET;
    }
    int fs = dish_visit(&dish, SP_SYMBIONT), gs = dish_visit(&dish, SP_CILIATE);
    float fd0 = pet_to(fs);
    for (float t = 0; t < 45; t += 1) {
        run(&dish, 1, 0, false);
        friend_kind |= dish.strain[fs].strat.id == ST_BEFRIEND && dish.strain[fs].strat.target == DISH_PET;
        guard |= dish.strain[gs].count && ((dish.strain[gs].strat.id == ST_GUARD && dish.strain[gs].strat.target == DISH_PET) || (dish.strain[gs].strat.id == ST_HUNT && dish.strain[gs].strat.target == vs));
    }
    CHECK(vib_hunts, "the vibrio never set out after the pet");
    CHECK(friend_kind && pet_to(fs) < fd0, "the symbiont did not come to befriend the pet (%.0f -> %.0f px)", fd0, pet_to(fs));
    CHECK(guard, "the ciliate neither guarded the pet nor went for the vibrio");
    /* the pet decides about them: a strain in its others list, a goal at it */
    {   bool pet_target = false;
        for (int q = 0; q < 3; q++) pet_target |= dish.strain[DISH_PET].other[q] >= 0;
        CHECK(pet_target, "the pet's mind sees no other strain with three visitors in the dish"); }
    /* the story: interactions, visits and samples are written up, and every line reads */
    run(&dish, 20, 10, false);                       /* (past the first minute: a sample) */
    {   int lines = dish_story_count(&dish), decide = 0; char l[96]; int mn;
        for (int i = 0; i < lines && dish_story_line(&dish, i, l, sizeof l, &mn); i++) { if (strstr(l, "SET OUT TO") || strstr(l, "CHOSE TO") || strstr(l, "TURNED TO")) decide++; CHECK(!strstr(l, "SOMETHING HAPPENED"), "a story line has no words: \"%s\"", l); }
        CHECK(decide > 0, "%d story lines and none about who decided what about whom", lines);
        CHECK(dish_story_samples(&dish) >= 1, "no minute samples in the story"); }
    {   /* every event type reads as a sentence */
        static dish_t s; dish_init(&s, 5, SP_COCCUS, "DOT"); s.log_n = 0; s.log_head = 0;
        for (int t = SE_START; t <= SE_LOSS; t++) dish_story_note(&s, t, SP_VIBRIO, SP_COCCUS, t == SE_MUT ? M_ZAP : t == SE_DECIDE ? ST_HUNT : 1, 3);
        char l[96]; int mn, ok = 0;
        for (int i = 0; i < dish_story_count(&s); i++) if (dish_story_line(&s, i, l, sizeof l, &mn) && strlen(l) > 8 && !strstr(l, "SOMETHING")) ok++;
        CHECK(ok == SE_LOSS - SE_START + 1, "only %d of %d event types read as a sentence", ok, SE_LOSS - SE_START + 1); }
    /* the state line: what the AI reads (dish schema v1), every word in the lexicon */
    {   char line[512]; dish_encode(&dish, DISH_PET, line, sizeof line);
        const char *lex[512]; int nl = dish_lexicon(lex, 512), bad = 0, words = 0;
        for (char *w = strtok(line, " "); w; w = strtok(NULL, " ")) {
            bool found = false; for (int k = 0; k < nl && !found; k++) found = !strcmp(lex[k], w);
            if (!found) { printf("  not in the lexicon: \"%s\"\n", w); bad++; }
            words++;
        }
        CHECK(bad == 0 && words == 49, "the state line has %d words (%d not in the lexicon): want 49, all known", words, bad); }
    printf("selftest-mind: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

/* the sampler: random dishes, and the exact line each strain's mind reads (the teacher labels these) */
static int dump_states(int n, uint32_t seed) {
    static const species_t VIS[] = { SP_CYANO, SP_SYMBIONT, SP_CILIATE, SP_DONOR, SP_COCCUS, SP_PHAGE, SP_MYXO, SP_MOLD, SP_VIBRIO, SP_LACTO, SP_HALO };
    dish_ui_init(&ui);
    int printed = 0;
    for (uint32_t k = 0; printed < n; k++) {
        dish_init(&dish, seed * 7919u + k * 104729u + 1, (species_t)((seed + k) % 5 == 4 ? SP_MYXO : (seed + k) % 5 == 3 ? SP_VIBRIO : (seed + k) % 3), "BLIP");
        dish.run_muts = 1; dish.visit_t = dish.outbreak_t = 1e9f;
        int picks = (int)dish_rand(&dish, 0, 7);
        for (int q = 0; q < picks; q++) dish_mut_take(&dish, DISH_PET, (int)dish_rand(&dish, 0, M_COUNT - 0.01f));
        int nv = (int)dish_rand(&dish, 0, 3.99f);
        for (int q = 0; q < nv; q++) { species_t v = VIS[(int)dish_rand(&dish, 0, 10.99f)]; if (v == SP_PHAGE) dish_outbreak(&dish, 3); else dish_visit(&dish, v); }
        if (dish_rand(&dish, 0, 1) < 0.4f)            /* (often right beside the pet: the interactions are what the mind is for) */
            for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].state == CS_LIVE && dish.cell[i].strain != DISH_PET) {
                dish.cell[i].x = DISH_CX + dish_rand(&dish, -70, 70); dish.cell[i].y = DISH_CY + dish_rand(&dish, -70, 70); }
        if (dish_rand(&dish, 0, 1) < 0.25f)           /* (and sometimes a pet already in trouble) */
            for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET) dish.cell[i].health = dish_rand(&dish, 0.1f, 0.6f);
        dish.light = dish_rand(&dish, 0, 1) < 0.7f;
        if (dish_rand(&dish, 0, 1) < 0.3f) memset(dish.nutr, 0, sizeof dish.nutr);
        float secs = dish_rand(&dish, 5, 40), bx = dish_rand(&dish, 40, DISH_W - 40), by = dish_rand(&dish, 40, DISH_H - 40);
        bool beam = dish_rand(&dish, 0, 1) < 0.35f;
        for (float t = 0; t < secs; t += 1 / 15.0f) { if (beam) dish_light(&dish, bx, by, 1 / 15.0f); dish_tick(&dish, 1 / 15.0f, dish_brain_rules); }
        for (int s = 0; s < DISH_MAX_STRAINS && printed < n; s++) {
            if (!dish.strain[s].used || !dish.strain[s].count) continue;
            char line[512]; dish_fill_others(&dish, s); dish_encode(&dish, s, line, sizeof line);
            printf("%s\n", line); printed++;
        }
    }
    return 0;
}

/* the AI: the whole decode path on the test model (random weights, the real vocabulary), and the scheduler */
static int selftest_ai(void) {
    dish_events_set(count_events, NULL);
    dish_init(&dish, 101, SP_BACILLUS, "BLIP"); dish_ui_init(&ui);
    CHECK(!dish_ai_ready(), "a mind is ready with no model");
    {   strat_t a = dish_ai_brain(&dish, DISH_PET), b = dish_brain_rules(&dish, DISH_PET);
        CHECK(a.id == b.id && a.target == b.target, "with no model the AI did not answer with the rules"); }
    size_t tl, ml = dish_ai_test_model_size(&tl);
    uint8_t *mb = malloc(ml), *tb = malloc(tl);
    CHECK(mb && tb && dish_ai_test_model(mb, ml, tb, tl, 7), "could not build the test model");
    CHECK(dish_ai_open(mb, ml, tb, tl, malloc) && dish_ai_ready(), "the test model did not open (%zu bytes)", ml);
    dish_ai_clock_us = now_us;
    /* the decode: always a valid decision */
    dish.run_muts = 1; dish.visit_t = dish.outbreak_t = 1e9f;
    dish_visit(&dish, SP_VIBRIO); dish_visit(&dish, SP_SYMBIONT); dish_visit(&dish, SP_CILIATE);
    run(&dish, 5, 0, false);
    for (int s = 0; s < DISH_MAX_STRAINS; s++) {
        if (!dish.strain[s].used || !dish.strain[s].count) continue;
        char line[512]; dish_fill_others(&dish, s); dish_encode(&dish, s, line, sizeof line);
        strat_t r; bool ok = dish_ai_decide(line, dish.strain[s].other, &r);
        bool in_others = r.target < 0 || r.target == dish.strain[s].other[0] || r.target == dish.strain[s].other[1] || r.target == dish.strain[s].other[2];
        CHECK(ok && r.id < ST_COUNT && r.urgency >= 0 && r.urgency <= 9 && r.confidence > 0 && r.confidence <= 1 && in_others && (ST_TARGETED(r.id) || r.target < 0),
              "strain %d: a bad decision (ok %d, goal %d, target %d, urgency %.0f, sure %.2f)", s, ok, r.id, r.target, r.urgency, r.confidence);
    }
    /* the scheduler: the dish asks through the AI, one thought a frame, and every strain gets thought about */
    brain = dish_ai_brain;
    for (float t = 0; t < 40; t += 1 / 30.0f) { dish_tick(&dish, 1 / 30.0f, brain); dish_ui_tick(&ui, &dish, 1 / 30.0f); dish_ai_step(); }
    brain = dish_brain_rules;
    dish_ai_stats_t st = dish_ai_stats();
    printf("ai: asked %u, answered %u, used %u, fell back %u, %.2f ms a decision (the test model)\n", (unsigned)st.asked, (unsigned)st.answered, (unsigned)st.used, (unsigned)st.fallback, st.ms_mean);
    CHECK(st.answered > 0 && st.used > st.asked / 2, "the model's answers were seldom used (%u of %u)", (unsigned)st.used, (unsigned)st.asked);
    for (int s = 0; s < DISH_MAX_STRAINS; s++)
        if (dish.strain[s].used && dish.strain[s].count) CHECK(dish_ai_answers(s) > 0, "strain %d (%s) was never thought about", s, SPECIES_NAMES[dish.strain[s].species]);
    printf("selftest-ai: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

/* a file into memory (the model, its tokenizer) */
static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = len > 0 ? malloc((size_t)len) : NULL;
    *n = b ? fread(b, 1, (size_t)len, f) : 0; fclose(f);
    return b;
}
static bool load_ai(const char *model, const char *tok) {
    size_t ml, tl; uint8_t *m = slurp(model, &ml), *t = slurp(tok, &tl);
    bool ok = m && t && dish_ai_open(m, ml, t, tl, malloc);
    printf("ai: %s (%s, %s)\n", ok ? "the model thinks" : "could not open the model: the rules think", model, tok);
    if (ok) { dish_ai_clock_us = now_us; brain = dish_ai_brain; }
    return ok;
}
/* the student against the teacher: held-out traces, the board's own decode */
static int ai_agree(const char *model, const char *tok, const char *traces) {
    if (!load_ai(model, tok)) return 1;
    FILE *f = fopen(traces, "r"); if (!f) { perror(traces); return 1; }
    static char ln[2048]; int n = 0, goal_ok = 0, tgt_n = 0, tgt_ok = 0;
    int conf[ST_COUNT][ST_COUNT]; memset(conf, 0, sizeof conf);
    while (fgets(ln, sizeof ln, f)) {
        char *st = strstr(ln, "\"state\": \""), *gl = strstr(ln, "\"goal\": \"");
        if (!st || !gl) continue;
        st += 10; gl += 9;
        char *se = strchr(st, '"'), *ge = strchr(gl, '"'); if (!se || !ge) continue;
        *se = 0; *ge = 0;
        char tg[16], tt[16]; int tu; if (sscanf(gl, "%15s %15s urgency %d", tg, tt, &tu) != 3) continue;
        int want = -1; for (int g = 0; g < ST_COUNT; g++) if (!strcmp(STRAT_NAMES[g], tg)) want = g;
        if (want < 0) continue;
        const int8_t other[3] = { 0, 1, 2 }; strat_t r;
        if (!dish_ai_decide(st, other, &r)) continue;
        n++; goal_ok += r.id == want; conf[want][r.id]++;
        if (ST_TARGETED(want) && r.id == want) { char got[8]; snprintf(got, sizeof got, r.target < 0 ? "none" : "other%d", r.target + 1); tgt_n++; tgt_ok += !strcmp(got, tt); }
    }
    fclose(f);
    printf("agreement on %d traces: goal %.1f%%, target (when the goal agrees and has one) %.1f%%, %.1f ms a decision\n",
           n, 100.0 * goal_ok / (n ? n : 1), 100.0 * tgt_ok / (tgt_n ? tgt_n : 1), dish_ai_stats().ms_mean);
    printf("teacher -> student (rows: the teacher's goal)\n");
    for (int a = 0; a < ST_COUNT; a++) {
        int row = 0; for (int b = 0; b < ST_COUNT; b++) row += conf[a][b];
        if (!row) continue;
        printf("  %-12s %5d:", STRAT_NAMES[a], row);
        for (int b = 0; b < ST_COUNT; b++) if (conf[a][b]) printf(" %s %d", STRAT_NAMES[b], conf[a][b]);
        printf("\n");
    }
    return 0;
}

int main(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--selftest-dish")) return selftest_dish();
        if (!strcmp(argv[a], "--selftest-pet")) return selftest_pet();
        if (!strcmp(argv[a], "--selftest-evolve")) return selftest_evolve();
        if (!strcmp(argv[a], "--selftest-prog")) return selftest_prog();
        if (!strcmp(argv[a], "--selftest-render")) return selftest_render();
        if (!strcmp(argv[a], "--selftest-run")) return selftest_run();
        if (!strcmp(argv[a], "--selftest-visitors")) return selftest_visitors();
        if (!strcmp(argv[a], "--selftest-verbs")) return selftest_verbs();
        if (!strcmp(argv[a], "--selftest-mind")) return selftest_mind();
        if (!strcmp(argv[a], "--selftest-ai")) return selftest_ai();
        if (!strcmp(argv[a], "--test-model") && a + 2 < argc) {   /* the selftests' random model, as files (to try --ai, --ai-agree) */
            size_t tl, ml = dish_ai_test_model_size(&tl); uint8_t *m = malloc(ml), *t = malloc(tl);
            dish_ai_test_model(m, ml, t, tl, 7);
            FILE *f = fopen(argv[a + 1], "wb"); if (f) { fwrite(m, 1, ml, f); fclose(f); }
            f = fopen(argv[a + 2], "wb"); if (f) { fwrite(t, 1, tl, f); fclose(f); }
            printf("wrote the test model (%zu bytes) and its tokenizer (%zu bytes)\n", ml, tl);
            return 0;
        }
        if (!strcmp(argv[a], "--ai-agree") && a + 3 < argc) return ai_agree(argv[a + 1], argv[a + 2], argv[a + 3]);
        if (!strcmp(argv[a], "--ai") && a + 2 < argc) { load_ai(argv[a + 1], argv[a + 2]); a += 2; continue; }   /* then --narrate, or the window */
        if (!strcmp(argv[a], "--dump-states") && a + 1 < argc) return dump_states(atoi(argv[a + 1]), a + 2 < argc ? (uint32_t)atoi(argv[a + 2]) : 1);
        if (!strcmp(argv[a], "--dish-lexicon")) { const char *w[512]; int n = dish_lexicon(w, 512); for (int k = 0; k < n; k++) printf("%s\n", w[k]); return 0; }
        if (!strcmp(argv[a], "--bench")) {          /* render cost on a busy dish: the number to compare with the device */
            dish_render_set_scene(scene); dish_render_set_background(bg);
            dish_init(&dish, 3, SP_BACILLUS, "BLIP"); dish_ui_init(&ui);
            run(&dish, 400, 12, false);
            dish_outbreak(&dish, 10); run(&dish, 5, 0, false);
            clock_t t0 = clock();
            for (int k = 0; k < 200; k++) { dish_tick(&dish, 1 / 30.0f, dish_brain_rules); }
            clock_t t1 = clock();
            int64_t ui_us = 0;
            dish_render_clock_us = now_us; memset(dish_render_prof_us, 0, sizeof dish_render_prof_us);
            int64_t band_us[2] = { 0, 0 };
            for (int k = 0; k < 200; k++) {
                dish.clock += 1 / 30.0f;                 /* (time moves, so the background refreshes at its real rate) */
                int64_t b0 = now_us(); dish_render_band(&dish, fb, DISH_W, 0, DISH_H / 2);
                int64_t b1 = now_us(); dish_render_band(&dish, fb, DISH_W, DISH_H / 2, DISH_H);
                band_us[0] += b1 - b0; band_us[1] += now_us() - b1;
                int64_t u0 = now_us(); dish_ui_render(&ui, &dish, fb, DISH_W); ui_us += now_us() - u0;
            }
            clock_t t2 = clock();
            printf("bench: %d cells, tick %.2f ms, render %.2f ms per frame\n", dish.n_live,
                   (t1 - t0) * 1000.0 / CLOCKS_PER_SEC / 200, (t2 - t1) * 1000.0 / CLOCKS_PER_SEC / 200);
            printf("       scene %.3f | fields+motes %.3f | cells %.3f | particles %.3f | ui %.3f ms\n",
                   dish_render_prof_us[0] / 200e3, dish_render_prof_us[1] / 200e3, dish_render_prof_us[2] / 200e3,
                   dish_render_prof_us[3] / 200e3, ui_us / 200e3);
            printf("       bands: top %.3f ms, bottom %.3f ms (on two cores the frame takes the slower one)\n", band_us[0] / 200e3, band_us[1] / 200e3);
            return 0;
        }
        if (!strcmp(argv[a], "--snapshot") && a + 1 < argc) return snapshot(argv[a + 1]);
        if (!strcmp(argv[a], "--narrate")) {
            float mins = a + 1 < argc ? (float)atof(argv[a + 1]) : 10;
            dish_init(&dish, 1, SP_BACILLUS, "BLIP");
            for (int t = 0; t < (int)(mins * 60); t++) {
                run(&dish, 1, 0, false); while (dish_ai_step()) {}
                const strain_t *pet = &dish.strain[DISH_PET];
                if (t % 45 == 44 && pet->count) dish_feed(&dish, pet->cx, pet->cy, 1.0f);
                if (t % 30 == 0) census_line(&dish);
            }
            return 0;
        }
    }
#ifdef DISH_SDL
    return window(argc > 1 && !strcmp(argv[1], "--fresh"));
#else
    printf("usage: dishsim --selftest-dish | --selftest-pet | --snapshot <prefix> | --narrate [minutes]\n"
           "(no window: this build found no SDL2 - brew install sdl2, then make dish)\n");
    return 0;
#endif
}
