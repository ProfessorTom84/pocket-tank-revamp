/* dish_ui.c — the pet layer: see dish_ui.h. */
#include "dish_ui.h"
#include "dish_render.h"
#include "dish_prog.h"
#include "dish_story.h"
#include "sounds.h"
#include "dish_mut.h"
#include "dish_talk.h"
#include "dish_quest.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TAU 6.2831853f
/* a goal as words, with and without the strain it is about */
static const char *const DOING[ST_COUNT] = { "LOOKING FOR FOOD", "HUDDLING UP", "EXPLORING", "RESTING", "SPLITTING IN TWO", "TURNING TO SPORES",
                                             "CHASING THE LIGHT", "HIDING FROM THE LIGHT", "HUNTING", "FLEEING", "GUARDING", "MAKING FRIENDS" };
static const char *const DOING_TO[ST_COUNT] = { [ST_HUNT] = "HUNTING", [ST_FLEE] = "FLEEING FROM", [ST_GUARD] = "GUARDING", [ST_BEFRIEND] = "MAKING FRIENDS WITH" };

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float hypot2(float x, float y) { return sqrtf(x * x + y * y); }

/* ---- layout ---- */
/* the highest row where a thing w px wide, centred, sits m px inside the glass (the round board's top is narrow) */
static float top_for(float w, float m) {
    float y = m;
    while (y < DISH_CY && !(dish_inside(DISH_CX - w * 0.5f, y, m) && dish_inside(DISH_CX + w * 0.5f, y, m))) y += 2;
    return y;
}
/* the corner buttons: STORY top left, DISH top right, WALL bottom left - as far into the corners as the glass allows */
enum { BTN_STORY, BTN_DISH, BTN_WALL, BTN_COUNT };
static void corner_pos(int k, float *x, float *y) {
    bool right = k == BTN_DISH, bottom = k == BTN_WALL;
    float t = DISH_BTN_R + 6;
    while (t < DISH_CY && !dish_inside(right ? DISH_W - t : t, bottom ? DISH_H - t - 10 : t, DISH_BTN_R + 4)) t += 1;
    *x = right ? DISH_W - t : t; *y = bottom ? DISH_H - t - 10 : t;   /* (the bottom one sits higher: its label goes under it) */
}
static int corner_hit(float x, float y) {
    for (int k = 0; k < BTN_COUNT; k++) { float cx, cy; corner_pos(k, &cx, &cy); if (hypot2(x - cx, y - cy) < DISH_BTN_R + 6) return k; }
    return -1;
}
#define PW (DISH_W - 48 < 340 ? DISH_W - 48 : 340)
#define PH (DISH_H - 80 < 272 ? DISH_H - 80 : 272)
#define PX ((int)(DISH_CX - PW / 2))
#define PY ((int)(DISH_CY - PH / 2))
typedef struct { int x, y, w, h; } rect_t;
static bool in_rect(rect_t r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
/* the banner: what the colony asks for, or the wave (under the name and the DNA) */
static rect_t quest_pill(void) { int w = DISH_W - 2 * (DISH_BTN_R * 2 + 20) < 330 ? DISH_W - 2 * (DISH_BTN_R * 2 + 20) : 330; return (rect_t){ (int)DISH_CX - w / 2, (int)top_for(150, 6) + 36, w, 30 }; }
static rect_t page_done(void) { return (rect_t){ (int)DISH_CX - 60, PY + PH - 54, 120, 40 }; }
/* the card's three buttons; the dex's and the badges' grids (6 x 3) */
static rect_t card_btn(int k) { int w = (PW - 52) / 3; return (rect_t){ PX + 18 + k * (w + 8), PY + PH - 42, w, 30 }; }
static rect_t tile(page_t p, int i) { (void)p; int c = i % 6, r = i / 6; return (rect_t){ (int)DISH_CX - 140 + c * 48, PY + 44 + r * 46, 40, 40 }; }
void dish_ui_tile(page_t page, int i, float *x, float *y) { rect_t r = tile(page, i); *x = r.x + r.w * 0.5f; *y = r.y + r.h * 0.5f; }
/* the story: two tabs and BACK along the top */
static rect_t story_btn(int k) { int w = (PW - 40) / 3; return (rect_t){ PX + 12 + k * (w + 8), PY + 12, w, 26 }; }
/* the setup: six starters (three to unlock with DNA), the name roller */
#define N_STARTERS 6
static const species_t STARTERS[N_STARTERS] = { SP_BACILLUS, SP_COCCUS, SP_SPIRILLUM, SP_CYANO, SP_VIBRIO, SP_MYXO };
static const char *const STARTER_SAYS[N_STARTERS] = { "A FAST SWIMMER WITH A WHIPPY TAIL", "ROUND BEADS THAT STICK TOGETHER", "A CORKSCREW THAT LOVES IT WARM",
                                                      "MAKES ITS OWN FOOD FROM LIGHT", "A TINY HUNTER: EATS OTHER GERMS", "HUNTS IN PACKS, HIDES IN SPORES" };
static const char *const NAMES[] = { "BLIP", "DOT", "ZIGGY", "MOSS", "PIP", "GLOOP", "NIX", "TOFU", "BEAN", "ZAP", "FIZZ", "WOBBLE",
                                     "SPECK", "NOODLE", "JELLY", "ORBIT", "PICO", "BUBBA", "SQUIGGLE", "MOCHI", "PEBBLE", "SPROUT", "GIZMO", "TWIRL" };
#define N_NAMES ((int)(sizeof NAMES / sizeof NAMES[0]))
static rect_t starter_tile(int k) { int w = (PW - 48) / 3; return (rect_t){ PX + 16 + (k % 3) * (w + 8), PY + 42 + (k / 3) * 74, w, 68 }; }
static rect_t name_prev(void) { return (rect_t){ PX + 20, PY + 92, 48, 48 }; }
static rect_t name_next(void) { return (rect_t){ PX + PW - 68, PY + 92, 48, 48 }; }
static rect_t name_box(void)  { return (rect_t){ PX + 76, PY + 88, PW - 152, 56 }; }
/* the draft: three cards across */
static rect_t draft_card(int k) { int w = (PW - 40) / 3; return (rect_t){ PX + 12 + k * (w + 8), PY + 44, w, 168 }; }
void dish_ui_hotspot(hotspot_t h, float *x, float *y) {
    rect_t r;
    switch (h) {
    case HS_STORY: corner_pos(BTN_STORY, x, y); return;
    case HS_DISH: corner_pos(BTN_DISH, x, y); return;
    case HS_WALL: corner_pos(BTN_WALL, x, y); return;
    case HS_QUEST: r = quest_pill(); break;
    case HS_CARD_DEX: r = card_btn(0); break;     case HS_CARD_BADGES: r = card_btn(1); break;
    case HS_CARD_NEWRUN: r = card_btn(2); break;
    case HS_STARTER_0: case HS_STARTER_1: case HS_STARTER_2: case HS_STARTER_3: case HS_STARTER_4: case HS_STARTER_5:
        r = starter_tile(h - HS_STARTER_0); break;
    case HS_NAME_PREV: r = name_prev(); break;    case HS_NAME_NEXT: r = name_next(); break;
    case HS_NAME: r = name_box(); break;
    case HS_DRAFT_0: case HS_DRAFT_1: case HS_DRAFT_2: r = draft_card(h - HS_DRAFT_0); break;
    case HS_TAB_JOURNAL: r = story_btn(0); break; case HS_TAB_CHARTS: r = story_btn(1); break;
    case HS_STORY_BACK: r = story_btn(2); break;
    default: r = page_done(); break;
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
    u->mood = 0.8f; u->lens = -1; u->lens_zoom = 5; u->sel = -1; u->prev_lens = -1; u->tap_t = 9; u->on_button = -1;
    u->down_cell = -1; u->wall_i = -1; u->chat_cd = 8; u->hint_cd = 4; u->info_x = -1;
    for (int k = 0; k < DISH_BUBBLES; k++) u->bub[k].strain = -1;
}
static void sfx(dish_ui_t *u, int cue, int pitch, float cd);
void dish_ui_begin_setup(dish_ui_t *u) { u->setup = 1; u->page = PAGE_NONE; u->lens = -1; sfx(u, SND_WELCOME, 256, 0); }
bool dish_ui_paused(const dish_ui_t *u) { return u->page == PAGE_DRAFT && !u->setup; }
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
static const char *strain_name(const dish_t *d, int s) { return s == DISH_PET ? d->strain[DISH_PET].name : SPECIES_NAMES[d->strain[s].species]; }
void dish_ui_thought(const dish_t *d, int s, char *out, int n) {
    const strain_t *st = &d->strain[s];
    int g = st->strat.id, t = st->strat.target, sure = (int)(clampf(st->strat.confidence, 0, 1) * 100 + 0.5f);
    char who[16], whom[16];
    snprintf(who, sizeof who, "%s", strain_name(d, s));
    for (char *c = who; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 'a' - 'A';
    if (ST_TARGETED(g) && t >= 0 && t < DISH_MAX_STRAINS && d->strain[t].used) {
        snprintf(whom, sizeof whom, "%s", strain_name(d, t));
        for (char *c = whom; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 'a' - 'A';
        snprintf(out, (size_t)n, t == DISH_PET ? "%s: %s %s  SURE %d%%" : "%s: %s THE %s  SURE %d%%", who, DOING_TO[g], whom, sure);
    } else snprintf(out, (size_t)n, "%s: %s  SURE %d%%", who, DOING[g], sure);
}
/* ---- talk ---- */
/* the cell a strain speaks from: the one given (if it lives), else the one nearest its colony's middle */
static int speaker_cell(const dish_t *d, int s, uint32_t id) {
    int best = -1; float bd = 1e18f;
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        const cell_t *c = &d->cell[i];
        if (c->strain != s || (c->state != CS_LIVE && c->state != CS_SPORE)) continue;
        if (id && c->id == id) return i;
        float dd = (c->x - d->strain[s].cx) * (c->x - d->strain[s].cx) + (c->y - d->strain[s].cy) * (c->y - d->strain[s].cy);
        if (dd < bd) { bd = dd; best = i; }
    }
    return best;
}
static void say_from(dish_ui_t *u, const dish_t *d, int s, uint32_t cell_id, int emoji, const char *text, float delay, float ttl) {
    if (u->setup || s < 0 || s >= DISH_MAX_STRAINS || !d->strain[s].used || !text[0]) return;
    int k = 0;
    for (int i = 0; i < DISH_BUBBLES; i++) if (u->bub[i].strain == s) { k = i; goto put; }   /* one bubble per strain: the new line replaces */
    for (int i = 0; i < DISH_BUBBLES; i++) if (u->bub[i].strain < 0 || u->bub[i].t >= u->bub[i].ttl) { k = i; goto put; }
    for (int i = 1; i < DISH_BUBBLES; i++) if (u->bub[i].ttl - u->bub[i].t < u->bub[k].ttl - u->bub[k].t) k = i;   /* (or the one nearest its end) */
put:
    u->bub[k] = (dish_bubble_t){ (int8_t)s, (uint8_t)emoji, cell_id, 0, ttl, delay, "" };
    snprintf(u->bub[k].text, sizeof u->bub[k].text, "%s", text);
    if (s == DISH_PET) u->chat_cd = 14;
}
void dish_ui_say(dish_ui_t *u, const dish_t *d, int s, int emoji, const char *text, float delay) {
    int n = (int)strlen(text);
    say_from(u, d, s, 0, emoji, text, delay, 2.2f + n * 0.06f);   /* (longer lines stay longer) */
}
/* the strain says what its mind decided to say, to `about` (the name in the line) */
static void say_mind(dish_ui_t *u, const dish_t *d, int s, int about, uint32_t cell_id, float delay) {
    char line[72];
    const char *t = about >= 0 && about < DISH_MAX_STRAINS && d->strain[about].used ? strain_name(d, about) : "";
    int e = dish_say_text(d->strain[s].strat.say, dish_say_variant(d, s), t, line, sizeof line);
    say_from(u, d, s, cell_id, e, line, delay, 2.2f + strlen(line) * 0.06f);
}
static void info(dish_ui_t *u, const char *text, float x, float y, float seconds) {
    snprintf(u->info, sizeof u->info, "%s", text);
    u->info_t = seconds; u->info_x = x; u->info_y = y;
}
/* a tap on a bubble: who that is, and what it is up to */
static const char *const GOAL_HELP[ST_COUNT] = { "LOOKING FOR FOOD.", "HUDDLING TOGETHER FOR SAFETY.", "WANDERING OFF TO FIND NEW GROUND.",
    "RESTING TO SAVE ENERGY.", "SPLITTING IN TWO TO GROW.", "TURNING INTO TOUGH SPORES TO SURVIVE.", "COMING TO YOUR LIGHT.",
    "HIDING FROM YOUR LIGHT.", "HUNTING THE %s.", "RUNNING AWAY FROM THE %s.", "GUARDING THE %s FROM DANGER.", "MAKING FRIENDS WITH THE %s." };
static void explain(dish_ui_t *u, const dish_t *d, int s) {
    const strain_t *st = &d->strain[s];
    char doing[64], b[128];
    const char *t = st->strat.target >= 0 && st->strat.target < DISH_MAX_STRAINS && d->strain[st->strat.target].used ? strain_name(d, st->strat.target) : "OTHERS";
    snprintf(doing, sizeof doing, GOAL_HELP[st->strat.id < ST_COUNT ? st->strat.id : 0], t);
    const char *role = s == DISH_PET ? "YOUR GERMS" : SPECIES_FRIENDLY[st->species] ? "A FRIENDLY VISITOR" : "A DANGEROUS VISITOR";
    snprintf(b, sizeof b, "%s, %s. RIGHT NOW: %s", strain_name(d, s), role, doing);
    info(u, b, -1, 0, 5);
}
int dish_ui_bubble_at(const dish_ui_t *u, const dish_t *d, float x, float y);

void dish_ui_event(dish_ui_t *u, const dish_t *d, int ev, int strain) {
    const strain_t *pet = &d->strain[DISH_PET];
    char b[64];
    bool pet_ev = strain == DISH_PET;
    switch (ev) {   /* the sound first: the bank's cues at the dish's pitches */
    case DEV_DIVIDE:   if (pet_ev) sfx(u, SND_BUBBLES, 340, 0.7f); break;
    case DEV_POP:      if (pet_ev) sfx(u, SND_TAP, 170, 0.35f); break;
    case DEV_BURST:    sfx(u, SND_SPOOK, 300, 0.6f); break;
    case DEV_INFECT:   if (pet_ev) sfx(u, SND_ERROR, 200, 2); break;
    case DEV_EXTINCT:  sfx(u, SND_SLEEP, 256, 0); break;
    case DEV_RESPAWN:  sfx(u, SND_ARRIVAL, 256, 0); break;
    case DEV_OUTBREAK: sfx(u, SND_SPOOK, 200, 0); break;
    case DEV_BADGE:    sfx(u, SND_MILESTONE, 256, 0); break;
    case DEV_VISIT:    sfx(u, SPECIES_FRIENDLY[strain] ? SND_ARRIVAL : SND_SPOOK, SPECIES_FRIENDLY[strain] ? 300 : 220, 0); break;
    case DEV_GIFT:     sfx(u, SND_MILESTONE, 330, 0); break;
    case DEV_DRAFT:    sfx(u, SND_STAGE_UP, 256, 0); break;
    case DEV_RUN_OVER: sfx(u, SND_SLEEP, 200, 0); break;
    case DEV_ZAP:      sfx(u, SND_SNIP, 420, 0.5f); break;
    case DEV_MEET:     sfx(u, SND_BUBBLES, 420, 1.5f); break;
    case DEV_QUEST:    sfx(u, strain == QS_WON ? SND_MILESTONE : strain == QS_ON ? SND_BEG : SND_ERROR, 300, 0); break;
    case DEV_WAVE:     sfx(u, strain == 1 ? SND_SPOOK : strain == 3 ? SND_STAGE_UP : SND_ERROR, strain == 1 ? 180 : 256, 0); break;
    case DEV_BLOOM:    sfx(u, SND_MILESTONE, 380, 0); break;
    }
    switch (ev) {
    case DEV_EXTINCT: u->extinct_show = 1; u->lens = -1; if (u->page != PAGE_DRAFT) u->page = PAGE_NONE; break;
    case DEV_RESPAWN: u->extinct_show = 0; u->back_show = 3.5f; snprintf(b, sizeof b, d->vault_used ? "LAST CHANCE: %s IS BACK" : "%s IS BACK!", pet->name); dish_ui_toast(u, b, 4); break;
    case DEV_RUN_OVER: u->lens = -1; u->page = PAGE_RUNOVER; break;
    case DEV_VISIT: {
        snprintf(b, sizeof b, SPECIES_FRIENDLY[strain] ? "A FRIEND: %s ARRIVED" : "DANGER: %s ARRIVED", SPECIES_NAMES[strain]);
        dish_ui_toast(u, b, 4);
        for (int s = 1; s < DISH_MAX_STRAINS; s++)      /* the newcomer introduces itself */
            if (d->strain[s].used && d->strain[s].species == strain) {
                char l[72]; int e = dish_say_text(SPECIES_FRIENDLY[strain] ? SAY_HELLO : SAY_TAUNT, dish_say_variant(d, s), pet->name, l, sizeof l);
                say_from(u, d, s, 0, e, l, 0.6f, 3.5f);
            }
        break; }
    case DEV_MEET: {                                  /* two strains met: the one with something to say first, the other answers */
        int a = strain / 8, c = strain % 8;
        if (a >= DISH_MAX_STRAINS || c >= DISH_MAX_STRAINS || !d->strain[a].used || !d->strain[c].used) break;
        int first = ST_TARGETED(d->strain[c].strat.id) && d->strain[c].strat.target == a ? c : a, second = first == a ? c : a;
        if (first == DISH_PET && second != DISH_PET && !ST_TARGETED(d->strain[DISH_PET].strat.id)) { first = second; second = DISH_PET; }
        int fc = speaker_cell(d, first, 0), sc = speaker_cell(d, second, 0);
        say_mind(u, d, first, second, fc >= 0 ? d->cell[fc].id : 0, 0);
        say_mind(u, d, second, first, sc >= 0 ? d->cell[sc].id : 0, 1.4f);
        break; }
    case DEV_QUEST:
        if (strain == QS_ON) { char l[96]; dish_quest_ask(d, l, sizeof l); dish_ui_say(u, d, DISH_PET, EMO_EXCLAIM, l, 0); }
        else if (strain == QS_WON) { snprintf(b, sizeof b, "THANK YOU, GIANT! +%d DNA", d->quest.reward); dish_ui_say(u, d, DISH_PET, EMO_STAR, b, 0); }
        else if (strain == QS_LOST) dish_ui_say(u, d, DISH_PET, EMO_SAD, "OH WELL... MAYBE NEXT TIME.", 0);
        break;
    case DEV_WAVE:
        if (strain == 1) dish_ui_say(u, d, DISH_PET, EMO_SCARED, "SOMETHING'S COMING! GET READY!", 0);
        else if (strain == 3) {
            const strain_t *p = &d->strain[DISH_PET];
            bool held = p->count > 0 && (int)(uint16_t)(p->deaths - d->wave.lost0) <= 6;
            dish_ui_say(u, d, DISH_PET, held ? EMO_STAR : EMO_SAD, held ? "WE HELD THEM OFF! YOU'RE AMAZING!" : "THAT WAS ROUGH...", 0);
        }
        break;
    case DEV_VISIT_GONE:
        if (u->toast_t <= 0) { snprintf(b, sizeof b, "THE %s LEFT", SPECIES_NAMES[strain]); dish_ui_toast(u, b, 3); }
        break;
    case DEV_GIFT: dish_ui_toast(u, "GENE GIFT: A FREE MUTATION!", 4); break;
    case DEV_OUTBREAK: dish_ui_toast(u, "VIRUSES ARE COMING!", 4); break;
    case DEV_BADGE: snprintf(b, sizeof b, "BADGE: %s  +%d DNA", BADGE_NAMES[strain], BADGE_DNA); dish_ui_toast(u, b, 4); break;
    case DEV_BLOOM: if (pet_ev) dish_ui_say(u, d, DISH_PET, EMO_SPARKLE, "LOOK AT US GLOW! WE'RE BLOOMING!", 0); break;
    case DEV_ZAP: if (pet_ev) { char l[72]; int e = dish_say_text(SAY_ZAP, dish_say_variant(d, DISH_PET), "", l, sizeof l); dish_ui_say(u, d, DISH_PET, e, l, 0); } break;
    case DEV_INFECT:
        if (pet_ev && u->toast_t <= 0) { snprintf(b, sizeof b, "%s CAUGHT A VIRUS", pet->name); dish_ui_toast(u, b, 3); }
        break;
    case DEV_STRAT:                                  /* the pet changed its mind: it says so */
        if (pet->count) say_mind(u, d, DISH_PET, pet->strat.target, 0, 0);
        break;
    case DEV_DIVIDE:
        if (pet_ev && (pet->births == 1 || pet->births % 25 == 0) && u->toast_t <= 0) {
            snprintf(b, sizeof b, pet->births == 1 ? "%s SPLIT IN TWO!" : "%s IS GROWING!", pet->name); dish_ui_toast(u, b, 3);
        }
        break;
    }
}

/* ---- first-time hints: each thing explained once, when it first matters ---- */
static bool hint(dish_ui_t *u, dish_t *d, int h, const char *text, float x, float y) {
    if (d->hints >> h & 1) return false;
    d->hints |= 1u << h;
    info(u, text, x, y, 7);
    u->hint_cd = 9;
    return true;
}
static void hints_tick(dish_ui_t *u, dish_t *d, float dt) {
    if (u->info_t > 0 || (u->hint_cd -= dt) > 0) return;
    const strain_t *pet = &d->strain[DISH_PET];
    float bx, by;
    int threat = -1, friend = -1;
    for (int s = 1; s < DISH_MAX_STRAINS; s++)
        if (d->strain[s].used && d->strain[s].count) { if (SPECIES_FRIENDLY[d->strain[s].species]) friend = s; else threat = s; }
    if (hint(u, d, HINT_LIGHT, "HOLD YOUR FINGER ON THE DISH TO SHINE LIGHT. FOOD GROWS WHERE IT SHINES.", pet->cx, pet->cy)) return;
    for (int k = 0; k < DISH_BUBBLES; k++)
        if (u->bub[k].strain >= 0 && u->bub[k].delay <= 0 && hint(u, d, HINT_TALK, "GERMS SAY WHAT THEY THINK. TAP A BUBBLE TO FIND OUT WHO IT IS.", -1, 0)) return;
    if (d->quest.state == QS_ON) { rect_t r = quest_pill(); if (hint(u, d, HINT_QUEST, "YOUR GERMS ASKED FOR SOMETHING! DO IT FOR DNA. TAP THE BANNER TO SEE HOW.", r.x + r.w * 0.5f, r.y + r.h)) return; }
    if (d->wave.on && hint(u, d, HINT_WAVE, "A WAVE IS COMING! PROTECT YOUR GERMS WITH CURRENTS, WALLS AND FLICKS.", -1, 0)) return;
    if (threat >= 0) {
        corner_pos(BTN_WALL, &bx, &by);
        if (hint(u, d, HINT_WALL, "TAP THE PENCIL, THEN DRAW A LINE: A WALL NO GERM CAN CROSS.", bx, by - DISH_BTN_R)) return;
        if (hint(u, d, HINT_FLICK, "TOUCH A GERM AND SWIPE FAST TO FLICK IT ACROSS THE DISH.", d->strain[threat].cx, d->strain[threat].cy)) return;
    }
    if ((threat >= 0 || friend >= 0) && hint(u, d, HINT_VISITOR, "A VISITOR! TAP ONE OF ITS GERMS TO SEE WHAT IT'S UP TO.", d->strain[threat >= 0 ? threat : friend].cx, d->strain[threat >= 0 ? threat : friend].cy)) return;
    if (d->run_t > 50 && hint(u, d, HINT_POKE, "TAP ONE OF YOUR GERMS TO SAY HI. IT WILL ANSWER!", pet->cx, pet->cy)) return;
    if (d->run_t > 90 && hint(u, d, HINT_TILT, "TILT THE DISH AND EVERYTHING SLIDES DOWNHILL. DOUBLE-TAP FOR DAY OR NIGHT.", -1, 0)) return;
    corner_pos(BTN_DISH, &bx, &by);
    if (d->run_t > 150 && hint(u, d, HINT_DISH, "THE DISH BUTTON SHOWS YOUR STRAIN, ITS BADGES AND EVERY GERM YOU'VE MET.", bx, by + DISH_BTN_R)) return;
    corner_pos(BTN_STORY, &bx, &by);
    if (d->run_t > 240 && hint(u, d, HINT_STORY, "THE BOOK TELLS THE STORY OF YOUR DISH, WITH CHARTS.", bx, by + DISH_BTN_R)) return;
}

void dish_ui_tick(dish_ui_t *u, dish_t *d, float dt) {
    u->clock += dt;
    for (int k = 0; k < 32; k++) if (u->snd_cd[k] > 0) u->snd_cd[k] -= dt;
    const strain_t *pet = &d->strain[DISH_PET];
    if (d->run_over && !u->setup) { u->page = PAGE_RUNOVER; u->lens = -1; }
    /* a draft is due: it fills the screen, and the dish waits for the pick */
    else if (d->drafts && !u->setup && pet->count > 0 && u->page != PAGE_DRAFT) {
        dish_draft_deal(d);
        if (d->offer[0] != 0xFF) { u->page = PAGE_DRAFT; u->lens = -1; }
        else d->drafts = 0;                          /* (nothing left to offer: a strain at the top tier of everything) */
    }
    if ((int)u->page != u->prev_page) {              /* a page opening, flipping or closing */
        sfx(u, u->page == PAGE_NONE ? SND_CARD_CLOSE : SND_CARD_OPEN, 256, 0);
        u->prev_page = u->page;
    }
    if ((u->lens >= 0) != (u->prev_lens >= 0)) { if (u->lens >= 0) sfx(u, SND_INVESTIGATE, 256, 0); u->prev_lens = u->lens; }
    if (pet->count > 0) {                            /* the faces' mood: fed, well, comfortable, room to grow */
        float t = 0.35f * clampf((pet->energy - 0.2f) / 0.55f, 0, 1) + 0.3f * pet->health * (1 - pet->infected) + 0.2f * pet->comfort + 0.15f * (1 - pet->crowd);
        u->mood += (t - u->mood) * clampf(dt * 1.5f, 0, 1);
    }
    dish_render_mood = u->mood;
    if (u->toast_t > 0) u->toast_t -= dt;
    if (u->back_show > 0) u->back_show -= dt;
    if (u->confirm_t > 0) u->confirm_t -= dt;
    if (pet->extinct_t <= 0) u->extinct_show = 0;
    /* the lens follows its cell, and closes when the cell is gone */
    if (u->lens >= 0) {
        const cell_t *c = &d->cell[u->lens];
        if ((c->state != CS_LIVE && c->state != CS_SPORE) || c->id != u->lens_id) u->lens = -1;
    }
    if (u->down) u->press_t += dt;
    /* the bubbles age; the pet speaks its mind now and then, so there is always a voice in the dish */
    for (int k = 0; k < DISH_BUBBLES; k++) {
        dish_bubble_t *b = &u->bub[k];
        if (b->strain < 0) continue;
        if (b->delay > 0) b->delay -= dt; else b->t += dt;
        if (b->t >= b->ttl || !d->strain[b->strain].used || !d->strain[b->strain].count) b->strain = -1;
    }
    if (u->page == PAGE_NONE && !u->setup && pet->count && (u->chat_cd -= dt) <= 0) {
        u->chat_cd = 16 + 6 * sinf(u->clock);
        say_mind(u, d, DISH_PET, pet->strat.target, 0, 0);
    }
    if (u->info_t > 0) u->info_t -= dt;
    if (u->page == PAGE_NONE && !u->setup && pet->count) hints_tick(u, d, dt);
    /* a finger held on the agar is light: food blooms under it, and the strains decide what to make of it */
    if (u->down && !u->dragged && !u->walling && u->page == PAGE_NONE && !u->setup && u->on_button < 0 && dish_inside(u->dx0, u->dy0, 4)) {
        u->down_t += dt;
        if (u->down_t > 0.25f) {
            if (!u->lighting) sfx(u, SND_LIGHT_ON, 330, 1.5f);
            u->lighting = true;
            dish_light(d, u->lx, u->ly, dt);
        }
    }
    u->tap_t += dt;
}

/* ---- touch ---- */
static void open_lens(dish_ui_t *u, const dish_t *d, int cell) {
    if (cell < 0) return;
    u->lens = cell; u->lens_id = d->cell[cell].id;
}
/* a locked thing: with enough DNA the tap unlocks it; without, it says what it costs. True only when already owned. */
static bool gate(dish_ui_t *u, dish_t *d, int unl) {
    if (dish_has(d, unl)) return true;
    char b[48];
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
        for (int k = 0; k < N_STARTERS; k++)
            if (in_rect(starter_tile(k), x, y) && gate(u, d, dish_unl_for_species(STARTERS[k]))) { u->starter = k; sfx(u, SND_TAP, 256 + k * 30, 0); }
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
            char b[48];
            dish_new_run(d, (uint32_t)(u->clock * 1000) * 2654435761u | 1, STARTERS[u->starter], NAMES[u->name_i]);   /* (DNA, badges, dex, unlocks stay) */
            u->page = PAGE_NONE;
            u->setup = 0;
            u->mood = 0.8f;
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
        else if (in_rect(card_btn(2), x, y)) {
            if (u->confirm_t <= 0) { u->confirm_t = 3; sfx(u, SND_TAP, 200, 0); return; }   /* a second tap to be sure */
            u->confirm_t = 0;
            dish_story_note(d, SE_RUN_OVER, d->strain[DISH_PET].species, 0, 0, (uint16_t)d->strain[DISH_PET].max_gen);
            d->run_over = 1;                         /* (so the next run is numbered on) */
            dish_ui_begin_setup(u);
            u->setup = 2;
        }
        else u->page = PAGE_NONE;
        break;
    case PAGE_DEX: case PAGE_BADGES: {
        int n = u->page == PAGE_DEX ? SP_COUNT + FORM_COUNT : BADGE_COUNT;
        for (int i = 0; i < n; i++) if (in_rect(tile(u->page, i), x, y)) { u->sel = i; return; }
        if (in_rect(page_done(), x, y)) { if (u->page == PAGE_BADGES) d->badges_new = 0; u->page = PAGE_CARD; }
        break; }
    case PAGE_STORY:
        if (in_rect(story_btn(0), x, y)) { u->story_tab = 0; sfx(u, SND_TAP, 256, 0); }
        else if (in_rect(story_btn(1), x, y)) { u->story_tab = 1; sfx(u, SND_TAP, 256, 0); }
        else if (in_rect(story_btn(2), x, y)) u->page = PAGE_NONE;
        break;
    case PAGE_DRAFT:                                 /* only a card: there is no LATER */
        for (int k = 0; k < 3; k++)
            if (in_rect(draft_card(k), x, y) && d->offer[k] != 0xFF) {
                int m = d->offer[k];
                if (!dish_draft_take(d, k)) return;
                char b[64], f[40]; dish_form_name(d, DISH_PET, f, sizeof f);
                snprintf(b, sizeof b, "%s: %s", MUTS[m].name, f);
                u->page = PAGE_NONE;
                dish_ui_toast(u, b, 4);
                sfx(u, MUTS[m].rarity == MR_EPIC ? SND_MILESTONE : SND_STAGE_UP, 256, 0);
                return;
            }
        break;
    case PAGE_RUNOVER:
        if (in_rect(page_done(), x, y)) { dish_ui_begin_setup(u); u->setup = 2; }   /* NEW RUN: pick a starter */
        break;
    default: u->page = PAGE_NONE;
    }
}
static void tap(dish_ui_t *u, dish_t *d, float x, float y) {
    if (u->setup) { setup_tap(u, d, x, y); return; }
    if (u->page != PAGE_NONE) { page_tap(u, d, x, y); return; }
    int b = corner_hit(x, y);
    if (b == BTN_STORY) { u->page = PAGE_STORY; u->story_top = 0; u->lens = -1; return; }
    if (b == BTN_DISH) { u->page = PAGE_CARD; u->confirm_t = 0; u->lens = -1; return; }
    if (b == BTN_WALL) {
        if (u->walling) { u->walling = false; return; }
        if (d->ink < 1) { info(u, "NO WALLS LEFT JUST NOW: ONE MORE EVERY 12 SECONDS.", -1, 0, 3); sfx(u, SND_ERROR, 256, 0.3f); return; }
        u->walling = true; sfx(u, SND_CONFIRM, 300, 0);
        info(u, "DRAW A LINE ON THE DISH: IT BECOMES A WALL FOR 20 SECONDS.", -1, 0, 4);
        return;
    }
    if (u->info_t > 0) {                             /* a tap on the explanation puts it away */
        int w = DISH_W - 60, h = 64;
        if (x > DISH_CX - w / 2 && x < DISH_CX + w / 2 && y > DISH_H - h - 48 && y < DISH_H - 24) { u->info_t = 0; return; }
    }
    int bb = dish_ui_bubble_at(u, d, x, y);
    if (bb >= 0) { explain(u, d, u->bub[bb].strain); sfx(u, SND_TAP, 300, 0); return; }
    if ((d->quest.state != QS_NONE || d->wave.on) && in_rect(quest_pill(), x, y)) {
        char t[48]; dish_quest_text(d, t, sizeof t);
        if (d->wave.on) info(u, "A WAVE OF GERMS IS ATTACKING! DRAG CURRENTS, DRAW WALLS AND FLICK THEM AWAY. LOSE 6 OR FEWER TO WIN.", -1, 0, 6);
        else if (d->quest.state == QS_ON) { char b2[128]; snprintf(b2, sizeof b2, "%s: %s", t, dish_quest_help(d->quest.kind)); info(u, b2, -1, 0, 6); }
        sfx(u, SND_TAP, 300, 0);
        return;
    }
    /* a double-tap: day or night */
    bool dbl = u->tap_t < 0.35f && hypot2(x - u->tap_x, y - u->tap_y) < 40;
    u->tap_t = 0; u->tap_x = x; u->tap_y = y;
    if (dbl) {
        d->light = !d->light; u->lens = -1; u->tap_t = 9;
        sfx(u, d->light ? SND_LIGHT_ON : SND_LIGHT_OFF, 256, 0);
        dish_ui_toast(u, d->light ? "LAMP ON: DAYTIME" : "LAMP OFF: NIGHT", 2);
        return;
    }
    if (u->lens >= 0) {                              /* a tap on the lens puts it away */
        float lx, ly, lr; lens_geom(d, u->lens, &lx, &ly, &lr);
        if (hypot2(x - lx, y - ly) < lr) { u->lens = -1; return; }
    }
    int c = dish_cell_at(d, x, y, 8);
    if (c >= 0) {                                    /* a poke: it jumps, it answers, and the lens shows it */
        dish_poke(d, c); open_lens(u, d, c);
        char l[72]; int e = dish_poke_text(d, d->cell[c].strain, false, l, sizeof l);
        say_from(u, d, d->cell[c].strain, d->cell[c].id, e, l, 0, 2.5f);
        sfx(u, SND_TAP, 380, 0);
    } else u->lens = -1;
}
void dish_ui_touch(dish_ui_t *u, dish_t *d, float x, float y, bool down) {
    bool glass = u->page == PAGE_NONE && !u->setup;
    if (down && !u->down) {
        u->down = true; u->dx0 = u->lx = x; u->dy0 = u->ly = y;
        u->down_t = u->press_t = 0; u->dragged = u->lighting = false; u->scroll_acc = 0;
        u->on_button = glass ? corner_hit(x, y) : -1;
        u->down_cell = glass && u->on_button < 0 && !u->walling ? dish_cell_at(d, x, y, 10) : -1;
        u->down_id = u->down_cell >= 0 ? d->cell[u->down_cell].id : 0;
        u->wall_i = glass && u->walling && u->on_button < 0 ? dish_wall_begin(d, x, y) : -1;
        return;
    }
    if (down) {
        if (!u->lighting && hypot2(x - u->dx0, y - u->dy0) > 10) u->dragged = true;
        if (u->wall_i >= 0) dish_wall_add(d, u->wall_i, x, y);   /* drawing a wall */
        else if (u->dragged && glass && u->on_button < 0 && !(u->down_cell >= 0 && u->press_t < 0.3f))   /* (a swipe off a germ may yet be a flick) */
            dish_stir(d, x, y, (x - u->lx) * 1.6f, (y - u->ly) * 1.6f);   /* a current */
        if (u->dragged && u->page == PAGE_STORY && u->story_tab == 0) {   /* the journal scrolls, an entry per 16 px */
            u->scroll_acc += u->ly - y;
            while (u->scroll_acc > 16) { u->scroll_acc -= 16; if (u->story_top < dish_story_count(d) - 1) u->story_top++; }
            while (u->scroll_acc < -16) { u->scroll_acc += 16; if (u->story_top > 0) u->story_top--; }
        }
        u->lx = x; u->ly = y;
        return;
    }
    if (!u->down) return;
    u->down = false;
    if (u->wall_i >= 0) {                           /* a wall drawn: the button disarms */
        if (d->wall[u->wall_i].n < 2) d->wall[u->wall_i].t = 0;   /* (a dot is no wall: it is given back) */
        if (d->wall[u->wall_i].n < 2) d->ink = fminf(DISH_WALLS, d->ink + 1);
        else sfx(u, SND_WIPE, 300, 0);
        u->wall_i = -1; u->walling = false; u->lighting = false;
        return;
    }
    float dx = x - u->dx0, dy = y - u->dy0, dd = hypot2(dx, dy);
    int c = u->down_cell;
    if (u->dragged && c >= 0 && u->press_t < 0.35f && dd > 25 && d->cell[c].id == u->down_id && d->cell[c].state == CS_LIVE) {   /* a flick */
        float t = fmaxf(0.06f, u->press_t);
        dish_flick(d, c, dx / t * 0.9f, dy / t * 0.9f);
        char l[72]; int e = dish_poke_text(d, d->cell[c].strain, true, l, sizeof l);
        say_from(u, d, d->cell[c].strain, d->cell[c].id, e, l, 0, 2.2f);
        sfx(u, SND_SPOOK, 420, 0.2f);
    } else if (!u->dragged && !u->lighting) tap(u, d, x, y);
    u->lighting = false; u->down_cell = -1;
}

/* ---- drawing ---- */
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
static const char *const SPECIES_SAYS[SP_COUNT] = {
    "ROUND BEADS THAT STICK IN BUNCHES.", "A ROD WITH A WHIPPY TAIL. FAST!", "A CORKSCREW THAT SPINS TO SWIM.",
    "GREEN CHAINS THAT EAT LIGHT.", "A HUNTER THAT EATS OTHER GERMS.", "A VIRUS: IT BURSTS GERMS FROM INSIDE.",
    "A MOLD: IT LEAKS ANTIBIOTIC.", "A TINY, FAST HUNTER.", "IT MAKES THE DISH SOUR.", "IT MAKES THE DISH SALTY.",
    "A FRIEND: IT LEAVES FOOD BEHIND.", "A GUARDIAN: IT EATS VIRUSES AND HUNTERS.", "A FRIEND: ITS GENES ARE A GIFT." };
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
    case BADGE_RUN100: for (int k = -1; k <= 0; k++) { dr_line(fb, st, x + k * s * 0.3f - s * 0.1f, y - s * 0.3f, x + k * s * 0.3f + s * 0.15f, y, 1.8f, ink, 255); dr_line(fb, st, x + k * s * 0.3f + s * 0.15f, y, x + k * s * 0.3f - s * 0.1f, y + s * 0.3f, 1.8f, ink, 255); } break;
    case BADGE_VISITORS5: text_c(fb, st, x, (int)(y - 7), 2, ink, "5"); break;
    case BADGE_EPIC: for (int k = 0; k < 5; k++) { float a = -1.5708f + k * 2.5133f, a2 = a + 2.5133f; dr_line(fb, st, x + cosf(a) * s * 0.5f, y + sinf(a) * s * 0.5f, x + cosf(a2) * s * 0.5f, y + sinf(a2) * s * 0.5f, 1.4f, ink, 255); } break;
    case BADGE_BUILD12: text_c(fb, st, x, (int)(y - 7), 2, ink, "12"); break;
    case BADGE_FRIENDS: dr_disc(fb, st, x - s * 0.18f, y - s * 0.08f, s * 0.22f, ink, 255); dr_disc(fb, st, x + s * 0.18f, y - s * 0.08f, s * 0.22f, ink, 255);
                        dr_line(fb, st, x - s * 0.36f, y + s * 0.02f, x, y + s * 0.38f, s * 0.2f, ink, 255); dr_line(fb, st, x + s * 0.36f, y + s * 0.02f, x, y + s * 0.38f, s * 0.2f, ink, 255); break;
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
        uint32_t edge = i == u->sel ? 0x7FF4FF : i < SP_COUNT && SPECIES_FRIENDLY[i] ? 0x2A5848 : 0x2A3A58;
        dr_round_rect(fb, st, r.x, r.y, r.w, r.h, 9, edge, 255);
        dr_round_rect(fb, st, r.x + 2, r.y + 2, r.w - 4, r.h - 4, 8, 0x03050A, 255);
        if (!seen) { text_c(fb, st, cx, (int)cy - 7, 2, 0x3A4860, "?"); continue; }
        if (i < SP_COUNT) {
            genome_t g = dish_species_genome((species_t)i);
            float sc = i == SP_PHAGE ? 2.0f : i == SP_CILIATE ? 1.0f : i == SP_MOLD ? 1.6f : 1.5f;
            dr_cell_icon(fb, st, (species_t)i, &g, NULL, cx, cy, sc, -0.5f, u->clock, false, r.w * 0.5f - 3);
        } else {
            genome_t g = base;
            switch (i - SP_COUNT) { case FORM_WARM: g.heat_opt = 0.85f; break; case FORM_ICY: g.heat_opt = 0.15f; break;
                                    case FORM_SHELL: g.salt_tol = 0.95f; break; default: g.drug_res = 0.8f; }
            dr_cell_icon(fb, st, (species_t)d->strain[DISH_PET].species, &g, NULL, cx, cy, 1.5f, -0.5f, u->clock, false, r.w * 0.5f - 3);
        }
    }
    if (u->sel >= 0) {
        int i = u->sel;
        bool seen = i < SP_COUNT ? (d->dex & DEX_SPECIES(i)) != 0 : (d->dex & DEX_FORM(i - SP_COUNT)) != 0;
        const char *name = !seen ? "NOT FOUND YET" : i < SP_COUNT ? SPECIES_NAMES[i] : FORM_NAMES[i - SP_COUNT];
        const char *says = !seen ? (i < SP_COUNT ? "IT MAY VISIT YOUR DISH ONE DAY..." : "RAISE A STRAIN TO EVOLVE IT.") : i < SP_COUNT ? SPECIES_SAYS[i] : FORM_SAYS[i - SP_COUNT];
        text_c(fb, st, DISH_CX, PY + 186, 2, 0xFFFFFF, name);
        text_c(fb, st, DISH_CX, PY + 206, 1, 0xB8E8D8, says);
    } else text_c(fb, st, DISH_CX, PY + 196, 1, 0x5C7598, "TAP ONE TO READ ABOUT IT");
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
        badge_glyph(fb, st, i, cx, cy, r.w * 0.5f - 3, got);
    }
    if (u->sel >= 0) {
        bool got = d->badges >> u->sel & 1;
        text_c(fb, st, DISH_CX, PY + 186, 2, got ? 0xFFFFFF : 0x8FA0B8, BADGE_NAMES[u->sel]);
        text_c(fb, st, DISH_CX, PY + 206, 1, got ? 0xB8E8D8 : 0x5C7598, BADGE_SAYS[u->sel]);
    } else {
        int n = 0; for (int i = 0; i < BADGE_COUNT; i++) n += d->badges >> i & 1;
        char b[32]; snprintf(b, sizeof b, "%d OF %d EARNED", n, BADGE_COUNT);
        text_c(fb, st, DISH_CX, PY + 192, 2, 0xC8D8F0, b);
    }
    button(fb, st, page_done(), 0x10304A, 0x5CC8E0, "BACK", 2);
}
/* a few words over lines of at most `w` px (scale 1), centred on cx or (left) from x; fb NULL only counts the lines */
static int wrap_text(uint16_t *fb, int st, float x, int y, int w, uint32_t rgb, const char *str, bool left) {
    char line[64]; int n = 0, lines = 0, per = w / 6;
    if (per > 62) per = 62;
    const char *p = str;
    while (*p) {
        const char *sp = strchr(p, ' '); int wl = sp ? (int)(sp - p) : (int)strlen(p);
        if (n && n + 1 + wl > per) {
            line[n] = 0;
            if (fb) { if (left) dr_text(fb, st, (int)x, y + lines * 10, 1, rgb, line); else text_c(fb, st, x, y + lines * 10, 1, rgb, line); }
            lines++; n = 0;
        }
        if (n) line[n++] = ' ';
        for (int k = 0; k < wl && n < 62; k++) line[n++] = p[k];
        p += wl; while (*p == ' ') p++;
    }
    if (n) {
        line[n] = 0;
        if (fb) { if (left) dr_text(fb, st, (int)x, y + lines * 10, 1, rgb, line); else text_c(fb, st, x, y + lines * 10, 1, rgb, line); }
        lines++;
    }
    return lines;
}
static int text_wrap(uint16_t *fb, int st, float cx, int y, int w, uint32_t rgb, const char *str) { return wrap_text(fb, st, cx, y, w, rgb, str, false); }

