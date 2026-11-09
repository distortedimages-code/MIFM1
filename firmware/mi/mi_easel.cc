// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's EASEL (0.16.63): a West Coast voice after Buchla's Music Easel (the 208, 1973). The Easel's sound as its
// voice has it: the complex oscillator, a sine whose TIMBRE folds it (Buchla's timbre circuit), frequency-modulated or
// amplitude-modulated by the modulation oscillator (a triangle; its rate a ratio of the note: the partials metallic off
// the whole numbers), then the lowpass gate, its vactrol lit by the envelope: a short pulse "pings" it, the gate open
// at once then closing slowly, dimming as it fades.
//
// The timbre: Buchla's 259 wavefolder as Fabian Esqueda, Henri Pontynen, Vesa Valimaki and Julian D. Parker model it
// ("Virtual Analog Buchla 259 Wavefolder", DAFx-17): five folding cells in parallel with the direct path, their
// thresholds and weights the circuit's, its output's first-order lowpass (1.2 MOhm, 100 pF: 1.33 kHz); the alias
// held down by the first-order antiderivative (ADAA: the cells' integral, exact, its difference over the input's) and
// the hardware's own trait, the timbre weaker as the pitch rises. SYMM shifts the sine into it (the 259's symmetry:
// the even partials).
//
// The gate: the Buchla lowpass gate as Julian Parker and Stefano D'Angelo model it ("A Digital Model of the Buchla
// Lowpass-Gate", DAFx-13): its audio circuit's transfer, H(s) = 1 / (a1 + a2 s + a3 s^2) (C1 1 nF, C2 220 pF, C3 4.7 nF
// in LP, R-alpha 5 MOhm, 5 kOhm in VCA: 50 k here, its ping as loud, nearly), its three modes (VCA, Combo: both at
// once, LP), the photoresistor Rf = A / If^1.4 + B (A 3.464, B 1136 Ohm) of the LED's current (40 mA lit; dark here
// 0.1 uA: the paper's floor of 10 uA leaves the gate ajar), the vactrol (0.16.64) a CdS cell's carriers: made at the
// light's rate, recombining in pairs and alone, slower with its history (the paper's one-pole, 12 ms up, 250 ms down,
// left: its pings fell as a cliff, flat then plunging; a struck thing falls quick, then lingers), the circuit's two
// states through the trapezoid (the paper's TPT).
//
// Eight knobs (k: 0..1; a bipolar knob from its middle): TIMBRE (the fold), RATIO (the modulation oscillator's rate,
// times the note: 24 ratios, whole and metallic), MOD (to the left AM, its index: the Easel's crossfade to the fully
// modulated sound; to the right FM, through zero, its index 0..8), DECAY (its first third a Ping: the envelope a
// pulse, the cell's fall: 40 dB down in 35 .. 700 ms; then the envelope's decay, 2 ms .. 8 s), GATE (VCA, Combo,
// LP), T.ENV and M.ENV (the gate's light onto TIMBRE and onto MOD), SYMM (the 259's symmetry).
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f;
const int kSub = 8;                     // the envelope, the vactrol, the gate's coefficients: their steps (samples)
const float kTus = 1e6f / kSr;          // a sample in microseconds (the gate's circuit: kOhm, nF, us)

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }

// the 259's five cells (Esqueda et al., eq. 13-18): above its threshold a cell gives g (|x| - th), its sign x's;
// the sum 5 x + the cells by their weights (the summing amplifiers'). kC: weight times gain; its integral's, a half
// (0.16.64: in their thresholds' order, the loop left at the first one over the input: most of the time one or two)
const float kTh[5] = {0.6f, 1.8f, 2.994f, 4.08f, 5.46f};
const float kC[5] = {-12.0f * 0.8333f, 17.647f * 0.5743f, -27.777f * 0.3768f, 36.363f * 0.2673f, -21.428f * 0.2829f};
const float kDirect = 5.0f;

// RATIO: the modulation oscillator times the note; the whole ones and between them the metallic (sqrt 2, phi,
// sqrt 3, sqrt 5, 1 + sqrt 2, the bell's 2.76, pi, 3.5, sqrt 18, 5.4, 2 pi ...)
const int kRatios = 24;
const float kRatio[kRatios] = {0.25f, 0.5f, 0.707f, 1.0f, 1.17f, 1.414f, 1.5f, 1.618f, 1.732f, 2.0f, 2.236f, 2.414f,
                               2.76f, 3.0f, 3.142f, 3.5f, 4.0f, 4.243f, 5.0f, 5.4f, 6.0f, 6.283f, 7.0f, 8.93f};
