// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1: a filter on each track's voice (after its grit), four models of analog filters, written for the FM-1 (float,
// no libm, a sample at a time):
//   LADDER  Moog's transistor ladder, after Stefano D'Angelo and Vesa Välimäki's improved virtual analog model ("An
//           Improved Virtual Analog Model of the Moog Ladder Filter", ICASSP 2013; its reference code, MoogLadders'
//           ImprovedModel.h, Copyright 2012 Stefano D'Angelo, ISC licence: notice at the end): a transistor pair's
//           tanh at each of the four stages, the thermal voltage 0.312, trapezoidal steps. Low pass 24 dB; band and
//           high pass from its stages (Oberheim Xpander's mixes). DRIVE: the input into the transistors (its level kept).
//   MS-20   Korg 35 (the MS-10's, the first MS-20's), after Will Pirkle's model (app note AN-5, "Designing Software
//           Synthesizer Plug-Ins in C++") on Vadim Zavalishin's zero-delay feedback one-poles ("The Art of VA Filter
//           Design"), its saturation inside the resonance loop as Pirkle's NLP option has it: the resonance drives.
//           Low pass 12 dB, high pass 6 as the circuit is; band pass, a one-pole high pass into its low pass (the
//           MS-20's two filters in series). DRIVE: the loop's saturation (its level given back).
//   SEM     Oberheim SEM's 12 dB state variable filter (Pirkle's model on Zavalishin's: low, band, high pass). DRIVE:
//           its input stage pushed (drive_in: a transistor stage a little off centre, anti-aliased) and its two
//           integrators' OTAs with it. Each OTA saturates on what it gets (the high pass, the band), so the drive lives
//           with the resonance and the cutoff: the peak thickens and squashes. Each curve is taken at the previous
//           sample's input (its gain, a sample behind), the filter solved without delay around it.
//   VOX     three formants (the vowels' first three resonances, as a voice makes them: Peterson and Barney's measured
//           averages, low, mid and high voices), CUTOFF going A, E, I, O, U, its resonance their sharpness. DRIVE:
//           its input stage and the three resonators' integrators pushed as SEM's (the formants growl, as a throat).
// Each: NOISE, white noise into it at its envelope's level (it breathes at the trigs; with resonance, whistles), its
// COLOR a tilt; ATTACK its envelope's rise.
// Each has its envelope, a vactrol's (Plaits' low-pass gate envelope, ported: a rise, ATTACK's, a fall slowing as it
// goes down, DECAY its length), its PING (an impulse into the filter at each trig: its resonance rings), its FM (its
// own output into its cutoff, at audio rate, either way; its knob on an exponential curve, 0.14) and its KEY (the
// note into its cutoff).
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f;
const float kInvSr = 1.0f / 44100.0f;
const float kPi = 3.14159265f;


struct Filter {
  int model, type;
  float oct0, oct1;                      // the cutoff from its knob and the note (octaves over 20 Hz): the last
                                         // block's, this one's (glided over the block)
  float reso, drive, env, ping, fm;
  float noise_amt, color, attack, nlp;   // the noise (its envelope's), its colour (a one-pole tilt), the attack's step
  float short_decay, decay_tail;         // the vactrol's
  float vs;                              // its state, 0..1
  int ramp;                              // rising
  uint32_t fade;                         // STOP: the samples left of its fade out (then it is still, until a trig)
  int still;
  uint32_t trig[8];                      // this block's trigs (their samples)
  int ntrig;
  float y1;                              // the last output (FM)
  uint32_t rng;
  float now_oct;                         // (the screen: the cutoff as it is)
  // the coefficients, made every 8 samples (each sample with FM): a model's own
  float g, G, a0, b2, b3, K, iK;         // (LADDER: g; MS-20: g, G, a0, b2, b3, K, 1 / K; SEM: g, a0, rho in b2)
  float kc;                              // (LADDER: its feedback's scale at that cutoff, ladder_g's)
  float vg[3], va0[3], vrho[3];          // VOX's three formants
  // LADDER
  float V[4], dV[4], tV[4];
  // MS-20: the one-poles' states (LPF1, LPF2, HPF1 or HPF2), the pre high pass (band)
  float k35[3], pre;
  // SEM / VOX: the state variable filters' states (two each, three for VOX), their integrators' last inputs (DRIVE)
  float sv[6], nl[6];
  // SEM / VOX: DRIVE's input stage (its curve's last input, the coupling capacitor's states)
  float du1, dcx, dcy;
  int dstale;
  // (0.16.65) TYPE changed, the model the same (the random dose throws it at a trig): the type it was (-1: none), the
  // way from it to the new one (12 ms: this block's start and end); the MS-20's loop as it was, on its own states.
  // Before, a new type reset the states: the sound cut at once, a click each time
  int from_type;
  float xa, xa1, xk[3], xpre;
  // (0.16.65) DRIVE, RESO, ENV and FM as set (the knobs, the dose's throw at a trig, TRAUMA's): reached over ~8 ms, a
  // step each kStep samples (reso, drive, env, fm: where they are). At once, the levels they set (DRIVE's given back,
  // the resonance's) and the cutoff (ENV, its envelope up) jumped at the trig: a click. pfresh: at once, the next time
  // (the model new, the track at rest)
  float drive_t, reso_t, fm_t, env_t;
  int pfresh;
  float kd, ka, kf;                     // (0.16.65) DECAY, ATTACK, FM as their curves were last made (slow_coefs)
};
const float kXfRate = 1.0f / (0.012f * kSr);
Filter flt[MF_TRACKS];

