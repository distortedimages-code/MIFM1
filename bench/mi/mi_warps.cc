// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1 bench: Warps' modulator (third_party/warps, Emilie Gillet, MIT) behind bridge.h.
// Its vocoder's filter bank decimates by 12, so a call takes a multiple of 12 frames (warps.cc: 60): the bridge
// queues the FM-1's blocks into 48-frame calls (48 frames of latency).
#include <new>
#include "bridge.h"
#include "warps/dsp/modulator.h"

namespace {

const uint32_t kChunk = 48;
const uint32_t kFifo = 512;

struct WarpsMod {
  warps::Modulator modulator;
  warps::ShortFrame pending[kChunk];
  uint32_t num_pending;
  float out_l[kFifo], out_r[kFifo];     // processed frames waiting to go out
  uint32_t out_read, out_count;
};

const char* const kAlgoNames[MW_ALGOS] = {
  "XFADE", "FOLD", "DIODE RM", "DIGITAL RM", "XOR", "COMPARATOR", "VOCODER", "VOCODER LONG", "FREQ SHIFT",
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
  w->modulator.Init(MI_SR);
  warps::Parameters* p = w->modulator.mutable_parameters();
  p->channel_drive[0] = 0.7f;
  p->channel_drive[1] = 0.7f;
  p->modulation_algorithm = 0.0f;
  p->modulation_parameter = 0.5f;
  p->frequency_shift_pot = 0.6f;
  p->frequency_shift_cv = 0.0f;
  p->phase_shift = 0.0f;
  p->note = 48.0f;
  p->carrier_shape = 0;                 // the carrier is our input 1, as with a cable in Warps' first input
  w->num_pending = 0;
  w->out_read = w->out_count = 0;
}

void mw_set(void* v, int algo, float timbre, float level1, float level2) {
  WarpsMod* w = static_cast<WarpsMod*>(v);
  warps::Parameters* p = w->modulator.mutable_parameters();
  if (algo < 0 || algo >= MW_ALGOS) algo = 0;
  w->modulator.set_easter_egg(algo == MW_ALGOS - 1);
  p->modulation_algorithm = algo < MW_ALGOS - 1 ? static_cast<float>(algo) / 8.0f : 0.0f;
  p->modulation_parameter = timbre;
  p->frequency_shift_pot = timbre;
  p->channel_drive[0] = level1;
  p->channel_drive[1] = level2;
}

void mw_render(void* v, const float* carrier, const float* modulator, float* out, float* aux, uint32_t n) {
  WarpsMod* w = static_cast<WarpsMod*>(v);
  warps::ShortFrame processed[kChunk];
  for (uint32_t i = 0; i < n; ++i) {
    w->pending[w->num_pending].l = to_short(carrier[i]);
    w->pending[w->num_pending].r = to_short(modulator[i]);
    if (++w->num_pending == kChunk) {
      w->modulator.Process(w->pending, processed, kChunk);
      for (uint32_t j = 0; j < kChunk && w->out_count < kFifo; ++j) {
        uint32_t k = (w->out_read + w->out_count) % kFifo;
        w->out_l[k] = static_cast<float>(processed[j].l) * (1.0f / 32768.0f);
        w->out_r[k] = static_cast<float>(processed[j].r) * (1.0f / 32768.0f);
        ++w->out_count;
      }
      w->num_pending = 0;
    }
  }
  for (uint32_t i = 0; i < n; ++i) {
    if (w->out_count) {
      out[i] = w->out_l[w->out_read];
      aux[i] = w->out_r[w->out_read];
      w->out_read = (w->out_read + 1) % kFifo;
      --w->out_count;
    } else {
      out[i] = aux[i] = 0.0f;           // the first 48 frames
    }
  }
}

const char* mw_algo_name(int algo) { return algo >= 0 && algo < MW_ALGOS ? kAlgoNames[algo] : "?"; }

}  // extern "C"
