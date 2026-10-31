// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1 bench: Marbles' t and X/Y generators (third_party/marbles, Emilie Gillet, MIT) behind bridge.h, as
// marbles.cc's ProcessFrame drives them with nothing patched: the internal clock, X clocked by T1/T2/T3, the
// random stream on its fallback generator (no hardware RNG here).
#include <new>
#include "bridge.h"
#include "stmlib/utils/gate_flags.h"
#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"

namespace {

const uint32_t kChunk = 8;
const float kMarblesRate = 32000.0f;    // marbles.cc kSampleRate: its times; the FM-1 calls it at 44.1 kHz

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
};

}  // namespace

extern "C" {

uint32_t mm_size(void) { return sizeof(MarblesGen); }

void mm_init(void* mem, uint32_t seed) {
  MarblesGen* m = new (mem) MarblesGen();
  m->random_generator.Init(seed);
  m->random_stream.Init(&m->random_generator);
  m->t_generator.Init(&m->random_stream, kMarblesRate);
  m->xy_generator.Init(&m->random_stream, kMarblesRate);
  marbles::Scale major;
  major.InitMajor();
  for (int i = 0; i < 6; ++i) m->xy_generator.LoadScale(i, major);
  m->t_generator.set_model(marbles::T_GENERATOR_MODEL_COMPLEMENTARY_BERNOULLI);
  m->t_generator.set_range(marbles::T_GENERATOR_RANGE_1X);
  m->t_generator.set_rate(0.0f);
  m->t_generator.set_bias(0.5f);
  m->t_generator.set_jitter(0.0f);
  m->t_generator.set_deja_vu(0.5f);
  m->t_generator.set_length(8);
  m->t_generator.set_pulse_width_mean(0.5f);
  m->t_generator.set_pulse_width_std(0.0f);

  m->x.control_mode = marbles::CONTROL_MODE_IDENTICAL;
  m->x.voltage_range = marbles::VOLTAGE_RANGE_NARROW;
  m->x.register_mode = false;
  m->x.register_value = 0.0f;
  m->x.spread = 0.5f;
  m->x.bias = 0.5f;
  m->x.steps = 0.7f;
  m->x.deja_vu = 0.5f;
  m->x.scale_index = 0;
  m->x.length = 8;
  m->x.ratio.p = 1;
  m->x.ratio.q = 1;
  m->y = m->x;                          // marbles.cc: Y identical, its own spread / bias / steps
  m->y.deja_vu = 0.0f;
  m->y.length = 1;
  m->y.steps = 0.0f;
  m->y.ratio.p = 1;
  m->y.ratio.q = 4;
  for (uint32_t i = 0; i < kChunk; ++i) m->no_clock[i] = stmlib::GATE_FLAG_LOW;
}

void mm_set(void* v, int t_model, float rate, float t_bias, float jitter, float deja_vu, int length, float spread,
            float x_bias, float steps) {
  MarblesGen* m = static_cast<MarblesGen*>(v);
  m->t_generator.set_model(static_cast<marbles::TGeneratorModel>(t_model < 0 ? 0 : t_model > 6 ? 6 : t_model));
  m->t_generator.set_rate(rate);
  m->t_generator.set_bias(t_bias);
  m->t_generator.set_jitter(jitter);
  m->t_generator.set_deja_vu(deja_vu);
  m->t_generator.set_length(length);
  m->x.deja_vu = deja_vu;
  m->x.length = length;
  m->x.spread = spread;
  m->x.bias = x_bias;
  m->x.steps = steps;
}

void mm_render(void* v, uint8_t* gates3, float* cv4, uint32_t n) {
  MarblesGen* m = static_cast<MarblesGen*>(v);
  while (n) {
    uint32_t size = n < kChunk ? n : kChunk;
    marbles::Ramps ramps;
    ramps.master = &m->ramp_buffer[0];
    ramps.external = &m->ramp_buffer[kChunk];
    ramps.slave[0] = &m->ramp_buffer[kChunk * 2];
    ramps.slave[1] = &m->ramp_buffer[kChunk * 3];
    bool reset = false;
    m->t_generator.Process(false, &reset, m->no_clock, ramps, m->gates, size);
    reset = false;
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
