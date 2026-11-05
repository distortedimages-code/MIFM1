// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// Portions: the SC-01's analog path (its glottal ladder, its noise, its filters from the die's capacitors) and its
// timing (the phone's ticks, the interpolations, the closure), ported from MAME's votrax.cpp, Copyright Olivier
// Galibert, BSD-3-Clause (LICENSES/BSD-3-Clause-MAME.txt).
//
// MIFM1's ROBOVOX (0.16.17): a singing speech chip, after Kraftwerk's Robovox (Florian Schneider, Gert Joachim Ott,
// Gert Jalass: "System for and method of synthesizing singing in real time", EP 0 396 141, 1990, withdrawn): a Votrax
// SC-02 a MIDI keyboard played, a phoneme a note, its pitch the 12-bit inflection, the filters' clock the vocal tract's
// length, the articulation the transitions' speed. No SC-02 is simulated anywhere; the SC-01 is (Galibert's, from its
// die): its vocal tract here, played as the SC-02 was. Its phonemes the SC-02's 64, their sounds MIFM1's own
// (robovox_tables.h: the chip's ROM is Votrax's).
//
// The tract (MAME's analog_calc, at 44.1 kHz): the glottal wave (a ladder of transistors: 9 steps of 0.4 ms, then 0
// to the period's end) times the voice's level, through F1 and F2; the noise (a 15-bit shift register, let through
// half of each period) times the noise's level through its shaper, then into F2 (fc) or after F3 (the rest); F3, F4,
// the closure (a stop: its silence, 1.4 ms down), a last low-pass; then a mouth's radiation (a first difference: the
// chip's sound alone is dull, MAME's too). Each filter as the die makes it: switched
// capacitors, their H(s) from the capacitors' ratios and the clock, made digital by the bilinear transform
// (Galibert's 3rd-order forms, here the same as biquads: their pole at -1 cancelled by a zero). The codes move toward
// a phone's as the chip moves them (an eighth of the way at each of its ticks: formants 416 a second, levels 1250),
// a code at a time: the Votrax's steps.
//
// Played: a trig sings a phoneme (PHON: each step its own, a lock), the next syllable of a word (WORD) or a babble
// (Q*bert's: Gottlieb's David Thiel put random numbers into the SC-01); a syllable's first vowel held while the gate
// is, what follows it at the release. The pitch the note's, its own (the SC-02's), gliding; the filters' clock
// TRACT's, and FOLLOW's share of the note's (all of it: the SC-01's way, the clock everything). The source: the chip's
// glottal pulse, a whisper (noise), or another track's sound (a voice that track's).
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"
#include "robovox_tables.h"

