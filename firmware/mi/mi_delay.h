// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's delay, after the knobs of Erica Synths' Zen Delay (its sound is its own): a line of 16-bit samples, and in
// its loop Mutable's state-variable filter (stmlib's Svf: low pass to the left of FILTER, high pass to the right,
// nothing in the middle) and a saturation (DRIVE: clean up to half the scale, then a soft knee to it; 0.4.6 had
// stmlib's SoftClip, which also squeezed the quiet repeats a little at each pass). Its time follows the tempo; when it
// changes, the read glides to it (a tape's pitch bend). MOD is a slow wow on the time. Mono: one echo every TIME;
// ping-pong: left at TIME, right at twice, back and forth (one line read at TIME and 2 x TIME, fed back from the
// second). Its loop never gains: FEEDBACK at full is a loop (the repeats held as they are, for good), DRIVE's gain is
// taken back on what is fed back (it colours, it does not push), and what the resonance would push past a ceiling
// (-8 dB) is held there: the repeats sustain at that level instead of running away.
//
// (0.16.65) BBD: the same line, played as a bucket brigade delay after Electro-Harmonix's Memory Man (two MN3005,
// 8192 stages, its compander an NE570). The clock: its rate the stages over twice the time (8.2 kHz at half a second:
// the long echoes darker, their bandwidth following it); the input through an anti-aliasing filter, then sampled by
// the clock and held (its images, and above half the clock what folds back down: a long echo's grit); after the line,
// the reconstruction filter. DIRT: fewer stages (the clock down to a sixth: grittier), the filters let through more
// of what folds (to past half the clock), the chip's hiss up. The compander: the input compressed 2:1 (its rectifier
// averaged over ~10 ms), the chip's soft saturation, its hiss, then expanded 1:2 after the line: the hiss pushed down
// in silence and breathing with the repeats, the repeats pumping as the expander follows what the compressor never
// saw (the line's loss, the filters). DRIVE: the input harder into the chip (it grinds; the loop's level as it was).
// WIDTH: from mono to a second tap a little later on the right (up to 22 ms: the repeats wide, their modulation the
// other way), then at its end ping-pong (left at TIME, right at twice, as the clean one). MOD: the clock swept by a
// triangle, from a slow chorus (0.35 Hz) to a vibrato (4 Hz), deeper as it goes. FILTER stays in the loop.
#ifndef MIFM1_MI_DELAY_H_
#define MIFM1_MI_DELAY_H_

#include <stdint.h>
#include "stmlib/stmlib.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"

namespace mifm1 {

class Delay {
 public:
  Delay() { }

  void Init(int16_t* line, uint32_t size, float sample_rate) {
    line_ = line;
    size_ = size;
    sr_ = sample_rate;
    for (uint32_t i = 0; i < size; ++i) line_[i] = 0;
    write_ = 0;
    time_ = target_ = static_cast<float>(size) * 0.25f;
    feedback_ = 0.0f;
    env_ = 0.0f;
    drive_ = 0.0f;
    mod_ = 0.0f;
    phase_ = 0.0f;
    ping_pong_ = 0;
    svf_.Init();
    mode_ = 0;
    blend_ = blend_now_ = 0.0f;
    set_filter(0.0f, 0.0f);
    bbd_ = 0;
    dirt_ = width_ = 0.0f;
    ResetBbd();
  }

  // (0.16.65) the BBD: on or not (the clean delay), its DIRT and WIDTH (0..1)
  void set_bbd(int on, float dirt, float width) {
    if (on != bbd_) {                   // (the line at the other's rate: emptied, the BBD from rest)
      for (uint32_t i = 0; i < size_; ++i) line_[i] = 0;
      ResetBbd();
      env_ = 0.0f;
    }
    bbd_ = on;
    dirt_ = dirt < 0.0f ? 0.0f : dirt > 1.0f ? 1.0f : dirt;
    width_ = width < 0.0f ? 0.0f : width > 1.0f ? 1.0f : width;
  }

  // time in samples (clipped to the line: a ping-pong's twice), feedback 0..1 (at the end, 1: the loop), filter -1..1,
  // resonance 0..1, drive 0..1, mod 0..1
  void set(float time, float feedback, float filter, float resonance, float drive, int ping_pong, float mod) {
    // (the BBD: room for its tap on the right and its modulation, 30 ms)
    float max = static_cast<float>(size_ - 4u - (bbd_ ? 1324u : 0u)) * (ping_pong ? 0.5f : 1.0f);
    while (time > max) time *= 0.5f;   // the same division, an octave of time shorter
    target_ = time < 16.0f ? 16.0f : time;
    feedback_ = feedback >= 0.99f ? 1.0f : feedback < 0.0f ? 0.0f : feedback;
    ping_pong_ = ping_pong;
    drive_ = drive;
    mod_ = mod;
    set_filter(filter, resonance);
  }

