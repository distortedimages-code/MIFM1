// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's sound (mi/engine.h): four Plaits voices played by a step sequencer (or the keys), three LFOs as modulators
// (0.3 had two Tides), a matrix of cables, three effects (a delay, Clouds' reverb, Warps) with their own steps, a
// mixer. Only bridge.h's C functions reach the modules: no module's header meets another's here.
//
// One audio block (128 frames, 2.9 ms) is one control step:
//   1. the LFOs move a block (on the tempo, or free, or from a track's trigs), through their slew;
//   2. every parameter is its knob, plus the random dose (OCT- and OCT+ held, SELECT), plus its cables;
//   3. the trigs, frame by frame (8 a block), on the tempo's sixteenths (swung): each lane's step; a track's trig
//      takes its locks and its randoms (over the knobs, until the next trig), its probability, its ratchets, its
//      length; an effect's opens it (its locks and randoms the same); the keyboard plays over, a new key a new trig;
//      a note is put in the global scale;
//   4. each voice plays the block, cut where its gate changes (a rising gate first takes its trig's note); the voices
//      and Warps shared between the two cores by what each costs (measured, block by block);
//   5. the mixer: level and pan per track (the trig's), the sends into the delay and the reverb (each opened by its
//      lane), Warps in its track's place (crossfaded with it by its lane), a soft limit on the sum; the gains glide,
//      sample by sample (a gain that jumps at a block's edge clicks).
#include <stdint.h>
#include <stddef.h>
extern "C" {
#include "plat.h"
#include "fastmath.h"
}
#include "bridge.h"
#include "engine.h"
#include <new>
#include "stmlib/dsp/filter.h"

#ifdef PLAT_HOST
float eng_test_prebus[2][128];           // (the tests: the master before its bus, what the USB channels sum to)
#endif

#ifdef PLAT_WEB
// The browser emulator (fm1-base's tools/build.py --web, tools/web): the engine's clock is the FM-1's, modelled. A
// browser cannot time the engine (its clock moves a render quantum at a time, and its speed is not the FM-1's); here
// each part, as it runs, moves the clock of the core it runs on by what it costs the FM-1 (mi/web_costs.h, counted
// by tools/web/calibrate.py), and the second core is simulated on its own clock: a job starts when it is asked and
// the core is free, core 0 waits for it when it must. The machine page, the header's load, the filters' CPU and the
// split between the cores then read the FM-1's times, as near as the table has them.
#include "web_costs.h"
namespace {
uint32_t web_clk[2], web_c1_end;
int web_core, web_c1_on;
void web_spend(float us) { web_clk[web_core] += static_cast<uint32_t>(us * static_cast<float>(PLAT_TICKS_PER_US)); }
int web_later(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) > 0; }   // (the clock wraps: 179 s)
}  // namespace
extern "C" {
static uint32_t web_ticks24(void) { return web_clk[web_core]; }
int plat_cpu2_set(int on) { web_c1_on = on != 0; return 0; }
int plat_cpu2_on(void) { return web_c1_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg) {
  if (!web_c1_on) return 0;
  web_clk[1] = web_later(web_c1_end, web_clk[0]) ? web_c1_end : web_clk[0];
  web_core = 1;
  fn(arg);
  web_core = 0;
  web_c1_end = web_clk[1];
  return 1;
}
int plat_cpu2_wait(void) {
  if (web_later(web_c1_end, web_clk[0])) web_clk[0] = web_c1_end;
  return 0;
}
void plat_cpu2_sync(void) {}
int plat_cpu2_busy(void) { return web_later(web_c1_end, web_clk[0]); }
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 0; }
uint32_t plat_cpu2_stack(void) { return 0; }
}
#define plat_ticks24 web_ticks24
#define WEB_SPEND(us) web_spend(us)
#else
#define WEB_SPEND(us) ((void)0)
#endif

namespace {

// ---------------------------------------------------------------- parameters ---
const char* const kLfoWaves[LW_N] = {"Sine", "Triangle", "Saw", "Ramp", "Square", "S&H", "Smooth"};
const char* const kLfoModes[6] = {"Free", "Sync", "T1", "T2", "T3", "T4"};
const char* const kOnOff[2] = {"Off", "On"};
const char* const kScaleNames[15] = {"Off", "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
                                     "Locrian", "Harm. minor", "Mel. minor", "Maj. penta", "Min. penta", "Blues",
                                     "Whole tone", "Hirajoshi"};
const uint16_t kScaleMasks[15] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B, 0x9AD, 0xAAD, 0x295, 0x4A9,
                                  0x4E9, 0x555, 0x18D};   // bit i: i semitones over the root
const char* const kNoteName[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kArpRates[6] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4"};
const float kArpMult[6] = {0.5f, 2.0f / 3.0f, 1.0f, 4.0f / 3.0f, 2.0f, 4.0f};   // in sixteenths
const char* const kArpDir[4] = {"Forward", "Backward", "Ping-pong", "Random"};
const char* const kWarpsCarrier[8] = {"Off", "T1", "T2", "T3", "T4", "Osc 1", "Osc 2", "Osc 3"};
const char* const kTrackNames[4] = {"T1", "T2", "T3", "T4"};
const char* const kWarpsMode[3] = {"Warps", "+ Dry", "Freq shift"};
// the delay's times: divisions of the tempo, shortest first
const int kDivs = 13;
const char* const kDivNames[kDivs] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D",
                                      "1/2", "1/2D", "1/1"};
const float kDivMult[kDivs] = {0.5f, 2.0f / 3.0f, 1.0f, 4.0f / 3.0f, 1.5f, 2.0f, 8.0f / 3.0f, 3.0f, 4.0f, 6.0f, 8.0f,
                               12.0f, 16.0f};   // in sixteenths
const char* const kDelayMode[2] = {"Mono", "Ping-pong"};
const char* const kBendShapes[BS_N] = {"Off", "Rect", "Down", "Down 3", "Fold", "Swap"};
const char* const kBendMasks[BM_N] = {"Clean", "Lo cut", "Lo inv", "Odd cut", "Even inv", "Holes", "Mid inv",
                                      "Hi inv", "Sign inv", "Sign cut", "3 bits", "Comb"};
// the masks on the 8-bit word (bit 0 the least): its bits inverted, its bits muted
const uint8_t kMaskInv[BM_N] = {0x00, 0x00, 0x03, 0x00, 0x55, 0x00, 0x18, 0x40, 0x80, 0x00, 0x00, 0x00};
const uint8_t kMaskCut[BM_N] = {0x00, 0x03, 0x00, 0x2A, 0x00, 0x18, 0x00, 0x00, 0x00, 0x80, 0x1F, 0x55};
const char* const kGlitchSizes[GL_N] = {"1/64", "1/32", "1/16", "1/8"};
const float kClockRatio[10] = {48.0f, 32.0f, 24.0f, 16.0f, 12.0f, 8.0f, 6.0f, 4.0f, 3.0f, 2.0f};   // CLOCK: x the note
const char* const kFiltTypes[3] = {"LP", "BP", "HP"};   // (VOX: its voices, eng_param_text)

// the knobs' names: the modules' own, short (a word, 52 px at most on the screen: the knobs apart)
const eng_pdef_t kPages[NPT][4] = {
  {{"Freq", K_BIP, 0, 0, 0}, {"Harmo", K_UNI, 0, 0, 0}, {"Timbre", K_UNI, 0, 0, 0}, {"Morph", K_UNI, 0, 0, 0}},
  {{"Decay", K_UNI, 0, 0, 0}, {"Colour", K_UNI, 0, 0, 0}, {"T.Env", K_BIP, 0, 0, 0}, {"M.Env", K_BIP, 0, 0, 0}},
  {{"P.Env", K_BIP, 0, 0, 0}, {"Gate", K_UNI, 0, 0, 0}, {"Transp", K_INT, -12, 12, 0}, {"Octave", K_INT, -3, 3, 0}},
  {{"Wave", K_ENUM, 0, LW_N - 1, kLfoWaves}, {"Rate", K_UNI, 0, 0, 0}, {"Mode", K_ENUM, 0, 5, kLfoModes},
   {"Slew", K_UNI, 0, 0, 0}},
  {{"Level", K_UNI, 0, 0, 0}, {"Level", K_UNI, 0, 0, 0}, {"Level", K_UNI, 0, 0, 0}, {"Level", K_UNI, 0, 0, 0}},
  {{"Pan", K_BIP, 0, 0, 0}, {"Pan", K_BIP, 0, 0, 0}, {"Pan", K_BIP, 0, 0, 0}, {"Pan", K_BIP, 0, 0, 0}},
  {{"Time", K_UNI, 0, 0, 0}, {"Damp", K_UNI, 0, 0, 0}, {"Diffuse", K_UNI, 0, 0, 0}, {"Return", K_UNI, 0, 0, 0}},
  {{"Reverb", K_UNI, 0, 0, 0}, {"Reverb", K_UNI, 0, 0, 0}, {"Reverb", K_UNI, 0, 0, 0}, {"Reverb", K_UNI, 0, 0, 0}},
  {{"Time", K_ENUM, 0, kDivs - 1, kDivNames}, {"Fdbk", K_UNI, 0, 0, 0}, {"Filter", K_BIP, 0, 0, 0},
   {"Drive", K_UNI, 0, 0, 0}},
  {{"Reso", K_UNI, 0, 0, 0}, {"Mode", K_ENUM, 0, 1, kDelayMode}, {"Mod", K_UNI, 0, 0, 0}, {"Return", K_UNI, 0, 0, 0}},
  {{"Delay", K_UNI, 0, 0, 0}, {"Delay", K_UNI, 0, 0, 0}, {"Delay", K_UNI, 0, 0, 0}, {"Delay", K_UNI, 0, 0, 0}},
  {{"Note", K_INT, -24, 24, 0}, {"Prob", K_UNI, 0, 0, 0}, {"Ratch", K_INT, 1, 8, 0}, {"Length", K_INT, 1, 64, 0}},
  {{"Decay", K_UNI, 0, 0, 0}, {"Prob", K_UNI, 0, 0, 0}, {"Depth", K_UNI, 0, 0, 0}, {"Length", K_INT, 1, 64, 0}},
  {{"Tempo", K_UNI, 0, 0, 0}, {"Scale", K_ENUM, 0, 14, kScaleNames}, {"Root", K_ENUM, 0, 11, kNoteName},
   {"Swing", K_UNI, 0, 0, 0}},
  {{"Rate", K_ENUM, 0, 5, kArpRates}, {"Gate", K_UNI, 0, 0, 0}, {"Dir", K_ENUM, 0, 3, kArpDir},
   {"Transp", K_INT, -24, 24, 0}},
  {{"Carrier", K_ENUM, 0, 7, kWarpsCarrier}, {"Mod", K_ENUM, 0, 3, kTrackNames}, {"Algo", K_UNI, 0, 0, 0},
   {"Mix", K_UNI, 0, 0, 0}},
  {{"Timbre", K_UNI, 0, 0, 0}, {"Level 1", K_UNI, 0, 0, 0}, {"Level 2", K_UNI, 0, 0, 0},
   {"Mode", K_ENUM, 0, 2, kWarpsMode}},
  {{"Decay", K_UNI, 0, 0, 0}, {"Duck", K_UNI, 0, 0, 0}, {"Duck", K_UNI, 0, 0, 0}, {"Duck", K_UNI, 0, 0, 0}},
  {{"Thresh", K_UNI, 0, 0, 0}, {"Ratio", K_UNI, 0, 0, 0}, {"Release", K_UNI, 0, 0, 0}, {"Gain", K_BIP, 0, 0, 0}},
  {{"Depth", K_UNI, 0, 0, 0}, {"Time", K_UNI, 0, 0, 0}, {"Drive", K_BIP, 0, 0, 0}, {"Level", K_BIP, 0, 0, 0}},
  {{"Filter", K_BIP, 0, 0, 0}, {"Filter", K_BIP, 0, 0, 0}, {"Filter", K_BIP, 0, 0, 0}, {"Filter", K_BIP, 0, 0, 0}},
  {{"Clock", K_UNI, 0, 0, 0}, {"Bits", K_UNI, 0, 0, 0}, {"Mask", K_ENUM, 0, BM_N - 1, kBendMasks},
   {"Shape", K_ENUM, 0, BS_N - 1, kBendShapes}},
  {{"Fills", K_INT, 0, 64, 0}, {"Rotate", K_INT, 0, 63, 0}, {"Length", K_INT, 1, 64, 0}, {"Prob", K_UNI, 0, 0, 0}},
  {{"Cutoff", K_UNI, 0, 0, 0}, {"Reso", K_UNI, 0, 0, 0}, {"Env", K_BIP, 0, 0, 0}, {"Decay", K_UNI, 0, 0, 0}},
  {{"Drive", K_UNI, 0, 0, 0}, {"Ping", K_UNI, 0, 0, 0}, {"FM", K_BIP, 0, 0, 0}, {"Key", K_UNI, 0, 0, 0}},
  {{"Type", K_ENUM, 0, 2, kFiltTypes}, {"Noise", K_UNI, 0, 0, 0}, {"Color", K_BIP, 0, 0, 0}, {"Attack", K_UNI, 0, 0, 0}},
  {{"Glitch", K_UNI, 0, 0, 0}, {"Size", K_ENUM, 0, GL_N - 1, kGlitchSizes}, {"Bend", K_BIP, 0, 0, 0},
   {"Drop", K_UNI, 0, 0, 0}},
};
const char* const kBusModes[3] = {"OFF", "COMP", "OTT"};
const char* const kDelayAlgos[2] = {"SYNC", "FREE"};
const eng_pdef_t kDelayFreeTime = {"Time", K_UNI, 0, 0, 0};   // the delay's TIME when FREE: 1 ms..1.2 s

// an effect lane's parameters: its effect's knobs, then its step page's DECAY, PROB and DEPTH
const int kLaneKnobs[ENG_LANES] = {0, 0, 0, 0, 8, 4, 8};
inline int lane_page0(int lane) {        // its effect's first page
  return lane == LANE_DELAY ? PG_DELAY(0) : lane == LANE_REVERB ? PG_REVERB : PG_WARPS(0);
}

// Warps' algorithms: its six cross-modulations, as its panel names them, over the whole of ALGORITHM (the module's
// knob, from its first mark to its sixth: 0..0.625; the vocoder, the rest of its travel, is left out: alone it cost
// as much as the four voices), then its easter egg
const char* const kWarpsNames[7] = {"CROSSFADE", "CROSS-FOLD", "DIODE RING", "DIGITAL RING", "XOR", "COMPARATOR",
                                    "FREQ SHIFTER"};
const char* const kWarpsShort[6] = {"Xfade", "Fold", "Diode", "Digi", "Xor", "Comp"};
const char* const kOscXmod[3] = {"Sine", "Triangle", "Saw"};        // modulator.cc: xmod_shape
const float kWarpsTravel = 0.625f;      // ALGORITHM's 0..1 on the module's 0..0.625
int warps_pos(float a) {                // the knob's nearest mark: 0..5
  int i = static_cast<int>(a * 5.0f + 0.5f);
  return i < 0 ? 0 : i > 5 ? 5 : i;
}
float warps_shift_hz(float pot) {       // the frequency shifter's shift (modulator.cc: ProcessEasterEgg)
  float dir = pot >= 0.5f ? 1.0f : -1.0f, f = 2.0f * (pot >= 0.5f ? pot - 0.5f : 0.5f - pot);
  f = f <= 0.4f ? f * f * f * 62.5f : 4.0f * fm_exp2f(15.0f * (f - 0.4f));
  return f * dir;
}

// the models (0.9), in ALGORITHM's order: MIFM1's drums (mi_drum.cc, after Sonic Charge's Microtonic), the LXR-02's
// FM drums (mi_lxr.cc), MIFM1's own voices (ZERO, mi_zero.cc), then Plaits' (mi_plaits.cc: its 1.2 bank, its pitched
// one, its noises and physical models; its three drums gone to MIFM1's). What plays each (its kind), its index there,
// its bank, its name in four letters or so (the tracks' row)
struct ModelDef {
  const char* name;
  const char* abbr;
  uint8_t kind, sub, bank;
};
const ModelDef kModelDef[ENG_MODELS] = {
  {"KICK", "KICK", MK_DRUM, 0, 0}, {"SNARE", "SNAR", MK_DRUM, 1, 0}, {"CLAP", "CLAP", MK_DRUM, 2, 0},
  {"HAT", "HAT", MK_DRUM, 3, 0}, {"TOM", "TOM", MK_DRUM, 4, 0}, {"PERC", "PERC", MK_DRUM, 5, 0},
  {"FM DRUM", "FDRM", MK_LXR, 0, 1}, {"FM SNARE", "FSNR", MK_LXR, 1, 1}, {"FM CYMBAL", "FCYM", MK_LXR, 2, 1},
  {"FM HI-HAT", "FHAT", MK_LXR, 3, 1},
  {"ZERO", "ZERO", MK_ZERO, 0, 2},
  {"FM", "FM", MK_FM, 0, 2},
  {"CHIPTUNE", "CHIP", MK_PLAITS, 7, 3}, {"FORMANT", "FRMT", MK_PLAITS, 11, 3}, {"SPEECH", "SPCH", MK_PLAITS, 15, 3},
  {"SWARM", "SWRM", MK_PLAITS, 16, 4}, {"NOISE", "NOIS", MK_PLAITS, 17, 4}, {"PARTICLES", "PART", MK_PLAITS, 18, 4},
  {"STRING", "STRG", MK_PLAITS, 19, 4}, {"MODAL", "MODL", MK_PLAITS, 20, 4},
};
const char* const kBankNames[ENG_BANKS] = {"DRUMS", "FM DRUMS", "MIFM1", "PLAITS", "NOISE & PHYSICAL"};
inline const ModelDef& mdef(int m) { return kModelDef[m < 0 ? 0 : m >= ENG_MODELS ? ENG_MODELS - 1 : m]; }

const char* const kSrcNames[NSRC] = {"-", "L1", "L2", "L3"};

// a synced LFO's rate: cycles a beat
const float kLfoRatios[9] = {4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 16.0f, 1.0f / 32.0f, 1.0f / 64.0f};
const char* const kLfoRatioNames[9] = {"4 / beat", "2 / beat", "1 / beat", "1 / 2 beats", "1 / bar",
                                         "1 / 2 bars", "1 / 4 bars", "1 / 8 bars", "1 / 16 bars"};

int page_type(int page) {
  if (page >= PG_GLITCH(0)) return PT_GLITCH;
  if (page >= PG_FILT(0, 0)) return PT_FILT1 + (page - PG_FILT(0, 0)) % 3;
  if (page < 12) return PT_PLAITS1 + page % 3;
  if (page < 16) return PT_LFO;
  if (page < PG_STEP(0)) {
    static const uint8_t kT[7] = {PT_MIX, PT_PAN, PT_REVERB, PT_RSEND, PT_DELAY1, PT_DELAY2, PT_DSEND};
    return kT[page - PG_MIX];
  }
  if (page < PG_STEP(LANE_DELAY)) return PT_STEP;
  if (page < PG_GLOBAL) return PT_LANE;
  if (page == PG_GLOBAL) return PT_GLOBAL;
  if (page < PG_WARPS(0)) return PT_ARP;
  if (page < PG_DUCK) return page == PG_WARPS(0) ? PT_WARPS1 : PT_WARPS2;
  if (page < PG_EQ) return page == PG_DUCK ? PT_DUCK : page == PG_COMP ? PT_COMP : PT_OTT;
  return page == PG_EQ ? PT_EQ : page < PG_EUC(0) ? PT_BEND : PT_EUC;
}

// the bits' CLOCK: its multiple of the note (0: off); BITS: how many (12 down to 1, 0: off)
inline int clock_index(float v) {
  int i = static_cast<int>(v * 10.0f - 1e-4f);
  return i < 0 ? 0 : i > 9 ? 9 : i;
}
inline float crush_bits(float v) { return 12.0f - 11.0f * v; }

