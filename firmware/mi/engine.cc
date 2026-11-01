// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's sound (mi/engine.h): four Plaits voices played by a step sequencer (or the keys), two Tides as modulators,
// a matrix of cables, three effects (a delay, Clouds' reverb, Warps) with their own steps, a mixer. Only bridge.h's C
// functions reach the modules: no module's header meets another's here.
//
// One audio block (128 frames, 2.9 ms) is one control step:
//   1. Tides takes its knobs as the cables left them at the last block, its trigger the tracks' gates of the last
//      block, and runs one frame;
//   2. the sources are read, and every parameter is its knob plus its cables;
//   3. the trigs, frame by frame (8 a block), on the tempo's sixteenths (swung): each lane's step; a track's trig
//      takes its locks and its randoms (over the knobs, until the next trig), its probability, its ratchets, its
//      length; an effect's opens it (its locks and randoms the same); the keyboard plays over, a new key a new trig;
//      a note is put in the global scale;
//   4. each voice plays the block, cut where its gate changes (a rising gate first takes its trig's note); the voices
//      and Warps shared between the two cores by what each costs (measured, block by block);
//   5. the mixer: level and pan per track (the trig's), the sends into the delay and the reverb (each opened by its
//      lane), Warps in its track's place (crossfaded with it by its lane), a soft limit on the sum; the gains glide,
//      sample by sample (a gain that jumps at a block's edge clicks).
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
const char* const kTidesOut[4] = {"Gates", "Amplitude", "Phase", "Frequency"};
const char* const kTidesRange[3] = {"Slow", "Medium", "Fast"};
const char* const kTidesTrig[6] = {"Free", "T1", "T2", "T3", "T4", "Clock"};
const int kTidesClock = 5;
const char* const kOnOff[2] = {"Off", "On"};
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
// the delay's times: divisions of the tempo, shortest first
const int kDivs = 13;
const char* const kDivNames[kDivs] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D",
                                      "1/2", "1/2D", "1/1"};
const float kDivMult[kDivs] = {0.5f, 2.0f / 3.0f, 1.0f, 4.0f / 3.0f, 1.5f, 2.0f, 8.0f / 3.0f, 3.0f, 4.0f, 6.0f, 8.0f,
                               12.0f, 16.0f};   // in sixteenths
const char* const kDelayMode[2] = {"Mono", "Ping-pong"};

// the knobs' names: the modules' own, short (a word, 52 px at most on the screen: the knobs apart)
const eng_pdef_t kPages[NPT][4] = {
  {{"Freq", K_BIP, 0, 0, 0}, {"Harmo", K_UNI, 0, 0, 0}, {"Timbre", K_UNI, 0, 0, 0}, {"Morph", K_UNI, 0, 0, 0}},
  {{"Decay", K_UNI, 0, 0, 0}, {"Colour", K_UNI, 0, 0, 0}, {"T.Env", K_BIP, 0, 0, 0}, {"M.Env", K_BIP, 0, 0, 0}},
  {{"P.Env", K_BIP, 0, 0, 0}, {"Gate", K_UNI, 0, 0, 0}, {"Transp", K_INT, -12, 12, 0}, {"Octave", K_INT, -3, 3, 0}},
  {{"Freq", K_UNI, 0, 0, 0}, {"Shape", K_UNI, 0, 0, 0}, {"Slope", K_UNI, 0, 0, 0}, {"Smooth", K_UNI, 0, 0, 0}},
  {{"Shift", K_UNI, 0, 0, 0}, {"Output", K_ENUM, 0, 3, kTidesOut}, {"Range", K_ENUM, 0, 2, kTidesRange},
   {"Trig", K_ENUM, 0, 5, kTidesTrig}},
  {{"Level", K_UNI, 0, 0, 0}, {"Level", K_UNI, 0, 0, 0}, {"Level", K_UNI, 0, 0, 0}, {"Level", K_UNI, 0, 0, 0}},
  {{"Pan", K_BIP, 0, 0, 0}, {"Pan", K_BIP, 0, 0, 0}, {"Pan", K_BIP, 0, 0, 0}, {"Pan", K_BIP, 0, 0, 0}},
  {{"Time", K_UNI, 0, 0, 0}, {"Damp", K_UNI, 0, 0, 0}, {"Diffuse", K_UNI, 0, 0, 0}, {"Return", K_UNI, 0, 0, 0}},
  {{"Reverb", K_UNI, 0, 0, 0}, {"Reverb", K_UNI, 0, 0, 0}, {"Reverb", K_UNI, 0, 0, 0}, {"Reverb", K_UNI, 0, 0, 0}},
  {{"Time", K_ENUM, 0, kDivs - 1, kDivNames}, {"Fdbk", K_UNI, 0, 0, 0}, {"Filter", K_BIP, 0, 0, 0},
   {"Drive", K_UNI, 0, 0, 0}},
  {{"Reso", K_UNI, 0, 0, 0}, {"Mode", K_ENUM, 0, 1, kDelayMode}, {"Mod", K_UNI, 0, 0, 0}, {"Return", K_UNI, 0, 0, 0}},
  {{"Delay", K_UNI, 0, 0, 0}, {"Delay", K_UNI, 0, 0, 0}, {"Delay", K_UNI, 0, 0, 0}, {"Delay", K_UNI, 0, 0, 0}},
  {{"Note", K_INT, -24, 24, 0}, {"Prob", K_UNI, 0, 0, 0}, {"Ratch", K_INT, 1, 8, 0}, {"Length", K_INT, 1, 64, 0}},
  {{"Decay", K_UNI, 0, 0, 0}, {"Prob", K_UNI, 0, 0, 0}, {"Depth", K_UNI, 0, 0, 0}, {"Length", K_INT, 1, 64, 0}},
  {{"Tempo", K_UNI, 0, 0, 0}, {"Scale", K_ENUM, 0, 14, kScaleNames}, {"Root", K_ENUM, 0, 11, kNoteName},
   {"Swing", K_UNI, 0, 0, 0}},
  {{"Rate", K_ENUM, 0, 5, kArpRates}, {"Gate", K_UNI, 0, 0, 0}, {"Dir", K_ENUM, 0, 3, kArpDir},
   {"Transp", K_INT, -24, 24, 0}},
  {{"Algo", K_UNI, 0, 0, 0}, {"Timbre", K_UNI, 0, 0, 0}, {"Level 1", K_UNI, 0, 0, 0}, {"Level 2", K_UNI, 0, 0, 0}},
  {{"Carrier", K_ENUM, 0, 7, kWarpsCarrier}, {"Mod", K_ENUM, 0, 3, kTrackNames}, {"Dry", K_ENUM, 0, 1, kOnOff},
   {"Mode", K_ENUM, 0, 1, kWarpsMode}},
};

// an effect lane's parameters: its effect's knobs, then its step page's DECAY, PROB and DEPTH
const int kLaneKnobs[ENG_LANES] = {0, 0, 0, 0, 8, 4, 4};
inline int lane_page0(int lane) {        // its effect's first page
  return lane == LANE_DELAY ? PG_DELAY(0) : lane == LANE_REVERB ? PG_REVERB : PG_WARPS(0);
}

// Warps' algorithms: its six cross-modulations, as its panel names them, over the whole of ALGORITHM (the module's
// knob, from its first mark to its sixth: 0..0.625; the vocoder, the rest of its travel, is left out: alone it cost
// as much as the four voices), then its easter egg
const char* const kWarpsNames[7] = {"CROSSFADE", "CROSS-FOLD", "DIODE RING", "DIGITAL RING", "XOR", "COMPARATOR",
                                    "FREQ SHIFTER"};