// Peterson and Barney (1952): the formants F1, F2, F3 (Hz) of the vowels a (father), e (bed), i (beet), o (boat),
// u (boot), men's, women's and children's averages
const float kVox[3][5][3] = {
    {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}},
    {{850, 1220, 2810}, {610, 2330, 2990}, {310, 2790, 3310}, {590, 920, 2710}, {370, 950, 2670}},
    {{1030, 1370, 3170}, {690, 2610, 3570}, {370, 3200, 3730}, {680, 1060, 3180}, {430, 1170, 3260}},
};
const float kVoxGain[3] = {1.0f, 0.7f, 0.45f};   // (the formants' levels: the first the loudest)

inline float noise(Filter& f) {
  f.rng ^= f.rng << 13;
  f.rng ^= f.rng >> 17;
  f.rng ^= f.rng << 5;
  return static_cast<float>(static_cast<int32_t>(f.rng)) * (1.0f / 2147483648.0f);
}

inline float cnoise(Filter& f) {         // white noise, tilted: COLOR to the left darker (a one-pole low pass at ~1
  float n = noise(f);                   // kHz), to the right brighter (what it leaves)
  f.nlp += 0.133f * (n - f.nlp);
  float c = f.color;
  return c < 0.0f ? n + c * (n - f.nlp) : n + c * (1.5f * (n - f.nlp) - n);
}

inline float hz_of(float oct) { return 20.0f * fm_exp2f(oct); }
// FM's knob (0.14): its depth on an exponential curve, (e^4|k| - 1) / (e^4 - 1), its sign kept: gentle up to the
// middle (12 % of the depth there, 3 % at a quarter), then rising to all of it (Yoann's call: the plain knob was too
// much too soon)
inline float fm_depth(float k) {
  float a = fm_fabsf(k), d = (fm_expf(4.0f * a) - 1.0f) * (1.0f / 53.59815f);
  return k < 0.0f ? -d : d;
}
inline float tan_pade(float x) {         // tan on 0 .. 1.32 (a cutoff up to 0.42 sr), a (3, 2) Padé: 1.4 % at the top
  float x2 = x * x;
  return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}
inline float soft(float y) {             // the output kept within +/-2: as it is under 1, then bending
  float a = fm_fabsf(y);
  if (a <= 1.0f) return y;
  float b = 2.0f - 1.0f / a;
  return y < 0.0f ? -b : b;
}

void reset_state(Filter& f) {
  for (int i = 0; i < 4; ++i) f.V[i] = f.dV[i] = f.tV[i] = 0.0f;
  for (int i = 0; i < 3; ++i) f.k35[i] = 0.0f;
  for (int i = 0; i < 6; ++i) f.sv[i] = f.nl[i] = 0.0f;
  f.du1 = f.dcx = f.dcy = 0.0f;
  f.dstale = 1;
  f.pre = f.y1 = f.nlp = 0.0f;
  for (int i = 0; i < 3; ++i) f.xk[i] = 0.0f;
  f.xpre = 0.0f;
  f.from_type = -1;                     // (from silence: the new type at once)
}

const float kInvVt2 = 1.0f / (2.0f * 0.312f);   // (the ladder: its transistors' thermal voltage, 0.312, twice)

// the coefficients for a cutoff of c octaves over 20 Hz (the model's range kept)
void coefs(Filter& f, float c) {
  f.now_oct = c;
  float hz = hz_of(c);
  switch (f.model) {
    case MF_LADDER:
      f.g = ladder_g(hz, &f.kc);
      break;
    case MF_MS20: {                      // Pirkle's KORG35 (its alpha0 and betas)
      if (hz > 0.42f * kSr) hz = 0.42f * kSr;
      float g = tan_pade(kPi * hz * kInvSr), G = g / (1.0f + g), K = f.K, ig = 1.0f / (1.0f + g);
      f.g = g;
      f.G = G;
      f.a0 = 1.0f / (1.0f - K * G + K * G * G);
      if (f.type != MF_HP) {
        f.b2 = (K - K * G) * ig;
        f.b3 = -ig;
      } else {
        f.b2 = -G * ig;
        f.b3 = ig;
      }
      break;
    }
    case MF_SEM: {
      if (hz > 0.42f * kSr) hz = 0.42f * kSr;
      float g = tan_pade(kPi * hz * kInvSr), r2 = 2.0f * f.a0;   // (a0 holds r: 1 / 2Q, made in mf_set)
      f.g = g;
      f.b2 = 1.0f / (1.0f + r2 * g + g * g);
      f.b3 = r2 + g;
      break;
    }
    default: {                           // VOX: CUTOFF's travel over the five vowels (each a fifth of it)
      float pos = fm_clampf(c * (4.0f / 9.8f), 0.0f, 4.0f), r2 = 2.0f * f.a0;
      int v = static_cast<int>(pos);
      if (v > 3) v = 3;
      float fr = pos - static_cast<float>(v);
      const float(*t)[3] = kVox[f.type];
      for (int k = 0; k < 3; ++k) {
        float g = tan_pade(kPi * (t[v][k] + (t[v + 1][k] - t[v][k]) * fr) * kInvSr);
        f.vg[k] = g;
        f.va0[k] = 1.0f / (1.0f + r2 * g + g * g);
        f.vrho[k] = r2 + g;
      }
      break;
    }
  }
}

