// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1 bench: Clouds' granular processor (third_party/clouds, Emilie Gillet, MIT) behind bridge.h.
// clouds.cc ran it at 32 kHz with Prepare() in its main loop; here it gets the FM-1's 44.1 kHz frames as they come
// (its times and pitches run 1.38 x faster: the bench measures the work per frame, the firmware will resample) and
// Prepare() runs right after each block, so its spectral FFTs and stretch search count in the render's time.
#include <new>
#include "bridge.h"
#include "clouds/dsp/granular_processor.h"

namespace {

struct CloudsProc {
  clouds::GranularProcessor processor;
};

const char* const kModeNames[MC_MODES] = { "GRANULAR", "STRETCH", "LOOPING DLY", "SPECTRAL" };

inline short to_short(float x) {
  x *= 32767.0f;
  if (x > 32767.0f) x = 32767.0f;
  if (x < -32767.0f) x = -32767.0f;
  return static_cast<short>(x);
}

}  // namespace

extern "C" {

uint32_t mc_size(void) { return sizeof(CloudsProc); }

void mc_init(void* mem, void* large, uint32_t large_size, void* small, uint32_t small_size) {
  CloudsProc* c = new (mem) CloudsProc();
  c->processor.Init(large, large_size, small, small_size);
  c->processor.set_num_channels(2);
  c->processor.set_low_fidelity(false);
  c->processor.set_playback_mode(clouds::PLAYBACK_MODE_GRANULAR);
  clouds::Parameters* p = c->processor.mutable_parameters();
  p->position = 0.3f;
  p->size = 0.5f;
  p->pitch = 0.0f;
  p->density = 0.7f;
  p->texture = 0.5f;
  p->dry_wet = 0.6f;
  p->stereo_spread = 0.5f;
  p->feedback = 0.2f;
  p->reverb = 0.3f;
  p->freeze = false;
  p->trigger = false;
  p->gate = false;
  c->processor.Prepare();
}

void mc_set(void* v, int mode, int quality, float position, float size, float pitch, float density,
            float texture, float dry_wet, float spread, float feedback, float reverb, int freeze) {
  CloudsProc* c = static_cast<CloudsProc*>(v);
  if (mode < 0 || mode >= MC_MODES) mode = 0;
  if (mode != c->processor.playback_mode()) {
    c->processor.set_playback_mode(static_cast<clouds::PlaybackMode>(mode));
  }
  if (quality != c->processor.quality()) c->processor.set_quality(quality & 3);
  clouds::Parameters* p = c->processor.mutable_parameters();
  p->position = position;
  p->size = size;
  p->pitch = pitch;
  p->density = density;
  p->texture = texture;
  p->dry_wet = dry_wet;
  p->stereo_spread = spread;
  p->feedback = feedback;
  p->reverb = reverb;
  p->freeze = freeze != 0;
}

void mc_render(void* v, const float* in_l, const float* in_r, float* out_l, float* out_r, uint32_t n) {
  CloudsProc* c = static_cast<CloudsProc*>(v);
  clouds::ShortFrame in[clouds::kMaxBlockSize], out[clouds::kMaxBlockSize];
  while (n) {
    uint32_t size = n < clouds::kMaxBlockSize ? n : clouds::kMaxBlockSize;
    for (uint32_t i = 0; i < size; ++i) {
      in[i].l = to_short(in_l[i]);
      in[i].r = to_short(in_r[i]);
    }
    c->processor.Process(in, out, size);
    for (uint32_t i = 0; i < size; ++i) {
      out_l[i] = static_cast<float>(out[i].l) * (1.0f / 32768.0f);
      out_r[i] = static_cast<float>(out[i].r) * (1.0f / 32768.0f);
    }
    in_l += size;
    in_r += size;
    out_l += size;
    out_r += size;
    n -= size;
  }
  c->processor.Prepare();
}

const char* mc_mode_name(int mode) { return mode >= 0 && mode < MC_MODES ? kModeNames[mode] : "?"; }

}  // extern "C"
