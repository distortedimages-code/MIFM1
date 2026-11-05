// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's STATIC (0.16.24): a noise machine for hard-cut, contrasted sequences, after Pan Sonic's and Ryoji Ikeda's
// sines and static, and the sample and hold that slows the noise down on Morphor's ECHON 6 (2025: white noise held at
// a variable rate, from broken crackles to the whole noise).
//
// A voice a track: a sine at the note and a noise held at a clock that follows the note (TONE between them); the
// noise's steps hard (digital: their aliasing kept) or slewed (analog: rounded, darker), sparse (DENSITY: most steps
// 0, the others dying out within theirs: a dust of clicks) or all there, held; each trig its own noise or the same one again (VARY: the same seed, from
// the same place: the same pattern, a sequence's to rely on); an envelope from a click to a hold (DECAY: at its end the
// gate holds it); a soft drive. SPACE: the track's reverb send raised with the sound's height (the note's, the clock's
// as much as TONE gives it): high sines and hiss into the space, the sub clean and dry.
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = MI_SR;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }
inline uint32_t xs(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline float bip(uint32_t& s) { return static_cast<float>(xs(s) >> 8) * (2.0f / 16777216.0f) - 1.0f; }
inline float u01(uint32_t& s) { return static_cast<float>(xs(s) >> 8) * (1.0f / 16777216.0f); }
inline float sin_cyc(float ph) {        // a sine of a phase in cycles, 0..1
  float x = ph < 0.5f ? ph : ph - 0.5f, sg = ph < 0.5f ? 1.0f : -1.0f;
  x = x < 0.25f ? x : 0.5f - x;
  float z = x * 4.0f, z2 = z * z;
  return sg * z * (1.5707963f + z2 * (-0.64596336f + z2 * (0.079688469f + z2 * (-0.0046722113f + z2 * 0.00015081274f))));
}
// the knobs: RATE the clock, the note's /64 .. x256 (14 octaves); DECAY 3 ms .. 4.4 s (to -60 dB), then HOLD
inline float clock_ratio(float u) { return fm_exp2f(14.0f * u - 6.0f); }
inline int holds(float u) { return u > 0.97f; }
inline float t60(float u) { return 0.003f * fm_exp2f(u * 10.5f); }

struct Stat {
  float k[8];
  float note;
  int gate, trig, alive;
  float ph, clk, held, y, lp;           // the sine's phase; the clock's, the value held, its slew, the softening
  float env;                            // the envelope
  int att;                              // (rising)
  uint32_t seed, base, rnd;             // the noise's random now, its pattern's start, VARY's own
  float space;                          // the reverb's share this block (SPACE, the height)
  float dcx, dcy;
};
static_assert(sizeof(Stat) <= MF_STATE_BYTES, "STATIC's state");
struct StatAt {
  Stat& operator[](int t) const { return *static_cast<Stat*>(mf_state(t)); }
};
const StatAt stv = {};

struct Run {                            // (a block's coefficients)
  float pinc, cinc, dens, hdec, slew, soft, gs, gn, ainc, kd, drive, dnorm;
};
// n samples into y: the sine, the noise held, their mix under the envelope, driven (at -O2: the rest for size)
__attribute__((noinline)) void run(Stat& s, const Run& r, float* y, int n) {
  float ph = s.ph, clk = s.clk, held = s.held, yv = s.y, lp = s.lp, env = s.env;
  int att = s.att;
  for (int i = 0; i < n; ++i) {
    ph += r.pinc;
    ph -= ph >= 1.0f ? 1.0f : 0.0f;
    clk += r.cinc;
    if (clk >= 1.0f) {                  // the clock: a new value (or, past DENSITY's share, nothing: 0)
      clk -= static_cast<float>(static_cast<int>(clk));
      const float v = bip(s.seed);
      held = u01(s.seed) < r.dens ? v : 0.0f;
    }
    held *= r.hdec;                     // (sparse: each value gone within its step, a click)
    yv += r.slew * (held - yv);         // (digital: at once; analog: rounded)
    lp += r.soft * (yv - lp);           // (analog: darker)
    if (att) {
      env += r.ainc;
      if (env >= 1.0f) env = 1.0f, att = 0;
    } else {
      env *= r.kd;
    }
    float x = (r.gs * sin_cyc(ph) + r.gn * lp) * env * r.drive;
    x = x < -1.5f ? -1.5f : x > 1.5f ? 1.5f : x;
    y[i] = (x - (4.0f / 27.0f) * x * x * x) * r.dnorm;
  }
  s.ph = ph, s.clk = clk, s.held = held, s.y = fm_fabsf(yv) < 1e-20f ? 0.0f : yv, s.lp = fm_fabsf(lp) < 1e-20f ? 0.0f : lp;
  s.env = env < 1e-6f && !att ? 0.0f : env, s.att = att;
}

}  // namespace