// a zero-delay feedback state variable filter (Zavalishin's; Pirkle's SEM), a sample: its band pass, its low pass in *lp,
// high pass in *hp
inline float svf(float* s, float x, float g, float a0, float rho, float* lp, float* hp) {
  float h = a0 * (x - rho * s[0] - s[1]);
  float b = g * h + s[0];
  float l = g * b + s[1];
  s[0] = g * h + b;
  s[1] = g * b + l;
  *lp = l;
  *hp = h;
  return b;
}

// the state variable filter with its integrators saturating (DRIVE: k, their OTAs' input gain): each integrator's
// rate is g times its curve's gain at what it got the sample before (sat(kv) / kv: 1 while small), then the filter
// is solved without delay as the linear one is, with the two rates (p: the two last inputs, the high pass and the
// band). rho is the linear filter's (twice the damping, plus g)
inline float ota_gain(float v) {
  float v2 = v * v;
  return v2 <= 2.25f ? 1.0f - (4.0f / 27.0f) * v2 : fm_rsqrtf(v2);   // (sat's: the cubic, then flat)
}
inline float svf_nl(float* s, float* p, float x, float g, float rho, float k, float* lp, float* hp) {
  float g1 = g * ota_gain(k * p[0]), g2 = g * ota_gain(k * p[1]), r = rho - g + g2;
  float h = (x - r * s[0] - s[1]) / (1.0f + g1 * r);
  float b = g1 * h + s[0];
  float l = g2 * b + s[1];
  s[0] = g1 * h + b;
  s[1] = g2 * b + l;
  p[0] = h;
  p[1] = b;
  *lp = l;
  *hp = h;
  return b;
}

// a transistor pair's curve without a division (the FM-1 reads code through a cache both cores share, and divides
// slowly): a cubic, tanh's slope at 0, flat at 1.5 and beyond (where tanh is at 0.9)
inline float sat(float x) {
  if (x > 1.5f) return 1.0f;
  if (x < -1.5f) return -1.0f;
  return x - (4.0f / 27.0f) * x * x * x;
}

// DRIVE's input stage (SEM, VOX): the signal pushed into a transistor stage biased a little off its centre (even
// harmonics with the odd: the warmth of a real one), its curve taken through its antiderivative (first-order
// antiderivative anti-aliasing: the harmonics it makes fold back far less), a coupling capacitor after it (the bias's
// DC out, under 5 Hz), its level kept about the same as it saturates
inline float sat_F(float x) {            // sat's antiderivative
  float a = fm_fabsf(x);
  if (a <= 1.5f) {
    float x2 = x * x;
    return x2 * (0.5f - x2 * (1.0f / 27.0f));
  }
  return a - 0.5625f;
}
// the curve's ADAA from u1 to u: its antiderivative's divided difference written out where both are on one piece of
// it (0.14.2): on the cubic, (u + u1)(1/2 - (u^2 + u1^2) / 27); both flat on one side, +/-1. No division there (the
// FM-1 divides slowly) and no cancellation; a division only where a step crosses the knee
inline float sat_adaa(float u, float u1) {
  float a = fm_fabsf(u), a1 = fm_fabsf(u1);
  if (a <= 1.5f && a1 <= 1.5f) return (u + u1) * (0.5f - (u * u + u1 * u1) * (1.0f / 27.0f));
  if (a >= 1.5f && a1 >= 1.5f && (u > 0.0f) == (u1 > 0.0f)) return u > 0.0f ? 1.0f : -1.0f;
  float du = u - u1;
  return fm_fabsf(du) > 1e-3f ? (sat_F(u) - sat_F(u1)) / du : sat(0.5f * (u + u1));
}
inline float drive_in(Filter& f, float x, float dg, float bias, float sb, float lvl) {
  float u = dg * x + bias, y;
  if (f.dstale) {
    y = sat(u);
    f.dstale = 0;
  } else {
    y = sat_adaa(u, f.du1);
  }
  f.du1 = u;
  y -= sb;                               // (the bias's own output)
  float z = y - f.dcx + 0.99929f * f.dcy;
  f.dcx = y;
  f.dcy = z;
  return z * lvl;
}

// the vactrol (Plaits' LPGEnvelope), a step each kStep samples; a trig in this sub-block starts its rise
const uint32_t kStep = 16;
inline float vactrol(Filter& f, float vs, int* ramp) {
  if (*ramp) {
    vs += f.attack;
    if (vs >= 1.0f) vs = 1.0f, *ramp = 0;
  } else {
    float s2 = vs * vs, coef = f.short_decay + (1.0f - s2 * s2) * f.decay_tail;
    vs -= static_cast<float>(kStep) * coef * vs;
    if (vs < 1e-6f) vs = 0.0f;
  }
  return vs;
}

// each model's block: sub-blocks of kStep samples, the vactrol and the coefficients at each (each sample with FM), the
// trigs (a ping: an impulse into the filter), then the model alone, its type a mix of its outputs (no branch a sample)
struct Run {
  float oct, doct, vs, y1, fm5, ping, nv;   // nv: the noise now (its amount by the envelope)
  int ramp, ti, fm_on, skip;            // (skip: the FM's coefficients at the samples i & skip == 0)
  int pg;                               // (0.16.65) DRIVE or RESO moved: what they set made again
};
// the CPU guard (engine.cc): the FM's coefficients every sample (0), every 2nd (1), every 4th (3); -1: no FM
volatile int fm_skip = 0;

