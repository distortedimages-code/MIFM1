/*
 * lxr_port.cc: the LXR firmware's modulation system, as the voices call it, without it: MIFM1 modulates its knobs
 * with its own LFOs and cables. A voice's LFO (lfo.c) is neither run nor started again at its trigger, and the
 * velocity's modulation node (modulationNode.c) is not kept: the voices' own velocity (their VELO) is.
 *
 * Copyright (C) 2026 Yoann (MIFM1)
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of MIFM1's port of the LXR firmware's voices, licensed under the GNU General Public License
 * version 3 with the terms in NOTICE.md (see LICENSE and NOTICE.md in this directory).
 */
#include "Soundengine/DSPAudio/lfo.h"
#include "Soundengine/DSPAudio/modulationNode.h"

ModulationNode velocityModulators[6];

void lfo_init(Lfo* lfo)
{
    memset(lfo, 0, sizeof(*lfo));
}

void lfo_retrigger(uint8_t voiceNr)
{
    (void)voiceNr;
}

void modNode_updateValue(ModulationNode* vm, float val)
{
    vm->lastVal = val;
}

#ifndef LXR_HARD_DOUBLE
/*
 * SlopeEg2.c's envelope (Copyright (C) 2012-2026 Erica Synths, GPL-3.0-only), on the FM-1: the same steps, in the
 * same order, computed without a double. The LXR-02 keeps the envelope's value in a double and adds or takes
 * floats from it (attack and decay are always float values): the sums are exact there, so they are here, in
 * integers of 2^-40 (no float has a bit below that: a decay's smallest is ~2^-17). Its curve, (1 + slope) x val /
 * (1 + slope x |val|), and the parameters' conversions are computed in pairs of floats (~48 bits: a double's 53
 * rounded to the float its callers keep give the same float but where it falls within ~2^-46 of a float's
 * halfway point).
 */
#include "Soundengine/DSPAudio/SlopeEg2.h"

namespace {

struct Dd {                             // hi + lo, hi the pair rounded to a float
    float hi, lo;
};
inline Dd dd(float a) { return Dd{a, 0.0f}; }
inline Dd two_sum(float a, float b)
{
    const float s = a + b, bb = s - a;
    return Dd{s, (a - (s - bb)) + (b - bb)};
}
inline Dd fast_two_sum(float a, float b)  // |a| >= |b|
{
    const float s = a + b;
    return Dd{s, b - (s - a)};
}
inline void split(float a, float& h, float& l)
{
    const float c = 4097.0f * a;
    h = c - (c - a);
    l = a - h;
}
inline Dd two_prod(float a, float b)
{
    const float p = a * b;
    float ah, al, bh, bl;
    split(a, ah, al);
    split(b, bh, bl);
    return Dd{p, ((ah * bh - p) + ah * bl + al * bh) + al * bl};
}
inline Dd dd_add(Dd x, Dd y)
{
    const Dd s = two_sum(x.hi, y.hi);
    return fast_two_sum(s.hi, s.lo + (x.lo + y.lo));
}
inline Dd dd_neg(Dd x) { return Dd{-x.hi, -x.lo}; }
inline Dd dd_mul(Dd x, Dd y)
{
    const Dd p = two_prod(x.hi, y.hi);
    return fast_two_sum(p.hi, p.lo + (x.hi * y.lo + x.lo * y.hi));
}
inline Dd dd_div(Dd x, Dd y)
{
    const float q1 = x.hi / y.hi;
    const Dd p = two_prod(q1, y.hi);
    const float r = ((x.hi - p.hi) - p.lo + x.lo) - q1 * y.lo;
    return fast_two_sum(q1, r / y.hi);
}

const int64_t kOne = (int64_t)1 << 40;  // 1.0
inline int64_t q40(float x)             // a float in units of 2^-40 (exact for the envelopes' values)
{
    uint32_t u;
    memcpy(&u, &x, 4);
    const int e = (int)((u >> 23) & 0xFFu);
    if (e == 0)
        return 0;
    const int64_t m = (int64_t)((u & 0x7FFFFFu) | 0x800000u);
    const int sh = e - 150 + 40;
    const int64_t q = sh >= 0 ? (sh > 22 ? (int64_t)0x7FFFFFFFFFFFLL : m << sh) : (sh < -24 ? 0 : m >> -sh);
    return (u >> 31) ? -q : q;
}
inline Dd dd_q40(int64_t q)             // and back, exactly: its high 24 bits and its low 17
{
    const int32_t h = (int32_t)(q >> 17), l = (int32_t)(q & 0x1FFFF);
    return two_sum((float)h * (1.0f / 8388608.0f), (float)l * (1.0f / 1099511627776.0f));
}

inline float slope_value(int64_t val, const float* slope)   // slopeEg2_calcSlopeValue
{
    const Dd s = Dd{slope[0], slope[1]}, one = dd(1.0f);
    const Dd num = dd_mul(dd_add(one, s), dd_q40(val));
    const Dd den = dd_add(one, dd_mul(s, dd_q40(val < 0 ? -val : val)));
    return dd_div(num, den).hi;
}

// TIME_K_ATTACK and TIME_K_DECAY, 2 x 0.99 / (1.0 - 0.99) and 2 x 0.999 / (1.0 - 0.999) in double, as pairs
const Dd kTimeKAttack = {198.0f, -1.7053025658242404e-13f};
const Dd kTimeKDecay = {1998.0f, -1.8189894035458565e-12f};

float calc_time(uint8_t data2, Dd amount)   // slopeEg2_calcTime (USE_BIG_SAMPLE_BUFFER: x 2)
{
    const Dd val = dd_div(dd((float)data2), dd(127.0f));
    const Dd one = dd(1.0f);
    const Dd v = dd_div(dd_mul(dd_add(one, amount), val), dd_add(one, dd_mul(amount, val)));
    const Dd r = dd_add(one, dd_neg(v));
    const float ret = dd_add(r, r).hi;
    return ret > 1.f ? 1.f : ret;
}

}  // namespace

