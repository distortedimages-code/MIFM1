// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's VOCODER (0.16.21): a channel vocoder and its own carrier, after Roland's VP-330 Vocoder Plus (1979: a
// vocoder, a string section and a choir, one ensemble over them) and the string machines (the Solina, the RS-202).
//
// The carrier: four voices a track (the trigs' notes, their step's chord, the keys held), each three saws; TONE goes from
// fat saws (three, wide apart, bright) to strings (two closer, a third an octave over them, softened: a string
// machine's 8' and 4') to a choir (the VP-330's Human Voice: the same through three fixed formants, "ah"). An
// ensemble over it all, as the Solina's and the VP-330's: three taps on a short line, swept by a slow and a fast LFO,
// each tap a third of a turn on.
//
// The vocoder: another track's sound (MOD: T1..T4, its last block as it played, before its level: a track turned down
// still speaks; Off: the carrier alone, a string machine) split into 12 bands, half an octave apart (130 Hz .. 6.3
// kHz: state variable band passes), each band's level followed (an attack of 2 ms, FOLLOW its release, at its end held:
// frozen), the voice's own level kept out (its bands over their rms: whispered or shouted, the carrier as loud); the
// carrier through the same 12 bands, each at its band's level. SHIFT: which band of the voice drives which
// of the carrier (a voice lower, higher). AIR: the voice's own highs let through (its sibilants: an S, a T: a carrier
// has none of them). The voice's bands are listened to at a lower rate (the six over 1 kHz at half the rate, the six
// under at an eighth: their levels the same, their work a fifth), the carrier's at the full one (they are heard).
//
// Its memory: the track's Plaits voice's, lent (mf_voice_mem): the voice's block, the bands, the ensemble's line. Eight
// knobs (k: 0..1, SHIFT bipolar from its middle): MOD, TONE, ENSEMBLE, ATTACK, RELEASE, FOLLOW, SHIFT, AIR.
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f;
const int kVoices = 4;
const int kBands = 12;
const int kLow = 6;                     // the bands listened to at an eighth of the rate (the rest at a half)
const int kLine = 1024;                 // the ensemble's line (23 ms; a power of two)
const int kMaxN = 128;                  // a block, at most (the engine's)

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }
inline uint32_t xs(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline float bip(uint32_t& s) { return static_cast<float>(xs(s) >> 8) * (2.0f / 16777216.0f) - 1.0f; }
inline float sin_cyc(float ph) {        // a sine of a phase in cycles, 0..1
  float x = ph < 0.5f ? ph : ph - 0.5f, sg = ph < 0.5f ? 1.0f : -1.0f;
  x = x < 0.25f ? x : 0.5f - x;
  float z = x * 4.0f, z2 = z * z;
  return sg * z * (1.5707963f + z2 * (-0.64596336f + z2 * (0.079688469f + z2 * (-0.0046722113f + z2 * 0.00015081274f))));
}
inline float two_sin(float hz, float sr) {   // Chamberlin's f: 2 sin(pi hz / sr)
  const float x = 3.14159265f * hz / sr;
  return 2.0f * x * (1.0f - x * x * (1.0f / 6.0f) + x * x * x * x * (1.0f / 120.0f));
}

struct VVoice {
  float note;
  float ph[3];                          // its three saws' phases
  float prev;                           // (its saws' sum a sample before: their edges' corrections land there too)
  float env, dec;                       // its level; where its release began
  float a0, att;                        // (its attack: where it began, its progress)
  int8_t key;                           // the key it plays (-1: a trig's)
  uint8_t slot, alive, let_go, trig;
  uint32_t age;
};
struct Voc {
  float k[8];
  float notes[kVoices];
  int n, gate, trig, mono;              // (mono: 0.16.28's PLAY Mono, one voice: its last again)
  uint32_t clock, rnd;
  VVoice v[kVoices];
  float lfo[2];                         // the ensemble's LFOs (cycles)
  int wr;                               // its line's write place
  float lp1, lp2;                       // the carrier's softening (two one-poles)
  float fo[3][2];                       // the choir's formants (state variable: low, band)
  float hp[2];                          // AIR's high pass (the voice's)
  float dcx, dcy;
  uint32_t epoch;                       // the lent memory's (another: it was taken back; ours made anew)
  int has_mod;                          // a voice block came this block
  float gn;
  float agc;                            // the voice's bands' scale (their spread: 1 over their rms), glided
};
static_assert(sizeof(Voc) <= MF_STATE_BYTES, "VOCODER's state");
struct VocAt {
  Voc& operator[](int t) const { return *static_cast<Voc*>(mf_state(t)); }
};
const VocAt voc = {};

// the lent memory: the voice's block, its bands (low, band; their level), the carrier's bands (low, band), the
// ensemble's line, the carrier's block
struct VocMem {
  float mod[kMaxN];
  float mb[kBands][4], lvl[kBands];     // (each band two band passes in a row: their low and band states)
  float cb[kBands][4];
  float dec[2];                         // (the voice brought down: its last half-rate sample's partner, the eighth's sum)
  int dn;                               // (where in the eighth's four)
  float y2;                             // (the low bands' last half-rate sample: the next block's interpolation)
  float line[kLine];
  float car[kMaxN];
};

// the bands: 130 Hz .. 6.3 kHz, half an octave apart, each two of Chamberlin's band passes in a row (Q 2.2 each: 0.41
// of an octave wide at -3 dB, an octave away 21 dB down), at their own rate: the voice's six low ones at an eighth of
// it, its six high ones at a half; the carrier's six low ones at a half, its high ones at the full rate (they are
// heard: the low ones brought back up between two samples).
// Chamberlin's f and damping put where the poles belong (0.16.21): a resonator's, its radius from the band's width
// (r = exp(-pi fc / (Q sr)), its angle at fc), f = |1 - r e^(j theta)|, its damping (1 - r^2) / f; its gain 1 at fc
// (g, the pair's). The plain f = 2 sin(pi fc / sr) holds far under the rate only: the voice's top band at the half
// rate (fc 0.29 of it) rang near its Nyquist, 200 times too loud, whatever the voice (its sizzle)
struct Band {
  float f, q, g;
};
Band band_v[kBands], band_c[kBands];    // (the voice's, the carrier's)
float band_w[kBands];                   // (0.16.38) the carrier's bands' emphasis (make_bands)
int bands_made;
const float kBandQ = 2.2f;
inline float cos_cyc(float ph) {        // (a cosine of a phase in cycles, 0..0.75)
  ph += 0.25f;
  return sin_cyc(ph < 1.0f ? ph : ph - 1.0f);
}
MI_COLD Band band_at(float hz, float sr) {   // (once, at the start)
  const float c = hz / sr, r = fm_expf(-3.14159265f * c / kBandQ);
  const float f = fm_sqrtf(1.0f - 2.0f * r * cos_cyc(c) + r * r);
  const float h = f * 2.0f * sin_cyc(0.5f * c) / ((1.0f - r) * fm_sqrtf(1.0f - 2.0f * r * cos_cyc(2.0f * c) + r * r));
  return Band{f, (1.0f - r * r) / f, 1.0f / (h * h)};
}
MI_COLD void make_bands() {
  for (int i = 0; i < kBands; ++i) {
    const float hz = 130.0f * fm_exp2f(static_cast<float>(i) * 0.5099f);
    band_v[i] = band_at(hz, i < kLow ? kSr / 8.0f : kSr / 2.0f);
    band_c[i] = band_at(hz, i < kLow ? kSr / 2.0f : kSr);
  }
  // (0.16.38) the bands emphasised, 1.5 dB a band (3 dB an octave), 1 between the 4th and the 5th (~450 Hz): the
  // voice's highs and the carrier's both fall with the frequency, its 2 to 5 kHz came out 21 dB under its 0.5 to 2,
  // dull, the more so played low; now 12 (SPEECH vocoded at C3: 2-5 kHz +10 dB, 5-10 kHz +12, its level the same)
  for (int i = 0; i < kBands; ++i) band_w[i] = fm_exp2f((static_cast<float>(i) - 3.5f) * 0.25f);
  bands_made = 1;
}
inline int mod_of(float u) {            // MOD: 0 Off, 1..4 a track
  int i = static_cast<int>(clampf(u, 0.0f, 1.0f) * 5.0f);
  return i > 4 ? 4 : i;
}
inline float attack_s(float u) { return 0.003f * fm_exp2f(u * 9.966f); }   // ATTACK: 3 ms .. 3 s
inline float release_s(float u) { return 0.03f * fm_exp2f(u * 7.644f); }   // RELEASE: 30 ms .. 6 s
inline float follow_s(float u) { return 0.01f * fm_exp2f(u * 6.644f); }    // FOLLOW: 10 ms .. 1 s (then held)

// a voice for each of the trig's notes (a free one, else the quietest, the oldest); the others let go
MI_COLD void take(Voc& s) {
  ++s.clock;
  const int n = s.n < 1 ? 1 : s.n > kVoices ? kVoices : s.n;
  int used = 0;
  for (int j = 0; j < n; ++j) {
    int best = 0;
    float bs = 1e9f;
    for (int i = 0; i < kVoices; ++i) {
      if ((used >> i) & 1) continue;
      const VVoice& v = s.v[i];
      float score = v.alive ? v.env + 1.0f - static_cast<float>(s.clock - v.age) * 1e-6f : 0.0f;
      if (v.alive && v.key >= 0) score += 2.0f;
      if (s.mono) score = v.alive ? -static_cast<float>(v.age) : 0.0f;   // (Mono: the voice playing, again)
      if (score < bs) bs = score, best = i;
    }
    used |= 1 << best;
    VVoice& v = s.v[best];
    v.key = -1;
    v.slot = static_cast<uint8_t>(j);
    v.note = s.notes[j];
    if (!v.alive) {
      for (int l = 0; l < 3; ++l) v.ph[l] = 0.5f + 0.5f * bip(s.rnd);
      v.env = 0.0f;
      v.prev = 0.0f;
    }
    v.trig = 1;
    v.age = s.clock;
  }
  for (int i = 0; i < kVoices; ++i)
    if (!((used >> i) & 1) && s.v[i].alive && !s.v[i].let_go && s.v[i].key < 0) s.v[i].let_go = 1, s.v[i].dec = s.v[i].env;
}

// a voice's saws added into y (n samples), its level going from e0 to e1: TONE's three (two apart by d cents, the
// third at r3 of the first's pitch, at g3). Falling saws, each wrap's step a two-point polyBLEP's: its part after the
// edge on this sample (2t - t^2 - 1, t: how far past it), its part before it on the last (t^2; the voice a sample
// late: its edges found when they come)
__attribute__((noinline)) void voice_saws(VVoice& v, float d_cents, float r3, float g3, float e0, float e1,
                                         float* __restrict y, int n) {
  const float f = note_hz(v.note);
  const float r = fm_exp2f(d_cents * (1.0f / 1200.0f));
  const float d[3] = {clampf(f / kSr, 1e-6f, 0.45f), clampf(f * r / kSr, 1e-6f, 0.45f), clampf(f * r3 / kSr, 1e-6f, 0.45f)};
  const float g[3] = {1.0f, 1.0f, g3};
  const float id[3] = {1.0f / d[0], 1.0f / d[1], 1.0f / d[2]};
  float p0 = v.ph[0], p1 = v.ph[1], p2 = v.ph[2], prev = v.prev, e = e0;
  const float de = (e1 - e0) / static_cast<float>(n);
  const float off = 1.0f + 1.0f + g[2];     // (the saws' 1 - 2p, together)
  for (int i = 0; i < n; ++i) {
    p0 += d[0];
    p1 += d[1];
    p2 += d[2];
    float cur = off - 2.0f * (p0 + p1 + g[2] * p2);
    if (p0 >= 1.0f) {                   // (a wrap: the step +2, its residual on this sample and the last)
      p0 -= 1.0f;
      const float t = p0 * id[0];
      cur += 2.0f + 2.0f * (t - 0.5f * t * t - 0.5f);
      prev += t * t;
    }
    if (p1 >= 1.0f) {
      p1 -= 1.0f;
      const float t = p1 * id[1];
      cur += 2.0f + 2.0f * (t - 0.5f * t * t - 0.5f);
      prev += t * t;
    }
    if (p2 >= 1.0f) {
      p2 -= 1.0f;
      const float t = p2 * id[2];
      cur += g[2] * (2.0f + 2.0f * (t - 0.5f * t * t - 0.5f));
      prev += g[2] * t * t;
    }
    e += de;
    y[i] += prev * e;
    prev = cur;
  }
  v.ph[0] = p0, v.ph[1] = p1, v.ph[2] = p2, v.prev = prev;
}

// the choir's formants ("ah": 650, 1080, 2650 Hz; Chamberlin's state variable band passes, their levels 1, 0.6, 0.3)
const float kFormHz[3] = {650.0f, 1080.0f, 2650.0f};
const float kFormGain[3] = {1.0f, 0.6f, 0.3f};

// the voice's bands: their levels followed (at their rates: x2 half the rate's samples, n2 of them; x8 the eighth's)
__attribute__((noinline)) void listen(VocMem* m, const float* x2, int n2, const float* x8, int n8, float att2,
                                      float rel2, float att8, float rel8, float* lv0, float* lv1) {
  for (int i = 0; i < kBands; ++i) {
    float l1 = m->mb[i][0], b1 = m->mb[i][1], l2 = m->mb[i][2], b2 = m->mb[i][3], lv = m->lvl[i];
    const float f = band_v[i].f, q = band_v[i].q, gg = band_v[i].g;
    const float* x = i < kLow ? x8 : x2;
    const int nn = i < kLow ? n8 : n2;
    const float att = i < kLow ? att8 : att2, rel = i < kLow ? rel8 : rel2;
    lv0[i] = lv;
    for (int j = 0; j < nn; ++j) {
      l1 += f * b1;
      b1 += f * (x[j] - l1 - q * b1);
      l2 += f * b2;
      b2 += f * (b1 - l2 - q * b2);
      const float a = fm_fabsf(b2) * gg;
      lv += (a > lv ? att : rel) * (a - lv);
    }
    if (fm_fabsf(b1) < 1e-20f) l1 = b1 = 0.0f;
    if (fm_fabsf(b2) < 1e-20f) l2 = b2 = 0.0f;
    if (lv < 1e-20f) lv = 0.0f;
    m->mb[i][0] = l1, m->mb[i][1] = b1, m->mb[i][2] = l2, m->mb[i][3] = b2, m->lvl[i] = lv;
    lv1[i] = lv;
  }
}

// the carrier's bands at their levels (from e0 to e1 over the block), added into y: its six high ones at the full rate,
// its six low ones at half of it (x2: the carrier brought down, n2 samples), their sum brought back up between two
// samples (linearly: its images at 21 kHz, 50 dB down)
inline void bands(float (*st)[4], const float* x, float* __restrict y, int n, const float* e0, const float* e1,
                  int from, int to) {
  const float inv = 1.0f / static_cast<float>(n);
  for (int i = from; i < to; ++i) {
    float l1 = st[i][0], b1 = st[i][1], l2 = st[i][2], b2 = st[i][3];
    const float fi = band_c[i].f, q = band_c[i].q, gg = band_c[i].g;
    float e = e0[i] * gg;
    const float de = (e1[i] - e0[i]) * gg * inv;
    for (int j = 0; j < n; ++j) {
      l1 += fi * b1;
      b1 += fi * (x[j] - l1 - q * b1);
      l2 += fi * b2;
      b2 += fi * (b1 - l2 - q * b2);
      e += de;
      y[j] += b2 * e;
    }
    if (fm_fabsf(b1) < 1e-20f) l1 = b1 = 0.0f;
    if (fm_fabsf(b2) < 1e-20f) l2 = b2 = 0.0f;
    st[i][0] = l1, st[i][1] = b1, st[i][2] = l2, st[i][3] = b2;
  }
}
__attribute__((noinline)) void speak(VocMem* m, const float* car, float* __restrict y, int n, const float* e0,
                                     const float* e1) {
  bands(m->cb, car, y, n, e0, e1, kLow, kBands);
  float c2[kMaxN / 2], y2[kMaxN / 2];
  const int n2 = n / 2;
  for (int j = 0; j < n2; ++j) c2[j] = 0.5f * (car[2 * j] + car[2 * j + 1]), y2[j] = 0.0f;
  bands(m->cb, c2, y2, n2, e0, e1, 0, kLow);
  float last = m->y2;
  for (int j = 0; j < n2; ++j) {
    y[2 * j] += 0.5f * (last + y2[j]);
    y[2 * j + 1] += y2[j];
    last = y2[j];
  }
  m->y2 = fm_fabsf(last) < 1e-20f ? 0.0f : last;
}

// the carrier softened (two one-poles at c: s2 its states), its level glided (g from g0 by dg), then (form > 0) the
// choir's formants mixed in (fo their states)
__attribute__((noinline)) void soften(float* car, int n, float c, float* st, float g, float dg, float form,
                                      float (*fo)[2]) {
  float l1 = st[0], l2 = st[1];
  if (form > 0.0f) {
    float fl[3], fb[3], ff[3];
    for (int j = 0; j < 3; ++j) fl[j] = fo[j][0], fb[j] = fo[j][1], ff[j] = two_sin(kFormHz[j], kSr);
    const float fq = 1.0f / 6.0f;       // (the formants' Q: 6)
    for (int i = 0; i < n; ++i) {
      g += dg;
      l1 += c * (car[i] * g - l1);
      l2 += c * (l1 - l2);
      fl[0] += ff[0] * fb[0];
      fb[0] += ff[0] * (l1 - fl[0] - fq * fb[0]);
      fl[1] += ff[1] * fb[1];
      fb[1] += ff[1] * (l1 - fl[1] - fq * fb[1]);
      fl[2] += ff[2] * fb[2];
      fb[2] += ff[2] * (l1 - fl[2] - fq * fb[2]);
      const float f = fq * (kFormGain[0] * fb[0] + kFormGain[1] * fb[1] + kFormGain[2] * fb[2]);
      car[i] = l2 + form * (2.2f * f - l2);
    }
    for (int j = 0; j < 3; ++j)
      fo[j][0] = fm_fabsf(fl[j]) < 1e-20f ? 0.0f : fl[j], fo[j][1] = fm_fabsf(fb[j]) < 1e-20f ? 0.0f : fb[j];
  } else {
    for (int i = 0; i < n; ++i) {
      g += dg;
      l1 += c * (car[i] * g - l1);
      l2 += c * (l1 - l2);
      car[i] = l2;
    }
  }
  st[0] = fm_fabsf(l1) < 1e-20f ? 0.0f : l1, st[1] = fm_fabsf(l2) < 1e-20f ? 0.0f : l2;
}

// the voice brought down: to half the rate (two samples' mean, n2 of them into x2), then to an eighth (four of those
// into x8: their sum carried over in sum, where in the four in dn); how many eighths
__attribute__((noinline)) int bring_down(const float* mod, int n2, float* x2, float* x8, float* sum, int* dn) {
  float acc = *sum;
  int d = *dn, n8 = 0;
  for (int j = 0; j < n2; ++j) {
    x2[j] = 0.5f * (mod[2 * j] + mod[2 * j + 1]);
    acc += x2[j];
    if (++d == 4) x8[n8++] = 0.25f * acc, acc = 0.0f, d = 0;
  }
  *sum = acc;
  *dn = d;
  return n8;
}

// AIR: the voice's highs (over 4.5 kHz: two one-pole high passes, h their states) added at a
__attribute__((noinline)) void add_air(const float* mod, float* y, int n, float a, float* h) {
  const float c = 1.0f - fm_expf(-6.2831853f * 4500.0f / kSr);
  float h1 = h[0], h2 = h[1];
  for (int j = 0; j < n; ++j) {
    h1 += c * (mod[j] - h1);
    const float x1 = mod[j] - h1;
    h2 += c * (x1 - h2);
    y[j] += a * (x1 - h2);
  }
  h[0] = fm_fabsf(h1) < 1e-20f ? 0.0f : h1, h[1] = fm_fabsf(h2) < 1e-20f ? 0.0f : h2;
}

// the ensemble: y through three taps of the line (their read places behind the write: dl, their steps ddl), dry and
// wet; its write place
__attribute__((noinline)) int ensemble(float* y, int n, float* line, int w, float* dl, const float* ddl, float dry,
                                       float wet) {
  float d0 = dl[0], d1 = dl[1], d2 = dl[2];
  for (int j = 0; j < n; ++j) {
    const float x = y[j];
    line[w] = x;
    d0 += ddl[0], d1 += ddl[1], d2 += ddl[2];
    const float fw = static_cast<float>(w);
    const float r0 = fw + d0, r1 = fw + d1, r2 = fw + d2;
    const int i0 = static_cast<int>(r0), i1 = static_cast<int>(r1), i2 = static_cast<int>(r2);
    const float a0 = line[i0 & (kLine - 1)], a1 = line[i1 & (kLine - 1)], a2 = line[i2 & (kLine - 1)];
    const float b0 = line[(i0 + 1) & (kLine - 1)], b1 = line[(i1 + 1) & (kLine - 1)], b2 = line[(i2 + 1) & (kLine - 1)];
    const float acc = a0 + (b0 - a0) * (r0 - static_cast<float>(i0)) + a1 + (b1 - a1) * (r1 - static_cast<float>(i1)) +
                      a2 + (b2 - a2) * (r2 - static_cast<float>(i2));
    w = (w + 1) & (kLine - 1);
    y[j] = dry * x + wet * acc;
  }
  return w;
}

MI_COLD VocMem* mem_of(Voc& s, int t) {   // its lent memory (silent when ours anew), null: none
  uint32_t bytes = 0, epoch = 0;
  VocMem* m = static_cast<VocMem*>(mf_voice_mem(t, &bytes, &epoch));
  if (!m || bytes < sizeof(VocMem)) return nullptr;
  if (s.epoch != epoch) {
    float* f = reinterpret_cast<float*>(m);
    for (uint32_t i = 0; i < sizeof(VocMem) / 4u; ++i) f[i] = 0.0f;
    m->dn = 0;
    s.epoch = epoch;
    s.has_mod = 0;
    s.wr = 0;
  }
  return m;
}

}  // namespace