// what RESO sets, a model's (0.16.65: each step of its glide)
void reso_coefs(Filter& f) {
  if (f.model == MF_MS20) f.K = 0.01f + 2.2f * f.reso, f.iK = 1.0f / f.K;   // (over 2: it sings, its loop's
                                                                             // saturation holding it)
  else if (f.model == MF_SEM) f.a0 = 0.5f / (0.5f + 12.0f * f.reso * f.reso * f.reso);   // r = 1 / 2Q, Q 0.5 .. 12.5
  else if (f.model == MF_VOX) f.a0 = 0.5f / (2.0f + 18.0f * f.reso);                    // Q 2 .. 20
}
// (0.16.65) DRIVE, RESO, ENV, FM a step closer to where they are set (~8 ms), there once within 1e-4
__attribute__((noinline)) void param_glide(Filter& f, Run& r) {
  const float kGl = 0.0443f;            // (16 samples of 8 ms)
  f.drive += (f.drive_t - f.drive) * kGl;
  f.reso += (f.reso_t - f.reso) * kGl;
  f.fm += (f.fm_t - f.fm) * kGl;
  f.env += (f.env_t - f.env) * kGl;
  if (fm_fabsf(f.env_t - f.env) < 1e-4f) f.env = f.env_t;
  if (fm_fabsf(f.drive_t - f.drive) < 1e-4f) f.drive = f.drive_t;
  if (fm_fabsf(f.reso_t - f.reso) < 1e-4f) f.reso = f.reso_t;
  if (fm_fabsf(f.fm_t - f.fm) < 1e-5f) f.fm = f.fm_t;
  reso_coefs(f);
  r.fm5 = f.fm * 5.0f;
  r.pg = 1;
}

__attribute__((noinline)) void sub_block(Filter& f, Run& r, uint32_t i0, uint32_t i1, uint32_t* ping_at) {
  if (f.drive != f.drive_t || f.reso != f.reso_t || f.fm != f.fm_t || f.env != f.env_t) param_glide(f, r);
  *ping_at = 0xFFFFFFFFu;
  while (r.ti < f.ntrig && f.trig[r.ti] < i1) {
    *ping_at = f.trig[r.ti] < i0 ? i0 : f.trig[r.ti];
    ++r.ti;
    r.ramp = 1;
  }
  r.vs = vactrol(f, r.vs, &r.ramp);
  r.nv = f.noise_amt * r.vs;
  r.oct += r.doct * static_cast<float>(i1 - i0);
  float c = fm_clampf(r.oct + f.env * 8.0f * r.vs, -0.5f, 10.2f);
  f.now_oct = c;
  if (!r.fm_on) coefs(f, c);
}
__attribute__((noinline)) void fm_coefs(Filter& f, const Run& r) {
  coefs(f, fm_clampf(f.now_oct + r.fm5 * r.y1, -0.5f, 10.2f));
}
inline float out(Filter& f, Run& r, float y) {   // its output kept within 2, a NaN reset
  if (!(y == y)) {
    reset_state(f);
    y = 0.0f;
  }
  y = soft(y);
  r.y1 = y;
  return y;
}

// the ladder's outputs mixed for a TYPE: the input, then its four stages (LP: the last, its passband given back a
// third; HP, BP: the Xpander's mixes)
void ladder_mix(int type, float res, float* w) {
  w[0] = w[1] = w[2] = w[3] = 0.0f, w[4] = 1.0f + 0.35f * res;
  if (type == MF_HP) w[0] = 1.0f, w[1] = -4.0f, w[2] = 6.0f, w[3] = -4.0f, w[4] = 1.0f;
  else if (type == MF_BP) w[2] = 4.0f, w[3] = -8.0f, w[4] = 4.0f;
}

// (0.16.65) XF: a TYPE on its way from the old one (process_xf, in the flash: the RAM's code room full); without it,
// the block as before, in the RAM
// what DRIVE and RESO set, a model's: made at its block's start (in the RAM, as before) and again, while they glide
// (0.16.65), by a copy in the flash (the RAM's code room full; a few blocks after a turn or a throw)
struct LadK {
  float r4, drive, lc, w[5], ow[5];
};
inline LadK lad_k(const Filter& f, float kc0, bool xf) {
  LadK k;
  k.r4 = 4.2f * f.reso, k.drive = 1.0f + 15.0f * f.drive * f.drive;
  k.lc = 1.0f / (1.0f + 1.4f * f.drive + 1.6f * f.drive * f.drive);   // (louder a little, then squashed)
  const float res = k.r4 * kc0;
  ladder_mix(f.type, res, k.w);
  if (xf) ladder_mix(f.from_type, res, k.ow);
  return k;
}
__attribute__((noinline)) LadK lad_k_glide(const Filter& f, float kc0, bool xf) { return lad_k(f, kc0, xf); }
struct Ms20K {
  float st, ist, mk;
};
inline Ms20K ms20_k(const Filter& f) {
  Ms20K k;
  k.st = 1.0f + 7.0f * f.drive, k.ist = 1.0f / k.st, k.mk = 1.0f + 0.6f * (k.st - 1.0f);   // (mk: its level back)
  return k;
}
__attribute__((noinline)) Ms20K ms20_k_glide(const Filter& f) { return ms20_k(f); }
struct SvfK {                           // (SEM, VOX: DRIVE's input stage and OTAs; SEM's band, VOX's gain)
  float k, dg, bias, sb, lvl, q;
};
inline SvfK svf_k(const Filter& f, bool vox) {
  SvfK k;
  k.k = (vox ? 1.5f : 6.0f) * f.drive;
  k.dg = 1.0f + 7.0f * f.drive, k.bias = 0.3f * f.drive, k.sb = sat(k.bias);
  k.lvl = 1.0f / (1.0f + (vox ? 0.04f : 0.08f) * (k.dg - 1.0f));
  k.q = vox ? 2.0f * f.a0 * (2.4f + 0.14f / (2.0f * f.a0)) : 2.0f * f.a0 * (1.0f + 2.0f * f.reso);
  return k;
}
__attribute__((noinline)) SvfK svf_k_glide(const Filter& f, bool vox) { return svf_k(f, vox); }

