// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1: one Plaits voice (third_party/plaits, Emilie Gillet, MIT) behind bridge.h.
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
  float target[3];                      // HARMONICS, TIMBRE, MORPH as set: the patch glides to them, a quarter of the
                                        // way each Render call (12 frames), so a control-rate change makes no steps
  float pend_out[plaits::kBlockSize];   // a block rendered and not yet given out (see mp_render)
  float pend_aux[plaits::kBlockSize];
  uint32_t pend_at, pend_n;
  float note;                           // the note as set: the patch takes it at once, or with the trig (below)
  int note_wait;                        // Render calls until the voice sees a rise of the gate
};
// a track's voice's memory, lent to OTIS (its 32 cycles, SPACE's line) and to ROBOVOX: at least that much (0.16.17:
// Plaits' speech gone, the voice itself smaller)
const uint32_t kLent = 16384 + 2048;

const char* const kModelNames[MP_MODELS] = {
  "VA VCF", "PHASE DIST", "6-OP FM 1", "6-OP FM 2", "6-OP FM 3", "WAVE TERRAIN", "STRING MACH", "CHIPTUNE",
  "VA", "WAVESHAPER", "FM 2-OP", "FORMANT", "HARMONIC", "WAVETABLE", "CHORDS", "SPEECH",
  "SWARM", "NOISE", "PARTICLES", "STRING", "MODAL", "KICK", "SNARE", "HI-HAT",
};

}  // namespace

extern "C" {

uint32_t mp_size(void) { return sizeof(PlaitsVoice) > kLent ? sizeof(PlaitsVoice) : kLent; }

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
  p->note = 48.0f;
  p->note_wait = 0;
  p->pend_at = p->pend_n = 0;
  p->target[0] = p->target[1] = p->target[2] = 0.5f;
}

void mp_set(void* v, int model, float note, float harmonics, float timbre, float morph, float decay,
            float colour) {
  PlaitsVoice* p = static_cast<PlaitsVoice*>(v);
  p->patch.engine = model < 0 ? 0 : model >= MP_MODELS ? MP_MODELS - 1 : model;
  p->note = note;
  p->target[0] = harmonics;
  p->target[1] = timbre;
  p->target[2] = morph;
  p->patch.decay = decay;
  p->patch.lpg_colour = colour;
}

// The voice sees its trigger 4 Render calls late (voice.cc: kTriggerDelay, for the CV that lags the gate on the
// module), its note at once: a trig's new note would bend the last note's tail for a millisecond before the new one
// starts, a crackle on the voices that ring (six-op FM, chords, string machine...). A rise's note waits for it.
void mp_gate(void* v, int high) {
  PlaitsVoice* p = static_cast<PlaitsVoice*>(v);
  if (high && !p->gate) p->note_wait = plaits::kTriggerDelay;
  p->gate = high;
}

void mp_amounts(void* v, float fm, float timbre, float morph) {
  PlaitsVoice* p = static_cast<PlaitsVoice*>(v);
  p->patch.frequency_modulation_amount = fm;
  p->patch.timbre_modulation_amount = timbre;
  p->patch.morph_modulation_amount = morph;
}

// Voice::Render times its envelopes, its LPG and the trigger's delay per call as kBlockSize frames (12, plaits.cc's
// block): it is always called with exactly that many, and a block not wholly wanted waits for the next render (128 is
// not a multiple of 12)
void mp_render(void* v, float* out, float* aux, uint32_t n) {
  PlaitsVoice* p = static_cast<PlaitsVoice*>(v);
  plaits::Voice::Frame frames[plaits::kBlockSize];
  while (n) {
    if (p->pend_at == p->pend_n) {
      p->mod.trigger = p->gate ? 1.0f : 0.0f;   // the voice delays it by 1 ms and finds its edges
      if (p->note_wait == 0 || --p->note_wait == 0) p->patch.note = p->note;
      p->patch.harmonics += 0.25f * (p->target[0] - p->patch.harmonics);
      p->patch.timbre += 0.25f * (p->target[1] - p->patch.timbre);
      p->patch.morph += 0.25f * (p->target[2] - p->patch.morph);
      p->voice.Render(p->patch, p->mod, frames, plaits::kBlockSize);
      for (uint32_t i = 0; i < plaits::kBlockSize; ++i) {
        p->pend_out[i] = static_cast<float>(frames[i].out) * (1.0f / 32768.0f);
        p->pend_aux[i] = static_cast<float>(frames[i].aux) * (1.0f / 32768.0f);
      }
      p->pend_at = 0;
      p->pend_n = plaits::kBlockSize;
    }
    while (n && p->pend_at < p->pend_n) {
      *out++ = p->pend_out[p->pend_at];
      *aux++ = p->pend_aux[p->pend_at++];
      --n;
    }
  }
}

const char* mp_model_name(int model) {
  return model >= 0 && model < MP_MODELS ? kModelNames[model] : "?";
}

}  // extern "C"
