// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1: Clouds' granular processor (third_party/clouds, Emilie Gillet, MIT) behind bridge.h.
// clouds.cc ran it at 32 kHz with Prepare() in its main loop; here it gets the FM-1's 44.1 kHz frames as they come
// (its times and pitches run 1.38 x faster: the bench measures the work per frame, the firmware will resample) and
// Prepare() runs right after each block, so its spectral FFTs and stretch search count in the render's time.
#include <new>
#include "bridge.h"
#include "clouds/dsp/granular_processor.h"
#include "mi_reverb.h"
#include "mi_metal.h"

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
  bool again = false;                   // a new mode or quality: Prepare() re-lays the buffers out, here and now
  if (mode != c->processor.playback_mode()) {
    c->processor.set_playback_mode(static_cast<clouds::PlaybackMode>(mode));
    again = true;
  }
  if (quality != c->processor.quality()) {
    c->processor.set_quality(quality & 3);
    again = true;
  }
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
  if (again) c->processor.Prepare();
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

// ---- the reverb, as a send: Clouds' (clouds/dsp/fx/reverb.h: the Griesinger topology of Dattorro's paper), or
// (0.16.41) MIFM1's METAL (mi_metal.h), SHIMMER its octave fed back in ----
// Their 16384 delay cells at 16 bits (32 KB) in the object, one or the other's: from Clouds to METAL or back, the tail
// faded out over a block (2.9 ms), the cells cleared, the other's from silence. At 44.1 kHz instead of Clouds' 32 its
// room is 0.73 x as large. Fully wet: the engine sets the return level.
namespace {

const uint32_t kRvChunk = 32;

struct ReverbFx {                       // mi/mi_reverb.h: Clouds' reverb, its delay lines in 16-bit words
  mifm1::Reverb reverb;
  mifm1::MetalVerb metal;
  uint16_t buffer[16384];
  clouds::FloatFrame frames[kRvChunk];
  int mode, want;                       // the algorithm heard (RV_*), the one asked
  float time, damping, diffusion;       // the knobs, Clouds' or METAL's (decay, size, tone)
};

float clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

void clouds_init(ReverbFx* r) {
  for (uint32_t i = 0; i < 16384; ++i) r->buffer[i] = 0u;
  r->reverb.Init(r->buffer, 44100.0f);
  r->reverb.set_amount(1.0f);
  r->reverb.set_input_gain(0.2f);
  r->reverb.set_time(0.7f);
  r->reverb.set_diffusion(0.625f);
  r->reverb.set_lp(0.7f);
}

void apply(ReverbFx* r) {
  if (r->mode == RV_CLOUDS) {
    r->reverb.set_time(0.35f + 0.63f * clamp01(r->time));   // granular_processor.cc's
    r->reverb.set_lp(0.95f - 0.6f * clamp01(r->damping));
    r->reverb.set_diffusion(0.4f + 0.35f * clamp01(r->diffusion));
  } else {
    r->metal.Set(r->time, r->damping, r->diffusion, r->mode == RV_SHIMMER);
  }
}

}  // namespace

extern "C" {

uint32_t mrv_size(void) { return sizeof(ReverbFx); }

void mrv_init(void* mem) {
  ReverbFx* r = new (mem) ReverbFx();
  clouds_init(r);
  r->mode = r->want = RV_CLOUDS;
  r->time = r->damping = r->diffusion = 0.5f;
}

void mrv_set(void* v, int mode, float time, float damping, float diffusion) {
  ReverbFx* r = static_cast<ReverbFx*>(v);
  r->want = mode == RV_METAL || mode == RV_SHIMMER ? mode : RV_CLOUDS;
  r->time = time, r->damping = damping, r->diffusion = diffusion;
  if ((r->want == RV_CLOUDS) == (r->mode == RV_CLOUDS)) r->mode = r->want;   // (METAL and SHIMMER: the same cells)
  apply(r);
}

void mrv_process(void* v, const float* in, float* out_l, float* out_r, uint32_t n) {
  ReverbFx* r = static_cast<ReverbFx*>(v);
  const int swap = r->want != r->mode;  // (from Clouds to METAL or back: this block the last of the old one, faded)
  if (r->mode != RV_CLOUDS) {          // (METAL: an even count, 128 at most, at a time; an odd one's last frame its
    for (uint32_t at = 0; at < n;) {    // last but one's)
      const uint32_t size = (n - at > 128u ? 128u : n - at) & ~1u;
      if (!size) {
        out_l[at] = at ? out_l[at - 1u] : 0.0f;
        out_r[at] = at ? out_r[at - 1u] : 0.0f;
        break;
      }
      r->metal.Process(in + at, out_l + at, out_r + at, size);
      at += size;
    }
  } else {
    float* const l0 = out_l;
    float* const r0 = out_r;
    const uint32_t n0 = n;
    while (n) {
      uint32_t size = n < kRvChunk ? n : kRvChunk;
      for (uint32_t i = 0; i < size; ++i) {
        r->frames[i].l = in[i];
        r->frames[i].r = 0.0f;
      }
      r->reverb.Process(r->frames, size);
      for (uint32_t i = 0; i < size; ++i) {
        out_l[i] = r->frames[i].l;
        out_r[i] = r->frames[i].r;
      }
      in += size;
      out_l += size;
      out_r += size;
      n -= size;
    }
    out_l = l0, out_r = r0, n = n0;
  }
  if (swap) {
    const float k = n ? 1.0f / static_cast<float>(n) : 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
      const float g = 1.0f - static_cast<float>(i + 1u) * k;
      out_l[i] *= g;
      out_r[i] *= g;
    }
    r->mode = r->want;
    if (r->mode == RV_CLOUDS) clouds_init(r);
    else r->metal.Init(reinterpret_cast<int16_t*>(r->buffer));
    apply(r);
  }
}

}  // extern "C"

