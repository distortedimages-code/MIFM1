// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's CHOIR (0.16.43): a singing voice, a soprano alone or a choir of four (soprano, alto, tenor, bass), in the
// way of IRCAM's CHANT (Xavier Rodet, Yves Potard, Jean-Baptiste Barriere; Computer Music Journal 8(3), 1984): a
// glottal source into the vocal tract's five formants, in parallel, each at the frequency, bandwidth and level the
// Csound manual gives it for that voice and that vowel (its appendix D, "Formant Values": the table that goes with its
// fof, after CHANT).
//
// A part: the glottis' flow (Rosenberg's: a half cosine opening, a quarter cosine closing, then shut), its
// derivative the source (what the lips radiate), its closure a step band-limited by a polyBLEP (the voice's highs, no
// alias); its open quotient and its closure's speed from TIMBRE (soft and dark, open long and closing slowly; pressed
// and bright, open short and closing fast); BREATH a noise, its lows cut, louder while the glottis is open, into the
// formants (none by default). The five formants: band passes at the table's levels, each raised by the source's tilt where
// it is (the source falls 6 dB an octave past its corner: the table's levels are the spectrum's, not the filters'); the
// first raised to the note when the note goes over it (a soprano's resonance tuning: her high notes as loud). The
// pitch: a vibrato (~5.6 Hz, coming in after the note's start), two jitters (a fast one, 2 cents; a slow drift, 3
// cents, 10 at most), a scoop up into each new note; a new note while one sings (legato: the keys slid over, a tie)
// glides to it, GLIDE's time, without a new attack. Its level held (the formants' power, slowly: a vowel, a pitch, a
// TIMBRE as loud as another).
//
// The parts of a choir: each its own singer (its register's formants, its vibrato's rate, a few cents off, a few ms
// late), the engine's notes (a step's chord, or the note harmonized in the scale: a third and a fifth under it, the
// bass an octave under that). It sings two octaves over the track's note (C3: a soprano's C5).
//
// Eight knobs (k: 0..1): VOWEL (A, E, I, O, U, between them morphed), VOICES (Solo, Duo, Trio, Choir), TIMBRE, BREATH,
// GLIDE (5 ms .. 0.6 s), VIBRATO (its depth: 0 .. 100 cents), ATTACK (5 ms .. 1.5 s), RELEASE (30 ms .. 4 s).
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f;
const int kParts = 4, kForm = 5, kSub = 16;
const float kUp = 24.0f;                // (two octaves over the track's note)
const float kOut = 0.14f;               // its level (its formants' held at 1): 0.14 rms alone, 0.12 four

// the formants (the Csound manual's appendix D, "Formant Values"): soprano, alto, tenor, bass; the vowels a e i o u;
// each formant its frequency (Hz), its bandwidth (Hz), its level (dB)
struct Fm {
  uint16_t f;
  uint8_t bw;
  int8_t db;
};
const Fm kTab[4][5][kForm] = {
    {{{800, 80, 0}, {1150, 90, -6}, {2900, 120, -32}, {3900, 130, -20}, {4950, 140, -50}},
     {{350, 60, 0}, {2000, 100, -20}, {2800, 120, -15}, {3600, 150, -40}, {4950, 200, -56}},
     {{270, 60, 0}, {2140, 90, -12}, {2950, 100, -26}, {3900, 120, -26}, {4950, 120, -44}},
     {{450, 70, 0}, {800, 80, -11}, {2830, 100, -22}, {3800, 130, -22}, {4950, 135, -50}},
     {{325, 50, 0}, {700, 60, -16}, {2700, 170, -35}, {3800, 180, -40}, {4950, 200, -60}}},
    {{{800, 80, 0}, {1150, 90, -4}, {2800, 120, -20}, {3500, 130, -36}, {4950, 140, -60}},
     {{400, 60, 0}, {1600, 80, -24}, {2700, 120, -30}, {3300, 150, -35}, {4950, 200, -60}},
     {{350, 50, 0}, {1700, 100, -20}, {2700, 120, -30}, {3700, 150, -36}, {4950, 200, -60}},
     {{450, 70, 0}, {800, 80, -9}, {2830, 100, -16}, {3500, 130, -28}, {4950, 135, -55}},
     {{325, 50, 0}, {700, 60, -12}, {2530, 170, -30}, {3500, 180, -40}, {4950, 200, -64}}},
    {{{650, 80, 0}, {1080, 90, -6}, {2650, 120, -7}, {2900, 130, -8}, {3250, 140, -22}},
     {{400, 70, 0}, {1700, 80, -14}, {2600, 100, -12}, {3200, 120, -14}, {3580, 120, -20}},
     {{290, 40, 0}, {1870, 90, -15}, {2800, 100, -18}, {3250, 120, -20}, {3540, 120, -30}},
     {{400, 40, 0}, {800, 80, -10}, {2600, 100, -12}, {2800, 120, -12}, {3000, 120, -26}},
     {{350, 40, 0}, {600, 60, -20}, {2700, 100, -17}, {2900, 120, -14}, {3300, 120, -26}}},
    {{{600, 60, 0}, {1040, 70, -7}, {2250, 110, -9}, {2450, 120, -9}, {2750, 130, -20}},
     {{400, 40, 0}, {1620, 80, -12}, {2400, 100, -9}, {2800, 120, -12}, {3100, 120, -18}},
     {{250, 60, 0}, {1750, 90, -30}, {2600, 100, -16}, {3050, 120, -22}, {3340, 120, -28}},
     {{400, 40, 0}, {750, 80, -11}, {2400, 100, -21}, {2600, 120, -20}, {2900, 120, -40}},
     {{350, 40, 0}, {600, 80, -20}, {2400, 100, -32}, {2675, 120, -28}, {2950, 120, -36}}}};

