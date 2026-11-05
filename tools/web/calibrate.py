#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""The browser emulator's FM-1 load: firmware/mi/web_costs.h, what each part of the engine costs the FM-1 a block.

The engine is built for this computer as the FM-1 runs it, scalar (no vectorised loops: the FM-1 has none), and
played under valgrind's callgrind (tools/web/calib.cc), which counts the instructions each part runs: every Plaits
model, FM drum and ZERO on its own, each filter (plain, with FM, with NOISE), the bits (the bending's first page), Warps' algorithms (Eco, HQ), the
effects with each master bus, the preparation (the sequencer, the LFOs, the cables). Counts, not times: they come
out the same on any computer, and the FM-1's time has kept close to them. 0.5.8's stress test, measured on the FM-1
(the machine page, its worst block: T1 20 %, T2 18, T3 20, T4 32, WP 22, FX 28, PREP 14) and counted here
(e161509, scalar), gives the FM-1's microseconds a thousand instructions:

    the voices (Plaits, the filters, the bits) 6.75   Warps 6.82   the effects and the bus 5.42   the preparation 17.2

(the preparation's code is branchy and spread out: the FM-1's cache misses it more). The table is those counts
times those rates. What it cannot know: a model's own code size (the FM-1 reads its code from flash through a
cache, a model that does not fit costs more), its settings (a CHORD's voices, a STRING's decay); the voices of the
stress test land within about 20 % of the FM-1's own figures.

  python3 tools/web/calibrate.py          (from the repository; valgrind, a C++ compiler; ~5 minutes)
