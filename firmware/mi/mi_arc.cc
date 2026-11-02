// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's own voice, ARC (0.8): an electric sound, between a pure sine bass and the parasites of a circuit, a Tesla
// coil's buzz (after Haus Arafna's). A sine carries the note; it is phase-modulated (FM) by its parasites: an
// operator with its own feedback (a DX7's operator 6: a sine at no feedback, a saw towards one, a tonal noise beyond)
// on a ratio of the note COLOR sets; XOR, the note's square times the operator's (a logic gate's ring modulation);
// and sparks, two resonators tuned on the note's harmonics, struck at random (as an arc cracks) and on the carrier's
// cycles (as a Tesla coil's interrupter fires: the buzz on the pitch). The parasites (not the sine) go through a
// double filter after the MS-20's, a high pass into a Korg 35 low pass, its resonance saturating in its loop (Pirkle's
// model, as mi_filter.cc's MS-20): they sing at its peak before they modulate. A part of them is heard as it is.
// SPARK at 0: a pure sine; up, it crackles, buzzes, sizzles, screams, the note always under it.
//
// Each trig is another sound (VARY: how far it goes): the operator's ratio, the parasites' amount, the decay, the
// sparks' harmonics, their timing, the note's wander (their own random, seeded at the trig). LOOP keeps the randoms
// in a loop, as Music Thing's Turing Machine does with its bits: sixteen trigs' seeds, each trig either the one of
// sixteen trigs ago (LOOP at the end: the same sixteen sounds, sparks and all, again and again) or a new one in its
// place (LOOP at 0).
// The trigs give the time; the loop gives the sounds.
//
// Its knobs, the FM drums' places (the track's knobs after FREQ): SPARK (HARMONICS: the parasites, and the filter's
// resonance with them), COLOR (TIMBRE: the operator's ratio, the sparks' harmonics, the filter's cutoff), VARY
// (MORPH), DECAY, LOOP (COLOUR), S.ENV (T.ENV: + the parasites at the attack, falling to the sine; - growing over the
// note), XOR (M.ENV: the logic gate's share of them), P.ENV (a pitch sweep at the trig: a zap, a kick). The note
// wanders a little, slowly, as an old circuit does (more with VARY). Its own envelope: a 1.5 ms rise, DECAY's fall
// (20 ms to 6 s; at the end, held while the gate is); a trig after silence starts its oscillators at 0 (the same
// attack each time), a trig over a sound goes on from where they are (no click).
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f;
const float kTwoPi = 6.2831853f;
const int kLoop = 16;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

// a sine of a phase in cycles (a 9th-order polynomial on the quarter wave, fitted: 3e-9 off, no table)
inline float sin_cyc(float ph) {
  ph -= fm_floorf(ph);
  float x = ph < 0.5f ? ph : ph - 0.5f, s = ph < 0.5f ? 1.0f : -1.0f;   // half a cycle, its sign
  x = x < 0.25f ? x : 0.5f - x;         // a quarter
  float z = x * 4.0f, z2 = z * z;       // 0..1 over the quarter
  float y = z * (1.5707963f + z2 * (-0.64596336f + z2 * (0.079688469f + z2 * (-0.0046722113f + z2 * 0.00015081274f))));
  return s * y;
}

struct Res {                            // a resonator: a two-pole on a harmonic, struck
  float a1, a2, y1, y2, g;
};

struct Arc {
  // the knobs as the trig plays them
  float note, spark, color, vary, decay, loop, senv, xr, penv;
  // this trig's draws
  float t_ratio, t_spark, t_decay, t_detune;
  int t_h[2];
  // the voice
  float ph, mph, mfb;                   // the carrier's, the operator's phase (cycles), its feedback's last two
  float mfb2;
  float amp, attack, a0, dk;            // the envelope: its level, the rise (0..1), where it rose from, its fall
  int gate, held, trig, alive;
  float pe;                             // the pitch envelope
  float se, sez;                        // the parasites' envelope (its own fall), its depth smoothed (no step: no click)
  Res res[2];
  float k35[3], hp;                     // the double filter: the Korg 35's one-poles, the high pass before it
  uint32_t rnd;                         // the sparks' random (seeded at the trig)
  float dr, dr_t;                       // the drift: where, where it goes (the trig's random: looped too)
  int dr_n;
  uint32_t loop_seed[kLoop];
  int loop_pos;
  uint32_t fresh;                       // the new seeds' random (never looped)
};

