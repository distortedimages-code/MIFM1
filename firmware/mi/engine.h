/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1's sound: four Plaits voices played by a step sequencer (or the keys), three LFOs as modulators, a matrix of
 * cables, three effects with their own steps (a delay, Clouds' reverb, Warps between two tracks) and a mixer
 * (mi/engine.cc). The project (project_t) is what the screen edits and what is saved; the engine reads it at every
 * audio block (the control rate, 344 Hz), runs the LFOs, adds the cables to the knobs and plays the voices and Warps,
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
#define ENG_NPAGES 68
#define ENG_NPARAMS (ENG_NPAGES * 4)
#define ENG_STEPS 64                     /* the steps a lane: four pages of 16 */
#define ENG_PLOCKS 384                   /* parameter locks, for all the lanes' steps */
#define ENG_ENDLESS 64                   /* an Endless sequence's notes (and rests), a track */
#define ENDLESS_REST 0xFF

/* the pages */
#define PG_TRACK(t, p) ((t) * 3 + (p))   /* Plaits: its sound, its envelope, its pitch */
#define PG_LFO(i) (12 + (i))              /* the three LFOs (0.4: in Tides' place): WAVE, RATE, MODE (free, synced to the
                                          * tempo, or started again at a track's trigs), SLEW (page 15: not used) */
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
#define PG_WARPS(p) (35 + (p))           /* Warps: what to play at once (CARRIER, MOD, ALGORITHM, MIX), then the rest of
                                          * the module (TIMBRE, LEVEL 1, LEVEL 2, MODE: Warps, the modulator's track
                                          * heard dry too, or the frequency shifter) */
#define PG_DUCK 37                       /* the duck: its DECAY, then how deep T2, T3, T4 duck at T1's trigs */
#define PG_COMP 38                       /* the master bus: the compressor (THRESHOLD, RATIO, RELEASE, GAIN), its
                                          * ALGORITHM the bus's mode (off, the compressor, OTT) */
#define PG_OTT 39                        /* OTT (three bands squashed up and down): DEPTH, TIME, DRIVE, LEVEL */
#define PG_EQ 40                         /* the mixer's filter, a knob a track: to the left a low pass coming down
                                          * (the highs cut), to the right a high pass going up (the lows), 24 dB/oct */
#define PG_BEND(t) (41 + (t))            /* a track's circuit bending, its bits (0.7: the grit's place): CLOCK (a sample
                                          * and hold on a multiple of the note), BITS, MASK (its 8-bit word's bits
                                          * on, inverted or muted), SHAPE (a digital shaper) */
#define PG_EUC(l) (45 + (l))             /* a lane's Euclidean rhythm (its steps' last face): FILLS spread as evenly
                                          * as they go over the lane's LENGTH, ROTATE; turning them writes its trigs
                                          * (the screen: its third and fourth knobs are the lane's LENGTH and PROB) */
#define PG_FILT(t, f) (52 + (t) * 3 + (f))   /* a track's filter (mi_filter.cc), three pages of four (the screen shows
                                          * them otherwise: app.c): CUTOFF, RESO, ENV, DECAY; DRIVE, PING, FM, KEY;
                                          * TYPE (LP, BP, HP; VOX: low, mid, high voices), NOISE, COLOR, ATTACK. The
                                          * first's ALGORITHM: the model (OFF, LADDER, MS-20, SEM, VOX) */
