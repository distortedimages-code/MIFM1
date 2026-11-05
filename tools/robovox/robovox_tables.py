#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""ROBOVOX's tables (0.16.17): its 64 phonemes in the SC-01's own terms, and its words, into firmware/mi/robovox_tables.h.

The phonemes are the SC-02's (the SSI-263's: their names and order from its data sheet), their sound MIFM1's own: the
chip's ROM is Votrax's, not ours to give. Each is written here as what a phonetician measures (its first three
formants in Hz, a male voice: the vowels after Peterson and Barney, 1952, and Hillenbrand et al., 1995; the
consonants' loci and noises after Klatt, 1980), its voicing, its noise and where the noise goes, a closure for the
stops, its length; then put in the SC-01's codes: the code of F1, F2 and F3 whose filter (the die's capacitors, as
MAME's votrax.cpp has them, Olivier Galibert's) peaks nearest the formant, F2's width the code nearest its bandwidth.

A word: its syllables (| between them), each its phonemes by name; the first vowel of a syllable is its nucleus, held
while the note is, what follows it sung at the release. A word's line: its name on the screen (8 letters at most),
then its phonemes.

  tools/robovox/robovox_tables.py firmware/mi/robovox_tables.h"""
import math
import sys
from pathlib import Path

CCLOCK = 20000.0                       # the switched capacitors' clock (720 kHz / 36)


def caps(v, cs):
    return sum(c for i, c in enumerate(cs) if v >> i & 1)


def std_peak(c1t, c1b, c2t, c2b, c3, c4):
    """the standard filter's H(s) = (1 + k0 s) / (1 + k1 s + k2 s^2): its peak (Hz) and its bandwidth (Hz)"""
    k0 = c1t / (CCLOCK * c1b)
    k1 = c4 * c2t / (CCLOCK * c1b * c3)
    k2 = c4 * c2b / (CCLOCK * CCLOCK * c1b * c3)
    best, fb = 0.0, 0.0
    f = 40.0
    while f < 9000.0:
        w = 2 * math.pi * f
        h = abs(complex(1, k0 * w)) / abs(complex(1 - k2 * w * w, k1 * w))
        if h > best:
            best, fb = h, f
        f *= 1.002
    fn = 1 / (2 * math.pi * math.sqrt(k2))
    return fb, fn * k1 / math.sqrt(k2)          # (the bandwidth: fn / Q, Q = sqrt(k2) / k1)


def f1_hz(c):
    return std_peak(11247, 11797, 949, 52067, 2280 + caps(c, [2546, 4973, 9861, 19724]), 166272)[0]


def f2_hz(c, q):
    return std_peak(24840, 29154, 829 + caps(q, [1390, 2965, 5875, 11297]), 38180,
                    2352 + caps(c, [833, 1663, 3164, 6327, 12654]), 34270)


def f3_hz(c):
    return std_peak(0, 17594, 868, 18828, 8480 + caps(c, [2226, 4485, 9056, 18111]), 50019)[0]


def nearest(target, n, fn):
    return min(range(n), key=lambda c: abs(math.log(fn(c) / target)))


