// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// MIFM1's drums (0.9), after Sonic Charge's Microtonic: each drum an oscillator and a noise, each with its own
// envelope, mixed and driven. The oscillator: a sine, a triangle or a saw (WAVE, between them), its pitch modulated
// (decaying from above or below it, a sine, or a random), an exponential attack (none: a click) and decay. The noise:
// white, through a state variable filter (low, band or high pass), its envelope exponential, linear (gated) or
// modulated (retriggered a few times before it falls: a clap). Then the mix and the drive (to the right odd harmonics,
// a hard edge; to the left even ones, a warm one; antialiased, ADAA).
//
// Six models, one engine: KICK, SNARE, CLAP, HAT, TOM, PERC differ by their kit (the oscillator's pitch at C3, the
// pitch modulation's mode, the oscillator's attack, the noise filter's mode and Q, the noise envelope's mode) and their
// knobs' defaults. The eight knobs, the same for all (the track's knobs after FREQ): WAVE, SWEEP (the pitch
// modulation's time, or rate), DECAY, FILTER (the noise's), N.DECAY, MIX (oscillator to noise), DRIVE, P.AMT (the
// pitch modulation's depth, up to four octaves; decaying: + from above, - from below). C3 plays the kit's pitch.
#include <stdint.h>
#include "fastmath.h"
#include "bridge.h"

