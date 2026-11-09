// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1: what the machines share, one copy for all (0.16.21: each had its own, ~3 KB of the flash): their knob
// texts, their output's coupling capacitor and soft knee; (0.16.84) the microtonal SCALEs' tuning.
#include "bridge.h"

extern "C" {

MI_COLD void put_s(char* b, int n, int* at, const char* s) {
  while (*s && *at < n - 1) b[(*at)++] = *s++;
  b[*at] = 0;
}

MI_COLD void put_i(char* b, int n, int* at, int x) {
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

MI_COLD void put_time(char* b, int n, int* at, float s) {   // 12ms, 340ms, 1.2s
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

// n samples of y (times g) through the coupling capacitor (~5 Hz: x, y its states) and a soft knee above 0.8 (at most
// 1.2); its tail settled under 1e-20: 0
void mi_finish(float* y, uint32_t n, float g, float* dcx, float* dcy) {
  float px = *dcx, py = *dcy;
  for (uint32_t j = 0; j < n; ++j) {
    const float yv = g * y[j], yd = yv - px + 0.9993f * py;
    px = yv;
    py = yd;
    float ay = yd < 0.0f ? -yd : yd, o = yd;
    if (ay > 0.8f) {
      const float z = (ay - 0.8f) * 2.5f;
      ay = 0.8f + 0.4f * z / (1.0f + z);
      o = yd < 0.0f ? -ay : ay;
    }
    y[j] = o;
  }
  *dcx = px;
  *dcy = (py < 0.0f ? -py : py) < 1e-20f ? 0.0f : py;
}

// (0.16.84) the microtonal SCALEs: the 12 notes over ROOT each moved by its own cents, as the engine set them for this
// block (mi_tune_set); a machine's note through it as it turns it into a pitch (mi_tuned). A note between two (a glide,
// a vibrato, a bend): the two moves blended, so that it slides from one to the other. Off: the note as it is
static float tune_st[13];               // (semitones; the 13th the first's again, an octave up)
static float tune_root;
static int tune_on;
MI_COLD void mi_tune_set(const int8_t* cents, int root) {
  tune_on = cents != 0;
  tune_root = static_cast<float>(root);
  for (int i = 0; i < 12; ++i) tune_st[i] = cents ? static_cast<float>(cents[i]) * 0.01f : 0.0f;
  tune_st[12] = tune_st[0];
}
float mi_tuned(float note) {
  if (!tune_on) return note;
  const float x = note - tune_root;
  float fl = static_cast<float>(static_cast<int>(x));
  fl -= fl > x ? 1.0f : 0.0f;           // (its floor: below 0 too)
  const int i = static_cast<int>(fl), pc = (i % 12 + 12) % 12;
  return note + tune_st[pc] + (tune_st[pc + 1] - tune_st[pc]) * (x - fl);
}
int mi_tuning(void) { return tune_on; }

}  // extern "C"
