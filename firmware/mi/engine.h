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
#define ENG_NPAGES 71                     /* (68 until 0.16.22: the followers' three pages after them, 0.16.23) */
#define ENG_NPARAMS (ENG_NPAGES * 4)
#define ENG_STEPS 64                     /* the steps a lane: four pages of 16 */
#define ENG_PLOCKS 367                   /* parameter locks, for all the lanes' steps (384 until 0.16.6: four gave
                                          * their room to CLIMB's randoms, 0.16.7; 380 until 0.16.22: thirteen to the
                                          * followers' pages, 0.16.23, the project the same size) */
#define ENG_ENDLESS 64                   /* an Endless sequence's notes (and rests), a track */
#define ENDLESS_REST 0xFF

/* the pages */
#define PG_TRACK(t, p) ((t) * 3 + (p))   /* Plaits: its sound, its envelope, its pitch */
#define PG_LFO(i) (12 + (i))              /* the three LFOs (0.4: in Tides' place): WAVE, RATE, MODE (free, synced to the
                                          * tempo, or started again at a track's trigs), SLEW */
#define PG_CURVE 15                      /* (0.16; page 15 unused before, 0 in every project) the tracks' ratchets'
                                          * CURVE, a knob a track: to the right the hits closer and closer and softer (a
                                          * ball bouncing), to the left further and further apart, 0 even */
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
                                          * (the screen: its third and fourth knobs are the lane's LENGTH and PROB);
                                          * then (0.16.7, a track's, in what was unused) CLIMB, the semitones (SCALE
                                          * on: its notes) a step's trig climbs each time it plays, -12..12; then
                                          * (0.16.19, a track's, in what was unused: 0 in every project since
                                          * 0.16.7, climb_off_all) its SPREAD, the mixer's (engine.cc: spread_track) */
#define PG_SPREAD_PARAM(t) (PG_EUC(t) * 4 + 3)   /* a track's SPREAD: 0 the track as it is, 1 its widest */
#define PG_FILT(t, f) (52 + (t) * 3 + (f))   /* a track's filter (mi_filter.cc), three pages of four (the screen shows
                                          * them otherwise: app.c): CUTOFF, RESO, ENV, DECAY; DRIVE, PING, FM, KEY;
                                          * TYPE (LP, BP, HP; VOX: low, mid, high voices), NOISE, COLOR, ATTACK. The
                                          * first's ALGORITHM: the model (OFF, LADDER, MS-20, SEM, VOX) */
