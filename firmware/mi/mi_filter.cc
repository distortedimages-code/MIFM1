// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1: a filter on each track's voice (after its grit), four models of analog filters, written for the FM-1 (float,
// no libm, a sample at a time):
//   LADDER  Moog's transistor ladder, after Stefano D'Angelo and Vesa Välimäki's improved virtual analog model ("An
//           Improved Virtual Analog Model of the Moog Ladder Filter", ICASSP 2013; its reference code, MoogLadders'
//           ImprovedModel.h, Copyright 2012 Stefano D'Angelo, ISC licence: notice at the end): a transistor pair's
//           tanh at each of the four stages, the thermal voltage 0.312, trapezoidal steps. Low pass 24 dB; band and
//           high pass from its stages (Oberheim Xpander's mixes). DRIVE: the input into the transistors.
//   MS-20   Korg 35 (the MS-10's, the first MS-20's), after Will Pirkle's model (app note AN-5, "Designing Software
//           Synthesizer Plug-Ins in C++") on Vadim Zavalishin's zero-delay feedback one-poles ("The Art of VA Filter
//           Design"), its saturation (tanh) inside the resonance loop as Pirkle's NLP option has it: the resonance
//           drives. Low and high pass as the circuit is; band pass, a one-pole high pass into its low pass (the
//           MS-20's two filters in series). DRIVE: the loop's saturation.
//   SEM     Oberheim SEM's 12 dB state variable filter (Pirkle's model on Zavalishin's: low, band, high pass), white
//           noise into it (NOISE): its resonance whistles and breathes.
//   VOX     three formants (the vowels' first three resonances, as a voice makes them: Peterson and Barney's measured
//           averages, low, mid and high voices), CUTOFF going A, E, I, O, U, its resonance their sharpness; BREATH:
//           noise into them (a whisper).
// Each has its envelope, a vactrol's (Plaits' low-pass gate envelope, ported: a fast rise, a fall slowing as it goes
// down, DECAY its length), its PING (an impulse into the filter at each trig: its resonance rings), its FM (its own
// output into its cutoff, at audio rate, either way) and its KEY (the note into its cutoff).
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
  float reso, special, env, ping, fm;
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
  float vg[3], va0[3], vrho[3];          // VOX's three formants
  // LADDER
  float V[4], dV[4], tV[4];
  // MS-20: the one-poles' states (LPF1, LPF2, HPF1 or HPF2), the pre high pass (band)
  float k35[3], pre;
  // SEM / VOX: the state variable filters' states (two each, three for VOX)
  float sv[6];
};
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

inline float hz_of(float oct) { return 20.0f * fm_exp2f(oct); }
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
  for (int i = 0; i < 6; ++i) f.sv[i] = 0.0f;
  f.pre = f.y1 = 0.0f;
}

const float kInvVt2 = 1.0f / (2.0f * 0.312f);   // (the ladder: its transistors' thermal voltage, 0.312, twice)

