// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's own voice, ZERO (0.8.1): pure sines in true through-zero FM, round pulses, a sound that lives. A sine carries
// the note; a modulator (a sine, a triangle, a saw: SHAPE) moves its frequency linearly, as an analog oscillator's
// through-zero FM input does: the carrier's frequency is the note plus INDEX times the note times the modulator, and
// past INDEX 1 it goes through zero: the phase runs backwards for a while, and the note stays where it is (a linear
// FM keeps its average pitch, an exponential one drifts). INDEX at 0: a pure sine.
//
// The pulse: BEAT is a rate (off, then up to 8 Hz). PULSE says where it goes: to the left into the level (a second
// sine, pure, BEAT Hz above the note, beats with the first: round pulsations of a pure sine, as two oscillators a
// little apart make them; under deep FM, the note's own partial pulses, the rest stays), to the right into the timbre (the modulator BEAT Hz off its ratio: the FM's spectrum turns over and
// over); in the middle, both.
//
// Each trig is a round pulse: a raised-cosine rise (3 to 50 ms), a rounded fall ((1 + t / T) e^(-t / T): no corner at
// the top), DECAY long (at the end, held while the gate is, let go roundly). MOVE moves INDEX over the note: to the
// right the FM opens at the trig and settles, to the left it grows. VARY, as MIFM1's randoms: to the right each trig
// another sound, to the left the same sixteen sounds in a loop (back to the middle: another loop next time); how far
// they go: the index, the ratio's tuning (a little off: the FM breathes), the decay, the shape, the pulse's rate and
// where it starts, the note's slow wander.
//
// Rendered at twice the rate (the FM's sidebands, a saw's edges, polyBLEP on them), brought down by a polyphase IIR
// half-band (Laurent de Soras' HIIR design, six coefficients: flat to 20.3 kHz, -74 dB from 23.8 kHz).
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f, kSr2 = 88200.0f;
const int kLoop = 16;
const int kRatios = 11;
const float kRatio[kRatios] = {0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
const char* const kRatioName[kRatios] = {"x1/4", "x1/2", "x1", "x3/2", "x2", "x3", "x4", "x5", "x6", "x7", "x8"};
// the half-band's allpass coefficients: the even ones on the odd samples' path, the odd ones on the even samples'
const float kHb[6] = {0.0682040760f, 0.2402703580f, 0.4486762359f, 0.6411223671f, 0.7999975637f, 0.9344822355f};

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

// a sine of a phase in cycles, 0..1 (a 9th-order polynomial on the quarter wave, fitted: 3e-9 off, no table)
inline float sin_cyc(float ph) {
  float x = ph < 0.5f ? ph : ph - 0.5f, s = ph < 0.5f ? 1.0f : -1.0f;
  x = x < 0.25f ? x : 0.5f - x;
  float z = x * 4.0f, z2 = z * z;
  return s * z * (1.5707963f + z2 * (-0.64596336f + z2 * (0.079688469f + z2 * (-0.0046722113f + z2 * 0.00015081274f))));
}

inline float blep(float t, float dt) {  // polyBLEP: a step's alias taken off around it
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0f;
  }
  if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

struct Zero {
  // the knobs as the trig plays them
  float note, index, shape, decay, beat, move, vary, pulse;
  int ratio;
  // this trig's draws
  float t_index, t_ratio, t_decay, t_shape, t_beat;
  // the voice
  float pc, pt, pm;                     // the carrier's, the second sine's, the modulator's phase (cycles)
  float amp, a0, att, att_inc;          // the level, where the rise started, the rise (0..1) and its step
  float u, w, dk, dh;                   // the rounded fall: e^(-t/T), (t/T) e^(-t/T); their step
  int gate, held, let_go, trig, alive, snap;   // (snap: the smoothed values set at once, after silence)
  float ie, ik;                         // MOVE's envelope (1 at the trig, falling), its step
  float idx_s, tw_s, mo_s;              // the index, the second sine's level, the modulator's offset: smoothed
  float hb[2][4];                       // the half-band's two paths: their input, their sections' outputs
  float dr, dr_t;                       // the wander (the trig's random)
  int dr_n;
  uint32_t rnd;                         // the trig's random
  uint32_t loop_seed[kLoop];
  int loop_pos, loop_ready;
  uint32_t fresh;                       // the new seeds' random (never looped)
};

// (its state on a track: the engine's, shared with the other machines, one a track at a time)
static_assert(sizeof(Zero) <= MF_STATE_BYTES, "ZERO's state");
struct ZeroAt {
  Zero& operator[](int t) const { return *static_cast<Zero*>(mf_state(t)); }
};
const ZeroAt zero = {};

inline uint32_t xs(uint32_t& s) {       // xorshift32
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline float u01(uint32_t& s) { return static_cast<float>(xs(s) >> 8) * (1.0f / 16777216.0f); }
inline float bip(uint32_t& s) { return 2.0f * u01(s) - 1.0f; }

inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }

void new_trig(Zero& v, int silent) {    // the trig's sound: its seed (a loop's, or a new one), its draws
  float a = fm_fabsf(v.vary);
  uint32_t s;
  if (v.vary < -0.02f) {                // to the left: the loop (a new one when the knob has been back to the middle)
    if (!v.loop_ready) {
      for (int i = 0; i < kLoop; ++i) v.loop_seed[i] = xs(v.fresh) | 1u;
      v.loop_pos = 0;
      v.loop_ready = 1;
    }
    s = v.loop_seed[v.loop_pos];
    v.loop_pos = (v.loop_pos + 1) % kLoop;
  } else {
    if (v.vary <= 0.02f) v.loop_ready = 0;
    s = xs(v.fresh) | 1u;
  }
  v.t_index = a * 0.6f * bip(s);
  v.t_ratio = a * a * 0.015f * bip(s);  // (a little off the ratio: the FM breathes)
  v.t_decay = a * 0.25f * bip(s);
  v.t_shape = a * 0.25f * bip(s);
  v.t_beat = a * 0.4f * bip(s);
  float mph = a * u01(s), tph = a * u01(s);
  v.rnd = s ^ 0x9E3779B9u;
  if (!v.rnd) v.rnd = 1u;
  v.dr = 0.6f * bip(v.rnd);
  v.dr_t = bip(v.rnd);
  v.dr_n = 8192;
  if (silent) {                         // after silence: the same start each time (over a sound: on from there)
    v.pc = 0.0f;
    v.pt = tph;
    v.pm = mph;
    for (int p = 0; p < 2; ++p)
      for (int i = 0; i < 4; ++i) v.hb[p][i] = 0.0f;
    v.snap = 1;
  }
}

}  // namespace

