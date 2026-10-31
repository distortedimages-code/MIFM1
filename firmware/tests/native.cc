// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1 on a computer: the engine (mi/engine.cc) plays its default patch, as the FM-1 would at power-on, and is
// checked: every track sounds, Marbles' gates fall on the tempo, the sources stay in range, no NaN, the keyboard
// plays a stopped machine, a project's values are written as the screen writes them. WAVs in the folder given.
//   build/native build/wav [seconds]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
extern "C" {
#include "plat.h"
}
#include "mi/engine.h"
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
  printf("  (T2 on track 1: %0.f beats at 120 BPM expected)\n", seconds * 2.0f);
  if (bad || rms < 0.01) ++failures;
  if (rises[0] < seconds * 2.0f * 0.9f || rises[0] > seconds * 2.0f * 1.1f + 1) {
    printf("FAIL: track 1 does not follow T2 at 120 BPM\n");
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
  p->val[PG_TRACK(0, 2) * 4 + 0] = GATE_KEYS;
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
  eng_param_text(PG_MARBLES(0) * 4 + 0, p->val[PG_MARBLES(0) * 4 + 0], t, sizeof t);
  printf("RATE %s", t);
  if (strcmp(t, "120 BPM")) ++failures, printf(" (FAIL: 120 BPM expected)");
  eng_param_text(PG_TRACK(0, 0) * 4 + 0, 0.5f, t, sizeof t);
  printf(", FREQUENCY +0.5 -> %s", t);
  if (strcmp(t, "+12.0")) ++failures, printf(" (FAIL: +12.0 expected)");
  eng_param_text(PG_TIDES(1, 0) * 4 + 0, p->val[PG_TIDES(1, 0) * 4 + 0], t, sizeof t);
  printf(", TIDES B %s", t);
  eng_param_text(PG_TIDES(0, 0) * 4 + 0, 0.0f, t, sizeof t);
  printf(", TIDES A at 0: %s\n", t);
  if (strcmp(t, "0.02 Hz")) ++failures, printf(" (FAIL: 0.02 Hz expected)\n");

  // 5. INIT: four Plaits on VIRTUAL ANALOG, nothing patched; each track plays
  eng_blank(p);
  int cables = 0, models = 0;
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
  printf("init: %d cables, %d tracks on VIRTUAL ANALOG (%s), rms %.3f, gates %u %u %u %u\n", cables, models,
         eng_algo_name(PG_TRACK(0, 0), 8), std::sqrt(ie / (iblocks * n)), irises[0], irises[1], irises[2], irises[3]);
  if (cables || models != ENG_TRACKS || inan || ie < 1e-3) ++failures, printf("FAIL: the INIT project\n");
  for (int tr = 0; tr < ENG_TRACKS; ++tr)
    if (!irises[tr]) ++failures, printf("FAIL: INIT, track %d never plays\n", tr + 1);

  // 6. the step sequencer: the demo's patterns at 120 BPM (a sixteenth 125 ms), 8 s = 4 bars
  eng_play(0);
  eng_defaults(p);
  p->val[PG_GLOBAL * 4] = SEQ_STEPS;
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
  p->val[PG_GLOBAL * 4 + 2] = 11.0f;                    // minor pentatonic
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

  // 9. v0.1's project (version 2) read into this version's
  {
    static uint8_t buf[sizeof(project_t)];
    project_t* q = reinterpret_cast<project_t*>(buf);
    for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = 0;
    q->magic = PROJECT_MAGIC;
    q->version = 2;
    q->size = 564;
    float* val = reinterpret_cast<float*>(buf + 8);
    for (int i = 0; i < 92; ++i) val[i] = 0.25f;
    buf[8 + 92 * 4 + PG_TRACK(1, 0)] = 17;                // track 2 on NOISE
    cable_t* cab = reinterpret_cast<cable_t*>(buf + 8 + 92 * 4 + 25);
    cab[0].src = SRC_A2;
    cab[0].dst = 3;
    cab[0].depth = 33;
    int ok = eng_upgrade(q, 564);
    printf("a version 2 project: %s, track 2 model %d, a cable %s, knob %.2f, %d locks\n", ok ? "read" : "REFUSED",
           q->algo[PG_TRACK(1, 0)], q->cable[0][0].depth == 33 ? "kept" : "LOST", q->val[5], eng_step_locked(0, 12));
    if (!ok || !eng_check(q) || q->algo[PG_TRACK(1, 0)] != 17 || q->cable[0][0].depth != 33 || q->val[5] != 0.25f)
      ++failures, printf("FAIL: the version 2 project\n");
  }

  printf(failures ? "%d FAILURES\n" : "all good\n", failures);
  return failures ? 1 : 0;
}