  void Process(const float* in, float* out_l, float* out_r, uint32_t n) {
    if (bbd_) {
      ProcessBbd(in, out_l, out_r, n);
      return;
    }
    const float kGlide = 0.0004f;       // ~60 ms to a new time
    const float lfo_inc = 0.45f / sr_;  // the wow: under a hertz
    const float kCeil = 0.4f;           // the line's level the loop holds (what would grow is held there)
    const float kAttack = 0.02f;        // the level: up in ~1 ms, down in ~0.5 s
    const float kRelease = 1.0f - 1.0f / (0.5f * 44100.0f);
    float gain = 1.0f + drive_ * 5.0f, makeup = 1.0f / (1.0f + drive_ * 1.5f);
    float fb = feedback_ / (gain * makeup);   // (DRIVE's gain taken back on the loop: FEEDBACK is the loop's gain)
    float env = env_;
    // (0.16.64: its states in locals over the block, put back after it: out_l, out_r might have been them for the
    // compiler, loaded and stored again every sample)
    float time = time_, phase = phase_;
    uint32_t wr = write_;
    const float target = target_, mod = mod_;
    // (0.16.65) FILTER's blend from the last block's, sample by sample (at once, a filter from rest taken in at 70 %:
    // its output still at 0, a step into the loop)
    float blend = blend_now_;
    const float dblend = (blend_ - blend_now_) / static_cast<float>(n);
    const int pp = ping_pong_, mode = mode_;
    int16_t* const line = line_;
    const uint32_t size = size_;
    for (uint32_t i = 0; i < n; ++i) {
      time += (target - time) * kGlide;
      phase += lfo_inc;
      if (phase >= 1.0f) phase -= 1.0f;
      float tri = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
      float t = time * (1.0f + mod * 0.006f * tri);
      float a = Read(line, size, wr, t), b = pp ? Read(line, size, wr, 2.0f * t) : a;
      float x = pp ? b : a, m = x < 0.0f ? -x : x;
      env = m > env ? env + (m - env) * kAttack : env * kRelease;
      float w = in[i] + fb * (env > kCeil ? kCeil / env : 1.0f) * x;
      blend += dblend;
      if (mode == 1) w = Crossfade(w, svf_.Process<stmlib::FILTER_MODE_LOW_PASS>(w), blend);
      else if (mode == 2) w = Crossfade(w, svf_.Process<stmlib::FILTER_MODE_HIGH_PASS>(w), blend);
      w = Knee(w * gain) * makeup;
      int32_t s = static_cast<int32_t>(w * 32767.0f);
      line[wr] = static_cast<int16_t>(s > 32767 ? 32767 : s < -32767 ? -32767 : s);
      if (++wr >= size) wr = 0;
      out_l[i] = a;
      out_r[i] = b;
    }
    env_ = env;
    time_ = time, phase_ = phase, write_ = wr;
    blend_now_ = blend_;
  }

  // (MIFM1 0.16.71) n samples passed asleep (the engine's fx_job: nothing in, its line silent): its clock as Process
  // moves it, the time's glide, the wow's phase, the write position, the level's release, FILTER's blend; 0 (run it)
  // on the BBD, whose state is its own
  int Skip(uint32_t n) {
    if (bbd_) return 0;
    const float kGlide = 0.0004f, kRelease = 1.0f - 1.0f / (0.5f * 44100.0f), lfo_inc = 0.45f / sr_;
    for (uint32_t i = 0; i < n; ++i) {
      time_ += (target_ - time_) * kGlide;
      phase_ += lfo_inc;
      if (phase_ >= 1.0f) phase_ -= 1.0f;
      env_ *= kRelease;
    }
    write_ = (write_ + n) % size_;
    blend_now_ = blend_;
    return 1;
  }

