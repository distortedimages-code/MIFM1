/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* The browser emulator's FM-1 (PLAT_WEB): what each part of the engine costs the FM-1, us a 128-frame block
 * (2902 us), from the instructions it runs (tools/web/calibrate.py writes this file: see there how). */
#pragma once
static const float kWebRest = 2.5f;   /* a resting voice */
static const float kWebModel[32] = {   /* a voice playing, by model */
    190.4f,  /*  0 KICK */
    187.1f,  /*  1 SNARE */
    187.4f,  /*  2 CLAP */
    148.7f,  /*  3 HAT */
    186.3f,  /*  4 TOM */
    209.9f,  /*  5 PERC */
    229.3f,  /*  6 FM DRUM */
    187.5f,  /*  7 FM SNARE */
    226.8f,  /*  8 FM CYMBAL */
    235.4f,  /*  9 FM HI-HAT */
    309.8f,  /* 10 ZERO */
    399.3f,  /* 11 VA VCF */
    502.8f,  /* 12 PHASE DIST */
    501.4f,  /* 13 6-OP FM 1 */
    493.7f,  /* 14 6-OP FM 2 */
    495.7f,  /* 15 6-OP FM 3 */
    437.1f,  /* 16 WAVE TERRAIN */
    575.1f,  /* 17 STRING MACHINE */
    190.0f,  /* 18 CHIPTUNE */
    432.9f,  /* 19 VIRTUAL ANALOG */
    327.3f,  /* 20 WAVESHAPER */
    512.0f,  /* 21 FM 2-OP */
    489.6f,  /* 22 FORMANT */
    894.2f,  /* 23 HARMONIC */
    496.8f,  /* 24 WAVETABLE */
    549.4f,  /* 25 CHORDS */
    170.5f,  /* 26 SPEECH */
    484.3f,  /* 27 SWARM */
    398.6f,  /* 28 NOISE */
    610.2f,  /* 29 PARTICLES */
    541.0f,  /* 30 STRING */
    774.3f,  /* 31 MODAL */
};
static const float kWebFilter[5][3] = {   /* a filter (OFF, LADDER, MS-20, SEM, VOX): plain, its FM's extra, its NOISE's */
    {0.0f, 0.0f, 0.0f},
    {133.5f, 60.9f, 27.6f},
    {80.5f, 75.1f, 28.5f},
    {55.4f, 69.7f, 30.2f},
    {113.7f, 153.2f, 32.0f},
};
static const float kWebBend = 77.4f;   /* the bits, one of their knobs up */
static const float kWebGlitch = 9.0f;   /* the glitch, taking or repeating its slice (estimated: a few loads a sample) */
static const float kWebWarps[2][6] = {   /* Warps, its algorithm's mark (Eco, HQ) */
    {569.6f, 567.0f, 637.7f, 538.2f, 585.4f, 602.8f},
    {983.6f, 978.4f, 1119.7f, 920.9f, 1015.1f, 1050.0f},
};
static const float kWebFx[3] = {551.1f, 574.9f, 722.3f};   /* the effects, the mix and the master bus (OFF, COMP, OTT) */
static const float kWebFxEq = 37.2f, kWebFxDuck = 6.9f, kWebFxDelay = 15.9f;   /* a track's EQ (playing), the duck on a track, the delay's filter and drive */
static const float kWebFxWarps = 73.8f;   /* Warps in the mix (the rest of the stress test's effects) */
static const float kWebPrep = 287.9f, kWebPrepTrack = 0.0f, kWebPrepCable = 2.3f;   /* the preparation: its floor, a track playing, a cable */
