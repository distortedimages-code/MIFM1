// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Yoann
// MIFM1: the FM drums (the LXR-02's voices, third_party/lxr) as the FM-1 computes them, against the same code
// computing as the LXR-02 does (built with LXR_HARD_DOUBLE: its doubles, its fused multiply-adds). Both builds play
// the same: each voice on its kit and on random knobs, at random notes, knobs turning, trigs every 200 ms.
//   make lxr-check   (build/lxr_ref writes build/lxr_ref.raw, build/lxr_port compares itself with it)
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include "mi/bridge.h"

static uint32_t rs = 12345;
static float rnd() {
  rs = rs * 1664525u + 1013904223u;
  return static_cast<float>(rs >> 8) / 16777216.0f;
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  const int N = 128, kSets = 12, kBlocks = 44100 * 2 / N;
  static int16_t out[LX_MODELS * kSets * kBlocks * N];
  float buf[N];
  long k = 0;
  lx_init();
  for (int m = 0; m < LX_MODELS; ++m) {
    for (int set = 0; set < kSets; ++set) {
      float knob[LX_KNOBS];
      lx_defaults(m, knob);
      if (set > 0)
        for (int i = 0; i < LX_KNOBS; ++i) knob[i] = rnd();
      float note = set == 0 ? 48.f : 30.f + rnd() * 40.f;
      int t = set % 4;
      for (int b = 0; b < kBlocks; ++b) {
        if (set > 6 && b % 50 == 25) knob[static_cast<int>(rnd() * LX_KNOBS) % LX_KNOBS] = rnd();
        lx_set(t, m, note, knob);
        if (b % 70 == 0) lx_trigger(t);
        lx_render(t, buf, N);
        for (int i = 0; i < N; ++i) out[k++] = static_cast<int16_t>(buf[i] * 32768.0f);
      }
    }
  }
#ifdef LXR_HARD_DOUBLE
  FILE* f = fopen(argv[1], "wb");
  if (!f || fwrite(out, sizeof out, 1, f) != 1) return 1;
  fclose(f);
  printf("the LXR-02's arithmetic: %ld samples written\n", k);
  return 0;
#else
  static int16_t ref[sizeof out / sizeof out[0]];
  FILE* f = fopen(argv[1], "rb");
  if (!f || fread(ref, sizeof ref, 1, f) != 1) return 1;
  fclose(f);
  long same = 0, maxd = 0;
  double err = 0.0, sig = 0.0;
  for (long i = 0; i < k; ++i) {
    long d = labs(static_cast<long>(out[i]) - ref[i]);
    same += d == 0;
    if (d > maxd) maxd = d;
    err += static_cast<double>(d) * d;
    sig += static_cast<double>(ref[i]) * ref[i];
  }
  printf("the FM drums against the LXR-02's arithmetic: %.3f %% of %ld samples identical, the others %ld LSB at "
         "most (%.1f dB under the sound)\n", 100.0 * same / k, k, maxd,
         err > 0 ? 10.0 * __builtin_log10(err / sig) : -999.0);
  return same >= k - k / 1000 && maxd <= 64 ? 0 : 1;   // (99.9 %: the gains' interpolation, a few filters, in float)
#endif
}