inline int ratio_of(float u) {
  const int i = static_cast<int>(clampf(u, 0.0f, 1.0f) * static_cast<float>(kRatios));
  return i > kRatios - 1 ? kRatios - 1 : i;
}
inline int gate_of(float u) {           // GATE: VCA, Combo, LP
  const int i = static_cast<int>(clampf(u, 0.0f, 1.0f) * 3.0f);
  return i > 2 ? 2 : i;
}
inline float timbre_a(float u) { return 0.4f * fm_exp2f(u * 4.644f); }   // the sine into the folder: 0.4 .. 10 V
const float kPing = 1.0f / 3.0f;        // DECAY: its first third a ping
inline float ping_s(float u) { return 0.035f * fm_exp2f(u * 3.0f * 4.3219f); }  // a ping, 40 dB down: 35 .. 700 ms
const float kVactrol = 0.5f;            // (its fall past a ping: an envelope's tail)
inline float decay_s(float u) { return 0.002f * fm_exp2f((u - kPing) * 1.5f * 11.97f); }   // the envelope: 2 ms .. 8 s

// the vactrol: the photoresistor's state as the LED's current that would hold it there (the paper's law, Rf of If,
// kept), its logarithm (dark 0.1 uA, the photoresistor past 20 GOhm: shut; lit 40 mA); its light, 0..1, over the
// paper's span (10 uA .. 40 mA)
const float kLnDark = -16.1181f, kLnMin = -11.5129f, kLnMax = -3.2189f, kLnSpan = kLnMax - kLnMin;
const float kDark = 1e-7f;
// (0.16.64) the cell as a CdS cell behaves. Lit, it turns on as before (its log rising, 12 ms, quicker lit hard: a
// ping's soft 2 ms onset), quicker still when warm (its history: 15 % at most). Dark, its carriers recombine, two ways
// at once: in pairs (i^2: quick while it is bright, then a power law's long tail, 1 / t: the ping's quick drop, its
// dark lingering end) and alone (i: the tail cut, the cell back to dark); its time T from DECAY (T 0.1 ms at 3 mA for
// the pairs, 0.35 T alone: a ping 40 dB down at C3, in Combo, in DECAY's time; the one-pole on its log it replaces
// fell flat, then plunged). Its history: lit a while, its traps full, it recombines slower (up to 1.6 times; filled in
// 0.25 s, emptied in 2.5 s: a run of pings rings on longer than one). Each strike a little its own: the LED's current
// +-4 %, T +-4 % (the hands, the cell's warmth). Steps of kSub samples
const float kStep = static_cast<float>(kSub) / kSr;
const float kHUp = 7.25361e-4f, kHDown = 7.25598e-5f;   // 1 - e^(-kStep / 0.25 s), / 2.5 s: the history
const float kAlone = 1.0f / 0.35f, kPairs = 30.0f / 0.003f, kHist = 0.6f;

struct Easel {
  float k[8];
  float note;
  int gate, trig, alive;
  float pc, pm;                         // the complex oscillator's phase, the modulation oscillator's (turns)
  float x1;                             // the folder's last input (ADAA)
  float lpx, lpy;                       // the 259's output lowpass
  float hx, hy;                         // a coupling capacitor before the gate (10 Hz: the fold's DC, FM's)
  float env;                            // the envelope (its pulse, its decay)
  float iv, ln_i, hist;                 // the vactrol: its state (A), its log; its history (0..1)
  float sv, tj;                         // this strike's LED current, its time (around 1)
  uint32_t rng;
  float vx, vo, u1;                     // the gate's circuit: C2's voltage, C1's (its output), its last input
  float c0[6];                          // its step's matrix at the last step's end (N, then Mb)
  float dcx, dcy;                       // the coupling capacitor
};
static_assert(sizeof(Easel) <= MF_STATE_BYTES, "EASEL's state");
struct EaselAt {
  Easel& operator[](int t) const { return *static_cast<Easel*>(mf_state(t)); }
};
const EaselAt easel = {};