extern "C" {

void zero_reset(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Zero& v = zero[t];
  uint8_t* raw = reinterpret_cast<uint8_t*>(&v);
  for (uint32_t i = 0; i < sizeof(Zero); ++i) raw[i] = 0;
  v.fresh = 0x2545F491u * static_cast<uint32_t>(t + 1);
  v.rnd = 1u;
  v.note = 36.0f;
  v.ratio = 2;
  v.att = 1.0f;
}

void zero_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) zero_reset(t);
  mf_forget();
}

void zero_silence(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  zero[t].alive = 0, zero[t].amp = 0.0f, zero[t].trig = 0, zero[t].att = 1.0f;
}

void zero_set(int t, float note, const float* k) {
  if (t < 0 || t >= MF_TRACKS) return;
  Zero& v = zero[t];
  v.note = note;
  float x = clampf(k[0], 0.0f, 1.0f);
  v.index = 5.0f * x * x;               // 0 .. 5 (through zero from 1)
  int r = static_cast<int>(clampf(k[1], 0.0f, 1.0f) * static_cast<float>(kRatios));
  v.ratio = r < 0 ? 0 : r > kRatios - 1 ? kRatios - 1 : r;
  v.shape = clampf(k[2], 0.0f, 1.0f);
  v.decay = clampf(k[3], 0.0f, 1.0f);
  v.beat = clampf(k[4], 0.0f, 1.0f);
  v.move = clampf(2.0f * k[5] - 1.0f, -1.0f, 1.0f);
  v.vary = clampf(2.0f * k[6] - 1.0f, -1.0f, 1.0f);
  v.pulse = clampf(2.0f * k[7] - 1.0f, -1.0f, 1.0f);
}

void zero_gate(int t, int g) {
  if (t < 0 || t >= MF_TRACKS) return;
  Zero& v = zero[t];
  if (g && !v.gate) v.trig = 1;
  if (!g && v.gate && v.held) {         // a held note let go: a rounded fall from where it is
    v.let_go = 1;
    v.u = v.att >= 1.0f ? v.amp : 1.0f;  // (during the rise: from its top)
    v.w = 0.0f;
  }
  v.gate = g;
}

