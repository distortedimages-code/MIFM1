// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's PROPHET (0.13): a five-voice classic, after Sequential Circuits' Prophet-5 (1978; its third revision, 1980,
// on Curtis' chips), four voices a track here. Each voice two oscillators (A: a saw and a pulse; B: a saw, a pulse or a
// triangle, an interval away), noise, Curtis' four-pole low pass, two envelopes (the filter's, the level's), and its
// POLY-MOD: the filter's envelope on A's pitch (to the left: with A synced to B, the sync's sweep), or B on A's
// frequency (to the right: linear FM, bells and metal).
//
// OSC: thirteen settings of the oscillators (the Prophet's panel, its classic ones: two saws, a saw and a square, two
// pulses, a fifth, an octave, a sub, hollow, thin, synced, a bell, a breath, noise). Every edge's alias taken off: the
// steps by a cubic B-spline's polyBLEP, the triangle's corners by its polyBLAMP (both four samples wide, two late), A's
// sync too (its step where B wraps). VINTAGE (0.13; after the 2020 revision's knob, which brings back the first ones'
// drift): from exact voices to the old ones' (A and B a few cents apart, each voice its own tuning, its own slow
// wander, its filter and its envelopes a little apart). ENV: to the right the filter's envelope (its depth: a pluck),
// to the left its depth with an attack (brass, then pads: the filter opening slower than the level rises). DECAY: both
// envelopes' fall (Hold: held while the gate is, the filter's settling to a third). CHORD: POLY's (a chord memory, or
// the harmony: PADS', the scale's).
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f;
const int kVoices = 4, kSub = 16;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }
inline uint32_t xs(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline float bip(uint32_t& s) { return static_cast<float>(xs(s) >> 8) * (2.0f / 16777216.0f) - 1.0f; }

// OSC: the oscillators' settings (each wave's level: +/- that; widths; B's interval, A's over B when synced)
struct OscProg {
  const char* name;
  float asaw, apul, apw;
  float bsaw, bpul, btri, bpw, bint;    // (B's interval over the note, semitones)
  float aint, noise;                    // (A over B when synced, semitones; the noise's level)
  uint8_t sync;
};
const int kProgs = 13;
const OscProg kOsc[kProgs] = {
    {"Saws", 0.5f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0},
    {"Saw+Sq", 0.5f, 0.0f, 0.5f, 0.0f, 0.42f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0},
    {"Pulses", 0.0f, 0.5f, 0.3f, 0.0f, 0.42f, 0.0f, 0.45f, 0.0f, 0.0f, 0.0f, 0},
    {"Fifth", 0.5f, 0.0f, 0.5f, 0.4f, 0.0f, 0.0f, 0.5f, 7.0f, 0.0f, 0.0f, 0},
    {"Octave", 0.5f, 0.0f, 0.5f, 0.35f, 0.0f, 0.0f, 0.5f, 12.0f, 0.0f, 0.0f, 0},
    {"Sub", 0.5f, 0.0f, 0.5f, 0.0f, 0.45f, 0.0f, 0.5f, -12.0f, 0.0f, 0.0f, 0},
    {"Hollow", 0.0f, 0.4f, 0.5f, 0.0f, 0.0f, 0.55f, 0.5f, 12.0f, 0.0f, 0.0f, 0},
    {"Thin", 0.0f, 0.55f, 0.12f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0},
    {"Sync", 0.55f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 7.0f, 0.0f, 1},
    {"SyncSq", 0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 12.0f, 0.0f, 1},
    {"Bell", 0.0f, 0.45f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 19.0f, 0.0f, 0.0f, 0},
    {"Breath", 0.45f, 0.0f, 0.5f, 0.35f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.22f, 0},
    {"Noise", 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.7f, 0},
};
inline int prog_of(float u) {
  int i = static_cast<int>(u * static_cast<float>(kProgs));
  return i < 0 ? 0 : i > kProgs - 1 ? kProgs - 1 : i;
}

struct XVoice {
  float note;
  float pa, pb;                         // A's phase, B's (0..1)
  float p1, p2, carry;                  // the two samples before, the next one's part (an edge's: two late)
  float bprev;                          // B's last value (its FM on A)
  float s1, fenv, s2, aenv, s3, fa0, s4, aa0;   // the filter's states; the envelopes: the filter's, the level's, where
                                        // their attacks began (apart: the compiler would pack the states)
  float fatt, aatt, fdec, adec;         // their attacks' progress, their falls
  float dr, dr_t;                       // its wander (VINTAGE)
  float tol[4];                         // its tolerances (-1..1): A's pitch, B's, the cutoff, the envelopes
  uint32_t nz;                          // its noise
  int slot, alive, trig, let_go;
  int key;                              // (0.16.15) the key it plays (CHORD Off, the keyboard), -1 a trig's
  uint32_t age;
};
struct Pro {
  float k[8];
  float notes[kVoices];
  int n, gate, trig, group;
  XVoice v[kVoices];
  uint32_t rnd, clock;
  int dr_n;
  float dcx, dcy;
  float gn;                             // (0.16.5) the voices' level now: 0.62 / sqrt(those sounding), glided
};
// (its state on a track: the engine's, shared with the other machines, one a track at a time)
static_assert(sizeof(Pro) <= MF_STATE_BYTES, "PROPHET's state");
struct ProAt {
  Pro& operator[](int t) const { return *static_cast<Pro*>(mf_state(t)); }
};
const ProAt pro = {};

inline float t60_s(float u) { return 0.05f * fm_exp2f(u * 8.644f); }   // DECAY: 50 ms .. 20 s (to -60 dB)
inline float cutoff_hz(float u) { return 20.0f * fm_exp2f(u * 10.0f); }  // CUTOFF: 20 Hz .. 20 kHz
inline float reso_k(float u) { return 4.2f * u * u * (3.0f - 2.0f * u); }   // RESO: past 4, it sings by itself

// an edge of J (a step) t samples ago (0..1): its band-limited step a cubic B-spline's integral, four samples wide
inline float bs_rise(float x) {         // the B-spline's integral at x (-1..0)
  return (1.0f / 24.0f) + (4.0f * x - 2.0f * x * x * x - 0.75f * x * x * x * x + 2.75f) * (1.0f / 6.0f);
}
inline void edge(float J, float t, float& p2, float& p1, float& cur, float& carry) {
  float t2 = t * t, u = 1.0f - t, u2 = u * u;
  p2 += J * t2 * t2 * (1.0f / 24.0f);
  p1 += J * bs_rise(t - 1.0f);
  cur -= J * bs_rise(-t);
  carry -= J * u2 * u2 * (1.0f / 24.0f);
}
// a corner (the slope changing by D a sample) t samples ago: the integral of the same (its residual R(-|x|))
inline float bs_ramp(float x) {         // R at x (-1..0)
  return (1.0f / 120.0f) + (x + 1.0f) * (1.0f / 24.0f) +
         (2.0f * x * x - 0.5f * x * x * x * x - 0.15f * x * x * x * x * x + 2.75f * x + 1.1f) * (1.0f / 6.0f);
}
inline void corner(float D, float t, float& p2, float& p1, float& cur, float& carry) {
  float t2 = t * t, u = 1.0f - t, u2 = u * u;
  p2 += D * t2 * t2 * t * (1.0f / 120.0f);
  p1 += D * bs_ramp(t - 1.0f);
  cur += D * bs_ramp(-t);
  carry += D * u2 * u2 * u * (1.0f / 120.0f);
}

}  // namespace