Arc arc[MF_TRACKS];

inline uint32_t xs(uint32_t& s) {       // xorshift32
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline float u01(uint32_t& s) { return static_cast<float>(xs(s) >> 8) * (1.0f / 16777216.0f); }
inline float bip(uint32_t& s) { return 2.0f * u01(s) - 1.0f; }

// polyBLEP: a step's alias taken off around it (t the phase since the step, dt a sample's)
inline float blep(float t, float dt) {
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
inline float square_bl(float ph, float dt) {   // a square, band-limited (+1 then -1)
  float h = ph + 0.5f;
  h -= h >= 1.0f ? 1.0f : 0.0f;
  return (ph < 0.5f ? 1.0f : -1.0f) + blep(ph, dt) - blep(h, dt);
}

inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }

void tune_res(Res& r, float hz, float decay_s) {   // a resonator at hz, its ring decay_s
  hz = clampf(hz, 30.0f, 16000.0f);
  float w = kTwoPi * hz / kSr;
  float rad = fm_expf(-1.0f / (decay_s * kSr));
  r.a1 = 2.0f * rad * fm_cosf(w);
  r.a2 = -rad * rad;
  r.g = fm_sinf(w);                     // (a strike's ring about 1, whatever its pitch)
}

void new_trig(Arc& v) {                 // the trig's sound: the loop's seed (or a new one), its draws
  if (u01(v.fresh) >= v.loop * v.loop * 0.98f + (v.loop > 0.995f ? 0.02f : 0.0f))   // (LOOP squared: the middle
    v.loop_seed[v.loop_pos] = xs(v.fresh) | 1u;                                       //  still moves; the end locks)
  uint32_t s = v.loop_seed[v.loop_pos];
  v.loop_pos = (v.loop_pos + 1) % kLoop;
  float a = v.vary;
  v.t_ratio = a * a * 1.2f * bip(s);    // the operator's ratio, octaves either way
  v.t_spark = a * 0.6f * bip(s);
  v.t_decay = a * 0.5f * bip(s);
  v.t_detune = a * 0.15f * bip(s);      // (semitones: a circuit's, not a melody's)
  static const int kH[8] = {2, 3, 5, 7, 9, 11, 13, 17};
  int base = static_cast<int>(v.color * 4.0f + 0.5f);
  for (int i = 0; i < 2; ++i) {
    int j = base + i * 2 + static_cast<int>(a * 3.0f * u01(s));
    v.t_h[i] = kH[j < 0 ? 0 : j > 7 ? 7 : j];
  }
  v.rnd = s ^ 0x9E3779B9u;              // the sparks' own random, the wander's: the loop's too
  if (!v.rnd) v.rnd = 1u;
  v.dr = 0.6f * bip(v.rnd);
  v.dr_t = bip(v.rnd);
  v.dr_n = 2048;
}

}  // namespace

extern "C" {

void arc_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) {
    Arc& v = arc[t];
    uint8_t* raw = reinterpret_cast<uint8_t*>(&v);
    for (uint32_t i = 0; i < sizeof(Arc); ++i) raw[i] = 0;
    v.fresh = 0x2545F491u * static_cast<uint32_t>(t + 1);
    for (int i = 0; i < kLoop; ++i) v.loop_seed[i] = xs(v.fresh) | 1u;
    v.rnd = 1u;
    v.note = 36.0f;
    v.dr_t = 0.0f;
  }
}

void arc_silence(void) {
  for (int t = 0; t < MF_TRACKS; ++t) arc[t].alive = 0, arc[t].amp = 0.0f, arc[t].trig = 0;
}

void arc_set(int t, float note, const float* k) {
  if (t < 0 || t >= MF_TRACKS) return;
  Arc& v = arc[t];
  v.note = note;
  v.spark = clampf(k[0], 0.0f, 1.0f);
  v.color = clampf(k[1], 0.0f, 1.0f);
  v.vary = clampf(k[2], 0.0f, 1.0f);
  v.decay = clampf(k[3], 0.0f, 1.0f);
  v.loop = clampf(k[4], 0.0f, 1.0f);
  v.senv = clampf(2.0f * k[5] - 1.0f, -1.0f, 1.0f);
  v.xr = clampf(k[6], 0.0f, 1.0f);
  v.penv = clampf(2.0f * k[7] - 1.0f, -1.0f, 1.0f);
}

