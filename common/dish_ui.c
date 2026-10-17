/* dish_ui.c — the pet layer: see dish_ui.h. */
#include "dish_ui.h"
#include "dish_render.h"
#include "dish_prog.h"
#include "sounds.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TAU 6.2831853f
const char *const NEED_NAMES[NEED_COUNT] = { "FOOD", "COMFORT", "SPACE", "CLEAN", "HEALTH" };
static const char *const DOING[ST_COUNT] = { "LOOKING FOR FOOD", "HUDDLING TOGETHER", "SPLITTING IN TWO", "ON THE HUNT",
                                             "HIDING", "RESTING", "TURNING TO SPORES", "SPREADING OUT" };

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float hypot2(float x, float y) { return sqrtf(x * x + y * y); }

/* ---- layout ---- */
void dish_ui_icon_pos(icon_t i, float *x, float *y) {
    /* the icons follow the glass's bottom edge: a straight row on a flat bottom, a curve where the glass curves */
    *x = DISH_CX + (i - (ICON_COUNT - 1) * 0.5f) * (2 * DISH_ICON_R + 8);   /* FEED on the left */
    *y = DISH_H - DISH_ICON_R - 4;
    while (*y > DISH_CY && !dish_inside(*x, *y, DISH_ICON_R + 6)) *y -= 1;   /* lifted, never pulled sideways: the row keeps its spacing */
}
/* the highest row where a thing w px wide, centred, sits m px inside the glass (the round board's top is narrow) */
static float top_for(float w, float m) {
    float y = m;
    while (y < DISH_CY && !(dish_inside(DISH_CX - w * 0.5f, y, m) && dish_inside(DISH_CX + w * 0.5f, y, m))) y += 2;
    return y;
}
#define PW (DISH_W - 48 < 340 ? DISH_W - 48 : 340)
#define PH (DISH_H - 80 < 272 ? DISH_H - 80 : 272)
#define PX ((int)(DISH_CX - PW / 2))
#define PY ((int)(DISH_CY - PH / 2))
typedef struct { int x, y, w, h; } rect_t;
static bool in_rect(rect_t r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
/* the lab: three dials (HEAT, PH, SALT), then LIGHT / UV / SAMPLES */
static rect_t dial_minus(int row) { return (rect_t){ PX + 76, PY + 46 + row * 42, 36, 34 }; }
static rect_t dial_plus(int row)  { return (rect_t){ PX + PW - 52, PY + 46 + row * 42, 36, 34 }; }
static rect_t lab_btn(int k)      { int w = (PW - 52) / 3; return (rect_t){ PX + 16 + k * (w + 10), PY + 176, w, 34 }; }
static rect_t lab_minus(void) { return dial_minus(0); }
static rect_t lab_plus(void)  { return dial_plus(0); }
static rect_t lab_light(void) { return lab_btn(0); }
/* the sample rack */
static const species_t SAMPLES[3] = { SP_SPIRILLUM, SP_CYANO, SP_MYXO };
static const char *const SAMPLE_SAYS[3] = { "A SPINNING CORKSCREW. LOVES HEAT.", "MAKES ITS OWN FOOD FROM LIGHT.", "A HUNTER: IT EATS OTHER GERMS!" };
static rect_t sample_row(int k) { return (rect_t){ PX + 16, PY + 48 + k * 56, PW - 32, 50 }; }
static rect_t page_done(void) { return (rect_t){ (int)DISH_CX - 60, PY + PH - 54, 120, 40 }; }
/* medicine: the two cures */
static rect_t meds_drug(void)  { return (rect_t){ PX + 20, PY + 58, PW - 40, 66 }; }
static rect_t meds_virus(void) { return (rect_t){ PX + 20, PY + 134, PW - 40, 66 }; }
/* the card's two buttons, the dex's and the badges' grids */
static rect_t card_btn(int k) { int w = (PW - 52) / 2; return (rect_t){ PX + 18 + k * (w + 16), PY + PH - 42, w, 30 }; }
static rect_t tile(page_t p, int i) {
    if (p == PAGE_DEX) { int c = i % 5, r = i / 5; return (rect_t){ (int)DISH_CX - 156 + c * 64, PY + 50 + r * 64, 56, 56 }; }
    int c = i % 6, r = i / 6; return (rect_t){ (int)DISH_CX - 153 + c * 52, PY + 50 + r * 52, 46, 46 };
}
void dish_ui_tile(page_t page, int i, float *x, float *y) { rect_t r = tile(page, i); *x = r.x + r.w * 0.5f; *y = r.y + r.h * 0.5f; }
/* the setup: three starters, the name roller */
static const species_t STARTERS[3] = { SP_BACILLUS, SP_COCCUS, SP_SPIRILLUM };
static const char *const STARTER_SAYS[3] = { "ZOOMS AROUND", "STICKS TOGETHER", "LOVES IT WARM" };
static const char *const NAMES[] = { "BLIP", "DOT", "ZIGGY", "MOSS", "PIP", "GLOOP", "NIX", "TOFU", "BEAN", "ZAP", "FIZZ", "WOBBLE",
                                     "SPECK", "NOODLE", "JELLY", "ORBIT", "PICO", "BUBBA", "SQUIGGLE", "MOCHI", "PEBBLE", "SPROUT", "GIZMO", "TWIRL" };
#define N_NAMES ((int)(sizeof NAMES / sizeof NAMES[0]))
static rect_t starter_tile(int k) { int w = (PW - 48) / 3; return (rect_t){ PX + 16 + k * (w + 8), PY + 52, w, 128 }; }
static rect_t name_prev(void) { return (rect_t){ PX + 20, PY + 92, 48, 48 }; }
static rect_t name_next(void) { return (rect_t){ PX + PW - 68, PY + 92, 48, 48 }; }
static rect_t name_box(void)  { return (rect_t){ PX + 76, PY + 88, PW - 152, 56 }; }
/* a tool in hand: the DONE chip at the top */
static rect_t tool_chip(void) { return (rect_t){ (int)DISH_CX - 70, (int)top_for(140, 6) + 34, 140, 32 }; }
void dish_ui_hotspot(hotspot_t h, float *x, float *y) {
    rect_t r;
    switch (h) {
    case HS_LAB_MINUS: r = lab_minus(); break;   case HS_LAB_PLUS: r = lab_plus(); break;
    case HS_LAB_LIGHT: r = lab_light(); break;   case HS_DONE: r = page_done(); break;
    case HS_MEDS_DRUG: r = meds_drug(); break;   case HS_MEDS_VIRUS: r = meds_virus(); break;
    case HS_PH_MINUS: r = dial_minus(1); break;   case HS_PH_PLUS: r = dial_plus(1); break;
    case HS_SALT_MINUS: r = dial_minus(2); break; case HS_SALT_PLUS: r = dial_plus(2); break;
    case HS_LAB_UV: r = lab_btn(1); break;        case HS_LAB_SAMPLES: r = lab_btn(2); break;
    case HS_SAMPLE_0: case HS_SAMPLE_1: case HS_SAMPLE_2: r = sample_row(h - HS_SAMPLE_0); break;
    case HS_CARD_DEX: r = card_btn(0); break;     case HS_CARD_BADGES: r = card_btn(1); break;
    case HS_STARTER_0: case HS_STARTER_1: case HS_STARTER_2: r = starter_tile(h - HS_STARTER_0); break;
    case HS_NAME_PREV: r = name_prev(); break;    case HS_NAME_NEXT: r = name_next(); break;
    case HS_NAME: r = name_box(); break;
    default: r = tool_chip(); break;
    }
    *x = r.x + r.w * 0.5f; *y = r.y + r.h * 0.5f;
}
/* the microscope: opposite the cell it watches */
static void lens_geom(const dish_t *d, int cell, float *cx, float *cy, float *r) {
    *r = fminf(DISH_W, DISH_H) * 0.24f;
    float x = cell >= 0 ? d->cell[cell].x : DISH_CX;
    *cx = x < DISH_CX ? DISH_CX + DISH_RX * 0.42f : DISH_CX - DISH_RX * 0.42f;
    *cy = DISH_CY - DISH_RY * 0.22f;
}

/* ---- state ---- */
void dish_ui_init(dish_ui_t *u) {
    memset(u, 0, sizeof *u);
    for (int i = 0; i < NEED_COUNT; i++) u->needs[i] = 0.8f;
    u->mood = 0.8f; u->lens = -1; u->lens_zoom = 5; u->sel = -1; u->prev_lens = -1;
}
static void sfx(dish_ui_t *u, int cue, int pitch, float cd);
void dish_ui_begin_setup(dish_ui_t *u) { u->setup = 1; u->page = PAGE_NONE; u->lens = -1; u->tool = TOOL_NONE; sfx(u, SND_WELCOME, 256, 0); }
/* a sound for the platform; cd > 0: not again for that long (a busy dish must not chatter) */
static void sfx(dish_ui_t *u, int cue, int pitch, float cd) {
    if (cue < 0 || cue >= 32 || u->snd_cd[cue] > 0 || u->snd_n >= 8) return;
    u->snd_cd[cue] = cd;
    u->snd_q[u->snd_n] = cue; u->snd_pitch[u->snd_n] = pitch; u->snd_n++;
}
int dish_ui_take_sound(dish_ui_t *u, int *pitch) {
    if (!u->snd_n) return -1;
    int cue = u->snd_q[0]; *pitch = u->snd_pitch[0];
    for (int k = 1; k < u->snd_n; k++) { u->snd_q[k - 1] = u->snd_q[k]; u->snd_pitch[k - 1] = u->snd_pitch[k]; }
    u->snd_n--;
    return cue;
}
void dish_ui_toast(dish_ui_t *u, const char *s, float seconds) {
    if (u->setup) return;                         /* nothing over the first-run setup: the dish behind it is only a demo */
    snprintf(u->toast, sizeof u->toast, "%s", s);
    u->toast_t = seconds;
}
void dish_ui_event(dish_ui_t *u, const dish_t *d, int ev, int strain) {
    const strain_t *pet = &d->strain[DISH_PET];
    char b[40];
    bool pet_ev = strain == DISH_PET;
    switch (ev) {   /* the sound first: the bank's cues at the dish's pitches */
    case DEV_FEED:     sfx(u, SND_FEED, 256, 0.15f); break;
    case DEV_DIVIDE:   if (pet_ev) sfx(u, SND_BUBBLES, 340, 0.7f); break;
    case DEV_POP:      if (pet_ev) sfx(u, SND_TAP, 170, 0.35f); break;
    case DEV_BURST:    sfx(u, SND_SPOOK, 300, 0.6f); break;
    case DEV_INFECT:   if (pet_ev) sfx(u, SND_ERROR, 200, 2); break;
    case DEV_EXTINCT:  sfx(u, SND_SLEEP, 256, 0); break;
    case DEV_RESPAWN:  sfx(u, SND_ARRIVAL, 256, 0); break;
    case DEV_OUTBREAK: sfx(u, SND_SPOOK, 200, 0); break;
    case DEV_BADGE:    sfx(u, SND_MILESTONE, 256, 0); break;
    }
    switch (ev) {
    case DEV_EXTINCT: u->extinct_show = 1; u->lens = -1; u->page = PAGE_NONE; break;
    case DEV_RESPAWN: u->extinct_show = 0; u->back_show = 3.5f; snprintf(b, sizeof b, "%s IS BACK!", pet->name); dish_ui_toast(u, b, 3.5f); break;
    case DEV_OUTBREAK: dish_ui_toast(u, "VIRUSES ARE COMING!", 4); break;
    case DEV_BADGE: snprintf(b, sizeof b, "BADGE: %s  +%d DNA", BADGE_NAMES[strain], BADGE_DNA); dish_ui_toast(u, b, 4); break;
    case DEV_INFECT:
        if (strain == DISH_PET && u->toast_t <= 0) { snprintf(b, sizeof b, "%s CAUGHT A VIRUS", pet->name); dish_ui_toast(u, b, 3); }
        break;
    case DEV_STRAT:
        if (u->toast_t <= 0 && pet->count) { snprintf(b, sizeof b, "%s", DOING[pet->strat.id]); dish_ui_toast(u, b, 2.5f); }
        break;
    case DEV_DIVIDE:
        if (strain == DISH_PET && (pet->births == 1 || pet->births % 25 == 0) && u->toast_t <= 0) {
            snprintf(b, sizeof b, pet->births == 1 ? "%s SPLIT IN TWO!" : "%s IS GROWING!", pet->name); dish_ui_toast(u, b, 3);
        }
        break;
    }
}

static void need_targets(const dish_t *d, float *t) {
    const strain_t *st = &d->strain[DISH_PET];
    t[NEED_FOOD] = clampf((st->energy - 0.2f) / 0.55f, 0, 1);
    t[NEED_COMFORT] = st->comfort;
    t[NEED_SPACE] = 1 - st->crowd;
    t[NEED_CLEAN] = 1 - clampf(st->filth * 2.2f, 0, 1);
    t[NEED_HEALTH] = st->health * (1 - st->infected);
}
static int worst_need(const dish_ui_t *u) {
    int w = 0;
    for (int i = 1; i < NEED_COUNT; i++) if (u->needs[i] < u->needs[w]) w = i;
    return w;
}
static icon_t icon_for_need(int n) {
    switch (n) { case NEED_FOOD: return ICON_FEED; case NEED_COMFORT: return ICON_LAB; case NEED_HEALTH: return ICON_MEDS; default: return ICON_CLEAN; }
}

static int icon_hit(float x, float y);
void dish_ui_tick(dish_ui_t *u, dish_t *d, float dt) {
    u->clock += dt;
    for (int k = 0; k < 32; k++) if (u->snd_cd[k] > 0) u->snd_cd[k] -= dt;
    if ((int)u->page != u->prev_page) {              /* a page opening, flipping or closing */
        sfx(u, u->page == PAGE_NONE ? SND_CARD_CLOSE : SND_CARD_OPEN, 256, 0);
        u->prev_page = u->page;
    }
    if ((u->lens >= 0) != (u->prev_lens >= 0)) { if (u->lens >= 0) sfx(u, SND_INVESTIGATE, 256, 0); u->prev_lens = u->lens; }
    const strain_t *pet = &d->strain[DISH_PET];
    if (pet->count > 0) {
        float t[NEED_COUNT]; need_targets(d, t);
        float mn = 1, sum = 0;
        for (int i = 0; i < NEED_COUNT; i++) {
            u->needs[i] += (t[i] - u->needs[i]) * clampf(dt * 1.5f, 0, 1);
            mn = fminf(mn, u->needs[i]); sum += u->needs[i];
        }
        u->mood = 0.5f * mn + 0.5f * sum / NEED_COUNT;
    }
    dish_render_mood = u->mood;
    if (u->toast_t > 0) u->toast_t -= dt;
    if (u->back_show > 0) u->back_show -= dt;
    if (pet->extinct_t <= 0) u->extinct_show = 0;
    /* the pet asks for things: an emote over the colony now and then */
    if (u->emote_t > 0) u->emote_t -= dt;
    else if ((u->emote_cd -= dt) <= 0 && pet->count > 0) {
        int w = worst_need(u);
        if (u->needs[w] < 0.35f) { u->emote = w; u->emote_t = 3; u->emote_cd = 6; }
        else if (u->mood > 0.75f) { u->emote = NEED_COUNT; u->emote_t = 2.5f; u->emote_cd = 14; }   /* hearts */
        else u->emote_cd = 2;
    }
    /* the lens follows its cell, and closes when the cell is gone */
    if (u->lens >= 0) {
        const cell_t *c = &d->cell[u->lens];
        if ((c->state != CS_LIVE && c->state != CS_SPORE) || c->id != u->lens_id) u->lens = -1;
    }
    /* a held finger warms the agar under it */
    /* a held finger warms the agar - when it came down on the agar, not on an icon or past the glass */
    if (u->down && !u->dragged && u->page == PAGE_NONE && !u->setup && u->tool == TOOL_NONE && icon_hit(u->dx0, u->dy0) < 0 && dish_inside(u->dx0, u->dy0, 4)) {
        u->down_t += dt; u->still_t += dt;
        if (u->down_t > 0.6f) { u->warmed = true; dish_warm(d, u->lx, u->ly, dt); }
    }
    if (u->tool != TOOL_NONE && (u->tool_idle += dt) > 20) u->tool = TOOL_NONE;
    for (int k = 0; k < 3; k++) u->tap_t[k] += dt;
}

/* ---- touch ---- */
static int icon_hit(float x, float y) {
    for (int i = 0; i < ICON_COUNT; i++) {
        float ix, iy; dish_ui_icon_pos((icon_t)i, &ix, &iy);
        if (hypot2(x - ix, y - iy) < DISH_ICON_R + 4) return i;
    }
    return -1;
}
static int pet_cell_near(const dish_t *d, float x, float y) {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        const cell_t *c = &d->cell[i];
        if (c->strain != DISH_PET || (c->state != CS_LIVE && c->state != CS_SPORE)) continue;
        float dd = hypot2(c->x - x, c->y - y);
        if (dd < bd) { bd = dd; best = i; }
    }
    return best;
}
static void open_lens(dish_ui_t *u, const dish_t *d, int cell) {
    if (cell < 0) return;
    u->lens = cell; u->lens_id = d->cell[cell].id;
}
static void icon_press(dish_ui_t *u, dish_t *d, int i) {
    const strain_t *pet = &d->strain[DISH_PET];
    switch (i) {
    case ICON_FEED: {
        float x = pet->count ? pet->cx : DISH_CX, y = pet->count ? pet->cy : DISH_CY;
        for (int k = 0; k < 3; k++) {
            float a = k * TAU / 3 + u->clock;
            dish_feed(d, x + cosf(a) * 18, y + sinf(a) * 18, 0.45f);
        }
        break; }
    case ICON_LAB:   u->page = PAGE_LAB; break;
    case ICON_MEDS:  u->page = PAGE_MEDS; break;
    case ICON_CLEAN: u->tool = u->tool == TOOL_WIPE ? TOOL_NONE : TOOL_WIPE; u->tool_idle = 0; sfx(u, SND_TAP, 256, 0); break;
    case ICON_ZOOM:
        if (u->lens >= 0) u->lens = -1;
        else open_lens(u, d, pet_cell_near(d, pet->cx, pet->cy));
        break;
    case ICON_CARD:  u->page = PAGE_CARD; break;
    }
}
/* a locked thing: with enough DNA the tap unlocks it; without, it says what it costs. True only when already owned. */
static bool gate(dish_ui_t *u, dish_t *d, int unl) {
    if (dish_has(d, unl)) return true;
    char b[40];
    if (dish_prog_unlock(d, unl)) { snprintf(b, sizeof b, "%s UNLOCKED!", UNL_NAMES[unl]); sfx(u, SND_STAGE_UP, 256, 0); }
    else { snprintf(b, sizeof b, "%s COSTS %d DNA", UNL_NAMES[unl], UNL_COST[unl]); sfx(u, SND_ERROR, 256, 0.3f); }
    dish_ui_toast(u, b, 3);
    return false;
}
static void setup_tap(dish_ui_t *u, dish_t *d, float x, float y) {
    bool next = in_rect(page_done(), x, y);
    if (next) sfx(u, u->setup == 4 ? SND_CONFIRM : SND_TAP, 256, 0);
    switch (u->setup) {
    case 1: if (next) u->setup = 2; break;
    case 2:
        for (int k = 0; k < 3; k++) if (in_rect(starter_tile(k), x, y)) { u->starter = k; sfx(u, SND_TAP, 256 + k * 40, 0); }
        if (next) u->setup = 3;
        break;
    case 3:
        if (in_rect(name_prev(), x, y)) { u->name_i = (u->name_i + N_NAMES - 1) % N_NAMES; sfx(u, SND_WHEEL_TICK, 256, 0); }
        else if (in_rect(name_next(), x, y)) { u->name_i = (u->name_i + 1) % N_NAMES; sfx(u, SND_WHEEL_TICK, 256, 0); }
        else if (in_rect(name_box(), x, y)) { u->name_i = (u->name_i + 7 + (int)(u->clock * 13) % (N_NAMES - 1)) % N_NAMES; sfx(u, SND_WHEEL_TICK, 320, 0); }
        else if (next) u->setup = 4;
        break;
    case 4:
        if (next) {
            char b[40];
            dish_init(d, (uint32_t)(u->clock * 1000) * 2654435761u | 1, STARTERS[u->starter], NAMES[u->name_i]);
            u->setup = 0;
            for (int i = 0; i < NEED_COUNT; i++) u->needs[i] = 0.8f;
            snprintf(b, sizeof b, "SAY HI TO %s!", NAMES[u->name_i]);
            dish_ui_toast(u, b, 4);
        }
        break;
    }
}
static void page_tap(dish_ui_t *u, dish_t *d, float x, float y) {
    switch (u->page) {
    case PAGE_CARD:
        if (in_rect(card_btn(0), x, y)) { u->page = PAGE_DEX; u->sel = -1; }
        else if (in_rect(card_btn(1), x, y)) { u->page = PAGE_BADGES; u->sel = -1; }
        else u->page = PAGE_NONE;
        break;
    case PAGE_DEX: case PAGE_BADGES: {
        int n = u->page == PAGE_DEX ? SP_COUNT + FORM_COUNT : BADGE_COUNT;
        for (int i = 0; i < n; i++) if (in_rect(tile(u->page, i), x, y)) { u->sel = i; return; }
        if (in_rect(page_done(), x, y)) { if (u->page == PAGE_BADGES) d->badges_new = 0; u->page = PAGE_CARD; }
        break; }
    case PAGE_LAB:
        if (in_rect(lab_minus(), x, y) || in_rect(lab_plus(), x, y) || in_rect(dial_minus(1), x, y) || in_rect(dial_plus(1), x, y)
            || in_rect(dial_minus(2), x, y) || in_rect(dial_plus(2), x, y)) sfx(u, SND_WHEEL_TICK, 256, 0);   /* a dial's click */
        if (in_rect(lab_minus(), x, y)) d->temp = clampf(d->temp - 0.05f, 0.05f, 0.95f);
        else if (in_rect(lab_plus(), x, y)) d->temp = clampf(d->temp + 0.05f, 0.05f, 0.95f);
        else if (in_rect(dial_minus(1), x, y)) { if (gate(u, d, UNL_PH)) d->ph = clampf(d->ph - 0.05f, 0, 1); }
        else if (in_rect(dial_plus(1), x, y)) { if (gate(u, d, UNL_PH)) d->ph = clampf(d->ph + 0.05f, 0, 1); }
        else if (in_rect(dial_minus(2), x, y)) { if (gate(u, d, UNL_SALT)) d->salt = clampf(d->salt - 0.05f, 0, 1); }
        else if (in_rect(dial_plus(2), x, y)) { if (gate(u, d, UNL_SALT)) d->salt = clampf(d->salt + 0.05f, 0, 1); }
        else if (in_rect(lab_light(), x, y)) { d->light = !d->light; sfx(u, d->light ? SND_LIGHT_ON : SND_LIGHT_OFF, 256, 0); }
        else if (in_rect(lab_btn(1), x, y)) { if (gate(u, d, UNL_UV)) { dish_uv(d, 10); u->page = PAGE_NONE; dish_ui_toast(u, "UV ON: WATCH THEM CHANGE!", 3); sfx(u, SND_LIGHT_ON, 150, 0); } }
        else if (in_rect(lab_btn(2), x, y)) u->page = PAGE_SAMPLES;
        else if (in_rect(page_done(), x, y)) u->page = PAGE_NONE;
        break;
    case PAGE_MEDS:
        if (in_rect(meds_drug(), x, y)) { if (!gate(u, d, UNL_DRUG)) return; u->page = PAGE_NONE; u->tool = TOOL_DRUG; u->tool_idle = 0; sfx(u, SND_CONFIRM, 256, 0); dish_ui_toast(u, "CAREFUL: IT KILLS GOOD GERMS TOO", 3.5f); }
        else if (in_rect(meds_virus(), x, y)) { u->page = PAGE_NONE; dish_antiviral(d); sfx(u, SND_CONFIRM, 300, 0); dish_ui_toast(u, "THE VIRUSES ARE GONE", 3); }
        else if (in_rect(page_done(), x, y)) u->page = PAGE_NONE;
        break;
    case PAGE_SAMPLES:
        for (int k = 0; k < 3; k++)
            if (in_rect(sample_row(k), x, y)) {
                if (!gate(u, d, dish_unl_for_species(SAMPLES[k]))) return;
                int s = dish_add_sample(d, SAMPLES[k]);
                char b[40];
                if (s >= 0) { snprintf(b, sizeof b, "%s JOINED THE DISH", SPECIES_NAMES[SAMPLES[k]]); u->page = PAGE_NONE; sfx(u, SND_ARRIVAL, 320, 0); }
                else snprintf(b, sizeof b, "NO ROOM FOR %s", SPECIES_NAMES[SAMPLES[k]]);
                dish_ui_toast(u, b, 3);
                return;
            }
        if (in_rect(page_done(), x, y)) u->page = PAGE_LAB;
        break;
    default: u->page = PAGE_NONE;
    }
}
static void tap(dish_ui_t *u, dish_t *d, float x, float y) {
    if (u->setup) { setup_tap(u, d, x, y); return; }
    if (u->page != PAGE_NONE) { page_tap(u, d, x, y); return; }
    int ic = icon_hit(x, y);
    if (ic >= 0) { icon_press(u, d, ic); return; }
    if (u->tool != TOOL_NONE && in_rect(tool_chip(), x, y)) { u->tool = TOOL_NONE; return; }
    if (u->lens >= 0) {                              /* a tap on the lens puts it away */
        float lx, ly, lr; lens_geom(d, u->lens, &lx, &ly, &lr);
        if (hypot2(x - lx, y - ly) < lr) { u->lens = -1; return; }
    }
    if (!dish_inside(x, y, 4)) return;
    u->tool_idle = 0;
    switch (u->tool) {
    case TOOL_DRUG: dish_dose(d, x, y, 0.7f); sfx(u, SND_FEED, 180, 0.15f); return;
    case TOOL_WIPE: dish_wipe(d, x, y, 24); return;
    default: break;
    }
    /* three quick raps on the glass scatter everyone */
    u->tap_t[2] = u->tap_t[1]; u->tap_t[1] = u->tap_t[0]; u->tap_t[0] = 0;
    if (u->tap_t[2] < 0.8f) { dish_rap(d, x, y); sfx(u, SND_SPOOK, 256, 0); dish_ui_toast(u, "EEK!", 1.5f); u->tap_t[1] = u->tap_t[2] = 9; return; }
    int c = dish_cell_at(d, x, y, 5);
    if (c >= 0) { open_lens(u, d, c); return; }
    dish_feed(d, x, y, 0.6f);
}
void dish_ui_touch(dish_ui_t *u, dish_t *d, float x, float y, bool down) {
    if (down && !u->down) {
        u->down = true; u->dx0 = u->lx = x; u->dy0 = u->ly = y;
        u->down_t = u->still_t = 0; u->dragged = u->warmed = false;
        return;
    }
    if (down) {
        if (hypot2(x - u->dx0, y - u->dy0) > 10) u->dragged = true;
        if (u->dragged && u->page == PAGE_NONE && !u->setup && icon_hit(u->dx0, u->dy0) < 0) {
            if (u->tool == TOOL_WIPE) { dish_wipe(d, x, y, 22); u->tool_idle = 0; sfx(u, SND_WIPE, 256, 0.45f); }
            else if (u->tool == TOOL_NONE) dish_stir(d, x, y, x - u->lx, y - u->ly);
        }
        u->lx = x; u->ly = y;
        return;
    }
    if (!u->down) return;
    u->down = false;
    if (!u->dragged && !u->warmed) tap(u, d, x, y);
}

