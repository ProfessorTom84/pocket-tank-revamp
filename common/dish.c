/* dish.c — the living dish: see dish.h. */
#include "dish.h"
#include "dish_prog.h"
#include "dish_mut.h"
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
const char *const STRAT_NAMES[ST_COUNT] = { "forage", "swarm", "divide", "hunt", "hide", "rest", "spore", "spread" };

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
        p->x += p->vx * dt; p->y += p->vy * dt;
        p->vx *= expf(-3 * dt); p->vy *= expf(-3 * dt);
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
            st->strat = (strat_t){ sp == SP_PHAGE ? ST_HUNT : ST_FORAGE, 5, 1 };
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
}
void dish_new_run(dish_t *d, uint32_t seed, species_t pet, const char *pet_name) {
    uint32_t dna = d->dna, badges = d->badges, nb = d->badges_new, dex = d->dex, unl = d->unlocked; uint16_t run = d->run;
    bool over = d->run_over;
    dish_init(d, seed, pet, pet_name);
    d->dna = dna; d->badges = badges; d->badges_new = nb; d->dex = dex; d->unlocked = unl;
    d->run = over ? run + 1 : run ? run : 1;      /* (the very first setup is run 1, not a second run) */
    d->run_dna0 = dna;
}

/* ---- the keeper's hands ---- */
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
static void kill(dish_t *d, int i, bool burst) {
    cell_t *c = &d->cell[i];
    if (c->state == CS_DYING || c->state == CS_FREE) return;
    c->state = CS_DYING; c->dying_t = 0;
    strain_t *st = &d->strain[c->strain];
    st->deaths++;
    if (SP[c->species].virus) return;            /* a phage just fades */
    splat(d->nutr, c->x, c->y, 0.10f, 10, 1.0f); /* the remains feed the others */
    uint32_t rgb = burst ? 0xC07CFF : 0xFFFFFF;
    for (int k = 0; k < 6; k++) {
        float a = k * TAU / 6 + dish_rand(d, -0.3f, 0.3f), sp = dish_rand(d, 20, 50);
        part_add(d, PT_SHARD, c->x, c->y, cosf(a) * sp, sinf(a) * sp, 0.6f, rgb);
    }
    emit(burst ? DEV_BURST : DEV_POP, c->strain);
}
static void divide(dish_t *d, int i) {
    cell_t *c = &d->cell[i];
    float r = dish_cell_radius(c);
    float ax = cosf(c->ang), ay = sinf(c->ang);
    cell_t parent = *c;
    float rate = (d->strain[c->strain].strat.id == ST_SPREAD ? 1.5f : 1.0f) * (d->uv_t > 0 ? 5.0f : 1.0f);
    genome_t g = mutate(d, &parent.g, rate);
    int j = dish_spawn(d, c->strain, c->x + ax * r, c->y + ay * r, &g);
    c = &d->cell[i];                              /* (same pool; the pointer is still good) */
    c->div_t = 0;
    if (j < 0) return;                            /* no room for a daughter: the pinch undoes, the energy is kept */
    c->x -= ax * r * 0.5f; c->y -= ay * r * 0.5f;
    c->energy = parent.energy * 0.45f;
    strain_t *st = &d->strain[c->strain];
    {
        cell_t *n = &d->cell[j];
        n->energy = c->energy; n->ang = c->ang + dish_rand(d, -0.3f, 0.3f);
        n->gen = parent.gen + 1; n->health = parent.health;
        n->vx = ax * 8; n->vy = ay * 8;
        st->births++;
        if (n->gen > st->max_gen) st->max_gen = n->gen;
        emit(DEV_DIVIDE, c->strain);
    }
}

