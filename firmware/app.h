/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* MIFM1: a four-track groovebox for the M-VAVE FM-1 whose whole sound is open source code: Mutable Instruments'
 * (Plaits on every track, Clouds' reverb, Warps) and the LXR-02's drum voices; a step sequencer, three LFOs, a
 * matrix of cables. This header is the app's half of the contract with fm1-base (base/device/unity.c,
 * base/host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "MIFM1"
#define APP_TAGLINE "4-TRACK SYNTHESIS MACHINE"
#define APP_SPLASH_RGB RGB(120, 200, 255)
#define APP_SPLASH                        /* the boot's screen black, not on paper: the intro (app.c) comes next */
void app_splash(void);
#define APP_PALETTE 2                     /* gfx.c's CYAN */
#define APP_BLOCK 128u                    /* frames per audio block: 2.9 ms at 44.1 kHz, the control rate's step */
#define APP_CANVAS_PX (240u * 84u)        /* gfx.c's canvas: the module's display (240 x 82), a knob (60 x 100) */
#define FM1B_UAC_DEEP 1                   /* USB audio input: a block more in hand (4 KB, in the pool), the renders taking
                                          * most of their time (past ~40 % the ring runs dry: holds in the recording) */

/* the store: the project (knobs, models, cables) and the panel's calibration (the platform's, where every fm1-base
 * firmware keeps it). 0xD8000: clear of the sectors other firmwares save in */
enum { OBJ_PROJECT, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xD8000u, 0xDC000u}
#define APP_BACKUP_OBJS {OBJ_PROJECT}
#define APP_BACKUP_MAGIC 0x3146494Du      /* "MIF1" */
#define APP_BACKUP_VERSION 2u             /* (1: the v0.0 bench's results) */
#define APP_BACKUP_WHAT "replaces the saved project"

/* the resource region: the biggest Mutable Instruments tables, out of the program flash, written by the installer
 * after the firmware; mi/res_tables.h, from tools/split_tables.py */
#include "mi/res_tables.h"
#define APP_RES_BASE MIFM1_RES_BASE
#define APP_RES_SECTORS MIFM1_RES_SECTORS

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);
#ifdef PLAT_HOST
#define APP_HOST_REBOOT                  /* the host simulator's "reboot": what RAM would forget, forgotten */
void app_host_reboot(void);
#endif
