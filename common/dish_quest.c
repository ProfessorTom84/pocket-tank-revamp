/* dish_quest.c — see dish_quest.h. */
#include "dish_quest.h"
#include "dish_story.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static float dist(float ax, float ay, float bx, float by) { return sqrtf((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); }
/* a visitor of the kind wanted: a threat (it hunts or harms), or a friend */
static int find(const dish_t *d, bool friendly, int min_cells) {
    for (int s = 1; s < DISH_MAX_STRAINS; s++) {
        const strain_t *st = &d->strain[s];
        if (st->used && st->count >= min_cells && SPECIES_FRIENDLY[st->species] == friendly && st->species != SP_MOLD) return s;
    }
    return -1;
}
static const char *who(const dish_t *d) {
    int s = d->quest.who;
    return s >= 0 && s < DISH_MAX_STRAINS && d->strain[s].used ? SPECIES_NAMES[d->strain[s].species] : "VISITOR";
}

void dish_quest_start(dish_t *d, int kind) {
    const strain_t *pet = &d->strain[DISH_PET];
    if (kind <= Q_NONE || kind >= Q_COUNT || !pet->count) return;
    int threat = find(d, false, kind == Q_FLICK ? 3 : 2), fr = find(d, true, 1);
    if ((kind == Q_DEFEND || kind == Q_FLICK || kind == Q_WALL) && threat < 0) return;
    if (kind == Q_FRIEND && fr < 0) return;
    memset(&d->quest, 0, sizeof d->quest);
    d->quest.kind = (uint8_t)kind; d->quest.state = QS_ON; d->quest.who = -1;
    switch (kind) {
    case Q_LIGHT:  d->quest.goal = 6; d->quest.limit = 40; d->quest.reward = 5; break;
    case Q_GROW:   d->quest.goal = (float)fminf(150, pet->count + fmaxf(5, pet->count / 3.0f)); d->quest.limit = 150; d->quest.reward = 8; break;
    case Q_HERD: {
        float bx = DISH_CX, by = DISH_CY, bd = 0;
        for (int k = 0; k < 24; k++) {                  /* somewhere well away from the colony */
            float x = dish_rand(d, 50, DISH_W - 50), y = dish_rand(d, 70, DISH_H - 50);
            if (!dish_inside(x, y, 45)) continue;
            float dd = dist(x, y, pet->cx, pet->cy);
            if (dd > bd) { bd = dd; bx = x; by = y; }
            if (dd > 160) break;
        }
        d->quest.x = bx; d->quest.y = by; d->quest.goal = 1; d->quest.limit = 60; d->quest.reward = 8; break; }
    case Q_DEFEND: d->quest.who = (int8_t)threat; d->quest.goal = 4; d->quest.limit = 40; d->quest.reward = 10; d->quest.lost0 = (uint16_t)pet->deaths; break;
    case Q_FLICK:  d->quest.who = (int8_t)threat; d->quest.goal = 3; d->quest.limit = 40; d->quest.reward = 6; d->quest.lost0 = d->flicks; break;
    case Q_WALL:   d->quest.who = (int8_t)threat; d->quest.goal = 40; d->quest.limit = 45; d->quest.reward = 6; d->quest.lost0 = d->wall_hits; break;
    case Q_FRIEND: d->quest.who = (int8_t)fr; d->quest.goal = 1; d->quest.limit = 60; d->quest.reward = 6; break;
    }
    dish_emit(DEV_QUEST, QS_ON);
}
static void finish(dish_t *d, bool won) {
    d->quest.state = won ? QS_WON : QS_LOST;
    d->quest.t = 3;                                   /* (the result shows a moment) */
    if (won) { d->dna += d->quest.reward; d->quests_won++; }
    dish_story_note(d, SE_QUEST, d->strain[DISH_PET].species, 0, d->quest.kind, won);
    dish_emit(DEV_QUEST, d->quest.state);
}

