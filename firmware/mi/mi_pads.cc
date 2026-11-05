// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's PADS (0.11): chords and their progressions, played by a string pad. One track at most holds it: its chords
// are the project's, the engine's scale may follow them (SCALE: Prog).
//
// The chords. A progression is a list of chords, each a degree of the scale (a triad stacked on it in the scale: its
// third, its fifth; COLOR adds the seventh, the ninth, the eleventh), a quality when the chord wants one the scale
// lacks (the major dominant of a minor key: its raised third), and notes it adds (Arvo Part's tintinnabuli: the tonic
// triad held under a melody moving by steps). Built in the engine's scale (any of them: the chords follow it), on its
// root; the progression's own mode when the scale is Off or Prog. MANUAL: the note played picks the chord (its degree
// in the scale: a chord a key). Each trig of the track the next chord.
//
// The chord's scale (what the other tracks follow, SCALE: Prog): the scale, the chord's own notes (a raised third
// too), less the notes a half step over a chord note (the avoid notes: they rub against it).
//
// The pad: each note one saw (VINTAGE, ENSEMBLE to the left: each a little off, wandering on its own) or three
// (ENSEMBLE to the right: a string machine's ensemble, wider), a sine under them (TONE to the left: rounder, the saws
// filtered down), a low pass on the sum that blooms with the envelope; the chord voiced close to the last one (each
// voice moves the least it can), VOICING spreading it (open, wide, a bass under it). TEXTURE (0.16.16, BELLS' place;
// after Boards of Canada's ways, as they told them): Bell (at each trig a bell, two operators, 1 : 3.5, its index
// falling, on the top note), Tape (wow and flutter, the tape's saturation, its top worn), Drift (the notes clashing a
// little out of tune, chorusing microtonally), Dust (a sampler's lower bits and rate, hiss and crackle), Down (an
// octave under each note, darker: resampled down); each from a touch to all of it.
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f;
const int kMaxV = 6;                    // the chord's notes (an eleventh: 6) with the bass

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

// ---- the progressions ----
enum { Q_DIA, Q_MAJ, Q_MIN, Q_SUS2, Q_SUS4 };   // (0.16.12: suspended, the third left for the second or the fourth)
enum { M_MAJOR = 1, M_MINOR = 2, M_DORIAN = 3, M_PHRYGIAN = 4, M_LYDIAN = 5, M_PHRYGDOM = 16 };   // (the engine's
                                        // scales)
