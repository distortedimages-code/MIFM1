// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1: the master bus, after the mixer, then stmlib's limiter on each side (Mutable's own, the one Rings and
// Elements end with: an instant peak, a slow release, 0.8 of full scale at most). Before it, one of:
//  - a compressor: stereo-linked, feed-forward, a soft knee of 6 dB; its peak read every 16 samples, its gain
//    reduction rising in ~10 ms and falling with RELEASE, the gain glided sample by sample;
//  - OTT, after the Ableton preset Xfer's plug-in made famous: three bands (split at 88 Hz and 2.5 kHz, stmlib's
//    state-variable filters, the bands each the rest of the one before: they add up to the input exactly), each
//    squashed from above (66:1 over its threshold) and raised from below (4.17:1 under its other one), with the
//    preset's thresholds, times and output gains; TIME scales the times, DEPTH mixes it with the dry, DRIVE goes in,
//    LEVEL out. Its raise is held to +24 dB, and nothing quieter than -84 dB is raised (silence stays silent).
#include <new>
#include "bridge.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/limiter.h"
extern "C" {
#include "fastmath.h"
}

namespace {

const uint32_t kChunk = 16;              // the samples a level reading covers
const float kChunksPerSec = MI_SR / static_cast<float>(kChunk);

// OTT's bands, low to high: the thresholds above and below, the attack and release (s), the output gain
const float kAbove[3] = {-33.8f, -30.2f, -35.5f};
const float kBelow[3] = {-40.8f, -41.8f, -40.8f};
const float kAttack[3] = {0.0478f, 0.0224f, 0.0135f};
const float kRelease[3] = {0.282f, 0.282f, 0.132f};
const float kOut[3] = {10.3f, 5.7f, 10.3f};
const float kWet = 6.5f;                 // and all three up as much again: as loud as the dry (a mix of the two, DEPTH,
                                         // stays as loud)
const float kDownSlope = 1.0f - 1.0f / 66.7f, kUpSlope = 1.0f - 1.0f / 4.17f;

struct Master {
  stmlib::Limiter limiter[2];
  stmlib::Svf split[2][2];               // per side: at 88 Hz, at 2.5 kHz
  int mode;
  // the compressor
  float threshold, ratio, release, makeup;   // dB, x:1, per-chunk coefficient, dB
  float gr;                              // its gain reduction now, dB
  float gain;                            // the gain the last sample took (linear)
  // OTT
  float depth, time, drive, level;       // 0..1, x, linear, linear
  float env[3];                          // each band's level, dB
  float bk[3];                           // each band's weight out (its gain, DEPTH, LEVEL), the last sample's
  float bdb[3];                          // its gain in dB (the screen's)
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

void ott(Master* m, float* l, float* r, uint32_t n) {
  float band[3][2][kChunk];
  for (uint32_t at = 0; at < n; at += kChunk) {
    uint32_t len = n - at < kChunk ? n - at : kChunk;
    float peak[3] = {1e-9f, 1e-9f, 1e-9f};
    for (uint32_t i = 0; i < len; ++i) {
      float in[2] = {l[at + i] * m->drive, r[at + i] * m->drive};
      for (int c = 0; c < 2; ++c) {
        float lo = m->split[c][0].Process<stmlib::FILTER_MODE_LOW_PASS>(in[c]), rest = in[c] - lo;
        float mid = m->split[c][1].Process<stmlib::FILTER_MODE_LOW_PASS>(rest);
        band[0][c][i] = lo;
        band[1][c][i] = mid;
        band[2][c][i] = rest - mid;
        for (int b = 0; b < 3; ++b) peak[b] = fm_maxf(peak[b], fm_fabsf(band[b][c][i]));
      }
    }
    // the output: the dry (the bands' sum: the split is complementary) and the wet (each band by its gain) mixed by
    // DEPTH, then LEVEL: each band by one weight, k = LEVEL x (1 - DEPTH + DEPTH x gain), glided over the chunk
    float k[3], dk[3];
    for (int b = 0; b < 3; ++b) {        // each band's level, then its gain: down from above, up from below
      float lv = 6.0206f * fm_log2f(peak[b]);
      m->env[b] += (lv - m->env[b]) * coef((lv > m->env[b] ? kAttack[b] : kRelease[b]) * m->time);
      float e = m->env[b], db = kOut[b] + kWet;
      if (e > kAbove[b]) db -= kDownSlope * (e - kAbove[b]);
      if (e < kBelow[b] && e > -96.0f) {
        float up = fm_minf(kUpSlope * (kBelow[b] - e), 24.0f);
        if (e < -84.0f) up *= (e + 96.0f) / 12.0f;   // (the floor: faded out)
        db += up;
      }
      m->bdb[b] = db;
      k[b] = m->level * (1.0f + m->depth * (fm_db2lin(db) - 1.0f));
      dk[b] = (k[b] - m->bk[b]) / static_cast<float>(len);
    }
    float k0 = m->bk[0], k1 = m->bk[1], k2 = m->bk[2];
    for (uint32_t i = 0; i < len; ++i) {
      k0 += dk[0];
      k1 += dk[1];
      k2 += dk[2];
      l[at + i] = band[0][0][i] * k0 + band[1][0][i] * k1 + band[2][0][i] * k2;
      r[at + i] = band[0][1][i] * k0 + band[1][1][i] * k1 + band[2][1][i] * k2;
    }
    for (int b = 0; b < 3; ++b) m->bk[b] = k[b];
  }
}

}  // namespace

extern "C" {

uint32_t mms_size(void) { return sizeof(Master); }

void mms_init(void* mem) {
  Master* m = new (mem) Master();
  for (int c = 0; c < 2; ++c) {
    m->limiter[c].Init();
    m->split[c][0].Init();
    m->split[c][1].Init();
    m->split[c][0].set_f_q<stmlib::FREQUENCY_ACCURATE>(88.3f / MI_SR, 0.7071f);
    m->split[c][1].set_f_q<stmlib::FREQUENCY_ACCURATE>(2500.0f / MI_SR, 0.7071f);
  }
  m->mode = 0;
  m->threshold = 0.0f;
  m->ratio = 1.0f;
  m->release = coef(0.1f);
  m->makeup = 0.0f;
  m->gr = 0.0f;
  m->gain = 1.0f;
  m->depth = 1.0f;
  m->time = 1.0f;
  m->drive = m->level = 1.0f;
  for (int b = 0; b < 3; ++b) {
    m->env[b] = -60.0f;
    m->bk[b] = 1.0f;
    m->bdb[b] = 0.0f;
  }
}

void mms_mode(void* v, int mode) {
  Master* m = static_cast<Master*>(v);
  if (mode == m->mode) return;
  m->mode = mode;                        // (from where it is: the gains glide from 1)
  m->gr = 0.0f;
  m->gain = 1.0f;
  for (int b = 0; b < 3; ++b) m->bk[b] = m->level;   // (the gains at 1: the dry, at LEVEL)
}

void mms_comp(void* v, float threshold_db, float ratio, float release_s, float makeup_db) {
  Master* m = static_cast<Master*>(v);
  m->threshold = threshold_db;
  m->ratio = ratio < 1.0f ? 1.0f : ratio;
  m->release = coef(release_s);
  m->makeup = makeup_db;
}

void mms_ott(void* v, float depth, float time, float drive_db, float level_db) {
  Master* m = static_cast<Master*>(v);
  m->depth = depth < 0.0f ? 0.0f : depth > 1.0f ? 1.0f : depth;
  m->time = time < 0.05f ? 0.05f : time;
  m->drive = fm_db2lin(drive_db);
  m->level = fm_db2lin(level_db);
}

void mms_process(void* v, float* l, float* r, uint32_t n) {
  Master* m = static_cast<Master*>(v);
  if (m->mode == 1) compress(m, l, r, n);
  else if (m->mode == 2) ott(m, l, r, n);
  for (uint32_t i = 0; i < n; ++i) {     // a floor at -140 dB: in a silence the limiter's peak stays an ordinary number
    l[i] += 1e-7f;                       // (its fall would sink into the subnormals, very slow on most FPUs)
    r[i] += 1e-7f;
  }
  m->limiter[0].Process(1.0f, l, n);
  m->limiter[1].Process(1.0f, r, n);
}

float mms_gain(void* v, int band) {
  Master* m = static_cast<Master*>(v);
  if (m->mode == 1) return band == 0 ? -m->gr : 0.0f;
  if (m->mode == 2 && band >= 0 && band < 3) return m->bdb[band] - kOut[band] - kWet;   // (its move alone)
  return 0.0f;
}

}  // extern "C"
