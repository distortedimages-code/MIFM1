/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* The browser emulator's FM-1 (PLAT_WEB): what each part of the engine costs the FM-1, us a 128-frame block
 * (2902 us), from the instructions it runs (tools/web/calibrate.py writes this file: see there how). */
#pragma once
static const float kWebRest = 2.5f;   /* a resting voice */
static const float kWebModel[20] = {   /* a voice playing, by model */
    196.6f,  /*  0 KICK */
    193.3f,  /*  1 SNARE */
    194.4f,  /*  2 CLAP */
    151.9f,  /*  3 HAT */
    192.5f,  /*  4 TOM */
    215.2f,  /*  5 PERC */
    229.3f,  /*  6 FM DRUM */
    187.5f,  /*  7 FM SNARE */
    226.8f,  /*  8 FM CYMBAL */
    235.4f,  /*  9 FM HI-HAT */
    309.8f,  /* 10 ZERO */
    322.3f,  /* 11 FM */
    185.7f,  /* 12 CHIPTUNE */
    463.7f,  /* 13 FORMANT */
    287.8f,  /* 14 SPEECH */
    482.8f,  /* 15 SWARM */
    393.5f,  /* 16 NOISE */
    610.2f,  /* 17 PARTICLES */
    510.6f,  /* 18 STRING */
    774.7f,  /* 19 MODAL */
};
static const float kWebFilter[5][3] = {   /* a filter (OFF, LADDER, MS-20, SEM, VOX): plain, its FM's extra, its NOISE's */
    {0.0f, 0.0f, 0.0f},
    {133.5f, 61.4f, 27.6f},
    {80.5f, 75.9f, 28.5f},
    {55.4f, 70.7f, 30.2f},
    {113.7f, 153.5f, 32.0f},
};
static const float kWebFm[3] = {51.9f, 90.8f, 39.0f};   /* the FM's extra: its square, PW, SYNC */
static const float kWebBend = 77.3f;   /* the bits, one of their knobs up */
static const float kWebGlitch = 9.0f;   /* the glitch, taking or repeating its slice (estimated: a few loads a sample) */
static const float kWebWarps[2][6] = {   /* Warps, its algorithm's mark (Eco, HQ) */
    {569.7f, 567.1f, 637.8f, 538.3f, 585.5f, 605.1f},
    {983.7f, 978.5f, 1119.8f, 921.0f, 1015.2f, 1054.5f},
};
static const float kWebFx[3] = {551.1f, 574.9f, 721.1f};   /* the effects, the mix and the master bus (OFF, COMP, OTT) */
static const float kWebFxEq = 37.2f, kWebFxDuck = 6.9f, kWebFxDelay = 15.9f;   /* a track's EQ (playing), the duck on a track, the delay's filter and drive */
static const float kWebFxWarps = 75.3f;   /* Warps in the mix (the rest of the stress test's effects) */
static const float kWebPrep = 288.5f, kWebPrepTrack = 0.0f, kWebPrepCable = 2.8f;   /* the preparation: its floor, a track playing, a cable */