// the mixer's filter (PG_EQ, a knob a track): its cutoff, to the left a low pass from 18 kHz down to 125 Hz, to the
// right a high pass from 20 Hz up to 8 kHz, an octave a step of the knob's travel the same all along; in the middle
// (within 1 %) nothing
const float kEqDead = 0.01f;
inline float eq_hz(float v) {
  return v < 0.0f ? 18000.0f * fm_exp2f(7.17f * v) : 20.0f * fm_exp2f(8.64f * v);
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline int i_of(float v) { return static_cast<int>(v + 0.5f); }

float bpm_of(float v) { return 30.0f * fm_exp2f(v * 3.321928f); }          // 0..1: 30..300 BPM
float lfo_hz(float v) { return 0.02f * fm_exp2f(v * 9.965784f); }          // an LFO's RATE: 0.02..20 Hz
float lfo_tau(float v) { return 0.001f * fm_exp2f(v * 11.0f); }             // its SLEW: 1 ms..2 s
float delay_ms(float v) { return fm_exp2f(v * 10.228819f); }                // the delay's FREE time: 1..1200 ms
float gate_frac(float v) { return 0.05f + 0.9f * v; }                         // LENGTH: 5..95 % of the sixteenth
float lane_tau(float v) { return 0.01f * fm_exp2f(v * 9.0f); }                // an effect lane's DECAY: 10 ms..5.1 s
float duck_tau(float v) { return 0.02f * fm_exp2f(v * 7.0f); }                // the duck's DECAY: 20 ms..2.6 s
// the master bus's knobs: the compressor's threshold (0..-36 dB), ratio (1..20), release (20 ms..1.3 s), gain
// (+/-12 dB); OTT's time (x1/4..x4, x1 in the middle), drive and level (+/-12 dB)
float comp_threshold(float v) { return -36.0f * v; }
float comp_ratio(float v) { return 1.0f + 19.0f * v * v; }
float comp_release(float v) { return 0.02f * fm_exp2f(v * 6.0f); }
float ott_time(float v) { return fm_exp2f((v - 0.5f) * 4.0f); }

// ------------------------------------------------------------------ engine ---
const uint32_t kN = 128;                // app.h APP_BLOCK
const uint32_t kFrames = 8;             // control frames per block (16 audio frames each: a trig lands within 0.4 ms)
const float kSr = 44100.0f;
const int kFx = ENG_LANES - ENG_TRACKS; // the effect lanes

project_t proj;
float fin[ENG_NPARAMS];                 // the parameters after the cables (the render writes, the screen reads)
float src[NSRC];

#ifndef ENG_ARENA_BYTES
#if UINTPTR_MAX > 0xFFFFFFFFu           /* a computer's 64-bit pointers: a little more */
#define ENG_ARENA_BYTES (292u * 1024u)
#else                                   /* the FM-1: 4 Plaits 100 KB, the reverb 32 (16-bit words), the delay 103,
                                         * Warps 8, the glitch's slices 32 (276 KB) */
#define ENG_ARENA_BYTES (280u * 1024u)
#endif
#endif
uint8_t arena[ENG_ARENA_BYTES] __attribute__((aligned(8))) PLAT_POOL;
void* voice[ENG_TRACKS];
void* reverb;
void* delay;
void* master;
void* warps;
int ready;

volatile int playing, use_core1 = 1;
volatile int key_note[ENG_TRACKS], key_on[ENG_TRACKS];

volatile int key_retrig[ENG_TRACKS], key_hold[ENG_TRACKS];   // a key pressed since the last block; its frames left

int vgate[ENG_TRACKS];                  // each voice's gate as last set
float vnote[ENG_TRACKS];                // its note (latched at the gate's rise)
volatile uint32_t vrises[ENG_TRACKS];   // the trigs it played
uint32_t gates_now;
int gate_seen[ENG_TRACKS];              // its gate was high in the last block
// the duck: T1's gate rising, frame by frame (this block's), then the envelope it starts (sample by sample: its jump
// to 1, its fall with DECAY, a ~1 ms glide over both) and each track's depth, glided
uint8_t duck_hit[kFrames];
int duck_prev;
float duck_d, duck_s, duck_depth[ENG_TRACKS], duckv[kN];

// the trigs: the step clock, then per lane what its trig decided
int restart;                            // PLAY: the steps from the first
float step_clk;                         // samples into the sixteenth
int32_t tick;                           // sixteenths since PLAY
uint32_t fill_trigs[ENG_TRACKS];        // the dose's fills' trigs each track played (the tests)
int cur_step[ENG_LANES];
int gl_at[ENG_TRACKS];                  // the glitch: this block's sixteenth (the sample it starts at; -1 none)
int gl_busy[ENG_TRACKS];                // its slice taken or repeating (the voice does not rest meanwhile)
float gl_six = 5000.0f;                 // a sixteenth, samples
void glitch_stop_all();
float raw_left[ENG_TRACKS];             // a step's gate: its samples left
int last_g[ENG_TRACKS];                 // the gate of the last frame
int rat_on[ENG_TRACKS], rat_n[ENG_TRACKS];
float rat_pos[ENG_TRACKS], rat_period[ENG_TRACKS];
float tdelta[ENG_LANES][ENG_NTP];       // the trig's locks and randoms, over the knobs, until the next trig (a lane
                                        // left without trigs, or another project: back to the knobs, eng_fresh)
void lane_fresh(int lane) {
  for (int tp = 0; tp < ENG_NTP; ++tp) tdelta[lane][tp] = 0.0f;
}
float tnote[ENG_TRACKS];                // its note
uint8_t fg[ENG_TRACKS][kFrames];        // the gates the voices play, frame by frame
float fnote[ENG_TRACKS][kFrames];       // the note a rise in that frame takes
uint32_t rng = 0x9E3779B9u;
float pan_s[ENG_TRACKS];                // each track's pan, glided
float lev_s[ENG_TRACKS], snd_s[ENG_TRACKS], dsnd_s[ENG_TRACKS];   // its level and sends, glided
float mix_gl[ENG_TRACKS], mix_gr[ENG_TRACKS], mix_send[ENG_TRACKS], mix_dsend[ENG_TRACKS], mix_ret, mix_dret;
// the effect lanes: their envelope (1 at a trig, falling with DECAY), their opening (the last block's, to glide from)
float fx_env[kFx], fx_open[kFx] = {1.0f, 1.0f, 1.0f};
int fx_hit[kFx];
// Endless: its clock, the note playing, ping-pong's way, recording
float e_clk[ENG_TRACKS];
int e_pos[ENG_TRACKS], e_dir[ENG_TRACKS], e_note[ENG_TRACKS];
volatile int e_rec[ENG_TRACKS];

// the voices' blocks, two: the voices play this one (vbuf) while the mix takes the last one (mbuf), a block behind
// with the effects, on either core
float vbufs[2][ENG_TRACKS][kN];
float (*vbuf)[kN] = vbufs[0];
float (*mbuf)[kN] = vbufs[1];
// Warps, one block behind: it plays the last block's carrier and modulator (w_car, w_mod, copied once the voices are
// done) while the voices play this one; its output takes the place of a track's in the mix, crossfaded with that
// track as it was (w_din, given with them; w_dry, as late as the output). Its outputs two blocks too: the one it
// plays into (w_out, w_dry), the one the mix takes (m_out, m_dry)
float w_car[kN], w_mod[kN], w_din[kN], w_outs[2][kN], w_drys[2][kN];
float *w_out = w_outs[0], *w_dry = w_drys[0], *m_out = w_outs[1], *m_dry = w_drys[1];
// ALL undone at the next bar (eng_back_at_bar): what to put back, taken in the block the bar starts in
float back_val[ENG_NPARAMS];
uint8_t back_algo[ENG_NPAGES];
int8_t back_rnd[ENG_TRACKS][ENG_NTP];
volatile int back_pending;
volatile int w_job;                     // Warps runs in this block's job
int w_last = -1;                        // the routing w_car and w_mod were taken with: carrier | modulator << 4, -1
int w_dst = -1, w_mute = -1;            // the track its output replaces; the modulator's track, silenced (-1: none)
float w_mix = 1.0f;                     // MIX, the last block's
// Warps' own oscillator (CARRIER Osc 1..3) sounds while the steps run or a key plays: stopped, it fades out in ~30 ms
// (a saw left droning under nothing), and Warps does not run (its track plays dry: its tail, if it has one)
float w_gate = 1.0f, w_gmix = 1.0f;     // its gain now; the one the last mix ended on (0: Warps left out)
int w_idle;                             // its job left out: the gate shut
float vaux[ENG_TRACKS][kN];
float wet_l[kN], wet_r[kN], echo_l[kN], echo_r[kN];
// the USB audio input's channels (eng_usb_frames): each track as the mix takes it (its level, pan, filter, duck and
// Warps; no delay, no reverb, no master bus), then the effects' return (the delay's and the reverb's), 16-bit at the
// master's own scale on the USB (1.0: 8,300,000 at 24 bits, 32,422 at 16): their sum is the master before its bus.
// Made only while the host records (usb_req, from eng_usb_frames' calls, counted down a block at a time)
alignas(4) int16_t usb_ch[kN * ENG_USB_CH];
volatile uint8_t usb_req;
uint8_t usb_now, usb_ok;                // this block's mix makes them; they hold this block's (else silence)
const float kUsbScale = 8300000.0f / 256.0f;
inline int16_t usb16(float x) {
  float v = x * kUsbScale;
  v = v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v;
  return static_cast<int16_t>(v + (v >= 0.0f ? 0.5f : -0.5f));
}
float meter[ENG_TRACKS + 2];
int8_t hist[NSRC][ENG_HIST];
uint32_t hist_at, hist_div;

volatile uint32_t load0, load1, peak, peak_hold_blocks;
// the CPU guard (0.8.2): a block over 92 % of its time lightens the filters' FM for the next 0.3 s (its coefficients
// every 2nd sample), a late one (over 100 %: a click) more (every 4th), a second late one within 0.3 s the FM off;
// then a level back every 0.3 s. Where the FM-1 is that full the sound is moving everywhere: a lighter FM for a moment
// does not show, a click would (Yoann's call). What it saves (the stress test with the dose at 100 %, counted): 3, 5,
// 6 points a core. Off on a computer (its clock is not the FM-1's), but for the tests that ask for it
#ifdef PLAT_HOST
int guard_on = 0;
#else
int guard_on = 1;
#endif
volatile int guard;
int guard_hold, guard_late;               // (guard_late: blocks since a late one, counting down; 0 none lately)
volatile uint32_t guard_blocks;
// the worst block of the last second, its own figures (what made the peak): each task's ticks (0: not played), core
// 0's preparation, core 0's and core 1's totals, the whole render; the preparation's parts (the start and the LFOs,
// the cables, the sequencer, the effects' lanes, the voices' knobs); its frames
volatile uint32_t peak_of[ENG_TRACKS + 2 + 4 + 5], peak_n = 128u, peak_c1;
volatile uint32_t c1_job_ticks[2];      // core 1's time on the block's first and second job (written by it alone)
float prep_cost = 1.0f;                 // core 0's preparation of a block (ticks, averaged): the sequencer, the cables
volatile int on_core1_last;
uint32_t last_voices;                   // the voices the last block played (core 1's first job's guess)

float sat(float x) {                    // a soft limit: tanh's shape up to |x| = 3, then flat
  x = clampf(x, -3.0f, 3.0f);
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

float src_value(int s) { return s > SRC_NONE && s < NSRC ? src[s] : 0.0f; }

// the random dose (proj.dice, OCT- held on the panel, SELECT): every sound parameter thrown anew, at most its span
// times the dose's curve (its square: barely there at the start of the knob's travel, wild at its end). A track's at
// each of its trigs (each note another sound: new_trig), the effects' at each beat (the sequencer: their colours move
// under the notes): the pitches in whole semitones, then in the scale (FREQ, TRANSP), the timbres (Plaits' HARMONICS
// but where it is an interval (the chords, the detune, a swarm's spread), TIMBRE, MORPH, the envelope's knobs; an FM
// drum's own), the gate, the bits (CLOCK, BITS, MASK, SHAPE) and the glitch (GLITCH, SIZE, BEND, DROP), the filter
// (eleven knobs, not its PING; from 60 %
// its TYPE too, on a trig in two at 100 %), the pan (not the EQ, 0.8.5: Yoann's call), the effects' colours (the delay's feedback, filter,
// drive, resonance, wow; the reverb's; Warps' ALGORITHM but as a frequency shift, TIMBRE and levels, but LEVEL 1
// where it is its own oscillator's note: 0.8.6). Nothing that
// leaves the scale (the pitch envelope, a ringing filter's ping, a shift), the levels, the time (divisions, rates,
// lengths, tempo), the scale, the patching; not the filter's model (OFF stays OFF: its CPU), nor its DRIVE, FM,
// NOISE, the bits and the glitch where they are off (0.8.3: dice_costly, their CPU too). The sends (0.5.7: the
// sound thrown, not drowned in the effects) only now and then, lightly: a trig in seven at 100 %, into one effect
float dice_span(int param) {
  if (param < 0 || param >= ENG_NPARAMS) return 0.0f;
  int pt = page_type(param / 4), k = param % 4;
  switch (pt) {
    case PT_PLAITS1: return k == 0 ? 0.25f : 0.6f;            // FREQ: +/-6 semitones (in the scale when it is on)
    case PT_PLAITS2: return k == 0 ? 0.5f : k == 1 ? 0.6f : 1.0f;
    case PT_PLAITS3: return k == 1 ? 0.35f : k == 2 ? 7.0f : 0.0f;   // GATE, TRANSP: +/-7 (then in the scale); not
                                                                     // the pitch envelope (its glide is off the scale)
    case PT_BEND: return k == 0 ? 0.3f : k == 1 ? 0.35f : k == 2 ? 3.0f : 2.0f;   // (MASK: three either way; SHAPE
                                                                                   // two)
    case PT_GLITCH: return k == 0 ? 0.25f : k == 1 ? 1.0f : k == 2 ? 0.5f : 0.3f;
    case PT_FILT1: return k == 0 ? 0.35f : k == 1 ? 0.35f : k == 2 ? 0.8f : 0.4f;   // CUTOFF +/-3.4 octaves, RESO,
    case PT_FILT2: return k == 0 ? 0.4f : k == 1 ? 0.0f : k == 2 ? 0.5f : 0.3f;     // ENV, DECAY; DRIVE, FM, KEY (not
                                                                                     // PING: a resonance it rings at
                                                                                     // any pitch, off the scale);
    case PT_FILT3: return k == 0 ? 0.0f : k == 1 ? 0.35f : k == 2 ? 1.0f : 0.35f;   // NOISE, COLOR, ATTACK (TYPE:
                                                                                     // dice_type)
    case PT_EQ: return 0.0f;                                   // (0.8.5: not thrown, the track's tone kept)
    case PT_PAN: return 0.6f;
    case PT_DELAY1: return k == 1 ? 0.2f : k == 2 ? 0.6f : k == 3 ? 0.5f : 0.0f;
    case PT_DELAY2: return k == 0 || k == 2 ? 0.5f : 0.0f;
    case PT_REVERB: return k < 3 ? 0.4f : 0.0f;
    case PT_WARPS1: return k == 2 ? 0.4f : 0.0f;
    case PT_WARPS2: return k < 3 ? 0.4f : 0.0f;
    default: return 0.0f;
  }
}

// what the dose moves but never turns on (0.8.3): the filter's DRIVE, FM and NOISE, the bits and the glitch, where
// they are off (their knob, lock and random at 0) they stay off. Each costs the FM-1 a lot once on (the FM: the
// filter's coefficients every sample, 2 to 5 % of a block a track); thrown on everywhere at 100 %, they took the
// stress test from 79 to 89 % a core (counted), its worst block over the edge. Where they are on, thrown as ever
int dice_keeps_off = 1;
inline int dice_costly(int tp) {
  return tp == TP_FDRIVE || tp == TP_FM || tp == TP_NOISE || tp == TP_CLOCK || tp == TP_CRUSH || tp == TP_MASK ||
         tp == TP_BSHAPE || tp == TP_GLITCH;
}
float rand01();
int dice_type[ENG_TRACKS] = {-1, -1, -1, -1};   // a track's filter TYPE as this trig's dose threw it (-1: its knob's)
// the spans, in RAM (made once, eng_init: dice_span asks the flash's tables): a track's parameters', in 0.05s; the
// effects' parameters the dose throws, theirs, and this beat's offsets at 100 % (their span times a random, -1..1)
const int kFxDice = 16;
uint8_t tp_dice[ENG_TRACKS][ENG_NTP];
int16_t fx_dice[kFxDice];
float fx_span[kFxDice], fx_off[kFxDice];
int n_fx_dice, dice_on;                 // (dice_on: the dose was up at the last beat, its offsets drawn)
inline int iround(float v);
float dice_curve() {                    // the dose's reach, 0..1: the knob's travel squared
  float d = static_cast<float>(proj.dice > 100 ? 100 : proj.dice) * 0.01f;
  return d * d;
}
int dice_interval(int m) {              // the parameter (tp) of a model that is an interval (off the scale, thrown):
  const ModelDef& d = mdef(m);          // Plaits' HARMONICS (chiptune, swarm, particle); 0 none (the FM's RATIO: see
  if (d.kind != MK_PLAITS) return 0;    // dice_fm_ratio)
  return d.sub == 7 || d.sub == 16 || d.sub == 18 ? 1 : 0;
}
void make_dice() {                      // the effects' (a track's: at its trigs, new_trig)
  for (int j = 0; j < n_fx_dice; ++j) fx_off[j] = fx_span[j] * (2.0f * rand01() - 1.0f);
  dice_on = 1;
}

// each parameter's bounds and its cables' scale (0: no cable; 1 a 0..1 knob, 2 a -1..1 one), in RAM: the render
// clamps some 400 values a block, and eng_pdef's tables are in the flash (on the FM-1, a read there that misses the
// cache costs as much as a few hundred instructions). Made again when the delay changes mode (its TIME's bounds)
float p_lo[ENG_NPARAMS], p_hi[ENG_NPARAMS], p_cab[ENG_NPARAMS];
int bounds_for = -1;
void make_bounds() {
  for (int i = 0; i < ENG_NPARAMS; ++i) {
    const eng_pdef_t* d = eng_pdef(i);
    p_lo[i] = d->kind == K_BIP ? -1.0f : d->kind == K_UNI ? 0.0f : static_cast<float>(d->lo);
    p_hi[i] = d->kind == K_BIP || d->kind == K_UNI ? 1.0f : static_cast<float>(d->hi);
    p_cab[i] = d->kind == K_BIP ? 2.0f : d->kind == K_UNI ? 1.0f : 0.0f;
  }
  bounds_for = proj.algo[PG_DELAY(0)];
}
inline float clamp_fast(int i, float v) { return v < p_lo[i] ? p_lo[i] : v > p_hi[i] ? p_hi[i] : v; }

void apply_cables() {
  if (bounds_for != proj.algo[PG_DELAY(0)]) make_bounds();
  for (int i = 0; i < ENG_NPARAMS; ++i) fin[i] = proj.val[i];
  if (!proj.dice) dice_on = 0;
  if (proj.dice) {                      // the random dose, over the knobs (under the cables): the effects' this beat
    if (!dice_on) make_dice();
    float a = dice_curve();
    int shift = iround(proj.val[PG_WARPS(1) * 4 + 3]) == WM_SHIFT;
    int osc = iround(proj.val[PG_WARPS(0) * 4 + 0]) >= WC_OSC1;   // (Warps' own oscillator: LEVEL 1 is its note)
    for (int j = 0; j < n_fx_dice; ++j)
      if (!(shift && fx_dice[j] == PG_WARPS(0) * 4 + 2) && !(osc && fx_dice[j] == PG_WARPS(1) * 4 + 1))
        fin[fx_dice[j]] += a * fx_off[j];   // (not the shift, not the oscillator's note: 0.8.6, Yoann's call)
    // Warps' ALGORITHM on a mark (one cross-modulation): thrown to another mark, never between two (there Warps plays
    // both algorithms, blended: twice its work)
    const int wa = PG_WARPS(0) * 4 + 2;
    float m = proj.val[wa] * 5.0f;
    if (fm_fabsf(m - fm_floorf(m + 0.5f)) < 0.01f) fin[wa] = fm_floorf(fin[wa] * 5.0f + 0.5f) * 0.2f;
  }
  for (int g = 0; g <= ENG_TRACKS; ++g) {
    for (int c = 0; c < ENG_CABLES; ++c) {
      const cable_t& k = proj.cable[g][c];
      if (!k.depth || k.src == SRC_NONE || k.src >= NSRC || k.dst >= ENG_NPARAMS) continue;
      fin[k.dst] += static_cast<float>(k.depth) * 0.01f * src_value(k.src) * p_cab[k.dst];
    }
  }
  for (int i = 0; i < ENG_NPARAMS; ++i) fin[i] = clamp_fast(i, fin[i]);
}

inline float P(int page, int k) { return fin[page * 4 + k]; }
inline int Pi(int page, int k) { return static_cast<int>(fin[page * 4 + k] + (fin[page * 4 + k] < 0 ? -0.5f : 0.5f)); }
inline int iround(float v) { return static_cast<int>(v + (v < 0 ? -0.5f : 0.5f)); }

float to_u(int param, float v) {        // a value as 0..1 of its travel (a lock's byte)
  const eng_pdef_t* d = eng_pdef(param);
  if (d->kind == K_BIP) return (v + 1.0f) * 0.5f;
  if (d->kind == K_UNI) return v;
  return d->hi > d->lo ? (v - static_cast<float>(d->lo)) / static_cast<float>(d->hi - d->lo) : 0.0f;
}
float from_u(int param, float u) {
  const eng_pdef_t* d = eng_pdef(param);
  if (d->kind == K_BIP) return u * 2.0f - 1.0f;
  if (d->kind == K_UNI) return u;
  return static_cast<float>(d->lo + iround(u * static_cast<float>(d->hi - d->lo)));
}

float from_u_fast(int param, float u) { // from_u on the bounds in RAM (the render's: make_bounds done)
  float c = p_cab[param];
  if (c == 2.0f) return u * 2.0f - 1.0f;
  if (c == 1.0f) return u;
  return p_lo[param] + static_cast<float>(iround(u * (p_hi[param] - p_lo[param])));
}

// a lane parameter as the trig plays it: its knob and cables, then the trig's lock and random
int16_t lane_param[ENG_LANES][ENG_NTP];  // eng_track_param's answers, made once (E is asked ~150 times a block)
uint32_t lane_lockable[ENG_LANES][2];    // its parameters a trig's lock or random moves (bit tp), made once
// the steps with a lock (bit s), each lane's: a trig looks for its locks (all 384 slots) only on those. Made again
// when the locks change (lock_gen: eng_lock_set, eng_lock_clear, eng_steps_clear, a project made or loaded)
uint32_t lock_steps[ENG_LANES][ENG_STEPS / 32];
volatile uint32_t lock_gen = 1u;
uint32_t lock_gen_seen;
void make_lock_steps() {
  lock_gen_seen = lock_gen;
  for (int l = 0; l < ENG_LANES; ++l)
    for (int w = 0; w < ENG_STEPS / 32; ++w) lock_steps[l][w] = 0u;
  for (int i = 0; i < ENG_PLOCKS; ++i) {
    const plock_t& k = proj.lock[i];
    if (k.lane < ENG_LANES && k.step < ENG_STEPS) lock_steps[k.lane][k.step / 32] |= 1u << (k.step % 32);
  }
}
float E(int lane, int tp) {
  int param = lane_param[lane][tp];
  return clamp_fast(param, fin[param] + tdelta[lane][tp]);
}

uint32_t rand_u32() {                   // xorshift32
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
float rand01() { return static_cast<float>(rand_u32() >> 8) * (1.0f / 16777216.0f); }
float loop01(int lane, int step, int tp) {   // the looped random: the same for this lane, step and parameter
  uint32_t h = (proj.seed[lane] + 1u) * 2654435761u ^ static_cast<uint32_t>(step) * 40503u ^
               static_cast<uint32_t>(tp * 9973 + lane * 7919) ^ static_cast<uint32_t>(proj.rseed[lane][tp]) * 0x9E3779B9u;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return static_cast<float>(h & 0xFFFFu) * (1.0f / 65535.0f);
}

int quantize_slow(int n, int sc, int root) {   // the nearest note of the scale (a tie: the lower)
  if (sc <= 0 || sc >= 15) return n;
  uint32_t m = kScaleMasks[sc];
  int rel = ((n - root) % 12 + 12) % 12;
  for (int d = 0; d <= 6; ++d) {
    if ((m >> ((rel - d + 12) % 12)) & 1u) return n - d;
    if ((m >> ((rel + d) % 12)) & 1u) return n + d;
  }
  return n;
}
// the render's: a table of the notes -32..159, made again when the scale or its root changes (the screen's,
// eng_quantize, works it out: the table is the render's alone)
const int kQLo = -32, kQN = 192;
int16_t qmap[kQN];
int q_sc = -1, q_root = -1;
int quantize(int n) {
  int sc = Pi(PG_GLOBAL, 1), root = Pi(PG_GLOBAL, 2);
  if (sc <= 0 || sc >= 15) return n;
  if (sc != q_sc || root != q_root) {
    for (int i = 0; i < kQN; ++i) qmap[i] = static_cast<int16_t>(quantize_slow(i + kQLo, sc, root));
    q_sc = sc;
    q_root = root;
  }
  int i = n - kQLo;
  return i >= 0 && i < kQN ? qmap[i] : quantize_slow(n, sc, root);
}

int scale_on() {                        // SCALE not Off: every pitch in it
  int sc = Pi(PG_GLOBAL, 1);
  return sc > 0 && sc < 15;
}

int warps_note(float level1) { return quantize(iround(12.0f + 60.0f * level1)); }   // its oscillator's (LEVEL 1)

int lock_find(int lane, int step, int tp) {
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (proj.lock[i].lane == lane && proj.lock[i].step == step && proj.lock[i].tp == tp) return i;
  return -1;
}

float step_len_now() { return kSr * 60.0f / (bpm_of(P(PG_GLOBAL, 0)) * 4.0f); }
float six_len(float sl) {               // the sixteenth playing's length, swung: the on-beat's long, the off-beat's short
  float sw = 0.5f + 0.25f * P(PG_GLOBAL, 3);
  return 2.0f * sl * ((tick & 1) ? 1.0f - sw : sw);
}

// the three LFOs, at the control rate (a value a block): WAVE; RATE, 0.02..20 Hz (synced: a ratio of the beat, its
// phase from the bar, in time with the steps); MODE, free, synced, or started again at a track's trigs (a block
// after, the trig seen); SLEW, a glide on what comes out (none, up to 2 s). S&H takes a random value a cycle, Smooth
// glides from one to the next. Out: -1..1, the cables' sources L1..L3
struct Lfo {
  float phase, out, a, b;               // its phase (0..1), what comes out, the random's last value and the next
  uint32_t rises, seed;                 // its track's trigs seen; its own random (the steps' stays theirs)
};
Lfo lfo[3];

float lfo_rand(Lfo& o) {
  o.seed = o.seed * 1664525u + 1013904223u;
  return static_cast<float>(o.seed >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

void run_lfos(uint32_t n) {
  float block_hz = kSr / static_cast<float>(n), beat_hz = bpm_of(P(PG_GLOBAL, 0)) / 60.0f;
  for (int i = 0; i < 3; ++i) {
    Lfo& o = lfo[i];
    int wave = Pi(PG_LFO(i), 0), mode = Pi(PG_LFO(i), 2), cycle = 0;
    float rate = P(PG_LFO(i), 1);
    if (!o.seed) o.seed = 0x9E3779B9u * static_cast<uint32_t>(i + 1);
    if (mode == LM_SYNC) {
      int r = static_cast<int>(rate * 8.999f);
      float ratio = kLfoRatios[r < 0 ? 0 : r > 8 ? 8 : r];
      if (playing) {                    // the phase from the bar: the beats since PLAY (the sixteenth, swung)
        float beats = (static_cast<float>(tick) + step_clk / six_len(step_len_now())) * 0.25f * ratio;
        float ph = beats - fm_floorf(beats);
        cycle = ph < o.phase;           // (wrapped)
        o.phase = ph;
      } else {
        o.phase += beat_hz * ratio / block_hz;
      }
    } else {
      if (mode >= LM_T1 && mode < LM_T1 + ENG_TRACKS) {
        uint32_t r = vrises[mode - LM_T1];
        if (r != o.rises) {             // a trig: from the start
          o.rises = r;
          o.phase = 0.0f;
          cycle = 1;
        }
      }
      o.phase += lfo_hz(rate) / block_hz;
    }
    if (o.phase >= 1.0f) {
      o.phase -= fm_floorf(o.phase);
      cycle = 1;
    }
    if (cycle) {                        // a new cycle: the random's next value
      o.a = o.b;
      o.b = lfo_rand(o);
    }
    float ph = o.phase, v;
    switch (wave) {
      case LW_SINE: v = fm_sinf(6.2831853f * ph); break;
      case LW_TRI: v = ph < 0.25f ? 4.0f * ph : ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f; break;
      case LW_SAW: v = 2.0f * ph - 1.0f; break;
      case LW_RAMP: v = 1.0f - 2.0f * ph; break;
      case LW_SQUARE: v = ph < 0.5f ? 1.0f : -1.0f; break;
      case LW_SH: v = o.b; break;
      default: v = o.a + (o.b - o.a) * (0.5f - 0.5f * fm_cosf(3.14159265f * ph)); break;
    }
    float slew = P(PG_LFO(i), 3);
    if (slew < 0.005f) o.out = v;
    else o.out += (v - o.out) * (1.0f - fm_expf(-1.0f / (lfo_tau(slew) * block_hz)));
    src[SRC_L1 + i] = o.out;
  }
}

// a trig: its locks and randoms over the knobs, whether it plays (its probability); a track's ratchets. The locks are
// found in one pass over the slots (not one a parameter: a sixteenth's trigs on seven lanes took 50,000 compares)
void new_trig(int lane, int step, int locks) {   // (on the RAM tables: lane_param, lane_lockable, the bounds)
  int16_t at[ENG_NTP];
  for (int tp = 0; tp < ENG_NTP; ++tp) at[tp] = -1;
  if (lock_gen_seen != lock_gen) make_lock_steps();
  if (locks && step >= 0 && step < ENG_STEPS && ((lock_steps[lane][step / 32] >> (step % 32)) & 1u))
    for (int i = 0; i < ENG_PLOCKS; ++i)
      if (proj.lock[i].lane == lane && proj.lock[i].step == step && proj.lock[i].tp < ENG_NTP)
        at[proj.lock[i].tp] = static_cast<int16_t>(i);
  for (int tp = 0; tp < ENG_NTP; ++tp) {
    tdelta[lane][tp] = 0.0f;
    if (!((lane_lockable[lane][tp >> 5] >> (tp & 31)) & 1u)) continue;
    int r = proj.rnd[lane][tp];
    if (at[tp] < 0 && !r) continue;     // (neither a lock nor a random: the knob)
    int param = lane_param[lane][tp];
    float base = proj.val[param], v = base;
    if (at[tp] >= 0) v = from_u_fast(param, static_cast<float>(proj.lock[at[tp]].v) / 255.0f);
    if (r) {                            // > 0: a new random at each trig; < 0: the same at each pass (a loop)
      float a = static_cast<float>(r < 0 ? -r : r) * 0.01f, x = r > 0 ? rand01() : loop01(lane, step, tp);
      float c = p_cab[param];
      if (lane < ENG_TRACKS && tp == TP_RATCHET) v += a * x * 7.0f;               // only more
      else if (lane < ENG_TRACKS && tp == TP_NOTE) v += a * (x * 2.0f - 1.0f) * 12.0f;   // an octave either way
      else v += a * (x * 2.0f - 1.0f) * (c != 0.0f ? c : p_hi[param] - p_lo[param]);
    }
    tdelta[lane][tp] = v - base;
  }
  if (lane < ENG_TRACKS) {              // the random dose: every sound parameter of the track thrown anew, this trig's
    dice_type[lane] = -1;
    if (proj.dice) {
      float a = dice_curve(), d = static_cast<float>(proj.dice > 100 ? 100 : proj.dice) * 0.01f;
      int iv = dice_interval(proj.algo[PG_TRACK(lane, 0)]);
      for (int tp = 0; tp < ENG_NTP; ++tp) {
        if (!tp_dice[lane][tp] || tp == iv) continue;
        if (dice_keeps_off && dice_costly(tp) &&
            fm_fabsf(fin[eng_track_param(lane, tp)] + tdelta[lane][tp]) < 0.005f) continue;
        float off = a * 0.05f * static_cast<float>(tp_dice[lane][tp]) * (2.0f * rand01() - 1.0f);   // (in 0.05s)
        if (tp == 0) off = static_cast<float>(iround(off * 24.0f)) * (1.0f / 24.0f);   // FREQ: whole semitones
        if (tp == 2 && mdef(proj.algo[PG_TRACK(lane, 0)]).kind == MK_FM) {   // the FM's RATIO: a chord stays, the others
                                        // not thrown into one
          float now = fin[eng_track_param(lane, tp)] + tdelta[lane][tp];
          if (fm4_chord(now)) continue;
          float top = static_cast<float>(fm4_chords_at()) - 1e-3f - now;
          off = off < top ? off : top;
        }
        tdelta[lane][tp] += off;
      }
      if (d > 0.6f && rand01() < (d - 0.6f) * 1.25f) dice_type[lane] = static_cast<int>(rand_u32() % MF_TYPES);
      if (rand01() < 0.15f * a)         // now and then, lightly, into the delay or the reverb
        tdelta[lane][rand01() < 0.5f ? TP_DSEND : TP_SEND] += 0.2f + 0.15f * rand01();
    }
  }
  if (lane >= ENG_TRACKS) {             // an effect: open, by its probability
    float prob = E(lane, kLaneKnobs[lane] + 1);
    if (prob >= 0.999f || rand01() < prob) fx_hit[lane - ENG_TRACKS] = 1;
    return;
  }
  float prob = E(lane, TP_PROB);
  rat_n[lane] = static_cast<int>(clampf(static_cast<float>(iround(fin[eng_track_param(lane, TP_RATCHET)] +
                                                                   tdelta[lane][TP_RATCHET])), 1.0f, 8.0f));
  rat_on[lane] = prob >= 0.999f || rand01() < prob;
  rat_pos[lane] = 0.0f;
}

// the random dose's fills, in its last quarter (76..100 %): trigs added at the end of bars, as a drummer's fills, on
// the tracks that play steps (Endless and its recording left alone). The bars from PLAY (16 sixteenths): from 76 % a
// fill on the last beat every 4 bars, on one track; up the dose, every 2 bars (from 85 %), on the last two beats (88
// %), every bar (93 %), on more tracks (one more each third of the quarter: all four at 100 %), from a wider set of
// patterns, the busiest with ratchets (two or three trigs in a sixteenth) and a descent of the pitch (a tom fill).
// Which tracks and which patterns: from the dose's seed and the bar (the same each pass while the dose is held). A
// fill's trig plays whatever its step's probability; on a step's own trig, its ratchets and its pitch are the fill's
struct FillPat {
  uint8_t hit[8];                       // the bar's last eight sixteenths: 0 nothing, 1 a trig, 2 and 3 ratchets
  int8_t dn[8];                         // the note, semitones (then in the scale)
};
const FillPat kFills[] = {
    // the last beat: from a pickup to a roll
    {{0, 0, 0, 0, 0, 0, 0, 1}, {0}},
    {{0, 0, 0, 0, 0, 0, 1, 1}, {0}},
    {{0, 0, 0, 0, 0, 1, 0, 1}, {0}},
    {{0, 0, 0, 0, 1, 0, 1, 1}, {0}},
    {{0, 0, 0, 0, 1, 1, 1, 1}, {0}},
    {{0, 0, 0, 0, 1, 0, 1, 2}, {0}},
    {{0, 0, 0, 0, 1, 2, 1, 2}, {0}},
    // the last two beats
    {{1, 0, 0, 1, 0, 0, 1, 0}, {0}},                                   // 3-3-2
    {{0, 0, 1, 0, 1, 0, 1, 1}, {0}},
    {{1, 0, 1, 0, 1, 1, 1, 1}, {0}},                                   // eighths, then sixteenths
    {{1, 0, 1, 1, 1, 0, 1, 1}, {0}},                                   // a gallop
    {{1, 1, 1, 1, 1, 1, 1, 1}, {0, 0, -2, -2, -4, -5, -7, -9}},       // down the toms
    {{1, 1, 1, 1, 1, 1, 1, 1}, {0}},                                   // a roll
    {{3, 0, 0, 3, 0, 0, 2, 2}, {0}},                                   // flams
    {{1, 1, 1, 1, 2, 2, 2, 2}, {0}},                                   // the roll into 32nds
    {{1, 1, 2, 1, 2, 1, 3, 3}, {0, 0, -2, -3, -5, -7, -9, -12}},      // all out, down
};
const int kFillShort = 7, kFillN = static_cast<int>(sizeof kFills / sizeof kFills[0]);

int fill_amount_q(int dice) { return dice > 75 ? (dice > 100 ? 100 : dice) - 75 : 0; }   // 0..25
struct FillPlan {
  int32_t bar;
  int dice, seed;
  int every, span, tracks;              // a fill every so many bars, over its last span sixteenths, on so many tracks
  int8_t pat[ENG_TRACKS];               // each track's pattern this bar (-1: none)
};
FillPlan fplan = {-1, -1, -1, 0, 0, 0, {-1, -1, -1, -1}};

uint32_t fill_hash(uint32_t a, uint32_t b) {
  uint32_t h = (a + 1u) * 0x9E3779B9u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return h;
}

void fill_shape(int q, int* every, int* span, int* tracks) {   // the dose's last quarter, 1..25
  *every = q < 10 ? 4 : q < 18 ? 2 : 1;
  *span = q < 13 ? 4 : 8;
  *tracks = 1 + (q - 1) * 3 / 24;       // 1 at 76 %, 2 from 84, 3 from 92, 4 at 100
}

void fill_plan(int32_t bar, const int* stepping) {
  if (fplan.bar == bar && fplan.dice == proj.dice && fplan.seed == proj.dice_seed) return;
  fplan.bar = bar;
  fplan.dice = proj.dice;
  fplan.seed = proj.dice_seed;
  for (int t = 0; t < ENG_TRACKS; ++t) fplan.pat[t] = -1;
  int q = fill_amount_q(proj.dice);
  if (!q) return;
  fill_shape(q, &fplan.every, &fplan.span, &fplan.tracks);
  if (bar % fplan.every != fplan.every - 1) return;
  uint32_t h = fill_hash(static_cast<uint32_t>(proj.dice_seed), static_cast<uint32_t>(bar));
  uint32_t key[ENG_TRACKS];
  for (int t = 0; t < ENG_TRACKS; ++t) key[t] = fill_hash(h, static_cast<uint32_t>(t) + 11u);
  // the patterns: the short ones (the last beat) or the long ones, more of them up the dose
  int lo = fplan.span == 4 ? 0 : kFillShort, all = fplan.span == 4 ? kFillShort : kFillN - kFillShort;
  int part = fplan.span == 4 ? q : q - 12;   // (1..12, 1..13)
  int pool = 2 + (all - 2) * part / (fplan.span == 4 ? 12 : 13);
  if (pool > all) pool = all;
  for (int t = 0; t < ENG_TRACKS; ++t) {
    if (!stepping[t]) continue;
    int rank = 0;                       // its place among the tracks that play steps, by its key
    for (int u = 0; u < ENG_TRACKS; ++u)
      if (u != t && stepping[u] && (key[u] < key[t] || (key[u] == key[t] && u < t))) ++rank;
    if (rank < fplan.tracks)
      fplan.pat[t] = static_cast<int8_t>(lo + static_cast<int>(fill_hash(key[t], 3u) % static_cast<uint32_t>(pool)));
  }
}

// a fill's hit for track t at this sixteenth: 0 none, 1..3 a trig (its ratchets); its note in *dn
int fill_hit(int t, int* dn) {
  int p = fplan.pat[t], pos = static_cast<int>(tick % 16) - 8;
  *dn = 0;
  if (p < 0 || pos < 0 || pos < 8 - fplan.span) return 0;
  *dn = kFills[p].dn[pos];
  return kFills[p].hit[pos];
}

// the trigs of one block, frame by frame: fg (the gates) and fnote (the notes) for the voices; the effects' trigs
void sequence(uint32_t n) {
  float fl = static_cast<float>(n / kFrames);
  float sl = step_len_now();
  if (restart) {
    restart = 0;
    step_clk = 1e9f;                    // the first sixteenth at once
    tick = -1;
    for (int t = 0; t < ENG_TRACKS; ++t) {
      e_clk[t] = 1e9f;
      e_pos[t] = -1;
      e_dir[t] = 1;
    }
  }
  int endless[ENG_TRACKS];
  float el[ENG_TRACKS];
  for (int t = 0; t < ENG_TRACKS; ++t) {
    endless[t] = proj.endless_len[t] > 0 && proj.endless_len[t] <= ENG_ENDLESS && !e_rec[t];
    int r = Pi(PG_ARP(t), 0);
    el[t] = sl * kArpMult[r < 0 ? 0 : r > 5 ? 5 : r];
    gate_seen[t] = 0;
  }
  for (int t = 0; t < ENG_TRACKS; ++t) gl_at[t] = -1;
  gl_six = sl;
  if (!playing) glitch_stop_all();
  for (uint32_t k = 0; k < kFrames; ++k) {
    int fresh[ENG_TRACKS] = {0, 0, 0, 0};   // a step's trig in this frame
    int fhit[ENG_TRACKS] = {0, 0, 0, 0}, fdn[ENG_TRACKS] = {0, 0, 0, 0};   // the dose's fills: a hit, its note
    if (playing && (step_clk += fl) >= six_len(sl)) {   // a sixteenth: each lane's step, its trig
      step_clk -= six_len(sl);
      ++tick;
      if (step_clk >= six_len(sl)) step_clk = 0.0f;
      if (proj.dice && tick % 4 == 0) make_dice();   // (the dose: the effects thrown anew at each beat)
      for (int t = 0; t < ENG_TRACKS; ++t) gl_at[t] = static_cast<int>(k * static_cast<uint32_t>(fl));   // (the glitch)
      if (fill_amount_q(proj.dice) && tick % 16 >= 8) {   // (the bar's second half: where fills are)
        int stepping[ENG_TRACKS];
        for (int t = 0; t < ENG_TRACKS; ++t) stepping[t] = !endless[t] && !e_rec[t] && eng_lane_has_trigs(t);
        fill_plan(tick / 16, stepping);
        for (int t = 0; t < ENG_TRACKS; ++t) fhit[t] = stepping[t] ? fill_hit(t, &fdn[t]) : 0;
      }
      for (int l = 0; l < ENG_LANES; ++l) {
        int len = Pi(PG_STEP(l), 3);
        cur_step[l] = static_cast<int>(tick % (len < 1 ? 1 : len));
        if (!eng_trig(l, cur_step[l]) && !(l < ENG_TRACKS && fhit[l])) continue;
        if (l >= ENG_TRACKS) new_trig(l, cur_step[l], 1);
        else if (!endless[l] && !e_rec[l]) fresh[l] = 1;
      }
    }
    for (int t = 0; t < ENG_TRACKS; ++t) {
      int raw, g, rise = 0;
      if (endless[t] && playing && (e_clk[t] += fl) >= el[t]) {   // Endless: its next note (or rest)
        int len = proj.endless_len[t], d = Pi(PG_ARP(t), 2);
        e_clk[t] = e_clk[t] >= 2.0f * el[t] ? 0.0f : e_clk[t] - el[t];
        if (d == 1) {
          e_pos[t] = e_pos[t] <= 0 ? len - 1 : e_pos[t] - 1;
        } else if (d == 2) {
          e_pos[t] += e_dir[t];
          if (e_pos[t] >= len) e_dir[t] = -1, e_pos[t] = len > 1 ? len - 2 : 0;
          if (e_pos[t] < 0) e_dir[t] = 1, e_pos[t] = len > 1 ? 1 : 0;
        } else if (d == 3) {
          e_pos[t] = static_cast<int>(rand_u32() % static_cast<uint32_t>(len));
        } else {
          e_pos[t] = (e_pos[t] + 1) % len;
        }
        int nt = proj.endless[t][e_pos[t]];
        if (nt != ENDLESS_REST) {
          raw_left[t] = fm_maxf(fl * 2.0f, el[t] * clampf(P(PG_ARP(t), 1), 0.05f, 1.0f) * 0.98f);
          e_note[t] = nt;
          rise = 1;
        }
      }
      if (fresh[t]) {                   // a step's trig: its length is its own (a lock, a random)
        new_trig(t, cur_step[t], 1);
        if (fhit[t]) {                  // a fill's: it plays, its ratchets and its note the fill's
          rat_on[t] = 1;
          if (rat_n[t] < fhit[t]) rat_n[t] = fhit[t];
          ++fill_trigs[t];
        }
        raw_left[t] = fm_maxf(fl * 2.0f, sl * gate_frac(E(t, TP_LENGTH)));
        rat_period[t] = sl / static_cast<float>(rat_n[t]);
        tnote[t] = static_cast<float>(quantize(iround(48.0f + 12.0f * static_cast<float>(iround(E(t, TP_OCTAVE))) +
                                                      static_cast<float>(iround(E(t, TP_TRANSPOSE))) +
                                                      E(t, TP_NOTE)) + fdn[t]));
      } else if (rise) {                // Endless' note: its randoms, no locks
        new_trig(t, e_pos[t], 0);
        rat_period[t] = el[t] / static_cast<float>(rat_n[t]);
        tnote[t] = static_cast<float>(quantize(e_note[t] + proj.endless_key[t] + Pi(PG_ARP(t), 3) +
                                               12 * iround(E(t, TP_OCTAVE)) + iround(E(t, TP_TRANSPOSE)) +
                                               iround(tdelta[t][TP_NOTE])));
      }
      if (e_rec[t]) {
        raw = 0;
      } else {
        raw = playing && raw_left[t] > 0.0f;
        raw_left[t] -= fl;
      }
      if (!rat_on[t]) {
        g = 0;
      } else if (rat_n[t] <= 1) {
        g = raw;
        if (!raw) rat_on[t] = 0;
      } else {                          // the ratchets: n gates in the step
        int i = static_cast<int>(rat_pos[t] / rat_period[t]);
        g = i < rat_n[t] && rat_pos[t] - static_cast<float>(i) * rat_period[t] < rat_period[t] * 0.5f;
        rat_pos[t] += fl;
        if (i >= rat_n[t]) rat_on[t] = 0;
      }
      if ((fresh[t] || rise) && last_g[t]) g = 0;   // a trig on a gate still high (swung, legato): low for a frame
      fnote[t][k] = tnote[t];
      if (key_on[t] || key_hold[t] > 0) {   // the keyboard plays over; a new key, a new trig
        g = !(key_retrig[t] && k == 0);
        fnote[t][k] = static_cast<float>(quantize(key_note[t] + 12 * iround(E(t, TP_OCTAVE)) +
                                                  iround(E(t, TP_TRANSPOSE))));   // (OCTAVE too, as the steps)
        if (key_hold[t] > 0) --key_hold[t];
      }
      fg[t][k] = static_cast<uint8_t>(g);
      last_g[t] = g;
      if (t == 0) {                     // T1: a rise ducks the others
        duck_hit[k] = static_cast<uint8_t>(g && !duck_prev);
        duck_prev = g;
      }
      gate_seen[t] |= g;
    }
  }
  for (int t = 0; t < ENG_TRACKS; ++t) key_retrig[t] = 0;
}

// the effects' openings this block: a trig opens its effect, DECAY closes it down to 1 - DEPTH; a lane without a trig
// (or the steps stopped) leaves it open
void fx_lanes(uint32_t n, float* open) {
  for (int f = 0; f < kFx; ++f) {
    int lane = ENG_TRACKS + f, kk = kLaneKnobs[lane];
    if (fx_hit[f]) {
      fx_env[f] = 1.0f;
      fx_hit[f] = 0;
    } else {
      float tau = lane_tau(E(lane, kk));
      fx_env[f] *= fm_exp2f(-1.442695f * static_cast<float>(n) / (kSr * tau));
      if (fx_env[f] < 1e-4f) fx_env[f] = 0.0f;
    }
    float depth = E(lane, kk + 2);
    open[f] = playing && eng_lane_has_trigs(lane) ? 1.0f - depth + depth * fx_env[f] : 1.0f;
  }
}

// an FM drum (the LXR-02's voices): its eight knobs, each 0..1 (a bipolar knob from its middle), the track's
// parameters after FREQ (1..8: HARMONICS, TIMBRE, MORPH, DECAY, COLOUR, T.ENV, M.ENV, P.ENV)
inline int track_kind(int t) { return mdef(proj.algo[PG_TRACK(t, 0)]).kind; }
inline int track_sub(int t) { return mdef(proj.algo[PG_TRACK(t, 0)]).sub; }
// a model with its own eight knobs (an FM drum, a drum, ZERO): each 0..1 (a bipolar knob from its middle), the
// track's parameters after FREQ (1..8: HARMONICS, TIMBRE, MORPH, DECAY, COLOUR, T.ENV, M.ENV, P.ENV)
int lx_model(int t) { return track_kind(t) == MK_LXR ? track_sub(t) : -1; }
inline int own_knobs(int t) { return track_kind(t) != MK_PLAITS; }
inline int zero_on(int t) { return track_kind(t) == MK_ZERO; }
inline int lx_param(int t, int i) { return PG_TRACK(t, 0) * 4 + 1 + i; }
inline float lx_u(int param, float v) { return eng_pdef(param)->kind == K_BIP ? (v + 1.0f) * 0.5f : v; }
void lx_kit(project_t* p, int t) {      // its knobs as its kit has them (a drum's, ZERO's: their own)
  float k[LX_KNOBS];
  const ModelDef& d = mdef(p->algo[PG_TRACK(t, 0)]);
  if (d.kind == MK_ZERO) zero_defaults(k);
  else if (d.kind == MK_DRUM) drum_defaults(d.sub, k);
  else if (d.kind == MK_FM) fm4_defaults(k);
  else lx_defaults(d.sub, k);
  for (int i = 0; i < LX_KNOBS; ++i) {
    int param = lx_param(t, i);
    p->val[param] = eng_pdef(param)->kind == K_BIP ? 2.0f * k[i] - 1.0f : k[i];
  }
  if (d.kind == MK_DRUM || d.kind == MK_LXR)   // OCTAVE 0: C3 plays the kit's own pitch (a bass's -1 would halve a
    p->val[PG_TRACK(t, 2) * 4 + 3] = 0.0f;     // kick; ZERO, the FM play the note as Plaits does: its octave kept)
}

void set_voice(int t) {
  if (own_knobs(t)) {
    float k[LX_KNOBS];
    for (int i = 0; i < LX_KNOBS; ++i) k[i] = lx_u(lx_param(t, i), E(t, 1 + i));
    float note = vnote[t] + 24.0f * E(t, 0);
    if (scale_on()) note = static_cast<float>(quantize(iround(note)));
    int kind = track_kind(t), sub = track_sub(t);
    if (kind == MK_ZERO) zero_set(t, note, k);
    else if (kind == MK_DRUM) drum_set(t, sub, note, k);
    else if (kind == MK_FM) fm4_set(t, note, k);
    else lx_set(t, sub, note, k);
    return;
  }
  // the models whose MORPH is their decay (string, modal, the drums: their own envelope, no LPG) take DECAY too, around
  // its middle: the envelope page's DECAY, its locks and its random move every model's decay
  int m = track_sub(t);                 // (Plaits' engine)
  float morph = E(t, 3);
  if (m >= 19) morph = clampf(morph + E(t, 4) - 0.5f, 0.0f, 1.0f);
  float note = vnote[t] + 24.0f * E(t, 0);   // FREQ (its cables, its locks) over the note: with SCALE on, in it too
  if (scale_on()) note = static_cast<float>(quantize(iround(note)));
  mp_set(voice[t], m, note, E(t, 1), E(t, 2), morph, E(t, 4), E(t, 5));
  mp_amounts(voice[t], E(t, TP_PITCHENV), E(t, 6), E(t, 7));   // the envelope on the pitch (Plaits' FM attenuverter)
}

// a voice silent a while, its gate low (its note over, its LPG or its own envelope down under -90 dB): it rests,
// rendered again at its next gate (its oscillators where they were: a new note starts from its trigger anyway)
const int kQuiet = 4;                   // the silent blocks before it rests
int quiet[ENG_TRACKS];
int voice_rests(int t) {
  if (vgate[t] || quiet[t] < kQuiet || gl_busy[t]) return 0;
  for (uint32_t k = 0; k < kFrames; ++k)
    if (fg[t][k]) return 0;
  return 1;
}

// a track's circuit bending (0.7, the grit's place), on its voice: its bits before its filter, its glitch after it.
// The bits (after OTO Machines' Biscuit, an 8-bit box with a switch for each bit): SHAPE first, a digital shaper
// (rectified: an octave up; DOWN, DOWN 3: the sign turned at every second, every third cycle, as a divider does: an
// octave, an octave and a fifth under; folded; or SWAP, the 8-bit word's halves swapped), then CLOCK (a sample and
// hold on a multiple of the note: x48 down to x2, the aliasing it makes in tune), BITS (12 down to 1), MASK (the
// sample as an 8-bit word, each bit on, inverted or muted: the part under the word kept but when its last bit is
// muted). A coupling capacitor after it, when a rectifier or a flipped bit leaves a DC. All off: nothing done
struct Bend {
  int on, shape, mask, ci, dc;          // (dc: a shape or a mask that leaves a DC: the capacitor on)
  float rate, steps;                    // CLOCK: a sample's share of a hold (1: none); BITS: the steps a unit (0: none)
  float hold, cnt;                      // the sample held, the count to the next
  int pos, zc;                          // the zero crossings: above, their count
  float dx, dy;                         // the coupling capacitor
};
Bend bend[ENG_TRACKS];

inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }

void set_bend(int t) {
  Bend& g = bend[t];
  float ck = E(t, TP_CLOCK), cr = E(t, TP_CRUSH);
  g.mask = iround(E(t, TP_MASK));
  g.mask = g.mask < 0 ? 0 : g.mask >= BM_N ? BM_N - 1 : g.mask;
  g.shape = iround(E(t, TP_BSHAPE));
  g.shape = g.shape < 0 ? 0 : g.shape >= BS_N ? BS_N - 1 : g.shape;
  g.ci = ck < 0.005f ? -1 : clock_index(ck);
  g.rate = 1.0f;
  if (g.ci >= 0) {
    float r = note_hz(vnote[t]) * kClockRatio[g.ci] / kSr;
    g.rate = r < 1.0f ? r : 1.0f;
  }
  g.steps = cr < 0.005f ? 0.0f : fm_exp2f(crush_bits(cr) - 1.0f);
  int on = g.shape != BS_OFF || g.rate < 1.0f || g.steps > 0.0f || g.mask != 0;
  int dc = g.shape == BS_RECT || g.shape == BS_SWAP || g.mask != 0;
  if (dc && !g.dc) g.dx = g.dy = 0.0f;
  g.on = on;
  g.dc = dc;
}

inline float bend_word(float x, int mask, int swap) {   // the sample as an 8-bit word, its mask or its halves swapped
  float s = x * 128.0f, f = fm_floorf(s);
  int w = static_cast<int>(f);
  w = w < -128 ? -128 : w > 127 ? 127 : w;
  float frac = s - f;
  uint32_t u = static_cast<uint32_t>(w) & 0xFFu;
  if (swap) u = ((u << 4) | (u >> 4)) & 0xFFu;
  u = (u ^ kMaskInv[mask]) & ~static_cast<uint32_t>(kMaskCut[mask]) & 0xFFu;
  int v = u >= 128u ? static_cast<int>(u) - 256 : static_cast<int>(u);
  if (kMaskCut[mask] & 1u) frac = 0.0f;
  return (static_cast<float>(v) + frac) * (1.0f / 128.0f);
}

inline float bend_one(Bend& g, float x) {
  if (g.shape != BS_OFF && g.shape != BS_SWAP) {
    if (g.shape == BS_RECT) {
      x = 1.6f * fm_fabsf(x);
    } else if (g.shape == BS_FOLD) {
      float u = (3.0f * x + 1.0f) * 0.25f;   // (a period of 4: up from -1 to 1, then back down)
      u -= fm_floorf(u);
      x = 1.0f - 4.0f * fm_fabsf(u - 0.5f);
    } else {                            // DOWN, DOWN 3: a divider on the rising zero crossings (a hysteresis)
      if (!g.pos && x > 0.02f) g.pos = 1, ++g.zc;
      else if (g.pos && x < -0.02f) g.pos = 0;
      int flip = g.shape == BS_DOWN ? (g.zc & 1) : (g.zc % 3 == 2);
      if (g.zc >= 6) g.zc -= 6;
      if (flip) x = -x;
    }
  }
  if (g.rate < 1.0f) {
    g.cnt += g.rate;
    if (g.cnt >= 1.0f) {
      g.cnt -= 1.0f;
      g.hold = x;
    }
    x = g.hold;
  }
  if (g.steps > 0.0f) x = fm_floorf(x * g.steps + 0.5f) / g.steps;
  if (g.mask || g.shape == BS_SWAP) x = bend_word(x, g.mask, g.shape == BS_SWAP);
  if (!g.dc) return x;
  float z = x - g.dx + 0.99857f * g.dy;   // (10 Hz)
  g.dx = x;
  g.dy = z;
  return z;
}

void apply_bend(int t, float* x, uint32_t n) {
  Bend& g = bend[t];
  if (!g.on) return;
  for (uint32_t i = 0; i < n; ++i) x[i] = bend_one(g, x[i]);
}

// the glitch (after Qu-Bit's Data Bender, a circuit-bent buffer): at each sixteenth, GLITCH's chance that the track
// stutters. Its slice (SIZE, halved until it fits) is taken as it plays, then played again and again until a
// sixteenth says otherwise; BEND turns each repeat slower and lower (a tape stopping) or faster and higher; DROP is
// each repeat's chance of an accident, backwards or a hole. The slices in 8-bit mu-law at 44.1 kHz, 8 KB a track
// (186 ms), in the engine's arena: what the reverb's delay lines gave back in 16 bits (the FM-1 had no other 32 KB)
const uint32_t kGlBytes = 8192u;
uint8_t* gl_mem[ENG_TRACKS];
inline uint8_t* gl_buf(int t) { return gl_mem[t]; }
int16_t mu_dec[256];
void make_mu() {
  for (int i = 0; i < 256; ++i) {       // G.711's mu-law
    int u = ~i & 0xFF, sign = u & 0x80, ex = (u >> 4) & 7, man = u & 0x0F;
    int s = (((man << 3) + 0x84) << ex) - 0x84;
    mu_dec[i] = static_cast<int16_t>(sign ? -s : s);
  }
}
inline uint8_t mu_enc(float x) {
  int s = static_cast<int>(x * 8031.0f) << 2;
  s = s > 32635 ? 32635 : s < -32635 ? -32635 : s;
  int sign = s < 0 ? 0x80 : 0;
  if (sign) s = -s;
  s += 0x84;
  int ex = 7;
  for (int m = 0x4000; ex > 0 && !(s & m); m >>= 1) --ex;
  int man = (s >> (ex + 3)) & 0x0F;
  return static_cast<uint8_t>(~(sign | (ex << 4) | man) & 0xFF);
}
const float kMuScale = 1.0f / (8031.0f * 4.0f);

struct Glitch {
  int on;                               // 0 live, 1 taking its slice (the live goes on), 2 repeating
  uint32_t len, rec;                    // the slice's samples, those taken
  float pos, speed;                     // the repeat's place, its speed
  int rev, mute, reps;
  float mix;                            // the repeats over the live (0..1), glided
};
Glitch glitch[ENG_TRACKS];
void glitch_stop_all() {                // (the transport stopped: the repeats fade back to the live)
  for (int t = 0; t < ENG_TRACKS; ++t) glitch[t].on = 0;
}

void glitch_event(int t) {              // a sixteenth: repeat (or go on repeating), or back to the live
  Glitch& g = glitch[t];
  float p = E(t, TP_GLITCH);
  if (playing && p > 0.005f && rand01() < p) {
    if (g.on) return;
    int sz = iround(E(t, TP_GSIZE));
    float len = gl_six * (sz <= GL_64 ? 0.25f : sz == GL_32 ? 0.5f : sz == GL_16 ? 1.0f : 2.0f);
    while (len > static_cast<float>(kGlBytes) && len > 64.0f) len *= 0.5f;
    g.len = static_cast<uint32_t>(len);
    if (g.len < 32u) g.len = 32u;
    g.rec = 0u;
    g.pos = 0.0f;
    g.speed = 1.0f;
    g.rev = g.mute = 0;
    g.reps = 0;
    g.on = 1;
  } else {
    g.on = 0;
  }
}

void apply_glitch(int t, float* x, uint32_t n) {
  Glitch& g = glitch[t];
  if (!g.on && g.mix <= 0.0f && gl_at[t] < 0) return;
  uint8_t* buf = gl_buf(t);
  float bendv = E(t, TP_GBEND), drop = E(t, TP_GDROP);
  float mult = fm_exp2f(bendv * 0.5f);  // (each repeat: up to half an octave lower or higher)
  for (uint32_t i = 0; i < n; ++i) {
    if (static_cast<int>(i) == gl_at[t]) glitch_event(t);
    float live = x[i], rep = 0.0f, target = 0.0f;
    if (g.on == 1) {                    // taking the slice: the live heard
      buf[g.rec++] = mu_enc(live);
      if (g.rec >= g.len) g.on = 2, g.pos = 0.0f;
    } else if (g.on == 2) {             // repeating
      float L = static_cast<float>(g.len);
      float q = g.rev ? L - 1.0f - g.pos : g.pos;
      int j = static_cast<int>(q);
      if (j < 0) j = 0;
      uint32_t j1 = static_cast<uint32_t>(j) + 1u < g.len ? static_cast<uint32_t>(j) + 1u : static_cast<uint32_t>(j);
      float fr = q - static_cast<float>(j);
      float s = (static_cast<float>(mu_dec[buf[j]]) + fr * static_cast<float>(mu_dec[buf[j1]] - mu_dec[buf[j]])) * kMuScale;
      float e = fm_minf(1.0f, fm_minf(g.pos, L - 1.0f - g.pos) * (1.0f / 24.0f));   // (the slice's ends faded)
      rep = g.mute ? 0.0f : s * fm_maxf(e, 0.0f);
      target = 1.0f;
      g.pos += g.speed;
      if (g.pos >= L - 1.0f) {          // the next repeat: its speed, its accident
        g.pos = 0.0f;
        ++g.reps;
        g.speed = clampf(g.speed * mult, 0.125f, 4.0f);
        g.rev = g.mute = 0;
        if (drop > 0.005f && rand01() < drop) {
          if (rand01() < 0.6f) g.rev = 1;
          else g.mute = 1;
        }
        if (g.speed < 0.16f) g.mute = 1;   // (the tape stopped)
      }
    }
    g.mix += (target - g.mix) * 0.03f;  // (~1 ms)
    if (g.mix < 1e-4f && !g.on) g.mix = 0.0f;
    x[i] = live + (rep - live) * g.mix;
  }
  gl_at[t] = -1;
  gl_busy[t] = g.on || g.mix > 0.0f;
}

volatile uint8_t filt_stop;             // the transport stopped: the filters to close
volatile uint32_t filt_ticks[ENG_TRACKS];   // the filter's time in its voice's task, the last block (the screen's)
float filt_cost[ENG_TRACKS];
const float mf_off_knobs[MF_KNOBS] = {1.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

// a track's filter (PG_FILT, mi_filter.cc): its model and type (the pages' ALGORITHM), its knobs as the trig sees
// them (locks, randoms, the dose, the cables), the note for its KEY
void set_filter(int t) {
  int m = proj.algo[PG_FILT(t, 0)];   // (TYPE: not a lock's nor a random's, a new type starts it again)
  if (m <= MF_OFF || m >= MF_MODELS) {
    mf_set(t, MF_OFF, MF_LP, mf_off_knobs, 60.0f);
    return;
  }
  float k[MF_KNOBS];
  for (int i = 0; i < MF_KNOBS; ++i) k[i] = E(t, TP_CUTOFF + i);
  if (!proj.dice) dice_type[t] = -1;
  mf_set(t, m, dice_type[t] >= 0 ? dice_type[t] : iround(proj.val[PG_FILT(t, 2) * 4]), k, vnote[t]);
}

void render_voice(int t, uint32_t n) {
  if (voice_rests(t)) {
    WEB_SPEND(kWebRest);
    for (uint32_t i = 0; i < n; ++i) vbuf[t][i] = 0.0f;
    bend[t].hold = bend[t].cnt = 0.0f;
    glitch[t].on = 0;
    glitch[t].mix = 0.0f;
    gl_at[t] = -1;
    gl_busy[t] = 0;
    mf_reset(t);
    return;
  }
#ifdef PLAT_WEB
  {                                     // (its model, its bits if any of them is up, its glitch)
    int m = proj.algo[PG_TRACK(t, 0)];
    float fm = 0.0f;                    // (the FM: its square, its PW or SYNC)
    if (track_kind(t) == MK_FM) {
      float mu = E(t, TP_PITCHENV);
      fm = (E(t, 7) < -0.004f ? kWebFm[0] : 0.0f) + (mu < -0.004f ? kWebFm[1] : mu > 0.004f ? kWebFm[2] : 0.0f);
    }
    web_spend((kWebModel[m < 0 ? 0 : m > ENG_MODELS - 1 ? ENG_MODELS - 1 : m] + fm + (bend[t].on ? kWebBend : 0.0f) +
               (glitch[t].on || glitch[t].mix > 0.0f ? kWebGlitch : 0.0f)) * static_cast<float>(n) / 128.0f);
  }
#endif
  int kind = track_kind(t);             // an FM drum: a rise triggers it, its own envelope does the rest; ZERO, a
  int lx = kind == MK_LXR, ac = kind == MK_ZERO, dr = kind == MK_DRUM, fm = kind == MK_FM;   // drum, the FM: their
                                        // gate, their own render
  uint32_t step = n / kFrames, pos = 0;
  for (uint32_t k = 0; k < kFrames; ++k) {
    int g = fg[t][k];
    if (g == vgate[t]) continue;
    uint32_t at = k * step;
    if (at > pos) {
      if (lx) lx_render(t, &vbuf[t][pos], at - pos);
      else if (ac) zero_render(t, &vbuf[t][pos], at - pos);
      else if (dr) drum_render(t, &vbuf[t][pos], at - pos);
      else if (fm) fm4_render(t, &vbuf[t][pos], at - pos);
      else mp_render(voice[t], &vbuf[t][pos], &vaux[t][pos], at - pos);
      pos = at;
    }
    if (g) {
      vnote[t] = fnote[t][k];
      set_voice(t);
      ++vrises[t];
      set_filter(t);                    // (its KEY on the new note)
      mf_trigger(t, at);                // (the filter's envelope and ping, at that sample)
    }
    if (ac) zero_gate(t, g);
    else if (dr) drum_gate(t, g);
    else if (fm) fm4_gate(t, g);
    else if (!lx) mp_gate(voice[t], g);
    else if (g) lx_trigger(t);
    vgate[t] = g;
  }
  if (n > pos) {
    if (lx) lx_render(t, &vbuf[t][pos], n - pos);
    else if (ac) zero_render(t, &vbuf[t][pos], n - pos);
    else if (dr) drum_render(t, &vbuf[t][pos], n - pos);
    else if (fm) fm4_render(t, &vbuf[t][pos], n - pos);
    else mp_render(voice[t], &vbuf[t][pos], &vaux[t][pos], n - pos);
  }
  apply_bend(t, vbuf[t], n);
  uint32_t fq = plat_ticks24();
#ifdef PLAT_WEB
  {
    static const float kGuardFm[4] = {1.0f, 0.55f, 0.3f, 0.0f};   // (the guard: the FM's coefficients fewer)
    int fm = proj.algo[PG_FILT(t, 0)];
    if (fm > MF_OFF && fm < MF_MODELS)
      web_spend((kWebFilter[fm][0] + (fm_fabsf(E(t, TP_FM)) > 0.005f ? kWebFilter[fm][1] * kGuardFm[guard & 3] : 0.0f) +
                 (E(t, TP_NOISE) > 0.005f ? kWebFilter[fm][2] : 0.0f)) * static_cast<float>(n) / 128.0f);
  }
#endif
  mf_process(t, vbuf[t], n);            // the filter (OFF: nothing)
  filt_ticks[t] = plat_ticks24() - fq;
  apply_glitch(t, vbuf[t], n);          // (its glitch: after the filter, the track's sound repeated)
  float pk = 0.0f;
  for (uint32_t i = 0; i < n; ++i) pk = fm_maxf(pk, fm_fabsf(vbuf[t][i]));
  quiet[t] = !vgate[t] && pk < 3e-5f && !glitch[t].on ? quiet[t] + 1 : 0;   // (a hole the glitch made: not a rest)
}

// the mix, the effects and the master bus, a block behind (the output 2.9 ms later): they take the last block's
// voices (mbuf) while the voices play this one, on either core. What the mix needs was taken once the voices were
// done (core 0 alone: only the gains, per track): MixJob
float fx_dry_l[kN], fx_dry_r[kN], fx_rsend[kN], fx_dsend[kN], fx_out_l[kN], fx_out_r[kN];
float fx_ret0, fx_ret1, fx_dret0, fx_dret1, fx_pk[2];
uint32_t fx_n;                           // its frames (0: none yet)
int fx_mode;                             // the output's mode, as the mix was taken

struct MixJob {
  int idle;                              // Warps shut (its oscillator, stopped): the tracks as they are
  int dst, mute, job;                    // Warps: the track its output replaces (-1: none), the one it silences, its
  float o0, o1;                          // output there (0: silence, not yet run); its lane's opening over the block
  float gl[ENG_TRACKS], gr[ENG_TRACKS], send[ENG_TRACKS], dsend[ENG_TRACKS];       // the gains from
  float dgl[ENG_TRACKS], dgr[ENG_TRACKS], dsend_d[ENG_TRACKS], ddsend_d[ENG_TRACKS];   // and their steps a sample
  float gain[ENG_TRACKS];                // the level (the meters')
  float duck0[ENG_TRACKS], duck1[ENG_TRACKS], duck_rel;   // the duck: each track's depth from, to; DECAY a sample
  float eq[ENG_TRACKS];                  // the filter's knob (glided)
  uint8_t hit[kFrames];                  // T1's rises, frame by frame
  float pk[ENG_TRACKS];                  // (out: each track's peak, the meters')
};
MixJob mj;
// the filter (PG_EQ): a track's two of stmlib's state variable filters (24 dB/oct, Butterworth: Q 0.54 and 1.31),
// made in place by eng_init (no static constructor, no destructor to register), its mode now (0 none, 1 low pass, 2
// high pass), its knob glided
struct EqPair {
  stmlib::Svf f[2];
};
alignas(8) uint8_t eq_mem[ENG_TRACKS][sizeof(EqPair)];
EqPair* eq_p[ENG_TRACKS];
int eq_mode[ENG_TRACKS];
float eq_s[ENG_TRACKS];

void mix_job(uint32_t n) {
  float inv = 1.0f / static_cast<float>(n);
  if (mj.dst >= 0 && !mj.idle) {         // Warps' output in its track's place, the modulator's track silenced
    float o = mj.o0, d = (mj.o1 - mj.o0) * inv;
    float* x = mbuf[mj.dst];
    for (uint32_t i = 0; i < n; ++i) {
      o += d;
      x[i] = mj.job ? m_dry[i] + o * (m_out[i] - m_dry[i]) : 0.0f;   // (the first block of a new patching: silence,
      if (mj.mute >= 0) mbuf[mj.mute][i] *= 1.0f - o;                // Warps not yet run)
    }
  }
  for (int t = 0; t < ENG_TRACKS; ++t) {   // the filter (before the duck and the sends: what a track sends is
    float v = mj.eq[t], pk = 0.0f;         // filtered with it)
    int mode = v <= -kEqDead ? 1 : v >= kEqDead ? 2 : 0;
    float* x = mbuf[t];
    if (mode)
      for (uint32_t i = 0; i < n; ++i) pk = fm_maxf(pk, fm_fabsf(x[i]));
    stmlib::Svf* ef = eq_p[t]->f;
    if (mode != eq_mode[t] || pk < 1e-6f) {   // (another mode, or silence: the states from zero, never down into
      ef[0].Init();                        // the subnormals)
      ef[1].Init();
      eq_mode[t] = mode;
    }
    if (!mode || pk < 1e-6f) continue;
    float f = clampf(eq_hz(v) / kSr, 0.0004f, 0.41f);
    ef[0].set_f_q<stmlib::FREQUENCY_ACCURATE>(f, 0.5412f);
    ef[1].set_f_q<stmlib::FREQUENCY_ACCURATE>(f, 1.3066f);
    if (mode == 1) {
      ef[0].Process<stmlib::FILTER_MODE_LOW_PASS>(x, x, n);
      ef[1].Process<stmlib::FILTER_MODE_LOW_PASS>(x, x, n);
    } else {
      ef[0].Process<stmlib::FILTER_MODE_HIGH_PASS>(x, x, n);
      ef[1].Process<stmlib::FILTER_MODE_HIGH_PASS>(x, x, n);
    }
  }
  {                                     // the duck: T1's rises (their frames), DECAY, each track's depth
    float rel = mj.duck_rel;
    uint32_t fr = n / kFrames;
    for (uint32_t i = 0; i < n; ++i) {
      if (i % fr == 0 && mj.hit[i / fr]) duck_d = 1.0f;
      else duck_d *= rel;
      duck_s += (duck_d - duck_s) * 0.03f;
      duckv[i] = duck_s;
    }
    if (duck_s < 1e-5f && duck_d < 1e-5f) duck_s = duck_d = 0.0f;   // (never down into the subnormals: slow)
    for (int t = 1; t < ENG_TRACKS; ++t) {   // (before the sends: what a track sends ducks with it)
      float d0 = mj.duck0[t], d1 = mj.duck1[t], dd = (d1 - d0) * inv;
      if (d0 <= 0.0f && d1 <= 0.0f) continue;
      for (uint32_t i = 0; i < n; ++i) {
        d0 += dd;
        mbuf[t][i] *= 1.0f - d0 * duckv[i];
      }
    }
  }
  float pk[ENG_TRACKS] = {0, 0, 0, 0};
  float sends[ENG_TRACKS], dsends[ENG_TRACKS], gl[ENG_TRACKS], gr[ENG_TRACKS];
  for (int t = 0; t < ENG_TRACKS; ++t) {
    sends[t] = mj.send[t];
    dsends[t] = mj.dsend[t];
    gl[t] = mj.gl[t];
    gr[t] = mj.gr[t];
  }
  for (uint32_t i = 0; i < n; ++i) {
    float s = 0.0f, ds = 0.0f;
    for (int t = 0; t < ENG_TRACKS; ++t) {
      sends[t] += mj.dsend_d[t];
      dsends[t] += mj.ddsend_d[t];
      s += sends[t] * mbuf[t][i];
      ds += dsends[t] * mbuf[t][i];
      float a = fm_fabsf(mbuf[t][i] * mj.gain[t]);
      if (a > pk[t]) pk[t] = a;
    }
    fx_rsend[i] = s;
    fx_dsend[i] = ds;
  }
  for (uint32_t i = 0; i < n; ++i) {
    float dl = 0.0f, dr = 0.0f;
    for (int t = 0; t < ENG_TRACKS; ++t) {
      gl[t] += mj.dgl[t];
      gr[t] += mj.dgr[t];
      dl += gl[t] * mbuf[t][i];
      dr += gr[t] * mbuf[t][i];
    }
    fx_dry_l[i] = dl;
    fx_dry_r[i] = dr;
  }
  for (int t = 0; t < ENG_TRACKS; ++t) mj.pk[t] = pk[t];
  if (!usb_now) return;
  for (int t = 0; t < ENG_TRACKS; ++t) {  // the USB input's tracks: the same gains, sample by sample
    float a = mj.gl[t], b = mj.gr[t];
    const float* x = mbuf[t];
    int16_t* u = usb_ch + 2 * t;
    for (uint32_t i = 0; i < n; ++i, u += ENG_USB_CH) {
      a += mj.dgl[t];
      b += mj.dgr[t];
      u[0] = usb16(a * x[i]);
      u[1] = usb16(b * x[i]);
    }
  }
}

void fx_job(uint32_t n) {
#ifdef PLAT_WEB
  {                                     // (the bus's mode; the EQ of the tracks playing, the duck, the delay's filter,
    int bus = proj.algo[PG_COMP] > MB_OTT ? static_cast<int>(MB_COMP) : proj.algo[PG_COMP];   // Warps in the mix)
    float us = kWebFx[bus];
    for (int t = 0; t < ENG_TRACKS; ++t) {
      if ((mj.eq[t] <= -kEqDead || mj.eq[t] >= kEqDead) && quiet[t] < kQuiet) us += kWebFxEq;
      if (t && (mj.duck0[t] > 0.0f || mj.duck1[t] > 0.0f)) us += kWebFxDuck;
    }
    if (fm_fabsf(E(LANE_DELAY, 2)) > 0.01f) us += kWebFxDelay;
    if (mj.dst >= 0 && !mj.idle) us += kWebFxWarps;
    web_spend(us * static_cast<float>(n) / 128.0f);
  }
#endif
  mix_job(n);
  float inv = 1.0f / static_cast<float>(n), ret = fx_ret0, dret = (fx_ret1 - fx_ret0) * inv, eret = fx_dret0;
  float deret = (fx_dret1 - fx_dret0) * inv, pr = 0.0f, pd = 0.0f;
  mrv_process(reverb, fx_rsend, wet_l, wet_r, n);
  mdl_process(delay, fx_dsend, echo_l, echo_r, n);
  for (uint32_t i = 0; i < n; ++i) {
    ret += dret;
    eret += deret;
    float wl = ret * wet_l[i], wr = ret * wet_r[i], el = eret * echo_l[i], er = eret * echo_r[i];
    pr = fm_maxf(pr, fm_maxf(fm_fabsf(wl), fm_fabsf(wr)));
    pd = fm_maxf(pd, fm_maxf(fm_fabsf(el), fm_fabsf(er)));
    fx_out_l[i] = fx_dry_l[i] + wl + el;
    fx_out_r[i] = fx_dry_r[i] + wr + er;
  }
  fx_pk[0] = pr;
  fx_pk[1] = pd;
  if (usb_now) {                        // the USB input's effects' return: the same ramps
    float a = fx_ret0, b = fx_dret0;
    int16_t* u = usb_ch + 2 * ENG_TRACKS;
    for (uint32_t i = 0; i < n; ++i, u += ENG_USB_CH) {
      a += dret;
      b += deret;
      u[0] = usb16(a * wet_l[i] + b * echo_l[i]);
      u[1] = usb16(a * wet_r[i] + b * echo_r[i]);
    }
  }
#ifdef PLAT_HOST
  for (uint32_t i = 0; i < n; ++i) {    // (the tests: the master before its bus, what the USB channels sum to)
    eng_test_prebus[0][i] = fx_out_l[i];
    eng_test_prebus[1][i] = fx_out_r[i];
  }
#endif
  mms_process(master, fx_out_l, fx_out_r, n);   // the master bus: the compressor or OTT, the limiter
  for (uint32_t i = 0; i < n; ++i) {     // then a soft limit, the output's mode
    float ol = sat(fx_out_l[i]), orr = sat(fx_out_r[i]);
    if (fx_mode == 1) {                 // the right inverted: a headset with a mic (a 4-pole plug in the 3-pole jack)
                                        // has its earpieces in series between left and right, they get left minus
                                        // right: the centre vanishes. Left plus right then reaches them: the whole mix
      orr = -orr;
    } else if (fx_mode == 2) {          // mono
      ol = orr = 0.5f * (ol + orr);
    }
    fx_out_l[i] = ol;
    fx_out_r[i] = orr;
  }
}

volatile uint32_t job_n;
// the render's six tasks (the four voices, Warps, the effects and the master bus), shared between the cores: each one
// timed where it ran, its cost averaged (a voice's while it plays), and each block they are split so that the busier
// core has the least to do (a modal voice costs ten times a virtual analog one)
const int kTasks = ENG_TRACKS + 2;
const int kTaskWarps = ENG_TRACKS, kTaskFx = ENG_TRACKS + 1;
volatile uint32_t task_ticks[kTasks];
float task_cost[kTasks] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
float warps_chunk_cost = 1.0f;           // Warps' cost a Process call: a block makes 2 or 3 of them (mw_calls)
uint32_t warps_calls;

void run_task(int i, uint32_t n) {
  uint32_t q = plat_ticks24();
  if (i < ENG_TRACKS) render_voice(i, n);
  else if (i == kTaskWarps) {
#ifdef PLAT_WEB
    int a = eng_warps_algo();
    WEB_SPEND(kWebWarps[proj.warps_hq ? 1 : 0][a < 0 ? 0 : a > 5 ? 5 : a] * static_cast<float>(n) / 128.0f);
#endif
    mw_render(warps, w_car, w_mod, w_din, w_out, w_dry, n);
  }
  else fx_job(fx_n);
  task_ticks[i] = plat_ticks24() - q;
}

// core 1's voices, offered while it plays its first job (core 0 posts them once it has prepared the block): it takes
// them as that job ends, instead of waiting idle for core 0 to hand them over between two of its own tasks (on the
// FM-1, up to a voice's time: the stress test's peak was 96 % with both cores busy 70 %). The offer carries the
// block's number and its complement (a misread fails it: then core 0 hands them over, or plays them); core 1 says
// it took them (claim), read once its job is done
volatile uint32_t offer, offer_chk, claim, block_no;

// the second core's job: tasks (bit i: task i), the block's first or second job (bit 8), the block's number (bits 16..)
void core1_job(uint32_t arg) {
  uint32_t t0 = plat_ticks24();
  for (int i = 0; i < kTasks; ++i)
    if ((arg >> i) & 1u) run_task(i, job_n);
  if (!((arg >> 8) & 1u)) {             // the first job: the voices offered meanwhile, at once
    plat_cpu2_sync();
    uint32_t o = offer, k = offer_chk, b = arg >> 16;
    if (o == ~k && (o >> 8) == b && (o & 0xF0u) == 0u && (o & 0x0Fu)) {
      claim = b;
      for (int i = 0; i < ENG_TRACKS; ++i)
        if ((o >> i) & 1u) run_task(i, job_n);
    }
  }
  c1_job_ticks[(arg >> 8) & 1u] = plat_ticks24() - t0;
}

// each set of tasks' cost, made once a block (0.8.4): the splits ask it some 150 times a block, and summing the set
// each time was 4 % of a block on core 0 (its preparation runs from the flash, through the cache). The same sums as one
// task at a time from the lowest: bit for bit what they were
float sub_cost[1u << kTasks];
void make_sub_costs() {
  int hi = 0;
  sub_cost[0] = 0.0f;
  for (uint32_t m = 1; m < (1u << kTasks); ++m) {
    if (m == 2u << hi) ++hi;            // (its highest task)
    sub_cost[m] = sub_cost[m & ~(1u << hi)] + task_cost[hi];
  }
}
inline float cost_of(uint32_t m) { return sub_cost[m & ((1u << kTasks) - 1u)]; }

// the voices core 1 plays (its share of v), it free at a, core 0 at b: the busier core's least (16 ways at most)
uint32_t split_voices(uint32_t v, float a, float b, float* worst) {
  float best = 1e30f;
  uint32_t pick = 0;
  for (uint32_t m = 0; m < (1u << ENG_TRACKS); ++m) {
    if (m & ~v) continue;
    float c1 = a + cost_of(m), c0 = b + cost_of(v & ~m), w = c0 > c1 ? c0 : c1;
    if (w < best - 0.5f) {               // (a tie: the first found, so the split stays put)
      best = w;
      pick = m;
    }
  }
  if (worst) *worst = best;
  return pick;
}

// what core 1 takes at once, before core 0 has prepared the block: some of the early tasks (the effects, Warps:
// they play the last block, nothing of this one), so that it reaches the voices when core 0 does (the voices: the
// last block's, a guess)
uint32_t split_early(uint32_t early, uint32_t voices) {
  float best = 1e30f, w;
  uint32_t pick = 0;
  for (uint32_t a = 0; a < (1u << kTasks); ++a) {
    if (a & ~early) continue;
    split_voices(voices, cost_of(a), prep_cost + cost_of(early & ~a), &w);
    if (w < best - 0.5f) {
      best = w;
      pick = a;
    }
  }
  return pick;
}

// Warps' knobs for this block (its lane's locks over them), and whether it runs: a carrier (a track or its
// oscillator), its modulator a track
void set_warps() {
  int car = Pi(PG_WARPS(0), 0), mod = Pi(PG_WARPS(0), 1), shift = Pi(PG_WARPS(1), 3) == WM_SHIFT;
  mod = mod < 0 ? 0 : mod > 3 ? 3 : mod;
  int osc = car >= WC_OSC1 ? car - WC_OSC1 + 1 : 0;
  int ready = car > WC_OFF && car <= WC_OSC3 && w_last == (car | mod << 4), shut = car >= WC_OSC1 && w_gate <= 0.0f;
  w_job = ready && !shut;
  w_idle = ready && shut;
  float alg = E(LANE_WARPS, 2), level1 = E(LANE_WARPS, 5);
  float note = scale_on() ? static_cast<float>(warps_note(level1)) : 12.0f + 60.0f * level1;
  mw_set(warps, shift ? alg : alg * kWarpsTravel, E(LANE_WARPS, 4), level1, E(LANE_WARPS, 6), osc, note,
         shift);   // the oscillator's note: LEVEL 1, as on the module (with SCALE on, in it)
  mw_eco(warps, !proj.warps_hq);        // ECO unless HQ: its cross-modulations at 3x
}

// after the voices: this block's carrier, modulator and the track Warps replaces, kept for the next block's job;
// the mix (mix_job) puts Warps' output (this block's, warped from the last) in its track's place, crossfaded with
// that track (as late) by Warps' lane from o0 to o1 over the block, the modulator's track silenced as Warps opens,
// unless heard dry
void route_warps(uint32_t n, float o0, float o1) {
  int car = Pi(PG_WARPS(0), 0), mod = Pi(PG_WARPS(0), 1);
  mod = mod < 0 ? 0 : mod > 3 ? 3 : mod;
  if (car <= WC_OFF || car > WC_OSC3) {
    w_last = w_dst = w_mute = mj.dst = -1;
    return;
  }
  int ct = car <= WC_T4 ? car - WC_T1 : -1, dst = ct >= 0 ? ct : mod;
  int mute = mod != dst && Pi(PG_WARPS(1), 3) != WM_DRY ? mod : -1;
  float mix = E(LANE_WARPS, 3);         // MIX: how much of Warps over its track (the lane's opening times it)
  int on = car < WC_OSC1 || playing;    // its own oscillator: while the steps run, or a key plays
  for (int t = 0; t < ENG_TRACKS; ++t) on |= key_on[t] || key_hold[t] > 0;
  w_gate = on ? 1.0f : fm_maxf(w_gate - 0.1f, 0.0f);
  for (uint32_t i = 0; i < n; ++i) {
    w_car[i] = ct >= 0 ? vbuf[ct][i] : 0.0f;
    w_mod[i] = vbuf[mod][i];
    w_din[i] = vbuf[dst][i];
  }
  mj.dst = w_dst = dst;
  mj.mute = w_mute = mute;
  mj.job = w_job;
  mj.idle = w_idle;
  mj.o0 = o0 * w_mix * w_gmix;          // (from where the last mix ended: back from shut, faded in over a block)
  mj.o1 = o1 * mix * w_gate;
  w_gmix = w_idle ? 0.0f : w_gate;
  w_mix = mix;
  w_last = car | mod << 4;
}

uint32_t pct10(uint32_t ticks, uint32_t n) {   // ticks as tenths of a percent of n frames' time
  float block_ticks = static_cast<float>(n) * (static_cast<float>(PLAT_TICKS_PER_US) * 1000000.0f / kSr);
  float p = static_cast<float>(ticks) * 1000.0f / block_ticks;
  return static_cast<uint32_t>(p > 65000.0f ? 65000.0f : p);
}

}  // namespace

extern "C" {
#ifdef ENG_PROFILE
uint32_t eng_prof[3];                    // (a computer's measure: the preparation, the mix, the whole render)
#endif

int eng_page_type(int page) { return page_type(page); }
int eng_page_track(int page) {
  if (page < 12) return page / 3;
  if (page >= PG_BEND(0) && page < PG_BEND(ENG_TRACKS)) return page - PG_BEND(0);
  if (page >= PG_GLITCH(0) && page < PG_GLITCH(ENG_TRACKS)) return page - PG_GLITCH(0);
  if (page >= PG_FILT(0, 0) && page < PG_FILT(ENG_TRACKS, 0)) return (page - PG_FILT(0, 0)) / 3;
  if (page >= PG_EUC(0) && page < PG_EUC(ENG_TRACKS)) return page - PG_EUC(0);
  if (page >= PG_ARP(0) && page < PG_ARP(0) + ENG_TRACKS) return page - PG_ARP(0);
  return page >= PG_STEP(0) && page < PG_STEP(ENG_TRACKS) ? page - PG_STEP(0) : -1;
}
int eng_page_lane(int page) {
  int t = eng_page_track(page);
  if (t >= 0) return t;
  if (page == PG_DELAY(0) || page == PG_DELAY(1) || page == PG_DSEND) return LANE_DELAY;
  if (page == PG_REVERB || page == PG_RSEND) return LANE_REVERB;
  if (page == PG_WARPS(0) || page == PG_WARPS(1)) return LANE_WARPS;
  if (page >= PG_STEP(ENG_TRACKS) && page < PG_STEP(ENG_LANES)) return page - PG_STEP(0);
  if (page >= PG_EUC(ENG_TRACKS) && page < PG_EUC(ENG_LANES)) return page - PG_EUC(0);
  return -1;
}

// the Euclidean rhythm: fills spread over len steps as evenly as they go (step i has one when i * fills, modulo len,
// is under fills: the first step always), then turned rot steps later
int eng_euclid(int len, int fills, int rot, int step) {
  if (len < 1 || fills < 1 || step < 0 || step >= len) return 0;
  if (fills >= len) return 1;
  int i = ((step - rot) % len + len) % len;
  return (i * fills) % len < fills;
}

int eng_lane_params(int lane) {
  if (lane < 0 || lane >= ENG_LANES) return 0;
  return lane < ENG_TRACKS ? ENG_NTP : kLaneKnobs[lane] + 3;
}

int eng_track_param(int lane, int tp) {
  if (lane < 0 || lane >= ENG_LANES || tp < 0 || tp >= eng_lane_params(lane)) return 0;
  if (lane >= ENG_TRACKS) {
    int kk = kLaneKnobs[lane];
    if (tp >= kk) return PG_STEP(lane) * 4 + (tp - kk);
    return lane_page0(lane) * 4 + tp;   // (the delay's two pages follow each other)
  }
  if (tp < 12) return PG_TRACK(lane, tp / 4) * 4 + tp % 4;
  if (tp == TP_LEVEL) return PG_MIX * 4 + lane;
  if (tp == TP_SEND) return PG_RSEND * 4 + lane;
  if (tp == TP_PAN) return PG_PAN * 4 + lane;
  if (tp == TP_DSEND) return PG_DSEND * 4 + lane;
  if (tp == TP_DUCK) return PG_DUCK * 4 + lane;   // (T1's: the duck's DECAY, not a parameter of T1's: not lockable)
  if (tp == TP_EQ) return PG_EQ * 4 + lane;
  if (tp >= TP_GLITCH) return PG_GLITCH(lane) * 4 + (tp - TP_GLITCH);
  if (tp >= TP_CUTOFF) return PG_FILT(lane, (tp - TP_CUTOFF) / 4) * 4 + (tp - TP_CUTOFF) % 4;
  if (tp >= TP_CLOCK) return PG_BEND(lane) * 4 + (tp - TP_CLOCK);
  return PG_STEP(lane) * 4 + (tp - TP_NOTE);
}

int eng_param_tp(int param, int* lane) {
  int page = param / 4, k = param % 4, l = -1, tp = -1;
  if (param < 0 || param >= ENG_NPARAMS) {
  } else if (page < 12) {
    l = page / 3;
    tp = (page % 3) * 4 + k;
  } else if (page == PG_MIX || page == PG_RSEND || page == PG_PAN || page == PG_DSEND) {
    l = k;
    tp = page == PG_MIX ? TP_LEVEL : page == PG_PAN ? TP_PAN : page == PG_RSEND ? TP_SEND : TP_DSEND;
  } else if (page >= PG_STEP(0) && page < PG_STEP(ENG_TRACKS)) {
    if (k < 3) l = page - PG_STEP(0), tp = TP_NOTE + k;
  } else if (page >= PG_STEP(ENG_TRACKS) && page < PG_STEP(ENG_LANES)) {
    if (k < 3) l = page - PG_STEP(0), tp = kLaneKnobs[l] + k;
  } else if (page == PG_DELAY(0) || page == PG_DELAY(1)) {
    l = LANE_DELAY;
    tp = (page - PG_DELAY(0)) * 4 + k;
  } else if (page == PG_REVERB) {
    l = LANE_REVERB;
    tp = k;
  } else if (page == PG_WARPS(0) || page == PG_WARPS(1)) {
    l = LANE_WARPS;
    tp = (page - PG_WARPS(0)) * 4 + k;
  } else if (page == PG_DUCK && k > 0) {
    l = k;
    tp = TP_DUCK;
  } else if (page == PG_EQ) {
    l = k;
    tp = TP_EQ;
  } else if (page >= PG_BEND(0) && page < PG_BEND(ENG_TRACKS)) {
    l = page - PG_BEND(0);
    tp = TP_CLOCK + k;
  } else if (page >= PG_GLITCH(0) && page < PG_GLITCH(ENG_TRACKS)) {
    l = page - PG_GLITCH(0);
    tp = TP_GLITCH + k;
  } else if (page >= PG_FILT(0, 0) && page < PG_FILT(ENG_TRACKS, 0)) {
    l = (page - PG_FILT(0, 0)) / 3;
    tp = TP_CUTOFF + ((page - PG_FILT(0, 0)) % 3) * 4 + k;
  }
  if (lane) *lane = l;
  return tp;
}

int eng_tp_lockable(int lane, int tp) {   // (not Warps' patching: CARRIER, MOD, MODE; nor a filter's TYPE)
  return tp >= 0 && tp < eng_lane_params(lane) && !(lane == 0 && tp == TP_DUCK) && !(lane < ENG_TRACKS && tp == TP_FTYPE) &&
         !(lane == LANE_WARPS && (tp == 0 || tp == 1 || tp == 7));
}

// Plaits' HARMONICS, TIMBRE and MORPH by model, after its manual (1.2's eight from its release notes), short
const char* const kModelKnobs[24][3] = {
  {"Reso", "Cutoff", "Wave"}, {"Dist F", "Amount", "Asym"}, {"Preset", "Mod", "Time"}, {"Preset", "Mod", "Time"},
  {"Preset", "Mod", "Time"}, {"Terrain", "Radius", "Offset"}, {"Chord", "Chorus", "Wave"}, {"Chord", "Arp", "PW"},
  {"Detune", "Square", "Saw"}, {"Shape", "Fold", "Asym"}, {"Ratio", "Index", "Fdbk"}, {"Ratio", "Form", "Width"},
  {"Bumps", "Peak", "Shape"}, {"Bank", "Row", "Column"}, {"Chord", "Invert", "Wave"}, {"Synth", "Species", "Vowel"},
  {"Detune", "Density", "Grain"}, {"Filter", "Clock", "Reso"}, {"Random", "Density", "Filter"},
  {"Struct", "Bright", "Decay"}, {"Struct", "Bright", "Decay"}, {"Attack", "Bright", "Decay"},
  {"Noise", "Tone", "Decay"}, {"Metal", "HPF", "Decay"},
};

int eng_lx_knob(int param) {            // an FM drum's knob (lx_knob): which, -1 none
  if (param < 0 || param >= 12 * 4) return -1;
  int t = param / 12, i = param - PG_TRACK(t, 0) * 4 - 1;   // (its track's first page's second knob: 0)
  return own_knobs(t) && i >= 0 && i < LX_KNOBS ? i : -1;
}

const char* eng_param_name(int param) {
  int lk = eng_lx_knob(param);
  if (lk >= 0) {
    int t = param / 12;
    return zero_on(t) ? zero_knob_name(lk) : track_kind(t) == MK_DRUM ? drum_knob_name(lk)
                                           : track_kind(t) == MK_FM ? fm4_knob_name(lk)
                                                                     : lx_knob_name(lx_model(t), lk);
  }
  if (param >= 0 && param < 12 * 4 && (param / 4) % 3 == 0 && param % 4 >= 1) {
    int m = track_sub(param / 12);
    return kModelKnobs[m < 0 || m > 23 ? 0 : m][param % 4 - 1];
  }
  return eng_pdef(param)->name;
}

int eng_model_enveloped(int m) {        // (Plaits' string, modal; the models with their own knobs)
  const ModelDef& d = mdef(m);
  return d.kind != MK_PLAITS || d.sub >= 19;
}
int eng_model_kind(int m) { return mdef(m).kind; }
int eng_model_sub(int m) { return mdef(m).sub; }
int eng_model_bank(int m) { return mdef(m).bank; }
const char* eng_model_abbr(int m) { return mdef(m).abbr; }
const char* eng_bank_name(int b) { return b >= 0 && b < ENG_BANKS ? kBankNames[b] : ""; }
int eng_fm_algo(int t) { return t >= 0 && t < ENG_TRACKS && track_kind(t) == MK_FM ? fm4_algo(E(t, 1)) : -1; }
int eng_model_own(int m) { return mdef(m).kind != MK_PLAITS; }
int eng_model_find(int kind, int sub) {
  for (int m = 0; m < ENG_MODELS; ++m)
    if (kModelDef[m].kind == kind && kModelDef[m].sub == sub) return m;
  return -1;
}

float eng_lx_step(int param, float v, int detents) {
  int lk = eng_lx_knob(param), bip = eng_pdef(param)->kind == K_BIP;
  if (lk < 0) return v;
  int t = param / 12;
  int n = zero_on(t) ? zero_knob_steps(lk) : track_kind(t) == MK_DRUM ? drum_knob_steps(lk)
                                            : track_kind(t) == MK_FM ? fm4_knob_steps(lk)
                                                                      : lx_knob_steps(lx_model(t), lk);
  float u = bip ? (v + 1.0f) * 0.5f : v;
  int i;
  if (n == 128) {                       // its 128 values: n / 127
    i = static_cast<int>(u * 127.0f + 0.5f) + detents;
    i = i < 0 ? 0 : i > 127 ? 127 : i;
    u = static_cast<float>(i) / 127.0f;
  } else {                              // a choice: the middle of its share
    i = static_cast<int>(u * static_cast<float>(n));
    i = (i < 0 ? 0 : i > n - 1 ? n - 1 : i) + detents;
    i = i < 0 ? 0 : i > n - 1 ? n - 1 : i;
    u = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
  }
  return bip ? 2.0f * u - 1.0f : u;
}

void eng_set_model(int t, int m) {
  if (t < 0 || t >= ENG_TRACKS || m < 0 || m >= ENG_MODELS) return;
  int was = proj.algo[PG_TRACK(t, 0)];
  proj.algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(m);
  if (eng_model_own(m) && m != was) lx_kit(&proj, t);
}

const eng_pdef_t* eng_pdef(int param) {   // (a table, made once: the render asks it hundreds of times a block)
  static const eng_pdef_t* table[ENG_NPARAMS];
  if (param < 0 || param >= ENG_NPARAMS) param = 0;
  if (param == PG_DELAY(0) * 4 && proj.algo[PG_DELAY(0)] == DL_FREE) return &kDelayFreeTime;   // (FREE: in ms)
  if (!table[param]) table[param] = &kPages[page_type(param / 4)][param % 4];
  return table[param];
}

int eng_param_mod(int param) {
  int k = eng_pdef(param)->kind;
  return (k == K_UNI || k == K_BIP) && param / 4 != PG_GLOBAL;
}

static void put(char* b, int n, const char* s) {
  int i = 0;
  while (s[i] && i < n - 1) {
    b[i] = s[i];
    ++i;
  }
  b[i] = 0;
}

static void put_num(char* b, int n, int32_t v, int decimals, int sign) {   // v in units of 10^-decimals
  char t[16];
  int i = 0, neg = v < 0;
  uint32_t u = static_cast<uint32_t>(neg ? -v : v);
  for (int d = 0; d < decimals; ++d) {
    t[i++] = static_cast<char>('0' + u % 10u);
    u /= 10u;
  }
  if (decimals) t[i++] = '.';
  do {
    t[i++] = static_cast<char>('0' + u % 10u);
    u /= 10u;
  } while (u && i < 13);
  if (neg) t[i++] = '-';
  else if (sign && v) t[i++] = '+';
  int k = 0;
  while (i && k < n - 1) b[k++] = t[--i];
  b[k] = 0;
}

static void put_end(char* b, int n, const char* s) {   // s after what b holds
  int l = 0;
  while (b[l]) ++l;
  put(b + l, n - l, s);
}

void eng_param_text(int param, float v, char* b, int n) {
  const eng_pdef_t* d = eng_pdef(param);
  int pt = page_type(param / 4), k = param % 4, lk = eng_lx_knob(param);
  if (lk >= 0 && zero_on(param / 12)) {   // ZERO's: as it writes them (an index, a ratio, Hz, %)
    zero_knob_text(lk, lx_u(param, v), b, n);
  } else if (lk >= 0 && track_kind(param / 12) == MK_DRUM) {   // a drum's (times, Hz, semitones)
    drum_knob_text(track_sub(param / 12), lk, lx_u(param, v), b, n);
  } else if (lk >= 0 && track_kind(param / 12) == MK_FM) {     // the FM's (an algorithm, a voicing, cents, times, %)
    fm4_knob_text(lk, lx_u(param, v), b, n);
  } else if (lk >= 0) {                   // an FM drum's: as the LXR-02 shows it (0..127, a wave)
    lx_knob_text(lx_model(param / 12), lk, lx_u(param, v), b, n);
  } else if (pt == PT_WARPS1 && k == 0 && i_of(v) >= WC_OSC1) {   // the oscillator's shape, as the algorithm uses it
    int o = i_of(v) > WC_OSC3 ? 2 : i_of(v) - WC_OSC1;
    put(b, n, Pi(PG_WARPS(1), 3) == WM_SHIFT ? kWarpsCarrier[WC_OSC1 + o] : kOscXmod[o]);
  } else if (pt == PT_FILT3 && k == 0) {  // TYPE: LP BP HP, VOX's voices
    int i = iround(v);
    put(b, n, mf_type_name(proj.algo[(param / 4) - 2], i < 0 ? 0 : i > 2 ? 2 : i));
  } else if (d->kind == K_ENUM) {
    int i = static_cast<int>(v + 0.5f);
    put(b, n, d->names[i < d->lo ? d->lo : i > d->hi ? d->hi : i]);
  } else if (pt == PT_WARPS1 && k == 2 && Pi(PG_WARPS(1), 3) == WM_SHIFT) {   // the frequency shifter: its shift
    float hz = warps_shift_hz(v), a = hz < 0 ? -hz : hz;
    if (a >= 1000.0f) {
      put_num(b, n, static_cast<int32_t>(hz * 0.01f + (hz < 0 ? -0.5f : 0.5f)), 1, 1);
      put_end(b, n, "k");
    } else {
      put_num(b, n, static_cast<int32_t>(hz + (hz < 0 ? -0.5f : 0.5f)), 0, 1);
      put_end(b, n, " Hz");
    }
  } else if (pt == PT_WARPS1 && k == 2) {
    put(b, n, kWarpsShort[warps_pos(v)]);
  } else if (pt == PT_WARPS2 && k == 1 && Pi(PG_WARPS(1), 3) != WM_SHIFT && Pi(PG_WARPS(0), 0) >= WC_OSC1) {
    int nt = warps_note(v), l = 0;      // LEVEL 1: the oscillator's note (C0..C5, in the scale)
    put(b, n, kNoteName[nt % 12]);
    while (b[l]) ++l;
    put_num(b + l, n - l, nt / 12 - 1, 0, 0);
  } else if (pt == PT_STEP && k == 0) {  // the note, from C3
    int nt = 48 + static_cast<int>(v + (v < 0 ? -0.5f : 0.5f)), l = 0;
    put(b, n, kNoteName[nt % 12]);
    while (b[l]) ++l;
    put_num(b + l, n - l, nt / 12 - 1, 0, 0);
  } else if (pt == PT_STEP && k == 2) {
    put(b, n, "x");
    put_num(b + 1, n - 1, static_cast<int32_t>(v + 0.5f), 0, 0);
  } else if (d->kind == K_INT) {
    put_num(b, n, static_cast<int32_t>(v + (v < 0 ? -0.5f : 0.5f)), 0, d->lo < 0);
  } else if (pt == PT_PAN) {
    int32_t q = static_cast<int32_t>(v * 100.0f + (v < 0 ? -0.5f : 0.5f));
    if (!q) {
      put(b, n, "C");
    } else {
      put(b, n, q < 0 ? "L" : "R");
      put_num(b + 1, n - 1, q < 0 ? -q : q, 0, 0);
    }
  } else if (pt == PT_DELAY1 && k == 2) {   // the filter: low pass to the left, high pass to the right
    int32_t q = static_cast<int32_t>(v * 100.0f + (v < 0 ? -0.5f : 0.5f));
    if (q == 0) {
      put(b, n, "Flat");
    } else {
      put(b, n, q < 0 ? "LP " : "HP ");
      put_num(b + 3, n - 3, q < 0 ? -q : q, 0, 0);
    }
  } else if (pt == PT_EQ) {              // the filter: its cutoff, LP or HP (in a knob: the cutoff in the ring, the
    if (v > -kEqDead && v < kEqDead) {   // kind under it)
      put(b, n, "Flat");
    } else {
      float hz = eq_hz(v);
      if (hz >= 1000.0f) {
        put_num(b, n, static_cast<int32_t>(hz * (hz >= 10000.0f ? 0.001f : 0.01f) + 0.5f), hz >= 10000.0f ? 0 : 1, 0);
        put_end(b, n, "k");
      } else {
        put_num(b, n, static_cast<int32_t>(hz + 0.5f), 0, 0);
      }
      put_end(b, n, v < 0.0f ? " LP" : " HP");
    }
  } else if (pt == PT_FILT1 && k == 0 && proj.algo[param / 4] == MF_VOX) {   // VOX's CUTOFF: the vowel (and the
    float pos = clampf(v, 0.0f, 1.0f) * 4.0f;                                // next, past its middle)
    static const char* const kV[5] = {"A", "E", "I", "O", "U"};
    int i = static_cast<int>(pos), j = i < 4 ? i + 1 : 4;
    float fr = pos - static_cast<float>(i);
    put(b, n, fr < 0.25f ? kV[i] : fr > 0.75f ? kV[j] : "");
    if (fr >= 0.25f && fr <= 0.75f) {
      put(b, n, kV[i]);
      put_end(b, n, ">");
      put_end(b, n, kV[j]);
    }
  } else if (pt == PT_FILT1 && k == 0) {  // CUTOFF: 20 Hz .. 18 kHz
    float hz = 20.0f * fm_exp2f(9.8f * clampf(v, 0.0f, 1.0f));
    if (hz >= 1000.0f) {
      put_num(b, n, static_cast<int32_t>(hz * (hz >= 10000.0f ? 0.001f : 0.01f) + 0.5f), hz >= 10000.0f ? 0 : 1, 0);
      put_end(b, n, "k");
    } else {
      put_num(b, n, static_cast<int32_t>(hz + 0.5f), 0, 0);
      put_end(b, n, " Hz");
    }
  } else if (pt == PT_FILT1 && k == 3) {  // DECAY: the vactrol's first fall (to a third), then its tail
    float t = 1.0f / (200.0f * fm_exp2f(-8.0f * clampf(v, 0.0f, 1.0f)));
    if (t < 1.0f) {
      put_num(b, n, static_cast<int32_t>(t * 1000.0f + 0.5f), 0, 0);
      put_end(b, n, " ms");
    } else {
      put_num(b, n, static_cast<int32_t>(t * 10.0f + 0.5f), 1, 0);
      put_end(b, n, " s");
    }
  } else if (pt == PT_FILT3 && k == 2) {  // COLOR: the noise darker, white, brighter
    int32_t q = static_cast<int32_t>(v * 100.0f + (v < 0 ? -0.5f : 0.5f));
    if (q > -3 && q < 3) {
      put(b, n, "White");
    } else {
      put(b, n, q < 0 ? "Dk " : "Br ");
      put_num(b + 3, n - 3, q < 0 ? -q : q, 0, 0);
    }
  } else if (pt == PT_FILT3 && k == 3) {  // ATTACK: the envelope's rise
    float t = 25.0f * fm_exp2f(12.0f * clampf(v, 0.0f, 1.0f)) / kSr;
    if (t < 0.0095f) {
      put_num(b, n, static_cast<int32_t>(t * 10000.0f + 0.5f), 1, 0);
      put_end(b, n, " ms");
    } else if (t < 1.0f) {
      put_num(b, n, static_cast<int32_t>(t * 1000.0f + 0.5f), 0, 0);
      put_end(b, n, " ms");
    } else {
      put_num(b, n, static_cast<int32_t>(t * 10.0f + 0.5f), 1, 0);
      put_end(b, n, " s");
    }
  } else if ((pt == PT_FILT1 && k == 1) || (pt == PT_FILT2 && (k == 0 || k == 1 || k == 3)) || (pt == PT_FILT3 && k == 1)) {
    put_num(b, n, static_cast<int32_t>(v * 100.0f + 0.5f), 0, 0);
    put_end(b, n, pt == PT_FILT2 && k == 3 ? "%" : "");
  } else if (pt == PT_BEND && k < 2) {   // CLOCK: its multiple of the note; BITS: how many
    if (v < 0.005f) {
      put(b, n, "Off");
    } else if (k == 0) {
      put(b, n, "x");
      put_num(b + 1, n - 1, static_cast<int32_t>(kClockRatio[clock_index(v)]), 0, 0);
    } else {
      put_num(b, n, static_cast<int32_t>(crush_bits(v) + 0.5f), 0, 0);
      put_end(b, n, " bit");
    }
  } else if (pt == PT_GLITCH && k == 2) {   // BEND: each repeat lower (Fall) or higher (Rise), in semitones
    int st = static_cast<int>(v * 6.0f + (v < 0 ? -0.5f : 0.5f));
    if (!st) {
      put(b, n, "Loop");
    } else {
      put(b, n, st < 0 ? "Fall " : "Rise ");
      put_num(b + 5, n - 5, st < 0 ? -st : st, 0, 0);
    }
  } else if (pt == PT_LANE && k == 0) {  // an effect's DECAY: its time
    float s = lane_tau(v);
    if (s < 1.0f) {
      put_num(b, n, static_cast<int32_t>(s * 1000.0f + 0.5f), 0, 0);
      put_end(b, n, " ms");
    } else {
      put_num(b, n, static_cast<int32_t>(s * 10.0f + 0.5f), 1, 0);
      put_end(b, n, " s");
    }
  } else if ((pt == PT_DUCK && k == 0) || (pt == PT_COMP && k == 2)) {   // the duck's DECAY, the release: times
    float t = pt == PT_DUCK ? duck_tau(v) : comp_release(v);
    if (t < 1.0f) {
      put_num(b, n, static_cast<int32_t>(t * 1000.0f + 0.5f), 0, 0);
      put_end(b, n, " ms");
    } else {
      put_num(b, n, static_cast<int32_t>(t * 10.0f + 0.5f), 1, 0);
      put_end(b, n, " s");
    }
  } else if (pt == PT_COMP && k == 0) {   // the threshold
    put_num(b, n, static_cast<int32_t>(comp_threshold(v) - 0.5f), 0, 0);
    put_end(b, n, " dB");
  } else if (pt == PT_COMP && k == 1) {   // the ratio
    float r = comp_ratio(v);
    if (r < 10.0f) put_num(b, n, static_cast<int32_t>(r * 10.0f + 0.5f), 1, 0);
    else put_num(b, n, static_cast<int32_t>(r + 0.5f), 0, 0);
    put_end(b, n, ":1");
  } else if ((pt == PT_COMP && k == 3) || (pt == PT_OTT && k >= 2)) {   // gains: +/-12 dB
    put_num(b, n, static_cast<int32_t>(v * 12.0f + (v < 0 ? -0.5f : 0.5f)), 0, 1);
    put_end(b, n, " dB");
  } else if (pt == PT_OTT && k == 1) {    // OTT's time: x1 its own
    put(b, n, "x");
    put_num(b + 1, n - 1, static_cast<int32_t>(ott_time(v) * 100.0f + 0.5f), 2, 0);
  } else if (pt == PT_PLAITS3 && k == 1) {   // LENGTH: the gate, in % of the sixteenth
    put_num(b, n, static_cast<int32_t>(gate_frac(v) * 100.0f + 0.5f), 0, 0);
    put_end(b, n, "%");
  } else if (pt == PT_GLOBAL && k == 3) {    // SWING: the on-beat's share of two sixteenths
    put_num(b, n, static_cast<int32_t>(50.0f + 25.0f * v + 0.5f), 0, 0);
    put_end(b, n, "%");
  } else if ((pt == PT_STEP && k == 1) || (pt == PT_ARP && k == 1) || (pt == PT_LANE && (k == 1 || k == 2)) ||
             (pt == PT_DUCK && k > 0) || (pt == PT_OTT && k == 0) || (pt == PT_GLITCH && (k == 0 || k == 3)) ||
             (pt == PT_WARPS1 && k == 3)) {
    put_num(b, n, static_cast<int32_t>(v * 100.0f + 0.5f), 0, 0);
    put_end(b, n, "%");
  } else if (pt == PT_GLOBAL && k == 0) {
    put_num(b, n, static_cast<int32_t>(bpm_of(v) + 0.5f), 0, 0);
    put_end(b, n, " BPM");
  } else if (pt == PT_PLAITS1 && k == 0) {
    put_num(b, n, static_cast<int32_t>(v * 240.0f + (v < 0 ? -0.5f : 0.5f)), 1, 1);   // semitones, 1 decimal
  } else if (pt == PT_LFO && k == 1 && Pi(param / 4, 2) == LM_SYNC) {   // RATE, synced: a ratio of the beat
    int r = static_cast<int>(v * 8.999f);
    put(b, n, kLfoRatioNames[r < 0 ? 0 : r > 8 ? 8 : r]);
  } else if (pt == PT_LFO && k == 1) {
    float hz = lfo_hz(v);
    put_num(b, n, static_cast<int32_t>(hz * (hz < 10.0f ? 100.0f : 10.0f) + 0.5f), hz < 10.0f ? 2 : 1, 0);
    put_end(b, n, " Hz");
  } else if (pt == PT_LFO && k == 3) {   // SLEW: its time
    if (v < 0.005f) {
      put(b, n, "Off");
    } else {
      float t = lfo_tau(v);
      if (t < 1.0f) {
        put_num(b, n, static_cast<int32_t>(t * 1000.0f + 0.5f), 0, 0);
        put_end(b, n, " ms");
      } else {
        put_num(b, n, static_cast<int32_t>(t * 100.0f + 0.5f), 2, 0);
        put_end(b, n, " s");
      }
    }
  } else if (pt == PT_DELAY1 && k == 1 && v >= 0.99f) {   // FEEDBACK at the end: the loop (held for good)
    put(b, n, "Loop");
  } else if (pt == PT_DELAY1 && k == 0) {   // the delay's TIME, FREE: ms
    float ms = delay_ms(v);
    if (ms < 1000.0f) {
      put_num(b, n, static_cast<int32_t>(ms + 0.5f), 0, 0);
      put_end(b, n, " ms");
    } else {
      put_num(b, n, static_cast<int32_t>(ms * 0.1f + 0.5f), 2, 0);
      put_end(b, n, " s");
    }
  } else if (d->kind == K_BIP) {
    put_num(b, n, static_cast<int32_t>(v * 100.0f + (v < 0 ? -0.5f : 0.5f)), 0, 1);
  } else {
    put_num(b, n, static_cast<int32_t>(v * 100.0f + 0.5f), 0, 0);
  }
}

const char* eng_src_name(int s) { return s >= 0 && s < NSRC ? kSrcNames[s] : "?"; }

int eng_algo_count(int page) {
  switch (page_type(page)) {
    case PT_PLAITS1: return ENG_MODELS;
    case PT_FILT1: return MF_MODELS;
    case PT_DELAY1: return 2;
    case PT_COMP: return 3;
    default: return 0;
  }
}

const char* eng_algo_name(int page, int a) {
  int n = eng_algo_count(page);
  if (!n) return "";
  a = a < 0 ? 0 : a >= n ? n - 1 : a;
  if (page_type(page) == PT_FILT1) return mf_model_name(a);
  return page_type(page) == PT_PLAITS1 ? kModelDef[a].name : page_type(page) == PT_COMP ? kBusModes[a] : kDelayAlgos[a];
}

project_t* eng_project(void) { return &proj; }

static void cable(project_t* p, int g, int c, int s, int dst, int depth) {
  p->cable[g][c].src = static_cast<uint8_t>(s);
  p->cable[g][c].dst = static_cast<uint8_t>(dst);
  p->cable[g][c].depth = static_cast<int8_t>(depth);
  p->cable[g][c].rsv = 0;
}

static void set_trigs(project_t* p, int lane, uint32_t bits) {   // bit s: step s (of the first 16)
  for (int st = 0; st < 16; ++st)
    if ((bits >> st) & 1u) p->trig[lane][st / 8] |= static_cast<uint8_t>(1u << (st % 8));
}

static void set_lock(project_t* p, int lane, int step, int tp, float v) {
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (p->lock[i].lane == 0xFF) {
      p->lock[i].lane = static_cast<uint8_t>(lane);
      p->lock[i].step = static_cast<uint8_t>(step);
      p->lock[i].tp = static_cast<uint8_t>(tp);
      p->lock[i].v = static_cast<uint8_t>(clampf(to_u(eng_track_param(lane, tp), v), 0.0f, 1.0f) * 255.0f + 0.5f);
      return;
    }
}

// INIT: four Plaits on VIRTUAL ANALOG (the first model of the classic bank), every knob at noon; no steps, nothing
// patched, nothing sent to the effects (the delay set to an eighth, ready), Warps off, C major, 120 BPM
// the LFOs as a new project has them: a sine (~0.7 Hz), a triangle a bar, a smooth random (~1 Hz, a little slewed)
static void lfo_blank(project_t* p) {
  static const float kLfo[3][4] = {{static_cast<float>(LW_SINE), 0.45f, static_cast<float>(LM_FREE), 0.0f},
                                   {static_cast<float>(LW_TRI), 4.5f / 9.0f, static_cast<float>(LM_SYNC), 0.0f},
                                   {static_cast<float>(LW_SMOOTH), 0.55f, static_cast<float>(LM_FREE), 0.3f}};
  for (int i = 0; i < 3; ++i)
    for (int k = 0; k < 4; ++k) p->val[PG_LFO(i) * 4 + k] = kLfo[i][k];
  for (int k = 0; k < 4; ++k) p->val[15 * 4 + k] = 0.0f;   // (page 15: not used)
  for (int pg = 12; pg < 16; ++pg) p->algo[pg] = 0;
}

void eng_blank(project_t* p) {
  ++lock_gen;                           // (the live project's locks, maybe: the lock map made again)
  uint8_t* raw = reinterpret_cast<uint8_t*>(p);
  for (uint32_t i = 0; i < sizeof(project_t); ++i) raw[i] = 0;
  p->magic = PROJECT_MAGIC;
  p->version = PROJECT_VERSION;
  p->size = sizeof(project_t);
  static const float kTrack[3][4] = {{0.0f, 0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f, 0.0f}};
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int pg = 0; pg < 3; ++pg)
      for (int k = 0; k < 4; ++k) p->val[PG_TRACK(t, pg) * 4 + k] = kTrack[pg][k];
    p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(eng_model_find(MK_FM, 0));   // the FM (its own knobs)
    lx_kit(p, t);
    p->val[PG_MIX * 4 + t] = 0.75f;
    p->val[PG_STEP(t) * 4 + 1] = 1.0f;                   // NOTE 0, PROB 100 %, no ratchet, 16 steps
    p->val[PG_STEP(t) * 4 + 2] = 1.0f;
    p->val[PG_ARP(t) * 4 + 0] = 2.0f;                    // Endless: sixteenths, half gates, forward
    p->val[PG_ARP(t) * 4 + 1] = 0.5f;
    p->val[PG_GLITCH(t) * 4 + 1] = static_cast<float>(GL_16);   // the glitch: off, its slice a sixteenth
  }
  for (int l = 0; l < ENG_LANES; ++l) {
    p->val[PG_STEP(l) * 4 + 3] = 16.0f;
    p->seed[l] = static_cast<uint16_t>(0x2A5 + 977 * l);
  }
  for (int l = ENG_TRACKS; l < ENG_LANES; ++l) {         // an effect's steps: DECAY 0.4 (~120 ms), PROB, DEPTH full
    p->val[PG_STEP(l) * 4 + 0] = 0.4f;
    p->val[PG_STEP(l) * 4 + 1] = 1.0f;
    p->val[PG_STEP(l) * 4 + 2] = 1.0f;
  }
  lfo_blank(p);
  // the reverb: a medium room, a little dark, its return at 60; the delay: an eighth, a few repeats, flat
  static const float kRev[4] = {0.6f, 0.4f, 0.6f, 0.6f};
  static const float kDelay[8] = {5.0f, 0.4f, 0.0f, 0.1f, 0.2f, 0.0f, 0.1f, 0.6f};
  for (int k = 0; k < 4; ++k) p->val[PG_REVERB * 4 + k] = kRev[k];
  for (int k = 0; k < 8; ++k) p->val[PG_DELAY(k / 4) * 4 + k % 4] = kDelay[k];
  // Warps: off (no carrier), ready on the diode ring modulator, track 2 as the modulator, all of it (MIX)
  static const float kW[8] = {static_cast<float>(WC_OFF), 1.0f, 0.4f, 1.0f, 0.5f, 0.6f, 0.6f,
                              static_cast<float>(WM_WARPS)};
  for (int k = 0; k < 8; ++k) p->val[PG_WARPS(k / 4) * 4 + k % 4] = kW[k];
  // the duck: off (its DECAY ~180 ms); the master bus: a gentle compressor (-10 dB, 3:1, 160 ms, +4 dB); OTT ready at
  // half its depth
  p->val[PG_DUCK * 4 + 0] = 0.45f;
  static const float kComp[4] = {0.28f, 0.32f, 0.5f, 4.0f / 12.0f};
  static const float kOtt[4] = {0.5f, 0.5f, 0.0f, 0.0f};
  for (int k = 0; k < 4; ++k) {
    p->val[PG_COMP * 4 + k] = kComp[k];
    p->val[PG_OTT * 4 + k] = kOtt[k];
  }
  p->algo[PG_COMP] = MB_COMP;
  for (int t = 0; t < ENG_TRACKS; ++t) {                 // the filters: off, LP; ready at 1.2 kHz, some resonance
    static const float kF[12] = {0.6f, 0.3f, 0.3f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};   // and
    for (int k = 0; k < 12; ++k) p->val[PG_FILT(t, k / 4) * 4 + k % 4] = kF[k];               // envelope
  }
  p->val[PG_GLOBAL * 4 + 0] = 0.60206f;                  // 30 x 2^(0.60206 x 3.3219) = 120 BPM
  p->val[PG_GLOBAL * 4 + 1] = 1.0f;                      // major
  for (int i = 0; i < ENG_PLOCKS; ++i) p->lock[i].lane = 0xFF;
}

// the demo: a kick, an FM bass whose INDEX LFO 1 opens at each of its notes, an FM pluck in a looped random tune
// thrown into the delay on two steps (its lane), a resonator (modal) in the reverb; C minor, 120 BPM
void eng_defaults(project_t* p) {
  eng_blank(p);
  const int kModel[4] = {eng_model_find(MK_DRUM, 0), eng_model_find(MK_FM, 0), eng_model_find(MK_FM, 0),
                         eng_model_find(MK_PLAITS, 20)};  // kick, the FM (a bass, a pluck), modal
  static const int kOct[4] = {-1, -1, 0, 0};
  static const float kLevel[4] = {0.9f, 0.75f, 0.7f, 0.7f};
  static const float kPan[4] = {0.0f, 0.0f, -0.3f, 0.35f};
  static const float kRSend[4] = {0.0f, 0.1f, 0.25f, 0.5f};
  static const float kDSend[4] = {0.0f, 0.0f, 0.6f, 0.0f};
  static const float kDecay[4] = {0.4f, 0.35f, 0.45f, 0.7f};
  for (int t = 0; t < ENG_TRACKS; ++t) {
    p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(kModel[t]);
    if (eng_model_own(kModel[t])) {     // (a drum: its kit, its own pitch; the FM its kit, its octave)
      lx_kit(p, t);
      if (eng_model_kind(kModel[t]) == MK_FM) p->val[PG_TRACK(t, 2) * 4 + 3] = static_cast<float>(kOct[t]);
    } else {
      p->val[PG_TRACK(t, 2) * 4 + 3] = static_cast<float>(kOct[t]);
      p->val[PG_TRACK(t, 1) * 4 + 0] = kDecay[t];
    }
    p->val[PG_MIX * 4 + t] = kLevel[t];
    p->val[PG_PAN * 4 + t] = kPan[t];
    p->val[PG_RSEND * 4 + t] = kRSend[t];
    p->val[PG_DSEND * 4 + t] = kDSend[t];
  }
  p->val[PG_TRACK(1, 0) * 4 + 1] = 0.5f / 8.0f;          // the bass: the first algorithm (four in a chain), INDEX
  p->val[PG_TRACK(1, 0) * 4 + 3] = 0.3f;                 // low, opened by LFO 1, a pluck over a soft saw
  p->val[PG_TRACK(1, 1) * 4 + 0] = 0.42f;
  p->val[PG_TRACK(1, 1) * 4 + 2] = 0.45f;
  p->val[PG_TRACK(1, 1) * 4 + 3] = 0.3f;
  p->val[PG_TRACK(1, 2) * 4 + 1] = 0.3f;                 // short notes
  p->val[PG_TRACK(2, 0) * 4 + 1] = 3.5f / 8.0f;          // the pluck: the fourth algorithm, its 1 at x3, a sharp pluck
  p->val[PG_TRACK(2, 0) * 4 + 2] = 6.5f / 30.0f;
  p->val[PG_TRACK(2, 1) * 4 + 0] = 0.5f;
  p->val[PG_TRACK(2, 1) * 4 + 2] = 0.6f;
  p->val[PG_TRACK(2, 2) * 4 + 1] = 0.2f;
  // LFO 1: a ramp down at each of T2's trigs (an envelope), 2.5 Hz; LFO 2: a sine a bar; LFO 3: a smooth random
  static const float kLfo[3][4] = {{static_cast<float>(LW_RAMP), 0.7f, static_cast<float>(LM_T1 + 1), 0.05f},
                                   {static_cast<float>(LW_SINE), 4.5f / 9.0f, static_cast<float>(LM_SYNC), 0.0f},
                                   {static_cast<float>(LW_SMOOTH), 3.5f / 9.0f, static_cast<float>(LM_SYNC), 0.2f}};
  for (int i = 0; i < 3; ++i)
    for (int k = 0; k < 4; ++k) p->val[PG_LFO(i) * 4 + k] = kLfo[i][k];
  p->val[PG_DUCK * 4 + 1] = 0.6f;                        // the kick ducks the bass, a little the others
  p->val[PG_DUCK * 4 + 2] = 0.3f;
  p->val[PG_DUCK * 4 + 3] = 0.3f;
  p->rnd[2][TP_NOTE] = -40;                              // the FM line: a looped random tune
  p->rnd[3][TP_NOTE] = -60;                              // the resonator too, its decay new at each trig
  p->rnd[3][TP_DECAY] = 25;
  // the delay: a dotted eighth, ping-pong, a little dark and driven, a slow wow
  static const float kDelay[8] = {7.0f, 0.6f, -0.2f, 0.2f, 0.3f, 1.0f, 0.15f, 0.6f};   // (0.4.7: FEEDBACK the loop's gain)
  for (int k = 0; k < 8; ++k) p->val[PG_DELAY(k / 4) * 4 + k % 4] = kDelay[k];
  p->val[PG_STEP(LANE_DELAY) * 4 + 0] = 0.45f;           // its lane: open on two of the line's notes, ~170 ms to
  set_trigs(p, LANE_DELAY, 0x1010);                      // close
  p->val[PG_GLOBAL * 4 + 1] = 2.0f;                      // minor
  cable(p, 1, 0, SRC_L1, PG_TRACK(1, 0) * 4 + 3, 50);    // track 2: L1 -> INDEX (the envelope)
  cable(p, 2, 0, SRC_L2, PG_TRACK(2, 0) * 4 + 3, 40);    // track 3: L2 -> INDEX (the loop)
  cable(p, 3, 0, SRC_L3, PG_TRACK(3, 0) * 4 + 2, 30);    // track 4: L3 -> TIMBRE (brightness)
  static const uint16_t kPat[4] = {0x1111, 0x6444, 0x1090, 0x4A49};   // bit s: step s
  for (int t = 0; t < ENG_TRACKS; ++t) set_trigs(p, t, kPat[t]);
  set_lock(p, 0, 12, TP_RATCHET, 2.0f);
  set_lock(p, 1, 10, TP_NOTE, 7.0f);
  set_lock(p, 1, 13, TP_NOTE, 10.0f);
  set_lock(p, 1, 14, TP_NOTE, 12.0f);
  set_lock(p, 2, 7, TP_NOTE, 3.0f);
  set_lock(p, 3, 9, TP_DECAY, 0.95f);
  ++lock_gen;
}

// the demo groove (GLO, SELECT: PROJECT, the second; a first power-on; the browser emulator's start): a kick on the
// beat, ducking an FM bass through a Moog filter (LADDER, its envelope, a triangle a bar on its cutoff) that rolls on
// the off-beats with a riff of locks, the FM hi-hat on every sixteenth (its decay and pan new at each, an open one on
// the off-beats), dub stabs of minor sevenths (the FM's PAD) through SEM (S&H on their brightness, a smooth random on
// the cutoff) thrown into a dotted-eighth ping-pong and the reverb; F minor, 124 BPM, a gentle compressor on the bus.
// Light on purpose: about half of each of the FM-1's cores (the machine page), room left for what is added
void eng_groove(project_t* p) {
  eng_blank(p);
  const int kModel[4] = {eng_model_find(MK_DRUM, 0), eng_model_find(MK_FM, 0), eng_model_find(MK_LXR, 3),
                         eng_model_find(MK_FM, 0)};   // kick, the FM (a bass), FM hi-hat, the FM (chords)
  static const int kOct[4] = {0, -1, 0, 0};
  static const float kLevel[4] = {1.0f, 0.4f, 0.5f, 1.0f};
  static const float kPan[4] = {0.0f, 0.0f, 0.25f, -0.15f};
  static const float kRSend[4] = {0.0f, 0.05f, 0.15f, 0.45f};
  static const float kDSend[4] = {0.0f, 0.0f, 0.1f, 0.5f};
  static const float kDecay[4] = {0.42f, 0.3f, 0.5f, 0.35f};
  for (int t = 0; t < ENG_TRACKS; ++t) {
    p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(kModel[t]);
    if (eng_model_own(kModel[t])) lx_kit(p, t);
    else p->val[PG_TRACK(t, 1) * 4 + 0] = kDecay[t];
    p->val[PG_TRACK(t, 2) * 4 + 3] = static_cast<float>(kOct[t]);
    p->val[PG_MIX * 4 + t] = kLevel[t];
    p->val[PG_PAN * 4 + t] = kPan[t];
    p->val[PG_RSEND * 4 + t] = kRSend[t];
    p->val[PG_DSEND * 4 + t] = kDSend[t];
  }
  // the bass: its pluck a little longer, a saw in the chain (FEEDBACK), short gates; through the ladder, opened by its
  // envelope at each note
  p->val[PG_TRACK(1, 0) * 4 + 1] = 0.5f / 8.0f;          // ALGO: four in a chain
  p->val[PG_TRACK(1, 0) * 4 + 3] = 0.5f;                 // INDEX
  p->val[PG_TRACK(1, 1) * 4 + 0] = 0.42f;                // DECAY
  p->val[PG_TRACK(1, 1) * 4 + 2] = 0.45f;                // MOVE: a pluck
  p->val[PG_TRACK(1, 1) * 4 + 3] = 0.6f;                 // FEEDBACK: toward the saw
  p->val[PG_TRACK(1, 2) * 4 + 1] = 0.35f;
  p->algo[PG_FILT(1, 0)] = MF_LADDER;
  static const float kBassF[12] = {0.5f, 0.55f, 0.45f, 0.28f, 0.3f, 0.0f, 0.0f, 0.5f, MF_LP, 0.0f, 0.0f, 0.0f};
  for (int k = 0; k < 12; ++k) p->val[PG_FILT(1, k / 4) * 4 + k % 4] = kBassF[k];
  // the stabs: a minor seventh chord (RATIO, three operators heard), short (a second's decay, a pluck of FM), through
  // SEM's low pass, its envelope and a breath of noise
  p->val[PG_TRACK(3, 0) * 4 + 1] = 5.5f / 8.0f;          // ALGO: one modulator, three heard
  p->val[PG_TRACK(3, 0) * 4 + 2] = 27.5f / 30.0f;        // RATIO: Min7
  p->val[PG_TRACK(3, 0) * 4 + 3] = 0.3f;                 // INDEX
  p->val[PG_TRACK(3, 1) * 4 + 0] = 0.5f;                 // DECAY: 1 s
  p->val[PG_TRACK(3, 1) * 4 + 1] = 0.35f;                // MOTION
  p->val[PG_TRACK(3, 1) * 4 + 2] = 0.3f;                 // MOVE: a pluck
  p->val[PG_TRACK(3, 1) * 4 + 3] = 0.4f;                 // FEEDBACK
  p->val[PG_TRACK(3, 2) * 4 + 1] = 0.35f;
  p->algo[PG_FILT(3, 0)] = MF_SEM;
  static const float kStabF[12] = {0.58f, 0.45f, 0.45f, 0.32f, 0.0f, 0.0f, 0.0f, 0.3f, MF_LP, 0.15f, -0.3f, 0.05f};
  for (int k = 0; k < 12; ++k) p->val[PG_FILT(3, k / 4) * 4 + k % 4] = kStabF[k];
  // LFO 1: a triangle a bar (the bass's cutoff); LFO 2: a sample and hold on the half bar (the stabs' voicing);
  // LFO 3: a slow smooth random (the stabs' cutoff, the delay's filter)
  static const float kLfo[3][4] = {{static_cast<float>(LW_TRI), 4.5f / 9.0f, static_cast<float>(LM_SYNC), 0.0f},
                                   {static_cast<float>(LW_SH), 5.5f / 9.0f, static_cast<float>(LM_SYNC), 0.1f},
                                   {static_cast<float>(LW_SMOOTH), 0.3f, static_cast<float>(LM_FREE), 0.3f}};
  for (int i = 0; i < 3; ++i)
    for (int k = 0; k < 4; ++k) p->val[PG_LFO(i) * 4 + k] = kLfo[i][k];
  p->val[PG_DUCK * 4 + 0] = 0.4f;                        // the kick ducks the bass and the stabs
  p->val[PG_DUCK * 4 + 1] = 0.55f;
  p->val[PG_DUCK * 4 + 3] = 0.35f;
  static const float kDelay[8] = {7.0f, 0.55f, -0.3f, 0.15f, 0.25f, 1.0f, 0.15f, 0.55f};   // a dotted eighth,
  for (int k = 0; k < 8; ++k) p->val[PG_DELAY(k / 4) * 4 + k % 4] = kDelay[k];               // ping-pong, darkened
  static const float kRev[4] = {0.7f, 0.5f, 0.7f, 0.45f};
  for (int k = 0; k < 4; ++k) p->val[PG_REVERB * 4 + k] = kRev[k];
  static const float kComp[4] = {0.3f, 0.3f, 0.5f, 3.0f / 12.0f};   // the bus: -9 dB, 3:1, +3 dB
  for (int k = 0; k < 4; ++k) p->val[PG_COMP * 4 + k] = kComp[k];
  p->algo[PG_COMP] = MB_COMP;
  p->val[PG_GLOBAL * 4 + 0] = 0.61629f;                  // 30 x 2^(0.61629 x 3.3219) = 124 BPM
  p->val[PG_GLOBAL * 4 + 1] = 2.0f;                      // minor
  p->val[PG_GLOBAL * 4 + 2] = 5.0f;                      // on F
  cable(p, 1, 0, SRC_L1, PG_FILT(1, 0) * 4 + 0, 22);     // T2: the triangle -> CUTOFF
  cable(p, 3, 0, SRC_L2, PG_TRACK(3, 0) * 4 + 3, 30);    // T4: S&H -> INDEX (the brightness), the smooth random ->
  cable(p, 3, 1, SRC_L3, PG_FILT(3, 0) * 4 + 0, 25);     // CUTOFF
  cable(p, 4, 0, SRC_L3, PG_DELAY(0) * 4 + 2, 20);       // global: the smooth random -> the delay's filter
  static const uint16_t kPat[4] = {0x1111, 0xEEEE, 0xFFFF, 0x0848};   // bit s: step s
  for (int t = 0; t < ENG_TRACKS; ++t) set_trigs(p, t, kPat[t]);
  set_lock(p, 1, 3, TP_NOTE, 12.0f);                     // the bass's riff (in the scale), its last note slid up
  set_lock(p, 1, 7, TP_NOTE, 3.0f);
  set_lock(p, 1, 10, TP_NOTE, 7.0f);
  set_lock(p, 1, 11, TP_NOTE, 5.0f);
  set_lock(p, 1, 15, TP_NOTE, 10.0f);
  set_lock(p, 2, 2, TP_DECAY, 0.75f);                    // the open hats, on the off-beats
  set_lock(p, 2, 6, TP_DECAY, 0.75f);
  set_lock(p, 2, 10, TP_DECAY, 0.75f);
  set_lock(p, 2, 14, TP_DECAY, 0.85f);
  set_lock(p, 0, 12, TP_RATCHET, 2.0f);                  // the kick's double, its last of the bar
  set_lock(p, 3, 11, TP_NOTE, -2.0f);                    // the stabs: their second one a step down
  p->rnd[1][TP_RATCHET] = 12;                            // the bass: a ratchet now and then
  p->rnd[2][TP_DECAY] = 20;                              // the hats: their decay and pan new at each
  p->rnd[2][TP_PAN] = 30;
  p->val[PG_STEP(2) * 4 + 1] = 0.9f;                     // (and 10 % of them left out)
  ++lock_gen;
}

// the stress test (GLO, SELECT: PROJECT, K1, REC held): a fixed patch that keeps everything busy, to read the
// machine page against, version after version: four tracks playing (a kick, the FM synced on a square, the FM in PW
// on a square, three operators heard, a chord: the FM at its heaviest, MODAL), two of them bent, Warps (diode ring, T3 by T2), OTT, the
// delay and the reverb fed, the duck, the filters, the three LFOs on 18 cables, randoms, locks, ratchets
void eng_stress(project_t* p) {
  eng_blank(p);
  const int kModel[4] = {eng_model_find(MK_DRUM, 0), eng_model_find(MK_FM, 0), eng_model_find(MK_FM, 0),
                         eng_model_find(MK_PLAITS, 20)};  // kick, the FM twice, modal
  static const int kOct[4] = {0, -1, 0, 0};
  static const float kLevel[4] = {0.9f, 0.75f, 0.6f, 0.6f};
  static const float kPan[4] = {0.0f, 0.0f, -0.3f, 0.35f};
  static const float kRSend[4] = {0.0f, 0.15f, 0.35f, 0.5f};
  static const float kDSend[4] = {0.0f, 0.1f, 0.45f, 0.2f};
  static const float kDecay[4] = {0.45f, 0.4f, 0.5f, 0.7f};
  static const float kEq[4] = {0.0f, 0.0f, -0.25f, 0.15f};
  for (int t = 0; t < ENG_TRACKS; ++t) {
    p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(kModel[t]);
    if (eng_model_own(kModel[t])) lx_kit(p, t);   // (the kick: its kit, its pitch; the FM: its kit)
    else p->val[PG_TRACK(t, 1) * 4 + 0] = kDecay[t];
    if (eng_model_kind(kModel[t]) != MK_DRUM) p->val[PG_TRACK(t, 2) * 4 + 3] = static_cast<float>(kOct[t]);
    p->val[PG_MIX * 4 + t] = kLevel[t];
    p->val[PG_PAN * 4 + t] = kPan[t];
    p->val[PG_RSEND * 4 + t] = kRSend[t];
    p->val[PG_DSEND * 4 + t] = kDSend[t];
    p->val[PG_EQ * 4 + t] = kEq[t];
  }
  p->val[PG_TRACK(1, 2) * 4 + 1] = 0.3f;                 // the bass: short notes
  p->val[PG_TRACK(1, 1) * 4 + 3] = -0.6f;                // the bass: a square (FEEDBACK), synced (MUTATE)
  p->val[PG_TRACK(1, 2) * 4 + 0] = 0.5f;
  p->val[PG_TRACK(2, 0) * 4 + 1] = 5.5f / 8.0f;          // the chords: one modulator, three heard, a minor chord
  p->val[PG_TRACK(2, 0) * 4 + 2] = 23.5f / 30.0f;        // (RATIO), a square, in PW
  p->val[PG_TRACK(2, 1) * 4 + 3] = -0.5f;
  p->val[PG_TRACK(2, 2) * 4 + 0] = -0.45f;
  // the filters, one model each (0.5.8): LADDER driven on the kick, MS-20 on the bass (its envelope, half its key),
  // SEM's noise on the chords (its FM: a sample at a time), VOX on the resonator, pinged
  static const int kFm[4] = {MF_LADDER, MF_MS20, MF_SEM, MF_VOX};
  static const float kFk[4][12] = {{0.78f, 0.3f, 0.3f, 0.3f, 0.4f, 0.0f, 0.0f, 0.0f, MF_LP, 0.0f, 0.0f, 0.0f},
                                   {0.45f, 0.6f, 0.5f, 0.35f, 0.6f, 0.0f, 0.0f, 0.5f, MF_LP, 0.0f, 0.0f, 0.0f},
                                   {0.62f, 0.5f, 0.3f, 0.5f, 0.0f, 0.0f, 0.3f, 0.0f, MF_LP, 0.55f, -0.4f, 0.0f},
                                   {0.5f, 0.6f, 0.4f, 0.6f, 0.0f, 0.5f, 0.0f, 0.0f, MF_MID, 0.35f, 0.0f, 0.1f}};
  for (int t = 0; t < ENG_TRACKS; ++t) {
    p->algo[PG_FILT(t, 0)] = static_cast<uint8_t>(kFm[t]);
    for (int k = 0; k < 12; ++k) p->val[PG_FILT(t, k / 4) * 4 + k % 4] = kFk[t][k];
  }
  p->val[PG_BEND(0) * 4 + 1] = 0.3f;                     // the kick crushed (0.7: the bits, the grit's place)
  p->val[PG_BEND(1) * 4 + 0] = 0.25f;                    // the bass: its clock on the note, a bit inverted, folded
  p->val[PG_BEND(1) * 4 + 2] = 2.0f;
  p->val[PG_BEND(1) * 4 + 3] = static_cast<float>(BS_FOLD);
  p->val[PG_GLITCH(3) * 4 + 0] = 0.3f;                   // T4 glitching
  // LFO 1: a triangle a bar; LFO 2: a sample and hold at ~3 Hz, slewed; LFO 3: a sine at 0.3 Hz
  static const float kLfo[3][4] = {{static_cast<float>(LW_TRI), 4.5f / 9.0f, static_cast<float>(LM_SYNC), 0.0f},
                                   {static_cast<float>(LW_SH), 0.725f, static_cast<float>(LM_FREE), 0.15f},
                                   {static_cast<float>(LW_SINE), 0.392f, static_cast<float>(LM_FREE), 0.0f}};
  for (int i = 0; i < 3; ++i)
    for (int k = 0; k < 4; ++k) p->val[PG_LFO(i) * 4 + k] = kLfo[i][k];
  p->val[PG_DUCK * 4 + 0] = 0.35f;
  p->val[PG_DUCK * 4 + 1] = 0.5f;
  p->val[PG_DUCK * 4 + 2] = 0.3f;
  p->val[PG_DUCK * 4 + 3] = 0.25f;
  static const float kDelay[8] = {7.0f, 0.55f, -0.15f, 0.3f, 0.35f, 1.0f, 0.25f, 0.55f};
  for (int k = 0; k < 8; ++k) p->val[PG_DELAY(k / 4) * 4 + k % 4] = kDelay[k];
  static const float kRev[4] = {0.75f, 0.45f, 0.7f, 0.55f};
  for (int k = 0; k < 4; ++k) p->val[PG_REVERB * 4 + k] = kRev[k];
  static const float kWarps[8] = {static_cast<float>(WC_T3), 1.0f, 0.4f, 0.8f, 0.5f, 0.6f, 0.6f,
                                  static_cast<float>(WM_WARPS)};
  for (int k = 0; k < 8; ++k) p->val[PG_WARPS(k / 4) * 4 + k % 4] = kWarps[k];
  p->algo[PG_COMP] = MB_OTT;                             // the master bus: OTT
  static const float kOtt[4] = {0.6f, 0.5f, 0.1f, 0.0f};
  for (int k = 0; k < 4; ++k) p->val[PG_OTT * 4 + k] = kOtt[k];
  p->val[PG_GLOBAL * 4 + 1] = 2.0f;                      // minor
  // the cables: each track's, then the global ones
  cable(p, 0, 0, SRC_L2, PG_TRACK(0, 0) * 4 + 2, 25);    // T1: S&H -> BRIGHT, sine -> its bits, the triangle -> DECAY
  cable(p, 0, 1, SRC_L3, PG_BEND(0) * 4 + 1, 30);
  cable(p, 0, 2, SRC_L1, PG_TRACK(0, 1) * 4 + 0, 20);
  cable(p, 1, 0, SRC_L1, PG_TRACK(1, 0) * 4 + 3, 40);    // T2: the triangle -> INDEX and the filter, S&H ->
  cable(p, 1, 1, SRC_L2, PG_TRACK(1, 1) * 4 + 1, 30);    // MOTION, the sine -> BITS
  cable(p, 1, 2, SRC_L3, PG_BEND(1) * 4 + 1, 15);
  cable(p, 1, 3, SRC_L1, PG_EQ * 4 + 1, -20);
  cable(p, 2, 0, SRC_L1, PG_TRACK(2, 0) * 4 + 3, 40);    // T3: the triangle -> INDEX, S&H -> MOTION, the sine -> the
  cable(p, 2, 1, SRC_L2, PG_TRACK(2, 1) * 4 + 1, 35);    // pan and the delay's send
  cable(p, 2, 2, SRC_L3, PG_PAN * 4 + 2, 60);
  cable(p, 2, 3, SRC_L3, PG_DSEND * 4 + 2, 30);
  cable(p, 3, 0, SRC_L2, PG_TRACK(3, 0) * 4 + 2, 40);    // T4: S&H -> TIMBRE and the pan, the sine -> HARMONICS, the
  cable(p, 3, 1, SRC_L3, PG_TRACK(3, 0) * 4 + 1, 30);    // triangle -> the reverb's send
  cable(p, 3, 2, SRC_L1, PG_RSEND * 4 + 3, 30);
  cable(p, 3, 3, SRC_L2, PG_PAN * 4 + 3, -50);
  cable(p, 4, 0, SRC_L1, PG_DELAY(0) * 4 + 2, 25);       // global: the delay's filter and wow, Warps' TIMBRE, the
  cable(p, 4, 1, SRC_L3, PG_DELAY(1) * 4 + 2, 30);       // reverb's time, OTT's depth
  cable(p, 4, 2, SRC_L2, PG_WARPS(1) * 4 + 0, 40);
  cable(p, 4, 3, SRC_L3, PG_REVERB * 4 + 0, 20);
  cable(p, 4, 4, SRC_L1, PG_OTT * 4 + 0, 20);
  static const uint16_t kPat[4] = {0x5111, 0x6DB6, 0x4925, 0x8282};   // bit s: step s
  for (int t = 0; t < ENG_TRACKS; ++t) set_trigs(p, t, kPat[t]);
  set_lock(p, 0, 14, TP_RATCHET, 3.0f);
  set_lock(p, 1, 3, TP_NOTE, 3.0f);
  set_lock(p, 1, 6, TP_NOTE, 7.0f);
  set_lock(p, 1, 11, TP_NOTE, 10.0f);
  p->rnd[1][TP_RATCHET] = 20;                            // the bass: ratchets now and then
  p->rnd[2][TP_NOTE] = -30;                              // the chords: a looped random tune, their pan new each trig
  p->rnd[2][TP_PAN] = 40;
  p->rnd[3][TP_NOTE] = -50;                              // the resonator: a looped tune, its decay new each trig
  p->rnd[3][TP_DECAY] = 30;
  ++lock_gen;
}

// the stress test on the four FM drums (GLO, SELECT: PROJECT, the fifth): the same patch, the cables and the effects
// as they are, the tracks on the drum, the snare, the cymbal and the hi-hat (their kits, at their own pitch): what
// the LXR-02's voices cost against Plaits', on the machine page
void eng_stress_fm(project_t* p) {
  eng_stress(p);
  for (int t = 0; t < ENG_TRACKS; ++t) {
    p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(eng_model_find(MK_LXR, t));
    lx_kit(p, t);
    p->val[PG_TRACK(t, 2) * 4 + 3] = 0.0f;               // (octave 0: each its kit's pitch)
  }
}

int eng_check(const project_t* p) {
  return p->magic == PROJECT_MAGIC && p->version == PROJECT_VERSION && p->size == sizeof(project_t);
}

}  // extern "C"

namespace {

// 0.1.4's to 0.2's projects (versions 4 and 5, 33 and 35 pages): with Marbles, three effects fewer
struct lock_v5 {
  uint8_t ts;                           // track << 6 | step
  uint8_t tp;                           // 0xFF: free
  uint8_t v, rsv;
};
template <int NP>
struct project_old {
  uint32_t magic;
  uint16_t version, size;
  float val[NP * 4];
  uint8_t algo[NP];
  uint8_t out_mode;
  uint8_t rsv[2];
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_TRACKS][ENG_STEPS / 8];
  int8_t rnd[ENG_TRACKS][18];
  uint16_t seed[ENG_TRACKS];
  lock_v5 lock[ENG_PLOCKS];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
};

// 0.3.0's to 0.3.2's (version 6): all of it, without the duck and the master bus (theirs: INIT's)
struct project_v6 {
  uint32_t magic;
  uint16_t version, size;
  float val[37 * 4];
  uint8_t algo[37];
  uint8_t out_mode;
  uint8_t rsv[2];
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_LANES][ENG_STEPS / 8];
  int8_t rnd[ENG_LANES][19];
  uint16_t seed[ENG_LANES];
  plock_t lock[ENG_PLOCKS];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
};

// 0.3.8's (version 8): all of it, without the grit and the Euclidean rhythms
struct project_v8 {
  uint32_t magic;
  uint16_t version, size;
  float val[41 * 4];
  uint8_t algo[41];
  uint8_t out_mode, warps_hq, theme;
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_LANES][ENG_STEPS / 8];
  int8_t rnd[ENG_LANES][21];
  uint16_t seed[ENG_LANES];
  plock_t lock[ENG_PLOCKS];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
};