/* the danger a cell feels at a point: the drug it can't shrug off, the filth, the heat it can't stand */
static float local_temp(const dish_t *d, float x, float y) {
    float t = d->temp;
    if (d->warm > 0) {
        float dx = x - d->warm_x, dy = y - d->warm_y;
        t += d->warm * expf(-(dx * dx + dy * dy) / (2 * 40.0f * 40.0f));
    }
    return t;
}
static float danger_at(const dish_t *d, const cell_t *c, float x, float y) {
    float m = fabsf(local_temp(d, x, y) - c->g.heat_opt);
    return dish_field(d->drug, x, y) * (1 - c->g.drug_res) + dish_field(d->waste, x, y) * 0.6f + (m > 0.15f ? m : 0);
}
/* what the dish itself does to a cell: too hot or cold, too sour or soapy,
 * too salty, the UV lamp. The same numbers are its comfort (the pet's need). */
float dish_stress(const dish_t *d, const cell_t *c, float *comfort) {
    float mt = fabsf(local_temp(d, c->x, c->y) - c->g.heat_opt);
    float mp = fabsf(d->ph - c->g.ph_opt);
    float ms = d->salt - (0.25f + c->g.salt_tol * 0.6f);          /* salt hurts only above what it tolerates */
    float ct = 1 - mt * 2.2f, cp = 1 - mp * 2.2f, cs = 1 - (ms > 0 ? ms * 3 : 0);
    if (comfort) *comfort = clampf(fminf(ct, fminf(cp, cs)), 0, 1);
    return (mt > 0.2f ? (mt - 0.2f) * 0.35f : 0) + (mp > 0.22f ? (mp - 0.22f) * 0.35f : 0)
         + (ms > 0 ? ms * 0.5f : 0) + (d->uv_t > 0 ? 0.008f : 0);
}

