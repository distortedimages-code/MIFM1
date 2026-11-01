// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's sound (mi/engine.h): four Plaits voices, played by Marbles or by a step sequencer, two Tides as modulators,
// a matrix of cables, a mixer. Only bridge.h's C functions reach the modules: no module's header meets another's here.
//
// One audio block (128 frames, 2.9 ms) is one control step:
//   1. Marbles and Tides take their knobs as the cables left them at the last block;
//   2. Marbles runs 8 frames (16 audio frames each: its gates land within 0.4 ms), Tides one;
//   3. the sources are read, and every parameter is its knob plus its cables;
//   4. the trigs, frame by frame: Marbles' gates, or the steps on the tempo's sixteenths; each trig takes its locks
//      and its randoms (over the knobs, until the next trig), its probability and its ratchets; the keyboard plays
//      over, a new key a new trig; a note is put in the global scale;
//   5. each voice plays the block, cut where its gate changes (a rising gate first takes its trig's note);
//      the voices and Warps shared between the two cores by what each costs (measured, block by block);
//   6. the mixer: level and pan per track (the trig's), the sends, a soft limit on the sum; the gains glide, sample
//      by sample (a gain that jumps at a block's edge clicks).
#include <stdint.h>
#include <stddef.h>
extern "C" {
#include "plat.h"
#include "fastmath.h"
}
#include "bridge.h"
#include "engine.h"

namespace {

// ---------------------------------------------------------------- parameters ---
const char* const kGateNames[NGATE] = {"T1", "T2", "T3", "A1", "A2", "A3", "A4", "B1", "B2", "B3", "B4", "Keys"};
const char* const kNoteNames[NNOTE] = {"X1", "X2", "X3", "Y", "Fixed"};
const char* const kTidesOut[4] = {"Gates", "Amplitude", "Phase", "Frequency"};
const char* const kTidesRange[3] = {"Slow", "Medium", "Fast"};
const char* const kTidesTrig[5] = {"Free", "T1", "T2", "T3", "Clock"};
const char* const kTRange[3] = {"/4", "x1", "x4"};
const char* const kOnOff[2] = {"Off", "On"};
const char* const kXMode[3] = {"Identical", "Bump", "Tilt"};
const char* const kXRange[3] = {"+2 V", "+5 V", "+/-5 V"};
const char* const kSeqNames[2] = {"Marbles", "Steps"};
const char* const kScaleNames[15] = {"Off", "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
                                     "Locrian", "Harm. minor", "Mel. minor", "Maj. penta", "Min. penta", "Blues",
                                     "Whole tone", "Hirajoshi"};
const uint16_t kScaleMasks[15] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B, 0x9AD, 0xAAD, 0x295, 0x4A9,
                                  0x4E9, 0x555, 0x18D};   // bit i: i semitones over the root