extern "C" {

MI_COLD void voc_reset(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Voc& s = voc[t];
  uint8_t* raw = reinterpret_cast<uint8_t*>(&s);
  for (uint32_t i = 0; i < sizeof(Voc); ++i) raw[i] = 0;
  s.rnd = (0x7F4A7C15u * static_cast<uint32_t>(t + 3)) | 1u;
  s.epoch = 0xFFFFFFFFu;
  float d[8];
  voc_defaults(d);
  for (int i = 0; i < 8; ++i) s.k[i] = d[i];
  s.notes[0] = 60.0f;
  s.n = 1;
  s.lfo[1] = 0.37f;
  for (int i = 0; i < kVoices; ++i) s.v[i].key = -1, s.v[i].att = 1.0f;
  if (!bands_made) make_bands();
}

MI_COLD void voc_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) voc_reset(t);
  mf_forget();
}

MI_COLD void voc_silence(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Voc& s = voc[t];
  s.trig = 0;
  for (int i = 0; i < kVoices; ++i) s.v[i].alive = 0, s.v[i].env = 0.0f, s.v[i].trig = 0, s.v[i].att = 1.0f;
  s.epoch = 0xFFFFFFFFu;                // (its memory made anew at its next block: the line, the bands silent)
  s.lp1 = s.lp2 = s.dcx = s.dcy = s.gn = 0.0f;
  for (int j = 0; j < 3; ++j) s.fo[j][0] = s.fo[j][1] = 0.0f;
  s.hp[0] = s.hp[1] = 0.0f;
}