extern "C" {

void pro_reset(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Pro& p = pro[t];
  uint8_t* raw = reinterpret_cast<uint8_t*>(&p);
  for (uint32_t i = 0; i < sizeof(Pro); ++i) raw[i] = 0;
  p.rnd = 0x7F4A7C15u * static_cast<uint32_t>(t + 5) | 1u;
  float d[8];
  pro_defaults(d);
  for (int i = 0; i < 8; ++i) p.k[i] = d[i];
  for (int i = 0; i < kVoices; ++i) {
    XVoice& v = p.v[i];
    v.note = 48.0f;
    v.fatt = v.aatt = 1.0f;
    for (int j = 0; j < 4; ++j) v.tol[j] = bip(p.rnd);   // (each voice its own, for good)
    v.key = -1;
    v.nz = xs(p.rnd) | 1u;
  }
}

void pro_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) pro_reset(t);
  mf_forget();
}

void pro_silence(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Pro& p = pro[t];
  p.trig = 0;
  p.group = 0;
  for (int i = 0; i < kVoices; ++i) {
    XVoice& v = p.v[i];
    v.alive = 0, v.trig = 0, v.fenv = v.aenv = 0.0f, v.fatt = v.aatt = 1.0f;
  }
}

void pro_set(int t, const float* notes, int n, const float* k) {
  if (t < 0 || t >= MF_TRACKS) return;
  Pro& p = pro[t];
  for (int i = 0; i < 8; ++i) p.k[i] = clampf(k[i], 0.0f, 1.0f);
  n = n < 1 ? 1 : n > kVoices ? kVoices : n;
  if (n == p.n && p.group)              // (the voices this trig took follow the notes: a knob, a lock, the progression)
    for (int i = 0; i < kVoices; ++i)
      if ((p.group >> i) & 1) p.v[i].note = notes[p.v[i].slot < n ? p.v[i].slot : n - 1];
  for (int i = 0; i < n; ++i) p.notes[i] = notes[i];
  p.n = n;
}