struct Chord {
  int8_t deg, q, add0, add1;            // its degree (0: the tonic), its quality, two added degrees over it (-1 none)
  int8_t chr, extra;                    // (0.16.12) a root off the scale, semitones over the tonic (0: the degree's;
                                        // its quality major unless minor); a note off the scale, semitones over
                                        // the chord's root (-1 none: a line moving inside the chord)
};
struct Prog {
  const char* name;
  uint8_t mode, n;
  Chord c[8];
};
#define C1(d) {d, 0, -1, -1, 0, -1}
#define CQ(d, q) {d, q, -1, -1, 0, -1}
#define CA(d, a) {d, 0, a, -1, 0, -1}
#define CX(d, x) {d, 0, -1, -1, 0, x}
#define CR(semi, q) {0, q, -1, -1, semi, -1}
// 0.11's sixteen where a project of then finds them (PROG's travel in 27 now: its share's middle, (i + 0.5) / 17,
// lands on the same one), 0.16.12's ten between them: darker, suspended, off the scale (after the way Trent Reznor
// and Atticus Ross's records and scores move: a line falling inside a minor chord, the flat second, the tritone, the
// major fourth in a minor key, chromatic mediants, a pedal under moving chords)
const int kProgs = 26;
const Prog kProg[kProgs] = {
    // (0.16.12) the line cliché: a minor chord, a voice inside it falling by half steps: i, i(maj7), i7, i6
    {"Descent", M_MINOR, 4, {C1(0), CX(0, 11), CX(0, 10), CX(0, 9)}},
    // after Arvo Part: the tonic triad held, a voice over it going down by steps and back (tintinnabuli)
    {"Tintin", M_MINOR, 8, {CA(0, 7), CA(0, 6), CA(0, 5), CA(0, 3), CA(0, 1), CA(0, 3), CA(0, 5), CA(0, 6)}},
    // the same in a major key, the tonic and its fourth degree (a mirror)
    {"Mirror", M_MAJOR, 8, {CA(0, 1), C1(0), CA(3, 1), CA(0, 5), CA(4, 3), C1(0), CA(3, 6), C1(4)}},
    // (0.16.12) the flat second (the Neapolitan, a half step over the tonic): i bII bVII i
    {"Flat II", M_MINOR, 4, {C1(0), CR(1, Q_MAJ), C1(6), C1(0)}},
    // Pachelbel's canon: I V vi iii IV I IV V
    {"Pachelbel", M_MAJOR, 8, {C1(0), C1(4), C1(5), C1(2), C1(3), C1(0), C1(3), C1(4)}},
    // (0.16.12) the tritone away: i bV i bVI
    {"Tritone", M_MINOR, 4, {C1(0), CR(6, Q_MAJ), C1(0), C1(5)}},
    // la Folia: i V i VII III VII i V (its dominant major)
    {"Folia", M_MINOR, 8, {C1(0), CQ(4, Q_MAJ), C1(0), C1(6), C1(2), C1(6), C1(0), CQ(4, Q_MAJ)}},
    // the lament (a descending tetrachord, Purcell's ground, the Andalusian cadence): i VII VI V
    {"Lament", M_MINOR, 4, {C1(0), C1(6), C1(5), CQ(4, Q_MAJ)}},
    // (0.16.12) the major fourth in a minor key (the dorian's, lifting): i III IV i
    {"Rise", M_MINOR, 4, {C1(0), C1(2), CQ(3, Q_MAJ), C1(0)}},
    // the circle of fifths (the baroque sequence): vi ii V I IV vii iii vi
    {"Fifths", M_MAJOR, 8, {C1(5), C1(1), C1(4), C1(0), C1(3), C1(6), C1(2), C1(5)}},
    // Satie's first Gymnopedie: IVmaj7 Imaj7
    {"Gymnopedie", M_MAJOR, 2, {CA(3, 6), CA(0, 6)}},
    // (0.16.12) suspended, nothing resolving: i(sus2) bVI(sus2) bIII bVII(sus4)
    {"Hover", M_MINOR, 4, {CQ(0, Q_SUS2), CQ(5, Q_SUS2), C1(2), CQ(6, Q_SUS4)}},
    // Bach's first prelude (the Well-Tempered Clavier), its opening: I ii7 V7 I vi ii V7 I
    {"Prelude", M_MAJOR, 8, {C1(0), CA(1, 6), CA(4, 6), C1(0), C1(5), C1(1), CA(4, 6), C1(0)}},
    // (0.16.12) the major and the minor on one tonic: I i bVI bVII
    {"Shift", M_MINOR, 4, {CQ(0, Q_MAJ), CQ(0, Q_MIN), C1(5), C1(6)}},
    // the plagal (the amen): I IV I IV vi IV I I
    {"Plagal", M_MAJOR, 8, {C1(0), C1(3), C1(0), C1(3), C1(5), C1(3), C1(0), C1(0)}},
    // the dorian vamp: i7 IV7
    {"Dorian", M_DORIAN, 2, {CA(0, 6), CA(3, 6)}},
    // (0.16.12) chromatic mediants (a film's): i bVI i III (major, off the scale)
    {"Mediant", M_MINOR, 4, {C1(0), C1(5), C1(0), CR(4, Q_MAJ)}},
    // dub: i7 iv7 i7 VII
    {"Dub", M_MINOR, 4, {CA(0, 6), CA(3, 6), CA(0, 6), C1(6)}},
    // a chorale's: I vi IV V I IV V I
    {"Hymn", M_MAJOR, 8, {C1(0), C1(5), C1(3), C1(4), C1(0), C1(3), C1(4), C1(0)}},
    // (0.16.12) chords moving over the tonic held: i, then bVII, v, bVII with the tonic in them
    {"Pedal", M_MINOR, 4, {C1(0), CA(6, 1), CA(4, 3), CA(6, 1)}},
    // the phrygian: i II i VII VI VII II i
    {"Phrygian", M_PHRYGIAN, 8, {C1(0), C1(1), C1(0), C1(6), C1(5), C1(6), C1(1), C1(0)}},
    // (0.16.12) the phrygian dominant's: I bII bvii bII
    {"Dominant", M_PHRYGDOM, 4, {C1(0), C1(1), C1(6), C1(1)}},
    // the epic (aeolian): i VI III VII
    {"Epic", M_MINOR, 4, {C1(0), C1(5), C1(2), C1(6)}},
    // a slow minor (lento): i(add9) VI i(add9) iv
    {"Lento", M_MINOR, 4, {CA(0, 1), C1(5), CA(0, 1), C1(3)}},
    // (0.16.12) slow, wide: i(add9) bVI(maj7) iv(add9) bVI
    {"Haze", M_MINOR, 4, {CA(0, 8), CA(5, 6), CA(3, 8), C1(5)}},
    // the tonic alone (a drone: with SCALE Prog, everything on its chord)
    {"Static", M_MAJOR, 1, {C1(0)}},
};
#undef C1
#undef CQ
#undef CA
#undef CX
#undef CR

// the scale's notes (semitones over its root, 0..11) from its mask, how many
int scale_notes(uint16_t mask, int* s) {
  int n = 0;
  for (int i = 0; i < 12; ++i)
    if ((mask >> i) & 1u) s[n++] = i;
  if (!n) s[n++] = 0;
  return n;
}
// the scale's k-th note from degree d (semitones, octaves past it)
inline int tone(const int* s, int n, int k) {
  int o = k >= 0 ? k / n : -((-k + n - 1) / n);
  return s[k - o * n] + 12 * o;
}

