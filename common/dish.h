/* dish.h — Pocket Dish: a living petri dish (the dish branch, 2026-10-16).
 *
 * The same split as the tank: this file and dish.c are the world (cells,
 * fields, genomes, the keeper's tools), with no I/O and no OS calls, so they
 * compile unchanged for the sim and the firmware. dish_render.c draws it,
 * dish_ui.c lays the pet's pages over it, dish_brain.c decides.
 *
 * The pet is a STRAIN, not a cell: cells divide and die all the time, the
 * strain is what the keeper names and raises. Each strain asks its brain for
 * a strategy every few seconds; its cells carry the strategy out with
 * physics (run-and-tumble chemotaxis, adhesion, the stir current). */
#ifndef POCKET_DISH_H
#define POCKET_DISH_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* the four glasses (docs/BOARDS.md): the same build flags as the tank */
#if defined(TANK_ROUND)
#define DISH_W 466
#define DISH_H 466
#elif defined(TANK_WATCH)
#define DISH_W 410
#define DISH_H 502
#elif defined(TANK_SQUARE)
#define DISH_W 480
#define DISH_H 480
#else
#define DISH_W 448
#define DISH_H 368
#endif
/* the dish: an ellipse inscribed in the frame (a circle on the bowl and the square) */
#define DISH_CX (DISH_W * 0.5f)
#define DISH_CY (DISH_H * 0.5f)
#define DISH_RX (DISH_W * 0.5f - 10)
#define DISH_RY (DISH_H * 0.5f - 10)

#define DISH_MAX_CELLS   192    /* ponytail: fixed pool; the device budget is set by the renderer, not RAM */
#define DISH_MAX_STRAINS 6
#define DISH_MAX_PARTS   96
#define FIELD_N          32     /* the nutrient / waste / flow / drug grids are FIELD_N x FIELD_N over the frame */
#define DISH_PET         0      /* strain 0 is always the keeper's */
#define DISH_DIV_TIME    2.6f   /* seconds a cell takes to pinch in two */

typedef enum { SP_COCCUS, SP_BACILLUS, SP_SPIRILLUM, SP_CYANO, SP_MYXO, SP_PHAGE, SP_COUNT } species_t;
extern const char *const SPECIES_NAMES[SP_COUNT];

typedef enum { ST_FORAGE, ST_SWARM, ST_DIVIDE, ST_HUNT, ST_HIDE, ST_REST, ST_SPORE, ST_SPREAD, ST_COUNT } strat_id_t;
extern const char *const STRAT_NAMES[ST_COUNT];

typedef struct { uint8_t id; float urgency, confidence; } strat_t;

/* what a cell inherits (copied with small mutations at every division) */
typedef struct {
    float speed;     /* x the species' base swim speed, 0.5..1.6 */
    float size;      /* x the species' base radius, 0.75..1.35 */
    float hue;       /* 0..1 around the color wheel */
    float heat_opt;  /* the temperature it thrives at, 0..1 (0.5 = room) */
    float salt_tol;  /* 0..1 */
    float drug_res;  /* antibiotic resistance 0..1 */
    float phage_res; /* chance a phage fails to get in, 0..1 */
    float div_rate;  /* x how fast it turns energy into a daughter, 0.6..1.6 */
    float ph_opt;    /* the pH it thrives at, 0 acid .. 1 base (0.5 = neutral) */
} genome_t;

typedef enum { CS_FREE, CS_LIVE, CS_SPORE, CS_DYING } cell_state_t;

typedef struct {
    float x, y, vx, vy;
    float ang, spin;         /* heading (rad) and its rate */
    float energy;            /* 0 starve .. 1 ready to divide */
    float health;            /* 0 dead .. 1 */
    float age;               /* seconds */
    float run_t, last_food;  /* run-and-tumble: time left in this run, food sampled at its start */
    float div_t;             /* > 0 while pinching in two (counts up to DIV_TIME) */
    float infect;            /* a phage's payload: at 1 the host bursts */
    float dying_t;           /* the pop animation */
    float phase;             /* animation phase (flagellum, wobble, blink) */
    uint32_t gen;            /* generation (the founders are 0) */
    uint8_t state, species, strain, host;  /* host: a phage clamped onto a cell (index + 1), 0 when free */
    genome_t g;
} cell_t;

typedef struct {
    bool used;
    uint8_t species;
    char name[12];
    strat_t strat;
    float ask_t;             /* seconds until the brain is asked again */
    /* census, refreshed every tick */
    int count, spores;
    float energy, health, comfort, crowd, filth, infected;
    float cx, cy;            /* centroid of the living cells */
    /* the pet's spore vault: the last healthy genome, and the countdown back from extinction */
    genome_t vault;
    float extinct_t;         /* > 0 while extinct: seconds until respawn */
    uint32_t births, deaths, max_gen;
} strain_t;

