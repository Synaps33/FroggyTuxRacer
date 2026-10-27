#include "tr_gfx.h"
#include <stdint.h>

#define FOCAL 270

#ifdef TR_DEBUG_GFX
unsigned gfx_dbg_cells, gfx_dbg_tris, gfx_dbg_pix;
static int g_dbg_billboard;
#endif
#define SKY_TOP TR_RGB565(38, 80, 150)
#define SKY_HORIZON TR_RGB565(196, 218, 238)
/* fixed-point light (Q16) */
#define LX (-20971)
#define LY 55705
#define LZ (-26214)

static uint16_t fb[TR_W * TR_H];
static uint32_t zbuf[TR_W * TR_H];

static q8 eye_q8[3];
static v3 eye_world16;
static int32_t cam_r[3], cam_u[3], cam_f[3];


typedef struct {
    int32_t vx, vy, vz;   /* Q8 view space */
    fx u, v;              /* Q16 texture pixels */
    int32_t shade, fog;   /* 0..255 */
} SVert;

typedef struct {
    int x, y;
    fx u, v;
    int32_t invz, shade, fog;
    uint16_t color;
} RV;

uint16_t *gfx_fb(void) { return fb; }

static inline int32_t q10(fx v) { return v >> 6; }

void gfx_set_camera(v3 eye, v3 target)
{
    v3 f = v3_norm(v3_sub(target, eye));
    v3 r = v3_cross(f, v3_make(0, FX_ONE, 0));
    if (v3_len(r) < FX_ONE / 100) r = v3_make(FX_ONE, 0, 0);
    r = v3_norm(r);
    v3 u = v3_cross(r, f);
    eye_world16 = eye;
    eye_q8[0] = eye.x >> 8;
    eye_q8[1] = eye.y >> 8;
    eye_q8[2] = eye.z >> 8;
    cam_r[0] = q10(r.x); cam_r[1] = q10(r.y); cam_r[2] = q10(r.z);
    cam_u[0] = q10(u.x); cam_u[1] = q10(u.y); cam_u[2] = q10(u.z);
    cam_f[0] = q10(f.x); cam_f[1] = q10(f.y); cam_f[2] = q10(f.z);
#ifdef TR_DEBUG_GFX
    tr_log("dbg basis f=(%d,%d,%d) r=(%d,%d,%d) u=(%d,%d,%d) eye=(%d,%d,%d)",
           cam_f[0],cam_f[1],cam_f[2],cam_r[0],cam_r[1],cam_r[2],cam_u[0],cam_u[1],cam_u[2],
           eye_q8[0],eye_q8[1],eye_q8[2]);
#endif
}

static inline void world_to_view(v3 p, int32_t *vx, int32_t *vy, int32_t *vz)
{
    int32_t dx = (p.x >> 8) - eye_q8[0];
    int32_t dy = (p.y >> 8) - eye_q8[1];
    int32_t dz = (p.z >> 8) - eye_q8[2];
    *vx = (int32_t)(((int64_t)dx * cam_r[0] + (int64_t)dy * cam_r[1] + (int64_t)dz * cam_r[2]) >> 10);
    *vy = (int32_t)(((int64_t)dx * cam_u[0] + (int64_t)dy * cam_u[1] + (int64_t)dz * cam_u[2]) >> 10);
    *vz = (int32_t)(((int64_t)dx * cam_f[0] + (int64_t)dy * cam_f[1] + (int64_t)dz * cam_f[2]) >> 10);
}

static inline void view_project(int32_t vx, int32_t vy, int32_t vz, RV *o)
{
    int32_t z = vz < 1 ? 1 : vz;
    o->x = TR_W / 2 + (int)(((int64_t)vx * FOCAL) / z);
    o->y = TR_H / 2 - (int)(((int64_t)vy * FOCAL) / z);
    o->invz = (int32_t)((1u << 24) / (uint32_t)z);
}

void gfx_project(v3 world, int *sx, int *sy, int *visible)
{
    int32_t vx, vy, vz;
    world_to_view(world, &vx, &vy, &vz);
    if (vz < TR_NEAR_Q8 || vz > TR_FAR_Q8 * 2) {
        *visible = 0;
        return;
    }
    RV r;
    view_project(vx, vy, vz, &r);
    *sx = r.x; *sy = r.y; *visible = 1;
}

void gfx_frame_begin(void)
{
    memset(zbuf, 0, sizeof(zbuf));
}

void gfx_clear_depth(void)
{
    memset(zbuf, 0, sizeof(zbuf));
}

void gfx_sky(void)
{
    int top_r = 38, top_g = 80, top_b = 150;
    int hor_r = 196, hor_g = 218, hor_b = 238;
    for (int y = 0; y < TR_H; y++) {
        int r = top_r + (hor_r - top_r) * y / (TR_H - 1);
        int g = top_g + (hor_g - top_g) * y / (TR_H - 1);
        int b = top_b + (hor_b - top_b) * y / (TR_H - 1);
        uint16_t c = TR_RGB565(r, g, b);
        uint16_t *row = &fb[y * TR_W];
        for (int x = 0; x < TR_W; x++) row[x] = c;
    }
}

/* ------------------------------------------------------------------ */
/* texture sampling                                                    */
/* ------------------------------------------------------------------ */

static inline uint16_t texel(tr_tex *t, int x, int y, int wrap)
{
    if (wrap) {
        x &= t->w - 1;
        y &= t->h - 1;
    } else {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= t->w) x = t->w - 1;
        if (y >= t->h) y = t->h - 1;
    }
    return t->px[y * t->w + x];
}

static inline uint8_t texel_a(tr_tex *t, int x, int y, int wrap)
{
    if (!t->a) return 255;
    if (wrap) {
        x &= t->w - 1;
        y &= t->h - 1;
    } else {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= t->w) x = t->w - 1;
        if (y >= t->h) y = t->h - 1;
    }
    return t->a[y * t->w + x];
}