  // the BBD's block (see above), at half the rate (its band under 8 kHz: half the work): the input two samples into
  // one ([1 2 1] / 4), the output each half-rate sample and halfway to it before; the clock's rate and the filters'
  // cutoff from the time at its start
  void ProcessBbd(const float* in, float* out_l, float* out_r, uint32_t n) {
    const float kGlide = 0.0008f;       // (a half-rate sample: ~60 ms to a new time, as the clean one)
    const float kCeil = 0.5f;           // (the loop's level held a little higher than the clean one's: it grinds first)
    const float kRelease = 1.0f - 1.0f / (0.5f * 22050.0f);   // (the loop's level: its peaks at once, down in ~0.5 s)
    const float kUp = 0.0204f, kDown = 0.0030f;   // the compander's rectifiers: up in ~2 ms, down in ~15 ms (a pluck's
                                                    // attack held in sooner: less of it spat through the chip)
    const float kRef = 0.25f;           // its unity level (compressed and expanded alike there)
    const float kNoise = 2.5e-4f * (1.0f + 3.0f * dirt_) * (1.0f / 2147483648.0f);   // the chip's hiss (-72 dB)
    const float hs = 0.5f * sr_;
    float time = 0.5f * time_, phase = phase_;   // (in half-rate samples)
    uint32_t wr = write_;
    const float target = 0.5f * target_;
    float blend = blend_now_;            // (FILTER's blend glided over the block, as the clean one's)
    const float dblend = (blend_ - blend_now_) / static_cast<float>((n + 1u) / 2u);
    const int pp = ping_pong_, mode = mode_;
    int16_t* const line = line_;
    const uint32_t size = size_;
    // the clock: its cycles a half-rate sample, the stages over twice the time; the filters a little under half of
    // it (DIRT: past it), never over 8 kHz; the anti-aliasing one only where the clock is under the rate
    const float stages = 8192.0f * Exp2(-2.6f * dirt_);
    const float ci = stages / (2.0f * (time > 8.0f ? time : 8.0f));
    float fc = (0.36f + 0.66f * dirt_) * ci;
    fc = fc > 8000.0f / hs ? 8000.0f / hs : fc;
    const float g = Tan(3.14159265f * fc), a1 = 1.0f / (1.0f + g * (g + 1.41421356f)), a2 = g * a1, a3 = g * a2;
    const int aa = ci < 1.0f;
    // the modulation: slow and shallow (a chorus) to fast and deep (a vibrato); WIDTH's tap on the right
    const float lfo_inc = 0.35f * Exp2(3.5f * mod_) / hs;
    const float depth = (0.0004f + 0.003f * mod_) * mod_ * hs;
    const float wv = pp ? 0.0f : width_ * (1.0f / 0.9f);
    float off = wv * 0.022f * hs;
    off = off > 0.25f * time ? 0.25f * time : off;
    const int two = pp || wv > 0.0f;
    const float gain = 1.0f + drive_ * 7.0f, igain = 1.0f / gain;
    const float fb = feedback_ * (feedback_ >= 1.0f ? 1.05f : 1.0f);   // (at the end: past the loop, held by the chip)
    float env = env_, ec = bb_ec_, el = bb_el_, er = bb_er_, clk = bb_clk_, held = bb_held_, dcx = bb_dcx_;
    float dcy = bb_dcy_, aa1 = bb_aa_[0], aa2 = bb_aa_[1], l1 = bb_l_[0], l2 = bb_l_[1], r1 = bb_r_[0], r2 = bb_r_[1];
    float prev = bb_in_, pl = bb_pl_, pr = bb_pr_, cg = bb_cg_;
    uint32_t rnd = bb_rnd_;
    for (uint32_t k = 0; 2u * k < n; ++k) {
      const float i0 = in[2u * k], i1 = 2u * k + 1u < n ? in[2u * k + 1u] : i0;
      const float xin = (prev + 2.0f * i0 + i1) * 0.25f;
      prev = i1;
      time += (target - time) * kGlide;
      phase += lfo_inc;
      if (phase >= 1.0f) phase -= 1.0f;
      const float tri = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
      const float tl = time + depth * tri;
      // the taps, their reconstruction filters, the expander (its gain its rectifier's level over the unity's)
      float yl = Lp(Read(line, size, wr, tl), &l1, &l2, a1, a2, a3);
      const float al = yl < 0.0f ? -yl : yl;
      el += (al - el) * (al > el ? kUp : kDown);
      yl *= el * (1.0f / kRef);
      float yr = yl;
      if (two) {
        yr = Lp(Read(line, size, wr, pp ? 2.0f * tl : time + off - depth * tri * wv), &r1, &r2, a1, a2, a3);
        const float ar = yr < 0.0f ? -yr : yr;
        er += (ar - er) * (ar > er ? kUp : kDown);
        yr *= er * (1.0f / kRef);
      }
      // the loop: what is fed back held under its ceiling, FILTER, then the input
      const float x = pp ? yr : yl, m = x < 0.0f ? -x : x;
      env = m > env ? m : env * kRelease;   // (its peaks at once: the compander's mistracking, pass after pass,
                                            // sharpened them and they grew past a slower ceiling)
      float w = xin + fb * (env > kCeil ? kCeil / env : 1.0f) * x;
      blend += dblend;
      if (mode == 1) w = Crossfade(w, svf_.Process<stmlib::FILTER_MODE_LOW_PASS>(w), blend);
      else if (mode == 2) w = Crossfade(w, svf_.Process<stmlib::FILTER_MODE_HIGH_PASS>(w), blend);
      // the compressor (2:1: its gain the root of the unity's over its rectifier's level, 32 dB at most; taken
      // every 2nd sample), the chip pushed (DRIVE: its curve, the level given back), a little lopsided, the
      // anti-aliasing filter, the clock's sample, held; the chip's hiss; a coupling capacitor (its curve's DC)
      const float aw = w < 0.0f ? -w : w;
      ec += (aw - ec) * (aw > ec ? kUp : kDown);
      if (!(k & 1u)) cg = RSqrt((ec > 1.6e-4f ? ec : 1.6e-4f) * (1.0f / kRef));
      float c = Chip(w * cg * gain) * igain;
      if (aa) c = Lp(c, &aa1, &aa2, a1, a2, a3);
      clk += ci;
      if (clk >= 1.0f) {
        clk -= static_cast<float>(static_cast<int32_t>(clk));
        held = c;
      }
      rnd ^= rnd << 13, rnd ^= rnd >> 17, rnd ^= rnd << 5;
      const float h = held + kNoise * static_cast<float>(static_cast<int32_t>(rnd));
      const float d = h - dcx + 0.9972f * dcy;
      dcx = h, dcy = d;
      int32_t q = static_cast<int32_t>(d * 32767.0f);
      line[wr] = static_cast<int16_t>(q > 32767 ? 32767 : q < -32767 ? -32767 : q);
      if (++wr >= size) wr = 0;
      out_l[2u * k] = 0.5f * (pl + yl);
      out_r[2u * k] = 0.5f * (pr + yr);
      if (2u * k + 1u < n) out_l[2u * k + 1u] = yl, out_r[2u * k + 1u] = yr;
      pl = yl, pr = yr;
    }
    env_ = env;
    time_ = 2.0f * time, phase_ = phase, write_ = wr;
    bb_ec_ = ec, bb_el_ = Flush(el), bb_er_ = Flush(er), bb_clk_ = clk, bb_held_ = held, bb_dcx_ = dcx;
    bb_dcy_ = Flush(dcy), bb_aa_[0] = Flush(aa1), bb_aa_[1] = Flush(aa2), bb_l_[0] = Flush(l1), bb_l_[1] = Flush(l2);
    bb_r_[0] = Flush(r1), bb_r_[1] = Flush(r2), bb_rnd_ = rnd, bb_in_ = prev, bb_pl_ = Flush(pl), bb_pr_ = Flush(pr);
    bb_cg_ = cg;
    blend_now_ = blend_;
  }

