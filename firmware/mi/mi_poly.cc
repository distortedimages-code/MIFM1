// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's POLY (0.12): a small polyphonic analog synthesizer, after Roland's SH-101 (1982: one oscillator, its saw,
// its pulse and its sub mixed, a four-pole filter, one envelope for both) and the S-1 (2023), which made it four voices
// with a chord memory. Four voices a track.
//
// A voice: the oscillator (a saw and a pulse, crossfaded by WAVE, its width narrowing, then moved by its own slow LFO:
// PWM), its sub (a square an octave under, then two, then two octaves under at 25 %: the SH-101's three, flip-flops on
// the oscillator's wraps), every edge's alias taken off (polyBLEP, a sample late: the step's both halves where they
// fall); the filter, the IR3109's four poles (Zavalishin's trapezoidal one-poles, the loop solved at once, its input
// saturated: the resonance squeals, then sings), its cutoff following the keyboard; one envelope on the filter and the
// level (ENV: to the right its depth on the cutoff; to the left a swell, the attack up to 2 s, the filter opening with
// it). DECAY: the envelope's fall (Hold: held while the gate is).
//
// The chords (CHORD), a trig playing several voices: to the right a chord memory (parallel chords: the note and its
// fifth, a minor ninth...); to the left the harmony (PADS' chord when a track plays it, else the scale's chord on the
// note: its triad, its seventh, its ninth, spread, its root alone: a bass following the progression). Off: a voice a
// trig, the last ones ringing on under it (four at most).
//
// VOICE: to the left VINTAGE (each voice its own: a few cents off, wandering, its filter and its envelope a little
// apart, as an old polysynth's voices are), to the right UNISON (the four voices on one note, spread: a lead's).
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f;
const int kVoices = 4, kSub = 16;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
inline float sin_cyc(float ph) {        // a sine of a phase in cycles, 0..1 (9th-order fit on the quarter wave)
  float x = ph < 0.5f ? ph : ph - 0.5f, s = ph < 0.5f ? 1.0f : -1.0f;
  x = x < 0.25f ? x : 0.5f - x;
  float z = x * 4.0f, z2 = z * z;
  return s * z * (1.5707963f + z2 * (-0.64596336f + z2 * (0.079688469f + z2 * (-0.0046722113f + z2 * 0.00015081274f))));
}
inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }
inline uint32_t xs(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline float bip(uint32_t& s) { return static_cast<float>(xs(s) >> 8) * (2.0f / 16777216.0f) - 1.0f; }
inline float sat(float x) {             // tanh's rational fit (exact at 0, 1 past 3)
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// CHORD: -7 .. 7. To the right a chord memory's (semitones over the note); to the left the harmony's
const int kChords = 15;
const int8_t kPar[7][4] = {{0, 12, -1, -1}, {0, 7, -1, -1}, {0, 5, 7, -1}, {0, 4, 7, -1},
                           {0, 3, 7, -1},   {0, 3, 7, 10},  {0, 3, 10, 14}};
const char* const kChordName[kChords] = {"Root", "Root5", "Sus", "Open", "9th", "7th", "Triad", "Off",
                                         "Oct",  "5th",   "Sus4", "Major", "Minor", "Min7", "Min9"};
inline int chord_of(float u) {
  int i = static_cast<int>(u * static_cast<float>(kChords));
  return (i < 0 ? 0 : i > kChords - 1 ? kChords - 1 : i) - kChords / 2;
}

struct PVoice {
  float note;                           // its pitch (MIDI)
  float ph, lfo;                        // the oscillator's phase (0..1); its PWM's LFO
  int subn;                             // the sub's divider (counts the wraps, 0..3)
  float p1, p2, carry;                  // the two samples before, the next one's part (an edge's 4 samples: two late)
  float s1, env, s2, a0, s3, att, s4, dec;   // the filter's four states; the envelope: its level, where the attack
                                        // began, its progress, the fall's (apart: the compiler would pack the states)
  float dr, dr_t;                       // its wander (VINTAGE)
  float tol[3];                         // its own tolerances (-1..1): pitch, cutoff, envelope
  float uni;                            // UNISON's place (-1..1)
  int slot;                             // its note in its trig's chord
  int alive, trig, let_go;
  uint32_t age;                         // (the oldest stolen first)
};
struct Poly {
  float k[8];
  float notes[kVoices];                 // this trig's notes
  int n;                                // how many (0: none yet)
  int gate, trig, group;                // the gate; a trig to take; the voices the last one took (bits)
  PVoice v[kVoices];
  uint32_t rnd, clock;
  int dr_n;
  float dcx, dcy;
};
// (its state on a track: the engine's, shared with the other machines, one a track at a time)
static_assert(sizeof(Poly) <= MF_STATE_BYTES, "POLY's state");
struct PolyAt {
  Poly& operator[](int t) const { return *static_cast<Poly*>(mf_state(t)); }
};
const PolyAt poly = {};

inline float t60_s(float u) { return 0.05f * fm_exp2f(u * 8.644f); }   // DECAY: 50 ms .. 20 s (to -60 dB)
inline float cutoff_hz(float u) { return 20.0f * fm_exp2f(u * 10.0f); }  // CUTOFF: 20 Hz .. 20 kHz
inline float reso_k(float u) { return 4.3f * u * u * (3.0f - 2.0f * u); }   // RESO: past 4, it sings by itself
// WAVE: 0 .. 1/4 a saw, the square rising under it; .. 1/2 the square alone; .. 3/4 its width down to 12 %; .. 1 PWM
struct Wave {
  float saw, pul, pw, pwm;
};
inline Wave wave_of(float u) {
  Wave w = {1.0f, 0.0f, 0.5f, 0.0f};
  if (u < 0.25f) w.pul = u * 4.0f;
  else if (u < 0.5f) w.saw = 1.0f - (u - 0.25f) * 4.0f, w.pul = 1.0f;
  else if (u < 0.75f) w.saw = 0.0f, w.pul = 1.0f, w.pw = 0.5f - (u - 0.5f) * 4.0f * 0.38f;
  else w.saw = 0.0f, w.pul = 1.0f, w.pw = 0.3f, w.pwm = (u - 0.75f) * 4.0f * 0.21f;
  return w;
}
// SUB: 0 .. 1/2 an octave under, rising; .. 3/4 to two octaves under; .. 1 to two under at 25 % (the SH-101's three)
inline void sub_of(float u, float* a) {
  a[0] = a[1] = a[2] = 0.0f;
  if (u < 0.5f) a[0] = u * 2.0f;
  else if (u < 0.75f) a[0] = 1.0f - (u - 0.5f) * 4.0f, a[1] = (u - 0.5f) * 4.0f;
  else a[1] = 1.0f - (u - 0.75f) * 4.0f, a[2] = (u - 0.75f) * 4.0f;
}
inline float sub_val(int c, const float* a) {   // (the divider at c: -1 octave its bit 0, -2 its bit 1, 25 %: c == 0)
  return a[0] * ((c & 1) ? -1.0f : 1.0f) + a[1] * ((c & 2) ? -1.0f : 1.0f) + a[2] * (c == 0 ? 1.5f : -0.5f);
}

// the harmony's chord (the left of CHORD): its root (semitones over the scale's root) and the scale, its tones stacked
int scale_notes(uint16_t mask, int* s) {
  int n = 0;
  for (int i = 0; i < 12; ++i)
    if ((mask >> i) & 1u) s[n++] = i;
  if (!n) s[n++] = 0;
  return n;
}
inline int tone(const int* s, int n, int k) {   // the scale's k-th note from its root (semitones, octaves past it)
  int o = k >= 0 ? k / n : -((-k + n - 1) / n);
  return s[k - o * n] + 12 * o;
}

}  // namespace

extern "C" {

int poly_notes(float u, float note, const int* pc, int pn, uint16_t mask, int root, float* out) {
  const int c = chord_of(u);
  if (c == 0) {
    out[0] = note;
    return 1;
  }
  if (c > 0) {                          // a chord memory's: parallel
    int n = 0;
    for (int i = 0; i < 4; ++i)
      if (kPar[c - 1][i] >= 0) out[n++] = note + static_cast<float>(kPar[c - 1][i]);
    return n;
  }
  // the harmony: PADS' chord (its root, its third and fifth), else the scale's chord at the note (Off: major)
  if (!mask || mask == 0xFFFu) mask = 0xAB5u;
  int s[12], ns = scale_notes(mask, s);
  int rel, deg = 0;                     // the chord's root (semitones over the scale's root, 0..11), its degree
  if (pn >= 3) rel = ((pc[0] % 12) + 12) % 12;
  else rel = ((static_cast<int>(note + 0.5f) - root) % 12 + 12) % 12;
  for (int i = 0; i < ns; ++i)
    if (s[i] <= rel) deg = i;           // (the scale's note at it or under it)
  if (pn < 3) rel = s[deg];
  int third = tone(s, ns, deg + 2) - tone(s, ns, deg), fifth = tone(s, ns, deg + 4) - tone(s, ns, deg);
  int fourth = tone(s, ns, deg + 3) - tone(s, ns, deg), seventh = tone(s, ns, deg + 6) - tone(s, ns, deg);
  int ninth = tone(s, ns, deg + 8) - tone(s, ns, deg);
  if (pn >= 3) third = pc[1] - pc[0], fifth = pc[2] - pc[0];
  // its root at the note or under it (the note: the register)
  float r = static_cast<float>(root + rel);
  while (r > note + 0.5f) r -= 12.0f;
  while (r + 12.0f <= note + 0.5f) r += 12.0f;
  const float T = static_cast<float>(third), F = static_cast<float>(fifth);
  int n = 0;
  out[n++] = r;
  switch (c) {
    case -1: out[n++] = r + T, out[n++] = r + F; break;                                        // Triad
    case -2: out[n++] = r + T, out[n++] = r + F, out[n++] = r + static_cast<float>(seventh); break;   // 7th
    case -3: out[n++] = r + T, out[n++] = r + static_cast<float>(seventh), out[n++] = r + static_cast<float>(ninth); break;
    case -4: out[n++] = r + F, out[n++] = r + T + 12.0f; break;                                 // Open: 1 5 10
    case -5: out[n++] = r + static_cast<float>(fourth), out[n++] = r + F; break;               // Sus
    case -6: out[n++] = r + F; break;                                                           // Root5
    default: break;                                                                             // Root
  }
  return n;
}

void poly_reset(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Poly& p = poly[t];
  uint8_t* raw = reinterpret_cast<uint8_t*>(&p);
  for (uint32_t i = 0; i < sizeof(Poly); ++i) raw[i] = 0;
  p.rnd = 0x2545F491u * static_cast<uint32_t>(t + 3) | 1u;
  float d[8];
  poly_defaults(d);
  for (int i = 0; i < 8; ++i) p.k[i] = d[i];
  for (int i = 0; i < kVoices; ++i) {
    PVoice& v = p.v[i];
    v.note = 48.0f;
    v.att = 1.0f;
    for (int j = 0; j < 3; ++j) v.tol[j] = bip(p.rnd);   // (each voice its own, for good)
    v.lfo = 0.25f * static_cast<float>(i);
    v.uni = -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(kVoices - 1);
  }
}

void poly_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) poly_reset(t);
  mf_forget();
}