void pro_gate(int t, int g) {
  if (t < 0 || t >= MF_TRACKS) return;
  Pro& p = pro[t];
  if (g && !p.gate) p.trig = 1;
  if (!g && p.gate && p.k[3] > 0.98f)   // let go: the held voices fall (the others fall anyway)
    for (int i = 0; i < kVoices; ++i) {
      XVoice& v = p.v[i];
      if (((p.group >> i) & 1) && v.alive) v.let_go = 1, v.fdec = v.fenv, v.adec = v.aenv, v.fatt = v.aatt = 1.0f;
    }
  p.gate = g;
}

// (0.16.15) the keys held (CHORD Off): a voice each, as POLY's (poly_keys)
void pro_keys(int t, const int* ids, const float* notes, int n) {
  if (t < 0 || t >= MF_TRACKS) return;
  Pro& p = pro[t];
  n = n < 0 ? 0 : n > kVoices ? kVoices : n;
  for (int i = 0; i < kVoices; ++i) {
    XVoice& v = p.v[i];
    if (v.key < 0) continue;
    int j = 0;
    while (j < n && ids[j] != v.key) ++j;
    if (j < n && v.alive) {
      v.note = notes[j];
      continue;
    }
    if (v.alive && !v.let_go && p.k[3] > 0.98f) v.let_go = 1, v.fdec = v.fenv, v.adec = v.aenv, v.fatt = v.aatt = 1.0f;
    v.key = -1;
  }
  for (int j = 0; j < n; ++j) {
    int have = 0;
    for (int i = 0; i < kVoices; ++i) have |= p.v[i].key == ids[j];
    if (have) continue;
    int best = 0;
    float bs = 1e9f;
    for (int i = 0; i < kVoices; ++i) {
      const XVoice& v = p.v[i];
      float score = v.alive ? v.aenv + 1.0f - static_cast<float>(p.clock - v.age) * 1e-6f : 0.0f;
      if (v.key >= 0) score += 2.0f;
      if (score < bs) bs = score, best = i;
    }
    XVoice& v = p.v[best];
    p.group &= ~(1 << best);
    if (!v.alive) {
      v.pa = 0.5f + 0.5f * bip(p.rnd);
      v.pb = 0.5f + 0.5f * bip(p.rnd);
      v.p1 = v.p2 = v.carry = v.bprev = 0.0f;
      v.s1 = v.s2 = v.s3 = v.s4 = 0.0f;
      v.fenv = v.aenv = 0.0f;
    }
    v.note = notes[j];
    v.slot = 0;
    v.key = ids[j];
    v.trig = 1;
    v.age = ++p.clock;
  }
}