// 0.3.3's to 0.3.7's (version 7): all of it, without the filter (its knobs flat)
struct project_v7 {
  uint32_t magic;
  uint16_t version, size;
  float val[40 * 4];
  uint8_t algo[40];
  uint8_t out_mode, warps_hq, theme;
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_LANES][ENG_STEPS / 8];
  int8_t rnd[ENG_LANES][20];
  uint16_t seed[ENG_LANES];
  plock_t lock[ENG_PLOCKS];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
};

// 0.3.9's and 0.3.10's (version 9): all of it, without the random dose (remap_v9 then moves Tides' and Warps')
struct project_v9 {
  uint32_t magic;
  uint16_t version, size;
  float val[52 * 4];
  uint8_t algo[52];
  uint8_t out_mode, warps_hq, theme;
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_LANES][ENG_STEPS / 8];
  int8_t rnd[ENG_LANES][25];
  uint16_t seed[ENG_LANES];
  plock_t lock[ENG_PLOCKS];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
};

// 0.5.6's (version 12; 10 and 11 the same without rseed): 52 pages, 25 lane parameters, no filters
struct project_v12 {
  uint32_t magic;
  uint16_t version, size;
  float val[52 * 4];
  uint8_t algo[52];
  uint8_t out_mode, warps_hq, theme, dice, dice_seed;
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_LANES][ENG_STEPS / 8];
  int8_t rnd[ENG_LANES][25];
  uint16_t seed[ENG_LANES];
  plock_t lock[ENG_PLOCKS];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
  uint8_t rseed[ENG_LANES][25];
};
const uint32_t kV11Bytes = 3104u;       // 0.4's and 0.5's (versions 10, 11): 0.5.6's without its rseed
static_assert(offsetof(project_v12, rseed) <= kV11Bytes && offsetof(project_v12, rseed) + 4u > kV11Bytes,
              "0.5's project: 0.5.6's but its draws (and its padding)");

