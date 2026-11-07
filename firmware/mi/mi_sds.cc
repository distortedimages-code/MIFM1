// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// SDS (0.16.49): drums after Simmons' SDS 8 (1984, "a non-modular, all-analogue, five-channel kit", E&MM, April 1984),
// the hexagonal pads' brain. Its manual: "The drum sounds produced by the SDS 8 are compiled from a controlled blend of
// tone and noise", each channel set by six knobs, FILTER, PITCH, BEND, DECAY, BALANCE, CLICK, and played by the pads'
// strength. Its five channels (bass drum, snare, three toms) are one circuit, the SDS-V's voice in a smaller box; so
// here: one voice, four models (KICK, SNARE, TOM, PERC) its kits.
//
// The voice, as the SDS-V's documentation and its clones describe it, measured against hits of a later Simmons (an
// SDS 1000's, sampled: its toms analog, their pitch, their bend, their harmonics, their click; its kick and snare,
// digital there, for their balance of tone and noise; README):
//   - TONE: a triangle core, its rise a little shorter than its fall (the second harmonic at -20 dB: the warmth an
//     ideal triangle lacks), through a passive RC low pass (its corners rounded); its pitch PITCH (FREQ: C3 the kit's),
//     a free-running oscillator (each hit starts it where it is), a few cents of drift;
//   - BEND: the pitch from the strike on, down (the head relaxing) or up (the manual's tabla), toward its depth on an
//     exponential slower than the decay (2.4 times; two to three on the toms measured: the pitch still falling
//     when the sound dies);
//   - NOISE: white, through a four-pole resonant low pass (the SDS-V's SSM2044 there: its resonance held under
//     oscillation), opened by the hit and closing with its envelope (the snare's not swept), FILTER its cutoff;
//   - CLICK: that filter's noise through a VCA of a few ms (its brightness FILTER's: "The brightness of the click is
//     simultaneously affected by the Filter control"), the trigger's pulse into the filter with it;
//   - BALANCE: tone to noise; DECAY: the tone's, the noise's N.DECAY times it; HIT: the pad's strength (louder,
//     brighter noise, more bend, more click; a lock or a random a step: the dynamics);
//   - the output: an OTA's soft saturation (DRIVE), a little biased (even harmonics), the coupling capacitor.
// Its eight knobs (the track's after FREQ): FILTER, BALANCE, CLICK, DECAY, N.DECAY, HIT, DRIVE, BEND.
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
inline float sat(float x) {             // tanh, its Pade (3, 2) to 3, then flat
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  const float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

struct Kit {
  const char* name;
  float f0;                             // its pitch at C3 (Hz)
  float rise;                           // the triangle's rise, a share of its cycle (0.5: symmetric)
  float rc;                             // the tone's passive low pass (Hz)
  float sweep;                          // the noise filter opened by the hit (octaves over FILTER)
  float reso;                           // its resonance (the ladder's feedback, 0..4)
  float click_ms;                       // the click's length
  float def[8];                         // FILTER, BALANCE, CLICK, DECAY, N.DECAY, HIT, DRIVE, BEND (0..1; BEND from
};                                      // its middle)
const Kit kKits[SDS_MODELS] = {
    {"SDS KICK", 105.0f, 0.40f, 420.0f, 2.0f, 1.0f, 4.0f, {0.55f, 0.1f, 0.6f, 0.52f, 0.45f, 0.75f, 0.45f, 0.715f}},
    {"SDS SNARE", 205.0f, 0.44f, 620.0f, 0.0f, 0.6f, 6.0f, {0.78f, 0.6f, 0.22f, 0.57f, 0.55f, 0.75f, 0.45f, 0.56f}},
    {"SDS TOM", 190.0f, 0.43f, 520.0f, 2.5f, 1.4f, 9.0f, {0.55f, 0.16f, 0.42f, 0.64f, 0.55f, 0.75f, 0.45f, 0.765f}},
    {"SDS PERC", 330.0f, 0.45f, 900.0f, 1.0f, 2.8f, 3.0f, {0.72f, 0.4f, 0.35f, 0.4f, 0.5f, 0.75f, 0.45f, 0.36f}},
};
const char* const kKnob[8] = {"Filter", "Balance", "Click", "Decay", "N.Decay", "Hit", "Drive", "Bend"};

struct Sds {
  int model;
  float note, k[8];
  float ph, rc;                         // the oscillator: its phase (cycles), its RC low pass
  float et, en, ec;                     // the envelopes: the tone's, the noise's, the click's
  int at, an, ab, hold;                 // (rising: the tone's, the noise's, the bend's; the click's held samples)
  float bend;                           // the bend's envelope: 1 at the strike, falling (the pitch off by D (1 - it))
  float s[4];                           // the noise's ladder
  float pulse;                          // the trigger's pulse into it
  float hit, jit, lvl;                  // this hit's strength, its pitch off (semitones), its level
  float drift, dtarget;                 // the oscillator's drift (semitones), where it goes
  int dcount;
  float dcx, dcy;                       // the coupling capacitor
  uint32_t rnd;
  int gate, trig, alive;
};
static_assert(sizeof(Sds) <= MF_STATE_BYTES, "SDS' state");
struct SdsAt {
  Sds& operator[](int t) const { return *static_cast<Sds*>(mf_state(t)); }
};
const SdsAt sds = {};

inline uint32_t xs(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline float bip(uint32_t& s) { return static_cast<float>(static_cast<int32_t>(xs(s))) * (1.0f / 2147483648.0f); }

// the knobs' meaning, shared by the render and the screen
inline float filter_hz(float u) { return 60.0f * fm_exp2f(u * 7.9f); }          // 60 Hz .. 14 kHz
inline float decay_s(float u) { return 0.02f * fm_exp2f(u * 7.6f); }            // 20 ms .. 3.9 s (to -43 dB)
inline float ndecay_x(float u) { return fm_exp2f((u - 0.5f) * 4.0f); }          // x 1/4 .. x 4
inline float bend_st(float u) {                                                // -48 .. 48 semitones (+: down)
  const float b = 2.0f * u - 1.0f;
  return 48.0f * b * (0.3f + 0.7f * fm_fabsf(b));
}

}  // namespace

extern "C" {

MI_COLD void sds_reset(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Sds& d = sds[t];
  uint8_t* raw = reinterpret_cast<uint8_t*>(&d);
  for (uint32_t i = 0; i < sizeof(Sds); ++i) raw[i] = 0;
  d.rnd = 0x9E3779B9u * static_cast<uint32_t>(t + 1);
  d.note = 48.0f;
  d.lvl = 1.0f;
  for (int i = 0; i < 8; ++i) d.k[i] = kKits[0].def[i];
}

void sds_silence(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  sds[t].alive = 0, sds[t].trig = 0;
}

void sds_set(int t, int model, float note, const float* k) {
  if (t < 0 || t >= MF_TRACKS) return;
  Sds& d = sds[t];
  d.model = model < 0 ? 0 : model >= SDS_MODELS ? SDS_MODELS - 1 : model;
  d.note = note;
  for (int i = 0; i < 8; ++i) d.k[i] = clampf(k[i], 0.0f, 1.0f);
}

void sds_gate(int t, int g) {
  if (t < 0 || t >= MF_TRACKS) return;
  if (g && !sds[t].gate) sds[t].trig = 1;
  sds[t].gate = g;
}

void sds_render(int t, float* out, uint32_t n) {
  if (t < 0 || t >= MF_TRACKS) return;
  Sds& d = sds[t];
  const Kit& kit = kKits[d.model];
  if (d.trig) {                         // a hit: the envelopes rise from where they are (nothing jumps), the
    d.trig = 0;                         // oscillator runs on (stopped: from near its zero, rising)
    if (!d.alive) {
      d.ph = 0.1f * bip(d.rnd);
      d.ph += d.ph < 0.0f ? 1.0f : 0.0f;
      d.rc = 0.0f;
    }
    d.at = d.an = d.ab = 1;
    d.hold = static_cast<int>(kit.click_ms * 0.001f * kSr * (1.0f + 0.1f * bip(d.rnd)));
    d.hit = d.k[5];
    d.jit = 0.04f * bip(d.rnd);         // (a few cents, a few tenths of a decibel: no two hits the same)
    d.lvl = 1.0f + 0.035f * bip(d.rnd);
    d.pulse = 1.0f;
    d.alive = 1;
  }
  if (!d.alive) {
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    return;
  }
  // the drift: a new place every 0.37 s or so, glided to
  if (--d.dcount <= 0) d.dcount = 128, d.dtarget = 0.05f * bip(d.rnd);
  d.drift += (d.dtarget - d.drift) * 0.02f;
  // the block's settings
  const float h = d.hit, strike = (0.18f + 0.82f * fm_powf(h, 1.2f)) * (1.0f / 0.7614f) * d.lvl;   // (1 at 0.75)
  const float tdec = decay_s(d.k[3]), ndec = tdec * ndecay_x(d.k[4]);
  const float kt = fm_expf(-5.0f / (tdec * kSr)), kn = fm_expf(-5.0f / (ndec * kSr));
  const float kc = fm_expf(-1.0f / (0.0007f * kSr));            // (the click's end: 0.7 ms)
  const float kat = 1.0f - fm_expf(-1.0f / (0.00025f * kSr));   // (the attacks: 0.25 ms)
  const float kb = fm_expf(-1.0f / (2.4f * 0.2f * tdec * kSr) * 8.0f);   // (the bend's: 2.4 times the decay's, a chunk)
  const float depth = bend_st(d.k[7]) * (0.4f + 0.8f * h);
  const float bal = d.k[1], gt = fm_cosf(bal * 1.5707963f), gn0 = fm_sin_turns(bal * 0.25f);
  const float clk = d.k[2], gc = 2.4f * clk * clk * (0.3f + 0.933f * h);
  const float fc0 = filter_hz(d.k[0]) * fm_exp2f(2.2f * (h - 0.75f));
  const float gn = gn0 * 3.0f * fm_powf(fc0 * 0.001f, -0.3f);   // (the noise less dark for its filter: half its loss)
  const float kq = kit.reso, comp = 1.0f + 0.6f * kq;
  const float f_note = kit.f0 * fm_exp2f((d.note - 48.0f) * (1.0f / 12.0f));
  const float rcg = 1.0f - fm_expf(-6.2831853f * kit.rc / kSr);
  const float rise = kit.rise, ir = 1.0f / rise, ifall = 1.0f / (1.0f - rise);
  const float dg = 0.5f * fm_exp2f(d.k[6] * 3.6f), bias = 0.15f;   // (DRIVE: x0.5 .. x6, a little biased)
  const float sb = sat(bias), norm = 0.8f / (sat(dg * 0.8f + bias) - sb);
  float dt = 0.0f, G = 0.0f, beta = 0.0f;
  for (uint32_t i = 0; i < n; ++i) {
    if ((i & 7) == 0) {                 // (every 8 samples: the pitch, the filter's cutoff)
      d.bend = d.bend * kb;
      const float st = -depth * (1.0f - d.bend) + d.jit + d.drift;
      dt = clampf(f_note * fm_exp2f(st * (1.0f / 12.0f)) / kSr, 0.0f, 0.18f);
      const float fc = clampf(fc0 * fm_exp2f(kit.sweep * d.en), 20.0f, 16000.0f);
      const float g = fm_tanf(3.14159265f * fc / kSr);
      G = g / (1.0f + g), beta = 1.0f / (1.0f + g);
    }
    // the tone: the triangle, its RC
    float p = d.ph, tri = p < rise ? -1.0f + 2.0f * p * ir : 1.0f - 2.0f * (p - rise) * ifall;
    d.ph += dt;
    d.ph -= d.ph >= 1.0f ? 1.0f : 0.0f;
    d.rc += (tri - d.rc) * rcg;
    // the noise (and the trigger's pulse) through the ladder: its feedback solved, then saturated at its input
    float x = bip(d.rnd) + 2.5f * d.pulse;
    d.pulse *= 0.93f;                   // (0.3 ms)
    const float S = G * G * G * beta * d.s[0] + G * G * beta * d.s[1] + G * beta * d.s[2] + beta * d.s[3];
    const float G4 = G * G * G * G, y4 = (G4 * x + S) / (1.0f + kq * G4);
    float u = sat(x - kq * y4);
    for (int j = 0; j < 4; ++j) {
      const float v = (u - d.s[j]) * G, y = v + d.s[j];
      d.s[j] = y + v;
      u = y;
    }
    const float nz = u * comp;
    // the envelopes
    if (d.at) {
      d.et += (1.05f - d.et) * kat;
      if (d.et >= 1.0f) d.et = 1.0f, d.at = 0;
    } else {
      d.et *= kt;
    }
    if (d.an) {
      d.en += (1.05f - d.en) * kat;
      if (d.en >= 1.0f) d.en = 1.0f, d.an = 0;
    } else {
      d.en *= kn;
    }
    if (d.ab) {                         // (the bend back to the strike's pitch as fast)
      d.bend += (1.05f - d.bend) * kat;
      if (d.bend >= 1.0f) d.bend = 1.0f, d.ab = 0;
    }
    if (d.hold > 0) {
      --d.hold;
      d.ec += (1.0f - d.ec) * 0.5f;
    } else {
      d.ec *= kc;
    }
    // the mix, the OTA
    const float mix = strike * (gt * d.et * d.rc + gn * d.en * nz + gc * d.ec * nz);
    out[i] = (sat(dg * mix + bias) - sb) * norm;
  }
  mi_finish(out, n, 1.0f, &d.dcx, &d.dcy);
  if (!d.at && !d.an && !d.ab && d.hold <= 0 && d.et < 1e-5f && d.en < 1e-5f && d.ec < 1e-5f) d.alive = 0;
}

const char* sds_model_name(int m) { return m >= 0 && m < SDS_MODELS ? kKits[m].name : ""; }
const char* sds_knob_name(int k) { return k >= 0 && k < 8 ? kKnob[k] : ""; }
MI_COLD void sds_defaults(int m, float* k) {
  m = m < 0 ? 0 : m >= SDS_MODELS ? SDS_MODELS - 1 : m;
  for (int i = 0; i < 8; ++i) k[i] = kKits[m].def[i];
}

MI_COLD void sds_knob_text(int k, float u, char* b, int n) {   // u: 0..1 (BEND from its middle)
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  switch (k) {
    case 0: {                           // FILTER: 85Hz, 1.2k
      const float hz = filter_hz(u);
      if (hz < 999.5f) {
        put_i(b, n, &at, static_cast<int>(hz + 0.5f));
        put_s(b, n, &at, "Hz");
      } else {
        const int x = static_cast<int>(hz * 0.01f + 0.5f);
        put_i(b, n, &at, x / 10);
        put_s(b, n, &at, ".");
        put_i(b, n, &at, x % 10);
        put_s(b, n, &at, "k");
      }
      return;
    }
    case 1:                             // BALANCE: the tone, the noise
      if (u < 0.01f) return put_s(b, n, &at, "Tone");
      if (u > 0.99f) return put_s(b, n, &at, "Noise");
      put_s(b, n, &at, "N ");
      put_i(b, n, &at, static_cast<int>(u * 100.0f + 0.5f));
      put_s(b, n, &at, "%");
      return;
    case 3: return put_time(b, n, &at, decay_s(u));
    case 4: {                           // N.DECAY: x0.25 .. x4.0
      const int x = static_cast<int>(ndecay_x(u) * 100.0f + 0.5f);
      put_s(b, n, &at, "x");
      put_i(b, n, &at, x / 100);
      put_s(b, n, &at, ".");
      if (x % 100 < 10) put_s(b, n, &at, "0");
      put_i(b, n, &at, x % 100);
      return;
    }
    case 7: {                           // BEND: down (the head relaxing), up (the tabla)
      const float st = bend_st(u);
      const int x = static_cast<int>(fm_fabsf(st) + 0.5f);
      if (x == 0) return put_s(b, n, &at, "Off");
      put_s(b, n, &at, st > 0.0f ? "Dn " : "Up ");   // (Down 12 too wide in a knob)
      put_i(b, n, &at, x);
      return;
    }
    default:
      break;
  }
  put_i(b, n, &at, static_cast<int>(u * 100.0f + 0.5f));
  put_s(b, n, &at, "%");
}

}  // extern "C"
