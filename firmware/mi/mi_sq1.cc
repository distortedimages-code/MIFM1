// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's OTIS (0.16.14 as SQ-1; 0.16.16 named after the Ensoniq chip it reads like): a pocket Ensoniq, after the SQ-1 (1990) and its OTIS chip (the ES5505: 16-bit PCM read by a
// phase accumulator, two samples apart linearly; a loop's points moved while it plays, the Transwaves; four one-pole
// filters, no resonance; MAME's es5506.cpp, Aaron Giles', read for how). Not its sounds, its ways: eight knobs a
// track, four voices, 24 programs on 15 Transwaves of MIFM1's own (0.16.16: eight of the first kept, seven grainier).
//
// A Transwave: 32 cycles of 256 samples (or, for a bell, 8 loops of 1024: four of its periods, its partials rounded
// to a quarter of the note), made from a recipe (a resonance climbing, FM, a bar's partials, a glass', keys, a pad's
// clusters, vowels, a hard sync; then random cycles smoothed less and less, a crusher, chirps, VOSIM's pulses,
// harmonics shuffled, a ring modulator, an FM fed back to noise) into the track's Plaits voice's memory, lent
// while the track plays this machine (16 KB the cycles, what is left SPACE's line). A cycle read whole, the next one
// taken at its wrap, as the chip's loop points moved: a Transwave steps through its cycles, it does not morph.
//
// WAVE: the program (its Transwave, its layers, its envelope's scale, its poles, its SPACE). INDEX: where in the
// Transwave a note starts. SWEEP: how far an envelope moves it (from the middle: to the right up, to the left down; a
// pluck's falls back to INDEX, a pad's rises from it). MOTION: an LFO on it (its pace and depth with it; past 50 % a
// little vibrato, past 70 % unsteady). COLOR: the poles (to the left dark, the low-pass ones closing; to the right
// thin, the high-pass one rising). SHAPE: the envelope (0 percussive, 50 % keys and basses, 100 % slow pads), the
// sweep's time with it. STACK: the second layer (the Transwave elsewhere, an octave or a fifth off, detuned), then the
// third (an attack's tick, a breath, a ring). SPACE: a chorus, then the program's own: a phaser, a comb, a drive.
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"
#include "plaits/resources.h"