int voc_mod_track(float u) { return mod_of(u) - 1; }

MI_COLD void voc_modulator(int t, const float* x, uint32_t n) {
  if (t < 0 || t >= MF_TRACKS) return;
  Voc& s = voc[t];
  VocMem* m = mem_of(s, t);
  s.has_mod = 0;
  if (!m || !x || n > static_cast<uint32_t>(kMaxN)) return;
  for (uint32_t i = 0; i < n; ++i) m->mod[i] = x[i];
  s.has_mod = 1;
}

MI_COLD void voc_set(int t, const float* notes, int n, int mono, const float* k) {
  if (t < 0 || t >= MF_TRACKS) return;
  Voc& s = voc[t];
  s.mono = mono;
  for (int i = 0; i < 8; ++i) s.k[i] = clampf(k[i], 0.0f, 1.0f);
  n = n < 1 ? 1 : n > kVoices ? kVoices : n;
  for (int i = 0; i < n; ++i) s.notes[i] = notes[i];
  if (s.gate && n == s.n)               // (the notes held follow: a lock, FREQ's modulations)
    for (int i = 0; i < kVoices; ++i)
      if (s.v[i].alive && !s.v[i].let_go && s.v[i].age == s.clock && s.v[i].key < 0 && s.v[i].slot < n)
        s.v[i].note = notes[s.v[i].slot];
  s.n = n;
}

