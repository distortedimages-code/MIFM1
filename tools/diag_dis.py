#!/usr/bin/env python3
"""The device disassembly (objdump -d): for each function, the helpers it calls (compiler-rt's __*),
and how many instructions it has. Usage: dis_calls.py mifm1.dis [name-substring ...]"""
import re
import sys
from collections import Counter, defaultdict

dis = open(sys.argv[1], encoding="utf-8", errors="replace").read().splitlines()
want = [a for a in sys.argv[2:] if not a.startswith("+")]
dump = [a[1:] for a in sys.argv[2:] if a.startswith("+")]
FN = re.compile(r"^([0-9a-f]+) <(.+)>:$")
CALL = re.compile(r"<(__[A-Za-z0-9_]+)(?:\+0x[0-9a-f]+)?>")
cur = None
calls = defaultdict(Counter)
size = Counter()
for ln in dis:
    m = FN.match(ln)
    if m:
        cur = m.group(2)
        continue
    if cur is None or not ln.strip():
        continue
    size[cur] += 1
    for h in CALL.findall(ln):
        if h.startswith("__") and not h.startswith("__cxa"):
            calls[cur][h] += 1
helpers = Counter()
for f, c in calls.items():
    for h, n in c.items():
        helpers[h] += n
print("helpers called anywhere:", ", ".join(f"{h} x{n}" for h, n in helpers.most_common()))
for f in sorted(size, key=lambda f: -size[f]):
    if want and not any(w in f for w in want):
        continue
    c = calls.get(f)
    print(f"{size[f]:6d}  {f[:90]}  " + (", ".join(f"{h} x{n}" for h, n in c.most_common()) if c else ""))

cur = None
for ln in dis:
    m = FN.match(ln)
    if m:
        cur = m.group(2)
        if any(d in cur for d in dump):
            print("=" * 20, cur)
        continue
    if cur is not None and any(d in cur for d in dump):
        print(ln)