const char* const kNoteName[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kArpRates[6] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4"};
const float kArpMult[6] = {0.5f, 2.0f / 3.0f, 1.0f, 4.0f / 3.0f, 2.0f, 4.0f};   // in sixteenths
const char* const kArpDir[4] = {"Forward", "Backward", "Ping-pong", "Random"};
const char* const kWarpsCarrier[8] = {"Off", "T1", "T2", "T3", "T4", "Osc 1", "Osc 2", "Osc 3"};
const char* const kTrackNames[4] = {"T1", "T2", "T3", "T4"};
const char* const kWarpsMode[2] = {"Warps", "Freq shift"};

// the names are the modules' own, as their panels print them (sentence case: Mutable's)
const eng_pdef_t kPages[NPT][4] = {
  {{"Frequency", K_BIP, 0, 0, 0}, {"Harmonics", K_UNI, 0, 0, 0}, {"Timbre", K_UNI, 0, 0, 0}, {"Morph", K_UNI, 0, 0, 0}},
  {{"Decay", K_UNI, 0, 0, 0}, {"LPG colour", K_UNI, 0, 0, 0}, {"Timbre env", K_BIP, 0, 0, 0},
   {"Morph env", K_BIP, 0, 0, 0}},
  {{"Gate", K_ENUM, 0, NGATE - 1, kGateNames}, {"Note", K_ENUM, 0, NNOTE - 1, kNoteNames},
   {"Transpose", K_INT, -12, 12, 0}, {"Octave", K_INT, -3, 3, 0}},
  {{"Frequency", K_UNI, 0, 0, 0}, {"Shape", K_UNI, 0, 0, 0}, {"Slope", K_UNI, 0, 0, 0}, {"Smoothness", K_UNI, 0, 0, 0}},
  {{"Shift/level", K_UNI, 0, 0, 0}, {"Outputs", K_ENUM, 0, 3, kTidesOut}, {"Range", K_ENUM, 0, 2, kTidesRange},
   {"Trigger", K_ENUM, 0, 4, kTidesTrig}},
  {{"Rate", K_UNI, 0, 0, 0}, {"Bias", K_UNI, 0, 0, 0}, {"Jitter", K_UNI, 0, 0, 0}, {"Deja vu", K_UNI, 0, 0, 0}},
  {{"Length", K_INT, 1, 16, 0}, {"t range", K_ENUM, 0, 2, kTRange}, {"Deja vu t", K_ENUM, 0, 1, kOnOff},
   {"Deja vu X", K_ENUM, 0, 1, kOnOff}},
  {{"Spread", K_UNI, 0, 0, 0}, {"Bias", K_UNI, 0, 0, 0}, {"Steps", K_UNI, 0, 0, 0}, {"X mode", K_ENUM, 0, 2, kXMode}},
  {{"X range", K_ENUM, 0, 2, kXRange}, {"Y spread", K_UNI, 0, 0, 0}, {"Y bias", K_UNI, 0, 0, 0},
   {"Y steps", K_UNI, 0, 0, 0}},
  {{"Level 1", K_UNI, 0, 0, 0}, {"Level 2", K_UNI, 0, 0, 0}, {"Level 3", K_UNI, 0, 0, 0}, {"Level 4", K_UNI, 0, 0, 0}},
  {{"Send 1", K_UNI, 0, 0, 0}, {"Send 2", K_UNI, 0, 0, 0}, {"Send 3", K_UNI, 0, 0, 0}, {"Send 4", K_UNI, 0, 0, 0}},
  {{"Time", K_UNI, 0, 0, 0}, {"Damping", K_UNI, 0, 0, 0}, {"Diffusion", K_UNI, 0, 0, 0}, {"Return", K_UNI, 0, 0, 0}},
  {{"Pan 1", K_BIP, 0, 0, 0}, {"Pan 2", K_BIP, 0, 0, 0}, {"Pan 3", K_BIP, 0, 0, 0}, {"Pan 4", K_BIP, 0, 0, 0}},
  {{"Note", K_INT, -24, 24, 0}, {"Prob", K_UNI, 0, 0, 0}, {"Ratchet", K_INT, 1, 8, 0}, {"Length", K_INT, 1, 64, 0}},
  {{"Sequencer", K_ENUM, 0, 1, kSeqNames}, {"Tempo", K_UNI, 0, 0, 0}, {"Scale", K_ENUM, 0, 14, kScaleNames},
   {"Root", K_ENUM, 0, 11, kNoteName}},
  {{"Rate", K_ENUM, 0, 5, kArpRates}, {"Gate", K_UNI, 0, 0, 0}, {"Direction", K_ENUM, 0, 3, kArpDir},
   {"Transpose", K_INT, -24, 24, 0}},
  {{"Algorithm", K_UNI, 0, 0, 0}, {"Timbre", K_UNI, 0, 0, 0}, {"Level 1", K_UNI, 0, 0, 0}, {"Level 2", K_UNI, 0, 0, 0}},
  {{"Carrier", K_ENUM, 0, 7, kWarpsCarrier}, {"Modulator", K_ENUM, 0, 3, kTrackNames},
   {"Mod dry", K_ENUM, 0, 1, kOnOff}, {"Mode", K_ENUM, 0, 1, kWarpsMode}},
};

// Warps' algorithms as its panel names them (the ALGORITHM knob's eight marks: six cross-modulations, then the vocoder
// over the last quarter), then its easter egg
const char* const kWarpsNames[8] = {"CROSSFADE", "CROSS-FOLD", "DIODE RING", "DIGITAL RING", "XOR", "COMPARATOR",
                                    "VOCODER", "FREQ SHIFTER"};
const char* const kWarpsShort[7] = {"Xfade", "Fold", "Diode", "Digital", "Xor", "Compar", "Vocoder"};
const char* const kOscXmod[3] = {"Sine", "Triangle", "Saw"};        // modulator.cc: xmod_shape
const char* const kOscVocoder[3] = {"Saw", "Pulse", "Noise"};       // vocoder_shape
int warps_pos(float a) {                // the knob's nearest mark: 0..5, 6 the vocoder (from 0.6875, the transition's
  if (a >= 0.6875f) return 6;           // middle; modulator.cc: vocoder_amount)
  int i = static_cast<int>(a * 8.0f + 0.5f);
  return i < 0 ? 0 : i > 5 ? 5 : i;
}
float warps_shift_hz(float pot) {       // the frequency shifter's shift (modulator.cc: ProcessEasterEgg)
  float dir = pot >= 0.5f ? 1.0f : -1.0f, f = 2.0f * (pot >= 0.5f ? pot - 0.5f : 0.5f - pot);
  f = f <= 0.4f ? f * f * f * 62.5f : 4.0f * fm_exp2f(15.0f * (f - 0.4f));
  return f * dir;
}

const char* const kModels[24] = {
  "VA VCF", "PHASE DIST", "6-OP FM 1", "6-OP FM 2", "6-OP FM 3", "WAVE TERRAIN", "STRING MACHINE", "CHIPTUNE",
  "VIRTUAL ANALOG", "WAVESHAPER", "FM 2-OP", "FORMANT", "HARMONIC", "WAVETABLE", "CHORDS", "SPEECH",
  "SWARM", "NOISE", "PARTICLES", "STRING", "MODAL", "KICK", "SNARE", "HI-HAT",
};
const char* const kRamps[3] = {"AD", "LOOPING", "AR"};
const char* const kTModels[7] = {"COIN TOSS", "CLUSTERS", "DRUMS", "INDEPENDENT", "DIVIDER", "THREE STATES",
                                 "MARKOV"};
const char* const kScales[6] = {"MAJOR", "MINOR", "PENTATONIC", "PELOG", "RAAG BHAIRAV", "RAAG SHRI"};
const char* const kSrcNames[NSRC] = {"-", "A1", "A2", "A3", "A4", "B1", "B2", "B3", "B4", "X1", "X2", "X3", "Y",
                                     "T1", "T2", "T3"};

const float kTidesRatios[9] = {4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 16.0f, 1.0f / 32.0f, 1.0f / 64.0f};
const char* const kTidesRatioNames[9] = {"4 / beat", "2 / beat", "1 / beat", "1 / 2 beats", "1 / bar",
                                         "1 / 2 bars", "1 / 4 bars", "1 / 8 bars", "1 / 16 bars"};

int page_type(int page) {
  if (page < 12) return PT_PLAITS1 + page % 3;
  if (page < 16) return PT_TIDES1 + (page - 12) % 2;
  if (page < 20) return PT_MARB_T1 + (page - 16);
  if (page < PG_PAN) return PT_MIX + (page - 20);
  if (page == PG_PAN) return PT_PAN;
  if (page < PG_GLOBAL) return PT_STEP;
  if (page >= PG_WARPS(0)) return PT_WARPS1 + (page - PG_WARPS(0));
  return page == PG_GLOBAL ? PT_GLOBAL : PT_ARP;
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline int i_of(float v) { return static_cast<int>(v + 0.5f); }

float bpm_of(float v) { return 30.0f * fm_exp2f(v * 3.321928f); }          // 0..1: 30..300 BPM
float tides_hz(float v, int range) {                                          // 0..1: 0.02..20 Hz, the range / 8, x 8
  float hz = 0.02f * fm_exp2f(v * 9.965784f);
  return range == 0 ? hz * 0.125f : range == 2 ? hz * 8.0f : hz;
}

// ------------------------------------------------------------------ engine ---
const uint32_t kN = 128;                // app.h APP_BLOCK
const uint32_t kFrames = 8;             // Marbles' frames per block
const float kSr = 44100.0f;

project_t proj;
float fin[ENG_NPARAMS];                 // the parameters after the cables (the render writes, the screen reads)
float src[NSRC];

#ifndef ENG_ARENA_BYTES
#define ENG_ARENA_BYTES (262u * 1024u)        /* 4 Plaits 108 KB, Marbles 9, the reverb in floats 64, Warps 78 */
#endif
uint8_t arena[ENG_ARENA_BYTES] __attribute__((aligned(8))) PLAT_POOL;
void* voice[ENG_TRACKS];
void* marbles;
void* tides[2];
void* reverb;
void* warps;
int ready;

volatile int playing, use_core1 = 1;
volatile int key_note[ENG_TRACKS], key_on[ENG_TRACKS];

volatile int key_retrig[ENG_TRACKS], key_hold[ENG_TRACKS];   // a key pressed since the last block; its frames left

uint8_t mgates[kFrames * 3];
float mcv[kFrames * 4];
float tout[2][4];
int vgate[ENG_TRACKS];                  // each voice's gate as last set
float vnote[ENG_TRACKS];                // its note (latched at the gate's rise)
volatile uint32_t vrises[ENG_TRACKS];   // the trigs it played
uint32_t gates_now;

// the trigs: the step clock, then per track what its trig decided
int restart;                            // PLAY: the steps from the first, Marbles' loop count from 0
float step_clk;                         // samples into the sixteenth
int32_t tick;                           // sixteenths since PLAY
int cur_step[ENG_TRACKS];
float raw_left[ENG_TRACKS];             // a step's gate: its samples left
int raw_prev[ENG_TRACKS];
float since_rise[ENG_TRACKS], interval[ENG_TRACKS];   // Marbles' trigs: the time between two (for the ratchets)
uint32_t trig_count[ENG_TRACKS];
int rat_on[ENG_TRACKS], rat_n[ENG_TRACKS];
float rat_pos[ENG_TRACKS], rat_period[ENG_TRACKS];
float tdelta[ENG_TRACKS][ENG_NTP];      // the trig's locks and randoms, over the knobs, until the next trig
float tnote[ENG_TRACKS];                // its note
uint8_t fg[ENG_TRACKS][kFrames];        // the gates the voices play, frame by frame
float fnote[ENG_TRACKS][kFrames];       // the note a rise in that frame takes
uint32_t rng = 0x9E3779B9u;
float pan_s[ENG_TRACKS];                // each track's pan, glided
float lev_s[ENG_TRACKS], snd_s[ENG_TRACKS];   // its level and send, glided
float mix_gl[ENG_TRACKS], mix_gr[ENG_TRACKS], mix_send[ENG_TRACKS], mix_ret;   // the gains the last block ended on
// Endless: its clock, the note playing, ping-pong's way, recording
float e_clk[ENG_TRACKS];
int e_pos[ENG_TRACKS], e_dir[ENG_TRACKS], e_note[ENG_TRACKS];
volatile int e_rec[ENG_TRACKS];

float vbuf[ENG_TRACKS][kN];
// Warps, one block behind: it plays the last block's carrier and modulator (w_car, w_mod, copied
// once the voices are done) while the voices play this one; its output takes the place of a track's this block
float w_car[kN], w_mod[kN], w_out[kN];
volatile int w_job;                     // Warps runs in this block's job
int w_last = -1;                        // the routing w_car and w_mod were taken with: carrier | modulator << 4, -1
int w_dst = -1, w_mute = -1;            // the track its output replaces; the modulator's track, silenced (-1: none)
float vaux[ENG_TRACKS][kN];
float send[kN], wet_l[kN], wet_r[kN];
float meter[ENG_TRACKS + 1];
int8_t hist[NSRC][ENG_HIST];
uint32_t hist_at, hist_div;

volatile uint32_t load0, load1, peak, peak_hold_blocks, c1_ticks;
volatile int on_core1_last;

float sat(float x) {                    // a soft limit: tanh's shape up to |x| = 3, then flat
  x = clampf(x, -3.0f, 3.0f);
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

float src_value(int s) { return s > SRC_NONE && s < NSRC ? src[s] : 0.0f; }

void apply_cables() {
  for (int i = 0; i < ENG_NPARAMS; ++i) fin[i] = proj.val[i];
  for (int g = 0; g <= ENG_TRACKS; ++g) {
    for (int c = 0; c < ENG_CABLES; ++c) {
      const cable_t& k = proj.cable[g][c];
      if (!k.depth || k.src == SRC_NONE || k.src >= NSRC || k.dst >= ENG_NPARAMS) continue;
      const eng_pdef_t* d = eng_pdef(k.dst);
      if (d->kind != K_UNI && d->kind != K_BIP) continue;
      fin[k.dst] += static_cast<float>(k.depth) * 0.01f * src_value(k.src) * (d->kind == K_BIP ? 2.0f : 1.0f);
    }
  }
  for (int i = 0; i < ENG_NPARAMS; ++i) {
    const eng_pdef_t* d = eng_pdef(i);
    fin[i] = d->kind == K_BIP ? clampf(fin[i], -1.0f, 1.0f)
           : d->kind == K_UNI ? clampf(fin[i], 0.0f, 1.0f)
           : clampf(fin[i], static_cast<float>(d->lo), static_cast<float>(d->hi));
  }
}

inline float P(int page, int k) { return fin[page * 4 + k]; }
inline int Pi(int page, int k) { return static_cast<int>(fin[page * 4 + k] + (fin[page * 4 + k] < 0 ? -0.5f : 0.5f)); }
inline int iround(float v) { return static_cast<int>(v + (v < 0 ? -0.5f : 0.5f)); }

float clamp_param(int param, float v) {
  const eng_pdef_t* d = eng_pdef(param);
  return d->kind == K_BIP ? clampf(v, -1.0f, 1.0f) : d->kind == K_UNI ? clampf(v, 0.0f, 1.0f)
                                                    : clampf(v, static_cast<float>(d->lo), static_cast<float>(d->hi));
}
float to_u(int param, float v) {        // a value as 0..1 of its travel (a lock's byte)
  const eng_pdef_t* d = eng_pdef(param);
  if (d->kind == K_BIP) return (v + 1.0f) * 0.5f;
  if (d->kind == K_UNI) return v;
  return d->hi > d->lo ? (v - static_cast<float>(d->lo)) / static_cast<float>(d->hi - d->lo) : 0.0f;
}
float from_u(int param, float u) {
  const eng_pdef_t* d = eng_pdef(param);
  if (d->kind == K_BIP) return u * 2.0f - 1.0f;
  if (d->kind == K_UNI) return u;
  return static_cast<float>(d->lo + iround(u * static_cast<float>(d->hi - d->lo)));
}

// a track parameter as the trig plays it: its knob and cables, then the trig's lock and random
float E(int t, int tp) {
  int param = eng_track_param(t, tp);
  return clamp_param(param, fin[param] + tdelta[t][tp]);
}

uint32_t rand_u32() {                   // xorshift32
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
float rand01() { return static_cast<float>(rand_u32() >> 8) * (1.0f / 16777216.0f); }
float loop01(int t, int step, int tp) { // the looped random: the same for this track, step and parameter
  uint32_t h = (proj.seed[t] + 1u) * 2654435761u ^ static_cast<uint32_t>(step) * 40503u ^
               static_cast<uint32_t>(tp * 9973 + t * 7919);
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return static_cast<float>(h & 0xFFFFu) * (1.0f / 65535.0f);
}

int quantize(int n) {                   // the nearest note of the global scale (a tie: the lower)
  int sc = Pi(PG_GLOBAL, 2), root = Pi(PG_GLOBAL, 3);
  if (sc <= 0 || sc >= 15) return n;
  uint32_t m = kScaleMasks[sc];
  int rel = ((n - root) % 12 + 12) % 12;
  for (int d = 0; d <= 6; ++d) {
    if ((m >> ((rel - d + 12) % 12)) & 1u) return n - d;
    if ((m >> ((rel + d) % 12)) & 1u) return n + d;
  }
  return n;
}

int lock_find(int t, int step, int tp) {
  uint8_t ts = static_cast<uint8_t>(t << 6 | step);
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (proj.lock[i].tp == tp && proj.lock[i].ts == ts) return i;
  return -1;
}

void set_marbles() {
  mm_params_t p;
  p.t_model = proj.algo[PG_MARBLES(0)];
  p.t_range = Pi(PG_MARBLES(1), 1);
  // RATE as BPM: at x1, marbles' clock is 2 Hz x 2^(rate / 12), T2 a beat
  p.rate = 12.0f * fm_log2f(bpm_of(P(PG_MARBLES(0), 0)) / 120.0f);
  p.t_bias = P(PG_MARBLES(0), 1);
  p.jitter = P(PG_MARBLES(0), 2);
  p.deja_vu = P(PG_MARBLES(0), 3);
  p.length = Pi(PG_MARBLES(1), 0);
  p.t_deja_vu = Pi(PG_MARBLES(1), 2);
  p.x_deja_vu = Pi(PG_MARBLES(1), 3);
  p.spread = P(PG_MARBLES(2), 0);
  p.x_bias = P(PG_MARBLES(2), 1);
  p.steps = P(PG_MARBLES(2), 2);
  p.x_mode = Pi(PG_MARBLES(2), 3);
  p.scale = proj.algo[PG_MARBLES(2)];
  p.x_range = Pi(PG_MARBLES(3), 0);
  p.y_spread = P(PG_MARBLES(3), 1);
  p.y_bias = P(PG_MARBLES(3), 2);
  p.y_steps = P(PG_MARBLES(3), 3);
  mm_set(marbles, &p);
}

float x_norm(float v) {                 // X and Y as 0..1 of their range
  int r = Pi(PG_MARBLES(3), 0);
  return r == 0 ? clampf(v * 0.5f, 0.0f, 1.0f) : r == 1 ? clampf(v * 0.2f, 0.0f, 1.0f)
                                                        : clampf(v * 0.1f + 0.5f, 0.0f, 1.0f);
}

void run_tides(uint32_t n) {
  float block_hz = kSr / static_cast<float>(n);
  float beat_hz = bpm_of(P(PG_MARBLES(0), 0)) / 60.0f;
  for (int i = 0; i < 2; ++i) {
    int trig = Pi(PG_TIDES(i, 1), 3);
    float hz = tides_hz(P(PG_TIDES(i, 0), 0), Pi(PG_TIDES(i, 1), 2));
    if (trig == 4) {                    // HORLOGE: a ratio of the beat
      int r = static_cast<int>(P(PG_TIDES(i, 0), 0) * 8.999f);
      hz = beat_hz * kTidesRatios[r < 0 ? 0 : r > 8 ? 8 : r];
    }
    mt_set(tides[i], proj.algo[PG_TIDES(i, 0)], Pi(PG_TIDES(i, 1), 1), 0, hz, P(PG_TIDES(i, 0), 1),
           P(PG_TIDES(i, 0), 2), P(PG_TIDES(i, 0), 3), P(PG_TIDES(i, 1), 0));
    int g = 0;
    if (trig >= 1 && trig <= 3) {       // T1..T3: high if it was in any of the block's frames
      for (uint32_t k = 0; k < kFrames; ++k) g |= mgates[k * 3 + trig - 1];
    }
    mt_gate(tides[i], g);
    mt_render(tides[i], tout[i], 1, block_hz);
  }
}

void read_sources() {
  // Tides' outputs are volts: 0..8 V for AD and AR, +/-5 V for a loop (its gates 0 / 8 V): as -1..1
  for (int i = 0; i < 2; ++i) {
    float k = proj.algo[PG_TIDES(i, 0)] == 1 ? 0.2f : 0.125f;
    for (int c = 0; c < 4; ++c) src[(i ? SRC_B1 : SRC_A1) + c] = clampf(tout[i][c] * k, -1.0f, 1.0f);
  }
  const float* last = &mcv[(kFrames - 1) * 4];
  src[SRC_X1] = x_norm(last[0]);
  src[SRC_X2] = x_norm(last[1]);
  src[SRC_X3] = x_norm(last[2]);
  src[SRC_Y] = x_norm(last[3]);
  for (int t = 0; t < 3; ++t) src[SRC_T1 + t] = mgates[(kFrames - 1) * 3 + t] ? 1.0f : 0.0f;
}

// a track's gate from Marbles (or a Tides output) in Marbles' frame k
int marbles_gate(int t, int k) {
  int g = Pi(PG_TRACK(t, 2), 0);
  if (g <= GATE_T3) return mgates[k * 3 + g];
  if (g <= GATE_B4) return src[SRC_A1 + (g - GATE_A1)] > 0.5f;
  return 0;
}

float trig_note(int t, int k, int steps) {   // the note a trig takes: the step's, or Marbles' X or Y; its random tune
  float base = 48.0f + 12.0f * static_cast<float>(iround(E(t, TP_OCTAVE))) + static_cast<float>(iround(E(t, TP_TRANSPOSE)));
  if (steps) return static_cast<float>(quantize(iround(base + E(t, TP_NOTE))));
  int s = Pi(PG_TRACK(t, 2), 1);
  float v = s == NOTE_FIXED ? 0.0f : 12.0f * mcv[k * 4 + (s == NOTE_Y ? 3 : s)];
  return static_cast<float>(quantize(iround(base + v + tdelta[t][TP_NOTE])));
}

// a trig: its locks (STEPS) and randoms over the knobs, whether it plays (its probability), its ratchets
void new_trig(int t, int step, int steps) {
  for (int tp = 0; tp < ENG_NTP; ++tp) {
    tdelta[t][tp] = 0.0f;
    if (!eng_tp_lockable(tp)) continue;
    int param = eng_track_param(t, tp), i;
    const eng_pdef_t* d = eng_pdef(param);
    float base = proj.val[param], v = base;
    if (steps && (i = lock_find(t, step, tp)) >= 0) v = from_u(param, static_cast<float>(proj.lock[i].v) / 255.0f);
    int r = proj.rnd[t][tp];
    if (r) {                            // > 0: a new random at each trig; < 0: the same at each pass (a loop)
      float a = static_cast<float>(r < 0 ? -r : r) * 0.01f, x = r > 0 ? rand01() : loop01(t, step, tp);
      if (tp == TP_RATCHET) v += a * x * 7.0f;              // only more
      else if (tp == TP_NOTE) v += a * (x * 2.0f - 1.0f) * 12.0f;   // up to an octave either way
      else v += a * (x * 2.0f - 1.0f) * (d->kind == K_BIP ? 2.0f : d->kind == K_UNI ? 1.0f
                                                                    : static_cast<float>(d->hi - d->lo));
    }
    tdelta[t][tp] = v - base;
  }
  float prob = E(t, TP_PROB);
  rat_n[t] = static_cast<int>(clampf(static_cast<float>(iround(fin[eng_track_param(t, TP_RATCHET)] +
                                                                tdelta[t][TP_RATCHET])), 1.0f, 8.0f));
  rat_on[t] = prob >= 0.999f || rand01() < prob;
  rat_pos[t] = 0.0f;
}

// the trigs of one block, frame by frame: fg (the gates) and fnote (the notes) for the voices
void sequence(uint32_t n) {
  float fl = static_cast<float>(n / kFrames);
  int steps = Pi(PG_GLOBAL, 0) == SEQ_STEPS, loop = Pi(PG_MARBLES(1), 0);
  float step_len = kSr * 60.0f / (bpm_of(P(PG_MARBLES(0), 0)) * 4.0f);
  if (restart) {
    restart = 0;
    step_clk = step_len;
    tick = -1;
    for (int t = 0; t < ENG_TRACKS; ++t) {
      trig_count[t] = 0;
      e_clk[t] = 1e9f;
      e_pos[t] = -1;
      e_dir[t] = 1;
    }
  }
  int endless[ENG_TRACKS];
  float el[ENG_TRACKS];
  for (int t = 0; t < ENG_TRACKS; ++t) {
    endless[t] = proj.endless_len[t] > 0 && proj.endless_len[t] <= ENG_ENDLESS && !e_rec[t];
    int r = Pi(PG_ARP(t), 0);
    el[t] = step_len * kArpMult[r < 0 ? 0 : r > 5 ? 5 : r];
  }
  if (loop < 1) loop = 1;
  for (uint32_t k = 0; k < kFrames; ++k) {
    if (steps && playing && (step_clk += fl) >= step_len) {   // a sixteenth: each track's step, its trig
      step_clk -= step_len;
      if (step_clk >= step_len) step_clk = 0.0f;
      ++tick;
      for (int t = 0; t < ENG_TRACKS; ++t) {
        int len = Pi(PG_STEP(t), 3);
        cur_step[t] = static_cast<int>(tick % (len < 1 ? 1 : len));
        if (eng_trig(t, cur_step[t]) && !endless[t] && !e_rec[t]) raw_left[t] = step_len * 0.5f;
      }
    }
    for (int t = 0; t < ENG_TRACKS; ++t) {
      int raw, g;
      if (endless[t] && playing && (e_clk[t] += fl) >= el[t]) {   // Endless: its next note (or rest)
        int len = proj.endless_len[t], d = Pi(PG_ARP(t), 2);
        e_clk[t] = e_clk[t] >= 2.0f * el[t] ? 0.0f : e_clk[t] - el[t];
        if (d == 1) {
          e_pos[t] = e_pos[t] <= 0 ? len - 1 : e_pos[t] - 1;
        } else if (d == 2) {
          e_pos[t] += e_dir[t];
          if (e_pos[t] >= len) e_dir[t] = -1, e_pos[t] = len > 1 ? len - 2 : 0;
          if (e_pos[t] < 0) e_dir[t] = 1, e_pos[t] = len > 1 ? 1 : 0;
        } else if (d == 3) {
          e_pos[t] = static_cast<int>(rand_u32() % static_cast<uint32_t>(len));
        } else {
          e_pos[t] = (e_pos[t] + 1) % len;
        }
        int nt = proj.endless[t][e_pos[t]];
        if (nt != ENDLESS_REST) {
          raw_left[t] = fm_maxf(fl * 2.0f, el[t] * clampf(P(PG_ARP(t), 1), 0.05f, 1.0f) * 0.98f);
          e_note[t] = nt;
        }
      }
      if (e_rec[t]) {
        raw = 0;
      } else if (steps || endless[t]) {
        raw = playing && raw_left[t] > 0.0f;
        raw_left[t] -= fl;
      } else {
        raw = playing && marbles_gate(t, static_cast<int>(k));
      }
      since_rise[t] += fl;
      if (raw && !raw_prev[t]) {        // a trig
        if (since_rise[t] > 4.0f * fl && since_rise[t] < 4.0f * kSr) interval[t] = since_rise[t];
        since_rise[t] = 0.0f;
        int step = endless[t] ? e_pos[t] : steps ? cur_step[t]
                 : static_cast<int>(trig_count[t] % static_cast<uint32_t>(loop));
        ++trig_count[t];
        new_trig(t, step, steps && !endless[t]);
        rat_period[t] = (endless[t] ? el[t] : steps ? step_len : (interval[t] > 0.0f ? interval[t] : kSr * 0.25f)) /
                        static_cast<float>(rat_n[t]);
        if (endless[t])                 // its note, the key's transposition, the page's, the track's
          tnote[t] = static_cast<float>(quantize(e_note[t] + proj.endless_key[t] + Pi(PG_ARP(t), 3) +
                                                 iround(E(t, TP_TRANSPOSE)) + iround(tdelta[t][TP_NOTE])));
        else
          tnote[t] = trig_note(t, static_cast<int>(k), steps);
      }
      raw_prev[t] = raw;
      if (!rat_on[t]) {
        g = 0;
      } else if (rat_n[t] <= 1) {
        g = raw;
        if (!raw) rat_on[t] = 0;
      } else {                          // the ratchets: n gates in the step (or in the time between two trigs)
        int i = static_cast<int>(rat_pos[t] / rat_period[t]);
        g = i < rat_n[t] && rat_pos[t] - static_cast<float>(i) * rat_period[t] < rat_period[t] * 0.5f;
        rat_pos[t] += fl;
        if (i >= rat_n[t]) rat_on[t] = 0;
      }
      fnote[t][k] = tnote[t];
      if (key_on[t] || key_hold[t] > 0) {   // the keyboard plays over; a new key, a new trig
        g = !(key_retrig[t] && k == 0);
        fnote[t][k] = static_cast<float>(quantize(key_note[t] + iround(E(t, TP_TRANSPOSE))));
        if (key_hold[t] > 0) --key_hold[t];
      }
      fg[t][k] = static_cast<uint8_t>(g);
    }
  }
  for (int t = 0; t < ENG_TRACKS; ++t) key_retrig[t] = 0;
}

void set_voice(int t) {
  mp_set(voice[t], proj.algo[PG_TRACK(t, 0)], vnote[t] + 24.0f * E(t, 0), E(t, 1), E(t, 2), E(t, 3), E(t, 4), E(t, 5));
  mp_amounts(voice[t], 0.0f, E(t, 6), E(t, 7));
}

void render_voice(int t, uint32_t n) {
  uint32_t step = n / kFrames, pos = 0;
  for (uint32_t k = 0; k < kFrames; ++k) {
    int g = fg[t][k];
    if (g == vgate[t]) continue;
    uint32_t at = k * step;
    if (at > pos) {
      mp_render(voice[t], &vbuf[t][pos], &vaux[t][pos], at - pos);
      pos = at;
    }
    if (g) {
      vnote[t] = fnote[t][k];
      set_voice(t);
      ++vrises[t];
    }
    mp_gate(voice[t], g);
    vgate[t] = g;
  }
  if (n > pos) mp_render(voice[t], &vbuf[t][pos], &vaux[t][pos], n - pos);
}

volatile uint32_t job_n;
// the render's five tasks (the four voices, Warps), shared between the cores: each one timed where it ran, its cost
// averaged, and each block they are split so that the busier core has the least to do (a modal voice costs ten times
// a virtual analog one; Warps' vocoder as much as two voices)
const int kTasks = ENG_TRACKS + 1;
volatile uint32_t task_ticks[kTasks];
float task_cost[kTasks] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
float warps_chunk_cost = 1.0f;           // Warps' cost a Process call: a block makes 2 or 3 of them (mw_calls)
uint32_t warps_calls;

void run_task(int i, uint32_t n) {
  uint32_t q = plat_ticks24();
  if (i < ENG_TRACKS) render_voice(i, n);
  else mw_render(warps, w_car, w_mod, w_out, n);
  task_ticks[i] = plat_ticks24() - q;
}

void core1_job(uint32_t mask) {         // the second core's tasks (bit i: task i)
  uint32_t t0 = plat_ticks24();
  for (int i = 0; i < kTasks; ++i)
    if ((mask >> i) & 1u) run_task(i, job_n);
  c1_ticks = plat_ticks24() - t0;
}

uint32_t split_tasks(uint32_t todo) {   // the second core's share of todo: the busier core's least
  float cost[kTasks], best = 1e30f;
  uint32_t pick = 0;
  for (int i = 0; i < kTasks; ++i) cost[i] = (todo >> i) & 1u ? task_cost[i] : 0.0f;
  for (uint32_t m = 0; m < (1u << kTasks); ++m) {
    if (m & ~todo) continue;
    float a = 0.0f, b = 0.0f;
    for (int i = 0; i < kTasks; ++i) {
      if ((m >> i) & 1u) b += cost[i];
      else a += cost[i];
    }
    float worst = a > b ? a : b;
    if (worst < best - 0.5f) {           // (a tie: the first found, so the split stays put)
      best = worst;
      pick = m;
    }
  }
  return pick;
}

// Warps' knobs for this block, and whether it runs: a carrier (a track or its oscillator), its modulator a track
void set_warps() {
  int car = Pi(PG_WARPS(1), 0), mod = Pi(PG_WARPS(1), 1), shift = Pi(PG_WARPS(1), 3);
  mod = mod < 0 ? 0 : mod > 3 ? 3 : mod;
  int osc = car >= WC_OSC1 ? car - WC_OSC1 + 1 : 0;
  w_job = car > WC_OFF && car <= WC_OSC3 && w_last == (car | mod << 4);
  float level1 = P(PG_WARPS(0), 2);
  mw_set(warps, clampf(P(PG_WARPS(0), 0), 0.0f, 1.0f), clampf(P(PG_WARPS(0), 1), 0.0f, 1.0f),
         clampf(level1, 0.0f, 1.0f), clampf(P(PG_WARPS(0), 3), 0.0f, 1.0f), osc,
         12.0f + 60.0f * clampf(level1, 0.0f, 1.0f), shift);   // the oscillator's note: LEVEL 1, as on the module
}

// after the voices: this block's carrier and modulator kept for the next block's job, Warps' output (the last
// block's, warped) in its track's place, the modulator's track silenced unless heard dry
void route_warps(uint32_t n) {
  int car = Pi(PG_WARPS(1), 0), mod = Pi(PG_WARPS(1), 1);
  mod = mod < 0 ? 0 : mod > 3 ? 3 : mod;
  if (car <= WC_OFF || car > WC_OSC3) {
    w_last = w_dst = w_mute = -1;
    return;
  }
  int ct = car <= WC_T4 ? car - WC_T1 : -1, dst = ct >= 0 ? ct : mod, job = w_job;
  for (uint32_t i = 0; i < n; ++i) {
    float c = ct >= 0 ? vbuf[ct][i] : 0.0f, m = vbuf[mod][i];
    w_car[i] = c;
    w_mod[i] = m;
    vbuf[dst][i] = job ? w_out[i] : 0.0f;   // (the first block of a new patching: silence, Warps not yet run)
  }
  w_dst = dst;
  w_mute = mod != dst && !Pi(PG_WARPS(1), 2) ? mod : -1;
  if (w_mute >= 0)
    for (uint32_t i = 0; i < n; ++i) vbuf[w_mute][i] = 0.0f;
  w_last = car | mod << 4;
}

uint32_t pct10(uint32_t ticks, uint32_t n) {   // ticks as tenths of a percent of n frames' time
  float block_ticks = static_cast<float>(n) * (static_cast<float>(PLAT_TICKS_PER_US) * 1000000.0f / kSr);
  float p = static_cast<float>(ticks) * 1000.0f / block_ticks;
  return static_cast<uint32_t>(p > 65000.0f ? 65000.0f : p);
}

}  // namespace

extern "C" {

int eng_page_type(int page) { return page_type(page); }
int eng_page_track(int page) {
  if (page < 12) return page / 3;
  if (page >= PG_ARP(0) && page < PG_ARP(0) + ENG_TRACKS) return page - PG_ARP(0);
  return page >= PG_STEP(0) && page < PG_GLOBAL ? page - PG_STEP(0) : -1;
}

int eng_track_param(int t, int tp) {
  if (tp < 12) return PG_TRACK(t, tp / 4) * 4 + tp % 4;
  if (tp == TP_LEVEL) return PG_MIX * 4 + t;
  if (tp == TP_SEND) return PG_FX(0) * 4 + t;
  if (tp == TP_PAN) return PG_PAN * 4 + t;
  return PG_STEP(t) * 4 + (tp - TP_NOTE);
}

int eng_param_tp(int param, int* t) {
  int page = param / 4, k = param % 4, tr = -1, tp = -1;
  if (page < 12) {
    tr = page / 3;
    tp = (page % 3) * 4 + k;
  } else if (page == PG_MIX || page == PG_FX(0) || page == PG_PAN) {
    tr = k;
    tp = page == PG_MIX ? TP_LEVEL : page == PG_PAN ? TP_PAN : TP_SEND;
  } else if (page >= PG_STEP(0) && page < PG_GLOBAL && k < 3) {
    tr = page - PG_STEP(0);
    tp = TP_NOTE + k;
  }
  if (t) *t = tr;
  return tp;
}

int eng_tp_lockable(int tp) { return tp >= 0 && tp < ENG_NTP && tp != 8 && tp != 9; }   // not the gate's and note's
                                                                                         // sources

const eng_pdef_t* eng_pdef(int param) {
  if (param < 0 || param >= ENG_NPARAMS) param = 0;
  return &kPages[page_type(param / 4)][param % 4];
}

int eng_param_mod(int param) {
  int k = eng_pdef(param)->kind;
  return (k == K_UNI || k == K_BIP) && param / 4 != PG_GLOBAL;
}

static void put(char* b, int n, const char* s) {
  int i = 0;
  while (s[i] && i < n - 1) {
    b[i] = s[i];
    ++i;
  }
  b[i] = 0;
}

static void put_num(char* b, int n, int32_t v, int decimals, int sign) {   // v in units of 10^-decimals
  char t[16];
  int i = 0, neg = v < 0;
  uint32_t u = static_cast<uint32_t>(neg ? -v : v);
  for (int d = 0; d < decimals; ++d) {
    t[i++] = static_cast<char>('0' + u % 10u);
    u /= 10u;
  }
  if (decimals) t[i++] = '.';
  do {
    t[i++] = static_cast<char>('0' + u % 10u);
    u /= 10u;
  } while (u && i < 13);
  if (neg) t[i++] = '-';
  else if (sign && v) t[i++] = '+';
  int k = 0;
  while (i && k < n - 1) b[k++] = t[--i];
  b[k] = 0;
}

void eng_param_text(int param, float v, char* b, int n) {
  const eng_pdef_t* d = eng_pdef(param);
  int pt = page_type(param / 4), k = param % 4;
  if (pt == PT_WARPS2 && k == 0 && i_of(v) >= WC_OSC1) {   // the oscillator's shape, as the algorithm uses it
    int o = i_of(v) > WC_OSC3 ? 2 : i_of(v) - WC_OSC1;
    put(b, n, Pi(PG_WARPS(1), 3) ? kWarpsCarrier[WC_OSC1 + o]
                                 : fin[PG_WARPS(0) * 4] >= 0.6875f ? kOscVocoder[o] : kOscXmod[o]);
  } else if (d->kind == K_ENUM) {
    int i = static_cast<int>(v + 0.5f);
    put(b, n, d->names[i < d->lo ? d->lo : i > d->hi ? d->hi : i]);
  } else if (pt == PT_WARPS1 && k == 0 && Pi(PG_WARPS(1), 3)) {   // the frequency shifter: its shift
    float hz = warps_shift_hz(v), a = hz < 0 ? -hz : hz;
    if (a >= 1000.0f) {
      put_num(b, n, static_cast<int32_t>(hz * 0.01f + (hz < 0 ? -0.5f : 0.5f)), 1, 1);
      int l = 0;
      while (b[l]) ++l;
      put(b + l, n - l, "k");
    } else {
      put_num(b, n, static_cast<int32_t>(hz + (hz < 0 ? -0.5f : 0.5f)), 0, 1);
      int l = 0;
      while (b[l]) ++l;
      put(b + l, n - l, " Hz");
    }
  } else if (pt == PT_WARPS1 && k == 0) {
    put(b, n, kWarpsShort[warps_pos(v)]);
  } else if (pt == PT_WARPS1 && k == 2 && !Pi(PG_WARPS(1), 3) && Pi(PG_WARPS(1), 0) >= WC_OSC1) {
    int nt = static_cast<int>(12.0f + 60.0f * v + 0.5f), l = 0;   // LEVEL 1: the oscillator's note (C0..C5)
    put(b, n, kNoteName[nt % 12]);
    while (b[l]) ++l;
    put_num(b + l, n - l, nt / 12 - 1, 0, 0);
  } else if (pt == PT_STEP && k == 0) {  // the note, from C3
    int nt = 48 + static_cast<int>(v + (v < 0 ? -0.5f : 0.5f)), l = 0;
    put(b, n, kNoteName[nt % 12]);
    while (b[l]) ++l;
    put_num(b + l, n - l, nt / 12 - 1, 0, 0);
  } else if (pt == PT_STEP && k == 2) {
    put(b, n, "x");
    put_num(b + 1, n - 1, static_cast<int32_t>(v + 0.5f), 0, 0);
  } else if (d->kind == K_INT) {
    put_num(b, n, static_cast<int32_t>(v + (v < 0 ? -0.5f : 0.5f)), 0, d->lo < 0);
  } else if (pt == PT_PAN) {
    int32_t q = static_cast<int32_t>(v * 100.0f + (v < 0 ? -0.5f : 0.5f));
    if (!q) {
      put(b, n, "C");
    } else {
      put(b, n, q < 0 ? "L" : "R");
      put_num(b + 1, n - 1, q < 0 ? -q : q, 0, 0);
    }
  } else if ((pt == PT_STEP && k == 1) || (pt == PT_ARP && k == 1)) {
    put_num(b, n, static_cast<int32_t>(v * 100.0f + 0.5f), 0, 0);
    int l = 0;
    while (b[l]) ++l;
    put(b + l, n - l, "%");
  } else if ((pt == PT_MARB_T1 && k == 0) || (pt == PT_GLOBAL && k == 1)) {
    put_num(b, n, static_cast<int32_t>(bpm_of(v) + 0.5f), 0, 0);
    int l = 0;
    while (b[l]) ++l;
    put(b + l, n - l, " BPM");
  } else if (pt == PT_PLAITS1 && k == 0) {
    put_num(b, n, static_cast<int32_t>(v * 240.0f + (v < 0 ? -0.5f : 0.5f)), 1, 1);   // semitones, 1 decimal
  } else if (pt == PT_TIDES1 && k == 0 && Pi(PG_TIDES(param / 4 == PG_TIDES(1, 0) ? 1 : 0, 1), 3) == 4) {
    int r = static_cast<int>(v * 8.999f);
    put(b, n, kTidesRatioNames[r < 0 ? 0 : r > 8 ? 8 : r]);
  } else if (pt == PT_TIDES1 && k == 0) {
    int tide = param / 4 == PG_TIDES(1, 0) ? 1 : 0;
    float hz = tides_hz(v, static_cast<int>(proj.val[PG_TIDES(tide, 1) * 4 + 2] + 0.5f));
    put_num(b, n, static_cast<int32_t>(hz * 100.0f + 0.5f), 2, 0);
    int l = 0;
    while (b[l]) ++l;
    put(b + l, n - l, " Hz");
  } else if (d->kind == K_BIP) {
    put_num(b, n, static_cast<int32_t>(v * 100.0f + (v < 0 ? -0.5f : 0.5f)), 0, 1);
  } else {
    put_num(b, n, static_cast<int32_t>(v * 100.0f + 0.5f), 0, 0);
  }
}

const char* eng_src_name(int s) { return s >= 0 && s < NSRC ? kSrcNames[s] : "?"; }

int eng_algo_count(int page) {
  switch (page_type(page)) {
    case PT_PLAITS1: return 24;
    case PT_TIDES1: return 3;
    case PT_MARB_T1: return 7;
    case PT_MARB_X1: return 6;
    default: return 0;
  }
}

const char* eng_algo_name(int page, int a) {
  int n = eng_algo_count(page);
  if (!n) return "";
  a = a < 0 ? 0 : a >= n ? n - 1 : a;
  switch (page_type(page)) {
    case PT_PLAITS1: return kModels[a];
    case PT_TIDES1: return kRamps[a];
    case PT_MARB_T1: return kTModels[a];
    default: return kScales[a];
  }
}

project_t* eng_project(void) { return &proj; }

static void cable(project_t* p, int g, int c, int s, int dst, int depth) {
  p->cable[g][c].src = static_cast<uint8_t>(s);
  p->cable[g][c].dst = static_cast<uint8_t>(dst);
  p->cable[g][c].depth = static_cast<int8_t>(depth);
  p->cable[g][c].rsv = 0;
}

// Warps: off (no carrier), ready on the diode ring modulator, track 2 as the modulator
static void warps_defaults(project_t* p) {
  static const float kW[8] = {0.25f, 0.5f, 0.6f, 0.6f, static_cast<float>(WC_OFF), 1.0f, 0.0f, 0.0f};
  for (int k = 0; k < 8; ++k) p->val[PG_WARPS(k / 4) * 4 + k % 4] = kW[k];
}

void eng_defaults(project_t* p) {
  uint8_t* raw = reinterpret_cast<uint8_t*>(p);
  for (uint32_t i = 0; i < sizeof(project_t); ++i) raw[i] = 0;
  p->magic = PROJECT_MAGIC;
  p->version = PROJECT_VERSION;
  p->size = sizeof(project_t);
  static const float kTrack[3][4] = {{0.0f, 0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.0f, 0.0f}, {0, 0, 0, 0}};
  static const int kGate[4] = {GATE_T2, GATE_T1, GATE_T3, GATE_T3};
  static const int kNote[4] = {NOTE_FIXED, NOTE_X1, NOTE_X2, NOTE_X3};
  static const int kOct[4] = {-1, -2, 0, 0};
  static const int kModel[4] = {21, 8, 10, 20};   // kick, virtual analog, FM 2-op, modal
  static const float kSend[4] = {0.0f, 0.1f, 0.3f, 0.5f};   // the spec's Clouds sends, now the reverb's
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int pg = 0; pg < 3; ++pg)
      for (int k = 0; k < 4; ++k) p->val[PG_TRACK(t, pg) * 4 + k] = kTrack[pg][k];
    p->val[PG_TRACK(t, 2) * 4 + 0] = static_cast<float>(kGate[t]);
    p->val[PG_TRACK(t, 2) * 4 + 1] = static_cast<float>(kNote[t]);
    p->val[PG_TRACK(t, 2) * 4 + 3] = static_cast<float>(kOct[t]);
    p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(kModel[t]);
    p->val[PG_MIX * 4 + t] = 0.75f;
    p->val[PG_FX(0) * 4 + t] = kSend[t];
  }
  p->val[PG_TRACK(0, 1) * 4 + 0] = 0.4f;          // the kick: a short decay, its own colour
  p->val[PG_TRACK(1, 0) * 4 + 3] = 0.3f;          // the bass: MORPH low, opened by Tides A
  p->val[PG_TRACK(1, 1) * 4 + 0] = 0.35f;
  p->val[PG_TRACK(2, 1) * 4 + 0] = 0.45f;
  p->val[PG_TRACK(3, 1) * 4 + 0] = 0.7f;          // the resonator rings longer
  p->val[PG_MIX * 4 + 0] = 0.9f;
  p->val[PG_FX(1) * 4 + 0] = 0.6f;               // the reverb: a medium room, a little dark, its return at 60
  p->val[PG_FX(1) * 4 + 1] = 0.4f;
  p->val[PG_FX(1) * 4 + 2] = 0.6f;
  p->val[PG_FX(1) * 4 + 3] = 0.6f;
  // Tides A: AD from T1, Tides B: a loop on the clock, phases spread
  p->algo[PG_TIDES(0, 0)] = 0;
  p->algo[PG_TIDES(1, 0)] = 1;
  static const float kTides[2][8] = {{0.55f, 0.3f, 0.2f, 0.5f, 0.5f, 2.0f, 1.0f, 1.0f},
                                     {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 2.0f, 1.0f, 4.0f}};
  for (int i = 0; i < 2; ++i)
    for (int k = 0; k < 8; ++k) p->val[PG_TIDES(i, k / 4) * 4 + k % 4] = kTides[i][k];
  p->val[PG_TIDES(1, 0) * 4 + 0] = 4.5f / 9.0f;  // HORLOGE: one cycle a bar
  // Marbles: 120 BPM, DEJA VU at noon, 8 steps, C minor
  // (RATE 0.60206: 30 x 2^(0.60206 x 3.3219) = 120 BPM; LENGTH 8, x1, DEJA VU on T and X; X: narrow, Y smooth)
  static const float kMarbles[16] = {0.60206f, 0.5f, 0.0f, 0.5f,  8.0f, 1.0f, 1.0f, 1.0f,
                                     0.5f, 0.5f, 0.6f, 0.0f,      0.0f, 0.5f, 0.5f, 0.0f};
  for (int k = 0; k < 16; ++k) p->val[PG_MARBLES(k / 4) * 4 + k % 4] = kMarbles[k];
  p->algo[PG_MARBLES(0)] = 0;
  p->algo[PG_MARBLES(2)] = 1;
  // the cables of the spec's patch
  cable(p, 0, 0, SRC_Y, PG_TRACK(0, 0) * 4 + 2, 20);    // track 1: Y -> TIMBRE, a little
  cable(p, 1, 0, SRC_A1, PG_TRACK(1, 0) * 4 + 3, 50);   // track 2: A1 -> MORPH (the AD envelope)
  cable(p, 2, 0, SRC_B1, PG_TRACK(2, 0) * 4 + 2, 40);   // track 3: B1 -> TIMBRE (the loop)
  cable(p, 3, 0, SRC_Y, PG_TRACK(3, 0) * 4 + 1, 40);    // track 4: Y -> HARMONICS (structure)
  cable(p, 3, 1, SRC_B2, PG_TRACK(3, 0) * 4 + 2, 30);   // and B2 -> TIMBRE (brightness)
  // the step sequencer (SEQUENCER on STEPS): a pattern a track, a few locks; the global scale: C minor
  for (int t = 0; t < ENG_TRACKS; ++t) {
    p->val[PG_PAN * 4 + t] = t == 2 ? -0.3f : t == 3 ? 0.35f : 0.0f;   // the kick and the bass in the middle
    p->val[PG_STEP(t) * 4 + 0] = 0.0f;
    p->val[PG_STEP(t) * 4 + 1] = 1.0f;
    p->val[PG_STEP(t) * 4 + 2] = 1.0f;
    p->val[PG_STEP(t) * 4 + 3] = 16.0f;
    p->val[PG_ARP(t) * 4 + 0] = 2.0f;                   // Endless: sixteenths, half gates, forward
    p->val[PG_ARP(t) * 4 + 1] = 0.5f;
    p->seed[t] = static_cast<uint16_t>(0x2A5 + 977 * t);
  }
  for (int i = 0; i < ENG_PLOCKS; ++i) p->lock[i].tp = 0xFF;
  warps_defaults(p);
  p->val[PG_GLOBAL * 4 + 0] = static_cast<float>(SEQ_MARBLES);
  p->val[PG_GLOBAL * 4 + 2] = 2.0f;                     // minor
  p->val[PG_GLOBAL * 4 + 3] = 0.0f;                     // C
  static const uint16_t kPat[4] = {0x1111, 0x6444, 0x1090, 0x4A49};   // bit s: step s
  for (int t = 0; t < ENG_TRACKS; ++t)
    for (int st = 0; st < 16; ++st)
      if ((kPat[t] >> st) & 1u) p->trig[t][st / 8] |= static_cast<uint8_t>(1u << (st % 8));
  static const struct { uint8_t t, st, tp; float v; } kLocks[] = {
      {0, 12, TP_RATCHET, 2.0f}, {1, 14, TP_NOTE, 12.0f}, {1, 10, TP_NOTE, 7.0f}, {2, 7, TP_NOTE, 3.0f},
      {3, 9, TP_DECAY, 0.95f}};
  for (uint32_t i = 0; i < sizeof kLocks / sizeof kLocks[0]; ++i) {
    int param = eng_track_param(kLocks[i].t, kLocks[i].tp);
    p->lock[i].ts = static_cast<uint8_t>(kLocks[i].t << 6 | kLocks[i].st);
    p->lock[i].tp = kLocks[i].tp;
    p->lock[i].v = static_cast<uint8_t>(to_u(param, kLocks[i].v) * 255.0f + 0.5f);
  }
}

// INIT: four Plaits on VIRTUAL ANALOG (the first model of the classic bank), every knob at noon, played by t1, t2,
// t3 and t2 with X1, X2, X3 and Y; Marbles as the demo's but in C major; nothing patched, nothing sent to the reverb
void eng_blank(project_t* p) {
  eng_defaults(p);
  static const int kGate[4] = {GATE_T1, GATE_T2, GATE_T3, GATE_T2};
  static const int kNote[4] = {NOTE_X1, NOTE_X2, NOTE_X3, NOTE_Y};
  static const float kTrack[2][4] = {{0.0f, 0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.0f, 0.0f}};
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int pg = 0; pg < 2; ++pg)
      for (int k = 0; k < 4; ++k) p->val[PG_TRACK(t, pg) * 4 + k] = kTrack[pg][k];
    p->val[PG_TRACK(t, 2) * 4 + 0] = static_cast<float>(kGate[t]);
    p->val[PG_TRACK(t, 2) * 4 + 1] = static_cast<float>(kNote[t]);
    p->val[PG_TRACK(t, 2) * 4 + 2] = 0.0f;
    p->val[PG_TRACK(t, 2) * 4 + 3] = 0.0f;
    p->algo[PG_TRACK(t, 0)] = 8;                   // VIRTUAL ANALOG
    p->val[PG_MIX * 4 + t] = 0.75f;
    p->val[PG_FX(0) * 4 + t] = 0.0f;
    p->val[PG_PAN * 4 + t] = 0.0f;
  }
  p->algo[PG_MARBLES(2)] = 0;                      // major
  p->val[PG_GLOBAL * 4 + 2] = 1.0f;                // the global scale too
  for (int t = 0; t < ENG_TRACKS; ++t)
    for (int b = 0; b < ENG_STEPS / 8; ++b) p->trig[t][b] = 0;
  for (int i = 0; i < ENG_PLOCKS; ++i) p->lock[i].tp = 0xFF;
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) {
      p->cable[g][c].src = SRC_NONE;
      p->cable[g][c].dst = 0;
      p->cable[g][c].depth = 0;
    }
}