MI_COLD void voc_gate(int t, int g) {
  if (t < 0 || t >= MF_TRACKS) return;
  Voc& s = voc[t];
  if (g && !s.gate) s.trig = 1;
  if (!g && s.gate)
    for (int i = 0; i < kVoices; ++i)
      if (s.v[i].alive && !s.v[i].let_go && s.v[i].key < 0) s.v[i].let_go = 1, s.v[i].dec = s.v[i].env;
  s.gate = g;
}

MI_COLD void voc_keys(int t, const int* ids, const float* notes, int n) {
  if (t < 0 || t >= MF_TRACKS) return;
  Voc& s = voc[t];
  n = n < 0 ? 0 : n > kVoices ? kVoices : n;
  for (int i = 0; i < kVoices; ++i) {
    VVoice& v = s.v[i];
    if (v.key < 0) continue;
    int j = 0;
    while (j < n && ids[j] != v.key) ++j;
    if (j < n && (v.alive || v.trig)) {
      v.note = notes[j];
      continue;
    }
    if (v.alive && !v.let_go) v.let_go = 1, v.dec = v.env;
    v.key = -1;
  }
  for (int j = 0; j < n; ++j) {
    int have = 0;
    for (int i = 0; i < kVoices; ++i) have |= s.v[i].key == ids[j];
    if (have) continue;
    int best = 0;
    float bs = 1e9f;
    for (int i = 0; i < kVoices; ++i) {
      const VVoice& v = s.v[i];
      float score = v.alive ? v.env + 1.0f - static_cast<float>(s.clock - v.age) * 1e-6f : 0.0f;
      if (v.key >= 0) score += 2.0f;
      if (score < bs) bs = score, best = i;
    }
    VVoice& v = s.v[best];
    if (!v.alive) {
      for (int l = 0; l < 3; ++l) v.ph[l] = 0.5f + 0.5f * bip(s.rnd);
      v.env = 0.0f;
      v.prev = 0.0f;
    }
    v.note = notes[j];
    v.key = static_cast<int8_t>(ids[j]);
    v.trig = 1;
    v.age = ++s.clock;
  }
}

