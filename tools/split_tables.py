#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1: the biggest of the Mutable Instruments tables, moved out of the FM-1's program flash into fm1-base's
resource region (the data area, which the installer writes right after the firmware): with them in the image the
six modules do not fit its 581 KB.

The modules' code is not changed. On the FM-1 the moved arrays are not compiled (resources.cc: #ifdef PLAT_HOST
around them) and their symbols are defined at the fixed addresses where the region reads in place (absolute
symbols, mi/res_symbols.cc); every reference to them, static initialisers included, links to those addresses. On a
computer (PLAT_HOST) the arrays stay in the build, and the test checks the region's file against them byte for
byte.

    tools/split_tables.py        (after tools/vendor_mi.sh; tools/patch_mi.sh runs it)

Writes firmware/res/mi_tables.bin (the region's payload) and symbols.txt (each moved symbol's address), firmware/mi/res_tables.h,
res_abs.cc (the absolute symbols) and res_symbols.cc, and wraps the moved arrays in their resources.cc."""
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TP = ROOT / "firmware" / "third_party"

# the region: whole sectors from the data area's start (base/include/plat.h PLAT_DATA_LO), up to MIFM1's own store
# (firmware/app.h APP_STORE_MAP: the project at 0xD8000, the panel's calibration at 0xDC000; ZINZIN's song, another
# firmware's, at 0xDA000). Until 0.16.58 the check stopped at 0xDA000, over the project's sectors: the region never
# reached them (0xD4000), but a bigger one would have written over the saved project
RES_BASE = 0x97000
RES_END = 0xD8000
RES_HEAD = 32                     # base/device/res.c: the commit record before the payload
XIP = lambda off: 0x02000000 + off - 0x4000   # base/bsp/chip/fm1_xip.h FM1_XIP

# (the file under third_party, namespace ("": none), a regular expression of the arrays moved)
MOVED = [
    ("marbles/resources.cc", "marbles", r"dist_icdf_\d+_\d+"),   # its distributions: 73 KB
    ("plaits/resources.cc", "plaits", r"wav_integrated_waves"),   # the wavetables of three models: 50 KB
    ("tides2/resources.cc", "tides", r"lut_wavetable"),           # SHAPE's curves: 24 KB
    # the FM drums (the LXR-02's voices, tools/vendor_lxr.py writes them so): the sine, the saw, rectangle and
    # triangle wavetables (74 KB), the transients' samples (26 KB)
    ("lxr/Soundengine/DSPAudio/wavetable.cc", "", r"sine_table|lxr_wave_tables"),
    ("lxr/Soundengine/DSPAudio/transientTables.cc", "", r"lxr_transient_data"),
    # (0.16.58) Warps' wavefolder (16 KB): read as the image's tables are, through the same cache; last, so the
    # tables before it keep their places
    ("warps/resources.cc", "warps", r"lut_bipolar_fold"),
]
TYPES = {"float": ("f", 4), "int16_t": ("h", 2), "uint16_t": ("H", 2), "int8_t": ("b", 1), "uint8_t": ("B", 1),
         "int32_t": ("i", 4), "uint32_t": ("I", 4)}
MARK = "// MIFM1: on the FM-1, in the resource region (tools/split_tables.py)"


def mangle(ns, name):
    return f"_ZN{len(ns)}{ns}{len(name)}{name}E" if ns else name   # (a global variable's name is its symbol)


def qualified(ns, name):
    return f"{ns}::{name}" if ns else f"::{name}"


def main():
    payload = bytearray()
    entries = []                  # (namespace, name, type, offset, count)
    for module, ns, pattern in MOVED:
        path = TP / module
        text = path.read_text()
        if MARK in text:          # wrapped already: unwrap first, so the run is the same every time
            text = re.sub(r"#ifdef PLAT_HOST  " + re.escape(MARK) + r"\n(.*?)#endif  // MIFM1\n", r"\1", text,
                          flags=re.S)
        rx = re.compile(r"^const (\w+) (" + pattern + r")\[\] = \{\n(.*?)\n\};\n", re.M | re.S)
        found = 0

        def wrap(m):
            nonlocal found
            ctype, name, body = m.group(1), m.group(2), m.group(3)
            fmt, size = TYPES[ctype]
            values = [float(v) if fmt == "f" else int(v) for v in re.findall(r"[-+0-9.eE]+", body)]
            while len(payload) % 4:
                payload.append(0)
            entries.append((ns, name, ctype, len(payload), len(values)))
            payload.extend(struct.pack("<%d%s" % (len(values), fmt), *values))
            found += 1
            return f"#ifdef PLAT_HOST  {MARK}\n{m.group(0)}#endif  // MIFM1\n"

        text = rx.sub(wrap, text)
        if not found:
            raise SystemExit(f"split_tables: {path}: nothing matches {pattern}")
        path.write_text(text)
        print(f"{module}: {found} arrays moved")
    while len(payload) % 4:
        payload.append(0)
    sectors = (RES_HEAD + len(payload) + 4095) // 4096
    if RES_BASE + sectors * 4096 > RES_END:
        raise SystemExit(f"split_tables: the region ({sectors} sectors) would reach the project's store at {RES_END:#x}")
    (ROOT / "firmware" / "res").mkdir(exist_ok=True)
    (ROOT / "firmware" / "res" / "mi_tables.bin").write_bytes(bytes(payload))
    base = XIP(RES_BASE + RES_HEAD)

    head = [
        "/* SPDX-License-Identifier: MPL-2.0 */",
        "/* generated by tools/split_tables.py: the modules' biggest tables (Mutable Instruments', the FM drums') in fm1-base's resource region */",
        "#pragma once",
        f"#define MIFM1_RES_BASE {RES_BASE:#x}u             /* app.h APP_RES_BASE */",
        f"#define MIFM1_RES_SECTORS {sectors}u",
        f"#define MIFM1_RES_LEN {len(payload)}u               /* the payload: firmware/res/mi_tables.bin */",
        f"/* the payload reads in place at {base:#010x} (FM1_XIP): the moved symbols' base (mi/res_symbols.cc) */",
        f"#define MIFM1_RES_TABLES {len(entries)}",
        "",
    ]
    (ROOT / "firmware" / "mi" / "res_tables.h").write_text("\n".join(head))

    syms = [
        "// SPDX-License-Identifier: MPL-2.0",
        "// generated by tools/split_tables.py: the moved tables, as the app sees them. On the FM-1 their symbols are",
        "// res_abs.cc's (addresses in the resource region); on a computer the arrays stay in the build, and the region's",
        "// file is checked against them.",
        "#include <stddef.h>",
        "#include <stdint.h>",
        '#include "res_tables.h"',
        "",
    ]
    absu = [
        "// SPDX-License-Identifier: MPL-2.0",
        "// generated by tools/split_tables.py: on the FM-1, the moved tables' symbols at their addresses in the resource",
        "// region (absolute symbols; the arrays are not compiled there). Alone in their unit: JieLi's assembler encodes",
        "// a reference it resolves itself, in the unit that defines the symbol, with the immediate's halfwords swapped",
        "// (r0 = 0x02093020 written as 0x30200209); every reference must be a relocation the linker resolves",
        "// (tools/check_res_links.py checks the firmware).",
        "#ifndef PLAT_HOST",
        "__asm__(",
    ]
    for ns, name, ctype, off, count in entries:
        sym = mangle(ns, name)
        absu.append(f'    ".globl {sym}\\n.set {sym}, {base + off:#010x}\\n"')
    absu += [");", "#endif", ""]
    (ROOT / "firmware" / "mi" / "res_abs.cc").write_text("\n".join(absu))
    (ROOT / "firmware" / "res" / "symbols.txt").write_text(
        "".join(f"{mangle(ns, name)} {base + off:#010x}\n" for ns, name, ctype, off, count in entries))
    for module_ns in sorted({e[0] for e in entries}):
        if module_ns:
            syms.append(f"namespace {module_ns} {{")
        for ns, name, ctype, off, count in entries:
            if ns == module_ns:
                syms.append(f"extern const {ctype} {name}[];")
        if module_ns:
            syms.append("}")
    first = entries[0]
    assert first[3] == 0, "the first moved table starts the payload"
    syms += [
        "",
        "// the first moved table (offset 0): on the FM-1 the region's payload, where plat_res must point (app.c)",
        f'extern "C" const void* mi_res_where(void) {{ return {first[0]}::{first[1]}; }}',
        "",
        "#ifdef PLAT_HOST",
        "namespace {", "struct Moved { const void* data; uint32_t offset, bytes; };", "const Moved kMoved[] = {"]
    for ns, name, ctype, off, count in entries:
        syms.append(f"  {{ {qualified(ns, name)}, {off}u, {count * TYPES[ctype][1]}u }},")
    syms += [
        "};",
        "}  // namespace",
        "",
        "// the region's payload against the arrays: the index of the first table that differs, or -1",
        'extern "C" int mi_res_check(const uint8_t* payload, uint32_t len) {',
        "  if (len != MIFM1_RES_LEN) return 0;",
        "  for (size_t i = 0; i < sizeof(kMoved) / sizeof(kMoved[0]); ++i) {",
        "    const uint8_t* a = static_cast<const uint8_t*>(kMoved[i].data);",
        "    for (uint32_t k = 0; k < kMoved[i].bytes; ++k) {",
        "      if (a[k] != payload[kMoved[i].offset + k]) return static_cast<int>(i);",
        "    }",
        "  }",
        "  return -1;",
        "}",
        "#endif",
        "",
    ]
    (ROOT / "firmware" / "mi" / "res_symbols.cc").write_text("\n".join(syms))
    print(f"region: {len(payload)} B in {sectors} sectors at {RES_BASE:#x} (reads at {base:#010x}), "
          f"{len(entries)} tables")


if __name__ == "__main__":
    main()
