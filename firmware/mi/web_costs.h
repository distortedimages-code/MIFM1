/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* The browser emulator's FM-1 (PLAT_WEB): what each part of the engine costs the FM-1, us a 128-frame block
 * (2902 us), from the instructions it runs (tools/web/calibrate.py writes this file: see there how). */
#pragma once
static const float kWebRest = 2.6f;   /* a resting voice */
static const float kWebModel[23] = {   /* a voice playing, by model */
    196.6f,  /*  0 KICK */
    194.4f,  /*  1 SNARE */
    195.4f,  /*  2 CLAP */
    152.6f,  /*  3 HAT */
    191.0f,  /*  4 TOM */
    216.3f,  /*  5 PERC */
    229.6f,  /*  6 FM DRUM */
    187.8f,  /*  7 FM SNARE */
    227.1f,  /*  8 FM CYMBAL */
    235.7f,  /*  9 FM HI-HAT */
    312.2f,  /* 10 ZERO */
    370.9f,  /* 11 FM */
    201.8f,  /* 12 PADS */
    302.8f,  /* 13 POLY */
    403.3f,  /* 14 PROPHET */
    186.0f,  /* 15 CHIPTUNE */
    466.2f,  /* 16 FORMANT */
    298.3f,  /* 17 SPEECH */
    486.4f,  /* 18 SWARM */
    396.8f,  /* 19 NOISE */
    610.5f,  /* 20 PARTICLES */
    510.9f,  /* 21 STRING */
    776.4f,  /* 22 MODAL */
};
static const float kWebFilter[5][3] = {   /* a filter (OFF, LADDER, MS-20, SEM, VOX): plain, its FM's extra, its NOISE's */
    {0.0f, 0.0f, 0.0f},
    {133.3f, 62.8f, 27.6f},
    {80.5f, 78.2f, 28.5f},
    {55.4f, 73.2f, 30.2f},
    {113.8f, 155.1f, 32.0f},
};
static const float kWebFm[3] = {36.3f, 72.9f, 21.9f};   /* the FM's extra: its square, PW, SYNC */
static const float kWebPads[4] = {78.3f, 22.6f, 31.9f, 70.5f};   /* PADS: its floor, a note (one saw, three), its bell */
static const float kWebPoly[2] = {21.3f, 77.5f};   /* POLY: its floor, a voice */
static const float kWebPro[2] = {28.1f, 104.0f};   /* PROPHET: its floor, a voice */
static const float kWebBend = 77.4f;   /* the bits, one of their knobs up */
static const float kWebTrauma = 42.3f;   /* TRAUMA's drive on a track */
static const float kWebGlitch = 9.0f;   /* the glitch, taking or repeating its slice (estimated: a few loads a sample) */
static const float kWebWarps[2][6] = {   /* Warps, its algorithm's mark (Eco, HQ) */
    {569.6f, 567.0f, 637.7f, 538.2f, 585.4f, 605.8f},
    {983.6f, 978.4f, 1119.7f, 920.9f, 1015.1f, 1056.0f},
};
static const float kWebFx[3] = {551.2f, 575.0f, 721.1f};   /* the effects, the mix and the master bus (OFF, COMP, OTT) */
static const float kWebFxEq = 37.2f, kWebFxDuck = 6.9f, kWebFxDelay = 15.9f;   /* a track's EQ (playing), the duck on a track, the delay's filter and drive */
static const float kWebFxWarps = 75.4f;   /* Warps in the mix (the rest of the stress test's effects) */
static const float kWebPrep = 297.0f, kWebPrepTrack = 0.0f, kWebPrepCable = 2.8f;   /* the preparation: its floor, a track playing, a cable */