#define PG_GLITCH(t) (64 + (t))          /* its glitch (after its filter): GLITCH (a sixteenth's chance to repeat), SIZE
                                          * (the slice: 1/64 .. 1/8), BEND (each repeat slower and lower, or faster
                                          * and higher), DROP (a repeat's chance of an accident: backwards, a hole) */
#define PG_FOLLOW(i) (68 + (i))          /* (0.16.23) an LFO's follower (its WAVE on FOLLOW), its input: SOURCE (the
                                          * track it listens to), GAIN (-24..+24 dB), THRESH (-60..0 dB: TRIG's,
                                          * FOLLOW's floor), FILTER (to the left the highs cut, to the right the lows) */
enum { PT_PLAITS1, PT_PLAITS2, PT_PLAITS3, PT_LFO, PT_MIX, PT_PAN, PT_REVERB, PT_RSEND, PT_DELAY1,
       PT_DELAY2, PT_DSEND, PT_STEP, PT_LANE, PT_GLOBAL, PT_ARP, PT_WARPS1, PT_WARPS2, PT_DUCK, PT_COMP, PT_OTT, PT_EQ,
       PT_BEND, PT_EUC, PT_FILT1, PT_FILT2, PT_FILT3, PT_GLITCH, PT_CURVE, PT_FOLLOW, NPT };
enum { BS_OFF, BS_RECT, BS_DOWN, BS_DOWN3, BS_FOLD, BS_SWAP, BS_N };   /* the bits' SHAPE */
#define BM_N 12                          /* the bits' MASKs */
enum { GL_64, GL_32, GL_16, GL_8, GL_N };   /* the glitch's SIZE */
enum { MB_OFF, MB_COMP, MB_OTT };       /* the master bus's mode (PG_COMP's ALGORITHM) */
/* Warps' carrier (PG_WARPS(0)'s first knob): none (Warps off), a track, or the module's internal oscillator, its three
 * shapes (sine, triangle, saw) */
enum { WC_OFF, WC_T1, WC_T2, WC_T3, WC_T4, WC_OSC1, WC_OSC2, WC_OSC3 };
enum { WM_WARPS, WM_DRY, WM_SHIFT };    /* Warps' MODE: the modulator's track silenced as Warps opens, heard too, the
                                         * frequency shifter (the module's easter egg) */
enum { LW_SINE, LW_TRI, LW_SAW, LW_RAMP, LW_SQUARE, LW_SH, LW_SMOOTH,   /* an LFO's waves; (0.16.22) the rhythmic */
       LW_PUMP, LW_STAIRS, LW_CLAVE, LW_BOUNCE, LW_RISES, LW_FALLS, LW_ZIGZAG,   /* ones, their figures on a cycle cut */
       LW_CHAOS, LW_FOLLOW, LW_N };      /* in eight, and Chaos (Lorenz's attractor); (0.16.23) FOLLOW: a track's
                                          * level (its RATE ATTACK, its SLEW DECAY, its MODE LF_*; its input on
                                          * PG_FOLLOW) */
enum { LF_FOLLOW, LF_TRIG, LF_T1 };     /* FOLLOW's MODE (in the LFO's MODE): the level followed; an attack and decay
                                          * at each rise over THRESH; at each of T1's (..T4's) trigs */
enum { LM_FREE, LM_SYNC, LM_T1 };       /* an LFO's MODE: free, synced to the tempo, started at T1's (..T4's) trigs */
enum { DL_SYNC, DL_FREE };              /* the delay's ALGORITHM: its time in divisions of the tempo, or in ms */

/* a track's parameters as a trig sees them (a lock, a random): its Plaits' 12, its level, sends and pan, its step
 * page's note, probability and ratchets, its duck, its filter. An effect lane's are its effect's knobs
 * (eng_lane_params) */
enum { TP_DECAY = 4, TP_PITCHENV = 8, TP_LENGTH, TP_TRANSPOSE, TP_OCTAVE, TP_LEVEL, TP_SEND, TP_PAN, TP_NOTE, TP_PROB,
       TP_RATCHET, TP_DSEND, TP_DUCK, TP_EQ, TP_CLOCK, TP_CRUSH, TP_MASK, TP_BSHAPE, TP_CUTOFF, TP_RESO, TP_FENV,
       TP_FDECAY, TP_FDRIVE, TP_PING, TP_FM, TP_KEY, TP_FTYPE, TP_NOISE, TP_COLOR, TP_FATTACK, TP_GLITCH, TP_GSIZE,
       TP_GBEND, TP_GDROP, TP_CURVE, TP_CLIMB, ENG_NTP };   /* (0.7: the grit's four the bits', the glitch's four
                                         * after the filter's; 0.16: the ratchets' CURVE; 0.16.7: CLIMB) */
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
int eng_param_centered(int param);       /* (0.16.16) its knob drawn from the middle, - + under it */
const char *eng_param_name(int param);   /* its name as the module prints it: Plaits' HARMONICS, TIMBRE and MORPH by
                                          * model (the kick's MORPH is its Decay) */
int eng_model_enveloped(int model);      /* 1: a Plaits model with its own envelope, the LPG bypassed (6-op FM,
                                          * string, modal, the drums), or an FM drum */
/* the models (0.10.1), in ALGORITHM's order, by bank: MIFM1's six drums (mi_drum.cc), the LXR-02's four FM drums
 * (mi_lxr.cc), MIFM1's own voices (ZERO, mi_zero.cc; the FM, four operators and eight algorithms: mi_fm4.cc), then
 * MIFM1's SQ-1 (0.16.14: a pocket Ensoniq, mi_sq1.cc), then Plaits' (its speech, its noises and physical models). A model with its own knobs (a drum, an FM drum,
 * ZERO, the FM) takes its eight parameters from the knobs after FREQ: HARMONICS, TIMBRE, MORPH, the envelope page's
 * four, the pitch envelope's. Its kind says what plays it, its sub its index there (Plaits' engine, the LXR's voice,
 * the drum, the FM's kit) */
enum { MK_PLAITS, MK_LXR, MK_ZERO, MK_DRUM, MK_FM, MK_PADS, MK_POLY, MK_PROPHET, MK_SQ1, MK_TAURUS, MK_VOCODER,
       MK_STATIC };
#define ENG_MODELS 25                    /* (0.16.20: TAURUS at 16; 0.16.21: VOCODER at 17; 0.16.24: STATIC, MIFM1's
                                          * last, at 18; Plaits' from 19) */
#define ENG_BANKS 5
int eng_model_kind(int m);
int eng_model_sub(int m);
int eng_model_bank(int m);
int eng_model_own(int m);                /* 1: its own eight knobs (not Plaits') */
int eng_sq_cycle(int t, float *y, int n, int *of);   /* SQ-1 on track t: the cycle it reads (n points of a period, -1..1),
                                                   * its place + 32 x its program; -1 none; of: its places */
const char *eng_sq_family(int t);        /* SQ-1 on track t: its program's family ("" another machine) */
int eng_model_find(int kind, int sub);   /* the model a kind's sub is, -1 none */
const char *eng_model_abbr(int m);       /* in four letters or so (the tracks' row) */
const char *eng_bank_name(int b);
int eng_fm_algo(int t);                  /* the FM on track t: its algorithm now (0..7, its cables and locks), -1 none */
int eng_fm_shaped(int t);                /* its SHAPE now: the operators it moves (bit i: operator i + 1), 0 none (Flat) */
float eng_fm_shape(int t, int op, float x);   /* operator op's level in it at x (its units), -1..1 */
int eng_model_free(int t, int m);        /* 0: a model once a project (PADS) another track holds */
int eng_pads_track(void);                /* the track PADS is on, -1 none */
void eng_pads_chord(char *b, int n);     /* its chord now, named (Am7, F, Gsus4...); its place: eng_pads_pos */
int eng_pads_pos(void);                  /* the progression's chord now (0..), -1 before its first trig */
void eng_pads_line(char *b, int n);      /* the progression, its place, the chord: "Tintin 3/8 Am" */
int eng_voc_play(int param);
int eng_voc_chord(int t, float *out);   /* its chord now (PLAY on Manual or a progression), its notes: how many, 0 none */             /* (0.16.28) 1: VOCODER's PLAY (FREQ's place): its choices a detent each */
void eng_static_mode(int param, float before);   /* (0.16.27) a knob turned from before: STATIC's MODE to another
                                         * generator, its other knobs that one's */
void eng_set_model(int t, int model);    /* a track's model (an FM drum new to it: its kit on those knobs) */
/* the presets (0.15.1): a track's sound, all of what plays it but its mixing, its steps and its cables: its eight pages'
 * knobs and choices (Plaits' three: the sound, the envelope, the pitch; the bits; the filter's three; the glitch), their
 * knobs' randoms, a name. Saved by the screen (app.c, pat_store.h), read onto any track */
#define SND_PAGES 8
#define SND_MAGIC 0x31444E53u            /* "SND1" */
#define SND_VERSION 7                    /* (6: 0.16.21's to 0.16.23's, STATIC (0.16.24) not yet at 18: read, the
                                          * models from 18 one further on, as the older ones after theirs; 5: 0.16.20's, VOCODER (0.16.21) not yet at 17: read, the models from 17 one
                                          * further on, as 1 to 4's after theirs; 4: 0.16.18's and 0.16.19's, TAURUS
                                          * (0.16.20) not yet at 16 either: from 16 one further on first; 3: 0.16.17's, ROBOVOX (16)
                                          * read as SPEECH from noon; 2: 0.16.14 to 0.16.16's, as 4; 1: before 0.16.14:
                                          * CHIPTUNE, FORMANT read as SQ-1) */