template <bool XF>
void run_ladder(Filter& f, Run& r, float* x, uint32_t n) {
  const float kc0 = f.kc;               // (the mix's resonance: at the block's start, as before; not the FM's now)
  LadK K = lad_k(f, kc0, XF);
  // (0.16.65) a new TYPE: its mix from the old one's over the block (the states the same for all three)
  const bool xf = XF;
  const float dx = xf ? (f.xa1 - f.xa) / static_cast<float>(n) : 0.0f;
  float a = xf ? f.xa : 1.0f;
  float* V = f.V;
  float* dV = f.dV;
  float* tV = f.tV;
  for (uint32_t i0 = 0; i0 < n; i0 += kStep) {
    uint32_t i1 = i0 + kStep > n ? n : i0 + kStep, ping_at;
    sub_block(f, r, i0, i1, &ping_at);
    if (r.pg) r.pg = 0, K = lad_k_glide(f, kc0, XF);
    const float r4 = K.r4, drive = K.drive, lc = K.lc;
    const float* w = K.w;
    const float* ow = K.ow;
    for (uint32_t i = i0; i < i1; ++i) {
      if (r.fm_on && !(i & r.skip)) fm_coefs(f, r);
      float in = x[i] + (i == ping_at ? r.ping : 0.0f);
      if (r.nv != 0.0f) in += r.nv * cnoise(f);
      float g = f.g, u = drive * in - r4 * f.kc * V[3];   // (f.kc: FM's too)
      float d0 = g * (sat(u * kInvVt2) - tV[0]);
      V[0] += d0 + dV[0];
      dV[0] = d0;
      tV[0] = sat(V[0] * kInvVt2);
      float d1 = g * (tV[0] - tV[1]);
      V[1] += d1 + dV[1];
      dV[1] = d1;
      tV[1] = sat(V[1] * kInvVt2);
      float d2 = g * (tV[1] - tV[2]);
      V[2] += d2 + dV[2];
      dV[2] = d2;
      tV[2] = sat(V[2] * kInvVt2);
      float d3 = g * (tV[2] - tV[3]);
      V[3] += d3 + dV[3];
      dV[3] = d3;
      tV[3] = sat(V[3] * kInvVt2);
      float y = w[0] * u + w[1] * V[0] + w[2] * V[1] + w[3] * V[2] + w[4] * V[3];
      if (xf) {
        a += dx;
        const float yo = ow[0] * u + ow[1] * V[0] + ow[2] * V[1] + ow[3] * V[2] + ow[4] * V[3];
        y = yo + a * (y - yo);
      }
      x[i] = out(f, r, lc * y);
    }
  }
}

// one MS-20 sample on states s (pre: BP's high pass), the topology its TYPE's
inline float ms20_tick(const Filter& f, float* s, float& pre, int hp, int bp, float in, float st, float ist) {
  const float G = f.G, K = f.K;
  float y;
  if (bp) {                              // the band: a one-pole high pass first (the MS-20's HPF, then its LPF)
    float v = (in - pre) * G, l = v + pre;
    pre = l + v;
    in = 2.5f * (in - l);
  }
  // LP (and BP): LPF1 -> u -> LPF2, HPF1 on the output for the feedback; HP: HPF1 -> u, HPF2 and LPF1 on it
  float v1 = (in - s[0]) * G, l1 = v1 + s[0];
  s[0] = l1 + v1;
  float u = f.a0 * ((hp ? in - l1 : l1) + f.b2 * s[1] + f.b3 * s[2]);
  u = sat(st * u) * ist;                 // (Pirkle's NLP: in its loop)
  float y2;
  if (hp) {
    y = K * u;
    float v2 = (y - s[1]) * G, l2 = v2 + s[1];
    s[1] = l2 + v2;
    y2 = y - l2;
  } else {
    float v2 = (u - s[1]) * G;
    y2 = v2 + s[1];
    s[1] = y2 + v2;
    y = K * y2;
    y2 = y;
  }
  float v3 = (y2 - s[2]) * G, l3 = v3 + s[2];
  s[2] = l3 + v3;
  return y;
}

template <bool XF>
void run_ms20(Filter& f, Run& r, float* x, uint32_t n) {
  Ms20K K = ms20_k(f);
  float* s = f.k35;
  const int hp = f.type == MF_HP, bp = f.type == MF_BP;
  // (0.16.65) a new TYPE (its loop another): the old one played on on a copy of the states and the new one faded in
  // over the block (no reset: no click)
  const bool xf = XF;
  const int ohp = f.from_type == MF_HP, obp = f.from_type == MF_BP;
  float* os = f.xk;
  float opre = f.xpre;
  const float dx = xf ? (f.xa1 - f.xa) / static_cast<float>(n) : 0.0f;
  float a = f.xa;
  for (uint32_t i0 = 0; i0 < n; i0 += kStep) {
    uint32_t i1 = i0 + kStep > n ? n : i0 + kStep, ping_at;
    sub_block(f, r, i0, i1, &ping_at);
    if (r.pg) r.pg = 0, K = ms20_k_glide(f);
    const float st = K.st, ist = K.ist, mk = K.mk;
    for (uint32_t i = i0; i < i1; ++i) {
      if (r.fm_on && !(i & r.skip)) fm_coefs(f, r);
      float in = x[i] + (i == ping_at ? r.ping : 0.0f);
      if (r.nv != 0.0f) in += r.nv * cnoise(f);
      float y = ms20_tick(f, s, f.pre, hp, bp, in, st, ist);
      if (xf) {
        a += dx;
        const float yo = ms20_tick(f, os, opre, ohp, obp, in, st, ist);
        y = yo + a * (y - yo);
      }
      x[i] = out(f, r, y * f.iK * mk);
    }
  }
  if (xf) f.xpre = opre;
}

