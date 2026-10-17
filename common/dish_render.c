/* dish_render.c — the dark-field microscope.
 *
 * Under a dark-field microscope the field is black and everything alive
 * scatters light: membranes glow at their edges, insides shimmer, the glass
 * rim catches the lamp. On an AMOLED that is the cheapest picture there is
 * (black pixels are off) and the prettiest. Cells are distance fields over
 * one to four segments (a bead, a rod, a pinching pair, a corkscrew), shaded
 * from one lookup table by d^2/r^2 so the hot loop has no sqrt. */
#include "dish_render.h"
#include <math.h>
#include <string.h>

#define TAU 6.2831853f
static float clampf01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
#define FCW ((float)DISH_W / FIELD_N)
#define FCH ((float)DISH_H / FIELD_N)

float dish_render_mood = 0.8f;

/* ---- a view: world -> screen, with a clip ---- */
typedef struct {
    uint16_t *fb; int stride;
    float s, fx, fy, scx, scy;           /* screen = (world - f) * s + sc */
    int x0, y0, x1, y1;                  /* clip rect, x1/y1 exclusive */
    bool circ; float ccx, ccy, cr2;      /* plus a clip circle (the lens) */
    float dim;                           /* the light off: everything dimmer */
} view_t;
static view_t frame_view(uint16_t *fb, int stride) {
    view_t v = { fb, stride, 1, 0, 0, 0, 0, 0, 0, DISH_W, DISH_H, false, 0, 0, 0, 1 };
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
    if (pr > 255) pr = 255; if (pg > 255) pg = 255; if (pb > 255) pb = 255;
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
#define LUT_N 384                         /* t^2 in [0, 6) at 64 steps per unit */
static float L_FILL[LUT_N], L_RIM[LUT_N], L_HALO[LUT_N];
static bool g_lut;
static void lut_build(void) {
    for (int i = 0; i < LUT_N; i++) {
        float t = sqrtf(i / 64.0f);
        float inside = t < 0.92f ? 1 : t > 1.04f ? 0 : (1.04f - t) / 0.12f;
        L_FILL[i] = (0.22f + 0.30f * t * t) * inside;
        L_RIM[i] = expf(-((t - 0.95f) / 0.13f) * ((t - 0.95f) / 0.13f));
        L_HALO[i] = t < 0.9f ? 0 : 0.5f * expf(-(t - 0.9f) * 2.4f) * (t > 2.2f ? fmaxf(0, (2.45f - t) / 0.25f) : 1);
    }
    g_lut = true;
}
typedef struct { float ax, ay, bx, by; } seg_t;
typedef struct { uint32_t body, rim, halo; float kf, kr, kh; } shade_t;
static void v_cell(const view_t *v, const seg_t *sg, int n, float r, const shade_t *sh) {
    if (!g_lut) lut_build();
    if (r < 0.8f) r = 0.8f;
    float ext = r * 2.45f;
    float xa = 1e9f, xb = -1e9f, ya = 1e9f, yb = -1e9f;
    for (int k = 0; k < n; k++) {
        xa = fminf(xa, fminf(sg[k].ax, sg[k].bx)); xb = fmaxf(xb, fmaxf(sg[k].ax, sg[k].bx));
        ya = fminf(ya, fminf(sg[k].ay, sg[k].by)); yb = fmaxf(yb, fmaxf(sg[k].ay, sg[k].by));
    }
    int x0 = (int)floorf(xa - ext), x1 = (int)ceilf(xb + ext), y0 = (int)floorf(ya - ext), y1 = (int)ceilf(yb + ext);
    if (x0 < v->x0) x0 = v->x0; if (y0 < v->y0) y0 = v->y0;
    if (x1 > v->x1 - 1) x1 = v->x1 - 1; if (y1 > v->y1 - 1) y1 = v->y1 - 1;
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
static uint16_t *g_scene; static int g_scene_light = -1;
void dish_render_set_scene(uint16_t *buf) { g_scene = buf; g_scene_light = -1; }
static const uint8_t BAYER4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static inline uint16_t pack_dither(float r, float g, float b, int x, int y) {
    float dth = (BAYER4[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f;
    int ri = (int)(r / 8 + dth), gi = (int)(g / 4 + dth), bi = (int)(b / 8 + dth);
    if (ri > 31) ri = 31; if (gi > 63) gi = 63; if (bi > 31) bi = 31;
    if (ri < 0) ri = 0; if (gi < 0) gi = 0; if (bi < 0) bi = 0;
    return (uint16_t)((ri << 11) | (gi << 5) | bi);
}
static uint32_t hash2(int x, int y) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }
static void scene_build(uint16_t *out, int stride, bool light, float ph, float salt) {
    float dim = light ? 1.0f : 0.45f;
    float R = (DISH_RX + DISH_RY) * 0.5f;
    const float lx = -0.7071f, ly = -0.7071f;     /* the lamp: upper left */
    for (int y = 0; y < DISH_H; y++)
        for (int x = 0; x < DISH_W; x++) {
            float u = (x + 0.5f - DISH_CX) / DISH_RX, v = (y + 0.5f - DISH_CY) / DISH_RY;
            float q = sqrtf(u * u + v * v), dist = (1 - q) * R;        /* px inside the agar's edge (negative: in the glass) */
            float r = 0, g = 0, b = 0;
            if (dist > -11) {
                float c = q > 1e-3f ? (u * lx + v * ly) / q : 0;      /* toward the lamp */
                float lamp = c > 0 ? c * c * c * c : 0, back = c < 0 ? powf(-c, 6) * 0.35f : 0;
                if (dist > 0) {                                        /* the agar */
                    float men = expf(-dist / 16) * (14 + 30 * lamp);     /* the meniscus catches the lamp */
                    float mott = 2.5f * sinf(x * 0.031f + sinf(y * 0.017f) * 2) * sinf(y * 0.027f + 1.3f);
                    float grain = (int)(hash2(x, y) % 7) - 3;
                    float base = 7 + mott + grain * 0.6f;
                    r = base * 0.75f + men * 0.55f; g = base * 0.7f + men * 0.75f; b = base * 1.2f + men;
                    /* the agar is an indicator: sour goes rose-orange, soapy goes violet-blue */
                    float pk = fabsf(ph - 0.5f) * 2;
                    if (pk > 0.08f) { pk = (pk - 0.08f) * 16; if (ph < 0.5f) { r += pk * 1.0f; g += pk * 0.35f; } else { r += pk * 0.4f; b += pk * 1.0f; } }
                    /* salt crusts the agar near the glass in white crystals */
                    if (salt > 0.3f && dist < 40 && (hash2(x * 3, y * 7) & 1023) < (salt - 0.3f) * 60 * (1 - dist / 40)) { r += 70; g += 75; b += 80; }
                    float edge = expf(-(dist / 1.8f) * (dist / 1.8f)) * (55 + 170 * lamp + 60 * back);
                    r += edge * 0.85f; g += edge * 0.92f; b += edge;
                } else {                                               /* the glass wall */
                    float wall = 10 + 18 * lamp;
                    float outer = expf(-((dist + 9) / 1.5f) * ((dist + 9) / 1.5f)) * (30 + 120 * lamp + 50 * back);
                    float inner = expf(-(dist / 1.8f) * (dist / 1.8f)) * (55 + 170 * lamp + 60 * back);
                    float body = dist > -9 ? 1 : fmaxf(0, 1 - (-9 - dist) / 2);
                    r = wall * body * 0.8f + (outer + inner) * 0.85f; g = wall * body * 0.9f + (outer + inner) * 0.92f; b = wall * body * 1.1f + outer + inner;
                }
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
static void draw_fields(const view_t *v, const dish_t *d) {
    if (v->s == 1 && !v->circ) {                     /* the frame: bilinear quads between field centres */
        for (int j = 0; j + 1 < FIELD_N; j++)
            for (int i = 0; i + 1 < FIELD_N; i++) {
                float c[4][3];
                int id[4] = { j * FIELD_N + i, j * FIELD_N + i + 1, (j + 1) * FIELD_N + i, (j + 1) * FIELD_N + i + 1 };
                float mx = 0;
                for (int k = 0; k < 4; k++) { field_rgb(d, id[k], &c[k][0], &c[k][1], &c[k][2]); mx = fmaxf(mx, c[k][0] + c[k][1] + c[k][2]); }
                if (mx < 1.5f) continue;
                int xa = (int)((i + 0.5f) * FCW), xb = (int)((i + 1.5f) * FCW), ya = (int)((j + 0.5f) * FCH), yb = (int)((j + 1.5f) * FCH);
                for (int y = ya; y < yb; y++) {
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
    /* food motes: specks of nutrient that twinkle where the food is rich */
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

static void draw_cell(const view_t *v, const dish_t *d, const cell_t *c) {
    if (c->species == SP_PHAGE) { draw_phage(v, c, d->clock); return; }
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
    shade_t sh = shade_for(c, fade);
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
    default: sg[n++] = (seg_t){ x, y, x, y };
    }
    /* the flagellum: a travelling wave off the back of the swimmers */
    if (!spore && (c->species == SP_BACILLUS) && c->state != CS_DYING) {
        float L = r * 1.35f, bx = x - dx * L, by = y - dy * L, lx = bx, ly = by;
        float len = r * 4.2f, amp = r * 0.55f;
        uint32_t fc = dr_hsv(shown_hue(&c->g), 0.3f, 1);
        for (int k = 1; k <= 14; k++) {
            float t = k / 14.0f, along = len * t, w = sinf(t * 9 - c->phase * 5) * amp * t;
            float nx = bx - dx * along + px * w, ny = by - dy * along + py * w;
            v_line(v, lx, ly, nx, ny, fmaxf(0.7f, r * 0.16f), fc, 0.55f * (1 - t * 0.7f) * fade, 0);
            lx = nx; ly = ny;
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
            float a = m * TAU / k + d->clock * 0.6f;
            v_glow(v, x + cosf(a) * r * 1.25f, y + sinf(a) * r * 1.25f, fmaxf(0.9f, r * 0.22f), 0xFFE08A, 0.25f + c->g.drug_res * 0.5f);
        }
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
            float a = (h & 1023) / 1023.0f * TAU + d->clock * 2, rr = r * 0.6f * ((h >> 10) & 255) / 255.0f;
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
        }
    }
}

static void draw_world(const view_t *v, const dish_t *d) {
    draw_fields(v, d);
    if (d->warm > 0.02f) v_glow(v, WX(v, d->warm_x), WY(v, d->warm_y), 46 * v->s, 0xFF5A1E, d->warm * 0.9f);
    /* spores and the dying under, the living over, the phages on top */
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < DISH_MAX_CELLS; i++) {
            const cell_t *c = &d->cell[i];
            if (c->state == CS_FREE) continue;
            int want = c->species == SP_PHAGE ? 2 : (c->state == CS_LIVE ? 1 : 0);
            if (want != pass) continue;
            float r = dish_cell_radius(c) * 3 * v->s, x = WX(v, c->x), y = WY(v, c->y);
            if (x + r < v->x0 || x - r >= v->x1 || y + r < v->y0 || y - r >= v->y1) continue;
            draw_cell(v, d, c);
        }
    draw_parts(v, d);
}

void dish_render(const dish_t *d, uint16_t *fb, int stride) {
    static uint16_t *own;
    /* the scene is rebuilt only when what it shows changes: the light, the pH or the salt (in steps) */
    int light = (d->light ? 1 : 0) | (int)(d->ph * 20 + 0.5f) << 1 | (int)(d->salt * 20 + 0.5f) << 6;
    if (g_scene) {
        if (g_scene_light != light) { scene_build(g_scene, DISH_W, d->light, d->ph, d->salt); g_scene_light = light; }
        for (int y = 0; y < DISH_H; y++) memcpy(&fb[y * stride], &g_scene[y * DISH_W], DISH_W * 2);
    } else { (void)own; scene_build(fb, stride, d->light, d->ph, d->salt); }
    view_t v = frame_view(fb, stride);
    v.dim = d->light ? 1.0f : 0.55f;
    /* the lamp's glints wander a little round the rim */
    for (int k = 0; k < 3; k++) {
        float a = -2.356f + (k - 1) * 0.35f + sinf(d->clock * 0.23f + k * 2.1f) * 0.12f;
        float x = DISH_CX + cosf(a) * (DISH_RX - 1.5f), y = DISH_CY + sinf(a) * (DISH_RY - 1.5f);
        v_glow(&v, x, y, k == 1 ? 9 : 6, 0xDDEBFF, (k == 1 ? 0.6f : 0.35f) * (0.75f + 0.25f * sinf(d->clock * 1.1f + k)));
    }
    draw_world(&v, d);
    /* the UV lamp: a violet bar over the dish and its rays, flickering */
    if (d->uv_t > 0) {
        float k = fminf(1, d->uv_t) * (0.75f + 0.25f * sinf(d->clock * 23));
        float ty = DISH_CY - DISH_RY + 4;
        v_line(&v, DISH_CX - 46, ty, DISH_CX + 46, ty, 5, 0xD8B0FF, 0, (int)(255 * k));
        v_glow(&v, DISH_CX, ty, 60, 0x9A4CFF, 0.6f * k);
        for (int m = -3; m <= 3; m++)
            v_line(&v, DISH_CX + m * 12, ty + 4, DISH_CX + m * 52, ty + DISH_RY * 1.1f, 7, 0x7A3CFF, 0.07f * k, 0);
    }
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

void dr_cell_icon(uint16_t *fb, int stride, species_t sp, const genome_t *g, float x, float y, float s, float ang, float clock, bool face, float clip_r) {
    static dish_t tmp;                                  /* a one-cell world, drawn through the same shader */
    cell_t *c = &tmp.cell[0];
    memset(c, 0, sizeof *c);
    c->state = CS_LIVE; c->species = (uint8_t)sp; c->strain = face ? DISH_PET : 1; c->g = *g;
    c->x = 0; c->y = 0; c->ang = ang; c->phase = clock * 4; c->health = 1;
    tmp.clock = clock;
    view_t v = frame_view(fb, stride);
    v.s = s; v.scx = x; v.scy = y;
    if (clip_r > 0) { v.circ = true; v.ccx = x; v.ccy = y; v.cr2 = clip_r * clip_r; }
    draw_cell(&v, &tmp, c);
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
