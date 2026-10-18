/* dish_ai.c — see dish_ai.h. */
#include "dish_ai.h"
#include "llm/q4_model.h"
#include "llm/word_tok.h"
#include "dish_talk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define LINE_N 512
static q4_model_t *g_m;
static word_tok_t g_tok;
static bool g_ok;
static int g_goal[ST_COUNT], g_tgt[4], g_dig[10], g_urg, g_sayw, g_say[SAY_COUNT];   /* the answer words' token ids (g_tgt: other1..3, none) */
static void nolock(void) {}
static void (*g_lock)(void) = nolock, (*g_unlock)(void) = nolock;
int64_t (*dish_ai_clock_us)(void);
static dish_ai_stats_t g_st;

/* one strain's mind: its latest line (posted by the dish), and the model's last answer */
typedef struct {
    char line[LINE_N], answered_line[LINE_N];
    int8_t other[3];
    uint8_t species; bool used, pending, has_ans;
    uint32_t posts, ans_post, n_ans;             /* how many lines it has posted; which one the answer is for; answers so far */
    strat_t ans;
} slot_t;
static slot_t g_slot[DISH_MAX_STRAINS];
#define FRESH_POSTS 2                            /* an answer older than this many posts is stale: the rules answer */

static int tok_of(const char *w) { int t[3]; return word_tok_encode(&g_tok, w, t, 3) == 2 ? t[1] : 0; }
void dish_ai_set_lock(void (*lock)(void), void (*unlock)(void)) { g_lock = lock ? lock : nolock; g_unlock = unlock ? unlock : nolock; }
bool dish_ai_ready(void) { return g_ok; }
dish_ai_stats_t dish_ai_stats(void) { return g_st; }
int dish_ai_answers(int s) { return s >= 0 && s < DISH_MAX_STRAINS && g_slot[s].used ? (int)g_slot[s].n_ans : 0; }

bool dish_ai_open(const uint8_t *model, size_t model_len, const uint8_t *tok, size_t tok_len, void *(*alloc)(size_t)) {
    g_ok = false;
    if (!model || !tok) return false;
    g_m = q4_model_open(model, model_len, alloc);
    if (!g_m) return false;
    const q4_config_t *c = q4_model_config(g_m);
    if (c->seq_len < 60 || word_tok_init(&g_tok, tok, tok_len, c->vocab_size) != 0) return false;
    for (int g = 0; g < ST_COUNT; g++) if (!(g_goal[g] = tok_of(STRAT_NAMES[g]))) return false;
    static const char *const T[4] = { "other1", "other2", "other3", "none" };
    for (int k = 0; k < 4; k++) if (!(g_tgt[k] = tok_of(T[k]))) return false;
    for (int k = 0; k < 10; k++) { char b[4]; snprintf(b, sizeof b, "%d", k); if (!(g_dig[k] = tok_of(b))) return false; }
    if (!(g_urg = tok_of("urgency")) || !(g_sayw = tok_of("say"))) return false;
    for (int k = 0; k < SAY_COUNT; k++) if (!(g_say[k] = tok_of(SAY_WORDS[k]))) return false;
    memset(g_slot, 0, sizeof g_slot);
    g_ok = true;
    return true;
}

