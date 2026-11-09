// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's STATIC (0.16.24; 0.16.27 after ARC/01): a machine of electrical events, for minimal, percussive and unstable
// sequences, after Pan Sonic's and Ryoji Ikeda's sines, ring modulators and static. Its plan is Yoann's ARC/01 (a DSP
// specification, November 2026): three generators, the pure sound against the destroyed one, frequencies in Hz,
// silence as much as timbre. Here a track plays one of them at a time (MODE, which a step can lock): three tracks, its
// three lanes; their cross-modulations go through the FOLLOW LFO and the cables.
//
// BODY: a sine an octave under the note (the sub), its pitch falling from up to +48 semitones over 2 to 120 ms; two
//   paths, PURE (nothing done to it: the fundamental kept) and DRIVE (a tanh), mixed; a click of 3 ms on top.
// ARC: a carrier at the note phase-modulated by a modulator at its own frequency in Hz (0.1 Hz to 8 kHz, never a ratio
//   of it): out = sin(2 pi phiC + INDEX m + FEEDBACK lp(out[n-1])), its feedback low-passed in the loop; a ring
//   modulator (carrier times modulator) mixed in; a slow drift of both frequencies (up to 30 cents), then random jumps
//   of the modulator; an asymmetric drive.
// SPARK: events, not a steady noise: on each trig (and while its gate is open, DENSITY more at random, up to 30 a
//   second) a burst of 1 to 9 hits at irregular gaps, each a short envelope on a source (white noise, coloured; noise
//   held by a sample and hold at 20 Hz .. 18 kHz; an impulse; an impulse train) through a high pass or a band pass of
//   100 Hz .. 16 kHz that the note transposes.
// Its random is a seeded xorshift: the same project played the same way plays the same events.
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = MI_SR;
enum { BODY, ARC, SPARK, kModes };
enum { SRC_WHITE, SRC_SH, SRC_IMPULSE, SRC_TRAIN, kSources };

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
__attribute__((noinline)) MI_COLD float ex2(float x) { return fm_exp2f(x); }   // (one copy: the knobs', the blocks')
inline float exn(float x) { return ex2(x * 1.4426950f); }
inline float note_hz(float note) { return 440.0f * ex2((note - 69.0f) * (1.0f / 12.0f)); }
inline float soft(float x) {            // a soft clip (tanh's Pade, 1 and flat from 3 on): the drives'
  x = clampf(x, -3.0f, 3.0f);
  const float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
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
inline int pick(float u, int n) {       // a choice's index (its share of the knob)
  const int i = static_cast<int>(clampf(u, 0.0f, 1.0f) * static_cast<float>(n));
  return i > n - 1 ? n - 1 : i;
}
inline float span(float u, float lo, float oct) { return lo * ex2(u * oct); }   // lo .. lo x 2^oct

// the knobs in physical units (ARC/01's ranges)
inline int holds(float u) { return u > 0.97f; }
inline float body_st(float u) { return 48.0f * u; }                                    // +0 .. +48 semitones
inline float body_sweep(float u) { return span(u, 0.002f, 5.9068906f); }              // 2 .. 120 ms
inline float body_drive(float u) { return 1.0f + 19.0f * u * u; }                       // x1 .. x20
inline float body_attack(float u) { return span(u, 0.0002f, 5.6438562f); }            // 0.2 .. 10 ms
inline float arc_mod(float u) { return span(u, 0.1f, 16.287712f); }                    // 0.1 Hz .. 8 kHz
inline float arc_index(float u) { return 15.0f * u * u; }                               // 0 .. 15 rad
inline float arc_drive(float u) { return 1.0f + 15.0f * u * u; }
inline float spark_hold(float u) { return span(u, 20.0f, 9.8137811f); }                // 20 Hz .. 18 kHz
inline float spark_filter(float u) { return span(u, 100.0f, 7.3219281f); }             // 100 Hz .. 16 kHz
inline float spark_q(float u) {         // the left half a high pass (Q 8 at its end, 0.7 at noon), the right a band pass
  return 0.7f * ex2(u < 0.5f ? (0.5f - u) * 7.0291462f : (u - 0.5f) * 8.1990712f);   // (to 12)
}
inline float spark_rate(float u) { return 30.0f * u * u; }                              // events a second, the gate open
inline float decay_t60(int mode, float u) {   // to -60 dB: BODY 20 ms .. 2 s, ARC 20 ms .. 4 s, SPARK 1 .. 400 ms
  const float x = clampf(u * (1.0f / 0.97f), 0.0f, 1.0f);
  return mode == SPARK ? span(x, 0.001f, 8.6438562f) : mode == ARC ? span(x, 0.02f, 7.6438562f)
                                                                   : span(x, 0.02f, 6.6438562f);
}

struct Stat {
  float k[8];
  float note;
  int gate, trig, alive;
  float env;
  int att;                              // (rising)
  float ph, phm, pe, cl, np, fb;        // the phases (BODY's or ARC's carrier, ARC's modulator); BODY's pitch over the
                                        // note (semitones), its click, its last noise; ARC's feedback (low-passed)
  float idx, fbk, ring;                 // ARC's knobs where its last block left them (ramped)
  float dr[2], dt[2], jump;             // ARC's drift (cents: the carrier's, the modulator's), its targets; the jump
  int dn, jn;                           // samples to the drift's next targets, to the next jump
  float clk, held, col;                 // SPARK's clock, the value held, the white noise coloured
  int pulse, left, gap, next;           // its impulse's samples left; the burst's hits left, the gap to the next; the
                                        // next event at random (its gate open)
  float ic1, ic2;                       // its filter
  uint32_t rng;
  float dcx, dcy;
};
static_assert(sizeof(Stat) <= MF_STATE_BYTES, "STATIC's state");
struct StatAt {
  Stat& operator[](int t) const { return *static_cast<Stat*>(mf_state(t)); }
};
const StatAt stv = {};

inline float env_step(float& env, int& att, float ainc, float kd) {   // a linear rise, then a fall at kd (1: held)
  if (att) {
    env += ainc;
    if (env >= 1.0f) env = 1.0f, att = 0;
  } else {
    env *= kd;
  }
  return env;
}

struct RunBody {
  float inc0, kp, ainc, kd, mix, drive, dnorm, click, kc;
};
__attribute__((noinline)) void run_body(Stat& s, const RunBody& r, float* y, int n) {
  float ph = s.ph, pe = s.pe, env = s.env, cl = s.cl, np = s.np;
  int att = s.att;
  for (int i = 0; i < n; ++i) {
    float inc = pe > 1e-3f ? r.inc0 * ex2(pe * (1.0f / 12.0f)) : r.inc0;   // (the pitch falling to the note)
    inc = inc > 0.45f ? 0.45f : inc;
    pe *= r.kp;
    ph += inc;
    ph -= ph >= 1.0f ? 1.0f : 0.0f;
    const float e = env_step(env, att, r.ainc, r.kd);
    const float x = 0.72f * e * sin_cyc(ph);                       // PURE: nothing more done to it
    float o = x + r.mix * (soft(r.drive * x) * r.dnorm - x);   // DRIVE mixed in
    if (cl > 1e-5f) {                    // the click: a noise's first difference (its highs) over 3 ms
      const float z = bip(s.rng);
      o += r.click * cl * (z - np);
      np = z;
      cl *= r.kc;
    }
    y[i] = o;
  }
  s.ph = ph, s.pe = pe < 1e-4f ? 0.0f : pe, s.cl = cl < 1e-5f ? 0.0f : cl, s.np = np;
  s.env = env < 1e-6f && !att ? 0.0f : env, s.att = att;
}

struct RunArc {
  float incc, incm, idx, didx, fbk, dfbk, ring, dring, lp, ainc, kd, drive, bias, tb, dnorm, wet;
};
__attribute__((noinline)) void run_arc(Stat& s, const RunArc& r, float* y, int n) {
  float ph = s.ph, phm = s.phm, fb = s.fb, env = s.env, idx = r.idx, fbk = r.fbk, ring = r.ring;
  int att = s.att;
  for (int i = 0; i < n; ++i) {
    ph += r.incc;
    ph -= ph >= 1.0f ? 1.0f : 0.0f;
    phm += r.incm;
    phm -= phm >= 1.0f ? 1.0f : 0.0f;
    const float m = sin_cyc(phm);
    float p = ph + idx * m + fbk * fb;    // (in turns: INDEX and FEEDBACK over 2 pi)
    p -= fm_floorf(p);
    const float c = sin_cyc(p);
    fb += r.lp * (c - fb);                // the loop's low pass (c in -1..1: the loop can't run away)
    idx += r.didx, fbk += r.dfbk, ring += r.dring;
    const float e = env_step(env, att, r.ainc, r.kd);
    const float x = 0.6f * e * (c + ring * (c * m - c));   // the ring modulator mixed in
    const float d = (soft(r.drive * x + r.bias) - r.tb) * r.dnorm;   // (asymmetric: its DC blocked after)
    y[i] = x + r.wet * (d - x);
  }
  s.ph = ph, s.phm = phm, s.fb = fm_flush(fb);
  s.env = env < 1e-6f && !att ? 0.0f : env, s.att = att;
}

struct RunSpark {
  float hinc, acol, gcol, wn, wh, wi, wt, tamp, k, a1, a2, a3, wbp, ainc, kd, gain, rate;
  int hits, pw, open;
};
inline int spark_gap(uint32_t& rng) { return 1 + static_cast<int>((0.003f + 0.027f * u01(rng)) * kSr); }   // 3..30 ms
int spark_wait(uint32_t& rng, float rate) {   // an event at random: its wait (Poisson: an exponential)
  const float w = -fm_logf(1.0f - 0.999f * u01(rng)) / rate * kSr;
  return 1 + static_cast<int>(w > 4.0e6f ? 4.0e6f : w);
}
__attribute__((noinline)) void run_spark(Stat& s, const RunSpark& r, float* y, int n) {
  float clk = s.clk, held = s.held, col = s.col, ic1 = s.ic1, ic2 = s.ic2, env = s.env;
  int att = s.att, pulse = s.pulse, left = s.left, gap = s.gap, next = s.next;
  for (int i = 0; i < n; ++i) {
    if (r.open && --next <= 0) {         // DENSITY: another event, its gate open
      left = r.hits, gap = 0;
      next = spark_wait(s.rng, r.rate);
    }
    if (left > 0 && --gap <= 0) {        // a hit of the burst: the envelope again, the impulse, the clock now
      att = 1, pulse = r.pw, clk = 1.0f;
      if (--left > 0) gap = spark_gap(s.rng);
    }
    const float z = bip(s.rng);
    clk += r.hinc;
    const int tick = clk >= 1.0f;
    clk -= tick ? static_cast<float>(static_cast<int>(clk)) : 0.0f;
    held = tick ? z : held;              // the sample and hold
    col += r.acol * (z - col);           // the white noise coloured
    const float e = env_step(env, att, r.ainc, r.kd);   // (on the source: the filter rings on as it would)
    const float x = e * (r.wn * r.gcol * col + r.wh * held + r.wi * (pulse > 0 ? r.tamp : 0.0f) +
                         r.wt * (tick ? r.tamp : 0.0f));
    pulse -= pulse > 0;
    const float v3 = x - ic2, v1 = r.a1 * ic1 + r.a2 * v3, v2 = ic2 + r.a2 * ic1 + r.a3 * v3;   // (a TPT state variable)
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    const float hp = x - r.k * v1 - v2, f = hp + r.wbp * (r.k * v1 - hp);   // its high pass, or its band pass (peak 1)
    y[i] = r.gain * f;
  }
  s.clk = clk, s.held = held, s.col = fm_flush(col), s.ic1 = fm_flush(ic1), s.ic2 = fm_flush(ic2);
  s.pulse = pulse, s.left = left, s.gap = gap, s.next = next;
  s.env = env < 1e-6f && !att ? 0.0f : env, s.att = att;
}

// ARC's drift (a block): two slow random walks of the frequencies, to 30 cents at 3/4 of DRIFT; past that, jumps of
// the modulator (to +-7 semitones, up to 4 a second)
MI_COLD void arc_drift(Stat& s, float u, int n) {
  const float depth = 30.0f * clampf(u * (4.0f / 3.0f), 0.0f, 1.0f), jumps = clampf((u - 0.75f) * 4.0f, 0.0f, 1.0f);
  s.dn -= n;
  if (s.dn <= 0) {
    s.dt[0] = bip(s.rng), s.dt[1] = bip(s.rng);
    s.dn = static_cast<int>((0.15f + 0.35f * u01(s.rng)) * kSr);
  }
  const float a = 1.0f - exn(-static_cast<float>(n) / (0.2f * kSr));
  for (int j = 0; j < 2; ++j) s.dr[j] += a * (s.dt[j] * depth - s.dr[j]);
  if (jumps <= 0.0f) {
    s.jump = 0.0f;
    return;
  }
  s.jn -= n;
  if (s.jn <= 0) {
    s.jump = static_cast<float>(static_cast<int>(bip(s.rng) * 7.5f * jumps));
    s.jn = spark_wait(s.rng, 4.0f * jumps);
  }
}

MI_COLD void put_dec(char* b, int n, int* at, float x, int dec) {   // x with 1 or 2 decimals
  const int sc = dec > 1 ? 100 : 10, v = static_cast<int>(x * static_cast<float>(sc) + 0.5f);
  put_i(b, n, at, v / sc);
  put_s(b, n, at, ".");
  if (dec > 1 && v % sc < 10) put_s(b, n, at, "0");
  put_i(b, n, at, v % sc);
}
MI_COLD void put_hz(char* b, int n, int* at, float hz) {   // 0.1Hz, 6.9Hz, 247Hz, 1.6kHz, 16kHz
  if (hz < 9.95f) {
    put_dec(b, n, at, hz, 1);
    put_s(b, n, at, "Hz");
  } else if (hz < 999.5f) {
    put_i(b, n, at, static_cast<int>(hz + 0.5f));
    put_s(b, n, at, "Hz");
  } else if (hz < 9950.0f) {
    put_dec(b, n, at, hz * 0.001f, 1);
    put_s(b, n, at, "kHz");
  } else {
    put_i(b, n, at, static_cast<int>(hz * 0.001f + 0.5f));
    put_s(b, n, at, "kHz");
  }
}
MI_COLD void put_ms(char* b, int n, int* at, float s) {   // 0.2ms .. 9.9ms, then put_time's
  if (s < 0.00995f) {
    put_dec(b, n, at, s * 1000.0f, 1);
    put_s(b, n, at, "ms");
  } else {
    put_time(b, n, at, s);
  }
}
MI_COLD void put_pct(char* b, int n, int* at, float u, const char* zero) {
  if (u < 0.005f) return put_s(b, n, at, zero);
  put_i(b, n, at, static_cast<int>(u * 100.0f + 0.5f));
  put_s(b, n, at, "%");
}

}  // namespace

