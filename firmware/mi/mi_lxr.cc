// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Yoann. The parameters' conversions (apply below) are those of the LXR firmware's MidiParser.c,
// Copyright (C) 2012-2026 Erica Synths, and its INIT kit's values (initKit.c), GPL-3.0-only.
// MIFM1: the LXR-02's drum voices (third_party/lxr: Erica Synths' LXR firmware, GPL-3.0-only with the terms of
// its NOTICE.md) behind bridge.h, one of each a track: the drum and the snare (0.16.43: the cymbal and the hi-hat
// gone, their room the CHOIR's). A track playing one of them sets its parameters as the
// LXR-02's MIDI parser does (the values of a modulation: 0..1, the MIDI value v x 127) and plays it as its mixer
// does: in blocks of 64 frames, the filter's frequency, then the envelopes (calcAsync), then the samples (calcSync).
// A trig starts a new block where it lands: the rest of the block under way is dropped (the voice is retriggered
// there, as the LXR-02 retriggers it between two blocks).
#include "bridge.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/MIDI/valueShaper.h"

namespace {

enum { DRUM, SNARE };

// the parameters, by what they do (MidiMessages.h: a voice's own CC numbers)
enum Param {
  P_WAVE, P_WAVE2, P_WAVE3,              // the oscillator's waveform, the modulators' (OSC_WAVE_DRUM1, MOD_WAVE...)
  P_COARSE, P_FINE,                      // F_OSCx_COARSE, _FINE (the note follows: see set_pitch)
  P_MOD1_F, P_MOD2_F,                    // the modulators' coarse (FMDTN1; CYM_MOD_OSC_F1, MOD_OSC_F1...)
  P_FM1, P_FM2,                          // FMAMNT1; CYM_MOD_OSC_GAIN1, MOD_OSC_GAIN1...
  P_MIXMODE,                             // CC2_MIX_MOD (the drum: 1 the two oscillators mixed, 0 one modulating)
  P_NOISE_MIX, P_NOISE_F,                // SNARE_MIX, SNARE_NOISE_F
  P_PITCHD, P_MODAMNT, P_PSLOPE,         // the pitch envelope: PITCHDx, MODAMNTx, PITCH_SLOPEx
  P_ATTACK, P_DECAY, P_DECAY_OPEN, P_SLOPE, P_REPEAT,   // the amplitude's: VELOAx, VELODx, VELOD6_OPEN, slopes,
                                         // REPEAT1 (the snare's), CYM_REPEAT
  P_FILTER, P_RESO, P_FTYPE, P_FDRIVE,   // FILTER_FREQ_DRUMx..., RESO..., CC2_FILTER_TYPE, CC2_FILTER_DRIVE
  P_DIST,                                // OSCx_DIST, SNARE_DISTORTION...
  P_TWAVE, P_TVOL, P_TFREQ,              // the transient: CC2_TRANSx_WAVE, _VOL, _FREQ
  P_VOL, P_VOLMOD, P_PWM,                // VOLx, CC2_VOLUME_MOD_ON_OFFx, CC2_PWM_x
  P_N
};

// MidiParser.c's
inline float calcPitchModAmount(uint8_t data2) {
  const float val = data2 / 127.f;
  return val * val * PITCH_AMOUNT_FACTOR;
}

uint8_t wave_of(uint8_t itg, float flt, int is_float) {   // (MidiParser.c's OSC_WAVE_DRUM1 and the others)
  if (!is_float) return itg;
  uint8_t wave = NUM_OSC_WAVEFORMS * flt;
  return wave >= NUM_OSC_WAVEFORMS ? NUM_OSC_WAVEFORMS - 1 : wave;
}

uint8_t type_of(uint8_t itg, float flt, int is_float) {   // (CC2_FILTER_TYPE_1...)
  if (!is_float) return itg + 1;
  uint8_t type = FILTER_NAIVE_2_POLE * flt + 1;
  return type >= FILTER_OFF ? FILTER_OFF - 1 : type;
}

uint8_t twave_of(uint8_t itg, float flt, int is_float) {  // (CC2_TRANS1_WAVE...)
  if (!is_float) return itg;
  uint8_t wave = NUM_TRANSIENTS * flt;
  return wave >= NUM_TRANSIENTS ? NUM_TRANSIENTS - 1 : wave;
}

void coarse(OscInfo* o, uint8_t itg) {
  o->midiFreq &= 0x00ff;
  o->midiFreq |= itg << 8;
  osc_recalcFreq(o);
}

void fine(OscInfo* o, uint8_t itg) {
  o->midiFreq &= 0xff00;
  o->midiFreq |= itg;
  osc_recalcFreq(o);
}

// one parameter of voice v (the track) of a model, as midiParser_ccHandler sets it: from a MIDI value (itg, flt
// itg / 127) or a modulation (is_float: flt, itg flt x 127)
void apply(int model, uint8_t v, int p, uint8_t itg, float flt, int is_float) {
  switch (model) {
    case DRUM: {
      DrumVoice& d = voiceArray[v];
      switch (p) {
        case P_WAVE: d.osc.waveform = wave_of(itg, flt, is_float); break;
        case P_WAVE2: d.modOsc.waveform = wave_of(itg, flt, is_float); break;
        case P_COARSE: coarse(&d.osc, itg); break;
        case P_FINE: fine(&d.osc, itg); break;
        case P_MOD1_F: coarse(&d.modOsc, itg); break;
        case P_FM1: d.fmModAmount = flt; break;
        case P_MIXMODE: d.mixOscs = itg != 0; break;
        case P_PITCHD: DecayEg_setDecay(&d.oscPitchEg, itg); break;
        case P_MODAMNT: d.egPitchModAmount = calcPitchModAmount(itg); break;
        case P_PSLOPE: DecayEg_setSlope(&d.oscPitchEg, itg); break;
        case P_ATTACK: slopeEg2_setAttack(&d.oscVolEg, itg, AMP_EG_SYNC); break;
        case P_DECAY: slopeEg2_setDecay(&d.oscVolEg, itg, AMP_EG_SYNC); break;
        case P_SLOPE: slopeEg2_setSlope(&d.oscVolEg, itg); break;
        case P_FILTER: SVF_directSetFilterValue(&d.filter, valueShaperF2F(flt, FILTER_SHAPER)); break;
        case P_RESO: SVF_setReso(&d.filter, flt); break;
        case P_FTYPE: d.filterType = type_of(itg, flt, is_float); break;
        case P_FDRIVE: SVF_setDrive(&d.filter, flt); break;
        case P_DIST: setDistortionShape(&d.distortion, flt); break;
        case P_TWAVE: transient_setWaveform(&d.transGen, twave_of(itg, flt, is_float)); break;
        case P_TVOL: d.transGen.volume = flt; break;
        case P_TFREQ: d.transGen.pitch = 1.f + ((itg / 33.9f) - 0.75f); break;
        case P_VOL: d.vol = flt; break;
        case P_VOLMOD: d.volumeMod = itg != 0; break;
        case P_PWM: d.pwm = flt; break;
      }
      break;
    }
    case SNARE: {
      SnareVoice& s = snareVoice[v];
      switch (p) {
        case P_WAVE: s.osc.waveform = wave_of(itg, flt, is_float); break;
        case P_COARSE: coarse(&s.osc, itg); break;
        case P_FINE: fine(&s.osc, itg); break;
        case P_NOISE_F: s.noiseOsc.freq = flt * 22000; break;
        case P_NOISE_MIX: s.mix = flt; break;
        case P_PITCHD: DecayEg_setDecay(&s.oscPitchEg, itg); break;
        case P_MODAMNT: s.egPitchModAmount = calcPitchModAmount(itg); break;
        case P_PSLOPE: DecayEg_setSlope(&s.oscPitchEg, itg); break;
        case P_ATTACK: slopeEg2_setAttack(&s.oscVolEg, itg, false); break;
        case P_DECAY: slopeEg2_setDecay(&s.oscVolEg, itg, false); break;
        case P_SLOPE: slopeEg2_setSlope(&s.oscVolEg, itg); break;
        case P_REPEAT: s.oscVolEg.repeat = itg; break;
        case P_FILTER: SVF_directSetFilterValue(&s.filter, valueShaperF2F(flt, FILTER_SHAPER)); break;
        case P_RESO: SVF_setReso(&s.filter, flt); break;
        case P_FTYPE: s.filterType = type_of(itg, flt, is_float); break;
        case P_FDRIVE: SVF_setDrive(&s.filter, flt); break;
        case P_DIST: setDistortionShape(&s.distortion, flt); break;
        case P_TWAVE: transient_setWaveform(&s.transGen, twave_of(itg, flt, is_float)); break;
        case P_TVOL: s.transGen.volume = flt; break;
        case P_TFREQ: s.transGen.pitch = 1.f + ((itg / 33.9f) - 0.75f); break;
        case P_VOL: s.vol = flt; break;
        case P_VOLMOD: s.volumeMod = itg != 0; break;
        case P_PWM: s.pwm = flt; break;
      }
      break;
    }
  }
}

struct Kit {
  int8_t p;
  uint8_t v;
};

// each voice's sound as the LXR firmware's INIT kit has it (initKit.c), then MIFM1's changes (the knobs' own
// values: lx_defaults): a punchier kick (a click)
const Kit kKits[LX_MODELS][26] = {
  {{P_WAVE, 0}, {P_WAVE2, 0}, {P_COARSE, 31}, {P_FINE, 63}, {P_MOD1_F, 0}, {P_FM1, 0}, {P_MIXMODE, 0},
   {P_PITCHD, 26}, {P_MODAMNT, 44}, {P_PSLOPE, 22}, {P_ATTACK, 0}, {P_DECAY, 21}, {P_SLOPE, 46},
   {P_FILTER, 127}, {P_RESO, 0}, {P_FTYPE, 0}, {P_FDRIVE, 0}, {P_DIST, 63}, {P_TWAVE, 0}, {P_TVOL, 0},
   {P_TFREQ, 0}, {P_VOL, 127}, {P_VOLMOD, 0}, {P_PWM, 0}, {-1, 0}},
  {{P_WAVE, 1}, {P_COARSE, 45}, {P_FINE, 64}, {P_NOISE_F, 127}, {P_NOISE_MIX, 61}, {P_PITCHD, 14},
   {P_MODAMNT, 81}, {P_PSLOPE, 15}, {P_ATTACK, 0}, {P_DECAY, 10}, {P_SLOPE, 10}, {P_REPEAT, 0}, {P_FILTER, 127},
   {P_RESO, 0}, {P_FTYPE, 0}, {P_FDRIVE, 0}, {P_DIST, 63}, {P_TWAVE, 0}, {P_TVOL, 0}, {P_TFREQ, 0}, {P_VOL, 127},
   {P_VOLMOD, 0}, {P_PWM, 0}, {-1, 0}},
};

// the knobs (bridge.h LX_KNOBS): a model's parameters on its track's pages, their names, their values as its kit
// has them (the knob's 0..1 is a modulation's: v x 127), and the oscillators the note moves
struct Knob {
  int8_t p;
  const char* name;
  uint8_t def;
};
const Knob kKnobs[LX_MODELS][LX_KNOBS] = {
  {{P_FM1, "FM", 0}, {P_MOD1_F, "ModFrq", 60}, {P_PITCHD, "P.Dec", 26}, {P_DECAY, "Decay", 21},
   {P_WAVE, "Wave", 0}, {P_TVOL, "Click", 32}, {P_DIST, "Drive", 63}, {P_MODAMNT, "P.Amt", 44}},
  {{P_NOISE_MIX, "Noise", 61}, {P_NOISE_F, "NoisF", 127}, {P_PITCHD, "P.Dec", 14}, {P_DECAY, "Decay", 12},
   {P_FILTER, "Filter", 127}, {P_RESO, "Reso", 0}, {P_DIST, "Drive", 63}, {P_MODAMNT, "P.Amt", 81}},
};

const char* const kWaves[NUM_OSC_WAVEFORMS] = {"Sine", "Tri", "Saw", "Rect", "Noise", "PWM"};

const int kBlock = OUTPUT_DMA_SIZE;     // 64
const int kHome = 48;                   // the note that plays a kit's pitch (as the LXR-02's SEQ_DEFAULT_NOTE):
                                        // MIFM1's C3, a step's at octave 0

struct Track {
  int model;                            // -1: none yet
  int16_t buf[OUTPUT_DMA_SIZE];         // the block under way, and where in it
  int pos;
  int trig;                             // a trig at the next frame
  float knob[LX_KNOBS];                 // the knobs as last applied
  int note;                             // the note as last applied (its semitones, its fine tune)
  int fine;
};
Track tracks[LXR_VOICES];

// a knob's MIDI value, v x 127 as the LXR-02 takes a modulation's (a hair over, so that a knob set to n / 127 is n)
inline int itg_of(float u) { return static_cast<int>(u * 127.0f + 1e-4f); }

int note_moves(int model, int p) {      // the oscillators the played note moves (Drum_trigger, Snare_trigger)
  return p == P_COARSE || (p == P_MOD1_F && model != SNARE);
}

int base_of(const Track& tr, int m, int p) {   // a pitch's own value: its knob's, or the kit's
  for (int k = 0; k < LX_KNOBS; ++k)
    if (kKnobs[m][k].p == p) return itg_of(tr.knob[k]);
  for (int i = 0; kKits[m][i].p >= 0; ++i)
    if (kKits[m][i].p == p) return kKits[m][i].v;
  return 0;
}

void set_pitch(Track& tr, int t, int semis) {   // the note: its semitones over the coarse of every oscillator it moves
  int m = tr.model;
  for (int p = P_COARSE; p <= P_MOD2_F; ++p) {
    if (!note_moves(m, p)) continue;
    int c = base_of(tr, m, p) + semis;
    apply(m, static_cast<uint8_t>(t), p, static_cast<uint8_t>(c < 0 ? 0 : c > 127 ? 127 : c), 0.0f, 0);
  }
}

void start(Track& tr, int t, int model) {   // a voice of that model on the track, its kit loaded
  tr.model = model;
  uint8_t v = static_cast<uint8_t>(t);
  switch (model) {
    case DRUM:                          // (initDrumVoice set all four up at lx_init: this one silent again)
      slopeEg2_init(&voiceArray[v].oscVolEg);
      voiceArray[v].ampFilterInput = voiceArray[v].lastGain = 0;
      SVF_reset(&voiceArray[v].filter);
      break;
    default: Snare_init(v); break;
  }
  for (int i = 0; kKits[model][i].p >= 0; ++i)
    apply(model, v, kKits[model][i].p, kKits[model][i].v, kKits[model][i].v / 127.f, 0);
  for (int k = 0; k < LX_KNOBS; ++k) tr.knob[k] = -1.0f;   // (applied at the next lx_set)
  tr.note = 1000;
  tr.fine = -1;
  tr.pos = kBlock;
  tr.trig = 0;
}

void block(Track& tr, int t) {          // the next 64 frames: the mixer's order (mixer_calcNextSampleBlock)
  uint8_t v = static_cast<uint8_t>(t);
  switch (tr.model) {
    case DRUM:
      if (tr.trig) Drum_trigger(v, 127, SEQ_DEFAULT_NOTE);
      SVF_recalcFreq(&voiceArray[v].filter);
      calcDrumVoiceAsync(v);
      calcDrumVoiceSyncBlock(v, tr.buf, kBlock);
      break;
    default:
      if (tr.trig) Snare_trigger(v, 127, SEQ_DEFAULT_NOTE);
      SVF_recalcFreq(&snareVoice[v].filter);
      Snare_calcAsync(v);
      Snare_calcSyncBlock(v, tr.buf, kBlock);
      break;
  }
  tr.trig = 0;
  tr.pos = 0;
}

}  // namespace