/* ---- drawing ---- */
static void icon_glyph(uint16_t *fb, int st, int kind, float x, float y, float s, float a) {
    int A = (int)(255 * a);
    switch (kind) {
    case ICON_FEED:          /* a pipette and its drop */
        dr_line(fb, st, x - s * 0.42f, y - s * 0.5f, x + s * 0.12f, y + s * 0.12f, s * 0.24f, 0xD8E6F5, A);
        dr_disc(fb, st, x - s * 0.44f, y - s * 0.52f, s * 0.2f, 0xFF7A8A, A);
        dr_line(fb, st, x + s * 0.12f, y + s * 0.12f, x + s * 0.24f, y + s * 0.24f, s * 0.1f, 0xD8E6F5, A);
        dr_glow(fb, st, x + s * 0.36f, y + s * 0.44f, s * 0.38f, 0xFFB040, (int)(A * 0.7f));
        dr_disc(fb, st, x + s * 0.36f, y + s * 0.44f, s * 0.15f, 0xFFD27A, A);
        break;
    case ICON_LAB:           /* a flask with something green in it */
        dr_line(fb, st, x - s * 0.13f, y - s * 0.55f, x - s * 0.13f, y - s * 0.12f, s * 0.08f, 0xD8E6F5, A);
        dr_line(fb, st, x + s * 0.13f, y - s * 0.55f, x + s * 0.13f, y - s * 0.12f, s * 0.08f, 0xD8E6F5, A);
        dr_line(fb, st, x - s * 0.13f, y - s * 0.12f, x - s * 0.45f, y + s * 0.48f, s * 0.08f, 0xD8E6F5, A);
        dr_line(fb, st, x + s * 0.13f, y - s * 0.12f, x + s * 0.45f, y + s * 0.48f, s * 0.08f, 0xD8E6F5, A);
        dr_line(fb, st, x - s * 0.45f, y + s * 0.48f, x + s * 0.45f, y + s * 0.48f, s * 0.08f, 0xD8E6F5, A);
        for (int k = 0; k < 4; k++) { float yy = y + s * (0.18f + k * 0.08f), hw = s * (0.26f + k * 0.045f); dr_line(fb, st, x - hw, yy, x + hw, yy, s * 0.07f, 0x5CF08A, (int)(A * 0.85f)); }
        dr_glow(fb, st, x, y + s * 0.3f, s * 0.5f, 0x2EE070, (int)(A * 0.45f));
        dr_disc(fb, st, x + s * 0.08f, y - s * 0.28f, s * 0.06f, 0xBFFFD0, A);
        break;
    case ICON_MEDS:          /* a capsule */
        dr_line(fb, st, x - s * 0.34f, y + s * 0.34f, x, y, s * 0.42f, 0xFF5470, A);
        dr_line(fb, st, x, y, x + s * 0.34f, y - s * 0.34f, s * 0.42f, 0xF2F4FF, A);
        dr_glow(fb, st, x - s * 0.2f, y - s * 0.05f, s * 0.12f, 0xFFFFFF, (int)(A * 0.6f));
        break;
    case ICON_CLEAN:         /* a sponge, sparkling */
        dr_round_rect(fb, st, (int)(x - s * 0.5f), (int)(y - s * 0.32f), (int)(s * 1.0f), (int)(s * 0.66f), (int)(s * 0.16f), 0xFFD23C, A);
        dr_disc(fb, st, x - s * 0.22f, y - s * 0.06f, s * 0.08f, 0xA07810, A);
        dr_disc(fb, st, x + s * 0.12f, y + s * 0.1f, s * 0.07f, 0xA07810, A);
        dr_disc(fb, st, x + s * 0.28f, y - s * 0.14f, s * 0.06f, 0xA07810, A);
        dr_glow(fb, st, x + s * 0.5f, y - s * 0.52f, s * 0.22f, 0xFFFFFF, A);
        break;
    case ICON_ZOOM:          /* a magnifier */
        dr_ring(fb, st, x - s * 0.1f, y - s * 0.1f, s * 0.3f, s * 0.08f, 0xD8E6F5, A);
        dr_glow(fb, st, x - s * 0.1f, y - s * 0.1f, s * 0.26f, 0x6FE0FF, (int)(A * 0.5f));
        dr_line(fb, st, x + s * 0.13f, y + s * 0.13f, x + s * 0.45f, y + s * 0.45f, s * 0.18f, 0xD8E6F5, A);
        break;
    case ICON_CARD: {        /* a little cell's face */
        dr_glow(fb, st, x, y, s * 0.62f, 0x3FE0D0, (int)(A * 0.5f));
        dr_ring(fb, st, x, y, s * 0.42f, s * 0.07f, 0xA8FFF4, A);
        dr_disc(fb, st, x - s * 0.15f, y - s * 0.06f, s * 0.08f, 0x06060A, A);
        dr_disc(fb, st, x + s * 0.15f, y - s * 0.06f, s * 0.08f, 0x06060A, A);
        dr_line(fb, st, x - s * 0.12f, y + s * 0.14f, x, y + s * 0.2f, s * 0.05f, 0x06060A, A);
        dr_line(fb, st, x, y + s * 0.2f, x + s * 0.12f, y + s * 0.14f, s * 0.05f, 0x06060A, A);
        break; }
    }
}
static void draw_icon(uint16_t *fb, int st, int kind, float x, float y, bool active, float alert, float clock) {
    float s = DISH_ICON_R;
    dr_disc(fb, st, x, y, s, 0x060A16, 190);
    if (alert > 0) {
        float p = 0.5f + 0.5f * sinf(clock * 6);
        dr_glow(fb, st, x, y, s * 1.7f, 0xFFA030, (int)(140 * alert * p));
        dr_ring(fb, st, x, y, s + 2, 1.8f, 0xFFB648, (int)(255 * alert * (0.5f + 0.5f * p)));
    }
    dr_ring(fb, st, x, y, s - 0.5f, 1.2f, active ? 0x7FF4FF : 0x5C7598, active ? 255 : 170);
    if (active) dr_glow(fb, st, x, y, s * 1.4f, 0x3FD8FF, 90);
    dr_glow(fb, st, x - s * 0.45f, y - s * 0.5f, s * 0.45f, 0xFFFFFF, 40);
    icon_glyph(fb, st, kind, x, y, s * 1.05f, 1);
}
static void panel(uint16_t *fb, int st, int x, int y, int w, int h) {
    dr_rect(fb, st, 0, 0, DISH_W, DISH_H, 0x000000, 110);
    dr_round_rect(fb, st, x - 2, y - 2, w + 4, h + 4, 18, 0x3C6AA0, 255);
    dr_round_rect(fb, st, x, y, w, h, 16, 0x0A1224, 245);
    for (int k = 0; k < 3; k++) dr_glow(fb, st, x + w * (0.25f + k * 0.25f), y + 2, 30, 0x5FA8FF, 25);
}
static void text_c(uint16_t *fb, int st, float cx, int y, int sc, uint32_t rgb, const char *s) { dr_text(fb, st, (int)(cx - dr_text_w(s, sc) * 0.5f), y, sc, rgb, s); }
static void button(uint16_t *fb, int st, rect_t r, uint32_t fill, uint32_t edge, const char *label, int sc) {
    dr_round_rect(fb, st, r.x - 1, r.y - 1, r.w + 2, r.h + 2, 12, edge, 255);
    dr_round_rect(fb, st, r.x + 1, r.y + 1, r.w - 2, r.h - 2, 11, fill, 255);
    if (label) text_c(fb, st, r.x + r.w * 0.5f, r.y + (r.h - 7 * sc) / 2, sc, 0xEAF4FF, label);
}
static uint32_t need_color(float v) { return v > 0.6f ? 0x4CF08C : v > 0.3f ? 0xFFC23C : 0xFF4E5E; }
static void bar(uint16_t *fb, int st, int x, int y, int w, int h, float v, uint32_t col) {
    dr_round_rect(fb, st, x, y, w, h, h / 2, 0x1A2438, 255);
    int fw = (int)(w * clampf(v, 0, 1));
    if (fw >= h) { dr_round_rect(fb, st, x, y, fw, h, h / 2, col, 255); dr_glow(fb, st, x + fw - h * 0.5f, y + h * 0.5f, h * 1.2f, col, 90); }
    dr_rect(fb, st, x + h / 2, y + 1, w - h, 1, 0xFFFFFF, 30);
}

