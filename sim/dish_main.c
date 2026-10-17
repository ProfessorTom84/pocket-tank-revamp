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
static void run(dish_t *d, float seconds, float feed_s, bool print) {
    float next_feed = feed_s, next_print = 0;
    for (float t = 0; t < seconds; t += 1 / 30.0f) {
        dish_tick(d, 1 / 30.0f, dish_brain_rules);
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
static void tap_icon(icon_t i) { float x, y; dish_ui_icon_pos(i, &x, &y); tap_at(x, y); }
static int snapshot(const char *pre) {
    dish_render_set_scene(scene);
    dish_events_set(on_event, NULL);
    dish_init(&dish, 3, SP_BACILLUS, "BLIP");
    dish_ui_init(&ui);
    run(&dish, 150, 25, false);
    dish_outbreak(&dish, 6);
    run(&dish, 25, 25, false);
    dish_feed(&dish, DISH_CX + 40, DISH_CY - 30, 1.0f);
    run(&dish, 0.15f, 0, false);
    shot(pre, "dish");
    tap_icon(ICON_ZOOM); run(&dish, 0.1f, 0, false); shot(pre, "lens"); tap_icon(ICON_ZOOM);
    tap_icon(ICON_CARD); shot(pre, "card"); tap_at(DISH_CX, DISH_CY);
    tap_icon(ICON_LAB); shot(pre, "lab"); dish_ui_touch(&ui, &dish, 0, 0, false); ui.page = PAGE_NONE;
    tap_icon(ICON_MEDS); shot(pre, "meds"); ui.page = PAGE_NONE;
    tap_icon(ICON_CLEAN); shot(pre, "tool"); tap_icon(ICON_CLEAN);
    /* a hungry pet: the emote and the pulsing FEED */
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET) dish.cell[i].energy = 0.2f;
    run(&dish, 11.5f, 0, false); shot(pre, "hungry");
    /* extinct: the vault */
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET && dish.cell[i].state != CS_FREE) dish.cell[i].state = CS_DYING;
    run(&dish, 8, 0, false); shot(pre, "extinct");
    /* phase 3: the lab, the rack, the new germs, evolved looks */
    run(&dish, 25, 10, false);
    ui.page = PAGE_NONE; dish.dna = 70;
    tap_icon(ICON_LAB); shot(pre, "lab_locked");
    float lx0, ly0; dish_ui_hotspot(HS_PH_PLUS, &lx0, &ly0); tap_at(lx0, ly0); ui.page = PAGE_NONE;   /* unlock the pH dial with the DNA */
    dish.unlocked = ~0u; ui.toast_t = 0;
    tap_icon(ICON_LAB);
    float hx, hy; dish_ui_hotspot(HS_PH_MINUS, &hx, &hy); tap_at(hx, hy); tap_at(hx, hy);
    dish_ui_hotspot(HS_SALT_PLUS, &hx, &hy); tap_at(hx, hy); tap_at(hx, hy); tap_at(hx, hy);
    shot(pre, "lab3");
    ui.page = PAGE_NONE;
    {   /* visitors: a threat or two and the friends */
        static const species_t V[] = { SP_MOLD, SP_CILIATE, SP_HALO, SP_SYMBIONT, SP_DONOR };
        for (int k = 0; k < 5; k++) dish_visit(&dish, V[k]);
    }
    run(&dish, 90, 10, false);
    /* a build: what drafting does to a strain */
    { static const int B[] = { M_SPIKES, M_SPIKES, M_CILIA, M_CHLORO, M_CHLORO, M_TOXIN, M_GLOW, M_FLAGELLA };
      for (int k = 0; k < 8; k++) dish_mut_take(&dish, DISH_PET, B[k]); }
    dish.drafts = 2; dish.offer[0] = dish.offer[1] = dish.offer[2] = 0xFF;
    ui.toast_t = 0; run(&dish, 0.1f, 0, false); shot(pre, "mutate_chip");
    dish_ui_hotspot(HS_MUTATE, &hx, &hy); tap_at(hx, hy); shot(pre, "draft");
    ui.page = PAGE_NONE;
    /* show off evolution: a warm-raised, salt-shelled, drug-armored pet */
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        cell_t *c = &dish.cell[i];
        if (c->strain != DISH_PET) continue;
        c->g.heat_opt = 0.5f + (i % 3) * 0.12f; c->g.salt_tol = 0.5f + (i % 4) * 0.12f; c->g.drug_res = (i % 2) * 0.7f;
    }
    dish_uv(&dish, 5);
    run(&dish, 0.5f, 0, false); shot(pre, "zoo");
    const strain_t *pet = &dish.strain[DISH_PET];
    int ci = -1; for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET && dish.cell[i].state == CS_LIVE && dish.cell[i].g.drug_res > 0.5f && dish.cell[i].g.salt_tol > 0.7f) { ci = i; break; }
    (void)pet;
    if (ci >= 0) { ui.lens = ci; ui.lens_id = dish.cell[ci].id; shot(pre, "evolved"); }
    ui.lens = -1;
    /* phase 4: the card's buttons, the dex, the badges, the setup */
    dish.badges |= 1u << BADGE_FIRST_SPLIT | 1u << BADGE_GEN10 | 1u << BADGE_COMEBACK | 1u << BADGE_SHELL | 1u << BADGE_ZOO;
    dish.badges_new = 1u << BADGE_ZOO;
    run(&dish, 0.2f, 0, false);
    ui.toast_t = 0; tap_icon(ICON_CARD); shot(pre, "card2");
    dish_ui_hotspot(HS_CARD_DEX, &hx, &hy); tap_at(hx, hy);
    dish_ui_tile(PAGE_DEX, SP_MYXO, &hx, &hy); tap_at(hx, hy); shot(pre, "dex");
    dish_ui_hotspot(HS_DONE, &hx, &hy); tap_at(hx, hy);
    dish_ui_hotspot(HS_CARD_BADGES, &hx, &hy); tap_at(hx, hy);
    dish_ui_tile(PAGE_BADGES, BADGE_SHELL, &hx, &hy); tap_at(hx, hy); shot(pre, "badges");
    ui.page = PAGE_NONE; ui.toast_t = 0;
    dish_ui_begin_setup(&ui);
    shot(pre, "setup1"); dish_ui_hotspot(HS_DONE, &hx, &hy); tap_at(hx, hy);
    shot(pre, "setup2"); dish_ui_hotspot(HS_DONE, &hx, &hy); tap_at(hx, hy);
    shot(pre, "setup3"); dish_ui_hotspot(HS_DONE, &hx, &hy); tap_at(hx, hy);
    shot(pre, "setup4");
    return 0;
}

