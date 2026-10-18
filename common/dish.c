/* dish.c — the living dish: see dish.h. */
#include "dish.h"
#include "dish_prog.h"
#include "dish_mut.h"
#include "dish_story.h"
#include "dish_talk.h"
#include <math.h>
#include <string.h>

#define TAU 6.2831853f
#define DIV_TIME     DISH_DIV_TIME
#define RESPAWN_S    30.0f   /* the pet's spore vault: extinct -> back in this long */
#define WILD_BACK_S  240.0f  /* (load's range check on a timer: no countdown runs longer) */
#define DRAFT_GENS   DISH_DRAFT_GENS
#define GIFT_CD      45.0f   /* seconds between free cards from gene transfer */
#define ASK_S        3.0f    /* a strain's brain is asked about this often */
#define FCW ((float)DISH_W / FIELD_N)
#define FCH ((float)DISH_H / FIELD_N)

const char *const SPECIES_NAMES[SP_COUNT] = { "coccus", "bacillus", "spirillum", "cyano", "myxo", "phage",
                                              "mold", "vibrio", "lacto", "halophile", "symbiont", "ciliate", "donor" };
const bool SPECIES_FRIENDLY[SP_COUNT] = { [SP_CYANO] = true, [SP_SYMBIONT] = true, [SP_CILIATE] = true, [SP_DONOR] = true };
const char *const STRAT_NAMES[ST_COUNT] = { "forage", "cluster", "explore", "rest", "divide", "spore", "seek_light", "avoid_light",
                                           "hunt", "flee", "guard", "befriend" };

/* the species: what every genome multiplies */
typedef struct {
    float radius, speed, metab, eat, photo, prey;   /* photo: energy/s in the light; prey: drain/s on a touched cell */
    bool adhesive, virus;
} sp_info_t;
static const sp_info_t SP[SP_COUNT] = {
    [SP_COCCUS]    = { 5.5f, 10, 0.0035f, 0.030f, 0,      0,    true,  false },   /* (a slow glide toward food: a rival, not a rock) */
    [SP_BACILLUS]  = { 4.6f, 30, 0.0050f, 0.032f, 0,      0,    false, false },
    [SP_SPIRILLUM] = { 3.4f, 40, 0.0060f, 0.030f, 0,      0,    false, false },
    [SP_CYANO]     = { 4.2f,  4, 0.0030f, 0.008f, 0.006f, 0,    true,  false },
    [SP_MYXO]      = { 3.8f, 20, 0.0050f, 0.010f, 0,      0.12f, false, false },
    [SP_PHAGE]     = { 2.8f,  9, 0,       0,     0,      0,    false, true  },
    [SP_MOLD]      = { 3.6f,  0, 0.0030f, 0.020f, 0,      0,    true,  false },   /* grows in from the glass, leaks antibiotic */
    [SP_VIBRIO]    = { 2.0f, 55, 0.0060f, 0.004f, 0,      0.45f, false, false },  /* a tiny fast hunter: it bores in */
    [SP_LACTO]     = { 3.8f, 14, 0.0040f, 0.030f, 0,      0,    false, false },   /* sours the dish */
    [SP_HALO]      = { 4.0f,  6, 0.0040f, 0.026f, 0,      0,    true,  false },   /* salts the dish */
    [SP_SYMBIONT]  = { 3.8f, 18, 0.0030f, 0.010f, 0.004f, 0,    false, false },   /* follows the pet, leaves food */
    [SP_CILIATE]   = { 8.5f, 16, 0.0040f, 0.010f, 0,      0,    false, false },   /* eats free viruses and hunters */
    [SP_DONOR]     = { 4.2f, 20, 0.0040f, 0.025f, 0,      0,    false, false },   /* a touch gives the pet a gene: a free card */
};

static dish_event_fn g_ev; static void *g_ev_ud;
void dish_events_set(dish_event_fn fn, void *ud) { g_ev = fn; g_ev_ud = ud; }
void dish_emit(int ev, int strain) { if (g_ev) g_ev(ev, strain, g_ev_ud); }
#define emit dish_emit

