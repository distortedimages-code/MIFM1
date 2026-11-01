/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1: a C face for each Mutable Instruments module (one C++ unit per module: their resources.h headers define
 * the same macro names, so no two of them may meet in one unit). Every object lives in memory the caller gives it
 * (the engine's arena): nothing is allocated, nothing is freed, no static constructor runs.
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
void mp_amounts(void *v, float fm, float timbre, float morph);   /* the attenuverters: with TRIG patched and nothing
                                         * else, they let the internal envelope move FM, TIMBRE and MORPH (-1..1) */
void mp_render(void *v, float *out, float *aux, uint32_t n);
const char *mp_model_name(int model);

/* ---- the LXR-02's drum voices (mi_lxr.cc: Erica Synths' LXR firmware, GPL-3.0), one of each a track ---- */
#define LX_MODELS 4                      /* the drum (kick, tom), the snare, the cymbal, the hi-hat */
#define LX_KNOBS 8                       /* the parameters a model takes from its track's knobs */
void lx_init(void);
/* k: the LX_KNOBS knobs, 0..1 (as a modulation reaches the LXR: its MIDI value is v x 127); note: MIDI, its fraction
 * on the fine tune (48: the voice's own pitch). A new model on the track starts that voice afresh */
void lx_set(int t, int model, float note, const float *k);
void lx_trigger(int t);                  /* at the next frame rendered */
void lx_render(int t, float *out, uint32_t n);
const char *lx_knob_name(int model, int k);
void lx_knob_text(int model, int k, float v, char *b, int n);   /* as the LXR-02 shows it: 0..127, a wave's name */
void lx_defaults(int model, float *k);   /* the knobs as the model's kit has them */
int lx_knob_steps(int model, int k);     /* its values a knob's turn goes through: 128 (MIDI's), a wave's 6 */

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

/* ---- Clouds' reverb, alone, as a send (fully wet) ---- */
uint32_t mrv_size(void);                 /* its 32 KB of delay cells included */
void mrv_init(void *mem);
void mrv_set(void *r, float time, float damping, float diffusion);   /* 0..1 each */
void mrv_process(void *r, const float *in, float *out_l, float *out_r, uint32_t n);

/* ---- the delay (mi_delay.h: Mutable's filter and saturation in a 16-bit line, Zen Delay's knobs) ---- */
uint32_t mdl_size(void);                 /* its 1.2 s line included */
void mdl_init(void *mem);
/* time in samples, feedback 0..1, filter -1 (low pass) .. 1 (high pass), resonance, drive, mod 0..1 */
void mdl_set(void *d, float time, float feedback, float filter, float resonance, float drive, int ping_pong,
             float mod);
void mdl_process(void *d, const float *in, float *out_l, float *out_r, uint32_t n);

/* ---- the master bus (mi_master.cc): a compressor or OTT's three bands, then stmlib's limiter on each side ---- */
uint32_t mms_size(void);
void mms_init(void *mem);
void mms_mode(void *m, int mode);        /* 0 off (the limiter alone), 1 the compressor, 2 OTT */
/* the compressor: threshold in dB (0 and under), ratio (1: none), release in seconds, makeup in dB */
void mms_comp(void *m, float threshold_db, float ratio, float release_s, float makeup_db);
/* OTT: depth (its mix with the dry, 0..1), time (its attacks and releases scaled: 1 OTT's own), drive and level dB */
void mms_ott(void *m, float depth, float time, float drive_db, float level_db);
void mms_process(void *m, float *l, float *r, uint32_t n);   /* in place */
float mms_gain(void *m, int band);       /* the gain now, dB: the compressor's (- its reduction), OTT's bands' */

/* ---- Warps, at the FM-1's 44.1 kHz (its tables made again for it: mi_warps.cc) ---- */
uint32_t mw_size(void);
void mw_init(void *mem);
/* algorithm 0..1 (the module's big knob), timbre, level 1 and 2 (0..1); osc 0: the carrier is the input, 1..3 the
 * internal oscillator's shapes, at note (MIDI); shift 1: the frequency shifter, the easter egg (algorithm the shift,
 * timbre the side, level 1 the feedback, level 2 dry/wet) */
void mw_set(void *w, float algorithm, float timbre, float level1, float level2, int osc, float note, int shift);
/* in calls of 64 frames, none late; dry: the input given as dry_in, as is (to mix with the output) */
void mw_render(void *w, const float *carrier, const float *modulator, const float *dry_in, float *out, float *dry,
               uint32_t n);
void mw_eco(void *w, int eco);           /* 1: the cross-modulations oversampled 3x instead of 6 (half the work) */
uint32_t mw_calls(void *w, uint32_t n);  /* the modulator's Process calls the next n frames make (its cost goes by them) */

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
typedef struct {                         /* its panel, as marbles.cc reads it (all 0..1 unless said) */
    int t_model;                         /* 0..6: complementary Bernoulli, clusters, drums, independent Bernoulli,
                                          * divider, three states, Markov */
    int t_range;                         /* 0 /4, 1 x1, 2 x4 */
    float rate;                          /* semitones about 2 Hz at x1 (marbles' RATE) */
    float t_bias, jitter, deja_vu;
    int length;                          /* 1..16 */
    int t_deja_vu, x_deja_vu;            /* DEJA VU acts on T, on X */
    float spread, x_bias, steps;
    int x_mode;                          /* 0 identical, 1 bump, 2 tilt */
    int x_range;                         /* 0 narrow (+2 V), 1 positive (+5 V), 2 full (+/-5 V) */
    int scale;                           /* 0..5: marbles' preset scales */
    float y_spread, y_bias, y_steps;
} mm_params_t;
uint32_t mm_size(void);
void mm_init(void *mem, uint32_t seed, float sr);   /* sr: the rate it is called at (its times follow it) */
void mm_set(void *m, const mm_params_t *p);
void mm_reset(void *m);                  /* the clock from its start, at the next render */
/* n frames: gates (3 per frame: t1 t2 t3) and voltages (4 per frame: x1 x2 x3 y, in volts), interleaved */
void mm_render(void *m, uint8_t *gates3, float *cv4, uint32_t n);

/* ---- the C++ runtime the vendored code needs (none of it allocates) ---- */
#ifdef __cplusplus
}
#endif
#endif
