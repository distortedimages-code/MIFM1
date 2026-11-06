/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* (0.16.33) MIFM1's USB audio input as it is now: the master output in stereo, two halves more in hand
 * (FELUCCA_UAC_DEEP 2, firmware/patches/fm1-base-usb-multi.patch on felucca-bsp's usb.c), on a computer: the
 * descriptors (2 channels, EP4's 184 bytes), then 60 s of streaming as on the FM-1, renders of 128 frames at the I2S
 * clock (~44,117.6 Hz), each writing the ring when it ends (95 % of its time: the deep ring's case; then a render
 * now and then a whole block late), packets at the host's 1 ms: every frame recorded once, in order, none repeated
 * or lost; the ring's fill and its latency.
 *   cc -std=c99 -O2 -Ibase/bsp/chip -Ibase/bsp/boards/fm1 tools/uac_stereo_test.c */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_UAC 1
#define FELUCCA_UAC_OUT 0
#define FELUCCA_UAC_48K 0
#define FELUCCA_UAC_MIDI_LAST 1
#define FELUCCA_UAC_DEEP 2
#define FELUCCA_UAC_CH 2
#define HALF_FRAMES 128u
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "usb.c"

#define CH 2u
static int fails;
static void fail(const char *what, long got, long want)
{
    if (fails++ < 12)
        printf("FAIL %s: got %ld, want %ld\n", what, got, want);
}

/* the render's frames, 24-bit as the I2S takes them: frame f (from 1), channel c: f's 11 low bits and c */
static uint32_t made = 1;
static void render_into(int32_t *o, uint32_t n)
{
    uint32_t i, c;
    for (i = 0; i < n; i++, made++)
        for (c = 0; c < CH; c++)
            o[i * CH + c] = (int32_t)((int16_t)(((made & 0x7FFu) << 4) | c)) * 256;
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

int main(void)
{
    const uint8_t *cfg = CFG_DESC_PLAIN, *p;
    uint32_t len = sizeof CFG_DESC_PLAIN, i;
    int16_t pkt[UA_MAXF * CH];
    uint32_t expect = 0, got = 0, maxb = 0, held = 0;
    double t, next_render = 0.0, next_pkt = 0.0003, tap_at = -1.0;
    const double render = HALF_FRAMES / 44117.647;   /* s */

    if (UAC_MAXP != 184u)
        fail("EP4's size", (long)UAC_MAXP, 184);
    if ((cfg[2] | cfg[3] << 8) != len)
        fail("the configuration's length", cfg[2] | cfg[3] << 8, (long)len);
    p = find(cfg, len, 0x24, 2);                      /* the input terminal (the first CS 0x02: AC input terminal) */
    if (!p || p[7] != CH || p[8] != 3 || p[9] != 0)
        fail("the input terminal's channels", p ? p[7] : -1, CH);
    for (i = 0, p = 0; i + 2u < len; i += cfg[i])     /* the format type (CS_INTERFACE 0x02 after an AS general) */
        if (cfg[i] == 11 && cfg[i + 1u] == 0x24 && cfg[i + 2u] == 2 && cfg[i + 3u] == 1)
            p = cfg + i;
    if (!p || p[4] != CH || p[5] != 2 || p[6] != 16)
        fail("the format's channels", p ? p[4] : -1, CH);
    for (i = 0, p = 0; i + 2u < len; i += cfg[i])
        if (cfg[i] == 9 && cfg[i + 1u] == 5 && cfg[i + 2u] == 0x84)
            p = cfg + i;
    if (!p || (p[4] | p[5] << 8) != 184)
        fail("EP4 IN's wMaxPacketSize", p ? (p[4] | p[5] << 8) : -1, 184);
    if (sizeof ua_ring != 1024u * 4u)
        fail("the ring's bytes", (long)sizeof ua_ring, 1024 * 4);

    usb.up = 1;
    usb.config = 1;
    uac_stream(1);
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
            static uint32_t nr;
            uac_render_start();
            tap_at = t + (++nr % 400u ? 0.95 : 1.95) * render;
            next_render += render;
            if (nr % 400u == 0u) {                    /* (the late one: the next render starts after it) */
                next_render += render;
                tap_at = next_render - 0.05 * render;
            }
        } else {                                      /* the host's IN token, every ms */
            uint32_t n = uac_packet((uint32_t *)pkt), f, c;
            if (n * CH * 2u > maxb)
                maxb = n * CH * 2u;
            for (f = 0; f < n; f++) {
                const int16_t *s = &pkt[f * CH];
                if (!s[0] && !expect)                 /* the priming's silence, before the first frame */
                    continue;
                if (!expect)
                    expect = 1;
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
    if (maxb > 184u)
        fail("the largest packet's bytes", (long)maxb, 184);
    if (held || uac.underruns || uac.overruns)
        fail("holds, underruns, overruns", (long)(held + uac.underruns + uac.overruns), 0);
    if (got < 44000u * 59u)
        fail("frames recorded in 60 s", (long)got, 44100L * 60);
    printf("USB audio, stereo, two halves in hand: %u frames in order, packets up to %u B, ring fill %u..%u (%.1f ms "
           "at the least), %u up %u down\n", (unsigned)got, (unsigned)maxb, (unsigned)uac.fill_lo, (unsigned)uac.fill_hi,
           uac.fill_lo / 44.1, (unsigned)uac.adj_up, (unsigned)uac.adj_down);
    if (fails)
        return 1;
    printf("ok\n");
    return 0;
}