// the gate's circuit at the vactrol's current (its log), in a mode, as the paper writes it (eq. 5, 6: C2's voltage vx,
// C1's vo, its output): vx' = b1 u + b2 vx + b3 vo + b4 (d1 vo + d2 vx)', vo' = a1 vx + a2 vo; the derivative taken in
// (C3's term, LP's), a system of two states, x' = A x + B u, through the trapezoid (the bilinear transform, as the
// paper's TPT): x[n] = N x[n-1] + Mb (u[n] + u[n-1]), N = (I - hA)^-1 (I + hA), Mb = (I - hA)^-1 h B. Its states the
// circuit's: the gate shut, its output drains through R-alpha (C1's 5 ms), nothing held (kOhm, nF, us)
void gate_coefs(float ln_i, int mode, float* c) {
  const float rf = 3.464e-3f * fm_expf(-1.4f * ln_i) + 1.136212f;   // kOhm
  // (VCA: R-alpha 50 k, the paper's 5 k left its ping 20 dB under Combo's, the vactrol a few mA lit by a ping; 3 dB
  // more, in its input: VCA's ping 3 dB under Combo's, a long envelope 2 dB over)
  const float ra = mode == 0 ? 50.0f : 5000.0f;                      // kOhm
  const float c1 = 1.0f, c2 = 0.22f, c3 = mode == 2 ? 4.7f : 0.0f;    // nF
  const float a1 = 1.0f / (c1 * rf), a2 = -(1.0f / c1) * (1.0f / rf + 1.0f / ra);
  const float b = 1.0f / (c2 * rf), b4 = c3 / c2;                     // (b1 = b3 = b, b2 = -2 b; d1 1, d2 -1)
  const float id = 1.0f / (1.0f + b4);
  const float A00 = (-2.0f * b + b4 * a1) * id, A01 = (b + b4 * a2) * id, A10 = a1, A11 = a2, B0 = b * id;
  const float h = 0.5f * kTus;
  // I - hA, its inverse; then N and Mb
  const float m00 = 1.0f - h * A00, m01 = -h * A01, m10 = -h * A10, m11 = 1.0f - h * A11;
  const float idet = 1.0f / (m00 * m11 - m01 * m10);
  const float i00 = m11 * idet, i01 = -m01 * idet, i10 = -m10 * idet, i11 = m00 * idet;
  const float p00 = 1.0f + h * A00, p01 = h * A01, p10 = h * A10, p11 = 1.0f + h * A11;
  c[0] = i00 * p00 + i01 * p10, c[1] = i00 * p01 + i01 * p11;
  c[2] = i10 * p00 + i11 * p10, c[3] = i10 * p01 + i11 * p11;
  const float g = mode == 0 ? 1.41f : 1.0f;
  c[4] = i00 * h * B0 * g, c[5] = i10 * h * B0 * g;
}

