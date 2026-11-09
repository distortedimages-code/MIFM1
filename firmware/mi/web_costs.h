/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* The browser emulator's FM-1 (PLAT_WEB): what each part of the engine costs the FM-1, us a 128-frame block
 * (2902 us), from the instructions it runs (tools/web/calibrate.py writes this file: see there how). */
#pragma once
static const float kWebRest = 2.7f;   /* a resting voice */
static const float kWebModel[28] = {   /* a voice playing, by model */
    202.4f,  /*  0 KICK */
    200.2f,  /*  1 SNARE */
    201.2f,  /*  2 CLAP */
    158.2f,  /*  3 HAT */
    196.8f,  /*  4 TOM */
    222.1f,  /*  5 PERC */
    235.4f,  /*  6 FM DRUM */
    193.7f,  /*  7 FM SNARE */
    212.4f,  /*  8 SDS KICK */
    212.2f,  /*  9 SDS SNARE */
    212.5f,  /* 10 SDS TOM */
    212.4f,  /* 11 SDS PERC */
    319.4f,  /* 12 ZERO */
    379.4f,  /* 13 FM */
    211.8f,  /* 14 PADS */
    312.1f,  /* 15 POLY */
    425.6f,  /* 16 REV3 */
    186.7f,  /* 17 TAURUS */
    178.5f,  /* 18 VOCODER */
    174.0f,  /* 19 CHOIR */
    127.4f,  /* 20 STATIC */
    243.8f,  /* 21 EASEL */
    303.7f,  /* 22 SPEECH */
    491.7f,  /* 23 SWARM */
    402.0f,  /* 24 NOISE */
    615.8f,  /* 25 PARTICLES */
    516.1f,  /* 26 STRING */
    781.7f,  /* 27 MODAL */
};
static const float kWebFilter[5][3] = {   /* a filter (OFF, LADDER, MS-20, SEM, VOX): plain, its FM's extra, its NOISE's */
    {0.0f, 0.0f, 0.0f},
    {137.5f, 84.8f, 28.5f},
    {80.2f, 79.3f, 28.5f},
    {54.8f, 73.6f, 28.5f},
    {113.2f, 155.4f, 30.2f},
};
static const float kWebFm[3] = {37.1f, 81.8f, 22.2f};   /* the FM's extra: its square, PW, SYNC */
static const float kWebPads[3] = {125.3f, 10.7f, 19.8f};   /* PADS: its floor, a note (one saw, three) */
static const float kWebPadsWave[2] = {20.4f, 25.2f};   /* (0.16.57) PADS' WAVE off Saw: a note's more (one saw, three) */
static const float kWebPadsLayer[9] = {0.0f, 74.1f, 121.2f, 99.9f, 113.3f, 80.3f, 117.4f, 97.6f, 102.4f};   /* its LAYER */
static const float kWebPadsTex[5] = {0.0f, 10.8f, 0.2f, 25.3f, 13.3f};   /* its TEXTURE (Down: a note's) */
static const float kWebPoly[2] = {33.7f, 76.8f};   /* POLY: its floor, a voice */
static const float kWebPro[2] = {34.2f, 108.5f};   /* REV3: its floor, a voice */
static const float kWebChoir[2] = {18.8f, 155.2f};   /* CHOIR: its floor, a part */
static const float kWebVoc[3] = {111.7f, 23.6f, 352.5f};   /* VOCODER: its floor, a voice, its bands (MOD on) */
static const float kWebStatic[3] = {127.4f, 131.7f, 113.4f};   /* STATIC: BODY, ARC, SPARK (held) */
static const float kWebBend = 78.4f;   /* the bits, one of their knobs up */
static const float kWebBend2 = 53.9f;   /* (0.16.74) their second page off its marks, more */
static const float kWebTrauma = 41.5f;   /* TRAUMA's drive on a track */
static const float kWebGlitch = 9.0f;   /* the glitch, taking or repeating its slice (estimated: a few loads a sample) */
static const float kWebWarps[2][6] = {   /* Warps, its algorithm's mark (Eco, HQ) */
    {569.6f, 567.0f, 637.7f, 538.2f, 585.4f, 605.8f},
    {983.6f, 978.4f, 1119.7f, 920.9f, 1015.1f, 1056.0f},
};
static const float kWebFx[2] = {520.9f, 544.6f};   /* the effects, the mix and the master bus (OFF, COMP) */
static const float kWebFxEq = 37.3f, kWebFxDuck = 6.9f, kWebFxDelay = 14.5f;   /* a track's EQ (playing), the duck on a track, the delay's filter and drive */
static const float kWebFxWarps = 87.6f;   /* Warps in the mix (the rest of the stress test's effects) */
static const float kWebFxSpread = 75.2f;   /* (0.16.19) a track's SPREAD */
static const float kWebFxMetal[2] = {78.3f, 113.6f};   /* (0.16.41) the reverb METAL, SHIMMER: over Clouds' */
static const float kWebFxBbd = 21.6f;   /* (0.16.65) the delay's BBD: over the clean one's */
static const float kWebFxRev = 229.4f, kWebFxDly = 75.4f;   /* (0.16.71) the reverb, the delay: awake (in kWebFx) */
static const float kWebFxSkip[2] = {5.2f, 7.8f};   /* (0.16.71) asleep: their clocks moved on */
static const float kWebPrep = 397.5f, kWebPrepTrack = 0.0f, kWebPrepCable = 2.9f;   /* the preparation: its floor, a track playing, a cable */