static uint32_t xs(dish_t *d) { uint32_t x = d->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return d->rng = x ? x : 1; }
float dish_rand(dish_t *d, float lo, float hi) { return lo + (hi - lo) * (xs(d) >> 8) * (1.0f / 16777216.0f); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* the glass as a rounded rectangle's distance field: inside the straight part, the distance to the
 * nearer side; in a corner's square, to the corner's arc */
float dish_edge_dist(float x, float y) {
    float qx = fabsf(x - DISH_CX) - (DISH_RX - DISH_CORNER_R), qy = fabsf(y - DISH_CY) - (DISH_RY - DISH_CORNER_R);
    float out = sqrtf(fmaxf(qx, 0) * fmaxf(qx, 0) + fmaxf(qy, 0) * fmaxf(qy, 0)), in = fminf(fmaxf(qx, qy), 0);
    return DISH_CORNER_R - out - in;
}
bool dish_inside(float x, float y, float m) { return dish_edge_dist(x, y) >= m; }
void dish_edge_normal(float x, float y, float *nx, float *ny) {
    float sx = x < DISH_CX ? -1.0f : 1.0f, sy = y < DISH_CY ? -1.0f : 1.0f;
    float qx = fabsf(x - DISH_CX) - (DISH_RX - DISH_CORNER_R), qy = fabsf(y - DISH_CY) - (DISH_RY - DISH_CORNER_R);
    if (qx > 0 && qy > 0) { float l = sqrtf(qx * qx + qy * qy); *nx = sx * qx / l; *ny = sy * qy / l; }   /* in a corner: out along its radius */
    else if (qx > qy) { *nx = sx; *ny = 0; }
    else { *nx = 0; *ny = sy; }
}
void dish_toward_inside(float *x, float *y, float m) {
    for (int k = 0; k < 4; k++) {                   /* (a step or two; a corner can need a second) */
        float e = dish_edge_dist(*x, *y);
        if (e >= m) return;
        float nx, ny; dish_edge_normal(*x, *y, &nx, &ny);
        *x -= nx * (m - e + 0.01f); *y -= ny * (m - e + 0.01f);
    }
}

/* ---- fields ---- */
static int fidx(float x, float y) {
    int i = (int)(x / FCW), j = (int)(y / FCH);
    if (i < 0) { i = 0; } if (i >= FIELD_N) { i = FIELD_N - 1; }
    if (j < 0) { j = 0; } if (j >= FIELD_N) { j = FIELD_N - 1; }
    return j * FIELD_N + i;
}
float dish_field(const float *f, float x, float y) {
    float fx = x / FCW - 0.5f, fy = y / FCH - 0.5f;
    int i = (int)floorf(fx), j = (int)floorf(fy);
    float a = fx - i, b = fy - j;
    int i0 = i < 0 ? 0 : i >= FIELD_N ? FIELD_N - 1 : i, i1 = i + 1 < 0 ? 0 : i + 1 >= FIELD_N ? FIELD_N - 1 : i + 1;
    int j0 = j < 0 ? 0 : j >= FIELD_N ? FIELD_N - 1 : j, j1 = j + 1 < 0 ? 0 : j + 1 >= FIELD_N ? FIELD_N - 1 : j + 1;
    return (f[j0 * FIELD_N + i0] * (1 - a) + f[j0 * FIELD_N + i1] * a) * (1 - b)
         + (f[j1 * FIELD_N + i0] * (1 - a) + f[j1 * FIELD_N + i1] * a) * b;
}
static uint8_t g_in[FIELD_N * FIELD_N];   /* the field cells inside the dish (same on every dish of a build) */
static void field_mask(void) {
    for (int j = 0; j < FIELD_N; j++)
        for (int i = 0; i < FIELD_N; i++)
            g_in[j * FIELD_N + i] = dish_inside((i + 0.5f) * FCW, (j + 0.5f) * FCH, -FCW * 0.5f);
}
/* one explicit diffusion step among the inside cells; the glass reflects */
static float g_tmp[FIELD_N * FIELD_N];   /* diffuse's and advect's scratch (one: they never overlap) */
static void diffuse(float *f, float k) {
    float *tmp = g_tmp;
    if (k <= 0) return;
    if (k > 0.24f) k = 0.24f;
    for (int j = 0; j < FIELD_N; j++)
        for (int i = 0; i < FIELD_N; i++) {
            int c = j * FIELD_N + i;
            if (!g_in[c]) { tmp[c] = 0; continue; }
            float v = f[c], s = 0;
            int nb[4] = { i > 0 ? c - 1 : c, i < FIELD_N - 1 ? c + 1 : c, j > 0 ? c - FIELD_N : c, j < FIELD_N - 1 ? c + FIELD_N : c };
            for (int q = 0; q < 4; q++) s += g_in[nb[q]] ? f[nb[q]] : v;
            tmp[c] = v + k * (s - 4 * v);
        }
    memcpy(f, tmp, sizeof g_tmp);
}
/* the stir current carries what is dissolved (semi-Lagrangian) */
static void advect(const dish_t *d, float *f, float dt) {
    float *tmp = g_tmp;
    for (int j = 0; j < FIELD_N; j++)
        for (int i = 0; i < FIELD_N; i++) {
            int c = j * FIELD_N + i;
            float x = (i + 0.5f) * FCW - d->flowx[c] * dt, y = (j + 0.5f) * FCH - d->flowy[c] * dt;
            tmp[c] = g_in[c] ? dish_field(f, x, y) : 0;
        }
    memcpy(f, tmp, sizeof g_tmp);
}
static void splat(float *f, float x, float y, float amount, float rad_px, float cap) {
    for (int j = 0; j < FIELD_N; j++)
        for (int i = 0; i < FIELD_N; i++) {
            int c = j * FIELD_N + i;
            if (!g_in[c]) continue;
            float dx = (i + 0.5f) * FCW - x, dy = (j + 0.5f) * FCH - y;
            float w = expf(-(dx * dx + dy * dy) / (2 * rad_px * rad_px));
            if (w < 0.02f) continue;
            f[c] = clampf(f[c] + amount * w, 0, cap);
        }
}

/* ---- particles ---- */
static void part_add(dish_t *d, part_kind_t k, float x, float y, float vx, float vy, float life, uint32_t rgb) {
    for (int i = 0; i < DISH_MAX_PARTS; i++)
        if (!d->part[i].used) {
            d->part[i] = (part_t){ x, y, vx, vy, 0, life, rgb, (uint8_t)k, 1 };
            return;
        }
}
static void parts_tick(dish_t *d, float dt) {
    for (int i = 0; i < DISH_MAX_PARTS; i++) {
        part_t *p = &d->part[i];
        if (!p->used) continue;
        p->t += dt;
        if (p->kind != PT_ZAP) {                       /* (a zap's vx, vy is the arc's far end, not a velocity) */
            p->x += p->vx * dt; p->y += p->vy * dt;
            p->vx *= expf(-3 * dt); p->vy *= expf(-3 * dt);
        }
        if (p->kind == PT_HEART) p->vy -= 6 * dt;
        if (p->t >= p->life) {
            p->used = 0;
            if (p->kind == PT_DROP) part_add(d, PT_RIPPLE, p->x, p->y, 0, 0, 0.9f, p->rgb);   /* the drop lands */
        }
    }
}

/* ---- genomes ---- */
genome_t dish_species_genome(species_t sp) {
    static const float HUE[SP_COUNT]  = { 0.08f, 0.52f, 0.86f, 0.33f, 0.14f, 0.74f, 0.20f, 0.99f, 0.62f, 0.93f, 0.45f, 0.58f, 0.15f };
    static const float HEAT[SP_COUNT] = { 0.45f, 0.5f, 0.68f, 0.52f, 0.5f, 0.5f, 0.45f, 0.55f, 0.5f, 0.55f, 0.5f, 0.5f, 0.5f };
    static const float PH[SP_COUNT]   = { 0.5f, 0.55f, 0.42f, 0.62f, 0.5f, 0.5f, 0.45f, 0.5f, 0.3f, 0.6f, 0.5f, 0.5f, 0.5f };
    static const float SALT[SP_COUNT] = { 0.5f, 0.3f, 0.3f, 0.4f, 0.35f, 1, 0.6f, 0.3f, 0.4f, 1, 0.4f, 0.4f, 0.4f };   /* coccus: salt-hardier, like a staph */
    genome_t g = { 1, 1, HUE[sp], HEAT[sp], SALT[sp], sp == SP_MOLD ? 1.0f : 0.05f, 0.15f, 1, PH[sp] };   /* (the mold makes the antibiotic: it is immune) */
    return g;
}
static float mut(dish_t *d, float v, float sd, float lo, float hi) {
    /* a cheap near-normal step: the mean of three uniforms */
    float n = (dish_rand(d, -1, 1) + dish_rand(d, -1, 1) + dish_rand(d, -1, 1)) * 0.577f;
    return clampf(v + n * sd, lo, hi);
}
static genome_t mutate(dish_t *d, const genome_t *p, float rate) {
    genome_t g = *p;
    g.speed     = mut(d, g.speed, 0.04f * rate, 0.5f, 1.6f);
    g.size      = mut(d, g.size, 0.03f * rate, 0.75f, 1.35f);
    g.hue       = g.hue + (dish_rand(d, -1, 1) * 0.012f * rate);
    g.hue      -= floorf(g.hue);
    g.heat_opt  = mut(d, g.heat_opt, 0.02f * rate, 0.05f, 0.95f);
    g.salt_tol  = mut(d, g.salt_tol, 0.02f * rate, 0, 1);
    g.drug_res  = mut(d, g.drug_res, 0.04f * rate, 0, 1);
    g.phage_res = mut(d, g.phage_res, 0.04f * rate, 0, 0.95f);
    g.div_rate  = mut(d, g.div_rate, 0.03f * rate, 0.6f, 1.6f);
    g.ph_opt    = mut(d, g.ph_opt, 0.02f * rate, 0, 1);
    return g;
}

float dish_cell_radius(const cell_t *c) { return SP[c->species].radius * c->g.size; }
/* how close two cells may come: the rods and chains are longer than their radius */
static float body_radius(const cell_t *c) {
    float k = c->species == SP_BACILLUS || c->species == SP_MYXO || c->species == SP_CYANO ? 1.7f : c->species == SP_SPIRILLUM ? 2.2f : 1;
    return dish_cell_radius(c) * (c->state == CS_SPORE ? 0.8f : k);
}

/* ---- strains and cells ---- */
int dish_add_strain(dish_t *d, species_t sp, const char *name) {
    for (int s = 0; s < DISH_MAX_STRAINS; s++)
        if (!d->strain[s].used) {
            strain_t *st = &d->strain[s];
            memset(st, 0, sizeof *st);
            st->used = true; st->species = (uint8_t)sp;
            strncpy(st->name, name ? name : SPECIES_NAMES[sp], sizeof st->name - 1);
            st->strat = (strat_t){ sp == SP_PHAGE ? ST_HUNT : ST_FORAGE, -1, 5, 1, 0 };
            st->other[0] = st->other[1] = st->other[2] = -1;
            st->vault = dish_species_genome(sp);
            return s;
        }
    return -1;
}
int dish_spawn(dish_t *d, int strain, float x, float y, const genome_t *g) {
    if (strain < 0 || !d->strain[strain].used) return -1;
    for (int i = 0; i < DISH_MAX_CELLS; i++)
        if (d->cell[i].state == CS_FREE) {
            cell_t *c = &d->cell[i];
            memset(c, 0, sizeof *c);
            c->species = d->strain[strain].species; c->strain = (uint8_t)strain;
            c->state = CS_LIVE;
            c->x = x; c->y = y;
            c->g = g ? *g : dish_species_genome(c->species);
            c->ang = dish_rand(d, 0, TAU);
            c->energy = 0.55f; c->health = 1;
            c->phase = dish_rand(d, 0, TAU);
            c->run_t = dish_rand(d, 0.2f, 1.5f);
            c->id = ++d->next_id; c->comfort = 1;
            return i;
        }
    return -1;
}
static void seed_colony(dish_t *d, int s, int n, float x, float y, float spread, const genome_t *g) {
    for (int k = 0; k < n; k++) {
        float a = dish_rand(d, 0, TAU), r = dish_rand(d, 0, spread);
        float px = x + cosf(a) * r, py = y + sinf(a) * r;
        if (!dish_inside(px, py, 14)) { px = x; py = y; }
        dish_spawn(d, s, px, py, g);
    }
}

void dish_init(dish_t *d, uint32_t seed, species_t pet, const char *pet_name) {
    memset(d, 0, sizeof *d);
    d->rng = seed ? seed : 0x2545F491u;
    field_mask();
    d->temp = 0.5f; d->ph = 0.5f; d->salt = 0.2f; d->light = true;
    d->outbreak_t = dish_rand(d, 240, 480);
    for (int c = 0; c < FIELD_N * FIELD_N; c++) if (g_in[c]) d->nutr[c] = 0.12f;   /* fresh agar: a little to start on */
    int ps = dish_add_strain(d, pet, pet_name);
    seed_colony(d, ps, 6, DISH_CX - DISH_RX * 0.25f, DISH_CY, 20, NULL);
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (d->cell[i].state == CS_LIVE) d->cell[i].energy = dish_rand(d, 0.7f, 0.95f);   /* the first divisions come soon */
    if (SP[pet].prey > 0) {                          /* a hunter starts with something to hunt: a lawn of coccus */
        int ls = dish_add_strain(d, SP_COCCUS, NULL);
        seed_colony(d, ls, 10, DISH_CX + DISH_RX * 0.3f, DISH_CY, 26, NULL);
        d->strain[ls].stay_t = 1e9f;
    }
    d->run = 1;
    d->offer[0] = d->offer[1] = d->offer[2] = 0xFF;
    d->next_draft_gen = DRAFT_GENS;
    d->visit_t = dish_rand(d, 120, 180);             /* the first visitor after a couple of minutes alone */
    d->ink = DISH_WALLS;
    d->quest_cd = 20;                                /* (the first request comes soon) */
    d->wave_cd = dish_rand(d, 240, 330);
}
void dish_new_run(dish_t *d, uint32_t seed, species_t pet, const char *pet_name) {
    uint32_t dna = d->dna, badges = d->badges, nb = d->badges_new, dex = d->dex, unl = d->unlocked, hints = d->hints; uint16_t run = d->run;
    uint16_t qw = d->quests_won, ww = d->waves_won;
    bool over = d->run_over;
    dish_init(d, seed, pet, pet_name);
    d->dna = dna; d->badges = badges; d->badges_new = nb; d->dex = dex; d->unlocked = unl; d->hints = hints;
    d->quests_won = qw; d->waves_won = ww;
    d->run = over ? run + 1 : run ? run : 1;      /* (the very first setup is run 1, not a second run) */
    d->run_dna0 = dna;
}

/* ---- the keeper's hands ---- */
void dish_poke(dish_t *d, int i) {
    if (i < 0 || i >= DISH_MAX_CELLS || d->cell[i].state != CS_LIVE) return;
    cell_t *c = &d->cell[i];
    float a = dish_rand(d, 0, TAU);
    c->vx += cosf(a) * 60; c->vy += sinf(a) * 60; c->spin += dish_rand(d, -6, 6);
    part_add(d, PT_RIPPLE, c->x, c->y, 0, 0, 0.5f, 0xBFE8FF);
}
void dish_flick(dish_t *d, int i, float vx, float vy) {
    if (i < 0 || i >= DISH_MAX_CELLS || d->cell[i].state != CS_LIVE) return;
    cell_t *c = &d->cell[i];
    float sp = sqrtf(vx * vx + vy * vy), mx = 700;
    if (sp > mx) { vx *= mx / sp; vy *= mx / sp; }
    c->vx = vx; c->vy = vy; c->stun_t = fmaxf(c->stun_t, 0.7f); c->tether = 0; c->host = 0;
    if (c->strain != DISH_PET && !SPECIES_FRIENDLY[c->species]) d->flicks++;
    for (int k = 0; k < 4; k++) part_add(d, PT_SPARK, c->x, c->y, -vx * 0.1f + dish_rand(d, -20, 20), -vy * 0.1f + dish_rand(d, -20, 20), 0.4f, 0xFFFFFF);
}
int dish_wall_begin(dish_t *d, float x, float y) {
    if (d->ink < 1) return -1;
    int w = 0;
    for (int k = 1; k < DISH_WALLS; k++) if (d->wall[k].t < d->wall[w].t) w = k;   /* a free slot, or the oldest */
    d->ink -= 1;
    d->wall[w].n = 1; d->wall[w].t = DISH_WALL_LIFE; d->wall[w].x[0] = x; d->wall[w].y[0] = y;
    return w;
}
void dish_wall_add(dish_t *d, int w, float x, float y) {
    if (w < 0 || w >= DISH_WALLS || d->wall[w].t <= 0) return;
    int n = d->wall[w].n;
    if (n >= DISH_WALL_PTS) return;
    float dx = x - d->wall[w].x[n - 1], dy = y - d->wall[w].y[n - 1];
    if (dx * dx + dy * dy < 9 * 9) return;           /* (a point every 9 px: 24 points draw a ~200 px wall) */
    d->wall[w].x[n] = x; d->wall[w].y[n] = y; d->wall[w].n = (uint8_t)(n + 1);
}
void dish_feed(dish_t *d, float x, float y, float amount) {
    if (!dish_inside(x, y, 6)) return;
    splat(d->nutr, x, y, amount * 0.9f, 26, 1.0f);
    part_add(d, PT_DROP, x, y, 0, 0, 0.35f, 0xFFD27A);
    dish_prog_care(d, CARE_FEED);
    emit(DEV_FEED, -1);
}
void dish_stir(dish_t *d, float x, float y, float dx, float dy) {
    float vx = clampf(dx * 25, -220, 220), vy = clampf(dy * 25, -220, 220);
    for (int j = 0; j < FIELD_N; j++)
        for (int i = 0; i < FIELD_N; i++) {
            int c = j * FIELD_N + i;
            float ex = (i + 0.5f) * FCW - x, ey = (j + 0.5f) * FCH - y;
            float w = expf(-(ex * ex + ey * ey) / (2 * 30.0f * 30.0f));
            if (w < 0.03f) continue;
            d->flowx[c] += (vx - d->flowx[c]) * w * 0.6f;
            d->flowy[c] += (vy - d->flowy[c]) * w * 0.6f;
        }
}
void dish_warm(dish_t *d, float x, float y, float dt) {
    d->warm_x = x; d->warm_y = y;
    d->warm = clampf(d->warm + dt * 0.25f, 0, 0.45f);
}
void dish_rap(dish_t *d, float x, float y) { d->scare_t = 2.5f; d->scare_x = x; d->scare_y = y; }
void dish_dose(dish_t *d, float x, float y, float amount) {
    splat(d->drug, x, y, amount, 34, 1.0f);
    part_add(d, PT_DROP, x, y, 0, 0, 0.35f, 0x9CF0FF);
}
void dish_antiviral(dish_t *d) {
    dish_prog_care(d, CARE_CURE);
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        cell_t *c = &d->cell[i];
        if (c->state == CS_LIVE && c->species == SP_PHAGE && !c->host) { c->state = CS_DYING; c->dying_t = 0; }
    }
}
void dish_wipe(dish_t *d, float x, float y, float r) {
    if (dish_field(d->waste, x, y) > 0.2f) dish_prog_care(d, CARE_CLEAN);
    for (int j = 0; j < FIELD_N; j++)
        for (int i = 0; i < FIELD_N; i++) {
            float ex = (i + 0.5f) * FCW - x, ey = (j + 0.5f) * FCH - y;
            if (ex * ex + ey * ey < r * r) d->waste[j * FIELD_N + i] *= 0.2f;
        }
}
void dish_uv(dish_t *d, float seconds) { d->uv_t = seconds; }
static int strain_of(const dish_t *d, species_t sp);
/* the living cells of a strain right now (the census's count is a tick old) */
static int strain_cells(const dish_t *d, int s) {
    int n = 0;
    for (int i = 0; i < DISH_MAX_CELLS; i++) n += d->cell[i].strain == s && (d->cell[i].state == CS_LIVE || d->cell[i].state == CS_SPORE);
    return n;
}
int dish_add_sample(dish_t *d, species_t sp) {
    int s = strain_of(d, sp);
    if (s >= 0 && strain_cells(d, s) > 0) return -1;        /* here already */
    if (s < 0) s = dish_add_strain(d, sp, NULL);            /* (a strain that died out keeps its slot: it is simply reseeded) */
    if (s < 0) return -1;
    d->strain[s].extinct_t = 0;
    float a = dish_rand(d, 0, TAU), x = DISH_CX + cosf(a) * DISH_RX * 0.45f, y = DISH_CY + sinf(a) * DISH_RY * 0.45f;
    seed_colony(d, s, sp == SP_MYXO ? 4 : 6, x, y, 16, NULL);
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (d->cell[i].strain == s && d->cell[i].state == CS_LIVE) d->cell[i].energy = 0.8f;
    splat(d->nutr, x, y, 0.3f, 22, 1.0f);
    part_add(d, PT_DROP, x, y, 0, 0, 0.35f, 0xBFFFD0);
    return s;
}
static int strain_of(const dish_t *d, species_t sp) {
    for (int s = 0; s < DISH_MAX_STRAINS; s++) if (d->strain[s].used && d->strain[s].species == sp) return s;
    return -1;
}
bool dish_visitor_here(const dish_t *d, species_t sp) {
    for (int s = 1; s < DISH_MAX_STRAINS; s++) if (d->strain[s].used && d->strain[s].species == sp && strain_cells(d, s) > 0) return true;
    return false;
}
int dish_visit(dish_t *d, species_t sp) {
    if (dish_visitor_here(d, sp)) return -1;
    if (sp == SP_PHAGE) { dish_outbreak(d, 6); return strain_of(d, SP_PHAGE); }
    int s = dish_add_strain(d, sp, NULL);
    if (s < 0) return -1;
    static const uint8_t N[SP_COUNT] = { 5, 5, 5, 6, 4, 6, 4, 4, 6, 5, 5, 2, 3 };
    float a = dish_rand(d, 0, TAU), x = DISH_CX + cosf(a) * DISH_RX, y = DISH_CY + sinf(a) * DISH_RY;
    dish_toward_inside(&x, &y, sp == SP_MOLD ? 8 : 22);           /* in at the glass's edge (the mold stays on it) */
    seed_colony(d, s, N[sp], x, y, sp == SP_MOLD ? 10 : 14, NULL);
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (d->cell[i].strain == s && d->cell[i].state == CS_LIVE) d->cell[i].energy = 0.8f;
    d->strain[s].stay_t = SPECIES_FRIENDLY[sp] ? dish_rand(d, 300, 420) : dish_rand(d, 360, 540);
    dish_story_note(d, SE_VISIT, sp, 0, 0, 0);
    emit(DEV_VISIT, sp);
    return s;
}
void dish_outbreak(dish_t *d, int n) {
    int s = strain_of(d, SP_PHAGE);
    if (s < 0) s = dish_add_strain(d, SP_PHAGE, NULL);
    float a0 = dish_rand(d, 0, TAU);
    for (int k = 0; k < n; k++) {
        float a = a0 + dish_rand(d, -0.4f, 0.4f);
        float x = DISH_CX + cosf(a) * (DISH_RX - 18), y = DISH_CY + sinf(a) * (DISH_RY - 18);
        dish_toward_inside(&x, &y, 18);              /* (in from a corner, where the glass curves) */
        dish_spawn(d, s, x, y, NULL);
    }
    dish_story_note(d, SE_VISIT, SP_PHAGE, 0, 0, 0);
    emit(DEV_OUTBREAK, s);
}