void zero_render(int t, float* out, uint32_t n) {
  if (t < 0 || t >= MF_TRACKS) return;
  Zero& v = zero[t];
  float dec = clampf(v.decay + v.t_decay, 0.0f, 1.0f);
  float tau = 0.03f * fm_exp2f(dec * 7.5f);   // 30 ms .. 5.4 s
  if (v.trig) {                         // a trig: its sound, the envelopes from the top
    v.trig = 0;
    new_trig(v, v.amp < 0.003f || !v.alive);
    dec = clampf(v.decay + v.t_decay, 0.0f, 1.0f);   // (this trig's decay)
    tau = 0.03f * fm_exp2f(dec * 7.5f);
    v.a0 = v.alive ? v.amp : 0.0f;
    v.att = 0.0f;
    v.att_inc = 1.0f / (clampf(0.1f * tau, 0.003f, 0.05f) * kSr);
    v.u = 1.0f;
    v.w = 0.0f;
    v.ie = 1.0f;
    v.let_go = 0;
    v.alive = 1;
  }
  if (!v.alive) {
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    return;
  }
  // the block's settings
  v.held = v.decay > 0.98f;
  float T = (v.let_go ? 0.08f : tau) * 0.6f;   // (the rounded fall's T: its -40 dB near the one-pole's)
  v.dk = fm_expf(-1.0f / (T * kSr));
  v.dh = 1.0f / (T * kSr);
  v.ik = fm_expf(-1.0f / (clampf(0.5f * (v.held ? 1.2f : tau), 0.02f, 3.0f) * kSr));
  v.dr_n -= static_cast<int>(n);         // the wander: a slow walk (a new aim every 0.19 s), a block at a time
  if (v.dr_n <= 0) {
    v.dr_n = 8192;
    v.dr_t = bip(v.rnd);
  }
  v.dr += (v.dr_t - v.dr) * clampf(static_cast<float>(n) * 3e-5f, 0.0f, 1.0f);
  float a = fm_fabsf(v.vary);
  float f0 = note_hz(v.note + v.dr * 0.12f * a * a);
  float bh = 8.0f * v.beat * v.beat * (1.0f + v.t_beat);   // the pulse's rate (Hz)
  float on = clampf(v.beat * 40.0f, 0.0f, 1.0f);           // (off at 0, faded in just above)
  float to_level = 0.5f * (1.0f - v.pulse), to_timbre = 0.5f * (1.0f + v.pulse);
  float tw = 0.5f * on * to_level;      // the second sine's level
  float mo = bh * to_timbre * on;       // the modulator's offset (Hz)
  float ratio = kRatio[v.ratio] * (1.0f + v.t_ratio);
  float sh = clampf(v.shape + v.t_shape, 0.0f, 1.0f);
  float i_max = clampf(20000.0f / f0 - 1.0f, 0.0f, 6.0f);   // (the carrier's swing kept under 20 kHz up high)
  float i_base = clampf(v.index * (1.0f + v.t_index), 0.0f, i_max);
  float inc_c = f0 / kSr2, inc_t = clampf((f0 + bh) / kSr, 0.0f, 0.49f), dev = f0 / kSr2;
  float inc_m0 = f0 * ratio / kSr2;
  int twin = tw > 0.0f || v.tw_s > 1e-5f;
  float* h0 = v.hb[0];
  float* h1 = v.hb[1];
  for (uint32_t i = 0; i < n; ++i) {
    // the envelope (at the output's rate)
    float lvl;
    if (v.att < 1.0f) {
      v.att += v.att_inc;
      float x = v.att < 1.0f ? v.att : 1.0f;
      float r = 0.5f - 0.5f * fm_cosf(3.14159265f * x);
      lvl = v.a0 + (1.0f - v.a0) * r;
    } else if (v.held && v.gate && !v.let_go) {
      lvl = 1.0f;
    } else {
      v.w = (v.w + v.u * v.dh) * v.dk;
      v.u *= v.dk;
      lvl = v.u + v.w;
      if (lvl < 1e-5f) {
        v.alive = 0;
        v.amp = 0.0f;
        for (uint32_t j = i; j < n; ++j) out[j] = 0.0f;
        return;
      }
    }
    v.amp = lvl;
    v.ie *= v.ik;
    float idx = v.move >= 0.0f ? i_base + v.move * 4.0f * v.ie : i_base - v.move * 4.0f * (1.0f - v.ie);
    idx = idx > i_max ? i_max : idx;
    if (v.snap) v.snap = 0, v.idx_s = idx, v.tw_s = tw, v.mo_s = mo;
    v.idx_s += (idx - v.idx_s) * 0.02f;
    v.tw_s += (tw - v.tw_s) * 0.002f;
    v.mo_s += (mo - v.mo_s) * 0.002f;
    float d = v.idx_s * dev, inc_m = inc_m0 + v.mo_s / kSr2;
    float y2[2];
    for (int k = 0; k < 2; ++k) {       // two samples at twice the rate
      // the modulator: a sine, to a triangle, to a saw (polyBLEP on its edge)
      float p = v.pm, m;
      if (sh < 1e-4f) {
        m = sin_cyc(p);
      } else if (sh < 0.5f) {
        float s = sh * 2.0f, tri = p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
        m = (1.0f - s) * sin_cyc(p) + s * tri;
      } else {
        float tri = p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
        float s = sh * 2.0f - 1.0f, q = p + 0.5f;
        q -= q >= 1.0f ? 1.0f : 0.0f;
        float saw = 2.0f * q - 1.0f - blep(q, inc_m);
        m = (1.0f - s) * tri + s * saw;
      }
      v.pm += inc_m;
      v.pm -= v.pm >= 1.0f ? 1.0f : 0.0f;
      // the carriers: linear FM, through zero (their phase may run backwards)
      float fm = d * m;                 // (a step under a cycle: swing x carrier < 0.5 of the rate, see i_max)
      float pc = v.pc + inc_c + fm;
      pc += pc < 0.0f ? 1.0f : pc >= 1.0f ? -1.0f : 0.0f;
      v.pc = pc;
      y2[k] = sin_cyc(pc);
    }
    // down to the output's rate: the half-band (path 0 on the second sample, path 1 on the first)
    float x0 = y2[1], x1 = y2[0];
    for (int s = 0; s < 3; ++s) {
      float c0 = kHb[2 * s], c1 = kHb[2 * s + 1];
      float o0 = c0 * (x0 - h0[s + 1]) + h0[s];
      h0[s] = x0;
      x0 = o0;
      float o1 = c1 * (x1 - h1[s + 1]) + h1[s];
      h1[s] = x1;
      x1 = o1;
    }
    h0[3] = x0;
    h1[3] = x1;
    float y = 0.5f * (x0 + x1);
    if (twin) {                         // the second sine: pure (no FM), beating with the first
      float pt = v.pt + inc_t;
      pt -= pt >= 1.0f ? 1.0f : 0.0f;
      v.pt = pt;
      y = (y + v.tw_s * sin_cyc(pt)) / (1.0f + v.tw_s);
    }
    out[i] = 0.8f * lvl * y;
  }
}