void upgrade_v12(project_t* p, const project_v12& o) {   // its pages and lane parameters first: the filters new
  eng_blank(p);
  for (int i = 0; i < 52 * 4; ++i) p->val[i] = o.val[i];
  for (int i = 0; i < 52; ++i) p->algo[i] = o.algo[i];
  p->out_mode = o.out_mode;
  p->warps_hq = o.warps_hq;
  p->theme = o.theme;
  p->dice = o.dice;
  p->dice_seed = o.dice_seed;
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) p->cable[g][c] = o.cable[g][c];
  for (int l = 0; l < ENG_LANES; ++l) {
    for (int i = 0; i < ENG_STEPS / 8; ++i) p->trig[l][i] = o.trig[l][i];
    for (int i = 0; i < 25; ++i) {
      p->rnd[l][i] = o.rnd[l][i];
      p->rseed[l][i] = o.rseed[l][i];
    }
    p->seed[l] = o.seed[l];
  }
  for (int i = 0; i < ENG_PLOCKS; ++i) p->lock[i] = o.lock[i];
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int i = 0; i < ENG_ENDLESS; ++i) p->endless[t][i] = o.endless[t][i];
    p->endless_len[t] = o.endless_len[t];
    p->endless_key[t] = o.endless_key[t];
  }
}

