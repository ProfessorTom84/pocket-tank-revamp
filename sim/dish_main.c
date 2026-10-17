/* dish_main.c — the Pocket Dish simulator.
 *
 * Headless always works (selftests, --snapshot, --narrate); with SDL2 found
 * at build time (DISH_SDL) it also opens a window where the mouse is the
 * finger. Same flags as fishsim where they mean the same thing. */
#include "dish.h"
#include "dish_render.h"
#include "dish_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static dish_t dish;
static dish_ui_t ui;
static void on_event(int ev, int strain, void *ud) { (void)ud; dish_ui_event(&ui, &dish, ev, strain); }
static uint16_t fb[DISH_W * DISH_H], scene[DISH_W * DISH_H];

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
    tap_icon(ICON_FEED);
    CHECK(dish_field(dish.nutr, pet->cx, pet->cy) > before + 0.2f, "FEED put no food by the pet");
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
    buf[0] ^= 0xFF;
    CHECK(!dish_load(&back, buf, n, 5), "a corrupt save loaded");
    run(&dish, 1, 0, false);
    printf("selftest-pet: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

#ifdef DISH_SDL
#include <SDL.h>
/* the window: the mouse is the finger. Keys: N light, O an outbreak, F 10x time,
 * S save now, X a fresh dish, Q quit. The dish saves to dish.sav every 30 s. */
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
    if (fresh || !load_file()) dish_init(&dish, SDL_GetTicks() | 1, SP_BACILLUS, "BLIP");
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
                case SDLK_x: dish_init(&dish, SDL_GetTicks() | 1, SP_BACILLUS, "BLIP"); dish_ui_init(&ui); break;
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

int main(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--selftest-dish")) return selftest_dish();
        if (!strcmp(argv[a], "--selftest-pet")) return selftest_pet();
        if (!strcmp(argv[a], "--bench")) {          /* render cost on a busy dish: the number to compare with the device */
            dish_render_set_scene(scene);
            dish_init(&dish, 3, SP_BACILLUS, "BLIP"); dish_ui_init(&ui);
            run(&dish, 400, 12, false);
            dish_outbreak(&dish, 10); run(&dish, 5, 0, false);
            clock_t t0 = clock();
            for (int k = 0; k < 200; k++) { dish_tick(&dish, 1 / 30.0f, dish_brain_rules); }
            clock_t t1 = clock();
            for (int k = 0; k < 200; k++) { dish_render(&dish, fb, DISH_W); dish_ui_render(&ui, &dish, fb, DISH_W); }
            clock_t t2 = clock();
            printf("bench: %d cells, tick %.2f ms, render %.2f ms per frame\n", dish.n_live,
                   (t1 - t0) * 1000.0 / CLOCKS_PER_SEC / 200, (t2 - t1) * 1000.0 / CLOCKS_PER_SEC / 200);
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
