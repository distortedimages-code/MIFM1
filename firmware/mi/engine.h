/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1's sound: four Plaits voices played by Marbles or the steps, two Tides as modulators, a matrix of cables,
 * Warps between two tracks, a mixer and Clouds' reverb (mi/engine.cc). The project (project_t) is what the screen edits and what is saved; the engine reads it at every
 * audio block (the control rate, 344 Hz), runs Marbles and Tides, adds the cables to the knobs and plays the voices,
 * two of them on the second core when it runs.
 *
 * Parameters: pages of 4 knobs (a page per face of a module, as the screen shows them). A parameter is
 * page * 4 + knob; its knob is val[] (0..1, bipolar -1..1, or an integer), its value after the cables final[]. */
#ifndef MIFM1_ENGINE_H_
#define MIFM1_ENGINE_H_
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENG_TRACKS 4
#define ENG_CABLES 8                     /* per track, and as many global */
#define ENG_NPAGES 35
#define ENG_NPARAMS (ENG_NPAGES * 4)
#define ENG_STEPS 64                     /* the step sequencer: steps a track, four pages of 16 */
#define ENG_PLOCKS 384                   /* parameter locks, for all the tracks' steps */
#define ENG_ENDLESS 64                   /* an Endless sequence's notes (and rests), a track */
#define ENDLESS_REST 0xFF

/* the pages: 3 per track, 2 per Tides, 4 for Marbles, the mixer's levels, the reverb's sends and its own knobs, the
 * tracks' pans, each track's step sequencer, the global settings */
#define PG_TRACK(t, p) ((t) * 3 + (p))
#define PG_TIDES(i, p) (12 + (i) * 2 + (p))
#define PG_MARBLES(p) (16 + (p))
#define PG_MIX 20
#define PG_FX(p) (21 + (p))
#define PG_PAN 23
#define PG_STEP(t) (24 + (t))
#define PG_GLOBAL 28
#define PG_ARP(t) (29 + (t))             /* each track's Endless: its rate, gate, direction, transposition */
#define PG_WARPS(p) (33 + (p))           /* Warps: its knobs (ALGORITHM, TIMBRE, LEVEL 1, LEVEL 2), its patching */
enum { PT_PLAITS1, PT_PLAITS2, PT_PLAITS3, PT_TIDES1, PT_TIDES2, PT_MARB_T1, PT_MARB_T2, PT_MARB_X1, PT_MARB_X2,
       PT_MIX, PT_FX1, PT_FX2, PT_PAN, PT_STEP, PT_GLOBAL, PT_ARP, PT_WARPS1, PT_WARPS2, NPT };
/* Warps' carrier (PG_WARPS(1)'s first knob): none (Warps off), a track, or the module's internal oscillator, its three
 * shapes (sine, triangle, saw; with the vocoder saw, pulse, noise) */
enum { WC_OFF, WC_T1, WC_T2, WC_T3, WC_T4, WC_OSC1, WC_OSC2, WC_OSC3 };
enum { SEQ_MARBLES, SEQ_STEPS };         /* PG_GLOBAL's first knob */

/* a track's parameters as a trig sees them (a lock, a random): its Plaits' 12, its level, send and pan, its step
 * page's note, probability and ratchets */
enum { TP_DECAY = 4, TP_TRANSPOSE = 10, TP_OCTAVE = 11, TP_LEVEL = 12, TP_SEND, TP_PAN, TP_NOTE, TP_PROB, TP_RATCHET,
       ENG_NTP };
int eng_track_param(int t, int tp);      /* its parameter */
int eng_param_tp(int param, int *t);     /* a parameter's tp and track, -1 if none */
int eng_tp_lockable(int tp);             /* 1: a lock or a random may move it */
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

typedef struct {
    uint8_t ts;                          /* track << 6 | step */
    uint8_t tp;                          /* the track parameter, 0xFF: a free slot */
    uint8_t v;                           /* its value, 0..255 over its travel */
    uint8_t rsv;
} plock_t;

