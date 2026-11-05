// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// ROBOVOX's check (0.16.17): its filters against MAME's (votrax.cpp's builders, Olivier Galibert's, BSD-3-Clause,
// here in double, their 3rd-order forms as MAME runs them), code by code, by their impulse responses; its vowels'
// formants where the table puts them; WAVs to hear (a vowel each, the words).
//   g++ -O2 -Ifirmware -Ifirmware/mi -Ibase/include tools/robovox/vox_check.cc -o build/vox_check && build/vox_check [DIR]
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../firmware/mi/mi_robovox.cc"

static uint8_t g_state[4][MF_STATE_BYTES] __attribute__((aligned(8)));
static float g_mem[4][8192];
extern "C" void* mf_state(int t) { return g_state[t]; }
extern "C" void* mf_voice_mem(int t, uint32_t* bytes, uint32_t* epoch) {
  *bytes = sizeof g_mem[t], *epoch = 0;
  return g_mem[t];
}

// ---- MAME's, in double (transcribed from votrax.cpp) ----
struct Ref {
  double a[4], b[4];
  int na, nb;
  double x[4], y[4];
  double run(double v) {
    for (int i = 3; i > 0; --i) x[i] = x[i - 1];
    x[0] = v;
    double t = 0;
    for (int i = 0; i < na; ++i) t += x[i] * a[i];
    for (int i = 1; i < nb; ++i) t -= y[i - 1] * b[i];
    t /= b[0];
    for (int i = 3; i > 0; --i) y[i] = y[i - 1];
    y[0] = t;
    return t;
  }
};
static const double PI = 3.14159265358979323846;
static void ref_std(Ref& r, double cc, double sr, double c1t, double c1b, double c2t, double c2b, double c3, double c4) {
  double k0 = c1t / (cc * c1b), k1 = c4 * c2t / (cc * c1b * c3), k2 = c4 * c2b / (cc * cc * c1b * c3);
  double fpeak = sqrt(fabs(k0 * k1 - k2)) / (2 * PI * k2), zc = 2 * PI * fpeak / tan(PI * fpeak / sr);
  double m0 = zc * k0, m1 = zc * k1, m2 = zc * zc * k2;
  memset(&r, 0, sizeof r);
  r.a[0] = 1 + m0, r.a[1] = 3 + m0, r.a[2] = 3 - m0, r.a[3] = 1 - m0;
  r.b[0] = 1 + m1 + m2, r.b[1] = 3 + m1 - m2, r.b[2] = 3 - m1 - m2, r.b[3] = 1 - m1 + m2;
  r.na = r.nb = 4;
}
// (the shaper: MAME's k1 and k2 multiply by the clock where its switched capacitors divide; here as ROBOVOX reads it,
// in double: each switched capacitor a resistor 1 / (C f))
static void ref_shaper(Ref& r, double cc, double sr, double c1, double c2t, double c2b, double c3, double c4) {
  double k0 = c3 / (c4 * cc), k1 = c2t / (c2b * cc), k2 = c1 * c3 / (c4 * c2b * cc * cc);
  double fpeak = 1 / (2 * PI * sqrt(k2)), zc = 2 * PI * fpeak / tan(PI * fpeak / sr);
  double m0 = zc * k0, m1 = zc * k1, m2 = zc * zc * k2;
  memset(&r, 0, sizeof r);
  r.a[0] = m0, r.a[1] = 0, r.a[2] = -m0;
  r.b[0] = 1 + m1 + m2, r.b[1] = 2 - 2 * m2, r.b[2] = 1 - m1 + m2;
  r.na = r.nb = 3;
}
static void ref_lowpass(Ref& r, double cc, double sr, double c1t, double c1b) {
  double k = c1b / (cc * c1t) * (150.0 / 4000.0), fpeak = 1 / (2 * PI * k), zc = 2 * PI * fpeak / tan(PI * fpeak / sr);
  double m = zc * k;
  memset(&r, 0, sizeof r);
  r.a[0] = 1, r.b[0] = 1 + m, r.b[1] = 1 - m;
  r.na = 1, r.nb = 2;
}
static void ref_injection(Ref& r, double cc, double sr, double c1b, double c2t, double c2b, double c3) {
  double k0 = cc * c2t, k1 = cc * (c1b * c3 / c2t + c2t), k2 = c2b, zc = 2 * sr, m = zc * k2;
  memset(&r, 0, sizeof r);
  r.a[0] = k0 + m, r.a[1] = k0 - m, r.b[0] = k1 + m, r.b[1] = k1 - m;
  r.na = r.nb = 2;
}

