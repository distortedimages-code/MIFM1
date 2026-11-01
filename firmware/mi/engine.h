/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1's sound: four Plaits voices played by a step sequencer (or the keys), two Tides as modulators, a matrix of
 * cables, three effects with their own steps (a delay, Clouds' reverb, Warps between two tracks) and a mixer
 * (mi/engine.cc). The project (project_t) is what the screen edits and what is saved; the engine reads it at every
 * audio block (the control rate, 344 Hz), runs Tides, adds the cables to the knobs and plays the voices and Warps,
 * shared between the two cores by what each costs.
 *
 * Parameters: pages of 4 knobs (a page per face of a module, as the screen shows them). A parameter is page * 4 +
 * knob; its knob is val[] (0..1, bipolar -1..1, or an integer), its value after the cables final[].
 *
 * Lanes: the step sequencer has seven, the four tracks and the three effects (LANE_DELAY, LANE_REVERB, LANE_WARPS).
 * A lane's trig plays its track, or opens its effect (which falls back with the lane's DECAY; a lane without a trig
 * leaves its effect open); a lane's locks move its track's parameters or its effect's. */
#ifndef MIFM1_ENGINE_H_
#define MIFM1_ENGINE_H_
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENG_TRACKS 4
#define ENG_LANES 7                      /* the tracks, then the delay, the reverb, Warps */
#define LANE_DELAY 4
#define LANE_REVERB 5
#define LANE_WARPS 6
#define ENG_CABLES 8                     /* per track, and as many global */
#define ENG_NPAGES 40
#define ENG_NPARAMS (ENG_NPAGES * 4)
#define ENG_STEPS 64                     /* the steps a lane: four pages of 16 */
#define ENG_PLOCKS 384                   /* parameter locks, for all the lanes' steps */
#define ENG_ENDLESS 64                   /* an Endless sequence's notes (and rests), a track */
#define ENDLESS_REST 0xFF

/* the pages */
#define PG_TRACK(t, p) ((t) * 3 + (p))   /* Plaits: its sound, its envelope, its pitch */
#define PG_TIDES(i, p) (12 + (i) * 2 + (p))
#define PG_MIX 16                        /* the levels */
#define PG_PAN 17
#define PG_REVERB 18                     /* Clouds' reverb: its knobs, then the tracks' sends */
#define PG_RSEND 19
#define PG_DELAY(p) (20 + (p))           /* the delay: its knobs (two pages), then the tracks' sends */
#define PG_DSEND 22
#define PG_STEP(l) (23 + (l))            /* a lane's steps: a track's NOTE, PROB, RATCHET, LENGTH; an effect's DECAY,
                                          * PROB, DEPTH, LENGTH */
#define PG_GLOBAL 30
#define PG_ARP(t) (31 + (t))             /* each track's Endless: its rate, gate, direction, transposition */
#define PG_WARPS(p) (35 + (p))           /* Warps: its knobs (ALGORITHM, TIMBRE, LEVEL 1, LEVEL 2), its patching */
#define PG_DUCK 37                       /* the duck: its DECAY, then how deep T2, T3, T4 duck at T1's trigs */
#define PG_COMP 38                       /* the master bus: the compressor (THRESHOLD, RATIO, RELEASE, GAIN), its
                                          * ALGORITHM the bus's mode (off, the compressor, OTT) */
#define PG_OTT 39                        /* OTT (three bands squashed up and down): DEPTH, TIME, DRIVE, LEVEL */
enum { PT_PLAITS1, PT_PLAITS2, PT_PLAITS3, PT_TIDES1, PT_TIDES2, PT_MIX, PT_PAN, PT_REVERB, PT_RSEND, PT_DELAY1,
       PT_DELAY2, PT_DSEND, PT_STEP, PT_LANE, PT_GLOBAL, PT_ARP, PT_WARPS1, PT_WARPS2, PT_DUCK, PT_COMP, PT_OTT, NPT };
enum { MB_OFF, MB_COMP, MB_OTT };       /* the master bus's mode (PG_COMP's ALGORITHM) */
/* Warps' carrier (PG_WARPS(1)'s first knob): none (Warps off), a track, or the module's internal oscillator, its three
 * shapes (sine, triangle, saw) */
enum { WC_OFF, WC_T1, WC_T2, WC_T3, WC_T4, WC_OSC1, WC_OSC2, WC_OSC3 };

/* a track's parameters as a trig sees them (a lock, a random): its Plaits' 12, its level, sends and pan, its step
 * page's note, probability and ratchets. An effect lane's are its effect's knobs (eng_lane_params) */
