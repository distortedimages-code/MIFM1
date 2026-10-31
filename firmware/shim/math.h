/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* The FM-1 build has no libm and a single-precision FPU (fm1-base: no double, no libm): these are the maths the
 * vendored Mutable Instruments code asks for, in float, on fm1-base's fastmath.h. Device only (fm1.toml
 * device_include); the host builds use the real libm. */
#ifndef MIFM1_SHIM_MATH_H_
#define MIFM1_SHIM_MATH_H_
#include "fastmath.h"

#undef M_PI
#define M_PI 3.14159265358979323846f
#define M_PI_2 1.57079632679489661923f
#define M_SQRT2 1.41421356237309504880f
#define HUGE_VALF __builtin_huge_valf()
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")

static inline float fabsf(float x) { return fm_fabsf(x); }
static inline float floorf(float x) { return fm_floorf(x); }
static inline float ceilf(float x) { return -fm_floorf(-x); }
static inline float truncf(float x) { return (float)(int32_t)x; }
static inline float roundf(float x) { return x < 0.0f ? -fm_floorf(-x + 0.5f) : fm_floorf(x + 0.5f); }
static inline float sqrtf(float x) { return x > 0.0f ? fm_sqrtf(x) : 0.0f; }
static inline float sinf(float x) { return fm_sinf(x); }
static inline float cosf(float x) { return fm_cosf(x); }
static inline float tanf(float x) { return fm_tanf(x); }
static inline float tanhf(float x) { return fm_tanhf(x); }
static inline float expf(float x) { return fm_expf(x); }
static inline float exp2f(float x) { return fm_exp2f(x); }
static inline float logf(float x) { return fm_logf(x); }
static inline float log2f(float x) { return fm_log2f(x); }
static inline float log10f(float x) { return fm_log2f(x) * 0.30102999566f; }
static inline float powf(float x, float y)
{
    if (x > 0.0f)
        return fm_powf(x, y);
    if (x == 0.0f)
        return y == 0.0f ? 1.0f : 0.0f;
    {   /* a negative base: only whole exponents are defined */
        int32_t n = (int32_t)y;
        float r = fm_powf(-x, y);
        return (n & 1) ? -r : r;
    }
}
static inline float fmodf(float x, float y) { return x - y * truncf(x / y); }
static inline float fminf(float a, float b) { return a < b ? a : b; }
static inline float fmaxf(float a, float b) { return a > b ? a : b; }
static inline float atanf(float x)
{   /* |error| < 1e-5 rad: x / (1 + 0.28 x^2) refined with two Newton steps on tan */
    float s = x < 0.0f ? -1.0f : 1.0f, a = x * s, y, inv = 0;
    if (a > 1.0f) {
        inv = 1;
        a = 1.0f / a;
    }
    y = a / (1.0f + 0.28086f * a * a);
    y -= (fm_tanf(y) - a) / (1.0f + a * a);
    y -= (fm_tanf(y) - a) / (1.0f + a * a);
    if (inv)
        y = 1.57079632679f - y;
    return s * y;
}
static inline float atan2f(float y, float x)
{
    if (x > 0.0f)
        return atanf(y / x);
    if (x < 0.0f)
        return atanf(y / x) + (y >= 0.0f ? 3.14159265359f : -3.14159265359f);
    return y > 0.0f ? 1.57079632679f : y < 0.0f ? -1.57079632679f : 0.0f;
}
static inline int isnanf_(float x) { return x != x; }

#ifdef __cplusplus
/* C++'s <cmath> overloads, in float only: a double argument is narrowed to float at the call (an expression that
 * computes in double before the call is still a double: the build's soft-double check finds those) */
static inline float fabs(float x) { return fabsf(x); }
static inline float floor(float x) { return floorf(x); }
static inline float ceil(float x) { return ceilf(x); }
static inline float sqrt(float x) { return sqrtf(x); }
static inline float sin(float x) { return sinf(x); }
static inline float cos(float x) { return cosf(x); }
static inline float tan(float x) { return tanf(x); }
static inline float tanh(float x) { return tanhf(x); }
static inline float exp(float x) { return expf(x); }
static inline float log(float x) { return logf(x); }
static inline float log2(float x) { return log2f(x); }
static inline float log10(float x) { return log10f(x); }
static inline float pow(float x, float y) { return powf(x, y); }
static inline float fmod(float x, float y) { return fmodf(x, y); }
static inline float atan(float x) { return atanf(x); }
static inline float atan2(float y, float x) { return atan2f(y, x); }
static inline float round(float x) { return roundf(x); }
#define isnan(x) ((x) != (x))
#define isinf(x) (fabsf(x) == __builtin_inff())
#define isfinite(x) (!isnan(x) && !isinf(x))
#endif
#endif