namespace {

const float kSr = 44100.0f;
const int kVoices = 4, kSub = 16;
const int kFrames = 32, kLen = 256, kLoops = 8, kLoopLen = 1024;
const uint32_t kBankBytes = static_cast<uint32_t>(kFrames * kLen) * 2u;   // (16 KB)
const uint32_t kLine = 1024;            // SPACE's line (23 ms: a power of two, its places masked)

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }
inline float sin_cyc(float ph) {        // a sine of a phase in cycles
  ph -= fm_floorf(ph);
  float x = ph < 0.5f ? ph : ph - 0.5f, s = ph < 0.5f ? 1.0f : -1.0f;
  x = x < 0.25f ? x : 0.5f - x;
  float z = x * 4.0f, z2 = z * z;
  return s * z * (1.5707963f + z2 * (-0.64596336f + z2 * (0.079688469f + z2 * (-0.0046722113f + z2 * 0.00015081274f))));
}
inline float lsin(uint32_t i) { return plaits::lut_sine[i & 511u]; }   // (512 a cycle)
inline float fsin(float ph) {           // a sine of a phase in cycles, from the table, between two of its steps (the
  ph -= fm_floorf(ph);                  // grainy recipes': cheaper than sin_cyc, -80 dB enough for them)
  const float u = ph * 512.0f;
  const int i = static_cast<int>(u);
  const float fr = u - static_cast<float>(i);
  return plaits::lut_sine[i] + (plaits::lut_sine[i + 1] - plaits::lut_sine[i]) * fr;
}
inline uint32_t xs(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline float bip(uint32_t& s) { return static_cast<float>(xs(s) >> 8) * (2.0f / 16777216.0f) - 1.0f; }
inline float sat(float x) {
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
inline float powr(float x, float p) { return x > 1e-6f ? fm_exp2f(p * fm_log2f(x)) : 0.0f; }
inline uint32_t inc_of(float hz, float periods) {
  return static_cast<uint32_t>(clampf(hz / periods / kSr, 0.0f, 0.45f) * 4294967296.0f);
}

// ---- the Transwaves ----
// (0.16.16) seven classics (a resonance, an electric piano's FM, keys, a pad, vowels, a hard sync, a bar's and a
// glass' modes) and seven of grain: random cycles, a crushed wave, a chirp, VOSIM's pulses, harmonics shuffled, a ring
// modulation, an FM fed back into chaos
enum { G_RESO, G_FM, G_BELL, G_HARM, G_PAD, G_VOX, G_SYNC, G_GRAIN, G_CRUSH, G_CHIRP, G_BURST, G_SHUFFLE, G_RING,
       G_CHAOS };
struct Wave {
  uint8_t gen, ga;                      // its recipe, the recipe's setting (an FM's ratio; a bell: bar 0, glass 1)
  float gb;                             // (an FM's index at its last cycle)
  uint8_t hmax;                         // its highest harmonic that counts (the poles kept under where it folds)
};
enum { W_RESO, W_FM1, W_BAR, W_GLASS, W_KEYS, W_PAD, W_VOX, W_SYNC, W_GRAIN, W_CRUSH, W_CHIRP, W_BURST, W_SHUFFLE,
       W_RING, W_CHAOS, W_N };
const Wave kWave[W_N] = {
    {G_RESO, 0, 0.0f, 64},    {G_FM, 1, 5.5f, 24},    {G_BELL, 0, 0.0f, 20},  {G_BELL, 1, 0.0f, 14},
    {G_HARM, 0, 0.0f, 48},    {G_PAD, 0, 0.0f, 48},   {G_VOX, 0, 0.0f, 40},   {G_SYNC, 0, 0.0f, 96},
    {G_GRAIN, 0, 0.0f, 128},  {G_CRUSH, 0, 0.0f, 128}, {G_CHIRP, 0, 0.0f, 48}, {G_BURST, 0, 0.0f, 64},
    {G_SHUFFLE, 0, 0.0f, 64}, {G_RING, 0, 0.0f, 40},  {G_CHAOS, 0, 0.0f, 128},
};
inline int looped(int w) { return kWave[w].gen == G_BELL; }

// ---- the programs ----
enum { L3_NONE, L3_TICK, L3_NOISE, L3_RING };
enum { SW_FALL, SW_RISE };
enum { F_LP2, F_LP4, F_THIN };          // OTIS' poles: two low-pass, four; two and the high-pass one higher
enum { SP_PHASER, SP_COMB, SP_DRIVE };  // SPACE's second half
struct Prog {
  const char* name;
  uint8_t wave;
  int8_t l2_semi, l2_at;                // the second layer: semitones off, positions on from the first's
  uint8_t l3;
  float env;                            // the envelope's times scaled
  uint8_t sweep, filt;
  float fenv;                           // octaves the envelope opens the poles
  uint8_t space;
  float motion, level;
};
const int kProgs = 24;
const Prog kProg[kProgs] = {
    // GRIT BASS
    {"Res Bass", W_RESO, -12, 0, L3_TICK, 0.6f, SW_FALL, F_LP4, 1.2f, SP_DRIVE, 0.5f, 1.15f},
    {"Crusher", W_CRUSH, -12, 4, L3_TICK, 0.6f, SW_FALL, F_LP4, 0.8f, SP_DRIVE, 0.6f, 0.78f},
    {"Chaos", W_CHAOS, 0, 3, L3_TICK, 0.7f, SW_FALL, F_LP2, 0.6f, SP_DRIVE, 0.7f, 0.90f},
    // ELECTRIC
    {"E.Piano", W_FM1, 12, 3, L3_TICK, 1.2f, SW_FALL, F_LP2, 0.3f, SP_PHASER, 0.6f, 0.64f},
    {"Tines", W_KEYS, 12, 6, L3_TICK, 1.0f, SW_FALL, F_LP4, 0.8f, SP_PHASER, 0.5f, 0.66f},
    {"Shard", W_SHUFFLE, 12, 5, L3_TICK, 0.8f, SW_FALL, F_THIN, 0.5f, SP_COMB, 0.7f, 1.14f},
    // METALLIC
    {"Metal", W_BAR, 7, 1, L3_NOISE, 1.2f, SW_FALL, F_THIN, 0.0f, SP_COMB, 0.8f, 1.06f},
    {"Ring", W_RING, 0, 6, L3_RING, 0.6f, SW_FALL, F_LP2, 0.4f, SP_COMB, 0.6f, 1.31f},
    {"Gong", W_BAR, -12, 1, L3_NOISE, 2.5f, SW_RISE, F_LP2, 0.2f, SP_PHASER, 1.0f, 0.77f},
    // GLASS
    {"Glass", W_GLASS, 12, 1, L3_TICK, 1.4f, SW_RISE, F_LP2, 0.0f, SP_PHASER, 0.6f, 0.76f},
    {"Chirp", W_CHIRP, 12, 4, L3_TICK, 0.7f, SW_FALL, F_THIN, 0.4f, SP_COMB, 0.6f, 0.97f},
    {"Crystal", W_GLASS, 19, 2, L3_RING, 0.8f, SW_FALL, F_THIN, 0.3f, SP_COMB, 0.5f, 1.04f},
    // VOICES
    {"Vox", W_VOX, 12, 2, L3_TICK, 0.9f, SW_FALL, F_LP2, 0.4f, SP_PHASER, 0.6f, 0.97f},
    {"Grains", W_BURST, 0, 6, L3_NOISE, 1.0f, SW_FALL, F_LP2, 0.3f, SP_PHASER, 0.8f, 1.93f},
    {"Choir", W_VOX, 0, 8, L3_NOISE, 2.0f, SW_RISE, F_LP2, 0.0f, SP_PHASER, 0.8f, 0.97f},
    // EVOLVING PAD
    {"Pad", W_PAD, 0, 10, L3_NOISE, 2.0f, SW_RISE, F_LP2, 0.2f, SP_PHASER, 1.0f, 1.26f},
    {"Dust", W_GRAIN, 0, 8, L3_NOISE, 2.2f, SW_RISE, F_LP2, 0.2f, SP_PHASER, 1.0f, 0.92f},
    {"Sweeper", W_RESO, 12, 6, L3_NONE, 2.5f, SW_RISE, F_LP2, 0.0f, SP_PHASER, 1.0f, 0.91f},
    // INDUSTRIAL
    {"Grind", W_SYNC, 7, 10, L3_RING, 0.7f, SW_FALL, F_THIN, 0.4f, SP_DRIVE, 0.9f, 0.87f},
    {"Static", W_GRAIN, -12, 12, L3_NOISE, 0.8f, SW_FALL, F_THIN, 0.6f, SP_DRIVE, 1.0f, 1.26f},
    {"Machine", W_CHAOS, 7, 9, L3_RING, 0.6f, SW_FALL, F_LP4, 0.8f, SP_COMB, 0.8f, 1.04f},
    // PERCUSSIVE
    {"Perc", W_RING, 19, 0, L3_RING, 0.4f, SW_FALL, F_LP2, 0.5f, SP_COMB, 0.4f, 1.48f},
    {"Mallet", W_BAR, 12, 0, L3_TICK, 0.35f, SW_FALL, F_LP2, 0.8f, SP_PHASER, 0.3f, 1.14f},
    {"Click", W_CRUSH, 12, 6, L3_TICK, 0.35f, SW_FALL, F_LP4, 1.0f, SP_COMB, 0.5f, 0.81f},
};
inline int prog_of(float u) {
  int i = static_cast<int>(u * static_cast<float>(kProgs));
  return i < 0 ? 0 : i > kProgs - 1 ? kProgs - 1 : i;
}

// ---- the state ----
struct SVoice {
  uint32_t ph[3];                       // the layers' phases (2^32 a cycle; a loop's four periods)
  uint8_t fr[3];                        // the cycles (loops) they read, another taken at their wraps; the one before,
  uint8_t was[3], xf[3];                // the samples left of its fade (8 at a change: the step's edge, not a morph)
  uint8_t alive, let_go, trig;
  int8_t key;                           // (0.16.15) the key it plays (the keyboard's chords), -1 a trig's
  uint8_t slot;                         // (0.16.16) its note in its trig's chord
  float note;
  float env, a0, att, dec;              // the envelope: its level, where its attack began, its progress, the fall's
  float swe, t;                         // the sweep's envelope; the time since the trig (s)
  float s1, s2, s3, s4, h1;             // the poles: four low-pass, the high-pass'
  float sign;                           // (the second layer's detune: up or down)
  uint32_t age;
};
struct Sq {
  float k[8];
  float note;                           // the trig's note (0.16.16: its chord's, the step's, notes[0] .. its n)
  float notes[kVoices];
  int n;
  int gate, trig;
  SVoice v[kVoices];
  uint32_t rnd, clock;
  float lfo, rs, rt;                    // MOTION: its phase, its smooth random (where, aiming)
  float gn, dcx, dcy;
  int8_t wave;                          // the bank: the Transwave it holds (-1 none); MOTION's quarter; the place
  uint8_t lq, aim;                      // wanted last (its next cycle made the nearest); the cycle being made (0xFF
  uint8_t job_f, job_m;                 // none), its next term; SPACE on at the last block (its line written)
  uint8_t fx_on, pad_[3];
  float job_g;                          // (its terms' scale)
  uint32_t made, epoch, credit;         // its cycles made (a bit each), the memory's epoch it was made in, the work it
  uint32_t lw;                          // may do; SPACE: the line's write place
  float cph, ap[4], cd, comb, drv;      // the chorus' phase, the phaser's all-passes, the comb's delay, its damping, the
  int32_t hold;                         // drive's tone; the samples SPACE rings on with no voice
};
static_assert(sizeof(Sq) <= MF_STATE_BYTES, "SQ-1's state");
struct SqAt {
  Sq& operator[](int t) const { return *static_cast<Sq*>(mf_state(t)); }
};
const SqAt sq = {};

// ---- the recipes ----
// A cycle made a little at a time (the work a block may do: 2048 terms' samples, about 140 us of the FM-1's 2902;
// twice that for a bank's first), the additive ones term by term into the cycle itself (its half: a sum of sines is
// odd over its cycle, of cosines even), then raised to its peak 0.9; the others at once
const uint32_t kWork = 16;              // (the work a sample of time earns)
inline int nterms(int w) {
  switch (kWave[w].gen) {
    case G_RESO: case G_SHUFFLE: return 64;
    case G_HARM: case G_PAD: return 48;
    case G_VOX: return 40;
    case G_BELL: return 7;
    default: return 0;                  // (made at once)
  }
}
// the term m (1..nterms) of the cycle f: its amplitude, its periods in the cycle (*cyc)
MI_COLD float term(int w, int f, int m, uint32_t* cyc) {
  const Wave& W = kWave[w];
  const float x = static_cast<float>(f) / static_cast<float>((looped(w) ? kLoops : kFrames) - 1);
  const float h = static_cast<float>(m);
  *cyc = static_cast<uint32_t>(m);
  switch (W.gen) {
    case G_RESO: {                      // a resonant low-pass' sweep frozen into the cycles: its peak climbing
      const float hc = 1.5f + 30.0f * powr(x, 1.6f), r = fm_log2f(h / hc);
      float v = (1.0f / h) * (1.0f + 4.0f * fm_expf(-r * r * 12.0f));
      if (h > hc) {
        const float q = hc / h;
        v *= q * q * q;
      }
      return v;
    }
    case G_HARM: {                      // keys: the harmonics falling steeply, then less; a tine's ring in the bright
      float v = fm_exp2f(-(3.0f - 2.2f * powr(x, 0.6f)) * fm_log2f(h));
      if (!(m & 1)) v *= 0.7f + 0.3f * x;
      if (m >= 5 && m <= 7) v *= 1.0f + 1.5f * x;
      return v;
    }
    case G_PAD: {                       // a pad's: the low harmonics, a cluster moving up over them (their signs spread:
      const float z = (h - 2.0f - 22.0f * x) / (2.0f + 3.0f * x);   // not one peak; the same in every cycle)
      const float v = 0.6f / h + 0.5f * fm_expf(-z * z);
      return ((static_cast<uint32_t>(m) * 2654435761u) >> 13) & 1u ? -v : v;
    }
    case G_VOX: {                       // vowels a, e, i, o, u: their three formants over a voice's harmonics
      static const float kF[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410},
                                     {300, 870, 2240}};
      static const float kB[3] = {90.0f, 120.0f, 170.0f}, kG[3] = {1.0f, 0.6f, 0.35f};
      const float pos = x * 4.0f;
      int v0 = static_cast<int>(pos);
      v0 = v0 > 3 ? 3 : v0;
      const float fr = pos - static_cast<float>(v0), hz = 120.0f * h;
      float v = 0.0f;
      for (int k = 0; k < 3; ++k) {
        const float F = kF[v0][k] + (kF[v0 + 1][k] - kF[v0][k]) * fr, z = (hz - F) / kB[k];
        v += kG[k] * fm_expf(-z * z);
      }
      return (v + 0.02f) * fm_exp2f(-0.5f * fm_log2f(h));
    }
    case G_SHUFFLE: {                   // harmonics shuffled: some kept, some not (fewer, higher, cycle by cycle; the
      if (m > 8 + static_cast<int>(56.0f * x)) return 0.0f;   // fundamental always), their signs anywhere
      const uint32_t hs = (static_cast<uint32_t>(m) * 2654435761u) ^ (static_cast<uint32_t>(f / 2) * 0x85EBCA6Bu);
      const uint32_t z = (hs ^ (hs >> 15)) * 0x2C1B3C6Du;
      if (m > 1 && static_cast<float>(z >> 8) * (1.0f / 16777216.0f) > 0.85f - 0.55f * x) return 0.0f;
      const float v = fm_exp2f(-0.7f * fm_log2f(h));
      return (z & 1u) ? -v : v;
    }
    case G_BELL: {                      // a bell's loop: four of the note's periods, its partials rounded to a quarter
      static const float kR[2][6] = {{1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f},   // of it (a bar's free modes; a
                                     {1.0f, 2.32f, 4.25f, 6.63f, 9.38f, 12.6f}};       // glass'), brighter loop by
      const float* R = kR[W.ga ? 1 : 0];                                               // loop, the third mode's twin
      const float tilt = 1.8f - 1.4f * x;                                              // a quarter above coming in
      if (m == 7) {                                                                    // (they beat)
        *cyc = static_cast<uint32_t>(4.0f * R[2] + 0.5f) + 1u;
        return 0.6f * x * fm_exp2f(-tilt * fm_log2f(3.0f));
      }
      *cyc = static_cast<uint32_t>(4.0f * R[m - 1] + 0.5f);
      return fm_exp2f(-tilt * fm_log2f(h));
    }
    default:
      return 0.0f;
  }
}
MI_COLD void normalize(int16_t* d, int len) {   // its peak 0.9
  int pk = 1;
  for (int i = 0; i < len; ++i) {
    int a = d[i] < 0 ? -d[i] : d[i];
    pk = a > pk ? a : pk;
  }
  const int32_t g = static_cast<int32_t>(29491.0f / static_cast<float>(pk) * 65536.0f);
  for (int i = 0; i < len; ++i) {
    int32_t y = (static_cast<int32_t>(d[i]) * g) >> 16;
    d[i] = static_cast<int16_t>(y > 32767 ? 32767 : y < -32767 ? -32767 : y);
  }
}
MI_COLD void smooth(int16_t* d, int len) {      // [1 2 1] / 4 round the cycle (a sharp edge's fold a little less)
  int first = d[0], prev = d[len - 1];
  for (int i = 0; i < len; ++i) {
    int cur = d[i], next = i + 1 < len ? d[i + 1] : first;
    d[i] = static_cast<int16_t>((prev + 2 * cur + next) >> 2);
    prev = cur;
  }
}
// the terms m0 .. m1 - 1 added into the half cycle (its first len / 2 + 1 samples)
__attribute__((noinline)) void add_terms(int16_t* d, int len, int w, int f, int m0, int m1, float g) {
  const uint32_t off = 0u;               // (sines: the cycles all start at 0)
  const int half = len / 2;
  for (int m = m0; m < m1; ++m) {
    uint32_t cyc = 0;
    const float a = term(w, f, m + 1, &cyc) * g;
    if (a < 0.5f && a > -0.5f) continue;   // (under a step of the sample)
    if (len == kLen) {                  // (two of the table's steps a sample: on them)
      const uint32_t step = 2u * cyc;
      uint32_t idx = 0;
      for (int i = 0; i <= half; ++i, idx += step) d[i] = static_cast<int16_t>(d[i] + static_cast<int>(a * lsin(idx + off)));
    } else {                            // (a loop: half a step; between two of them)
      uint32_t idx2 = 0;
      for (int i = 0; i <= half; ++i, idx2 += cyc) {
        const uint32_t k = (idx2 >> 1) & 511u;
        const float v = (idx2 & 1u) ? 0.5f * (plaits::lut_sine[k] + plaits::lut_sine[k + 1]) : plaits::lut_sine[k];
        d[i] = static_cast<int16_t>(d[i] + static_cast<int>(a * v));
      }
    }
  }
}
// a cycle made in time (FM, a sync, the grainy ones): at once
MI_COLD void make_now(int w, int f, int16_t* d) {
  const Wave& W = kWave[w];
  const float x = static_cast<float>(f) / static_cast<float>(kFrames - 1);
  switch (W.gen) {
    case G_FM: {                        // two operators, the index rising (the ratio the recipe's)
      const float I = 0.05f + W.gb * powr(x, 1.2f), r = static_cast<float>(W.ga);
      for (int i = 0; i < kLen; ++i) {
        const float ph = static_cast<float>(i) / static_cast<float>(kLen);
        d[i] = static_cast<int16_t>(29000.0f * fsin(ph + I * 0.159155f * fsin(r * ph)));
      }
      break;
    }
    case G_SYNC: {                      // a saw hard-synced, its ratio 1 to 7.5
      const float r = 1.0f + 6.5f * powr(x, 1.2f);
      for (int i = 0; i < kLen; ++i) {
        const float q = r * static_cast<float>(i) / static_cast<float>(kLen);
        d[i] = static_cast<int16_t>(29000.0f * (2.0f * (q - fm_floorf(q)) - 1.0f));
      }
      smooth(d, kLen);
      smooth(d, kLen);
      break;
    }
    case G_GRAIN: {                     // random cycles: a common one and this one's own (more of it to the right),
      uint32_t s0 = 0x2545F491u, s1 = 0x9E3779B9u ^ (static_cast<uint32_t>(f + 1) * 0x85EBCA6Bu);   // smoothed less
      const float a = 0.03f + 0.97f * x * x;   // and less: a dark wobble to a raw buzz
      for (int i = 0; i < kLen; ++i) d[i] = static_cast<int16_t>(20000.0f * ((1.0f - x) * bip(s0) + x * bip(s1)));
      float y1 = 0.0f, y2 = 0.0f;
      for (int lap = 0; lap < 3; ++lap)  // (two poles round the cycle, settled)
        for (int i = 0; i < kLen; ++i) {
          y1 += a * (static_cast<float>(d[i]) - y1);
          y2 += a * (y1 - y2);
          if (lap == 2) d[i] = static_cast<int16_t>(y2);
        }
      int32_t mean = 0;
      for (int i = 0; i < kLen; ++i) mean += d[i];
      mean /= kLen;
      for (int i = 0; i < kLen; ++i) d[i] = static_cast<int16_t>(d[i] - mean);
      break;
    }
    case G_CRUSH: {                     // a sine and a saw, crushed: held over more and more samples (1 to 16), on
      const int hold = 1 + static_cast<int>(15.0f * powr(x, 1.5f) + 0.5f);   // fewer and fewer levels (64 to 2)
      const float lv = 2.0f + 62.0f * (1.0f - x) * (1.0f - x);
      for (int i = 0; i < kLen; ++i) {
        const float ph = static_cast<float>((i / hold) * hold) / static_cast<float>(kLen);
        const float v = 0.7f * fsin(ph) + 0.3f * (2.0f * ph - 1.0f);
        d[i] = static_cast<int16_t>(29000.0f * fm_floorf(v * lv + 0.5f) / lv);
      }
      break;
    }
    case G_CHIRP: {                     // a chirp in each cycle, from the note up 1 to 41 harmonics, in a sine's window
      const float k = 1.0f + 40.0f * powr(x, 1.3f);
      for (int i = 0; i < kLen; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kLen);
        d[i] = static_cast<int16_t>(29000.0f * fsin(t + 0.5f * k * t * t) * fsin(0.5f * t));
      }
      break;
    }
    case G_BURST: {                     // VOSIM's (Kaegi and Tempelaars, 1978): three sine-squared pulses, each softer,
      const float F = 3.0f + 25.0f * x;   // their pitch the formant (3 to 28 harmonics), then silence to the cycle's end
      int32_t mean = 0;
      for (int i = 0; i < kLen; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(kLen) * F;
        const int k = static_cast<int>(u);
        float v = 0.0f;
        if (k < 3) {
          const float sn = fsin(0.5f * (u - static_cast<float>(k)));
          v = (k == 0 ? 1.0f : k == 1 ? 0.7f : 0.49f) * sn * sn;
        }
        d[i] = static_cast<int16_t>(29000.0f * v);
        mean += d[i];
      }
      mean /= kLen;
      for (int i = 0; i < kLen; ++i) d[i] = static_cast<int16_t>(d[i] - mean);
      break;
    }
    case G_RING: {                      // the note by two sines 2 to 32 and half again as many times over: a clang
      const int r = 2 + static_cast<int>(30.0f * powr(x, 1.4f) + 0.5f), r2 = r + 1 + r / 2;
      for (int i = 0; i < kLen; ++i) {
        const float ph = static_cast<float>(i) / static_cast<float>(kLen);
        const float c = fsin(ph);
        d[i] = static_cast<int16_t>(29000.0f * (c * (0.6f * fsin(r * ph) + 0.4f * fsin(r2 * ph)) + 0.25f * c));
      }
      break;
    }
    case G_CHAOS: {                     // an FM fed back on itself (0.3 to 5): its own sine, then a scream, then noise
      const float beta = (0.3f + 4.7f * x) * 0.159155f;   // that keeps (the same each cycle)
      float y = 0.0f;
      for (int i = kLen - 64; i < 2 * kLen; ++i) {   // (settled over the cycle's last quarter first)
        const int j = i % kLen;
        y = fsin(static_cast<float>(j) / static_cast<float>(kLen) + beta * y);
        if (i >= kLen) d[j] = static_cast<int16_t>(29000.0f * y);
      }
      smooth(d, kLen);
      break;
    }
    default:
      for (int i = 0; i < kLen; ++i) d[i] = 0;
      break;
  }
  normalize(d, kLen);
}