int dish_cell_at(const dish_t *d, float x, float y, float slop) {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        const cell_t *c = &d->cell[i];
        if (c->state != CS_LIVE && c->state != CS_SPORE) continue;
        float dx = c->x - x, dy = c->y - y, dd = dx * dx + dy * dy, r = dish_cell_radius(c) + slop;
        if (dd < r * r && dd < bd) { bd = dd; best = i; }
    }
    return best;
}

/* ---- the neighbor grid (rebuilt every tick) ---- */
#define GRID_PX 16
#define GW ((DISH_W + GRID_PX - 1) / GRID_PX)
#define GH ((DISH_H + GRID_PX - 1) / GRID_PX)
static int16_t g_head[GW * GH], g_next[DISH_MAX_CELLS];
static void grid_build(const dish_t *d) {
    for (int i = 0; i < GW * GH; i++) g_head[i] = -1;
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        const cell_t *c = &d->cell[i];
        if (c->state != CS_LIVE && c->state != CS_SPORE) continue;
        int gx = (int)(c->x / GRID_PX), gy = (int)(c->y / GRID_PX);
        if (gx < 0) { gx = 0; } if (gx >= GW) { gx = GW - 1; }
        if (gy < 0) { gy = 0; } if (gy >= GH) { gy = GH - 1; }
        g_next[i] = g_head[gy * GW + gx]; g_head[gy * GW + gx] = (int16_t)i;
    }
}

/* ---- life and death ---- */
static void kill(dish_t *d, int i, bool burst, int cause) {
    cell_t *c = &d->cell[i];
    if (c->state == CS_DYING || c->state == CS_FREE) return;
    c->state = CS_DYING; c->dying_t = 0;
    strain_t *st = &d->strain[c->strain];
    if (SP[c->species].virus == SP[st->species].virus) {   /* (a tamed phage fading is no loss of the strain's) */
        st->deaths++;
        dish_story_death(d, c->strain, cause);
    }
    if (SP[c->species].virus) return;            /* a phage just fades */
    splat(d->nutr, c->x, c->y, 0.10f, 10, 1.0f); /* the remains feed the others */
    uint32_t rgb = burst ? 0xC07CFF : 0xFFFFFF;
    for (int k = 0; k < 6; k++) {
        float a = k * TAU / 6 + dish_rand(d, -0.3f, 0.3f), sp = dish_rand(d, 20, 50);
        part_add(d, PT_SHARD, c->x, c->y, cosf(a) * sp, sinf(a) * sp, 0.6f, rgb);
    }
    /* spore burst: a dying cell leaves spores that drift off and hatch later */
    int sb = st->mut[M_SPOREBURST];
    if (sb && cause != DIE_OLD) {
        cell_t parent = *c;
        for (int k = 0; k < sb; k++) {
            float a = dish_rand(d, 0, TAU);
            int j = dish_spawn(d, parent.strain, parent.x + cosf(a) * 6, parent.y + sinf(a) * 6, &parent.g);
            if (j < 0) break;
            cell_t *n = &d->cell[j];
            n->state = CS_SPORE; n->hatch_t = dish_rand(d, 15, 25); n->energy = 0.5f;
            n->vx = cosf(a) * 25; n->vy = sinf(a) * 25; n->gen = parent.gen;
        }
    }
    emit(burst ? DEV_BURST : DEV_POP, c->strain);
}
static void divide(dish_t *d, int i) {
    cell_t *c = &d->cell[i];
    float r = dish_cell_radius(c);
    float ax = cosf(c->ang), ay = sinf(c->ang);
    cell_t parent = *c;
    genome_t g = mutate(d, &parent.g, d->strain[c->strain].strat.id == ST_EXPLORE ? 1.5f : 1.0f);
    int j = dish_spawn(d, c->strain, c->x + ax * r, c->y + ay * r, &g);
    c = &d->cell[i];
    c->div_t = 0;
    if (j < 0) return;                            /* no room for a daughter: the pinch undoes, the energy is kept */
    c->x -= ax * r * 0.5f; c->y -= ay * r * 0.5f;
    c->energy = parent.energy * 0.45f;
    strain_t *st = &d->strain[c->strain];
    cell_t *n = &d->cell[j];
    n->energy = c->energy; n->ang = c->ang + dish_rand(d, -0.3f, 0.3f);
    n->gen = parent.gen + 1; n->health = parent.health;
    n->vx = ax * 8; n->vy = ay * 8;
    st->births++;
    if (n->gen > st->max_gen) st->max_gen = n->gen;
    dish_story_birth(d, c->strain);
    emit(DEV_DIVIDE, c->strain);
}

/* the warmth at a point: the dish's, plus the finger's light (a held light warms a little) */
static float beam_at(const dish_t *d, float x, float y) {
    if (d->beam <= 0.01f) return 0;
    float dx = x - d->beam_x, dy = y - d->beam_y;
    return d->beam * expf(-(dx * dx + dy * dy) / (2 * 42.0f * 42.0f));
}
static float local_temp(const dish_t *d, float x, float y) { return d->temp + 0.15f * beam_at(d, x, y); }
float dish_light_at(const dish_t *d, float x, float y) { return (d->light ? 0.45f : 0) + beam_at(d, x, y); }
/* what the dish itself does to a cell: too hot or cold, too sour or soapy, too salty.
 * The same numbers are its comfort (the pet's need). */
float dish_stress(const dish_t *d, const cell_t *c, float *comfort) {
    float mt = fabsf(local_temp(d, c->x, c->y) - c->g.heat_opt);
    float mp = fabsf(d->ph - c->g.ph_opt);
    float ms = d->salt - (0.25f + c->g.salt_tol * 0.6f);          /* salt hurts only above what it tolerates */
    float ct = 1 - mt * 2.2f, cp = 1 - mp * 2.2f, cs = 1 - (ms > 0 ? ms * 3 : 0);
    if (comfort) *comfort = clampf(fminf(ct, fminf(cp, cs)), 0, 1);
    return (mt > 0.2f ? (mt - 0.2f) * 0.35f : 0) + (mp > 0.22f ? (mp - 0.22f) * 0.35f : 0) + (ms > 0 ? ms * 0.5f : 0);
}

/* the cells a hunter could eat: every other strain's, phages aside (last tick's census) */
static int prey_count(const dish_t *d, int strain) {
    int n = 0;
    for (int s = 0; s < DISH_MAX_STRAINS; s++)
        if (s != strain && d->strain[s].used && !SP[d->strain[s].species].virus) n += d->strain[s].count;
    return n;
}
/* how much of a bite reaches a cell: its strain's mimicry (hunters mistake it for their own) */
static float bite_k(const dish_t *d, const cell_t *o) { return 1 - 0.25f * d->strain[o->strain].mut[M_MIMIC]; }
/* gene transfer: a free draft card, now and then (only when no draft is waiting: they never pile up) */
static void gift(dish_t *d) {
    if (d->gift_cd > 0 || d->run_over || d->drafts) return;
    d->gift_cd = GIFT_CD;
    d->drafts = 1;
    dish_story_note(d, SE_GIFT, d->strain[DISH_PET].species, 0, 0, 0);
    emit(DEV_GIFT, DISH_PET);
}
/* a cell's role in its strain's build, fixed by its id: a wall anchor, a scout */
static bool is_anchor(const cell_t *c, const uint8_t *mu) { return mu[M_BIOFILM] && !SP[c->species].virus && (c->id * 2654435761u >> 24) % 100 < 12u * mu[M_BIOFILM]; }
static bool is_scout(const cell_t *c, const uint8_t *mu) { return mu[M_SCOUTS] && !SP[c->species].virus && (c->id * 40503u >> 8) % 100 < 6u * mu[M_SCOUTS] && !is_anchor(c, mu); }
bool dish_cell_anchor(const dish_t *d, const cell_t *c) { return is_anchor(c, d->strain[c->strain].mut); }
bool dish_cell_scout(const dish_t *d, const cell_t *c) { return is_scout(c, d->strain[c->strain].mut); }
/* is a strain one that d's strain s should treat as something to fear (it hunts s) */
static bool hunts(const dish_t *d, int hunter, int prey) {
    const strain_t *h = &d->strain[hunter];
    return h->used && h->count && h->strat.id == ST_HUNT && (h->strat.target == prey || h->strat.target < 0);
}