// the parts' singers: their vibrato's rate (Hz), how far off (cents), how late (ms), their formants a little apart
const float kRate[kParts] = {5.6f, 5.25f, 5.85f, 5.1f};
const float kCents[kParts] = {0.0f, -6.0f, 5.0f, -4.0f};
const float kLate[kParts] = {0.0f, 9.0f, 16.0f, 24.0f};
const float kSize[kParts] = {1.0f, 0.985f, 1.015f, 0.99f};
const float kLevel[kParts] = {1.0f, 0.8f, 0.75f, 0.85f};

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
inline uint32_t xs(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline float bip(uint32_t& s) { return static_cast<float>(xs(s) >> 8) * (2.0f / 16777216.0f) - 1.0f; }
inline float glide_s(float u) { return 0.005f + 0.6f * u * u; }     // GLIDE: 5 ms .. 0.6 s (a time constant)
inline float attack_s(float u) { return 0.005f * fm_exp2f(u * 8.23f); }   // ATTACK: 5 ms .. 1.5 s
inline float release_s(float u) { return 0.03f * fm_exp2f(u * 7.06f); }   // RELEASE: 30 ms .. 4 s
inline float vib_cents(float u) { return 100.0f * u * u; }         // VIBRATO: its depth, 0 .. 100 cents
inline int voices_of(float u) {
  int i = static_cast<int>(clampf(u, 0.0f, 1.0f) * 4.0f);
  return i > 3 ? 4 : i + 1;
}

struct Part {
  float ph;                             // the glottis' phase (0..1)
  float semi, target;                   // its pitch (semitones), where it goes
  float vph, venv;                      // the vibrato: its phase, its depth's envelope (0..1)
  float jf, js;                         // the jitters: fast, slow
  float scoop;                          // a new note's scoop (semitones, back to 0)
  float env;                            // its loudness
  float prev;                           // the source a sample late (the polyBLEP's)
  float pw, ag;                         // its level held: the formants' power (~0.3 s), the gain made of it
  float hp;                             // the breath's low cut
  float x1, x2;                         // the source's last two (the formants' zeros, shared: 1 - z^-2)
  float s[kForm][2];                    // the formants' last two outputs
  int32_t wait, age;                    // its start's delay (samples), its time since (samples)
};
struct Choir {
  float k[8];
  float vowel;                          // VOWEL slewed (the formants move, never jump)
  int gate, n;                          // the gate, the parts singing
  uint32_t rnd;
  int alive;
  Part p[kParts];
};
static_assert(sizeof(Choir) <= MF_STATE_BYTES, "CHOIR's state");
struct ChoirAt {
  Choir& operator[](int t) const { return *static_cast<Choir*>(mf_state(t)); }
};
const ChoirAt choir = {};

inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }
inline float quarter_sin(float x) {     // sin(pi x / 2) on 0..1 (a fifth-order odd polynomial: 5e-5 off at most)
  const float x2 = x * x;
  return x * (1.5707963f + x2 * (-0.6459641f + x2 * 0.0796926f));
}

