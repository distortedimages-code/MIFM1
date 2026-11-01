/*
 * lxr_port.h: what the LXR-02's sound engine took from the STM32 and from the parts of its firmware MIFM1 does not
 * carry over, for the FM-1 and a computer alike: CMSIS's intrinsics, the Cortex-M7's conversions, the MIDI parser's
 * detune, the sequencer's default note, the faders, the modulation nodes and the LFOs (their types: MIFM1 has its
 * own), and the copies' settings (one voice a track, single precision).
 *
 * Copyright (C) 2026 Yoann (MIFM1). midiParser_calcDetune: Copyright (C) 2012-2026 Erica Synths (MidiParser.c).
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of MIFM1's port of the LXR firmware's voices, licensed under the GNU General Public License
 * version 3 with the terms in NOTICE.md (see LICENSE and NOTICE.md in this directory).
 */
#ifndef LXR_PORT_H_
#define LXR_PORT_H_

#include <stdint.h>
#include <string.h>
#include <math.h>

/* one voice of each kind a MIFM1 track */
#define LXR_VOICES 4

/* What the LXR-02 computes in double (its Cortex-M7 has a double precision FPU) is computed in float on the FM-1,
 * which has none: a few coefficients, a gain's interpolation. The amplitude envelopes (SlopeEg2), whose state a float
 * would round (the cymbal's steep curve showed it at -60 dB), keep the double's value exactly (lxr_port.cc). Built
 * with LXR_HARD_DOUBLE, the copies compute as the LXR-02 does (doubles, fmaf as its VFMA): the test compares the two. */
#ifdef LXR_HARD_DOUBLE
#define LXR_DOUBLE double
#define LXR_FMAF(a, b, c) fmaf((a), (b), (c))
#else
#define LXR_DOUBLE float
#define LXR_FMAF(a, b, c) ((a) * (b) + (c))
#endif
#define LXR_D(x) ((LXR_DOUBLE)(x))

/* The Cortex-M7's float to integer conversions (VCVT), which the LXR's code meets with values a C conversion leaves
 * undefined: toward zero and saturated, NaN 0. A negative float made unsigned is 0 there (an FM index's negative
 * half, for one), a phase increment past 2^32 the largest. */
static inline uint32_t lxr_f2u(float x)
{
    if (!(x > 0.0f))
        return 0u;
    if (x >= 4294967296.0f)
        return 0xFFFFFFFFu;
    return (uint32_t)x;
}
static inline int32_t lxr_f2i(float x)
{
    if (x != x)
        return 0;
    if (x >= 2147483648.0f)
        return 0x7FFFFFFF;
    if (x <= -2147483648.0f)
        return (int32_t)0x80000000u;
    return (int32_t)x;
}

/* CMSIS's intrinsics, as the Cortex-M7 computes them */
static inline uint32_t __CLZ(uint32_t v) { return v ? (uint32_t)__builtin_clz(v) : 32u; }
static inline int32_t __SSAT(int32_t x, uint32_t bits)
{
    const int32_t hi = (int32_t)((1u << (bits - 1u)) - 1u), lo = -hi - 1;
    return x > hi ? hi : x < lo ? lo : x;
}
static inline int16_t lxr_sat16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }
static inline uint32_t __QADD16(uint32_t a, uint32_t b)    /* each half word, saturated */
{
    uint32_t lo = (uint16_t)lxr_sat16((int16_t)(a & 0xFFFFu) + (int16_t)(b & 0xFFFFu));
    uint32_t hi = (uint16_t)lxr_sat16((int16_t)(a >> 16) + (int16_t)(b >> 16));
    return lo | hi << 16;
}
static inline uint32_t __QSUB16(uint32_t a, uint32_t b)
{
    uint32_t lo = (uint16_t)lxr_sat16((int16_t)(a & 0xFFFFu) - (int16_t)(b & 0xFFFFu));
    uint32_t hi = (uint16_t)lxr_sat16((int16_t)(a >> 16) - (int16_t)(b >> 16));
    return lo | hi << 16;
}
/* __QADD16 over a buffer of int16 as the LXR does it, two samples a word (size / 2 words): sample by sample here
 * (the same sums, without reading int16 buffers as int32) */
static inline void lxr_qadd16_block(int16_t* x, const int16_t* y, const uint8_t size)
{
    const uint8_t n = (uint8_t)(size & ~1u);
    for (uint8_t i = 0; i < n; i++)
        x[i] = lxr_sat16((int32_t)x[i] + y[i]);
}

/* The wavetables' interpolation, ((int64_t)d * frac) >> 22 (>> 20 for the sine), in 32-bit multiplies: the same
 * number (|d| < 2^17, frac < 2^22), without the 64-bit multiply beside 16-bit loads that the FM-1's CPU now and then
 * gets wrong (fm1-base bsp/docs/CPU.md) */
static inline int32_t lxr_mul_shr22(int32_t d, uint32_t frac)
{
    return (d * (int32_t)(frac >> 11) + ((d * (int32_t)(frac & 0x7FFu)) >> 11)) >> 11;
}
static inline int32_t lxr_mul_shr20(int32_t d, uint32_t frac)
{
    return (d * (int32_t)(frac >> 10) + ((d * (int32_t)(frac & 0x3FFu)) >> 10)) >> 10;
}

/* the sequencer's (sequencer.h) */
#define SEQ_DEFAULT_NOTE 60

/* the MIDI parser's (MidiParser.c): a MIDI value 0..127 to a detune of +/- a half semitone or so */
#define SEMITONE_UP 1.0594630943592952645618252949463f
static inline float midiParser_calcDetune(uint8_t value)
{
    //linear interpolation between 1(no change) and semitone up/down)
    float frac = (value / 127.f - 0.5f);
    float cent = 1;
    cent += frac * (SEMITONE_UP - 1);
    return cent;
}

/* the faders (adcPots.c): none, as on the LXR-02 (USE_FADER_VOLUME off) */
static inline float adc_getFaderVolume(uint8_t idx)
{
    (void)idx;
    return 1.f;
}

/* the parameter array's value (ParameterArray.h), which the modulation node keeps */
typedef union
{
    float flt;
    uint32_t itg;
} ptrValue;

#endif /* LXR_PORT_H_ */