#define PG_GLITCH(t) (64 + (t))          /* its glitch (after its filter): GLITCH (a sixteenth's chance to repeat), SIZE
                                          * (the slice: 1/64 .. 1/8), BEND (each repeat slower and lower, or faster
                                          * and higher), DROP (a repeat's chance of an accident: backwards, a hole) */
enum { PT_PLAITS1, PT_PLAITS2, PT_PLAITS3, PT_LFO, PT_MIX, PT_PAN, PT_REVERB, PT_RSEND, PT_DELAY1,
       PT_DELAY2, PT_DSEND, PT_STEP, PT_LANE, PT_GLOBAL, PT_ARP, PT_WARPS1, PT_WARPS2, PT_DUCK, PT_COMP, PT_OTT, PT_EQ,
       PT_BEND, PT_EUC, PT_FILT1, PT_FILT2, PT_FILT3, PT_GLITCH, NPT };
enum { BS_OFF, BS_RECT, BS_DOWN, BS_DOWN3, BS_FOLD, BS_SWAP, BS_N };   /* the bits' SHAPE */
#define BM_N 12                          /* the bits' MASKs */
enum { GL_64, GL_32, GL_16, GL_8, GL_N };   /* the glitch's SIZE */
enum { MB_OFF, MB_COMP, MB_OTT };       /* the master bus's mode (PG_COMP's ALGORITHM) */
/* Warps' carrier (PG_WARPS(0)'s first knob): none (Warps off), a track, or the module's internal oscillator, its three
 * shapes (sine, triangle, saw) */
enum { WC_OFF, WC_T1, WC_T2, WC_T3, WC_T4, WC_OSC1, WC_OSC2, WC_OSC3 };
enum { WM_WARPS, WM_DRY, WM_SHIFT };    /* Warps' MODE: the modulator's track silenced as Warps opens, heard too, the
                                         * frequency shifter (the module's easter egg) */
enum { LW_SINE, LW_TRI, LW_SAW, LW_RAMP, LW_SQUARE, LW_SH, LW_SMOOTH, LW_N };   /* an LFO's waves */
enum { LM_FREE, LM_SYNC, LM_T1 };       /* an LFO's MODE: free, synced to the tempo, started at T1's (..T4's) trigs */
enum { DL_SYNC, DL_FREE };              /* the delay's ALGORITHM: its time in divisions of the tempo, or in ms */

/* a track's parameters as a trig sees them (a lock, a random): its Plaits' 12, its level, sends and pan, its step
 * page's note, probability and ratchets, its duck, its filter. An effect lane's are its effect's knobs
 * (eng_lane_params) */
enum { TP_DECAY = 4, TP_PITCHENV = 8, TP_LENGTH, TP_TRANSPOSE, TP_OCTAVE, TP_LEVEL, TP_SEND, TP_PAN, TP_NOTE, TP_PROB,
       TP_RATCHET, TP_DSEND, TP_DUCK, TP_EQ, TP_CLOCK, TP_CRUSH, TP_MASK, TP_BSHAPE, TP_CUTOFF, TP_RESO, TP_FENV,
       TP_FDECAY, TP_FDRIVE, TP_PING, TP_FM, TP_KEY, TP_FTYPE, TP_NOISE, TP_COLOR, TP_FATTACK, TP_GLITCH, TP_GSIZE,
       TP_GBEND, TP_GDROP, ENG_NTP };    /* (0.7: the grit's four the bits', the glitch's four after the filter's) */
                                         /* (T1 has no TP_DUCK: it ducks the others) */
int eng_track_param(int lane, int tp);   /* its parameter (an effect lane: its effect's knob tp) */
int eng_param_tp(int param, int *lane);  /* a parameter's tp and lane, -1 if none */
int eng_tp_lockable(int lane, int tp);   /* 1: a lock or a random may move it */
int eng_lane_params(int lane);           /* how many: a track ENG_NTP; an effect its knobs (the delay 8, the reverb 4,
                                          * Warps 4), then its step page's DECAY, PROB and DEPTH */
int eng_page_type(int page);
int eng_page_track(int page);            /* the track a page belongs to, -1: none */
int eng_page_lane(int page);             /* the lane its steps are (a track's pages: the track; an effect's: its
                                          * lane), -1 none */
int eng_euclid(int len, int fills, int rot, int step);   /* 1: a trig on that step of the Euclidean rhythm */

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
                                          * string, modal, the drums), or an FM drum */
/* the models: Plaits' 24, then the four FM drums (the LXR-02's voices, mi_lxr.cc). An FM drum takes its eight
 * parameters from the knobs after FREQ: HARMONICS, TIMBRE, MORPH, the envelope page's four, the pitch envelope's */