/* a double helix: the DNA points' mark */
static void dna_glyph(uint16_t *fb, int st, float x, float y, float s) {
    float px[2] = { 0, 0 }, py[2] = { 0, 0 };
    for (int k = 0; k <= 8; k++) {
        float t = k / 8.0f, yy = y - s + t * 2 * s;
        for (int h = 0; h < 2; h++) {
            float xx = x + sinf(t * 6.2831853f + h * 3.14159f) * s * 0.45f;
            if (k) dr_line(fb, st, px[h], py[h], xx, yy, fmaxf(1, s * 0.16f), h ? 0x7FF4FF : 0xFF7AD0, 255);
            px[h] = xx; py[h] = yy;
        }
        if (k % 2 == 1) dr_line(fb, st, x + sinf(t * 6.2831853f) * s * 0.45f, yy, x - sinf(t * 6.2831853f) * s * 0.45f, yy, 1, 0xC8D8F0, 160);
    }
}
static void dna_count(uint16_t *fb, int st, float cx, int y, int sc, uint32_t dna) {
    char b[16]; snprintf(b, sizeof b, "%u", (unsigned)dna);   /* (uint32_t is unsigned long on the S3) */
    int w = dr_text_w(b, sc) + 7 * sc + 4;
    dna_glyph(fb, st, cx - w * 0.5f + 3 * sc, y + 3.5f * sc, 3.5f * sc);
    dr_text(fb, st, (int)(cx - w * 0.5f + 7 * sc + 4), y, sc, 0xEAF4FF, b);
}
/* a padlock and its price (green when you can pay it) */
static void draw_lock(uint16_t *fb, int st, float x, float y, int cost, bool afford) {
    uint32_t c = afford ? 0x4CF08C : 0x8FA0B8;
    dr_ring(fb, st, x, y - 4, 4.5f, 1.6f, c, 255);
    dr_round_rect(fb, st, (int)(x - 6), (int)(y - 2), 12, 10, 2, c, 255);
    dr_disc(fb, st, x, y + 3, 1.4f, 0x0A1224, 255);
    char b[16]; snprintf(b, sizeof b, "%d DNA", cost);
    dr_text(fb, st, (int)x + 11, (int)y - 3, 1, c, b);
}
static void lock_over(uint16_t *fb, int st, const dish_t *d, rect_t r, int unl) {
    dr_round_rect(fb, st, r.x, r.y, r.w, r.h, 10, 0x0A1224, 248);
    draw_lock(fb, st, r.x + r.w * 0.5f - 22, r.y + r.h * 0.5f, UNL_COST[unl], d->dna >= UNL_COST[unl]);
}
static const char *const SPECIES_SAYS[SP_COUNT] = { "ROUND BEADS THAT STICK IN BUNCHES.", "A ROD WITH A WHIPPY TAIL. FAST!",
    "A CORKSCREW THAT SPINS TO SWIM.", "GREEN CHAINS THAT EAT LIGHT.", "A HUNTER THAT EATS OTHER GERMS.", "A VIRUS: IT BURSTS GERMS FROM INSIDE." };
