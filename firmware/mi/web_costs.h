/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* The browser emulator's FM-1 (PLAT_WEB): what each part of the engine costs the FM-1, us a 128-frame block
 * (2902 us), from the instructions it runs (tools/web/calibrate.py writes this file: see there how). */
#pragma once
static const float kWebRest = 2.4f;   /* a resting voice */
static const float kWebModel[28] = {   /* a voice playing, by model */
    398.6f,  /*  0 VA VCF */
    502.2f,  /*  1 PHASE DIST */
    500.7f,  /*  2 6-OP FM 1 */
    493.1f,  /*  3 6-OP FM 2 */
    495.1f,  /*  4 6-OP FM 3 */
    436.5f,  /*  5 WAVE TERRAIN */
    574.5f,  /*  6 STRING MACH */
    189.3f,  /*  7 CHIPTUNE */
    432.2f,  /*  8 VIRTUAL ANALOG */
    326.6f,  /*  9 WAVESHAPING */
    511.4f,  /* 10 FM 2-OP */
    488.9f,  /* 11 GRAIN */
    893.5f,  /* 12 ADDITIVE */
    496.1f,  /* 13 WAVETABLE */
    548.7f,  /* 14 CHORD */
    169.9f,  /* 15 SPEECH */
    483.7f,  /* 16 SWARM */
    398.0f,  /* 17 NOISE */
    609.5f,  /* 18 PARTICLE */
    540.3f,  /* 19 STRING */
    773.6f,  /* 20 MODAL */
    391.6f,  /* 21 KICK */
    503.0f,  /* 22 SNARE */
    442.0f,  /* 23 HI-HAT */
    228.7f,  /* 24 FM DRUM */
    186.9f,  /* 25 FM SNARE */
    226.1f,  /* 26 FM CYMBAL */
    234.8f,  /* 27 FM HI-HAT */
};
static const float kWebFilter[5][3] = {   /* a filter (OFF, LADDER, MS-20, SEM, VOX): plain, its FM's extra, its NOISE's */
    {0.0f, 0.0f, 0.0f},
    {135.9f, 58.3f, 28.5f},
    {79.9f, 73.4f, 28.5f},
    {54.7f, 67.9f, 28.5f},
    {110.1f, 151.5f, 31.1f},
};
static const float kWebGrit = 30.9f;   /* the grit, one of its knobs up */
static const float kWebWarps[2][6] = {   /* Warps, its algorithm's mark (Eco, HQ) */
    {569.6f, 567.0f, 637.7f, 538.2f, 585.4f, 602.8f},
    {983.6f, 978.4f, 1119.7f, 920.9f, 1015.1f, 1050.0f},
};
static const float kWebFx[3] = {454.1f, 477.8f, 625.2f};   /* the effects, the mix and the master bus (OFF, COMP, OTT) */
static const float kWebFxEq = 37.2f, kWebFxDuck = 6.9f, kWebFxDelay = 15.9f;   /* a track's EQ (playing), the duck on a track, the delay's filter and drive */
static const float kWebFxWarps = 74.8f;   /* Warps in the mix (the rest of the stress test's effects) */
static const float kWebPrep = 269.6f, kWebPrepTrack = 0.0f, kWebPrepCable = 6.8f;   /* the preparation: its floor, a track playing, a cable */
