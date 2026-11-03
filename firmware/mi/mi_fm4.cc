// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's FM (0.10.1): four operators, as the Yamaha YM2612 has them (the Mega Drive's sound chip, the MEGAfm's):
// its eight algorithms (ALGO), the first operator's feedback; with two of a modern synthesizer's mutators (ASM's
// Hydrasynth: PW, sync) on the operators that are heard. One machine: RATIO goes through thirty voicings of the four
// operators (their ratios, harmonic or not; the last eight chords, chords where three operators or more are heard).
//
// An operator is a sine whose phase the operators before it move (phase modulation, as the chip and the DX7 do).
// The first one modulates itself: FEEDBACK to the right, its sine leans into a saw (its two last outputs averaged, as
// the chip does it), then breaks into noise; to the left, a square (two of them half a cycle apart, one taken from the
// other: the saw's even harmonics cancelled), then noise. MUTATE on the operators heard: to the left PW (the cycle's
// first half squeezed: its wave leans, as Casio's phase distortion), to the right SYNC (their phase started again at
// each of the note's cycles, their pitch up to three octaves above: the edge's step taken off around it, polyBLEP).
//
// INDEX: the modulators' depth (with every operator heard: the three upper ones' levels). MOVE, the envelope's
// character: to the right a pluck (the FM opens at the trig and settles back, more and faster further right), to the
// left a swell (the note rises slower, the FM grows after it). DECAY (held at the top while the gate is); an operator
// high above the note dies sooner (a sound's high partials do: a tine, a bell's shimmer). DETUNE: the operators apart
// (a few cents, then a chorus), wandering.
//
// Rendered at twice the rate (the sidebands, the feedback's saw), brought down by ZERO's half-band (HIIR, six
// coefficients). The modulators' depth falls 3 dB an octave above C4 (and is kept under what would alias).
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f, kSr2 = 88200.0f;
const float kTwoPi = 6.2831853f;
const float kHb[6] = {0.0682040760f, 0.2402703580f, 0.4486762359f, 0.6411223671f, 0.7999975637f, 0.9344822355f};

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
inline float sin_cyc(float ph) {        // a sine of a phase in cycles, 0..1 (9th-order fit on the quarter wave)
  float x = ph < 0.5f ? ph : ph - 0.5f, s = ph < 0.5f ? 1.0f : -1.0f;
  x = x < 0.25f ? x : 0.5f - x;
  float z = x * 4.0f, z2 = z * z;
  return s * z * (1.5707963f + z2 * (-0.64596336f + z2 * (0.079688469f + z2 * (-0.0046722113f + z2 * 0.00015081274f))));
}
// the operators' sine: a table (512 points a cycle, linear between them: 2e-5 off at most, -94 dB), the phases as
// 32-bit words (a cycle's 2^32: they wrap by themselves), a modulation (cycles) added as one
float g_sin[513];                       // (filled at fm4_init: in RAM)
inline float sin_u(uint32_t ph) {
  uint32_t i = ph >> 23;
  float f = static_cast<float>(ph & 0x7FFFFFu) * (1.0f / 8388608.0f), a = g_sin[i];
  return a + (g_sin[i + 1] - a) * f;
}
inline uint32_t cyc_u(float c) {        // a modulation (cycles, -32..32) as a phase
  return static_cast<uint32_t>(static_cast<int32_t>(c * 67108864.0f)) << 6;
}
inline uint32_t step_u(float inc) {     // a step (cycles, >= 0) as a phase's
  inc -= static_cast<float>(static_cast<int>(inc));
  return static_cast<uint32_t>(inc * 4294967296.0f);
}
inline float u_cyc(uint32_t ph) { return static_cast<float>(ph >> 8) * (1.0f / 16777216.0f); }

// the YM2612's algorithms: which operator modulates which (the pairs 1>2, 1>3, 1>4, 2>3, 2>4, 3>4), which are heard
const float kRoute[8][6] = {
    {1, 0, 0, 1, 0, 1},                 // 0: 1 > 2 > 3 > 4
    {0, 1, 0, 1, 0, 1},                 // 1: (1 + 2) > 3 > 4
    {0, 0, 1, 1, 0, 1},                 // 2: (1 + (2 > 3)) > 4
    {1, 0, 0, 0, 1, 1},                 // 3: ((1 > 2) + 3) > 4
    {1, 0, 0, 0, 0, 1},                 // 4: (1 > 2) + (3 > 4)
    {1, 1, 1, 0, 0, 0},                 // 5: 1 > (2, 3, 4)
    {1, 0, 0, 0, 0, 0},                 // 6: (1 > 2) + 3 + 4
    {0, 0, 0, 0, 0, 0},                 // 7: 1 + 2 + 3 + 4
};
const uint8_t kOut[8][4] = {{0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 1},
                            {0, 1, 0, 1}, {0, 1, 1, 1}, {0, 1, 1, 1}, {1, 1, 1, 1}};
