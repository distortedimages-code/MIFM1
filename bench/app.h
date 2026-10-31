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

/* the store: the bench's last results (kept across power-off, and in a backup) and the panel's calibration (the
 * platform's, where every fm1-base firmware keeps it). 0xD8000: clear of the sectors other firmwares save in */
enum { OBJ_RESULTS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xD8000u, 0xDC000u}
#define APP_BACKUP_OBJS {OBJ_RESULTS}
#define APP_BACKUP_MAGIC 0x3146494Du      /* "MIF1" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces the bench results"

/* the resource region: the biggest Mutable Instruments tables, out of the program flash (they do not fit beside the
 * six modules' code), written by the installer after the firmware; mi/res_tables.h, from tools/split_tables.py */
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