// 0.5.8's and 0.5.9's (version 13): the filters in two pages each (52 + 2 t: CUTOFF, RESO, ENV, DECAY; the model's
// own knob, PING, FM, KEY), the type the second's ALGORITHM; 60 pages, 33 lane parameters
struct project_v13 {
  uint32_t magic;
  uint16_t version, size;
  float val[60 * 4];
  uint8_t algo[60];
  uint8_t out_mode, warps_hq, theme, dice, dice_seed;
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_LANES][ENG_STEPS / 8];
  int8_t rnd[ENG_LANES][33];
  uint16_t seed[ENG_LANES];
  plock_t lock[ENG_PLOCKS];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
  uint8_t rseed[ENG_LANES][33];
};

int v13_param(int q) {                  // a 0.5.8 parameter's place now: its filters' two pages moved, a third added
  if (q < 52 * 4) return q;
  int pg = q / 4 - 52;
  return PG_FILT(pg / 2, pg % 2) * 4 + q % 4;
}

void upgrade_v13(project_t* p, const project_v13& o) {
  eng_blank(p);
  for (int i = 0; i < 60 * 4; ++i) p->val[v13_param(i)] = o.val[i];
  for (int i = 0; i < 52; ++i) p->algo[i] = o.algo[i];
  for (int t = 0; t < ENG_TRACKS; ++t) {  // the model; the type, an ALGORITHM then; SEM's NOISE and VOX's BREATH, their
    int m = o.algo[52 + 2 * t];           // own knob then, now NOISE (DRIVE back at 0)
    p->algo[PG_FILT(t, 0)] = static_cast<uint8_t>(m);
    p->val[PG_FILT(t, 2) * 4 + 0] = static_cast<float>(o.algo[52 + 2 * t + 1] % 3);
    if (m == MF_SEM || m == MF_VOX) {
      float sp = o.val[(52 + 2 * t + 1) * 4];
      p->val[PG_FILT(t, 2) * 4 + 1] = m == MF_SEM ? 0.94f * sp : fm_sqrtf(0.83f * sp);   // (the same noise)
      p->val[PG_FILT(t, 1) * 4 + 0] = 0.0f;
    }
  }
  p->out_mode = o.out_mode;
  p->warps_hq = o.warps_hq;
  p->theme = o.theme;
  p->dice = o.dice;
  p->dice_seed = o.dice_seed;
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) {
      p->cable[g][c] = o.cable[g][c];
      p->cable[g][c].dst = static_cast<uint8_t>(v13_param(o.cable[g][c].dst));
    }
  for (int l = 0; l < ENG_LANES; ++l) {
    for (int i = 0; i < ENG_STEPS / 8; ++i) p->trig[l][i] = o.trig[l][i];
    for (int i = 0; i < 33; ++i) {        // (the lane parameters' order kept: the filter's new four after)
      p->rnd[l][i] = o.rnd[l][i];
      p->rseed[l][i] = o.rseed[l][i];
    }
    p->seed[l] = o.seed[l];
  }
  for (int i = 0; i < ENG_PLOCKS; ++i) p->lock[i] = o.lock[i];
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int i = 0; i < ENG_ENDLESS; ++i) p->endless[t][i] = o.endless[t][i];
    p->endless_len[t] = o.endless_len[t];
    p->endless_key[t] = o.endless_key[t];
  }
}