/* the closed pick: the best of n candidate ids in the logits, and its softmax share among them */
static int pick(const float *logits, const int *cand, int n, float *p_out) {
    float mx = -1e30f, sum = 0; int best = 0;
    for (int i = 0; i < n; i++) if (logits[cand[i]] > mx) { mx = logits[cand[i]]; best = i; }
    for (int i = 0; i < n; i++) sum += expf(logits[cand[i]] - mx);
    if (p_out) *p_out = 1 / sum;
    return best;
}
bool dish_ai_decide(const char *line, const int8_t other[3], strat_t *out) {
    if (!g_ok) return false;
    char buf[LINE_N + 4];
    snprintf(buf, sizeof buf, "%s ->", line);
    int toks[96], n = word_tok_encode(&g_tok, buf, toks, 96);
    const q4_config_t *c = q4_model_config(g_m);
    if (n < 3 || n + 6 >= c->seq_len) return false;
    int64_t t0 = dish_ai_clock_us ? dish_ai_clock_us() : 0;
    float *lg = q4_model_prefill(g_m, toks, n);
    if (!lg) for (int p = 0; p < n; p++) if (!(lg = q4_model_forward(g_m, toks[p], p))) return false;
    float sure;
    int g = pick(lg, g_goal, ST_COUNT, &sure);
    if (!(lg = q4_model_forward(g_m, g_goal[g], n))) return false;
    int t = ST_TARGETED(g) ? pick(lg, g_tgt, 4, NULL) : 3;             /* (a goal about no one is about none) */
    if (!q4_model_forward(g_m, g_tgt[t], n + 1)) return false;
    if (!(lg = q4_model_forward(g_m, g_urg, n + 2))) return false;    /* "urgency" is always the next word */
    int u = pick(lg, g_dig, 10, NULL);
    if (!q4_model_forward(g_m, g_dig[u], n + 3)) return false;
    if (!(lg = q4_model_forward(g_m, g_sayw, n + 4))) return false;     /* "say", then what it says */
    int say = pick(lg, g_say, SAY_COUNT, NULL);
    out->id = (uint8_t)g;
    out->target = (int8_t)(t < 3 ? other[t] : -1);
    out->urgency = (float)u;
    out->confidence = sure;
    out->say = (uint8_t)say;
    if (dish_ai_clock_us) {
        float ms = (dish_ai_clock_us() - t0) / 1000.0f;
        g_st.ms_mean = g_st.answered ? g_st.ms_mean * 0.9f + ms * 0.1f : ms;
    }
    return true;
}

strat_t dish_ai_brain(const dish_t *d, int s) {
    strat_t rules = dish_brain_rules(d, s);
    if (!g_ok || s < 0 || s >= DISH_MAX_STRAINS) return rules;
    const strain_t *st = &d->strain[s];
    strat_t ans; bool have = false;
    g_lock();
    slot_t *sl = &g_slot[s];
    if (!sl->used || sl->species != st->species) { memset(sl, 0, sizeof *sl); sl->used = true; sl->species = st->species; }   /* a new strain in the slot: a new mind */
    dish_encode(d, s, sl->line, LINE_N);
    memcpy(sl->other, st->other, 3);
    sl->posts++; sl->pending = true;
    if (sl->has_ans && sl->posts - sl->ans_post <= FRESH_POSTS) { ans = sl->ans; have = true; }
    g_st.asked++;
    g_unlock();
    if (!have) { g_st.fallback++; return rules; }
    if (ans.target >= 0 && (ans.target >= DISH_MAX_STRAINS || !d->strain[ans.target].used || ans.target == s)) ans.target = -1;
    g_st.used++;
    return ans;
}

/* how different two lines are, in words: a situation that changed is worth thinking about again */
static int words_changed(const char *a, const char *b) {
    int n = 0;
    while (*a || *b) {
        const char *ea = strchr(a, ' '), *eb = strchr(b, ' ');
        size_t la = ea ? (size_t)(ea - a) : strlen(a), lb = eb ? (size_t)(eb - b) : strlen(b);
        if (la != lb || strncmp(a, b, la)) n++;
        a += la; b += lb; while (*a == ' ') a++; while (*b == ' ') b++;
    }
    return n;
}
bool dish_ai_step(void) {
    if (!g_ok) return false;
    char line[LINE_N]; int8_t other[3]; int best = -1; uint32_t post = 0; uint8_t sp = 0;
    g_lock();
    float bn = 0;
    for (int s = 0; s < DISH_MAX_STRAINS; s++) {
        const slot_t *sl = &g_slot[s];
        if (!sl->used || !sl->pending) continue;
        float need = (float)(sl->posts - sl->ans_post) + words_changed(sl->line, sl->answered_line) * 0.25f + (s == DISH_PET ? 1 : 0) + (sl->has_ans ? 0 : 3);
        if (need > bn) { bn = need; best = s; }
    }
    if (best >= 0) {
        slot_t *sl = &g_slot[best];
        memcpy(line, sl->line, LINE_N); memcpy(other, sl->other, 3);
        post = sl->posts; sp = sl->species; sl->pending = false;
    }
    g_unlock();
    if (best < 0) return false;
    strat_t r;
    bool ok = dish_ai_decide(line, other, &r);
    g_lock();
    slot_t *sl = &g_slot[best];
    if (ok && sl->used && sl->species == sp) {
        sl->ans = r; sl->has_ans = true; sl->ans_post = post; sl->n_ans++;
        memcpy(sl->answered_line, line, LINE_N);
        g_st.answered++;
    }
    g_unlock();
    return true;
}