// ---- the pad ----
struct Pad {
  float k[8], note;
  int chord[kMaxV], cn;                 // the chord (semitones over the tonic's octave under the note), its notes
  int tonic;
  float pitch[kMaxV];                   // the voiced notes (MIDI)
  int nv;
  float ph[kMaxV][3], dr[kMaxV], dr_t[kMaxV];   // the saws' phases; each voice's wander
  float sx[kMaxV], sy[kMaxV];           // each voice's sine (a rotation: Minsky's circle)
  float pend;                           // the voices' last sample, a sample late (its saws' wraps may yet touch it)
  int dr_n;
  float env, a0, att, dec;              // the envelope: its level, where the attack started, its progress, the decay's
  int gate, trig, alive, let_go, voiced;
  float lp1, lp2;                       // the low pass (a state variable filter: its two states)
  float bell_c, bell_m, bell_e, bell_i, bell_hz, bell_k, bell_ik;   // the bell: its phases, level, index, pitch
  float wow, flut, flut_t;              // (0.16.16) Tape: its wow's phase, its flutter (now, aiming)
  float dx[kMaxV], dy[kMaxV];           // Down: each note's sine an octave under
  float dust_v, crack;                  // Dust: the sample held, a crackle ringing
  int dust_h, flut_n;
  float dcx, dcy;
  uint32_t rnd;
  float gn;                             // (0.16.5) its level now: 0.55 / sqrt(the chord's notes), glided (a chord of
                                        // another size, held over: no step)
};
Pad pad;

inline float t60_s(float u) { return 0.2f * fm_exp2f(u * 6.644f); }        // DECAY: 0.2 .. 20 s
// TEXTURE: Off at the left, then five, each from a touch to all of it
enum { TX_OFF, TX_BELL, TX_TAPE, TX_DRIFT, TX_DUST, TX_DOWN, TX_N };
const char* const kTexName[TX_N] = {"Off", "Bell", "Tape", "Drift", "Dust", "Down"};
inline int tex_of(float u, float* amt) {
  if (u < 0.04f) {
    *amt = 0.0f;
    return TX_OFF;
  }
  const float z = (u - 0.04f) / 0.192f;
  int k = static_cast<int>(z);
  k = k > 4 ? 4 : k;
  const float a = z - static_cast<float>(k);
  *amt = 0.15f + 0.85f * (a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a);
  return TX_BELL + k;
}
inline float attack_s(float u) { return 0.001f * fm_exp2f(u * 12.0f); }    // ATTACK: 1 ms .. 4 s
inline int prog_of(float u) {
  int p = static_cast<int>(u * static_cast<float>(kProgs + 1));
  return p < 0 ? 0 : p > kProgs ? kProgs : p;
}
// COLOR (0.16.12: nine, 0.11's four where a project of then finds them: its share's middle, (i + 0.5) / 4, on 1 3 5 7)
enum { CO_POWER, CO_TRIAD, CO_SUS2, CO_7TH, CO_SUS4, CO_9TH, CO_ADD9, CO_11TH, CO_ADD6, CO_N };
const char* const kColorName[CO_N] = {"Power", "Triad", "Sus2", "7th", "Sus4", "9th", "Add9", "11th", "Add6"};
inline int color_of(float u) {
  int c = static_cast<int>(u * static_cast<float>(CO_N));
  return c < 0 ? 0 : c > CO_N - 1 ? CO_N - 1 : c;
}
inline int voicing_of(float u) {
  int c = static_cast<int>(u * 4.0f);
  return c < 0 ? 0 : c > 3 ? 3 : c;
}

// the chord voiced: close to the last (each voice moving the least it can), then spread
void voice_chord(Pad& v) {
  const int n = v.cn;
  if (n <= 0) return;
  float base = v.note - static_cast<float>(((static_cast<int>(v.note) - v.tonic) % 12 + 12) % 12);   // the tonic under
  int close[kMaxV];
  for (int i = 0; i < n; ++i) close[i] = static_cast<int>(base) + v.chord[i];
  // its inversions (each a rotation, the lowest notes up an octave), around the note
  int best[kMaxV], bc = 1 << 30;
  for (int r = 0; r < n; ++r) {
    for (int sh = -12; sh <= 0; sh += 12) {
      int c[kMaxV];
      for (int i = 0; i < n; ++i) c[i] = close[(i + r) % n] + (i + r >= n ? 12 : 0) + sh;
      int lo = c[0], cost = 0;
      if (v.voiced && v.nv >= n) {      // (the last chord's voices, from the bottom)
        for (int i = 0; i < n; ++i) {
          int d = c[i] - static_cast<int>(v.pitch[i]);
          cost += d < 0 ? -d : d;
        }
      } else {
        cost = lo - static_cast<int>(v.note);
        cost = cost < 0 ? -cost : cost;
      }
      int drift = lo - static_cast<int>(v.note);   // (not too far from where the note puts it)
      if (drift < -7 || drift > 9) cost += 100;
      if (cost < bc) {
        bc = cost;
        for (int i = 0; i < n; ++i) best[i] = c[i];
      }
    }
  }
  // the spread: close, open (the second from the top an octave down), wide (and the fourth), a bass under it
  int vo = voicing_of(v.k[2]), nv = n;
  float p[kMaxV];
  for (int i = 0; i < n; ++i) p[i] = static_cast<float>(best[i]);
  if (vo >= 1 && n >= 3) p[n - 2] -= 12.0f;
  if (vo >= 2 && n >= 4) p[n - 4] -= 12.0f;
  if (vo >= 3 && nv < kMaxV) {
    float lo = p[0];
    for (int i = 1; i < nv; ++i) lo = p[i] < lo ? p[i] : lo;
    float root = static_cast<float>(close[0]);
    while (root >= lo) root -= 12.0f;
    p[nv++] = root;
  }
  // (sorted, low to high: the next chord's voice leading reads them so)
  for (int i = 1; i < nv; ++i)
    for (int j = i; j > 0 && p[j] < p[j - 1]; --j) {
      float t = p[j];
      p[j] = p[j - 1];
      p[j - 1] = t;
    }
  for (int i = 0; i < nv; ++i) v.pitch[i] = p[i];
  v.nv = nv;
  v.voiced = 1;
}

}  // namespace