MI_COLD int voc_voices(int t) {
  if (t < 0 || t >= MF_TRACKS) return 0;
  int n = 0;
  for (int i = 0; i < kVoices; ++i) n += voc[t].v[i].alive;
  return n;
}

MI_COLD void voc_render(int t, float* out, uint32_t n) {   // (its glue for size: its samples' loops at -O2)
  if (t < 0 || t >= MF_TRACKS) return;
  Voc& s = voc[t];
  for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
  VocMem* m = mem_of(s, t);
  if (!m || n > static_cast<uint32_t>(kMaxN) || n < 8u) return;
  if (s.trig) {
    s.trig = 0;
    take(s);
  }
  // the carrier: TONE's place (saws, strings, choir)
  const float tone = s.k[1], a = tone < 0.5f ? tone * 2.0f : 1.0f, b = tone < 0.5f ? 0.0f : tone * 2.0f - 1.0f;
  const float det = 18.0f - 12.0f * a - 2.5f * b;   // (cents: 18 apart, then 6, then 3.5)
  // the third saw: under the others by as much (fat saws), then an octave over (the strings' 4'), quieter (the choir)
  const float r3 = a < 1.0f ? fm_exp2f(-det * (1.0f / 1200.0f)) * (1.0f - a) + 2.0f * a : 2.0f;
  const float g3 = a < 1.0f ? 1.0f - 0.45f * a : 0.55f - 0.25f * b;
  const float soft = 15000.0f - 8000.0f * a - 3500.0f * b;   // (its softening's cutoff: 15 kHz, 7, 3.5; 0.16.38:
                                                              // the strings' 4.5 and the choir's 2.7 before)
  const float form = b;                             // (the choir's formants)
  const float att = attack_s(s.k[3]), rel = release_s(s.k[4]);
  const float ainc = static_cast<float>(n) / (att * kSr), rk = fm_expf(-static_cast<float>(n) / (rel * kSr));
  float* car = m->car;
  for (uint32_t i = 0; i < n; ++i) car[i] = 0.0f;
  int active = 0;
  for (int i = 0; i < kVoices; ++i) {
    VVoice& v = s.v[i];
    if (v.trig) {
      v.trig = 0;
      v.a0 = v.env;
      v.att = 0.0f;
      v.let_go = 0;
      v.alive = 1;
    }
    if (!v.alive) continue;
    ++active;
    const float e0 = v.env;
    if (v.let_go) {
      v.dec *= rk;
      v.env = v.dec;
      if (v.att < 1.0f) v.att = 1.0f;
    } else if (v.att < 1.0f) {
      v.att += ainc;
      const float x = v.att < 1.0f ? v.att : 1.0f;
      v.env = v.a0 + (1.0f - v.a0) * x * (2.0f - x);
    } else {
      v.env = 1.0f;
    }
    voice_saws(v, det, r3, g3, e0, v.env, car, static_cast<int>(n));
    if (v.let_go && v.env < 1e-4f) v.alive = 0, v.env = 0.0f;
  }
  // the voices' level: 0.3 / sqrt(those sounding), glided
  const float norm = 0.3f / fm_sqrtf(static_cast<float>(active > 1 ? active : 1));
  if (s.gn <= 0.0f) s.gn = norm;
  const float g0 = s.gn;
  s.gn += (norm - s.gn) * 0.3f;
  const float dgn = (s.gn - g0) / static_cast<float>(n);
  // the softening (two one-poles), then the formants mixed in (the choir)
  {
    float st[2] = {s.lp1, s.lp2};
    soften(car, static_cast<int>(n), 1.0f - fm_expf(-6.2831853f * soft / kSr), st, g0, dgn, form, s.fo);
    s.lp1 = st[0], s.lp2 = st[1];
  }
  // the vocoder: the voice's 12 bands followed, the carrier's 12 at their levels
  const int mt = mod_of(s.k[0]);
  if (mt > 0 && s.has_mod) {
    if (s.agc <= 0.0f) s.agc = 100.0f;
    const float* mod = m->mod;
    float x2[kMaxN / 2], x8[kMaxN / 8];
    const int n2 = static_cast<int>(n / 2u);
    const int n8 = bring_down(mod, n2, x2, x8, &m->dec[1], &m->dn);
    const float rs = s.k[5] > 0.98f ? 0.0f : 1.0f / (follow_s(s.k[5]) * kSr);
    const float att2 = 1.0f - fm_expf(-2.0f / (0.002f * kSr)), att8 = 1.0f - fm_expf(-8.0f / (0.002f * kSr));
    const float rel2 = 1.0f - fm_expf(-2.0f * rs), rel8 = 1.0f - fm_expf(-8.0f * rs);
    float lv0[kBands], lv1[kBands], e0[kBands], e1[kBands];
    listen(m, x2, n2, x8, n8, att2, rel2, att8, rel8, lv0, lv1);
    // the voice's level kept out of it (an automatic gain: its bands over their rms, a voice under -40 dB brought down
    // with it): a whisper, a shout, a drum loop, the carrier as loud; its shape alone moves it (a voice in one band:
    // that band 3.5 times the carrier's, a noise's flat: all of it as it is)
    float sq = 0.0f;
    for (int i = 0; i < kBands; ++i) sq += lv1[i] * lv1[i];
    const float r = fm_sqrtf(sq * (1.0f / static_cast<float>(kBands)));
    const float a0 = s.agc, aim = 1.0f / (r > 0.01f ? r : 0.01f);
    s.agc += (aim - s.agc) * 0.15f;     // (~20 ms)
    // SHIFT: a band of the voice another's (between two: both), up to 3 bands (an octave and a half) lower or higher
    const float shift = (2.0f * s.k[6] - 1.0f) * 3.0f;
    for (int i = 0; i < kBands; ++i) {
      float src = clampf(static_cast<float>(i) - shift, 0.0f, static_cast<float>(kBands) - 1.001f);
      const int si = static_cast<int>(src);
      const float fr = src - static_cast<float>(si);
      e0[i] = a0 * band_w[i] * (lv0[si] + (lv0[si + 1] - lv0[si]) * fr);
      e1[i] = s.agc * band_w[i] * (lv1[si] + (lv1[si + 1] - lv1[si]) * fr);
    }
    speak(m, car, out, static_cast<int>(n), e0, e1);
    const float air = s.k[7] * s.k[7];   // AIR: the voice's highs let through
    if (air > 0.0f) add_air(mod, out, static_cast<int>(n), 1.5f * air, s.hp);
  } else {
    for (uint32_t j = 0; j < n; ++j) out[j] = car[j];
  }
  // the ensemble: three taps on a short line (6 ms, swept), a slow LFO and a fast one, each tap a third of a turn on
  {
    const float ens = s.k[2], l0 = s.lfo[0], l1 = s.lfo[1];
    s.lfo[0] += static_cast<float>(n) * 0.63f / kSr;
    s.lfo[1] += static_cast<float>(n) * 5.9f / kSr;
    s.lfo[0] -= s.lfo[0] >= 1.0f ? 1.0f : 0.0f;
    s.lfo[1] -= s.lfo[1] >= 1.0f ? 1.0f : 0.0f;
    float dl[3], ddl[3];                // (each tap's read place behind the write, samples: from, its step)
    for (int k = 0; k < 3; ++k) {
      const float o = static_cast<float>(k) * (1.0f / 3.0f);
      float p = l0 + o, q = l1 + o, p1 = s.lfo[0] + o, q1 = s.lfo[1] + o;
      p -= p >= 1.0f ? 1.0f : 0.0f;
      q -= q >= 1.0f ? 1.0f : 0.0f;
      p1 -= p1 >= 1.0f ? 1.0f : 0.0f;
      q1 -= q1 >= 1.0f ? 1.0f : 0.0f;
      const float d0 = 265.0f + ens * (75.0f * sin_cyc(p) + 9.0f * sin_cyc(q));
      const float d1 = 265.0f + ens * (75.0f * sin_cyc(p1) + 9.0f * sin_cyc(q1));
      dl[k] = static_cast<float>(kLine) - d0;   // (read = write + kLine - delay: never under 0)
      ddl[k] = (d0 - d1) / static_cast<float>(n);
    }
    s.wr = ensemble(out, static_cast<int>(n), m->line, s.wr, dl, ddl, 1.0f - 0.45f * ens, 0.25f * ens);
  }
  mi_finish(out, n, 1.0f, &s.dcx, &s.dcy);
}

