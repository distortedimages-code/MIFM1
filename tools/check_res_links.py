#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1: the firmware's references to the moved tables (tools/split_tables.py) are the region's addresses.

JieLi's assembler writes a reference it resolves itself (the symbol absolute, in the same unit) with the 32-bit
immediate's halfwords swapped: 0x02093020 becomes 0x30200209, and the code reads elsewhere. The linker's relocations
are right. This reads the linked ELF: no word anywhere in the image (code or data, at any byte offset) is a moved
address with its halfwords swapped; mi_res_where (app.c's check) holds the first table's address; and the data that
points at the tables (Marbles' distributions_table...) points at their addresses.

    tools/check_res_links.py firmware/build/mifm1.elf firmware/res/symbols.txt"""
import struct
import sys


def elf(path):
    """the allocated sections (name, addr, bytes, code?) and the symbols {name: (value, size)} of a 32-bit LE ELF
    file"""
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
        raise SystemExit(f"{path}: not a 32-bit little-endian ELF file")
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
    sh = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
    def cstr(off):
        return d[off:d.index(b"\0", off)].decode("ascii", "replace")
    names = [cstr(sh[shstrndx][4] + s[0]) for s in sh]
    secs, syms = [], {}
    for i, (name, typ, flags, addr, off, size, link, info, align, entsize) in enumerate(sh):
        if flags & 2 and typ != 8:                    # SHF_ALLOC, not NOBITS
            secs.append((names[i], addr, d[off:off + size], bool(flags & 4)))   # SHF_EXECINSTR
        if typ == 2:                                  # SYMTAB
            stroff = sh[link][4]
            for k in range(size // 16):
                st_name, value, ssize, sinfo, other, shndx = struct.unpack_from("<IIIBBH", d, off + k * 16)
                syms[cstr(stroff + st_name)] = (value, ssize)
    return secs, syms


def read(secs, addr, n):
    for _, a, b, _ in secs:
        if a <= addr and addr + n <= a + len(b):
            return b[addr - a:addr - a + n]
    return None


def main(elf_path, sym_path):
    moved = {}
    for line in open(sym_path):
        name, addr = line.split()
        moved[name] = int(addr, 16)
    secs, syms = elf(elf_path)
    bad, errors = {}, []
    for name, a in moved.items():
        swapped = ((a & 0xFFFF) << 16) | (a >> 16)
        bad[struct.pack("<I", swapped)] = name
        if syms.get(name, (None,))[0] != a:
            errors.append(f"{name}: at {syms.get(name, ('none',))[0]}, not {a:#010x}")
    for sname, addr, blob, code in secs:   # code: at every halfword (an instruction's immediate); data: every word
        step = 2 if code else 4               # (two right addresses side by side make a swapped one between them)
        for i in range((-addr) % step, len(blob) - 3, step):
            name = bad.get(blob[i:i + 4])
            if name:
                errors.append(f"{sname} {addr + i:#010x}: {name}'s address with its halfwords swapped")
    first = min(moved.values())
    v, n = syms.get("mi_res_where", (0, 0))
    body = read(secs, v & ~1, max(n, 8)) or b""
    if struct.pack("<I", first) not in body:
        errors.append(f"mi_res_where ({v:#010x}, {body.hex(' ')}): not {first:#010x}")
    refs = 0                                          # the words in the image that are a moved address, as they should be
    for sname, addr, blob, code in secs:
        for a in moved.values():
            refs += blob.count(struct.pack("<I", a))
    table = syms.get("_ZN7marbles18distributions_tableE")
    if table:
        words = read(secs, table[0], 4) or b""
        if struct.pack("<I", moved["_ZN7marbles13dist_icdf_0_0E"]) != words:
            errors.append(f"marbles::distributions_table[0] is {words.hex()}, not dist_icdf_0_0's address")
    for e in errors:
        print(e)
    print(f"moved tables: {len(moved)} symbols, {refs} references in the image as their addresses, "
          f"{'none swapped' if not errors else f'{len(errors)} errors'}")
    return 1 if errors else 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    sys.exit(main(*sys.argv[1:]))
