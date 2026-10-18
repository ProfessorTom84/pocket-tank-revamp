/* dish_render.c — the dark-field microscope.
 *
 * Under a dark-field microscope the field is black and everything alive
 * scatters light: membranes glow at their edges, insides shimmer, the glass
 * rim catches the lamp. On an AMOLED that is the cheapest picture there is
 * (black pixels are off) and the prettiest. Cells are distance fields over
 * one to four segments (a bead, a rod, a pinching pair, a corkscrew), shaded
 * from one lookup table by d^2/r^2 so the hot loop has no sqrt. */
#include "dish_render.h"
#include "dish_mut.h"
#include <math.h>
#include <string.h>

#define TAU 6.2831853f
static float clampf01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
#define FCW ((float)DISH_W / FIELD_N)
#define FCH ((float)DISH_H / FIELD_N)

float dish_render_mood = 0.8f;
int64_t (*dish_render_clock_us)(void);
int64_t dish_render_prof_us[DISH_PROF_N];
static int64_t prof_t;
static void prof(int stage) {
    if (!dish_render_clock_us) return;
    int64_t t = dish_render_clock_us();
    if (stage >= 0) dish_render_prof_us[stage] += t - prof_t;
    prof_t = t;
}

/* ---- a view: world -> screen, with a clip ---- */
typedef struct {
    uint16_t *fb; int stride;
    float s, fx, fy, scx, scy;           /* screen = (world - f) * s + sc */
    int x0, y0, x1, y1;                  /* clip rect, x1/y1 exclusive */
    bool circ; float ccx, ccy, cr2;      /* plus a clip circle (the lens) */
    float dim;                           /* the light off: everything dimmer */
    bool fields_baked;                   /* the background already holds the field glow (only the motes are drawn) */
} view_t;
static view_t frame_view(uint16_t *fb, int stride) {
    view_t v = { fb, stride, 1, 0, 0, 0, 0, 0, 0, DISH_W, DISH_H, false, 0, 0, 0, 1, false };
    return v;
}
#define WX(v, x) (((x) - (v)->fx) * (v)->s + (v)->scx)
#define WY(v, y) (((y) - (v)->fy) * (v)->s + (v)->scy)

static inline bool clip_ok(const view_t *v, int x, int y) {
    if (x < v->x0 || x >= v->x1 || y < v->y0 || y >= v->y1) return false;
    if (v->circ) { float dx = x + 0.5f - v->ccx, dy = y + 0.5f - v->ccy; if (dx * dx + dy * dy > v->cr2) return false; }
    return true;
}
static inline void px_add(const view_t *v, int x, int y, int r, int g, int b) {
    if (!clip_ok(v, x, y)) return;
    uint16_t *p = &v->fb[y * v->stride + x];
    int pr = (*p >> 11) << 3, pg = ((*p >> 5) & 63) << 2, pb = (*p & 31) << 3;
    pr += r; pg += g; pb += b;
    if (pr > 255) { pr = 255; } if (pg > 255) { pg = 255; } if (pb > 255) { pb = 255; }
    *p = (uint16_t)(((pr >> 3) << 11) | ((pg >> 2) << 5) | (pb >> 3));
}
static inline void px_over(const view_t *v, int x, int y, uint32_t rgb, int a) {
    if (a <= 0 || !clip_ok(v, x, y)) return;
    if (a > 255) a = 255;
    uint16_t *p = &v->fb[y * v->stride + x];
    int pr = (*p >> 11) << 3, pg = ((*p >> 5) & 63) << 2, pb = (*p & 31) << 3;
    pr += ((int)((rgb >> 16) & 255) - pr) * a / 255;
    pg += ((int)((rgb >> 8) & 255) - pg) * a / 255;
    pb += ((int)(rgb & 255) - pb) * a / 255;
    *p = (uint16_t)(((pr >> 3) << 11) | ((pg >> 2) << 5) | (pb >> 3));
}
static inline void px_addc(const view_t *v, int x, int y, uint32_t rgb, float k) {
    k *= v->dim;
    if (k <= 0.002f) return;
    px_add(v, x, y, (int)(((rgb >> 16) & 255) * k), (int)(((rgb >> 8) & 255) * k), (int)((rgb & 255) * k));
}

