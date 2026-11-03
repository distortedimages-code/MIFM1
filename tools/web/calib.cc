// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// The browser emulator's FM-1 load (tools/web/calibrate.py): the engine played on this computer under valgrind's
// callgrind, which counts the instructions each part runs. A count, not a time: the computer's clock says little of
// the FM-1's (x86 vectorises loops, crawls on subnormals, has caches of another size), while the FM-1's time for a
// part of the engine has kept close to its instruction count (0.5.8's stress test, measured on both: the voices,
// Warps and the effects 5.4 to 6.8 us a thousand instructions, the preparation 17). The browser cannot time the engine
// either (its clock moves a render quantum at a time), so there the engine's clock is a model's: each part costs
// what the table calibrate.py writes says (firmware/mi/web_costs.h).
//   calib stress SECONDS                 the stress test playing
//   calib one MODEL FILTER FM NOISE BEND BUS WARPS HQ SECONDS [EXTRA]
//                                        T1 alone playing a note every step (its voice never rests), on MODEL, its
//                                        filter FILTER (FM, NOISE: 0..1), BEND 0/1 (its bits); the bus BUS (MB_*); WARPS -1 off
//                                        or its algorithm's mark 0..5 (T2 by T1, T2 playing too), HQ 0/1; EXTRA
//                                        (bits) T1's EQ, the duck, the delay's filter and drive; the FM's square, PW,
//                                        SYNC; PADS' MANUAL triad, six notes, one saw a note, its bell; POLY's voice, four
//   calib blank SECONDS                  nothing playing (the preparation's floor, a resting voice)
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
#include "plat.h"
}
#include "mi/engine.h"
#include "mi/bridge.h"

extern "C" {
uint32_t plat_ticks24(void) { return 0; }
int plat_cpu2_on(void) { return 1; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t a) { fn(a); return 1; }
int plat_cpu2_wait(void) { return 0; }
int plat_cpu2_busy(void) { return 0; }
void plat_cpu2_sync(void) {}
}

