// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's METAL reverb (0.16.41; the reverb's ALGORITHM: CLOUDS, METAL, SHIMMER), after what Make Noise's Erbe-Verb
// does (its sizes from a can to a hall, its tails moving, its infinite decay), MIFM1's own design: a feedback delay
// network of eight lines, an orthogonal mix between them (a Hadamard matrix: each line's energy to all eight, none
// lost), each line read through a slow swaying tap (its length moving: the tail never still), its loss a gain for its
// length (every line dies at the same rate) and Jot's one-pole (the highs die faster than the lows by as much
// whatever the size: what the walls absorb). Before it, a low cut and four all-passes thicken the input.
//   At half the rate (22.05 kHz: half the work, the lines twice as long; its tail stops under 11 kHz, as the old
// digital reverbs' did), in Clouds' reverb's memory: 16384 cells of 16 bits, one pointer for all the lines (as Clouds'
// FxEngine does), the lines its segments; each cell's rounding error carried to the line's next one (the tails decay as
// set down to a few steps, then die).
//   DECAY: 0.3 s to 30 s, then FREEZE at its end: the lines lossless, the input closed, the tail held as it was.
//   SIZE: the lines from 1.5 to 2.9 ms (a pipe, a tank: their resonances heard, metallic) to 50 to 96 ms (a vast
//     hall); turned, the taps slide, the pitch bends as on a tape.
//   TONE: from the middle to the left, the highs die faster (to 16 times the lows' rate: a cave); to the right, as
//     fast as the lows, less diffusion and less sway: the lines' own resonances stand out (metal, industrial).
//   SHIMMER (its ALGORITHM): the tail an octave up fed back in (a pitch shifter: two taps sliding through a 93 ms
//     line, crossfaded), the octaves climbing as it rings; what it takes under ~4 kHz, what it gives back over
//     ~150 Hz, its loop's gain held under 1 at any decay (600 settings tried, an input 40 times too loud: all die).
// Fully wet (a send): the engine sets the return.
#ifndef MIFM1_MI_METAL_H_
#define MIFM1_MI_METAL_H_
#include <stdint.h>
#include "fastmath.h"

// (its code in the FM-1's flash, every byte counted: the per-block work and the settings small (minsize), the
// per-sample loops not unrolled, eight lines or four all-passes a loop)
#if defined(__clang__)
#define METAL_ROLLED _Pragma("clang loop unroll(disable)")
#else
#define METAL_ROLLED
#endif
#define METAL_SMALL __attribute__((noinline, minsize))

namespace mifm1 {

namespace metal {
const float kFs = 22050.0f;             // its rate: half the FM-1's
const uint32_t kAp[4] = {71, 53, 191, 139};   // the input's all-passes (samples: 3.2, 2.4, 8.7, 6.3 ms)
const uint32_t kLen[8] = {1103, 1249, 1427, 1583, 1693, 1823, 1979, 2111};   // the lines at full SIZE (50 .. 96 ms)
const float kRate[8] = {0.13f, 0.17f, 0.23f, 0.29f, 0.37f, 0.43f, 0.53f, 0.71f};   // their sways (Hz)
const uint32_t kModRoom = 16;           // a line's room for its tap's sway past its length
const uint32_t kShift = 2048;           // the shimmer's line (93 ms)
const float kShiftP = 2046.0f;          // its taps' round (a cell short of it each end: the interpolation's)
const float kOut = 0.71f;               // the tail's level: as loud as Clouds' at the same send and T60 (SIZE's middle;
                                        // octave bands of noise, 125 Hz to 4 kHz)
}  // namespace metal

class MetalVerb {
 public:
  static const int kLines = 8;
  static const uint32_t kMask = 16383u;