int eng_check(const project_t* p) {
  return p->magic == PROJECT_MAGIC && p->version == PROJECT_VERSION && p->size == sizeof(project_t);
}

// v0.1's project (version 2): its knobs, models and cables kept, the rest the demo's (the steps, no locks, no randoms)
struct project_v2 {
  uint32_t magic;
  uint16_t version, size;
  float val[92];
  uint8_t algo[23];
  uint8_t rsv[2];
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
};

// 0.1.3's (version 3): all of it kept, Endless empty
struct project_v3 {
  uint32_t magic;
  uint16_t version, size;
  float val[29 * 4];
  uint8_t algo[29];
  uint8_t rsv[3];
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_TRACKS][ENG_STEPS / 8];
  int8_t rnd[ENG_TRACKS][ENG_NTP];
  uint16_t seed[ENG_TRACKS];
  plock_t lock[ENG_PLOCKS];
};

// 0.1.4's to 0.1.9's (version 4): all of it kept, Warps off
struct project_v4 {
  uint32_t magic;
  uint16_t version, size;
  float val[33 * 4];
  uint8_t algo[33];
  uint8_t out_mode;
  uint8_t rsv[2];
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_TRACKS][ENG_STEPS / 8];
  int8_t rnd[ENG_TRACKS][ENG_NTP];
  uint16_t seed[ENG_TRACKS];
  plock_t lock[ENG_PLOCKS];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
};