int pro_voices(int t) {
  if (t < 0 || t >= MF_TRACKS) return 0;
  int n = 0;
  for (int i = 0; i < kVoices; ++i) n += pro[t].v[i].alive;
  return n;
}

}  // extern "C"

namespace {

void take(Pro& p) {                     // a trig's notes to voices: the free first, then the quietest, the oldest
  int used = 0;
  ++p.clock;
  for (int j = 0; j < p.n; ++j) {
    int best = 0;
    float bs = 1e9f;
    for (int i = 0; i < kVoices; ++i) {
      if ((used >> i) & 1) continue;
      const XVoice& v = p.v[i];
      float score = v.alive ? v.aenv + 1.0f - static_cast<float>(p.clock - v.age) * 1e-6f : 0.0f;
      if (v.alive && v.key >= 0) score += 2.0f;   // (a key held: the last taken)
      if (score < bs) bs = score, best = i;
    }
    used |= 1 << best;
    XVoice& v = p.v[best];
    v.slot = j;
    v.note = p.notes[j];
    if (!v.alive) {                     // from silence: at rest, its phases anywhere (no two voices in phase)
      v.pa = 0.5f + 0.5f * bip(p.rnd);
      v.pb = 0.5f + 0.5f * bip(p.rnd);
      v.p1 = v.p2 = v.carry = v.bprev = 0.0f;
      v.s1 = v.s2 = v.s3 = v.s4 = 0.0f;
      v.fenv = v.aenv = 0.0f;
    }
    v.trig = 1;
    v.key = -1;
    v.age = p.clock;
  }
  for (int i = 0; i < kVoices; ++i) {   // (the last trig's voices not taken again: held, let go; else ringing on)
    XVoice& v = p.v[i];
    if (p.k[3] > 0.98f && ((p.group >> i) & 1) && !((used >> i) & 1) && v.alive && !v.let_go)
      v.let_go = 1, v.fdec = v.fenv, v.adec = v.aenv, v.fatt = v.aatt = 1.0f;
  }
  p.group = used;
}

struct XBlock {                         // a voice's settings for a sub-block
  float da, db, idb;                    // the oscillators' steps (A's before its FM), 1 / B's
  float fm;                             // B on A's frequency (POLY-MOD to the right)
  float asaw, apul, apw, bsaw, bpul, btri, bpw, noise;
  float G, cx, ks;                      // the filter: the one-poles' gain; the loop solved (u = cx x - ks S)
};

// the oscillators: n samples into x (each its own loop by what it has: B's triangle, the sync; the FM in all of them,
// 0 when there is none: A's step times 1, the same sound to the bit, 0.16.24: half the code)
template <bool TRI, bool SYNC>
void oscs(XVoice& v, const XBlock& c, float* __restrict x, int n) {
  float pa = v.pa, pb = v.pb, p1 = v.p1, p2 = v.p2, carry = v.carry, bprev = v.bprev;
  const float db = c.db, idb = c.idb, da0 = c.da, fm = c.fm, noise = c.noise;
  const float apw = c.apul != 0.0f ? c.apw : 2.0f, bpw = c.bpul != 0.0f ? c.bpw : 2.0f;   // (no pulse: never crossed)
  const float Aa = 2.0f * c.asaw, Ab = c.apul - c.asaw, P = c.apul;   // (A: Aa p + Ab - (p >= pw: 2 P))
  const float Ba = 2.0f * c.bsaw, Bb = c.bpul - c.bsaw, Q = c.bpul;
  const float btri = c.btri, Db = 8.0f * db * c.btri;
  const float Jwa = -2.0f * c.asaw + 2.0f * c.apul, Jwb = -2.0f * c.bsaw + 2.0f * c.bpul;
  for (int s = 0; s < n; ++s) {
    float cur = carry;
    carry = 0.0f;
    // B (the master): its wrap (the saw falls, the pulse rises, the triangle turns up), its pulse's fall, its peak
    const float old_pb = pb;
    pb += db;
    float tb = -1.0f;                   // (its wrap, samples ago: -1 none)
    if (pb >= 1.0f) {
      pb -= 1.0f;
      tb = pb * idb;
      edge(Jwb, tb, p2, p1, cur, carry);
      if (TRI) corner(Db, tb, p2, p1, cur, carry);
    } else {
      if (old_pb < bpw && pb >= bpw) edge(-2.0f * Q, (pb - bpw) * idb, p2, p1, cur, carry);
      if (TRI && old_pb < 0.5f && pb >= 0.5f) corner(-Db, (pb - 0.5f) * idb, p2, p1, cur, carry);
    }
    // A: its step (B's FM on it), its wrap, its pulse's fall; synced: started again where B wrapped
    const float da = da0 * (1.0f + fm * bprev);
    const float old_pa = pa;
    pa += da;
    if (SYNC && tb >= 0.0f) {
      float at = old_pa + (1.0f - tb) * da;   // (A where B wrapped)
      if (at >= 1.0f) {                 // (A wrapped first)
        at -= 1.0f;
        edge(Jwa, tb + at / da, p2, p1, cur, carry);
      } else if (old_pa < apw && at >= apw) {   // (its pulse fell first)
        edge(-2.0f * P, tb + (at - apw) / da, p2, p1, cur, carry);
      }
      const float before = Aa * at + Ab - (at >= apw ? 2.0f * P : 0.0f);
      edge(Ab - before, tb, p2, p1, cur, carry);   // (from there to its start: a saw's -1, a pulse's +1)
      pa = tb * da;
    } else if (pa >= 1.0f) {
      pa -= 1.0f;
      edge(Jwa, pa / da, p2, p1, cur, carry);   // (its step, FM's included: a division, at the edges only)
    } else if (old_pa < apw && pa >= apw) {
      edge(-2.0f * P, (pa - apw) / da, p2, p1, cur, carry);
    }
    // the sample: A, B, the noise
    cur += Aa * pa + Ab - (pa >= apw ? 2.0f * P : 0.0f) + Ba * pb + Bb - (pb >= bpw ? 2.0f * Q : 0.0f);
    if (TRI) cur += btri * (pb < 0.5f ? 4.0f * pb - 1.0f : 3.0f - 4.0f * pb);
    bprev = 2.0f * pb - 1.0f;           // (its FM: B as a saw)
    if (noise != 0.0f) cur += noise * bip(v.nz);
    x[s] = p2;                          // (the sample two before: now whole)
    p2 = p1;
    p1 = cur;
  }
  v.pa = pa, v.pb = pb, v.p1 = p1, v.p2 = p2, v.carry = carry, v.bprev = bprev;
}

// n (<= kSub) samples of a voice, added to y: its oscillators into x, then its filter
void voice_block(XVoice& v, const XBlock& c, int sync, float e0, float de, float* __restrict y, int n) {
  float x[kSub];
  const int tri = c.btri != 0.0f;
  switch ((tri ? 2 : 0) | (sync ? 1 : 0)) {
    case 0: oscs<false, false>(v, c, x, n); break;
    case 1: oscs<false, true>(v, c, x, n); break;
    case 2: oscs<true, false>(v, c, x, n); break;
    default: oscs<true, true>(v, c, x, n); break;
  }
  // the filter: Curtis' four poles (one-poles in a loop, solved at once), its input soft-clipped
  float s1 = v.s1, s2 = v.s2, s3 = v.s3, s4 = v.s4, e = e0;
  const float G = c.G, cx = c.cx, ksb = c.ks * (1.0f - c.G);
  for (int s = 0; s < n; ++s) {
    float u = cx * x[s] - ksb * (s4 + G * (s3 + G * (s2 + G * s1)));
    u = fm_minf(fm_maxf(u, -1.5f), 1.5f);
    u -= (4.0f / 27.0f) * u * u * u;
    float w = G * (u - s1);
    const float y1 = w + s1;
    s1 = y1 + w;
    w = G * (y1 - s2);
    const float y2 = w + s2;
    s2 = y2 + w;
    w = G * (y2 - s3);
    const float y3 = w + s3;
    s3 = y3 + w;
    w = G * (y3 - s4);
    const float o = w + s4;
    s4 = o + w;
    y[s] += o * e;
    e += de;
  }
  v.s1 = s1, v.s2 = s2, v.s3 = s3, v.s4 = s4;
}

// an envelope's sub-block: its attack (an RC's), its fall toward a floor (held: its sustain), its release
inline float env_step(float& lvl, float& a0, float& att, float& dec, float ainc, float dkb, float floor_, int m,
                      float dk, int sustain) {
  float e1;
  if (att < 1.0f) {
    att += ainc * static_cast<float>(m);
    float a = att < 1.0f ? att : 1.0f;
    e1 = a0 + (1.0f - a0) * a * (2.0f - a);
    if (att >= 1.0f) dec = 1.0f;
  } else if (sustain && floor_ >= 1.0f) {
    e1 = 1.0f;
  } else {
    float k = m == kSub ? dkb : fm_exp2f(fm_log2f(dk) * static_cast<float>(m));
    dec = floor_ + (dec - floor_) * k;
    e1 = dec;
  }
  lvl = e1;
  return e1;
}

}  // namespace