// the worst difference of two impulse responses (2048 samples), over the reference's peak
template <class F>
static double compare(Ref& r, F& f, float (*run)(F&, float)) {
  double worst = 0, peak = 1e-30;
  for (int i = 0; i < 2048; ++i) {
    const double x = i == 0 ? 1.0 : 0.0, a = r.run(x), b = run(f, static_cast<float>(x));
    worst = fmax(worst, fabs(a - b));
    peak = fmax(peak, fabs(a));
  }
  return worst / peak;
}
static float run_bq(Bq& f, float x) { return bq(f, x); }
static float run_op(Op& f, float x) { return op(f, x); }

static void wav(const char* path, const float* x, int n) {
  FILE* f = fopen(path, "wb");
  if (!f) return;
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f), u32(36 + n * 2), fwrite("WAVEfmt ", 1, 8, f), u32(16), u16(1), u16(1), u32(44100),
      u32(88200), u16(2), u16(16), fwrite("data", 1, 4, f), u32(n * 2);
  float pk = 1e-6f;                     // (normalized: its peak at -1 dB)
  for (int i = 0; i < n; ++i) pk = fmaxf(pk, fabsf(x[i]));
  for (int i = 0; i < n; ++i) {
    float v = x[i] * 0.89f / pk;
    v = v > 1 ? 1 : v < -1 ? -1 : v;
    u16(static_cast<uint16_t>(static_cast<int16_t>(v * 32767)));
  }
  fclose(f);
}