const int8_t kFirst[8][4] = {           // the operator each modulator modulates first (its sidebands' place)
    {1, 2, 3, -1}, {2, 2, 3, -1}, {3, 2, 3, -1}, {1, 3, 3, -1},
    {1, -1, 3, -1}, {1, -1, -1, -1}, {1, -1, -1, -1}, {-1, -1, -1, -1}};

// each operator's level by algorithm: a modulator's depth at INDEX full (cycles), one heard: its level (with every one
// heard, the three upper ones x the depth)
const float kLvl[8][4] = {{0.35f, 0.5f, 1.0f, 1.0f}, {0.5f, 0.5f, 1.0f, 1.0f}, {0.7f, 0.5f, 0.8f, 1.0f},
                          {0.5f, 0.7f, 0.8f, 1.0f}, {0.8f, 1.0f, 0.6f, 1.0f}, {0.6f, 1.0f, 1.0f, 1.0f},
                          {0.8f, 1.0f, 0.8f, 0.8f}, {1.0f, 1.0f, 1.0f, 1.0f}};
const float kDet[4] = {0.5f, -1.0f, 1.0f, -0.3f};   // each operator's detune (x DETUNE): the heard ones spread

// RATIO: the four operators' ratios to the note (the fourth always heard, always the note). Harmonic, then sub, high
// and inharmonic ones; then eight chords (equal tempered: in tune with the other tracks), the note and an octave on
// the first two, the third and the fifth or the seventh on the second and third
const float kM2 = 1.122462f, km3 = 1.189207f, kM3 = 1.259921f, kP4 = 1.334840f, kP5 = 1.498307f, km7 = 1.781797f,
            kM7 = 1.887749f;
const int kVoicings = 30, kChords = 22;
const float kVoicing[kVoicings][4] = {
    {1, 1, 1, 1}, {2, 1, 1, 1}, {1, 2, 1, 1}, {1, 1, 2, 1}, {2, 2, 1, 1}, {3, 1, 1, 1}, {1, 3, 1, 1}, {1, 1, 3, 1},
    {2, 3, 1, 1}, {4, 3, 2, 1}, {0.5f, 1, 1, 1}, {1, 1, 0.5f, 1}, {5, 1, 1, 1}, {1, 5, 2, 1}, {7, 3, 1, 1},
    {1, 1, 7, 1}, {1, 1, 14, 1}, {11, 1, 1, 1}, {1.41f, 1, 2, 1}, {3.5f, 1, 1, 1}, {2.756f, 1, 5.404f, 1},
    {7.13f, 1, 3.17f, 1},
    {2, kM3, kP5, 1}, {2, km3, kP5, 1}, {2, kP4, kP5, 1}, {2, kM2, kP5, 1}, {2, kM3, kM7, 1}, {2, km3, km7, 1},
    {2, kM3, km7, 1}, {2, kP4, km7, 1}};
const char* const kVoicingName[kVoicings] = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, "Tine", nullptr, "Bell", "Tube", "Bars", "Metal",
    "Major", "Minor", "Sus4", "Sus2", "Maj7", "Min7", "Dom7", "4ths"};
// the knobs as choosing the FM sets them: the second algorithm, unison, a bright pluck over a saw
const float kDef[8] = {1.5f / 8.0f, 0.5f / kVoicings, 0.5f, 0.55f, 0.2f, 0.675f, 0.775f, 0.5f};

inline int algo_of(float u) {
  int a = static_cast<int>(u * 8.0f);
  return a < 0 ? 0 : a > 7 ? 7 : a;
}
inline int voicing_of(float u) {
  int r = static_cast<int>(u * static_cast<float>(kVoicings));
  return r < 0 ? 0 : r > kVoicings - 1 ? kVoicings - 1 : r;
}

struct Voice {
  int algo, voi;
  float note, k[8];
  uint32_t ph[4], pm;                   // the operators' phases, the note's (the sync's master)
  float fa0, fa1, fb0, fb1;             // the first operator's feedback: its two last outputs (the square's two saws)
  float e[4], att[4], ainc[4], a0[4], dec[4];   // the envelopes: the level; the attack's progress, step, start; the decay
  float g[4];                           // each operator's gain as the last sub-block left it
  float ie;                             // MOVE's (1 at the trig, falling)
  float i_s, fb_s, mu_s;                // smoothed: the depth, the feedback, the mutator
  float corr;                           // the sync's step: its correction on the next sample
  float hb[2][4];
  float dcx, dcy;                       // the coupling capacitor
  float dr[4], dr_t[4];                 // the wander
  int dr_n;
  uint32_t rnd;
  int gate, trig, alive, held, let_go, snap;
};