namespace {

const float kSr = 44100.0f;
const float kChip = 20000.0f;           // the chip's clock: 720 kHz / 36
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
inline uint32_t xs(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

// a biquad (direct form I): y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2
struct Bq {
  float b0, b1, b2, a1, a2, x1, x2, y1, y2;
};
inline float bq(Bq& f, float x) {
  const float y = f.b0 * x + f.b1 * f.x1 + f.b2 * f.x2 - f.a1 * f.y1 - f.a2 * f.y2;
  f.x2 = f.x1, f.x1 = x, f.y2 = f.y1, f.y1 = y;
  return y;
}
// a first order: y = b0 x + b1 x1 - a1 y1
struct Op {
  float b0, b1, a1, x1, y1;
};
inline float op(Op& f, float x) {
  const float y = f.b0 * x + f.b1 * f.x1 - f.a1 * f.y1;
  f.x1 = x, f.y1 = y;
  return y;
}

float caps(int v, const float* c, int n) {
  float t = 0.0f;
  for (int i = 0; i < n; ++i)
    if (v >> i & 1) t += c[i];
  return t;
}
// the warp of the bilinear transform at the peak fp (Hz): 2 pi fp / tan(pi fp / fs)
inline float warp(float fp) {
  fp = clampf(fp, 1.0f, 0.45f * kSr);
  return 6.2831853f * fp / fm_tanf(3.14159265f * fp / kSr);
}
// Galibert's standard filter: H(s) = (1 + k0 s) / (1 + k1 s + k2 s^2), cc the capacitors' clock
MI_COLD void std_filter(Bq& f, float cc, float c1t, float c1b, float c2t, float c2b, float c3, float c4) {
  const float k0 = c1t / (cc * c1b), k1 = c4 * c2t / (cc * c1b * c3), k2 = c4 * c2b / (cc * cc * c1b * c3);
  const float fp = fm_sqrtf(fm_fabsf(k0 * k1 - k2)) / (6.2831853f * k2);
  const float zc = warp(fp), m0 = zc * k0, m1 = zc * k1, m2 = zc * zc * k2, a0 = 1.0f / (1.0f + m1 + m2);
  f.b0 = (1.0f + m0) * a0, f.b1 = 2.0f * a0, f.b2 = (1.0f - m0) * a0;   // ((1 + z^-1)(1 + m0 + (1 - m0) z^-1))
  f.a1 = (2.0f - 2.0f * m2) * a0, f.a2 = (1.0f - m1 + m2) * a0;
}
// its noise shaper: H(s) = k0 s / (1 + k1 s + k2 s^2), a band-pass. (MAME's multiplies by the clock where its
// switched capacitors divide: its k1 a million times too big, the filter flat, the noise its level, 3.4e-5, at every
// frequency. Its circuit read again, each switched capacitor a resistor 1 / (C f): a band around 2.9 kHz, Q 0.63, the
// hiss of an S; its input set for MAME's level)
MI_COLD void shaper(Bq& f, float cc, float c1, float c2t, float c2b, float c3, float c4) {
  const float k0 = c3 / (c4 * cc), k1 = c2t / (c2b * cc), k2 = c1 * c3 / (c4 * c2b * cc * cc);
  const float fp = 1.0f / (6.2831853f * fm_sqrtf(k2));
  const float zc = warp(fp), m0 = zc * k0, m1 = zc * k1, m2 = zc * zc * k2, a0 = 1.0f / (1.0f + m1 + m2);
  f.b0 = m0 * a0, f.b1 = 0.0f, f.b2 = -m0 * a0;
  f.a1 = (2.0f - 2.0f * m2) * a0, f.a2 = (1.0f - m1 + m2) * a0;
}
// its last low-pass (the caps put it near 150 Hz; recordings near 4 kHz: Galibert's fuzz)
MI_COLD void lowpass(Op& f, float cc, float c1t, float c1b) {
  const float k = c1b / (cc * c1t) * (150.0f / 4000.0f);
  const float m = warp(1.0f / (6.2831853f * k)) * k, a0 = 1.0f / (1.0f + m);
  f.b0 = a0, f.b1 = 0.0f, f.a1 = (1.0f - m) * a0;
}
// the noise into F2: H(s) = (k0 + k2 s) / (k1 + k2 s), not prewarped
MI_COLD void injection(Op& f, float cc, float c1b, float c2t, float c2b, float c3) {
  const float k0 = cc * c2t, k1 = cc * (c1b * c3 / c2t + c2t), k2 = c2b, m = 2.0f * kSr * k2, a0 = 1.0f / (k1 + m);
  f.b0 = (k0 + m) * a0, f.b1 = (k0 - m) * a0, f.a1 = (k1 - m) * a0;
}

const float kGlottal[9] = {0.0f, -4.0f / 7.0f, 1.0f, 6.0f / 7.0f, 5.0f / 7.0f, 4.0f / 7.0f, 3.0f / 7.0f, 2.0f / 7.0f,
                           1.0f / 7.0f};
const float kF1c[4] = {2546, 4973, 9861, 19724}, kF2qc[4] = {1390, 2965, 5875, 11297},
            kF2c[5] = {833, 1663, 3164, 6327, 12654}, kF3c[4] = {2226, 4485, 9056, 18111};

enum { M_PHON, M_WORD, M_BABBLE };
const int kQ = 14;                      // a syllable's phonemes, at most

struct Vox {
  Bq f1, f2v, f3, f4, fn;               // the tract
  Op f2n, fx;
  float k[8], note, f0, f0_to, cc, cc_was;   // the knobs; the pitch (Hz), gliding; the filters' clock, as made
  float ph, per, gtick, chip, prate, part;   // the glottal phase (chip ticks), its period; the chip's clocks' phases
  float em;                             // (the radiation's last sample)
  uint8_t phone, ticks, phonetick, upd; // the phone sung, its ticks (16 a phone), its sub-ticks; the update counter
  uint8_t cur_f1, cur_f2, cur_f2q, cur_f3, cur_fa, cur_fc, cur_va;   // the chip's registers (x16, interpolated)
  uint8_t filt_f1, filt_f2, filt_f2q, filt_f3, filt_fa, filt_fc, filt_va;   // their codes (the filters made of them)
  uint8_t closure, cur_closure, cur_noise, gate, alive, dirty;
  uint16_t noise;
  uint8_t q[kQ], qn, qi, nuc, held;     // the syllable: its phonemes, how many, the one sung, its nucleus, held
  uint8_t mode, word, syl, quiet;       // the word sung, its syllable; (silent blocks)
  uint8_t cn, cpos;                     // the carrier's samples this block, the next read
  uint32_t rnd, seed;
};
static_assert(sizeof(Vox) <= MF_STATE_BYTES, "ROBOVOX's state");
struct State {
  Vox& operator[](int t) const { return *static_cast<Vox*>(mf_state(t)); }
} vox;

inline int mode_of(float u) {
  const int m = static_cast<int>(u * 3.0f);
  return m < 0 ? 0 : m > 2 ? 2 : m;
}
inline int pick(float u, int n) {
  const int i = static_cast<int>(u * static_cast<float>(n));
  return i < 0 ? 0 : i >= n ? n - 1 : i;
}
inline int is_vowel(int p) { return kVoxVowel[p >> 3] >> (p & 7) & 1; }
inline float rate_of(float u) { return fm_exp2f(2.0f * u - 1.0f); }        // RATE: x0.5 .. x2
inline float artic_of(float u) { return fm_exp2f(4.0f * u - 2.0f); }       // ARTIC: x0.25 .. x4
inline float tract_of(float u) { return 2.0f * u - 1.0f; }                 // TRACT: -1 .. +1 octave
inline float glide_s(float u) { return u < 0.02f ? 0.0f : 0.6f * u * u; } // GLIDE: 0 .. 0.6 s

// the filters of the codes, the capacitors' clock cc (all: the clock or a code moved)
void commit(Vox& v, int all) {
  v.filt_fa = v.cur_fa >> 4, v.filt_fc = v.cur_fc >> 4, v.filt_va = v.cur_va >> 4;
  const float cc = v.cc;
  if (all || v.filt_f1 != v.cur_f1 >> 4) {
    v.filt_f1 = v.cur_f1 >> 4;
    std_filter(v.f1, cc, 11247, 11797, 949, 52067, 2280 + caps(v.filt_f1, kF1c, 4), 166272);
  }
  if (all || v.filt_f2 != v.cur_f2 >> 3 || v.filt_f2q != v.cur_f2q >> 4) {
    v.filt_f2 = v.cur_f2 >> 3, v.filt_f2q = v.cur_f2q >> 4;
    const float q = 829 + caps(v.filt_f2q, kF2qc, 4), c = 2352 + caps(v.filt_f2, kF2c, 5);
    std_filter(v.f2v, cc, 24840, 29154, q, 38180, c, 34270);
    injection(v.f2n, cc, 29154, q, 38180, c);
  }
  if (all || v.filt_f3 != v.cur_f3 >> 4) {
    v.filt_f3 = v.cur_f3 >> 4;
    std_filter(v.f3, cc, 0, 17594, 868, 18828, 8480 + caps(v.filt_f3, kF3c, 4), 50019);
  }
  if (all) {
    std_filter(v.f4, cc, 0, 28810, 1165, 21457, 8558, 7289);
    lowpass(v.fx, cc, 1122, 23131);
    shaper(v.fn, cc, 15500, 14854, 8450, 9523, 14083);
    v.cc_was = cc;
  }
}

// a phone to the chip: its ROM's values (here, ours), its two counters from 0
void phone_commit(Vox& v, int p) {
  v.phone = static_cast<uint8_t>(p & 63);
  v.ticks = 0, v.phonetick = 0;
  if (kVoxPhon[v.phone].vd == 0) v.cur_closure = kVoxPhon[v.phone].cl;
}
inline void interp(uint8_t& reg, int target) {   // an eighth of the way: reg - reg / 8 + target x 2 (to target x 16)
  reg = static_cast<uint8_t>(reg - (reg >> 3) + (target << 1));
}

// the syllable: its next phone (the nucleus held while the gate is), PA after its last
void next_phone(Vox& v) {
  if (v.qi + 1 < v.qn) {
    ++v.qi;
    phone_commit(v, v.q[v.qi]);
  } else if (v.phone != 0) {
    v.qi = v.qn;
    phone_commit(v, 0);
  }
}

// the chip's tick (20 kHz): the phone's ticks (at RATE), the interpolations (at ARTIC), the closure, the noise
void chip_tick(Vox& v) {
  const VoxPhon& r = kVoxPhon[v.phone];
  v.prate += rate_of(v.k[2]);
  while (v.prate >= 1.0f) {
    v.prate -= 1.0f;
    if (v.ticks != 0x10) {
      if (++v.phonetick == ((r.dur << 2) | 1)) {
        v.phonetick = 0;
        ++v.ticks;
        if (v.ticks == r.vd) v.cur_closure = r.cl;
        if (v.ticks == 0x10 && !(v.held && v.qi == v.nuc && v.gate)) next_phone(v);
      }
    }
  }
  v.part += artic_of(v.k[3]);
  while (v.part >= 1.0f) {
    v.part -= 1.0f;
    if (++v.upd == 0x30) v.upd = 0;
    const int t625 = !(v.upd & 0xF), t208 = v.upd == 0x28;
    const VoxPhon& c = kVoxPhon[v.phone];
    if (t208 && (!c.pause || !(v.filt_fa || v.filt_va))) {
      interp(v.cur_f1, c.f1);
      interp(v.cur_f2, c.f2);
      interp(v.cur_f2q, c.f2q);
      interp(v.cur_f3, c.f3);
      v.dirty = 1;
    }
    if (t625) {
      if (v.ticks >= c.vd) interp(v.cur_fa, c.fa), interp(v.cur_fc, c.fc);
      if (v.ticks >= c.cld) interp(v.cur_va, c.va);
      v.dirty = 1;
    }
  }
  if (!v.cur_closure && (v.filt_fa || v.filt_va)) v.closure = 0;
  else if (v.closure != 7 << 2) ++v.closure;
  const int inp = v.cur_noise && v.noise != 0x7FFF;
  v.noise = static_cast<uint16_t>(((v.noise << 1) & 0x7FFE) | inp);
  v.cur_noise = !(((v.noise >> 14) ^ (v.noise >> 13)) & 1);
}

// a babble: 2 to 5 phonemes, any but the pause, its last vowel the nucleus (seed 0: another each trig)
void babble(Vox& v, uint32_t seed) {
  uint32_t s = seed ? seed * 2654435761u + 0x9E3779B9u : xs(v.rnd);
  xs(s);
  const int n = 2 + static_cast<int>(xs(s) % 4u);
  v.qn = 0, v.nuc = 0;
  for (int i = 0; i < n; ++i) {
    const int p = 1 + static_cast<int>(xs(s) % 63u);
    if (is_vowel(p)) v.nuc = static_cast<uint8_t>(v.qn);
    v.q[v.qn++] = static_cast<uint8_t>(p);
  }
}
// a word's syllable s: its phonemes; returns how many syllables the word has
int syllable(Vox& v, int w, int s) {
  const int a = kVoxWordAt[w], b = kVoxWordAt[w + 1];
  int n = 0, at = -1;
  for (int i = a; i < b; ++i)
    if (kVoxWordPh[i] & 0x80) {
      if (n == s) at = i;
      ++n;
    }
  if (at < 0) return n;
  v.qn = 0, v.nuc = 0;
  for (int i = at; i < b && v.qn < kQ; ++i) {
    if (i > at && (kVoxWordPh[i] & 0x80)) break;
    if (kVoxWordPh[i] & 0x40) v.nuc = v.qn;
    v.q[v.qn++] = kVoxWordPh[i] & 0x3F;
  }
  return n;
}

// a trig: its syllable, its first phone now (from where the chip is: the formants glide on); the last syllable's
// end not sung yet (a trig before its release, legato): sung first, at its pace
void start(Vox& v) {
  uint8_t carry[6];
  int nc = 0;
  if (v.qn && v.qi < v.qn)
    for (int i = v.qi <= v.nuc ? v.nuc + 1 : v.qi + 1; i < v.qn && nc < 6; ++i) carry[nc++] = v.q[i];
  const int m = mode_of(v.k[0]);
  if (m == M_PHON) {
    v.q[0] = static_cast<uint8_t>(pick(v.k[1], 64)), v.qn = 1, v.nuc = 0;
  } else if (m == M_WORD) {
    const int w = pick(v.k[1], kVoxWords);
    if (w != v.word || v.mode != M_WORD) v.word = static_cast<uint8_t>(w), v.syl = 0;
    int n = syllable(v, w, v.syl);
    if (v.syl >= n) v.syl = 0, n = syllable(v, w, 0);
    v.syl = static_cast<uint8_t>(v.syl + 1 >= n ? 0 : v.syl + 1);
  } else {
    babble(v, static_cast<uint32_t>(pick(v.k[1], 33)));
  }
  v.mode = static_cast<uint8_t>(m);
  v.held = 1;
  if (m == M_PHON && kVoxPhon[v.q[0]].cl) v.held = 0;   // (a stop alone: its closure, then its burst; not held shut)
  if (nc) {
    const int n = v.qn + nc > kQ ? kQ - nc : v.qn;
    for (int i = n - 1; i >= 0; --i) v.q[i + nc] = v.q[i];
    for (int i = 0; i < nc; ++i) v.q[i] = carry[i];
    v.qn = static_cast<uint8_t>(n + nc), v.nuc = static_cast<uint8_t>(v.nuc + nc);
  }
  v.qi = 0;
  phone_commit(v, v.q[0]);
}

// the filters' clock: TRACT's octaves and FOLLOW's share of the note (around A2, the SC-01's own pitch)
inline float tract_cc(const Vox& v) {
  const float o = tract_of(v.k[4]) + v.k[5] * (v.note - 45.0f) * (1.0f / 12.0f);
  return kChip * fm_exp2f(clampf(o, -1.5f, 1.4f));
}

MI_COLD void vox_init_one(Vox& v) {
  uint8_t* raw = reinterpret_cast<uint8_t*>(&v);
  for (uint32_t i = 0; i < sizeof(Vox); ++i) raw[i] = 0;
  vox_defaults(v.k);
  v.rnd = 0x7F4A7C15u;
  v.note = 45.0f, v.f0 = v.f0_to = 110.0f, v.per = kChip / 110.0f;
  v.cc = kChip;
  v.phone = 0, v.qi = v.qn = 0;
  v.cur_closure = 1, v.closure = 28;
  v.word = 0xFF;
  commit(v, 1);
}

}  // namespace

extern "C" {

void vox_reset(int t) { vox_init_one(vox[t]); }

void vox_silence(int t) {
  Vox& v = vox[t];
  v.gate = 0, v.alive = 0;
  v.qi = v.qn = 0;
  phone_commit(v, 0);
  v.cur_fa = v.cur_va = 0, v.filt_fa = v.filt_va = 0;
  v.cur_closure = 1, v.closure = 28;
  v.f1.x1 = v.f1.x2 = v.f1.y1 = v.f1.y2 = 0.0f;
  v.f2v.x1 = v.f2v.x2 = v.f2v.y1 = v.f2v.y2 = 0.0f;
  v.f3.x1 = v.f3.x2 = v.f3.y1 = v.f3.y2 = 0.0f;
  v.f4.x1 = v.f4.x2 = v.f4.y1 = v.f4.y2 = 0.0f;
  v.fn.x1 = v.fn.x2 = v.fn.y1 = v.fn.y2 = 0.0f;
  v.f2n.x1 = v.f2n.y1 = v.fx.x1 = v.fx.y1 = 0.0f;
  v.em = 0.0f;
}

void vox_set(int t, float note, const float* k) {
  Vox& v = vox[t];
  for (int i = 0; i < 8; ++i) v.k[i] = clampf(k[i], 0.0f, 1.0f);
  v.note = note;
  v.f0_to = 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f));
  if (glide_s(v.k[7]) <= 0.0f || !v.alive) v.f0 = v.f0_to;
  v.cc = tract_cc(v);
}

