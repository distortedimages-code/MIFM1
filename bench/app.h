/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1 v0.0, the bench: how much of the FM-1 each Mutable Instruments module takes, alone and in busy mixes over
 * both cores. This header is the app's half of the contract with fm1-base (base/device/unity.c,
 * base/host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "MIFM1"
#define APP_TAGLINE "BANC D'ESSAI"
#define APP_SPLASH_RGB RGB(120, 200, 255)
#define APP_BLOCK 128u                    /* frames per audio block: 2.9 ms at 44.1 kHz (X0X's, Mutable Dreams') */
#define APP_CANVAS_PX (240u * 48u)        /* gfx.c's canvas: the screen drawn in strips of three text lines; the pool
                                           * goes to the modules */

/* the store: only the panel's calibration (the platform's), where every fm1-base firmware keeps it */
enum { OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