Voice voice[MF_TRACKS];

inline uint32_t xs(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline float bip(uint32_t& s) { return static_cast<float>(xs(s) >> 8) * (2.0f / 16777216.0f) - 1.0f; }
inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }

inline float t60_s(float u) { return 0.05f * fm_exp2f(u * 8.644f); }        // DECAY: 50 ms .. 20 s (to -60 dB)
inline float detune_ct(float u) { return 30.0f * u * u; }                  // DETUNE: up to 30 cents apart
// FEEDBACK: its depth (the phase moved by the two last outputs' sum, cycles): the saw (and the square) at 0.75 (0.16:
// past it the loop starts to hunt, it aliases), then noise (x 4.5)
inline float fb_beta(float v) {
  v = fm_fabsf(v);
  if (v <= 0.75f) {
    float x = v * (1.0f / 0.75f);
    return 0.16f * x * x;
  }
  return 0.16f * fm_exp2f((v - 0.75f) * 4.0f * 2.17f);
}

// an operator heard, its wave under PW: its cycle's first half squeezed into kn (as a cosine's: no DC)
inline float pw_wave(uint32_t ph, float kn, float s1, float s2) {
  float q = u_cyc(ph + 0x40000000u);
  float w = q < kn ? q * s1 : 0.5f + (q - kn) * s2;   // (0..1)
  return sin_u(static_cast<uint32_t>(w * 4294967040.0f) - 0x40000000u);
}

// a sub-block's settings
const int kSub = 8;                     // the envelopes, the gains, the smoothing: every 8 samples (the gains ramped)
struct Core {
  uint32_t inc[4], incs[4], inc0;       // the phases' steps; under SYNC (the heard ones faster); the note's (the master)
  float finc0, fincs[4];                // (the same in cycles: the sync's step)
  float R[6], cw[3];                    // the algorithm: who modulates whom; the first three heard (the fourth is)
  int car[4];
  float g[4], dg[4];                    // the gains at the start, their step (an internal sample)
  float e1, de1;                        // the first operator's envelope (its feedback's)
  float bt, mk;                         // the feedback's depth; the square's level
  float kn, s1, s2;                     // PW: the knee, the two halves' slopes
};