static inline uint16_t shade_px(uint16_t p, int sh)
{
    int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    r = (r * (sh + 1)) >> 8;
    g = (g * (sh + 1)) >> 8;
    b = (b * (sh + 1)) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline uint16_t fog_px(uint16_t p, int fog)
{
    if (fog <= 0) return p;
    const int fr = 25, fg = 51, fb2 = 27;
    int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    r = (r * (255 - fog) + fr * fog) >> 8;
    g = (g * (255 - fog) + fg * fog) >> 8;
    b = (b * (255 - fog) + fb2 * fog) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline uint16_t tint_px(uint16_t p, uint16_t tint)
{
    if (tint == 0xFFFF) return p;
    int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    int tr = (tint >> 11) & 31, tg = (tint >> 5) & 63, tb = tint & 31;
    r = (r * tr) / 31;
    g = (g * tg) / 63;
    b = (b * tb) / 31;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline uint16_t blend565(uint16_t d, uint16_t s, int a)
{
    int r = ((s >> 11) & 31) * a + ((d >> 11) & 31) * (255 - a);
    int g = ((s >> 5) & 63) * a + ((d >> 5) & 63) * (255 - a);
    int b = (s & 31) * a + (d & 31) * (255 - a);
    return (uint16_t)(((r / 255) << 11) | ((g / 255) << 5) | (b / 255));
}

/* ------------------------------------------------------------------ */
/* rasterizer                                                          */
/* ------------------------------------------------------------------ */

#define DF_ALPHA_TEST 1
#define DF_BLEND      2
#define DF_NO_ZTEST   4
#define DF_NO_ZWRITE  8
#define DF_WRAP       16
#define DF_FLAT_DEPTH 32

static void draw_tri_flat(RV a, RV b, RV c, uint16_t color, int flags)
{
    if (a.y > b.y) { RV t = a; a = b; b = t; }
    if (b.y > c.y) { RV t = b; b = c; c = t; }
    if (a.y > b.y) { RV t = a; a = b; b = t; }
    int32_t X0 = a.x * 2, Y0 = a.y * 2;
    int32_t X1 = b.x * 2, Y1 = b.y * 2;
    int32_t X2 = c.x * 2, Y2 = c.y * 2;
    int64_t area = (int64_t)(X1 - X0) * (Y2 - Y0) - (int64_t)(Y1 - Y0) * (X2 - X0);
    if (area == 0) return;
    if (area < 0) {
        RV t = b; b = c; c = t;
        int32_t tx = X1; X1 = X2; X2 = tx;
        int32_t ty = Y1; Y1 = Y2; Y2 = ty;
    }
    int minx = a.x < b.x ? (a.x < c.x ? a.x : c.x) : (b.x < c.x ? b.x : c.x);
    int maxx = a.x > b.x ? (a.x > c.x ? a.x : c.x) : (b.x > c.x ? b.x : c.x);
    int miny = a.y < b.y ? (a.y < c.y ? a.y : c.y) : (b.y < c.y ? b.y : c.y);
    int maxy = a.y > b.y ? (a.y > c.y ? a.y : c.y) : (b.y > c.y ? b.y : c.y);
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= TR_W) maxx = TR_W - 1;
    if (maxy >= TR_H) maxy = TR_H - 1;
    if (minx > maxx || miny > maxy) return;
    int32_t Xcur = minx * 2 + 1, Ystart = miny * 2 + 1;
    int64_t de0dx = (int64_t)(Y1 - Y2) * 2, de0dy = (int64_t)(X2 - X1) * 2;
    int64_t de1dx = (int64_t)(Y2 - Y0) * 2, de1dy = (int64_t)(X0 - X2) * 2;
    int64_t de2dx = (int64_t)(Y0 - Y1) * 2, de2dy = (int64_t)(X1 - X0) * 2;
    int64_t e0 = (int64_t)(X1 - Xcur) * (Y2 - Ystart) - (int64_t)(Y1 - Ystart) * (X2 - Xcur);
    int64_t e1 = (int64_t)(X2 - Xcur) * (Y0 - Ystart) - (int64_t)(Y2 - Ystart) * (X0 - Xcur);
    int64_t e2 = (int64_t)(X0 - Xcur) * (Y1 - Ystart) - (int64_t)(Y0 - Ystart) * (X1 - Xcur);
    int32_t iz = (int32_t)(((int64_t)a.invz + b.invz + c.invz) / 3);
    int sh = a.shade;
    if (sh < 0) sh = 0; if (sh > 255) sh = 255;
    uint16_t col = shade_px(color, sh);
    for (int y = miny; y <= maxy; y++) {
        int64_t e0r = e0, e1r = e1, e2r = e2;
        for (int x = minx; x <= maxx; x++) {
            if (e0r >= 0 && e1r >= 0 && e2r >= 0) {
                int idx = y * TR_W + x;
                if ((flags & DF_NO_ZTEST) || iz > (int32_t)zbuf[idx]) {
                    fb[idx] = col;
                    if (!(flags & DF_NO_ZWRITE)) zbuf[idx] = (uint32_t)iz;
                }
            }
            e0r += de0dx; e1r += de1dx; e2r += de2dx;
        }
        e0 += de0dy; e1 += de1dy; e2 += de2dy;
    }
}

static void draw_tri_tex(RV a, RV b, RV c, tr_tex *tex, uint16_t tint, int alpha, int flags)
{
    if (!tex && (flags & DF_FLAT_DEPTH)) {
        draw_tri_flat(a, b, c, a.color, flags);
        return;
    }
    if (a.y > b.y) { RV t = a; a = b; b = t; }
    if (b.y > c.y) { RV t = b; b = c; c = t; }
    if (a.y > b.y) { RV t = a; a = b; b = t; }

    int32_t X0 = a.x * 2, Y0 = a.y * 2;
    int32_t X1 = b.x * 2, Y1 = b.y * 2;
    int32_t X2 = c.x * 2, Y2 = c.y * 2;
    int64_t area = (int64_t)(X1 - X0) * (Y2 - Y0) - (int64_t)(Y1 - Y0) * (X2 - X0);
    if (area == 0) return;
    if (area < 0) {
        RV t = b; b = c; c = t;
        int32_t tx = X1; X1 = X2; X2 = tx;
        int32_t ty = Y1; Y1 = Y2; Y2 = ty;
        area = -area;
    }

    int minx = a.x < b.x ? (a.x < c.x ? a.x : c.x) : (b.x < c.x ? b.x : c.x);
    int maxx = a.x > b.x ? (a.x > c.x ? a.x : c.x) : (b.x > c.x ? b.x : c.x);
    int miny = a.y < b.y ? (a.y < c.y ? a.y : c.y) : (b.y < c.y ? b.y : c.y);
    int maxy = a.y > b.y ? (a.y > c.y ? a.y : c.y) : (b.y > c.y ? b.y : c.y);
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= TR_W) maxx = TR_W - 1;
    if (maxy >= TR_H) maxy = TR_H - 1;
    if (minx > maxx || miny > maxy) return;

    int64_t de0dx = (int64_t)(Y1 - Y2) * 2, de0dy = (int64_t)(X2 - X1) * 2;
    int64_t de1dx = (int64_t)(Y2 - Y0) * 2, de1dy = (int64_t)(X0 - X2) * 2;
    int64_t de2dx = (int64_t)(Y0 - Y1) * 2, de2dy = (int64_t)(X1 - X0) * 2;
    int64_t inv_area = (1LL << 40) / area;

    int64_t gux = 0, guy = 0, gvx = 0, gvy = 0, giz = 0, giy = 0;
    if (tex) {
        int64_t nux = de0dx * (int64_t)a.u + de1dx * (int64_t)b.u + de2dx * (int64_t)c.u;
        int64_t nuy = de0dy * (int64_t)a.u + de1dy * (int64_t)b.u + de2dy * (int64_t)c.u;
        int64_t nvx = de0dx * (int64_t)a.v + de1dx * (int64_t)b.v + de2dx * (int64_t)c.v;
        int64_t nvy = de0dy * (int64_t)a.v + de1dy * (int64_t)b.v + de2dy * (int64_t)c.v;
        gux = (nux * inv_area) >> 32;
        guy = (nuy * inv_area) >> 32;
        gvx = (nvx * inv_area) >> 32;
        gvy = (nvy * inv_area) >> 32;
    }
    int64_t nix = de0dx * (int64_t)a.invz + de1dx * (int64_t)b.invz + de2dx * (int64_t)c.invz;
    int64_t niy = de0dy * (int64_t)a.invz + de1dy * (int64_t)b.invz + de2dy * (int64_t)c.invz;
    giz = (nix * inv_area) >> 32;
    giy = (niy * inv_area) >> 32;

    int dx0 = minx - a.x, dy0 = miny - a.y;
    int64_t u_acc = (int64_t)a.u * 256 + gux * dx0 + guy * dy0;
    int64_t v_acc = (int64_t)a.v * 256 + gvx * dx0 + gvy * dy0;
    int64_t i_acc = (int64_t)a.invz * 256 + giz * dx0 + giy * dy0;
    int sh = a.shade, fog = a.fog;
    if (sh < 0) sh = 0;
    if (sh > 255) sh = 255;
    if (fog < 0) fog = 0;
    if (fog > 255) fog = 255;

    int32_t Xcur = minx * 2 + 1;
    int32_t Ystart = miny * 2 + 1;
    int64_t e0 = (int64_t)(X1 - Xcur) * (Y2 - Ystart) - (int64_t)(Y1 - Ystart) * (X2 - Xcur);
    int64_t e1 = (int64_t)(X2 - Xcur) * (Y0 - Ystart) - (int64_t)(Y2 - Ystart) * (X0 - Xcur);
    int64_t e2 = (int64_t)(X0 - Xcur) * (Y1 - Ystart) - (int64_t)(Y0 - Ystart) * (X1 - Xcur);
    int64_t s0x = de0dx, s1x = de1dx, s2x = de2dx;

    for (int y = miny; y <= maxy; y++) {
        int64_t e0r = e0, e1r = e1, e2r = e2;
        int64_t ur = u_acc, vr = v_acc, ir = i_acc;
        for (int x = minx; x <= maxx; x++) {
            if (e0r >= 0 && e1r >= 0 && e2r >= 0) {
                int32_t iz = (int32_t)(ir >> 8);
                int idx = y * TR_W + x;
                if ((flags & DF_NO_ZTEST) || iz > (int32_t)zbuf[idx]) {
                    int alpha_px = alpha;
                    uint16_t col;
                    if (tex) {
                        int tx = (int)(ur >> 16), ty = (int)(vr >> 16);
                        int wrap = (flags & DF_WRAP) != 0;
                        if (flags & DF_ALPHA_TEST) {
                            int ta = texel_a(tex, tx, ty, wrap);
                            if (ta < 96) goto nextpx;
                            alpha_px = (alpha_px * ta) >> 8;
                        } else if (tex->a) {
                            alpha_px = (alpha_px * texel_a(tex, tx, ty, wrap)) >> 8;
                        }
                        col = texel(tex, tx, ty, wrap);
                        col = shade_px(col, sh);
                        col = tint_px(col, tint);
                        col = fog_px(col, fog);
                    } else {
                        col = fog_px(shade_px(a.color, sh), fog);
                    }
                    if (alpha_px <= 0) goto nextpx;
                    if ((flags & DF_BLEND) && alpha_px < 255) col = blend565(fb[idx], col, alpha_px);
                    fb[idx] = col;
                    if (!(flags & DF_NO_ZWRITE)) zbuf[idx] = (uint32_t)iz;
                }
            }
nextpx:
            e0r += s0x; e1r += s1x; e2r += s2x;
            ur += gux; vr += gvx; ir += giz;
        }
        e0 += de0dy; e1 += de1dy; e2 += de2dy;
        u_acc += guy; v_acc += gvy; i_acc += giy;
        (void)Xcur; (void)Ystart;
    }
}

/* clip a triangle in view space, project and draw */
static void emit_tri(SVert a, SVert b, SVert c, tr_tex *tex, uint16_t color,
                     uint16_t tint, int alpha, int flags)
{
    SVert in[5], out[5];
    int n = 0;
    in[n++] = a; in[n++] = b; in[n++] = c;
    int m = 0;
    for (int i = 0; i < n; i++) {
        SVert *p = &in[i], *q = &in[(i + 1) % n];
        bool pin = p->vz >= TR_NEAR_Q8;
        bool qin = q->vz >= TR_NEAR_Q8;
        if (pin) out[m++] = *p;
        if (pin != qin) {
            fx t = fx_div(TR_NEAR_Q8 - p->vz, q->vz - p->vz);
            SVert r;
            r.vx = p->vx + (int32_t)(((int64_t)(q->vx - p->vx) * t) >> 16);
            r.vy = p->vy + (int32_t)(((int64_t)(q->vy - p->vy) * t) >> 16);
            r.vz = TR_NEAR_Q8;
            r.u = p->u + fx_mul(q->u - p->u, t);
            r.v = p->v + fx_mul(q->v - p->v, t);
            r.shade = p->shade + (int32_t)(((q->shade - p->shade) * (int64_t)t) >> 16);
            r.fog = p->fog + (int32_t)(((q->fog - p->fog) * (int64_t)t) >> 16);
            out[m++] = r;
        }
    }
    if (m < 3) return;

    RV rv[5];
    for (int i = 0; i < m; i++) {
        view_project(out[i].vx, out[i].vy, out[i].vz, &rv[i]);
        rv[i].u = out[i].u;
        rv[i].v = out[i].v;
        rv[i].shade = out[i].shade;
        rv[i].fog = out[i].fog;
        rv[i].color = color;
    }
    for (int i = 1; i + 1 < m; i++)
        draw_tri_tex(rv[0], rv[i], rv[i + 1], tex, tint, alpha, flags);
}

/* ------------------------------------------------------------------ */
/* terrain                                                             */
/* ------------------------------------------------------------------ */

static inline int surface_tex_id(tr_course *c, int ix, int iy, int step)
{
    int s = 0;
    int samples = 0;
    for (int dy = 0; dy <= step; dy++) {
        for (int dx = 0; dx <= step; dx++) {
            int py = iy + dy, px = ix + dx;
            if (py >= c->ny) py = c->ny - 1;
            if (px >= c->nx) px = c->nx - 1;
            s += c->surfaces[py * c->nx + px];
            samples++;
        }
    }
    /* Surface values: 0=ice, 1=rock, 2=snow. Average: */
    int avg = (s * 10) / samples;
    if (avg < 5) return 0;   /* Ice */
    if (avg >= 15) return 2; /* Snow */
    return 1;                /* Rock */
}

static void draw_terrain_tri(SVert a, SVert b, SVert c, tr_tex *tex)
{
#ifdef TR_DEBUG_GFX
    gfx_dbg_tris++;
#endif
    emit_tri(a, b, c, tex, 0, 0xFFFF, 255, DF_WRAP);
}

void gfx_draw_terrain(tr_course *c, fx player_x, fx player_z)
{
    (void)player_x;
#ifdef TR_DEBUG_GFX
    static int dbg_once;
    fx dbgpx = player_x, dbgpz = player_z;
#endif
    int nx = c->nx, ny = c->ny;
    if (nx < 2 || ny < 2) return;
    fx dist = -player_z;
    int row = fx_int(fx_div(dist, c->length) * (ny - 1));
    int r0 = row - 2, r1 = row + 52;
    if (r0 < 0) r0 = 0;
    if (r1 > ny - 2) r1 = ny - 2;
    fx cellw = fx_div(c->width, (fx)((nx - 1) << 16));
    fx celll = fx_div(c->length, (fx)((ny - 1) << 16));

    for (int iy = r0; iy <= r1; ) {
        int step = (iy < row + 10) ? 1 : 2;
        if (iy + step >= ny) step = 1;

        for (int ix = 0; ix + step < nx; ix += step) {
            fx wx0 = (fx)ix * cellw;
            fx wx1 = (fx)(ix + step) * cellw;
            fx wz0 = -((fx)iy * celll);
            fx wz1 = -((fx)(iy + step) * celll);
            fx h00 = c->heights[iy * nx + ix];
            fx h10 = c->heights[iy * nx + ix + step];
            fx h01 = c->heights[(iy + step) * nx + ix];
            fx h11 = c->heights[(iy + step) * nx + ix + step];

            v3 p00 = v3_make(wx0, h00, wz0);
            v3 p10 = v3_make(wx1, h10, wz0);
            v3 p01 = v3_make(wx0, h01, wz1);
            v3 p11 = v3_make(wx1, h11, wz1);

            SVert s00, s10, s01, s11;
            int32_t vx, vy, vz;
            world_to_view(p00, &vx, &vy, &vz); s00.vx=vx; s00.vy=vy; s00.vz=vz;
            world_to_view(p10, &vx, &vy, &vz); s10.vx=vx; s10.vy=vy; s10.vz=vz;
            world_to_view(p01, &vx, &vy, &vz); s01.vx=vx; s01.vy=vy; s01.vz=vz;
            world_to_view(p11, &vx, &vy, &vz); s11.vx=vx; s11.vy=vy; s11.vz=vz;

            if (s00.vz < TR_NEAR_Q8 && s10.vz < TR_NEAR_Q8 &&
                s01.vz < TR_NEAR_Q8 && s11.vz < TR_NEAR_Q8) continue;
            if (s00.vz > TR_FAR_Q8 && s10.vz > TR_FAR_Q8 &&
                s01.vz > TR_FAR_Q8 && s11.vz > TR_FAR_Q8) continue;

            {
                int32_t cvx = (s00.vx + s10.vx + s01.vx + s11.vx) >> 2;
                int32_t cvy = (s00.vy + s10.vy + s01.vy + s11.vy) >> 2;
                int32_t cvz = (s00.vz + s10.vz + s01.vz + s11.vz) >> 2;
                if (cvz > TR_NEAR_Q8) {
                    int32_t sc = (int32_t)(((int64_t)FOCAL << 8) / cvz);
                    int sx = TR_W / 2 + (int)(((int64_t)cvx * sc) >> 8);
                    int sy = TR_H / 2 - (int)(((int64_t)cvy * sc) >> 8);
                    int cs = (step * cellw) >> 8;
                    int half = (int)(((int64_t)cs * sc) >> 9) + 10;
                    if (sx + half < 0 || sx - half >= TR_W ||
                        sy + half < 0 || sy - half >= TR_H) continue;
                }
            }

            /* flat normal from the cell (fast approximation) */
            fx dhdx = fx_div((h10 + h11) - (h00 + h01), (2 * step) * cellw);
            fx dhdz = fx_div((h01 + h11) - (h00 + h10), (2 * step) * celll);
            int dot = (int)(((int64_t)(-dhdx) * LX + (int64_t)FX_ONE * LY + (int64_t)(-dhdz) * LZ) >> 16);
            int shade = 150 + dot / 700;
            if (shade < 70) shade = 70;
            if (shade > 245) shade = 245;

            fx cx = (wx0 + wx1) / 2, cz = (wz0 + wz1) / 2;
            fx ddx = cx - (fx)((eye_q8[0] << 8));
            fx ddz = cz - (fx)((eye_q8[2] << 8));
            int adx = ddx < 0 ? -ddx : ddx, adz = ddz < 0 ? -ddz : ddz;
            int ad = adx > adz ? adx : adz;
            int fog = (int)(((int64_t)(ad - 28 * FX_ONE) * 255) / (110 * FX_ONE));
            if (fog < 0) fog = 0;
            if (fog > 235) fog = 235;

            fx u0 = (fx)((wx0 >> 11) << 8);
            fx u1 = (fx)((wx1 >> 11) << 8);
            fx v0 = (fx)((wz0 >> 11) << 8);
            fx v1 = (fx)((wz1 >> 11) << 8);
            s00.u = u0; s00.v = v0; s00.shade = shade; s00.fog = fog;
            s10.u = u1; s10.v = v0; s10.shade = shade; s10.fog = fog;
            s01.u = u0; s01.v = v1; s01.shade = shade; s01.fog = fog;
            s11.u = u1; s11.v = v1; s11.shade = shade; s11.fog = fog;

#ifdef TR_DEBUG_GFX
            if (iy == r0 + 20 && ix == (nx - 1) / 2 && !dbg_once) {
                RV q; view_project(s00.vx, s00.vy, s00.vz, &q);
                tr_log("dbg midcell iy=%d view=(%d,%d,%d) screen=(%d,%d)", iy, s00.vx, s00.vy, s00.vz, q.x, q.y);
            }
#endif
            int stex = surface_tex_id(c, ix, iy, step);
            tr_tex *tex = stex == 0 ? tr_tex_ice : (stex == 1 ? tr_tex_rock : tr_tex_snow);
#ifdef TR_DEBUG_GFX
            gfx_dbg_cells++;
#endif
            draw_terrain_tri(s00, s10, s11, tex);
            draw_terrain_tri(s00, s11, s01, tex);
        }
        iy += step;
    }
#ifdef TR_DEBUG_GFX
    if (!dbg_once) {
        dbg_once = 1;
        tr_log("dbg terrain eye=(%d,%d,%d) player=(%d,%d) row=%d..%d cells=%u tris=%u pix=%u",
               eye_q8[0], eye_q8[1], eye_q8[2], dbgpx>>16, dbgpz>>16,
               r0, r1, gfx_dbg_cells, gfx_dbg_tris, gfx_dbg_pix);
    }
#endif
}

/* ------------------------------------------------------------------ */
/* billboards / objects                                                */
/* ------------------------------------------------------------------ */

static void draw_billboard(v3 center, fx width, fx height, tr_tex *tex,
                           int x_axis, int shade, int fog, int blend)
{
#ifdef TR_DEBUG_GFX
    static int dbg_bb;
    if (dbg_bb < 3 && tex) {
        g_dbg_billboard = 1;
        tr_log("dbg billboard %d tex=%dx%d a=%p w=%d h=%d alpha_center=%d",
               dbg_bb, tex->w, tex->h, (void*)tex->a, width>>16, height>>16,
               tex->a ? tex->a[(tex->h/2)*tex->w + tex->w/2] : -1);
        dbg_bb++;
    }
#endif
    v3 right;
    if (x_axis) {
        right = v3_make(FX_ONE, 0, 0);
    } else {
        fx rx = (fx)cam_r[0] << 6, rz = (fx)cam_r[2] << 6;
        fx l = v3_len(v3_make(rx, 0, rz));
        if (l <= 0) { rx = FX_ONE; rz = 0; }
        else { rx = fx_div(rx, l); rz = fx_div(rz, l); }
        right = v3_make(rx, 0, rz);
    }
    v3 up = v3_make(0, FX_ONE, 0);
    v3 hw = v3_scale(right, width / 2);
    v3 hh = v3_scale(up, height / 2);

    v3 p0 = v3_add(v3_add(center, hw), hh);
    v3 p1 = v3_add(v3_sub(center, hw), hh);
    v3 p2 = v3_sub(v3_sub(center, hw), hh);
    v3 p3 = v3_sub(v3_add(center, hw), hh);
    SVert s0, s1, s2, s3;
    int32_t vx, vy, vz;
    world_to_view(p0, &vx, &vy, &vz); s0.vx=vx; s0.vy=vy; s0.vz=vz;
    world_to_view(p1, &vx, &vy, &vz); s1.vx=vx; s1.vy=vy; s1.vz=vz;
    world_to_view(p2, &vx, &vy, &vz); s2.vx=vx; s2.vy=vy; s2.vz=vz;
    world_to_view(p3, &vx, &vy, &vz); s3.vx=vx; s3.vy=vy; s3.vz=vz;
    if (s0.vz < TR_NEAR_Q8 && s1.vz < TR_NEAR_Q8 &&
        s2.vz < TR_NEAR_Q8 && s3.vz < TR_NEAR_Q8) return;
    if (s0.vz > TR_FAR_Q8 && s1.vz > TR_FAR_Q8 &&
        s2.vz > TR_FAR_Q8 && s3.vz > TR_FAR_Q8) return;

    fx uw = (fx)tex->w << 8, uh = (fx)tex->h << 8;
    s0.u = uw; s0.v = 0; s1.u = 0; s1.v = 0;
    s2.u = 0; s2.v = uh; s3.u = uw; s3.v = uh;
    s0.shade=s1.shade=s2.shade=s3.shade = shade;
    s0.fog=s1.fog=s2.fog=s3.fog = fog;
#ifdef TR_DEBUG_GFX
    if (dbg_bb == 1) {
        tr_log("dbg bb uv %d,%d %d,%d %d,%d %d,%d scr0=(%d,%d) scr1=(%d,%d)",
               s0.u,s0.v,s1.u,s1.v,s2.u,s2.v,s3.u,s3.v,
               (s0.vx*FOCAL)/s0.vz, (s0.vy*FOCAL)/s0.vz,
               (s1.vx*FOCAL)/s1.vz, (s1.vy*FOCAL)/s1.vz);
    }
#endif
    int flags = DF_ALPHA_TEST;
    if (blend) flags |= DF_BLEND;
    emit_tri(s0, s1, s2, tex, 0, 0xFFFF, 255, flags);
    emit_tri(s0, s2, s3, tex, 0, 0xFFFF, 255, flags);
}

void gfx_draw_world_objects(tr_course *c, fx player_x, fx player_z, fx time_s, int show_gates, const uint8_t *item_taken)
{
    (void)player_x;
    fx far2 = 95 * FX_ONE;
    for (int i = 0; i < c->num_trees; i++) {
        tr_obj *o = &c->trees[i];
        fx dz = o->z - player_z;
        if (dz > 12 * FX_ONE || dz < -far2) continue;
        fx dx = o->x - 0 * FX_ONE;
        (void)dx;
        fx h = tr_course_height(o->x, o->z);
        fx ddx = o->x - (fx)(eye_q8[0] << 8);
        fx ddz = o->z - (fx)(eye_q8[2] << 8);
        int adx = ddx < 0 ? -ddx : ddx, adz = ddz < 0 ? -ddz : ddz;
        int ad = adx > adz ? adx : adz;
        int fog = (int)(((int64_t)(ad - 28 * FX_ONE) * 255) / (110 * FX_ONE));
        if (fog < 0) fog = 0;
        if (fog > 230) fog = 230;
        int shade = 225;
        v3 ctr = v3_make(o->x, h + o->h / 2, o->z);
        draw_billboard(ctr, o->d, o->h, tr_tex_tree[o->type % 3], 0, shade, fog, 0);
    }

    for (int i = 0; i < c->num_items; i++) {
        tr_obj *o = &c->items[i];
        if (o->type == 4) continue;
        if (o->type >= 2 && !show_gates) continue;
        if (o->type == 0 && item_taken && item_taken[i & 511]) continue;
        fx dz = o->z - player_z;
        if (dz > 15 * FX_ONE || dz < -far2) continue;
        fx h = tr_course_height(o->x, o->z);
        fx ddx = o->x - (fx)(eye_q8[0] << 8);
        fx ddz = o->z - (fx)(eye_q8[2] << 8);
        int adx = ddx < 0 ? -ddx : ddx, adz = ddz < 0 ? -ddz : ddz;
        int ad = adx > adz ? adx : adz;
        int fog = (int)(((int64_t)(ad - 28 * FX_ONE) * 255) / (110 * FX_ONE));
        if (fog < 0) fog = 0;
        if (fog > 230) fog = 230;
        tr_tex *tex = tr_tex_item[o->type];
        if (!tex) continue;
        fx width = o->d, height = o->h;
        fx y = h + o->h / 2 + o->above;
        if (o->type == 0) {
            y += fx_mul(tr_sin((ang)((time_s >> 8) * 160)), FX_ONE / 5);
            width = 1.3 * FX_ONE;
            height = 1.3 * FX_ONE;
        } else if (o->type == 1) {
            width = 1.2 * FX_ONE;
            height = 2.6 * FX_ONE;
            y = h + height / 2;
        } else {
            width = 9 * FX_ONE;
            height = 5.5 * FX_ONE;
            y = h + height / 2;
        }
#ifdef TR_DEBUG_GFX
        {
            int sx, sy, vis;
            gfx_project(v3_make(o->x, y, o->z), &sx, &sy, &vis);
            if (vis && sx > -100 && sx < TR_W+100 && dz > -20*FX_ONE) {
                tr_log("dbg item type=%d z=%d x=%d wy=%d w=%d h=%d screen=(%d,%d) fog=%d",
                       o->type, o->z>>16, o->x>>16, y>>16, width>>16, height>>16, sx, sy, fog);
            }
        }
#endif
        draw_billboard(v3_make(o->x, y, o->z), width, height, tex,
                       (o->type >= 2), 245, fog, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Tux (low-poly sphere assembly)                                      */
/* ------------------------------------------------------------------ */



/* ------------------------------------------------------------------ */
/* Original Tux model, baked from tux.tcl into rigid 3D poses          */
/* ------------------------------------------------------------------ */

#define TUX_LIFT (0.28 * FX_ONE)
#define TUX_MAX_VERTS 1100
#define TUX_MAX_FACES 1100
#define TUX_MAX_POSES 4

typedef struct {
    int32_t m[12];
} Mat34;

typedef struct {
    uint32_t nv, nf;
    const uint8_t *vptr;
    const uint8_t *fptr;
} TuxPose;

static uint8_t *tux_mesh_data;
static TuxPose tux_poses[TUX_MAX_POSES];
static int tux_pose_count = -1;

static void mat_identity34(Mat34 *m)
{
    static const int32_t id[12] = { 65536,0,0,0, 0,65536,0,0, 0,0,65536,0 };
    memcpy(m->m, id, sizeof(id));
}

static Mat34 mat_mul34(const Mat34 *a, const Mat34 *b)
{
    Mat34 o;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            int64_t s = 0;
            for (int k = 0; k < 3; k++)
                s += (int64_t)a->m[r*4+k] * b->m[k*4+c];
            o.m[r*4+c] = (int32_t)(s >> 16);
        }
        int64_t s = ((int64_t)a->m[r*4+3]) << 16;
        for (int k = 0; k < 3; k++)
            s += (int64_t)a->m[r*4+k] * b->m[k*4+3];
        o.m[r*4+3] = (int32_t)(s >> 16);
    }
    return o;
}

static Mat34 mat_rot_x(ang a)
{
    Mat34 m; mat_identity34(&m);
    fx c = tr_cos(a), s = tr_sin(a);
    m.m[5] = c; m.m[6] = -s; m.m[9] = s; m.m[10] = c;
    return m;
}
static Mat34 mat_rot_y(ang a)
{
    Mat34 m; mat_identity34(&m);
    fx c = tr_cos(a), s = tr_sin(a);
    m.m[0] = c; m.m[2] = s; m.m[8] = -s; m.m[10] = c;
    return m;
}
static Mat34 mat_rot_z(ang a)
{
    Mat34 m; mat_identity34(&m);
    fx c = tr_cos(a), s = tr_sin(a);
    m.m[0] = c; m.m[1] = -s; m.m[4] = s; m.m[5] = c;
    return m;
}
static Mat34 mat_trans(fx x, fx y, fx z)
{
    Mat34 m; mat_identity34(&m);
    m.m[3] = x; m.m[7] = y; m.m[11] = z;
    return m;
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void tux_mesh_load(void)
{
    if (tux_pose_count >= 0) return;
    tux_pose_count = 0;
    char path[700];
    snprintf(path, sizeof(path), "%s/model/tux_mesh.bin", tr_asset_root());
    FILE *f = fopen(path, "rb");
    if (!f) { tr_log("Tux Racer: missing 3D mesh %s", path); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 16 || sz > 400000) { fclose(f); return; }
    tux_mesh_data = (uint8_t *)malloc((size_t)sz);
    if (!tux_mesh_data) { fclose(f); return; }
    if (fread(tux_mesh_data, 1, (size_t)sz, f) != (size_t)sz) {
        free(tux_mesh_data); tux_mesh_data = NULL; fclose(f); return;
    }
    fclose(f);
    if (memcmp(tux_mesh_data, "TMH1", 4) != 0) {
        free(tux_mesh_data); tux_mesh_data = NULL; return;
    }
    uint32_t count = rd_u32(tux_mesh_data + 4);
    if (count > TUX_MAX_POSES) count = TUX_MAX_POSES;
    const uint8_t *p = tux_mesh_data + 8;
    const uint8_t *end = tux_mesh_data + sz;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 8 > end) break;
        uint32_t nv = rd_u32(p), nf = rd_u32(p + 4); p += 8;
        if (nv > TUX_MAX_VERTS || nf > TUX_MAX_FACES) break;
        if (p + (size_t)nv * 12 + (size_t)nf * 14 > end) break;
        tux_poses[i].nv = nv;
        tux_poses[i].nf = nf;
        tux_poses[i].vptr = p;
        p += (size_t)nv * 12;
        tux_poses[i].fptr = p;
        p += (size_t)nf * 14;
        tux_pose_count++;
    }
    tr_log("Tux Racer: baked 3D mesh poses=%d", tux_pose_count);
}

void gfx_draw_tux_model(v3 pos, ang yaw, ang pitch, ang roll, fx speed,
                        int turn, int braking, int paddling, int airborne)
{
    (void)speed; (void)braking; (void)paddling;
    tux_mesh_load();
    if (tux_pose_count <= 0) return;
    int pi = 0;
    if (airborne) pi = 3;
    else if (turn > 0) pi = 2;
    else if (turn < 0) pi = 1;
    if (pi >= tux_pose_count) pi = 0;
    const TuxPose *pose = &tux_poses[pi];

    Mat34 root = mat_trans(pos.x, pos.y + TUX_LIFT, pos.z);
    Mat34 ry = mat_rot_y((ang)(yaw + ANG_180));
    Mat34 rx = mat_rot_x(pitch);
    Mat34 rz = mat_rot_z(roll);
    Mat34 r_stomach = mat_rot_x(ANG_90); /* Tux slides on stomach: head forward -Z, feet back +Z, belly on snow -Y */
    root = mat_mul34(&root, &ry);
    root = mat_mul34(&root, &rx);
    root = mat_mul34(&root, &rz);
    root = mat_mul34(&root, &r_stomach);

    SVert sv[TUX_MAX_VERTS];
    v3 wp[TUX_MAX_VERTS];
    const uint8_t *vp = pose->vptr;
    for (uint32_t i = 0; i < pose->nv; i++) {
        int32_t vx = (int32_t)rd_u32(vp);
        int32_t vy = (int32_t)rd_u32(vp + 4);
        int32_t vz = (int32_t)rd_u32(vp + 8);
        vp += 12;
        fx wx = (fx)((((int64_t)root.m[0]*vx + (int64_t)root.m[1]*vy + (int64_t)root.m[2]*vz) >> 16) + root.m[3]);
        fx wy = (fx)((((int64_t)root.m[4]*vx + (int64_t)root.m[5]*vy + (int64_t)root.m[6]*vz) >> 16) + root.m[7]);
        fx wz = (fx)((((int64_t)root.m[8]*vx + (int64_t)root.m[9]*vy + (int64_t)root.m[10]*vz) >> 16) + root.m[11]);
        wp[i] = v3_make(wx, wy, wz);
        int32_t xv, yv, zv;
        world_to_view(wp[i], &xv, &yv, &zv);
        sv[i].vx = xv; sv[i].vy = yv; sv[i].vz = zv;
        sv[i].u = sv[i].v = 0;
        sv[i].shade = 160;
        sv[i].fog = 0;
    }

    const uint8_t *fp = pose->fptr;
    for (uint32_t i = 0; i < pose->nf; i++) {
        uint16_t ia = (uint16_t)(fp[0] | (fp[1] << 8));
        uint16_t ib = (uint16_t)(fp[2] | (fp[3] << 8));
        uint16_t ic = (uint16_t)(fp[4] | (fp[5] << 8));
        uint16_t color = (uint16_t)(fp[6] | (fp[7] << 8));
        int16_t nx15 = (int16_t)(fp[8] | (fp[9] << 8));
        int16_t ny15 = (int16_t)(fp[10] | (fp[11] << 8));
        int16_t nz15 = (int16_t)(fp[12] | (fp[13] << 8));
        fp += 14;
        if (ia >= pose->nv || ib >= pose->nv || ic >= pose->nv) continue;
        fx nx = (fx)((int64_t)root.m[0]*nx15*2 + (int64_t)root.m[1]*ny15*2 + (int64_t)root.m[2]*nz15*2) >> 16;
        fx ny = (fx)((int64_t)root.m[4]*nx15*2 + (int64_t)root.m[5]*ny15*2 + (int64_t)root.m[6]*nz15*2) >> 16;
        fx nz = (fx)((int64_t)root.m[8]*nx15*2 + (int64_t)root.m[9]*ny15*2 + (int64_t)root.m[10]*nz15*2) >> 16;
        v3 nrm = v3_make(nx, ny, nz);
        v3 ctr = v3_make((wp[ia].x + wp[ib].x + wp[ic].x) / 3,
                         (wp[ia].y + wp[ib].y + wp[ic].y) / 3,
                         (wp[ia].z + wp[ib].z + wp[ic].z) / 3);
        if (v3_dot(nrm, v3_sub(eye_world16, ctr)) <= 0) continue;
        int dot = (int)(((int64_t)nx * -22938 + (int64_t)ny * 55705 + (int64_t)nz * -26214) >> 16);
        int sh = 135 + dot / 500;
        if (sh < 45) sh = 45;
        if (sh > 255) sh = 255;
        sv[ia].shade = sv[ib].shade = sv[ic].shade = sh;
        emit_tri(sv[ia], sv[ib], sv[ic], NULL, color, 0xFFFF, 255, DF_FLAT_DEPTH);
    }
}

void gfx_draw_tux_shadow(v3 pos)
{
    int sx, sy, vis;
    fx h = tr_course_height(pos.x, pos.z);
    gfx_project(v3_make(pos.x, h + FX_ONE / 100, pos.z), &sx, &sy, &vis);
    if (!vis) return;
    int rx = 12, ry = 4;
    for (int y = -ry; y <= ry; y++) {
        int w = rx - (abs(y) * rx) / (ry + 1);
        if (w <= 0) continue;
        int py = sy + y;
        if ((unsigned)py >= TR_H) continue;
        for (int x = -w; x <= w; x++) {
            int px = sx + x;
            if ((unsigned)px >= TR_W) continue;
            int idx = py * TR_W + px;
            fb[idx] = blend565(fb[idx], 0x0000, 90);
        }
    }
}

/* ------------------------------------------------------------------ */
/* UI                                                                  */
/* ------------------------------------------------------------------ */

void gfx_rect(int x, int y, int w, int h, uint16_t color, int alpha)
{
    if (w <= 0 || h <= 0) return;
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > TR_W) x1 = TR_W;
    if (y1 > TR_H) y1 = TR_H;
    for (int yy = y; yy < y1; yy++) {
        uint16_t *row = &fb[yy * TR_W];
        if (alpha >= 255) {
            for (int xx = x; xx < x1; xx++) row[xx] = color;
        } else {
            for (int xx = x; xx < x1; xx++) row[xx] = blend565(row[xx], color, alpha);
        }
    }
}