typedef struct {
    uint32_t magic;
    uint16_t version, size;
    float val[SND_PAGES * 4];
    uint8_t algo[SND_PAGES];             /* the first: the model; the fifth: the filter's */
    int8_t rnd[SND_PAGES * 4];           /* each knob's random (0: none; the pages' knobs without one: 0) */
    uint8_t rseed[SND_PAGES * 4];
    char name[12];                       /* a factory sound's, or its model's */
} eng_sound_t;
void eng_sound_get(int t, eng_sound_t *s);
int eng_sound_set(int t, const eng_sound_t *s);   /* 0 done; -1 not a sound; -2 PADS (once a project) on another track */
int eng_sound_check(const eng_sound_t *s, int bytes);   /* 1: a sound this version reads */
#define ENG_FACTORY 16                   /* the factory's sounds (bank A) */
int eng_factory(int i, eng_sound_t *s);
const char *eng_factory_name(int i);
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
#define PROJECT_VERSION 34u              /* (33: 0.16.23's: STATIC (0.16.24) not yet at 18: read, the models from 18
                                          * one further on, as the older ones after theirs; 32: 0.16.21's and 0.16.22's: 68 pages, 380 locks, no followers: read,
                                          * the followers' pages at their defaults, the locks past 367 left out;
                                          * 31: 0.16.20's: VOCODER (0.16.21) not yet at 17, the models from there one
                                          * lower: read, one further on; 30: 0.16.18's and 0.16.19's: TAURUS (0.16.20)
                                          * not yet at 16 either: read, from 16 one further on, then from 17;
                                          * 29: 0.16.17's, ROBOVOX (16, withdrawn) there: read, SPEECH back from noon
                                          * (its locks on its knobs gone); 28: 0.16.14's to 0.16.16's: as 30;
                                          * 27: 0.16.7's to 0.16.13's, CHIPTUNE (15) and FORMANT (16) there: read,
                                          * both SQ-1 (its kit, their locks on its knobs gone), the others one lower;
                                          * 26: 0.16.4's to 0.16.6's, without CLIMB (42 parameters a lane, 384 locks,
                                          * 3820 bytes): read, CLIMB off, its locks the first 380;
                                          * 25: 0.16.2's and 0.16.3's, RATCH 1..8 then eight tuplets (9..16): read, each
                                          * as hits over a span, its locks too;
                                          * 24: 0.16.0's and 0.16.1's, without Endless' options: read, all its notes
                                          * (eng_upgrade reads them; 0.16.32: the versions before, 0.1.4's to 0.15's,
                                          * no longer: such a project, or a pattern, is not loaded) */
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
    uint8_t trauma;                      /* TRAUMA (0.14, OCT+ held, SELECT): 0..100 % of a dose of saturation (engine.cc:
                                          * the drives, the filters' FM, Warps), 0 nothing; last, in what was 0.13's
                                          * padding: its projects the same size */
    int8_t speed[ENG_LANES];             /* (0.16) each lane's SPEED: -4..4, x1/8 x1/4 x1/2 x3/4 x1 x3/2 x2 x3 x4 (0: x1) */
    uint8_t dir[ENG_LANES];              /* its DIRECTION: DIR_* */
    uint8_t endless_opt[ENG_TRACKS][4];  /* (0.16.2) Endless' options (its second face): STRIDE (bits 0..2: 1..7, 0 as
                                          * 1) and REST (bits 3..6: 0 none, 2..8) a byte; HOLES, LEAP (0..100 %);
                                          * then (0.16.7, its spare byte) CLIMB's: TIMES (bits 0..3: 0 as 4, else
                                          * 1 + them, 2..16), ON (bits 4, 5: CLON_*), SHAPE (bits 6, 7: CLSH_*) */
    uint8_t arp_opt[ENG_TRACKS];         /* (0.16.13, in the spare) Endless' ARP: MODE (bits 0, 1: AM_*), OCTAVES (bits
                                          * 2, 3: 1 + them), LATCH (bit 4), ORDER (bit 5: 0 by pitch, 1 as played) */
    uint8_t rsv[4];                      /* (0.16.7) spare, 0: the project at the patterns' record's size */
} project_t;
#define PROJECT_SIZE_24 3804u            /* (0.16.0's and 0.16.1's project, version 24) */
#define PROJECT_SIZE_26 3820u            /* (0.16.2's to 0.16.6's, versions 25, 26) */
#define PROJECT_SIZE_32 3824u            /* (0.16.7's to 0.16.22's, versions 27 to 32; now's too) */
enum { CL_TIMES, CL_ON, CL_SHAPE };      /* (0.16.7) CLIMB's options: how many times before the note again (2..16), */
enum { CLON_TRIG, CLON_RATCH, CLON_BOTH, CLON_N };   /* what climbs: each play of the trig, each hit of its ratchets
                                          * (from the trig's note again), both (each hit on from the last) */