# ---- the 64 phonemes: name, an example (the data sheet's), class, F1 F2 F3 (Hz), F2's bandwidth (Hz), voice 0..15,
# noise 0..15, the noise through F2 (15: all, P SH K; 0: none, S T: into F3's output), closure, length (ms),
# the noise's and the closure's delay, the voice's delay (in sixteenths of the length)
V, G, N, F, S, H, P = 'vowel', 'glide', 'nasal', 'fricative', 'stop', 'aspirate', 'pause'
PH = [
    ("PA", "pause", P, 500, 1500, 2500, 300, 0, 0, 0, 0, 100, 0, 0),
    ("E", "mEEt", V, 270, 2290, 2900, 150, 15, 0, 0, 0, 150, 1, 2),
    ("E1", "bEnt", V, 420, 1950, 2550, 150, 15, 0, 0, 0, 110, 1, 2),
    ("Y", "bEfore", G, 290, 2200, 2850, 200, 13, 0, 0, 0, 60, 1, 1),
    ("YI", "Year", G, 250, 2250, 2900, 200, 13, 0, 0, 0, 70, 1, 1),
    ("AY", "plEAse", V, 280, 2270, 2900, 150, 15, 0, 0, 0, 170, 1, 2),
    ("IE", "anY", V, 300, 2150, 2750, 150, 15, 0, 0, 0, 110, 1, 2),
    ("I", "sIx", V, 390, 1990, 2550, 150, 15, 0, 0, 0, 100, 1, 2),
    ("A", "mAde", V, 480, 2050, 2600, 150, 15, 0, 0, 0, 160, 1, 2),
    ("AI", "cAre", V, 560, 1850, 2500, 150, 15, 0, 0, 0, 160, 1, 2),
    ("EH", "nEst", V, 530, 1840, 2480, 150, 15, 0, 0, 0, 120, 1, 2),
    ("EH1", "bElt", V, 560, 1700, 2450, 150, 15, 0, 0, 0, 120, 1, 2),
    ("AE", "dAd", V, 660, 1720, 2410, 150, 15, 0, 0, 0, 160, 1, 2),
    ("AE1", "After", V, 690, 1650, 2400, 150, 15, 0, 0, 0, 160, 1, 2),
    ("AH", "gOt", V, 720, 1100, 2440, 150, 15, 0, 0, 0, 130, 1, 2),
    ("AH1", "fAther", V, 730, 1090, 2440, 150, 15, 0, 0, 0, 180, 1, 2),
    ("AW", "Office", V, 580, 850, 2410, 150, 15, 0, 0, 0, 160, 1, 2),
    ("O", "stOre", V, 500, 820, 2400, 150, 15, 0, 0, 0, 160, 1, 2),
    ("OU", "bOAt", V, 470, 900, 2380, 150, 15, 0, 0, 0, 170, 1, 2),
    ("OO", "lOOk", V, 440, 1020, 2240, 150, 15, 0, 0, 0, 110, 1, 2),
    ("IU", "yOU", V, 300, 1350, 2250, 150, 15, 0, 0, 0, 160, 1, 2),
    ("IU1", "cOUld", V, 440, 1050, 2250, 150, 15, 0, 0, 0, 110, 1, 2),
    ("U", "tUne", V, 300, 1150, 2250, 150, 15, 0, 0, 0, 160, 1, 2),
    ("U1", "cartOOn", V, 300, 870, 2240, 150, 15, 0, 0, 0, 170, 1, 2),
    ("UH", "wOnder", V, 520, 1190, 2390, 150, 15, 0, 0, 0, 110, 1, 2),
    ("UH1", "lOve", V, 560, 1150, 2400, 150, 15, 0, 0, 0, 120, 1, 2),
    ("UH2", "whAt", V, 600, 1170, 2400, 150, 15, 0, 0, 0, 120, 1, 2),
    ("UH3", "nUt", V, 620, 1200, 2390, 150, 15, 0, 0, 0, 110, 1, 2),
    ("ER", "bIRd", V, 490, 1350, 1690, 150, 15, 0, 0, 0, 160, 1, 2),
    ("R", "Roof", G, 310, 1060, 1380, 200, 13, 0, 0, 0, 70, 1, 1),
    ("R1", "Rug", G, 340, 1100, 1450, 200, 13, 0, 0, 0, 70, 1, 1),
    ("R2", "mutteR", V, 560, 1250, 2300, 180, 14, 0, 0, 0, 110, 1, 2),
    ("L", "Lift", G, 360, 1300, 2700, 220, 13, 0, 0, 0, 70, 1, 1),
    ("L1", "pLay", G, 380, 1250, 2650, 220, 12, 0, 0, 0, 60, 1, 1),
    ("LF", "falL", G, 450, 850, 2500, 220, 13, 0, 0, 0, 90, 1, 1),
    ("W", "Water", G, 290, 650, 2150, 200, 13, 0, 0, 0, 70, 1, 1),
    ("B", "Bag", S, 250, 900, 2300, 400, 6, 6, 15, 1, 60, 0, 6),
    ("D", "paiD", S, 250, 1700, 2600, 400, 6, 7, 6, 1, 60, 0, 6),
    ("KV", "taG", S, 250, 1900, 2500, 400, 6, 7, 15, 1, 60, 0, 6),
    ("P", "Pen", S, 400, 900, 2300, 500, 0, 11, 15, 1, 70, 0, 15),
    ("T", "Tart", S, 400, 1700, 2600, 500, 0, 13, 2, 1, 70, 0, 15),
    ("K", "Kit", S, 400, 1900, 2500, 500, 0, 12, 15, 1, 70, 0, 15),
    ("HV", "hold, voiced", H, 500, 1500, 2500, 300, 10, 4, 15, 0, 60, 0, 0),
    ("HVC", "hold, closing", H, 500, 1500, 2500, 300, 8, 4, 15, 1, 50, 6, 0),
    ("HF", "Heart", H, 500, 1500, 2500, 400, 0, 9, 15, 0, 60, 0, 15),
    ("HFC", "Heart, closing", H, 500, 1500, 2500, 400, 0, 9, 15, 1, 50, 6, 15),
    ("HN", "hold, nasal", N, 250, 1100, 2400, 500, 10, 0, 0, 0, 60, 0, 0),
    ("Z", "Zero", F, 250, 1700, 2600, 400, 8, 11, 0, 0, 90, 0, 0),
    ("S", "Same", F, 400, 1700, 2600, 400, 0, 14, 0, 0, 110, 0, 15),
    ("J", "meaSure", F, 250, 1900, 2400, 600, 8, 11, 15, 0, 90, 0, 0),
    ("SCH", "SHip", F, 400, 1900, 2400, 600, 0, 13, 15, 0, 120, 0, 15),
    ("V", "Very", F, 250, 1100, 2300, 500, 9, 6, 4, 0, 70, 0, 0),
    ("F", "Four", F, 400, 1100, 2300, 500, 0, 7, 4, 0, 100, 0, 15),
    ("THV", "THere", F, 250, 1500, 2500, 500, 9, 5, 2, 0, 60, 0, 0),
    ("TH", "wiTH", F, 400, 1500, 2500, 500, 0, 6, 2, 0, 90, 0, 15),
    ("M", "More", N, 250, 1000, 2300, 500, 11, 0, 0, 0, 80, 0, 0),
    ("N", "NiNe", N, 250, 1500, 2500, 500, 11, 0, 0, 0, 80, 0, 0),
    ("NG", "raNG", N, 250, 1900, 2600, 500, 11, 0, 0, 0, 90, 0, 0),
    (":A", "mÄrchen", V, 600, 1800, 2500, 150, 15, 0, 0, 0, 160, 1, 2),
    (":OH", "lÖwe", V, 380, 1500, 2200, 150, 15, 0, 0, 0, 160, 1, 2),
    (":U", "fÜnf", V, 330, 1600, 2150, 150, 15, 0, 0, 0, 120, 1, 2),
    (":UH", "menU", V, 270, 1750, 2100, 150, 15, 0, 0, 0, 160, 1, 2),
    ("E2", "bittE", V, 500, 1500, 2500, 150, 15, 0, 0, 0, 90, 1, 2),
    ("LB", "Lube", G, 330, 900, 2400, 220, 13, 0, 0, 0, 70, 1, 1),
]
assert len(PH) == 64
NAMES = [p[0] for p in PH]
VOWELS = {p[0] for p in PH if p[2] == V}


