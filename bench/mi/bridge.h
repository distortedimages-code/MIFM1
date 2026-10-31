/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1 bench: a C face for each Mutable Instruments module (one C++ unit per module: their resources.h headers
 * define the same macro names, so no two of them may meet in one unit). Every object lives in memory the caller
 * gives it (the bench's arena): nothing is allocated, nothing is freed, no static constructor runs.
 *
 * Audio is float, -1..1, mono buffers of n frames (any n: each bridge cuts it into its module's own blocks). */
#ifndef MIFM1_BRIDGE_H_
#define MIFM1_BRIDGE_H_
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MI_SR 44100.0f                   /* the FM-1's rate */

/* ---- Plaits (Plaits 1.2: 24 models; the kSampleRate of the copy is 44.1 kHz) ---- */
#define MP_MODELS 24
uint32_t mp_size(void);                  /* bytes for one voice, its 16 KB engine buffer included */
void mp_init(void *mem);
void mp_set(void *v, int model, float note, float harmonics, float timbre, float morph, float decay, float colour);
void mp_gate(void *v, int high);         /* the TRIG input, held: a rise starts the LPG and decay, its length holds
                                         * the engines that sustain */
void mp_render(void *v, float *out, float *aux, uint32_t n);
const char *mp_model_name(int model);

/* ---- Rings ---- */
#define MR_MODELS 6
uint32_t mr_size(void);                  /* the part (four voices' strings and resonators) */
uint32_t mr_reverb_size(void);           /* its reverb's buffer: only the "string and reverb" model needs it */
void mr_init(void *mem, void *reverb);   /* reverb 0: no reverb (the model then plays dry) */
void mr_set(void *r, int model, int polyphony, float note, float structure, float brightness, float damping,
            float position);
void mr_strum(void *r);                  /* the internal exciter, at the next render */
void mr_render(void *r, float *out, float *aux, uint32_t n);
const char *mr_model_name(int model);

/* ---- Clouds ---- */
#define MC_MODES 4
uint32_t mc_size(void);                  /* the processor alone */
#define MC_LARGE_FULL 118784u            /* Clouds' own buffers (clouds.cc), and a smaller pair for a busy scene */
#define MC_SMALL_FULL (65536u - 128u)
void mc_init(void *mem, void *large, uint32_t large_size, void *small, uint32_t small_size);
void mc_set(void *c, int mode, int quality, float position, float size, float pitch, float density, float texture,
            float dry_wet, float spread, float feedback, float reverb, int freeze);
void mc_render(void *c, const float *in_l, const float *in_r, float *out_l, float *out_r, uint32_t n);
const char *mc_mode_name(int mode);

/* ---- Warps ---- */
#define MW_ALGOS 9                       /* the 8 positions of ALGORITHM's sweep, then the easter egg */
uint32_t mw_size(void);
void mw_init(void *mem);
void mw_set(void *w, int algo, float timbre, float level1, float level2);
void mw_render(void *w, const float *carrier, const float *modulator, float *out, float *aux, uint32_t n);
const char *mw_algo_name(int algo);

/* ---- Tides 2 ---- */
uint32_t mt_size(void);
void mt_init(void *mem);
/* ramp 0 AD, 1 looping, 2 AR; output 0 gates, 1 amplitudes, 2 phases, 3 frequencies; range 0 control, 1 audio */
void mt_set(void *t, int ramp, int output, int range, float hz, float shape, float slope, float smoothness,
            float shift);
void mt_gate(void *t, int high);         /* the trigger / gate input */
/* out: 4 channels interleaved (n frames x 4); n frames at rate sr (44100 at audio rate, the block rate as a
 * modulator: one frame per audio block) */
void mt_render(void *t, float *out4, uint32_t n, float sr);

/* ---- Marbles ---- */
uint32_t mm_size(void);
void mm_init(void *mem, uint32_t seed);
void mm_set(void *m, int t_model, float rate, float t_bias, float jitter, float deja_vu, int length, float spread,
            float x_bias, float steps);
/* n frames: gates (3 per frame: t1 t2 t3) and voltages (4 per frame: x1 x2 x3 y), interleaved */
void mm_render(void *m, uint8_t *gates3, float *cv4, uint32_t n);

/* ---- the C++ runtime the vendored code needs (none of it allocates) ---- */
#ifdef __cplusplus
}
#endif
#endif
