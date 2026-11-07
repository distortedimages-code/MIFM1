// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's TAURUS (0.16.20): a monophonic bass after Moog's Taurus bass pedals (the 205A, 1975-81) and the Minimoog
// Model D (1970). The Taurus, as its owner's and service manual has it: two sawtooth oscillators, B against A (its
// pitch, a beat of +/-2 %, their balance), the Minimoog's patented transistor ladder (24 dB/oct, its emphasis up to
// oscillation), a contour on the cutoff that rises and falls whatever the pedal does (attack 5..56 ms, decay 50 ms ..
// 2.8 s: "non-sustaining"), a loudness contour (attack, decay, sustain; its DECAY switch: on, the sound fades at the
// decay's rate when the pedal is let go), glide (an RC's lag up to 0.5 s); its three fixed presets, TUBA, TAURUS and
// BASS. The Minimoog's: its six waves (triangle, shark tooth, saw, square, wide and narrow pulses), its third
// oscillator an octave under the other two (the sub), its mixer pushed into the ladder (its overdrive).
//
// A voice: A, B (and the Minimoog's C, the sub) free-running, each wandering a cent or two; their sum into the ladder
// (Stefano D'Angelo and Vesa Valimaki's improved model, as MIFM1's LADDER filter: a transistor pair's tanh at each of
// its four stages), its bass given back as the emphasis takes it (a real ladder loses its lows as it rings: here the
// input pushed by as much, most of it), then the loudness contour; a coupling capacitor, a soft knee.
//
// Eight knobs (k: 0..1, a bipolar knob from its middle): VOICE (the Taurus' three presets, then the Minimoog's six
// waves), BEAT (B off A: 0 .. 50 cents), MIX (to the middle B coming in to A's level, past it both pushed into the
// ladder: its drive), CUTOFF (30 Hz .. 11 kHz, the note's octave a half), EMPHASIS, CONTOUR (to the right its depth, 0
// .. 7 octaves; to the left a swell, the loudness and the contour rising slowly together), DECAY (both contours', 50 ms
// .. 2.8 s, and the loudness' release: the DECAY switch on), GLIDE (0 .. 0.5 s).
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f;
const int kSub = 16;                    // the contours, the cutoff, the glide: their steps (samples)

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }
inline uint32_t xs(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline float bip(uint32_t& s) { return static_cast<float>(xs(s) >> 8) * (2.0f / 16777216.0f) - 1.0f; }

// the waves (the Minimoog's switch, in its order)
enum { W_TRI, W_SHARK, W_SAW, W_SQUARE, W_WIDE, W_NARROW };
// VOICE: the Taurus' presets, then the Minimoog's waves. Each: its wave; B's interval over A (semitones) and C's (the
// sub: 0 none); C's level; the loudness' sustain and attack; the contour's sustain; the ladder's drive; how much the
// cutoff follows the note
struct VoiceDef {
  uint8_t wave;
  int8_t b_semis;
  float sub, sustain, attack, cont_sus, drive, kt, gain;
};
const int kVoicesN = 9;
const VoiceDef kVoiceDef[kVoicesN] = {
    // TAURUS: "a throaty roar, gradually decaying to a rounded purr as the pedal is held down": A and B saws, B an
    // octave up (its fundamental A's alone, solid; B beating with A's second harmonic: the swirl), held
    {W_SAW, 12, 0.0f, 1.0f, 0.004f, 0.0f, 1.25f, 0.5f, 1.0f},
    // BASS: "chunky, plucked, its decay fast": both at 16', the loudness falling to nothing with DECAY
    {W_SAW, 0, 0.0f, 0.0f, 0.002f, 0.0f, 1.45f, 0.5f, 1.2f},
    // TUBA: "the lows filled without much character, the ultimate sub-oscillator": B an octave under, a soft
    // attack, its cutoff following the note less
    {W_SAW, -12, 0.0f, 0.85f, 0.035f, 0.0f, 1.1f, 0.3f, 1.2f},
    // the Minimoog's: A and B (by the beat) at 16', C at 32' under them; their contour's sustain a third
    {W_TRI, 0, 0.75f, 0.8f, 0.003f, 0.3f, 1.6f, 0.66f, 1.05f},
    {W_SHARK, 0, 0.75f, 0.8f, 0.003f, 0.3f, 1.5f, 0.66f, 1.0f},
    {W_SAW, 0, 0.6f, 0.8f, 0.003f, 0.3f, 1.35f, 0.66f, 1.1f},
    {W_SQUARE, 0, 0.6f, 0.8f, 0.003f, 0.3f, 1.35f, 0.66f, 0.85f},
    {W_WIDE, 0, 0.6f, 0.8f, 0.003f, 0.3f, 1.35f, 0.66f, 0.8f},
    {W_NARROW, 0, 0.6f, 0.8f, 0.003f, 0.3f, 1.35f, 0.66f, 1.05f},
};
const char* const kVoiceName[kVoicesN] = {"Taurus", "Bass", "Tuba", "Tri", "Shark", "Saw", "Square", "Wide", "Narrow"};
inline int voice_of(float u) {
  int i = static_cast<int>(clampf(u, 0.0f, 1.0f) * static_cast<float>(kVoicesN));
  return i > kVoicesN - 1 ? kVoicesN - 1 : i;
}
inline float beat_cents(float u) { return 50.0f * u * u; }                  // BEAT: 0 .. 50 cents (3 %)
inline float cutoff_hz(float u) { return 30.0f * fm_exp2f(u * 8.5f); }      // CUTOFF: 30 Hz .. 10.9 kHz
inline float decay_s(float u) { return 0.05f * fm_exp2f(u * 5.807f); }      // DECAY: 50 ms .. 2.8 s (a time constant)
inline float glide_s(float u) { return 0.5f * u * u; }                      // GLIDE: 0 .. 0.5 s (a time constant)
inline float emph_k(float u) { return 4.3f * u * (2.0f - u) * u; }          // EMPHASIS: the ladder's loop, past 4 it sings
const float kInvVt2 = 1.0f / (2.0f * 0.312f);   // (the ladder: its transistors' thermal voltage, 0.312, twice)
inline float sat(float x) {             // the transistor pair's tanh (a cubic's, flat past 1.5; held there, no branch)
  x = x < -1.5f ? -1.5f : x;
  x = x > 1.5f ? 1.5f : x;
  return x - (4.0f / 27.0f) * x * x * x;
}

struct Osc {
  float ph;                             // its phase (0..1)
  float dr, dr_t;                       // its wander (cents), where it goes
};
struct Taurus {
  float k[8];
  float note, target;                   // the pitch now (glided), the one played
  int gate, trig;
  Osc o[3];                             // A, B, C (the sub)
  float V[4], dV[4], tV[4];             // the ladder
  float amp, cont;                      // the contours: their levels
  int amp_att, cont_att;                // rising
  float a0, c0, ap, cp;                 // (an attack's: where it began, its progress)
  float dcx, dcy;                       // the coupling capacitor
  uint32_t rnd;
  int dr_n;
  int alive;
};
static_assert(sizeof(Taurus) <= MF_STATE_BYTES, "TAURUS' state");
struct TaurusAt {
  Taurus& operator[](int t) const { return *static_cast<Taurus*>(mf_state(t)); }
};
const TaurusAt taurus = {};

// a step of J at the sample's fraction t past the edge (0..1, dt its phase step): the two-point polyBLEP's residual,
// on this sample and the next (a bass's partials: its alias far under its own)
inline float blep_after(float t) { return t - 0.5f * t * t - 0.5f; }   // (this sample, t past the edge: x = t / dt)
inline float blep_before(float t) { return 0.5f * t * t + t + 0.5f; }  // (the sample before the edge, t = x / dt - 1)
inline float poly_blep(float ph, float dt) {
  if (ph < dt) return blep_after(ph / dt);
  if (ph > 1.0f - dt) return blep_before((ph - 1.0f) / dt);
  return 0.0f;
}
const float kPw[3] = {0.5f, 0.3f, 0.12f};   // the square, the wide pulse, the narrow one (the Minimoog's: by eye)

// m samples of the oscillators (A, B at bl, C at sl when there) into x, by family (the code small: the flash's room):
// the saw; the pulses (their width pw); the triangles (rising over r of the cycle: the triangle a half, the shark tooth
// three quarters)
__attribute__((noinline)) void osc_saw(float* x, uint32_t m, float* ph, const float* d, float bl, float sl) {
  float pa = ph[0], pb = ph[1], pc = ph[2];
  for (uint32_t i = 0; i < m; ++i) {
    float v = (1.0f - 2.0f * pa + 2.0f * poly_blep(pa, d[0])) + bl * (1.0f - 2.0f * pb + 2.0f * poly_blep(pb, d[1]));
    if (sl > 0.0f) v += sl * (1.0f - 2.0f * pc + 2.0f * poly_blep(pc, d[2]));
    x[i] = v;
    pa += d[0];
    pa -= pa >= 1.0f ? 1.0f : 0.0f;
    pb += d[1];
    pb -= pb >= 1.0f ? 1.0f : 0.0f;
    pc += d[2];
    pc -= pc >= 1.0f ? 1.0f : 0.0f;
  }
  ph[0] = pa, ph[1] = pb, ph[2] = pc;
}
inline float pulse(float p, float pw, float dt) {
  float p2 = p - pw;
  p2 += p2 < 0.0f ? 1.0f : 0.0f;
  return (p < pw ? 1.0f : -1.0f) + 2.0f * (poly_blep(p, dt) - poly_blep(p2, dt)) - (2.0f * pw - 1.0f);
}
__attribute__((noinline)) void osc_pulse(float* x, uint32_t m, float* ph, const float* d, float bl, float sl, float pw) {
  float pa = ph[0], pb = ph[1], pc = ph[2];
  for (uint32_t i = 0; i < m; ++i) {
    float v = pulse(pa, pw, d[0]) + bl * pulse(pb, pw, d[1]);
    if (sl > 0.0f) v += sl * pulse(pc, pw, d[2]);
    x[i] = v;
    pa += d[0];
    pa -= pa >= 1.0f ? 1.0f : 0.0f;
    pb += d[1];
    pb -= pb >= 1.0f ? 1.0f : 0.0f;
    pc += d[2];
    pc -= pc >= 1.0f ? 1.0f : 0.0f;
  }
  ph[0] = pa, ph[1] = pb, ph[2] = pc;
}
inline float tri(float p, float r, float ir, float ir1) {   // (its corners: their alias at -90 dB)
  return p < r ? 2.0f * p * ir - 1.0f : 1.0f - 2.0f * (p - r) * ir1;
}
__attribute__((noinline)) void osc_tri(float* x, uint32_t m, float* ph, const float* d, float bl, float sl, float r) {
  const float ir = 1.0f / r, ir1 = 1.0f / (1.0f - r);
  float pa = ph[0], pb = ph[1], pc = ph[2];
  for (uint32_t i = 0; i < m; ++i) {
    float v = tri(pa, r, ir, ir1) + bl * tri(pb, r, ir, ir1);
    if (sl > 0.0f) v += sl * tri(pc, r, ir, ir1);
    x[i] = v;
    pa += d[0];
    pa -= pa >= 1.0f ? 1.0f : 0.0f;
    pb += d[1];
    pb -= pb >= 1.0f ? 1.0f : 0.0f;
    pc += d[2];
    pc -= pc >= 1.0f ? 1.0f : 0.0f;
  }
  ph[0] = pa, ph[1] = pb, ph[2] = pc;
}
// m samples of x through the ladder (g its stages' gain, res its emphasis; V, dV, tV its states), into y at the
// loudness gliding from e by de: the input (pushed, its bass given back) less the emphasis' loop; four stages, a tanh
// each (its states in the registers)
__attribute__((noinline)) void ladder(const float* x, float* y, uint32_t m, float in_gain, float res, float g, float e,
                                      float de, float* V, float* dV, float* tV) {
  float v0 = V[0], v1 = V[1], v2 = V[2], v3 = V[3], q0 = dV[0], q1 = dV[1], q2 = dV[2], q3 = dV[3];
  float t0 = tV[0], t1 = tV[1], t2 = tV[2], t3 = tV[3];
  for (uint32_t i = 0; i < m; ++i) {
    const float u = in_gain * x[i] - res * v3;
    const float d0 = g * (sat(u * kInvVt2) - t0);
    v0 += d0 + q0;
    q0 = d0;
    t0 = sat(v0 * kInvVt2);
    const float d1 = g * (t0 - t1);
    v1 += d1 + q1;
    q1 = d1;
    t1 = sat(v1 * kInvVt2);
    const float d2 = g * (t1 - t2);
    v2 += d2 + q2;
    q2 = d2;
    t2 = sat(v2 * kInvVt2);
    const float d3 = g * (t2 - t3);
    v3 += d3 + q3;
    q3 = d3;
    t3 = sat(v3 * kInvVt2);
    e += de;
    y[i] = v3 * e;
  }
  V[0] = v0, V[1] = v1, V[2] = v2, V[3] = v3, dV[0] = q0, dV[1] = q1, dV[2] = q2, dV[3] = q3;
  tV[0] = t0, tV[1] = t1, tV[2] = t2, tV[3] = t3;
}
}  // namespace

