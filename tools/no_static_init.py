#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1: no C++ static initializer in the modules' objects. fm1-base never runs .init_array (nor .ctors): an object
whose initial value is computed at start-up (a namespace-scope `const float k = sqrtf(n);` the compiler did not fold)
would stay 0 on the FM-1, silently. Each ELF object given is read for those sections; any with bytes is an error.

    tools/no_static_init.py build/o2/*.o"""
import struct
import sys


def sections(path):
    """(name, size) of a 32- or 64-bit little-endian ELF object's sections"""
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF" or d[5] != 1:
        raise SystemExit(f"{path}: not a little-endian ELF file")
    if d[4] == 1:
        shoff, = struct.unpack_from("<I", d, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
        fmt, name_at, size_at, off_at = "<I", 0, 0x14, 0x10
    else:
        shoff, = struct.unpack_from("<Q", d, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x3A)
        fmt, name_at, size_at, off_at = "<Q", 0, 0x20, 0x18
    def field(i, at, f):
        return struct.unpack_from(f, d, shoff + i * shentsize + at)[0]
    strtab = field(shstrndx, off_at, fmt)
    out = []
    for i in range(shnum):
        n = field(i, name_at, "<I")
        name = d[strtab + n:d.index(b"\0", strtab + n)].decode("ascii", "replace")
        out.append((name, field(i, size_at, fmt)))
    return out


def main(paths):
    bad = [(p, n, s) for p in paths for n, s in sections(p)
           if s and (n.startswith(".init_array") or n.startswith(".ctors") or n.startswith(".preinit_array"))]
    for p, n, s in bad:
        print(f"{p}: {n} ({s} B): a static initializer, which the FM-1 never runs")
    print(f"static initializers: {'none' if not bad else len(bad)} in {len(paths)} objects")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