// a part's five formants now: the table's at its register r (0 soprano .. 3 bass, between two blended) and the vowel v
// (0..4), its size; the first raised to the note's (resonance tuning); each a band pass (its peak at 1) at its level,
// raised by the source's tilt there (6 dB an octave over the first: the source's slope, measured, -6.2 dB/oct at any
// TIMBRE), every other one upside down (Klatt's parallel branch: two neighbours' skirts out of phase between them
// would cancel; so, each formant's peak within 1 or 2 dB of the table's, but the weakest under a neighbour's skirt)
struct Coef {
  float b0[kForm], a1[kForm], a2[kForm];
};
__attribute__((noinline, minsize)) void formants(Coef& c, float r, float v, float size, float f0) {
  const int ri = r >= 3.0f ? 2 : static_cast<int>(r), vi = v >= 4.0f ? 3 : static_cast<int>(v);
  const float rf = r - static_cast<float>(ri), vf = v - static_cast<float>(vi);
  float f[kForm], bw[kForm], db[kForm];
  for (int i = 0; i < kForm; ++i) {
    float fa = 0, ba = 0, da = 0;
    for (int q = 0; q < 4; ++q) {      // (the four corners: two registers, two vowels)
      const float w = (q & 1 ? rf : 1.0f - rf) * (q & 2 ? vf : 1.0f - vf);
      const Fm& m = kTab[ri + (q & 1)][vi + (q >> 1)][i];
      fa += w * static_cast<float>(m.f), ba += w * static_cast<float>(m.bw), da += w * static_cast<float>(m.db);
    }
    f[i] = fa * size, bw[i] = ba, db[i] = da;
  }
  const float t1 = f0 * 1.04f;          // (the first under the note: raised to it, smoothly)
  if (t1 > 0.7f * f[0]) f[0] = fm_sqrtf(f[0] * f[0] + t1 * t1 - 0.49f * f[0] * f[0]);
  if (f[1] < f[0] * 1.3f) f[1] = f[0] * 1.3f;
  for (int i = 0; i < kForm; ++i) {
    const float x = clampf(f[i], 60.0f, 9000.0f) * (1.0f / kSr);
    const float sn = fm_sin_turns(x), cs = fm_sin_turns(x + 0.25f);
    const float al = sn * bw[i] / (2.0f * f[i]), ia = 1.0f / (1.0f + al);
    c.b0[i] = al * ia * fm_db2lin(db[i]) * (f[i] / f[0]) * (i & 1 ? -1.0f : 1.0f);
    c.a1[i] = -2.0f * cs * ia;
    c.a2[i] = (1.0f - al) * ia;
  }
}

// (the formants: band passes all with the same zeros, at DC and Nyquist: 1 - z^-2 once on the source, then each its
// two poles)

// the register a pitch sings in, alone: a soprano from E4 (64) up, an alto at A3, a tenor at D3, a bass under G2
inline float register_of(float semi) {
  return semi >= 64.0f ? 0.0f : semi >= 57.0f ? (64.0f - semi) / 7.0f : semi >= 50.0f ? 1.0f + (57.0f - semi) / 7.0f
         : semi >= 43.0f ? 2.0f + (50.0f - semi) / 7.0f : 3.0f;
}

