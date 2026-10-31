/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* <stdio.h> for the FM-1 build: declared for the vendored code's debugging lines, never linked (nothing calls it) */
#ifndef MIFM1_SHIM_STDIO_H_
#define MIFM1_SHIM_STDIO_H_
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int printf(const char *format, ...);
#ifdef __cplusplus
}
#endif
#endif