void vox_gate(int t, int g) {
  Vox& v = vox[t];
  if (g && !v.gate) {
    start(v);
    v.alive = 1, v.quiet = 0;
  } else if (!g && v.gate && v.held) {  // the release: from the nucleus on (if it holds there now)
    v.held = 0;
    if (v.qi == v.nuc && v.ticks == 0x10) next_phone(v);
  }
  v.gate = static_cast<uint8_t>(g ? 1 : 0);
}

// the carrier (SOURCE on a track): that track's last block, n samples (none: x null), kept in the track's Plaits
// voice's memory (lent while ROBOVOX plays there)
static float* carrier_buf(int t) {
  uint32_t bytes = 0, epoch = 0;
  float* b = static_cast<float*>(mf_voice_mem(t, &bytes, &epoch));
  return b && bytes >= 128 * sizeof(float) ? b : nullptr;
}
void vox_carrier(int t, const float* x, uint32_t n) {
  Vox& v = vox[t];
  float* b = x ? carrier_buf(t) : nullptr;
  n = n > 128 ? 128 : n;
  v.cpos = 0, v.cn = 0;
  if (!b) return;
  for (uint32_t i = 0; i < n; ++i) b[i] = x[i];
  v.cn = static_cast<uint8_t>(n);
}
int vox_carrier_track(int t) {          // the track SOURCE takes its sound from, -1 none (the chip's, a whisper)
  const float u = vox[t].k[6];
  if (u < 0.6f) return -1;
  const int c = static_cast<int>((u - 0.6f) * 10.0f);
  return c < 0 ? 0 : c > 3 ? 3 : c;
}