#define ENG_MODELS 29
#define ENG_LX0 24
#define ENG_ARC 28                       /* MIFM1's own (mi_arc.cc, 0.8): after the FM drums, their knobs' places */
void eng_set_model(int t, int model);    /* a track's model (an FM drum new to it: its kit on those knobs) */
int eng_lx_knob(int param);              /* an FM drum's knob (0..7) that parameter is now, -1 none */
float eng_lx_step(int param, float v, int detents);   /* an FM drum's knob turned: one of its values a detent (its
                                          * 128, as on the LXR-02; a wave's 6) */
int eng_param_mod(int param);            /* 1: a cable may move it */
void eng_param_text(int param, float v, char *b, int n);   /* its value as the screen writes it */

/* the modulation sources: the three LFOs (-1..1) */
enum { SRC_NONE, SRC_L1, SRC_L2, SRC_L3, NSRC };   /* (0.3's were Tides' A1..A4, B1..B4) */
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
#define PROJECT_VERSION 15u              /* (14: 0.6's, with the grit (its four knobs where the bits are, no glitch):
                                          * read, the grit gone; 13: 0.5.8's and 0.5.9's, their filters in two pages (the type an
                                          * ALGORITHM, no NOISE as such, COLOR, ATTACK): read; 12: 0.5.6's, without the filters (8 pages, 8 lane parameters): read, the
                                          * filters off; 11: 0.5's, without the looped randoms' draws (rseed) either: read, 0 each; 10: 0.4's,
                                          * the same but without the FM drums either: read as it is; 9: 0.3.9's and 0.3.10's, with Tides, Warps' faces as they were, no random
                                          * dose; 8: 0.3.8's,
                                          * without the grit and the Euclidean rhythms either; 7: 0.3.3's to
                                          * 0.3.7's, without the filter either; 6: 0.3.0's to 0.3.2's, without
                                          * the duck and the master bus; 5: 0.2's, with Marbles; 4: 0.1.4's to 0.1.9's:
                                          * eng_upgrade reads them) */
typedef struct {
    uint32_t magic;
    uint16_t version, size;
    float val[ENG_NPARAMS];
    uint8_t algo[ENG_NPAGES];            /* the page's ALGORITHM choice: Plaits' model, the delay's mode, the bus' */
    uint8_t out_mode;                    /* the output: 0 stereo, 1 a headset with a mic (the right inverted), 2 mono */
    uint8_t warps_hq;                    /* Warps: 0 ECO (its cross-modulations at 3x), 1 HQ (6x, the module's) */
    uint8_t theme;                       /* the screen's colours (app.c's THEMES) */
    uint8_t dice;                        /* the random dose (OCT- held, SELECT): 0..100 % of a random offset on the
                                          * sound's knobs (eng_dice), 0 the sound as set; its last quarter adds fills */
    uint8_t dice_seed;                   /* which random: a new one each time the dose leaves 0 */
    cable_t cable[ENG_TRACKS + 1][ENG_CABLES];   /* the tracks' cables, then the global ones */
    uint8_t trig[ENG_LANES][ENG_STEPS / 8];      /* the steps with a trig, a bit each */
    int8_t rnd[ENG_LANES][ENG_NTP];      /* each lane parameter's random: > 0 new at each trig, < 0 looped, 0 none */
    uint16_t seed[ENG_LANES];            /* the looped randoms' */
    plock_t lock[ENG_PLOCKS];
    uint8_t endless[ENG_TRACKS][ENG_ENDLESS];   /* Endless: the notes recorded (MIDI), ENDLESS_REST a rest */
    uint8_t endless_len[ENG_TRACKS];     /* 0: none (the track plays its steps) */
    int8_t endless_key[ENG_TRACKS];      /* the transposition the last key set */
    uint8_t rseed[ENG_LANES][ENG_NTP];   /* each looped random's own draw: another each time its knob comes back to
                                          * 0 (eng_rnd_redraw), so the loop side plays a new one (0: 0.5.5's loops) */
} project_t;
void eng_rnd_redraw(project_t *p, int lane, int tp);   /* a random knob back at 0: its next loop another */
int eng_algo_count(int page);            /* 0: the page has no ALGORITHM choice */
float eng_dice_span(int param);          /* how far the random dose moves a parameter at 100 % (0: not at all) */
/* the random dose's fills (its last quarter, 76..100 %; none under): a fill every so many bars (4, 2, 1), over the
 * bar's last beat or two, on so many of the tracks that play steps (1..4); 0 when the dose gives none */