// the made cycle nearest to the one wanted
inline int near_made(uint32_t made, int want, int n) {
  if ((made >> want) & 1u) return want;
  for (int dd = 1; dd < n; ++dd) {
    if (want - dd >= 0 && ((made >> (want - dd)) & 1u)) return want - dd;
    if (want + dd < n && ((made >> (want + dd)) & 1u)) return want + dd;
  }
  return 0;
}

// the bank: the Transwave's cycles, one after another (the one nearest to where the voices are first), as much of
// them as the time since allows
MI_COLD __attribute__((noinline)) void build(Sq& s, int t, int w, uint32_t n, int16_t** bank) {
  uint32_t bytes = 0, epoch = 0;
  int16_t* b = static_cast<int16_t*>(mf_voice_mem(t, &bytes, &epoch));
  *bank = nullptr;
  if (!b || bytes < kBankBytes + kLine * 2u) return;
  *bank = b;
  if (s.wave != w || s.epoch != epoch) {
    s.wave = static_cast<int8_t>(w);
    s.epoch = epoch;
    s.made = 0;
    s.credit = 128u * kWork;            // (its first cycle the sooner)
    s.job_f = 0xFF;
    s.fx_on = 0;                        // (its line cleared when SPACE comes up)
    s.lw = 0;
    s.comb = 0.0f;
  }
  const int loop = looped(w), nf = loop ? kLoops : kFrames, len = loop ? kLoopLen : kLen, nt = nterms(w);
  const uint32_t full = loop ? 0xFFu : 0xFFFFFFFFu;
  if (s.made == full) return;
  s.credit += n * kWork;
  const uint32_t most = 2u * 128u * kWork;
  s.credit = s.credit > most ? most : s.credit;
  const uint32_t per = static_cast<uint32_t>(len / 2 + 1);   // (a term's work)
  while (s.made != full) {
    if (s.job_f == 0xFF) {              // the next cycle: the nearest to the place wanted
      const int aim = s.aim < nf ? s.aim : nf - 1;
      int f = -1;
      for (int dd = 0; dd < nf && f < 0; ++dd) {
        if (aim + dd < nf && !((s.made >> (aim + dd)) & 1u)) f = aim + dd;
        else if (aim - dd >= 0 && !((s.made >> (aim - dd)) & 1u)) f = aim - dd;
      }
      if (f < 0) return;
      if (!nt) {                        // (made at once: its work a few terms')
        if (s.credit < 16u * per) return;
        s.credit -= 16u * per;
        make_now(w, f, b + f * len);
        s.made |= 1u << f;
        continue;
      }
      if (s.credit < per * 4u) return;
      int16_t* d = b + f * len;
      for (int i = 0; i < len; ++i) d[i] = 0;
      float sum = 0.0f;
      for (int m = 1; m <= nt; ++m) {
        uint32_t cyc = 0;
        sum += fm_fabsf(term(w, f, m, &cyc));
      }
      s.job_f = static_cast<uint8_t>(f);
      s.job_m = 0;
      s.job_g = 32000.0f / (sum > 1e-6f ? sum : 1.0f);
      s.credit -= per * 4u;             // (its terms' sum: about four terms' work)
    }
    const int f = s.job_f;
    int16_t* d = b + f * len;
    int m1 = s.job_m + static_cast<int>(s.credit / per);
    m1 = m1 > nt ? nt : m1;
    if (m1 <= s.job_m) return;
    add_terms(d, len, w, f, s.job_m, m1, s.job_g);
    s.credit -= static_cast<uint32_t>(m1 - s.job_m) * per;
    s.job_m = static_cast<uint8_t>(m1);
    if (m1 < nt) return;
    const int half = len / 2, even = 0;   // its other half: turned over (a sum of sines)
    for (int i = 1; i < half; ++i) d[len - i] = static_cast<int16_t>(even ? d[i] : -d[i]);
    if (!even) d[0] = d[half] = 0;
    normalize(d, len);
    s.made |= 1u << f;
    s.job_f = 0xFF;
  }
}