  METAL_SMALL void Init(int16_t* buffer) {
    using namespace metal;
    buf_ = buffer;
    for (uint32_t i = 0; i <= kMask; ++i) buf_[i] = 0;
    ptr_ = 0;
    uint32_t at = 0;
    for (int i = 0; i < 4; ++i) ap_at_[i] = at, at += kAp[i] + 1u;
    for (int i = 0; i < kLines; ++i) {
      line_at_[i] = at, at += kLen[i] + kModRoom;
      lp_[i] = hp_[i] = ay_[i] = le_[i] = 0.0f, ph_[i] = static_cast<float>(i) * 0.125f;
    }
    sh_at_ = at;                          // the shimmer's line: the rest (2048 and more)
    in_hp1_ = in_hp2_ = prev_in_ = prev_l_ = prev_r_ = sh_hp1_ = sh_hp2_ = sh_lp_ = 0.0f;
    in_gain_ = 1.0f;
    sh_d_ = 1.0f;
    Set(0.5f, 0.5f, 0.5f, 0);
    scale_ = scale_aim_;
  }

  static float Decay(float decay) {     // DECAY's T60 (s; its lows'): 0.3 s .. 30 s (100 x), FREEZE from 0.97
    decay = clamp01(decay);
    return 0.3f * fm_exp2f(decay * (6.643856f / 0.97f));
  }
  static int Frozen(float decay) { return decay >= 0.97f; }
  static float Scale(float size) {      // SIZE: the lines' lengths over their longest
    size = clamp01(size);
    return 0.03f + 0.97f * size * size;
  }
  static float SizeMs(float size) { return Scale(size) * (1000.0f * 1621.0f / metal::kFs); }   // (their mean)

  // decay, size, tone 0..1; shimmer 0 or 1
  METAL_SMALL void Set(float decay, float size, float tone, int shimmer) {
    decay = clamp01(decay), size = clamp01(size), tone = clamp01(tone);
    freeze_ = Frozen(decay);
    t60_ = Decay(decay);
    in_level_ = 1.0f - 0.45f * decay;     // (the long tails a little less fed: they pile up)
    scale_aim_ = Scale(size);
    hf_ = 0.0625f * fm_exp2f(tone * 4.0f);   // the highs' T60 over the lows': 1/16 .. 1
    diff_ = tone > 0.5f ? 0.7f - 0.5f * (tone - 0.5f) : 0.7f;
    sway_ = tone > 0.5f ? 1.0f - 1.6f * (tone - 0.5f) : 1.0f;
    shimmer_ = shimmer;
  }