void eng_fills(int dice, int *every, int *beats, int *tracks);
uint32_t eng_fill_trigs(int t);          /* the fills' trigs track t played, since power-on */
float eng_filter_hz(int t);              /* a track's filter: its cutoff now (its envelope, FM and key in), Hz */
float eng_filter_env(int t);             /* its envelope now, 0..1 */
/* the circuit bending, for the screen: a cycle of a sine through a track's bits as they are now (n points, -1..1);
 * the bits' mask (its inverted and muted bits); its glitch now (0 live, 1 taking its slice, 2 repeating), the repeats
 * so far, their speed */
void eng_bend_preview(int t, float *y, int n);
void eng_bend_mask(int t, uint8_t *inv, uint8_t *cut);
int eng_glitch_state(int t, int *reps, float *speed);
uint32_t eng_filter_load(int t);         /* its time, in tenths of % of the block (its voice's task holds it), averaged */
const char *eng_algo_name(int page, int a);

/* Warps as it plays: its algorithm (0..5 the cross-modulations, 6 the frequency shifter; -1 off), its name, the track
 * its output takes the place of (-1 off) */
int eng_warps_algo(void);
const char *eng_warps_name(int algo);
int eng_warps_track(void);

/* the engine */
int eng_init(void);                      /* main loop, once: the modules in the engine's memory (0 ok) */
project_t *eng_project(void);            /* the live project: the screen writes it, the render reads it */
void eng_defaults(project_t *p);         /* the first demo (0.1's), the tests' patch */
void eng_groove(project_t *p);           /* the demo groove: a first power-on's, the browser emulator's */
void eng_blank(project_t *p);            /* INIT: four Plaits at noon, nothing patched, no steps */
void eng_stress_fm(project_t *p);        /* the same with the four FM drums on the tracks */
void eng_stress(project_t *p);           /* the stress test: a fixed patch keeping everything busy (the machine page's
                                          * figures, version after version) */
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
void eng_fresh(void);                    /* another project loaded: the last one's trigs' locks and randoms gone */
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
/* the USB audio input's channels for the block eng_render just gave (the audio interrupt, after it; its calls tell the
 * engine the host records: it makes them from the next block on, silence until then or when live is 0): 16-bit,
 * interleaved, T1 L R .. T4 L R (each track after its level, pan, filter, duck and Warps), then the delay's and the
 * reverb's return L R, at the master's scale on the USB: their sum is the master before its bus */
#define ENG_USB_CH 10
const int16_t *eng_usb_frames(uint32_t n, int live);
void eng_play(int on);                   /* the transport: the steps run or stop (from their start) */
void eng_cut(void);                      /* STOP twice: stopped, every sound cut (the voices, Warps, the effects' tails:
                                          * silent until the next trig) */
int eng_cut_pending(void);
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
float eng_now(int param);                /* the value playing: a track's, as its last trig made it (its lock, its random,
                                          * the dose's throw) over the cables */
float eng_src(int src);                  /* a source's value: -1..1 */
float eng_lfo_phase(int i);              /* an LFO's phase now, 0..1 (MOD mode: its key blinks with it) */
uint32_t eng_gates(void);                /* bit t: track t's gate now */
int eng_note(int track);                 /* its note now (MIDI number) */
uint32_t eng_load(int core);             /* the render's time on core 0 or 1, in tenths of % of the block (smoothed) */
void eng_delay_mode(int mode);           /* the delay's ALGORITHM (DL_SYNC, DL_FREE), its TIME kept */
uint32_t eng_prep_load(void);            /* core 0's preparation of a block (the sequencer, the LFOs, the cables), the same */
uint32_t eng_peak(void);                 /* the worst total of the last second, same unit */
uint32_t eng_peak_part(int i);           /* that worst block's own figures, same unit: 0..3 the voices (0: resting), 4
                                          * Warps, 5 the effects, 6 core 0's preparation, 7 core 0's total, 8 core 1's,
                                          * 9 the whole render; 10..14 the preparation's parts: the start and the LFOs,
                                          * the cables, the sequencer (the trigs), the effects' lanes, the voices'
                                          * knobs */
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
