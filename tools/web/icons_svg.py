#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""The machine's icons on the browser emulator's page: app.c's ICONS (each a few strokes on a 32-unit grid: lines,
rings, discs, boxes, as vicon draws them on the screen) written as SVG symbols into firmware/web/emu/index.html,
between its <!-- icons --> and <!-- /icons --> marks (their strokes a unit long, pathLength: the page draws them in).
Run it again when an icon changes.

  python3 tools/web/icons_svg.py"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
app = (ROOT / "firmware" / "app.c").read_text()
page_path = ROOT / "firmware" / "web" / "emu" / "index.html"

body = app[app.index("static const vop_t ICONS[NICON][IOPS] = {") + len("static const vop_t ICONS[NICON][IOPS] = {"):]
icons, depth, cur = [], 0, ""
for ch in body:                                   # the table's rows: one an icon (depth 1 .. its braces)
    if ch == "{":
        depth += 1
        if depth == 1:
            cur = ""
            continue
    elif ch == "}":
        depth -= 1
        if depth == 0:
            icons.append(cur)
            continue
        if depth < 0:
            break
    if depth >= 1:
        cur += ch
ops = [[(o, *map(int, n)) for o, *n in re.findall(r"\{'(.)',\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\}", row)]
       for row in icons]

out = ['<svg width="0" height="0" style="position:absolute" aria-hidden="true" focusable="false"><defs>']
for i, row in enumerate(ops):
    parts = []
    for op, a, b, c, d in row:
        if op == "L":
            parts.append(f'<line x1="{a}" y1="{b}" x2="{c}" y2="{d}" pathLength="1"/>')
        elif op == "C":
            parts.append(f'<circle cx="{a}" cy="{b}" r="{c}" pathLength="1"/>')
        elif op == "D":
            parts.append(f'<circle cx="{a}" cy="{b}" r="{c}" fill="currentColor" stroke="none"/>')
        else:
            parts.append(f'<rect x="{a}" y="{b}" width="{c}" height="{d}" pathLength="1"/>')
    out.append(f'<symbol id="ic{i}" viewBox="0 0 32 32">{"".join(parts)}</symbol>')
out.append("</defs></svg>")
page = page_path.read_text()
a, b = page.index("<!-- icons -->") + len("<!-- icons -->"), page.index("<!-- /icons -->")
page_path.write_text(page[:a] + "\n" + "\n".join(out) + "\n" + page[b:])
print(f"{len(ops)} icons")