  // n samples (even, <= 128) of a mono send into a stereo tail
  void Process(const float* in, float* out_l, float* out_r, uint32_t n) {
    using namespace metal;
    const uint32_t m = n / 2u;          // (its samples: half as many)
    const float s1 = Prepare(m);
    // the shimmer's share: the tail against what comes in grows as 1 / sqrt(1 - g^2), the feed scaled by its
    // inverse (the loop's gain under 1); none while frozen (what rings holds its octaves already)
    const float gp = gp_;
    const float shim = shimmer_ && !freeze_ ? 1.8f * fm_sqrtf(1.0f - (gp < 1.0f ? gp : 1.0f)) : 0.0f;
    const float in_aim = freeze_ ? 0.0f : in_level_, kd = diff_;
    // its level: the lines' energy is what came in times how long it lasts, whatever their lengths, read 8 cells a
    // sample out of all of theirs: the small sizes louder, as 1 / sqrt(size); held as at SIZE's middle
    const float og = kOut * fm_sqrtf(s1 * (1.0f / 0.379f));
    const float* const g = g_;
    const float* const eta = eta_;
    const float* const pb = pb_;
    const float* const dc = dc_;
    const uint32_t* const ti = ti_;
    float hl[64], hr[64];
    for (uint32_t k = 0; k < m; ++k) {
      in_gain_ += (in_aim - in_gain_) * 0.001f;   // (the input closed or opened over ~50 ms)
      // the input: two samples into one (a [1 2 1] / 4 low pass), under 50 Hz out, four all-passes
      const float a0 = in[2u * k], a1 = in[2u * k + 1u];
      float x = (prev_in_ + 2.0f * a0 + a1) * 0.25f * in_gain_;
      prev_in_ = a1;
      in_hp1_ += 0.014f * (x - in_hp1_);
      x -= in_hp1_;
      in_hp2_ += 0.014f * (x - in_hp2_);
      x -= in_hp2_;
      METAL_ROLLED
      for (int i = 0; i < 4; ++i) {
        const float dd = rd(ap_at_[i] + kAp[i]);
        const float w = x + kd * dd;
        wt(ap_at_[i], w);
        x = dd - kd * w;
      }
      if (shim > 0.0f) {                // the shimmer: the last tail an octave up, back in, its lows cut (~150 Hz,
        float o = shift();              // two poles: an octave up, the lows stay low and DC stays DC, the shifter's
        sh_hp1_ += 0.0418f * (o - sh_hp1_);   // crossfade (~11 Hz) around it: they would grow for ever)
        o -= sh_hp1_;
        sh_hp2_ += 0.0418f * (o - sh_hp2_);
        x += shim * (o - sh_hp2_);
      }
      // the lines: read, absorbed, mixed (Hadamard), their loss, written back with the input and a DC blocker (~3 Hz:
      // what is still, all the lines in phase, would ring 1 / (1 - g) times louder than the rest, a drift)
      float y[kLines], v[kLines];
      METAL_ROLLED
      for (int i = 0; i < kLines; ++i) {
        y[i] = eta[i] * (rd(ti[i]) - ay_[i]) + rd(ti[i] + 1u);
        ay_[i] = y[i];
        lp_[i] = y[i] + pb[i] * (lp_[i] - y[i]);
        v[i] = lp_[i];
      }
      hadamard(v);
      const float xi = x * 0.35355339f;
      METAL_ROLLED
      for (int i = 0; i < kLines; ++i) {
        const float w = g[i] * v[i] * 0.35355339f + ((0x5Au >> i) & 1u ? -xi : xi);   // (the input's signs)
        hp_[i] += dc[i] * (w - hp_[i]);
        wr(line_at_[i], soft(w - hp_[i]), &le_[i]);
      }
      // out: two orthogonal sums (decorrelated sides)
      const float l = (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]) * 0.35355339f;
      const float r = (y[0] + y[1] - y[2] - y[3] + y[4] + y[5] - y[6] - y[7]) * 0.35355339f;
      if (shimmer_) {                   // (into the shifter under ~4 kHz: its octave under the top, not folded back)
        sh_lp_ += 0.68f * (l + r - sh_lp_);
        wt(sh_at_, sh_lp_);
      }
      hl[k] = l * og;
      hr[k] = r * og;
      ptr_ = (ptr_ - 1u) & kMask;
    }
    for (uint32_t k = 0; k < m; ++k) {    // back to the FM-1's rate: each half-rate sample, and halfway to it before
      out_l[2u * k] = 0.5f * (prev_l_ + hl[k]);
      out_r[2u * k] = 0.5f * (prev_r_ + hr[k]);
      out_l[2u * k + 1u] = prev_l_ = hl[k];
      out_r[2u * k + 1u] = prev_r_ = hr[k];
    }
    METAL_ROLLED
    for (int i = 0; i < kLines; ++i) {    // (the filters' states flushed under -300 dB: no denormals)
      lp_[i] = fm_fabsf(lp_[i]) < 1e-15f ? 0.0f : lp_[i];
      hp_[i] = fm_fabsf(hp_[i]) < 1e-15f ? 0.0f : hp_[i];
      ay_[i] = fm_fabsf(ay_[i]) < 1e-15f ? 0.0f : ay_[i];
    }
    in_hp1_ = fm_fabsf(in_hp1_) < 1e-15f ? 0.0f : in_hp1_;
    in_hp2_ = fm_fabsf(in_hp2_) < 1e-15f ? 0.0f : in_hp2_;
    sh_hp1_ = fm_fabsf(sh_hp1_) < 1e-15f ? 0.0f : sh_hp1_;
    sh_hp2_ = fm_fabsf(sh_hp2_) < 1e-15f ? 0.0f : sh_hp2_;
    sh_lp_ = fm_fabsf(sh_lp_) < 1e-15f ? 0.0f : sh_lp_;
    if (!(fm_fabsf(lp_[0]) < 1e3f)) Init(buf_);   // (a NaN or a runaway: silence, never noise)
  }

