/* dish_story.c — see dish_story.h. */
#include "dish_story.h"
#include "dish_mut.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define LOG_N    ((int)(sizeof ((dish_t *)0)->log / sizeof ((dish_t *)0)->log[0]))
#define SERIES_N ((int)(sizeof ((dish_t *)0)->series / sizeof ((dish_t *)0)->series[0]))
const char *const DIE_NAMES[DIE_COUNT] = { "HUNGER", "HUNTERS", "A VIRUS", "POISON", "A HARSH DISH", "OLD AGE" };

void dish_story_note(dish_t *d, int type, int a, int b, int v, uint16_t x) {
    int i = d->log_head;
    d->log[i].min = (uint16_t)(d->run_t / 60);
    d->log[i].type = (uint8_t)type; d->log[i].a = (uint8_t)a; d->log[i].b = (uint8_t)b; d->log[i].v = (uint8_t)v; d->log[i].x = x;
    d->log_head = (uint16_t)((i + 1) % LOG_N);
    if (d->log_n < LOG_N) d->log_n++;
}
void dish_story_death(dish_t *d, int strain, int cause) {
    if (strain == DISH_PET && cause >= 0 && cause < DIE_COUNT && d->m_deaths[cause] < 0xFFFF) d->m_deaths[cause]++;
}
void dish_story_birth(dish_t *d, int strain) {
    if (strain != DISH_PET) return;
    if (d->m_births < 0xFFFF) d->m_births++;
    if (d->strain[DISH_PET].births == 1) dish_story_note(d, SE_RECORD, d->strain[DISH_PET].species, 0, 0, 2);   /* the first split */
}

void dish_story_tick(dish_t *d, float dt) {
    const strain_t *pet = &d->strain[DISH_PET];
    if (d->run_t == 0 && pet->used) dish_story_note(d, SE_START, pet->species, 0, 0, 0);
    d->run_t += dt;
    /* records: the colony's size passing a milestone */
    static const uint16_t MILE[] = { 10, 25, 50, 100, 150 };
    for (int k = 0; k < 5; k++)
        if (pet->count >= MILE[k] && d->best_count < MILE[k]) { d->best_count = MILE[k]; dish_story_note(d, SE_RECORD, pet->species, 0, 0, MILE[k]); }
    /* once a minute: a sample, and a heavy loss written up */
    if ((int)(d->run_t / 60) == (int)((d->run_t - dt) / 60)) return;
    float food = 0; int cells = 0;
    for (int c = 0; c < FIELD_N * FIELD_N; c++) if (d->nutr[c] > 0) { food += d->nutr[c]; cells++; }
    int guests = 0; bool threat = false;
    for (int s = 1; s < DISH_MAX_STRAINS; s++)
        if (d->strain[s].used && d->strain[s].count) { guests++; if (!SPECIES_FRIENDLY[d->strain[s].species]) threat = true; }
    int deaths = 0, cause = 0;
    for (int k = 0; k < DIE_COUNT; k++) { deaths += d->m_deaths[k]; if (d->m_deaths[k] > d->m_deaths[cause]) cause = k; }
    if (d->m_deaths[cause] >= 6) dish_story_note(d, SE_LOSS, pet->species, 0, cause, d->m_deaths[cause]);
    int i = d->series_head;
    d->series[i].pet = (uint8_t)(pet->count > 255 ? 255 : pet->count);
    int others = d->n_live - pet->count; if (others < 0) others = 0;
    d->series[i].others = (uint8_t)(others > 255 ? 255 : others);
    d->series[i].food = (uint8_t)fminf(255, cells ? food / cells * 600 : 0);
    d->series[i].births = (uint8_t)(d->m_births > 255 ? 255 : d->m_births);
    d->series[i].deaths = (uint8_t)(deaths > 255 ? 255 : deaths);
    d->series[i].cause = (uint8_t)(deaths ? cause : 0xFF);
    d->series[i].goal = pet->strat.id;
    d->series[i].guests = (uint8_t)(guests | (threat ? 0x80 : 0));
    d->series_head = (uint16_t)((i + 1) % SERIES_N);
    if (d->series_n < SERIES_N) d->series_n++;
    d->m_births = 0; memset(d->m_deaths, 0, sizeof d->m_deaths);
}

int dish_story_count(const dish_t *d) { return d->log_n; }
int dish_story_samples(const dish_t *d) { return d->series_n; }
const void *dish_story_sample(const dish_t *d, int i) {
    if (i < 0 || i >= d->series_n) return NULL;
    int at = (d->series_head - d->series_n + i + SERIES_N * 2) % SERIES_N;
    return &d->series[at];
}