enum { CLSH_RAMP, CLSH_BOUNCE, CLSH_RANDOM, CLSH_N };   /* how: up to the last then the note again, up and back down,
                                          * any of them */
int eng_climb_opt(int t, int o);         /* TIMES 2..16, ON CLON_*, SHAPE CLSH_* */
void eng_climb_opt_set(project_t *p, int t, int o, int v);
const char *eng_climb_on_name(int v);
const char *eng_climb_shape_name(int v);
int eng_climb_now(int t, int step);      /* the transposition that step plays now (its last), 0 none */
enum { AO_MODE, AO_OCT, AO_LATCH, AO_ORDER };   /* (0.16.13) Endless' ARP face: what it walks (its DIR, STRIDE, REST,
                                          * HOLES, LEAP, RATE, GATE as ever), */
enum { AM_LOOP, AM_KEYS, AM_CHORD, AM_N };   /* its recorded notes, the keys held (an arpeggiator), PADS' chord */
int eng_arp_opt(int t, int o);           /* MODE AM_*, OCTAVES 1..4, LATCH 0/1, ORDER 0 (by pitch) / 1 (as played) */
void eng_arp_opt_set(project_t *p, int t, int o, int v);
const char *eng_arp_mode_name(int v);
void eng_arp_key(int t, int note, int on);   /* KEYS: a key down or up (LATCH: kept; the first after all let go: anew) */
int eng_arp_list(int t, uint8_t *out, int max);   /* what KEYS or CHORD walks now (its octaves in), how many */
void eng_arp_release_all(void);          /* every key let go (a key let go on another track meanwhile: nothing stuck) */
enum { EO_STRIDE, EO_REST, EO_HOLES, EO_LEAP, EO_N };   /* Endless' options as the screen reads and writes them */
int eng_endless_opt(int t, int o);       /* STRIDE 1..7, REST 0 or 2..8, HOLES and LEAP 0..100 */
void eng_endless_opt_set(project_t *p, int t, int o, int v);
enum { DIR_FWD, DIR_BACK, DIR_PINGPONG, DIR_RANDOM, DIR_DRUNK, DIR_TWO_ONE, DIR_N };   /* a lane's DIRECTION: forward,
                                          * backward, there and back, a step at random, a step either way at random,
                                          * two steps forward and one back */
