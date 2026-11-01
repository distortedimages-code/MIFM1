// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1: Warps' modulator (third_party/warps, Emilie Gillet, MIT) behind bridge.h, at the FM-1's 44.1 kHz.
//
// The module runs at 96 kHz. What depends on its rate is in tables, made again for 44.1 kHz (tools/warps_tables.py,
// patch_mi.sh 5): the vocoder's filter bank, the frequency shifter's allpass poles and the oscillators' note table by
// the module's own scripts; the cross-modulations' 6x oversampling filters by the recipe written above them, longer
// (72 taps) to stay flat to 12 kHz. (0.2.0 and 0.2.1 ran it at 88.2 kHz: twice the cost, too much for the FM-1.)
//
// Its inputs never reach exact zero on the module (its converters' noise): here they get a 1-LSB dither, so that in
// a silence its filters' states stay ordinary numbers instead of sinking into subnormals (very slow on most FPUs).
//
// The vocoder decimates by 12, so a Process call takes a multiple of 12 frames (warps.cc: 60): the bridge queues 48
// of the FM-1's frames a call (~1.1 ms late).
#include <new>
#include "bridge.h"
#include "warps/dsp/modulator.h"

namespace {

const uint32_t kFrames = 48;             // the frames a Process call
const uint32_t kFifo = 256;

struct WarpsMod {
  warps::Modulator modulator;
  warps::ShortFrame in[kFrames], out[kFrames];
  uint32_t num_in;                       // the frames queued
  uint32_t dither;                       // a little noise generator (LCG)
  float fifo[kFifo];                     // the output, waiting to go
  uint32_t fifo_read, fifo_count;
};

inline short to_short(float x, uint32_t* seed) {
  *seed = *seed * 1664525u + 1013904223u;
  x = x * 32767.0f + ((*seed >> 31) ? 0.5f : -0.5f);   // half an LSB either way: a silence is +/-1 LSB of noise
  if (x > 32767.0f) x = 32767.0f;
  if (x < -32767.0f) x = -32767.0f;
  return static_cast<short>(x + (x >= 0.0f ? 0.5f : -0.5f));
}

}  // namespace

extern "C" {

uint32_t mw_size(void) { return sizeof(WarpsMod); }

void mw_init(void* mem) {
  WarpsMod* w = new (mem) WarpsMod();
  w->modulator.Init(MI_SR);
  warps::Parameters* p = w->modulator.mutable_parameters();
  p->channel_drive[0] = 0.7f;
  p->channel_drive[1] = 0.7f;
  p->modulation_algorithm = 0.0f;
  p->modulation_parameter = 0.5f;
  p->frequency_shift_pot = 0.5f;
  p->frequency_shift_cv = 0.0f;
  p->phase_shift = 0.0f;
  p->note = 48.0f;
  p->carrier_shape = 0;
  w->num_in = 0;
  w->dither = 0x2545F491u;
  w->fifo_read = 0;
  w->fifo_count = kFrames / 2;           // a call's worth of silence ahead: the output never waits mid-block
  for (uint32_t i = 0; i < kFifo; ++i) w->fifo[i] = 0.0f;
}

void mw_set(void* v, float algorithm, float timbre, float level1, float level2, int osc, float note, int shift) {
  WarpsMod* w = static_cast<WarpsMod*>(v);
  warps::Parameters* p = w->modulator.mutable_parameters();
  w->modulator.set_easter_egg(shift != 0);
  p->modulation_algorithm = algorithm;
  p->modulation_parameter = timbre;
  p->frequency_shift_pot = algorithm;    // the easter egg: ALGORITHM shifts, TIMBRE picks the side, LEVEL 1 feeds
  p->channel_drive[0] = level1;          // back, LEVEL 2 mixes (modulator.cc: ProcessEasterEgg)
  p->channel_drive[1] = level2;
  p->carrier_shape = osc < 0 ? 0 : osc > 3 ? 3 : osc;
  p->note = note;
}

uint32_t mw_calls(void* v, uint32_t n) {   // the Process calls the next n frames make (2 or 3 for 128)
  return (static_cast<WarpsMod*>(v)->num_in + n) / kFrames;
}

void mw_render(void* v, const float* carrier, const float* modulator, float* out, uint32_t n) {
  WarpsMod* w = static_cast<WarpsMod*>(v);
  for (uint32_t i = 0; i < n; ++i) {
    w->in[w->num_in].l = to_short(carrier[i], &w->dither);
    w->in[w->num_in].r = to_short(modulator[i], &w->dither);
    if (++w->num_in < kFrames) continue;
    w->num_in = 0;
    w->modulator.Process(w->in, w->out, kFrames);
    for (uint32_t j = 0; j < kFrames && w->fifo_count < kFifo; ++j) {
      w->fifo[(w->fifo_read + w->fifo_count) % kFifo] = static_cast<float>(w->out[j].l) * (1.0f / 32768.0f);
      ++w->fifo_count;
    }
  }
  for (uint32_t i = 0; i < n; ++i) {
    if (w->fifo_count) {
      out[i] = w->fifo[w->fifo_read];
      w->fifo_read = (w->fifo_read + 1) % kFifo;
      --w->fifo_count;
    } else {
      out[i] = 0.0f;
    }
  }
}

}  // extern "C"