void poly_silence(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Poly& p = poly[t];
  p.trig = 0;
  p.group = 0;
  for (int i = 0; i < kVoices; ++i) p.v[i].alive = 0, p.v[i].env = 0.0f, p.v[i].att = 1.0f, p.v[i].trig = 0;
}

void poly_set(int t, const float* notes, int n, const float* k) {
  if (t < 0 || t >= MF_TRACKS) return;
  Poly& p = poly[t];
  for (int i = 0; i < 8; ++i) p.k[i] = clampf(k[i], 0.0f, 1.0f);
  n = n < 1 ? 1 : n > kVoices ? kVoices : n;
  // the voices this trig took follow the notes (a knob, a lock, the progression moving): the same many, in order
  if (n == p.n && p.group)
    for (int i = 0; i < kVoices; ++i)
      if ((p.group >> i) & 1) p.v[i].note = notes[p.v[i].slot < n ? p.v[i].slot : n - 1];
  for (int i = 0; i < n; ++i) p.notes[i] = notes[i];
  p.n = n;
}

void poly_gate(int t, int g) {
  if (t < 0 || t >= MF_TRACKS) return;
  Poly& p = poly[t];
  if (g && !p.gate) p.trig = 1;
  if (!g && p.gate && p.k[3] > 0.98f)   // let go: the held voices fall (the others fall anyway)
    for (int i = 0; i < kVoices; ++i)
      if (((p.group >> i) & 1) && p.v[i].alive) p.v[i].let_go = 1, p.v[i].dec = p.v[i].env, p.v[i].att = 1.0f;
  p.gate = g;
}

