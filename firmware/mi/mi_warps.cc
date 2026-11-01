// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1: Warps' modulator (third_party/warps, Emilie Gillet, MIT) behind bridge.h, at 88.2 kHz.
//
// The module runs at 96 kHz, and its tables are for that rate: the vocoder's 20 bands (resources.cc, 87 Hz to 7 kHz,
// at 96 kHz decimated by 12, 3 and 1), the frequency shifter's quadrature filters, the internal oscillators'
// note-to-increment table. At the FM-1's 44.1 kHz all would sit more than an octave low. The bridge runs the modulator
// at twice the FM-1's rate, 88.2 kHz: the bands and filters 8 % (1.5 semitone) under the module's, the oscillators'
// notes raised by those 1.5 semitones to play in tune. A half-band filter (47 taps, Kaiser) takes the inputs up and
// the output down: flat to 19 kHz, -52 dB from 25 kHz.
//
// The vocoder decimates by 12, so a Process call takes a multiple of 12 frames (warps.cc: 60): the bridge queues them
// by 96 (kMaxBlockSize) at 88.2 kHz, 48 of the FM-1's frames; with the filters, ~1.6 ms of latency.
#include <new>
#include "bridge.h"
#include "warps/dsp/modulator.h"

namespace {

const uint32_t kChunk = 96;              // Warps' frames a call, at 88.2 kHz
const uint32_t kFifo = 256;
const int kTaps = 12;                    // the half-band's taps each side of its centre (the odd ones; the even are 0)
const float kHalfBand[kTaps] = {         // h[23 ± (2i + 1)]; h[23] = 0.5
  3.171045930e-01f, -1.021766419e-01f, 5.724601119e-02f, -3.683010031e-02f, 2.482882255e-02f, -1.688292362e-02f,
  1.132235443e-02f, -7.356148259e-03f, 4.542039737e-03f, -2.594200723e-03f, 1.304474981e-03f, -5.082811444e-04f,
};
const float kNoteFix = 1.4674f;          // 12 log2(96000 / 88200): the oscillators' tables are for 96 kHz

// 44.1 kHz in, 88.2 out: a frame on the grid (the input, 12 frames late), then one between
struct Up {
  float x[4 * kTaps];                    // the last 24 inputs, twice (a window always in one piece)
  int at;
  void Process(float in, float* a, float* b) {
    x[at] = x[at + 2 * kTaps] = in;
    if (++at == 2 * kTaps) at = 0;
    const float* w = &x[at];             // w[0] the oldest .. w[23] the newest
    float s = 0.0f;
    for (int i = 0; i < kTaps; ++i) s += kHalfBand[i] * (w[kTaps - 1 - i] + w[kTaps + i]);
    *a = w[kTaps - 1];
    *b = 2.0f * s;
  }
};

// 88.2 kHz in, 44.1 out: two frames in, one out
struct Down {
  float y[4 * 2 * kTaps];                // the last 48 frames, twice
  int at;
  float Process(float a, float b) {
    y[at] = y[at + 4 * kTaps] = a;
    y[at + 1] = y[at + 1 + 4 * kTaps] = b;
    at += 2;
    if (at == 4 * kTaps) at = 0;
    const float* w = &y[at];             // w[47] the newest; the centre w[24]
    float s = 0.5f * w[2 * kTaps];
    for (int i = 0; i < kTaps; ++i) s += kHalfBand[i] * (w[2 * kTaps - 1 - 2 * i] + w[2 * kTaps + 1 + 2 * i]);
    return s;
  }
};

struct WarpsMod {
  warps::Modulator modulator;
  warps::ShortFrame in[kChunk], out[kChunk];
  uint32_t num_in;
  Up up_carrier, up_modulator;
  Down down;
  float fifo[kFifo];                     // the output, waiting to go
  uint32_t fifo_read, fifo_count;
};

inline short to_short(float x) {
  x *= 32767.0f;
  if (x > 32767.0f) x = 32767.0f;
  if (x < -32767.0f) x = -32767.0f;
  return static_cast<short>(x);
}

}  // namespace

extern "C" {

uint32_t mw_size(void) { return sizeof(WarpsMod); }

void mw_init(void* mem) {
  WarpsMod* w = new (mem) WarpsMod();
  w->modulator.Init(2.0f * MI_SR);
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
  for (int i = 0; i < 4 * kTaps; ++i) w->up_carrier.x[i] = w->up_modulator.x[i] = 0.0f;
  for (int i = 0; i < 8 * kTaps; ++i) w->down.y[i] = 0.0f;
  w->up_carrier.at = w->up_modulator.at = w->down.at = 0;
  w->fifo_read = 0;
  w->fifo_count = kChunk / 2;            // a call's worth of silence ahead: the output never waits mid-block
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
  p->note = note + kNoteFix;
}

void mw_render(void* v, const float* carrier, const float* modulator, float* out, uint32_t n) {
  WarpsMod* w = static_cast<WarpsMod*>(v);
  for (uint32_t i = 0; i < n; ++i) {
    float c0, c1, m0, m1;
    w->up_carrier.Process(carrier[i], &c0, &c1);
    w->up_modulator.Process(modulator[i], &m0, &m1);
    w->in[w->num_in].l = to_short(c0);
    w->in[w->num_in].r = to_short(m0);
    w->in[w->num_in + 1].l = to_short(c1);
    w->in[w->num_in + 1].r = to_short(m1);
    w->num_in += 2;
    if (w->num_in == kChunk) {
      w->modulator.Process(w->in, w->out, kChunk);
      for (uint32_t j = 0; j < kChunk; j += 2) {
        float y = w->down.Process(static_cast<float>(w->out[j].l) * (1.0f / 32768.0f),
                                  static_cast<float>(w->out[j + 1].l) * (1.0f / 32768.0f));
        if (w->fifo_count < kFifo) {
          w->fifo[(w->fifo_read + w->fifo_count) % kFifo] = y;
          ++w->fifo_count;
        }
      }
      w->num_in = 0;
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
