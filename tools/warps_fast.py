#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""Warps' cross-modulations, lighter on the FM-1, the same sound (patch_mi.sh 7): ProcessXmod<a1, a2> plays two
algorithms and crossfades them by ALGORITHM's fraction; on a mark (the fraction 0 over the whole block: MIFM1's
ALGORITHM selector jumps from mark to mark) the second one's weight is 0, its work thrown away. There the loop plays
the first alone: the same samples (a + (b - a) * 0 is a), half the work of the algorithms at 6x.

(0.3.5 and 0.3.6 also tried to run the modulator's loops from RAM, out of the XIP cache's way: the FM-1's clang has
no #pragma clang section, and a section attribute puts them out of reach of the flash's calls, and the flash's of
theirs, on its 23-bit jumps. What they left in the files is taken out here.)

  tools/warps_fast.py firmware/third_party/warps
"""
import sys
from pathlib import Path

MARK = "MIFM1: one algorithm on a mark"
FAST = """    if (balance == 0.0f && balance_end == 0.0f) {  // MIFM1: one algorithm on a mark (its weight 0: the same
      float parameter_increment = (parameter_end - parameter) * (1.0f / static_cast<float>(size));   // samples,
                                                                   // half the work)
      while (size--) {
        *out++ = Xmod<algorithm_1>(*in_1++, *in_2++, parameter);
        parameter += parameter_increment;
      }
      return;
    }
"""
ANCHOR = """      size_t size) {
    float step = 1.0f / static_cast<float>(size);"""

OLD_PRAGMA = """#if defined(MIFM1_RAM_CODE) && defined(__clang__)  // MIFM1: the unit's code in RAM (fm1-base's .c1_text)
#pragma clang section text=".c1_text"
#endif
"""


def main():
    root = Path(sys.argv[1])
    h = root / "dsp" / "modulator.h"
    s = h.read_text()
    if MARK not in s:
        if s.count(ANCHOR) != 1:
            raise SystemExit("warps_fast: ProcessXmod's start not found")
        s = s.replace(ANCHOR, ANCHOR.split("\n")[0] + "\n" + FAST + ANCHOR.split("\n", 1)[1])
        h.write_text(s)
    cc = root / "dsp" / "modulator.cc"
    c = cc.read_text()
    if OLD_PRAGMA in c:                  # (0.3.5's way, which the FM-1's clang ignored)
        cc.write_text(c.replace(OLD_PRAGMA, ""))
    print("warps_fast: done")


if __name__ == "__main__":
    main()
