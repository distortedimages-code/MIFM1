// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1: the delay (mi_delay.h) behind bridge.h, its line 1.2 s of 16-bit samples (a ping-pong's echoes 0.6 s apart
// at most).
#include <new>
#include "bridge.h"
#include "mi_delay.h"

namespace {

const uint32_t kLine = 52920u;           // 1.2 s at 44.1 kHz

struct DelayMod {
  mifm1::Delay delay;
  int16_t line[kLine];
};

}  // namespace

extern "C" {

uint32_t mdl_size(void) { return sizeof(DelayMod); }

void mdl_init(void* mem) {
  DelayMod* d = new (mem) DelayMod();
  d->delay.Init(d->line, kLine, MI_SR);
}

void mdl_set(void* v, float time, float feedback, float filter, float resonance, float drive, int ping_pong,
             float mod) {
  static_cast<DelayMod*>(v)->delay.set(time, feedback, filter, resonance, drive, ping_pong, mod);
}

void mdl_process(void* v, const float* in, float* out_l, float* out_r, uint32_t n) {
  static_cast<DelayMod*>(v)->delay.Process(in, out_l, out_r, n);
}

}  // extern "C"
