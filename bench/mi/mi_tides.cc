// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1 bench: Tides 2's poly slope generator (third_party/tides2, Emilie Gillet, MIT) behind bridge.h, as tides.cc
// drives it with nothing in its clock input: free-running at a frequency, triggered by its TRIG input.
#include <new>
#include "bridge.h"
#include "stmlib/utils/gate_flags.h"
#include "tides2/poly_slope_generator.h"

namespace {

const uint32_t kChunk = 8;              // tides2/io_buffer.h kBlockSize

struct TidesGen {
  tides::PolySlopeGenerator generator;
  tides::RampMode ramp;
  tides::OutputMode output;
  tides::Range range;
  float hz, shape, slope, smoothness, shift;
  int gate;
  stmlib::GateFlags previous;
};

}  // namespace

extern "C" {

uint32_t mt_size(void) { return sizeof(TidesGen); }

void mt_init(void* mem) {
  TidesGen* t = new (mem) TidesGen();
  t->generator.Init();
  t->ramp = tides::RAMP_MODE_LOOPING;
  t->output = tides::OUTPUT_MODE_SLOPE_PHASE;
  t->range = tides::RANGE_CONTROL;
  t->hz = 1.0f;
  t->shape = 0.5f;
  t->slope = 0.5f;
  t->smoothness = 0.5f;
  t->shift = 0.5f;
  t->gate = 0;
  t->previous = stmlib::GATE_FLAG_LOW;
}

void mt_set(void* v, int ramp, int output, int range, float hz, float shape, float slope, float smoothness,
            float shift) {
  TidesGen* t = static_cast<TidesGen*>(v);
  tides::OutputMode o = static_cast<tides::OutputMode>(output & 3);
  if (o != t->output) t->generator.Reset();   // tides.cc: a new output mode resets the generator
  t->ramp = static_cast<tides::RampMode>(ramp < 0 ? 0 : ramp > 2 ? 2 : ramp);
  t->output = o;
  t->range = range ? tides::RANGE_AUDIO : tides::RANGE_CONTROL;
  t->hz = hz;
  t->shape = shape;
  t->slope = slope;
  t->smoothness = smoothness;
  t->shift = shift;
}

void mt_gate(void* v, int high) { static_cast<TidesGen*>(v)->gate = high; }

void mt_render(void* v, float* out4, uint32_t n, float sr) {
  TidesGen* t = static_cast<TidesGen*>(v);
  tides::PolySlopeGenerator::OutputSample out[kChunk];
  stmlib::GateFlags gates[kChunk];
  float frequency = t->hz / sr;
  if (frequency > 0.25f) frequency = 0.25f;
  while (n) {
    uint32_t size = n < kChunk ? n : kChunk;
    for (uint32_t i = 0; i < size; ++i) {
      t->previous = stmlib::ExtractGateFlags(t->previous, t->gate != 0);
      gates[i] = t->previous;
    }
    t->generator.Render(t->ramp, t->output, t->range, frequency, t->slope, t->shape, t->smoothness, t->shift,
                        gates, 0, out, size);
    for (uint32_t i = 0; i < size; ++i) {
      for (int c = 0; c < 4; ++c) *out4++ = out[i].channel[c];
    }
    n -= size;
  }
}

}  // extern "C"