// m samples: the oscillators, the folder (its ADAA), its lowpass, AM, then the gate gliding from its step's matrix c
// by dc6 a sample
__attribute__((noinline)) void voice(Easel& p, float* y, uint32_t m, float dc, float dm, float dev, float am, float A,
                                     float bias, const float* c, const float* dc6) {
  float pc = p.pc, pm = p.pm, x1 = p.x1, lpx = p.lpx, lpy = p.lpy, hx = p.hx, hy = p.hy;
  float vx = p.vx, vo = p.vo, u1 = p.u1;
  float n00 = c[0], n01 = c[1], n10 = c[2], n11 = c[3], b0 = c[4], b1 = c[5];
  const float lb = 0.086326f, la = -0.827349f;   // (the 259's 1.33 kHz: wc T / (2 + wc T), (wc T - 2) / (wc T + 2))
  for (uint32_t i = 0; i < m; ++i) {
    pm += dm;
    pm -= pm >= 1.0f ? 1.0f : 0.0f;
    const float mo = 1.0f - 4.0f * fm_fabsf(pm - 0.5f);   // the triangle, -1..1
    pc += dc + dev * mo;                                   // (through zero: the phase may go back)
    pc -= fm_floorf(pc);
    const float s = fm_sin_pi((pc < 0.5f ? pc : pc - 1.0f) * 6.2831853f);
    const float x = A * s + bias;
    // the folder through its antiderivative: (F(x) - F(x1)) / (x - x1), a cell's (d^2 - d1^2) / (x - x1). Both over
    // its threshold on one side, d - d1 is x - x1 (its sign x's): the cell (d + d1) / 2, no division (taken as the
    // difference, d - d1 lost its digits as x neared x1: clicks, a low note's folded crest). Crossing its threshold
    // (or zero), d and d1 under |x - x1|: the division as it is, well conditioned
    const float dx = x - x1, ax = fm_fabsf(x), a1 = fm_fabsf(x1), top = fm_maxf(ax, a1);
    const float sx = x < 0.0f ? -0.5f : 0.5f;
    const bool same = (x < 0.0f) == (x1 < 0.0f);
    float v = 0.5f * kDirect * (x + x1);
    for (int c = 0; c < 5 && top > kTh[c]; ++c) {   // (a cell under its threshold, both ends: nothing)
      const float d = fm_maxf(ax - kTh[c], 0.0f), d1 = fm_maxf(a1 - kTh[c], 0.0f);
      v += kC[c] * (d + d1) * (same && d > 0.0f && d1 > 0.0f ? sx : 0.5f * (d - d1) / dx);
    }
    x1 = x;
    lpy = lb * (v + lpx) - la * lpy;   // (the 259's output lowpass)
    lpx = v;
    hy = lpy - hx + 0.998576f * hy;    // (its DC out before the gate: a fold of FM's sidebands leaves one, the gate
    hx = lpy;                          // shutting would turn it into a thump)
    const float a = hy * (1.0f + am * mo);   // AM (the Easel's index: to the fully modulated sound, twice as loud)
    // the gate's circuit, a sample
    n00 += dc6[0], n01 += dc6[1], n10 += dc6[2], n11 += dc6[3], b0 += dc6[4], b1 += dc6[5];
    const float uu = a + u1, nx = n00 * vx + n01 * vo + b0 * uu;
    vo = n10 * vx + n11 * vo + b1 * uu;
    vx = nx;
    u1 = a;
    y[i] = vo;
  }
  p.pc = pc, p.pm = pm, p.x1 = x1, p.lpx = lpx, p.lpy = lpy, p.hx = hx, p.hy = hy, p.vx = vx, p.vo = vo, p.u1 = u1;
}

}  // namespace

extern "C" {

MI_COLD void easel_reset(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Easel& p = easel[t];
  uint8_t* raw = reinterpret_cast<uint8_t*>(&p);
  for (uint32_t i = 0; i < sizeof(Easel); ++i) raw[i] = 0;
  float d[8];
  easel_defaults(d);
  for (int i = 0; i < 8; ++i) p.k[i] = d[i];
  p.note = 48.0f;
  p.iv = kDark;
  p.ln_i = kLnDark;
  p.sv = p.tj = 1.0f;
  p.rng = 0x9E3779B9u * static_cast<uint32_t>(t + 1);
  p.pm = 0.25f;                         // (the two never in phase)
}

MI_COLD void easel_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) easel_reset(t);
  mf_forget();
}

MI_COLD void easel_silence(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Easel& p = easel[t];
  p.trig = p.alive = 0;
  p.env = p.hist = 0.0f;
  p.iv = kDark;
  p.ln_i = kLnDark;
  p.x1 = p.lpx = p.lpy = p.hx = p.hy = p.vx = p.vo = p.u1 = p.dcx = p.dcy = 0.0f;
}

MI_COLD void easel_set(int t, float note, const float* k) {
  if (t < 0 || t >= MF_TRACKS) return;
  Easel& p = easel[t];
  for (int i = 0; i < 8; ++i) p.k[i] = clampf(k[i], 0.0f, 1.0f);
  p.note = note;
}

MI_COLD void easel_gate(int t, int g) {
  if (t < 0 || t >= MF_TRACKS) return;
  Easel& p = easel[t];
  if (g && !p.gate) p.trig = 1;
  p.gate = g;
}

