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
#include "mi_metal.h"
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
const char* const kLfoWaves[LW_N] = {"Sine",  "Triangle", "Saw",   "Ramp",   "Square", "S&H",    "Smooth", "Pump",
                                     "Stairs", "Clave",   "Bounce", "Rises", "Falls",  "Zigzag", "Chaos",  "Follow"};
const char* const kLfoModes[6] = {"Free", "Sync", "T1", "T2", "T3", "T4"};
const char* const kOnOff[2] = {"Off", "On"};
const char* const kScaleNames[22] = {"Off", "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
                                     "Locrian", "Harm. minor", "Mel. minor", "Maj. penta", "Min. penta", "Blues",
                                     "Whole tone", "Hirajoshi", "Prog", "Phryg. dom", "Flip",
                                     "Follow T1", "Follow T2", "Follow T3", "Follow T4"};
// (Prog: PADS' chord, as it goes; 0.16.12, after it (the projects' scales where they were): the phrygian dominant
// (its major third over the flat second), FLIP (minor one bar, dorian the next: the sixth flat, then not); 0.16.42,
// FOLLOW T1..T4: that track's chord, whatever plays it (a track that plays chords: SCALE turns past the others))
const int kScaleProg = 15, kScaleFlip = 17, kScaleT1 = 18, kScales = 22;
const uint16_t kScaleMasks[17] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B, 0x9AD, 0xAAD, 0x295, 0x4A9,
                                  0x4E9, 0x555, 0x18D, 0, 0x5B3};   // bit i: i semitones over the root
const char* const kNoteName[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kArpRates[6] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4"};
const float kArpMult[6] = {0.5f, 2.0f / 3.0f, 1.0f, 4.0f / 3.0f, 2.0f, 4.0f};   // in sixteenths
const char* const kArpDir[6] = {"Forward", "Backward", "Ping-pong", "Random", "+2-1", "Converge"};   // (0.16.13: two
                                        // on one back; from both ends to the middle)
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
  {{"Note", K_INT, -24, 24, 0}, {"Prob", K_UNI, 0, 0, 0}, {"Ratch", K_INT, 1, ENG_RATCH_MAX, 0}, {"Length", K_INT, 1, 64, 0}},
  {{"Decay", K_UNI, 0, 0, 0}, {"Prob", K_UNI, 0, 0, 0}, {"Depth", K_UNI, 0, 0, 0}, {"Length", K_INT, 1, 64, 0}},
  {{"Tempo", K_UNI, 0, 0, 0}, {"Scale", K_ENUM, 0, kScales - 1, kScaleNames}, {"Root", K_ENUM, 0, 11, kNoteName},
   {"Swing", K_UNI, 0, 0, 0}},
  {{"Rate", K_ENUM, 0, 5, kArpRates}, {"Gate", K_UNI, 0, 0, 0}, {"Dir", K_ENUM, 0, 5, kArpDir},
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
  {{"Fills", K_INT, 0, 64, 0}, {"Rotate", K_INT, 0, 63, 0}, {"Climb", K_INT, -12, 12, 0}, {"Spread", K_UNI, 0, 0, 0}},
  {{"Cutoff", K_UNI, 0, 0, 0}, {"Reso", K_UNI, 0, 0, 0}, {"Env", K_BIP, 0, 0, 0}, {"Decay", K_UNI, 0, 0, 0}},
  {{"Drive", K_UNI, 0, 0, 0}, {"Ping", K_UNI, 0, 0, 0}, {"FM", K_BIP, 0, 0, 0}, {"Key", K_UNI, 0, 0, 0}},
  {{"Type", K_ENUM, 0, 2, kFiltTypes}, {"Noise", K_UNI, 0, 0, 0}, {"Color", K_BIP, 0, 0, 0}, {"Attack", K_UNI, 0, 0, 0}},
  {{"Glitch", K_UNI, 0, 0, 0}, {"Size", K_ENUM, 0, GL_N - 1, kGlitchSizes}, {"Bend", K_BIP, 0, 0, 0},
   {"Drop", K_UNI, 0, 0, 0}},
  {{"Curve", K_BIP, 0, 0, 0}, {"Curve", K_BIP, 0, 0, 0}, {"Curve", K_BIP, 0, 0, 0}, {"Curve", K_BIP, 0, 0, 0}},
  {{"Source", K_ENUM, 0, 3, kTrackNames}, {"Gain", K_BIP, 0, 0, 0}, {"Thresh", K_UNI, 0, 0, 0},
   {"Filter", K_BIP, 0, 0, 0}},
};
const char* const kBusModes[2] = {"OFF", "COMP"};   // (0.16.43: OTT gone; a project's OTT read as COMP)
const char* const kDelayAlgos[2] = {"SYNC", "FREE"};
const eng_pdef_t kDelayFreeTime = {"Time", K_UNI, 0, 0, 0};   // the delay's TIME when FREE: 1 ms..1.2 s
const char* const kReverbAlgos[RV_ALGOS] = {"CLOUDS", "METAL", "SHIMMER"};
const eng_pdef_t kMetalKnobs[3] = {{"Decay", K_UNI, 0, 0, 0}, {"Size", K_UNI, 0, 0, 0}, {"Tone", K_UNI, 0, 0, 0}};

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
// FM drums (mi_lxr.cc), the SDS' (0.16.49, mi_sds.cc, after Simmons' SDS 8; their bank the sixth), MIFM1's own voices
// (ZERO, mi_zero.cc), then Plaits' (mi_plaits.cc: its 1.2 bank, its pitched one, its noises and physical models; its
// three drums gone to MIFM1's). What plays each (its kind), its index there,
// its bank, its name in four letters or so (the tracks' row)
struct ModelDef {
  const char* name;
  const char* abbr;
  uint8_t kind, sub, bank;
};
const ModelDef kModelDef[ENG_MODELS] = {
  {"KICK", "KICK", MK_DRUM, 0, 0}, {"SNARE", "SNAR", MK_DRUM, 1, 0}, {"CLAP", "CLAP", MK_DRUM, 2, 0},
  {"HAT", "HAT", MK_DRUM, 3, 0}, {"TOM", "TOM", MK_DRUM, 4, 0}, {"PERC", "PERC", MK_DRUM, 5, 0},
  {"FM DRUM", "FDRM", MK_LXR, 0, 1}, {"FM SNARE", "FSNR", MK_LXR, 1, 1},
  {"SDS KICK", "SKCK", MK_SDS, 0, 5}, {"SDS SNARE", "SSNR", MK_SDS, 1, 5}, {"SDS TOM", "STOM", MK_SDS, 2, 5},
  {"SDS PERC", "SPRC", MK_SDS, 3, 5},
  {"ZERO", "ZERO", MK_ZERO, 0, 2},
  {"FM", "FM", MK_FM, 0, 2}, {"PADS", "PADS", MK_PADS, 0, 2}, {"POLY", "POLY", MK_POLY, 0, 2},
  {"REV3", "REV3", MK_PROPHET, 0, 2}, {"TAURUS", "TAUR", MK_TAURUS, 0, 2},
  {"VOCODER", "VOCO", MK_VOCODER, 0, 2}, {"CHOIR", "CHOR", MK_CHOIR, 0, 2}, {"STATIC", "STAT", MK_STATIC, 0, 2},
  {"SPEECH", "SPCH", MK_PLAITS, 15, 3},
  {"SWARM", "SWRM", MK_PLAITS, 16, 4}, {"NOISE", "NOIS", MK_PLAITS, 17, 4}, {"PARTICLES", "PART", MK_PLAITS, 18, 4},
  {"STRING", "STRG", MK_PLAITS, 19, 4}, {"MODAL", "MODL", MK_PLAITS, 20, 4},
};
// (0.16.31) each model's level, so that they come out together: their own sounds as ALGORITHM brings them, measured
// (BS.1770 loudness: the loudest 400 ms of a held note, and of quarter notes), brought to -21 LUFS for the synths (they
// were spread from -16, ZERO, to -30, POLY and SPEECH), -22 the kicks, -24 the snares and the toms, -25 the clap, -26 the
// perc, -28 the cymbal, -29 the hats (from -20 to -35). In quarter decibels, on the voice as it comes out (before its
// bits, its filter, Warps). (0.16.43: CHOIR's a held C5 as loud as the synths' held notes, -16 LUFS on their
// average here, measured; 0.16.49: the SDS' as MIFM1's drums, -22 the kick, -24 the snare and the tom, -26 the perc)
const int8_t kModelTrim[ENG_MODELS] = {13, 10, 13, 18, 3, 8, -7, 0, -12, -23, -31, -30, -21, -17, -12, 30, 20, 8, -3, 2, 8,
                                       35, 1, 14, 10, 32, -8};
const int kModels34 = 25, kOtis34 = 15;   // (0.16.24 to 0.16.42's numbers: 25 models, OTIS 15)
const int8_t kFrom34[kModels34] = {0, 1, 2, 3, 4, 5, 6, 7, 3, 3, 8, 9, 10, 11, 12, 11, 13, 14, 16, 17, 18, 19, 20, 21, 22};
const char* const kBankNames[ENG_BANKS] = {"DRUMS", "FM DRUMS", "MIFM1", "PLAITS", "NOISE & PHYSICAL", "SDS"};
const int kSdsAt = 8, kSdsN = 4;        // (0.16.49: the SDS' four at 8, the models from there four further on)
inline const ModelDef& mdef(int m) { return kModelDef[m < 0 ? 0 : m >= ENG_MODELS ? ENG_MODELS - 1 : m]; }

const char* const kSrcNames[NSRC] = {"-", "L1", "L2", "L3"};

// a synced LFO's rate: cycles a beat
const float kLfoRatios[9] = {4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 16.0f, 1.0f / 32.0f, 1.0f / 64.0f};
const char* const kLfoRatioNames[9] = {"4 / beat", "2 / beat", "1 / beat", "1 / 2 beats", "1 / bar",
                                         "1 / 2 bars", "1 / 4 bars", "1 / 8 bars", "1 / 16 bars"};

int page_type(int page) {
  if (page >= PG_FOLLOW(0)) return PT_FOLLOW;
  if (page >= PG_GLITCH(0)) return PT_GLITCH;
  if (page >= PG_FILT(0, 0)) return PT_FILT1 + (page - PG_FILT(0, 0)) % 3;
  if (page < 12) return PT_PLAITS1 + page % 3;
  if (page == PG_CURVE) return PT_CURVE;
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
// (0.16.23) FOLLOW's: ATTACK (RATE's place) 0.5 ms..1 s, DECAY (SLEW's) 5 ms..5 s; its input's GAIN -24..+24 dB,
// THRESH -60..0 dB, FILTER (to the left a low pass from 18 kHz down to 100 Hz, to the right a high pass from 20 Hz up
// to 5 kHz, 12 dB an octave, off in its middle)
MI_COLD float fol_att(float v) { return 0.0005f * fm_exp2f(v * 10.966f); }
MI_COLD float fol_dec(float v) { return 0.005f * fm_exp2f(v * 9.966f); }
MI_COLD float fol_db(float v) { return -60.0f + 60.0f * v; }
MI_COLD float fol_hz(float v) { return v < 0.0f ? 18000.0f * fm_exp2f(v * 7.492f) : 20.0f * fm_exp2f(v * 7.966f); }
float delay_ms(float v) { return fm_exp2f(v * 10.228819f); }                // the delay's FREE time: 1..1200 ms
float gate_frac(float v) { return 0.05f + 0.9f * v; }                         // LENGTH: 5..95 % of the sixteenth
float lane_tau(float v) { return 0.01f * fm_exp2f(v * 9.0f); }                // an effect lane's DECAY: 10 ms..5.1 s
float duck_tau(float v) { return 0.02f * fm_exp2f(v * 7.0f); }                // the duck's DECAY: 20 ms..2.6 s
// the master bus's knobs: the compressor's threshold (0..-36 dB), ratio (1..20), release (20 ms..1.3 s), gain
// (+/-12 dB)
float comp_threshold(float v) { return -36.0f * v; }
float comp_ratio(float v) { return 1.0f + 19.0f * v * v; }
float comp_release(float v) { return 0.02f * fm_exp2f(v * 6.0f); }

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
#else                                   /* the FM-1: 4 Plaits 74 KB (0.16.1: the eight engines MIFM1 plays, 100 KB
                                         * before), the reverb 32 (16-bit words), the delay 103, Warps 8, the
                                         * glitch's slices 32 (251.5 KB, 8.5 left; 32-bit sizes, tools/device_check.sh's
                                         * objects: mp_size and the others) */
#define ENG_ARENA_BYTES (260u * 1024u)
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
// (0.16.15) the keys held on a track, in the order pressed (the newest last): a voice of one plays the newest, back
// to the one before when it is let go; POLY (MODE Poly), PROPHET (CHORD Off) and SQ-1 play them all, a voice each
const int kKeys = 8;
volatile int kb_note[ENG_TRACKS][kKeys], kb_n[ENG_TRACKS];
volatile int key_new[ENG_TRACKS];       // a key pressed since the last block (the keys' chords: the filter's envelope)
int kb_poly(int t);                     // 1: the track's machine plays the keys' chords

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
int8_t rec_skip[ENG_TRACKS];            // (0.16.31) live recording: a step the keys wrote just ahead (heard already)
int32_t count_in;                       // the next PLAY's count-in, sixteenths
float seq_at;                           // the sixteenths since PLAY, as the sequencer last saw them
uint8_t click_on, clk_hi;               // the metronome: on, the beat's ping (at sample clk_at, -1 none; the bar's
int32_t clk_at = -1;                    // higher), its resonator
float clk_c, clk_y1, clk_y2;
float clk_out[ENG_CLICK_N];              // (0.16.34) this block's click, apart: the USB records the mix without it
uint8_t clk_n;                           // its samples this block (0: none)
int gl_at[ENG_TRACKS];                  // the glitch: this block's sixteenth (the sample it starts at; -1 none)
int gl_busy[ENG_TRACKS];                // its slice taken or repeating (the voice does not rest meanwhile)
float gl_six = 5000.0f;                 // a sixteenth, samples
void glitch_stop_all();
float raw_left[ENG_TRACKS];             // a step's gate: its samples left
int last_g[ENG_TRACKS];                 // the gate of the last frame
int rat_on[ENG_TRACKS], rat_n[ENG_TRACKS], rat_span[ENG_TRACKS], rat_i[ENG_TRACKS];
float rat_pos[ENG_TRACKS], rat_lvl[ENG_TRACKS];
float rat_t[ENG_TRACKS][17];            // the ratchets' hits: where each starts (samples into the trig), then their end
                                        // (0.16: over one step or several, CURVE drawing them closer or apart)
int32_t lane_cnt[ENG_LANES];            // (0.16) the steps each lane took since PLAY, at its SPEED (-1: none yet)
uint8_t climb_cnt[ENG_TRACKS][ENG_STEPS];   // (0.16.7) CLIMB: each step's place in its cycle (its plays since PLAY)
int8_t climb_off[ENG_TRACKS][ENG_STEPS];    // the transposition it played last (the screen's)
int climb_t[ENG_TRACKS], climb_amt[ENG_TRACKS], climb_base[ENG_TRACKS];   // the trig climbing: its step (-1: none),
                                        // its CLIMB (a lock's, a random's), its note before
int climb_dice[ENG_TRACKS];             // (0.16.8) 1: the dose's roll, climbing through its hits (not the step's CLIMB)
int32_t lane_grid[ENG_LANES];           // (0.16.6) its grid's point last seen (the sixteenths since PLAY at its SPEED,
int8_t lane_spd[ENG_LANES];             // whole), the SPEED it is at: another, a grid anew (0.16..0.16.5 took the
                                        // steps from the grid: back from x2 to x1, the lane waited as long as it had
                                        // played; down to x1/2, the same)
float lane_sl[ENG_LANES];               // its step's length now (samples)

// (0.16) a lane's SPEED: its step a multiple of the sixteenth; its DIRECTION; the tuplets of RATCH (9..16): so many
// hits over so many of the lane's steps
const float kSpeedMul[ENG_SPEEDS] = {0.125f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
const char* const kSpeedNames[ENG_SPEEDS] = {"x1/8", "x1/4", "x1/2", "x3/4", "x1", "x3/2", "x2", "x3", "x4"};
const char* const kDirNames[DIR_N] = {"Forward", "Backward", "Ping-pong", "Random", "Drunk", "2 fwd 1 back"};
const uint8_t kTuplet[8][2] = {{3, 2}, {3, 4}, {2, 3}, {4, 3}, {5, 4}, {6, 4}, {7, 4}, {3, 8}};   // (0.16.0's: hits,
                                                                                               // steps)
inline int lane_speed(int l) {          // 0..8 (4: x1)
  int v = proj.speed[l] + 4;
  return v < 0 ? 0 : v >= ENG_SPEEDS ? ENG_SPEEDS - 1 : v;
}
int lane_steps(int l) {                 // its pass, in its steps (there and back: twice, its ends once)
  int len = static_cast<int>(proj.val[PG_STEP(l) * 4 + 3] + 0.5f);
  len = len < 1 ? 1 : len > ENG_STEPS ? ENG_STEPS : len;
  return proj.dir[l] == DIR_PINGPONG && len > 1 ? 2 * len - 2 : len;
}
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
volatile int e_free[ENG_TRACKS], e_free_go[ENG_TRACKS];   // (0.16.11) Endless played by a key held, the sequencer
                                        // stopped: on; from its first note (the main loop's ask, taken by the render)
int e_pos[ENG_TRACKS], e_dir[ENG_TRACKS], e_note[ENG_TRACKS];
int32_t e_cnt[ENG_TRACKS];              // (0.16.2) Endless' steps since PLAY (its REST counts them)
int e_state[ENG_TRACKS];                // (0.16.3) what its last step did (ES_*: the screen shows it)
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
#if ENG_USB_TRACKS
alignas(4) int16_t usb_ch[kN * ENG_USB_CH];
volatile uint8_t usb_req;
uint8_t usb_now, usb_ok;                // this block's mix makes them; they hold this block's (else silence)
#else                                   // (0.16.33: set aside, the USB records the master: none made, nothing kept)
int16_t* const usb_ch = nullptr;
const uint8_t usb_now = 0, usb_ok = 0;
#endif
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
  const ModelDef& d = mdef(m);          // Plaits' HARMONICS (swarm, particle), PADS' PROG; 0 none (the FM's RATIO:
  if (d.kind == MK_PADS) return 1;      // kept out of its chords, at the dose; POLY's CHORD: its parallel ones would
  if (d.kind == MK_POLY) return 7;      // leave the scale; PROPHET's too; SQ-1's WAVE: a program, its Transwave made
  if (d.kind == MK_PROPHET) return 8;   // anew)
  if (d.kind == MK_CHOIR) return 2;     // (0.16.43: CHOIR's VOICES, Solo or a choir)
  if (d.kind == MK_TAURUS) return 1;    // (TAURUS' VOICE: a preset, a wave)
  if (d.kind == MK_VOCODER) return 1;   // (VOCODER's MOD: which track it listens to, kept; its PLAY too, below)
  if (d.kind == MK_STATIC) return 1;    // (0.16.28: STATIC's MODE, its other knobs another generator's)
  if (d.kind != MK_PLAITS) return 0;
  return d.sub == 16 || d.sub == 18 ? 1 : 0;
}
void make_dice() {                      // the effects' (a track's: at its trigs, new_trig)
  for (int j = 0; j < n_fx_dice; ++j) fx_off[j] = fx_span[j] * (2.0f * rand01() - 1.0f);
  dice_on = 1;
}

// TRAUMA (0.14: proj.trauma, OCT+ held on the panel, SELECT), after Audiotrauma, the Strasbourg label of electro,
// industrial and noise that pushed its records into the red: a dose of saturation over the sound. At 0 nothing (the
// sound as set: the knob slides in from there); then what adds harmonics, never a note (all of it stays in the
// scale), each part from its own point of the knob's travel:
//   from the start  a drive on every track after its filter (apply_trauma: the drums' curve, x - x^3 / 3 then flat,
//                   antialiased; its gain 1 to 8, its level given back, in from the sound as it is over the first
//                   quarter), and where the track has a filter, its DRIVE (+0.45 at the end)
//   from 25 %       each trig its own drive (20 to 100 % of the dose's at the end, 0.14.7: one nearly clean, the next
//                   all in; 70 to 100 % before), warm (biased: even harmonics) or, on up to half of them, hard (odd ones)
//   from 40 %       the filter's FM on a share of the trigs (a third, then all of them), either way, at most 10 % of
//                   its depth (the knob's old 10 %: Yoann's limit, more and the sound is lost)
//   from 50 %       Warps opening now and then (its MIX, TIMBRE, LEVEL 2), where it is patched, never turned on (its
//                   CPU: a fifth of a core): thrown again at each trig of the track it plays (0.14.7), a beat at a
//                   time when it plays its own oscillator; its CPU the same (Warps runs as patched, open or not)
//   from 60 %       the delay's DRIVE (its echoes pushed)
inline float ramp01(float x, float a, float b) { return x <= a ? 0.0f : x >= b ? 1.0f : (x - a) / (b - a); }
float trauma01() { return static_cast<float>(proj.trauma > 100 ? 100 : proj.trauma) * 0.01f; }
float tr_f[ENG_TRACKS] = {1.0f, 1.0f, 1.0f, 1.0f};   // this trig's share of the dose's drive (0.2..1)
uint8_t tr_hard[ENG_TRACKS];            // this trig's drive hard rather than warm
float tr_w[3];                          // Warps now: its opening (0: shut), TIMBRE (-1..1), LEVEL 2 (0..1)
// the filter's FM knob for a depth (mi_filter's curve, the other way): ln(1 + depth (e^4 - 1)) / 4
inline float fm_knob_for(float depth) { return fm_logf(1.0f + depth * 53.59815f) * 0.25f; }
void trauma_warps() {                   // Warps open or not, its colours: at each trig of its carrier, or each beat
  float w = ramp01(trauma01(), 0.5f, 1.0f);
  tr_w[0] = w > 0.0f && rand01() < 0.25f + 0.5f * w ? w * (0.5f + 0.5f * rand01()) : 0.0f;
  tr_w[1] = 2.0f * rand01() - 1.0f;
  tr_w[2] = rand01();
}
// Warps' carrier a track (WC_T1..4: its trigs throw Warps again), or -1 (off, or its own oscillator: each beat)
int warps_track() {
  int c = iround(proj.val[PG_WARPS(0) * 4]);
  return c >= WC_T1 && c <= WC_T4 ? c - WC_T1 : -1;
}
void trauma_trig(int t) {               // a track's trig: its drive, its filter's FM; Warps, if it plays this track
  float d = trauma01(), v = ramp01(d, 0.25f, 0.6f), w = ramp01(d, 0.4f, 1.0f);
  tr_f[t] = 1.0f - 0.8f * v * rand01();
  tr_hard[t] = rand01() < 0.5f * v;
  if (t == warps_track()) trauma_warps();
  if (proj.algo[PG_FILT(t, 0)] == MF_OFF) return;   // (the filter's: where there is one)
  if (w > 0.0f && rand01() < 0.33f + 0.67f * w)
    tdelta[t][TP_FM] += (rand01() < 0.5f ? -1.0f : 1.0f) * fm_knob_for(0.1f * w * (0.5f + 0.5f * rand01()));
}
void trauma_knobs();                    // (over the knobs, each block: after lane_param)

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
  if (proj.trauma) trauma_knobs();      // TRAUMA, over the knobs (and the dose), under the cables
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
// (0.16.42) the duck's source, the DUCK page's ALGORITHM (T1..T4: its trigs duck the others); the depth knobs the three
// others', in order (T1's source: T2, T3, T4 as before)
inline int duck_src() { return proj.algo[PG_DUCK] & 3; }
inline int duck_slot(int t) { return t < duck_src() ? t + 1 : t; }       // a track's depth's knob (1..3)
inline int duck_knob_track(int k) { return k - 1 < duck_src() ? k - 1 : k; }   // a knob's track
int16_t lane_param[ENG_LANES][ENG_NTP];  // eng_track_param's answers, made once (E is asked ~150 times a block)
void trauma_knobs() {                   // TRAUMA's knobs: the filters' DRIVE, Warps' now, the delay's DRIVE
  float d = trauma01();
  for (int t = 0; t < ENG_TRACKS; ++t)
    if (proj.algo[PG_FILT(t, 0)] != MF_OFF) fin[lane_param[t][TP_FDRIVE]] += 0.45f * d;
  if (tr_w[0] > 0.0f && iround(proj.val[PG_WARPS(0) * 4]) > WC_OFF) {   // (Warps: where it is patched)
    fin[PG_WARPS(0) * 4 + 3] += 0.4f * tr_w[0];                        // MIX
    fin[PG_WARPS(1) * 4 + 0] += 0.25f * tr_w[0] * tr_w[1];             // TIMBRE
    fin[PG_WARPS(1) * 4 + 2] += 0.3f * tr_w[0] * tr_w[2];              // LEVEL 2
  }
  float dl = ramp01(d, 0.6f, 1.0f);
  if (dl > 0.0f) fin[PG_DELAY(0) * 4 + 3] += 0.45f * dl;               // the delay's DRIVE
}
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