extern "C" {

MI_COLD void taurus_reset(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Taurus& p = taurus[t];
  uint8_t* raw = reinterpret_cast<uint8_t*>(&p);
  for (uint32_t i = 0; i < sizeof(Taurus); ++i) raw[i] = 0;
  p.rnd = 0x9E3779B9u * static_cast<uint32_t>(t + 5) | 1u;
  float d[8];
  taurus_defaults(d);
  for (int i = 0; i < 8; ++i) p.k[i] = d[i];
  p.note = p.target = 36.0f;
  for (int i = 0; i < 3; ++i) p.o[i].ph = 0.5f + 0.5f * bip(p.rnd);   // (free-running: never in phase)
}

MI_COLD void taurus_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) taurus_reset(t);
  mf_forget();
}

MI_COLD void taurus_silence(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Taurus& p = taurus[t];
  p.trig = 0;
  p.amp = p.cont = 0.0f;
  p.amp_att = p.cont_att = 0;
  p.alive = 0;
  for (int i = 0; i < 4; ++i) p.V[i] = p.dV[i] = p.tV[i] = 0.0f;
  p.dcx = p.dcy = 0.0f;
}

MI_COLD void taurus_set(int t, float note, const float* k) {
  if (t < 0 || t >= MF_TRACKS) return;
  Taurus& p = taurus[t];
  for (int i = 0; i < 8; ++i) p.k[i] = clampf(k[i], 0.0f, 1.0f);
  p.target = note;
  if (!p.alive || glide_s(p.k[7]) < 0.001f) p.note = note;   // (from silence, or no glide: there at once)
}

