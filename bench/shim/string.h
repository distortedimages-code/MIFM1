/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* <string.h> for the FM-1 build: felucca-bsp's libc.c has memset, memcpy and memcmp (no memmove: here, inline) */
#ifndef MIFM1_SHIM_STRING_H_
#define MIFM1_SHIM_STRING_H_
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
#ifdef __cplusplus
}
#endif
static inline void *memmove(void *d, const void *s, unsigned n)
{
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    if (dd < ss)
        while (n--)
            *dd++ = *ss++;
    else
        while (n--)
            dd[n] = ss[n];
    return d;
}
#endif