int eng_upgrade(project_t* p, int bytes) {
  static union {
    project_v2 v2;
    project_v3 v3;
    project_v4 v4;
  } u;
  project_v2& old = u.v2;
  project_v3& v3 = u.v3;
  project_v4& v4 = u.v4;
  if (bytes == static_cast<int>(sizeof(project_t)) && eng_check(p)) return 1;
  if (bytes == static_cast<int>(sizeof(project_v4)) && p->magic == PROJECT_MAGIC && p->version == 4 &&
      p->size == sizeof(project_v4)) {
    const uint8_t* from = reinterpret_cast<const uint8_t*>(p);
    uint8_t* to = reinterpret_cast<uint8_t*>(&v4);
    for (uint32_t i = 0; i < sizeof v4; ++i) to[i] = from[i];
    eng_defaults(p);
    for (int i = 0; i < 33 * 4; ++i) p->val[i] = v4.val[i];
    for (int i = 0; i < 33; ++i) p->algo[i] = v4.algo[i];
    p->out_mode = v4.out_mode;
    for (int g = 0; g <= ENG_TRACKS; ++g)
      for (int c = 0; c < ENG_CABLES; ++c) p->cable[g][c] = v4.cable[g][c];
    for (int t = 0; t < ENG_TRACKS; ++t) {
      for (int i = 0; i < ENG_STEPS / 8; ++i) p->trig[t][i] = v4.trig[t][i];
      for (int i = 0; i < ENG_NTP; ++i) p->rnd[t][i] = v4.rnd[t][i];
      p->seed[t] = v4.seed[t];
      for (int i = 0; i < ENG_ENDLESS; ++i) p->endless[t][i] = v4.endless[t][i];
      p->endless_len[t] = v4.endless_len[t];
      p->endless_key[t] = v4.endless_key[t];
    }
    for (int i = 0; i < ENG_PLOCKS; ++i) p->lock[i] = v4.lock[i];
    return 1;
  }
  if (bytes == static_cast<int>(sizeof(project_v3)) && p->magic == PROJECT_MAGIC && p->version == 3 &&
      p->size == sizeof(project_v3)) {
    const uint8_t* from = reinterpret_cast<const uint8_t*>(p);
    uint8_t* to = reinterpret_cast<uint8_t*>(&v3);
    for (uint32_t i = 0; i < sizeof v3; ++i) to[i] = from[i];
    eng_defaults(p);
    for (int i = 0; i < 29 * 4; ++i) p->val[i] = v3.val[i];
    for (int i = 0; i < 29; ++i) p->algo[i] = v3.algo[i];
    for (int g = 0; g <= ENG_TRACKS; ++g)
      for (int c = 0; c < ENG_CABLES; ++c) p->cable[g][c] = v3.cable[g][c];
    for (int t = 0; t < ENG_TRACKS; ++t) {
      for (int i = 0; i < ENG_STEPS / 8; ++i) p->trig[t][i] = v3.trig[t][i];
      for (int i = 0; i < ENG_NTP; ++i) p->rnd[t][i] = v3.rnd[t][i];
      p->seed[t] = v3.seed[t];
    }
    for (int i = 0; i < ENG_PLOCKS; ++i) p->lock[i] = v3.lock[i];
    return 1;
  }
  if (bytes != static_cast<int>(sizeof(project_v2)) || p->magic != PROJECT_MAGIC || p->version != 2 ||
      p->size != sizeof(project_v2))
    return 0;
  const uint8_t* from = reinterpret_cast<const uint8_t*>(p);
  uint8_t* to = reinterpret_cast<uint8_t*>(&old);
  for (uint32_t i = 0; i < sizeof old; ++i) to[i] = from[i];
  eng_defaults(p);
  for (int i = 0; i < 92; ++i) p->val[i] = old.val[i];
  for (int i = 0; i < 23; ++i) p->algo[i] = old.algo[i];
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) p->cable[g][c] = old.cable[g][c];
  for (int i = 0; i < ENG_PLOCKS; ++i) p->lock[i].tp = 0xFF;
  p->val[PG_GLOBAL * 4 + 2] = static_cast<float>(old.algo[PG_MARBLES(2)] < 2 ? old.algo[PG_MARBLES(2)] + 1 : 0);
  return 1;
}

