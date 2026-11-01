// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1: Warps' modulator (third_party/warps, Emilie Gillet, MIT) behind bridge.h.
//
// The module runs at 96 kHz. Its rate shows in two places:
//   - its tables: the vocoder's filter bank, the frequency shifter's allpass poles, the oscillators' note-to-increment
//     table. tools/warps_tables.py (patch_mi.sh, 5) makes them again for 44.1 kHz with the module's own scripts;
//   - the cross-modulations oversample by 6 through filters cut at a fixed part of the rate (sample_rate_conversion_
//     filters.h): at 44.1 kHz they would let through only up to ~8 kHz (the module: ~18 kHz).
// So the vocoder and the frequency shifter run at the FM-1's 44.1 kHz, on those tables (their cost halved), and the
// cross-modulations at twice it, 88.2 kHz, a half-band filter (47 taps, Kaiser) up and down: flat to 19 kHz, -52 dB
// from 25 kHz; the oscillators' notes an octave down there (the table is 44.1 kHz's). Where ALGORITHM crosses into the
// vocoder (0.7: modulator.cc plays the modulator alone there) the modulator starts again at the other rate, its
// output faded in over 1.5 ms.
//
// The vocoder decimates by 12, so a Process call takes a multiple of 12 frames (warps.cc: 60): the bridge queues 48 of
// the FM-1's frames a call, 96 at 88.2 kHz; ~1.1 ms late (with the half-band filters ~1.6 ms).
#include <new>
#include "bridge.h"
#include "warps/dsp/modulator.h"

namespace {

const uint32_t kFrames = 48;             // the FM-1's frames a Process call
const uint32_t kFifo = 256;
const uint32_t kFade = 64;               // a new rate's output faded in
const int kTaps = 12;                    // the half-band's taps each side of its centre (the odd ones; the even are 0)
const float kHalfBand[kTaps] = {         // h[23 ± (2i + 1)]; h[23] = 0.5
  3.171045930e-01f, -1.021766419e-01f, 5.724601119e-02f, -3.683010031e-02f, 2.482882255e-02f, -1.688292362e-02f,
  1.132235443e-02f, -7.356148259e-03f, 4.542039737e-03f, -2.594200723e-03f, 1.304474981e-03f, -5.082811444e-04f,
};

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
  warps::ShortFrame in[2 * kFrames], out[2 * kFrames];
  uint32_t num_in;                       // the FM-1's frames queued
  int twice;                             // running at 88.2 kHz (the cross-modulations)
  int want;                              // the rate the knobs ask for, taken at the next call's start
  uint32_t fade;                         // frames into a fade-in
  float note;                            // the oscillators' note as set (MIDI)
  warps::Parameters set;                 // the knobs as set (a new start clears the modulator's own)
  int shift;
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

void apply(WarpsMod* w) {               // the knobs to the modulator, at its rate
  warps::Parameters* p = w->modulator.mutable_parameters();
  *p = w->set;
  p->note = w->note - (w->twice ? 12.0f : 0.0f);   // the table is 44.1 kHz's: an octave down at twice the rate
  w->modulator.set_easter_egg(w->shift != 0);
}

void start(WarpsMod* w, int twice) {    // the modulator (again) at a rate
  w->twice = twice;
  w->modulator.Init(twice ? 2.0f * MI_SR : MI_SR);
  apply(w);
  for (int i = 0; i < 8 * kTaps; ++i) w->down.y[i] = 0.0f;
  w->down.at = 0;
  w->fade = 0;
}

}  // namespace

