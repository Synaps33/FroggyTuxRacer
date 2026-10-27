#ifndef TUXRACER_H
#define TUXRACER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tr_sincos.h"

#define TR_W 320
#define TR_H 240

#define TR_VERSION "0.1"
#define TR_TITLE "Tux Racer"

/* Q16.16 fixed point */
typedef int32_t fx;
#define FX_ONE 65536
#define FX_MAX 0x7fffffff
#define FX(a) ((fx)((a) * 65536.0))

/* Q8 render coordinates (1/256 m) */
typedef int32_t q8;
#define Q8_ONE 256

/* 16.16 angle (full circle = 65536) */
typedef uint16_t ang;
#define ANG_90 16384
#define ANG_180 32768
#define ANG_270 49152

typedef struct { fx x, y, z; } v3;
typedef struct { fx x, z; } v2;

static inline fx fx_mul(fx a, fx b) { return (fx)(((int64_t)a * b) >> 16); }
static inline fx fx_div(fx a, fx b) { return b ? (fx)(((int64_t)a << 16) / b) : 0; }
static inline fx fx_abs(fx a) { return a < 0 ? -a : a; }
static inline fx fx_clamp(fx v, fx lo, fx hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline fx fx_min(fx a, fx b) { return a < b ? a : b; }
static inline fx fx_max(fx a, fx b) { return a > b ? a : b; }
static inline int fx_int(fx a) { return (int)(a >> 16); }
static inline fx int_fx(int a) { return (fx)(a << 16); }
static inline q8 fx_q8(fx a) { return a >> 8; }
static inline fx q8_fx(q8 a) { return a << 8; }

static inline uint32_t tr_isqrt(uint32_t x)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* vector helpers in Q16 */
static inline v3 v3_make(fx x, fx y, fx z) { v3 v = { x, y, z }; return v; }
static inline v3 v3_add(v3 a, v3 b) { return v3_make(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline v3 v3_sub(v3 a, v3 b) { return v3_make(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline v3 v3_scale(v3 a, fx s) { return v3_make(fx_mul(a.x, s), fx_mul(a.y, s), fx_mul(a.z, s)); }
static inline fx v3_dot(v3 a, v3 b) { return fx_mul(a.x, b.x) + fx_mul(a.y, b.y) + fx_mul(a.z, b.z); }
static inline v3 v3_cross(v3 a, v3 b)
{
    return v3_make(fx_mul(a.y, b.z) - fx_mul(a.z, b.y),
                   fx_mul(a.z, b.x) - fx_mul(a.x, b.z),
                   fx_mul(a.x, b.y) - fx_mul(a.y, b.x));
}
static inline fx v3_len(v3 a)
{
    int64_t d = (int64_t)a.x * a.x + (int64_t)a.y * a.y + (int64_t)a.z * a.z;
    uint32_t isq = tr_isqrt((uint32_t)(d >> 16));
    return (fx)(isq << 8);
}
static inline v3 v3_norm(v3 a)
{
    fx l = v3_len(a);
    if (l <= 0) return v3_make(0, FX_ONE, 0);
    return v3_make(fx_div(a.x, l), fx_div(a.y, l), fx_div(a.z, l));
}

static inline fx tr_sin(ang a) { return (fx)tr_sin_table[(a >> 4) & (TR_SIN_N - 1)] << 1; }
static inline fx tr_cos(ang a) { return tr_sin((ang)(a + ANG_90)); }

/* look-up helpers defined in the implementation files */
void tr_log(const char *fmt, ...);
uint32_t tr_ticks_ms(void);

#endif