/* the cells a hunter could eat: every other strain's, phages aside (last tick's census) */
static int prey_count(const dish_t *d, int strain) {
    int n = 0;
    for (int s = 0; s < DISH_MAX_STRAINS; s++)
        if (s != strain && d->strain[s].used && !SP[d->strain[s].species].virus) n += d->strain[s].count;
    return n;
}
/* how much of a bite (a hunter's, a predator's) reaches a cell: its strain's slime, size and twitch */
static float bite_k(const dish_t *d, const cell_t *o) {
    const uint8_t *m = d->strain[o->strain].mut;
    return powf(0.75f, m[M_SLIME]) * powf(0.85f, m[M_BIG]) * (1 - 0.2f * m[M_TWITCH]);
}
/* gene transfer: a free draft card, now and then */
static void gift(dish_t *d) {
    if (d->gift_cd > 0 || d->run_over) return;
    d->gift_cd = GIFT_CD;
    if (d->drafts < 3) d->drafts++;
    emit(DEV_GIFT, DISH_PET);
}
static void cell_tick(dish_t *d, int i, float dt) {
    cell_t *c = &d->cell[i];
    const sp_info_t *sp = &SP[c->species];
    strain_t *st = &d->strain[c->strain];
    const uint8_t *mu = st->mut;                  /* the strain's mutations (only the pet drafts any) */
    int strat = st->strat.id;
    if (c->state != CS_SPORE) c->age += dt;      /* spores don't age */
    c->phase += dt * (2.0f + c->g.speed * 3);

    if (c->state == CS_DYING) {
        c->dying_t += dt;
        if (c->dying_t > 0.5f) c->state = CS_FREE;
        return;
    }
    float r = dish_cell_radius(c);

    /* ---- spores: a strain that chose to sporulate goes dormant, a cell at a time ---- */
    if (!sp->virus) {
        float sk = mu[M_SPOREMASTER] ? 3 : 1;    /* a spore master goes dormant and wakes three times as fast */
        if (c->state == CS_LIVE && strat == ST_SPORE && dish_rand(d, 0, 1) < dt * 0.8f * sk) { c->state = CS_SPORE; c->div_t = 0; }
        else if (c->state == CS_SPORE && strat != ST_SPORE && dish_rand(d, 0, 1) < dt * 0.6f * sk) c->state = CS_LIVE;
    }
    bool spore = c->state == CS_SPORE;

    /* ---- a phage clamped on its host: inject, then it is spent ---- */
    if (sp->virus && c->host) {
        cell_t *h = &d->cell[c->host - 1];
        if (h->state != CS_LIVE) { c->host = 0; }
        else {
            float hr = dish_cell_radius(h) + 1.5f;
            c->x = h->x + cosf(c->ang) * hr; c->y = h->y + sinf(c->ang) * hr;
            c->div_t += dt;                                      /* (reused as the injection clock) */
            if (c->div_t > 1.2f) {
                if (h->infect <= 0) emit(DEV_INFECT, h->strain);
                h->infect += 0.35f;
                c->state = CS_FREE;
            }
            return;
        }
    }

    /* ---- metabolism, food, the light ---- */
    float comfort, env = dish_stress(d, c, &comfort);
    c->comfort = comfort;
    float metab = sp->metab * (0.6f + 0.4f * c->g.speed * c->g.size) * (strat == ST_REST ? 0.45f : 1) * (spore ? 0.05f : 1)
                * (1 + 0.1f * mu[M_GLUTTON]) * powf(0.8f, mu[M_FERMENT]);
    if (!sp->virus) {
        c->energy -= metab * dt;
        if (!spore) {
            int fc = fidx(c->x, c->y);
            float food = d->nutr[fc];
            float take = sp->eat * (1 + 0.3f * mu[M_GLUTTON]) * food * dt * (0.4f + 0.6f * comfort) * (c->energy < 1.05f ? 1 : 0);
            if (take * 2.0f > food) take = food * 0.5f;
            d->nutr[fc] -= take * 2.0f;          /* a full field cell is one daughter's worth */
            c->energy += take;
            float photo = sp->photo + 0.004f * mu[M_CHLORO];             /* chloroplasts: a cyano's trick, learned */
            if (photo > 0) c->energy += photo * comfort * dt * (d->light ? 1 : mu[M_GLOW] ? 0.5f : 0);   /* (a glowing cell lights its own) */
            if (mu[M_SCAVENGER]) {                                        /* waste and the dead are food too */
                float w = d->waste[fc] * 0.15f * mu[M_SCAVENGER] * dt;
                d->waste[fc] -= w; c->energy += w * 0.8f;
            }
            if (c->species == SP_MOLD) d->drug[fc] = fminf(1, d->drug[fc] + 0.08f * dt);        /* the mold's antibiotic */
            if (c->species == SP_SYMBIONT) d->nutr[fc] = fminf(1, d->nutr[fc] + 0.012f * dt);  /* the symbiont's gift */
            d->waste[fc] = clampf(d->waste[fc] + dt * 0.0010f, 0, 1);
        }
        /* health: what hurts, what heals */
        float dmg = env * powf(0.7f, mu[M_SIGNAL])
                  + dish_field(d->drug, c->x, c->y) * (1 - c->g.drug_res) * (1 - c->g.drug_res) * 0.6f * powf(0.85f, mu[M_SHELL])   /* squared: a little resistance goes a long way */
                  + fmaxf(0, dish_field(d->waste, c->x, c->y) - 0.45f) * 0.12f;
        if (c->nbrs >= 2) dmg *= powf(0.8f, mu[M_BIOFILM]);        /* a clump shares the harm */
        if (c->nbrs >= 3) dmg *= powf(0.85f, mu[M_QUORUM]);
        if (c->strain != DISH_PET && st->stay_t <= 0) dmg += 0.05f;   /* a visitor whose time is up: it withers and is gone */
        /* old age: past its span (6-9 minutes) a cell slowly fails, so a full dish keeps turning over and evolving */
        bool old = c->age > 360 + 180 * (((uint32_t)i * 2654435761u >> 16) & 1023) / 1023.0f;
        if (old) dmg += 0.006f;
        if (spore) dmg *= mu[M_SPOREMASTER] ? 0.03f : 0.08f;
        /* one phage's payload is a cold a healthy cell shakes off; two take hold */
        if (c->infect >= 0.5f) { c->infect += dt * 0.05f; c->energy -= dt * 0.006f; }
        else if (c->infect > 0) c->infect = fmaxf(0, c->infect - dt * 0.03f * c->health);
        c->health = clampf(c->health - dmg * dt + (dmg < 0.01f && !old && c->energy > 0.3f ? 0.03f * dt : 0), 0, 1);   /* the old don't heal */
        if (c->infect >= 1) {                       /* the burst: new phages pour out */
            int ps = strain_of(d, SP_PHAGE);
            float hx = c->x, hy = c->y;
            kill(d, i, true);
            if (ps >= 0) for (int k = 0; k < 3; k++) {
                int j = dish_spawn(d, ps, hx + dish_rand(d, -4, 4), hy + dish_rand(d, -4, 4), NULL);
                if (j >= 0) { float a = dish_rand(d, 0, TAU); d->cell[j].vx = cosf(a) * 30; d->cell[j].vy = sinf(a) * 30; }
            }
            return;
        }
        if (c->energy <= 0 || c->health <= 0) { kill(d, i, false); return; }
    } else {
        c->health -= dt / 45.0f;                    /* a free phage does not last */
        if (c->health <= 0) { kill(d, i, false); return; }
    }

    /* ---- division ---- */
    if (!sp->virus && !spore) {
        float need = 0.85f - 0.15f * (c->g.div_rate - 1) - (strat == ST_DIVIDE ? 0.12f : 0);   /* generations come quick enough to watch evolution in an afternoon */
        if (c->div_t > 0) {
            c->div_t += dt * c->g.div_rate;
            if (c->div_t >= DIV_TIME) { divide(d, i); return; }
        } else if (c->energy >= need && c->health > 0.5f && d->n_live < DISH_MAX_CELLS - 32   /* ponytail: the last 32 slots are the phages' */
                   && (sp->prey <= 0 || st->count * 3 < prey_count(d, c->strain)))           /* a hunter breeds only while prey outnumbers it 3 to 1 */
            c->div_t = 0.001f;
    }

    /* ---- where to go ---- */
    float speed = SP[c->species].speed * c->g.speed * (1 + 0.2f * mu[M_FLAGELLA]) * (c->nbrs >= 3 ? 1 + 0.05f * mu[M_QUORUM] : 1);
    float mult = 1;
    switch (strat) {
    case ST_REST:   mult = 0.15f; break;
    case ST_DIVIDE: mult = 0.4f; break;
    case ST_SWARM:  mult = 0.8f; break;
    case ST_HIDE:   mult = 1.1f; break;
    case ST_HUNT:   mult = 1.2f; break;
    }
    if (spore || c->div_t > 0) mult = 0.05f;
    float hx = cosf(c->ang), hy = sinf(c->ang);
    if (speed > 8 && !spore) {
        /* run and tumble: keep running while things get better, tumble when they don't */
        c->run_t -= dt;
        if (c->run_t <= 0) {
            float f = dish_field(d->nutr, c->x, c->y) - danger_at(d, c, c->x, c->y);
            bool better = f > c->last_food + 0.002f;
            if (better && strat != ST_SPREAD) c->run_t = dish_rand(d, 1.0f, 2.5f);
            else { c->ang += dish_rand(d, -2.6f, 2.6f); c->run_t = dish_rand(d, 0.4f, 1.2f); }
            c->last_food = f;
        }
    }
    /* steering on top of the run */
    float sx = 0, sy = 0;
    if (strat == ST_FORAGE || strat == ST_DIVIDE) {
        float e = 6;
        float gain = 40 * (1 + 0.6f * mu[M_CILIA]);      /* cilia smell food from further */
        sx += (dish_field(d->nutr, c->x + e, c->y) - dish_field(d->nutr, c->x - e, c->y)) * gain;
        sy += (dish_field(d->nutr, c->x, c->y + e) - dish_field(d->nutr, c->x, c->y - e)) * gain;
    } else if (strat == ST_HIDE) {
        float e = 10;
        sx -= (danger_at(d, c, c->x + e, c->y) - danger_at(d, c, c->x - e, c->y)) * 30;
        sy -= (danger_at(d, c, c->x, c->y + e) - danger_at(d, c, c->x, c->y - e)) * 30;
    } else if (strat == ST_SWARM && st->count > 1) {
        float dx = st->cx - c->x, dy = st->cy - c->y, dd = sqrtf(dx * dx + dy * dy) + 1;
        if (dd > 28) { sx += dx / dd; sy += dy / dd; }
    }
    if (mu[M_GLIDE] && st->count > 1) {               /* gliding: the strain moves as a clump */
        float dx = st->cx - c->x, dy = st->cy - c->y, dd = sqrtf(dx * dx + dy * dy) + 1;
        if (dd > 20) { sx += dx / dd * 0.6f * mu[M_GLIDE]; sy += dy / dd * 0.6f * mu[M_GLIDE]; }
    }
    if (c->species == SP_SYMBIONT && d->strain[DISH_PET].count > 0) {   /* the symbiont keeps close to the pet */
        float dx = d->strain[DISH_PET].cx - c->x, dy = d->strain[DISH_PET].cy - c->y, dd = sqrtf(dx * dx + dy * dy) + 1;
        if (dd > 30) { sx += dx / dd; sy += dy / dd; }
    }
    /* neighbors: soft collisions, adhesion, prey, hosts, crowd */
    int gx = (int)(c->x / GRID_PX), gy = (int)(c->y / GRID_PX);
    float px = 0, py = 0; int crowd = 0, touching = 0;
    int prey = -1; float prey_d = sp->virus ? 50 * 50 : c->species == SP_CILIATE ? 140 * 140 : 90 * 90;
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
                if (touch && !o_virus) touching++;
                /* the guardian eats free viruses and hunters */
                if (c->species == SP_CILIATE && (o_virus ? !o->host : (o->species == SP_MYXO || o->species == SP_VIBRIO))) {
                    if (dd < rr * 0.8f + 2) { kill(d, j, false); continue; }
                    if (dd2 < prey_d) { prey_d = dd2; prey = j; }
                }
                if (!sp->virus && !o_virus && other && o->state == CS_LIVE) {
                    const uint8_t *om = d->strain[o->strain].mut;
                    if (touch && om[M_SPIKES]) c->health -= 0.06f * om[M_SPIKES] * dt;          /* it touched something spiny */
                    if (mu[M_TOXIN] && dd < rr * 2.2f) o->health -= 0.035f * mu[M_TOXIN] * dt;   /* its toxin, a little way out */
                    if (mu[M_PREDATOR] && touch) {                                               /* a predator eats what it touches */
                        float bite = 0.12f * mu[M_PREDATOR] * bite_k(d, o) * dt;
                        o->health -= bite; c->energy += bite * 0.5f;
                    }
                    if (c->strain == DISH_PET && touch && (o->species == SP_DONOR || mu[M_GENETHIEF])) gift(d);   /* a gene, passed or stolen */
                }
                if (!sp->virus && !o_virus) {
                    if (dd < rr * 2.2f) crowd++;
                    if (dd < rr) { float push = (rr - dd) / rr * 60; px += dx / dd * push; py += dy / dd * push; }
                    else if (sp->adhesive && o->strain == c->strain && dd < rr * 1.35f) {
                        float pull = (dd - rr * 1.02f) * 4; px -= dx / dd * pull; py -= dy / dd * pull;
                    }
                    if (sp->prey > 0 && strat == ST_HUNT && o->strain != c->strain && dd < rr + 1 && o->state == CS_LIVE) {
                        float bite = sp->prey * bite_k(d, o) * dt;
                        o->health -= bite; c->energy += bite * (c->species == SP_VIBRIO ? 0.5f : 2.0f);   /* a few seconds of a meal fills a hunter, and a full one stops hunting (a vibrio eats its prey whole) */
                    }
                    if ((sp->prey > 0 || mu[M_PREDATOR]) && o->strain != c->strain && dd2 < prey_d) { prey_d = dd2; prey = j; }
                } else if (sp->virus && !o_virus && !c->host && o->state == CS_LIVE) {
                    if (dd2 < prey_d) { prey_d = dd2; prey = j; }
                    if (dd < body_radius(o) + r) {
                        const uint8_t *om = d->strain[o->strain].mut;
                        float fail = o->g.phage_res + 0.25f * om[M_SLIME] + 0.15f * om[M_TWITCH] + (o->strain == DISH_PET && d->crispr ? 0.9f : 0);
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
    /* a hunter with nothing in reach heads for the nearest colony it could eat (the guardian: for viruses and hunters) */
    if (prey < 0 && strat == ST_HUNT && !sp->virus && (sp->prey > 0 || mu[M_PREDATOR] || c->species == SP_CILIATE)) {
        float best = 1e9f, tx = 0, ty = 0;
        for (int s2 = 0; s2 < DISH_MAX_STRAINS; s2++) {
            const strain_t *o = &d->strain[s2];
            if (!o->used || !o->count || s2 == c->strain) continue;
            bool food = c->species == SP_CILIATE ? (o->species == SP_PHAGE || o->species == SP_MYXO || o->species == SP_VIBRIO) : !SP[o->species].virus;
            float dx = o->cx - c->x, dy = o->cy - c->y, dd2 = dx * dx + dy * dy;
            if (food && dd2 < best) { best = dd2; tx = dx; ty = dy; }
        }
        if (best < 1e9f) { float dd = sqrtf(best) + 1; sx += tx / dd * 1.2f; sy += ty / dd * 1.2f; }
    }
    if (prey >= 0 && (strat == ST_HUNT || sp->virus)) {
        float dx = d->cell[prey].x - c->x, dy = d->cell[prey].y - c->y, dd = sqrtf(dx * dx + dy * dy) + 1;
        sx += dx / dd * 1.5f; sy += dy / dd * 1.5f;
    }
    if (strat == ST_SPREAD && crowd > 0) { sx += px * 0.02f; sy += py * 0.02f; }
    if (d->scare_t > 0) {
        float dx = c->x - d->scare_x, dy = c->y - d->scare_y, dd = sqrtf(dx * dx + dy * dy) + 1;
        if (dd < 140) { sx += dx / dd * 3; sy += dy / dd * 3; mult = 2.2f; }
    }
    if (sx != 0 || sy != 0) {                       /* turn toward the steer */
        float want = atan2f(hy + sy, hx + sx), da = want - c->ang;
        while (da > 3.14159f) da -= TAU;
        while (da < -3.14159f) da += TAU;
        c->ang += clampf(da, -3 * dt, 3 * dt);
    }
    float tx = cosf(c->ang) * speed * mult, ty = sinf(c->ang) * speed * mult;
    float k = clampf(4 * dt, 0, 1);
    c->vx += (tx - c->vx) * k + dish_rand(d, -1, 1) * 18 * dt;
    c->vy += (ty - c->vy) * k + dish_rand(d, -1, 1) * 18 * dt;
    int fc = fidx(c->x, c->y);
    c->x += (c->vx + px + d->flowx[fc] * 0.85f) * dt;
    c->y += (c->vy + py + d->flowy[fc] * 0.85f) * dt;
    if (!sp->adhesive && speed <= 8) c->ang += c->spin * dt;
    if (sp->adhesive) c->ang += dish_rand(d, -0.3f, 0.3f) * dt;

    /* the glass: back inside, heading turned in */
    if (!dish_inside(c->x, c->y, r + 2)) {
        float nx, ny; dish_edge_normal(c->x, c->y, &nx, &ny);
        dish_toward_inside(&c->x, &c->y, r + 2.5f);
        float dot = c->vx * nx + c->vy * ny;
        if (dot > 0) { c->vx -= 2 * dot * nx; c->vy -= 2 * dot * ny; }
        if (cosf(c->ang) * nx + sinf(c->ang) * ny > 0) c->ang = atan2f(-ny, -nx) + dish_rand(d, -1, 1);
    }
}

/* ---- the census, the vault, the brain ---- */
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
        int s = c->strain; cnt[s]++;
        if (c->state == CS_SPORE) spores[s]++;
        acc[s][0] += c->energy; acc[s][1] += c->health; acc[s][2] += c->comfort;   /* (cell_tick's, a tick old) */
        acc[s][3] += dish_field(d->waste, c->x, c->y); acc[s][4] += c->infect > 0;
        acc[s][5] += c->x; acc[s][6] += c->y;
        int gx = (int)(c->x / GRID_PX), gy = (int)(c->y / GRID_PX), n = 0;
        if (gx >= 0 && gx < GW && gy >= 0 && gy < GH) for (int j = g_head[gy * GW + gx]; j >= 0; j = g_next[j]) n++;
        acc[s][7] += n;
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
            st->crowd = clampf((acc[s][7] / n - 1) / 4, 0, 1);
            if (best[s] >= 0 && d->cell[best[s]].health > 0.6f) st->vault = d->cell[best[s]].g;
        }
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
                if (!dying) { emit(DEV_VISIT_GONE, st->species); if (!SPECIES_FRIENDLY[st->species] && d->strain[DISH_PET].count) d->run_visitors++; st->used = false; }
                continue;
            }
            st->stay_t -= dt;
            float k = fminf(1, st->count / 8.0f) * dt;
            if (st->species == SP_LACTO) d->ph = fmaxf(0.12f, d->ph - 0.0015f * k);     /* the acid maker sours the agar */
            if (st->species == SP_HALO) d->salt = fminf(0.9f, d->salt + 0.0012f * k);   /* the halophile salts it */
        } else if (st->count == 0) {
            /* the pet: the vault brings it back once a run; the second time, the run is over */
            if (d->run_over) continue;
            if (st->extinct_t <= 0) {
                if (d->vault_used) { d->run_over = 1; emit(DEV_RUN_OVER, s); continue; }
                st->extinct_t = RESPAWN_S;                /* (before its first mutation the vault always saves a strain: a run has a start) */
                emit(DEV_EXTINCT, s);
            } else if ((st->extinct_t -= dt) <= 0) {
                st->extinct_t = 0;
                if (d->run_muts) d->vault_used = 1;
                seed_colony(d, s, 5, DISH_CX, DISH_CY, 12, &st->vault);
                splat(d->nutr, DISH_CX, DISH_CY, 0.35f, 24, 1.0f);    /* the vault wakes them on a little food */
                st->strat = (strat_t){ ST_FORAGE, 5, 1 };
                emit(DEV_RESPAWN, s);
            }
            continue;
        }
        if (st->count == 0) continue;
        if ((st->ask_t -= dt) <= 0 && brain) {
            strat_t ns = brain(d, s);
            if (ns.id < ST_COUNT) {
                if (ns.id != st->strat.id && s == DISH_PET) emit(DEV_STRAT, s);
                st->strat = ns;
            }
            st->ask_t = ASK_S + dish_rand(d, 0, 1.5f);
        }
    }
}

