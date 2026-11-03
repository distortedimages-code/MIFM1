/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* The browser emulator's FM-1 (PLAT_WEB): what each part of the engine costs the FM-1, us a 128-frame block
 * (2902 us), from the instructions it runs (tools/web/calibrate.py writes this file: see there how). */
#pragma once
static const float kWebRest = 2.5f;   /* a resting voice */
static const float kWebModel[27] = {   /* a voice playing, by model */
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
    318.9f,  /* 11 BASS */
    319.0f,  /* 12 LEAD */
    321.3f,  /* 13 BRASS */
    318.9f,  /* 14 PLUCK */
    319.3f,  /* 15 KEYS */
    325.1f,  /* 16 PAD */
    319.2f,  /* 17 BELL */
    319.4f,  /* 18 ORGAN */
    185.5f,  /* 19 CHIPTUNE */
    485.9f,  /* 20 FORMANT */
    154.8f,  /* 21 SPEECH */
    481.8f,  /* 22 SWARM */
    396.0f,  /* 23 NOISE */
    610.0f,  /* 24 PARTICLES */
    541.0f,  /* 25 STRING */
    771.8f,  /* 26 MODAL */
};
static const float kWebFilter[5][3] = {   /* a filter (OFF, LADDER, MS-20, SEM, VOX): plain, its FM's extra, its NOISE's */
    {0.0f, 0.0f, 0.0f},
    {133.5f, 61.3f, 27.6f},
    {80.5f, 75.4f, 28.5f},
    {55.4f, 70.2f, 30.2f},
    {113.7f, 153.7f, 32.0f},
};
static const float kWebFm[3] = {51.3f, 89.5f, 38.7f};   /* the FM's extra: its square, PW, SYNC */
static const float kWebBend = 77.3f;   /* the bits, one of their knobs up */
static const float kWebGlitch = 9.0f;   /* the glitch, taking or repeating its slice (estimated: a few loads a sample) */
static const float kWebWarps[2][6] = {   /* Warps, its algorithm's mark (Eco, HQ) */
    {569.6f, 567.1f, 637.7f, 538.3f, 585.4f, 607.2f},
    {983.7f, 978.5f, 1119.8f, 920.9f, 1015.2f, 1058.8f},
};
static const float kWebFx[3] = {551.1f, 574.9f, 721.7f};   /* the effects, the mix and the master bus (OFF, COMP, OTT) */
static const float kWebFxEq = 37.2f, kWebFxDuck = 6.9f, kWebFxDelay = 15.9f;   /* a track's EQ (playing), the duck on a track, the delay's filter and drive */
static const float kWebFxWarps = 74.6f;   /* Warps in the mix (the rest of the stress test's effects) */
static const float kWebPrep = 288.1f, kWebPrepTrack = 0.0f, kWebPrepCable = 2.8f;   /* the preparation: its floor, a track playing, a cable */