static int selftest_pet(void) {
    dish_events_set(on_event, NULL);
    dish_init(&dish, 21, SP_BACILLUS, "BLIP");
    dish_ui_init(&ui);
    run(&dish, 20, 0, false);
    /* FEED drops food where the pet is */
    const strain_t *pet = &dish.strain[DISH_PET];
    float before = dish_field(dish.nutr, pet->cx, pet->cy);
    int pitch; while (dish_ui_take_sound(&ui, &pitch) >= 0) {}
    tap_icon(ICON_FEED);
    CHECK(dish_field(dish.nutr, pet->cx, pet->cy) > before + 0.2f, "FEED put no food by the pet");
    CHECK(dish_ui_take_sound(&ui, &pitch) == SND_FEED, "FEED made no feed sound");
    tap_icon(ICON_CARD); run(&dish, 0.05f, 0, false);
    int heard = -1; for (int c; (c = dish_ui_take_sound(&ui, &pitch)) >= 0;) if (c == SND_CARD_OPEN) heard = c;
    CHECK(heard == SND_CARD_OPEN, "the card opened silently");
    ui.page = PAGE_NONE; run(&dish, 0.05f, 0, false);
    while (dish_ui_take_sound(&ui, &pitch) >= 0) {}
    {   /* a slow press on FEED is still a press: it feeds, and warms nothing */
        float fx, fy; dish_ui_icon_pos(ICON_FEED, &fx, &fy);
        float n0 = 0; for (int c = 0; c < FIELD_N * FIELD_N; c++) n0 += dish.nutr[c];
        dish.warm = 0;
        dish_ui_touch(&ui, &dish, fx, fy, true); run(&dish, 1.2f, 0, false); dish_ui_touch(&ui, &dish, fx, fy, false);
        float n1 = 0; for (int c = 0; c < FIELD_N * FIELD_N; c++) n1 += dish.nutr[c];
        CHECK(dish.warm < 0.01f, "holding FEED warmed the agar (%.2f)", dish.warm);
        CHECK(n1 > n0 + 1, "a slow press on FEED fed nothing");
        while (dish_ui_take_sound(&ui, &pitch) >= 0) {}
    }
    /* the pages open and close */
    tap_icon(ICON_CARD); CHECK(ui.page == PAGE_CARD, "CARD did not open the card");
    tap_at(DISH_CX, DISH_CY); CHECK(ui.page == PAGE_NONE, "a tap did not close the card");
    float t0 = dish.temp;
    tap_icon(ICON_LAB); CHECK(ui.page == PAGE_LAB, "LAB did not open the lab");
    float hx, hy; dish_ui_hotspot(HS_LAB_PLUS, &hx, &hy); tap_at(hx, hy);
    CHECK(dish.temp > t0 + 0.04f, "the lab's + did not warm the dish");
    dish_ui_hotspot(HS_LAB_LIGHT, &hx, &hy); tap_at(hx, hy);
    CHECK(!dish.light, "the lab's LIGHT did not turn the light off");
    dish_ui_hotspot(HS_DONE, &hx, &hy); tap_at(hx, hy);
    CHECK(ui.page == PAGE_NONE, "DONE did not close the lab");
    dish.temp = t0; dish.light = true;
    /* the antiviral clears free phages */
    dish_outbreak(&dish, 6);
    run(&dish, 0.5f, 0, false);
    tap_icon(ICON_MEDS); CHECK(ui.page == PAGE_MEDS, "MEDS did not open medicine");
    dish_ui_hotspot(HS_MEDS_VIRUS, &hx, &hy); tap_at(hx, hy);
    CHECK(ui.page == PAGE_NONE, "ANTIVIRAL did not close medicine");
    run(&dish, 1, 0, false);
    int ph = 0; for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].state == CS_LIVE && dish.cell[i].species == SP_PHAGE && !dish.cell[i].host) ph++;
    CHECK(ph == 0, "the antiviral left %d phages", ph);
    /* the microscope opens on a pet cell and closes */
    tap_icon(ICON_ZOOM); CHECK(ui.lens >= 0 && dish.cell[ui.lens].strain == DISH_PET, "ZOOM did not look at the pet");
    tap_icon(ICON_ZOOM); CHECK(ui.lens < 0, "ZOOM again did not put the lens away");
    /* a hungry pet asks: FOOD becomes the worst need */
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET) dish.cell[i].energy = 0.18f;
    run(&dish, 4, 0, false);
    CHECK(ui.needs[NEED_FOOD] < 0.3f, "a starving pet's FOOD need reads %.2f", ui.needs[NEED_FOOD]);
    /* the save round-trips */
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
/* the window: the mouse is the finger. Keys: N light, O an outbreak, F 10x time,
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
        uint32_t now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f; last = now;
        if (dt > 0.1f) dt = 0.1f;
        for (int k = 0; k < (int)speed; k++) { dish_tick(&dish, dt, dish_brain_rules); dish_ui_tick(&ui, &dish, dt); }
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

/* the pet's mean of one gene over its living cells */
static float pet_mean(const dish_t *d, size_t off) {
    float s = 0; int n = 0;
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        const cell_t *c = &d->cell[i];
        if (c->strain != DISH_PET || (c->state != CS_LIVE && c->state != CS_SPORE)) continue;
        s += *(const float *)((const char *)&c->g + off); n++;
    }
    return n ? s / n : *(const float *)((const char *)&d->strain[DISH_PET].vault + off);
}
/* `minutes` of a fed dish; with `dose`, antibiotic on the colony every 30 s and the UV lamp now and then */
static float evolve(uint32_t seed, float minutes, bool dose, unsigned *gens) {
    dish_init(&dish, seed, SP_BACILLUS, "BLIP");
    for (float t = 0; t < minutes * 60; t += 30) {
        const strain_t *pet = &dish.strain[DISH_PET];
        if (dose && pet->count > 0 && t > 60) dish_dose(&dish, pet->cx, pet->cy, 0.3f);   /* sublethal: most live, the weakest don't */
        if (dose && (int)t % 300 == 0) dish_uv(&dish, 10);
        run(&dish, 30, 15, false);
    }
    *gens = dish.strain[DISH_PET].max_gen;
    return pet_mean(&dish, offsetof(genome_t, drug_res));
}
static int selftest_evolve(void) {
    unsigned g0 = 0, g1 = 0, g;
    float base = dish_species_genome(SP_BACILLUS).drug_res, ctrl = 0, treated = 0;
    for (uint32_t seed = 31; seed < 61; seed += 10) {   /* three dishes each way: evolution is chancy, the trend is not */
        ctrl += evolve(seed, 40, false, &g) / 3; g0 += g;
        treated += evolve(seed, 40, true, &g) / 3; g1 += g;
    }
    printf("drug resistance: starts %.3f, untreated %.3f (gen %u), treated %.3f (gen %u)\n", base, ctrl, g0, treated, g1);
    CHECK(treated > ctrl + 0.08f, "antibiotic pressure did not select for resistance (%.3f vs %.3f)", treated, ctrl);
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
    dish.dna = 15; CHECK(!dish_prog_unlock(&dish, UNL_PH), "unlocked the pH dial on 15 DNA");
    dish.dna = 25; CHECK(dish_prog_unlock(&dish, UNL_PH) && dish.dna == 5, "the pH dial did not cost 20");
    CHECK(!dish_prog_unlock(&dish, UNL_PH), "bought the pH dial twice");
    /* a locked dial in the lab: says its price, then unlocks, then works */
    dish.dna = 0; float s0 = dish.salt, hx, hy;
    tap_icon(ICON_LAB); dish_ui_hotspot(HS_SALT_PLUS, &hx, &hy); tap_at(hx, hy);
    CHECK(dish.salt == s0 && !dish_has(&dish, UNL_SALT), "a locked salt dial moved");
    CHECK(strstr(ui.toast, "30 DNA") != NULL, "a locked dial did not say its price (\"%s\")", ui.toast);
    dish.dna = 30; tap_at(hx, hy);
    CHECK(dish_has(&dish, UNL_SALT) && dish.salt == s0 && dish.dna == 0, "the tap with 30 DNA did not just unlock the salt dial");
    tap_at(hx, hy); CHECK(dish.salt > s0, "an unlocked salt dial did not move");
    ui.page = PAGE_NONE; dish.salt = s0;
    /* the starters cost DNA: a locked one can't be picked */
    dish_ui_begin_setup(&ui);
    dish_ui_hotspot(HS_DONE, &hx, &hy); tap_at(hx, hy);                 /* START */
    dish_ui_hotspot(HS_STARTER_5, &hx, &hy); tap_at(hx, hy);            /* the myxo: 100 DNA, and there is none */
    CHECK(ui.starter != 5, "a locked myxo starter was picked");
    ui.setup = 0; ui.page = PAGE_NONE;
    /* care pays when it was needed, not twice in a row */
    run(&dish, 16, 0, false);
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET) dish.cell[i].energy = 0.2f;
    run(&dish, 0.1f, 0, false);
    unsigned dna0 = dish.dna;
    tap_icon(ICON_FEED);
    CHECK(dish.dna == dna0 + 2, "feeding a hungry pet paid %u", dish.dna - dna0);
    tap_icon(ICON_FEED);
    CHECK(dish.dna == dna0 + 2, "a second feed paid again at once");
    /* survivor: an outbreak comes and goes, the pet lives */
    dish_outbreak(&dish, 4); run(&dish, 1, 0, false);
    dish_antiviral(&dish); run(&dish, 2, 0, false);
    CHECK(dish.badges >> BADGE_SURVIVOR & 1, "surviving an outbreak earned no badge");
    /* comeback: extinct, then back from the vault */
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET && dish.cell[i].state != CS_FREE) dish.cell[i].state = CS_DYING;
    run(&dish, 33, 0, false);
    CHECK(dish.badges >> BADGE_COMEBACK & 1, "coming back from the vault earned no badge");
    /* the card's DEX and BADGES */
    tap_icon(ICON_CARD); dish_ui_hotspot(HS_CARD_DEX, &hx, &hy); tap_at(hx, hy);
    CHECK(ui.page == PAGE_DEX, "the card's DEX did not open the dex");
    dish_ui_tile(PAGE_DEX, SP_PHAGE, &hx, &hy); tap_at(hx, hy);
    CHECK(ui.sel == SP_PHAGE, "a dex tile did not select");
    dish_ui_hotspot(HS_DONE, &hx, &hy); tap_at(hx, hy);
    dish_ui_hotspot(HS_CARD_BADGES, &hx, &hy); tap_at(hx, hy);
    CHECK(ui.page == PAGE_BADGES && dish.badges_new, "BADGES did not open, or nothing was new");
    dish_ui_hotspot(HS_DONE, &hx, &hy); tap_at(hx, hy);
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
    dish_ui_hotspot(HS_DONE, &hx, &hy); tap_at(hx, hy);                 /* START */
    dish_ui_hotspot(HS_STARTER_1, &hx, &hy); tap_at(hx, hy);
    dish_ui_hotspot(HS_DONE, &hx, &hy); tap_at(hx, hy);                 /* NEXT */
    dish_ui_hotspot(HS_NAME_NEXT, &hx, &hy); tap_at(hx, hy);
    dish_ui_hotspot(HS_DONE, &hx, &hy); tap_at(hx, hy); tap_at(hx, hy); /* NEXT, BEGIN */
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
    dish.uv_t = 3; dish_feed(&dish, DISH_CX, DISH_CY, 1);   /* everything at once: lamp, drop, ripples */
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
    run(&dish, 1, 0, false);
    CHECK(dish.strain[1].used == false && dish.n_live == dish.strain[DISH_PET].count, "a new run starts with more than the pet");
    /* 1. a draft comes due every 8 generations */
    dish.strain[DISH_PET].max_gen = DISH_DRAFT_GENS; run(&dish, 0.1f, 0, false);
    CHECK(dish.drafts == 1 && dish.next_draft_gen == 2 * DISH_DRAFT_GENS, "the first draft's generation brought %d drafts (next at %u)", dish.drafts, dish.next_draft_gen);
    dish.strain[DISH_PET].max_gen = 40; run(&dish, 0.1f, 0, false);
    CHECK(dish.drafts == 3, "drafts did not stop at three waiting (%d)", dish.drafts);
    /* 2. a deal is three different cards, and taking one applies it */
    dish_draft_deal(&dish);
    CHECK(dish.offer[0] != 0xFF && dish.offer[1] != 0xFF && dish.offer[2] != 0xFF && dish.offer[0] != dish.offer[1] && dish.offer[1] != dish.offer[2] && dish.offer[0] != dish.offer[2],
          "a deal was not three different cards (%d %d %d)", dish.offer[0], dish.offer[1], dish.offer[2]);
    int m = dish.offer[1], r0 = dish.strain[DISH_PET].mut[m];
    CHECK(dish_draft_take(&dish, 1) && dish.strain[DISH_PET].mut[m] == r0 + 1 && dish.drafts == 2 && dish.offer[0] == 0xFF, "taking a card did not apply it");
    float res0 = dish.strain[DISH_PET].vault.drug_res;
    dish_mut_take(&dish, DISH_PET, M_EFFLUX);
    int edited = 0, pets = 0;
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (dish.cell[i].strain == DISH_PET && dish.cell[i].state == CS_LIVE) { pets++; edited += dish.cell[i].g.drug_res > 0.2f; }
    CHECK(dish.strain[DISH_PET].vault.drug_res > res0 + 0.2f && edited == pets, "PUMPS did not edit every cell's genome (%d of %d) and the vault", edited, pets);
    /* 3. the look changes with the build */
    {   genome_t g = dish_species_genome(SP_BACILLUS); uint8_t mu[DISH_MUT_N] = { 0 };
        memset(fb, 0, sizeof fb); dr_cell_icon(fb, DISH_W, SP_BACILLUS, &g, mu, 100, 100, 4, 0, 0, true, 0);
        memset(fb2, 0, sizeof fb2); mu[M_SPIKES] = 2; mu[M_CILIA] = 1; dr_cell_icon(fb2, DISH_W, SP_BACILLUS, &g, mu, 100, 100, 4, 0, 0, true, 0);
        CHECK(lit_diff(fb, fb2) > 200, "spikes and cilia changed only %d pixels of the cell", lit_diff(fb, fb2)); }
    /* 4. the deal answers what the dish is doing: a virus, then heat */
    dish_outbreak(&dish, 6); run(&dish, 0.1f, 0, false);
    int answered = 0;
    for (int k = 0; k < 200; k++) {
        dish.drafts = 1; dish.offer[0] = dish.offer[1] = dish.offer[2] = 0xFF; dish_draft_deal(&dish);
        for (int c = 0; c < 3; c++) if (dish.offer[c] != 0xFF && MUTS[dish.offer[c]].answers >> P_VIRUS & 1) { answered++; break; }
    }
    CHECK(answered >= 198, "a virus in the dish, and only %d of 200 deals answered it", answered);
    dish_antiviral(&dish); run(&dish, 1, 0, false);
    dish.temp = 0.95f; run(&dish, 0.1f, 0, false);
    answered = 0;
    for (int k = 0; k < 200; k++) {
        dish.drafts = 1; dish.offer[0] = dish.offer[1] = dish.offer[2] = 0xFF; dish_draft_deal(&dish);
        for (int c = 0; c < 3; c++) if (dish.offer[c] != 0xFF && MUTS[dish.offer[c]].answers >> P_HEAT & 1) { answered++; break; }
    }
    CHECK(answered >= 198, "a hot dish, and only %d of 200 deals answered the heat", answered);
    dish.temp = 0.5f;
    /* 5. one comeback, then the run is over - and what was earned stays */
    dish.dna = 55; dish.badges |= 1u << BADGE_ZOO; dish.dex |= DEX_SPECIES(SP_MOLD); dish.unlocked |= 1u << UNL_UV;
    uint32_t badges = dish.badges, dex = dish.dex, unl = dish.unlocked;
    int built = dish_mut_total(&dish, DISH_PET);
    CHECK(dish.run_muts > 0, "(the run has taken mutations, so the vault's one comeback counts)");
    kill_pet(); run(&dish, 31.5f, 0, false);
    CHECK(dish.strain[DISH_PET].count > 0 && dish.vault_used && !dish.run_over, "the first extinction did not come back from the vault");
    CHECK(dish_mut_total(&dish, DISH_PET) == built && built > 0, "the vault lost the build (%d of %d)", dish_mut_total(&dish, DISH_PET), built);
    kill_pet(); run(&dish, 2, 0, false);
    CHECK(dish.run_over && ev_count[DEV_RUN_OVER] == 1 && ui.page == PAGE_RUNOVER, "the second extinction did not end the run (page %d)", ui.page);
    float hx, hy;
    uint32_t dna_kept = dish.dna;                                           /* (badges earned during the run add to it) */
    dish_ui_hotspot(HS_DONE, &hx, &hy); tap_at(hx, hy);                     /* NEW RUN */
    CHECK(ui.setup == 1, "NEW RUN did not open the setup");
    tap_at(hx, hy);                                                         /* START */
    dish_ui_hotspot(HS_STARTER_1, &hx, &hy); tap_at(hx, hy);                /* the coccus */
    dish_ui_hotspot(HS_DONE, &hx, &hy); tap_at(hx, hy); tap_at(hx, hy); tap_at(hx, hy);   /* NEXT, NEXT, BEGIN */
    run(&dish, 0.1f, 0, false);
    CHECK(dish.run == 2 && !dish.run_over && !dish.vault_used && dish.strain[DISH_PET].species == SP_COCCUS && dish_mut_total(&dish, DISH_PET) == 0,
          "the new run is not fresh (run %d, over %d, vault %d, %d mutations)", dish.run, dish.run_over, dish.vault_used, dish_mut_total(&dish, DISH_PET));
    CHECK(dish.dna == dna_kept && dna_kept >= 55 && (dish.badges & badges) == badges && (dish.dex & dex) == dex && dish.unlocked == unl, "the new run lost what was earned (DNA %u of %u)", dish.dna, dna_kept);
    CHECK(ui.page == PAGE_NONE && !ui.setup, "the new run left a page up");
    /* 6. a save from before runs (v4) loads: run 1, the dex's forms moved up */
    {   static uint8_t buf[DISH_SAVE_MAX]; static dish_t back;
        FILE *f = fopen("testdata/dish-v4.sav", "rb");
        size_t n = f ? fread(buf, 1, sizeof buf, f) : 0; if (f) fclose(f);
        CHECK(n > 0 && dish_load(&back, buf, n, 3), "the v4 save (testdata/dish-v4.sav) did not load");
        CHECK(back.run == 1 && back.dna == 77 && !strcmp(back.strain[DISH_PET].name, "OLDIE"), "the v4 save loaded wrong (run %d, DNA %u)", back.run, back.dna);
        CHECK((back.dex & DEX_FORM(FORM_SHELL)) && !(back.dex & (1u << 10)) && (back.dex & DEX_SPECIES(SP_COCCUS)), "the v4 dex was not moved (%08x)", back.dex);
        CHECK(back.offer[0] == 0xFF && back.next_draft_gen > back.strain[DISH_PET].max_gen, "the v4 save's draft state is wrong (next at %u)", back.next_draft_gen);
        size_t n5 = dish_save(&back, buf, sizeof buf); static dish_t again;
        CHECK(dish_load(&again, buf, n5, 3) && again.dex == back.dex && again.run == 1, "a migrated save does not round-trip as v5"); }
    printf("selftest-run: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

/* a lazy keeper: feeds the colony every 45 s and takes the first card of every draft */
static void lazy_run(dish_t *d, float seconds) {
    for (float t = 0; t < seconds && !d->run_over; t += 15) {
        run(d, 15, 0, false);
        const strain_t *pet = &d->strain[DISH_PET];
        if ((int)t % 45 == 0 && pet->count) dish_feed(d, pet->cx, pet->cy, 1.0f);
        while (d->drafts) { dish_draft_deal(d); dish_draft_take(d, 0); }
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
    {   FRESH(76); int ds = dish_visit(&dish, SP_DONOR); int di = -1, pi = -1;
        for (int i = 0; i < DISH_MAX_CELLS; i++) { if (dish.cell[i].strain == ds && di < 0) di = i; if (dish.cell[i].strain == DISH_PET && dish.cell[i].state == CS_LIVE) pi = i; }
        int d0 = dish.drafts, g0 = ev_count[DEV_GIFT];
        if (di >= 0 && pi >= 0) { dish.cell[di].x = dish.cell[pi].x + 4; dish.cell[di].y = dish.cell[pi].y; run(&dish, 0.2f, 0, false); }
        CHECK(dish.drafts == d0 + 1 && ev_count[DEV_GIFT] == g0 + 1, "touching the donor gave no free card"); }
    {   FRESH(77); unsigned dead0 = dish.strain[DISH_PET].deaths; dish_visit(&dish, SP_VIBRIO); run(&dish, 90, 10, false);
        CHECK(dish.strain[DISH_PET].deaths > dead0, "the vibrio hunted nothing"); }
    /* a visitor whose time is up leaves, and frees its slot */
    {   FRESH(78); int vs = dish_visit(&dish, SP_HALO); int gone0 = ev_count[DEV_VISIT_GONE];
        dish.strain[vs].stay_t = 0; run(&dish, 45, 10, false);
        CHECK(!dish.strain[vs].used && ev_count[DEV_VISIT_GONE] == gone0 + 1, "a visitor past its time did not leave (%d cells)", dish.strain[vs].count); }
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

int main(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--selftest-dish")) return selftest_dish();
        if (!strcmp(argv[a], "--selftest-pet")) return selftest_pet();
        if (!strcmp(argv[a], "--selftest-evolve")) return selftest_evolve();
        if (!strcmp(argv[a], "--selftest-prog")) return selftest_prog();
        if (!strcmp(argv[a], "--selftest-render")) return selftest_render();
        if (!strcmp(argv[a], "--selftest-run")) return selftest_run();
        if (!strcmp(argv[a], "--selftest-visitors")) return selftest_visitors();
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
            run(&dish, mins * 60, 45, true);
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