typedef enum { PT_DROP, PT_RIPPLE, PT_SHARD, PT_SPARK, PT_HEART } part_kind_t;
typedef struct { float x, y, vx, vy, t, life; uint32_t rgb; uint8_t kind, used; } part_t;

typedef struct {
    cell_t cell[DISH_MAX_CELLS];
    strain_t strain[DISH_MAX_STRAINS];
    part_t part[DISH_MAX_PARTS];
    float nutr[FIELD_N * FIELD_N];     /* food, 0..1 per field cell */
    float waste[FIELD_N * FIELD_N];    /* what the cells leave behind, 0..1 */
    float drug[FIELD_N * FIELD_N];     /* antibiotic, 0..1 (fades) */
    float flowx[FIELD_N * FIELD_N], flowy[FIELD_N * FIELD_N];   /* the stir current, px/s */
    float temp;                        /* the dish's temperature 0..1 (0.5 = room) */
    float ph;                          /* 0 acid .. 1 base (0.5 = neutral) */
    float salt;                        /* 0 fresh .. 1 brine (0.2 = agar) */
    float uv_t;                        /* > 0 while the UV lamp is on: mutations run wild */
    float warm_x, warm_y, warm;        /* a finger held on the glass warms the spot under it */
    bool light;
    float clock;
    float outbreak_t;                  /* seconds until the wild phages may drift in */
    float scare_t;                     /* > 0 after three raps */
    float scare_x, scare_y;
    int n_live;
    uint32_t rng;
} dish_t;

typedef strat_t (*dish_brain_fn)(const dish_t *d, int strain);

void dish_init(dish_t *d, uint32_t seed, species_t pet, const char *pet_name);
void dish_tick(dish_t *d, float dt, dish_brain_fn brain);

/* strains and cells */
int  dish_add_strain(dish_t *d, species_t sp, const char *name);
int  dish_spawn(dish_t *d, int strain, float x, float y, const genome_t *g);
genome_t dish_species_genome(species_t sp);
bool dish_inside(float x, float y, float margin);     /* within the dish, at least margin px from the glass */
float dish_rand(dish_t *d, float lo, float hi);

/* the keeper's hands (frame coordinates) */
void dish_feed(dish_t *d, float x, float y, float amount);   /* a drop of nutrient */
void dish_stir(dish_t *d, float x, float y, float dx, float dy);
void dish_warm(dish_t *d, float x, float y, float dt);       /* a finger held still */
void dish_rap(dish_t *d, float x, float y);                  /* three raps: the cells scatter */
void dish_dose(dish_t *d, float x, float y, float amount);   /* antibiotic */
void dish_antiviral(dish_t *d);                              /* clears the free phages */
void dish_wipe(dish_t *d, float x, float y, float r);        /* wipes waste under a stroke */
void dish_uv(dish_t *d, float seconds);                      /* the UV lamp: more mutations, a little harm */
int  dish_add_sample(dish_t *d, species_t sp);               /* a new colony from the sample rack (-1: no room, or already here) */
float dish_stress(const dish_t *d, const cell_t *c, float *comfort);   /* harm/s from heat, pH, salt, UV; comfort 0..1 */
void dish_outbreak(dish_t *d, int n);                        /* wild phages drift in from the rim */

/* the save: a compact byte image (~8 KB), append-only. dish_load returns false
 * (and leaves d alone) on anything it doesn't recognise. */
#define DISH_SAVE_MAX 12288
size_t dish_save(const dish_t *d, uint8_t *out, size_t cap);
bool   dish_load(dish_t *d, const uint8_t *in, size_t len, uint32_t seed);

/* fields */
float dish_field(const float *f, float x, float y);          /* bilinear sample */
int   dish_cell_at(const dish_t *d, float x, float y, float slop);   /* the living cell under a point, or -1 */
float dish_cell_radius(const cell_t *c);

/* events (sounds are feedback, never behavior: one listener, may only enqueue) */
enum { DEV_FEED, DEV_DIVIDE, DEV_POP, DEV_BURST, DEV_INFECT, DEV_EXTINCT, DEV_RESPAWN, DEV_OUTBREAK, DEV_STRAT, DEV_COUNT };
typedef void (*dish_event_fn)(int ev, int strain, void *ud);
void dish_events_set(dish_event_fn fn, void *ud);

/* the rule brain (dish_brain.c) — the LLM's place once the dish model is distilled */
strat_t dish_brain_rules(const dish_t *d, int strain);
void    dish_encode(const dish_t *d, int strain, char *out, int n);   /* the state line a model would read */

#endif