int main(int argc, char** argv) {
  if (eng_init() != 0) return 1;
  const char* what = argc > 1 ? argv[1] : "stress";
  project_t* p = eng_project();
  float secs = 2.0f;
  if (!strcmp(what, "stress")) {
    eng_stress(p);
    secs = argc > 2 ? static_cast<float>(atof(argv[2])) : 4.0f;
  } else if (!strcmp(what, "blank")) {                   // (the tracks on an FM drum: silent until a trig, they
    eng_blank(p);                                        // rest; INIT's Plaits sound without one)
    for (int t = 0; t < ENG_TRACKS; ++t) eng_set_model(t, eng_model_find(MK_LXR, 0));
    for (int l = 0; l < ENG_LANES; ++l)
      for (int s = 0; s < ENG_STEPS; ++s) eng_trig_set(l, s, 0);
    secs = argc > 2 ? static_cast<float>(atof(argv[2])) : 2.0f;
  } else if (!strcmp(what, "one") && argc >= 11) {
    int model = atoi(argv[2]), filter = atoi(argv[3]), bend = atoi(argv[6]), bus = atoi(argv[7]);
    int warps = atoi(argv[8]), hq = atoi(argv[9]);
    float fm = static_cast<float>(atof(argv[4])), noise = static_cast<float>(atof(argv[5]));
    secs = static_cast<float>(atof(argv[10]));
    eng_blank(p);
    for (int l = 0; l < ENG_LANES; ++l)
      for (int s = 0; s < ENG_STEPS; ++s) eng_trig_set(l, s, 0);
    for (int t = 1; t < ENG_TRACKS; ++t) eng_set_model(t, eng_model_find(MK_LXR, 0));   // (resting)
    eng_set_model(0, model);
    p->val[PG_TRACK(0, 1) * 4 + 0] = 0.6f;               // DECAY
    p->val[PG_TRACK(0, 2) * 4 + 1] = 0.9f;               // GATE: long
    for (int s = 0; s < 16; ++s) eng_trig_set(0, s, 1);
    p->algo[PG_FILT(0, 0)] = static_cast<uint8_t>(filter);
    p->val[PG_FILT(0, 0) * 4 + 0] = 0.6f;
    p->val[PG_FILT(0, 0) * 4 + 1] = 0.5f;
    p->val[PG_FILT(0, 0) * 4 + 2] = 0.3f;
    p->val[PG_FILT(0, 1) * 4 + 2] = fm;
    p->val[PG_FILT(0, 2) * 4 + 1] = noise;
    if (bend) {                          // (the bits: CLOCK, BITS, a MASK)
      p->val[PG_BEND(0) * 4 + 0] = 0.3f;
      p->val[PG_BEND(0) * 4 + 1] = 0.3f;
      p->val[PG_BEND(0) * 4 + 2] = 2.0f;
    }
    p->algo[PG_COMP] = static_cast<uint8_t>(bus);
    int extra = argc > 11 ? atoi(argv[11]) : 0;          // the effects' own costs: 1 T1's EQ, 2 the duck on T2..T4,
    if (extra & 1) p->val[PG_EQ * 4 + 0] = 0.25f;        // 4 the delay's filter and drive (the stress test's)
    if (extra & 2)
      for (int t = 1; t < ENG_TRACKS; ++t) p->val[PG_DUCK * 4 + t] = 0.3f;
    if (extra & 4) p->val[PG_DELAY(0) * 4 + 2] = -0.15f, p->val[PG_DELAY(0) * 4 + 3] = 0.3f;
    if (extra & 8) p->val[PG_TRACK(0, 1) * 4 + 3] = -0.6f;   // 8 the FM's square (FEEDBACK), 16 its PW, 32 its SYNC
    if (extra & 16) p->val[PG_TRACK(0, 2) * 4 + 0] = -0.5f;  // (MUTATE), on its sixth algorithm (three operators
    if (extra & 32) p->val[PG_TRACK(0, 2) * 4 + 0] = 0.5f;   // heard)
    if (extra & 56) p->val[PG_TRACK(0, 0) * 4 + 1] = 5.5f / 8.0f;
    if (extra & 512) p->val[PG_TRACK(0, 0) * 4 + 1] = 0.0f;   // PADS (512), MANUAL: a triad; 64 an eleventh, a bass
    if (extra & 64) p->val[PG_TRACK(0, 0) * 4 + 2] = p->val[PG_TRACK(0, 0) * 4 + 3] = 0.9f;   // under it (6 notes);
    if (extra & 128) p->val[PG_TRACK(0, 1) * 4 + 3] = -0.5f;  // 128 ENSEMBLE to VINTAGE (one saw a note); 256 the bells
    if (extra & 256) p->val[PG_TRACK(0, 2) * 4 + 0] = 0.5f;   // (their bell on the top note)
    if (extra & 1024) p->val[PG_TRACK(0, 1) * 4 + 0] = 1.0f;  // POLY (1024): held; 2048 UNISON (four voices); 8192
    if (extra & 2048) p->val[PG_TRACK(0, 2) * 4 + 0] = 0.5f;  // its DECAY short (a voice a trig, over before the
    if (extra & 8192) p->val[PG_TRACK(0, 1) * 4 + 0] = 0.0f;  // next); PROPHET: 1024 held, 8192 short, 16384 a
    if (extra & 16384) p->val[PG_TRACK(0, 2) * 4 + 0] = 2.0f * 13.5f / 15.0f - 1.0f;   // Min7 (four voices)
    p->warps_hq = static_cast<uint8_t>(hq);
    if (warps >= 0) {
      static const float kW[8] = {static_cast<float>(WC_T2), 0.0f, 0.0f, 1.0f, 0.5f, 0.6f, 0.6f,
                                  static_cast<float>(WM_WARPS)};
      for (int k = 0; k < 8; ++k) p->val[PG_WARPS(k / 4) * 4 + k % 4] = kW[k];
      p->val[PG_WARPS(0) * 4 + 2] = static_cast<float>(warps) / 5.0f;
      eng_set_model(1, eng_model_find(MK_FM, 0));
      p->val[PG_TRACK(1, 2) * 4 + 1] = 0.9f;
      for (int s = 0; s < 16; ++s) eng_trig_set(1, s, 1);
    }
  } else {
    fprintf(stderr, "calib stress|blank|one ...\n");
    return 2;
  }
  eng_fresh();
  eng_play(1);
  static float l[128], r[128];
  uint32_t nb = static_cast<uint32_t>(secs * 44100.0f / 128.0f);
  double nv = 0.0;                      // (POLY's or PROPHET's voices sounding, on average: their cost's unit)
  for (uint32_t b = 0; b < nb; ++b) {
    eng_render(l, r, 128);
    nv += eng_model_kind(eng_project()->algo[PG_TRACK(0, 0)]) == MK_PROPHET ? pro_voices(0) : poly_voices(0);
  }
  printf("blocks %u\nvoices %.4f\n", nb, nv / static_cast<double>(nb ? nb : 1));
  return 0;
}
