#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""MIFM1: copies the LXR-02's drum voices (Erica Synths' LXR firmware, GPL-3.0-only with the terms of its NOTICE.md)
into firmware/third_party/lxr/ and makes the changes the FM-1 needs, from a checkout of
https://github.com/erica-synths/lxr-desktop-app:

    tools/vendor_lxr.py /path/to/lxr-desktop-app

Pinned: a43cd4fcda23ec3d577dc425261c3eb28bd5abff (23 October 2026). The changes are listed in
firmware/third_party/lxr/MIFM1.md; each changed file says so under its licence header. Run again, it writes the same
files, then runs tools/split_tables.py (the tables in the FM-1's resource region). Not touched: lxr_port.h and
lxr_port.cc (MIFM1's own, the layer the copies run on) and MIFM1.md."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DST = ROOT / "firmware" / "third_party" / "lxr"

DSP = ["Oscillator", "wavetable", "transientTables", "transientGenerator", "DrumVoice", "Snare", "CymbalVoice",
       "HiHat", "ResonantFilter", "distortion", "SlopeEg2", "Decay", "snapEg", "1PoleLp"]
DSP_H = DSP + ["BufferTools", "dither", "random", "lfo", "modulationNode", "squareRootLut"]
TOP_H = ["config", "datatypes", "globals"]
MIDI_H = ["valueShaper", "MidiNoteNumbers"]

MARK = " *\n * Modified for MIFM1 (2026): ported to the FM-1, see MIFM1.md in this directory.\n"

# the headers of the LXR firmware the copies do not bring: what they took from them is in lxr_port.h
PORTED_INCLUDES = ["stm32f7xx_hal.h", "stm32f7xx.h", "Soundengine/TriggerOut.h", "Soundengine/MIDI/MidiVoiceControl.h",
                   "Soundengine/FxBus.h", "IO/adcPots.h", "Soundengine/MIDI/ParameterArray.h",
                   "Soundengine/MIDI/MidiParser.h", "Soundengine/Sequencer/sequencer.h",
                   "Soundengine/DSPAudio/Samples.h"]


def sub(text, pattern, repl, count=None, flags=0, name=""):
    """re.sub that insists on finding what it changes (count: exactly that many times)"""
    new, n = re.subn(pattern, repl, text, flags=flags)
    if n == 0 or (count is not None and n != count):
        raise SystemExit(f"vendor_lxr: {name}: {pattern!r} matched {n} times (expected {count or 'some'})")
    return new


def numbers(body):
    """the integers of a C initializer (decimal or hex), its comments left out"""
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    return [int(v, 0) for v in re.findall(r"-?(?:0[xX][0-9A-Fa-f]+|\d+)", body)]


def between(text, start, end):
    i = text.index(start) + len(start)
    return text[i:text.index(end, i)]


def c_array(ctype, name, values, per=16):
    rows = [", ".join(str(v) for v in values[i:i + per]) for i in range(0, len(values), per)]
    return f"const {ctype} {name}[] = {{\n" + ",\n".join(rows) + "\n};\n"


def common(text, name):
    for inc in PORTED_INCLUDES:
        text = text.replace(f'#include "{inc}"', '#include "lxr_port.h"')
    text = re.sub(r'#include "config.h"', '#include "Soundengine/config.h"', text)
    text = re.sub(r"\bdouble\b", "LXR_DOUBLE", text)               # the FM-1's FPU: single precision
    text = re.sub(r"\bfmaf\(", "LXR_FMAF(", text)
    return text


def mark(text, name):
    """the modification notice, at the end of the licence header (the file's first comment)"""
    end = text.index("*/")
    return text[:end].rstrip() + "\n" + MARK + " " + text[end:]


def instances(text, struct_var, prefix, name):
    """one voice a MIFM1 track (four of each): the global made an array, each function given its index v"""
    text = re.sub(r"\b" + struct_var + r"\.", struct_var + "[v].", text)
    text = sub(text, r"(extern|INDTCMZ) (\w+) " + struct_var + ";", r"\1 \2 " + struct_var + "[LXR_VOICES];",
               count=1, name=name)
    text = re.sub(r"void " + prefix + r"_(\w+)\(\s*(const uint8_t|uint8_t|int16_t\*)",
                  r"void " + prefix + r"_\1(const uint8_t v, \2", text)
    text = re.sub(r"void " + prefix + r"_(\w+)\(\s*\)", r"void " + prefix + r"_\1(const uint8_t v)", text)
    text = re.sub(r"\b" + prefix + r"_setPan\(0\)", prefix + "_setPan(v,0)", text)
    text = re.sub(r"\b" + prefix + r"_updatePan\(\);", prefix + "_updatePan(v);", text)
    return text


# the envelope on the FM-1 (see lxr_port.h and lxr_port.cc)
EG_STRUCT = """/* MIFM1: the FM-1 has no double precision. attack and decay are floats (the LXR-02's doubles hold float values),
 * the slopes pairs of floats (a double's value to 48 bits), value the LXR-02's double exactly: an integer in units
 * of 2^-40 (its sums of the floats above are exact in a double: here too). lxr_port.cc computes the curve from it
 * in pairs of floats */
typedef struct SLOPE_EG2_Struct
{
\tfloat \tattack;
\tfloat \tdecay;
\tfloat \tslope[2];
\tfloat \tinvSlope[2];
\tuint8_t repeat; \t\t\t\t/**<number of repetitions of the attack phase*/
\tuint8_t repeatCnt;\t\t\t\t/**<a counter for the already played repeats*/
\tint64_t value;
\tuint8_t state;

} SlopeEg2;
"""

QADD_PAIRS = (r"int32_t\* x = \(int32_t\*\)(\w+);\s*int32_t\* y = \(int32_t\*\)(\w+);\s*(?://[^\n]*\n\s*)?"
              r"for\(uint8_t i=0;i<size/2;i\+\+\)\s*\{\s*x\[i\] = \(?__QADD16\(x\[i\],y\[i\]\)\)?;\s*\}")


def patch(name, text):
    if name == "config.h":
        text = sub(text, r"\n#define true 1\n#define false 0\n", "\n", count=1, name=name)
        text = sub(text, r"#define REAL_FS\t*\t\t\t\t\(float\)44140\.f", "#define REAL_FS\t\t\t\t(float)44100.f", count=1,
                   name=name)
        text = sub(text, r"#define NUM_DRUM_VOICES 3", "#define NUM_DRUM_VOICES LXR_VOICES", count=1, name=name)
        text = sub(text, r"#define INDTCM __attribute__ \(\(section\(\"\.dtcm\"\)\)\)", "#define INDTCM", count=1, name=name)
        text = sub(text, r"#define INDTCMZ __attribute__ \(\(section\(\"\.dtcmz\"\)\)\)", "#define INDTCMZ", count=1,
                   name=name)
        text = sub(text, r"#define IN_ITCM_RAM __attribute__\(\(section\(\"\.itcm_text\"\)\)\)", "#define IN_ITCM_RAM",
                   count=1, name=name)
    elif name == "Oscillator.h":
        text = sub(text, r"(\tuint32_t startPhase;[^\n]*\n)", r"\1\tuint32_t rng;\t\t/* MIFM1: its noise's random state */\n",
                   count=1, name=name)
    elif name == "Oscillator.cc":
        text = sub(text, r"fast_log2\(\(uint32_t\)\(f/440\.f\)\)", "fast_log2(lxr_f2u(f/440.f))", count=1, name=name)
        text = sub(text, r"return (\(\(\(\w+\*f\)/REAL_FS\)\)\*\d+) ;", r"return lxr_f2u(\1);", count=3, name=name)
        text = sub(text, r"\(\(\(int64_t\)\(([^()]*)\) \* frac\)\s*>>(20|22)\)", r"lxr_mul_shr\2(\1, frac)", name=name)
        text = sub(text, r"\(uint32_t\)\((\w+(?:\[i\]|->output)\*osc->fmMod)\)", r"lxr_f2u(\1)", name=name)
        text = sub(text, r"= (index|osc->phase) \+ pwm \* \(UINT32_MAX/2\);", r"= lxr_f2u(\1 + pwm * (UINT32_MAX/2));",
                   count=2, name=name)
        text = sub(text, r"GetRngValue\(\)", "GetRngValue(&osc->rng)", name=name)
        text = sub(text, r"(int16_t calcNoise\(OscInfo\* osc\)\n\{.*?\n\}\n)",
                   r"#if 0  /* MIFM1: not used (it reads the Cortex-M's status register) */\n\1#endif\n", count=1,
                   flags=re.S, name=name)
    elif name == "wavetable.h":
        text = sub(text, r"extern const int16_t recTable\[11\]\[1024\];\nextern const int16_t triTable\[11\]\[1024\];\n"
                   r"extern const int16_t sawTable\[11\]\[1024\];\n",
                   "/* MIFM1: the three tables (11 rows of 1024 each) in one array, in the order wavetable.c defines them, as\n"
                   " * the FM-1's resource region keeps them (tools/split_tables.py): the interpolation at a row's last sample\n"
                   " * reads one past it, the next row's first (past a table's last row, the next table's first; past the\n"
                   " * last one, a copy of its last row's first sample) */\n"
                   "extern const int16_t lxr_wave_tables[3 * 11 * 1024 + 1];\n"
                   "#define sawTable ((const int16_t (*)[1024])(lxr_wave_tables))\n"
                   "#define recTable ((const int16_t (*)[1024])(lxr_wave_tables + 11 * 1024))\n"
                   "#define triTable ((const int16_t (*)[1024])(lxr_wave_tables + 22 * 1024))\n", count=1, name=name)
    elif name == "wavetable.cc":
        # the tables written as the region's tool reads them (const T name[] = {...}, decimal): the sine as it is,
        # saw, rec and tri one after the other, then the tri's last row's first sample
        sine = numbers(between(text, "sine_table[TABLESIZE+1]=", "};"))
        saw = numbers(between(text, "const int16_t sawTable[11][1024] =", "};"))
        rec = numbers(between(text, "const int16_t recTable[11][1024] =", "};"))
        tri = numbers(between(text, "const int16_t triTable[11][1024] =", "};"))
        assert len(sine) == 4097 and len(saw) == len(rec) == len(tri) == 11 * 1024, "wavetable.c's tables"
        head = text[:text.index("//Saw Table")]
        text = (head + "/* MIFM1: wavetable.c's tables, the same numbers, written for tools/split_tables.py (on the FM-1 they\n"
                " * are in the resource region): the sine (4097), then the saw's, the rectangle's and the triangle's 11 rows\n"
                " * of 1024 (128 wavetables a 256 samples, sample rate 44000 Hz, at their making), then end (wavetable.h) */\n"
                + c_array("int16_t", "sine_table", sine) + "\n"
                + c_array("int16_t", "lxr_wave_tables", saw + rec + tri + [tri[10 * 1024]]))
    elif name == "transientTables.h":
        text = sub(text, r"extern const int8_t transientData\[NUM_TRANSIENTS\]\[TRANSIENT_SAMPLE_LENGTH\];",
                   "/* MIFM1: one array, as the FM-1's resource region keeps it (tools/split_tables.py) */\n"
                   "extern const int8_t lxr_transient_data[NUM_TRANSIENTS * TRANSIENT_SAMPLE_LENGTH];\n"
                   "#define transientData ((const int8_t (*)[TRANSIENT_SAMPLE_LENGTH])lxr_transient_data)", count=1,
                   name=name)
    elif name == "transientTables.cc":
        # int8_t samples written 0x00..0xFF: C makes 0x80..0xFF negative (as the LXR-02's compiler does)
        s0 = text.index("INDTCM const int8_t transientData[NUM_TRANSIENTS][TRANSIENT_SAMPLE_LENGTH] =")
        e0 = text.index("};", s0) + 2
        body = text[s0:e0]
        names = [c.strip() for c in re.findall(r"//([^\n]*)", body)]
        rows = [numbers(r) for r in body.split("=", 1)[1].split("}")[:12]]
        assert len(names) == 12 and all(0 < len(r) <= 2205 for r in rows), "transientTables.c's samples"
        # (a row given short is zeros to its end, as C fills it: the last one is 2192 long)
        data = [v - 256 if v > 127 else v for r in rows for v in r + [0] * (2205 - len(r))]
        text = (text[:s0] + "/* MIFM1: the samples, the same numbers, written for tools/split_tables.py (on the FM-1 they are in\n"
                " * the resource region): 12 of 2205, signed (0x80..0xFF read as C reads them, negative):\n"
                + "".join(f" *   {i}: {n}\n" for i, n in enumerate(names)) + " */\n"
                + c_array("int8_t", "lxr_transient_data", data) + text[e0:].lstrip("\n").join(["\n", ""]))
    elif name == "random.h":
        text = sub(text, r"extern RNG_HandleTypeDef hrng;\n\nvoid initRng\(\);\n\nuint32_t GetRngValue\(\);",
                   "/* MIFM1: an oscillator's own xorshift instead of the STM32's random number generator */\n"
                   "static inline uint32_t GetRngValue(uint32_t* state)\n{\n\tuint32_t x = *state ? *state : 0x9E3779B9u;\n"
                   "\tx ^= x << 13;\n\tx ^= x >> 17;\n\tx ^= x << 5;\n\t*state = x;\n\treturn x;\n}", count=1, name=name)
    elif name == "DrumVoice.h":
        text = sub(text, r"(\tfloat \t\tvolEgValueBlock\[OUTPUT_DMA_SIZE\];\n)",
                   r"#ifdef USE_AMP_FILTER  /* MIFM1: only with it */\n\1#endif\n", count=1, name=name)
    elif name in ("Snare.h", "Snare.cc"):
        text = instances(text, "snareVoice", "Snare", name)
    elif name in ("CymbalVoice.h", "CymbalVoice.cc"):
        text = instances(text, "cymbalVoice", "Cymbal", name)
    elif name in ("HiHat.h", "HiHat.cc"):
        text = instances(text, "hatVoice", "HiHat", name)
    elif name == "ResonantFilter.h":
        text = sub(text, r"void SVF_init\(\);", "void SVF_init(ResonantFilter* filter);", count=1, name=name)
    elif name == "ResonantFilter.cc":
        text = sub(text, r"fastTan\(M_PI \* filter->f \)", "fastTan((LXR_DOUBLE)M_PI * filter->f )", name=name)
        text = sub(text, r"tanhXdX\(0\.5\*in\)", "tanhXdX(LXR_D(0.5)*in)", count=1, name=name)
        text = sub(text, r"\*1\.4 \+ \(1-filter->q\) / \(1\.0 - f_lp2\)", "*LXR_D(1.4) + (1-filter->q) / (LXR_D(1.0) - f_lp2)",
                   name=name)
        text = sub(text, r"\(1-filter->q\), 1\.4f, \(1-filter->q\) / \(1\.0 - f_lp2\)",
                   "(1-filter->q), 1.4f, (1-filter->q) / (LXR_D(1.0) - f_lp2)", name=name)
        text = sub(text, r"\btmp = ([^;\n]*);", r"tmp = lxr_f2i(\1);", name=name)
        text = sub(text, r"#pragma GCC push_options\n#pragma GCC optimize\(\"O0\"\)\n", "", count=1, name=name)
        text = sub(text, r"#pragma GCC pop_options\n", "", count=1, name=name)
    elif name == "SlopeEg2.h":
        text = sub(text, r"(typedef struct SLOPE_EG2_Struct\n\{.*?\} SlopeEg2;\n)",
                   lambda m: "#ifdef LXR_HARD_DOUBLE\n" + m.group(1) + "#else\n" + EG_STRUCT + "#endif\n", count=1,
                   flags=re.S, name=name)
    elif name == "SlopeEg2.cc":
        text = sub(text, r"(#include <math.h>\n)", r"\1\n#ifdef LXR_HARD_DOUBLE  /* MIFM1: on the FM-1, lxr_port.cc's (the same "
                   r"envelope, its double kept exactly) */\n", count=1, name=name)
        text = text.rstrip("\n") + "\n\n#endif  /* LXR_HARD_DOUBLE */\n"
        text = sub(text, r"register LXR_DOUBLE val", "LXR_DOUBLE val", count=1, name=name)
        text = sub(text, r"\(1\.0\+(slope|amount)\)\*val/\(1\.0\+(slope|amount)\*fabs\(val\)\)",
                   r"(LXR_D(1.0)+\1)*val/(LXR_D(1.0)+\2*fabs(val))", name=name)
        text = sub(text, r"\(1\.0-\(\(LXR_D", "(LXR_D(1.0)-((LXR_D", name=name)
        text = sub(text, r"if\(val >= 1\.0\)", "if(val >= LXR_D(1.0))", count=1, name=name)
        text = sub(text, r"\(data2\)/127\.0;", "(data2)/LXR_D(127.0);", count=1, name=name)
    elif name == "distortion.cc":
        text = sub(text, r"shape -= 0\.0078125;", "shape -= LXR_D(0.0078125);", count=1, name=name)
        text = sub(text, r"IN_ITCM_RAM __inline void setDistortionShape", "IN_ITCM_RAM void setDistortionShape", count=1,
                   name=name)
    elif name == "1PoleLp.h":
        text = sub(text, r"-2\.f\*M_PI\*f", "-2.f*(LXR_DOUBLE)M_PI*f", count=1, name=name)
    elif name == "1PoleLp.cc":
        text = sub(text, r"if\(diff >0\.5 \|\| \(input < 0\.01\)\)", "if(diff >LXR_D(0.5) || (input < LXR_D(0.01)))",
                   count=1, name=name)
    elif name == "BufferTools.h":
        text = sub(text, QADD_PAIRS, r"lxr_qadd16_block(\1, \2, size);", count=1, name=name)
        text = sub(text, r"i/\(size-1\.0\)", "i/(size-LXR_D(1.0))", count=2, name=name)
        text = sub(text, r"if\(in>1\.0\)in=1\.0;\n\t\telse if\( in < -1\.0\)in=-1\.0;",
                   "if(in>LXR_D(1.0))in=1.0;\n\t\telse if( in < LXR_D(-1.0))in=-1.0;", count=1, name=name)
    if name in ("CymbalVoice.cc", "HiHat.cc"):
        text = sub(text, QADD_PAIRS, r"lxr_qadd16_block(\1, \2, size);", count=2, name=name)
    return text


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: tools/vendor_lxr.py LXR_DESKTOP_APP_CHECKOUT")
    src = Path(sys.argv[1])
    core = src / "Core"
    if not (core / "Src/Soundengine/DSPAudio/DrumVoice.c").exists():
        raise SystemExit(f"vendor_lxr: {src}: not a checkout of lxr-desktop-app")
    keep = {p.name: p.read_bytes() for p in DST.glob("lxr_port.*")} if DST.exists() else {}
    keep.update({p.name: p.read_bytes() for p in DST.glob("MIFM1.md")} if DST.exists() else {})
    shutil.rmtree(DST, ignore_errors=True)
    (DST / "Soundengine" / "DSPAudio").mkdir(parents=True)
    (DST / "Soundengine" / "MIDI").mkdir(parents=True)
    for n, b in keep.items():
        (DST / n).write_bytes(b)
    shutil.copy(src / "LICENSE", DST / "LICENSE")
    shutil.copy(src / "NOTICE.md", DST / "NOTICE.md")
    files = [(core / "Src/Soundengine/DSPAudio" / f"{n}.c", f"Soundengine/DSPAudio/{n}.cc") for n in DSP]
    files += [(core / "Inc/Soundengine/DSPAudio" / f"{n}.h", f"Soundengine/DSPAudio/{n}.h") for n in DSP_H]
    files += [(core / "Inc/Soundengine" / f"{n}.h", f"Soundengine/{n}.h") for n in TOP_H]
    files += [(core / "Inc/Soundengine/MIDI" / f"{n}.h", f"Soundengine/MIDI/{n}.h") for n in MIDI_H]
    for s, d in files:
        text = s.read_text(encoding="utf-8", errors="surrogateescape").replace("\r\n", "\n")
        name = Path(d).name
        new = patch(name, common(text, name))
        if new != text:
            new = mark(new, name)
        (DST / d).write_text(new, encoding="utf-8", errors="surrogateescape")
    print(f"vendored {len(files)} files into {DST}")
    # the tables into the FM-1's resource region (their arrays wrapped for the computer's builds alone)
    subprocess.run([sys.executable, str(ROOT / "tools" / "split_tables.py")], check=True)


if __name__ == "__main__":
    main()