static void cell_tick(dish_t *d, int i, float dt) {
    cell_t *c = &d->cell[i];
    const sp_info_t *sp = &SP[c->species];
    strain_t *st = &d->strain[c->strain];
    const uint8_t *mu = st->mut;                  /* the strain's mutations (the pet's build; visitors have none) */
    int strat = st->strat.id, tgt = st->strat.target;
    if (tgt >= 0 && (!d->strain[tgt].used || !d->strain[tgt].count)) tgt = -1;
    if (c->state != CS_SPORE) c->age += dt;      /* spores don't age */
    c->phase += dt * (2.0f + c->g.speed * 3);

    if (c->state == CS_DYING) {
        c->dying_t += dt;
        if (c->dying_t > 0.5f) c->state = CS_FREE;
        return;
    }
    float r = dish_cell_radius(c);
    if (c->stun_t > 0) c->stun_t -= dt;
    bool stunned = c->stun_t > 0;
    if (c->jet_t > 0) { c->jet_t -= dt; if (c->jet_t <= 0) c->jet_t = -4.0f + 0.8f * mu[M_JET]; }   /* then it recharges */
    else if (c->jet_t < 0) c->jet_t = fminf(0, c->jet_t + dt);

    /* ---- spores: a strain that chose to sporulate goes dormant; a burst spore hatches on its own clock ---- */
    if (!sp->virus) {
        if (c->hatch_t > 0) { if ((c->hatch_t -= dt) <= 0) { c->hatch_t = 0; c->state = CS_LIVE; } }
        else if (c->state == CS_LIVE && strat == ST_SPORE && dish_rand(d, 0, 1) < dt * 0.8f) { c->state = CS_SPORE; c->div_t = 0; }
        else if (c->state == CS_SPORE && strat != ST_SPORE && dish_rand(d, 0, 1) < dt * 0.6f) c->state = CS_LIVE;
    }
    bool spore = c->state == CS_SPORE;

    /* ---- a phage clamped on its host: inject, then it is spent (or, on a tamer, it joins) ---- */
    if (sp->virus && c->host) {
        cell_t *h = &d->cell[c->host - 1];
        if (h->state != CS_LIVE) { c->host = 0; }
        else {
            float hr = dish_cell_radius(h) + 1.5f;
            c->x = h->x + cosf(c->ang) * hr; c->y = h->y + sinf(c->ang) * hr;
            c->div_t += dt;                                      /* (reused as the injection clock) */
            if (c->div_t > 1.2f) {
                int tame = d->strain[h->strain].mut[M_TAMER];
                if (tame && dish_rand(d, 0, 1) < 0.3f * tame) {  /* the tamer turns it: a phage of its own */
                    c->strain = h->strain; c->host = 0; c->health = 1; c->div_t = 0;
                    c->vx = cosf(c->ang) * 30; c->vy = sinf(c->ang) * 30;
                    if (h->strain == DISH_PET) dish_story_note(d, SE_TAMED, d->strain[DISH_PET].species, SP_PHAGE, 0, 0);
                    return;
                }
                if (h->infect <= 0) emit(DEV_INFECT, h->strain);
                h->infect += 0.35f; h->hurt = DIE_VIRUS;
                c->state = CS_FREE;
            }
            return;
        }
    }

    /* ---- metabolism, food, the light ---- */
    float comfort, env = dish_stress(d, c, &comfort);
    c->comfort = comfort;
    bool anchor = is_anchor(c, mu);
    float metab = sp->metab * (0.6f + 0.4f * c->g.speed * c->g.size) * (strat == ST_REST || anchor ? 0.45f : 1) * (spore ? 0.05f : 1);
    int fc = fidx(c->x, c->y);
    if (!sp->virus) {
        c->energy -= metab * dt;
        if (!spore) {
            float food = d->nutr[fc];
            float take = sp->eat * food * dt * (0.4f + 0.6f * comfort) * (c->energy < 1.05f ? 1 : 0);
            if (take * 2.0f > food) take = food * 0.5f;
            d->nutr[fc] -= take * 2.0f;                                   /* a full field cell is one daughter's worth */
            c->energy += take;
            float photo = sp->photo + 0.005f * mu[M_PHOTO];               /* the light feeds the green ones */
            if (photo > 0) c->energy += photo * comfort * dt * fminf(1.5f, dish_light_at(d, c->x, c->y) * (1 + 0.5f * mu[M_PHOTO]));
            d->waste[fc] = clampf(d->waste[fc] + dt * 0.0010f, 0, 1);
            if (c->species == SP_MOLD) d->drug[fc] = fminf(1, d->drug[fc] + 0.08f * dt);        /* the mold's antibiotic */
            if (c->species == SP_SYMBIONT) d->nutr[fc] = fminf(1, d->nutr[fc] + 0.012f * dt);  /* the symbiont's gift */
        }
        /* health: what hurts (and so what it would die of), what heals */
        float drug = dish_field(d->drug, c->x, c->y) * (1 - c->g.drug_res) * (1 - c->g.drug_res) * 0.6f;   /* squared: a little resistance goes a long way */
        float filth = fmaxf(0, dish_field(d->waste, c->x, c->y) - 0.45f) * 0.12f;
        float dmg = env + drug + filth;
        if (drug > env && drug > 0.004f) c->hurt = DIE_POISON; else if (env > 0.004f) c->hurt = DIE_HARSH;
        /* a wall: its anchors take half, and cells sheltering by them take less */
        if (anchor) dmg *= 0.5f;
        else if (mu[M_BIOFILM] && c->nbrs >= 2) dmg *= 1 - 0.15f * mu[M_BIOFILM];
        bool old = c->age > 360 + 180 * (((uint32_t)i * 2654435761u >> 16) & 1023) / 1023.0f;
        if (old) { dmg += 0.006f; if (c->hurt == DIE_STARVED) c->hurt = DIE_OLD; }
        if (c->strain != DISH_PET && st->stay_t <= 0) dmg += 0.05f;   /* a visitor whose time is up: it withers and is gone */
        if (spore) dmg *= 0.08f;
        /* one phage's payload is a cold a healthy cell shakes off; two take hold */
        if (c->infect >= 0.5f) { c->infect += dt * 0.05f; c->energy -= dt * 0.006f; }
        else if (c->infect > 0) c->infect = fmaxf(0, c->infect - dt * 0.03f * c->health);
        c->health = clampf(c->health - dmg * dt + (dmg < 0.01f && !old && c->energy > 0.3f ? 0.03f * dt : 0), 0, 1);
        if (c->infect >= 1) {                       /* the burst: new phages pour out */
            int ps = strain_of(d, SP_PHAGE);
            float hx = c->x, hy = c->y;
            kill(d, i, true, DIE_VIRUS);
            if (ps >= 0) for (int k = 0; k < 3; k++) {
                int j = dish_spawn(d, ps, hx + dish_rand(d, -4, 4), hy + dish_rand(d, -4, 4), NULL);
                if (j >= 0) { float a = dish_rand(d, 0, TAU); d->cell[j].vx = cosf(a) * 30; d->cell[j].vy = sinf(a) * 30; }
            }
            return;
        }
        if (c->energy <= 0) { kill(d, i, false, DIE_STARVED); return; }
        if (c->health <= 0) { kill(d, i, false, c->hurt); return; }
    } else {
        c->health -= dt / (c->strain == DISH_PET ? 90.0f : 45.0f);   /* a free phage does not last (a tamed one a little longer) */
        if (c->health <= 0) { kill(d, i, false, DIE_OLD); return; }
    }

    /* ---- division (a quorum bloom lowers the bar for a while) ---- */
    if (!sp->virus && !spore && !anchor) {
        float need = 0.85f - 0.15f * (c->g.div_rate - 1) - (strat == ST_DIVIDE ? 0.12f : 0) - (st->bloom_t < 0 ? 0.2f : 0);
        if (c->div_t > 0) {
            c->div_t += dt * c->g.div_rate;
            if (c->div_t >= DIV_TIME) { divide(d, i); return; }
        } else if (c->energy >= need && c->health > 0.5f && d->n_live < DISH_MAX_CELLS - 32   /* ponytail: the last 32 slots are the phages' */
                   && (sp->prey <= 0 || st->count * 3 < prey_count(d, c->strain)))           /* a hunter breeds only while prey outnumbers it 3 to 1 */
            c->div_t = 0.001f;
    }

    /* ---- where to go: the strain's goal, and its target ---- */
    float speed = SP[c->species].speed * c->g.speed;
    bool scout = is_scout(c, mu);
    if (scout) speed = fmaxf(speed, 30) * 1.8f;
    if (c->jet_t > 0) speed = fmaxf(speed, 30) * (1.8f + 0.4f * mu[M_JET]);
    float mult = 1;
    switch (strat) {
    case ST_REST:    mult = 0.15f; break;
    case ST_DIVIDE:  mult = 0.4f; break;
    case ST_CLUSTER: mult = 0.8f; break;
    case ST_FLEE:    mult = 1.3f; break;
    case ST_HUNT:    mult = 1.2f; break;
    }
    if (spore || c->div_t > 0 || stunned || anchor) mult = spore || anchor ? 0 : 0.05f;
    float urg = 0.8f + st->strat.urgency * 0.05f;   /* the AI's urgency: a little faster when it matters */
    float hx = cosf(c->ang), hy = sinf(c->ang);
    if (speed > 8 && !spore && !anchor) {
        /* run and tumble: keep running while things get better, tumble when they don't */
        c->run_t -= dt;
        if (c->run_t <= 0) {
            float f = dish_field(d->nutr, c->x, c->y) - dish_field(d->drug, c->x, c->y);   /* (all shy from the drug: resistance is what survives it, not what flees it) */
            bool better = f > c->last_food + 0.002f;
            if (better && strat != ST_EXPLORE) c->run_t = dish_rand(d, 1.0f, 2.5f);
            else { c->ang += dish_rand(d, -2.6f, 2.6f) * (1 - 0.28f * mu[M_SWARM]); c->run_t = dish_rand(d, 0.4f, 1.2f); }   /* (a school barely tumbles) */
            c->last_food = f;
        }
    }
    float sx = 0, sy = 0;
    #define TOWARD(X, Y, K) do { float _dx = (X) - c->x, _dy = (Y) - c->y, _d = sqrtf(_dx * _dx + _dy * _dy) + 1; sx += _dx / _d * (K); sy += _dy / _d * (K); } while (0)
    const strain_t *ts = tgt >= 0 ? &d->strain[tgt] : NULL;
    if (scout) {                                  /* a scout roams; on rich food it tells the colony where */
        if (dish_field(d->nutr, c->x, c->y) > 0.3f) { st->wx = c->x; st->wy = c->y; }
        if (c->run_t > 1.5f) c->run_t = 1.5f;
    } else switch (strat) {
    case ST_FORAGE: case ST_DIVIDE: {
        float e = 6, gain = 40;
        sx += (dish_field(d->nutr, c->x + e, c->y) - dish_field(d->nutr, c->x - e, c->y)) * gain;
        sy += (dish_field(d->nutr, c->x, c->y + e) - dish_field(d->nutr, c->x, c->y - e)) * gain;
        if (mu[M_SCOUTS] && st->wx > 0) TOWARD(st->wx, st->wy, 0.7f);    /* where the scouts found food */
        break; }
    case ST_CLUSTER: if (st->count > 1) { float dx = st->cx - c->x, dy = st->cy - c->y; if (dx * dx + dy * dy > 28 * 28) TOWARD(st->cx, st->cy, 1); } break;
    case ST_EXPLORE: if (st->wx > 0) TOWARD(st->wx, st->wy, 0.8f); break;
    case ST_SEEK_LIGHT: if (d->beam > 0.05f) TOWARD(d->beam_x, d->beam_y, 1.2f); break;
    case ST_AVOID_LIGHT: if (d->beam > 0.05f && beam_at(d, c->x, c->y) > 0.01f) TOWARD(2 * c->x - d->beam_x, 2 * c->y - d->beam_y, 1.4f); break;
    case ST_FLEE:
        if (ts) {
            TOWARD(2 * c->x - ts->cx, 2 * c->y - ts->cy, 1.6f);
            float dx = ts->cx - c->x, dy = ts->cy - c->y;
            if (mu[M_JET] && c->jet_t == 0 && dx * dx + dy * dy < 60 * 60) c->jet_t = 0.6f + 0.3f * mu[M_JET];   /* jet away */
        }
        break;
    case ST_GUARD:
        if (ts) {                                 /* between what it guards and whatever hunts that */
            float gx = ts->cx, gy = ts->cy;
            for (int s2 = 0; s2 < DISH_MAX_STRAINS; s2++)
                if (s2 != tgt && s2 != c->strain && hunts(d, s2, tgt)) { gx = ts->cx * 0.4f + d->strain[s2].cx * 0.6f; gy = ts->cy * 0.4f + d->strain[s2].cy * 0.6f; break; }
            TOWARD(gx, gy, 1);
        }
        break;
    case ST_BEFRIEND: if (ts) { float dx = ts->cx - c->x, dy = ts->cy - c->y; if (dx * dx + dy * dy > 30 * 30) TOWARD(ts->cx, ts->cy, 1); } break;
    }
    /* the lure: foreign, unfriendly cells within reach of a glowing colony are drawn to it */
    const strain_t *pet = &d->strain[DISH_PET];
    if (c->strain != DISH_PET && pet->mut[M_LURE] && pet->count && !SPECIES_FRIENDLY[c->species]) {
        float dx = pet->cx - c->x, dy = pet->cy - c->y, reach = 80 + 30 * pet->mut[M_LURE];
        if (dx * dx + dy * dy < reach * reach) TOWARD(pet->cx, pet->cy, 0.6f * pet->mut[M_LURE]);
    }
    /* neighbors: soft collisions, adhesion, bites, the build's verbs, the crowd */
    int gx = (int)(c->x / GRID_PX), gy = (int)(c->y / GRID_PX);
    float px = 0, py = 0, ax = 0, ay = 0; int crowd = 0, touching = 0, flock = 0;
    int prey = -1; float prey_d = sp->virus ? 50 * 50 : c->species == SP_CILIATE ? 140 * 140 : 90 * 90;
    bool hunting = strat == ST_HUNT && !stunned;
    for (int yy = gy - 1; yy <= gy + 1; yy++) {
        if (yy < 0 || yy >= GH) continue;
        for (int xx = gx - 1; xx <= gx + 1; xx++) {
            if (xx < 0 || xx >= GW) continue;
            for (int j = g_head[yy * GW + xx]; j >= 0; j = g_next[j]) {
                if (j == i) continue;
                cell_t *o = &d->cell[j];
                if (o->state != CS_LIVE && o->state != CS_SPORE) continue;
                float dx = c->x - o->x, dy = c->y - o->y, dd2 = dx * dx + dy * dy;
                float rr = body_radius(c) + body_radius(o);
                bool o_virus = SP[o->species].virus;
                if (dd2 < 1e-4f) { dx = dish_rand(d, -1, 1); dy = dish_rand(d, -1, 1); dd2 = dx * dx + dy * dy + 1e-4f; }
                float dd = sqrtf(dd2);
                bool touch = dd < rr + 1, other = o->strain != c->strain;
                if (touch && other) { int a = c->strain < o->strain ? c->strain : o->strain, b = c->strain ^ o->strain ^ a; d->met |= 1ull << (a * DISH_MAX_STRAINS + b); }
                bool aimed = tgt < 0 || o->strain == tgt;                     /* (a hunt aims at its target strain) */
                if (touch && !o_virus) touching++;
                if (!other && !o_virus && mu[M_SWARM] && dd < 40) { ax += cosf(o->ang); ay += sinf(o->ang); flock++; }
                /* the guardian eats free viruses and hunters */
                if (c->species == SP_CILIATE && (o_virus ? !o->host && other : (o->species == SP_MYXO || o->species == SP_VIBRIO))) {
                    if (dd < rr * 0.8f + 2) { o->hurt = DIE_EATEN; kill(d, j, false, DIE_EATEN); continue; }
                    if (dd2 < prey_d) { prey_d = dd2; prey = j; }
                }
                if (!sp->virus && !o_virus && other && o->state == CS_LIVE) {
                    const uint8_t *om = d->strain[o->strain].mut;
                    if (touch && om[M_SPIKES]) { c->health -= 0.06f * om[M_SPIKES] * dt; c->hurt = DIE_EATEN; }   /* it touched something spiny */
                    if (mu[M_TOXIN] && !SPECIES_FRIENDLY[o->species] && dd < rr * 2 + 6 * mu[M_TOXIN]) { o->health -= 0.03f * mu[M_TOXIN] * dt; o->hurt = DIE_POISON; }
                    if (mu[M_ENGULF] && touch && !stunned && (hunting ? aimed : mu[M_ENGULF] >= 3) &&
                        body_radius(o) < body_radius(c) * (0.9f + 0.15f * mu[M_ENGULF]) && dish_rand(d, 0, 1) < 0.6f * mu[M_ENGULF] * dt) {
                        kill(d, j, false, DIE_EATEN); c->energy = fminf(1.1f, c->energy + 0.35f);   /* swallowed whole */
                        continue;
                    }
                    if (mu[M_GRAPPLE] && touch && !c->tether && !SPECIES_FRIENDLY[o->species] && dish_rand(d, 0, 1) < 0.8f * mu[M_GRAPPLE] * dt) c->tether = (uint8_t)(j + 1);
                    if (sp->prey > 0 && hunting && aimed && touch) {             /* a hunter's bite */
                        float bite = sp->prey * bite_k(d, o) * dt;
                        o->health -= bite; o->hurt = DIE_EATEN; c->energy += bite * (c->species == SP_VIBRIO ? 0.5f : 2.0f);
                    }
                    if ((sp->prey > 0 || mu[M_ENGULF] || mu[M_GRAPPLE]) && aimed && dd2 < prey_d && dish_rand(d, 0, 1) >= 0.25f * om[M_MIMIC]) { prey_d = dd2; prey = j; }
                    if (touch && c->strain == DISH_PET && (o->species == SP_DONOR || (mu[M_SYMBIOSIS] >= 3 && SPECIES_FRIENDLY[o->species]))) gift(d);   /* a gene, passed */
                    if (touch && c->strain == DISH_PET && mu[M_SYMBIOSIS] && SPECIES_FRIENDLY[o->species]) c->energy += 0.005f * mu[M_SYMBIOSIS] * dt;   /* friends share */
                }
                if (!sp->virus && !o_virus) {
                    if (dd < rr * 2.2f) crowd++;
                    if (dd < rr) { float push = (rr - dd) / rr * 60; px += dx / dd * push; py += dy / dd * push; }
                    else if ((sp->adhesive || mu[M_BIOFILM]) && !other && dd < rr * 1.35f) {
                        float pull = (dd - rr * 1.02f) * 4; px -= dx / dd * pull; py -= dy / dd * pull;
                    }
                } else if (sp->virus && !o_virus && !c->host && o->state == CS_LIVE && other) {   /* a phage: any cell not its own (a tamed one: its master's enemies) */
                    if (dd2 < prey_d) { prey_d = dd2; prey = j; }
                    if (dd < body_radius(o) + r) {
                        const uint8_t *om = d->strain[o->strain].mut;
                        float fail = o->g.phage_res + 0.1f * om[M_MIMIC];
                        if (dish_rand(d, 0, 1) >= fail) {               /* in: clamp on */
                            c->host = (uint8_t)(j + 1); c->div_t = 0;
                            c->ang = atan2f(c->y - o->y, c->x - o->x);
                            return;
                        }
                        px += dx / dd * 40; py += dy / dd * 40;     /* bounced off a resistant wall */
                    }
                }
            }
        }
    }
    c->nbrs = (uint8_t)(touching > 255 ? 255 : touching);
    /* grapple: hold on, drain it, drag it home */
    if (c->tether) {
        cell_t *o = &d->cell[c->tether - 1];
        float dx = o->x - c->x, dy = o->y - c->y;
        if (o->state != CS_LIVE || dx * dx + dy * dy > 45 * 45 || stunned) c->tether = 0;
        else {
            o->health -= 0.06f * mu[M_GRAPPLE] * dt; o->hurt = DIE_EATEN; c->energy += 0.03f * mu[M_GRAPPLE] * dt;
            float k = (0.5f + 0.3f * mu[M_GRAPPLE]) * dt;
            o->x -= dx * k; o->y -= dy * k;
        }
    }
    if (mu[M_SWARM] && flock) { ax /= flock; ay /= flock; sx += ax * 1.1f * mu[M_SWARM]; sy += ay * 1.1f * mu[M_SWARM];
                                if (st->count > 1) TOWARD(st->cx, st->cy, 0.25f * mu[M_SWARM]); }
    if (prey >= 0 && (hunting || sp->virus)) TOWARD(d->cell[prey].x, d->cell[prey].y, 1.5f);
    /* a hunter with nothing in reach heads for the colony it means to eat */
    if (prey < 0 && hunting && !sp->virus) {
        float best = 1e9f, tx = 0, ty = 0;
        for (int s2 = 0; s2 < DISH_MAX_STRAINS; s2++) {
            const strain_t *o = &d->strain[s2];
            if (!o->used || !o->count || s2 == c->strain || (tgt >= 0 && s2 != tgt)) continue;
            bool food = c->species == SP_CILIATE ? (o->species == SP_PHAGE || o->species == SP_MYXO || o->species == SP_VIBRIO) : !SP[o->species].virus;
            float dx = o->cx - c->x, dy = o->cy - c->y, dd2 = dx * dx + dy * dy;
            if (food && dd2 < best) { best = dd2; tx = o->cx; ty = o->cy; }
        }
        if (best < 1e9f) TOWARD(tx, ty, 1.2f);
    }
    if (strat == ST_EXPLORE && crowd > 0) { sx += px * 0.02f; sy += py * 0.02f; }
    if (d->scare_t > 0) {
        float dx = c->x - d->scare_x, dy = c->y - d->scare_y, dd = sqrtf(dx * dx + dy * dy) + 1;
        if (dd < 140) { sx += dx / dd * 3; sy += dy / dd * 3; mult = 2.2f; }
    }
    #undef TOWARD
    if (sx != 0 || sy != 0) {                       /* turn toward the steer */
        float want = atan2f(hy + sy, hx + sx), da = want - c->ang;
        while (da > 3.14159f) da -= TAU;
        while (da < -3.14159f) da += TAU;
        c->ang += clampf(da, -3 * dt, 3 * dt);
    }
    float tx = cosf(c->ang) * speed * mult * urg, ty = sinf(c->ang) * speed * mult * urg;
    float k = clampf(4 * dt, 0, 1);
    c->vx += (tx - c->vx) * k + dish_rand(d, -1, 1) * 18 * dt * !anchor;
    c->vy += (ty - c->vy) * k + dish_rand(d, -1, 1) * 18 * dt * !anchor;
    if (anchor) { c->vx *= 0.5f; c->vy *= 0.5f; px *= 0.1f; py *= 0.1f; }
    float drift = anchor ? 0 : 1;                  /* (the current and the tilt carry everything but a wall) */
    c->x += (c->vx + px + (d->flowx[fc] * 0.85f + d->tilt_x * 22) * drift) * dt;
    c->y += (c->vy + py + (d->flowy[fc] * 0.85f + d->tilt_y * 22) * drift) * dt;
    if (!sp->adhesive && speed <= 8) c->ang += c->spin * dt;
    if (sp->adhesive) c->ang += dish_rand(d, -0.3f, 0.3f) * dt;
    if (c->jet_t > 0 && !(c->id & 3)) part_add(d, PT_SPARK, c->x, c->y, -c->vx * 0.2f, -c->vy * 0.2f, 0.3f, 0xBFEFFF);   /* a jet's streak */

    /* the keeper's walls: nothing crosses one */
    for (int w = 0; w < DISH_WALLS; w++) {
        if (d->wall[w].t <= 0 || d->wall[w].n < 2) continue;
        for (int k = 0; k + 1 < d->wall[w].n; k++) {
            float ax = d->wall[w].x[k], ay = d->wall[w].y[k], bx = d->wall[w].x[k + 1], by = d->wall[w].y[k + 1];
            float vx = bx - ax, vy = by - ay, t = ((c->x - ax) * vx + (c->y - ay) * vy) / (vx * vx + vy * vy + 1e-6f);
            t = clampf(t, 0, 1);
            float qx = c->x - (ax + vx * t), qy = c->y - (ay + vy * t), q = sqrtf(qx * qx + qy * qy), need = r + 3;
            if (q >= need) continue;
            if (q < 1e-3f) { qx = -vy; qy = vx; q = sqrtf(qx * qx + qy * qy) + 1e-6f; }
            float nx = qx / q, ny = qy / q;
            c->x += nx * (need - q); c->y += ny * (need - q);
            float dot = c->vx * nx + c->vy * ny;
            if (dot < 0) { c->vx -= 2 * dot * nx; c->vy -= 2 * dot * ny; }
            if (c->strain != DISH_PET && !SPECIES_FRIENDLY[c->species] && d->wall_hits < 60000) d->wall_hits++;
        }
    }
    /* the glass: back inside, heading turned in */
    if (!dish_inside(c->x, c->y, r + 2)) {
        float nx, ny; dish_edge_normal(c->x, c->y, &nx, &ny);
        dish_toward_inside(&c->x, &c->y, r + 2.5f);
        float dot = c->vx * nx + c->vy * ny;
        if (dot > 0) { c->vx -= 2 * dot * nx; c->vy -= 2 * dot * ny; }
        if (cosf(c->ang) * nx + sinf(c->ang) * ny > 0) c->ang = atan2f(-ny, -nx) + dish_rand(d, -1, 1);
    }
}