  // the block's: the size slewed (~0.1 s); each line's sway, its tap, its gain and its absorption; the lines' mean
  // gain squared (gp_); the size's scale, the block's end's
  METAL_SMALL float Prepare(uint32_t m) {
    using namespace metal;
    const float s0 = scale_;
    scale_ += (scale_aim_ - scale_) * (1.0f - fm_expf(-static_cast<float>(m) / 2205.0f));
    const float s1 = scale_;
    float gp = 0.0f;
    for (int i = 0; i < kLines; ++i) {
      const float len1 = static_cast<float>(kLen[i]) * s1;
      const float depth = fm_minf(6.0f, 0.05f * len1) * sway_;
      const float m0 = depth * fm_sin_turns(ph_[i]);
      ph_[i] += kRate[i] * static_cast<float>(m) / kFs;
      ph_[i] -= ph_[i] >= 1.0f ? 1.0f : 0.0f;
      const float m1 = depth * fm_sin_turns(ph_[i]);
      // (the block's tap, its middle's: whole cells, and Thiran's all-pass for the fraction (f in 0.1 .. 1.1,
      // (1 - f) / (1 + f)): every frequency as loud, where a linear one loses the highs at each pass, the tail
      // darkening and the freeze fading as it goes; the sway moves it a tenth of a cell a block at most)
      const float d = 0.5f * (static_cast<float>(kLen[i]) * s0 + m0 + len1 + m1);
      int32_t di = static_cast<int32_t>(d);
      float f = d - static_cast<float>(di);
      if (f < 0.1f) f += 1.0f, --di;
      ti_[i] = line_at_[i] + static_cast<uint32_t>(di);
      eta_[i] = (1.0f - f) / (1.0f + f);
      const float lg = -3.0f * d / (kFs * t60_);   // log10 of its gain: 10^(-3 L / (fs T60))
      g_[i] = freeze_ ? 1.0f : fm_exp2f(3.3219281f * lg);
      // its one-pole: 1 at DC, at the top (11 kHz) the gain that makes its T60 hf times the lows', 10^(lg (1/hf - 1)):
      // (1 - b) / (1 + b) = r (Jot's b, ln10 / 4 log10(g) (1 - 1 / hf^2), its first order: past 1 for long lines and
      // short tails, all the mids gone)
      const float r = freeze_ ? 1.0f : fm_exp2f(3.3219281f * lg * (1.0f / hf_ - 1.0f));
      pb_[i] = (1.0f - r) / (1.0f + r);
      // its DC blocker's pole: its loss at a low frequency a pass, (fc / f)^2 / 2, times the passes a second (fs / L):
      // its corner as sqrt(L) (the same loss a second at any size: the small sizes' lows as long as the large ones'),
      // 0.4 Hz at the smallest, 3 Hz at the largest
      dc_[i] = 0.00002f * fm_sqrtf(d);
      gp += g_[i] * g_[i];
    }
    gp_ = gp * (1.0f / static_cast<float>(kLines));
    return s1;
  }

