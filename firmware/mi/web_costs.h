/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* The browser emulator's FM-1 (PLAT_WEB): what each part of the engine costs the FM-1, us a 128-frame block
 * (2902 us), from the instructions it runs (tools/web/calibrate.py writes this file: see there how). */
#pragma once
static const float kWebRest = 2.6f;   /* a resting voice */
static const float kWebModel[22] = {   /* a voice playing, by model */
    196.6f,  /*  0 KICK */
    194.5f,  /*  1 SNARE */
    195.5f,  /*  2 CLAP */
    152.7f,  /*  3 HAT */
    191.1f,  /*  4 TOM */
    216.3f,  /*  5 PERC */
    229.7f,  /*  6 FM DRUM */
    187.9f,  /*  7 FM SNARE */
    227.2f,  /*  8 FM CYMBAL */
    235.8f,  /*  9 FM HI-HAT */
    312.2f,  /* 10 ZERO */
    373.7f,  /* 11 FM */
    205.5f,  /* 12 PADS */
    305.0f,  /* 13 POLY */
    404.8f,  /* 14 REV3 */
    441.2f,  /* 15 OTIS */
    298.2f,  /* 16 SPEECH */
    486.1f,  /* 17 SWARM */
    396.5f,  /* 18 NOISE */
    610.2f,  /* 19 PARTICLES */
    510.6f,  /* 20 STRING */
    776.1f,  /* 21 MODAL */
};
static const float kWebFilter[5][3] = {   /* a filter (OFF, LADDER, MS-20, SEM, VOX): plain, its FM's extra, its NOISE's */
    {0.0f, 0.0f, 0.0f},
    {134.2f, 61.9f, 28.5f},
    {80.1f, 78.2f, 28.5f},
    {53.8f, 73.2f, 28.5f},
    {112.1f, 155.1f, 30.2f},
};
static const float kWebFm[3] = {37.1f, 81.8f, 22.2f};   /* the FM's extra: its square, PW, SYNC */
static const float kWebPads[4] = {113.9f, 11.9f, 21.2f, 69.2f};   /* PADS: its floor, a note (one saw, three), its bell */
static const float kWebPoly[2] = {27.7f, 76.5f};   /* POLY: its floor, a voice */
static const float kWebPro[2] = {28.3f, 104.4f};   /* REV3: its floor, a voice */
static const float kWebSq[2] = {162.7f, 94.2f};   /* OTIS: its floor, a voice */
static const float kWebBend = 77.4f;   /* the bits, one of their knobs up */
static const float kWebTrauma = 42.5f;   /* TRAUMA's drive on a track */
static const float kWebGlitch = 9.0f;   /* the glitch, taking or repeating its slice (estimated: a few loads a sample) */
static const float kWebWarps[2][6] = {   /* Warps, its algorithm's mark (Eco, HQ) */
    {569.6f, 567.0f, 637.7f, 538.2f, 585.4f, 605.8f},
    {983.6f, 978.4f, 1119.7f, 920.9f, 1015.1f, 1056.0f},
};
static const float kWebFx[3] = {551.0f, 574.8f, 721.0f};   /* the effects, the mix and the master bus (OFF, COMP, OTT) */
static const float kWebFxEq = 37.3f, kWebFxDuck = 6.9f, kWebFxDelay = 15.9f;   /* a track's EQ (playing), the duck on a track, the delay's filter and drive */
static const float kWebFxWarps = 75.6f;   /* Warps in the mix (the rest of the stress test's effects) */
static const float kWebPrep = 350.7f, kWebPrepTrack = 0.0f, kWebPrepCable = 2.9f;   /* the preparation: its floor, a track playing, a cable */