extern "C" {

int pads_progs(void) { return kProgs + 1; }
int pads_prog(float u) { return prog_of(u); }
int pads_prog_len(int prog) { return prog >= 1 && prog <= kProgs ? kProg[prog - 1].n : 1; }
int pads_prog_mode(int prog) { return prog >= 1 && prog <= kProgs ? kProg[prog - 1].mode : M_MAJOR; }
const char* pads_prog_name(int prog) { return prog >= 1 && prog <= kProgs ? kProg[prog - 1].name : "Manual"; }

int pads_chord(int prog, int pos, int degree, uint16_t mask, float color_u, int* out, int* raised) {
  int s[12], n = scale_notes(mask, s), color = color_of(color_u);
  Chord c = {static_cast<int8_t>(degree), Q_DIA, -1, -1, 0, -1};
  if (prog >= 1 && prog <= kProgs) {
    const Prog& pr = kProg[prog - 1];
    c = pr.c[(pos % pr.n + pr.n) % pr.n];
  }
  int k = 0, d = c.deg, r, third, fifth, second, fourth, sixth, seventh, ninth, eleventh;
  *raised = -1;
  if (c.chr) {                          // (0.16.12) a root off the scale: its intervals its own, major unless minor
    r = c.chr;
    third = r + (c.q == Q_MIN ? 3 : 4), fifth = r + 7, second = r + 2, fourth = r + 5, sixth = r + 9;
    seventh = r + (c.q == Q_MIN ? 10 : 11), ninth = r + 14, eleventh = r + 17;
  } else {
    r = tone(s, n, d);
    third = tone(s, n, d + 2), fifth = tone(s, n, d + 4), second = tone(s, n, d + 1), fourth = tone(s, n, d + 3);
    sixth = tone(s, n, d + 5), seventh = tone(s, n, d + 6), ninth = tone(s, n, d + 8), eleventh = tone(s, n, d + 10);
    if (c.q == Q_MAJ || c.q == Q_MIN) { // (a quality the scale lacks: its third, its fifth set)
      int want = r + (c.q == Q_MAJ ? 4 : 3);
      if (third != want) *raised = (want % 12 + 12) % 12;
      third = want;
      fifth = r + 7;
    }
  }
  // its third: suspended by the progression (its second, its fourth) or by COLOR; POWER: none, the octave instead
  if (c.q == Q_SUS2 || color == CO_SUS2) third = second;
  else if (c.q == Q_SUS4 || color == CO_SUS4) third = fourth;
  out[k++] = r;
  if (color == CO_POWER) {
    out[k++] = fifth;
    out[k++] = r + 12;
  } else {
    if (color != CO_11TH) out[k++] = third;   // (the eleventh without its third, which rubs against it: before
    out[k++] = fifth;                         // 0.16.12, five notes at most, the eleventh never got in: a ninth)
  }
  if (color == CO_7TH || color == CO_9TH || color == CO_11TH) out[k++] = seventh;
  if (color == CO_9TH || color == CO_11TH || color == CO_ADD9) out[k++] = ninth;
  if (color == CO_11TH) out[k++] = eleventh;
  if (color == CO_ADD6) out[k++] = sixth;   // (five at most, the bass besides: kMaxV)
  const int8_t add[2] = {c.add0, c.add1};
  for (int i = 0; i < 2; ++i) {
    if (add[i] < 0 || k >= kMaxV - 1) continue;
    int t = c.chr ? r + tone(s, n, add[i]) : tone(s, n, d + add[i]), dup = 0;
    for (int j = 0; j < k; ++j) dup |= (out[j] - t) % 12 == 0;
    if (!dup) out[k++] = t;
  }
  if (c.extra >= 0 && k < kMaxV - 1) {   // (0.16.12) a note off the scale: a line inside the chord (the cliché's)
    int t = r + c.extra, dup = 0;
    for (int j = 0; j < k; ++j) dup |= (out[j] - t) % 12 == 0;
    if (!dup) out[k++] = t;
  }
  return k;
}

uint16_t pads_chord_scale(const int* chord, int n, uint16_t mask, int raised) {
  uint16_t cm = 0;
  for (int i = 0; i < n; ++i) cm |= static_cast<uint16_t>(1u << ((chord[i] % 12 + 12) % 12));
  uint16_t m = mask;
  if (raised >= 0) {                    // (the raised third in, the note it raised out)
    m |= static_cast<uint16_t>(1u << raised);
    m &= static_cast<uint16_t>(~(1u << ((raised + 11) % 12)));
  }
  m |= cm;
  for (int i = 0; i < 12; ++i)          // the avoid notes: a half step over a chord note
    if (((cm >> i) & 1u) && ((m >> ((i + 1) % 12)) & 1u) && !((cm >> ((i + 1) % 12)) & 1u))
      m &= static_cast<uint16_t>(~(1u << ((i + 1) % 12)));
  return m ? m : cm;
}

void pads_init(void) {
  uint8_t* raw = reinterpret_cast<uint8_t*>(&pad);
  for (uint32_t i = 0; i < sizeof(Pad); ++i) raw[i] = 0;
  pad.rnd = 0x9E3779B9u;
  pad.note = 48.0f;
  for (int i = 0; i < kMaxV; ++i) pad.sy[i] = pad.dy[i] = 1.0f;
  pad.att = 1.0f;
  float d[8];
  pads_defaults(d);
  for (int i = 0; i < 8; ++i) pad.k[i] = d[i];
}

void pads_silence(void) {
  pad.alive = 0;
  pad.trig = 0;
  pad.env = 0.0f;
  pad.att = 1.0f;
  pad.bell_e = 0.0f;
  pad.voiced = 0;
}

void pads_set(float note, const float* k, const int* chord, int n, int tonic) {
  Pad& v = pad;
  int changed = n != v.cn || tonic != v.tonic || static_cast<int>(note) != static_cast<int>(v.note) ||
                voicing_of(k[2]) != voicing_of(v.k[2]);
  for (int i = 0; i < n && !changed; ++i) changed = chord[i] != v.chord[i];
  v.note = note;
  v.tonic = tonic;
  for (int i = 0; i < 8; ++i) v.k[i] = clampf(k[i], 0.0f, 1.0f);
  v.cn = n < kMaxV ? n : kMaxV;
  for (int i = 0; i < v.cn; ++i) v.chord[i] = chord[i];
  if (changed && (v.trig || !v.alive)) voice_chord(v);   // (a new chord at a trig: held notes keep theirs)
}

void pads_gate(int g) {
  Pad& v = pad;
  if (g && !v.gate) {
    v.trig = 1;
    voice_chord(v);                     // (this trig's chord, voiced from the last)
  }
  if (!g && v.gate && v.alive && v.k[3] > 0.98f) {   // held, let go
    v.let_go = 1;
    v.dec = v.env;
    v.att = 1.0f;
  }
  v.gate = g;
}

}  // extern "C"

