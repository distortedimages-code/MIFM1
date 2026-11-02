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

inline float tanh_pade(float x) {        // tanh, as a (3, 2) Padé: odd, 1 at 3 and beyond, within 2 % (a transistor
  if (x > 3.0f) return 1.0f;            // pair's curve: the shape, not the digits)
  if (x < -3.0f) return -1.0f;
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

struct Filter {
  int model, type;
  float oct0, oct1;                      // the cutoff from its knob and the note (octaves over 20 Hz): the last
                                         // block's, this one's (glided over the block)
  float reso, special, env, ping, fm;
  float short_decay, decay_tail;         // the vactrol's
  float vs;                              // its state, 0..1
  int ramp;                              // rising
  uint32_t trig[8];                      // this block's trigs (their samples)
  int ntrig;
  float y1;                              // the last output (FM)
  uint32_t rng;
  float now_oct;                         // (the screen: the cutoff as it is)
  // the coefficients, made every 8 samples (each sample with FM): a model's own
  float g, G, a0, b2, b3, K;             // (LADDER: g; MS-20: g, G, a0, b2, b3, K; SEM: g, a0, rho in b2)
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

const float kVt2 = 2.0f * 0.312f, kInvVt2 = 1.0f / (2.0f * 0.312f);

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

// a zero-delay feedback state variable filter (Zavalishin's; Pirkle's SEM): its band pass, its low and high in lp, hp
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

// LADDER, a sample: D'Angelo and Valimaki's equations (ImprovedModel.h's), its output not inverted
inline float ladder(Filter& f, float x, float res, float drive, int type) {
  float g = f.g, u = drive * x - res * f.V[3];
  float d0 = g * (tanh_pade(u * kInvVt2) - f.tV[0]);
  f.V[0] += d0 + f.dV[0];
  f.dV[0] = d0;
  f.tV[0] = tanh_pade(f.V[0] * kInvVt2);
  float d1 = g * (f.tV[0] - f.tV[1]);
  f.V[1] += d1 + f.dV[1];
  f.dV[1] = d1;
  f.tV[1] = tanh_pade(f.V[1] * kInvVt2);
  float d2 = g * (f.tV[1] - f.tV[2]);
  f.V[2] += d2 + f.dV[2];
  f.dV[2] = d2;
  f.tV[2] = tanh_pade(f.V[2] * kInvVt2);
  float d3 = g * (f.tV[2] - f.tV[3]);
  f.V[3] += d3 + f.dV[3];
  f.dV[3] = d3;
  f.tV[3] = tanh_pade(f.V[3] * kInvVt2);
  if (type == MF_LP) return f.V[3] * (1.0f + 0.35f * res);   // (the passband the resonance takes, a third given back)
  float in = kVt2 * tanh_pade(u * kInvVt2);   // the chain's input as its first stage sees it
  if (type == MF_HP) return in - 4.0f * f.V[0] + 6.0f * f.V[1] - 4.0f * f.V[2] + f.V[3];
  return 4.0f * (f.V[1] - 2.0f * f.V[2] + f.V[3]);   // (a 12 dB band: two stages high, two low)
}

// MS-20: Korg 35, a sample (Pirkle's KORG35 LPF / HPF, its NLP on: tanh(sat u) in the loop)
inline float ms20(Filter& f, float x, float sat, float isat, int type) {
  float* s = f.k35;
  float G = f.G, K = f.K, y;
  if (type == MF_BP) {                   // the band: a one-pole high pass first (the MS-20's HPF, then its LPF)
    float v = (x - f.pre) * G, l = v + f.pre;
    f.pre = l + v;
    x = 2.5f * (x - l);
  }
  if (type != MF_HP) {                   // LPF1 -> (u) -> LPF2, HPF1 on the output for the feedback
    float v1 = (x - s[0]) * G, y1 = v1 + s[0];
    s[0] = y1 + v1;
    float u = f.a0 * (y1 + f.b2 * s[1] + f.b3 * s[2]);
    u = tanh_pade(sat * u) * isat;
    float v2 = (u - s[1]) * G, y2 = v2 + s[1];
    s[1] = y2 + v2;
    y = K * y2;
    float v3 = (y - s[2]) * G, l3 = v3 + s[2];
    s[2] = l3 + v3;
  } else {                               // HPF1 -> (u) -> y, HPF2 and LPF1 on it for the feedback
    float v1 = (x - s[0]) * G, l1 = v1 + s[0];
    s[0] = l1 + v1;
    float u = f.a0 * ((x - l1) + f.b2 * s[1] + f.b3 * s[2]);
    u = tanh_pade(sat * u) * isat;
    y = K * u;
    float v2 = (y - s[1]) * G, l2 = v2 + s[1];
    s[1] = l2 + v2;
    float v3 = ((y - l2) - s[2]) * G, l3 = v3 + s[2];
    s[2] = l3 + v3;
  }
  return y / K;
}

void process(Filter& f, float* x, uint32_t n) {
  const float res = f.reso, sp = f.special;
  const float ladder_res = 4.2f * res, ladder_drive = 1.0f + 15.0f * sp * sp;
  const float k35_sat = 1.0f + 7.0f * sp, k35_isat = 1.0f / k35_sat;
  const float noise_amt = f.model == MF_SEM ? 0.35f * sp * sp : 0.5f * sp;
  const int fm_on = f.fm != 0.0f, model = f.model, type = f.type;
  const float fm5 = f.fm * 5.0f;
  float oct = f.oct0, doct = (f.oct1 - f.oct0) / static_cast<float>(n);
  float vs = f.vs, y1 = f.y1, out_bp = 0.0f;
  int ramp = f.ramp, ti = 0;
  if (model == MF_SEM) out_bp = 2.0f * f.a0 * (1.0f + 2.0f * res);   // (the band: its peak 0 dB, then the resonance's)
  for (uint32_t i0 = 0; i0 < n; i0 += 8) {
    uint32_t i1 = i0 + 8 > n ? n : i0 + 8;
    int pinged[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    while (ti < f.ntrig && f.trig[ti] < i1) {   // a trig: the vactrol rises, the filter is pinged
      pinged[f.trig[ti] - i0 < 8 ? f.trig[ti] - i0 : 0] = 1;
      ++ti;
      ramp = 1;
    }
    // the vactrol (Plaits' LPGEnvelope), a step each 8 samples
    if (ramp) {
      vs += 0.32f;
      if (vs >= 1.0f) vs = 1.0f, ramp = 0;
    } else {
      float s2 = vs * vs, coef = f.short_decay + (1.0f - s2 * s2) * f.decay_tail;
      vs -= 8.0f * coef * vs;
      if (vs < 1e-6f) vs = 0.0f;
    }
    oct += doct * static_cast<float>(i1 - i0);
    float c = oct + f.env * 8.0f * vs;
    if (!fm_on) coefs(f, fm_clampf(c, -0.5f, 10.2f));
    for (uint32_t i = i0; i < i1; ++i) {
      float in = x[i], y;
      if (pinged[i - i0]) in += f.ping * (model == MF_VOX ? 12.0f : 3.0f);   // (VOX: narrow, a harder knock)
      if (fm_on) coefs(f, fm_clampf(c + fm5 * y1, -0.5f, 10.2f));
      switch (model) {
        case MF_LADDER:
          y = ladder(f, in, ladder_res, ladder_drive, type);
          break;
        case MF_MS20:
          y = ms20(f, in, k35_sat, k35_isat, type);
          break;
        case MF_SEM: {
          float lp, hp, bp = svf(f.sv, in + noise_amt * noise(f), f.g, f.b2, f.b3, &lp, &hp);
          y = type == MF_LP ? lp : type == MF_BP ? bp * out_bp : hp;
          break;
        }
        default: {
          float lp, hp, sum = 0.0f, v = in + noise_amt * noise(f);
          for (int k = 0; k < 3; ++k) sum += kVoxGain[k] * svf(&f.sv[2 * k], v, f.vg[k], f.va0[k], f.vrho[k], &lp, &hp);
          y = sum * 2.0f * f.a0 * (2.4f + 0.14f / (2.0f * f.a0));   // (each formant's peak +7 dB, sharper a little louder)
          break;
        }
      }
      if (!(y == y)) {                   // (blown up: never, but a NaN would stay)
        reset_state(f);
        y = 0.0f;
      }
      y = soft(y);
      y1 = y;
      x[i] = y;
    }
  }
  for (int i = 0; i < 4; ++i) f.V[i] = fm_flush(f.V[i]), f.tV[i] = fm_flush(f.tV[i]);
  for (int i = 0; i < 3; ++i) f.k35[i] = fm_flush(f.k35[i]);
  for (int i = 0; i < 6; ++i) f.sv[i] = fm_flush(f.sv[i]);
  f.pre = fm_flush(f.pre);
  f.vs = vs;
  f.y1 = y1;
  f.ramp = ramp;
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
    f.rng = 0x9E3779B9u * static_cast<uint32_t>(t + 1);
    f.now_oct = 9.8f;
    f.g = f.G = f.a0 = f.b2 = f.b3 = 0.0f;
    f.K = 1.0f;
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
  if (f.model == MF_MS20) f.K = 0.01f + 2.2f * f.reso;   // (over 2: it sings, its loop's tanh holding it)
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
