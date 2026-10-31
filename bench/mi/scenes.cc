// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1 bench: the scenes and their timing. Each scene builds its modules in the arena (one at a time: they share
// it), plays them with a 16th-note clock (120 BPM) or, in the busy scenes, with Marbles' gates, and times the
// render with the FM-1's 24 MHz counter (plat_ticks24): core 0's part, the second core's job, and the whole.
// Only bridge.h's C functions are called here: no module's header meets another's.
#include <stdint.h>
#include <stddef.h>
extern "C" {
#include "plat.h"
}
#include "bridge.h"
#include "scenes.h"

#ifndef MI_ARENA_BYTES
#define MI_ARENA_BYTES (300u * 1024u)
#endif

// The FM-1's 581 KB of program flash do not hold the six modules at once (their tables alone are 300 KB): a
// variant builds some of them (tools/variant.py, from MIFM1_VARIANT, writes variant.h). 0: all (the computer)
#if defined(__has_include)
#if __has_include("variant.h")
#include "variant.h"
#endif
#endif
#ifndef MI_VARIANT
#define MI_VARIANT 0
#endif
#define MI_HAS_PLAITS 1
#define MI_HAS_RINGS (MI_VARIANT != 2)
#define MI_HAS_TIDES (MI_VARIANT != 2)
#define MI_HAS_MARBLES (MI_VARIANT != 2)
#define MI_HAS_CLOUDS (MI_VARIANT != 1)
#define MI_HAS_WARPS (MI_VARIANT != 1)