MI_COLD void easel_render(int t, float* out, uint32_t n) {   // (its glue for size: its samples' loop at -O2)
  if (t < 0 || t >= MF_TRACKS) return;
  Easel& p = easel[t];
  if (p.trig) {                         // a trig: the envelope up at once (the vactrol rises on its own); this strike's
    p.trig = 0;                         // own current and time
    p.env = 1.0f;
    uint32_t r = p.rng;
    r ^= r << 13, r ^= r >> 17, r ^= r << 5;
    p.rng = r;
    p.sv = 1.0f + 0.04f * (static_cast<float>(r >> 16) * (2.0f / 65535.0f) - 1.0f);
    p.tj = 1.0f + 0.04f * (static_cast<float>(r & 0xFFFFu) * (2.0f / 65535.0f) - 1.0f);
    if (!p.alive) {                     // (from rest: the gate's coefficients from closed)
      gate_coefs(p.ln_i, gate_of(p.k[4]), p.c0);
      p.alive = 1;
    }
  }
  if (!p.alive) {                       // (at rest: its history fading, the coupling capacitor's tail out, then none)
    if (p.hist > 1e-4f) p.hist *= fm_expf(static_cast<float>(n) * (-1.0f / (2.5f * kSr)));
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    if (fm_fabsf(p.dcy) < 1e-6f) {
      p.dcx = p.dcy = 0.0f;
      return;
    }
    mi_finish(out, n, 0.3f, &p.dcx, &p.dcy);
    return;
  }
  const float fc = note_hz(mi_tuned(p.note));   // (0.16.84: the microtonal SCALEs' tuning)
  const float ratio = kRatio[ratio_of(p.k[1])];
  const float dc = clampf(fc / kSr, 1e-6f, 0.45f), dm = clampf(dc * ratio, 1e-6f, 0.45f);
  const int mode = gate_of(p.k[4]);
  const float dk = p.k[3];
  const float tau_e = dk < kPing ? 0.001f : decay_s(dk);   // (a ping: the envelope a pulse)
  const float rate = 1.0f / ((dk < kPing ? ping_s(dk) : kVactrol) * p.tj);   // (the vactrol's fall: 1 / T)
  const float te = 2.0f * p.k[5] - 1.0f, me = 2.0f * p.k[6] - 1.0f, sym = 2.0f * p.k[7] - 1.0f;
  const float mod0 = 2.0f * p.k[2] - 1.0f;
  const float ttrack = 1.0f / (1.0f + fc * (1.0f / 3000.0f));   // (the hardware's timbre weaker up high)
  const float fe = fm_expf(-static_cast<float>(kSub) / (tau_e * kSr));
  for (uint32_t s0 = 0; s0 < n; s0 += kSub) {
    const uint32_t m = n - s0 < static_cast<uint32_t>(kSub) ? n - s0 : static_cast<uint32_t>(kSub);
    // the envelope, the LED's current; the cell: rising with it, or recombining (in pairs and alone: the exact step
    // of i' = -a i - b i^2), slower as its history fills; its light (0..1) for T.ENV and M.ENV, and for its history
    const float fs = m == static_cast<uint32_t>(kSub) ? 1.0f : static_cast<float>(m) * (1.0f / kSub);
    p.env *= fe;
    const float c = p.env * p.sv;
    const float lt = fm_logf(kDark + 0.04f * c);
    if (lt > p.ln_i) {
      const float tau = 0.012f / (1.0f + 24.0f * c) * (1.0f - 0.15f * p.hist);
      p.ln_i += (lt - p.ln_i) * (1.0f - fm_expf(-kStep * fs / tau));
      p.iv = fm_expf(p.ln_i);
    } else {
      const float sl = rate / (1.0f + kHist * p.hist), a = sl * kAlone, b = sl * kPairs;
      const float ea = fm_expf(-a * kStep * fs);
      p.iv = a * p.iv * ea / (a + b * p.iv * (1.0f - ea));
      p.ln_i = fm_logf(fm_maxf(p.iv, kDark));
    }
    const float light = clampf((p.ln_i - kLnMin) * (1.0f / kLnSpan), 0.0f, 1.0f);
    p.hist += (light - p.hist) * (light > p.hist ? kHUp : kHDown) * fs;
    // TIMBRE, MOD and their contours (the light)
    const float tu = clampf(p.k[0] + te * light, 0.0f, 1.0f);
    const float A = timbre_a(tu) * ttrack;
    const float mo = clampf(mod0 + me * light, -1.0f, 1.0f);
    const float dev = mo > 0.0f ? 8.0f * mo * mo * dm : 0.0f;   // (FM: its index times the modulator's rate)
    const float am = mo < 0.0f ? -mo : 0.0f;
    float cf[6], d6[6];
    gate_coefs(p.ln_i, mode, cf);
    const float im = 1.0f / static_cast<float>(m);
    for (int i = 0; i < 6; ++i) d6[i] = (cf[i] - p.c0[i]) * im;
    voice(p, out + s0, m, dc, dm, dev, am, A, sym * 0.75f * A, p.c0, d6);
    for (int i = 0; i < 6; ++i) p.c0[i] = cf[i];
  }
  if (p.env < 1e-4f && p.ln_i < -15.4f) {   // (its sound over, the gate shut: it rests, its states from rest)
    p.alive = 0;
    p.env = 0.0f;
    p.iv = kDark;
    p.ln_i = kLnDark;
    p.x1 = p.lpx = p.lpy = p.hx = p.hy = p.vx = p.vo = p.u1 = 0.0f;
  }
  // the coupling capacitor (~5 Hz), the level, a soft knee above 0.8 (at most 1.2)
  mi_finish(out, n, 0.3f, &p.dcx, &p.dcy);
}