int eng_init(void) {
  uint32_t need = 0, a = 0;
  need = 4u * ((mp_size() + 7u) & ~7u) + ((mm_size() + 7u) & ~7u) + 2u * ((mt_size() + 7u) & ~7u) +
         ((mrv_size() + 7u) & ~7u) + ((mw_size() + 7u) & ~7u);
  if (need > sizeof(arena)) return -1;
  for (int t = 0; t < ENG_TRACKS; ++t) {
    voice[t] = arena + a;
    a += (mp_size() + 7u) & ~7u;
    mp_init(voice[t]);
    vgate[t] = 0;
    vnote[t] = 48.0f;
    key_on[t] = 0;
  }
  marbles = arena + a;
  a += (mm_size() + 7u) & ~7u;
  mm_init(marbles, 0x2A5u, kSr / static_cast<float>(kN / kFrames));
  for (int i = 0; i < 2; ++i) {
    tides[i] = arena + a;
    a += (mt_size() + 7u) & ~7u;
    mt_init(tides[i]);
  }
  reverb = arena + a;
  a += (mrv_size() + 7u) & ~7u;
  mrv_init(reverb);
  warps = arena + a;
  a += (mw_size() + 7u) & ~7u;
  mw_init(warps);
  if (!eng_check(&proj)) eng_defaults(&proj);
  for (uint32_t k = 0; k < kFrames * 3; ++k) mgates[k] = 0;
  for (uint32_t k = 0; k < kFrames * 4; ++k) mcv[k] = 0.0f;
  apply_cables();
  ready = 1;
  return 0;
}