// m samples of a part, added into out: the source (its shape: ta the opening's length, tc the closing's, over a
// period), the breath, the formants, its loudness
__attribute__((noinline)) void sing(Part& p, const Coef& c, float* out, uint32_t m, float dph, float ta, float tc,
                                    float breath, float e, float de, float lvl, float kp, uint32_t& rnd) {
  // (its level held: as the harmonics fall in the formants or between them, from a vowel, a pitch, a TIMBRE to
  // another, the formants' power over ~0.3 s (slower than the vibrato: the swell it gives each harmonic kept) brought
  // to 1 (it is 0.3 .. 1.7 without: 15 dB apart), its gain between x0.4 and x4, gliding over the samples)
  const float ag0 = p.ag, ag1 = clampf(1.0f / fm_sqrtf(p.pw + 1e-9f), 0.4f, 4.0f);
  const float ag_aim = ag0 + (ag1 - ag0) * 0.3f, dag = (ag_aim - ag0) / static_cast<float>(m);
  float ag = ag0, pw = p.pw;
  const float te = ta + tc, a1 = 1.5707963f / ta, a2 = 1.5707963f / tc, ita = 2.0f / ta, itc = 1.0f / tc;
  float ph = p.ph, prev = p.prev, hp = p.hp, x1 = p.x1, x2 = p.x2;
  float o10 = p.s[0][0], o20 = p.s[0][1], o11 = p.s[1][0], o21 = p.s[1][1], o12 = p.s[2][0], o22 = p.s[2][1];
  float o13 = p.s[3][0], o23 = p.s[3][1], o14 = p.s[4][0], o24 = p.s[4][1];
  for (uint32_t n = 0; n < m; ++n) {
    const float p0 = ph;
    ph += dph;
    ph -= ph >= 1.0f ? 1.0f : 0.0f;
    float x = ph < ta ? a1 * quarter_sin(1.0f - fm_fabsf(ph * ita - 1.0f)) : ph < te ? -a2 * quarter_sin((ph - ta) * itc)
                                                                                   : 0.0f;
    if (p0 < te && (ph >= te || ph < p0)) {   // the closure: a step of a2, band-limited (this sample and the last)
      const float d = ((ph < p0 ? ph + 1.0f : ph) - te) / dph;
      prev += a2 * 0.5f * d * d;
      x -= a2 * 0.5f * (1.0f - d) * (1.0f - d);
    }
    float src = prev;
    prev = x;
    if (breath > 0.0f) {                // the breath: its lows cut (~800 Hz), louder while the glottis is open
      const float w = bip(rnd);
      hp += 0.11f * (w - hp);
      src += breath * (w - hp) * (ph < te ? 1.0f : 0.35f);
    }
    const float z = src - x2;           // (the zeros)
    x2 = x1, x1 = src;
    float o;
    o = c.b0[0] * z - c.a1[0] * o10 - c.a2[0] * o20, o20 = o10, o10 = o;
    float y = o;
    o = c.b0[1] * z - c.a1[1] * o11 - c.a2[1] * o21, o21 = o11, o11 = o, y += o;
    o = c.b0[2] * z - c.a1[2] * o12 - c.a2[2] * o22, o22 = o12, o12 = o, y += o;
    o = c.b0[3] * z - c.a1[3] * o13 - c.a2[3] * o23, o23 = o13, o13 = o, y += o;
    o = c.b0[4] * z - c.a1[4] * o14 - c.a2[4] * o24, o24 = o14, o14 = o, y += o;
    pw += (y * y - pw) * kp;
    e += de;
    ag += dag;
    out[n] += y * ag * e * lvl;
  }
  p.s[0][0] = o10, p.s[0][1] = o20, p.s[1][0] = o11, p.s[1][1] = o21, p.s[2][0] = o12, p.s[2][1] = o22;
  p.s[3][0] = o13, p.s[3][1] = o23, p.s[4][0] = o14, p.s[4][1] = o24;
  p.ph = ph, p.prev = prev, p.hp = hp, p.pw = pw, p.ag = ag, p.x1 = x1, p.x2 = x2;
}

}  // namespace

extern "C" {

MI_COLD void choir_reset(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Choir& c = choir[t];
  uint8_t* raw = reinterpret_cast<uint8_t*>(&c);
  for (uint32_t i = 0; i < sizeof(Choir); ++i) raw[i] = 0;
  c.rnd = 0x9E3779B9u * static_cast<uint32_t>(t + 11) | 1u;
  float d[8];
  choir_defaults(d);
  for (int i = 0; i < 8; ++i) c.k[i] = d[i];
  c.n = 1;
  for (int i = 0; i < kParts; ++i) {
    c.p[i].semi = c.p[i].target = 72.0f;
    c.p[i].ph = 0.25f * static_cast<float>(i);
    c.p[i].vph = 0.37f * static_cast<float>(i);
  }
}

MI_COLD void choir_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) choir_reset(t);
  mf_forget();
}

MI_COLD void choir_silence(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Choir& c = choir[t];
  c.alive = 0;
  for (int i = 0; i < kParts; ++i) {
    Part& p = c.p[i];
    p.env = p.prev = p.hp = 0.0f;
    for (int f = 0; f < kForm; ++f) p.s[f][0] = p.s[f][1] = 0.0f;
  }
}

int choir_voices(float u) { return voices_of(u); }

