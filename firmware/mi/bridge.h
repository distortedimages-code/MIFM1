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

/* MI_RAM (0.14.3): a function the FM-1 runs from RAM. Its code runs in place from the flash, through a small cache:
 * code not in it yet waits for the flash, a 32-byte line at a time (about 1 us: on the stress test, the first FM
 * track of a block paid ~170 us for the FM's code, the same FM on the next track nothing; instruction counts never see
 * it). What every track runs at every block goes in the RAM the FM-1 keeps for code (.ram_text, 32 KB, copied at
 * boot): the same instructions, read without waiting. Its part where calls into the flash are allowed (.c1_text: the
 * audio runs in the audio interrupt and on the second core, never while the flash is off, fm1-base's tools/build.py).
 * The host and the browser: nothing. */
#ifdef PLAT_HOST
#define MI_RAM
#else
#define MI_RAM __attribute__((section(".c1_text.mifm1")))
#endif

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

/* ---- the machines' state on a track (0.13): ZERO's, a drum's, the FM's, POLY's and PROPHET's share it, one machine a track at a
 * time. The engine's (mf_state); a machine's made when it comes to a track (its _reset), its init making all four its
 * own (and the engine's owners forgotten: each made again at its next use) ---- */
#define MF_STATE_BYTES 640
void *mf_state(int t);
void mf_forget(void);

/* ---- the drums (mi_drum.cc, 0.9): MIFM1's, after Sonic Charge's Microtonic, an oscillator and a noise each with its
 * envelope, mixed and driven; six models (KICK, SNARE, CLAP, HAT, TOM, PERC), one engine. Their eight knobs as an FM
 * drum's (k: 0..1, a bipolar knob from its middle): WAVE, SWEEP, DECAY, FILTER, N.DECAY, MIX, DRIVE, P.AMT ---- */
#define DR_MODELS 6
void drum_init(void);
void drum_reset(int t);                  /* its state on track t made (it comes there) */
void drum_silence(int t);
void drum_set(int t, int model, float note, const float *k);
void drum_gate(int t, int g);            /* a rise: a trig (at the next block) */
void drum_render(int t, float *out, uint32_t n);
const char *drum_model_name(int m);
const char *drum_knob_name(int k);
void drum_defaults(int m, float *k);
int drum_knob_steps(int k);
void drum_knob_text(int model, int k, float u, char *b, int n);

/* ---- ZERO (mi_zero.cc, 0.8.1): MIFM1's own voice, pure sines in true through-zero FM (a sine, triangle or saw
 * modulator), round pulses, a beat (a second sine, or the modulator off its ratio), each trig another sound or a loop
 * of sixteen. Its eight knobs as an FM drum's (k: 0..1, a bipolar knob from its middle): INDEX, RATIO, SHAPE, DECAY,
 * BEAT, MOVE, VARY, PULSE ---- */
void zero_init(void);
void zero_reset(int t);
void zero_silence(int t);                /* its sound cut: silent until the next trig (its loops kept) */
void zero_set(int t, float note, const float *k);
void zero_gate(int t, int g);            /* a rise: a trig (at the next block); a fall: a held note let go */
void zero_render(int t, float *out, uint32_t n);
const char *zero_knob_name(int k);
void zero_defaults(float *k);
int zero_knob_steps(int k);              /* RATIO's 11 ratios, the others 128 */
void zero_knob_text(int k, float u, char *b, int n);

/* ---- FM (mi_fm4.cc, 0.10.1): MIFM1's, four operators as the YM2612 has them (its eight algorithms, the first
 * operator's feedback) with a Hydrasynth's mutators on the operators heard (PW, SYNC); one machine. Its eight knobs
 * (k: 0..1, a bipolar knob from its middle): ALGO, RATIO (thirty voicings of the operators, the last eight chords),
 * INDEX, DECAY, MOTION (the operators' levels cycling), MOVE (- a swell, + a pluck), FEEDBACK, MUTATE ---- */