// m samples at twice the rate: SQ the first operator a square (else its feedback a saw), MUT 1 PW, 2 SYNC
template <bool SQ, int MUT>
void core(Voice& v, const Core& c, float* y, int m) {
  uint32_t p1 = v.ph[0], p2 = v.ph[1], p3 = v.ph[2], p4 = v.ph[3], pm = v.pm;
  float fa0 = v.fa0, fa1 = v.fa1, fb0 = v.fb0, fb1 = v.fb1;
  float g1 = c.g[0], g2 = c.g[1], g3 = c.g[2], g4 = c.g[3], e1 = c.e1;
  float corr = v.corr;
  const float bt = c.bt, kn = c.kn, s1 = c.s1, s2 = c.s2;
  const int c1 = c.car[0], c2 = c.car[1], c3 = c.car[2];
  for (int s = 0; s < m; ++s) {
    // operator 1: its feedback (the square: two saws half a cycle apart)
    uint32_t q1 = p1 + cyc_u(bt * (fa0 + fa1));
    float ya = MUT == 1 && c1 ? pw_wave(q1, kn, s1, s2) : sin_u(q1);
    fa1 = fa0;
    fa0 = ya * e1;
    float y1 = ya;
    if (SQ) {
      uint32_t qb = p1 + 0x80000000u + cyc_u(bt * (fb0 + fb1));
      float yb = MUT == 1 && c1 ? pw_wave(qb, kn, s1, s2) : sin_u(qb);
      fb1 = fb0;
      fb0 = yb * e1;
      y1 = 0.5f * (ya - yb) * c.mk;
    }
    float z1 = y1 * g1;
    float m2 = c.R[0] * z1;
    uint32_t q2 = p2 + cyc_u(m2);
    float z2 = (MUT == 1 && c2 ? pw_wave(q2, kn, s1, s2) : sin_u(q2)) * g2;
    float m3 = c.R[1] * z1 + c.R[3] * z2;
    uint32_t q3 = p3 + cyc_u(m3);
    float z3 = (MUT == 1 && c3 ? pw_wave(q3, kn, s1, s2) : sin_u(q3)) * g3;
    float m4 = c.R[2] * z1 + c.R[4] * z2 + c.R[5] * z3;
    uint32_t q4 = p4 + cyc_u(m4);
    float z4 = (MUT == 1 ? pw_wave(q4, kn, s1, s2) : sin_u(q4)) * g4;
    float out = c.cw[0] * z1 + c.cw[1] * z2 + c.cw[2] * z3 + z4 + corr;
    corr = 0.0f;
    if (MUT == 2) {                     // SYNC: the heard ones faster; the note's cycle over, their phase started again
      uint32_t np[4] = {p1 + c.incs[0], p2 + c.incs[1], p3 + c.incs[2], p4 + c.incs[3]};
      pm += c.inc0;
      if (pm < c.inc0) {                // (wrapped) the step at the restart, d (a sample's part) before the next sample
        float d = clampf(static_cast<float>(pm) / static_cast<float>(c.inc0 ? c.inc0 : 1u), 0.0f, 1.0f);
        float mods[4] = {bt * (fa0 + fa1), m2, m3, m4};
        float gs[4] = {g1, g2, g3, g4};
        float hstep = 0.0f;
        for (int i = 0; i < 4; ++i) {
          if (!c.car[i]) continue;
          uint32_t back = step_u(d * c.fincs[i]), mo = cyc_u(mods[i]);
          uint32_t before = np[i] - back;
          float vb = sin_u(before + mo), va = sin_u(mo);
          if (SQ && i == 0) {
            uint32_t fbm = 0x80000000u + cyc_u(bt * (fb0 + fb1));
            vb = 0.5f * (vb - sin_u(before + fbm)) * c.mk;
            va = 0.5f * (va - sin_u(fbm)) * c.mk;
          }
          hstep += (va - vb) * gs[i];
          np[i] = back;
        }
        float hh = 0.5f * hstep;        // polyBLEP: this sample (1 - d before the step), the next (d after it)
        out += hh * d * d;
        corr = -hh * (1.0f - d) * (1.0f - d);
      }
      p1 = np[0], p2 = np[1], p3 = np[2], p4 = np[3];
    } else {
      p1 += c.inc[0];
      p2 += c.inc[1];
      p3 += c.inc[2];
      p4 += c.inc[3];
    }
    g1 += c.dg[0];
    g2 += c.dg[1];
    g3 += c.dg[2];
    g4 += c.dg[3];
    e1 += c.de1;
    y[s] = out;
  }
  if (MUT != 2) pm += c.inc0 * static_cast<uint32_t>(m);   // (the master on, as if it had run)
  v.ph[0] = p1, v.ph[1] = p2, v.ph[2] = p3, v.ph[3] = p4, v.pm = pm;
  v.fa0 = fa0, v.fa1 = fa1, v.fb0 = fb0, v.fb1 = fb1;
  v.corr = corr;
}

}  // namespace