#define ENG_SPEEDS 9                     /* a lane's SPEED: -4..4 */
const char *eng_speed_name(int s);       /* x1/8 .. x4 */
const char *eng_dir_name(int d);
/* (0.16.4) a step's ratchets: RATCH so many hits (1..16) over SPAN of the lane's steps (1..16), both in RATCH's
 * parameter (and its locks, its random): hits + 16 x (span - 1), 1..256 (a lock's 256 values, each its own) */
#define ENG_RATCH_MAX 256
int eng_ratch_hits(int r, int *span);   /* RATCH's value r: its hits, its span in *span */
int eng_ratch_join(int hits, int span);
void eng_rnd_redraw(project_t *p, int lane, int tp);   /* a random knob back at 0: its next loop another */
int eng_algo_count(int page);            /* 0: the page has no ALGORITHM choice */
float eng_dice_span(int param);          /* how far the random dose moves a parameter at 100 % (0: not at all) */
/* TRAUMA at a dose (0..100), as its screen tells it: the drive's gain (a trig's full share), the filters' FM at most
 * (its depth, 0..0.1), the share of beats Warps opens on (-1: Warps not patched), the delay's DRIVE added */
void eng_trauma(int dose, float *gain, float *fm, float *warps, float *delay);
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
/* (0.16.15) a track's trig held over the steps after it (the step held, another pressed further): its steps more
 * (0: its own; 63 at most), a lock of its own kind (cleared with the step's others) */