uint32_t dr_hsv(float h, float s, float v) {
    h -= floorf(h); h *= 6;
    int i = (int)h; float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r, g, b;
    switch (i % 6) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break;
                     case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; }
    return ((uint32_t)(r * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(b * 255);
}

/* ---- primitives (screen space) ---- */
static void v_glow(const view_t *v, float x, float y, float r, uint32_t rgb, float k) {
    if (r < 0.6f) r = 0.6f;
    int xa = (int)floorf(x - r), xb = (int)ceilf(x + r), ya = (int)floorf(y - r), yb = (int)ceilf(y + r);
    if (ya < v->y0) { ya = v->y0; } if (yb > v->y1 - 1) { yb = v->y1 - 1; }
    if (xa < v->x0) { xa = v->x0; } if (xb > v->x1 - 1) { xb = v->x1 - 1; }
    float ir2 = 1.0f / (r * r);
    for (int yy = ya; yy <= yb; yy++)
        for (int xx = xa; xx <= xb; xx++) {
            float dx = xx + 0.5f - x, dy = yy + 0.5f - y, t = 1 - (dx * dx + dy * dy) * ir2;
            if (t > 0) px_addc(v, xx, yy, rgb, k * t * t);
        }
}
static void v_disc(const view_t *v, float x, float y, float r, uint32_t rgb, int a) {
    int xa = (int)floorf(x - r - 1), xb = (int)ceilf(x + r + 1), ya = (int)floorf(y - r - 1), yb = (int)ceilf(y + r + 1);
    for (int yy = ya; yy <= yb; yy++)
        for (int xx = xa; xx <= xb; xx++) {
            float dx = xx + 0.5f - x, dy = yy + 0.5f - y, e = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (e > 0) px_over(v, xx, yy, rgb, (int)(a * (e > 1 ? 1 : e)));
        }
}
static void v_ring(const view_t *v, float x, float y, float r, float w, uint32_t rgb, float k) {
    float ro = r + w + 1;
    int xa = (int)floorf(x - ro), xb = (int)ceilf(x + ro), ya = (int)floorf(y - ro), yb = (int)ceilf(y + ro);
    for (int yy = ya; yy <= yb; yy++)
        for (int xx = xa; xx <= xb; xx++) {
            float dx = xx + 0.5f - x, dy = yy + 0.5f - y, e = 1 - fabsf(sqrtf(dx * dx + dy * dy) - r) / w;
            if (e > 0) px_addc(v, xx, yy, rgb, k * e);
        }
}
/* an antialiased line of width w, additive (k) or over (a > 0) */
static void v_line(const view_t *v, float x0, float y0, float x1, float y1, float w, uint32_t rgb, float k, int a) {
    float hw = w * 0.5f + 0.6f;
    int xa = (int)floorf(fminf(x0, x1) - hw), xb = (int)ceilf(fmaxf(x0, x1) + hw);
    int ya = (int)floorf(fminf(y0, y1) - hw), yb = (int)ceilf(fmaxf(y0, y1) + hw);
    if (ya < v->y0) { ya = v->y0; } if (yb > v->y1 - 1) { yb = v->y1 - 1; }
    if (xa < v->x0) { xa = v->x0; } if (xb > v->x1 - 1) { xb = v->x1 - 1; }
    float vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy + 1e-6f;
    for (int yy = ya; yy <= yb; yy++)
        for (int xx = xa; xx <= xb; xx++) {
            float px = xx + 0.5f - x0, py = yy + 0.5f - y0, t = (px * vx + py * vy) / l2;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            float dx = px - t * vx, dy = py - t * vy, e = w * 0.5f + 0.5f - sqrtf(dx * dx + dy * dy);
            if (e <= 0) continue;
            if (e > 1) e = 1;
            if (a > 0) px_over(v, xx, yy, rgb, (int)(a * e)); else px_addc(v, xx, yy, rgb, k * e);
        }
}

/* ---- the cell shader: one LUT over t^2 = d^2 / r^2 ---- */
#define LUT_N 256                         /* t^2 in [0, 4) at 64 steps per unit: out to t = 2 */
static float L_FILL[LUT_N], L_RIM[LUT_N], L_HALO[LUT_N];
static bool g_lut;
static void lut_build(void) {
    for (int i = 0; i < LUT_N; i++) {
        float t = sqrtf(i / 64.0f);
        float inside = t < 0.92f ? 1 : t > 1.04f ? 0 : (1.04f - t) / 0.12f;
        L_FILL[i] = (0.22f + 0.30f * t * t) * inside;
        L_RIM[i] = expf(-((t - 0.95f) / 0.13f) * ((t - 0.95f) / 0.13f));
        L_HALO[i] = t < 0.9f ? 0 : 0.5f * expf(-(t - 0.9f) * 2.4f) * (t > 1.7f ? fmaxf(0, (2.0f - t) / 0.3f) : 1);
    }
    g_lut = true;
}
typedef struct { float ax, ay, bx, by; } seg_t;
typedef struct { uint32_t body, rim, halo; float kf, kr, kh; } shade_t;
static void v_cell(const view_t *v, const seg_t *sg, int n, float r, const shade_t *sh) {
    if (!g_lut) lut_build();
    if (r < 0.8f) r = 0.8f;
    float ext = r * 2.0f;
    float xa = 1e9f, xb = -1e9f, ya = 1e9f, yb = -1e9f;
    for (int k = 0; k < n; k++) {
        xa = fminf(xa, fminf(sg[k].ax, sg[k].bx)); xb = fmaxf(xb, fmaxf(sg[k].ax, sg[k].bx));
        ya = fminf(ya, fminf(sg[k].ay, sg[k].by)); yb = fmaxf(yb, fmaxf(sg[k].ay, sg[k].by));
    }
    int x0 = (int)floorf(xa - ext), x1 = (int)ceilf(xb + ext), y0 = (int)floorf(ya - ext), y1 = (int)ceilf(yb + ext);
    if (x0 < v->x0) { x0 = v->x0; } if (y0 < v->y0) { y0 = v->y0; }
    if (x1 > v->x1 - 1) { x1 = v->x1 - 1; } if (y1 > v->y1 - 1) { y1 = v->y1 - 1; }
    float ir2 = 64.0f / (r * r);
    float br = ((sh->body >> 16) & 255) * sh->kf * v->dim, bg = ((sh->body >> 8) & 255) * sh->kf * v->dim, bb = (sh->body & 255) * sh->kf * v->dim;
    float rr = ((sh->rim >> 16) & 255) * sh->kr * v->dim, rg = ((sh->rim >> 8) & 255) * sh->kr * v->dim, rb = (sh->rim & 255) * sh->kr * v->dim;
    float hr = ((sh->halo >> 16) & 255) * sh->kh * v->dim, hg = ((sh->halo >> 8) & 255) * sh->kh * v->dim, hb = (sh->halo & 255) * sh->kh * v->dim;
    float vx[4], vy[4], il[4];
    for (int k = 0; k < n; k++) { vx[k] = sg[k].bx - sg[k].ax; vy[k] = sg[k].by - sg[k].ay; il[k] = 1.0f / (vx[k] * vx[k] + vy[k] * vy[k] + 1e-6f); }
    for (int yy = y0; yy <= y1; yy++)
        for (int xx = x0; xx <= x1; xx++) {
            float px = xx + 0.5f, py = yy + 0.5f, best = 1e9f;
            for (int k = 0; k < n; k++) {
                float qx = px - sg[k].ax, qy = py - sg[k].ay, t = (qx * vx[k] + qy * vy[k]) * il[k];
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                float dx = qx - t * vx[k], dy = qy - t * vy[k], d2 = dx * dx + dy * dy;
                if (d2 < best) best = d2;
            }
            int i = (int)(best * ir2);
            if (i >= LUT_N) continue;
            float f = L_FILL[i], m = L_RIM[i], h = L_HALO[i];
            px_add(v, xx, yy, (int)(br * f + rr * m + hr * h), (int)(bg * f + rg * m + hg * h), (int)(bb * f + rb * m + hb * h));
        }
}

/* ---- the scene: the agar, the glass, the lamp (baked) ---- */
static uint16_t *g_scene, *g_bg;
/* each band keeps its own keys, so two cores never rebuild the same rows (a band is named by its first row) */
#define BAND_SLOTS 4
static int g_scene_key[BAND_SLOTS] = { -1, -1, -1, -1 }, g_bg_key[BAND_SLOTS] = { -1, -1, -1, -1 };
void dish_render_set_scene(uint16_t *buf) { g_scene = buf; for (int k = 0; k < BAND_SLOTS; k++) g_scene_key[k] = g_bg_key[k] = -1; }
void dish_render_set_background(uint16_t *buf) { g_bg = buf; for (int k = 0; k < BAND_SLOTS; k++) g_bg_key[k] = -1; }
static const uint8_t BAYER4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static inline uint16_t pack_dither(float r, float g, float b, int x, int y) {
    float dth = (BAYER4[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f;
    int ri = (int)(r / 8 + dth), gi = (int)(g / 4 + dth), bi = (int)(b / 8 + dth);
    if (ri > 31) { ri = 31; } if (gi > 63) { gi = 63; } if (bi > 31) { bi = 31; }
    if (ri < 0) { ri = 0; } if (gi < 0) { gi = 0; } if (bi < 0) { bi = 0; }
    return (uint16_t)((ri << 11) | (gi << 5) | bi);
}
static uint32_t hash2(int x, int y) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }
static void scene_build(uint16_t *out, int stride, bool light, float ph, float salt, int ya, int yb) {
    /* the agar fills the glass edge to edge: no rim, no wall - the screen's own edge is the dish's */
    float dim = light ? 1.0f : 0.45f;
    for (int y = ya; y < yb; y++)
        for (int x = 0; x < DISH_W; x++) {
            float dist = dish_edge_dist(x + 0.5f, y + 0.5f);       /* px in from the glass (negative: past a round corner) */
            float r = 0, g = 0, b = 0;
            if (dist > 0) {
                float mott = 2.5f * sinf(x * 0.031f + sinf(y * 0.017f) * 2) * sinf(y * 0.027f + 1.3f);
                float grain = (int)(hash2(x, y) % 7) - 3;
                float base = 7 + mott + grain * 0.6f;
                r = base * 0.75f; g = base * 0.7f; b = base * 1.2f;
                /* the agar is an indicator: sour goes rose-orange, soapy goes violet-blue */
                float pk = fabsf(ph - 0.5f) * 2;
                if (pk > 0.08f) { pk = (pk - 0.08f) * 16; if (ph < 0.5f) { r += pk * 1.0f; g += pk * 0.35f; } else { r += pk * 0.4f; b += pk * 1.0f; } }
                /* salt crusts the agar near the glass in white crystals */
                if (salt > 0.3f && dist < 40 && (hash2(x * 3, y * 7) & 1023) < (salt - 0.3f) * 60 * (1 - dist / 40)) { r += 70; g += 75; b += 80; }
                if (dist < 1) { r *= dist; g *= dist; b *= dist; }   /* (a round corner's edge, antialiased) */
            }
            if (!light) { r *= dim * 0.8f; g *= dim * 0.9f; b *= dim * 1.15f; }
            out[y * stride + x] = pack_dither(r, g, b, x, y);
        }
}

/* ---- the fields: food glows amber, filth murky, the drug cyan ---- */
static void field_rgb(const dish_t *d, int c, float *r, float *g, float *b) {
    float n = d->nutr[c], w = d->waste[c], m = d->drug[c];
    *r = n * 46 + w * 26 + m * 6;
    *g = n * 26 + w * 24 + m * 40;
    *b = n * 6 + w * 4 + m * 52;
}
static void draw_motes(const view_t *v, const dish_t *d);
static void draw_fields_glow(const view_t *v, const dish_t *d);
static void draw_fields(const view_t *v, const dish_t *d) {
    if (!v->fields_baked) draw_fields_glow(v, d);
    draw_motes(v, d);
}
static void draw_fields_glow(const view_t *v, const dish_t *d) {
    if (v->s == 1 && !v->circ) {                     /* the frame: bilinear quads between field centres */
        for (int j = 0; j + 1 < FIELD_N; j++)
            for (int i = 0; i + 1 < FIELD_N; i++) {
                if ((int)((j + 1.5f) * FCH) <= v->y0 || (int)((j + 0.5f) * FCH) >= v->y1) continue;   /* not this band's rows */
                float c[4][3];
                int id[4] = { j * FIELD_N + i, j * FIELD_N + i + 1, (j + 1) * FIELD_N + i, (j + 1) * FIELD_N + i + 1 };
                float mx = 0;
                for (int k = 0; k < 4; k++) { field_rgb(d, id[k], &c[k][0], &c[k][1], &c[k][2]); mx = fmaxf(mx, c[k][0] + c[k][1] + c[k][2]); }
                if (mx < 1.5f) continue;
                int xa = (int)((i + 0.5f) * FCW), xb = (int)((i + 1.5f) * FCW), ya = (int)((j + 0.5f) * FCH), yb = (int)((j + 1.5f) * FCH);
                for (int y = ya > v->y0 ? ya : v->y0; y < yb && y < v->y1; y++) {
                    float b = (y - ya) / (float)(yb - ya);
                    for (int x = xa; x < xb; x++) {
                        float a = (x - xa) / (float)(xb - xa);
                        float w0 = (1 - a) * (1 - b), w1 = a * (1 - b), w2 = (1 - a) * b, w3 = a * b;
                        px_add(v, x, y, (int)((c[0][0] * w0 + c[1][0] * w1 + c[2][0] * w2 + c[3][0] * w3) * v->dim),
                                         (int)((c[0][1] * w0 + c[1][1] * w1 + c[2][1] * w2 + c[3][1] * w3) * v->dim),
                                         (int)((c[0][2] * w0 + c[1][2] * w1 + c[2][2] * w2 + c[3][2] * w3) * v->dim));
                    }
                }
            }
    } else {                                         /* the lens: sample per pixel */
        for (int y = v->y0; y < v->y1; y++)
            for (int x = v->x0; x < v->x1; x++) {
                if (!clip_ok(v, x, y)) continue;
                float wx = (x + 0.5f - v->scx) / v->s + v->fx, wy = (y + 0.5f - v->scy) / v->s + v->fy;
                float n = dish_field(d->nutr, wx, wy), w = dish_field(d->waste, wx, wy), m = dish_field(d->drug, wx, wy);
                px_add(v, x, y, (int)((n * 46 + w * 26 + m * 6) * v->dim), (int)((n * 26 + w * 24 + m * 40) * v->dim), (int)((n * 6 + w * 4 + m * 52) * v->dim));
            }
    }
}
/* food motes: specks of nutrient that twinkle where the food is rich */
static void draw_motes(const view_t *v, const dish_t *d) {
    for (int c = 0; c < FIELD_N * FIELD_N; c++) {
        float n = d->nutr[c];
        if (n < 0.16f) continue;                     /* the bare agar has no motes: they mark food worth swimming to */
        int i = c % FIELD_N, j = c / FIELD_N, motes = n > 0.5f ? 3 : n > 0.3f ? 2 : 1;
        for (int k = 0; k < motes; k++) {
            uint32_t h = hash2(c, k);
            float wx = (i + (h & 255) / 255.0f) * FCW, wy = (j + ((h >> 8) & 255) / 255.0f) * FCH;
            float tw = 0.5f + 0.5f * sinf(d->clock * (1.5f + (h >> 24) * 0.01f) + (h >> 16));
            float k2 = (n - 0.12f) * tw * 1.4f;
            if (k2 < 0.04f) continue;
            v_glow(v, WX(v, wx), WY(v, wy), 2.2f * v->s, 0xFFC870, fminf(0.9f, k2));
        }
    }
}

/* ---- the cells ---- */
/* evolution you can see: a strain raised warm reddens, one raised cold goes icy blue */
static float shown_hue(const genome_t *g) {
    float h = g->hue, to = g->heat_opt > 0.5f ? 0.02f : 0.56f, t = clampf01((fabsf(g->heat_opt - 0.5f) - 0.05f) * 3.0f);
    float dh = to - h; dh -= floorf(dh + 0.5f);          /* the short way round the wheel */
    h += dh * t;
    return h - floorf(h);
}
static shade_t shade_for(const cell_t *c, float bright) {
    float h = shown_hue(&c->g);
    shade_t s;
    if (c->state == CS_SPORE) {
        s.body = dr_hsv(h, 0.15f, 1); s.rim = 0xFFFFFF; s.halo = dr_hsv(h, 0.3f, 0.8f);
        s.kf = 0.85f * bright; s.kr = 1.0f * bright; s.kh = 0.5f * bright;
    } else {
        s.body = dr_hsv(h, 0.70f, 0.75f); s.rim = dr_hsv(h, 0.32f, 1.0f); s.halo = dr_hsv(h, 0.85f, 0.65f);
        s.kf = 0.95f * bright; s.kr = 0.95f * bright; s.kh = 0.55f * bright;
    }
    return s;
}
static void draw_eyes(const view_t *v, float x, float y, float r, float ang, float blinkph, bool mouth) {
    /* the face looks the way the cell is going, kept upright so it always reads as a face */
    float fx = cosf(ang), fy = sinf(ang) * 0.5f;
    float cx = x + fx * r * 0.28f, cy = y + fy * r * 0.28f - r * 0.12f;
    float er = fmaxf(0.9f, r * 0.2f), gap = r * 0.36f;
    bool blink = fmodf(blinkph, 6.0f) < 0.13f;
    for (int e = -1; e <= 1; e += 2) {
        float ex = cx + e * gap;
        if (blink) v_line(v, ex - er, cy, ex + er, cy, fmaxf(0.8f, er * 0.5f), 0x06060A, 0, 230);
        else {
            v_disc(v, ex, cy, er, 0x06060A, 235);
            if (er >= 1.6f) v_disc(v, ex - er * 0.35f, cy - er * 0.35f, er * 0.38f, 0xFFFFFF, 230);
            else px_over(v, (int)(ex - 0.5f), (int)(cy - 0.5f), 0xFFFFFF, 200);
        }
    }
    if (!mouth) return;
    float mood = dish_render_mood, my = cy + r * 0.38f, mw = r * 0.26f, curve = (mood - 0.5f) * r * 0.32f;
    float px = cx - mw, py = my;
    for (int k = 1; k <= 6; k++) {
        float t = -1 + k / 3.0f, nx = cx + t * mw, ny = my + curve * (1 - t * t);
        v_line(v, px, py, nx, ny, fmaxf(0.9f, r * 0.08f), 0x06060A, 0, 220);
        px = nx; py = ny;
    }
    if (mood > 0.65f) { v_glow(v, cx - gap * 1.25f, cy + r * 0.3f, r * 0.2f, 0xFF6F9A, 0.5f); v_glow(v, cx + gap * 1.25f, cy + r * 0.3f, r * 0.2f, 0xFF6F9A, 0.5f); }
}

static void draw_phage(const view_t *v, const cell_t *c, float clock) {
    float s = v->s, x = WX(v, c->x), y = WY(v, c->y), r = dish_cell_radius(c) * s;
    float a = c->health > 0 ? 0.35f + 0.65f * fminf(1, c->health * 1.5f) : 0;
    if (c->state == CS_DYING) a *= 1 - c->dying_t / 0.5f;
    uint32_t col = dr_hsv(shown_hue(&c->g), 0.55f, 1), deep = dr_hsv(shown_hue(&c->g), 0.8f, 0.8f);
    float dx = cosf(c->ang), dy = sinf(c->ang);            /* head away from where it is going to bite */
    float tx = -dx, ty = -dy;                              /* the tail */
    float hx = x, hy = y;
    v_glow(v, hx, hy, r * 2.6f, deep, 0.35f * a);
    /* the capsid: a hexagon with its facets */
    float vxs[6], vys[6];
    for (int k = 0; k < 6; k++) { float t = c->ang + k * TAU / 6; vxs[k] = hx + cosf(t) * r; vys[k] = hy + sinf(t) * r; }
    v_glow(v, hx, hy, r * 1.05f, col, 0.45f * a);
    float lw = fmaxf(0.8f, r * 0.22f);
    for (int k = 0; k < 6; k++) v_line(v, vxs[k], vys[k], vxs[(k + 1) % 6], vys[(k + 1) % 6], lw, col, 0.9f * a, 0);
    for (int k = 0; k < 6; k += 2) v_line(v, hx, hy, vxs[k], vys[k], lw * 0.6f, col, 0.45f * a, 0);
    /* the tail and its legs */
    float t0x = hx + tx * r, t0y = hy + ty * r, t1x = hx + tx * r * 2.3f, t1y = hy + ty * r * 2.3f;
    v_line(v, t0x, t0y, t1x, t1y, lw * 1.2f, col, 0.8f * a, 0);
    float px = -ty, py = tx;
    for (int k = -1; k <= 1; k += 2)
        for (int m = 0; m < 2; m++) {
            float sp = (0.6f + m * 0.7f) * k, tw = sinf(clock * 9 + c->phase + m) * 0.15f;
            float kx = t1x + (px * sp + tx * 0.3f) * r * 0.9f, ky = t1y + (py * sp + ty * 0.3f) * r * 0.9f;
            float fx = kx + (px * (sp * 0.4f) + tx * (0.9f + tw)) * r * 0.8f, fy = ky + (py * (sp * 0.4f) + ty * (0.9f + tw)) * r * 0.8f;
            v_line(v, t1x, t1y, kx, ky, lw * 0.7f, col, 0.7f * a, 0);
            v_line(v, kx, ky, fx, fy, lw * 0.6f, col, 0.6f * a, 0);
        }
}

static const uint8_t NO_MUTS[DISH_MUT_N];
static void draw_muts(const view_t *v, const cell_t *c, const uint8_t *mu, float x, float y, float r, float a, float dx, float dy, float clock);
static void draw_cell(const view_t *v, float clock, const cell_t *c, const uint8_t *mu) {
    if (c->species == SP_PHAGE) { draw_phage(v, c, clock); return; }
    if (!mu) mu = NO_MUTS;
    float s = v->s, x = WX(v, c->x), y = WY(v, c->y);
    float r = dish_cell_radius(c) * s * (1 + 0.22f * fminf(1, c->infect)) * (1 + 0.03f * sinf(c->phase * 0.7f));
    float fade = 1;
    if (c->state == CS_DYING) {
        float t = c->dying_t / 0.5f;
        fade = 1 - t;
        v_ring(v, x, y, r * (1 + 1.6f * t), fmaxf(1, r * 0.25f), dr_hsv(shown_hue(&c->g), 0.3f, 1), 0.7f * (1 - t));
    }
    bool spore = c->state == CS_SPORE;
    if (spore) r *= 0.72f;
    float dx = cosf(c->ang), dy = sinf(c->ang), px = -dy, py = dx;
    float p = c->div_t > 0 ? fminf(1, c->div_t / DISH_DIV_TIME) : 0;
    shade_t sh = shade_for(c, fade * (1 - 0.18f * mu[M_MIMIC]));   /* (mimicry: harder to see, tier by tier) */
    seg_t sg[4]; int n = 0;
    float front = r;                                         /* how far ahead of the centre the face sits */
    switch (spore ? SP_COCCUS : c->species) {
    case SP_COCCUS: case SP_CYANO: {
        if (c->species == SP_CYANO && !spore) {                /* a short filament of beads, the middle one dividing */
            float step = r * 1.55f * (1 + p * 0.5f);
            for (int k = -1; k <= 1; k++) { float bx = x + dx * step * k, by = y + dy * step * k; sg[n++] = (seg_t){ bx, by, bx, by }; }
            front = step;
        } else if (p > 0) {
            float o = r * p * 0.95f;
            sg[n++] = (seg_t){ x + dx * o, y + dy * o, x + dx * o, y + dy * o };
            sg[n++] = (seg_t){ x - dx * o, y - dy * o, x - dx * o, y - dy * o };
            r *= 1 - 0.12f * p;
        } else sg[n++] = (seg_t){ x, y, x, y };
        break; }
    case SP_BACILLUS: case SP_MYXO: {
        float L = r * (c->species == SP_MYXO ? 1.9f : 1.35f) * (1 + p * 0.55f);
        if (p > 0.15f) {                                       /* pinching in the middle */
            float gap = r * (p - 0.15f) * 0.9f;
            sg[n++] = (seg_t){ x - dx * L, y - dy * L, x - dx * gap, y - dy * gap };
            sg[n++] = (seg_t){ x + dx * gap, y + dy * gap, x + dx * L, y + dy * L };
        } else if (c->species == SP_MYXO) {                    /* a gliding, flexing rod */
            float bend = sinf(c->phase * 0.8f) * r * 0.6f;
            float mx = x + px * bend, my = y + py * bend;
            sg[n++] = (seg_t){ x - dx * L, y - dy * L, mx, my };
            sg[n++] = (seg_t){ mx, my, x + dx * L, y + dy * L };
        } else sg[n++] = (seg_t){ x - dx * L, y - dy * L, x + dx * L, y + dy * L };
        front = L;
        break; }
    case SP_SPIRILLUM: {                                       /* a corkscrew: the helix seen from above */
        float step = r * 1.25f, pts[5][2];
        for (int k = 0; k < 5; k++) {
            float along = (k - 2) * step, w = sinf(k * 1.5f - c->phase * 3) * r * 0.85f;
            pts[k][0] = x + dx * along + px * w; pts[k][1] = y + dy * along + py * w;
        }
        for (int k = 0; k < 4; k++) sg[n++] = (seg_t){ pts[k][0], pts[k][1], pts[k + 1][0], pts[k + 1][1] };
        r *= 0.6f; front = step * 2;
        break; }
    case SP_MOLD: {                                            /* a tuft of hyphae: three filaments from one point */
        uint32_t h = c->id * 2654435761u;
        float lens[3] = { 2.4f, 1.8f, 1.5f };
        for (int k = 0; k < 3; k++) {
            float a = c->ang + (k - 1) * 2.1f + ((h >> (k * 5)) & 31) / 31.0f * 0.6f, L = r * lens[k];
            sg[n++] = (seg_t){ x, y, x + cosf(a) * L, y + sinf(a) * L };
        }
        r *= 0.42f; front = r;
        break; }
    case SP_VIBRIO: {                                          /* a comma: a short curved rod */
        float L = r * 1.4f, mx = x + px * r * 0.7f, my = y + py * r * 0.7f;
        sg[n++] = (seg_t){ x - dx * L, y - dy * L, mx, my };
        sg[n++] = (seg_t){ mx, my, x + dx * L, y + dy * L };
        front = L;
        break; }
    case SP_LACTO: {                                           /* rods in pairs, end to end */
        float L = r * 0.9f, gap = r * 0.35f;
        sg[n++] = (seg_t){ x - dx * (gap + L), y - dy * (gap + L), x - dx * gap, y - dy * gap };
        sg[n++] = (seg_t){ x + dx * gap, y + dy * gap, x + dx * (gap + L), y + dy * (gap + L) };
        front = gap + L;
        break; }
    case SP_HALO: {                                            /* square archaea: a box, not a bead */
        float h = r * 0.95f, cxs[4] = { 1, -1, -1, 1 }, cys[4] = { 1, 1, -1, -1 };
        for (int k = 0; k < 4; k++) {
            int k2 = (k + 1) & 3;
            sg[n++] = (seg_t){ x + (dx * cxs[k] + px * cys[k]) * h, y + (dy * cxs[k] + py * cys[k]) * h,
                               x + (dx * cxs[k2] + px * cys[k2]) * h, y + (dy * cxs[k2] + py * cys[k2]) * h };
        }
        r *= 0.4f; front = h;
        break; }
    case SP_SYMBIONT: {                                        /* a pair of beads */
        sg[n++] = (seg_t){ x + dx * r * 0.75f, y + dy * r * 0.75f, x + dx * r * 0.75f, y + dy * r * 0.75f };
        sg[n++] = (seg_t){ x - dx * r * 0.75f, y - dy * r * 0.75f, x - dx * r * 0.75f, y - dy * r * 0.75f };
        r *= 0.85f; front = r * 1.6f;
        break; }
    case SP_CILIATE: case SP_DONOR: {                          /* a big oval (the guardian), a plain rod (the donor) */
        float L = r * (c->species == SP_CILIATE ? 0.55f : 1.2f);
        sg[n++] = (seg_t){ x - dx * L, y - dy * L, x + dx * L, y + dy * L };
        front = L;
        break; }
    default: sg[n++] = (seg_t){ x, y, x, y };
    }
    /* the flagellum: a travelling wave off the back of the swimmers */
    int tails = (c->species == SP_BACILLUS || c->species == SP_VIBRIO) + (mu[M_JET] >= 2);
    if (!spore && tails && c->state != CS_DYING) {
        float bx = x - dx * front, by = y - dy * front;
        float len = r * 4.2f, amp = r * 0.55f;
        uint32_t fc = dr_hsv(shown_hue(&c->g), 0.3f, 1);
        int beads = r > 8 ? 22 : 11;                     /* (the lens wants a smooth whip; the dish, a few beads) */
        for (int f = 0; f < tails; f++) {
            float o = (f - (tails - 1) * 0.5f) * 0.55f, tx = dx * cosf(o) - dy * sinf(o), ty = dy * cosf(o) + dx * sinf(o);
            for (int k = 1; k <= beads; k++) {
                float t = k / (float)beads, along = len * t, w = sinf(t * 9 - c->phase * 5 + f) * amp * t;
                v_glow(v, bx - tx * along - ty * w, by - ty * along + tx * w, fmaxf(1.1f, r * 0.24f), fc, 0.7f * (1 - t * 0.7f) * fade);
            }
        }
    }
    v_cell(v, sg, n, r, &sh);
    if (c->state == CS_DYING) return;
    /* a salt-raised strain grows a thick shell: a second wall just outside the first */
    if (c->g.salt_tol > 0.56f && !spore) {
        shade_t shell = { 0, dr_hsv(shown_hue(&c->g), 0.12f, 1), 0, 0, 0.3f + (c->g.salt_tol - 0.56f) * 1.8f, 0 };
        v_cell(v, sg, n, r * (1.12f + (c->g.salt_tol - 0.56f) * 0.45f), &shell);
    }
    /* a drug-resistant one wears armor: glinting plates that turn round its wall */
    if (c->g.drug_res > 0.35f && !spore) {
        int k = 4 + (int)(c->g.drug_res * 6);
        for (int m = 0; m < k; m++) {
            float a = m * TAU / k + clock * 0.6f;
            v_glow(v, x + cosf(a) * r * 1.25f, y + sinf(a) * r * 1.25f, fmaxf(0.9f, r * 0.22f), 0xFFE08A, 0.25f + c->g.drug_res * 0.5f);
        }
    }
    draw_muts(v, c, mu, x, y, r, front + (front > r * 1.2f ? r : 0), dx, dy, clock);
    if (c->species == SP_MOLD)                               /* the mold's spore heads, at the filaments' tips */
        for (int k = 0; k < n; k++) v_glow(v, sg[k].bx, sg[k].by, fmaxf(1.2f, r * 1.4f), 0xE8F07A, 0.7f);
    if (c->species == SP_CILIATE) {                          /* the guardian's mouth groove, and its ring of cilia */
        v_line(v, x + dx * r * 0.2f, y + dy * r * 0.2f, x + dx * r * 0.9f + px * r * 0.3f, y + dy * r * 0.9f + py * r * 0.3f, fmaxf(1, r * 0.12f), 0xBFE8FF, 0.5f, 0);
        float a = front + r, lw = fmaxf(0.7f, r * 0.07f);
        int k = r < 4 ? 10 : 18;
        for (int m = 0; m < k; m++) {
            float t = m * TAU / k, w = sinf(c->phase * 6 + m) * 0.08f, ct = cosf(t), st_ = sinf(t), cw = cosf(t + w), sw = sinf(t + w);
            v_line(v, x + dx * ct * a + px * st_ * r, y + dy * ct * a + py * st_ * r,
                   x + (dx * cw * a + px * sw * r) * 1.32f, y + (dy * cw * a + py * sw * r) * 1.32f, lw, dr_hsv(shown_hue(&c->g), 0.2f, 1), 0.55f, 0);
        }
    }
    if (c->species == SP_DONOR)                              /* the donor's two long pili, gene beads at their tips */
        for (int m = -1; m <= 1; m += 2) {
            float ct = cosf(m * 0.5f), st_ = sinf(m * 0.5f), a = front + r;
            float ix = x + dx * ct * a + px * st_ * r, iy = y + dy * ct * a + py * st_ * r;
            float ox = x + (dx * ct * a + px * st_ * r) * 2.7f, oy = y + (dy * ct * a + py * st_ * r) * 2.7f;
            v_line(v, ix, iy, ox, oy, fmaxf(0.7f, r * 0.06f), 0xFFE08A, 0.5f, 0); v_glow(v, ox, oy, fmaxf(1, r * 0.25f), 0xFFE08A, 0.8f);
        }
    /* the insides: a nucleoid, granules, the lamp's glint */
    uint32_t hue = dr_hsv(shown_hue(&c->g) + 0.06f, 0.35f, 0.9f);
    float gr = r * 0.32f;
    if (!spore) {
        v_glow(v, x - dx * r * 0.2f, y - dy * r * 0.2f, r * 0.55f, hue, 0.35f);
        for (int k = 0; k < 2; k++) {
            float a = c->ang + c->phase * 0.1f + k * 2.4f;
            v_glow(v, x + cosf(a) * r * 0.45f, y + sinf(a) * r * 0.45f, fmaxf(0.8f, gr * 0.7f), 0xFFFFFF, 0.35f);
        }
    }
    v_glow(v, x - r * 0.38f, y - r * 0.42f, fmaxf(1, r * 0.38f), 0xFFFFFF, spore ? 0.8f : 0.55f);
    /* a phage inside: violet sparks that multiply as it takes over */
    if (c->infect > 0) {
        int k = 1 + (int)(c->infect * 6);
        for (int m = 0; m < k; m++) {
            uint32_t h = hash2((int)(c->x * 7) + m, m * 13);
            float a = (h & 1023) / 1023.0f * TAU + clock * 2, rr = r * 0.6f * ((h >> 10) & 255) / 255.0f;
            v_glow(v, x + cosf(a) * rr, y + sinf(a) * rr, fmaxf(1, r * 0.22f), 0xC07CFF, 0.8f);
        }
    }
    /* the pet's cells wear faces */
    if (c->strain == DISH_PET && !spore && r >= 3.2f) {
        float ax = c->species == SP_COCCUS || c->species == SP_CYANO ? 0 : c->ang;
        float er = c->species == SP_COCCUS ? r : r * 0.95f;
        float fx = c->species == SP_COCCUS ? x : x + dx * front * 0.35f, fy = c->species == SP_COCCUS ? y : y + dy * front * 0.35f;
        draw_eyes(v, fx, fy, er, ax, c->phase * 0.31f + c->gen * 1.7f, r >= 9);
    }
}

/* what the strain evolved, drawn on each cell: a mutation is something you can see. `a` is how far the body
 * reaches along its heading (r for a bead, more for a rod): features sit on the ellipse a x r round the centre. */
static void draw_muts(const view_t *v, const cell_t *c, const uint8_t *mu, float x, float y, float r, float a, float dx, float dy, float clock) {
    float px = -dy, py = dx, ph = c->phase;
    uint32_t rim = dr_hsv(shown_hue(&c->g), 0.2f, 1);
    float lw = fmaxf(0.8f, r * 0.12f);
    #define OUT(t, k, X, Y) do { float ct = cosf(t), st_ = sinf(t); X = x + (dx * ct * a + px * st_ * r) * (k); Y = y + (dy * ct * a + py * st_ * r) * (k); } while (0)
    float ox, oy, ix, iy;
    /* round it: hazes and rings */
    if (mu[M_TOXIN] && !(c->id & 3))                         /* the haze: drawn from every fourth cell, so a colony's cloud costs a quarter */
        v_glow(v, x, y, a * (2.6f + 0.8f * mu[M_TOXIN]), 0x9CD830, 0.05f + 0.025f * mu[M_TOXIN]);
    if (mu[M_LURE]) v_glow(v, x, y, a * (1.6f + 0.3f * mu[M_LURE]), 0x7FE8FF, (0.06f + 0.03f * mu[M_LURE]) * (0.6f + 0.4f * sinf(clock * 2.2f + ph * 0.2f)));
    if (mu[M_BIOFILM]) v_ring(v, x, y, a * 1.3f, fmaxf(1, lw * 1.5f), 0x9FD8FF, 0.1f + 0.06f * mu[M_BIOFILM]);
    if (mu[M_QUORUM]) v_glow(v, x, y, a * 1.3f, 0x7FF4FF, 0.06f * mu[M_QUORUM] * (0.5f + 0.5f * sinf(clock * 3)));   /* the whole strain pulses as one */
    if (mu[M_SWARM])                                         /* a school's wake: short lines behind, all one way */
        v_line(v, x - dx * a, y - dy * a, x - dx * (a + r * (1 + 0.8f * mu[M_SWARM])), y - dy * (a + r * (1 + 0.8f * mu[M_SWARM])), lw, rim, 0.3f, 0);
    /* on the wall */
    if (mu[M_SPIKES]) {
        int k = 5 + 2 * mu[M_SPIKES];
        for (int m = 0; m < k; m++) { float t = m * TAU / k + 0.3f; OUT(t, 1.0f, ix, iy); OUT(t, 1.3f + 0.12f * mu[M_SPIKES], ox, oy); v_line(v, ix, iy, ox, oy, lw * 1.3f, rim, 0.85f, 0); }
    }
    if (mu[M_ZAP]) {                                         /* sparks crawl on the wall, more at each tier */
        for (int m = 0; m < mu[M_ZAP]; m++) {
            float t = clock * (3 + m) + m * 2.1f + ph; OUT(t, 1.08f, ix, iy); OUT(t + 0.5f, 1.25f, ox, oy);
            v_line(v, ix, iy, ox, oy, lw, 0xBFE8FF, 0.8f, 0); v_glow(v, ox, oy, fmaxf(1, r * 0.25f), 0x7FC8FF, 0.6f);
        }
    }
    if (mu[M_GRAPPLE]) for (int m = 0; m < 2 + 2 * mu[M_GRAPPLE]; m++) {   /* pili: thin hooks reaching out */
        float t = m * 1.05f + 0.5f; OUT(t, 1.0f, ix, iy); OUT(t, 1.6f + 0.2f * mu[M_GRAPPLE], ox, oy);
        v_line(v, ix, iy, ox, oy, fmaxf(0.6f, lw * 0.45f), 0xFFC070, 0.45f, 0); v_glow(v, ox, oy, fmaxf(0.8f, r * 0.12f), 0xFFC070, 0.5f);
    }
    if (mu[M_ENGULF]) {                                      /* a maw at the front that opens and shuts */
        float open = 0.35f + 0.2f * sinf(clock * 4 + ph) + 0.08f * mu[M_ENGULF];
        OUT(0.0f, 0.85f, ix, iy); OUT(open, 1.3f, ox, oy); v_line(v, ix, iy, ox, oy, lw * 1.4f, 0xFF5A4E, 0.9f, 0);
        OUT(-open, 1.3f, ox, oy); v_line(v, ix, iy, ox, oy, lw * 1.4f, 0xFF5A4E, 0.9f, 0);
    }
    if (mu[M_JET]) {                                         /* a nozzle at the back */
        OUT(TAU / 2, 1.05f, ox, oy); v_glow(v, ox, oy, fmaxf(1, r * (0.25f + 0.08f * mu[M_JET])), 0xBFEFFF, c->jet_t > 0 ? 1.0f : 0.35f);
        if (c->jet_t > 0) v_line(v, ox, oy, ox - dx * r * 3, oy - dy * r * 3, lw * 1.5f, 0xBFEFFF, 0.6f, 0);
    }
    if (mu[M_TAMER]) { int k = 2 + 2 * mu[M_TAMER]; for (int m = 0; m < k; m++) { OUT(m * TAU / k + clock * 0.5f, 1.35f, ox, oy); v_glow(v, ox, oy, fmaxf(0.9f, r * 0.16f), 0xC07CFF, 0.6f); } }
    if (mu[M_SYMBIOSIS]) for (int m = -1; m <= 1; m += 2) { OUT(m * 1.6f, 1.2f, ox, oy); v_glow(v, ox, oy, fmaxf(1, r * 0.2f), 0xFF7FAA, 0.3f + 0.15f * mu[M_SYMBIOSIS]); }
    if (mu[M_MIMIC]) for (int m = 0; m < 3; m++) {           /* a mottled skin */
        float t = m * 2.1f + c->id; OUT(t, 0.55f, ox, oy); v_glow(v, ox, oy, fmaxf(1, r * 0.35f), 0x405060, 0.35f);
    }
    /* inside */
    for (int m = 0; m < 2 + 2 * mu[M_PHOTO] && mu[M_PHOTO]; m++) { float t = m * 2.39996f + ph * 0.05f; v_glow(v, x + (dx * cosf(t) * a + px * sinf(t) * r) * 0.5f, y + (dy * cosf(t) * a + py * sinf(t) * r) * 0.5f, fmaxf(1, r * 0.2f), 0x4CF070, 0.75f); }
    if (mu[M_SPOREBURST]) for (int m = 0; m < mu[M_SPOREBURST]; m++) { float t = m * 2.1f + 1; v_glow(v, x + (dx * cosf(t) * a + px * sinf(t) * r) * 0.4f, y + (dy * cosf(t) * a + py * sinf(t) * r) * 0.4f, fmaxf(1, r * 0.22f), 0xFFFFFF, 0.6f); }
    if (mu[M_SCOUTS]) v_glow(v, x + dx * a * 0.6f, y + dy * a * 0.6f, fmaxf(1, r * 0.2f), 0xFFE08A, 0.25f * mu[M_SCOUTS]);
    #undef OUT
}

static void draw_heart(const view_t *v, float x, float y, float s, uint32_t col, float k) {
    v_glow(v, x - s * 0.5f, y - s * 0.2f, s * 0.75f, col, k);
    v_glow(v, x + s * 0.5f, y - s * 0.2f, s * 0.75f, col, k);
    v_glow(v, x, y + s * 0.5f, s * 0.65f, col, k);
}
static void draw_parts(const view_t *v, const dish_t *d) {
    for (int i = 0; i < DISH_MAX_PARTS; i++) {
        const part_t *p = &d->part[i];
        if (!p->used) continue;
        float t = p->t / p->life, x = WX(v, p->x), y = WY(v, p->y), s = v->s;
        switch (p->kind) {
        case PT_DROP:   /* a drop falling onto the agar: it shrinks to the point it lands on */
            v_glow(v, x, y, (14 * (1 - t) + 4) * s, p->rgb, 0.9f);
            v_glow(v, x - 3 * (1 - t) * s, y - 3 * (1 - t) * s, (5 * (1 - t) + 1.5f) * s, 0xFFFFFF, 0.6f);
            break;
        case PT_RIPPLE: v_ring(v, x, y, (6 + 34 * t) * s, 1.4f * s, p->rgb, 0.55f * (1 - t)); break;
        case PT_SHARD:  v_glow(v, x, y, 1.8f * s, p->rgb, 0.9f * (1 - t)); break;
        case PT_SPARK:  v_glow(v, x, y, 2.5f * s, p->rgb, 1 - t); break;
        case PT_HEART:  draw_heart(v, x, y, 3.2f * s, 0xFF5C8A, 0.9f * (1 - t)); break;
        case PT_ZAP: {  /* a jagged arc from (x, y) to (vx, vy), re-jagged as it flickers */
            float ex = WX(v, p->vx), ey = WY(v, p->vy), lx = x, ly = y, nx = -(ey - y), ny = ex - x;
            float nl = sqrtf(nx * nx + ny * ny) + 1e-3f;
            nx /= nl; ny /= nl;
            uint32_t h = hash2(i, (int)(p->t * 30));
            for (int k = 1; k <= 4; k++) {
                float f = k / 4.0f, j = k < 4 ? ((int)((h >> (k * 6)) & 31) - 16) * 0.45f * s : 0;
                float qx = x + (ex - x) * f + nx * j, qy = y + (ey - y) * f + ny * j;
                v_line(v, lx, ly, qx, qy, 1.4f * s, 0xBFE8FF, 0.9f * (1 - t), 0);
                lx = qx; ly = qy;
            }
            v_glow(v, ex, ey, 5 * s, 0x7FC8FF, 0.7f * (1 - t));
            break; }
        }
    }
}

/* what the strains are doing to each other, drawn over the cells: tethers, stuns, walls, scouts, blooms */
static void draw_links(const view_t *v, const dish_t *d) {
    for (int i = 0; i < DISH_MAX_CELLS; i++) {
        const cell_t *c = &d->cell[i];
        if (c->state != CS_LIVE) continue;
        float x = WX(v, c->x), y = WY(v, c->y), r = dish_cell_radius(c) * v->s;
        if (c->tether) {                                     /* grapple pili: a taut amber line to what it holds */
            const cell_t *o = &d->cell[c->tether - 1];
            if (o->state == CS_LIVE) v_line(v, x, y, WX(v, o->x), WY(v, o->y), fmaxf(1, 0.9f * v->s), 0xFFC070, 0.7f, 0);
        }
        if (c->stun_t > 0) v_ring(v, x, y, r * 1.6f, fmaxf(1, r * 0.15f), 0x7FC8FF, 0.5f * fminf(1, c->stun_t));
        if (dish_cell_anchor(d, c)) {                        /* a wall's anchor: a thick plate the others hide behind */
            v_ring(v, x, y, r * 1.5f, fmaxf(1.4f, r * 0.35f), 0x9FD8FF, 0.45f);
            v_glow(v, x, y, r * 2.2f, 0x5FA8E8, 0.12f);
        }
        if (dish_cell_scout(d, c)) {                         /* a scout: a bright head and a trail */
            v_glow(v, x, y, r * 1.4f, 0xFFE08A, 0.4f);
            float sp = sqrtf(c->vx * c->vx + c->vy * c->vy) + 1e-3f;
            v_line(v, x, y, x - c->vx / sp * r * 4, y - c->vy / sp * r * 4, fmaxf(1, r * 0.3f), 0xFFE08A, 0.3f, 0);
        }
        const strain_t *st = &d->strain[c->strain];
        if (st->bloom_t < 0)                                 /* a quorum bloom: the colony lights up in a wave of color */
            v_glow(v, x, y, r * 3, dr_hsv(fmodf(d->clock * 0.3f + c->x * 0.004f + c->y * 0.003f, 1), 0.6f, 1), 0.35f);
    }
}
bool dish_render_thoughts = true;
/* a strain's thought, as a little icon over its colony (12 goals, 12 drawings) */
static void draw_thought_icon(const view_t *v, float x, float y, float s, int g, float k) {
    float w = fmaxf(1.2f, s * 0.18f);
    switch (g) {
    case ST_FORAGE: v_glow(v, x, y, s * 0.55f, 0xFFC870, k); v_glow(v, x - s * 0.4f, y + s * 0.3f, s * 0.3f, 0xFFC870, k * 0.7f); break;
    case ST_CLUSTER: for (int m = 0; m < 3; m++) v_disc(v, x + cosf(m * 2.094f) * s * 0.32f, y + sinf(m * 2.094f) * s * 0.32f, s * 0.24f, 0xBFE8FF, (int)(230 * k)); break;
    case ST_EXPLORE: v_line(v, x - s * 0.5f, y, x + s * 0.5f, y, w, 0xBFE8FF, 0, (int)(230 * k));
                     v_line(v, x + s * 0.5f, y, x + s * 0.15f, y - s * 0.35f, w, 0xBFE8FF, 0, (int)(230 * k));
                     v_line(v, x + s * 0.5f, y, x + s * 0.15f, y + s * 0.35f, w, 0xBFE8FF, 0, (int)(230 * k)); break;
    case ST_REST: for (int m = 0; m < 2; m++) { float o = m * s * 0.45f, z = s * (0.3f - m * 0.08f), zx = x - s * 0.2f + o, zy = y + s * 0.2f - o;
                     v_line(v, zx - z, zy - z, zx + z, zy - z, w * 0.8f, 0x9FB8FF, 0, (int)(220 * k)); v_line(v, zx + z, zy - z, zx - z, zy + z, w * 0.8f, 0x9FB8FF, 0, (int)(220 * k));
                     v_line(v, zx - z, zy + z, zx + z, zy + z, w * 0.8f, 0x9FB8FF, 0, (int)(220 * k)); } break;
    case ST_DIVIDE: v_ring(v, x - s * 0.25f, y, s * 0.3f, w * 0.7f, 0x7FF0A0, k); v_ring(v, x + s * 0.25f, y, s * 0.3f, w * 0.7f, 0x7FF0A0, k); break;
    case ST_SPORE: v_ring(v, x, y, s * 0.4f, w, 0xFFFFFF, k); v_glow(v, x, y, s * 0.25f, 0xFFFFFF, k); break;
    case ST_SEEK_LIGHT: case ST_AVOID_LIGHT:
        v_disc(v, x, y, s * 0.25f, 0xFFE07A, (int)(240 * k));
        for (int m = 0; m < 8; m++) { float t = m * TAU / 8; v_line(v, x + cosf(t) * s * 0.36f, y + sinf(t) * s * 0.36f, x + cosf(t) * s * 0.55f, y + sinf(t) * s * 0.55f, w * 0.6f, 0xFFE07A, 0, (int)(220 * k)); }
        if (g == ST_AVOID_LIGHT) v_line(v, x - s * 0.5f, y + s * 0.5f, x + s * 0.5f, y - s * 0.5f, w, 0xFF5A4E, 0, (int)(240 * k));
        break;
    case ST_HUNT: v_line(v, x - s * 0.45f, y - s * 0.35f, x + s * 0.1f, y, w, 0xFF5A4E, 0, (int)(240 * k)); v_line(v, x + s * 0.1f, y, x - s * 0.45f, y + s * 0.35f, w, 0xFF5A4E, 0, (int)(240 * k));
                  v_line(v, x, y - s * 0.35f, x + s * 0.5f, y, w, 0xFF5A4E, 0, (int)(240 * k)); v_line(v, x + s * 0.5f, y, x, y + s * 0.35f, w, 0xFF5A4E, 0, (int)(240 * k)); break;
    case ST_FLEE: v_line(v, x, y - s * 0.5f, x, y + s * 0.12f, w * 1.3f, 0xFFD040, 0, (int)(240 * k)); v_disc(v, x, y + s * 0.42f, w * 0.75f, 0xFFD040, (int)(240 * k)); break;
    case ST_GUARD: v_line(v, x - s * 0.4f, y - s * 0.4f, x + s * 0.4f, y - s * 0.4f, w, 0x7FB8FF, 0, (int)(240 * k));
                   v_line(v, x - s * 0.4f, y - s * 0.4f, x, y + s * 0.5f, w, 0x7FB8FF, 0, (int)(240 * k)); v_line(v, x + s * 0.4f, y - s * 0.4f, x, y + s * 0.5f, w, 0x7FB8FF, 0, (int)(240 * k)); break;
    case ST_BEFRIEND: draw_heart(v, x, y, s * 0.32f, 0xFF5C8A, k); break;
    }
}
static uint32_t goal_color(int g) { return g == ST_HUNT ? 0xFF5A4E : g == ST_FLEE ? 0xFFD040 : g == ST_GUARD ? 0x7FB8FF : 0xFF7FAA; }
static void draw_thoughts(const view_t *v, const dish_t *d) {
    for (int s = 0; s < DISH_MAX_STRAINS; s++) {
        const strain_t *st = &d->strain[s];
        if (!st->used || st->count == 0) continue;
        int g = st->strat.id, t = st->strat.target;
        float x = WX(v, st->cx), y = WY(v, st->cy);
        if (ST_TARGETED(g) && t >= 0 && t < DISH_MAX_STRAINS && d->strain[t].used && d->strain[t].count) {
            /* a dashed line from the colony to the strain its thought is about, marching toward it */
            float tx = WX(v, d->strain[t].cx), ty = WY(v, d->strain[t].cy), lx = tx - x, ly = ty - y, L = sqrtf(lx * lx + ly * ly);
            int n = (int)(L / 14);
            float off = fmodf(d->clock * 1.5f, 1);
            for (int k = 0; k < n; k++) {
                float a = (k + off) / n, b = (k + off + 0.45f) / n;
                if (b > 1) b = 1;
                v_line(v, x + lx * a, y + ly * a, x + lx * b, y + ly * b, 1.6f, goal_color(g), 0.35f, 0);
            }
        }
        /* the bubble pops in when the goal changes, and it is bold while the strain is sure */
        float pop = fminf(1, st->decided_t * 4), sz = 13 * (pop < 1 ? 0.6f + 0.6f * pop : 1);
        float bx = x, by = y - 34;
        if (by < 20) by = y + 34;
        v_disc(v, bx, by, sz, 0x060A14, 190);
        v_ring(v, bx, by, sz, 1.2f, dr_hsv(shown_hue(&st->vault), 0.5f, 1), 0.6f);
        draw_thought_icon(v, bx, by, sz * 1.25f, g, 0.5f + 0.5f * fminf(1, st->strat.confidence));
    }
}

static void draw_world(const view_t *v, const dish_t *d) {
    prof(-1);
    draw_fields(v, d);
    prof(1);
    if (d->warm > 0.02f) v_glow(v, WX(v, d->warm_x), WY(v, d->warm_y), 46 * v->s, 0xFF5A1E, d->warm * 0.9f);
    if (d->beam > 0.02f) {                                   /* the finger's light: a warm pool on the agar */
        v_glow(v, WX(v, d->beam_x), WY(v, d->beam_y), 58 * v->s, 0xFFE6A8, d->beam * 0.45f);
        v_glow(v, WX(v, d->beam_x), WY(v, d->beam_y), 18 * v->s, 0xFFFFFF, d->beam * 0.35f);
    }
    /* spores and the dying under, the living over, the phages on top */
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < DISH_MAX_CELLS; i++) {
            const cell_t *c = &d->cell[i];
            if (c->state == CS_FREE) continue;
            int want = c->species == SP_PHAGE ? 2 : (c->state == CS_LIVE ? 1 : 0);
            if (want != pass) continue;
            float r = dish_cell_radius(c) * 7 * v->s, x = WX(v, c->x), y = WY(v, c->y);   /* 7 radii: a bacillus's tail reaches ~5.5 behind it (a band culled it at 3) */
            if (x + r < v->x0 || x - r >= v->x1 || y + r < v->y0 || y - r >= v->y1) continue;
            draw_cell(v, d->clock, c, d->strain[c->strain].mut);
        }
    draw_links(v, d);
    prof(2);
    draw_parts(v, d);
    if (dish_render_thoughts && v->s == 1 && !v->circ) draw_thoughts(v, d);
    prof(3);
}

void dish_render(const dish_t *d, uint16_t *fb, int stride) { dish_render_band(d, fb, stride, 0, DISH_H); }
void dish_render_band(const dish_t *d, uint16_t *fb, int stride, int y0, int y1) {
    prof(-1);
    int slot = y0 * BAND_SLOTS / DISH_H;
    /* the scene is rebuilt only when what it shows changes: the light, the pH or the salt (in steps) */
    int light = (d->light ? 1 : 0) | (int)(d->ph * 20 + 0.5f) << 1 | (int)(d->salt * 20 + 0.5f) << 6;
    view_t bv = frame_view(g_bg, DISH_W);
    bv.y0 = y0; bv.y1 = y1; bv.dim = d->light ? 1.0f : 0.55f;
    if (g_scene) {
        if (g_scene_key[slot] != light) { scene_build(g_scene, DISH_W, d->light, d->ph, d->salt, y0, y1); g_scene_key[slot] = light; g_bg_key[slot] = -1; }
        /* the background: the scene plus the field glow, rebuilt four times a second (the fields drift slowly) */
        int bk = (int)(d->clock * 4);
        if (g_bg && g_bg_key[slot] != bk) {
            for (int y = y0; y < y1; y++) memcpy(&g_bg[y * DISH_W], &g_scene[y * DISH_W], DISH_W * 2);
            draw_fields_glow(&bv, d);
            g_bg_key[slot] = bk;
        }
        const uint16_t *src = g_bg ? g_bg : g_scene;
        for (int y = y0; y < y1; y++) memcpy(&fb[y * stride], &src[y * DISH_W], DISH_W * 2);
    } else scene_build(fb, stride, d->light, d->ph, d->salt, y0, y1);
    prof(0);
    view_t v = frame_view(fb, stride);
    v.y0 = y0; v.y1 = y1;
    v.dim = d->light ? 1.0f : 0.55f;
    /* the field glow, when the background doesn't already hold it */
    if (!(g_scene && g_bg)) draw_fields_glow(&v, d);
    v.fields_baked = true;
    draw_world(&v, d);
}

void dish_render_lens(const dish_t *d, float wx, float wy, float zoom, uint16_t *fb, int stride, float cx, float cy, float rad) {
    view_t v = frame_view(fb, stride);
    v.s = zoom; v.fx = wx; v.fy = wy; v.scx = cx; v.scy = cy;
    v.x0 = (int)fmaxf(0, cx - rad); v.x1 = (int)fminf(DISH_W, cx + rad + 1);
    v.y0 = (int)fmaxf(0, cy - rad); v.y1 = (int)fminf(DISH_H, cy + rad + 1);
    v.circ = true; v.ccx = cx; v.ccy = cy; v.cr2 = rad * rad;
    v.dim = d->light ? 1.0f : 0.6f;
    /* the lens's own field: deep agar, a vignette toward its edge */
    for (int y = v.y0; y < v.y1; y++)
        for (int x = v.x0; x < v.x1; x++) {
            float ex = x + 0.5f - cx, ey = y + 0.5f - cy, q = sqrtf(ex * ex + ey * ey) / rad;
            if (q > 1) continue;
            float k = 9 * (1 - q * q * 0.6f);
            fb[y * stride + x] = pack_dither(k * 0.7f, k * 0.75f, k * 1.25f, x, y);
        }
    draw_world(&v, d);
    view_t f = frame_view(fb, stride);
    v_ring(&f, cx, cy, rad + 1.5f, 2.2f, 0xBFD8FF, 0.9f);           /* the brass and glass of the eyepiece */
    v_ring(&f, cx, cy, rad + 4.5f, 1.5f, 0x4A6688, 0.8f);
    v_glow(&f, cx - rad * 0.55f, cy - rad * 0.6f, rad * 0.35f, 0xFFFFFF, 0.12f);
}

/* ---- the page helpers ---- */
void dr_add(uint16_t *fb, int stride, int x, int y, uint32_t rgb, int a) { view_t v = frame_view(fb, stride); px_addc(&v, x, y, rgb, a / 255.0f); }
void dr_blend(uint16_t *fb, int stride, int x, int y, uint32_t rgb, int a) { view_t v = frame_view(fb, stride); px_over(&v, x, y, rgb, a); }
void dr_rect(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb, int a) {
    view_t v = frame_view(fb, stride);
    for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) px_over(&v, xx, yy, rgb, a);
}
void dr_round_rect(uint16_t *fb, int stride, int x, int y, int w, int h, int r, uint32_t rgb, int a) {
    view_t v = frame_view(fb, stride);
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) {
            float cx = xx < x + r ? x + r : xx >= x + w - r ? x + w - r : xx + 0.5f;
            float cy = yy < y + r ? y + r : yy >= y + h - r ? y + h - r : yy + 0.5f;
            float dx = xx + 0.5f - cx, dy = yy + 0.5f - cy, e = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (e > 0) px_over(&v, xx, yy, rgb, (int)(a * (e > 1 ? 1 : e)));
        }
}
void dr_glow(uint16_t *fb, int stride, float x, float y, float r, uint32_t rgb, int a) { view_t v = frame_view(fb, stride); v_glow(&v, x, y, r, rgb, a / 255.0f); }
void dr_disc(uint16_t *fb, int stride, float x, float y, float r, uint32_t rgb, int a) { view_t v = frame_view(fb, stride); v_disc(&v, x, y, r, rgb, a); }
void dr_ring(uint16_t *fb, int stride, float x, float y, float r, float w, uint32_t rgb, int a) { view_t v = frame_view(fb, stride); v_ring(&v, x, y, r, w, rgb, a / 255.0f); }
void dr_line(uint16_t *fb, int stride, float x0, float y0, float x1, float y1, float w, uint32_t rgb, int a) { view_t v = frame_view(fb, stride); v_line(&v, x0, y0, x1, y1, w, rgb, 0, a); }