void vox_render(int t, float* out, uint32_t n) {
  Vox& v = vox[t];
  if (!v.alive) {
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    return;
  }
  const float src = v.k[6], whisper = src < 0.6f ? src / 0.6f : 0.0f;
  const float* cb = vox_carrier_track(t) >= 0 && v.cn ? carrier_buf(t) : nullptr;
  // the pitch: gliding (log), the period in the chip's ticks (FOLLOW: the clock with the note, as the SC-01's)
  const float gs = glide_s(v.k[7]);
  const float follow = fm_exp2f(clampf(v.k[5] * (v.note - 45.0f) * (1.0f / 12.0f), -1.5f, 1.4f));
  const float tick = kChip * follow / kSr;   // (the glottal ladder's ticks a sample)
  if (gs > 0.0f) {
    const float a = 1.0f - fm_expf(-static_cast<float>(n) / (gs * kSr));
    v.f0 *= fm_exp2f(a * fm_log2f(v.f0_to / v.f0));
  } else {
    v.f0 = v.f0_to;
  }
  v.per = kChip * follow / clampf(v.f0, 20.0f, 2000.0f);
  const float nscale = 224.0f / v.per;  // (the noise's gate: bit 6 of the SC-01's own period, whatever the pitch)
  if (fm_fabsf(v.cc - v.cc_was) > 0.001f * v.cc_was) commit(v, 1);
  const float cstep = kChip / kSr;
  for (uint32_t i = 0; i < n; ++i) {
    v.chip += cstep;
    while (v.chip >= 1.0f) {
      v.chip -= 1.0f;
      chip_tick(v);
    }
    v.ph += tick;
    if (v.ph >= v.per) v.ph -= v.per;
    const int pt = static_cast<int>(v.ph);
    if (v.dirty && ((pt & 0xF8) == 0x08 || !v.filt_va)) {   // (the filters made again on the pulse's step 1, as the
      v.dirty = 0;                                          // chip does; at once when unvoiced)
      commit(v, 0);
    }
    // the voice: the glottal ladder, a whisper, or the carrier
    float g = pt < 72 ? kGlottal[pt >> 3] : 0.0f;
    const float nz = v.cur_noise ? 1.0f : -1.0f;
    if (whisper > 0.0f) g += whisper * (0.8f * nz - g);
    if (cb) {
      g = 2.0f * cb[v.cpos];
      if (v.cpos + 1 < v.cn) ++v.cpos;
    }
    float x = g * static_cast<float>(v.filt_va) * (1.0f / 15.0f);
    x = bq(v.f1, x);
    x = bq(v.f2v, x);
    // the noise: half of each period (the SC-01's bit 6), its level, the shaper, into F2 or after F3. (MAME's holds
    // it at -1 the other half: a square at the pitch its flat shaper lets through, a buzz under every S; here it rests
    // at 0, the shaper's DC gone as the die's must take it)
    const int nb = static_cast<int>(v.ph * nscale) & 0x40;
    float ns = nb ? 0.67f * (v.cur_noise ? 1.0f : -1.0f) * static_cast<float>(v.filt_fa) * (1.0f / 15.0f) : 0.0f;
    ns = bq(v.fn, ns);
    const float n2 = op(v.f2n, ns * static_cast<float>(v.filt_fc) * (1.0f / 15.0f));
    float y = bq(v.f3, x + n2);
    y += ns * static_cast<float>(15 ^ v.filt_fc) * (1.0f / 15.0f);
    y = bq(v.f4, y);
    y *= static_cast<float>(7 ^ (v.closure >> 2)) * (1.0f / 7.0f);
    y = op(v.fx, y);
    const float e = y - 0.9f * v.em;    // (a mouth's radiation, +6 dB an octave: the chip's own sound is dull, its
    v.em = y;                           // consonants lost; through it a recognizer heard 16 words of 46, not 5)
    out[i] = 2.6f * e;
  }
  // rests: the pause sung, the levels down, a few blocks quiet
  if (!v.gate && v.phone == 0 && !v.filt_va && !v.filt_fa && v.closure == 28) {
    if (++v.quiet > 8) v.alive = 0;
  } else {
    v.quiet = 0;
  }
}