static void draw_runover(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    const strain_t *pet = &d->strain[DISH_PET];
    panel(fb, st, PX, PY, PW, PH);
    char b[48], f[32];
    snprintf(b, sizeof b, "RUN %d IS OVER", d->run);
    text_c(fb, st, DISH_CX, PY + 14, 2, 0xFFFFFF, b);
    dr_disc(fb, st, PX + 70, PY + 100, 46, 0x03050A, 255);
    dr_glow(fb, st, PX + 70, PY + 100, 50, dr_hsv(pet->vault.hue, 0.5f, 1), 50);
    dr_cell_icon(fb, st, (species_t)pet->species, &pet->vault, pet->mut, PX + 70, PY + 100, 3.6f, sinf(u->clock * 0.4f) * 0.3f, u->clock, false, 45);
    dish_form_name(d, DISH_PET, f, sizeof f);
    text_wrap(fb, st, PX + 70, PY + 154, 120, 0xC8F4FF, f);
    int x = PX + 140;
    snprintf(b, sizeof b, "%s LASTED %u GENERATIONS", pet->name, (unsigned)pet->max_gen);
    text_wrap(fb, st, x + (PW - 156) * 0.5f, PY + 52, PW - 156, 0xFFFFFF, b);
    snprintf(b, sizeof b, "MUTATIONS  %d", d->run_muts);           dr_text(fb, st, x, PY + 90, 1, 0xB8D0EE, b);
    snprintf(b, sizeof b, "THREATS SEEN OFF  %d", d->run_visitors);  dr_text(fb, st, x, PY + 106, 1, 0xB8D0EE, b);
    snprintf(b, sizeof b, "DNA EARNED  %u", (unsigned)(d->dna - d->run_dna0)); dr_text(fb, st, x, PY + 122, 1, 0xB8D0EE, b);
    text_wrap(fb, st, x + (PW - 156) * 0.5f, PY + 150, PW - 156, 0x8FB0D8, "YOUR DNA, BADGES AND DEX STAY. A NEW GERM WAITS.");
    button(fb, st, page_done(), 0x103A30, 0x5CE0B0, "NEW RUN", 2);
}
/* the tips' little drawings: light, current, tilt, look, lamp */
static void tip_glyph(uint16_t *fb, int st, int k, float x, float y) {
    switch (k) {
    case 0: dr_glow(fb, st, x, y, 14, 0xFFE6A8, 200); dr_disc(fb, st, x, y, 5, 0xFFFFFF, 255); break;
    case 1: for (int m = -1; m <= 1; m++) { dr_line(fb, st, x - 11, y + m * 6, x + 7, y + m * 6, 2, 0x7FE8FF, 220); dr_line(fb, st, x + 7, y + m * 6, x + 3, y + m * 6 - 3, 2, 0x7FE8FF, 220); } break;
    case 2: dr_round_rect(fb, st, (int)x - 10, (int)y - 8, 20, 16, 4, 0x5C7598, 255); dr_line(fb, st, x - 6, y + 12, x + 8, y + 12, 2, 0xFFC23C, 255);
            dr_line(fb, st, x + 8, y + 12, x + 4, y + 9, 2, 0xFFC23C, 255); break;
    case 3: dr_ring(fb, st, x - 2, y - 2, 7, 2, 0xD8E6F5, 255); dr_line(fb, st, x + 3, y + 3, x + 10, y + 10, 3, 0xD8E6F5, 255); break;
    default: dr_disc(fb, st, x, y, 8, 0xFFE07A, 255); dr_disc(fb, st, x + 4, y - 3, 7, 0x0A1224, 255); break;
    }
}
static void draw_setup(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    panel(fb, st, PX, PY, PW, PH);
    switch (u->setup) {
    case 1: {
        text_c(fb, st, DISH_CX, PY + 18, 3, 0xFFFFFF, "POCKET DISH");
        genome_t g = dish_species_genome(SP_BACILLUS);
        dr_glow(fb, st, DISH_CX, PY + 102, 60, 0x2B8CC0, 70);
        dr_cell_icon(fb, st, SP_BACILLUS, &g, NULL, DISH_CX, PY + 102, 4, sinf(u->clock) * 0.3f, u->clock, true, 0);
        text_c(fb, st, DISH_CX, PY + 158, 2, 0xC8F4FF, "A TINY WORLD OF GERMS");
        text_c(fb, st, DISH_CX, PY + 180, 1, 0x8FB0D8, "EVERY GERM THINKS FOR ITSELF. WATCH THEM EVOLVE.");
        button(fb, st, page_done(), 0x103A30, 0x5CE0B0, "START", 2);
        break; }
    case 2:
        text_c(fb, st, DISH_CX, PY + 16, 2, 0xFFFFFF, d->run > 1 || d->run_over ? "PICK YOUR NEXT GERM" : "PICK YOUR GERM");
        for (int k = 0; k < N_STARTERS; k++) {
            rect_t r = starter_tile(k);
            genome_t g = dish_species_genome(STARTERS[k]);
            int unl = dish_unl_for_species(STARTERS[k]);
            bool on = k == u->starter, locked = !dish_has(d, unl);
            button(fb, st, r, on ? 0x103A4A : 0x0C1424, on ? 0x7FF4FF : 0x2A3A58, NULL, 2);
            if (on) dr_glow(fb, st, r.x + r.w * 0.5f, r.y + 28, 30, dr_hsv(g.hue, 0.6f, 1), 70);
            dr_cell_icon(fb, st, STARTERS[k], &g, NULL, r.x + r.w * 0.5f, r.y + 28, 1.9f, -0.4f, u->clock, on, 24);
            if (locked) { dr_round_rect(fb, st, r.x + 2, r.y + 2, r.w - 4, r.h - 4, 10, 0x0A1224, 200); draw_lock(fb, st, r.x + r.w * 0.5f - 22, r.y + 28, UNL_COST[unl], d->dna >= UNL_COST[unl]); }
            text_c(fb, st, r.x + r.w * 0.5f, r.y + r.h - 13, 1, on ? 0xFFFFFF : 0xA0B4D0, SPECIES_NAMES[STARTERS[k]]);
        }
        text_c(fb, st, DISH_CX, PY + 196, 1, 0xB8E8D8, STARTER_SAYS[u->starter]);
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
        dr_cell_icon(fb, st, STARTERS[u->starter], &g, NULL, DISH_CX, PY + 192, 2.2f, 0, u->clock, true, 0);
        button(fb, st, page_done(), 0x10304A, 0x5CC8E0, "NEXT", 2);
        break; }
    case 4: {
        text_c(fb, st, DISH_CX, PY + 18, 2, 0xFFFFFF, "THE GLASS IS YOURS");
        static const char *const TIPS[5] = { "HOLD: LIGHT, FOOD GROWS", "DRAG: MAKE A CURRENT", "TILT: TIP THE DISH", "TAP A GERM: ITS THOUGHTS", "DOUBLE-TAP: DAY OR NIGHT" };
        for (int k = 0; k < 5; k++) {
            int y = PY + 48 + k * 32;
            tip_glyph(fb, st, k, PX + 36, y + 7);
            dr_text(fb, st, PX + 60, y + 1, 2, 0xD8E6F5, TIPS[k]);
        }
        button(fb, st, page_done(), 0x103A30, 0x5CE0B0, "BEGIN", 2);
        break; }
    }
}
static void pips(uint16_t *fb, int st, float cx, float y, int have, int next) {
    for (int q = 0; q < MUT_TIERS; q++) {
        float qx = cx + (q - (MUT_TIERS - 1) * 0.5f) * 12;
        dr_disc(fb, st, qx, y, 3.5f, q < have ? 0x7FA8D8 : q == next ? 0xFFE07A : 0x2A3448, 255);
    }
}
static void draw_card(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    const strain_t *pet = &d->strain[DISH_PET];
    panel(fb, st, PX, PY, PW, PH);
    dr_text(fb, st, PX + 18, PY + 16, 3, 0xFFFFFF, pet->name);
    char b[96];                                   /* (room for the longest form name, count and run: GCC checks) */
    { char f[40]; dish_form_name(d, DISH_PET, f, sizeof f); int nm = dish_mut_total(d, DISH_PET);
      if (nm) snprintf(b, sizeof b, "%s  RUN %d", f, d->run);
      else snprintf(b, sizeof b, "A %s STRAIN  RUN %d", SPECIES_NAMES[pet->species], d->run); }
    dr_text(fb, st, PX + 18, PY + 42, 1, 0x8FB0D8, b);
    /* the portrait: the vault's genome and the build, alive under glass */
    float px = PX + 66, py = PY + 110;
    dr_disc(fb, st, px, py, 46, 0x03050A, 255);
    dr_glow(fb, st, px, py, 50, 0x2B5C90, 60);
    dr_ring(fb, st, px, py, 46, 1.6f, 0x7FA8D8, 220);
    dr_cell_icon(fb, st, (species_t)pet->species, &pet->vault, pet->mut, px, py, 4.2f, sinf(u->clock * 0.5f) * 0.4f, u->clock, true, 45);
    /* what it can do: the strongest mutations and their tiers */
    int nx = PX + 132, t[4], k = dish_top_traits(pet->mut, t, 4);
    dr_text(fb, st, nx, PY + 62, 1, 0x5C7598, "WHAT IT CAN DO");
    if (!k) dr_text(fb, st, nx, PY + 78, 1, 0x8FA0B8, "NOTHING SPECIAL YET: EVOLVE IT!");
    for (int i = 0; i < k; i++) {
        int y = PY + 76 + i * 22;
        dr_text(fb, st, nx, y, 1, 0xFFFFFF, MUTS[t[i]].name);
        pips(fb, st, PX + PW - 44, y + 3, pet->mut[t[i]], -1);
        dr_text(fb, st, nx, y + 10, 1, 0x8FB0D8, MUTS[t[i]].says[pet->mut[t[i]] - 1]);
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
    dr_text(fb, st, nx, PY + 168, 2, 0xFFFFFF, b);
    if (pet->count) { char th[64]; dish_ui_thought(d, DISH_PET, th, sizeof th); wrap_text(fb, st, nx, PY + 188, PX + PW - 18 - nx, 0x7FF4FF, strchr(th, ':') + 2, true); }
    else dr_text(fb, st, nx, PY + 188, 1, 0x7FF4FF, "IN THE SPORE VAULT");
    snprintf(b, sizeof b, "BORN %u  LOST %u", (unsigned)pet->births, (unsigned)pet->deaths);
    dr_text(fb, st, nx, PY + 212, 1, 0x8FB0D8, b);
    dna_count(fb, st, PX + PW - 46, PY + 20, 2, d->dna);
    button(fb, st, card_btn(0), 0x102A3A, 0x5CC8E0, "DEX", 2);
    button(fb, st, card_btn(1), 0x2A2410, 0xFFC23C, "BADGES", 2);
    button(fb, st, card_btn(2), u->confirm_t > 0 ? 0x4A1020 : 0x201020, 0xFF6E7A, u->confirm_t > 0 ? "SURE?" : "NEW RUN", u->confirm_t > 0 ? 2 : 1);
    if (d->badges_new) { rect_t r = card_btn(1); dr_disc(fb, st, r.x + r.w - 8, r.y + 6, 5, 0xFF4E5E, 255); }
}
static void draw_draft(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    panel(fb, st, PX, PY, PW, PH);
    char b[40];
    text_c(fb, st, DISH_CX, PY + 16, 2, 0xFFFFFF, "EVOLVE: PICK ONE");
    float p[P_COUNT]; dish_pressures(d, p);
    int worst = 0; for (int i = 1; i < P_COUNT; i++) if (p[i] > p[worst]) worst = i;
    static const uint32_t RIM[3] = { 0x5C7598, 0x5CE0F0, 0xFF6EF0 };
    static const char *const RAR[3] = { "COMMON", "RARE", "EPIC" };
    for (int k = 0; k < 3; k++) {
        int m = d->offer[k];
        if (m == 0xFF) continue;
        rect_t r = draft_card(k);
        const mut_info_t *mi = &MUTS[m];
        float cx = r.x + r.w * 0.5f;
        int have = d->strain[DISH_PET].mut[m];
        if (mi->rarity == MR_EPIC) dr_glow(fb, st, cx, r.y + r.h * 0.5f, r.w * 0.8f, 0xFF6EF0, (int)(60 + 30 * sinf(u->clock * 3)));
        dr_round_rect(fb, st, r.x - 1, r.y - 1, r.w + 2, r.h + 2, 12, RIM[mi->rarity], 255);
        dr_round_rect(fb, st, r.x + 1, r.y + 1, r.w - 2, r.h - 2, 11, 0x0A1428, 255);
        text_c(fb, st, cx, r.y + 6, 1, RIM[mi->rarity], have ? "TIER UP" : RAR[mi->rarity]);
        /* the pet as it would be: its build plus this card */
        uint8_t mu[DISH_MUT_N]; genome_t g; dish_mut_preview(d, m, mu, &g);
        dr_disc(fb, st, cx, r.y + 46, 28, 0x03050A, 255);
        dr_cell_icon(fb, st, (species_t)d->strain[DISH_PET].species, &g, mu, cx, r.y + 46, 2.3f, -0.3f, u->clock, true, 27);
        int sc = dr_text_w(mi->name, 2) <= r.w - 8 ? 2 : 1;
        text_c(fb, st, cx, r.y + 82 + (sc == 1 ? 3 : 0), sc, 0xFFFFFF, mi->name);
        text_wrap(fb, st, cx, r.y + 104, r.w - 8, 0xB8D0EE, mi->says[have < MUT_TIERS ? have : MUT_TIERS - 1]);
        pips(fb, st, cx, r.y + r.h - 26, have, have);   /* owned tiers, and the bright one this card adds */
        if (p[worst] >= 0.3f && (mi->answers >> worst & 1)) {
            snprintf(b, sizeof b, "VS %s", PRESSURE_NAMES[worst]);
            text_c(fb, st, cx, r.y + r.h - 14, 1, 0x4CF08C, b);
        }
    }
    text_c(fb, st, DISH_CX, PY + PH - 20, 1, 0x8FB0D8, "THE DISH WAITS FOR YOUR PICK");
}
/* ---- the story: a journal of what happened, and charts of how it went ---- */
static void draw_journal(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    int n = dish_story_count(d), y = PY + 50, bottom = PY + PH - 12, x = PX + 16, w = PW - 32 - 50;
    if (!n) { text_c(fb, st, DISH_CX, PY + PH / 2, 1, 0x8FB0D8, "NOTHING HAS HAPPENED YET. WATCH THE DISH!"); return; }
    for (int i = u->story_top; i < n; i++) {
        char line[96], mn[24]; int minute = 0;
        if (!dish_story_line(d, i, line, sizeof line, &minute)) break;
        int lines = wrap_text(NULL, st, 0, 0, w, 0, line, true);
        if (y + lines * 10 > bottom) break;
        snprintf(mn, sizeof mn, "MIN %d", minute);
        dr_text(fb, st, x, y, 1, 0x5C7598, mn);
        wrap_text(fb, st, x + 50, y, w, i == 0 ? 0xFFFFFF : 0xC8D8F0, line, true);
        y += lines * 10 + 5;
    }
    if (u->story_top > 0) text_c(fb, st, DISH_CX, PY + 42, 1, 0x5C7598, "- NEWER ABOVE -");
}
typedef struct { uint8_t pet, others, food, births, deaths, cause, goal, guests; } sample_t;
static void draw_charts(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    (void)u;
    int n = dish_story_samples(d);
    int cx0 = PX + 16, cw = PW - 32, cy0 = PY + 52, ch = 66;
    dr_text(fb, st, cx0, PY + 44, 1, 0x8FB0D8, "CELLS, A MINUTE A STEP");
    dr_round_rect(fb, st, cx0 - 2, cy0 + 6, cw + 4, ch + 4, 6, 0x03050A, 255);
    if (n < 2) { text_c(fb, st, DISH_CX, cy0 + 34, 1, 0x5C7598, "A MINUTE MORE AND THE CHARTS BEGIN"); return; }
    int mx = 10;
    int deaths[DIE_COUNT] = { 0 }, goals[ST_COUNT] = { 0 };
    for (int i = 0; i < n; i++) {
        const sample_t *s = dish_story_sample(d, i);
        if (s->pet > mx) mx = s->pet;
        if (s->others > mx) mx = s->others;
        if (s->cause < DIE_COUNT) deaths[s->cause] += s->deaths;
        if (s->goal < ST_COUNT) goals[s->goal]++;
    }
    float step = (float)cw / (n - 1);
    uint32_t petc = dr_hsv(d->strain[DISH_PET].vault.hue, 0.6f, 1);
    for (int i = 0; i < n; i++) {                    /* minutes with a threat in the dish: a red band */
        const sample_t *s = dish_story_sample(d, i);
        if (s->guests & 0x80) dr_rect(fb, st, (int)(cx0 + i * step - step * 0.5f), cy0 + 8, (int)step + 1, ch, 0x601820, 120);
    }
    int now_min = (int)(d->run_t / 60);
    const int LN = (int)(sizeof d->log / sizeof d->log[0]);
    for (int i = 0; i < dish_story_count(d); i++) {  /* the mutations, as marks along the top */
        int at = (d->log_head - 1 - i + LN * 2) % LN;
        if (d->log[at].type != SE_MUT) continue;
        int m = d->log[at].min, k = m - (now_min - n);
        if (k < 0 || k >= n) continue;
        float xx = cx0 + k * step;
        dr_line(fb, st, xx, cy0 + 8, xx, cy0 + 8 + ch, 1, 0xFF6EF0, 90);
        dr_disc(fb, st, xx, cy0 + 9, 2.5f, 0xFF6EF0, 255);
    }
    for (int i = 1; i < n; i++) {
        const sample_t *a = dish_story_sample(d, i - 1), *b = dish_story_sample(d, i);
        float x0 = cx0 + (i - 1) * step, x1 = cx0 + i * step;
        dr_line(fb, st, x0, cy0 + 8 + ch - a->others * ch / (float)mx, x1, cy0 + 8 + ch - b->others * ch / (float)mx, 1.5f, 0xFF8A70, 200);
        dr_line(fb, st, x0, cy0 + 8 + ch - a->pet * ch / (float)mx, x1, cy0 + 8 + ch - b->pet * ch / (float)mx, 2.2f, petc, 255);
    }
    char b[48];
    snprintf(b, sizeof b, "%d", mx); dr_text(fb, st, cx0 + cw - dr_text_w(b, 1), cy0 + 10, 1, 0x5C7598, b);
    dr_text(fb, st, cx0, cy0 + ch + 14, 1, petc, d->strain[DISH_PET].name);
    dr_text(fb, st, cx0 + 80, cy0 + ch + 14, 1, 0xFF8A70, "OTHERS");
    dr_text(fb, st, cx0 + 140, cy0 + ch + 14, 1, 0xFF6EF0, "MUTATIONS");
    /* how they died, two columns of three */
    int y = cy0 + ch + 30, dmax = 1;
    for (int k = 0; k < DIE_COUNT; k++) if (deaths[k] > dmax) dmax = deaths[k];
    dr_text(fb, st, cx0, y, 1, 0x8FB0D8, "LOST TO");
    for (int k = 0; k < DIE_COUNT; k++) {
        int col = k / 3, row = k % 3, xx = cx0 + col * (cw / 2), yy = y + 12 + row * 12;
        dr_text(fb, st, xx, yy, 1, 0xC8D8F0, DIE_NAMES[k]);
        bar(fb, st, xx + 80, yy, cw / 2 - 90, 7, deaths[k] / (float)dmax, 0xFF6E7A);
    }
    /* how it spent its time: the pet's goals, in one stacked bar */
    y += 54;
    dr_text(fb, st, cx0, y, 1, 0x8FB0D8, "HOW IT SPENT ITS TIME");
    int bx = cx0, top[3] = { -1, -1, -1 };
    for (int g = 0; g < ST_COUNT; g++) {
        if (!goals[g]) continue;
        int gw = (int)(cw * goals[g] / (float)n + 0.5f);
        dr_rect(fb, st, bx, y + 12, gw, 9, dr_hsv(g / (float)ST_COUNT, 0.6f, 0.9f), 255);
        bx += gw;
        for (int q = 0; q < 3; q++) if (top[q] < 0 || goals[g] > goals[top[q]]) { for (int r = 2; r > q; r--) top[r] = top[r - 1]; top[q] = g; break; }
    }
    int lx = cx0;
    for (int q = 0; q < 3 && top[q] >= 0; q++) {
        snprintf(b, sizeof b, "%s %d%%", STRAT_NAMES[top[q]], goals[top[q]] * 100 / n);
        for (char *c = b; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 'a' - 'A'; else if (*c == '_') *c = ' ';
        dr_disc(fb, st, lx + 3, y + 29, 3, dr_hsv(top[q] / (float)ST_COUNT, 0.6f, 0.9f), 255);
        lx += 10 + dr_text(fb, st, lx + 10, y + 26, 1, 0xC8D8F0, b) + 12;
    }
}
static void draw_story(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    panel(fb, st, PX, PY, PW, PH);
    button(fb, st, story_btn(0), u->story_tab == 0 ? 0x103A4A : 0x0C1424, u->story_tab == 0 ? 0x7FF4FF : 0x2A3A58, "JOURNAL", 1);
    button(fb, st, story_btn(1), u->story_tab == 1 ? 0x103A4A : 0x0C1424, u->story_tab == 1 ? 0x7FF4FF : 0x2A3A58, "CHARTS", 1);
    button(fb, st, story_btn(2), 0x10304A, 0x5CC8E0, "BACK", 1);
    if (u->story_tab == 0) draw_journal(u, d, fb, st); else draw_charts(u, d, fb, st);
}
/* the corner buttons: an open book (STORY) and a dish (DISH) */
/* the corner buttons, each with its name under it: an open book (STORY), a dish (DISH), a pencil (WALL) */
static void draw_corner(uint16_t *fb, int st, int k, bool alert, bool active, float ink, float clock) {
    float x, y; corner_pos(k, &x, &y);
    float s = DISH_BTN_R;
    dr_disc(fb, st, x, y, s, 0x060A16, 200);
    if (alert) dr_ring(fb, st, x, y, s + 2, 1.6f, 0xFFB648, (int)(150 + 100 * sinf(clock * 5)));
    if (active) { dr_glow(fb, st, x, y, s * 1.6f, 0x3FD8FF, 110); dr_ring(fb, st, x, y, s + 1.5f, 2, 0x7FF4FF, 255); }
    dr_ring(fb, st, x, y, s - 0.5f, 1.2f, 0x5C7598, 190);
    if (k == BTN_STORY) {
        for (int m = -1; m <= 1; m += 2) {
            dr_line(fb, st, x, y + 7, x + m * 11, y + 4, 1.6f, 0xD8E6F5, 255);
            dr_line(fb, st, x, y - 6, x + m * 11, y - 9, 1.6f, 0xD8E6F5, 255);
            dr_line(fb, st, x + m * 11, y - 9, x + m * 11, y + 4, 1.6f, 0xD8E6F5, 255);
            for (int r = 0; r < 3; r++) dr_line(fb, st, x + m * 3, y - 4 + r * 3.5f, x + m * 8, y - 5 + r * 3.5f, 1, 0x7FA8D8, 200);
        }
        dr_line(fb, st, x, y - 6, x, y + 7, 1.4f, 0xD8E6F5, 255);
    } else if (k == BTN_DISH) {
        dr_ring(fb, st, x, y, 11, 1.6f, 0xD8E6F5, 255);
        for (int m = 0; m < 3; m++) dr_glow(fb, st, x + cosf(m * 2.1f + clock * 0.5f) * 5, y + sinf(m * 2.1f + clock * 0.5f) * 5, 3.5f, 0x7FF4FF, 220);
    } else {                                          /* a pencil, and the walls left as dots */
        dr_line(fb, st, x - 8, y + 6, x + 6, y - 8, 4.5f, 0xFFC23C, 255);
        dr_line(fb, st, x - 8, y + 6, x - 11, y + 9, 2, 0xD8E6F5, 255);
        dr_line(fb, st, x + 6, y - 8, x + 8, y - 10, 4.5f, 0xFF7A8A, 255);
        for (int m = 0; m < DISH_WALLS; m++) {
            float a = 2.2f + m * 0.45f, f = ink - m;
            dr_disc(fb, st, x + cosf(a) * (s - 5), y + sinf(a) * (s - 5), 2.2f, f >= 1 ? 0x7FF4FF : 0x2A3448, 255);
        }
    }
    static const char *const LABEL[BTN_COUNT] = { "STORY", "DISH", "WALL" };
    text_c(fb, st, x, (int)(y + s + 4), 1, active ? 0x7FF4FF : 0xA8B8D8, LABEL[k]);
}

/* ---- the emoji: little drawings, not font glyphs (the font has none) ---- */
static void face(uint16_t *fb, int st, float x, float y, float r, uint32_t skin) {
    dr_disc(fb, st, x, y, r, skin, 255);
    dr_glow(fb, st, x - r * 0.35f, y - r * 0.4f, r * 0.35f, 0xFFFFFF, 90);
}
static void draw_emoji(uint16_t *fb, int st, int e, float x, float y, float r) {
    const uint32_t INK = 0x2A1A08;
    float w = fmaxf(1.2f, r * 0.16f);
    switch (e) {
    case EMO_HAPPY: case EMO_YUM: case EMO_ANGRY: case EMO_SCARED: case EMO_SAD: {
        face(fb, st, x, y, r, e == EMO_ANGRY ? 0xFF8A4C : 0xFFD23C);
        if (e == EMO_SCARED) { for (int m = -1; m <= 1; m += 2) { dr_disc(fb, st, x + m * r * 0.35f, y - r * 0.2f, r * 0.24f, 0xFFFFFF, 255); dr_disc(fb, st, x + m * r * 0.35f, y - r * 0.2f, r * 0.1f, INK, 255); }
                               dr_disc(fb, st, x, y + r * 0.42f, r * 0.2f, INK, 255); break; }
        for (int m = -1; m <= 1; m += 2) dr_disc(fb, st, x + m * r * 0.35f, y - r * 0.2f, r * 0.12f, INK, 255);
        if (e == EMO_ANGRY) for (int m = -1; m <= 1; m += 2) dr_line(fb, st, x + m * r * 0.55f, y - r * 0.55f, x + m * r * 0.15f, y - r * 0.38f, w, INK, 255);
        float cv = e == EMO_SAD || e == EMO_ANGRY ? -1 : 1;
        for (int k = 0; k < 4; k++) {
            float t0 = -1 + k * 0.5f, t1 = t0 + 0.5f;
            dr_line(fb, st, x + t0 * r * 0.45f, y + r * 0.35f + cv * r * 0.2f * (1 - t0 * t0), x + t1 * r * 0.45f, y + r * 0.35f + cv * r * 0.2f * (1 - t1 * t1), w, INK, 255);
        }
        if (e == EMO_YUM) dr_disc(fb, st, x + r * 0.15f, y + r * 0.6f, r * 0.2f, 0xFF5C8A, 255);
        if (e == EMO_SAD) dr_disc(fb, st, x + r * 0.45f, y + r * 0.1f, r * 0.13f, 0x5AC8FF, 255);
        break; }
    case EMO_HEART:
        dr_disc(fb, st, x - r * 0.4f, y - r * 0.2f, r * 0.48f, 0xFF4E7A, 255); dr_disc(fb, st, x + r * 0.4f, y - r * 0.2f, r * 0.48f, 0xFF4E7A, 255);
        for (int k = 0; k <= 6; k++) { float f = k / 6.0f; dr_line(fb, st, x - r * 0.85f * (1 - f), y + f * r * 0.85f - r * 0.05f, x + r * 0.85f * (1 - f), y + f * r * 0.85f - r * 0.05f, 2, 0xFF4E7A, 255); }
        break;
    case EMO_FOOD:
        dr_disc(fb, st, x, y + r * 0.1f, r * 0.8f, 0xFF4A3C, 255); dr_glow(fb, st, x - r * 0.3f, y - r * 0.15f, r * 0.3f, 0xFFFFFF, 120);
        dr_line(fb, st, x, y - r * 0.6f, x + r * 0.15f, y - r * 0.95f, w, 0x6A3A10, 255);
        dr_disc(fb, st, x + r * 0.35f, y - r * 0.75f, r * 0.22f, 0x4CF070, 255);
        break;
    case EMO_SHIELD:
        dr_round_rect(fb, st, (int)(x - r * 0.7f), (int)(y - r * 0.8f), (int)(r * 1.4f), (int)(r * 0.9f), 2, 0x5A9CFF, 255);
        for (int k = 0; k <= 6; k++) { float f = k / 6.0f; dr_line(fb, st, x - r * 0.7f * (1 - f), y + f * r * 0.9f, x + r * 0.7f * (1 - f), y + f * r * 0.9f, 2, 0x5A9CFF, 255); }
        dr_line(fb, st, x, y - r * 0.6f, x, y + r * 0.6f, w, 0xD8E6F5, 255);
        break;
    case EMO_BOLT:
        dr_line(fb, st, x + r * 0.3f, y - r, x - r * 0.35f, y + r * 0.1f, w * 2, 0xFFE040, 255);
        dr_line(fb, st, x - r * 0.35f, y + r * 0.1f, x + r * 0.35f, y - r * 0.1f, w * 2, 0xFFE040, 255);
        dr_line(fb, st, x + r * 0.35f, y - r * 0.1f, x - r * 0.3f, y + r, w * 2, 0xFFE040, 255);
        break;
    case EMO_MOON: dr_disc(fb, st, x, y, r * 0.85f, 0xFFE07A, 255); dr_disc(fb, st, x + r * 0.4f, y - r * 0.25f, r * 0.7f, 0xF4F8FF, 255); break;
    case EMO_SUN:
        dr_disc(fb, st, x, y, r * 0.5f, 0xFFC23C, 255);
        for (int k = 0; k < 8; k++) { float a = k * TAU / 8; dr_line(fb, st, x + cosf(a) * r * 0.65f, y + sinf(a) * r * 0.65f, x + cosf(a) * r * 0.95f, y + sinf(a) * r * 0.95f, w, 0xFFA020, 255); }
        break;
    case EMO_SPARKLE: case EMO_STAR: {
        uint32_t c = e == EMO_STAR ? 0xFFC23C : 0xB07CFF;
        for (int k = 0; k < 5; k++) {
            float a = -1.5708f + k * TAU / 5, b = a + TAU / 10;
            dr_line(fb, st, x + cosf(a) * r, y + sinf(a) * r, x + cosf(b) * r * 0.42f, y + sinf(b) * r * 0.42f, w * 1.4f, c, 255);
            dr_line(fb, st, x + cosf(b) * r * 0.42f, y + sinf(b) * r * 0.42f, x + cosf(a + TAU / 5) * r, y + sinf(a + TAU / 5) * r, w * 1.4f, c, 255);
        }
        dr_disc(fb, st, x, y, r * 0.38f, c, 255);
        break; }
    case EMO_ZZZ: dr_text(fb, st, (int)(x - r * 0.9f), (int)(y - r * 0.2f), 1, 0x5A7CFF, "Z"); dr_text(fb, st, (int)(x - r * 0.2f), (int)(y - r * 0.9f), 2, 0x5A7CFF, "Z"); break;
    case EMO_EYES:
        for (int m = -1; m <= 1; m += 2) { dr_disc(fb, st, x + m * r * 0.42f, y, r * 0.42f, 0xFFFFFF, 255); dr_disc(fb, st, x + m * r * 0.42f + r * 0.12f, y, r * 0.18f, INK, 255); }
        break;
    case EMO_QUESTION: case EMO_EXCLAIM: {
        dr_disc(fb, st, x, y, r, e == EMO_EXCLAIM ? 0xFF4E5E : 0x5A9CFF, 255);
        const char *g = e == EMO_EXCLAIM ? "!" : "?";
        dr_text(fb, st, (int)(x - 5), (int)(y - 7), 2, 0xFFFFFF, g);
        break; }
    case EMO_WAVE:
        face(fb, st, x - r * 0.1f, y + r * 0.1f, r * 0.62f, 0xFFD23C);
        for (int k = 0; k < 4; k++) dr_line(fb, st, x - r * 0.45f + k * r * 0.3f, y - r * 0.2f, x - r * 0.5f + k * r * 0.33f, y - r * 0.9f, w * 1.4f, 0xFFD23C, 255);
        dr_line(fb, st, x + r * 0.7f, y - r * 0.8f, x + r * 0.95f, y - r * 0.4f, w * 0.7f, 0x7FA8D8, 255);
        break;
    case EMO_RING: dr_ring(fb, st, x, y, r * 0.75f, w * 1.6f, 0xFFC870, 255); break;
    default: dr_disc(fb, st, x, y, r * 0.6f, 0x7FF4FF, 255); break;
    }
}

/* ---- the bubbles: where each sits, wrapped, over the cell that speaks ---- */
#define BUB_PER 15                                    /* characters a line, at scale 2 */
#define BUB_LINE 19                                   /* px a line (the round dots of scale 2 need the air) */
typedef struct { rect_t r; float ax, ay; int nl; char line[4][BUB_PER + 1]; } bub_geom_t;
static int wrap_lines(const char *str, int per, char out[][BUB_PER + 1], int max_lines) {
    int nl = 0, n = 0; const char *p = str;
    char *line = out[0];
    while (*p && nl < max_lines) {
        const char *sp = strchr(p, ' '); int wl = sp ? (int)(sp - p) : (int)strlen(p);
        if (wl > per) wl = per;
        if (n && n + 1 + wl > per) { line[n] = 0; if (++nl >= max_lines) break; line = out[nl]; n = 0; }
        if (n) line[n++] = ' ';
        for (int k = 0; k < wl; k++) line[n++] = p[k];
        p += wl; while (*p == ' ') p++;
    }
    if (nl < max_lines) { line[n] = 0; if (n) nl++; }
    return nl;
}
static bool bubble_geom(const dish_ui_t *u, const dish_t *d, int k, bub_geom_t *g) {
    const dish_bubble_t *b = &u->bub[k];
    if (b->strain < 0 || b->delay > 0) return false;
    int c = speaker_cell(d, b->strain, b->cell_id);
    if (c < 0) return false;
    g->ax = d->cell[c].x; g->ay = d->cell[c].y;
    int per = (DISH_W - 120) / 12 < BUB_PER ? (DISH_W - 120) / 12 : BUB_PER;
    g->nl = wrap_lines(b->text, per, g->line, 4);
    int mx = 0; for (int i = 0; i < g->nl; i++) { int l = (int)strlen(g->line[i]); if (l > mx) mx = l; }
    int nw = (int)strlen(strain_name(d, b->strain)) * 6 + 32;   /* (wide enough for the speaker's name too) */
    int w = mx * 12 + 40 > nw ? mx * 12 + 40 : nw, h = g->nl * BUB_LINE + 22;
    float x = g->ax - w * 0.5f, y = g->ay - 18 - h;
    if (y < top_for(150, 6) + 70) y = g->ay + 18;    /* (no room above, under the HUD: below the cell) */
    if (x < 8) x = 8;
    if (x + w > DISH_W - 8) x = DISH_W - 8 - w;
    if (y + h > DISH_H - 8) y = DISH_H - 8 - h;
    g->r = (rect_t){ (int)x, (int)y, w, h };
    return true;
}
/* every bubble placed, none on top of another: a later one moves above (or, out of room, below) the ones before it */
static int bubble_layout(const dish_ui_t *u, const dish_t *d, bub_geom_t *g, int *idx) {
    int n = 0;
    for (int k = 0; k < DISH_BUBBLES; k++) {
        if (!bubble_geom(u, d, k, &g[n])) continue;
        rect_t *r = &g[n].r;
        /* the free spot nearest where it wants to be: where it is, or just above or below one placed before it */
        int want = r->y, best = want, bd = 1 << 30, top = (int)top_for(150, 6) + 70;
        for (int c = -1; c < 2 * n; c++) {
            int y = c < 0 ? want : c % 2 == 0 ? g[c / 2].r.y - r->h - 6 : g[c / 2].r.y + g[c / 2].r.h + 6;
            if (y < top || y + r->h > DISH_H - 8) continue;
            bool clear = true;
            for (int j = 0; j < n && clear; j++) {
                const rect_t *o = &g[j].r;
                clear = !(r->x < o->x + o->w + 4 && o->x < r->x + r->w + 4 && y < o->y + o->h + 4 && o->y < y + r->h + 4);
            }
            if (clear && abs(y - want) < bd) { bd = abs(y - want); best = y; }
        }
        r->y = best;
        idx[n++] = k;
    }
    return n;
}
int dish_ui_bubble_at(const dish_ui_t *u, const dish_t *d, float x, float y) {
    bub_geom_t g[DISH_BUBBLES]; int idx[DISH_BUBBLES], n = bubble_layout(u, d, g, idx);
    for (int k = n - 1; k >= 0; k--) if (in_rect(g[k].r, x, y)) return idx[k];
    return -1;
}
static void draw_bubbles(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    bub_geom_t gs[DISH_BUBBLES]; int idx[DISH_BUBBLES], n = bubble_layout(u, d, gs, idx);
    for (int pass = 0; pass < 2; pass++)              /* every tail first, then every box: no tail crosses a line of text */
        for (int q = 0; q < n; q++) {
            const bub_geom_t g = gs[q];
            const dish_bubble_t *b = &u->bub[idx[q]];
            float a = fminf(1, fminf(b->t * 6, (b->ttl - b->t) * 3));
            if (a <= 0.02f) continue;
            if (pass == 0) {                          /* the tail, toward the speaker */
                float tx = fminf(fmaxf(g.ax, g.r.x + 14), g.r.x + g.r.w - 14), ty = g.r.y > g.ay ? g.r.y : g.r.y + g.r.h;
                float ey = g.ay + (g.r.y > g.ay ? 6 : -6);
                dr_line(fb, st, tx - 5, ty, g.ax, ey, 2.5f, 0xF4F8FF, (int)(230 * a));
                dr_line(fb, st, tx + 5, ty, g.ax, ey, 2.5f, 0xF4F8FF, (int)(230 * a));
                continue;
            }
            uint32_t edge = dr_hsv(d->strain[b->strain].vault.hue, 0.55f, 1);
            dr_round_rect(fb, st, g.r.x - 2, g.r.y - 2, g.r.w + 4, g.r.h + 4, 12, edge, (int)(255 * a));
            dr_round_rect(fb, st, g.r.x, g.r.y, g.r.w, g.r.h, 10, 0xF4F8FF, (int)(250 * a));
            char who[16]; snprintf(who, sizeof who, "%s", strain_name(d, b->strain));   /* who is talking, in its colour */
            dr_text(fb, st, g.r.x + 10, g.r.y + 5, 1, dr_hsv(d->strain[b->strain].vault.hue, 0.8f, 0.6f), who);
            draw_emoji(fb, st, b->emoji, g.r.x + 17, g.r.y + 14 + (g.r.h - 14) * 0.5f, 10);
            for (int i = 0; i < g.nl; i++) dr_text(fb, st, g.r.x + 32, g.r.y + 17 + i * BUB_LINE, 2, 0x141A2E, g.line[i]);
        }
}

/* ---- the explanation bar: hints and what a tap means, at the bottom, pointing at what it is about ---- */
static void draw_info(const dish_ui_t *u, uint16_t *fb, int st) {
    if (u->info_t <= 0) return;
    char lines[4][BUB_PER * 3 + 1];
    int per = (DISH_W - 2 * (DISH_BTN_R * 2 + 18)) / 12;
    if (per > BUB_PER * 3) per = BUB_PER * 3;
    /* (a wider wrap than the bubbles': its own pass) */
    int nl = 0, n = 0; const char *p = u->info; char *line = lines[0];
    while (*p && nl < 4) {
        const char *sp = strchr(p, ' '); int wl = sp ? (int)(sp - p) : (int)strlen(p);
        if (wl > per) wl = per;
        if (n && n + 1 + wl > per) { line[n] = 0; if (++nl >= 4) break; line = lines[nl]; n = 0; }
        if (n) line[n++] = ' ';
        for (int k = 0; k < wl; k++) line[n++] = p[k];
        p += wl; while (*p == ' ') p++;
    }
    if (nl < 4) { line[n] = 0; if (n) nl++; }
    int w = per * 12 + 24, h = nl * BUB_LINE + 14;
    float y = DISH_H - 22 - h;
    while (y > DISH_CY && !(dish_inside(DISH_CX - w * 0.5f, y + h, 4) && dish_inside(DISH_CX + w * 0.5f, y + h, 4))) y -= 2;
    float a = fminf(1, u->info_t * 3);
    if (u->info_x >= 0) {                            /* a pointer at what it is about */
        float k = 0.5f + 0.5f * sinf(u->clock * 5);
        dr_line(fb, st, DISH_CX, y, u->info_x, u->info_y, 2, 0xFFE07A, (int)(170 * a));
        dr_ring(fb, st, u->info_x, u->info_y, 16 + 6 * k, 2.5f, 0xFFE07A, (int)(255 * a));
    }
    dr_round_rect(fb, st, (int)(DISH_CX - w / 2) - 2, (int)y - 2, w + 4, h + 4, 14, 0xFFE07A, (int)(255 * a));
    dr_round_rect(fb, st, (int)(DISH_CX - w / 2), (int)y, w, h, 12, 0x0A1224, (int)(245 * a));
    for (int i = 0; i < nl; i++) text_c(fb, st, DISH_CX, (int)y + 8 + i * BUB_LINE, 2, 0xFFF4D8, lines[i]);
}

/* ---- the banner: the quest, or the wave ---- */
static const char *const SIDE_NAMES[4] = { "LEFT", "RIGHT", "TOP", "BOTTOM" };
static void draw_banner(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    rect_t r = quest_pill();
    char t[64], tm[16];
    uint32_t fill = 0x0A1224, edge = 0xFFC23C, ink = 0xFFFFFF; int emoji = EMO_EXCLAIM; float prog = -1;
    if (d->wave.on) {
        float ox, oy; dish_wave_origin(d, &ox, &oy);
        float k = 0.5f + 0.5f * sinf(u->clock * 8);
        dr_glow(fb, st, ox, oy, 70, 0xFF3A3A, (int)(70 + 60 * k));
        for (int m = 0; m < 3; m++) {                 /* chevrons pointing in from that side */
            float dx = DISH_CX - ox, dy = DISH_CY - oy, l = hypot2(dx, dy) + 1; dx /= l; dy /= l;
            float cx = ox + dx * (20 + m * 16 + 8 * k), cy = oy + dy * (20 + m * 16 + 8 * k);
            dr_line(fb, st, cx - dy * 10 - dx * 8, cy + dx * 10 - dy * 8, cx, cy, 3, 0xFF6E6E, 230);
            dr_line(fb, st, cx + dy * 10 - dx * 8, cy - dx * 10 - dy * 8, cx, cy, 3, 0xFF6E6E, 230);
        }
        int s = d->wave.strain;
        const char *who = d->wave.on == WAVE_COMING ? SPECIES_NAMES[s >= 0 && s < SP_COUNT ? s : SP_PHAGE]
                        : s >= 0 && s < DISH_MAX_STRAINS && d->strain[s].used ? SPECIES_NAMES[d->strain[s].species] : "GERM";
        if (d->wave.on == WAVE_COMING) snprintf(t, sizeof t, "%sS FROM THE %s! %d", who, SIDE_NAMES[d->wave.side & 3], (int)ceilf(d->wave.t));
        else snprintf(t, sizeof t, "DEFEND! %d S  LOST %d", (int)ceilf(d->wave.t), (int)(uint16_t)(d->strain[DISH_PET].deaths - d->wave.lost0));
        for (char *c = t; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        fill = 0x3A0A14; edge = 0xFF5A5A; emoji = EMO_SCARED;
        if (d->wave.on == WAVE_ON) prog = 1 - d->wave.t / WAVE_FIGHT_S;
    } else if (d->quest.state != QS_NONE) {
        if (d->quest.kind == Q_HERD && d->quest.state == QS_ON) {   /* the ring to herd them into */
            float k = 0.5f + 0.5f * sinf(u->clock * 4);
            dr_glow(fb, st, d->quest.x, d->quest.y, 52, 0xFFC870, (int)(40 + 40 * k));
            dr_ring(fb, st, d->quest.x, d->quest.y, 45, 2.5f, 0xFFC870, 230);
            text_c(fb, st, d->quest.x, (int)d->quest.y - 4, 2, 0xFFE0A0, "HERE");
        }
        if (d->quest.state == QS_WON) { snprintf(t, sizeof t, "DONE! +%d DNA", d->quest.reward); edge = 0x4CF08C; fill = 0x0A2A1A; emoji = EMO_STAR; }
        else if (d->quest.state == QS_LOST) { snprintf(t, sizeof t, "MISSED IT"); edge = 0x8FA0B8; ink = 0xC8D0E0; emoji = EMO_SAD; }
        else {
            dish_quest_text(d, t, sizeof t);
            int left = (int)ceilf(d->quest.limit - d->quest.t);
            snprintf(tm, sizeof tm, " %d", left < 0 ? 0 : left);
            if (strlen(t) + strlen(tm) < sizeof t) strcat(t, tm);
            prog = dish_quest_progress(d);
            emoji = d->quest.kind == Q_LIGHT ? EMO_SUN : d->quest.kind == Q_HERD ? EMO_RING : d->quest.kind == Q_FRIEND ? EMO_HEART :
                    d->quest.kind == Q_GROW ? EMO_SPARKLE : d->quest.kind == Q_FLICK ? EMO_BOLT : EMO_SHIELD;
        }
    } else return;
    int sc = dr_text_w(t, 2) + 40 <= r.w ? 2 : 1;
    dr_round_rect(fb, st, r.x - 2, r.y - 2, r.w + 4, r.h + 4, 14, edge, 255);
    dr_round_rect(fb, st, r.x, r.y, r.w, r.h, 12, fill, 240);
    if (prog >= 0) dr_rect(fb, st, r.x + 10, r.y + r.h - 5, (int)((r.w - 20) * fminf(1, prog)), 2, edge, 220);
    draw_emoji(fb, st, emoji, r.x + 16, r.y + r.h * 0.5f - 1, 9);
    dr_text(fb, st, r.x + 32, r.y + (r.h - 7 * sc) / 2 - 1, sc, ink, t);
}

void dish_ui_render(const dish_ui_t *u, const dish_t *d, uint16_t *fb, int st) {
    const strain_t *pet = &d->strain[DISH_PET];
    char b[64];
    /* the microscope, and the thought of the colony it looks at */
    if (u->lens >= 0 && u->page == PAGE_NONE) {
        const cell_t *c = &d->cell[u->lens];
        float lx, ly, lr; lens_geom(d, u->lens, &lx, &ly, &lr);
        dr_line(fb, st, c->x, c->y, lx + (c->x - lx) * lr / fmaxf(1, hypot2(c->x - lx, c->y - ly)),
                ly + (c->y - ly) * lr / fmaxf(1, hypot2(c->x - lx, c->y - ly)), 1, 0x7FA8D8, 90);
        dr_ring(fb, st, c->x, c->y, dish_cell_radius(c) * 2.6f, 1, 0x7FF4FF, 160);
        dish_render_lens(d, c->x, c->y, u->lens_zoom, fb, st, lx, ly, lr);
        snprintf(b, sizeof b, "%s  GEN %u", c->strain == DISH_PET ? pet->name : SPECIES_NAMES[c->species], (unsigned)c->gen);
        int w = dr_text_w(b, 1) + 14;
        dr_round_rect(fb, st, (int)(lx - w / 2), (int)(ly + lr - 18), w, 13, 6, 0x0A1224, 220);
        text_c(fb, st, lx, (int)(ly + lr - 15), 1, 0xC8F4FF, b);
        dish_ui_thought(d, c->strain, b, sizeof b);
        w = dr_text_w(b, 1) + 16;
        float ty = ly + lr + 10;
        dr_round_rect(fb, st, (int)(lx - w / 2), (int)ty - 4, w, 16, 7, 0x0A1224, 230);
        text_c(fb, st, lx, (int)ty, 1, 0x7FF4FF, b);
    }
    /* the top: the pet's name, its mood, and the DNA */
    if (u->page == PAGE_NONE && !u->setup) {
        int ty = (int)top_for(150, 6) + 4;
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
        draw_corner(fb, st, BTN_STORY, false, false, 0, u->clock);
        draw_corner(fb, st, BTN_DISH, d->badges_new != 0, false, 0, u->clock);
        draw_corner(fb, st, BTN_WALL, false, u->walling, d->ink, u->clock);
        if (pet->count) draw_banner(u, d, fb, st);
        draw_bubbles(u, d, fb, st);
        draw_info(u, fb, st);
    }
    /* extinct: the vault counts down */
    if (pet->count == 0 && pet->extinct_t > 0) {
        dr_rect(fb, st, 0, 0, DISH_W, DISH_H, 0x000000, 120);
        float k = 0.6f + 0.4f * sinf(u->clock * 3);
        dr_glow(fb, st, DISH_CX, DISH_CY, 70, dr_hsv(pet->vault.hue, 0.5f, 1), (int)(110 * k));
        dr_cell_icon(fb, st, (species_t)pet->species, &pet->vault, pet->mut, DISH_CX, DISH_CY, 3.2f, 0, u->clock * 0.2f, false, 0);
        snprintf(b, sizeof b, "%s DIED OUT", pet->name);
        text_c(fb, st, DISH_CX, (int)(DISH_CY - 90), 3, 0xFFFFFF, b);
        text_c(fb, st, DISH_CX, (int)(DISH_CY - 60), 1, 0x8FB0D8, "THE SPORE VAULT IS WAKING IT UP");
        if (d->run_muts) text_c(fb, st, DISH_CX, (int)(DISH_CY - 48), 1, 0xFFB0BC, d->vault_used ? "" : "IT CAN ONLY DO THIS ONCE A RUN");
        snprintf(b, sizeof b, "%d", (int)ceilf(pet->extinct_t));
        text_c(fb, st, DISH_CX, (int)(DISH_CY + 54), 4, dr_hsv(pet->vault.hue, 0.4f, 1), b);
    }
    switch (u->page) {
    case PAGE_CARD: draw_card(u, d, fb, st); break;
    case PAGE_DRAFT: draw_draft(u, d, fb, st); break;
    case PAGE_RUNOVER: draw_runover(u, d, fb, st); break;
    case PAGE_DEX:  draw_dex(u, d, fb, st); break;
    case PAGE_BADGES: draw_badges(u, d, fb, st); break;
    case PAGE_STORY: draw_story(u, d, fb, st); break;
    default: break;
    }
    if (u->setup) draw_setup(u, d, fb, st);
    /* a toast: over the dish, or just under the page when one is up */
    if (u->toast_t > 0 && u->extinct_show <= 0 && u->page != PAGE_DRAFT) {
        bool paged = u->page != PAGE_NONE || u->setup;
        float a = fminf(1, u->toast_t * 3);
        int sc = dr_text_w(u->toast, 2) + 24 <= DISH_W - 40 ? 2 : 1;
        int w = dr_text_w(u->toast, sc) + 24, y = paged ? PY + PH + 14 : (int)(DISH_CY - DISH_RY * 0.55f);
        if (paged && y + 22 > DISH_H) y = PY - 34;
        dr_round_rect(fb, st, (int)(DISH_CX - w / 2), y - 8, w, 30, 15, 0x0A1224, (int)(230 * a));
        text_c(fb, st, DISH_CX, y + (sc == 1 ? 4 : 0), sc, 0xFFFFFF, u->toast);
    }
}