int eng_step_tie(int lane, int step);
int eng_step_tie_set(int lane, int step, int steps);   /* 0 ok, -1 no room */
void eng_step_ties(int lane, uint8_t *out);   /* all ENG_STEPS of them, one look */
/* (0.16.16) a track's step as a chord: its NOTE the lowest, up to three notes over it (locks of their own kind);
 * the machines that play chords (eng_track_chords: POLY in Poly, REV3 with CHORD Off, OTIS) play them all, the others
 * NOTE alone */
int eng_step_notes(int lane, int step, int *out);   /* its notes (NOTE's values), the lowest first: how many (1..4) */
int eng_step_notes_set(int lane, int step, const int *notes, int n);   /* 0 ok; -1 no room; -2 out of NOTE's range */
void eng_step_chords(int lane, uint8_t *out);   /* every step's notes over its NOTE (0: alone), ENG_STEPS of them */
int eng_track_chords(int t);             /* 1: track t's machine plays chords (a step's, the keys') */
void eng_chord_name(const int *notes, int nn, char *b, int n);   /* "Cm7" (0: C), its notes the lowest first */
void eng_steps_clear(int lane);          /* the lane's pattern: every trig and lock (its knobs and randoms stay) */
void eng_fresh(void);                    /* another project loaded: the last one's trigs' locks and randoms gone */
int eng_step_locked(int lane, int step); /* how many locks it has */
int eng_step_now(int lane);              /* the step playing, -1 stopped */
float eng_lane_pos(int lane);            /* (0.16.31) its place since PLAY, in its steps (-1 stopped, counting in) */
void eng_rec_skip(int lane, int step);   /* live recording: a step written just ahead of it, not played there */
void eng_count_in(int sixteenths);       /* the next PLAY's count-in */
void eng_click(int on);                  /* the metronome: a ping at each beat (the bar's a fifth up) */
#define ENG_CLICK_N 128                  /* (0.16.34) the click apart: the last eng_render's (blocks up to this), in its */
const float *eng_click_out(void);        /* l and r too; null when silent (the USB takes the mix without it: app.c) */
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
void eng_endless_free(int t, int on);    /* (0.16.11) the sequencer stopped: Endless played while a key is held (on),
                                          * from its first note; -1: every track let go */