const char* zero_knob_name(int k) {
  static const char* const kN[8] = {"Index", "Ratio", "Shape", "Decay", "Beat", "Move", "Vary", "Pulse"};
  return k >= 0 && k < 8 ? kN[k] : "";
}

void zero_defaults(float* k) {          // round pulses of a sine, a breath of FM, a slow beat, a little loop
  static const float kD[8] = {0.2f, 2.5f / 11.0f, 0.0f, 0.3f, 0.3f, 0.5f, 0.4f, 0.5f};
  for (int i = 0; i < 8; ++i) k[i] = kD[i];
}

int zero_knob_steps(int k) { return k == 1 ? kRatios : 128; }

MI_COLD void zero_knob_text(int k, float u, char* b, int n) {   // u: 0..1 (a bipolar knob from its middle)
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  switch (k) {
    case 0: {                           // INDEX: the FM's depth, through zero past 1.00
      int x = static_cast<int>(500.0f * u * u + 0.5f);
      put_i(b, n, &at, x / 100);
      put_s(b, n, &at, ".");
      if (x % 100 < 10) put_s(b, n, &at, "0");
      put_i(b, n, &at, x % 100);
      return;
    }
    case 1: {
      int r = static_cast<int>(u * static_cast<float>(kRatios));
      put_s(b, n, &at, kRatioName[r < 0 ? 0 : r > kRatios - 1 ? kRatios - 1 : r]);
      return;
    }
    case 2:
      if (u < 0.02f) return put_s(b, n, &at, "Sine");
      if (u > 0.48f && u < 0.52f) return put_s(b, n, &at, "Tri");
      if (u > 0.98f) return put_s(b, n, &at, "Saw");
      break;
    case 3:
      if (u > 0.98f) return put_s(b, n, &at, "Hold");
      break;
    case 4: {                           // BEAT: its rate
      if (u * 40.0f < 0.5f) return put_s(b, n, &at, "Off");
      int x = static_cast<int>(800.0f * u * u + 0.5f);   // hundredths of Hz
      if (x < 100) {
        put_s(b, n, &at, ".");
        if (x < 10) put_s(b, n, &at, "0");
        put_i(b, n, &at, x);
      } else {
        put_i(b, n, &at, x / 100);
        put_s(b, n, &at, ".");
        put_i(b, n, &at, x / 10 % 10);
      }
      put_s(b, n, &at, "Hz");
      return;
    }
    case 5: case 6: case 7: {           // bipolar: MOVE, VARY (- in a loop, + new each trig), PULSE (- level, + timbre)
      int x = static_cast<int>((2.0f * u - 1.0f) * 100.0f + (u < 0.5f ? -0.5f : 0.5f));
      if (x > 0) put_s(b, n, &at, "+");
      if (x < 0) put_s(b, n, &at, "-"), x = -x;
      put_i(b, n, &at, x);
      put_s(b, n, &at, "%");
      return;
    }
    default:
      break;
  }
  put_i(b, n, &at, static_cast<int>(u * 100.0f + 0.5f));
  put_s(b, n, &at, "%");
}

}  // extern "C"
