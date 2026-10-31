// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1 bench: one Plaits voice (third_party/plaits, Emilie Gillet, MIT) behind bridge.h.
#include <new>
#include "bridge.h"
#include "plaits/dsp/voice.h"

namespace {

struct PlaitsVoice {
  plaits::Voice voice;
  plaits::Patch patch;
  plaits::Modulations mod;
  int gate;                             // the TRIG input as a gate: its edge starts the LPG, its length holds
                                        // the engines that sustain (six-op FM, string machine...)
  char buffer[16384];                   // the engines' shared memory (plaits.cc: shared_buffer)
};

const char* const kModelNames[MP_MODELS] = {
  "VA VCF", "PHASE DIST", "6-OP FM 1", "6-OP FM 2", "6-OP FM 3", "WAVE TERRAIN", "STRING MACH", "CHIPTUNE",
  "VA", "WAVESHAPER", "FM 2-OP", "FORMANT", "HARMONIC", "WAVETABLE", "CHORDS", "SPEECH",
  "SWARM", "NOISE", "PARTICLES", "STRING", "MODAL", "KICK", "SNARE", "HI-HAT",
};

}  // namespace

extern "C" {

uint32_t mp_size(void) { return sizeof(PlaitsVoice); }

void mp_init(void* mem) {
  PlaitsVoice* p = new (mem) PlaitsVoice();
  stmlib::BufferAllocator allocator(p->buffer, sizeof(p->buffer));
  p->voice.Init(&allocator);
  p->patch.note = 48.0f;
  p->patch.harmonics = 0.5f;
  p->patch.timbre = 0.5f;
  p->patch.morph = 0.5f;
  p->patch.frequency_modulation_amount = 0.0f;
  p->patch.timbre_modulation_amount = 0.0f;
  p->patch.morph_modulation_amount = 0.0f;
  p->patch.engine = 8;
  p->patch.decay = 0.5f;
  p->patch.lpg_colour = 0.5f;
  p->mod.engine = 0.0f;
  p->mod.note = 0.0f;
  p->mod.frequency = 0.0f;
  p->mod.harmonics = 0.0f;
  p->mod.timbre = 0.0f;
  p->mod.morph = 0.0f;
  p->mod.trigger = 0.0f;
  p->mod.level = 0.0f;
  p->mod.frequency_patched = false;
  p->mod.timbre_patched = false;
  p->mod.morph_patched = false;
  p->mod.trigger_patched = true;        // the LPG and the decay envelope follow our triggers
  p->mod.level_patched = false;
  p->gate = 0;
}

void mp_set(void* v, int model, float note, float harmonics, float timbre, float morph, float decay,
            float colour) {
  PlaitsVoice* p = static_cast<PlaitsVoice*>(v);
  p->patch.engine = model < 0 ? 0 : model >= MP_MODELS ? MP_MODELS - 1 : model;
  p->patch.note = note;
  p->patch.harmonics = harmonics;
  p->patch.timbre = timbre;
  p->patch.morph = morph;
  p->patch.decay = decay;
  p->patch.lpg_colour = colour;
}

void mp_gate(void* v, int high) { static_cast<PlaitsVoice*>(v)->gate = high; }

void mp_render(void* v, float* out, float* aux, uint32_t n) {
  PlaitsVoice* p = static_cast<PlaitsVoice*>(v);
  plaits::Voice::Frame frames[plaits::kMaxBlockSize];
  while (n) {
    uint32_t size = n < plaits::kMaxBlockSize ? n : plaits::kMaxBlockSize;
    p->mod.trigger = p->gate ? 1.0f : 0.0f;   // the voice delays it by 1 ms and finds its edges
    p->voice.Render(p->patch, p->mod, frames, size);
    for (uint32_t i = 0; i < size; ++i) {
      out[i] = static_cast<float>(frames[i].out) * (1.0f / 32768.0f);
      aux[i] = static_cast<float>(frames[i].aux) * (1.0f / 32768.0f);
    }
    out += size;
    aux += size;
    n -= size;
  }
}

const char* mp_model_name(int model) {
  return model >= 0 && model < MP_MODELS ? kModelNames[model] : "?";
}

}  // extern "C"