extern "C" {

uint32_t mw_size(void) { return sizeof(WarpsMod); }

void mw_init(void* mem) {
  WarpsMod* w = new (mem) WarpsMod();
  w->set.channel_drive[0] = 0.7f;
  w->set.channel_drive[1] = 0.7f;
  w->set.modulation_algorithm = 0.0f;
  w->set.modulation_parameter = 0.5f;
  w->set.frequency_shift_pot = 0.5f;
  w->set.frequency_shift_cv = 0.0f;
  w->set.phase_shift = 0.0f;
  w->set.note = 48.0f;
  w->set.carrier_shape = 0;
  w->note = 48.0f;
  w->shift = 0;
  w->num_in = 0;
  for (int i = 0; i < 4 * kTaps; ++i) w->up_carrier.x[i] = w->up_modulator.x[i] = 0.0f;
  w->up_carrier.at = w->up_modulator.at = 0;
  w->want = 1;
  start(w, 1);
  w->fade = kFade;
  w->fifo_read = 0;
  w->fifo_count = kFrames / 2;           // a call's worth of silence ahead: the output never waits mid-block
  for (uint32_t i = 0; i < kFifo; ++i) w->fifo[i] = 0.0f;
}

void mw_set(void* v, float algorithm, float timbre, float level1, float level2, int osc, float note, int shift) {
  WarpsMod* w = static_cast<WarpsMod*>(v);
  w->set.modulation_algorithm = algorithm;
  w->set.modulation_parameter = timbre;
  w->set.frequency_shift_pot = algorithm;   // the easter egg: ALGORITHM shifts, TIMBRE picks the side, LEVEL 1 feeds
  w->set.channel_drive[0] = level1;         // back, LEVEL 2 mixes (modulator.cc: ProcessEasterEgg)
  w->set.channel_drive[1] = level2;
  w->set.carrier_shape = osc < 0 ? 0 : osc > 3 ? 3 : osc;
  w->note = note;
  w->shift = shift;
  // the rate: twice for the cross-modulations; with a margin around 0.7, so that a knob held there does not swap
  if (shift) w->want = 0;
  else if (algorithm < 0.69f) w->want = 1;
  else if (algorithm > 0.71f) w->want = 0;
  else w->want = w->twice;
  apply(w);
}

uint32_t mw_calls(void* v, uint32_t n) {   // the Process calls the next n frames make (2 or 3 for 128)
  return (static_cast<WarpsMod*>(v)->num_in + n) / kFrames;
}

void mw_render(void* v, const float* carrier, const float* modulator, float* out, uint32_t n) {
  WarpsMod* w = static_cast<WarpsMod*>(v);
  for (uint32_t i = 0; i < n; ++i) {
    float c0, c1, m0, m1;                // (the up-samplers always run: their past is ready for a swap)
    w->up_carrier.Process(carrier[i], &c0, &c1);
    w->up_modulator.Process(modulator[i], &m0, &m1);
    if (w->num_in == 0 && w->want != w->twice) start(w, w->want);   // a swap of rate, between two calls
    if (w->twice) {
      w->in[2 * w->num_in].l = to_short(c0);
      w->in[2 * w->num_in].r = to_short(m0);
      w->in[2 * w->num_in + 1].l = to_short(c1);
      w->in[2 * w->num_in + 1].r = to_short(m1);
    } else {
      w->in[w->num_in].l = to_short(carrier[i]);
      w->in[w->num_in].r = to_short(modulator[i]);
    }
    if (++w->num_in < kFrames) continue;
    w->num_in = 0;
    w->modulator.Process(w->in, w->out, w->twice ? 2 * kFrames : kFrames);
    for (uint32_t j = 0; j < kFrames; ++j) {
      float y = w->twice ? w->down.Process(static_cast<float>(w->out[2 * j].l) * (1.0f / 32768.0f),
                                           static_cast<float>(w->out[2 * j + 1].l) * (1.0f / 32768.0f))
                         : static_cast<float>(w->out[j].l) * (1.0f / 32768.0f);
      if (w->fade < kFade) y *= static_cast<float>(w->fade++) * (1.0f / kFade);
      if (w->fifo_count < kFifo) {
        w->fifo[(w->fifo_read + w->fifo_count) % kFifo] = y;
        ++w->fifo_count;
      }
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
