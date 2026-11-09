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
// The pad: each note one wave (VINTAGE, ENSEMBLE to the left: each a little off, wandering on its own) or three
// (ENSEMBLE to the right: a string machine's ensemble, wider), a sine under them (TONE to the left: rounder, the waves
// filtered down), a low pass on the sum that blooms with the envelope; the chord voiced close to the last one (each
// voice moves the least it can), VOICING spreading it (open, wide, a bass under it).
//
// (0.16.57) Its knobs on four pages: FREQ, PROG, COLOR, VOICING; DECAY, ATTACK, TONE, GLIDE (each voice sliding to its
// note in the next chord, 20 ms .. 2 s; Off at the left); RELEASE (held, let go: 30 ms .. 7.7 s), TRANSP, OCTAVE,
// F.KEY; then a fourth, in the project's page 39 (OTT's until 0.16.42; PADS plays on one track only): WAVE (sine,
// triangle, saw, square, from one to the next: Saw as before 0.16.57), ENSEMBLE, LAYER, TEXTURE. TEXTURE (0.16.16, BELLS' place;
// after Boards of Canada's ways, as they told them): Tape (wow and flutter, the tape's saturation, its top worn), Drift
// (the notes clashing a little out of tune, chorusing microtonally), Dust (a sampler's lower bits and rate, hiss and
// crackle), Down (an octave under each note, darker: resampled down); each from a touch to all of it.
//
// LAYER (0.16.57, FREQ's place on the track: engine.cc; Bell was TEXTURE's first until then): a voice over the chord,
// added after its low pass, from a touch to all of it. Struck at each trig: Bell (two operators, 1 : 3.5, its index
// falling, on the top note, an octave up), Chime (a wind chime: the chord's notes two octaves up struck at random, now
// and then, more often to the right, as long as the chord sounds; a free bar's three modes, 1 : 2.756 : 5.404), Kalimba
// (a tine's modes, 1 : 6.27 : 17.55, on the top note an octave up; from the middle, the next one under it too, a
// moment later), Pluck (the chord's top four strummed, up then down, a string each: Karplus and Strong's line, its
// fraction an all pass, its notes under 110 Hz an octave up). Held with the chord: Vox (the chord's own waves through
// a voice's three formants, an alto's A slowly to O and towards U and back: the Csound manual's appendix D), Air (a
// breath on each of the top four notes, an octave up: a noise in narrow bands), Halo (an octave and a fifth and two
// octaves over the top three notes, blooming slowly), Swell (each chord's sound reversed into the next trig: it learns
// the time between the trigs, rises from silence and stops at the next one).
#include <stdint.h>
#include "plat.h"                       // (PLAT_POOL: Pluck's lines)
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f;
const float kVoiceFade = 1.0f / (0.010f * kSr);   // (0.16.65) a voice's level, coming or going: 10 ms
const int kMaxV = 6;                    // the chord's notes (an eleventh: 6) with the bass

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
inline float sin_cyc(float ph) {        // a sine of a phase in cycles, 0..1 (9th-order fit on the quarter wave)
  float x = ph < 0.5f ? ph : ph - 0.5f, s = ph < 0.5f ? 1.0f : -1.0f;
  x = x < 0.25f ? x : 0.5f - x;
  float z = x * 4.0f, z2 = z * z;
  return s * z * (1.5707963f + z2 * (-0.64596336f + z2 * (0.079688469f + z2 * (-0.0046722113f + z2 * 0.00015081274f))));
}
// (0.16.57) the fast math out of line, once, for what runs a block (not a sample) at a time: a call each rather than
// its polynomial written out again at every place (~3 KB of program flash; a few calls a block)
__attribute__((noinline)) float exp2_b(float x) { return fm_exp2f(x); }
__attribute__((noinline)) float exp_b(float x) { return fm_expf(x); }
__attribute__((noinline)) float sin_b(float ph) { return sin_cyc(ph); }
inline float note_hz(float note) { return 440.0f * exp2_b((note - 69.0f) * (1.0f / 12.0f)); }
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
MI_COLD int scale_notes(uint16_t mask, int* s) {
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
  // (0.16.57) the fourth page (0..1: WAVE, ENSEMBLE, LAYER, TEXTURE); each voice's pitch now, gliding to pitch[];
  // LAYER's own draws (the pad's untouched by it: a layer only adds)
  float x[4], x5[4], cur[kMaxV];
  int nvp;                              // (the voices before: one more starts on its note, no glide)
  float lt[kMaxV];                      // (0.16.68) LAYER's TRANSP, each voice's: semitones over it (lay_steps)
  int lt_tr, lt_new;                    // (the steps they were made for; 1: the chord, its scale moved since)
  uint16_t smask;                       // (the chords' scale, over the tonic)
  uint32_t lr;
  float mx[4][3], my[4][3], ma[4][3], mk[4][3], me[4][3];   // the strikes (Chime, Kalimba): four, three modes each,
  int mwait[4], mnext;                  // Minsky's circles, their levels falling; each a wait (samples) before it rings
  float ch_wait;                        // Chime: its next strike, samples away
  int pl_n[4], pl_pos[4], pl_wait[4], pl_left[4], pl_life[4], pl_up;   // Pluck: four strings (a line's length, where
  float pl_c[4], pl_x1[4], pl_y1[4], pl_prev[4], pl_g[4], pl_hz[4], pl_v[4];   // it is; its next pluck, samples away
                                        // + 1 (0: none); its samples left; its next one's life; the all pass, the
                                        // average's last, the loss a period, the pitch, the level)
  float vs[3][2], vph;                  // Vox: its three formants' states, the vowel's slow way
  float as[4][2], alp;                  // Air: its four bands' states, its breath's low pass (a high pass made of it)
  float hx[3][2], hy[3][2], he;         // Halo: its circles (an octave and a fifth, two octaves), its swell
  int since, iv, sw_on, sw_t, sw_len, sw_fade;   // Swell: samples since the last trig, between the last two; where it is
  float swx[3], swy[3], sw_lp, sw_at;   // (1 rising, 2 cut), its circles, its wash, its level when cut
  int lring;                            // a strike, a string or a swell still sounding at the last block's end
  // (0.16.65) each voice's level (0..1): a note the chord gains comes in, one it loses goes, over 10 ms (before, at
  // once: a click at each trig where the chord changed its size, the random dose's PROG and VOICING the most); the
  // voices sounding (nf: those of the chord, then those going); TONE glided (~8 ms)
  float vg[kMaxV], tone_s;
  int nf;
};
Pad pad;
const int kPlMax = 404;                 // (a string's line at 110 Hz: lower notes an octave up)
int16_t pl_line[4][kPlMax] PLAT_POOL;   // (Pluck's four lines: 3.2 KB of the pool)

inline float t60_s(float u) { return 0.2f * exp2_b(u * 6.644f); }        // DECAY: 0.2 .. 20 s
// (0.16.57) GLIDE: Off under 0.02, then 20 ms .. 2 s (each voice there within it); RELEASE: 30 ms .. 7.7 s
inline float glide_s(float u) { return u < 0.02f ? 0.0f : 0.02f * exp2_b((u - 0.02f) * (6.643856f / 0.98f)); }
inline float release_s(float u) { return 0.03f * exp2_b(u * 8.0f); }
const float kRelDefault = 0.3065f;      // (164 ms: before RELEASE, 0.06 + 0.6 x ATTACK, 174 ms at its default)
const float kWaveSaw = 2.0f / 3.0f;     // (WAVE on Saw: the pad as before 0.16.57, its saws alone, as fast)
// TONE's and ENSEMBLE's text: a share of their side (-100 .. 100 %, 0 in the middle)
void bip_text(char* b, int n, int* at, float u, int which) {
  float w = 2.0f * u - 1.0f;
  int x = static_cast<int>(w * 100.0f + (w < 0.0f ? -0.5f : 0.5f));
  if (x == 0) return put_s(b, n, at, "0%");
  put_i(b, n, at, x < 0 ? -x : x);
  static const char* const kL[2][2] = {{"% DARK", "% BRIGHT"}, {"% VINTAGE", "% WIDE"}};
  put_s(b, n, at, kL[which][x > 0]);
}
// a knob's choice among nz, each over its share from a touch (15 %) to all of it; Off at the left (under 0.04)
inline int zone_of(float u, int nz, float* amt) {
  if (u < 0.04f) {
    *amt = 0.0f;
    return 0;
  }
  const float w = 0.96f / static_cast<float>(nz), z = (u - 0.04f) / w;
  int k = static_cast<int>(z);
  k = k > nz - 1 ? nz - 1 : k;
  const float a = z - static_cast<float>(k);
  *amt = 0.15f + 0.85f * (a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a);
  return 1 + k;
}
// TEXTURE: Off at the left, then four (0.16.57: Bell, the first of five until then, LAYER's now)
enum { TX_OFF, TX_TAPE, TX_DRIFT, TX_DUST, TX_DOWN, TX_N };
const char* const kTexName[TX_N] = {"Off", "Tape", "Drift", "Dust", "Down"};
inline int tex_of(float u, float* amt) { return zone_of(u, TX_N - 1, amt); }
// LAYER (0.16.57): Off at the left, then eight
enum { LY_OFF, LY_BELL, LY_CHIME, LY_KALIMBA, LY_PLUCK, LY_VOX, LY_AIR, LY_HALO, LY_SWELL, LY_N };
const char* const kLayName[LY_N] = {"Off", "Bell", "Chime", "Kalimba", "Pluck", "Vox", "Air", "Halo", "Swell"};
inline int lay_of(float u, float* amt) { return zone_of(u, LY_N - 1, amt); }
// (0.16.59) the fourth page's TEXTURE and the fifth's LAYER: choices (0 Off); a choice from its knob's value
inline int choice_of(float u, int hi) {
  const int c = static_cast<int>(u + 0.5f);
  return c < 0 ? 0 : c > hi ? hi : c;
}
// (0.16.59) LAYER's level: each layer's amplitude at an amount la (0.16.57's, where that amount set it), taken at its
// zone's middle (0.575), LEVEL's gain on it (2 u^2: silent at 0, 0.16.57's at 0.707, +6 dB at 1)
const float kLaRef = 0.575f;
const float kLevelUnity = 0.70710678f;
inline float lay_gain(float u) { return 2.0f * u * u; }
float lay_level(int ly, float la) {
  switch (ly) {
    case LY_BELL: return la;
    case LY_CHIME: return 0.07f + 0.1f * la;
    case LY_KALIMBA: return 0.13f + 0.2f * la;
    case LY_PLUCK: return 0.32f + 0.4f * la;
    case LY_VOX: return (0.5f + 1.5f * la) * 2.0f;
    case LY_AIR: return (0.4f + 1.6f * la) * 3.0f;
    case LY_HALO: return 0.07f + 0.13f * la;
    case LY_SWELL: return 0.25f + 0.3f * la;
    default: return 0.0f;
  }
}
// CHARACTER: what each does with it (0.16.57's amount, where it did more than its level; the four it did not, 0.5
// what they did then)
const char* const kCharName[LY_N] = {"", "Bright", "Wind", "Tines", "Ring", "Vowels", "Focus", "Bloom", "Rise"};
inline int char_was_level(int ly) { return ly == LY_BELL || ly == LY_VOX || ly == LY_AIR || ly == LY_HALO; }
inline float attack_s(float u) { return 0.001f * exp2_b(u * 12.0f); }    // ATTACK: 1 ms .. 4 s
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
MI_COLD void voice_chord(Pad& v) {
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
  v.lt_new = 1;
}

}  // namespace