static void badge_glyph(uint16_t *fb, int st, int b, float x, float y, float s, bool got) {
    uint32_t base = got ? dr_hsv(b / 12.0f, 0.6f, 0.95f) : 0x2A3448, ink = got ? 0xFFFFFF : 0x4A5870;
    if (got) dr_glow(fb, st, x, y, s * 1.5f, base, 90);
    dr_disc(fb, st, x, y, s, base, 255);
    dr_ring(fb, st, x, y, s - 2, 1.2f, got ? 0xFFFFFF : 0x3A4860, 120);
    switch (b) {
    case BADGE_FIRST_SPLIT: dr_ring(fb, st, x - s * 0.22f, y, s * 0.32f, 1.6f, ink, 255); dr_ring(fb, st, x + s * 0.22f, y, s * 0.32f, 1.6f, ink, 255); break;
    case BADGE_GEN10: text_c(fb, st, x, (int)(y - 7), 2, ink, "10"); break;
    case BADGE_GEN50: text_c(fb, st, x, (int)(y - 7), 2, ink, "50"); break;
    case BADGE_CROWD: text_c(fb, st, x, (int)(y - 3), 1, ink, "100"); break;
    case BADGE_COMEBACK: dr_disc(fb, st, x, y, s * 0.3f, ink, 255); dr_ring(fb, st, x, y, s * 0.5f, 1.2f, ink, 200); break;
    case BADGE_SURVIVOR:
        for (int k = 0; k < 6; k++) { float a = k * 1.0472f, a2 = a + 1.0472f; dr_line(fb, st, x + cosf(a) * s * 0.4f, y + sinf(a) * s * 0.4f, x + cosf(a2) * s * 0.4f, y + sinf(a2) * s * 0.4f, 1.6f, ink, 255); }
        dr_line(fb, st, x - s * 0.45f, y + s * 0.45f, x + s * 0.45f, y - s * 0.45f, 2, 0xFF4E5E, got ? 255 : 0);
        break;
    case BADGE_RESIST: dr_ring(fb, st, x, y, s * 0.32f, 1.4f, ink, 255); for (int k = 0; k < 6; k++) dr_disc(fb, st, x + cosf(k * 1.0472f) * s * 0.55f, y + sinf(k * 1.0472f) * s * 0.55f, 1.6f, ink, 255); break;
    case BADGE_SHELL: dr_ring(fb, st, x, y, s * 0.3f, 1.4f, ink, 255); dr_ring(fb, st, x, y, s * 0.5f, 2, ink, 255); break;
    case BADGE_FIRE: dr_disc(fb, st, x, y, s * 0.45f, got ? 0xFF4A20 : ink, 255); dr_glow(fb, st, x, y - s * 0.2f, s * 0.35f, 0xFFE070, got ? 255 : 0); break;
    case BADGE_ICE: dr_disc(fb, st, x, y, s * 0.45f, got ? 0x5AC8FF : ink, 255); dr_glow(fb, st, x - s * 0.15f, y - s * 0.15f, s * 0.3f, 0xFFFFFF, got ? 255 : 0); break;
    case BADGE_SPORES: for (int k = 0; k < 3; k++) dr_disc(fb, st, x + (k - 1) * s * 0.36f, y + (k == 1 ? -s * 0.2f : s * 0.12f), s * 0.18f, ink, 255); break;
    case BADGE_ZOO: for (int k = 0; k < 5; k++) dr_disc(fb, st, x + cosf(k * 1.2566f - 1.5708f) * s * 0.45f, y + sinf(k * 1.2566f - 1.5708f) * s * 0.45f, s * 0.16f, got ? dr_hsv(k / 5.0f, 0.6f, 1) : ink, 255); break;
    }
}
static void draw_dex(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    panel(fb, st, PX, PY, PW, PH);
    text_c(fb, st, DISH_CX, PY + 14, 3, 0xFFFFFF, "MICROBE-DEX");
    const genome_t base = dish_species_genome((species_t)d->strain[DISH_PET].species);
    for (int i = 0; i < SP_COUNT + FORM_COUNT; i++) {
        rect_t r = tile(PAGE_DEX, i);
        bool seen = i < SP_COUNT ? (d->dex & DEX_SPECIES(i)) != 0 : (d->dex & DEX_FORM(i - SP_COUNT)) != 0;
        float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
        dr_round_rect(fb, st, r.x, r.y, r.w, r.h, 10, i == u->sel ? 0x7FF4FF : 0x2A3A58, 255);
        dr_round_rect(fb, st, r.x + 2, r.y + 2, r.w - 4, r.h - 4, 9, 0x03050A, 255);
        if (!seen) { text_c(fb, st, cx, (int)cy - 7, 2, 0x3A4860, "?"); continue; }
        if (i < SP_COUNT) {
            genome_t g = dish_species_genome((species_t)i);
            dr_cell_icon(fb, st, (species_t)i, &g, cx, cy, i == SP_PHAGE ? 2.6f : 2.0f, -0.5f, u->clock, false, r.w * 0.5f - 3);
        } else {
            genome_t g = base;
            switch (i - SP_COUNT) { case FORM_WARM: g.heat_opt = 0.85f; break; case FORM_ICY: g.heat_opt = 0.15f; break;
                                    case FORM_SHELL: g.salt_tol = 0.95f; break; default: g.drug_res = 0.8f; }
            dr_cell_icon(fb, st, (species_t)d->strain[DISH_PET].species, &g, cx, cy, 2.0f, -0.5f, u->clock, false, r.w * 0.5f - 3);
        }
    }
    if (u->sel >= 0) {
        int i = u->sel;
        bool seen = i < SP_COUNT ? (d->dex & DEX_SPECIES(i)) != 0 : (d->dex & DEX_FORM(i - SP_COUNT)) != 0;
        const char *name = !seen ? "NOT FOUND YET" : i < SP_COUNT ? SPECIES_NAMES[i] : FORM_NAMES[i - SP_COUNT];
        const char *says = !seen ? (i < SP_COUNT ? "ADD IT FROM THE LAB, OR WAIT..." : "RAISE A STRAIN TO EVOLVE IT.") : i < SP_COUNT ? SPECIES_SAYS[i] : FORM_SAYS[i - SP_COUNT];
        text_c(fb, st, DISH_CX, PY + 182, 2, 0xFFFFFF, name);
        text_c(fb, st, DISH_CX, PY + 204, 1, 0xB8E8D8, says);
    } else text_c(fb, st, DISH_CX, PY + 192, 1, 0x5C7598, "TAP ONE TO READ ABOUT IT");
    button(fb, st, page_done(), 0x10304A, 0x5CC8E0, "BACK", 2);
}
static void draw_badges(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    panel(fb, st, PX, PY, PW, PH);
    text_c(fb, st, DISH_CX, PY + 14, 3, 0xFFFFFF, "BADGES");
    for (int i = 0; i < BADGE_COUNT; i++) {
        rect_t r = tile(PAGE_BADGES, i);
        float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
        bool got = d->badges >> i & 1;
        if (d->badges_new >> i & 1) dr_ring(fb, st, cx, cy, r.w * 0.5f + 1, 1.6f, 0xFFE07A, (int)(160 + 90 * sinf(u->clock * 5)));
        if (i == u->sel) dr_ring(fb, st, cx, cy, r.w * 0.5f + 1, 1.6f, 0x7FF4FF, 255);
        badge_glyph(fb, st, i, cx, cy, r.w * 0.5f - 4, got);
    }
    if (u->sel >= 0) {
        bool got = d->badges >> u->sel & 1;
        text_c(fb, st, DISH_CX, PY + 166, 2, got ? 0xFFFFFF : 0x8FA0B8, BADGE_NAMES[u->sel]);
        text_c(fb, st, DISH_CX, PY + 188, 1, got ? 0xB8E8D8 : 0x5C7598, BADGE_SAYS[u->sel]);
    } else {
        int n = 0; for (int i = 0; i < BADGE_COUNT; i++) n += d->badges >> i & 1;
        char b[32]; snprintf(b, sizeof b, "%d OF %d EARNED", n, BADGE_COUNT);
        text_c(fb, st, DISH_CX, PY + 176, 2, 0xC8D8F0, b);
    }
    button(fb, st, page_done(), 0x10304A, 0x5CC8E0, "BACK", 2);
}
static void draw_setup(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    panel(fb, st, PX, PY, PW, PH);
    switch (u->setup) {
    case 1: {
        text_c(fb, st, DISH_CX, PY + 18, 3, 0xFFFFFF, "POCKET DISH");
        genome_t g = dish_species_genome(SP_BACILLUS);
        dr_glow(fb, st, DISH_CX, PY + 102, 60, 0x2B8CC0, 70);
        dr_cell_icon(fb, st, SP_BACILLUS, &g, DISH_CX, PY + 102, 4, sinf(u->clock) * 0.3f, u->clock, true, 0);
        text_c(fb, st, DISH_CX, PY + 158, 2, 0xC8F4FF, "A TINY WORLD OF GERMS");
        text_c(fb, st, DISH_CX, PY + 180, 1, 0x8FB0D8, "LOOK AFTER YOUR STRAIN AND WATCH IT EVOLVE.");
        button(fb, st, page_done(), 0x103A30, 0x5CE0B0, "START", 2);
        break; }
    case 2:
        text_c(fb, st, DISH_CX, PY + 18, 2, 0xFFFFFF, "PICK YOUR GERM");
        for (int k = 0; k < 3; k++) {
            rect_t r = starter_tile(k);
            genome_t g = dish_species_genome(STARTERS[k]);
            bool on = k == u->starter;
            button(fb, st, r, on ? 0x103A4A : 0x0C1424, on ? 0x7FF4FF : 0x2A3A58, NULL, 2);
            if (on) dr_glow(fb, st, r.x + r.w * 0.5f, r.y + 46, 40, dr_hsv(g.hue, 0.6f, 1), 70);
            dr_cell_icon(fb, st, STARTERS[k], &g, r.x + r.w * 0.5f, r.y + 46, 2.8f, -0.4f, u->clock, on, r.w * 0.5f - 6);
            text_c(fb, st, r.x + r.w * 0.5f, r.y + 88, 1, on ? 0xFFFFFF : 0xA0B4D0, SPECIES_NAMES[STARTERS[k]]);
            text_c(fb, st, r.x + r.w * 0.5f, r.y + 104, 1, on ? 0xB8E8D8 : 0x5C7598, STARTER_SAYS[k]);
        }
        button(fb, st, page_done(), 0x10304A, 0x5CC8E0, "NEXT", 2);
        break;
    case 3: {
        text_c(fb, st, DISH_CX, PY + 18, 2, 0xFFFFFF, "NAME YOUR STRAIN");
        button(fb, st, name_prev(), 0x10203A, 0x5C7598, "<", 3);
        button(fb, st, name_next(), 0x10203A, 0x5C7598, ">", 3);
        rect_t nb = name_box();
        dr_round_rect(fb, st, nb.x, nb.y, nb.w, nb.h, 14, 0x081830, 255);
        dr_glow(fb, st, DISH_CX, nb.y + nb.h * 0.5f, 60, 0x2B8CC0, 50);
        text_c(fb, st, DISH_CX, nb.y + 18, 3, 0xFFFFFF, NAMES[u->name_i]);
        text_c(fb, st, DISH_CX, PY + 158, 1, 0x8FB0D8, "TAP THE NAME FOR A SURPRISE");
        genome_t g = dish_species_genome(STARTERS[u->starter]);
        dr_cell_icon(fb, st, STARTERS[u->starter], &g, DISH_CX, PY + 192, 2.2f, 0, u->clock, true, 0);
        button(fb, st, page_done(), 0x10304A, 0x5CC8E0, "NEXT", 2);
        break; }
    case 4: {
        text_c(fb, st, DISH_CX, PY + 18, 2, 0xFFFFFF, "HOW TO CARE");
        static const char *const TIPS[5] = { "TAP TO FEED", "DRAG TO STIR", "HOLD STILL TO WARM", "TAP A GERM TO ZOOM", "EARN DNA FOR THE LAB" };
        static const int TIP_ICON[5] = { ICON_FEED, ICON_CLEAN, ICON_LAB, ICON_ZOOM, ICON_CARD };
        for (int k = 0; k < 5; k++) {
            int y = PY + 48 + k * 32;
            if (k == 4) dna_glyph(fb, st, PX + 42, y + 7, 9); else icon_glyph(fb, st, TIP_ICON[k], PX + 42, y + 7, 13, 1);
            dr_text(fb, st, PX + 66, y + 1, 2, 0xD8E6F5, TIPS[k]);
        }
        button(fb, st, page_done(), 0x103A30, 0x5CE0B0, "BEGIN", 2);
        break; }
    }
    (void)d;
}
static void draw_card(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    const strain_t *pet = &d->strain[DISH_PET];
    panel(fb, st, PX, PY, PW, PH);
    dr_text(fb, st, PX + 18, PY + 16, 3, 0xFFFFFF, pet->name);
    char b[48];
    snprintf(b, sizeof b, "A %s STRAIN", SPECIES_NAMES[pet->species]);
    dr_text(fb, st, PX + 18, PY + 42, 1, 0x8FB0D8, b);
    /* the portrait: the healthiest cell's genome, alive under glass */
    float px = PX + 66, py = PY + 110;
    dr_disc(fb, st, px, py, 46, 0x03050A, 255);
    dr_glow(fb, st, px, py, 50, 0x2B5C90, 60);
    dr_ring(fb, st, px, py, 46, 1.6f, 0x7FA8D8, 220);
    dr_cell_icon(fb, st, (species_t)pet->species, &pet->vault, px, py, 4.2f, sinf(u->clock * 0.5f) * 0.4f, u->clock, true, 45);
    /* the needs */
    int nx = PX + 132, nw = PX + PW - 18 - (nx + 92);
    for (int i = 0; i < NEED_COUNT; i++) {
        int y = PY + 62 + i * 24;
        dr_text(fb, st, nx, y, 2, 0xC8D8F0, NEED_NAMES[i]);
        bar(fb, st, nx + 92, y + 1, nw, 12, u->needs[i], need_color(u->needs[i]));
    }
    /* what it inherited: the vault genome's traits */
    const genome_t *g = &pet->vault;
    const char *tn[5] = { "SPEED", "SIZE", "HEAT", "SALT", "SHIELD" };
    float tv[5] = { (g->speed - 0.5f) / 1.1f, (g->size - 0.75f) / 0.6f, g->heat_opt, g->salt_tol, fmaxf(g->drug_res, g->phage_res) };
    for (int i = 0; i < 5; i++) {
        int y = PY + 166 + i * 12;
        dr_text(fb, st, PX + 18, y, 1, 0x8FB0D8, tn[i]);
        bar(fb, st, PX + 62, y, 54, 7, tv[i], dr_hsv(g->hue, 0.5f, 1));
    }
    snprintf(b, sizeof b, "CELLS %d   GEN %u", pet->count, (unsigned)pet->max_gen);
    dr_text(fb, st, nx, PY + 186, 2, 0xFFFFFF, b);
    snprintf(b, sizeof b, "%s", pet->count ? DOING[pet->strat.id] : "IN THE SPORE VAULT");
    dr_text(fb, st, nx, PY + 204, 1, 0x7FF4FF, b);
    snprintf(b, sizeof b, "BORN %u  LOST %u", (unsigned)pet->births, (unsigned)pet->deaths);   /* (above the card's buttons) */
    dr_text(fb, st, nx, PY + 216, 1, 0x8FB0D8, b);
    dna_count(fb, st, PX + PW - 46, PY + 20, 2, d->dna);
    button(fb, st, card_btn(0), 0x102A3A, 0x5CC8E0, "DEX", 2);
    button(fb, st, card_btn(1), 0x2A2410, 0xFFC23C, "BADGES", 2);
    if (d->badges_new) { rect_t r = card_btn(1); dr_disc(fb, st, r.x + r.w - 8, r.y + 6, 5, 0xFF4E5E, 255); }
}
/* a dial: - [gradient track with the pet's mark] + */
static void dial(uint16_t *fb, int st, int row, const char *label, float v, float mark, const char *mark_label, uint32_t mark_col, int kind) {
    rect_t m = dial_minus(row), p = dial_plus(row);
    dr_text(fb, st, PX + 16, m.y + 10, 2, 0xC8D8F0, label);
    button(fb, st, m, 0x10203A, 0x5C7598, "-", 3);
    button(fb, st, p, 0x10203A, 0x5C7598, "+", 3);
    int tx = m.x + m.w + 10, tw = p.x - 10 - tx, ty = m.y + 11;
    for (int k = 0; k < tw; k++) {
        float t = k / (float)tw;
        uint32_t c = kind == 0 ? (t < 0.5f ? dr_hsv(0.6f - t * 0.3f, 0.8f, 0.9f) : dr_hsv(0.12f - (t - 0.5f) * 0.24f, 0.85f, 1))   /* cold..hot */
                   : kind == 1 ? dr_hsv(t * 0.78f, 0.8f, 0.95f)                                                                    /* the indicator: red..green..violet */
                   : dr_hsv(0.58f, 0.25f * (1 - t), 0.3f + 0.7f * t);                                                             /* fresh..brine */
        dr_rect(fb, st, tx + k, ty, 1, 12, c, 255);
    }
    float mx = tx + tw * clampf(mark, 0, 1);
    dr_line(fb, st, mx, ty - 5, mx, ty + 17, 2, mark_col, 255);
    text_c(fb, st, mx, ty + 20, 1, mark_col, mark_label);
    dr_disc(fb, st, tx + tw * v, ty + 6, 8, 0xFFFFFF, 255);
    dr_glow(fb, st, tx + tw * v, ty + 6, 15, 0xFFFFFF, 90);
}
static void draw_lab(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    const genome_t *g = &d->strain[DISH_PET].vault;
    panel(fb, st, PX, PY, PW, PH);
    text_c(fb, st, DISH_CX, PY + 14, 3, 0xFFFFFF, "THE LAB");
    dial(fb, st, 0, "HEAT", d->temp, g->heat_opt, "BEST", 0x4CF08C, 0);
    dial(fb, st, 1, "PH", d->ph, g->ph_opt, "BEST", 0x4CF08C, 1);
    dial(fb, st, 2, "SALT", d->salt, 0.25f + g->salt_tol * 0.6f, "TOO MUCH", 0xFF6E7A, 2);
    for (int row = 1; row <= 2; row++) {
        int unl = row == 1 ? UNL_PH : UNL_SALT;
        if (dish_has(d, unl)) continue;
        rect_t m = dial_minus(row), p = dial_plus(row);
        lock_over(fb, st, d, (rect_t){ m.x, m.y, p.x + p.w - m.x, m.h + 12 }, unl);   /* (+12: over the mark's label too) */
    }
    button(fb, st, lab_btn(0), d->light ? 0x4A4010 : 0x101828, d->light ? 0xFFE07A : 0x5C7598, d->light ? "LIGHT" : "DARK", 2);
    button(fb, st, lab_btn(1), d->uv_t > 0 ? 0x3A1060 : 0x1A1030, 0xB07CFF, "UV", 2);
    if (!dish_has(d, UNL_UV)) lock_over(fb, st, d, lab_btn(1), UNL_UV);
    dna_count(fb, st, PX + PW - 40, PY + 20, 2, d->dna);
    button(fb, st, lab_btn(2), 0x103A30, 0x5CE0B0, "GERMS", 2);
    button(fb, st, page_done(), 0x10304A, 0x5CC8E0, "DONE", 2);
    (void)u;
}
static void draw_samples(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    panel(fb, st, PX, PY, PW, PH);
    text_c(fb, st, DISH_CX, PY + 14, 3, 0xFFFFFF, "NEW GERMS");
    for (int k = 0; k < 3; k++) {
        rect_t r = sample_row(k);
        bool here = false;
        for (int s = 0; s < DISH_MAX_STRAINS; s++) if (d->strain[s].used && d->strain[s].species == SAMPLES[k] && d->strain[s].count > 0) here = true;   /* (one that died out can come back) */
        genome_t g = dish_species_genome(SAMPLES[k]);
        button(fb, st, r, here ? 0x0C1424 : 0x102A3A, here ? 0x2A3A50 : dr_hsv(g.hue, 0.5f, 0.9f), NULL, 2);
        dr_disc(fb, st, r.x + 28, r.y + r.h * 0.5f, 20, 0x03050A, 255);
        dr_cell_icon(fb, st, SAMPLES[k], &g, r.x + 28, r.y + r.h * 0.5f, 2.4f, u->clock * 0.3f, u->clock, false, 19);
        dr_text(fb, st, r.x + 58, r.y + 10, 2, here ? 0x5C7598 : 0xFFFFFF, SPECIES_NAMES[SAMPLES[k]]);
        int unl = dish_unl_for_species(SAMPLES[k]);
        if (!here && !dish_has(d, unl)) draw_lock(fb, st, r.x + 64, r.y + 36, UNL_COST[unl], d->dna >= UNL_COST[unl]);
        else dr_text(fb, st, r.x + 58, r.y + 32, 1, here ? 0x5C7598 : 0xB8E8D8, here ? "ALREADY IN THE DISH" : SAMPLE_SAYS[k]);
    }
    button(fb, st, page_done(), 0x10304A, 0x5CC8E0, "BACK", 2);
}
static void draw_meds(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    panel(fb, st, PX, PY, PW, PH);
    text_c(fb, st, DISH_CX, PY + 18, 3, 0xFFFFFF, "MEDICINE");
    rect_t a = meds_drug(), v = meds_virus();
    button(fb, st, a, 0x3A1020, 0xFF5470, NULL, 2);
    icon_glyph(fb, st, ICON_MEDS, a.x + 34, a.y + a.h * 0.5f, 20, 1);
    dr_text(fb, st, a.x + 64, a.y + 16, 2, 0xFFFFFF, "ANTIBIOTIC");
    if (dish_has(d, UNL_DRUG)) dr_text(fb, st, a.x + 64, a.y + 40, 1, 0xFFB0BC, "KILLS BACTERIA. TAP TO DROP.");
    else draw_lock(fb, st, a.x + 70, a.y + 43, UNL_COST[UNL_DRUG], d->dna >= UNL_COST[UNL_DRUG]);
    button(fb, st, v, 0x261040, 0xC07CFF, NULL, 2);
    dr_glow(fb, st, v.x + 34, v.y + v.h * 0.5f, 18, 0xC07CFF, 160);
    dr_ring(fb, st, v.x + 34, v.y + v.h * 0.5f, 10, 2, 0xF0DCFF, 255);
    dr_line(fb, st, v.x + 26, v.y + v.h * 0.5f - 8, v.x + 42, v.y + v.h * 0.5f + 8, 3, 0xFF4E5E, 255);
    dr_text(fb, st, v.x + 64, v.y + 16, 2, 0xFFFFFF, "ANTIVIRAL");
    dr_text(fb, st, v.x + 64, v.y + 40, 1, 0xDCC0FF, "CLEARS THE VIRUSES AT ONCE.");
    button(fb, st, page_done(), 0x10304A, 0x5CC8E0, "DONE", 2);
    (void)u; (void)d;
}