enum { ES_NOTE, ES_REST, ES_PAUSE, ES_HOLE, ES_LEAP };   /* (0.16.3) what Endless' last step did: a note, a recorded
                                          * rest, REST's pause, a hole (HOLES), a note an octave up (LEAP) */
int eng_endless_state(int t);
void eng_render(float *l, float *r, uint32_t n);   /* audio interrupt: the last block out (the effects and the master
                                          * bus play a block behind the voices: 2.9 ms) */
/* the USB audio input's channels for the block eng_render just gave (the audio interrupt, after it; its calls tell the
 * engine the host records: it makes them from the next block on, silence until then or when live is 0): 16-bit,
 * interleaved, T1 L R .. T4 L R (each track after its level, pan, filter, duck and Warps), then the delay's and the
 * reverb's return L R, at the master's scale on the USB: their sum is the master before its bus */
#define ENG_USB_CH 10
#ifndef ENG_USB_TRACKS
#define ENG_USB_TRACKS 0                 /* (0.16.33) set aside: the USB records the master (app.h); 1 makes them again */
#endif
#if ENG_USB_TRACKS
const int16_t *eng_usb_frames(uint32_t n, int live);
#endif
void eng_play(int on);                   /* the transport: the steps run or stop (from their start) */
void eng_cut(void);                      /* STOP twice: stopped, every sound cut (the voices, Warps, the effects' tails:
                                          * silent until the next trig) */
int eng_cut_pending(void);
int eng_playing(void);
/* the patterns (0.15: app.c keeps them, each a whole project): the next one cued, taken where the playing one ends
 * (its longest lane's LENGTH; the machine's settings stay, the lanes from their first step); next must stay as it is
 * until taken (eng_cue_pending 0), 0 cancels */
void eng_cue(const project_t *next);
int eng_cue_pending(void);
uint32_t eng_cycles(void);               /* the patterns played to their end (taken or again) */
int eng_pattern_len(void);               /* the playing one's, in sixteenths */
int eng_pattern_pos(void);               /* its sixteenth now, -1 stopped */
/* ALL undone in time: these knobs, models and randoms put back as the next bar starts (16 sixteenths of the tempo from
 * PLAY; stopped: at once) */
void eng_back_at_bar(const float *val, const uint8_t *algo, const int8_t *rnd);
int eng_back_pending(void);
void eng_back_cancel(void);
void eng_key(int track, int note, int on);   /* the keyboard plays a track (note: MIDI number); 0.16.15: the keys
                                              * held kept, the newest plays (back to the one before when let go); POLY
                                              * in MODE Poly, PROPHET with CHORD Off and SQ-1 play them all */
void eng_key_release_all(void);          /* every key let go (none down any more) */
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
uint32_t eng_peak_core1(void);           /* the worst block's tasks core 1 played (bit i: 0..3 the voices, 4 Warps, 5
                                          * the effects) */
int eng_guard(void);                     /* the CPU guard now: 0 idle, 1..3 the filters' FM lightened (a block over 92 %
                                          * of its time, a late one, two late ones; held 0.3 s a level) */
uint32_t eng_guard_blocks(void);         /* the blocks it has lightened since power-on */
void eng_guard_test(int on, int level);  /* (the tests: the guard on or off, or held at a level, -1 as it is) */
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
float eng_spread_width(int t);           /* (0.16.19) SPREAD: track t's width now, the side's gain over the middle's
                                          * (0: as it is; 0.8 the widest, in the middle) */
float eng_gr(int band);                  /* the master bus's gain now, dB (- reduced, + raised): the compressor's
                                          * (band 0), or OTT's low, mid and high bands (0, 1, 2) */
#define ENG_HIST 120                     /* the sources' recent past: one value every 4 blocks (1.4 s) */
int eng_hist(int src, int i);            /* -127..127, i 0 the oldest .. ENG_HIST - 1 the newest */
int eng_core1_running(void);

#ifdef __cplusplus
}
#endif
#endif