// the parts' notes (the soprano's first; n: the knob's or fewer), the knobs
MI_COLD void choir_set(int t, const float* notes, int n, const float* k) {
  if (t < 0 || t >= MF_TRACKS || n < 1) return;
  Choir& c = choir[t];
  for (int i = 0; i < 8; ++i) c.k[i] = clampf(k[i], 0.0f, 1.0f);
  n = n > kParts ? kParts : n;
  for (int i = 0; i < kParts; ++i) {
    Part& p = c.p[i];
    p.target = notes[i < n ? i : n - 1] + kUp + kCents[i] * 0.01f;
    if (p.env < 0.003f && !c.gate) p.semi = p.target;   // (silent: there at its start)
  }
  c.n = n;
}

// a rise: each part sounding glides to its note (legato), each silent one starts (its note at once, its scoop, its
// vibrato later, a little late in a choir)
MI_COLD void choir_gate(int t, int g) {
  if (t < 0 || t >= MF_TRACKS) return;
  Choir& c = choir[t];
  if (g && !c.gate) {
    for (int i = 0; i < kParts; ++i) {
      Part& p = c.p[i];
      if (p.env > 0.02f) continue;      // (legato)
      p.semi = p.target;
      p.scoop = -0.45f;
      p.age = 0;
      p.venv = 0.0f;
      p.wait = c.n > 1 ? static_cast<int32_t>(kLate[i] * 0.001f * kSr) : 0;
    }
    c.alive = 1;
  }
  c.gate = g;
}

MI_COLD void choir_render(int t, float* out, uint32_t n) {   // (its glue small: the samples' loop, sing, at -O2)
  if (t < 0 || t >= MF_TRACKS) return;
  for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
  Choir& c = choir[t];
  if (!c.alive || !n) return;
  const float* k = c.k;
  const float tb = k[2], br = k[3];
  // the glottis: open 0.78 of a period and closing over 37 % of it, soft (TIMBRE 0: its first harmonic 10 dB over the
  // second, measured), to 0.5 and 22 % pressed (1: the two as loud, the highs 12 dB up); breath opens it more
  const float oq = clampf(0.78f - 0.28f * tb + 0.08f * br, 0.4f, 0.92f);
  const float tc = oq * (0.37f - 0.15f * tb), ta = oq - tc;
  const float breath = 1.8f * br * br / (tc * 1.08f + 0.4f);   // (against the source's closure: as loud at any TIMBRE)
  c.vowel += (clampf(k[0], 0.0f, 1.0f) * 4.0f - c.vowel) * (1.0f - fm_expf(-static_cast<float>(n) / (0.04f * kSr)));
  const float kg = 1.0f - fm_expf(-static_cast<float>(kSub) / (glide_s(k[4]) * kSr));
  // (the loudness: a one-pole up (ATTACK) and down (RELEASE), its value after m samples (1 - k)^m away from where it
  // goes: log2(1 - k) a sample)
  const float la = -1.442695f / (attack_s(k[6]) * kSr), lr = -1.442695f / (release_s(k[7]) * kSr);
  const float vib = vib_cents(k[5]) * 0.01f;
  const float lvl_all = 1.0f / fm_sqrtf(static_cast<float>(c.n));
  int any = 0;
  for (int i = 0; i < kParts; ++i) {
    Part& p = c.p[i];
    const int on = i < c.n;
    if (!on && p.env < 1e-5f) continue;
    const float r = c.n > 1 ? static_cast<float>(i) : register_of(p.semi);
    Coef cf;                            // (its formants: once a call, at its pitch then)
    formants(cf, r, c.vowel, kSize[i], note_hz(clampf(p.semi + p.scoop, 28.0f, 92.0f)));
    for (uint32_t at = 0; at < n;) {
      uint32_t m = n - at > static_cast<uint32_t>(kSub) ? kSub : n - at;
      if (p.wait > 0) {                 // (a choir's part: a few ms late)
        const uint32_t w = static_cast<uint32_t>(p.wait) < m ? static_cast<uint32_t>(p.wait) : m;
        p.wait -= static_cast<int32_t>(w);
        at += w;
        continue;
      }
      // the pitch: glided, its scoop, its vibrato (in after 0.18 s, over 0.35 s; half while gliding), its jitters
      p.semi += (p.target - p.semi) * kg;
      p.scoop *= 0.68f;                 // (~40 ms)
      p.age += static_cast<int32_t>(m);
      const float since = static_cast<float>(p.age) * (1.0f / kSr) - 0.18f;
      float ve = since <= 0.0f ? 0.0f : since >= 0.35f ? 1.0f : since * (1.0f / 0.35f);
      ve = ve * ve * (3.0f - 2.0f * ve);
      if (fm_fabsf(p.target - p.semi) > 0.3f) ve *= 0.5f;
      p.venv += (ve - p.venv) * 0.05f;
      p.jf += 0.034f * (bip(c.rnd) - p.jf);
      p.js += 0.0016f * (bip(c.rnd) - p.js);
      p.vph += kRate[i] * (1.0f + 0.6f * p.js) * static_cast<float>(m) * (1.0f / kSr);
      p.vph -= p.vph >= 1.0f ? 1.0f : 0.0f;
      const float semi = p.semi + p.scoop + vib * p.venv * fm_sin_turns(p.vph) + 0.5f * p.jf + 2.0f * p.js;
      const float f0 = note_hz(clampf(semi, 28.0f, 92.0f));
      // its loudness over these samples: up to 1 while the gate is (ATTACK), down after (RELEASE); a little shimmer
      const float aim = c.gate && on ? 1.0f + 0.25f * p.jf : 0.0f;
      const float e0 = p.env;
      p.env = aim + (e0 - aim) * fm_exp2f((aim > e0 ? la : lr) * static_cast<float>(m));
      sing(p, cf, out + at, m, f0 * (1.0f / kSr), ta, tc, breath, e0, (p.env - e0) / static_cast<float>(m),
           kLevel[i] * lvl_all * kOut, p.age < 0.15f * kSr ? 1.0f / (0.02f * kSr) : 1.0f / (0.3f * kSr), c.rnd);
      at += m;
    }
    for (int f = 0; f < kForm; ++f)     // (no subnormals: slow)
      for (int j = 0; j < 2; ++j) p.s[f][j] = fm_fabsf(p.s[f][j]) < 1e-20f ? 0.0f : p.s[f][j];
    any |= p.env > 1e-5f || (c.gate && on);
  }
  c.alive = any;
}