extern "C" {

int pads_progs(void) { return kProgs + 1; }
int pads_prog(float u) { return prog_of(u); }
int pads_prog_len(int prog) { return prog >= 1 && prog <= kProgs ? kProg[prog - 1].n : 1; }
int pads_prog_mode(int prog) { return prog >= 1 && prog <= kProgs ? kProg[prog - 1].mode : M_MAJOR; }
const char* pads_prog_name(int prog) { return prog >= 1 && prog <= kProgs ? kProg[prog - 1].name : "Manual"; }

MI_COLD int pads_chord(int prog, int pos, int degree, uint16_t mask, float color_u, int* out, int* raised) {
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

MI_COLD uint16_t pads_chord_scale(const int* chord, int n, uint16_t mask, int raised) {
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

MI_COLD void pads_init(void) {
  uint8_t* raw = reinterpret_cast<uint8_t*>(&pad);
  for (uint32_t i = 0; i < sizeof(Pad); ++i) raw[i] = 0;
  pad.rnd = 0x9E3779B9u;
  pad.lr = 0x2545F491u;                 // (0.16.57) LAYER's own draws
  pad.note = 48.0f;
  for (int i = 0; i < kMaxV; ++i) pad.sy[i] = pad.dy[i] = 1.0f;
  pad.att = 1.0f;
  float d[8];
  pads_defaults(d);
  for (int i = 0; i < 8; ++i) pad.k[i] = d[i];
  pads_page4_defaults(pad.x);           // (0.16.57)
  pads_page5_defaults(pad.x5);          // (0.16.59)
}

MI_COLD void pads_silence(void) {
  pad.alive = 0;
  pad.trig = 0;
  pad.env = 0.0f;
  pad.att = 1.0f;
  pad.bell_e = 0.0f;
  pad.voiced = 0;
  for (int i = 0; i < 4; ++i) {         // (0.16.57) LAYER's: the strikes, the strings, the swell
    pad.ma[i][0] = pad.ma[i][1] = pad.ma[i][2] = 0.0f;
    pad.pl_left[i] = pad.pl_wait[i] = 0;
  }
  pad.sw_on = 0;
  pad.lring = 0;
  pad.he = 0.0f;
  pad.nf = 0;
  for (int i = 0; i < kMaxV; ++i) pad.vg[i] = 0.0f;
  // (0.16.82) its gate down, as the engine's (vgate: 0, a key still held a new trig). Before, a gate high at STOP twice
  // stayed high here: the next rise, not one for the pad, its first chord silent
  pad.gate = 0;
  pad.let_go = 0;
  // (0.16.82) every state that rings made new as well (the voices' own at the next trig, from silence): whatever went
  // wrong in one (a NaN, a value run away), silent here, sounding again at the next trig
  pad.bell_c = pad.bell_m = 0.0f;
  pad.lp1 = pad.lp2 = pad.dcx = pad.dcy = pad.pend = 0.0f;
  pad.dust_v = pad.crack = 0.0f;
  for (int f = 0; f < 3; ++f) pad.vs[f][0] = pad.vs[f][1] = 0.0f;
  for (int j = 0; j < 4; ++j) pad.as[j][0] = pad.as[j][1] = 0.0f;
  pad.alp = pad.sw_lp = 0.0f;
  for (int j = 0; j < 3; ++j) {
    pad.swx[j] = 0.0f, pad.swy[j] = 1.0f;
    pad.hx[j][0] = pad.hx[j][1] = 0.0f, pad.hy[j][0] = pad.hy[j][1] = 1.0f;
  }
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 3; ++j) pad.mx[i][j] = 0.0f, pad.my[i][j] = 1.0f;
}

// (0.16.57) the fourth page (0.16.59: WAVE, ENSEMBLE 0..1, TEXTURE a choice 0..4, its AMOUNT 0..1): the engine's,
// from the project's page 39; (0.16.59) the fifth (LAYER a choice 0..8, LEVEL, CHARACTER 0..1, TRANSP -14..14
// steps of the scale since 0.16.68, -24..24 semitones before), from its page 71
void pads_set_page4(const float* x) {
  for (int i = 0; i < 4; ++i) pad.x[i] = x[i];
}
void pads_set_page5(const float* x) {
  for (int i = 0; i < 4; ++i) pad.x5[i] = x[i];
}
int pads_layer_of(float u) { return choice_of(u, LY_N - 1); }     // LAYER's choice: 0 Off, 1 Bell .. 8 Swell
int pads_texture_of(float u) { return choice_of(u, TX_N - 1); }   // TEXTURE's: 0 Off, 1 Tape .. 4 Down
// (0.16.59) 0.16.57's fourth page (WAVE, ENSEMBLE, LAYER and TEXTURE, a zone of the knob each, its place there the
// amount, 15 .. 100 %): the fourth now (TEXTURE, its AMOUNT that amount) and the fifth (LAYER; its LEVEL what that
// amount gave, CHARACTER the amount where it did more than the level, else 0.5, as they were; TRANSP 0)
MI_COLD void pads_from_v37(const float* x37, float* x4, float* x5) {
  float a = 0.0f;
  x4[0] = x37[0];
  x4[1] = x37[1];
  const int t = tex_of(clampf(x37[3], 0.0f, 1.0f), &a);
  x4[2] = static_cast<float>(t);
  x4[3] = t ? a : 0.5f;
  const int l = lay_of(clampf(x37[2], 0.0f, 1.0f), &a);
  x5[0] = static_cast<float>(l);
  x5[1] = l ? clampf(fm_sqrtf(0.5f * lay_level(l, a) / lay_level(l, kLaRef)), 0.0f, 1.0f) : kLevelUnity;
  x5[2] = l && !char_was_level(l) ? a : 0.5f;
  x5[3] = 0.0f;
}
// a project's or a sound's PADS before 0.16.57 (its eight knobs, 0..1: ENSEMBLE seventh, TEXTURE eighth, its
// Bell, Tape, Drift, Dust, Down a fifth of the knob each): its eight now (GLIDE Off, RELEASE as it let go then: 0.06 s +
// 0.6 x ATTACK) and its fourth page (Saw, its ENSEMBLE, Bell in LAYER, the others in TEXTURE, each as much)
MI_COLD void pads_from_v36(const float* k, float* k_now, float* x) {
  for (int i = 0; i < 8; ++i) k_now[i] = k[i];
  k_now[6] = 0.0f;
  k_now[7] = clampf(fm_log2f((0.06f + 0.6f * attack_s(k[4])) / 0.03f) * 0.125f, 0.0f, 1.0f);
  x[0] = kWaveSaw;
  x[1] = k[6];
  x[2] = x[3] = 0.0f;
  const float tex = k[7];
  if (tex < 0.04f) return;
  const float z = (tex - 0.04f) / 0.192f;
  int i = static_cast<int>(z);
  i = i > 4 ? 4 : i;
  const float a = clampf(z - static_cast<float>(i), 0.0f, 0.999f);
  if (i == 0) x[2] = 0.04f + 0.12f * a;
  else x[3] = 0.04f + 0.24f * (static_cast<float>(i - 1) + a);
}

// (0.16.68) LAYER's TRANSP in steps of the scale the chords are built in, from its tonic: each voice's layer so many of
// its notes away (+2 a third, +4 a fifth, +7 the octave in a scale of seven; the scale's own, so always in it); a note
// off the scale as far from the one under it as it was. Semitones before (a fifth over the seventh degree: off the scale)
MI_COLD int pads_steps(uint16_t mask, int pc, int tr) {   // from pc (0..11 over the tonic): the semitones to the note
  if (!mask) mask = 0xFFFu;                               // tr steps of the scale away (none: semitones)
  int p = pc, q;
  while (p > 0 && !((mask >> p) & 1u)) --p;              // (the scale's note at or under it)
  for (q = p; tr > 0; tr -= (mask >> ((++q + 120) % 12)) & 1u) {}
  for (; tr < 0; tr += (mask >> ((--q + 120) % 12)) & 1u) {}
  return q - p;
}
MI_COLD void lay_steps(Pad& v, int tr) {
  for (int i = 0; i < v.nv; ++i)
    v.lt[i] = static_cast<float>(pads_steps(v.smask, (static_cast<int>(v.pitch[i]) - v.tonic + 120) % 12, tr));
  v.lt_tr = tr;
  v.lt_new = 0;
}

MI_COLD void pads_set(float note, const float* k, const int* chord, int n, int tonic, uint16_t scale) {
  Pad& v = pad;
  if (scale != v.smask || tonic != v.tonic) v.smask = scale, v.lt_new = 1;
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
// sample before takes -t^2, this one (1 - t)^2, both written into the sum where they fall (g their gain there; 0.16.64:
// this one's no more carried to the sum a sample at a time)
inline void saw(float& p, float d, float id, float& acc, float* before, float g) {
  p += d;
  if (p >= 1.0f) {
    p -= 1.0f;
    float t = p * id;
    before[0] -= g * t * t;
    before[1] += g * (1.0f - t) * (1.0f - t);
  }
  acc += p;
}

// the voices, a block of them summed (NS saws a voice), a sample late: sum[0] the last block's last sample (still
// open to a wrap's correction), sum[1..n] this block's; each voice through the block in turn, its phases held. (0.16.64:
// the saws' offsets, a constant, in the sum from the start, once for all the voices; a voice's sample two products)
// (0.16.65) the voices at their level (fm: those coming or going, bit i, left to voice_fade; ns the others)
template <int NS>
void voices(Pad& v, float* sum, uint32_t n, const float (*inc)[3], const float (*idt)[3], const float* se, float g_saw,
            float g_sin, int nf, uint32_t fm, int ns) {
  sum[0] = v.pend;
  const float off = -static_cast<float>(NS) * g_saw * static_cast<float>(ns);   // (each saw 2 p - 1)
  for (uint32_t s = 1; s <= n; ++s) sum[s] = off;
  const float g2 = 2.0f * g_saw;
  for (int i = 0; i < nf; ++i) {
    if ((fm >> i) & 1u) continue;
    float p0 = v.ph[i][0], p1 = v.ph[i][1], p2 = v.ph[i][2];
    const float d0 = inc[i][0], d1 = NS == 3 ? inc[i][1] : 0.0f, d2 = NS == 3 ? inc[i][2] : 0.0f;
    const float i0 = idt[i][0], i1 = NS == 3 ? idt[i][1] : 0.0f, i2 = NS == 3 ? idt[i][2] : 0.0f;
    float x = v.sx[i], y = v.sy[i];
    const float e = se[i];
    for (uint32_t s = 0; s < n; ++s) {
      float acc = 0.0f;
      saw(p0, d0, i0, acc, sum + s, g_saw);
      if (NS == 3) {
        saw(p1, d1, i1, acc, sum + s, g_saw);
        saw(p2, d2, i2, acc, sum + s, g_saw);
      }
      x += e * y;                       // (the sine: x' = x + e y, y' = y - e x')
      y -= e * x;
      sum[s + 1] += g2 * acc + g_sin * y;
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

// (0.16.57) WAVE off Saw: each wave's step, SEG the two shapes it is between (0 sine and triangle, 1 triangle and
// saw, 2 saw and square), c their weights as it needs them. The saw's fall (and the square's, at the same place) a
// polyBLEP's as the saw's, a sample late; the square's rise half a cycle on, its own the other way. The sine a
// parabola's (its odd harmonics: -30 dB), the triangle as it is (its corners: harmonics falling 12 dB an octave; its
// 1 - 2 |x| without the 1, a constant the block adds once). Each about as loud as a saw (the square at 0.58, the sine
// at 0.82)
struct WaveW {
  float sin4, tri2, saw, sq, fall;      // 4 w_sine, 2 w_triangle, w_saw, w_square; the fall's (w_saw + w_square)
};
template <int SEG>                      // (-1: any, its weights as they are)
inline void wave(float& p, float d, float id, float& acc, float* before, float g, const WaveW& c) {
  p += d;
  if (p >= 1.0f) {
    p -= 1.0f;
    if (SEG != 0) {
      const float t = p * id;
      before[0] -= c.fall * g * t * t;
      before[1] += g * c.fall * (1.0f - t) * (1.0f - t);
    }
  } else if ((SEG == 2 || (SEG < 0 && c.sq > 0.0f)) && p >= 0.5f && p - d < 0.5f) {
    const float t = (p - 0.5f) * id;
    before[0] += c.sq * g * t * t;
    before[1] -= g * c.sq * (1.0f - t) * (1.0f - t);
  }
  const float x = 2.0f * p - 1.0f;
  if (SEG == 0) {
    const float ax = fm_fabsf(x);
    acc += c.sin4 * x * (1.0f - ax) - c.tri2 * ax;
  } else if (SEG == 1) {
    acc += c.saw * x - c.tri2 * fm_fabsf(x);
  } else if (SEG == 2) {
    acc += c.saw * x + (x < 0.0f ? -c.sq : c.sq);
  } else {
    const float ax = fm_fabsf(x);
    acc += c.sin4 * x * (1.0f - ax) - c.tri2 * ax + c.saw * x + (x < 0.0f ? -c.sq : c.sq);
  }
}
template <int NS, int SEG>
void voices_w(Pad& v, float* sum, uint32_t n, const float (*inc)[3], const float (*idt)[3], const float* se,
              float g_saw, float g_sin, const WaveW& c, int nf, uint32_t fm, int ns) {
  sum[0] = v.pend;
  const float off = g_saw * static_cast<float>(NS) * 0.5f * c.tri2 * static_cast<float>(ns);   // (the triangles' 1)
  for (uint32_t s = 1; s <= n; ++s) sum[s] = off;
  const WaveW cw = c;                   // (its weights in registers: the sum's stores could have been them)
  for (int i = 0; i < nf; ++i) {
    if ((fm >> i) & 1u) continue;
    float p0 = v.ph[i][0], p1 = v.ph[i][1], p2 = v.ph[i][2];
    const float d0 = inc[i][0], d1 = NS == 3 ? inc[i][1] : 0.0f, d2 = NS == 3 ? inc[i][2] : 0.0f;
    const float i0 = idt[i][0], i1 = NS == 3 ? idt[i][1] : 0.0f, i2 = NS == 3 ? idt[i][2] : 0.0f;
    float x = v.sx[i], y = v.sy[i];
    const float e = se[i];
    for (uint32_t s = 0; s < n; ++s) {
      float acc = 0.0f;
      wave<SEG>(p0, d0, i0, acc, sum + s, g_saw, cw);
      if (NS == 3) {
        wave<SEG>(p1, d1, i1, acc, sum + s, g_saw, cw);
        wave<SEG>(p2, d2, i2, acc, sum + s, g_saw, cw);
      }
      x += e * y;
      y -= e * x;
      sum[s + 1] += g_saw * acc + g_sin * y;
    }
    v.ph[i][0] = p0;
    if (NS == 3) {
      v.ph[i][1] = p1;
      v.ph[i][2] = p2;
    }
    float r = (1.0f - 0.25f * e * e) / (x * x + e * x * y + y * y + 1e-9f);
    r = 0.5f * (1.0f + r);
    v.sx[i] = x * r;
    v.sy[i] = y * r;
  }
  v.pend = sum[n];
}
template <int SEG>
void voices_ws(Pad& v, int nsaw, float* sum, uint32_t n, const float (*inc)[3], const float (*idt)[3], const float* se,
               float g_saw, float g_sin, const WaveW& c, int nf, uint32_t fm, int ns) {
  if (nsaw == 3) voices_w<3, SEG>(v, sum, n, inc, idt, se, g_saw, g_sin, c, nf, fm, ns);
  else voices_w<1, -1>(v, sum, n, inc, idt, se, g_saw, g_sin, c, nf, fm, ns);   // (one oscillator: one body for the
                                                                                 // three, 0.6 KB)
}
// (0.16.65) a voice coming or going: added into the sum (after the others) with its level from g to g + n dg, sample
// by sample; its saws or its WAVE (so: Saw alone), NS of them. Slower, one body for all (a few chunks a trig at most)
__attribute__((noinline)) void voice_fade(Pad& v, int i, int ns, int so, float* sum, uint32_t n, const float (*inc)[3],
                                          const float (*idt)[3], const float* se, float g_saw, float g_sin,
                                          const WaveW& c, float g, float dg) {
  float p[3] = {v.ph[i][0], v.ph[i][1], v.ph[i][2]};
  float x = v.sx[i], y = v.sy[i];
  const float e = se[i], off = so ? -static_cast<float>(ns) : static_cast<float>(ns) * 0.5f * c.tri2;
  for (uint32_t s = 0; s < n; ++s) {
    g += dg;
    const float gs = g_saw * g;
    float acc = 0.0f;
    for (int j = 0; j < ns; ++j) {
      if (so) saw(p[j], inc[i][j], idt[i][j], acc, sum + s, gs);
      else wave<-1>(p[j], inc[i][j], idt[i][j], acc, sum + s, gs, c);
    }
    x += e * y;
    y -= e * x;
    sum[s + 1] += gs * ((so ? 2.0f * acc : acc) + off) + g * g_sin * y;
  }
  for (int j = 0; j < ns; ++j) v.ph[i][j] = p[j];
  float r = (1.0f - 0.25f * e * e) / (x * x + e * x * y + y * y + 1e-9f);
  r = 0.5f * (1.0f + r);
  v.sx[i] = x * r;
  v.sy[i] = y * r;
}

// ---- LAYER's voices (0.16.57) ----
inline float rnd01(uint32_t& s) { return 0.5f + 0.5f * bip(s); }
inline float circle(float hz) { return 2.0f * sin_b(clampf(0.5f * hz / kSr, 0.0f, 0.2f)); }   // (a circle's step)
__attribute__((noinline)) float tpt_g(float hz) { return fm_tanf(3.14159265f * clampf(hz, 20.0f, 0.45f * kSr) / kSr); }

// the strikes: three modes each, ringing from a peak (the mallet), each its own decay; above 0.42 of the rate, none
struct Modal {
  float r[3], a[3], t60[3];
};
const Modal kChime = {{1.0f, 2.756f, 5.404f}, {1.0f, 0.5f, 0.28f}, {3.2f, 1.5f, 0.6f}};       // a free bar's modes
const Modal kKalimba = {{1.0f, 6.267f, 17.547f}, {1.0f, 0.3f, 0.1f}, {1.3f, 0.2f, 0.05f}};   // a tine's (clamped,
                                                                                              // free)
MI_COLD void strike(Pad& v, const Modal& m, float hz, float lvl, int wait) {
  const int i = v.mnext;
  v.mnext = (i + 1) & 3;                // (the oldest gives its place)
  for (int j = 0; j < 3; ++j) {
    const float f = hz * m.r[j];
    v.mx[i][j] = 0.0f;
    v.my[i][j] = 1.0f;
    v.ma[i][j] = f < 0.42f * kSr ? m.a[j] * lvl : 0.0f;
    v.me[i][j] = circle(f);
    v.mk[i][j] = exp_b(-6.9078f / (m.t60[j] * kSr));
  }
  v.mwait[i] = wait;
}
// Chime: one of the top four notes, two octaves up, as hard as the wind blows it
MI_COLD void chime(Pad& v, float lv) {
  const int ns = v.nv < 4 ? v.nv : 4;
  const int i = v.nv - ns + static_cast<int>(rnd01(v.lr) * static_cast<float>(ns) * 0.999f);
  strike(v, kChime, note_hz(v.pitch[i] + 24.0f + v.lt[i]), lv * (0.45f + 0.55f * rnd01(v.lr)), 0);
}
MI_COLD float chime_gap(Pad& v, float la) {     // the next, samples away: 1.5 s on average at a touch .. 0.4 s, as rain falls
  float u = rnd01(v.lr);
  u = u < 0.01f ? 0.01f : u;
  return clampf((1.6f - 1.2f * la) * -fm_logf(u), 0.06f, 5.0f) * kSr;
}
float strikes(Pad& v, float* o, uint32_t m) {   // the strikes ringing, added to o; 1 if any still rings
  int any = 0;
  for (int i = 0; i < 4; ++i) {
    if (v.ma[i][0] + v.ma[i][1] + v.ma[i][2] < 1e-5f) continue;
    any = 1;
    uint32_t q = 0;
    if (v.mwait[i] > 0) {
      const int w = v.mwait[i] < static_cast<int>(m) ? v.mwait[i] : static_cast<int>(m);
      v.mwait[i] -= w;
      q = static_cast<uint32_t>(w);
    }
    float x0 = v.mx[i][0], y0 = v.my[i][0], a0 = v.ma[i][0], x1 = v.mx[i][1], y1 = v.my[i][1], a1 = v.ma[i][1];
    float x2 = v.mx[i][2], y2 = v.my[i][2], a2 = v.ma[i][2];   // (the three modes in one pass: o read once a sample)
    const float e0 = v.me[i][0], e1 = v.me[i][1], e2 = v.me[i][2], k0 = v.mk[i][0], k1 = v.mk[i][1], k2 = v.mk[i][2];
    for (uint32_t s = q; s < m; ++s) {
      x0 += e0 * y0;
      y0 -= e0 * x0;
      x1 += e1 * y1;
      y1 -= e1 * x1;
      x2 += e2 * y2;
      y2 -= e2 * x2;
      o[s] += a0 * y0 + a1 * y1 + a2 * y2;
      a0 *= k0;
      a1 *= k1;
      a2 *= k2;
    }
    v.mx[i][0] = x0, v.my[i][0] = y0, v.ma[i][0] = a0 < 3e-6f ? 0.0f : a0;   // (-110 dB under a peak of 1: done)
    v.mx[i][1] = x1, v.my[i][1] = y1, v.ma[i][1] = a1 < 3e-6f ? 0.0f : a1;
    v.mx[i][2] = x2, v.my[i][2] = y2, v.ma[i][2] = a2 < 3e-6f ? 0.0f : a2;
  }
  return static_cast<float>(any);
}

// Pluck: the top four notes strummed, up from the lowest at one trig, down from the top at the next; each string
// rings on until its turn comes, then its line is filled with soft noise (a pick)
MI_COLD void strum(Pad& v, float la, float lv) {
  const int ns = v.nv < 4 ? v.nv : 4;
  v.pl_up ^= 1;
  const float g = lv / fm_sqrtf(static_cast<float>(ns > 0 ? ns : 1));
  const float t60 = 1.2f + 2.6f * la;
  for (int j = ns; j < 4; ++j) v.pl_wait[j] = 0;   // (a string over the chord's: left to ring out)
  for (int j = 0; j < ns; ++j) {
    const int s = v.pl_up ? j : ns - 1 - j;
    float hz = note_hz(v.pitch[v.nv - ns + s] + v.lt[v.nv - ns + s]);
    while (hz < 110.0f) hz *= 2.0f;
    while (hz > 1800.0f) hz *= 0.5f;
    v.pl_hz[s] = hz;
    v.pl_wait[s] = 1 + static_cast<int>((0.016f + 0.014f * la) * kSr * static_cast<float>(j));   // (16 .. 30 ms apart)
    v.pl_v[s] = g * (0.8f + 0.2f * rnd01(v.lr));
    v.pl_g[s] = exp_b(-6.9078f / (hz * t60));   // (its loss a period: T60 1.2 .. 3.8 s)
    v.pl_life[s] = static_cast<int>(t60 * 1.1f * kSr);
  }
}
MI_COLD void excite(Pad& v, int s) {
  const float L = kSr / v.pl_hz[s] - 0.5f;   // (the loop: the line, the average's half sample, the all pass's fraction)
  int n = static_cast<int>(L - 0.1f);
  n = n < 4 ? 4 : n > kPlMax ? kPlMax : n;
  const float d = L - static_cast<float>(n);
  v.pl_c[s] = (1.0f - d) / (1.0f + d);
  v.pl_n[s] = n;
  v.pl_pos[s] = 0;
  v.pl_x1[s] = v.pl_y1[s] = v.pl_prev[s] = 0.0f;
  int16_t* b = pl_line[s];
  float lp = 0.0f, mean = 0.0f;
  for (int i = 0; i < n; ++i) {
    lp += 0.5f * (bip(v.lr) - lp);       // (a soft pick: its top a little rounded)
    b[i] = static_cast<int16_t>(lp * 16000.0f);
    mean += lp;
  }
  const int16_t dc = static_cast<int16_t>(mean / static_cast<float>(n) * 16000.0f);
  for (int i = 0; i < n; ++i) b[i] = static_cast<int16_t>(b[i] - dc);
  v.pl_left[s] = v.pl_life[s];
}
void string_run(Pad& v, int s, float* o, uint32_t from, uint32_t to) {   // a string sounding, samples from .. to
  int16_t* b = pl_line[s];
  int p = v.pl_pos[s];
  const int n = v.pl_n[s];
  const float c = v.pl_c[s], g = v.pl_g[s] * 16000.0f, lv = v.pl_v[s];
  float x1 = v.pl_x1[s], y1 = v.pl_y1[s], prev = v.pl_prev[s];
  for (uint32_t i = from; i < to; ++i) {
    const float x = static_cast<float>(b[p]) * (1.0f / 16000.0f);   // (the line's end: n samples ago)
    const float avg = 0.5f * (x + prev);  // (the loss: its average, half a sample)
    const float ap = c * (avg - y1) + x1;   // (the fraction: an all pass)
    x1 = avg;
    y1 = ap;
    float w = g * ap;
    w = w > 32767.0f ? 32767.0f : w < -32767.0f ? -32767.0f : w;
    b[p] = static_cast<int16_t>(w + (w < 0.0f ? -0.5f : 0.5f));
    p = p + 1 >= n ? 0 : p + 1;
    prev = x;
    o[i] += lv * x;
  }
  v.pl_pos[s] = p, v.pl_x1[s] = x1, v.pl_y1[s] = y1, v.pl_prev[s] = prev;
  v.pl_left[s] -= static_cast<int>(to - from);
  if (v.pl_left[s] < 0) v.pl_left[s] = 0;
}
float plucks(Pad& v, float* o, uint32_t m) {   // the strings, added to o; 1 if any sounds or waits
  int any = 0;
  for (int s = 0; s < 4; ++s) {
    if (!v.pl_left[s] && !v.pl_wait[s]) continue;
    any = 1;
    uint32_t i = 0;
    while (i < m) {
      uint32_t end = m;
      if (v.pl_wait[s] > 0 && static_cast<uint32_t>(v.pl_wait[s] - 1) < m - i) end = i + static_cast<uint32_t>(v.pl_wait[s] - 1);
      if (v.pl_left[s] > 0 && end > i) string_run(v, s, o, i, end);
      if (v.pl_wait[s] > 0) {
        v.pl_wait[s] -= static_cast<int>(end - i);
        if (v.pl_wait[s] <= 1) v.pl_wait[s] = 0, excite(v, s);
      }
      if (end == i && !v.pl_wait[s]) continue;   // (plucked here: it sounds from here on)
      i = end;
    }
  }
  return static_cast<float>(any);
}

// Vox: an alto's first three formants (the Csound manual's appendix D: Hz, bandwidth, dB; the levels written here
// as amplitudes: 0, -4, -20 dB; 0, -9, -16; 0, -12, -30) for A, O and U
const float kVox[3][3][3] = {{{800.0f, 80.0f, 1.0f}, {1150.0f, 90.0f, 0.63096f}, {2800.0f, 120.0f, 0.1f}},
                             {{450.0f, 70.0f, 1.0f}, {800.0f, 80.0f, 0.35481f}, {2830.0f, 100.0f, 0.15849f}},
                             {{325.0f, 50.0f, 1.0f}, {700.0f, 60.0f, 0.25119f}, {2530.0f, 170.0f, 0.031623f}}};
// the chord's own waves (sum: the pad's, before its low pass) through them, into o: band passes in parallel
// (Zavalishin's state variable, its band normalized: a peak of 1), the vowel from A to O, towards U and back (~14 s)
void vox(Pad& v, const float* sum, float* o, uint32_t m, float rate, float ft) {   // (0.16.59: rate, Hz; ft
  v.vph += static_cast<float>(m) * (rate / kSr);                                  // its formants' shift)
  v.vph -= v.vph >= 1.0f ? 1.0f : 0.0f;
  const float w = 1.6f * (v.vph < 0.5f ? 2.0f * v.vph : 2.0f - 2.0f * v.vph);
  const int a = w < 1.0f ? 0 : 1;
  const float t = w - static_cast<float>(a);
  for (int f = 0; f < 3; ++f) {
    const float hz = ft * (kVox[a][f][0] + t * (kVox[a + 1][f][0] - kVox[a][f][0]));
    const float bw = ft * (kVox[a][f][1] + t * (kVox[a + 1][f][1] - kVox[a][f][1]));
    const float lv = kVox[a][f][2] + t * (kVox[a + 1][f][2] - kVox[a][f][2]);
    const float g = tpt_g(hz), k = bw / hz, d = 1.0f / (1.0f + g * (g + k)), kl = k * lv;
    float s1 = v.vs[f][0], s2 = v.vs[f][1];
    for (uint32_t i = 0; i < m; ++i) {
      const float hp = (sum[i] - (g + k) * s1 - s2) * d;
      const float bp = g * hp + s1;
      s1 = g * hp + bp;
      const float lp = g * bp + s2;
      s2 = g * bp + lp;
      o[i] += kl * bp;
    }
    v.vs[f][0] = fm_flush(s1), v.vs[f][1] = fm_flush(s2);
  }
}

// Air: a noise in narrow bands on the top four notes, an octave up (Q 28: a breath with a pitch), and a little of it
// broad, its lows taken off (the breath itself)
void air(Pad& v, float* o, uint32_t m, float q) {   // (0.16.59: q its bands' Q, 28 as before; TRANSP: lt)
  const int ns = v.nv < 4 ? v.nv : 4;
  float g[4], d[4];
  const float k = 1.0f / q, kq = k * fm_sqrtf(q * (1.0f / 28.0f));   // (narrower, as loud: its level kept)
  for (int j = 0; j < ns; ++j) {
    g[j] = tpt_g(note_hz(v.cur[v.nv - ns + j] + 12.0f + v.lt[v.nv - ns + j]));
    d[j] = 1.0f / (1.0f + g[j] * (g[j] + k));
  }
  uint32_t r = v.lr;
  float lp = v.alp;
  for (uint32_t i = 0; i < m; ++i) {
    const float x = bip(r);
    lp += 0.1f * (x - lp);
    float acc = 0.06f * (x - lp);
    for (int j = 0; j < ns; ++j) {
      float& s1 = v.as[j][0];
      float& s2 = v.as[j][1];
      const float hp = (x - (g[j] + k) * s1 - s2) * d[j];
      const float bp = g[j] * hp + s1;
      s1 = g[j] * hp + bp;
      const float l2 = g[j] * bp + s2;
      s2 = g[j] * bp + l2;
      acc += kq * bp;
    }
    o[i] += acc;
  }
  v.lr = r;
  v.alp = lp;
}

// Halo: over the top three notes, an octave and a fifth (0.55) and two octaves (0.35), into o at gain
void halo(Pad& v, float* o, uint32_t m, float gain) {
  const int ns = v.nv < 3 ? v.nv : 3;
  static const float kR[2] = {2.9966141f, 4.0f}, kA[2] = {0.55f, 0.35f};
  for (int j = 0; j < ns; ++j)
    for (int h = 0; h < 2; ++h) {
      const float f = note_hz(v.cur[v.nv - ns + j] + v.lt[v.nv - ns + j]) * kR[h];
      if (f > 0.42f * kSr) continue;
      const float e = circle(f), a = kA[h] * gain;
      float x = v.hx[j][h], y = v.hy[j][h];
      for (uint32_t i = 0; i < m; ++i) {
        x += e * y;
        y -= e * x;
        o[i] += a * y;
      }
      const float r = 0.5f * (1.0f + (1.0f - 0.25f * e * e) / (x * x + e * x * y + y * y + 1e-9f));
      v.hx[j][h] = x * r;
      v.hy[j][h] = y * r;
    }
}

// Swell: the top three notes an octave up and a wash of noise opening with it, rising as the cube of its time to the
// next trig, where it stops (a sound played backwards); cut short (6 ms) by a trig before, or 30 ms past it if none came
void swell(Pad& v, float* o, uint32_t m, float gain) {
  const int ns = v.nv < 3 ? v.nv : 3;
  float e[3];
  for (int j = 0; j < ns; ++j) e[j] = circle(note_hz(v.pitch[v.nv - ns + j] + 12.0f + v.lt[v.nv - ns + j]));
  const float len = static_cast<float>(v.sw_len > 0 ? v.sw_len : 1);
  const int fade = static_cast<int>(0.006f * kSr);
  for (uint32_t i = 0; i < m && v.sw_on; ++i) {
    float lvl;
    if (v.sw_on == 1) {
      const float r = clampf(static_cast<float>(v.sw_t) / len, 0.0f, 1.0f);
      lvl = r * r * r;
      v.sw_at = lvl;
      if (v.sw_t > v.sw_len + static_cast<int>(0.03f * kSr)) v.sw_on = 2, v.sw_fade = 0;
    } else {
      lvl = v.sw_at * (1.0f - static_cast<float>(v.sw_fade) / static_cast<float>(fade));
      if (++v.sw_fade >= fade) v.sw_on = 0, lvl = 0.0f;
    }
    float acc = 0.0f;
    for (int j = 0; j < ns; ++j) {
      v.swx[j] += e[j] * v.swy[j];
      v.swy[j] -= e[j] * v.swx[j];
      acc += (j == ns - 1 ? 0.5f : 0.35f) * v.swy[j];
    }
    v.sw_lp += (0.03f + 0.35f * lvl) * (bip(v.lr) - v.sw_lp);
    o[i] += gain * lvl * (acc + 0.5f * v.sw_lp);
    ++v.sw_t;
  }
}

// a trig: the swell's clock, the phases scattered from silence, the envelope from where it is, the layer's strike
// (0.16.57: out of pads_render, small: once a trig)
MI_COLD __attribute__((noinline)) void pad_trig(Pad& v, int ly, float la, float lv) {   // (0.16.59: la
  // CHARACTER, lv the layer's level; its transposition each voice's lt)
  // (Swell: the time between the last two trigs, the next one guessed from it; a swell under way cut)
  v.iv = v.since > static_cast<int>(0.12f * kSr) && v.since < static_cast<int>(8.0f * kSr) ? v.since : 0;
  v.since = 0;
  if (v.sw_on == 1) v.sw_on = 2, v.sw_fade = 0;
  if (!v.alive || v.env < 1e-3f) v.nvp = 0;   // (GLIDE: from silence, each voice on its note at once)
  // from silence: the phases scattered (a pad has no first cycle), the filter shut. (0.16.65) Only once it has stopped:
  // before, from a tail under -60 dB, its phases, filter and capacitor thrown, a step there; TRAUMA's drive, bringing a
  // quiet track up to its level, made it a click at each trig
  if (!v.alive) {
    for (int i = 0; i < kMaxV; ++i) {
      for (int j = 0; j < 3; ++j) v.ph[i][j] = 0.5f + 0.5f * bip(v.rnd);
      v.sx[i] = 0.0f;
      v.sy[i] = 1.0f;
      v.dx[i] = 0.0f;
      v.dy[i] = 1.0f;
    }
    for (int i = 0; i < 3; ++i) v.hx[i][0] = v.hx[i][1] = 0.0f, v.hy[i][0] = v.hy[i][1] = 1.0f;
    v.he = 0.0f;
    v.lp1 = v.lp2 = 0.0f;
    v.dcx = v.dcy = 0.0f;
    v.pend = 0.0f;
    v.env = 0.0f;
    for (int i = 0; i < kMaxV; ++i) v.vg[i] = i < v.nv ? 1.0f : 0.0f;   // (from silence: at once, the envelope rises)
    v.nf = v.nv;
    v.tone_s = 2.0f * v.k[5] - 1.0f;
  }
  v.a0 = v.env;
  v.att = 0.0f;
  v.dec = 1.0f;
  v.let_go = 0;
  v.alive = 1;
  if (ly == LY_BELL && v.nv > 0) {    // the bell: on the top note, an octave up; CHARACTER its index (as 0.16.57's
    float hz = note_hz(v.pitch[v.nv - 1] + 12.0f + v.lt[v.nv - 1]);   // at 0.5: x 1/2 .. x 2)
    // (0.16.82) its modulator (3.5 x) under 0.44 of the rate: higher, the bell an octave down, as often as it takes.
    // Before, over 12.6 kHz (a high chord, TRANSP up) the modulator's phase stepped more than a cycle a sample, its wrap
    // (one cycle) fell behind, the phase grew without end: the sine's fit off its cycle, huge, then a NaN, the pad
    // silent for good (its phase never wrapped back, not even STOP twice made it new)
    while (hz * 3.5f > 0.44f * kSr) hz *= 0.5f;
    v.bell_hz = hz;
    v.bell_e = lv;
    v.bell_i = exp2_b(2.0f * la - 1.0f);
  }
  if (ly == LY_CHIME && v.nv > 0) {   // (0.16.57) a chime at once, then now and then
    chime(v, lv);
    v.ch_wait = chime_gap(v, la);
  }
  if (ly == LY_KALIMBA && v.nv > 0) {   // the top note an octave up; from the middle, the next one too, 110 ms on
    strike(v, kKalimba, note_hz(v.pitch[v.nv - 1] + 12.0f + v.lt[v.nv - 1]), lv, 0);
    if (la >= 0.5f && v.nv >= 2)
      strike(v, kKalimba, note_hz(v.pitch[v.nv - 2] + 12.0f + v.lt[v.nv - 2]), 0.6f * lv, static_cast<int>(0.11f * kSr));
  }
  if (ly == LY_PLUCK && v.nv > 0) strum(v, la, lv);
}

}  // namespace

extern "C" {

void pads_render(float* out, uint32_t n) {
  Pad& v = pad;
  // ATTACK; (0.16.65) from a level (a trig over the chord still sounding), never quicker than 6 ms: at 1 ms, a step
  const float A = fm_maxf(attack_s(v.k[4]), v.a0 > 0.001f ? 0.006f : 0.0f);
  const int held = v.k[3] > 0.98f;
  const float tone = 2.0f * v.k[5] - 1.0f, ens = 2.0f * v.x[1] - 1.0f;   // (0.16.57: ENSEMBLE on the fourth page)
  // (0.16.59) TEXTURE and its AMOUNT on the fourth page; LAYER, its LEVEL, its CHARACTER, its TRANSP on the fifth
  const int tx = choice_of(v.x[2], TX_N - 1), ly = choice_of(v.x5[0], LY_N - 1);
  const float ta = clampf(v.x[3], 0.0f, 1.0f), la = clampf(v.x5[2], 0.0f, 1.0f);
  const float lv = lay_level(ly, kLaRef) * lay_gain(clampf(v.x5[1], 0.0f, 1.0f));
  const int trs = static_cast<int>(clampf(v.x5[3], -14.0f, 14.0f) + 14.5f) - 14;   // (0.16.68: steps of the scale)
  if (trs != v.lt_tr || v.lt_new) lay_steps(v, trs);
  if (v.trig) {
    v.trig = 0;
    pad_trig(v, ly, la, lv);
  }
  v.since += static_cast<int>(n);       // (Swell's clock, the pad sounding or not)
  if (!v.alive) {
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    return;
  }
  if (ly == LY_CHIME && v.nv > 0 && v.env > 0.04f) {   // Chime: the wind, as long as the chord sounds
    v.ch_wait -= static_cast<float>(n);
    if (v.ch_wait <= 0.0f) chime(v, lv), v.ch_wait += chime_gap(v, la);
  }
  // Swell: from silence, its time before the next trig (most of the time between them: 0.35 .. 1.75 s), the chord's
  if (ly == LY_SWELL && !v.sw_on && v.iv > 0 && v.nv > 0) {
    const float T = clampf((0.35f + 1.4f * la) * kSr, 0.0f, 0.85f * static_cast<float>(v.iv));
    const int at = v.since - static_cast<int>(n);   // (this block's start)
    if (at >= v.iv - static_cast<int>(T) && at < v.iv - 64) {
      v.sw_on = 1;
      v.sw_t = 0;
      v.sw_len = v.iv - at;
      for (int j = 0; j < 3; ++j) v.swx[j] = 0.0f, v.swy[j] = 1.0f;
      v.sw_lp = 0.0f;
    }
  }
  // Halo: its swell, the envelope followed slowly up (~1.8 s; CHARACTER 0 .. 1: 5.1 .. 0.64 s), quickly down (~0.25 s)
  if (ly == LY_HALO) {
    const float up = 1.8f * exp2_b(1.5f - 3.0f * la);
    v.he += (v.env - v.he) * (1.0f - exp_b(-static_cast<float>(n) / ((v.env > v.he ? up : 0.25f) * kSr)));
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
    tape = ta * (0.28f * sin_b(v.wow) + 0.08f * v.flut);   // (semitones)
  }
  static const float kDrift[kMaxV] = {1.0f, -0.6f, 0.4f, -1.0f, 0.8f, -0.3f};
  // (0.16.57) GLIDE: each voice towards its note in this chord (exponentially: there, within 5 %, in GLIDE's time); a
  // voice the last chord had not, there at once
  {
    const float gt = glide_s(v.k[6]);
    const float gkk = gt > 0.0f ? 1.0f - exp_b(-3.0f * static_cast<float>(n) / (gt * kSr)) : 1.0f;
    for (int i = 0; i < v.nv; ++i) v.cur[i] = i >= v.nvp ? v.pitch[i] : v.cur[i] + (v.pitch[i] - v.cur[i]) * gkk;
    v.nvp = v.nv;
  }
  // the voices' steps: the saws (ENSEMBLE to the right: three, apart; to the left one, VINTAGE: a few cents off and
  // wandering), the sine
  const int nsaw = ens > 0.02f ? 3 : 1;
  const float vint = ens < 0.0f ? -ens : 0.0f, wide = ens > 0.0f ? ens : 0.0f;
  float inc[kMaxV][3], idt[kMaxV][3], se[kMaxV];
  const int nf = v.nf > v.nv ? v.nf : v.nv;   // (0.16.65: the chord's voices, and those still going)
  for (int i = 0; i < nf; ++i) {
    float off = (0.04f + vint * 0.22f) * (static_cast<float>(i % 3) - 1.0f) + v.dr[i] * (0.03f + vint * 0.12f) + tape;
    if (tx == TX_DRIFT) off += ta * (0.32f * kDrift[i] + 0.15f * v.dr[i]);   // (Drift: out of tune, each its way)
    float f = note_hz(v.cur[i] + off);
    se[i] = 2.0f * sin_b(clampf(0.5f * f / kSr, 0.0f, 0.2f));   // (the circle's step: 2 sin(pi f / sr))
    for (int j = 0; j < nsaw; ++j) {
      float det = nsaw == 3 ? (static_cast<float>(j) - 1.0f) * (0.03f + 0.17f * wide) : 0.0f;   // (semitones)
      float hz = f * exp2_b(det * (1.0f / 12.0f));
      inc[i][j] = clampf(hz / kSr, 1e-5f, 0.45f);
      idt[i][j] = 1.0f / inc[i][j];
    }
  }
  // (0.16.65) TONE glided, ~8 ms (its saws, its sine and its cutoff: a chunk at a time); the level for the voices as
  // they will be (0.55 / sqrt(their levels' squares summed): the chord's notes, as before, once there)
  const float tk = 1.0f - fm_expf(-32.0f / (0.008f * kSr));
  float ts = v.tone_s;
  float norm_to = 0.55f / fm_sqrtf(static_cast<float>(v.nv > 0 ? v.nv : 1));
  if (v.gn <= 0.0f) v.gn = norm_to;
  const float gk = 1.0f - fm_expf(-1.0f / (0.004f * kSr));   // (~4 ms a step)
  float norm = v.gn;
  // the envelope
  const float dk = held && v.gate && !v.let_go ? 1.0f : exp_b(-6.9078f / ((v.let_go ? release_s(v.k[7]) : t60_s(v.k[3])) * kSr));
  const float ainc = 1.0f / (A * kSr);
  // the low pass: TONE, blooming with the envelope (an octave and a half over it)
  const float worn = tx == TX_TAPE ? 0.8f * ta : tx == TX_DOWN ? 0.7f * ta : 0.0f;   // (Tape's top worn, Down's)
  float fc0 = 0.0f;                     // (250 Hz .. 14 kHz: each chunk, TONE as it glides)
  // Down: an octave under each note (a sine), its step; Dust: its rate (44.1 kHz held down to 8), its bits (12 to 5)
  float de[kMaxV];
  if (tx == TX_DOWN)
    for (int i = 0; i < nf; ++i) de[i] = 0.5f * se[i];
  float g_saw = 0.0f, g_sin = 0.0f, g_down = 0.0f;   // (each chunk)
  const int hold = tx == TX_DUST ? 1 + static_cast<int>(ta * 4.5f) : 1;
  const float qstep = tx == TX_DUST ? exp2_b(-(12.0f - 7.0f * ta)) : 0.0f;
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
  // (0.16.57) LAYER: what it adds these 32 samples (lay), and what follows the pad's envelope and level (lye: Vox's,
  // Air's), their gains; ring: a strike or a string still sounding
  float lay[32], lye[32], ring = 0.0f;
  // (0.16.57) WAVE: its two shapes' weights (sine .. triangle .. saw .. square), each about as loud as a saw
  float ww[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float wz = clampf(v.x[0], 0.0f, 1.0f) * 3.0f;
  const int saw_only = fm_fabsf(wz - 2.0f) < 0.015f;
  const int wseg = wz >= 3.0f ? 2 : static_cast<int>(wz);
  {
    const float wf = wz - static_cast<float>(wseg);
    ww[wseg] = 1.0f - wf;
    ww[wseg + 1] = wf;
    ww[0] *= 0.82f;
    ww[3] *= 0.58f;
  }
  const WaveW wc = {4.0f * ww[0], 2.0f * ww[1], ww[2], ww[3], ww[2] + ww[3]};
  // (LAYER: nothing to add, none chosen and none still sounding; the pad alone, as before 0.16.57)
  const int lay_on = ly != LY_OFF || v.lring, lye_on = ly == LY_VOX || ly == LY_AIR;
  const float g_lye = ly == LY_VOX || ly == LY_AIR ? lv : 0.0f;
  // (0.16.59) Vox: its vowels' speed (CHARACTER: 57 s .. 3.6 s a round, 14 s at 0.5), its formants with TRANSP; Air:
  // its bands' Q (9.9 .. 79, 28 at 0.5)
  const float vox_rate = ly == LY_VOX ? 0.07f * exp2_b(4.0f * la - 2.0f) : 0.0f;
  const float vox_ft = ly == LY_VOX ? exp2_b(v.lt[0] * (1.0f / 12.0f)) : 1.0f;   // (its lowest voice's TRANSP)
  const float air_q = ly == LY_AIR ? 28.0f * exp2_b(4.5f * la - 2.25f) : 28.0f;
  // (0.16.64) the pad's states in locals over the block, put back after it (the compiler kept them in memory, out[]
  // might have been one of them: loaded and stored again every sample); its 32 samples' chunks, a loop each
  float att = v.att, dec = v.dec, e = v.env, lp1 = v.lp1, lp2 = v.lp2, dcx = v.dcx, dcy = v.dcy;
  float bell_m = v.bell_m, bell_c = v.bell_c, bell_e = v.bell_e, bell_i = v.bell_i;
  for (uint32_t s0 = 0; s0 < n; s0 += 32u) {
    const uint32_t m = n - s0 < 32u ? n - s0 : 32u;
    {                                   // TONE, gliding: the saws' and the sine's weights, the cutoff
      ts += (tone - ts) * tk;
      const float dark = ts < 0.0f ? -ts : 0.0f;
      g_saw = (1.0f - 0.85f * dark) / static_cast<float>(nsaw), g_sin = 0.15f + 0.85f * dark;
      g_down = tx == TX_DOWN ? 0.9f * ta * (g_saw + g_sin) : 0.0f;
      fc0 = 250.0f * exp2_b((ts + 1.0f) * 2.9f - worn);
    }
    // (0.16.65) the voices' levels over these samples: those of the chord up to 1, the others down to 0 (10 ms); a
    // voice on its way left to voice_fade
    float g0[kMaxV], dg[kMaxV];
    uint32_t fm = 0u;
    int ns = 0;
    float g2s = 0.0f;
    for (int i = 0; i < nf; ++i) {
      const float a = v.vg[i], to = i < v.nv ? 1.0f : 0.0f;
      const float b = to > a ? fm_minf(to, a + static_cast<float>(m) * kVoiceFade)
                             : fm_maxf(to, a - static_cast<float>(m) * kVoiceFade);
      g0[i] = a;
      dg[i] = (b - a) / static_cast<float>(m);
      v.vg[i] = b;
      g2s += b * b;
      if (a == 1.0f && b == 1.0f) ++ns;
      else fm |= 1u << i;
    }
    if (fm) norm_to = 0.55f / fm_sqrtf(g2s > 1.0f ? g2s : 1.0f);   // (the level: as the voices are)
    {                                   // the voices, 32 samples at a time
      if (saw_only) {                   // (WAVE on Saw: as before 0.16.57)
        if (nsaw == 3) voices<3>(v, sum, m, inc, idt, se, g_saw, g_sin, nf, fm, ns);
        else voices<1>(v, sum, m, inc, idt, se, g_saw, g_sin, nf, fm, ns);
      } else if (wseg == 0) {
        voices_ws<0>(v, nsaw, sum, m, inc, idt, se, g_saw, g_sin, wc, nf, fm, ns);
      } else if (wseg == 1) {
        voices_ws<1>(v, nsaw, sum, m, inc, idt, se, g_saw, g_sin, wc, nf, fm, ns);
      } else {
        voices_ws<2>(v, nsaw, sum, m, inc, idt, se, g_saw, g_sin, wc, nf, fm, ns);
      }
      if (fm) {
        for (int i = 0; fm >> i; ++i)
          if (((fm >> i) & 1u) && (g0[i] > 0.0f || dg[i] > 0.0f))
            voice_fade(v, i, nsaw, saw_only, sum, m, inc, idt, se, g_saw, g_sin, wc, g0[i], dg[i]);
        v.pend = sum[m];                // (the last sample, still open: theirs in it too)
      }
      if (g_down > 0.0f)                // (Down: each note's sine an octave under, added)
        for (int i = 0; i < nf; ++i) {
          float x = v.dx[i], y = v.dy[i];
          const float de_i = de[i], dd = g_down * dg[i];
          float gd = g_down * g0[i];
          for (uint32_t q = 0; q < m; ++q) {
            x += de_i * y;
            y -= de_i * x;
            gd += dd;
            sum[q] += gd * y;
          }
          const float r = 0.5f * (1.0f + (1.0f - 0.25f * de_i * de_i) / (x * x + de_i * x * y + y * y + 1e-9f));
          v.dx[i] = x * r;
          v.dy[i] = y * r;
        }
      if (lay_on) {                     // (0.16.57) LAYER
        for (uint32_t q = 0; q < m; ++q) lay[q] = 0.0f;
        if (lye_on)
          for (uint32_t q = 0; q < m; ++q) lye[q] = 0.0f;
        ring += strikes(v, lay, m) + plucks(v, lay, m);   // (struck: they ring out, the knob turned or not)
        if (v.sw_on) swell(v, lay, m, ly == LY_SWELL ? lv : lay_level(LY_SWELL, kLaRef));
        if (ly == LY_HALO && v.nv > 0) halo(v, lay, m, v.he * lv);
        if (ly == LY_VOX) vox(v, sum, lye, m, vox_rate, vox_ft);
        if (ly == LY_AIR && v.nv > 0) air(v, lye, m, air_q);
      }
    }
    for (uint32_t q = 0; q < m; ++q) {
      if (att < 1.0f) {
        att += ainc;
        float a = att < 1.0f ? att : 1.0f;
        e = v.a0 + (1.0f - v.a0) * a * a * (3.0f - 2.0f * a);
      } else {
        dec *= dk;
        e = dec;
      }
      // the low pass (a state variable, trapezoidal: Zavalishin's), its cutoff opening with the envelope (every 16
      // samples)
      if ((q & 15u) == 0u) {
        float x = 3.14159265f * clampf(fc0 * (0.35f + 0.65f * e) / kSr, 0.0f, 0.3f), x2 = x * x;
        float g = x * (15.0f - x2) / (15.0f - 6.0f * x2);   // (tan, Pade)
        fa1 = 1.0f / (1.0f + g * (g + 1.4f));
        fa2 = g * fa1;
        fa3 = g * fa2;
      }
      float v3 = sum[q] - lp2, v1 = fa1 * lp1 + fa2 * v3, v2 = lp2 + fa2 * lp1 + fa3 * v3;
      lp1 = 2.0f * v1 - lp1;
      lp2 = 2.0f * v2 - lp2;
      norm += (norm_to - norm) * gk;
      float y = v2 * norm * e;
      if (lay_on) {                     // (the layer: before TEXTURE wears it)
        y += lay[q];
        if (lye_on) y += g_lye * norm * e * lye[q];
      }
      if (bell_e > 1e-4f) {             // the bell: 1 : 3.5, its index falling fast, its level slowly
        bell_m += binc * 3.5f;
        bell_m -= bell_m >= 1.0f ? 1.0f : 0.0f;
        bell_c += binc;
        bell_c -= bell_c >= 1.0f ? 1.0f : 0.0f;
        float ph = bell_c + 0.35f * bell_i * sin_cyc(bell_m);
        ph -= static_cast<float>(static_cast<int>(ph));
        ph += ph < 0.0f ? 1.0f : 0.0f;
        y += 0.3f * bell_e * sin_cyc(ph);
        bell_e *= bk;
        bell_i *= bik;
      }
      if (tx == TX_TAPE) {              // (Tape: its saturation, the level about kept)
        const float z = y * drive;
        y = z / (1.0f + 0.6f * fm_fabsf(z)) * (1.0f + 0.5f * ta) / drive;
      } else if (tx == TX_DUST) {       // (Dust: held, on fewer bits; hiss, now and then a crackle; its state in locals)
        if (--d_h <= 0) d_h = hold, d_v = fm_floorf(y * qinv + 0.5f) * qstep;
        const uint32_t r = xs(d_rnd);   // (one draw: its bits the hiss, its top the crackle's chance)
        y = d_v + hiss * static_cast<float>(static_cast<int32_t>(r)) + d_cr;
        if ((r >> 20) < crk) d_cr = ta * 0.12f * bip(d_rnd);   // (~10 a s)
        d_cr *= 0.55f;
      }
      float yd = y - dcx + 0.9995f * dcy;
      dcx = y;
      dcy = yd;
      float ay = fm_fabsf(yd);          // a soft knee above 0.8 (at most 1.2)
      if (ay > 0.8f) {
        float z = (ay - 0.8f) * 2.5f;
        ay = 0.8f + 0.4f * z / (1.0f + z);
        yd = yd < 0.0f ? -ay : ay;
      }
      out[s0 + q] = yd;
    }
  }
  v.att = att, v.dec = dec, v.env = e, v.lp1 = lp1, v.lp2 = lp2, v.dcx = dcx, v.dcy = dcy;
  v.tone_s = ts;
  {
    int k = nf;                         // (the voices gone: out of the count)
    while (k > v.nv && v.vg[k - 1] <= 0.0f) --k;
    v.nf = k;
  }
  v.bell_m = bell_m, v.bell_c = bell_c, v.bell_e = bell_e, v.bell_i = bell_i;
  v.gn = norm;
  v.lring = ring > 0.0f || v.sw_on;
  v.dust_h = d_h, v.dust_v = d_v, v.crack = d_cr, v.rnd = d_rnd;
  // (0.16.82) a state gone wrong (a NaN, a value run away: the DC blocker's sees all the pad gives, the layer's too):
  // this block silent, the pad made new, sounding again at the next trig
  if (!(fm_fabsf(dcy) < 1e6f)) {
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    pads_silence();
    return;
  }
  if (v.att >= 1.0f && !(held && v.gate && !v.let_go) && v.env < 1e-5f && v.bell_e < 1e-4f && ring == 0.0f &&
      !v.sw_on) {
    v.alive = 0;
    v.env = 0.0f;
    v.gn = 0.0f;                        // (from silence: its level at once)
  }
}

int pads_voices(void) { return pad.alive ? pad.nv : 0; }

const char* pads_knob_name(int k) {
  static const char* const kN[8] = {"Prog", "Color", "Voicing", "Decay", "Attack", "Tone", "Glide", "Release"};
  return k >= 0 && k < 8 ? kN[k] : "";
}
const char* pads_page4_name(int k) {    // (0.16.57) the fourth page's (0.16.59: TEXTURE, its AMOUNT)
  static const char* const kN[4] = {"Wave", "Ensemble", "Texture", "Amount"};
  return k >= 0 && k < 4 ? kN[k] : "";
}
const char* pads_page5_name(int k) {    // (0.16.59) the fifth's
  static const char* const kN[4] = {"Layer", "Level", "Character", "Transp"};
  return k >= 0 && k < 4 ? kN[k] : "";
}
void pads_page4_defaults(float* x) {    // Saw, an ensemble, no texture (half of one, when it comes)
  static const float kD[4] = {kWaveSaw, 0.75f, 0.0f, 0.5f};
  for (int i = 0; i < 4; ++i) x[i] = kD[i];
}
void pads_page5_defaults(float* x) {    // no layer; as loud as 0.16.57's at its zone's middle, its character's middle,
  static const float kD[4] = {0.0f, kLevelUnity, 0.5f, 0.0f};   // not transposed
  for (int i = 0; i < 4; ++i) x[i] = kD[i];
}

MI_COLD void pads_defaults(float* k) {          // Tintin, triads, open, held, a slow swell, a little dark, no glide, its
  static const float kD[8] = {2.5f / 27.0f, 1.5f / 9.0f, 0.375f, 1.0f, 0.62f, 0.4f, 0.0f, kRelDefault};   // release
  for (int i = 0; i < 8; ++i) k[i] = kD[i];   // as it was before RELEASE (0.06 + 0.6 x ATTACK's 174 ms)
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
    case 6:                             // (0.16.57) GLIDE
      if (u < 0.02f) return put_s(b, n, &at, "Off");
      return put_time(b, n, &at, glide_s(u));
    case 7: return put_time(b, n, &at, release_s(u));   // (0.16.57) RELEASE
    case 5: return bip_text(b, n, &at, u, 0);
    default:
      break;
  }
}
// (0.16.57) the fourth page's: WAVE (a shape, or the two it is between), ENSEMBLE (as TONE: %, its side); (0.16.59)
// TEXTURE (its name), AMOUNT (%)
MI_COLD void pads_page4_text(int k, float u, char* b, int n) {
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  if (k == 2) return put_s(b, n, &at, kTexName[choice_of(u, TX_N - 1)]);
  u = clampf(u, 0.0f, 1.0f);
  switch (k) {
    case 0: {
      static const char* const kW[4] = {"Sine", "Tri", "Saw", "Square"};
      const float z = u * 3.0f;
      const int i = static_cast<int>(z + 0.5f);
      if (fm_fabsf(z - static_cast<float>(i)) < 0.04f) return put_s(b, n, &at, kW[i]);
      const int lo = static_cast<int>(z);
      put_s(b, n, &at, kW[lo]);
      put_s(b, n, &at, "/");
      return put_s(b, n, &at, kW[lo + 1]);
    }
    case 1: return bip_text(b, n, &at, u, 1);
    case 3:
      put_i(b, n, &at, static_cast<int>(u * 100.0f + 0.5f));
      return put_s(b, n, &at, "%");
    default:
      break;
  }
}
// (0.16.59) the fifth's: LAYER (its name), LEVEL (dB against 0.16.57's, Off at 0), CHARACTER (what it does with the
// layer playing now: its name, %), TRANSP (0.16.68: steps of the scale; semitones before)
MI_COLD void pads_page5_text(int k, float u, char* b, int n) {
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  switch (k) {
    case 0: return put_s(b, n, &at, kLayName[choice_of(u, LY_N - 1)]);
    case 1: {
      u = clampf(u, 0.0f, 1.0f);
      if (u < 0.01f) return put_s(b, n, &at, "Off");
      const float db = 20.0f * fm_log2f(lay_gain(u)) * 0.30103f;
      const int d = static_cast<int>(db + (db < 0.0f ? -0.5f : 0.5f));
      if (d > 0) put_s(b, n, &at, "+");
      else if (d < 0) put_s(b, n, &at, "-");
      put_i(b, n, &at, d < 0 ? -d : d);
      return put_s(b, n, &at, " dB");
    }
    case 2: {
      const int ly = choice_of(pad.x5[0], LY_N - 1);
      if (ly) {
        put_s(b, n, &at, kCharName[ly]);
        put_s(b, n, &at, " ");
      }
      put_i(b, n, &at, static_cast<int>(clampf(u, 0.0f, 1.0f) * 100.0f + 0.5f));
      return put_s(b, n, &at, "%");
    }
    case 3: {
      const int t = static_cast<int>(u + (u < 0.0f ? -0.5f : 0.5f));
      if (t > 0) put_s(b, n, &at, "+");
      else if (t < 0) put_s(b, n, &at, "-");
      return put_i(b, n, &at, t < 0 ? -t : t);
    }
    default:
      break;
  }
}

}  // extern "C"