extern "C" {

void fm4_init(void) {
  for (int i = 0; i < 512; ++i) g_sin[i] = sin_cyc(static_cast<float>(i) * (1.0f / 512.0f));
  g_sin[512] = 0.0f;
  for (int t = 0; t < MF_TRACKS; ++t) {
    Voice& v = voice[t];
    uint8_t* raw = reinterpret_cast<uint8_t*>(&v);
    for (uint32_t i = 0; i < sizeof(Voice); ++i) raw[i] = 0;
    v.rnd = 0x6C078965u * static_cast<uint32_t>(t + 1) | 1u;
    v.note = 48.0f;
    for (int i = 0; i < 4; ++i) v.att[i] = 1.0f;
    for (int i = 0; i < 8; ++i) v.k[i] = kDef[i];
  }
}

void fm4_silence(void) {
  for (int t = 0; t < MF_TRACKS; ++t) {
    Voice& v = voice[t];
    v.alive = 0;
    v.trig = 0;
    for (int i = 0; i < 4; ++i) v.e[i] = 0.0f, v.att[i] = 1.0f;
  }
}

void fm4_set(int t, float note, const float* k) {
  if (t < 0 || t >= MF_TRACKS) return;
  Voice& v = voice[t];
  v.note = note;
  for (int i = 0; i < 8; ++i) v.k[i] = clampf(k[i], 0.0f, 1.0f);
  v.algo = algo_of(v.k[0]);
  v.voi = voicing_of(v.k[1]);
}

void fm4_gate(int t, int g) {
  if (t < 0 || t >= MF_TRACKS) return;
  Voice& v = voice[t];
  if (g && !v.gate) v.trig = 1;
  if (!g && v.gate && v.held && v.alive) {   // a held note let go: each envelope falls from where it is
    v.let_go = 1;
    for (int i = 0; i < 4; ++i) v.dec[i] = v.e[i], v.att[i] = 1.0f;
  }
  v.gate = g;
}

void fm4_render(int t, float* out, uint32_t n) {
  if (t < 0 || t >= MF_TRACKS) return;
  Voice& v = voice[t];
  const int algo = v.algo;
  const uint8_t* C = kOut[algo];
  const float* lvl = kLvl[algo];
  const int additive = algo == 7;
  // the knobs
  const float I = v.k[2] * v.k[2], ud = v.k[4];
  const float move = 2.0f * v.k[5] - 1.0f, fbk = 2.0f * v.k[6] - 1.0f, mut = 2.0f * v.k[7] - 1.0f;
  // MOVE to the left: a swell, the attack up to 2.5 s; to the right a pluck, the attack from 2 ms down to 0.5
  const float A = move < 0.0f ? 0.002f + 2.5f * move * move : 0.002f - 0.0015f * move;
  float tau = t60_s(v.k[3]) * (1.0f / 6.9078f);
  v.held = v.k[3] > 0.98f;
  if (v.trig) {                         // a trig: the envelopes from where they are (from silence: the phases at 0)
    v.trig = 0;
    float lvl = 0.0f;
    for (int i = 0; i < 4; ++i) lvl = v.e[i] > lvl ? v.e[i] : lvl;
    int silent = !v.alive || lvl < 1e-3f;
    if (silent) {
      for (int i = 0; i < 4; ++i) v.ph[i] = 0u, v.e[i] = 0.0f, v.g[i] = 0.0f;
      v.pm = 0u;
      v.fa0 = v.fa1 = v.fb0 = v.fb1 = 0.0f;
      v.corr = 0.0f;
      for (int p = 0; p < 2; ++p)
        for (int i = 0; i < 4; ++i) v.hb[p][i] = 0.0f;
      v.dcx = v.dcy = 0.0f;
      v.snap = 1;
    }
    for (int i = 0; i < 4; ++i) {
      v.a0[i] = v.e[i];
      v.att[i] = 0.0f;
      v.dec[i] = 1.0f;
    }
    v.ie = 1.0f;
    v.let_go = 0;
    v.alive = 1;
  }
  if (!v.alive) {
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    return;
  }
  // the wander: a slow walk each operator (a new aim every 0.19 s)
  v.dr_n -= static_cast<int>(n);
  if (v.dr_n <= 0) {
    v.dr_n = 8192;
    for (int i = 0; i < 4; ++i) v.dr_t[i] = bip(v.rnd);
  }
  float wk = clampf(static_cast<float>(n) * 3e-5f, 0.0f, 1.0f);
  for (int i = 0; i < 4; ++i) v.dr[i] += (v.dr_t[i] - v.dr[i]) * wk;
  // the operators' frequencies
  Core c;
  const float f0 = note_hz(v.note);
  const float* rt = kVoicing[v.voi];
  float fr[4], inc[4];
  float spread = detune_ct(ud), hz = ud * ud * 1.2f;
  for (int i = 0; i < 4; ++i) {
    float ct = kDet[i] * spread + v.dr[i] * 2.5f * ud;
    fr[i] = f0 * rt[i] * fm_exp2f(ct * (1.0f / 1200.0f)) + kDet[i] * hz;
    fr[i] = clampf(fr[i], 0.0f, 30000.0f);
    inc[i] = fr[i] * (1.0f / kSr2);
    c.inc[i] = c.incs[i] = step_u(inc[i]);
    c.fincs[i] = inc[i];
  }
  for (int i = 0; i < 6; ++i) c.R[i] = kRoute[algo][i];
  // each operator's gain: a modulator's depth (cycles: the keyboard's scaling, under what would alias), a heard one's level
  float ks = v.note > 60.0f ? fm_exp2f((60.0f - v.note) * (1.0f / 24.0f)) : 1.0f;
  float B[4], cap[4];
  int modx[4];
  float n2 = 0.0f, xt = clampf(I + fm_fabsf(move) * 0.4f, 0.0f, 1.2f);
  const float s_max = mut > 0.0f ? fm_exp2f(3.0f * mut) : 1.0f;   // (SYNC: the heard ones' pitch up)
  float inc_hi = 0.0f;
  for (int i = 0; i < 4; ++i) {
    c.car[i] = C[i];
    modx[i] = !C[i] || (additive && i > 0);   // (its gain x the depth)
    cap[i] = 1e9f;
    if (C[i]) {
      float fs = fr[i] * s_max;         // one heard: faded out from 18 to 20 kHz
      B[i] = lvl[i] * clampf((20000.0f - fs) * (1.0f / 2000.0f), 0.0f, 1.0f);
      inc_hi = inc[i] > inc_hi ? inc[i] : inc_hi;
      float b = B[i] * (additive && i > 0 ? xt : 1.0f);
      n2 += b;
    } else {
      B[i] = lvl[i] * ks;
      int j = kFirst[algo][i];
      float fj = j >= 0 ? fr[j] : f0;
      float bmax = (60000.0f - fj) / (fr[i] > 1.0f ? fr[i] : 1.0f) - 1.0f;   // its sidebands under 60 kHz
      cap[i] = clampf(bmax, 0.0f, 100.0f) * (1.0f / kTwoPi);
    }
  }
  for (int i = 0; i < 3; ++i) c.cw[i] = C[i] ? 1.0f : 0.0f;
  // the heard ones' sum: their levels' sum to the 0.75 (their peaks seldom meet: louder than their sum's peak, quieter
  // than their power's)
  float norm = n2 > 1.0f ? 0.6f * fm_exp2f(-0.75f * fm_log2f(n2)) : 0.6f;   // (a sine's peak 0.6: the drums')
  for (int i = 0; i < 4; ++i)
    if (C[i]) B[i] *= norm;
  // the envelopes' steps (a sub-block's): an operator above the note dies sooner (1 / the square root of its ratio)
  float kd[4];
  float rel = fm_expf(-1.0f / ((0.03f + 0.5f * A) * kSr));
  for (int i = 0; i < 4; ++i) {
    float dk = clampf(fm_exp2f(-0.5f * fm_log2f(rt[i])), 0.35f, 1.4f);
    kd[i] = v.let_go ? rel : fm_expf(-1.0f / (tau * dk * kSr));
    for (int q = 0; q < 3; ++q) kd[i] *= kd[i];   // (^8)
    v.ainc[i] = static_cast<float>(kSub) / (clampf(A, 0.0005f, 6.0f) * kSr);
  }
  // MOVE's time: to the right a third of the note's (held: of a second), shorter the further right; to the left, after
  // the attack
  float tref = v.held ? 1.0f : tau, ti;
  if (move >= 0.0f) ti = clampf(0.33f * tref * (1.2f - 0.8f * move), 0.006f, 2.5f);
  else ti = 0.05f + 0.8f * A;
  const float ik = fm_expf(-static_cast<float>(kSub) / (ti * kSr));
  // the feedback (its sign: the square; above 1 kHz less: a saw as clean as an octave under), the mutator
  const float fhi = fr[0] > 1000.0f ? fm_exp2f(-0.25f * fm_log2f(fr[0] * 0.001f)) : 1.0f;
  const float beta = fb_beta(fbk) * fhi * (fbk < 0.0f ? -1.0f : 1.0f);
  const float mu = mut;
  const int mode = mu < -0.004f ? 1 : mu > 0.004f ? 2 : 0;   // PW, SYNC
  if (v.snap) {
    v.snap = 0;
    v.i_s = I;
    v.fb_s = beta;
    v.mu_s = mu;
  }
  c.finc0 = f0 * (1.0f / kSr2);
  c.inc0 = step_u(c.finc0);
  const int sustain = v.held && v.gate && !v.let_go;
  const float ksm = 0.15f;              // the smoothing (a sub-block's: ~1 ms)
  float buf[2 * kSub];
  float* h0 = v.hb[0];
  float* h1 = v.hb[1];
  for (uint32_t s0 = 0; s0 < n; s0 += kSub) {
    const int ns = n - s0 < static_cast<uint32_t>(kSub) ? static_cast<int>(n - s0) : kSub;
    // the envelopes at the sub-block's end
    const float e1_0 = v.e[0];
    for (int i = 0; i < 4; ++i) {
      float x;
      if (v.att[i] < 1.0f) {
        v.att[i] += v.ainc[i] * static_cast<float>(ns) * (1.0f / kSub);
        float a = v.att[i] < 1.0f ? v.att[i] : 1.0f;
        x = v.a0[i] + (1.0f - v.a0[i]) * a * a * (3.0f - 2.0f * a);
      } else if (sustain) {
        x = 1.0f;
      } else {
        float k = kd[i];
        if (ns != kSub) k = fm_exp2f(fm_log2f(k) * static_cast<float>(ns) * (1.0f / kSub));
        v.dec[i] *= k;
        x = v.dec[i];
      }
      v.e[i] = x;
    }
    v.ie *= ns == kSub ? ik : fm_exp2f(fm_log2f(ik) * static_cast<float>(ns) * (1.0f / kSub));
    v.i_s += (I - v.i_s) * ksm;
    v.fb_s += (beta - v.fb_s) * ksm;
    v.mu_s += (mu - v.mu_s) * ksm;
    float X = v.i_s + (move >= 0.0f ? move * 0.8f * v.ie : -move * 0.8f * (1.0f - v.ie));
    X = clampf(X, 0.0f, 1.2f);
    const float r2 = 1.0f / static_cast<float>(2 * ns);
    for (int i = 0; i < 4; ++i) {
      float gt = B[i] * v.e[i] * (modx[i] ? X : 1.0f);
      if (!C[i]) gt = gt < cap[i] ? gt : cap[i];
      c.g[i] = v.g[i];
      c.dg[i] = (gt - v.g[i]) * r2;
      v.g[i] = gt;
    }
    c.e1 = e1_0;
    c.de1 = (v.e[0] - e1_0) * r2;
    // the feedback, the mutator
    const int sq = v.fb_s < 0.0f;
    c.bt = sq ? -v.fb_s : v.fb_s;
    c.mk = sq ? (c.bt <= 0.15f ? 1.0f + 18.8f * c.bt * c.bt : clampf(1.423f - (c.bt - 0.15f) * 4.0f, 1.1f, 1.5f))
              : 1.0f;                  // (the square's level: its two saws partly cancel)
    c.kn = 0.5f, c.s1 = 1.0f, c.s2 = 1.0f;
    if (mode == 1) {
      float a = -v.mu_s;
      a = a > 0.0f ? a : 0.0f;
      float kn = 0.5f - 0.47f * a, lo = 3.0f * inc_hi;   // (at least three samples to rise, the highest heard)
      kn = kn > lo ? kn : lo;
      kn = kn < 0.5f ? kn : 0.5f;
      c.kn = kn;
      c.s1 = 0.5f / kn;
      c.s2 = 0.5f / (1.0f - kn);
    } else if (mode == 2) {
      float a = v.mu_s > 0.0f ? v.mu_s : 0.0f, S = fm_exp2f(3.0f * a);
      for (int i = 0; i < 4; ++i) {
        c.fincs[i] = inc[i] * (C[i] ? S : 1.0f);
        c.incs[i] = step_u(c.fincs[i]);
      }
    }
    const int m2 = 2 * ns;
    switch (mode * 2 + sq) {
      case 0: core<false, 0>(v, c, buf, m2); break;
      case 1: core<true, 0>(v, c, buf, m2); break;
      case 2: core<false, 1>(v, c, buf, m2); break;
      case 3: core<true, 1>(v, c, buf, m2); break;
      case 4: core<false, 2>(v, c, buf, m2); break;
      default: core<true, 2>(v, c, buf, m2); break;
    }
    // down to the output's rate: the half-band; the coupling capacitor; a soft knee above 0.8 (at most 1.2)
    for (int s = 0; s < ns; ++s) {
      float x0 = buf[2 * s + 1], x1 = buf[2 * s];
      for (int q = 0; q < 3; ++q) {
        float c0 = kHb[2 * q], c1 = kHb[2 * q + 1];
        float o0 = c0 * (x0 - h0[q + 1]) + h0[q];
        h0[q] = x0;
        x0 = o0;
        float o1 = c1 * (x1 - h1[q + 1]) + h1[q];
        h1[q] = x1;
        x1 = o1;
      }
      h0[3] = x0;
      h1[3] = x1;
      float y = 0.5f * (x0 + x1);
      float yd = y - v.dcx + 0.9986f * v.dcy;   // (10 Hz: 1:1 FM puts a sideband at 0 Hz)
      v.dcx = y;
      v.dcy = yd;
      float ay = fm_fabsf(yd);
      if (ay > 0.8f) {
        float z = (ay - 0.8f) * 2.5f;
        ay = 0.8f + 0.4f * z / (1.0f + z);
        yd = yd < 0.0f ? -ay : ay;
      }
      out[s0 + s] = yd;
    }
  }
  // over: every heard operator under -100 dB, out of its attack
  float hi = 0.0f;
  int rising = 0;
  for (int i = 0; i < 4; ++i) {
    if (!C[i]) continue;
    hi = v.e[i] > hi ? v.e[i] : hi;
    rising |= v.att[i] < 1.0f;
  }
  if (!rising && !sustain && hi < 1e-5f) {
    v.alive = 0;
    for (int i = 0; i < 4; ++i) v.e[i] = 0.0f, v.g[i] = 0.0f;
  }
}

int fm4_algo(float u) { return algo_of(u); }

const char* fm4_knob_name(int k) {
  static const char* const kN[8] = {"Algo", "Ratio", "Index", "Decay", "Detune", "Move", "Feedback", "Mutate"};
  return k >= 0 && k < 8 ? kN[k] : "";
}

void fm4_defaults(float* k) {
  for (int i = 0; i < 8; ++i) k[i] = kDef[i];
}

int fm4_knob_steps(int k) { return k == 0 ? 8 : k == 1 ? kVoicings : 128; }

int fm4_chord(float u) { return voicing_of(u) >= kChords; }
float fm4_chords_at(void) { return static_cast<float>(kChords) / static_cast<float>(kVoicings); }

static void put_s(char* b, int n, int* at, const char* s) {
  while (*s && *at < n - 1) b[(*at)++] = *s++;
  b[*at] = 0;
}
static void put_i(char* b, int n, int* at, int x) {
  char t[8];
  int i = 0;
  if (x < 0) put_s(b, n, at, "-"), x = -x;
  do {
    t[i++] = static_cast<char>('0' + x % 10);
    x /= 10;
  } while (x && i < 7);
  while (i && *at < n - 1) b[(*at)++] = t[--i];
  b[*at] = 0;
}
static void put_time(char* b, int n, int* at, float s) {   // 12ms, 340ms, 1.2s
  if (s < 0.9995f) {
    put_i(b, n, at, static_cast<int>(s * 1000.0f + 0.5f));
    put_s(b, n, at, "ms");
  } else {
    int x = static_cast<int>(s * 10.0f + 0.5f);
    put_i(b, n, at, x / 10);
    put_s(b, n, at, ".");
    put_i(b, n, at, x % 10);
    put_s(b, n, at, "s");
  }
}
static void put_ratio(char* b, int n, int* at, float r) {   // 1, .5, 3.5, 2.76
  int x = static_cast<int>(r * 100.0f + 0.5f);
  if (x >= 100) put_i(b, n, at, x / 100);
  if (x % 100) {
    put_s(b, n, at, ".");
    put_i(b, n, at, x / 10 % 10);
    if (x % 10) put_i(b, n, at, x % 10);
  }
}

void fm4_knob_text(int k, float u, char* b, int n) {   // u: 0..1 (a bipolar knob from its middle)
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  switch (k) {
    case 0:                             // ALGO: the chip's, 1 to 8
      return put_i(b, n, &at, algo_of(u) + 1);
    case 1: {                           // RATIO: the voicing (its name; else the four ratios)
      int r = voicing_of(u);
      if (kVoicingName[r]) return put_s(b, n, &at, kVoicingName[r]);
      for (int i = 0; i < 4; ++i) {
        if (i) put_s(b, n, &at, ":");
        put_ratio(b, n, &at, kVoicing[r][i]);
      }
      return;
    }
    case 4: {                           // DETUNE: cents apart
      float c = detune_ct(u);
      if (c < 0.05f) return put_s(b, n, &at, "Off");
      int x = static_cast<int>(c * 10.0f + 0.5f);
      if (x < 100) {
        put_i(b, n, &at, x / 10);
        put_s(b, n, &at, ".");
        put_i(b, n, &at, x % 10);
      } else {
        put_i(b, n, &at, (x + 5) / 10);
      }
      put_s(b, n, &at, "ct");
      return;
    }
    case 3:
      if (u > 0.98f) return put_s(b, n, &at, "Hold");
      return put_time(b, n, &at, t60_s(u));
    case 5: case 6: case 7: {           // bipolar: MOVE (- swell, + pluck); FEEDBACK (- square, + saw); MUTATE (- PW,
                                        // + SYNC)
      float w = 2.0f * u - 1.0f;
      int x = static_cast<int>(w * 100.0f + (w < 0.0f ? -0.5f : 0.5f));
      if (k == 6) {
        if (x >= 97 || x <= -97) return put_s(b, n, &at, "Noise");
        if (x >= 73 && x <= 77) return put_s(b, n, &at, "Saw");
        if (x <= -73 && x >= -77) return put_s(b, n, &at, "Square");
        if (x == 0) return put_s(b, n, &at, "Sine");
      }
      if (k == 7 || k == 5) {
        if (x == 0) return put_s(b, n, &at, k == 7 ? "Off" : "0%");
        put_i(b, n, &at, x < 0 ? -x : x);
        put_s(b, n, &at, k == 7 ? (x < 0 ? "% PW" : "% SYNC") : (x < 0 ? "% SWELL" : "% PLUCK"));
        return;
      }
      if (x > 0) put_s(b, n, &at, "+");
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