// ---- the envelope (SHAPE) ----
struct Shape {
  float A, D, S, R;                     // the attack, the fall to the sustain, the sustain, the release (s)
  float sweep;                          // the sweep's time (s)
};
inline Shape shape_of(float u, float scale) {
  Shape e;
  e.A = u < 0.5f ? 0.001f + 0.014f * u : 0.008f * fm_exp2f((u - 0.5f) * 2.0f * 7.55f);   // (1 ms .. 8 ms .. 1.5 s)
  e.D = 0.12f * fm_exp2f(u * 4.0f);     // (0.12 .. 1.9 s)
  e.S = u < 0.25f ? 0.0f : u < 0.5f ? (u - 0.25f) * 2.4f : 0.6f + 0.4f * (u - 0.5f) * 2.0f;
  e.R = 0.04f * fm_exp2f(u * 6.2f);     // (40 ms .. 3 s)
  e.sweep = (0.04f + 1.6f * u * u) * scale;
  e.A *= scale, e.D *= scale, e.R *= scale;
  return e;
}

// a trig: a voice (a free one, else the quietest, the oldest); the ones still sounding let go
void take(Sq& s) {                      // (0.16.16: a voice for each of its chord's notes)
  ++s.clock;
  const int n = s.n < 1 ? 1 : s.n > kVoices ? kVoices : s.n;
  int used = 0;
  for (int j = 0; j < n; ++j) {
    int best = 0;
    float bs = 1e9f;
    for (int i = 0; i < kVoices; ++i) {
      if ((used >> i) & 1) continue;
      const SVoice& v = s.v[i];
      float score = v.alive ? v.env + 1.0f - static_cast<float>(s.clock - v.age) * 1e-6f : 0.0f;
      if (v.alive && v.key >= 0) score += 2.0f;   // (a key held: the last taken)
      if (score < bs) bs = score, best = i;
    }
    used |= 1 << best;
    SVoice& v = s.v[best];
    v.key = -1;
    v.slot = static_cast<uint8_t>(j);
    v.note = s.notes[j];
    if (!v.alive) {                     // from silence: its poles at rest, its layers' phases anywhere
      for (int l = 0; l < 3; ++l) v.ph[l] = xs(s.rnd);
      v.s1 = v.s2 = v.s3 = v.s4 = v.h1 = 0.0f;
      v.env = 0.0f;
    }
    v.sign = ((s.clock + static_cast<uint32_t>(j)) & 1u) ? 1.0f : -1.0f;
    v.trig = 1;
    v.age = s.clock;
  }
  for (int i = 0; i < kVoices; ++i)
    if (!((used >> i) & 1) && s.v[i].alive && !s.v[i].let_go && s.v[i].key < 0)
      s.v[i].let_go = 1, s.v[i].dec = s.v[i].env;
}