void dish_tick(dish_t *d, float dt, dish_brain_fn brain) {
    if (dt <= 0) return;
    if (dt > 0.1f) dt = 0.1f;
    d->clock += dt;
    grid_build(d);
    census(d);
    strains_tick(d, dt, brain);
    for (int i = 0; i < DISH_MAX_CELLS; i++) if (d->cell[i].state != CS_FREE) cell_tick(d, i, dt);

    /* fields: the current carries and fades, food and filth spread slowly */
    float fmax = 0;
    for (int c = 0; c < FIELD_N * FIELD_N; c++) {
        float k = expf(-1.4f * dt);
        d->flowx[c] *= k; d->flowy[c] *= k;
        fmax = fmaxf(fmax, fabsf(d->flowx[c]) + fabsf(d->flowy[c]));
        d->drug[c] *= expf(-dt / 90.0f);
        if (d->nutr[c] > 0.45f) { float sp = (d->nutr[c] - 0.45f) * dt / 60.0f; d->nutr[c] -= sp; d->waste[c] = fminf(1, d->waste[c] + sp * 1.5f); }   /* too much food spoils */
        d->waste[c] *= expf(-dt / 1800.0f);
    }
    if (fmax > 1) { advect(d, d->nutr, dt); advect(d, d->waste, dt); advect(d, d->drug, dt); diffuse(d->flowx, 4 * dt); diffuse(d->flowy, 4 * dt); }
    diffuse(d->nutr, 0.6f * dt);
    diffuse(d->waste, 0.15f * dt);
    diffuse(d->drug, 0.5f * dt);
    d->warm *= expf(-0.5f * dt);
    if (d->uv_t > 0) d->uv_t -= dt;
    if (d->scare_t > 0) d->scare_t -= dt;
    parts_tick(d, dt);
    dish_prog_tick(d, dt);

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
    uint32_t magic = SAVE_MAGIC; uint16_t ver = 5;   /* v2 (phase 3): pH, salt, ph_opt - nothing shipped v1, so it is refused; v3: progression at the tail;
                                                       v4: each cell's age, and the badges' trackers; v5: the builds, the run, the draft */
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
    const size_t tail5 = DISH_MAX_STRAINS * (4 + DISH_MUT_N) + 2 + 2 + 4 + 8 + 4 + 4 + 4;
    size_t tail = (ver >= 5 ? tail5 : 0) + (ver >= 4 ? 24 + 2 : ver >= 3 ? 24 : 0);
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
        st->strat = (strat_t){ (uint8_t)(strat < ST_COUNT ? strat : 0), 5, 1 };
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
        for (int s = 0; s < DISH_MAX_STRAINS; s++) { r_bytes(&r, &nd.strain[s].stay_t, 4); r_bytes(&r, nd.strain[s].mut, DISH_MUT_N); }
        uint8_t flags[8];
        r_bytes(&r, &nd.run, 2); r_bytes(&r, &nd.run_visitors, 2); r_bytes(&r, &nd.run_dna0, 4); r_bytes(&r, flags, 8);
        r_bytes(&r, &nd.next_draft_gen, 4); r_bytes(&r, &nd.visit_t, 4); r_bytes(&r, &nd.gift_cd, 4);
        nd.vault_used = flags[0] != 0; nd.run_over = flags[1] != 0; nd.run_muts = flags[2]; nd.crispr = flags[3] != 0;
        nd.drafts = flags[4] > 3 ? 3 : flags[4];
        for (int k = 0; k < 3; k++) nd.offer[k] = flags[5 + k] < DISH_MUT_N ? flags[5 + k] : 0xFF;
        for (int s = 0; s < DISH_MAX_STRAINS; s++) {
            nd.strain[s].stay_t = fin(nd.strain[s].stay_t, -1e9f, 1e9f, 300);
            for (int m = 0; m < DISH_MUT_N; m++) if (nd.strain[s].mut[m] > MUTS[m].max_rank) nd.strain[s].mut[m] = MUTS[m].max_rank;
        }
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