"""
import os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware"
OUT = ROOT / "build" / "web_calib"
K_VOICE, K_WARPS, K_FX, K_PREP = 6.75, 6.82, 5.42, 17.2
FLAGS = ["-O2", "-ffp-contract=off", "-DTEST", "-DPLAT_HOST", "-fno-exceptions", "-fno-rtti",
         "-fno-threadsafe-statics", "-fno-tree-vectorize", "-fno-tree-slp-vectorize", "-w"]
INC = [f"-I{FW}", f"-I{FW / 'third_party'}", f"-I{FW / 'third_party' / 'lxr'}", f"-I{ROOT / 'base' / 'include'}"]
SECS = 2.0
NAMES = re.findall(r'\{"([^"]+)", "[^"]*", MK_', (FW / "mi" / "engine.cc").read_text())   # (the models, in order)
VA = NAMES.index("FM")                               # (the filters', the bus', Warps' runs: under the FM)
PAD = VA                                             # (the FM's square, PW and SYNC: three operators heard, calib.cc)
PADS = NAMES.index("PADS")                           # (PADS: its notes, one saw each or three, its bell: calib.cc)
POLY = NAMES.index("POLY")                           # (POLY: a voice held, four in unison)
PRO = NAMES.index("REV3")                            # (REV3, after the Prophet-5: a voice, four in a chord)
SQ1 = NAMES.index("OTIS")                            # (OTIS, after the SQ-1: a voice, four ringing on; its layers, SPACE)


def build():
    obj = OUT / "obj"
    obj.mkdir(parents=True, exist_ok=True)
    srcs = sorted(FW.glob("mi/*.cc")) + sorted((FW / "third_party").rglob("*.cc"))

    def one(s):
        o = obj / (str(s.relative_to(FW)).replace("/", "_") + ".o")
        if not o.exists() or o.stat().st_mtime < s.stat().st_mtime or s.name == "engine.cc":
            subprocess.run(["c++", "-std=gnu++11", *FLAGS, *INC, "-c", str(s), "-o", str(o)], check=True)
        return str(o)
    with ThreadPoolExecutor(os.cpu_count() or 2) as ex:
        objs = list(ex.map(one, srcs))
    exe = OUT / "calib"
    subprocess.run(["c++", "-std=gnu++11", *FLAGS, *INC, str(ROOT / "tools" / "web" / "calib.cc"), *objs, "-lm",
                    "-o", str(exe)], check=True)
    return exe


FUNCS = {"total": r"\beng_render\b", "tasks": r"run_task\(", "voice": r"render_voice\(", "plaits": r"\bmp_render\b",
         "lx": r"\blx_render\b", "filter": r"\bmf_process\b", "bend": r"apply_bend\(", "warps": r"\bmw_render\b"}


def count(exe, *args):
    """the instructions a block of each part (inclusive), the stress test or a part played under callgrind"""
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / "cg.out"
        r = subprocess.run(["valgrind", "--tool=callgrind", f"--callgrind-out-file={out}", "--toggle-collect=eng_render",
                            str(exe), *map(str, args)], capture_output=True, text=True)
        blocks = int(re.search(r"blocks (\d+)", r.stdout).group(1))
        nv = re.search(r"voices ([\d.]+)", r.stdout)
        ann = subprocess.run(["callgrind_annotate", "--inclusive=yes", "--threshold=100", str(out)],
                             capture_output=True, text=True).stdout
    got = {}
    for line in ann.splitlines():
        m = re.match(r"\s*([\d,]+)\s+\([\s\d.]+%\)\s+\S*?:(.*)", line)
        if not m:
            continue
        n, fn = int(m.group(1).replace(",", "")), m.group(2)
        for k, pat in FUNCS.items():
            if k not in got and re.search(pat, fn):
                got[k] = n
    res = {k: got.get(k, 0) / blocks for k in FUNCS}
    res["nv"] = float(nv.group(1)) if nv else 0.0         # (POLY's voices, on average)
    return res


def main():
    exe = build()
    jobs = {"blank": ("blank", SECS), "stress": ("stress", SECS * 2)}
    for m in range(len(NAMES)):                          # every model (engine.cc's kModelDef)
        jobs[f"model{m}"] = ("one", m, 0, 0, 0, 0, 0, -1, 0, SECS)
    for x, bit in (("sq", 8), ("pw", 16), ("sync", 32)):   # the FM's square, PW, SYNC: their extra
        jobs[f"fm{x}"] = ("one", PAD, 0, 0, 0, 0, 0, -1, 0, SECS, bit)
    for x, bit in (("triad", 512), ("six", 512 | 64), ("one", 512 | 128), ("bell", 512 | 256)):   # PADS, MANUAL:
        jobs[f"pads{x}"] = ("one", PADS, 0, 0, 0, 0, 0, -1, 0, SECS, bit)   # 3 notes, 6, one saw each, a bell
    for x, bit in (("one", 1024 | 8192), ("four", 1024 | 2048)):
        jobs[f"poly{x}"] = ("one", POLY, 0, 0, 0, 0, 0, -1, 0, SECS, bit)
    for x, bit in (("one", 1024 | 8192), ("four", 1024 | 16384)):
        jobs[f"pro{x}"] = ("one", PRO, 0, 0, 0, 0, 0, -1, 0, SECS, bit)
    for x, bit in (("one", 65536 | 131072), ("four", 65536)):
        jobs[f"sq{x}"] = ("one", SQ1, 0, 0, 0, 0, 0, -1, 0, SECS, bit)
    for f in range(1, 5):                                # MF_LADDER..MF_VOX, under the LEAD
        for v, (fm, nz) in enumerate(((0, 0), (0.5, 0), (0, 0.5))):
            jobs[f"filter{f}.{v}"] = ("one", VA, f, fm, nz, 0, 0, -1, 0, SECS)
    jobs["bend"] = ("one", VA, 0, 0, 0, 1, 0, -1, 0, SECS)
    jobs["trauma"] = ("one", VA, 0, 0, 0, 0, 0, -1, 0, SECS, 32768)   # (TRAUMA's drive on the track)
    for b in range(3):
        jobs[f"bus{b}"] = ("one", VA, 0, 0, 0, 0, b, -1, 0, SECS)
    for e in (1, 2, 4, 262144):                          # (262144: T1's SPREAD)
        jobs[f"fx{e}"] = ("one", VA, 0, 0, 0, 0, 0, -1, 0, SECS, e)
    for hq in range(2):
        for a in range(6):
            jobs[f"warps{hq}.{a}"] = ("one", VA, 0, 0, 0, 0, 0, a, hq, SECS)
    with ThreadPoolExecutor(os.cpu_count() or 2) as ex:
        res = dict(zip(jobs, ex.map(lambda a: count(exe, *a), jobs.values())))
    rest = res["blank"]["voice"] / 4.0                   # a resting voice's render (its silence), each block
    va = res[f"model{VA}"]
    k = lambda ir, rate: ir / 1000.0 * rate              # instructions a block -> the FM-1's us a block
    model = [k(res[f"model{m}"]["voice"] - 3 * rest, K_VOICE) for m in range(len(NAMES))]
    filt = [[0.0, 0.0, 0.0]] + [[k(res[f"filter{f}.{v}"]["filter"] - va["filter"], K_VOICE) for v in range(3)]
                                for f in range(1, 5)]
    bend = k(res["bend"]["voice"] - va["voice"], K_VOICE)   # (apply_bend inlined in render_voice)
    trauma = k(max(res["trauma"]["voice"] - va["voice"], 0.0), K_VOICE)   # (apply_trauma, the same)
    fx = [k(res[f"bus{b}"]["tasks"] - (res[f"bus{b}"]["voice"] - 3 * rest), K_FX) for b in range(3)]
    st = res["stress"]                                   # (Warps in the mix, and what else the stress test's effects
    fx_s = k(st["tasks"] - st["voice"] - st["warps"], K_FX)   # do that these runs do not: the rest of its figure)
    fx_w = None
    fxe = [k(res[f"fx{e}"]["tasks"] - res[f"fx{e}"]["voice"] - (res["bus0"]["tasks"] - res["bus0"]["voice"]), K_FX)
           for e in (1, 2, 4, 262144)]
    fx_w = max(fx_s - (fx[2] + 2 * fxe[0] + fxe[1] + fxe[2]), 0.0)   # (OTT, T3 and T4's EQ, the duck on 3, the delay's)
    warps = [[k(res[f"warps{hq}.{a}"]["warps"], K_WARPS) for a in range(6)] for hq in range(2)]
    fmx = [max(k(res[f"fm{x}"]["voice"] - 3 * rest, K_VOICE) - model[PAD], 0.0) for x in ("sq", "pw", "sync")]
    pv = {x: k(res[f"pads{x}"]["voice"] - 3 * rest, K_VOICE) for x in ("triad", "six", "one", "bell")}
    pv3 = max((pv["six"] - pv["triad"]) / 3.0, 0.0)      # (a note, three saws: the triad and six notes apart)
    pv1 = max(pv3 - (pv["triad"] - pv["one"]) / 3.0, 0.0)   # (one saw: two fewer a note)
    pads = [max(pv["triad"] - 3.0 * pv3, 0.0), pv1, pv3, max(pv["bell"] - pv["triad"], 0.0)]
    po = {x: k(res[f"poly{x}"]["voice"] - 3 * rest, K_VOICE) for x in ("one", "four")}
    n1, n4 = res["polyone"]["nv"], res["polyfour"]["nv"]   # (the voices sounding: a trig's and its last ones' tails;
    pvv = max((po["four"] - po["one"]) / max(n4 - n1, 0.5), 0.0)   # four in unison)
    polyc = [max(po["one"] - n1 * pvv, 0.0), pvv]
    pr = {x: k(res[f"pro{x}"]["voice"] - 3 * rest, K_VOICE) for x in ("one", "four")}
    m1, m4 = res["proone"]["nv"], res["profour"]["nv"]
    prv = max((pr["four"] - pr["one"]) / max(m4 - m1, 0.5), 0.0)
    proc = [max(pr["one"] - m1 * prv, 0.0), prv]
    sq = {x: k(res[f"sq{x}"]["voice"] - 3 * rest, K_VOICE) for x in ("one", "four")}
    q1, q4 = res["sqone"]["nv"], res["sqfour"]["nv"]
    sqv = max((sq["four"] - sq["one"]) / max(q4 - q1, 0.5), 0.0)
    sqc = [max(sq["one"] - q1 * sqv, 0.0), sqv]
    prep0 = k(res["blank"]["total"] - res["blank"]["tasks"], K_PREP)
    prep1 = k(va["total"] - va["tasks"] - 3 * rest, K_PREP)             # (a track playing: its trigs, its knobs)
    prep_s = k(res["stress"]["total"] - res["stress"]["tasks"], K_PREP)
    track = max(prep1 - prep0, 0.0)
    cable = max((prep_s - prep0 - 4 * track) / 20.0, 0.0)   # (the stress test: 20 cables, 4 tracks playing)
    s = res["stress"]
    print(f"stress: voices {k(s['voice'], K_VOICE):.0f} us, Warps {k(s['warps'], K_WARPS):.0f}, effects "
          f"{k(s['tasks'] - s['voice'] - s['warps'], K_FX):.0f}, preparation {prep_s:.0f} (the FM-1's block: 2902 us)")
    names = NAMES
    f1 = lambda v: f"{v:.1f}f"
    h = ["/* SPDX-License-Identifier: MPL-2.0",
         " * Copyright (C) 2026 Yoann */",
         "/* The browser emulator's FM-1 (PLAT_WEB): what each part of the engine costs the FM-1, us a 128-frame block",
         " * (2902 us), from the instructions it runs (tools/web/calibrate.py writes this file: see there how). */",
         "#pragma once",
         f"static const float kWebRest = {f1(k(rest, K_VOICE))};   /* a resting voice */",
         f"static const float kWebModel[{len(NAMES)}] = {{   /* a voice playing, by model */"]
    for m in range(len(NAMES)):
        h.append(f"    {f1(model[m])},{' ' * (8 - len(f1(model[m])))}/* {m:2d} {names[m]} */")
    h += ["};",
          "static const float kWebFilter[5][3] = {   /* a filter (OFF, LADDER, MS-20, SEM, VOX): plain, its FM's extra, "
          "its NOISE's */"]
    for f in range(5):
        base = filt[f][0]
        h.append(f"    {{{f1(base)}, {f1(max(filt[f][1] - base, 0.0))}, {f1(max(filt[f][2] - base, 0.0))}}},")
    h += ["};",
          "static const float kWebFm[3] = {" + ", ".join(f1(v) for v in fmx) + "};   /* the FM's extra: its square, "
          "PW, SYNC */",
          "static const float kWebPads[4] = {" + ", ".join(f1(v) for v in pads) + "};   /* PADS: its floor, a note "
          "(one saw, three), its bell */",
          "static const float kWebPoly[2] = {" + ", ".join(f1(v) for v in polyc) + "};   /* POLY: its floor, a voice */",
          "static const float kWebPro[2] = {" + ", ".join(f1(v) for v in proc) + "};   /* REV3: its floor, a voice */",
          "static const float kWebSq[2] = {" + ", ".join(f1(v) for v in sqc) + "};   /* OTIS: its floor, a voice */",
          f"static const float kWebBend = {f1(bend)};   /* the bits, one of their knobs up */",
          f"static const float kWebTrauma = {f1(trauma)};   /* TRAUMA's drive on a track */",
          "static const float kWebGlitch = 9.0f;   /* the glitch, taking or repeating its slice (estimated: a few loads a sample) */",
          "static const float kWebWarps[2][6] = {   /* Warps, its algorithm's mark (Eco, HQ) */",
          "    {" + ", ".join(f1(v) for v in warps[0]) + "},",
          "    {" + ", ".join(f1(v) for v in warps[1]) + "},",
          "};",
          "static const float kWebFx[3] = {" + ", ".join(f1(v) for v in fx) + "};   /* the effects, the mix and the master "
          "bus (OFF, COMP, OTT) */",
          f"static const float kWebFxEq = {f1(fxe[0])}, kWebFxDuck = {f1(fxe[1] / 3.0)}, kWebFxDelay = {f1(fxe[2])};   /* "
          "a track's EQ (playing), the duck on a track, the delay's filter and drive */",
          f"static const float kWebFxWarps = {f1(fx_w)};   /* Warps in the mix (the rest of the stress test's effects) */",
          f"static const float kWebFxSpread = {f1(max(fxe[3], 0.0))};   /* (0.16.19) a track's SPREAD */",
          f"static const float kWebPrep = {f1(prep0)}, kWebPrepTrack = {f1(track)}, kWebPrepCable = {f1(cable)};   /* the "
          "preparation: its floor, a track playing, a cable */",
          ""]
    (FW / "mi" / "web_costs.h").write_text("\n".join(h))
    print("\n".join(h))


if __name__ == "__main__":
    main()