void eng_play(int on) {
  if (on && !playing && ready) {
    mm_reset(marbles);
    restart = 1;
  }
  if (!on)
    for (int t = 0; t < ENG_TRACKS; ++t) {
      raw_left[t] = 0.0f;
      rat_on[t] = 0;
    }
  playing = on != 0;
}
int eng_playing(void) { return playing; }

void eng_key(int track, int note, int on) {
  if (track < 0 || track >= ENG_TRACKS) return;
  if (on) {
    key_note[track] = note;
    key_on[track] = 1;
    key_retrig[track] = 1;              // a new key: a new trig, even played legato
    key_hold[track] = static_cast<int>(kFrames) * 3;   // and at least three blocks, however short the press
  } else if (key_note[track] == note) {
    key_on[track] = 0;
  }
}

void eng_core1(int on) { use_core1 = on; }

void eng_render(float* l, float* r, uint32_t n) {
  if (!ready || n > kN || n < kFrames || !eng_check(&proj)) {
    for (uint32_t i = 0; i < n; ++i) l[i] = r[i] = 0.0f;
    return;
  }
  uint32_t t0 = plat_ticks24();
  // 1-2. the sequencer and the modulators, with the knobs and cables of the last block
  set_marbles();
  if (playing) {
    mm_render(marbles, mgates, mcv, kFrames);
  } else {
    for (uint32_t k = 0; k < kFrames * 3; ++k) mgates[k] = 0;
  }
  run_tides(n);
  // 3. the sources, then every parameter
  read_sources();
  apply_cables();
  // 4. the trigs
  sequence(n);
  for (int t = 0; t < ENG_TRACKS; ++t) set_voice(t);
  set_warps();
  // 4. the voices and Warps (the last block's), split between the cores
  job_n = n;
  uint32_t todo = w_job ? (1u << kTasks) - 1u : (1u << ENG_TRACKS) - 1u;
  if (w_job) {
    warps_calls = mw_calls(warps, n);
    task_cost[ENG_TRACKS] = warps_chunk_cost * static_cast<float>(warps_calls);
  }
  uint32_t mask = split_tasks(todo);
  int on_core1 = use_core1 && plat_cpu2_on() ? plat_cpu2_run(core1_job, mask) : 0;
  for (int i = 0; i < kTasks; ++i)
    if (((todo & ~mask) >> i) & 1u) run_task(i, n);
  uint32_t c0 = plat_ticks24() - t0, c1 = 0;
  if (on_core1) {
    if (plat_cpu2_wait() != 0) {        // given up meanwhile: the job was dropped, do it here
      on_core1 = 0;
      core1_job(mask);
    }
    c1 = c1_ticks;
  } else {
    core1_job(mask);
    c0 = plat_ticks24() - t0;
  }
  on_core1_last = on_core1;
  for (int i = 0; i < ENG_TRACKS; ++i)  // the costs: averaged over ~8 blocks
    task_cost[i] += (static_cast<float>(task_ticks[i]) - task_cost[i]) * 0.125f;
  if (w_job && warps_calls)
    warps_chunk_cost += (static_cast<float>(task_ticks[ENG_TRACKS]) / static_cast<float>(warps_calls) -
                         warps_chunk_cost) * 0.125f;
  route_warps(n);
  // 5. the mixer: the levels, the sends into Clouds' reverb (core 0, once the four voices are done), its return
  uint32_t g = 0;
  for (int t = 0; t < ENG_TRACKS; ++t) g |= vgate[t] ? 1u << t : 0u;
  gates_now = g;
  // the gains: each block's targets, glided (a trig's random pan, a lock on the level, a cable), and reached sample by
  // sample over the block from the last block's: a gain that jumps at a block's edge is a click on what plays
  float gain[ENG_TRACKS], gl[ENG_TRACKS], gr[ENG_TRACKS], sends[ENG_TRACKS];
  float dgl[ENG_TRACKS], dgr[ENG_TRACKS], dsend[ENG_TRACKS], inv = 1.0f / static_cast<float>(n);
  for (int t = 0; t < ENG_TRACKS; ++t) {
    // the pan: equal power (a track as loud wherever it sits: 0.71 a side in the middle, 1 on one side)
    pan_s[t] += (E(t, TP_PAN) - pan_s[t]) * 0.5f;
    lev_s[t] += (E(t, TP_LEVEL) - lev_s[t]) * 0.5f;
    snd_s[t] += (E(t, TP_SEND) - snd_s[t]) * 0.5f;
    float a = (clampf(pan_s[t], -1.0f, 1.0f) + 1.0f) * 0.785398f, level = lev_s[t];
    gain[t] = level * 0.7f;
    float tl = level * 0.85f * fm_cosf(a), tr = level * 0.85f * fm_sinf(a);
    float ts = snd_s[t] * gain[t];        // post-fader, before the pan (the reverb is stereo itself)
    gl[t] = mix_gl[t];
    gr[t] = mix_gr[t];
    sends[t] = mix_send[t];
    dgl[t] = (tl - gl[t]) * inv;
    dgr[t] = (tr - gr[t]) * inv;
    dsend[t] = (ts - sends[t]) * inv;
    mix_gl[t] = tl;
    mix_gr[t] = tr;
    mix_send[t] = ts;
  }
  mrv_set(reverb, P(PG_FX(1), 0), P(PG_FX(1), 1), P(PG_FX(1), 2));
  float ret = mix_ret, dret = (P(PG_FX(1), 3) * 1.6f - mix_ret) * inv, pk[ENG_TRACKS + 1] = {0, 0, 0, 0, 0};
  mix_ret = P(PG_FX(1), 3) * 1.6f;
  for (uint32_t i = 0; i < n; ++i) {
    float s = 0.0f;
    for (int t = 0; t < ENG_TRACKS; ++t) {
      sends[t] += dsend[t];
      s += sends[t] * vbuf[t][i];
      float a = fm_fabsf(vbuf[t][i] * gain[t]);
      if (a > pk[t]) pk[t] = a;
    }
    send[i] = s;
  }
  mrv_process(reverb, send, wet_l, wet_r, n);
  for (uint32_t i = 0; i < n; ++i) {
    float dl = 0.0f, dr = 0.0f;
    for (int t = 0; t < ENG_TRACKS; ++t) {
      gl[t] += dgl[t];
      gr[t] += dgr[t];
      dl += gl[t] * vbuf[t][i];
      dr += gr[t] * vbuf[t][i];
    }
    ret += dret;
    float wl = ret * wet_l[i], wr = ret * wet_r[i];
    float a = fm_fabsf(wl) > fm_fabsf(wr) ? fm_fabsf(wl) : fm_fabsf(wr);
    if (a > pk[ENG_TRACKS]) pk[ENG_TRACKS] = a;
    float ol = sat(dl + wl), orr = sat(dr + wr);
    if (proj.out_mode == 1) {           // the right inverted: a headset with a mic (a 4-pole plug in the 3-pole jack)
                                        // has its earpieces in series between left and right, they get left minus
                                        // right: the centre vanishes. Left plus right then reaches them: the whole mix
      orr = -orr;
    } else if (proj.out_mode == 2) {    // mono
      ol = orr = 0.5f * (ol + orr);
    }
    l[i] = ol;
    r[i] = orr;
  }
  for (int t = 0; t <= ENG_TRACKS; ++t)    // the meters: up at once, down slowly
    meter[t] = pk[t] > meter[t] ? pk[t] : meter[t] * 0.985f;
  if (++hist_div >= 4u) {                 // the sources' past, for the screen
    hist_div = 0;
    for (int s = 1; s < NSRC; ++s) {
      float v = src[s] * 127.0f;
      hist[s][hist_at] = static_cast<int8_t>(v > 127.0f ? 127 : v < -127.0f ? -127 : static_cast<int>(v));
    }
    hist_at = (hist_at + 1u) % ENG_HIST;
  }
  // the load: one-pole averages, the worst total held a second
  uint32_t tot = plat_ticks24() - t0;
  uint32_t p0 = pct10(c0, n), p1 = pct10(c1, n), pt = pct10(tot, n);
  load0 = (load0 * 15u + p0) / 16u;
  load1 = (load1 * 15u + p1) / 16u;
  if (pt > peak || ++peak_hold_blocks > 344u) {
    peak = pt;
    peak_hold_blocks = 0;
  }
}