void dish_wave_start(dish_t *d, int side, species_t sp) {
    d->wave.on = WAVE_COMING; d->wave.t = WAVE_WARN_S; d->wave.side = (int8_t)(side & 3);
    d->wave.strain = (int8_t)sp;                      /* (the species until it comes; then its strain) */
    dish_emit(DEV_WAVE, 1);
}
void dish_wave_origin(const dish_t *d, float *x, float *y) {
    static const float SX[4] = { 0, 1, 0.5f, 0.5f }, SY[4] = { 0.5f, 0.5f, 0, 1 };   /* left, right, top, bottom */
    *x = SX[d->wave.side & 3] * DISH_W; *y = SY[d->wave.side & 3] * DISH_H;
    dish_toward_inside(x, y, 24);
}
static void wave_arrive(dish_t *d) {
    species_t sp = (species_t)d->wave.strain;
    int s = -1;
    if (sp == SP_PHAGE) { dish_outbreak(d, 10); for (int k = 1; k < DISH_MAX_STRAINS; k++) if (d->strain[k].used && d->strain[k].species == SP_PHAGE) s = k; }
    else s = dish_visit(d, sp);
    if (s < 0) for (int k = 1; k < DISH_MAX_STRAINS; k++) if (d->strain[k].used && d->strain[k].species == sp) s = k;   /* (already here: they all come) */
    float ox, oy; dish_wave_origin(d, &ox, &oy);
    bool across = d->wave.side >= 2;                  /* top or bottom: spread along x */
    if (s >= 0)
        for (int i = 0; i < DISH_MAX_CELLS; i++) {
            cell_t *c = &d->cell[i];
            if (c->strain != s || c->state != CS_LIVE || c->host) continue;
            float o = dish_rand(d, -70, 70);
            c->x = ox + (across ? o : 0); c->y = oy + (across ? 0 : o);
            dish_toward_inside(&c->x, &c->y, 20);
        }
    if (s >= 0 && sp != SP_PHAGE) d->strain[s].stay_t = WAVE_FIGHT_S + 10;   /* (they go when it's over) */
    d->wave.strain = (int8_t)s;
    d->wave.lost0 = (uint16_t)d->strain[DISH_PET].deaths;
    d->wave.on = WAVE_ON; d->wave.t = WAVE_FIGHT_S;
    dish_emit(DEV_WAVE, 2);
}
static void wave_over(dish_t *d) {
    const strain_t *pet = &d->strain[DISH_PET];
    int lost = (int)(uint16_t)(pet->deaths - d->wave.lost0);
    bool won = pet->count > 0 && lost <= 6;
    int s = d->wave.strain;
    int sp = s >= 0 && s < DISH_MAX_STRAINS && d->strain[s].used ? d->strain[s].species : SP_PHAGE;
    if (won) { d->dna += (uint32_t)(lost <= 1 ? 12 : 12 - lost); d->waves_won++; } else d->dna += 1;
    dish_story_note(d, SE_WAVE, sp, pet->species, won, (uint16_t)lost);
    d->wave.on = WAVE_NONE;
    d->wave_cd = dish_rand(d, 240, 360);
    dish_emit(DEV_WAVE, 3);
}

