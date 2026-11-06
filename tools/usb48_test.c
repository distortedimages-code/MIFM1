/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* (0.16.35) The USB audio input's 44.1 -> 48 kHz resampler (firmware/usb48.h), on a computer: its taps against the
 * same filter designed in double (the FM-1's fast sine and the series' Bessel function, no libm there); its frames,
 * n * 160 / 147 to a frame over 10 s of 128-frame blocks; then tones through it at 48 kHz, each measured by its own
 * sine and cosine over 2 s: the response (1 to 20 kHz), what comes back of a tone's image folded into the band (a
 * 5 and a 10 kHz tone: their images at 39.1 and 34.1 kHz fold to 8.9 and 13.9 kHz), and the rest of a 1 kHz tone
 * (the noise, the 16-bit's floor, and its own images, the 43.1 kHz one folded to 4.9: 80 dB under it at least).
 *   cc -std=c99 -O2 -Ibase/include -Ifirmware tools/usb48_test.c -lm */
#include <stdio.h>
#include <math.h>
#include "usb48.h"
#define PI 3.14159265358979323846

static r48_t rs;
static int fails;

static double bessel_i0(double x)
{
    double s = 1.0, t = 1.0, q = 0.25 * x * x;
    for (int k = 1; k < 60; k++)
        {
        t *= q / ((double)k * k);
        s += t;
    }
    return s;
}

/* a tone at f Hz (44.1 kHz in), amplitude a, 2 s after 0.2 s: at 48 kHz, the amplitude at g Hz (its sine and cosine
 * fitted), and the rest's RMS once the tone at f (g = f) is taken out */
static double tone(double f, double a, double g, double *rest)
{
    static float l[128], r[128];
    static int16_t out[2 * R48_MAX(128)];
    static double y[200000];
    uint32_t got = 0;
    r48_init(&rs);
    for (uint32_t b = 0; b < 2.4 * 44100 / 128; b++) {
        for (int i = 0; i < 128; i++)
            l[i] = r[i] = (float)(a * sin(2 * PI * f * (b * 128 + i) / 44100.0));
        uint32_t m = r48_run(&rs, l, r, 128, out, 32767.0f);
        for (uint32_t i = 0; i < m && got < 200000; i++)
            y[got++] = out[2 * i] / 32767.0;
    }
    /* output frame k is at input time k * 147 / 160 (the first at 0): the filter's delay is (160 * 24 - 1) / 2 / 160 */
    uint32_t k0 = 9600, k1 = k0 + 96000;
    double ss = 0, sc = 0, n = k1 - k0;
    for (uint32_t k = k0; k < k1; k++) {
        double t = k / 48000.0;
        ss += y[k] * sin(2 * PI * g * t);
        sc += y[k] * cos(2 * PI * g * t);
    }
    ss *= 2 / n;
    sc *= 2 / n;
    if (rest) {
        double e = 0;
        for (uint32_t k = k0; k < k1; k++) {
            double t = k / 48000.0, v = y[k] - ss * sin(2 * PI * g * t) - sc * cos(2 * PI * g * t);
            e += v * v;
        }
        *rest = sqrt(e / n);
    }
    return sqrt(ss * ss + sc * sc);
}

int main(void)
{
    /* the taps against double's */
    double fc = 2.0 * 22500.0 / 44100.0, mid = 0.5 * (160 * 24 - 1), err = 0;
    r48_init(&rs);
    for (int p = 0; p < 80; p++) {
        double c[24], sum = 0;
        for (int j = 0; j < 24; j++) {
            double k = p + j * 160, x = fc * (k - mid) / 160, u = (k - mid) / mid;
            c[j] = fc * (fabs(x) < 1e-9 ? 1 : sin(PI * x) / (PI * x)) * bessel_i0(7.5 * sqrt(1 - u * u)) /
                   bessel_i0(7.5);
            sum += c[j];
        }
        for (int j = 0; j < 24; j++)
            err = fmax(err, fabs(c[j] / sum - rs.c[p][j]));
    }
    if (err > 2e-6)
        fails++, printf("FAIL the taps: %.1e off double's\n", err);

    /* its frames: 10 s of blocks */
    {
        static float z[128];
        static int16_t out[2 * R48_MAX(128)];
        uint64_t in = 0, outn = 0;
        uint32_t lo = 999, hi = 0;
        r48_init(&rs);
        for (int b = 0; b < 3446; b++) {
            uint32_t m = r48_run(&rs, z, z, 128, out, 1.0f);
            lo = m < lo ? m : lo;
            hi = m > hi ? m : hi;
            in += 128;
            outn += m;
        }
        double want = (double)in * 160 / 147;
        if (fabs((double)outn - want) > 1.0 || lo < 139 || hi > 140)
            fails++, printf("FAIL the frames: %llu for %.1f, %u..%u a block\n", (unsigned long long)outn, want, lo, hi);
        printf("usb48: taps %.1e off double's; %llu frames in, %llu out (x 160 / 147: %.1f), %u..%u a block\n", err,
               (unsigned long long)in, (unsigned long long)outn, want, lo, hi);
    }

    /* the response */
    {
        static const double F[6] = {1000, 10000, 16000, 18000, 19000, 20000};
        static const double LO[6] = {-0.02, -0.02, -0.02, -0.05, -0.15, -0.8};
        printf("usb48: response");
        for (int i = 0; i < 6; i++) {
            double db = 20 * log10(tone(F[i], 0.5, F[i], 0) / 0.5);
            printf(" %.0f kHz %.2f dB%s", F[i] / 1000, db, i < 5 ? "," : ";");
            if (db < LO[i] || db > 0.02)
                fails++, printf(" (FAIL)");
        }
        /* the images folded back: 5 kHz -> 39.1 kHz -> 8.9; 10 kHz -> 34.1 -> 13.9 */
        double i5 = 20 * log10(tone(5000, 0.5, 48000 - (44100 - 5000), 0) / 0.5 + 1e-12);
        double i10 = 20 * log10(tone(10000, 0.5, 48000 - (44100 - 10000), 0) / 0.5 + 1e-12);
        double rest, a1 = tone(1000, 0.5, 1000, &rest), snr = 20 * log10(a1 / sqrt(2) / rest);
        printf(" images 5 kHz's %.0f dB, 10 kHz's %.0f dB; a 1 kHz tone at -6 dB, what is not it %.0f dB under\n", i5,
               i10, snr);
        if (i5 > -75 || i10 > -75 || snr < 80)
            fails++, printf("FAIL the images or the rest\n");
    }
    if (fails)
        return 1;
    printf("ok\n");
    return 0;
}
