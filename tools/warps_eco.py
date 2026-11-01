#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""Warps' ECO mode (patch_mi.sh 8): its cross-modulations oversampled 3 times instead of 6, half the work.

The module oversamples its cross-modulations by 6 (at 96 kHz: 576 kHz). MIFM1's ECO runs them at 3 x 44.1 kHz
(132 kHz), through a pair of filters made by the module's own recipe (the one written above its 6x filters) for that
ratio and 44.1 kHz: remez(33, [0, 12000 / (3 x 44100), 0.5 / 3, 0.5]), flat to 12 kHz (-0.05 dB), -3.6 dB at 16 kHz,
the images (from 22.05 kHz) at -45 dB: as flat as the 6x pair MIFM1 plays (tools/warps_tables.py, -52 dB), 11 taps a
phase instead of 12 (each converter's state and coefficients stay in the FPU's registers; the module already has a
3x36 pair, for 96 kHz). What the lower ratio lets through: the algorithms' own harmonics over 66 kHz folding back
(aliasing), audible on the brightest ones (FOLD, XOR, COMPARATOR) up high.

The modulator gets a second pair of converters and set_eco(); Process takes the 3x pair when it is on (the algorithms
play the same, on half the samples). Needs numpy and scipy only to make the filters (once: then they are in the file).

  tools/warps_eco.py firmware/third_party/warps
"""
import sys
from pathlib import Path

MARK = "MIFM1: ECO"
TAPS, PASS, SR = 33, 12000.0, 44100


def filters_code():
    import scipy.signal  # (only to make them)
    h = scipy.signal.remez(TAPS, [0, PASS / (3.0 * SR), 0.5 / 3, 0.5], [1, 0], fs=1.0)
    out = []
    for kind, gain in (("SRC_UP", 3), ("SRC_DOWN", 1)):
        half = gain * h[:(TAPS + 1) // 2]   # (the first half and the middle tap: the rest mirrors them)
        vals = ["%16.9e" % v for v in half]
        lines = "\n".join("      " + ", ".join(vals[i:i + 4]) + "," for i in range(0, len(vals), 4))
        out.append("""// MIFM1: ECO, 3x at 44.1 kHz (tools/warps_eco.py), with:
// %d * scipy.signal.remez(%d, [0, %.0f / (3 * %d), 0.5 / 3, 0.5], [1, 0], fs=1.0)
template<>
struct SRC_FIR<%s, 3, %d> {
  template<int32_t i> inline float Read() const {
    const float h[] = {
%s
    };
    return h[i];
  }
};
""" % (gain, TAPS, PASS, SR, kind, TAPS, lines))
    return "\n".join(out)


def sub(text, old, new, what):
    if text.count(old) != 1:
        raise SystemExit("warps_eco: %s not found" % what)
    return text.replace(old, new)


def main():
    dsp = Path(sys.argv[1]) / "dsp"
    f = dsp / "sample_rate_conversion_filters.h"
    s = f.read_text()
    if MARK not in s:
        end = s.rindex("}  // namespace warps")
        s = s[:end] + filters_code() + "\n" + s[end:]
        f.write_text(s)
    h = dsp / "modulator.h"
    s = h.read_text()
    if MARK not in s:
        s = sub(s, "  inline void set_easter_egg(bool easter_egg) { easter_egg_ = easter_egg; }",
                "  inline void set_easter_egg(bool easter_egg) { easter_egg_ = easter_egg; }\n"
                "  inline void set_eco(bool eco) { eco_ = eco; }  // MIFM1: ECO, the cross-modulations at 3x",
                "set_easter_egg")
        s = sub(s, "  bool easter_egg_;\n", "  bool easter_egg_;\n  bool eco_;  // MIFM1: ECO\n", "easter_egg_")
        s = sub(s, "  SampleRateConverter<SRC_DOWN, kOversampling, 72> src_down_;",
                "  SampleRateConverter<SRC_UP, 3, %d> src_up_eco_[2];  // MIFM1: ECO, the 3x pair (tools/warps_eco.py)\n"
                "  SampleRateConverter<SRC_DOWN, 3, %d> src_down_eco_;\n"
                "  SampleRateConverter<SRC_DOWN, kOversampling, 72> src_down_;" % (TAPS, TAPS), "src_down_")
        h.write_text(s)
    c = dsp / "modulator.cc"
    s = c.read_text()
    if MARK not in s:
        s = sub(s, "  src_down_.Init();\n",
                "  src_down_.Init();\n"
                "  src_up_eco_[0].Init();  // MIFM1: ECO\n  src_up_eco_[1].Init();\n  src_down_eco_.Init();\n"
                "  eco_ = false;\n", "src_down_.Init()")
        s = sub(s, "    src_up_[0].Process(carrier, oversampled_carrier, size);\n"
                   "    src_up_[1].Process(modulator, oversampled_modulator, size);\n",
                "    size_t oversampling = eco_ ? 3 : kOversampling;  // MIFM1: ECO, 3x\n"
                "    if (eco_) {\n"
                "      src_up_eco_[0].Process(carrier, oversampled_carrier, size);\n"
                "      src_up_eco_[1].Process(modulator, oversampled_modulator, size);\n"
                "    } else {\n"
                "      src_up_[0].Process(carrier, oversampled_carrier, size);\n"
                "      src_up_[1].Process(modulator, oversampled_modulator, size);\n"
                "    }\n", "the up-converters")
        s = sub(s, "        size * kOversampling);\n", "        size * oversampling);\n", "the algorithms' size")
        s = sub(s, "    src_down_.Process(oversampled_output, main_output, size * kOversampling);\n",
                "    if (eco_)\n"
                "      src_down_eco_.Process(oversampled_output, main_output, size * 3);\n"
                "    else\n"
                "      src_down_.Process(oversampled_output, main_output, size * kOversampling);\n",
                "the down-converter")
        c.write_text(s)
    print("warps_eco: done")


if __name__ == "__main__":
    main()
