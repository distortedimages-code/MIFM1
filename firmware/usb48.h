/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* (0.16.35) The USB audio input at 48 kHz (app.c app_uac_frames48; fm1-base's usb.c takes the frames,
 * patches/fm1-base-usb-multi.patch FELUCCA_UAC_SOURCE48): the render's 44.1 kHz frames to 48, x 160 / 147, a
 * polyphase FIR in float. Its prototype: 160 x 24 taps, a sinc cut at 22.5 kHz under a Kaiser window (beta 7.5), each
 * phase's taps summing to 1; flat to 19 kHz (-0.07 dB), -0.5 dB at 20, -78 dB from 28 kHz on (where the images of
 * what is under 16 kHz would come back down into the band); tools/usb48_test.c measures it. Built at boot in RAM (80
 * phases: the other 80 are the same backwards), 7.7 KB. Each input frame enters the history, then the 1 or 2 output
 * frames whose newest input it is, phase p: sum over j of tap[p][j] x[newest - j]. ~2.5 % of a core at 48 kHz. */
#ifndef MIFM1_USB48_H
#define MIFM1_USB48_H
#include <stdint.h>
#include "fastmath.h"

#define R48_L 160u                       /* up */
#define R48_M 147u                       /* down */
#define R48_N 24u                        /* taps a phase */
#define R48_H 32u                        /* the history: a power of two, >= R48_N, written twice (no wrap in a window) */
#define R48_MAX(n) ((n) * R48_L / R48_M + 2u)   /* the most frames n give */
typedef struct {
    float c[R48_L / 2u][R48_N];          /* phases 0..79, tap j for x[newest - j] */
    float l[2u * R48_H], r[2u * R48_H];
    uint32_t w, ph;                      /* frames written; the next output's phase (0..159) */
} r48_t;

static float r48_i0(float x)             /* the modified Bessel function I0, its series */
{
    float s = 1.0f, t = 1.0f, q = 0.25f * x * x;
    for (int k = 1; k < 40; k++) {
        t *= q / (float)(k * k);
        s += t;
        if (t < s * 1e-9f)
            break;
    }
    return s;
}

static void r48_init(r48_t *s)
{
    const float fc = 2.0f * 22500.0f / 44100.0f, beta = 7.5f, i0b = r48_i0(beta);
    const float mid = 0.5f * (float)(R48_L * R48_N - 1u);
    for (uint32_t p = 0; p < R48_L / 2u; p++) {
        float sum = 0.0f;
        for (uint32_t j = 0; j < R48_N; j++) {
            float k = (float)(p + j * R48_L), tau = (k - mid) / (float)R48_L, x = fc * tau;
            float sinc = fm_fabsf(x) < 1e-6f ? 1.0f : fm_sinf(FM_PI * x) / (FM_PI * x);
            float u = (k - mid) / mid, win = r48_i0(beta * fm_sqrtf(fm_maxf(0.0f, 1.0f - u * u))) / i0b;
            s->c[p][j] = fc * sinc * win;
            sum += s->c[p][j];
        }
        for (uint32_t j = 0; j < R48_N; j++)
            s->c[p][j] /= sum;
    }
    for (uint32_t i = 0; i < 2u * R48_H; i++)
        s->l[i] = s->r[i] = 0.0f;
    s->w = s->ph = 0;
}

/* n frames of l, r (44.1 kHz) into out (16-bit, interleaved, times gain, rounded): the frames written */
static uint32_t r48_run(r48_t *s, const float *l, const float *r, uint32_t n, int16_t *out, float gain)
{
    uint32_t w = s->w, ph = s->ph, k = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t h = w & (R48_H - 1u);
        s->l[h] = s->l[h + R48_H] = l[i];
        s->r[h] = s->r[h + R48_H] = r[i];
        w++;
        const uint32_t o = (w - R48_N) & (R48_H - 1u);
        const float *xl = &s->l[o], *xr = &s->r[o];   /* oldest .. newest */
        do {
            float al = 0.0f, ar = 0.0f;
            if (ph < R48_L / 2u) {           /* tap j on x[newest - j]: the window backwards */
                const float *c = &s->c[ph][R48_N - 1u];
                for (uint32_t j = 0; j < R48_N; j++, c--)
                    al += *c * xl[j], ar += *c * xr[j];
            } else {                         /* phase 159 - p backwards: forwards */
                const float *c = s->c[R48_L - 1u - ph];
                for (uint32_t j = 0; j < R48_N; j++, c++)
                    al += *c * xl[j], ar += *c * xr[j];
            }
            al = fm_clampf(al * gain, -32767.0f, 32767.0f);
            ar = fm_clampf(ar * gain, -32767.0f, 32767.0f);
            out[2u * k] = (int16_t)(al + (al < 0.0f ? -0.5f : 0.5f));
            out[2u * k + 1u] = (int16_t)(ar + (ar < 0.0f ? -0.5f : 0.5f));
            k++;
            ph += R48_M;
        } while (ph < R48_L);
        ph -= R48_L;
    }
    s->w = w;
    s->ph = ph;
    return k;
}
#endif
