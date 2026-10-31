/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1 bench: the scenes (one module, or a busy mix over both cores) and their timing (mi/scenes.cc).
 * sc_start in the main loop, sc_render in the audio interrupt; the rest anywhere. */
#ifndef MIFM1_SCENES_H_
#define MIFM1_SCENES_H_
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t measured;                   /* blocks measured (the first ones after a start are left out) */
    uint32_t blocks;                     /* the sums' weight: the averages are sum / blocks (halved now and then) */
    uint32_t c0_sum, c0_peak;            /* core 0's part of the render, in ticks (PLAT_TICKS_PER_US a us) */
    uint32_t c1_sum, c1_peak;            /* the job on the second core (or after core 0's part, on core 0) */
    uint32_t tot_sum, tot_peak;          /* the whole render, from its start to the last part done */
    uint32_t c1_jobs;                    /* blocks whose second part ran on the second core */
} sc_stats_t;

int sc_count(void);
const char *sc_name(int i);              /* "PLAITS 01 VA VCF" ... */
const char *sc_short(int i);             /* for the results list: at most 15 characters */
int sc_has_core1(int i);                 /* the scene has a part for the second core */
uint32_t sc_need(int i);                 /* its RAM, bytes */
uint32_t sc_arena(void);                 /* the RAM there is for scenes */
int sc_start(int i);                     /* main loop: 0 = playing, -1 = not enough RAM (silence) */
void sc_stop(void);                      /* main loop: silence, the modules not called (their tables may be away) */
int sc_current(void);
void sc_render(float *l, float *r, uint32_t n);   /* audio interrupt */
void sc_stats(sc_stats_t *s);            /* a copy of the current scene's numbers */
void sc_stats_reset(void);
void sc_core1(int on);                   /* use the second core (when it runs): 1, the default */

/* the modules' sizes, for the memory page */
uint32_t sc_size_plaits(void);
uint32_t sc_size_rings(void);
uint32_t sc_size_rings_reverb(void);
uint32_t sc_size_clouds(void);
uint32_t sc_size_warps(void);
uint32_t sc_size_tides(void);
uint32_t sc_size_marbles(void);

#ifdef __cplusplus
}
#endif
#endif