 private:
  static float clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
  static float soft(float x) {          // a soft limit from 1.5, never past 2 (the cells' range)
    const float a = fm_fabsf(x);
    return a < 1.5f ? x : (x > 0.0f ? 1.0f : -1.0f) * (1.5f + 0.5f * (a - 1.5f) / (0.5f + (a - 1.5f)));
  }
  float rd(uint32_t at) const { return static_cast<float>(buf_[(ptr_ + at) & kMask]) * (1.0f / 16384.0f); }
  // a cell written, its rounding's error carried to that line's next one (error feedback: on average the line holds
  // what it was given, to a fraction of a step; rounded or truncated alone, a tail under a few hundred steps lost or
  // kept a step a pass, its decay cut short as it went quiet, or held in a limit cycle). Under 2 steps, truncated
  // toward 0, the error dropped: the last of a tail dies (at -75 to -95 dB) instead of a hiss at a step's level going
  // round for ever
  void wr(uint32_t at, float v, float* e) {
    v = v * 16384.0f + *e;
    v = v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v;
    int32_t q;
    if (fm_fabsf(v) < 2.0f) {
      q = static_cast<int32_t>(v);
      *e = 0.0f;
    } else {
      q = static_cast<int32_t>(v + (v < 0.0f ? -0.5f : 0.5f));
      *e = v - static_cast<float>(q);
    }
    buf_[(ptr_ + at) & kMask] = static_cast<int16_t>(q);
  }
  // a cell of the all-passes or the shimmer's line, truncated (toward 0: no limit cycle; their errors heard once,
  // not a pass a pass for as long as the tail lasts)
  void wt(uint32_t at, float v) {
    v *= 16384.0f;
    v = v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v;
    buf_[(ptr_ + at) & kMask] = static_cast<int16_t>(static_cast<int32_t>(v));
  }
  float tap(uint32_t at, float d) const {   // a line read d samples back (d >= 1), linear between two cells
    const int32_t i = static_cast<int32_t>(d);
    const float f = d - static_cast<float>(i);
    const float a = rd(at + static_cast<uint32_t>(i)), b = rd(at + static_cast<uint32_t>(i) + 1u);
    return a + (b - a) * f;
  }
  static void hadamard(float* v) {      // 8 x 8, unnormalised (the 1 / sqrt(8) at the write), its butterflies written out
    float a0 = v[0] + v[1], a1 = v[0] - v[1], a2 = v[2] + v[3], a3 = v[2] - v[3];
    float a4 = v[4] + v[5], a5 = v[4] - v[5], a6 = v[6] + v[7], a7 = v[6] - v[7];
    float b0 = a0 + a2, b2 = a0 - a2, b1 = a1 + a3, b3 = a1 - a3;
    float b4 = a4 + a6, b6 = a4 - a6, b5 = a5 + a7, b7 = a5 - a7;
    v[0] = b0 + b4, v[4] = b0 - b4, v[1] = b1 + b5, v[5] = b1 - b5;
    v[2] = b2 + b6, v[6] = b2 - b6, v[3] = b3 + b7, v[7] = b3 - b7;
  }
  float shift() {                       // the shimmer's line an octave up: two taps sliding, half a round apart,
    using namespace metal;              // crossfaded (a triangle each, their sum 1); read twice as fast as written
    sh_d_ -= 1.0f;                      // (the delay shortens a sample a sample), each tap 1 .. 2047 back, its
    if (sh_d_ < 1.0f) sh_d_ += kShiftP; // weight 0 at both ends
    float d2 = sh_d_ + 0.5f * kShiftP;
    if (d2 >= kShiftP + 1.0f) d2 -= kShiftP;
    const float w1 = 1.0f - fm_fabsf((sh_d_ - 1.0f) * (2.0f / kShiftP) - 1.0f);
    const float a = tap(sh_at_, sh_d_), b = tap(sh_at_, d2);
    return b + (a - b) * w1;
  }

  int16_t* buf_;
  uint32_t ptr_, ap_at_[4], line_at_[kLines], sh_at_;
  float lp_[kLines], hp_[kLines], ph_[kLines], ay_[kLines], le_[kLines];
  float scale_, scale_aim_, t60_, hf_, diff_, sway_, in_gain_, in_level_, sh_d_, in_hp1_, in_hp2_;
  float prev_in_, prev_l_, prev_r_, sh_hp1_, sh_hp2_, sh_lp_;
  float g_[kLines], eta_[kLines], pb_[kLines], dc_[kLines], gp_;   // (the block's)
  uint32_t ti_[kLines];
  int freeze_, shimmer_;
};

}  // namespace mifm1

#endif  // MIFM1_MI_METAL_H_
