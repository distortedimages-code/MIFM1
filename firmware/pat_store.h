/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Yoann */
/* The patterns (0.15): 11 banks of 16, each a whole project (its sounds too), kept in the flash's two windows for an
 * app's big data (plat.h: PLAT_XDATA1, 23 sectors, then PLAT_XDATA0, 4), outside the store (its objects take a sector
 * pair each: 176 would not fit). Each one packed (LZSS, lz_pack: a project's 3776 bytes in 650 to 1000 or so), so
 * some 100 fit where 27 whole ones would.
 *
 * A sector: a header (its magic), then records one after the other, never across sectors, in its first 0xF00 bytes
 * (plat.h: the update loader reads a record of its own at 0xF00..0xF07 of a sector there: those stay erased). A
 * record: a header (its slot, its sequence number, its length, its bytes' CRC, the header's own CRC, a commit byte),
 * then its bytes. Written in that order, the commit byte (0xFF to 0) last: a power cut leaves a record whole or
 * ignored. A slot's pattern is its committed record with the highest sequence number, wherever it is; an erased slot
 * a record of no bytes. A save appends; when no sector has room, one sector is kept empty for this: the sector with
 * the most dead records gives its live ones to it and is erased, the spare from then on (pat_gc). That move has its
 * own commit, in the spare's header: its number (gcseq) written before the copies, its done word after them. At boot
 * every sector is read once (pat_init): each slot's record found; a spare whose move did not finish erased (the
 * sector it came from still whole); of two copies of a record (a move done, its source not erased yet), the moved one
 * (the higher gcseq); then every sector with no live record left, and those not MIFM1's, erased and made its.
 *
 * Main loop only (an erase or a write stops the sound for a moment: plat.h). Included by app.c, and by
 * tests/pat_store_test.c (a simulated flash, power cuts anywhere) */
#define PAT_BANKS 11
#define PAT_PER_BANK 16
#define PAT_SLOTS (PAT_BANKS * PAT_PER_BANK)
#define PAT_NSEC 27u                     /* PLAT_XDATA1's 23 sectors, then PLAT_XDATA0's 4 */
#define PAT_USE 0xF00u                   /* the bytes of a sector used (0xF00..0xF07: the update loader's) */
#define PAT_SEC_MAGIC 0x3150494Du        /* "MIP1" */
#define PAT_REC_MAGIC 0x5450u            /* "PT" */
#define PAT_RAW 2u                       /* a record's bytes as they are */
#define PAT_LZ 1u                        /* packed */

typedef struct {
    uint32_t magic;
    uint32_t gcseq, done;                /* a move's (pat_gc) into it: its number, then 0 once its copies are all in;
                                          * 0xFFFFFFFF both: a sector made as it is */
    uint32_t rsv;
} pat_sec_t;
typedef struct {
    uint16_t magic;
    uint8_t slot, kind;                  /* kind: PAT_LZ, PAT_RAW, 0 the slot erased */
    uint32_t seq;
    uint16_t len, crc;                   /* its bytes, their CRC-16 */
    uint16_t hcrc;                       /* the 12 bytes before */
    uint8_t pad, commit;                 /* commit: 0xFF while written, 0 done */
} pat_rec_t;
_Static_assert(sizeof(pat_sec_t) == 16 && sizeof(pat_rec_t) == 16, "the flash's layout");

static uint32_t pat_at[PAT_SLOTS];       /* each slot's record (its flash offset), 0 none */
static uint16_t pat_end[PAT_NSEC];       /* each sector's first free byte (PAT_USE: full or unusable) */
static uint32_t pat_seq = 1;             /* the next record's */
static int pat_last = -1;                /* the slot saved last (a pattern in it), -1 none */
static int pat_ok;                       /* the windows there (an FM-1, the host, the page) */
static uint32_t pat_gcseq = 1;           /* the next move's */

static uint32_t pat_sec(uint32_t i) { return i < 23u ? PLAT_XDATA1_LO + i * 0x1000u : PLAT_XDATA0_LO + (i - 23u) * 0x1000u; }