void dish_ui_render(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    const strain_t *pet = &d->strain[DISH_PET];
    char b[48];
    /* the emote over the colony */
    if (u->emote_t > 0 && pet->count > 0 && u->page == PAGE_NONE) {
        float k = fminf(1, u->emote_t * 2) * fminf(1, (3 - u->emote_t) * 4 + 0.01f);
        float x = pet->cx, y = pet->cy - 34 - (3 - u->emote_t) * 4;
        if (y < top_for(40, 6) + 30) y = top_for(40, 6) + 30;
        if (u->emote == NEED_COUNT) {
            for (int h = 0; h < 3; h++) {
                float hx = x + (h - 1) * 16, hy = y - h % 2 * 6, s = 5;
                dr_glow(fb, st, hx - s * 0.5f, hy - s * 0.2f, s * 0.8f, 0xFF5C8A, (int)(255 * k));
                dr_glow(fb, st, hx + s * 0.5f, hy - s * 0.2f, s * 0.8f, 0xFF5C8A, (int)(255 * k));
                dr_glow(fb, st, hx, hy + s * 0.5f, s * 0.7f, 0xFF5C8A, (int)(255 * k));
            }
        } else {
            dr_disc(fb, st, x, y, 17, 0xF4F8FF, (int)(235 * k));
            dr_disc(fb, st, x - 6, y + 18, 4, 0xF4F8FF, (int)(235 * k));
            dr_disc(fb, st, x - 10, y + 25, 2.5f, 0xF4F8FF, (int)(235 * k));
            dr_disc(fb, st, x, y, 15, 0x0A1224, (int)(255 * k));
            icon_glyph(fb, st, icon_for_need(u->emote), x, y, 13, k);
        }
    }
    /* the microscope */
    if (u->lens >= 0 && u->page == PAGE_NONE) {
        const cell_t *c = &d->cell[u->lens];
        float lx, ly, lr; lens_geom(d, u->lens, &lx, &ly, &lr);
        dr_line(fb, st, c->x, c->y, lx + (c->x - lx) * lr / fmaxf(1, hypot2(c->x - lx, c->y - ly)),
                ly + (c->y - ly) * lr / fmaxf(1, hypot2(c->x - lx, c->y - ly)), 1, 0x7FA8D8, 90);
        dr_ring(fb, st, c->x, c->y, dish_cell_radius(c) * 2.6f, 1, 0x7FF4FF, 160);
        dish_render_lens(d, c->x, c->y, u->lens_zoom, fb, st, lx, ly, lr);
        const char *who = c->strain == DISH_PET ? pet->name : SPECIES_NAMES[c->species];
        snprintf(b, sizeof b, "%s  GEN %u", who, (unsigned)c->gen);
        int w = dr_text_w(b, 1) + 14;
        dr_round_rect(fb, st, (int)(lx - w / 2), (int)(ly + lr - 18), w, 13, 6, 0x0A1224, 220);
        text_c(fb, st, lx, (int)(ly + lr - 15), 1, 0xC8F4FF, b);
    }
    /* the top: the pet's name, its mood, and the DNA to spend */
    if (u->page == PAGE_NONE && !u->setup) {
        int ty = (int)top_for(150, 6) + 4 + (d->uv_t > 0 ? 14 : 0);   /* (out from under the UV lamp) */
        snprintf(b, sizeof b, "%s", pet->name);
        int w = dr_text_w(b, 2);
        float fx = DISH_CX + w * 0.5f + 14;
        text_c(fb, st, DISH_CX - 8, ty, 2, 0xE8F2FF, b);
        uint32_t mc = need_color(u->mood);
        dr_glow(fb, st, fx, ty + 7, 11, mc, 110);
        dr_disc(fb, st, fx, ty + 7, 7, mc, 255);
        dr_disc(fb, st, fx - 2.5f, ty + 5.5f, 1.2f, 0x06060A, 255);
        dr_disc(fb, st, fx + 2.5f, ty + 5.5f, 1.2f, 0x06060A, 255);
        float cv = (u->mood - 0.5f) * 3;
        dr_line(fb, st, fx - 3, ty + 9 - cv * 0.2f, fx, ty + 9 + cv, 1, 0x06060A, 255);
        dr_line(fb, st, fx, ty + 9 + cv, fx + 3, ty + 9 - cv * 0.2f, 1, 0x06060A, 255);
        dna_count(fb, st, DISH_CX, ty + 20, 1, d->dna);
    }
    /* the icon arc */
    if (u->page == PAGE_NONE && !u->setup) {
        int w = worst_need(u);
        icon_t alert = u->needs[w] < 0.3f && pet->count > 0 ? icon_for_need(w) : ICON_COUNT;
        for (int i = 0; i < ICON_COUNT; i++) {
            float x, y; dish_ui_icon_pos((icon_t)i, &x, &y);
            bool active = (i == ICON_CLEAN && u->tool == TOOL_WIPE) || (i == ICON_MEDS && u->tool == TOOL_DRUG) || (i == ICON_ZOOM && u->lens >= 0);
            draw_icon(fb, st, i, x, y, active, i == (int)alert ? 1 : 0, u->clock);
        }
    }
    /* a tool in hand */
    if (u->tool != TOOL_NONE && u->page == PAGE_NONE) {
        rect_t r = tool_chip();
        button(fb, st, r, 0x10304A, 0x7FF4FF, "DONE", 2);
        text_c(fb, st, DISH_CX, r.y + r.h + 6, 1, 0x7FF4FF, u->tool == TOOL_DRUG ? "TAP TO DROP MEDICINE" : "RUB TO CLEAN THE DISH");
    }

    /* extinct: the vault counts down */
    if (pet->count == 0 && pet->extinct_t > 0) {
        dr_rect(fb, st, 0, 0, DISH_W, DISH_H, 0x000000, 120);
        float k = 0.6f + 0.4f * sinf(u->clock * 3);
        dr_glow(fb, st, DISH_CX, DISH_CY, 70, dr_hsv(pet->vault.hue, 0.5f, 1), (int)(110 * k));
        dr_cell_icon(fb, st, (species_t)pet->species, &pet->vault, DISH_CX, DISH_CY, 3.2f, 0, u->clock * 0.2f, false, 0);
        snprintf(b, sizeof b, "%s DIED OUT", pet->name);
        text_c(fb, st, DISH_CX, (int)(DISH_CY - 90), 3, 0xFFFFFF, b);
        text_c(fb, st, DISH_CX, (int)(DISH_CY - 60), 1, 0x8FB0D8, "THE SPORE VAULT IS WAKING IT UP");
        snprintf(b, sizeof b, "%d", (int)ceilf(pet->extinct_t));
        text_c(fb, st, DISH_CX, (int)(DISH_CY + 54), 4, dr_hsv(pet->vault.hue, 0.4f, 1), b);
    }
    switch (u->page) {
    case PAGE_CARD: draw_card(u, d, fb, st); break;
    case PAGE_LAB:  draw_lab(u, d, fb, st); break;
    case PAGE_MEDS: draw_meds(u, d, fb, st); break;
    case PAGE_SAMPLES: draw_samples(u, d, fb, st); break;
    case PAGE_DEX:  draw_dex(u, d, fb, st); break;
    case PAGE_BADGES: draw_badges(u, d, fb, st); break;
    default: break;
    }
    if (u->setup) draw_setup(u, d, fb, st);
    /* a toast: over the dish, or just under the page when one is up */
    if (u->toast_t > 0 && u->extinct_show <= 0) {
        bool paged = u->page != PAGE_NONE || u->setup;
        float a = fminf(1, u->toast_t * 3);
        int w = dr_text_w(u->toast, 2) + 24, y = paged ? PY + PH + 14 : (int)(DISH_CY - DISH_RY * 0.55f) + (u->tool != TOOL_NONE ? 44 : 0);
        if (paged && y + 22 > DISH_H) y = PY - 34;
        dr_round_rect(fb, st, (int)(DISH_CX - w / 2), y - 8, w, 30, 15, 0x0A1224, (int)(230 * a));
        text_c(fb, st, DISH_CX, y, 2, 0xFFFFFF, u->toast);
    }
}