void fm4_init(void);
void fm4_reset(int t);
void fm4_silence(int t);
void fm4_set(int t, float note, const float *k);
void fm4_gate(int t, int g);            /* a rise: a trig (at the next block); a fall: a held note let go */
void fm4_render(int t, float *out, uint32_t n);
int fm4_algo(float u);                  /* ALGO's algorithm at u (the YM2612's 0..7) */
int fm4_chord(float u);                 /* 1: RATIO at u is a chord */
float fm4_chords_at(void);              /* where on RATIO (0..1) the chords start */
const char *fm4_knob_name(int k);
void fm4_defaults(float *k);
int fm4_knob_steps(int k);              /* ALGO's 8, RATIO's 30, SHAPE's 15, the others 128 */
int fm4_shape(float u);                 /* SHAPE at u: -7 .. 7 (0 none) */
int fm4_modulators(int algo);           /* the operators SHAPE moves (bit i: operator i + 1) */
float fm4_shape_level(float u, int op, float x);   /* its operator op's level at x (its units: the drawing's) */
float fm4_from_move(float move);         /* 0.10's MOVE (-1..1) as SHAPE (-1..1) */
void fm4_knob_text(int k, float u, char *b, int n);

/* ---- PADS (mi_pads.cc, 0.11): MIFM1's chords and their progressions (MANUAL, then sixteen: after Arvo Part,
 * Pachelbel, la Folia, the lament, the circle of fifths, Satie, Bach...), built in a scale (a 12-bit mask over its
 * root), played by a string pad; once a project. Its eight knobs (k: 0..1, a bipolar knob from its middle): PROG,
 * COLOR, VOICING, DECAY, ATTACK, TONE, ENSEMBLE (- VINTAGE, + WIDE), BELLS ---- */
int pads_progs(void);                    /* MANUAL and the progressions */
int pads_prog(float u);                  /* PROG at u: 0 MANUAL, 1.. a progression */
int pads_prog_len(int prog);             /* its chords */
int pads_prog_mode(int prog);            /* its own mode (the engine's scale: 1 major, 2 minor...) */
/* a chord: the prog's pos-th (MANUAL: the triad on the scale's degree), COLOR its extensions; its notes (semitones
 * over the scale's root, the root first) in out (6 at most), how many; raised: a note the scale lacks (a raised third,
 * 0..11), -1 none */
int pads_chord(int prog, int pos, int degree, uint16_t mask, float color, int *out, int *raised);
uint16_t pads_chord_scale(const int *chord, int n, uint16_t mask, int raised);   /* its scale, the avoid notes out */
void pads_init(void);
void pads_silence(void);
void pads_set(float note, const float *k, const int *chord, int n, int tonic);   /* the note: the chord's register */
void pads_gate(int g);
void pads_render(float *out, uint32_t n);
int pads_voices(void);                   /* the notes sounding (0: silent) */
const char *pads_knob_name(int k);
void pads_defaults(float *k);
int pads_knob_steps(int k);              /* PROG's 17, COLOR's and VOICING's 4, the others 128 */
void pads_knob_text(int k, float u, char *b, int n);

/* ---- POLY (mi_poly.cc, 0.12): a small polyphonic analog synthesizer after Roland's SH-101 and S-1, four voices a
 * track: its oscillator (saw, pulse, PWM, its sub), the IR3109's four poles, one envelope; chords (a chord memory's,
 * or the harmony's: PADS' chord, the scale's). Its eight knobs (k: 0..1, a bipolar knob from its middle): WAVE,
 * CUTOFF, RESO, DECAY, SUB, ENV (- swell, + its depth), CHORD (-: the harmony's, +: a chord memory's), VOICE
 * (- VINTAGE, + UNISON) ---- */
/* a trig's notes (4 at most) in out, how many: CHORD at u over the note; pc, pn: PADS' chord (semitones over the
 * scale's root, its root first; pn 0: none), the scale (bit i: i semitones over root; 0 or 0xFFF: major) */
