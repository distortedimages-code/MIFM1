/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* malloc for a wasm module of a fixed memory (tools/web/emcc with -sALLOW_MEMORY_GROWTH=0: an .rfm1, build.py
 * --rfm1): the heap is the memory after the data and the stack (__heap_base) to its end, as Emscripten's malloc has it.
 * wasi-libc's (Zig's) asks memory.grow for its every byte and never uses that part: with no growth, every malloc
 * failed (0.16.55: RemoteFM1 refused MIFM1, "rfm1_alloc(17440) failed"). A first fit on a list of free blocks kept in
 * address order, each with a header of its size (16-byte aligned, a block 16 bytes at least), merged with its
 * neighbours when freed. One thread (the format's: one call at a time). */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern unsigned char __heap_base;

typedef struct blk {
    size_t size;                        /* the whole block, its header included (a multiple of 16) */
    struct blk *next;                   /* (free blocks) the next free block, in address order */
} blk_t;
#define HDR 16u                         /* the header's room: the payload 16-byte aligned */
static blk_t *free_list;
static int ready;

static void heap_init(void)
{
    uintptr_t lo = ((uintptr_t)&__heap_base + 15u) & ~(uintptr_t)15u;
    uintptr_t hi = (uintptr_t)__builtin_wasm_memory_size(0) * 65536u;
    ready = 1;
    if (hi <= lo + 2u * HDR)
        return;
    free_list = (blk_t *)lo;
    free_list->size = (size_t)((hi - lo) & ~(uintptr_t)15u);
    free_list->next = 0;
}

void *malloc(size_t n)
{
    blk_t **pp, *b;
    size_t need;
    if (!ready)
        heap_init();
    if (n > SIZE_MAX - 2u * HDR)
        return 0;
    need = (n + HDR + 15u) & ~(size_t)15u;
    for (pp = &free_list; (b = *pp); pp = &b->next)
        if (b->size >= need) {
            if (b->size - need >= 2u * HDR) {   /* the rest stays free */
                blk_t *r = (blk_t *)((unsigned char *)b + need);
                r->size = b->size - need;
                r->next = b->next;
                *pp = r;
                b->size = need;
            } else
                *pp = b->next;
            return (unsigned char *)b + HDR;
        }
    return 0;
}

void free(void *p)
{
    blk_t *b, *prev = 0, *cur;
    if (!p)
        return;
    b = (blk_t *)((unsigned char *)p - HDR);
    for (cur = free_list; cur && cur < b; cur = cur->next)
        prev = cur;
    b->next = cur;
    if (cur && (unsigned char *)b + b->size == (unsigned char *)cur) {   /* merged with the next */
        b->size += cur->size;
        b->next = cur->next;
    }
    if (prev && (unsigned char *)prev + prev->size == (unsigned char *)b) {   /* and the previous */
        prev->size += b->size;
        prev->next = b->next;
    } else if (prev)
        prev->next = b;
    else
        free_list = b;
}

void *calloc(size_t k, size_t n)
{
    void *p;
    if (n && k > SIZE_MAX / n)
        return 0;
    p = malloc(k * n);
    if (p)
        memset(p, 0, k * n);
    return p;
}

void *realloc(void *p, size_t n)
{
    void *q;
    size_t have;
    if (!p)
        return malloc(n);
    if (!n) {
        free(p);
        return 0;
    }
    have = ((blk_t *)((unsigned char *)p - HDR))->size - HDR;
    if (have >= n)
        return p;
    q = malloc(n);
    if (q) {
        memcpy(q, p, have);
        free(p);
    }
    return q;
}

void *aligned_alloc(size_t a, size_t n) { return a <= 16u ? malloc(n) : 0; }
int posix_memalign(void **out, size_t a, size_t n)
{
    void *p = a <= 16u ? malloc(n) : 0;
    if (!p)
        return 12;                      /* ENOMEM */
    *out = p;
    return 0;
}

/* (tests: the bytes free now, the largest block) */
__attribute__((used, visibility("default"), export_name("heap_free_bytes"))) size_t heap_free_bytes(int largest)
{
    size_t t = 0, m = 0;
    blk_t *b;
    if (!ready)
        heap_init();
    for (b = free_list; b; b = b->next) {
        t += b->size;
        if (b->size > m)
            m = b->size;
    }
    return largest ? m : t;
}
