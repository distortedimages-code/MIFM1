#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""Warps' cross-modulations, lighter on the FM-1, the same sound (patch_mi.sh 7):

  - ProcessXmod<a1, a2> plays two algorithms and crossfades them by ALGORITHM's fraction; on a mark (the fraction 0
    over the whole block: MIFM1's ALGORITHM selector jumps from mark to mark) the second one's weight is 0, its work
    thrown away. There the loop plays the first alone: the same samples (a + (b - a) * 0 is a), half the work of the
    algorithms at 6x;
  - with MIFM1_RAM_CODE defined (the firmware's build, fm1.toml), the modulator's Process, ProcessEasterEgg and
    ProcessXmod (the algorithms inline), the input amplifiers, the 6x converters and the frequency shifter's quadrature
    filters go to section .c1_text, which fm1-base copies to RAM at boot (its RAM
    code window, where code may still call into the flash): the modulator's loops out of the XIP cache's way (the four
    voices fill it). (A section attribute: the FM-1's clang has no #pragma clang section.)

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

RAM = "MIFM1: in RAM"
MACRO = """#define WARPS_DSP_MODULATOR_H_

#ifndef MIFM1_RAM
#if defined(MIFM1_RAM_CODE)  // MIFM1: in RAM on the FM-1 (fm1-base's .c1_text: copied at boot)
#define MIFM1_RAM __attribute__((section(".c1_text")))
#else
#define MIFM1_RAM
#endif
#endif"""
GUARD = """
#ifndef MIFM1_RAM  // MIFM1: in RAM on the FM-1 (modulator.h)
#if defined(MIFM1_RAM_CODE)
#define MIFM1_RAM __attribute__((section(".c1_text")))
#else
#define MIFM1_RAM
#endif
#endif"""
# the other headers' hot loops: the 6x converters, the frequency shifter's quadrature filters
OTHERS = {"sample_rate_converter.h": ("#define WARPS_DSP_SAMPLE_RATE_CONVERTER_H_", [
              ("  inline void Process(const float* in, float* out, size_t input_size) {",
               "  MIFM1_RAM inline void Process(const float* in, float* out, size_t input_size) {", 2)]),
          "quadrature_transform.h": ("#define WARPS_DSP_QUADRATURE_TRANSFORM_H_", [
              ("  void Process(const float* in, float* i_out, float* q_out, size_t size) {",
               "  MIFM1_RAM void Process(const float* in, float* i_out, float* q_out, size_t size) {", 1)])}
TAGS = [("  void Process(\n      float drive,", "  MIFM1_RAM void Process(\n      float drive,"),
        ("  void Process(ShortFrame* input, ShortFrame* output, size_t size);",
         "  MIFM1_RAM void Process(ShortFrame* input, ShortFrame* output, size_t size);"),
        ("  void ProcessEasterEgg(ShortFrame* input, ShortFrame* output, size_t size);",
         "  MIFM1_RAM void ProcessEasterEgg(ShortFrame* input, ShortFrame* output, size_t size);"),
        ("  template<XmodAlgorithm algorithm_1, XmodAlgorithm algorithm_2>\n  void ProcessXmod(",
         "  template<XmodAlgorithm algorithm_1, XmodAlgorithm algorithm_2>\n  MIFM1_RAM void ProcessXmod(")]
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
    if RAM not in s:
        if s.count("#define WARPS_DSP_MODULATOR_H_") != 1:
            raise SystemExit("warps_fast: modulator.h's guard not found")
        s = s.replace("#define WARPS_DSP_MODULATOR_H_", MACRO)
        for old, new in TAGS:
            if s.count(old) != 1:
                raise SystemExit(f"warps_fast: {old.strip()} not found")
            s = s.replace(old, new)
        h.write_text(s)
    for name, (guard, tags) in OTHERS.items():
        f = root / "dsp" / name
        t = f.read_text()
        if "MIFM1: in RAM" in t:
            continue
        if t.count(guard) != 1:
            raise SystemExit(f"warps_fast: {name}'s guard not found")
        t = t.replace(guard, guard + GUARD)
        for old, new, n in tags:
            if t.count(old) != n:
                raise SystemExit(f"warps_fast: {name}: {old.strip()} found {t.count(old)} times, not {n}")
            t = t.replace(old, new)
        f.write_text(t)
    cc = root / "dsp" / "modulator.cc"
    c = cc.read_text()
    if OLD_PRAGMA in c:                  # (0.3.5's way, which the FM-1's clang ignored)
        cc.write_text(c.replace(OLD_PRAGMA, ""))
    print("warps_fast: done")


if __name__ == "__main__":
    main()