int poly_notes(float u, float note, const int *pc, int pn, uint16_t mask, int root, float *out);
void poly_init(void);
void poly_reset(int t);
void poly_silence(int t);
void poly_set(int t, const float *notes, int n, const float *k);
void poly_gate(int t, int g);
void poly_render(int t, float *out, uint32_t n);
int poly_voices(int t);                  /* its voices sounding */
int poly_chord(float u);                 /* CHORD at u: -7 .. 7 (0 Off) */
const char *poly_knob_name(int k);
void poly_defaults(float *k);
int poly_knob_steps(int k);              /* CHORD's 15, the others 128 */
void poly_knob_text(int k, float u, char *b, int n);

/* ---- PROPHET (mi_prophet.cc, 0.13): a five-voice classic after Sequential Circuits' Prophet-5 (its third revision),
 * four voices a track: two oscillators (thirteen settings of them: saws, pulses, a fifth, an octave, a sub, synced, a
 * bell...), noise, Curtis' four poles, the filter's envelope and the level's, POLY-MOD (- the filter's envelope on A's
 * pitch: the sync's sweep; + B on A's frequency: FM). Its eight knobs (k: 0..1, a bipolar knob from its middle): OSC,
 * CUTOFF, RESO, DECAY, VINTAGE, ENV (- with an attack, + its depth), POLY-MOD, CHORD (POLY's) ---- */
void pro_init(void);
void pro_reset(int t);
void pro_silence(int t);
void pro_set(int t, const float *notes, int n, const float *k);   /* (its notes: poly_notes, CHORD its eighth knob) */
void pro_gate(int t, int g);
void pro_render(int t, float *out, uint32_t n);
int pro_voices(int t);
const char *pro_knob_name(int k);
void pro_defaults(float *k);
int pro_knob_steps(int k);               /* OSC's 13, CHORD's 15, the others 128 */
void pro_knob_text(int k, float u, char *b, int n);

/* ---- the tracks' filters (mi_filter.cc): LADDER (Moog's, D'Angelo and Valimaki's model), MS-20 (Korg 35, its
 * resonance driven), SEM (Oberheim's state variable, noise into it), VOX (three formants); each with a vactrol
 * envelope, a ping, FM from its own output, key tracking ---- */
#define MF_TRACKS 4
enum { MF_OFF, MF_LADDER, MF_MS20, MF_SEM, MF_VOX, MF_MODELS };
enum { MF_LP, MF_BP, MF_HP, MF_TYPES };  /* (VOX: low, mid, high voices) */
#define MF_MID MF_BP
/* the knobs, as the engine keeps them (three pages of four): TYPE the type (0..2) */
enum { MF_CUTOFF, MF_RESO, MF_ENV, MF_DECAY, MF_DRIVE, MF_PING, MF_FM, MF_KEY, MF_TYPE, MF_NOISE, MF_COLOR, MF_ATTACK,
       MF_KNOBS };
void mf_init(void);
/* k: MF_KNOBS values (CUTOFF, RESO, DECAY, DRIVE, PING, KEY, NOISE, ATTACK 0..1; ENV, FM, COLOR -1..1; TYPE: the
 * type argument's); note: MIDI (KEY's) */
void mf_set(int t, int model, int type, const float *k, float note);
void mf_trigger(int t, uint32_t at);     /* a trig at that sample of the next mf_process */
void mf_process(int t, float *x, uint32_t n);   /* in place; nothing when OFF */
void mf_reset(int t);
void mf_guard(int level);                /* the CPU guard: its FM's coefficients every sample (0), every 2nd (1), every
                                          * 4th (2), none (3: the FM off) */
void mf_stop(int t);                     /* the transport stopped: its envelope closed, faded out (~23 ms), then silent
                                          * (a self-oscillation, a ring, its noise) until its next trig */
float mf_cutoff_hz(int t);               /* (the screen) the cutoff now, all modulations in */
float mf_env(int t);                     /* its envelope now, 0..1 */
const char *mf_model_name(int m);
const char *mf_type_name(int m, int type);

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