// a layer's samples added into y: its cycle read at its phase, linearly between two samples; at its wrap the next one
// (the one wanted then), the one it leaves faded out over 8 samples (the step's edge softened)
const int kXf = 8;
__attribute__((noinline)) void layer(SVoice& v, int l, int want, const int16_t* bank, int len_bits, uint32_t inc, float gain,
                  float* __restrict y, int n) {
  const int sh = 32 - len_bits;
  const uint32_t mask = (1u << len_bits) - 1u;
  uint32_t ph = v.ph[l];
  const int16_t* w = bank + (static_cast<uint32_t>(v.fr[l]) << len_bits);
  const int16_t* o = bank + (static_cast<uint32_t>(v.was[l]) << len_bits);
  int xf = v.xf[l];
  for (int i = 0; i < n; ++i) {
    const uint32_t old = ph;
    ph += inc;
    if (ph < old && want != v.fr[l]) {  // (the wrap: the loop's points moved)
      v.was[l] = v.fr[l];
      v.fr[l] = static_cast<uint8_t>(want);
      o = w;
      w = bank + (static_cast<uint32_t>(want) << len_bits);
      xf = kXf;
    }
    const uint32_t idx = ph >> sh, idx1 = (idx + 1) & mask;
    const int32_t frac = static_cast<int32_t>((ph >> (sh - 15)) & 0x7FFFu);
    int32_t a = w[idx], b = w[idx1];
    int32_t x = a + (((b - a) * frac) >> 15);
    if (xf) {
      const int32_t c = o[idx], d = o[idx1], z = c + (((d - c) * frac) >> 15);
      x += (z - x) * xf / (kXf + 1);
      --xf;
    }
    y[i] += gain * static_cast<float>(x);
  }
  v.ph[l] = ph;
  v.xf[l] = static_cast<uint8_t>(xf);
}

}  // namespace

extern "C" {

void sq_reset(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Sq& s = sq[t];
  uint8_t* raw = reinterpret_cast<uint8_t*>(&s);
  for (uint32_t i = 0; i < sizeof(Sq); ++i) raw[i] = 0;
  s.rnd = (0x51ED270Bu * static_cast<uint32_t>(t + 5)) | 1u;
  s.wave = -1;
  float d[8];
  sq_defaults(d);
  for (int i = 0; i < 8; ++i) s.k[i] = d[i];
  s.note = s.notes[0] = 48.0f;
  s.n = 1;
  for (int i = 0; i < kVoices; ++i) s.v[i].att = 1.0f, s.v[i].key = -1;
}

void sq_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) sq_reset(t);
  mf_forget();
}

void sq_silence(int t) {                // (its bank kept; SPACE's line silent)
  if (t < 0 || t >= MF_TRACKS) return;
  Sq& s = sq[t];
  s.trig = 0;
  for (int i = 0; i < kVoices; ++i) s.v[i].alive = 0, s.v[i].env = 0.0f, s.v[i].att = 1.0f, s.v[i].trig = 0;
  s.hold = 0;
  s.gn = s.dcx = s.dcy = s.comb = s.drv = 0.0f;
  for (int i = 0; i < 4; ++i) s.ap[i] = 0.0f;
  uint32_t bytes = 0, epoch = 0;
  int16_t* b = static_cast<int16_t*>(mf_voice_mem(t, &bytes, &epoch));
  if (b && s.wave >= 0 && s.epoch == epoch && bytes >= kBankBytes + kLine * 2u)
    for (uint32_t i = 0; i < kLine; ++i) b[kFrames * kLen + i] = 0;
}

void sq_set(int t, const float* notes, int n, const float* k) {
  if (t < 0 || t >= MF_TRACKS) return;
  Sq& s = sq[t];
  for (int i = 0; i < 8; ++i) s.k[i] = clampf(k[i], 0.0f, 1.0f);
  n = n < 1 ? 1 : n > kVoices ? kVoices : n;
  for (int i = 0; i < n; ++i) s.notes[i] = notes[i];
  s.note = notes[0];
  if (s.gate && n == s.n)               // (the notes held follow: a lock, FREQ's modulations)
    for (int i = 0; i < kVoices; ++i)
      if (s.v[i].alive && !s.v[i].let_go && s.v[i].age == s.clock && s.v[i].key < 0 && s.v[i].slot < n)
        s.v[i].note = notes[s.v[i].slot];
  s.n = n;
}

void sq_gate(int t, int g) {
  if (t < 0 || t >= MF_TRACKS) return;
  Sq& s = sq[t];
  if (g && !s.gate) s.trig = 1;
  if (!g && s.gate)                     // let go: its release (a key's voice: when its key is)
    for (int i = 0; i < kVoices; ++i)
      if (s.v[i].alive && !s.v[i].let_go && s.v[i].key < 0) s.v[i].let_go = 1, s.v[i].dec = s.v[i].env;
  s.gate = g;
}