// (0.16.42) the duck's depths where its source puts them: each track's TP_DUCK on its knob (the source's on DECAY, not
// lockable), made again when the source changes
int duck_mapped = -1;
void duck_map() {
  if (duck_src() == duck_mapped) return;
  duck_mapped = duck_src();
  for (int t = 0; t < ENG_TRACKS; ++t) {
    lane_param[t][TP_DUCK] = static_cast<int16_t>(eng_track_param(t, TP_DUCK));
    if (eng_tp_lockable(t, TP_DUCK)) lane_lockable[t][TP_DUCK >> 5] |= 1u << (TP_DUCK & 31);
    else lane_lockable[t][TP_DUCK >> 5] &= ~(1u << (TP_DUCK & 31));
    tp_dice[t][TP_DUCK] = static_cast<uint8_t>(dice_span(lane_param[t][TP_DUCK]) * 20.0f + 0.5f);
  }
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

// PADS' chords (0.11): the track it is on, the progression's place, the chord (semitones over the scale's root), its
// scale (SCALE Prog: every pitch in it)
int pad_track = -1, pad_pos = -1, pad_n = 0, pad_raised = -1;
int pad_chord[6];
// (0.16.28) VOCODER's chords (PLAY on Manual or a progression): each track its progression's place, its chord voiced
// (its notes; 0: the trig's own, Poly's and Mono's)
int8_t vc_pos[4] = {-1, -1, -1, -1};
uint8_t vc_n[4];
float vc_ch[4][4];
uint16_t pad_mask = 0;                  // the chord's scale (over the root)
int quantize_mask(int n, uint32_t m, int root) {   // the nearest note of the scale (a tie: the lower)
  if (!m) return n;
  int rel = ((n - root) % 12 + 12) % 12;
  for (int d = 0; d <= 6; ++d) {
    if ((m >> ((rel - d + 12) % 12)) & 1u) return n - d;
    if ((m >> ((rel + d) % 12)) & 1u) return n + d;
  }
  return n;
}
uint16_t base_mask(int sc) {            // a scale's notes, not PADS' chord's (Off, Prog: none); FLIP's this bar's
  if (sc == kScaleFlip) return playing && tick >= 0 && (tick / 16) % 2 ? kScaleMasks[3] : kScaleMasks[2];
  return sc > 0 && sc < kScaleFlip && sc != kScaleProg ? kScaleMasks[sc] : 0u;
}
// (0.16.42) FOLLOW T1..T4: the track's chord leads. Each track's last chord (two notes or more: their pitch classes
// over ROOT) and the scale it makes: its notes, and the passing notes of the nearest major scale that holds them all
// (ROOT's own first, then a fifth away, then two...; none: the chord's notes alone), the avoid notes out (a half step
// over a chord note); PADS' its own (Prog's). The leading track is not quantized: its notes are the harmony
uint16_t lead_cm[ENG_TRACKS], lead_scale[ENG_TRACKS];
int8_t lead_root[ENG_TRACKS] = {-1, -1, -1, -1};
inline int lead_track() {
  const int sc = Pi(PG_GLOBAL, 1);
  return sc >= kScaleT1 && sc < kScales ? sc - kScaleT1 : -1;
}
void lead_chord(int t, const float* notes, int n) {
  if (n < 2) return;
  const int root = Pi(PG_GLOBAL, 2);
  uint16_t cm = 0;
  for (int i = 0; i < n; ++i) cm |= static_cast<uint16_t>(1u << (((iround(notes[i]) - root) % 12 + 12) % 12));
  if (cm == lead_cm[t] && root == lead_root[t]) return;
  int ch[12], cn = 0;
  for (int i = 0; i < 12; ++i)
    if ((cm >> i) & 1u) ch[cn++] = i;
  if (cn < 2) return;
  lead_cm[t] = cm;
  lead_root[t] = static_cast<int8_t>(root);
  uint16_t m = 0;
  for (int f = 0; f <= 6 && !m; ++f)    // (a fifth up, a fifth down, ...)
    for (int sg = 1; sg >= -1 && !m; sg -= 2) {
      const int k = ((sg * f * 7) % 12 + 12) % 12;
      const uint16_t mj = static_cast<uint16_t>(((0xAB5u << k) | (0xAB5u >> (12 - k))) & 0xFFFu);
      if ((mj & cm) == cm) m = mj;
    }
  lead_scale[t] = pads_chord_scale(ch, cn, m, -1);
}
uint32_t scale_mask(int sc) {           // SCALE's notes (Prog: the chord's, none without PADS)
  if (sc == kScaleProg) return pad_track >= 0 ? pad_mask : 0u;
  if (sc >= kScaleT1 && sc < kScales) return sc - kScaleT1 == pad_track ? pad_mask : lead_scale[sc - kScaleT1];
  return base_mask(sc);
}
void pads_now(float note);              // (its chord now: after track_kind, further down)
// the render's: a table of the notes -32..159, made again when the scale (its notes) or its root changes (the
// screen's, eng_quantize, works it out: the table is the render's alone)
const int kQLo = -32, kQN = 192;
int16_t qmap[kQN];
int q_root = -1;
uint32_t q_mask = 0xFFFFFFFFu;
int quantize(int n) {
  int root = Pi(PG_GLOBAL, 2);
  uint32_t m = scale_mask(Pi(PG_GLOBAL, 1));
  if (!m) return n;
  if (m != q_mask || root != q_root) {
    for (int i = 0; i < kQN; ++i) qmap[i] = static_cast<int16_t>(quantize_mask(i + kQLo, m, root));
    q_mask = m;
    q_root = root;
  }
  int i = n - kQLo;
  return i >= 0 && i < kQN ? qmap[i] : quantize_mask(n, m, root);
}

int scale_on() {                        // SCALE not Off: every pitch in it
  return scale_mask(Pi(PG_GLOBAL, 1)) != 0u;
}
inline int scale_on_t(int t) { return t != lead_track() && scale_on(); }   // (the leading track: free)
inline int quantize_t(int t, int n) { return t == lead_track() ? n : quantize(n); }

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
// (0.16.22) The rhythmic waves, their figures on the cycle cut in eight (synced, at 1 / 2 beats: sixteenths; at 1 /
// bar: eighths; on a track's trigs: from each): PUMP the sidechain's duck and its swell back; STAIRS four steps up;
// CLAVE the tresillo's three hits (3 + 3 + 2), each falling back; BOUNCE a ball's arcs, quicker and lower each time
// (0.6 the last's length: the cycle holds them all); RISES and FALLS ramps on the tresillo's 3 + 3 + 2, up or down;
// ZIGZAG up 3, down 1, up 2, down 2. CHAOS: Lorenz's attractor (sigma 10, rho 28, beta 8/3), its x, run at the
// rate (a turn around one of its two wings a cycle, about), neither periodic nor random
struct Lfo {
  float phase, out, a, b;               // its phase (0..1), what comes out, the random's last value and the next
  uint32_t rises, seed;                 // its track's trigs seen; its own random (the steps' stays theirs)
  float cx, cy, cz;                     // (CHAOS: where it is)
  float fe, f1, f2, det, ad;            // (FOLLOW: its level, its filter, TRIG's detector, its envelope)
  uint8_t armed, fire, rising;          // (TRIG: ready, a rise heard, the envelope rising)
  uint32_t frises;                      // (T1..T4: the track's trigs seen)
};
Lfo lfo[3];

// (0.16.23) FOLLOW: an LFO listening to a track (its last block, as the track played it: before its level, the mix's
// filter and duck), through its GAIN and FILTER; FOLLOW its level (its ATTACK and DECAY, under THRESH nothing, over it
// stretched back to 0..1), TRIG its rises over THRESH (a detector of 1 ms up and 50 ms down, ready again under half of
// it): the envelope's start (run_lfos). Out: 0..1
struct FolK {                           // (a follower's coefficients this block)
  float g, thr, c, ka, kb, kc, a, r, ta, tr;
};
__attribute__((noinline)) MI_COLD FolK fol_coefs(int i) {         // (for size: a block's)
  const int pg = PG_LFO(i), fp = PG_FOLLOW(i);
  const float fv = P(fp, 3), fc = fol_hz(fv), sr = kSr;
  FolK k;
  k.g = fm_exp2f(P(fp, 1) * 24.0f * 0.16609640f);
  k.thr = fm_exp2f(fol_db(P(fp, 2)) * 0.16609640f);
  k.c = 1.0f - fm_expf(-6.2831853f * (fc < 0.45f * sr ? fc : 0.45f * sr) / sr);
  // (the filter: what comes in, the first low pass, the second, mixed: off (1 0 0), LP (0 0 1), HP (1 -2 1))
  k.ka = fv <= -0.02f ? 0.0f : 1.0f, k.kb = fv >= 0.02f ? -2.0f : 0.0f;
  k.kc = fv <= -0.02f || fv >= 0.02f ? 1.0f : 0.0f;
  k.a = 1.0f - fm_expf(-1.0f / (fol_att(P(pg, 1)) * sr)), k.r = 1.0f - fm_expf(-1.0f / (fol_dec(P(pg, 3)) * sr));
  k.ta = 1.0f - fm_expf(-1.0f / (0.001f * sr)), k.tr = 1.0f - fm_expf(-1.0f / (0.05f * sr));
  return k;
}
__attribute__((noinline)) void follow_listen(const float* x, int i, uint32_t n) {   // (its loop at -O2)
  Lfo& o = lfo[i];
  const FolK k = fol_coefs(i);
  float f1 = o.f1, f2 = o.f2, e = o.fe, d = o.det;
  int armed = o.armed, fire = o.fire;
  for (uint32_t j = 0; j < n; ++j) {    // (FOLLOW's level and TRIG's detector both: no branch on the mode)
    const float y = k.g * x[j];
    f1 += k.c * (y - f1);
    f2 += k.c * (f1 - f2);
    const float l = fm_fabsf(k.ka * y + k.kb * f1 + k.kc * f2);
    e += (l > e ? k.a : k.r) * (l - e);
    d += (l > d ? k.ta : k.tr) * (l - d);
    const int over = d > k.thr, under = d < 0.5f * k.thr;
    fire |= armed & over;
    armed = (armed & !over) | under;
  }
  o.f1 = fm_fabsf(f1) < 1e-20f ? 0.0f : f1, o.f2 = fm_fabsf(f2) < 1e-20f ? 0.0f : f2;
  o.fe = e < 1e-20f ? 0.0f : e, o.det = d < 1e-20f ? 0.0f : d;
  o.armed = static_cast<uint8_t>(armed), o.fire = static_cast<uint8_t>(Pi(PG_LFO(i), 2) == LF_TRIG && fire);
}
__attribute__((noinline)) MI_COLD float follow_out(Lfo& o, int i, float block_hz) {   // FOLLOW's value this block (0..1)
  const int pg = PG_LFO(i), mode = Pi(pg, 2);
  if (mode == LF_FOLLOW) {
    const float thr = fm_exp2f(fol_db(P(PG_FOLLOW(i), 2)) * 0.16609640f);
    const float v = o.fe <= thr ? 0.0f : (o.fe - thr) / (1.0f - thr > 1e-6f ? 1.0f - thr : 1e-6f);
    return v > 1.0f ? 1.0f : v;
  }
  if (mode >= LF_T1 && mode < LF_T1 + ENG_TRACKS) {   // the track's trigs
    const uint32_t rr = vrises[mode - LF_T1];
    if (rr != o.frises) o.frises = rr, o.fire = 1;
  }
  if (o.fire) o.fire = 0, o.rising = 1;   // (from where it is: no click)
  if (o.rising) {
    o.ad += 1.0f / (fol_att(P(pg, 1)) * block_hz);
    if (o.ad >= 1.0f) o.ad = 1.0f, o.rising = 0;
  } else {
    o.ad *= fm_expf(-6.9077553f / (fol_dec(P(pg, 3)) * block_hz));   // (DECAY: its fall to -60 dB)
    if (o.ad < 1e-5f) o.ad = 0.0f;
  }
  return o.ad;
}

float lfo_hit(float x) {                // a hit x units ago (0..1.5 its fall), then the floor
  if (x >= 1.5f) return -1.0f;
  const float d = 1.0f - x * (1.0f / 1.5f);
  return -1.0f + 2.0f * d * d;
}
__attribute__((noinline)) MI_COLD float lfo_figure(int wave, float ph) {   // the rhythmic waves at their phase, -1..1
  const float u = 8.0f * ph;            // (the eighths of the cycle)
  const float s = u < 3.0f ? 0.0f : u < 6.0f ? 3.0f : 6.0f, len = u < 6.0f ? 3.0f : 2.0f;   // (3 + 3 + 2)
  switch (wave) {
    case LW_PUMP: {                     // down at once, held a moment, swelling back over 70 % of the cycle
      const float x = ph < 0.7f ? ph * (1.0f / 0.7f) : 1.0f;
      return -1.0f + 2.0f * x * x * (3.0f - 2.0f * x);
    }
    case LW_STAIRS: return -1.0f + (2.0f / 3.0f) * fm_floorf(ph * 4.0f > 3.0f ? 3.0f : ph * 4.0f);
    case LW_CLAVE: return lfo_hit(u - s);
    case LW_BOUNCE: {                   // the arcs: 0.4 of the cycle, then 0.6 of each before, their heights squared
      float t = 0.0f, len_k = 0.4f, h = 1.0f;
      for (int k = 0; k < 12 && ph >= t + len_k; ++k) t += len_k, len_k *= 0.6f, h *= 0.36f;
      const float x = (ph - t) / len_k;
      return -1.0f + 8.0f * h * x * (1.0f - x);
    }
    case LW_RISES: return -1.0f + 2.0f * (u - s) / len;
    case LW_FALLS: return 1.0f - 2.0f * (u - s) / len;
    default: {                          // ZIGZAG: through (0, -1) (3, 1) (4, -0.2) (6, 0.6) (8, -1)
      static const float kX[5] = {0.0f, 3.0f, 4.0f, 6.0f, 8.0f}, kY[5] = {-1.0f, 1.0f, -0.2f, 0.6f, -1.0f};
      int i = u < 3.0f ? 0 : u < 4.0f ? 1 : u < 6.0f ? 2 : 3;
      return kY[i] + (kY[i + 1] - kY[i]) * (u - kX[i]) / (kX[i + 1] - kX[i]);
    }
  }
}
__attribute__((noinline)) MI_COLD float lfo_chaos(Lfo& o, float inc) {    // Lorenz's attractor run inc of a cycle on (~0.8 of its time a cycle)
  if (o.cx == 0.0f && o.cy == 0.0f && o.cz == 0.0f) o.cx = 1.0f, o.cy = 1.0f + 0.5f * (o.seed & 3u), o.cz = 20.0f;
  float dt = (inc < 0.0f ? 0.0f : inc > 0.25f ? 0.25f : inc) * 0.8f;
  int n = 1 + static_cast<int>(dt * 125.0f);   // (steps of 0.008 at most: Euler holds there)
  dt /= static_cast<float>(n);
  float x = o.cx, y = o.cy, z = o.cz;
  for (int i = 0; i < n; ++i) {
    const float dx = 10.0f * (y - x), dy = x * (28.0f - z) - y, dz = x * y - (8.0f / 3.0f) * z;
    x += dx * dt, y += dy * dt, z += dz * dt;
  }
  if (!(fm_fabsf(x) < 100.0f && fm_fabsf(y) < 100.0f && z > -10.0f && z < 100.0f)) x = 1.0f, y = 1.0f, z = 20.0f;
  o.cx = x, o.cy = y, o.cz = z;
  const float v = x * (1.0f / 19.0f);
  return v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
}

float lfo_rand(Lfo& o) {
  o.seed = o.seed * 1664525u + 1013904223u;
  return static_cast<float>(o.seed >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

__attribute__((noinline)) MI_COLD void run_lfos(uint32_t n) {   // (a block's: for size, out of the render)
  float block_hz = kSr / static_cast<float>(n), beat_hz = bpm_of(P(PG_GLOBAL, 0)) / 60.0f;
  for (int i = 0; i < 3; ++i) {
    Lfo& o = lfo[i];
    int wave = Pi(PG_LFO(i), 0), mode = Pi(PG_LFO(i), 2), cycle = 0;
    float rate = P(PG_LFO(i), 1), inc;        // (inc: a block's share of a cycle)
    if (!o.seed) o.seed = 0x9E3779B9u * static_cast<uint32_t>(i + 1);
    if (mode == LM_SYNC) {
      int r = static_cast<int>(rate * 8.999f);
      float ratio = kLfoRatios[r < 0 ? 0 : r > 8 ? 8 : r];
      inc = beat_hz * ratio / block_hz;
      if (playing) {                    // the phase from the bar: the beats since PLAY (the sixteenth, swung)
        float beats = (static_cast<float>(tick) + step_clk / six_len(step_len_now())) * 0.25f * ratio;
        float ph = beats - fm_floorf(beats);
        cycle = ph < o.phase;           // (wrapped)
        o.phase = ph;
      } else {
        o.phase += inc;
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
      inc = lfo_hz(rate) / block_hz;
      o.phase += inc;
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
      case LW_SMOOTH: v = o.a + (o.b - o.a) * (0.5f - 0.5f * fm_cosf(3.14159265f * ph)); break;
      case LW_CHAOS: v = lfo_chaos(o, inc); break;
      case LW_FOLLOW: v = follow_out(o, i, block_hz); break;
      default: v = lfo_figure(wave, ph); break;
    }
    float slew = wave == LW_FOLLOW ? 0.0f : P(PG_LFO(i), 3);   // (FOLLOW: SLEW's place its DECAY)
    if (slew < 0.005f) o.out = v;
    else o.out += (v - o.out) * (1.0f - fm_expf(-1.0f / (lfo_tau(slew) * block_hz)));
    src[SRC_L1 + i] = o.out;
  }
}

// a trig: its locks and randoms over the knobs, whether it plays (its probability); a track's ratchets. The locks are
// found in one pass over the slots (not one a parameter: a sixteenth's trigs on seven lanes took 50,000 compares)
// (0.16.8, 0.16.9: Yoann's curves) the random dose on the ratchets, by its travel (not its curve), kept rare to stay
// musical: a trig without some gets some from 50 % (2..4 hits; past 75 %, up to 6, now and then over 2..4 steps), its
// chance rising fast then levelling: 19 % at 60 %, 34 % at 70 %, 43 % at 80 %, 50 % at 100 % (0.5 (1 - (1 - x)^2.2)
// over 50..100 %); a trig with some, left alone below 30 %, then now and then changed (a hit or two more or fewer,
// past 75 % a step of span either way) or gone (a single hit), half and half: 0..5 % over 30..50 %, then the same
// curve up to 50 %. Past 50 % their CURVE thrown, past 75 % now and then the roll climbing (or falling) through its
// hits, if the step has no CLIMB of its own. Each trig anew, as the rest of the dose
int dice_climb[ENG_TRACKS];             // (that roll's CLIMB: a semitone or a scale's note or up to three a hit, 0 none)
float dice_rise(float d) {              // 0..1 over 50..100 %: fast, then levelling
  if (d <= 0.5f) return 0.0f;
  float y = 1.0f - (d - 0.5f) * 2.0f;
  return y <= 1e-4f ? 1.0f : 1.0f - fm_exp2f(2.2f * fm_log2f(y));
}
void dice_ratch(int t, float d) {
  dice_climb[t] = 0;
  if (d < 0.3f) return;
  int param = lane_param[t][TP_RATCHET];
  float base = clampf(fin[param] + tdelta[t][TP_RATCHET], 1.0f, static_cast<float>(ENG_RATCH_MAX));
  int sp, h = eng_ratch_hits(iround(base), &sp), h0 = h, sp0 = sp;
  if (h <= 1) {
    if (rand01() < 0.5f * dice_rise(d)) {
      h = 2 + static_cast<int>(rand_u32() % (d > 0.75f ? 5u : 3u));
      if (d > 0.75f && rand01() < (d - 0.75f) * 1.6f) sp = 2 + static_cast<int>(rand_u32() % 3u);
    }
  } else if (rand01() < (d < 0.5f ? 0.05f * (d - 0.3f) * 5.0f : 0.05f + 0.45f * dice_rise(d))) {
    if (rand01() < 0.5f) {
      h = sp = 1;
    } else {
      int dh = 1 + static_cast<int>(rand_u32() % 2u);
      h += (rand_u32() & 1u) ? dh : -dh;
      h = h < 2 ? 2 : h > 16 ? 16 : h;
      if (d > 0.75f && rand01() < 0.5f) sp += (rand_u32() & 1u) ? 1 : -1;
      sp = sp < 1 ? 1 : sp > 16 ? 16 : sp;
    }
  }
  if (h != h0 || sp != sp0) tdelta[t][TP_RATCHET] = static_cast<float>(eng_ratch_join(h, h <= 1 ? 1 : sp)) - fin[param];
  if (h <= 1) return;
  if (d > 0.5f && rand01() < (d - 0.5f) * 1.2f) tdelta[t][TP_CURVE] += (2.0f * rand01() - 1.0f) * (d - 0.5f) * 2.0f;
  if (d > 0.75f && rand01() < (d - 0.75f) * 2.0f)
    dice_climb[t] = ((rand_u32() & 1u) ? 1 : -1) * (1 + static_cast<int>(rand_u32() % 3u));
}

// (0.16.15) a trig's length over the steps after it: a lock of its own (tp kTpTie, beyond the lane's parameters: no
// knob, no random), its value the steps it holds over (1..63); the step held and another one pressed further
const int kTpTie = ENG_NTP;
uint8_t trig_tie[ENG_LANES];            // the trig playing's
// (0.16.16) a step's chord: its NOTE the lowest, up to three notes over it, each a lock of its own (tp kTpNote2 ..
// kTpNote2 + 2: the semitones over NOTE, 1..96); the machines that play chords (POLY in Poly, REV3 with CHORD Off,
// VOCODER in Poly, CHOIR past Solo) play them all, the others NOTE alone
const int kTpNote2 = ENG_NTP + 1;
uint8_t trig_iv[ENG_TRACKS][3], trig_cn[ENG_TRACKS];   // the trig playing's
// (0.16.40) a step's model: a lock of its own (tp kTpModel, its value the model's number), another model of the
// track's family (Plaits' six engines, the six drums, the two FM drums, the SDS' four: one voice that changes its
// engine at a trig, its knobs as they are), played from that trig to the next
const int kTpModel = ENG_NTP + 4;
uint8_t trig_model[ENG_TRACKS];         // the trig playing's: its model + 1 (0: the track's)
void new_trig(int lane, int step, int locks) {   // (on the RAM tables: lane_param, lane_lockable, the bounds)
  int16_t at[ENG_NTP];
  for (int tp = 0; tp < ENG_NTP; ++tp) at[tp] = -1;
  if (lock_gen_seen != lock_gen) make_lock_steps();
  trig_tie[lane] = 0;
  uint8_t iv[3] = {0, 0, 0}, tm = 0;
  if (locks && step >= 0 && step < ENG_STEPS && ((lock_steps[lane][step / 32] >> (step % 32)) & 1u))
    for (int i = 0; i < ENG_PLOCKS; ++i)
      if (proj.lock[i].lane == lane && proj.lock[i].step == step) {
        const int tp = proj.lock[i].tp;
        if (tp < ENG_NTP) at[tp] = static_cast<int16_t>(i);
        else if (tp == kTpTie) trig_tie[lane] = proj.lock[i].v;
        else if (tp >= kTpNote2 && tp < kTpNote2 + 3) iv[tp - kTpNote2] = proj.lock[i].v;
        else if (tp == kTpModel) tm = static_cast<uint8_t>(proj.lock[i].v + 1);
      }
  if (lane < ENG_TRACKS) trig_model[lane] = tm;
  if (lane < ENG_TRACKS) {              // (its chord's notes over NOTE, in order)
    int c = 0;
    for (int j = 0; j < 3; ++j)
      if (iv[j]) trig_iv[lane][c++] = iv[j];
    trig_cn[lane] = static_cast<uint8_t>(c);
  }
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
      if (lane < ENG_TRACKS && tp == TP_RATCHET) {   // only more hits (7 at half the knob's travel, 15 at its end);
        int sp, h = eng_ratch_hits(iround(v), &sp);   // over half, a longer span too (3 steps more at most)
        h += iround(a * x * (a <= 0.5f ? 7.0f : 7.0f + 16.0f * (a - 0.5f)));
        if (a > 0.5f) {
          float y = x * 7.13f;
          sp += iround((a - 0.5f) * 6.0f * (y - fm_floorf(y)));
        }
        v = static_cast<float>(eng_ratch_join(h, sp));
      }
      else if (lane < ENG_TRACKS && tp == TP_NOTE) v += a * (x * 2.0f - 1.0f) * 12.0f;   // an octave either way
      else v += a * (x * 2.0f - 1.0f) * (c != 0.0f ? c : p_hi[param] - p_lo[param]);
    }
    tdelta[lane][tp] = v - base;
  }
  if (lane < ENG_TRACKS) {              // the random dose: every sound parameter of the track thrown anew, this trig's
    dice_type[lane] = -1;
    dice_climb[lane] = 0;
    if (proj.dice) {
      float a = dice_curve(), d = static_cast<float>(proj.dice > 100 ? 100 : proj.dice) * 0.01f;
      int iv = dice_interval(proj.algo[PG_TRACK(lane, 0)]), vk = mdef(proj.algo[PG_TRACK(lane, 0)]).kind == MK_VOCODER;
      for (int tp = 0; tp < ENG_NTP; ++tp) {
        if (!tp_dice[lane][tp] || tp == iv || (vk && !tp)) continue;   // (VOCODER: FREQ's place its PLAY)
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
      dice_ratch(lane, d);              // (0.16.8) its ratchets too
      if (rand01() < 0.15f * a)         // now and then, lightly, into the delay or the reverb
        tdelta[lane][rand01() < 0.5f ? TP_DSEND : TP_SEND] += 0.2f + 0.15f * rand01();
    }
    if (proj.trauma) trauma_trig(lane);  // TRAUMA: this trig's drive, its filter's FM
  }
  if (lane >= ENG_TRACKS) {             // an effect: open, by its probability
    float prob = E(lane, kLaneKnobs[lane] + 1);
    if (prob >= 0.999f || rand01() < prob) fx_hit[lane - ENG_TRACKS] = 1;
    return;
  }
  float prob = E(lane, TP_PROB);
  rat_n[lane] = eng_ratch_hits(static_cast<int>(clampf(static_cast<float>(iround(fin[eng_track_param(lane, TP_RATCHET)] +
                                                                                tdelta[lane][TP_RATCHET])),
                                                        1.0f, static_cast<float>(ENG_RATCH_MAX))),
                               &rat_span[lane]);
  rat_on[lane] = prob >= 0.999f || rand01() < prob;
  if (rat_on[lane] && locks && proj.dice) {   // (0.16.10) the dose leaving a step's trig out now and then, from 50 %
    float d = static_cast<float>(proj.dice > 100 ? 100 : proj.dice) * 0.01f;   // (its ratchets' curve, 20 % at most,
    float q = 0.2f * dice_rise(d);            // Yoann's: 8 % at 60 %, 14 % at 70 %, 17 % at 80 %, 20 % at 100 %; on a
    if (step % 16 == 0) q = 0.0f;             // beat half as often, the bar's first step never: the groove kept)
    else if (step % 4 == 0) q *= 0.5f;
    if (q > 0.0f && rand01() < q) rat_on[lane] = 0;
  }
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

// PADS held (its DECAY at Hold, 0.15.2): a step's chord held to the track's next trig, whatever GATE, as a pad is
// played (0.11 to 0.15.1: GATE's share of the step, then let go: a sixteenth of a chord). The next trig takes it over
// (a gate still high: low for a frame); a trig PROB leaves out, the chord goes on
int held_pad(int t) { return mdef(proj.algo[PG_TRACK(t, 0)]).kind == MK_PADS && E(t, TP_DECAY) > 0.98f; }
int steps_to_next(int t, int step) {    // the steps to the lane's next trig (its whole length: none other)
  int len = Pi(PG_STEP(t), 3);
  if (len < 1) len = 1;
  if (proj.dir[t] != DIR_FWD) return 1;   // (another direction: its next step, whatever it is)
  for (int d = 1; d < len; ++d)
    if (eng_trig(t, (step + d) % len)) return d;
  return len;
}

// (0.16) a lane's next step, by its DIRECTION (lane_cnt: its steps since PLAY, 0 its first)
int lane_next(int l) {
  int len = Pi(PG_STEP(l), 3);
  if (len < 1) len = 1;
  int32_t c = lane_cnt[l];
  int p;
  switch (proj.dir[l]) {
    case DIR_BACK: return len - 1 - static_cast<int>(c % len);
    case DIR_PINGPONG: {
      if (len < 2) return 0;
      int per = 2 * len - 2, m = static_cast<int>(c % per);
      return m < len ? m : per - m;
    }
    case DIR_RANDOM: return c == 0 ? 0 : static_cast<int>(rand_u32() % static_cast<uint32_t>(len));
    case DIR_DRUNK: p = c == 0 ? 0 : cur_step[l] + ((rand_u32() >> 9) & 1u ? 1 : -1); break;
    case DIR_TWO_ONE: p = c == 0 ? 0 : cur_step[l] + (c % 3 == 0 ? -1 : 1); break;
    default: return static_cast<int>(c % len);
  }
  return (p % len + len) % len;
}

// (0.16) a trig's ratchets laid out: RATCH's hits over SPAN (0.16.4: 1 to 16 of the lane's steps), CURVE drawing each
// gap a share of the last (to the right: 2^(-0.6 CURVE), closer and closer, a ball bouncing; to the left wider and
// wider)
void ratchet_layout(int t) {
  int n = rat_n[t];
  float span = lane_sl[t] * static_cast<float>(rat_span[t]), c = clampf(E(t, TP_CURVE), -1.0f, 1.0f);
  float r = fm_exp2f(-0.6f * c), d = 1.0f, sum = 0.0f;   // (each gap two thirds of the last at most, or 1.5 times)
  if (n > 16) n = 16;
  for (int i = 0; i < n; ++i) sum += d, d *= r;   // (the gaps, the first 1)
  float g = span / sum, at = 0.0f;
  d = 1.0f;
  for (int i = 0; i < n; ++i) {
    rat_t[t][i] = at;
    at += g * d;
    d *= r;
  }
  rat_t[t][n] = span;
  rat_i[t] = -1;
  rat_lvl[t] = tdelta[t][TP_LEVEL];
}

// (0.16.13) Endless' ARP: KEYS walks the keys held (an arpeggiator: by pitch or as played, over OCTAVES; LATCH keeps
// them once let go, the next key after all let go anew), CHORD walks PADS' chord as it goes; both with Endless' DIR,
// STRIDE, REST, HOLES, LEAP, RATE and GATE, played with the sequencer or (a key held, LATCH's notes) without it
uint8_t arp_held[ENG_TRACKS][8];        // (KEYS: the keys, as pressed; written by the main loop, its count last)
volatile int arp_n[ENG_TRACKS], arp_down[ENG_TRACKS];
uint8_t arp_list[ENG_TRACKS][32];       // (what it walks, built at each of its steps)
int arp_len[ENG_TRACKS];
int arp_mode(int t) { return eng_arp_opt(t, AO_MODE); }
int arp_has(int t) {                    // (something to walk)
  int m = arp_mode(t);
  return m == AM_KEYS ? arp_n[t] > 0 : m == AM_CHORD ? pad_track >= 0 && pad_n > 0 : 0;
}
int arp_build(int t, uint8_t* out, int max) {
  int base[8], n = 0, m = arp_mode(t);
  if (m == AM_KEYS) {
    int c = arp_n[t];
    for (int i = 0; i < c && i < 8; ++i) base[n++] = arp_held[t][i];
    if (!eng_arp_opt(t, AO_ORDER))      // (by pitch)
      for (int i = 1; i < n; ++i)
        for (int j = i; j > 0 && base[j] < base[j - 1]; --j) { int x = base[j]; base[j] = base[j - 1]; base[j - 1] = x; }
  } else if (m == AM_CHORD && pad_track >= 0) {
    int root = Pi(PG_GLOBAL, 2);
    for (int i = 0; i < pad_n && n < 8; ++i) base[n++] = 48 + root + pad_chord[i];
    for (int i = 1; i < n; ++i)
      for (int j = i; j > 0 && base[j] < base[j - 1]; --j) { int x = base[j]; base[j] = base[j - 1]; base[j - 1] = x; }
  }
  int k = 0, oct = eng_arp_opt(t, AO_OCT);
  for (int o = 0; o < oct; ++o)
    for (int i = 0; i < n && k < max; ++i) {
      int v = base[i] + 12 * o;
      if (v >= 0 && v <= 127) out[k++] = static_cast<uint8_t>(v);
    }
  return k;
}

// (0.16.7) CLIMB: a step's trig a little higher (or lower) each time it plays, CLIMB semitones (SCALE on: its notes)
// a time, back to its note after TIMES of them (RAMP; BOUNCE: up, then back down; RANDOM: any of them); ON: each play
// of the trig (its ratchets on its note), each hit of its ratchets (from the trig's note), or both (each hit on from
// the last, across its plays). Each step its own count, from PLAY
int climb_by(int n, int d) {            // n moved d of the scale's notes (SCALE off: d semitones), within 0..127
  uint32_t m = scale_mask(Pi(PG_GLOBAL, 1));
  if (!m) {
    n += d;
  } else {
    int root = Pi(PG_GLOBAL, 2), s = d > 0 ? 1 : -1;
    for (int k = 0, g = 0; k != d && g < 400; ++g) {
      n += s;
      if ((m >> (((n - root) % 12 + 12) % 12)) & 1u) k += s;
    }
  }
  return n < 0 ? 0 : n > 127 ? 127 : n;
}
void climb_play(int t, int hit) {       // a trig climbing (hit 0) or one of its ratchets' hits: its note
  int st = climb_t[t];
  if (st < 0) return;
  int dc = climb_dice[t];               // (the dose's roll: each hit on, over its hits, once)
  int on = dc ? CLON_RATCH : eng_climb_opt(t, CL_ON), sh = dc ? CLSH_RAMP : eng_climb_opt(t, CL_SHAPE), c;
  int n = dc ? (rat_n[t] < 2 ? 2 : rat_n[t] > 16 ? 16 : rat_n[t]) : eng_climb_opt(t, CL_TIMES);
  int per = sh == CLSH_BOUNCE ? 2 * n - 2 : n;
  if (on == CLON_RATCH) {
    c = hit;
  } else {
    if (hit > 0 && on == CLON_TRIG) return;
    c = climb_cnt[t][st] % per;
    climb_cnt[t][st] = static_cast<uint8_t>((c + 1) % per);
  }
  int at = sh == CLSH_RANDOM ? static_cast<int>(rand_u32() % static_cast<uint32_t>(n))
         : sh == CLSH_BOUNCE ? (c % per < n ? c % per : per - c % per) : c % n;
  int off = climb_amt[t] * at;
  climb_off[t][st] = static_cast<int8_t>(off < -127 ? -127 : off > 127 ? 127 : off);
  tnote[t] = static_cast<float>(climb_by(climb_base[t], off));
}
void climb_fresh() {                    // (PLAY, another pattern: each step from its note)
  for (int t = 0; t < ENG_TRACKS; ++t) {
    climb_t[t] = -1;
    for (int i = 0; i < ENG_STEPS; ++i) climb_cnt[t][i] = 0, climb_off[t][i] = 0;
  }
}

// the trigs of one block, frame by frame: fg (the gates) and fnote (the notes) for the voices; the effects' trigs
void sequence(uint32_t n) {
  float fl = static_cast<float>(n / kFrames);
  float sl = step_len_now();
  if (restart) {
    restart = 0;
    step_clk = 1e9f;                    // the first sixteenth at once (counting in: so many before it)
    tick = -1 - count_in;
    count_in = 0;
    for (int t = 0; t < ENG_TRACKS; ++t) rec_skip[t] = -1;
    for (int l = 0; l < ENG_LANES; ++l) lane_cnt[l] = lane_grid[l] = -1, lane_spd[l] = static_cast<int8_t>(lane_speed(l));
    climb_fresh();
    pad_pos = -1;                       // (PADS: its first trig its first chord; VOCODER's too)
    vc_pos[0] = vc_pos[1] = vc_pos[2] = vc_pos[3] = -1;
    for (int t = 0; t < ENG_TRACKS; ++t) {
      e_clk[t] = 1e9f;
      e_pos[t] = -1;
      e_dir[t] = 1;
      e_cnt[t] = 0;
    }
  }
  pad_track = -1;                       // PADS: its track (the first holding it), its chord as the knobs have it now
  for (int t = 0; t < ENG_TRACKS && pad_track < 0; ++t)
    if (mdef(proj.algo[PG_TRACK(t, 0)]).kind == MK_PADS) pad_track = t;
  if (pad_track >= 0) pads_now(vnote[pad_track]);
  int endless[ENG_TRACKS];
  float el[ENG_TRACKS];
  for (int t = 0; t < ENG_TRACKS; ++t) {
    endless[t] = (arp_mode(t) == AM_LOOP ? proj.endless_len[t] > 0 && proj.endless_len[t] <= ENG_ENDLESS : arp_has(t)) &&
                 !e_rec[t];
    int r = Pi(PG_ARP(t), 0);
    el[t] = sl * kArpMult[r < 0 ? 0 : r > 5 ? 5 : r];
    gate_seen[t] = 0;
  }
  for (int l = 0; l < ENG_LANES; ++l) lane_sl[l] = sl / kSpeedMul[lane_speed(l)];
  for (int t = 0; t < ENG_TRACKS; ++t) gl_at[t] = -1;
  gl_six = sl;
  if (!playing) glitch_stop_all();
  int kbp_t[ENG_TRACKS];
  for (int t = 0; t < ENG_TRACKS; ++t) kbp_t[t] = kb_poly(t);
  for (uint32_t k = 0; k < kFrames; ++k) {
    int fresh[ENG_TRACKS] = {0, 0, 0, 0};   // a step's trig in this frame
    int fhit[ENG_TRACKS] = {0, 0, 0, 0}, fdn[ENG_TRACKS] = {0, 0, 0, 0};   // the dose's fills: a hit, its note
    if (playing && (step_clk += fl) >= six_len(sl)) {   // a sixteenth: each lane's step, its trig
      step_clk -= six_len(sl);
      ++tick;
      if (step_clk >= six_len(sl)) step_clk = 0.0f;
      if (click_on && !(tick & 3)) clk_at = static_cast<int32_t>(k * static_cast<uint32_t>(fl)), clk_hi = !(tick & 15);
      if (proj.dice && tick % 4 == 0) make_dice();   // (the dose: the effects thrown anew at each beat)
      if (proj.trauma && tick % 4 == 0 && warps_track() < 0) trauma_warps();   // (TRAUMA: Warps on its oscillator,
                                                                              // a beat at a time)
      for (int t = 0; t < ENG_TRACKS; ++t) gl_at[t] = static_cast<int>(k * static_cast<uint32_t>(fl));   // (the glitch)
      if (fill_amount_q(proj.dice) && tick % 16 >= 8) {   // (the bar's second half: where fills are)
        int stepping[ENG_TRACKS];
        for (int t = 0; t < ENG_TRACKS; ++t) stepping[t] = !endless[t] && !e_rec[t] && eng_lane_has_trigs(t);
        fill_plan(tick / 16, stepping);
        for (int t = 0; t < ENG_TRACKS; ++t) fhit[t] = stepping[t] ? fill_hit(t, &fdn[t]) : 0;
      }
    }
    if (playing && tick >= 0) {         // (0.16) each lane's step at its SPEED (x1: the sixteenth's, as ever), in its
                                        // DIRECTION; a fill's trig at the sixteenth
      float at = static_cast<float>(tick) + step_clk / six_len(sl);
      seq_at = at;
      for (int l = 0; l < ENG_LANES; ++l) {
        int sp = lane_speed(l), adv;
        int32_t g = sp == 4 ? tick : static_cast<int32_t>(at * kSpeedMul[sp]);
        if (sp != lane_spd[l]) {        // another SPEED: on from its step, its next at the new grid's next point
          lane_spd[l] = static_cast<int8_t>(sp);
          if (lane_cnt[l] >= 0) lane_grid[l] = g;
        }
        adv = g > lane_grid[l];
        if (adv) {
          lane_grid[l] = g;
          ++lane_cnt[l];
          cur_step[l] = lane_next(l);
        }
        if (adv && l < ENG_TRACKS && rec_skip[l] >= 0) {   // (0.16.31) the step the keys wrote just ahead: heard
          int sk = rec_skip[l];
          rec_skip[l] = -1;
          if (sk == cur_step[l]) continue;
        }
        if (!(adv && eng_trig(l, cur_step[l])) && !(l < ENG_TRACKS && fhit[l])) continue;
        if (l >= ENG_TRACKS) new_trig(l, cur_step[l], 1);
        else if (!endless[l] && !e_rec[l]) fresh[l] = 1;
      }
    }
    for (int oi = 0; oi < ENG_TRACKS; ++oi) {   // (PADS' track first: its trig moves the others' scale)
      int t = pad_track < 0 ? oi : oi == 0 ? pad_track : oi <= pad_track ? oi - 1 : oi;
      int raw, g, rise = 0;
      if (e_free_go[t]) {               // (0.16.11) a key held, stopped: Endless from its first note, at once
        e_free_go[t] = 0;
        e_clk[t] = 1e9f;
        e_pos[t] = -1;
        e_dir[t] = 1;
        e_cnt[t] = 0;
      }
      int e_run = playing || (e_free[t] && endless[t]), am = arp_mode(t);
      if (am != AM_LOOP && endless[t] && !arp_len[t]) {   // (ARP: something to walk again: from its first, at once)
        e_clk[t] = 1e9f;
        e_pos[t] = -1;
        e_dir[t] = 1;
        e_cnt[t] = 0;
      }
      if (am != AM_LOOP && !endless[t]) arp_len[t] = 0;
      if (endless[t] && e_run && (e_clk[t] += fl) >= el[t]) {   // Endless: its next note (or rest)
        const uint8_t* src = proj.endless[t];
        if (am != AM_LOOP) {            // (ARP: the keys or the chord now, its place kept within them)
          arp_len[t] = arp_build(t, arp_list[t], 32);
          src = arp_list[t];
          if (e_pos[t] >= arp_len[t]) e_pos[t] = arp_len[t] - 1;
        }
        int len = am != AM_LOOP ? arp_len[t] : proj.endless_len[t], d = Pi(PG_ARP(t), 2);
        int sd = eng_endless_opt(t, EO_STRIDE);
        if (len < 1) len = 1;
        int rn = eng_endless_opt(t, EO_REST), pause = rn >= 2 && e_cnt[t] % rn == rn - 1;
        e_clk[t] = e_clk[t] >= 2.0f * el[t] ? 0.0f : e_clk[t] - el[t];
        ++e_cnt[t];
        if (pause) {                    // (0.16.2) REST: a rest every so many steps, the notes waiting for it
        } else if (d == 1) {            // STRIDE: so many notes on at a time (1: each)
          e_pos[t] = e_pos[t] < 0 ? len - 1 : ((e_pos[t] - sd) % len + len) % len;
        } else if (d == 2) {
          if (sd == 1) {
            e_pos[t] += e_dir[t];
            if (e_pos[t] >= len) e_dir[t] = -1, e_pos[t] = len > 1 ? len - 2 : 0;
            if (e_pos[t] < 0) e_dir[t] = 1, e_pos[t] = len > 1 ? 1 : 0;
          } else {                      // (there and back by STRIDE: its ends a mirror)
            int q = e_pos[t] < 0 ? 0 : e_pos[t] + e_dir[t] * sd;
            for (int g = 0; g < 4 && (q >= len || q < 0); ++g) {
              if (q >= len) q = 2 * (len - 1) - q, e_dir[t] = -1;
              if (q < 0) q = -q, e_dir[t] = 1;
            }
            e_pos[t] = len > 1 ? (q < 0 ? 0 : q >= len ? len - 1 : q) : 0;
          }
        } else if (d == 3) {
          e_pos[t] = static_cast<int>(rand_u32() % static_cast<uint32_t>(len));
        } else if (d == 4) {            // (0.16.13) +2-1: two on, one back (by STRIDE)
          int c = e_cnt[t] - 1, q = c / 2 * sd + (c % 2 ? 2 * sd : 0);
          e_pos[t] = (q % len + len) % len;
        } else if (d == 5) {            // (0.16.13) CONVERGE: the first, the last, the second, the one before...
          int c = (e_cnt[t] - 1) % len;
          e_pos[t] = c % 2 ? len - 1 - c / 2 : c / 2;
        } else {
          e_pos[t] = e_pos[t] < 0 ? 0 : (e_pos[t] + sd) % len;
        }
        int nt = pause ? ENDLESS_REST : src[e_pos[t]];
        int holes = eng_endless_opt(t, EO_HOLES), leap = eng_endless_opt(t, EO_LEAP);
        e_state[t] = pause ? ES_PAUSE : nt == ENDLESS_REST ? ES_REST : ES_NOTE;
        if (nt != ENDLESS_REST && holes && rand_u32() % 100u < static_cast<uint32_t>(holes))   // HOLES
          nt = ENDLESS_REST, e_state[t] = ES_HOLE;
        if (nt != ENDLESS_REST && leap && rand_u32() % 100u < static_cast<uint32_t>(leap))     // LEAP: an octave up
          nt += 12, e_state[t] = ES_LEAP;
        if (nt != ENDLESS_REST) {
          int rests = 0;                // (PADS held: to its next note, over the rests after it, forward)
          if (held_pad(t) && d == 0 && am == AM_LOOP)
            while (rests < len - 1 && proj.endless[t][(e_pos[t] + 1 + rests) % len] == ENDLESS_REST) ++rests;
          raw_left[t] = fm_maxf(fl * 2.0f, held_pad(t) ? el[t] * (static_cast<float>(rests) + 1.5f)
                                                      : el[t] * clampf(P(PG_ARP(t), 1), 0.05f, 1.0f) * 0.98f);
          e_note[t] = nt;
          trig_cn[t] = 0;               // (Endless: a note alone)
          rise = 1;
        }
      }
      if (fresh[t]) {                   // a step's trig: its length is its own (a lock, a random)
        new_trig(t, cur_step[t], 1);
        if (fhit[t]) {                  // a fill's: it plays, its ratchets and its note the fill's
          rat_on[t] = 1;
          if (rat_n[t] < fhit[t]) rat_n[t] = fhit[t], rat_span[t] = 1;
          ++fill_trigs[t];
        }
        raw_left[t] = fm_maxf(fl * 2.0f, held_pad(t) ? lane_sl[t] * (static_cast<float>(steps_to_next(t, cur_step[t])) + 0.5f)
                                                      : lane_sl[t] * (static_cast<float>(trig_tie[t]) +
                                                                      gate_frac(E(t, TP_LENGTH))));   // (0.16.15: its
                                                                                                     // length over
                                                                                                     // the steps)
        ratchet_layout(t);
        tnote[t] = static_cast<float>(quantize_t(t, iround(48.0f + 12.0f * static_cast<float>(iround(E(t, TP_OCTAVE))) +
                                                      static_cast<float>(iround(E(t, TP_TRANSPOSE))) +
                                                      E(t, TP_NOTE)) + fdn[t]));
        climb_t[t] = -1;                // CLIMB (0.16.7): a trig that plays, its step's count
        int ca = iround(E(t, TP_CLIMB)), cs = cur_step[t];
        climb_dice[t] = !ca && dice_climb[t] && rat_n[t] > 1;   // (0.16.8: else the dose's roll, climbing)
        if (climb_dice[t]) ca = dice_climb[t];
        if (cs >= 0 && cs < ENG_STEPS) {
          if (ca && rat_on[t]) {
            climb_t[t] = cs;
            climb_amt[t] = ca;
            climb_base[t] = iround(tnote[t]);
            climb_play(t, 0);
          } else if (!ca) {
            climb_off[t][cs] = 0;
          }
        }
      } else if (rise) {                // Endless' note: its randoms, no locks
        climb_t[t] = -1;
        new_trig(t, e_pos[t], 0);
        {                               // (Endless: its ratchets over its own step)
          float keep = lane_sl[t];
          lane_sl[t] = el[t];
          ratchet_layout(t);
          lane_sl[t] = keep;
        }
        tnote[t] = static_cast<float>(quantize_t(t, e_note[t] + (am == AM_LOOP ? proj.endless_key[t] : 0) + Pi(PG_ARP(t), 3) +
                                               12 * iround(E(t, TP_OCTAVE)) + iround(E(t, TP_TRANSPOSE)) +
                                               iround(tdelta[t][TP_NOTE])));
      }
      if (e_rec[t]) {
        raw = 0;
      } else {
        raw = (playing || (e_free[t] && endless[t])) && raw_left[t] > 0.0f;
        raw_left[t] -= fl;
      }
      if (!rat_on[t]) {
        g = 0;
      } else if (rat_n[t] <= 1) {
        g = raw;
        if (!raw) rat_on[t] = 0;
      } else {                          // the ratchets: n gates over their span (ratchet_layout), each half its gap
        int i = rat_i[t] < 0 ? 0 : rat_i[t], n = rat_n[t] > 16 ? 16 : rat_n[t];
        while (i < n && rat_pos[t] >= rat_t[t][i + 1]) ++i;
        if (i != rat_i[t]) {            // a hit: CURVE to the right, each softer (a bounce's); CLIMB's (0.16.7)
          rat_i[t] = i;
          if (i > 0 && i < n) climb_play(t, i);
          float c = E(t, TP_CURVE);
          if (c > 0.0f && n > 1) tdelta[t][TP_LEVEL] = rat_lvl[t] - 0.35f * c * static_cast<float>(i) / static_cast<float>(n - 1);
        }
        g = i < n && rat_pos[t] - rat_t[t][i] < 0.5f * (rat_t[t][i + 1] - rat_t[t][i]);
        rat_pos[t] += fl;
        if (i >= n) rat_on[t] = 0, tdelta[t][TP_LEVEL] = rat_lvl[t];
      }
      if ((fresh[t] || rise) && last_g[t]) g = 0;   // a trig on a gate still high (swung, legato): low for a frame
      fnote[t][k] = tnote[t];
      if ((key_on[t] || key_hold[t] > 0) && kbp_t[t]) {   // the keys' chords: the machine plays them (set_voice), the
        g = 0;                          // steps silent meanwhile
        if (key_hold[t] > 0) --key_hold[t];
      } else if (key_on[t] || key_hold[t] > 0) {   // the keyboard plays over; a new key, a new trig
        g = !(key_retrig[t] && k == 0);
        trig_cn[t] = 0;                 // (a key alone, not the step's chord)
        fnote[t][k] = static_cast<float>(quantize_t(t, key_note[t] + 12 * iround(E(t, TP_OCTAVE)) +
                                                  iround(E(t, TP_TRANSPOSE))));   // (OCTAVE too, as the steps)
        if (key_hold[t] > 0) --key_hold[t];
      }
      fg[t][k] = static_cast<uint8_t>(g);
      if (t == pad_track && g && !last_g[t]) {   // PADS: a trig, its progression's next chord
        ++pad_pos;
        pads_now(fnote[t][k]);
      }
      last_g[t] = g;
      if (t == duck_src()) {            // the duck's source (T1 unless chosen): a rise ducks the others
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
inline int family(int kind) {           // (0.16.40; 0.16.49: the SDS' four, one kit on a track)
  return kind == MK_PLAITS || kind == MK_DRUM || kind == MK_LXR || kind == MK_SDS;
}
inline int voice_model(int t) {         // (0.16.40) the model the voice plays: its trig's (the track's family), else
  const int a = proj.algo[PG_TRACK(t, 0)], m = trig_model[t] - 1;   // the track's
  return m >= 0 && m < ENG_MODELS && family(mdef(a).kind) && mdef(m).kind == mdef(a).kind ? m : a;
}
// a model with its own eight knobs (an FM drum, a drum, ZERO): each 0..1 (a bipolar knob from its middle), the
// track's parameters after FREQ (1..8: HARMONICS, TIMBRE, MORPH, DECAY, COLOUR, T.ENV, M.ENV, P.ENV)
int lx_model(int t) { return track_kind(t) == MK_LXR ? track_sub(t) : -1; }
inline int own_knobs(int t) { return track_kind(t) != MK_PLAITS; }
inline int zero_on(int t) { return track_kind(t) == MK_ZERO; }

int note_degree(float note, int root, uint16_t m) {   // the scale's note at the note or under it: its degree (MANUAL)
  int rel = ((static_cast<int>(note + 0.5f) - root) % 12 + 12) % 12, deg = 0;
  for (int i = 0, d = 0; i < 12; ++i)
    if ((m >> i) & 1u) {
      if (i <= rel) deg = d;
      ++d;
    }
  return deg;
}
// PADS: its chord now (each block, each of its trigs): the progression's (at its place), MANUAL's (the note's degree),
// built in the scale (the progression's own mode when SCALE is Off or Prog), COLOR's extensions; the chord's scale
// (SCALE Prog: every track's notes in it)
void pads_now(float note) {
  const int t = pad_track;
  if (t < 0) {
    pad_n = 0;
    return;
  }
  const int sc = Pi(PG_GLOBAL, 1), root = Pi(PG_GLOBAL, 2), prog = pads_prog(E(t, 1));
  const uint16_t m = base_mask(sc) ? base_mask(sc) : base_mask(pads_prog_mode(prog));
  const int len = pads_prog_len(prog);
  if (pad_pos >= len) pad_pos %= len;
  pad_n = pads_chord(prog, pad_pos < 0 ? 0 : pad_pos, prog ? 0 : note_degree(note, root, m), m, E(t, 2), pad_chord,
                     &pad_raised);
  pad_mask = pads_chord_scale(pad_chord, pad_n, m, pad_raised);
}
// (0.16.28) VOCODER's PLAY, in FREQ's place (its knob from the middle): to the left Mono, at noon Poly (the trig's
// chord, the keys), to the right Manual (the key picks the scale's chord, as PADS'), then PADS' 26 progressions
inline int voc_play(float v) { return v < -1.0f / 54.0f ? -1 : iround(clampf(v, 0.0f, 1.0f) * 27.0f); }
__attribute__((noinline)) MI_COLD int voc_pl(int t) { return voc_play(E(t, 0)); }   // (a track's, now)
inline int voc_play_param(int param) {
  const int t = param / 12;
  return param >= 0 && param < ENG_TRACKS * 12 && param == PG_TRACK(t, 0) * 4 && track_kind(t) == MK_VOCODER;
}
// (0.16.28) VOCODER on Manual or a progression, at a trig: the next chord (PADS' own, its triad and the notes its
// progression adds: four at most), voiced near the last one (each voice moving the least it can), around the note
__attribute__((noinline)) MI_COLD void voc_chord_now(int t, float note) {
  const int pl = voc_pl(t);
  if (pl <= 0) {
    vc_n[t] = 0;
    return;
  }
  const int prog = pl - 1, sc = Pi(PG_GLOBAL, 1), root = Pi(PG_GLOBAL, 2), len = pads_prog_len(prog);
  const uint16_t m = base_mask(sc) ? base_mask(sc) : base_mask(pads_prog_mode(prog));
  vc_pos[t] = static_cast<int8_t>((vc_pos[t] + 1) % len);
  int c[6], raised;
  int n = pads_chord(prog, vc_pos[t], prog ? 0 : note_degree(note, root, m), m, 1.5f / 9.0f, c, &raised);
  n = n > 4 ? 4 : n;
  float r = static_cast<float>(root + c[0]);   // (its root at the note or under it)
  while (r > note + 0.5f) r -= 12.0f;
  while (r + 12.0f <= note + 0.5f) r += 12.0f;
  const int had = vc_n[t] == n;
  float best[4], bc = 1e9f;
  for (int inv = 0; inv < n; ++inv)     // (its inversions, an octave down or not)
    for (int sh = -12; sh <= 0; sh += 12) {
      float q[4], lo = 1e9f, cost = 0.0f;
      for (int i = 0; i < n; ++i) {
        q[i] = r + static_cast<float>(c[i] - c[0] + (i < inv ? 12 : 0) + sh);
        lo = q[i] < lo ? q[i] : lo;
      }
      for (int i = 1; i < n; ++i)       // (low to high, as the last one was kept)
        for (int j = i; j > 0 && q[j] < q[j - 1]; --j) {
          const float x = q[j];
          q[j] = q[j - 1], q[j - 1] = x;
        }
      for (int i = 0; i < n; ++i) cost += had ? fm_fabsf(q[i] - vc_ch[t][i]) : 0.0f;
      if (!had) cost = fm_fabsf(lo - note);
      if (lo < note - 7.0f || lo > note + 9.0f) cost += 100.0f;   // (not too far from where the note puts it)
      if (cost < bc)
        for (int i = 0; i < n; ++i) best[i] = q[i], bc = cost;
    }
  for (int i = 0; i < n; ++i) vc_ch[t][i] = best[i];
  vc_n[t] = static_cast<uint8_t>(n);
}
// the machines' state on the tracks (0.13): ZERO's, a drum's, the FM's, POLY's share a track's, one a track at a time;
// its owner, the machine whose state it holds (0xFF: none yet), made again when another comes (its _reset)
alignas(8) uint8_t mstate[ENG_TRACKS][MF_STATE_BYTES];
uint8_t mowner[ENG_TRACKS] = {0xFF, 0xFF, 0xFF, 0xFF};
// (0.16.14) a track's Plaits voice's memory lent (VOCODER's): made Plaits' again when another machine comes, its epoch
// another (what was there gone)
uint8_t vlent[ENG_TRACKS];
uint32_t vepoch[ENG_TRACKS];
void mclaim(int t) {
  const int k = track_kind(t);
  if (vlent[t] && k != MK_VOCODER) {
    mp_init(voice[t]);
    ++vepoch[t];
    vlent[t] = 0;
  }
  if (mowner[t] == k) return;
  if (k == MK_ZERO) zero_reset(t);
  else if (k == MK_DRUM) drum_reset(t);
  else if (k == MK_SDS) sds_reset(t);
  else if (k == MK_FM) fm4_reset(t);
  else if (k == MK_POLY) poly_reset(t);
  else if (k == MK_PROPHET) pro_reset(t);
  else if (k == MK_CHOIR) choir_reset(t);
  else if (k == MK_TAURUS) taurus_reset(t);
  else if (k == MK_VOCODER) voc_reset(t);
  else if (k == MK_STATIC) static_reset(t);
  mowner[t] = static_cast<uint8_t>(k);
}
uint16_t harmony_mask() {               // the harmony's scale (POLY's chords): PADS' (its chords'), else the project's
  const int sc = Pi(PG_GLOBAL, 1);      // (Off, Prog without PADS: none)
  if (pad_track >= 0) return base_mask(sc) ? base_mask(sc) : base_mask(pads_prog_mode(pads_prog(E(pad_track, 1))));
  return base_mask(sc);
}
inline int lx_param(int t, int i) { return PG_TRACK(t, 0) * 4 + 1 + i; }
inline float lx_u(int param, float v) { return eng_pdef(param)->kind == K_BIP ? (v + 1.0f) * 0.5f : v; }
inline float static_mode_u(int t) { return lx_u(lx_param(t, 0), proj.val[lx_param(t, 0)]); }   // (STATIC's MODE)
int kb_poly(int t) {                    // (0.16.15) POLY in MODE Poly, PROPHET with CHORD Off, VOCODER in Poly
  const int k = track_kind(t);
  if (k == MK_VOCODER) return voc_pl(t) == 0;
  if (k == MK_POLY) return lx_u(lx_param(t, 6), E(t, 7)) >= 0.5f;
  if (k == MK_PROPHET) return poly_chord(lx_u(lx_param(t, 7), E(t, 8))) == 0;
  return 0;
}
MI_COLD void kit_of(int m, float* k) {          // a model's knobs as its kit has them, 0..1 (Plaits': at noon)
  const ModelDef& d = mdef(m);
  if (d.kind == MK_ZERO) zero_defaults(k);
  else if (d.kind == MK_DRUM) drum_defaults(d.sub, k);
  else if (d.kind == MK_SDS) sds_defaults(d.sub, k);
  else if (d.kind == MK_FM) fm4_defaults(k);
  else if (d.kind == MK_PADS) pads_defaults(k);
  else if (d.kind == MK_POLY) poly_defaults(k);
  else if (d.kind == MK_PROPHET) pro_defaults(k);
  else if (d.kind == MK_CHOIR) choir_defaults(k);
  else if (d.kind == MK_TAURUS) taurus_defaults(k);
  else if (d.kind == MK_VOCODER) voc_defaults(k);
  else if (d.kind == MK_STATIC) static_defaults(k);
  else if (d.kind == MK_LXR) lx_defaults(d.sub, k);
  else
    for (int i = 0; i < LX_KNOBS; ++i) k[i] = 0.5f;
}
MI_COLD void lx_kit(project_t* p, int t) {      // its knobs as its kit has them (a drum's, ZERO's: their own)
  float k[LX_KNOBS];
  const ModelDef& d = mdef(p->algo[PG_TRACK(t, 0)]);
  kit_of(p->algo[PG_TRACK(t, 0)], k);
  for (int i = 0; i < LX_KNOBS; ++i) {
    int param = lx_param(t, i);
    p->val[param] = eng_pdef(param)->kind == K_BIP ? 2.0f * k[i] - 1.0f : k[i];
  }
  if (d.kind == MK_DRUM || d.kind == MK_LXR || d.kind == MK_SDS)   // OCTAVE 0: C3 plays the kit's own pitch (a
    p->val[PG_TRACK(t, 2) * 4 + 3] = 0.0f;     // bass's -1 would halve a kick; ZERO, the FM play the note as Plaits
                                               // does: its octave kept)
}

// the presets (0.15.1): a track's sound, its eight pages (eng_sound_t) in this order: Plaits' three (the sound, the
// envelope, the pitch), the bits, the filter's three, the glitch
int snd_page(int t, int i) {
  return i < 3 ? PG_TRACK(t, i) : i == 3 ? PG_BEND(t) : i < 7 ? PG_FILT(t, i - 4) : PG_GLITCH(t);
}

// the factory's sounds (bank A): a model (its kind, its index there), its eight knobs as its kit's are written (0..1, a
// bipolar knob from its middle; < 0: the kit's own; Plaits': HARMONICS, TIMBRE, MORPH, then the envelope's DECAY,
// COLOUR, T.ENV, M.ENV and P.ENV), its FREQ, GATE, TRANSP and OCTAVE, its filter (its model; its twelve as the project
// keeps them: CUTOFF, RESO, ENV, DECAY; DRIVE, PING, FM, KEY; TYPE, NOISE, COLOR, ATTACK), its bits (CLOCK, BITS, MASK,
// SHAPE), its glitch (GLITCH, SIZE, BEND, DROP)
#define STEP(i, n) ((static_cast<float>(i) + 0.5f) / static_cast<float>(n))
struct FactorySound {
  const char* name;
  uint8_t kind, sub;
  float k[8];
  float freq, gate, transp, octave;
  uint8_t filt;
  float f[12], bits[4], glitch[4];
};
#define NO_FILTER MF_OFF, {0.6f, 0.3f, 0.3f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f, MF_LP, 0.0f, 0.0f, 0.0f}
#define NO_BITS {0.0f, 0.0f, 0.0f, 0.0f}
#define NO_GLITCH {0.0f, static_cast<float>(GL_16), 0.0f, 0.0f}
const FactorySound kFactory[ENG_FACTORY] = {
  // the drums: a long kick, its sweep slow, round; a hard one, driven, its click up; a snare, its noise longer, driven;
  // a clap, driven
  {"KICK LONG", MK_DRUM, 0, {0.0f, 0.42f, 0.70f, 0.62f, 0.05f, 0.08f, 0.58f, 0.74f}, 0.0f, 0.5f, -2.0f, 0.0f,
   NO_FILTER, NO_BITS, NO_GLITCH},
  {"KICK HARD", MK_DRUM, 0, {0.2f, 0.30f, 0.50f, 0.82f, 0.12f, 0.25f, 0.92f, 0.86f}, 0.0f, 0.5f, 0.0f, 0.0f,
   NO_FILTER, NO_BITS, NO_GLITCH},
  {"SNARE", MK_DRUM, 1, {-1.0f, -1.0f, -1.0f, 0.74f, 0.47f, 0.66f, 0.68f, -1.0f}, 0.0f, 0.5f, 0.0f, 0.0f,
   NO_FILTER, NO_BITS, NO_GLITCH},
  {"CLAP", MK_DRUM, 2, {-1.0f, -1.0f, -1.0f, -1.0f, 0.50f, -1.0f, 0.66f, -1.0f}, 0.0f, 0.5f, 0.0f, 0.0f,
   NO_FILTER, NO_BITS, NO_GLITCH},
  // the LXR-02's hi-hat closed, then open; a tom, longer, lower; metal (Plaits' modal resonator, its structure high)
  {"HAT", MK_DRUM, 3, {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f}, 0.0f, 0.5f, 0.0f, 0.0f,
   NO_FILTER, NO_BITS, NO_GLITCH},       // (0.16.43: MIFM1's HAT, the FM HI-HAT gone; open: its decays ~0.3 s)
  {"OPEN HAT", MK_DRUM, 3, {-1.0f, -1.0f, 0.42f, -1.0f, 0.56f, -1.0f, -1.0f, -1.0f}, 0.0f, 0.5f, 0.0f, 0.0f,
   NO_FILTER, NO_BITS, NO_GLITCH},
  {"TOM", MK_DRUM, 4, {-1.0f, -1.0f, 0.56f, -1.0f, -1.0f, -1.0f, -1.0f, 0.68f}, 0.0f, 0.5f, -3.0f, 0.0f,
   NO_FILTER, NO_BITS, NO_GLITCH},
  {"METAL", MK_PLAITS, 20, {0.82f, 0.72f, 0.42f, 0.5f, 0.6f, 0.5f, 0.5f, 0.5f}, 0.0f, 0.5f, 0.0f, 0.0f,
   NO_FILTER, NO_BITS, NO_GLITCH},
  // the basses: acid (POLY: a saw, its filter low, its resonance up, its envelope deep); the FM's, as the demo has it
  // (four in a chain, a pluck, through the ladder); a reese (PROPHET: two saws drifting apart, held); then the demo's
  // stabs (the FM: a minor seventh, a pluck, through SEM)
  {"ACID", MK_POLY, 0, {0.0f, 0.30f, 0.82f, 0.32f, 0.0f, 0.9f, 0.5f, 0.5f}, 0.0f, 0.45f, 0.0f, -1.0f,
   NO_FILTER, NO_BITS, NO_GLITCH},
  {"FM BASS", MK_FM, 0, {STEP(0, 8), -1.0f, 0.5f, 0.42f, -1.0f, 9.5f / 15.0f, 0.8f, 0.5f}, 0.0f, 0.35f, 0.0f, -1.0f,
   MF_LADDER, {0.5f, 0.55f, 0.45f, 0.28f, 0.3f, 0.0f, 0.0f, 0.5f, MF_LP, 0.0f, 0.0f, 0.0f}, NO_BITS, NO_GLITCH},
  {"REESE", MK_PROPHET, 0, {STEP(0, 13), 0.44f, 0.18f, 1.0f, 0.75f, 0.62f, 0.5f, 0.5f}, 0.0f, 0.85f, 0.0f, -1.0f,
   NO_FILTER, NO_BITS, NO_GLITCH},
  {"FM STAB", MK_FM, 0, {STEP(5, 8), STEP(27, 30), 0.3f, 0.5f, 0.35f, 9.5f / 15.0f, 0.7f, 0.5f}, 0.0f, 0.35f, 0.0f,
   0.0f, MF_SEM, {0.58f, 0.45f, 0.45f, 0.32f, 0.0f, 0.0f, 0.0f, 0.3f, MF_LP, 0.15f, -0.3f, 0.05f}, NO_BITS, NO_GLITCH},
  // a dark pad (PADS: Lament, sevenths, wide, swelling, long: a trig of a sixteenth blooms); a pluck (Plaits' string); a lead (PROPHET: synced, its
  // sweep); noise (Plaits': clocked, resonant, crushed to five bits with holes, glitched)
  {"DARK PAD", MK_PADS, 0, {STEP(5, 17), STEP(1, 4), STEP(2, 4), 0.8f, 0.7f, 0.3f, 0.875f, 0.334f}, 0.0f, 0.95f, 0.0f,
   0.0f, NO_FILTER, NO_BITS, NO_GLITCH},
  {"PLUCK", MK_PLAITS, 19, {0.35f, 0.62f, 0.55f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}, 0.0f, 0.5f, 0.0f, 0.0f,
   NO_FILTER, NO_BITS, NO_GLITCH},
  {"SYNC LEAD", MK_PROPHET, 0, {STEP(8, 13), 0.68f, 0.22f, 0.6f, 0.3f, 0.7f, 0.3f, 0.5f}, 0.0f, 0.6f, 0.0f, 1.0f,
   NO_FILTER, NO_BITS, NO_GLITCH},
  {"LE BRUIT", MK_PLAITS, 17, {0.7f, 0.62f, 0.75f, 0.42f, 0.6f, 0.5f, 0.5f, 0.5f}, 0.0f, 0.5f, 0.0f, 0.0f,
   NO_FILTER, {0.35f, 0.6f, 5.0f, static_cast<float>(BS_OFF)}, {0.4f, static_cast<float>(GL_32), 0.3f, 0.35f}},
};
#undef STEP
#undef NO_FILTER
#undef NO_BITS
#undef NO_GLITCH

// (0.16.15) the keys held on a track to its machine's voices (POLY in MODE Poly, PROPHET with CHORD Off, SQ-1): the
// newest four, each its pitch as the keyboard's (OCTAVE, TRANSPOSE, the scale, FREQ); a key tapped and let go within
// a block: the last one, its few blocks
void set_filter(int t);
void keys_to_voices(int t, int kind) {
  int ids[4], n = 0;
  float nt[4];
  if (kb_poly(t)) {
    const int kn = kb_n[t];
    for (int i = kn > 4 ? kn - 4 : 0; i < kn; ++i) ids[n++] = kb_note[t][i];
    if (!n && key_hold[t] > 0) ids[n++] = key_note[t];
    const float fq = kind == MK_VOCODER ? 0.0f : 24.0f * E(t, 0);   // (VOCODER: FREQ's place its PLAY)
    for (int i = 0; i < n; ++i) {
      float nn = static_cast<float>(quantize_t(t, ids[i] + 12 * iround(E(t, TP_OCTAVE)) + iround(E(t, TP_TRANSPOSE)))) + fq;
      if (scale_on_t(t)) nn = static_cast<float>(quantize(iround(nn)));
      nt[i] = nn;
    }
    if (n) vnote[t] = nt[n - 1];
    lead_chord(t, nt, n);               // (0.16.42: the keys' chord, the harmony when the track leads)
    if (key_new[t]) {                   // (a key: the filter's envelope and ping, the trig's)
      key_new[t] = 0;
      set_filter(t);
      mf_trigger(t, 0);
      ++vrises[t];
    }
  }
  if (kind == MK_POLY) poly_keys(t, ids, nt, n);
  else if (kind == MK_PROPHET) pro_keys(t, ids, nt, n);
  else if (kind == MK_VOCODER) voc_keys(t, ids, nt, n);
}

// (0.16.16) the trig's notes: its note, its step's chord over it (a machine that plays chords: POLY in Poly, REV3 with
// CHORD Off, VOCODER in Poly), each in the scale; how many
int step_chord(int t, float note, float* out) {
  int n = 0;
  out[n++] = note;
  if (!kb_poly(t)) return n;
  for (int i = 0; i < trig_cn[t] && i < 3; ++i) {
    float x = note + static_cast<float>(trig_iv[t][i]);
    if (scale_on_t(t)) x = static_cast<float>(quantize(iround(x)));
    out[n++] = x;
  }
  return n;
}

// (0.16.43) CHOIR's parts, the soprano's first: the step's chord (its notes from the top; parts past them an octave
// under its lowest), or the note harmonized in the scale (Off: the major one on ROOT), the melody on top: the alto a
// third under it, the tenor a fifth (a triad under the note, its root the tenor's), the bass an octave under the
// tenor; as many as VOICES has (nv)
int choir_parts(int t, float note, int nv, float* out) {
  nv = nv < 1 ? 1 : nv > 4 ? 4 : nv;
  if (trig_cn[t] > 0 && nv > 1) {        // (a step's chord, its top the soprano's; Solo: NOTE, as a mono machine)
    float c[4];
    int n = 0;
    c[n++] = note;
    for (int i = 0; i < trig_cn[t] && i < 3; ++i) {
      float x = note + static_cast<float>(trig_iv[t][i]);
      if (scale_on_t(t)) x = static_cast<float>(quantize(iround(x)));
      c[n++] = x;
    }
    for (int i = 1; i < n; ++i)         // (high to low)
      for (int j = i; j > 0 && c[j] > c[j - 1]; --j) {
        const float x = c[j];
        c[j] = c[j - 1], c[j - 1] = x;
      }
    for (int i = 0; i < nv; ++i) out[i] = i < n ? c[i] : c[n - 1] - 12.0f * static_cast<float>(i - n + 1);
    return nv;
  }
  out[0] = note;
  if (nv == 1) return 1;
  const int root = Pi(PG_GLOBAL, 2);
  uint32_t m = scale_on_t(t) ? scale_mask(Pi(PG_GLOBAL, 1)) : 0u;
  m = m & 0xFFFu ? m : 0xAB5u;
  auto in = [&](int x) { return (m >> (((x - root) % 12 + 12) % 12)) & 1u; };
  int x = iround(note);
  for (int g = 0; g < 12 && !in(x); ++g) --x;   // (the note in the scale, or the one under it)
  int deg[5] = {x, 0, 0, 0, 0};
  for (int d = 1; d <= 4; ++d) {        // (the scale's notes under it, one by one)
    int y = deg[d - 1] - 1;
    for (int g = 0; g < 12 && !in(y); ++g) --y;
    deg[d] = y;
  }
  const float frac = note - static_cast<float>(x);   // (a note between the scale's: its parts the same way off)
  out[1] = static_cast<float>(deg[2]) + frac;
  out[2] = static_cast<float>(deg[4]) + frac;
  out[3] = out[2] - 12.0f;
  return nv;
}

void set_voice(int t) {
  mclaim(t);
  if (own_knobs(t)) {
    float k[LX_KNOBS];
    for (int i = 0; i < LX_KNOBS; ++i) k[i] = lx_u(lx_param(t, i), E(t, 1 + i));
    int kind = track_kind(t), sub = mdef(voice_model(t)).sub;   // (0.16.40: its trig's model, a drum's)
    float note = vnote[t] + (kind == MK_VOCODER ? 0.0f : 24.0f * E(t, 0));   // (VOCODER: FREQ's place its PLAY)
    if (scale_on_t(t)) note = static_cast<float>(quantize(iround(note)));
    if (kind == MK_ZERO) zero_set(t, note, k);
    else if (kind == MK_DRUM) drum_set(t, sub, note, k);
    else if (kind == MK_SDS) sds_set(t, sub, note, k);
    else if (kind == MK_FM) fm4_set(t, note, k);
    else if (kind == MK_PADS) {
      if (t == pad_track) pads_set(note, k, pad_chord, pad_n, Pi(PG_GLOBAL, 2));
    } else if (kind == MK_POLY) {       // (0.16.15: one note; its chords the keys', 0.16.16 the step's)
      float sc[4];
      const int cn = step_chord(t, note, sc);
      lead_chord(t, sc, cn);
      poly_set(t, sc, cn, k);
    } else if (kind == MK_PROPHET) {    // (its chord: the S-1's memory, the harmony; Off: the step's, 0.16.16)
      float nn[4];
      int cn = poly_notes(k[7], note, pad_chord, pad_track >= 0 ? pad_n : 0, harmony_mask(), Pi(PG_GLOBAL, 2), nn);
      if (cn == 1) cn = step_chord(t, note, nn);
      lead_chord(t, nn, cn);
      pro_set(t, nn, cn, k);
    } else if (kind == MK_CHOIR) {      // (0.16.43: the step's chord, or the note harmonized: choir_parts)
      float sc[4];
      choir_set(t, sc, choir_parts(t, note, choir_voices(k[1]), sc), k);
    } else if (kind == MK_TAURUS) {
      taurus_set(t, note, k);
    } else if (kind == MK_STATIC) {
      static_set(t, note, k);
    } else if (kind == MK_VOCODER) {    // (0.16.28: Mono a voice; Poly the step's chord; a progression's, Manual's)
      float sc[4];
      const int pl = voc_pl(t);
      int cn = pl > 0 ? vc_n[t] : 0;
      for (int i = 0; i < cn; ++i) sc[i] = vc_ch[t][i];
      if (!cn) cn = step_chord(t, note, sc);
      lead_chord(t, sc, cn);
      voc_set(t, sc, cn, pl < 0, k);
    }
    else lx_set(t, sub, note, k);
    if (kind == MK_POLY || kind == MK_PROPHET || kind == MK_VOCODER) keys_to_voices(t, kind);
    return;
  }
  // the models whose MORPH is their decay (string, modal, the drums: their own envelope, no LPG) take DECAY too, around
  // its middle: the envelope page's DECAY, its locks and its random move every model's decay
  int m = mdef(voice_model(t)).sub;     // (Plaits' engine; 0.16.40: its trig's)
  float morph = E(t, 3);
  if (m >= 19) morph = clampf(morph + E(t, 4) - 0.5f, 0.0f, 1.0f);
  float note = vnote[t] + 24.0f * E(t, 0);   // FREQ (its cables, its locks) over the note: with SCALE on, in it too
  if (scale_on_t(t)) note = static_cast<float>(quantize(iround(note)));
  mp_set(voice[t], m, note, E(t, 1), E(t, 2), morph, E(t, 4), E(t, 5));
  mp_amounts(voice[t], E(t, TP_PITCHENV), E(t, 6), E(t, 7));   // the envelope on the pitch (Plaits' FM attenuverter)
}

// a voice silent a while, its gate low (its note over, its LPG or its own envelope down under -90 dB): it rests,
// rendered again at its next gate (its oscillators where they were: a new note starts from its trigger anyway)
const int kQuiet = 4;                   // the silent blocks before it rests
int quiet[ENG_TRACKS];
int voice_rests(int t) {
  if (vgate[t] || quiet[t] < kQuiet || gl_busy[t] || kb_n[t] || key_hold[t] > 0) return 0;
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
  float ei, eo, lg;                     // (0.16.31) its level: what goes in and out (followed), the gain given back
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
  if (on && !g.on) g.ei = g.eo = 0.0f, g.lg = 1.0f;   // (its level: as it was, then followed)
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

// (0.16.31) the bits keep the track's level: what comes out brought back to what went in, both followed over ~50 ms
// (a mask, or few bits on a quiet sound, made it up to 13 dB louder): down as far as it takes, up 3 dB at most
__attribute__((noinline)) void level_keep(float* x, uint32_t n, float ei, float eo, float* fi, float* fo, float* lg, float a, float up) {
  *fi += (ei - *fi) * a;
  *fo += (eo - *fo) * a;
  float k = *fo > 1e-9f ? fm_minf(fm_sqrtf(*fi / *fo), up) : 1.0f;
  *lg += (k - *lg) * 0.3f;              // (a gain a block, glided from block to block: the bits' steps stay steps)
  for (uint32_t i = 0; i < n; ++i) x[i] *= *lg;
}
void apply_bend(int t, float* x, uint32_t n) {
  Bend& g = bend[t];
  if (!g.on) return;
  float ei = 0.0f, eo = 0.0f;
  for (uint32_t i = 0; i < n; ++i) {
    ei += x[i] * x[i];
    x[i] = bend_one(g, x[i]);
    eo += x[i] * x[i];
  }
  level_keep(x, n, ei, eo, &g.ei, &g.eo, &g.lg, 0.06f, 1.41f);
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

// TRAUMA's drive on a track, after its filter: the drums' curve (x - x^3 / 3 up to 1, then flat at 2/3), antialiased
// (ADAA: its antiderivative), its gain 1 to 8 (the dose's share this trig, squared), warm (biased: even harmonics, the
// DC taken off by a coupling capacitor at 8 Hz) or hard. The track's level followed (its peaks, falling over 0.1 s)
// and brought to 0.6 into the curve, then given back: the same drive on a quiet track as on a loud one, its level kept
// (its two sides' mean, a warm drive's lopsided); its tails pushed up into it as they fall (to a twelfth:
// then no more), as a distortion pedal sustains. In from the sound as it is over the knob's first quarter. Glided
// over the block from the last one's (a trig, a turn: no step); at 0 it lets the sound through, untouched
struct TraumaDrive {
  float x1, dc, dcy;                    // the last input (ADAA), the capacitor
  float g, b, fb, k, m, s;              // the gain, the bias, the curve at the bias, the level given back, the share,
  float env;                            // the level brought to 0.6; the track's peaks followed
};
TraumaDrive tdrv[ENG_TRACKS];
inline float tclip(float x) { return x >= 1.0f ? 0.6666667f : x <= -1.0f ? -0.6666667f : x - x * x * x * (1.0f / 3.0f); }
inline float tclip_F(float x) {
  float a = fm_fabsf(x);
  return a <= 1.0f ? x * x * (0.5f - x * x * (1.0f / 12.0f)) : 0.6666667f * a - 0.25f;
}
// the curve's ADAA from x1 to x: the antiderivative's divided difference, (F(x) - F(x1)) / (x - x1), written out
// where both are on one piece of it (0.14.2): on the cubic, (x + x1)(1/2 - (x^2 + x1^2) / 12); both flat on one side,
// +/-2/3. No division there (the FM-1 divides slowly) and no cancellation (the subtraction of two near values the
// division then blew up: 3e-3 off at worst, now 7e-5); a division only where a step crosses the knee (a few samples
// in a hundred, driven hard)
inline float tclip_adaa(float x, float x1) {
  float a = fm_fabsf(x), a1 = fm_fabsf(x1);
  if (a <= 1.0f && a1 <= 1.0f) return (x + x1) * (0.5f - (x * x + x1 * x1) * (1.0f / 12.0f));
  if (a >= 1.0f && a1 >= 1.0f && (x > 0.0f) == (x1 > 0.0f)) return x > 0.0f ? 0.6666667f : -0.6666667f;
  float dx = x - x1;
  return fm_fabsf(dx) > 1e-4f ? (tclip_F(x) - tclip_F(x1)) / dx : tclip(0.5f * (x + x1));
}
void trauma_drive(int t, float* g, float* b, float* k, float* m) {   // a track's drive now: gain, bias, level, share
  float d = trauma01(), u = d * tr_f[t];
  *g = 1.0f + 7.0f * u * u;
  *b = tr_hard[t] ? 0.0f : 0.3f * fm_minf(1.0f, 2.0f * u);
  float fb = tclip(*b), up = tclip(0.6f * *g + *b) - fb, dn = fb - tclip(*b - 0.6f * *g);
  *k = 1.2f / (up + dn);                // (both sides' mean back to 0.6)
  *m = fm_minf(1.0f, 4.0f * d);
}
// its samples: the bias and its curve held where they do not move (between trigs: most blocks), all of it without
// the blend (from the first quarter); the same sums either way (8 % fewer instructions, the same samples)
template <bool kBias, bool kWet>
void trauma_run(float* x, uint32_t n, float* st, const float* d) {   // st: g b fb k m x1 dc dcy; d: their steps
  float g = st[0], b = st[1], fb = st[2], k = st[3], m = st[4], x1 = st[5], dc = st[6], dcy = st[7];
  const float dg = d[0], db = d[1], dfb = d[2], dk = d[3], dm = d[4];
  for (uint32_t i = 0; i < n; ++i) {
    g += dg, k += dk;
    if (kBias) b += db, fb += dfb;
    float xin = g * x[i] + b, y = tclip_adaa(xin, x1);
    x1 = xin;
    y = (y - fb) * k;
    float hp = y - dc + 0.99886f * dcy;   // (the capacitor)
    dc = y;
    dcy = hp;
    if (kWet) x[i] = hp;
    else m += dm, x[i] += m * (hp - x[i]);
  }
  st[5] = x1, st[6] = dc, st[7] = dcy;
}
void apply_trauma(int t, float* x, uint32_t n) {
  TraumaDrive& s = tdrv[t];
  float g1 = 1.0f, b1 = 0.0f, k1 = s.k, m1 = 0.0f;
  if (proj.trauma) trauma_drive(t, &g1, &b1, &k1, &m1);
  if (m1 <= 0.0f && s.m <= 0.0f) {      // (off, and out: nothing done)
    s.x1 = s.dc = s.dcy = s.env = 0.0f;
    return;
  }
  float pk = 0.0f;                      // the level: this block's peak, or the last falling (0.1 s)
  for (uint32_t i = 0; i < n; ++i) pk = fm_maxf(pk, fm_fabsf(x[i]));
  s.env = fm_maxf(pk, s.env * (1.0f - static_cast<float>(n) * (1.0f / 4410.0f)));
  float s1 = 0.6f / fm_maxf(s.env, 0.05f);
  if (s.m <= 0.0f) s.g = g1, s.b = b1, s.fb = tclip(b1), s.k = k1, s.s = s1;   // (coming in: from where it is)
  float inv = 1.0f / static_cast<float>(n), fb1 = tclip(b1);
  float g = s.g * s.s, g1s = g1 * s1, dg = (g1s - g) * inv, b = s.b, db = (b1 - b) * inv, fb = s.fb;
  float dfb = (fb1 - fb) * inv, k = s.k / s.s, dk = (k1 / s1 - k) * inv, m = s.m, dm = (m1 - m) * inv;
  float st[8] = {g, b, fb, k, m, s.x1, s.dc, s.dcy}, d[5] = {dg, db, dfb, dk, dm};
  const int wet = m >= 1.0f && dm == 0.0f, bias = db != 0.0f || dfb != 0.0f;   // (all of it: no blend; the bias moving)
  if (bias) wet ? trauma_run<true, true>(x, n, st, d) : trauma_run<true, false>(x, n, st, d);
  else wet ? trauma_run<false, true>(x, n, st, d) : trauma_run<false, false>(x, n, st, d);
  s.g = g1, s.b = b1, s.fb = fb1, s.k = k1, s.m = m1, s.s = s1, s.x1 = st[5], s.dc = st[6], s.dcy = st[7];
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
    tdrv[t].x1 = tdrv[t].dc = tdrv[t].dcy = tdrv[t].env = 0.0f;
    return;
  }
  mclaim(t);                            // (its machine's state: made if another's was there)
#ifdef PLAT_WEB
  {                                     // (its model, its bits if any of them is up, its glitch)
    int m = proj.algo[PG_TRACK(t, 0)];
    float fm = 0.0f;                    // (the FM: its square, its PW or SYNC)
    if (track_kind(t) == MK_FM) {
      float mu = E(t, TP_PITCHENV);
      fm = (E(t, 7) < -0.004f ? kWebFm[0] : 0.0f) + (mu < -0.004f ? kWebFm[1] : mu > 0.004f ? kWebFm[2] : 0.0f);
    }
    float own = kWebModel[m < 0 ? 0 : m > ENG_MODELS - 1 ? ENG_MODELS - 1 : m];
    if (track_kind(t) == MK_PADS) {     // (PADS: its notes, one saw each or three, its TEXTURE: Off under 0.04, then
      const float tu = (E(t, 8) + 1.0f) * 0.5f;   // Bell, Tape, Drift, Dust, Down; Dust's and Down's estimated from
      const int tz = tu < 0.04f ? -1 : static_cast<int>((tu - 0.04f) / 0.192f);   // their instructions, 0.16.16)
      const float nv = static_cast<float>(pads_voices());
      own = t != pad_track ? kWebRest
                           : kWebPads[0] + nv * kWebPads[E(t, 7) > 0.02f ? 2 : 1] +
                                 (tz == 0 ? kWebPads[3] : tz == 3 ? 31.0f : tz >= 4 ? 14.7f * nv : 0.0f);
    }
    if (track_kind(t) == MK_POLY) own = kWebPoly[0] + static_cast<float>(poly_voices(t)) * kWebPoly[1];   // (its voices)
    if (track_kind(t) == MK_PROPHET) own = kWebPro[0] + static_cast<float>(pro_voices(t)) * kWebPro[1];
    if (track_kind(t) == MK_CHOIR)       // (0.16.43: its parts)
      own = kWebChoir[0] + static_cast<float>(choir_voices(lx_u(lx_param(t, 1), E(t, 2)))) * kWebChoir[1];
    if (track_kind(t) == MK_STATIC) own = kWebStatic[static_mode(lx_u(lx_param(t, 0), E(t, 1)))];   // (0.16.27: its generator's)
    if (track_kind(t) == MK_VOCODER)     // (its voices; its bands, MOD on)
      own = kWebVoc[0] + static_cast<float>(voc_voices(t)) * kWebVoc[1] +
            (voc_mod_track(lx_u(lx_param(t, 0), E(t, 1))) >= 0 ? kWebVoc[2] : 0.0f);
    web_spend((own + fm + (bend[t].on ? kWebBend : 0.0f) +
               (glitch[t].on || glitch[t].mix > 0.0f ? kWebGlitch : 0.0f) +
               (proj.trauma || tdrv[t].m > 0.0f ? kWebTrauma : 0.0f)) * static_cast<float>(n) / 128.0f);
  }
#endif
  int kind = track_kind(t);             // an FM drum: a rise triggers it, its own envelope does the rest; ZERO, a
  int lx = kind == MK_LXR, ac = kind == MK_ZERO, dr = kind == MK_DRUM, fm = kind == MK_FM;   // drum, the FM, PADS:
  int pd = kind == MK_PADS, pd_on = pd && t == pad_track;   // their gate, their own render (PADS: one track's)
  int po = kind == MK_POLY, px = kind == MK_PROPHET, ch = kind == MK_CHOIR, tau = kind == MK_TAURUS;
  int vc = kind == MK_VOCODER, stc = kind == MK_STATIC, sd = kind == MK_SDS;
  uint32_t step = n / kFrames, pos = 0;
  for (uint32_t k = 0; k < kFrames; ++k) {
    int g = fg[t][k];
    if (g == vgate[t]) continue;
    uint32_t at = k * step;
    if (at > pos) {
      if (lx) lx_render(t, &vbuf[t][pos], at - pos);
      else if (ac) zero_render(t, &vbuf[t][pos], at - pos);
      else if (dr) drum_render(t, &vbuf[t][pos], at - pos);
      else if (sd) sds_render(t, &vbuf[t][pos], at - pos);
      else if (fm) fm4_render(t, &vbuf[t][pos], at - pos);
      else if (po) poly_render(t, &vbuf[t][pos], at - pos);
      else if (px) pro_render(t, &vbuf[t][pos], at - pos);
      else if (ch) choir_render(t, &vbuf[t][pos], at - pos);
      else if (tau) taurus_render(t, &vbuf[t][pos], at - pos);
      else if (vc) voc_render(t, &vbuf[t][pos], at - pos);
      else if (stc) static_render(t, &vbuf[t][pos], at - pos);
      else if (pd) {
        if (pd_on) pads_render(&vbuf[t][pos], at - pos);
        else for (uint32_t i = pos; i < at; ++i) vbuf[t][i] = 0.0f;
      }
      else mp_render(voice[t], &vbuf[t][pos], &vaux[t][pos], at - pos);
      pos = at;
    }
    if (g) {
      vnote[t] = fnote[t][k];
      if (vc) voc_chord_now(t, vnote[t]);   // (0.16.28: Manual's, a progression's)
      set_voice(t);
      ++vrises[t];
      set_filter(t);                    // (its KEY on the new note)
      mf_trigger(t, at);                // (the filter's envelope and ping, at that sample)
    }
    if (ac) zero_gate(t, g);
    else if (dr) drum_gate(t, g);
    else if (sd) sds_gate(t, g);
    else if (fm) fm4_gate(t, g);
    else if (po) poly_gate(t, g);
    else if (px) pro_gate(t, g);
    else if (ch) choir_gate(t, g);
    else if (tau) taurus_gate(t, g);
    else if (vc) voc_gate(t, g);
    else if (stc) static_gate(t, g);
    else if (pd) {
      if (pd_on) pads_gate(g);
    }
    else if (!lx) mp_gate(voice[t], g);
    else if (g) lx_trigger(t);
    vgate[t] = g;
  }
  if (n > pos) {
    if (lx) lx_render(t, &vbuf[t][pos], n - pos);
    else if (ac) zero_render(t, &vbuf[t][pos], n - pos);
    else if (dr) drum_render(t, &vbuf[t][pos], n - pos);
    else if (sd) sds_render(t, &vbuf[t][pos], n - pos);
    else if (fm) fm4_render(t, &vbuf[t][pos], n - pos);
    else if (po) poly_render(t, &vbuf[t][pos], n - pos);
    else if (px) pro_render(t, &vbuf[t][pos], n - pos);
    else if (ch) choir_render(t, &vbuf[t][pos], n - pos);
    else if (tau) taurus_render(t, &vbuf[t][pos], n - pos);
    else if (vc) voc_render(t, &vbuf[t][pos], n - pos);
    else if (stc) static_render(t, &vbuf[t][pos], n - pos);
    else if (pd) {
      if (pd_on) pads_render(&vbuf[t][pos], n - pos);
      else for (uint32_t i = pos; i < n; ++i) vbuf[t][i] = 0.0f;
    }
    else mp_render(voice[t], &vbuf[t][pos], &vaux[t][pos], n - pos);
  }
  {
    const int m = voice_model(t);       // (0.16.40: its trig's model's level)
    const float tg = fm_exp2f(static_cast<float>(kModelTrim[m < ENG_MODELS ? m : 0]) * 0.0415241f);   // (2^(1/24.08))
    for (uint32_t i = 0; i < n; ++i) vbuf[t][i] *= tg;
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
  apply_trauma(t, vbuf[t], n);          // TRAUMA's drive (0: nothing)
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
  float sp0[ENG_TRACKS], sp1[ENG_TRACKS];   // (0.16.19) SPREAD: the side's gain from, to (0: none)
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
// (0.16.19) SPREAD (the mixer's, a knob a track): a side made from the track itself, added to the left and taken from
// the right (complementary combs: Lauridsen's pseudo-stereo, 1954; a side diffused through all-pass filters, as
// Orban's and Polyverse's Wider do). The track's highs (above 150 Hz: two of stmlib's state variable filters, a
// 24 dB/oct high pass), delayed 8 to 13 ms, then through two of Schroeder's all-pass filters (a few ms each: the side
// a short, dense run of echoes, its combs irregular, never a flanger's teeth), each track its own delays (two tracks
// spread never comb alike). Left plus right is the track as it was, its lows the same on both sides: summed to mono (a
// club's bass, a phone) nothing cancels, nothing combs. Each side alone: the highs a little louder (1 + the side's
// gain squared: +2.1 dB at the widest), apart from the other side by a few tens of Hz at a time (heard as width; over
// a third of an octave, within a fifth of a dB). The delay in 16-bit words (its rounding some -95 dB under the track),
// the all-passes in floats, in the pool (8.8 KB)
const int kSpreadLen[ENG_TRACKS] = {353, 419, 487, 557};   // the delay, samples (8.0, 9.5, 11.0, 12.6 ms)
const int kSpreadAp[ENG_TRACKS][2] = {{113, 181}, {127, 199}, {139, 211}, {151, 223}};   // the all-passes' (2.6..5.1 ms)
const int kSpreadTotal = 353 + 419 + 487 + 557;
const int kSpreadApTotal = 113 + 181 + 127 + 199 + 139 + 211 + 151 + 223;
const float kSpreadMax = 0.8f;          // the side's gain over the middle's at the widest (L and R correlated 0.22)
const float kSpreadHz = 150.0f;         // under it: the same on both sides
const float kSpreadG = 0.6f;            // the all-passes' gain
int16_t spread_line[kSpreadTotal] PLAT_POOL;
float spread_ap[kSpreadApTotal] PLAT_POOL;
alignas(8) uint8_t sp_mem[ENG_TRACKS][sizeof(EqPair)];
EqPair* sp_p[ENG_TRACKS];
int sp_at[ENG_TRACKS][3], sp_run[ENG_TRACKS];   // the lines' places (the delay, the all-passes); running (its lines
float sp_s[ENG_TRACKS], mix_sp[ENG_TRACKS];     // hold the track's past); the knob, glided; the side's gain (the last
float sp_w[ENG_TRACKS];                 // block's); (the screen's: the side's gain over the middle's)
// its side into y (the scratch), added to the dry mix's sides: through its delay and its all-passes
void spread_side(int t, const float* x, float* y, uint32_t n, float pk, int16_t* ln, float* ap1, float* ap2, int len,
                 int m1, int m2) {
  stmlib::Svf* f = sp_p[t]->f;
  if (!sp_run[t]) {                     // (from rest: its lines silent, no past of another time)
    for (int i = 0; i < len; ++i) ln[i] = 0;
    for (int i = 0; i < m1 + m2; ++i) ap1[i] = 0.0f;
    f[0].Init();
    f[1].Init();
    sp_at[t][0] = sp_at[t][1] = sp_at[t][2] = 0;
    sp_run[t] = 1;
  }
  if (pk < 1e-6f) {                     // (silence: the filters from zero, never down into the subnormals)
    f[0].Init();
    f[1].Init();
    for (uint32_t i = 0; i < n; ++i) y[i] = 0.0f;
  } else {
    const float fc = kSpreadHz / kSr;
    f[0].set_f_q<stmlib::FREQUENCY_ACCURATE>(fc, 0.7071f);   // (Linkwitz-Riley's: two Butterworths, -6 dB at 150)
    f[1].set_f_q<stmlib::FREQUENCY_ACCURATE>(fc, 0.7071f);
    f[0].Process<stmlib::FILTER_MODE_HIGH_PASS>(x, y, n);
    f[1].Process<stmlib::FILTER_MODE_HIGH_PASS>(y, y, n);
  }
  float g = mj.sp0[t], dg = (mj.sp1[t] - g) / static_cast<float>(n);
  int at = sp_at[t][0], i1 = sp_at[t][1], i2 = sp_at[t][2];
  for (uint32_t i = 0; i < n;) {        // (in runs where none of the three lines wraps around)
    uint32_t r = n - i, a = static_cast<uint32_t>(len - at), b = static_cast<uint32_t>(m1 - i1);
    const uint32_t c = static_cast<uint32_t>(m2 - i2);
    r = r < a ? r : a;
    r = r < b ? r : b;
    r = r < c ? r : c;
    int16_t* lp = ln + at;
    float *a1 = ap1 + i1, *a2 = ap2 + i2;
    for (uint32_t k = 0; k < r; ++k, ++i) {
      float d = static_cast<float>(lp[k]) * (1.0f / 16384.0f);   // (written len samples ago)
      float v = y[i] * 16384.0f;
      v = v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v;
      lp[k] = static_cast<int16_t>(v + (v < 0.0f ? -0.5f : 0.5f));
      float w = d + kSpreadG * a1[k];   // Schroeder's: w = in + g w(m ago), out = w(m ago) - g w
      d = a1[k] - kSpreadG * w;
      a1[k] = w;
      w = d + kSpreadG * a2[k];
      d = a2[k] - kSpreadG * w;
      a2[k] = w;
      g += dg;
      d *= g;
      y[i] = d;                         // (the side, in the scratch)
      fx_dry_l[i] += d;
      fx_dry_r[i] -= d;
    }
    at += static_cast<int>(r);
    i1 += static_cast<int>(r);
    i2 += static_cast<int>(r);
    if (at == len) at = 0;
    if (i1 == m1) i1 = 0;
    if (i2 == m2) i2 = 0;
  }
  sp_at[t][0] = at;
  sp_at[t][1] = i1;
  sp_at[t][2] = i2;
  if (pk < 1e-6f) {                     // (silent: its echoes gone, its lines cleared before they would sink into
    float m = 0.0f;                     // the subnormals, at the next sound)
    for (int k = 0; k < m1 + m2; ++k) m = fm_maxf(m, fm_fabsf(ap1[k]));
    if (m < 1e-7f) sp_run[t] = 0;
  }
}
// a track's side, added to fx_dry_l and taken from fx_dry_r (and its USB pair's): its highs from x (m_dry: the
// scratch), into its delay, out of it into the all-passes, at the side's gain (sp0 to sp1 over the block). Out of
// line, in the flash: the mix (run_task's) runs from the RAM, whose room is counted (fm1.toml's o2_ram)
__attribute__((noinline)) void spread_track(int t, const float* x, uint32_t n, float a, float b) {
  float* y = m_dry;
  float pk = 0.0f;
  for (uint32_t i = 0; i < n; ++i) pk = fm_maxf(pk, fm_fabsf(x[i]));
  int off = 0, aoff = 0;
  for (int i = 0; i < t; ++i) off += kSpreadLen[i], aoff += kSpreadAp[i][0] + kSpreadAp[i][1];
  int16_t* ln = spread_line + off;
  float* ap1 = spread_ap + aoff;
  const int len = kSpreadLen[t], m1 = kSpreadAp[t][0], m2 = kSpreadAp[t][1];
  if (!sp_run[t] && pk < 1e-6f) {        // (at rest, silent: nothing to add)
    for (uint32_t i = 0; i < n; ++i) y[i] = 0.0f;
  } else {
    spread_side(t, x, y, n, pk, ln, ap1, ap1 + m1, len, m1, m2);
  }
  if (!usb_now) return;
  int16_t* u = usb_ch + 2 * t;           // the USB input's pair: the same
  for (uint32_t i = 0; i < n; ++i, u += ENG_USB_CH) {
    a += mj.dgl[t];
    b += mj.dgr[t];
    u[0] = usb16(a * x[i] + y[i]);
    u[1] = usb16(b * x[i] - y[i]);
  }
}

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
    const int was = eq_mode[t];
    if (!mode && !was) continue;
    float* x = mbuf[t];
    for (uint32_t i = 0; i < n; ++i) pk = fm_maxf(pk, fm_fabsf(x[i]));
    stmlib::Svf* ef = eq_p[t]->f;
    if (pk < 1e-6f) {                   // (silence: the states from zero, never down into the subnormals; the mode
      ef[0].Init();                     // taken at once, nothing to hear)
      ef[1].Init();
      eq_mode[t] = mode;
      continue;
    }
    float f = clampf(eq_hz(v) / kSr, 0.0004f, 0.41f);
    // another mode (0.16.5: the filter taken out or in at once, a click: 23 Hz's high pass to flat, flat to 16 kHz's
    // low pass): the one it was, its states on, into a scratch (the mix's Warps dry: read above, no more this block),
    // the new one from rest (the low pass at the signal's level), then from the one to the other over the block
    float* y = m_dry;
    if (mode != was) {
      if (was == 1) {                   // (its states on, at its cutoff: then from rest)
        ef[0].Process<stmlib::FILTER_MODE_LOW_PASS>(x, y, n);
        ef[1].Process<stmlib::FILTER_MODE_LOW_PASS>(y, y, n);
      } else if (was) {
        ef[0].Process<stmlib::FILTER_MODE_HIGH_PASS>(x, y, n);
        ef[1].Process<stmlib::FILTER_MODE_HIGH_PASS>(y, y, n);
      } else {
        for (uint32_t i = 0; i < n; ++i) y[i] = x[i];
      }
      ef[0].Init();
      ef[1].Init();
      if (mode == 1) ef[0].Prime(x[0]), ef[1].Prime(x[0]);
      eq_mode[t] = mode;
    }
    if (mode) {
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
    if (mode != was) {
      for (uint32_t i = 0; i < n; ++i) {   // (an S: no corner at either end)
        float a = static_cast<float>(i + 1) * inv;
        a = a * a * (3.0f - 2.0f * a);
        x[i] = y[i] + (x[i] - y[i]) * a;
      }
    }
  }
  {                                     // the duck: its source's rises (their frames), DECAY, each track's depth
    float rel = mj.duck_rel;
    uint32_t fr = n / kFrames;
    for (uint32_t i = 0; i < n; ++i) {
      if (i % fr == 0 && mj.hit[i / fr]) duck_d = 1.0f;
      else duck_d *= rel;
      duck_s += (duck_d - duck_s) * 0.03f;
      duckv[i] = duck_s;
    }
    if (duck_s < 1e-5f && duck_d < 1e-5f) duck_s = duck_d = 0.0f;   // (never down into the subnormals: slow)
    for (int t = 0; t < ENG_TRACKS; ++t) {   // (before the sends: what a track sends ducks with it; the source's 0)
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
  int spread = 0;
  for (int t = 0; t < ENG_TRACKS; ++t) {  // (0.16.19) SPREAD: the tracks spread, their sides
    if (mj.sp0[t] <= 0.0f && mj.sp1[t] <= 0.0f) {
      sp_run[t] = 0;                     // (at rest: its line out of mind, silent again when it comes back)
      continue;
    }
    spread_track(t, mbuf[t], n, mj.gl[t], mj.gr[t]);
    spread |= 1 << t;
  }
  if (!usb_now) return;
  for (int t = 0; t < ENG_TRACKS; ++t) {  // the USB input's tracks: the same gains, sample by sample
    if ((spread >> t) & 1) continue;     // (a track spread: its pair written with its side)
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
    int bus = proj.algo[PG_COMP] >= MB_OTT ? static_cast<int>(MB_COMP) : proj.algo[PG_COMP];   // Warps in the mix)
    float us = kWebFx[bus];
    for (int t = 0; t < ENG_TRACKS; ++t) {
      if ((mj.eq[t] <= -kEqDead || mj.eq[t] >= kEqDead) && quiet[t] < kQuiet) us += kWebFxEq;
      if (mj.duck0[t] > 0.0f || mj.duck1[t] > 0.0f) us += kWebFxDuck;
      if (mj.sp0[t] > 0.0f || mj.sp1[t] > 0.0f) us += kWebFxSpread;
    }
    if (fm_fabsf(E(LANE_DELAY, 2)) > 0.01f) us += kWebFxDelay;
    if (mj.dst >= 0 && !mj.idle) us += kWebFxWarps;
    if (proj.algo[PG_REVERB] == RV_METAL || proj.algo[PG_REVERB] == RV_SHIMMER)   // (0.16.41) METAL's over Clouds'
      us += kWebFxMetal[proj.algo[PG_REVERB] - RV_METAL];
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
  mms_process(master, fx_out_l, fx_out_r, n);   // the master bus: the compressor, the limiter
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
float w_ei, w_eo, w_lg;                  // (0.16.31) its level: what goes in and out (followed), the gain given back

void run_task(int i, uint32_t n) {
  uint32_t q = plat_ticks24();
  if (i < ENG_TRACKS) render_voice(i, n);
  else if (i == kTaskWarps) {
#ifdef PLAT_WEB
    int a = eng_warps_algo();
    WEB_SPEND(kWebWarps[proj.warps_hq ? 1 : 0][a < 0 ? 0 : a > 5 ? 5 : a] * static_cast<float>(n) / 128.0f);
#endif
    mw_render(warps, w_car, w_mod, w_din, w_out, w_dry, n);
    // (0.16.31) Warps keeps the level: its output brought to its two inputs' together (what the carrier and the
    // modulator played before it took their place), followed over ~0.2 s: it came out up to 20 dB over a quiet
    // track. Down as far as it takes, up 6 dB at most
    float ei = 0.0f, eo = 0.0f;
    for (uint32_t k = 0; k < n; ++k) ei += w_car[k] * w_car[k] + w_mod[k] * w_mod[k], eo += w_out[k] * w_out[k];
    level_keep(w_out, n, ei, eo, &w_ei, &w_eo, &w_lg, 0.015f, 2.0f);
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

int eng_endless_opt(int t, int o) {
  if (t < 0 || t >= ENG_TRACKS) return 0;
  const uint8_t* b = proj.endless_opt[t];
  switch (o) {
    case EO_STRIDE: { int v = b[0] & 7; return v < 1 ? 1 : v; }
    case EO_REST: { int v = (b[0] >> 3) & 15; return v < 2 ? 0 : v > 8 ? 8 : v; }
    case EO_HOLES: return b[1] > 100 ? 100 : b[1];
    case EO_LEAP: return b[2] > 100 ? 100 : b[2];
    default: return 0;
  }
}
void eng_endless_opt_set(project_t* p, int t, int o, int v) {
  if (t < 0 || t >= ENG_TRACKS) return;
  uint8_t* b = p->endless_opt[t];
  if (o == EO_STRIDE) b[0] = static_cast<uint8_t>((b[0] & ~7u) | static_cast<unsigned>(v < 1 ? 1 : v > 7 ? 7 : v));
  else if (o == EO_REST) b[0] = static_cast<uint8_t>((b[0] & 7u) | static_cast<unsigned>(v < 2 ? 0 : v > 8 ? 8 : v) << 3);
  else if (o == EO_HOLES || o == EO_LEAP) b[o == EO_HOLES ? 1 : 2] = static_cast<uint8_t>(v < 0 ? 0 : v > 100 ? 100 : v);
}

// (0.16.7) CLIMB's options, in Endless' options' spare byte (engine.h)
const char* const kClimbOn[CLON_N] = {"Trig", "Ratch", "Both"};
const char* const kClimbShape[CLSH_N] = {"Ramp", "Bounce", "Random"};
int eng_climb_opt(int t, int o) {
  if (t < 0 || t >= ENG_TRACKS) return 0;
  unsigned b = proj.endless_opt[t][3];
  if (o == CL_TIMES) return (b & 15u) ? static_cast<int>(b & 15u) + 1 : 4;
  if (o == CL_ON) { int v = static_cast<int>((b >> 4) & 3u); return v >= CLON_N ? CLON_TRIG : v; }
  if (o == CL_SHAPE) { int v = static_cast<int>(b >> 6); return v >= CLSH_N ? CLSH_RAMP : v; }
  return 0;
}
void eng_climb_opt_set(project_t* p, int t, int o, int v) {
  if (t < 0 || t >= ENG_TRACKS) return;
  unsigned b = p->endless_opt[t][3];
  if (o == CL_TIMES) b = (b & ~15u) | static_cast<unsigned>((v < 2 ? 2 : v > 16 ? 16 : v) - 1);
  else if (o == CL_ON) b = (b & ~0x30u) | static_cast<unsigned>(v < 0 ? 0 : v >= CLON_N ? CLON_N - 1 : v) << 4;
  else if (o == CL_SHAPE) b = (b & 0x3Fu) | static_cast<unsigned>(v < 0 ? 0 : v >= CLSH_N ? CLSH_N - 1 : v) << 6;
  p->endless_opt[t][3] = static_cast<uint8_t>(b);
}
const char* eng_climb_on_name(int v) { return kClimbOn[v < 0 || v >= CLON_N ? 0 : v]; }
const char* eng_climb_shape_name(int v) { return kClimbShape[v < 0 || v >= CLSH_N ? 0 : v]; }

const char* eng_speed_name(int s) { return kSpeedNames[(s + 4) < 0 ? 0 : (s + 4) >= ENG_SPEEDS ? ENG_SPEEDS - 1 : s + 4]; }
const char* eng_dir_name(int d) { return kDirNames[d < 0 || d >= DIR_N ? 0 : d]; }
int eng_ratch_hits(int r, int* span) {
  r = r < 1 ? 1 : r > ENG_RATCH_MAX ? ENG_RATCH_MAX : r;
  if (span) *span = (r - 1) / 16 + 1;
  return (r - 1) % 16 + 1;
}
int eng_ratch_join(int hits, int span) {
  hits = hits < 1 ? 1 : hits > 16 ? 16 : hits;
  span = span < 1 ? 1 : span > 16 ? 16 : span;
  return hits + 16 * (span - 1);
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
  if (tp == TP_DUCK) return PG_DUCK * 4 + (lane == duck_src() ? 0 : duck_slot(lane));   // (the source's: the duck's
                                                                                        // DECAY, not lockable)
  if (tp == TP_EQ) return PG_EQ * 4 + lane;
  if (tp == TP_CURVE) return PG_CURVE * 4 + lane;
  if (tp == TP_CLIMB) return PG_EUC(lane) * 4 + 2;
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
    l = duck_knob_track(k);
    tp = TP_DUCK;
  } else if (page == PG_EQ) {
    l = k;
    tp = TP_EQ;
  } else if (page == PG_CURVE) {
    l = k;
    tp = TP_CURVE;
  } else if (page >= PG_EUC(0) && page < PG_EUC(ENG_TRACKS) && k == 2) {
    l = page - PG_EUC(0);
    tp = TP_CLIMB;
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
  return tp >= 0 && tp < eng_lane_params(lane) && !(lane == duck_src() && tp == TP_DUCK) && !(lane < ENG_TRACKS && tp == TP_FTYPE) &&
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

int eng_voc_play(int param) { return voc_play_param(param); }
int eng_voc_chord(int t, float* out) {  // (0.16.28) VOCODER's chord now (Manual's, a progression's): its notes
  if (t < 0 || t >= ENG_TRACKS || track_kind(t) != MK_VOCODER || voc_pl(t) <= 0) return 0;
  for (int i = 0; i < vc_n[t]; ++i) out[i] = vc_ch[t][i];
  return vc_n[t];
}

MI_COLD const char* eng_param_name(int param) {
  if (voc_play_param(param)) return "Play";
  int lk = eng_lx_knob(param);
  if (lk >= 0) {
    int t = param / 12;
    return zero_on(t) ? zero_knob_name(lk) : track_kind(t) == MK_DRUM ? drum_knob_name(lk)
                                           : track_kind(t) == MK_SDS ? sds_knob_name(lk)
                                           : track_kind(t) == MK_FM ? fm4_knob_name(lk)
                                           : track_kind(t) == MK_PADS ? pads_knob_name(lk)
                                           : track_kind(t) == MK_POLY ? poly_knob_name(lk)
                                           : track_kind(t) == MK_PROPHET ? pro_knob_name(lk)
                                           : track_kind(t) == MK_CHOIR ? choir_knob_name(lk)
                                           : track_kind(t) == MK_TAURUS ? taurus_knob_name(lk)
                                           : track_kind(t) == MK_VOCODER ? voc_knob_name(lk)
                                           : track_kind(t) == MK_STATIC ? static_knob_name(lk, static_mode_u(t))
                                                                     : lx_knob_name(lx_model(t), lk);
  }
  if (param >= PG_LFO(0) * 4 && param < PG_LFO(3) * 4 && param % 4 && Pi(param / 4, 0) == LW_FOLLOW) {
    static const char* const kF[4] = {"Wave", "Attack", "Mode", "Decay"};   // (0.16.23) FOLLOW's, in the LFO's places
    return kF[param % 4];
  }
  if (param >= 0 && param < 12 * 4 && (param / 4) % 3 == 0 && param % 4 >= 1) {
    int m = track_sub(param / 12);
    return kModelKnobs[m < 0 || m > 23 ? 0 : m][param % 4 - 1];
  }
  return eng_pdef(param)->name;
}

// (0.16.16) a knob drawn from its middle (its - +): a bipolar parameter's, a negative range's; a machine's own knobs say
// where theirs start (PADS' TEXTURE from the left on a bipolar place; TAURUS' CONTOUR, VOCODER's SHIFT from the middle;
// CHOIR's all from the left)
int eng_param_centered(int param) {
  const eng_pdef_t* d = eng_pdef(param);
  const int c = d->kind == K_BIP || (d->kind == K_INT && d->lo < 0);
  const int lk = eng_lx_knob(param);
  if (lk < 0 || zero_on(param / 12)) return c;
  const int k = track_kind(param / 12);
  if (k == MK_PADS && lk == 7) return 0;
  if (k == MK_TAURUS) return lk == 5;   // (CONTOUR: a swell to the left, its depth to the right)
  if (k == MK_VOCODER) return lk == 6;  // (SHIFT: the voice lower or higher)
  if (k == MK_CHOIR) return 0;
  if (k == MK_SDS) return lk == 7;      // (0.16.49: BEND down or up; HIT, DRIVE from the left)
  return c;
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
int eng_model_free(int t, int m) {
  if (mdef(m).kind != MK_PADS) return 1;
  for (int u = 0; u < ENG_TRACKS; ++u)
    if (u != t && mdef(proj.algo[PG_TRACK(u, 0)]).kind == MK_PADS) return 0;
  return 1;
}
int eng_pads_track(void) { return pad_track; }
int eng_choir_parts(int t, float note, int nv, float* out) {   // (0.16.43: the tests')
  return t >= 0 && t < ENG_TRACKS ? choir_parts(t, note, nv, out) : 0;
}
int eng_duck_track(int k) { return k <= 0 ? duck_src() : duck_knob_track(k); }   // (0.16.42)
int eng_lead_ok(int t) {                // (0.16.42) a track that plays chords: SCALE can follow it
  const int k = t >= 0 && t < ENG_TRACKS ? track_kind(t) : -1;
  return k == MK_PADS || k == MK_POLY || k == MK_PROPHET || k == MK_VOCODER;
}
int eng_scale_step(int sc, int e) {     // (0.16.42) SCALE turned e detents, past FOLLOW's tracks that play no chords
  const int d = e > 0 ? 1 : -1;
  for (int i = 0; i < (e < 0 ? -e : e); ++i) {
    int n = sc + d;
    while (n >= kScaleT1 && n < kScales && !eng_lead_ok(n - kScaleT1)) n += d;
    if (n < 0 || n >= kScales) break;
    sc = n;
  }
  return sc;
}
int eng_pads_pos(void) { return pad_track >= 0 ? pad_pos : -1; }
// a chord's name from its notes (semitones, its root first; pc0: the pitch class of 0): its root, its quality (m, dim,
// +, sus2, sus4, 5), its extension (7, Maj7, 9, 11, add9, 6, b6); a note none of them explains: + (0.16.12)
MI_COLD void chord_name(const int* notes, int nn, int pc0, char* b, int n) {
  if (n <= 0) return;
  b[0] = 0;
  if (nn < 1) return;
  const int r = notes[0];
  uint32_t has = 0;                     // (bit i: a note i semitones over the root, 0..23)
  for (int i = 1; i < nn; ++i) {
    int d = notes[i] - r;
    while (d < 0) d += 12;
    if (d < 24) has |= 1u << d;
  }
  auto h = [&](int d) { return (has >> d) & 1u; };
  uint32_t used = 0;
  auto use = [&](int d) { used |= 1u << d; };
  char t[20];
  int k = 0;
  auto put = [&](const char* q) { while (*q && k < 18) t[k++] = *q++; };
  put(kNoteName[((pc0 + r) % 12 + 12) % 12]);
  int maj = h(4), mnr = !maj && h(3), sus2 = !maj && !mnr && h(2), sus4 = !maj && !mnr && !sus2 && h(5);
  if (maj) use(4);
  if (mnr) use(3);
  if (sus2) use(2);
  if (sus4) use(5);
  if (h(7)) use(7);
  if (h(12)) use(12);
  int sev = h(10) ? 10 : h(11) ? 11 : 0, ninth = h(14), eleventh = h(17);
  if (mnr && !h(7) && h(6)) put("dim"), use(6);
  else if (maj && !h(7) && h(8)) put("+"), use(8);
  else if (mnr) put("m");
  else if (sus2) put("sus2");
  else if (sus4) put("sus4");
  else if (!maj && !sev && !ninth && !eleventh && h(7)) put("5");
  if (eleventh && sev) {
    put(sev == 11 ? "Maj11" : "11");
    use(sev), use(14), use(17);
  } else if (ninth && sev) {
    put(sev == 11 ? "Maj9" : "9");
    use(sev), use(14);
  } else if (sev) {
    put(sev == 11 ? "Maj7" : "7");
    use(sev);
  } else if (ninth) {
    put("add9");
    use(14);
  } else if (h(9)) {
    put("6");
    use(9);
  } else if (h(8) && h(7)) {
    put("b6");
    use(8);
  }
  if (has & ~used) put("+");            // (a note more: Tintin's voice, an added degree)
  t[k] = 0;
  for (k = 0; t[k] && k < n - 1; ++k) b[k] = t[k];
  b[k] = 0;
}
void eng_pads_chord(char* b, int n) {   // PADS' chord now
  if (n <= 0) return;
  b[0] = 0;
  if (pad_track < 0 || pad_n < 3) return;
  chord_name(pad_chord, pad_n, Pi(PG_GLOBAL, 2), b, n);
}
void eng_chord_name(const int* notes, int nn, char* b, int n) {   // (a step's: its notes over an octave folded in,
  int f[4], m = 0;                                                  // its ninth, eleventh and thirteenth kept)
  nn = nn > 4 ? 4 : nn;
  for (int i = 0; i < nn; ++i) {
    int d = notes[i] - notes[0];
    while (d >= 24) d -= 12;
    if (d >= 12 && d != 14 && d != 17 && d != 21) d -= 12;
    if (!i || d) f[m++] = notes[0] + d;  // (the root's octaves: the root)
  }
  chord_name(f, m, 0, b, n);
}
MI_COLD void eng_pads_line(char* b, int n) {    // the progression, its place, the chord: "Tintin 3/8 Am"
  if (n <= 0) return;
  b[0] = 0;
  const int t = pad_track;
  if (t < 0) return;
  char c[16], tmp[48];
  const int prog = pads_prog(E(t, 1));
  pads_knob_text(0, E(t, 1), tmp, 24);
  int k = 0;
  while (tmp[k]) ++k;
  if (prog) {
    int len = pads_prog_len(prog), pos = pad_pos < 0 ? 0 : pad_pos % len;
    tmp[k++] = ' ';
    if (pos + 1 >= 10) tmp[k++] = static_cast<char>('0' + (pos + 1) / 10);
    tmp[k++] = static_cast<char>('0' + (pos + 1) % 10);
    tmp[k++] = '/';
    if (len >= 10) tmp[k++] = static_cast<char>('0' + len / 10);
    tmp[k++] = static_cast<char>('0' + len % 10);
  }
  eng_pads_chord(c, sizeof c);
  if (c[0]) {
    tmp[k++] = ' ';
    for (int i = 0; c[i] && k < 46; ++i) tmp[k++] = c[i];
  }
  tmp[k] = 0;
  for (k = 0; tmp[k] && k < n - 1; ++k) b[k] = tmp[k];
  b[k] = 0;
}
extern "C" void* mf_state(int t) { return mstate[t < 0 ? 0 : t >= ENG_TRACKS ? ENG_TRACKS - 1 : t]; }
extern "C" void mf_forget(void) {
  for (int t = 0; t < ENG_TRACKS; ++t) mowner[t] = 0xFF;
}
extern "C" void* mf_voice_mem(int t, uint32_t* bytes, uint32_t* epoch) {
  t = t < 0 ? 0 : t >= ENG_TRACKS ? ENG_TRACKS - 1 : t;
  if (track_kind(t) != MK_VOCODER || !voice[t]) {   // (only the machine on the track)
    *bytes = 0;
    *epoch = vepoch[t];
    return nullptr;
  }
  vlent[t] = 1;
  *bytes = mp_size() & ~7u;
  *epoch = vepoch[t];
  return voice[t];
}
int eng_fm_algo(int t) { return t >= 0 && t < ENG_TRACKS && track_kind(t) == MK_FM ? fm4_algo(E(t, 1)) : -1; }
int eng_fm_shaped(int t) { return eng_fm_algo(t) >= 0 && fm4_shape((E(t, 6) + 1.0f) * 0.5f) ? fm4_modulators(eng_fm_algo(t)) : 0; }
float eng_fm_shape(int t, int op, float x) {
  return eng_fm_algo(t) >= 0 ? fm4_shape_level((E(t, 6) + 1.0f) * 0.5f, op, x) : 0.0f;
}
int eng_model_own(int m) { return mdef(m).kind != MK_PLAITS; }
int eng_model_find(int kind, int sub) {
  for (int m = 0; m < ENG_MODELS; ++m)
    if (kModelDef[m].kind == kind && kModelDef[m].sub == sub) return m;
  return -1;
}

MI_COLD float eng_lx_step(int param, float v, int detents) {
  if (voc_play_param(param)) {          // (VOCODER's PLAY: a choice a detent; Mono at the left end)
    int c = voc_play(v) + detents;
    c = c < -1 ? -1 : c > 27 ? 27 : c;
    return c < 0 ? -1.0f : static_cast<float>(c) * (1.0f / 27.0f);
  }
  int lk = eng_lx_knob(param), bip = eng_pdef(param)->kind == K_BIP;
  if (lk < 0) return v;
  int t = param / 12;
  int n = zero_on(t) ? zero_knob_steps(lk) : track_kind(t) == MK_DRUM || track_kind(t) == MK_SDS ? 128
                                            : track_kind(t) == MK_FM ? fm4_knob_steps(lk)
                                            : track_kind(t) == MK_PADS ? pads_knob_steps(lk)
                                            : track_kind(t) == MK_POLY ? poly_knob_steps(lk)
                                            : track_kind(t) == MK_PROPHET ? pro_knob_steps(lk)
                                            : track_kind(t) == MK_CHOIR ? choir_knob_steps(lk)
                                            : track_kind(t) == MK_TAURUS ? taurus_knob_steps(lk)
                                            : track_kind(t) == MK_VOCODER ? voc_knob_steps(lk)
                                            : track_kind(t) == MK_STATIC ? static_knob_steps(lk, static_mode_u(t))
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

// (0.16.27) STATIC's MODE turned to another generator: its other seven knobs that generator's (ARC/01's presets)
MI_COLD void eng_static_mode(int param, float before) {
  const int t = param / 12;
  if (param < 0 || t >= ENG_TRACKS || track_kind(t) != MK_STATIC || param != lx_param(t, 0)) return;
  const int m = static_mode(static_mode_u(t));
  if (m == static_mode(lx_u(param, before))) return;
  float k[LX_KNOBS];
  static_mode_defaults(m, k);
  for (int i = 1; i < LX_KNOBS; ++i) {
    const int q = lx_param(t, i);
    proj.val[q] = eng_pdef(q)->kind == K_BIP ? 2.0f * k[i] - 1.0f : k[i];
  }
}

MI_COLD void eng_set_model(int t, int m) {
  if (t < 0 || t >= ENG_TRACKS || m < 0 || m >= ENG_MODELS) return;
  int was = proj.algo[PG_TRACK(t, 0)];
  proj.algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(m);
  if (eng_model_own(m) && m != was) lx_kit(&proj, t);
  if (mdef(m).kind != mdef(was).kind)   // (0.16.40: another family: its steps' models gone)
    for (int i = 0; i < ENG_PLOCKS; ++i)
      if (proj.lock[i].lane == t && proj.lock[i].tp == kTpModel) proj.lock[i].lane = 0xFF, ++lock_gen;
}
int eng_model_lockable(int t, int m) {   // (0.16.40) 1: m may be a step's model on track t (its family's)
  if (t < 0 || t >= ENG_TRACKS || m < 0 || m >= ENG_MODELS) return 0;
  const int k = mdef(proj.algo[PG_TRACK(t, 0)]).kind;
  return family(k) && mdef(m).kind == k;
}
int eng_model_lock(int lane, int step) {   // a step's model, -1 none
  if (lane < 0 || lane >= ENG_TRACKS) return -1;
  int i = lock_find(lane, step, kTpModel);
  return i >= 0 ? proj.lock[i].v : -1;
}
int eng_model_lock_set(int lane, int step, int m) {   // 0 ok, -1 no room or not its family; m -1 or the track's: none
  if (lane < 0 || lane >= ENG_TRACKS || step < 0 || step >= ENG_STEPS) return -1;
  int i = lock_find(lane, step, kTpModel);
  if (m < 0 || m == proj.algo[PG_TRACK(lane, 0)]) {
    if (i >= 0) proj.lock[i].lane = 0xFF, ++lock_gen;
    return 0;
  }
  if (!eng_model_lockable(lane, m)) return -1;
  if (i < 0)
    for (int j = 0; j < ENG_PLOCKS && i < 0; ++j)
      if (proj.lock[j].lane == 0xFF) i = j;
  if (i < 0) return -1;
  proj.lock[i].v = static_cast<uint8_t>(m);
  proj.lock[i].step = static_cast<uint8_t>(step);
  proj.lock[i].tp = static_cast<uint8_t>(kTpModel);
  proj.lock[i].lane = static_cast<uint8_t>(lane);
  ++lock_gen;
  return 0;
}

// the presets (0.15.1): a track's sound
static void sound_name(eng_sound_t* s, const char* n) {
  int i = 0;
  for (; n && n[i] && i < static_cast<int>(sizeof s->name) - 1; ++i) s->name[i] = n[i];
  for (; i < static_cast<int>(sizeof s->name); ++i) s->name[i] = 0;
}
static int sound_tp(int t, int i, int k) {   // the random of that page's knob k (-1: none)
  int lane = -1, tp = eng_param_tp(snd_page(t, i) * 4 + k, &lane);
  return lane == t && tp >= 0 && tp < ENG_NTP ? tp : -1;
}
MI_COLD void eng_sound_get(int t, eng_sound_t* s) {
  if (t < 0 || t >= ENG_TRACKS) t = 0;
  uint8_t* raw = reinterpret_cast<uint8_t*>(s);
  for (uint32_t i = 0; i < sizeof *s; ++i) raw[i] = 0;
  s->magic = SND_MAGIC;
  s->version = SND_VERSION;
  s->size = sizeof *s;
  for (int i = 0; i < SND_PAGES; ++i) {
    int pg = snd_page(t, i);
    s->algo[i] = proj.algo[pg];
    for (int k = 0; k < 4; ++k) {
      s->val[i * 4 + k] = proj.val[pg * 4 + k];
      int tp = sound_tp(t, i, k);
      if (tp >= 0) s->rnd[i * 4 + k] = proj.rnd[t][tp], s->rseed[i * 4 + k] = proj.rseed[t][tp];
    }
  }
  sound_name(s, mdef(proj.algo[PG_TRACK(t, 0)]).name);
}
int eng_sound_check(const eng_sound_t* s, int bytes) {
  if (bytes < static_cast<int>(sizeof *s) || s->magic != SND_MAGIC || s->version < 1 || s->version > SND_VERSION ||
      s->size != sizeof *s)             // (1, 2, 3: before 0.16.14, 0.16.14 to 0.16.16, 0.16.17: read as this one)
    return 0;
  for (int i = 0; i < SND_PAGES * 4; ++i)
    if (!(s->val[i] > -1000.0f && s->val[i] < 1000.0f)) return 0;   // (a NaN fails both)
  return 1;
}
MI_COLD int eng_sound_set(int t, const eng_sound_t* s) {
  if (t < 0 || t >= ENG_TRACKS || !eng_sound_check(s, sizeof *s)) return -1;
  int m = s->algo[0], was_plaits = 0;
  if (s->version == 1 && m >= 15) {     // (before 0.16.14: CHIPTUNE 15, FORMANT 16 now SQ-1 from its kit; the others one
    was_plaits = m <= 16;               // lower)
    m = was_plaits ? kOtis34 : m - 1;
  } else if (s->version == 3 && m == 16) {   // (0.16.17's ROBOVOX, withdrawn: SPEECH again, at noon)
    was_plaits = 1;
  }
  if (s->version <= 4 && m >= 16) ++m;   // (0.16.20: TAURUS at 16, the models from there one further on)
  if (s->version <= 5 && m >= 17) ++m;   // (0.16.21: VOCODER at 17, the same)
  if (s->version <= 6 && m >= 18) ++m;   // (0.16.24: STATIC at 18, the same)
  if (s->version <= 7) {                // (0.16.43: FM CYMBAL, FM HI-HAT, OTIS gone, CHOIR at 15: models_v35's)
    was_plaits |= m == 8 || m == 9 || m == kOtis34;
    m = m >= 0 && m < kModels34 ? kFrom34[m] : 0;
  }
  if (s->version <= 8 && m >= kSdsAt) m += kSdsN;   // (0.16.49: the SDS at 8, the models from there four further on)
  m = m >= 0 && m < ENG_MODELS ? m : 0;
  if (!eng_model_free(t, m)) return -2;   // (PADS: once a project)
  for (int i = 0; i < SND_PAGES; ++i) {
    int pg = snd_page(t, i);
    for (int k = 0; k < 4; ++k) {
      proj.val[pg * 4 + k] = s->val[i * 4 + k];
      int tp = sound_tp(t, i, k);
      if (tp >= 0) proj.rnd[t][tp] = s->rnd[i * 4 + k], proj.rseed[t][tp] = s->rseed[i * 4 + k];
    }
    if (i == 0) proj.algo[pg] = static_cast<uint8_t>(m);
    else if (i == 4) proj.algo[pg] = static_cast<uint8_t>(s->algo[i] < MF_MODELS ? s->algo[i] : MF_OFF);
    else proj.algo[pg] = s->algo[i];
  }
  if (was_plaits) {                     // (its knobs its kit's, SQ-1's or SPEECH's noon; their randoms none)
    lx_kit(&proj, t);
    for (int k = 0; k < LX_KNOBS; ++k) proj.rnd[t][1 + k] = 0;
  }
  return 0;
}
const char* eng_factory_name(int i) { return i >= 0 && i < ENG_FACTORY ? kFactory[i].name : ""; }
MI_COLD int eng_factory(int i, eng_sound_t* s) {
  if (i < 0 || i >= ENG_FACTORY) return -1;
  const FactorySound& f = kFactory[i];
  static const float kTrack[3][4] = {{0.0f, 0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f, 0.0f}};
  float kit[LX_KNOBS];
  int m = eng_model_find(f.kind, f.sub);
  uint8_t* raw = reinterpret_cast<uint8_t*>(s);
  for (uint32_t b = 0; b < sizeof *s; ++b) raw[b] = 0;
  s->magic = SND_MAGIC;
  s->version = SND_VERSION;
  s->size = sizeof *s;
  for (int pg = 0; pg < 3; ++pg)       // (eng_blank's track)
    for (int k = 0; k < 4; ++k) s->val[pg * 4 + k] = kTrack[pg][k];
  s->algo[0] = static_cast<uint8_t>(m < 0 ? 0 : m);
  kit_of(s->algo[0], kit);
  for (int k = 0; k < LX_KNOBS; ++k) {   // (its knobs: the second of the first page on, as lx_param)
    float u = f.k[k] < 0.0f ? kit[k] : f.k[k];
    s->val[1 + k] = eng_pdef(lx_param(0, k))->kind == K_BIP ? 2.0f * u - 1.0f : u;
  }
  s->val[0] = f.freq;
  s->val[9] = f.gate;
  s->val[10] = f.transp;
  s->val[11] = f.octave;
  for (int k = 0; k < 4; ++k) s->val[12 + k] = f.bits[k], s->val[28 + k] = f.glitch[k];
  for (int k = 0; k < 12; ++k) s->val[16 + k] = f.f[k];
  s->algo[4] = f.filt;
  sound_name(s, f.name);
  return 0;
}

const eng_pdef_t* eng_pdef(int param) {   // (a table, made once: the render asks it hundreds of times a block)
  static const eng_pdef_t* table[ENG_NPARAMS];
  if (param < 0 || param >= ENG_NPARAMS) param = 0;
  if (param == PG_DELAY(0) * 4 && proj.algo[PG_DELAY(0)] == DL_FREE) return &kDelayFreeTime;   // (FREE: in ms)
  if (param >= PG_REVERB * 4 && param < PG_REVERB * 4 + 3 && proj.algo[PG_REVERB] != RV_CLOUDS)   // (METAL's, SHIMMER's)
    return &kMetalKnobs[param - PG_REVERB * 4];
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

MI_COLD void eng_param_text(int param, float v, char* b, int n) {
  const eng_pdef_t* d = eng_pdef(param);
  int pt = page_type(param / 4), k = param % 4, lk = eng_lx_knob(param);
  if (voc_play_param(param)) {          // (0.16.28) VOCODER's PLAY: Mono, Poly, Manual, a progression's name
    const int c = voc_play(v);
    put(b, n, c < 0 ? "Mono" : c ? pads_prog_name(c - 1) : "Poly");
  } else if (lk >= 0 && zero_on(param / 12)) {   // ZERO's: as it writes them (an index, a ratio, Hz, %)
    zero_knob_text(lk, lx_u(param, v), b, n);
  } else if (lk >= 0 && track_kind(param / 12) == MK_DRUM) {   // a drum's (times, Hz, semitones)
    drum_knob_text(track_sub(param / 12), lk, lx_u(param, v), b, n);
  } else if (lk >= 0 && track_kind(param / 12) == MK_SDS) {   // the SDS' (Hz, the balance, times, semitones)
    sds_knob_text(lk, lx_u(param, v), b, n);
  } else if (lk >= 0 && track_kind(param / 12) == MK_FM) {     // the FM's (an algorithm, a voicing, cents, times, %)
    fm4_knob_text(lk, lx_u(param, v), b, n);
  } else if (lk >= 0 && track_kind(param / 12) == MK_PADS) {   // PADS' (a progression, a color, a voicing, times, %)
    pads_knob_text(lk, lx_u(param, v), b, n);
  } else if (lk >= 0 && track_kind(param / 12) == MK_POLY) {   // POLY's (a wave, Hz, a chord, times, %)
    poly_knob_text(lk, lx_u(param, v), b, n);
  } else if (lk >= 0 && track_kind(param / 12) == MK_PROPHET) {   // PROPHET's (its oscillators, Hz, a chord...)
    pro_knob_text(lk, lx_u(param, v), b, n);
  } else if (lk >= 0 && track_kind(param / 12) == MK_CHOIR) {   // CHOIR's (a vowel, its parts, %, times, cents)
    choir_knob_text(lk, lx_u(param, v), b, n);
  } else if (lk >= 0 && track_kind(param / 12) == MK_TAURUS) {   // TAURUS' (a preset or a wave, cents, Hz, times)
    taurus_knob_text(lk, lx_u(param, v), b, n);
  } else if (lk >= 0 && track_kind(param / 12) == MK_VOCODER) {   // VOCODER's (a track, a tone, times, bands)
    voc_knob_text(lk, lx_u(param, v), b, n);
  } else if (lk >= 0 && track_kind(param / 12) == MK_STATIC) {   // STATIC's (its mode's: Hz, ms, rad, %)
    static_knob_text(lk, lx_u(param, v), static_mode_u(param / 12), b, n);
  } else if (lk >= 0) {                   // an FM drum's: as the LXR-02 shows it (0..127, a wave)
    lx_knob_text(lx_model(param / 12), lk, lx_u(param, v), b, n);
  } else if (pt == PT_WARPS1 && k == 0 && i_of(v) >= WC_OSC1) {   // the oscillator's shape, as the algorithm uses it
    int o = i_of(v) > WC_OSC3 ? 2 : i_of(v) - WC_OSC1;
    put(b, n, Pi(PG_WARPS(1), 3) == WM_SHIFT ? kWarpsCarrier[WC_OSC1 + o] : kOscXmod[o]);
  } else if (pt == PT_LFO && k && Pi(param / 4, 0) == LW_FOLLOW) {   // (0.16.23) FOLLOW's ATTACK, MODE, DECAY
    if (k == 2) {
      static const char* const kM[6] = {"Follow", "Trig", "T1", "T2", "T3", "T4"};
      const int m = i_of(v);
      put(b, n, kM[m < 0 ? 0 : m > 5 ? 5 : m]);
    } else {
      const float t = k == 1 ? fol_att(v) : fol_dec(v);
      if (t < 1.0f) {
        put_num(b, n, static_cast<int32_t>(t * (t < 0.01f ? 10000.0f : 1000.0f) + 0.5f), t < 0.01f ? 1 : 0, 0);
        put_end(b, n, " ms");
      } else {
        put_num(b, n, static_cast<int32_t>(t * 100.0f + 0.5f), 2, 0);
        put_end(b, n, " s");
      }
    }
  } else if (pt == PT_FOLLOW && k == 1) {   // GAIN: dB
    put_num(b, n, static_cast<int32_t>(v * 24.0f * 10.0f + (v < 0 ? -0.5f : 0.5f)), 1, 1);
    put_end(b, n, " dB");
  } else if (pt == PT_FOLLOW && k == 2) {   // THRESH: dB
    put_num(b, n, static_cast<int32_t>(fol_db(v) + (v < 1.0f ? -0.5f : 0.5f)), 0, 1);
    put_end(b, n, " dB");
  } else if (pt == PT_FOLLOW && k == 3) {   // FILTER: off, LP or HP at its cutoff
    if (v > -0.02f && v < 0.02f) {
      put(b, n, "Off");
    } else {
      const float hz = fol_hz(v);
      put(b, n, v < 0 ? "LP " : "HP ");
      if (hz < 1000.0f) {
        put_num(b + 3, n - 3, static_cast<int32_t>(hz + 0.5f), 0, 0);
      } else {
        put_num(b + 3, n - 3, static_cast<int32_t>(hz * 0.01f + 0.5f), 1, 0);
        put_end(b, n, "k");
      }
    }
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
  } else if (pt == PT_STEP && k == 2) {   // RATCH: its hits (0.16.4: SPAN, the steps they take, on the PLAY face)
    put(b, n, "x");
    put_num(b + 1, n - 1, eng_ratch_hits(iround(v), 0), 0, 0);
  } else if (pt == PT_EUC && k == 2) {   // CLIMB (0.16.7): semitones, or (SCALE on) the scale's notes
    int32_t c = static_cast<int32_t>(v + (v < 0 ? -0.5f : 0.5f));
    if (!c) {
      put(b, n, "Off");
    } else {
      put_num(b, n, c, 0, 1);
      int l = 0;
      while (b[l]) ++l;
      put(b + l, n - l, scale_on() ? " degrees" : " semitones");   // (the knob: the number in its ring)
    }
  } else if (d->kind == K_INT) {
    put_num(b, n, static_cast<int32_t>(v + (v < 0 ? -0.5f : 0.5f)), 0, d->lo < 0);
  } else if (pt == PT_EUC && k == 3) {   // SPREAD (0.16.19): Off, then how wide
    int32_t q = static_cast<int32_t>(v * 100.0f + 0.5f);
    if (q <= 0) put(b, n, "Off");
    else put_num(b, n, q, 0, 0);
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
  } else if (pt == PT_COMP && k == 3) {   // its gain: +/-12 dB
    put_num(b, n, static_cast<int32_t>(v * 12.0f + (v < 0 ? -0.5f : 0.5f)), 0, 1);
    put_end(b, n, " dB");
  } else if (pt == PT_CURVE) {          // (0.16) the ratchets' CURVE: closer (a bounce), apart, or even
    int c = static_cast<int>(v * 100.0f + (v < 0 ? -0.5f : 0.5f));
    if (c == 0) {
      put(b, n, "Even");
    } else {
      put_num(b, n, c < 0 ? -c : c, 0, 0);
      put_end(b, n, c > 0 ? "% Fast" : "% Slow");
    }
  } else if (pt == PT_PLAITS3 && k == 1) {   // LENGTH: the gate, in % of the sixteenth
    put_num(b, n, static_cast<int32_t>(gate_frac(v) * 100.0f + 0.5f), 0, 0);
    put_end(b, n, "%");
  } else if (pt == PT_GLOBAL && k == 3) {    // SWING: the on-beat's share of two sixteenths
    put_num(b, n, static_cast<int32_t>(50.0f + 25.0f * v + 0.5f), 0, 0);
    put_end(b, n, "%");
  } else if ((pt == PT_STEP && k == 1) || (pt == PT_ARP && k == 1) || (pt == PT_LANE && (k == 1 || k == 2)) ||
             (pt == PT_DUCK && k > 0) || (pt == PT_GLITCH && (k == 0 || k == 3)) ||
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
  } else if (pt == PT_REVERB && k < 3 && proj.algo[PG_REVERB] != RV_CLOUDS) {   // METAL's: DECAY in s (Freeze at
    if (k == 0 && mifm1::MetalVerb::Frozen(v)) {                                  // its end), SIZE in ms, TONE
      put(b, n, "Freeze");                                                        // from the middle (-100 the
    } else if (k == 0) {                                                          // darkest, +100 the metal)
      const float t = mifm1::MetalVerb::Decay(v);
      put_num(b, n, static_cast<int32_t>(t < 10.0f ? t * 10.0f + 0.5f : t + 0.5f), t < 10.0f ? 1 : 0, 0);
      put_end(b, n, " s");
    } else if (k == 1) {
      const float ms = mifm1::MetalVerb::SizeMs(v);
      put_num(b, n, static_cast<int32_t>(ms < 10.0f ? ms * 10.0f + 0.5f : ms + 0.5f), ms < 10.0f ? 1 : 0, 0);
      put_end(b, n, " ms");
    } else {
      const float c = (v - 0.5f) * 200.0f;
      put_num(b, n, static_cast<int32_t>(c + (c < 0.0f ? -0.5f : 0.5f)), 0, 1);
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
    case PT_REVERB: return RV_ALGOS;
    case PT_COMP: return 2;
    default: return 0;
  }
}

MI_COLD const char* eng_algo_name(int page, int a) {
  int n = eng_algo_count(page);
  if (!n) return "";
  a = a < 0 ? 0 : a >= n ? n - 1 : a;
  if (page_type(page) == PT_FILT1) return mf_model_name(a);
  if (page_type(page) == PT_REVERB) return kReverbAlgos[a];
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

MI_COLD void eng_blank(project_t* p) {
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
  // the duck: off (its DECAY ~180 ms); the master bus: a gentle compressor (-10 dB, 3:1, 160 ms, +4 dB)
  p->val[PG_DUCK * 4 + 0] = 0.45f;
  static const float kComp[4] = {0.28f, 0.32f, 0.5f, 4.0f / 12.0f};
  for (int k = 0; k < 4; ++k) p->val[PG_COMP * 4 + k] = kComp[k];
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
MI_COLD void eng_defaults(project_t* p) {
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
  p->val[PG_TRACK(1, 1) * 4 + 2] = 2.0f * 9.5f / 15.0f - 1.0f;   // (SHAPE: Pluck)
  p->val[PG_TRACK(1, 1) * 4 + 3] = 0.3f;
  p->val[PG_TRACK(1, 2) * 4 + 1] = 0.3f;                 // short notes
  p->val[PG_TRACK(2, 0) * 4 + 1] = 3.5f / 8.0f;          // the pluck: the fourth algorithm, its 1 at x3, a sharp pluck
  p->val[PG_TRACK(2, 0) * 4 + 2] = 6.5f / 30.0f;
  p->val[PG_TRACK(2, 1) * 4 + 0] = 0.5f;
  p->val[PG_TRACK(2, 1) * 4 + 2] = 2.0f * 10.5f / 15.0f - 1.0f;   // (SHAPE: Hard)
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
// the off-beats with a riff of locks, the hat on every sixteenth (its decay and pan new at each, an open one on the
// off-beats; 0.16.43: MIFM1's HAT, the FM HI-HAT gone), dub stabs of minor sevenths (the FM's PAD) through SEM (S&H on their brightness, a smooth random on
// the cutoff) thrown into a dotted-eighth ping-pong and the reverb; F minor, 124 BPM, a gentle compressor on the bus.
// Light on purpose: about half of each of the FM-1's cores (the machine page), room left for what is added
MI_COLD void eng_groove(project_t* p) {
  eng_blank(p);
  const int kModel[4] = {eng_model_find(MK_DRUM, 0), eng_model_find(MK_FM, 0), eng_model_find(MK_DRUM, 3),
                         eng_model_find(MK_FM, 0)};   // kick, the FM (a bass), the hat, the FM (chords)
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
  p->val[PG_TRACK(1, 1) * 4 + 2] = 2.0f * 9.5f / 15.0f - 1.0f;   // SHAPE: Pluck
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
  p->val[PG_TRACK(3, 1) * 4 + 2] = 2.0f * 9.5f / 15.0f - 1.0f;   // SHAPE: Pluck
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
  set_lock(p, 2, 2, 5, 0.5f);                            // the open hats, on the off-beats (N.DECAY, the HAT's
  set_lock(p, 2, 6, 5, 0.5f);                            // fifth knob)
  set_lock(p, 2, 10, 5, 0.5f);
  set_lock(p, 2, 14, 5, 0.6f);
  set_lock(p, 0, 12, TP_RATCHET, 2.0f);                  // the kick's double, its last of the bar
  set_lock(p, 3, 11, TP_NOTE, -2.0f);                    // the stabs: their second one a step down
  p->rnd[1][TP_RATCHET] = 12;                            // the bass: a ratchet now and then
  p->rnd[2][5] = 20;                                     // the hats: their decay and pan new at each
  p->rnd[2][TP_PAN] = 30;
  p->val[PG_STEP(2) * 4 + 1] = 0.9f;                     // (and 10 % of them left out)
  ++lock_gen;
}

// the stress test (GLO, SELECT: PROJECT, K1, REC held): a fixed patch that keeps everything busy, to read the
// machine page against, version after version: four tracks playing (a kick, the FM synced on a square, the FM in PW
// on a square, three operators heard, a chord: the FM at its heaviest, MODAL), two of them bent, Warps (diode ring, T3 by T2), the compressor, the
// delay and the reverb fed, the duck, the filters, the three LFOs on 18 cables, randoms, locks, ratchets
MI_COLD void eng_stress(project_t* p) {
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
  p->algo[PG_COMP] = MB_COMP;                            // the master bus: the compressor (0.16.43: OTT gone)
  static const float kComp[4] = {0.45f, 0.5f, 0.4f, 4.0f / 12.0f};
  for (int k = 0; k < 4; ++k) p->val[PG_COMP * 4 + k] = kComp[k];
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
  cable(p, 4, 1, SRC_L3, PG_DELAY(1) * 4 + 2, 30);       // reverb's time, the compressor's threshold
  cable(p, 4, 2, SRC_L2, PG_WARPS(1) * 4 + 0, 40);
  cable(p, 4, 3, SRC_L3, PG_REVERB * 4 + 0, 20);
  cable(p, 4, 4, SRC_L1, PG_COMP * 4 + 0, 20);
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

// the stress test on the FM drums (GLO, SELECT: PROJECT, the fifth): the same patch, the cables and the effects as
// they are, the tracks on the drum, the snare, the drum, the snare (their kits, at their own pitch; 0.16.43: the
// cymbal and the hi-hat gone): what the LXR-02's voices cost against Plaits', on the machine page
MI_COLD void eng_stress_fm(project_t* p) {
  eng_stress(p);
  for (int t = 0; t < ENG_TRACKS; ++t) {
    p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(eng_model_find(MK_LXR, t & 1));
    lx_kit(p, t);
    p->val[PG_TRACK(t, 2) * 4 + 3] = 0.0f;               // (octave 0: each its kit's pitch)
  }
}

int eng_check(const project_t* p) {
  return p->magic == PROJECT_MAGIC && p->version == PROJECT_VERSION && p->size == sizeof(project_t);
}

}  // extern "C"

namespace {

// the locks a project had before 0.16.7: 384 (now 380); the used ones read in order into the ones there are now (more
// than 380 used: the last left out)
const int kOldLocks = 384;
const int kOldPages = 68;               // (the pages before 0.16.23: the followers' after them)
MI_COLD void locks_from(project_t* p, const plock_t* o, int n = kOldLocks) {
  int j = 0;
  for (int i = 0; i < n && j < ENG_PLOCKS; ++i)
    if (o[i].lane != 0xFF) p->lock[j++] = o[i];
  for (; j < ENG_PLOCKS; ++j) p->lock[j].lane = 0xFF, p->lock[j].step = 0, p->lock[j].tp = 0, p->lock[j].v = 0;
}

}  // namespace

extern "C" {

float eng_dice_span(int param) { return dice_span(param); }
void eng_trauma(int dose, float* gain, float* fm, float* warps, float* delay) {
  float d = static_cast<float>(dose < 0 ? 0 : dose > 100 ? 100 : dose) * 0.01f, w = ramp01(d, 0.5f, 1.0f);
  if (gain) *gain = 1.0f + 7.0f * d * d;
  if (fm) *fm = 0.1f * ramp01(d, 0.4f, 1.0f);
  if (warps) *warps = iround(proj.val[PG_WARPS(0) * 4]) <= WC_OFF ? -1.0f : w > 0.0f ? 0.25f + 0.5f * w : 0.0f;
  if (delay) *delay = 0.45f * ramp01(d, 0.6f, 1.0f);
}

MI_COLD void eng_bend_preview(int t, float* y, int n) {   // a cycle of a sine through the bits as they are (CLOCK: its
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

// a track on another machine than it was (0.16.14: CHIPTUNE and FORMANT gone, SQ-1 in their place; 0.16.43: FM
// CYMBAL, FM HI-HAT, OTIS gone): its knobs its kit's, its locks on its eight knobs (the old meanings) gone
MI_COLD void kit_fresh(project_t* p, int t) {
  lx_kit(p, t);
  for (int i = 0; i < ENG_PLOCKS; ++i) {
    plock_t& k = p->lock[i];
    if (k.lane == t && k.tp >= 1 && k.tp <= LX_KNOBS) k.lane = 0xFF;
  }
}
// (0.16.43) FM CYMBAL (8), FM HI-HAT (9) and OTIS (15) gone, CHOIR at 15 (after VOCODER): 0.16.24's to 0.16.42's
// numbers to these; a track on a model gone plays the nearest (the cymbal, the hi-hat: HAT; OTIS: POLY) from its kit;
// a step's model gone, none. (0.16.49: to this version's numbers at once, the SDS' place made: sds_at)
inline int sds_at(int m) { return m >= kSdsAt ? m + kSdsN : m; }   // (0.16.43's number: this version's)
MI_COLD void models_v35(project_t* p) {
  for (int t = 0; t < ENG_TRACKS; ++t) {
    const int o = p->algo[PG_TRACK(t, 0)];
    p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(sds_at(o < kModels34 ? kFrom34[o] : 0));
    if (o == 8 || o == 9 || o == kOtis34) kit_fresh(p, t);
  }
  if (p->algo[PG_COMP] >= MB_OTT) p->algo[PG_COMP] = MB_COMP;   // (OTT gone too: the compressor)
  for (int i = 0; i < ENG_PLOCKS; ++i) {
    plock_t& k = p->lock[i];
    if (k.lane >= ENG_TRACKS || k.tp != kTpModel) continue;
    if (k.v >= kModels34 || k.v == 8 || k.v == 9 || k.v == kOtis34) k.lane = 0xFF;
    else k.v = static_cast<uint8_t>(sds_at(kFrom34[k.v]));
  }
}
MI_COLD void models_from(project_t* p, int at) {   // a machine come in at at: the numbers from there one further on
  for (int t = 0; t < ENG_TRACKS; ++t) {           // (0.16.20: TAURUS at 16, 0.16.18's and 0.16.19's SPEECH 16 ..
    const int o = p->algo[PG_TRACK(t, 0)];         // MODAL 21; 0.16.21: VOCODER at 17, 0.16.20's SPEECH 17 ..)
    if (o >= at) p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(o + 1 < kModels34 ? o + 1 : 0);
  }
  for (int i = 0; i < ENG_PLOCKS; ++i)             // (0.16.40: a step's model too)
    if (p->lock[i].lane < ENG_TRACKS && p->lock[i].tp == kTpModel && p->lock[i].v >= at) ++p->lock[i].v;
}
MI_COLD void sds_in(project_t* p) {      // (0.16.49) 0.16.43's to 0.16.48's numbers: the SDS' four at 8, the models
  for (int t = 0; t < ENG_TRACKS; ++t)   // from there four further on, a step's model too
    p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(sds_at(p->algo[PG_TRACK(t, 0)]));
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (p->lock[i].lane < ENG_TRACKS && p->lock[i].tp == kTpModel)
      p->lock[i].v = static_cast<uint8_t>(sds_at(p->lock[i].v));
}
MI_COLD void taurus_in(project_t* p) { models_from(p, 16); }   // (0.16.20)
MI_COLD void vocoder_in(project_t* p) { models_from(p, 17); }  // (0.16.21)
MI_COLD void static_in(project_t* p) { models_from(p, 18); }   // (0.16.24)
MI_COLD void models_v27(project_t* p) {   // (0.13 to 0.16.13's numbers: ..., PROPHET 14, CHIPTUNE 15, FORMANT 16,
  for (int t = 0; t < ENG_TRACKS; ++t) {  // SPEECH 17 ... MODAL 22)
    const int o = p->algo[PG_TRACK(t, 0)];
    if (o <= 14) continue;
    if (o <= 16) {
      p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(kOtis34);   // (its kit at models_v35: POLY's)
    } else {
      p->algo[PG_TRACK(t, 0)] = static_cast<uint8_t>(o - 1 < kModels34 ? o - 1 : 0);
    }
  }
}


static_assert(sizeof(project_t) <= 3824u, "a project in one record of the patterns' store (pat_store.h: a sector's 0xF00 "
                                          "bytes, its header and the record's), in one object of the platform's store");
// the old layouts read in place of the live one (one at a time, the main loop's: a project read)
// 0.16.0's to 0.16.6's (versions 24 to 26; 24 without Endless' options, its bytes 0): 42 parameters a lane, 384 locks
struct project_v26 {
  uint32_t magic;
  uint16_t version, size;
  float val[kOldPages * 4];
  uint8_t algo[kOldPages];
  uint8_t out_mode, warps_hq, theme, dice, dice_seed;
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_LANES][ENG_STEPS / 8];
  int8_t rnd[ENG_LANES][42];
  uint16_t seed[ENG_LANES];
  plock_t lock[kOldLocks];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
  uint8_t rseed[ENG_LANES][42];
  uint8_t trauma;
  int8_t speed[ENG_LANES];
  uint8_t dir[ENG_LANES];
  uint8_t endless_opt[ENG_TRACKS][4];
};
static_assert(sizeof(project_v26) == PROJECT_SIZE_26 && offsetof(project_v26, endless_opt) <= PROJECT_SIZE_24 &&
              offsetof(project_v26, endless_opt) + 4u > PROJECT_SIZE_24,
              "0.16.0's to 0.16.6's layout (0.16.0's: Endless' options from its padding on)");
static_assert(TP_CLIMB == 42 && sizeof(project_t) >= PROJECT_SIZE_26, "CLIMB after the 42 parameters 0.16.6's lanes "
              "had; a project's buffer holds an older one");
// 0.16.7's to 0.16.22's (versions 27 to 32): 68 pages (no followers), 380 locks; the same size as now
struct project_v32 {
  uint32_t magic;
  uint16_t version, size;
  float val[kOldPages * 4];
  uint8_t algo[kOldPages];
  uint8_t out_mode, warps_hq, theme, dice, dice_seed;
  cable_t cable[ENG_TRACKS + 1][ENG_CABLES];
  uint8_t trig[ENG_LANES][ENG_STEPS / 8];
  int8_t rnd[ENG_LANES][ENG_NTP];
  uint16_t seed[ENG_LANES];
  plock_t lock[380];
  uint8_t endless[ENG_TRACKS][ENG_ENDLESS];
  uint8_t endless_len[ENG_TRACKS];
  int8_t endless_key[ENG_TRACKS];
  uint8_t rseed[ENG_LANES][ENG_NTP];
  uint8_t trauma;
  int8_t speed[ENG_LANES];
  uint8_t dir[ENG_LANES];
  uint8_t endless_opt[ENG_TRACKS][4];
  uint8_t arp_opt[ENG_TRACKS];
  uint8_t rsv[4];
};
static_assert(sizeof(project_v32) == PROJECT_SIZE_32 && sizeof(project_t) == PROJECT_SIZE_32,
              "0.16.7's to 0.16.22's layout, and now's: the same size (the followers' pages in thirteen locks' room)");
union UpgBuf {                           // (0.16.32: the projects before 0.16 no longer read)
  project_v26 v26;
  project_v32 v32;
};
UpgBuf upg PLAT_POOL;                    // (0.16.1: in the pool, out of the RAM the screen and the engine share)
MI_COLD void upgrade_v26(project_t* p, const project_v26& o) {   // (CLIMB off: its parameter 0, its options 0)
  uint8_t* raw = reinterpret_cast<uint8_t*>(p);
  for (uint32_t i = 0; i < sizeof(project_t); ++i) raw[i] = 0;
  p->magic = PROJECT_MAGIC;
  p->version = PROJECT_VERSION;
  p->size = sizeof(project_t);
  for (int i = 0; i < kOldPages * 4; ++i) p->val[i] = o.val[i];
  for (int i = 0; i < kOldPages; ++i) p->algo[i] = o.algo[i];
  p->out_mode = o.out_mode, p->warps_hq = o.warps_hq, p->theme = o.theme, p->dice = o.dice;
  p->dice_seed = o.dice_seed;
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) p->cable[g][c] = o.cable[g][c];
  for (int l = 0; l < ENG_LANES; ++l) {
    for (int b = 0; b < ENG_STEPS / 8; ++b) p->trig[l][b] = o.trig[l][b];
    for (int i = 0; i < 42; ++i) p->rnd[l][i] = o.rnd[l][i], p->rseed[l][i] = o.rseed[l][i];
    p->seed[l] = o.seed[l];
    p->speed[l] = o.speed[l];
    p->dir[l] = o.dir[l];
  }
  locks_from(p, o.lock);
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int i = 0; i < ENG_ENDLESS; ++i) p->endless[t][i] = o.endless[t][i];
    p->endless_len[t] = o.endless_len[t];
    p->endless_key[t] = o.endless_key[t];
    for (int i = 0; i < 3; ++i) p->endless_opt[t][i] = o.endless_opt[t][i];
  }
  p->trauma = o.trauma;
}
MI_COLD void upgrade_v32(project_t* p, const project_v32& o) {   // (the followers' pages 0; the locks past 367 out)
  uint8_t* raw = reinterpret_cast<uint8_t*>(p);
  for (uint32_t i = 0; i < sizeof(project_t); ++i) raw[i] = 0;
  p->magic = PROJECT_MAGIC;
  p->version = PROJECT_VERSION;
  p->size = sizeof(project_t);
  for (int i = 0; i < kOldPages * 4; ++i) p->val[i] = o.val[i];
  for (int i = 0; i < kOldPages; ++i) p->algo[i] = o.algo[i];
  p->out_mode = o.out_mode, p->warps_hq = o.warps_hq, p->theme = o.theme, p->dice = o.dice;
  p->dice_seed = o.dice_seed;
  for (int g = 0; g <= ENG_TRACKS; ++g)
    for (int c = 0; c < ENG_CABLES; ++c) p->cable[g][c] = o.cable[g][c];
  for (int l = 0; l < ENG_LANES; ++l) {
    for (int b = 0; b < ENG_STEPS / 8; ++b) p->trig[l][b] = o.trig[l][b];
    for (int i = 0; i < ENG_NTP; ++i) p->rnd[l][i] = o.rnd[l][i], p->rseed[l][i] = o.rseed[l][i];
    p->seed[l] = o.seed[l];
    p->speed[l] = o.speed[l];
    p->dir[l] = o.dir[l];
  }
  locks_from(p, o.lock, 380);
  for (int t = 0; t < ENG_TRACKS; ++t) {
    for (int i = 0; i < ENG_ENDLESS; ++i) p->endless[t][i] = o.endless[t][i];
    p->endless_len[t] = o.endless_len[t];
    p->endless_key[t] = o.endless_key[t];
    for (int i = 0; i < 4; ++i) p->endless_opt[t][i] = o.endless_opt[t][i];
    p->arp_opt[t] = o.arp_opt[t];
  }
  p->trauma = o.trauma;
}
MI_COLD void climb_off_all(project_t* p) {   // (a project before 0.16.7: CLIMB where nothing was, 0)
  for (int l = 0; l < ENG_LANES; ++l) p->val[PG_EUC(l) * 4 + 2] = p->val[PG_EUC(l) * 4 + 3] = 0.0f;
  for (int t = 0; t < ENG_TRACKS; ++t) p->endless_opt[t][3] = 0;
}
// RATCH's travel: 1..8 before 0.16, 1..8 then eight tuplets (9..16) in 0.16.0 to 0.16.3, hits over a span (1..256)
// since 0.16.4: a project before, its knobs and its locks as they were (a lock is a share of the travel; old_max: 8 or
// 16)
MI_COLD int ratch_from(int r) {         // 0.16.0's value: hits (1..8), or a tuplet's hits over its steps
  if (r <= 8) return r < 1 ? 1 : r;
  const uint8_t* q = kTuplet[(r > 16 ? 16 : r) - 9];
  return eng_ratch_join(q[0], q[1]);
}
MI_COLD void ratch_widened(project_t* p, int old_max) {
  for (int t = 0; t < ENG_TRACKS; ++t) {
    float* v = &p->val[PG_STEP(t) * 4 + 2];
    *v = static_cast<float>(ratch_from(static_cast<int>(*v + 0.5f)));
  }
  for (int i = 0; i < ENG_PLOCKS; ++i) {
    plock_t& k = p->lock[i];
    if (k.lane >= ENG_TRACKS || k.tp != TP_RATCHET) continue;
    int r = static_cast<int>(1.0f + static_cast<float>(k.v) * static_cast<float>(old_max - 1) / 255.0f + 0.5f);
    k.v = static_cast<uint8_t>(ratch_from(r) - 1);   // (1..256 over 0..255: each its own)
  }
}
int upgrade_any(project_t* p, int bytes);
MI_COLD int eng_upgrade(project_t* p, int bytes) {   // (p: room for any of them, sizeof(project_t) >= the largest)
  if (bytes == static_cast<int>(sizeof(project_t)) && eng_check(p)) return 1;
  const int v = p->magic == PROJECT_MAGIC ? static_cast<int>(p->version) : 0;
  if (v >= 33 && v <= 35 && bytes == static_cast<int>(sizeof(project_t)) && p->size == sizeof(project_t)) {
    p->version = PROJECT_VERSION;       // (0.16.23: its layout this one; STATIC at 18, the models from there one
    if (v == 33) static_in(p);          // further on; 0.16.24 to 0.16.42: the models gone, CHOIR's place; 0.16.43 to
    if (v <= 34) models_v35(p);         // 0.16.48: the SDS' place)
    else sds_in(p);
    return 1;
  }
  if (v >= 27 && v <= 32 && bytes == static_cast<int>(sizeof(project_v32)) && p->size == sizeof(project_v32)) {
    const uint8_t* from = reinterpret_cast<const uint8_t*>(p);   // (0.16.7 to 0.16.22: 68 pages, 380 locks)
    uint8_t* to = reinterpret_cast<uint8_t*>(&upg.v32);
    for (uint32_t i = 0; i < sizeof(project_v32); ++i) to[i] = from[i];
    upgrade_v32(p, upg.v32);
    if (v == 27) models_v27(p);         // (0.16.13's models one more; then TAURUS at 16, VOCODER at 17, STATIC at 18,
    if (v <= 30) taurus_in(p);          // the models from there one further on; 0.16.17's ROBOVOX (16), withdrawn:
    if (v <= 31) vocoder_in(p);         // SPEECH again in its place, at noon)
    static_in(p);
    models_v35(p);
    if (v == 29)
      for (int t = 0; t < ENG_TRACKS; ++t)
        if (p->algo[PG_TRACK(t, 0)] == eng_model_find(MK_PLAITS, 15)) kit_fresh(p, t);
    return 1;
  }
  if (!upgrade_any(p, bytes)) return 0;   // (0.16.32: before 0.16, a project is no longer read: 0)
  climb_off_all(p);
  models_v27(p), taurus_in(p), vocoder_in(p), static_in(p);   // (0.16.0 to 0.16.6: their numbers 0.16.13's)
  models_v35(p);
  return 1;
}
MI_COLD int upgrade_any(project_t* p, int bytes) {
  int v = p->magic == PROJECT_MAGIC ? p->version : 0;
  if (v >= 24 && v <= 26 && bytes == static_cast<int>(v == 24 ? PROJECT_SIZE_24 : PROJECT_SIZE_26) &&
      p->size == (v == 24 ? PROJECT_SIZE_24 : PROJECT_SIZE_26)) {
    const uint8_t* from = reinterpret_cast<const uint8_t*>(p);   // (0.16.0 to 0.16.6: their layout widened; 0.16.0's
    uint8_t* to = reinterpret_cast<uint8_t*>(&upg.v26);          // and 0.16.1's without Endless' options, 0)
    uint32_t keep = v == 24 ? offsetof(project_v26, endless_opt) : sizeof(project_v26);
    for (uint32_t i = 0; i < sizeof(project_v26); ++i) to[i] = i < keep ? from[i] : 0;
    upgrade_v26(p, upg.v26);
    if (v <= 25) ratch_widened(p, 16);   // (0.16.0 to 0.16.3: RATCH's tuplets)
    return 1;
  }
  return 0;
}

MI_COLD int eng_init(void) {
  uint32_t need = 0, a = 0;
  mf_init();                            // (the tracks' filters: their own static state)
  make_mu();                            // (the glitch's slices)
  for (int t = 0; t < ENG_TRACKS; ++t) gl_at[t] = -1;
  climb_fresh();
  need = 4u * ((mp_size() + 7u) & ~7u) + ((mrv_size() + 7u) & ~7u) +
         ((mw_size() + 7u) & ~7u) + ((mdl_size() + 7u) & ~7u) + ((mms_size() + 7u) & ~7u) + ENG_TRACKS * kGlBytes;
  if (need > sizeof(arena)) return -1;
  for (int t = 0; t < ENG_TRACKS; ++t) {
    voice[t] = arena + a;
    a += (mp_size() + 7u) & ~7u;
    mp_init(voice[t]);
    vlent[t] = 0;
    ++vepoch[t];
    vgate[t] = 0;
    vnote[t] = 48.0f;
    key_on[t] = 0;
    kb_n[t] = 0;
  }
  lx_init();                            // (the FM drums: static, one of each a track)
  zero_init();                          // (ZERO's: its loops)
  drum_init();
  fm4_init();
  pads_init();
  poly_init();
  taurus_init();
  voc_init();
  static_init();
  pro_init();
  choir_init();
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
  duck_map();
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
    sp_p[t] = new (sp_mem[t]) EqPair();
    sp_run[t] = 0;
    sp_s[t] = mix_sp[t] = sp_w[t] = 0.0f;
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
MI_COLD void cut_now() {
  lx_init();                            // (the FM drums too: started again from their kits at the next block)
  for (int t = 0; t < ENG_TRACKS; ++t) {   // (each track's machine: ZERO silent until its next trig, its loops kept)
    const int k = mowner[t];
    if (k == MK_ZERO) zero_silence(t);
    else if (k == MK_DRUM) drum_silence(t);
    else if (k == MK_SDS) sds_silence(t);
    else if (k == MK_FM) fm4_silence(t);
    else if (k == MK_POLY) poly_silence(t);
    else if (k == MK_PROPHET) pro_silence(t);
    else if (k == MK_CHOIR) choir_silence(t);
    else if (k == MK_TAURUS) taurus_silence(t);
    else if (k == MK_VOCODER) voc_silence(t);
    else if (k == MK_STATIC) static_silence(t);
  }
  pads_silence();
  for (int t = 0; t < ENG_TRACKS; ++t) mf_reset(t);   // (the filters' rings)
  for (int t = 0; t < ENG_TRACKS; ++t) {
    sp_run[t] = 0;                      // (SPREAD's lines: silent again)
    if (!vlent[t]) mp_init(voice[t]);   // (VOCODER's memory kept)
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

// the patterns (0.15, app.c's: each a whole project): the next one cued by the main loop, taken by the render in the
// block where the playing one ends (its longest lane's LENGTH in sixteenths), the lanes from their first step
const project_t* volatile cue_p;        // the next pattern (the main loop's copy, untouched until taken), 0 none
volatile uint32_t pat_cycles;           // the patterns played to their end (taken or again)
int32_t pat_end_seen = -2;
int pattern_len() {                     // in sixteenths: the longest lane's pass at its SPEED (0.16)
  int len = 1;
  for (int l = 0; l < ENG_LANES; ++l) {
    int sp = lane_speed(l), k = lane_steps(l);
    if (sp != 4) k = static_cast<int>(static_cast<float>(k) / kSpeedMul[sp] + 0.999f);
    len = k > len ? k : len;
  }
  return len;
}
void take_pattern(const project_t* c) {   // the machine's settings stay (the FM-1's, not the music's)
  uint8_t out = proj.out_mode, hq = proj.warps_hq, th = proj.theme;
  proj = *c;
  proj.out_mode = out;
  proj.warps_hq = hq;
  proj.theme = th;
  for (int l = 0; l < ENG_LANES; ++l) lane_fresh(l);
  ++lock_gen;
  pad_pos = -1;
  vc_pos[0] = vc_pos[1] = vc_pos[2] = vc_pos[3] = -1;
  for (int t = 0; t < ENG_TRACKS; ++t) {
    e_clk[t] = 1e9f;
    e_pos[t] = -1;
    e_dir[t] = 1;
  }
}
void eng_cue(const project_t* next) {   // (its bytes all written before the pointer is)
  asm volatile("" ::: "memory");          // (the project read whole before the cue: a compiler barrier, no call)
  cue_p = next;
}
int eng_cue_pending(void) { return cue_p != 0; }
uint32_t eng_cycles(void) { return pat_cycles; }
int eng_pattern_len(void) { return pattern_len(); }
int eng_pattern_pos(void) { return playing && tick >= 0 ? static_cast<int>(tick % pattern_len()) : -1; }

void eng_key(int track, int note, int on) {
  if (track < 0 || track >= ENG_TRACKS) return;
  if (on) trig_model[track] = 0;        // (0.16.40: the keys play the track's model, not the last trig's)
  int n = kb_n[track], j = 0;           // (0.16.15) the keys held: this one out, then on top if pressed
  for (int i = 0; i < n; ++i)
    if (kb_note[track][i] != note) kb_note[track][j++] = kb_note[track][i];
  n = j;
  if (on) {
    if (n == kKeys) {                   // (the oldest forgotten)
      for (int i = 1; i < n; ++i) kb_note[track][i - 1] = kb_note[track][i];
      --n;
    }
    kb_note[track][n++] = note;
    kb_n[track] = n;
    key_note[track] = note;
    key_on[track] = 1;
    key_retrig[track] = 1;              // a new key: a new trig, even played legato
    key_new[track] = 1;
    key_hold[track] = static_cast<int>(kFrames) * 3;   // and at least three blocks, however short the press
  } else {
    kb_n[track] = n;
    if (!n) {
      key_on[track] = 0;
    } else if (key_note[track] == note) {   // the one playing let go, others held: back to the newest of them, a
      key_note[track] = kb_note[track][n - 1];   // new trig
      key_retrig[track] = 1;
    }
  }
}
void eng_key_release_all(void) {        // (no key down any more: none held anywhere, whatever the track then)
  for (int t = 0; t < ENG_TRACKS; ++t) kb_n[t] = 0, key_on[t] = 0;
}

void eng_core1(int on) { use_core1 = on; }

void eng_cut(void) {
  eng_play(0);
  if (!cut_req) cut_req = 1;
}
int eng_cut_pending(void) { return cut_req != 0; }

#if ENG_USB_TRACKS
const int16_t* eng_usb_frames(uint32_t n, int live) {
  usb_req = 2;                          // the host records: the next blocks make them
  if (!live || !usb_ok || n > kN) {     // (none made for this block: silence)
    for (uint32_t i = 0; i < kN * ENG_USB_CH; ++i) usb_ch[i] = 0;
    usb_ok = 0;
  }
  return usb_ch;
}
#endif

void eng_render(float* l, float* r, uint32_t n) {
#if ENG_USB_TRACKS
  usb_ok = 0;
#endif
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
#if ENG_USB_TRACKS
  usb_now = usb_req != 0;               // the host records: the effects' job (the last block's mix) makes its channels
  if (usb_req) usb_req = static_cast<uint8_t>(usb_req - 1u);
#endif
  uint32_t early = fx_n ? 1u << kTaskFx : 0u;
  if (w_job) {
    early |= 1u << kTaskWarps;
    warps_calls = mw_calls(warps, n);
    task_cost[kTaskWarps] = warps_chunk_cost * static_cast<float>(warps_calls);
  }
  for (int t = 0; t < ENG_TRACKS; ++t)   // (0.16.21) VOCODER's voice: its MOD track's last block, as the voice played it
    if (track_kind(t) == MK_VOCODER) {   // (before its level, the mix's filter and duck: taken before the mix's job,
      const int u = voc_mod_track(lx_u(lx_param(t, 0), E(t, 1)));   // which works in it, can start)
      mclaim(t);                         // (its state its own first)
      voc_modulator(t, u >= 0 && u != t && fx_n == n ? mbuf[u] : nullptr, n);
    }
  if (fx_n == n)                        // (0.16.23) the followers: the same, their tracks' last block
    for (int i = 0; i < 3; ++i)
      if (Pi(PG_LFO(i), 0) == LW_FOLLOW && Pi(PG_LFO(i), 2) <= LF_TRIG) {
        const int src = Pi(PG_FOLLOW(i), 0);
        follow_listen(mbuf[src < 0 ? 0 : src > ENG_TRACKS - 1 ? ENG_TRACKS - 1 : src], i, n);
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
  if (playing && !restart && tick >= 0 && pat_end_seen != tick) {   // the pattern's end in this block: the next one
    int len = pattern_len();                                         // in (cued), or the same again
    if ((tick + 1) % len == 0 && six_len(step_len_now()) - step_clk <= static_cast<float>(n)) {
      pat_end_seen = tick;
      ++pat_cycles;
      const project_t* c = cue_p;
      if (c) {
        take_pattern(c);
        cue_p = 0;
        tick = -1;                      // (its next sixteenth its first step)
        for (int l = 0; l < ENG_LANES; ++l) lane_cnt[l] = lane_grid[l] = -1, lane_spd[l] = static_cast<int8_t>(lane_speed(l));
        climb_fresh();
        pat_end_seen = -2;
      }
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
  clk_n = 0;
  if (clk_at >= 0 || clk_y1 != 0.0f) {  // (0.16.31) the metronome: a ping at each beat (C6), the bar's a fifth up, ~25 ms;
    clk_n = static_cast<uint8_t>(n <= ENG_CLICK_N ? n : 0);   // (0.16.34) apart too: the USB records the mix without it
    for (uint32_t i = 0; i < n; ++i) {
      if (static_cast<int32_t>(i) == clk_at) {
        clk_c = clk_hi ? 1.9485305f : 1.9760169f;
        clk_y1 += clk_hi ? 0.0775f : 0.0371f;   // (0.35 and 0.25 at their peaks)
      }
      float y = clk_c * clk_y1 - 0.9981876f * clk_y2;
      clk_y2 = clk_y1;
      clk_y1 = y;
      if (clk_n) clk_out[i] = y;
      l[i] += y;
      r[i] += y;
    }
    clk_at = -1;
    if (clk_y1 * clk_y1 + clk_y2 * clk_y2 < 1e-10f) clk_y1 = clk_y2 = 0.0f;
  }
#if ENG_USB_TRACKS
  usb_ok = usb_now && fx_n == n;
#endif
  if (cut_req == 1) {                   // STOP twice: faded out over this block, the modules made new at the next
    float g = 1.0f, dg = 1.0f / static_cast<float>(n);
    for (uint32_t i = 0; i < n; ++i) {
      g -= dg;
      l[i] *= g;
      r[i] *= g;
      if (clk_n) clk_out[i] *= g;
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
    mj.duck1[t] = duck_depth[t] = t != duck_src() ? E(t, TP_DUCK) : 0.0f;
    eq_s[t] += (E(t, TP_EQ) - eq_s[t]) * 0.5f;
    mj.eq[t] = eq_s[t];
    // SPREAD: the side at the middle's gain on its quieter side (none hard to one side: a point stays a point), the
    // knob eased (its first half the most of the width)
    float w = fin[PG_SPREAD_PARAM(t)];
    sp_s[t] += (w - sp_s[t]) * 0.5f;
    if (sp_s[t] < 1e-4f && w <= 0.0f) sp_s[t] = 0.0f;
    float ws = clampf(sp_s[t], 0.0f, 1.0f), side = kSpreadMax * ws * (2.0f - ws);
    float tsp = side * fm_minf(tl, tr);
    sp_w[t] = level > 0.0f ? tsp * 1.41421f / (level * 0.85f) : 0.0f;   // (the middle's gain in the middle)
    mj.sp0[t] = mix_sp[t];
    mj.sp1[t] = mix_sp[t] = tsp;
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
  duck_map();
  mrv_set(reverb, proj.algo[PG_REVERB], E(LANE_REVERB, 0), E(LANE_REVERB, 1), E(LANE_REVERB, 2));
  int div = iround(E(LANE_DELAY, 0));   // its time: a division of the tempo, or (FREE) in ms
  float dtime = proj.algo[PG_DELAY(0)] == DL_FREE ? delay_ms(E(LANE_DELAY, 0)) * kSr * 0.001f
                                                  : step_len_now() * kDivMult[div < 0 ? 0 : div >= kDivs ? kDivs - 1 : div];
  mdl_set(delay, dtime, E(LANE_DELAY, 1), E(LANE_DELAY, 2), E(LANE_DELAY, 4), E(LANE_DELAY, 3),
          iround(E(LANE_DELAY, 5)), E(LANE_DELAY, 6));
  mms_mode(master, proj.algo[PG_COMP] >= MB_OTT ? static_cast<int>(MB_COMP) : proj.algo[PG_COMP]);
  mms_comp(master, comp_threshold(P(PG_COMP, 0)), comp_ratio(P(PG_COMP, 1)), comp_release(P(PG_COMP, 2)),
           P(PG_COMP, 3) * 12.0f);
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
MI_COLD void eng_delay_mode(int mode) {
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
float eng_spread_width(int t) { return t >= 0 && t < ENG_TRACKS ? sp_w[t] : 0.0f; }
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
int eng_step_tie(int lane, int step) {  // (0.16.15) a trig's length over the steps after it (0: its own step)
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (proj.lock[i].lane == lane && proj.lock[i].step == step && proj.lock[i].tp == kTpTie) return proj.lock[i].v;
  return 0;
}
int eng_step_tie_set(int lane, int step, int steps) {   // 0 ok, -1 no room; steps 0: none
  if (lane < 0 || lane >= ENG_TRACKS || step < 0 || step >= ENG_STEPS) return -1;
  steps = steps < 0 ? 0 : steps > 63 ? 63 : steps;
  int i = lock_find(lane, step, kTpTie);
  if (!steps) {
    if (i >= 0) proj.lock[i].lane = 0xFF, ++lock_gen;
    return 0;
  }
  if (i < 0)
    for (int j = 0; j < ENG_PLOCKS && i < 0; ++j)
      if (proj.lock[j].lane == 0xFF) i = j;
  if (i < 0) return -1;
  proj.lock[i].v = static_cast<uint8_t>(steps);
  proj.lock[i].step = static_cast<uint8_t>(step);
  proj.lock[i].tp = static_cast<uint8_t>(kTpTie);
  proj.lock[i].lane = static_cast<uint8_t>(lane);
  ++lock_gen;
  return 0;
}
void eng_step_ties(int lane, uint8_t* out) {   // every step's (ENG_STEPS), one look
  for (int s = 0; s < ENG_STEPS; ++s) out[s] = 0;
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (proj.lock[i].lane == lane && proj.lock[i].tp == kTpTie && proj.lock[i].step < ENG_STEPS)
      out[proj.lock[i].step] = proj.lock[i].v;
}
// (0.16.16) a step's notes (NOTE's values: its lock, else the track's; the chord's over it), the lowest first; how many
int eng_step_notes(int lane, int step, int* out) {
  if (lane < 0 || lane >= ENG_TRACKS || step < 0 || step >= ENG_STEPS) return 0;
  float v = 0.0f;
  int base = eng_lock(lane, step, TP_NOTE, &v) ? iround(v) : iround(proj.val[eng_track_param(lane, TP_NOTE)]);
  int n = 0;
  out[n++] = base;
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (proj.lock[i].lane == lane && proj.lock[i].step == step && proj.lock[i].tp >= kTpNote2 &&
        proj.lock[i].tp < kTpNote2 + 3 && proj.lock[i].v && n < 4)
      out[n++] = base + proj.lock[i].v;
  for (int i = 1; i < n; ++i)           // (in order)
    for (int j = i; j > 0 && out[j] < out[j - 1]; --j) {
      int x = out[j];
      out[j] = out[j - 1], out[j - 1] = x;
    }
  return n;
}
MI_COLD int eng_step_notes_set(int lane, int step, const int* notes, int n) {   // 0 ok; -1 no room, -2 out of NOTE's range
  if (lane < 0 || lane >= ENG_TRACKS || step < 0 || step >= ENG_STEPS || n < 1) return -1;
  int s[4];
  n = n > 4 ? 4 : n;
  for (int i = 0; i < n; ++i) s[i] = notes[i];
  for (int i = 1; i < n; ++i)
    for (int j = i; j > 0 && s[j] < s[j - 1]; --j) {
      int x = s[j];
      s[j] = s[j - 1], s[j - 1] = x;
    }
  const eng_pdef_t* d = eng_pdef(eng_track_param(lane, TP_NOTE));
  if (s[0] < static_cast<int>(d->lo) || s[0] > static_cast<int>(d->hi) || s[n - 1] - s[0] > 96) return -2;
  int m = 0;                            // (the same notes once)
  for (int i = 0; i < n; ++i)
    if (!m || s[i] != s[m - 1]) s[m++] = s[i];
  n = m;
  for (int j = 0; j < 3; ++j) {         // the chord's locks: the old ones out, then the new
    int i = lock_find(lane, step, kTpNote2 + j);
    if (i >= 0) proj.lock[i].lane = 0xFF;
  }
  if (eng_lock_set(lane, step, TP_NOTE, static_cast<float>(s[0])) != 0) return -1;
  for (int j = 1; j < n; ++j) {
    int i = -1;
    for (int q = 0; q < ENG_PLOCKS && i < 0; ++q)
      if (proj.lock[q].lane == 0xFF) i = q;
    if (i < 0) return -1;
    proj.lock[i].v = static_cast<uint8_t>(s[j] - s[0]);
    proj.lock[i].step = static_cast<uint8_t>(step);
    proj.lock[i].tp = static_cast<uint8_t>(kTpNote2 + j - 1);
    proj.lock[i].lane = static_cast<uint8_t>(lane);
  }
  ++lock_gen;
  return 0;
}
void eng_step_chords(int lane, uint8_t* out) {   // every step's notes over its NOTE (0: a note alone), one look
  for (int s = 0; s < ENG_STEPS; ++s) out[s] = 0;
  for (int i = 0; i < ENG_PLOCKS; ++i)
    if (proj.lock[i].lane == lane && proj.lock[i].tp >= kTpNote2 && proj.lock[i].tp < kTpNote2 + 3 &&
        proj.lock[i].step < ENG_STEPS && proj.lock[i].v)
      ++out[proj.lock[i].step];
}
int eng_track_chords(int t) {           // (0.16.43: CHOIR past Solo sings a step's chord, its keys one at a time)
  return t >= 0 && t < ENG_TRACKS &&
         (kb_poly(t) || (track_kind(t) == MK_CHOIR && choir_voices(lx_u(lx_param(t, 1), E(t, 2))) > 1));
}
int eng_step_locked(int lane, int step) {
  int n = 0;
  for (int i = 0; i < ENG_PLOCKS; ++i) n += proj.lock[i].lane == lane && proj.lock[i].step == step;
  return n;
}
int eng_step_now(int lane) {
  return playing && lane >= 0 && lane < ENG_LANES && tick >= 0 ? cur_step[lane] : -1;
}
// (0.16.31) live recording: a lane's place since PLAY in its own steps (whole: the steps it took; the fraction: how far
// into the one playing), -1 stopped or counting in; the step the keys wrote just ahead of it (not played when it gets
// there: the key was heard); the next PLAY's count-in; the metronome
float eng_lane_pos(int lane) {
  if (!playing || restart || tick < 0 || lane < 0 || lane >= ENG_LANES) return -1.0f;
  return seq_at * kSpeedMul[lane_speed(lane)];
}
void eng_rec_skip(int lane, int step) {
  if (lane >= 0 && lane < ENG_TRACKS) rec_skip[lane] = static_cast<int8_t>(step);
}
void eng_count_in(int sixteenths) { count_in = sixteenths; }
void eng_click(int on) { click_on = on != 0; }
const float* eng_click_out(void) { return clk_n ? clk_out : nullptr; }
float eng_lane_open(int lane) {
  return lane >= ENG_TRACKS && lane < ENG_LANES ? fx_open[lane - ENG_TRACKS] : 1.0f;
}
int eng_quantize(int note) { return quantize_mask(note, scale_mask(Pi(PG_GLOBAL, 1)), Pi(PG_GLOBAL, 2)); }
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
int eng_endless_pos(int t) {
  if (t < 0 || t >= ENG_TRACKS) return -1;
  if (arp_mode(t) != AM_LOOP) return arp_has(t) && arp_len[t] && (playing || e_free[t]) ? e_pos[t] : -1;
  return eng_endless_on(t) && (playing || e_free[t]) ? e_pos[t] : -1;
}
const char* const kArpModes[AM_N] = {"Loop", "Keys", "Chord"};
int eng_arp_opt(int t, int o) {
  if (t < 0 || t >= ENG_TRACKS) return 0;
  unsigned b = proj.arp_opt[t];
  if (o == AO_MODE) { int v = static_cast<int>(b & 3u); return v >= AM_N ? AM_LOOP : v; }
  if (o == AO_OCT) return static_cast<int>((b >> 2) & 3u) + 1;
  if (o == AO_LATCH) return static_cast<int>((b >> 4) & 1u);
  if (o == AO_ORDER) return static_cast<int>((b >> 5) & 1u);
  return 0;
}
void eng_arp_opt_set(project_t* p, int t, int o, int v) {
  if (t < 0 || t >= ENG_TRACKS) return;
  unsigned b = p->arp_opt[t];
  if (o == AO_MODE) b = (b & ~3u) | static_cast<unsigned>(v < 0 ? 0 : v >= AM_N ? AM_N - 1 : v);
  else if (o == AO_OCT) b = (b & ~0x0Cu) | static_cast<unsigned>((v < 1 ? 1 : v > 4 ? 4 : v) - 1) << 2;
  else if (o == AO_LATCH) b = (b & ~0x10u) | (v ? 0x10u : 0u);
  else if (o == AO_ORDER) b = (b & ~0x20u) | (v ? 0x20u : 0u);
  p->arp_opt[t] = static_cast<uint8_t>(b);
  if (p == &proj && o == AO_LATCH && !v && !arp_down[t]) arp_n[t] = 0;   // (LATCH off, no key down: let go)
}
const char* eng_arp_mode_name(int v) { return kArpModes[v < 0 || v >= AM_N ? 0 : v]; }
void eng_arp_key(int t, int note, int on) {
  if (t < 0 || t >= ENG_TRACKS) return;
  int n = arp_n[t], latch = eng_arp_opt(t, AO_LATCH);
  if (on) {
    if (latch && !arp_down[t]) n = 0;   // (latched: a new chord the keys after all were let go)
    int at = -1;
    for (int i = 0; i < n; ++i)
      if (arp_held[t][i] == note) at = i;
    if (at < 0 && n < 8) arp_held[t][n++] = static_cast<uint8_t>(note & 127);
    ++arp_down[t];
  } else {
    if (arp_down[t] > 0) --arp_down[t];
    if (!latch) {
      int k = 0;
      for (int i = 0; i < n; ++i)
        if (arp_held[t][i] != note) arp_held[t][k++] = arp_held[t][i];
      n = k;
    }
  }
  arp_n[t] = n;
}
void eng_arp_release_all(void) {        // (every key let go: the ARPs not latched empty, no key down anywhere)
  for (int t = 0; t < ENG_TRACKS; ++t) {
    arp_down[t] = 0;
    if (!eng_arp_opt(t, AO_LATCH)) arp_n[t] = 0;
  }
}
int eng_arp_list(int t, uint8_t* out, int max) {
  if (t < 0 || t >= ENG_TRACKS || arp_mode(t) == AM_LOOP) return 0;
  return arp_build(t, out, max);
}
void eng_endless_free(int t, int on) {   // (0.16.11) the sequencer stopped: Endless by a key held (-1: all let go;
  for (int i = 0; i < ENG_TRACKS; ++i) { // 0.16.13: an ARP's latched notes go on)
    if (t >= 0 && i != t) continue;
    if (!on && t < 0 && arp_mode(i) == AM_KEYS && eng_arp_opt(i, AO_LATCH) && arp_n[i] > 0) continue;
    if (on && !e_free[i]) e_free_go[i] = 1;
    e_free[i] = on;
  }
}
int eng_climb_now(int t, int step) {
  return t >= 0 && t < ENG_TRACKS && step >= 0 && step < ENG_STEPS && playing ? climb_off[t][step] : 0;
}
int eng_endless_state(int t) {
  return t >= 0 && t < ENG_TRACKS && eng_endless_on(t) && (playing || e_free[t]) ? e_state[t] : ES_NOTE;
}

}  // extern "C"