void gfx_hline(int x, int y, int w, uint16_t color, int alpha)
{
    gfx_rect(x, y, w, 1, color, alpha);
}

static inline int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void gfx_blit_sub(tr_tex *t, int sx, int sy, int sw, int sh,
                  int dx, int dy, int dw, int dh, uint16_t tint, int alpha, int bilinear)
{
    if (!t || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    int64_t stepx = ((int64_t)sw << 16) / dw;
    int64_t stepy = ((int64_t)sh << 16) / dh;
    int64_t fy = (int64_t)sy << 16;
    for (int y = 0; y < dh; y++) {
        int py = dy + y;
        int ty = (int)(fy >> 16);
        int ay = (int)((fy >> 8) & 255);
        int64_t fx = (int64_t)sx << 16;
        if ((unsigned)py < TR_H) {
            uint16_t *row = &fb[py * TR_W];
            for (int x = 0; x < dw; x++) {
                int px = dx + x;
                int tx = (int)(fx >> 16);
                int ax = (int)((fx >> 8) & 255);
                if ((unsigned)px < TR_W) {
                    uint16_t col;
                    int a = 255;
                    if (bilinear) {
                        int tx1 = clamp_int(tx + 1, sx, sx + sw - 1);
                        int ty1 = clamp_int(ty + 1, sy, sy + sh - 1);
                        int tx0 = clamp_int(tx, sx, sx + sw - 1);
                        int ty0 = clamp_int(ty, sy, sy + sh - 1);
                        uint16_t p00 = t->px[ty0 * t->w + tx0], p10 = t->px[ty0 * t->w + tx1];
                        uint16_t p01 = t->px[ty1 * t->w + tx0], p11 = t->px[ty1 * t->w + tx1];
                        int r = ((((p00>>11)&31)*(256-ax) + ((p10>>11)&31)*ax) * (256-ay)
                              + (((p01>>11)&31)*(256-ax) + ((p11>>11)&31)*ax) * ay) >> 16;
                        int g = ((((p00>>5)&63)*(256-ax) + ((p10>>5)&63)*ax) * (256-ay)
                              + (((p01>>5)&63)*(256-ax) + ((p11>>5)&63)*ax) * ay) >> 16;
                        int b = (((p00&31)*(256-ax) + (p10&31)*ax) * (256-ay)
                              + ((p01&31)*(256-ax) + (p11&31)*ax) * ay) >> 16;
                        col = (uint16_t)((r << 11) | (g << 5) | b);
                        if (t->a) {
                            int a00 = t->a[ty0 * t->w + tx0], a10 = t->a[ty0 * t->w + tx1];
                            int a01 = t->a[ty1 * t->w + tx0], a11 = t->a[ty1 * t->w + tx1];
                            a = ((a00*(256-ax) + a10*ax) * (256-ay) + (a01*(256-ax) + a11*ax) * ay) >> 16;
                        }
                    } else {
                        int tx0 = clamp_int(tx, sx, sx + sw - 1);
                        int ty0 = clamp_int(ty, sy, sy + sh - 1);
                        col = t->px[ty0 * t->w + tx0];
                        if (t->a) a = t->a[ty0 * t->w + tx0];
                    }
                    col = tint_px(col, tint);
                    a = (a * alpha) >> 8;
                    if (a > 0) row[px] = blend565(row[px], col, a);
                }
                fx += stepx;
            }
        }
        fy += stepy;
    }
}

void gfx_blit(tr_tex *t, int x, int y, int w, int h, uint16_t tint, int alpha, int bilinear)
{
    if (t) gfx_blit_sub(t, 0, 0, t->w, t->h, x, y, w, h, tint, alpha, bilinear);
}

static tr_glyph *glyph_for(tr_font *f, char c)
{
    tr_glyph *g = &f->g[(unsigned char)c];
    if (g->w || g->h || g->kern) return g;
    if (c >= 'a' && c <= 'z') return &f->g[c - 32];
    if (c >= 'A' && c <= 'Z') return &f->g[c + 32];
    return &f->g[' '];
}

int gfx_text_width(tr_font *f, const char *s, int size)
{
    if (!f || f->max_ascent <= 0) return 0;
    int w = 0;
    while (*s) {
        tr_glyph *g = glyph_for(f, *s++);
        w += (g->kern * size) / f->max_ascent;
    }
    return w;
}

void gfx_text(tr_font *f, tr_tex *t, const char *s, int x, int y, int size,
              uint16_t tint, int align)
{
    if (!f || !t || f->max_ascent <= 0) return;
    int w = gfx_text_width(f, s, size);
    if (align == 1) x -= w / 2;
    else if (align == 2) x -= w;
    int baseline = y + size;
    int draw_shadow = (t == tr_tex_font_white);
    for (int pass = 0; pass < (draw_shadow ? 2 : 1); pass++) {
        int ox = x + (pass == 0 && draw_shadow ? 1 : 0);
        int oy = y + (pass == 0 && draw_shadow ? 1 : 0);
        uint16_t tc = (pass == 0 && draw_shadow) ? 0x0000 : tint;
        int alpha = (pass == 0 && draw_shadow) ? 150 : 255;
        const char *p = s;
        int cx = ox;
        while (*p) {
            tr_glyph *g = glyph_for(f, *p++);
            if (g->w && g->h) {
                int dw = (g->w * size) / f->max_ascent;
                int dh = (g->h * size) / f->max_ascent;
                int gx = cx + (g->xo * size) / f->max_ascent;
                int gy = baseline - ((g->yo + g->h) * size) / f->max_ascent + oy - y;
                int ss = f->src_scale;
                int sw = g->w / ss, sh = g->h / ss;
                int sx = g->xp / ss, sy = (f->texh - g->yp - g->h) / ss;
                if (sw < 1) sw = 1;
                if (sh < 1) sh = 1;
                gfx_blit_sub(t, sx, sy, sw, sh, gx, gy, dw, dh, tc, alpha, 1);
            }
            cx += (g->kern * size) / f->max_ascent;
        }
    }
}

void gfx_snowflake(int x, int y, int size, int alpha)
{
    gfx_rect(x, y, size, size, 0xFFFF, alpha);
}
