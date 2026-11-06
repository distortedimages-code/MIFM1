// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1: the master bus, after the mixer, then stmlib's limiter on each side (Mutable's own, the one Rings and
// Elements end with: an instant peak, a slow release, 0.8 of full scale at most). Before it, a compressor:
// stereo-linked, feed-forward, a soft knee of 6 dB; its peak read every 16 samples, its gain reduction rising in ~10
// ms and falling with RELEASE, the gain glided sample by sample. (0.16.43: OTT, its three bands, gone: the
// compressor alone)
#include <new>
#include "bridge.h"
#include "stmlib/dsp/limiter.h"
extern "C" {
#include "fastmath.h"
}

namespace {

const uint32_t kChunk = 16;              // the samples a level reading covers
const float kChunksPerSec = MI_SR / static_cast<float>(kChunk);

struct Master {
  stmlib::Limiter limiter[2];
  int mode;
  // the compressor
  float threshold, ratio, release, makeup;   // dB, x:1, per-chunk coefficient, dB
  float gr;                              // its gain reduction now, dB
  float gain;                            // the gain the last sample took (linear)
};

inline float coef(float seconds) {       // a one-pole's coefficient a chunk for a time constant
  float c = seconds * kChunksPerSec;
  return 1.0f - fm_expf(-1.0f / (c < 1.0f ? 1.0f : c));
}

void compress(Master* m, float* l, float* r, uint32_t n) {
  const float kAtt = coef(0.010f), kKnee = 6.0f;
  float slope = 1.0f - 1.0f / m->ratio;
  for (uint32_t at = 0; at < n; at += kChunk) {
    uint32_t len = n - at < kChunk ? n - at : kChunk;
    float peak = 1e-6f;
    for (uint32_t i = 0; i < len; ++i) peak = fm_maxf(peak, fm_maxf(fm_fabsf(l[at + i]), fm_fabsf(r[at + i])));
    float over = 6.0206f * fm_log2f(peak) - m->threshold, target;   // dB over the threshold
    if (over <= -0.5f * kKnee) target = 0.0f;
    else if (over < 0.5f * kKnee) target = slope * (over + 0.5f * kKnee) * (over + 0.5f * kKnee) / (2.0f * kKnee);
    else target = slope * over;
    m->gr += (target - m->gr) * (target > m->gr ? kAtt : m->release);
    if (m->gr < 1e-4f) m->gr = 0.0f;     // (never down into the subnormals: slow)
    float g = fm_db2lin(m->makeup - m->gr), dg = (g - m->gain) / static_cast<float>(len);
    for (uint32_t i = 0; i < len; ++i) {
      m->gain += dg;
      l[at + i] *= m->gain;
      r[at + i] *= m->gain;
    }
    m->gain = g;
  }
}

}  // namespace

extern "C" {

uint32_t mms_size(void) { return sizeof(Master); }

void mms_init(void* mem) {
  Master* m = new (mem) Master();
  for (int c = 0; c < 2; ++c) m->limiter[c].Init();
  m->mode = 0;
  m->threshold = 0.0f;
  m->ratio = 1.0f;
  m->release = coef(0.1f);
  m->makeup = 0.0f;
  m->gr = 0.0f;
  m->gain = 1.0f;
}

void mms_mode(void* v, int mode) {
  Master* m = static_cast<Master*>(v);
  if (mode == m->mode) return;
  m->mode = mode;                        // (from where it is: the gains glide from 1)
  m->gr = 0.0f;
  m->gain = 1.0f;
}

void mms_comp(void* v, float threshold_db, float ratio, float release_s, float makeup_db) {
  Master* m = static_cast<Master*>(v);
  m->threshold = threshold_db;
  m->ratio = ratio < 1.0f ? 1.0f : ratio;
  m->release = coef(release_s);
  m->makeup = makeup_db;
}

void mms_process(void* v, float* l, float* r, uint32_t n) {
  Master* m = static_cast<Master*>(v);
  if (m->mode == 1) compress(m, l, r, n);
  for (uint32_t i = 0; i < n; ++i) {     // a floor at -140 dB: in a silence the limiter's peak stays an ordinary number
    l[i] += 1e-7f;                       // (its fall would sink into the subnormals, very slow on most FPUs)
    r[i] += 1e-7f;
  }
  m->limiter[0].Process(1.0f, l, n);
  m->limiter[1].Process(1.0f, r, n);
}

float mms_gain(void* v, int band) {
  Master* m = static_cast<Master*>(v);
  return m->mode == 1 && band == 0 ? -m->gr : 0.0f;
}

}  // extern "C"