void arc_gate(int t, int g) {
  if (t < 0 || t >= MF_TRACKS) return;
  Arc& v = arc[t];
  if (g && !v.gate) v.trig = 1;
  v.gate = g;
}

void arc_render(int t, float* out, uint32_t n) {
  if (t < 0 || t >= MF_TRACKS) return;
  Arc& v = arc[t];
  if (v.trig) {                         // a trig: its sound, the envelopes from the top
    v.trig = 0;
    new_trig(v);
    if (v.amp < 0.003f) {               // after silence: the same start each time (over a sound: on from there)
      v.ph = v.mph = v.mfb = v.mfb2 = v.sez = 0.0f;
      for (int i = 0; i < 2; ++i) v.res[i].y1 = v.res[i].y2 = 0.0f;
      v.k35[0] = v.k35[1] = v.k35[2] = v.hp = 0.0f;
    }
    v.attack = 0.0f;                    // (the rise: from where the sound is, to 1, a smooth step)
    v.a0 = v.amp;
    v.pe = 1.0f;
    v.se = 1.0f;
    v.alive = 1;
  }
  if (!v.alive) {
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    return;
  }
  // the block's settings: the decay (this trig's), the sparks' amount, the operator, the resonators
  float dec = clampf(v.decay + v.t_decay, 0.0f, 1.0f);
  float tau = 0.02f * fm_exp2f(dec * 8.2f);   // 20 ms .. 6 s
  v.held = v.decay > 0.98f;             // (at the end: held while the gate is)
  v.dk = fm_expf(-1.0f / (tau * kSr));
  float sp = clampf(v.spark + v.t_spark * v.spark, 0.0f, 1.0f);
  float f0 = note_hz(v.note + v.t_detune);
  float ratio = fm_exp2f(v.color * 5.0f - 1.0f + v.t_ratio);   // 0.5 .. 16 (inharmonic between the marks)
  float fb = sp * sp * 1.7f;            // the operator's feedback: a sine, a saw, a tonal noise
  float idx_op = sp * sp * 2.2f, idx_sp = sp * 3.0f, direct = sp * 0.18f;
  float density = sp * sp * (8.0f + 600.0f * v.color) / kSr;   // the random sparks, a sample
  float fire = 0.15f + 0.8f * sp;       // the interrupter's: a spark on a cycle's start, its chance
  for (int i = 0; i < 2; ++i) tune_res(v.res[i], f0 * static_cast<float>(v.t_h[i]), 0.002f + 0.012f * (1.0f - v.color));
  float pk = fm_exp2f(-1.0f / (0.004f + 0.05f * fm_fabsf(v.penv)) / kSr * 1.4427f);   // (the pitch sweep's fall)
  float sk = fm_exp2f(-1.0f / (0.02f + 0.4f * (1.0f - fm_fabsf(v.senv))) / kSr * 1.4427f);
  float dr_amt = 0.08f + 0.5f * v.vary * v.vary;   // (semitones at most: the drift)
  // the double filter: a one-pole high pass under the note, then the Korg 35 low pass, its cutoff COLOR's harmonic
  // of the note, its resonance SPARK's (K to 1.9: it screams, its loop's saturation holds it)
  float lpf = clampf(f0 * fm_exp2f(1.0f + v.color * 6.0f), 60.0f, 14000.0f), hpf = clampf(f0 * 0.7f, 20.0f, 4000.0f);
  float g = fm_tanf(3.14159265f * lpf / kSr), G = g / (1.0f + g);
  float K = 0.2f + 1.7f * sp, a0 = 1.0f / (1.0f - K * G + K * G * G);
  float b2 = (K - K * G) / (1.0f + g), b3 = -1.0f / (1.0f + g), iK = 1.0f / K;
  float gh = fm_tanf(3.14159265f * hpf / kSr), Gh = gh / (1.0f + gh);
  float xor_amt = v.xr;
  for (uint32_t i = 0; i < n; ++i) {
    // the envelope
    if (v.attack < 1.0f) {
      v.attack += 1.0f / (0.0015f * kSr);
      float a = v.attack < 1.0f ? v.attack : 1.0f;
      v.amp = v.a0 + (1.0f - v.a0) * a * a * (3.0f - 2.0f * a);
    } else if (!(v.held && v.gate)) {
      v.amp *= v.held ? 0.99985f : v.dk;   // (held: let go, a 150 ms fall)
    }
    if (v.amp < 1e-5f && v.attack >= 1.0f) {
      v.alive = 0;
      v.amp = 0.0f;
      for (uint32_t j = i; j < n; ++j) out[j] = 0.0f;
      return;
    }
    // the drift: a slow random walk
    if (--v.dr_n <= 0) {
      v.dr_n = 2048;
      v.dr_t = bip(v.rnd);
    }
    v.dr += (v.dr_t - v.dr) * 0.00012f;
    v.pe *= pk;
    v.se *= sk;
    float pitch = v.penv * v.pe * 36.0f + v.dr * dr_amt;
    float f = f0 * fm_exp2f(pitch * (1.0f / 12.0f));
    float inc = clampf(f / kSr, 0.0f, 0.45f);
    // the operator, its own feedback (two samples averaged, as the DX7)
    float m = sin_cyc(v.mph + fb * 0.5f * (v.mfb + v.mfb2) * 0.159f);
    v.mfb2 = v.mfb;
    v.mfb = m;
    // XOR: the note's square times the operator's (both band-limited: a gate's, without its alias)
    float minc = clampf(inc * ratio, 0.0f, 0.45f);
    float xo = square_bl(v.ph, inc) * square_bl(v.mph, minc);
    v.mph += minc;
    v.mph -= fm_floorf(v.mph);
    // the sparks: on the carrier's cycles (the interrupter), and at random (the arcs)
    float hit = 0.0f;
    float nph = v.ph + inc;
    if (nph >= 1.0f && u01(v.rnd) < fire * sp) hit = 0.5f + 0.5f * u01(v.rnd);
    if (u01(v.rnd) < density) hit += bip(v.rnd);
    float s = 0.0f;
    for (int r = 0; r < 2; ++r) {
      Res& z = v.res[r];
      float y = z.a1 * z.y1 + z.a2 * z.y2 + z.g * (r ? -hit : hit);
      z.y2 = z.y1;
      z.y1 = y;
      s += y;
    }
    s = s / (1.0f + fm_fabsf(s));       // (an arc saturates: soft)
    // the parasites through the double filter
    float par = idx_op * m * (1.0f - xor_amt) + 1.6f * sp * xor_amt * xo + idx_sp * s;
    float vh = (par - v.hp) * Gh, lh = vh + v.hp;
    v.hp = lh + vh;
    float x = par - lh;                 // (the high pass)
    float v1 = (x - v.k35[0]) * G, l1 = v1 + v.k35[0];
    v.k35[0] = l1 + v1;
    float u = a0 * (l1 + b2 * v.k35[1] + b3 * v.k35[2]);
    u = u > 1.5f ? 1.0f : u < -1.5f ? -1.0f : u - (4.0f / 27.0f) * u * u * u;   // (the loop's saturation)
    float v2 = (u - v.k35[1]) * G, y2 = v2 + v.k35[1];
    v.k35[1] = y2 + v2;
    float yk = K * y2;
    float v3 = (yk - v.k35[2]) * G, l3 = v3 + v.k35[2];
    v.k35[2] = l3 + v3;
    float pf = yk * iK;
    // the parasites' envelope (S.ENV): + falling from the attack, - growing
    float se = v.senv >= 0.0f ? 1.0f - v.senv + v.senv * v.se : 1.0f + v.senv * v.se;
    v.sez += (se - v.sez) * 0.08f;      // (0.3 ms: the attack sharp, not a step)
    se = v.sez;
    float pm = se * pf;
    v.ph = nph - fm_floorf(nph);
    float y = sin_cyc(v.ph + pm * 0.159f) + direct * se * pf;
    out[i] = 0.8f * v.amp * y;
  }
}

const char* arc_knob_name(int k) {
  static const char* const kN[8] = {"Spark", "Color", "Vary", "Decay", "Loop", "S.Env", "Xor", "P.Env"};
  return k >= 0 && k < 8 ? kN[k] : "";
}

void arc_defaults(float* k) {           // a bass with a little electricity in it, its sparks at the attack
  static const float kD[8] = {0.3f, 0.35f, 0.3f, 0.45f, 0.5f, 0.75f, 0.0f, 0.5f};
  for (int i = 0; i < 8; ++i) k[i] = kD[i];
}

}  // extern "C"