int vox_voices(int t) { return vox[t].alive ? 1 : 0; }

// (the screen) the phoneme sung (-1: silent), the word and the syllable sung next (-1: not a word), F1's and F2's
// codes now (0..16, 0..32, between two as the chip moves them), the levels (0..1: voice, noise)
int vox_view(int t, int* word, int* syl, float* f1, float* f2, float* va, float* fa) {
  const Vox& v = vox[t];
  if (word) *word = v.mode == M_WORD && v.word != 0xFF ? v.word : -1;
  if (syl) *syl = v.syl;
  if (f1) *f1 = static_cast<float>(v.cur_f1) * (1.0f / 16.0f);
  if (f2) *f2 = static_cast<float>(v.cur_f2) * (1.0f / 8.0f);
  if (va) *va = static_cast<float>(v.cur_va) * (1.0f / 255.0f);
  if (fa) *fa = static_cast<float>(v.cur_fa) * (1.0f / 255.0f);
  return v.alive ? v.phone : -1;
}

const char* vox_phon_name(int p) { return p >= 0 && p < 64 ? kVoxPhonName[p] : ""; }
int vox_words(void) { return kVoxWords; }
const char* vox_word_name(int w) { return w >= 0 && w < kVoxWords ? kVoxWordName[w] : ""; }
int vox_syllables(int w) {
  if (w < 0 || w >= kVoxWords) return 0;
  int n = 0;
  for (int i = kVoxWordAt[w]; i < kVoxWordAt[w + 1]; ++i) n += kVoxWordPh[i] >> 7;
  return n;
}