// 0.6's (version 14): 64 pages, 37 lane parameters, the grit where the bits are now (its DRIVE, SHAPE, RATE, BITS),
// no glitch
struct project_v14 {
  uint32_t magic;
  uint16_t version, size;
  float val[64 * 4];
  uint8_t algo[64];
  uint8_t out_mode, warps_hq, theme, dice, dice_seed;
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_LANES][ENG_STEPS / 8];
  int8_t rnd[ENG_LANES][37];
  uint16_t seed[ENG_LANES];
  plock_t lock[ENG_PLOCKS];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
  uint8_t rseed[ENG_LANES][37];
};

static_assert(sizeof(project_v14) == 3652u, "0.6's project: its bytes as 0.6.6 saved them");

void upgrade_v14(project_t* p, const project_v14& o) {
  eng_blank(p);
  for (int i = 0; i < 64 * 4; ++i) p->val[i] = o.val[i];
  for (int i = 0; i < 64; ++i) p->algo[i] = o.algo[i];
  p->out_mode = o.out_mode;
  p->warps_hq = o.warps_hq;
  p->theme = o.theme;
  p->dice = o.dice;
  p->dice_seed = o.dice_seed;
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) p->cable[g][c] = o.cable[g][c];
  for (int l = 0; l < ENG_LANES; ++l) {
    for (int i = 0; i < ENG_STEPS / 8; ++i) p->trig[l][i] = o.trig[l][i];
    for (int i = 0; i < 37; ++i) {        // (the lane parameters' order kept: the glitch's four after)
      p->rnd[l][i] = o.rnd[l][i];
      p->rseed[l][i] = o.rseed[l][i];
    }
    p->seed[l] = o.seed[l];
  }
  for (int i = 0; i < ENG_PLOCKS; ++i) p->lock[i] = o.lock[i];
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int i = 0; i < ENG_ENDLESS; ++i) p->endless[t][i] = o.endless[t][i];
    p->endless_len[t] = o.endless_len[t];
    p->endless_key[t] = o.endless_key[t];
  }
}

