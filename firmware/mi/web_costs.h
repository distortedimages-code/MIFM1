/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* The browser emulator's FM-1 (PLAT_WEB): what each part of the engine costs the FM-1, us a 128-frame block
 * (2902 us), from the instructions it runs (tools/web/calibrate.py writes this file: see there how). */
#pragma once
static const float kWebRest = 2.5f;   /* a resting voice */
static const float kWebModel[21] = {   /* a voice playing, by model */
    196.6f,  /*  0 KICK */
    193.3f,  /*  1 SNARE */
    194.3f,  /*  2 CLAP */
    151.8f,  /*  3 HAT */
    192.5f,  /*  4 TOM */
    215.2f,  /*  5 PERC */
    229.3f,  /*  6 FM DRUM */
    187.5f,  /*  7 FM SNARE */
    226.8f,  /*  8 FM CYMBAL */
    235.4f,  /*  9 FM HI-HAT */
    309.8f,  /* 10 ZERO */
    368.5f,  /* 11 FM */
    201.5f,  /* 12 PADS */
    185.7f,  /* 13 CHIPTUNE */
    465.9f,  /* 14 FORMANT */
    298.0f,  /* 15 SPEECH */
    486.1f,  /* 16 SWARM */
    396.5f,  /* 17 NOISE */
    610.2f,  /* 18 PARTICLES */
    510.6f,  /* 19 STRING */
    776.1f,  /* 20 MODAL */
};
static const float kWebFilter[5][3] = {   /* a filter (OFF, LADDER, MS-20, SEM, VOX): plain, its FM's extra, its NOISE's */
    {0.0f, 0.0f, 0.0f},
    {133.3f, 61.7f, 27.6f},
    {80.5f, 76.9f, 28.5f},
    {55.4f, 71.8f, 30.2f},
    {113.8f, 152.7f, 32.0f},
};
static const float kWebFm[3] = {34.7f, 71.3f, 20.2f};   /* the FM's extra: its square, PW, SYNC */
static const float kWebPads[4] = {78.0f, 22.6f, 31.9f, 70.5f};   /* PADS: its floor, a note (one saw, three), its bell */
static const float kWebBend = 77.4f;   /* the bits, one of their knobs up */
static const float kWebGlitch = 9.0f;   /* the glitch, taking or repeating its slice (estimated: a few loads a sample) */
static const float kWebWarps[2][6] = {   /* Warps, its algorithm's mark (Eco, HQ) */
    {569.6f, 567.0f, 637.7f, 538.2f, 585.4f, 605.8f},
    {983.6f, 978.4f, 1119.7f, 920.9f, 1015.1f, 1056.0f},
};
static const float kWebFx[3] = {551.1f, 575.0f, 721.1f};   /* the effects, the mix and the master bus (OFF, COMP, OTT) */
static const float kWebFxEq = 37.2f, kWebFxDuck = 6.9f, kWebFxDelay = 15.9f;   /* a track's EQ (playing), the duck on a track, the delay's filter and drive */
static const float kWebFxWarps = 75.5f;   /* Warps in the mix (the rest of the stress test's effects) */
static const float kWebPrep = 291.4f, kWebPrepTrack = 0.0f, kWebPrepCable = 2.8f;   /* the preparation: its floor, a track playing, a cable */