template <bool XF>
void run_sem(Filter& f, Run& r, float* x, uint32_t n) {
  // DRIVE: its OTAs' input gain k (0: the linear filter, as it was), and its input
  SvfK K = svf_k(f, false);
  const float wl = f.type == MF_LP ? 1.0f : 0.0f, wh = f.type == MF_HP ? 1.0f : 0.0f;
  // (0.16.65) a new TYPE: from the old one's outputs over the block (the states the same for all three)
  const int ot = XF ? f.from_type : f.type;
  const float ol = ot == MF_LP ? 1.0f : 0.0f, oh = ot == MF_HP ? 1.0f : 0.0f;
  const float dx = XF ? (f.xa1 - f.xa) / static_cast<float>(n) : 0.0f;
  float a = XF ? f.xa : 1.0f;
  for (uint32_t i0 = 0; i0 < n; i0 += kStep) {
    uint32_t i1 = i0 + kStep > n ? n : i0 + kStep, ping_at;
    sub_block(f, r, i0, i1, &ping_at);
    if (r.pg) r.pg = 0, K = svf_k_glide(f, false);
    const float k = K.k, dg = K.dg, bias = K.bias, sb = K.sb, lvl = K.lvl;
    const float wb = f.type == MF_BP ? K.q : 0.0f, ob = ot == MF_BP ? K.q : 0.0f;   // (the band: 0 dB, then its Q)
    for (uint32_t i = i0; i < i1; ++i) {
      if (r.fm_on && !(i & r.skip)) fm_coefs(f, r);
      float lp, hp, in = x[i] + (i == ping_at ? r.ping : 0.0f);
      if (r.nv != 0.0f) in += r.nv * cnoise(f);   // (the noise: its envelope's)
      if (k > 0.0f) in = drive_in(f, in, dg, bias, sb, lvl);
      else f.dstale = 1;
      float bp = k > 0.0f ? svf_nl(f.sv, f.nl, in, f.g, f.b3, k, &lp, &hp) : svf(f.sv, in, f.g, f.b2, f.b3, &lp, &hp);
      float y = wl * lp + wb * bp + wh * hp;
      if (XF && a < 1.0f) {
        a += dx;
        const float yo = ol * lp + ob * bp + oh * hp;
        y = yo + a * (y - yo);
      }
      x[i] = out(f, r, y);
    }
  }
}

void run_vox(Filter& f, Run& r, float* x, uint32_t n) {
  // DRIVE: the resonators' OTAs (gentler than SEM's: the vowels stay), the input; each formant +7 dB
  SvfK K = svf_k(f, true);
  for (uint32_t i0 = 0; i0 < n; i0 += kStep) {
    uint32_t i1 = i0 + kStep > n ? n : i0 + kStep, ping_at;
    sub_block(f, r, i0, i1, &ping_at);
    if (r.pg) r.pg = 0, K = svf_k_glide(f, true);
    const float gain = K.q, k = K.k, dg = K.dg, bias = K.bias, sb = K.sb, lvl = K.lvl;
    for (uint32_t i = i0; i < i1; ++i) {
      if (r.fm_on && !(i & r.skip)) fm_coefs(f, r);
      float lp, hp, sum = 0.0f, in = x[i] + (i == ping_at ? 4.0f * r.ping : 0.0f);
      if (r.nv != 0.0f) in += 1.5f * r.nv * cnoise(f);   // (narrow formants: more of it)
      if (k > 0.0f) in = drive_in(f, in, dg, bias, sb, lvl);
      else f.dstale = 1;
      if (k > 0.0f)
        for (int j = 0; j < 3; ++j)
          sum += kVoxGain[j] * svf_nl(&f.sv[2 * j], &f.nl[2 * j], in, f.vg[j], f.vrho[j], k, &lp, &hp);
      else
        for (int j = 0; j < 3; ++j) sum += kVoxGain[j] * svf(&f.sv[2 * j], in, f.vg[j], f.va0[j], f.vrho[j], &lp, &hp);
      x[i] = out(f, r, sum * gain);
    }
  }
}

// (0.16.65) a block with a TYPE on its way (12 ms after the random dose throws it): in the flash
__attribute__((noinline)) void process_xf(Filter& f, Run& r, float* x, uint32_t n) {
  f.xa1 = fm_minf(1.0f, f.xa + static_cast<float>(n) * kXfRate);
  switch (f.model) {
    case MF_LADDER: run_ladder<true>(f, r, x, n); break;
    case MF_MS20: run_ms20<true>(f, r, x, n); break;
    default: run_sem<true>(f, r, x, n); break;   // (SEM: VOX's type, its vowels' set, takes no fade, and stays
  }                                              // in the RAM alone)
  f.xa = f.xa1;                         // (the new type reached: the old one gone)
  for (int i = 0; i < 3; ++i) f.xk[i] = fm_flush(f.xk[i]);
  f.xpre = fm_flush(f.xpre);
  if (f.xa >= 1.0f || f.from_type < 0) f.from_type = -1;
}