// the grit gone (0.7): what an older project had of it (its knobs, their randoms, locks and cables) is not the bits'
void grit_gone(project_t* p) {
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int k = 0; k < 4; ++k) p->val[PG_BEND(t) * 4 + k] = 0.0f;
    for (int tp = TP_CLOCK; tp <= TP_BSHAPE; ++tp) p->rnd[t][tp] = 0, p->rseed[t][tp] = 0;
  }
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (p->lock[i].lane < ENG_TRACKS && p->lock[i].tp >= TP_CLOCK && p->lock[i].tp <= TP_BSHAPE) p->lock[i].lane = 0xFF;
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) {
      int d = p->cable[g][c].dst;
      if (d >= PG_BEND(0) * 4 && d < PG_BEND(ENG_TRACKS) * 4) p->cable[g][c].depth = 0, p->cable[g][c].src = SRC_NONE;
    }
}

// versions 7 to 9: their pages first, their lane parameters first (what came after is new)
void blank_v9(project_t* p);

template <class P, int NP, int NTP>
void upgrade_78(project_t* p, const P& o) {
  blank_v9(p);
  for (int i = 0; i < NP * 4; ++i) p->val[i] = o.val[i];
  for (int i = 0; i < NP; ++i) p->algo[i] = o.algo[i];
  p->out_mode = o.out_mode;
  p->warps_hq = o.warps_hq;
  p->theme = o.theme;
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) p->cable[g][c] = o.cable[g][c];
  for (int l = 0; l < ENG_LANES; ++l) {
    for (int i = 0; i < ENG_STEPS / 8; ++i) p->trig[l][i] = o.trig[l][i];
    for (int i = 0; i < NTP; ++i) p->rnd[l][i] = o.rnd[l][i];
    p->seed[l] = o.seed[l];
  }
  for (int i = 0; i < ENG_PLOCKS; ++i) p->lock[i] = o.lock[i];
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int i = 0; i < ENG_ENDLESS; ++i) p->endless[t][i] = o.endless[t][i];
    p->endless_len[t] = o.endless_len[t];
    p->endless_key[t] = o.endless_key[t];
  }
}

void upgrade_v6(project_t* p, const project_v6& o) {
  blank_v9(p);
  for (int i = 0; i < 37 * 4; ++i) p->val[i] = o.val[i];
  for (int i = 0; i < 37; ++i) p->algo[i] = o.algo[i];
  p->out_mode = o.out_mode;
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) p->cable[g][c] = o.cable[g][c];
  for (int l = 0; l < ENG_LANES; ++l) {
    for (int i = 0; i < ENG_STEPS / 8; ++i) p->trig[l][i] = o.trig[l][i];
    for (int i = 0; i < 19; ++i) p->rnd[l][i] = o.rnd[l][i];
    p->seed[l] = o.seed[l];
  }
  for (int i = 0; i < ENG_PLOCKS; ++i) p->lock[i] = o.lock[i];
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int i = 0; i < ENG_ENDLESS; ++i) p->endless[t][i] = o.endless[t][i];
    p->endless_len[t] = o.endless_len[t];
    p->endless_key[t] = o.endless_key[t];
  }
}

// a project of 0.3 (version 9, or an older one read as version 9) into 0.4's: Tides' four pages are the LFOs' (as a
// new project has them), the cables from Tides A1, A2, A3 come from the LFOs 1, 2, 3 (the others go, and those to
// Tides' knobs); Warps' knobs move to their faces (CARRIER, MOD, ALGORITHM, MIX full; TIMBRE, LEVEL 1, LEVEL 2,
// MODE: DRY and the frequency shifter in one), its lane's locks and randoms with them
void blank_v9(project_t* p) {           // a new project, but Warps' knobs where 0.3 had them (an older version without
  eng_blank(p);                         // Warps keeps these; remap_v9 then moves them)
  static const float kW[8] = {0.4f, 0.5f, 0.6f, 0.6f, static_cast<float>(WC_OFF), 1.0f, 0.0f, 0.0f};
  for (int k = 0; k < 8; ++k) p->val[PG_WARPS(k / 4) * 4 + k % 4] = kW[k];
}

int remap_v9_param(int q) {             // an old parameter's place (-1: gone)
  int pg = q / 4, k = q % 4;
  if (pg >= 12 && pg < 16) return -1;
  if (pg == PG_WARPS(0)) {
    static const int16_t kW0[4] = {PG_WARPS(0) * 4 + 2, PG_WARPS(1) * 4 + 0, PG_WARPS(1) * 4 + 1, PG_WARPS(1) * 4 + 2};
    return kW0[k];
  }
  if (pg == PG_WARPS(1)) return k < 2 ? PG_WARPS(0) * 4 + k : -1;   // (DRY and MODE: choices, never cabled)
  return q;
}

void remap_v9(project_t* p) {
  float* w0 = &p->val[PG_WARPS(0) * 4];
  float* w1 = &p->val[PG_WARPS(1) * 4];
  float algo = w0[0], timbre = w0[1], l1 = w0[2], l2 = w0[3], car = w1[0], mod = w1[1], dry = w1[2], mode = w1[3];
  w0[0] = car;
  w0[1] = mod;
  w0[2] = algo;
  w0[3] = 1.0f;
  w1[0] = timbre;
  w1[1] = l1;
  w1[2] = l2;
  w1[3] = static_cast<float>(mode >= 0.5f ? WM_SHIFT : dry >= 0.5f ? WM_DRY : WM_WARPS);
  static const int8_t kTp[7] = {2, 4, 5, 6, 8, 9, 10};   // the Warps lane's: ALGORITHM, TIMBRE, LEVEL 1, LEVEL 2, its
  int8_t r[ENG_NTP];                                     // steps' DECAY, PROB, DEPTH
  for (int i = 0; i < ENG_NTP; ++i) r[i] = 0;
  for (int i = 0; i < 7; ++i) r[kTp[i]] = p->rnd[LANE_WARPS][i];
  for (int i = 0; i < ENG_NTP; ++i) p->rnd[LANE_WARPS][i] = r[i];
  for (int i = 0; i < ENG_PLOCKS; ++i) {
    plock_t& l = p->lock[i];
    if (l.lane != LANE_WARPS) continue;
    if (l.tp < 7) l.tp = static_cast<uint8_t>(kTp[l.tp]);
    else l.lane = 0xFF;
  }
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) {
      cable_t& k = p->cable[g][c];
      int dst = remap_v9_param(k.dst);
      if (!k.depth || k.src == SRC_NONE || k.src > 3 || dst < 0) {   // (Tides A1..A3 were 1..3: the LFOs' numbers)
        k.src = SRC_NONE;
        k.dst = 0;
        k.depth = 0;
      } else {
        k.dst = static_cast<uint8_t>(dst);
      }
    }
  lfo_blank(p);
  p->algo[PG_DELAY(0)] = DL_SYNC;
}

// an old page's place now (-1: gone, Marbles' four)
int old_page(int op) {
  if (op < 16) return op;               // the tracks' Plaits, Tides
  if (op < 20) return -1;               // Marbles
  static const int8_t kMap[15] = {PG_MIX, PG_RSEND, PG_REVERB, PG_PAN, PG_STEP(0), PG_STEP(1), PG_STEP(2), PG_STEP(3),
                                  PG_GLOBAL, PG_ARP(0), PG_ARP(1), PG_ARP(2), PG_ARP(3), PG_WARPS(0), PG_WARPS(1)};
  return op - 20 < 15 ? kMap[op - 20] : -1;
}
int old_param(int op) {          // an old parameter's place now (-1: gone)
  int pg = old_page(op / 4), k = op % 4;
  if (pg < 0) return -1;
  if (pg == PG_GLOBAL) return k >= 2 ? PG_GLOBAL * 4 + k - 1 : -1;   // SEQUENCER and its TEMPO gone (Marbles' RATE was
  if (pg < 12 && pg % 3 == 2 && k < 2) return -1;                  // the tempo); a track's GATE and NOTE sources
  return pg * 4 + k;
}

template <int NP>
void upgrade_from(project_t* p, const project_old<NP>& o) {
  blank_v9(p);
  for (int i = 0; i < NP * 4; ++i) {
    int q = old_param(i);
    if (q >= 0) p->val[q] = o.val[i];
  }
  p->val[PG_GLOBAL * 4 + 0] = o.val[16 * 4 + 0];         // the tempo: Marbles' RATE
  for (int i = 0; i < 16; ++i) p->algo[i] = o.algo[i];
  p->out_mode = o.out_mode;
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) {
      cable_t k = o.cable[g][c];
      int dst = old_param(k.dst);
      if (k.depth && k.src > SRC_NONE && k.src < NSRC && dst >= 0) cable(p, g, c, k.src, dst, k.depth);
    }
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int i = 0; i < ENG_STEPS / 8; ++i) p->trig[t][i] = o.trig[t][i];
    for (int i = 0; i < 18; ++i) p->rnd[t][i] = i == 8 || i == 9 ? 0 : o.rnd[t][i];
    p->seed[t] = o.seed[t];
    for (int i = 0; i < ENG_ENDLESS; ++i) p->endless[t][i] = o.endless[t][i];
    p->endless_len[t] = o.endless_len[t];
    p->endless_key[t] = o.endless_key[t];
  }
  int j = 0;
  for (int i = 0; i < ENG_PLOCKS; ++i) {
    const lock_v5& l = o.lock[i];
    if (l.tp >= 18 || l.tp == 8 || l.tp == 9) continue;
    p->lock[j].lane = static_cast<uint8_t>(l.ts >> 6);
    p->lock[j].step = static_cast<uint8_t>(l.ts & 63u);
    p->lock[j].tp = l.tp;
    p->lock[j].v = l.v;
    ++j;
  }
}

}  // namespace