namespace {

const float kSr = 44100.0f;
const float kPi = 3.14159265f;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
inline float sin_cyc(float ph) {        // a sine of a phase in cycles, 0..1 (9th-order fit on the quarter wave)
  float x = ph < 0.5f ? ph : ph - 0.5f, s = ph < 0.5f ? 1.0f : -1.0f;
  x = x < 0.25f ? x : 0.5f - x;
  float z = x * 4.0f, z2 = z * z;
  return s * z * (1.5707963f + z2 * (-0.64596336f + z2 * (0.079688469f + z2 * (-0.0046722113f + z2 * 0.00015081274f))));
}
inline float blep(float t, float dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0f;
  }
  if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

enum { PM_DECAY, PM_SINE, PM_RANDOM };
enum { NF_LP, NF_BP, NF_HP };
enum { NE_EXP, NE_LIN, NE_MOD };

struct Kit {
  const char* name;
  float f0;                             // the oscillator at C3 (Hz)
  uint8_t pmode;                        // its pitch modulation
  float o_att;                          // its attack (s; 0: a click)
  uint8_t nmode;                        // the noise filter
  float nq;                             // its Q
  uint8_t emode;                        // the noise envelope
  float n_att;                          // its attack (s; modulated: the bursts' time)
  float def[8];                         // the knobs (0..1; MIX, DRIVE, P.AMT from their middle)
};
const Kit kKits[DR_MODELS] = {
    {"KICK", 50.0f, PM_DECAY, 0.0f, NF_HP, 0.7f, NE_EXP, 0.0f, {0.0f, 0.35f, 0.55f, 0.72f, 0.06f, 0.12f, 0.62f, 0.8f}},
    {"SNARE", 185.0f, PM_DECAY, 0.0f, NF_HP, 0.9f, NE_EXP, 0.0f, {0.5f, 0.2f, 0.35f, 0.7f, 0.42f, 0.62f, 0.6f, 0.6f}},
    {"CLAP", 300.0f, PM_DECAY, 0.0f, NF_BP, 2.2f, NE_MOD, 0.03f, {0.5f, 0.2f, 0.15f, 0.62f, 0.45f, 0.95f, 0.58f, 0.5f}},
    {"HAT", 1100.0f, PM_SINE, 0.0f, NF_HP, 0.8f, NE_EXP, 0.0f, {1.0f, 0.8f, 0.14f, 0.86f, 0.22f, 0.8f, 0.55f, 0.75f}},
    {"TOM", 110.0f, PM_DECAY, 0.0f, NF_LP, 0.7f, NE_EXP, 0.0f, {0.15f, 0.4f, 0.5f, 0.5f, 0.1f, 0.2f, 0.55f, 0.65f}},
    {"PERC", 560.0f, PM_SINE, 0.0005f, NF_BP, 3.0f, NE_LIN, 0.0f, {0.75f, 0.7f, 0.38f, 0.7f, 0.1f, 0.3f, 0.65f, 0.68f}},
};
const char* const kKnob[8] = {"Wave", "Sweep", "Decay", "Filter", "N.Decay", "Mix", "Drive", "P.Amt"};

struct Drum {
  int model;
  float note, k[8];
  // the voice
  float ph;                             // the oscillator's phase (cycles)
  float oenv, oatt, ok, oa;             // its envelope: level, rising (1) or not, its fall a sample, its rise
  float penv, pk;                       // the pitch modulation: decaying, its fall a sample
  float mph, rlp;                       // the sine modulator's phase; the random one, lowpassed
  float nenv, nk, nlin, natt, na;       // the noise's envelope: level, its exponential fall, its linear step, rising
  float burst_t, burst_len;             // (modulated: the time into the bursts, between two)
  int bursts_left;
  float s1, s2;                         // the noise filter
  float x1, dc, dcy;                    // the drive's last input (ADAA), its DC blocker
  float click, last;                    // (a retrig over a sound: its last sample, faded out, no step)
  uint32_t rnd;
  int gate, trig, alive;
};
// (its state on a track: the engine's, shared with the other machines, one a track at a time)
static_assert(sizeof(Drum) <= MF_STATE_BYTES, "a drum's state");
struct DrumAt {
  Drum& operator[](int t) const { return *static_cast<Drum*>(mf_state(t)); }
};
const DrumAt drum = {};

inline uint32_t xs(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline float bip(uint32_t& s) { return static_cast<float>(static_cast<int32_t>(xs(s))) * (1.0f / 2147483648.0f); }

// the drive's curve: x - x^3 / 3 up to 1, then flat at 2/3; its antiderivative (ADAA)
inline float clip3(float x) { return x >= 1.0f ? 0.6666667f : x <= -1.0f ? -0.6666667f : x - x * x * x * (1.0f / 3.0f); }
inline float clip3_F(float x) {
  float a = fm_fabsf(x);
  return a <= 1.0f ? x * x * (0.5f - x * x * (1.0f / 12.0f)) : 0.6666667f * a - 0.25f;
}

}  // namespace

extern "C" {

void drum_reset(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  Drum& d = drum[t];
  uint8_t* raw = reinterpret_cast<uint8_t*>(&d);
  for (uint32_t i = 0; i < sizeof(Drum); ++i) raw[i] = 0;
  d.rnd = 0x6C078965u * static_cast<uint32_t>(t + 1);
  d.note = 48.0f;
  for (int i = 0; i < 8; ++i) d.k[i] = kKits[0].def[i];
}

void drum_init(void) {
  for (int t = 0; t < MF_TRACKS; ++t) drum_reset(t);
  mf_forget();
}

void drum_silence(int t) {
  if (t < 0 || t >= MF_TRACKS) return;
  drum[t].alive = 0, drum[t].trig = 0, drum[t].click = drum[t].last = 0.0f;
}

void drum_set(int t, int model, float note, const float* k) {
  if (t < 0 || t >= MF_TRACKS) return;
  Drum& d = drum[t];
  d.model = model < 0 ? 0 : model >= DR_MODELS ? DR_MODELS - 1 : model;
  d.note = note;
  for (int i = 0; i < 8; ++i) d.k[i] = clampf(k[i], 0.0f, 1.0f);
}

void drum_gate(int t, int g) {
  if (t < 0 || t >= MF_TRACKS) return;
  if (g && !drum[t].gate) drum[t].trig = 1;
  drum[t].gate = g;
}

// the knobs' meaning, shared by the render and the screen
static float sweep_s(float u) { return 0.003f * fm_exp2f(u * 8.966f); }        // decaying: 3 ms .. 1.5 s
static float sweep_hz(float u) { return 0.5f * fm_exp2f(u * 11.966f); }        // a sine: 0.5 Hz .. 2 kHz
static float rand_hz(float u) { return 2.0f * fm_exp2f(u * 13.288f); }         // random: lowpassed at 2 Hz .. 20 kHz
static float decay_s(float u) { return 0.01f * fm_exp2f(u * 8.644f); }         // 10 ms .. 4 s
static float filter_hz(float u) { return 30.0f * fm_exp2f(u * 9.229f); }       // 30 Hz .. 18 kHz

MI_RAM void drum_render(int t, float* out, uint32_t n) {
  if (t < 0 || t >= MF_TRACKS) return;
  Drum& d = drum[t];
  const Kit& kit = kKits[d.model];
  float wave = d.k[0], mix = 2.0f * d.k[5] - 1.0f, drv = 2.0f * d.k[6] - 1.0f, pamt = (2.0f * d.k[7] - 1.0f) * 48.0f;
  float odec = decay_s(d.k[2]), ndec = decay_s(d.k[4]);
  if (d.trig) {                         // a trig: everything from the top (the last sample faded out)
    d.trig = 0;
    d.click = d.alive ? d.last : 0.0f;
    d.ph = 0.0f;
    d.mph = 0.0f;
    d.oenv = kit.o_att > 0.0f ? 0.0f : 1.0f;
    d.oatt = kit.o_att > 0.0f ? 1.0f : 0.0f;
    d.penv = 1.0f;
    d.nenv = 0.0f;
    d.natt = 1.0f;
    d.burst_t = 0.0f;
    d.bursts_left = kit.emode == NE_MOD ? 3 : 0;
    d.alive = 1;
  }
  if (!d.alive) {
    for (uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
    return;
  }
  // the block's settings
  float f0 = kit.f0 * fm_exp2f((d.note - 48.0f) * (1.0f / 12.0f));
  d.ok = fm_expf(-5.0f / (odec * kSr));           // (5 time constants in DECAY: -43 dB)
  d.oa = kit.o_att > 0.0f ? 1.0f - fm_expf(-5.0f / (kit.o_att * kSr)) : 1.0f;
  d.pk = fm_expf(-5.0f / (sweep_s(d.k[1]) * kSr));
  float sine_inc = sweep_hz(d.k[1]) / kSr, rnd_g = 1.0f - fm_expf(-6.2831853f * rand_hz(d.k[1]) / kSr);
  d.nk = fm_expf(-5.0f / (ndec * kSr));
  d.nlin = 1.0f / (ndec * kSr);
  float nat = kit.emode == NE_MOD ? 0.0008f : kit.n_att;   // (the bursts: each rises in 0.8 ms)
  d.na = nat > 0.0f ? 1.0f - fm_expf(-5.0f / (nat * kSr)) : 1.0f;
  d.burst_len = kit.n_att / 3.5f * kSr;            // (modulated: three bursts, then the fall)
  float burst_k = fm_expf(-5.0f / (d.burst_len * 0.6f));
  float g = fm_tanf(kPi * clampf(filter_hz(d.k[3]), 20.0f, 18000.0f) / kSr), kq = 1.0f / kit.nq;
  float a1 = 1.0f / (1.0f + g * (g + kq)), a2 = g * a1, a3 = g * a2;
  float go = fm_sqrtf(0.5f * (1.0f - mix)), gn = fm_sqrtf(0.5f * (1.0f + mix)) * (kit.nmode == NF_BP ? 2.2f : 1.0f);
  float dg = 0.1f * fm_exp2f(fm_fabsf(drv) * 7.23f), bias = drv < 0.0f ? -0.5f * drv : 0.0f;   // (gain 0.1 .. 15)
  float fb = clip3(bias), up = clip3(0.7f * dg + bias) - fb, down = fb - clip3(bias - 0.7f * dg);
  float dnorm = 0.7f / (up > down ? up : down);   // (a 0.7 either way at 0.7 at most: biased, its larger side)
  float sw = wave < 0.5f ? wave * 2.0f : wave * 2.0f - 1.0f;
  for (uint32_t i = 0; i < n; ++i) {
    // the oscillator's pitch: the kit's, moved by its modulation
    float pm;
    if (kit.pmode == PM_DECAY) {
      pm = d.penv;
      d.penv *= d.pk;
    } else if (kit.pmode == PM_SINE) {
      pm = sin_cyc(d.mph);
      d.mph += sine_inc;
      d.mph -= d.mph >= 1.0f ? 1.0f : 0.0f;
    } else {
      d.rlp += (bip(d.rnd) - d.rlp) * rnd_g;
      pm = d.rlp * 2.0f;
    }
    float f = f0 * fm_exp2f(pamt * pm * (1.0f / 12.0f));
    float dt = clampf(f / kSr, 0.0f, 0.45f);
    // its wave: a sine, to a triangle, to a saw (polyBLEP on its edge)
    float p = d.ph, o, tri = p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
    if (wave < 0.5f) {
      o = sw < 1e-4f ? sin_cyc(p) : (1.0f - sw) * sin_cyc(p) + sw * tri;
    } else {
      float q = p + 0.5f;
      q -= q >= 1.0f ? 1.0f : 0.0f;
      o = (1.0f - sw) * tri + sw * (2.0f * q - 1.0f - blep(q, dt));
    }
    d.ph += dt;
    d.ph -= d.ph >= 1.0f ? 1.0f : 0.0f;
    // its envelope: an exponential rise (or none), an exponential fall
    if (d.oatt > 0.0f) {
      d.oenv += (1.02f - d.oenv) * d.oa;
      if (d.oenv >= 1.0f) d.oenv = 1.0f, d.oatt = 0.0f;
    } else {
      d.oenv *= d.ok;
    }
    // the noise, filtered (Zavalishin's state variable)
    float w = bip(d.rnd);
    float v3 = w - d.s2, v1 = a1 * d.s1 + a2 * v3, v2 = d.s2 + a2 * d.s1 + a3 * v3;
    d.s1 = 2.0f * v1 - d.s1;
    d.s2 = 2.0f * v2 - d.s2;
    float nz = kit.nmode == NF_LP ? v2 : kit.nmode == NF_BP ? kq * v1 : w - kq * v1 - v2;   // (the band: its peak at 1)
    // its envelope
    if (kit.emode == NE_MOD && d.bursts_left > 0) {   // the clap's bursts: each a quick rise and fall
      d.burst_t += 1.0f;
      if (d.burst_t >= d.burst_len) d.burst_t = 0.0f, --d.bursts_left, d.natt = 1.0f;
      if (d.natt > 0.0f) {
        d.nenv += (1.02f - d.nenv) * d.na;
        if (d.nenv >= 1.0f) d.nenv = 1.0f, d.natt = 0.0f;
      } else {
        d.nenv *= burst_k;
      }
    } else if (d.natt > 0.0f) {
      if (kit.emode == NE_LIN) d.nenv += d.na;
      else d.nenv += (1.02f - d.nenv) * d.na;
      if (d.nenv >= 1.0f) d.nenv = 1.0f, d.natt = 0.0f;
    } else if (kit.emode == NE_LIN) {
      d.nenv = d.nenv > d.nlin ? d.nenv - d.nlin : 0.0f;
    } else {
      d.nenv *= d.nk;
    }
    float x = go * d.oenv * o + gn * d.nenv * nz;
    // the drive: ADAA on the cubic clip (to the left, biased: even harmonics, its DC taken off)
    float xin = dg * x + bias, dx = xin - d.x1, y;
    y = fm_fabsf(dx) > 1e-4f ? (clip3_F(xin) - clip3_F(d.x1)) / dx : clip3(0.5f * (xin + d.x1));
    d.x1 = xin;
    y = (y - fb) * dnorm;
    if (bias > 0.0f) {                  // (a coupling capacitor at 8 Hz)
      float hp = y - d.dc + 0.99886f * d.dcy;
      d.dc = y;
      d.dcy = hp;
      y = hp;
    }
    d.click *= 0.85f;                   // (a retrig: the last sound's sample, gone in 0.2 ms)
    float v = 0.8f * y + d.click, e = fm_fabsf(v) - 0.8f;
    if (e > 0.0f) v = (v < 0.0f ? -1.0f : 1.0f) * (0.8f + 0.4f * e / (0.4f + e));   // (a soft knee: 1.2 at most)
    out[i] = v;
  }
  d.last = out[n - 1];
  if (d.oenv < 1e-5f && d.oatt <= 0.0f && d.nenv < 1e-5f && d.natt <= 0.0f && d.bursts_left <= 0) {
    d.alive = 0;
    d.click = d.last = 0.0f;
  }
}

const char* drum_model_name(int m) { return m >= 0 && m < DR_MODELS ? kKits[m].name : ""; }
const char* drum_knob_name(int k) { return k >= 0 && k < 8 ? kKnob[k] : ""; }
void drum_defaults(int m, float* k) {
  m = m < 0 ? 0 : m >= DR_MODELS ? DR_MODELS - 1 : m;
  for (int i = 0; i < 8; ++i) k[i] = kKits[m].def[i];
}
int drum_knob_steps(int k) { return k >= 0 ? 128 : 128; }   // (each knob its 128 values)

static void put_s(char* b, int n, int* at, const char* s) {
  while (*s && *at < n - 1) b[(*at)++] = *s++;
  b[*at] = 0;
}
static void put_i(char* b, int n, int* at, int x) {
  char t[8];
  int i = 0;
  if (x < 0) put_s(b, n, at, "-"), x = -x;
  do {
    t[i++] = static_cast<char>('0' + x % 10);
    x /= 10;
  } while (x && i < 7);
  while (i && *at < n - 1) b[(*at)++] = t[--i];
  b[*at] = 0;
}
static void put_time(char* b, int n, int* at, float s) {   // 12ms, 340ms, 1.2s
  if (s < 0.9995f) {
    put_i(b, n, at, static_cast<int>(s * 1000.0f + 0.5f));
    put_s(b, n, at, "ms");
  } else {
    int x = static_cast<int>(s * 10.0f + 0.5f);
    put_i(b, n, at, x / 10);
    put_s(b, n, at, ".");
    put_i(b, n, at, x % 10);
    put_s(b, n, at, "s");
  }
}
static void put_hz(char* b, int n, int* at, float hz) {   // 0.5Hz, 85Hz, 2.4k
  if (hz < 9.95f) {
    int x = static_cast<int>(hz * 10.0f + 0.5f);
    put_i(b, n, at, x / 10);
    put_s(b, n, at, ".");
    put_i(b, n, at, x % 10);
    put_s(b, n, at, "Hz");
  } else if (hz < 999.5f) {
    put_i(b, n, at, static_cast<int>(hz + 0.5f));
    put_s(b, n, at, "Hz");
  } else {
    int x = static_cast<int>(hz * 0.01f + 0.5f);
    put_i(b, n, at, x / 10);
    put_s(b, n, at, ".");
    put_i(b, n, at, x % 10);
    put_s(b, n, at, "k");
  }
}

void drum_knob_text(int model, int k, float u, char* b, int n) {   // u: 0..1 (a bipolar knob from its middle)
  if (n <= 0) return;
  int at = 0;
  b[0] = 0;
  u = clampf(u, 0.0f, 1.0f);
  const Kit& kit = kKits[model < 0 ? 0 : model >= DR_MODELS ? DR_MODELS - 1 : model];
  switch (k) {
    case 0:
      if (u < 0.02f) return put_s(b, n, &at, "Sine");
      if (u > 0.48f && u < 0.52f) return put_s(b, n, &at, "Tri");
      if (u > 0.98f) return put_s(b, n, &at, "Saw");
      break;
    case 1:
      if (kit.pmode == PM_DECAY) return put_time(b, n, &at, sweep_s(u));
      return put_hz(b, n, &at, kit.pmode == PM_SINE ? sweep_hz(u) : rand_hz(u));
    case 2: case 4: return put_time(b, n, &at, decay_s(u));
    case 3: return put_hz(b, n, &at, filter_hz(u));
    case 5: {                           // MIX: the oscillator, the noise
      int x = static_cast<int>((2.0f * u - 1.0f) * 100.0f + (u < 0.5f ? -0.5f : 0.5f));
      if (x <= -99) return put_s(b, n, &at, "Osc");
      if (x >= 99) return put_s(b, n, &at, "Noise");
      if (x > 0) put_s(b, n, &at, "+");
      put_i(b, n, &at, x);
      put_s(b, n, &at, "%");
      return;
    }
    case 6: case 7: {                   // DRIVE (- warm, + hard), P.AMT (semitones)
      float v = 2.0f * u - 1.0f;
      int x = k == 6 ? static_cast<int>(v * 100.0f + (v < 0.0f ? -0.5f : 0.5f))
                     : static_cast<int>(v * 48.0f + (v < 0.0f ? -0.5f : 0.5f));
      if (x > 0) put_s(b, n, &at, "+");
      put_i(b, n, &at, x);
      put_s(b, n, &at, k == 6 ? "%" : "st");
      return;
    }
    default:
      break;
  }
  put_i(b, n, &at, static_cast<int>(u * 100.0f + 0.5f));
  put_s(b, n, &at, "%");
}

}  // extern "C"