void process(Filter& f, float* x, uint32_t n) {
  Run r;
  r.oct = f.oct0;
  r.doct = (f.oct1 - f.oct0) / static_cast<float>(n);
  r.vs = f.vs;
  r.y1 = f.y1;
  r.ramp = f.ramp;
  r.ti = 0;
  r.pg = 0;
  int sk = fm_skip;
  r.fm_on = (f.fm != 0.0f || f.fm_t != 0.0f) && sk >= 0;
  r.skip = sk < 0 ? 0 : sk;
  r.fm5 = f.fm * 5.0f;
  r.ping = f.ping * 3.0f;
  if (f.ntrig) f.still = 0, f.fade = 0;   // (a trig wakes it)
  if (f.still) {                         // STOP: faded out, still (a self-oscillation, a ring: silent) until a trig
    for (uint32_t i = 0; i < n; ++i) x[i] = 0.0f;
    f.oct0 = f.oct1;
    f.from_type = -1;
    return;
  }
  if (f.from_type >= 0 && f.model != MF_VOX) {
    process_xf(f, r, x, n);
  } else {
    switch (f.model) {
      case MF_LADDER: run_ladder<false>(f, r, x, n); break;
      case MF_MS20: run_ms20<false>(f, r, x, n); break;
      case MF_SEM: run_sem<false>(f, r, x, n); break;
      default: run_vox(f, r, x, n); break;
    }
  }
  if (f.fade) {                          // STOP's fade out (~23 ms), then still
    float g = static_cast<float>(f.fade) * (1.0f / 1024.0f), dg = 1.0f / 1024.0f;
    for (uint32_t i = 0; i < n; ++i) {
      x[i] *= g;
      g = g > dg ? g - dg : 0.0f;
    }
    f.fade = f.fade > n ? f.fade - n : 0;
    if (!f.fade) {
      reset_state(f);
      f.still = 1;
    }
  }
  for (int i = 0; i < 4; ++i) f.V[i] = fm_flush(f.V[i]), f.dV[i] = fm_flush(f.dV[i]), f.tV[i] = fm_flush(f.tV[i]);
  for (int i = 0; i < 3; ++i) f.k35[i] = fm_flush(f.k35[i]);
  for (int i = 0; i < 6; ++i) f.sv[i] = fm_flush(f.sv[i]);
  f.pre = fm_flush(f.pre);
  f.vs = r.vs;
  f.y1 = r.y1;
  f.ramp = r.ramp;
  f.ntrig = 0;
  f.oct0 = f.oct1;
}

// (0.16.65) DECAY's, ATTACK's and FM's curves (their exponentials), made again only when their knobs move (each block
// before: the same values, and their code in the RAM, its room full), in the flash
__attribute__((noinline)) void slow_coefs(Filter& f, float d, float a, float fk) {
  f.kd = d, f.ka = a, f.kf = fk;
  f.short_decay = 200.0f * kInvSr * fm_exp2f(-8.0f * d);   // (Plaits' coefficients, a sample's: 5 ms .. 1.3 s, its
  f.decay_tail = 20.0f * kInvSr * fm_exp2f(-6.0f * d) - f.short_decay;   // tail 3 s)
  f.attack = 0.04f * static_cast<float>(kStep) * fm_exp2f(-12.0f * a);   // (0.7 ms .. 2.3 s)
  f.fm_t = fm_depth(fk);
}
// (0.16.65) a new TYPE, the model the same: from the type it was, the MS-20's loop as it was on its own states (in the
// flash: mf_set is in the RAM, its room full)
__attribute__((noinline)) void begin_xf(Filter& f) {
  f.from_type = f.type;
  f.xa = 0.0f;
  for (int i = 0; i < 3; ++i) f.xk[i] = f.k35[i];
  f.xpre = f.pre;
}
}  // namespace

extern "C" {

// (0.16.30) the ladder's stages' g for a cutoff (Hz), and in *kc its feedback's scale (LADDER's, TAURUS'). Its loop,
// a sample late (each stage's own last state, the feedback the last output: Huovilainen's), rings lower than its
// stages' cutoff and sings sooner as it rises: D'Angelo and Valimaki's g put it at 2.7 kHz singing at RESO 50 % for a
// CUTOFF of 6.4 kHz, and closed it past that (1.3 kHz at 9 kHz). Here a, its stages' gain (g / 2 Vt), is the one
// whose loop rings at that cutoff (a fit: within a cent from 15 Hz to 4 kHz), then eases to 0.3 (the ring at 4.8 kHz,
// the low pass open to 6.8 kHz without emphasis, its stages still a decibel or two from flat); kc the loop's gain
// there over its gain at rest, so that it sings past 4 at every cutoff, as the real one does
__attribute__((noinline)) float ladder_g(float hz, float* kc) {
  const float w = kPi * hz * kInvSr, wk = 0.284952f, ak = 0.250230f;
  const float a = w <= wk ? w * (0.999615f + w * (-0.973237f + w * (2.37433f - 1.59619f * w)))
                          : 0.3f - (0.3f - ak) * fm_exp2f(-25.3813f * (w - wk));
  *kc = 0.998467f + a * (-1.84811f + a * (-10.4595f + a * (32.3894f - 21.0662f * a)));
  return a * (2.0f * 0.312f);
}

MI_COLD void mf_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) {
    Filter& f = flt[t];
    f.model = MF_OFF;
    f.type = MF_LP;
    f.oct0 = f.oct1 = 9.8f;
    f.reso = f.drive = f.env = f.ping = f.fm = 0.0f;
    f.noise_amt = f.color = f.nlp = 0.0f;
    f.attack = 0.64f;
    f.short_decay = 0.001f;
    f.decay_tail = 0.0f;
    f.vs = 0.0f;
    f.ramp = 0;
    f.ntrig = 0;
    f.fade = 0;
    f.still = 0;
    f.rng = 0x9E3779B9u * static_cast<uint32_t>(t + 1);
    f.now_oct = 9.8f;
    f.g = f.G = f.a0 = f.b2 = f.b3 = 0.0f;
    f.K = f.iK = 1.0f;
    f.from_type = -1;
    f.reso_t = f.drive_t = f.fm_t = f.env_t = 0.0f;
    f.pfresh = 1;
    f.kd = f.ka = f.kf = -9.0f;         // (their curves made at the first set)
    reset_state(f);
  }
}

