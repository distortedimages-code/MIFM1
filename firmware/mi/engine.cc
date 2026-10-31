// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's sound (mi/engine.h): four Plaits voices, Marbles as the only sequencer, two Tides as modulators, a matrix
// of cables, a mixer. Only bridge.h's C functions reach the modules: no module's header meets another's here.
//
// One audio block (128 frames, 2.9 ms) is one control step:
//   1. Marbles and Tides take their knobs as the cables left them at the last block;
//   2. Marbles runs 8 frames (16 audio frames each: its gates land within 0.4 ms), Tides one;
//   3. the sources are read, and every parameter is its knob plus its cables;
//   4. each voice plays the block, cut where its gate changes (a rising gate first takes its note from the source);
//      voices 3 and 4 on the second core while core 0 plays 1 and 2;
//   5. the mixer: level and pan per track, a soft limit on the sum.
#include <stdint.h>
#include <stddef.h>
extern "C" {
#include "plat.h"
#include "fastmath.h"
}
#include "bridge.h"
#include "engine.h"

namespace {

// ---------------------------------------------------------------- parameters ---
const char* const kGateNames[NGATE] = {"T1", "T2", "T3", "A1", "A2", "A3", "A4", "B1", "B2", "B3", "B4", "Keys"};
const char* const kNoteNames[NNOTE] = {"X1", "X2", "X3", "Y", "Fixed"};
const char* const kTidesOut[4] = {"Gates", "Amplitude", "Phase", "Frequency"};
const char* const kTidesRange[3] = {"Slow", "Medium", "Fast"};
const char* const kTidesTrig[5] = {"Free", "T1", "T2", "T3", "Clock"};
const char* const kTRange[3] = {"/4", "x1", "x4"};
const char* const kOnOff[2] = {"Off", "On"};
const char* const kXMode[3] = {"Identical", "Bump", "Tilt"};
const char* const kXRange[3] = {"+2 V", "+5 V", "+/-5 V"};

// the names are the modules' own, as their panels print them (sentence case: Mutable's)
const eng_pdef_t kPages[NPT][4] = {
  {{"Frequency", K_BIP, 0, 0, 0}, {"Harmonics", K_UNI, 0, 0, 0}, {"Timbre", K_UNI, 0, 0, 0}, {"Morph", K_UNI, 0, 0, 0}},
  {{"Decay", K_UNI, 0, 0, 0}, {"LPG colour", K_UNI, 0, 0, 0}, {"Timbre env", K_BIP, 0, 0, 0},
   {"Morph env", K_BIP, 0, 0, 0}},
  {{"Gate", K_ENUM, 0, NGATE - 1, kGateNames}, {"Note", K_ENUM, 0, NNOTE - 1, kNoteNames},
   {"Transpose", K_INT, -12, 12, 0}, {"Octave", K_INT, -3, 3, 0}},
  {{"Frequency", K_UNI, 0, 0, 0}, {"Shape", K_UNI, 0, 0, 0}, {"Slope", K_UNI, 0, 0, 0}, {"Smoothness", K_UNI, 0, 0, 0}},
  {{"Shift/level", K_UNI, 0, 0, 0}, {"Outputs", K_ENUM, 0, 3, kTidesOut}, {"Range", K_ENUM, 0, 2, kTidesRange},
   {"Trigger", K_ENUM, 0, 4, kTidesTrig}},
  {{"Rate", K_UNI, 0, 0, 0}, {"Bias", K_UNI, 0, 0, 0}, {"Jitter", K_UNI, 0, 0, 0}, {"Deja vu", K_UNI, 0, 0, 0}},
  {{"Length", K_INT, 1, 16, 0}, {"t range", K_ENUM, 0, 2, kTRange}, {"Deja vu t", K_ENUM, 0, 1, kOnOff},
   {"Deja vu X", K_ENUM, 0, 1, kOnOff}},
  {{"Spread", K_UNI, 0, 0, 0}, {"Bias", K_UNI, 0, 0, 0}, {"Steps", K_UNI, 0, 0, 0}, {"X mode", K_ENUM, 0, 2, kXMode}},
  {{"X range", K_ENUM, 0, 2, kXRange}, {"Y spread", K_UNI, 0, 0, 0}, {"Y bias", K_UNI, 0, 0, 0},
   {"Y steps", K_UNI, 0, 0, 0}},
  {{"Level 1", K_UNI, 0, 0, 0}, {"Level 2", K_UNI, 0, 0, 0}, {"Level 3", K_UNI, 0, 0, 0}, {"Level 4", K_UNI, 0, 0, 0}},
  {{"Send 1", K_UNI, 0, 0, 0}, {"Send 2", K_UNI, 0, 0, 0}, {"Send 3", K_UNI, 0, 0, 0}, {"Send 4", K_UNI, 0, 0, 0}},
  {{"Time", K_UNI, 0, 0, 0}, {"Damping", K_UNI, 0, 0, 0}, {"Diffusion", K_UNI, 0, 0, 0}, {"Return", K_UNI, 0, 0, 0}},
};

const char* const kModels[24] = {
  "VA VCF", "PHASE DIST", "6-OP FM 1", "6-OP FM 2", "6-OP FM 3", "WAVE TERRAIN", "STRING MACHINE", "CHIPTUNE",
  "VIRTUAL ANALOG", "WAVESHAPER", "FM 2-OP", "FORMANT", "HARMONIC", "WAVETABLE", "CHORDS", "SPEECH",
  "SWARM", "NOISE", "PARTICLES", "STRING", "MODAL", "KICK", "SNARE", "HI-HAT",
};
const char* const kRamps[3] = {"AD", "LOOPING", "AR"};
const char* const kTModels[7] = {"COIN TOSS", "CLUSTERS", "DRUMS", "INDEPENDENT", "DIVIDER", "THREE STATES",
                                 "MARKOV"};
const char* const kScales[6] = {"MAJOR", "MINOR", "PENTATONIC", "PELOG", "RAAG BHAIRAV", "RAAG SHRI"};
const char* const kSrcNames[NSRC] = {"-", "A1", "A2", "A3", "A4", "B1", "B2", "B3", "B4", "X1", "X2", "X3", "Y",
                                     "T1", "T2", "T3"};

const float kTidesRatios[9] = {4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 16.0f, 1.0f / 32.0f, 1.0f / 64.0f};
const char* const kTidesRatioNames[9] = {"4 / beat", "2 / beat", "1 / beat", "1 / 2 beats", "1 / bar",
                                         "1 / 2 bars", "1 / 4 bars", "1 / 8 bars", "1 / 16 bars"};

int page_type(int page) {
  if (page < 12) return PT_PLAITS1 + page % 3;
  if (page < 16) return PT_TIDES1 + (page - 12) % 2;
  if (page < 20) return PT_MARB_T1 + (page - 16);
  return PT_MIX + (page - 20);
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

float bpm_of(float v) { return 30.0f * fm_exp2f(v * 3.321928f); }          // 0..1: 30..300 BPM
float tides_hz(float v, int range) {                                          // 0..1: 0.02..20 Hz, the range / 8, x 8
  float hz = 0.02f * fm_exp2f(v * 9.965784f);
  return range == 0 ? hz * 0.125f : range == 2 ? hz * 8.0f : hz;
}

// ------------------------------------------------------------------ engine ---
const uint32_t kN = 128;                // app.h APP_BLOCK
const uint32_t kFrames = 8;             // Marbles' frames per block
const float kSr = 44100.0f;

project_t proj;
float fin[ENG_NPARAMS];                 // the parameters after the cables (the render writes, the screen reads)
float src[NSRC];

#ifndef ENG_ARENA_BYTES
#define ENG_ARENA_BYTES (160u * 1024u)
#endif
uint8_t arena[ENG_ARENA_BYTES] __attribute__((aligned(8))) PLAT_POOL;
void* voice[ENG_TRACKS];
void* marbles;
void* tides[2];
void* reverb;
int ready;

volatile int playing, use_core1 = 1;
volatile int key_note[ENG_TRACKS], key_on[ENG_TRACKS];

uint8_t mgates[kFrames * 3];
float mcv[kFrames * 4];
float tout[2][4];
int vgate[ENG_TRACKS];                  // each voice's gate as last set
float vnote[ENG_TRACKS];                // its note (latched at the gate's rise)
uint32_t gates_now;

float vbuf[ENG_TRACKS][kN];
float vaux[ENG_TRACKS][kN];
float send[kN], wet_l[kN], wet_r[kN];
float meter[ENG_TRACKS + 1];
int8_t hist[NSRC][ENG_HIST];
uint32_t hist_at, hist_div;

volatile uint32_t load0, load1, peak, peak_hold_blocks, c1_ticks;
volatile int on_core1_last;

float sat(float x) {                    // a soft limit: tanh's shape up to |x| = 3, then flat
  x = clampf(x, -3.0f, 3.0f);
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

float src_value(int s) { return s > SRC_NONE && s < NSRC ? src[s] : 0.0f; }

void apply_cables() {
  for (int i = 0; i < ENG_NPARAMS; ++i) fin[i] = proj.val[i];
  for (int g = 0; g <= ENG_TRACKS; ++g) {
    for (int c = 0; c < ENG_CABLES; ++c) {
      const cable_t& k = proj.cable[g][c];
      if (!k.depth || k.src == SRC_NONE || k.src >= NSRC || k.dst >= ENG_NPARAMS) continue;
      const eng_pdef_t* d = eng_pdef(k.dst);
      if (d->kind != K_UNI && d->kind != K_BIP) continue;
      fin[k.dst] += static_cast<float>(k.depth) * 0.01f * src_value(k.src) * (d->kind == K_BIP ? 2.0f : 1.0f);
    }
  }
  for (int i = 0; i < ENG_NPARAMS; ++i) {
    const eng_pdef_t* d = eng_pdef(i);
    fin[i] = d->kind == K_BIP ? clampf(fin[i], -1.0f, 1.0f)
           : d->kind == K_UNI ? clampf(fin[i], 0.0f, 1.0f)
           : clampf(fin[i], static_cast<float>(d->lo), static_cast<float>(d->hi));
  }
}

inline float P(int page, int k) { return fin[page * 4 + k]; }
inline int Pi(int page, int k) { return static_cast<int>(fin[page * 4 + k] + (fin[page * 4 + k] < 0 ? -0.5f : 0.5f)); }

void set_marbles() {
  mm_params_t p;
  p.t_model = proj.algo[PG_MARBLES(0)];
  p.t_range = Pi(PG_MARBLES(1), 1);
  // RATE as BPM: at x1, marbles' clock is 2 Hz x 2^(rate / 12), T2 a beat
  p.rate = 12.0f * fm_log2f(bpm_of(P(PG_MARBLES(0), 0)) / 120.0f);
  p.t_bias = P(PG_MARBLES(0), 1);
  p.jitter = P(PG_MARBLES(0), 2);
  p.deja_vu = P(PG_MARBLES(0), 3);
  p.length = Pi(PG_MARBLES(1), 0);
  p.t_deja_vu = Pi(PG_MARBLES(1), 2);
  p.x_deja_vu = Pi(PG_MARBLES(1), 3);
  p.spread = P(PG_MARBLES(2), 0);
  p.x_bias = P(PG_MARBLES(2), 1);
  p.steps = P(PG_MARBLES(2), 2);
  p.x_mode = Pi(PG_MARBLES(2), 3);
  p.scale = proj.algo[PG_MARBLES(2)];
  p.x_range = Pi(PG_MARBLES(3), 0);
  p.y_spread = P(PG_MARBLES(3), 1);
  p.y_bias = P(PG_MARBLES(3), 2);
  p.y_steps = P(PG_MARBLES(3), 3);
  mm_set(marbles, &p);
}

float x_norm(float v) {                 // X and Y as 0..1 of their range
  int r = Pi(PG_MARBLES(3), 0);
  return r == 0 ? clampf(v * 0.5f, 0.0f, 1.0f) : r == 1 ? clampf(v * 0.2f, 0.0f, 1.0f)
                                                        : clampf(v * 0.1f + 0.5f, 0.0f, 1.0f);
}

void run_tides(uint32_t n) {
  float block_hz = kSr / static_cast<float>(n);
  float beat_hz = bpm_of(P(PG_MARBLES(0), 0)) / 60.0f;
  for (int i = 0; i < 2; ++i) {
    int trig = Pi(PG_TIDES(i, 1), 3);
    float hz = tides_hz(P(PG_TIDES(i, 0), 0), Pi(PG_TIDES(i, 1), 2));
    if (trig == 4) {                    // HORLOGE: a ratio of the beat
      int r = static_cast<int>(P(PG_TIDES(i, 0), 0) * 8.999f);
      hz = beat_hz * kTidesRatios[r < 0 ? 0 : r > 8 ? 8 : r];
    }
    mt_set(tides[i], proj.algo[PG_TIDES(i, 0)], Pi(PG_TIDES(i, 1), 1), 0, hz, P(PG_TIDES(i, 0), 1),
           P(PG_TIDES(i, 0), 2), P(PG_TIDES(i, 0), 3), P(PG_TIDES(i, 1), 0));
    int g = 0;
    if (trig >= 1 && trig <= 3) {       // T1..T3: high if it was in any of the block's frames
      for (uint32_t k = 0; k < kFrames; ++k) g |= mgates[k * 3 + trig - 1];
    }
    mt_gate(tides[i], g);
    mt_render(tides[i], tout[i], 1, block_hz);
  }
}

void read_sources() {
  // Tides' outputs are volts: 0..8 V for AD and AR, +/-5 V for a loop (its gates 0 / 8 V): as -1..1
  for (int i = 0; i < 2; ++i) {
    float k = proj.algo[PG_TIDES(i, 0)] == 1 ? 0.2f : 0.125f;
    for (int c = 0; c < 4; ++c) src[(i ? SRC_B1 : SRC_A1) + c] = clampf(tout[i][c] * k, -1.0f, 1.0f);
  }
  const float* last = &mcv[(kFrames - 1) * 4];
  src[SRC_X1] = x_norm(last[0]);
  src[SRC_X2] = x_norm(last[1]);
  src[SRC_X3] = x_norm(last[2]);
  src[SRC_Y] = x_norm(last[3]);
  for (int t = 0; t < 3; ++t) src[SRC_T1 + t] = mgates[(kFrames - 1) * 3 + t] ? 1.0f : 0.0f;
}

// a track's gate in Marbles' frame k
int gate_at(int t, int k) {
  if (key_on[t]) return 1;
  int g = Pi(PG_TRACK(t, 2), 0);
  if (g <= GATE_T3) return mgates[k * 3 + g];
  if (g <= GATE_B4) return src[SRC_A1 + (g - GATE_A1)] > 0.5f;
  return 0;
}

float note_of(int t, int k) {           // the note a rising gate takes
  float base = 48.0f + 12.0f * static_cast<float>(Pi(PG_TRACK(t, 2), 3)) + static_cast<float>(Pi(PG_TRACK(t, 2), 2));
  if (key_on[t]) return static_cast<float>(key_note[t]) + static_cast<float>(Pi(PG_TRACK(t, 2), 2));
  int s = Pi(PG_TRACK(t, 2), 1);
  if (s == NOTE_FIXED) return base;
  float v = mcv[k * 4 + (s == NOTE_Y ? 3 : s)];
  return base + 12.0f * v;
}

void set_voice(int t) {
  int pg1 = PG_TRACK(t, 0), pg2 = PG_TRACK(t, 1);
  mp_set(voice[t], proj.algo[pg1], vnote[t] + 24.0f * P(pg1, 0), P(pg1, 1), P(pg1, 2), P(pg1, 3), P(pg2, 0),
         P(pg2, 1));
  mp_amounts(voice[t], 0.0f, P(pg2, 2), P(pg2, 3));
}

void render_voice(int t, uint32_t n) {
  uint32_t step = n / kFrames, pos = 0;
  for (uint32_t k = 0; k < kFrames; ++k) {
    int g = gate_at(t, static_cast<int>(k));
    if (g == vgate[t]) continue;
    uint32_t at = k * step;
    if (at > pos) {
      mp_render(voice[t], &vbuf[t][pos], &vaux[t][pos], at - pos);
      pos = at;
    }
    if (g) {
      vnote[t] = note_of(t, static_cast<int>(k));
      set_voice(t);
    }
    mp_gate(voice[t], g);
    vgate[t] = g;
  }
  if (n > pos) mp_render(voice[t], &vbuf[t][pos], &vaux[t][pos], n - pos);
}

volatile uint32_t job_n;
void core1_job(uint32_t) {
  uint32_t t0 = plat_ticks24();
  render_voice(2, job_n);
  render_voice(3, job_n);
  c1_ticks = plat_ticks24() - t0;
}

uint32_t pct10(uint32_t ticks, uint32_t n) {   // ticks as tenths of a percent of n frames' time
  float block_ticks = static_cast<float>(n) * (static_cast<float>(PLAT_TICKS_PER_US) * 1000000.0f / kSr);
  float p = static_cast<float>(ticks) * 1000.0f / block_ticks;
  return static_cast<uint32_t>(p > 65000.0f ? 65000.0f : p);
}

}  // namespace

extern "C" {

int eng_page_type(int page) { return page_type(page); }
int eng_page_track(int page) { return page < 12 ? page / 3 : -1; }

const eng_pdef_t* eng_pdef(int param) {
  if (param < 0 || param >= ENG_NPARAMS) param = 0;
  return &kPages[page_type(param / 4)][param % 4];
}

int eng_param_mod(int param) {
  int k = eng_pdef(param)->kind;
  return k == K_UNI || k == K_BIP;
}

static void put(char* b, int n, const char* s) {
  int i = 0;
  while (s[i] && i < n - 1) {
    b[i] = s[i];
    ++i;
  }
  b[i] = 0;
}

static void put_num(char* b, int n, int32_t v, int decimals, int sign) {   // v in units of 10^-decimals
  char t[16];
  int i = 0, neg = v < 0;
  uint32_t u = static_cast<uint32_t>(neg ? -v : v);
  for (int d = 0; d < decimals; ++d) {
    t[i++] = static_cast<char>('0' + u % 10u);
    u /= 10u;
  }
  if (decimals) t[i++] = '.';
  do {
    t[i++] = static_cast<char>('0' + u % 10u);
    u /= 10u;
  } while (u && i < 13);
  if (neg) t[i++] = '-';
  else if (sign && v) t[i++] = '+';
  int k = 0;
  while (i && k < n - 1) b[k++] = t[--i];
  b[k] = 0;
}

void eng_param_text(int param, float v, char* b, int n) {
  const eng_pdef_t* d = eng_pdef(param);
  int pt = page_type(param / 4), k = param % 4;
  if (d->kind == K_ENUM) {
    int i = static_cast<int>(v + 0.5f);
    put(b, n, d->names[i < d->lo ? d->lo : i > d->hi ? d->hi : i]);
  } else if (d->kind == K_INT) {
    put_num(b, n, static_cast<int32_t>(v + (v < 0 ? -0.5f : 0.5f)), 0, 1);
  } else if (pt == PT_MARB_T1 && k == 0) {
    put_num(b, n, static_cast<int32_t>(bpm_of(v) + 0.5f), 0, 0);
    int l = 0;
    while (b[l]) ++l;
    put(b + l, n - l, " BPM");
  } else if (pt == PT_PLAITS1 && k == 0) {
    put_num(b, n, static_cast<int32_t>(v * 240.0f + (v < 0 ? -0.5f : 0.5f)), 1, 1);   // semitones, 1 decimal
  } else if (pt == PT_TIDES1 && k == 0 && Pi(PG_TIDES(param / 4 == PG_TIDES(1, 0) ? 1 : 0, 1), 3) == 4) {
    int r = static_cast<int>(v * 8.999f);
    put(b, n, kTidesRatioNames[r < 0 ? 0 : r > 8 ? 8 : r]);
  } else if (pt == PT_TIDES1 && k == 0) {
    int tide = param / 4 == PG_TIDES(1, 0) ? 1 : 0;
    float hz = tides_hz(v, static_cast<int>(proj.val[PG_TIDES(tide, 1) * 4 + 2] + 0.5f));
    put_num(b, n, static_cast<int32_t>(hz * 100.0f + 0.5f), 2, 0);
    int l = 0;
    while (b[l]) ++l;
    put(b + l, n - l, " Hz");
  } else if (d->kind == K_BIP) {
    put_num(b, n, static_cast<int32_t>(v * 100.0f + (v < 0 ? -0.5f : 0.5f)), 0, 1);
  } else {
    put_num(b, n, static_cast<int32_t>(v * 100.0f + 0.5f), 0, 0);
  }
}

const char* eng_src_name(int s) { return s >= 0 && s < NSRC ? kSrcNames[s] : "?"; }

int eng_algo_count(int page) {
  switch (page_type(page)) {
    case PT_PLAITS1: return 24;
    case PT_TIDES1: return 3;
    case PT_MARB_T1: return 7;
    case PT_MARB_X1: return 6;
    default: return 0;
  }
}

const char* eng_algo_name(int page, int a) {
  int n = eng_algo_count(page);
  if (!n) return "";
  a = a < 0 ? 0 : a >= n ? n - 1 : a;
  switch (page_type(page)) {
    case PT_PLAITS1: return kModels[a];
    case PT_TIDES1: return kRamps[a];
    case PT_MARB_T1: return kTModels[a];
    default: return kScales[a];
  }
}

project_t* eng_project(void) { return &proj; }

static void cable(project_t* p, int g, int c, int s, int dst, int depth) {
  p->cable[g][c].src = static_cast<uint8_t>(s);
  p->cable[g][c].dst = static_cast<uint8_t>(dst);
  p->cable[g][c].depth = static_cast<int8_t>(depth);
  p->cable[g][c].rsv = 0;
}

void eng_defaults(project_t* p) {
  uint8_t* raw = reinterpret_cast<uint8_t*>(p);
  for (uint32_t i = 0; i < sizeof(project_t); ++i) raw[i] = 0;
  p->magic = PROJECT_MAGIC;
  p->version = PROJECT_VERSION;
  p->size = sizeof(project_t);
  static const float kTrack[3][4] = {{0.0f, 0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.0f, 0.0f}, {0, 0, 0, 0}};
  static const int kGate[4] = {GATE_T2, GATE_T1, GATE_T3, GATE_T3};
  static const int kNote[4] = {NOTE_FIXED, NOTE_X1, NOTE_X2, NOTE_X3};
  static const int kOct[4] = {-1, -2, 0, 0};
  static const int kModel[4] = {21, 8, 10, 20};   // kick, virtual analog, FM 2-op, modal
  static const float kSend[4] = {0.0f, 0.1f, 0.3f, 0.5f};   // the spec's Clouds sends, now the reverb's
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int pg = 0; pg < 3; ++pg)
      for (int k = 0; k < 4; ++k) p->val[PG_TRACK(t, pg) * 4 + k] = kTrack[pg][k];
    p->val[PG_TRACK(t, 2) * 4 + 0] = static_cast<float>(kGate[t]);
    p->val[PG_TRACK(t, 2) * 4 + 1] = static_cast<float>(kNote[t]);
    p->val[PG_TRACK(t, 2) * 4 + 3] = static_cast<float>(kOct[t]);
    p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(kModel[t]);
    p->val[PG_MIX * 4 + t] = 0.75f;
    p->val[PG_FX(0) * 4 + t] = kSend[t];
  }
  p->val[PG_TRACK(0, 1) * 4 + 0] = 0.4f;          // the kick: a short decay, its own colour
  p->val[PG_TRACK(1, 0) * 4 + 3] = 0.3f;          // the bass: MORPH low, opened by Tides A
  p->val[PG_TRACK(1, 1) * 4 + 0] = 0.35f;
  p->val[PG_TRACK(2, 1) * 4 + 0] = 0.45f;
  p->val[PG_TRACK(3, 1) * 4 + 0] = 0.7f;          // the resonator rings longer
  p->val[PG_MIX * 4 + 0] = 0.9f;
  p->val[PG_FX(1) * 4 + 0] = 0.6f;               // the reverb: a medium room, a little dark, its return at 60
  p->val[PG_FX(1) * 4 + 1] = 0.4f;
  p->val[PG_FX(1) * 4 + 2] = 0.6f;
  p->val[PG_FX(1) * 4 + 3] = 0.6f;
  // Tides A: AD from T1, Tides B: a loop on the clock, phases spread
  p->algo[PG_TIDES(0, 0)] = 0;
  p->algo[PG_TIDES(1, 0)] = 1;
  static const float kTides[2][8] = {{0.55f, 0.3f, 0.2f, 0.5f, 0.5f, 2.0f, 1.0f, 1.0f},
                                     {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 2.0f, 1.0f, 4.0f}};
  for (int i = 0; i < 2; ++i)
    for (int k = 0; k < 8; ++k) p->val[PG_TIDES(i, k / 4) * 4 + k % 4] = kTides[i][k];
  p->val[PG_TIDES(1, 0) * 4 + 0] = 4.5f / 9.0f;  // HORLOGE: one cycle a bar
  // Marbles: 120 BPM, DEJA VU at noon, 8 steps, C minor
  // (RATE 0.60206: 30 x 2^(0.60206 x 3.3219) = 120 BPM; LENGTH 8, x1, DEJA VU on T and X; X: narrow, Y smooth)
  static const float kMarbles[16] = {0.60206f, 0.5f, 0.0f, 0.5f,  8.0f, 1.0f, 1.0f, 1.0f,
                                     0.5f, 0.5f, 0.6f, 0.0f,      0.0f, 0.5f, 0.5f, 0.0f};
  for (int k = 0; k < 16; ++k) p->val[PG_MARBLES(k / 4) * 4 + k % 4] = kMarbles[k];
  p->algo[PG_MARBLES(0)] = 0;
  p->algo[PG_MARBLES(2)] = 1;
  // the cables of the spec's patch
  cable(p, 0, 0, SRC_Y, PG_TRACK(0, 0) * 4 + 2, 20);    // track 1: Y -> TIMBRE, a little
  cable(p, 1, 0, SRC_A1, PG_TRACK(1, 0) * 4 + 3, 50);   // track 2: A1 -> MORPH (the AD envelope)
  cable(p, 2, 0, SRC_B1, PG_TRACK(2, 0) * 4 + 2, 40);   // track 3: B1 -> TIMBRE (the loop)
  cable(p, 3, 0, SRC_Y, PG_TRACK(3, 0) * 4 + 1, 40);    // track 4: Y -> HARMONICS (structure)
  cable(p, 3, 1, SRC_B2, PG_TRACK(3, 0) * 4 + 2, 30);   // and B2 -> TIMBRE (brightness)
}

int eng_check(const project_t* p) {
  return p->magic == PROJECT_MAGIC && p->version == PROJECT_VERSION && p->size == sizeof(project_t);
}

int eng_init(void) {
  uint32_t need = 0, a = 0;
  need = 4u * ((mp_size() + 7u) & ~7u) + ((mm_size() + 7u) & ~7u) + 2u * ((mt_size() + 7u) & ~7u) +
         ((mrv_size() + 7u) & ~7u);
  if (need > sizeof(arena)) return -1;
  for (int t = 0; t < ENG_TRACKS; ++t) {
    voice[t] = arena + a;
    a += (mp_size() + 7u) & ~7u;
    mp_init(voice[t]);
    vgate[t] = 0;
    vnote[t] = 48.0f;
    key_on[t] = 0;
  }
  marbles = arena + a;
  a += (mm_size() + 7u) & ~7u;
  mm_init(marbles, 0x2A5u, kSr / static_cast<float>(kN / kFrames));
  for (int i = 0; i < 2; ++i) {
    tides[i] = arena + a;
    a += (mt_size() + 7u) & ~7u;
    mt_init(tides[i]);
  }
  reverb = arena + a;
  a += (mrv_size() + 7u) & ~7u;
  mrv_init(reverb);
  if (!eng_check(&proj)) eng_defaults(&proj);
  for (uint32_t k = 0; k < kFrames * 3; ++k) mgates[k] = 0;
  for (uint32_t k = 0; k < kFrames * 4; ++k) mcv[k] = 0.0f;
  apply_cables();
  ready = 1;
  return 0;
}

void eng_play(int on) {
  if (on && !playing && ready) mm_reset(marbles);
  playing = on != 0;
}
int eng_playing(void) { return playing; }

void eng_key(int track, int note, int on) {
  if (track < 0 || track >= ENG_TRACKS) return;
  if (on) {
    key_note[track] = note;
    key_on[track] = 1;
  } else if (key_note[track] == note) {
    key_on[track] = 0;
  }
}

void eng_core1(int on) { use_core1 = on; }

void eng_render(float* l, float* r, uint32_t n) {
  if (!ready || n > kN || n < kFrames || !eng_check(&proj)) {
    for (uint32_t i = 0; i < n; ++i) l[i] = r[i] = 0.0f;
    return;
  }
  uint32_t t0 = plat_ticks24();
  // 1-2. the sequencer and the modulators, with the knobs and cables of the last block
  set_marbles();
  if (playing) {
    mm_render(marbles, mgates, mcv, kFrames);
  } else {
    for (uint32_t k = 0; k < kFrames * 3; ++k) mgates[k] = 0;
  }
  run_tides(n);
  // 3. the sources, then every parameter
  read_sources();
  apply_cables();
  for (int t = 0; t < ENG_TRACKS; ++t) set_voice(t);
  // 4. the voices: 3 and 4 on the second core
  job_n = n;
  int on_core1 = use_core1 && plat_cpu2_on() ? plat_cpu2_run(core1_job, 0) : 0;
  render_voice(0, n);
  render_voice(1, n);
  uint32_t c0 = plat_ticks24() - t0, c1 = 0;
  if (on_core1) {
    if (plat_cpu2_wait() != 0) {        // given up meanwhile: the job was dropped, do it here
      on_core1 = 0;
      core1_job(0);
    }
    c1 = c1_ticks;
  } else {
    core1_job(0);
    c0 = plat_ticks24() - t0;
  }
  on_core1_last = on_core1;
  // 5. the mixer: the levels, the sends into Clouds' reverb (core 0, once the four voices are done), its return
  uint32_t g = 0;
  for (int t = 0; t < ENG_TRACKS; ++t) g |= vgate[t] ? 1u << t : 0u;
  gates_now = g;
  float gain[ENG_TRACKS], sends[ENG_TRACKS];
  for (int t = 0; t < ENG_TRACKS; ++t) {
    gain[t] = P(PG_MIX, t) * 0.7f;
    sends[t] = P(PG_FX(0), t) * gain[t];   // post-fader
  }
  mrv_set(reverb, P(PG_FX(1), 0), P(PG_FX(1), 1), P(PG_FX(1), 2));
  float ret = P(PG_FX(1), 3) * 1.6f, pk[ENG_TRACKS + 1] = {0, 0, 0, 0, 0};
  for (uint32_t i = 0; i < n; ++i) {
    float s = 0.0f;
    for (int t = 0; t < ENG_TRACKS; ++t) {
      s += sends[t] * vbuf[t][i];
      float a = fm_fabsf(vbuf[t][i] * gain[t]);
      if (a > pk[t]) pk[t] = a;
    }
    send[i] = s;
  }
  mrv_process(reverb, send, wet_l, wet_r, n);
  for (uint32_t i = 0; i < n; ++i) {
    float dry = 0.0f;
    for (int t = 0; t < ENG_TRACKS; ++t) dry += gain[t] * vbuf[t][i];
    float wl = ret * wet_l[i], wr = ret * wet_r[i];
    float a = fm_fabsf(wl) > fm_fabsf(wr) ? fm_fabsf(wl) : fm_fabsf(wr);
    if (a > pk[ENG_TRACKS]) pk[ENG_TRACKS] = a;
    l[i] = sat(dry + wl);
    r[i] = sat(dry + wr);
  }
  for (int t = 0; t <= ENG_TRACKS; ++t)    // the meters: up at once, down slowly
    meter[t] = pk[t] > meter[t] ? pk[t] : meter[t] * 0.985f;
  if (++hist_div >= 4u) {                 // the sources' past, for the screen
    hist_div = 0;
    for (int s = 1; s < NSRC; ++s) {
      float v = src[s] * 127.0f;
      hist[s][hist_at] = static_cast<int8_t>(v > 127.0f ? 127 : v < -127.0f ? -127 : static_cast<int>(v));
    }
    hist_at = (hist_at + 1u) % ENG_HIST;
  }
  // the load: one-pole averages, the worst total held a second
  uint32_t tot = plat_ticks24() - t0;
  uint32_t p0 = pct10(c0, n), p1 = pct10(c1, n), pt = pct10(tot, n);
  load0 = (load0 * 15u + p0) / 16u;
  load1 = (load1 * 15u + p1) / 16u;
  if (pt > peak || ++peak_hold_blocks > 344u) {
    peak = pt;
    peak_hold_blocks = 0;
  }
}

float eng_final(int param) { return param >= 0 && param < ENG_NPARAMS ? fin[param] : 0.0f; }
float eng_src(int s) { return src_value(s); }
uint32_t eng_gates(void) { return gates_now; }
int eng_note(int t) { return t >= 0 && t < ENG_TRACKS ? static_cast<int>(vnote[t] + 0.5f) : 0; }
uint32_t eng_load(int core) { return core ? load1 : load0; }
uint32_t eng_peak(void) { return peak; }
float eng_meter(int t) { return t >= 0 && t <= ENG_TRACKS ? meter[t] : 0.0f; }
int eng_hist(int s, int i) {
  if (s <= SRC_NONE || s >= NSRC || i < 0 || i >= ENG_HIST) return 0;
  return hist[s][(hist_at + static_cast<uint32_t>(i)) % ENG_HIST];
}
int eng_core1_running(void) { return on_core1_last; }

}  // extern "C"