float eng_final(int param) { return param >= 0 && param < ENG_NPARAMS ? fin[param] : 0.0f; }
float eng_src(int s) { return src_value(s); }
uint32_t eng_gates(void) { return gates_now; }
int eng_note(int t) { return t >= 0 && t < ENG_TRACKS ? static_cast<int>(vnote[t] + 0.5f) : 0; }
uint32_t eng_load(int core) { return core ? load1 : load0; }
uint32_t eng_peak(void) { return peak; }
float eng_meter(int t) { return t >= 0 && t <= ENG_TRACKS ? meter[t] : 0.0f; }
int eng_hist(int s, int i) {
  if (s <= SRC_NONE || s >= NSRC || i < 0 || i >= ENG_HIST) return 0;
  return hist[s][(hist_at + static_cast<uint32_t>(i)) % ENG_HIST];
}
int eng_core1_running(void) { return on_core1_last; }

int eng_warps_algo(void) {
  int car = Pi(PG_WARPS(1), 0);
  if (car <= WC_OFF || car > WC_OSC3) return -1;
  return Pi(PG_WARPS(1), 3) ? 7 : warps_pos(P(PG_WARPS(0), 0));
}
const char* eng_warps_name(int algo) { return algo >= 0 && algo < 8 ? kWarpsNames[algo] : "WARPS"; }
int eng_warps_track(void) { return w_dst; }

