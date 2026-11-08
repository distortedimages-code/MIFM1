/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* The browser emulator's FM-1 (PLAT_WEB): what each part of the engine costs the FM-1, us a 128-frame block
 * (2902 us), from the instructions it runs (tools/web/calibrate.py writes this file: see there how). */
#pragma once
static const float kWebRest = 2.7f;   /* a resting voice */
static const float kWebModel[28] = {   /* a voice playing, by model */
    202.2f,  /*  0 KICK */
    200.0f,  /*  1 SNARE */
    201.0f,  /*  2 CLAP */
    158.0f,  /*  3 HAT */
    196.6f,  /*  4 TOM */
    221.9f,  /*  5 PERC */
    235.2f,  /*  6 FM DRUM */
    193.5f,  /*  7 FM SNARE */
    212.3f,  /*  8 SDS KICK */
    212.0f,  /*  9 SDS SNARE */
    212.3f,  /* 10 SDS TOM */
    212.3f,  /* 11 SDS PERC */
    317.8f,  /* 12 ZERO */
    379.3f,  /* 13 FM */
    207.6f,  /* 14 PADS */
    311.1f,  /* 15 POLY */
    424.9f,  /* 16 REV3 */
    186.1f,  /* 17 TAURUS */
    177.8f,  /* 18 VOCODER */
    172.9f,  /* 19 CHOIR */
    127.1f,  /* 20 STATIC */
    243.6f,  /* 21 EASEL */
    303.7f,  /* 22 SPEECH */
    491.7f,  /* 23 SWARM */
    402.0f,  /* 24 NOISE */
    615.8f,  /* 25 PARTICLES */
    516.1f,  /* 26 STRING */
    781.6f,  /* 27 MODAL */
};
static const float kWebFilter[5][3] = {   /* a filter (OFF, LADDER, MS-20, SEM, VOX): plain, its FM's extra, its NOISE's */
    {0.0f, 0.0f, 0.0f},
    {137.6f, 84.8f, 28.5f},
    {79.5f, 79.3f, 28.5f},
    {54.7f, 73.6f, 28.5f},
    {113.1f, 155.4f, 30.2f},
};
static const float kWebFm[3] = {37.1f, 81.8f, 22.2f};   /* the FM's extra: its square, PW, SYNC */
static const float kWebPads[3] = {124.0f, 11.0f, 19.2f};   /* PADS: its floor, a note (one saw, three) */
static const float kWebPadsWave[2] = {20.4f, 26.1f};   /* (0.16.57) PADS' WAVE off Saw: a note's more (one saw, three) */
static const float kWebPadsLayer[9] = {0.0f, 74.2f, 121.3f, 100.0f, 116.0f, 80.3f, 114.6f, 91.5f, 100.8f};   /* its LAYER */
static const float kWebPadsTex[5] = {0.0f, 10.8f, 0.1f, 25.3f, 13.3f};   /* its TEXTURE (Down: a note's) */
static const float kWebPoly[2] = {33.5f, 76.6f};   /* POLY: its floor, a voice */
static const float kWebPro[2] = {34.0f, 108.4f};   /* REV3: its floor, a voice */
static const float kWebChoir[2] = {18.7f, 154.2f};   /* CHOIR: its floor, a part */
static const float kWebVoc[3] = {111.5f, 23.4f, 352.6f};   /* VOCODER: its floor, a voice, its bands (MOD on) */
static const float kWebStatic[3] = {127.1f, 131.5f, 113.2f};   /* STATIC: BODY, ARC, SPARK (held) */
static const float kWebBend = 87.4f;   /* the bits, one of their knobs up */
static const float kWebTrauma = 41.5f;   /* TRAUMA's drive on a track */
static const float kWebGlitch = 9.0f;   /* the glitch, taking or repeating its slice (estimated: a few loads a sample) */
static const float kWebWarps[2][6] = {   /* Warps, its algorithm's mark (Eco, HQ) */
    {569.6f, 567.0f, 637.7f, 538.2f, 585.4f, 605.8f},
    {983.6f, 978.4f, 1119.7f, 920.9f, 1015.1f, 1056.0f},
};
static const float kWebFx[2] = {519.2f, 542.9f};   /* the effects, the mix and the master bus (OFF, COMP) */
static const float kWebFxEq = 37.3f, kWebFxDuck = 6.9f, kWebFxDelay = 14.5f;   /* a track's EQ (playing), the duck on a track, the delay's filter and drive */
static const float kWebFxWarps = 87.6f;   /* Warps in the mix (the rest of the stress test's effects) */
static const float kWebFxSpread = 75.2f;   /* (0.16.19) a track's SPREAD */
static const float kWebFxMetal[2] = {78.3f, 113.6f};   /* (0.16.41) the reverb METAL, SHIMMER: over Clouds' */
static const float kWebFxBbd = 21.6f;   /* (0.16.65) the delay's BBD: over the clean one's */
static const float kWebPrep = 362.1f, kWebPrepTrack = 0.0f, kWebPrepCable = 2.9f;   /* the preparation: its floor, a track playing, a cable */