extern "C" {

MI_COLD void static_reset(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Stat& s = stv[t];
  uint8_t* raw = reinterpret_cast<uint8_t*>(&s);
  for (uint32_t i = 0; i < sizeof(Stat); ++i) raw[i] = 0;
  s.rnd = 0x9E3779B9u * static_cast<uint32_t>(t + 11) | 1u;
  s.base = s.seed = 0x85EBCA6Bu * static_cast<uint32_t>(t + 3) | 1u;
  float d[8];
  static_defaults(d);
  for (int i = 0; i < 8; ++i) s.k[i] = d[i];
  s.note = 60.0f;
}

MI_COLD void static_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) static_reset(t);
  mf_forget();
}

MI_COLD void static_silence(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Stat& s = stv[t];
  s.trig = s.att = s.alive = 0;
  s.env = s.y = s.lp = s.held = s.dcx = s.dcy = 0.0f;
}

MI_COLD void static_set(int t, float note, const float* k) {
  if (t < 0 || t >= MF_TRACKS) return;
  Stat& s = stv[t];
  for (int i = 0; i < 8; ++i) s.k[i] = clampf(k[i], 0.0f, 1.0f);
  s.note = note;
}

MI_COLD void static_gate(int t, int g) {
  if (t < 0 || t >= MF_TRACKS) return;
  Stat& s = stv[t];
  if (g && !s.gate) s.trig = 1;
  s.gate = g;
}

MI_COLD float static_space(int t) { return t >= 0 && t < MF_TRACKS ? stv[t].space : 0.0f; }