// the analog filter's peak (Hz) at the chip's clock: F1 (which 1) or F2 (2) of a code
static double std_peak_hz(int which, int code, int q) {
  double c1t, c1b, c2t, c2b, c3, c4, cc = 20000.0;
  if (which == 1) c1t = 11247, c1b = 11797, c2t = 949, c2b = 52067, c3 = 2280 + caps(code, kF1c, 4), c4 = 166272;
  else c1t = 24840, c1b = 29154, c2t = 829 + caps(q, kF2qc, 4), c2b = 38180, c3 = 2352 + caps(code, kF2c, 5), c4 = 34270;
  double k0 = c1t / (cc * c1b), k1 = c4 * c2t / (cc * c1b * c3), k2 = c4 * c2b / (cc * cc * c1b * c3), best = 0, fb = 0;
  for (double f = 40; f < 9000; f *= 1.002) {
    double w = 2 * PI * f, h = hypot(1, k0 * w) / hypot(1 - k2 * w * w, k1 * w);
    if (h > best) best = h, fb = f;
  }
  return fb;
}
static float buf[44100 * 8];
static double level(const float* x, int n, double hz) {
  double re = 0, im = 0, w = 2 * PI * hz / 44100.0;
  for (int i = 0; i < n; ++i) {
    double h = 0.5 - 0.5 * cos(2 * PI * i / n), v = x[i] * h;
    re += v * cos(w * i), im -= v * sin(w * i);
  }
  return sqrt(re * re + im * im) / (0.25 * n);
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : nullptr;
  int fails = 0;
  // 1. the filters, code by code, at the chip's clock and at TRACT's ends
  const double sr = 44100.0;
  double worst = 0;
  for (float cc : {20000.0f, 10000.0f, 40000.0f}) {
    for (int c = 0; c < 16; ++c) {
      Ref r;
      Bq f = {};
      ref_std(r, cc, sr, 11247, 11797, 949, 52067, 2280 + caps(c, kF1c, 4), 166272);
      std_filter(f, cc, 11247, 11797, 949, 52067, 2280 + caps(c, kF1c, 4), 166272);
      worst = fmax(worst, compare(r, f, run_bq));
      ref_std(r, cc, sr, 0, 17594, 868, 18828, 8480 + caps(c, kF3c, 4), 50019);
      std_filter(f, cc, 0, 17594, 868, 18828, 8480 + caps(c, kF3c, 4), 50019);
      worst = fmax(worst, compare(r, f, run_bq));
    }
    for (int c = 0; c < 32; ++c)
      for (int q = 0; q < 16; q += 3) {
        Ref r;
        Bq f = {};
        Op o = {};
        const float qq = 829 + caps(q, kF2qc, 4), c2 = 2352 + caps(c, kF2c, 5);
        ref_std(r, cc, sr, 24840, 29154, qq, 38180, c2, 34270);
        std_filter(f, cc, 24840, 29154, qq, 38180, c2, 34270);
        worst = fmax(worst, compare(r, f, run_bq));
        ref_injection(r, cc, sr, 29154, qq, 38180, c2);
        injection(o, cc, 29154, qq, 38180, c2);
        worst = fmax(worst, compare(r, o, run_op));
      }
    Ref r;
    Bq f = {};
    Op o = {};
    ref_std(r, cc, sr, 0, 28810, 1165, 21457, 8558, 7289);
    std_filter(f, cc, 0, 28810, 1165, 21457, 8558, 7289);
    worst = fmax(worst, compare(r, f, run_bq));
    ref_lowpass(r, cc, sr, 1122, 23131);
    lowpass(o, cc, 1122, 23131);
    worst = fmax(worst, compare(r, o, run_op));
    ref_shaper(r, cc, sr, 15500, 14854, 8450, 9523, 14083);
    f = Bq{};
    shaper(f, cc, 15500, 14854, 8450, 9523, 14083);
    const double ws = compare(r, f, run_bq);
    printf("the noise shaper at %.0f Hz: %.2e\n", cc, ws);
    worst = fmax(worst, ws);
  }
  printf("filters against MAME's: worst %.2e of the peak %s\n", worst, worst < 1e-2 ? "ok" : "WRONG");
  if (worst >= 1e-2) ++fails;

  // 2. the vowels: each held 1 s at A2; F1, F2 measured (the strongest harmonic near each) against the table
  float k[8];
  vox_defaults(k);
  k[0] = 0.1f;                          // Phon
  int bad = 0;
  for (int p = 1; p < 64; ++p) {
    if (!is_vowel(p)) continue;
    k[1] = (p + 0.5f) / 64.0f;
    vox_reset(0);
    vox_set(0, 45.0f, k);
    vox_gate(0, 1);
    for (int i = 0; i < 44100; i += 128) vox_render(0, &buf[i], 128);
    double e = 0, pk = 0;
    for (int i = 22050; i < 44100; ++i) e += buf[i] * buf[i], pk = fmax(pk, fabs(buf[i]));
    // the formants: the strongest harmonic of 110 Hz in each one's range
    const VoxPhon& r = kVoxPhon[p];
    double L[40] = {0}, f1 = 0, l1 = 0, f2 = 0;
    for (int h = 1; h < 40 && h * 110.0 < 4000; ++h) L[h] = level(&buf[22050], 16384, h * 110.0);
    for (int h = 1; h * 110.0 < 900; ++h)
      if (L[h] > l1) l1 = L[h], f1 = h * 110.0;
    const double t1 = std_peak_hz(1, r.f1, r.f2q), t2 = std_peak_hz(2, 2 * r.f2, r.f2q);
    // F2: a peak of the harmonics (above F1's, louder than its neighbours) nearest its place
    for (int h = 2; h < 38; ++h) {
      const double f = h * 110.0;
      if (f < 1.3 * f1 || L[h] < L[h - 1] || L[h] < L[h + 1]) continue;
      if (!f2 || fabs(log2(f / t2)) < fabs(log2(f2 / t2))) f2 = f;
    }
    if (!f2) f2 = 1;
    const int ok = fabs(log2(f1 / t1)) < 0.45 && fabs(log2(f2 / t2)) < 0.45 && sqrt(e / 22050) > 0.01;
    if (!ok) ++bad;
    printf("  %-4s rms %.3f pk %.2f  F1 %4.0f (%4.0f)  F2 %4.0f (%4.0f) %s\n", kVoxPhonName[p], sqrt(e / 22050), pk, f1, t1, f2, t2,
           ok ? "" : "<-");
    if (dir) {
      char path[256];
      snprintf(path, sizeof path, "%s/phon_%02d_%s.wav", dir, p, kVoxPhonName[p][0] == ':' ? kVoxPhonName[p] + 1
                                                                                         : kVoxPhonName[p]);
      wav(path, buf, 44100);
    }
  }
  printf("vowels: %d off their formants %s\n", bad, bad <= 3 ? "ok" : "WRONG");
  if (bad > 3) ++fails;
  // 3. the words (a WAV of them all, a syllable a 1/8 at 120)
  if (dir) {
    vox_defaults(k);
    k[0] = 0.5f;
    const int step = 11025;             // (an 1/8 at 120)
    int at = 0;
    vox_reset(0);
    for (int w = 0; w < kVoxWords && at < 44100 * 7; ++w) {
      k[1] = (w + 0.5f) / kVoxWords;
      int ns = 0;
      for (int i = kVoxWordAt[w]; i < kVoxWordAt[w + 1]; ++i) ns += kVoxWordPh[i] >> 7;
      for (int s = 0; s < ns; ++s) {
        vox_set(0, 45.0f + ((s * 5) % 12), k);
        vox_gate(0, 1);
        for (int i = 0; i < step * 3 / 4; i += 128) vox_render(0, &buf[at + i], 128);
        vox_gate(0, 0);
        for (int i = step * 3 / 4; i < step; i += 128) vox_render(0, &buf[at + i], 128);
        at += step;
      }
      for (int i = 0; i < step; i += 128) vox_render(0, &buf[at + i], 128);
      at += step;
    }
    char path[256];
    snprintf(path, sizeof path, "%s/words.wav", dir);
    wav(path, buf, at);
  }
  printf(fails ? "%d FAILURES\n" : "vox: all good\n", fails);
  return fails ? 1 : 0;
}