/* ---- the census, the vault, the minds ---- */
static void census(dish_t *d) {
    float acc[DISH_MAX_STRAINS][8]; memset(acc, 0, sizeof acc);
    int cnt[DISH_MAX_STRAINS] = { 0 }, spores[DISH_MAX_STRAINS] = { 0 };
    int best[DISH_MAX_STRAINS]; for (int s = 0; s < DISH_MAX_STRAINS; s++) best[s] = -1;
    d->n_live = 0;
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        cell_t *c = &d->cell[i];
        if (c->state == CS_FREE) continue;
        d->n_live++;
        if (c->state == CS_DYING) continue;
        int s = c->strain;
        if (SP[c->species].virus != SP[d->strain[s].species].virus) continue;   /* (a tamed phage helps its strain; it isn't one of its cells) */
        cnt[s]++;
        if (c->state == CS_SPORE) spores[s]++;
        acc[s][0] += c->energy; acc[s][1] += c->health; acc[s][2] += c->comfort;   /* (cell_tick's, a tick old) */
        acc[s][3] += dish_field(d->waste, c->x, c->y); acc[s][4] += c->infect > 0;
        acc[s][5] += c->x; acc[s][6] += c->y;
        acc[s][7] += c->nbrs;
        if (best[s] < 0 || c->health + c->energy > d->cell[best[s]].health + d->cell[best[s]].energy) best[s] = i;
    }
    for (int s = 0; s < DISH_MAX_STRAINS; s++) {
        strain_t *st = &d->strain[s];
        if (!st->used) continue;
        st->count = cnt[s]; st->spores = spores[s];
        if (cnt[s]) {
            float n = (float)cnt[s];
            st->energy = acc[s][0] / n; st->health = acc[s][1] / n; st->comfort = acc[s][2] / n;
            st->filth = acc[s][3] / n; st->infected = acc[s][4] / n;
            st->cx = acc[s][5] / n; st->cy = acc[s][6] / n;
            st->crowd = clampf(acc[s][7] / n / 3, 0, 1);
            if (best[s] >= 0 && d->cell[best[s]].health > 0.6f && !SP[st->species].virus) st->vault = d->cell[best[s]].g;
        }
    }
}

