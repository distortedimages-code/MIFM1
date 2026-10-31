// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1: Marbles' t and X/Y generators (third_party/marbles, Emilie Gillet, MIT) behind bridge.h, as marbles.cc's
// ProcessFrame drives them with nothing patched: the internal clock, X clocked by T1/T2/T3, Y by the master clock
// divided, the random stream on its fallback generator (no hardware RNG here). The firmware calls it at the control
// rate (a few frames per audio block): its times are given that rate (mm_init's sr), so RATE keeps its meaning.
#include <new>
#include "bridge.h"
#include "stmlib/utils/gate_flags.h"
#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "marbles/preset_scales.h"

namespace {

const uint32_t kChunk = 8;

struct MarblesGen {
  marbles::RandomGenerator random_generator;
  marbles::RandomStream random_stream;
  marbles::TGenerator t_generator;
  marbles::XYGenerator xy_generator;
  marbles::GroupSettings x, y;
  stmlib::GateFlags no_clock[kChunk];
  float ramp_buffer[kChunk * 4];
  bool gates[kChunk * 2];
  float voltages[kChunk * 4];
  bool reset;
};

template <typename T> T clampv(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }

}  // namespace

extern "C" {

uint32_t mm_size(void) { return sizeof(MarblesGen); }

void mm_init(void* mem, uint32_t seed, float sr) {
  MarblesGen* m = new (mem) MarblesGen();
  m->random_generator.Init(seed);
  m->random_stream.Init(&m->random_generator);
  m->t_generator.Init(&m->random_stream, sr);
  m->xy_generator.Init(&m->random_stream, sr);
  for (int i = 0; i < 6; ++i) m->xy_generator.LoadScale(i, marbles::preset_scales[i]);
  m->t_generator.set_pulse_width_mean(0.5f);
  m->t_generator.set_pulse_width_std(0.0f);
  m->x.control_mode = marbles::CONTROL_MODE_IDENTICAL;
  m->x.voltage_range = marbles::VOLTAGE_RANGE_NARROW;
  m->x.register_mode = false;
  m->x.register_value = 0.0f;
  m->x.spread = 0.5f;
  m->x.bias = 0.5f;
  m->x.steps = 0.5f;
  m->x.deja_vu = 0.0f;
  m->x.scale_index = 0;
  m->x.length = 8;
  m->x.ratio.p = 1;
  m->x.ratio.q = 1;
  m->y = m->x;                          // marbles.cc: Y its own spread / bias / steps, no deja vu, the clock / 4
  m->y.length = 1;
  m->y.ratio.p = 1;
  m->y.ratio.q = 4;
  for (uint32_t i = 0; i < kChunk; ++i) m->no_clock[i] = stmlib::GATE_FLAG_LOW;
  m->reset = false;
  mm_params_t p = {0, 1, 0.0f, 0.5f, 0.0f, 0.5f, 8, 1, 1, 0.5f, 0.5f, 0.5f, 0, 0, 0, 0.5f, 0.5f, 0.0f};
  mm_set(m, &p);
}

void mm_set(void* v, const mm_params_t* p) {
  MarblesGen* m = static_cast<MarblesGen*>(v);
  int length = clampv(p->length, 1, 16);
  float deja_vu = clampv(p->deja_vu, 0.0f, 1.0f);
  m->t_generator.set_model(static_cast<marbles::TGeneratorModel>(clampv(p->t_model, 0, 6)));
  m->t_generator.set_range(static_cast<marbles::TGeneratorRange>(clampv(p->t_range, 0, 2)));
  m->t_generator.set_rate(clampv(p->rate, -48.0f, 48.0f));
  m->t_generator.set_bias(clampv(p->t_bias, 0.0f, 1.0f));
  m->t_generator.set_jitter(clampv(p->jitter, 0.0f, 1.0f));
  m->t_generator.set_deja_vu(p->t_deja_vu ? deja_vu : 0.0f);
  m->t_generator.set_length(length);
  m->x.deja_vu = p->x_deja_vu ? deja_vu : 0.0f;
  m->x.length = length;
  m->x.spread = clampv(p->spread, 0.0f, 1.0f);
  m->x.bias = clampv(p->x_bias, 0.0f, 1.0f);
  m->x.steps = clampv(p->steps, 0.0f, 1.0f);
  m->x.control_mode = static_cast<marbles::ControlMode>(clampv(p->x_mode, 0, 2));
  m->x.voltage_range = static_cast<marbles::VoltageRange>(clampv(p->x_range, 0, 2));
  m->x.scale_index = clampv(p->scale, 0, 5);
  m->y.spread = clampv(p->y_spread, 0.0f, 1.0f);
  m->y.bias = clampv(p->y_bias, 0.0f, 1.0f);
  m->y.steps = clampv(p->y_steps, 0.0f, 1.0f);
  m->y.voltage_range = m->x.voltage_range;
  m->y.scale_index = m->x.scale_index;
}

void mm_reset(void* v) { static_cast<MarblesGen*>(v)->reset = true; }

void mm_render(void* v, uint8_t* gates3, float* cv4, uint32_t n) {
  MarblesGen* m = static_cast<MarblesGen*>(v);
  while (n) {
    uint32_t size = n < kChunk ? n : kChunk;
    marbles::Ramps ramps;
    ramps.master = &m->ramp_buffer[0];
    ramps.external = &m->ramp_buffer[kChunk];
    ramps.slave[0] = &m->ramp_buffer[kChunk * 2];
    ramps.slave[1] = &m->ramp_buffer[kChunk * 3];
    bool reset = m->reset;
    m->t_generator.Process(false, &reset, m->no_clock, ramps, m->gates, size);
    reset = m->reset;
    m->reset = false;
    m->xy_generator.Process(marbles::CLOCK_SOURCE_INTERNAL_T1_T2_T3, m->x, m->y, &reset, m->no_clock, ramps,
                            m->voltages, size);
    for (uint32_t i = 0; i < size; ++i) {
      *gates3++ = m->gates[2 * i];
      *gates3++ = ramps.master[i] < 0.5f;
      *gates3++ = m->gates[2 * i + 1];
      for (int c = 0; c < 4; ++c) *cv4++ = m->voltages[4 * i + c];
    }
    n -= size;
  }
}

}  // extern "C"