const char* const kWarpsShort[6] = {"Xfade", "Fold", "Diode", "Digi", "Xor", "Comp"};
const char* const kOscXmod[3] = {"Sine", "Triangle", "Saw"};        // modulator.cc: xmod_shape
const float kWarpsTravel = 0.625f;      // ALGORITHM's 0..1 on the module's 0..0.625
int warps_pos(float a) {                // the knob's nearest mark: 0..5
  int i = static_cast<int>(a * 5.0f + 0.5f);
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
const char* const kSrcNames[NSRC] = {"-", "A1", "A2", "A3", "A4", "B1", "B2", "B3", "B4"};

const float kTidesRatios[9] = {4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 16.0f, 1.0f / 32.0f, 1.0f / 64.0f};
const char* const kTidesRatioNames[9] = {"4 / beat", "2 / beat", "1 / beat", "1 / 2 beats", "1 / bar",
                                         "1 / 2 bars", "1 / 4 bars", "1 / 8 bars", "1 / 16 bars"};

int page_type(int page) {
  if (page < 12) return PT_PLAITS1 + page % 3;
  if (page < 16) return PT_TIDES1 + (page - 12) % 2;
  if (page < PG_STEP(0)) {
    static const uint8_t kT[7] = {PT_MIX, PT_PAN, PT_REVERB, PT_RSEND, PT_DELAY1, PT_DELAY2, PT_DSEND};
    return kT[page - PG_MIX];
  }
  if (page < PG_STEP(LANE_DELAY)) return PT_STEP;
  if (page < PG_GLOBAL) return PT_LANE;
  if (page == PG_GLOBAL) return PT_GLOBAL;
  if (page < PG_WARPS(0)) return PT_ARP;
  return page == PG_WARPS(0) ? PT_WARPS1 : PT_WARPS2;
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline int i_of(float v) { return static_cast<int>(v + 0.5f); }

float bpm_of(float v) { return 30.0f * fm_exp2f(v * 3.321928f); }          // 0..1: 30..300 BPM
float tides_hz(float v, int range) {                                          // 0..1: 0.02..20 Hz, the range / 8, x 8
  float hz = 0.02f * fm_exp2f(v * 9.965784f);
  return range == 0 ? hz * 0.125f : range == 2 ? hz * 8.0f : hz;
}
float gate_frac(float v) { return 0.05f + 0.9f * v; }                         // LENGTH: 5..95 % of the sixteenth
float lane_tau(float v) { return 0.01f * fm_exp2f(v * 9.0f); }                // an effect lane's DECAY: 10 ms..5.1 s

// ------------------------------------------------------------------ engine ---
const uint32_t kN = 128;                // app.h APP_BLOCK
const uint32_t kFrames = 8;             // control frames per block (16 audio frames each: a trig lands within 0.4 ms)
const float kSr = 44100.0f;
const int kFx = ENG_LANES - ENG_TRACKS; // the effect lanes

project_t proj;
float fin[ENG_NPARAMS];                 // the parameters after the cables (the render writes, the screen reads)
float src[NSRC];

#ifndef ENG_ARENA_BYTES
#if UINTPTR_MAX > 0xFFFFFFFFu           /* a computer's 64-bit pointers: a little more */
#define ENG_ARENA_BYTES (292u * 1024u)
#else                                   /* the FM-1: 4 Plaits 100 KB, the reverb in floats 64, the delay 103, Warps 10,
                                         * Tides 0.6 (284528 bytes) */
#define ENG_ARENA_BYTES (280u * 1024u)
#endif
#endif
uint8_t arena[ENG_ARENA_BYTES] __attribute__((aligned(8))) PLAT_POOL;
void* voice[ENG_TRACKS];
void* tides[2];
void* reverb;
void* delay;
void* warps;
int ready;

volatile int playing, use_core1 = 1;
volatile int key_note[ENG_TRACKS], key_on[ENG_TRACKS];

volatile int key_retrig[ENG_TRACKS], key_hold[ENG_TRACKS];   // a key pressed since the last block; its frames left

float tout[2][4];
int vgate[ENG_TRACKS];                  // each voice's gate as last set
float vnote[ENG_TRACKS];                // its note (latched at the gate's rise)
volatile uint32_t vrises[ENG_TRACKS];   // the trigs it played
uint32_t gates_now;
int gate_seen[ENG_TRACKS];              // its gate was high in the last block (Tides' triggers)

// the trigs: the step clock, then per lane what its trig decided
int restart;                            // PLAY: the steps from the first
float step_clk;                         // samples into the sixteenth
int32_t tick;                           // sixteenths since PLAY
int cur_step[ENG_LANES];
float raw_left[ENG_TRACKS];             // a step's gate: its samples left
int last_g[ENG_TRACKS];                 // the gate of the last frame
int rat_on[ENG_TRACKS], rat_n[ENG_TRACKS];
float rat_pos[ENG_TRACKS], rat_period[ENG_TRACKS];
float tdelta[ENG_LANES][ENG_NTP];       // the trig's locks and randoms, over the knobs, until the next trig
float tnote[ENG_TRACKS];                // its note
uint8_t fg[ENG_TRACKS][kFrames];        // the gates the voices play, frame by frame
float fnote[ENG_TRACKS][kFrames];       // the note a rise in that frame takes
uint32_t rng = 0x9E3779B9u;
float pan_s[ENG_TRACKS];                // each track's pan, glided
float lev_s[ENG_TRACKS], snd_s[ENG_TRACKS], dsnd_s[ENG_TRACKS];   // its level and sends, glided
float mix_gl[ENG_TRACKS], mix_gr[ENG_TRACKS], mix_send[ENG_TRACKS], mix_dsend[ENG_TRACKS], mix_ret, mix_dret;
// the effect lanes: their envelope (1 at a trig, falling with DECAY), their opening (the last block's, to glide from)
float fx_env[kFx], fx_open[kFx] = {1.0f, 1.0f, 1.0f};
int fx_hit[kFx];
// Endless: its clock, the note playing, ping-pong's way, recording
float e_clk[ENG_TRACKS];
int e_pos[ENG_TRACKS], e_dir[ENG_TRACKS], e_note[ENG_TRACKS];
volatile int e_rec[ENG_TRACKS];

float vbuf[ENG_TRACKS][kN];
// Warps, one block behind: it plays the last block's carrier and modulator (w_car, w_mod, copied once the voices are
// done) while the voices play this one; its output takes the place of a track's this block, crossfaded with that
// track as it was (w_din, given with them; w_dry, as late as the output)
float w_car[kN], w_mod[kN], w_din[kN], w_out[kN], w_dry[kN];
// ALL undone at the next bar (eng_back_at_bar): what to put back, taken in the block the bar starts in
float back_val[ENG_NPARAMS];
uint8_t back_algo[ENG_NPAGES];
int8_t back_rnd[ENG_TRACKS][ENG_NTP];
volatile int back_pending;
volatile int w_job;                     // Warps runs in this block's job
int w_last = -1;                        // the routing w_car and w_mod were taken with: carrier | modulator << 4, -1
int w_dst = -1, w_mute = -1;            // the track its output replaces; the modulator's track, silenced (-1: none)
float vaux[ENG_TRACKS][kN];
float send[kN], dsend[kN], wet_l[kN], wet_r[kN], echo_l[kN], echo_r[kN];
float meter[ENG_TRACKS + 2];
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

// a lane parameter as the trig plays it: its knob and cables, then the trig's lock and random
float E(int lane, int tp) {
  int param = eng_track_param(lane, tp);
  return clamp_param(param, fin[param] + tdelta[lane][tp]);
}

uint32_t rand_u32() {                   // xorshift32
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
float rand01() { return static_cast<float>(rand_u32() >> 8) * (1.0f / 16777216.0f); }
float loop01(int lane, int step, int tp) {   // the looped random: the same for this lane, step and parameter
  uint32_t h = (proj.seed[lane] + 1u) * 2654435761u ^ static_cast<uint32_t>(step) * 40503u ^
               static_cast<uint32_t>(tp * 9973 + lane * 7919);
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return static_cast<float>(h & 0xFFFFu) * (1.0f / 65535.0f);
}

int quantize(int n) {                   // the nearest note of the global scale (a tie: the lower)
  int sc = Pi(PG_GLOBAL, 1), root = Pi(PG_GLOBAL, 2);
  if (sc <= 0 || sc >= 15) return n;
  uint32_t m = kScaleMasks[sc];
  int rel = ((n - root) % 12 + 12) % 12;
  for (int d = 0; d <= 6; ++d) {
    if ((m >> ((rel - d + 12) % 12)) & 1u) return n - d;
    if ((m >> ((rel + d) % 12)) & 1u) return n + d;
  }
  return n;
}

int lock_find(int lane, int step, int tp) {
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (proj.lock[i].lane == lane && proj.lock[i].step == step && proj.lock[i].tp == tp) return i;
  return -1;
}

float step_len_now() { return kSr * 60.0f / (bpm_of(P(PG_GLOBAL, 0)) * 4.0f); }
float six_len(float sl) {               // the sixteenth playing's length, swung: the on-beat's long, the off-beat's short
  float sw = 0.5f + 0.25f * P(PG_GLOBAL, 3);
  return 2.0f * sl * ((tick & 1) ? 1.0f - sw : sw);
}

void run_tides(uint32_t n) {
  float block_hz = kSr / static_cast<float>(n);
  float beat_hz = bpm_of(P(PG_GLOBAL, 0)) / 60.0f;
  for (int i = 0; i < 2; ++i) {
    int trig = Pi(PG_TIDES(i, 1), 3);
    float hz = tides_hz(P(PG_TIDES(i, 0), 0), Pi(PG_TIDES(i, 1), 2));
    if (trig == kTidesClock) {          // CLOCK: a ratio of the beat
      int r = static_cast<int>(P(PG_TIDES(i, 0), 0) * 8.999f);
      hz = beat_hz * kTidesRatios[r < 0 ? 0 : r > 8 ? 8 : r];
    }
    mt_set(tides[i], proj.algo[PG_TIDES(i, 0)], Pi(PG_TIDES(i, 1), 1), 0, hz, P(PG_TIDES(i, 0), 1),
           P(PG_TIDES(i, 0), 2), P(PG_TIDES(i, 0), 3), P(PG_TIDES(i, 1), 0));
    mt_gate(tides[i], trig >= 1 && trig <= ENG_TRACKS ? gate_seen[trig - 1] : 0);   // T1..T4: the track's gate
    mt_render(tides[i], tout[i], 1, block_hz);
  }
}

void read_sources() {
  // Tides' outputs are volts: 0..8 V for AD and AR, +/-5 V for a loop (its gates 0 / 8 V): as -1..1
  for (int i = 0; i < 2; ++i) {
    float k = proj.algo[PG_TIDES(i, 0)] == 1 ? 0.2f : 0.125f;
    for (int c = 0; c < 4; ++c) src[(i ? SRC_B1 : SRC_A1) + c] = clampf(tout[i][c] * k, -1.0f, 1.0f);
  }
}

// a trig: its locks and randoms over the knobs, whether it plays (its probability); a track's ratchets
void new_trig(int lane, int step, int locks) {
  int np = eng_lane_params(lane);
  for (int tp = 0; tp < ENG_NTP; ++tp) {
    tdelta[lane][tp] = 0.0f;
    if (tp >= np) continue;
    int param = eng_track_param(lane, tp), i;
    const eng_pdef_t* d = eng_pdef(param);
    float base = proj.val[param], v = base;
    if (locks && (i = lock_find(lane, step, tp)) >= 0)
      v = from_u(param, static_cast<float>(proj.lock[i].v) / 255.0f);
    int r = proj.rnd[lane][tp];
    if (r) {                            // > 0: a new random at each trig; < 0: the same at each pass (a loop)
      float a = static_cast<float>(r < 0 ? -r : r) * 0.01f, x = r > 0 ? rand01() : loop01(lane, step, tp);
      if (lane < ENG_TRACKS && tp == TP_RATCHET) v += a * x * 7.0f;               // only more
      else if (lane < ENG_TRACKS && tp == TP_NOTE) v += a * (x * 2.0f - 1.0f) * 12.0f;   // an octave either way
      else v += a * (x * 2.0f - 1.0f) * (d->kind == K_BIP ? 2.0f : d->kind == K_UNI ? 1.0f
                                                                  : static_cast<float>(d->hi - d->lo));
    }
    tdelta[lane][tp] = v - base;
  }
  if (lane >= ENG_TRACKS) {             // an effect: open, by its probability
    float prob = E(lane, kLaneKnobs[lane] + 1);
    if (prob >= 0.999f || rand01() < prob) fx_hit[lane - ENG_TRACKS] = 1;
    return;
  }
  float prob = E(lane, TP_PROB);
  rat_n[lane] = static_cast<int>(clampf(static_cast<float>(iround(fin[eng_track_param(lane, TP_RATCHET)] +
                                                                   tdelta[lane][TP_RATCHET])), 1.0f, 8.0f));
  rat_on[lane] = prob >= 0.999f || rand01() < prob;
  rat_pos[lane] = 0.0f;
}

// the trigs of one block, frame by frame: fg (the gates) and fnote (the notes) for the voices; the effects' trigs
void sequence(uint32_t n) {
  float fl = static_cast<float>(n / kFrames);
  float sl = step_len_now();
  if (restart) {
    restart = 0;
    step_clk = 1e9f;                    // the first sixteenth at once
    tick = -1;
    for (int t = 0; t < ENG_TRACKS; ++t) {
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
    el[t] = sl * kArpMult[r < 0 ? 0 : r > 5 ? 5 : r];
    gate_seen[t] = 0;
  }
  for (uint32_t k = 0; k < kFrames; ++k) {
    int fresh[ENG_TRACKS] = {0, 0, 0, 0};   // a step's trig in this frame
    if (playing && (step_clk += fl) >= six_len(sl)) {   // a sixteenth: each lane's step, its trig
      step_clk -= six_len(sl);
      ++tick;
      if (step_clk >= six_len(sl)) step_clk = 0.0f;
      for (int l = 0; l < ENG_LANES; ++l) {
        int len = Pi(PG_STEP(l), 3);
        cur_step[l] = static_cast<int>(tick % (len < 1 ? 1 : len));
        if (!eng_trig(l, cur_step[l])) continue;
        if (l >= ENG_TRACKS) new_trig(l, cur_step[l], 1);
        else if (!endless[l] && !e_rec[l]) fresh[l] = 1;
      }
    }
    for (int t = 0; t < ENG_TRACKS; ++t) {
      int raw, g, rise = 0;
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
          rise = 1;
        }
      }
      if (fresh[t]) {                   // a step's trig: its length is its own (a lock, a random)
        new_trig(t, cur_step[t], 1);
        raw_left[t] = fm_maxf(fl * 2.0f, sl * gate_frac(E(t, TP_LENGTH)));
        rat_period[t] = sl / static_cast<float>(rat_n[t]);
        tnote[t] = static_cast<float>(quantize(iround(48.0f + 12.0f * static_cast<float>(iround(E(t, TP_OCTAVE))) +
                                                      static_cast<float>(iround(E(t, TP_TRANSPOSE))) +
                                                      E(t, TP_NOTE))));
      } else if (rise) {                // Endless' note: its randoms, no locks
        new_trig(t, e_pos[t], 0);
        rat_period[t] = el[t] / static_cast<float>(rat_n[t]);
        tnote[t] = static_cast<float>(quantize(e_note[t] + proj.endless_key[t] + Pi(PG_ARP(t), 3) +
                                               iround(E(t, TP_TRANSPOSE)) + iround(tdelta[t][TP_NOTE])));
      }
      if (e_rec[t]) {
        raw = 0;
      } else {
        raw = playing && raw_left[t] > 0.0f;
        raw_left[t] -= fl;
      }
      if (!rat_on[t]) {
        g = 0;
      } else if (rat_n[t] <= 1) {
        g = raw;
        if (!raw) rat_on[t] = 0;
      } else {                          // the ratchets: n gates in the step
        int i = static_cast<int>(rat_pos[t] / rat_period[t]);
        g = i < rat_n[t] && rat_pos[t] - static_cast<float>(i) * rat_period[t] < rat_period[t] * 0.5f;
        rat_pos[t] += fl;
        if (i >= rat_n[t]) rat_on[t] = 0;
      }
      if ((fresh[t] || rise) && last_g[t]) g = 0;   // a trig on a gate still high (swung, legato): low for a frame
      fnote[t][k] = tnote[t];
      if (key_on[t] || key_hold[t] > 0) {   // the keyboard plays over; a new key, a new trig
        g = !(key_retrig[t] && k == 0);
        fnote[t][k] = static_cast<float>(quantize(key_note[t] + iround(E(t, TP_TRANSPOSE))));
        if (key_hold[t] > 0) --key_hold[t];
      }
      fg[t][k] = static_cast<uint8_t>(g);
      last_g[t] = g;
      gate_seen[t] |= g;
    }
  }
  for (int t = 0; t < ENG_TRACKS; ++t) key_retrig[t] = 0;
}

// the effects' openings this block: a trig opens its effect, DECAY closes it down to 1 - DEPTH; a lane without a trig
// (or the steps stopped) leaves it open
void fx_lanes(uint32_t n, float* open) {
  for (int f = 0; f < kFx; ++f) {
    int lane = ENG_TRACKS + f, kk = kLaneKnobs[lane];
    if (fx_hit[f]) {
      fx_env[f] = 1.0f;
      fx_hit[f] = 0;
    } else {
      float tau = lane_tau(E(lane, kk));
      fx_env[f] *= fm_exp2f(-1.442695f * static_cast<float>(n) / (kSr * tau));
      if (fx_env[f] < 1e-4f) fx_env[f] = 0.0f;
    }
    float depth = E(lane, kk + 2);
    open[f] = playing && eng_lane_has_trigs(lane) ? 1.0f - depth + depth * fx_env[f] : 1.0f;
  }
}

void set_voice(int t) {
  // the models whose MORPH is their decay (string, modal, the drums: their own envelope, no LPG) take DECAY too, around
  // its middle: the envelope page's DECAY, its locks and its random move every model's decay
  int m = proj.algo[PG_TRACK(t, 0)];
  float morph = E(t, 3);
  if (m >= 19) morph = clampf(morph + E(t, 4) - 0.5f, 0.0f, 1.0f);
  mp_set(voice[t], m, vnote[t] + 24.0f * E(t, 0), E(t, 1), E(t, 2), morph, E(t, 4), E(t, 5));
  mp_amounts(voice[t], E(t, TP_PITCHENV), E(t, 6), E(t, 7));   // the envelope on the pitch (Plaits' FM attenuverter)
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
// a virtual analog one)
const int kTasks = ENG_TRACKS + 1;
volatile uint32_t task_ticks[kTasks];
float task_cost[kTasks] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
float warps_chunk_cost = 1.0f;           // Warps' cost a Process call: a block makes 2 or 3 of them (mw_calls)
uint32_t warps_calls;

void run_task(int i, uint32_t n) {
  uint32_t q = plat_ticks24();
  if (i < ENG_TRACKS) render_voice(i, n);
  else mw_render(warps, w_car, w_mod, w_din, w_out, w_dry, n);
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

// Warps' knobs for this block (its lane's locks over them), and whether it runs: a carrier (a track or its
// oscillator), its modulator a track
void set_warps() {
  int car = Pi(PG_WARPS(1), 0), mod = Pi(PG_WARPS(1), 1), shift = Pi(PG_WARPS(1), 3);
  mod = mod < 0 ? 0 : mod > 3 ? 3 : mod;
  int osc = car >= WC_OSC1 ? car - WC_OSC1 + 1 : 0;
  w_job = car > WC_OFF && car <= WC_OSC3 && w_last == (car | mod << 4);
  float alg = E(LANE_WARPS, 0), level1 = E(LANE_WARPS, 2);
  mw_set(warps, shift ? alg : alg * kWarpsTravel, E(LANE_WARPS, 1), level1, E(LANE_WARPS, 3), osc,
         12.0f + 60.0f * level1, shift);   // the oscillator's note: LEVEL 1, as on the module
}

// after the voices: this block's carrier, modulator and the track Warps replaces, kept for the next block's job;
// Warps' output (the last block's, warped) in its track's place, crossfaded with that track (as late) by Warps' lane
// from o0 to o1 over the block; the modulator's track silenced as Warps opens, unless heard dry
void route_warps(uint32_t n, float o0, float o1) {
  int car = Pi(PG_WARPS(1), 0), mod = Pi(PG_WARPS(1), 1);
  mod = mod < 0 ? 0 : mod > 3 ? 3 : mod;
  if (car <= WC_OFF || car > WC_OSC3) {
    w_last = w_dst = w_mute = -1;
    return;
  }
  int ct = car <= WC_T4 ? car - WC_T1 : -1, dst = ct >= 0 ? ct : mod, job = w_job;
  int mute = mod != dst && !Pi(PG_WARPS(1), 2) ? mod : -1;
  float o = o0, d = (o1 - o0) / static_cast<float>(n);
  for (uint32_t i = 0; i < n; ++i) {
    float x = vbuf[dst][i];
    w_car[i] = ct >= 0 ? vbuf[ct][i] : 0.0f;
    w_mod[i] = vbuf[mod][i];
    w_din[i] = x;
    o += d;
    vbuf[dst][i] = job ? w_dry[i] + o * (w_out[i] - w_dry[i]) : 0.0f;   // (the first block of a new patching:
    if (mute >= 0) vbuf[mute][i] *= 1.0f - o;                          // silence, Warps not yet run)
  }
  w_dst = dst;
  w_mute = mute;
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
  return page >= PG_STEP(0) && page < PG_STEP(ENG_TRACKS) ? page - PG_STEP(0) : -1;
}
int eng_page_lane(int page) {
  int t = eng_page_track(page);
  if (t >= 0) return t;
  if (page == PG_DELAY(0) || page == PG_DELAY(1) || page == PG_DSEND) return LANE_DELAY;
  if (page == PG_REVERB || page == PG_RSEND) return LANE_REVERB;
  if (page == PG_WARPS(0) || page == PG_WARPS(1)) return LANE_WARPS;
  if (page >= PG_STEP(ENG_TRACKS) && page < PG_STEP(ENG_LANES)) return page - PG_STEP(0);
  return -1;
}

int eng_lane_params(int lane) {
  if (lane < 0 || lane >= ENG_LANES) return 0;
  return lane < ENG_TRACKS ? ENG_NTP : kLaneKnobs[lane] + 3;
}

int eng_track_param(int lane, int tp) {
  if (lane < 0 || lane >= ENG_LANES || tp < 0 || tp >= eng_lane_params(lane)) return 0;
  if (lane >= ENG_TRACKS) {
    int kk = kLaneKnobs[lane];
    if (tp >= kk) return PG_STEP(lane) * 4 + (tp - kk);
    return lane_page0(lane) * 4 + tp;   // (the delay's two pages follow each other)
  }
  if (tp < 12) return PG_TRACK(lane, tp / 4) * 4 + tp % 4;
  if (tp == TP_LEVEL) return PG_MIX * 4 + lane;
  if (tp == TP_SEND) return PG_RSEND * 4 + lane;
  if (tp == TP_PAN) return PG_PAN * 4 + lane;
  if (tp == TP_DSEND) return PG_DSEND * 4 + lane;
  return PG_STEP(lane) * 4 + (tp - TP_NOTE);
}

int eng_param_tp(int param, int* lane) {
  int page = param / 4, k = param % 4, l = -1, tp = -1;
  if (param < 0 || param >= ENG_NPARAMS) {
  } else if (page < 12) {
    l = page / 3;
    tp = (page % 3) * 4 + k;
  } else if (page == PG_MIX || page == PG_RSEND || page == PG_PAN || page == PG_DSEND) {
    l = k;
    tp = page == PG_MIX ? TP_LEVEL : page == PG_PAN ? TP_PAN : page == PG_RSEND ? TP_SEND : TP_DSEND;
  } else if (page >= PG_STEP(0) && page < PG_STEP(ENG_TRACKS)) {
    if (k < 3) l = page - PG_STEP(0), tp = TP_NOTE + k;
  } else if (page >= PG_STEP(ENG_TRACKS) && page < PG_STEP(ENG_LANES)) {
    if (k < 3) l = page - PG_STEP(0), tp = kLaneKnobs[l] + k;
  } else if (page == PG_DELAY(0) || page == PG_DELAY(1)) {
    l = LANE_DELAY;
    tp = (page - PG_DELAY(0)) * 4 + k;
  } else if (page == PG_REVERB) {
    l = LANE_REVERB;
    tp = k;
  } else if (page == PG_WARPS(0)) {
    l = LANE_WARPS;
    tp = k;
  }
  if (lane) *lane = l;
  return tp;
}

int eng_tp_lockable(int lane, int tp) { return tp >= 0 && tp < eng_lane_params(lane); }

// Plaits' HARMONICS, TIMBRE and MORPH by model, after its manual (1.2's eight from its release notes), short
const char* const kModelKnobs[24][3] = {
  {"Reso", "Cutoff", "Wave"}, {"Dist F", "Amount", "Asym"}, {"Preset", "Mod", "Time"}, {"Preset", "Mod", "Time"},
  {"Preset", "Mod", "Time"}, {"Terrain", "Radius", "Offset"}, {"Chord", "Chorus", "Wave"}, {"Chord", "Arp", "PW"},
  {"Detune", "Square", "Saw"}, {"Shape", "Fold", "Asym"}, {"Ratio", "Index", "Fdbk"}, {"Ratio", "Form", "Width"},
  {"Bumps", "Peak", "Shape"}, {"Bank", "Row", "Column"}, {"Chord", "Invert", "Wave"}, {"Synth", "Species", "Vowel"},
  {"Detune", "Density", "Grain"}, {"Filter", "Clock", "Reso"}, {"Random", "Density", "Filter"},
  {"Struct", "Bright", "Decay"}, {"Struct", "Bright", "Decay"}, {"Attack", "Bright", "Decay"},
  {"Noise", "Tone", "Decay"}, {"Metal", "HPF", "Decay"},
};

const char* eng_param_name(int param) {
  if (param >= 0 && param < 12 * 4 && (param / 4) % 3 == 0 && param % 4 >= 1) {
    int m = proj.algo[param / 4];
    return kModelKnobs[m < 0 || m > 23 ? 0 : m][param % 4 - 1];
  }
  return eng_pdef(param)->name;
}

int eng_model_enveloped(int m) { return (m >= 2 && m <= 4) || m >= 19; }

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

static void put_end(char* b, int n, const char* s) {   // s after what b holds
  int l = 0;
  while (b[l]) ++l;
  put(b + l, n - l, s);
}

void eng_param_text(int param, float v, char* b, int n) {
  const eng_pdef_t* d = eng_pdef(param);
  int pt = page_type(param / 4), k = param % 4;
  if (pt == PT_WARPS2 && k == 0 && i_of(v) >= WC_OSC1) {   // the oscillator's shape, as the algorithm uses it
    int o = i_of(v) > WC_OSC3 ? 2 : i_of(v) - WC_OSC1;
    put(b, n, Pi(PG_WARPS(1), 3) ? kWarpsCarrier[WC_OSC1 + o] : kOscXmod[o]);
  } else if (d->kind == K_ENUM) {
    int i = static_cast<int>(v + 0.5f);
    put(b, n, d->names[i < d->lo ? d->lo : i > d->hi ? d->hi : i]);
  } else if (pt == PT_WARPS1 && k == 0 && Pi(PG_WARPS(1), 3)) {   // the frequency shifter: its shift
    float hz = warps_shift_hz(v), a = hz < 0 ? -hz : hz;
    if (a >= 1000.0f) {
      put_num(b, n, static_cast<int32_t>(hz * 0.01f + (hz < 0 ? -0.5f : 0.5f)), 1, 1);
      put_end(b, n, "k");
    } else {
      put_num(b, n, static_cast<int32_t>(hz + (hz < 0 ? -0.5f : 0.5f)), 0, 1);
      put_end(b, n, " Hz");
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
  } else if (pt == PT_DELAY1 && k == 2) {   // the filter: low pass to the left, high pass to the right
    int32_t q = static_cast<int32_t>(v * 100.0f + (v < 0 ? -0.5f : 0.5f));
    if (q == 0) {
      put(b, n, "Flat");
    } else {
      put(b, n, q < 0 ? "LP " : "HP ");
      put_num(b + 3, n - 3, q < 0 ? -q : q, 0, 0);
    }
  } else if (pt == PT_LANE && k == 0) {  // an effect's DECAY: its time
    float s = lane_tau(v);
    if (s < 1.0f) {
      put_num(b, n, static_cast<int32_t>(s * 1000.0f + 0.5f), 0, 0);
      put_end(b, n, " ms");
    } else {
      put_num(b, n, static_cast<int32_t>(s * 10.0f + 0.5f), 1, 0);
      put_end(b, n, " s");
    }
  } else if (pt == PT_PLAITS3 && k == 1) {   // LENGTH: the gate, in % of the sixteenth
    put_num(b, n, static_cast<int32_t>(gate_frac(v) * 100.0f + 0.5f), 0, 0);
    put_end(b, n, "%");
  } else if (pt == PT_GLOBAL && k == 3) {    // SWING: the on-beat's share of two sixteenths
    put_num(b, n, static_cast<int32_t>(50.0f + 25.0f * v + 0.5f), 0, 0);
    put_end(b, n, "%");
  } else if ((pt == PT_STEP && k == 1) || (pt == PT_ARP && k == 1) || (pt == PT_LANE && (k == 1 || k == 2))) {
    put_num(b, n, static_cast<int32_t>(v * 100.0f + 0.5f), 0, 0);
    put_end(b, n, "%");
  } else if (pt == PT_GLOBAL && k == 0) {
    put_num(b, n, static_cast<int32_t>(bpm_of(v) + 0.5f), 0, 0);
    put_end(b, n, " BPM");
  } else if (pt == PT_PLAITS1 && k == 0) {
    put_num(b, n, static_cast<int32_t>(v * 240.0f + (v < 0 ? -0.5f : 0.5f)), 1, 1);   // semitones, 1 decimal
  } else if (pt == PT_TIDES1 && k == 0 && Pi(PG_TIDES(param / 4 == PG_TIDES(1, 0) ? 1 : 0, 1), 3) == kTidesClock) {
    int r = static_cast<int>(v * 8.999f);
    put(b, n, kTidesRatioNames[r < 0 ? 0 : r > 8 ? 8 : r]);
  } else if (pt == PT_TIDES1 && k == 0) {
    int tide = param / 4 == PG_TIDES(1, 0) ? 1 : 0;
    float hz = tides_hz(v, static_cast<int>(proj.val[PG_TIDES(tide, 1) * 4 + 2] + 0.5f));
    put_num(b, n, static_cast<int32_t>(hz * 100.0f + 0.5f), 2, 0);
    put_end(b, n, " Hz");
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
    default: return 0;
  }
}

const char* eng_algo_name(int page, int a) {
  int n = eng_algo_count(page);
  if (!n) return "";
  a = a < 0 ? 0 : a >= n ? n - 1 : a;
  return page_type(page) == PT_PLAITS1 ? kModels[a] : kRamps[a];
}

project_t* eng_project(void) { return &proj; }

static void cable(project_t* p, int g, int c, int s, int dst, int depth) {
  p->cable[g][c].src = static_cast<uint8_t>(s);
  p->cable[g][c].dst = static_cast<uint8_t>(dst);
  p->cable[g][c].depth = static_cast<int8_t>(depth);
  p->cable[g][c].rsv = 0;
}

static void set_trigs(project_t* p, int lane, uint32_t bits) {   // bit s: step s (of the first 16)
  for (int st = 0; st < 16; ++st)
    if ((bits >> st) & 1u) p->trig[lane][st / 8] |= static_cast<uint8_t>(1u << (st % 8));
}

static void set_lock(project_t* p, int lane, int step, int tp, float v) {
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (p->lock[i].lane == 0xFF) {
      p->lock[i].lane = static_cast<uint8_t>(lane);
      p->lock[i].step = static_cast<uint8_t>(step);
      p->lock[i].tp = static_cast<uint8_t>(tp);
      p->lock[i].v = static_cast<uint8_t>(clampf(to_u(eng_track_param(lane, tp), v), 0.0f, 1.0f) * 255.0f + 0.5f);
      return;
    }
}

// INIT: four Plaits on VIRTUAL ANALOG (the first model of the classic bank), every knob at noon; no steps, nothing
// patched, nothing sent to the effects (the delay set to an eighth, ready), Warps off, C major, 120 BPM
void eng_blank(project_t* p) {
  uint8_t* raw = reinterpret_cast<uint8_t*>(p);
  for (uint32_t i = 0; i < sizeof(project_t); ++i) raw[i] = 0;
  p->magic = PROJECT_MAGIC;
  p->version = PROJECT_VERSION;
  p->size = sizeof(project_t);
  static const float kTrack[3][4] = {{0.0f, 0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f, 0.0f}};
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int pg = 0; pg < 3; ++pg)
      for (int k = 0; k < 4; ++k) p->val[PG_TRACK(t, pg) * 4 + k] = kTrack[pg][k];
    p->algo[PG_TRACK(t, 0)] = 8;                         // VIRTUAL ANALOG
    p->val[PG_MIX * 4 + t] = 0.75f;
    p->val[PG_STEP(t) * 4 + 1] = 1.0f;                   // NOTE 0, PROB 100 %, no ratchet, 16 steps
    p->val[PG_STEP(t) * 4 + 2] = 1.0f;
    p->val[PG_ARP(t) * 4 + 0] = 2.0f;                    // Endless: sixteenths, half gates, forward
    p->val[PG_ARP(t) * 4 + 1] = 0.5f;
  }
  for (int l = 0; l < ENG_LANES; ++l) {
    p->val[PG_STEP(l) * 4 + 3] = 16.0f;
    p->seed[l] = static_cast<uint16_t>(0x2A5 + 977 * l);
  }
  for (int l = ENG_TRACKS; l < ENG_LANES; ++l) {         // an effect's steps: DECAY 0.4 (~120 ms), PROB, DEPTH full
    p->val[PG_STEP(l) * 4 + 0] = 0.4f;
    p->val[PG_STEP(l) * 4 + 1] = 1.0f;
    p->val[PG_STEP(l) * 4 + 2] = 1.0f;
  }
  // Tides A: AD triggered by track 1, Tides B: a loop on the clock (one cycle a bar)
  p->algo[PG_TIDES(0, 0)] = 0;
  p->algo[PG_TIDES(1, 0)] = 1;
  static const float kTides[2][8] = {{0.55f, 0.3f, 0.2f, 0.5f, 0.5f, 2.0f, 1.0f, 1.0f},
                                     {4.5f / 9.0f, 0.5f, 0.5f, 0.5f, 0.5f, 2.0f, 1.0f, 5.0f}};
  for (int i = 0; i < 2; ++i)
    for (int k = 0; k < 8; ++k) p->val[PG_TIDES(i, k / 4) * 4 + k % 4] = kTides[i][k];
  // the reverb: a medium room, a little dark, its return at 60; the delay: an eighth, a few repeats, flat
  static const float kRev[4] = {0.6f, 0.4f, 0.6f, 0.6f};
  static const float kDelay[8] = {5.0f, 0.4f, 0.0f, 0.1f, 0.2f, 0.0f, 0.1f, 0.6f};
  for (int k = 0; k < 4; ++k) p->val[PG_REVERB * 4 + k] = kRev[k];
  for (int k = 0; k < 8; ++k) p->val[PG_DELAY(k / 4) * 4 + k % 4] = kDelay[k];
  // Warps: off (no carrier), ready on the diode ring modulator, track 2 as the modulator
  static const float kW[8] = {0.4f, 0.5f, 0.6f, 0.6f, static_cast<float>(WC_OFF), 1.0f, 0.0f, 0.0f};
  for (int k = 0; k < 8; ++k) p->val[PG_WARPS(k / 4) * 4 + k % 4] = kW[k];
  p->val[PG_GLOBAL * 4 + 0] = 0.60206f;                  // 30 x 2^(0.60206 x 3.3219) = 120 BPM
  p->val[PG_GLOBAL * 4 + 1] = 1.0f;                      // major
  for (int i = 0; i < ENG_PLOCKS; ++i) p->lock[i].lane = 0xFF;
}

// the demo: a kick, a bass whose MORPH Tides A opens at each of its notes, an FM line in a looped random tune thrown
// into the delay on two steps (its lane), a resonator (modal) in the reverb; C minor, 120 BPM
void eng_defaults(project_t* p) {
  eng_blank(p);
  static const int kModel[4] = {21, 8, 10, 20};          // kick, virtual analog, FM 2-op, modal
  static const int kOct[4] = {-1, -1, 0, 0};
  static const float kLevel[4] = {0.9f, 0.75f, 0.7f, 0.7f};
  static const float kPan[4] = {0.0f, 0.0f, -0.3f, 0.35f};
  static const float kRSend[4] = {0.0f, 0.1f, 0.25f, 0.5f};
  static const float kDSend[4] = {0.0f, 0.0f, 0.6f, 0.0f};
  static const float kDecay[4] = {0.4f, 0.35f, 0.45f, 0.7f};
  for (int t = 0; t < ENG_TRACKS; ++t) {
    p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(kModel[t]);
    p->val[PG_TRACK(t, 2) * 4 + 3] = static_cast<float>(kOct[t]);
    p->val[PG_TRACK(t, 1) * 4 + 0] = kDecay[t];
    p->val[PG_MIX * 4 + t] = kLevel[t];
    p->val[PG_PAN * 4 + t] = kPan[t];
    p->val[PG_RSEND * 4 + t] = kRSend[t];
    p->val[PG_DSEND * 4 + t] = kDSend[t];
  }
  p->val[PG_TRACK(1, 0) * 4 + 3] = 0.3f;                 // the bass: MORPH low, opened by Tides A
  p->val[PG_TRACK(1, 2) * 4 + 1] = 0.3f;                 // short notes
  p->val[PG_TRACK(2, 2) * 4 + 1] = 0.2f;
  p->val[PG_TIDES(0, 1) * 4 + 3] = 2.0f;                 // Tides A: triggered by T2
  p->rnd[2][TP_NOTE] = -40;                              // the FM line: a looped random tune
  p->rnd[3][TP_NOTE] = -60;                              // the resonator too, its decay new at each trig
  p->rnd[3][TP_DECAY] = 25;
  // the delay: a dotted eighth, ping-pong, a little dark and driven, a slow wow
  static const float kDelay[8] = {7.0f, 0.45f, -0.2f, 0.2f, 0.3f, 1.0f, 0.15f, 0.6f};
  for (int k = 0; k < 8; ++k) p->val[PG_DELAY(k / 4) * 4 + k % 4] = kDelay[k];
  p->val[PG_STEP(LANE_DELAY) * 4 + 0] = 0.45f;           // its lane: open on two of the line's notes, ~170 ms to
  set_trigs(p, LANE_DELAY, 0x1010);                      // close
  p->val[PG_GLOBAL * 4 + 1] = 2.0f;                      // minor
  cable(p, 1, 0, SRC_A1, PG_TRACK(1, 0) * 4 + 3, 50);    // track 2: A1 -> MORPH (the AD envelope)
  cable(p, 2, 0, SRC_B1, PG_TRACK(2, 0) * 4 + 2, 40);    // track 3: B1 -> TIMBRE (the loop)
  cable(p, 3, 0, SRC_B2, PG_TRACK(3, 0) * 4 + 2, 30);    // track 4: B2 -> TIMBRE (brightness)
  static const uint16_t kPat[4] = {0x1111, 0x6444, 0x1090, 0x4A49};   // bit s: step s
  for (int t = 0; t < ENG_TRACKS; ++t) set_trigs(p, t, kPat[t]);
  set_lock(p, 0, 12, TP_RATCHET, 2.0f);
  set_lock(p, 1, 10, TP_NOTE, 7.0f);
  set_lock(p, 1, 13, TP_NOTE, 10.0f);
  set_lock(p, 1, 14, TP_NOTE, 12.0f);
  set_lock(p, 2, 7, TP_NOTE, 3.0f);
  set_lock(p, 3, 9, TP_DECAY, 0.95f);
}

int eng_check(const project_t* p) {
  return p->magic == PROJECT_MAGIC && p->version == PROJECT_VERSION && p->size == sizeof(project_t);
}

}  // extern "C"

namespace {

// 0.1.4's to 0.2's projects (versions 4 and 5, 33 and 35 pages): with Marbles, three effects fewer
struct lock_v5 {
  uint8_t ts;                           // track << 6 | step
  uint8_t tp;                           // 0xFF: free
  uint8_t v, rsv;
};
template <int NP>
struct project_old {
  uint32_t magic;
  uint16_t version, size;
  float val[NP * 4];
  uint8_t algo[NP];
  uint8_t out_mode;
  uint8_t rsv[2];
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_TRACKS][ENG_STEPS / 8];
  int8_t rnd[ENG_TRACKS][18];
  uint16_t seed[ENG_TRACKS];
  lock_v5 lock[ENG_PLOCKS];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
};

// an old page's place now (-1: gone, Marbles' four)
int old_page(int op) {
  if (op < 16) return op;               // the tracks' Plaits, Tides
  if (op < 20) return -1;               // Marbles
  static const int8_t kMap[15] = {PG_MIX, PG_RSEND, PG_REVERB, PG_PAN, PG_STEP(0), PG_STEP(1), PG_STEP(2), PG_STEP(3),
                                  PG_GLOBAL, PG_ARP(0), PG_ARP(1), PG_ARP(2), PG_ARP(3), PG_WARPS(0), PG_WARPS(1)};
  return op - 20 < 15 ? kMap[op - 20] : -1;
}
int old_param(int op) {          // an old parameter's place now (-1: gone)
  int pg = old_page(op / 4), k = op % 4;
  if (pg < 0) return -1;
  if (pg == PG_GLOBAL) return k >= 2 ? PG_GLOBAL * 4 + k - 1 : -1;   // SEQUENCER and its TEMPO gone (Marbles' RATE was
  if (pg < 12 && pg % 3 == 2 && k < 2) return -1;                  // the tempo); a track's GATE and NOTE sources
  return pg * 4 + k;
}

template <int NP>
void upgrade_from(project_t* p, const project_old<NP>& o) {
  eng_blank(p);
  for (int i = 0; i < NP * 4; ++i) {
    int q = old_param(i);
    if (q >= 0) p->val[q] = o.val[i];
  }
  p->val[PG_GLOBAL * 4 + 0] = o.val[16 * 4 + 0];         // the tempo: Marbles' RATE
  for (int i = 0; i < 2; ++i) {                          // Tides' TRIGGER: Marbles' t gone, the clock moved
    float& tr = p->val[PG_TIDES(i, 1) * 4 + 3];
    tr = tr > 3.5f ? static_cast<float>(kTidesClock) : 0.0f;
  }
  for (int i = 0; i < 16; ++i) p->algo[i] = o.algo[i];
  p->out_mode = o.out_mode;
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) {
      cable_t k = o.cable[g][c];
      int dst = old_param(k.dst);
      if (k.depth && k.src > SRC_NONE && k.src < NSRC && dst >= 0) cable(p, g, c, k.src, dst, k.depth);
    }
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int i = 0; i < ENG_STEPS / 8; ++i) p->trig[t][i] = o.trig[t][i];
    for (int i = 0; i < 18; ++i) p->rnd[t][i] = i == 8 || i == 9 ? 0 : o.rnd[t][i];
    p->seed[t] = o.seed[t];
    for (int i = 0; i < ENG_ENDLESS; ++i) p->endless[t][i] = o.endless[t][i];
    p->endless_len[t] = o.endless_len[t];
    p->endless_key[t] = o.endless_key[t];
  }
  int j = 0;
  for (int i = 0; i < ENG_PLOCKS; ++i) {
    const lock_v5& l = o.lock[i];
    if (l.tp >= 18 || l.tp == 8 || l.tp == 9) continue;
    p->lock[j].lane = static_cast<uint8_t>(l.ts >> 6);
    p->lock[j].step = static_cast<uint8_t>(l.ts & 63u);
    p->lock[j].tp = l.tp;
    p->lock[j].v = l.v;
    ++j;
  }
}

}  // namespace