/* the strains a mind sees: the three nearest others, by centroid (other1..3 in its state line) */
void dish_fill_others(dish_t *d, int s) {
    strain_t *st = &d->strain[s];
    float dist[DISH_MAX_STRAINS];
    for (int k = 0; k < 3; k++) st->other[k] = -1;
    for (int o = 0; o < DISH_MAX_STRAINS; o++) {
        const strain_t *t = &d->strain[o];
        dist[o] = o != s && t->used && t->count ? (t->cx - st->cx) * (t->cx - st->cx) + (t->cy - st->cy) * (t->cy - st->cy) : 1e18f;
    }
    for (int k = 0; k < 3; k++) {
        int best = -1;
        for (int o = 0; o < DISH_MAX_STRAINS; o++) if (dist[o] < 1e17f && (best < 0 || dist[o] < dist[best])) best = o;
        if (best < 0) break;
        st->other[k] = (int8_t)best; dist[best] = 1e18f;
    }
}

static void strains_tick(dish_t *d, float dt, dish_brain_fn brain) {
    for (int s = 0; s < DISH_MAX_STRAINS; s++) {
        strain_t *st = &d->strain[s];
        if (!st->used) continue;
        if (s != DISH_PET) {
            /* a visitor: gone when its last cell is (its slot is free for the next), and it changes the dish while here */
            if (st->count == 0) {
                bool dying = false;
                for (int i = 0; i < DISH_MAX_CELLS && !dying; i++) dying = d->cell[i].strain == s && d->cell[i].state == CS_DYING;
                if (!dying) {
                    bool beaten = st->stay_t > 0;                       /* gone before its time: something saw it off */
                    dish_story_note(d, beaten ? SE_WIPED : SE_LEAVE, st->species, d->strain[DISH_PET].species, 0, 0);
                    emit(DEV_VISIT_GONE, st->species);
                    if (!SPECIES_FRIENDLY[st->species] && d->strain[DISH_PET].count) d->run_visitors++;
                    st->used = false;
                }
                continue;
            }
            /* friends stay as long as the pet's symbiosis keeps them */
            if (!(SPECIES_FRIENDLY[st->species] && d->strain[DISH_PET].mut[M_SYMBIOSIS])) st->stay_t -= dt;
            float k = fminf(1, st->count / 8.0f) * dt;
            if (st->species == SP_LACTO) d->ph = fmaxf(0.12f, d->ph - 0.0015f * k);     /* the acid maker sours the agar */
            if (st->species == SP_HALO) d->salt = fminf(0.9f, d->salt + 0.0012f * k);   /* the halophile salts it */
        } else if (st->count == 0) {
            /* the pet: the vault brings it back once a run; the second time, the run is over */
            if (d->run_over) continue;
            if (st->extinct_t <= 0) {
                if (d->vault_used) { d->run_over = 1; dish_story_note(d, SE_RUN_OVER, st->species, 0, 0, (uint16_t)st->max_gen); emit(DEV_RUN_OVER, s); continue; }
                st->extinct_t = RESPAWN_S;                /* (before its first mutation the vault always saves a strain: a run has a start) */
                dish_story_note(d, SE_EXTINCT, st->species, 0, 0, 0);
                emit(DEV_EXTINCT, s);
            } else if ((st->extinct_t -= dt) <= 0) {
                st->extinct_t = 0;
                if (d->run_muts) d->vault_used = 1;
                seed_colony(d, s, 5, DISH_CX, DISH_CY, 12, &st->vault);
                splat(d->nutr, DISH_CX, DISH_CY, 0.35f, 24, 1.0f);    /* the vault wakes them on a little food */
                st->strat = (strat_t){ ST_FORAGE, -1, 5, 1, 0 };
                dish_story_note(d, SE_COMEBACK, st->species, 0, d->vault_used, 0);
                emit(DEV_RESPAWN, s);
            }
            continue;
        }
        if (st->count == 0) continue;
        st->decided_t += dt;
        /* an explore waypoint: a new one when reached (or none yet); a scout's food beacon fades */
        if (st->strat.id == ST_EXPLORE && (st->wx <= 0 || (st->cx - st->wx) * (st->cx - st->wx) + (st->cy - st->wy) * (st->cy - st->wy) < 30 * 30)) {
            st->wx = dish_rand(d, 30, DISH_W - 30); st->wy = dish_rand(d, 30, DISH_H - 30);
            dish_toward_inside(&st->wx, &st->wy, 30);
        }
        /* the build's strain-wide verbs: the zap, the quorum bloom */
        if (st->mut[M_ZAP] && (st->zap_t -= dt) <= 0) {
            st->zap_t = 7.5f - 1.5f * st->mut[M_ZAP];
            int src = -1; float bd = 1e9f;            /* the cell of ours nearest something foreign */
            for (int i = 0; i < DISH_MAX_CELLS; i++) {
                const cell_t *c = &d->cell[i];
                if (c->strain != s || c->state != CS_LIVE) continue;
                for (int o = 0; o < 3; o++) {
                    int os = st->other[o]; if (os < 0) continue;
                    float dx = d->strain[os].cx - c->x, dy = d->strain[os].cy - c->y, dd = dx * dx + dy * dy;
                    if (dd < bd) { bd = dd; src = i; }
                }
            }
            if (src >= 0) {
                cell_t *z = &d->cell[src]; float R = 30 + 10 * st->mut[M_ZAP]; int hit = 0;
                for (int i = 0; i < DISH_MAX_CELLS; i++) {
                    cell_t *o = &d->cell[i];
                    if (o->strain == s || (o->state != CS_LIVE && o->state != CS_SPORE) || SPECIES_FRIENDLY[o->species]) continue;
                    float dx = o->x - z->x, dy = o->y - z->y;
                    if (dx * dx + dy * dy > R * R) continue;
                    o->stun_t = 1 + 0.6f * st->mut[M_ZAP]; o->health -= 0.08f * st->mut[M_ZAP]; o->hurt = DIE_POISON;
                    if (hit++ < 6) part_add(d, PT_ZAP, z->x, z->y, o->x, o->y, 0.35f, 0xBFE8FF);   /* (vx, vy carry the arc's far end) */
                    if (SP[o->species].virus && o->health <= 0) kill(d, i, false, DIE_POISON);
                }
                if (hit) { emit(DEV_ZAP, s); if (s == DISH_PET && hit >= 3) dish_story_note(d, SE_ZAP, st->species, 0, 0, (uint16_t)hit); }
            }
        }
        if (st->mut[M_QUORUM]) {
            if (st->bloom_t < 0) { if ((st->bloom_t += dt) >= 0) st->bloom_t = 80 - 15 * st->mut[M_QUORUM]; }   /* (a bloom runs 12 s) */
            else if ((st->bloom_t -= dt) <= 0 && st->count >= 45 - 5 * st->mut[M_QUORUM]) {
                st->bloom_t = -12;
                part_add(d, PT_RIPPLE, st->cx, st->cy, 0, 0, 1.4f, 0xFFE07A);
                if (s == DISH_PET) dish_story_note(d, SE_BLOOM, st->species, 0, 0, (uint16_t)st->count);
                emit(DEV_BLOOM, s);
            } else if (st->bloom_t <= 0) st->bloom_t = 5;
        }
        /* the mind: asked when its time comes round (the AI's scheduler may ask sooner) */
        if ((st->ask_t -= dt) <= 0 && brain) {
            dish_fill_others(d, s);
            strat_t ns = brain(d, s);
            if (ns.id < ST_COUNT) {
                if (ns.target >= DISH_MAX_STRAINS || (ns.target >= 0 && !d->strain[ns.target].used)) ns.target = -1;
                bool changed = ns.id != st->strat.id || ns.target != st->strat.target;
                if (changed) {
                    st->decided_t = 0;
                    if (ST_TARGETED(ns.id) && ns.target >= 0)    /* who means what to whom: the story's interactions */
                        dish_story_note(d, SE_DECIDE, st->species, d->strain[ns.target].species, ns.id, (uint16_t)(s == DISH_PET ? 1 : ns.target == DISH_PET ? 2 : 0));
                    if (s == DISH_PET) emit(DEV_STRAT, s);
                }
                st->strat = ns;
            }
            st->ask_t = ASK_S + dish_rand(d, 0, 1.5f);
        }
    }
}