namespace {

// each module through a guard: a variant without it never references it, and the linker leaves it out
inline uint32_t g_mp_size() { return MI_HAS_PLAITS ? mp_size() : 0; }
inline void g_mp_init(void *mem) { if (MI_HAS_PLAITS) mp_init(mem); }
inline void g_mp_set(void *v, int model, float note, float harmonics, float timbre, float morph, float decay, float colour) { if (MI_HAS_PLAITS) mp_set(v, model, note, harmonics, timbre, morph, decay, colour); }
inline void g_mp_gate(void *v, int high) { if (MI_HAS_PLAITS) mp_gate(v, high); }
inline void g_mp_render(void *v, float *out, float *aux, uint32_t n) { if (MI_HAS_PLAITS) mp_render(v, out, aux, n); }
inline const char* g_mp_model_name(int model) { return MI_HAS_PLAITS ? mp_model_name(model) : ""; }
inline uint32_t g_mr_size() { return MI_HAS_RINGS ? mr_size() : 0; }
inline uint32_t g_mr_reverb_size() { return MI_HAS_RINGS ? mr_reverb_size() : 0; }
inline void g_mr_init(void *mem, void *reverb) { if (MI_HAS_RINGS) mr_init(mem, reverb); }
inline void g_mr_set(void *r, int model, int polyphony, float note, float structure, float brightness, float damping, float position) { if (MI_HAS_RINGS) mr_set(r, model, polyphony, note, structure, brightness, damping, position); }
inline void g_mr_strum(void *r) { if (MI_HAS_RINGS) mr_strum(r); }
inline void g_mr_render(void *r, float *out, float *aux, uint32_t n) { if (MI_HAS_RINGS) mr_render(r, out, aux, n); }
inline const char* g_mr_model_name(int model) { return MI_HAS_RINGS ? mr_model_name(model) : ""; }
inline uint32_t g_mc_size() { return MI_HAS_CLOUDS ? mc_size() : 0; }
inline void g_mc_init(void *mem, void *large, uint32_t large_size, void *small, uint32_t small_size) { if (MI_HAS_CLOUDS) mc_init(mem, large, large_size, small, small_size); }
inline void g_mc_set(void *c, int mode, int quality, float position, float size, float pitch, float density, float texture, float dry_wet, float spread, float feedback, float reverb, int freeze) { if (MI_HAS_CLOUDS) mc_set(c, mode, quality, position, size, pitch, density, texture, dry_wet, spread, feedback, reverb, freeze); }
inline void g_mc_render(void *c, const float *in_l, const float *in_r, float *out_l, float *out_r, uint32_t n) { if (MI_HAS_CLOUDS) mc_render(c, in_l, in_r, out_l, out_r, n); }
inline const char* g_mc_mode_name(int mode) { return MI_HAS_CLOUDS ? mc_mode_name(mode) : ""; }
inline uint32_t g_mw_size() { return MI_HAS_WARPS ? mw_size() : 0; }
inline void g_mw_init(void *mem) { if (MI_HAS_WARPS) mw_init(mem); }
inline void g_mw_set(void *w, int algo, float timbre, float level1, float level2) { if (MI_HAS_WARPS) mw_set(w, algo, timbre, level1, level2); }
inline void g_mw_render(void *w, const float *carrier, const float *modulator, float *out, float *aux, uint32_t n) { if (MI_HAS_WARPS) mw_render(w, carrier, modulator, out, aux, n); }
inline const char* g_mw_algo_name(int algo) { return MI_HAS_WARPS ? mw_algo_name(algo) : ""; }
inline uint32_t g_mt_size() { return MI_HAS_TIDES ? mt_size() : 0; }
inline void g_mt_init(void *mem) { if (MI_HAS_TIDES) mt_init(mem); }
inline void g_mt_set(void *t, int ramp, int output, int range, float hz, float shape, float slope, float smoothness, float shift) { if (MI_HAS_TIDES) mt_set(t, ramp, output, range, hz, shape, slope, smoothness, shift); }
inline void g_mt_gate(void *t, int high) { if (MI_HAS_TIDES) mt_gate(t, high); }
inline void g_mt_render(void *t, float *out4, uint32_t n, float sr) { if (MI_HAS_TIDES) mt_render(t, out4, n, sr); }
inline uint32_t g_mm_size() { return MI_HAS_MARBLES ? mm_size() : 0; }
inline void g_mm_init(void *mem, uint32_t seed) { if (MI_HAS_MARBLES) mm_init(mem, seed); }
inline void g_mm_set(void *m, int t_model, float rate, float t_bias, float jitter, float deja_vu, int length, float spread, float x_bias, float steps) { if (MI_HAS_MARBLES) mm_set(m, t_model, rate, t_bias, jitter, deja_vu, length, spread, x_bias, steps); }
inline void g_mm_render(void *m, uint8_t *gates3, float *cv4, uint32_t n) { if (MI_HAS_MARBLES) mm_render(m, gates3, cv4, n); }

const uint32_t kMaxN = 128;             // app.h APP_BLOCK
const uint32_t kWarmup = 40;            // blocks left out of the numbers after a start (buffers filling, first grains)
const uint32_t kStep = 5512;            // a 16th note at 120 BPM, in frames

enum Kind { K_PLAITS, K_RINGS, K_CLOUDS, K_WARPS, K_TIDES, K_MARBLES, K_COMBO };

struct SceneDef { uint8_t kind, a, b; };

const uint8_t kRingsScenes[][2] = {     // model, polyphony
  {0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {0, 2}, {0, 4}, {2, 4},
};
const int kNumRings = sizeof(kRingsScenes) / sizeof(kRingsScenes[0]);

// the busy scenes: core 0 plays the tracks, the second core the rest
enum { X_TRACKS, X_RINGS, X_CLOUDS_WARPS, X_FX_ONLY, X_ALL, X_COUNT };
const char* const kComboNames[X_COUNT] = {
  "4 PLAITS+MARBLES+2 TIDES", "MIX + RINGS (C1)", "MIX + CLOUDS+WARPS (C1)", "RINGS+CLOUDS+WARPS (C1)",
  "MIX + RINGS+CL+WARPS (C1)",
};
const char* const kComboShort[X_COUNT] = { "MIX 4 VOIX", "MIX+RINGS", "MIX+CL+WARPS", "FX SEULS C1", "MIX+TOUT" };

const int kMaxScenes = MP_MODELS + kNumRings + MC_MODES + MW_ALGOS + 2 + 1 + X_COUNT;
SceneDef scenes[kMaxScenes];
char names[kMaxScenes][32];
char shorts[kMaxScenes][16];
int num_scenes;

// the arena: every scene's modules, built again at each start
uint8_t arena[MI_ARENA_BYTES] PLAT_POOL __attribute__((aligned(8)));
uint32_t arena_used;

// the reduced Clouds of the busy scenes (its own buffers are 180 KB): 24 KB of sample memory a channel, and the
// 44 KB its diffuser, reverb and correlator take from the large one (granular_processor.cc Prepare)
const uint32_t kCloudsSmall = 24576u, kCloudsLarge = 24576u + 45056u;

inline uint32_t align8(uint32_t n) { return (n + 7u) & ~7u; }

void* take(uint32_t n) {
  uint8_t* p = &arena[arena_used];
  arena_used += align8(n);
  return p;
}

// ---- names, without a C library ----
void put(char* d, uint32_t max, const char* s) {
  uint32_t i = 0;
  while (*d && i < max - 1) { ++d; ++i; }
  while (*s && i < max - 1) { *d++ = *s++; ++i; }
  *d = 0;
}
void put2(char* d, uint32_t max, int v) {
  char t[3] = { static_cast<char>('0' + v / 10 % 10), static_cast<char>('0' + v % 10), 0 };
  put(d, max, t);
}

bool combo_here(int x) {
  if (MI_VARIANT == 1) return x == X_TRACKS || x == X_RINGS;
  if (MI_VARIANT == 2) return x == X_TRACKS || x == X_CLOUDS_WARPS || x == X_FX_ONLY;
  return true;
}

void build_table() {
  if (num_scenes) return;
  int n = 0;
  for (int m = 0; MI_HAS_PLAITS && MI_VARIANT != 2 && m < MP_MODELS; ++m, ++n) {   // (variant 2: in variant 1)
    scenes[n].kind = K_PLAITS; scenes[n].a = m;
    put(names[n], 32, "PLAITS "); put2(names[n], 32, m + 1); put(names[n], 32, " "); put(names[n], 32, g_mp_model_name(m));
    put(shorts[n], 16, "P "); put(shorts[n], 16, g_mp_model_name(m));
  }
  for (int i = 0; MI_HAS_RINGS && i < kNumRings; ++i, ++n) {
    scenes[n].kind = K_RINGS; scenes[n].a = kRingsScenes[i][0]; scenes[n].b = kRingsScenes[i][1];
    put(names[n], 32, "RINGS "); put(names[n], 32, g_mr_model_name(scenes[n].a));
    put(names[n], 32, " POLY "); put2(names[n], 32, scenes[n].b);
    put(shorts[n], 16, "R "); put(shorts[n], 16, g_mr_model_name(scenes[n].a));
    if (scenes[n].b > 1) { put(shorts[n], 16, " x"); put2(shorts[n], 16, scenes[n].b); }
  }
  for (int m = 0; MI_HAS_CLOUDS && m < MC_MODES; ++m, ++n) {
    scenes[n].kind = K_CLOUDS; scenes[n].a = m;
    put(names[n], 32, "CLOUDS "); put(names[n], 32, g_mc_mode_name(m));
    put(shorts[n], 16, "C "); put(shorts[n], 16, g_mc_mode_name(m));
  }
  for (int a = 0; MI_HAS_WARPS && a < MW_ALGOS; ++a, ++n) {
    scenes[n].kind = K_WARPS; scenes[n].a = a;
    put(names[n], 32, "WARPS "); put(names[n], 32, g_mw_algo_name(a));
    put(shorts[n], 16, "W "); put(shorts[n], 16, g_mw_algo_name(a));
  }
  if (MI_HAS_TIDES) {
    scenes[n].kind = K_TIDES; scenes[n].a = 0;
    put(names[n], 32, "TIDES x2 AUDIO"); put(shorts[n], 16, "T x2 AUDIO"); ++n;
    scenes[n].kind = K_TIDES; scenes[n].a = 1;
    put(names[n], 32, "TIDES x2 MODULATION"); put(shorts[n], 16, "T x2 MODUL"); ++n;
  }
  if (MI_HAS_MARBLES) {
    scenes[n].kind = K_MARBLES; scenes[n].a = 0;
    put(names[n], 32, "MARBLES T+X+Y"); put(shorts[n], 16, "MARBLES"); ++n;
  }
  for (int x = 0; x < X_COUNT; ++x) {
    if (!combo_here(x)) continue;
    scenes[n].kind = K_COMBO; scenes[n].a = x;
    if (x == X_TRACKS && !MI_HAS_MARBLES) {
      put(names[n], 32, "4 PLAITS (CLOCK)"); put(shorts[n], 16, "MIX 4 VOIX");
    } else if (x == X_FX_ONLY && !MI_HAS_RINGS) {
      put(names[n], 32, "CLOUDS+WARPS (C1)"); put(shorts[n], 16, "FX SEULS C1");
    } else {
      put(names[n], 32, kComboNames[x]); put(shorts[n], 16, kComboShort[x]);
    }
    ++n;
  }
  num_scenes = n;
}

uint32_t clouds_bytes(bool full) {
  return align8(g_mc_size()) + (full ? align8(MC_LARGE_FULL) + align8(MC_SMALL_FULL)
                                   : align8(kCloudsLarge) + align8(kCloudsSmall));
}

bool uses_rings(int x) { return MI_HAS_RINGS && (x == X_RINGS || x == X_FX_ONLY || x == X_ALL); }
bool uses_clouds(int x) { return MI_HAS_CLOUDS && (x == X_CLOUDS_WARPS || x == X_FX_ONLY || x == X_ALL); }
bool uses_warps(int x) { return MI_HAS_WARPS && (x == X_CLOUDS_WARPS || x == X_FX_ONLY || x == X_ALL); }

uint32_t need_of(const SceneDef& s) {
  switch (s.kind) {
    case K_PLAITS: return align8(g_mp_size());
    case K_RINGS: return align8(g_mr_size()) + (s.a == 5 ? align8(g_mr_reverb_size()) : 0u);
    case K_CLOUDS: return clouds_bytes(true);
    case K_WARPS: return align8(g_mw_size());
    case K_TIDES: return 2u * align8(g_mt_size());
    case K_MARBLES: return align8(g_mm_size());
    default: break;
  }
  uint32_t n = s.a == X_FX_ONLY ? 0u : 4u * align8(g_mp_size()) + (MI_HAS_TIDES ? 2u * align8(g_mt_size()) : 0u) +
                                       (MI_HAS_MARBLES ? align8(g_mm_size()) : 0u);
  if (uses_rings(s.a)) n += align8(g_mr_size());
  if (uses_clouds(s.a)) n += clouds_bytes(false);
  if (uses_warps(s.a)) n += align8(g_mw_size());
  return n;
}

bool has_core1(const SceneDef& s) { return s.kind == K_COMBO && s.a != X_TRACKS; }

// ---- the playing state ----
volatile int ready;                     // the render plays the scene (0: silence while it is built)
volatile int use_core1 = 1;
int current = -1;
SceneDef cur;

void* plaits[4];
void* rings;
void* clouds;
void* warps;
void* tides[2];
void* marbles;

uint32_t clock_acc, rng = 0x12345678u, steps;
float lfo;                              // 0..1, one turn in 8 s
float pluck_env, pluck_phase, pluck_inc;   // the cheap input signal for Clouds and Warps
float osc_phase[2];
int c1_strum;                           // the second core's events, decided in the block before (it runs in parallel)
int strum_next;                         // ... and the ones decided during this block
float mods[2][4];                       // the two Tides as modulators (one value a block)
uint8_t last_gates[3];

float buf[4][kMaxN], aux[kMaxN], mix_l[kMaxN], mix_r[kMaxN], in_l[kMaxN], in_r[kMaxN];
float prev_mix[kMaxN], prev_v1[kMaxN], prev_v2[kMaxN];
float c1_l[kMaxN], c1_r[kMaxN], c1_tmp[kMaxN], c1_aux[kMaxN];
float tides_out[4 * kMaxN];
uint8_t m_gates[3 * kMaxN];
float m_cv[4 * kMaxN];
uint32_t block_n;

volatile uint32_t c1_ticks;             // the job's own time, measured where it ran
sc_stats_t st;

uint32_t xorshift(uint32_t* r) {
  *r ^= *r << 13;
  *r ^= *r >> 17;
  *r ^= *r << 5;
  return *r;
}
uint32_t next_rand() { return xorshift(&rng); }
uint32_t c1_rng = 0x9E3779B9u;          // the second core's own: the two run at once
float note_of(uint32_t r) {             // a minor pentatonic over two octaves
  static const uint8_t kPenta[10] = { 0, 3, 5, 7, 10, 12, 15, 17, 19, 22 };
  return 45.0f + kPenta[r % 10];
}
float semis_to_ratio(float s) {         // 2^(s/12) without libm: a few octaves either way are enough here
  float r = 1.0f;
  while (s >= 12.0f) { r *= 2.0f; s -= 12.0f; }
  while (s < 0.0f) { r *= 0.5f; s += 12.0f; }
  // 2^(s/12) on [0, 12): Taylor around the octave's middle is enough for a test tone
  float x = (s - 6.0f) * (0.6931472f / 12.0f);
  float e = 1.0f + x * (1.0f + x * (0.5f + x * (0.16666667f + x * 0.041666668f)));
  return r * 1.4142135f * e;
}
float hz_of(float note) { return 440.0f * semis_to_ratio(note - 69.0f); }

// a decaying saw: what Clouds and Warps chew on
void pluck(float* l, float* r, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    pluck_phase += pluck_inc;
    if (pluck_phase >= 1.0f) pluck_phase -= 1.0f;
    float s = (2.0f * pluck_phase - 1.0f) * pluck_env;
    pluck_env *= 0.99985f;
    l[i] = s;
    if (r) r[i] = s;
  }
}

// ---- the second core's job: the busy scenes' effects, from the block before (they run beside core 0) ----
void core1_job(uint32_t) {
  uint32_t t0 = plat_ticks24();
  uint32_t n = block_n;
  for (uint32_t i = 0; i < n; ++i) c1_l[i] = c1_r[i] = 0.0f;
  if (rings) {
    if (c1_strum) {
      g_mr_set(rings, 0, 1, note_of(xorshift(&c1_rng)) - 12.0f, 0.3f + 0.4f * lfo, 0.6f, 0.7f, 0.3f);
      g_mr_strum(rings);
    }
    g_mr_render(rings, c1_tmp, c1_aux, n);
    for (uint32_t i = 0; i < n; ++i) { c1_l[i] += 0.5f * c1_tmp[i]; c1_r[i] += 0.5f * c1_aux[i]; }
  }
  if (warps) {
    g_mw_render(warps, prev_v1, prev_v2, c1_tmp, c1_aux, n);
    for (uint32_t i = 0; i < n; ++i) { c1_l[i] += 0.3f * c1_tmp[i]; c1_r[i] += 0.3f * c1_tmp[i]; }
  }
  if (clouds) {
    g_mc_render(clouds, prev_mix, prev_mix, c1_tmp, c1_aux, n);
    for (uint32_t i = 0; i < n; ++i) { c1_l[i] += 0.5f * c1_tmp[i]; c1_r[i] += 0.5f * c1_aux[i]; }
  }
  c1_ticks = plat_ticks24() - t0;
}

void build(const SceneDef& s) {
  for (int i = 0; i < 4; ++i) plaits[i] = 0;
  rings = clouds = warps = marbles = 0;
  tides[0] = tides[1] = 0;
  arena_used = 0;
  switch (s.kind) {
    case K_PLAITS:
      plaits[0] = take(g_mp_size()); g_mp_init(plaits[0]);
      break;
    case K_RINGS: {
      rings = take(g_mr_size());
      void* reverb = s.a == 5 ? take(g_mr_reverb_size()) : 0;
      g_mr_init(rings, reverb);
      break;
    }
    case K_CLOUDS: {
      clouds = take(g_mc_size());
      void* large = take(MC_LARGE_FULL);
      void* small = take(MC_SMALL_FULL);
      g_mc_init(clouds, large, MC_LARGE_FULL, small, MC_SMALL_FULL);
      break;
    }
    case K_WARPS:
      warps = take(g_mw_size()); g_mw_init(warps);
      break;
    case K_TIDES:
      for (int i = 0; i < 2; ++i) { tides[i] = take(g_mt_size()); g_mt_init(tides[i]); }
      break;
    case K_MARBLES:
      marbles = take(g_mm_size()); g_mm_init(marbles, 1);
      break;
    default: {
      static const uint8_t kModels[4] = { 21, 8, 10, 20 };   // kick, VA, FM 2-op, modal
      if (s.a != X_FX_ONLY) {
        for (int i = 0; i < 4; ++i) {
          plaits[i] = take(g_mp_size()); g_mp_init(plaits[i]);
          g_mp_set(plaits[i], kModels[i], 48.0f, 0.5f, 0.5f, 0.5f, i == 0 ? 0.6f : 0.4f, 0.5f);
        }
        for (int i = 0; MI_HAS_TIDES && i < 2; ++i) {
          tides[i] = take(g_mt_size()); g_mt_init(tides[i]);
          g_mt_set(tides[i], 1, 2, 0, i ? 0.13f : 0.31f, 0.5f, 0.5f, 0.5f, 0.5f);   // two slow LFOs, four phases
        }
        if (MI_HAS_MARBLES) {
          marbles = take(g_mm_size()); g_mm_init(marbles, 7);
          g_mm_set(marbles, 0, 0.0f, 0.5f, 0.1f, 0.4f, 8, 0.5f, 0.5f, 0.7f);
        }
      }
      if (uses_rings(s.a)) { rings = take(g_mr_size()); g_mr_init(rings, 0); }
      if (uses_clouds(s.a)) {
        clouds = take(g_mc_size());
        void* large = take(kCloudsLarge);
        void* small = take(kCloudsSmall);
        g_mc_init(clouds, large, kCloudsLarge, small, kCloudsSmall);
        g_mc_set(clouds, 0, 0, 0.4f, 0.5f, 0.0f, 0.6f, 0.5f, 0.6f, 0.5f, 0.2f, 0.4f, 0);
      }
      if (uses_warps(s.a)) { warps = take(g_mw_size()); g_mw_init(warps); g_mw_set(warps, 2, 0.5f, 0.8f, 0.8f); }
      break;
    }
  }
}

// one block of a single-module scene (core 0, timed)
void render_single(uint32_t n, bool step) {
  const SceneDef& s = cur;
  switch (s.kind) {
    case K_PLAITS:
      if (step) {
        g_mp_set(plaits[0], s.a, note_of(next_rand()), 0.2f + 0.6f * lfo, 0.3f + 0.4f * lfo, 0.7f - 0.4f * lfo, 0.5f, 0.5f);
      }
      g_mp_gate(plaits[0], clock_acc < kStep / 2);   // a gate held for half a step
      g_mp_render(plaits[0], buf[0], aux, n);
      for (uint32_t i = 0; i < n; ++i) mix_l[i] = mix_r[i] = 0.5f * buf[0][i];
      break;
    case K_RINGS:
      if (step) {
        g_mr_set(rings, s.a, s.b, note_of(next_rand()) - 12.0f, 0.2f + 0.6f * lfo, 0.6f, 0.7f, 0.3f);
        g_mr_strum(rings);
      }
      g_mr_render(rings, mix_l, mix_r, n);
      break;
    case K_CLOUDS:
      if (step) g_mc_set(clouds, s.a, 0, 0.2f + 0.6f * lfo, 0.5f, 0.0f, 0.7f, 0.5f, 0.7f, 0.5f, 0.3f, 0.3f, 0);
      g_mc_render(clouds, in_l, in_r, mix_l, mix_r, n);
      break;
    case K_WARPS:
      if (step) g_mw_set(warps, s.a, 0.2f + 0.6f * lfo, 0.8f, 0.8f);
      g_mw_render(warps, in_l, in_r, mix_l, mix_r, n);
      break;
    case K_TIDES:
      if (s.a == 0) {                   // audio rate: two four-voice oscillators
        for (int t = 0; t < 2; ++t) {
          if (step) g_mt_set(tides[t], 1, 3, 1, t ? 165.0f : 110.0f, 0.3f + 0.4f * lfo, 0.5f, 0.5f, 0.5f);
          g_mt_render(tides[t], tides_out, n, MI_SR);
          for (uint32_t i = 0; i < n; ++i) {
            float v = 0.25f * (tides_out[4 * i] + tides_out[4 * i + 1] + tides_out[4 * i + 2] + tides_out[4 * i + 3]);
            if (t) { mix_r[i] = 0.4f * v; } else { mix_l[i] = 0.4f * v; }
          }
        }
      } else {                          // as modulators: one value a block
        for (int t = 0; t < 2; ++t) {
          if (step) g_mt_set(tides[t], 1, 2, 0, t ? 0.7f : 0.25f, 0.5f, 0.5f, 0.5f, 0.5f);
          g_mt_render(tides[t], tides_out, 1, MI_SR / static_cast<float>(n));
          for (int c = 0; c < 4; ++c) mods[t][c] = tides_out[c];
        }
      }
      break;
    case K_MARBLES:
      g_mm_render(marbles, m_gates, m_cv, n);
      break;
    default:
      break;
  }
}

// core 0's part of a busy scene: Marbles at audio rate (its gates play the tracks), the Tides as modulators, the
// four Plaits voices
void render_tracks(uint32_t n) {
  if (!plaits[0]) {                     // the effects alone: their input is a plucked saw, every step
    if (clock_acc < n) {
      pluck_env = 0.8f;
      pluck_inc = hz_of(note_of(next_rand())) / MI_SR;
      strum_next = 1;
    }
    pluck(buf[1], 0, n);
    for (uint32_t i = 0; i < n; ++i) {
      buf[2][i] = buf[1][i] > 0.0f ? 0.4f : -0.4f;
      mix_l[i] = mix_r[i] = 0.0f;
    }
    return;
  }
  if (marbles) {
    g_mm_render(marbles, m_gates, m_cv, n);
  } else {                              // no Marbles in this build: the gates from the clock, notes at random
    for (uint32_t i = 0; i < n; ++i) {
      uint32_t at = clock_acc + i;      // (clock_acc counts this block already: an approximation is enough here)
      int on = (at % kStep) < kStep / 2;
      uint32_t st = steps + (at >= kStep ? 1u : 0u);
      m_gates[3 * i] = static_cast<uint8_t>(on && (st % 2u) == 0u);
      m_gates[3 * i + 1] = static_cast<uint8_t>(on && (st % 4u) == 0u);
      m_gates[3 * i + 2] = static_cast<uint8_t>(on && (st % 3u) == 1u);
      for (int c = 0; c < 4; ++c) m_cv[4 * i + c] = static_cast<float>((st * (5u + c)) % 7u) / 7.0f;
    }
  }
  int rise[3] = { 0, 0, 0 };
  float x[3] = { m_cv[0], m_cv[1], m_cv[2] };
  for (uint32_t i = 0; i < n; ++i) {
    for (int g = 0; g < 3; ++g) {
      uint8_t v = m_gates[3 * i + g];
      if (v && !last_gates[g]) { rise[g] = 1; x[g] = m_cv[4 * i + g]; }
      last_gates[g] = v;
    }
  }
  for (int t = 0; t < 2; ++t) {
    if (tides[t]) {
      g_mt_render(tides[t], tides_out, 1, MI_SR / static_cast<float>(n));
      for (int c = 0; c < 4; ++c) mods[t][c] = tides_out[c];
    } else {
      for (int c = 0; c < 4; ++c) mods[t][c] = lfo;
    }
  }
  static const uint8_t kGateOf[4] = { 1, 0, 2, 0 };   // kick on T2, the others on T1, T3, T1
  static const uint8_t kModels[4] = { 21, 8, 10, 20 };
  for (int v = 0; v < 4; ++v) {
    int g = kGateOf[v];
    if (rise[g]) {
      float note = v == 0 ? 36.0f : 45.0f + 12.0f * x[g];
      g_mp_set(plaits[v], kModels[v], note, 0.5f, 0.3f + 0.5f * mods[0][v], 0.3f + 0.5f * mods[1][v],
             v == 0 ? 0.6f : 0.4f, 0.5f);
    }
    g_mp_gate(plaits[v], last_gates[g]);   // Marbles' gate as it is at the block's end
    g_mp_render(plaits[v], buf[v], aux, n);
  }
  if (rise[2]) strum_next = 1;
  for (uint32_t i = 0; i < n; ++i) {
    float s = 0.25f * (buf[0][i] + buf[1][i] + buf[2][i] + buf[3][i]);
    mix_l[i] = s + 0.1f * (buf[1][i] - buf[2][i]);
    mix_r[i] = s - 0.1f * (buf[1][i] - buf[2][i]);
  }
}

volatile uint32_t warm;                 // blocks since the numbers were reset

void count(uint32_t c0, uint32_t c1, uint32_t tot, int on_core1) {
  if (warm < kWarmup) {
    ++warm;
    return;
  }
  st.blocks++;
  st.c0_sum += c0; if (c0 > st.c0_peak) st.c0_peak = c0;
  st.c1_sum += c1; if (c1 > st.c1_peak) st.c1_peak = c1;
  st.tot_sum += tot; if (tot > st.tot_peak) st.tot_peak = tot;
  if (on_core1) st.c1_jobs++;
}

}  // namespace

