// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1 bench on a computer: every scene played for a few seconds through the same code as on the FM-1 (with the
// computer's libm), checked for NaN, silence and clipping, written as WAV. Its timings are the computer's, not the
// FM-1's: they only show which scenes are heavy beside the others.
//   build/native OUT_DIR [SECONDS]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
extern "C" {
#include "plat.h"
}
#include "mi/scenes.h"
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

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  float seconds = argc > 2 ? static_cast<float>(atof(argv[2])) : 3.0f;
  const uint32_t n = 128, blocks = static_cast<uint32_t>(seconds * 44100.0f / n);
  int failures = 0;
  {  // the resource region's file (bench/res/mi_tables.bin, what the FM-1 reads) against the arrays it replaces there
    FILE* rf = fopen("bench/res/mi_tables.bin", "rb");
    static uint8_t region[MIFM1_RES_LEN + 1];
    size_t got = rf ? fread(region, 1, sizeof region, rf) : 0;
    if (rf) fclose(rf);
    int k = mi_res_check(region, static_cast<uint32_t>(got));
    printf("resource region: %u bytes, %d tables: %s\n", static_cast<unsigned>(got), MIFM1_RES_TABLES,
           k == -1 ? "the same bytes as the arrays" : "DIFFERENT (run tools/split_tables.py)");
    if (k != -1) ++failures;
  }
  printf("%-28s %8s %8s %8s %7s  %s\n", "scene", "RAM KB", "x86 us", "x86 pk", "level", "check");
  for (int s = 0; s < sc_count(); ++s) {
    if (sc_start(s) != 0) {
      printf("%-28s %8.1f  (more than the arena's %u KB: not played)\n", sc_name(s), sc_need(s) / 1024.0, sc_arena() / 1024u);
      continue;
    }
    char path[512];
    snprintf(path, sizeof path, "%s/%02d.wav", dir, s + 1);
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    uint32_t frames = blocks * n;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + frames * 4); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 44100); put32(f, 44100 * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, frames * 4);
    float l[128], r[128], peak = 0.0f;
    double energy = 0.0;
    int bad = 0;
    for (uint32_t b = 0; b < blocks; ++b) {
      sc_render(l, r, n);
      for (uint32_t i = 0; i < n; ++i) {
        if (!std::isfinite(l[i]) || !std::isfinite(r[i])) { bad = 1; l[i] = r[i] = 0.0f; }
        float a = std::fabs(l[i]) > std::fabs(r[i]) ? std::fabs(l[i]) : std::fabs(r[i]);
        if (a > peak) peak = a;
        energy += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i];
        int16_t sl = static_cast<int16_t>(std::fmax(-1.0f, std::fmin(1.0f, l[i])) * 32767.0f);
        int16_t sr = static_cast<int16_t>(std::fmax(-1.0f, std::fmin(1.0f, r[i])) * 32767.0f);
        put16(f, static_cast<uint16_t>(sl)); put16(f, static_cast<uint16_t>(sr));
      }
    }
    fclose(f);
    sc_stats_t st;
    sc_stats(&st);
    double rms = std::sqrt(energy / (2.0 * frames));
    const char* verdict = bad ? "NaN" : rms < 1e-4 ? "SILENT" : peak > 4.0f ? "LOUD" : "ok";
    if (bad || rms < 1e-4) ++failures;
    printf("%-28s %8.1f %8.1f %8.1f %7.3f  %s\n", sc_name(s), sc_need(s) / 1024.0,
           st.blocks ? st.tot_sum / 24.0 / st.blocks : 0.0, st.tot_peak / 24.0, rms, verdict);
  }
  printf("module sizes (bytes): plaits %u, rings %u (+ reverb %u), clouds %u (with its buffers), warps %u, tides %u, marbles %u\n",
         sc_size_plaits(), sc_size_rings(), sc_size_rings_reverb(), sc_size_clouds(), sc_size_warps(), sc_size_tides(),
         sc_size_marbles());
  printf("%s\n", failures ? "FAILED" : "all scenes play");
  return failures ? 1 : 0;
}