extern "C" {

float eng_dice_span(int param) { return dice_span(param); }

void eng_bend_preview(int t, float* y, int n) {   // a cycle of a sine through the bits as they are (CLOCK: its
  if (t < 0 || t >= ENG_TRACKS || n < 2) return; // multiple of the cycle)
  Bend g = bend[t];
  g.hold = g.cnt = 0.0f;
  g.pos = g.zc = 0;
  g.dx = g.dy = 0.0f;
  g.rate = g.ci >= 0 ? kClockRatio[g.ci] / static_cast<float>(n) : 1.0f;
  if (g.rate > 1.0f) g.rate = 1.0f;
  for (int pass = 0; pass < 7; ++pass)            // (its divider and its capacitor settled: six cycles first)
    for (int i = 0; i < n; ++i) {
      float x = 0.9f * fm_sinf(6.2831853f * static_cast<float>(i) / static_cast<float>(n));
      float v = g.on ? bend_one(g, x) : x;
      if (pass == 6) y[i] = clampf(v, -1.2f, 1.2f);
    }
}
void eng_bend_mask(int t, uint8_t* inv, uint8_t* cut) {
  int m = t >= 0 && t < ENG_TRACKS ? bend[t].mask : 0;
  if (inv) *inv = kMaskInv[m];
  if (cut) *cut = kMaskCut[m];
}
int eng_glitch_state(int t, int* reps, float* speed) {
  if (t < 0 || t >= ENG_TRACKS) return 0;
  if (reps) *reps = glitch[t].reps;
  if (speed) *speed = glitch[t].speed;
  return glitch[t].on;
}
float eng_filter_hz(int t) { return mf_cutoff_hz(t); }
float eng_filter_env(int t) { return mf_env(t); }
uint32_t eng_filter_load(int t) {
  return t >= 0 && t < ENG_TRACKS && proj.algo[PG_FILT(t, 0)] ? pct10(static_cast<uint32_t>(filt_cost[t]), kN) : 0u;
}

void eng_rnd_redraw(project_t* p, int lane, int tp) {
  if (lane >= 0 && lane < ENG_LANES && tp >= 0 && tp < ENG_NTP) p->rseed[lane][tp] = static_cast<uint8_t>(p->rseed[lane][tp] + 1u);
}

// a Plaits engine's model now (0.10: thirteen gone to the FM, on its own knobs, the nearest algorithm: *algo; -1:
// still Plaits')
int plaits_now(int sub, int* algo) {
  int m = eng_model_find(MK_PLAITS, sub);
  *algo = -1;
  if (m >= 0) return m;
  //                      VCF PD 6OP 6OP 6OP TERR SMCH CHIP VA SHPR FM2 FRMT HARM WTBL CHRD
  static const int8_t kTo[15] = {1, 1, 4, 4, 4, 5, 5, -1, 1, 0, 0, -1, 7, 5, 5};
  *algo = sub >= 0 && sub < 15 && kTo[sub] >= 0 ? kTo[sub] : 1;
  return eng_model_find(MK_FM, 0);
}

// the models renumbered: 0.9 (the drums first; Plaits' three drums gone to MIFM1's), 0.10 (thirteen of Plaits' gone
// to the FM), 0.10.1 (the FM's eight models one). A project of 0.8 or before takes its tracks' models from their old
// numbers (Plaits' 24, the LXR-02's four, ZERO), one of 0.9 from 0.9's (the drums, the FM drums, ZERO, then Plaits' 21
// in its order), one of 0.10.0 from its (ZERO, the FM's eight, then Plaits' eight); a model gone the FM, on its own
// knobs, ALGO the nearest
void models_renumbered(project_t* p, int from) {
  for (int t = 0; t < ENG_TRACKS; ++t) {
    int o = p->algo[PG_TRACK(t, 0)], m, kit = 0, algo = -1;
    if (from == 17) {
      if (o <= 10) m = o;
      else if (o <= 18) m = eng_model_find(MK_FM, 0), algo = o - 11;
      else m = o - 7;
    } else if (from == 16) {
      if (o <= 10) m = o;
      else m = plaits_now(o - 11, &algo);
    } else {
      if (o <= 20) m = plaits_now(o, &algo);
      else if (o <= 23) m = eng_model_find(MK_DRUM, o == 21 ? 0 : o == 22 ? 1 : 3), kit = 1;
      else if (o <= 27) m = eng_model_find(MK_LXR, o - 24);
      else m = eng_model_find(MK_ZERO, 0);
    }
    p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(m < 0 ? 0 : m);
    if (kit || algo >= 0) lx_kit(p, t);
    if (algo >= 0) p->val[PG_TRACK(t, 0) * 4 + 1] = (static_cast<float>(algo) + 0.5f) / 8.0f;
  }
}

int upgrade_before_16(project_t* p, int bytes);
int eng_upgrade(project_t* p, int bytes) {
  if (bytes == static_cast<int>(sizeof(project_t)) && eng_check(p)) return 1;
  int v = p->magic == PROJECT_MAGIC ? p->version : 0;
  if (v >= 15 && v <= 17 && bytes == static_cast<int>(sizeof(project_t)) && p->size == sizeof(project_t)) {
    p->version = PROJECT_VERSION;
    models_renumbered(p, v);
    return 1;
  }
  if (!upgrade_before_16(p, bytes)) return 0;
  models_renumbered(p, 15);
  return 1;
}

int upgrade_before_16(project_t* p, int bytes) {
  static union {
    project_old<33> v4;
    project_old<35> v5;
    project_v6 v6;
    project_v7 v7;
    project_v8 v8;
    project_v9 v9;
    project_v12 v12;
    project_v13 v13;
    project_v14 v14;
  } u;
  if (bytes == static_cast<int>(sizeof(project_t)) && eng_check(p)) return 1;
  int v = p->magic == PROJECT_MAGIC ? p->version : 0;
  if (v == 14 && bytes == static_cast<int>(sizeof(project_v14)) && p->size == sizeof(project_v14)) {
    const uint8_t* from = reinterpret_cast<const uint8_t*>(p);
    uint8_t* to = reinterpret_cast<uint8_t*>(&u.v14);
    for (uint32_t i = 0; i < sizeof(project_v14); ++i) to[i] = from[i];
    upgrade_v14(p, u.v14);
    grit_gone(p);
    return 1;
  }
  if (v == 13 && bytes == static_cast<int>(sizeof(project_v13)) && p->size == sizeof(project_v13)) {
    const uint8_t* from = reinterpret_cast<const uint8_t*>(p);
    uint8_t* to = reinterpret_cast<uint8_t*>(&u.v13);
    for (uint32_t i = 0; i < sizeof(project_v13); ++i) to[i] = from[i];
    upgrade_v13(p, u.v13);
    grit_gone(p);
    return 1;
  }
  if (((v == 10 || v == 11) && bytes == static_cast<int>(kV11Bytes) && p->size == kV11Bytes) ||
      (v == 12 && bytes == static_cast<int>(sizeof(project_v12)) && p->size == sizeof(project_v12))) {
    const uint8_t* from = reinterpret_cast<const uint8_t*>(p);   // (0.4's and 0.5's: 0.5.6's, its draws 0; 0.4's
    uint8_t* to = reinterpret_cast<uint8_t*>(&u.v12);             // models all Plaits')
    for (uint32_t i = 0; i < sizeof(project_v12); ++i) to[i] = i < static_cast<uint32_t>(bytes) ? from[i] : 0;
    if (v != 12)
      for (int l = 0; l < ENG_LANES; ++l)
        for (int i = 0; i < 25; ++i) u.v12.rseed[l][i] = 0;
    upgrade_v12(p, u.v12);
    grit_gone(p);
    return 1;
  }
  uint32_t size = v == 9 ? sizeof u.v9 : v == 8 ? sizeof u.v8 : v == 7 ? sizeof u.v7 : v == 6 ? sizeof u.v6 : v == 5 ? sizeof u.v5
                : v == 4 ? sizeof u.v4 : 0;
  if (!size || bytes != static_cast<int>(size) || p->size != size) return 0;
  const uint8_t* from = reinterpret_cast<const uint8_t*>(p);
  uint8_t* to = reinterpret_cast<uint8_t*>(&u);
  for (uint32_t i = 0; i < size; ++i) to[i] = from[i];
  if (v == 9) upgrade_78<project_v9, 52, 25>(p, u.v9);
  else if (v == 8) upgrade_78<project_v8, 41, 21>(p, u.v8);
  else if (v == 7) upgrade_78<project_v7, 40, 20>(p, u.v7);
  else if (v == 6) upgrade_v6(p, u.v6);
  else if (v == 5) upgrade_from(p, u.v5);
  else upgrade_from(p, u.v4);
  remap_v9(p);                          // (read as 0.3's: then into 0.4's)
  grit_gone(p);
  return 1;
}

int eng_init(void) {
  uint32_t need = 0, a = 0;
  mf_init();                            // (the tracks' filters: their own static state)
  make_mu();                            // (the glitch's slices)
  for (int t = 0; t < ENG_TRACKS; ++t) gl_at[t] = -1;
  need = 4u * ((mp_size() + 7u) & ~7u) + ((mrv_size() + 7u) & ~7u) +
         ((mw_size() + 7u) & ~7u) + ((mdl_size() + 7u) & ~7u) + ((mms_size() + 7u) & ~7u) + ENG_TRACKS * kGlBytes;
  if (need > sizeof(arena)) return -1;
  for (int t = 0; t < ENG_TRACKS; ++t) {
    voice[t] = arena + a;
    a += (mp_size() + 7u) & ~7u;
    mp_init(voice[t]);
    vgate[t] = 0;
    vnote[t] = 48.0f;
    key_on[t] = 0;
  }
  lx_init();                            // (the FM drums: static, one of each a track)
  zero_init();                          // (ZERO's: its loops)
  drum_init();
  fm4_init();
  reverb = arena + a;
  a += (mrv_size() + 7u) & ~7u;
  mrv_init(reverb);
  warps = arena + a;
  a += (mw_size() + 7u) & ~7u;
  mw_init(warps);
  delay = arena + a;
  a += (mdl_size() + 7u) & ~7u;
  mdl_init(delay);
  master = arena + a;
  a += (mms_size() + 7u) & ~7u;
  mms_init(master);
  for (int t = 0; t < ENG_TRACKS; ++t) {  // the glitch's slices
    gl_mem[t] = arena + a;
    a += kGlBytes;
  }
  for (int l = 0; l < ENG_LANES; ++l)
    for (int tp = 0; tp < ENG_NTP; ++tp) {
      lane_param[l][tp] = static_cast<int16_t>(eng_track_param(l, tp));
      if (eng_tp_lockable(l, tp)) lane_lockable[l][tp >> 5] |= 1u << (tp & 31);
    }
  for (int t = 0; t < ENG_TRACKS; ++t)  // the random dose's spans
    for (int tp = 0; tp < ENG_NTP; ++tp)
      tp_dice[t][tp] = static_cast<uint8_t>(dice_span(lane_param[t][tp]) * 20.0f + 0.5f);
  n_fx_dice = 0;
  for (int i = 0; i < ENG_NPARAMS && n_fx_dice < kFxDice; ++i) {
    int lane;
    if (dice_span(i) != 0.0f && (eng_param_tp(i, &lane) < 0 || lane >= ENG_TRACKS)) {
      fx_dice[n_fx_dice] = static_cast<int16_t>(i);
      fx_span[n_fx_dice++] = dice_span(i);
    }
  }
  for (int t = 0; t < ENG_TRACKS; ++t) {
    eq_p[t] = new (eq_mem[t]) EqPair();
    eq_p[t]->f[0].Init();
    eq_p[t]->f[1].Init();
    eq_mode[t] = 0;
  }
  if (!eng_check(&proj)) eng_defaults(&proj);
  apply_cables();
  ready = 1;
  return 0;
}

// STOP twice (eng_cut): every sound cut. The block asked in fades its output out; the next one (core 1 idle: the
// render waits for its job before it returns) makes the voices, Warps, the delay, the reverb and the master bus new,
// their buffers silent: nothing sounds until the next trig (a drum ringing on a decay a cable holds at full, the
// effects' tails: gone)
volatile int cut_req;                   // 1: asked; 2: faded out, the modules next
void cut_now() {
  lx_init();                            // (the FM drums too: started again from their kits at the next block)
  zero_silence();                       // (ZERO: silent until its next trig, its loops kept)
  drum_silence();
  fm4_silence();
  for (int t = 0; t < ENG_TRACKS; ++t) mf_reset(t);   // (the filters' rings)
  for (int t = 0; t < ENG_TRACKS; ++t) {
    mp_init(voice[t]);
    vgate[t] = 0;                       // (a key still held: a new trig)
    quiet[t] = kQuiet;                  // (resting until then)
    bend[t].hold = bend[t].cnt = 0.0f;
    glitch[t].on = 0;
    glitch[t].mix = 0.0f;
    meter[t] = 0.0f;
  }
  mw_init(warps);
  mrv_init(reverb);
  mdl_init(delay);
  mms_init(master);
  for (int b = 0; b < 2; ++b) {
    for (int t = 0; t < ENG_TRACKS; ++t)
      for (uint32_t i = 0; i < kN; ++i) vbufs[b][t][i] = 0.0f;
    for (uint32_t i = 0; i < kN; ++i) w_outs[b][i] = w_drys[b][i] = 0.0f;
  }
  w_last = -1;                          // (Warps from its first block again: silence, then its job)
  w_job = 0;
  w_gate = w_gmix = 0.0f;               // (its oscillator shut: the transport is stopped)
  for (int l = 0; l < ENG_LANES; ++l) lane_fresh(l);
  fx_n = 0;                             // (no mix to play: the next block's output silent)
  meter[ENG_TRACKS] = meter[ENG_TRACKS + 1] = 0.0f;
}

void eng_play(int on) {
  if (on && !playing && ready) restart = 1;
  if (!on && playing) filt_stop = 1;    // (the render closes the filters: their state is its)
  if (!on)
    for (int t = 0; t < ENG_TRACKS; ++t) {
      raw_left[t] = 0.0f;
      rat_on[t] = 0;
    }
  playing = on != 0;
}
int eng_playing(void) { return playing; }

void eng_back_at_bar(const float* val, const uint8_t* algo, const int8_t* rnd) {
  back_pending = 0;
  for (int i = 0; i < ENG_NPARAMS; ++i) back_val[i] = val[i];
  for (int i = 0; i < ENG_NPAGES; ++i) back_algo[i] = algo[i];
  for (int t = 0; t < ENG_TRACKS; ++t)
    for (int i = 0; i < ENG_NTP; ++i) back_rnd[t][i] = rnd[t * ENG_NTP + i];
  back_pending = 1;
}
int eng_back_pending(void) { return back_pending; }
void eng_back_cancel(void) { back_pending = 0; }

void eng_key(int track, int note, int on) {
  if (track < 0 || track >= ENG_TRACKS) return;
  if (on) {
    key_note[track] = note;
    key_on[track] = 1;
    key_retrig[track] = 1;              // a new key: a new trig, even played legato
    key_hold[track] = static_cast<int>(kFrames) * 3;   // and at least three blocks, however short the press
  } else if (key_note[track] == note) {
    key_on[track] = 0;
  }
}

void eng_core1(int on) { use_core1 = on; }

void eng_cut(void) {
  eng_play(0);
  if (!cut_req) cut_req = 1;
}
int eng_cut_pending(void) { return cut_req != 0; }

const int16_t* eng_usb_frames(uint32_t n, int live) {
  usb_req = 2;                          // the host records: the next blocks make them
  if (!live || !usb_ok || n > kN) {     // (none made for this block: silence)
    for (uint32_t i = 0; i < kN * ENG_USB_CH; ++i) usb_ch[i] = 0;
    usb_ok = 0;
  }
  return usb_ch;
}

void eng_render(float* l, float* r, uint32_t n) {
  usb_ok = 0;
  if (!ready || n > kN || n < kFrames || !eng_check(&proj)) {
    for (uint32_t i = 0; i < n; ++i) l[i] = r[i] = 0.0f;
    return;
  }
  if (cut_req == 2) {                   // STOP twice: the modules made new, this block silent
    cut_now();
    for (uint32_t i = 0; i < n; ++i) l[i] = r[i] = 0.0f;
    cut_req = 0;
    return;
  }
  uint32_t t0 = plat_ticks24();
  // 0. what can start at once, on core 1 while core 0 prepares the block: the effects (the last block's mix) and
  // Warps (the last block's carrier and modulator), their knobs set at the end of the last block
  job_n = n;
  usb_now = usb_req != 0;               // the host records: the effects' job (the last block's mix) makes its channels
  if (usb_req) usb_req = static_cast<uint8_t>(usb_req - 1u);
  uint32_t early = fx_n ? 1u << kTaskFx : 0u;
  if (w_job) {
    early |= 1u << kTaskWarps;
    warps_calls = mw_calls(warps, n);
    task_cost[kTaskWarps] = warps_chunk_cost * static_cast<float>(warps_calls);
  }
  int c1ok = use_core1 && plat_cpu2_on(), jobs = 0;
  make_sub_costs();
  uint32_t ja = c1ok ? split_early(early, last_voices) : 0u, pending = 0;   // core 1's first job, its job outstanding
  uint32_t bno = (block_no + 1u) & 0xFFFFu;   // (this block's number: its offer's)
  block_no = bno;
  if (ja && plat_cpu2_run(core1_job, ja | bno << 16)) {
    pending = ja;
    jobs = 1;
  } else {
    ja = 0;
  }
  if (back_pending) {                   // ALL undone: in the block whose next sixteenth starts a bar (or stopped)
    if (!playing || restart || ((tick + 1) % 16 == 0 && six_len(step_len_now()) - step_clk <= static_cast<float>(n))) {
      for (int i = 0; i < ENG_NPARAMS; ++i) proj.val[i] = back_val[i];
      for (int i = 0; i < ENG_NPAGES; ++i) proj.algo[i] = back_algo[i];
      for (int t = 0; t < ENG_TRACKS; ++t)
        for (int i = 0; i < ENG_NTP; ++i) proj.rnd[t][i] = back_rnd[t][i];
      back_pending = 0;
    }
  }
  // 1. the LFOs, with the knobs and cables of the last block
  uint32_t pp[6];                       // (the preparation's parts, timed: the machine page's worst block)
#ifdef PLAT_WEB
  float prep_us = kWebPrep;             // (its floor and its cables, in the FM-1's shares: the stress test's worst
  for (int g = 0; g <= ENG_TRACKS; ++g) // block, LFO 5.1 %, CAB 1.6, SEQ 2.0, LANES 1.3, KNOBS 4.2)
    for (int c = 0; c < ENG_CABLES; ++c)
      if (proj.cable[g][c].depth && proj.cable[g][c].src != SRC_NONE) prep_us += kWebPrepCable;
  prep_us *= static_cast<float>(n) / 128.0f / 14.2f;
#endif
  run_lfos(n);
  WEB_SPEND(prep_us * 5.1f);
  pp[0] = plat_ticks24();
  // 2. every parameter, the cables over the knobs
  apply_cables();
  WEB_SPEND(prep_us * 1.6f);
  pp[1] = plat_ticks24();
  // 3. the trigs
  sequence(n);
  WEB_SPEND(prep_us * 2.0f);
  pp[2] = plat_ticks24();
  float open[kFx];
  fx_lanes(n, open);
  WEB_SPEND(prep_us * 1.3f);
  pp[3] = plat_ticks24();
  if (filt_stop) {                      // STOP: the filters' envelopes closed, their rings and noise faded out
    filt_stop = 0;
    for (int t = 0; t < ENG_TRACKS; ++t) mf_stop(t);
  }
  for (int t = 0; t < ENG_TRACKS; ++t) {
    set_voice(t);
    set_bend(t);
    set_filter(t);
  }
  WEB_SPEND(prep_us * 4.2f);
  // 4. the voices (those not resting) on both cores: core 1's handed over as soon as it is done with its first job;
  // core 0 meanwhile plays the early tasks it kept, then its voices, the costliest first
  uint32_t v = 0, rest = 0;
  for (int t = 0; t < ENG_TRACKS; ++t) {
    if (voice_rests(t)) rest |= 1u << t;
    else v |= 1u << t;
  }
  for (int t = 0; t < ENG_TRACKS; ++t)
    if ((rest >> t) & 1u) render_voice(t, n);   // (silence: nothing to share)
  last_voices = v;
  pp[4] = plat_ticks24();
  uint32_t prep_t = pp[4] - t0;
  prep_cost += (static_cast<float>(prep_t) - prep_cost) * 0.125f;
#ifdef ENG_PROFILE
  eng_prof[0] += plat_ticks24() - t0;
#endif
  uint32_t jb = c1ok ? split_voices(v, cost_of(ja), prep_cost + cost_of(early & ~ja), 0) : 0u;
  uint32_t mine = (early & ~ja) | (v & ~jb), done = 0;
  int offered = 0;
  if (jb && pending) {                  // core 1 still on its first job: its voices offered, taken as it ends
    offer_chk = ~(jb | bno << 8);
    offer = jb | bno << 8;
    plat_cpu2_sync();
    offered = 1;
  }
  auto hand_b = [&](int block) -> int { // core 1's voices: taken, or handed to it if it is free (1); offered, core 0
    if (!c1ok) return 0;                // waits for it (block), never plays them meanwhile
    if (pending) {
      if (!block && plat_cpu2_busy()) return 0;   // (still on its first job)
      int failed = plat_cpu2_wait() != 0;
      plat_cpu2_sync();
      int took = offered && !failed && claim == bno;
      offered = 0;
      offer = 0;
      if (failed) {                     // given up meanwhile: its job dropped, played here; core 1's voices too
        for (int i = 0; i < kTasks; ++i)
          if ((pending >> i) & 1u) run_task(i, n);
        c1ok = 0;
        jobs = 0;
        pending = 0;
        return 0;
      }
      pending = 0;
      if (took) {                       // played with its first job
        c1_job_ticks[1] = 0;
        jobs |= 2;
        return 1;
      }
    }
    if (!plat_cpu2_run(core1_job, jb | 1u << 8)) return 0;
    pending = jb;
    jobs |= 2;
    return 1;
  };
  while (mine) {                        // core 0's own: the early ones first, then the voices by cost
    int pick = -1;
    for (int i = kTasks - 1; i >= 0; --i) {
      if (!((mine >> i) & 1u)) continue;
      if (pick < 0 || (i < ENG_TRACKS && pick < ENG_TRACKS && task_cost[i] > task_cost[pick])) pick = i;
    }
    if (jb && !(jobs & 2)) hand_b(0);
    run_task(pick, n);
    mine &= ~(1u << pick);
    done |= 1u << pick;
  }
  while (jb && !(jobs & 2)) {           // core 1 still busy (or gone): its voices one by one here, the rest to it as
    if (hand_b(offered)) break;         // soon as it is free (offered: they are its, once its first job ends)
    int pick = -1;
    for (int i = 0; i < ENG_TRACKS; ++i)
      if (((jb >> i) & 1u) && (pick < 0 || task_cost[i] < task_cost[pick])) pick = i;
    run_task(pick, n);
    jb &= ~(1u << pick);
  }
  uint32_t c0 = plat_ticks24() - t0, c1 = 0;
  if (pending && plat_cpu2_wait() != 0) {   // given up meanwhile: the job dropped, played here
    for (int i = 0; i < kTasks; ++i)
      if ((pending >> i) & 1u) run_task(i, n);
    jobs = 0;
  }
  if (jobs & 1) c1 += c1_job_ticks[0];
  if (jobs & 2) c1 += c1_job_ticks[1];
  uint32_t on1 = ((jobs & 1) ? ja : 0u) | ((jobs & 2) ? jb : 0u);   // (the tasks core 1 played: the machine page)
  int on_core1 = jobs != 0;
  uint32_t todo = early | v;
  on_core1_last = on_core1;
#ifdef ENG_PROFILE
  uint32_t tj = plat_ticks24();
#endif
  for (int i = 0; i < ENG_TRACKS; ++i)  // the costs: averaged over ~8 blocks (a voice's while it plays)
    if ((todo >> i) & 1u) {
      task_cost[i] += (static_cast<float>(task_ticks[i]) - task_cost[i]) * 0.125f;
      filt_cost[i] += (static_cast<float>(filt_ticks[i]) - filt_cost[i]) * 0.125f;   // (its filter's share)
    }
  if (w_job && warps_calls)
    warps_chunk_cost += (static_cast<float>(task_ticks[kTaskWarps]) / static_cast<float>(warps_calls) -
                         warps_chunk_cost) * 0.125f;
  if (fx_n) task_cost[kTaskFx] += (static_cast<float>(task_ticks[kTaskFx]) - task_cost[kTaskFx]) * 0.125f;
  // the output: the last block's, through its effects and the master bus (none yet, or another size: silence)
  if (fx_n == n) {
    for (uint32_t i = 0; i < n; ++i) l[i] = fx_out_l[i], r[i] = fx_out_r[i];
  } else {
    for (uint32_t i = 0; i < n; ++i) l[i] = r[i] = 0.0f;
  }
  usb_ok = usb_now && fx_n == n;
  if (cut_req == 1) {                   // STOP twice: faded out over this block, the modules made new at the next
    float g = 1.0f, dg = 1.0f / static_cast<float>(n);
    for (uint32_t i = 0; i < n; ++i) {
      g -= dg;
      l[i] *= g;
      r[i] *= g;
      if (usb_ok)
        for (uint32_t c = 0; c < ENG_USB_CH; ++c) {
          int16_t* u = &usb_ch[i * ENG_USB_CH + c];
          *u = static_cast<int16_t>(static_cast<float>(*u) * g);
        }
    }
    cut_req = 2;
  }
  // 5. what this block's mix needs (the next block's task plays it, with the effects): Warps' input and routing, the
  // gains and sends, the duck; then the blocks swap (the voices' and Warps' next ones where the mix took these)
  float pk[ENG_TRACKS + 2];
  for (int t = 0; t < ENG_TRACKS; ++t) pk[t] = fx_n ? mj.pk[t] : 0.0f;   // (the last block's mix's, before mj changes)
  const int fd = LANE_DELAY - ENG_TRACKS, fr = LANE_REVERB - ENG_TRACKS, fw = LANE_WARPS - ENG_TRACKS;
  route_warps(n, fx_open[fw], open[fw]);
  uint32_t g = 0;
  for (int t = 0; t < ENG_TRACKS; ++t) g |= vgate[t] ? 1u << t : 0u;
  gates_now = g;
  // the gains: each block's targets, glided (a trig's random pan, a lock on the level, a cable), and reached sample by
  // sample over the block from the last block's: a gain that jumps at a block's edge is a click on what plays
  float inv = 1.0f / static_cast<float>(n);
  for (int t = 0; t < ENG_TRACKS; ++t) {
    // the pan: equal power (a track as loud wherever it sits: 0.71 a side in the middle, 1 on one side)
    pan_s[t] += (E(t, TP_PAN) - pan_s[t]) * 0.5f;
    lev_s[t] += (E(t, TP_LEVEL) - lev_s[t]) * 0.5f;
    snd_s[t] += (E(t, TP_SEND) - snd_s[t]) * 0.5f;
    dsnd_s[t] += (E(t, TP_DSEND) - dsnd_s[t]) * 0.5f;
    float a = (clampf(pan_s[t], -1.0f, 1.0f) + 1.0f) * 0.785398f, level = lev_s[t], gain = level * 0.7f;
    float tl = level * 0.85f * fm_cosf(a), tr = level * 0.85f * fm_sinf(a);
    // the sends: post-fader, before the pan (both effects are stereo themselves), each through its lane's opening
    float ts = snd_s[t] * gain * open[fr], td = dsnd_s[t] * gain * open[fd];
    mj.gain[t] = gain;
    mj.gl[t] = mix_gl[t];
    mj.gr[t] = mix_gr[t];
    mj.send[t] = mix_send[t];
    mj.dsend[t] = mix_dsend[t];
    mj.dgl[t] = (tl - mix_gl[t]) * inv;
    mj.dgr[t] = (tr - mix_gr[t]) * inv;
    mj.dsend_d[t] = (ts - mix_send[t]) * inv;
    mj.ddsend_d[t] = (td - mix_dsend[t]) * inv;
    mix_gl[t] = tl;
    mix_gr[t] = tr;
    mix_send[t] = ts;
    mix_dsend[t] = td;
    mj.duck0[t] = duck_depth[t];        // the duck's depth (T2..T4)
    mj.duck1[t] = duck_depth[t] = t ? E(t, TP_DUCK) : 0.0f;
    eq_s[t] += (E(t, TP_EQ) - eq_s[t]) * 0.5f;
    mj.eq[t] = eq_s[t];
  }
  for (int f = 0; f < kFx; ++f) fx_open[f] = open[f];
  mj.duck_rel = fm_exp2f(-1.442695f / (kSr * duck_tau(P(PG_DUCK, 0))));
  for (uint32_t k = 0; k < kFrames; ++k) mj.hit[k] = duck_hit[k];
  float rv = E(LANE_REVERB, 3) * 1.6f, dv = E(LANE_DELAY, 7) * 1.2f;
  fx_ret0 = mix_ret;                    // the returns' gains, from the last block's to this one's
  fx_ret1 = rv;
  fx_dret0 = mix_dret;
  fx_dret1 = dv;
  mix_ret = rv;
  mix_dret = dv;
  // the next block's early tasks' knobs: Warps' (and whether it runs), the effects' and the master bus's (they will
  // play this block's mix)
  set_warps();
  mrv_set(reverb, E(LANE_REVERB, 0), E(LANE_REVERB, 1), E(LANE_REVERB, 2));
  int div = iround(E(LANE_DELAY, 0));   // its time: a division of the tempo, or (FREE) in ms
  float dtime = proj.algo[PG_DELAY(0)] == DL_FREE ? delay_ms(E(LANE_DELAY, 0)) * kSr * 0.001f
                                                  : step_len_now() * kDivMult[div < 0 ? 0 : div >= kDivs ? kDivs - 1 : div];
  mdl_set(delay, dtime, E(LANE_DELAY, 1), E(LANE_DELAY, 2), E(LANE_DELAY, 4), E(LANE_DELAY, 3),
          iround(E(LANE_DELAY, 5)), E(LANE_DELAY, 6));
  mms_mode(master, proj.algo[PG_COMP] > MB_OTT ? static_cast<int>(MB_COMP) : proj.algo[PG_COMP]);
  mms_comp(master, comp_threshold(P(PG_COMP, 0)), comp_ratio(P(PG_COMP, 1)), comp_release(P(PG_COMP, 2)),
           P(PG_COMP, 3) * 12.0f);
  mms_ott(master, P(PG_OTT, 0), ott_time(P(PG_OTT, 1)), P(PG_OTT, 2) * 12.0f, P(PG_OTT, 3) * 12.0f);
  {
    float(*vb)[kN] = vbuf;              // the voices' next block where the mix takes this one
    vbuf = mbuf;
    mbuf = vb;
    float *o = w_out, *d = w_dry;       // and Warps'
    w_out = m_out;
    w_dry = m_dry;
    m_out = o;
    m_dry = d;
  }
  pk[ENG_TRACKS] = fx_pk[0];            // (the returns' peaks: the last block's, measured by its task)
  pk[ENG_TRACKS + 1] = fx_pk[1];
  fx_mode = proj.out_mode;
  fx_n = n;
  for (int t = 0; t < ENG_TRACKS + 2; ++t)   // the meters: up at once, down slowly
    meter[t] = pk[t] > meter[t] ? pk[t] : meter[t] * 0.985f;
  if (++hist_div >= 4u) {                 // the sources' past, for the screen
    hist_div = 0;
    for (int s = 1; s < NSRC; ++s) {
      float v = src[s] * 127.0f;
      hist[s][hist_at] = static_cast<int8_t>(v > 127.0f ? 127 : v < -127.0f ? -127 : static_cast<int>(v));
    }
    hist_at = (hist_at + 1u) % ENG_HIST;
  }
#ifdef ENG_PROFILE
  eng_prof[1] += plat_ticks24() - tj;
  eng_prof[2] += plat_ticks24() - t0;
#endif
  // the load: one-pole averages, the worst total held a second
  uint32_t tot = plat_ticks24() - t0;
  uint32_t p0 = pct10(c0, n), p1 = pct10(c1, n), pt = pct10(tot, n);
  load0 = (load0 * 15u + p0) / 16u;
  load1 = (load1 * 15u + p1) / 16u;
  if (pt > peak || ++peak_hold_blocks > 344u) {
    peak = pt;
    peak_hold_blocks = 0;
    for (int i = 0; i < kTasks; ++i) peak_of[i] = (todo >> i) & 1u ? task_ticks[i] : 0u;
    peak_of[kTasks] = prep_t;
    peak_of[kTasks + 1] = c0;
    peak_of[kTasks + 2] = c1;
    peak_of[kTasks + 3] = tot;
    peak_c1 = on1;
    peak_of[kTasks + 4] = pp[0] - t0;
    for (int i = 1; i < 5; ++i) peak_of[kTasks + 4 + i] = pp[i] - pp[i - 1];
    peak_n = n;
  }
  if (guard_on) {
    int lv = pt >= 1000u ? (guard_late ? 3 : 2) : pt >= 920u ? 1 : 0;
    if (pt >= 1000u) guard_late = 103;
    else if (guard_late) --guard_late;
    if (lv && lv >= guard) {
      guard = lv;
      guard_hold = 103;                 // (0.3 s)
    } else if (guard && --guard_hold <= 0) {
      --guard;
      guard_hold = 103;
    }
    mf_guard(guard);
    if (guard) ++guard_blocks;
  }
}

float eng_final(int param) { return param >= 0 && param < ENG_NPARAMS ? fin[param] : 0.0f; }
float eng_now(int param) {
  int lane, tp = eng_param_tp(param, &lane);
  if (tp >= 0 && tp < ENG_NTP && lane >= 0 && lane < ENG_LANES && lane_param[lane][tp] == param) return E(lane, tp);
  return eng_final(param);
}
float eng_src(int s) { return src_value(s); }
float eng_lfo_phase(int i) { return i >= 0 && i < 3 ? lfo[i].phase : 0.0f; }
uint32_t eng_gates(void) { return gates_now; }
int eng_note(int t) { return t >= 0 && t < ENG_TRACKS ? static_cast<int>(vnote[t] + 0.5f) : 0; }
uint32_t eng_load(int core) { return core ? load1 : load0; }

// the delay's ALGORITHM, SYNC or FREE: its TIME the same (the division's length now in ms, or the nearest division)
void eng_delay_mode(int mode) {
  mode = mode == DL_FREE ? DL_FREE : DL_SYNC;
  if (mode == proj.algo[PG_DELAY(0)]) return;
  float& v = proj.val[PG_DELAY(0) * 4];
  float sl_ms = step_len_now() * 1000.0f / kSr;
  if (mode == DL_FREE) {
    int d = iround(v);
    float ms = sl_ms * kDivMult[d < 0 ? 0 : d >= kDivs ? kDivs - 1 : d];
    v = clampf(fm_log2f(ms > 1.0f ? ms : 1.0f) / 10.228819f, 0.0f, 1.0f);
  } else {
    float ms = delay_ms(v), best = 1e30f;
    int pick = 0;
    for (int d = 0; d < kDivs; ++d) {
      float e = fm_fabsf(fm_log2f(sl_ms * kDivMult[d]) - fm_log2f(ms));
      if (e < best) best = e, pick = d;
    }
    v = static_cast<float>(pick);
  }
  proj.algo[PG_DELAY(0)] = static_cast<uint8_t>(mode);
}
uint32_t eng_peak_part(int i) {         // the worst block's: 0..3 the voices, 4 Warps, 5 the effects, 6 the
  return i >= 0 && i < kTasks + 9 ? pct10(peak_of[i], peak_n) : 0u;   // preparation, 7 core 0, 8 core 1, 9 all,
}                                                                      // 10..14 the preparation's parts
uint32_t eng_prep_load(void) { return ready ? pct10(static_cast<uint32_t>(prep_cost), kN) : 0u; }
uint32_t eng_peak(void) { return peak; }
int eng_guard(void) { return guard; }
uint32_t eng_peak_core1(void) { return peak_c1; }
uint32_t eng_guard_blocks(void) { return guard_blocks; }
void eng_guard_test(int on, int level) {   // (the tests: the guard on, or held at a level)
  guard_on = on;
  if (level >= 0) guard = level, mf_guard(level);
}
float eng_meter(int t) { return t >= 0 && t < ENG_TRACKS + 2 ? meter[t] : 0.0f; }
float eng_duck(void) { return duck_s; }
float eng_gr(int band) { return ready ? mms_gain(master, band) : 0.0f; }
int eng_hist(int s, int i) {
  if (s <= SRC_NONE || s >= NSRC || i < 0 || i >= ENG_HIST) return 0;
  return hist[s][(hist_at + static_cast<uint32_t>(i)) % ENG_HIST];
}
int eng_core1_running(void) { return on_core1_last; }
uint32_t eng_task_load(int task) {
  if (task < 0 || task >= kTasks) return 0;
  return task == kTaskWarps && !w_job ? 0 : pct10(static_cast<uint32_t>(task_cost[task]), kN);
}

int eng_warps_algo(void) {
  int car = Pi(PG_WARPS(0), 0);
  if (car <= WC_OFF || car > WC_OSC3) return -1;
  return Pi(PG_WARPS(1), 3) == WM_SHIFT ? 6 : warps_pos(E(LANE_WARPS, 2));
}
const char* eng_warps_name(int algo) { return algo >= 0 && algo < 7 ? kWarpsNames[algo] : "WARPS"; }
int eng_warps_track(void) { return w_dst; }

int eng_trig(int lane, int step) {
  if (lane < 0 || lane >= ENG_LANES || step < 0 || step >= ENG_STEPS) return 0;
  return (proj.trig[lane][step / 8] >> (step % 8)) & 1;
}
void eng_trig_set(int lane, int step, int on) {
  if (lane < 0 || lane >= ENG_LANES || step < 0 || step >= ENG_STEPS) return;
  if (on) proj.trig[lane][step / 8] = static_cast<uint8_t>(proj.trig[lane][step / 8] | (1u << (step % 8)));
  else proj.trig[lane][step / 8] = static_cast<uint8_t>(proj.trig[lane][step / 8] & ~(1u << (step % 8)));
  if (!on && !eng_lane_has_trigs(lane)) lane_fresh(lane);   // (its last trig gone: its last lock with it)
}
void eng_fills(int dice, int* every, int* beats, int* tracks) {
  int q = fill_amount_q(dice), span = 0;
  *every = *tracks = 0;
  if (q) fill_shape(q, every, &span, tracks);
  *beats = span / 4;
}
uint32_t eng_fill_trigs(int t) { return t >= 0 && t < ENG_TRACKS ? fill_trigs[t] : 0u; }

int eng_lane_has_trigs(int lane) {
  if (lane < 0 || lane >= ENG_LANES) return 0;
  int len = iround(proj.val[PG_STEP(lane) * 4 + 3]);
  len = len < 1 ? 1 : len > ENG_STEPS ? ENG_STEPS : len;
  for (int b = 0; b * 8 < len; ++b) {
    uint32_t bits = proj.trig[lane][b];
    if (len - b * 8 < 8) bits &= (1u << (len - b * 8)) - 1u;   // (only the steps it plays)
    if (bits) return 1;
  }
  return 0;
}
int eng_lock(int lane, int step, int tp, float* v) {
  int i = lock_find(lane, step, tp);
  if (i < 0) return 0;
  if (v) *v = from_u(eng_track_param(lane, tp), static_cast<float>(proj.lock[i].v) / 255.0f);
  return 1;
}
int eng_lock_set(int lane, int step, int tp, float v) {   // the value first, the slot's names last (the render reads it)
  if (!eng_tp_lockable(lane, tp) || step < 0 || step >= ENG_STEPS) return -1;
  int i = lock_find(lane, step, tp);
  uint8_t u = static_cast<uint8_t>(clampf(to_u(eng_track_param(lane, tp), v), 0.0f, 1.0f) * 255.0f + 0.5f);
  if (i < 0)
    for (int j = 0; j < ENG_PLOCKS && i < 0; ++j)
      if (proj.lock[j].lane == 0xFF) i = j;
  if (i < 0) return -1;
  proj.lock[i].v = u;
  proj.lock[i].step = static_cast<uint8_t>(step);
  proj.lock[i].tp = static_cast<uint8_t>(tp);
  proj.lock[i].lane = static_cast<uint8_t>(lane);
  ++lock_gen;
  return 0;
}
void eng_lock_clear(int lane, int step) {
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (proj.lock[i].lane == lane && proj.lock[i].step == step) proj.lock[i].lane = 0xFF;
  ++lock_gen;
}
void eng_steps_clear(int lane) {        // a lane's pattern: its trigs and its locks (its knobs and randoms stay)
  if (lane < 0 || lane >= ENG_LANES) return;
  for (int b = 0; b < ENG_STEPS / 8; ++b) proj.trig[lane][b] = 0;
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (proj.lock[i].lane == lane) proj.lock[i].lane = 0xFF;
  ++lock_gen;
  lane_fresh(lane);                     // (the last trig's locks and randoms go with them)
}
void eng_fresh(void) {                   // another project: no trig's lock or random left from the last one
  for (int l = 0; l < ENG_LANES; ++l) lane_fresh(l);
  ++lock_gen;
}
int eng_step_locked(int lane, int step) {
  int n = 0;
  for (int i = 0; i < ENG_PLOCKS; ++i) n += proj.lock[i].lane == lane && proj.lock[i].step == step;
  return n;
}
int eng_step_now(int lane) {
  return playing && lane >= 0 && lane < ENG_LANES && tick >= 0 ? cur_step[lane] : -1;
}
float eng_lane_open(int lane) {
  return lane >= ENG_TRACKS && lane < ENG_LANES ? fx_open[lane - ENG_TRACKS] : 1.0f;
}
int eng_quantize(int note) { return quantize_slow(note, Pi(PG_GLOBAL, 1), Pi(PG_GLOBAL, 2)); }
uint32_t eng_rises(int t) { return t >= 0 && t < ENG_TRACKS ? vrises[t] : 0u; }

void eng_endless_rec(int t, int on) {
  if (t < 0 || t >= ENG_TRACKS) return;
  e_rec[t] = on != 0;
  if (!on) {
    raw_left[t] = 0.0f;
    e_clk[t] = 1e9f;                    // the first note at the next frame
    e_pos[t] = -1;
    e_dir[t] = 1;
  }
}
int eng_endless_add(int t, int note) {
  if (t < 0 || t >= ENG_TRACKS || proj.endless_len[t] >= ENG_ENDLESS) return -1;
  proj.endless[t][proj.endless_len[t]] = static_cast<uint8_t>(note == ENDLESS_REST ? ENDLESS_REST : note & 127);
  ++proj.endless_len[t];
  return 0;
}
void eng_endless_clear(int t) {
  if (t < 0 || t >= ENG_TRACKS) return;
  proj.endless_len[t] = 0;
  proj.endless_key[t] = 0;
}
int eng_endless_on(int t) {
  return t >= 0 && t < ENG_TRACKS && proj.endless_len[t] > 0 && !e_rec[t];
}
void eng_endless_key(int t, int note) {   // from its first note
  if (!eng_endless_on(t)) return;
  for (int i = 0; i < proj.endless_len[t]; ++i)
    if (proj.endless[t][i] != ENDLESS_REST) {
      int k = note - proj.endless[t][i];
      proj.endless_key[t] = static_cast<int8_t>(k < -48 ? -48 : k > 48 ? 48 : k);
      return;
    }
}
int eng_endless_pos(int t) { return eng_endless_on(t) && playing ? e_pos[t] : -1; }

}  // extern "C"