extern "C" {

int eng_upgrade(project_t* p, int bytes) {
  static union {
    project_old<33> v4;
    project_old<35> v5;
  } u;
  if (bytes == static_cast<int>(sizeof(project_t)) && eng_check(p)) return 1;
  int v = p->magic == PROJECT_MAGIC ? p->version : 0;
  uint32_t size = v == 5 ? sizeof u.v5 : v == 4 ? sizeof u.v4 : 0;
  if (!size || bytes != static_cast<int>(size) || p->size != size) return 0;
  const uint8_t* from = reinterpret_cast<const uint8_t*>(p);
  uint8_t* to = reinterpret_cast<uint8_t*>(&u);
  for (uint32_t i = 0; i < size; ++i) to[i] = from[i];
  if (v == 5) upgrade_from(p, u.v5);
  else upgrade_from(p, u.v4);
  return 1;
}

int eng_init(void) {
  uint32_t need = 0, a = 0;
  need = 4u * ((mp_size() + 7u) & ~7u) + 2u * ((mt_size() + 7u) & ~7u) + ((mrv_size() + 7u) & ~7u) +
         ((mw_size() + 7u) & ~7u) + ((mdl_size() + 7u) & ~7u);
  if (need > sizeof(arena)) return -1;
  for (int t = 0; t < ENG_TRACKS; ++t) {
    voice[t] = arena + a;
    a += (mp_size() + 7u) & ~7u;
    mp_init(voice[t]);
    vgate[t] = 0;
    vnote[t] = 48.0f;
    key_on[t] = 0;
  }
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
  delay = arena + a;
  a += (mdl_size() + 7u) & ~7u;
  mdl_init(delay);
  if (!eng_check(&proj)) eng_defaults(&proj);
  apply_cables();
  ready = 1;
  return 0;
}

void eng_play(int on) {
  if (on && !playing && ready) restart = 1;
  if (!on)
    for (int t = 0; t < ENG_TRACKS; ++t) {
      raw_left[t] = 0.0f;
      rat_on[t] = 0;
    }
  playing = on != 0;
}
int eng_playing(void) { return playing; }

void eng_back_at_bar(const float* val, const uint8_t* algo, const int8_t* rnd) {
  back_pending = 0;
  for (int i = 0; i < ENG_NPARAMS; ++i) back_val[i] = val[i];
  for (int i = 0; i < ENG_NPAGES; ++i) back_algo[i] = algo[i];
  for (int t = 0; t < ENG_TRACKS; ++t)
    for (int i = 0; i < ENG_NTP; ++i) back_rnd[t][i] = rnd[t * ENG_NTP + i];
  back_pending = 1;
}
int eng_back_pending(void) { return back_pending; }
void eng_back_cancel(void) { back_pending = 0; }

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
  if (back_pending) {                   // ALL undone: in the block whose next sixteenth starts a bar (or stopped)
    if (!playing || restart || ((tick + 1) % 16 == 0 && six_len(step_len_now()) - step_clk <= static_cast<float>(n))) {
      for (int i = 0; i < ENG_NPARAMS; ++i) proj.val[i] = back_val[i];
      for (int i = 0; i < ENG_NPAGES; ++i) proj.algo[i] = back_algo[i];
      for (int t = 0; t < ENG_TRACKS; ++t)
        for (int i = 0; i < ENG_NTP; ++i) proj.rnd[t][i] = back_rnd[t][i];
      back_pending = 0;
    }
  }
  // 1. the modulators, with the knobs and cables of the last block
  run_tides(n);
  // 2. the sources, then every parameter
  read_sources();
  apply_cables();
  // 3. the trigs
  sequence(n);
  float open[kFx];
  fx_lanes(n, open);
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
  const int fd = LANE_DELAY - ENG_TRACKS, fr = LANE_REVERB - ENG_TRACKS, fw = LANE_WARPS - ENG_TRACKS;
  route_warps(n, fx_open[fw], open[fw]);
  // 5. the mixer: the levels, the sends into the delay and Clouds' reverb (core 0, once the voices are done), their
  // returns
  uint32_t g = 0;
  for (int t = 0; t < ENG_TRACKS; ++t) g |= vgate[t] ? 1u << t : 0u;
  gates_now = g;
  // the gains: each block's targets, glided (a trig's random pan, a lock on the level, a cable), and reached sample by
  // sample over the block from the last block's: a gain that jumps at a block's edge is a click on what plays
  float gain[ENG_TRACKS], gl[ENG_TRACKS], gr[ENG_TRACKS], sends[ENG_TRACKS], dsends[ENG_TRACKS];
  float dgl[ENG_TRACKS], dgr[ENG_TRACKS], dsend_d[ENG_TRACKS], ddsend_d[ENG_TRACKS], inv = 1.0f / static_cast<float>(n);
  for (int t = 0; t < ENG_TRACKS; ++t) {
    // the pan: equal power (a track as loud wherever it sits: 0.71 a side in the middle, 1 on one side)
    pan_s[t] += (E(t, TP_PAN) - pan_s[t]) * 0.5f;
    lev_s[t] += (E(t, TP_LEVEL) - lev_s[t]) * 0.5f;
    snd_s[t] += (E(t, TP_SEND) - snd_s[t]) * 0.5f;
    dsnd_s[t] += (E(t, TP_DSEND) - dsnd_s[t]) * 0.5f;
    float a = (clampf(pan_s[t], -1.0f, 1.0f) + 1.0f) * 0.785398f, level = lev_s[t];
    gain[t] = level * 0.7f;
    float tl = level * 0.85f * fm_cosf(a), tr = level * 0.85f * fm_sinf(a);
    // the sends: post-fader, before the pan (both effects are stereo themselves), each through its lane's opening
    float ts = snd_s[t] * gain[t] * open[fr], td = dsnd_s[t] * gain[t] * open[fd];
    gl[t] = mix_gl[t];
    gr[t] = mix_gr[t];
    sends[t] = mix_send[t];
    dsends[t] = mix_dsend[t];
    dgl[t] = (tl - gl[t]) * inv;
    dgr[t] = (tr - gr[t]) * inv;
    dsend_d[t] = (ts - sends[t]) * inv;
    ddsend_d[t] = (td - dsends[t]) * inv;
    mix_gl[t] = tl;
    mix_gr[t] = tr;
    mix_send[t] = ts;
    mix_dsend[t] = td;
  }
  for (int f = 0; f < kFx; ++f) fx_open[f] = open[f];
  mrv_set(reverb, E(LANE_REVERB, 0), E(LANE_REVERB, 1), E(LANE_REVERB, 2));
  int div = iround(E(LANE_DELAY, 0));
  mdl_set(delay, step_len_now() * kDivMult[div < 0 ? 0 : div >= kDivs ? kDivs - 1 : div], E(LANE_DELAY, 1),
          E(LANE_DELAY, 2), E(LANE_DELAY, 4), E(LANE_DELAY, 3), iround(E(LANE_DELAY, 5)), E(LANE_DELAY, 6));
  float rv = E(LANE_REVERB, 3) * 1.6f, dv = E(LANE_DELAY, 7) * 1.2f;
  float ret = mix_ret, dret = (rv - mix_ret) * inv, eret = mix_dret, deret = (dv - mix_dret) * inv;
  float pk[ENG_TRACKS + 2] = {0, 0, 0, 0, 0, 0};
  mix_ret = rv;
  mix_dret = dv;
  for (uint32_t i = 0; i < n; ++i) {
    float s = 0.0f, ds = 0.0f;
    for (int t = 0; t < ENG_TRACKS; ++t) {
      sends[t] += dsend_d[t];
      dsends[t] += ddsend_d[t];
      s += sends[t] * vbuf[t][i];
      ds += dsends[t] * vbuf[t][i];
      float a = fm_fabsf(vbuf[t][i] * gain[t]);
      if (a > pk[t]) pk[t] = a;
    }
    send[i] = s;
    dsend[i] = ds;
  }
  mrv_process(reverb, send, wet_l, wet_r, n);
  mdl_process(delay, dsend, echo_l, echo_r, n);
  for (uint32_t i = 0; i < n; ++i) {
    float dl = 0.0f, dr = 0.0f;
    for (int t = 0; t < ENG_TRACKS; ++t) {
      gl[t] += dgl[t];
      gr[t] += dgr[t];
      dl += gl[t] * vbuf[t][i];
      dr += gr[t] * vbuf[t][i];
    }
    ret += dret;
    eret += deret;
    float wl = ret * wet_l[i], wr = ret * wet_r[i], el = eret * echo_l[i], er = eret * echo_r[i];
    float a = fm_fabsf(wl) > fm_fabsf(wr) ? fm_fabsf(wl) : fm_fabsf(wr);
    if (a > pk[ENG_TRACKS]) pk[ENG_TRACKS] = a;
    a = fm_fabsf(el) > fm_fabsf(er) ? fm_fabsf(el) : fm_fabsf(er);
    if (a > pk[ENG_TRACKS + 1]) pk[ENG_TRACKS + 1] = a;
    float ol = sat(dl + wl + el), orr = sat(dr + wr + er);
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
  for (int t = 0; t < ENG_TRACKS + 2; ++t)   // the meters: up at once, down slowly
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
float eng_meter(int t) { return t >= 0 && t < ENG_TRACKS + 2 ? meter[t] : 0.0f; }
int eng_hist(int s, int i) {
  if (s <= SRC_NONE || s >= NSRC || i < 0 || i >= ENG_HIST) return 0;
  return hist[s][(hist_at + static_cast<uint32_t>(i)) % ENG_HIST];
}
int eng_core1_running(void) { return on_core1_last; }
uint32_t eng_task_load(int task) {
  if (task < 0 || task >= kTasks) return 0;
  return task == ENG_TRACKS && !w_job ? 0 : pct10(static_cast<uint32_t>(task_cost[task]), kN);
}

int eng_warps_algo(void) {
  int car = Pi(PG_WARPS(1), 0);
  if (car <= WC_OFF || car > WC_OSC3) return -1;
  return Pi(PG_WARPS(1), 3) ? 6 : warps_pos(E(LANE_WARPS, 0));
}
const char* eng_warps_name(int algo) { return algo >= 0 && algo < 7 ? kWarpsNames[algo] : "WARPS"; }
int eng_warps_track(void) { return w_dst; }

int eng_trig(int lane, int step) {
  if (lane < 0 || lane >= ENG_LANES || step < 0 || step >= ENG_STEPS) return 0;
  return (proj.trig[lane][step / 8] >> (step % 8)) & 1;
}
void eng_trig_set(int lane, int step, int on) {
  if (lane < 0 || lane >= ENG_LANES || step < 0 || step >= ENG_STEPS) return;
  if (on) proj.trig[lane][step / 8] = static_cast<uint8_t>(proj.trig[lane][step / 8] | (1u << (step % 8)));
  else proj.trig[lane][step / 8] = static_cast<uint8_t>(proj.trig[lane][step / 8] & ~(1u << (step % 8)));
}
int eng_lane_has_trigs(int lane) {
  if (lane < 0 || lane >= ENG_LANES) return 0;
  int len = iround(proj.val[PG_STEP(lane) * 4 + 3]);
  len = len < 1 ? 1 : len > ENG_STEPS ? ENG_STEPS : len;
  for (int b = 0; b * 8 < len; ++b) {
    uint32_t bits = proj.trig[lane][b];
    if (len - b * 8 < 8) bits &= (1u << (len - b * 8)) - 1u;   // (only the steps it plays)
    if (bits) return 1;
  }
  return 0;
}
int eng_lock(int lane, int step, int tp, float* v) {
  int i = lock_find(lane, step, tp);
  if (i < 0) return 0;
  if (v) *v = from_u(eng_track_param(lane, tp), static_cast<float>(proj.lock[i].v) / 255.0f);
  return 1;
}
int eng_lock_set(int lane, int step, int tp, float v) {   // the value first, the slot's names last (the render reads it)
  if (!eng_tp_lockable(lane, tp) || step < 0 || step >= ENG_STEPS) return -1;
  int i = lock_find(lane, step, tp);
  uint8_t u = static_cast<uint8_t>(clampf(to_u(eng_track_param(lane, tp), v), 0.0f, 1.0f) * 255.0f + 0.5f);
  if (i < 0)
    for (int j = 0; j < ENG_PLOCKS && i < 0; ++j)
      if (proj.lock[j].lane == 0xFF) i = j;
  if (i < 0) return -1;
  proj.lock[i].v = u;
  proj.lock[i].step = static_cast<uint8_t>(step);
  proj.lock[i].tp = static_cast<uint8_t>(tp);
  proj.lock[i].lane = static_cast<uint8_t>(lane);
  return 0;
}
void eng_lock_clear(int lane, int step) {
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (proj.lock[i].lane == lane && proj.lock[i].step == step) proj.lock[i].lane = 0xFF;
}
void eng_steps_clear(int lane) {        // a lane's pattern: its trigs and its locks (its knobs and randoms stay)
  if (lane < 0 || lane >= ENG_LANES) return;
  for (int b = 0; b < ENG_STEPS / 8; ++b) proj.trig[lane][b] = 0;
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (proj.lock[i].lane == lane) proj.lock[i].lane = 0xFF;
}
int eng_step_locked(int lane, int step) {
  int n = 0;
  for (int i = 0; i < ENG_PLOCKS; ++i) n += proj.lock[i].lane == lane && proj.lock[i].step == step;
  return n;
}
int eng_step_now(int lane) {
  return playing && lane >= 0 && lane < ENG_LANES && tick >= 0 ? cur_step[lane] : -1;
}
float eng_lane_open(int lane) {
  return lane >= ENG_TRACKS && lane < ENG_LANES ? fx_open[lane - ENG_TRACKS] : 1.0f;
}
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