/* the keeper's hands: a held finger is light, the box's tilt is gravity */
void dish_light(dish_t *d, float x, float y, float dt) {
    d->beam_x = x; d->beam_y = y;
    d->beam = fminf(1, d->beam + dt * 1.5f);
    dish_prog_care(d, CARE_FEED);                 /* (light for a hungry strain is a good deed) */
}
void dish_set_tilt(dish_t *d, float x, float y) { d->tilt_x = clampf(x, -1, 1); d->tilt_y = clampf(y, -1, 1); }

void dish_tick(dish_t *d, float dt, dish_brain_fn brain) {
    if (dt <= 0) return;
    if (dt > 0.1f) dt = 0.1f;
    d->clock += dt;
    grid_build(d);
    census(d);
    strains_tick(d, dt, brain);
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (d->cell[i].state != CS_FREE) cell_tick(d, i, dt);

    /* the light: food blooms where it falls, and it fades when the finger lifts */
    if (d->beam > 0.01f) splat(d->nutr, d->beam_x, d->beam_y, 0.09f * d->beam * dt, 30, 0.8f);
    d->beam = fmaxf(0, d->beam - dt * 0.6f);
    /* the day and the night: a little cooler in the dark */
    d->temp += ((d->light ? 0.5f : 0.42f) - d->temp) * fminf(1, dt / 60);
    /* fields: the current carries and fades, food and filth spread slowly, the agar regrows a little,
       pH and salt drift back to normal when nothing is pushing them */
    float fmax = 0;
    for (int c = 0; c < FIELD_N * FIELD_N; c++) {
        float k = expf(-1.4f * dt);
        d->flowx[c] *= k; d->flowy[c] *= k;
        fmax = fmaxf(fmax, fabsf(d->flowx[c]) + fabsf(d->flowy[c]));
        d->drug[c] *= expf(-dt / 90.0f);
        if (d->nutr[c] > 0.45f) { float sp = (d->nutr[c] - 0.45f) * dt / 60.0f; d->nutr[c] -= sp; d->waste[c] = fminf(1, d->waste[c] + sp * 1.5f); }   /* too much food spoils */
        else if (g_in[c] && d->nutr[c] < 0.06f) d->nutr[c] += (0.06f - d->nutr[c]) * dt / 400;   /* the agar regrows, slowly */
        d->waste[c] *= expf(-dt / 600.0f);
    }
    if (!dish_visitor_here(d, SP_LACTO)) d->ph += (0.5f - d->ph) * fminf(1, dt / 240);
    if (!dish_visitor_here(d, SP_HALO)) d->salt += (0.2f - d->salt) * fminf(1, dt / 240);
    if (fmax > 1) { advect(d, d->nutr, dt); advect(d, d->waste, dt); advect(d, d->drug, dt); diffuse(d->flowx, 4 * dt); diffuse(d->flowy, 4 * dt); }
    diffuse(d->nutr, 0.6f * dt);
    diffuse(d->waste, 0.15f * dt);
    diffuse(d->drug, 0.5f * dt);
    d->warm *= expf(-0.5f * dt);
    if (d->uv_t > 0) d->uv_t -= dt;
    if (d->scare_t > 0) d->scare_t -= dt;
    parts_tick(d, dt);
    /* meetings: two strains whose cells touched talk, now and then (the story keeps a pet's first in a while) */
    for (int a = 0; a < DISH_MAX_STRAINS; a++)
        for (int b = a + 1; b < DISH_MAX_STRAINS; b++) {
            float *cd = &d->meet_cd[a][b];
            if (*cd > -60) *cd -= dt;
            if (!(d->met >> (a * DISH_MAX_STRAINS + b) & 1) || *cd > 0 || !d->strain[a].used || !d->strain[b].used) continue;
            if (a == DISH_PET && *cd <= -48) {
                int sp = ST_TARGETED(d->strain[a].strat.id) && d->strain[a].strat.target == b ? a : b, o = sp == a ? b : a;
                dish_story_note(d, SE_SAID, d->strain[sp].species, d->strain[o].species, d->strain[sp].strat.say, (uint16_t)(sp == DISH_PET ? 1 : 0) | (uint16_t)(dish_say_variant(d, sp) << 4));
            }
            *cd = 12;
            emit(DEV_MEET, a * 8 + b);
        }
    d->met = 0;
    for (int w = 0; w < DISH_WALLS; w++) if (d->wall[w].t > 0) d->wall[w].t -= dt;
    d->ink = fminf(DISH_WALLS, d->ink + dt / 12.0f);
    dish_prog_tick(d, dt);
    dish_story_tick(d, dt);
    if (d->gift_cd > 0) d->gift_cd -= dt;          /* (visitors, the phages among them, come from the director: dish_prog.c) */
}

/* ---- the save (append-only, like progression.c's SAVE LAYOUT LOCK: a new
 * field goes at the tail and an older save simply ends before it) ----
 * Cells and fields are quantized to bytes: ~8 KB for a full dish, so it fits
 * the board's NVS beside the tank's. */
#define SAVE_MAGIC 0x31485344u                  /* "DSH1" */
typedef struct { uint8_t *p; size_t n, cap; bool ok; } wbuf_t;
static void w_bytes(wbuf_t *w, const void *v, size_t n) {
    if (w->n + n > w->cap) { w->ok = false; return; }
    memcpy(w->p + w->n, v, n); w->n += n;
}
static void w_q8(wbuf_t *w, float v, float lo, float hi) { uint8_t b = (uint8_t)(clampf((v - lo) / (hi - lo), 0, 1) * 255 + 0.5f); w_bytes(w, &b, 1); }
static void w_u16(wbuf_t *w, float v, float lo, float hi) { uint16_t b = (uint16_t)(clampf((v - lo) / (hi - lo), 0, 1) * 65535 + 0.5f); w_bytes(w, &b, 2); }
typedef struct { const uint8_t *p; size_t n, len; bool ok; } rbuf_t;
static void r_bytes(rbuf_t *r, void *v, size_t n) {
    if (r->n + n > r->len) { r->ok = false; memset(v, 0, n); return; }
    memcpy(v, r->p + r->n, n); r->n += n;
}
static float r_q8(rbuf_t *r, float lo, float hi) { uint8_t b; r_bytes(r, &b, 1); return lo + (hi - lo) * b / 255.0f; }
static float r_u16(rbuf_t *r, float lo, float hi) { uint16_t b; r_bytes(r, &b, 2); return lo + (hi - lo) * b / 65535.0f; }
#define GENES ((int)(sizeof(genome_t) / sizeof(float)))
static float fin(float v, float lo, float hi, float dflt) { return v >= lo && v <= hi ? v : dflt; }   /* (NaN fails both tests) */
static const float GLO[GENES] = { 0.5f, 0.75f, 0, 0, 0, 0, 0, 0.6f, 0 }, GHI[GENES] = { 1.6f, 1.35f, 1, 1, 1, 1, 1, 1.6f, 1 };

size_t dish_save(const dish_t *d, uint8_t *out, size_t cap) {
    wbuf_t w = { out, 0, cap, true };
    uint32_t magic = SAVE_MAGIC; uint16_t ver = 7;   /* v2 (phase 3): pH, salt, ph_opt - nothing shipped v1, so it is refused; v3: progression at the tail;
                                                       v4: each cell's age, and the badges' trackers; v5: the builds, the run, the draft;
                                                       v6: the 16 behaviour mutations (the 30 of v5 are gone), and the story */
    w_bytes(&w, &magic, 4); w_bytes(&w, &ver, 2);
    w_bytes(&w, &d->temp, 4); w_bytes(&w, &d->clock, 4); w_bytes(&w, &d->outbreak_t, 4);
    uint8_t light = d->light; w_bytes(&w, &light, 1);
    w_bytes(&w, &d->ph, 4); w_bytes(&w, &d->salt, 4);
    for (int s = 0; s < DISH_MAX_STRAINS; s++) {
        const strain_t *st = &d->strain[s];
        uint8_t used = st->used, sp = st->species, strat = st->strat.id;
        w_bytes(&w, &used, 1); w_bytes(&w, &sp, 1); w_bytes(&w, st->name, sizeof st->name); w_bytes(&w, &strat, 1);
        w_bytes(&w, &st->extinct_t, 4); w_bytes(&w, &st->births, 4); w_bytes(&w, &st->deaths, 4); w_bytes(&w, &st->max_gen, 4);
        w_bytes(&w, &st->vault, sizeof st->vault);
    }
    for (int c = 0; c < FIELD_N * FIELD_N; c++) { w_q8(&w, d->nutr[c], 0, 1); w_q8(&w, d->waste[c], 0, 1); w_q8(&w, d->drug[c], 0, 1); }
    uint16_t n = 0;
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (d->cell[i].state == CS_LIVE || d->cell[i].state == CS_SPORE) n++;
    w_bytes(&w, &n, 2);
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        const cell_t *c = &d->cell[i];
        if (c->state != CS_LIVE && c->state != CS_SPORE) continue;
        uint8_t hdr[3] = { c->state, c->species, c->strain };
        uint16_t gen = c->gen > 65535 ? 65535 : (uint16_t)c->gen;
        w_bytes(&w, hdr, 3); w_bytes(&w, &gen, 2);
        w_u16(&w, c->x, 0, DISH_W); w_u16(&w, c->y, 0, DISH_H);
        w_q8(&w, c->energy, 0, 1.1f); w_q8(&w, c->health, 0, 1); w_q8(&w, c->infect, 0, 1); w_q8(&w, fmodf(c->ang + 100 * TAU, TAU), 0, TAU);
        const float *g = &c->g.speed;
        for (int k = 0; k < GENES; k++) w_q8(&w, g[k], GLO[k], GHI[k]);
        uint16_t age = c->age > 65535 ? 65535 : (uint16_t)c->age;   /* v4 */
        w_bytes(&w, &age, 2);
    }
    /* v3 */
    uint32_t prog[6] = { d->dna, d->badges, d->badges_new, d->dex, d->unlocked, d->births_paid };
    w_bytes(&w, prog, sizeof prog);
    uint8_t track[2] = { d->outbreak_on, d->was_extinct };          /* v4 */
    w_bytes(&w, track, 2);
    for (int s = 0; s < DISH_MAX_STRAINS; s++) { w_bytes(&w, &d->strain[s].stay_t, 4); w_bytes(&w, d->strain[s].mut, DISH_MUT_N); }   /* v5 */
    uint8_t flags[8] = { d->vault_used, d->run_over, d->run_muts, d->crispr, d->drafts, d->offer[0], d->offer[1], d->offer[2] };
    w_bytes(&w, &d->run, 2); w_bytes(&w, &d->run_visitors, 2); w_bytes(&w, &d->run_dna0, 4); w_bytes(&w, flags, 8);
    w_bytes(&w, &d->next_draft_gen, 4); w_bytes(&w, &d->visit_t, 4); w_bytes(&w, &d->gift_cd, 4);
    /* v6: the story */
    w_bytes(&w, &d->log_head, 2); w_bytes(&w, &d->log_n, 2); w_bytes(&w, d->log, sizeof d->log);
    w_bytes(&w, &d->series_head, 2); w_bytes(&w, &d->series_n, 2); w_bytes(&w, d->series, sizeof d->series);
    w_bytes(&w, &d->run_t, 4); w_bytes(&w, &d->best_count, 2);
    /* v7: the hints shown, the quests and waves won */
    w_bytes(&w, &d->hints, 4); w_bytes(&w, &d->quests_won, 2); w_bytes(&w, &d->waves_won, 2);
    return w.ok ? w.n : 0;
}

