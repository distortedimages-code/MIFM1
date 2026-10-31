/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1's sound: four Plaits voices played by Marbles, two Tides as modulators, a matrix of cables, a mixer
 * (mi/engine.cc). The project (project_t) is what the screen edits and what is saved; the engine reads it at every
 * audio block (the control rate, 344 Hz), runs Marbles and Tides, adds the cables to the knobs and plays the voices,
 * two of them on the second core when it runs.
 *
 * Parameters: 23 pages of 4 knobs (a page per face of a module, as the screen shows them). A parameter is
 * page * 4 + knob; its knob is val[] (0..1, bipolar -1..1, or an integer), its value after the cables final[]. */
#ifndef MIFM1_ENGINE_H_
#define MIFM1_ENGINE_H_
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENG_TRACKS 4
#define ENG_CABLES 8                     /* per track, and as many global */
#define ENG_NPAGES 23
#define ENG_NPARAMS (ENG_NPAGES * 4)

/* the pages: 3 per track, 2 per Tides, 4 for Marbles, the mixer's levels, the reverb's sends and its own knobs */
#define PG_TRACK(t, p) ((t) * 3 + (p))
#define PG_TIDES(i, p) (12 + (i) * 2 + (p))
#define PG_MARBLES(p) (16 + (p))
#define PG_MIX 20
#define PG_FX(p) (21 + (p))
enum { PT_PLAITS1, PT_PLAITS2, PT_PLAITS3, PT_TIDES1, PT_TIDES2, PT_MARB_T1, PT_MARB_T2, PT_MARB_X1, PT_MARB_X2,
       PT_MIX, PT_FX1, PT_FX2, NPT };
int eng_page_type(int page);
int eng_page_track(int page);            /* the track a page belongs to, -1: global */

/* the parameters' kinds */
enum { K_UNI, K_BIP, K_INT, K_ENUM };    /* 0..1, -1..1, an integer lo..hi, a choice among names */
typedef struct {
    const char *name;
    uint8_t kind;
    int16_t lo, hi;                      /* K_INT, K_ENUM */
    const char *const *names;            /* K_ENUM */
} eng_pdef_t;
const eng_pdef_t *eng_pdef(int param);
int eng_param_mod(int param);            /* 1: a cable may move it */
void eng_param_text(int param, float v, char *b, int n);   /* its value as the screen writes it */

/* the modulation sources */
enum { SRC_NONE, SRC_A1, SRC_A2, SRC_A3, SRC_A4, SRC_B1, SRC_B2, SRC_B3, SRC_B4, SRC_X1, SRC_X2, SRC_X3, SRC_Y,
       SRC_T1, SRC_T2, SRC_T3, NSRC };
const char *eng_src_name(int src);
/* a track's gate and note sources (PLAITS 3/3) */
enum { GATE_T1, GATE_T2, GATE_T3, GATE_A1, GATE_A2, GATE_A3, GATE_A4, GATE_B1, GATE_B2, GATE_B3, GATE_B4, GATE_KEYS,
       NGATE };
enum { NOTE_X1, NOTE_X2, NOTE_X3, NOTE_Y, NOTE_FIXED, NNOTE };

typedef struct {
    uint8_t src, dst;                    /* SRC_*, a parameter */
    int8_t depth;                        /* -100..100 (0: no cable) */
    uint8_t rsv;
} cable_t;

/* the project: everything the sound depends on */
#define PROJECT_MAGIC 0x314A5250u        /* "PRJ1" */
#define PROJECT_VERSION 2u               /* (1: v0.1's, with the mixer's pans and no reverb) */
typedef struct {
    uint32_t magic;
    uint16_t version, size;
    float val[ENG_NPARAMS];
    uint8_t algo[ENG_NPAGES];            /* the page's ALGORITHM choice: Plaits' model, Tides' ramp, Marbles' T model
                                          * and scale */
    uint8_t rsv[2];
    cable_t cable[ENG_TRACKS + 1][ENG_CABLES];   /* the tracks' cables, then the global ones */
} project_t;
int eng_algo_count(int page);            /* 0: the page has no ALGORITHM choice */
const char *eng_algo_name(int page, int a);

/* the engine */
int eng_init(void);                      /* main loop, once: the modules in the engine's memory (0 ok) */
project_t *eng_project(void);            /* the live project: the screen writes it, the render reads it */
void eng_defaults(project_t *p);         /* the patch a new project plays (the demo) */
void eng_blank(project_t *p);            /* INIT: four Plaits at noon, nothing patched */
int eng_check(const project_t *p);       /* 1: a project this version can load */
void eng_render(float *l, float *r, uint32_t n);   /* audio interrupt */
void eng_play(int on);                   /* the transport: Marbles' clock runs or stops (from its start) */
int eng_playing(void);
void eng_key(int track, int note, int on);   /* the keyboard plays a track (note: MIDI number) */
void eng_core1(int on);                  /* two voices on the second core when it runs (1, the default) */

/* what the screen shows */
float eng_final(int param);              /* the value after the cables */
float eng_src(int src);                  /* a source's value: 0..1 (Tides and gates), X and Y as 0..1 of their range */
uint32_t eng_gates(void);                /* bit t: track t's gate now */
int eng_note(int track);                 /* its note now (MIDI number) */
uint32_t eng_load(int core);             /* the render's time on core 0 or 1, in tenths of % of the block (smoothed) */
uint32_t eng_peak(void);                 /* the worst total of the last second, same unit */
float eng_meter(int t);                  /* track t's output (4: the reverb's return), its peak, falling slowly */
#define ENG_HIST 120                     /* the sources' recent past: one value every 4 blocks (1.4 s) */
int eng_hist(int src, int i);            /* -127..127, i 0 the oldest .. ENG_HIST - 1 the newest */
int eng_core1_running(void);

#ifdef __cplusplus
}
#endif
#endif