MI_COLD void static_render(int t, float* out, uint32_t n) {   // (its glue for size: its samples' loop at -O2)
  if (t < 0 || t >= MF_TRACKS) return;
  Stat& s = stv[t];
  if (s.trig) {                         // a trig: VARY, a new noise or the same again (from its start)
    s.trig = 0;
    if (u01(s.rnd) < s.k[4] * 1.0001f) s.base = xs(s.rnd) | 1u;
    s.seed = s.base;
    s.clk = 1.0f, s.held = s.y = s.lp = 0.0f, s.ph = 0.0f;
    s.att = 1, s.alive = 1;
  }
  if (!s.alive && fm_fabsf(s.dcy) < 1e-6f) {
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    s.dcx = s.dcy = 0.0f, s.space = 0.0f;
    return;
  }
  const float tone = s.k[0], an = s.k[2], f0 = note_hz(s.note);
  const float clock = clampf(f0 * clock_ratio(s.k[1]), 0.5f, kSr);
  Run r;
  r.pinc = clampf(f0 / kSr, 0.0f, 0.45f);
  r.cinc = clock / kSr;
  r.dens = s.k[3] * s.k[3];             // (squared: its sparse end, a dust)
  // (under 1, a value dies out: in 0.3 ms at the sparse end, ~30 ms near 1; at 1 held, the sample and hold's)
  r.hdec = s.k[3] > 0.995f ? 1.0f : fm_expf(-1.0f / (0.0003f / (1.0f - r.dens + 0.01f) * kSr));
  const float fs = 20000.0f * fm_exp2f(-3.7f * an);   // (ANALOG: its steps rounded, 20 kHz down to 1.5 kHz)
  r.slew = an < 0.02f ? 1.0f : 1.0f - fm_expf(-6.2831853f * fs / kSr);
  r.soft = 1.0f - fm_expf(-6.2831853f * (18000.0f - 14000.0f * an) / kSr);
  r.gs = 0.5f * sin_cyc(0.25f + 0.25f * tone), r.gn = 0.5f * sin_cyc(0.25f * tone);   // (equal power)
  r.ainc = 1.0f / (0.0005f * kSr);
  const float dk = s.k[5];
  r.kd = holds(dk) && s.gate ? 1.0f : fm_expf(-6.9077553f / ((holds(dk) ? 0.03f : t60(dk)) * kSr));
  r.drive = 1.0f + 6.0f * s.k[7] * s.k[7];
  r.dnorm = 1.0f / (1.0f + 1.5f * s.k[7]);
  run(s, r, out, static_cast<int>(n));
  if (!s.att && s.env < 1e-6f) s.alive = 0;
  // SPACE: the height (the note's, the clock's as much as TONE gives it), over 80 Hz .. 5 kHz, an S-curve: the sub
  // none of it, 200 Hz a tenth, 1 kHz two thirds, the highs all of it
  const float hz = f0 * fm_exp2f(tone * fm_log2f(clampf(clock, 1.0f, 0.5f * kSr) / f0));
  const float h = clampf(fm_log2f(hz * (1.0f / 80.0f)) * (1.0f / 6.0f), 0.0f, 1.0f);
  s.space = s.k[6] * h * h * (3.0f - 2.0f * h);
  mi_finish(out, n, 0.9f, &s.dcx, &s.dcy);
  if (!s.alive && fm_fabsf(s.dcy) < 1e-6f) s.dcx = s.dcy = 0.0f;
}

const char* static_knob_name(int k) {
  static const char* const kN[8] = {"Tone", "Rate", "Analog", "Density", "Vary", "Decay", "Space", "Drive"};
  return k >= 0 && k < 8 ? kN[k] : "";
}

void static_defaults(float* k) {        // half sine, half noise; the clock four times the note; a little analog; all
  static const float kD[8] = {0.5f, 0.57f, 0.25f, 1.0f, 1.0f, 0.3f, 0.6f, 0.15f};   // there; a new noise each trig;
  for (int i = 0; i < 8; ++i) k[i] = kD[i];                                          // short; the space up; a little
}                                                                                    // drive

int static_knob_steps(int k) { return k >= 0 ? 128 : 128; }

MI_COLD void static_knob_text(int k, float u, char* b, int n) {
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  switch (k) {
    case 0:                             // TONE: the sine, the noise, between
      if (u < 0.02f) return put_s(b, n, &at, "Sine");
      if (u > 0.98f) return put_s(b, n, &at, "Noise");
      put_i(b, n, &at, static_cast<int>(u * 100.0f + 0.5f));
      return put_s(b, n, &at, "%");
    case 1: {                           // RATE: the clock, a ratio of the note
      const float r = clock_ratio(u);
      if (r < 1.0f) {
        put_s(b, n, &at, "/");
        return put_i(b, n, &at, static_cast<int>(1.0f / r + 0.5f));
      }
      put_s(b, n, &at, "x");
      return put_i(b, n, &at, static_cast<int>(r + 0.5f));
    }
    case 2:                             // ANALOG: digital, analog, between
      if (u < 0.02f) return put_s(b, n, &at, "Digital");
      if (u > 0.98f) return put_s(b, n, &at, "Analog");
      break;
    case 4:                             // VARY: the same noise each trig, a new one, the chance between
      if (u < 0.01f) return put_s(b, n, &at, "Same");
      if (u > 0.99f) return put_s(b, n, &at, "New");
      break;
    case 5:
      if (holds(u)) return put_s(b, n, &at, "Hold");
      return put_time(b, n, &at, t60(u));
    default: break;
  }
  put_i(b, n, &at, static_cast<int>(u * 100.0f + 0.5f));
}

}  // extern "C"
