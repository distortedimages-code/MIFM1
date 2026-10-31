#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1: the bench's results from a backup file (the site's backup page, or fm1-base's tools/fm1_backup.py), as a
table: each scene's time in % of an audio block (128 frames, 2902 us), average and peak, core 0, the second core's
part, the whole.

    python3 tools/read_results.py results.mifm1 [--csv]"""
import struct
import sys
import zlib

BACKUP_MAGIC, RESULTS_MAGIC, MAX_SCENES = 0x3146494D, 0x3152494D, 64   # app.h, app.c


def records(img):
    """(object id, payload) of a backup image (base/device/backup.c), its CRCs checked"""
    if len(img) < 20 or struct.unpack_from("<I", img)[0] != BACKUP_MAGIC:
        raise SystemExit("not a MIFM1 backup")
    if zlib.crc32(img[:-4]) != struct.unpack_from("<I", img, len(img) - 4)[0]:
        raise SystemExit("the file is damaged (its CRC)")
    _, nobj, _, _ = struct.unpack_from("<HHII", img, 4)
    at, out = 16, []
    for _ in range(nobj):
        oid, n, crc = struct.unpack_from("<HHI", img, at)
        payload = img[at + 8:at + 8 + n]
        if zlib.crc32(payload) != crc:
            raise SystemExit(f"object {oid}: damaged")
        out.append((oid, payload))
        at += 8 + n
    return out


def main(path, csv=False):
    img = open(path, "rb").read()
    payload = next((p for oid, p in records(img) if p), b"")
    if len(payload) < 8:
        raise SystemExit("no results in this backup (run the bench: REC)")
    magic, count, size = struct.unpack_from("<IHH", payload)
    if magic != RESULTS_MAGIC or size != 16:
        raise SystemExit("results of another version of the bench")
    names_at = 8 + MAX_SCENES * size
    rows = []
    for i in range(count):
        done, core1, c0, c0p, c1, c1p, tot, totp, xr = struct.unpack_from("<BB7H", payload, 8 + i * size)
        name = payload[names_at + i * 16:names_at + i * 16 + 16].split(b"\0")[0].decode("ascii", "replace")
        rows.append((name, done, core1, c0 / 10, c0p / 10, c1 / 10, c1p / 10, tot / 10, totp / 10, xr))
    if csv:
        print("scene,status,core1_ran,c0_avg,c0_peak,c1_avg,c1_peak,total_avg,total_peak,late_blocks")
        for r in rows:
            status = {0: "not run", 1: "ok", 2: "no RAM"}.get(r[1], "?")
            print(",".join([r[0], status, str(r[2]), *[f"{v:.1f}" for v in r[3:9]], str(r[9])]))
        return
    print(f"{'scene':16} {'C0 avg':>7} {'peak':>6} {'C1 avg':>7} {'peak':>6} {'total':>7} {'peak':>6}  late")
    for name, done, core1, c0, c0p, c1, c1p, tot, totp, xr in rows:
        if done != 1:
            print(f"{name:16} {'(no RAM)' if done == 2 else '(not run)'}")
            continue
        two = c1 or c1p or tot != c0
        c1s = (f"{c1:7.1f} {c1p:6.1f}" + ("" if core1 else "*")) if two else f"{'':7} {'':6}"
        print(f"{name:16} {c0:7.1f} {c0p:6.1f} {c1s} {tot:7.1f} {totp:6.1f}  {xr or ''}")
    print("% of an audio block (2902 us); * the second part ran on core 0")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    main(sys.argv[1], "--csv" in sys.argv[2:])
