// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1 on a computer: the engine (mi/engine.cc) plays its default patch, as the FM-1 would at power-on, and is
// checked: every track sounds, the steps fall on the tempo, the sources stay in range, no NaN, the keyboard plays a
// stopped machine, a project's values are written as the screen writes them, the effects' lanes open and close them.
// WAVs in the folder given.
//   build/native build/wav [seconds]
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
extern "C" {
#include "plat.h"
}
#include "mi/engine.h"
#include "mi/bridge.h"
#include "mi/res_tables.h"
extern "C" int mi_res_check(const uint8_t* payload, uint32_t len);

extern "C" {
uint32_t plat_ticks24(void) {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<uint32_t>(static_cast<uint64_t>(t.tv_sec) * 24000000u + t.tv_nsec / 1000u * 24u);
}
int plat_cpu2_on(void) { return 0; }
int plat_cpu2_run(void (*)(uint32_t), uint32_t) { return 0; }
int plat_cpu2_wait(void) { return 0; }
}

static void put32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

struct Wav {
  FILE* f = nullptr;
  uint32_t frames = 0;
  bool open(const char* path, uint32_t total) {
    f = fopen(path, "wb");
    if (!f) return false;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + total * 4); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 44100); put32(f, 44100 * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, total * 4);
    return true;
  }
  void add(const float* l, const float* r, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
      put16(f, static_cast<uint16_t>(static_cast<int16_t>(std::fmax(-1.0f, std::fmin(1.0f, l[i])) * 32767.0f)));
      put16(f, static_cast<uint16_t>(static_cast<int16_t>(std::fmax(-1.0f, std::fmin(1.0f, r[i])) * 32767.0f)));
    }
  }
  void close() { if (f) fclose(f); }
};

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  float seconds = argc > 2 ? static_cast<float>(atof(argv[2])) : 16.0f;
  const uint32_t n = 128, blocks = static_cast<uint32_t>(seconds * 44100.0f / n);
  int failures = 0;
  {  // the resource region's file (firmware/res/mi_tables.bin, what the FM-1 reads) against the arrays it replaces
    FILE* rf = fopen("firmware/res/mi_tables.bin", "rb");
    static uint8_t region[MIFM1_RES_LEN + 1];
    size_t got = rf ? fread(region, 1, sizeof region, rf) : 0;
    if (rf) fclose(rf);
    int k = mi_res_check(region, static_cast<uint32_t>(got));
    printf("resource region: %u bytes, %d tables: %s\n", static_cast<unsigned>(got), MIFM1_RES_TABLES,
           k == -1 ? "the same bytes as the arrays" : "DIFFERENT (run tools/split_tables.py)");
    if (k != -1) ++failures;
  }
  if (eng_init() != 0) {
    printf("FAIL: the engine does not fit its memory\n");
    return 1;
  }
  printf("project: %u bytes\n", static_cast<unsigned>(sizeof(project_t)));

  // 1. the default patch, playing
  char path[512];
  snprintf(path, sizeof path, "%s/default.wav", dir);
  Wav w;
  if (!w.open(path, blocks * n)) { perror(path); return 1; }
  eng_play(1);
  float l[128], r[128];
  uint32_t rises[ENG_TRACKS] = {0}, prev = 0;
  double energy = 0.0;
  float peak = 0.0f, smin[NSRC], smax[NSRC];
  for (int s = 0; s < NSRC; ++s) smin[s] = 1e9f, smax[s] = -1e9f;
  int bad = 0, notes_lo = 999, notes_hi = -999;
  double t0 = static_cast<double>(clock());
  for (uint32_t b = 0; b < blocks; ++b) {
    eng_render(l, r, n);
    for (uint32_t i = 0; i < n; ++i) {
      if (!std::isfinite(l[i]) || !std::isfinite(r[i])) bad = 1;
      energy += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i];
      peak = std::fmax(peak, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
    }
    uint32_t g = eng_gates();
    for (int t = 0; t < ENG_TRACKS; ++t) {
      if ((g & (1u << t)) && !(prev & (1u << t))) {
        ++rises[t];
        int note = eng_note(t);
        if (note < notes_lo) notes_lo = note;
        if (note > notes_hi) notes_hi = note;
      }
    }
    prev = g;
    for (int s = 1; s < NSRC; ++s) {
      smin[s] = std::fmin(smin[s], eng_src(s));
      smax[s] = std::fmax(smax[s], eng_src(s));
    }
    w.add(l, r, n);
  }
  w.close();
  double us = (static_cast<double>(clock()) - t0) / CLOCKS_PER_SEC * 1e6 / blocks;
  double rms = std::sqrt(energy / (2.0 * blocks * n));
  printf("default patch, %.0f s: rms %.3f, peak %.3f, %.1f us a block on this computer, notes %d..%d%s\n", seconds,
         rms, peak, us, notes_lo, notes_hi, bad ? ", NaN" : "");
  printf("gates per track:");
  for (int t = 0; t < ENG_TRACKS; ++t) printf(" %u", rises[t]);
  printf("  (track 1: %0.f at 120 BPM expected, a beat each and a ratchet a bar)\n", seconds * 2.5f);
  if (bad || rms < 0.01) ++failures;
  if (rises[0] < seconds * 2.5f * 0.95f || rises[0] > seconds * 2.5f * 1.05f + 1) {
    printf("FAIL: track 1 does not play its steps at 120 BPM\n");
    ++failures;
  }
  for (int t = 1; t < ENG_TRACKS; ++t) {
    if (!rises[t]) {
      printf("FAIL: track %d never plays\n", t + 1);
      ++failures;
    }
  }
  printf("sources (min..max):");
  for (int s = 1; s < NSRC; ++s) printf(" %s %.2f..%.2f", eng_src_name(s), smin[s], smax[s]);
  printf("\n");
  for (int s = SRC_A1; s <= SRC_B4; ++s) {
    if (smax[s] - smin[s] < 0.05f) {
      printf("FAIL: %s does not move\n", eng_src_name(s));
      ++failures;
    }
  }

  // 2. stopped: the keyboard plays track 2
  eng_play(0);
  for (int b = 0; b < 200; ++b) eng_render(l, r, n);   // the tails die away
  eng_key(1, 60, 1);
  double ke = 0.0;
  for (int b = 0; b < 100; ++b) {
    eng_render(l, r, n);
    for (uint32_t i = 0; i < n; ++i) ke += static_cast<double>(l[i]) * l[i];
  }
  eng_key(1, 60, 0);
  int kn = eng_note(1);
  printf("keyboard, stopped: rms %.3f, note %d\n", std::sqrt(ke / (100.0 * n)), kn);
  if (ke < 1e-3 || kn != 60) {
    printf("FAIL: the keyboard does not play track 2\n");
    ++failures;
  }

  // 3. every model on track 1, a second each
  project_t* p = eng_project();
  for (int m = 0; m < eng_algo_count(PG_TRACK(0, 0)); ++m) {
    p->algo[PG_TRACK(0, 0)] = static_cast<uint8_t>(m);
    eng_key(0, 48, 1);
    double e = 0.0;
    int nan = 0;
    for (int b = 0; b < 344; ++b) {
      eng_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) {
        if (!std::isfinite(l[i])) nan = 1;
        e += static_cast<double>(l[i]) * l[i];
      }
      if (b == 200) eng_key(0, 48, 0);
    }
    if (nan || e < 1e-4) {
      printf("FAIL: model %s: %s\n", eng_algo_name(PG_TRACK(0, 0), m), nan ? "NaN" : "silent");
      ++failures;
    }
  }
  printf("track 1, the %d models: played\n", eng_algo_count(PG_TRACK(0, 0)));

  // 4. the values as the screen writes them
  char t[24];
  eng_param_text(PG_GLOBAL * 4 + 0, p->val[PG_GLOBAL * 4 + 0], t, sizeof t);
  printf("TEMPO %s", t);
  if (strcmp(t, "120 BPM")) ++failures, printf(" (FAIL: 120 BPM expected)");
  eng_param_text(PG_TRACK(0, 0) * 4 + 0, 0.5f, t, sizeof t);
  printf(", FREQUENCY +0.5 -> %s", t);
  if (strcmp(t, "+12.0")) ++failures, printf(" (FAIL: +12.0 expected)");
  eng_param_text(PG_TIDES(1, 0) * 4 + 0, p->val[PG_TIDES(1, 0) * 4 + 0], t, sizeof t);
  printf(", TIDES B %s", t);
  eng_param_text(PG_TIDES(0, 0) * 4 + 0, 0.0f, t, sizeof t);
  printf(", TIDES A at 0: %s\n", t);
  if (strcmp(t, "0.02 Hz")) ++failures, printf(" (FAIL: 0.02 Hz expected)\n");
  {
    static const struct { int param; float v; const char* want; } kText[] = {
        {PG_DELAY(0) * 4 + 0, 7.0f, "1/8D"},    {PG_DELAY(0) * 4 + 2, -0.2f, "LP 20"}, {PG_DELAY(0) * 4 + 2, 0.0f, "Flat"},
        {PG_DELAY(1) * 4 + 1, 1.0f, "Ping-pong"}, {PG_GLOBAL * 4 + 3, 1.0f, "75%"},  {PG_GLOBAL * 4 + 3, 0.0f, "50%"},
        {PG_STEP(LANE_DELAY) * 4 + 0, 0.0f, "10 ms"}, {PG_STEP(LANE_REVERB) * 4 + 0, 1.0f, "5.1 s"},
        {PG_TRACK(0, 2) * 4 + 1, 1.0f, "95%"},   {PG_TIDES(0, 1) * 4 + 3, 4.0f, "T4"}};
    for (const auto& k : kText) {
      eng_param_text(k.param, k.v, t, sizeof t);
      if (strcmp(t, k.want)) ++failures, printf("FAIL: %s at %.2f written %s, %s expected\n", eng_pdef(k.param)->name, k.v, t, k.want);
    }
    printf("the delay's, the swing's, the lanes' values: written as expected\n");
  }

  // 5. INIT: four Plaits on VIRTUAL ANALOG, nothing patched, no steps; a step on each, each track plays
  eng_blank(p);
  int cables = 0, models = 0, steps = 0;
  for (int l = 0; l < ENG_LANES; ++l) steps += eng_lane_has_trigs(l);
  for (int tr = 0; tr < ENG_TRACKS; ++tr) eng_trig_set(tr, tr * 4, 1);
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) cables += p->cable[g][c].depth != 0;
  for (int tr = 0; tr < ENG_TRACKS; ++tr) models += p->algo[PG_TRACK(tr, 0)] == 8;
  snprintf(path, sizeof path, "%s/init.wav", dir);
  Wav wi;
  const uint32_t iblocks = 8u * 44100u / n;
  if (!wi.open(path, iblocks * n)) { perror(path); return 1; }
  eng_play(0);
  eng_play(1);
  uint32_t irises[ENG_TRACKS] = {0};
  prev = 0;
  double ie = 0.0;
  int inan = 0;
  for (uint32_t b = 0; b < iblocks; ++b) {
    eng_render(l, r, n);
    for (uint32_t i = 0; i < n; ++i) {
      if (!std::isfinite(l[i])) inan = 1;
      ie += static_cast<double>(l[i]) * l[i];
    }
    uint32_t g = eng_gates();
    for (int tr = 0; tr < ENG_TRACKS; ++tr)
      if ((g & (1u << tr)) && !(prev & (1u << tr))) ++irises[tr];
    prev = g;
    wi.add(l, r, n);
  }
  wi.close();
  printf("init: %d cables, %d lanes with steps, %d tracks on VIRTUAL ANALOG (%s), a step each: rms %.3f, gates %u %u %u %u\n",
         cables, steps, models, eng_algo_name(PG_TRACK(0, 0), 8), std::sqrt(ie / (iblocks * n)), irises[0], irises[1],
         irises[2], irises[3]);
  if (cables || steps || models != ENG_TRACKS || inan || ie < 1e-3) ++failures, printf("FAIL: the INIT project\n");
  for (int tr = 0; tr < ENG_TRACKS; ++tr)
    if (!irises[tr]) ++failures, printf("FAIL: INIT, track %d never plays\n", tr + 1);

  // 6. the step sequencer: the demo's patterns at 120 BPM (a sixteenth 125 ms), 8 s = 4 bars
  eng_play(0);
  eng_defaults(p);
  auto run = [&](int seconds, uint32_t* rises, int* lo, int* hi, uint16_t* inscale) {
    uint32_t pv = 0, bl = static_cast<uint32_t>(seconds) * 44100u / n;
    for (int tr = 0; tr < ENG_TRACKS; ++tr) rises[tr] = 0, lo[tr] = 999, hi[tr] = -999;
    *inscale = 1;
    for (int b = 0; b < 400; ++b) eng_render(l, r, n);   // stopped: the tails die away
    eng_play(1);
    for (uint32_t b = 0; b < bl; ++b) {
      eng_render(l, r, n);
      uint32_t g = eng_gates();
      for (int tr = 0; tr < ENG_TRACKS; ++tr)
        if ((g & (1u << tr)) && !(pv & (1u << tr))) {
          ++rises[tr];
          int nt = eng_note(tr);
          if (nt < lo[tr]) lo[tr] = nt;
          if (nt > hi[tr]) hi[tr] = nt;
          if (eng_quantize(nt) != nt) *inscale = 0;
        }
      pv = g;
    }
    eng_play(0);
  };
  uint32_t sr[ENG_TRACKS];
  int slo[ENG_TRACKS], shi[ENG_TRACKS];
  uint16_t ok_scale;
  run(8, sr, slo, shi, &ok_scale);
  printf("steps, 4 bars: trigs %u %u %u %u (20 20 12 24 expected: track 1 has a ratchet x2 a bar), track 2 notes %d..%d\n",
         sr[0], sr[1], sr[2], sr[3], slo[1], shi[1]);
  static const uint32_t kExpect[4] = {20, 20, 12, 24};
  for (int tr = 0; tr < ENG_TRACKS; ++tr)
    if (sr[tr] + 1 < kExpect[tr] || sr[tr] > kExpect[tr] + 1) ++failures, printf("FAIL: steps, track %d\n", tr + 1);
  if (shi[1] - slo[1] < 12) ++failures, printf("FAIL: track 2's lock (an octave up on step 15) not played\n");

  // 7. probability, ratchets, a random tune kept in the scale
  p->val[PG_STEP(2) * 4 + 1] = 0.0f;                   // track 3 never
  p->val[PG_STEP(0) * 4 + 2] = 3.0f;                   // track 1: three a trig (its locked step: two)
  p->rnd[3][TP_NOTE] = 100;                             // track 4: any note an octave around, new at each trig
  p->val[PG_GLOBAL * 4 + 1] = 11.0f;                    // minor pentatonic
  run(8, sr, slo, shi, &ok_scale);
  printf("prob 0 on track 3: %u trigs; ratchets x3 on track 1: %u (44 expected); random tune on track 4: %d..%d, %s\n",
         sr[2], sr[0], slo[3], shi[3], ok_scale ? "all in the scale" : "OUT OF THE SCALE");
  if (sr[2]) ++failures, printf("FAIL: probability 0 played\n");
  if (sr[0] + 2 < 44 || sr[0] > 45) ++failures, printf("FAIL: ratchets\n");
  if (!ok_scale || shi[3] - slo[3] < 5) ++failures, printf("FAIL: the random tune or the quantizer\n");
  p->rnd[3][TP_NOTE] = -100;                            // looped: the same notes each bar
  run(4, sr, slo, shi, &ok_scale);
  int a0 = slo[3], a1 = shi[3];
  run(4, sr, slo, shi, &ok_scale);
  printf("looped random tune: %d..%d then %d..%d\n", a0, a1, slo[3], shi[3]);
  if (a0 != slo[3] || a1 != shi[3]) ++failures, printf("FAIL: the looped random is not a loop\n");

  // 8. the keyboard: a key pressed while another is held is a new trig
  eng_defaults(p);
  for (int b = 0; b < 400; ++b) eng_render(l, r, n);
  {
    uint32_t r0 = eng_rises(1), kr;
    for (int b = 0; b < 300; ++b) {
      if (b == 10) eng_key(1, 60, 1);
      if (b == 100) eng_key(1, 62, 1);                  // legato
      if (b == 101) eng_key(1, 60, 0);
      if (b == 200) eng_key(1, 62, 0);
      if (b == 250) {                                   // a tap shorter than a block
        eng_key(1, 64, 1);
        eng_key(1, 64, 0);
      }
      eng_render(l, r, n);
    }
    kr = eng_rises(1) - r0;
    printf("keyboard: legato then a short tap: %u trigs (3 expected)\n", kr);
    if (kr != 3) ++failures, printf("FAIL: legato keys\n");
  }

  // 10. Endless: track 1 records C3, a rest, D#3, G3, then loops them on sixteenths; a key transposes it
  eng_defaults(p);
  eng_endless_rec(0, 1);
  eng_endless_add(0, 60);
  eng_endless_add(0, ENDLESS_REST);
  eng_endless_add(0, 63);
  eng_endless_add(0, 67);
  eng_endless_rec(0, 0);
  run(4, sr, slo, shi, &ok_scale);
  int e1lo = slo[0], e1hi = shi[0];
  uint32_t e1 = sr[0];
  eng_endless_key(0, 65);                                // up a fourth
  run(4, sr, slo, shi, &ok_scale);
  printf("endless: %u notes in 2 bars (24 expected), %d..%d; transposed by a key: %d..%d, %s\n", e1, e1lo, e1hi, slo[0],
         shi[0], ok_scale ? "in the scale" : "OUT OF THE SCALE");
  if (e1 + 1 < 24 || e1 > 25 || e1lo != 60 || e1hi != 67 || slo[0] != 65 || shi[0] != 72 || !ok_scale)
    ++failures, printf("FAIL: Endless\n");
  eng_endless_clear(0);

  // 11. no clicks: a smooth voice (HARMONIC, long decay) on every sixteenth, its pan, level and tune random at each
  // trig. A click is a sample whose second difference stands far above its neighbours' (a gain or a pitch that jumps
  // at a block's edge); 0.1.8's mixer made them by the thousand, ~5000 times its neighbours
  {
    eng_play(0);
    eng_blank(p);
    p->algo[PG_TRACK(0, 0)] = 12;
    p->val[eng_track_param(0, TP_DECAY)] = 0.75f;
    p->val[eng_track_param(0, TP_SEND)] = 0.0f;
    for (int s = 0; s < 16; ++s) eng_trig_set(0, s, 1);
    p->rnd[0][TP_PAN] = 100;
    p->rnd[0][TP_NOTE] = 100;
    p->rnd[0][TP_RATCHET] = 100;
    eng_lock_set(0, 4, TP_LEVEL, 0.5f);                   // a lock on the level, step 5: half
    for (int b = 0; b < 400; ++b) eng_render(l, r, n);
    eng_play(1);
    static float d2[2][44100 * 4];
    float pl = 0, pr = 0, ppl = 0, ppr = 0;
    const int nb = 44100 * 4 / static_cast<int>(n);
    for (int b = 0; b < nb; ++b) {
      eng_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) {
        d2[0][b * n + i] = l[i] - 2.0f * pl + ppl;
        d2[1][b * n + i] = r[i] - 2.0f * pr + ppr;
        ppl = pl, pl = l[i], ppr = pr, pr = r[i];
      }
    }
    eng_play(0);
    double worst = 0;
    for (int c = 0; c < 2; ++c) {
      double acc = 0;
      const int W = 88;                                   // 2 ms around
      for (int i = 2; i < nb * static_cast<int>(n); ++i) {
        acc += std::fabs(d2[c][i]);
        if (i >= W + 2) acc -= std::fabs(d2[c][i - W]);
        if (i < W + 3) continue;
        double q = std::fabs(d2[c][i]) / ((acc - std::fabs(d2[c][i])) / (W - 1) + 1e-5);
        if (std::fabs(d2[c][i]) > 0.003 && q > worst) worst = q;
      }
    }
    printf("random pan, tune and ratchets, a level lock: the worst click %.0f times its neighbours (under 200: none)\n",
           worst);
    if (worst > 200.0) ++failures, printf("FAIL: clicks\n");
    eng_defaults(p);
  }

  // 12. Warps: the demo with track 3 (FM 2-op) as the carrier and track 2 (the bass) as the modulator, through every
  // mark of ALGORITHM (its six cross-modulations) and the frequency shifter; its output on track 3, finite and heard;
  // its cost against the four voices'. Then its oscillator alone (the tracks silent): A4 on LEVEL 1, 440 Hz out
  {
    eng_play(0);
    eng_defaults(p);
    const int nb = 44100 * 2 / static_cast<int>(n);
    auto play = [&](double* rms, double* us) {
      double e = 0;
      int nan = 0;
      for (int b = 0; b < 200; ++b) eng_render(l, r, n);
      double t0 = static_cast<double>(clock());
      for (int b = 0; b < nb; ++b) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          if (!std::isfinite(l[i]) || !std::isfinite(r[i])) nan = 1;
          e += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i];
        }
      }
      *us = (static_cast<double>(clock()) - t0) / CLOCKS_PER_SEC * 1e6 / nb;
      *rms = nan ? -1.0 : std::sqrt(e / (2.0 * nb * n));
    };
    double rms0, us0, rms, us, worst = 0;
    eng_play(1);
    play(&rms0, &us0);
    p->val[PG_WARPS(1) * 4 + 0] = WC_T3;
    p->val[PG_WARPS(1) * 4 + 1] = 1.0f;                  // track 2
    static const float kMarks[6] = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
    printf("warps (T3 by T2), rms and the render's time, us a block (off: %.3f, %.0f):", rms0, us0);
    for (int a = 0; a < 7; ++a) {
      p->val[PG_WARPS(0) * 4 + 0] = a < 6 ? kMarks[a] : 0.7f;
      p->val[PG_WARPS(1) * 4 + 3] = a == 6 ? 1.0f : 0.0f;
      if (a == 6) p->val[PG_WARPS(1) * 4 + 0] = WC_OSC1;   // the shifter on its own oscillator
      play(&rms, &us);
      int alg = eng_warps_algo();
      printf(" %s %.3f %.0f%s", eng_warps_name(alg), rms, us, a == 6 ? "\n" : ",");
      if (us - us0 > worst) worst = us - us0;
      if (rms <= 0.002 || rms > 1.0 || eng_warps_track() != (a == 6 ? 1 : 2) || alg != a)
        ++failures, printf("FAIL: Warps at %d (%s): rms %.3f, on track %d\n", a, eng_warps_name(alg), rms, eng_warps_track() + 1);
    }
    printf("warps: at most %.0f us a block more than without, the four voices and the rest %.0f\n", worst, us0);
    eng_play(0);
    eng_blank(p);                                         // nothing plays: the oscillator alone, through XFADE
    p->val[PG_WARPS(1) * 4 + 0] = WC_OSC1;
    p->val[PG_WARPS(1) * 4 + 1] = 0.0f;
    p->val[PG_WARPS(0) * 4 + 0] = 0.0f;
    p->val[PG_WARPS(0) * 4 + 2] = 57.0f / 60.0f;          // LEVEL 1: A4
    p->val[PG_MIX * 4 + 0] = 1.0f;
    double best = 0;
    for (int side = 0; side < 2; ++side) {                // TIMBRE at either end: the carrier alone, one way
      p->val[PG_WARPS(0) * 4 + 1] = static_cast<float>(side);
      for (int b = 0; b < 400; ++b) eng_render(l, r, n);
      int cross = 0;
      float prev = 0;
      double e = 0;
      for (int b = 0; b < 44100 / static_cast<int>(n); ++b) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          if ((l[i] >= 0) != (prev >= 0)) ++cross;
          prev = l[i];
          e += static_cast<double>(l[i]) * l[i];
        }
      }
      double hz = cross / 2.0 / (44100.0 / n * n / 44100.0 * (44100 / n) / (44100.0 / n));
      if (e > best) best = e, hz = cross / 2.0 / ((44100 / n) * n / 44100.0);
      if (side == 1 || e >= best) {
        printf("warps' oscillator, TIMBRE %d: %.1f Hz, rms %.3f\n", side, cross / 2.0 / ((44100 / n) * n / 44100.0),
               std::sqrt(e / ((44100 / n) * n)));
      }
      (void)hz;
    }
    eng_defaults(p);
  }

  // 14. Warps' frequency shifter (at 44.1 kHz, its allpass pairs made for that rate): 1 kHz in, shifted 100 Hz; one
  // side kept, the other under it by 30 dB or more (TIMBRE at either end)
  {
    static uint8_t wm[96 * 1024] __attribute__((aligned(8)));
    if (mw_size() > sizeof wm) {
      ++failures, printf("FAIL: Warps needs %u bytes\n", mw_size());
    } else {
      float pot = 0.5f + 0.5f * (0.4f + std::log2(100.0f / 4.0f) / 15.0f);   // modulator.cc: +100 Hz
      double side[2][2];
      for (int t = 0; t < 2; ++t) {
        mw_init(wm);
        static float c[44100], m[44100], o[44100];
        for (int i = 0; i < 44100; ++i) c[i] = 0.0f, m[i] = 0.4f * std::sin(2.0 * M_PI * 1000.0 * i / 44100.0);
        for (int b = 0; b < 44100 / 128; ++b) {
          mw_set(wm, pot, static_cast<float>(t), 0.0f, 1.0f, 1, 48.0f, 1);
          static float dry[128];
          mw_render(wm, c + b * 128, m + b * 128, m + b * 128, o + b * 128, dry, 128);
        }
        for (int k = 0; k < 2; ++k) {
          double f = k ? 1100.0 : 900.0, re = 0, im = 0;
          for (int i = 11025; i < 44100 / 128 * 128; ++i)
            re += o[i] * std::cos(2.0 * M_PI * f * i / 44100.0), im += o[i] * std::sin(2.0 * M_PI * f * i / 44100.0);
          side[t][k] = 10.0 * std::log10(re * re + im * im + 1e-20);
        }
      }
      printf("warps' frequency shifter, 1 kHz up 100 Hz: TIMBRE 0: 1100 Hz %+.0f dB over 900 Hz; TIMBRE 1: %+.0f dB\n",
             side[0][1] - side[0][0], side[1][1] - side[1][0]);
      double a = side[0][1] - side[0][0], b = side[1][1] - side[1][0];
      if (!((a > 30 && b < -30) || (a < -30 && b > 30))) ++failures, printf("FAIL: the frequency shifter\n");
    }
  }

  // 25. Warps' ECO (its cross-modulations at 3x instead of 6x): each algorithm, a 220 Hz carrier by a 330 Hz
  // modulator, plays as at 6x (what differs, the samples a little apart in time and what folds back from over 66 kHz,
  // under it: -35 dB or so, XOR's bits the most, -18), in less time
  {
    static uint8_t wm[96 * 1024] __attribute__((aligned(8)));
    static float c[44100], m[44100], o[2][44100], dry[128];
    for (int i = 0; i < 44100; ++i)
      c[i] = 0.4f * std::sin(2.0 * M_PI * 220.0 * i / 44100.0), m[i] = 0.4f * std::sin(2.0 * M_PI * 330.0 * i / 44100.0);
    const int nb = 44100 / 128;
    double us[2] = {0, 0}, worst = -200;
    printf("warps ECO against 6x, the difference (dB):");
    for (int a = 0; a < 6; ++a) {
      for (int eco = 0; eco < 2; ++eco) {
        mw_init(wm);
        mw_eco(wm, eco);
        double t0 = static_cast<double>(clock());
        for (int b = 0; b < nb; ++b) {
          mw_set(wm, a / 8.0f, 0.5f, 0.6f, 0.6f, 0, 48.0f, 0);
          mw_render(wm, c + b * 128, m + b * 128, m + b * 128, o[eco] + b * 128, dry, 128);
        }
        us[eco] += (static_cast<double>(clock()) - t0) / CLOCKS_PER_SEC * 1e6 / nb;
      }
      double best = 1e30, ref = 0;
      for (int i = 4410; i < nb * 128 - 8; ++i) ref += static_cast<double>(o[0][i]) * o[0][i];
      for (int lag = -3; lag <= 3; ++lag) {                // (the two pairs of filters delay it a little differently)
        double e = 0;
        for (int i = 4410; i < nb * 128 - 8; ++i) {
          double d = static_cast<double>(o[1][i + lag]) - o[0][i];
          e += d * d;
        }
        if (e < best) best = e;
      }
      double db = 10.0 * std::log10(best / (ref + 1e-20) + 1e-20);
      printf(" %s %.0f", eng_warps_name(a), db);
      if (db > worst) worst = db;
      if (ref <= 1e-6 || db > -15.0) ++failures, printf(" FAIL");
    }
    printf("; its time %.1f us a block, 6x %.1f\n", us[1] / 6, us[0] / 6);
  }

  // 13. 0.1.9's project (version 4, 2644 bytes) read into this version's: all of it, Warps off
  {
    static uint8_t buf[sizeof(project_t)];
    project_t* q = reinterpret_cast<project_t*>(buf);
    for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = 0;
    q->magic = PROJECT_MAGIC;
    q->version = 4;
    q->size = 2644;
    float* val = reinterpret_cast<float*>(buf + 8);
    for (int i = 0; i < 132; ++i) val[i] = 0.25f;
    buf[8 + 528 + PG_TRACK(1, 0)] = 17;                   // track 2 on NOISE
    buf[8 + 528 + 33] = 2;                                // the output: mono
    cable_t* cab = reinterpret_cast<cable_t*>(buf + 572);
    cab[0].src = SRC_A2;
    cab[0].dst = 3;
    cab[0].depth = 33;
    buf[2380 + 0] = 60;                                   // Endless, track 1: three notes
    buf[2636 + 0] = 3;
    int ok = eng_upgrade(q, 2644);
    printf("a version 4 project: %s, track 2 model %d, output %d, a cable %s, knob %.2f, Endless %d notes, warps %s\n",
           ok ? "read" : "REFUSED", q->algo[PG_TRACK(1, 0)], q->out_mode, q->cable[0][0].depth == 33 ? "kept" : "LOST",
           q->val[5], q->endless_len[0], q->val[PG_WARPS(1) * 4] == WC_OFF ? "off" : "ON");
    if (!ok || !eng_check(q) || q->algo[PG_TRACK(1, 0)] != 17 || q->out_mode != 2 || q->cable[0][0].depth != 33 ||
        q->val[5] != 0.25f || q->endless_len[0] != 3 || q->endless[0][0] != 60 || q->val[PG_WARPS(1) * 4] != WC_OFF)
      ++failures, printf("FAIL: the version 4 project\n");
  }

  // 15. clearing a track's pattern: its trigs and locks gone, the other tracks' kept
  {
    eng_defaults(p);
    int before = 0, after = 0, other = 0, locks = 0;
    for (int s = 0; s < 16; ++s) before += eng_trig(1, s), other += eng_trig(0, s);
    eng_steps_clear(1);
    for (int s = 0; s < 16; ++s) after += eng_trig(1, s), other -= eng_trig(0, s), locks += eng_step_locked(1, s);
    printf("clear track 2: %d trigs, then %d (%d locks); track 1 %s\n", before, after, locks, other ? "CHANGED" : "kept");
    if (!before || after || locks || other) ++failures, printf("FAIL: clearing a track\n");
    eng_defaults(p);
  }

  // 16. ALL undone in time: asked 200 blocks into PLAY (120 BPM, a bar 88200 samples, 689 blocks), the knobs come
  // back as the second bar starts, 489 blocks later; stopped, at once
  {
    eng_play(0);
    eng_defaults(p);
    static float was[ENG_NPARAMS];
    static uint8_t was_algo[ENG_NPAGES];
    static int8_t was_rnd[ENG_TRACKS * ENG_NTP];
    for (int i = 0; i < ENG_NPARAMS; ++i) was[i] = p->val[i];
    for (int i = 0; i < ENG_NPAGES; ++i) was_algo[i] = p->algo[i];
    for (int i = 0; i < ENG_TRACKS * ENG_NTP; ++i) was_rnd[i] = p->rnd[i / ENG_NTP][i % ENG_NTP];
    eng_play(1);
    for (int b = 0; b < 200; ++b) eng_render(l, r, n);
    for (int t = 0; t < ENG_TRACKS; ++t) p->val[eng_track_param(t, TP_DECAY)] = 0.05f;   // an ALL
    eng_back_at_bar(was, was_algo, was_rnd);
    int at = -1;
    for (int b = 0; b < 1000 && at < 0; ++b) {
      eng_render(l, r, n);
      if (!eng_back_pending()) at = b;
    }
    int ok = at >= 480 && at <= 495 && p->val[eng_track_param(2, TP_DECAY)] == was[eng_track_param(2, TP_DECAY)];
    eng_play(0);
    p->val[eng_track_param(0, TP_DECAY)] = 0.05f;
    eng_back_at_bar(was, was_algo, was_rnd);
    eng_render(l, r, n);
    int stopped = !eng_back_pending() && p->val[eng_track_param(0, TP_DECAY)] == was[eng_track_param(0, TP_DECAY)];
    printf("ALL undone at the bar: %d blocks after it was asked (489 expected); stopped: %s\n", at,
           stopped ? "at once" : "NOT DONE");
    if (!ok || !stopped) ++failures, printf("FAIL: ALL's undo\n");
    eng_defaults(p);
  }

  // 9. v0.1's project (version 2) is not read any more (0.3 reads 0.1.4's and later)
  {
    static uint8_t buf[sizeof(project_t)];
    project_t* q = reinterpret_cast<project_t*>(buf);
    for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = 0;
    q->magic = PROJECT_MAGIC;
    q->version = 2;
    q->size = 564;
    if (eng_upgrade(q, 564)) ++failures, printf("FAIL: a version 2 project read\n");
  }

  // 17. 0.2's project (version 5, 2680 bytes): Marbles' RATE the tempo, the pages moved (the mixer, the reverb, the
  // steps, the scale, Warps), a lock and a cable kept, a cable from Marbles dropped
  {
    static uint8_t buf[sizeof(project_t)];
    project_t* q = reinterpret_cast<project_t*>(buf);
    for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = 0;
    q->magic = PROJECT_MAGIC;
    q->version = 5;
    q->size = 2680;
    float* val = reinterpret_cast<float*>(buf + 8);
    val[16 * 4 + 0] = 0.7f;                               // Marbles' RATE
    val[20 * 4 + 2] = 0.33f;                              // the mixer: track 3's level
    val[22 * 4 + 0] = 0.8f;                               // the reverb's time
    val[24 * 4 + 3] = 12.0f;                              // track 1's steps: 12
    val[28 * 4 + 2] = 3.0f;                               // the scale: Dorian
    val[34 * 4 + 0] = 2.0f;                               // Warps: T2 the carrier
    val[13 * 4 + 3] = 4.0f;                               // Tides A's trigger: the clock
    uint8_t* algo = buf + 8 + 140 * 4;
    algo[PG_TRACK(2, 0)] = 19;                            // track 3 on STRING
    cable_t* cab = reinterpret_cast<cable_t*>(buf + 8 + 560 + 35 + 3);
    cab[0] = {SRC_B1, 20 * 4 + 1, 40, 0};                 // B1 -> track 2's level
    cab[1] = {12, 2, 50, 0};                              // Marbles' Y -> track 1's TIMBRE
    uint8_t* trig = buf + 8 + 560 + 35 + 3 + 160;
    trig[1 * 8 + 0] = 0x11;                               // track 2: steps 1 and 5
    uint8_t* lock = trig + 32 + 72 + 8;
    lock[0] = 1 << 6 | 4;                                 // track 2, step 5: its note
    lock[1] = TP_NOTE;
    lock[2] = 200;
    for (int i = 1; i < ENG_PLOCKS; ++i) lock[i * 4 + 1] = 0xFF;
    int ok = eng_upgrade(q, 2680);
    char tx[24];
    eng_param_text(PG_GLOBAL * 4, q->val[PG_GLOBAL * 4], tx, sizeof tx);
    float lv = 0;
    int haslock = 0;
    for (int i = 0; i < ENG_PLOCKS; ++i)
      if (q->lock[i].lane == 1 && q->lock[i].step == 4 && q->lock[i].tp == TP_NOTE) haslock = q->lock[i].v;
    lv = q->val[PG_MIX * 4 + 2];
    printf("a version 5 project: %s, tempo %s, level %.2f, reverb %.2f, length %.0f, scale %.0f, Warps' carrier %.0f, "
           "Tides A %.0f, model %d, cables %d/%d, trig %d, lock %d\n",
           ok ? "read" : "REFUSED", tx, lv, q->val[PG_REVERB * 4], q->val[PG_STEP(0) * 4 + 3], q->val[PG_GLOBAL * 4 + 1],
           q->val[PG_WARPS(1) * 4], q->val[PG_TIDES(0, 1) * 4 + 3], q->algo[PG_TRACK(2, 0)], q->cable[0][0].depth,
           q->cable[0][1].depth, (q->trig[1][0] >> 4) & 1, haslock);
    if (!ok || !eng_check(q) || q->val[PG_GLOBAL * 4] != 0.7f || lv != 0.33f || q->val[PG_REVERB * 4] != 0.8f ||
        q->val[PG_STEP(0) * 4 + 3] != 12.0f || q->val[PG_GLOBAL * 4 + 1] != 3.0f || q->val[PG_WARPS(1) * 4] != 2.0f ||
        q->val[PG_TIDES(0, 1) * 4 + 3] != 5.0f || q->algo[PG_TRACK(2, 0)] != 19 || q->cable[0][0].depth != 40 ||
        q->cable[0][0].dst != PG_MIX * 4 + 1 || q->cable[0][1].depth != 0 || haslock != 200)
      ++failures, printf("FAIL: the version 5 project\n");
  }

  // 18. swing: every step of track 1 played, SWING at 75 %: the off-beats late, a sixteenth and a half then half of
  // one apart (at 120 BPM 8269 and 2756 samples, ~65 and ~22 blocks)
  {
    eng_play(0);
    eng_blank(p);
    for (int s = 0; s < 16; ++s) eng_trig_set(0, s, 1);
    p->val[PG_GLOBAL * 4 + 3] = 1.0f;
    for (int b = 0; b < 400; ++b) eng_render(l, r, n);
    uint32_t r0 = eng_rises(0);
    int at[12], k = 0;
    eng_play(1);
    for (int b = 0; b < 2000 && k < 12; ++b) {
      eng_render(l, r, n);
      if (eng_rises(0) != r0) r0 = eng_rises(0), at[k++] = b;
    }
    eng_play(0);
    int lo = 9999, hi = 0;
    for (int i = 2; i + 1 < k; i += 2) {
      int a = at[i + 1] - at[i], b = i + 2 < k ? at[i + 2] - at[i + 1] : -1;
      if (a > hi) hi = a;
      if (b >= 0 && b < lo) lo = b;
      if (b >= 0 && (a < 60 || a > 70 || b < 18 || b > 26)) lo = -1;
    }
    printf("swing 75 %%: the sixteenths %d and %d blocks apart (65 and 22 expected)\n", hi, lo);
    if (k < 12 || lo < 0) ++failures, printf("FAIL: swing\n");
  }

  // 19. a step's LENGTH: a sustained voice (VIRTUAL ANALOG), its gate 5 % then 95 % of the sixteenth (6 and 119 ms)
  {
    eng_blank(p);
    for (int s = 0; s < 16; s += 4) eng_trig_set(0, s, 1);
    int held[2];
    for (int k = 0; k < 2; ++k) {
      p->val[eng_track_param(0, TP_LENGTH)] = static_cast<float>(k);
      for (int b = 0; b < 400; ++b) eng_render(l, r, n);
      eng_play(1);
      held[k] = 0;
      for (int b = 0; b < 689; ++b) {                     // a bar: four notes
        eng_render(l, r, n);
        held[k] += eng_gates() & 1u;
      }
      eng_play(0);
    }
    printf("length 5 %% then 95 %%: the gate held %d then %d blocks a bar (~8 and ~164 expected)\n", held[0], held[1]);
    if (held[0] > 14 || held[1] < 150 || held[1] > 175) ++failures, printf("FAIL: the steps' length\n");
  }

  // 20. the delay: one note (a kick), its echoes a quarter (0.5 s) apart, ping-pong: left at a quarter, right at two
  {
    eng_blank(p);
    p->algo[PG_TRACK(0, 0)] = 21;
    eng_trig_set(0, 0, 1);
    p->val[PG_STEP(0) * 4 + 3] = 64.0f;                   // one note in four bars
    p->val[eng_track_param(0, TP_DSEND)] = 1.0f;
    p->val[eng_track_param(0, TP_PAN)] = 1.0f;           // the kick on the right: the left the echoes alone
    static const float kD[8] = {8.0f, 0.3f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f};   // 1/4, ping-pong, flat, no mod
    for (int k = 0; k < 8; ++k) p->val[PG_DELAY(k / 4) * 4 + k % 4] = kD[k];
    for (int b = 0; b < 400; ++b) eng_render(l, r, n);
    eng_play(1);
    static float L[44100 * 2], R[44100 * 2];
    for (int b = 0; b < 44100 * 2 / 128; ++b) {
      eng_render(l, r, n);
      for (int i = 0; i < 128; ++i) L[b * 128 + i] = l[i], R[b * 128 + i] = r[i];
    }
    eng_play(0);
    int pl = 0, pr = 0;
    pr = 33075;                                           // (the right after the kick)
    for (int i = 1; i < 44100 * 2 / 128 * 128; ++i) {
      if (std::fabs(L[i]) > std::fabs(L[pl])) pl = i;
      if (i > 33075 && std::fabs(R[i]) > std::fabs(R[pr])) pr = i;
    }
    printf("the delay, a quarter ping-pong: its loudest echo at %.3f s left, %.3f s right (0.5 and 1.0 expected)\n",
           pl / 44100.0, pr / 44100.0);
    if (pl < 22050 || pl > 22050 + 2000 || pr < 44100 || pr > 44100 + 2000) ++failures, printf("FAIL: the delay\n");
  }

  // 21. the effects' lanes: without a step the effect stays open (1); a step opens it, DECAY closes it down to
  // 1 - DEPTH; Warps' lane locks its ALGORITHM on a step
  {
    eng_defaults(p);
    eng_steps_clear(LANE_DELAY);
    p->val[PG_WARPS(1) * 4 + 0] = WC_T3;                  // Warps: T3 by T2
    p->val[PG_WARPS(0) * 4 + 0] = 0.0f;                   // CROSSFADE, and DIGITAL RING locked on step 9
    eng_trig_set(LANE_WARPS, 0, 1);
    eng_trig_set(LANE_WARPS, 8, 1);
    eng_lock_set(LANE_WARPS, 8, 0, 0.6f);
    p->val[PG_STEP(LANE_WARPS) * 4 + 0] = 0.2f;           // ~35 ms
    p->val[PG_STEP(LANE_WARPS) * 4 + 2] = 1.0f;
    for (int b = 0; b < 400; ++b) eng_render(l, r, n);
    eng_play(1);
    float dmin = 2, dmax = -1, wmin = 2, wmax = -1, rmin = 2;
    int algs = 0;
    for (int b = 0; b < 689; ++b) {
      eng_render(l, r, n);
      float d = eng_lane_open(LANE_DELAY), w = eng_lane_open(LANE_WARPS), rv = eng_lane_open(LANE_REVERB);
      dmin = std::fmin(dmin, d), dmax = std::fmax(dmax, d), wmin = std::fmin(wmin, w), wmax = std::fmax(wmax, w);
      rmin = std::fmin(rmin, rv);
      algs |= 1 << eng_warps_algo();
    }
    eng_play(0);
    printf("lanes: the delay without steps open %.2f..%.2f, the reverb %.2f; Warps' %.2f..%.2f, its algorithms %s%s\n",
           dmin, dmax, rmin, wmin, wmax, algs & 1 ? "CROSSFADE" : "", algs & 8 ? " and DIGITAL RING" : "");
    if (dmin < 1.0f || rmin < 1.0f || wmax < 0.95f || wmin > 0.05f || algs != (1 | 8)) ++failures, printf("FAIL: the lanes\n");
    eng_defaults(p);
  }

  // 22. the duck: T1 on every beat, T2 a held note (VIRTUAL ANALOG, its gate the whole step); T2's level just after
  // T1's trigs against just before them: down as deep as T2's DUCK (0.8: -14 dB), none at 0
  {
    double ratio[2];
    for (int k = 0; k < 2; ++k) {
      eng_play(0);
      eng_blank(p);
      p->algo[PG_COMP] = MB_OFF;
      p->algo[PG_TRACK(0, 0)] = 21;
      p->val[eng_track_param(0, TP_LEVEL)] = 0.0f;         // (T1 itself silent: T2 alone heard)
      for (int st = 0; st < 16; st += 4) eng_trig_set(0, st, 1);
      for (int st = 0; st < 16; ++st) eng_trig_set(1, st, 1);
      p->val[eng_track_param(1, TP_LENGTH)] = 1.0f;
      p->val[PG_TRACK(1, 1) * 4 + 0] = 1.0f;               // a long LPG decay: a steady note
      p->val[PG_DUCK * 4 + 0] = 0.3f;                      // ~85 ms
      p->val[eng_track_param(1, TP_DUCK)] = k ? 0.8f : 0.0f;
      for (int b = 0; b < 400; ++b) eng_render(l, r, n);
      eng_play(1);
      double after = 0, before = 0;
      for (int b = 0; b < 689 * 2; ++b) {                  // two bars: a beat every 172 blocks
        eng_render(l, r, n);
        int ph = b % 172;
        double e = 0;
        for (uint32_t i = 0; i < n; ++i) e += static_cast<double>(l[i]) * l[i];
        if (b < 172) continue;
        if (ph >= 3 && ph < 8) after += e;                 // 9..23 ms after the trig
        if (ph >= 160 && ph < 165) before += e;            // the end of the beat: back up
      }
      ratio[k] = 10.0 * std::log10((after + 1e-12) / (before + 1e-12));
      eng_play(0);
    }
    printf("duck: T2 just after T1's trigs %+.1f dB against just before (duck 0.8), %+.1f dB (duck 0)\n", ratio[1],
           ratio[0]);
    if (ratio[1] > -9.0 || std::fabs(ratio[0]) > 3.0) ++failures, printf("FAIL: the duck\n");
  }

  // 23. the master bus: the compressor brings a loud patch down (its reduction seen), OTT keeps it as loud at its
  // depth's middle, every mode's peak under full scale (the limiter: 0.8 at most, the soft limit after)
  {
    double rms[3];
    float pk[3], gr = 0;
    for (int m = 0; m < 3; ++m) {
      eng_play(0);
      eng_defaults(p);
      p->algo[PG_COMP] = static_cast<uint8_t>(m);
      for (int t = 0; t < ENG_TRACKS; ++t) p->val[eng_track_param(t, TP_LEVEL)] = 1.0f;
      p->val[PG_COMP * 4 + 0] = 0.6f;                      // -22 dB, 3:1
      for (int b = 0; b < 400; ++b) eng_render(l, r, n);
      eng_play(1);
      double e = 0;
      pk[m] = 0;
      for (int b = 0; b < 1378; ++b) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          e += static_cast<double>(l[i]) * l[i];
          pk[m] = std::fmax(pk[m], std::fabs(l[i]));
        }
        if (m == MB_COMP) gr = std::fmin(gr, eng_gr(0));
      }
      rms[m] = std::sqrt(e / (1378.0 * n));
      eng_play(0);
    }
    printf("master bus: off rms %.3f peak %.2f; comp (-22 dB, 3:1, +4 dB) rms %.3f peak %.2f, %.1f dB at most; OTT rms %.3f "
           "peak %.2f\n", rms[0], pk[0], rms[1], pk[1], gr, rms[2], pk[2]);
    if (gr > -6.0f || pk[0] > 1.0f || pk[1] > 1.0f || pk[2] > 1.0f || rms[2] < rms[0] * 0.6 || rms[2] > rms[0] * 1.7)
      ++failures, printf("FAIL: the master bus\n");
    eng_defaults(p);
  }

  // 24. 0.3's project (version 6, 2804 bytes): all of it, the duck off, the master bus INIT's
  {
    static uint8_t buf[sizeof(project_t)];
    project_t* q = reinterpret_cast<project_t*>(buf);
    for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = 0;
    q->magic = PROJECT_MAGIC;
    q->version = 6;
    q->size = 2804;
    float* val = reinterpret_cast<float*>(buf + 8);
    for (int i = 0; i < 148; ++i) val[i] = 0.25f;
    uint8_t* algo = buf + 8 + 148 * 4;
    algo[PG_TRACK(3, 0)] = 23;                            // track 4 on HI-HAT
    uint8_t* trig = buf + 8 + 592 + 37 + 3 + 160;
    trig[LANE_DELAY * 8] = 0x05;                          // the delay's lane: steps 1 and 3
    int8_t* rnd = reinterpret_cast<int8_t*>(trig + 56);
    rnd[2 * 19 + TP_DSEND] = -40;                         // track 3's delay send, looped
    int ok = eng_upgrade(q, 2804);
    printf("a version 6 project: %s, track 4 model %d, a knob %.2f, the delay's lane %d%d, a random %d, duck %.2f, bus %d\n",
           ok ? "read" : "REFUSED", q->algo[PG_TRACK(3, 0)], q->val[100], q->trig[LANE_DELAY][0] & 1,
           (q->trig[LANE_DELAY][0] >> 2) & 1, q->rnd[2][TP_DSEND], q->val[PG_DUCK * 4 + 2], q->algo[PG_COMP]);
    if (!ok || !eng_check(q) || q->algo[PG_TRACK(3, 0)] != 23 || q->val[100] != 0.25f || q->trig[LANE_DELAY][0] != 0x05 ||
        q->rnd[2][TP_DSEND] != -40 || q->val[PG_DUCK * 4 + 2] != 0.0f || q->algo[PG_COMP] != MB_COMP)
      ++failures, printf("FAIL: the version 6 project\n");
  }

  // 26. 0.3.7's project (version 7, 2864 bytes): all of it, the filter flat. Then the filter: the demo playing, every
  // track's low pass down (the highs gone: the mix darker), then its high pass up (brighter); the knob's text
  {
    static uint8_t buf[sizeof(project_t)];
    project_t* q = reinterpret_cast<project_t*>(buf);
    for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = 0;
    q->magic = PROJECT_MAGIC;
    q->version = 7;
    q->size = 2864;
    float* val = reinterpret_cast<float*>(buf + 8);
    for (int i = 0; i < 160; ++i) val[i] = 0.25f;
    uint8_t* algo = buf + 8 + 160 * 4;
    algo[PG_COMP] = MB_OTT;
    algo[41] = 1;                                         // (after the 40 pages' choices: out_mode, warps_hq,
    algo[42] = 3;                                         // theme)
    uint8_t* trig = algo + 43 + 160;
    trig[LANE_REVERB * 8] = 0x11;
    int8_t* rnd = reinterpret_cast<int8_t*>(trig + 56);
    rnd[1 * 20 + TP_DUCK] = -30;
    int ok = eng_upgrade(q, 2864);
    printf("a version 7 project: %s, OTT %d, HQ %d, theme %d, the reverb's lane %02x, a random %d, the filter %.2f\n",
           ok ? "read" : "REFUSED", q->algo[PG_COMP], q->warps_hq, q->theme, q->trig[LANE_REVERB][0],
           q->rnd[1][TP_DUCK], q->val[PG_EQ * 4 + 1]);
    if (!ok || !eng_check(q) || q->algo[PG_COMP] != MB_OTT || q->warps_hq != 1 || q->theme != 3 ||
        q->trig[LANE_REVERB][0] != 0x11 || q->rnd[1][TP_DUCK] != -30 || q->val[PG_EQ * 4 + 1] != 0.0f ||
        q->val[PG_OTT * 4 + 3] != 0.25f)
      ++failures, printf("FAIL: the version 7 project\n");

    char a[16], b[16], c[16];
    eng_param_text(PG_EQ * 4, -1.0f, a, sizeof a);
    eng_param_text(PG_EQ * 4 + 1, 1.0f, b, sizeof b);
    eng_param_text(PG_EQ * 4 + 2, 0.0f, c, sizeof c);
    eng_play(0);
    eng_defaults(p);
    double bright[3];
    for (int m = 0; m < 3; ++m) {
      for (int t = 0; t < ENG_TRACKS; ++t) p->val[PG_EQ * 4 + t] = m == 0 ? 0.0f : m == 1 ? -0.8f : 0.6f;
      eng_play(0);
      eng_play(1);
      double e = 0, d = 0;
      float prev = 0;
      int nan = 0;
      for (int k = 0; k < 44100 * 4 / static_cast<int>(n); ++k) {
        eng_render(l, r, n);
        for (uint32_t i = 0; i < n; ++i) {
          if (!std::isfinite(l[i])) nan = 1;
          e += static_cast<double>(l[i]) * l[i];
          d += static_cast<double>(l[i] - prev) * (l[i] - prev);
          prev = l[i];
        }
      }
      bright[m] = nan ? -1.0 : std::sqrt(d / (e + 1e-20));
    }
    printf("the filter: %s, %s, %s; the mix's brightness flat %.3f, low passes %.3f, high passes %.3f\n", a, b, c,
           bright[0], bright[1], bright[2]);
    if (bright[0] <= 0 || !(bright[1] < bright[0] * 0.6) || !(bright[2] > bright[0] * 1.5))
      ++failures, printf("FAIL: the filter\n");
    eng_play(0);
    eng_defaults(p);
  }

  // 27. the Euclidean rhythms (3 in 8: x..x..x., 5 in 16, turned), 0.3.8's project (version 8, 2888 bytes: all of it,
  // no grit), then the grit on a held note of track 1 (a virtual analog voice, alone): BITS at the end (2 bits: a few
  // values left), RATE at the end (689 Hz: a sample held 64), DRIVE through each shape (finite, within full scale)
  {
    char e38[9], e516r[17];
    int n516 = 0;
    for (int i = 0; i < 8; ++i) e38[i] = eng_euclid(8, 3, 0, i) ? 'x' : '.';
    for (int i = 0; i < 16; ++i) {
      e516r[i] = eng_euclid(16, 5, 2, i) ? 'x' : '.';
      n516 += eng_euclid(16, 5, 0, i);
    }
    e38[8] = e516r[16] = 0;
    printf("euclid: 3 in 8 %s, 5 in 16 turned 2 %s\n", e38, e516r);
    if (strcmp(e38, "x..x..x.") != 0 || n516 != 5 || !eng_euclid(16, 5, 2, 2) || eng_euclid(16, 5, 2, 0) ||
        eng_euclid(8, 0, 0, 0) || !eng_euclid(4, 9, 0, 3))
      ++failures, printf("FAIL: the Euclidean rhythms\n");

    static uint8_t buf[sizeof(project_t)];
    project_t* q = reinterpret_cast<project_t*>(buf);
    for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = 0;
    q->magic = PROJECT_MAGIC;
    q->version = 8;
    q->size = 2888;
    float* val = reinterpret_cast<float*>(buf + 8);
    for (int i = 0; i < 164; ++i) val[i] = 0.25f;
    uint8_t* algo = buf + 8 + 164 * 4;
    algo[43] = 4;                                         // (after the 41 pages' choices: out_mode, warps_hq, theme)
    int8_t* rnd = reinterpret_cast<int8_t*>(algo + 44 + 160 + 56);
    rnd[3 * 21 + TP_EQ] = 25;
    int ok = eng_upgrade(q, 2888);
    printf("a version 8 project: %s, theme %d, the filter %.2f, its random %d, drive %.2f, fills %.0f\n",
           ok ? "read" : "REFUSED", q->theme, q->val[PG_EQ * 4 + 2], q->rnd[3][TP_EQ], q->val[PG_GRIT(1) * 4],
           q->val[PG_EUC(2) * 4]);
    if (!ok || !eng_check(q) || q->theme != 4 || q->val[PG_EQ * 4 + 2] != 0.25f || q->rnd[3][TP_EQ] != 25 ||
        q->val[PG_GRIT(1) * 4] != 0.0f || q->val[PG_EUC(2) * 4] != 0.0f)
      ++failures, printf("FAIL: the version 8 project\n");

    eng_play(0);
    eng_blank(p);
    for (int t = 1; t < ENG_TRACKS; ++t) p->val[PG_MIX * 4 + t] = 0.0f;
    p->val[PG_TRACK(0, 1) * 4 + 0] = 1.0f;               // a long decay: the note held
    p->algo[PG_COMP] = MB_OFF;                           // (the compressor's gain moves sample by sample, the
    p->val[PG_REVERB * 4 + 3] = 0.0f;                    // effects' tails of the tests before go on)
    p->val[PG_DELAY(1) * 4 + 3] = 0.0f;
    auto measure = [&](int* distinct, int* changes, float* peak) {
      for (int b = 0; b < 300; ++b) eng_render(l, r, n);
      int d = 0, c = 0, seen[128];
      float pk = 0.0f;
      for (int b = 0; b < 20; ++b) {     // (values to 1e-4: what the other tracks' silence leaves is under it)
        eng_render(l, r, n);
        int nd = 0;
        for (uint32_t i = 0; i < n; ++i) {
          if (!std::isfinite(l[i])) pk = 1e9f;
          pk = std::max(pk, std::fabs(l[i]));
          if (i && std::fabs(l[i] - l[i - 1]) > 1e-4f) ++c;
          int v = static_cast<int>(std::floor(l[i] * 1e4f + 0.5f)), k = 0;
          while (k < nd && seen[k] != v) ++k;
          if (k == nd && nd < 128) seen[nd++] = v;
        }
        d = std::max(d, nd);
      }
      *distinct = d;
      *changes = c / 20;
      *peak = pk;
    };
    eng_key(0, 48, 1);
    int d0, c0, d1, c1, d2, c2, dd, cd;
    float k0, k1, k2, kd[3];
    measure(&d0, &c0, &k0);
    p->val[PG_GRIT(0) * 4 + 3] = 0.85f;                  // BITS: 4
    measure(&d1, &c1, &k1);
    float k3;
    p->val[PG_GRIT(0) * 4 + 3] = 1.0f;                   // BITS: 2 (this quiet a voice: none left)
    measure(&dd, &cd, &k3);
    p->val[PG_GRIT(0) * 4 + 3] = 0.0f;
    p->val[PG_GRIT(0) * 4 + 2] = 1.0f;                   // RATE: 689 Hz
    measure(&d2, &c2, &k2);
    p->val[PG_GRIT(0) * 4 + 2] = 0.0f;
    p->val[PG_GRIT(0) * 4 + 0] = 1.0f;                   // DRIVE, each shape
    for (int sh = 0; sh < 3; ++sh) {
      p->val[PG_GRIT(0) * 4 + 1] = static_cast<float>(sh);
      measure(&dd, &cd, &kd[sh]);
    }
    eng_key(0, 48, 0);
    printf("grit: plain %d values a block, %d changes, peak %.2f; 4 bits %d values; 2 bits peak %.4f; 689 Hz %d changes;"
           " drive peaks %.2f %.2f %.2f\n", d0, c0, k0, d1, k3, c2, kd[0], kd[1], kd[2]);
    if (d0 < 64 || d1 > 16 || k3 > 1e-3f || c2 > 4 || k0 <= 0.01f || kd[0] > 1.0f || kd[1] > 1.0f || kd[2] > 1.0f ||
        kd[0] < k0)
      ++failures, printf("FAIL: the grit\n");
    eng_defaults(p);
  }

  printf(failures ? "%d FAILURES\n" : "all good\n", failures);
  return failures ? 1 : 0;
}