extern "C" {

MI_COLD int static_mode(float u) { return pick(u, kModes); }

MI_COLD void static_mode_defaults(int mode, float* k) {   // a mode's knobs (MODE turned: its own, ARC/01's presets)
  static const float kD[kModes][8] = {
      {0.1667f, 0.375f, 0.6446f, 0.15f, 0.4f, 0.15f, 0.2342f, 0.5233f},     // BODY, PURE IMPACT: +18 st in 28 ms, 15 %
                                                                             // driven, a light click, 0.5 ms, 240 ms
      {0.5f, 0.6920f, 0.4472f, 0.09f, 0.0f, 0.15f, 0.35f, 0.5700f},          // ARC, COPPER ARC: 247 Hz, 3 rad, 0.18, no
                                                                             // ring, a little drift, a mid drive, 450 ms
      {0.8333f, 0.375f, 0.6442f, 0.6566f, 0.8067f, 0.0f, 0.0556f, 0.4023f}};  // SPARK, RELAY ROOM: S/H at 1.6 kHz, a band
                                                                             // pass at 2.8 kHz Q 4, a hit a trig, 12 ms
  const int m = mode < 0 ? 0 : mode >= kModes ? kModes - 1 : mode;
  for (int i = 0; i < 8; ++i) k[i] = kD[m][i];
}

MI_COLD void static_defaults(float* k) { static_mode_defaults(BODY, k); }

MI_COLD void static_reset(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Stat& s = stv[t];
  uint8_t* raw = reinterpret_cast<uint8_t*>(&s);
  for (uint32_t i = 0; i < sizeof(Stat); ++i) raw[i] = 0;
  s.rng = 0x9E3779B9u * static_cast<uint32_t>(t + 11) | 1u;
  static_defaults(s.k);
  s.note = 48.0f;
}

MI_COLD void static_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) static_reset(t);
  mf_forget();
}