int poly_voices(int t) {
  if (t < 0 || t >= MF_TRACKS) return 0;
  int n = 0;
  for (int i = 0; i < kVoices; ++i) n += poly[t].v[i].alive;
  return n;
}

}  // extern "C"

namespace {

// a trig: its notes to voices (the free ones first, then the quietest, the oldest); UNISON: all four on one note
void take(Poly& p) {
  const float vo = 2.0f * p.k[7] - 1.0f;
  const int unison = p.n == 1 && vo > 0.02f;
  const int want = unison ? kVoices : p.n;
  int used = 0;
  ++p.clock;
  for (int j = 0; j < want; ++j) {
    int best = -1;
    float bs = 1e9f;
    for (int i = 0; i < kVoices; ++i) {
      if ((used >> i) & 1) continue;
      const PVoice& v = p.v[i];
      float score = v.alive ? v.env + 1.0f - static_cast<float>(p.clock - v.age) * 1e-6f : 0.0f;
      if (score < bs) bs = score, best = i;
    }
    used |= 1 << best;
    PVoice& v = p.v[best];
    v.slot = unison ? 0 : j;
    v.note = p.notes[v.slot];
    if (!v.alive) {                     // from silence: its states at rest, its phase anywhere (a poly's voices are
      v.ph = 0.5f + 0.5f * bip(p.rnd);  // never in phase)
      v.subn = static_cast<int>(xs(p.rnd) & 3u);
      v.p1 = v.p2 = v.carry = 0.0f;
      v.s1 = v.s2 = v.s3 = v.s4 = 0.0f;
      v.env = 0.0f;
    }
    v.trig = 1;
    v.age = p.clock;
  }
  for (int i = 0; i < kVoices; ++i)     // (the last trig's voices not taken again: held, let go; else ringing on)
    if (p.k[3] > 0.98f && ((p.group >> i) & 1) && !((used >> i) & 1) && p.v[i].alive && !p.v[i].let_go)
      p.v[i].let_go = 1, p.v[i].dec = p.v[i].env, p.v[i].att = 1.0f;
  p.group = used;
}

// an edge of J (the wave's step) t samples ago (0..1): its band-limited step a cubic B-spline's integral, four samples
// wide (the polyBLEP of the third order: its residual on the two samples before, this one and the next; 16 dB less
// alias than the two-point one's, the top octave a little softer)
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

struct VBlock {                         // a voice's settings for a sub-block
  float d, id;                          // its phase's step, 1 / it
  float A, B, P, pw, subv[4];           // the wave: A p + B (the saw), +/- P (the pulse, under pw), the sub at each count
  float G, b;                           // the filter: the one-poles' gain (b: 1 - G)
  float cx, ks;                         // the loop solved: its input u = cx x - ks S (S: the states' part)
};

// n (<= kSub) samples of a voice, added to y (its envelope and level on it): its oscillator into x, then its filter
// (two loops: each its values in the registers)
void voice_block(PVoice& v, const VBlock& c, float e0, float de, float* __restrict y, int n) {
  float x[kSub];
  {
    float ph = v.ph, p1 = v.p1, p2 = v.p2, carry = v.carry;
    int subn = v.subn;
    const float d = c.d, A = c.A, P = c.P, pw = c.pw;
    float sv = c.subv[subn], Bs = c.B + sv;   // (the saw's offset and the sub's, together)
    for (int s = 0; s < n; ++s) {
      // the oscillator: the saw, the pulse, the sub; every edge corrected two samples late
      const float old_ph = ph;
      ph += d;
      float cur;
      if (ph >= 1.0f) {                 // the wrap: the saw falls, the pulse rises, the sub turns
        ph -= 1.0f;
        subn = (subn + 1) & 3;
        const float nsv = c.subv[subn];
        const float J = -A + 2.0f * P + nsv - sv;   // (the saw: A p + B, from A + B down to B)
        sv = nsv;
        Bs = c.B + sv;
        cur = A * ph + Bs + (ph < pw ? P : -P) + carry;
        carry = 0.0f;
        edge(J, ph * c.id, p2, p1, cur, carry);
      } else {
        cur = A * ph + Bs + (ph < pw ? P : -P) + carry;
        carry = 0.0f;
        if (old_ph < pw && ph >= pw) edge(-2.0f * P, (ph - pw) * c.id, p2, p1, cur, carry);   // (the pulse falls;
      }                                 // PW keeps it two steps past the wrap: never the same sample)
      x[s] = p2;                        // (the sample two before: now whole)
      p2 = p1;
      p1 = cur;
    }
    v.ph = ph, v.p1 = p1, v.p2 = p2, v.carry = carry, v.subn = subn;
  }
  // the filter: four one-poles, the loop solved, its input saturated
  float s1 = v.s1, s2 = v.s2, s3 = v.s3, s4 = v.s4, e = e0;
  const float G = c.G, cx = c.cx, ksb = c.ks * c.b;
  for (int s = 0; s < n; ++s) {
    float u = cx * x[s] - ksb * (s4 + G * (s3 + G * (s2 + G * s1)));
    u = fm_minf(fm_maxf(u, -1.5f), 1.5f);
    u -= (4.0f / 27.0f) * u * u * u;    // (a cubic's soft clip: 1 at 1.5, smooth)
    float w = G * (u - s1);             // (each one-pole: v = G (x - s), y = v + s, s = y + v)
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

}  // namespace

extern "C" {

void poly_render(int t, float* out, uint32_t n) {
  if (t < 0 || t >= MF_TRACKS) return;
  Poly& p = poly[t];
  for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
  if (p.trig) {
    p.trig = 0;
    take(p);
  }
  const Wave w = wave_of(p.k[0]);
  float suba[3];
  sub_of(p.k[4], suba);
  const float env_u = 2.0f * p.k[5] - 1.0f, vo = 2.0f * p.k[7] - 1.0f;
  const float vint = vo < 0.0f ? -vo : 0.0f, uni = vo > 0.0f ? vo : 0.0f;
  const float A = env_u < 0.0f ? 0.001f + 2.0f * env_u * env_u : 0.001f;   // ENV to the left: a swell
  const float eamt = env_u >= 0.0f ? 5.0f * env_u : 1.5f * -env_u;          // (octaves the envelope opens)
  const int held = p.k[3] > 0.98f;
  const float tau = t60_s(p.k[3]) * (1.0f / 6.9078f);
  const float kres = reso_k(p.k[2]);
  const float fc0 = cutoff_hz(p.k[1]);
  // the wander: a new aim each voice every 0.3 s
  p.dr_n -= static_cast<int>(n);
  if (p.dr_n <= 0) {
    p.dr_n = 13230;
    for (int i = 0; i < kVoices; ++i) p.v[i].dr_t = bip(p.rnd);
  }
  const float wk = clampf(static_cast<float>(n) * 2e-5f, 0.0f, 1.0f);
  int active = 0;
  for (int i = 0; i < kVoices; ++i) {
    PVoice& v = p.v[i];
    v.dr += (v.dr_t - v.dr) * wk;
    if (v.trig) {
      v.trig = 0;
      v.a0 = v.env;
      v.att = 0.0f;
      v.dec = 1.0f;
      v.let_go = 0;
      v.alive = 1;
    }
    active += v.alive;
  }
  if (!active) return;
  const int chord = p.n > 1, unison = !chord && uni > 0.02f;
  const float norm = 0.6f / fm_sqrtf(static_cast<float>(active > 1 ? active : 1));
  for (int i = 0; i < kVoices; ++i) {
    PVoice& v = p.v[i];
    if (!v.alive) continue;
    // its pitch: VINTAGE (its own few cents, its wander), UNISON (spread), the ensemble of a chord's voices
    float ct = v.tol[0] * 7.0f * vint + v.dr * 5.0f * vint;
    if (unison) ct += v.uni * (3.0f + 27.0f * uni);
    else if (chord) ct += v.uni * 9.0f * uni;
    float f = note_hz(v.note + ct * 0.01f);
    VBlock c;
    c.d = clampf(f / kSr, 1e-5f, 0.45f);
    c.id = 1.0f / c.d;
    c.A = w.saw, c.B = -0.5f * w.saw, c.P = 0.5f * w.pul;   // (each wave +/- 0.5)
    for (int j = 0; j < 4; ++j) c.subv[j] = 0.5f * sub_val(j, suba);
    // its envelope: the attack, then the fall (held: at the top while the gate is; let go: a short release)
    const float tt = 1.0f + 0.15f * v.tol[2] * vint;
    const float dk = v.let_go ? fm_expf(-1.0f / ((0.03f + 0.5f * A) * kSr))
                              : fm_expf(-1.0f / (tau * tt * kSr));
    float dkb = dk;                     // (a sub-block's: dk^16)
    for (int q = 0; q < 4; ++q) dkb *= dkb;
    const float ainc = 1.0f / (A * tt * kSr);
    const int sustain = held && p.gate && !v.let_go && ((p.group >> i) & 1);
    const float kt = (v.note - 60.0f) * (0.7f / 12.0f) + v.tol[1] * 0.25f * vint;   // (the keyboard: 70 %)
    float* y = out;
    for (uint32_t s0 = 0; s0 < n; s0 += kSub) {
      const int m = n - s0 < static_cast<uint32_t>(kSub) ? static_cast<int>(n - s0) : kSub;
      // the envelope at the sub-block's end
      float e0 = v.env, e1;
      if (v.att < 1.0f) {
        v.att += ainc * static_cast<float>(m);
        float a = v.att < 1.0f ? v.att : 1.0f;
        e1 = v.a0 + (1.0f - v.a0) * a * (2.0f - a);   // (an RC's: fast, then settling)
      } else if (sustain) {
        e1 = 1.0f;
      } else {
        v.dec *= m == kSub ? dkb : fm_exp2f(fm_log2f(dk) * static_cast<float>(m));
        e1 = v.dec;
      }
      v.env = e1;
      // PWM: its LFO (each voice its own pace, 0.55 .. 0.9 Hz)
      v.lfo += static_cast<float>(m) * (0.55f + 0.1f * static_cast<float>(i)) / kSr;
      v.lfo -= v.lfo >= 1.0f ? 1.0f : 0.0f;
      c.pw = clampf(w.pw + w.pwm * sin_cyc(v.lfo), 2.0f * c.d, 1.0f - 2.0f * c.d);
      // the filter: its cutoff (the knob, the keyboard, the envelope), its gain, the loop
      float fc = fc0 * fm_exp2f(kt + eamt * e1);
      float x = 3.14159265f * clampf(fc / kSr, 0.0002f, 0.45f), x2 = x * x;
      float g = x * (15.0f - x2) / (15.0f - 6.0f * x2);   // (tan, Pade; held under fs / 2)
      g = g < 12.0f ? g : 12.0f;
      c.G = g / (1.0f + g);
      c.b = 1.0f - c.G;
      // the loop: y4 = (G^4 x + S) / (1 + k G^4), u = comp x - k y4 (comp: the bass the resonance takes, half given
      // back; the input driven a little)
      const float G4 = c.G * c.G * c.G * c.G, inv = 1.0f / (1.0f + kres * G4), drive = 1.1f;
      c.cx = drive * (1.0f + 0.5f * kres - kres * G4 * inv);
      c.ks = kres * inv;               // (x b: S = b (s4 + G (s3 + G (s2 + G s1))))
      voice_block(v, c, e0 * norm, (e1 - e0) * norm / static_cast<float>(m), y + s0, m);
    }
    if (v.att >= 1.0f && !sustain && v.env < 1e-5f) {
      v.alive = 0;
      v.env = 0.0f;
    }
  }
  // the sum: the coupling capacitor (the 25 % sub's offset), a soft knee above 0.8 (at most 1.2)
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

const char* poly_knob_name(int k) {
  static const char* const kN[8] = {"Wave", "Cutoff", "Reso", "Decay", "Sub", "Env", "Chord", "Voice"};
  return k >= 0 && k < 8 ? kN[k] : "";
}

void poly_defaults(float* k) {          // a saw over its sub, a squelch, a voice a trig, a little vintage
  static const float kD[8] = {0.0f, 0.5f, 0.35f, 0.45f, 0.3f, 0.75f, 0.5f, 0.35f};
  for (int i = 0; i < 8; ++i) k[i] = kD[i];
}

int poly_knob_steps(int k) { return k == 6 ? kChords : 128; }
int poly_chord(float u) { return chord_of(u); }

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
static void put_time(char* b, int n, int* at, float s) {
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

MI_COLD void poly_knob_text(int k, float u, char* b, int n) {
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  switch (k) {
    case 0: {                           // WAVE: the saw, the square, the pulse's width, PWM
      if (u < 0.01f) return put_s(b, n, &at, "Saw");
      if (u < 0.49f) return put_s(b, n, &at, u < 0.25f ? "Saw+Sq" : "Sq+Saw");
      if (u < 0.51f) return put_s(b, n, &at, "Square");
      Wave w = wave_of(u);
      if (u < 0.75f) {
        put_s(b, n, &at, "PW ");
        put_i(b, n, &at, static_cast<int>(w.pw * 100.0f + 0.5f));
        return put_s(b, n, &at, "%");
      }
      put_s(b, n, &at, "PWM ");
      put_i(b, n, &at, static_cast<int>((u - 0.75f) * 400.0f + 0.5f));
      return put_s(b, n, &at, "%");
    }
    case 1: {                           // CUTOFF: Hz
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
    case 4: {                           // SUB: its level, then its octave
      if (u < 0.004f) return put_s(b, n, &at, "Off");
      if (u <= 0.5f) {
        put_i(b, n, &at, static_cast<int>(u * 200.0f + 0.5f));
        return put_s(b, n, &at, "%");
      }
      return put_s(b, n, &at, u < 0.625f ? "-1 Oct" : u < 0.875f ? "-2 Oct" : "-2 25%");
    }
    case 6:
      return put_s(b, n, &at, kChordName[chord_of(u) + kChords / 2]);
    case 5: case 7: {                   // bipolar: ENV (- SWELL, + its depth), VOICE (- VINTAGE, + UNISON)
      float wv = 2.0f * u - 1.0f;
      int x = static_cast<int>(wv * 100.0f + (wv < 0.0f ? -0.5f : 0.5f));
      if (x == 0) return put_s(b, n, &at, k == 7 ? "Clean" : "0%");
      put_i(b, n, &at, x < 0 ? -x : x);
      static const char* const kL[2][2] = {{"% SWELL", "% ENV"}, {"% VINTAGE", "% UNISON"}};
      return put_s(b, n, &at, kL[k == 7][x > 0]);
    }
    default:
      break;
  }
  put_i(b, n, &at, static_cast<int>(u * 100.0f + 0.5f));
  put_s(b, n, &at, "%");
}

}  // extern "C"