static uint16_t pat_crc(const uint8_t *p, uint32_t n)   /* CRC-16/CCITT-FALSE */
{
    uint16_t c = 0xFFFFu;
    while (n--) {
        c ^= (uint16_t)(*p++ << 8);
        for (int b = 0; b < 8; b++)
            c = (uint16_t)(c & 0x8000u ? (uint32_t)(c << 1) ^ 0x1021u : (uint32_t)(c << 1));
    }
    return c;
}

/* LZSS: a flags byte (bit set: a literal) then up to 8 items; a match 2 bytes, its distance (1..4095) on 12 bits,
 * its length (3..18) on 4. One candidate a position (a hash of its 3 bytes): quick, and enough for a project */
static uint32_t lz_hash(const uint8_t *p) { return ((p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16) * 2654435761u) >> 22; }
static uint32_t lz_pack(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)   /* bytes out, 0: no room */
{
    uint16_t head[1024];
    uint32_t i = 0, o = 0, fpos = 0, bit = 0;
    memset(head, 0xFF, sizeof head);
    while (i < n) {
        uint32_t best = 0, dist = 0;
        if (bit == 0) {
            if (o >= max)
                return 0;
            fpos = o++;
            out[fpos] = 0;
        }
        if (i + 3u <= n) {
            uint32_t h = lz_hash(in + i), c = head[h];
            head[h] = (uint16_t)i;
            if (c != 0xFFFFu && i - c <= 4095u) {
                uint32_t len = 0;
                while (len < 18u && i + len < n && in[c + len] == in[i + len])
                    len++;
                if (len >= 3u)
                    best = len, dist = i - c;
            }
        }
        if (best) {
            if (o + 2u > max)
                return 0;
            out[o++] = (uint8_t)dist;
            out[o++] = (uint8_t)((dist >> 8) << 4 | (best - 3u));
            for (uint32_t k = 1; k < best; k++)
                if (i + k + 3u <= n)
                    head[lz_hash(in + i + k)] = (uint16_t)(i + k);
            i += best;
        } else {
            if (o >= max)
                return 0;
            out[fpos] |= (uint8_t)(1u << bit);
            out[o++] = in[i++];
        }
        bit = (bit + 1u) & 7u;
    }
    return o;
}
static int lz_unpack(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)   /* bytes out, < 0 bad */
{
    uint32_t i = 0, o = 0;
    while (i < n) {
        uint32_t f = in[i++];
        for (uint32_t b = 0; b < 8u && i < n; b++) {
            if (f >> b & 1u) {
                if (o >= max)
                    return -1;
                out[o++] = in[i++];
            } else {
                uint32_t d, len;
                if (i + 1u >= n)
                    return -1;
                d = in[i] | (uint32_t)(in[i + 1] >> 4) << 8;
                len = (in[i + 1] & 15u) + 3u;
                i += 2u;
                if (d == 0 || d > o || o + len > max)
                    return -1;
                while (len--) {
                    out[o] = out[o - d];
                    o++;
                }
            }
        }
    }
    return (int)o;
}

static int pat_write(uint32_t at, const uint8_t *p, uint32_t n)   /* from RAM, never across a 256-byte page */
{
    while (n) {
        uint32_t k = 256u - (at & 255u);
        k = k < n ? k : n;
        if (plat_data_write(at, p, k))
            return -1;
        at += k, p += k, n -= k;
    }
    return 0;
}

static int pat_rec_at(uint32_t at, pat_rec_t *r)   /* 1: a committed record whose bytes are whole */
{
    const uint8_t *f = plat_data(at);
    if (!f)
        return 0;
    memcpy(r, f, sizeof *r);
    return r->magic == PAT_REC_MAGIC && pat_crc((const uint8_t *)r, 12) == r->hcrc && r->commit == 0 &&
           r->slot < PAT_SLOTS && pat_crc(f + sizeof *r, r->len) == r->crc;
}

