/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* The browser emulator's FM-1 (PLAT_WEB): what each part of the engine costs the FM-1, us a 128-frame block
 * (2902 us), from the instructions it runs (tools/web/calibrate.py writes this file: see there how). */
#pragma once
static const float kWebRest = 2.7f;   /* a resting voice */
static const float kWebModel[25] = {   /* a voice playing, by model */
    202.2f,  /*  0 KICK */
    200.0f,  /*  1 SNARE */
    201.0f,  /*  2 CLAP */
    158.0f,  /*  3 HAT */
    196.6f,  /*  4 TOM */
    221.9f,  /*  5 PERC */
    235.2f,  /*  6 FM DRUM */
    193.4f,  /*  7 FM SNARE */
    232.7f,  /*  8 FM CYMBAL */
    241.3f,  /*  9 FM HI-HAT */
    317.8f,  /* 10 ZERO */
    379.2f,  /* 11 FM */
    216.9f,  /* 12 PADS */
    311.0f,  /* 13 POLY */
    424.9f,  /* 14 REV3 */
    445.7f,  /* 15 OTIS */
    186.0f,  /* 16 TAURUS */
    177.7f,  /* 17 VOCODER */
    127.1f,  /* 18 STATIC */
    303.7f,  /* 19 SPEECH */
    491.7f,  /* 20 SWARM */
    402.0f,  /* 21 NOISE */
    615.7f,  /* 22 PARTICLES */
    516.1f,  /* 23 STRING */
    781.6f,  /* 24 MODAL */
};
static const float kWebFilter[5][3] = {   /* a filter (OFF, LADDER, MS-20, SEM, VOX): plain, its FM's extra, its NOISE's */
    {0.0f, 0.0f, 0.0f},
    {135.7f, 86.5f, 27.6f},
    {80.5f, 79.3f, 28.5f},
    {55.4f, 73.6f, 30.2f},
    {113.7f, 155.4f, 32.0f},
};
static const float kWebFm[3] = {37.1f, 81.8f, 22.2f};   /* the FM's extra: its square, PW, SYNC */
static const float kWebPads[4] = {125.2f, 11.9f, 21.2f, 71.0f};   /* PADS: its floor, a note (one saw, three), its bell */
static const float kWebPoly[2] = {33.5f, 76.6f};   /* POLY: its floor, a voice */
static const float kWebPro[2] = {34.0f, 108.4f};   /* REV3: its floor, a voice */
static const float kWebSq[2] = {167.1f, 94.2f};   /* OTIS: its floor, a voice */
static const float kWebVoc[3] = {111.5f, 23.4f, 352.3f};   /* VOCODER: its floor, a voice, its bands (MOD on) */
static const float kWebStatic[3] = {127.1f, 131.4f, 113.2f};   /* STATIC: BODY, ARC, SPARK (held) */
static const float kWebBend = 87.4f;   /* the bits, one of their knobs up */
static const float kWebTrauma = 43.4f;   /* TRAUMA's drive on a track */
static const float kWebGlitch = 9.0f;   /* the glitch, taking or repeating its slice (estimated: a few loads a sample) */
static const float kWebWarps[2][6] = {   /* Warps, its algorithm's mark (Eco, HQ) */
    {569.6f, 567.0f, 637.7f, 538.2f, 585.4f, 605.8f},
    {983.6f, 978.4f, 1119.7f, 920.9f, 1015.1f, 1056.0f},
};
static const float kWebFx[3] = {551.4f, 575.1f, 720.7f};   /* the effects, the mix and the master bus (OFF, COMP, OTT) */
static const float kWebFxEq = 37.3f, kWebFxDuck = 6.9f, kWebFxDelay = 15.9f;   /* a track's EQ (playing), the duck on a track, the delay's filter and drive */
static const float kWebFxWarps = 88.4f;   /* Warps in the mix (the rest of the stress test's effects) */
static const float kWebFxSpread = 75.2f;   /* (0.16.19) a track's SPREAD */
static const float kWebPrep = 364.8f, kWebPrepTrack = 0.0f, kWebPrepCable = 2.9f;   /* the preparation: its floor, a track playing, a cable */