MI_COLD void static_silence(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Stat& s = stv[t];
  s.trig = s.att = s.alive = s.left = s.pulse = 0;
  s.env = s.cl = s.pe = s.fb = s.ic1 = s.ic2 = s.held = s.col = s.dcx = s.dcy = 0.0f;
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

MI_COLD void static_render(int t, float* out, uint32_t n) {   // (its glue for size: its samples' loops at -O2)
  if (t < 0 || t >= MF_TRACKS) return;
  Stat& s = stv[t];
  const float* k = s.k;
  const int mode = static_mode(k[0]);
  const float dk = k[7];
  const float kd = holds(dk) && s.gate ? 1.0f : exn(-6.9077553f / ((holds(dk) ? 0.03f : decay_t60(mode, dk)) * kSr));
  if (s.trig) {                         // a trig: from the start if it was silent (its phases at 0), else from where
    s.trig = 0;                         // it is (no click)
    if (s.env < 1e-3f) s.ph = s.phm = s.fb = 0.0f;
    s.att = mode != SPARK, s.alive = 1;   // (SPARK's hits raise it)
    if (mode == BODY) s.pe = body_st(k[1]), s.cl = 1.0f;
    if (mode == SPARK) {
      s.left = 1 + pick(k[6], 9), s.gap = 0;
      s.next = spark_wait(s.rng, spark_rate(k[5]) + 1e-3f);
    }
  }
  const int open = mode == SPARK && s.gate && k[5] > 0.01f;
  if (!s.alive && !open && fm_fabsf(s.dcy) < 1e-6f) {
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    s.dcx = s.dcy = 0.0f;
    return;
  }
  const int ni = static_cast<int>(n);
  if (mode == BODY) {
    RunBody r;
    r.inc0 = clampf(note_hz(mi_tuned(s.note) - 12.0f) / kSr, 0.0f, 0.45f);   // (0.16.84: tuned)
    r.kp = exn(-1.0f / (body_sweep(k[2]) * kSr));
    r.mix = k[3];
    r.drive = body_drive(k[4]);
    r.dnorm = 0.72f / soft(r.drive * 0.72f);              // (its peak the pure one's)
    r.click = 0.3f * k[5];
    r.kc = 0.94912686f;                                  // (-60 dB in 3 ms)
    r.ainc = 1.0f / (body_attack(k[6]) * kSr);
    r.kd = kd;
    run_body(s, r, out, ni);
  } else if (mode == ARC) {
    arc_drift(s, k[5], ni);
    RunArc r;
    const float f0 = clampf(note_hz(mi_tuned(s.note)), 20.0f, 8000.0f);   // (0.16.84: tuned)
    r.incc = clampf(f0 * ex2(s.dr[0] * (1.0f / 1200.0f)) / kSr, 0.0f, 0.45f);
    r.incm = clampf(arc_mod(k[1]) * ex2(s.dr[1] * (1.0f / 1200.0f) + s.jump * (1.0f / 12.0f)) / kSr, 0.0f, 0.45f);
    const float inv = 1.0f / static_cast<float>(ni > 0 ? ni : 1), turns = 1.0f / 6.2831853f;
    const float idx = arc_index(k[2]) * turns, fbk = 2.0f * k[3] * turns, ring = k[4];
    if (s.att && s.env <= 0.0f) s.idx = idx, s.fbk = fbk, s.ring = ring;   // (from silence: no ramp)
    r.idx = s.idx, r.didx = (idx - s.idx) * inv, r.fbk = s.fbk, r.dfbk = (fbk - s.fbk) * inv;   // (a block's ramp:
    r.ring = s.ring, r.dring = (ring - s.ring) * inv;                                          // no zipper)
    s.idx = idx, s.fbk = fbk, s.ring = ring;
    r.lp = 0.34781536f;                                  // (the feedback's loop: a low pass at 3 kHz)
    r.ainc = 1.0f / (0.001f * kSr);
    r.kd = kd;
    const float u = k[6];
    r.drive = arc_drive(u), r.bias = 0.5f * u, r.tb = soft(r.bias);   // (the bias after the gain: the asymmetry kept)
    r.dnorm = 0.6f / (r.tb + soft(r.drive * 0.6f - r.bias));            // (its larger half, the lower, at 0.6)
    r.wet = clampf(10.0f * u, 0.0f, 1.0f);
    run_arc(s, r, out, ni);
  } else {
    RunSpark r;
    const int src = pick(k[1], kSources);
    const float hz = spark_hold(k[2]);
    r.hinc = clampf(hz / kSr, 0.0f, 1.0f);
    r.acol = 1.0f - exn(-6.2831853f * fm_minf(hz, 0.45f * kSr) / kSr);   // (white: HOLD its colour)
    r.gcol = fm_minf(fm_sqrtf((2.0f - r.acol) / r.acol), 12.0f) * 0.577f;     // (its level kept as it darkens)
    r.wn = src == SRC_WHITE, r.wh = src == SRC_SH, r.wi = src == SRC_IMPULSE, r.wt = src == SRC_TRAIN;
    r.pw = 1 + static_cast<int>(kSr / hz * 0.5f);         // (the impulse: half a period of HOLD, wider low)
    const float fc = clampf(spark_filter(k[3]) * ex2((s.note - 48.0f) * (1.0f / 12.0f)), 20.0f, 0.45f * kSr);
    const float q = spark_q(k[4]), g = fm_tanf(3.1415927f * fc / kSr);
    const int bp = k[4] >= 0.5f;
    r.k = 1.0f / q;
    r.a1 = 1.0f / (1.0f + g * (g + r.k)), r.a2 = g * r.a1, r.a3 = g * r.a2;
    r.wbp = static_cast<float>(bp);
    // the levels: a band pass keeps pi fc / (q sr) of a white noise's power, and rings an impulse at 2 / that: each
    // brought back to a hit of noise's (an impulse's ring at 0.6)
    const float ringq = q * kSr / (3.1415927f * fc);
    r.gain = 0.5f * (bp ? fm_minf(fm_sqrtf(ringq), 6.0f) : 1.0f);
    const float a1 = bp ? fm_minf(0.3f * ringq / r.gain, 200.0f) : 1.2f;
    const float pwe = fm_maxf(fm_minf(static_cast<float>(r.pw), kSr / (4.0f * fc)), 1.0f);
    r.tamp = src == SRC_TRAIN ? fm_minf(a1, 0.6f * fm_sqrtf(kSr / hz) * (bp ? fm_sqrtf(ringq) : 1.0f))
                              : bp ? a1 / pwe : a1;
    r.ainc = src >= SRC_IMPULSE ? 1.0f : 1.0f / (0.0003f * kSr);   // (the noises softened in 0.3 ms; an impulse at once)
    r.kd = kd;
    r.rate = spark_rate(k[5]) + 1e-3f;
    r.hits = 1 + pick(k[6], 9);
    r.open = open;
    run_spark(s, r, out, ni);
  }
  if (!s.att && s.env < 1e-6f && s.left <= 0 && !open && fm_fabsf(s.ic1) + fm_fabsf(s.ic2) < 1e-6f) s.alive = 0;
  mi_finish(out, n, 1.0f, &s.dcx, &s.dcy);
  if (!s.alive && fm_fabsf(s.dcy) < 1e-6f) s.dcx = s.dcy = 0.0f;
}

const char* static_knob_name(int k, float mode) {
  static const char* const kN[kModes][8] = {
      {"Mode", "Pitch", "Sweep", "Mix", "Drive", "Click", "Attack", "Decay"},
      {"Mode", "Mod", "Index", "Feedback", "Ring", "Drift", "Drive", "Decay"},
      {"Mode", "Source", "Hold", "Filter", "Q", "Density", "Cluster", "Decay"}};
  return k >= 0 && k < 8 ? kN[static_mode(mode)][k] : "";
}

int static_knob_steps(int k, float mode) {
  const int m = static_mode(mode);
  return k == 0 ? kModes : m == SPARK && k == 1 ? kSources : m == SPARK && k == 6 ? 9 : 128;
}

MI_COLD void static_knob_text(int k, float u, float mode, char* b, int n) {
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  const int m = static_mode(mode);
  if (k == 0) {
    static const char* const kM[kModes] = {"Body", "Arc", "Spark"};
    return put_s(b, n, &at, kM[static_mode(u)]);
  }
  if (k == 7) return holds(u) ? put_s(b, n, &at, "Hold") : put_ms(b, n, &at, decay_t60(m, u));
  if (m == BODY) {
    switch (k) {
      case 1:
        put_s(b, n, &at, "+");
        put_i(b, n, &at, static_cast<int>(body_st(u) + 0.5f));
        return put_s(b, n, &at, "st");
      case 2: return put_ms(b, n, &at, body_sweep(u));
      case 3: return u > 0.995f ? put_s(b, n, &at, "Drive") : put_pct(b, n, &at, u, "Pure");
      case 4:
        put_s(b, n, &at, "x");
        return put_dec(b, n, &at, body_drive(u), 1);
      case 5: return put_pct(b, n, &at, u, "Off");
      default: return put_ms(b, n, &at, body_attack(u));
    }
  }
  if (m == ARC) {
    switch (k) {
      case 1: return put_hz(b, n, &at, arc_mod(u));
      case 2:
        put_dec(b, n, &at, arc_index(u), 1);
        return put_s(b, n, &at, "rad");
      case 3: return put_dec(b, n, &at, 2.0f * u, 2);
      case 4: return put_pct(b, n, &at, u, "Off");
      case 5:
        if (u < 0.005f) return put_s(b, n, &at, "Off");
        if (u <= 0.75f) {
          put_i(b, n, &at, static_cast<int>(30.0f * u * (4.0f / 3.0f) + 0.5f));
          return put_s(b, n, &at, "ct");
        }
        put_s(b, n, &at, "Jump");
        return put_i(b, n, &at, static_cast<int>((u - 0.75f) * 400.0f + 0.5f));
      default:
        if (u < 0.005f) return put_s(b, n, &at, "Off");
        put_s(b, n, &at, "x");
        return put_dec(b, n, &at, arc_drive(u), 1);
    }
  }
  switch (k) {
    case 1: {
      static const char* const kS[kSources] = {"White", "S/H", "Impulse", "Train"};
      return put_s(b, n, &at, kS[pick(u, kSources)]);
    }
    case 2: return put_hz(b, n, &at, spark_hold(u));
    case 3: return put_hz(b, n, &at, spark_filter(u));
    case 4:
      put_s(b, n, &at, u < 0.5f ? "HP " : "BP ");
      return put_dec(b, n, &at, spark_q(u), 1);
    case 5:
      if (u <= 0.01f) return put_s(b, n, &at, "Trig");
      put_dec(b, n, &at, spark_rate(u), 1);
      return put_s(b, n, &at, "/s");
    default:
      put_s(b, n, &at, "x");
      return put_i(b, n, &at, 1 + pick(u, 9));
  }
}

}  // extern "C"
