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
    set_filter(0.0f, 0.0f);
  }

  // time in samples (clipped to the line: a ping-pong's twice), feedback 0..1 (at the end, 1: the loop), filter -1..1,
  // resonance 0..1, drive 0..1, mod 0..1
  void set(float time, float feedback, float filter, float resonance, float drive, int ping_pong, float mod) {
    float max = static_cast<float>(size_ - 4u) * (ping_pong ? 0.5f : 1.0f);
    while (time > max) time *= 0.5f;   // the same division, an octave of time shorter
    target_ = time < 16.0f ? 16.0f : time;
    feedback_ = feedback >= 0.99f ? 1.0f : feedback < 0.0f ? 0.0f : feedback;
    ping_pong_ = ping_pong;
    drive_ = drive;
    mod_ = mod;
    set_filter(filter, resonance);
  }

  void Process(const float* in, float* out_l, float* out_r, uint32_t n) {
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
    const float target = target_, mod = mod_, blend = blend_;
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
  }

 private:
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
    if (filter < -0.01f) {              // low pass: 12 kHz in the middle down to ~120 Hz
      mode_ = 1;
      hz = 12000.0f * Exp2(filter * 6.6f);
    } else if (filter > 0.01f) {        // high pass: 30 Hz up to ~3 kHz
      mode_ = 2;
      hz = 30.0f * Exp2(filter * 6.6f);
    } else {
      mode_ = 0;
      hz = 1000.0f;
    }
    float a = filter < 0.0f ? -filter : filter;
    blend_ = a > 0.15f ? 1.0f : a / 0.15f;   // in from nothing over the first part of the knob
    float f = hz / sr_;
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

  int16_t* line_;
  uint32_t size_, write_;
  float sr_, time_, target_, feedback_, env_, drive_, mod_, phase_, blend_;
  int ping_pong_, mode_;
  stmlib::Svf svf_;

  DISALLOW_COPY_AND_ASSIGN(Delay);
};

}  // namespace mifm1

#endif  // MIFM1_MI_DELAY_H_