void dr_cell_icon(uint16_t *fb, int stride, species_t sp, const genome_t *g, const uint8_t *mut, float x, float y, float s, float ang, float clock, bool face, float clip_r) {
    cell_t one, *c = &one;                              /* one cell, drawn through the same shader */
    memset(c, 0, sizeof *c);
    c->state = CS_LIVE; c->species = (uint8_t)sp; c->strain = face ? DISH_PET : 1; c->g = *g;
    c->x = 0; c->y = 0; c->ang = ang; c->phase = clock * 4; c->health = 1;
    view_t v = frame_view(fb, stride);
    v.s = s; v.scx = x; v.scy = y;
    if (clip_r > 0) { v.circ = true; v.ccx = x; v.ccy = y; v.cr2 = clip_r * clip_r; }
    draw_cell(&v, clock, c, mut);
}

/* ---- the 5x7 pixel font (the tank's, render.c), drawn with a soft glow ---- */
static const uint8_t FONT5X7[][7] = {
    { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e }, { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },
    { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e }, { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f }, { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
    { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f }, { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c }, { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
    { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 }, { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 }, { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
    { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 }, { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d }, { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
    { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e }, { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 }, { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a }, { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
    { 0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04 }, { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f }, { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
    { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e }, { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f }, { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e },
    { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 }, { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e }, { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e },
    { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e }, { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c },
    { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 }, { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 }, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c },
    { 0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08 }, { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 }, { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 },
    { 0x0c, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 }, { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 }, { 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00 },
    { 0x19, 0x1a, 0x02, 0x04, 0x08, 0x0b, 0x13 }, { 0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00 },
    { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 }, { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 },   /* < > (the name roller) */
};
static const char FONT_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789?!.,-:'/+%~<>";
static const uint8_t *glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    const char *p = ch ? strchr(FONT_CHARS, ch) : NULL;
    return p ? FONT5X7[p - FONT_CHARS] : NULL;
}
int dr_text_w(const char *s, int scale) { int n = (int)strlen(s); return n ? n * 6 * scale - scale : 0; }
int dr_text(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const char *s) {
    view_t v = frame_view(fb, stride);
    int x0 = x;
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = glyph(*s);
        if (!g) continue;
        for (int r = 0; r < 7; r++)
            for (int k = 0; k < 5; k++)
                if (g[r] & (0x10 >> k)) {
                    /* rounded dots, not squares: each pixel of the glyph is a soft-edged disc */
                    float cx = x + k * scale + scale * 0.5f, cy = y + r * scale + scale * 0.5f;
                    if (scale >= 2) v_disc(&v, cx, cy, scale * 0.74f, rgb, 255);
                    else px_over(&v, x + k, y + r, rgb, 255);
                }
    }
    return x - x0 - scale;
}