// the coefficients for a cutoff of c octaves over 20 Hz (the model's range kept)
void coefs(Filter& f, float c) {
  f.now_oct = c;
  float hz = hz_of(c);
  switch (f.model) {
    case MF_LADDER: {                    // D'Angelo and Valimaki's g (its warping is for cutoffs well under sr / pi),
      if (hz > 0.23f * kSr) hz = 0.23f * kSr;   // over 2 sr: the trapezoidal steps
      float w = kPi * hz * kInvSr;
      f.g = 4.0f * kPi * 0.312f * hz * (1.0f - w) / (1.0f + w) * (0.5f * kInvSr);
      break;
    }
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

// a transistor pair's curve without a division (the FM-1 reads code through a cache both cores share, and divides
// slowly): a cubic, tanh's slope at 0, flat at 1.5 and beyond (where tanh is at 0.9)
inline float sat(float x) {
  if (x > 1.5f) return 1.0f;
  if (x < -1.5f) return -1.0f;
  return x - (4.0f / 27.0f) * x * x * x;
}

// the vactrol (Plaits' LPGEnvelope), a step each kStep samples; a trig in this sub-block starts its rise
const uint32_t kStep = 16;
inline float vactrol(Filter& f, float vs, int* ramp) {
  if (*ramp) {
    vs += 0.04f * static_cast<float>(kStep);
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
  float oct, doct, vs, y1, fm5, ping;
  int ramp, ti, fm_on;
};

__attribute__((noinline)) void sub_block(Filter& f, Run& r, uint32_t i0, uint32_t i1, uint32_t* ping_at) {
  *ping_at = 0xFFFFFFFFu;
  while (r.ti < f.ntrig && f.trig[r.ti] < i1) {
    *ping_at = f.trig[r.ti] < i0 ? i0 : f.trig[r.ti];
    ++r.ti;
    r.ramp = 1;
  }
  r.vs = vactrol(f, r.vs, &r.ramp);
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

void run_ladder(Filter& f, Run& r, float* x, uint32_t n) {
  const float res = 4.2f * f.reso, drive = 1.0f + 15.0f * f.special * f.special;
  // its outputs mixed: the input, then its four stages (LP: the last, its passband given back a third; HP, BP: the
  // Xpander's mixes)
  float w[5] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f + 0.35f * res};
  if (f.type == MF_HP) w[0] = 1.0f, w[1] = -4.0f, w[2] = 6.0f, w[3] = -4.0f, w[4] = 1.0f;
  else if (f.type == MF_BP) w[2] = 4.0f, w[3] = -8.0f, w[4] = 4.0f;
  float* V = f.V;
  float* dV = f.dV;
  float* tV = f.tV;
  for (uint32_t i0 = 0; i0 < n; i0 += kStep) {
    uint32_t i1 = i0 + kStep > n ? n : i0 + kStep, ping_at;
    sub_block(f, r, i0, i1, &ping_at);
    for (uint32_t i = i0; i < i1; ++i) {
      if (r.fm_on) fm_coefs(f, r);
      float g = f.g, u = drive * (x[i] + (i == ping_at ? r.ping : 0.0f)) - res * V[3];
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
      x[i] = out(f, r, w[0] * u + w[1] * V[0] + w[2] * V[1] + w[3] * V[2] + w[4] * V[3]);
    }
  }
}

void run_ms20(Filter& f, Run& r, float* x, uint32_t n) {
  const float st = 1.0f + 7.0f * f.special, ist = 1.0f / st;
  float* s = f.k35;
  const int hp = f.type == MF_HP, bp = f.type == MF_BP;
  for (uint32_t i0 = 0; i0 < n; i0 += kStep) {
    uint32_t i1 = i0 + kStep > n ? n : i0 + kStep, ping_at;
    sub_block(f, r, i0, i1, &ping_at);
    for (uint32_t i = i0; i < i1; ++i) {
      if (r.fm_on) fm_coefs(f, r);
      float G = f.G, K = f.K, in = x[i] + (i == ping_at ? r.ping : 0.0f), y;
      if (bp) {                          // the band: a one-pole high pass first (the MS-20's HPF, then its LPF)
        float v = (in - f.pre) * G, l = v + f.pre;
        f.pre = l + v;
        in = 2.5f * (in - l);
      }
      // LP (and BP): LPF1 -> u -> LPF2, HPF1 on the output for the feedback; HP: HPF1 -> u, HPF2 and LPF1 on it
      float v1 = (in - s[0]) * G, l1 = v1 + s[0];
      s[0] = l1 + v1;
      float u = f.a0 * ((hp ? in - l1 : l1) + f.b2 * s[1] + f.b3 * s[2]);
      u = sat(st * u) * ist;             // (Pirkle's NLP: in its loop)
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
      x[i] = out(f, r, y * f.iK);
    }
  }
}

void run_sem(Filter& f, Run& r, float* x, uint32_t n) {
  const float nz = 0.35f * f.special * f.special;
  const float wl = f.type == MF_LP ? 1.0f : 0.0f, wh = f.type == MF_HP ? 1.0f : 0.0f;
  const float wb = f.type == MF_BP ? 2.0f * f.a0 * (1.0f + 2.0f * f.reso) : 0.0f;   // (the band: 0 dB, then its Q)
  for (uint32_t i0 = 0; i0 < n; i0 += kStep) {
    uint32_t i1 = i0 + kStep > n ? n : i0 + kStep, ping_at;
    sub_block(f, r, i0, i1, &ping_at);
    for (uint32_t i = i0; i < i1; ++i) {
      if (r.fm_on) fm_coefs(f, r);
      float lp, hp, in = x[i] + (i == ping_at ? r.ping : 0.0f) + nz * r.vs * noise(f);   // (the noise: its envelope's)
      float bp = svf(f.sv, in, f.g, f.b2, f.b3, &lp, &hp);
      x[i] = out(f, r, wl * lp + wb * bp + wh * hp);
    }
  }
}

void run_vox(Filter& f, Run& r, float* x, uint32_t n) {
  const float nz = 0.5f * f.special, gain = 2.0f * f.a0 * (2.4f + 0.14f / (2.0f * f.a0));   // (each formant +7 dB)
  for (uint32_t i0 = 0; i0 < n; i0 += kStep) {
    uint32_t i1 = i0 + kStep > n ? n : i0 + kStep, ping_at;
    sub_block(f, r, i0, i1, &ping_at);
    for (uint32_t i = i0; i < i1; ++i) {
      if (r.fm_on) fm_coefs(f, r);
      float lp, hp, sum = 0.0f, in = x[i] + (i == ping_at ? 4.0f * r.ping : 0.0f) + nz * r.vs * noise(f);
      for (int k = 0; k < 3; ++k) sum += kVoxGain[k] * svf(&f.sv[2 * k], in, f.vg[k], f.va0[k], f.vrho[k], &lp, &hp);
      x[i] = out(f, r, sum * gain);
    }
  }
}

void process(Filter& f, float* x, uint32_t n) {
  Run r;
  r.oct = f.oct0;
  r.doct = (f.oct1 - f.oct0) / static_cast<float>(n);
  r.vs = f.vs;
  r.y1 = f.y1;
  r.ramp = f.ramp;
  r.ti = 0;
  r.fm_on = f.fm != 0.0f;
  r.fm5 = f.fm * 5.0f;
  r.ping = f.ping * 3.0f;
  if (f.ntrig) f.still = 0, f.fade = 0;   // (a trig wakes it)
  if (f.still) {                         // STOP: faded out, still (a self-oscillation, a ring: silent) until a trig
    for (uint32_t i = 0; i < n; ++i) x[i] = 0.0f;
    f.oct0 = f.oct1;
    return;
  }
  switch (f.model) {
    case MF_LADDER: run_ladder(f, r, x, n); break;
    case MF_MS20: run_ms20(f, r, x, n); break;
    case MF_SEM: run_sem(f, r, x, n); break;
    default: run_vox(f, r, x, n); break;
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

}  // namespace

extern "C" {

void mf_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) {
    Filter& f = flt[t];
    f.model = MF_OFF;
    f.type = MF_LP;
    f.oct0 = f.oct1 = 9.8f;
    f.reso = f.special = f.env = f.ping = f.fm = 0.0f;
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
    reset_state(f);
  }
}

void mf_set(int t, int model, int type, const float* k, float note) {
  if (t < 0 || t >= MF_TRACKS) return;
  Filter& f = flt[t];
  if (model != f.model || type != f.type) reset_state(f);
  f.model = model < 0 || model >= MF_MODELS ? MF_OFF : model;
  f.type = type < 0 || type >= MF_TYPES ? MF_LP : type;
  float oct = 9.8f * fm_clampf(k[MF_CUTOFF], 0.0f, 1.0f) + k[MF_KEY] * (note - 60.0f) * (1.0f / 12.0f);
  f.oct1 = oct;
  f.reso = fm_clampf(k[MF_RESO], 0.0f, 1.0f);
  f.env = fm_clampf(k[MF_ENV], -1.0f, 1.0f);
  float d = fm_clampf(k[MF_DECAY], 0.0f, 1.0f);   // (Plaits' coefficients, a sample's: 5 ms .. 1.3 s, its tail 3 s)
  f.short_decay = 200.0f * kInvSr * fm_exp2f(-8.0f * d);
  f.decay_tail = 20.0f * kInvSr * fm_exp2f(-6.0f * d) - f.short_decay;
  f.special = fm_clampf(k[MF_SPECIAL], 0.0f, 1.0f);
  f.ping = fm_clampf(k[MF_PING], 0.0f, 1.0f);
  f.fm = fm_clampf(k[MF_FM], -1.0f, 1.0f);
  if (f.model == MF_MS20) f.K = 0.01f + 2.2f * f.reso, f.iK = 1.0f / f.K;   // (over 2: it sings, its loop's
                                                                             // saturation holding it)
  else if (f.model == MF_SEM) f.a0 = 0.5f / (0.5f + 12.0f * f.reso * f.reso * f.reso);   // r = 1 / 2Q, Q 0.5 .. 12.5
  else if (f.model == MF_VOX) f.a0 = 0.5f / (2.0f + 18.0f * f.reso);                    // Q 2 .. 20
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
  flt[t].vs = 0.0f;
  flt[t].ramp = 0;
  flt[t].ntrig = 0;
}

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
const char* mf_special_name(int m) {
  return m == MF_SEM ? "Noise" : m == MF_VOX ? "Breath" : "Drive";
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