extern "C" {

void lx_init(void) {
  initDrumVoice();
  for (int t = 0; t < LXR_VOICES; ++t) {
    // each oscillator's noise its own (the LXR-02 draws all of them from the STM32's generator)
    uint32_t seed = 0x6C078965u * static_cast<uint32_t>(t + 1);
    voiceArray[t].osc.rng = seed ^ 1u;
    voiceArray[t].modOsc.rng = seed ^ 2u;
    snareVoice[t].osc.rng = seed ^ 3u;
    snareVoice[t].noiseOsc.rng = seed ^ 4u;
    tracks[t].model = -1;
    tracks[t].pos = kBlock;
    tracks[t].trig = 0;
  }
}

void lx_set(int t, int model, float note, const float* k) {
  if (t < 0 || t >= LXR_VOICES || model < 0 || model >= LX_MODELS) return;
  Track& tr = tracks[t];
  if (tr.model != model) start(tr, t, model);
  int pitch_dirty = 0;
  for (int i = 0; i < LX_KNOBS; ++i) {
    float u = k[i] < 0.0f ? 0.0f : k[i] > 1.0f ? 1.0f : k[i];
    if (u == tr.knob[i]) continue;
    tr.knob[i] = u;
    int p = kKnobs[model][i].p;
    if (note_moves(model, p)) pitch_dirty = 1;
    else apply(model, static_cast<uint8_t>(t), p, static_cast<uint8_t>(itg_of(u)), u, 1);
  }
  // the note: its semitones over the oscillators' coarse (as a MIDI note over SEQ_DEFAULT_NOTE moves them), the rest
  // of it on the main oscillator's fine tune (+/- 50 cents, the LXR's own curve)
  float n = note < 0.0f ? 0.0f : note > 127.0f ? 127.0f : note;
  int semis = static_cast<int>(n + 0.5f);
  float frac = n - static_cast<float>(semis);   // -0.5..0.5
  int f = static_cast<int>(63.5f + frac * (127.0f * 0.0578f / (SEMITONE_UP - 1.0f)) + 0.5f);
  f = f < 0 ? 0 : f > 127 ? 127 : f;
  if (pitch_dirty || semis - kHome != tr.note) {
    tr.note = semis - kHome;
    set_pitch(tr, t, tr.note);
  }
  if (f != tr.fine) {
    tr.fine = f;
    apply(model, static_cast<uint8_t>(t), P_FINE, static_cast<uint8_t>(f), f / 127.f, 0);
  }
}

void lx_trigger(int t) {
  if (t < 0 || t >= LXR_VOICES || tracks[t].model < 0) return;
  tracks[t].trig = 1;
  tracks[t].pos = kBlock;               // (the block under way dropped: the next frame starts the trig's)
}

void lx_render(int t, float* out, uint32_t n) {
  if (t < 0 || t >= LXR_VOICES || tracks[t].model < 0) {
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    return;
  }
  Track& tr = tracks[t];
  uint32_t done = 0;
  while (done < n) {
    if (tr.pos >= kBlock) block(tr, t);
    uint32_t c = static_cast<uint32_t>(kBlock - tr.pos);
    if (c > n - done) c = n - done;
    const int16_t* s = &tr.buf[tr.pos];
    for (uint32_t i = 0; i < c; ++i) out[done + i] = static_cast<float>(s[i]) * (1.0f / 32768.0f);
    tr.pos += static_cast<int>(c);
    done += c;
  }
}

const char* lx_knob_name(int model, int k) {
  if (model < 0 || model >= LX_MODELS || k < 0 || k >= LX_KNOBS) return "";
  return kKnobs[model][k].name;
}

MI_COLD void lx_knob_text(int model, int k, float v, char* b, int n) {
  if (n <= 0) return;
  b[0] = 0;
  if (model < 0 || model >= LX_MODELS || k < 0 || k >= LX_KNOBS) return;
  float u = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
  const char* s = 0;
  char num[4];
  if (kKnobs[model][k].p == P_WAVE) {
    s = kWaves[wave_of(0, u, 1)];
  } else {                              // 0..127, as the LXR-02 shows it
    int x = itg_of(u), i = 0;
    if (x >= 100) num[i++] = static_cast<char>('0' + x / 100);
    if (x >= 10) num[i++] = static_cast<char>('0' + x / 10 % 10);
    num[i++] = static_cast<char>('0' + x % 10);
    num[i] = 0;
    s = num;
  }
  int i = 0;
  while (s[i] && i < n - 1) {
    b[i] = s[i];
    ++i;
  }
  b[i] = 0;
}

void lx_defaults(int model, float* k) {
  if (model < 0 || model >= LX_MODELS) return;
  for (int i = 0; i < LX_KNOBS; ++i) k[i] = kKnobs[model][i].def / 127.0f;
}

int lx_knob_steps(int model, int k) {
  if (model < 0 || model >= LX_MODELS || k < 0 || k >= LX_KNOBS) return 128;
  return kKnobs[model][k].p == P_WAVE ? NUM_OSC_WAVEFORMS : 128;
}

}  // extern "C"