const char* choir_knob_name(int k) {
  static const char* const kN[8] = {"Vowel", "Voices", "Timbre", "Breath", "Glide", "Vibrato", "Attack", "Release"};
  return k >= 0 && k < 8 ? kN[k] : "";
}

MI_COLD void choir_defaults(float* k) {         // a soprano alone, on A, her vibrato, no breath, a soft start
  static const float kD[8] = {0.0f, 0.0f, 0.5f, 0.0f, 0.3f, 0.6f, 0.3f, 0.35f};
  for (int i = 0; i < 8; ++i) k[i] = kD[i];
}

int choir_knob_steps(int k) { return k == 1 ? 4 : 128; }

MI_COLD void choir_knob_text(int k, float u, char* b, int n) {
  if (n < 2) return;
  int i = 0;
  auto put = [&](const char* s) {
    while (*s && i < n - 1) b[i++] = *s++;
  };
  auto num = [&](int v) {
    char d[8];
    int m = 0;
    do d[m++] = static_cast<char>('0' + v % 10), v /= 10;
    while (v && m < 7);
    while (m && i < n - 1) b[i++] = d[--m];
  };
  u = clampf(u, 0.0f, 1.0f);
  if (k == 0) {                         // A, then A-E between A and E, ...
    static const char kV[5] = {'A', 'E', 'I', 'O', 'U'};
    const float v = u * 4.0f;
    const int a = static_cast<int>(v + 0.5f), lo = v >= 4.0f ? 3 : static_cast<int>(v);
    if (fm_fabsf(v - static_cast<float>(a)) < 0.15f) {
      b[i++] = kV[a];
    } else {
      b[i++] = kV[lo];
      put("-");
      if (i < n - 1) b[i++] = kV[lo + 1];
    }
  } else if (k == 1) {
    static const char* const kS[4] = {"Solo", "Duo", "Trio", "Choir"};
    put(kS[voices_of(u) - 1]);
  } else if (k == 3 && u < 0.005f) {
    put("Off");
  } else if (k == 5) {
    if (u < 0.005f) put("Off");
    else num(static_cast<int>(vib_cents(u) + 0.5f)), put(" ct");
  } else if (k == 4 || k == 6 || k == 7) {
    const float s = k == 4 ? glide_s(u) : k == 6 ? attack_s(u) : release_s(u);
    if (s < 1.0f) {
      num(static_cast<int>(s * 1000.0f + 0.5f)), put(" ms");
    } else {
      const int c = static_cast<int>(s * 10.0f + 0.5f);
      num(c / 10), put("."), num(c % 10), put(" s");
    }
  } else {
    num(static_cast<int>(u * 100.0f + 0.5f));
  }
  b[i] = 0;
}

}  // extern "C"