void dish_quest_tick(dish_t *d, float dt) {
    const strain_t *pet = &d->strain[DISH_PET];
    bool alive = pet->count > 0 && !d->run_over;
    /* the wave */
    if (d->wave.on == WAVE_COMING) { if ((d->wave.t -= dt) <= 0) wave_arrive(d); }
    else if (d->wave.on == WAVE_ON) { if ((d->wave.t -= dt) <= 0 || !alive) wave_over(d); }
    else if (alive && d->run_muts && pet->count >= 4 && !d->drafts && (d->wave_cd -= dt) <= 0) {
        static const species_t W[4] = { SP_PHAGE, SP_VIBRIO, SP_PHAGE, SP_MYXO };
        dish_wave_start(d, (int)dish_rand(d, 0, 3.99f), W[(int)dish_rand(d, 0, 3.99f)]);
    }
    /* the quest */
    if (d->quest.state == QS_WON || d->quest.state == QS_LOST) {
        if ((d->quest.t -= dt) <= 0) { d->quest.state = QS_NONE; d->quest_cd = dish_rand(d, 25, 45); }
        return;
    }
    if (d->quest.state == QS_NONE) {
        if (!alive || d->wave.on || (d->quest_cd -= dt) > 0) return;
        int pool[Q_COUNT], n = 0;
        if (pet->energy < 0.6f) { pool[n++] = Q_LIGHT; pool[n++] = Q_LIGHT; }
        if (pet->count < 120) pool[n++] = Q_GROW;
        pool[n++] = Q_HERD;
        if (find(d, false, 2) >= 0) { pool[n++] = Q_DEFEND; pool[n++] = Q_WALL; }
        if (find(d, false, 3) >= 0) pool[n++] = Q_FLICK;
        if (find(d, true, 1) >= 0) pool[n++] = Q_FRIEND;
        int k = pool[(int)dish_rand(d, 0, n - 0.01f)];
        if (k == d->quest.kind && n > 1) k = pool[(int)dish_rand(d, 0, n - 0.01f)];   /* (not the same one twice, mostly) */
        dish_quest_start(d, k);
        if (d->quest.state != QS_ON) d->quest_cd = 5;
        return;
    }
    /* on */
    if (!alive) { d->quest.state = QS_NONE; d->quest_cd = 30; return; }
    d->quest.t += dt;
    int w = d->quest.who;
    bool gone = w >= 0 && (!d->strain[w].used || !d->strain[w].count);
    switch (d->quest.kind) {
    case Q_LIGHT:
        if (d->beam > 0.3f && dist(d->beam_x, d->beam_y, pet->cx, pet->cy) < 80) d->quest.prog += dt;
        if (d->quest.prog >= d->quest.goal) { finish(d, true); return; }
        break;
    case Q_GROW:
        d->quest.prog = (float)pet->count;
        if (pet->count >= d->quest.goal) { finish(d, true); return; }
        break;
    case Q_HERD:
        d->quest.prog = 1 - fminf(1, (dist(pet->cx, pet->cy, d->quest.x, d->quest.y) - 45) / 200);
        if (dist(pet->cx, pet->cy, d->quest.x, d->quest.y) < 45) { finish(d, true); return; }
        break;
    case Q_DEFEND:
        d->quest.prog = (float)(uint16_t)(pet->deaths - d->quest.lost0);
        if (d->quest.prog >= d->quest.goal) { finish(d, false); return; }
        if (gone || d->quest.t >= d->quest.limit) { finish(d, true); return; }   /* (seen off, or held out) */
        break;
    case Q_FLICK:
        d->quest.prog = (float)(uint16_t)(d->flicks - d->quest.lost0);
        if (d->quest.prog >= d->quest.goal) { finish(d, true); return; }
        break;
    case Q_WALL:
        d->quest.prog = (float)(uint16_t)(d->wall_hits - d->quest.lost0);
        if (d->quest.prog >= d->quest.goal) { finish(d, true); return; }
        break;
    case Q_FRIEND:
        if (!gone && w >= 0 && dist(pet->cx, pet->cy, d->strain[w].cx, d->strain[w].cy) < 55) { finish(d, true); return; }
        break;
    }
    if (d->quest.t >= d->quest.limit || (gone && d->quest.kind != Q_DEFEND)) finish(d, false);
}

