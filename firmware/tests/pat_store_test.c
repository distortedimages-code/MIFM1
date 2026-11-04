/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* firmware/pat_store.h against a simulated flash (plat.h's two windows, NOR: erased 0xFF, a write only clears bits):
 *   - a blank flash and one full of another firmware's bytes: every sector made MIFM1's, no pattern
 *   - saves and loads, a reboot (the index read again), a slot erased, the slot saved last
 *   - LZSS: every project packs and unpacks to itself
 *   - saves over and over (the room made again and again: pat_gc) against a model of what each slot holds
 *   - a power cut at every flash operation of a save and of a room-making: the slot as it was or as saved, never
 *     neither, every other slot as it was
 *   - the room: how many patterns of a project's kind fit
 *   make test (cc -std=c11 firmware/tests/pat_store_test.c) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"

static uint8_t flash[PLAT_XDATA1_HI - PLAT_XDATA0_LO];
static long cut_after = -1;              /* every erase and write after N fails (a power cut) */
static long ops;
static int cut(void)
{
    ops++;
    if (cut_after == 0)
        return 1;
    if (cut_after > 0)
        cut_after--;
    return 0;
}
const uint8_t *plat_data(uint32_t off) { return PLAT_DATA_OK(off, 1u) ? &flash[off - PLAT_XDATA0_LO] : 0; }
int plat_data_erase(uint32_t off)
{
    if ((off & 0xFFFu) || !PLAT_DATA_OK(off, 0x1000u))
        return -1;
    if (cut())
        return -9;
    memset(&flash[off - PLAT_XDATA0_LO], 0xFF, 0x1000u);
    return 0;
}
int plat_data_write(uint32_t off, const void *src, uint32_t n)
{
    if (!PLAT_DATA_OK(off, n) || (off & 0xFFu) + n > 256u) {
        printf("bad write at %#x (%u)\n", off, n);
        exit(1);
    }
    if (cut())
        return -9;
    for (uint32_t i = 0; i < n; i++)
        flash[off - PLAT_XDATA0_LO + i] &= ((const uint8_t *)src)[i];
    return 0;
}

#include "../pat_store.h"

#define PSIZE 3776u                      /* a project's bytes (engine.h project_t) */
static int bad;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* a project of a kind: its 272 knobs from a few values, its choices, a few locks then free slots (0xFF), cables */
static uint32_t rs = 1;
static uint32_t rnd(void) { return rs = rs * 1664525u + 1013904223u; }
static void project(uint8_t *p, uint32_t seed, int locks)
{
    static const float vals[] = {0.0f, 0.5f, 0.25f, 0.75f, 1.0f, 0.3f, 0.6f, 0.1f, 0.85f, 0.4f, -0.5f, 120.0f};
    uint32_t o = 0;
    rs = seed * 2654435761u + 7u;
    memset(p, 0, PSIZE);
    p[o++] = 'P', p[o++] = 'R', p[o++] = 'J', p[o++] = '1', o += 4;
    for (int i = 0; i < 272; i++, o += 4) {
        float v = i % 9 ? vals[(i * 7u + seed % 3u) % 12u] : rnd() % 3u ? vals[rnd() % 12u] : (float)(rnd() % 1000u) / 1000.0f;
        memcpy(p + o, &v, 4);
    }
    for (int i = 0; i < 68; i++)
        p[o++] = (uint8_t)(rnd() % 6u ? 0 : rnd() % 23u);
    o += 8;
    for (int i = 0; i < 40; i++, o += 4)
        if (rnd() % 4u == 0)
            p[o] = (uint8_t)(1 + rnd() % 3u), p[o + 1] = (uint8_t)(rnd() % 255u), p[o + 2] = (uint8_t)(rnd() % 200u);
    for (int i = 0; i < 7; i++, o += 8)
        for (int k = 0; k < 8; k++)
            p[o + k] = (uint8_t)(rnd() % 4u ? 0 : 1u << (rnd() % 8u));
    o += 7 * 24 + 14;
    for (int i = 0; i < 384; i++, o += 4)
        if (i < locks)
            p[o] = (uint8_t)(rnd() % 7u), p[o + 1] = (uint8_t)(rnd() % 64u), p[o + 2] = (uint8_t)(rnd() % 24u),
            p[o + 3] = (uint8_t)rnd();
        else
            p[o] = 0xFFu;
    for (; o < PSIZE; o++)
        p[o] = (uint8_t)(rnd() % 12u ? 0 : rnd() % 128u);
}

static uint8_t model[PAT_SLOTS][PSIZE];  /* what each slot must load */
static uint8_t full[PAT_SLOTS];
static int matches(int s)                /* the slot loads as the model has it */
{
    static uint8_t got[PSIZE];
    int n = pat_load(s, got, sizeof got);
    if (!full[s])
        return n < 0 && !pat_full(s);
    return n == (int)PSIZE && !memcmp(got, model[s], PSIZE);
}
static int all_match(void)
{
    for (int s = 0; s < PAT_SLOTS; s++)
        if (!matches(s))
            return 0;
    return 1;
}

int main(void)
{
    static uint8_t p[PSIZE], q[PSIZE], z[PSIZE + 512];
    char what[96];

    /* a blank flash; one full of another firmware's bytes */
    memset(flash, 0xFF, sizeof flash);
    check("a blank flash: read, every sector made MIFM1's, no pattern", pat_init() == 0 && pat_last < 0 && !pat_full(0));
    for (uint32_t i = 0; i < sizeof flash; i++)
        flash[i] = (uint8_t)rnd();
    pat_init();
    {
        int mine = 1, none = 1;
        for (uint32_t i = 0; i < PAT_NSEC; i++)
            mine &= ((const pat_sec_t *)plat_data(pat_sec(i)))->magic == PAT_SEC_MAGIC && pat_end[i] == 16u;
        for (int s = 0; s < PAT_SLOTS; s++)
            none &= !pat_full(s);
        check("another firmware's bytes: every sector erased and made MIFM1's, no pattern", mine && none);
        int kept = 1;
        for (uint32_t i = 0; i < PAT_NSEC; i++)
            kept &= flash[pat_sec(i) - PLAT_XDATA0_LO + 0xF06] == 0xFFu && flash[pat_sec(i) - PLAT_XDATA0_LO + 0xF07] == 0xFFu;
        check("the update loader's bytes (0xF00..0xF07 of each sector) left erased", kept);
    }

    /* LZSS */
    {
        int ok = 1;
        uint32_t most = 0, least = 99999;
        for (uint32_t k = 0; k < 200; k++) {
            project(p, k, (int)(k % 120u));
            uint32_t n = lz_pack(p, PSIZE, z, sizeof z);
            ok &= n > 0 && lz_unpack(z, n, q, PSIZE) == (int)PSIZE && !memcmp(p, q, PSIZE);
            most = n > most ? n : most;
            least = n < least ? n : least;
        }
        snprintf(what, sizeof what, "LZSS: 200 projects packed and unpacked to themselves (%u to %u bytes)", least, most);
        check(what, ok);
        memset(q, 0, sizeof q);
        check("LZSS: a broken stream refused, never past its buffer", lz_unpack((const uint8_t *)"\x00\xFF\xFF", 3, q, 16) < 0);
    }

    /* saves, loads, a reboot, an erase */
    memset(full, 0, sizeof full);
    for (int s = 0; s < 20; s++) {
        project(model[s], 1000u + (uint32_t)s, s * 5);
        full[s] = pat_save(s, model[s], PSIZE) == 0;
    }
    check("20 patterns saved, each loads as saved", all_match() && full[19]);
    check("the slot saved last: the last one", pat_last == 19);
    pat_init();
    check("a reboot: the same 20, the slot saved last the same", all_match() && pat_last == 19);
    pat_erase(7);
    full[7] = 0;
    pat_init();
    check("a slot erased: empty, after a reboot too, the others as they were", all_match());

    /* over and over: the room made again and again */
    {
        int ok = 1, gcs = 0;
        for (int k = 0; k < 3000 && ok; k++) {
            int s = (int)(rnd() % 60u);
            if (rnd() % 10u == 0) {
                ok &= pat_erase(s) == 0;
                full[s] = 0;
            } else {
                project(model[s], 5000u + (uint32_t)k, (int)(rnd() % 100u));
                int e = pat_save(s, model[s], PSIZE);
                ok &= e == 0;
                full[s] = 1;
            }
            if (k % 250 == 0) {
                pat_init();
                gcs++;
                ok &= all_match();
            }
        }
        pat_init();
        snprintf(what, sizeof what, "3000 saves and erases over 60 slots (%d reboots): each slot as the model says", gcs);
        check(what, ok && all_match());
    }

    /* a power cut at every flash operation of a save (and of the room it makes) */
    {
        int ok = 1, tried = 0, in_gc = 0;
        static uint8_t snap[sizeof flash], before[PAT_SLOTS][PSIZE], fb[PAT_SLOTS];
        for (int s = 50; s < 58; s++) {      /* (the store well filled: room made at many saves) */
            project(model[s], 3000u + (uint32_t)s, 40);
            full[s] = pat_save(s, model[s], PSIZE) == 0 || full[s];
        }
        for (long n = 0; n < 900 && ok; n++) {
            int s = (int)(rnd() % 58u);
            memcpy(snap, flash, sizeof flash);
            memcpy(before, model, sizeof model);
            memcpy(fb, full, sizeof full);
            project(p, 90000u + (uint32_t)n, (int)(rnd() % 120u));
            ops = 0;                         /* (how many it takes uncut: more than a record's, room was made) */
            {
                static uint8_t f2[sizeof flash];
                memcpy(f2, flash, sizeof flash);
                pat_save(s, p, PSIZE);
                in_gc += ops > 12 && (long)(n % 150) < ops && (long)(n % 150) >= 6;
                memcpy(flash, f2, sizeof flash);
                pat_init();
            }
            ops = 0;
            cut_after = n % 150;             /* (the cut somewhere in it: the record, a room-making's copies or erase) */
            int e = pat_save(s, p, PSIZE);
            cut_after = -1;
            pat_init();                      /* (the power back) */
            static uint8_t got[PSIZE];
            int m = pat_load(s, got, sizeof got);
            int as_was = fb[s] ? m == (int)PSIZE && !memcmp(got, before[s], PSIZE) : m < 0;
            int as_saved = m == (int)PSIZE && !memcmp(got, p, PSIZE);
            ok &= as_was || as_saved;
            if (e == 0 && ops <= n % 150)
                ok &= as_saved;              /* (it went through before the cut) */
            memcpy(model[s], as_saved ? p : before[s], PSIZE);
            full[s] = (uint8_t)(as_saved || fb[s]);
            for (int o = 0; o < PAT_SLOTS && ok; o++)
                if (o != s)
                    ok &= matches(o);
            tried++;
            if (rnd() % 2u)
                memcpy(flash, snap, sizeof flash), memcpy(model, before, sizeof model), memcpy(full, fb, sizeof full),
                    pat_init();
        }
        snprintf(what, sizeof what, "a power cut in %d saves (%d while making room): the slot as it was or as saved", tried,
                 in_gc);
        check(what, ok);
    }

    /* the room */
    {
        int n = 0;
        memset(flash, 0xFF, sizeof flash);
        pat_init();
        memset(full, 0, sizeof full);
        for (int s = 0; s < PAT_SLOTS; s++) {
            project(model[s], 70000u + (uint32_t)s, 30);
            if (pat_save(s, model[s], PSIZE) != 0)
                break;
            full[s] = 1;
            n++;
        }
        snprintf(what, sizeof what, "the room: %d patterns of 30 locks each fit, all of them loading", n);
        check(what, n >= 70 && all_match());
        check("full: a save refused (no room), nothing lost", n == PAT_SLOTS || (pat_save(n, model[0], PSIZE) == -1 && all_match()));
    }
    printf(bad ? "pat_store: %d FAILED\n" : "pat_store: all good\n", bad);
    return bad != 0;
}