 private:
  void ResetBbd() {
    bb_ec_ = bb_el_ = bb_er_ = bb_clk_ = bb_held_ = bb_dcx_ = bb_dcy_ = bb_in_ = bb_pl_ = bb_pr_ = 0.0f;
    bb_cg_ = 1.0f;
    bb_aa_[0] = bb_aa_[1] = bb_l_[0] = bb_l_[1] = bb_r_[0] = bb_r_[1] = 0.0f;
    bb_rnd_ = 0x2545F491u;
  }
  static inline float Flush(float x) { return x < 1e-15f && x > -1e-15f ? 0.0f : x; }
  // a two-pole low pass (a state variable, trapezoidal: Zavalishin's; Q 0.71)
  static inline float Lp(float x, float* s1, float* s2, float a1, float a2, float a3) {
    const float v3 = x - *s2, v1 = a1 * *s1 + a2 * v3, v2 = *s2 + a2 * *s1 + a3 * v3;
    *s1 = 2.0f * v1 - *s1;
    *s2 = 2.0f * v2 - *s2;
    return v2;
  }
  static inline float Tan(float x) {    // tan on 0 .. 1.3: a (3, 2) Pade
    const float x2 = x * x;
    return x * (15.0f - x2) / (15.0f - 6.0f * x2);
  }
  static inline float RSqrt(float x) {  // 1 / sqrt(x): the bits' guess, two of Newton's steps
    union { float f; uint32_t u; } b;
    b.f = x;
    b.u = 0x5F3759DFu - (b.u >> 1);
    float y = b.f;
    y = y * (1.5f - 0.5f * x * y * y);
    return y * (1.5f - 0.5f * x * y * y);
  }
  static inline float Chip(float x) {   // the chip's input stage: soft, a little lopsided (even harmonics), at most 1
    x = x > 1.5f ? 1.5f : x < -1.5f ? -1.5f : x;
    return x - (4.0f / 27.0f) * x * x * x + 0.04f * x * x;
  }
  static inline float Read(const int16_t* line_, uint32_t size_, uint32_t write_, float delay) {   // delay samples ago,
    float pos = static_cast<float>(write_) - delay;                                                  // linear between two
    while (pos < 0.0f) pos += static_cast<float>(size_);
    uint32_t i = static_cast<uint32_t>(pos);
    float f = pos - static_cast<float>(i);
    if (i >= size_) i -= size_;
    uint32_t j = i + 1u >= size_ ? 0u : i + 1u;
    float x = static_cast<float>(line_[i]), y = static_cast<float>(line_[j]);
    return (x + (y - x) * f) * (1.0f / 32768.0f);
  }