// (0.16.15) the keys held: a voice each (a free one, else a trig's, the quietest, else the oldest key's), its pitch
// following FREQ; a key let go, its voice released
void sq_keys(int t, const int* ids, const float* notes, int n) {
  if (t < 0 || t >= MF_TRACKS) return;
  Sq& s = sq[t];
  n = n < 0 ? 0 : n > kVoices ? kVoices : n;
  for (int i = 0; i < kVoices; ++i) {
    SVoice& v = s.v[i];
    if (v.key < 0) continue;
    int j = 0;
    while (j < n && ids[j] != v.key) ++j;
    if (j < n && (v.alive || v.trig)) {
      v.note = notes[j];
      continue;
    }
    if (v.alive && !v.let_go) v.let_go = 1, v.dec = v.env;
    v.key = -1;
  }
  for (int j = 0; j < n; ++j) {
    int have = 0;
    for (int i = 0; i < kVoices; ++i) have |= s.v[i].key == ids[j];
    if (have) continue;
    int best = 0;
    float bs = 1e9f;
    for (int i = 0; i < kVoices; ++i) {
      const SVoice& v = s.v[i];
      float score = v.alive ? v.env + 1.0f - static_cast<float>(s.clock - v.age) * 1e-6f : 0.0f;
      if (v.key >= 0) score += 2.0f;
      if (score < bs) bs = score, best = i;
    }
    SVoice& v = s.v[best];
    if (!v.alive) {
      for (int l = 0; l < 3; ++l) v.ph[l] = xs(s.rnd);
      v.s1 = v.s2 = v.s3 = v.s4 = v.h1 = 0.0f;
      v.env = 0.0f;
    }
    v.note = notes[j];
    v.key = static_cast<int8_t>(ids[j]);
    v.sign = (s.clock & 1u) ? 1.0f : -1.0f;
    v.trig = 1;
    v.age = ++s.clock;
  }
}

int sq_voices(int t) {
  if (t < 0 || t >= MF_TRACKS) return 0;
  int n = 0;
  for (int i = 0; i < kVoices; ++i) n += sq[t].v[i].alive;
  return n;
}