bool dish_load(dish_t *d, const uint8_t *in, size_t len, uint32_t seed) {
    rbuf_t r = { in, 0, len, true };
    uint32_t magic; uint16_t ver;
    r_bytes(&r, &magic, 4); r_bytes(&r, &ver, 2);
    if (!r.ok || magic != SAVE_MAGIC || ver < 2) return false;
    /* the shape is checked before anything is written, so a bad save leaves the dish alone
     * (no second 45 KB dish to load beside: on the device that would be internal RAM) */
    const size_t strain_b = 3 + sizeof d->strain[0].name + 16 + sizeof(genome_t);
    const size_t n_at = 6 + 21 + DISH_MAX_STRAINS * strain_b + 3 * FIELD_N * FIELD_N, cell_b = 13 + GENES + (ver >= 4 ? 2 : 0);
    if (len < n_at + 2) return false;
    uint16_t nc; memcpy(&nc, in + n_at, 2);
    const int muts_saved = ver >= 6 ? DISH_MUT_N : 30;   /* (v5 kept 30 stat mutations per strain) */
    const size_t tail5 = DISH_MAX_STRAINS * (4 + muts_saved) + 2 + 2 + 4 + 8 + 4 + 4 + 4;
    const size_t tail6 = 4 + sizeof d->log + 4 + sizeof d->series + 4 + 2;
    size_t tail = (ver >= 7 ? 8 : 0) + (ver >= 6 ? tail6 : 0) + (ver >= 5 ? tail5 : 0) + (ver >= 4 ? 24 + 2 : ver >= 3 ? 24 : 0);
    if (nc > DISH_MAX_CELLS || len != n_at + 2 + nc * cell_b + tail || !in[6 + 21]) return false;   /* (+27: strain 0 used) */
    dish_t *dp = d;
    #define nd (*dp)
    memset(&nd, 0, sizeof nd);
    nd.rng = seed ? seed : 0x2545F491u;
    field_mask();
    r_bytes(&r, &nd.temp, 4); r_bytes(&r, &nd.clock, 4); r_bytes(&r, &nd.outbreak_t, 4);
    uint8_t light; r_bytes(&r, &light, 1); nd.light = light;
    r_bytes(&r, &nd.ph, 4); r_bytes(&r, &nd.salt, 4);
    for (int s = 0; s < DISH_MAX_STRAINS; s++) {
        strain_t *st = &nd.strain[s];
        uint8_t used, sp, strat;
        r_bytes(&r, &used, 1); r_bytes(&r, &sp, 1); r_bytes(&r, st->name, sizeof st->name); r_bytes(&r, &strat, 1);
        st->name[sizeof st->name - 1] = 0;
        r_bytes(&r, &st->extinct_t, 4); r_bytes(&r, &st->births, 4); r_bytes(&r, &st->deaths, 4); r_bytes(&r, &st->max_gen, 4);
        r_bytes(&r, &st->vault, sizeof st->vault);
        st->used = used; st->species = sp < SP_COUNT ? sp : 0;
        st->strat = (strat_t){ (uint8_t)(strat < ST_COUNT ? strat : 0), -1, 5, 1, 0 };
        st->other[0] = st->other[1] = st->other[2] = -1;
    }
    for (int c = 0; c < FIELD_N * FIELD_N; c++) { nd.nutr[c] = r_q8(&r, 0, 1); nd.waste[c] = r_q8(&r, 0, 1); nd.drug[c] = r_q8(&r, 0, 1); }
    uint16_t n; r_bytes(&r, &n, 2);
    for (int i = 0; i < n && i < DISH_MAX_CELLS && r.ok; i++) {
        cell_t *c = &nd.cell[i];
        uint8_t hdr[3]; uint16_t gen;
        r_bytes(&r, hdr, 3); r_bytes(&r, &gen, 2);
        c->state = hdr[0] == CS_SPORE ? CS_SPORE : CS_LIVE; c->species = hdr[1] < SP_COUNT ? hdr[1] : 0;
        c->strain = hdr[2] < DISH_MAX_STRAINS ? hdr[2] : 0; c->gen = gen;
        c->x = r_u16(&r, 0, DISH_W); c->y = r_u16(&r, 0, DISH_H);
        c->energy = r_q8(&r, 0, 1.1f); c->health = r_q8(&r, 0, 1); c->infect = r_q8(&r, 0, 1); c->ang = r_q8(&r, 0, TAU);
        float *g = &c->g.speed;
        for (int k = 0; k < GENES; k++) g[k] = r_q8(&r, GLO[k], GHI[k]);
        if (ver >= 4) { uint16_t age; r_bytes(&r, &age, 2); c->age = age; }
        c->phase = dish_rand(&nd, 0, TAU); c->run_t = dish_rand(&nd, 0.2f, 1.5f);
        c->id = ++nd.next_id; c->comfort = 1;
        if (!nd.strain[c->strain].used) c->state = CS_FREE;
    }
    if (ver >= 3) {
        uint32_t prog[6]; r_bytes(&r, prog, sizeof prog);
        nd.dna = prog[0]; nd.badges = prog[1]; nd.badges_new = prog[2]; nd.dex = prog[3]; nd.unlocked = prog[4]; nd.births_paid = prog[5];
    }
    if (ver >= 4) { uint8_t track[2]; r_bytes(&r, track, 2); nd.outbreak_on = track[0] != 0; nd.was_extinct = track[1] != 0; }
    if (ver >= 5) {
        for (int s = 0; s < DISH_MAX_STRAINS; s++) {
            uint8_t m30[30]; r_bytes(&r, &nd.strain[s].stay_t, 4); r_bytes(&r, m30, (size_t)muts_saved);
            if (ver >= 6) memcpy(nd.strain[s].mut, m30, DISH_MUT_N);   /* (v5's builds were other mutations: dropped) */
        }
        uint8_t flags[8];
        r_bytes(&r, &nd.run, 2); r_bytes(&r, &nd.run_visitors, 2); r_bytes(&r, &nd.run_dna0, 4); r_bytes(&r, flags, 8);
        r_bytes(&r, &nd.next_draft_gen, 4); r_bytes(&r, &nd.visit_t, 4); r_bytes(&r, &nd.gift_cd, 4);
        nd.vault_used = flags[0] != 0; nd.run_over = flags[1] != 0; nd.run_muts = flags[2]; nd.crispr = flags[3] != 0;
        nd.drafts = flags[4] > 3 ? 3 : flags[4];
        for (int k = 0; k < 3; k++) nd.offer[k] = ver >= 6 && flags[5 + k] < DISH_MUT_N ? flags[5 + k] : 0xFF;
        if (ver < 6) { nd.run_muts = 0; nd.drafts = nd.drafts ? 1 : 0; }
        if (nd.drafts > 1) nd.drafts = 1;             /* (one at a time) */
        for (int s = 0; s < DISH_MAX_STRAINS; s++) {
            nd.strain[s].stay_t = fin(nd.strain[s].stay_t, -1e9f, 1e9f, 300);
            for (int m = 0; m < DISH_MUT_N; m++) if (nd.strain[s].mut[m] > MUT_TIERS) nd.strain[s].mut[m] = MUT_TIERS;
        }
        if (ver >= 6) {
            r_bytes(&r, &nd.log_head, 2); r_bytes(&r, &nd.log_n, 2); r_bytes(&r, nd.log, sizeof nd.log);
            r_bytes(&r, &nd.series_head, 2); r_bytes(&r, &nd.series_n, 2); r_bytes(&r, nd.series, sizeof nd.series);
            r_bytes(&r, &nd.run_t, 4); r_bytes(&r, &nd.best_count, 2);
            const int LN = (int)(sizeof nd.log / sizeof nd.log[0]), SN = (int)(sizeof nd.series / sizeof nd.series[0]);
            if (nd.log_head >= LN || nd.log_n > LN) nd.log_head = nd.log_n = 0;
            if (nd.series_head >= SN || nd.series_n > SN) nd.series_head = nd.series_n = 0;
            nd.run_t = fin(nd.run_t, 0, 1e8f, 0);
        }
        if (ver >= 7) { r_bytes(&r, &nd.hints, 4); r_bytes(&r, &nd.quests_won, 2); r_bytes(&r, &nd.waves_won, 2); }
        if (nd.run_t == 0) nd.run_t = 1;              /* (a loaded dish is no new run: no START line) */
        nd.visit_t = fin(nd.visit_t, 0, 3600, 120); nd.gift_cd = fin(nd.gift_cd, 0, GIFT_CD, 0);
    } else {                                        /* a save from before runs: run 1, its draft due at the next 8 generations,
                                                       and the dex's forms moved up past the new species (bits 8.. -> 16..) */
        nd.run = 1; nd.offer[0] = nd.offer[1] = nd.offer[2] = 0xFF;
        nd.next_draft_gen = (nd.strain[DISH_PET].max_gen / DRAFT_GENS + 1) * DRAFT_GENS;
        nd.visit_t = 120;
        nd.dex = (nd.dex & 0xFF) | (nd.dex >> 8 & 0xF) << 16;
        for (int s = 1; s < DISH_MAX_STRAINS; s++) nd.strain[s].stay_t = 300;
    }
    /* the raw floats are trusted no further than their ranges: a flipped bit must not load a NaN into the world */
    nd.temp = fin(nd.temp, 0.05f, 0.95f, 0.5f); nd.ph = fin(nd.ph, 0, 1, 0.5f); nd.salt = fin(nd.salt, 0, 1, 0.2f);
    nd.clock = fin(nd.clock, 0, 1e9f, 0); nd.outbreak_t = fin(nd.outbreak_t, 0, 3600, 300);
    for (int s = 0; s < DISH_MAX_STRAINS; s++) {
        strain_t *st = &nd.strain[s];
        st->extinct_t = fin(st->extinct_t, 0, WILD_BACK_S, 0);
        const float *g = &st->vault.speed;
        for (int k = 0; k < GENES; k++)
            if (!(g[k] >= GLO[k] && g[k] <= GHI[k])) { st->vault = dish_species_genome((species_t)st->species); break; }
    }
    nd.ink = DISH_WALLS; nd.quest_cd = 20; nd.wave_cd = 240;   /* (walls, quests and waves are not saved: a loaded dish starts them fresh) */
    #undef nd
    return r.ok;
}

/* ---- time away: the board asleep or off. The dish lives it through in one
 * step, like a fridge: everything slows right down (metabolism ~1/700),
 * nothing divides, a free phage doesn't last. A fed dish sleeps through a
 * night; a few days away and the strain starves - and the vault, as ever,
 * brings it back. ---- */
void dish_sleep(dish_t *d, float seconds) {
    if (seconds <= 0) return;
    float t = fminf(seconds, 30 * 86400.0f);
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        cell_t *c = &d->cell[i];
        if (c->state == CS_FREE) continue;
        if (c->state == CS_DYING || SP[c->species].virus) { c->state = CS_FREE; continue; }
        c->energy -= SP[c->species].metab * 0.0015f * t * (c->state == CS_SPORE ? 0.05f : 1);
        if (c->state != CS_SPORE) c->age += t * 0.0015f;   /* (the fridge slows aging as much as hunger) */
        c->div_t = 0; c->infect = 0;
        if (c->energy <= 0) {                             /* starved in the dark: no pop, it's just gone in the morning */
            c->state = CS_FREE;
            d->strain[c->strain].deaths++;
            splat(d->nutr, c->x, c->y, 0.05f, 10, 1.0f);
        }
    }
    for (int k = 0; k < FIELD_N * FIELD_N; k++) { d->drug[k] *= expf(-t / 90.0f); d->flowx[k] = d->flowy[k] = 0; }
    for (int s = 0; s < DISH_MAX_STRAINS; s++) if (d->strain[s].extinct_t > 0) d->strain[s].extinct_t = fmaxf(0.01f, d->strain[s].extinct_t - t);
    d->outbreak_t = fmaxf(30, d->outbreak_t - t);
    d->visit_t = fmaxf(20, d->visit_t - t);
    for (int s = 1; s < DISH_MAX_STRAINS; s++) if (d->strain[s].used) d->strain[s].stay_t -= t;   /* visitors keep their hours while you're away */
    d->warm = 0; d->uv_t = 0; d->scare_t = 0;
    for (int i = 0; i < DISH_MAX_PARTS; i++) d->part[i].used = 0;
}