extern "C" {

void pro_render(int t, float* out, uint32_t n) {
  if (t < 0 || t >= MF_TRACKS) return;
  Pro& p = pro[t];
  for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
  if (p.trig) {
    p.trig = 0;
    take(p);
  }
  const OscProg& o = kOsc[prog_of(p.k[0])];
  const float vint = p.k[4];
  const float env_u = 2.0f * p.k[5] - 1.0f, pm = 2.0f * p.k[6] - 1.0f;
  const float ea = env_u < 0.0f ? -env_u : 0.0f;
  const float fA = 0.001f + 1.5f * ea * ea, aA = 0.001f + 2.5f * ea * ea * ea;   // ENV to the left: the attacks
  const float eamt = env_u >= 0.0f ? 5.0f * env_u : 4.5f * fm_sqrtf(ea);       // (octaves the filter opens)
  const int held = p.k[3] > 0.98f;
  const float tau = t60_s(p.k[3]) * (1.0f / 6.9078f);
  const float kres = reso_k(p.k[2]);
  const float fc0 = cutoff_hz(p.k[1]);
  const float sweep = pm < 0.0f ? -pm * 36.0f : 0.0f, fmd = pm > 0.0f ? 0.9f * pm : 0.0f;   // POLY-MOD
  // the wander: a new aim each voice every 0.3 s
  p.dr_n -= static_cast<int>(n);
  if (p.dr_n <= 0) {
    p.dr_n = 13230;
    for (int i = 0; i < kVoices; ++i) p.v[i].dr_t = bip(p.rnd);
  }
  const float wk = clampf(static_cast<float>(n) * 2e-5f, 0.0f, 1.0f);
  int active = 0;
  for (int i = 0; i < kVoices; ++i) {
    XVoice& v = p.v[i];
    v.dr += (v.dr_t - v.dr) * wk;
    if (v.trig) {
      v.trig = 0;
      v.fa0 = v.fenv, v.aa0 = v.aenv;
      v.fatt = v.aatt = 0.0f;
      v.fdec = v.adec = 1.0f;
      v.let_go = 0;
      v.alive = 1;
    }
    active += v.alive;
  }
  if (!active && fm_fabsf(p.dcy) < 1e-6f) {   // (silent: the coupling capacitor's tail out too)
    p.gn = 0.0f;
    p.dcx = p.dcy = 0.0f;
    return;
  }
  // the voices' level, glided over ~8 ms (0.16.5: POLY's: a voice ringing on under the next one, then gone, a click)
  const float norm = 0.62f / fm_sqrtf(static_cast<float>(active > 1 ? active : 1));
  if (p.gn <= 0.0f) p.gn = norm;
  const float gb = p.gn + (norm - p.gn) * clampf(static_cast<float>(n) / (0.008f * kSr), 0.0f, 1.0f);
  const float gstep = (gb - p.gn) / static_cast<float>(n);
  for (int i = 0; i < kVoices; ++i) {
    XVoice& v = p.v[i];
    if (!v.alive) continue;
    // its pitches: VINTAGE (A and B apart, each voice its own few cents, its wander)
    const float spread = o.sync ? 0.0f : 7.0f * vint;   // (cents, A down, B up)
    const float ca = v.tol[0] * 6.0f * vint + v.dr * 5.0f * vint - spread;
    const float cb = v.tol[1] * 6.0f * vint + v.dr * 4.0f * vint + spread;
    const float fb = note_hz(v.note + o.bint + cb * 0.01f);
    const float fa0 = o.sync ? fb * fm_exp2f(o.aint * (1.0f / 12.0f)) : note_hz(v.note + ca * 0.01f);
    XBlock c;
    c.db = clampf(fb / kSr, 1e-6f, 0.45f);
    c.idb = 1.0f / c.db;
    c.fm = fmd;
    c.asaw = o.asaw, c.apul = o.apul, c.bsaw = o.bsaw, c.bpul = o.bpul, c.btri = o.btri, c.noise = o.noise;
    c.bpw = clampf(o.bpw, 2.0f * c.db, 1.0f - 2.0f * c.db);
    // its envelopes (VINTAGE: their times a little apart)
    const float tt = 1.0f + 0.25f * v.tol[3] * vint;
    const float rel = 0.03f + 0.5f * aA;
    const float adk = fm_expf(-1.0f / ((v.let_go ? rel : tau * tt) * kSr));
    const float fdk = fm_expf(-1.0f / ((v.let_go ? rel : held ? 0.5f * tt : 0.7f * tau * tt) * kSr));
    float adkb = adk, fdkb = fdk;       // (a sub-block's: ^16)
    for (int q = 0; q < 4; ++q) adkb *= adkb, fdkb *= fdkb;
    const float ainc = 1.0f / (aA * tt * kSr), finc = 1.0f / (fA * tt * kSr);
    const int sustain = held && !v.let_go && (v.key >= 0 || (p.gate && ((p.group >> i) & 1)));
    const float kt = (v.note - 60.0f) * (0.7f / 12.0f) + v.tol[2] * 0.3f * vint;   // (the keyboard: 70 %)
    for (uint32_t s0 = 0; s0 < n; s0 += kSub) {
      const int m = n - s0 < static_cast<uint32_t>(kSub) ? static_cast<int>(n - s0) : kSub;
      const float e0 = v.aenv;
      const float e1 = env_step(v.aenv, v.aa0, v.aatt, v.adec, ainc, adkb, sustain ? 1.0f : 0.0f, m, adk, sustain);
      const float f1 = env_step(v.fenv, v.fa0, v.fatt, v.fdec, finc, fdkb, sustain ? 0.33f : 0.0f, m, fdk, sustain);
      // A: its pitch, the filter's envelope on it (POLY-MOD to the left)
      const float fa = sweep > 0.0f ? fa0 * fm_exp2f(sweep * f1 * (1.0f / 12.0f)) : fa0;
      c.da = clampf(fa / kSr, 1e-6f, 0.45f / (1.0f + fmd));
      c.apw = clampf(o.apw, 2.0f * c.da, 1.0f - 2.0f * c.da);
      // the filter: its cutoff (the knob, the keyboard, its envelope), its gain, the loop
      float fc = fc0 * fm_exp2f(kt + eamt * f1);
      float x = 3.14159265f * clampf(fc / kSr, 0.0002f, 0.45f), x2 = x * x;
      float g = x * (15.0f - x2) / (15.0f - 6.0f * x2);   // (tan, Pade)
      g = g < 12.0f ? g : 12.0f;
      c.G = g / (1.0f + g);
      // the loop: u = comp x - k y4 (y4 solved: (G^4 x + S) / (1 + k G^4)); comp, the bass the resonance takes, a
      // third given back
      const float G4 = c.G * c.G * c.G * c.G, inv = 1.0f / (1.0f + kres * G4), drive = 0.95f;
      c.cx = drive * (1.0f + 0.35f * kres - kres * G4 * inv);
      c.ks = kres * inv;
      const float g0 = p.gn + gstep * static_cast<float>(s0), g1 = g0 + gstep * static_cast<float>(m);
      voice_block(v, c, o.sync, e0 * g0, (e1 * g1 - e0 * g0) / static_cast<float>(m), out + s0, m);
    }
    if (v.aatt >= 1.0f && !sustain && v.aenv < 1e-5f) {
      v.alive = 0;
      v.aenv = v.fenv = 0.0f;
    }
  }
  p.gn = active ? gb : 0.0f;
  // the sum: the coupling capacitor, a soft knee above 0.8 (at most 1.2)
  for (uint32_t s = 0; s < n; ++s) {
    float yv = out[s], yd = yv - p.dcx + 0.9993f * p.dcy;
    p.dcx = yv;
    p.dcy = yd;
    float ay = fm_fabsf(yd);
    if (ay > 0.8f) {
      float z = (ay - 0.8f) * 2.5f;
      ay = 0.8f + 0.4f * z / (1.0f + z);
      yd = yd < 0.0f ? -ay : ay;
    }
    out[s] = yd;
  }
}

const char* pro_knob_name(int k) {
  static const char* const kN[8] = {"Osc", "Cutoff", "Reso", "Decay", "Vintage", "Env", "Polymod", "Chord"};
  return k >= 0 && k < 8 ? kN[k] : "";
}

void pro_defaults(float* k) {           // two saws a little apart, a brass's filter, a voice a trig
  static const float kD[8] = {0.5f / 13.0f, 0.52f, 0.25f, 0.55f, 0.35f, 0.35f, 0.5f, 0.5f};
  for (int i = 0; i < 8; ++i) k[i] = kD[i];
}

int pro_knob_steps(int k) { return k == 0 ? kProgs : k == 7 ? 15 : 128; }

MI_COLD void pro_knob_text(int k, float u, char* b, int n) {
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  switch (k) {
    case 0:
      return put_s(b, n, &at, kOsc[prog_of(u)].name);
    case 1: {
      float f = cutoff_hz(u);
      if (f < 1000.0f) {
        put_i(b, n, &at, static_cast<int>(f + 0.5f));
        return put_s(b, n, &at, "Hz");
      }
      int x = static_cast<int>(f * 0.01f + 0.5f);
      put_i(b, n, &at, x / 10);
      if (x < 100) put_s(b, n, &at, "."), put_i(b, n, &at, x % 10);
      return put_s(b, n, &at, "k");
    }
    case 3:
      if (u > 0.98f) return put_s(b, n, &at, "Hold");
      return put_time(b, n, &at, t60_s(u));
    case 4:
      if (u < 0.004f) return put_s(b, n, &at, "Clean");
      break;
    case 7:
      return put_s(b, n, &at, poly_chord_name(u));   // (CHORD: POLY's chords)
    case 5: case 6: {                   // bipolar: ENV (- with an attack, + a pluck), POLY-MOD (- the sweep, + FM)
      float w = 2.0f * u - 1.0f;
      int x = static_cast<int>(w * 100.0f + (w < 0.0f ? -0.5f : 0.5f));
      if (x == 0) return put_s(b, n, &at, k == 6 ? "Off" : "0%");
      put_i(b, n, &at, x < 0 ? -x : x);
      static const char* const kL[2][2] = {{"% SWELL", "% ENV"}, {"% SWEEP", "% FM"}};
      return put_s(b, n, &at, kL[k == 6][x > 0]);
    }
    default:
      break;
  }
  put_i(b, n, &at, static_cast<int>(u * 100.0f + 0.5f));
  put_s(b, n, &at, "%");
}

}  // extern "C"