namespace {

// a saw's step, its alias taken off by a polyBLEP a sample late: at the wrap (t the sample's fraction past it) the
// sample before takes -t^2 (written back to it, in the sum: g its gain there), this one (1 - t)^2
inline void saw(float& p, float d, float id, float& acc, float& cor, float* before, float g) {
  p += d;
  if (p >= 1.0f) {
    p -= 1.0f;
    float t = p * id;
    *before -= g * t * t;
    cor += (1.0f - t) * (1.0f - t);
  }
  acc += p;
}

// the voices, a block of them summed (NS saws a voice), a sample late: sum[0] the last block's last sample (still
// open to a wrap's correction), sum[1..n] this block's; each voice through the block in turn, its phases held
template <int NS>
void voices(Pad& v, float* sum, uint32_t n, const float (*inc)[3], const float (*idt)[3], const float* se, float g_saw,
            float g_sin) {
  sum[0] = v.pend;
  for (uint32_t s = 1; s <= n; ++s) sum[s] = 0.0f;
  const float off = -static_cast<float>(NS) * g_saw;   // (each saw 2 p - 1)
  const float g2 = 2.0f * g_saw;
  for (int i = 0; i < v.nv; ++i) {
    float p0 = v.ph[i][0], p1 = v.ph[i][1], p2 = v.ph[i][2];
    const float d0 = inc[i][0], d1 = NS == 3 ? inc[i][1] : 0.0f, d2 = NS == 3 ? inc[i][2] : 0.0f;
    const float i0 = idt[i][0], i1 = NS == 3 ? idt[i][1] : 0.0f, i2 = NS == 3 ? idt[i][2] : 0.0f;
    float x = v.sx[i], y = v.sy[i];
    const float e = se[i];
    for (uint32_t s = 0; s < n; ++s) {
      float acc = 0.0f, cor = 0.0f;
      saw(p0, d0, i0, acc, cor, sum + s, g_saw);
      if (NS == 3) {
        saw(p1, d1, i1, acc, cor, sum + s, g_saw);
        saw(p2, d2, i2, acc, cor, sum + s, g_saw);
      }
      x += e * y;                       // (the sine: x' = x + e y, y' = y - e x')
      y -= e * x;
      sum[s + 1] += g2 * acc + g_saw * cor + off + g_sin * y;
    }
    v.ph[i][0] = p0;
    if (NS == 3) {
      v.ph[i][1] = p1;
      v.ph[i][2] = p2;
    }
    // its level held (x^2 + e x y + y^2 the circle's invariant: 1 - e^2 / 4 for a peak of 1)
    float r = (1.0f - 0.25f * e * e) / (x * x + e * x * y + y * y + 1e-9f);
    r = 0.5f * (1.0f + r);              // (a step of Newton's to its root: r is ever near 1)
    v.sx[i] = x * r;
    v.sy[i] = y * r;
  }
  v.pend = sum[n];
}

}  // namespace