  inline float Crossfade(float a, float b, float f) { return a + (b - a) * f; }

  static inline float Knee(float x) {   // as it is up to 0.5, then softly to 1 (its slope 1 at the knee: no corner)
    float a = x < 0.0f ? -x : x;
    if (a <= 0.5f) return x;
    float e = a - 0.5f, y = 0.5f + e / (1.0f + 2.0f * e);
    return x < 0.0f ? -y : y;
  }

  void set_filter(float filter, float resonance) {
    float q = 0.6f + 9.0f * resonance * resonance;
    float hz;
    int mode;
    if (filter < -0.01f) {              // low pass: 12 kHz in the middle down to ~120 Hz
      mode = 1;
      hz = 12000.0f * Exp2(filter * 6.6f);
    } else if (filter > 0.01f) {        // high pass: 30 Hz up to ~3 kHz
      mode = 2;
      hz = 30.0f * Exp2(filter * 6.6f);
    } else {
      mode = 0;
      hz = 1000.0f;
    }
    float a = filter < 0.0f ? -filter : filter;
    // in from nothing over the first part of the knob; (0.16.65) from the very point where it changes sides, and from
    // rest there: before, 7 % of it at the switch, the high pass's states the low pass's (its 30 Hz from 12 kHz: the
    // low's level held as a step, into the loop), a bump repeated with the echoes (the random dose throws FILTER a beat
    // at a time, through the middle)
    blend_ = a > 0.15f ? 1.0f : a <= 0.01f ? 0.0f : (a - 0.01f) * (1.0f / 0.14f);
    if (mode != mode_) svf_.Init(), blend_now_ = 0.0f;   // (the new side from nothing: its blend 0 there)
    mode_ = mode;
    float f = hz / (bbd_ ? 0.5f * sr_ : sr_);   // (the BBD: at half the rate)
    svf_.set_f_q<stmlib::FREQUENCY_ACCURATE>(f > 0.24f ? 0.24f : f, q);
  }

  static float Exp2(float x) {          // 2^x, |x| < 8: a polynomial, no libm
    int32_t i = static_cast<int32_t>(x < 0.0f ? x - 1.0f : x);
    float f = x - static_cast<float>(i);
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    while (i > 0) p *= 2.0f, --i;
    while (i < 0) p *= 0.5f, ++i;
    return p;
  }

  int bbd_;
  float dirt_, width_;
  float bb_ec_, bb_el_, bb_er_, bb_clk_, bb_held_, bb_dcx_, bb_dcy_, bb_aa_[2], bb_l_[2], bb_r_[2];
  float bb_in_, bb_pl_, bb_pr_, bb_cg_;
  uint32_t bb_rnd_;
  int16_t* line_;
  uint32_t size_, write_;
  float sr_, time_, target_, feedback_, env_, drive_, mod_, phase_, blend_, blend_now_;
  int ping_pong_, mode_;
  stmlib::Svf svf_;

  DISALLOW_COPY_AND_ASSIGN(Delay);
};

}  // namespace mifm1

#endif  // MIFM1_MI_DELAY_H_