const char* voc_knob_name(int k) {
  static const char* const kN[8] = {"Mod", "Tone", "Ensemble", "Attack", "Release", "Follow", "Shift", "Air"};
  return k >= 0 && k < 8 ? kN[k] : "";
}

void voc_defaults(float* k) {           // no voice yet (Off: a string machine), strings, the ensemble up, a pad's
  static const float kD[8] = {0.1f, 0.5f, 0.7f, 0.25f, 0.45f, 0.3f, 0.5f, 0.35f};   // envelope
  for (int i = 0; i < 8; ++i) k[i] = kD[i];
}

int voc_knob_steps(int k) { return k == 0 ? 5 : 128; }

MI_COLD void voc_knob_text(int k, float u, char* b, int n) {
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  switch (k) {
    case 0: {
      static const char* const kM[5] = {"Off", "T1", "T2", "T3", "T4"};
      return put_s(b, n, &at, kM[mod_of(u)]);
    }
    case 1:                             // TONE: saws, strings, choir, and between
      if (u < 0.03f) return put_s(b, n, &at, "Saws");
      if (u > 0.47f && u < 0.53f) return put_s(b, n, &at, "Strings");
      if (u > 0.97f) return put_s(b, n, &at, "Choir");
      put_s(b, n, &at, u < 0.5f ? "Str " : "Choir ");
      put_i(b, n, &at, static_cast<int>((u < 0.5f ? u : u - 0.5f) * 200.0f + 0.5f));
      return put_s(b, n, &at, "%");
    case 3: return put_time(b, n, &at, attack_s(u));
    case 4: return put_time(b, n, &at, release_s(u));
    case 5:
      if (u > 0.98f) return put_s(b, n, &at, "Hold");
      return put_time(b, n, &at, follow_s(u));
    case 6: {                           // SHIFT: bands down or up (a band: half an octave)
      const int q = static_cast<int>((2.0f * u - 1.0f) * 30.0f + (u < 0.5f ? -0.5f : 0.5f));
      if (!q) return put_s(b, n, &at, "0");
      put_s(b, n, &at, q > 0 ? "+" : "-");
      put_i(b, n, &at, (q > 0 ? q : -q) / 10);
      put_s(b, n, &at, ".");
      return put_i(b, n, &at, (q > 0 ? q : -q) % 10);
    }
    default:                            // ENSEMBLE, AIR: 0..100
      put_i(b, n, &at, static_cast<int>(u * 100.0f + 0.5f));
      return;
  }
}

}  // extern "C"
