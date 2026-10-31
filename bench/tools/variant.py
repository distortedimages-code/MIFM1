#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""MIFM1's build variant (fm1.toml gen): which modules this firmware holds, from MIFM1_VARIANT in the environment
(1: Plaits, Rings, Tides, Marbles; 2: Clouds, Warps and Plaits for the busy scenes; 0: all, too big for the FM-1's
flash). Writes variant.h for mi/scenes.cc and app.c.

    tools/variant.py --out GEN/variant.h"""
import os
import sys
from pathlib import Path

out = Path(sys.argv[sys.argv.index("--out") + 1])
v = os.environ.get("MIFM1_VARIANT", "1")
if v not in ("0", "1", "2"):
    raise SystemExit(f"MIFM1_VARIANT={v!r}: 0, 1 or 2")
text = f"/* tools/variant.py: MIFM1_VARIANT={v} */\n#define MI_VARIANT {v}\n"
if not out.exists() or out.read_text() != text:
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text)
print(f"variant  {v}")