enum { TP_DECAY = 4, TP_PITCHENV = 8, TP_LENGTH, TP_TRANSPOSE, TP_OCTAVE, TP_LEVEL, TP_SEND, TP_PAN, TP_NOTE, TP_PROB,
       TP_RATCHET, TP_DSEND, TP_DUCK, ENG_NTP };   /* (T1 has no TP_DUCK: it ducks the others) */
int eng_track_param(int lane, int tp);   /* its parameter (an effect lane: its effect's knob tp) */
int eng_param_tp(int param, int *lane);  /* a parameter's tp and lane, -1 if none */
int eng_tp_lockable(int lane, int tp);   /* 1: a lock or a random may move it */
int eng_lane_params(int lane);           /* how many: a track ENG_NTP; an effect its knobs (the delay 8, the reverb 4,
                                          * Warps 4), then its step page's DECAY, PROB and DEPTH */
int eng_page_type(int page);
int eng_page_track(int page);            /* the track a page belongs to, -1: none */
int eng_page_lane(int page);             /* the lane its steps are (a track's pages: the track; an effect's: its
                                          * lane), -1 none */

/* the parameters' kinds */
enum { K_UNI, K_BIP, K_INT, K_ENUM };    /* 0..1, -1..1, an integer lo..hi, a choice among names */
typedef struct {
    const char *name;
    uint8_t kind;
    int16_t lo, hi;                      /* K_INT, K_ENUM */
    const char *const *names;            /* K_ENUM */
} eng_pdef_t;
const eng_pdef_t *eng_pdef(int param);
const char *eng_param_name(int param);   /* its name as the module prints it: Plaits' HARMONICS, TIMBRE and MORPH by
                                          * model (the kick's MORPH is its Decay) */
int eng_model_enveloped(int model);      /* 1: a Plaits model with its own envelope, the LPG bypassed (6-op FM,
                                          * string, modal, the drums) */
int eng_param_mod(int param);            /* 1: a cable may move it */
void eng_param_text(int param, float v, char *b, int n);   /* its value as the screen writes it */

/* the modulation sources: Tides A's four outputs, Tides B's */
enum { SRC_NONE, SRC_A1, SRC_A2, SRC_A3, SRC_A4, SRC_B1, SRC_B2, SRC_B3, SRC_B4, NSRC };
const char *eng_src_name(int src);

typedef struct {
    uint8_t src, dst;                    /* SRC_*, a parameter */
    int8_t depth;                        /* -100..100 (0: no cable) */
    uint8_t rsv;
} cable_t;

typedef struct {
    uint8_t lane;                        /* 0xFF: a free slot */
    uint8_t step;
    uint8_t tp;                          /* the lane's parameter */
    uint8_t v;                           /* its value, 0..255 over its travel */
} plock_t;

/* the project: everything the sound depends on */
#define PROJECT_MAGIC 0x314A5250u        /* "PRJ1" */
#define PROJECT_VERSION 7u               /* (6: 0.3.0's to 0.3.2's, without the duck and the master bus; 5: 0.2's, with
                                          * Marbles; 4: 0.1.4's to 0.1.9's: eng_upgrade reads them) */
typedef struct {
    uint32_t magic;
    uint16_t version, size;
    float val[ENG_NPARAMS];
    uint8_t algo[ENG_NPAGES];            /* the page's ALGORITHM choice: Plaits' model, Tides' ramp */
    uint8_t out_mode;                    /* the output: 0 stereo, 1 a headset with a mic (the right inverted), 2 mono */
    uint8_t rsv[2];
    cable_t cable[ENG_TRACKS + 1][ENG_CABLES];   /* the tracks' cables, then the global ones */
    uint8_t trig[ENG_LANES][ENG_STEPS / 8];      /* the steps with a trig, a bit each */
    int8_t rnd[ENG_LANES][ENG_NTP];      /* each lane parameter's random: > 0 new at each trig, < 0 looped, 0 none */
    uint16_t seed[ENG_LANES];            /* the looped randoms' */
    plock_t lock[ENG_PLOCKS];
    uint8_t endless[ENG_TRACKS][ENG_ENDLESS];   /* Endless: the notes recorded (MIDI), ENDLESS_REST a rest */
    uint8_t endless_len[ENG_TRACKS];     /* 0: none (the track plays its steps) */
    int8_t endless_key[ENG_TRACKS];      /* the transposition the last key set */
} project_t;
int eng_algo_count(int page);            /* 0: the page has no ALGORITHM choice */
const char *eng_algo_name(int page, int a);