static const char *const NAMES[Q_COUNT] = { "", "LIGHT", "TO GROW", "TO BE TAKEN TO THE RING", "PROTECTION", "A FLICK", "A WALL", "TO MEET A FRIEND" };
const char *dish_quest_name(int k) { return k > 0 && k < Q_COUNT ? NAMES[k] : "SOMETHING"; }
void dish_quest_text(const dish_t *d, char *out, int n) {
    switch (d->quest.kind) {
    case Q_LIGHT:  snprintf(out, (size_t)n, "SHINE LIGHT ON US"); break;
    case Q_GROW:   snprintf(out, (size_t)n, "GROW TO %d CELLS", (int)d->quest.goal); break;
    case Q_HERD:   snprintf(out, (size_t)n, "TAKE US TO THE RING"); break;
    case Q_DEFEND: snprintf(out, (size_t)n, "KEEP THE %sS AWAY", who(d)); break;
    case Q_FLICK:  snprintf(out, (size_t)n, "FLICK %d %sS AWAY", (int)d->quest.goal, who(d)); break;
    case Q_WALL:   snprintf(out, (size_t)n, "WALL OFF THE %sS", who(d)); break;
    case Q_FRIEND: snprintf(out, (size_t)n, "TAKE US TO THE %sS", who(d)); break;
    default:       snprintf(out, (size_t)n, "-"); break;
    }
}
void dish_quest_ask(const dish_t *d, char *out, int n) {
    switch (d->quest.kind) {
    case Q_LIGHT:  snprintf(out, (size_t)n, "WE'RE HUNGRY! HOLD YOUR FINGER ON US TO SHINE LIGHT."); break;
    case Q_GROW:   snprintf(out, (size_t)n, "LET'S GROW BIG! %d OF US, CAN WE DO IT?", (int)d->quest.goal); break;
    case Q_HERD:   snprintf(out, (size_t)n, "SEE THAT GLOWING RING? TAKE US THERE!"); break;
    case Q_DEFEND: snprintf(out, (size_t)n, "THE %sS ARE AFTER US! KEEP THEM AWAY!", who(d)); break;
    case Q_FLICK:  snprintf(out, (size_t)n, "FLICK THOSE %sS AWAY, GIANT!", who(d)); break;
    case Q_WALL:   snprintf(out, (size_t)n, "BUILD US A WALL, THE %sS ARE COMING!", who(d)); break;
    case Q_FRIEND: snprintf(out, (size_t)n, "I WANT TO MEET THE %sS. TAKE US OVER?", who(d)); break;
    default:       out[0] = 0; break;
    }
}
const char *dish_quest_help(int k) {
    switch (k) {
    case Q_LIGHT:  return "HOLD YOUR FINGER ON THE COLONY: LIGHT GROWS FOOD THERE.";
    case Q_GROW:   return "LIGHT GROWS FOOD, AND FED GERMS SPLIT IN TWO.";
    case Q_HERD:   return "DRAG TO MAKE A CURRENT, OR TILT THE DISH, AND MOVE THEM INTO THE RING.";
    case Q_DEFEND: return "DRAG CURRENTS, DRAW WALLS AND FLICK ENEMIES SO YOU LOSE FEWER THAN 4.";
    case Q_FLICK:  return "TOUCH AN ENEMY GERM AND SWIPE FAST TO THROW IT ACROSS THE DISH.";
    case Q_WALL:   return "TAP THE WALL BUTTON (BOTTOM LEFT), THEN DRAW A LINE IN THEIR WAY.";
    case Q_FRIEND: return "DRAG A CURRENT OR TILT THE DISH TO BRING YOUR COLONY TO THE FRIEND.";
    }
    return "";
}
float dish_quest_progress(const dish_t *d) {
    if (d->quest.state != QS_ON) return d->quest.state == QS_WON ? 1 : 0;
    switch (d->quest.kind) {
    case Q_GROW:   return fminf(1, d->quest.prog / fmaxf(1, d->quest.goal));
    case Q_DEFEND: return fminf(1, d->quest.t / d->quest.limit);
    case Q_HERD:   return fmaxf(0, d->quest.prog);
    case Q_FRIEND: { int w = d->quest.who; if (w < 0 || !d->strain[w].used) return 0;
                     return fmaxf(0, 1 - (dist(d->strain[DISH_PET].cx, d->strain[DISH_PET].cy, d->strain[w].cx, d->strain[w].cy) - 55) / 200); }
    default:       return fminf(1, d->quest.prog / fmaxf(1, d->quest.goal));
    }
}