static int pat_format(uint32_t i)        /* erased, and MIFM1's: its header */
{
    pat_sec_t h = {PAT_SEC_MAGIC, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    pat_end[i] = PAT_USE;
    if (plat_data_erase(pat_sec(i)) || pat_write(pat_sec(i), (const uint8_t *)&h, sizeof h))
        return -1;
    pat_end[i] = sizeof h;
    return 0;
}

/* a sector's records, each as it is found: its offset, its header, whether it is committed and whole; the first free
 * byte returned (PAT_USE: none, or a broken header: nothing written after it) */
typedef void (*pat_visit_t)(uint32_t at, const pat_rec_t *r, int ok, void *arg);
static uint32_t pat_walk(uint32_t i, pat_visit_t fn, void *arg)
{
    uint32_t off = sizeof(pat_sec_t);
    const uint8_t *s = plat_data(pat_sec(i));
    while (off + sizeof(pat_rec_t) <= PAT_USE) {
        pat_rec_t r;
        int blank = 1;
        memcpy(&r, s + off, sizeof r);
        for (uint32_t k = 0; k < sizeof r; k++)
            blank &= s[off + k] == 0xFFu;
        if (blank)
            return off;                  /* (the free bytes from here) */
        if (r.magic != PAT_REC_MAGIC || pat_crc((const uint8_t *)&r, 12) != r.hcrc ||
            off + sizeof r + r.len > PAT_USE)
            return PAT_USE;              /* (a header torn: nothing after it) */
        fn(pat_sec(i) + off, &r, r.commit == 0 && r.slot < PAT_SLOTS &&
                                 pat_crc(s + off + sizeof r, r.len) == r.crc, arg);
        off += (uint32_t)sizeof r + ((r.len + 3u) & ~3u);
    }
    return PAT_USE;
}

static uint32_t *pat_best, *pat_rank;    /* (pat_init: each slot's sequence number so far, the move its copy came in) */
static uint32_t pat_newest_seq;
static void pat_visit_init(uint32_t at, const pat_rec_t *r, int ok, void *arg)
{
    uint32_t rank = *(const uint32_t *)arg;
    if (!ok)
        return;
    if (r->seq >= pat_seq)
        pat_seq = r->seq + 1u;
    if (!pat_at[r->slot] || r->seq > pat_best[r->slot] ||
        (r->seq == pat_best[r->slot] && rank > pat_rank[r->slot])) {
        pat_best[r->slot] = r->seq;
        pat_rank[r->slot] = rank;
        pat_at[r->slot] = at;
    }
}
static uint32_t pat_sector_of(uint32_t at)
{
    for (uint32_t i = 0; i < PAT_NSEC; i++)
        if (at >= pat_sec(i) && at < pat_sec(i) + 0x1000u)
            return i;
    return PAT_NSEC;
}

static int pat_full(int slot)            /* 1: a pattern there */
{
    pat_rec_t r;
    if (slot < 0 || slot >= PAT_SLOTS || !pat_at[slot])
        return 0;
    memcpy(&r, plat_data(pat_at[slot]), sizeof r);
    return r.kind != 0;
}

static int pat_init(void)                /* at boot: each slot's record; the sectors not MIFM1's erased and made its */
{
    uint32_t best[PAT_SLOTS], rank[PAT_SLOTS], live[PAT_NSEC];
    pat_ok = plat_data(pat_sec(0)) && plat_data(pat_sec(PAT_NSEC - 1u));
    if (!pat_ok)
        return -1;
    memset(pat_at, 0, sizeof pat_at);
    memset(best, 0, sizeof best);
    memset(rank, 0, sizeof rank);
    memset(live, 0, sizeof live);
    pat_best = best;
    pat_rank = rank;
    pat_seq = 1;
    pat_gcseq = 1;
    for (uint32_t i = 0; i < PAT_NSEC; i++) {
        pat_sec_t h;
        uint32_t r;
        memcpy(&h, plat_data(pat_sec(i)), sizeof h);
        if (h.magic != PAT_SEC_MAGIC || (h.gcseq != 0xFFFFFFFFu && h.done != 0)) {
            pat_end[i] = 0;              /* (not MIFM1's, or a move into it cut short: made MIFM1's below) */
            continue;
        }
        r = h.gcseq == 0xFFFFFFFFu ? 0u : h.gcseq;
        if (r >= pat_gcseq)
            pat_gcseq = r + 1u;
        pat_end[i] = (uint16_t)pat_walk(i, pat_visit_init, &r);
    }
    for (int s = 0; s < PAT_SLOTS; s++)  /* (each sector's live records: none left, it is erased) */
        if (pat_at[s] && pat_sector_of(pat_at[s]) < PAT_NSEC)
            live[pat_sector_of(pat_at[s])]++;
    for (uint32_t i = 0; i < PAT_NSEC; i++)
        if (!pat_end[i] || (pat_end[i] > sizeof(pat_sec_t) && !live[i]))
            pat_format(i);
    pat_last = -1;
    pat_newest_seq = 0;
    for (int s = 0; s < PAT_SLOTS; s++)
        if (pat_full(s) && best[s] >= pat_newest_seq)
            pat_newest_seq = best[s], pat_last = s;
    pat_best = pat_rank = 0;
    return 0;
}

static uint32_t pat_rec_size(const pat_rec_t *r) { return (uint32_t)sizeof *r + ((r->len + 3u) & ~3u); }

typedef struct {
    uint32_t live;
} pat_live_t;
static void pat_visit_live(uint32_t at, const pat_rec_t *r, int ok, void *arg)
{
    if (ok && pat_at[r->slot] == at)
        ((pat_live_t *)arg)->live += pat_rec_size(r);
}

typedef struct {
    uint32_t to;
    int err;
} pat_move_t;
static void pat_visit_move(uint32_t at, const pat_rec_t *r, int ok, void *arg)   /* a live record copied to ->to */
{
    pat_move_t *m = (pat_move_t *)arg;
    uint8_t buf[256];
    uint32_t n = pat_rec_size(r), done = 0;
    if (!ok || pat_at[r->slot] != at || m->err)
        return;
    while (done < n) {
        uint32_t k = n - done < sizeof buf ? n - done : (uint32_t)sizeof buf;
        memcpy(buf, plat_data(at + done), k);
        if (pat_write(m->to + done, buf, k)) {
            m->err = 1;
            return;
        }
        done += k;
    }
    pat_at[r->slot] = m->to;
    m->to += n;
}

/* room again: the sector with the most dead records gives its live ones to the empty one kept for this, and is
 * erased (the empty one from then on). 0 done, -1 nothing to gain */
static int pat_gc(void)
{
    int sp = -1, v = -1;
    uint32_t vdead = 0;
    for (uint32_t i = 0; i < PAT_NSEC && sp < 0; i++)
        if (pat_end[i] == sizeof(pat_sec_t))
            sp = (int)i;
    if (sp < 0)
        return -1;
    for (uint32_t i = 0; i < PAT_NSEC; i++) {
        pat_live_t lv = {0};
        uint32_t used;
        if ((int)i == sp || pat_end[i] <= sizeof(pat_sec_t))
            continue;
        pat_walk(i, pat_visit_live, &lv);
        used = pat_end[i] - (uint32_t)sizeof(pat_sec_t);
        if (pat_end[i] == PAT_USE)       /* (a sector closed early: all its bytes count) */
            used = PAT_USE - (uint32_t)sizeof(pat_sec_t);
        if (used - lv.live > vdead)
            vdead = used - lv.live, v = (int)i;
    }
    if (v < 0)
        return -1;
    pat_move_t m = {pat_sec((uint32_t)sp) + (uint32_t)sizeof(pat_sec_t), 0};
    uint32_t zero = 0, gs = pat_gcseq++;
    pat_end[sp] = PAT_USE;               /* (a failure from here: nothing more written in it) */
    if (pat_write(pat_sec((uint32_t)sp) + 4u, (const uint8_t *)&gs, 4))   /* the move's number, then its copies */
        return -1;
    pat_walk((uint32_t)v, pat_visit_move, &m);
    if (m.err || pat_write(pat_sec((uint32_t)sp) + 8u, (const uint8_t *)&zero, 4))   /* all in: done */
        return -1;
    pat_end[sp] = (uint16_t)(m.to - pat_sec((uint32_t)sp));
    return pat_format((uint32_t)v);
}

static int pat_place(uint32_t need)      /* a sector with room for need bytes (the last empty one kept), -1 none */
{
    int empty = 0, first_empty = -1;
    for (uint32_t i = 0; i < PAT_NSEC; i++) {
        if (pat_end[i] == sizeof(pat_sec_t)) {
            empty++;
            if (first_empty < 0)
                first_empty = (int)i;
        } else if (pat_end[i] < PAT_USE && pat_end[i] + need <= PAT_USE) {
            return (int)i;
        }
    }
    return empty >= 2 ? first_empty : -1;
}

static int pat_append(int slot, uint32_t kind, const uint8_t *p, uint32_t n)   /* 0 done, -1 no room, -2 flash */
{
    pat_rec_t r;
    uint32_t need = (uint32_t)sizeof r + ((n + 3u) & ~3u), at;
    int i, tries = 0;
    while ((i = pat_place(need)) < 0)
        if (++tries > (int)PAT_NSEC || pat_gc() != 0)
            return -1;
    at = pat_sec((uint32_t)i) + pat_end[i];
    r.magic = PAT_REC_MAGIC;
    r.slot = (uint8_t)slot;
    r.kind = (uint8_t)kind;
    r.seq = pat_seq;
    r.len = (uint16_t)n;
    r.crc = pat_crc(p, n);
    r.hcrc = pat_crc((const uint8_t *)&r, 12);
    r.pad = 0xFFu;
    r.commit = 0xFFu;
    pat_end[i] = PAT_USE;                /* (a failure from here: nothing more written in it) */
    if (pat_write(at, (const uint8_t *)&r, sizeof r) || (n && pat_write(at + sizeof r, p, n)))
        return -2;
    r.commit = 0;
    if (pat_write(at + 15u, &r.commit, 1))
        return -2;
    pat_end[i] = (uint16_t)(at - pat_sec((uint32_t)i) + need);
    pat_at[slot] = at;
    pat_seq++;
    return 0;
}

static int pat_save(int slot, const void *src, uint32_t n)   /* 0 done, -1 no room, -2 flash */
{
    uint8_t buf[PAT_USE - 32u];
    uint32_t k;
    int e;
    if (!pat_ok || slot < 0 || slot >= PAT_SLOTS || n > sizeof buf)
        return -2;
    k = lz_pack((const uint8_t *)src, n, buf, sizeof buf);
    if (k && k < n)
        e = pat_append(slot, PAT_LZ, buf, k);
    else
        e = pat_append(slot, PAT_RAW, (const uint8_t *)src, n);
    if (e == 0)
        pat_last = slot;
    return e;
}

static int pat_erase(int slot)           /* 0 done (or nothing there), -1 no room, -2 flash */
{
    int e;
    if (!pat_full(slot))
        return 0;
    e = pat_append(slot, 0, 0, 0);
    if (e == 0 && pat_last == slot)
        pat_last = -1;
    return e;
}

static int pat_load(int slot, void *dst, uint32_t max)   /* its bytes into dst: their count, < 0 none or bad */
{
    pat_rec_t r;
    const uint8_t *f;
    if (!pat_full(slot) || !pat_rec_at(pat_at[slot], &r))
        return -1;
    f = plat_data(pat_at[slot]) + sizeof r;
    if (r.kind == PAT_RAW) {
        if (r.len > max)
            return -1;
        memcpy(dst, f, r.len);
        return r.len;
    }
    return lz_unpack(f, r.len, (uint8_t *)dst, max);
}

static uint32_t pat_room(void)           /* the bytes free for patterns, the dead ones counted (a save frees them) */
{
    uint32_t live = 0, total = (PAT_NSEC - 1u) * (PAT_USE - (uint32_t)sizeof(pat_sec_t));   /* (one kept empty) */
    for (int s = 0; s < PAT_SLOTS; s++) {
        pat_rec_t r;
        if (pat_at[s]) {
            memcpy(&r, plat_data(pat_at[s]), sizeof r);
            live += pat_rec_size(&r);
        }
    }
    return live < total ? total - live : 0u;
}