/* Warps as it plays: its algorithm (0..5 the cross-modulations, 6 the frequency shifter; -1 off), its name, the track
 * its output takes the place of (-1 off) */
int eng_warps_algo(void);
const char *eng_warps_name(int algo);
int eng_warps_track(void);

/* the engine */
int eng_init(void);                      /* main loop, once: the modules in the engine's memory (0 ok) */
project_t *eng_project(void);            /* the live project: the screen writes it, the render reads it */
void eng_defaults(project_t *p);         /* the patch a new project plays (the demo) */
void eng_blank(project_t *p);            /* INIT: four Plaits at noon, nothing patched, no steps */
int eng_check(const project_t *p);       /* 1: a project this version can load */
int eng_upgrade(project_t *p, int bytes);   /* a project read whole or an older one, made this version's: 1 ok */

/* the step sequencer; the screen edits, the render reads */
int eng_trig(int lane, int step);
void eng_trig_set(int lane, int step, int on);
int eng_lane_has_trigs(int lane);        /* 0: its effect stays open (no steps) */
int eng_lock(int lane, int step, int tp, float *v);   /* 1: that step locks tp (its value in v) */
int eng_lock_set(int lane, int step, int tp, float v);   /* 0 ok, -1 no room */
void eng_lock_clear(int lane, int step); /* all its locks */
void eng_steps_clear(int lane);          /* the lane's pattern: every trig and lock (its knobs and randoms stay) */
int eng_step_locked(int lane, int step); /* how many locks it has */
int eng_step_now(int lane);              /* the step playing, -1 stopped */
float eng_lane_open(int lane);           /* an effect lane's opening now, 0..1 (1: no steps) */
int eng_quantize(int note);              /* a note in the global scale */
uint32_t eng_rises(int t);               /* the trigs track t played (a ratchet's each), since power-on */

/* Endless (after the OP-1's): a track records notes and rests one by one, then loops them on the tempo, over its
 * steps; a key transposes it from its first note */
void eng_endless_rec(int t, int on);     /* recording: the track's steps silent, the keys play */
int eng_endless_add(int t, int note);    /* a note, or ENDLESS_REST; -1 full */
void eng_endless_clear(int t);
int eng_endless_on(int t);               /* 1: it plays the track */
void eng_endless_key(int t, int note);   /* a key: its transposition */
int eng_endless_pos(int t);              /* the note playing, -1 none */
void eng_render(float *l, float *r, uint32_t n);   /* audio interrupt: the last block out (the effects and the master
                                          * bus play a block behind the voices: 2.9 ms) */
void eng_play(int on);                   /* the transport: the steps run or stop (from their start) */
int eng_playing(void);
/* ALL undone in time: these knobs, models and randoms put back as the next bar starts (16 sixteenths of the tempo from
 * PLAY; stopped: at once) */
void eng_back_at_bar(const float *val, const uint8_t *algo, const int8_t *rnd);
int eng_back_pending(void);
void eng_back_cancel(void);
void eng_key(int track, int note, int on);   /* the keyboard plays a track (note: MIDI number) */
void eng_core1(int on);                  /* the second core takes its share when it runs (1, the default) */

/* what the screen shows */
float eng_final(int param);              /* the value after the cables */
float eng_src(int src);                  /* a source's value: -1..1 */
uint32_t eng_gates(void);                /* bit t: track t's gate now */
int eng_note(int track);                 /* its note now (MIDI number) */
uint32_t eng_load(int core);             /* the render's time on core 0 or 1, in tenths of % of the block (smoothed) */
uint32_t eng_peak(void);                 /* the worst total of the last second, same unit */
uint32_t eng_task_load(int task);        /* a voice's (0..3, while it plays), Warps' (4) or the effects' and the
                                          * master bus's (5) time, averaged, same unit */
float eng_meter(int t);                  /* track t's output (4: the reverb's return, 5: the delay's), its peak,
                                          * falling slowly */
float eng_duck(void);                    /* the duck now, 0..1 (1: T1's trig, as deep as each track's knob) */
float eng_gr(int band);                  /* the master bus's gain now, dB (- reduced, + raised): the compressor's
                                          * (band 0), or OTT's low, mid and high bands (0, 1, 2) */
#define ENG_HIST 120                     /* the sources' recent past: one value every 4 blocks (1.4 s) */
int eng_hist(int src, int i);            /* -127..127, i 0 the oldest .. ENG_HIST - 1 the newest */
int eng_core1_running(void);

#ifdef __cplusplus
}
#endif
#endif