extern "C" {

void pads_render(float* out, uint32_t n) {
  Pad& v = pad;
  const float A = attack_s(v.k[4]);
  const int held = v.k[3] > 0.98f;
  const float tone = 2.0f * v.k[5] - 1.0f, ens = 2.0f * v.k[6] - 1.0f;
  float ta = 0.0f;
  const int tx = tex_of(v.k[7], &ta);
  if (v.trig) {
    v.trig = 0;
    if (!v.alive || v.env < 1e-3f) {    // from silence: the phases scattered (a pad has no first cycle), the filter
      for (int i = 0; i < kMaxV; ++i) {   // shut
        for (int j = 0; j < 3; ++j) v.ph[i][j] = 0.5f + 0.5f * bip(v.rnd);
        v.sx[i] = 0.0f;
        v.sy[i] = 1.0f;
        v.dx[i] = 0.0f;
        v.dy[i] = 1.0f;
      }
      v.lp1 = v.lp2 = 0.0f;
      v.dcx = v.dcy = 0.0f;
      v.pend = 0.0f;
      v.env = 0.0f;
    }
    v.a0 = v.env;
    v.att = 0.0f;
    v.dec = 1.0f;
    v.let_go = 0;
    v.alive = 1;
    if (tx == TX_BELL && v.nv > 0) {    // the bell: on the top note, an octave up
      v.bell_hz = note_hz(v.pitch[v.nv - 1] + 12.0f);
      v.bell_e = ta;
      v.bell_i = 1.0f;
    }
  }
  if (!v.alive) {
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    return;
  }
  // the wander (VINTAGE: each voice its own, slow)
  v.dr_n -= static_cast<int>(n);
  if (v.dr_n <= 0) {
    v.dr_n = 11025;
    for (int i = 0; i < kMaxV; ++i) v.dr_t[i] = bip(v.rnd);
  }
  for (int i = 0; i < kMaxV; ++i) v.dr[i] += (v.dr_t[i] - v.dr[i]) * clampf(static_cast<float>(n) * 2e-5f, 0.0f, 1.0f);
  // Tape: the wow (0.45 Hz) and the flutter (a new aim every 20 ms, smoothed), the same for every note
  float tape = 0.0f;
  if (tx == TX_TAPE) {
    v.wow += 0.45f * static_cast<float>(n) / kSr;
    v.wow -= v.wow >= 1.0f ? 1.0f : 0.0f;
    v.flut_n -= static_cast<int>(n);
    if (v.flut_n <= 0) v.flut_n = 882, v.flut_t = bip(v.rnd);
    v.flut += (v.flut_t - v.flut) * clampf(static_cast<float>(n) / 300.0f, 0.0f, 1.0f);
    tape = ta * (0.28f * sin_cyc(v.wow) + 0.08f * v.flut);   // (semitones)
  }
  static const float kDrift[kMaxV] = {1.0f, -0.6f, 0.4f, -1.0f, 0.8f, -0.3f};
  // the voices' steps: the saws (ENSEMBLE to the right: three, apart; to the left one, VINTAGE: a few cents off and
  // wandering), the sine
  const int nsaw = ens > 0.02f ? 3 : 1;
  const float vint = ens < 0.0f ? -ens : 0.0f, wide = ens > 0.0f ? ens : 0.0f;
  float inc[kMaxV][3], idt[kMaxV][3], se[kMaxV];
  for (int i = 0; i < v.nv; ++i) {
    float off = (0.04f + vint * 0.22f) * (static_cast<float>(i % 3) - 1.0f) + v.dr[i] * (0.03f + vint * 0.12f) + tape;
    if (tx == TX_DRIFT) off += ta * (0.32f * kDrift[i] + 0.15f * v.dr[i]);   // (Drift: out of tune, each its way)
    float f = note_hz(v.pitch[i] + off);
    se[i] = 2.0f * sin_cyc(clampf(0.5f * f / kSr, 0.0f, 0.2f));   // (the circle's step: 2 sin(pi f / sr))
    for (int j = 0; j < nsaw; ++j) {
      float det = nsaw == 3 ? (static_cast<float>(j) - 1.0f) * (0.03f + 0.17f * wide) : 0.0f;   // (semitones)
      float hz = f * fm_exp2f(det * (1.0f / 12.0f));
      inc[i][j] = clampf(hz / kSr, 1e-5f, 0.45f);
      idt[i][j] = 1.0f / inc[i][j];
    }
  }
  const float dark = tone < 0.0f ? -tone : 0.0f;
  const float g_saw = (1.0f - 0.85f * dark) / static_cast<float>(nsaw), g_sin = 0.15f + 0.85f * dark;
  const float norm_to = 0.55f / fm_sqrtf(static_cast<float>(v.nv > 0 ? v.nv : 1));
  if (v.gn <= 0.0f) v.gn = norm_to;
  const float gk = 1.0f - fm_expf(-1.0f / (0.004f * kSr));   // (~4 ms a step)
  float norm = v.gn;
  // the envelope
  const float dk = held && v.gate && !v.let_go ? 1.0f : fm_expf(-6.9078f / ((v.let_go ? 0.06f + 0.6f * A : t60_s(v.k[3])) * kSr));
  const float ainc = 1.0f / (A * kSr);
  // the low pass: TONE, blooming with the envelope (an octave and a half over it)
  const float worn = tx == TX_TAPE ? 0.8f * ta : tx == TX_DOWN ? 0.7f * ta : 0.0f;   // (Tape's top worn, Down's)
  const float fc0 = 250.0f * fm_exp2f((tone + 1.0f) * 2.9f - worn);   // 250 Hz .. 14 kHz
  // Down: an octave under each note (a sine), its step; Dust: its rate (44.1 kHz held down to 8), its bits (12 to 5)
  float de[kMaxV];
  if (tx == TX_DOWN)
    for (int i = 0; i < v.nv; ++i) de[i] = 0.5f * se[i];
  const float g_down = tx == TX_DOWN ? 0.9f * ta * (g_saw + g_sin) : 0.0f;
  const int hold = tx == TX_DUST ? 1 + static_cast<int>(ta * 4.5f) : 1;
  const float qstep = tx == TX_DUST ? fm_exp2f(-(12.0f - 7.0f * ta)) : 0.0f;
  const float drive = tx == TX_TAPE ? 1.0f + 2.0f * ta : 1.0f;
  const float qinv = tx == TX_DUST ? 1.0f / qstep : 0.0f, hiss = ta * 0.004f * (1.0f / 2147483648.0f);
  const uint32_t crk = static_cast<uint32_t>(ta * 1.2f + 0.5f);
  int d_h = v.dust_h;                   // (Dust's state, kept in locals over the block)
  float d_v = v.dust_v, d_cr = v.crack;
  uint32_t d_rnd = v.rnd;
  // the bell
  const float bk = fm_expf(-1.0f / (2.2f * kSr)), bik = fm_expf(-1.0f / (0.35f * kSr));
  const float binc = v.bell_hz / kSr;
  float fa1 = 0.0f, fa2 = 0.0f, fa3 = 0.0f;
  float sum[33];
  for (uint32_t s = 0; s < n; ++s) {
    if ((s & 31u) == 0u) {              // the voices, 32 samples at a time
      uint32_t m = n - s < 32u ? n - s : 32u;
      if (nsaw == 3) voices<3>(v, sum, m, inc, idt, se, g_saw, g_sin);
      else voices<1>(v, sum, m, inc, idt, se, g_saw, g_sin);
      if (g_down > 0.0f)                // (Down: each note's sine an octave under, added)
        for (int i = 0; i < v.nv; ++i) {
          float x = v.dx[i], y = v.dy[i];
          const float e = de[i];
          for (uint32_t q = 0; q < m; ++q) {
            x += e * y;
            y -= e * x;
            sum[q] += g_down * y;
          }
          const float r = 0.5f * (1.0f + (1.0f - 0.25f * e * e) / (x * x + e * x * y + y * y + 1e-9f));
          v.dx[i] = x * r;
          v.dy[i] = y * r;
        }
    }
    float e;
    if (v.att < 1.0f) {
      v.att += ainc;
      float a = v.att < 1.0f ? v.att : 1.0f;
      e = v.a0 + (1.0f - v.a0) * a * a * (3.0f - 2.0f * a);
    } else {
      v.dec *= dk;
      e = v.dec;
    }
    v.env = e;
    // the low pass (a state variable, trapezoidal: Zavalishin's), its cutoff opening with the envelope (every 16 samples)
    if ((s & 15u) == 0u) {
      float x = 3.14159265f * clampf(fc0 * (0.35f + 0.65f * e) / kSr, 0.0f, 0.3f), x2 = x * x;
      float g = x * (15.0f - x2) / (15.0f - 6.0f * x2);   // (tan, Pade)
      fa1 = 1.0f / (1.0f + g * (g + 1.4f));
      fa2 = g * fa1;
      fa3 = g * fa2;
    }
    float v3 = sum[s & 31u] - v.lp2, v1 = fa1 * v.lp1 + fa2 * v3, v2 = v.lp2 + fa2 * v.lp1 + fa3 * v3;
    v.lp1 = 2.0f * v1 - v.lp1;
    v.lp2 = 2.0f * v2 - v.lp2;
    norm += (norm_to - norm) * gk;
    float y = v2 * norm * e;
    if (tx == TX_TAPE) {                // (Tape: its saturation, the level about kept)
      const float z = y * drive;
      y = z / (1.0f + 0.6f * fm_fabsf(z)) * (1.0f + 0.5f * ta) / drive;
    } else if (tx == TX_DUST) {         // (Dust: held, on fewer bits; hiss, now and then a crackle; its state in locals)
      if (--d_h <= 0) d_h = hold, d_v = fm_floorf(y * qinv + 0.5f) * qstep;
      const uint32_t r = xs(d_rnd);     // (one draw: its bits the hiss, its top the crackle's chance)
      y = d_v + hiss * static_cast<float>(static_cast<int32_t>(r)) + d_cr;
      if ((r >> 20) < crk) d_cr = ta * 0.12f * bip(d_rnd);   // (~10 a s)
      d_cr *= 0.55f;
    }
    if (v.bell_e > 1e-4f) {             // the bell: 1 : 3.5, its index falling fast, its level slowly
      v.bell_m += binc * 3.5f;
      v.bell_m -= v.bell_m >= 1.0f ? 1.0f : 0.0f;
      v.bell_c += binc;
      v.bell_c -= v.bell_c >= 1.0f ? 1.0f : 0.0f;
      float ph = v.bell_c + 0.35f * v.bell_i * sin_cyc(v.bell_m);
      ph -= static_cast<float>(static_cast<int>(ph));
      ph += ph < 0.0f ? 1.0f : 0.0f;
      y += 0.3f * v.bell_e * sin_cyc(ph);
      v.bell_e *= bk;
      v.bell_i *= bik;
    }
    float yd = y - v.dcx + 0.9995f * v.dcy;
    v.dcx = y;
    v.dcy = yd;
    float ay = fm_fabsf(yd);            // a soft knee above 0.8 (at most 1.2)
    if (ay > 0.8f) {
      float z = (ay - 0.8f) * 2.5f;
      ay = 0.8f + 0.4f * z / (1.0f + z);
      yd = yd < 0.0f ? -ay : ay;
    }
    out[s] = yd;
  }
  v.gn = norm;
  v.dust_h = d_h, v.dust_v = d_v, v.crack = d_cr, v.rnd = d_rnd;
  if (v.att >= 1.0f && !(held && v.gate && !v.let_go) && v.env < 1e-5f && v.bell_e < 1e-4f) {
    v.alive = 0;
    v.env = 0.0f;
    v.gn = 0.0f;                        // (from silence: its level at once)
  }
}

int pads_voices(void) { return pad.alive ? pad.nv : 0; }

const char* pads_knob_name(int k) {
  static const char* const kN[8] = {"Prog", "Color", "Voicing", "Decay", "Attack", "Tone", "Ensemble", "Texture"};
  return k >= 0 && k < 8 ? kN[k] : "";
}

void pads_defaults(float* k) {          // Tintin, triads, open, held, a slow swell, a little dark, an ensemble, no
  static const float kD[8] = {2.5f / 27.0f, 1.5f / 9.0f, 0.375f, 1.0f, 0.62f, 0.4f, 0.75f, 0.0f};   // texture
  for (int i = 0; i < 8; ++i) k[i] = kD[i];
}

int pads_knob_steps(int k) { return k == 0 ? kProgs + 1 : k == 1 ? CO_N : k == 2 ? 4 : 128; }

MI_COLD void pads_knob_text(int k, float u, char* b, int n) {
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  switch (k) {
    case 0: {
      int p = prog_of(u);
      return put_s(b, n, &at, pads_prog_name(p));
    }
    case 1: return put_s(b, n, &at, kColorName[color_of(u)]);
    case 2: {
      static const char* const kV[4] = {"Close", "Open", "Wide", "Bass"};
      return put_s(b, n, &at, kV[voicing_of(u)]);
    }
    case 3:
      if (u > 0.98f) return put_s(b, n, &at, "Hold");
      return put_time(b, n, &at, t60_s(u));
    case 4: return put_time(b, n, &at, attack_s(u));
    case 7: {                           // TEXTURE: its name, how much
      float a = 0.0f;
      const int tx = tex_of(u, &a);
      put_s(b, n, &at, kTexName[tx]);
      if (tx == TX_OFF) return;
      put_s(b, n, &at, " ");
      put_i(b, n, &at, static_cast<int>(a * 100.0f + 0.5f));
      return put_s(b, n, &at, "%");
    }
    case 5: case 6: {
      float w = 2.0f * u - 1.0f;
      int x = static_cast<int>(w * 100.0f + (w < 0.0f ? -0.5f : 0.5f));
      if (x == 0) return put_s(b, n, &at, "0%");
      put_i(b, n, &at, x < 0 ? -x : x);
      static const char* const kL[2][2] = {{"% DARK", "% BRIGHT"}, {"% VINTAGE", "% WIDE"}};
      return put_s(b, n, &at, kL[k - 5][x > 0]);
    }
    default:
      break;
  }
}

}  // extern "C"