int eng_steps_mode(void) { return iround(proj.val[PG_GLOBAL * 4]) == SEQ_STEPS; }
int eng_trig(int t, int step) {
  if (t < 0 || t >= ENG_TRACKS || step < 0 || step >= ENG_STEPS) return 0;
  return (proj.trig[t][step / 8] >> (step % 8)) & 1;
}
void eng_trig_set(int t, int step, int on) {
  if (t < 0 || t >= ENG_TRACKS || step < 0 || step >= ENG_STEPS) return;
  if (on) proj.trig[t][step / 8] = static_cast<uint8_t>(proj.trig[t][step / 8] | (1u << (step % 8)));
  else proj.trig[t][step / 8] = static_cast<uint8_t>(proj.trig[t][step / 8] & ~(1u << (step % 8)));
}
int eng_lock(int t, int step, int tp, float* v) {
  int i = lock_find(t, step, tp);
  if (i < 0) return 0;
  if (v) *v = from_u(eng_track_param(t, tp), static_cast<float>(proj.lock[i].v) / 255.0f);
  return 1;
}
int eng_lock_set(int t, int step, int tp, float v) {   // the value first, the slot's names last (the render reads it)
  int i = lock_find(t, step, tp);
  uint8_t u = static_cast<uint8_t>(clampf(to_u(eng_track_param(t, tp), v), 0.0f, 1.0f) * 255.0f + 0.5f);
  if (i < 0)
    for (int j = 0; j < ENG_PLOCKS && i < 0; ++j)
      if (proj.lock[j].tp == 0xFF) i = j;
  if (i < 0) return -1;
  proj.lock[i].v = u;
  proj.lock[i].ts = static_cast<uint8_t>(t << 6 | step);
  proj.lock[i].tp = static_cast<uint8_t>(tp);
  return 0;
}
void eng_lock_clear(int t, int step) {
  uint8_t ts = static_cast<uint8_t>(t << 6 | step);
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (proj.lock[i].tp != 0xFF && proj.lock[i].ts == ts) proj.lock[i].tp = 0xFF;
}
int eng_step_locked(int t, int step) {
  uint8_t ts = static_cast<uint8_t>(t << 6 | step);
  int n = 0;
  for (int i = 0; i < ENG_PLOCKS; ++i) n += proj.lock[i].tp != 0xFF && proj.lock[i].ts == ts;
  return n;
}
int eng_step_now(int t) { return playing && eng_steps_mode() && t >= 0 && t < ENG_TRACKS && tick >= 0 ? cur_step[t] : -1; }
int eng_quantize(int note) { return quantize(note); }
uint32_t eng_rises(int t) { return t >= 0 && t < ENG_TRACKS ? vrises[t] : 0u; }

void eng_endless_rec(int t, int on) {
  if (t < 0 || t >= ENG_TRACKS) return;
  e_rec[t] = on != 0;
  if (!on) {
    raw_left[t] = 0.0f;
    e_clk[t] = 1e9f;                    // the first note at the next frame
    e_pos[t] = -1;
    e_dir[t] = 1;
  }
}
int eng_endless_add(int t, int note) {
  if (t < 0 || t >= ENG_TRACKS || proj.endless_len[t] >= ENG_ENDLESS) return -1;
  proj.endless[t][proj.endless_len[t]] = static_cast<uint8_t>(note == ENDLESS_REST ? ENDLESS_REST : note & 127);
  ++proj.endless_len[t];
  return 0;
}
void eng_endless_clear(int t) {
  if (t < 0 || t >= ENG_TRACKS) return;
  proj.endless_len[t] = 0;
  proj.endless_key[t] = 0;
}
int eng_endless_on(int t) {
  return t >= 0 && t < ENG_TRACKS && proj.endless_len[t] > 0 && !e_rec[t];
}
void eng_endless_key(int t, int note) {   // from its first note
  if (!eng_endless_on(t)) return;
  for (int i = 0; i < proj.endless_len[t]; ++i)
    if (proj.endless[t][i] != ENDLESS_REST) {
      int k = note - proj.endless[t][i];
      proj.endless_key[t] = static_cast<int8_t>(k < -48 ? -48 : k > 48 ? 48 : k);
      return;
    }
}
int eng_endless_pos(int t) { return eng_endless_on(t) && playing ? e_pos[t] : -1; }

}  // extern "C"