extern "C" {

int sc_count(void) { build_table(); return num_scenes; }
const char* sc_name(int i) { build_table(); return i >= 0 && i < num_scenes ? names[i] : ""; }
const char* sc_short(int i) { build_table(); return i >= 0 && i < num_scenes ? shorts[i] : ""; }
int sc_has_core1(int i) { build_table(); return i >= 0 && i < num_scenes && has_core1(scenes[i]); }
uint32_t sc_need(int i) { build_table(); return i >= 0 && i < num_scenes ? need_of(scenes[i]) : 0u; }
uint32_t sc_arena(void) { return MI_ARENA_BYTES; }
int sc_current(void) { return current; }
void sc_core1(int on) { use_core1 = on; }

int sc_start(int i) {
  build_table();
  ready = 0;                            // from here the render plays silence: it runs in an interrupt over this loop
  if (i < 0 || i >= num_scenes) return -1;
  current = i;
  cur = scenes[i];
  sc_stats_reset();
  if (need_of(cur) > MI_ARENA_BYTES) return -1;
  build(cur);
  clock_acc = kStep;                    // a first step at once
  steps = 0;
  lfo = 0.0f;
  pluck_env = 0.0f;
  pluck_phase = 0.0f;
  pluck_inc = 110.0f / MI_SR;
  osc_phase[0] = osc_phase[1] = 0.0f;
  c1_strum = strum_next = 0;
  last_gates[0] = last_gates[1] = last_gates[2] = 0;
  for (uint32_t k = 0; k < kMaxN; ++k) prev_mix[k] = prev_v1[k] = prev_v2[k] = 0.0f;
  ready = 1;
  return 0;
}

void sc_stats(sc_stats_t* s) { *s = st; }
void sc_stats_reset(void) {
  sc_stats_t z = {};
  st = z;
  warm = 0;
}

void sc_render(float* l, float* r, uint32_t n) {
  if (!ready || n > kMaxN) {
    for (uint32_t i = 0; i < n; ++i) l[i] = r[i] = 0.0f;
    return;
  }
  bool step = false;
  clock_acc += n;
  if (clock_acc >= kStep) {
    clock_acc -= kStep;
    step = true;
    ++steps;
  }
  lfo += static_cast<float>(n) / (8.0f * MI_SR);
  if (lfo >= 1.0f) lfo -= 1.0f;
  float tri = lfo < 0.5f ? 2.0f * lfo : 2.0f - 2.0f * lfo;
  float saved_lfo = lfo;
  lfo = tri;                            // the scenes sweep a triangle

  // inputs, before the clock starts: they are not the modules' work
  if (cur.kind == K_CLOUDS || cur.kind == K_WARPS) {
    if (step) {
      pluck_env = 0.8f;
      pluck_inc = hz_of(note_of(next_rand())) / MI_SR;
    }
    pluck(in_l, 0, n);
    for (uint32_t i = 0; i < n; ++i) {   // the second input: a square a fifth up
      osc_phase[1] += pluck_inc * 1.5f;
      if (osc_phase[1] >= 1.0f) osc_phase[1] -= 1.0f;
      in_r[i] = (osc_phase[1] < 0.5f ? 0.5f : -0.5f) * pluck_env;
    }
  }
  block_n = n;

  uint32_t t0 = plat_ticks24(), c0, c1 = 0, tot;
  int on_core1 = 0;
  if (cur.kind == K_COMBO) {
    bool core1 = has_core1(cur);
    if (core1 && use_core1 && plat_cpu2_on()) on_core1 = plat_cpu2_run(core1_job, 0);
    render_tracks(n);
    c0 = plat_ticks24() - t0;
    if (on_core1) {
      if (plat_cpu2_wait() != 0) {      // given up meanwhile: the job was dropped, do it here
        on_core1 = 0;
        core1_job(0);
      }
    } else if (core1) {
      core1_job(0);                     // one core: after core 0's part
    }
    tot = plat_ticks24() - t0;
    for (uint32_t i = 0; i < n; ++i) {   // the effects' inputs for the next block: the tracks before them
      prev_mix[i] = plaits[0] ? 0.5f * (mix_l[i] + mix_r[i]) : buf[1][i];
      prev_v1[i] = buf[1][i];
      prev_v2[i] = buf[2][i];
    }
    if (core1) {
      c1 = c1_ticks;
      for (uint32_t i = 0; i < n; ++i) { mix_l[i] += c1_l[i]; mix_r[i] += c1_r[i]; }
    }
    c1_strum = strum_next;              // what core 0 decided this block, for the second core's next one
    strum_next = 0;
  } else {
    render_single(n, step);
    c0 = tot = plat_ticks24() - t0;
  }
  count(c0, c1, tot, on_core1);
  lfo = saved_lfo;

  // what the listener hears of the scenes that make no sound of their own
  if (cur.kind == K_TIDES && cur.a == 1) {
    float f = hz_of(48.0f + 24.0f * mods[0][0]) / MI_SR;
    for (uint32_t i = 0; i < n; ++i) {
      osc_phase[0] += f;
      if (osc_phase[0] >= 1.0f) osc_phase[0] -= 1.0f;
      mix_l[i] = mix_r[i] = 0.2f * (osc_phase[0] < 0.5f ? 1.0f : -1.0f) * (0.5f + 0.5f * mods[1][0]);
    }
  } else if (cur.kind == K_MARBLES) {
    for (uint32_t i = 0; i < n; ++i) {
      if (m_gates[3 * i] && !last_gates[0]) {
        pluck_env = 0.6f;
        pluck_inc = hz_of(48.0f + 24.0f * m_cv[4 * i]) / MI_SR;
      }
      last_gates[0] = m_gates[3 * i];
      pluck_phase += pluck_inc;
      if (pluck_phase >= 1.0f) pluck_phase -= 1.0f;
      mix_l[i] = mix_r[i] = (2.0f * pluck_phase - 1.0f) * pluck_env * 0.5f;
      pluck_env *= 0.9997f;
    }
  }
  for (uint32_t i = 0; i < n; ++i) {
    l[i] = mix_l[i];
    r[i] = mix_r[i];
  }
}

uint32_t sc_size_plaits(void) { return g_mp_size(); }
uint32_t sc_size_rings(void) { return g_mr_size(); }
uint32_t sc_size_rings_reverb(void) { return g_mr_reverb_size(); }
uint32_t sc_size_clouds(void) { return MI_HAS_CLOUDS ? g_mc_size() + MC_LARGE_FULL + MC_SMALL_FULL : 0u; }
uint32_t sc_size_warps(void) { return g_mw_size(); }
uint32_t sc_size_tides(void) { return g_mt_size(); }
uint32_t sc_size_marbles(void) { return g_mm_size(); }

}  // extern "C"