/* ---- the test model ---- */
#define TM_DIM 64
#define TM_HID 128
#define TM_SEQ 64
static size_t q4_bytes(size_t numel) { return numel / 2 + numel / 64 * 2; }
size_t dish_ai_test_model_size(size_t *tok_len) {
    const char *w[512]; int nl = dish_lexicon(w, 512), v = nl + 3;
    if (tok_len) {
        size_t t = 4 + 3 * 8 + 5 + 5 + 6;                    /* the three specials */
        for (int k = 0; k < nl; k++) t += 8 + 1 + strlen(w[k]);
        *tok_len = t;
    }
    return 256 + (2 * TM_DIM + TM_DIM) * 4 + q4_bytes((size_t)v * TM_DIM) + 4 * q4_bytes(TM_DIM * TM_DIM) + 3 * q4_bytes(TM_DIM * TM_HID);
}
bool dish_ai_test_model(uint8_t *model, size_t model_len, uint8_t *tok, size_t tok_len, uint32_t seed) {
    size_t tl, ml = dish_ai_test_model_size(&tl);
    if (model_len < ml || tok_len < tl) return false;
    const char *w[512]; int nl = dish_lexicon(w, 512);
    /* the tokenizer: llama2.c's tokenizer.bin, as train_tokenizer.py writes it */
    uint8_t *p = tok;
    int32_t mx = 16; memcpy(p, &mx, 4); p += 4;
    static const char *const SPEC[3] = { "<unk>", "\n<s>\n", "\n</s>\n" };
    for (int k = 0; k < nl + 3; k++) {
        char piece[40]; snprintf(piece, sizeof piece, k < 3 ? "%s" : " %s", k < 3 ? SPEC[k] : w[k - 3]);
        float sc = 0; int32_t len = (int32_t)strlen(piece);
        memcpy(p, &sc, 4); memcpy(p + 4, &len, 4); memcpy(p + 8, piece, (size_t)len); p += 8 + len;
    }
    /* the model: version 3, shared classifier, random 4-bit weights at a small scale */
    memset(model, 0, ml);
    uint32_t magic = 0x616b3432; int32_t ver = 3;
    q4_config_t c = { TM_DIM, TM_HID, 1, 2, 2, nl + 3, TM_SEQ };
    memcpy(model, &magic, 4); memcpy(model + 4, &ver, 4); memcpy(model + 8, &c, sizeof c);
    model[8 + 28] = 1; int32_t gs = 64; memcpy(model + 8 + 28 + 1, &gs, 4);
    float *f = (float *)(void *)(model + 256);
    for (int k = 0; k < 3 * TM_DIM; k++) f[k] = 1;            /* the norms */
    uint8_t *q = model + 256 + 3 * TM_DIM * 4;
    uint32_t x = seed | 1;
    /* each tensor in the file's order (emb, wq wk wv wo, w1 w2 w3): its nibbles, then its fp16 scales */
    size_t sizes[8] = { (size_t)c.vocab_size * TM_DIM, TM_DIM * TM_DIM, TM_DIM * TM_DIM, TM_DIM * TM_DIM, TM_DIM * TM_DIM, TM_DIM * TM_HID, TM_HID * TM_DIM, TM_DIM * TM_HID };
    for (int t = 0; t < 8; t++) {
        for (size_t b = 0; b < sizes[t] / 2; b++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; *q++ = (uint8_t)x; }
        for (size_t g = 0; g < sizes[t] / 64; g++) { uint16_t h = 0x2E66; memcpy(q, &h, 2); q += 2; }   /* ~0.1 */
    }
    return q == model + ml;
}