static void up(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 'a' - 'A'; }
/* a goal as the verb of a sentence about one strain doing it to another */
static const char *verb(int g) {
    switch (g) { case ST_HUNT: return "HUNT"; case ST_FLEE: return "FLEE FROM"; case ST_GUARD: return "GUARD"; case ST_BEFRIEND: return "MAKE FRIENDS WITH"; }
    return "WATCH";
}
bool dish_story_line(const dish_t *d, int i, char *out, int n, int *minute) {
    if (i < 0 || i >= d->log_n) return false;
    int at = (d->log_head - 1 - i + LOG_N * 2) % LOG_N;
    const char *pet = d->strain[DISH_PET].name;
    int type = d->log[at].type, a = d->log[at].a, b = d->log[at].b, v = d->log[at].v, x = d->log[at].x;
    char A[16], B[16];
    snprintf(A, sizeof A, "%s", a < SP_COUNT ? SPECIES_NAMES[a] : "GERM"); up(A);
    snprintf(B, sizeof B, "%s", b < SP_COUNT ? SPECIES_NAMES[b] : "GERM"); up(B);
    int pick = (d->log[at].min * 7 + type * 3 + a) % 3;   /* the same event always reads the same way */
    if (minute) *minute = d->log[at].min;
    switch (type) {
    case SE_START: snprintf(out, (size_t)n, pick ? "%s THE %s WOKE IN A FRESH DISH." : "A NEW RUN BEGAN WITH %s THE %s.", pet, A); break;
    case SE_VISIT:
        if (a < SP_COUNT && SPECIES_FRIENDLY[a]) snprintf(out, (size_t)n, pick == 0 ? "A FRIENDLY %s DRIFTED IN." : pick == 1 ? "%sS ARRIVED. THEY SEEM KIND." : "A %s CAME VISITING.", A);
        else snprintf(out, (size_t)n, pick == 0 ? "DANGER: %sS SLIPPED IN FROM THE EDGE." : pick == 1 ? "%sS INVADED THE DISH." : "A %s THREAT APPEARED AT THE RIM.", A);
        break;
    case SE_LEAVE: snprintf(out, (size_t)n, pick ? "THE %sS MOVED ON." : "THE %sS DRIFTED AWAY.", A); break;
    case SE_WIPED: snprintf(out, (size_t)n, pick ? "THE %sS WERE WIPED OUT." : "NO %sS ARE LEFT IN THE DISH.", A); break;
    case SE_DECIDE:
        if (x == 1) snprintf(out, (size_t)n, "%s CHOSE TO %s THE %sS.", pet, verb(v), B);
        else if (x == 2) snprintf(out, (size_t)n, "THE %sS SET OUT TO %s %s.", A, verb(v), pet);
        else snprintf(out, (size_t)n, "THE %sS TURNED TO %s THE %sS.", A, verb(v), B);
        break;
    case SE_MUT:
        if (x > 1) snprintf(out, (size_t)n, "%s'S %s GREW STRONGER: TIER %d.", pet, v < M_COUNT ? MUTS[v].name : "?", x);
        else snprintf(out, (size_t)n, pick ? "%s EVOLVED %s." : "%s GAINED %s.", pet, v < M_COUNT ? MUTS[v].name : "?");
        break;
    case SE_EXTINCT: snprintf(out, (size_t)n, "%s DIED OUT. THE SPORE VAULT STIRRED.", pet); break;
    case SE_COMEBACK: snprintf(out, (size_t)n, v ? "%s CAME BACK. NO MORE CHANCES." : "%s CAME BACK FROM THE VAULT.", pet); break;
    case SE_RUN_OVER: snprintf(out, (size_t)n, "THE RUN ENDED AFTER %d GENERATIONS.", x); break;
    case SE_RECORD: snprintf(out, (size_t)n, x == 2 ? "%s SPLIT IN TWO FOR THE FIRST TIME." : "%s GREW TO %d CELLS.", pet, x); break;
    case SE_BLOOM: snprintf(out, (size_t)n, "%s BLOOMED AT %d CELLS.", pet, x); break;
    case SE_ZAP: snprintf(out, (size_t)n, pick ? "%s ZAPPED %d INTRUDERS." : "SPARKS FLEW: %s STUNNED %d.", pet, x); break;
    case SE_TAMED: snprintf(out, (size_t)n, "%s TAMED A VIRUS. IT FIGHTS FOR THEM NOW.", pet); break;
    case SE_GIFT: snprintf(out, (size_t)n, "A GENE GIFT: A FREE MUTATION."); break;
    case SE_LOSS: snprintf(out, (size_t)n, "%s LOST %d CELLS TO %s.", pet, x, v < DIE_COUNT ? DIE_NAMES[v] : "SOMETHING"); break;
    default: snprintf(out, (size_t)n, "SOMETHING HAPPENED."); break;
    }
    return true;
}