void slopeEg2_init(SlopeEg2* eg)
{
    eg->attack = 0.01f;
    eg->decay = 0.01f;
    eg->value = 0;
    eg->state = EG_STOPPED;
    eg->repeat = 0;

    slopeEg2_setSlope(eg, (uint8_t)0.5f);   /* (as SlopeEg2.c: 0.5f made a uint8_t: 0) */
}

void slopeEg2_trigger(SlopeEg2* eg)
{
    //if no repeat is selected beginn in attack state
    if (!eg->repeat)
    {
        eg->state = EG_A;
    }
    // if repeat is active we want to repeat the decay stage during the attack time
    else
    {
        eg->state = EG_REPEAT;
        eg->value = kOne;
    }
    eg->repeatCnt = eg->repeat;
}

float slopeEg2_calc(SlopeEg2* eg)
{
    int64_t val = eg->value;
    switch (eg->state)
    {
    case EG_STOPPED:
        return 0.0f;

    case EG_REPEAT:
        if (val > 0)
        {
            val -= q40(eg->attack);
        }
        else
        {
            eg->repeatCnt--;
            if (eg->repeatCnt > 0)
            {
                val = kOne;
            }
            else
            {
                eg->state = EG_D;
                val = kOne;
            }
        }
        eg->value = val;
        return slope_value(val, eg->slope);

    case EG_A:
        val += q40(eg->attack);
        if (val >= kOne)
        {
            val = kOne;
            eg->state = EG_D;
        }
        eg->value = val;
        return slope_value(val, eg->invSlope);

    case EG_D:
        if (val > 0)
        {
            val -= q40(eg->decay);
            eg->value = val;
            return slope_value(val, eg->slope);
        }
        else
        {
            eg->value = 0;
            return 0;
        }
    }
    return 0;
}

void slopeEg2_setAttack(SlopeEg2* eg, uint8_t data2, uint8_t isSync)
{
    eg->attack = calc_time(data2, kTimeKAttack);
    if (isSync)
        eg->attack /= 16;
}

void slopeEg2_setDecay(SlopeEg2* eg, uint8_t data2, uint8_t isSync)
{
    eg->decay = calc_time(data2, kTimeKDecay);
    if (isSync)
        eg->decay /= 16;
}

float slopeEg2_calcDecay(uint8_t data2)
{
    return calc_time(data2, kTimeKDecay);
}

void slopeEg2_setSlope(SlopeEg2* eg, uint8_t data2)
{
    // amount and its negation, floats (in doubles there); 2 x amount / (1 - amount), in double there
    const float amount = ((data2 / 127.f) - 0.5f) * 1.999f;
    const float invAmount = -amount;
    const Dd slope = dd_div(dd(2 * amount), two_sum(1.0f, -amount));
    const Dd invSlope = dd_div(dd(2 * invAmount), two_sum(1.0f, -invAmount));
    eg->slope[0] = slope.hi;
    eg->slope[1] = slope.lo;
    eg->invSlope[0] = invSlope.hi;
    eg->invSlope[1] = invSlope.lo;
}
#endif /* LXR_HARD_DOUBLE */