const char* vox_knob_name(int k) {
  static const char* const kN[8] = {"Mode", "Say", "Rate", "Artic", "Tract", "Follow", "Source", "Glide"};
  return k >= 0 && k < 8 ? kN[k] : "";
}

void vox_defaults(float* k) {           // a word (ROBOT), the chip's pace, its formants a little slower (clearer), its
                                        // tract, its pulse, no glide
  static const float kD[8] = {0.5f, 11.5f / static_cast<float>(kVoxWords), 0.5f, 0.35f, 0.5f, 0.0f, 0.0f, 0.0f};
  for (int i = 0; i < 8; ++i) k[i] = kD[i];
}

int vox_knob_steps(int k) { return k == 0 ? 3 : 128; }

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
static void put_x(char* b, int n, int* at, float x) {   // x1.5
  const int c = static_cast<int>(x * 100.0f + 0.5f);
  put_s(b, n, at, "x");
  put_i(b, n, at, c / 100);
  put_s(b, n, at, ".");
  put_i(b, n, at, (c / 10) % 10);
  if (c % 10) put_i(b, n, at, c % 10);
}

MI_COLD void vox_knob_text(int k, float u, float mode, char* b, int n) {
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  switch (k) {
    case 0: {
      static const char* const kM[3] = {"Phon", "Word", "Babble"};
      return put_s(b, n, &at, kM[mode_of(u)]);
    }
    case 1: {                           // SAY: a phoneme (its name, an example under it), a word, a babble
      const int m = mode_of(mode);
      if (m == M_PHON) {
        const int p = pick(u, 64);
        put_s(b, n, &at, kVoxPhonName[p]);
        put_s(b, n, &at, " ");
        static const char* const kEx[64] = {
            "pause", "mEEt", "bEnt", "bEfore", "Year", "plEAse", "anY", "sIx", "mAde", "cAre", "nEst", "bElt", "dAd",
            "After", "gOt", "fAther", "Office", "stOre", "bOAt", "lOOk", "yOU", "cOUld", "tUne", "cartOOn", "wOnder",
            "lOve", "whAt", "nUt", "bIRd", "Roof", "Rug", "mutteR", "Lift", "pLay", "falL", "Water", "Bag", "paiD",
            "taG", "Pen", "Tart", "Kit", "hold", "hold.", "Heart", "Heart.", "hum", "Zero", "Same", "meaSure", "SHip",
            "Very", "Four", "THere", "wiTH", "More", "NiNe", "raNG", "mArchen", "lOwe", "fUnf", "menU", "bittE",
            "Lube"};
        return put_s(b, n, &at, kEx[p]);
      }
      if (m == M_WORD) return put_s(b, n, &at, kVoxWordName[pick(u, kVoxWords)]);
      const int s = pick(u, 33);
      if (!s) return put_s(b, n, &at, "Random");
      put_s(b, n, &at, "#");
      return put_i(b, n, &at, s);
    }
    case 2: return put_x(b, n, &at, rate_of(u));
    case 3: return put_x(b, n, &at, artic_of(u));
    case 4: {                           // TRACT: semitones
      const int s = static_cast<int>(tract_of(u) * 12.0f + (u < 0.5f ? -0.5f : 0.5f));
      if (s > 0) put_s(b, n, &at, "+");
      put_i(b, n, &at, s);
      return put_s(b, n, &at, " st");
    }
    case 6: {                           // SOURCE: the pulse, a whisper, a track's sound
      if (u < 0.6f) {
        if (u < 0.01f) return put_s(b, n, &at, "Pulse");
        if (u > 0.59f) return put_s(b, n, &at, "Whisper");
        put_i(b, n, &at, static_cast<int>(u / 0.6f * 100.0f + 0.5f));
        return put_s(b, n, &at, "% Whisper");
      }
      int c = static_cast<int>((u - 0.6f) * 10.0f);
      c = c < 0 ? 0 : c > 3 ? 3 : c;
      put_s(b, n, &at, "Track ");
      return put_i(b, n, &at, c + 1);
    }
    case 7: {
      const float s = glide_s(u);
      if (s <= 0.0f) return put_s(b, n, &at, "Off");
      put_i(b, n, &at, static_cast<int>(s * 1000.0f + 0.5f));
      return put_s(b, n, &at, " ms");
    }
    default:
      break;
  }
  put_i(b, n, &at, static_cast<int>(u * 100.0f + 0.5f));
  put_s(b, n, &at, "%");
}

}  // extern "C"