MI_COLD void taurus_gate(int t, int g) {
  if (t < 0 || t >= MF_TRACKS) return;
  Taurus& p = taurus[t];
  if (g && !p.gate) p.trig = 1;
  p.gate = g;
}

int taurus_voice(float u) { return voice_of(u); }

MI_COLD void taurus_render(int t, float* out, uint32_t n) {   // (its glue for size: its samples' loops at -O2)
  if (t < 0 || t >= MF_TRACKS) return;
  Taurus& p = taurus[t];
  if (p.trig) {                         // a trig: both contours from where they are (no click), rising
    p.trig = 0;
    p.a0 = p.amp, p.ap = 0.0f, p.amp_att = 1;
    p.c0 = p.cont, p.cp = 0.0f, p.cont_att = 1;
    p.alive = 1;
  }
  if (!p.alive && fm_fabsf(p.dcy) < 1e-6f) {   // (silent: the capacitor's tail out too)
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    p.dcx = p.dcy = 0.0f;
    return;
  }
  const VoiceDef& vd = kVoiceDef[voice_of(p.k[0])];
  const float beat = beat_cents(p.k[1]);
  // MIX: to the middle B rising to A's level, past it both pushed (x1 .. x3 into the ladder)
  const float mix = p.k[2], bl = mix < 0.5f ? 2.0f * mix : 1.0f, push = mix < 0.5f ? 1.0f : 1.0f + 4.0f * (mix - 0.5f);
  const float sl = vd.sub;
  const float lvl = 0.55f / (1.0f + 0.5f * (bl + sl));   // (the oscillators' sum: A alone +/-0.55 into the ladder)
  const float res = emph_k(p.k[4]);
  const float in_gain = lvl * push * vd.drive;   // (times 1 + 0.75 res: its bass given back, most of what it takes)
  const float c = 2.0f * p.k[5] - 1.0f;   // CONTOUR: + its depth, - a swell
  const float depth = c >= 0.0f ? 7.0f * c : 3.0f * -c;   // (octaves)
  const float swell = c < 0.0f ? 1.5f * c * c : 0.0f;
  const float att_a = vd.attack + swell, att_c = 0.005f + swell;   // (the contour's: the Taurus' 5 ms)
  const float tau = decay_s(p.k[6]);
  const float gl = glide_s(p.k[7]);
  const float fc0 = cutoff_hz(p.k[3]);
  // the wander: each oscillator a new aim every 0.4 s, a cent or two
  p.dr_n -= static_cast<int>(n);
  if (p.dr_n <= 0) {
    p.dr_n = 17640;
    for (int i = 0; i < 3; ++i) p.o[i].dr_t = 1.5f * bip(p.rnd);
  }
  const float wk = clampf(static_cast<float>(n) * 2e-5f, 0.0f, 1.0f);
  for (int i = 0; i < 3; ++i) p.o[i].dr += (p.o[i].dr_t - p.o[i].dr) * wk;
  const float kd = fm_expf(-static_cast<float>(kSub) / (tau * kSr));   // (a step's fall, both contours)
  const float kg = gl < 0.001f ? 0.0f : fm_expf(-static_cast<float>(kSub) / (gl * kSr));
  const int w = vd.wave;
  float* V = p.V;
  float* dV = p.dV;
  float* tV = p.tV;
  for (uint32_t s0 = 0; s0 < n; s0 += kSub) {
    const uint32_t m = n - s0 < static_cast<uint32_t>(kSub) ? n - s0 : static_cast<uint32_t>(kSub);
    // the glide (an RC's lag), the pitches
    p.note = p.target + (p.note - p.target) * kg;
    const float fa = note_hz(p.note + p.o[0].dr * 0.01f);
    const float da = clampf(fa / kSr, 1e-6f, 0.45f);
    const float db = clampf(da * fm_exp2f((static_cast<float>(vd.b_semis) * 100.0f + beat + p.o[1].dr) * (1.0f / 1200.0f)),
                            1e-6f, 0.45f);
    const float dc = clampf(da * fm_exp2f((-1200.0f - 0.5f * beat + p.o[2].dr) * (1.0f / 1200.0f)), 1e-6f, 0.45f);
    // the contours at the step's end: the loudness (attack, decay to its sustain while held, then its release at
    // DECAY's rate), the cutoff's (attack, decay to its sustain: the Taurus' none)
    const float e0 = p.amp;
    if (p.amp_att) {
      p.ap += static_cast<float>(m) / (att_a * kSr);
      const float a = p.ap < 1.0f ? p.ap : 1.0f;
      p.amp = p.a0 + (1.0f - p.a0) * a * (2.0f - a);   // (an RC's: fast, then settling)
      if (p.ap >= 1.0f) p.amp_att = 0;
    } else {
      const float to = p.gate ? vd.sustain : 0.0f;
      p.amp = to + (p.amp - to) * kd;
    }
    if (p.cont_att) {
      p.cp += static_cast<float>(m) / (att_c * kSr);
      const float a = p.cp < 1.0f ? p.cp : 1.0f;
      p.cont = p.c0 + (1.0f - p.c0) * a * (2.0f - a);
      if (p.cp >= 1.0f) p.cont_att = 0;
    } else {
      const float to = p.gate ? vd.cont_sus : 0.0f;
      p.cont = to + (p.cont - to) * kd;
    }
    // the ladder's g at this cutoff (the knob, the note at kt, the contour), its loop's ring there (0.16.30: ladder_g)
    float hz = fc0 * fm_exp2f((p.note - 36.0f) * (vd.kt / 12.0f) + depth * p.cont), kc;
    hz = clampf(hz, 10.0f, 20000.0f);
    const float g = ladder_g(hz, &kc);
    const float e = e0, de = (p.amp - e0) / static_cast<float>(m);
    float ph[3] = {p.o[0].ph, p.o[1].ph, p.o[2].ph};
    const float dd[3] = {da, db, dc};
    float x[kSub];
    if (w == W_SAW) osc_saw(x, m, ph, dd, bl, sl);
    else if (w >= W_SQUARE) osc_pulse(x, m, ph, dd, bl, sl, kPw[w - W_SQUARE]);
    else osc_tri(x, m, ph, dd, bl, sl, w == W_TRI ? 0.5f : 0.75f);
    p.o[0].ph = ph[0], p.o[1].ph = ph[1], p.o[2].ph = ph[2];
    ladder(x, out + s0, m, in_gain * (1.0f + 0.75f * res * kc), res * kc, g, e, de, V, dV, tV);
  }
  if (!p.gate && !p.amp_att && p.amp < 1e-5f) {   // (its sound over: it rests, the ladder's states from rest)
    p.alive = 0;
    p.amp = 0.0f;
    for (int i = 0; i < 4; ++i) p.V[i] = p.dV[i] = p.tV[i] = 0.0f;
  }
  // the coupling capacitor (~5 Hz), the level (each voice's: their loudness alike), a soft knee above 0.8 (at most 1.2)
  mi_finish(out, n, 0.45f * vd.gain, &p.dcx, &p.dcy);
  if (!p.alive && fm_fabsf(p.dcy) < 1e-6f) p.dcx = p.dcy = 0.0f;
}

