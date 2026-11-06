/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* (0.16.33) MIFM1's USB audio input as it is now: the master mix in stereo, two halves more in hand
 * (FELUCCA_UAC_DEEP 2, firmware/patches/fm1-base-usb-multi.patch on felucca-bsp's usb.c); (0.16.34) its frames the
 * app's (FELUCCA_UAC_SOURCE: the mix without the metronome, under full scale), not the master output's, which is
 * never read; (0.16.35) at 44.1 or 48 kHz, the host's choice (FELUCCA_UAC_48K; at 48 the app's resampled frames,
 * FELUCCA_UAC_SOURCE48). On a computer: the descriptors (2 channels, the two rates, EP4's 196 bytes), then at each
 * rate 60 s of streaming as on the FM-1, renders of 128 frames at the I2S clock (~44,117.6 Hz), each writing the ring
 * when it ends (95 % of its time: the deep ring's case; then a render now and then a whole block late), packets at
 * the host's 1 ms: every frame recorded once, in order, none repeated or lost; the ring's fill and its latency.
 * (0.16.37) Then 60 s more at each rate, no render late, for the packets' pace (uac_pace): a host that reads at the
 * stream's average rate needs how many frames in hand, past the first 5 s (the loop settled), so that the stream
 * never falls behind it: 1 or 2 (the extra frame one packet at a time); the band's edges alone, before, 6 (48-frame
 * packets, then a burst of 49s, ~every 260 ms: an iPhone's camera ran dry before each and filled in zeros).
 *   cc -std=c99 -O2 -Ibase/bsp/chip -Ibase/bsp/boards/fm1 tools/uac_stereo_test.c */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_UAC 1
#define FELUCCA_UAC_OUT 0
#define FELUCCA_UAC_48K 1
#define FELUCCA_UAC_MIDI_LAST 1
#define FELUCCA_UAC_DEEP 2
#define FELUCCA_UAC_CH 2
#define HALF_FRAMES 128u
static const int16_t *test_frames(uint32_t n);
static const int16_t *test_frames48(uint32_t n, uint32_t *m);
#define FELUCCA_UAC_SOURCE test_frames
#define FELUCCA_UAC_SOURCE48 test_frames48
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "usb.c"

#define CH 2u
static int fails;
static void fail(const char *what, long got, long want)
{
    if (fails++ < 12)
        printf("FAIL %s: got %ld, want %ld\n", what, got, want);
}

/* the render: the app's frames, 16-bit, frame f (from 1), channel c: f's 11 low bits and c; the master output beside
 * them something else (the tap must not take it). At 48 kHz the app's frames n * 160 / 147 of them, numbered so too */
static uint32_t made = 1, made48 = 1, acc48;
static int16_t app16[2u * HALF_FRAMES] __attribute__((aligned(4)));
static int16_t app48[2u * (HALF_FRAMES * 160u / 147u + 2u)] __attribute__((aligned(4)));
static const int16_t *test_frames(uint32_t n)
{
    (void)n;
    return app16;
}
static const int16_t *test_frames48(uint32_t n, uint32_t *m)
{
    uint32_t i, k = 0, c;
    for (i = 0; i < n; i++)
        for (acc48 += 160u; acc48 >= 147u; acc48 -= 147u, k++, made48++)
            for (c = 0; c < CH; c++)
                app48[k * CH + c] = (int16_t)(((made48 & 0x7FFu) << 4) | c);
    *m = k;
    return app48;
}
static void render_into(int32_t *o, uint32_t n)
{
    uint32_t i, c;
    for (i = 0; i < n; i++, made++)
        for (c = 0; c < CH; c++) {
            app16[i * CH + c] = (int16_t)(((made & 0x7FFu) << 4) | c);
            o[i * CH + c] = 0x7FFF00;
        }
}

static const uint8_t *find(const uint8_t *d, uint32_t len, uint8_t type, uint8_t sub)   /* a class descriptor */
{
    uint32_t i = 0;
    while (i + 2u < len && d[i]) {
        if (d[i + 1u] == type && (sub == 0xFFu || d[i + 2u] == sub))
            return d + i;
        i += d[i];
    }
    return 0;
}

/* 60 s at the rate hz (late: a render now and then a whole block late): the frames the host recorded; without late
 * renders, what a steady reader needs in hand */
static uint8_t sizes[60000];
static void run(uint32_t hz, int late)
{
    int16_t pkt[UA_MAXF * CH];
    uint32_t expect = 0, got = 0, maxb = 0, held = 0, nr = 0;
    double t, next_render = 0.0, next_pkt = 0.0003, tap_at = -1.0;
    const double render = HALF_FRAMES / 44117.647;   /* s */
    const uint32_t per_ms = hz / 1000u, maxp = (per_ms + 2u) * CH * 2u;

    uac_stream(0);
    uac_rate_set(hz);
    uac_stream(1);
    uac.underruns = uac.overruns = uac.missed = uac.adj_up = uac.adj_down = 0;
    uac.fill_lo = 0xFFFFFFFFu;
    uac.fill_hi = 0;
    uac.flowing = 1;                                  /* (the host took a first packet) */
    for (t = 0.0; t < 60.0;) {
        double nt = next_pkt < next_render ? next_pkt : next_render;
        if (tap_at >= 0.0 && tap_at < nt)
            nt = tap_at;
        t = nt;
        if (t == tap_at) {                            /* the render's end: its frames into the ring */
            int32_t o[2u * HALF_FRAMES];
            render_into(o, HALF_FRAMES);
            uac_tap(o, HALF_FRAMES);
            tap_at = -1.0;
        } else if (t == next_render) {                /* a render starts; it takes 95 % of its time, and every
                                                       * 400th starts a whole block late (its end with the next's) */
            uac_render_start();
            tap_at = t + (++nr % 400u || !late ? 0.95 : 1.95) * render;
            next_render += render;
            if (late && nr % 400u == 0u) {            /* (the late one: the next render starts after it) */
                next_render += render;
                tap_at = next_render - 0.05 * render;
            }
        } else {                                      /* the host's IN token, every ms */
            uint32_t n = uac_packet((uint32_t *)pkt), f, c, ms = (uint32_t)(t * 1000.0);
            if (ms < 60000u)
                sizes[ms] = (uint8_t)n;
            if (n * CH * 2u > maxb)
                maxb = n * CH * 2u;
            for (f = 0; f < n; f++) {
                const int16_t *s = &pkt[f * CH];
                if (!s[0] && !expect)                 /* the priming's silence, before the first frame */
                    continue;
                if (!expect)
                    expect = (uint32_t)s[0] >> 4;     /* (wherever the numbers are: the rate before went on) */
                if ((uint32_t)s[0] >> 4 == ((expect - 1u) & 0x7FFu) && got) {
                    held++;                           /* the last frame again: an underrun's hold */
                    continue;
                }
                for (c = 0; c < CH; c++)
                    if (s[c] != (int16_t)(((expect & 0x7FFu) << 4) | c)) {
                        fail("a frame's sample (frame, channel)", s[c], ((expect & 0x7FFu) << 4) | c);
                        break;
                    }
                expect++;
                got++;
            }
            next_pkt += 0.001;
        }
    }
    if (maxb > maxp)
        fail("the largest packet's bytes", (long)maxb, (long)maxp);
    if (held || uac.underruns || uac.overruns)
        fail("holds, underruns, overruns", (long)(held + uac.underruns + uac.overruns), 0);
    if (got < per_ms * 1000u * 59u)
        fail("frames recorded in 60 s", (long)got, (long)per_ms * 60000L);
    if (late) {
        printf("USB audio, stereo, the app's frames, two halves in hand, at %.1f kHz, a render late now and then: %u "
               "frames in order, packets up to %u B, ring fill %u..%u (%.1f ms at the least), %u up %u down\n",
               hz / 1000.0, (unsigned)got, (unsigned)maxb, (unsigned)uac.fill_lo, (unsigned)uac.fill_hi,
               uac.fill_lo / (hz / 1000.0), (unsigned)uac.adj_up, (unsigned)uac.adj_down);
        return;
    }
    {   /* the pace past 5 s: the stream against a straight line (its average rate, least squares), the gap between its
         * furthest ahead and furthest behind; the packets' sizes */
        double sx = 0, sy = 0, sxx = 0, sxy = 0, c = 0, a, b, lo = 1e9, hi = -1e9, m = 0;
        uint32_t k, cnt[64] = {0};
        for (k = 5000; k < 60000; k++) {
            c += sizes[k];
            sx += k, sy += c, sxx += (double)k * k, sxy += (double)k * c, m++;
            cnt[sizes[k] & 63u]++;
        }
        b = (m * sxy - sx * sy) / (m * sxx - sx * sx);
        a = (sy - b * sx) / m;
        for (c = 0, k = 5000; k < 60000; k++) {
            double d;
            c += sizes[k];
            d = c - (a + b * k);
            lo = d < lo ? d : lo;
            hi = d > hi ? d : hi;
        }
        printf("USB audio at %.1f kHz, the pace: %.4f frames a ms; packets of %u: %u, of %u: %u, others %u; a reader at "
               "that rate needs %.1f frames in hand\n", hz / 1000.0, b, hz / 1000u, (unsigned)cnt[hz / 1000u],
               hz / 1000u + 1u, (unsigned)cnt[hz / 1000u + 1u],
               (unsigned)(55000u - cnt[hz / 1000u] - cnt[hz / 1000u + 1u]), hi - lo);
        if (hi - lo > 2.0 || cnt[hz / 1000u] + cnt[hz / 1000u + 1u] != 55000u)
            fail("the pace: frames a steady reader needs in hand (x10)", (long)((hi - lo) * 10), 20);
    }
}

int main(void)
{
    const uint8_t *cfg = CFG_DESC_PLAIN, *p;
    uint32_t len = sizeof CFG_DESC_PLAIN, i;

    if (UAC_MAXP != 196u)
        fail("EP4's size", (long)UAC_MAXP, 196);
    if ((cfg[2] | cfg[3] << 8) != len)
        fail("the configuration's length", cfg[2] | cfg[3] << 8, (long)len);
    p = find(cfg, len, 0x24, 2);                      /* the input terminal (the first CS 0x02: AC input terminal) */
    if (!p || p[7] != CH || p[8] != 3 || p[9] != 0)
        fail("the input terminal's channels", p ? p[7] : -1, CH);
    for (i = 0, p = 0; i + 2u < len; i += cfg[i])     /* the format type (CS_INTERFACE 0x02 after an AS general) */
        if (cfg[i] == 14 && cfg[i + 1u] == 0x24 && cfg[i + 2u] == 2 && cfg[i + 3u] == 1)
            p = cfg + i;
    if (!p || p[4] != CH || p[5] != 2 || p[6] != 16 || p[7] != 2 || (p[8] | p[9] << 8 | p[10] << 16) != 44100 ||
        (p[11] | p[12] << 8 | p[13] << 16) != 48000)
        fail("the format: its channels, 16 bit, 44.1 and 48 kHz", p ? p[4] : -1, CH);
    for (i = 0, p = 0; i + 2u < len; i += cfg[i])
        if (cfg[i] == 9 && cfg[i + 1u] == 5 && cfg[i + 2u] == 0x84)
            p = cfg + i;
    if (!p || (p[4] | p[5] << 8) != 196)
        fail("EP4 IN's wMaxPacketSize", p ? (p[4] | p[5] << 8) : -1, 196);
    if (sizeof ua_ring != 1024u * 4u)
        fail("the ring's bytes", (long)sizeof ua_ring, 1024 * 4);

    usb.up = 1;
    usb.config = 1;
    run(44100u, 1);
    run(48000u, 1);
    run(44100u, 0);
    run(48000u, 0);
    if (fails)
        return 1;
    printf("ok\n");
    return 0;
}