const char* easel_knob_name(int k) {
  static const char* const kN[8] = {"Timbre", "Ratio", "Mod", "Decay", "Gate", "T.Env", "M.Env", "Symm"};
  return k >= 0 && k < 8 ? kN[k] : "";
}

MI_COLD void easel_defaults(float* k) {         // a ping: some fold, a little FM a fifth and an octave over, Combo
  static const float kD[8] = {0.3f, 13.5f / 24.0f, 0.62f, 0.22f, 0.5f, 0.7f, 0.65f, 0.5f};
  for (int i = 0; i < 8; ++i) k[i] = kD[i];
}

int easel_knob_steps(int k) { return k == 1 ? kRatios : k == 4 ? 3 : 128; }

MI_COLD void easel_knob_text(int k, float u, char* b, int n) {
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  const float c = 2.0f * u - 1.0f;      // (a bipolar knob)
  switch (k) {
    case 1: {                           // RATIO: x2.76
      const int r = static_cast<int>(kRatio[ratio_of(u)] * 100.0f + 0.5f);
      put_s(b, n, &at, "x");
      put_i(b, n, &at, r / 100);
      if (r % 100) {
        put_s(b, n, &at, ".");
        if (r % 100 < 10) put_s(b, n, &at, "0");
        put_i(b, n, &at, r % 10 ? r % 100 : r % 100 / 10);
      }
      return;
    }
    case 2:                             // MOD: AM its index, FM its index
      if (c > -0.01f && c < 0.01f) return put_s(b, n, &at, "Off");
      if (c < 0.0f) {
        put_s(b, n, &at, "AM ");
        put_i(b, n, &at, static_cast<int>(-c * 100.0f + 0.5f));
        return put_s(b, n, &at, "%");
      } else {
        const int x = static_cast<int>(80.0f * c * c + 0.5f);   // (the index, a tenth)
        put_s(b, n, &at, "FM ");
        put_i(b, n, &at, x / 10);
        put_s(b, n, &at, ".");
        return put_i(b, n, &at, x % 10);
      }
    case 3:                             // DECAY: a ping (the vactrol's fall), then the envelope's time
      if (u < kPing) {
        put_s(b, n, &at, "Ping ");
        return put_time(b, n, &at, ping_s(u));
      }
      return put_time(b, n, &at, decay_s(u));
    case 4: {
      static const char* const kG[3] = {"VCA", "Combo", "LP"};
      return put_s(b, n, &at, kG[gate_of(u)]);
    }
    case 5:
    case 6:
    case 7:                             // T.ENV, M.ENV, SYMM: -100..100 %
      if (c > -0.01f && c < 0.01f) return put_s(b, n, &at, "Off");
      put_s(b, n, &at, c > 0.0f ? "+" : "-");
      put_i(b, n, &at, static_cast<int>(fm_fabsf(c) * 100.0f + 0.5f));
      return put_s(b, n, &at, "%");
    default:                            // TIMBRE: 0..100
      put_i(b, n, &at, static_cast<int>(u * 100.0f + 0.5f));
      return;
  }
}

}  // extern "C"