def code(p):
    name, ex, cls, f1, f2, f3, bw, va, fa, fc, cl, ms, vd, cld = p
    c1 = nearest(f1, 16, f1_hz)
    q = min(range(16), key=lambda q: abs(math.log(f2_hz(16, q)[1] / bw)))
    c2 = nearest(f2, 16, lambda c: f2_hz(2 * c, q)[0])   # (F2's ROM value is 4 bits: its filter's code twice it, the
    #                                                       fifth bit the interpolation's, between two)
    c3 = nearest(f3, 16, f3_hz)
    dur = max(0, min(127, round((ms / 0.8 - 1) / 4)))          # (4 dur + 1) x 0.8 ms
    return c1, c2, q, c3, va, fa, fc, cl, dur, vd, cld, 1 if cls == P else 0


def words():
    out = []
    for line in (Path(__file__).resolve().parent / "words.txt").read_text().splitlines():
        line = line.split('#')[0].strip()
        if not line:
            continue
        name, ph = line.split(':', 1)
        name = name.strip()
        assert 0 < len(name) <= 8, name
        sy = []
        for s in ph.split('|'):
            ps = s.split()
            assert ps, (name, s)
            for x in ps:
                assert x in NAMES, (name, x)
            nuc = next((i for i, x in enumerate(ps) if x in VOWELS), len(ps) - 1)
            sy.append((ps, nuc))
        out.append((name, sy))
    return out