const char* taurus_knob_name(int k) {
  static const char* const kN[8] = {"Voice", "Beat", "Mix", "Cutoff", "Emph", "Contour", "Decay", "Glide"};
  return k >= 0 && k < 8 ? kN[k] : "";
}

MI_COLD void taurus_defaults(float* k) {        // the Taurus: B beating a little, as loud as A; the roar, then the purr
  static const float kD[8] = {0.5f / 9.0f, 0.35f, 0.5f, 0.3f, 0.25f, 0.78f, 0.55f, 0.0f};
  for (int i = 0; i < 8; ++i) k[i] = kD[i];
}

int taurus_knob_steps(int k) { return k == 0 ? kVoicesN : 128; }

MI_COLD void taurus_knob_text(int k, float u, char* b, int n) {
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  switch (k) {
    case 0: return put_s(b, n, &at, kVoiceName[voice_of(u)]);
    case 1: {                           // BEAT: cents
      const int c = static_cast<int>(beat_cents(u) + 0.5f);
      if (!c) return put_s(b, n, &at, "Locked");
      put_i(b, n, &at, c);
      return put_s(b, n, &at, "c");
    }
    case 2:                             // MIX: B's level, then the push
      if (u < 0.495f) {
        put_s(b, n, &at, "B ");
        put_i(b, n, &at, static_cast<int>(u * 200.0f + 0.5f));
        return put_s(b, n, &at, "%");
      }
      if (u < 0.505f) return put_s(b, n, &at, "A=B");
      put_s(b, n, &at, "Hot ");
      put_i(b, n, &at, static_cast<int>((u - 0.5f) * 200.0f + 0.5f));
      return put_s(b, n, &at, "%");
    case 3: {                           // CUTOFF: Hz
      const float hz = cutoff_hz(u);
      if (hz >= 1000.0f) {
        const int x = static_cast<int>(hz * 0.01f + 0.5f);
        put_i(b, n, &at, x / 10);
        put_s(b, n, &at, ".");
        put_i(b, n, &at, x % 10);
        return put_s(b, n, &at, "k");
      }
      put_i(b, n, &at, static_cast<int>(hz + 0.5f));
      return put_s(b, n, &at, "Hz");
    }
    case 5: {                           // CONTOUR: + its octaves, - the swell's time
      const float c = 2.0f * u - 1.0f;
      if (c > -0.01f && c < 0.01f) return put_s(b, n, &at, "Off");
      if (c > 0.0f) {
        const int x = static_cast<int>(70.0f * c + 0.5f);
        put_s(b, n, &at, "+");
        put_i(b, n, &at, x / 10);
        put_s(b, n, &at, ".");
        put_i(b, n, &at, x % 10);
        return put_s(b, n, &at, "oct");
      }
      put_s(b, n, &at, "Sw ");
      return put_time(b, n, &at, 1.5f * c * c);
    }
    case 6: return put_time(b, n, &at, decay_s(u));
    case 7:
      if (glide_s(u) < 0.001f) return put_s(b, n, &at, "Off");
      return put_time(b, n, &at, glide_s(u));
    default:                            // EMPHASIS: 0..100
      put_i(b, n, &at, static_cast<int>(u * 100.0f + 0.5f));
      return;
  }
}

}  // extern "C"