void sq_render(int t, float* out, uint32_t n) {
  if (t < 0 || t >= MF_TRACKS) return;
  Sq& s = sq[t];
  for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
  if (!n) return;
  const int pi = prog_of(s.k[0]);
  const Prog& P = kProg[pi];
  const int w = P.wave, loop = looped(w), nf = loop ? kLoops : kFrames, len_bits = loop ? 10 : 8;
  const float periods = loop ? 4.0f : 1.0f, top = static_cast<float>(nf - 1);
  const float sw0 = 2.0f * s.k[2] - 1.0f, sweep = sw0 * (0.35f + 0.65f * fm_fabsf(sw0));   // (finer about its middle)
  const float base = s.k[1] * top;
  int start = static_cast<int>(clampf(base + (P.sweep == SW_FALL ? sweep * top : 0.0f), 0.0f, top) + 0.5f);
  if (s.trig || !sq_voices(t)) s.aim = static_cast<uint8_t>(start);   // (a note's first cycle made first)
  int16_t* bank = nullptr;
  build(s, t, w, n, &bank);
  if (!bank || !s.made) return;
  if (s.trig) {
    s.trig = 0;
    take(s);
  }
  const float fn = static_cast<float>(n);
  const float mot = s.k[3], color = s.k[4];
  const Shape E = shape_of(s.k[5], P.env);
  const float stack = s.k[6], space = s.k[7];
  // MOTION: its pace (0.15 .. 7 Hz), its depth on the Transwave; past 50 % vibrato, past 70 % unsteady
  const float rate = 0.15f * fm_exp2f(mot * 5.5f);
  const float depth = mot * mot * 10.0f * P.motion * (loop ? 0.3f : 1.0f);
  s.lfo += rate * fn / kSr;
  s.lfo -= s.lfo >= 1.0f ? 1.0f : 0.0f;
  const uint8_t q = static_cast<uint8_t>(s.lfo * 4.0f);
  if (q != s.lq) s.lq = q, s.rt = bip(s.rnd);   // (its unsteady part: a new aim four times a cycle)
  s.rs += (s.rt - s.rs) * clampf(rate * 8.0f * fn / kSr, 0.0f, 1.0f);
  const float tri = 4.0f * (s.lfo < 0.5f ? s.lfo : 1.0f - s.lfo) - 1.0f;
  const float unsteady = mot > 0.7f ? (mot - 0.7f) / 0.3f : 0.0f;
  const float lfo = tri * (1.0f - unsteady) + s.rs * unsteady;
  const float vib = mot > 0.5f ? (mot - 0.5f) * 2.0f * 0.12f * lfo : 0.0f;   // (semitones: 12 cents at the most)
  // STACK: the second layer (0 .. 50 %: its level, its detune), the third (50 .. 100 %)
  const float l2 = stack < 0.5f ? stack * 1.6f : 0.8f;
  const float det = (stack < 0.5f ? stack * 2.0f * 12.0f : 12.0f + (stack - 0.5f) * 16.0f) * 0.01f;
  const float l3 = stack > 0.5f ? (stack - 0.5f) * 1.4f : 0.0f;
  // COLOR: the low-pass poles 160 Hz .. 18 kHz over 0 .. 60 %; past it the high-pass one rising, 30 Hz .. 1.6 kHz
  const float lp_oct = color < 0.6f ? color / 0.6f * 6.8f : 6.8f;
  const float hp_hz = (P.filt == F_THIN ? 120.0f : 30.0f) * fm_exp2f(color > 0.6f ? (color - 0.6f) / 0.4f * 5.7f : 0.0f);
  const float hp_k = 1.0f - fm_expf(-6.2831853f * hp_hz / kSr);
  int active = 0;
  for (int i = 0; i < kVoices; ++i) active += s.v[i].alive || s.v[i].trig;
  if (!active) {                        // no voice: SPACE rings out, then nothing
    if (s.hold <= 0 && fm_fabsf(s.dcy) < 1e-6f) {
      s.gn = 0.0f;
      s.dcx = s.dcy = 0.0f;
      return;
    }
  } else {
    s.hold = static_cast<int32_t>(kSr * 0.25f);
  }
  const float norm = 0.5f * P.level / fm_sqrtf(static_cast<float>(active > 1 ? active : 1));
  if (s.gn <= 0.0f) s.gn = norm;
  const float gb = s.gn + (norm - s.gn) * clampf(fn / (0.008f * kSr), 0.0f, 1.0f);
  const float gstep = (gb - s.gn) / fn;
  const float to_f = 1.0f / 32768.0f;
  const int hmax = kWave[w].hmax;
  float x[kSub];
  for (int i = 0; i < kVoices; ++i) {
    SVoice& v = s.v[i];
    if (!v.alive && !v.trig) continue;
    if (v.trig) {                       // its start: the envelope from where it is, the sweep's, its cycles
      v.trig = 0;
      v.a0 = v.env;
      v.att = 0.0f;
      v.dec = 1.0f;
      v.let_go = 0;
      v.alive = 1;
      v.t = 0.0f;
      v.swe = P.sweep == SW_FALL ? 1.0f : 0.0f;
      v.fr[0] = static_cast<uint8_t>(near_made(s.made, start, nf));
      v.fr[1] = v.fr[0];
      v.fr[2] = static_cast<uint8_t>(near_made(s.made, nf - 1, nf));
      for (int l = 0; l < 3; ++l) v.xf[l] = 0, v.was[l] = v.fr[l];
    }
    for (int l = 0; l < 3; ++l) {       // (a program's change under it: its cycles in the new bank's)
      if (v.fr[l] >= nf) v.fr[l] = static_cast<uint8_t>(nf - 1);
      if (v.was[l] >= nf) v.was[l] = v.fr[l], v.xf[l] = 0;
    }
    const float f = note_hz(v.note + vib);
    const uint32_t inc1 = inc_of(f, periods);
    const uint32_t inc2 = inc_of(note_hz(v.note + static_cast<float>(P.l2_semi) + v.sign * det + vib), periods);
    const uint32_t inc3 = inc_of(f * 2.0f, periods);
    const float kf = 1.0f / (E.D * 0.33f * kSr), kr = 1.0f / (E.R * 0.25f * kSr);
    const float ainc = 1.0f / (E.A * kSr), ks = 1.0f / (E.sweep * kSr);
    const float xr = fm_expf(-kr * kSub), xf_ = fm_expf(-kf * kSub), xs_ = fm_expf(-ks * kSub);   // (a sub-block's)
    const float kt = (v.note - 60.0f) * (0.5f / 12.0f);   // (the keyboard: half)
    const float fold = f * static_cast<float>(hmax);       // (its highest harmonic: the poles under where it folds)
    const float cap = fold < 0.5f * kSr ? 18000.0f : clampf(kSr - fold - 2000.0f, 2500.0f, 18000.0f);
    const int four = P.filt == F_LP4;
    for (uint32_t s0 = 0; s0 < n; s0 += kSub) {
      const int m = n - s0 < static_cast<uint32_t>(kSub) ? static_cast<int>(n - s0) : kSub;
      const float fm_ = static_cast<float>(m);
      // the envelope at the sub-block's end: the attack, the fall to the sustain, the release
      const float e0 = v.env;
      float e1;
      if (v.att < 1.0f && !v.let_go) {
        v.att += ainc * fm_;
        const float a = v.att < 1.0f ? v.att : 1.0f;
        e1 = v.a0 + (1.0f - v.a0) * a * (2.0f - a);
        v.dec = e1;
      } else if (v.let_go) {
        v.dec *= m == kSub ? xr : fm_expf(-kr * fm_);
        e1 = v.dec;
      } else {
        v.dec = E.S + (v.dec - E.S) * (m == kSub ? xf_ : fm_expf(-kf * fm_));
        e1 = v.dec;
      }
      v.env = e1;
      v.t += fm_ / kSr;
      // the sweep (a pluck's falls to INDEX, a pad's rises from it), MOTION on it: the cycle wanted
      const float sp = m == kSub ? xs_ : fm_expf(-ks * fm_);
      v.swe = P.sweep == SW_FALL ? v.swe * sp : 1.0f - (1.0f - v.swe) * sp;
      const float wp = clampf(base + sweep * top * v.swe + depth * lfo, 0.0f, top);
      int want = static_cast<int>(wp + 0.5f);
      if (v.age == s.clock) s.aim = static_cast<uint8_t>(want);
      int want2 = want + P.l2_at;
      want2 = want2 > nf - 1 ? 2 * (nf - 1) - want2 : want2;   // (folded back at the end)
      want2 = want2 < 0 ? 0 : want2;
      if (s.made != (loop ? 0xFFu : 0xFFFFFFFFu)) {
        want = near_made(s.made, want, nf);
        want2 = near_made(s.made, want2, nf);
      }
      for (int qq = 0; qq < m; ++qq) x[qq] = 0.0f;
      layer(v, 0, want, bank, len_bits, inc1, to_f, x, m);
      if (l2 > 0.001f) layer(v, 1, want2, bank, len_bits, inc2, l2 * to_f, x, m);
      if (l3 > 0.001f && P.l3 != L3_NONE) {
        if (P.l3 == L3_TICK) {          // the bright end an octave up, 25 ms
          const float te = l3 * fm_expf(-v.t * 40.0f);
          if (te > 0.002f) layer(v, 2, v.fr[2], bank, len_bits, inc3, te * to_f, x, m);
        } else if (P.l3 == L3_NOISE) {  // a breath: its chiff, a little of it held
          const float ne = l3 * (0.5f * fm_expf(-v.t * 25.0f) + 0.08f * e1);
          for (int qq = 0; qq < m; ++qq) x[qq] += ne * bip(s.rnd);
        } else {                        // a ring: the layers by a sine 3.7 times over, 70 ms
          const float me = l3 * fm_expf(-v.t * 14.0f);
          if (me > 0.002f) {
            const uint32_t rinc = inc_of(f * 3.7f, 1.0f);
            for (int qq = 0; qq < m; ++qq) {
              v.ph[2] += rinc;
              x[qq] += me * x[qq] * lsin(v.ph[2] >> 23);
            }
          }
        }
      }
      // OTIS' poles (one-poles, no resonance): two low-pass or four, the high-pass one; the cutoff COLOR's, the
      // keyboard's, the envelope's, under the cycle's fold
      const float lp_hz = clampf(160.0f * fm_exp2f(lp_oct + kt + P.fenv * e1), 20.0f, cap);
      const float lk = 1.0f - fm_expf(-6.2831853f * lp_hz / kSr);
      float s1 = v.s1, s2 = v.s2, s3 = v.s3, s4 = v.s4, h1 = v.h1;
      const float g0 = s.gn + gstep * static_cast<float>(s0), g1 = g0 + gstep * fm_;
      float e = e0 * g0;
      const float de = (e1 * g1 - e0 * g0) / fm_;
      float* y = out + s0;
      for (int qq = 0; qq < m; ++qq) {
        s1 += lk * (x[qq] - s1);
        s2 += lk * (s1 - s2);
        float o = s2;
        if (four) {
          s3 += lk * (s2 - s3);
          s4 += lk * (s3 - s4);
          o = s4;
        }
        h1 += hp_k * (o - h1);          // (the high-pass: what its low-pass leaves)
        y[qq] += (o - h1) * e;
        e += de;
      }
      v.s1 = s1, v.s2 = s2, v.s3 = s3, v.s4 = s4, v.h1 = h1;
    }
    if (v.env < 1e-5f && (v.let_go || (v.att >= 1.0f && E.S <= 0.0f))) v.alive = 0, v.env = 0.0f;
  }
  s.gn = gb;
  s.hold -= static_cast<int32_t>(n);
  // SPACE: a chorus (0 .. 50 %: two taps 7 .. 16 ms, moving), then the program's: the chorus deeper and a phaser,
  // a comb on the note (a metal's, 3 ms or more, fed back), a drive
  int16_t* line = bank + kFrames * kLen;
  const int fx = space > 0.001f;
  if (fx && !s.fx_on)                   // (SPACE up from nothing: its line silent, nothing of before in it)
    for (uint32_t i = 0; i < kLine; ++i) line[i] = 0;
  s.fx_on = static_cast<uint8_t>(fx);
  const float ch = space < 0.5f ? space * 2.0f : 1.0f, second = space > 0.5f ? (space - 0.5f) * 2.0f : 0.0f;
  const float chmix = 0.5f * ch * (P.space == SP_PHASER ? 1.0f + 0.3f * second : 1.0f);
  const float cinc = (0.31f + (P.space == SP_PHASER ? 0.4f * second : 0.0f)) / kSr;
  {                                     // the comb's delay: whole periods of the note, 3 ms or more
    const float per = kSr / note_hz(s.note);
    float d = per;
    while (d < 132.0f) d += per;
    d = d > static_cast<float>(kLine) - 4.0f ? static_cast<float>(kLine) - 4.0f : d;
    s.cd = s.cd <= 0.0f ? d : s.cd + (d - s.cd) * clampf(fn / 441.0f, 0.0f, 1.0f);
  }
  const float fb = 0.72f * second, drive = 1.0f + 5.0f * second;
  const int comb = P.space == SP_COMB && second > 0.0f, phaser = P.space == SP_PHASER && second > 0.0f;
  const float wet_g = chmix * 0.5f * to_f, dry_g = 1.0f - 0.5f * chmix, comb_q = s.cd * 65536.0f;
  auto tap = [&](int32_t dq) {          // (the line dq / 65536 samples back, between two of them: unscaled)
    const uint32_t a = (s.lw - static_cast<uint32_t>(dq >> 16)) & (kLine - 1u), b = (a - 1u) & (kLine - 1u);
    const int32_t x0 = line[a], x1 = line[b], fr = (dq >> 1) & 0x7FFF;
    return static_cast<float>(x0 + (((x1 - x0) * fr) >> 15));
  };
  for (uint32_t s0 = 0; s0 < n; s0 += kSub) {   // (the chorus' delays, the phaser's turn: each 16 samples, between)
    const int m = n - s0 < static_cast<uint32_t>(kSub) ? static_cast<int>(n - s0) : kSub;
    int32_t d1 = 0, d2 = 0, dd1 = 0, dd2 = 0;
    float c = 0.0f;
    if (fx) {
      const float ms = kSr * 0.001f * 65536.0f, c0 = s.cph;
      s.cph += cinc * static_cast<float>(m);
      s.cph -= s.cph >= 1.0f ? 1.0f : 0.0f;
      d1 = static_cast<int32_t>((7.0f + 4.5f * (1.0f + sin_cyc(c0))) * ms);
      d2 = static_cast<int32_t>((9.0f + 3.5f * (1.0f + sin_cyc(c0 + 0.37f))) * ms);
      dd1 = (static_cast<int32_t>((7.0f + 4.5f * (1.0f + sin_cyc(s.cph))) * ms) - d1) / m;
      dd2 = (static_cast<int32_t>((9.0f + 3.5f * (1.0f + sin_cyc(s.cph + 0.37f))) * ms) - d2) / m;
      c = 0.35f + 0.45f * sin_cyc(c0 * 0.5f) * second;
    }
    for (int q = 0; q < m; ++q) {
      const uint32_t i = s0 + static_cast<uint32_t>(q);
      const float yv = out[i], yd = yv - s.dcx + 0.9993f * s.dcy;   // (the coupling capacitor, the voices' sum)
      s.dcx = yv;
      s.dcy = yd;
      float o = yd;
      if (fx) {
        float in = o;
        const float wet = (tap(d1) + tap(d2)) * wet_g;
        d1 += dd1, d2 += dd2;
        if (comb) {                     // (the comb: its tap fed back, damped)
          s.comb += 0.55f * (tap(static_cast<int32_t>(comb_q)) * to_f - s.comb);
          in = o + fb * s.comb;
          o = o + second * 0.7f * s.comb;
        }
        o = o * dry_g + wet;
        if (phaser) {                   // (the phaser: two all-passes swept by the chorus' LFO)
          float z = o;
          for (int qq = 0; qq < 2; ++qq) {
            const float yq = -c * z + s.ap[qq * 2] + c * s.ap[qq * 2 + 1];
            s.ap[qq * 2] = z;
            s.ap[qq * 2 + 1] = yq;
            z = yq;
          }
          o = o + 0.5f * second * z;
        }
        s.lw = (s.lw + 1u) & (kLine - 1u);
        line[s.lw] = static_cast<int16_t>(clampf(in, -0.999f, 0.999f) * 32767.0f);
      }
      if (P.space == SP_DRIVE && second > 0.0f) {   // (the drive: into a soft clip, its top a little darker)
        const float dv = sat(o * drive) / (0.6f + 0.4f * drive) * (1.0f + 0.4f * second);
        s.drv += 0.6f * (dv - s.drv);
        o = o + (s.drv - o) * second;
      }
      float ay = fm_fabsf(o);           // a soft knee above 0.8
      if (ay > 0.8f) {
        const float z = (ay - 0.8f) * 2.5f;
        ay = 0.8f + 0.4f * z / (1.0f + z);
        o = o < 0.0f ? -ay : ay;
      }
      out[i] = o;
    }
  }
}