/* the project: everything the sound depends on */
#define PROJECT_MAGIC 0x314A5250u        /* "PRJ1" */
#define PROJECT_VERSION 5u               /* (4: 0.1.4's to 0.1.9's, without Warps; 3: 0.1.3's, without Endless; 2: 0.1's:
                                          * eng_upgrade reads them) */
typedef struct {
    uint32_t magic;
    uint16_t version, size;
    float val[ENG_NPARAMS];
    uint8_t algo[ENG_NPAGES];            /* the page's ALGORITHM choice: Plaits' model, Tides' ramp, Marbles' T model
                                          * and scale */
    uint8_t out_mode;                    /* the output: 0 stereo, 1 a headset with a mic (the right inverted), 2 mono */
    uint8_t rsv[2];
    cable_t cable[ENG_TRACKS + 1][ENG_CABLES];   /* the tracks' cables, then the global ones */
    uint8_t trig[ENG_TRACKS][ENG_STEPS / 8];     /* the steps with a trig, a bit each */
    int8_t rnd[ENG_TRACKS][ENG_NTP];     /* each track parameter's random: > 0 new at each trig, < 0 looped, 0 none */
    uint16_t seed[ENG_TRACKS];           /* the looped randoms' */
    plock_t lock[ENG_PLOCKS];
    uint8_t endless[ENG_TRACKS][ENG_ENDLESS];   /* Endless: the notes recorded (MIDI), ENDLESS_REST a rest */
    uint8_t endless_len[ENG_TRACKS];     /* 0: none (the track plays its steps or Marbles) */
    int8_t endless_key[ENG_TRACKS];      /* the transposition the last key set */
} project_t;
int eng_algo_count(int page);            /* 0: the page has no ALGORITHM choice */
const char *eng_algo_name(int page, int a);

/* Warps as it plays: its algorithm (0..5 the cross-modulations, 6 the vocoder, 7 the frequency shifter; -1 off), its
 * name, the track its output takes the place of (-1 off) */
int eng_warps_algo(void);
const char *eng_warps_name(int algo);
int eng_warps_track(void);

/* the engine */
int eng_init(void);                      /* main loop, once: the modules in the engine's memory (0 ok) */
project_t *eng_project(void);            /* the live project: the screen writes it, the render reads it */
void eng_defaults(project_t *p);         /* the patch a new project plays (the demo) */
void eng_blank(project_t *p);            /* INIT: four Plaits at noon, nothing patched */
int eng_check(const project_t *p);       /* 1: a project this version can load */
int eng_upgrade(project_t *p, int bytes);   /* a project read whole or an older one, made this version's: 1 ok */

/* the step sequencer (PG_GLOBAL's SEQUENCER on STEPS); the screen edits, the render reads */
int eng_steps_mode(void);
int eng_trig(int t, int step);
void eng_trig_set(int t, int step, int on);
int eng_lock(int t, int step, int tp, float *v);   /* 1: that step locks tp (its value in v) */
int eng_lock_set(int t, int step, int tp, float v);   /* 0 ok, -1 no room */
void eng_lock_clear(int t, int step);    /* all its locks */
int eng_step_locked(int t, int step);    /* how many locks it has */
int eng_step_now(int t);                 /* the step playing, -1 stopped (or Marbles) */
int eng_quantize(int note);              /* a note in the global scale */
uint32_t eng_rises(int t);               /* the trigs track t played (a ratchet's each), since power-on */

/* Endless (after the OP-1's): a track records notes and rests one by one, then loops them on the tempo, over its
 * steps or Marbles; a key transposes it from its first note */
void eng_endless_rec(int t, int on);     /* recording: the track's sequencers silent, the keys play */
int eng_endless_add(int t, int note);    /* a note, or ENDLESS_REST; -1 full */
void eng_endless_clear(int t);
int eng_endless_on(int t);               /* 1: it plays the track */
void eng_endless_key(int t, int note);   /* a key: its transposition */
int eng_endless_pos(int t);              /* the note playing, -1 none */
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
