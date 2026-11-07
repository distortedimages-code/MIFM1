#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""The screen's mirror against the screen (0.16.53): a stream the host simulator recorded (`mirror out:NAME.syx`),
decoded as the site's mirror page decodes it (fm1-base's tools/fm1_mirror.py Screen), and the screen itself at the
end (`fbdump out:NAME.raw`, 240 x 240 RGB565, high byte first): the same pixels, every one.

    python3 tools/mirror_check.py OUTDIR NAME [NAME...]     (OUTDIR/NAME.syx against OUTDIR/NAME.raw)

Exit 0: each the same; 1: a pixel apart, a frame the decoder refused, or no frame at all."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "base", "tools"))
import fm1_mirror as M  # noqa: E402


def check(out, name):
    with open(os.path.join(out, name + ".syx"), "rb") as f:
        stream = f.read()
    with open(os.path.join(out, name + ".raw"), "rb") as f:
        raw = f.read()
    scr = M.Screen()
    for pkt in M.split(stream):
        scr.frame(pkt)
    want = [raw[2 * i] << 8 | raw[2 * i + 1] for i in range(M.W * M.H)]
    apart = sum(1 for a, b in zip(scr.px, want) if a != b)
    ok = scr.frames > 0 and not scr.bad and not apart and len(raw) == 2 * M.W * M.H
    print(f"  {'ok  ' if ok else 'FAIL'}  mirror {name}: {len(stream)} B, {scr.frames} frames ({scr.bad} refused), "
          f"{apart} pixels apart from the screen")
    if not ok:
        scr.png(os.path.join(out, name + "_mirror.png"))
    return ok


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    return 0 if all([check(argv[1], n) for n in argv[2:]]) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