const char* sq_knob_name(int k) {
  static const char* const kN[8] = {"Wave", "Index", "Sweep", "Motion", "Color", "Shape", "Stack", "Space"};
  return k >= 0 && k < 8 ? kN[k] : "";
}

void sq_defaults(float* k) {            // E.Piano, a little way in, a fall, a little motion, open, keys, two layers,
  static const float kD[8] = {3.5f / 24.0f, 0.3f, 0.7f, 0.25f, 0.55f, 0.45f, 0.3f, 0.25f};   // a chorus
  for (int i = 0; i < 8; ++i) k[i] = kD[i];
}

int sq_knob_steps(int k) { return k == 0 ? kProgs : 128; }
int sq_progs(void) { return kProgs; }
const char* sq_prog_name(float wave) { return kProg[prog_of(wave)].name; }
const char* sq_family(float wave) {
  static const char* const kF[8] = {"Grit Bass", "Electric", "Metallic", "Glass", "Voices", "Evolving Pad",
                                    "Industrial", "Percussive"};
  return kF[prog_of(wave) / 3];
}

// the cycle the newest voice reads (none: INDEX's), n points of one period into y (-1..1); its place, and the
// program's (place + 32 x program), -1 none made yet
int sq_cycle(int t, float* y, int n, int* of) {
  if (t < 0 || t >= MF_TRACKS) return -1;
  const Sq& s = sq[t];
  if (s.wave < 0 || s.wave >= W_N || !s.made) return -1;
  uint32_t bytes = 0, epoch = 0;
  const int16_t* b = static_cast<const int16_t*>(mf_voice_mem(t, &bytes, &epoch));
  if (!b || s.epoch != epoch || bytes < kBankBytes) return -1;
  const int w = s.wave, nf = looped(w) ? kLoops : kFrames;
  int f = -1;
  for (int i = 0; i < kVoices; ++i)
    if (s.v[i].alive && s.v[i].age == s.clock) f = s.v[i].fr[0];
  if (f < 0 || f >= nf) f = static_cast<int>(clampf(s.k[1], 0.0f, 1.0f) * static_cast<float>(nf - 1) + 0.5f);
  f = near_made(s.made, f, nf);
  const int16_t* c = b + f * (looped(w) ? kLoopLen : kLen);
  for (int i = 0; i < n; ++i) y[i] = static_cast<float>(c[i * kLen / n]) * (1.0f / 32768.0f);
  if (of) *of = nf;
  return f + 32 * prog_of(s.k[0]);
}

static void put_pc(char* b, int n, int* at, float u) {
  put_i(b, n, at, static_cast<int>(u * 100.0f + 0.5f));
  put_s(b, n, at, "%");
}

MI_COLD void sq_knob_text(int k, float u, float wave, char* b, int n) {
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  switch (k) {
    case 0: return put_s(b, n, &at, kProg[prog_of(u)].name);
    case 2: {                           // SWEEP: up or down
      const int x = static_cast<int>((2.0f * u - 1.0f) * 100.0f + (u < 0.5f ? -0.5f : 0.5f));
      if (x > 0) put_s(b, n, &at, "+");
      put_i(b, n, &at, x);
      return put_s(b, n, &at, "%");
    }
    case 4:                             // COLOR: dark, open, thin (the value in the ring, its word under it)
      if (u < 0.57f) return put_pc(b, n, &at, (0.6f - u) / 0.6f), put_s(b, n, &at, " Dark");
      if (u <= 0.63f) return put_s(b, n, &at, "Open");
      return put_pc(b, n, &at, (u - 0.6f) / 0.4f), put_s(b, n, &at, " Thin");
    case 5: {                           // SHAPE: what it makes
      static const char* const kS[5] = {"Perc", "Pluck", "Keys", "Swell", "Pad"};
      const int i = static_cast<int>(u * 5.0f);
      return put_s(b, n, &at, kS[i > 4 ? 4 : i]);
    }
    case 6:                             // STACK: its layers
      if (u < 0.01f) return put_s(b, n, &at, "Off");
      put_pc(b, n, &at, u <= 0.5f ? u * 2.0f : (u - 0.5f) * 2.0f);
      return put_s(b, n, &at, u <= 0.5f ? " 2nd" : " 3rd");
    case 7: {                           // SPACE: the chorus, then the program's
      static const char* const kN[3] = {" +Phaser", " +Comb", " +Drive"};
      if (u < 0.01f) return put_s(b, n, &at, "Dry");
      if (u <= 0.5f) return put_pc(b, n, &at, u * 2.0f), put_s(b, n, &at, " Chorus");
      return put_pc(b, n, &at, (u - 0.5f) * 2.0f), put_s(b, n, &at, kN[kProg[prog_of(wave)].space]);
    }
    default:
      break;
  }
  put_pc(b, n, &at, u);
}

}  // extern "C"
