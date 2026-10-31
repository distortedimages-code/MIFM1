/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* <stdlib.h> for the FM-1 build: integer abs only */
#ifndef MIFM1_SHIM_STDLIB_H_
#define MIFM1_SHIM_STDLIB_H_
#include <stddef.h>
static inline int abs(int x) { return x < 0 ? -x : x; }
#endif
