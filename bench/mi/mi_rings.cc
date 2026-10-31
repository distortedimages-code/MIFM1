// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1 bench: Rings' part (third_party/rings, Emilie Gillet, MIT) behind bridge.h, played by its internal exciter
// (rings.cc's FillBuffer without the codec: no audio in, a strum from us).
#include <new>
#include "bridge.h"
#include "rings/dsp/part.h"

namespace {

struct RingsPart {
  rings::Part part;
  rings::Patch patch;
  rings::PerformanceState state;
  int model;
  int strum;
  int has_reverb;
  float silence[rings::kMaxBlockSize];
  float pend_out[rings::kMaxBlockSize];  // a block rendered and not yet given out (see mr_render)
  float pend_aux[rings::kMaxBlockSize];
  uint32_t pend_at, pend_n;
};

const char* const kModelNames[MR_MODELS] = {
  "MODAL", "SYMPATHETIC", "STRING", "FM VOICE", "SYMP QUANT", "STRING+REVERB",
};

}  // namespace

extern "C" {

uint32_t mr_size(void) { return sizeof(RingsPart); }
uint32_t mr_reverb_size(void) { return 32768u * sizeof(uint16_t); }   // rings.cc: reverb_buffer[32768]

void mr_init(void* mem, void* reverb) {
  RingsPart* r = new (mem) RingsPart();
  r->part.Init(static_cast<uint16_t*>(reverb));
  r->has_reverb = reverb != 0;
  r->part.set_polyphony(1);
  r->part.set_model(rings::RESONATOR_MODEL_MODAL);
  r->model = 0;
  r->patch.structure = 0.4f;
  r->patch.brightness = 0.6f;
  r->patch.damping = 0.6f;
  r->patch.position = 0.3f;
  r->state.strum = false;
  r->state.internal_exciter = true;    // no audio in: the part plucks / strikes itself
  r->state.internal_strum = false;     // the strums are ours
  r->state.internal_note = false;
  r->state.tonic = 12.0f;
  r->state.note = 36.0f;
  r->state.fm = 0.0f;
  r->state.chord = 0;
  r->strum = 0;
  for (size_t i = 0; i < rings::kMaxBlockSize; ++i) r->silence[i] = 0.0f;
  r->pend_at = r->pend_n = 0;
}

void mr_set(void* v, int model, int polyphony, float note, float structure, float brightness, float damping,
            float position) {
  RingsPart* r = static_cast<RingsPart*>(v);
  if (model < 0 || model >= MR_MODELS) model = 0;
  if (model == rings::RESONATOR_MODEL_STRING_AND_REVERB && !r->has_reverb) {
    model = rings::RESONATOR_MODEL_STRING;   // no buffer for its reverb: the string alone
  }
  if (model != r->model) {
    r->part.set_model(static_cast<rings::ResonatorModel>(model));
    r->model = model;
  }
  if (polyphony != r->part.polyphony()) r->part.set_polyphony(polyphony);
  r->state.note = note - 12.0f;          // rings: tonic + note, in semitones (cv_scaler.cc)
  r->state.chord = static_cast<int32_t>(structure * 10.0f);
  r->patch.structure = structure;
  r->patch.brightness = brightness;
  r->patch.damping = damping;
  r->patch.position = position;
}

void mr_strum(void* v) { static_cast<RingsPart*>(v)->strum = 1; }

// Part::Process steps its note filter and LFOs once a call, for kMaxBlockSize frames (24, rings.cc's block): it is
// always called with exactly that many, and a block not wholly wanted waits for the next render (128 = 5 x 24 + 8)
void mr_render(void* v, float* out, float* aux, uint32_t n) {
  RingsPart* r = static_cast<RingsPart*>(v);
  while (n) {
    if (r->pend_at == r->pend_n) {
      r->state.strum = r->strum != 0;
      r->strum = 0;
      r->part.Process(r->state, r->patch, r->silence, r->pend_out, r->pend_aux, rings::kMaxBlockSize);
      r->pend_at = 0;
      r->pend_n = rings::kMaxBlockSize;
    }
    while (n && r->pend_at < r->pend_n) {
      *out++ = r->pend_out[r->pend_at];
      *aux++ = r->pend_aux[r->pend_at++];
      --n;
    }
  }
}

const char* mr_model_name(int model) {
  return model >= 0 && model < MR_MODELS ? kModelNames[model] : "?";
}

}  // extern "C"