def main(dst):
    rows = [code(p) for p in PH]
    ws = words()
    o = ["// generated by tools/robovox/robovox_tables.py: ROBOVOX's phonemes (the SC-02's names, MIFM1's sounds in the",
         "// SC-01's codes) and its words. Do not edit.", "#pragma once", "#include <stdint.h>", "",
         "// a phoneme: F1, F2 (its filter's code twice it), F2's width, F3, voice, noise, the noise through F2 (4 bits each),",
         "// a closure, a length ((4 x it + 1) x 0.8 ms), the noise's and the closure's delay, the voice's (sixteenths), a",
         "// pause",
         "struct VoxPhon {", "  uint8_t f1, f2, f2q, f3, va, fa, fc, cl, dur, vd, cld, pause;", "};",
         f"const VoxPhon kVoxPhon[64] = {{"]
    for p, r in zip(PH, rows):
        o.append("    {%2d, %2d, %2d, %2d, %2d, %2d, %2d, %d, %3d, %2d, %2d, %d},   // %-4s %s: %d %d %d Hz -> %.0f %.0f %.0f" % (
            *r, p[0], p[1], p[3], p[4], p[5], f1_hz(r[0]), f2_hz(2 * r[1], r[2])[0], f3_hz(r[3])))
    o.append("};")
    o.append("const char* const kVoxPhonName[64] = {" + ", ".join('"%s"' % n for n in NAMES) + "};")
    o.append("const uint8_t kVoxVowel[8] = {" + ", ".join(
        "0x%02X" % sum(1 << b for b in range(8) if NAMES[i * 8 + b] in VOWELS) for i in range(8)) + "};   // (a bit a phoneme)")
    o.append("")
    o.append("// the words: each its phonemes, a byte each (0x80: a syllable starts, 0x40: its nucleus, held), from its offset")
    data, offs = [], []
    for name, sy in ws:
        offs.append(len(data))
        for ps, nuc in sy:
            for i, x in enumerate(ps):
                data.append(NAMES.index(x) | (0x80 if i == 0 else 0) | (0x40 if i == nuc else 0))
    offs.append(len(data))
    o.append(f"const int kVoxWords = {len(ws)};")
    o.append("const char* const kVoxWordName[%d] = {" % len(ws) + ", ".join('"%s"' % n for n, _ in ws) + "};")
    o.append("const uint16_t kVoxWordAt[%d] = {" % (len(ws) + 1) + ", ".join(str(x) for x in offs) + "};")
    o.append("const uint8_t kVoxWordPh[%d] = {" % len(data))
    for i in range(0, len(data), 24):
        o.append("    " + ", ".join("0x%02X" % x for x in data[i:i + 24]) + ",")
    o.append("};")
    Path(dst).write_text("\n".join(o) + "\n")
    print(f"{dst}: 64 phonemes, {len(ws)} words, {len(data)} bytes of them")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "firmware/mi/robovox_tables.h")