void mf_set(int t, int model, int type, const float* k, float note) {
  if (t < 0 || t >= MF_TRACKS) return;
  Filter& f = flt[t];
  if (model != f.model) {
    reset_state(f);
    f.from_type = -1;
    f.pfresh = 1;
  } else if (type != f.type && f.model != MF_OFF && f.model != MF_VOX && f.from_type < 0) {   // (0.16.65: no reset;
    begin_xf(f);                                                                             // from the type it was)
  }
  f.model = model < 0 || model >= MF_MODELS ? MF_OFF : model;
  f.type = type < 0 || type >= MF_TYPES ? MF_LP : type;
  float oct = 9.8f * fm_clampf(k[MF_CUTOFF], 0.0f, 1.0f) + k[MF_KEY] * (note - 60.0f) * (1.0f / 12.0f);
  f.oct1 = oct;
  f.reso_t = fm_clampf(k[MF_RESO], 0.0f, 1.0f);
  f.env_t = fm_clampf(k[MF_ENV], -1.0f, 1.0f);
  const float d = fm_clampf(k[MF_DECAY], 0.0f, 1.0f), a = fm_clampf(k[MF_ATTACK], 0.0f, 1.0f);
  const float fk = fm_clampf(k[MF_FM], -1.0f, 1.0f);
  if (d != f.kd || a != f.ka || fk != f.kf) slow_coefs(f, d, a, fk);
  f.drive_t = fm_clampf(k[MF_DRIVE], 0.0f, 1.0f);
  f.ping = fm_clampf(k[MF_PING], 0.0f, 1.0f);
  if (f.pfresh) f.reso = f.reso_t, f.drive = f.drive_t, f.fm = f.fm_t, f.env = f.env_t, f.pfresh = 0;
  float nz = fm_clampf(k[MF_NOISE], 0.0f, 1.0f);
  f.noise_amt = 0.4f * nz * nz;
  f.color = fm_clampf(k[MF_COLOR], -1.0f, 1.0f);
  reso_coefs(f);
}

void mf_trigger(int t, uint32_t at) {
  if (t < 0 || t >= MF_TRACKS) return;
  Filter& f = flt[t];
  if (f.ntrig < 8) f.trig[f.ntrig++] = at;
}

void mf_process(int t, float* x, uint32_t n) {
  if (t < 0 || t >= MF_TRACKS) return;
  Filter& f = flt[t];
  if (f.model == MF_OFF) {
    f.ntrig = 0;
    f.oct0 = f.oct1;
    return;
  }
  process(f, x, n);
}

void mf_stop(int t) {                   // the transport stopped: its envelope closed, the filter faded out, then still
  if (t < 0 || t >= MF_TRACKS) return;
  Filter& f = flt[t];
  f.vs = 0.0f;
  f.ramp = 0;
  if (!f.still && f.model != MF_OFF) f.fade = 1024;
}

void mf_reset(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  reset_state(flt[t]);
  flt[t].pfresh = 1;
  flt[t].vs = 0.0f;
  flt[t].ramp = 0;
  flt[t].ntrig = 0;
}

void mf_guard(int level) { fm_skip = level <= 0 ? 0 : level == 1 ? 1 : level == 2 ? 3 : -1; }

float mf_cutoff_hz(int t) { return t >= 0 && t < MF_TRACKS ? hz_of(flt[t].now_oct) : 0.0f; }
float mf_env(int t) { return t >= 0 && t < MF_TRACKS ? flt[t].vs : 0.0f; }

const char* mf_model_name(int m) {
  static const char* const N[MF_MODELS] = {"OFF", "LADDER", "MS-20", "SEM", "VOX"};
  return m >= 0 && m < MF_MODELS ? N[m] : "?";
}
const char* mf_type_name(int m, int type) {
  static const char* const T[MF_TYPES] = {"LP", "BP", "HP"};
  static const char* const V[MF_TYPES] = {"LOW", "MID", "HIGH"};
  if (type < 0 || type >= MF_TYPES) return "?";
  return m == MF_VOX ? V[type] : T[type];
}

}  // extern "C"

/* MoogLadders' ImprovedModel.h, whose equations LADDER follows:
 *
 * Copyright 2012 Stefano D'Angelo <zanga.mail@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby
 * granted, provided that the above copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE. */
